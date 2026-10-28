#include "console.h"

#include "utf8.h"

#include <algorithm>
#include <cstdarg>
#include <iostream>
#include <iterator>
#include <sstream>

namespace {

constexpr size_t kHistoryMax = 500;
// The input thread wakes up this often without keys, to redraw after a resize.
constexpr int kResizePollMs = 250;
constexpr long kWheelRows = 3;

std::string vformat(const char* format, va_list args) {
    va_list copy;
    va_copy(copy, args);
    const int n = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (n <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(n), '\0');
    std::vsnprintf(out.data(), out.size() + 1, format, args);
    return out;
}

std::vector<std::string> split(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> words;
    for (std::string w; in >> w;) {
        words.push_back(w);
    }
    return words;
}

std::string commonPrefix(const std::vector<std::string>& words) {
    std::string prefix = words.front();
    for (const std::string& w : words) {
        const auto end = std::mismatch(prefix.begin(), prefix.end(), w.begin(), w.end()).first;
        prefix.erase(end, prefix.end());
    }
    return prefix;
}

// Control characters in what the payload sends (a STATUS_TEXT) must not move the cursor
// around the full screen.
std::string printable(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        out += c == '\n' || (u >= 0x20 && u != 0x7F) ? c : (c == '\t' ? ' ' : '?');
    }
    return out;
}

std::string fit(const std::string& s, size_t width) {
    return s.substr(0, advance(s, 0, width));
}

std::string moveTo(size_t row, size_t column = 1) {
    return strprintf("\x1b[%zu;%zuH", row, column);
}

// The full-screen rows, top to bottom: output, a separator, the last commands, the input line.
struct Layout {
    size_t output;
    size_t recent;
    bool separator;
};

Layout layoutFor(size_t rows) {
    const size_t recent = rows >= 16 ? 3 : rows >= 10 ? 1 : 0;
    const bool separator = rows >= 4;
    const size_t fixed = recent + (separator ? 1 : 0) + 1;
    return {rows > fixed ? rows - fixed : 0, recent, separator};
}

} // namespace

std::string strprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::string out = vformat(format, args);
    va_end(args);
    return out;
}

Console::~Console() {
    stop();
}

bool Console::logTo(const std::string& path) {
    std::lock_guard lock(mutex_);
    log_.reset(std::fopen(path.c_str(), "w"));
    return log_ != nullptr;
}

void Console::start(std::string prompt, Completer completer, View view) {
    prompt_ = std::move(prompt);
    completer_ = std::move(completer);
    if (!terminal::available()) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (!terminal::enterRaw()) {
        return;
    }
    historyPos_ = history_.size();
    if (view == View::Screen) {
        terminal::enterScreen();
        mode_ = Mode::Screen;
        drawScreen();
    } else {
        mode_ = Mode::Inline;
        showLine();
    }
}

void Console::stop() {
    std::lock_guard lock(mutex_);
    if (mode_ == Mode::Screen) {
        terminal::leaveScreen();
    } else if (mode_ == Mode::Inline) {
        hideLine();
        std::fflush(stdout);
    } else {
        return;
    }
    terminal::leaveRaw();
    mode_ = Mode::Plain;
}

void Console::print(const std::string& text) {
    std::lock_guard lock(mutex_);
    emit(text, false);
}

void Console::printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    const std::string text = vformat(format, args);
    va_end(args);
    print(text);
}

void Console::emit(const std::string& text, bool follow) {
    if (log_ != nullptr) {
        std::fputs(text.c_str(), log_.get());
        std::fflush(log_.get());
    }
    switch (mode_) {
    case Mode::Plain:
        std::fputs(text.c_str(), stdout);
        std::fflush(stdout);
        break;
    case Mode::Inline:
        hideLine();
        std::fwrite(text.data(), 1, text.size(), stdout);
        if (!text.empty() && text.back() != '\n') {
            std::fputc('\n', stdout);
        }
        showLine();
        break;
    case Mode::Screen:
        output_.append(printable(text));
        if (follow) {
            output_.follow();
        }
        drawScreen();
        break;
    }
}

bool Console::readLine(std::string& line) {
    // Only this thread changes mode_ and buffer_, so it reads them without the lock.
    if (mode_ == Mode::Plain) {
        if (!std::getline(std::cin, line)) {
            return false;
        }
        std::lock_guard lock(mutex_);
        if (log_ != nullptr) {
            std::fputs((prompt_ + line + "\n").c_str(), log_.get());
            std::fflush(log_.get());
        }
        return true;
    }
    for (;;) {
        const Key key = terminal::readKey(kResizePollMs);
        switch (key.code) {
        case Key::Eof:
            return false;
        case Key::Enter:
            submit(line);
            return true;
        case Key::Interrupt:
        case Key::EndOfInput:
            // On a typed line Ctrl-C drops it, as in a shell, and Ctrl-D deletes as Delete does.
            if (buffer_.empty()) {
                return false;
            }
            edit({key.code == Key::Interrupt ? Key::KillLine : Key::Delete, {}});
            break;
        case Key::Tab:
            complete();
            break;
        case Key::Suspend:
            suspend();
            break;
        case Key::Tick: {
            std::lock_guard lock(mutex_);
            if (mode_ == Mode::Screen && terminal::size() != size_) {
                drawScreen();
            }
            break;
        }
        default:
            edit(key);
            break;
        }
    }
}

void Console::submit(std::string& line) {
    std::lock_guard lock(mutex_);
    line = buffer_;
    if (!line.empty() && (history_.empty() || history_.back() != line)) {
        history_.push_back(line);
        if (history_.size() > kHistoryMax) {
            history_.erase(history_.begin());
        }
    }
    historyPos_ = history_.size();
    draft_.clear();
    buffer_.clear();
    cursor_ = 0;
    // The command stays in the output, above what it prints, and the view goes back to live.
    emit(prompt_ + line + "\n", true);
}

bool Console::scrollKey(Key::Code code) {
    const long page = static_cast<long>(std::max<size_t>(outputRows_, 2) - 1);
    switch (code) {
    case Key::PageUp: output_.scroll(page); return true;
    case Key::PageDown: output_.scroll(-page); return true;
    case Key::WheelUp: output_.scroll(kWheelRows); return true;
    case Key::WheelDown: output_.scroll(-kWheelRows); return true;
    case Key::Escape: output_.follow(); return true;
    default: return false;
    }
}

void Console::edit(const Key& key) {
    std::lock_guard lock(mutex_);
    if (mode_ == Mode::Screen && scrollKey(key.code)) {
        drawScreen();
        return;
    }
    switch (key.code) {
    case Key::Text:
        buffer_.insert(cursor_, key.text);
        cursor_ += key.text.size();
        break;
    case Key::Backspace:
        if (cursor_ > 0) {
            const size_t from = previous(buffer_, cursor_);
            buffer_.erase(from, cursor_ - from);
            cursor_ = from;
        }
        break;
    case Key::Delete:
        buffer_.erase(cursor_, advance(buffer_, cursor_, 1) - cursor_);
        break;
    case Key::Left: cursor_ = previous(buffer_, cursor_); break;
    case Key::Right: cursor_ = advance(buffer_, cursor_, 1); break;
    case Key::Home: cursor_ = 0; break;
    case Key::End:
        cursor_ = buffer_.size();
        // In the full screen, End also brings the output back to live.
        if (mode_ == Mode::Screen && !output_.following()) {
            output_.follow();
            drawScreen();
            return;
        }
        break;
    case Key::Up: historyMove(-1); break;
    case Key::Down: historyMove(1); break;
    case Key::KillToEnd: buffer_.erase(cursor_); break;
    case Key::KillToStart:
        buffer_.erase(0, cursor_);
        cursor_ = 0;
        break;
    case Key::KillLine:
        buffer_.clear();
        cursor_ = 0;
        break;
    case Key::KillWord: {
        size_t from = cursor_;
        while (from > 0 && buffer_[from - 1] == ' ') {
            --from;
        }
        while (from > 0 && buffer_[from - 1] != ' ') {
            --from;
        }
        buffer_.erase(from, cursor_ - from);
        cursor_ = from;
        break;
    }
    case Key::ClearScreen:
        if (mode_ == Mode::Screen) {
            drawScreen();
            return;
        }
        std::fputs("\x1b[H\x1b[2J", stdout);
        break;
    default: return;
    }
    showLine();
}

void Console::historyMove(int step) {
    if (step < 0 && historyPos_ > 0) {
        if (historyPos_ == history_.size()) {
            draft_ = buffer_;
        }
        buffer_ = history_[--historyPos_];
    } else if (step > 0 && historyPos_ < history_.size()) {
        ++historyPos_;
        buffer_ = historyPos_ == history_.size() ? draft_ : history_[historyPos_];
    }
    cursor_ = buffer_.size();
}

// Completes the word before the cursor: a single match whole, several up to what they share;
// when that adds nothing, lists them (or shows the command's usage) in the output.
void Console::complete() {
    std::string before;
    {
        std::lock_guard lock(mutex_);
        before = buffer_.substr(0, cursor_);
    }
    const size_t space = before.find_last_of(' ');
    const size_t wordStart = space == std::string::npos ? 0 : space + 1;
    const std::string word = before.substr(wordStart);
    // Asked without the lock: the completer reads the session, which may be printing.
    const Completion offer =
        completer_ ? completer_(split(before.substr(0, wordStart))) : Completion{};
    std::vector<std::string> matches;
    std::copy_if(offer.words.begin(), offer.words.end(), std::back_inserter(matches),
                 [&](const std::string& w) { return w.compare(0, word.size(), word) == 0; });
    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    const auto show = [this](const std::string& text) {
        std::lock_guard lock(mutex_);
        emit(text, true);
    };
    if (matches.empty()) {
        if (!offer.hint.empty()) {
            show(offer.hint + "\n");
        }
        return;
    }
    if (matches.size() == 1) {
        edit({Key::Text, matches.front().substr(word.size()) + " "});
        return;
    }
    if (const std::string shared = commonPrefix(matches); shared.size() > word.size()) {
        edit({Key::Text, shared.substr(word.size())});
        return;
    }
    size_t width = 0;
    for (const std::string& m : matches) {
        width = std::max(width, columns(m) + 2);
    }
    const size_t perRow = std::max<size_t>(1, terminal::size().cols / width);
    std::string list;
    for (size_t i = 0; i < matches.size(); ++i) {
        list += matches[i];
        const bool rowEnd = (i + 1) % perRow == 0 || i + 1 == matches.size();
        list += rowEnd ? std::string("\n") : std::string(width - columns(matches[i]), ' ');
    }
    show(list);
}

void Console::suspend() {
    Mode resumed = Mode::Plain;
    {
        std::lock_guard lock(mutex_);
        if (mode_ == Mode::Screen) {
            terminal::leaveScreen();
        } else {
            hideLine();
            std::fflush(stdout);
        }
        terminal::leaveRaw();
        std::swap(resumed, mode_);
    }
    terminal::suspendProcess();
    // Back with fg.
    std::lock_guard lock(mutex_);
    if (!terminal::enterRaw()) {
        return;
    }
    mode_ = resumed;
    if (mode_ == Mode::Screen) {
        terminal::enterScreen();
        drawScreen();
    } else {
        showLine();
    }
}

void Console::hideLine() {
    std::fputs("\r\x1b[2K", stdout);
}

// The prompt and the part of the line around the cursor that fits: the input line never
// wraps, so redrawing it never leaves a stray row behind.
std::string Console::inputRow(size_t width, size_t& cursorColumn) const {
    const size_t promptColumns = columns(prompt_);
    const size_t room = width > promptColumns + 1 ? width - promptColumns - 1 : 1;
    const size_t column = columns(buffer_, 0, cursor_);
    const size_t first = column >= room ? column - room + 1 : 0;
    const size_t from = advance(buffer_, 0, first);
    const size_t to = advance(buffer_, from, room);
    cursorColumn = promptColumns + column - first;
    return prompt_ + buffer_.substr(from, to - from);
}

void Console::showLine() {
    size_t column = 0;
    std::string out;
    if (mode_ == Mode::Screen) {
        out = moveTo(size_.rows) + "\x1b[2K" + inputRow(size_.cols, column) +
              moveTo(size_.rows, column + 1);
    } else if (mode_ == Mode::Inline) {
        out = "\r" + inputRow(terminal::size().cols, column) + "\x1b[K\r";
        if (column > 0) {
            out += strprintf("\x1b[%zuC", column);
        }
    } else {
        return;
    }
    std::fputs(out.c_str(), stdout);
    std::fflush(stdout);
}

std::string Console::separator(size_t width) const {
    std::string line;
    if (output_.following()) {
        line = "\x1b[2m── wheel, PgUp/PgDn: scroll back · Tab: complete · ↑↓: history ";
    } else if (output_.unseen() > 0) {
        line = strprintf("\x1b[1;33m── ↓ %zu new lines · End or Esc: back to live ",
                         output_.unseen());
    } else {
        line = "\x1b[1;33m── scrolled back · End or Esc: back to live ";
    }
    const size_t styleBytes = line.find("──");
    std::string text = line.substr(styleBytes);
    for (size_t used = columns(text); used < width; ++used) {
        text += "─";
    }
    return line.substr(0, styleBytes) + fit(text, width) + "\x1b[0m";
}

// The whole full-screen view in one write, inside a synchronized update where the terminal
// supports it, so it does not flicker.
void Console::drawScreen() {
    size_ = terminal::size();
    const Layout layout = layoutFor(size_.rows);
    outputRows_ = layout.output;
    std::string out = "\x1b[?2026h";
    const std::vector<std::string> rows = output_.visible(size_.cols, layout.output);
    for (size_t r = 0; r < layout.output; ++r) {
        out += moveTo(r + 1) + "\x1b[2K";
        if (r < rows.size()) {
            out += rows[r];
        }
    }
    size_t row = layout.output + 1;
    if (layout.separator) {
        out += moveTo(row++) + "\x1b[2K" + separator(size_.cols);
    }
    for (size_t back = layout.recent; back > 0; --back) {
        const std::string command =
            history_.size() >= back ? history_[history_.size() - back] : std::string();
        out += moveTo(row++) + "\x1b[2K\x1b[2m" + fit("  " + command, size_.cols) + "\x1b[0m";
    }
    size_t column = 0;
    out += moveTo(row) + "\x1b[2K" + inputRow(size_.cols, column) + moveTo(row, column + 1);
    out += "\x1b[?2026l";
    std::fputs(out.c_str(), stdout);
    std::fflush(stdout);
}

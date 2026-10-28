#include "console.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <sstream>

#if !defined(_WIN32)
#include <cerrno>
#include <csignal>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

constexpr size_t kHistoryMax = 500;
// Bytes of an escape sequence or a UTF-8 character arrive together, over ssh too; a lone Esc
// does not.
constexpr int kSequenceTimeoutMs = 100;

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

bool isContinuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// Terminal columns taken by s[from, to): one per UTF-8 character.
size_t columns(const std::string& s, size_t from, size_t to) {
    size_t n = 0;
    for (size_t i = from; i < to; ++i) {
        n += isContinuation(s[i]) ? 0 : 1;
    }
    return n;
}

// Byte offset `count` characters after `from`.
size_t advance(const std::string& s, size_t from, size_t count) {
    size_t i = from;
    for (; i < s.size() && count > 0; --count) {
        ++i;
        while (i < s.size() && isContinuation(s[i])) {
            ++i;
        }
    }
    return i;
}

size_t previous(const std::string& s, size_t pos) {
    if (pos == 0) {
        return 0;
    }
    --pos;
    while (pos > 0 && isContinuation(s[pos])) {
        --pos;
    }
    return pos;
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

#if !defined(_WIN32)
termios gSaved{};
bool gRaw = false;

bool enterRaw() {
    termios raw = gSaved;
    raw.c_iflag &= static_cast<tcflag_t>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
    // ISIG off: Ctrl-C and Ctrl-Z arrive as keys, so quitting still closes the session and
    // the --tlog cleanly.
    raw.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON | IEXTEN | ISIG));
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    // Output processing stays on: "\n" still returns the carriage.
    gRaw = tcsetattr(STDIN_FILENO, TCSADRAIN, &raw) == 0;
    return gRaw;
}

void leaveRaw() {
    if (gRaw) {
        tcsetattr(STDIN_FILENO, TCSADRAIN, &gSaved);
        gRaw = false;
    }
}

// kill or a closed terminal must not leave the shell without echo.
extern "C" void onTerminate(int sig) {
    tcsetattr(STDIN_FILENO, TCSADRAIN, &gSaved);
    static const char kNewLine[] = "\r\n";
    (void)!write(STDOUT_FILENO, kNewLine, sizeof(kNewLine) - 1);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

// A byte from stdin; -1 at end of input, -2 when none arrives within timeoutMs (< 0: wait).
int readByte(int timeoutMs) {
    if (timeoutMs >= 0) {
        pollfd p{STDIN_FILENO, POLLIN, 0};
        if (poll(&p, 1, timeoutMs) <= 0) {
            return -2;
        }
    }
    for (;;) {
        unsigned char c = 0;
        const ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n == 1) {
            return c;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return -1;
    }
}

size_t terminalColumns() {
    winsize w{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
        return w.ws_col;
    }
    return 80;
}
#endif

} // namespace

std::string strprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::string out = vformat(format, args);
    va_end(args);
    return out;
}

struct Console::Key {
    enum Code {
        Ignore, Text, Enter, Tab, Backspace, Delete, Left, Right, Home, End, Up, Down,
        KillToEnd, KillToStart, KillWord, KillLine, ClearScreen, Interrupt, EndOfInput, Suspend, Eof
    };
    Code code = Ignore;
    std::string text;
};

namespace {

#if !defined(_WIN32)
Console::Key::Code readEscape();

// One key press, decoded; called without the console's lock, it may block.
Console::Key readKey() {
    using K = Console::Key;
    const int c = readByte(-1);
    switch (c) {
    case -1: return {K::Eof, {}};
    case '\r': case '\n': return {K::Enter, {}};
    case '\t': return {K::Tab, {}};
    case 127: case 8: return {K::Backspace, {}};
    case 1: return {K::Home, {}};       // Ctrl-A
    case 5: return {K::End, {}};        // Ctrl-E
    case 2: return {K::Left, {}};       // Ctrl-B
    case 6: return {K::Right, {}};      // Ctrl-F
    case 16: return {K::Up, {}};        // Ctrl-P
    case 14: return {K::Down, {}};      // Ctrl-N
    case 11: return {K::KillToEnd, {}}; // Ctrl-K
    case 21: return {K::KillToStart, {}}; // Ctrl-U
    case 23: return {K::KillWord, {}};  // Ctrl-W
    case 12: return {K::ClearScreen, {}}; // Ctrl-L
    case 3: return {K::Interrupt, {}};  // Ctrl-C
    case 4: return {K::EndOfInput, {}}; // Ctrl-D
    case 26: return {K::Suspend, {}};   // Ctrl-Z
    case 27: return {readEscape(), {}};
    default: break;
    }
    if (c < 0x20 || (c >= 0x80 && c < 0xC0)) {
        return {K::Ignore, {}};
    }
    // A UTF-8 character is inserted whole, so the cursor never splits one.
    std::string text(1, static_cast<char>(c));
    const int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    for (int i = 0; i < more; ++i) {
        const int b = readByte(kSequenceTimeoutMs);
        if (b < 0 || !isContinuation(static_cast<char>(b))) {
            return {K::Ignore, {}};
        }
        text += static_cast<char>(b);
    }
    return {K::Text, text};
}

// CSI and SS3 sequences of the arrows, Home, End and Delete, as xterm, macOS Terminal,
// iTerm2 and the Linux console send them.
Console::Key::Code readEscape() {
    using K = Console::Key;
    const int intro = readByte(kSequenceTimeoutMs);
    if (intro != '[' && intro != 'O') {
        return K::Ignore;
    }
    std::string params;
    int terminator = -1;
    for (int i = 0; i < 16; ++i) {
        const int b = readByte(kSequenceTimeoutMs);
        if (b < 0) {
            break;
        }
        if (b >= 0x40 && b <= 0x7E) {
            terminator = b;
            break;
        }
        params += static_cast<char>(b);
    }
    switch (terminator) {
    case 'A': return K::Up;
    case 'B': return K::Down;
    case 'C': return K::Right;
    case 'D': return K::Left;
    case 'H': return K::Home;
    case 'F': return K::End;
    case '~':
        switch (std::atoi(params.c_str())) {
        case 1: case 7: return K::Home;
        case 4: case 8: return K::End;
        case 3: return K::Delete;
        default: return K::Ignore;
        }
    default: return K::Ignore;
    }
}
#endif

} // namespace

Console::~Console() {
    stop();
}

void Console::start(std::string prompt, Completer completer) {
    prompt_ = std::move(prompt);
    completer_ = std::move(completer);
#if !defined(_WIN32)
    const char* term = std::getenv("TERM");
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) ||
        (term != nullptr && std::strcmp(term, "dumb") == 0) ||
        tcgetattr(STDIN_FILENO, &gSaved) != 0) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (!enterRaw()) {
        return;
    }
    std::signal(SIGTERM, onTerminate);
    std::signal(SIGHUP, onTerminate);
    interactive_ = true;
    historyPos_ = history_.size();
    showLine();
#endif
}

void Console::stop() {
    std::lock_guard lock(mutex_);
    if (!interactive_) {
        return;
    }
    hideLine();
    std::fflush(stdout);
#if !defined(_WIN32)
    leaveRaw();
    std::signal(SIGTERM, SIG_DFL);
    std::signal(SIGHUP, SIG_DFL);
#endif
    interactive_ = false;
}

void Console::print(const std::string& text) {
    std::lock_guard lock(mutex_);
    if (!interactive_) {
        std::fputs(text.c_str(), stdout);
        std::fflush(stdout);
        return;
    }
    hideLine();
    std::fwrite(text.data(), 1, text.size(), stdout);
    if (!text.empty() && text.back() != '\n') {
        std::fputc('\n', stdout);
    }
    showLine();
}

void Console::printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    const std::string text = vformat(format, args);
    va_end(args);
    print(text);
}

bool Console::readLine(std::string& line) {
    if (!interactive_) {
        return static_cast<bool>(std::getline(std::cin, line));
    }
#if !defined(_WIN32)
    for (;;) {
        const Key key = readKey();
        switch (key.code) {
        case Key::Eof:
            return false;
        case Key::Enter:
            submit(line);
            return true;
        case Key::Interrupt:
        case Key::EndOfInput:
            // Only this thread changes buffer_. On a typed line Ctrl-C drops it, as in a
            // shell, and Ctrl-D deletes as Delete does.
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
        default:
            edit(key);
            break;
        }
    }
#else
    return false;
#endif
}

void Console::submit(std::string& line) {
    std::lock_guard lock(mutex_);
    hideLine();
    // The command stays in the output, above what it prints.
    std::fputs((prompt_ + buffer_ + "\n").c_str(), stdout);
    line = buffer_;
    if (!buffer_.empty() && (history_.empty() || history_.back() != buffer_)) {
        history_.push_back(buffer_);
        if (history_.size() > kHistoryMax) {
            history_.erase(history_.begin());
        }
    }
    historyPos_ = history_.size();
    draft_.clear();
    buffer_.clear();
    cursor_ = 0;
    showLine();
}

void Console::edit(const Key& key) {
    std::lock_guard lock(mutex_);
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
    case Key::End: cursor_ = buffer_.size(); break;
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
    case Key::ClearScreen: std::fputs("\x1b[H\x1b[2J", stdout); break;
    default: return;
    }
    showLine();
}

void Console::insert(const std::string& text) {
    edit({Key::Text, text});
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
// when that adds nothing, lists them (or shows the command's usage) above the input line.
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
    if (matches.empty()) {
        if (!offer.hint.empty()) {
            print(offer.hint + "\n");
        }
        return;
    }
    if (matches.size() == 1) {
        insert(matches.front().substr(word.size()) + " ");
        return;
    }
    if (const std::string shared = commonPrefix(matches); shared.size() > word.size()) {
        insert(shared.substr(word.size()));
        return;
    }
    size_t width = 0;
    for (const std::string& m : matches) {
        width = std::max(width, columns(m, 0, m.size()) + 2);
    }
#if !defined(_WIN32)
    const size_t perRow = std::max<size_t>(1, terminalColumns() / width);
#else
    const size_t perRow = 1;
#endif
    std::string list;
    for (size_t i = 0; i < matches.size(); ++i) {
        list += matches[i];
        const bool rowEnd = (i + 1) % perRow == 0 || i + 1 == matches.size();
        list += rowEnd ? std::string("\n")
                       : std::string(width - columns(matches[i], 0, matches[i].size()), ' ');
    }
    print(list);
}

void Console::suspend() {
#if !defined(_WIN32)
    {
        std::lock_guard lock(mutex_);
        hideLine();
        std::fflush(stdout);
        leaveRaw();
        interactive_ = false;
    }
    std::raise(SIGTSTP);
    // Back with fg.
    std::lock_guard lock(mutex_);
    if (enterRaw()) {
        interactive_ = true;
        showLine();
    }
#endif
}

void Console::hideLine() {
    std::fputs("\r\x1b[2K", stdout);
}

// The input line never wraps: when it is wider than the terminal, the part around the cursor
// shows, so hideLine() always erases all of it.
void Console::showLine() {
#if !defined(_WIN32)
    const size_t promptColumns = columns(prompt_, 0, prompt_.size());
    const size_t total = terminalColumns();
    const size_t room = total > promptColumns + 1 ? total - promptColumns - 1 : 1;
    const size_t cursorColumn = columns(buffer_, 0, cursor_);
    const size_t firstColumn = cursorColumn >= room ? cursorColumn - room + 1 : 0;
    const size_t from = advance(buffer_, 0, firstColumn);
    const size_t to = advance(buffer_, from, room);
    std::string out = "\r" + prompt_ + buffer_.substr(from, to - from) + "\x1b[K\r";
    if (const size_t right = promptColumns + cursorColumn - firstColumn; right > 0) {
        out += strprintf("\x1b[%zuC", right);
    }
    std::fputs(out.c_str(), stdout);
    std::fflush(stdout);
#endif
}

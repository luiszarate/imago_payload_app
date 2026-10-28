#include "terminal.h"

#include "utf8.h"

#if !defined(_WIN32)
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace terminal {

#if !defined(_WIN32)
namespace {

// Bytes of an escape sequence or a UTF-8 character arrive together, over ssh too; a lone Esc
// does not.
constexpr int kSequenceTimeoutMs = 100;
// SGR mouse reports (1006) of button events (1000): the wheel comes as buttons 64 and 65.
constexpr char kScreenOn[] = "\x1b[?1049h\x1b[?1000h\x1b[?1006h";
constexpr char kScreenOff[] = "\x1b[?1006l\x1b[?1000l\x1b[?1049l";

termios gSaved{};
bool gHaveSaved = false;
bool gRaw = false;
volatile std::sig_atomic_t gScreen = 0;

// kill or a closed terminal must not leave the shell without echo or on the other screen.
extern "C" void onTerminate(int sig) {
    if (gScreen != 0) {
        (void)!write(STDOUT_FILENO, kScreenOff, sizeof(kScreenOff) - 1);
    } else {
        static const char kNewLine[] = "\r\n";
        (void)!write(STDOUT_FILENO, kNewLine, sizeof(kNewLine) - 1);
    }
    tcsetattr(STDIN_FILENO, TCSADRAIN, &gSaved);
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

Key::Code mouse(const std::string& params, int terminator) {
    // "<button;x;y", M on press; 4, 8 and 16 are Shift, Alt and Ctrl.
    if (terminator != 'M' || params.empty() || params[0] != '<') {
        return Key::Ignore;
    }
    switch (std::atoi(params.c_str() + 1) & ~(4 | 8 | 16)) {
    case 64: return Key::WheelUp;
    case 65: return Key::WheelDown;
    default: return Key::Ignore;
    }
}

// CSI and SS3 sequences of the arrows, Home, End, Delete, PageUp/PageDown and the wheel, as
// xterm, macOS Terminal, iTerm2 and the Linux console send them.
Key::Code readEscape() {
    const int intro = readByte(kSequenceTimeoutMs);
    if (intro == -2) {
        return Key::Escape;
    }
    if (intro != '[' && intro != 'O') {
        return Key::Ignore;
    }
    std::string params;
    int terminator = -1;
    for (int i = 0; i < 32; ++i) {
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
    case 'A': return Key::Up;
    case 'B': return Key::Down;
    case 'C': return Key::Right;
    case 'D': return Key::Left;
    case 'H': return Key::Home;
    case 'F': return Key::End;
    case 'M':
    case 'm': return mouse(params, terminator);
    case '~':
        switch (std::atoi(params.c_str())) {
        case 1: case 7: return Key::Home;
        case 4: case 8: return Key::End;
        case 3: return Key::Delete;
        case 5: return Key::PageUp;
        case 6: return Key::PageDown;
        default: return Key::Ignore;
        }
    default: return Key::Ignore;
    }
}

Key::Code control(int c) {
    switch (c) {
    case '\r': case '\n': return Key::Enter;
    case '\t': return Key::Tab;
    case 127: case 8: return Key::Backspace;
    case 1: return Key::Home;          // Ctrl-A
    case 5: return Key::End;           // Ctrl-E
    case 2: return Key::Left;          // Ctrl-B
    case 6: return Key::Right;         // Ctrl-F
    case 16: return Key::Up;           // Ctrl-P
    case 14: return Key::Down;         // Ctrl-N
    case 11: return Key::KillToEnd;    // Ctrl-K
    case 21: return Key::KillToStart;  // Ctrl-U
    case 23: return Key::KillWord;     // Ctrl-W
    case 12: return Key::ClearScreen;  // Ctrl-L
    case 3: return Key::Interrupt;     // Ctrl-C
    case 4: return Key::EndOfInput;    // Ctrl-D
    case 26: return Key::Suspend;      // Ctrl-Z
    default: return Key::Ignore;
    }
}

} // namespace

bool available() {
    const char* term = std::getenv("TERM");
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) &&
           !(term != nullptr && std::strcmp(term, "dumb") == 0);
}

bool enterRaw() {
    if (!gHaveSaved) {
        gHaveSaved = tcgetattr(STDIN_FILENO, &gSaved) == 0;
        if (!gHaveSaved) {
            return false;
        }
    }
    termios raw = gSaved;
    raw.c_iflag &= static_cast<tcflag_t>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
    // ISIG off: Ctrl-C and Ctrl-Z arrive as keys, so quitting still closes the session and
    // the --tlog cleanly.
    raw.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON | IEXTEN | ISIG));
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    gRaw = tcsetattr(STDIN_FILENO, TCSADRAIN, &raw) == 0;
    if (gRaw) {
        std::signal(SIGTERM, onTerminate);
        std::signal(SIGHUP, onTerminate);
    }
    return gRaw;
}

void leaveRaw() {
    if (gRaw) {
        tcsetattr(STDIN_FILENO, TCSADRAIN, &gSaved);
        std::signal(SIGTERM, SIG_DFL);
        std::signal(SIGHUP, SIG_DFL);
        gRaw = false;
    }
}

void enterScreen() {
    std::fputs(kScreenOn, stdout);
    std::fflush(stdout);
    gScreen = 1;
}

void leaveScreen() {
    if (gScreen != 0) {
        std::fputs(kScreenOff, stdout);
        std::fflush(stdout);
        gScreen = 0;
    }
}

void suspendProcess() {
    std::raise(SIGTSTP);
}

Size size() {
    winsize w{};
    Size s;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0 && w.ws_row > 0) {
        s.rows = w.ws_row;
        s.cols = w.ws_col;
    }
    return s;
}

Key readKey(int timeoutMs) {
    const int c = readByte(timeoutMs);
    if (c == -2) {
        return {Key::Tick, {}};
    }
    if (c == -1) {
        return {Key::Eof, {}};
    }
    if (c == 27) {
        return {readEscape(), {}};
    }
    if (c < 0x20 || c == 127) {
        return {control(c), {}};
    }
    if (c >= 0x80 && c < 0xC0) {
        return {Key::Ignore, {}};
    }
    // A UTF-8 character is inserted whole, so the cursor never splits one.
    std::string text(1, static_cast<char>(c));
    const int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    for (int i = 0; i < more; ++i) {
        const int b = readByte(kSequenceTimeoutMs);
        if (b < 0 || !isContinuation(static_cast<char>(b))) {
            return {Key::Ignore, {}};
        }
        text += static_cast<char>(b);
    }
    return {Key::Text, text};
}

#else

bool available() { return false; }
bool enterRaw() { return false; }
void leaveRaw() {}
void enterScreen() {}
void leaveScreen() {}
Size size() { return {}; }
void suspendProcess() {}
Key readKey(int) { return {Key::Eof, {}}; }

#endif

} // namespace terminal

// Raw terminal I/O for the imago-cli console: termios, ANSI screen modes and key decoding.
// POSIX only; on Windows available() is false and the console stays line by line.

#pragma once

#include <cstddef>
#include <string>

namespace terminal {

struct Size {
    size_t rows = 24;
    size_t cols = 80;
    bool operator==(const Size&) const = default;
};

struct Key {
    enum Code {
        Ignore, Text, Enter, Tab, Backspace, Delete, Left, Right, Home, End, Up, Down,
        PageUp, PageDown, WheelUp, WheelDown, Escape,
        KillToEnd, KillToStart, KillWord, KillLine, ClearScreen,
        Interrupt, EndOfInput, Suspend, Tick, Eof
    };
    Code code = Ignore;
    std::string text; // the character, for Text
};

// stdin and stdout are terminals that take ANSI sequences.
bool available();
// No echo, no line buffering, Ctrl-C and Ctrl-Z as keys; output processing stays on, so "\n"
// still returns the carriage. Also restores the terminal on SIGTERM and SIGHUP.
bool enterRaw();
void leaveRaw();
// The alternate screen, with the mouse wheel reported as keys.
void enterScreen();
void leaveScreen();
Size size();
// Stops the process as Ctrl-Z would (the terminal must be given back first); returns on fg.
void suspendProcess();
// The next key press; Tick when none comes within timeoutMs.
Key readKey(int timeoutMs);

} // namespace terminal

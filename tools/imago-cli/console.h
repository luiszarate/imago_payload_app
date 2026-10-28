// The imago-cli terminal. The state printer and the core's event callbacks write from other
// threads while the user types; output never breaks what is being typed.
//
// Three modes:
//  - Screen (default in a terminal): full screen, an output pane that scrolls on its own
//    (wheel, PageUp/PageDown) above a fixed pane with the last commands and the input line.
//  - Inline (--plain): the terminal scrolls as usual and the input line stays at the bottom:
//    print() erases it, writes the text and draws it again.
//  - Plain (stdin or stdout not a terminal, or Windows): lines read and printed as they come,
//    for pipes and scripts.

#pragma once

#include "output_pane.h"
#include "terminal.h"

#include <cstddef>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if defined(__GNUC__)
#define IMAGO_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define IMAGO_PRINTF(fmt, args)
#endif

std::string strprintf(const char* format, ...) IMAGO_PRINTF(1, 2);

// What Tab can offer for the word being typed: words to complete it with, or, when there
// are none, a usage line to show instead.
struct Completion {
    std::vector<std::string> words;
    std::string hint;
};

class Console {
public:
    enum class View { Screen, Inline };
    // Gets the words typed before the one under completion.
    using Completer = std::function<Completion(const std::vector<std::string>& before)>;

    Console() = default;
    ~Console();
    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

    // Copies everything printed, and the commands typed, to a file. False if it cannot open it.
    bool logTo(const std::string& path);
    // Takes over the terminal when stdin and stdout are terminals; plain mode otherwise.
    void start(std::string prompt, Completer completer, View view);
    // Gives the terminal back as it was; later output is printed plainly.
    void stop();

    // Thread-safe. One call is printed as one block.
    void print(const std::string& text);
    void printf(const char* format, ...) IMAGO_PRINTF(2, 3);

    // One line typed by the user. False at end of input: EOF, or Ctrl-C / Ctrl-D on an
    // empty line.
    bool readLine(std::string& line);

private:
    enum class Mode { Plain, Inline, Screen };
    using Key = terminal::Key;

    void edit(const Key& key);
    void complete();
    void suspend();
    void submit(std::string& line);
    // Callers hold mutex_.
    void emit(const std::string& text, bool follow);
    void historyMove(int step);
    bool scrollKey(Key::Code code);
    void hideLine();
    void showLine();
    void drawScreen();
    std::string inputRow(size_t width, size_t& cursorColumn) const;
    std::string separator(size_t width) const;

    std::mutex mutex_;
    Mode mode_ = Mode::Plain;
    struct FileCloser {
        void operator()(FILE* f) const { std::fclose(f); }
    };
    std::unique_ptr<FILE, FileCloser> log_;
    std::string prompt_;
    Completer completer_;
    OutputPane output_;
    terminal::Size size_;
    size_t outputRows_ = 0; // of the last drawScreen()
    // The line being typed; only the input thread changes it, under mutex_.
    std::string buffer_;
    size_t cursor_ = 0; // byte offset, always at the start of a UTF-8 character
    std::vector<std::string> history_;
    size_t historyPos_ = 0;
    std::string draft_; // the unfinished line while browsing the history
};

// The imago-cli terminal: output scrolls above an input line that stays at the bottom.
//
// The state printer and the core's event callbacks write from other threads while the user
// types; print() erases the input line, writes the text and draws the line again, so output
// never breaks what is being typed. Interactive only when stdin and stdout are terminals
// (not on Windows); otherwise lines are read and printed plainly, for pipes and scripts.

#pragma once

#include <cstddef>
#include <functional>
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
    // Gets the words typed before the one under completion.
    using Completer = std::function<Completion(const std::vector<std::string>& before)>;

    Console() = default;
    ~Console();
    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

    // Takes over the terminal when stdin and stdout are terminals; plain mode otherwise.
    void start(std::string prompt, Completer completer);
    // Gives the terminal back as it was; later output is printed plainly.
    void stop();

    // Thread-safe. Text should end with a newline; one call is printed as one block.
    void print(const std::string& text);
    void printf(const char* format, ...) IMAGO_PRINTF(2, 3);

    // One line typed by the user. False at end of input: EOF, or Ctrl-C / Ctrl-D on an
    // empty line.
    bool readLine(std::string& line);

    struct Key; // a decoded key press

private:

    void edit(const Key& key);
    void insert(const std::string& text);
    void complete();
    void suspend();
    void submit(std::string& line);
    // Callers hold mutex_.
    void historyMove(int step);
    void hideLine();
    void showLine();

    std::mutex mutex_;
    bool interactive_ = false;
    std::string prompt_;
    Completer completer_;
    // The line being typed; only the input thread changes it, under mutex_.
    std::string buffer_;
    size_t cursor_ = 0; // byte offset, always at the start of a UTF-8 character
    std::vector<std::string> history_;
    size_t historyPos_ = 0;
    std::string draft_; // the unfinished line while browsing the history
};

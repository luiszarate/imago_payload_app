// The output pane of the full-screen view: the lines printed so far, wrapped to the terminal
// width and scrolled by screen rows from the bottom.
//
// While the user reads back, new lines do not move what is on screen; they are counted so the
// view can say how many arrived. Not thread-safe: the console guards it with its mutex.

#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

class OutputPane {
public:
    explicit OutputPane(size_t maxLines = 10000) : maxLines_(maxLines) {}

    // Text split at '\n'; a last line without one is still a line.
    void append(const std::string& text);
    // The rows to show, top to bottom: `rows` of them, or fewer while the output is short.
    std::vector<std::string> visible(size_t width, size_t rows);
    // Positive goes back in time.
    void scroll(long rows);
    void follow();

    bool following() const { return offset_ == 0; }
    // Lines that arrived while scrolled back.
    size_t unseen() const { return unseen_; }

private:
    size_t rowsOf(const std::string& line) const;

    size_t maxLines_;
    std::deque<std::string> lines_;
    size_t width_ = 80; // of the last visible(), to keep the view still on append
    size_t offset_ = 0; // rows between the bottom of the view and the newest row
    size_t unseen_ = 0;
};

// A line cut into rows of at most `width` columns; an empty line is one empty row.
std::vector<std::string> wrapLine(const std::string& line, size_t width);

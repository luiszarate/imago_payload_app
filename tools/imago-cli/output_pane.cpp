#include "output_pane.h"

#include "utf8.h"

#include <algorithm>
#include <utility>

std::vector<std::string> wrapLine(const std::string& line, size_t width) {
    width = std::max<size_t>(width, 1);
    std::vector<std::string> rows;
    size_t from = 0;
    do {
        const size_t to = advance(line, from, width);
        rows.push_back(line.substr(from, to - from));
        from = to;
    } while (from < line.size());
    return rows;
}

size_t OutputPane::rowsOf(const std::string& line) const {
    const size_t n = columns(line);
    return n == 0 ? 1 : (n + width_ - 1) / width_;
}

void OutputPane::append(const std::string& text) {
    for (size_t from = 0; from < text.size();) {
        const size_t newline = text.find('\n', from);
        const size_t end = newline == std::string::npos ? text.size() : newline;
        std::string line = text.substr(from, end - from);
        if (offset_ > 0) {
            // Rows added below the view: the view keeps its place.
            offset_ += rowsOf(line);
            ++unseen_;
        }
        lines_.push_back(std::move(line));
        from = end + 1;
    }
    while (lines_.size() > maxLines_) {
        lines_.pop_front();
    }
}

std::vector<std::string> OutputPane::visible(size_t width, size_t rows) {
    width_ = std::max<size_t>(width, 1);
    // Newest row first, only as far back as the view reaches.
    const size_t wanted = offset_ + rows;
    std::vector<std::string> collected;
    for (auto line = lines_.rbegin(); line != lines_.rend() && collected.size() < wanted; ++line) {
        const std::vector<std::string> wrapped = wrapLine(*line, width_);
        for (auto row = wrapped.rbegin(); row != wrapped.rend() && collected.size() < wanted;
             ++row) {
            collected.push_back(*row);
        }
    }
    if (collected.size() < wanted) { // the oldest row is in reach: no further back than that
        offset_ = collected.size() > rows ? collected.size() - rows : 0;
        if (offset_ == 0) {
            unseen_ = 0;
        }
    }
    const size_t end = std::min(collected.size(), offset_ + rows);
    std::vector<std::string> view(collected.begin() + static_cast<std::ptrdiff_t>(offset_),
                                  collected.begin() + static_cast<std::ptrdiff_t>(end));
    std::reverse(view.begin(), view.end());
    return view;
}

void OutputPane::scroll(long rows) {
    if (rows >= 0) {
        offset_ += static_cast<size_t>(rows);
    } else {
        const size_t back = static_cast<size_t>(-rows);
        offset_ = offset_ > back ? offset_ - back : 0;
    }
    if (offset_ == 0) {
        unseen_ = 0;
    }
}

void OutputPane::follow() {
    offset_ = 0;
    unseen_ = 0;
}

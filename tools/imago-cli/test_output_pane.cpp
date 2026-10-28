#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "output_pane.h"

#include <string>
#include <vector>

using Rows = std::vector<std::string>;

TEST_CASE("wrapLine cuts by columns, UTF-8 characters whole") {
    CHECK(wrapLine("abcdef", 4) == Rows{"abcd", "ef"});
    CHECK(wrapLine("abcd", 4) == Rows{"abcd"});
    CHECK(wrapLine("", 4) == Rows{""});
    CHECK(wrapLine("ñañañ", 2) == Rows{"ña", "ña", "ñ"});
    CHECK(wrapLine("ab", 0) == Rows{"a", "b"});
}

TEST_CASE("append splits lines; a blank line stays") {
    OutputPane pane;
    pane.append("a\n\nb\n");
    pane.append("c");
    CHECK(pane.visible(10, 10) == Rows{"a", "", "b", "c"});
}

TEST_CASE("following shows the newest rows") {
    OutputPane pane;
    pane.append("1\n2\n3\n4\n5\n");
    CHECK(pane.visible(10, 3) == Rows{"3", "4", "5"});
    pane.append("6\n");
    CHECK(pane.visible(10, 3) == Rows{"4", "5", "6"});
    CHECK(pane.following());
}

TEST_CASE("wrapped lines count as their rows") {
    OutputPane pane;
    pane.append("abcdef\nxy\n");
    CHECK(pane.visible(4, 2) == Rows{"ef", "xy"});
    CHECK(pane.visible(4, 3) == Rows{"abcd", "ef", "xy"});
}

TEST_CASE("scrolled back, new output does not move the view") {
    OutputPane pane;
    pane.append("1\n2\n3\n4\n5\n");
    pane.scroll(2);
    CHECK(pane.visible(10, 2) == Rows{"2", "3"});
    pane.append("6\n7\n");
    CHECK(pane.visible(10, 2) == Rows{"2", "3"});
    CHECK(pane.unseen() == 2);
    CHECK_FALSE(pane.following());
    pane.follow();
    CHECK(pane.visible(10, 2) == Rows{"6", "7"});
    CHECK(pane.unseen() == 0);
}

TEST_CASE("scroll stops at both ends") {
    OutputPane pane;
    pane.append("1\n2\n3\n4\n");
    pane.scroll(100);
    CHECK(pane.visible(10, 2) == Rows{"1", "2"});
    pane.scroll(-1);
    CHECK(pane.visible(10, 2) == Rows{"2", "3"});
    pane.append("5\n");
    pane.scroll(-100);
    CHECK(pane.following());
    CHECK(pane.unseen() == 0);
    CHECK(pane.visible(10, 2) == Rows{"4", "5"});
}

TEST_CASE("short output never scrolls") {
    OutputPane pane;
    pane.append("1\n2\n");
    pane.scroll(5);
    CHECK(pane.visible(10, 4) == Rows{"1", "2"});
    CHECK(pane.following());
}

TEST_CASE("the oldest lines go past the limit") {
    OutputPane pane(3);
    pane.append("1\n2\n3\n4\n5\n");
    CHECK(pane.visible(10, 10) == Rows{"3", "4", "5"});
}

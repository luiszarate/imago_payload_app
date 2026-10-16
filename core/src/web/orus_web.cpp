#include "web/orus_web.h"

#include <cctype>

namespace imago::web {

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// The value of attribute `name` inside one tag's text, quoted or not; found false if absent.
std::string attribute(std::string_view tag, std::string_view name, bool* found = nullptr) {
    const std::string lowered = lower(tag);
    size_t at = 0;
    while ((at = lowered.find(name, at)) != std::string::npos) {
        const bool startsWord = at > 0 && std::isspace(static_cast<unsigned char>(lowered[at - 1]));
        size_t p = at + name.size();
        while (p < tag.size() && std::isspace(static_cast<unsigned char>(tag[p]))) {
            ++p;
        }
        if (!startsWord) {
            at = p;
            continue;
        }
        if (p >= tag.size() || tag[p] != '=') {
            // A bare attribute such as `selected`.
            if (p >= tag.size() || tag[p] == ' ' || tag[p] == '>' || tag[p] == '/' ||
                std::isspace(static_cast<unsigned char>(tag[p]))) {
                if (found != nullptr) {
                    *found = true;
                }
                return {};
            }
            at = p;
            continue;
        }
        ++p;
        while (p < tag.size() && std::isspace(static_cast<unsigned char>(tag[p]))) {
            ++p;
        }
        if (found != nullptr) {
            *found = true;
        }
        if (p < tag.size() && (tag[p] == '"' || tag[p] == '\'')) {
            const char quote = tag[p];
            const size_t end = tag.find(quote, p + 1);
            return std::string(tag.substr(p + 1, end == std::string_view::npos ? std::string_view::npos
                                                                                 : end - p - 1));
        }
        size_t end = p;
        while (end < tag.size() && !std::isspace(static_cast<unsigned char>(tag[end])) &&
               tag[end] != '>') {
            ++end;
        }
        return std::string(tag.substr(p, end - p));
    }
    if (found != nullptr) {
        *found = false;
    }
    return {};
}

} // namespace

std::map<std::string, std::string> formValues(const std::string& html) {
    std::map<std::string, std::string> values;
    const std::string lowered = lower(html);
    size_t at = 0;
    while ((at = lowered.find('<', at)) != std::string::npos) {
        const size_t end = lowered.find('>', at);
        if (end == std::string::npos) {
            break;
        }
        const std::string_view tag(html.data() + at, end - at + 1);
        if (lowered.compare(at, 6, "<input") == 0) {
            const std::string name = attribute(tag, "name");
            const std::string type = lower(attribute(tag, "type"));
            if (!name.empty() && type != "submit" && type != "button") {
                values[name] = attribute(tag, "value");
            }
        } else if (lowered.compare(at, 7, "<select") == 0) {
            const std::string name = attribute(tag, "name");
            const size_t close = lowered.find("</select", end);
            const size_t stop = close == std::string::npos ? lowered.size() : close;
            std::string first;
            bool haveFirst = false, haveSelected = false;
            size_t o = end;
            while ((o = lowered.find("<option", o)) != std::string::npos && o < stop) {
                const size_t oEnd = lowered.find('>', o);
                if (oEnd == std::string::npos) {
                    break;
                }
                const std::string_view option(html.data() + o, oEnd - o + 1);
                const std::string value = attribute(option, "value");
                bool selected = false;
                attribute(option, "selected", &selected);
                if (!haveFirst) {
                    first = value;
                    haveFirst = true;
                }
                if (selected && !haveSelected) {
                    values[name] = value;
                    haveSelected = true;
                }
                o = oEnd;
            }
            if (!name.empty() && !haveSelected && haveFirst) {
                values[name] = first; // what a browser shows without "selected"
            }
            at = stop;
            continue;
        }
        at = end;
    }
    return values;
}

std::string isothermForm(bool enabled, int thresholdC, int measurementRange) {
    // The fields in the page's order, then the button that sends them, as a browser does.
    return "IsoTherms_Status=" + std::to_string(enabled ? 1 : 0) +
           "&Threshold=" + std::to_string(thresholdC) +
           "&Measurement_Range=" + std::to_string(measurementRange == 1 ? 1 : 0) + "&submit=Apply";
}

std::string restartForm(bool fullReboot) {
    return fullReboot ? "Reboot_Service=1" : "Restart_Service=1";
}

} // namespace imago::web

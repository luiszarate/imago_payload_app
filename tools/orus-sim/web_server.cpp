#include "web_server.h"

#include "platform/clock.h"

#include <cstdlib>
#include <map>

namespace imago::sim {

namespace {

std::map<std::string, std::string> parseForm(const std::string& body) {
    std::map<std::string, std::string> fields;
    size_t at = 0;
    while (at < body.size()) {
        size_t end = body.find('&', at);
        if (end == std::string::npos) {
            end = body.size();
        }
        const std::string pair = body.substr(at, end - at);
        const size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            fields[pair.substr(0, eq)] = pair.substr(eq + 1); // our values need no decoding
        }
        at = end + 1;
    }
    return fields;
}

std::string option(int value, int current, const char* label) {
    return "<option value=\"" + std::to_string(value) + "\"" + (value == current ? " selected" : "") +
           ">" + label + "</option>";
}

} // namespace

WebServer::~WebServer() { stop(); }

bool WebServer::start(uint16_t port, std::string* error) {
    if (!listener_.open(port, error)) {
        return false;
    }
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void WebServer::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    listener_.close();
}

WebServer::Isotherm WebServer::isotherm() const {
    std::lock_guard lock(mutex_);
    return isotherm_;
}

std::string WebServer::page() const {
    const Isotherm iso = isotherm();
    return "<!DOCTYPE html><html><body><h1>IR Camera (orus-sim)</h1>"
           "<form method=\"post\"><h2>Isotherms Regions</h2>"
           "<select name=\"IsoTherms_Status\">" +
           option(0, iso.enabled ? 1 : 0, "Disable") + option(1, iso.enabled ? 1 : 0, "Enable") +
           "</select><input type=\"number\" name=\"Threshold\" min=\"0\" max=\"550\" value=\"" +
           std::to_string(iso.thresholdC) +
           "\"><select name=\"Measurement_Range\">" +
           option(0, iso.measurementRange, "-20~150") + option(1, iso.measurementRange, "-20~550") +
           "</select><input type=\"submit\" name=\"submit\" value=\"Apply\"></form>"
           "<form method=\"post\"><h2>IR Camera</h2><select name=\"FFC_Mode\">" +
           option(0, 1, "Manual") + option(1, 1, "Auto") +
           "</select><input type=\"number\" name=\"FFC_Period\" value=\"2\"></form></body></html>";
}

void WebServer::run() {
    while (running_) {
        const net::SocketHandle fd = listener_.fd();
        bool readable = false;
        if (net::waitReadable({&fd, 1}, 50, {&readable, 1}) <= 0) {
            continue;
        }
        net::TcpSocket socket;
        net::Endpoint peer;
        if (listener_.accept(&socket, &peer)) {
            serve(socket);
            socket.close();
        }
    }
}

void WebServer::serve(const net::TcpSocket& socket) {
    std::string raw;
    char buffer[2048];
    const int64_t deadline = monotonicNs() + 2 * kNsPerSec;
    size_t headEnd = std::string::npos;
    size_t length = 0;
    while (monotonicNs() < deadline) {
        const long n = socket.receive(buffer, sizeof(buffer));
        if (n > 0) {
            raw.append(buffer, static_cast<size_t>(n));
        } else if (n < 0) {
            break;
        } else {
            const net::SocketHandle fd = socket.fd();
            bool readable = false;
            net::waitReadable({&fd, 1}, 20, {&readable, 1});
        }
        if (headEnd == std::string::npos && (headEnd = raw.find("\r\n\r\n")) != std::string::npos) {
            const size_t cl = raw.find("Content-Length:");
            length = cl != std::string::npos && cl < headEnd ? std::strtoul(raw.c_str() + cl + 15, nullptr, 10) : 0;
        }
        if (headEnd != std::string::npos && raw.size() >= headEnd + 4 + length) {
            break;
        }
    }
    if (headEnd == std::string::npos) {
        return;
    }
    const std::string requestLine = raw.substr(0, raw.find("\r\n"));
    const std::string body = raw.substr(headEnd + 4, length);
    std::string response;
    auto redirect = [](const char* to) {
        return std::string("HTTP/1.1 302 FOUND\r\nLocation: ") + to +
               "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    };
    if (requestLine.rfind("GET /setup-ir-camera ", 0) == 0) {
        const std::string html = page();
        response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                   std::to_string(html.size()) + "\r\nConnection: close\r\n\r\n" + html;
    } else if (requestLine.rfind("POST /setup-ir-camera ", 0) == 0) {
        ++posts_;
        const auto fields = parseForm(body);
        if (fields.count("IsoTherms_Status") != 0) {
            std::lock_guard lock(mutex_);
            isotherm_.enabled = fields.at("IsoTherms_Status") == "1";
            if (fields.count("Threshold") != 0) {
                isotherm_.thresholdC = std::atoi(fields.at("Threshold").c_str());
            }
            if (fields.count("Measurement_Range") != 0) {
                isotherm_.measurementRange = std::atoi(fields.at("Measurement_Range").c_str());
            }
        }
        response = redirect("/setup-ir-camera");
    } else if (requestLine.rfind("POST /setup-systems ", 0) == 0) {
        const auto fields = parseForm(body);
        if (fields.count("Reboot_Service") != 0) {
            ++reboots_;
        } else if (fields.count("Restart_Service") != 0) {
            ++restarts_;
        }
        response = redirect("/setup-systems");
    } else {
        response = "HTTP/1.1 404 NOT FOUND\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    }
    socket.sendAll(response.data(), response.size(), 1000);
}

} // namespace imago::sim

#pragma once
#include <cpp-httplib/httplib.h>
#include <algorithm>
#include <cctype>
#include <string>

namespace windows_local_http {
inline bool loopback_bind(const std::string& host) {
    return host == "127.0.0.1" || host == "::1" || host == "localhost";
}
inline std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return value;
}
inline bool allowed_host(const std::string& value, int port) {
    const std::string host = lower(value), suffix = ":" + std::to_string(port);
    if (host == "127.0.0.1" + suffix || host == "localhost" + suffix || host == "[::1]" + suffix) return true;
    return port == 80 && (host == "127.0.0.1" || host == "localhost" || host == "[::1]");
}
inline bool allowed_origin(const std::string& origin, int port) {
    if (origin.empty()) return true; // non-browser API clients have no Origin
    const std::string normalized = lower(origin);
    return normalized.rfind("http://", 0) == 0 && allowed_host(normalized.substr(7), port);
}
inline bool authorized(const std::string& header, const std::string& key) {
    const std::string expected = "Bearer " + key;
    if (key.empty() || header.size() != expected.size()) return false;
    volatile unsigned int difference = 0;
    for (size_t i = 0; i < expected.size(); ++i) difference = difference | (unsigned char)(header[i] ^ expected[i]);
    return difference == 0;
}
inline bool access_allowed(const httplib::Request& request, httplib::Response& response, int port, const std::string& key) {
        int status = 0;
        const char* message = nullptr;
        if (request.get_header_value_count("Host") != 1 ||
            !allowed_host(request.get_header_value("Host"), port)) {
            status = 403; message = "invalid Host";
        } else if (request.get_header_value_count("Origin") > 1 ||
                   !allowed_origin(request.get_header_value("Origin"), port)) {
            status = 403; message = "origin is not allowed";
        } else if (request.get_header_value_count("Authorization") != 1 ||
                   !authorized(request.get_header_value("Authorization"), key)) {
            status = 401; message = "valid Bearer API key required";
            response.set_header("WWW-Authenticate", "Bearer");
        }
        if (!status) return true;
        response.status = status;
        response.set_header("Cache-Control", "no-store");
        response.set_content(std::string("{\"error\":{\"message\":\"") + message + "\"}}", "application/json");
        return false;
}
inline void configure(httplib::Server& http, int port, const std::string& key) {
    http.new_task_queue = [] { return new httplib::ThreadPool(4, 4, 32); };
    http.set_read_timeout(10, 0);
    http.set_keep_alive_timeout(5);
    http.set_payload_max_length(1024 * 1024);
    http.set_pre_routing_handler([port, key](const httplib::Request& request, httplib::Response& response) {
        return access_allowed(request, response, port, key) ? httplib::Server::HandlerResponse::Unhandled : httplib::Server::HandlerResponse::Handled;
    });
}
} // namespace windows_local_http

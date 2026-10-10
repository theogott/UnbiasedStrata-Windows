#pragma once
#include "http_access.h"
#include <nlohmann/json.hpp>
#include <functional>

namespace deepseek_http {
inline bool text_request(const std::string& input) {
    if (input.size() > 1024 * 1024) return false;
    // Bound JSON nesting before allocating a parse tree. Ignore brackets in strings.
    int depth=0; bool quoted=false, escaped=false;
    for (const char c : input) {
        if (quoted) { if (escaped) escaped=false; else if(c=='\\') escaped=true; else if(c=='"') quoted=false; continue; }
        if(c=='"') quoted=true;
        else if(c=='{' || c=='[') { if(++depth>64) return false; }
        else if(c=='}' || c==']') { if(--depth<0) return false; }
    }
    if(quoted || depth) return false;
    const auto body = nlohmann::json::parse(input, nullptr, false);
    if (!body.is_object() || !body.contains("messages") || !body["messages"].is_array() || body["messages"].empty()) return false;
    // This is a text-only endpoint. Do not pass media URLs or backend controls.
    static const std::vector<std::string> allowed = {
        "model", "messages", "stream", "stream_options", "max_tokens", "max_completion_tokens",
        "temperature", "top_p", "top_k", "min_p", "seed", "stop", "presence_penalty", "frequency_penalty",
        "repeat_penalty", "tools", "tool_choice", "parallel_tool_calls", "response_format"
    };
    for (auto it = body.begin(); it != body.end(); ++it)
        if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end()) return false;
    if (body.contains("model") && (!body["model"].is_string() || body["model"] != "DeepSeek-V4.1-Flash")) return false;
    if (body.contains("stream") && !body["stream"].is_boolean()) return false;
    for (const auto& message : body["messages"]) {
        if (!message.is_object() || !message.contains("role") || !message["role"].is_string()) return false;
        const auto role = message["role"].get<std::string>();
        if (role != "system" && role != "user" && role != "assistant" && role != "tool") return false;
        for (auto it = message.begin(); it != message.end(); ++it)
            if (it.key() != "role" && it.key() != "content" && it.key() != "tool_calls" && it.key() != "tool_call_id" && it.key() != "name" && it.key() != "reasoning_content") return false;
        if (!message.contains("content")) { if (role != "assistant" || !message.contains("tool_calls")) return false; continue; }
        const auto& content = message["content"];
        if (content.is_string() || (content.is_null() && role == "assistant")) continue;
        if (!content.is_array()) return false;
        for (const auto& part : content)
            if (!part.is_object() || part.size() != 2 || part.value("type", nlohmann::json()) != "text" || !part.contains("text") || !part["text"].is_string()) return false;
    }
    return true;
}
inline void configure(httplib::Server& http, int port, const std::string& key,
                      std::function<bool(const httplib::Request&, httplib::Response&)> ready = {}) {
    windows_local_http::configure(http, port, key);
    http.set_write_timeout(600, 0);
    // Reuse the same access policy before applying the DeepSeek route and body policy.
    http.set_pre_routing_handler([port, key, ready](const auto& request, auto& response) {
        if (!windows_local_http::access_allowed(request, response, port, key)) return httplib::Server::HandlerResponse::Handled;
        const bool allowed = (request.method == "GET" && (request.path == "/health" || request.path == "/v1/models")) ||
                             (request.method == "POST" && request.path == "/v1/chat/completions");
        if (!allowed) { response.status = 404; response.set_content("{\"error\":{\"message\":\"route unavailable\"}}", "application/json"); return httplib::Server::HandlerResponse::Handled; }
        if (ready && !ready(request, response)) return httplib::Server::HandlerResponse::Handled;
        return httplib::Server::HandlerResponse::Unhandled;
    });
}
inline bool body_allowed(const httplib::Request& request, httplib::Response& response) {
    // cpp-httplib reads the body AFTER pre-routing. Validate it in the POST handler.
    if (!request.is_multipart_form_data() && text_request(request.body)) return true;
    response.status=400;
    response.set_content("{\"error\":{\"message\":\"valid text-only chat request required\"}}", "application/json");
    return false;
}
}

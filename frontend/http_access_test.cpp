#include "http_access.h"
#include "local_key.h"
#include "engine_tuning.h"
#include <fstream>
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    const std::string key(64, 'a');
    httplib::Server server;
    const int port = server.bind_to_any_port("127.0.0.1");
    if (port <= 0) return 1;
    windows_local_http::configure(server, port, key);
    server.Get("/health", [](const auto&, auto& res) { res.set_content("ok", "text/plain"); });
    server.Post("/v1/chat/completions", [](const auto&, auto& res) { res.set_content("ok", "text/plain"); });
    std::thread serving([&] { server.listen_after_bind(); });
    while (!server.is_running()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    int failures = 0;
    auto check = [&](bool result, const char* name) {
        std::cout << (result ? "PASS: " : "FAIL: ") << name << '\n';
        if (!result) ++failures;
    };
    httplib::Client client("127.0.0.1", port);
    auto get = [&](httplib::Headers headers) { return client.Get("/health", headers); };
    auto result = get({}); check(result && result->status == 401, "missing API key rejected");
    result = get({{"Authorization", "Bearer wrong"}}); check(result && result->status == 401, "wrong API key rejected");
    result = get({{"Authorization", "Bearer " + key}}); check(result && result->status == 200, "authorized local request allowed");
    result = get({{"Authorization", "Bearer " + key}, {"Host", "attacker.example:" + std::to_string(port)}});
    check(result && result->status == 403, "DNS-rebinding Host rejected");
    result = get({{"Authorization", "Bearer " + key}, {"Origin", "https://attacker.example"}});
    check(result && result->status == 403, "foreign browser Origin rejected");
    result = get({{"Authorization", "Bearer " + key}, {"Origin", "null"}});
    check(result && result->status == 403, "opaque Origin rejected");
    result = get({{"Authorization", "Bearer " + key}, {"Origin", "http://localhost:" + std::to_string(port)}});
    check(result && result->status == 200, "matching local Origin allowed");
    result = client.Post("/v1/chat/completions", {{"Authorization", "Bearer " + key}}, std::string(1024 * 1024 + 1, 'x'), "application/json");
    check(result && result->status == 413, "oversized request rejected");
    check(!windows_local_http::loopback_bind("0.0.0.0") && !windows_local_http::loopback_bind("192.168.1.1"), "nonlocal bind rejected");
    check(!tuning_value("--slots", "2") && !tuning_value("--native", "other.gguf") && !tuning_value("--max-context", "999999"), "protected engine arguments rejected");
    check(tuning_value("--vram-reserve-mib", "4096") && !tuning_value("--vram-reserve-mib", "-1") && !tuning_value("--vram-reserve-mib", "99999999"), "tuning values bounded");
    std::string read_key;
    check(!read_local_api_key("\\\\server\\share\\key", read_key) && !read_local_api_key("\\\\.\\NUL", read_key), "UNC and device key paths rejected");
    char temp_directory[MAX_PATH], temp_name[MAX_PATH];
    GetTempPathA(MAX_PATH, temp_directory); GetTempFileNameA(temp_directory, "usk", 0, temp_name);
    { std::ofstream output(temp_name, std::ios::binary); output << key << "\r\n"; }
    check(read_local_api_key(temp_name, read_key) && read_key == key, "regular local key accepted");
    { std::ofstream output(temp_name, std::ios::binary); output << std::string(515, 'a'); }
    check(!read_local_api_key(temp_name, read_key) && read_key.empty(), "oversized key rejected before reading");
    DeleteFileA(temp_name);
    server.stop(); serving.join();
    return failures ? 1 : 0;
}

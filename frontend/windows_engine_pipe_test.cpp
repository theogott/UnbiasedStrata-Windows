#include "windows_engine_pipe.h"
#include <iostream>
#include <stdexcept>

void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--stub") {
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
        for (int i = 2; i < argc; ++i) std::cout << std::string(argv[i]).size() << ":" << argv[i] << '\n';
        std::cout << "READY\n" << std::flush;
        std::string line;
        while (std::getline(std::cin, line)) std::cout << line << '\n' << std::flush;
        return 0;
    }
    try {
        WindowsEnginePipe pipe;
        std::string error, line;
        const std::vector<std::string> arguments = {"self", "--stub", "", "path with spaces\\", "a\"b", "C:\\trailing\\"};
        check(pipe.start(arguments, error), error.c_str());
        for (size_t i = 2; i < arguments.size(); ++i) {
            check(pipe.read_line(line), "argument EOF");
            check(line == std::to_string(arguments[i].size()) + ":" + arguments[i], "argument round-trip mismatch");
        }
        check(pipe.read_line(line) && line == "READY", "missing READY");
        std::string large(100000, 'x');
        check(pipe.send(large + "\n"), "large pipe write failed");
        check(pipe.read_line(line) && line == large, "large line mismatch");
        pipe.quit();
        pipe.quit();
        check(!pipe.send("after quit\n"), "closed pipe accepted write");
        check(pipe.start({"self", "--stub"}, error), "restart failed");
        check(pipe.read_line(line) && line == "READY", "restart missing READY");
        pipe.quit();
        std::cout << "PASS: Windows child startup, argument quoting, 100KB pipe exchange, EOF shutdown, repeat shutdown, restart\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

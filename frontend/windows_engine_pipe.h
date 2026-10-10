#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <fcntl.h>
#include <io.h>
#include <string>
#include <vector>

// Owns only the engine child and its pipes. A job prevents an orphaned GPU engine
// when the frontend is closed or terminated from Task Manager.
class WindowsEnginePipe {
public:
    WindowsEnginePipe() = default;
    WindowsEnginePipe(const WindowsEnginePipe&) = delete;
    WindowsEnginePipe& operator=(const WindowsEnginePipe&) = delete;
    ~WindowsEnginePipe() { quit(); }

    static std::wstring quote(const std::wstring& arg) {
        std::wstring result = L"\"";
        size_t slashes = 0;
        for (wchar_t c : arg) {
            if (c == L'\\') { ++slashes; continue; }
            result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
            slashes = 0;
            result += c;
        }
        result.append(slashes * 2, L'\\');
        return result + L"\"";
    }

    bool start(const std::vector<std::string>& args, std::string& error) {
        if (process_) { error = "engine is already running"; return false; }
        std::vector<wchar_t> exe(32768);
        const DWORD n = GetModuleFileNameW(nullptr, exe.data(), (DWORD)exe.size());
        if (!n || n >= exe.size()) { error = "cannot resolve frontend executable"; return false; }
        std::wstring command = quote(std::wstring(exe.data(), n));
        for (size_t i = 1; i < args.size(); ++i) {
            const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, args[i].data(), (int)args[i].size(), nullptr, 0);
            if (!length && !args[i].empty()) { error = "engine argument is not UTF-8"; return false; }
            std::wstring arg(length, L'\0');
            if (length) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, args[i].data(), (int)args[i].size(), arg.data(), length);
            command += L" " + quote(arg);
        }
        if (command.size() >= 32767) { error = "engine command line is too long"; return false; }

        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE child_in = nullptr, parent_out = nullptr, parent_in = nullptr, child_out = nullptr, child_err = nullptr;
        auto close = [](HANDLE& h) { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); h = nullptr; };
        auto cleanup = [&] { close(child_in); close(parent_out); close(parent_in); close(child_out); close(child_err); };
        if (!CreatePipe(&child_in, &parent_in, &sa, 0) || !CreatePipe(&parent_out, &child_out, &sa, 0) ||
            !SetHandleInformation(parent_in, HANDLE_FLAG_INHERIT, 0) ||
            !SetHandleInformation(parent_out, HANDLE_FLAG_INHERIT, 0)) {
            error = "cannot create engine pipes: " + std::to_string(GetLastError()); cleanup(); return false;
        }
        HANDLE std_error = GetStdHandle(STD_ERROR_HANDLE);
        if (!std_error || std_error == INVALID_HANDLE_VALUE ||
            !DuplicateHandle(GetCurrentProcess(), std_error, GetCurrentProcess(), &child_err, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
            child_err = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        }
        if (child_err == INVALID_HANDLE_VALUE) { error = "cannot open engine stderr"; cleanup(); return false; }

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = child_in;
        startup.StartupInfo.hStdOutput = child_out;
        startup.StartupInfo.hStdError = child_err;
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<unsigned char> attributes(bytes);
        startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes)) {
            error = "cannot initialize engine handle list"; cleanup(); return false;
        }
        HANDLE handles[] = {child_in, child_out, child_err};
        if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       handles, sizeof(handles), nullptr, nullptr)) {
            error = "cannot restrict engine handle inheritance";
            DeleteProcThreadAttributeList(startup.lpAttributeList); cleanup(); return false;
        }
        job_ = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job_ || !SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            error = "cannot create engine lifetime job";
            DeleteProcThreadAttributeList(startup.lpAttributeList); cleanup(); close(job_); return false;
        }
        PROCESS_INFORMATION pi{};
        const BOOL created = CreateProcessW(exe.data(), command.data(), nullptr, nullptr, TRUE,
            EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup.StartupInfo, &pi);
        const DWORD create_error = GetLastError();
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        close(child_in); close(child_out); close(child_err);
        if (!created) { error = "cannot start engine: " + std::to_string(create_error); cleanup(); close(job_); return false; }
        process_ = pi.hProcess;
        if (!AssignProcessToJobObject(job_, process_)) {
            error = "cannot attach engine lifetime job: " + std::to_string(GetLastError());
            TerminateProcess(process_, 1); WaitForSingleObject(process_, 5000);
            close(pi.hThread); close(process_); close(job_); cleanup(); return false;
        }
        const int input = _open_osfhandle(reinterpret_cast<intptr_t>(parent_in), _O_WRONLY | _O_BINARY);
        if (input >= 0) { in_fd_ = input; parent_in = nullptr; }
        const int output = _open_osfhandle(reinterpret_cast<intptr_t>(parent_out), _O_RDONLY | _O_BINARY);
        if (output >= 0) { parent_out = nullptr; out_ = _fdopen(output, "rb"); if (!out_) _close(output); }
        if (in_fd_ < 0 || !out_ || ResumeThread(pi.hThread) == (DWORD)-1) {
            error = "cannot initialize engine pipe streams";
            close(pi.hThread); cleanup(); quit(); return false;
        }
        close(pi.hThread);
        return true;
    }

    bool send(const std::string& text) {
        if (in_fd_ < 0) return false;
        size_t offset = 0;
        while (offset < text.size()) {
            const int count = _write(in_fd_, text.data() + offset,
                (unsigned int)std::min<size_t>(text.size() - offset, 65536));
            if (count <= 0) return false;
            offset += count;
        }
        return true;
    }
    bool read_line(std::string& line) {
        line.clear();
        char buffer[4096];
        while (out_ && std::fgets(buffer, sizeof(buffer), out_)) {
            line += buffer;
            if (!line.empty() && line.back() == '\n') break;
        }
        if (line.empty()) return false;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        return true;
    }
    void quit() {
        if (process_) {
            // Closing stdin ends the line-protocol loop without risking a blocked QUIT write.
            if (in_fd_ >= 0) { _close(in_fd_); in_fd_ = -1; }
            if (WaitForSingleObject(process_, 10000) == WAIT_TIMEOUT) {
                TerminateJobObject(job_, 1);
                WaitForSingleObject(process_, 5000);
            }
            CloseHandle(process_); process_ = nullptr;
        }
        if (job_) { CloseHandle(job_); job_ = nullptr; }
        if (in_fd_ >= 0) { _close(in_fd_); in_fd_ = -1; }
        if (out_) { std::fclose(out_); out_ = nullptr; }
    }
private:
    HANDLE process_ = nullptr;
    HANDLE job_ = nullptr;
    int in_fd_ = -1;
    FILE* out_ = nullptr;
};

#pragma once
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <filesystem>
#include <fstream>
#endif

inline bool read_local_api_key(const std::string& path, std::string& key) {
    key.clear();
    if (path.empty() || path.rfind("\\\\", 0) == 0 || path.rfind("//", 0) == 0) return false;
    char buffer[514];
    size_t count = 0;
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), (int)path.size(), nullptr, 0);
    if (!n) return false;
    std::wstring name(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), (int)path.size(), name.data(), n);
    wchar_t absolute[32768];
    const DWORD absolute_n = GetFullPathNameW(name.c_str(), 32768, absolute, nullptr);
    if (absolute_n < 3 || absolute_n >= 32768 || absolute[1] != L':' || absolute[2] != L'\\') return false;
    const std::wstring checked_name(absolute, absolute_n);
    if (checked_name.find(L':', 2) != std::wstring::npos) return false;
    wchar_t preflight_drive[] = {absolute[0], L':', L'\\', L'\0'};
    const UINT preflight_kind = GetDriveTypeW(preflight_drive);
    if (!(preflight_kind == DRIVE_FIXED || preflight_kind == DRIVE_REMOVABLE || preflight_kind == DRIVE_RAMDISK || preflight_kind == DRIVE_CDROM)) return false;
    // Reject each ancestor before following it. Final-handle checks below remain
    // necessary; hostile local filesystem races are outside this trust boundary.
    for (size_t end = 3; end <= checked_name.size(); ++end) {
        if (end != checked_name.size() && checked_name[end] != L'\\' && checked_name[end] != L'/') continue;
        const auto prefix = checked_name.substr(0, end);
        const DWORD attributes = GetFileAttributesW(prefix.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE))) return false;
        if (end != checked_name.size() && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    }
    name = checked_name;
    HANDLE file = CreateFileW(name.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER length{};
    wchar_t final_path[32768];
    const DWORD final_n = GetFinalPathNameByHandleW(file, final_path, 32768, FILE_NAME_NORMALIZED);
    bool valid = GetFileType(file) == FILE_TYPE_DISK && GetFileInformationByHandle(file, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE)) &&
        GetFileSizeEx(file, &length) && length.QuadPart >= 32 && length.QuadPart <= sizeof(buffer) &&
        final_n >= 7 && final_n < 32768 && std::wstring(final_path, 4) == L"\\\\?\\" && final_path[5] == L':' && final_path[6] == L'\\';
    if (valid) {
        wchar_t drive[] = {final_path[4], L':', L'\\', L'\0'};
        const UINT kind = GetDriveTypeW(drive);
        valid = kind == DRIVE_FIXED || kind == DRIVE_REMOVABLE || kind == DRIVE_RAMDISK || kind == DRIVE_CDROM;
    }
    DWORD read = 0;
    if (valid) valid = ReadFile(file, buffer, (DWORD)length.QuadPart, &read, nullptr) && read == length.QuadPart;
    CloseHandle(file);
    if (!valid) return false;
    count = read;
#else
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) return false;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size < 32 || size > sizeof(buffer)) return false;
    std::ifstream input(path, std::ios::binary);
    input.read(buffer, sizeof(buffer)); count = (size_t)input.gcount();
#endif
    key.assign(buffer, count);
    while (!key.empty() && (key.back() == '\r' || key.back() == '\n')) key.pop_back();
    if (key.size() < 32 || key.size() > 512) { key.clear(); return false; }
    for (unsigned char c : key) if (c < 33 || c > 126) { key.clear(); return false; }
    return true;
}

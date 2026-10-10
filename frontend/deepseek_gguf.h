#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace deepseek_gguf {
inline std::wstring local_path(const std::string& input) {
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), (int)input.size(), nullptr, 0);
    if (!n) throw std::runtime_error("path must be UTF-8");
    std::wstring path(n, L'\0'); MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), (int)input.size(), path.data(), n);
    if (path.size() < 3 || path[1] != L':' || (path[2] != L'\\' && path[2] != L'/') || path.find(L':', 2) != std::wstring::npos) throw std::runtime_error("use an absolute local drive path");
    wchar_t drive[] = {path[0], L':', L'\\', 0};
    if (GetDriveTypeW(drive) != DRIVE_FIXED) throw std::runtime_error("model/backend must be on a fixed local drive");
    wchar_t absolute[32768]; const auto length=GetFullPathNameW(path.c_str(),32768,absolute,nullptr);
    if(length<3 || length>=32768) throw std::runtime_error("cannot resolve local path");
    path.assign(absolute,length);
    // Check ancestors before following them. A local junction must not redirect reads to a network share.
    for(size_t end=3;end<=path.size();++end) {
        if(end<path.size() && path[end]!=L'\\') continue;
        const auto attributes=GetFileAttributesW(path.substr(0,end).c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_OFFLINE))) throw std::runtime_error("missing or redirected local file path");
        if(end==path.size() && (attributes&FILE_ATTRIBUTE_DIRECTORY)) throw std::runtime_error("expected regular local file");
    }
    return path;
}
struct Metadata { std::string architecture; uint64_t count = 1, index = 0; };
class Reader {
    FILE* file_ = nullptr; uint64_t bytes_ = 0;
public:
    explicit Reader(const std::string& path) { file_ = _wfopen(local_path(path).c_str(), L"rb"); if (!file_) throw std::runtime_error("cannot open GGUF: " + path); }
    ~Reader() { if (file_) fclose(file_); }
    Reader(const Reader&) = delete;
    void read(void* ptr, size_t size) { bytes_ += size; if (bytes_ > 64 * 1024 * 1024 || fread(ptr, 1, size, file_) != size) throw std::runtime_error("invalid or oversized GGUF metadata"); }
    template<class T> T number() { T value{}; read(&value, sizeof(value)); return value; }
    std::string string() { uint64_t n = number<uint64_t>(); if (n > 16 * 1024 * 1024) throw std::runtime_error("oversized GGUF string"); std::string value((size_t)n, '\0'); read(value.data(), value.size()); return value; }
    uint64_t integer(uint32_t type) {
        switch(type) { case 0:return number<uint8_t>(); case 1:return number<int8_t>(); case 2:return number<uint16_t>(); case 3:return number<int16_t>(); case 4:return number<uint32_t>(); case 5:return number<int32_t>(); case 7:return number<uint8_t>(); case 10:return number<uint64_t>(); case 11:return number<int64_t>(); default:throw std::runtime_error("invalid GGUF integer type"); }
    }
    void skip(uint32_t type, int depth=0) {
        if (depth > 1) throw std::runtime_error("nested GGUF arrays are unsupported");
        if (type == 8) { string(); return; }
        if (type == 9) { const auto subtype=number<uint32_t>(); const auto n=number<uint64_t>(); if (n > 1000000) throw std::runtime_error("oversized GGUF array"); for(uint64_t i=0;i<n;++i) skip(subtype,depth+1); return; }
        if (type == 6) { number<uint32_t>(); return; }
        if (type == 12) { number<uint64_t>(); return; }
        integer(type);
    }
    Metadata metadata() {
        if (number<uint32_t>() != 0x46554747 || number<uint32_t>() != 3) throw std::runtime_error("expected little-endian GGUF v3");
        number<uint64_t>(); const auto entries=number<uint64_t>(); if (entries > 100000) throw std::runtime_error("oversized GGUF header");
        Metadata result;
        for(uint64_t i=0;i<entries;++i) { auto key=string(); auto type=number<uint32_t>();
            if(key=="general.architecture") { if(type!=8) throw std::runtime_error("architecture must be a string"); result.architecture=string(); }
            else if(key=="split.count") result.count=integer(type);
            else if(key=="split.no") result.index=integer(type);
            else skip(type);
        }
        return result;
    }
};
inline std::vector<std::string> check(const std::string& first) {
    auto header=Reader(first).metadata();
    if(header.architecture!="deepseek41") throw std::runtime_error("requires general.architecture=deepseek41; Qwen, V4 and R1 are different models");
    if(header.index!=0 || header.count<1 || header.count>128) throw std::runtime_error("requires first GGUF shard with valid split metadata");
    std::vector<std::string> files{first};
    if(header.count==1) return files;
    char suffix[40]; snprintf(suffix,sizeof(suffix),"-00001-of-%05llu.gguf",(unsigned long long)header.count);
    const std::string ending(suffix);
    if(first.size()<ending.size() || first.substr(first.size()-ending.size())!=ending) throw std::runtime_error("first shard filename disagrees with split.count");
    const auto prefix=first.substr(0,first.size()-ending.size());
    for(uint64_t i=1;i<header.count;++i) { snprintf(suffix,sizeof(suffix),"-%05llu-of-%05llu.gguf",(unsigned long long)(i+1),(unsigned long long)header.count); auto name=prefix+suffix; auto next=Reader(name).metadata();
        if(next.count!=header.count || next.index!=i || (!next.architecture.empty() && next.architecture!="deepseek41")) throw std::runtime_error("inconsistent GGUF shard metadata"); files.push_back(name);
    }
    return files;
}
}

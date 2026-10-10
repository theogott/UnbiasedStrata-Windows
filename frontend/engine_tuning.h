#pragma once
#include <string>

inline bool tuning_flag(const std::string& name) { return name == "--mmap-experts"; }
inline bool tuning_value(const std::string& name, const std::string& value) {
    if (name == "--kv") return value == "int8" || value == "fp16";
    if (name == "--expert-cache" && value == "auto") return true;
    unsigned int maximum = 0, minimum = 0;
    if (name == "--vram-reserve-mib") maximum = 65536;
    else if (name == "--resident-budget-gib") { maximum = 128; minimum = 1; }
    else if (name == "--expert-cache") maximum = 1000000;
    else return false;
    if (value.empty() || value.size() > 7) return false;
    unsigned int number = 0;
    for (unsigned char c : value) { if (c < '0' || c > '9') return false; number = number * 10 + c - '0'; }
    return number >= minimum && number <= maximum;
}

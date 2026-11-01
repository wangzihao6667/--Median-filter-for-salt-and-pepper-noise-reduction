#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <functional>
#include <filesystem>

std::wstring utf8_to_wstring(const std::string &s);
std::string wstring_to_utf8(const std::wstring &s);
std::string format_double(double v, int decimals = 2);
double now_ms();

struct ImageRGB {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;

    bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
    size_t byte_size() const { return (size_t)width * (size_t)height * 3u; }
};

using ProgressFn = std::function<void(int)>;

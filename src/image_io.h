#pragma once

#include "common.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

bool load_image(const std::filesystem::path &path, ImageRGB &out, std::string &error);
bool save_jpeg(const std::filesystem::path &path, const uint8_t *pixels,
               int width, int height, int quality, std::string &error);

std::vector<std::filesystem::path> list_images(const std::filesystem::path &dir);

bool load_images_parallel(const std::vector<std::filesystem::path> &paths, int count,
                          int &width, int &height, std::vector<uint8_t> &buffer,
                          std::string &error, ProgressFn progress,
                          std::atomic<bool> *cancel = nullptr);

bool save_images_parallel(const std::vector<std::filesystem::path> &paths,
                          const std::vector<uint8_t> &buffer, int count,
                          int width, int height, int quality,
                          std::string &error, ProgressFn progress,
                          std::atomic<bool> *cancel = nullptr);

std::filesystem::path default_image_dir();
std::filesystem::path executable_dir();

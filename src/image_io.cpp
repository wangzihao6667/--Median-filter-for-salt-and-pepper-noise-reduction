#include "image_io.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

namespace {

struct FileHandle {
    FILE *f = nullptr;
    ~FileHandle() { if (f) fclose(f); }
};

int read_cb(void *user, char *data, int size) {
    return (int)fread(data, 1, (size_t)size, (FILE *)user);
}

void skip_cb(void *user, int n) {
    if (n > 0) fseek((FILE *)user, n, SEEK_CUR);
}

int eof_cb(void *user) {
    return feof((FILE *)user) ? 1 : 0;
}

void write_cb(void *user, void *data, int size) {
    fwrite(data, 1, (size_t)size, (FILE *)user);
}

int worker_count(int count) {
    unsigned int hc = std::thread::hardware_concurrency();
    if (hc == 0) hc = 4;
    int n = (int)hc;
    if (n > 8) n = 8;
    if (n > count) n = count;
    return n < 1 ? 1 : n;
}

}  // namespace

bool load_image(const std::filesystem::path &path, ImageRGB &out, std::string &error) {
    FileHandle fh;
    fh.f = _wfopen(path.c_str(), L"rb");
    if (!fh.f) {
        error = "无法打开图片: " + wstring_to_utf8(path.wstring());
        return false;
    }

    stbi_io_callbacks cb;
    cb.read = read_cb;
    cb.skip = skip_cb;
    cb.eof = eof_cb;

    int w = 0, h = 0, comp = 0;
    unsigned char *data = stbi_load_from_callbacks(&cb, fh.f, &w, &h, &comp, 3);
    if (!data || w <= 0 || h <= 0) {
        error = "图片解码失败: " + wstring_to_utf8(path.wstring());
        if (data) stbi_image_free(data);
        return false;
    }

    out.width = w;
    out.height = h;
    out.pixels.assign(data, data + (size_t)w * (size_t)h * 3u);
    stbi_image_free(data);
    return true;
}

bool save_jpeg(const std::filesystem::path &path, const uint8_t *pixels,
               int width, int height, int quality, std::string &error) {
    if (!pixels || width <= 0 || height <= 0) {
        error = "保存失败：图像为空";
        return false;
    }
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    FileHandle fh;
    fh.f = _wfopen(path.c_str(), L"wb");
    if (!fh.f) {
        error = "无法写入文件: " + wstring_to_utf8(path.wstring());
        return false;
    }

    int ok = stbi_write_jpg_to_func(write_cb, fh.f, width, height, 3, pixels,
                                    quality < 1 ? 1 : (quality > 100 ? 100 : quality));
    if (!ok) {
        error = "JPEG 编码失败: " + wstring_to_utf8(path.wstring());
        return false;
    }
    return true;
}

std::vector<std::filesystem::path> list_images(const std::filesystem::path &dir) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return files;

    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::wstring ext = entry.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
        if (ext == L".jpg" || ext == L".jpeg" || ext == L".png" || ext == L".bmp") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) {
        return a.filename().wstring() < b.filename().wstring();
    });
    return files;
}

bool load_images_parallel(const std::vector<std::filesystem::path> &paths, int count,
                          int &width, int &height, std::vector<uint8_t> &buffer,
                          std::string &error, ProgressFn progress,
                          std::atomic<bool> *cancel) {
    if (count <= 0 || paths.empty()) {
        error = "没有可读取的图片";
        return false;
    }
    if (count > (int)paths.size()) count = (int)paths.size();

    ImageRGB first;
    if (!load_image(paths[0], first, error)) return false;
    width = first.width;
    height = first.height;
    size_t image_bytes = first.byte_size();
    buffer.assign((size_t)count * image_bytes, 0);
    std::copy(first.pixels.begin(), first.pixels.end(), buffer.begin());
    if (progress) progress(1);

    std::atomic<int> next(1);
    std::atomic<bool> failed(false);
    std::mutex err_mutex;
    int nthreads = worker_count(count);
    std::vector<std::thread> workers;
    for (int t = 0; t < nthreads; ++t) {
        workers.emplace_back([&]() {
            while (true) {
                if (cancel && cancel->load()) return;
                int i = next.fetch_add(1);
                if (i >= count) return;
                ImageRGB img;
                std::string local_error;
                if (!load_image(paths[(size_t)i], img, local_error)) {
                    std::lock_guard<std::mutex> lock(err_mutex);
                    if (!failed.exchange(true)) error = local_error;
                    return;
                }
                if (img.width != width || img.height != height) {
                    std::lock_guard<std::mutex> lock(err_mutex);
                    if (!failed.exchange(true)) {
                        error = "批量图片尺寸不一致: " + wstring_to_utf8(paths[(size_t)i].filename().wstring()) +
                                " (" + std::to_string(img.width) + "x" + std::to_string(img.height) + ")";
                    }
                    return;
                }
                std::copy(img.pixels.begin(), img.pixels.end(),
                          buffer.begin() + (size_t)i * image_bytes);
                if (progress) progress(i + 1);
            }
        });
    }
    for (auto &w : workers) w.join();
    return !failed.load();
}

bool save_images_parallel(const std::vector<std::filesystem::path> &paths,
                          const std::vector<uint8_t> &buffer, int count,
                          int width, int height, int quality,
                          std::string &error, ProgressFn progress,
                          std::atomic<bool> *cancel) {
    if (count <= 0) return true;
    size_t image_bytes = (size_t)width * (size_t)height * 3u;
    std::atomic<int> next(0);
    std::atomic<bool> failed(false);
    std::mutex err_mutex;
    int nthreads = worker_count(count);
    std::vector<std::thread> workers;
    for (int t = 0; t < nthreads; ++t) {
        workers.emplace_back([&]() {
            while (true) {
                if (cancel && cancel->load()) return;
                int i = next.fetch_add(1);
                if (i >= count) return;
                std::string local_error;
                if (!save_jpeg(paths[(size_t)i], buffer.data() + (size_t)i * image_bytes,
                               width, height, quality, local_error)) {
                    std::lock_guard<std::mutex> lock(err_mutex);
                    if (!failed.exchange(true)) error = local_error;
                    return;
                }
                if (progress) progress(i + 1);
            }
        });
    }
    for (auto &w : workers) w.join();
    return !failed.load();
}

std::filesystem::path executable_dir() {
    wchar_t path[MAX_PATH * 2] = {};
    DWORD n = GetModuleFileNameW(nullptr, path, (DWORD)(sizeof(path) / sizeof(path[0])));
    if (n == 0) return std::filesystem::current_path();
    return std::filesystem::path(path).parent_path();
}

std::filesystem::path default_image_dir() {
    std::error_code ec;
    std::filesystem::path external = L"D:\\项目\\嵌入式\\image\\image";
    if (std::filesystem::is_directory(external, ec)) return external;
    std::filesystem::path local = executable_dir() / L"samples";
    if (std::filesystem::is_directory(local, ec)) return local;
    local = executable_dir().parent_path() / L"samples";
    if (std::filesystem::is_directory(local, ec)) return local;
    return executable_dir();
}

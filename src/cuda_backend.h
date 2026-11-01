#pragma once

#include "common.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

enum class ProcessingMode {
    PerImage,   // one H2D / kernel / D2H sequence per image (baseline)
    Separate,   // one batched H2D, noise kernel, median kernel, D2H
    Fused       // one batched H2D, fused tiled kernel, D2H
};

struct StageTimes {
    double h2d_ms = 0.0;
    double noise_ms = 0.0;
    double median_ms = 0.0;
    double kernel_ms = 0.0;
    double stats_ms = 0.0;
    double d2h_ms = 0.0;
    double gpu_total_ms = 0.0;
    double wall_ms = 0.0;
};

struct ImageQuality {
    double psnr_noisy = 0.0;
    double psnr_filtered = 0.0;
    double noise_ratio = 0.0;
};

struct GpuInfo {
    std::string name;
    int compute_major = 0;
    int compute_minor = 0;
    int driver_version = 0;
    int nvrtc_version = 0;
    size_t total_memory = 0;
    size_t free_memory_at_init = 0;
    std::string nvrtc_dll;
    int sm_count = 0;
    int max_threads_per_sm = 0;
    int max_shared_memory_per_sm = 0;
    int l2_cache_size = 0;
    int memory_bus_width = 0;
    int memory_clock_khz = 0;
};

struct KernelResource {
    int registers_per_thread = 0;
    int static_shared_bytes = 0;
    int local_bytes = 0;
    int max_threads_per_block = 0;
    int active_blocks_per_sm = 0;
    int threads_per_block = 256;
    double theoretical_occupancy = 0.0;
};

class CudaBackend {
public:
    CudaBackend();
    ~CudaBackend();

    CudaBackend(const CudaBackend &) = delete;
    CudaBackend &operator=(const CudaBackend &) = delete;

    bool initialize(const std::filesystem::path &kernel_source, std::string &error);
    bool ready() const;

    const GpuInfo &gpu_info() const;
    KernelResource kernel_resource(bool fused, int kernel_size) const;

    // Processes a contiguous RGB batch.  Input layout is image-major,
    // row-major, three bytes per pixel.  noisy and filtered are resized to
    // count * width * height * 3.
    bool process(const uint8_t *input, int count, int width, int height,
                 float noise_ratio, int kernel_size, ProcessingMode mode,
                 std::vector<uint8_t> &noisy, std::vector<uint8_t> &filtered,
                 std::vector<ImageQuality> &quality, StageTimes &times,
                 std::string &error, ProgressFn progress = {});

    std::string last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

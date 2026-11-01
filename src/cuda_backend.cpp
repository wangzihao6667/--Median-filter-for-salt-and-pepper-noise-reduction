#include "cuda_backend.h"

#include <cuda.h>
#include <nvrtc.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <sstream>
#include <stdexcept>

namespace {

struct LaunchDim {
    unsigned int x = 1, y = 1, z = 1;
    LaunchDim(unsigned int x_, unsigned int y_, unsigned int z_) : x(x_), y(y_), z(z_) {}
};

template <typename T>
bool load_symbol(HMODULE module, const char *name, T &fn, std::string &error) {
    fn = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!fn) {
        error = std::string("无法定位 CUDA 导出函数: ") + name;
        return false;
    }
    return true;
}

std::string read_text_file(const std::filesystem::path &path, std::string &error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "无法读取内核源码: " + wstring_to_utf8(path.wstring());
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

struct CudaBackend::Impl {
    HMODULE nvcuda = nullptr;
    HMODULE nvrtc = nullptr;
    CUcontext context = nullptr;
    CUdevice device = 0;
    CUmodule module = nullptr;
    CUfunction fn_noise = nullptr;
    CUfunction fn_median3 = nullptr;
    CUfunction fn_median5 = nullptr;
    CUfunction fn_median7 = nullptr;
    CUfunction fn_fused3 = nullptr;
    CUfunction fn_fused5 = nullptr;
    CUfunction fn_fused7 = nullptr;
    CUfunction fn_stats = nullptr;
    GpuInfo info;
    std::string last_error;

    decltype(&cuInit) p_cuInit = nullptr;
    decltype(&cuDriverGetVersion) p_cuDriverGetVersion = nullptr;
    decltype(&cuDeviceGet) p_cuDeviceGet = nullptr;
    decltype(&cuDeviceGetCount) p_cuDeviceGetCount = nullptr;
    decltype(&cuDeviceGetName) p_cuDeviceGetName = nullptr;
    decltype(&cuDeviceGetAttribute) p_cuDeviceGetAttribute = nullptr;
    decltype(&cuDevicePrimaryCtxRetain) p_cuDevicePrimaryCtxRetain = nullptr;
    decltype(&cuDevicePrimaryCtxRelease) p_cuDevicePrimaryCtxRelease = nullptr;
    decltype(&cuCtxSetCurrent) p_cuCtxSetCurrent = nullptr;
    decltype(&cuMemAlloc) p_cuMemAlloc = nullptr;
    decltype(&cuMemFree) p_cuMemFree = nullptr;
    decltype(&cuMemGetInfo) p_cuMemGetInfo = nullptr;
    decltype(&cuMemcpyHtoD) p_cuMemcpyHtoD = nullptr;
    decltype(&cuMemcpyDtoH) p_cuMemcpyDtoH = nullptr;
    decltype(&cuMemsetD32) p_cuMemsetD32 = nullptr;
    decltype(&cuModuleLoadData) p_cuModuleLoadData = nullptr;
    decltype(&cuModuleUnload) p_cuModuleUnload = nullptr;
    decltype(&cuModuleGetFunction) p_cuModuleGetFunction = nullptr;
    decltype(&cuLaunchKernel) p_cuLaunchKernel = nullptr;
    decltype(&cuFuncGetAttribute) p_cuFuncGetAttribute = nullptr;
    decltype(&cuOccupancyMaxActiveBlocksPerMultiprocessor) p_cuOccupancy = nullptr;
    decltype(&cuCtxSynchronize) p_cuCtxSynchronize = nullptr;
    decltype(&cuEventCreate) p_cuEventCreate = nullptr;
    decltype(&cuEventRecord) p_cuEventRecord = nullptr;
    decltype(&cuEventSynchronize) p_cuEventSynchronize = nullptr;
    decltype(&cuEventElapsedTime) p_cuEventElapsedTime = nullptr;
    decltype(&cuEventDestroy) p_cuEventDestroy = nullptr;
    decltype(&cuGetErrorName) p_cuGetErrorName = nullptr;
    decltype(&cuGetErrorString) p_cuGetErrorString = nullptr;

    decltype(&nvrtcCreateProgram) p_nvrtcCreateProgram = nullptr;
    decltype(&nvrtcCompileProgram) p_nvrtcCompileProgram = nullptr;
    decltype(&nvrtcGetProgramLogSize) p_nvrtcGetProgramLogSize = nullptr;
    decltype(&nvrtcGetProgramLog) p_nvrtcGetProgramLog = nullptr;
    decltype(&nvrtcGetCUBINSize) p_nvrtcGetCUBINSize = nullptr;
    decltype(&nvrtcGetCUBIN) p_nvrtcGetCUBIN = nullptr;
    decltype(&nvrtcGetPTXSize) p_nvrtcGetPTXSize = nullptr;
    decltype(&nvrtcGetPTX) p_nvrtcGetPTX = nullptr;
    decltype(&nvrtcGetErrorString) p_nvrtcGetErrorString = nullptr;
    decltype(&nvrtcVersion) p_nvrtcVersion = nullptr;
    decltype(&nvrtcDestroyProgram) p_nvrtcDestroyProgram = nullptr;

    ~Impl() { shutdown(); }

    bool check(CUresult result, const char *what, std::string &error) {
        if (result == CUDA_SUCCESS) return true;
        const char *name = "CUDA_ERROR";
        const char *text = "";
        if (p_cuGetErrorName) p_cuGetErrorName(result, &name);
        if (p_cuGetErrorString) p_cuGetErrorString(result, &text);
        error = std::string(what) + " 失败: " + (name ? name : "") + " (" + (text ? text : "") + ")";
        last_error = error;
        return false;
    }

    bool make_current() {
        return p_cuCtxSetCurrent && p_cuCtxSetCurrent(context) == CUDA_SUCCESS;
    }

    bool load_driver(std::string &error) {
        nvcuda = LoadLibraryA("nvcuda.dll");
        if (!nvcuda) {
            error = "无法加载 nvcuda.dll，请确认已安装 NVIDIA 显卡驱动";
            return false;
        }
        bool ok = true;
        ok &= load_symbol(nvcuda, "cuInit", p_cuInit, error);
        ok &= load_symbol(nvcuda, "cuDriverGetVersion", p_cuDriverGetVersion, error);
        ok &= load_symbol(nvcuda, "cuDeviceGet", p_cuDeviceGet, error);
        ok &= load_symbol(nvcuda, "cuDeviceGetCount", p_cuDeviceGetCount, error);
        ok &= load_symbol(nvcuda, "cuDeviceGetName", p_cuDeviceGetName, error);
        ok &= load_symbol(nvcuda, "cuDeviceGetAttribute", p_cuDeviceGetAttribute, error);
        ok &= load_symbol(nvcuda, "cuDevicePrimaryCtxRetain", p_cuDevicePrimaryCtxRetain, error);
        ok &= load_symbol(nvcuda, "cuDevicePrimaryCtxRelease", p_cuDevicePrimaryCtxRelease, error);
        ok &= load_symbol(nvcuda, "cuCtxSetCurrent", p_cuCtxSetCurrent, error);
        ok &= load_symbol(nvcuda, "cuMemAlloc_v2", p_cuMemAlloc, error);
        ok &= load_symbol(nvcuda, "cuMemFree_v2", p_cuMemFree, error);
        ok &= load_symbol(nvcuda, "cuMemGetInfo_v2", p_cuMemGetInfo, error);
        ok &= load_symbol(nvcuda, "cuMemcpyHtoD_v2", p_cuMemcpyHtoD, error);
        ok &= load_symbol(nvcuda, "cuMemcpyDtoH_v2", p_cuMemcpyDtoH, error);
        ok &= load_symbol(nvcuda, "cuMemsetD32_v2", p_cuMemsetD32, error);
        ok &= load_symbol(nvcuda, "cuModuleLoadData", p_cuModuleLoadData, error);
        ok &= load_symbol(nvcuda, "cuModuleUnload", p_cuModuleUnload, error);
        ok &= load_symbol(nvcuda, "cuModuleGetFunction", p_cuModuleGetFunction, error);
        ok &= load_symbol(nvcuda, "cuLaunchKernel", p_cuLaunchKernel, error);
        ok &= load_symbol(nvcuda, "cuFuncGetAttribute", p_cuFuncGetAttribute, error);
        ok &= load_symbol(nvcuda, "cuOccupancyMaxActiveBlocksPerMultiprocessor", p_cuOccupancy, error);
        ok &= load_symbol(nvcuda, "cuCtxSynchronize", p_cuCtxSynchronize, error);
        ok &= load_symbol(nvcuda, "cuEventCreate", p_cuEventCreate, error);
        ok &= load_symbol(nvcuda, "cuEventRecord", p_cuEventRecord, error);
        ok &= load_symbol(nvcuda, "cuEventSynchronize", p_cuEventSynchronize, error);
        ok &= load_symbol(nvcuda, "cuEventElapsedTime", p_cuEventElapsedTime, error);
        ok &= load_symbol(nvcuda, "cuEventDestroy_v2", p_cuEventDestroy, error);
        ok &= load_symbol(nvcuda, "cuGetErrorName", p_cuGetErrorName, error);
        ok &= load_symbol(nvcuda, "cuGetErrorString", p_cuGetErrorString, error);
        return ok;
    }

    bool load_nvrtc(std::string &error) {
        const char *candidates[] = {
            "nvrtc64_130_0.dll", "nvrtc64_120_0.dll", "nvrtc64_112_0.dll",
            "nvrtc64_111_0.dll", "nvrtc64_110_0.dll", "nvrtc64_102_0.dll",
            "nvrtc64_101_0.dll", "nvrtc64_100_0.dll", "nvrtc64_90.dll"};
        for (const char *name : candidates) {
            nvrtc = LoadLibraryA(name);
            if (nvrtc) {
                info.nvrtc_dll = name;
                break;
            }
        }
        if (!nvrtc) {
            error = "无法加载 NVRTC DLL，请确认 CUDA Toolkit 的 bin 目录已加入 PATH";
            return false;
        }
        bool ok = true;
        ok &= load_symbol(nvrtc, "nvrtcCreateProgram", p_nvrtcCreateProgram, error);
        ok &= load_symbol(nvrtc, "nvrtcCompileProgram", p_nvrtcCompileProgram, error);
        ok &= load_symbol(nvrtc, "nvrtcGetProgramLogSize", p_nvrtcGetProgramLogSize, error);
        ok &= load_symbol(nvrtc, "nvrtcGetProgramLog", p_nvrtcGetProgramLog, error);
        ok &= load_symbol(nvrtc, "nvrtcGetCUBINSize", p_nvrtcGetCUBINSize, error);
        ok &= load_symbol(nvrtc, "nvrtcGetCUBIN", p_nvrtcGetCUBIN, error);
        ok &= load_symbol(nvrtc, "nvrtcGetPTXSize", p_nvrtcGetPTXSize, error);
        ok &= load_symbol(nvrtc, "nvrtcGetPTX", p_nvrtcGetPTX, error);
        ok &= load_symbol(nvrtc, "nvrtcGetErrorString", p_nvrtcGetErrorString, error);
        ok &= load_symbol(nvrtc, "nvrtcVersion", p_nvrtcVersion, error);
        ok &= load_symbol(nvrtc, "nvrtcDestroyProgram", p_nvrtcDestroyProgram, error);
        return ok;
    }

    bool compile_kernel(const std::string &source, std::string &error) {
        nvrtcProgram program = nullptr;
        nvrtcResult r = p_nvrtcCreateProgram(&program, source.c_str(), "kernels.cu", 0, nullptr, nullptr);
        if (r != NVRTC_SUCCESS) {
            error = std::string("NVRTC 创建程序失败: ") + p_nvrtcGetErrorString(r);
            return false;
        }
        std::string arch = "sm_" + std::to_string(info.compute_major) + std::to_string(info.compute_minor);
        const char *options[] = {"--std=c++17", "-lineinfo", nullptr, "--device-as-default-execution-space"};
        std::string arch_option = "--gpu-architecture=" + arch;
        options[2] = arch_option.c_str();
        r = p_nvrtcCompileProgram(program, 3, options);
        size_t log_size = 0;
        p_nvrtcGetProgramLogSize(program, &log_size);
        if (log_size > 1) {
            std::string log(log_size, '\0');
            p_nvrtcGetProgramLog(program, log.data());
            if (r != NVRTC_SUCCESS) error = "NVRTC 编译失败:\n" + log;
        }
        if (r != NVRTC_SUCCESS) {
            if (error.empty()) error = std::string("NVRTC 编译失败: ") + p_nvrtcGetErrorString(r);
            p_nvrtcDestroyProgram(&program);
            return false;
        }

        std::vector<char> binary;
        bool is_cubin = false;
        size_t size = 0;
        if (p_nvrtcGetCUBINSize(program, &size) == NVRTC_SUCCESS && size > 0) {
            binary.resize(size);
            if (p_nvrtcGetCUBIN(program, binary.data()) == NVRTC_SUCCESS) is_cubin = true;
        }
        if (!is_cubin) {
            if (p_nvrtcGetPTXSize(program, &size) != NVRTC_SUCCESS || size == 0) {
                error = "NVRTC 无法生成 CUBIN 或 PTX";
                p_nvrtcDestroyProgram(&program);
                return false;
            }
            binary.resize(size);
            if (p_nvrtcGetPTX(program, binary.data()) != NVRTC_SUCCESS) {
                error = "NVRTC 获取 PTX 失败";
                p_nvrtcDestroyProgram(&program);
                return false;
            }
        }
        p_nvrtcDestroyProgram(&program);

        CUresult cr = p_cuModuleLoadData(&module, binary.data());
        if (cr != CUDA_SUCCESS) {
            check(cr, "加载 CUDA 模块", error);
            return false;
        }
        struct FnSpec { const char *name; CUfunction *fn; };
        FnSpec specs[] = {
            {"salt_pepper_kernel", &fn_noise},
            {"median3_kernel", &fn_median3},
            {"median5_kernel", &fn_median5},
            {"median7_kernel", &fn_median7},
            {"fused3_kernel", &fn_fused3},
            {"fused5_kernel", &fn_fused5},
            {"fused7_kernel", &fn_fused7},
            {"stats_kernel", &fn_stats},
        };
        for (const auto &spec : specs) {
            cr = p_cuModuleGetFunction(spec.fn, module, spec.name);
            if (cr != CUDA_SUCCESS) {
                check(cr, std::string("查找内核 ").append(spec.name).c_str(), error);
                return false;
            }
        }
        return true;
    }

    bool initialize(const std::filesystem::path &kernel_source, std::string &error) {
        if (!load_driver(error)) return false;
        CUresult r = p_cuInit(0);
        if (!check(r, "cuInit", error)) return false;

        int count = 0;
        r = p_cuDeviceGetCount(&count);
        if (!check(r, "cuDeviceGetCount", error)) return false;
        if (count <= 0) {
            error = "未检测到可用的 NVIDIA GPU";
            return false;
        }
        r = p_cuDeviceGet(&device, 0);
        if (!check(r, "cuDeviceGet", error)) return false;

        char name[256] = {};
        p_cuDeviceGetName(name, sizeof(name), device);
        info.name = name;
        p_cuDeviceGetAttribute(&info.compute_major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, device);
        p_cuDeviceGetAttribute(&info.compute_minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, device);
        p_cuDeviceGetAttribute(&info.sm_count, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, device);
        p_cuDeviceGetAttribute(&info.max_threads_per_sm,
                               CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_MULTIPROCESSOR, device);
        p_cuDeviceGetAttribute(&info.max_shared_memory_per_sm,
                               CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_MULTIPROCESSOR, device);
        p_cuDeviceGetAttribute(&info.l2_cache_size, CU_DEVICE_ATTRIBUTE_L2_CACHE_SIZE, device);
        p_cuDeviceGetAttribute(&info.memory_bus_width, CU_DEVICE_ATTRIBUTE_GLOBAL_MEMORY_BUS_WIDTH, device);
        p_cuDeviceGetAttribute(&info.memory_clock_khz, CU_DEVICE_ATTRIBUTE_MEMORY_CLOCK_RATE, device);
        p_cuDriverGetVersion(&info.driver_version);

        r = p_cuDevicePrimaryCtxRetain(&context, device);
        if (!check(r, "cuDevicePrimaryCtxRetain", error)) return false;
        if (!make_current()) {
            error = "无法设置 CUDA 上下文";
            return false;
        }
        size_t total_mem = 0, free_mem = 0;
        p_cuMemGetInfo(&free_mem, &total_mem);
        info.total_memory = total_mem;
        info.free_memory_at_init = free_mem;

        if (!load_nvrtc(error)) return false;
        int nvrtc_major = 0, nvrtc_minor = 0;
        if (p_nvrtcVersion) p_nvrtcVersion(&nvrtc_major, &nvrtc_minor);
        info.nvrtc_version = nvrtc_major * 100 + nvrtc_minor;
        std::string source = read_text_file(kernel_source, error);
        if (source.empty()) return false;
        return compile_kernel(source, error);
    }

    void shutdown() {
        if (context && make_current()) {
            if (module && p_cuModuleUnload) {
                p_cuModuleUnload(module);
                module = nullptr;
            }
        }
        if (context && p_cuDevicePrimaryCtxRelease) {
            p_cuDevicePrimaryCtxRelease(device);
            context = nullptr;
        }
        if (nvcuda) {
            FreeLibrary(nvcuda);
            nvcuda = nullptr;
        }
        if (nvrtc) {
            FreeLibrary(nvrtc);
            nvrtc = nullptr;
        }
    }

    bool launch(CUfunction fn, LaunchDim grid, LaunchDim block, void **args, const char *what, std::string &error) {
        CUresult r = p_cuLaunchKernel(fn, grid.x, grid.y, grid.z, block.x, block.y, block.z,
                                      0, nullptr, args, nullptr);
        return check(r, what, error);
    }
};

CudaBackend::CudaBackend() : impl_(new Impl()) {}

CudaBackend::~CudaBackend() = default;

bool CudaBackend::initialize(const std::filesystem::path &kernel_source, std::string &error) {
    error.clear();
    bool ok = impl_->initialize(kernel_source, error);
    impl_->last_error = error;
    return ok;
}

bool CudaBackend::ready() const {
    return impl_ && impl_->module != nullptr;
}

const GpuInfo &CudaBackend::gpu_info() const {
    return impl_->info;
}

KernelResource CudaBackend::kernel_resource(bool fused, int kernel_size) const {
    KernelResource r;
    if (!ready()) return r;
    CUfunction fn = nullptr;
    if (fused) {
        fn = kernel_size == 3 ? impl_->fn_fused3 :
             (kernel_size == 5 ? impl_->fn_fused5 : impl_->fn_fused7);
    } else {
        fn = kernel_size == 3 ? impl_->fn_median3 :
             (kernel_size == 5 ? impl_->fn_median5 : impl_->fn_median7);
    }
    if (!fn || !impl_->p_cuFuncGetAttribute) return r;
    impl_->p_cuFuncGetAttribute(&r.registers_per_thread, CU_FUNC_ATTRIBUTE_NUM_REGS, fn);
    impl_->p_cuFuncGetAttribute(&r.static_shared_bytes, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, fn);
    impl_->p_cuFuncGetAttribute(&r.local_bytes, CU_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES, fn);
    impl_->p_cuFuncGetAttribute(&r.max_threads_per_block, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, fn);
    int blocks = 0;
    if (impl_->p_cuOccupancy) {
        impl_->p_cuOccupancy(&blocks, fn, r.threads_per_block, 0);
    }
    r.active_blocks_per_sm = blocks;
    if (impl_->info.max_threads_per_sm > 0) {
        r.theoretical_occupancy =
            (double)blocks * (double)r.threads_per_block / (double)impl_->info.max_threads_per_sm;
    }
    return r;
}

std::string CudaBackend::last_error() const {
    return impl_->last_error;
}

namespace {

struct DevBuffer {
    CUdeviceptr ptr = 0;
    std::function<void(CUdeviceptr)> free_fn;
    ~DevBuffer() {
        if (ptr && free_fn) free_fn(ptr);
    }
};

struct EventSet {
    CUevent events[6] = {};
    std::function<void(CUevent)> destroy_fn;
    ~EventSet() {
        for (auto &e : events) {
            if (e && destroy_fn) destroy_fn(e);
        }
    }
};

double psnr_from_mse(double mse) {
    if (mse <= 1e-12) return 99.0;
    return 10.0 * std::log10((255.0 * 255.0) / mse);
}

}  // namespace

bool CudaBackend::process(const uint8_t *input, int count, int width, int height,
                          float noise_ratio, int kernel_size, ProcessingMode mode,
                          std::vector<uint8_t> &noisy, std::vector<uint8_t> &filtered,
                          std::vector<ImageQuality> &quality, StageTimes &times,
                          std::string &error, ProgressFn progress) {
    error.clear();
    times = StageTimes{};
    quality.clear();
    if (!ready()) {
        error = "CUDA 后端尚未初始化";
        return false;
    }
    if (!input || count <= 0 || width <= 0 || height <= 0) {
        error = "输入参数无效";
        return false;
    }
    if (kernel_size != 3 && kernel_size != 5 && kernel_size != 7) {
        error = "滤波窗口只支持 3、5、7";
        return false;
    }
    if (noise_ratio < 0.0f) noise_ratio = 0.0f;
    if (noise_ratio > 0.5f) noise_ratio = 0.5f;
    impl_->make_current();

    const size_t image_bytes = (size_t)width * (size_t)height * 3u;
    if ((size_t)count > SIZE_MAX / image_bytes) {
        error = "批量数据尺寸溢出";
        return false;
    }
    const size_t total_bytes = image_bytes * (size_t)count;

    size_t free_mem = 0, total_mem = 0;
    impl_->p_cuMemGetInfo(&free_mem, &total_mem);
    const size_t stats_bytes = (size_t)count * (sizeof(double) * 2 + sizeof(unsigned long long));
    const size_t required = total_bytes * 3 + stats_bytes + (size_t)64 * 1024 * 1024;
    if (required > free_mem) {
        error = "GPU 显存不足：需要约 " + std::to_string(required / (1024 * 1024)) +
                " MiB，当前可用 " + std::to_string(free_mem / (1024 * 1024)) + " MiB";
        return false;
    }

    try {
        noisy.resize(total_bytes);
        filtered.resize(total_bytes);
    } catch (const std::bad_alloc &) {
        error = "主机内存不足，无法分配批量图像缓冲区";
        return false;
    }

    DevBuffer d_in, d_noisy, d_out, d_mse_n, d_mse_f, d_imp;
    auto alloc = [&](DevBuffer &buf, size_t bytes, const char *what) -> bool {
        CUresult r = impl_->p_cuMemAlloc(&buf.ptr, bytes);
        if (!impl_->check(r, what, error)) return false;
        buf.free_fn = [this](CUdeviceptr p) { impl_->p_cuMemFree(p); };
        return true;
    };
    if (!alloc(d_in, total_bytes, "分配输入显存") ||
        !alloc(d_noisy, total_bytes, "分配加噪显存") ||
        !alloc(d_out, total_bytes, "分配输出显存") ||
        !alloc(d_mse_n, (size_t)count * sizeof(double), "分配统计显存") ||
        !alloc(d_mse_f, (size_t)count * sizeof(double), "分配统计显存") ||
        !alloc(d_imp, (size_t)count * sizeof(unsigned long long), "分配统计显存")) {
        return false;
    }

    EventSet events;
    events.destroy_fn = [this](CUevent e) { impl_->p_cuEventDestroy(e); };
    for (auto &e : events.events) {
        CUresult r = impl_->p_cuEventCreate(&e, CU_EVENT_DEFAULT);
        if (!impl_->check(r, "创建 CUDA 事件", error)) return false;
    }
    auto record = [&](int i) { impl_->p_cuEventRecord(events.events[i], nullptr); };
    auto elapsed = [&](int a, int b) {
        float ms = 0.0f;
        impl_->p_cuEventElapsedTime(&ms, events.events[a], events.events[b]);
        return (double)ms;
    };

    double wall_start = now_ms();
    record(0);
    CUresult r = impl_->p_cuMemcpyHtoD(d_in.ptr, input, total_bytes);
    if (!impl_->check(r, "输入数据拷贝到 GPU", error)) return false;
    record(1);
    if (progress) progress(20);

    const LaunchDim block(16, 16, 1);
    const LaunchDim grid((width + 15) / 16, (height + 15) / 16, count);
    const unsigned int seed = 12345u;
    int write_noisy = 1;

    if (mode == ProcessingMode::PerImage) {
        const LaunchDim g1((width + 15) / 16, (height + 15) / 16, 1);
        for (int i = 0; i < count; ++i) {
            size_t offset = (size_t)i * image_bytes;
            CUdeviceptr in_ptr = d_in.ptr + (CUdeviceptr)offset;
            CUdeviceptr noisy_ptr = d_noisy.ptr + (CUdeviceptr)offset;
            CUdeviceptr out_ptr = d_out.ptr + (CUdeviceptr)offset;
            int w = width, h = height;
            float p = noise_ratio;
            unsigned int image_seed = seed ^ ((unsigned int)i * 0x9e3779b9u);
            void *noise_args[] = {&in_ptr, &noisy_ptr, &w, &h, &p, (void *)&image_seed};
            if (!impl_->launch(impl_->fn_noise, g1, block, noise_args, "椒盐噪声内核", error)) return false;
            CUfunction median_fn = kernel_size == 3 ? impl_->fn_median3 :
                                   (kernel_size == 5 ? impl_->fn_median5 : impl_->fn_median7);
            void *median_args[] = {&noisy_ptr, &out_ptr, &w, &h};
            if (!impl_->launch(median_fn, g1, block, median_args, "中值滤波内核", error)) return false;
            r = impl_->p_cuMemcpyDtoH(noisy.data() + offset, noisy_ptr, image_bytes);
            if (!impl_->check(r, "加噪结果拷贝回主机", error)) return false;
            r = impl_->p_cuMemcpyDtoH(filtered.data() + offset, out_ptr, image_bytes);
            if (!impl_->check(r, "滤波结果拷贝回主机", error)) return false;
        }
        impl_->p_cuCtxSynchronize();
    } else {
        void *noise_args[] = {&d_in.ptr, &d_noisy.ptr, (void *)&width, (void *)&height,
                              (void *)&noise_ratio, (void *)&seed};
        if (mode == ProcessingMode::Separate) {
            if (!impl_->launch(impl_->fn_noise, grid, block, noise_args, "椒盐噪声内核", error)) return false;
            record(2);
            const int k = kernel_size;
            CUfunction median_fn = k == 3 ? impl_->fn_median3 : (k == 5 ? impl_->fn_median5 : impl_->fn_median7);
            void *median_args[] = {&d_noisy.ptr, &d_out.ptr, (void *)&width, (void *)&height};
            if (!impl_->launch(median_fn, grid, block, median_args, "中值滤波内核", error)) return false;
            record(3);
        } else {
            int w = width, h = height;
            float p = noise_ratio;
            CUfunction fused_fn = kernel_size == 3 ? impl_->fn_fused3 :
                                  (kernel_size == 5 ? impl_->fn_fused5 : impl_->fn_fused7);
            void *fused_args[] = {&d_in.ptr, &d_noisy.ptr, &d_out.ptr, &w, &h, &p,
                                  (void *)&seed, &write_noisy};
            if (!impl_->launch(fused_fn, grid, block, fused_args, "融合内核", error)) return false;
            record(2);
        }
    }
    if (progress) progress(75);

    // Statistics run on the GPU.  This avoids a second full CPU pass over the
    // 1.2 GiB batch and is included in the measured GPU time.
    record(mode == ProcessingMode::Separate ? 3 : 2);
    void *stats_args[] = {&d_in.ptr, &d_noisy.ptr, &d_out.ptr,
                          &d_mse_n.ptr, &d_mse_f.ptr, &d_imp.ptr,
                          (void *)&width, (void *)&height};
    if (!impl_->launch(impl_->fn_stats, LaunchDim(count, 1, 1), LaunchDim(256, 1, 1), stats_args,
                       "质量统计内核", error)) return false;
    int after_kernel = mode == ProcessingMode::Separate ? 3 : 2;
    record(after_kernel + 1);

    if (mode != ProcessingMode::PerImage) {
        r = impl_->p_cuMemcpyDtoH(noisy.data(), d_noisy.ptr, total_bytes);
        if (!impl_->check(r, "加噪结果拷贝回主机", error)) return false;
        r = impl_->p_cuMemcpyDtoH(filtered.data(), d_out.ptr, total_bytes);
        if (!impl_->check(r, "滤波结果拷贝回主机", error)) return false;
        record(after_kernel + 2);
    }
    r = impl_->p_cuCtxSynchronize();
    if (!impl_->check(r, "同步 GPU", error)) return false;

    std::vector<double> mse_n((size_t)count), mse_f((size_t)count);
    std::vector<unsigned long long> imp((size_t)count);
    r = impl_->p_cuMemcpyDtoH(mse_n.data(), d_mse_n.ptr, (size_t)count * sizeof(double));
    if (!impl_->check(r, "读取统计结果", error)) return false;
    r = impl_->p_cuMemcpyDtoH(mse_f.data(), d_mse_f.ptr, (size_t)count * sizeof(double));
    if (!impl_->check(r, "读取统计结果", error)) return false;
    r = impl_->p_cuMemcpyDtoH(imp.data(), d_imp.ptr, (size_t)count * sizeof(unsigned long long));
    if (!impl_->check(r, "读取统计结果", error)) return false;

    double wall_end = now_ms();
    times.wall_ms = wall_end - wall_start;
    if (mode == ProcessingMode::PerImage) {
        times.gpu_total_ms = times.wall_ms;
        times.h2d_ms = 0.0;
        times.noise_ms = 0.0;
        times.median_ms = 0.0;
        times.kernel_ms = 0.0;
        times.d2h_ms = 0.0;
        times.stats_ms = 0.0;
    } else if (mode == ProcessingMode::Separate) {
        times.h2d_ms = elapsed(0, 1);
        times.noise_ms = elapsed(1, 2);
        times.median_ms = elapsed(2, 3);
        times.kernel_ms = times.noise_ms + times.median_ms;
        times.stats_ms = elapsed(3, 4);
        times.d2h_ms = elapsed(4, 5);
        times.gpu_total_ms = elapsed(0, 5);
    } else {
        times.h2d_ms = elapsed(0, 1);
        times.kernel_ms = elapsed(1, 2);
        times.noise_ms = 0.0;
        times.median_ms = 0.0;
        times.stats_ms = elapsed(2, 3);
        times.d2h_ms = elapsed(3, 4);
        times.gpu_total_ms = elapsed(0, 4);
    }

    quality.resize((size_t)count);
    double pixels = (double)width * (double)height;
    for (int i = 0; i < count; ++i) {
        quality[(size_t)i].psnr_noisy = psnr_from_mse(mse_n[(size_t)i]);
        quality[(size_t)i].psnr_filtered = psnr_from_mse(mse_f[(size_t)i]);
        quality[(size_t)i].noise_ratio = pixels > 0 ? (double)imp[(size_t)i] / pixels : 0.0;
    }
    if (progress) progress(100);
    return true;
}

#include "benchmark.h"

#include "common.h"
#include "cuda_backend.h"
#include "image_io.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <new>
#include <sstream>
#include <thread>
#include <vector>

namespace {

struct Options {
    std::filesystem::path input;
    std::filesystem::path out_dir;
    std::filesystem::path csv;
    int count = 1000;
    float noise = 0.05f;
    int kernel = 3;
    std::string mode = "fused";
    int repeat = 1;
    int warmup = 1;
    int threads = 0;
    int quality = 92;
    bool save_noisy = false;
    bool save_denoised = false;
    bool quiet = false;
};

void usage() {
    std::cout
        << "CUDA salt-and-pepper median filter benchmark\n"
        << "  --input DIR        image folder (default: built-in samples / D:\\...\\image)\n"
        << "  --count N          number of images (default 1000)\n"
        << "  --noise P          impulse probability, 0..0.5 (default 0.05)\n"
        << "  --kernel K         3, 5 or 7 (default 3)\n"
        << "  --mode M           fused | separate | perimage | cpu | selftest | info\n"
        << "  --repeat N         timed repeats (default 1)\n"
        << "  --warmup N         warm-up runs (default 1)\n"
        << "  --threads N        CPU worker threads (default: hardware)\n"
        << "  --save MODE        none | denoised | both (default none)\n"
        << "  --out DIR          output folder for saved images\n"
        << "  --csv FILE         append one CSV row per timed run\n"
        << "  --quality Q        JPEG quality for saved images (default 92)\n"
        << "  --quiet            only print the final summary\n";
}

bool parse_args(int argc, char **argv, Options &o, bool &show_help) {
    show_help = false;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto need = [&](std::string &v) -> bool {
            if (i + 1 >= argc) return false;
            v = argv[++i];
            return true;
        };
        std::string v;
        if (k == "--help" || k == "-h") {
            show_help = true;
        } else if (k == "--input" && need(v)) {
            o.input = utf8_to_wstring(v);
        } else if (k == "--count" && need(v)) {
            o.count = std::max(1, std::stoi(v));
        } else if (k == "--limit" && need(v)) {
            o.count = std::max(1, std::stoi(v));
        } else if (k == "--noise" && need(v)) {
            o.noise = std::stof(v);
        } else if (k == "--kernel" && need(v)) {
            o.kernel = std::stoi(v);
        } else if (k == "--mode" && need(v)) {
            o.mode = v;
        } else if (k == "--repeat" && need(v)) {
            o.repeat = std::max(1, std::stoi(v));
        } else if (k == "--warmup" && need(v)) {
            o.warmup = std::max(0, std::stoi(v));
        } else if (k == "--threads" && need(v)) {
            o.threads = std::max(0, std::stoi(v));
        } else if (k == "--save" && need(v)) {
            if (v == "both") {
                o.save_noisy = true;
                o.save_denoised = true;
            } else if (v == "denoised") {
                o.save_denoised = true;
            } else if (v == "none") {
                o.save_noisy = o.save_denoised = false;
            } else {
                std::cerr << "invalid --save value: " << v << "\n";
                return false;
            }
        } else if (k == "--out" && need(v)) {
            o.out_dir = utf8_to_wstring(v);
        } else if (k == "--csv" && need(v)) {
            o.csv = utf8_to_wstring(v);
        } else if (k == "--quality" && need(v)) {
            o.quality = std::max(1, std::min(100, std::stoi(v)));
        } else if (k == "--quiet") {
            o.quiet = true;
        } else {
            std::cerr << "unknown argument: " << k << "\n";
            return false;
        }
    }
    return true;
}

unsigned int pcg_hash_host(unsigned int x) {
    unsigned int state = x * 747796405u + 2891336453u;
    unsigned int word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float u01_host(unsigned int x) {
    return (float)(x >> 8) * (1.0f / 16777216.0f);
}

unsigned char add_impulse_host(unsigned char v, unsigned int seed, int img,
                               int x, int y, int w, float p, bool &hit) {
    unsigned int h = pcg_hash_host((unsigned int)((unsigned long long)y * (unsigned long long)w +
                                                  (unsigned long long)x) ^
                                   seed ^ ((unsigned int)img * 0x9e3779b9u));
    unsigned int h2 = pcg_hash_host(h ^ 0x85ebca6bu);
    hit = u01_host(h) < p;
    if (hit) return (h2 & 1u) ? (unsigned char)255 : (unsigned char)0;
    return v;
}

unsigned char median_host(std::array<unsigned char, 49> &v, int n) {
    for (int i = 1; i < n; ++i) {
        unsigned char key = v[(size_t)i];
        int j = i - 1;
        while (j >= 0 && v[(size_t)j] > key) {
            v[(size_t)j + 1] = v[(size_t)j];
            --j;
        }
        v[(size_t)j + 1] = key;
    }
    return v[(size_t)(n / 2)];
}

void cpu_noise_filter(const uint8_t *in, uint8_t *noisy, uint8_t *out,
                      int w, int h, int img, float p, int radius) {
    size_t image_bytes = (size_t)w * (size_t)h * 3u;
    const uint8_t *src = in + (size_t)img * image_bytes;
    uint8_t *nimg = noisy + (size_t)img * image_bytes;
    uint8_t *oimg = out + (size_t)img * image_bytes;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t off = ((size_t)y * (size_t)w + (size_t)x) * 3u;
            bool hit = false;
            unsigned char value = add_impulse_host(src[off], 12345u, img, x, y, w, p, hit);
            nimg[off] = hit ? value : src[off];
            nimg[off + 1] = hit ? value : src[off + 1];
            nimg[off + 2] = hit ? value : src[off + 2];
        }
    }

    const int K = 2 * radius + 1;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t off = ((size_t)y * (size_t)w + (size_t)x) * 3u;
            for (int c = 0; c < 3; ++c) {
                std::array<unsigned char, 49> v{};
                int n = 0;
                for (int dy = -radius; dy <= radius; ++dy) {
                    int yy = std::max(0, std::min(h - 1, y + dy));
                    for (int dx = -radius; dx <= radius; ++dx) {
                        int xx = std::max(0, std::min(w - 1, x + dx));
                        v[(size_t)n++] = nimg[((size_t)yy * (size_t)w + (size_t)xx) * 3u + c];
                    }
                }
                oimg[off + c] = median_host(v, K * K);
            }
        }
    }
}

std::vector<ImageQuality> quality_cpu(const uint8_t *orig, const uint8_t *noisy,
                                      const uint8_t *filtered, int count, int w, int h) {
    std::vector<ImageQuality> q((size_t)count);
    size_t pixels = (size_t)w * (size_t)h;
    size_t bytes = pixels * 3u;
    for (int i = 0; i < count; ++i) {
        double sn = 0.0, sf = 0.0;
        unsigned long long impulses = 0;
        const uint8_t *a = orig + (size_t)i * bytes;
        const uint8_t *bn = noisy + (size_t)i * bytes;
        const uint8_t *bf = filtered + (size_t)i * bytes;
        for (size_t p = 0; p < pixels; ++p) {
            size_t j = p * 3u;
            unsigned char nv = bn[j];
            if ((nv == 0 || nv == 255) && bn[j + 1] == nv && bn[j + 2] == nv &&
                (a[j] != nv || a[j + 1] != nv || a[j + 2] != nv)) {
                ++impulses;
            }
            for (int c = 0; c < 3; ++c) {
                double dn = (double)bn[j + c] - (double)a[j + c];
                double df = (double)bf[j + c] - (double)a[j + c];
                sn += dn * dn;
                sf += df * df;
            }
        }
        double msen = sn / (double)bytes;
        double msef = sf / (double)bytes;
        q[(size_t)i].psnr_noisy = msen <= 1e-12 ? 99.0 : 10.0 * std::log10(65025.0 / msen);
        q[(size_t)i].psnr_filtered = msef <= 1e-12 ? 99.0 : 10.0 * std::log10(65025.0 / msef);
        q[(size_t)i].noise_ratio = (double)impulses / (double)pixels;
    }
    return q;
}

double average_psnr(const std::vector<ImageQuality> &q, bool filtered) {
    if (q.empty()) return 0.0;
    double s = 0.0;
    for (const auto &v : q) s += filtered ? v.psnr_filtered : v.psnr_noisy;
    return s / (double)q.size();
}

double average_noise(const std::vector<ImageQuality> &q) {
    if (q.empty()) return 0.0;
    double s = 0.0;
    for (const auto &v : q) s += v.noise_ratio;
    return s / (double)q.size();
}

double min_psnr_filtered(const std::vector<ImageQuality> &q) {
    if (q.empty()) return 0.0;
    double v = q[0].psnr_filtered;
    for (const auto &x : q) v = std::min(v, x.psnr_filtered);
    return v;
}

std::string sanitize(std::string s) {
    for (char &c : s) {
        if (c == ',' || c == '\n' || c == '\r') c = ' ';
    }
    return s;
}

std::filesystem::path find_kernel_file() {
    std::error_code ec;
    std::vector<std::filesystem::path> candidates = {
        std::filesystem::path(L"src") / L"kernels.cu",
        std::filesystem::current_path() / L"src" / L"kernels.cu",
        executable_dir() / L"kernels.cu",
        executable_dir() / L"src" / L"kernels.cu",
        executable_dir().parent_path() / L"src" / L"kernels.cu",
    };
    for (const auto &p : candidates) {
        if (std::filesystem::exists(p, ec)) return p;
    }
    return candidates.back();
}

void append_csv(const std::filesystem::path &path, const std::string &line) {
    if (path.empty()) return;
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    bool exists = std::filesystem::exists(path, ec);
    std::ofstream out(path, std::ios::app);
    if (!out) return;
    if (!exists || std::filesystem::file_size(path, ec) == 0) {
        out << "timestamp,run_id,gpu,mode,count,width,height,noise,kernel,repeat,"
               "h2d_ms,noise_ms,median_ms,kernel_ms,stats_ms,d2h_ms,gpu_total_ms,wall_ms,"
               "images_per_sec,psnr_noisy_avg,psnr_filtered_avg,psnr_filtered_min,"
               "noise_actual_avg\n";
    }
    out << line << "\n";
}

std::string make_csv_line(const std::string &gpu, const Options &o, const std::string &mode,
                          int count, int w, int h, int repeat, const StageTimes &t,
                          const std::vector<ImageQuality> &q) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4);
    ss << (int)time(nullptr) << ",run," << sanitize(gpu) << "," << mode << ","
       << count << "," << w << "," << h << "," << o.noise << "," << o.kernel << ","
       << repeat << "," << t.h2d_ms << "," << t.noise_ms << "," << t.median_ms << ","
       << t.kernel_ms << "," << t.stats_ms << "," << t.d2h_ms << "," << t.gpu_total_ms
       << "," << t.wall_ms << ","
       << (t.wall_ms > 0 ? (double)count / (t.wall_ms / 1000.0) : 0.0) << ","
       << average_psnr(q, false) << "," << average_psnr(q, true) << ","
       << min_psnr_filtered(q) << "," << average_noise(q);
    return ss.str();
}

void print_stage_times(const StageTimes &t) {
    std::cout << std::fixed << std::setprecision(3)
              << "  H2D " << t.h2d_ms << " ms | noise " << t.noise_ms
              << " ms | median " << t.median_ms << " ms | fused/kernel " << t.kernel_ms
              << " ms | stats " << t.stats_ms << " ms | D2H " << t.d2h_ms
              << " ms | GPU total " << t.gpu_total_ms << " ms | wall " << t.wall_ms << " ms\n";
}

int selftest(CudaBackend &backend) {
    const int w = 63, h = 47;
    std::vector<uint8_t> input((size_t)w * h * 3);
    unsigned int state = 1;
    for (auto &v : input) {
        state = state * 1664525u + 1013904223u;
        v = (uint8_t)(state >> 24);
    }
    int failures = 0;
    for (int k : {3, 5, 7}) {
        int radius = (k - 1) / 2;
        std::vector<uint8_t> cn((size_t)w * h * 3), cf((size_t)w * h * 3);
        cpu_noise_filter(input.data(), cn.data(), cf.data(), w, h, 0, 0.07f, radius);
        for (auto mode : {ProcessingMode::Separate, ProcessingMode::Fused}) {
            std::vector<uint8_t> gn, gf;
            std::vector<ImageQuality> q;
            StageTimes t;
            std::string err;
            if (!backend.process(input.data(), 1, w, h, 0.07f, k, mode, gn, gf, q, t, err)) {
                std::cerr << "selftest process error: " << err << "\n";
                return 2;
            }
            int nd = 0, fd = 0;
            for (size_t i = 0; i < cn.size(); ++i) {
                if (cn[i] != gn[i]) ++nd;
                if (cf[i] != gf[i]) ++fd;
            }
            std::cout << "selftest kernel=" << k
                      << " mode=" << (mode == ProcessingMode::Separate ? "separate" : "fused")
                      << " noisy_diff=" << nd << " filtered_diff=" << fd
                      << " psnr=" << std::fixed << std::setprecision(3) << q[0].psnr_filtered << "\n";
            if (nd != 0 || fd != 0) ++failures;
        }
    }
    if (failures == 0) {
        std::cout << "SELFTEST PASS\n";
        return 0;
    }
    std::cout << "SELFTEST FAIL count=" << failures << "\n";
    return 1;
}

}  // namespace

int run_benchmark(int argc, char **argv) {
    Options o;
    o.input = default_image_dir();
    o.out_dir = std::filesystem::current_path() / L"bench_out";
    bool help = false;
    if (!parse_args(argc, argv, o, help)) {
        usage();
        return 2;
    }
    if (help) {
        usage();
        return 0;
    }
    if (o.kernel != 3 && o.kernel != 5 && o.kernel != 7) {
        std::cerr << "--kernel must be 3, 5 or 7\n";
        return 2;
    }

    CudaBackend backend;
    std::string err;
    if (o.mode == "info") {
        std::filesystem::path kernel_path = find_kernel_file();
        if (!backend.initialize(kernel_path, err)) {
            std::cerr << "CUDA init failed: " << err << "\n";
            return 3;
        }
        const auto &gpu = backend.gpu_info();
        std::cout << "GPU " << gpu.name << "\n";
        std::cout << "compute_capability " << gpu.compute_major << "." << gpu.compute_minor << "\n";
        std::cout << "SM_count " << gpu.sm_count << "\n";
        std::cout << "max_threads_per_SM " << gpu.max_threads_per_sm << "\n";
        std::cout << "max_shared_memory_per_SM " << gpu.max_shared_memory_per_sm << "\n";
        std::cout << "L2_cache_bytes " << gpu.l2_cache_size << "\n";
        std::cout << "memory_bus_width_bits " << gpu.memory_bus_width << "\n";
        std::cout << "memory_clock_khz " << gpu.memory_clock_khz << "\n";
        std::cout << "total_memory_MiB " << (gpu.total_memory / (1024 * 1024)) << "\n";
        for (int k : {3, 5, 7}) {
            auto m = backend.kernel_resource(false, k);
            auto f = backend.kernel_resource(true, k);
            std::cout << "median" << k << "_kernel regs=" << m.registers_per_thread
                      << " shared=" << m.static_shared_bytes << " local=" << m.local_bytes
                      << " max_threads=" << m.max_threads_per_block
                      << " blocks_per_SM=" << m.active_blocks_per_sm
                      << " occupancy=" << std::fixed << std::setprecision(1)
                      << (m.theoretical_occupancy * 100.0) << "%\n";
            std::cout << "fused" << k << "_kernel regs=" << f.registers_per_thread
                      << " shared=" << f.static_shared_bytes << " local=" << f.local_bytes
                      << " max_threads=" << f.max_threads_per_block
                      << " blocks_per_SM=" << f.active_blocks_per_sm
                      << " occupancy=" << std::fixed << std::setprecision(1)
                      << (f.theoretical_occupancy * 100.0) << "%\n";
        }
        return 0;
    }

    if (o.mode == "selftest") {
        std::filesystem::path kernel_path = find_kernel_file();
        if (!backend.initialize(kernel_path, err)) {
            std::cerr << "CUDA init failed: " << err << "\n";
            return 3;
        }
        return selftest(backend);
    }

    auto files = list_images(o.input);
    if (files.empty()) {
        std::cerr << "no images found in " << wstring_to_utf8(o.input.wstring()) << "\n";
        return 4;
    }
    int count = std::min(o.count, (int)files.size());
    if (!o.quiet) {
        std::cout << "input: " << wstring_to_utf8(o.input.wstring()) << "\n"
                  << "images: " << count << " | noise=" << o.noise << " | kernel=" << o.kernel
                  << " | mode=" << o.mode << "\n";
    }

    int width = 0, height = 0;
    std::vector<uint8_t> input;
    double t0 = now_ms();
    if (!load_images_parallel(files, count, width, height, input, err, {}, nullptr)) {
        std::cerr << "load failed: " << err << "\n";
        return 5;
    }
    double load_ms = now_ms() - t0;
    if (!o.quiet) {
        std::cout << "decoded " << count << " images " << width << "x" << height
                  << " in " << std::fixed << std::setprecision(1) << load_ms << " ms\n";
    }

    std::vector<ImageQuality> quality;
    StageTimes times;
    std::vector<uint8_t> noisy, filtered;

    if (o.mode == "cpu") {
        try {
            noisy.resize((size_t)width * height * 3u * (size_t)count);
            filtered.resize((size_t)width * height * 3u * (size_t)count);
        } catch (const std::bad_alloc &) {
            std::cerr << "not enough host memory\n";
            return 6;
        }
        int nthreads = o.threads > 0 ? o.threads : (int)std::thread::hardware_concurrency();
        nthreads = std::max(1, std::min(nthreads, count));
        std::atomic<int> next(0);
        std::atomic<int> done(0);
        double start = now_ms();
        std::vector<std::thread> workers;
        for (int t = 0; t < nthreads; ++t) {
            workers.emplace_back([&]() {
                while (true) {
                    int i = next.fetch_add(1);
                    if (i >= count) return;
                    cpu_noise_filter(input.data(), noisy.data(), filtered.data(),
                                     width, height, i, o.noise, (o.kernel - 1) / 2);
                    int d = ++done;
                    if (!o.quiet && (d % 50 == 0 || d == count)) {
                        std::cout << "\rCPU " << d << "/" << count << std::flush;
                    }
                }
            });
        }
        for (auto &w : workers) w.join();
        double wall = now_ms() - start;
        if (!o.quiet) std::cout << "\n";
        times.wall_ms = wall;
        times.gpu_total_ms = 0.0;
        quality = quality_cpu(input.data(), noisy.data(), filtered.data(), count, width, height);
        std::cout << "CPU mode: " << std::fixed << std::setprecision(3)
                  << wall << " ms, " << (count / (wall / 1000.0)) << " images/s\n";
        std::cout << "PSNR noisy " << average_psnr(quality, false)
                  << " dB | filtered " << average_psnr(quality, true)
                  << " dB | actual noise " << (average_noise(quality) * 100.0) << "%\n";
        append_csv(o.csv, make_csv_line("CPU " + std::to_string(nthreads) + " threads", o, "cpu",
                                        count, width, height, 1, times, quality));
    } else {
        std::filesystem::path kernel_path = find_kernel_file();
        double init0 = now_ms();
        if (!backend.initialize(kernel_path, err)) {
            std::cerr << "CUDA init failed: " << err << "\n";
            return 3;
        }
        double init_ms = now_ms() - init0;
        const auto &gpu = backend.gpu_info();
        if (!o.quiet) {
            std::cout << "GPU: " << gpu.name << " | cc " << gpu.compute_major << "."
                      << gpu.compute_minor << " | driver " << gpu.driver_version
                      << " | NVRTC " << gpu.nvrtc_version << " | init " << std::fixed
                      << std::setprecision(1) << init_ms << " ms\n";
        }

        ProcessingMode mode = ProcessingMode::Fused;
        if (o.mode == "separate") mode = ProcessingMode::Separate;
        else if (o.mode == "perimage") mode = ProcessingMode::PerImage;
        else if (o.mode != "fused") {
            std::cerr << "invalid --mode (use fused, separate, perimage, cpu, selftest, info)\n";
            return 2;
        }

        int warm_count = o.warmup > 0 ? count : 0;
        if (warm_count > 0) {
            std::vector<uint8_t> wn, wf;
            std::vector<ImageQuality> wq;
            StageTimes wt;
            if (!o.quiet) std::cout << "warm-up: " << warm_count << " images\n";
            if (!backend.process(input.data(), warm_count, width, height, o.noise, o.kernel,
                                 mode, wn, wf, wq, wt, err)) {
                std::cerr << "warmup failed: " << err << "\n";
                return 7;
            }
        }

        for (int r = 0; r < o.repeat; ++r) {
            if (!backend.process(input.data(), count, width, height, o.noise, o.kernel,
                                 mode, noisy, filtered, quality, times, err)) {
                std::cerr << "process failed: " << err << "\n";
                return 8;
            }
            if (!o.quiet) {
                std::cout << "run " << (r + 1) << "/" << o.repeat << "\n";
                print_stage_times(times);
                std::cout << "  PSNR noisy " << average_psnr(quality, false)
                          << " dB | filtered " << average_psnr(quality, true)
                          << " dB | actual noise " << (average_noise(quality) * 100.0) << "%\n";
            }
            append_csv(o.csv, make_csv_line(gpu.name, o, o.mode, count, width, height, r + 1,
                                            times, quality));
        }
        std::cout << "RESULT mode=" << o.mode << " count=" << count
                  << " wall_ms=" << std::fixed << std::setprecision(3) << times.wall_ms
                  << " gpu_total_ms=" << times.gpu_total_ms
                  << " kernel_ms=" << times.kernel_ms
                  << " fps=" << (count / (times.wall_ms / 1000.0))
                  << " psnr=" << average_psnr(quality, true) << "\n";
    }

    if (o.save_noisy || o.save_denoised) {
        if (o.out_dir.empty()) {
            std::cerr << "no --out dir\n";
            return 9;
        }
        std::vector<std::filesystem::path> noisy_paths, filtered_paths;
        for (int i = 0; i < count; ++i) {
            auto name = files[(size_t)i].filename();
            noisy_paths.push_back(o.out_dir / L"noisy" / name);
            filtered_paths.push_back(o.out_dir / L"denoised" / name);
        }
        double s0 = now_ms();
        if (o.save_noisy &&
            !save_images_parallel(noisy_paths, noisy, count, width, height, o.quality, err, {}, nullptr)) {
            std::cerr << "save noisy failed: " << err << "\n";
            return 10;
        }
        if (o.save_denoised &&
            !save_images_parallel(filtered_paths, filtered, count, width, height, o.quality, err, {}, nullptr)) {
            std::cerr << "save denoised failed: " << err << "\n";
            return 11;
        }
        std::cout << "saved images in " << std::fixed << std::setprecision(1)
                  << (now_ms() - s0) << " ms -> " << wstring_to_utf8(o.out_dir.wstring()) << "\n";
    }
    return 0;
}

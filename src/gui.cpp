#include "gui.h"
#include "common.h"
#include "cuda_backend.h"
#include "image_io.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

constexpr int WM_APP_INIT_CUDA = WM_APP + 1;
constexpr int WM_APP_BATCH_DONE = WM_APP + 2;
constexpr int TIMER_BATCH = 1;
constexpr int TIMER_CAPTURE = 2;

enum ControlId {
    ID_TAB_SINGLE = 100,
    ID_TAB_BATCH,
    ID_OPEN,
    ID_PREV,
    ID_NEXT,
    ID_NOISE_SLIDER,
    ID_NOISE_VALUE,
    ID_NOISE_LABEL,
    ID_KERNEL,
    ID_KERNEL_LABEL,
    ID_ADD_NOISE,
    ID_FILTER,
    ID_ONE_CLICK,
    ID_SAVE_NOISY,
    ID_SAVE_DENOISED,
    ID_FILE_LABEL,
    ID_METRICS,
    ID_LEFT_TITLE,
    ID_RIGHT_TITLE,
    ID_GPU_INFO,
    ID_BATCH_FOLDER_LABEL,
    ID_BATCH_FOLDER,
    ID_BROWSE,
    ID_BATCH_COUNT,
    ID_BATCH_NOISE_LABEL,
    ID_BATCH_NOISE_VALUE,
    ID_BATCH_NOISE,
    ID_BATCH_KERNEL_LABEL,
    ID_BATCH_KERNEL,
    ID_SAVE_DENOISED_CHECK,
    ID_SAVE_NOISY_CHECK,
    ID_BATCH_START,
    ID_BATCH_CANCEL,
    ID_OPEN_OUTPUT,
    ID_PROGRESS,
    ID_BATCH_STATUS,
    ID_BATCH_HINT,
    ID_SINGLE_HINT
};

const COLORREF C_BG = RGB(244, 247, 251);
const COLORREF C_NAVY = RGB(20, 33, 61);
const COLORREF C_NAVY2 = RGB(29, 45, 78);
const COLORREF C_ACCENT = RGB(47, 111, 237);
const COLORREF C_ACCENT_DARK = RGB(35, 86, 190);
const COLORREF C_TEAL = RGB(0, 168, 150);
const COLORREF C_ORANGE = RGB(244, 162, 97);
const COLORREF C_CARD = RGB(255, 255, 255);
const COLORREF C_BORDER = RGB(224, 231, 240);
const COLORREF C_TEXT = RGB(30, 41, 59);
const COLORREF C_MUTED = RGB(100, 116, 139);
const COLORREF C_SOFT = RGB(248, 250, 253);
const COLORREF C_GREEN = RGB(22, 163, 74);

struct BatchSummary {
    int count = 0;
    int width = 0;
    int height = 0;
    double noise = 0.0;
    int kernel = 3;
    double decode_ms = 0.0;
    double h2d_ms = 0.0;
    double noise_ms = 0.0;
    double median_ms = 0.0;
    double kernel_ms = 0.0;
    double stats_ms = 0.0;
    double d2h_ms = 0.0;
    double gpu_total_ms = 0.0;
    double wall_ms = 0.0;
    double save_ms = 0.0;
    double total_ms = 0.0;
    double fps = 0.0;
    double psnr_noisy = 0.0;
    double psnr_filtered = 0.0;
    double noise_actual = 0.0;
    bool saved_denoised = false;
    bool saved_noisy = false;
    std::wstring error;
};

struct App {
    HWND hwnd = nullptr;
    HINSTANCE instance = nullptr;
    int dpi = 96;
    double scale = 1.0;
    int tab = 0;
    HFONT font_ui = nullptr;
    HFONT font_small = nullptr;
    HFONT font_title = nullptr;
    HFONT font_section = nullptr;
    HFONT font_mono = nullptr;
    HFONT font_button = nullptr;
    HBRUSH brush_bg = nullptr;
    HBRUSH brush_card = nullptr;
    HBRUSH brush_navy = nullptr;
    HBRUSH brush_soft = nullptr;
    HBRUSH brush_white = nullptr;

    HWND h_tab_single = nullptr, h_tab_batch = nullptr;
    HWND h_open = nullptr, h_prev = nullptr, h_next = nullptr;
    HWND h_noise_slider = nullptr, h_noise_value = nullptr;
    HWND h_noise_label = nullptr, h_kernel_label = nullptr;
    HWND h_kernel = nullptr;
    HWND h_add_noise = nullptr, h_filter = nullptr, h_one_click = nullptr;
    HWND h_save_noisy = nullptr, h_save_denoised = nullptr;
    HWND h_file_label = nullptr, h_metrics = nullptr;
    HWND h_left_title = nullptr, h_right_title = nullptr, h_gpu_info = nullptr;
    HWND h_single_hint = nullptr;

    HWND h_batch_folder_label = nullptr, h_batch_folder = nullptr, h_browse = nullptr;
    HWND h_batch_count = nullptr;
    HWND h_batch_noise_label = nullptr, h_batch_noise_value = nullptr, h_batch_noise = nullptr;
    HWND h_batch_kernel_label = nullptr, h_batch_kernel = nullptr;
    HWND h_save_denoised_check = nullptr, h_save_noisy_check = nullptr;
    HWND h_batch_start = nullptr, h_batch_cancel = nullptr, h_open_output = nullptr;
    HWND h_progress = nullptr, h_batch_status = nullptr, h_batch_hint = nullptr;

    CudaBackend backend;
    bool cuda_ready = false;
    std::wstring gpu_text;
    std::filesystem::path kernel_path;

    std::filesystem::path folder;
    std::vector<std::filesystem::path> files;
    int current = 0;
    ImageRGB original;
    ImageRGB noisy;
    ImageRGB denoised;
    bool has_noisy = false;
    bool has_denoised = false;
    double psnr_noisy = 0.0;
    double psnr_denoised = 0.0;
    double noise_actual = 0.0;
    double last_noise_ms = 0.0;
    double last_filter_ms = 0.0;

    std::filesystem::path batch_folder;
    int batch_limit = 1000;
    std::atomic<bool> batch_running{false};
    std::atomic<bool> batch_cancel{false};
    std::atomic<bool> batch_has_preview{false};
    std::atomic<int> batch_progress{0};
    std::mutex batch_mutex;
    std::wstring batch_stage;
    BatchSummary batch_summary;
    ImageRGB batch_original;
    ImageRGB batch_noisy;
    ImageRGB batch_filtered;
    std::thread batch_thread;

    std::wstring capture_path;
    std::wstring status_text;
    bool auto_run = false;
    bool capture_when_done = false;
};

App g;

int S(int v) {
    return (int)std::lround((double)v * g.scale);
}

std::wstring fmt(double v, int digits = 2) {
    std::wostringstream os;
    os << std::fixed << std::setprecision(digits) << v;
    return os.str();
}

std::wstring fmt_sec(double ms) {
    if (ms >= 1000.0) return fmt(ms / 1000.0, 3) + L" s";
    return fmt(ms, 1) + L" ms";
}

std::filesystem::path project_root() {
    auto exe = executable_dir();
    std::error_code ec;
    if (std::filesystem::exists(exe / L"src" / L"kernels.cu", ec)) return exe;
    return exe.parent_path();
}

std::filesystem::path find_kernel() {
    std::error_code ec;
    std::vector<std::filesystem::path> candidates = {
        executable_dir() / L"kernels.cu",
        executable_dir() / L"src" / L"kernels.cu",
        executable_dir().parent_path() / L"src" / L"kernels.cu",
        std::filesystem::current_path() / L"src" / L"kernels.cu",
        std::filesystem::current_path() / L"cuda_median_project" / L"src" / L"kernels.cu",
    };
    for (auto &p : candidates) {
        if (std::filesystem::exists(p, ec)) return p;
    }
    return candidates[1];
}

double psnr_between(const ImageRGB &a, const ImageRGB &b) {
    if (a.width != b.width || a.height != b.height || a.empty() || b.empty()) return 0.0;
    double sum = 0.0;
    size_t n = a.pixels.size();
    for (size_t i = 0; i < n; ++i) {
        double d = (double)a.pixels[i] - (double)b.pixels[i];
        sum += d * d;
    }
    double mse = sum / (double)n;
    if (mse <= 1e-12) return 99.0;
    return 10.0 * std::log10(65025.0 / mse);
}

double impulse_ratio(const ImageRGB &clean, const ImageRGB &noisy) {
    if (clean.width != noisy.width || clean.height != noisy.height || clean.empty()) return 0.0;
    size_t pixels = (size_t)clean.width * (size_t)clean.height;
    size_t hits = 0;
    for (size_t p = 0; p < pixels; ++p) {
        size_t j = p * 3;
        unsigned char v = noisy.pixels[j];
        if ((v == 0 || v == 255) && noisy.pixels[j + 1] == v && noisy.pixels[j + 2] == v &&
            (clean.pixels[j] != v || clean.pixels[j + 1] != v || clean.pixels[j + 2] != v)) {
            ++hits;
        }
    }
    return pixels ? (double)hits / (double)pixels : 0.0;
}

void set_stage(const std::wstring &text) {
    std::lock_guard<std::mutex> lock(g.batch_mutex);
    g.batch_stage = text;
}

std::wstring get_stage() {
    std::lock_guard<std::mutex> lock(g.batch_mutex);
    return g.batch_stage;
}

void set_status(const std::wstring &text) {
    g.status_text = text;
    if (g.h_batch_status) SetWindowTextW(g.h_batch_status, text.c_str());
}

void set_metrics_text(const std::wstring &text) {
    SetWindowTextW(g.h_metrics, text.c_str());
}

HFONT make_font(int px, bool bold, const wchar_t *name) {
    return CreateFontW(-S(px), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, name);
}

void create_fonts() {
    g.font_ui = make_font(16, false, L"Microsoft YaHei UI");
    g.font_small = make_font(13, false, L"Microsoft YaHei UI");
    g.font_title = make_font(28, true, L"Microsoft YaHei UI");
    g.font_section = make_font(19, true, L"Microsoft YaHei UI");
    g.font_mono = make_font(14, false, L"Consolas");
    g.font_button = make_font(15, true, L"Microsoft YaHei UI");
}

void delete_fonts() {
    for (HFONT *f : {&g.font_ui, &g.font_small, &g.font_title, &g.font_section,
                     &g.font_mono, &g.font_button}) {
        if (*f) { DeleteObject(*f); *f = nullptr; }
    }
}

void create_brushes() {
    g.brush_bg = CreateSolidBrush(C_BG);
    g.brush_card = CreateSolidBrush(C_CARD);
    g.brush_navy = CreateSolidBrush(C_NAVY);
    g.brush_soft = CreateSolidBrush(C_SOFT);
    g.brush_white = CreateSolidBrush(RGB(255, 255, 255));
}

void delete_brushes() {
    for (HBRUSH *b : {&g.brush_bg, &g.brush_card, &g.brush_navy, &g.brush_soft, &g.brush_white}) {
        if (*b) { DeleteObject(*b); *b = nullptr; }
    }
}

void set_font(HWND h, HFONT f) {
    SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE);
}

HWND make_button(HWND parent, int id, const wchar_t *text, bool owner_draw = true) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | (owner_draw ? BS_OWNERDRAW : BS_PUSHBUTTON);
    HWND h = CreateWindowExW(0, L"BUTTON", text, style, 0, 0, 10, 10, parent,
                            (HMENU)(INT_PTR)id, g.instance, nullptr);
    set_font(h, g.font_button);
    return h;
}

HWND make_static(HWND parent, int id, const wchar_t *text, DWORD extra = SS_LEFT) {
    HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | extra,
                            0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, g.instance, nullptr);
    set_font(h, g.font_ui);
    return h;
}

void layout_controls() {
    int L = S(24), T = S(132), LW = S(344), LH = S(690);
    int RX = S(384), RW = S(972), RH = S(690);

    MoveWindow(g.h_tab_single, S(24), S(82), S(154), S(38), TRUE);
    MoveWindow(g.h_tab_batch, S(190), S(82), S(154), S(38), TRUE);

    MoveWindow(g.h_left_title, L + S(22), T + S(20), LW - S(44), S(28), TRUE);
    MoveWindow(g.h_right_title, RX + S(24), T + S(20), RW - S(48), S(28), TRUE);
    MoveWindow(g.h_gpu_info, S(760), S(22), S(590), S(30), TRUE);

    int x = L + S(20), y = T + S(56), w = S(304);
    MoveWindow(g.h_open, x, y, S(134), S(36), TRUE);
    MoveWindow(g.h_prev, x + S(144), y, S(76), S(36), TRUE);
    MoveWindow(g.h_next, x + S(228), y, S(76), S(36), TRUE);
    MoveWindow(g.h_file_label, x, y + S(44), w, S(24), TRUE);

    MoveWindow(g.h_noise_value, x + S(226), y + S(80), S(78), S(22), TRUE);
    MoveWindow(g.h_noise_slider, x, y + S(104), w, S(34), TRUE);
    MoveWindow(g.h_noise_label, x, y + S(78), S(150), S(22), TRUE);
    MoveWindow(g.h_kernel, x + S(150), y + S(148), S(154), S(220), TRUE);
    MoveWindow(g.h_kernel_label, x, y + S(150), S(140), S(22), TRUE);

    MoveWindow(g.h_add_noise, x, y + S(204), w, S(42), TRUE);
    MoveWindow(g.h_filter, x, y + S(254), w, S(42), TRUE);
    MoveWindow(g.h_one_click, x, y + S(306), w, S(46), TRUE);
    MoveWindow(g.h_save_noisy, x, y + S(366), S(146), S(36), TRUE);
    MoveWindow(g.h_save_denoised, x + S(156), y + S(366), S(148), S(36), TRUE);
    MoveWindow(g.h_single_hint, x, y + S(420), w, S(50), TRUE);

    // Batch controls occupy the same left card.
    MoveWindow(g.h_batch_folder_label, x, y, w, S(22), TRUE);
    MoveWindow(g.h_batch_folder, x, y + S(24), S(226), S(30), TRUE);
    MoveWindow(g.h_browse, x + S(234), y + S(24), S(70), S(30), TRUE);
    MoveWindow(g.h_batch_count, x, y + S(62), w, S(22), TRUE);
    MoveWindow(g.h_batch_noise_value, x + S(226), y + S(94), S(78), S(22), TRUE);
    MoveWindow(g.h_batch_noise, x, y + S(118), w, S(34), TRUE);
    MoveWindow(g.h_batch_noise_label, x, y + S(92), S(150), S(22), TRUE);
    MoveWindow(g.h_batch_kernel, x + S(150), y + S(162), S(154), S(220), TRUE);
    MoveWindow(g.h_batch_kernel_label, x, y + S(164), S(140), S(22), TRUE);
    MoveWindow(g.h_save_denoised_check, x, y + S(210), w, S(26), TRUE);
    MoveWindow(g.h_save_noisy_check, x, y + S(240), w, S(26), TRUE);
    MoveWindow(g.h_batch_start, x, y + S(284), w, S(46), TRUE);
    MoveWindow(g.h_batch_cancel, x, y + S(338), w, S(38), TRUE);
    MoveWindow(g.h_open_output, x, y + S(384), w, S(36), TRUE);
    MoveWindow(g.h_progress, x, y + S(440), w, S(18), TRUE);
    MoveWindow(g.h_batch_status, x, y + S(466), w, S(52), TRUE);
    MoveWindow(g.h_batch_hint, x, y + S(530), w, S(54), TRUE);

    MoveWindow(g.h_metrics, RX + S(24), T + S(414), RW - S(48), S(250), TRUE);
}

std::wstring single_metrics() {
    if (g.original.empty()) {
        return L"打开一张图片开始。\r\n\r\n"
               L"支持 JPG / PNG / BMP；单张处理可调节噪声比例和 3×3、5×5、7×7 窗口。\r\n"
               L"步骤①只生成噪声图，步骤②在噪声图上做中值滤波。";
    }
    std::wostringstream os;
    os << L"文件    " << (g.files.empty() ? L"" : g.files[(size_t)g.current].filename().wstring()) << L"\r\n";
    os << L"尺寸    " << g.original.width << L" × " << g.original.height << L"\r\n";
    int noise_pos = (int)SendMessageW(g.h_noise_slider, TBM_GETPOS, 0, 0);
    int ki = (int)SendMessageW(g.h_kernel, CB_GETCURSEL, 0, 0);
    int k = 3 + 2 * (ki < 0 ? 0 : ki);
    os << L"参数    " << fmt(noise_pos / 10.0, 1) << L"% · " << k << L"×" << k << L"\r\n";
    if (g.has_noisy) {
        os << L"实测噪声 " << fmt(g.noise_actual * 100.0, 2) << L"%\r\n";
        os << L"加噪 PSNR " << fmt(g.psnr_noisy, 2) << L" dB    " << fmt_sec(g.last_noise_ms) << L"\r\n";
    } else {
        os << L"加噪    " << L"尚未执行\r\n";
    }
    if (g.has_denoised) {
        os << L"去噪 PSNR " << fmt(g.psnr_denoised, 2) << L" dB    " << fmt_sec(g.last_filter_ms) << L"\r\n";
        os << L"质量提升 " << fmt(g.psnr_denoised - g.psnr_noisy, 2) << L" dB";
    } else {
        os << L"去噪    " << L"尚未执行";
    }
    return os.str();
}

std::wstring batch_metrics() {
    std::lock_guard<std::mutex> lock(g.batch_mutex);
    const auto &s = g.batch_summary;
    if (s.count == 0) {
        return L"选择包含 1000 张 640×640 图片的文件夹，点击“开始批量处理”。\r\n\r\n"
               L"批量模式一次把全部图片送入 GPU，只统计 GPU 处理时间，也给出解码、传输、统计和保存耗时。";
    }
    std::wostringstream os;
    os << L"图片    " << s.count << L" 张 · " << s.width << L" × " << s.height
       << L" · 噪声 " << fmt(s.noise * 100.0, 1) << L"% · " << s.kernel << L"×" << s.kernel << L"\r\n";
    os << L"总耗时  " << fmt_sec(s.total_ms) << L"    吞吐 " << fmt(s.fps, 1) << L" 张/s\r\n";
    os << L"解码    " << fmt_sec(s.decode_ms) << L"    H2D " << fmt_sec(s.h2d_ms) << L"\r\n";
    if (s.noise_ms > 0.0 || s.median_ms > 0.0) {
        os << L"噪声    " << fmt_sec(s.noise_ms) << L"    中值 " << fmt_sec(s.median_ms) << L"\r\n";
        os << L"内核    " << fmt_sec(s.kernel_ms) << L"    统计 " << fmt_sec(s.stats_ms) << L"\r\n";
    } else {
        os << L"融合内核 " << fmt_sec(s.kernel_ms) << L"    统计 " << fmt_sec(s.stats_ms) << L"\r\n";
    }
    os << L"D2H     " << fmt_sec(s.d2h_ms) << L"    保存 " << fmt_sec(s.save_ms) << L"\r\n";
    os << L"PSNR    " << fmt(s.psnr_noisy, 2) << L" dB → " << fmt(s.psnr_filtered, 2) << L" dB"
       << L"    实测噪声 " << fmt(s.noise_actual * 100.0, 2) << L"%";
    if (!s.error.empty()) os << L"\r\n状态    " << s.error;
    return os.str();
}

void update_metrics() {
    if (g.tab == 0) set_metrics_text(single_metrics());
    else set_metrics_text(batch_metrics());
}

void show_tab(int tab) {
    g.tab = tab;
    struct Set { HWND h; bool show; };
    Set single[] = {
        {g.h_open, true}, {g.h_prev, true}, {g.h_next, true}, {g.h_file_label, true},
        {g.h_noise_slider, true}, {g.h_noise_value, true}, {g.h_noise_label, true},
        {g.h_kernel, true}, {g.h_kernel_label, true},
        {g.h_add_noise, true}, {g.h_filter, true}, {g.h_one_click, true},
        {g.h_save_noisy, true}, {g.h_save_denoised, true}, {g.h_single_hint, true},
    };
    Set batch[] = {
        {g.h_batch_folder_label, true}, {g.h_batch_folder, true}, {g.h_browse, true},
        {g.h_batch_count, true}, {g.h_batch_noise_label, true}, {g.h_batch_noise_value, true},
        {g.h_batch_noise, true}, {g.h_batch_kernel_label, true}, {g.h_batch_kernel, true},
        {g.h_save_denoised_check, true}, {g.h_save_noisy_check, true}, {g.h_batch_start, true},
        {g.h_batch_cancel, true}, {g.h_open_output, true}, {g.h_progress, true},
        {g.h_batch_status, true}, {g.h_batch_hint, true},
    };
    for (auto &s : single) ShowWindow(s.h, (tab == 0 && s.show) ? SW_SHOW : SW_HIDE);
    for (auto &s : batch) ShowWindow(s.h, (tab == 1 && s.show) ? SW_SHOW : SW_HIDE);
    SetWindowTextW(g.h_left_title, tab == 0 ? L"参数与操作" : L"批量任务");
    SetWindowTextW(g.h_right_title, tab == 0 ? L"处理结果预览" : L"批量样例与统计");
    InvalidateRect(g.h_tab_single, nullptr, TRUE);
    InvalidateRect(g.h_tab_batch, nullptr, TRUE);
    update_metrics();
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void refresh_file_list(const std::filesystem::path &folder, const std::filesystem::path &preferred) {
    g.folder = folder;
    g.files = list_images(folder);
    g.current = 0;
    for (size_t i = 0; i < g.files.size(); ++i) {
        if (_wcsicmp(g.files[i].filename().c_str(), preferred.filename().c_str()) == 0) {
            g.current = (int)i;
            break;
        }
    }
}

void load_current() {
    if (g.files.empty()) {
        MessageBoxW(g.hwnd, L"文件夹中没有图片", L"提示", MB_ICONINFORMATION);
        return;
    }
    if (g.current < 0) g.current = (int)g.files.size() - 1;
    if (g.current >= (int)g.files.size()) g.current = 0;
    ImageRGB img;
    std::string err;
    if (!load_image(g.files[(size_t)g.current], img, err)) {
        MessageBoxW(g.hwnd, utf8_to_wstring(err).c_str(), L"读取失败", MB_ICONERROR);
        return;
    }
    g.original = std::move(img);
    g.noisy = ImageRGB{};
    g.denoised = ImageRGB{};
    g.has_noisy = false;
    g.has_denoised = false;
    g.psnr_noisy = g.psnr_denoised = 0.0;
    g.noise_actual = 0.0;
    g.last_noise_ms = g.last_filter_ms = 0.0;
    std::wstring name = g.files[(size_t)g.current].filename().wstring();
    SetWindowTextW(g.h_file_label, name.c_str());
    SetWindowTextW(g.h_batch_folder, g.batch_folder.wstring().c_str());
    update_metrics();
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void open_image_dialog() {
    wchar_t buf[2048] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"图像文件\0*.jpg;*.jpeg;*.png;*.bmp\0所有文件\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 2048;
    ofn.lpstrInitialDir = g.folder.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    std::filesystem::path p(buf);
    refresh_file_list(p.parent_path(), p);
    load_current();
}

void browse_batch_folder() {
    BROWSEINFOW bi{};
    bi.hwndOwner = g.hwnd;
    bi.lpszTitle = L"选择包含图片的文件夹";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t path[MAX_PATH] = {};
    if (SHGetPathFromIDListW(pidl, path)) {
        g.batch_folder = path;
        SetWindowTextW(g.h_batch_folder, path);
        auto files = list_images(g.batch_folder);
        std::wstring text = L"共 " + std::to_wstring(files.size()) + L" 张图片";
        if (files.size() > 1000) text += L"，本次处理前 1000 张";
        SetWindowTextW(g.h_batch_count, text.c_str());
    }
    CoTaskMemFree(pidl);
}

void process_error(const std::string &err) {
    MessageBoxW(g.hwnd, utf8_to_wstring(err).c_str(), L"CUDA 处理失败", MB_ICONERROR);
}

void run_add_noise() {
    if (!g.cuda_ready) { MessageBoxW(g.hwnd, L"CUDA 尚未初始化完成", L"提示", MB_ICONWARNING); return; }
    if (g.original.empty()) { MessageBoxW(g.hwnd, L"请先打开图片", L"提示", MB_ICONWARNING); return; }
    float p = (float)SendMessageW(g.h_noise_slider, TBM_GETPOS, 0, 0) / 1000.0f;
    int kernel = 3 + 2 * (int)SendMessageW(g.h_kernel, CB_GETCURSEL, 0, 0);
    std::vector<uint8_t> noisy, filtered;
    std::vector<ImageQuality> quality;
    StageTimes times;
    std::string err;
    if (!g.backend.process(g.original.pixels.data(), 1, g.original.width, g.original.height,
                           p, kernel, ProcessingMode::Separate, noisy, filtered, quality, times, err)) {
        process_error(err);
        return;
    }
    g.noisy.width = g.original.width;
    g.noisy.height = g.original.height;
    g.noisy.pixels = std::move(noisy);
    g.denoised = ImageRGB{};
    g.has_noisy = true;
    g.has_denoised = false;
    g.noise_actual = impulse_ratio(g.original, g.noisy);
    g.psnr_noisy = psnr_between(g.original, g.noisy);
    g.last_noise_ms = times.noise_ms;
    g.last_filter_ms = 0.0;
    set_status(L"加噪完成");
    update_metrics();
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void run_filter() {
    if (!g.cuda_ready) { MessageBoxW(g.hwnd, L"CUDA 尚未初始化完成", L"提示", MB_ICONWARNING); return; }
    if (!g.has_noisy) { MessageBoxW(g.hwnd, L"请先执行第①步：添加椒盐噪声", L"提示", MB_ICONWARNING); return; }
    int kernel = 3 + 2 * (int)SendMessageW(g.h_kernel, CB_GETCURSEL, 0, 0);
    std::vector<uint8_t> noisy2, filtered;
    std::vector<ImageQuality> quality;
    StageTimes times;
    std::string err;
    if (!g.backend.process(g.noisy.pixels.data(), 1, g.noisy.width, g.noisy.height,
                           0.0f, kernel, ProcessingMode::Separate, noisy2, filtered, quality, times, err)) {
        process_error(err);
        return;
    }
    g.denoised.width = g.noisy.width;
    g.denoised.height = g.noisy.height;
    g.denoised.pixels = std::move(filtered);
    g.has_denoised = true;
    g.psnr_denoised = psnr_between(g.original, g.denoised);
    g.last_filter_ms = times.median_ms;
    set_status(L"中值滤波完成");
    update_metrics();
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void run_one_click() {
    if (!g.cuda_ready) { MessageBoxW(g.hwnd, L"CUDA 尚未初始化完成", L"提示", MB_ICONWARNING); return; }
    if (g.original.empty()) { MessageBoxW(g.hwnd, L"请先打开图片", L"提示", MB_ICONWARNING); return; }
    float p = (float)SendMessageW(g.h_noise_slider, TBM_GETPOS, 0, 0) / 1000.0f;
    int kernel = 3 + 2 * (int)SendMessageW(g.h_kernel, CB_GETCURSEL, 0, 0);
    std::vector<uint8_t> noisy, filtered;
    std::vector<ImageQuality> quality;
    StageTimes times;
    std::string err;
    if (!g.backend.process(g.original.pixels.data(), 1, g.original.width, g.original.height,
                           p, kernel, ProcessingMode::Fused, noisy, filtered, quality, times, err)) {
        process_error(err);
        return;
    }
    g.noisy.width = g.original.width;
    g.noisy.height = g.original.height;
    g.noisy.pixels = std::move(noisy);
    g.denoised.width = g.original.width;
    g.denoised.height = g.original.height;
    g.denoised.pixels = std::move(filtered);
    g.has_noisy = g.has_denoised = true;
    g.noise_actual = impulse_ratio(g.original, g.noisy);
    g.psnr_noisy = psnr_between(g.original, g.noisy);
    g.psnr_denoised = psnr_between(g.original, g.denoised);
    g.last_noise_ms = times.kernel_ms;
    g.last_filter_ms = times.kernel_ms;
    set_status(L"一键处理完成");
    update_metrics();
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void save_current(bool denoised) {
    const ImageRGB &img = denoised ? g.denoised : g.noisy;
    if ((denoised && !g.has_denoised) || (!denoised && !g.has_noisy)) {
        MessageBoxW(g.hwnd, L"没有可保存的结果", L"提示", MB_ICONWARNING);
        return;
    }
    std::wstring base = g.files.empty() ? L"image" : g.files[(size_t)g.current].stem().wstring();
    std::wstring suggested = base + (denoised ? L"_denoised.jpg" : L"_noisy.jpg");
    wchar_t buf[2048] = {};
    wcsncpy(buf, suggested.c_str(), 2047);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"JPEG 图片\0*.jpg\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 2048;
    ofn.lpstrDefExt = L"jpg";
    ofn.lpstrInitialDir = g.folder.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    std::string err;
    if (!save_jpeg(buf, img.pixels.data(), img.width, img.height, 95, err)) {
        MessageBoxW(g.hwnd, utf8_to_wstring(err).c_str(), L"保存失败", MB_ICONERROR);
        return;
    }
    set_status(L"图片已保存");
}

void set_batch_ui(bool running) {
    for (HWND h : {g.h_batch_start, g.h_browse, g.h_batch_folder, g.h_batch_noise,
                   g.h_batch_kernel, g.h_save_denoised_check, g.h_save_noisy_check}) {
        EnableWindow(h, !running);
    }
    EnableWindow(g.h_batch_cancel, running);
    InvalidateRect(g.h_batch_start, nullptr, TRUE);
    InvalidateRect(g.h_batch_cancel, nullptr, TRUE);
}

void batch_worker(std::vector<std::filesystem::path> files, int count,
                  std::filesystem::path folder, float p, int kernel,
                  bool save_denoised, bool save_noisy) {
    BatchSummary s;
    s.count = count;
    s.noise = p;
    s.kernel = kernel;
    double total0 = now_ms();
    std::string err;
    std::vector<uint8_t> input;
    int w = 0, h = 0;
    set_stage(L"正在解码图片...");
    g.batch_progress = 3;
    double d0 = now_ms();
    bool ok = load_images_parallel(files, count, w, h, input, err, [&](int n) {
        g.batch_progress = 3 + 17 * n / std::max(1, count);
    }, &g.batch_cancel);
    s.decode_ms = now_ms() - d0;
    s.width = w;
    s.height = h;
    if (!ok || g.batch_cancel.load()) {
        s.error = g.batch_cancel.load() ? L"已取消" : utf8_to_wstring(err);
        s.total_ms = now_ms() - total0;
        {
            std::lock_guard<std::mutex> lock(g.batch_mutex);
            g.batch_summary = s;
        }
        g.batch_running = false;
        PostMessageW(g.hwnd, WM_APP_BATCH_DONE, 0, 0);
        return;
    }

    set_stage(L"GPU 批量加噪与中值滤波...");
    g.batch_progress = 20;
    std::vector<uint8_t> noisy, filtered;
    std::vector<ImageQuality> quality;
    StageTimes times;
    ok = g.backend.process(input.data(), count, w, h, p, kernel, ProcessingMode::Fused,
                           noisy, filtered, quality, times, err, [&](int v) {
                               g.batch_progress = 20 + 60 * v / 100;
                           });
    s.h2d_ms = times.h2d_ms;
    s.noise_ms = times.noise_ms;
    s.median_ms = times.median_ms;
    s.kernel_ms = times.kernel_ms;
    s.stats_ms = times.stats_ms;
    s.d2h_ms = times.d2h_ms;
    s.gpu_total_ms = times.gpu_total_ms;
    s.wall_ms = times.wall_ms;
    if (!ok || g.batch_cancel.load()) {
        s.error = g.batch_cancel.load() ? L"已取消" : utf8_to_wstring(err);
        s.total_ms = now_ms() - total0;
        {
            std::lock_guard<std::mutex> lock(g.batch_mutex);
            g.batch_summary = s;
        }
        g.batch_running = false;
        PostMessageW(g.hwnd, WM_APP_BATCH_DONE, 0, 0);
        return;
    }

    double sum_n = 0.0, sum_f = 0.0, sum_r = 0.0;
    for (const auto &q : quality) {
        sum_n += q.psnr_noisy;
        sum_f += q.psnr_filtered;
        sum_r += q.noise_ratio;
    }
    if (!quality.empty()) {
        s.psnr_noisy = sum_n / (double)quality.size();
        s.psnr_filtered = sum_f / (double)quality.size();
        s.noise_actual = sum_r / (double)quality.size();
    }

    size_t image_bytes = (size_t)w * (size_t)h * 3u;
    g.batch_original.width = w;
    g.batch_original.height = h;
    g.batch_original.pixels.assign(input.begin(), input.begin() + (std::ptrdiff_t)image_bytes);
    g.batch_noisy.width = w;
    g.batch_noisy.height = h;
    g.batch_noisy.pixels.assign(noisy.begin(), noisy.begin() + (std::ptrdiff_t)image_bytes);
    g.batch_filtered.width = w;
    g.batch_filtered.height = h;
    g.batch_filtered.pixels.assign(filtered.begin(), filtered.begin() + (std::ptrdiff_t)image_bytes);
    g.batch_has_preview = true;

    if ((save_denoised || save_noisy) && !g.batch_cancel.load()) {
        set_stage(L"正在保存结果图片...");
        std::filesystem::path out = project_root() / L"results" / L"batch_output";
        std::vector<std::filesystem::path> np, fp;
        for (int i = 0; i < count; ++i) {
            auto name = files[(size_t)i].filename();
            np.push_back(out / L"noisy" / name);
            fp.push_back(out / L"denoised" / name);
        }
        double s0 = now_ms();
        if (save_noisy) {
            ok = save_images_parallel(np, noisy, count, w, h, 92, err, [&](int n) {
                g.batch_progress = 80 + 10 * n / std::max(1, count);
            }, &g.batch_cancel);
            s.saved_noisy = ok;
        }
        if (ok && save_denoised) {
            ok = save_images_parallel(fp, filtered, count, w, h, 92, err, [&](int n) {
                g.batch_progress = 90 + 10 * n / std::max(1, count);
            }, &g.batch_cancel);
            s.saved_denoised = ok;
        }
        s.save_ms = now_ms() - s0;
        if (!ok) s.error = utf8_to_wstring(err);
    }

    s.total_ms = now_ms() - total0;
    s.fps = s.total_ms > 0 ? (double)count / (s.total_ms / 1000.0) : 0.0;
    {
        std::lock_guard<std::mutex> lock(g.batch_mutex);
        g.batch_summary = s;
    }
    g.batch_progress = 100;
    g.batch_running = false;
    PostMessageW(g.hwnd, WM_APP_BATCH_DONE, 0, 0);
}

void start_batch() {
    if (g.batch_running.load()) return;
    if (!g.cuda_ready) { MessageBoxW(g.hwnd, L"CUDA 尚未初始化完成", L"提示", MB_ICONWARNING); return; }
    wchar_t buf[2048] = {};
    GetWindowTextW(g.h_batch_folder, buf, 2048);
    std::filesystem::path folder = buf;
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        MessageBoxW(g.hwnd, L"图片文件夹不存在", L"提示", MB_ICONWARNING);
        return;
    }
    auto files = list_images(folder);
    if (files.empty()) {
        MessageBoxW(g.hwnd, L"文件夹中没有图片", L"提示", MB_ICONWARNING);
        return;
    }
    int count = (int)std::min<size_t>(files.size(), (size_t)std::max(1, g.batch_limit));
    float p = (float)SendMessageW(g.h_batch_noise, TBM_GETPOS, 0, 0) / 1000.0f;
    int kernel = 3 + 2 * (int)SendMessageW(g.h_batch_kernel, CB_GETCURSEL, 0, 0);
    bool save_denoised = SendMessageW(g.h_save_denoised_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool save_noisy = SendMessageW(g.h_save_noisy_check, BM_GETCHECK, 0, 0) == BST_CHECKED;

    g.batch_folder = folder;
    g.batch_cancel = false;
    g.batch_progress = 0;
    g.batch_running = true;
    g.batch_has_preview = false;
    {
        std::lock_guard<std::mutex> lock(g.batch_mutex);
        g.batch_summary = BatchSummary{};
        g.batch_stage = L"准备批量任务...";
    }
    set_batch_ui(true);
    set_status(L"准备批量任务...");
    SendMessageW(g.h_progress, PBM_SETPOS, 0, 0);
    if (g.batch_thread.joinable()) g.batch_thread.join();
    g.batch_thread = std::thread(batch_worker, files, count, folder, p, kernel,
                                 save_denoised, save_noisy);
    SetTimer(g.hwnd, TIMER_BATCH, 100, nullptr);
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void open_output_folder() {
    std::filesystem::path out = project_root() / L"results" / L"batch_output";
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    ShellExecuteW(g.hwnd, L"open", out.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void draw_round_rect(HDC hdc, RECT r, int radius, COLORREF fill, COLORREF border) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(hdc, b);
    HGDIOBJ op = SelectObject(hdc, p);
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(b);
    DeleteObject(p);
}

void draw_card(HDC hdc, RECT r) {
    RECT shadow = r;
    OffsetRect(&shadow, S(2), S(3));
    HBRUSH sb = CreateSolidBrush(RGB(231, 236, 243));
    FillRect(hdc, &shadow, sb);
    DeleteObject(sb);
    draw_round_rect(hdc, r, S(14), C_CARD, C_BORDER);
}

void draw_image_fit(HDC hdc, RECT area, const ImageRGB &img) {
    if (img.empty()) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, C_MUTED);
        SelectObject(hdc, g.font_ui);
        DrawTextW(hdc, L"尚未处理", -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    std::vector<uint32_t> bgra((size_t)img.width * (size_t)img.height);
    for (size_t i = 0; i < bgra.size(); ++i) {
        size_t j = i * 3;
        bgra[i] = ((uint32_t)img.pixels[j] << 16) | ((uint32_t)img.pixels[j + 1] << 8) |
                  (uint32_t)img.pixels[j + 2];
    }
    int aw = area.right - area.left;
    int ah = area.bottom - area.top;
    double sx = (double)aw / (double)img.width;
    double sy = (double)ah / (double)img.height;
    double sc = std::min(sx, sy);
    int dw = std::max(1, (int)(img.width * sc));
    int dh = std::max(1, (int)(img.height * sc));
    int dx = area.left + (aw - dw) / 2;
    int dy = area.top + (ah - dh) / 2;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = img.width;
    bi.bmiHeader.biHeight = -img.height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, nullptr);
    StretchDIBits(hdc, dx, dy, dw, dh, 0, 0, img.width, img.height, bgra.data(), &bi,
                  DIB_RGB_COLORS, SRCCOPY);
}

void draw_preview(HDC hdc, RECT r, const ImageRGB &img, const wchar_t *title,
                  const std::wstring &sub) {
    draw_round_rect(hdc, r, S(10), C_SOFT, C_BORDER);
    RECT tr = r;
    tr.left += S(12);
    tr.top += S(8);
    tr.right -= S(12);
    tr.bottom = tr.top + S(24);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, C_TEXT);
    SelectObject(hdc, g.font_section);
    DrawTextW(hdc, title, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (!sub.empty()) {
        SetTextColor(hdc, C_TEAL);
        SelectObject(hdc, g.font_small);
        DrawTextW(hdc, sub.c_str(), -1, &tr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    RECT ia = r;
    ia.left += S(8);
    ia.right -= S(8);
    ia.top += S(36);
    ia.bottom -= S(8);
    draw_image_fit(hdc, ia, img);
}

void paint_window(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, g.brush_bg);

    RECT header{0, 0, rc.right, S(72)};
    FillRect(hdc, &header, g.brush_navy);
    RECT accent{0, S(70), rc.right, S(74)};
    HBRUSH ab = CreateSolidBrush(C_TEAL);
    FillRect(hdc, &accent, ab);
    DeleteObject(ab);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    SelectObject(hdc, g.font_title);
    RECT tr{S(28), S(10), S(760), S(48)};
    DrawTextW(hdc, L"CUDA 图像去噪实验平台", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(hdc, RGB(174, 196, 228));
    SelectObject(hdc, g.font_small);
    RECT sr{S(30), S(47), S(760), S(70)};
    DrawTextW(hdc, L"椒盐噪声建模 · 中值滤波 · 1000 张批量 GPU 性能分析", -1, &sr,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT lc{S(24), S(132), S(24) + S(344), S(132) + S(690)};
    RECT rcard{S(384), S(132), S(384) + S(972), S(132) + S(690)};
    draw_card(hdc, lc);
    draw_card(hdc, rcard);

    SetTextColor(hdc, C_TEXT);
    SelectObject(hdc, g.font_section);
    RECT accent1{S(46), S(180), S(66), S(183)};
    HBRUSH tb = CreateSolidBrush(C_TEAL);
    FillRect(hdc, &accent1, tb);
    DeleteObject(tb);

    const ImageRGB &o = (g.tab == 0) ? g.original : g.batch_original;
    const ImageRGB &n = (g.tab == 0) ? g.noisy : g.batch_noisy;
    const ImageRGB &f = (g.tab == 0) ? g.denoised : g.batch_filtered;
    bool have_n = (g.tab == 0) ? g.has_noisy : g.batch_has_preview.load();
    bool have_f = (g.tab == 0) ? g.has_denoised : g.batch_has_preview.load();

    int px = S(384) + S(24);
    int py = S(132) + S(66);
    int pw = (S(972) - S(48) - S(36)) / 3;
    int ph = pw + S(34);
    RECT p0{px, py, px + pw, py + ph};
    RECT p1{px + pw + S(18), py, px + 2 * pw + S(18), py + ph};
    RECT p2{px + 2 * (pw + S(18)), py, px + 3 * pw + S(36), py + ph};

    std::wstring sn = have_n ? (fmt(psnr_between(o, n), 2) + L" dB") : L"";
    std::wstring sf = have_f ? (fmt(psnr_between(o, f), 2) + L" dB") : L"";
    if (g.tab == 1 && g.batch_has_preview) {
        std::lock_guard<std::mutex> lock(g.batch_mutex);
        sn = fmt(g.batch_summary.psnr_noisy, 2) + L" dB";
        sf = fmt(g.batch_summary.psnr_filtered, 2) + L" dB";
    }
    draw_preview(hdc, p0, o, L"原图", L"");
    draw_preview(hdc, p1, n, L"加噪图", sn);
    draw_preview(hdc, p2, f, L"去噪图", sf);

    RECT status{0, rc.bottom - S(30), rc.right, rc.bottom};
    HBRUSH status_bg = CreateSolidBrush(C_NAVY2);
    FillRect(hdc, &status, status_bg);
    DeleteObject(status_bg);
    HPEN pen = CreatePen(PS_SOLID, 1, C_BORDER);
    HGDIOBJ op = SelectObject(hdc, pen);
    MoveToEx(hdc, 0, status.top, nullptr);
    LineTo(hdc, rc.right, status.top);
    SelectObject(hdc, op);
    DeleteObject(pen);
    SetTextColor(hdc, RGB(226, 235, 248));
    SelectObject(hdc, g.font_small);
    SetBkMode(hdc, TRANSPARENT);
    RECT st{S(26), status.top, rc.right - S(320), status.bottom};
    DrawTextW(hdc, g.status_text.c_str(), -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT sr2{rc.right - S(320), status.top, rc.right - S(24), status.bottom};
    std::wstring right = g.cuda_ready ? L"CUDA 就绪 · 实时统计" : L"CUDA 未初始化";
    SetTextColor(hdc, RGB(174, 196, 228));
    DrawTextW(hdc, right.c_str(), -1, &sr2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    EndPaint(hwnd, &ps);
}

void draw_button(LPDRAWITEMSTRUCT d) {
    bool tab = (d->CtlID == ID_TAB_SINGLE || d->CtlID == ID_TAB_BATCH);
    bool primary = (d->CtlID == ID_OPEN || d->CtlID == ID_ADD_NOISE || d->CtlID == ID_FILTER ||
                    d->CtlID == ID_ONE_CLICK || d->CtlID == ID_BATCH_START);
    bool danger = (d->CtlID == ID_BATCH_CANCEL);
    bool selected = (d->CtlID == ID_TAB_SINGLE && g.tab == 0) ||
                    (d->CtlID == ID_TAB_BATCH && g.tab == 1);
    bool disabled = (d->itemState & ODS_DISABLED) != 0;
    bool pressed = (d->itemState & ODS_SELECTED) != 0;

    COLORREF fill = C_SOFT, border = C_BORDER, text = C_TEXT;
    if (tab) {
        fill = selected ? C_ACCENT : RGB(238, 242, 248);
        border = selected ? C_ACCENT : RGB(226, 232, 240);
        text = selected ? RGB(255, 255, 255) : C_MUTED;
    } else if (primary) {
        fill = pressed ? C_ACCENT_DARK : C_ACCENT;
        border = fill;
        text = RGB(255, 255, 255);
    } else if (danger) {
        fill = pressed ? RGB(190, 45, 45) : RGB(239, 68, 68);
        border = fill;
        text = RGB(255, 255, 255);
    } else if (selected) {
        fill = C_ACCENT;
        border = C_ACCENT;
        text = RGB(255, 255, 255);
    }
    if (disabled) {
        fill = RGB(235, 238, 243);
        border = RGB(226, 232, 240);
        text = RGB(160, 170, 184);
    }

    RECT r = d->rcItem;
    draw_round_rect(d->hDC, r, tab ? S(9) : S(8), fill, border);
    wchar_t text_buf[256] = {};
    GetWindowTextW(d->hwndItem, text_buf, 256);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, text);
    SelectObject(d->hDC, tab ? g.font_button : g.font_button);
    DrawTextW(d->hDC, text_buf, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

bool capture_window(const std::filesystem::path &path) {
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return false;
    HDC hdc = GetDC(g.hwnd);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    BOOL ok = PrintWindow(g.hwnd, mem, 1);
    if (!ok) ok = BitBlt(mem, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
    std::vector<uint8_t> bits((size_t)w * (size_t)h * 4u);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    GetDIBits(mem, bmp, 0, (UINT)h, bits.data(), &bi, DIB_RGB_COLORS);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(g.hwnd, hdc);

    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + (DWORD)bits.size();
    ih.biSize = sizeof(BITMAPINFOHEADER);
    ih.biWidth = w;
    ih.biHeight = h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = (DWORD)bits.size();
    FILE *fp = _wfopen(path.c_str(), L"wb");
    if (!fp) return false;
    fwrite(&fh, sizeof(fh), 1, fp);
    fwrite(&ih, sizeof(ih), 1, fp);
    fwrite(bits.data(), 1, bits.size(), fp);
    fclose(fp);
    return true;
}

void handle_batch_done() {
    if (g.batch_thread.joinable()) g.batch_thread.join();
    KillTimer(g.hwnd, TIMER_BATCH);
    BatchSummary s;
    {
        std::lock_guard<std::mutex> lock(g.batch_mutex);
        s = g.batch_summary;
    }
    set_batch_ui(false);
    g.batch_running = false;
    SendMessageW(g.h_progress, PBM_SETPOS, 100, 0);
    if (!s.error.empty()) {
        set_status(s.error);
        if (s.error != L"已取消") {
            MessageBoxW(g.hwnd, s.error.c_str(), L"批量处理", MB_ICONWARNING);
        }
    } else {
        set_status(L"批量处理完成");
    }
    update_metrics();
    InvalidateRect(g.hwnd, nullptr, FALSE);
    if (g.capture_when_done && !g.capture_path.empty()) {
        g.capture_when_done = false;
        SetTimer(g.hwnd, TIMER_CAPTURE, 700, nullptr);
    }
}

void initialize_cuda() {
    set_status(L"正在初始化 CUDA / NVRTC...");
    UpdateWindow(g.hwnd);
    std::string err;
    g.kernel_path = find_kernel();
    g.cuda_ready = g.backend.initialize(g.kernel_path, err);
    if (!g.cuda_ready) {
        std::wstring msg = L"CUDA 初始化失败：\n" + utf8_to_wstring(err);
        MessageBoxW(g.hwnd, msg.c_str(), L"CUDA", MB_ICONERROR);
        set_status(L"CUDA 初始化失败");
        g.gpu_text = L"未就绪";
        SetWindowTextW(g.h_gpu_info, g.gpu_text.c_str());
        return;
    }
    const auto &info = g.backend.gpu_info();
    std::wostringstream os;
    os << utf8_to_wstring(info.name) << L"  ·  CC " << info.compute_major << L"."
       << info.compute_minor << L"  ·  " << (info.total_memory / (1024 * 1024)) << L" MiB";
    g.gpu_text = os.str();
    SetWindowTextW(g.h_gpu_info, g.gpu_text.c_str());
    set_status(L"CUDA 就绪，选择图片开始处理");
    if (g.files.empty() || g.original.empty()) load_current();
    if (g.auto_run) {
        g.auto_run = false;
        if (g.tab == 1) {
            g.capture_when_done = !g.capture_path.empty();
            start_batch();
        } else {
            run_one_click();
            if (!g.capture_path.empty()) SetTimer(g.hwnd, TIMER_CAPTURE, 1200, nullptr);
        }
    }
}

void create_controls(HWND hwnd) {
    g.h_tab_single = make_button(hwnd, ID_TAB_SINGLE, L"单张处理");
    g.h_tab_batch = make_button(hwnd, ID_TAB_BATCH, L"1000 张批量");
    g.h_open = make_button(hwnd, ID_OPEN, L"打开图片");
    g.h_prev = make_button(hwnd, ID_PREV, L"上一张");
    g.h_next = make_button(hwnd, ID_NEXT, L"下一张");
    g.h_file_label = make_static(hwnd, ID_FILE_LABEL, L"未选择图片", SS_LEFT | SS_NOPREFIX);
    set_font(g.h_file_label, g.font_small);
    g.h_noise_value = make_static(hwnd, ID_NOISE_VALUE, L"5.0%", SS_RIGHT | SS_NOPREFIX);
    set_font(g.h_noise_value, g.font_small);
    g.h_noise_label = make_static(hwnd, ID_NOISE_LABEL, L"噪声比例", SS_LEFT | SS_NOPREFIX);
    set_font(g.h_noise_label, g.font_small);
    g.h_kernel_label = make_static(hwnd, ID_KERNEL_LABEL, L"滤波窗口", SS_LEFT | SS_NOPREFIX);
    set_font(g.h_kernel_label, g.font_small);
    g.h_noise_slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ,
                                        0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_NOISE_SLIDER,
                                        g.instance, nullptr);
    SendMessageW(g.h_noise_slider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
    SendMessageW(g.h_noise_slider, TBM_SETPOS, TRUE, 50);
    g.h_kernel = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                 0, 0, 10, 200, hwnd, (HMENU)(INT_PTR)ID_KERNEL, g.instance, nullptr);
    set_font(g.h_kernel, g.font_ui);
    SendMessageW(g.h_kernel, CB_ADDSTRING, 0, (LPARAM)L"3 × 3  （推荐）");
    SendMessageW(g.h_kernel, CB_ADDSTRING, 0, (LPARAM)L"5 × 5");
    SendMessageW(g.h_kernel, CB_ADDSTRING, 0, (LPARAM)L"7 × 7");
    SendMessageW(g.h_kernel, CB_SETCURSEL, 0, 0);
    g.h_add_noise = make_button(hwnd, ID_ADD_NOISE, L"① 添加椒盐噪声");
    g.h_filter = make_button(hwnd, ID_FILTER, L"② 中值滤波去噪");
    g.h_one_click = make_button(hwnd, ID_ONE_CLICK, L"一键加噪 + 去噪");
    g.h_save_noisy = make_button(hwnd, ID_SAVE_NOISY, L"保存加噪图");
    g.h_save_denoised = make_button(hwnd, ID_SAVE_DENOISED, L"保存去噪图");
    g.h_single_hint = make_static(hwnd, ID_SINGLE_HINT,
                                  L"建议流程：① 加噪 → ② 去噪。\r\n一键处理使用融合内核，适合快速演示。",
                                  SS_LEFT | SS_NOPREFIX);
    set_font(g.h_single_hint, g.font_small);

    g.h_left_title = make_static(hwnd, ID_LEFT_TITLE, L"参数与操作", SS_LEFT | SS_NOPREFIX);
    set_font(g.h_left_title, g.font_section);
    g.h_right_title = make_static(hwnd, ID_RIGHT_TITLE, L"处理结果预览", SS_LEFT | SS_NOPREFIX);
    set_font(g.h_right_title, g.font_section);
    g.h_gpu_info = make_static(hwnd, ID_GPU_INFO, L"GPU 检测中...", SS_RIGHT | SS_NOPREFIX);
    set_font(g.h_gpu_info, g.font_small);
    g.h_metrics = make_static(hwnd, ID_METRICS, L"", SS_LEFT | SS_NOPREFIX);
    set_font(g.h_metrics, g.font_mono);

    g.h_batch_folder_label = make_static(hwnd, ID_BATCH_FOLDER_LABEL, L"图片文件夹", SS_LEFT | SS_NOPREFIX);
    g.h_batch_folder = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                       0, 0, 10, 30, hwnd, (HMENU)(INT_PTR)ID_BATCH_FOLDER,
                                       g.instance, nullptr);
    set_font(g.h_batch_folder, g.font_small);
    g.h_browse = make_button(hwnd, ID_BROWSE, L"浏览...");
    g.h_batch_count = make_static(hwnd, ID_BATCH_COUNT, L"", SS_LEFT | SS_NOPREFIX);
    set_font(g.h_batch_count, g.font_small);
    g.h_batch_noise_label = make_static(hwnd, ID_BATCH_NOISE_LABEL, L"噪声比例", SS_LEFT | SS_NOPREFIX);
    g.h_batch_noise_value = make_static(hwnd, ID_BATCH_NOISE_VALUE, L"5.0%", SS_RIGHT | SS_NOPREFIX);
    set_font(g.h_batch_noise_value, g.font_small);
    g.h_batch_noise = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ,
                                      0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_BATCH_NOISE,
                                      g.instance, nullptr);
    SendMessageW(g.h_batch_noise, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
    SendMessageW(g.h_batch_noise, TBM_SETPOS, TRUE, 50);
    g.h_batch_kernel_label = make_static(hwnd, ID_BATCH_KERNEL_LABEL, L"滤波窗口", SS_LEFT | SS_NOPREFIX);
    g.h_batch_kernel = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                       0, 0, 10, 200, hwnd, (HMENU)(INT_PTR)ID_BATCH_KERNEL,
                                       g.instance, nullptr);
    set_font(g.h_batch_kernel, g.font_ui);
    SendMessageW(g.h_batch_kernel, CB_ADDSTRING, 0, (LPARAM)L"3 × 3  （推荐）");
    SendMessageW(g.h_batch_kernel, CB_ADDSTRING, 0, (LPARAM)L"5 × 5");
    SendMessageW(g.h_batch_kernel, CB_ADDSTRING, 0, (LPARAM)L"7 × 7");
    SendMessageW(g.h_batch_kernel, CB_SETCURSEL, 0, 0);
    g.h_save_denoised_check = CreateWindowExW(0, L"BUTTON", L"保存去噪结果（可选，1000 张）",
                                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                              0, 0, 10, 24, hwnd, (HMENU)(INT_PTR)ID_SAVE_DENOISED_CHECK,
                                              g.instance, nullptr);
    set_font(g.h_save_denoised_check, g.font_small);
    g.h_save_noisy_check = CreateWindowExW(0, L"BUTTON", L"同时保存加噪结果",
                                           WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                           0, 0, 10, 24, hwnd, (HMENU)(INT_PTR)ID_SAVE_NOISY_CHECK,
                                           g.instance, nullptr);
    set_font(g.h_save_noisy_check, g.font_small);
    g.h_batch_start = make_button(hwnd, ID_BATCH_START, L"开始批量处理");
    g.h_batch_cancel = make_button(hwnd, ID_BATCH_CANCEL, L"取消");
    g.h_open_output = make_button(hwnd, ID_OPEN_OUTPUT, L"打开结果文件夹");
    g.h_progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                   0, 0, 10, 18, hwnd, (HMENU)(INT_PTR)ID_PROGRESS, g.instance, nullptr);
    SendMessageW(g.h_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    g.h_batch_status = make_static(hwnd, ID_BATCH_STATUS, L"", SS_LEFT | SS_NOPREFIX);
    set_font(g.h_batch_status, g.font_small);
    g.h_batch_hint = make_static(hwnd, ID_BATCH_HINT,
                                 L"默认读取 D:\\项目\\嵌入式\\image\\image。\r\n"
                                 L"一次送入 GPU，批内 1000 张共享两次内核启动。",
                                 SS_LEFT | SS_NOPREFIX);
    set_font(g.h_batch_hint, g.font_small);
    SetWindowTextW(g.h_batch_folder, g.batch_folder.wstring().c_str());
    {
        auto files = list_images(g.batch_folder);
        std::wstring text = L"共 " + std::to_wstring(files.size()) + L" 张图片";
        if (files.size() > 1000) text += L"，本次处理前 1000 张";
        SetWindowTextW(g.h_batch_count, text.c_str());
    }
    layout_controls();
    show_tab(g.tab);
}

void update_noise_labels() {
    int p1 = (int)SendMessageW(g.h_noise_slider, TBM_GETPOS, 0, 0);
    int p2 = (int)SendMessageW(g.h_batch_noise, TBM_GETPOS, 0, 0);
    SetWindowTextW(g.h_noise_value, (fmt(p1 / 10.0, 1) + L"%").c_str());
    SetWindowTextW(g.h_batch_noise_value, (fmt(p2 / 10.0, 1) + L"%").c_str());
    if (g.tab == 0) update_metrics();
}

void parse_command_line() {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&](std::wstring &v) -> bool {
            if (i + 1 >= argc) return false;
            v = argv[++i];
            return true;
        };
        std::wstring v;
        if (a == L"--capture" && next(v)) g.capture_path = v;
        else if (a == L"--tab" && next(v)) g.tab = (v == L"batch" ? 1 : 0);
        else if (a == L"--autorun") g.auto_run = true;
        else if (a == L"--count" && next(v)) g.batch_limit = std::max(1, _wtoi(v.c_str()));
        else if (a == L"--folder" && next(v)) g.batch_folder = v;
        else if (a == L"--image" && next(v)) {
            std::filesystem::path p = v;
            refresh_file_list(p.parent_path(), p);
        }
    }
    LocalFree(argv);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g.hwnd = hwnd;
        create_brushes();
        create_fonts();
        create_controls(hwnd);
        PostMessageW(hwnd, WM_APP_INIT_CUDA, 0, 0);
        return 0;

    case WM_APP_INIT_CUDA:
        initialize_cuda();
        return 0;

    case WM_APP_BATCH_DONE:
        handle_batch_done();
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp);
        switch (id) {
        case ID_TAB_SINGLE: show_tab(0); break;
        case ID_TAB_BATCH: show_tab(1); break;
        case ID_OPEN: open_image_dialog(); break;
        case ID_PREV:
            if (!g.files.empty()) { g.current = (g.current - 1 + (int)g.files.size()) % (int)g.files.size(); load_current(); }
            break;
        case ID_NEXT:
            if (!g.files.empty()) { g.current = (g.current + 1) % (int)g.files.size(); load_current(); }
            break;
        case ID_ADD_NOISE: run_add_noise(); break;
        case ID_FILTER: run_filter(); break;
        case ID_ONE_CLICK: run_one_click(); break;
        case ID_SAVE_NOISY: save_current(false); break;
        case ID_SAVE_DENOISED: save_current(true); break;
        case ID_BROWSE: browse_batch_folder(); break;
        case ID_BATCH_START: start_batch(); break;
        case ID_BATCH_CANCEL:
            g.batch_cancel = true;
            set_stage(L"正在取消...");
            break;
        case ID_OPEN_OUTPUT: open_output_folder(); break;
        default: break;
        }
        return 0;
    }

    case WM_HSCROLL: {
        if ((HWND)lp == g.h_noise_slider || (HWND)lp == g.h_batch_noise) {
            update_noise_labels();
        }
        return 0;
    }

    case WM_DRAWITEM:
        draw_button((LPDRAWITEMSTRUCT)lp);
        return TRUE;

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        HWND ctl = (HWND)lp;
        SetBkMode(hdc, TRANSPARENT);
        if (ctl == g.h_gpu_info) {
            SetTextColor(hdc, RGB(200, 215, 238));
            return (LRESULT)g.brush_navy;
        }
        SetTextColor(hdc, C_TEXT);
        return (LRESULT)g.brush_card;
    }

    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wp;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, C_TEXT);
        return (LRESULT)g.brush_card;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, RGB(255, 255, 255));
        SetTextColor(hdc, C_TEXT);
        return (LRESULT)g.brush_white;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        paint_window(hwnd);
        return 0;

    case WM_TIMER:
        if (wp == TIMER_BATCH && g.batch_running.load()) {
            int p = g.batch_progress.load();
            SendMessageW(g.h_progress, PBM_SETPOS, (WPARAM)std::max(0, std::min(100, p)), 0);
            std::wstring st = get_stage();
            if (!st.empty()) set_status(st);
        } else if (wp == TIMER_CAPTURE) {
            KillTimer(hwnd, TIMER_CAPTURE);
            if (g.batch_running.load()) {
                SetTimer(hwnd, TIMER_CAPTURE, 500, nullptr);
                return 0;
            }
            if (!g.capture_path.empty()) {
                RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                capture_window(g.capture_path);
            }
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_CLOSE:
        if (g.batch_running.load()) {
            if (MessageBoxW(hwnd, L"批量处理正在进行，确定退出吗？", L"退出",
                            MB_YESNO | MB_ICONQUESTION) != IDYES) {
                return 0;
            }
            g.batch_cancel = true;
            if (g.batch_thread.joinable()) g.batch_thread.join();
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_BATCH);
        KillTimer(hwnd, TIMER_CAPTURE);
        if (g.batch_thread.joinable()) g.batch_thread.join();
        delete_fonts();
        delete_brushes();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int run_gui(HINSTANCE instance, int show_command) {
    g.instance = instance;
    SetProcessDPIAware();

    HDC screen = GetDC(nullptr);
    int dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int screen_w = work.right - work.left;
    int screen_h = work.bottom - work.top;
    double desired = dpi / 96.0;
    double cap_w = (double)screen_w * 0.94 / 1380.0;
    double cap_h = (double)screen_h * 0.92 / 900.0;
    g.scale = std::max(0.75, std::min(desired, std::min(cap_w, cap_h)));
    g.dpi = dpi;

    g.folder = default_image_dir();
    g.batch_folder = g.folder;
    refresh_file_list(g.folder, std::filesystem::path(L"0012.jpg"));
    parse_command_line();
    if (g.batch_folder.empty()) g.batch_folder = g.folder;

    const wchar_t *cls = L"CUDA_MedianFilter_App";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);

    int client_w = S(1380);
    int client_h = S(880);
    RECT wr{0, 0, client_w, client_h};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectEx(&wr, style, FALSE, 0);
    int win_w = wr.right - wr.left;
    int win_h = wr.bottom - wr.top;
    int x = work.left + std::max(0, (screen_w - win_w) / 2);
    int y = work.top + std::max(0, (screen_h - win_h) / 2);

    HWND hwnd = CreateWindowExW(0, cls, L"CUDA 图像去噪实验平台 · 椒盐噪声中值滤波",
                                style, x, y, win_w, win_h, nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

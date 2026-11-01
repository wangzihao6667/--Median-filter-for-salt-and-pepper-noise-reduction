# -*- coding: utf-8 -*-
"""Generate report figures with Pillow (no external plotting dependency)."""
from PIL import Image, ImageDraw, ImageFont
import csv
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "results" / "figures"
OUT.mkdir(parents=True, exist_ok=True)

W, H = 1600, 900
C_BG = (255, 255, 255)
C_NAVY = (20, 33, 61)
C_ACCENT = (47, 111, 237)
C_ACCENT2 = (35, 86, 190)
C_TEAL = (0, 168, 150)
C_ORANGE = (244, 162, 97)
C_RED = (239, 68, 68)
C_GREEN = (22, 163, 74)
C_MUTED = (100, 116, 139)
C_LIGHT = (241, 245, 249)
C_GRID = (226, 232, 240)
C_TEXT = (30, 41, 59)

FONT = "C:/Windows/Fonts/msyh.ttc"
FONT_B = "C:/Windows/Fonts/msyhbd.ttc"
MONO = "C:/Windows/Fonts/consola.ttf"


def font(size, bold=False):
    return ImageFont.truetype(FONT_B if bold else FONT, size)


def mono(size):
    return ImageFont.truetype(MONO, size)


def canvas(title, subtitle=""):
    img = Image.new("RGB", (W, H), C_BG)
    d = ImageDraw.Draw(img)
    d.text((72, 42), title, font=font(38, True), fill=C_NAVY)
    if subtitle:
        d.text((74, 98), subtitle, font=font(21), fill=C_MUTED)
    d.rectangle((72, 138, W - 72, 142), fill=C_TEAL)
    d.text((74, H - 38), "数据来源：CUDA Event 计时 / 课程项目实测", font=font(16), fill=C_MUTED)
    return img, d


def save(img, name):
    p = OUT / name
    img.save(p, "PNG", optimize=True)
    print("saved", p)


def rounded_bar(d, x0, y0, x1, y1, fill, r=8):
    d.rounded_rectangle((x0, y0, x1, y1), radius=r, fill=fill)


def axis_y(d, rect, vmax, ticks=5, fmt="{:.0f}", unit=""):
    x0, y0, x1, y1 = rect
    for i in range(ticks + 1):
        v = vmax * i / ticks
        y = y1 - (y1 - y0) * i / ticks
        d.line((x0, y, x1, y), fill=C_GRID, width=1)
        d.text((x0 - 14, y), fmt.format(v), font=mono(18), fill=C_MUTED, anchor="rm")
    d.line((x0, y1, x1, y1), fill=C_MUTED, width=2)


def chart_mode():
    img, d = canvas("1000 张图像批处理的端到端耗时",
                    "5% 椒盐噪声 · 3×3 中值滤波 · 5 次重复取中位数")
    # left: time
    rect = (210, 250, 760, 760)
    axis_y(d, rect, 800, 4, "{:.0f}", "ms")
    vals = [("逐张处理\n(per-image)", 663.9, C_ORANGE), ("批量分离内核\n(separate)", 465.2, C_ACCENT),
            ("批量融合内核\n(fused)", 467.4, C_TEAL)]
    bw = 120
    gap = 55
    for i, (label, v, color) in enumerate(vals):
        x = rect[0] + 50 + i * (bw + gap)
        h = (rect[3] - rect[1]) * v / 800.0
        rounded_bar(d, x, rect[3] - h, x + bw, rect[3], color)
        d.text((x + bw / 2, rect[3] - h - 28), f"{v:.1f}", font=mono(24), fill=color, anchor="ms")
        d.multiline_text((x + bw / 2, rect[3] + 18), label, font=font(18), fill=C_TEXT,
                         anchor="ma", align="center", spacing=6)
    d.text((rect[0] + 20, rect[1] - 40), "端到端耗时", font=font(24, True), fill=C_NAVY)

    # right: throughput
    rect2 = (980, 250, 1500, 760)
    axis_y(d, rect2, 2400, 4, "{:.0f}", "张/秒")
    vals2 = [("逐张", 1506, C_ORANGE), ("批量分离", 2150, C_ACCENT), ("批量融合", 2140, C_TEAL)]
    bw2 = 130
    gap2 = 40
    for i, (label, v, color) in enumerate(vals2):
        x = rect2[0] + 25 + i * (bw2 + gap2)
        h = (rect2[3] - rect2[1]) * v / 2400.0
        rounded_bar(d, x, rect2[3] - h, x + bw2, rect2[3], color)
        d.text((x + bw2 / 2, rect2[3] - h - 28), f"{v:.0f}", font=mono(24), fill=color, anchor="ms")
        d.text((x + bw2 / 2, rect2[3] + 18), label, font=font(18), fill=C_TEXT, anchor="ma")
    d.text((rect2[0] + 20, rect2[1] - 40), "吞吐率", font=font(24, True), fill=C_NAVY)

    d.text((1080, 805), "批量处理相对逐张提升 1.43×", font=font(20, True), fill=C_ACCENT)
    save(img, "fig_mode.png")


def chart_optimization():
    img, d = canvas("3×3 中值核优化：19 比较器排序网络",
                    "1000 张 · 5% 噪声 · 批量分离模式；同一硬件、同一输入")
    # left: kernel
    rect = (210, 250, 740, 760)
    axis_y(d, rect, 450, 3, "{:.0f}", "ms")
    vals = [("优化前\n插入排序", 407.0, C_ORANGE), ("优化后\n排序网络", 34.8, C_GREEN)]
    for i, (label, v, color) in enumerate(vals):
        x = rect[0] + 80 + i * 230
        h = (rect[3] - rect[1]) * v / 450.0
        rounded_bar(d, x, rect[3] - h, x + 130, rect[3], color)
        d.text((x + 65, rect[3] - h - 28), f"{v:.1f}", font=mono(24), fill=color, anchor="ms")
        d.multiline_text((x + 65, rect[3] + 18), label, font=font(18), fill=C_TEXT, anchor="ma",
                         align="center", spacing=6)
    d.text((rect[0] + 20, rect[1] - 40), "中值核耗时", font=font(24, True), fill=C_NAVY)
    d.text((rect[0] + 335, 298), "11.7×", font=font(40, True), fill=C_GREEN)
    d.text((rect[0] + 335, 348), "核内加速", font=font(19), fill=C_MUTED)

    # right: total
    rect2 = (940, 250, 1480, 760)
    axis_y(d, rect2, 900, 3, "{:.0f}", "ms")
    vals2 = [("优化前", 834.9, C_ORANGE), ("优化后", 465.2, C_GREEN)]
    for i, (label, v, color) in enumerate(vals2):
        x = rect2[0] + 90 + i * 230
        h = (rect2[3] - rect2[1]) * v / 900.0
        rounded_bar(d, x, rect2[3] - h, x + 130, rect2[3], color)
        d.text((x + 65, rect2[3] - h - 28), f"{v:.1f}", font=mono(24), fill=color, anchor="ms")
        d.text((x + 65, rect2[3] + 18), label, font=font(18), fill=C_TEXT, anchor="ma")
    d.text((rect2[0] + 20, rect2[1] - 40), "端到端耗时", font=font(24, True), fill=C_NAVY)
    d.text((rect2[0] + 305, 298), "1.79×", font=font(40, True), fill=C_GREEN)
    d.text((rect2[0] + 305, 348), "整体提升", font=font(19), fill=C_MUTED)
    save(img, "fig_optimization.png")


def chart_kernel():
    img, d = canvas("滤波窗口大小对耗时与质量的影响",
                    "1000 张 · 5% 噪声 · 融合内核 · 3 次重复取中位数")
    panels = [(190, 300, 620, 760), (650, 300, 1080, 760), (1110, 300, 1510, 760)]
    titles = ["端到端耗时 (ms)", "去噪 PSNR (dB)", "理论占用率 (%)"]
    colors = [C_ACCENT, C_TEAL, C_ORANGE]
    data = [(3, 467.5, 34.31, 100.0), (5, 2972.4, 29.46, 100.0), (7, 9148.2, 27.36, 66.7)]
    maxs = [10000, 40, 110]
    fmts = ["{:.0f}", "{:.1f}", "{:.0f}"]
    for p, title, color, mx, fm in zip(panels, titles, colors, maxs, fmts):
        axis_y(d, p, mx, 4, fm)
        d.text((p[0] + 10, p[1] - 38), title, font=font(21, True), fill=C_NAVY)
        bw = 95
        for i, row in enumerate(data):
            v = [row[1], row[2], row[3]][titles.index(title)]
            x = p[0] + 50 + i * 125
            h = (p[3] - p[1]) * v / mx
            c = C_GREEN if row[0] == 3 else color
            rounded_bar(d, x, p[3] - h, x + bw, p[3], c)
            txt = f"{v:.1f}" if title.startswith("去噪") else f"{v:.0f}"
            d.text((x + bw / 2, p[3] - h - 26), txt, font=mono(20), fill=c, anchor="ms")
            d.text((x + bw / 2, p[3] + 18), f"{row[0]}×{row[0]}", font=font(18), fill=C_TEXT, anchor="ma")
    d.text((190, 825), "3×3 在质量与耗时之间最优；5×5/7×7 明显过平滑，且寄存器压力导致占用率下降。",
           font=font(18), fill=C_MUTED)
    save(img, "fig_kernel.png")


def chart_noise():
    img, d = canvas("噪声比例对去噪质量与耗时的影响",
                    "1000 张 · 3×3 窗口 · 融合内核 · 5 次重复取中位数")
    # left line chart
    rect = (190, 260, 780, 740)
    axis_y(d, rect, 40, 5, "{:.0f}", "PSNR (dB)")
    xs = [1, 5, 10, 20]
    noisy = [24.80, 17.81, 14.80, 11.79]
    filtered = [35.00, 34.31, 33.02, 28.37]

    def px(v):
        return rect[0] + (rect[2] - rect[0]) * (v - 1) / 19.0

    def py(v):
        return rect[3] - (rect[3] - rect[1]) * v / 40.0

    for v in [1, 5, 10, 15, 20]:
        x = px(v)
        d.text((x, rect[3] + 12), f"{v}", font=mono(17), fill=C_MUTED, anchor="ma")
    d.text(((rect[0] + rect[2]) / 2, 790), "噪声比例 / %", font=font(19), fill=C_MUTED, anchor="ma")
    for vals, color, label in [(noisy, C_ORANGE, "加噪图"), (filtered, C_GREEN, "去噪图")]:
        pts = [(px(x), py(y)) for x, y in zip(xs, vals)]
        d.line(pts, fill=color, width=4, joint="curve")
        for (x, y), v in zip(pts, vals):
            d.ellipse((x - 7, y - 7, x + 7, y + 7), fill=color, outline="white", width=2)
            d.text((x, y - 18), f"{v:.1f}", font=mono(18), fill=color, anchor="ms")
    d.text((rect[0] + 20, rect[1] - 40), "PSNR 对比", font=font(23, True), fill=C_NAVY)
    d.line((rect[2] - 250, 210, rect[2] - 220, 210), fill=C_ORANGE, width=5)
    d.text((rect[2] - 210, 210), "加噪图", font=font(18), fill=C_TEXT, anchor="lm")
    d.line((rect[2] - 120, 210, rect[2] - 90, 210), fill=C_GREEN, width=5)
    d.text((rect[2] - 80, 210), "去噪图", font=font(18), fill=C_TEXT, anchor="lm")

    # right time bars
    rect2 = (980, 300, 1500, 740)
    axis_y(d, rect2, 600, 3, "{:.0f}", "ms")
    times = [462.2, 459.9, 471.0, 469.1]
    for i, (x, v) in enumerate(zip(xs, times)):
        bx = rect2[0] + 35 + i * 120
        h = (rect2[3] - rect2[1]) * v / 600.0
        rounded_bar(d, bx, rect2[3] - h, bx + 85, rect2[3], C_ACCENT)
        d.text((bx + 42, rect2[3] - h - 26), f"{v:.0f}", font=mono(19), fill=C_ACCENT, anchor="ms")
        d.text((bx + 42, rect2[3] + 16), f"{x}%", font=font(18), fill=C_TEXT, anchor="ma")
    d.text((rect2[0] + 20, rect2[1] - 40), "端到端耗时", font=font(23, True), fill=C_NAVY)
    d.text((1000, 790), "耗时基本不随噪声比例变化：中值滤波的窗口操作数与噪声比例无关。",
           font=font(17), fill=C_MUTED)
    save(img, "fig_noise.png")


def chart_speedup():
    img, d = canvas("CPU 与 GPU 处理 100 张图像的耗时对比",
                    "5% 噪声 · 3×3 · CPU i9-14900HX 8 线程；GPU 含 H2D / 内核 / 统计 / D2H")
    rect = (240, 280, 1420, 720)
    d.line((rect[0], rect[3], rect[2], rect[3]), fill=C_MUTED, width=2)
    vals = [("CPU 8 线程", 808.6, C_ORANGE), ("RTX 4060 GPU", 55.4, C_ACCENT)]
    maxv = 900.0
    for i, (label, v, color) in enumerate(vals):
        y = rect[1] + i * 190
        w = (rect[2] - rect[0]) * v / maxv
        rounded_bar(d, rect[0], y, rect[0] + w, y + 95, color)
        d.text((rect[0] + 14, y + 47), label, font=font(25, True), fill="white", anchor="lm")
        d.text((rect[0] + w + 18, y + 47), f"{v:.1f} ms", font=mono(25), fill=color, anchor="lm")
    d.text((rect[0] + 20, 235), "耗时越短越好（线性坐标，0–900 ms）", font=font(19), fill=C_MUTED)
    d.text((1040, 545), "14.6×", font=font(52, True), fill=C_GREEN)
    d.text((1040, 610), "GPU 加速比", font=font(21), fill=C_MUTED)
    save(img, "fig_speedup.png")


def chart_timeline():
    img, d = canvas("1000 张批量处理时序（单次运行）",
                    "CPU 解码/缓冲与 GPU 阶段；GPU 纯计算阶段仅 0.37 s，端到端 1.37 s")
    x0, x1 = 220, 1500
    total = 1370.0

    def tx(t):
        return x0 + (x1 - x0) * t / total

    # time axis
    for t in range(0, 1401, 200):
        x = tx(t)
        d.line((x, 220, x, 760), fill=C_GRID, width=1)
        d.text((x, 775), f"{t}", font=mono(17), fill=C_MUTED, anchor="ma")
    d.text(((x0 + x1) / 2, 815), "时间 / ms", font=font(19), fill=C_MUTED, anchor="ma")

    rows = [
        ("CPU", 320, [
            ("JPEG 解码 + RGB 拼接", 0, 544, C_ORANGE),
            ("主机输出缓冲分配", 544, 997, (251, 191, 36)),
        ]),
        ("GPU", 490, [
            ("H2D", 997, 1099, C_ACCENT),
            ("融合内核", 1099, 1130, C_TEAL),
            ("统计", 1130, 1177, C_GREEN),
            ("D2H 2.34 GB", 1177, 1370, C_ACCENT2),
        ]),
    ]
    for label, y, segs in rows:
        d.text((150, y + 38), label, font=font(24, True), fill=C_NAVY, anchor="rm")
        d.line((x0, y + 38, x1, y + 38), fill=(248, 250, 252), width=1)
        for name, a, b, color in segs:
            xa, xb = tx(a), tx(b)
            rounded_bar(d, xa, y, max(xa + 2, xb), y + 76, color, 6)
            if xb - xa > 95:
                d.text(((xa + xb) / 2, y + 38), name, font=font(17), fill="white", anchor="mm")
    # Bracket for the two very short GPU stages.
    xa, xb = tx(1099), tx(1177)
    d.line((xa, 575, xb, 575), fill=C_MUTED, width=2)
    d.line((xa, 566, xa, 575), fill=C_MUTED, width=2)
    d.line((xb, 566, xb, 575), fill=C_MUTED, width=2)
    d.text(((xa + xb) / 2, 590), "融合 31 ms + 统计 47 ms", font=font(16), fill=C_TEXT, anchor="ma")
    d.text((240, 195), "CPU 解码与主机缓冲占端到端主要部分；GPU 阶段仅 0.37 s。",
           font=font(19), fill=C_MUTED)
    save(img, "fig_timeline.png")


def chart_gpu():
    rows = []
    with open(ROOT / "results" / "gpu_samples.csv", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            rows.append({k: float(v) for k, v in r.items()})
    img, d = canvas("GPU 利用率采样（nvidia-smi）",
                    "1000 张融合批处理；0–5.6 s 为 NVRTC 编译/启动阶段，之后进入连续批处理")
    # top panel
    rect = (190, 240, 1500, 570)
    axis_y(d, rect, 100, 5, "{:.0f}", "%")
    for k, color, label in [("gpu_util_pct", C_ACCENT, "GPU 利用率"), ("mem_util_pct", C_ORANGE, "显存带宽利用率")]:
        pts = [(rect[0] + (rect[2] - rect[0]) * r["elapsed_s"] / 18.0,
                rect[3] - (rect[3] - rect[1]) * r[k] / 100.0) for r in rows if r["elapsed_s"] <= 18.0]
        d.line(pts, fill=color, width=3, joint="curve")
    d.text((rect[0] + 10, rect[1] - 38), "利用率", font=font(22, True), fill=C_NAVY)
    d.line((1200, 200, 1235, 200), fill=C_ACCENT, width=4)
    d.text((1245, 200), "GPU", font=font(18), fill=C_TEXT, anchor="lm")
    d.line((1320, 200, 1355, 200), fill=C_ORANGE, width=4)
    d.text((1365, 200), "显存带宽", font=font(18), fill=C_TEXT, anchor="lm")
    d.line((tx_gpu(5.6), rect[1], tx_gpu(5.6), rect[3]), fill=C_RED, width=2)
    d.text((tx_gpu(5.6) + 8, rect[1] + 8), "编译结束", font=font(16), fill=C_RED)

    # bottom panel
    rect2 = (190, 650, 1500, 800)
    maxp = 50.0
    d.line((rect2[0], rect2[3], rect2[2], rect2[3]), fill=C_MUTED, width=2)
    pts = [(rect2[0] + (rect2[2] - rect2[0]) * r["elapsed_s"] / 18.0,
            rect2[3] - (rect2[3] - rect2[1]) * r["power_w"] / maxp) for r in rows if r["elapsed_s"] <= 18.0]
    d.line(pts, fill=C_GREEN, width=3, joint="curve")
    d.text((rect2[0] + 10, rect2[1] - 26), "功耗 / W（峰值 38.8 W，GPU 利用率中位数 94%）",
           font=font(18), fill=C_NAVY)
    for t in range(0, 19, 3):
        x = rect2[0] + (rect2[2] - rect2[0]) * t / 18.0
        d.text((x, rect2[3] + 10), f"{t}s", font=mono(16), fill=C_MUTED, anchor="ma")
    save(img, "fig_gpu.png")


def tx_gpu(t):
    return 190 + (1500 - 190) * t / 18.0


def diagram_pipeline():
    img = Image.new("RGB", (1800, 1050), C_BG)
    d = ImageDraw.Draw(img)
    d.text((70, 42), "系统数据流与 GPU 批处理流水线", font=font(38, True), fill=C_NAVY)
    d.text((72, 98), "单张交互与 1000 张批量共用同一套 CUDA 后端；批量模式一次提交全部图像", font=font(20), fill=C_MUTED)
    d.rectangle((70, 138, 1730, 142), fill=C_TEAL)

    boxes = [
        (70, 230, 300, 420, "1000 × JPEG\n640×640×3", C_ORANGE),
        (370, 230, 650, 420, "CPU 8 线程\n解码为连续 RGB\n1.17 GB", C_ORANGE),
        (720, 230, 980, 420, "H2D\n1.17 GB\nPCIe 4.0", C_ACCENT),
        (1050, 210, 1370, 440, "融合内核\n共享内存 tile\n加噪 + 中值滤波", C_TEAL),
        (1440, 230, 1700, 420, "GPU 统计\nMSE / PSNR\n噪声比例", C_GREEN),
    ]
    for x0, y0, x1, y1, text, color in boxes:
        d.rounded_rectangle((x0, y0, x1, y1), radius=18, fill="white", outline=color, width=4)
        d.rectangle((x0, y0, x0 + 10, y1), fill=color)
        d.multiline_text(((x0 + x1) / 2, (y0 + y1) / 2), text, font=font(24, True), fill=C_TEXT,
                         anchor="mm", align="center", spacing=10)
    for i in range(4):
        xa = boxes[i][2] + 8
        xb = boxes[i + 1][0] - 8
        y = 325
        d.line((xa, y, xb, y), fill=C_NAVY, width=4)
        d.polygon((xb, y, xb - 16, y - 9, xb - 16, y + 9), fill=C_NAVY)
    # D2H result box
    d.line((1570, 440, 1570, 560), fill=C_ACCENT2, width=4)
    d.polygon((1570, 560, 1561, 544, 1579, 544), fill=C_ACCENT2)
    d.rounded_rectangle((1280, 565, 1720, 640), radius=14, fill="white", outline=C_ACCENT2, width=3)
    d.text((1500, 602), "主机结果 / 可选 JPEG 保存", font=font(20, True), fill=C_ACCENT2, anchor="mm")
    d.text((1548, 500), "D2H 2.34 GB", font=font(20, True), fill=C_ACCENT2, anchor="rm")

    # bottom option boxes
    d.rounded_rectangle((100, 650, 820, 930), radius=16, fill=C_LIGHT, outline=C_BORDER if False else C_GRID, width=2)
    d.text((130, 675), "单张交互", font=font(26, True), fill=C_NAVY)
    d.text((130, 725), "打开 / 上一张 / 下一张\n① 添加椒盐噪声\n② 中值滤波去噪\n一键融合处理\n保存加噪图 / 去噪图",
           font=font(21), fill=C_TEXT, spacing=10)
    d.rounded_rectangle((900, 650, 1700, 930), radius=16, fill=C_LIGHT, outline=C_GRID, width=2)
    d.text((930, 675), "1000 张批量", font=font(26, True), fill=C_NAVY)
    d.text((930, 725), "选择文件夹 / 前 1000 张\n一次 H2D + 一次融合内核\nGPU 统计 PSNR / 噪声比例\n可选保存 1000 张结果\n显示分阶段耗时与吞吐",
           font=font(21), fill=C_TEXT, spacing=10)
    save(img, "fig_pipeline.png")


def diagram_tile():
    img = Image.new("RGB", (1600, 900), C_BG)
    d = ImageDraw.Draw(img)
    d.text((70, 42), "融合内核对共享内存 tile 的使用", font=font(38, True), fill=C_NAVY)
    d.text((72, 98), "16×16 线程块读取 18×18×3 的含 halo 区域，先加噪写入 shared，再在 shared 上做 3×3 中值", font=font(20), fill=C_MUTED)
    d.rectangle((70, 138, 1530, 142), fill=C_TEAL)
    # tile
    ox, oy, cell = 300, 240, 26
    for y in range(18):
        for x in range(18):
            halo = x in (0, 17) or y in (0, 17)
            fill = (254, 226, 226) if halo else (219, 234, 254)
            d.rectangle((ox + x * cell, oy + y * cell, ox + (x + 1) * cell, oy + (y + 1) * cell),
                        fill=fill, outline="white")
    d.rectangle((ox, oy, ox + 18 * cell, oy + 18 * cell), outline=C_NAVY, width=3)
    d.rectangle((ox + cell, oy + cell, ox + 17 * cell, oy + 17 * cell), outline=C_ACCENT, width=3)
    d.text((ox + 9 * cell, oy + 9 * cell), "16×16\n输出 tile", font=font(20, True), fill=C_ACCENT, anchor="mm", align="center")
    d.text((ox + 9 * cell, oy - 35), "halo（边缘复制）", font=font(18), fill=C_RED, anchor="ma")
    d.text((ox + 9 * cell, oy + 18 * cell + 18), "18×18×3 = 972 B shared / block", font=font(18), fill=C_MUTED, anchor="ma")

    # right text
    tx = 920
    lines = [
        ("1", "线程 (16,16) 块覆盖一个 16×16 输出 tile"),
        ("2", "每个线程加载 tile + 1 像素 halo 的噪声值"),
        ("3", "__syncthreads() 后，每个输出像素从 shared 取 9 个邻居"),
        ("4", "19 比较器网络求中值，写回全局内存"),
        ("5", "相比两内核版本，省去整幅噪声图的全局写 + 读"),
    ]
    for i, (n, t) in enumerate(lines):
        y = 275 + i * 100
        d.ellipse((tx, y - 22, tx + 44, y + 22), fill=C_TEAL)
        d.text((tx + 22, y), n, font=font(20, True), fill="white", anchor="mm")
        d.text((tx + 62, y), t, font=font(21), fill=C_TEXT, anchor="lm")
    d.text((920, 800), "资源：39 寄存器/线程，972 B 静态 shared，理论占用率 100%",
           font=font(19), fill=C_MUTED)
    save(img, "fig_tile.png")


def fig_sample():
    base = ROOT
    paths = [base / "samples" / "0002.jpg", base / "results" / "sample_out" / "noisy" / "0002.jpg",
             base / "results" / "sample_out" / "denoised" / "0002.jpg"]
    labels = ["原图", "加噪图  PSNR 17.81 dB", "去噪图  PSNR 34.31 dB"]
    thumbs = []
    for p in paths:
        im = Image.open(p).convert("RGB").resize((500, 500), Image.Resampling.LANCZOS)
        thumbs.append(im)
    W2, H2 = 1600, 690
    img = Image.new("RGB", (W2, H2), C_BG)
    d = ImageDraw.Draw(img)
    for i, (im, label) in enumerate(zip(thumbs, labels)):
        x = 28 + i * 524
        d.rounded_rectangle((x - 6, 48, x + 506, 620), radius=12, fill="white", outline=C_GRID, width=2)
        img.paste(im, (x, 54))
        d.text((x + 250, 640), label, font=font(24, True), fill=C_TEXT, anchor="ma")
    save(img, "fig_sample.png")


if __name__ == "__main__":
    chart_mode()
    chart_optimization()
    chart_kernel()
    chart_noise()
    chart_speedup()
    chart_timeline()
    chart_gpu()
    diagram_pipeline()
    diagram_tile()
    fig_sample()

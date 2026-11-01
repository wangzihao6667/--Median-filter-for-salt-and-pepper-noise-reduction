# -*- coding: utf-8 -*-
"""Build the course project report DOCX with python-docx."""
from __future__ import annotations

import json
import sys
from pathlib import Path

from docx import Document
from docx.enum.section import WD_SECTION
from docx.enum.table import WD_TABLE_ALIGNMENT, WD_CELL_VERTICAL_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_LINE_SPACING, WD_TAB_ALIGNMENT, WD_TAB_LEADER
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

ROOT = Path(__file__).resolve().parents[2]
FIG = ROOT / "results" / "figures"
OUT = ROOT / "report"
OUT.mkdir(exist_ok=True)

NAVY = RGBColor(20, 33, 61)
ACCENT = RGBColor(47, 111, 237)
TEAL = RGBColor(0, 148, 136)
MUTED = RGBColor(100, 116, 139)
WHITE = RGBColor(255, 255, 255)
HEADER_FILL = "14213D"
LIGHT_FILL = "F1F5F9"
CODE_FILL = "F6F8FB"

PAGES = {}
if len(sys.argv) > 1:
    p = Path(sys.argv[1])
    if p.exists():
        PAGES = json.loads(p.read_text(encoding="utf-8"))


def pg(key: str) -> str:
    return str(PAGES.get(key, "00"))


def set_run_font(run, name="宋体", size=12, bold=None, color=None, italic=None):
    run.font.name = name
    run.font.size = Pt(size)
    if bold is not None:
        run.bold = bold
    if italic is not None:
        run.italic = italic
    if color is not None:
        run.font.color.rgb = color
    rPr = run._element.get_or_add_rPr()
    rFonts = rPr.find(qn("w:rFonts"))
    if rFonts is None:
        rFonts = OxmlElement("w:rFonts")
        rPr.append(rFonts)
    rFonts.set(qn("w:ascii"), name)
    rFonts.set(qn("w:hAnsi"), name)
    rFonts.set(qn("w:eastAsia"), "宋体" if name in ("Times New Roman", "Consolas") else name)


def shade(cell, fill):
    tcPr = cell._tc.get_or_add_tcPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:fill"), fill)
    tcPr.append(shd)


def table_borders(table, color="DCE3EE", sz="4"):
    tbl = table._tbl
    tblPr = tbl.tblPr
    borders = OxmlElement("w:tblBorders")
    for edge in ("top", "left", "bottom", "right", "insideH", "insideV"):
        el = OxmlElement(f"w:{edge}")
        el.set(qn("w:val"), "single")
        el.set(qn("w:sz"), sz)
        el.set(qn("w:space"), "0")
        el.set(qn("w:color"), color)
        borders.append(el)
    tblPr.append(borders)


def add_para(doc, text="", size=12, bold=False, color=None, indent=True,
             align=WD_ALIGN_PARAGRAPH.JUSTIFY, before=0, after=6, line=1.5,
             font="宋体"):
    p = doc.add_paragraph()
    p.alignment = align
    pf = p.paragraph_format
    pf.space_before = Pt(before)
    pf.space_after = Pt(after)
    pf.line_spacing = line
    if indent:
        pf.first_line_indent = Cm(0.74)
    if text:
        r = p.add_run(text)
        set_run_font(r, font, size, bold, color)
    return p


def add_heading(doc, text, level=1, page_break=False):
    h = doc.add_heading(level=level)
    if page_break:
        h.paragraph_format.page_break_before = True
    h.paragraph_format.keep_with_next = True
    h.paragraph_format.space_before = Pt(18 if level == 1 else 12)
    h.paragraph_format.space_after = Pt(8)
    r = h.add_run(text)
    if level == 1:
        set_run_font(r, "黑体", 18, True, NAVY)
    elif level == 2:
        set_run_font(r, "黑体", 15, True, NAVY)
    else:
        set_run_font(r, "黑体", 13, True, RGBColor(30, 41, 59))
    return h


def add_bullet(doc, text, level=0):
    p = doc.add_paragraph(style="List Bullet")
    p.paragraph_format.left_indent = Cm(0.74 + 0.6 * level)
    p.paragraph_format.first_line_indent = Cm(-0.4)
    p.paragraph_format.line_spacing = 1.4
    p.paragraph_format.space_after = Pt(3)
    r = p.add_run(text)
    set_run_font(r, "宋体", 11.5)
    return p


def add_caption(doc, text, kind="图"):
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(3)
    p.paragraph_format.space_after = Pt(10)
    r = p.add_run(text)
    set_run_font(r, "宋体", 10.5, False, MUTED)
    return p


def add_figure(doc, filename, caption, width=15.4):
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(6)
    p.paragraph_format.space_after = Pt(0)
    p.paragraph_format.keep_with_next = True
    run = p.add_run()
    run.add_picture(str(FIG / filename), width=Cm(width))
    add_caption(doc, caption)


def add_table(doc, headers, rows, caption=None, widths=None, font_size=10.5,
              first_col_bold=False):
    if caption:
        p = doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p.paragraph_format.space_before = Pt(6)
        p.paragraph_format.space_after = Pt(3)
        p.paragraph_format.keep_with_next = True
        r = p.add_run(caption)
        set_run_font(r, "宋体", 10.5, True, NAVY)
    table = doc.add_table(rows=1, cols=len(headers))
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = False
    table.style = "Table Grid"
    table_borders(table)
    hdr = table.rows[0]
    trPr = hdr._tr.get_or_add_trPr()
    tblHeader = OxmlElement("w:tblHeader")
    tblHeader.set(qn("w:val"), "true")
    trPr.append(tblHeader)
    for i, htxt in enumerate(headers):
        cell = hdr.cells[i]
        cell.text = ""
        p = cell.paragraphs[0]
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p.paragraph_format.space_before = Pt(2)
        p.paragraph_format.space_after = Pt(2)
        r = p.add_run(htxt)
        set_run_font(r, "宋体", font_size, True, WHITE)
        shade(cell, HEADER_FILL)
        cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
    for row in rows:
        cells = table.add_row().cells
        for i, val in enumerate(row):
            cell = cells[i]
            cell.text = ""
            p = cell.paragraphs[0]
            p.alignment = WD_ALIGN_PARAGRAPH.CENTER if i > 0 else WD_ALIGN_PARAGRAPH.LEFT
            p.paragraph_format.space_before = Pt(2)
            p.paragraph_format.space_after = Pt(2)
            r = p.add_run(str(val))
            set_run_font(r, "宋体", font_size, first_col_bold and i == 0, RGBColor(30, 41, 59))
            cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
    if widths:
        total = sum(widths)
        if total > 16.0:
            widths = [w * 16.0 / total for w in widths]
        for i, w in enumerate(widths):
            table.columns[i].width = Cm(w)
            for row in table.rows:
                row.cells[i].width = Cm(w)
    # Keep short tables on one page and avoid splitting a row.
    for idx, row in enumerate(table.rows):
        trPr = row._tr.get_or_add_trPr()
        cs = OxmlElement("w:cantSplit")
        cs.set(qn("w:val"), "true")
        trPr.append(cs)
        if idx < len(table.rows) - 1:
            for cell in row.cells:
                for p in cell.paragraphs:
                    p.paragraph_format.keep_with_next = True
    doc.add_paragraph().paragraph_format.space_after = Pt(2)
    return table


def add_code(doc, code, caption):
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(4)
    p.paragraph_format.space_after = Pt(2)
    p.paragraph_format.left_indent = Cm(0.3)
    p.paragraph_format.right_indent = Cm(0.2)
    p.paragraph_format.line_spacing = 1.0
    p.paragraph_format.keep_together = True
    pPr = p._p.get_or_add_pPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:fill"), CODE_FILL)
    pPr.append(shd)
    lines = code.strip("\n").split("\n")
    for i, line in enumerate(lines):
        if i:
            p.add_run().add_break()
        r = p.add_run(line if line else " ")
        set_run_font(r, "Consolas", 8.2, False, RGBColor(30, 41, 59))
    add_caption(doc, caption, "代码")


def add_toc_entry(doc, num, title, key, level=0):
    p = doc.add_paragraph()
    p.paragraph_format.left_indent = Cm(0.35 + 0.65 * level)
    p.paragraph_format.space_after = Pt(3)
    p.paragraph_format.line_spacing = 1.05
    p.paragraph_format.tab_stops.add_tab_stop(Cm(15.9), WD_TAB_ALIGNMENT.RIGHT, WD_TAB_LEADER.DOTS)
    text = f"{num}  {title}" if num else title
    r = p.add_run(text)
    set_run_font(r, "宋体", 11, level == 0)
    r2 = p.add_run("\t" + pg(key))
    set_run_font(r2, "Times New Roman", 11, False, MUTED)


def add_field(paragraph, instr, placeholder="1"):
    run = paragraph.add_run()
    fld_begin = OxmlElement("w:fldChar")
    fld_begin.set(qn("w:fldCharType"), "begin")
    instr_el = OxmlElement("w:instrText")
    instr_el.set(qn("xml:space"), "preserve")
    instr_el.text = instr
    fld_sep = OxmlElement("w:fldChar")
    fld_sep.set(qn("w:fldCharType"), "separate")
    text = OxmlElement("w:t")
    text.text = placeholder
    fld_end = OxmlElement("w:fldChar")
    fld_end.set(qn("w:fldCharType"), "end")
    run._r.append(fld_begin)
    run._r.append(instr_el)
    run._r.append(fld_sep)
    run._r.append(text)
    run._r.append(fld_end)


def setup_document():
    doc = Document()
    sec = doc.sections[0]
    sec.page_width = Cm(21.0)
    sec.page_height = Cm(29.7)
    sec.left_margin = Cm(2.4)
    sec.right_margin = Cm(2.2)
    sec.top_margin = Cm(2.4)
    sec.bottom_margin = Cm(2.2)
    sec.header_distance = Cm(1.2)
    sec.footer_distance = Cm(1.2)
    sec.different_first_page_header_footer = True

    styles = doc.styles
    normal = styles["Normal"]
    normal.font.name = "Times New Roman"
    normal.font.size = Pt(12)
    normal.element.rPr.rFonts.set(qn("w:eastAsia"), "宋体")
    normal.paragraph_format.line_spacing = 1.5
    normal.paragraph_format.space_after = Pt(0)

    # header (not on cover)
    hp = sec.header.paragraphs[0]
    hp.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    r = hp.add_run("《嵌入式基础》课程项目 · 实施报告")
    set_run_font(r, "宋体", 8.5, False, MUTED)
    pPr = hp._p.get_or_add_pPr()
    pBdr = OxmlElement("w:pBdr")
    bottom = OxmlElement("w:bottom")
    bottom.set(qn("w:val"), "single")
    bottom.set(qn("w:sz"), "4")
    bottom.set(qn("w:space"), "1")
    bottom.set(qn("w:color"), "DCE3EE")
    pBdr.append(bottom)
    pPr.append(pBdr)

    # footer page field
    fp = sec.footer.paragraphs[0]
    fp.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r = fp.add_run("第 ")
    set_run_font(r, "宋体", 9, False, MUTED)
    add_field(fp, " PAGE ", "1")
    r = fp.add_run(" 页")
    set_run_font(r, "宋体", 9, False, MUTED)
    # make field run font consistent
    for rr in fp.runs:
        set_run_font(rr, "宋体", 9, False, MUTED)

    doc.core_properties.title = "基于 CUDA 的椒盐噪声图像中值滤波去噪"
    doc.core_properties.subject = "嵌入式基础课程项目 实施报告"
    doc.core_properties.author = "课程项目组"
    return doc


def cover(doc):
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(6)
    r = p.add_run("《嵌入式基础》课程项目")
    set_run_font(r, "宋体", 20, True, NAVY)

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(0)
    r = p.add_run("实 施 报 告")
    set_run_font(r, "黑体", 30, True, NAVY)

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(10)
    p.paragraph_format.space_after = Pt(6)
    r = p.add_run("基于 CUDA 的椒盐噪声图像\n中值滤波去噪")
    set_run_font(r, "黑体", 22, True, RGBColor(30, 41, 59))

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_after = Pt(16)
    r = p.add_run("—— 单张交互与 1000 张批量 GPU 加速")
    set_run_font(r, "宋体", 14, False, TEAL)

    table = doc.add_table(rows=4, cols=2)
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = False
    table_borders(table, "CBD5E1", "6")
    rows = [
        ("题    目", "基于 CUDA 的椒盐噪声图像中值滤波去噪"),
        ("组    号", "________________"),
        ("任课教师", "________________"),
        ("联系方式", "________________"),
    ]
    for i, (a, b) in enumerate(rows):
        c0, c1 = table.rows[i].cells
        c0.width = Cm(4.0)
        c1.width = Cm(10.0)
        c0.text = ""
        c1.text = ""
        c0.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
        c1.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
        p0 = c0.paragraphs[0]
        p0.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p0.paragraph_format.space_before = Pt(3)
        p0.paragraph_format.space_after = Pt(3)
        r0 = p0.add_run(a)
        set_run_font(r0, "宋体", 11, True, NAVY)
        p1 = c1.paragraphs[0]
        p1.alignment = WD_ALIGN_PARAGRAPH.LEFT
        p1.paragraph_format.space_before = Pt(3)
        p1.paragraph_format.space_after = Pt(3)
        r1 = p1.add_run(b)
        set_run_font(r1, "宋体", 11, False, RGBColor(30, 41, 59))
        shade(c0, LIGHT_FILL)

    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(8)
    p.paragraph_format.space_after = Pt(2)
    r = p.add_run("项目组成员与分工")
    set_run_font(r, "黑体", 12, True, NAVY)

    headers = ["序号", "学号", "姓名", "工作量比例", "签名"]
    t = doc.add_table(rows=1, cols=5)
    t.alignment = WD_TABLE_ALIGNMENT.CENTER
    t.autofit = False
    t.style = "Table Grid"
    table_borders(t, "CBD5E1", "4")
    for i, h in enumerate(headers):
        cell = t.rows[0].cells[i]
        cell.width = [Cm(1.5), Cm(4.2), Cm(3.6), Cm(3.4), Cm(3.0)][i]
        cell.text = ""
        p = cell.paragraphs[0]
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p.paragraph_format.space_before = Pt(3)
        p.paragraph_format.space_after = Pt(3)
        r = p.add_run(h)
        set_run_font(r, "宋体", 10, True, WHITE)
        shade(cell, HEADER_FILL)
    for i in range(1, 6):
        cells = t.add_row().cells
        vals = [str(i), "", "", "", ""]
        for j, v in enumerate(vals):
            cells[j].width = [Cm(1.5), Cm(4.2), Cm(3.6), Cm(3.4), Cm(3.0)][j]
            cells[j].text = ""
            p = cells[j].paragraphs[0]
            p.alignment = WD_ALIGN_PARAGRAPH.CENTER
            p.paragraph_format.space_before = Pt(4)
            p.paragraph_format.space_after = Pt(4)
            r = p.add_run(v)
            set_run_font(r, "宋体", 10)

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(24)
    r = p.add_run("2026 年 10 月")
    set_run_font(r, "宋体", 14, False, RGBColor(30, 41, 59))
    doc.add_page_break()


def abstract(doc):
    add_heading(doc, "摘要", 1, page_break=False)
    text = (
        "针对嵌入式视觉前端图像在采集与传输过程中容易混入椒盐噪声的问题，本项目设计并实现了一套基于 CUDA 的"
        "椒盐噪声建模与中值滤波去噪系统。系统以 C++/Win32 构建交互界面，以 CUDA C++ 实现噪声生成、3×3/5×5/7×7 "
        "中值滤波和质量统计，并通过 NVRTC 在运行时编译 kernel、通过 CUDA Driver API 调度，避免了 Python 依赖。"
        "单张模式支持打开、浏览、加噪、去噪、保存和 PSNR 对比；批量模式可一次提交 1000 张 640×640 图像，"
        "在 GPU 上完成整批加噪与滤波，并给出 H2D、内核、统计、D2H、端到端耗时和吞吐率。"
        "在 NVIDIA GeForce RTX 4060 Laptop GPU 上，1000 张图像、5% 椒盐噪声、3×3 窗口的批量加速版本"
        "端到端耗时约 0.46 s，GPU 阶段约 0.40 s，吞吐率约 2150 张/s；平均 PSNR 由加噪后的 17.81 dB 提升到 34.31 dB。"
        "与 8 线程 CPU 基线相比，100 张图像的处理速度提升约 14.6 倍。项目进一步比较了逐张处理、批量分离内核、"
        "批量融合内核以及插入排序和 19 比较器排序网络，结合 CUDA Event、kernel 资源属性和 nvidia-smi 采样"
        "分析了 GPU 占用率、寄存器压力、共享内存和端到端瓶颈，并给出了多流重叠、固定内存和异步解码等优化方向。"
    )
    add_para(doc, text, after=8)
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(4)
    r = p.add_run("关键词：")
    set_run_font(r, "黑体", 12, True, NAVY)
    r = p.add_run("CUDA；椒盐噪声；中值滤波；图像去噪；并行计算；性能分析")
    set_run_font(r, "宋体", 12)
    doc.add_page_break()


def toc(doc):
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(2)
    p.paragraph_format.space_after = Pt(14)
    r = p.add_run("目    录")
    set_run_font(r, "黑体", 22, True, NAVY)

    entries = [
        ("", "摘要", "abstract", 0),
        ("1", "项目名称", "sec1", 0),
        ("2", "课程项目要求", "sec2", 0),
        ("3", "项目背景及研究意义", "sec3", 0),
        ("4", "课程项目实施方案", "sec4", 0),
        ("4.1", "设计思想", "sec41", 1),
        ("4.2", "实现方式", "sec42", 1),
        ("4.3", "开发环境", "sec43", 1),
        ("5", "制作过程", "sec5", 0),
        ("5.1", "数据检查", "sec51", 1),
        ("5.2", "正确性验证", "sec52", 1),
        ("5.3", "交互界面实现", "sec53", 1),
        ("5.4", "性能测试方法", "sec54", 1),
        ("5.5", "Nsight 工具使用情况", "sec55", 1),
        ("6", "部分核心代码", "sec6", 0),
        ("7", "项目的结果分析", "sec7", 0),
        ("7.1", "去噪质量", "sec71", 1),
        ("7.2", "1000 张图像的批处理性能", "sec72", 1),
        ("7.3", "优化前后对比", "sec73", 1),
        ("7.4", "窗口大小与噪声比例的影响", "sec74", 1),
        ("7.5", "硬件资源与占用率", "sec75", 1),
        ("7.6", "端到端时序与 GPU 利用率", "sec76", 1),
        ("7.7", "CPU 与 GPU 对比", "sec77", 1),
        ("7.8", "瓶颈分析与优化空间", "sec78", 1),
        ("8", "总结与展望", "sec8", 0),
        ("9", "小组分工", "sec9", 0),
        ("10", "参考文献", "sec10", 0),
        ("附录 A", "运行与复现说明", "appa", 0),
        ("附录 B", "完整性能数据", "appb", 0),
        ("附录 C", "验收演示步骤", "appc", 0),
    ]
    for num, title, key, level in entries:
        add_toc_entry(doc, num, title, key, level)
    doc.add_page_break()


def chapter1(doc):
    add_heading(doc, "1 项目名称", 1, page_break=True)
    add_para(doc, "项目名称：基于 CUDA 的椒盐噪声图像中值滤波去噪。")
    add_para(
        doc,
        "本项目面向嵌入式 AI 视觉前端的图像预处理环节，完成“对混有椒盐噪声的图像使用中值滤波去噪”这一课题。"
        "系统以 NVIDIA GPU 为计算平台，使用 C++/CUDA 实现，不依赖 Python 运行时；既提供单张图片的交互式演示，"
        "也提供 1000 张 640×640 图片的批量处理与性能测试。"
    )
    add_para(
        doc,
        "代码按“CUDA 内核—CUDA 后端—图像 I/O—GUI/CLI”分层：src/kernels.cu 只包含设备端代码，"
        "src/cuda_backend.cpp 负责 NVRTC、Driver API、显存和计时，src/image_io.cpp 负责图像编解码，"
        "src/gui.cpp 和 src/benchmark.cpp 分别是交互入口与命令行基准入口。"
    )
    add_para(doc, "项目的主要功能如下：")
    for s in [
        "单张交互：打开图片、上一张/下一张浏览、实时显示原图/加噪图/去噪图，支持保存结果。",
        "可变参数：图片数量、图片尺寸、噪声比例（0%–20%）和滤波窗口（3×3、5×5、7×7）均可调整。",
        "两步处理：① 添加椒盐噪声；② 对噪声图执行中值滤波。另提供一键融合处理。",
        "批量处理：选择文件夹后一次处理最多 1000 张图片，显示解码、H2D、内核、统计、D2H、保存和端到端耗时。",
        "质量评价：逐图计算加噪/去噪 PSNR、实际噪声比例，批量给出平均值和最小值。",
        "性能分析：输出 CSV、kernel 资源属性、GPU 利用率采样，并提供 Nsight Compute/Systems 复现脚本。",
    ]:
        add_bullet(doc, s)


def chapter2(doc):
    add_heading(doc, "2 课程项目要求", 1, page_break=True)
    add_para(
        doc,
        "课程文档要求以嵌入式 AI 应用中主流的计算机视觉任务为切入点，对图像预处理任务用 GPU 做并行计算加速。"
        "本项目对应“对混有椒盐噪声的图像用中值滤波去噪”题目。文档中的关键要求可以归纳为以下几点。"
    )
    for s in [
        "以小组形式在具备 NVIDIA GPU 的设备上完成项目的设计与实现。",
        "完成基础功能后，对算法进行函数封装和灵活性拓展，增加可变参数用于调整输入图片个数、图片尺寸、"
        "卷积核大小和权重等，考察算法对不同数据和使用场景的适应性。",
        "提交至少包含基础功能实现的 C/C++/CUDA 源码，程序禁止使用 Python。",
        "自备 1000 张 640×640 图片，展示图像处理用时。",
        "利用 Nsight Compute、Nsight Systems 等工具给出硬件利用指标和时序图表，解释异步运行逻辑、"
        "程序瓶颈及优化空间。",
        "附加项：根据性能分析发现瓶颈、改进代码，给出前后代码、时序、硬件利用和用时对比，并解释改进与性能提升的关系。",
    ]:
        add_bullet(doc, s)

    add_para(
        doc,
        "需要说明的是，项目提交的程序本体为 C++/CUDA；Python 仅用于离线生成报告图表和整理 CSV 数据，"
        "不参与图像处理，也不作为程序运行依赖。"
    )
    add_para(doc, "对照课程要求，本项目的完成情况如表 2-1 所示。")
    add_table(
        doc,
        ["课程要求", "本项目实现", "对应证据"],
        [
            ["基础去噪功能", "椒盐噪声 + 3/5/7 中值滤波，单张与批量均可运行", "第 5、7 章；GUI 截图"],
            ["可变参数", "数量、尺寸、噪声比例、窗口大小均可配置", "表 2-2；命令行参数"],
            ["C/C++/CUDA 源码", "C++17 + CUDA C++ kernel，无 Python 运行时", "src/ 目录"],
            ["1000 张 640×640", "1000 张批量测试，完整 CSV 与统计", "表 7-2、图 7-2"],
            ["性能分析", "CUDA Event、kernel 资源、nvidia-smi、Nsight 脚本", "第 7.5–7.8 节"],
            ["附加优化", "逐张→批量；插入排序→19 比较器网络；融合内核尝试", "第 7.3 节"],
            ["交互展示", "Win32 图形界面，单张/批量两个页签", "图 5-1、图 5-2"],
        ],
        caption="表 2-1  课程要求与实现对应关系",
        widths=[3.4, 7.2, 5.0],
    )

    add_table(
        doc,
        ["可变参数", "取值范围", "实现位置", "说明"],
        [
            ["图片数量", "1–1000", "GUI/CLI", "批量一次提交，grid.z = 图片数"],
            ["图片尺寸", "任意 W×H", "CUDA 后端", "边界复制，支持非 16 倍数尺寸"],
            ["噪声比例 p", "0%–20%", "滑杆/CLI", "盐、椒各占 p/2"],
            ["滤波窗口", "3×3、5×5、7×7", "下拉框/CLI", "模板参数 R=1/2/3"],
            ["处理模式", "逐张/分离/融合", "CLI/GUI", "用于性能对比"],
            ["随机种子", "固定基种子", "CUDA 后端", "每个 image id 派生独立种子，结果可复现"],
        ],
        caption="表 2-2  可变参数与实现方式",
        widths=[3.2, 3.8, 4.2, 5.0],
    )


def chapter3(doc):
    add_heading(doc, "3 项目背景及研究意义", 1, page_break=True)
    add_heading(doc, "3.1 椒盐噪声与中值滤波", 2)
    add_para(
        doc,
        "椒盐噪声（salt-and-pepper noise）是图像采集、无线传输和存储过程中常见的一种脉冲噪声。"
        "其典型表现为图像中随机出现少量纯白（盐）或纯黑（椒）像素，故又称脉冲噪声。"
        "设噪声比例为 p，且盐、椒等概率出现，则每个像素被污染的概率为 p，其中盐点和椒点各约 p/2。"
        "在彩色图像中，本项目对同一像素的三个通道同时置为 0 或 255，使噪声表现为黑/白斑点，符合常见的椒盐噪声模型。"
    )
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(4)
    p.paragraph_format.space_after = Pt(4)
    r = p.add_run("P(盐点) ≈ p/2，P(椒点) ≈ p/2，P(未污染) ≈ 1 − p")
    set_run_font(r, "Cambria Math", 11, False, NAVY)

    add_para(
        doc,
        "中值滤波是一种典型的非线性空域滤波方法。对像素 (x, y) 取以它为中心的 (2R+1)×(2R+1) 邻域，"
        "将邻域内所有像素按灰度排序，取中位数作为输出。与均值滤波相比，中值滤波不会把孤立的极值点平均到邻域中，"
        "因此对椒盐噪声特别有效，同时能在一定程度上保护边缘。"
    )
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r = p.add_run("g(x, y) = median{ f(x+i, y+j) | −R ≤ i, j ≤ R }")
    set_run_font(r, "Cambria Math", 11, False, NAVY)
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r = p.add_run("PSNR = 10·log₁₀(255² / MSE)，MSE = (1/N)·Σ(I − K)²")
    set_run_font(r, "Cambria Math", 11, False, NAVY)

    add_heading(doc, "3.2 为什么需要 GPU 加速", 2)
    add_para(
        doc,
        "1000 张 640×640 的 RGB 图像约含 1.23×10⁹ 个字节。若采用 3×3 中值滤波，每个像素需要读取 9 个邻域像素，"
        "整批数据仅“读”就达到约 11 GB；同时每个像素还要对 3 个通道分别做排序。若再考虑加噪、PSNR 统计、"
        "JPEG 解码与编码，CPU 串行实现很难满足嵌入式视觉前端对实时性的要求。"
    )
    add_para(
        doc,
        "GPU 的 SIMT 架构可以把大量像素分配给不同线程，使邻域读取、排序和写回并行执行；"
        "CUDA 的统一寻址、共享内存和 L2 缓存又能有效降低重复访存。"
        "在本项目中，GPU 还承担了整批 1000 张图像的一次性提交，减少了逐张 kernel 启动和同步的开销。"
    )

    add_heading(doc, "3.3 研究意义", 2)
    add_para(
        doc,
        "从课程角度看，本项目覆盖了 CUDA 编程中的线程/块映射、共享内存、边界处理、随机数生成、"
        "kernel 融合、批处理、CUDA Event 计时和性能分析等知识点。"
        "从工程角度看，椒盐噪声去噪是智能驾驶、具身智能、工业视觉等嵌入式 AI 系统的常见预处理步骤，"
        "在有限功耗和算力下把预处理做快、做稳，具有直接的工程价值。"
    )


def chapter4(doc):
    add_heading(doc, "4 课程项目实施方案", 1, page_break=True)
    add_heading(doc, "4.1 设计思想", 2)
    add_para(
        doc,
        "系统采用“CUDA 后端 + 双入口（GUI/CLI）”的结构。CUDA 后端只负责图像数据、显存、kernel 和计时；"
        "GUI 负责交互和展示；CLI 负责可重复的批量基准测试。三者共用同一套 kernel 和参数，避免“演示一套、测试另一套”。"
    )
    add_para(
        doc,
        "批量模式的核心设计是“一次提交、整批处理”：把 1000 张图片解码为连续的 RGB 缓冲区，"
        "一次 H2D 拷贝到显存，启动一个 grid.z=1000 的 kernel，然后一次 D2H 拷贝回主机。"
        "这样既能让所有图片共享同一次 kernel 启动，又能让显存访问保持连续，便于 GPU 打满带宽。"
    )
    add_para(
        doc,
        "为了兼顾“先加噪、再去噪”的教学演示和性能，系统提供三种模式："
    )
    for s in [
        "逐张模式（per-image）：每张图片单独 H2D、噪声 kernel、中值 kernel、D2H，作为批处理基线。",
        "批量分离模式（separate）：整批一次 H2D，先跑噪声 kernel，再跑中值 kernel，最后整批 D2H。",
        "批量融合模式（fused）：整批一次 H2D，在一个 tile 内核中先写共享内存噪声、同步后再做中值，最后整批 D2H。",
    ]:
        add_bullet(doc, s)
    add_figure(doc, "fig_pipeline.png", "图 4-1  系统数据流与 GPU 批处理流水线", 16.0)

    add_heading(doc, "4.2 实现方式", 2)
    add_heading(doc, "4.2.1 椒盐噪声生成", 3)
    add_para(
        doc,
        "噪声生成使用 PCG 哈希而不是 curand，原因有三点：一是无需额外链接 curand，NVRTC 编译更简单；"
        "二是每个像素的随机数只由 image id、坐标和固定种子决定，便于复现和逐图对比；三是哈希计算是纯整数运算，"
        "在 GPU 上开销很小。对每个像素生成两个均匀随机数：第一个判断是否被污染，第二个决定盐/椒。"
        "同一像素的三个通道使用同一个判断结果，因此噪声表现为整像素的黑/白斑点。"
    )
    add_para(
        doc,
        "批量模式下每个 image id 会派生出独立种子，避免 1000 张图片出现完全相同的噪声图案；"
        "统计 kernel 会重新扫描噪声图，按“三通道同为 0 或 255 且与原图不同”统计实际噪声比例，"
        "用于解释设定概率 p 与实际比例之间的微小差异。"
    )

    add_heading(doc, "4.2.2 中值滤波与边界处理", 3)
    add_para(
        doc,
        "中值滤波按通道独立执行。对 3×3 窗口，使用一个 19 比较器的排序网络：它只需要 19 次比较交换，"
        "比插入排序的约 36 次比较少得多，且没有数据相关的循环，编译器可以充分展开。"
        "对 5×5 和 7×7 窗口，元素个数分别为 25 和 49，使用插入排序求中位数；虽然比较次数更多，"
        "但代码简单、结果精确，便于验证。"
    )
    add_para(
        doc,
        "边界采用“复制边缘”策略：当邻域坐标越界时，将其钳制到最近的图像边界。"
        "这样不会在图像四周引入人为的黑边，且实现简单。对任意 W×H 尺寸，kernel 都用钳制后的坐标读取，"
        "因此不要求图像尺寸是 16 的倍数。"
    )

    add_heading(doc, "4.2.3 CUDA 线程映射与共享内存", 3)
    add_para(
        doc,
        "线程块固定为 16×16=256 个线程，grid 的 x、y 方向覆盖图像，z 方向对应图片编号。"
        "每个线程负责一个像素的三个通道。对于 640×640 图像，grid 为 40×40×1000，"
        "总计 160 万个线程块、4.096 亿个像素线程，足以填满 RTX 4060 的 24 个 SM。"
    )
    add_para(
        doc,
        "融合内核为每个 16×16 输出 tile 额外读取 1 像素 halo，形成 18×18 的共享内存区域，"
        "共 18×18×3=972 字节。线程先把“加噪后”的 tile 写入 shared，__syncthreads() 之后，"
        "每个输出像素直接从 shared 读取 9 个邻居，避免把整幅噪声图写回全局再读一次。"
        "图 4-2 给出了 tile 与 halo 的关系。"
    )
    add_figure(doc, "fig_tile.png", "图 4-2  融合内核对共享内存 tile 的使用", 15.0)

    add_heading(doc, "4.2.4 主机端软件结构", 3)
    add_para(
        doc,
        "主机端使用 C++17 和 Win32 API。图像解码/编码使用 stb_image/stb_image_write 的单头文件实现，"
        "通过 _wfopen 和回调接口支持中文路径。GUI 使用原生 Win32 控件和自绘按钮，避免引入 Qt 等大型依赖。"
        "批量任务在独立 std::thread 中执行，主线程只通过原子变量和 PostMessage 更新进度，因此界面不会在 1000 张处理时卡死。"
    )
    add_para(
        doc,
        "CUDA 后端通过 LoadLibrary 动态加载 nvcuda.dll 和 nvrtc64_*.dll，使用 Driver API 完成 context、"
        "显存、module、kernel 和 event 的管理。这样做的直接好处是：主机代码不需要 nvcc 或 MSVC 的 CUDA 集成，"
        "只需要 CUDA Toolkit 的头文件和运行库；kernel 仍然以标准 .cu 源文件提交，并由 NVRTC 在运行时编译成 "
        "sm_89 的 CUBIN，便于在缺少完整 CUDA 编译环境的机器上演示。"
    )

    add_heading(doc, "4.2.5 计时方法与批处理", 3)
    add_para(
        doc,
        "CUDA Event 分别记录 H2D、噪声 kernel、中值 kernel、统计 kernel、D2H 的起止时间；"
        "主机端另用 steady_clock 记录解码、保存和端到端 wall time。批量测试先跑一次完整 warm-up，"
        "再重复 3–5 次，报告中取中位数，以降低笔记本 GPU 功耗/频率波动的影响。"
        "所有原始记录都写入 results/*.csv，便于复核。"
    )

    add_heading(doc, "4.3 开发环境", 2)
    add_table(
        doc,
        ["项目", "配置"],
        [
            ["GPU", "NVIDIA GeForce RTX 4060 Laptop GPU，8 GB，compute capability 8.9，24 SM"],
            ["CPU", "Intel Core i9-14900HX，32 线程（CPU 基线使用 8 线程）"],
            ["CUDA / 驱动", "CUDA Toolkit 13.4；NVIDIA 驱动 617.42；NVRTC 13.4"],
            ["操作系统", "Windows 11，10.0.26200"],
            ["编译工具", "Zig 0.15.2（MinGW-w64 目标，x86_64-windows-gnu）；另提供 MSVC/CMake 构建脚本"],
            ["分析工具", "Nsight Compute 2026.3.1、Nsight Systems 2026.3.2、nvidia-smi"],
            ["图像数据", "1000 张 640×640 RGB JPEG，约 92.7 MB"],
        ],
        caption="表 4-1  开发与测试环境",
        widths=[4.0, 11.8],
        first_col_bold=True,
    )


def chapter5(doc):
    add_heading(doc, "5 制作过程", 1, page_break=True)
    add_heading(doc, "5.1 数据检查", 2)
    add_para(
        doc,
        "原始图片位于 D:\\项目\\嵌入式\\image\\image，文件名为 0001.jpg–1000.jpg。"
        "使用脚本检查后确认：1000 张图片全部为 640×640、RGB 三通道，总大小约 92.7 MB，适合一次性解码为连续缓冲区。"
        "程序在批量处理前会再次校验所有图片的尺寸；若尺寸不一致会立即报错，避免越界或错位。"
    )
    add_table(
        doc,
        ["检查项", "结果"],
        [
            ["文件数量", "1000（0001.jpg–1000.jpg）"],
            ["分辨率", "全部 640×640"],
            ["颜色模式", "全部 RGB（3 通道）"],
            ["总大小", "约 92.7 MB"],
            ["单张解码后大小", "640×640×3 = 1,228,800 字节"],
            ["整批解码后大小", "约 1.17 GB"],
        ],
        caption="表 5-1  数据集检查结果",
        widths=[5.0, 10.0],
        first_col_bold=True,
    )

    add_heading(doc, "5.2 正确性验证", 2)
    add_para(
        doc,
        "为避免“看起来能跑但结果不对”，实现了 CPU 参考实现，并在同一输入、同一种子下逐字节比较 CPU 与 GPU 输出。"
        "测试覆盖 3×3、5×5、7×7 三种窗口，以及分离内核和融合内核两种路径，共 6 组。"
        "比较内容包括加噪图（noisy_diff）和去噪图（filtered_diff），两者均为 0 才判定通过。"
    )
    add_table(
        doc,
        ["窗口", "路径", "加噪图差异字节", "去噪图差异字节", "结果"],
        [
            ["3×3", "separate", "0", "0", "PASS"],
            ["3×3", "fused", "0", "0", "PASS"],
            ["5×5", "separate", "0", "0", "PASS"],
            ["5×5", "fused", "0", "0", "PASS"],
            ["7×7", "separate", "0", "0", "PASS"],
            ["7×7", "fused", "0", "0", "PASS"],
        ],
        caption="表 5-2  CPU/GPU 逐字节一致性测试",
        widths=[2.0, 3.0, 4.2, 4.2, 2.0],
    )
    add_para(
        doc,
        "除自检外，还对 12 张不同内容的样例图进行了人工检查：加噪图能看到随机黑/白斑点，去噪后斑点基本消失，"
        "边缘和纹理没有被明显破坏；PSNR 从加噪后的约 17.8 dB 提升到约 34.3 dB，符合中值滤波的预期。"
    )

    add_heading(doc, "5.3 交互界面实现", 2)
    add_para(
        doc,
        "GUI 分为“单张处理”和“1000 张批量”两个页签。单张页签提供打开、上一张/下一张、噪声比例滑杆、"
        "窗口下拉框，以及“① 添加椒盐噪声”“② 中值滤波去噪”“一键加噪+去噪”三个按钮；"
        "右侧三联图同时展示原图、加噪图和去噪图，并在下方给出实测噪声比例、PSNR 和单步耗时。"
        "批量页签提供文件夹选择、数量提示、参数、保存选项、进度条和分阶段统计。"
    )
    add_figure(doc, "fig_gui_single.png", "图 5-1  单张处理界面（0012.jpg，5% 噪声，3×3 窗口）", 16.0)
    add_figure(doc, "fig_gui_batch.png", "图 5-2  1000 张批量处理界面（5% 噪声，3×3 窗口）", 16.0)

    add_heading(doc, "5.4 性能测试方法", 2)
    add_para(
        doc,
        "正式测试前先完整跑一遍同参数批次作为 warm-up，使 GPU 进入较高频率；随后重复 3–5 次，"
        "取 wall time、各阶段 CUDA Event 时间和 PSNR 的中位数。"
        "为保证可比性，所有模式使用同一组 1000 张图片、同一噪声比例和窗口；"
        "“逐张/分离/融合”的差异只在提交方式和 kernel 组织，不改变噪声种子和滤波算法。"
    )
    add_para(
        doc,
        "测试输出包括 results/bench_mode_1000.csv、bench_noise.csv、bench_kernel.csv、bench_cpu.csv。"
        "CSV 字段包含 H2D、noise、median、kernel、stats、D2H、GPU total、wall、吞吐率、PSNR 和实测噪声比例。"
    )

    add_heading(doc, "5.5 Nsight 工具使用情况", 2)
    add_para(
        doc,
        "项目按课程要求尝试了 Nsight Compute 和 Nsight Systems，并编写了 profile_nsight.bat 复现脚本。"
        "在当前机器上，Nsight Compute 在注入目标进程后返回 ERR_NVGPUCTRPERM："
        "当前账户不是管理员，驱动默认禁止访问 GPU 性能计数器；Nsight Systems 的 CUDA 注入同样受完整性级别限制，"
        "只能采集到进程/OS 事件，未生成 CUDA kernel 数据。"
    )
    add_para(
        doc,
        "为不伪造数据，报告中的硬件指标改用三条可验证途径："
        "（1）CUDA Event 给出各阶段真实耗时；（2）Driver API 的 cuFuncGetAttribute / "
        "cuOccupancyMaxActiveBlocksPerMultiprocessor 给出寄存器、共享内存、每 SM 活跃块数和理论占用率；"
        "（3）nvidia-smi 以约 10 Hz 采样 GPU 利用率、显存利用率、功耗和 SM 频率。"
        "同时保留 Nsight 脚本：在 NVIDIA App 中打开 System → Advanced → Developer → "
        "Manage GPU Performance Counters → Allow access to all users，并以管理员身份运行脚本，"
        "即可生成 .ncu-rep 和 .nsys-rep，用于现场答辩。"
    )
    add_table(
        doc,
        ["工具", "目的", "当前结果", "处理"],
        [
            ["Nsight Compute", "SOL、内存吞吐、占用率、指令统计", "ERR_NVGPUCTRPERM", "脚本保留；管理员权限下可复现"],
            ["Nsight Systems", "CUDA API/kernel 时间线、异步关系", "未采集到 CUDA 事件", "脚本保留；管理员权限下可复现"],
            ["CUDA Event", "H2D/kernel/stats/D2H 分阶段计时", "正常", "报告主计时依据"],
            ["cuFuncGetAttribute", "寄存器/shared/占用率", "正常", "表 7-5"],
            ["nvidia-smi", "GPU/显存利用率、功耗、频率", "正常", "图 7-7"],
        ],
        caption="表 5-3  性能分析工具使用情况",
        widths=[3.2, 5.2, 3.8, 4.4],
    )


def chapter6(doc):
    add_heading(doc, "6 部分核心代码", 1, page_break=True)
    add_heading(doc, "6.1 噪声生成", 2)
    add_para(
        doc,
        "add_impulse 先用 PCG 哈希把“坐标 + 图片编号 + 种子”映射为两个随机数，再用第一个判断是否污染、"
        "第二个决定盐/椒。所有通道共用同一个 hit 标志，因此噪声是整像素的。"
    )
    add_code(
        doc,
        r'''__device__ __forceinline__ unsigned int pcg_hash(unsigned int x) {
    unsigned int state = x * 747796405u + 2891336453u;
    unsigned int word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

__device__ __forceinline__ unsigned char add_impulse(
        unsigned char v, unsigned int seed, int img,
        int x, int y, int w, float p, int *hit) {
    unsigned int h = pcg_hash((unsigned int)((unsigned long long)y * w + x)
                              ^ seed ^ ((unsigned int)img * 0x9e3779b9u));
    unsigned int h2 = pcg_hash(h ^ 0x85ebca6bu);
    *hit = (u01(h) < p) ? 1 : 0;
    return *hit ? ((h2 & 1u) ? (unsigned char)255 : (unsigned char)0) : v;
}''',
        "代码 6-1  基于 PCG 哈希的椒盐噪声生成",
    )

    add_heading(doc, "6.2 19 比较器 3×3 中值网络", 2)
    add_para(
        doc,
        "该网络对 9 个输入做固定的 19 次比较交换，最终 p[4] 就是中位数。由于没有数据相关的 while 循环，"
        "编译器可以把它展开成纯比较/选择指令；这是本项目从 407 ms 降到 35 ms 的关键。"
    )
    add_code(
        doc,
        r'''__device__ __forceinline__ unsigned char median3_network(unsigned char *p) {
    #define CMP(a,b) do { if (p[a] > p[b]) { \
        unsigned char t = p[a]; p[a] = p[b]; p[b] = t; } } while (0)
    CMP(1,2); CMP(4,5); CMP(7,8);
    CMP(0,1); CMP(3,4); CMP(6,7);
    CMP(1,2); CMP(4,5); CMP(7,8);
    CMP(0,3); CMP(5,8); CMP(4,7);
    CMP(3,6); CMP(1,4); CMP(2,5);
    CMP(4,7); CMP(4,2); CMP(6,4);
    CMP(4,2);
    #undef CMP
    return p[4];
}''',
        "代码 6-2  19 比较器 3×3 中值排序网络",
    )

    add_heading(doc, "6.3 融合 tile 内核", 2)
    add_para(
        doc,
        "每个 block 先把 18×18 的含 halo 区域写成“加噪后”的 shared tile，同步后再从 shared 取 9 个邻居做中值。"
        "由于噪声和滤波在同一个 kernel 内完成，省去了整幅噪声图的全局写和读；同时每个像素的噪声仍由坐标唯一确定，"
        "所以融合结果与“先加噪、再滤波”两内核版本逐字节一致。"
    )
    add_code(
        doc,
        r'''template <int R>
__device__ __forceinline__ void fused_impl(unsigned char *smem, ...) {
    const int SW = TILE + 2 * R;              // 18 for 3x3
    int tx = threadIdx.x, ty = threadIdx.y;
    int bx = blockIdx.x * TILE, by = blockIdx.y * TILE;
    for (int ly = ty; ly < SW; ly += TILE) {
        int gy = clamp(by + ly - R, 0, h - 1);
        for (int lx = tx; lx < SW; lx += TILE) {
            int gx = clamp(bx + lx - R, 0, w - 1);
            int hit = 0;
            for (int c = 0; c < 3; ++c)
                smem[(ly*SW + lx)*3 + c] =
                    add_impulse(in[(gy*w + gx)*3 + c], seed, img, gx, gy, w, p, &hit);
        }
    }
    __syncthreads();
    // 从 smem 取 (2R+1)^2 个邻居，median_channel<R> 输出中值
}''',
        "代码 6-3  融合内核的共享内存装载与同步",
    )

    add_heading(doc, "6.4 GPU 质量统计", 2)
    add_para(
        doc,
        "质量统计同样在 GPU 上完成：每个 block 负责一张图片，线程在整幅图上做 grid-stride 累加，"
        "再用 shared 做归约，最后由 thread 0 写出该图的 MSE 和脉冲数。这样避免了把 1.17 GB 结果再读回 CPU 做统计。"
    )
    add_code(
        doc,
        r'''for (size_t i = t; i < n; i += blockDim.x) {
    int a  = orig[base + i];
    int bn = noisy[base + i];
    int bf = filtered[base + i];
    double dn = (double)(bn - a), df = (double)(bf - a);
    sn += dn * dn;  sf += df * df;
}
__shared__ double shn[256], shf[256];
shn[t] = sn; shf[t] = sf; __syncthreads();
for (int s = blockDim.x / 2; s > 0; s >>= 1) {
    if (t < s) { shn[t] += shn[t+s]; shf[t] += shf[t+s]; }
    __syncthreads();
}
if (t == 0) { mse_noisy[img] = shn[0] / n; mse_filtered[img] = shf[0] / n; }''',
        "代码 6-4  PSNR/MSE 的 GPU 归约",
    )

    add_heading(doc, "6.5 批量任务与计时", 2)
    add_para(
        doc,
        "批量任务在 GUI 的后台线程中执行；主线程只读取原子进度并刷新进度条。"
        "后端在 H2D、噪声、中值、统计、D2H 之间插入 CUDA Event，最后一次性同步并读取各段耗时。"
    )
    add_code(
        doc,
        r'''record(0);
cuMemcpyHtoD(d_in, input, total_bytes);          record(1);  // H2D
launch(noise_kernel, grid, block, noise_args);   record(2);  // noise
launch(median_kernel, grid, block, median_args); record(3);  // median
launch(stats_kernel, dim3(count), dim3(256), stats_args);    // PSNR
record(4);
cuMemcpyDtoH(noisy, d_noisy, total_bytes);
cuMemcpyDtoH(filtered, d_out, total_bytes);      record(5);  // D2H
cuCtxSynchronize();
times.h2d_ms   = elapsed(0, 1);
times.noise_ms = elapsed(1, 2);
times.median_ms= elapsed(2, 3);
times.stats_ms = elapsed(3, 4);
times.d2h_ms   = elapsed(4, 5);''',
        "代码 6-5  批量流水线的 CUDA Event 计时",
    )


def chapter7(doc):
    add_heading(doc, "7 项目的结果分析", 1, page_break=True)
    add_heading(doc, "7.1 去噪质量", 2)
    add_para(
        doc,
        "图 7-1 给出了 0002.jpg 在 5% 噪声、3×3 窗口下的三联对比。加噪图在深色背景上出现大量白点，"
        "水母主体也被黑白斑点污染；去噪后斑点基本消失，主体边缘和触手细节保持完整。"
        "该图加噪 PSNR 为 17.81 dB，去噪后为 34.31 dB，提升约 16.5 dB。"
    )
    add_figure(doc, "fig_sample.png", "图 7-1  单张去噪效果对比（0002.jpg，5% 噪声，3×3）", 15.6)

    add_table(
        doc,
        ["噪声比例", "加噪 PSNR/dB", "去噪 PSNR/dB", "去噪最小值/dB", "实测噪声比例", "端到端耗时/ms"],
        [
            ["1%", "24.80", "35.00", "23.96", "1.00%", "462.2"],
            ["5%", "17.81", "34.31", "23.63", "4.97%", "459.9"],
            ["10%", "14.80", "33.02", "23.13", "9.95%", "471.0"],
            ["20%", "11.79", "28.37", "21.56", "19.90%", "469.1"],
        ],
        caption="表 7-1  不同噪声比例下的质量与耗时（1000 张，3×3，融合内核）",
        widths=[2.4, 3.0, 3.0, 3.2, 3.2, 3.2],
    )
    add_para(
        doc,
        "随着噪声比例升高，加噪 PSNR 快速下降，去噪 PSNR 也随污染密度增加而下降；"
        "但在 1%–10% 范围内仍能保持 33 dB 以上，说明 3×3 中值滤波对稀疏椒盐噪声非常有效。"
        "当噪声达到 20% 时，邻域内被污染的像素比例已很高，中值滤波开始出现误判，PSNR 降到 28.37 dB，"
        "这也是中值滤波的典型适用范围限制。"
    )

    add_heading(doc, "7.2 1000 张图像的批处理性能", 2)
    add_para(
        doc,
        "在 1000 张 640×640、5% 噪声、3×3 窗口下，三种提交方式的端到端耗时和吞吐率如表 7-2 与图 7-2 所示。"
        "逐张模式每张图片单独 H2D、两个 kernel、D2H，共 1000 次提交和同步；"
        "批量分离模式把整批数据一次提交，H2D 和 D2H 都是 1.17 GB/2.34 GB 的大块传输；"
        "批量融合模式再把噪声和中值合并到一个 kernel。"
    )
    add_table(
        doc,
        ["模式", "端到端/ms", "GPU 阶段/ms", "内核/ms", "吞吐/(张/s)", "相对逐张"],
        [
            ["逐张 per-image", "663.9", "663.9", "—", "1506", "1.00×"],
            ["批量分离 separate", "465.2", "402.8", "34.8", "2150", "1.43×"],
            ["批量融合 fused", "467.4", "404.8", "37.8", "2140", "1.42×"],
        ],
        caption="表 7-2  1000 张图像的三种处理方式（5% 噪声，3×3，5 次中位数）",
        widths=[3.8, 3.0, 3.0, 2.6, 3.0, 2.4],
    )
    add_figure(doc, "fig_mode.png", "图 7-2  三种处理方式的端到端耗时与吞吐率", 15.8)
    add_para(
        doc,
        "批量分离模式相对逐张模式把端到端时间从 663.9 ms 降到 465.2 ms，提升 1.43 倍。"
        "收益主要来自两点：一是把 1000 次小拷贝、2000 次 kernel 启动和 1000 次同步合并为一次大拷贝和两次 kernel 启动；"
        "二是连续的大块 H2D/D2H 更容易打满 PCIe 带宽。"
        "值得注意的是，融合模式与分离模式几乎持平，甚至略慢：在 3×3 窗口下，中值核已经从 365 ms 降到 21.7 ms，"
        "整批时间主要由 H2D（约 107 ms）、D2H（约 202 ms）和统计（约 58 ms）决定，融合省下的全局读写不再显著。"
    )

    add_heading(doc, "7.3 优化前后对比", 2)
    add_para(
        doc,
        "本项目做了两组可量化的优化，均在同一硬件、同一 1000 张输入、同一 5% 噪声和 3×3 窗口下对比。"
    )
    add_table(
        doc,
        ["优化项", "优化前", "优化后", "提升", "说明"],
        [
            ["提交方式", "逐张 663.9 ms", "批量 465.2 ms", "1.43×", "一次 H2D + 两次 kernel + 一次 D2H"],
            ["3×3 中值核", "插入排序 407.0 ms", "19 比较器 34.8 ms", "11.7×", "消除数据相关循环，指令级并行提高"],
            ["端到端（分离）", "834.9 ms", "465.2 ms", "1.79×", "两项优化叠加"],
        ],
        caption="表 7-3  优化前后对比（1000 张，5% 噪声，3×3）",
        widths=[3.8, 4.0, 3.8, 2.2, 6.0],
    )
    add_figure(doc, "fig_optimization.png", "图 7-3  插入排序与 19 比较器网络的耗时对比", 15.8)
    add_para(
        doc,
        "插入排序的耗时主要不是比较次数，而是数据相关的 while 循环：每个线程的排序路径不同，"
        "导致 warp 内分支发散，编译器也难以把循环完全展开。19 比较器网络是固定顺序的 19 次 compare-exchange，"
        "没有分支，9 个元素可以全部放在寄存器中，因此中值核从 407 ms 降到 34.8 ms，加速 11.7 倍。"
        "端到端只提升 1.79 倍，是因为 H2D、D2H 和统计等固定开销没有变化，这也说明进一步优化的重点应转向数据搬运和主机端。"
    )

    add_heading(doc, "7.4 窗口大小与噪声比例的影响", 2)
    add_table(
        doc,
        ["窗口", "端到端/ms", "融合内核/ms", "PSNR/dB", "最小值/dB", "理论占用率"],
        [
            ["3×3", "467.5", "37.5", "34.31", "23.63", "100%"],
            ["5×5", "2972.4", "2538.8", "29.46", "18.62", "100%"],
            ["7×7", "9148.2", "8723.9", "27.36", "16.51", "66.7%"],
        ],
        caption="表 7-4  窗口大小的影响（1000 张，5% 噪声，融合内核）",
        widths=[2.4, 3.4, 3.8, 2.8, 2.8, 3.0],
    )
    add_figure(doc, "fig_kernel.png", "图 7-4  窗口大小对耗时、PSNR 和占用率的影响", 15.8)
    add_para(
        doc,
        "3×3 在质量和耗时之间最优；5×5 和 7×7 虽然能处理更密集的噪声，但会把更多细节平滑掉，"
        "PSNR 反而下降（5×5 为 29.46 dB，7×7 为 27.36 dB）。"
        "从资源角度看，5×5/7×7 的共享内存和寄存器压力明显增大：分离内核的 5×5 使用 95 个寄存器、"
        "活跃块数只有 2 个/SM，理论占用率 33.3%；7×7 使用 167 个寄存器，只有 1 个块/SM，占用率 16.7%。"
        "融合内核通过共享内存把 5×5/7×7 的占用率提高到 100%/66.7%，但排序本身仍是主要开销。"
    )
    add_figure(doc, "fig_noise.png", "图 7-5  噪声比例对 PSNR 与耗时的影响", 15.8)
    add_para(
        doc,
        "耗时基本不随噪声比例变化（459.9–471.0 ms），因为中值滤波的窗口操作数与噪声比例无关；"
        "变化的只是 PSNR。这符合算法复杂度分析，也说明批量测试的结果是稳定的。"
    )

    add_heading(doc, "7.5 硬件资源与占用率", 2)
    add_para(
        doc,
        "表 7-5 给出了驱动 API 查询到的 kernel 资源属性。理论占用率 = 每 SM 活跃块数 × 256 / 1536。"
        "3×3 的中值核和融合核都达到 100% 理论占用率，5×5/7×7 的分离核受寄存器数量限制明显。"
    )
    add_table(
        doc,
        ["Kernel", "寄存器/线程", "静态 shared/B", "Local/B", "活跃块/SM", "理论占用率"],
        [
            ["median3", "40", "0", "0", "6", "100%"],
            ["fused3", "39", "972", "0", "6", "100%"],
            ["median5", "95", "0", "32", "2", "33.3%"],
            ["fused5", "40", "1200", "32", "6", "100%"],
            ["median7", "167", "0", "56", "1", "16.7%"],
            ["fused7", "56", "1452", "56", "4", "66.7%"],
        ],
        caption="表 7-5  Kernel 资源与理论占用率（block = 256 线程）",
        widths=[3.0, 3.2, 3.6, 2.4, 2.8, 3.2],
    )
    add_para(
        doc,
        "这组数据解释了 5×5/7×7 为什么慢：分离核为了保证通用性，把 25/49 个元素放在局部数组里，"
        "寄存器分配器不得不保留大量中间值，导致每 SM 可同时驻留的 block 数下降，延迟隐藏能力变差。"
        "融合核把数据放到 shared，寄存器压力反而下降，但因每个像素要做 25/49 次比较，计算量仍然很大。"
    )

    add_heading(doc, "7.6 端到端时序与 GPU 利用率", 2)
    add_para(
        doc,
        "图 7-6 把一次 1000 张批量运行拆成 CPU 解码、主机缓冲分配、H2D、融合 kernel、统计、D2H 六段。"
        "GPU 纯计算阶段约 0.37 s，但端到端约 1.37 s；CPU 解码约 0.54 s，主机输出缓冲分配约 0.45 s，"
        "两者合计占了约 73%。这说明“GPU 很快”不等于“系统很快”，主机端的数据准备和结果整理是当前端到端的主要瓶颈。"
    )
    add_figure(doc, "fig_timeline.png", "图 7-6  1000 张批量处理的 CPU/GPU 时序", 16.0)
    add_para(
        doc,
        "主机缓冲分配的时间来自 std::vector 的零初始化：加噪图和去噪图各 1.17 GB，共 2.34 GB，"
        "默认构造会把内存清零。若改为未初始化分配或复用缓冲区，可进一步降低端到端时间。"
        "H2D 约 103 ms、D2H 约 195 ms，合计约 0.30 s；以 3.5 GB 的总传输量估算，等效带宽约 11.7 GB/s，"
        "已接近该平台 PCIe 的实际上限。"
    )
    add_para(
        doc,
        "图 7-7 是 nvidia-smi 以约 10 Hz 采样得到的 GPU 利用率。0–5.6 s 为 NVRTC 编译和启动阶段，"
        "GPU 利用率约 15%；进入连续批处理后，GPU 利用率中位数 94%、峰值 99%，显存带宽利用率约 17%，"
        "功耗峰值 38.8 W，SM 频率稳定在 2595 MHz。高 GPU 利用率说明 kernel 已经把 SM 喂得比较满；"
        "显存带宽利用率不高，则说明 3×3 融合核的算术强度较高、主要受计算/同步而不是显存带宽限制。"
    )
    add_figure(doc, "fig_gpu.png", "图 7-7  批量处理期间的 GPU 利用率、显存带宽和功耗采样", 16.0)

    add_heading(doc, "7.7 CPU 与 GPU 对比", 2)
    add_para(
        doc,
        "CPU 基线使用同一套噪声模型和 3×3 插入排序，8 线程并行处理 100 张图片；GPU 使用批量分离模式，"
        "计时包含 H2D、噪声、中值、统计和 D2H。两者结果 PSNR 均为 33.79 dB，逐字节一致。"
    )
    add_table(
        doc,
        ["实现", "图片数", "耗时/ms", "吞吐/(张/s)", "相对 GPU"],
        [
            ["CPU i9-14900HX，8 线程", "100", "808.6", "123.7", "1.00×"],
            ["RTX 4060，批量分离", "100", "55.4", "1806", "14.6×"],
        ],
        caption="表 7-6  CPU 与 GPU 处理 100 张图像的耗时",
        widths=[6.2, 2.6, 3.2, 3.4, 3.0],
    )
    add_figure(doc, "fig_speedup.png", "图 7-8  CPU 与 GPU 的耗时对比", 15.2)
    add_para(
        doc,
        "14.6 倍的加速比是在“8 线程 CPU、包含全部数据搬运”的保守口径下得到的；若只比较中值 kernel，"
        "GPU 的核内耗时约 5 ms，而 CPU 的 8 线程中值部分约 0.8 s，核内加速比更高。"
        "这也说明 GPU 的优势在于把大量像素的排序并行化，但主机与设备之间的数据搬运会稀释端到端收益。"
    )

    add_heading(doc, "7.8 瓶颈分析与优化空间", 2)
    add_para(
        doc,
        "综合时序、资源属性和利用率，可以把瓶颈分为三层。"
    )
    add_bullet(doc, "主机端：JPEG 解码约 0.54 s、2.34 GB 输出缓冲零初始化约 0.45 s，是端到端最大瓶颈；"
                    "可通过固定内存、复用缓冲、异步解码/编码和多流重叠进一步压缩。")
    add_bullet(doc, "数据搬运：H2D+D2H 约 0.30 s，等效带宽约 11.7 GB/s，已接近 PCIe 实际上限；"
                    "若只回传去噪图（不回传加噪图），可把 D2H 从 2.34 GB 降到 1.17 GB。")
    add_bullet(doc, "GPU 计算：3×3 融合核约 37 ms，GPU 利用率中位数 94%，继续优化的空间主要来自"
                    "减少统计 kernel 的 3.5 GB 读取、使用半精度/整数向量化，以及把统计与滤波合并。")
    add_para(
        doc,
        "具体的后续优化建议如下："
    )
    for s in [
        "使用 cudaHostAlloc/固定内存 + 多流，把第 i+1 批的 H2D 与第 i 批的 kernel/D2H 重叠；",
        "用未初始化分配或对象池复用 2.34 GB 输出缓冲，消除零初始化开销；",
        "批量保存时只回传去噪图，加噪图按需回传；",
        "把 PSNR 统计改为按 block 采样或与中值 kernel 融合，减少一次全量读取；",
        "对 5×5/7×7 使用排序网络/直方图/快速选择，而不是插入排序；",
        "在嵌入式端使用 CUDA Graph 把固定形状的批量流水线固化为图，降低启动开销。",
    ]:
        add_bullet(doc, s)
    add_para(
        doc,
        "这些优化与本文的两项已实现优化并不矛盾：本文已经证明“减少提交次数”和“消除数据相关分支”能带来"
        "1.43× 和 11.7× 的局部提升；下一步则是把主机端和传输端也纳入流水线，追求端到端而不是单 kernel 的加速。"
    )


def chapter8(doc):
    add_heading(doc, "8 总结与展望", 1, page_break=True)
    add_para(
        doc,
        "本项目完成了课程文档要求的椒盐噪声中值滤波去噪功能，并在此基础上做了单张交互、1000 张批量、"
        "质量统计和性能分析。系统全部使用 C++/CUDA 实现，程序不依赖 Python；通过 NVRTC + Driver API，"
        "在缺少完整 CUDA 编译环境的机器上也能直接运行 .cu kernel。"
    )
    add_para(
        doc,
        "在 1000 张 640×640、5% 噪声、3×3 窗口的测试中，批量分离模式端到端耗时 465.2 ms，"
        "GPU 阶段 402.8 ms，吞吐率约 2150 张/s；去噪 PSNR 从 17.81 dB 提升到 34.31 dB。"
        "与 8 线程 CPU 基线相比，100 张图片的处理速度提升约 14.6 倍。"
        "通过把 3×3 中值核从插入排序改为 19 比较器排序网络，核内耗时从 407.0 ms 降到 34.8 ms（11.7×），"
        "端到端从 834.9 ms 降到 465.2 ms（1.79×）。"
    )
    add_para(
        doc,
        "项目也暴露了不足：受当前账户权限限制，Nsight Compute 的性能计数器和 Nsight Systems 的 CUDA 注入"
        "未能采集到数据，报告改用 CUDA Event、驱动资源属性和 nvidia-smi 采样，并保留了管理员权限下的复现脚本；"
        "端到端时间仍受主机解码和缓冲分配限制，尚未实现多流重叠。"
    )
    add_para(
        doc,
        "后续工作可以从三个方向展开：一是用固定内存、多流和 CUDA Graph 做主机/设备重叠；"
        "二是针对 5×5/7×7 设计更低寄存器压力的中值算法；三是把去噪接入更完整的嵌入式视觉流水线，"
        "在实际摄像头视频流上验证实时性和功耗。"
    )


def chapter9(doc):
    add_heading(doc, "9 小组分工", 1, page_break=True)
    add_para(
        doc,
        "本项目按“算法与 CUDA、界面与工程、测试与报告”三个方向分工，具体成员信息请在封面和表中填写。"
    )
    add_table(
        doc,
        ["成员", "学号", "主要工作", "工作量比例"],
        [
            ["组长（请填写）", "", "总体方案、CUDA 内核、融合与优化、性能分析", "40%"],
            ["成员（请填写）", "", "Win32 界面、单张/批量交互、图像 I/O", "35%"],
            ["成员（请填写）", "", "数据整理、正确性测试、图表与报告", "25%"],
        ],
        caption="表 9-1  小组分工",
        widths=[4.0, 3.2, 8.0, 3.0],
    )
    add_para(
        doc,
        "说明：若小组人数不足 3 人，可删除多余行并调整比例；封面中的组号、任课教师、联系方式、"
        "成员姓名/学号/签名也请按实际情况填写。"
    )


def chapter10(doc):
    add_heading(doc, "10 参考文献", 1, page_break=True)
    refs = [
        "NVIDIA. CUDA C++ Programming Guide[EB/OL]. https://docs.nvidia.com/cuda/cuda-c-programming-guide/.",
        "NVIDIA. NVRTC User Guide[EB/OL]. https://docs.nvidia.com/cuda/nvrtc/.",
        "NVIDIA. Nsight Compute User Guide[EB/OL]. https://docs.nvidia.com/nsight-compute/.",
        "NVIDIA. Nsight Systems User Guide[EB/OL]. https://docs.nvidia.com/nsight-systems/.",
        "GONZALEZ R C, WOODS R E. Digital Image Processing[M]. 4th ed. New York: Pearson, 2018.",
        "PITAS I, VENETSANOPOULOS A N. Nonlinear Digital Filters: Principles and Applications[M]. Boston: Springer, 1990.",
        "NVIDIA. ERR_NVGPUCTRPERM: Permission issue with Performance Counters[EB/OL]. "
        "https://developer.nvidia.com/nvidia-development-tools-solutions-ERR_NVGPUCTRPERM-permission-issue-performance-counters.",
        "NOTHINGS. stb: single-file public domain libraries for C/C++[EB/OL]. https://github.com/nothings/stb.",
    ]
    for i, s in enumerate(refs, 1):
        p = doc.add_paragraph()
        p.paragraph_format.left_indent = Cm(0.8)
        p.paragraph_format.first_line_indent = Cm(-0.8)
        p.paragraph_format.line_spacing = 1.35
        p.paragraph_format.space_after = Pt(6)
        r = p.add_run(f"[{i}] {s}")
        set_run_font(r, "宋体", 11)


def appendix(doc):
    add_heading(doc, "附录 A  运行与复现说明", 1, page_break=True)
    add_para(doc, "A.1 直接运行")
    add_para(doc, "双击 run_gui.bat，或直接运行 bin\\median_gui.exe。程序会自动在 CUDA Toolkit 的 bin 目录中查找 nvrtc64_*.dll；若未找到，请把 CUDA_PATH\\bin 和 CUDA_PATH\\bin\\x64 加入 PATH。")
    add_para(doc, "A.2 命令行基准")
    add_code(
        doc,
        r'''bin\median_bench.exe --input "D:\项目\嵌入式\image\image" ^
  --count 1000 --noise 0.05 --kernel 3 --mode fused ^
  --repeat 3 --warmup 1 --csv results\bench_1000.csv''',
        "代码 A-1  1000 张批量基准命令",
    )
    add_para(doc, "A.3 构建")
    add_para(doc, "已提供 build_zig.bat（需要 Zig 0.15+）和 build_msvc.bat（需要 Visual Studio x64 Native Tools）。二者都只编译 C++ 主机代码；kernel 由 NVRTC 在运行时编译，不需要 nvcc。")
    add_para(doc, "A.4 Nsight 复现")
    add_para(doc, "以管理员身份运行 profile_nsight.bat；若 Nsight Compute 报 ERR_NVGPUCTRPERM，请在 NVIDIA App 的 System → Advanced → Developer → Manage GPU Performance Counters 中选择“Allow access to all users”。")

    add_heading(doc, "附录 B  完整性能数据", 1)
    add_para(doc, "表 B-1 给出 1000 张、5% 噪声、3×3 窗口的完整分阶段数据；表 B-2 给出三种窗口的对比。")
    add_table(
        doc,
        ["模式", "H2D/ms", "噪声/ms", "中值/ms", "统计/ms", "D2H/ms", "GPU 合计/ms", "wall/ms"],
        [
            ["逐张", "—", "—", "—", "—", "—", "663.9", "663.9"],
            ["分离", "106.7", "13.1", "21.7", "58.5", "202.4", "402.8", "465.2"],
            ["融合", "107.4", "—", "37.8（融合）", "58.4", "201.7", "404.8", "467.4"],
        ],
        caption="表 B-1  1000 张 5% 噪声 3×3 的分阶段数据",
        widths=[2.4, 2.6, 2.6, 3.6, 2.6, 2.6, 3.2, 2.4],
    )
    add_table(
        doc,
        ["窗口", "H2D/ms", "融合核/ms", "统计/ms", "D2H/ms", "GPU 合计/ms", "wall/ms", "PSNR/dB"],
        [
            ["3×3", "106.2", "37.5", "58.5", "201.5", "404.4", "467.5", "34.31"],
            ["5×5", "106.5", "2538.8", "59.3", "202.9", "2908.9", "2972.4", "29.46"],
            ["7×7", "105.0", "8723.9", "58.2", "198.6", "9085.3", "9148.2", "27.36"],
        ],
        caption="表 B-2  三种窗口的完整数据",
        widths=[2.2, 2.6, 3.0, 2.6, 2.6, 3.2, 2.6, 2.6],
    )

    add_heading(doc, "附录 C  验收演示步骤", 1)
    add_para(doc, "建议按以下顺序演示，时间约 3–5 分钟：")
    for s in [
        "启动 GUI，指出右上角的 GPU 型号、CC 8.9 和显存容量。",
        "在“单张处理”中打开 0012.jpg，拖动噪声比例到 5%，点击“① 添加椒盐噪声”，观察加噪图和 PSNR。",
        "点击“② 中值滤波去噪”，观察去噪图和 PSNR 提升；再点击“一键加噪+去噪”演示融合内核。",
        "切换到“1000 张批量”，确认文件夹为 D:\\项目\\嵌入式\\image\\image，显示“共 1000 张图片”。",
        "保持 5% 和 3×3，点击“开始批量处理”，观察进度条和分阶段耗时；记录总耗时与吞吐率。",
        "打开 results\\bench_mode_1000.csv，说明逐张/分离/融合的差异；展示图 7-3 的优化前后对比，"
        "并说明 Nsight 权限限制和 profile_nsight.bat 的复现方法。",
    ]:
        add_bullet(doc, s)


def main():
    doc = setup_document()
    cover(doc)
    abstract(doc)
    toc(doc)
    chapter1(doc)
    chapter2(doc)
    chapter3(doc)
    chapter4(doc)
    chapter5(doc)
    chapter6(doc)
    chapter7(doc)
    chapter8(doc)
    chapter9(doc)
    chapter10(doc)
    appendix(doc)
    out = OUT / "嵌入式基础课程项目_椒盐噪声中值滤波_项目报告.docx"
    doc.save(out)
    print(out)


if __name__ == "__main__":
    main()

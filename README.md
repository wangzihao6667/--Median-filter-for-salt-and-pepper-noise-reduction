# 基于 CUDA 的椒盐噪声图像中值滤波去噪

《嵌入式基础》课程项目课题 3：对混有椒盐噪声的图像使用中值滤波去噪。项目使用 **C++17 + CUDA C++** 实现，程序本体不依赖 Python；包含单张交互界面、1000 张批量处理、质量统计、性能基准和 Nsight 复现脚本。

## 1. 功能

- **单张处理**
  - 打开 JPG/PNG/BMP，上一张/下一张浏览，中文路径可用。
  - 噪声比例 0%–20% 可调，窗口 3×3/5×5/7×7 可选。
  - ① 添加椒盐噪声；② 中值滤波去噪；另有一键融合处理。
  - 三联图预览：原图 / 加噪图 / 去噪图，显示 PSNR、实测噪声比例和单步耗时。
  - 保存加噪图 / 去噪图。
- **1000 张批量**
  - 选择文件夹，一次提交最多 1000 张 640×640 图片。
  - 整批一次 H2D、一个/两个 kernel、一次 D2H。
  - 显示解码、H2D、噪声、中值/融合、统计、D2H、保存、端到端耗时与吞吐率。
  - 可选保存 1000 张加噪图/去噪图。
- **性能与验证**
  - 逐张 / 批量分离 / 批量融合三种模式。
  - 3×3 使用 19 比较器排序网络；5×5、7×7 使用精确插入排序。
  - CPU 参考实现逐字节对比（3/5/7 窗口、分离/融合路径全部为 0 差异）。
  - CUDA Event 分阶段计时，CSV 输出，kernel 资源属性与理论占用率。
  - `profile_nsight.bat` 一键生成 Nsight Compute / Nsight Systems 报告。

## 2. 目录结构

```
cuda_median_project/
├─ bin/
│  ├─ median_gui.exe        # Win32 图形界面
│  ├─ median_bench.exe      # 命令行基准/自检
│  └─ kernels.cu            # 运行时 NVRTC 编译的 CUDA 源
├─ src/
│  ├─ kernels.cu            # 噪声、3/5/7 中值、融合 tile、统计 kernel
│  ├─ cuda_backend.cpp/.h   # NVRTC + Driver API + 显存 + CUDA Event
│  ├─ image_io.cpp/.h       # stb 图像编解码，支持中文路径
│  ├─ gui.cpp/.h            # Win32 界面
│  ├─ benchmark.cpp/.h      # 批量基准、CPU 基线、CSV
│  ├─ common.cpp/.h
│  ├─ main_gui.cpp
│  └─ main_cli.cpp
├─ samples/                 # 12 张示例图（完整 1000 张请使用原数据目录）
├─ results/
│  ├─ bench_mode_1000.csv   # 逐张/分离/融合
│  ├─ bench_noise.csv       # 1/5/10/20% 噪声
│  ├─ bench_kernel.csv      # 3/5/7 窗口
│  ├─ bench_cpu.csv         # CPU/GPU 对比
│  ├─ gpu_samples.csv       # nvidia-smi 采样
│  ├─ kernel_info.txt       # 寄存器/共享内存/占用率
│  ├─ figures/              # 报告图表
│  ├─ sample_out/           # 12 张加噪/去噪结果样例
│  └─ exp_insertion/        # 插入排序基线 kernel 与数据
├─ report/
│  ├─ 嵌入式基础课程项目_椒盐噪声中值滤波_项目报告.docx
│  ├─ 项目报告.pdf
│  └─ toc_pages.json
├─ tools/analysis/          # 图表与报告生成脚本（离线使用，不参与程序运行）
├─ build_zig.bat            # 当前机器使用的构建方式
├─ build_msvc.bat           # Visual Studio x64 Native Tools 构建
├─ CMakeLists.txt
├─ run_gui.bat
├─ run_benchmark_1000.bat
└─ profile_nsight.bat       # 需管理员权限
```

## 3. 运行环境

- Windows 10/11 x64
- NVIDIA GPU（本项目测试：RTX 4060 Laptop 8 GB，CC 8.9，驱动 617.42）
- CUDA Toolkit 13.x（需要 `nvrtc64_*.dll` 和 `nvcuda.dll`；`nvcc` 不是运行时必需）
- 无需安装 Qt/OpenCV；图像编解码使用 stb 单头文件
- 运行 `run_gui.bat` 会自动把 `%CUDA_PATH%\bin` 和 `%CUDA_PATH%\bin\x64` 加入 PATH

## 4. 快速开始

### 4.1 图形界面

双击 `run_gui.bat`，或：

```bat
bin\median_gui.exe
```

默认读取 `D:\项目\嵌入式\image\image`；如果不存在则读取 `samples`。批量页签显示“共 1000 张图片”，点击“开始批量处理”即可。

### 4.2 命令行基准

```bat
bin\median_bench.exe --input "D:\项目\嵌入式\image\image" ^
  --count 1000 --noise 0.05 --kernel 3 --mode fused ^
  --repeat 3 --warmup 1 --csv results\bench_1000.csv
```

常用参数：

| 参数 | 含义 |
|---|---|
| `--input DIR` | 图片文件夹 |
| `--count N` | 图片数量，默认 1000 |
| `--noise P` | 噪声比例，0–0.5，默认 0.05 |
| `--kernel K` | 3/5/7 |
| `--mode M` | `fused` / `separate` / `perimage` / `cpu` / `selftest` / `info` |
| `--repeat N` | 计时重复次数 |
| `--warmup N` | 是否先跑一整批 warm-up（默认 1） |
| `--save MODE` | `none` / `denoised` / `both` |
| `--out DIR` | 保存目录 |
| `--csv FILE` | 追加 CSV 结果 |

自检（CPU/GPU 逐字节对比）：

```bat
bin\median_bench.exe --mode selftest
```

Kernel 资源与理论占用率：

```bat
bin\median_bench.exe --mode info
```

CPU 基线：

```bat
bin\median_bench.exe --input samples --count 100 --noise 0.05 --kernel 3 ^
  --mode cpu --threads 8 --repeat 1
```

## 5. 主要结果

测试条件：1000 张 640×640 RGB，5% 椒盐噪声，3×3 中值滤波，5 次重复取中位数。

| 模式 | 端到端/ms | GPU 阶段/ms | 内核/ms | 吞吐/(张/s) |
|---|---:|---:|---:|---:|
| 逐张 per-image | 663.9 | 663.9 | — | 1506 |
| 批量分离 separate | 465.2 | 402.8 | 34.8 | 2150 |
| 批量融合 fused | 467.4 | 404.8 | 37.8 | 2140 |

- 去噪 PSNR：17.81 dB → 34.31 dB（平均），最小值 23.63 dB。
- 3×3 中值核：插入排序 407.0 ms → 19 比较器网络 34.8 ms（11.7×）。
- 端到端（分离模式）：834.9 ms → 465.2 ms（1.79×）。
- CPU（i9-14900HX，8 线程，100 张）808.6 ms vs GPU 55.4 ms，约 14.6×。
- GPU 采样：利用率中位数 94%、峰值 99%，功耗峰值 38.8 W，SM 2595 MHz。

## 6. 构建

### 6.1 Zig（当前机器的构建方式）

需要 Zig 0.15+，且 `zig` 在 PATH 中：

```bat
build_zig.bat
```

脚本会生成 `bin\median_gui.exe`、`bin\median_bench.exe`，并把 `kernels.cu` 复制到 `bin`。

### 6.2 Visual Studio

打开 “x64 Native Tools Command Prompt for VS”，确保 `cl.exe` 和 `CUDA_PATH` 可用：

```bat
build_msvc.bat
```

### 6.3 CMake

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

CMake 只编译 C++ 主机代码；CUDA kernel 由 NVRTC 在运行时编译，不需要 nvcc。

## 7. Nsight 说明（重要）

课程要求使用 Nsight Compute / Nsight Systems。本项目已编写 `profile_nsight.bat`，但当前机器账户不是管理员，驱动默认禁止访问 GPU 性能计数器，运行时出现：

```text
ERR_NVGPUCTRPERM: The user does not have permission to access NVIDIA GPU Performance Counters
```

Nsight Systems 的 CUDA 注入也受完整性级别限制，未能采集到 CUDA kernel 数据。为保证报告数据真实，正文中的硬件分析使用了：

1. CUDA Event 分阶段计时；
2. `cuFuncGetAttribute` / `cuOccupancyMaxActiveBlocksPerMultiprocessor` 的寄存器、shared、活跃块和理论占用率；
3. `nvidia-smi` 约 10 Hz 的 GPU 利用率/显存/功耗/频率采样。

要生成正式的 `.ncu-rep` / `.nsys-rep`：

1. 打开 **NVIDIA App → System → Advanced → Developer → Manage GPU Performance Counters**，选择 **Allow access to all users**（需要管理员权限）；
2. 以管理员身份打开命令提示符；
3. 运行：

```bat
profile_nsight.bat "D:\项目\嵌入式\image\image"
```

脚本会创建 ASCII 目录联接 `data_1000`（避免 Nsight 命令行解析中文路径失败），并输出到 `results\`。

## 8. 数据与提交说明

- 完整 1000 张原图未放入压缩包（约 92.7 MB），程序默认从 `D:\项目\嵌入式\image\image` 读取；压缩包内 `samples` 含 12 张示例图，可直接演示。
- 报告中的封面组号、任课教师、成员姓名/学号/签名/联系方式为占位符，请按实际情况填写。
- `tools/analysis` 下的 Python 脚本只用于离线生成图表和 DOCX，不参与图像处理，也不是程序运行依赖。
- 结果 CSV 为原始记录；报告中的数值为多次重复的中位数。

## 9. 参考

- CUDA C++ Programming Guide: https://docs.nvidia.com/cuda/cuda-c-programming-guide/
- NVRTC: https://docs.nvidia.com/cuda/nvrtc/
- Nsight Compute: https://docs.nvidia.com/nsight-compute/
- Nsight Systems: https://docs.nvidia.com/nsight-systems/
- stb: https://github.com/nothings/stb

# 结果数据说明

| 文件 | 内容 |
|---|---|
| `bench_mode_1000.csv` | 逐张 / 批量分离 / 批量融合，1000 张，5% 噪声，3×3，5 次重复 |
| `bench_noise.csv` | 1% / 5% / 10% / 20% 噪声，1000 张，3×3，融合，5 次重复 |
| `bench_kernel.csv` | 3×3 / 5×5 / 7×7，1000 张，5% 噪声，融合，3 次重复 |
| `bench_cpu.csv` | CPU 8 线程（100 张）与 GPU 分离模式（100 张）对比 |
| `gpu_samples.csv` | 1000 张连续批处理期间的 nvidia-smi 采样（约 10 Hz） |
| `kernel_info.txt` | GPU 属性与各 kernel 的寄存器 / shared / 理论占用率 |
| `figures/` | 报告使用的全部图表和 GUI 截图 |
| `sample_out/` | 12 张样例的加噪图与去噪图（JPEG 92） |
| `exp_insertion/` | 优化前插入排序 kernel 的副本与 5 次重复 CSV |

CSV 中的时间单位为 ms；`wall_ms` 是端到端时间，`gpu_total_ms` 是 CUDA Event
覆盖的 GPU 阶段（H2D + kernel(s) + stats + D2H），`kernel_ms` 只包含计算 kernel。
报告中取多次重复的中位数。

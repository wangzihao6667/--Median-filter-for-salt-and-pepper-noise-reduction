@echo off
setlocal
cd /d "%~dp0"
if defined CUDA_PATH (
  set "PATH=%CUDA_PATH%\bin;%CUDA_PATH%\bin\x64;%PATH%"
) else if exist "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4" (
  set "PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4\bin;C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4\bin\x64;%PATH%"
)
echo Running 1000-image benchmark (5%% salt-and-pepper, 3x3 median) ...
bin\median_bench.exe --count 1000 --noise 0.05 --kernel 3 --mode fused --repeat 3 --warmup 1 --csv results\bench_1000.csv %*
endlocal

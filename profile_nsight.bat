@echo off
setlocal
cd /d "%~dp0"
set "IMAGEDIR=D:\项目\嵌入式\image\image"
if not "%~1"=="" set "IMAGEDIR=%~1"

net session >nul 2>&1
if errorlevel 1 (
  echo [ERROR] Please run this script from an Administrator command prompt.
  echo.
  echo If Nsight Compute reports ERR_NVGPUCTRPERM:
  echo   NVIDIA App -^> System -^> Advanced -^> Developer
  echo   -^> Manage GPU Performance Counters -^> Allow access to all users
  pause
  exit /b 1
)

if not exist "results" mkdir "results"
if not exist "data_1000" (
  echo Creating ASCII junction data_1000 for Nsight command-line parsing ...
  mklink /J "data_1000" "%IMAGEDIR%"
)

set "NCU=C:\Program Files\NVIDIA Corporation\Nsight Compute 2026.3.1\ncu.bat"
set "NSYS=C:\Program Files\NVIDIA Corporation\Nsight Systems 2026.3.2\target-windows-x64\nsys.exe"
if not exist "%NCU%" set "NCU=ncu"
if not exist "%NSYS%" set "NSYS=nsys"

echo [1/3] Nsight Compute: fused3_kernel ...
call "%NCU%" --target-processes all --kernel-name regex:fused3_kernel --launch-count 1 ^
  --set full --force-overwrite -o "results\ncu_fused3" ^
  "bin\median_bench.exe" --input "data_1000" --count 200 --noise 0.05 --kernel 3 ^
  --mode fused --repeat 1 --warmup 0 --quiet
if errorlevel 1 echo [WARN] Nsight Compute failed. Check GPU performance counter permission.

echo [2/3] Nsight Systems: 1000-image timeline ...
call "%NSYS%" profile --trace=cuda --sample=none --cpuctxsw=none --force-overwrite=true ^
  -o "results\nsys_fused_1000" -- "bin\median_bench.exe" --input "data_1000" ^
  --count 1000 --noise 0.05 --kernel 3 --mode fused --repeat 1 --warmup 0 --quiet
if errorlevel 1 echo [WARN] Nsight Systems failed. Run as administrator.

echo [3/3] Summary ...
call "%NSYS%" stats --report cuda_gpu_kern_sum,cuda_api_sum "results\nsys_fused_1000.nsys-rep" ^
  > "results\nsys_summary.txt" 2>&1

echo Done. Reports are in results\ncu_fused3.ncu-rep and results\nsys_fused_1000.nsys-rep
endlocal

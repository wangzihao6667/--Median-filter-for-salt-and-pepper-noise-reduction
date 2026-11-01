@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

if not defined CUDA_PATH (
  if exist "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4" (
    set "CUDA_PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4"
  )
)
if not defined CUDA_PATH (
  echo [ERROR] CUDA_PATH is not set. Install CUDA Toolkit 13.x first.
  exit /b 1
)

if not exist "build" mkdir "build"
if not exist "bin" mkdir "bin"

if exist "%~dp0tools\zig\zig.exe" (
  set "ZIG=%~dp0tools\zig\zig.exe"
) else (
  set "ZIG=zig"
)
"%ZIG%" version >nul 2>&1
if errorlevel 1 (
  echo [ERROR] Zig was not found. Put zig.exe in tools\zig\ or add it to PATH.
  echo         Alternatively use build_msvc.bat from an x64 Native Tools prompt.
  exit /b 1
)

set "ZIG_GLOBAL_CACHE_DIR=%CD%\build\zig-cache-global"
set "ZIG_LOCAL_CACHE_DIR=%CD%\build\zig-cache-local"
set "INC=%CUDA_PATH%\include"
set "CFLAGS=-target x86_64-windows-gnu -std=c++17 -O2 -static -I "%INC%" -I "src" -I "third_party""
set "COMMON=src\common.cpp src\image_io.cpp src\cuda_backend.cpp"

echo [1/3] Building median_bench.exe ...
"%ZIG%" c++ %CFLAGS% src\main_cli.cpp src\benchmark.cpp %COMMON% -o bin\median_bench.exe -lole32 -luuid -lshell32
if errorlevel 1 exit /b 1

echo [2/3] Building median_gui.exe ...
"%ZIG%" c++ %CFLAGS% "-Wl,--subsystem,windows" src\main_gui.cpp src\gui.cpp %COMMON% -o bin\median_gui.exe -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32 -luser32
if errorlevel 1 exit /b 1

echo [3/3] Copying kernel source ...
copy /Y "src\kernels.cu" "bin\kernels.cu" >nul

echo.
echo Build OK:
echo   bin\median_gui.exe
echo   bin\median_bench.exe
echo   bin\kernels.cu
endlocal

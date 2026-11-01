@echo off
setlocal
cd /d "%~dp0"

if not defined CUDA_PATH (
  if exist "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4" (
    set "CUDA_PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4"
  )
)
if not defined CUDA_PATH (
  echo [ERROR] CUDA_PATH is not set.
  exit /b 1
)
where cl >nul 2>&1
if errorlevel 1 (
  echo [ERROR] cl.exe not found. Open an "x64 Native Tools Command Prompt for VS" first.
  exit /b 1
)

if not exist build mkdir build
if not exist bin mkdir bin

set INC=/I "%CUDA_PATH%\include" /I "src" /I "third_party"
set "COMMON=src\common.cpp src\image_io.cpp src\cuda_backend.cpp"
set "LIBS=user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib ole32.lib uuid.lib"

echo [1/3] Building median_bench.exe ...
cl /nologo /std:c++17 /O2 /EHsc /MD /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE %INC% ^
  /Fo:build\ /Fe:bin\median_bench.exe src\main_cli.cpp src\benchmark.cpp %COMMON% ^
  /link /SUBSYSTEM:CONSOLE %LIBS%
if errorlevel 1 exit /b 1

echo [2/3] Building median_gui.exe ...
cl /nologo /std:c++17 /O2 /EHsc /MD /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE %INC% ^
  /Fo:build\ /Fe:bin\median_gui.exe src\main_gui.cpp src\gui.cpp %COMMON% ^
  /link /SUBSYSTEM:WINDOWS %LIBS%
if errorlevel 1 exit /b 1

echo [3/3] Copying kernel source ...
copy /Y "src\kernels.cu" "bin\kernels.cu" >nul

echo.
echo Build OK.
endlocal

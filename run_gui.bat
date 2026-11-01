@echo off
setlocal
cd /d "%~dp0"
if defined CUDA_PATH (
  set "PATH=%CUDA_PATH%\bin;%CUDA_PATH%\bin\x64;%PATH%"
) else if exist "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4" (
  set "PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4\bin;C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4\bin\x64;%PATH%"
)
start "" "%~dp0bin\median_gui.exe" %*
endlocal

@echo off
cd /d "%~dp0"
set "PATH=%PATH%;C:\Users\gosee\Documents\Codex\tools\Git\cmd;C:\Users\gosee\Documents\Codex\tools\cmake-4.3.3-windows-x86_64\bin;C:\Users\gosee\Documents\Codex\tools\ninja;C:\Users\gosee\Documents\Codex\tools\llvm-mingw-20260616-ucrt-x86_64\bin"
cmake --build build\gpu --target lg_duel_client
if errorlevel 1 (
  echo Build failed.
  pause
  exit /b 1
)
set LG_DUEL_RENDER_BACKEND=gpu
build\gpu\lg_duel_client.exe 127.0.0.1 27960

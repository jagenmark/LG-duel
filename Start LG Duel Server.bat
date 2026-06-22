@echo off
cd /d "%~dp0"
set "PATH=%PATH%;C:\Users\gosee\Documents\Codex\tools\Git\cmd;C:\Users\gosee\Documents\Codex\tools\cmake-4.3.3-windows-x86_64\bin;C:\Users\gosee\Documents\Codex\tools\ninja;C:\Users\gosee\Documents\Codex\tools\llvm-mingw-20260616-ucrt-x86_64\bin"
cmake -S . -B build\gpu -G Ninja -DLG_DUEL_REQUIRE_SDL3=ON -DLG_DUEL_FETCH_SDL3=ON
if errorlevel 1 (
  echo Configure failed.
  pause
  exit /b 1
)
cmake --build build\gpu --target lg_duel_server
if errorlevel 1 (
  echo Build failed.
  pause
  exit /b 1
)
build\gpu\lg_duel_server.exe 27960

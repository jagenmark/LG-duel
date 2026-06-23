@echo off
cd /d "%~dp0"
set "PATH=%PATH%;C:\Users\gosee\Documents\Codex\tools\Git\cmd;C:\Users\gosee\Documents\Codex\tools\cmake-4.3.3-windows-x86_64\bin;C:\Users\gosee\Documents\Codex\tools\ninja;C:\Users\gosee\Documents\Codex\tools\llvm-mingw-20260616-ucrt-x86_64\bin"
set "CC=C:\Users\gosee\Documents\Codex\tools\llvm-mingw-20260616-ucrt-x86_64\bin\clang.exe"
set "CXX=C:\Users\gosee\Documents\Codex\tools\llvm-mingw-20260616-ucrt-x86_64\bin\clang++.exe"
if not exist "build\default\build.ninja" (
  cmake --preset default
  if errorlevel 1 (
    echo Configure failed.
    pause
    exit /b 1
  )
)
cmake --build --preset default --target lg_duel_server
if errorlevel 1 (
  echo Build failed.
  pause
  exit /b 1
)
build\default\lg_duel_server.exe 27960

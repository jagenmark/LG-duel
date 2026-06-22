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
cmake --build --preset default --target lg_duel_client
if errorlevel 1 (
  echo Build failed.
  pause
  exit /b 1
)
if exist "build\default\_deps\sdl3-local-build\SDL3.dll" (
  copy /Y "build\default\_deps\sdl3-local-build\SDL3.dll" "build\default\SDL3.dll" >nul
)
if not exist "build\default\SDL3.dll" (
  echo SDL3.dll was not found beside lg_duel_client.exe.
  pause
  exit /b 1
)
set LG_DUEL_RENDER_BACKEND=gpu
build\default\lg_duel_client.exe 127.0.0.1 27960

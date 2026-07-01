@echo off
cd /d "%~dp0"
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
if errorlevel 1 (
  echo Server exited with code %ERRORLEVEL%.
  pause
  exit /b %ERRORLEVEL%
)

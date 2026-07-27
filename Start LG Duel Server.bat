@echo off
cd /d "%~dp0"
if not exist "build\default\build.ninja" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\bootstrap-windows-client.ps1"
  if errorlevel 1 (
    echo Configure failed.
    pause
    exit /b 1
  )
)
cmake --build "build\default" --target lg_duel_server --parallel
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

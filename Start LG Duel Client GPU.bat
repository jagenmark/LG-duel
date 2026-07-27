@echo off
setlocal
cd /d "%~dp0"
tasklist /FI "IMAGENAME eq lg_duel_client.exe" | find /I "lg_duel_client.exe" >nul
set "CLIENT_ALREADY_RUNNING=%ERRORLEVEL%"

if "%CLIENT_ALREADY_RUNNING%"=="0" (
  echo Existing LG Duel client detected; skipping rebuild so the running exe stays usable.
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\bootstrap-windows-client.ps1" -RepairIfNeeded
  if errorlevel 1 (
    echo SDL client bootstrap failed.
    pause
    exit /b 1
  )
)
if exist "build\default\_deps\sdl3-local-build\SDL3.dll" (
  copy /Y "build\default\_deps\sdl3-local-build\SDL3.dll" "build\default\SDL3.dll" >nul
)
if not exist "build\default\SDL3.dll" (
  echo SDL3.dll was not found beside lg_duel_client.exe.
  pause
  exit /b 1
)
echo Starting verified Vulkan client...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\lg-dev.ps1" start -ServerPort 27960 -ControlPort 27961 -Renderer gpu -ExternalServer
if errorlevel 1 (
  echo.
  echo Verified GPU startup failed. The client was not left running in fallback mode.
  echo Diagnostics: %~dp0build\dev-control\client.stderr.log
  pause
  exit /b 1
)

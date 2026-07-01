@echo off
cd /d "%~dp0"
tasklist /FI "IMAGENAME eq lg_duel_client.exe" | find /I "lg_duel_client.exe" >nul
set "CLIENT_ALREADY_RUNNING=%ERRORLEVEL%"

if not exist "build\default\build.ninja" (
  cmake --preset default
  if errorlevel 1 (
    echo Configure failed.
    pause
    exit /b 1
  )
)
if "%CLIENT_ALREADY_RUNNING%"=="0" (
  echo Existing LG Duel client detected; skipping rebuild so the running exe stays usable.
) else (
  cmake --build --preset default --target lg_duel_client
  if errorlevel 1 (
    echo Build failed.
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
set LG_DUEL_RENDER_BACKEND=gpu
build\default\lg_duel_client.exe 127.0.0.1 27960

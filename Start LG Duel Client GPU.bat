@echo off
cd /d "%~dp0"
set "PATH=%USERPROFILE%\Documents\ninja-win;C:\rtools45\x86_64-w64-mingw32.static.posix\bin;C:\rtools45\usr\bin;%PATH%"
set "BUILD_PRESET=sdl3"
set "BUILD_DIR=build\sdl3"
set "SDL3_ROOT=%USERPROFILE%\Documents\SDL3-devel-3.4.12-mingw\SDL3-3.4.12\x86_64-w64-mingw32"
tasklist /FI "IMAGENAME eq lg_duel_client.exe" | find /I "lg_duel_client.exe" >nul
set "CLIENT_ALREADY_RUNNING=%ERRORLEVEL%"

if not exist "%BUILD_DIR%\build.ninja" (
  cmake --preset %BUILD_PRESET%
  if errorlevel 1 (
    echo Configure failed.
    pause
    exit /b 1
  )
)
if "%CLIENT_ALREADY_RUNNING%"=="0" (
  echo Existing LG Duel client detected; skipping rebuild so the running exe stays usable.
) else (
  cmake --build --preset %BUILD_PRESET% --target lg_duel_client
  if errorlevel 1 (
    echo Build failed.
    pause
    exit /b 1
  )
)
if not exist "%BUILD_DIR%\SDL3.dll" if exist "%SDL3_ROOT%\bin\SDL3.dll" (
  copy /Y "%SDL3_ROOT%\bin\SDL3.dll" "%BUILD_DIR%\SDL3.dll" >nul
)
if not exist "%BUILD_DIR%\SDL3.dll" (
  echo SDL3.dll was not found beside lg_duel_client.exe in %BUILD_DIR%.
  echo Expected SDL3 at %SDL3_ROOT%\bin\SDL3.dll.
  pause
  exit /b 1
)
set LG_DUEL_RENDER_BACKEND=gpu
"%BUILD_DIR%\lg_duel_client.exe" 127.0.0.1 27960

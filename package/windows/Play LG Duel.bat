@echo off
setlocal
cd /d "%~dp0"

if not exist "server-address.txt" (
  echo server-address.txt is missing from the package.
  pause
  exit /b 1
)

for /f "usebackq tokens=1,2 delims=:" %%A in ("server-address.txt") do (
  set "SERVER_HOST=%%A"
  set "SERVER_PORT=%%B"
)

if not defined SERVER_HOST (
  echo server-address.txt does not contain a server host.
  pause
  exit /b 1
)
if not defined SERVER_PORT (
  echo server-address.txt does not contain a server port.
  pause
  exit /b 1
)

echo Starting LG Duel...
echo Server: %SERVER_HOST%:%SERVER_PORT%
echo.

set "LG_DUEL_RENDER_BACKEND=gpu"
"%~dp0lg_duel_client.exe" "%SERVER_HOST%" "%SERVER_PORT%"
if errorlevel 1 (
  echo.
  echo LG Duel closed with an error.
  echo Take a screenshot of this window and send it to the host.
  pause
)

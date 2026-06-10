@echo off
setlocal
cd /d "%~dp0"

set "SERVER_HOST=213.66.106.51"
set "SERVER_PORT=27960"

if exist "server-address.txt" (
  for /f "usebackq tokens=1,2 delims=:" %%A in ("server-address.txt") do (
    set "SERVER_HOST=%%A"
    set "SERVER_PORT=%%B"
  )
)

echo Starting LG Duel...
echo Server: %SERVER_HOST%:%SERVER_PORT%
echo.

"%~dp0lg_duel_client.exe" "%SERVER_HOST%" "%SERVER_PORT%"
if errorlevel 1 (
  echo.
  echo LG Duel closed with an error.
  echo Take a screenshot of this window and send it to the host.
  pause
)

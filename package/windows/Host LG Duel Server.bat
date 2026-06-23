@echo off
setlocal
cd /d "%~dp0"

set "SERVER_PORT=27960"
if exist "server-address.txt" (
  for /f "usebackq tokens=1,2 delims=:" %%A in ("server-address.txt") do (
    if not "%%B" == "" set "SERVER_PORT=%%B"
  )
)

if not "%~1" == "" set "SERVER_PORT=%~1"

echo Starting LG Duel server on UDP port %SERVER_PORT%...
echo Share your public IP address and port %SERVER_PORT% with players.
echo.

"%~dp0lg_duel_server.exe" "%SERVER_PORT%"
if errorlevel 1 (
  echo.
  echo LG Duel server closed with an error.
  pause
)

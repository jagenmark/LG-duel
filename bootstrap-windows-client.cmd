@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\bootstrap-windows-client.ps1"
exit /b %ERRORLEVEL%

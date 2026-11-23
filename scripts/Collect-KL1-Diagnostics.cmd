@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Collect-KL1-Diagnostics.ps1" %*
exit /b %errorlevel%

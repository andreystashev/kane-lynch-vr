@echo off
setlocal
cd /d "%~dp0"
if "%~1"=="" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-KL1-Recorder.ps1" -ShowConsole
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-KL1-Recorder.ps1" %*
)
set "RECORD_RESULT=%errorlevel%"
if not "%RECORD_RESULT%"=="0" pause
exit /b %RECORD_RESULT%

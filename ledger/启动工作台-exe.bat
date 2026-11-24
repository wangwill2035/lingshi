@echo off
chcp 936 >nul
set "CHROME=C:\Program Files\Google\Chrome\Application\chrome.exe"

echo [1/3] Closing old service...
for /f "tokens=5" %%a in ('netstat -aon ^| findstr ":8000" ^| findstr "LISTENING"') do taskkill /f /pid %%a >nul 2>&1
ping 127.0.0.1 -n 2 >nul

echo [2/3] Starting workbench-server.exe...
cd /d "%~dp0"
start "" "%~dp0workbench-server.exe"
ping 127.0.0.1 -n 6 >nul

echo [3/3] Opening browser...
if exist "%CHROME%" (start "" "%CHROME%" --new-window "http://localhost:8000") else (start "" "http://localhost:8000")
ping 127.0.0.1 -n 3 >nul
exit


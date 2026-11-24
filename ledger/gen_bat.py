# -*- coding: utf-8 -*-
import os

PYW = r"C:\Users\wangwill\AppData\Local\Programs\Python\Python312\pythonw.exe"
PROJ = r"C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰"
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"

# ping 延时替代 timeout（避免输入重定向报错）
def wait(sec):
    return f"ping 127.0.0.1 -n {sec+1} >nul"

desktop_bat = f"""@echo off
chcp 936 >nul
set "PYW={PYW}"
set "PROJ={PROJ}"
set "CHROME={CHROME}"

echo [1/3] Closing old service...
for /f "tokens=5" %%a in ('netstat -aon ^| findstr ":8000" ^| findstr "LISTENING"') do taskkill /f /pid %%a >nul 2>&1
{wait(1)}

echo [2/3] Starting workbench...
cd /d "%PROJ%"
start "" "%PYW%" "%PROJ%\\server.py"
{wait(3)}

echo [3/3] Opening browser...
if exist "%CHROME%" (start "" "%CHROME%" --new-window "http://localhost:8000") else (start "" "http://localhost:8000")
{wait(2)}
exit
"""

proj_bat = f"""@echo off
chcp 936 >nul
set "PYW={PYW}"
set "CHROME={CHROME}"

echo [1/3] Closing old service...
for /f "tokens=5" %%a in ('netstat -aon ^| findstr ":8000" ^| findstr "LISTENING"') do taskkill /f /pid %%a >nul 2>&1
{wait(1)}

echo [2/3] Starting workbench...
cd /d "%~dp0"
start "" "%PYW%" "%~dp0server.py"
{wait(3)}

echo [3/3] Opening browser...
if exist "%CHROME%" (start "" "%CHROME%" --new-window "http://localhost:8000") else (start "" "http://localhost:8000")
{wait(2)}
exit
"""

restart_bat = f"""@echo off
chcp 936 >nul
set "PYW={PYW}"
set "CHROME={CHROME}"

echo Restarting workbench service...
for /f "tokens=5" %%a in ('netstat -aon ^| findstr ":8000" ^| findstr "LISTENING"') do taskkill /f /pid %%a >nul 2>&1
{wait(1)}

cd /d "%~dp0"
start "" "%PYW%" "%~dp0server.py"
{wait(3)}

if exist "%CHROME%" (start "" "%CHROME%" --new-window "http://localhost:8000") else (start "" "http://localhost:8000")
echo Done.
{wait(3)}
"""

targets = [
    (r"C:\Users\wangwill\Desktop\启动灵饰工作台.bat", desktop_bat),
    (os.path.join(PROJ, "启动工作台.bat"), proj_bat),
    (os.path.join(PROJ, "重启服务.bat"), restart_bat),
    (os.path.join(PROJ, "打开工作台.bat"), proj_bat),
]

for path, content in targets:
    with open(path, "w", encoding="gbk", newline="\r\n") as f:
        f.write(content)
    with open(path, "r", encoding="gbk") as f:
        back = f.read()
    ok = "梦幻西游灵饰" in back or "%~dp0" in back
    print(f"OK  {os.path.basename(path)}  校验={ok}")

print("全部写入完成")

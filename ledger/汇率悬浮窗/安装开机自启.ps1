# 安装 / 卸载「梦幻汇率悬浮窗」开机自启（当前用户级，不需要管理员）
# 用法：powershell -ExecutionPolicy Bypass -File 安装开机自启.ps1        # 安装
#       powershell -ExecutionPolicy Bypass -File 安装开机自启.ps1 -Remove # 卸载

param([switch]$Remove)

$startup = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Startup"
$lnk = Join-Path $startup "梦幻汇率悬浮窗.lnk"
$vbs = Join-Path $env:APPDATA "MHXY-RateWidget\start.vbs"

if ($Remove) {
    if (Test-Path -LiteralPath $lnk) { Remove-Item -LiteralPath $lnk -Force; "已删除启动项：$lnk" }
    else { "启动项本来就不存在：$lnk" }
    return
}

if (-not (Test-Path -LiteralPath $vbs)) { throw "找不到启动器：$vbs，请先确认悬浮窗已安装" }

$sh = New-Object -ComObject WScript.Shell
$s = $sh.CreateShortcut($lnk)
$s.TargetPath = "C:\Windows\System32\wscript.exe"
$s.Arguments = "`"$vbs`""
$s.WorkingDirectory = (Split-Path -Parent $vbs)
$s.Description = "梦幻西游汇率悬浮窗（白云山·广东4区，每6小时刷新）"
$s.Save()
"已安装开机自启：$lnk"

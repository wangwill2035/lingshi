# 梦幻西游汇率悬浮窗 —— 白云山（广东4区）
# 固定时刻刷新：00:00 / 08:00 / 12:00 / 18:00 / 22:00
# 显示：实时换算价 + 近 7 日走势曲线 + 小刷新按钮
# 外观：白色玻璃，常态 25% 不透明（淡影），鼠标移到窗口上完全不透明（100%）
# 底部：在售快照概要（款数 / 件数 / 金币合计 / 折人民币），读 stock.json
# 数据源：阿鱼工具箱公开接口（第三方市场快照，非网易官方汇率）

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch { }

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class MHXYHover {
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int idx);
    [DllImport("user32.dll")] public static extern int SetWindowLong(IntPtr h, int idx, int val);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X; public int Y; }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
}
"@

# ---------- 单实例 ----------
$script:mutex = New-Object System.Threading.Mutex($false, "Global\MHXYRateWidget")
if (-not $script:mutex.WaitOne(0)) { exit }

# ---------- 路径 ----------
$script:CfgDir = Join-Path $env:APPDATA "MHXY-RateWidget"
$script:StatePath = Join-Path $script:CfgDir "state.json"
$script:HistPath = Join-Path $script:CfgDir "history.jsonl"
$script:DailyCachePath = Join-Path $script:CfgDir "daily7.json"
$script:StockPath = Join-Path $script:CfgDir "stock.json"
$script:StkPath = Join-Path $script:CfgDir "storage.json"
$script:PayPath = Join-Path $script:CfgDir "cash.json"
$script:TodayPath = Join-Path $script:CfgDir "today.json"
$script:YestPath = Join-Path $script:CfgDir "yesterday.json"
New-Item -ItemType Directory -Force -Path $script:CfgDir | Out-Null

# ---------- 配置 ----------
$script:Region = "广东4区"
$script:Server = "白云山"
$script:Group = "over3"
$script:SourceId = 85
$script:Slots = @(0, 8, 12, 18, 22)      # 每天刷新时刻
$script:UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36"

# ---------- 状态 ----------
$script:Price = $null
$script:Chg1d = $null
$script:Chg7d = $null
$script:CollectedAt = $null
$script:Daily = @()
$script:LastErr = $null
$script:NextAt = $null
$script:dragX = $null
$script:busy = $false
$script:NormalW = 400
$script:NormalH = 380
$script:SepY2 = 328
$script:MiniW = 28
$script:MiniH = 150
$script:Mini = $false
$script:SnapSide = $null
$script:RestoreMini = $false
$script:AllCtrls = @()

# ---------- 颜色（浅色玻璃） ----------
$script:C_BG = "#ffffff"
$script:C_GOLD = "#C4811C"
$script:C_BLUE = "#2F73D9"
$script:C_TXT = "#1F2430"
$script:C_DIM = "#5A6270"
$script:C_FAINT = "#8A92A0"
$script:C_GRID = "#E6EAF0"
$script:C_ERR = "#C0392B"
$script:C_GREEN = "#2FC258"      # 收起胶囊底色
$script:C_WHITE = "#FFFFFF"

function Col([string]$hex) { return [System.Drawing.ColorTranslator]::FromHtml($hex) }

# ---------- 时间 ----------
function Get-NextSlot {
    $now = Get-Date
    $today = $now.Date
    foreach ($h in $script:Slots) {
        $t = $today.AddHours($h)
        if ($t -gt $now) { return $t }
    }
    return $today.AddDays(1).AddHours($script:Slots[0])
}

function Format-Slot([datetime]$t) {
    $now = Get-Date
    if ($t.Date -eq $now.Date) { return $t.ToString("HH:mm") }
    if ($t.Date -eq $now.Date.AddDays(1)) { return "明日 " + $t.ToString("HH:mm") }
    return $t.ToString("MM-dd HH:mm")
}

# ---------- 网络 ----------
function Invoke-Api([string]$uri) {
    return Invoke-RestMethod -Uri $uri -TimeoutSec 25 -Headers @{ "User-Agent" = $script:UA; "Accept" = "application/json" }
}

function Get-LatestQuote {
    $uri = "https://mh.zyungame.com/api/prices/latest?page=1&page_size=500"
    $lastErr = ""
    for ($i = 1; $i -le 3; $i++) {
        try {
            $r = Invoke-Api $uri
            $q = $r.quotes | Where-Object { $_.server_name -eq $script:Server -and $_.region -eq $script:Region } | Select-Object -First 1
            if (-not $q) { throw "返回数据里没有 $($script:Region)/$($script:Server)" }
            if ($r.snapshot -and $r.snapshot.collected_at) {
                $q | Add-Member -NotePropertyName collected_at -NotePropertyValue $r.snapshot.collected_at -Force
            }
            return $q
        } catch {
            $lastErr = $_.Exception.Message
            Start-Sleep -Seconds (2 * $i)
        }
    }
    throw $lastErr
}

function Get-DailyHistory {
    $uri = "https://mh.zyungame.com/api/prices/history?group=$($script:Group)&source_id=$($script:SourceId)"
    $r = Invoke-Api $uri
    $pts = @()
    foreach ($p in $r.points) {
        if ($p.price) { $pts += [pscustomobject]@{ day = [string]$p.period_start; price = [double]$p.price } }
    }
    return ($pts | Sort-Object day)
}

function Append-History($price, $c1, $c7, $collected) {
    $line = [ordered]@{
        ts = (Get-Date).ToString("s")
        server = "$($script:Region)/$($script:Server)"
        price = $price
        rmb_per_wan = [math]::Round(($price / 3000.0), 4)
        wan_per_rmb = [math]::Round((3000.0 / $price), 2)
        change_1d = $c1
        change_7d = $c7
        collected_at = $collected
        ok = $true
    }
    [IO.File]::AppendAllText($script:HistPath, (($line | ConvertTo-Json -Compress) + "`n"), (New-Object Text.UTF8Encoding $false))
}

function Save-State {
    $o = [ordered]@{
        server = $script:Server; region = $script:Region
        price = $script:Price; collected_at = $script:CollectedAt
        next_slot = if ($script:NextAt) { $script:NextAt.ToString("s") } else { $null }
        x = $script:Form.Left; y = $script:Form.Top
        mini = $script:Mini
        snap = $script:SnapSide
        opacity = $script:Form.Opacity
    }
    [IO.File]::WriteAllText($script:StatePath, ($o | ConvertTo-Json), (New-Object Text.UTF8Encoding $false))
}

function Read-State {
    if (Test-Path -LiteralPath $script:StatePath) {
        try {
            $s = Get-Content -LiteralPath $script:StatePath -Raw -Encoding utf8 | ConvertFrom-Json
            if ($s.price) { $script:Price = [double]$s.price }
            if ($s.collected_at) { $script:CollectedAt = $s.collected_at }
            if ($s.x -ne $null -and $s.y -ne $null) { $script:Pos = @([int]$s.x, [int]$s.y) }
            if ($s.snap) { $script:SnapSide = [string]$s.snap }
            if ($s.mini) { $script:RestoreMini = $true }
        } catch { }
    }
    if (Test-Path -LiteralPath $script:DailyCachePath) {
        try {
            $d = Get-Content -LiteralPath $script:DailyCachePath -Raw -Encoding utf8 | ConvertFrom-Json
            $script:Daily = @($d | ForEach-Object { [pscustomobject]@{ day = [string]$_.day; price = [double]$_.price } })
        } catch { }
    }
}

# ---------- 绘图 ----------
function Draw-PriceChart([int]$w, [int]$h, $pts, [string]$lineHex) {
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
    $g.Clear((Col $script:C_BG))

    if (-not $pts -or $pts.Count -eq 0) {
        $f = New-Object System.Drawing.Font("Microsoft YaHei UI", 8)
        $g.DrawString("暂无数据", $f, (New-Object System.Drawing.SolidBrush((Col $script:C_FAINT))), 10, [float]($h / 2 - 10))
        $f.Dispose(); $g.Dispose()
        return $bmp
    }

    $padL = 36; $padR = 8; $padT = 8; $padB = 18
    $plotW = $w - $padL - $padR
    $plotH = $h - $padT - $padB

    $vals = @($pts | ForEach-Object { $_.price })
    $mn = ($vals | Measure-Object -Minimum).Minimum
    $mx = ($vals | Measure-Object -Maximum).Maximum
    if ($mx -eq $mn) { $mn = $mn - 1; $mx = $mx + 1 }
    $span = $mx - $mn
    $mn = $mn - $span * 0.15
    $mx = $mx + $span * 0.15

    $fY = New-Object System.Drawing.Font("Microsoft YaHei UI", 6.5)
    $fX = New-Object System.Drawing.Font("Microsoft YaHei UI", 6.5)
    $brText = New-Object System.Drawing.SolidBrush((Col $script:C_DIM))
    $brGold = New-Object System.Drawing.SolidBrush((Col $script:C_GOLD))
    $penGrid = New-Object System.Drawing.Pen((Col $script:C_GRID), 1)

    foreach ($frac in @(0.0, 0.5, 1.0)) {
        $y = $padT + $plotH * $frac
        $g.DrawLine($penGrid, [float]$padL, [float]$y, [float]($padL + $plotW), [float]$y)
        $v = $mx - ($mx - $mn) * $frac
        $g.DrawString(("{0:N1}" -f $v), $fY, $brText, 2, [float]($y - 7))
    }

    $yAt = { param($v) return $padT + $plotH * (1.0 - (($v - $mn) / ($mx - $mn))) }

    $n = $pts.Count
    $xs = @(); $ys = @()
    for ($i = 0; $i -lt $n; $i++) {
        if ($n -le 1) { $xs += [double]($padL + $plotW / 2) }
        else { $xs += [double]($padL + $plotW * ($i / ($n - 1.0))) }
        $ys += [double](& $yAt $pts[$i].price)
    }

    if ($n -ge 2) {
        $penLine = New-Object System.Drawing.Pen((Col $lineHex), 1.6)
        for ($i = 1; $i -lt $n; $i++) { $g.DrawLine($penLine, [float]$xs[$i - 1], [float]$ys[$i - 1], [float]$xs[$i], [float]$ys[$i]) }
        $penLine.Dispose()
        $brushFill = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(30, (Col $lineHex)))
        $poly = New-Object System.Drawing.Drawing2D.GraphicsPath
        $poly.AddLine([float]$xs[0], [float]($padT + $plotH), [float]$xs[0], [float]$ys[0])
        for ($i = 1; $i -lt $n; $i++) { $poly.AddLine([float]$xs[$i - 1], [float]$ys[$i - 1], [float]$xs[$i], [float]$ys[$i]) }
        $poly.AddLine([float]$xs[$n - 1], [float]$ys[$n - 1], [float]$xs[$n - 1], [float]($padT + $plotH))
        $g.FillPath($brushFill, $poly)
        $poly.Dispose(); $brushFill.Dispose()
    }

    $brushDot = New-Object System.Drawing.SolidBrush((Col $lineHex))
    for ($i = 0; $i -lt $n; $i++) {
        $r = 2.4; $b = $brushDot
        if ($i -eq $n - 1) { $r = 3.2; $b = $brGold }
        $g.FillEllipse($b, [float]($xs[$i] - $r), [float]($ys[$i] - $r), [float]($r * 2), [float]($r * 2))
    }
    $brushDot.Dispose()

    # 末点数值
    $labLast = "现 {0:N1}" -f $pts[$n - 1].price
    $szL = $g.MeasureString($labLast, $fX)
    $lx = [double]$xs[$n - 1] - $szL.Width - 4
    if ($lx -lt $padL) { $lx = [double]($padL + 2) }
    $ly = [double]$ys[$n - 1] - 12
    if ($ly -lt $padT) { $ly = [double]$ys[$n - 1] + 4 }
    $g.DrawString($labLast, $fX, $brGold, [float]$lx, [float]$ly)

    for ($i = 0; $i -lt $n; $i++) {
        $lab = [string]$pts[$i].day
        try { $lab = ([datetime]::ParseExact([string]$pts[$i].day, "yyyy-MM-dd", [Globalization.CultureInfo]::InvariantCulture)).ToString("MM-dd") } catch { }
        $sz = $g.MeasureString($lab, $fX)
        $x = $xs[$i] - $sz.Width / 2
        if ($i -eq 0) { $x = $padL }
        if ($i -eq $n - 1) { $x = $xs[$i] - $sz.Width }
        $g.DrawString($lab, $fX, $brText, [float]$x, [float]($h - $padB + 2))
    }

    $fY.Dispose(); $fX.Dispose(); $brText.Dispose(); $brGold.Dispose(); $penGrid.Dispose()
    $g.Dispose()
    return $bmp
}

function Redraw {
    $d7 = @($script:Daily | Select-Object -Last 7)
    $bmp7 = Draw-PriceChart 376 112 $d7 $script:C_BLUE
    $script:Pb7.Image = $bmp7
}

function Render {
    if ($script:Price -eq $null) {
        $script:LblBig.Text = "等待数据…"
        $script:LblSub.Text = "尚未取得过行情"
        $script:LblMeta.Text = ""
    } else {
        $wan = 3000.0 / $script:Price
        $perWan = $script:Price / 3000.0
        $script:LblBig.Text = "1 元 ≈ {0:N4} 万" -f $wan
        $script:LblSub.Text = "1 万梦幻币 ≈ ¥{0:N4}" -f $perWan
        $arrow = "—"
        if ($script:Chg1d -ne $null) {
            $c1 = [double]$script:Chg1d
            if ($c1 -gt 0) { $arrow = "↑" } elseif ($c1 -lt 0) { $arrow = "↓" }
        }
        $chg7 = ""
        if ($script:Chg7d -ne $null) { $chg7 = " · 7日 {0:+0.0;-0.0}%" -f [double]$script:Chg7d }
        $script:LblMeta.Text = ("金价 ¥{0:N2}/3000万  {1}{2}{3}" -f $script:Price, $arrow, ("{0:+0.00;-0.00}" -f [double]$script:Chg1d), $chg7)
    }

    $col = ""
    if ($script:CollectedAt) {
        try {
            $t = [datetime]::Parse($script:CollectedAt, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AssumeUniversal -bor [Globalization.DateTimeStyles]::AdjustToUniversal)
            $col = $t.ToLocalTime().ToString("MM-dd HH:mm")
        } catch { $col = $script:CollectedAt }
    }
    $nxt = if ($script:NextAt) { Format-Slot $script:NextAt } else { "—" }
    if ($script:LastErr) {
        $script:LblTime.Text = "采集 $col · 取数异常，保留上次 · 下次 $nxt"
        $script:LblTime.ForeColor = Col $script:C_ERR
    } else {
        $script:LblTime.Text = "采集 $col · 下次 $nxt 刷新"
        $script:LblTime.ForeColor = Col $script:C_FAINT
    }

    if ($script:Daily.Count -ge 2) {
        $first = $script:Daily[0]
        $last = $script:Daily[$script:Daily.Count - 1]
        $pct7 = if ($first.price -gt 0) { ($last.price - $first.price) / $first.price * 100 } else { 0 }
        $script:LblRecent.Text = ("近 7 日走势（{0:N1} → {1:N1}，{2:+0.0;-0.0}%）" -f $first.price, $last.price, $pct7)
    }
    Update-MiniText
    Render-Stock
    Render-Today
    $script:PayLines = Render-Pay
}

# ---------- 在售快照（面板底部概要） ----------
# 把在售明细按等级排成一行一条：60级：香木镯 6 + 芝兰佩 4
# 同一等级内顺序固定为 戒指 → 耳饰 → 手镯 → 佩饰，缺哪类就跳过
function Build-Inv-Lines($items, $width, $font) {
    $order = @("ring", "ear", "brace", "pend")
    $names = @{ ring = "戒指"; ear = "耳饰"; brace = "手镯"; pend = "佩饰" }
    $byLevel = @{}
    foreach ($it in $items) {
        $q = [double]$it.qty
        if ($q -le 0) { continue }
        $code = [string]$it.code
        if ($code -notmatch '^(ring|ear|brace|pend)_(\d+)$') { continue }
        $t = $matches[1]; $lv = [int]$matches[2]
        $sold = 0
        if ($it.PSObject.Properties.Name -contains 'sold') { $sold = [double]$it.sold }
        if (-not $byLevel.ContainsKey($lv)) { $byLevel[$lv] = @() }
        $byLevel[$lv] += [pscustomobject]@{ type = $t; name = [string]$it.name; qty = $q; sold = $sold }
    }
    if ($byLevel.Count -eq 0) { return @() }

    $flags = [System.Windows.Forms.TextFormatFlags]::NoPrefix
    $measure = {
        param($s)
        return ([System.Windows.Forms.TextRenderer]::MeasureText($s, $font).Width)
    }

    $lines = @()
    foreach ($lv in ($byLevel.Keys | Sort-Object)) {
        $arr = $byLevel[$lv]
        $mainParts = @(); $soldParts = @()
        foreach ($t in $order) {
            foreach ($x in $arr) {
                if ($x.type -eq $t) {
                    $mainParts += ("{0} {1:N0}" -f $x.name, $x.qty)
                    if ($x.sold -gt 0) { $soldParts += ("{0} {1:N0}" -f $x.name, $x.sold) }
                }
            }
        }
        if ($mainParts.Count -eq 0) { continue }
        $prefix = "{0}级：" -f $lv
        $indent = (" " * ($prefix.Length - 3)) + "    "   # 折行缩进，视觉上对齐到冒号后
        $cur = $prefix
        $sep = ""
        foreach ($p in $mainParts) {
            $cand = $cur + $sep + $p
            if ((& $measure $cand) -gt $width -and $cur -ne $prefix) {
                $lines += [pscustomobject]@{ text = $cur; sold = $false }
                $cur = $indent + $p
                $sep = ""
            } else {
                $cur = $cand
                $sep = " + "
            }
        }
        $lines += [pscustomobject]@{ text = $cur; sold = $false }
        if ($soldParts.Count -gt 0) {
            $scur = "已售：  "
            $sep = ""
            foreach ($p in $soldParts) {
                $cand = $scur + $sep + $p
                if ((& $measure $cand) -gt $width -and $scur -ne "已售：  ") {
                    $lines += [pscustomobject]@{ text = $scur; sold = $true }
                    $scur = $indent + $p
                    $sep = ""
                } else {
                    $scur = $cand
                    $sep = " + "
                }
            }
            $lines += [pscustomobject]@{ text = $scur; sold = $true }
        }
    }
    return $lines
}

function Test-HasProv {
    if ($script:DayView -eq "yesterday") { return $false }
    if (-not (Test-Path -LiteralPath $script:TodayPath)) { return $false }
    try {
        $t = Get-Content -LiteralPath $script:TodayPath -Raw -Encoding utf8 | ConvertFrom-Json
        if ($t.provisional -and ($null -ne $t.provisional.sale_qty)) { return $true }
    } catch { }
    return $false
}

# ---------- 回款 / 未回款 ----------
function Render-Pay {
    if (-not $script:LblPay) { return 0 }
    $c = $null
    if (Test-Path -LiteralPath $script:PayPath) {
        try { $c = Get-Content -LiteralPath $script:PayPath -Raw -Encoding utf8 | ConvertFrom-Json } catch { $c = $null }
    }
    if ($null -eq $c -or ($null -eq $c.balance_wan)) {
        $script:LblPay.Text = ""
        $script:LblPay.Visible = $false
        return 0
    }
    $bal = [double]$c.balance_wan
    $hist = @()
    if ($c.history) { $hist = @($c.history | ForEach-Object { $_ }) }
    elseif ($c.last -and $c.last.time) { $hist = @([ordered]@{ time = [string]$c.last.time; wan = [double]$c.last.wan }) }
    if ($hist.Count -eq 0) {
        $script:LblPay.ForeColor = Col $script:C_FAINT
        $script:LblPay.Text = ("应收 {0:N0}万 · 暂无回款" -f $bal)
        $script:LblPay.Visible = $true
        return 1
    }
    $lines = @()
    for ($i = 0; $i -lt $hist.Count; $i++) {
        $h = $hist[$i]
        $line = ("{0} 回款 {1:N0}万" -f [string]$h.time, [double]$h.wan)
        if ($i -eq $hist.Count - 1 -and $bal -gt 0) { $line += (" · 未回 {0:N0}万" -f $bal) }
        $lines += $line
    }
    $script:LblPay.ForeColor = Col $script:C_GREEN
    $script:LblPay.Text = ($lines -join "`r`n")
    $script:LblPay.Width = 380
    $script:LblPay.Visible = $true
    return $lines.Count
}

# ---------- 库存快照（行囊存货，与在售并列） ----------
function Render-Storage {
    if (-not $script:LblStk1) { return 0 }
    $st = $null
    if (Test-Path -LiteralPath $script:StkPath) {
        try { $st = Get-Content -LiteralPath $script:StkPath -Raw -Encoding utf8 | ConvertFrom-Json } catch { $st = $null }
    }
    if (($null -eq $st) -or ($null -eq $st.items)) {
        $script:LblStk1.ForeColor = Col $script:C_FAINT
        $script:LblStk1.Text = "库存快照：暂无记录"
        $script:LblStk2.Text = ""
        $script:LblStkLines.Text = ""
        return 0
    }
    $qty = 0.0; $wan = 0.0; $cnt = 0
    foreach ($it in $st.items) {
        $q = [double]$it.qty
        if ($q -le 0) { continue }
        $cnt++
        $qty += $q
        if ($null -ne $it.unit_price_wan) { $wan += $q * [double]$it.unit_price_wan }
    }
    $ts = ""
    if ($st.updated_at) {
        try { $ts = ([datetime]::Parse([string]$st.updated_at)).ToString("MM-dd HH:mm") } catch { $ts = [string]$st.updated_at }
    }
    $script:LblStk1.ForeColor = Col $script:C_DIM
    $script:LblStk1.Text = ("库存快照 {0} · {1} 款 · {2:N0} 件" -f $ts, $cnt, $qty)
    if ($script:Price) {
        $script:LblStk2.Text = ("库存商品价值合计：{0:N0} 万 ≈ ¥{1:N1}" -f $wan, ($wan * $script:Price / 3000.0))
    } else {
        $script:LblStk2.Text = ("库存商品价值合计：{0:N0} 万" -f $wan)
    }
    $lines = @(Build-Inv-Lines $st.items 322 $script:LblStkLines.Font)
    if ($lines.Count -gt 0) {
        $script:LblStkLines.ForeColor = Col $script:C_TXT
        $script:LblStkLines.Text = (($lines | ForEach-Object { $_.text }) -join "`r`n")
    } else {
        $script:LblStkLines.Text = ""
    }
    return $lines.Count
}

# ---------- 打造需求（在售+库存 vs 最低库存） ----------
function Render-Needs {
    if (-not $script:LblNeed1) { return 0 }
    $mins = @{ 60=20; 80=15; 100=10; 120=5 }
    $totals = @{}
    $names = @{}
    foreach ($p in @($script:StockPath, $script:StkPath)) {
        if (-not (Test-Path -LiteralPath $p)) { continue }
        try { $j = Get-Content -LiteralPath $p -Raw -Encoding utf8 | ConvertFrom-Json } catch { $j = $null }
        if ($null -eq $j -or $null -eq $j.items) { continue }
        foreach ($it in $j.items) {
            $code = [string]$it.code
            $totals[$code] = [double]$totals[$code] + [double]$it.qty
            $names[$code] = [string]$it.name
        }
    }
    $short = @{}
    foreach ($code in $totals.Keys) {
        if ($code -notmatch '^(ring|ear|brace|pend)_(\d+)$') { continue }
        $lv = [int]$matches[2]
        if (-not $mins.ContainsKey($lv)) { continue }
        $gap = $mins[$lv] - $totals[$code]
        if ($gap -gt 0) { $short[$code] = $gap }
    }
    if ($short.Count -eq 0) {
        $script:LblNeed1.ForeColor = Col $script:C_GREEN
        $script:LblNeed1.Text = "打造需求 · 全部达标"
        $script:LblNeedLines.Text = ""
        return 0
    }
    $order = @("ring", "ear", "brace", "pend")
    $byLevel = @{}
    foreach ($code in $short.Keys) {
        if ($code -match '^(\w+)_(\d+)$') {
            $lv = [int]$matches[2]; $t = $matches[1]
            if (-not $byLevel.ContainsKey($lv)) { $byLevel[$lv] = @() }
            $byLevel[$lv] += @{ type = $t; name = $names[$code]; gap = $short[$code] }
        }
    }
    $lines = @()
    foreach ($lv in ($byLevel.Keys | Sort-Object)) {
        $parts = @()
        foreach ($t in $order) {
            foreach ($x in $byLevel[$lv]) { if ($x.type -eq $t) { $parts += ("{0}缺{1:N0}" -f $x.name, $x.gap) } }
        }
        if ($parts.Count -gt 0) { $lines += ("{0}级：" -f $lv) + ($parts -join " + ") }
    }
    $script:LblNeed1.ForeColor = Col $script:C_DIM
    $script:LblNeed1.Text = ("打造需求 · 缺 {0} 款" -f $short.Count)
    $script:LblNeedLines.ForeColor = Col $script:C_TXT
    $script:LblNeedLines.Text = ($lines -join "`r`n")
    return $lines.Count
}

function Layout-Bottom($lineCount, $lineH, $hasDay3, $stkLineCount, $needLineCount) {
    if (-not $script:LblInv) { return }
    $invH = 0
    if ($lineCount -gt 0) { $invH = $lineCount * $lineH + 4 }
    $script:LblInv.Height = [math]::Max(0, $invH)
    if (-not $stkLineCount) { $stkLineCount = 0 }
    if (-not $needLineCount) { $needLineCount = 0 }
    $stkTop = 326 + $invH + 2
    if ($script:LblStk1) { $script:LblStk1.Top = $stkTop }
    if ($script:LblStk2) { $script:LblStk2.Top = $stkTop + 18 }
    if ($script:LblStkLines) {
        $script:LblStkLines.Top = $stkTop + 38
        $script:LblStkLines.Height = [math]::Max(0, $stkLineCount * 13)
    }
    $needTop = $stkTop + 38 + $stkLineCount * 13 + 2
    if ($script:LblNeed1) { $script:LblNeed1.Top = $needTop }
    if ($script:LblNeedLines) {
        $script:LblNeedLines.Top = $needTop + 18
        $script:LblNeedLines.Height = [math]::Max(0, $needLineCount * 13)
    }
    $sepY = $needTop + 18 + $needLineCount * 13 + 4
    $script:SepY2 = $sepY
    $day1Y = $sepY + 7
    $day2Y = $day1Y + 20
    $day3Y = $day2Y + 20
    if ($script:LblDay1) { $script:LblDay1.Top = $day1Y }
    if ($script:LblDay2) { $script:LblDay2.Top = $day2Y }
    if ($script:LblDay3) {
        $script:LblDay3.Top = $day3Y
        $script:LblDay3.Visible = [bool]$hasDay3
    }
    if ($script:BtnToday) {
        $script:BtnToday.Location = New-Object System.Drawing.Point(305, ($day1Y - 2))
        $script:BtnYest.Location = New-Object System.Drawing.Point(349, ($day1Y - 2))
    }
    $rowEnd = $day2Y
    if ($hasDay3) { $rowEnd = $day3Y }
    $payY = $rowEnd + 20
    # 直接读 cash.json 历史条数，避免依赖 Render-Pay 后才更新的 PayLines
    $payN = 1
    if (Test-Path -LiteralPath $script:PayPath) {
        try {
            $pc = Get-Content -LiteralPath $script:PayPath -Raw -Encoding utf8 | ConvertFrom-Json
            if ($pc.history) { $payN = @($pc.history).Count }
        } catch { $payN = 1 }
    }
    $hasPay = [bool](Test-Path -LiteralPath $script:PayPath)
    if ($script:LblPay) {
        $script:LblPay.Top = $payY
        $script:LblPay.Height = $payN * 16
        $script:LblPay.Visible = $hasPay
    }
    $bottomY = $rowEnd
    if ($hasPay) { $bottomY = $payY + $payN * 16 }
    $newH = $bottomY + 24
    if ($newH -ne $script:NormalH) {
        $script:NormalH = $newH
        if (-not $script:Mini -and $script:Form) {
            $script:Form.ClientSize = New-Object System.Drawing.Size($script:NormalW, $script:NormalH)
        }
    }
    if ($script:Form) { $script:Form.Invalidate() }
}

function Render-Stock {
    if (-not $script:LblSnap1) { return }
    $stock = $null
    if (Test-Path -LiteralPath $script:StockPath) {
        try { $stock = Get-Content -LiteralPath $script:StockPath -Raw -Encoding utf8 | ConvertFrom-Json } catch { $stock = $null }
    }
    if (($null -eq $stock) -or ($null -eq $stock.items)) {
        $script:LblSnap1.ForeColor = Col $script:C_FAINT
        $script:LblSnap1.Text = "在售快照：尚未统计（发摆摊/背包截图给我）"
        $script:LblSnap2.Text = ""
        if ($script:LblInv) { $script:LblInv.Text = "" }
        if ($script:LblInvSold1) { $script:LblInvSold1.Visible = $false }
        if ($script:LblInvSold2) { $script:LblInvSold2.Visible = $false }
        $stkN = Render-Storage
        $needN = Render-Needs
        Layout-Bottom 0 12 (Test-HasProv) $stkN $needN
        return
    }
    $qty = 0.0; $wan = 0.0; $cnt = 0; $sqty = 0.0; $scnt = 0
    foreach ($it in $stock.items) {
        $q = [double]$it.qty
        if ($q -le 0) { continue }
        $cnt++
        $qty += $q
        if ($null -ne $it.unit_price_wan) { $wan += $q * [double]$it.unit_price_wan }
        if ($it.PSObject.Properties.Name -contains 'sold') {
            $s = [double]$it.sold
            if ($s -gt 0) { $scnt++; $sqty += $s }
        }
    }
    $ts = ""
    if ($stock.updated_at) {
        try { $ts = ([datetime]::Parse([string]$stock.updated_at)).ToString("MM-dd HH:mm") } catch { $ts = [string]$stock.updated_at }
    }
    $script:LblSnap1.ForeColor = Col $script:C_DIM
    $script:LblSnap1.Text = ("在售快照 {0} · {1} 款 · {2:N0} 件，已售{3}款{4:N0}件" -f $ts, $cnt, $qty, $scnt, $sqty)
    if ($script:Price) {
        $perWan = $script:Price / 3000.0
        $script:LblSnap2.Text = ("在售商品价值合计：{0:N0} 万 ≈ ¥{1:N1}" -f $wan, ($wan * $perWan))
    } else {
        $script:LblSnap2.Text = ("在售商品价值合计：{0:N0} 万" -f $wan)
    }

    $lines = @(Build-Inv-Lines $stock.items 322 $script:LblInv.Font)
    $stkN = Render-Storage
    $needN = Render-Needs
    if ($lines.Count -gt 0) {
        $script:LblInv.ForeColor = Col $script:C_TXT
        $script:LblInv.Text = (($lines | ForEach-Object { $_.text }) -join "`r`n")
        $soldIdx = 0
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i].sold) {
                $lbl = $null
                if ($soldIdx -eq 0) { $lbl = $script:LblInvSold1 }
                elseif ($soldIdx -eq 1) { $lbl = $script:LblInvSold2 }
                if ($lbl) {
                    $lbl.Top = 326 + $i * 13
                    $lbl.Text = $lines[$i].text
                    $lbl.Visible = $true
                }
                $soldIdx++
            }
        }
        if ($script:LblInvSold1 -and $soldIdx -lt 1) { $script:LblInvSold1.Visible = $false }
        if ($script:LblInvSold2 -and $soldIdx -lt 2) { $script:LblInvSold2.Visible = $false }
        Layout-Bottom $lines.Count 13 (Test-HasProv) $stkN $needN
    } else {
        $script:LblInv.Text = ""
        if ($script:LblInvSold1) { $script:LblInvSold1.Visible = $false }
        if ($script:LblInvSold2) { $script:LblInvSold2.Visible = $false }
        Layout-Bottom 0 12 (Test-HasProv) $stkN $needN
    }
}

# ---------- 当日日结（销量 / 销售额） ----------
function Render-Yesterday {
    $y = $null
    if (Test-Path -LiteralPath $script:YestPath) {
        try { $y = Get-Content -LiteralPath $script:YestPath -Raw -Encoding utf8 | ConvertFrom-Json } catch { $y = $null }
    }
    if ($null -eq $y -or ($null -eq $y.start_qty)) {
        $script:LblDay1.ForeColor = Col $script:C_FAINT
        $script:LblDay1.Text = "昨日：尚未日结"
        $script:LblDay2.ForeColor = Col $script:C_FAINT
        $script:LblDay2.Text = ""
    } else {
        $md = [string]$y.date
        try {
            $md = ([datetime]::ParseExact([string]$y.date, "yyyy-MM-dd", [Globalization.CultureInfo]::InvariantCulture)).ToString("MM-dd")
        } catch { }
        $script:LblDay1.ForeColor = Col $script:C_DIM
        $script:LblDay1.Text = ("{0} · 期初 {1:N0} 件 · 补货 {2} 次" -f $md, [double]$y.start_qty, [int]$y.restock_cnt)
        if ($y.settled -and ($null -ne $y.sale_qty)) {
            $script:LblDay2.ForeColor = Col $script:C_BLUE
            $script:LblDay2.Text = ("销量 {0:N0} 件 · {1:N0} 万 ≈ ¥{2:N1}" -f [double]$y.sale_qty, [double]$y.sale_wan, [double]$y.rmb)
        } else {
            $script:LblDay2.ForeColor = Col $script:C_FAINT
            $script:LblDay2.Text = "昨日未日结（缺期末盘点）"
        }
    }
    if ($script:LblDay3) { $script:LblDay3.Text = ""; $script:LblDay3.Visible = $false }
    Update-DayBtnColor
}

function Render-Today {
    if (-not $script:LblDay1) { return }
    if ($script:DayView -eq "yesterday") { Render-Yesterday; return }
    $t = $null
    if (Test-Path -LiteralPath $script:TodayPath) {
        try { $t = Get-Content -LiteralPath $script:TodayPath -Raw -Encoding utf8 | ConvertFrom-Json } catch { $t = $null }
    }
    if ($null -eq $t) {
        $script:LblDay1.ForeColor = Col $script:C_FAINT
        $script:LblDay1.Text = "日结：尚无记录（等首次盘点）"
        $script:LblDay2.ForeColor = Col $script:C_FAINT
        $script:LblDay2.Text = ""
        if ($script:LblDay3) { $script:LblDay3.Text = ""; $script:LblDay3.Visible = $false }
        Update-DayBtnColor
        return
    }
    $md = [string]$t.date
    try {
        $md = ([datetime]::ParseExact([string]$t.date, "yyyy-MM-dd", [Globalization.CultureInfo]::InvariantCulture)).ToString("MM-dd")
    } catch { }
    $script:LblDay1.ForeColor = Col $script:C_DIM
    $script:LblDay1.Text = ("{0} · 期初 {1:N0} 件 · 补货 {2} 次" -f $md, [double]$t.start_qty, [int]$t.restock_cnt)
    if ($t.settled -and ($null -ne $t.sale_qty)) {
        $script:LblDay2.ForeColor = Col $script:C_BLUE
        $script:LblDay2.Text = ("销量 {0:N0} 件 · {1:N0} 万 ≈ ¥{2:N1}" -f [double]$t.sale_qty, [double]$t.sale_wan, [double]$t.rmb)
    } else {
        $script:LblDay2.ForeColor = Col $script:C_FAINT
        $script:LblDay2.Text = "销量：待 22:00 盘点后生成"
    }
    $pv = $t.provisional
    if ($script:LblDay3) {
        if ($pv -and ($null -ne $pv.sale_qty)) {
            $script:LblDay3.ForeColor = Col $script:C_GOLD
            $script:LblDay3.Text = ("截止{0}，已销{1:N0}件，价值：{2:N0} 万 ≈ ¥{3:N1}" -f [string]$pv.time, [double]$pv.sale_qty, [double]$pv.sale_wan, [double]$pv.rmb)
            $script:LblDay3.Visible = $true
        } else {
            $script:LblDay3.Text = ""
            $script:LblDay3.Visible = $false
        }
    }
    Update-DayBtnColor
}

# ---------- 贴边收起 / 展开 ----------
function Get-SnapSide {
    if ($script:Mini) { return $null }
    $wa = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    $f = $script:Form
    if ($f.Right -ge ($wa.Right - 10)) { return "right" }
    if ($f.Top -le ($wa.Top + 10)) { return "top" }
    return $null
}

function Update-MiniText {
    if (-not $script:LblMini) { return }
    $w = $script:MiniW; $h = $script:MiniH
    if ($script:Price) {
        $wan = 3000.0 / $script:Price
        $txt = "1元 ≈ {0:N4}万" -f $wan
    } else {
        $txt = "暂无行情"
    }
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
    $g.Clear((Col $script:C_GREEN))

    # 字号自适应：横条按宽度、竖条按高度，文字不能超出胶囊
    $fs = 11.0
    $f = New-Object System.Drawing.Font("Microsoft YaHei UI", $fs, [System.Drawing.FontStyle]::Bold)
    while ($fs -gt 6) {
        $sz = $g.MeasureString($txt, $f)
        if ($script:SnapSide -eq "top") { $limit = $w - 12 } else { $limit = $h - 12 }
        if ($sz.Width -le $limit) { break }
        $f.Dispose()
        $fs = $fs - 0.5
        $f = New-Object System.Drawing.Font("Microsoft YaHei UI", $fs, [System.Drawing.FontStyle]::Bold)
    }
    $sz = $g.MeasureString($txt, $f)
    $br = New-Object System.Drawing.SolidBrush((Col $script:C_WHITE))
    if ($script:SnapSide -eq "top") {
        # 横条：文字平放居中
        $g.DrawString($txt, $f, $br, [float](($w - $sz.Width) / 2), [float](($h - $sz.Height) / 2))
    } else {
        # 竖条：文字旋转 90° 竖排
        $g.TranslateTransform([float]($w / 2), [float]($h / 2))
        $g.RotateTransform(-90)
        $g.DrawString($txt, $f, $br, [float](-$sz.Width / 2), [float](-$sz.Height / 2))
        $g.ResetTransform()
    }
    $br.Dispose(); $f.Dispose(); $g.Dispose()
    $script:LblMini.Image = $bmp
}

function New-RoundedRegion([int]$w, [int]$h, [int]$r) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc(($w - $d), 0, $d, $d, 270, 90)
    $path.AddArc(($w - $d), ($h - $d), $d, $d, 0, 90)
    $path.AddArc(0, ($h - $d), $d, $d, 90, 90)
    $path.CloseFigure()
    return $path
}

function Enter-Mini {
    if ($script:Mini) { return }
    $f = $script:Form
    $wa = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    $side = Get-SnapSide
    if (-not $side) { $side = "right" }
    $script:SnapSide = $side
    # 胶囊形状随贴边方向：上边 → 横条；右边 → 竖条
    if ($side -eq "top") { $script:MiniW = 160; $script:MiniH = 28 }
    else                { $script:MiniW = 28;  $script:MiniH = 150 }
    $mw = $script:MiniW; $mh = $script:MiniH
    foreach ($c in $script:AllCtrls) { $c.Visible = $false }
    $f.ClientSize = New-Object System.Drawing.Size($mw, $mh)
    $f.Region = New-RoundedRegion $mw $mh 12
    $script:LblMini.Size = New-Object System.Drawing.Size($mw, $mh)
    $script:LblMini.Visible = $true
    if ($side -eq "right") {
        $mx = $wa.Right - $mw - 2
        $my = [math]::Min([math]::Max($f.Top, $wa.Top + 4), ($wa.Bottom - $mh - 4))
    } else {
        $my = $wa.Top + 2
        $mx = [math]::Min([math]::Max($f.Left, $wa.Left + 4), ($wa.Right - $mw - 4))
    }
    $f.Location = New-Object System.Drawing.Point([int]$mx, [int]$my)
    $script:Mini = $true
    Update-MiniText
    $f.Invalidate()
    Save-State
}

function Exit-Mini {
    if (-not $script:Mini) { return }
    $f = $script:Form
    $wa = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    if ($script:SnapSide -eq "top") {
        $my = $wa.Top + 12
        $mx = [math]::Min([math]::Max($f.Left, $wa.Left + 8), ($wa.Right - $script:NormalW - 8))
    } else {
        $mx = $wa.Right - $script:NormalW - 16
        $my = [math]::Min([math]::Max($f.Top, $wa.Top + 12), ($wa.Bottom - $script:NormalH - 12))
    }
    $script:Mini = $false
    $script:LblMini.Visible = $false
    $f.ClientSize = New-Object System.Drawing.Size($script:NormalW, $script:NormalH)
    $f.Region = $null
    $f.Location = New-Object System.Drawing.Point([int]$mx, [int]$my)
    foreach ($c in $script:AllCtrls) { $c.Visible = $true }
    Render
    Redraw
    $f.Invalidate()
    Save-State
}

# ---------- 刷新 ----------
function Refresh-Rate {
    if ($script:busy) { return }
    $script:busy = $true
    $script:BtnRefresh.Text = "..."
    $script:BtnRefresh.Enabled = $false
    try {
        $q = Get-LatestQuote
        $p = [double]$q.price
        if ($p -le 0) { throw "价格异常" }
        $script:Price = $p
        $script:Chg1d = $q.change_1d
        $script:Chg7d = $q.change_7d
        $script:CollectedAt = $q.collected_at
        $script:LastErr = $null

        try {
            $pts = Get-DailyHistory
            $script:Daily = @($pts | Select-Object -Last 7)
            $cache = @($script:Daily | ForEach-Object { [ordered]@{ day = $_.day; price = $_.price } })
            [IO.File]::WriteAllText($script:DailyCachePath, ($cache | ConvertTo-Json -Depth 3), (New-Object Text.UTF8Encoding $false))
        } catch {
            $script:LastErr = "日线取数失败：" + $_.Exception.Message
        }

        Append-History $script:Price $script:Chg1d $script:Chg7d $script:CollectedAt
    } catch {
        $script:LastErr = $_.Exception.Message
    }

    Render
    Redraw
    Save-State
    Arm-Timer
    $script:BtnRefresh.Text = "⟳"
    $script:BtnRefresh.Enabled = $true
    $script:busy = $false
}

function Arm-Timer {
    $next = Get-NextSlot
    $ms = [int][math]::Max(1500, (($next - (Get-Date)).TotalMilliseconds))
    $script:SchedTimer.Stop()
    $script:SchedTimer.Interval = $ms
    $script:NextAt = $next
    $script:SchedTimer.Start()
}

# ---------- 界面 ----------
Read-State
$script:NextAt = Get-NextSlot

$form = New-Object System.Windows.Forms.Form
$form.Text = "梦幻西游汇率悬浮窗"
$form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::None
$form.TopMost = $true
$form.ShowInTaskbar = $false
$form.BackColor = Col $script:C_BG
$form.ClientSize = New-Object System.Drawing.Size($script:NormalW, $script:NormalH)
$form.StartPosition = [System.Windows.Forms.FormStartPosition]::Manual
if ($script:Pos) { $form.Location = New-Object System.Drawing.Point($script:Pos[0], $script:Pos[1]) }
else {
    $screen = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    $form.Location = New-Object System.Drawing.Point(($screen.Right - 380), ($screen.Top + 40))
}
$script:Form = $form

# 防止窗口超出屏幕
$wa = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
$lx = [math]::Min([math]::Max($form.Left, $wa.Left + 8), $wa.Right - $form.Width - 8)
$ly = [math]::Min([math]::Max($form.Top, $wa.Top + 8), $wa.Bottom - $form.Height - 8)
$form.Location = New-Object System.Drawing.Point([int]$lx, [int]$ly)

function New-Label($text, $x, $y, $w, $h, $size, $color, $bold) {
    $l = New-Object System.Windows.Forms.Label
    $l.Text = $text
    $l.AutoSize = $false
    $l.Location = New-Object System.Drawing.Point($x, $y)
    $l.Size = New-Object System.Drawing.Size($w, $h)
    $style = if ($bold) { [System.Drawing.FontStyle]::Bold } else { [System.Drawing.FontStyle]::Regular }
    $l.Font = New-Object System.Drawing.Font("Microsoft YaHei UI", $size, $style, [System.Drawing.GraphicsUnit]::Point)
    $l.ForeColor = Col $color
    $l.BackColor = [System.Drawing.Color]::Transparent
    $form.Controls.Add($l)
    return $l
}

$script:LblTitle = New-Label "$($script:Server) · $($script:Region)" 14 9 210 22 10 $script:C_TXT $false
$script:LblBig   = New-Label "等待数据…" 12 32 300 40 21 $script:C_GOLD $true
$script:LblSub   = New-Label "" 15 74 300 20 9.5 "#4A5261" $false
$script:LblMeta  = New-Label "" 15 95 330 20 8.5 "#5A6270" $false
$script:LblRecent = New-Label "近 7 日走势" 14 117 376 18 8.5 $script:C_DIM $false

$script:Pb7 = New-Object System.Windows.Forms.PictureBox
$script:Pb7.Location = New-Object System.Drawing.Point(12, 137)
$script:Pb7.Size = New-Object System.Drawing.Size(376, 112)
$form.Controls.Add($script:Pb7)

$script:LblTime = New-Label "" 15 253 376 18 7.5 $script:C_FAINT $false
$script:LblSnap1 = New-Label "" 15 284 376 16 8 $script:C_FAINT $false
$script:LblSnap2 = New-Label "" 15 303 376 22 10.5 $script:C_GOLD $true
$script:LblInv = New-Label "" 15 326 376 0 8 $script:C_TXT $false
$script:LblInv.AutoSize = $false
$script:LblInv.Width = 376
$script:LblInvSold1 = New-Label "" 15 326 376 13 8 $script:C_ERR $false
$script:LblInvSold1.BackColor = Col $script:C_BG
$script:LblInvSold1.Visible = $false
$script:LblInvSold2 = New-Label "" 15 326 376 13 8 $script:C_ERR $false
$script:LblInvSold2.BackColor = Col $script:C_BG
$script:LblInvSold2.Visible = $false
$script:LblStk1 = New-Label "" 15 340 376 16 8 $script:C_FAINT $false
$script:LblStk2 = New-Label "" 15 358 376 20 10.5 $script:C_GOLD $true
$script:LblStkLines = New-Label "" 15 378 376 0 8 $script:C_TXT $false
$script:LblStkLines.AutoSize = $false
$script:LblStkLines.Width = 376
$script:LblNeed1 = New-Label "" 15 398 376 16 8 $script:C_FAINT $false
$script:LblNeedLines = New-Label "" 15 416 376 0 8 $script:C_TXT $false
$script:LblNeedLines.AutoSize = $false
$script:LblNeedLines.Width = 376
$script:LblDay1 = New-Label "" 15 332 240 16 8 $script:C_DIM $false
$script:LblDay2 = New-Label "" 15 350 240 20 10.5 $script:C_BLUE $true
$script:LblDay3 = New-Label "" 15 368 376 20 10.5 $script:C_GOLD $true
$script:LblDay3.Visible = $false
$script:LblPay = New-Label "" 15 388 376 20 10.5 $script:C_GREEN $false
$script:LblPay.Visible = $false

# 日结分组切换：昨日 / 今日
$script:DayView = "today"
$script:NoSnap = $false
function Set-DayView($v) {
    $script:DayView = [string]$v
    Render-Stock
    Render-Today
    if ($script:Form) { $script:Form.Invalidate() }
}

function New-DayBtn($text) {
    $b = New-Object System.Windows.Forms.Button
    $b.Text = $text
    $b.Size = New-Object System.Drawing.Size(40, 18)
    $b.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
    $b.FlatAppearance.BorderSize = 0
    $b.BackColor = Col $script:C_WHITE
    $b.ForeColor = Col $script:C_DIM
    $b.Font = New-Object System.Drawing.Font("Microsoft YaHei UI", 7.5, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Point)
    $b.Cursor = [System.Windows.Forms.Cursors]::Hand
    $b.TabStop = $false
    $b.Add_MouseDown({ $script:NoSnap = $true })
    $form.Controls.Add($b)
    return $b
}
$script:BtnToday = New-DayBtn "今日"
$script:BtnToday.Add_Click({ Set-DayView "today" })
$script:BtnYest = New-DayBtn "昨日"
$script:BtnYest.Add_Click({ Set-DayView "yesterday" })

function Update-DayBtnColor {
    if ($script:BtnToday) {
        if ($script:DayView -eq "today") { $script:BtnToday.ForeColor = Col $script:C_BLUE } else { $script:BtnToday.ForeColor = Col $script:C_FAINT }
    }
    if ($script:BtnYest) {
        if ($script:DayView -eq "yesterday") { $script:BtnYest.ForeColor = Col $script:C_BLUE } else { $script:BtnYest.ForeColor = Col $script:C_FAINT }
    }
}

# 小刷新按钮
$btn = New-Object System.Windows.Forms.Button
$btn.Text = "⟳"
$btn.Size = New-Object System.Drawing.Size(24, 24)
$btn.Location = New-Object System.Drawing.Point(298, 8)
$btn.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
$btn.FlatAppearance.BorderSize = 0
$btn.FlatAppearance.MouseOverBackColor = [System.Drawing.ColorTranslator]::FromHtml("#E6EBF3")
$btn.BackColor = [System.Drawing.ColorTranslator]::FromHtml("#F2F4F7")
$btn.ForeColor = Col $script:C_TXT
$btn.Font = New-Object System.Drawing.Font("Segoe UI Symbol", 11, [System.Drawing.FontStyle]::Regular)
$btn.Cursor = [System.Windows.Forms.Cursors]::Hand
$btn.TabStop = $false
$btn.Add_Click({
    $script:SchedTimer.Stop()
    Refresh-Rate
})
$form.Controls.Add($btn)
$script:BtnRefresh = $btn

# 关闭
$btnClose = New-Object System.Windows.Forms.Label
$btnClose.Text = "✕"
$btnClose.AutoSize = $false
$btnClose.Size = New-Object System.Drawing.Size(22, 22)
$btnClose.Location = New-Object System.Drawing.Point(328, 10)
$btnClose.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter
$btnClose.Font = New-Object System.Drawing.Font("Microsoft YaHei UI", 9.5)
$btnClose.ForeColor = Col $script:C_FAINT
$btnClose.BackColor = [System.Drawing.Color]::Transparent
$btnClose.Cursor = [System.Windows.Forms.Cursors]::Hand
$btnClose.Add_Click({ $script:Form.Close() })
$form.Controls.Add($btnClose)

# 收起后的胶囊小图标（点击展开；贴顶=横条平排文字、贴右=竖条竖排文字，文字绘制在图里）
$lblMini = New-Object System.Windows.Forms.PictureBox
$lblMini.Size = New-Object System.Drawing.Size(28, 120)
$lblMini.Location = New-Object System.Drawing.Point(0, 0)
$lblMini.BackColor = Col $script:C_BG
$lblMini.Cursor = [System.Windows.Forms.Cursors]::Hand
$lblMini.Visible = $false
$lblMini.Add_Click({ Exit-Mini })
$form.Controls.Add($lblMini)
$script:LblMini = $lblMini

$script:AllCtrls = @($form.Controls | Where-Object { $_ -ne $script:LblMini })

# 拖动（松手时判断是否贴边收起）
foreach ($c in (@($form) + $script:AllCtrls)) {
    $c.Add_MouseDown({
        param($s, $e)
        if ($e.Button -eq [System.Windows.Forms.MouseButtons]::Left) { $script:dragX = $e.X; $script:dragY = $e.Y }
    })
    $c.Add_MouseMove({
        param($s, $e)
        if ($script:dragX -ne $null -and $e.Button -eq [System.Windows.Forms.MouseButtons]::Left) {
            $script:Form.Location = New-Object System.Drawing.Point(
                ($script:Form.Left + $e.X - $script:dragX),
                ($script:Form.Top + $e.Y - $script:dragY))
        }
    })
    $c.Add_MouseUp({
        $script:dragX = $null
        if ($script:NoSnap) { $script:NoSnap = $false; return }
        if (-not $script:Mini) {
            $side = Get-SnapSide
            if ($side) { Enter-Mini } else { Save-State }
        }
    })
}

# 右键菜单
$menu = New-Object System.Windows.Forms.ContextMenuStrip
$miRefresh = $menu.Items.Add("立即刷新")
$miRefresh.Add_Click({ $script:SchedTimer.Stop(); Refresh-Rate })
$miCopy = $menu.Items.Add("复制当前汇率")
$miCopy.Add_Click({
    if ($script:Price) {
        $txt = "$($script:Server)（$($script:Region)）1元 ≈ {0:N4}万，1万 ≈ ¥{1:N4}，金价 ¥{2:N2}/3000万" -f (3000.0 / $script:Price), ($script:Price / 3000.0), $script:Price
        [System.Windows.Forms.Clipboard]::SetText($txt)
    }
})
$miTop = $menu.Items.Add("切换置顶")
$miTop.Add_Click({ $script:Form.TopMost = -not $script:Form.TopMost })
$menu.Items.Add("-") | Out-Null
$miExit = $menu.Items.Add("退出")
$miExit.Add_Click({ $script:Form.Close() })
$form.ContextMenuStrip = $menu
foreach ($c in $form.Controls) { $c.ContextMenuStrip = $menu }
$script:Menu = $menu

# ---------- 外观：窗体与胶囊全部实体，不透明（用户口径：不搞透明） ----------
$script:BaseOpa = 1.0       # 常态：完全不透明（实体）
$script:HoverOpa = 1.0      # 鼠标在窗口上时：完全不透明（与常态一致）
$script:ClickThrough = $false
$script:PeekUntil = $null

function Set-ClickThrough([bool]$on) {
    if ($script:ClickThrough -eq $on) { return }
    if (-not $script:Form -or -not $script:Form.IsHandleCreated) { return }
    $h = $script:Form.Handle
    $style = [MHXYHover]::GetWindowLong($h, -20)
    if ($on) { $style = $style -bor 0x00000020 } else { $style = $style -band (-bnot 0x00000020) }
    [void][MHXYHover]::SetWindowLong($h, -20, $style)
    $script:ClickThrough = $on
}

$form.Opacity = $script:BaseOpa
Set-ClickThrough ($script:BaseOpa -le 0.05)

$hoverTimer = New-Object System.Windows.Forms.Timer
$hoverTimer.Interval = 120
$hoverTimer.Add_Tick({
    if (-not $script:Form -or -not $script:Form.IsHandleCreated) { return }
    if ($script:Menu -and $script:Menu.Visible) { return }          # 菜单开着时不要闪
    if ($script:dragX -ne $null) {                                  # 拖动中保持实心、可交互
        Set-ClickThrough $false
        if ([math]::Abs($script:Form.Opacity - $script:HoverOpa) -gt 0.01) { $script:Form.Opacity = $script:HoverOpa }
        return
    }
    $pt = New-Object MHXYHover+POINT
    [void][MHXYHover]::GetCursorPos([ref]$pt)
    $rc = New-Object MHXYHover+RECT
    [void][MHXYHover]::GetWindowRect($script:Form.Handle, [ref]$rc)
    $hit = ($pt.X -ge $rc.Left -and $pt.X -lt $rc.Right -and $pt.Y -ge $rc.Top -and $pt.Y -lt $rc.Bottom)
    $peeking = ($script:PeekUntil -and (Get-Date) -lt $script:PeekUntil)
    if ($hit -or $peeking) {
        Set-ClickThrough $false
        if ([math]::Abs($script:Form.Opacity - $script:HoverOpa) -gt 0.01) { $script:Form.Opacity = $script:HoverOpa }
    } else {
        if ([math]::Abs($script:Form.Opacity - $script:BaseOpa) -gt 0.01) { $script:Form.Opacity = $script:BaseOpa }
        if ($script:BaseOpa -le 0.05) { Set-ClickThrough $true } else { Set-ClickThrough $false }
    }
    if ($env:MHXY_WIDGET_DEBUG) {
        try {
            [IO.File]::WriteAllText((Join-Path $env:TEMP "widget-diag.txt"), "cursor=$($pt.X),$($pt.Y) rect=$($rc.Left),$($rc.Top),$($rc.Right),$($rc.Bottom) hit=$hit opacity=$($script:Form.Opacity) clickthrough=$($script:ClickThrough)`n")
        } catch { }
    }
})
$hoverTimer.Start()
$script:SchedTimer = New-Object System.Windows.Forms.Timer
$script:SchedTimer.Interval = 600000
$script:SchedTimer.Add_Tick({ Refresh-Rate })

# 每 30 秒刷新一次文字
$tick = New-Object System.Windows.Forms.Timer
$tick.Interval = 30000
$tick.Add_Tick({ Render })
$tick.Start()

$form.Add_Shown({
    Render
    Redraw
    Refresh-Rate
    if ($script:RestoreMini) { Enter-Mini }
    # 启动后保持实体不透明；PeekUntil 机制在实体态下无变化，保留无碍
    $script:PeekUntil = (Get-Date).AddSeconds(3)
    $form.Opacity = $script:HoverOpa
})

$form.Add_FormClosing({ Save-State })

# 浅色描边，避免白底融进白色桌面
$form.Add_Paint({
    param($s, $e)
    if ($script:Mini) { return }
    $pen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#D5DBE4"), 1)
    $e.Graphics.DrawRectangle($pen, 0, 0, ($script:Form.ClientSize.Width - 1), ($script:Form.ClientSize.Height - 1))
    $pen.Dispose()
    $penSep = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#E6EAF0"), 1)
    $e.Graphics.DrawLine($penSep, 14, 274, 346, 274)
    $e.Graphics.DrawLine($penSep, 14, $script:SepY2, 346, $script:SepY2)
    $penSep.Dispose()
})

[System.Windows.Forms.Application]::Run($form)

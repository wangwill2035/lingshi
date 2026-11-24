// print_page.hpp — 热敏打印页渲染（58mm / 80mm 按纸张宽度自适应）
// 样式（用户定稿）：
//   第一行：标题"入 库 明 细"（字间一个空格位，居中）
//   双线
//   表头行：级别 | 戒 耳 手 佩（简称，居中）
//   单线
//   4 个级别数据行（每行后一条单线）
//   二维码 + 底部打印时间
#pragma once
#include <windows.h>
#include <string>

// 渲染一页打印内容
// hdc: 打印 DC；dpiX/dpiY: 设备分辨率；pageW/pageH: 可打印区像素；
// qrBmp: 二维码位图（正方形，可空）；op: 'I' 入库 / 'O' 上架；
// counts[16]: 16 个数量（行序）；names[16]: 16 个灵饰名称（行序，本版不用但保留签名）
inline void RenderPrintPage(HDC hdc, int dpiX, int dpiY, int pageW, int pageH,
                            HBITMAP qrBmp, char op, const int counts[16],
                            const wchar_t* const names[16]) {
    (void)names;
    const double mmX = (double)dpiX / 25.4;
    const double mmY = (double)dpiY / 25.4;
    auto px = [&](double v) { return (int)(v * mmX); };
    auto py = [&](double v) { return (int)(v * mmY); };

    const double availWmm = (double)pageW * 25.4 / dpiX;   // 可打印宽度(mm)
    const double margin  = 4.0;                             // 左右边距(mm)
    double tabW = availWmm - margin * 2;
    if (tabW < 20.0) tabW = 20.0;

    HGDIOBJ oldFont = nullptr;
    auto useFont = [&](double hmm, bool bold) {
        HFONT f = CreateFontW(-py(hmm), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
        oldFont = SelectObject(hdc, f);
        return f;
    };
    auto doneFont = [&](HFONT f) { SelectObject(hdc, oldFont); DeleteObject(f); };

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t hhmm[64];
    wsprintfW(hhmm, L"%04d-%02d-%02d %02d:%02d",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    std::wstring title;
    if (op == 'I')      title = L"入库明细";
    else if (op == 'O') title = L"上架明细";
    else if (op == 'C') title = L"中途盘点";
    else                title = L"日结盘点";   // 'E'

    // 标题字间加一个空格位："入 库 明 细"
    std::wstring spaced;
    for (size_t k = 0; k < title.size(); ++k) {
        if (k) spaced += L' ';
        spaced += title[k];
    }

    const int left = px(margin), right = px(margin + tabW);

    // 横线辅助
    auto hline = [&](int y) {
        HPEN pen = CreatePen(PS_SOLID, px(0.35), RGB(0, 0, 0));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        MoveToEx(hdc, left, y, nullptr);
        LineTo(hdc, right, y);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    };

    // ---- 第一行：标题（6mm 粗体居中，字间一个空格位） ----
    {
        HFONT tf = useFont(6.0, true);
        RECT tr = { left, py(3), right, py(10) };
        DrawTextW(hdc, spaced.c_str(), (int)spaced.size(), &tr, DT_CENTER | DT_TOP | DT_SINGLELINE);
        doneFont(tf);
    }

    // ---- 双线（标题下方） ----
    hline(py(12));
    hline(py(13));

    // ---- 表格主体：表头 + 4 数据行 ----
    // 列：级别列 lvW，其余 4 列均分（戒 耳 手 佩）
    const double lvWmm = 12.0;
    const double colWmm = (tabW - lvWmm) / 4.0;
    int xs[6];
    xs[0] = left;
    for (int c = 0; c < 4; ++c) xs[c + 1] = left + px(lvWmm + c * colWmm);
    xs[5] = right;

    const int y0 = py(14.5);      // 表头行顶
    const int rowH = py(5.8);     // 行高（表头/数据，留足字形高度防遮字）
    const int fs = 3.2;           // 表头/数据字号(mm)

    // 表头：级别（左对齐）+ 戒 耳 手 佩（居中，垂直居中）
    {
        HFONT hf = useFont(fs, true);
        RECT lr = { xs[0] + px(1.0), y0, xs[1] - px(1.0), y0 + rowH };
        DrawTextW(hdc, L"级别", 2, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        const wchar_t* hs[4] = { L"戒", L"耳", L"手", L"佩" };
        for (int c = 0; c < 4; ++c) {
            RECT cr = { xs[c + 1] + px(1.0), y0, xs[c + 2] - px(1.0), y0 + rowH };
            DrawTextW(hdc, hs[c], 1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        doneFont(hf);
    }
    // 表头下单线
    hline(y0 + rowH);

    // 4 个数据行（每行后一条单线，文本垂直居中）
    const wchar_t* levels[4] = { L"60级", L"80级", L"100级", L"120级" };
    {
        HFONT df = useFont(fs, false);
        for (int r = 0; r < 4; ++r) {
            int y = y0 + (r + 1) * rowH;
            RECT lr = { xs[0] + px(1.0), y, xs[1] - px(1.0), y + rowH };
            DrawTextW(hdc, levels[r], (int)wcslen(levels[r]), &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            for (int c = 0; c < 4; ++c) {
                int i = r * 4 + c;
                RECT cr = { xs[c + 1] + px(1.0), y, xs[c + 2] - px(1.0), y + rowH };
                wchar_t nb[8];
                wsprintfW(nb, L"%d", counts[i]);
                DrawTextW(hdc, nb, (int)wcslen(nb), &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            hline(y + rowH);   // 每数据行下分隔线
        }
        doneFont(df);
    }

    // ---- 二维码（居中） ----
    // 窄纸（热敏小票）二维码放大占比，保证 203dpi 下模块足够大可扫；
    // 宽纸（80mm）保持紧凑比例
    const int qy0 = y0 + 5 * rowH;
    const float qrRatio = (tabW < 60.0f) ? 0.96f : 0.72f;
    const int qrp = px(tabW * qrRatio);
    const int qx = (pageW - qrp) / 2, qy = qy0 + py(2);
    if (qrBmp) {
        BITMAP bm = {};
        GetObject(qrBmp, sizeof(bm), &bm);
        if (bm.bmWidth > 0 && bm.bmHeight > 0) {
            HDC mem = CreateCompatibleDC(hdc);
            HGDIOBJ o = SelectObject(mem, qrBmp);
            StretchBlt(hdc, qx, qy, qrp, qrp, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
            SelectObject(mem, o);
            DeleteDC(mem);
        }
    }

    // ---- 打印时间（二维码正下方，行距紧凑） ----
    HFONT pf = useFont(3.5, false);
    wchar_t pt[96];
    wsprintfW(pt, L"打印时间：%s", hhmm);
    int ptY = qy + qrp + py(1.5);
    RECT pr = { 0, ptY, pageW, ptY + py(8) };
    DrawTextW(hdc, pt, (int)wcslen(pt), &pr, DT_CENTER | DT_TOP | DT_SINGLELINE);
    doneFont(pf);
}

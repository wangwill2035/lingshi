// test_print.cpp — 打印布局离线验证：渲染 58mm / 80mm 两版页面为 PNG
// 用法: test_print.exe  → 生成 58mm.png / 80mm.png（与打印输出同比例）
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include "qrcodegen.hpp"
#include "print_page.hpp"

#pragma comment(lib, "gdiplus.lib")

// 生成二维码位图（模块 8px，白底黑块，含 4 模块静区）
static HBITMAP MakeQrBmp(const qrcodegen::QrCode& qr, int modulePx = 8, int quiet = 4) {
    int n = qr.getSize();
    int px = (n + quiet * 2) * modulePx;
    HDC dc = GetDC(nullptr);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = px;
    bi.bmiHeader.biHeight = -px;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP hb = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (hb && bits) {
        DWORD* p = (DWORD*)bits;
        for (int y = 0; y < px; ++y) {
            for (int x = 0; x < px; ++x) {
                int m = y / modulePx - quiet, n2 = x / modulePx - quiet;
                bool dark = (m >= 0 && m < n && n2 >= 0 && n2 < n && qr.getModule(m, n2));
                p[y * px + x] = dark ? 0x000000u : 0xFFFFFFu;
            }
        }
    }
    ReleaseDC(nullptr, dc);
    return hb;
}

// 查找 PNG 编码器 CLSID（MinGW 无 CLSID_PngEncoder 全局常量）
static bool GetPngClsid(CLSID* clsid) {
    UINT n = 0, sz = 0;
    Gdiplus::GetImageEncodersSize(&n, &sz);
    if (sz == 0) return false;
    std::vector<BYTE> buf(sz);
    Gdiplus::ImageCodecInfo* infos = (Gdiplus::ImageCodecInfo*)buf.data();
    Gdiplus::GetImageEncoders(n, sz, infos);
    for (UINT i = 0; i < n; ++i) {
        if (wcscmp(infos[i].MimeType, L"image/png") == 0) { *clsid = infos[i].Clsid; return true; }
    }
    return false;
}

static void SavePng(HBITMAP hb, const wchar_t* path) {
    CLSID clsid;
    bool ok = GetPngClsid(&clsid);
    if (!ok) wprintf(L"[err] no PNG encoder\n");
    BITMAP bm = {};
    GetObject(hb, sizeof(bm), &bm);
    HDC dc = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ o = SelectObject(mem, hb);
    Gdiplus::Bitmap bmp(bm.bmWidth, bm.bmHeight, 4 * bm.bmWidth, PixelFormat32bppRGB, (BYTE*)bm.bmBits);
    Gdiplus::Status st = Gdiplus::Ok;
    if (ok) st = bmp.Save(path, &clsid, nullptr);
    wprintf(L"[save] %s ok=%d status=%d w=%d h=%d\n", path, ok, (int)st, bm.bmWidth, bm.bmHeight);
    SelectObject(mem, o);
    DeleteDC(mem);
    ReleaseDC(nullptr, dc);
}

int wmain() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartup(&token, &gsi, nullptr);
    wprintf(L"[0] start\n");
    // 模拟数据（与用户效果图一致：60/80/100/120 级 × 戒环镯佩）
    const int counts[16] = { 3, 3, 8, 2,  4, 3, 4, 10,  10, 10, 10, 6,  2, 2, 0, 1 };
    const wchar_t* names[16] = {
        L"枫华戒", L"翠叶环", L"香木镯", L"芝兰佩",
        L"芙蓉戒", L"明月珰", L"翡玉镯", L"逸云佩",
        L"金麟绕", L"玉蝶翩", L"墨影扣", L"莲音玦",
        L"悦碧水", L"点星芒", L"花映月", L"相思染"
    };

    std::string content =
        "1103143025I03030802040304101010100602020001";
    qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(content.c_str(), qrcodegen::QrCode::Ecc::LOW);
    wprintf(L"[1] qr size=%d\n", qr.getSize());
    HBITMAP qrb = MakeQrBmp(qr);
    wprintf(L"[2] qr bmp=%p\n", qrb);

    // 58mm × 100mm 标签 @203dpi
    {
        int dpi = 203;
        int w = (int)(58.0 / 25.4 * dpi), h = (int)(100.0 / 25.4 * dpi);
        HDC dc = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(dc);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bits) memset(bits, 0xFF, (size_t)w * h * 4);
        HGDIOBJ o = SelectObject(mem, dib);
        wprintf(L"[3] 58 render begin\n");
        RenderPrintPage(mem, dpi, dpi, w, h, qrb, 'I', counts, names);
        wprintf(L"[4] 58 render done\n");
        SelectObject(mem, o);
        SavePng(dib, L"58mm.png");
        DeleteObject(dib);
        DeleteDC(mem);
        ReleaseDC(nullptr, dc);
    }

    // 80mm × 120mm @203dpi
    {
        wprintf(L"[4.5] 80 setup\n");
        int dpi = 203;
        int w = (int)(80.0 / 25.4 * dpi), h = (int)(120.0 / 25.4 * dpi);
        wprintf(L"[4.6] 80 w=%d h=%d\n", w, h);
        HDC dc = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(dc);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        wprintf(L"[4.7] 80 dib=%p bits=%p\n", dib, bits);
        if (bits) memset(bits, 0xFF, (size_t)w * h * 4);
        HGDIOBJ o = SelectObject(mem, dib);
        wprintf(L"[5] 80 render begin\n");
        RenderPrintPage(mem, dpi, dpi, w, h, qrb, 'O', counts, names);
        wprintf(L"[6] 80 render done\n");
        SelectObject(mem, o);
        SavePng(dib, L"80mm.png");
        wprintf(L"[7] 80 saved\n");
        DeleteObject(dib);
        DeleteDC(mem);
        ReleaseDC(nullptr, dc);
    }

    DeleteObject(qrb);
    Gdiplus::GdiplusShutdown(token);
    wprintf(L"[8] done\n");
    return 0;
}

// 梦幻西游灵饰经营工作台 —— 纯 Win32 + SQLite（动态加载）单 exe
// 约束见《工作台开发技术文档》：宽字符 W 系列 API、sqlite 函数指针必须 __cdecl、
// 扫码框必须子类化截获回车、GDI 只画网格与数字、Static 只写标签、编译前先杀进程。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <gdiplus.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

// ---------------- 阶段日志（调试用，上线前置 0 关闭）----------------
#define WB_DEBUG_LOG 0
#if WB_DEBUG_LOG
static void LOGS(const char* msg) {
    HANDLE h = CreateFileA("wb.log", FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char line[256];
    sprintf_s(line, "[%02d:%02d:%02d.%03d] %s\r\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
    DWORD written = 0;
    WriteFile(h, line, (DWORD)strlen(line), &written, NULL);
    CloseHandle(h);
}
#else
static void LOGS(const char*) {}
#endif

// ---------------- 控件 ID ----------------
#define IDC_SCAN_EDIT   1001
#define IDC_CASH_EDIT   1003
#define IDC_CASH_BTN    1004
#define IDC_HINT        1010
#define IDC_TITLE_L     1021
#define IDC_TITLE_R     1022
#define IDC_TITLE_SALE  1031
#define IDC_TITLE_CASH  1032
#define IDC_SALE_LIST   3001
#define IDC_CASH_LIST   3002
#define IDC_PRICE_BTN   1005

static void OpenPriceDialog(HWND owner);
static HWND MakeStatic(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id, HFONT f, UINT style);

// ---------------- sqlite 动态加载（必须 __cdecl）----------------
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

#define SQLITE_OK 0
#define SQLITE_ROW 100
#define SQLITE_DONE 101

typedef int (__cdecl *t_open)(const char*, sqlite3**);
typedef int (__cdecl *t_close)(sqlite3*);
typedef int (__cdecl *t_prepare)(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
typedef int (__cdecl *t_step)(sqlite3_stmt*);
typedef int (__cdecl *t_finalize)(sqlite3_stmt*);
typedef int (__cdecl *t_reset)(sqlite3_stmt*);
typedef int (__cdecl *t_colint)(sqlite3_stmt*, int);
typedef double (__cdecl *t_coldouble)(sqlite3_stmt*, int);
typedef const unsigned char* (__cdecl *t_coltext)(sqlite3_stmt*, int);
typedef int (__cdecl *t_colcount)(sqlite3_stmt*);
typedef int (__cdecl *t_bindint)(sqlite3_stmt*, int, int);
typedef int (__cdecl *t_bindtext)(sqlite3_stmt*, int, const char*, int, void(*)(void*));
typedef int (__cdecl *t_binddouble)(sqlite3_stmt*, int, double);
typedef int (__cdecl *t_exec)(sqlite3*, const char*, void*, void*, char**);
typedef const char* (__cdecl *t_errmsg)(sqlite3*);
typedef int (__cdecl *t_changes)(sqlite3*);

static t_open       pOpen = NULL;
static t_close      pClose = NULL;
static t_prepare    pPrepare = NULL;
static t_step       pStep = NULL;
static t_finalize   pFinalize = NULL;
static t_reset      pReset = NULL;
static t_colint     pColInt = NULL;
static t_coldouble  pColDouble = NULL;
static t_coltext    pColText = NULL;
static t_colcount   pColCount = NULL;
static t_bindint    pBindInt = NULL;
static t_bindtext   pBindText = NULL;
static t_binddouble pBindDouble = NULL;
static t_exec       pExec = NULL;
static t_errmsg     pErrMsg = NULL;
static t_changes    pChanges = NULL;

static sqlite3* db = NULL;
static HMODULE  hSqliteDll = NULL;

// ---------------- 16 品类（名称/等级/部位固定）----------------
static const int ITEM_COUNT = 16;
static const wchar_t* ITEM_NAMES[16] = {
    L"枫华戒", L"翠叶环", L"香木镯", L"芝兰佩",
    L"芙蓉戒", L"明月珰", L"翡玉镯", L"逸云佩",
    L"金麟绕", L"玉蝶翩", L"墨影扣", L"莲音玦",
    L"悦碧水", L"点星芒", L"花映月", L"相思染"
};
static const wchar_t* ITEM_LEVEL[16] = {
    L"60级", L"60级", L"60级", L"60级",
    L"80级", L"80级", L"80级", L"80级",
    L"100级", L"100级", L"100级", L"100级",
    L"120级", L"120级", L"120级", L"120级"
};
static const wchar_t* ITEM_PART[16] = {
    L"戒指", L"耳饰", L"手镯", L"佩饰",
    L"戒指", L"耳饰", L"手镯", L"佩饰",
    L"戒指", L"耳饰", L"手镯", L"佩饰",
    L"戒指", L"耳饰", L"手镯", L"佩饰"
};

// ---------------- 运行态 ----------------
static int    g_onsale[16];
static int    g_storage[16];
static double g_price[16];
static double g_cost[16];

static HWND   g_hwnd = NULL;
static HFONT  g_font22 = NULL;
static HFONT  g_fontBold = NULL;
static HFONT  g_fontList = NULL;
static WNDPROC g_oldScanProc = NULL;
static Image* g_bgImg = NULL;
static wchar_t g_dbPath[MAX_PATH];
static wchar_t g_hint[512] = L"就绪：扫一扫二维码即可入账（I 入库 / O 上架 / C 盘点 / E 日结）";
static bool   g_inBusy = false;

// ---------------- 工具 ----------------
static void Utf8ToWide(const char* src, wchar_t* dst, int dstChars) {
    if (!src) { dst[0] = 0; return; }
    MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, dstChars);
}
static void WideToUtf8(const wchar_t* src, char* dst, int dstBytes) {
    if (!src) { dst[0] = 0; return; }
    WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dstBytes, NULL, NULL);
}

static void TrimW(wchar_t* s) {
    size_t n = wcslen(s);
    while (n > 0 && (s[n-1] == L'\r' || s[n-1] == L'\n' || s[n-1] == L' ' || s[n-1] == L'\t')) s[--n] = 0;
    size_t i = 0;
    while (s[i] == L' ' || s[i] == L'\t') i++;
    if (i > 0) { size_t k = 0; for (; s[i]; ++i) s[k++] = s[i]; s[k] = 0; }
}

// 日期字符串 YYYY-MM-DD 加天数（处理跨月跨年）
static void AddDays(const wchar_t* dateStr, int days, wchar_t* out, int outChars) {
    int y = 0, m = 0, d = 0;
    swscanf_s(dateStr, L"%d-%d-%d", &y, &m, &d);
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = y - 1900; tmv.tm_mon = m - 1; tmv.tm_mday = d + days;
    tmv.tm_hour = 12; tmv.tm_isdst = -1;
    mktime(&tmv);
    swprintf_s(out, outChars, L"%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
}

static void TodayStr(wchar_t* out, int chars) {
    SYSTEMTIME st; GetLocalTime(&st);
    swprintf_s(out, chars, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
}
static void NowOccStr(wchar_t* out, int chars) {
    SYSTEMTIME st; GetLocalTime(&st);
    swprintf_s(out, chars, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
}

// MMDDHHMMSS（10 位，无年）→ "YYYY-MM-DD HH:MM"，年份取当前年
static void OccFromTicket(const wchar_t* ts10, wchar_t* out, int chars) {
    SYSTEMTIME now; GetLocalTime(&now);
    int mm = (ts10[0]-L'0')*10 + (ts10[1]-L'0');
    int dd = (ts10[2]-L'0')*10 + (ts10[3]-L'0');
    int hh = (ts10[4]-L'0')*10 + (ts10[5]-L'0');
    int mi = (ts10[6]-L'0')*10 + (ts10[7]-L'0');
    swprintf_s(out, chars, L"%04d-%02d-%02d %02d:%02d", now.wYear, mm, dd, hh, mi);
}

// ---------------- 数据库 ----------------
// 只读模式：连到非测试库时置 true，任何写操作都会在这里被挡住
static bool g_readOnly = false;
static wchar_t g_dbTag[64] = L"测试库";
static wchar_t g_simTicket[128] = { 0 };

static bool DbInit(const wchar_t* dbPath) {
    LOGS("dbinit enter");
    // 说明：目录内原 sqlite3.dll 调用即崩（open 时写 0 地址），故优先用系统自带 winsqlite3.dll
    static const wchar_t* dllCandidates[] = { L"winsqlite3.dll", L"sqlite3.dll" };
    for (int di = 0; di < 2 && !hSqliteDll; di++) {
        HMODULE h = LoadLibraryW(dllCandidates[di]);
        if (!h) continue;
        hSqliteDll = h;
        #define GET(n, t) *(FARPROC*)&(n) = GetProcAddress(hSqliteDll, #t)
        GET(pOpen,       sqlite3_open);
        GET(pClose,      sqlite3_close);
        GET(pPrepare,    sqlite3_prepare_v2);
        GET(pStep,       sqlite3_step);
        GET(pFinalize,   sqlite3_finalize);
        GET(pReset,      sqlite3_reset);
        GET(pColInt,     sqlite3_column_int);
        GET(pColDouble,  sqlite3_column_double);
        GET(pColText,    sqlite3_column_text);
        GET(pColCount,   sqlite3_column_count);
        GET(pBindInt,    sqlite3_bind_int);
        GET(pBindText,   sqlite3_bind_text);
        GET(pBindDouble, sqlite3_bind_double);
        GET(pExec,       sqlite3_exec);
        GET(pErrMsg,     sqlite3_errmsg);
        GET(pChanges,    sqlite3_changes);
        #undef GET
        if (!pOpen || !pClose || !pPrepare || !pStep || !pFinalize || !pExec) {
            FreeLibrary(hSqliteDll);
            hSqliteDll = NULL;
            LOGS("dbinit: proc missing, try next dll");
        } else {
            LOGS("dbinit: dll loaded");
        }
    }
    if (!hSqliteDll) { LOGS("dbinit: LoadLibrary FAILED"); return false; }

    char pathA[MAX_PATH * 3];
    WideToUtf8(dbPath, pathA, sizeof(pathA));
    LOGS("dbinit: path converted");
    if (pOpen(pathA, &db) != SQLITE_OK) { db = NULL; LOGS("dbinit: open failed"); return false; }
    LOGS("dbinit: opened");
    pExec(db, "PRAGMA encoding='UTF-8'", NULL, NULL, NULL);
    pExec(db, "PRAGMA busy_timeout=5000", NULL, NULL, NULL);
    // 只读模式：数据库层直接拒绝一切写入，双保险（即使逻辑层漏挡也改不动数据）
    if (g_readOnly) pExec(db, "PRAGMA query_only=ON", NULL, NULL, NULL);
    LOGS("dbinit: pragma ok");
    return true;
}

// 写操作统一入口：只读模式直接拦下，返回 true 表示可以写
static bool WriteAllowed(const wchar_t* what, wchar_t* tipOut, int tipLen) {
    if (!g_readOnly) return true;
    swprintf_s(tipOut, tipLen, L"%s已禁用：当前连接的是真实库（只读）。测试请用 --test 连测试库。", what);
    return false;
}

// 执行无参 SQL
static bool ExecSql(const char* sql) {
    return pExec(db, sql, NULL, NULL, NULL) == SQLITE_OK;
}

// 读 16 品某表 qty 到数组（1 参数：表名由调用方决定，SQL 由调用方构造）
static void LoadQtyTable(const char* sql, int* dst) {
    for (int i = 0; i < ITEM_COUNT; i++) dst[i] = 0;
    sqlite3_stmt* st = NULL;
    if (pPrepare(db, sql, -1, &st, NULL) != SQLITE_OK) return;
    while (pStep(st) == SQLITE_ROW) {
        wchar_t nameW[64];
        Utf8ToWide((const char*)pColText(st, 0), nameW, 64);
        int qty = pColInt(st, 1);
        for (int i = 0; i < ITEM_COUNT; i++) {
            if (wcscmp(nameW, ITEM_NAMES[i]) == 0) { dst[i] = qty; break; }
        }
    }
    pFinalize(st);
}

static void LoadPriceCost() {
    for (int i = 0; i < ITEM_COUNT; i++) { g_price[i] = 0; g_cost[i] = 0; }
    sqlite3_stmt* st = NULL;
    if (pPrepare(db, "SELECT name, price_wan FROM price", -1, &st, NULL) == SQLITE_OK) {
        while (pStep(st) == SQLITE_ROW) {
            wchar_t nw[64]; Utf8ToWide((const char*)pColText(st, 0), nw, 64);
            double v = pColDouble(st, 1);
            for (int i = 0; i < ITEM_COUNT; i++) if (wcscmp(nw, ITEM_NAMES[i]) == 0) g_price[i] = v;
        }
        pFinalize(st);
    }
    // 成本：每个品类各取"距今天最近"的一条（effective_date 最大、同日取 id 最大），
    // 避免某品类单独调价后把其它品类的成本读成 0
    const char* costSql =
        "SELECT name, cost_wan FROM cost_history WHERE id IN ("
        "  SELECT c2.id FROM cost_history c2 WHERE NOT EXISTS ("
        "    SELECT 1 FROM cost_history c3 WHERE c3.name=c2.name AND ("
        "      c3.effective_date>c2.effective_date OR (c3.effective_date=c2.effective_date AND c3.id>c2.id)"
        "    )"
        "  )"
        ")";
    if (pPrepare(db, costSql, -1, &st, NULL) == SQLITE_OK) {
        while (pStep(st) == SQLITE_ROW) {
            wchar_t nw[64]; Utf8ToWide((const char*)pColText(st, 0), nw, 64);
            double v = pColDouble(st, 1);
            for (int i = 0; i < ITEM_COUNT; i++) if (wcscmp(nw, ITEM_NAMES[i]) == 0) g_cost[i] = v;
        }
        pFinalize(st);
    }
}

static void ReloadAll() {
    LoadQtyTable("SELECT name, qty FROM onsale", g_onsale);
    LoadQtyTable("SELECT name, qty FROM storage", g_storage);
    LoadPriceCost();
}

// 当前业务日：最后一条 scan_log 是 E → 次日；否则沿用该条 biz_date；无记录 → 今天
static void CurrentBizDate(wchar_t* out, int chars) {
    out[0] = 0;
    sqlite3_stmt* st = NULL;
    if (pPrepare(db, "SELECT biz_date, op FROM scan_log ORDER BY id DESC LIMIT 1", -1, &st, NULL) == SQLITE_OK) {
        if (pStep(st) == SQLITE_ROW) {
            wchar_t biz[32]; wchar_t op[8];
            Utf8ToWide((const char*)pColText(st, 0), biz, 32);
            Utf8ToWide((const char*)pColText(st, 1), op, 8);
            if (op[0] == L'E') AddDays(biz, 1, out, chars);
            else wcscpy_s(out, chars, biz);
        }
        pFinalize(st);
    }
    if (!out[0]) TodayStr(out, chars);
}

// 当天同签名是否已登记
static bool IsDupSignature(const wchar_t* bizDate, const wchar_t* sig) {
    bool dup = false;
    sqlite3_stmt* st = NULL;
    if (pPrepare(db, "SELECT 1 FROM scan_log WHERE biz_date=? AND signature=? LIMIT 1", -1, &st, NULL) == SQLITE_OK) {
        char a1[64], a2[128];
        WideToUtf8(bizDate, a1, sizeof(a1));
        WideToUtf8(sig, a2, sizeof(a2));
        pBindText(st, 1, a1, -1, NULL);
        pBindText(st, 2, a2, -1, NULL);
        if (pStep(st) == SQLITE_ROW) dup = true;
        pFinalize(st);
    }
    return dup;
}

// 今日统计
static void TodayStats(double* saleQty, double* saleAmt, double* profit, double* cash, int* onsaleTotal, int* storageTotal) {
    wchar_t biz[32]; CurrentBizDate(biz, 32);
    char bizA[64]; WideToUtf8(biz, bizA, sizeof(bizA));
    *saleQty = 0; *saleAmt = 0; *profit = 0; *cash = 0;

    sqlite3_stmt* st = NULL;
    if (pPrepare(db, "SELECT IFNULL(SUM(qty),0), IFNULL(SUM(amount),0), IFNULL(SUM(profit),0) FROM sale_detail WHERE biz_date=?", -1, &st, NULL) == SQLITE_OK) {
        pBindText(st, 1, bizA, -1, NULL);
        if (pStep(st) == SQLITE_ROW) {
            *saleQty = pColDouble(st, 0);
            *saleAmt = pColDouble(st, 1);
            *profit  = pColDouble(st, 2);
        }
        pFinalize(st);
    }
    st = NULL;
    if (pPrepare(db, "SELECT IFNULL(SUM(amount),0) FROM cash_log WHERE biz_date=?", -1, &st, NULL) == SQLITE_OK) {
        pBindText(st, 1, bizA, -1, NULL);
        if (pStep(st) == SQLITE_ROW) *cash = pColDouble(st, 0);
        pFinalize(st);
    }
    int ot = 0, stt = 0;
    for (int i = 0; i < ITEM_COUNT; i++) { ot += g_onsale[i]; stt += g_storage[i]; }
    *onsaleTotal = ot; *storageTotal = stt;
}

// ---------------- 今日销售明细 / 回款记录列表 ----------------
static void LvSet(HWND lv, int row, int col, const wchar_t* text) {
    LVITEMW it;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_TEXT;
    it.iItem = row;
    it.iSubItem = col;
    it.pszText = (LPWSTR)text;
    if (col == 0) SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
    else          SendMessageW(lv, LVM_SETITEMW, 0, (LPARAM)&it);
}

static void RefreshLists() {
    wchar_t biz[32]; CurrentBizDate(biz, 32);
    char bizA[64]; WideToUtf8(biz, bizA, sizeof(bizA));

    // 今日销售明细
    HWND lvSale = GetDlgItem(g_hwnd, IDC_SALE_LIST);
    if (lvSale) {
        SendMessageW(lvSale, LVM_DELETEALLITEMS, 0, 0);
        sqlite3_stmt* st = NULL;
        const char* sql = "SELECT name,part,level,qty,unit_price,amount,IFNULL(profit,0) FROM sale_detail WHERE biz_date=? ORDER BY id";
        if (pPrepare(db, sql, -1, &st, NULL) == SQLITE_OK) {
            pBindText(st, 1, bizA, -1, NULL);
            int row = 0;
            while (pStep(st) == SQLITE_ROW) {
                wchar_t nw[64] = { 0 }, pww[64] = { 0 }, lww[64] = { 0 };
                if (pColText(st, 0)) Utf8ToWide((const char*)pColText(st, 0), nw, 64);
                if (pColText(st, 1)) Utf8ToWide((const char*)pColText(st, 1), pww, 64);
                if (pColText(st, 2)) Utf8ToWide((const char*)pColText(st, 2), lww, 64);
                int q = pColInt(st, 3);
                double up = pColDouble(st, 4), am = pColDouble(st, 5), pf = pColDouble(st, 6);
                wchar_t b0[160], b1[32], b2[32], b3[32], b4[32];
                swprintf_s(b0, L"%s %s %s", nw, lww, pww);
                swprintf_s(b1, L"%d", q);
                swprintf_s(b2, L"%.0f", up);
                swprintf_s(b3, L"%.0f", am);
                swprintf_s(b4, L"%.0f", pf);
                LvSet(lvSale, row, 0, b0);
                LvSet(lvSale, row, 1, b1);
                LvSet(lvSale, row, 2, b2);
                LvSet(lvSale, row, 3, b3);
                LvSet(lvSale, row, 4, b4);
                row++;
            }
            pFinalize(st);
        }
    }

    // 今日回款记录
    HWND lvCash = GetDlgItem(g_hwnd, IDC_CASH_LIST);
    if (lvCash) {
        SendMessageW(lvCash, LVM_DELETEALLITEMS, 0, 0);
        sqlite3_stmt* st2 = NULL;
        const char* sql2 = "SELECT occ_time,amount,type FROM cash_log WHERE biz_date=? ORDER BY id";
        if (pPrepare(db, sql2, -1, &st2, NULL) == SQLITE_OK) {
            pBindText(st2, 1, bizA, -1, NULL);
            int row = 0;
            while (pStep(st2) == SQLITE_ROW) {
                wchar_t t[64] = { 0 };
                if (pColText(st2, 0)) Utf8ToWide((const char*)pColText(st2, 0), t, 64);
                double am = pColDouble(st2, 1);
                wchar_t ty[48] = { 0 };
                if (pColText(st2, 2)) Utf8ToWide((const char*)pColText(st2, 2), ty, 48);
                wchar_t b0[64], b1[32];
                swprintf_s(b0, L"%s", t + ((wcslen(t) > 11) ? 11 : 0));
                swprintf_s(b1, L"%.0f", am);
                LvSet(lvCash, row, 0, b0);
                LvSet(lvCash, row, 1, b1);
                LvSet(lvCash, row, 2, ty);
                row++;
            }
            pFinalize(st2);
        }
    }
}

// ---------------- UI 文本刷新 ----------------
static void SetText(int id, const wchar_t* text) {
    HWND h = GetDlgItem(g_hwnd, id);
    if (h) SetWindowTextW(h, text);
}

static void UpdateTitleAndHint() {
    wchar_t biz[32]; CurrentBizDate(biz, 32);
    wchar_t t[256];
    if (g_readOnly) swprintf_s(t, L"灵饰经营工作台  ·  业务日 %s  ·  【%s 只读】", biz, g_dbTag);
    else            swprintf_s(t, L"灵饰经营工作台  ·  业务日 %s  ·  %s", biz, g_dbTag);
    SetWindowTextW(g_hwnd, t);
    SetText(IDC_HINT, g_hint);
}

static void RefreshUI() {
    LOGS("RefreshUI enter");
    ReloadAll();
    LOGS("RefreshUI reloaded");
    double sq, sa, pf, cash; int ot, stt;
    TodayStats(&sq, &sa, &pf, &cash, &ot, &stt);
    LOGS("RefreshUI stats ok");
    double unpaid = sa - cash;
    wchar_t buf[128];

    swprintf_s(buf, L"今日已售：%.0f 件", sq);     SetText(2001, buf);
    swprintf_s(buf, L"销售额：%.0f 万", sa);       SetText(2002, buf);
    swprintf_s(buf, L"已回款：%.0f 万", cash);     SetText(2003, buf);
    swprintf_s(buf, L"未回款：%.0f 万", unpaid > 0 ? unpaid : 0); SetText(2004, buf);
    swprintf_s(buf, L"在售 %d 件 / 库存 %d 件", ot, stt); SetText(2005, buf);

    UpdateTitleAndHint();
    RefreshLists();
    InvalidateRect(g_hwnd, NULL, TRUE);
}

// ---------------- 落库：扫码 ----------------
static bool InsertScanLog(const wchar_t* bizDate, const wchar_t* occ, wchar_t op, const wchar_t* raw,
                          const wchar_t* detail, int total, const wchar_t* sig) {
    sqlite3_stmt* st = NULL;
    const char* sql = "INSERT INTO scan_log(biz_date, occ_time, op, raw_text, detail, total_qty, signature) VALUES(?,?,?,?,?,?,?)";
    if (pPrepare(db, sql, -1, &st, NULL) != SQLITE_OK) return false;
    char a1[64], a2[64], a3[8], a4[128], a5[512], a6[128];
    wchar_t opW[8];
    swprintf_s(opW, L"%c", op);
    WideToUtf8(bizDate, a1, sizeof(a1));
    WideToUtf8(occ, a2, sizeof(a2));
    WideToUtf8(opW, a3, sizeof(a3));
    WideToUtf8(raw, a4, sizeof(a4));
    WideToUtf8(detail, a5, sizeof(a5));
    WideToUtf8(sig, a6, sizeof(a6));
    pBindText(st, 1, a1, -1, NULL);
    pBindText(st, 2, a2, -1, NULL);
    pBindText(st, 3, a3, -1, NULL);
    pBindText(st, 4, a4, -1, NULL);
    pBindText(st, 5, a5, -1, NULL);
    pBindInt(st, 6, total);
    pBindText(st, 7, a6, -1, NULL);
    bool ok = (pStep(st) == SQLITE_DONE);
    pFinalize(st);
    return ok;
}

static bool WriteQtyDelta(const char* sql, int idx, int delta) {
    sqlite3_stmt* st = NULL;
    if (pPrepare(db, sql, -1, &st, NULL) != SQLITE_OK) return false;
    char nameA[64]; WideToUtf8(ITEM_NAMES[idx], nameA, sizeof(nameA));
    pBindInt(st, 1, delta);
    pBindText(st, 2, nameA, -1, NULL);
    bool ok = (pStep(st) == SQLITE_DONE);
    pFinalize(st);
    return ok;
}

static bool SetQty(const char* sql, int idx, int value) {
    sqlite3_stmt* st = NULL;
    if (pPrepare(db, sql, -1, &st, NULL) != SQLITE_OK) return false;
    char nameA[64]; WideToUtf8(ITEM_NAMES[idx], nameA, sizeof(nameA));
    pBindInt(st, 1, value);
    pBindText(st, 2, nameA, -1, NULL);
    bool ok = (pStep(st) == SQLITE_DONE);
    pFinalize(st);
    return ok;
}

static bool InsertDetail(const char* table, const wchar_t* occ, const wchar_t* bizDate, int idx, int qty) {
    sqlite3_stmt* st = NULL;
    char sql[256];
    sprintf_s(sql, "INSERT INTO %s(occ_time, biz_date, name, part, level, qty) VALUES(?,?,?,?,?,?)", table);
    if (pPrepare(db, sql, -1, &st, NULL) != SQLITE_OK) return false;
    char a1[64], a2[64], a3[64], a4[32], a5[32];
    WideToUtf8(occ, a1, sizeof(a1));
    WideToUtf8(bizDate, a2, sizeof(a2));
    WideToUtf8(ITEM_NAMES[idx], a3, sizeof(a3));
    WideToUtf8(ITEM_PART[idx], a4, sizeof(a4));
    WideToUtf8(ITEM_LEVEL[idx], a5, sizeof(a5));
    pBindText(st, 1, a1, -1, NULL);
    pBindText(st, 2, a2, -1, NULL);
    pBindText(st, 3, a3, -1, NULL);
    pBindText(st, 4, a4, -1, NULL);
    pBindText(st, 5, a5, -1, NULL);
    pBindInt(st, 6, qty);
    bool ok = (pStep(st) == SQLITE_DONE);
    pFinalize(st);
    return ok;
}

static bool InsertSale(const wchar_t* occ, const wchar_t* bizDate, int idx, int qty,
                       double unitPrice, double amount, double rate, double profit) {
    sqlite3_stmt* st = NULL;
    const char* sql = "INSERT INTO sale_detail(occ_time, biz_date, name, part, level, qty, unit_price, amount, profit_rate, profit) VALUES(?,?,?,?,?,?,?,?,?,?)";
    if (pPrepare(db, sql, -1, &st, NULL) != SQLITE_OK) return false;
    char a1[64], a2[64], a3[64], a4[32], a5[32];
    WideToUtf8(occ, a1, sizeof(a1));
    WideToUtf8(bizDate, a2, sizeof(a2));
    WideToUtf8(ITEM_NAMES[idx], a3, sizeof(a3));
    WideToUtf8(ITEM_PART[idx], a4, sizeof(a4));
    WideToUtf8(ITEM_LEVEL[idx], a5, sizeof(a5));
    pBindText(st, 1, a1, -1, NULL);
    pBindText(st, 2, a2, -1, NULL);
    pBindText(st, 3, a3, -1, NULL);
    pBindText(st, 4, a4, -1, NULL);
    pBindText(st, 5, a5, -1, NULL);
    pBindInt(st, 6, qty);
    pBindDouble(st, 7, unitPrice);
    pBindDouble(st, 8, amount);
    pBindDouble(st, 9, rate);
    pBindDouble(st, 10, profit);
    bool ok = (pStep(st) == SQLITE_DONE);
    pFinalize(st);
    return ok;
}

// 处理一张票；返回是否需要弹框（error 输出）与结果提示
static void HandleScan(const wchar_t* raw) {
    wchar_t clean[128];
    wcsncpy_s(clean, raw, 127);
    TrimW(clean);

    if (g_readOnly) {
        MessageBoxW(g_hwnd, L"当前连接的是真实库，只能查看，扫码入账已禁用。\n测试请用 --test 启动（连 mhxy_test.db）。",
                    L"只读模式", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (wcslen(clean) != 43) {
        wchar_t msg[256];
        swprintf_s(msg, L"扫码长度错误：%d 位（应为 43 位，42 位多为中文输入法吞字，切英文后重扫）\n内容：%s",
                   (int)wcslen(clean), clean);
        MessageBoxW(g_hwnd, msg, L"无法识别", MB_OK | MB_ICONWARNING);
        return;
    }
    wchar_t op = clean[10];
    if (op != L'I' && op != L'O' && op != L'C' && op != L'E') {
        wchar_t msg[256];
        swprintf_s(msg, L"操作符非法：%c（应为 I/O/C/E）\n内容：%s", op, clean);
        MessageBoxW(g_hwnd, msg, L"无法识别", MB_OK | MB_ICONWARNING);
        return;
    }
    int qty[16];
    for (int i = 0; i < 16; i++) {
        wchar_t a = clean[11 + i*2], b = clean[12 + i*2];
        if (a < L'0' || a > L'9' || b < L'0' || b > L'9') {
            MessageBoxW(g_hwnd, L"数量段含非数字字符，请重新扫码", L"无法识别", MB_OK | MB_ICONWARNING);
            return;
        }
        qty[i] = (a - L'0') * 10 + (b - L'0');
    }
    int total = 0;
    for (int i = 0; i < 16; i++) total += qty[i];
    if (total == 0) {
        MessageBoxW(g_hwnd, L"全零票：16 个品类数量全为 0。盘点全零不开放，请核对票据后重扫。", L"无效票据", MB_OK | MB_ICONWARNING);
        return;
    }

    // 票面时间 / 签名 / 业务日
    wchar_t occ[64]; OccFromTicket(clean, occ, 64);
    wchar_t sig[128];
    swprintf_s(sig, L"%c%s", op, clean + 11);
    wchar_t biz[32]; CurrentBizDate(biz, 32);

    if (op == L'I' || op == L'O') {
        if (IsDupSignature(biz, sig)) {
            wchar_t msg[320];
            swprintf_s(msg, L"重复票：本业务日已登记过同一张票（%c%s）\n业务日 %s，已跳过，未改动任何数据。",
                       op, clean + 11, biz);
            MessageBoxW(g_hwnd, msg, L"重复登记", MB_OK | MB_ICONINFORMATION);
            return;
        }
    }

    // 上架前库存校验
    if (op == L'O') {
        for (int i = 0; i < 16; i++) {
            if (qty[i] > g_storage[i]) {
                wchar_t msg[320];
                swprintf_s(msg, L"库存不足，整票未写入：\n%s（%s）需 %d 件，库存仅 %d 件。\n请先补货入库，或核对票据。",
                           ITEM_NAMES[i], ITEM_LEVEL[i], qty[i], g_storage[i]);
                MessageBoxW(g_hwnd, msg, L"库存不足", MB_OK | MB_ICONWARNING);
                return;
            }
        }
    }

    // 明细串
    wchar_t detail[512]; detail[0] = 0;
    for (int i = 0; i < 16; i++) {
        if (qty[i] > 0) {
            wchar_t part[64];
            swprintf_s(part, L"%s×%d", ITEM_NAMES[i], qty[i]);
            if (detail[0]) wcscat_s(detail, L"+");
            wcscat_s(detail, part);
        }
    }

    if (!ExecSql("BEGIN")) return;

    bool ok = true;
    int soldQty = 0;
    double soldAmt = 0;
    wchar_t done[512]; done[0] = 0;

    if (op == L'I') {
        for (int i = 0; i < 16 && ok; i++) {
            if (qty[i] <= 0) continue;
            ok = WriteQtyDelta("UPDATE storage SET qty = qty + ? WHERE name = ?", i, qty[i]);
            if (ok) ok = InsertDetail("in_detail", occ, biz, i, qty[i]);
        }
        swprintf_s(done, L"入库 %d 件已入账（库存已加）", total);
    } else if (op == L'O') {
        for (int i = 0; i < 16 && ok; i++) {
            if (qty[i] <= 0) continue;
            ok = WriteQtyDelta("UPDATE storage SET qty = qty - ? WHERE name = ?", i, qty[i]);
            if (ok) ok = WriteQtyDelta("UPDATE onsale SET qty = qty + ? WHERE name = ?", i, qty[i]);
            if (ok) ok = InsertDetail("out_detail", occ, biz, i, qty[i]);
            // 铁律 6：上架同步在入库明细写负数量，保持流水完整
            if (ok) ok = InsertDetail("in_detail", occ, biz, i, -qty[i]);
        }
        swprintf_s(done, L"上架 %d 件已入账（库存已扣、在售已加）", total);
    } else {
        // C/E：差额 = 在售现值 - 盘点值，写销售；在售覆盖为盘点值
        for (int i = 0; i < 16 && ok; i++) {
            int diff = g_onsale[i] - qty[i];
            if (diff > 0) {
                double price = g_price[i], cost = g_cost[i];
                double amt = price * diff;
                double rate = (price > 0) ? (price - cost) / price : 0;
                double pf = (price - cost) * diff;
                ok = InsertSale(occ, biz, i, diff, price, amt, rate, pf);
                soldQty += diff;
                soldAmt += amt;
            } else if (diff < 0) {
                // 盘出多于系统：提示但不写负销售
                ok = true;
            }
        }
        for (int i = 0; i < 16 && ok; i++) {
            ok = SetQty("UPDATE onsale SET qty = ? WHERE name = ?", i, qty[i]);
        }
        if (op == L'C') swprintf_s(done, L"中途盘点：卖出 %d 件 / %.0f 万已记销售，在售已覆盖为 %d 件", soldQty, soldAmt, total);
        else           swprintf_s(done, L"日结盘点：卖出 %d 件 / %.0f 万已记销售，本日结清，新票归次日", soldQty, soldAmt);
    }

    if (ok) ok = InsertScanLog(biz, occ, op, clean, detail, total, sig);

    if (ok) {
        ExecSql("COMMIT");
        wchar_t opName[32];
        const wchar_t* n = (op==L'I')?L"入库":(op==L'O')?L"上架":(op==L'C')?L"中途盘点":L"日结盘点";
        wcsncpy_s(opName, n, 31);
        swprintf_s(g_hint, L"%s %s · %s %d 件 · %s", occ + 11, opName, biz, total, done);
    } else {
        ExecSql("ROLLBACK");
        MessageBoxW(g_hwnd, L"写库失败，已回滚本次操作，未改动任何数据。", L"数据库错误", MB_OK | MB_ICONERROR);
    }

    // C/E 后若盘出多于系统，单独提示
    if (ok && (op == L'C' || op == L'E')) {
        wchar_t warn[512]; warn[0] = 0;
        for (int i = 0; i < 16; i++) {
            int diff = g_onsale[i] - qty[i];
            if (diff < 0) {
                wchar_t line[96];
                swprintf_s(line, L"\n%s 盘出比系统多 %d 件（可能漏记上架/入库）", ITEM_NAMES[i], -diff);
                wcscat_s(warn, line);
            }
        }
        if (warn[0]) MessageBoxW(g_hwnd, warn, L"盘出多于系统", MB_OK | MB_ICONWARNING);
    }
}

// ---------------- 落库：回款 ----------------
static bool InsertCash(const wchar_t* occ, const wchar_t* biz, double amount,
                       const wchar_t* type, const wchar_t* note) {
    sqlite3_stmt* st = NULL;
    const char* sql = "INSERT INTO cash_log(occ_time, biz_date, amount, type, note) VALUES(?,?,?,?,?)";
    if (pPrepare(db, sql, -1, &st, NULL) != SQLITE_OK) return false;
    char a1[64], a2[64], a4[64], a5[64];
    WideToUtf8(occ, a1, sizeof(a1));
    WideToUtf8(biz, a2, sizeof(a2));
    WideToUtf8(type, a4, sizeof(a4));
    WideToUtf8(note, a5, sizeof(a5));
    pBindText(st, 1, a1, -1, NULL);
    pBindText(st, 2, a2, -1, NULL);
    pBindDouble(st, 3, amount);
    pBindText(st, 4, a4, -1, NULL);
    pBindText(st, 5, a5, -1, NULL);
    bool ok = (pStep(st) == SQLITE_DONE);
    pFinalize(st);
    return ok;
}

static void HandleCash() {
    wchar_t buf[128]; buf[0] = 0;
    GetDlgItemTextW(g_hwnd, IDC_CASH_EDIT, buf, 128);
    TrimW(buf);
    if (!buf[0]) return;
    if (g_readOnly) {
        MessageBoxW(g_hwnd, L"当前连接的是真实库，只能查看，登记回款已禁用。\n测试请用 --test 启动（连 mhxy_test.db）。",
                    L"只读模式", MB_OK | MB_ICONINFORMATION);
        SetWindowTextW(GetDlgItem(g_hwnd, IDC_CASH_EDIT), L"");
        return;
    }
    wchar_t* endp = NULL;
    double amount = wcstod(buf, &endp);
    if (endp == buf || amount <= 0) {
        MessageBoxW(g_hwnd, L"回款金额必须为正数（单位：万）", L"输入有误", MB_OK | MB_ICONWARNING);
        return;
    }
    wchar_t biz[32]; CurrentBizDate(biz, 32);
    wchar_t occ[64]; NowOccStr(occ, 64);

    // 口径：当日回款超过当日销售额的部分记为「点卡回款」，正常部分记「正常」
    double sq = 0, sa = 0, pf = 0, cashAlready = 0; int ot = 0, stt = 0;
    TodayStats(&sq, &sa, &pf, &cashAlready, &ot, &stt);
    double remain = sa - cashAlready;         // 还能记多少「正常」
    if (remain < 0) remain = 0;

    double normalPart = (amount <= remain) ? amount : remain;
    double cardPart = amount - normalPart;

    ExecSql("BEGIN");
    bool ok = true;
    if (normalPart > 0) ok = InsertCash(occ, biz, normalPart, L"正常", L"");
    if (ok && cardPart > 0) ok = InsertCash(occ, biz, cardPart, L"点卡回款", L"超出当日销售额部分");

    if (ok) {
        ExecSql("COMMIT");
        SetDlgItemTextW(g_hwnd, IDC_CASH_EDIT, L"");
        if (cardPart > 0)
            swprintf_s(g_hint, L"%s · 回款 %.0f 万已登记（其中点卡回款 %.0f 万，业务日 %s）",
                       occ + 11, amount, cardPart, biz);
        else
            swprintf_s(g_hint, L"%s · 回款 %.0f 万已登记（业务日 %s）", occ + 11, amount, biz);
        RefreshUI();
        SetFocus(GetDlgItem(g_hwnd, IDC_SCAN_EDIT));
    } else {
        ExecSql("ROLLBACK");
        MessageBoxW(g_hwnd, L"回款写入失败，已回滚", L"数据库错误", MB_OK | MB_ICONERROR);
    }
}

// ---------------- 扫码框子类化（EDIT 会吞回车）----------------
static LRESULT CALLBACK ScanEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        if (!g_inBusy) {
            g_inBusy = true;
            wchar_t buf[256]; buf[0] = 0;
            GetWindowTextW(hWnd, buf, 256);
            TrimW(buf);
            SetWindowTextW(hWnd, L"");
            if (buf[0]) {
                HandleScan(buf);
                RefreshUI();
            }
            g_inBusy = false;
        }
        return 0;
    }
    if (msg == WM_CHAR && (wParam == VK_RETURN || wParam == 10 || wParam == 13)) return 0;
    return CallWindowProcW(g_oldScanProc, hWnd, msg, wParam, lParam);
}

// ---------------- 绘制 ----------------
static void DrawMatrix(HDC hdc, int x0, int y0, int cw, int ch, const int* qty) {
    HPEN penGrid = CreatePen(PS_SOLID, 1, RGB(196, 208, 224));
    HPEN penOld = (HPEN)SelectObject(hdc, penGrid);

    for (int r = 0; r <= 4; r++) {
        MoveToEx(hdc, x0, y0 + r * ch, NULL);
        LineTo(hdc, x0 + 4 * cw, y0 + r * ch);
    }
    for (int c = 0; c <= 4; c++) {
        MoveToEx(hdc, x0 + c * cw, y0, NULL);
        LineTo(hdc, x0 + c * cw, y0 + 4 * ch);
    }
    SelectObject(hdc, penOld);
    DeleteObject(penGrid);

    SetBkMode(hdc, TRANSPARENT);
    HFONT fB = g_fontBold ? g_fontBold : g_font22;
    HFONT fOld = (HFONT)SelectObject(hdc, fB);
    SetTextColor(hdc, RGB(28, 42, 66));

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int idx = r * 4 + c;
            wchar_t num[16];
            swprintf_s(num, L"%d", qty[idx]);
            SIZE sz; GetTextExtentPoint32W(hdc, num, (int)wcslen(num), &sz);
            int tx = x0 + c * cw + (cw - sz.cx) / 2;
            int ty = y0 + r * ch + (ch - sz.cy) / 2;
            if (qty[idx] == 0) SetTextColor(hdc, RGB(160, 172, 188));
            else SetTextColor(hdc, RGB(28, 42, 66));
            TextOutW(hdc, tx, ty, num, (int)wcslen(num));
        }
    }
    SelectObject(hdc, fOld);
}

static void OnPaint(HWND hWnd) {
    LOGS("paint enter");
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    RECT rc; GetClientRect(hWnd, &rc);
    LOGS("paint begun");

    if (g_bgImg) {
        Graphics g(hdc);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(g_bgImg, 0, 0, 900, 548);
    } else {
        RECT rh = { 0, 0, rc.right, 548 };
        HBRUSH br = CreateSolidBrush(RGB(245, 248, 252));
        FillRect(hdc, &rh, br);
        DeleteObject(br);
    }

    // 下半区（列表区）底色
    {
        RECT rb = { 0, 548, rc.right, rc.bottom };
        HBRUSH br2 = CreateSolidBrush(RGB(246, 249, 253));
        FillRect(hdc, &rb, br2);
        DeleteObject(br2);
    }

    // 两块面板底
    HBRUSH panel = CreateSolidBrush(RGB(255, 255, 255));
    RECT r1 = { 18, 180, 442, 486 };
    RECT r2 = { 458, 180, 882, 486 };
    FillRect(hdc, &r1, panel);
    DeleteObject(panel);

    HPEN penBox = CreatePen(PS_SOLID, 1, RGB(150, 170, 196));
    HPEN pOld = (HPEN)SelectObject(hdc, penBox);
    RoundRect(hdc, 18, 180, 442, 486, 10, 10);
    RoundRect(hdc, 458, 180, 882, 486, 10, 10);
    SelectObject(hdc, pOld);
    DeleteObject(penBox);

    // hmm: 圆角框与白色填充叠加，先填充后描边已足够
    DrawMatrix(hdc, 100, 250, 84, 56, g_onsale);
    LOGS("paint matrix L ok");
    DrawMatrix(hdc, 540, 250, 84, 56, g_storage);
    LOGS("paint matrix R ok");

    EndPaint(hWnd, &ps);
    LOGS("paint end");
}

// ---------------- 窗口过程 ----------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT:
            OnPaint(hWnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_CASH_BTN) { HandleCash(); return 0; }
            if (LOWORD(wParam) == IDC_PRICE_BTN) { OpenPriceDialog(hWnd); return 0; }
            break;

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(28, 42, 66));
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }

        case WM_SETFOCUS:
            SetFocus(GetDlgItem(hWnd, IDC_SCAN_EDIT));
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------------- 价格管理对话框 ----------------
#define IDC_PRICE_BTN  1005
#define IDC_PLIST      4001
#define IDC_PPRICE     4002
#define IDC_PCOST      4003
#define IDC_PSAVE      4004
#define IDC_PCLOSE     4005
#define IDC_PTIP       4006
static HWND g_priceDlg = NULL;
static int  g_priceSel = -1;

static void PriceDlgFillList(HWND hDlg) {
    HWND lv = GetDlgItem(hDlg, IDC_PLIST);
    if (!lv) return;
    SendMessageW(lv, LVM_DELETEALLITEMS, 0, 0);
    for (int i = 0; i < ITEM_COUNT; i++) {
        wchar_t b0[96], b1[32], b2[32], b3[32];
        swprintf_s(b0, L"%s（%s）", ITEM_NAMES[i], ITEM_LEVEL[i]);
        swprintf_s(b1, L"%.0f", g_price[i]);
        swprintf_s(b2, L"%.0f", g_cost[i]);
        double rate = (g_price[i] > 0) ? ((g_price[i] - g_cost[i]) / g_price[i]) : 0.0;
        swprintf_s(b3, L"%.1f%%", rate * 100.0);
        LvSet(lv, i, 0, b0);
        LvSet(lv, i, 1, b1);
        LvSet(lv, i, 2, b2);
        LvSet(lv, i, 3, b3);
    }
}

static void PriceDlgSave(HWND hDlg) {
    if (g_readOnly) {
        MessageBoxW(hDlg, L"当前连接的是真实库，只能查看，改价已禁用。\n测试请用 --test 启动（连 mhxy_test.db）。",
                    L"只读模式", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (g_priceSel < 0 || g_priceSel >= ITEM_COUNT) {
        MessageBoxW(hDlg, L"请先在列表里点选一个品类，再修改售价或成本。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }
    wchar_t tp[64] = { 0 }, tc[64] = { 0 };
    GetDlgItemTextW(hDlg, IDC_PPRICE, tp, 64);
    GetDlgItemTextW(hDlg, IDC_PCOST, tc, 64);
    double np = wcstod(tp, NULL), nc = wcstod(tc, NULL);
    if (np <= 0.0 || nc <= 0.0) {
        MessageBoxW(hDlg, L"售价和成本都必须大于 0（单位：万）。", L"提示", MB_OK | MB_ICONWARNING);
        return;
    }
    if (np < nc) {
        if (MessageBoxW(hDlg, L"售价低于成本，毛利为负，确定保存？", L"确认",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }

    wchar_t today[16]; TodayStr(today, 16);
    char nameA[64], todayA[24];
    WideToUtf8(ITEM_NAMES[g_priceSel], nameA, sizeof(nameA));
    WideToUtf8(today, todayA, sizeof(todayA));

    // 时间戳自己拼，不依赖 sqlite 的 datetime('now','localtime')
    char nowA[32];
    {
        SYSTEMTIME stm; GetLocalTime(&stm);
        sprintf_s(nowA, "%04d-%02d-%02d %02d:%02d:%02d",
                  (int)stm.wYear, (int)stm.wMonth, (int)stm.wDay,
                  (int)stm.wHour, (int)stm.wMinute, (int)stm.wSecond);
    }

    int ok = 1, failStep = 0;
    char errBuf[512] = { 0 };
    sqlite3_stmt* st = NULL;

    ExecSql("BEGIN");
    // 1) 当前价
    if (pPrepare(db, "UPDATE price SET price_wan=?, updated_at=? WHERE name=?", -1, &st, NULL) != SQLITE_OK) {
        ok = 0; failStep = 1;
    } else {
        pBindDouble(st, 1, np);
        pBindText(st, 2, todayA, -1, NULL);
        pBindText(st, 3, nameA, -1, NULL);
        if (pStep(st) != SQLITE_DONE) { ok = 0; failStep = 1; }
        pFinalize(st);
    }
    // 2) price 表里没有该品类则补插
    if (ok) {
        const char* s2 = "INSERT INTO price(name, price_wan, updated_at) SELECT ?, ?, ? WHERE NOT EXISTS (SELECT 1 FROM price WHERE name=?)";
        if (pPrepare(db, s2, -1, &st, NULL) != SQLITE_OK) {
            ok = 0; failStep = 2;
        } else {
            pBindText(st, 1, nameA, -1, NULL);
            pBindDouble(st, 2, np);
            pBindText(st, 3, todayA, -1, NULL);
            pBindText(st, 4, nameA, -1, NULL);
            if (pStep(st) != SQLITE_DONE) { ok = 0; failStep = 2; }
            pFinalize(st);
        }
    }
    // 3) 售价历史
    if (ok) {
        const char* s3 = "INSERT INTO price_history(name, price_wan, effective_date, created_at) VALUES(?,?,?,?)";
        if (pPrepare(db, s3, -1, &st, NULL) != SQLITE_OK) {
            ok = 0; failStep = 3;
        } else {
            pBindText(st, 1, nameA, -1, NULL);
            pBindDouble(st, 2, np);
            pBindText(st, 3, todayA, -1, NULL);
            pBindText(st, 4, nowA, -1, NULL);
            if (pStep(st) != SQLITE_DONE) { ok = 0; failStep = 3; }
            pFinalize(st);
        }
    }
    // 4) 成本历史
    if (ok) {
        const char* s4 = "INSERT INTO cost_history(name, cost_wan, effective_date, created_at) VALUES(?,?,?,?)";
        if (pPrepare(db, s4, -1, &st, NULL) != SQLITE_OK) {
            ok = 0; failStep = 4;
        } else {
            pBindText(st, 1, nameA, -1, NULL);
            pBindDouble(st, 2, nc);
            pBindText(st, 3, todayA, -1, NULL);
            pBindText(st, 4, nowA, -1, NULL);
            if (pStep(st) != SQLITE_DONE) { ok = 0; failStep = 4; }
            pFinalize(st);
        }
    }
    if (!ok && pErrMsg) {
        const char* m = pErrMsg(db);
        if (m) { strncpy(errBuf, m, sizeof(errBuf) - 1); }
    }
    if (ok) ExecSql("COMMIT"); else ExecSql("ROLLBACK");

    wchar_t tip[200];
    if (ok) {
        ReloadAll();
        PriceDlgFillList(hDlg);
        RefreshUI();
        swprintf_s(tip, L"已保存：%s %s → 售价 %.0f / 成本 %.0f 万（%s 起生效，历史不回溯）",
                   ITEM_NAMES[g_priceSel], ITEM_LEVEL[g_priceSel], np, nc, today);
    } else {
        wchar_t errW[512] = { 0 };
        if (errBuf[0]) Utf8ToWide(errBuf, errW, 512);
        swprintf_s(tip, L"保存失败（第 %d 步），已回滚：%s", failStep, errW[0] ? errW : L"未知错误");
    }
    SetDlgItemTextW(hDlg, IDC_PTIP, tip);
}

static LRESULT CALLBACK PriceDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)lParam;
        if (nh->idFrom == IDC_PLIST && nh->code == LVN_ITEMCHANGED) {
            NMLISTVIEW* nv = (NMLISTVIEW*)lParam;
            if ((nv->uNewState & LVIS_SELECTED) && !(nv->uOldState & LVIS_SELECTED)) {
                g_priceSel = nv->iItem;
                wchar_t b[32];
                swprintf_s(b, L"%.0f", g_price[g_priceSel]);
                SetDlgItemTextW(hDlg, IDC_PPRICE, b);
                swprintf_s(b, L"%.0f", g_cost[g_priceSel]);
                SetDlgItemTextW(hDlg, IDC_PCOST, b);
                wchar_t tip[128];
                swprintf_s(tip, L"已选中：%s（%s），改完点保存。", ITEM_NAMES[g_priceSel], ITEM_LEVEL[g_priceSel]);
                SetDlgItemTextW(hDlg, IDC_PTIP, tip);
            }
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_PSAVE)  { PriceDlgSave(hDlg); return 0; }
        if (LOWORD(wParam) == IDC_PCLOSE) { DestroyWindow(hDlg); return 0; }
        break;
    case WM_CLOSE:
        DestroyWindow(hDlg);
        return 0;
    case WM_DESTROY:
        g_priceDlg = NULL;
        return 0;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

static void OpenPriceDialog(HWND owner) {
    if (g_priceDlg && IsWindow(g_priceDlg)) {
        SetForegroundWindow(g_priceDlg);
        return;
    }
    const wchar_t* PCLS = L"MHXYPriceW";
    WNDCLASSEXW pwc;
    memset(&pwc, 0, sizeof(pwc));
    pwc.cbSize = sizeof(pwc);
    pwc.lpfnWndProc = PriceDlgProc;
    pwc.hInstance = GetModuleHandleW(NULL);
    pwc.hCursor = LoadCursor(NULL, IDC_ARROW);
    pwc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    pwc.lpszClassName = PCLS;
    RegisterClassExW(&pwc);

    RECT wr = { 0, 0, 660, 580 };
    AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);

    RECT orc; GetWindowRect(owner, &orc);
    int w = wr.right - wr.left, h = wr.bottom - wr.top;
    int x = orc.left + ((orc.right - orc.left) - w) / 2;
    int y = orc.top + ((orc.bottom - orc.top) - h) / 2;

    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, PCLS, L"价格管理",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                x, y, w, h, owner, NULL, GetModuleHandleW(NULL), NULL);
    if (!hDlg) return;
    g_priceDlg = hDlg;
    g_priceSel = -1;

    MakeStatic(hDlg, L"价格管理 · 保存即生效，历史销售不回溯", 16, 12, 628, 26, 4010, g_fontBold, SS_LEFT);

    HWND lv = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        16, 46, 628, 366, hDlg, (HMENU)(INT_PTR)IDC_PLIST, NULL, NULL);
    SendMessageW(lv, WM_SETFONT, (WPARAM)g_fontList, TRUE);
    SendMessageW(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, (LPARAM)(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES));
    {
        const wchar_t* cols[4] = { L"品类", L"售价(万)", L"成本(万)", L"毛利率" };
        int widths[4] = { 250, 126, 126, 110 };
        for (int i = 0; i < 4; i++) {
            LVCOLUMNW col;
            memset(&col, 0, sizeof(col));
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            col.fmt = (i == 0) ? LVCFMT_LEFT : LVCFMT_RIGHT;
            col.pszText = (LPWSTR)cols[i];
            col.cx = widths[i];
            SendMessageW(lv, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
        }
    }

    MakeStatic(hDlg, L"售价(万)", 16, 424, 96, 30, 4011, g_font22, SS_LEFT);
    HWND ep = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 118, 420, 140, 34, hDlg,
        (HMENU)(INT_PTR)IDC_PPRICE, NULL, NULL);
    SendMessageW(ep, WM_SETFONT, (WPARAM)g_font22, TRUE);

    MakeStatic(hDlg, L"成本(万)", 278, 424, 96, 30, 4012, g_font22, SS_LEFT);
    HWND ec = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 380, 420, 140, 34, hDlg,
        (HMENU)(INT_PTR)IDC_PCOST, NULL, NULL);
    SendMessageW(ec, WM_SETFONT, (WPARAM)g_font22, TRUE);

    HWND bSave = CreateWindowExW(0, L"BUTTON", L"保存",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 534, 420, 110, 34, hDlg,
        (HMENU)(INT_PTR)IDC_PSAVE, NULL, NULL);
    SendMessageW(bSave, WM_SETFONT, (WPARAM)g_font22, TRUE);

    MakeStatic(hDlg, L"先在列表里点选一个品类，再改售价 / 成本，点保存后立即对后续 C/E 毛利生效。",
              16, 466, 628, 26, 4013, g_font22, SS_LEFT);
    MakeStatic(hDlg, L"", 16, 492, 640, 26, IDC_PTIP, g_fontList, SS_LEFT);

    HWND bClose = CreateWindowExW(0, L"BUTTON", L"关闭",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 534, 528, 110, 34, hDlg,
        (HMENU)(INT_PTR)IDC_PCLOSE, NULL, NULL);
    SendMessageW(bClose, WM_SETFONT, (WPARAM)g_font22, TRUE);

    PriceDlgFillList(hDlg);
    // 默认选中第一行，省得每次都要先点
    {
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_STATE;
        it.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        it.state = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageW(lv, LVM_SETITEMSTATE, 0, (LPARAM)&it);
        SetFocus(lv);
    }

    // 模态：禁掉主窗交互，只跑对话框，关掉后返回
    EnableWindow(owner, FALSE);
    while (IsWindow(hDlg)) {
        MSG pm;
        if (GetMessageW(&pm, NULL, 0, 0) <= 0) break;
        TranslateMessage(&pm);
        DispatchMessageW(&pm);
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
}

// ---------------- 控件创建 ----------------
static HWND MakeStatic(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id, HFONT f, UINT style) {
    HWND c = CreateWindowExW(0, L"STATIC", text, WS_VISIBLE | WS_CHILD | style,
                             x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    if (c && f) SendMessage(c, WM_SETFONT, (WPARAM)f, TRUE);
    return c;
}

static void BuildUI(HWND hWnd) {
    LOGS("ui enter");
    // 顶部指标
    MakeStatic(hWnd, L"今日已售：0 件",      20,  16, 165, 34, 2001, g_font22, SS_CENTER);
    MakeStatic(hWnd, L"销售额：0 万",       190,  16, 165, 34, 2002, g_font22, SS_CENTER);
    MakeStatic(hWnd, L"已回款：0 万",       360,  16, 165, 34, 2003, g_font22, SS_CENTER);
    MakeStatic(hWnd, L"未回款：0 万",       530,  16, 165, 34, 2004, g_font22, SS_CENTER);
    MakeStatic(hWnd, L"在售 0 件 / 库存 0 件", 700, 16, 200, 34, 2005, g_font22, SS_CENTER);

    // 扫码行
    MakeStatic(hWnd, L"扫码", 20, 62, 56, 32, 2009, g_font22, SS_LEFT);
    HWND scanEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 78, 60, 800, 34, hWnd,
        (HMENU)(INT_PTR)IDC_SCAN_EDIT, NULL, NULL);
    SendMessage(scanEdit, WM_SETFONT, (WPARAM)g_font22, TRUE);
    g_oldScanProc = (WNDPROC)SetWindowLongPtrW(scanEdit, GWLP_WNDPROC, (LONG_PTR)ScanEditProc);
    LOGS("ui scan edit ok");

    // 回款行
    MakeStatic(hWnd, L"回款", 20, 104, 56, 32, 2010, g_font22, SS_LEFT);
    HWND cashEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 78, 102, 200, 34, hWnd,
        (HMENU)(INT_PTR)IDC_CASH_EDIT, NULL, NULL);
    SendMessage(cashEdit, WM_SETFONT, (WPARAM)g_font22, TRUE);
    HWND btn = CreateWindowExW(0, L"BUTTON", L"登记回款",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 290, 102, 132, 34, hWnd,
        (HMENU)(INT_PTR)IDC_CASH_BTN, NULL, NULL);
    SendMessage(btn, WM_SETFONT, (WPARAM)g_font22, TRUE);
    MakeStatic(hWnd, L"（单位：万）", 434, 104, 110, 32, 2011, g_font22, SS_LEFT);
    HWND btnPrice = CreateWindowExW(0, L"BUTTON", L"价格管理",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 556, 102, 130, 34, hWnd,
        (HMENU)(INT_PTR)IDC_PRICE_BTN, NULL, NULL);
    SendMessage(btnPrice, WM_SETFONT, (WPARAM)g_font22, TRUE);

    // 两个矩阵的标题与表头
    MakeStatic(hWnd, L"在售（摊位）", 24, 188, 200, 30, IDC_TITLE_L, g_fontBold, SS_LEFT);
    MakeStatic(hWnd, L"库存（行囊）", 464, 188, 200, 30, IDC_TITLE_R, g_fontBold, SS_LEFT);

    const wchar_t* parts[4] = { L"戒指", L"耳饰", L"手镯", L"佩饰" };
    for (int c = 0; c < 4; c++) {
        MakeStatic(hWnd, parts[c], 100 + c * 84, 220, 84, 28, 2100 + c, g_font22, SS_CENTER);
        MakeStatic(hWnd, parts[c], 540 + c * 84, 220, 84, 28, 2110 + c, g_font22, SS_CENTER);
    }
    const wchar_t* levels[4] = { L"60级", L"80级", L"100级", L"120级" };
    for (int r = 0; r < 4; r++) {
        MakeStatic(hWnd, levels[r], 24, 250 + r * 56, 72, 56, 2120 + r, g_font22, SS_CENTER | SS_CENTERIMAGE);
        MakeStatic(hWnd, levels[r], 464, 250 + r * 56, 72, 56, 2130 + r, g_font22, SS_CENTER | SS_CENTERIMAGE);
    }

    // 底部提示（居中，铺满客户区宽度）
    MakeStatic(hWnd, g_hint, 0, 500, 900, 30, IDC_HINT, g_font22, SS_CENTER);

    // 下半区：今日销售明细 / 今日回款记录
    MakeStatic(hWnd, L"今日销售明细（按 C/E 盘点差额入账）", 18, 552, 560, 26, IDC_TITLE_SALE, g_fontBold, SS_LEFT);
    MakeStatic(hWnd, L"今日回款记录", 592, 552, 290, 26, IDC_TITLE_CASH, g_fontBold, SS_LEFT);

    HWND lvSale = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        18, 580, 560, 186, hWnd, (HMENU)(INT_PTR)IDC_SALE_LIST, NULL, NULL);
    SendMessageW(lvSale, WM_SETFONT, (WPARAM)g_fontList, TRUE);
    SendMessageW(lvSale, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                (LPARAM)(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES));
    {
        const wchar_t* cols[5] = { L"品类", L"数量", L"单价(万)", L"金额(万)", L"毛利(万)" };
        // 列宽合计 556 = 控件宽 560 − WS_EX_CLIENTEDGE 左右各 2px，右边不留空白
        int widths[5] = { 220, 60, 88, 96, 92 };
        for (int i = 0; i < 5; i++) {
            LVCOLUMNW col;
            memset(&col, 0, sizeof(col));
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            col.fmt = (i == 0) ? LVCFMT_LEFT : LVCFMT_RIGHT;
            col.pszText = (LPWSTR)cols[i];
            col.cx = widths[i];
            SendMessageW(lvSale, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
        }
    }

    HWND lvCash = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        592, 580, 290, 186, hWnd, (HMENU)(INT_PTR)IDC_CASH_LIST, NULL, NULL);
    SendMessageW(lvCash, WM_SETFONT, (WPARAM)g_fontList, TRUE);
    SendMessageW(lvCash, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                (LPARAM)(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES));
    {
        const wchar_t* cols[3] = { L"时间", L"金额(万)", L"类型" };
        // 列宽合计 286 = 控件宽 290 − 边框 4px
        int widths[3] = { 96, 86, 104 };
        for (int i = 0; i < 3; i++) {
            LVCOLUMNW col;
            memset(&col, 0, sizeof(col));
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            col.fmt = (i == 0) ? LVCFMT_LEFT : (i == 1 ? LVCFMT_RIGHT : LVCFMT_LEFT);
            col.pszText = (LPWSTR)cols[i];
            col.cx = widths[i];
            SendMessageW(lvCash, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
        }
    }

    LOGS("ui done");
}

// ---------------- 入口 ----------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    LOGS("start");
    GdiplusStartupInput gsi;
    ULONG_PTR gpToken = 0;
    GdiplusStartup(&gpToken, &gsi, NULL);
    LOGS("gdiplus ok");

    // exe 所在目录
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t* slash = wcsrchr(exePath, L'\\');
    if (slash) slash[1] = 0;
    SetCurrentDirectoryW(exePath);
    LOGS("exedir ok");

    // 背景图：先找桌面运行目录，再找程序同目录
    const wchar_t* bgCandidates[2] = {
        L"C:\\Users\\wangwill\\Desktop\\入库上架二维码工具\\背景图片.png",
        L"背景图片.png"
    };
    for (int i = 0; i < 2 && !g_bgImg; i++) {
        if (PathFileExistsW(bgCandidates[i])) {
            Image* img = Image::FromFile(bgCandidates[i]);
            if (img && img->GetLastStatus() == Ok) g_bgImg = img;
            else delete img;
        }
    }

    // 数据库选择：默认一律连测试库；真实库必须显式 --real，且强制只读
    wchar_t dbPath[MAX_PATH];
    swprintf_s(dbPath, L"%s..\\mhxy_test.db", exePath);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool wantReal = false;
    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--test") == 0)      swprintf_s(dbPath, L"%s..\\mhxy_test.db", exePath);
        else if (wcscmp(argv[i], L"--real") == 0) { wantReal = true; swprintf_s(dbPath, L"%s..\\mhxy.db", exePath); }
        else if (wcscmp(argv[i], L"--db") == 0 && i + 1 < argc) swprintf_s(dbPath, L"%s", argv[i + 1]);
        else if (wcscmp(argv[i], L"--sim-ticket") == 0 && i + 1 < argc) wcsncpy_s(g_simTicket, 128, argv[i + 1], _TRUNCATE);
    }
    if (argv) LocalFree(argv);
    (void)wantReal;

    // 只读判定：凡不是 mhxy_test.db 的库，一律只读，禁止任何写入
    if (wcsstr(dbPath, L"mhxy_test.db") == NULL) {
        g_readOnly = true;
        wcsncpy_s(g_dbTag, 64, L"真实库", _TRUNCATE);
        wchar_t ask[512];
        swprintf_s(ask, L"即将连接真实数据库（只读）：\n%s\n\n本程序不会对该库做任何写入，\n仅用于查看。继续打开吗？", dbPath);
        if (MessageBoxW(NULL, ask, L"只读打开真实库", MB_YESNO | MB_ICONWARNING) != IDYES) return 0;
    } else {
        g_readOnly = false;
        wcsncpy_s(g_dbTag, 64, L"测试库", _TRUNCATE);
    }

    wcsncpy_s(g_dbPath, MAX_PATH, dbPath, _TRUNCATE);
    LOGS("args ok");

    if (!DbInit(dbPath)) {
        LOGS("db init FAILED");
        MessageBoxW(NULL, L"数据库打开失败：请确认 sqlite3.dll 与 wb.exe 同目录，且数据库文件存在。",
                    L"启动失败", MB_OK | MB_ICONERROR);
        return 1;
    }
    LOGS("db open ok");
    ReloadAll();
    LOGS("reload ok");

    g_font22 = CreateFontW(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"微软雅黑");
    g_fontBold = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"微软雅黑");
    g_fontList = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"微软雅黑");

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    const wchar_t* CLS = L"MHXYWorkbenchW";
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = CLS;
    RegisterClassExW(&wc);
    LOGS("class ok");

    RECT wr = { 0, 0, 900, 780 };
    AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    HWND hWnd = CreateWindowExW(0, CLS, L"灵饰经营工作台",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                wr.right - wr.left, wr.bottom - wr.top,
                                NULL, NULL, hInstance, NULL);
    if (!hWnd) { LOGS("createwindow FAILED"); return 1; }
    g_hwnd = hWnd;
    LOGS("window ok");

    BuildUI(hWnd);
    LOGS("ui ok");
    // 居中
    {
        RECT rc; GetWindowRect(hWnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
        int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
        SetWindowPos(hWnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    ShowWindow(hWnd, (nCmdShow == SW_HIDE) ? SW_SHOWNORMAL : nCmdShow);
    UpdateWindow(hWnd);
    LOGS("show ok");
    SetFocus(GetDlgItem(hWnd, IDC_SCAN_EDIT));
    LOGS("refresh start");
    RefreshUI();
    LOGS("refresh ok");

    // 命令行自测入口：--sim-ticket <43位票>，等价于扫码一次（走同一条 HandleScan 通路）
    if (g_simTicket[0]) {
        LOGS("sim-ticket run");
        HandleScan(g_simTicket);
        RefreshUI();
        LOGS("sim-ticket done");
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    LOGS("loop exit");

    if (g_font22) DeleteObject(g_font22);
    if (g_fontBold) DeleteObject(g_fontBold);
    if (g_bgImg) delete g_bgImg;
    if (db && pClose) pClose(db);
    if (hSqliteDll) FreeLibrary(hSqliteDll);
    GdiplusShutdown(gpToken);
    return 0;
}

// ============================================================
// 入库上架二维码打印工具（v3 无复选框版）
// 第一部分：选项卡 入库(I) / 上架(O) / 盘点(默认 C，勾选“日结盘点(E)”则为 E)
// 第二部分：4排×4列 = 16格，每格 = 灵饰名称 + 2位数字输入
//          （填了数字即有效，按行序自动读取，1位自动前置0）
// 第三部分：按钮 重置 / 保存 / 打印 / 退出
// 第四部分：实时二维码预览 + 内容原文展示
// 数据契约：时间(10位 MMDDHHMMSS) + I/O(1位) + 16组两位数字直接相连（不足前置补0，无分隔符）
//          无任何输入时显示 16 格全为 00 的二维码
// 构建：g++ -O2 -municode -mwindows -std=c++17 -DNDEBUG
//       -finput-charset=UTF-8 -fexec-charset=UTF-8
//       -Ithird_party/qrcodegen src/main.cpp third_party/qrcodegen/qrcodegen.cpp
//       -o dist/入库上架二维码工具.exe -lgdiplus -lgdi32 -luser32 -lcomdlg32 -lwinspool
// ============================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>              // SetWindowSubclass（单选自绘透明化）
#include <ocidl.h>               // MinGW：PROPID 定义
#include <commdlg.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include "qrcodegen.hpp"
#include "print_page.hpp"

#define IDI_APP 100   // 应用图标资源（app.rc：app.ico）

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "winspool.lib")

using qrcodegen::QrCode;

// ---------- 控件 ID ----------
#define IDC_TAB_OP     1200          // 入库/上架 选项卡（系统 Tab 控件）
#define IDC_RADIO_IN   1101
#define IDC_RADIO_OUT  1102
#define IDC_CHK_BOTH   1103          // “入库并同时上架”复选框（仅入库选中时显示）
#define IDC_CHK_EOD    1104          // “日结盘点(E)”复选框（仅盘点标签显示；不勾选=中途盘点 C）
#define IDC_TAB_CHECK  1105          // 盘点标签的内部枚举值（非真实控件）
#define WM_APP_TABCHANGED (WM_APP + 10)   // 选项卡切换的延后处理消息（不在 TCN_SELCHANGE 栈内动控件）
#define IDC_SUM_LABEL  1201          // 数量合计标签（入库合计/上架合计 N 件）
#define IDC_EDT_BASE   1300          // +1..16
#define IDC_PLUS_BASE  1500          // 数量 + 步进按钮（+0..15）
#define IDC_MINUS_BASE 1600          // 数量 - 步进按钮（+0..15）
#define IDC_RESET      1401
#define IDC_PRINT      1403
#define IDC_EXIT       1404
#define IDC_BTN_HELP   1701          // “帮助”按钮（按钮行最右）
#define IDC_HELP_CLOSE 1801          // 帮助窗口“关闭”按钮

// ---------- 数据契约 ----------
static const int SLOT_ROWS = 4;
static const int SLOT_COLS = 4;
static const int SLOT_TOTAL = 16;

// ---------- 布局 ----------
const int CW = 620;                                  // 客户区宽度（随窗口整体收窄，格子行右缘601+边距19）
const int ROW_LABEL_X = 27, ROW_LABEL_W = 56;        // 行标签
const int GRID_X0 = 89, GRID_Y0 = 126;                // 网格起点（上移 6px，为下方两部分留出更大间距）
const int CELL_W = 128, CELL_H = 36;                 // 列宽=98内容+30px列间距（约2字符宽；名称48+输入34+步进16）
const int NAME_W = 48, EDT_W = 34, EDT_H = 24;       // 名称 / 输入框
const int SPIN_W = 16, SPIN_GAP = 2;                 // + / - 步进按钮（上下堆叠）
const int SPIN_H = 11, SPIN_VGAP = 2;                // 上/下按钮高度与纵向间隙
const int RADIO_W = 120, RADIO_GAP = 24, RADIO_H = 30; // 单选组（自绘，无灰底）
const int BTN_Y = 346, BTN_GAP = 12;                 // 按钮行（上移10px：346-376；背景玻璃台保持原位232-356不动）
const int CHK_Y = 296, CHK_W = 135, CHK_H = 24;      // “入库并同时上架”复选框（下移10px：296-320）
const int QUIET_ZONE = 4;                            // 打印/存档位图渲染的静区（界面预览二维码已取消）

// ---------- 灵饰名称表（4等级 × 4品类） ----------
static const wchar_t* ROW_LABELS[SLOT_ROWS] = { L"60级", L"80级", L"100级", L"120级" };
static const wchar_t* ITEM_NAMES[SLOT_TOTAL] = {
    L"枫华戒", L"翠叶环", L"香木镯", L"芝兰佩",
    L"芙蓉戒", L"明月珰", L"翡玉镯", L"逸云佩",
    L"金麟绕", L"玉蝶翩", L"墨影扣", L"莲音玦",
    L"悦碧水", L"点星芒", L"花映月", L"相思染"
};

// ---------- 全局状态 ----------
struct QrImage {
    bool valid = false;
    int size = 0;
    std::vector<bool> modules;
};
static QrImage      g_qr;
static std::string  g_content;              // 二维码原文
static HWND         g_edits[SLOT_TOTAL];
static HWND         g_plus[SLOT_TOTAL], g_minus[SLOT_TOTAL]; // 数量步进按钮
static HWND         g_tabOp = nullptr;              // 入库/上架 选项卡（系统 Tab 控件）
static int          g_radioSel = IDC_RADIO_IN;   // 当前选项卡：IDC_RADIO_IN(入库) / IDC_RADIO_OUT(上架)
static HWND         g_chkBoth = nullptr;         // “入库并同时上架”复选框
static HWND         g_chkEod = nullptr;          // “日结盘点(E)”复选框
static bool         g_eodChecked = false;        // 盘点标签下：勾选=日结盘点 E，未勾选=中途盘点 C
static HWND         g_sumLabel = nullptr;        // 数量合计标签（入库合计/上架合计 N 件）
static bool         g_bothChecked = false;       // 是否同时生成并打印上架二维码
static WNDPROC      g_origEditProc = nullptr;
static Gdiplus::Image* g_bgImage = nullptr; // 窗口背景图（EXE同目录\背景图片.png）
static Gdiplus::Image* g_topImage = nullptr; // 顶部横幅（EXE同目录\top.png）

// ---------- 交互契约（已定稿 7 条）全局状态 ----------
static HWND          g_btnReset = nullptr, g_btnPrint = nullptr;        // 受全零规则控制的按钮
static HWND          g_btnHelp = nullptr;                                                   // 帮助按钮
static HFONT         g_btnFont = nullptr;                                                   // 命令按钮粗体字体（自绘文字用）
static bool          g_allZero = true;       // 当前是否全零（决定按钮可用性与二维码灰度）
static DWORD         g_focusLostAt = 0;      // 失焦时刻（自动置零计时）
static bool          g_timerRunning = false; // 失焦计时器是否在跑
static bool          g_inDialog = false;     // 模态对话框/帮助窗口打开中（暂停失焦计时）
static std::vector<std::string> g_savedSigs; // 本次运行已保存过的签名集合（打印自动存档判定）
static std::wstring  g_saveDir;              // 默认保存目录（EXE同目录\保存单据\）

// ---------- 工具函数 ----------

// 本机时间 -> MMDDHHMMSS（二维码临时用，不含年份；系统会自动保存图片）
static std::string FormatTimeNow() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d%02d%02d%02d%02d",
                  st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::string(buf);
}

static char CurrentOp() {
    if (g_radioSel == IDC_RADIO_IN) return 'I';
    if (g_radioSel == IDC_RADIO_OUT) return 'O';
    return g_eodChecked ? 'E' : 'C';          // 盘点标签：默认 C，勾选“日结盘点”为 E
}
static std::string OpName(char op) {
    if (op == 'I') return "入库";
    if (op == 'O') return "上架";
    if (op == 'C') return "中途盘点";
    return "日结盘点";
}

// 读取格子输入（返回纯数字串；空格子返回空串）
// 注意：buf 必须清零——GetWindowTextW 在文本为空时一个字符都不写，
// 未初始化的缓冲会把栈上残留当成内容读出来（表现为空格子随机变成 03/05/06 等）。
static std::string SlotValue(int i) {
    wchar_t buf[8] = {0};
    int n = GetWindowTextW(g_edits[i], buf, 8);
    if (n <= 0) return "";                    // 格子里没有文字 -> 明确返回空
    char tmp[8] = {0};
    WideCharToMultiByte(CP_ACP, 0, buf, n, tmp, (int)sizeof(tmp), nullptr, nullptr);
    std::string digits;                       // 只保留半角数字，最多 2 位
    for (size_t k = 0; tmp[k] && digits.size() < 2; ++k)
        if (tmp[k] >= '0' && tmp[k] <= '9') digits += tmp[k];
    return digits;
}

// 按指定操作符组装内容：行序读取，填了即有效，1位自动前置0
// 16 组数字始终完整输出，未填的格子补 00（无输入时即 16 个 00）
static std::string BuildContentFor(char op) {
    std::string content = FormatTimeNow();
    content += op;

    std::string joined;
    for (int i = 0; i < SLOT_TOTAL; ++i) {
        std::string v = SlotValue(i);
        if (v.empty()) v = "00";                     // 未填 -> 补 00
        else if (v.size() == 1) v = "0" + v;          // 不足两位前置补0
        joined += v;
    }
    // 契约兜底：16 组必须正好 32 位（合计 43 位），异常一律按两位对齐修正
    if (joined.size() < (size_t)(2 * SLOT_TOTAL))
        joined.append((size_t)(2 * SLOT_TOTAL) - joined.size(), '0');
    else if (joined.size() > (size_t)(2 * SLOT_TOTAL))
        joined.resize((size_t)(2 * SLOT_TOTAL));
    return content + joined;
}

// 按当前选中操作生成内容
static std::string BuildContent() { return BuildContentFor(CurrentOp()); }

// ---------- 交互契约辅助 ----------

// 数据签名：操作码 + 16 组两位数字（不含时间戳）——防重复保存/打印补存的判定口径
static std::string DataSignature(char op) {
    std::string s;
    s += op;
    for (int i = 0; i < SLOT_TOTAL; ++i) {
        std::string v = SlotValue(i);
        if (v.empty()) v = "00";
        else if (v.size() == 1) v = "0" + v;
        s += v;
    }
    return s;
}

// 是否全零：16 框全为 0 或空（空框按零计）
static bool IsAllZero() {
    for (int i = 0; i < SLOT_TOTAL; ++i) {
        int v = 0;
        for (char ch : SlotValue(i))
            if (ch >= '0' && ch <= '9') v = v * 10 + (ch - '0');
        if (v != 0) return false;
    }
    return true;
}

// 打印按钮文字随票种变化：按下去之前就知道自己要打什么票（防误操作）
static void UpdatePrintBtnLabel() {
    if (!g_btnPrint) return;
    const wchar_t* t = (g_radioSel == IDC_RADIO_IN)  ? L"打印入库"
                     : (g_radioSel == IDC_RADIO_OUT) ? L"打印上架"
                     : (g_eodChecked ? L"打印日结" : L"打印盘点");
    SetWindowTextW(g_btnPrint, t);
}

// 刷新受全零规则控制的按钮（重置/打印）+ 灰度标志
// 盘点票（C/E）允许 16 格全零：营业中/日结把在售清空是有效盘点结果
static void RefreshButtons(HWND hwnd) {
    g_allZero = IsAllZero();
    bool checkTab = (g_radioSel == IDC_TAB_CHECK);   // 盘点标签
    bool canUse = checkTab || !g_allZero;
    if (g_btnReset) EnableWindow(g_btnReset, canUse);
    if (g_btnPrint) EnableWindow(g_btnPrint, canUse);
    UpdatePrintBtnLabel();
    (void)hwnd;
}

// EXE 所在目录（带尾斜杠）
static std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    wchar_t* s = wcsrchr(buf, L'\\');
    if (s) *(s + 1) = 0;
    return std::wstring(buf);
}

// 默认保存目录：EXE 同目录\保存单据\（不存在则创建）；保存/补存统一指向此处
static const std::wstring& GetSaveDir() {
    if (g_saveDir.empty()) {
        g_saveDir = GetExeDir();
        g_saveDir += L"保存单据\\";
        CreateDirectoryW(g_saveDir.c_str(), nullptr);
    }
    return g_saveDir;
}

// 将内容编码进指定 QrImage（异常安全）
static bool EncodeQrInto(const std::string& content, QrImage& qr) {
    auto fill = [&](const QrCode& q) {
        qr.valid = true;
        qr.size = q.getSize();
        qr.modules.assign((size_t)qr.size * qr.size, false);
        for (int y = 0; y < qr.size; ++y)
            for (int x = 0; x < qr.size; ++x)
                qr.modules[(size_t)y * qr.size + x] = q.getModule(x, y);
    };
    // 43 位只含 0-9 与 I/O/C/E，全部落在 QR「字母数字模式」字符集内。
    // 用 alphanumeric 分段取代默认的 byte 模式：同内容版本从 41×41 降到 25×25，
    // 同一物理宽度下每个模块大近一倍，58mm 小票上扫码盒才不容易在边缘丢字符。
    // 纠错等级固定 MEDIUM、boostEcl=false——提升 ECC 会把版本重新顶上去，模块又变小。
    try {
        std::vector<qrcodegen::QrSegment> segs;
        segs.push_back(qrcodegen::QrSegment::makeAlphanumeric(content.c_str()));
        QrCode q = QrCode::encodeSegments(segs, QrCode::Ecc::MEDIUM, 1, 40, -1, false);
        fill(q);
        return true;
    } catch (const std::exception&) {}
    try {   // 回退：内容万一含字母数字模式之外的字符，仍走字节模式
        QrCode q = QrCode::encodeText(content.c_str(), QrCode::Ecc::MEDIUM);
        fill(q);
        return true;
    } catch (const std::exception&) {
        qr.valid = false;
        return false;
    }
}

// 取证日志：打印瞬间把 16 格的原始文本 + 最终内容落到 _slots_debug.txt。
// 用于判定"二维码里出现莫名数字"时，到底是格子里真有内容，还是程序读错了控件。
static void DbgLogSlots(const char* tag) {
    std::string line = std::string(tag) + " ";
    for (int i = 0; i < SLOT_TOTAL; ++i) {
        wchar_t w[16] = {0};
        int n = GetWindowTextW(g_edits[i], w, 16);
        char u8[64] = {0};
        if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, n, u8, 63, nullptr, nullptr);
        line += "[" + std::to_string(i + 1) + "]=<" + u8 + "> ";
    }
    line += "| content=" + BuildContent() + "\r\n";

    std::wstring path = GetSaveDir() + L"\\_slots_debug.txt";
    HANDLE hf = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0;
    WriteFile(hf, line.c_str(), (DWORD)line.size(), &wr, nullptr);
    CloseHandle(hf);
}

// 合计标签：按当前标签显示“入库合计/上架合计/盘点合计 N 件”
static void UpdateTotalLabel() {
    if (!g_sumLabel) return;
    int total = 0;
    for (int i = 0; i < SLOT_TOTAL; ++i) total += atoi(SlotValue(i).c_str());
    const wchar_t* lab = (g_radioSel == IDC_RADIO_IN) ? L"入库"
                       : (g_radioSel == IDC_RADIO_OUT) ? L"上架" : L"盘点";
    wchar_t buf[64];
    swprintf(buf, 64, L"%ls合计 %d 件", lab, total);
    SetWindowTextW(g_sumLabel, buf);
}

// 实时预览：任何输入变化时调用（生成二维码数据供打印/存档用）+ 刷新按钮可用性
static void UpdatePreview(HWND hwnd) {
    UpdateTotalLabel();
    std::string content = BuildContent();
    if (EncodeQrInto(content, g_qr)) g_content = content;
    RefreshButtons(hwnd);
}

// 两个复选框按标签显隐：入库→“入库并同时上架”；上架→都隐藏；盘点→“日结盘点(E)”
static void UpdateChkVisibility() {
    bool inTab  = (g_radioSel == IDC_RADIO_IN);
    bool chkTab = (g_radioSel == IDC_TAB_CHECK);
    if (g_chkBoth) ShowWindow(g_chkBoth, inTab ? SW_SHOW : SW_HIDE);
    if (g_chkEod)  ShowWindow(g_chkEod,  chkTab ? SW_SHOW : SW_HIDE);
}

// 重置：清空全部输入为0 + 取消两个复选框勾选
// keepTab=true（打印成功后、手动重置）：保留当前选中的操作标签（入库/上架/盘点）不切回，避免打完上架单又跳回入库
// keepTab=false（失焦超时自动置零）：切回入库，等同“放太久重置一切”
static void DoReset(HWND hwnd, bool keepTab = true) {
    if (!keepTab) {
        g_radioSel = IDC_RADIO_IN;
        TabCtrl_SetCurSel(g_tabOp, 0);
    }
    g_bothChecked = false;
    g_eodChecked = false;
    if (g_chkBoth) SendMessageW(g_chkBoth, BM_SETCHECK, BST_UNCHECKED, 0);   // 系统复选框自带重绘
    if (g_chkEod)  SendMessageW(g_chkEod,  BM_SETCHECK, BST_UNCHECKED, 0);
    UpdateChkVisibility();
    for (int i = 0; i < SLOT_TOTAL; ++i)
        SetWindowTextW(g_edits[i], L"0");
    UpdateTotalLabel();   // 显式刷新合计：置零后合计必须归 0，不能留上一批的 40/89
    UpdatePreview(hwnd);
}

// 步进按钮：数量 ±1（范围 0..99），随后刷新二维码
static void SpinEdit(int i, int delta) {
    wchar_t buf[8] = { 0 };
    GetWindowTextW(g_edits[i], buf, 8);
    int v = 0;
    for (const wchar_t* p = buf; *p; ++p) {
        if (*p < L'0' || *p > L'9') break;
        v = v * 10 + (*p - L'0');
    }
    v += delta;
    if (v < 0) v = 0;
    if (v > 99) v = 99;
    wchar_t t[8];
    wsprintfW(t, L"%d", v);
    SetWindowTextW(g_edits[i], t);
    UpdatePreview(GetParent(g_edits[i]));
}

// ---------- 二维码渲染 ----------

// 渲染成带静区的 32bpp 位图（白底，供保存/打印使用）
static HBITMAP MakeQrBitmapFor(const QrImage& qr, int modulePx) {
    if (!qr.valid) return nullptr;
    const int total = qr.size + 2 * QUIET_ZONE;
    const int w = total * modulePx, h = w;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!hbmp || !bits) return hbmp;

    uint32_t* px = (uint32_t*)bits;
    for (int i = 0; i < w * h; ++i) px[i] = 0xFFFFFFFFu;
    for (int my = 0; my < qr.size; ++my) {
        for (int mx = 0; mx < qr.size; ++mx) {
            if (!qr.modules[(size_t)my * qr.size + mx]) continue;
            const int bx = (mx + QUIET_ZONE) * modulePx;
            const int by = (my + QUIET_ZONE) * modulePx;
            for (int dy = 0; dy < modulePx; ++dy)
                for (int dx = 0; dx < modulePx; ++dx)
                    px[(size_t)(by + dy) * w + (bx + dx)] = 0xFF000000u;
        }
    }
    return hbmp;
}

static HBITMAP MakeQrBitmap(int modulePx) { return MakeQrBitmapFor(g_qr, modulePx); }

// 取 PNG 编码器 CLSID
static int GetPngEncoderClsid(CLSID* clsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    std::vector<Gdiplus::ImageCodecInfo> infos(size / sizeof(Gdiplus::ImageCodecInfo));
    Gdiplus::GetImageEncoders(num, size, infos.data());
    for (UINT i = 0; i < num; ++i)
        if (wcscmp(infos[i].MimeType, L"image/png") == 0) { *clsid = infos[i].Clsid; return 0; }
    return -1;
}

// 默认文件名：操作_时间_数字串.png
static std::wstring DefaultFileNameFor(const std::string& c) { // c = 10位时间(MMDDHHMMSS) + 操作码1位 + 数字串
    if (c.size() < 11) return L"未命名.png";
    std::string t = c.substr(0, 10);
    std::string op = OpName(c[10]);
    std::string nums = (c.size() > 11) ? c.substr(11) : "";
    for (size_t i = 0; i < nums.size(); ++i)
        if ((unsigned char)nums[i] >= 0x80) {    // 逗号(多字节UTF-8)换成下划线
            nums.replace(i, 3, "_");             // UTF-8 全角逗号占3字节
        }
    std::string name = op + "_" + t;
    if (!nums.empty()) name += "_" + nums;
    name += ".png";
    // UTF-8 -> UTF-16 正确转换（避免中文乱码）
    std::wstring w;
    int len = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), (int)name.size(), nullptr, 0);
    w.resize(len);
    MultiByteToWideChar(CP_UTF8, 0, name.c_str(), (int)name.size(), &w[0], len);
    return w;
}

static std::wstring DefaultFileName() { return DefaultFileNameFor(g_content); }

// 渲染整页单据位图：固定 80mm 版（80×120mm @203dpi = 639×959），白底
// op: 'I'/'O' 决定标题“入库明细/上架明细”；qrBmp: 二维码位图（渲染时自动缩放）
static HBITMAP MakePageBitmap(char op, HBITMAP qrBmp) {
    const int W = 639, H = 959, DPI = 203;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = W;
    bmi.bmiHeader.biHeight = -H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    HDC sdc = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(sdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, sdc);
    if (!hbmp) return nullptr;

    HDC mdc = CreateCompatibleDC(nullptr);
    HGDIOBJ o = SelectObject(mdc, hbmp);
    RECT rc = { 0, 0, W, H };
    HBRUSH wb = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(mdc, &rc, wb);
    DeleteObject(wb);
    int counts[SLOT_TOTAL];
    for (int i = 0; i < SLOT_TOTAL; ++i) counts[i] = atoi(SlotValue(i).c_str());
    RenderPrintPage(mdc, DPI, DPI, W, H, qrBmp, op, counts, ITEM_NAMES);
    SelectObject(mdc, o);
    DeleteDC(mdc);
    return hbmp;
}

// 保存整页 PNG 到指定路径（不弹对话框）。成功则更新防重复记录 + 已保存签名集合 + 最近文件
static bool SavePageToFile(const std::wstring& path, char op, HBITMAP qrBmp) {
    HBITMAP page = MakePageBitmap(op, qrBmp);
    if (!page) return false;
    CLSID clsid;
    bool ok = false;
    if (GetPngEncoderClsid(&clsid) == 0) {
        Gdiplus::Bitmap bmp(page, nullptr);
        ok = (bmp.Save(path.c_str(), &clsid, nullptr) == Gdiplus::Ok);
    }
    DeleteObject(page);
    if (ok) {
        std::string sig = DataSignature(op);
        bool found = false;
        for (auto& s : g_savedSigs) if (s == sig) { found = true; break; }
        if (!found) g_savedSigs.push_back(sig);
    }
    return ok;
}

// 打印一页：文本紧凑排版（标题+8行文本+二维码+打印时间），58mm/80mm 自适应
static void PrintPage(HDC hdc, const QrImage& qr, char op) {
    const int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    const int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
    const int pageW = GetDeviceCaps(hdc, HORZRES);
    const int pageH = GetDeviceCaps(hdc, VERTRES);

    int counts[SLOT_TOTAL];
    for (int i = 0; i < SLOT_TOTAL; ++i) {
        std::string v = SlotValue(i);
        counts[i] = atoi(v.c_str());
    }

    HBITMAP hbmp = MakeQrBitmapFor(qr, 8);
    RenderPrintPage(hdc, dpiX, dpiY, pageW, pageH, hbmp, op, counts, ITEM_NAMES);
    if (hbmp) DeleteObject(hbmp);
}

// 打印标签；勾选“入库并同时上架”且当前为入库时，打印2页（入库+上架）
static void PrintQr(HWND hwnd) {
    // 打印内容一律在点击瞬间按当前格子重新生成，绝不使用界面缓存——
    // 缓存与界面一旦不同步，就会打出与所填数字不符的二维码，且存档文件名跟着错
    std::string curContent = BuildContentFor(CurrentOp());
    QrImage curQr;
    if (!EncodeQrInto(curContent, curQr)) {
        MessageBoxW(hwnd, L"当前内容无法生成二维码，请检查输入。", L"提示", MB_ICONERROR | MB_OK);
        return;
    }
    g_content = curContent;   // 顺手同步缓存，供其它分支使用
    g_qr = curQr;
    DbgLogSlots("PRINT");     // 取证：打印瞬间 16 格原文 + 最终内容落日志

    // 盘点票全零必须二次确认（默认按钮=否）：全零多属误操作，高价值件卖得慢、极少真的清空
    char curOp = CurrentOp();
    if ((curOp == 'C' || curOp == 'E') && IsAllZero()) {
        const wchar_t* what = (curOp == 'E') ? L"日结盘点(E)" : L"中途盘点(C)";
        std::wstring msg = std::wstring(L"本次为「") + what +
            L"」，16 格全为 0 件。\r\n\r\n全零多数是误操作（高价值件卖得慢，正常很少全部卖空）。\r\n确认确实要打印这张全零盘点票吗？";
        g_inDialog = true;       // 确认弹窗期间暂停失焦自动置零计时
        int r = MessageBoxW(hwnd, msg.c_str(), L"全零盘点票确认",
                            MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
        g_inDialog = false;
        if (r != IDYES) return;  // 未确认则既不打印也不存档
    }

    bool dual = (CurrentOp() == 'I' && g_bothChecked);
    QrImage q2;
    bool q2ok = dual && EncodeQrInto(BuildContentFor('O'), q2);

    // 打印前自动补存：本次运行未保存过的数据（按数据签名）静默保存一份整页到默认目录
    auto ensureSaved = [&](char op) {
        std::string sig = DataSignature(op);
        for (auto& s : g_savedSigs) if (s == sig) return;   // 已保存过，跳过
        std::string content = BuildContentFor(op);          // 现算，不用 g_content
        QrImage qtmp;
        if (!EncodeQrInto(content, qtmp)) return;
        HBITMAP qb = MakeQrBitmapFor(qtmp, 8);
        if (!qb) return;
        SavePageToFile(GetSaveDir() + DefaultFileNameFor(content), op, qb);
        DeleteObject(qb);
    };
    ensureSaved(CurrentOp());
    if (q2ok) ensureSaved('O');

    g_inDialog = true;   // 打印对话框打开期间暂停失焦自动置零计时
    PRINTDLGW pd = {};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = hwnd;
    pd.Flags = PD_RETURNDC | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
    pd.nFromPage = 1; pd.nToPage = q2ok ? 2 : 1; pd.nMinPage = 1; pd.nMaxPage = q2ok ? 2 : 1;
    BOOL got = PrintDlgW(&pd);
    g_inDialog = false;
    if (!got) return;

    HDC hdc = pd.hDC;
    DOCINFOW di = {};
    di.cbSize = sizeof(di);
    di.lpszDocName = L"入库上架二维码";
    if (StartDocW(hdc, &di) <= 0) { DeleteDC(hdc); return; }

    StartPage(hdc);
    PrintPage(hdc, curQr, CurrentOp());
    if (q2ok) {
        EndPage(hdc);
        StartPage(hdc);
        PrintPage(hdc, q2, 'O');
    }
    EndPage(hdc);
    EndDoc(hdc);
    DeleteDC(hdc);

    // 打印作业提交成功 -> 自动置零（等同手动重置；打印按钮随后由全零规则自动禁用）
    DoReset(hwnd);
}

// ---------- 帮助窗口（前置声明） ----------
static void ShowHelp(HWND hwndParent);

// ---------- 编辑框子类：只允许数字，最多2位 ----------
static LRESULT CALLBACK SlotEditProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CHAR) {
        // 仅接受半角数字 0-9、退格、回车；全角数字及其他字符一律拒绝
        if (wParam == VK_BACK || wParam == VK_RETURN)
            return CallWindowProcW(g_origEditProc, h, msg, wParam, lParam);
        if (wParam < '0' || wParam > '9') return 0;
    }
    if (msg == WM_PASTE) {
        // 粘贴时过滤：只保留半角数字（最多2位），全角/字母等自动剔除
        if (!OpenClipboard(h)) return 0;
        HANDLE hd = GetClipboardData(CF_UNICODETEXT);
        if (!hd) { CloseClipboard(); return 0; }
        const wchar_t* src = (const wchar_t*)GlobalLock(hd);
        wchar_t out[4] = { 0 };
        int n = 0;
        if (src) {
            for (const wchar_t* p = src; *p && n < 2; ++p)
                if (*p >= L'0' && *p <= L'9') out[n++] = *p;
        }
        GlobalUnlock(hd);
        CloseClipboard();
        SetWindowTextW(h, out);
        return 0;
    }
    return CallWindowProcW(g_origEditProc, h, msg, wParam, lParam);
}

// ---------- 帮助窗口 ----------

// 操作指南文本（与已定稿 6 内容一致）
static const wchar_t* HELP_TEXT =
    L"入库上架二维码打印工具  操作指南\r\n"
    L"\r\n"
    L"一、基本操作流程\r\n"
    L"1. 在 16 个灵饰格子中填入数量（0~99），或用格子右侧 ▲/▼ 步进调整；\r\n"
    L"2. 选择标签「入 库(I)」「上 架(O)」或「盘 点」；\r\n"
    L"3. 点「打印」出热敏小票，并自动把整页单据存为 PNG 留档（每次打印存一张到「保存单据」文件夹）；\r\n"
    L"4. 点「重置」清空所有输入。\r\n"
    L"\r\n"
    L"二、「入库并同时上架」\r\n"
    L"勾选后，一次操作同时生成入库和上架各一份（打印/存档均为双份）。仅入库标签时显示。\r\n"
    L"\r\n"
    L"三、盘点（C/E）\r\n"
    L"1. 「盘 点」标签默认打「中途盘点(C)」；勾选下方「日结盘点(E)」复选框则打日结盘点；\r\n"
    L"2. 盘点 16 格填的是实际在售件数（快照），不是变动量：中途盘点用于算截止当时的销售，日结盘点用于收盘结转；\r\n"
    L"3. 盘点票允许 16 格全为 0（例如货已卖空），此时仍可打印；但点「打印」会先弹窗确认，选「否」则不打印、不存档；\r\n"
    L"\r\n"
    L"四、防误操作规则\r\n"
    L"1. 入库/上架时所有格子为 0 或空，「重置/打印」按钮禁用；盘点票不受此限（全零可打印）；\r\n"
    L"2. 窗口失去焦点（含最小化）满 5 分钟自动清空，防止残留误操作；5 分钟内回来不清空、保留输入；\r\n"
    L"3. 每次打印自动存一份整页到「保存单据」文件夹；打印成功自动清空；\r\n"
    L"\r\n"
    L"五、二维码数据格式\r\n"
    L"时间(10位，不含年份) + 操作码(I 入库 / O 上架 / C 中途盘点 / E 日结盘点) + 16 组两位数字（直接相连，无分隔符），供管理程序扫码录入。\r\n"
    L"\r\n"
    L"六、提示\r\n"
    L"1. 「背景图片.png」「top.png」与 EXE 放在同一文件夹即可自定义背景和顶部横幅；";

// 帮助窗口过程（非模态：关闭即销毁，不影响主窗）
static LRESULT CALLBACK HelpWndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = GetModuleHandleW(nullptr);
        HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            12, 12, 496, 348, hw, (HMENU)1, hi, nullptr);
        static HFONT hF = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Microsoft YaHei UI");
        SendMessageW(ed, WM_SETFONT, (WPARAM)hF, TRUE);
        SetWindowTextW(ed, HELP_TEXT);
        CreateWindowExW(0, L"BUTTON", L"关闭",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            420, 368, 88, 30, hw, (HMENU)IDC_HELP_CLOSE, hi, nullptr);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_HELP_CLOSE) { DestroyWindow(hw); return 0; }
        break;
    case WM_DESTROY:
        g_inDialog = false;   // 帮助窗口关闭，恢复失焦自动置零计时
        return 0;   // 非模态：不结束主消息循环
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

static void ShowHelp(HWND hwndParent) {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    HWND hw = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MhxyQrHelpWnd", L"操作指南",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 540, 440, hwndParent, nullptr, hInst, nullptr);
    if (!hw) return;
    // 相对于主窗居中
    RECT pr, wr;
    GetWindowRect(hwndParent, &pr);
    GetWindowRect(hw, &wr);
    int w = wr.right - wr.left, h = wr.bottom - wr.top;
    SetWindowPos(hw, nullptr,
                 pr.left + (pr.right - pr.left - w) / 2,
                 pr.top + (pr.bottom - pr.top - h) / 2,
                 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    // 帮助窗口打开期间暂停失焦自动置零计时
    g_inDialog = true;
    ShowWindow(hw, SW_SHOW);
}

// ---------- 界面绘制 ----------
static void PaintWindow(HWND hwnd, HDC hdc) {
    // 背景图：覆盖铺满客户区 + 半透明白色蒙层（保证文字可读）
    RECT cr;
    GetClientRect(hwnd, &cr);
    {
        Gdiplus::Graphics g(hdc);
        if (g_bgImage) {
            int iw = g_bgImage->GetWidth(), ih = g_bgImage->GetHeight();
            float scale = std::max((float)cr.right / iw, (float)cr.bottom / ih);
            int dw = (int)(iw * scale), dh = (int)(ih * scale);
            int dx = (cr.right - dw) / 2;
            // dy=52：玻璃台（原图 y≈218-368）对位到客户区 y≈232-356；背景图保持原位不动
            //        顶部 52px 露白由横幅（高 58px）盖住
            int dy = 52;
            g.DrawImage(g_bgImage, dx, dy, dw, dh);
            Gdiplus::SolidBrush ov(Gdiplus::Color(90, 255, 255, 255));
            g.FillRectangle(&ov, 0, 0, cr.right, cr.bottom);
        } else {
            g.Clear(Gdiplus::Color(255, 255, 255, 255));
        }
    }

    // 顶部横幅：铺满客户区宽度（左右拉伸与窗口一致）、高度 58px、贴顶
    if (g_topImage) {
        Gdiplus::Graphics g(hdc);
        g.DrawImage(g_topImage, 0, 0, cr.right, 58);
    }
}

// ---------- 窗口过程 ----------
// 单选自绘子类：拦截 WM_ERASEBKGND，阻止控件用灰色(COLOR_BTNFACE)擦底 → 背景完全透明
static LRESULT CALLBACK RadioSubclassProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                          UINT_PTR id, DWORD_PTR data) {
    if (msg == WM_ERASEBKGND) return 1;
    return DefSubclassProc(h, msg, wp, lp);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        // 标题栏小图标（16px 版）
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(hInst, MAKEINTRESOURCE(IDI_APP)));

        // 第一部分：入库/上架/盘点 选项卡（系统 Tab 控件，选中态由系统绘制，切换明显）
        const int TAB_ITEM_W = 96;              // 每标签宽度（三标签等宽）
        const int tabW = TAB_ITEM_W * 3 + 8;    // 三标签 + 边框
        int radioLeft = (CW - tabW) / 2;
        g_tabOp = CreateWindowExW(0, WC_TABCONTROLW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_FIXEDWIDTH | 0x0800 /* TCS_CENTER */,
            radioLeft, 80, tabW, 28,
            hwnd, (HMENU)IDC_TAB_OP, hInst, nullptr);
        {
            TCITEMW ti;
            memset(&ti, 0, sizeof(ti));
            ti.mask = TCIF_TEXT;
            wchar_t tabIn[]  = L"入 库";
            wchar_t tabOut[] = L"上 架";
            wchar_t tabChk[] = L"盘 点";
            ti.pszText = tabIn;  TabCtrl_InsertItem(g_tabOp, 0, &ti);
            ti.pszText = tabOut; TabCtrl_InsertItem(g_tabOp, 1, &ti);
            ti.pszText = tabChk; TabCtrl_InsertItem(g_tabOp, 2, &ti);
            TabCtrl_SetCurSel(g_tabOp, 0);
            SendMessageW(g_tabOp, TCM_SETITEMSIZE, 0, MAKELPARAM(TAB_ITEM_W, 24));
        }
        g_radioSel = IDC_RADIO_IN;

        // “入库并同时上架”复选框：系统标准复选框（BS_AUTOCHECKBOX，自带勾选状态与重绘），仅入库选中时显示
        // 左侧“数量合计”标签：入库合计/上架合计 N 件（标签+复选框作为一组整体居中）
        {
            const int SUM_W = 120, GRP_GAP = 10;
            int grpX0 = (CW - (SUM_W + GRP_GAP + CHK_W)) / 2;
            g_sumLabel = CreateWindowExW(0, L"STATIC", L"入库合计 0 件",
                WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
                grpX0, CHK_Y, SUM_W, CHK_H,
                hwnd, (HMENU)IDC_SUM_LABEL, hInst, nullptr);
            g_chkBoth = CreateWindowExW(0, L"BUTTON", L"  入库并同时上架",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                grpX0 + SUM_W + GRP_GAP, CHK_Y, CHK_W, CHK_H,
                hwnd, (HMENU)IDC_CHK_BOTH, hInst, nullptr);
            // “日结盘点(E)”复选框：同一位置，仅盘点标签显示；不勾选=中途盘点 C（初始隐藏）
            g_chkEod = CreateWindowExW(0, L"BUTTON", L"  日结盘点(E)",
                WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                grpX0 + SUM_W + GRP_GAP, CHK_Y, CHK_W, CHK_H,
                hwnd, (HMENU)IDC_CHK_EOD, hInst, nullptr);
        }

        // 第二部分：4排×4列 = 行标签 + 灵饰名称 + 两位输入（填了即有效）
        for (int r = 0; r < SLOT_ROWS; ++r) {
            CreateWindowExW(0, L"STATIC", ROW_LABELS[r],
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                ROW_LABEL_X, GRID_Y0 + r * CELL_H, ROW_LABEL_W, CELL_H,
                hwnd, (HMENU)(INT_PTR)(100 + r), hInst, nullptr);
            for (int c = 0; c < SLOT_COLS; ++c) {
                int i = r * SLOT_COLS + c;
                int cx = GRID_X0 + c * CELL_W;
                int cy = GRID_Y0 + r * CELL_H;
                CreateWindowExW(0, L"STATIC", ITEM_NAMES[i],
                    WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                    cx, cy, NAME_W, CELL_H,
                    hwnd, (HMENU)(INT_PTR)(200 + i), hInst, nullptr);
                g_edits[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_AUTOHSCROLL | ES_NUMBER,
                    cx + NAME_W + 2, cy + (CELL_H - EDT_H) / 2, EDT_W, EDT_H,
                    hwnd, (HMENU)(INT_PTR)(IDC_EDT_BASE + i + 1), hInst, nullptr);
                SendMessageW(g_edits[i], EM_SETLIMITTEXT, 2, 0);
                // 每个输入框后的 ▲ / ▼ 步进按钮（上下堆叠迷你样式，自绘扁平无灰底）
                g_plus[i] = CreateWindowExW(0, L"BUTTON", L"▲",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW,
                    cx + NAME_W + 2 + EDT_W + SPIN_GAP, cy + (CELL_H - EDT_H) / 2, SPIN_W, SPIN_H,
                    hwnd, (HMENU)(INT_PTR)(IDC_PLUS_BASE + i), hInst, nullptr);
                g_minus[i] = CreateWindowExW(0, L"BUTTON", L"▼",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW,
                    cx + NAME_W + 2 + EDT_W + SPIN_GAP,
                    cy + (CELL_H - EDT_H) / 2 + SPIN_H + SPIN_VGAP, SPIN_W, SPIN_H,
                    hwnd, (HMENU)(INT_PTR)(IDC_MINUS_BASE + i), hInst, nullptr);
                SetWindowSubclass(g_plus[i], RadioSubclassProc, (UINT_PTR)(10 + i), 0);
                SetWindowSubclass(g_minus[i], RadioSubclassProc, (UINT_PTR)(20 + i), 0);
            }
        }
        g_origEditProc = (WNDPROC)SetWindowLongPtrW(g_edits[0], GWLP_WNDPROC, (LONG_PTR)SlotEditProc);
        for (int i = 1; i < SLOT_TOTAL; ++i)
            SetWindowLongPtrW(g_edits[i], GWLP_WNDPROC, (LONG_PTR)SlotEditProc);

        // 第三部分：四个命令按钮（水平居中：重置/打印/退出/帮助）
        int btnX0 = (CW - (4 * 86 + 3 * BTN_GAP)) / 2;
        const wchar_t* btnLabels[4] = { L"重置", L"打印", L"退出", L"帮助" };
        const UINT btnIds[4] = { IDC_RESET, IDC_PRINT, IDC_EXIT, IDC_BTN_HELP };
        if (!g_btnFont)
            g_btnFont = CreateFontW(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS, L"微软雅黑");
        for (int i = 0; i < 4; ++i) {
            HWND hb = CreateWindowExW(0, L"BUTTON", btnLabels[i],
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                btnX0 + i * (86 + BTN_GAP), BTN_Y, 86, 30,
                hwnd, (HMENU)(INT_PTR)btnIds[i], hInst, nullptr);
            SendMessageW(hb, WM_SETFONT, (WPARAM)g_btnFont, TRUE);
            if (btnIds[i] == IDC_RESET) g_btnReset = hb;
            else if (btnIds[i] == IDC_PRINT) g_btnPrint = hb;
            else if (btnIds[i] == IDC_BTN_HELP) g_btnHelp = hb;
        }

        UpdatePrintBtnLabel();   // 初始按当前标签显示按钮文字

        // 帮助按钮悬停提示
        {
            HWND tt = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr,
                WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
                CW_USEDEFAULT, CW_USEDEFAULT, hwnd, nullptr, hInst, nullptr);
            auto AddTip = [&](HWND target, const wchar_t* text) {
                TOOLINFOW ti = {};
                ti.cbSize = sizeof(ti);
                ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                ti.hwnd = hwnd;
                ti.uId = (UINT_PTR)target;
                ti.lpszText = (LPWSTR)text;
                SendMessageW(tt, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            };
            AddTip(g_btnHelp, L"查看本软件操作指南");
        }

        // 统一字体（美观/对齐）
        static HFONT hUiFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Microsoft YaHei UI");
        struct EnumFontCtx { HFONT h; };
        auto EnumFontProc = [](HWND hChild, LPARAM lp) -> BOOL {
            SendMessageW(hChild, WM_SETFONT, (WPARAM)((EnumFontCtx*)lp)->h, TRUE);
            return TRUE;
        };
        EnumFontCtx fontCtx = { hUiFont };
        EnumChildWindows(hwnd, EnumFontProc, (LPARAM)&fontCtx);
        // 单选按钮用更大的字体
        static HFONT hBigFont = CreateFontW(-19, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Microsoft YaHei UI");
        SendMessageW(g_tabOp, WM_SETFONT, (WPARAM)hBigFont, TRUE);

        // 加载背景图（EXE 同目录\背景图片.png，缺失则回退纯白）
        {
            wchar_t buf[MAX_PATH];
            GetModuleFileNameW(nullptr, buf, MAX_PATH);
            wchar_t* slash = wcsrchr(buf, L'\\');
            if (slash) *(slash + 1) = 0;
            wcscat_s(buf, L"背景图片.png");
            g_bgImage = Gdiplus::Image::FromFile(buf, FALSE);
            if (g_bgImage && g_bgImage->GetLastStatus() != Gdiplus::Ok) {
                delete g_bgImage;
                g_bgImage = nullptr;
            }
        }

        // 加载顶部横幅（EXE 同目录\top.png，缺失则跳过）
        {
            wchar_t buf[MAX_PATH];
            GetModuleFileNameW(nullptr, buf, MAX_PATH);
            wchar_t* slash = wcsrchr(buf, L'\\');
            if (slash) *(slash + 1) = 0;
            wcscat_s(buf, L"top.png");
            g_topImage = Gdiplus::Image::FromFile(buf, FALSE);
            if (g_topImage && g_topImage->GetLastStatus() != Gdiplus::Ok) {
                delete g_topImage;
                g_topImage = nullptr;
            }
        }

        // 启动即生成空态二维码（16 格全 00）
        UpdatePreview(hwnd);
        return 0;
    }

    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lParam;
        if (nm && nm->idFrom == IDC_TAB_OP && nm->code == TCN_SELCHANGE) {
            // 不在 Tab 通知栈内同步改兄弟控件显隐（重入 COMCTL32 会崩），延后到消息队列处理
            PostMessageW(hwnd, WM_APP_TABCHANGED, 0, 0);
        }
        return 0;
    }

    case WM_APP_TABCHANGED: {
        int sel = TabCtrl_GetCurSel(g_tabOp);
        g_radioSel = (sel == 0) ? IDC_RADIO_IN : (sel == 1 ? IDC_RADIO_OUT : IDC_TAB_CHECK);
        UpdateChkVisibility();
        UpdatePreview(hwnd);
        return 0;
    }

    case WM_COMMAND: {
        UINT id = LOWORD(wParam), code = HIWORD(wParam);
        switch (id) {
        case IDC_RESET: DoReset(hwnd); break;
        case IDC_PRINT: PrintQr(hwnd); break;
        case IDC_EXIT:  DestroyWindow(hwnd); break;
        case IDC_BTN_HELP: ShowHelp(hwnd); break;
        case IDC_CHK_BOTH:
            if (code == BN_CLICKED) {
                // 系统复选框点击后自带勾选状态与重绘，这里同步逻辑变量（保存/打印判定用）
                g_bothChecked = (SendMessageW(g_chkBoth, BM_GETCHECK, 0, 0) == BST_CHECKED);
            }
            break;
        case IDC_CHK_EOD:
            if (code == BN_CLICKED) {
                // 勾选=日结盘点 E，未勾选=中途盘点 C；操作码变了，内容/合计/按钮文字即时刷新
                // 用状态取反而不是回读控件：系统复选框点击时已自动切换，这里取反与之一致；
                // 同一路径也兼容脚本驱动（跨进程 BM_SETCHECK/BM_CLICK 在某些环境下对复选框不生效）
                g_eodChecked = !g_eodChecked;
                SendMessageW(g_chkEod, BM_SETCHECK, g_eodChecked ? BST_CHECKED : BST_UNCHECKED, 0);
                UpdatePreview(hwnd);
            }
            break;
        default:
            if (id >= IDC_PLUS_BASE && id < IDC_PLUS_BASE + SLOT_TOTAL) {
                if (code == BN_CLICKED) SpinEdit(id - IDC_PLUS_BASE, 1);
                break;
            }
            if (id >= IDC_MINUS_BASE && id < IDC_MINUS_BASE + SLOT_TOTAL) {
                if (code == BN_CLICKED) SpinEdit(id - IDC_MINUS_BASE, -1);
                break;
            }
            if (id >= IDC_EDT_BASE + 1 && id <= IDC_EDT_BASE + SLOT_TOTAL) {
                if (code == EN_CHANGE) UpdatePreview(hwnd);
            }
        }
        return 0;
    }

    // 自绘互斥按钮（命令按钮样式）：选中=蓝底白字圆角，未选=透明蓝框蓝字
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlType == ODT_BUTTON) {
            Gdiplus::Graphics g(dis->hDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            RECT rc = dis->rcItem;
            int id = GetDlgCtrlID(dis->hwndItem);

            // 步进按钮 ▲ / ▼：扁平样式（细边框 + 三角箭头，无灰底）
            if ((id >= IDC_PLUS_BASE && id < IDC_PLUS_BASE + SLOT_TOTAL) ||
                (id >= IDC_MINUS_BASE && id < IDC_MINUS_BASE + SLOT_TOTAL)) {
                bool up = (id < IDC_PLUS_BASE + SLOT_TOTAL);
                bool pressed = (Button_GetState(dis->hwndItem) & BST_PUSHED) != 0;
                float x = (float)rc.left, y = (float)rc.top;
                float w = (float)(rc.right - rc.left), h = (float)(rc.bottom - rc.top);
                Gdiplus::Pen pen(Gdiplus::Color(255, 50, 90, 150), 1.0f);
                g.DrawRectangle(&pen, x + 0.5f, y + 0.5f, w - 1.0f, h - 1.0f);
                if (pressed) {
                    Gdiplus::SolidBrush fill(Gdiplus::Color(70, 50, 90, 150));
                    g.FillRectangle(&fill, x + 1, y + 1, w - 2, h - 2);
                }
                float cx = x + w / 2, cy = y + h / 2;
                Gdiplus::SolidBrush tri(Gdiplus::Color(255, 50, 90, 150));
                Gdiplus::PointF pts[3];
                if (up) { pts[0] = { cx - 3.2f, cy + 2.4f }; pts[1] = { cx + 3.2f, cy + 2.4f }; pts[2] = { cx, cy - 2.4f }; }
                else    { pts[0] = { cx - 3.2f, cy - 2.4f }; pts[1] = { cx + 3.2f, cy - 2.4f }; pts[2] = { cx, cy + 2.4f }; }
                g.FillPolygon(&tri, pts, 3);
                return TRUE;
            }
        }
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintWindow(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    // 禁止背景擦除（防闪烁，背景统一在 WM_PAINT 里画）
    case WM_ERASEBKGND:
        return TRUE;

    // 静态文本透明，露出背景图（合计标签除外：动态更新须白底擦除旧字，防重叠）
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        if (GetDlgCtrlID((HWND)lParam) == IDC_SUM_LABEL)
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    // 复选框透明文字（背景用白底刷保证系统复选框可见）
    case WM_CTLCOLORBTN: {
        LONG style = GetWindowLongW((HWND)lParam, GWL_STYLE) & BS_TYPEMASK;
        if (style == BS_AUTOCHECKBOX) {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        break;
    }

    // 失焦自动置零计时器：失焦（含最小化）满 5 分钟自动置零；获焦取消计时保留输入
    case WM_ACTIVATE:
        if (wParam == WA_INACTIVE) {
            // 模态对话框/帮助窗口打开期间不启动计时（用户仍在程序相关操作中）
            if (!g_inDialog && !g_timerRunning) {
                g_focusLostAt = GetTickCount();
                SetTimer(hwnd, 1, 30000, nullptr);
                g_timerRunning = true;
            }
        } else if (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE) {
            if (g_timerRunning) { KillTimer(hwnd, 1); g_timerRunning = false; }
        }
        break;

    case WM_TIMER:
        if (wParam == 1 && g_timerRunning &&
            (GetTickCount() - g_focusLostAt) >= 5 * 60 * 1000) {
            KillTimer(hwnd, 1);
            g_timerRunning = false;
            DoReset(hwnd, false);   // 自动置零：放太久重置一切（清空 + 取消勾选 + 标签切回入库）
        }
        return 0;

    case WM_DESTROY:
        if (g_bgImage) { delete g_bgImage; g_bgImage = nullptr; }
        if (g_topImage) { delete g_topImage; g_topImage = nullptr; }
        if (g_btnFont) { DeleteObject(g_btnFont); g_btnFont = nullptr; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gsi, nullptr);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCE(IDI_APP));   // 应用图标（app.ico）
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MhxyQrToolWnd";
    RegisterClassW(&wc);

    // 帮助窗口类（非模态）
    WNDCLASSW wch = {};
    wch.lpfnWndProc = HelpWndProc;
    wch.hInstance = hInst;
    wch.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wch.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wch.lpszClassName = L"MhxyQrHelpWnd";
    RegisterClassW(&wch);

    RECT wr = { 0, 0, 620, 400 };   // 客户区尺寸（窗口加高 25px：按钮底356 + 台面下倒影留白 44）
    AdjustWindowRectEx(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"入库上架二维码打印工具",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) {
        Gdiplus::GdiplusShutdown(gdiToken);
        return 1;
    }
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Gdiplus::GdiplusShutdown(gdiToken);
    return (int)msg.wParam;
}

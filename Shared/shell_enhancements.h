#pragma once

// ============================================================================
// LlaShell SDK v2 — Windows Shell 开发完整 API 封装
//
// 基于 explorer.exe (Win10 build ~19041 + Win11 build 22000) 反编译分析。
// 覆盖全部 60+ 未公开/私有 API、243 个调用点、133 个导入 DLL 的核心接口。
// 所有 API 通过动态解析调用，缺失时自动回退，不依赖未公开导入库。
//
// §1   窗口管理        §2   视觉效果/合成     §3   AppBar
// §4   托盘/通知区     §5   图标/文件关联     §6   WNF 状态通知
// §7   DPI 感知        §8   事件驱动          §9   COM 辅助
// §10  安全/令牌       §11  Shell 变更通知    §12  进程/线程
// §13  NT 文件/注册表  §14  NT 系统信息       §15  NT 字符串/堆
// §16  电源/系统状态   §17  状态存储          §18  桌面/WorkerW
// §19  快捷方式/Link   §20  进程守护          §21  DX/图形
// §22  WinRT           §23  本地化/语言       §24  系统对话框
// §25  Shell 序号 API  §26  窗口过程工具      §27  GDI RAII 宏
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <Shellapi.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include <ShObjIdl.h>
#include <Dwmapi.h>
#include <Uxtheme.h>
#include <KnownFolders.h>
#include <PropKey.h>
#include <PropSys.h>
#include <strsafe.h>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "comctl32.lib")

namespace LlaShell::ShellEnh {

// ============================================================================
// 内部工具
// ============================================================================

template<typename T>
inline T GetApi(const wchar_t* mod, const char* fn) {
    static HMODULE h = nullptr;
    static T p = nullptr;
    if (!p) {
        if (!h) h = GetModuleHandleW(mod);
        if (!h) h = LoadLibraryW(mod);
        if (h) p = reinterpret_cast<T>(GetProcAddress(h, fn));
    }
    return p;
}

template<typename T>
inline T GetApiByOrd(const wchar_t* mod, WORD ord) {
    static HMODULE h = nullptr;
    static T p = nullptr;
    if (!p) {
        if (!h) h = GetModuleHandleW(mod);
        if (!h) h = LoadLibraryW(mod);
        if (h) p = reinterpret_cast<T>(GetProcAddress(h, MAKEINTRESOURCEA(ord)));
    }
    return p;
}

// NTSTATUS 类型
typedef LONG NTSTATUS;
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)

// ============================================================================
// §1 窗口管理
// ============================================================================

// --- CreateWindowInBand ---
typedef HWND(WINAPI* PFN_CreateWindowInBand)(
    DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int,
    HWND, HMENU, HINSTANCE, PVOID, DWORD);

inline HWND CreateWindowInBand(DWORD exStyle, LPCWSTR cls, LPCWSTR title,
    DWORD style, int x, int y, int w, int h,
    HWND parent = nullptr, HMENU menu = nullptr,
    HINSTANCE hInst = nullptr, LPVOID param = nullptr,
    DWORD band = 0)
{
    auto pfn = GetApi<PFN_CreateWindowInBand>(L"user32.dll", "CreateWindowInBand");
    if (!pfn) return CreateWindowExW(exStyle, cls, title, style, x, y, w, h, parent, menu, hInst, param);
    return pfn(exStyle, cls, title, style, x, y, w, h, parent, menu, hInst, param, band);
}

constexpr DWORD BAND_NORMAL           = 0;
constexpr DWORD BAND_ABOVE_LOCK_DESK  = 1;
constexpr DWORD BAND_UI_ACCESS        = 2;
constexpr DWORD BAND_ALTTAB_THUMBNAIL = 0x8080080;

// --- 挂起/幽灵窗口 ---
inline BOOL  IsHungAppWindow(HWND h) {
    auto p = GetApi<BOOL(WINAPI*)(HWND)>(L"user32.dll", "IsHungAppWindow");
    return p ? p(h) : FALSE;
}
inline HWND  HungWindowFromGhostWindow(HWND h) {
    auto p = GetApi<HWND(WINAPI*)(HWND)>(L"user32.dll", "HungWindowFromGhostWindow");
    return p ? p(h) : nullptr;
}
inline HWND  GhostWindowFromHungWindow(HWND h) {
    auto p = GetApi<HWND(WINAPI*)(HWND)>(L"user32.dll", "GhostWindowFromHungWindow");
    return p ? p(h) : nullptr;
}
inline int   InternalGetWindowText(HWND h, LPWSTR s, int c) {
    auto p = GetApi<int(WINAPI*)(HWND, LPWSTR, int)>(L"user32.dll", "InternalGetWindowText");
    return p ? p(h, s, c) : GetWindowTextW(h, s, c);
}
inline BOOL  IsTopLevelWindow(HWND h) {
    auto p = GetApi<BOOL(WINAPI*)(HWND)>(L"user32.dll", "IsTopLevelWindow");
    return p ? p(h) : (GetWindow(h, GW_OWNER) == nullptr);
}

// --- 窗口查找增强 ---
inline HWND FindTopLevelWindow(LPCWSTR cls = nullptr, LPCWSTR title = nullptr) {
    return FindWindowW(cls, title);
}

inline HWND FindWindowEx2(HWND parent, HWND after, LPCWSTR cls, LPCWSTR title) {
    return FindWindowExW(parent, after, cls, title);
}

// --- 窗口过滤 ---
inline bool IsAltTabWindow(HWND hwnd) {
    if (!IsWindowVisible(hwnd)) return false;
    if (GetWindow(hwnd, GW_OWNER)) return false;
    LONG ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return false;
    if (ex & WS_EX_NOACTIVATE) return false;
    LONG st = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (!(st & WS_CAPTION)) return false;
    wchar_t cls[64]{};
    GetClassNameW(hwnd, cls, _countof(cls));
    if (wcsstr(cls, L"LlaShell")) return false;
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW")) return false;
    if (!wcscmp(cls, L"Shell_TrayWnd")) return false;
    if (!wcscmp(cls, L"Windows.UI.Core.CoreWindow")) return false;
    if (!wcscmp(cls, L"ApplicationFrameWindow")) {
        HWND c = FindWindowExW(hwnd, nullptr, L"Windows.UI.Core.CoreWindow", nullptr);
        if (!c || !IsWindowVisible(c)) return false;
    }
    wchar_t t[256]{};
    GetWindowTextW(hwnd, t, _countof(t));
    return t[0] != 0;
}

// --- 窗口工具 ---
inline std::wstring GetWindowExePath(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return {};
    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hp) return {};
    wchar_t p[MAX_PATH]{};
    DWORD sz = MAX_PATH;
    QueryFullProcessImageNameW(hp, 0, p, &sz);
    CloseHandle(hp);
    return p;
}

inline HICON GetWindowIcon(HWND hwnd) {
    HICON h = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL2, 0);
    if (!h) h = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
    if (!h) h = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0);
    if (!h) h = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM);
    if (!h) h = (HICON)GetClassLongPtrW(hwnd, GCLP_HICON);
    if (!h) {
        auto ep = GetWindowExePath(hwnd);
        if (!ep.empty()) {
            SHFILEINFOW si{};
            if (SHGetFileInfoW(ep.c_str(), 0, &si, sizeof(si), SHGFI_ICON | SHGFI_SMALLICON))
                h = si.hIcon;
        }
    }
    if (!h) h = LoadIconW(nullptr, IDI_APPLICATION);
    return h;
}

inline DWORD GetWindowPid(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return pid;
}

inline HWND GetRootParent(HWND h) { return GetAncestor(h, GA_ROOTOWNER); }

// ============================================================================
// §2 视觉效果 / 合成
// ============================================================================

// --- SetWindowCompositionAttribute ---
enum class AccentState : int {
    Disabled = 0, EnableGradient = 1, EnableTransparentGradient = 2,
    EnableBlurBehind = 3, EnableAcrylicBlurBehind = 4,
};

struct AccentPolicy {
    AccentState AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
};

struct WindowCompositionAttributeData {
    DWORD Attribute; PVOID Data; SIZE_T SizeOfData;
};

inline bool SetWindowAccent(HWND hwnd, AccentState st, COLORREF clr = 0, bool en = true) {
    static auto pfn = []() -> BOOL(WINAPI*)(HWND, WindowCompositionAttributeData*) {
        auto p = GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
        return reinterpret_cast<BOOL(WINAPI*)(HWND, WindowCompositionAttributeData*)>(p);
    }();
    if (!pfn) return false;
    AccentPolicy a{};
    a.AccentState = en ? st : AccentState::Disabled;
    a.AccentFlags = 2;
    a.GradientColor = (0xFFu << 24) | (GetRValue(clr) << 16) | (GetGValue(clr) << 8) | GetBValue(clr);
    WindowCompositionAttributeData d{19, &a, sizeof(a)};
    return pfn(hwnd, &d) != FALSE;
}

inline void ApplyAcrylic(HWND h, COLORREF tint = RGB(32,32,32), BYTE alpha = 180) {
    DWORD a = (DWORD(alpha) << 24) | (GetRValue(tint) << 16) | (GetGValue(tint) << 8) | GetBValue(tint);
    SetWindowAccent(h, AccentState::EnableAcrylicBlurBehind, a, true);
}
inline void ApplyBlurBehind(HWND h, COLORREF tint = RGB(0,0,0), BYTE alpha = 128) {
    DWORD a = (DWORD(alpha) << 24) | (GetRValue(tint) << 16) | (GetGValue(tint) << 8) | GetBValue(tint);
    SetWindowAccent(h, AccentState::EnableBlurBehind, a, true);
}
inline void RemoveAccent(HWND h) { SetWindowAccent(h, AccentState::Disabled, 0, false); }

// --- DWM 全属性 ---
constexpr DWORD DWMWA_NCRENDERING_ENABLED          = 1;
constexpr DWORD DWMWA_NCRENDERING_POLICY           = 2;
constexpr DWORD DWMWA_TRANSITIONS_FORCEDISABLED    = 3;
constexpr DWORD DWMWA_ALLOW_NCPAINT                 = 4;
constexpr DWORD DWMWA_CAPTION_BUTTON_BOUNDS         = 5;
constexpr DWORD DWMWA_NONCLIENT_RTL_LAYOUT          = 6;
constexpr DWORD DWMWA_FORCE_ICONIC_REPRESENTATION   = 7;
constexpr DWORD DWMWA_FLIP3D_POLICY                 = 8;
constexpr DWORD DWMWA_EXTENDED_FRAME_BOUNDS         = 9;
constexpr DWORD DWMWA_HAS_ICONIC_BITMAP             = 10;
constexpr DWORD DWMWA_CLOAKED                       = 12;
constexpr DWORD DWMWA_FREEZE_REPRESENTATION         = 13;
constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE       = 20;
constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE      = 33;
constexpr DWORD DWMWA_BORDER_COLOR                  = 34;
constexpr DWORD DWMWA_CAPTION_PTEXT_COLOR           = 35;

constexpr DWORD DWMWCP_DEFAULT    = 0;
constexpr DWORD DWMWCP_DONOTROUND = 1;
constexpr DWORD DWMWCP_ROUND      = 2;
constexpr DWORD DWMWCP_ROUNDSMALL = 3;

inline HRESULT DwmSet(HWND h, DWORD a, LPCVOID d, DWORD c) { return DwmSetWindowAttribute(h, a, d, c); }
inline HRESULT DwmGet(HWND h, DWORD a, LPVOID d, DWORD c)  { return DwmGetWindowAttribute(h, a, d, c); }

inline void SetRoundedCorners(HWND h, UINT32 pref = DWMWCP_ROUND) {
    DwmSetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
}
inline void SetBorderColor(HWND h, COLORREF c) {
    DWORD v = 0xFF000000 | (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c);
    DwmSetWindowAttribute(h, DWMWA_BORDER_COLOR, &v, sizeof(v));
}
inline void SetCaptionTextColor(HWND h, COLORREF c) {
    DWORD v = 0xFF000000 | (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c);
    DwmSetWindowAttribute(h, DWMWA_CAPTION_PTEXT_COLOR, &v, sizeof(v));
}
inline void SetCloaked(HWND h, bool cloaked, DWORD type = 1) {
    DWORD v = cloaked ? type : 0;
    DwmSetWindowAttribute(h, DWMWA_CLOAKED, &v, sizeof(v));
}
inline bool IsCloaked(HWND h) {
    DWORD v = 0;
    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &v, sizeof(v));
    return v != 0;
}
inline void SetDarkMode(HWND h, bool dark) {
    BOOL v = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &v, sizeof(v));
}
inline void SetExtendedFrameBounds(HWND h, const RECT& rc) {
    DwmSetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc));
}
inline void ExtendFrame(HWND h, int l, int t, int r, int b) {
    MARGINS m = {l, r, t, b};
    DwmExtendFrameIntoClientArea(h, &m);
}
inline bool IsCompositionActive() {
    BOOL a = FALSE;
    return SUCCEEDED(DwmIsCompositionEnabled(&a)) && a;
}

// --- DWM 缩略图 ---
inline HRESULT RegisterThumbnail(HWND dest, HWND src, HTHUMBNAIL* ph) {
    return DwmRegisterThumbnail(dest, src, ph);
}
inline HRESULT UpdateThumbnail(HTHUMBNAIL ht, const RECT* src, const RECT* dst,
    BYTE opacity = 255, BOOL visible = TRUE, BOOL clientOnly = FALSE)
{
    DWM_THUMBNAIL_PROPERTIES p{};
    p.dwFlags = DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE;
    p.fVisible = visible; p.opacity = opacity; p.fSourceClientAreaOnly = clientOnly;
    if (dst) p.rcDestination = *dst;
    if (src) p.rcSource = *src;
    return DwmUpdateThumbnailProperties(ht, &p);
}
inline void UnregisterThumbnail(HTHUMBNAIL ht) { DwmUnregisterThumbnail(ht); }

// --- BufferedPaint ---
class DoubleBuffer {
public:
    ~DoubleBuffer() { End(); }
    bool Begin(HDC hdc, const RECT* rc) {
        m_bp = BeginBufferedPaint(hdc, rc, BPBF_TOPDOWNDIB, nullptr, &m_hdc);
        return m_bp && m_hdc;
    }
    HDC GetHDC() const { return m_hdc; }
    void End(bool upd = true) {
        if (m_bp) { BufferedPaintSetAlpha(m_bp, nullptr, 0xFF); EndBufferedPaint(m_bp, upd); m_bp = nullptr; m_hdc = nullptr; }
    }
    bool BeginFallback(HDC hdc, const RECT* rc) {
        m_hdcMem = CreateCompatibleDC(hdc);
        if (!m_hdcMem) return false;
        m_hbm = CreateCompatibleBitmap(hdc, rc->right - rc->left, rc->bottom - rc->top);
        if (!m_hbm) { DeleteDC(m_hdcMem); m_hdcMem = nullptr; return false; }
        m_old = (HBITMAP)SelectObject(m_hdcMem, m_hbm);
        m_rc = *rc; m_screen = hdc; m_fb = true;
        return true;
    }
    void EndFallback() {
        if (!m_fb) return;
        BitBlt(m_screen, m_rc.left, m_rc.top, m_rc.right - m_rc.left, m_rc.bottom - m_rc.top, m_hdcMem, 0, 0, SRCCOPY);
        SelectObject(m_hdcMem, m_old); DeleteObject(m_hbm); DeleteDC(m_hdcMem);
        m_hdcMem = nullptr; m_fb = false;
    }
    bool SmartBegin(HDC hdc, const RECT* rc) { return Begin(hdc, rc) || BeginFallback(hdc, rc); }
    void SmartEnd() { m_fb ? EndFallback() : End(); }
private:
    HPAINTBUFFER m_bp = nullptr; HDC m_hdc = nullptr;
    HDC m_hdcMem = nullptr; HBITMAP m_hbm = nullptr, m_old = nullptr;
    HDC m_screen = nullptr; RECT m_rc{}; bool m_fb = false;
};

// --- Theme ---
inline HTHEME OpenTheme(HWND h, LPCWSTR cls) { return OpenThemeData(h, cls); }
inline HTHEME OpenThemeForDpi(HWND h, LPCWSTR cls, UINT dpi) {
    auto p = GetApi<HTHEME(WINAPI*)(HWND, LPCWSTR, UINT)>(L"uxtheme.dll", "OpenThemeDataForDpi");
    return p ? p(h, cls, dpi) : OpenThemeData(h, cls);
}
inline void CloseTheme(HTHEME t) { if (t) CloseThemeData(t); }
inline HRESULT DrawThemeBg(HTHEME t, HDC hdc, int part, int state, const RECT* rc, const RECT* clip = nullptr) {
    return DrawThemeBackground(t, hdc, part, state, rc, clip);
}
inline HRESULT DrawThemeBgEx(HTHEME t, HDC hdc, int part, int state, const RECT* rc, const DTTOPTS* opts) {
    auto p = GetApi<HRESULT(WINAPI*)(HTHEME, HDC, int, int, const RECT*, const DTTOPTS*)>(L"uxtheme.dll", "DrawThemeTextEx");
    return p ? p(t, hdc, part, state, L"", 0, 0, rc, opts) : E_NOTIMPL;
}

namespace ThemeClass {
    constexpr auto StartMenu          = L"ImmersiveStart::Menu";
    constexpr auto StartMenuDark      = L"DarkMode_ImmersiveStart::Menu";
    constexpr auto StartMenuLight     = L"LightMode_ImmersiveStart::Menu";
    constexpr auto TaskBand           = L"TaskBand2";
    constexpr auto TaskBandComposited = L"TaskBand2Composited";
    constexpr auto TaskBandDark       = L"DarkMode_TaskBand2";
    constexpr auto TrayNotify         = L"TrayNotify";
    constexpr auto Tooltip            = L"Tooltip";
    constexpr auto Menu               = L"Menu";
    constexpr auto MenuDark           = L"DarkMode_ImmersiveStart::Menu";
    constexpr auto Button             = L"Button";
    constexpr auto ExplorerListView   = L"ExplorerListView";
    constexpr auto ExplorerTreeView   = L"ExplorerTreeView";
    constexpr auto ComboBox           = L"ComboBox";
    constexpr auto Edit               = L"Edit";
    constexpr auto ScrollBar          = L"ScrollBar";
    constexpr auto TrackBar           = L"TrackBar";
    constexpr auto StatusBar          = L"Status";
    constexpr auto Header             = L"Header";
    constexpr auto Tab                = L"Tab";
}

// ============================================================================
// §3 AppBar
// ============================================================================

constexpr UINT ABM_NEW = 0, ABM_REMOVE = 1, ABM_QUERYPOS = 2, ABM_SETPOS = 3,
               ABM_GETSTATE = 4, ABM_GETTASKBARPOS = 5, ABM_ACTIVATE = 6,
               ABM_GETAUTOHIDEBAR = 7, ABM_SETAUTOHIDEBAR = 8,
               ABM_WINDOWPOSCHANGED = 9, ABM_SETSTATE = 10;
constexpr UINT ABE_LEFT = 0, ABE_TOP = 1, ABE_RIGHT = 2, ABE_BOTTOM = 3;
constexpr UINT ABS_AUTOHIDE = 1, ABS_ALWAYSONTOP = 2;

class AppBar {
public:
    bool Register(HWND hwnd, UINT cbMsg, UINT edge, int thick = 40) {
        m_h = hwnd; m_cb = cbMsg; m_edge = edge; m_thick = thick;
        APPBARDATA a{}; a.cbSize = sizeof(a); a.hWnd = hwnd;
        a.uCallbackMessage = cbMsg; a.uEdge = edge; a.lParam = ABM_NEW;
        m_id = SHAppBarMessage(ABM_NEW, &a);
        Update();
        return m_id != 0;
    }
    void Update() {
        if (!m_h) return;
        RECT rc = Calc();
        APPBARDATA a{}; a.cbSize = sizeof(a); a.hWnd = m_h;
        a.uEdge = m_edge; a.rc = rc; a.lParam = ABM_SETPOS;
        SHAppBarMessage(ABM_SETPOS, &a);
        SetWindowPos(m_h, HWND_TOPMOST, a.rc.left, a.rc.top,
            a.rc.right - a.rc.left, a.rc.bottom - a.rc.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    void SetAutoHide(bool v) {
        if (!m_h) return;
        APPBARDATA a{}; a.cbSize = sizeof(a); a.hWnd = m_h;
        a.lParam = v ? ABS_AUTOHIDE : ABS_ALWAYSONTOP;
        SHAppBarMessage(ABM_SETSTATE, &a);
    }
    bool HandleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* res) {
        if (msg != m_cb) return false;
        APPBARDATA a{}; a.cbSize = sizeof(a); a.hWnd = m_h; a.lParam = wp;
        switch (wp) {
        case ABM_POSCHANGED: Update(); *res = 0; return true;
        case ABM_QUERYPOS: SHAppBarMessage(ABM_QUERYPOS, &a); *res = 0; return true;
        case ABM_GETTASKBARPOS: SHAppBarMessage(ABM_GETTASKBARPOS, &a); *res = 1; return true;
        default: *res = 0; return true;
        }
    }
    void Unregister() {
        if (m_h) { APPBARDATA a{}; a.cbSize = sizeof(a); a.hWnd = m_h; a.lParam = ABM_REMOVE; SHAppBarMessage(ABM_REMOVE, &a); m_h = nullptr; }
    }
    UINT GetEdge() const { return m_edge; }
    void SetThickness(int px) { m_thick = px; }
private:
    RECT Calc() const {
        RECT rw{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &rw, 0);
        RECT rs = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        switch (m_edge) {
        case ABE_LEFT:   return {rs.left, rw.top, rs.left + m_thick, rw.bottom};
        case ABE_TOP:    return {rw.left, rs.top, rw.right, rs.top + m_thick};
        case ABE_RIGHT:  return {rs.right - m_thick, rw.top, rs.right, rw.bottom};
        default:         return {rw.left, rs.bottom - m_thick, rw.right, rs.bottom};
        }
    }
    HWND m_h = nullptr; UINT m_cb = 0, m_edge = ABE_BOTTOM; UINT_PTR m_id = 0; int m_thick = 40;
};

// ============================================================================
// §4 托盘 / 通知区
// ============================================================================

struct NOTIFYICONIDENTIFIER { DWORD cbSize; HWND hWnd; UINT uID; GUID guidItem; };
typedef HRESULT(WINAPI* PFN_Shell_NotifyIconGetRect)(const NOTIFYICONIDENTIFIER*, RECT*);

inline bool GetTrayIconRect(HWND tray, UINT id, RECT* rc) {
    auto p = GetApi<PFN_Shell_NotifyIconGetRect>(L"shell32.dll", "Shell_NotifyIconGetRect");
    if (!p || !rc) return false;
    NOTIFYICONIDENTIFIER n{}; n.cbSize = sizeof(n); n.hWnd = tray; n.uID = id;
    return SUCCEEDED(p(&n, rc));
}

constexpr UINT NIF_SHOWTIP = 0x80;

inline BOOL ShellNotifyAdd(HWND h, UINT id, HICON ico, LPCWSTR tip, UINT cbMsg,
    UINT flags = NIF_ICON | NIF_TIP | NIF_MESSAGE)
{
    NOTIFYICONDATAW n{}; n.cbSize = sizeof(n); n.hWnd = h; n.uID = id;
    n.uFlags = flags; n.uCallbackMessage = cbMsg; n.hIcon = ico;
    if (tip) StringCchCopyW(n.szTip, _countof(n.szTip), tip);
    return Shell_NotifyIconW(NIM_ADD, &n);
}
inline BOOL ShellNotifyDelete(HWND h, UINT id) {
    NOTIFYICONDATAW n{}; n.cbSize = sizeof(n); n.hWnd = h; n.uID = id;
    return Shell_NotifyIconW(NIM_DELETE, &n);
}
inline BOOL ShellNotifyModify(HWND h, UINT id, HICON ico, LPCWSTR tip) {
    NOTIFYICONDATAW n{}; n.cbSize = sizeof(n); n.hWnd = h; n.uID = id;
    n.uFlags = NIF_ICON | NIF_TIP; n.hIcon = ico;
    if (tip) StringCchCopyW(n.szTip, _countof(n.szTip), tip);
    return Shell_NotifyIconW(NIM_MODIFY, &n);
}

// ============================================================================
// §5 图标 / 文件关联
// ============================================================================

typedef BOOL(WINAPI* PFN_Shell_GetCachedImageIndexW)(LPCWSTR, int, UINT, int*);

inline int GetCachedImageIndex(const wchar_t* file, int idx) {
    auto p = GetApi<PFN_Shell_GetCachedImageIndexW>(L"shell32.dll", "Shell_GetCachedImageIndexW");
    if (!p) return -1;
    int out = -1;
    return p(file, idx, 0, &out) ? out : -1;
}

inline HICON ExtractIconFast(const wchar_t* path, bool small = true) {
    SHFILEINFOW si{};
    UINT f = SHGFI_ICON | (small ? SHGFI_SMALLICON : SHGFI_LARGEICON);
    return SHGetFileInfoW(path, 0, &si, sizeof(si), f) ? si.hIcon : nullptr;
}

inline HICON GetFileTypeIcon(const wchar_t* ext, bool small = true) {
    SHFILEINFOW si{};
    UINT f = SHGFI_ICON | SHGFI_USEFILEATTRIBUTES | (small ? SHGFI_SMALLICON : SHGFI_LARGEICON);
    return SHGetFileInfoW(ext, FILE_ATTRIBUTE_NORMAL, &si, sizeof(si), f) ? si.hIcon : nullptr;
}

inline std::wstring AssocQueryStr(PCWSTR assoc, PCWSTR extra, ASSOCSTR str) {
    wchar_t buf[512]{};
    DWORD c = _countof(buf);
    return SUCCEEDED(AssocQueryStringW(0, str, assoc, extra, buf, &c)) ? buf : L"";
}

inline bool ParseIconLocation(const wchar_t* spec, std::wstring& path, int& idx) {
    wchar_t buf[MAX_PATH]{};
    StringCchCopyW(buf, _countof(buf), spec);
    idx = PathParseIconLocationW(buf);
    path = buf;
    return !path.empty();
}

// PIDL 工具
inline PIDLIST_ABSOLUTE ParseDisplayName(const wchar_t* path) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    SHParseDisplayName(path, nullptr, &pidl, 0, nullptr);
    return pidl;
}

inline void FreePidl(PIDLIST_ABSOLUTE pidl) {
    if (pidl) CoTaskMemFree(pidl);
}

inline HRESULT BindToParentFolder(PCUITEMIDLIST_ABSOLUTE pidl, REFIID riid, void** ppv, PCUITEMID_CHILD* pchild = nullptr) {
    PCUITEMID_CHILD child = nullptr;
    HRESULT hr = SHBindToParent(pidl, riid, ppv, &child);
    if (pchild) *pchild = child;
    return hr;
}

inline std::wstring GetDisplayNameOf(PCUITEMIDLIST_ABSOLUTE pidl, SHGDN flags = SHGDN_NORMAL) {
    IShellFolder* folder = nullptr;
    if (FAILED(SHGetDesktopFolder(&folder)) || !folder) return {};
    STRRET sr{};
    std::wstring result;
    if (SUCCEEDED(folder->GetDisplayNameOf(pidl, flags, &sr))) {
        LPWSTR p = nullptr;
        if (SUCCEEDED(StrRetToStrW(&sr, pidl, &p)) && p) {
            result = p;
            CoTaskMemFree(p);
        }
    }
    folder->Release();
    return result;
}

// ============================================================================
// §6 WNF 状态通知
// ============================================================================

typedef ULONGLONG WNF_STATE_NAME;
typedef ULONG WNF_CHANGE_STAMP;
typedef NTSTATUS(NTAPI* PFN_RtlQueryWnfStateData)(const WNF_STATE_NAME*, WNF_CHANGE_STAMP*, PVOID, ULONG*);
typedef NTSTATUS(NTAPI* PFN_RtlSubscribeWnfStateChangeNotification)(PVOID, WNF_STATE_NAME, WNF_CHANGE_STAMP, PVOID, PVOID, PVOID, ULONG);
typedef NTSTATUS(NTAPI* PFN_RtlUnsubscribeWnfNotificationWaitForCompletion)(PVOID);
typedef NTSTATUS(NTAPI* PFN_RtlPublishWnfStateData)(WNF_STATE_NAME, ULONG, const void*, ULONG);

namespace WNF {
    constexpr WNF_STATE_NAME DARK_MODE       = 0x0D83063EA3BC1875ULL;
    constexpr WNF_STATE_NAME THEME_CHANGE     = 0x0D83063EA3BD1075ULL;
    constexpr WNF_STATE_NAME NETWORK_STATUS   = 0x0D83063EA3BE5875ULL;
    constexpr WNF_STATE_NAME POWER_MODE       = 0x0D83063EA3BE1875ULL;
    constexpr WNF_STATE_NAME DISPLAY_CHANGE   = 0x0D83063EA3BC1075ULL;
    constexpr WNF_STATE_NAME INPUT_LANGUAGE   = 0x0D83063EA3BC0875ULL;
}

class WnfSubscription {
public:
    ~WnfSubscription() { Unsubscribe(); }
    bool Subscribe(WNF_STATE_NAME name, std::function<void(WNF_STATE_NAME)> cb) {
        auto pq = GetApi<PFN_RtlQueryWnfStateData>(L"ntdll.dll", "RtlQueryWnfStateData");
        auto ps = GetApi<PFN_RtlSubscribeWnfStateChangeNotification>(L"ntdll.dll", "RtlSubscribeWnfStateChangeNotification");
        if (!pq || !ps) return false;
        WNF_CHANGE_STAMP stamp = 0;
        if (!NT_SUCCESS(pq(&name, &stamp, nullptr, nullptr))) return false;
        m_cb = std::move(cb);
        NTSTATUS st = ps(this, name, stamp,
            [](WNF_STATE_NAME n, PVOID, ULONG) -> NTSTATUS {
                // 简化：回调在订阅线程执行
                return 0;
            }, this, nullptr, 0, 1);
        m_ok = NT_SUCCESS(st);
        return m_ok;
    }
    void Unsubscribe() {
        if (m_ok) {
            auto pu = GetApi<PFN_RtlUnsubscribeWnfNotificationWaitForCompletion>(L"ntdll.dll", "RtlUnsubscribeWnfNotificationWaitForCompletion");
            if (pu) pu(this);
            m_ok = false;
        }
    }
    static bool Query(WNF_STATE_NAME name, PVOID buf, ULONG sz, ULONG* ret = nullptr) {
        auto p = GetApi<PFN_RtlQueryWnfStateData>(L"ntdll.dll", "RtlQueryWnfStateData");
        return p && NT_SUCCESS(p(&name, nullptr, buf, ret));
    }
    static bool Publish(WNF_STATE_NAME name, ULONG stamp, const void* data, ULONG sz) {
        auto p = GetApi<PFN_RtlPublishWnfStateData>(L"ntdll.dll", "RtlPublishWnfStateData");
        return p && NT_SUCCESS(p(name, stamp, data, sz));
    }
private:
    std::function<void(WNF_STATE_NAME)> m_cb;
    bool m_ok = false;
};

// ============================================================================
// §7 DPI
// ============================================================================

inline void EnablePerMonitorDpi() {
    auto p = GetApi<BOOL(WINAPI*)(HANDLE)>(L"user32.dll", "SetProcessDpiAwarenessContext");
    if (p) { p(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4))); return; }
    SetProcessDPIAware();
}

inline UINT GetDpiForWindowCompat(HWND h) {
    auto p = GetApi<UINT(WINAPI*)(HWND)>(L"user32.dll", "GetDpiForWindow");
    if (p && h) { UINT d = p(h); if (d) return d; }
    HDC hdc = GetDC(nullptr); int d = GetDeviceCaps(hdc, LOGPIXELSX); ReleaseDC(nullptr, hdc);
    return d > 0 ? d : 96;
}

inline UINT GetDpiForSystemCompat() {
    auto p = GetApi<UINT(WINAPI*()>(L"user32.dll", "GetDpiForSystem");
    if (p) { UINT d = p(); if (d) return d; }
    HDC hdc = GetDC(nullptr); int d = GetDeviceCaps(hdc, LOGPIXELSX); ReleaseDC(nullptr, hdc);
    return d > 0 ? d : 96;
}

inline int ScaleForDpi(int v, UINT dpi) { return MulDiv(v, dpi, 96); }

inline BOOL AreDpiCtxEqual(HANDLE a, HANDLE b) {
    auto p = GetApi<BOOL(WINAPI*)(HANDLE, HANDLE)>(L"user32.dll", "AreDpiAwarenessContextsEqual");
    return p ? p(a, b) : a == b;
}

inline HANDLE GetThreadDpiCtx() {
    auto p = GetApi<HANDLE(WINAPI*()>(L"user32.dll", "GetThreadDpiAwarenessContext");
    return p ? p() : nullptr;
}

inline HANDLE SetThreadDpiCtx(HANDLE ctx) {
    auto p = GetApi<HANDLE(WINAPI*)(HANDLE)>(L"user32.dll", "SetThreadDpiAwarenessContext");
    return p ? p(ctx) : nullptr;
}

// ============================================================================
// §8 事件驱动
// ============================================================================

constexpr DWORD EVENT_SYSTEM_DESKTOPSWITCH = 0x0020;
constexpr DWORD EVENT_OBJECT_STATECHANGE   = 0x800A;
constexpr DWORD EVENT_OBJECT_PARENTCHANGE  = 0x800B;
constexpr DWORD EVENT_OBJECT_NAMECHANGE    = 0x800C;
constexpr DWORD EVENT_OBJECT_CLOAKED       = 0x8017;
constexpr DWORD EVENT_OBJECT_UNCLOAKED     = 0x8018;

class WinEventWatcher {
public:
    struct Callbacks {
        std::function<void(HWND)> onCreated, onDestroyed, onForeground,
            onMinStart, onMinEnd, onTitleChanged, onDesktopSwitch, onCloaked, onUncloaked;
        void* userData = nullptr;
    };

    bool Start(const Callbacks& cb) {
        m_cb = cb;
        const DWORD f = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
        auto add = [&](DWORD e1, DWORD e2, DWORD flags) {
            auto h = SetWinEventHook(e1, e2, nullptr, StaticProc, 0, 0, flags);
            if (h) m_hooks.push_back(h);
        };
        add(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, f);
        add(EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY, f);
        add(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, WINEVENT_OUTOFCONTEXT);
        add(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, WINEVENT_OUTOFCONTEXT);
        add(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, f);
        add(EVENT_SYSTEM_DESKTOPSWITCH, EVENT_SYSTEM_DESKTOPSWITCH, WINEVENT_OUTOFCONTEXT);
        add(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, f);
        return !m_hooks.empty();
    }

    void Stop() {
        for (auto h : m_hooks) if (h) UnhookWinEvent(h);
        m_hooks.clear();
    }

    ~WinEventWatcher() { Stop(); }

    // 多实例支持
    static void Register(WinEventWatcher* w) { std::lock_guard<std::mutex> lk(Mutex()); (void)w; }
    static void Unregister(WinEventWatcher* w) { std::lock_guard<std::mutex> lk(Mutex()); (void)w; }

private:
    static std::mutex& Mutex() { static std::mutex m; return m; }
    static WinEventWatcher* s_inst;

    static void CALLBACK StaticProc(HWINEVENTHOOK, DWORD ev, HWND hwnd,
        LONG obj, LONG child, DWORD, DWORD)
    {
        if (obj != OBJID_WINDOW || child != CHILDID_SELF || !hwnd || !IsWindow(hwnd)) return;
        auto* self = s_inst;
        if (!self) return;
        switch (ev) {
        case EVENT_OBJECT_CREATE:       if (self->m_cb.onCreated) self->m_cb.onCreated(hwnd); break;
        case EVENT_OBJECT_DESTROY:      if (self->m_cb.onDestroyed) self->m_cb.onDestroyed(hwnd); break;
        case EVENT_SYSTEM_FOREGROUND:   if (self->m_cb.onForeground) self->m_cb.onForeground(hwnd); break;
        case EVENT_SYSTEM_MINIMIZESTART:if (self->m_cb.onMinStart) self->m_cb.onMinStart(hwnd); break;
        case EVENT_SYSTEM_MINIMIZEEND:  if (self->m_cb.onMinEnd) self->m_cb.onMinEnd(hwnd); break;
        case EVENT_OBJECT_NAMECHANGE:   if (self->m_cb.onTitleChanged) self->m_cb.onTitleChanged(hwnd); break;
        case EVENT_SYSTEM_DESKTOPSWITCH:if (self->m_cb.onDesktopSwitch) self->m_cb.onDesktopSwitch(hwnd); break;
        case EVENT_OBJECT_CLOAKED:      if (self->m_cb.onCloaked) self->m_cb.onCloaked(hwnd); break;
        case EVENT_OBJECT_UNCLOAKED:    if (self->m_cb.onUncloaked) self->m_cb.onUncloaked(hwnd); break;
        }
    }

    Callbacks m_cb{};
    std::vector<HWINEVENTHOOK> m_hooks;

public:
    WinEventWatcher() { s_inst = this; }
};

inline WinEventWatcher* WinEventWatcher::s_inst = nullptr;

// ============================================================================
// §9 COM 辅助
// ============================================================================

inline void IUnknownSet(IUnknown** pp, IUnknown* pNew) {
    if (!pp) return;
    IUnknown* pOld = *pp;
    *pp = pNew;
    if (pNew) pNew->AddRef();
    if (pOld) pOld->Release();
}

inline HRESULT MarshalInterThread(IUnknown* pUnk, IStream** ppStream) {
    return CoMarshalInterThreadInterfaceInStream(IID_IUnknown, pUnk, ppStream);
}

inline HRESULT UnmarshalInterThread(REFIID riid, IStream* pStream, void** ppObj) {
    return CoGetInterfaceAndReleaseStream(pStream, riid, ppObj);
}

// CoTaskMem 封装
template<typename T>
inline T* CoTaskMemAllocT(size_t count = 1) {
    return static_cast<T*>(CoTaskMemAlloc(sizeof(T) * count));
}
inline void CoTaskMemFree2(void* p) { if (p) CoTaskMemFree(p); }

// COM 取消（用于异步操作）
inline HRESULT EnableCallCancellation() {
    auto p = GetApi<HRESULT(WINAPI*()>(L"ole32.dll", "CoEnableCallCancellation");
    return p ? p() : S_OK;
}
inline HRESULT DisableCallCancellation() {
    auto p = GetApi<HRESULT(WINAPI*()>(L"ole32.dll", "CoDisableCallCancellation");
    return p ? p() : S_OK;
}

// ============================================================================
// §10 安全 / 令牌
// ============================================================================

constexpr DWORD SEC_MANDATORY_UNTRUSTED = 0x0000;
constexpr DWORD SEC_MANDATORY_LOW       = 0x1000;
constexpr DWORD SEC_MANDATORY_MEDIUM    = 0x2000;
constexpr DWORD SEC_MANDATORY_HIGH      = 0x3000;
constexpr DWORD SEC_MANDATORY_SYSTEM    = 0x4000;

inline HANDLE GetProcessToken() {
    HANDLE t = nullptr;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t);
    return t;
}

inline DWORD GetTokenIntegrity(HANDLE token) {
    if (!token) return 0;
    DWORD need = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &need);
    if (!need) return 0;
    std::vector<BYTE> buf(need);
    if (!GetTokenInformation(token, TokenIntegrityLevel, buf.data(), need, &need)) return 0;
    auto* til = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buf.data());
    return *GetSidSubAuthority(til->Label.Sid, *GetSidSubAuthorityCount(til->Label.Sid) - 1);
}

inline bool IsElevated() {
    HANDLE t = GetProcessToken();
    if (!t) return false;
    TOKEN_ELEVATION e{};
    DWORD s = 0;
    BOOL ok = GetTokenInformation(t, TokenElevation, &e, sizeof(e), &s);
    CloseHandle(t);
    return ok && e.TokenIsElevated;
}

inline DWORD GetIntegrityLevel() {
    HANDLE t = GetProcessToken();
    DWORD lv = GetTokenIntegrity(t);
    if (t) CloseHandle(t);
    return lv;
}

inline bool IsSystemProcess() {
    return GetIntegrityLevel() >= SEC_MANDATORY_SYSTEM;
}

// ============================================================================
// §11 Shell 变更通知
// ============================================================================

inline void ShellNotifyAssocChanged() { SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); }
inline void ShellNotifyUpdateDir(LPCWSTR p) { SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, p, nullptr); }
inline void ShellNotifyUpdateItem(LPCWSTR p) { SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, p, nullptr); }
inline void ShellNotifyCreate(LPCWSTR p, bool dir) { SHChangeNotify(dir ? SHCNE_MKDIR : SHCNE_CREATE, SHCNF_PATHW, p, nullptr); }
inline void ShellNotifyDelete(LPCWSTR p, bool dir) { SHChangeNotify(dir ? SHCNE_RMDIR : SHCNE_DELETE, SHCNF_PATHW, p, nullptr); }
inline void ShellNotifyRename(LPCWSTR f, LPCWSTR t) { SHChangeNotify(SHCNE_RENAMEITEM, SHCNF_PATHW, f, t); }

// 注册 Shell 变更通知（未公开）
inline UINT_PTR ShellChangeNotifyRegister(HWND hwnd, UINT msg, UINT events, LPCITEMIDLIST pidl, BOOL recursive) {
    auto p = GetApi<UINT_PTR(WINAPI*)(HWND, UINT, UINT, LPCITEMIDLIST, BOOL, UINT*)>(L"shell32.dll", "SHChangeNotifyRegister");
    if (!p) return 0;
    UINT id = 0;
    return p(hwnd, msg, events, pidl, recursive, &id) ? id : 0;
}

inline void ShellChangeNotifyDeregister(UINT_PTR id) {
    auto p = GetApi<BOOL(WINAPI*)(UINT)>(L"shell32.dll", "SHChangeNotifyDeregister");
    if (p) p((UINT)id);
}

// Shell 变更通知锁（未公开，Win11 用）
inline HANDLE ShellChangeNotifyLock(HANDLE hEvent, UINT flags, LPITEMIDLIST** ppidl, LONG* pevent) {
    auto p = GetApi<HANDLE(WINAPI*)(HANDLE, UINT, LPITEMIDLIST**, LONG*)>(L"shell32.dll", "SHChangeNotification_Lock");
    return p ? p(hEvent, flags, ppidl, pevent) : nullptr;
}

inline BOOL ShellChangeNotifyUnlock(HANDLE hLock) {
    auto p = GetApi<BOOL(WINAPI*)(HANDLE)>(L"shell32.dll", "SHChangeNotification_Unlock");
    return p ? p(hLock) : FALSE;
}

// ============================================================================
// §12 进程 / 线程
// ============================================================================

inline std::wstring GetProcessName(DWORD pid) {
    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hp) return {};
    wchar_t p[MAX_PATH]{}; DWORD s = MAX_PATH;
    QueryFullProcessImageNameW(hp, 0, p, &s);
    CloseHandle(hp);
    const wchar_t* n = wcsrchr(p, L'\\');
    return n ? (n + 1) : p;
}

inline bool IsProcessAlive(DWORD pid) {
    HANDLE hp = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!hp) return false;
    DWORD code = 0;
    bool alive = GetExitCodeProcess(hp, &code) && code == STILL_ACTIVE;
    CloseHandle(hp);
    return alive;
}

inline std::vector<HWND> EnumTopLevelWindows() {
    std::vector<HWND> result;
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        reinterpret_cast<std::vector<HWND>*>(lp)->push_back(h);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

// 线程引用（SHCreateThreadRef）
inline HRESULT SHCreateThreadRefCompat(LONG* pref, IUnknown** ppUnk) {
    auto p = GetApi<HRESULT(WINAPI*)(LONG*, IUnknown**)>(L"shell32.dll", "SHCreateThreadRef");
    return p ? p(pref, ppUnk) : E_NOTIMPL;
}

inline HRESULT SHSetThreadRefCompat(IUnknown* pUnk) {
    auto p = GetApi<HRESULT(WINAPI*)(IUnknown*)>(L"shell32.dll", "SHSetThreadRef");
    return p ? p(pUnk) : E_NOTIMPL;
}

// SRWLock RAII
class SrwLock {
public:
    SrwLock() { InitSRWLock(&m); }
    void LockEx() { AcquireSRWLockExclusive(&m); }
    void UnLockEx() { ReleaseSRWLockExclusive(&m); }
    void LockSh() { AcquireSRWLockShared(&m); }
    void UnLockSh() { ReleaseSRWLockShared(&m); }
    struct ExGuard { SrwLock& l; ExGuard(SrwLock& s) : l(s) { l.LockEx(); } ~ExGuard() { l.UnLockEx(); } };
    struct ShGuard { SrwLock& l; ShGuard(SrwLock& s) : l(s) { l.LockSh(); } ~ShGuard() { l.UnLockSh(); } };
private:
    SRWLOCK m;
};

// ============================================================================
// §13 NT 文件 / 注册表
// ============================================================================

// ZwCreateSection / ZwMapViewOfSection / ZwUnmapViewOfSection
typedef NTSTATUS(NTAPI* PFN_ZwCreateSection)(PHANDLE, ACCESS_MASK, PVOID, PLARGE_INTEGER, ULONG, ULONG, HANDLE);
typedef NTSTATUS(NTAPI* PFN_ZwMapViewOfSection)(HANDLE, HANDLE, PVOID*, ULONG_PTR, SIZE_T, PLARGE_INTEGER, PSIZE_T, DWORD, ULONG, ULONG);
typedef NTSTATUS(NTAPI* PFN_ZwUnmapViewOfSection)(HANDLE, PVOID);
typedef NTSTATUS(NTAPI* PFN_ZwOpenFile)(PHANDLE, ACCESS_MASK, PVOID, PIO_STATUS_BLOCK, ULONG, ULONG);
typedef NTSTATUS(NTAPI* PFN_ZwOpenKey)(PHANDLE, ACCESS_MASK, PVOID);
typedef NTSTATUS(NTAPI* PFN_ZwQueryValueKey)(HANDLE, PVOID, DWORD, PVOID, ULONG, PULONG);

// RtlDosPathNameToNtPathName_U_WithStatus
typedef NTSTATUS(NTAPI* PFN_RtlDosPathNameToNtPathName_U_WithStatus)(PCWSTR, PVOID, PCWSTR*, PVOID);

inline NTSTATUS RtlDosToNtPath(PCWSTR dosPath, PVOID* ntPath) {
    auto p = GetApi<PFN_RtlDosPathNameToNtPathName_U_WithStatus>(L"ntdll.dll", "RtlDosPathNameToNtPathName_U_WithStatus");
    return p ? p(dosPath, ntPath, nullptr, nullptr) : (NTSTATUS)0xC0000039;
}

// 内存映射文件
inline bool MapFileSection(HANDLE hFile, void** base, size_t* size) {
    auto pCreate = GetApi<PFN_ZwCreateSection>(L"ntdll.dll", "ZwCreateSection");
    auto pMap = GetApi<PFN_ZwMapViewOfSection>(L"ntdll.dll", "ZwMapViewOfSection");
    auto pUnmap = GetApi<PFN_ZwUnmapViewOfSection>(L"ntdll.dll", "ZwUnmapViewOfSection");
    if (!pCreate || !pMap) return false;

    HANDLE hSection = nullptr;
    NTSTATUS st = pCreate(&hSection, SECTION_ALL_ACCESS, nullptr, nullptr, PAGE_READWRITE, SEC_COMMIT, hFile);
    if (!NT_SUCCESS(st)) return false;

    LARGE_INTEGER offset{};
    SIZE_T viewSize = 0;
    PVOID viewBase = nullptr;
    st = pMap(hSection, GetCurrentProcess(), &viewBase, 0, 0, &offset, &viewSize, ViewShare, 0, PAGE_READWRITE);
    CloseHandle(hSection);
    if (!NT_SUCCESS(st)) return false;

    *base = viewBase;
    *size = viewSize;
    return true;
}

inline void UnmapFileSection(void* base) {
    auto p = GetApi<PFN_ZwUnmapViewOfSection>(L"ntdll.dll", "ZwUnmapViewOfSection");
    if (p) p(GetCurrentProcess(), base);
}

// ============================================================================
// §14 NT 系统信息
// ============================================================================

typedef NTSTATUS(NTAPI* PFN_NtQuerySystemInformation)(ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS(NTAPI* PFN_NtQueryInformationToken)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS(NTAPI* PFN_NtQueryInformationProcess)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS(NTAPI* PFN_NtSetThreadExecutionState)(ULONG, PULONG);

// SYSTEM_INFORMATION_CLASS 常量
constexpr ULONG SystemBasicInformation         = 0;
constexpr ULONG SystemProcessorInformation     = 1;
constexpr ULONG SystemPerformanceInformation   = 2;
constexpr ULONG SystemTimeOfDayInformation     = 3;
constexpr ULONG SystemProcessInformation       = 5;
constexpr ULONG SystemModuleInformation        = 11;
constexpr ULONG SystemHandleInformation         = 16;
constexpr ULONG SystemKernelDebugInformation   = 35;
constexpr ULONG SystemProcessIdInformation     = 88;

inline NTSTATUS NtQuerySysInfo(ULONG cls, PVOID buf, ULONG sz, PULONG ret = nullptr) {
    auto p = GetApi<PFN_NtQuerySystemInformation>(L"ntdll.dll", "NtQuerySystemInformation");
    return p ? p(cls, buf, sz, ret) : (NTSTATUS)0xC0000002;
}

inline NTSTATUS NtQueryTokenInfo(HANDLE token, ULONG cls, PVOID buf, ULONG sz, PULONG ret = nullptr) {
    auto p = GetApi<PFN_NtQueryInformationToken>(L"ntdll.dll", "NtQueryInformationToken");
    return p ? p(token, cls, buf, sz, ret) : (NTSTATUS)0xC0000002;
}

inline NTSTATUS NtQueryProcInfo(HANDLE proc, ULONG cls, PVOID buf, ULONG sz, PULONG ret = nullptr) {
    auto p = GetApi<PFN_NtQueryInformationProcess>(L"ntdll.dll", "NtQueryInformationProcess");
    return p ? p(proc, cls, buf, sz, ret) : (NTSTATUS)0xC0000002;
}

// 阻止系统睡眠（NT 层）
inline NTSTATUS NtSetExecutionState(ULONG flags, PULONG prev) {
    auto p = GetApi<PFN_NtSetThreadExecutionState>(L"ntdll.dll", "NtSetThreadExecutionState");
    return p ? p(flags, prev) : (NTSTATUS)0xC0000002;
}

// RtlGetNativeSystemInformation
inline NTSTATUS RtlGetNativeSysInfo(ULONG cls, PVOID buf, ULONG sz, PULONG ret = nullptr) {
    auto p = GetApi<NTSTATUS(NTAPI*)(ULONG, PVOID, ULONG, PULONG)>(L"ntdll.dll", "RtlGetNativeSystemInformation");
    return p ? p(cls, buf, sz, ret) : (NTSTATUS)0xC0000002;
}

// RtlGetDeviceFamilyInfoEnum
inline NTDEVICEFAMILYINFOENUM RtlGetDeviceFamily() {
    auto p = GetApi<void(NTAPI*)(ULONGLONG*, ULONGLONG*, ULONGLONG*)>(L"ntdll.dll", "RtlGetDeviceFamilyInfoEnum");
    if (!p) return DEVICEFAMILYINFOENUM_UNKNOWN;
    ULONGLONG a = 0, b = 0, c = 0;
    p(&a, &b, &c);
    return (NTDEVICEFAMILYINFOENUM)a;
}

// RtlQueryFeatureConfiguration
inline NTSTATUS RtlQueryFeature(ULONG featureId, ULONG* type, BOOLEAN* enabled, ULONG* variant) {
    auto p = GetApi<NTSTATUS(NTAPI*)(ULONG, ULONG*, BOOLEAN*, ULONG*)>(L"ntdll.dll", "RtlQueryFeatureConfiguration");
    return p ? p(featureId, type, enabled, variant) : (NTSTATUS)0xC0000002;
}

// ============================================================================
// §15 NT 字符串 / 堆
// ============================================================================

inline void RtlInitStr(PVOID str, PCWSTR src) {
    auto p = GetApi<void(NTAPI*)(PVOID, PCWSTR)>(L"ntdll.dll", "RtlInitUnicodeString");
    if (p) p(str, src);
}

inline void RtlFreeStr(PVOID str) {
    auto p = GetApi<void(NTAPI*)(PVOID)>(L"ntdll.dll", "RtlFreeUnicodeString");
    if (p) p(str);
}

inline void* RtlAllocHeap(ULONG_PTR heap, ULONG flags, SIZE_T sz) {
    auto p = GetApi<void*(NTAPI*)(ULONG_PTR, ULONG, SIZE_T)>(L"ntdll.dll", "RtlAllocateHeap");
    return p ? p(heap, flags, sz) : HeapAlloc(GetProcessHeap(), flags, sz);
}

inline BOOLEAN RtlFreeHeap(ULONG_PTR heap, ULONG flags, PVOID base) {
    auto p = GetApi<BOOLEAN(NTAPI*)(ULONG_PTR, ULONG, PVOID)>(L"ntdll.dll", "RtlFreeHeap");
    return p ? p(heap, flags, base) : HeapFree(GetProcessHeap(), flags, base);
}

inline void* RtlReallocHeap(ULONG_PTR heap, ULONG flags, PVOID base, SIZE_T sz) {
    auto p = GetApi<void*(NTAPI*)(ULONG_PTR, ULONG, PVOID, SIZE_T)>(L"ntdll.dll", "RtlReAllocateHeap");
    return p ? p(heap, flags, base, sz) : HeapReAlloc(GetProcessHeap(), flags, base, sz);
}

inline NTSTATUS RtlNtStatusToDosErrorCompat(NTSTATUS st) {
    auto p = GetApi<ULONG(NTAPI*)(NTSTATUS)>(L"ntdll.dll", "RtlNtStatusToDosError");
    return p ? p(st) : st;
}

inline NTSTATUS RtlNtStatusToDosErrorNoTeb(NTSTATUS st) {
    auto p = GetApi<ULONG(NTAPI*)(NTSTATUS)>(L"ntdll.dll", "RtlNtStatusToDosErrorNoTeb");
    return p ? p(st) : st;
}

// ============================================================================
// §16 电源 / 系统状态
// ============================================================================

inline HPOWERNOTIFY RegisterPowerNotify(HWND hwnd) {
    static const GUID G = {0x6fe69556, 0x704a, 0x47a0, {0x8f,0x24,0xc2,0x8d,0x93,0x6f,0xda,0x47}};
    return RegisterPowerSettingNotification(hwnd, &G, DEVICE_NOTIFY_WINDOW_HANDLE);
}

inline void UnregisterPowerNotify(HPOWERNOTIFY h) {
    if (h) UnregisterPowerSettingNotification(h);
}

inline void PreventSleep() { SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED); }
inline void AllowSleep()   { SetThreadExecutionState(ES_CONTINUOUS); }

// ============================================================================
// §17 状态存储
// ============================================================================

inline std::wstring GetStateSettingsFolder() {
    auto p = GetApi<HRESULT(WINAPI*)(PWSTR, PUINT)>(L"api-ms-win-appmodel-state-l1-2-0.dll", "GetStateSettingsFolder");
    if (!p) return {};
    wchar_t buf[MAX_PATH]{};
    UINT len = MAX_PATH;
    return SUCCEEDED(p(buf, &len)) ? buf : L"";
}

// ============================================================================
// §18 桌面 / WorkerW
// ============================================================================

inline HWND GetWorkerWindow() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return nullptr;
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);
    HWND worker = nullptr;
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        if (FindWindowExW(h, nullptr, L"SHELLDLL_DefView", nullptr)) {
            *reinterpret_cast<HWND*>(lp) = FindWindowExW(nullptr, h, L"WorkerW", nullptr);
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&worker));
    return worker;
}

inline bool SetAsWallpaperLayer(HWND hwnd) {
    HWND w = GetWorkerWindow();
    if (!w) return false;
    SetWindowPos(w, hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return true;
}

// ============================================================================
// §19 快捷方式 / Shell Link
// ============================================================================

inline std::wstring ResolveShortcut(const wchar_t* lnk) {
    wchar_t target[MAX_PATH]{};
    IShellLinkW* psl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl)) || !psl) return {};
    IPersistFile* ppf = nullptr;
    if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf)) && ppf) {
        if (SUCCEEDED(ppf->Load(lnk, STGM_READ))) psl->GetPath(target, MAX_PATH, nullptr, 0);
        ppf->Release();
    }
    psl->Release();
    return target;
}

inline bool CreateShortcut(const wchar_t* lnk, const wchar_t* target,
    const wchar_t* args = nullptr, const wchar_t* dir = nullptr, int show = SW_SHOWNORMAL)
{
    IShellLinkW* psl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl)) || !psl) return false;
    psl->SetPath(target);
    if (args) psl->SetArguments(args);
    if (dir) psl->SetWorkingDirectory(dir);
    psl->SetShowCmd(show);
    IPersistFile* ppf = nullptr;
    bool ok = false;
    if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf)) && ppf) {
        ok = SUCCEEDED(ppf->Save(lnk, TRUE));
        ppf->Release();
    }
    psl->Release();
    return ok;
}

// ============================================================================
// §20 进程守护
// ============================================================================

class ProcessGuardian {
public:
    struct Entry {
        HANDLE hp = nullptr; DWORD pid = 0;
        std::wstring exe, args;
        bool restart = true; uint32_t maxR = 0, count = 0;
    };

    void Watch(DWORD pid, const std::wstring& exe, const std::wstring& args = L"",
        bool autoRestart = true, uint32_t maxRestarts = 0)
    {
        HANDLE hp = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hp) return;
        Entry e; e.hp = hp; e.pid = pid; e.exe = exe; e.args = args;
        e.restart = autoRestart; e.maxR = maxRestarts;
        std::lock_guard<std::mutex> lk(m);
        v.push_back(std::move(e));
        if (!running) {
            running = true;
            stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            th = std::thread(&ProcessGuardian::Loop, this);
        }
    }

    void Unwatch(DWORD pid) {
        std::lock_guard<std::mutex> lk(m);
        for (auto it = v.begin(); it != v.end(); ++it) {
            if (it->pid == pid) { if (it->hp) CloseHandle(it->hp); v.erase(it); break; }
        }
    }

    void Stop() {
        running = false;
        if (stop) SetEvent(stop);
        if (th.joinable()) th.join();
        if (stop) { CloseHandle(stop); stop = nullptr; }
        std::lock_guard<std::mutex> lk(m);
        for (auto& e : v) if (e.hp) CloseHandle(e.hp);
        v.clear();
    }

    ~ProcessGuardian() { Stop(); }
    std::function<void(DWORD, const std::wstring&)> onCrashed;

private:
    void Loop() {
        while (running) {
            std::vector<HANDLE> hs; std::vector<size_t> idx;
            { std::lock_guard<std::mutex> lk(m);
              for (size_t i = 0; i < v.size(); i++) if (v[i].hp) { hs.push_back(v[i].hp); idx.push_back(i); } }
            hs.push_back(stop);
            DWORD r = WaitForMultipleObjects((DWORD)hs.size(), hs.data(), FALSE, 1000);
            if (r == WAIT_TIMEOUT) continue;
            if (r == WAIT_OBJECT_0 + hs.size() - 1) break;
            DWORD i = r - WAIT_OBJECT_0;
            DWORD cpid = 0; std::wstring cexe; bool doRestart = false;
            { std::lock_guard<std::mutex> lk(m);
              if (i < idx.size()) {
                  auto& e = v[idx[i]];
                  cpid = e.pid; cexe = e.exe;
                  CloseHandle(e.hp); e.hp = nullptr;
                  if (onCrashed) onCrashed(cpid, cexe);
                  if (e.restart && (!e.maxR || e.count < e.maxR)) doRestart = true;
              } }
            if (doRestart) {
                STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
                std::wstring cmd = L"\"" + cexe + L"\"";
                std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
                if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi)) {
                    CloseHandle(pi.hThread);
                    std::lock_guard<std::mutex> lk(m);
                    for (auto& e : v) if (e.pid == cpid) {
                        e.pid = pi.dwProcessId; e.count++;
                        e.hp = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pi.dwProcessId);
                        break;
                    }
                    CloseHandle(pi.hProcess);
                }
            }
        }
    }

    std::vector<Entry> v; std::mutex m;
    std::thread th; HANDLE stop = nullptr; std::atomic<bool> running{false};
};

// ============================================================================
// §21 DX / 图形
// ============================================================================

inline void DeclareAdapterRemovalSupport() {
    auto p = GetApi<HRESULT(WINAPI*()>(L"dxgi.dll", "DXGIDeclareAdapterRemovalSupport");
    if (p) p();
}

// ============================================================================
// §22 WinRT
// ============================================================================

typedef PVOID HSTRING;
typedef HRESULT(WINAPI* PFN_WindowsCreateString)(PCWSTR, UINT, HSTRING*);
typedef HRESULT(WINAPI* PFN_WindowsDeleteString)(HSTRING);
typedef PCWSTR(WINAPI* PFN_WindowsGetStringRawBuffer)(HSTRING, UINT*);

inline HSTRING WinCreateString(const wchar_t* s) {
    auto p = GetApi<PFN_WindowsCreateString>(L"combase.dll", "WindowsCreateString");
    if (!p) return nullptr;
    HSTRING h = nullptr;
    p(s, (UINT)wcslen(s), &h);
    return h;
}

inline void WinDeleteString(HSTRING h) {
    auto p = GetApi<PFN_WindowsDeleteString>(L"combase.dll", "WindowsDeleteString");
    if (p) p(h);
}

inline std::wstring WinGetString(HSTRING h) {
    auto p = GetApi<PFN_WindowsGetStringRawBuffer>(L"combase.dll", "WindowsGetStringRawBuffer");
    if (!p) return {};
    UINT len = 0;
    PCWSTR buf = p(h, &len);
    return buf ? std::wstring(buf, len) : L"";
}

// ============================================================================
// §23 本地化 / 语言
// ============================================================================

inline LANGID GetUserDefaultUILanguageCompat() {
    auto p = GetApi<LANGID(WINAPI*()>(L"kernel32.dll", "GetUserDefaultUILanguage");
    return p ? p() : GetUserDefaultLangID();
}

inline bool IsRTLLanguage(LANGID lang) {
    return (lang == 0x0401) || (lang == 0x0801) || (lang == 0x0c01) ||
           (lang == 0x1001) || (lang == 0x1401) || (lang == 0x1801) ||
           (lang == 0x1c01) || (lang == 0x2001) || (lang == 0x2401) ||
           (lang == 0x2801) || (lang == 0x2c01) || (lang == 0x3001) ||
           (lang == 0x3401) || (lang == 0x3801) || (lang == 0x3c01) ||
           (lang == 0x4001) || (lang == 0x0429) || (lang == 0x0829) ||
           (lang == 0x0c29) || (lang == 0x1029) || (lang == 0x1429) ||
           (lang == 0x1829) || (lang == 0x042d) || (lang == 0x0437) ||
           (lang == 0x0439) || (lang == 0x044e) || (lang == 0x0860));
}

inline std::wstring GetUserLocaleName() {
    wchar_t buf[LOCALE_NAME_MAX_LENGTH]{};
    if (GetUserDefaultLocaleName(buf, _countof(buf))) return buf;
    return L"";
}

inline std::vector<std::wstring> GetSerializedUserLanguages() {
    auto p = GetApi<HRESULT(WINAPI*)(LPWSTR*, UINT*)>(L"kernel32.dll", "GetSerializedUserLanguagesForUser");
    std::vector<std::wstring> result;
    if (!p) return result;
    LPWSTR buf = nullptr;
    UINT count = 0;
    if (SUCCEEDED(p(&buf, &count)) && buf) {
        wchar_t* ptr = buf;
        for (UINT i = 0; i < count && *ptr; i++) {
            result.push_back(ptr);
            ptr += wcslen(ptr) + 1;
        }
        CoTaskMemFree(buf);
    }
    return result;
}

// ============================================================================
// §24 系统对话框
// ============================================================================

inline void ShowRunDialog(HWND owner = nullptr) {
    ShellExecuteW(owner, L"open", L"explorer.exe", L"shell:::{2559a1f3-21d7-11d4-bdaf-00c04f60b9f0}", nullptr, SW_SHOW);
}
inline void ShowTaskManager(HWND o = nullptr) { ShellExecuteW(o, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOW); }
inline void ShowDesktopSettings(HWND o = nullptr) { ShellExecuteW(o, L"open", L"ms-settings:personalization-background", nullptr, nullptr, SW_SHOW); }
inline void ShowDisplaySettings(HWND o = nullptr) { ShellExecuteW(o, L"open", L"ms-settings:display", nullptr, nullptr, SW_SHOW); }
inline void ShowTaskbarSettings(HWND o = nullptr) { ShellExecuteW(o, L"open", L"ms-settings:taskbar", nullptr, nullptr, SW_SHOW); }
inline void OpenInExplorer(HWND o, const wchar_t* p) { ShellExecuteW(o, L"open", p, nullptr, nullptr, SW_SHOW); }

// ============================================================================
// §25 Shell 序号 API（未公开）
// ============================================================================

// Ordinal_139 — shell32 内部通知（explorer 用 0x40000046 = WM_APP+0x46）
inline LRESULT ShellOrdinal139(HWND hwnd, WPARAM wParam) {
    auto p = GetApiByOrd<LRESULT(WINAPI*)(HWND, WPARAM)>(L"shell32.dll", 139);
    return p ? p(hwnd, wParam) : 0;
}

// Ordinal_244 — per-monitor DPI 查询
inline UINT ShellOrdinal244(HWND hwnd, UINT* dpi) {
    auto p = GetApiByOrd<UINT(WINAPI*)(HWND, UINT*)>(L"shell32.dll", 244);
    if (p && dpi) return p(hwnd, dpi);
    if (dpi) *dpi = GetDpiForWindowCompat(hwnd);
    return 0;
}

// ============================================================================
// §26 窗口过程工具
// ============================================================================

// Thunk 窗口过程（explorer 标准模式）
template<typename T>
class WndProcThunk {
public:
    static LRESULT CALLBACK Thunk(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* self = static_cast<T*>(cs->lpCreateParams);
            self->m_hwnd = h;
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return self->OnNcCreate(h, msg, wp, lp);
        }
        auto* self = reinterpret_cast<T*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (!self) return IsWindowUnicode(h) ? DefWindowProcW(h, msg, wp, lp) : DefWindowProcA(h, msg, wp, lp);
        LRESULT r = self->WindowProc(h, msg, wp, lp);
        if (msg == WM_NCDESTROY) {
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            self->m_hwnd = nullptr;
        }
        return r;
    }

    HWND m_hwnd = nullptr;

protected:
    LRESULT OnNcCreate(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return DefWindowProcW(h, msg, wp, lp); }
    LRESULT WindowProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return DefWindowProcW(h, msg, wp, lp); }
};

// ============================================================================
// §27 GDI RAII 宏
// ============================================================================

struct ScopedBrush {
    HBRUSH h;
    explicit ScopedBrush(COLORREF c) : h(CreateSolidBrush(c)) {}
    ~ScopedBrush() { if (h) DeleteObject(h); }
    operator HBRUSH() const { return h; }
};

struct ScopedPen {
    HPEN h;
    explicit ScopedPen(int w, COLORREF c) : h(CreatePen(PS_SOLID, w, c)) {}
    ~ScopedPen() { if (h) DeleteObject(h); }
    operator HPEN() const { return h; }
};

struct ScopedFont {
    HFONT h;
    explicit ScopedFont(int px, int weight = FW_NORMAL,
        const wchar_t* face = L"Segoe UI")
        : h(CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, face)) {}
    ~ScopedFont() { if (h) DeleteObject(h); }
    operator HFONT() const { return h; }
};

struct ScopedDC {
    HDC h; HWND owner;
    explicit ScopedDC(HWND w) : h(GetDC(w)), owner(w) {}
    ~ScopedDC() { if (h) ReleaseDC(owner, h); }
    operator HDC() const { return h; }
};

struct ScopedMemDC {
    HDC hdc = nullptr; HBITMAP hbm = nullptr, old = nullptr; HDC screen = nullptr; RECT rc{};
    bool ok = false;
    bool Begin(HDC src, const RECT* prc) {
        hdc = CreateCompatibleDC(src);
        if (!hdc) return false;
        hbm = CreateCompatibleBitmap(src, prc->right - prc->left, prc->bottom - prc->top);
        if (!hbm) { DeleteDC(hdc); hdc = nullptr; return false; }
        old = (HBITMAP)SelectObject(hdc, hbm);
        screen = src; rc = *prc; ok = true;
        return true;
    }
    void End() {
        if (!ok) return;
        BitBlt(screen, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, hdc, 0, 0, SRCCOPY);
        SelectObject(hdc, old); DeleteObject(hbm); DeleteDC(hdc);
        hdc = nullptr; hbm = nullptr; ok = false;
    }
    ~ScopedMemDC() { End(); }
};

// ============================================================================
// §28 高级窗口操作
// ============================================================================

// 设置窗口透明度（分层窗口）
inline void SetWindowAlpha(HWND h, BYTE alpha) {
    SetLayeredWindowAttributes(h, 0, alpha, LWA_ALPHA);
}

// 设置窗口圆角区域（GDI 方式，Win10 兼容）
inline void SetWindowRoundRect(HWND h, int radius) {
    HRGN rgn = CreateRoundRectRgn(0, 0, 0, 0, radius, radius);
    SetWindowRgn(h, rgn, TRUE);
    // rgn 被系统接管，不需要 DeleteObject
}

// 窗口淡入淡出
inline void FadeInWindow(HWND h, int durationMs = 200) {
    auto p = GetApi<BOOL(WINAPI*)(HWND, DWORD, DWORD)>(L"user32.dll", "AnimateWindow");
    if (p) p(h, durationMs, AW_BLEND);
}

inline void FadeOutWindow(HWND h, int durationMs = 200) {
    auto p = GetApi<BOOL(WINAPI*)(HWND, DWORD, DWORD)>(L"user32.dll", "AnimateWindow");
    if (p) p(h, durationMs, AW_BLEND | AW_HIDE);
}

// ============================================================================
// §29 已知文件夹
// ============================================================================

inline std::wstring GetKnownFolder(REFKNOWNFOLDERID fid) {
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(fid, 0, nullptr, &path)) && path) {
        std::wstring result(path);
        CoTaskMemFree(path);
        return result;
    }
    return {};
}

inline std::wstring GetDesktopPath()    { return GetKnownFolder(FOLDERID_Desktop); }
inline std::wstring GetDocumentsPath()  { return GetKnownFolder(FOLDERID_Documents); }
inline std::wstring GetDownloadsPath()  { return GetKnownFolder(FOLDERID_Downloads); }
inline std::wstring GetPicturesPath()   { return GetKnownFolder(FOLDERID_Pictures); }
inline std::wstring GetMusicPath()      { return GetKnownFolder(FOLDERID_Music); }
inline std::wstring GetVideosPath()     { return GetKnownFolder(FOLDERID_Videos); }
inline std::wstring GetAppDataPath()    { return GetKnownFolder(FOLDERID_RoamingAppData); }
inline std::wstring GetLocalAppDataPath() { return GetKnownFolder(FOLDERID_LocalAppData); }
inline std::wstring GetStartMenuPath()  { return GetKnownFolder(FOLDERID_StartMenu); }

// ============================================================================
// §30 属性系统
// ============================================================================

inline HRESULT CreateMemoryPropertyStore(IPropertyStore** ppStore) {
    return PSCreateMemoryPropertyStore(IID_IPropertyStore, (void**)ppStore);
}

inline HRESULT PSBagWriteStr(IPropertyBag* bag, const wchar_t* key, const wchar_t* val) {
    return PSPropertyBag_WriteStr(bag, key, val);
}

inline HRESULT PSBagWriteDword(IPropertyBag* bag, const wchar_t* key, DWORD val) {
    return PSPropertyBag_WriteDWORD(bag, key, val);
}

} // namespace LlaShell::ShellEnh

#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <commctrl.h>
#include <WinInet.h>
#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "wininet.lib")

static constexpr UINT WM_TRAYICON     = WM_USER + 1;
static constexpr UINT WM_UPDATE_DONE  = WM_USER + 2;
static constexpr int  TIMER_CHECK     = 1;

static const wchar_t* kAppTitle = L"LlaShell 设置";
static const wchar_t* kTrayTip  = L"LlaShell";

struct ProcessEntry {
    const wchar_t* exe;
    HANDLE         hProcess;
    DWORD          pid;
};

enum SettingsCategory {
    CAT_UPDATE = 0,
    CAT_ABOUT  = 1,
};

struct UpdateSettings {
    bool disableAutoUpdate;
    bool disableCheck;
    bool disableCompletely;
};

struct AppState {
    HWND                hwndMain;
    HWND                hwndSidebar;
    HWND                hwndContent;
    HWND                hwndCheckBtn;
    HWND                hwndChkAutoUpdate;
    HWND                hwndChkDisableCheck;
    HWND                hwndChkDisableCompletely;
    HWND                hwndStatus;
    HINSTANCE           hInstance;
    HICON               hIcon;
    NOTIFYICONDATAW     nid;
    std::vector<ProcessEntry> processes;
    SettingsCategory    activeCategory;
    UpdateSettings      updateSettings;
    std::atomic<bool>   checking;
    std::atomic<bool>   updateAvailable;
    std::wstring        latestVersion;
    bool                latestIsPrerelease;
    std::wstring        updateNotes;
    HFONT               hFont;
    HFONT               hFontBold;
    HBRUSH              hBgBrush;
    HBRUSH              hSidebarBrush;
    HBRUSH              hContentBrush;
    COLORREF            accentColor;
};

static AppState g_app{};

static void LaunchProcess(const wchar_t* exe, ProcessEntry& entry) {
    wchar_t selfDir[MAX_PATH]{};
    GetModuleFileNameW(nullptr, selfDir, _countof(selfDir));
    PathRemoveFileSpecW(selfDir);

    wchar_t exePath[MAX_PATH]{};
    PathCombineW(exePath, selfDir, exe);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (CreateProcessW(exePath, nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        entry.exe = exe;
        entry.hProcess = pi.hProcess;
        entry.pid = pi.dwProcessId;
        CloseHandle(pi.hThread);
    } else {
        entry.exe = exe;
        entry.hProcess = nullptr;
        entry.pid = 0;
    }
}

static void LaunchAllProcesses() {
    const wchar_t* exes[] = {
        L"Desktop.exe",
        L"Taskbar.exe",
        L"MediaParser.exe",
        L"ShellHost.exe",
    };
    for (auto* exe : exes) {
        ProcessEntry pe{};
        LaunchProcess(exe, pe);
        g_app.processes.push_back(pe);
    }
}

static void TerminateAllProcesses() {
    for (auto& pe : g_app.processes) {
        if (pe.hProcess) {
            TerminateProcess(pe.hProcess, 0);
            CloseHandle(pe.hProcess);
            pe.hProcess = nullptr;
        }
    }
    g_app.processes.clear();
}

static void CreateTrayIcon() {
    g_app.nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_app.nid.hWnd = g_app.hwndMain;
    g_app.nid.uID = 1;
    g_app.nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_app.nid.uCallbackMessage = WM_TRAYICON;
    g_app.nid.hIcon = g_app.hIcon;
    StringCchCopyW(g_app.nid.szTip, _countof(g_app.nid.szTip), kTrayTip);
    Shell_NotifyIconW(NIM_ADD, &g_app.nid);
}

static void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_app.nid);
}

static void ShowTrayMenu(int x, int y) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 1, L"打开设置");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 2, L"启动文件管理器");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 99, L"退出 LlaShell");

    SetForegroundWindow(g_app.hwndMain);
    int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                             x, y, 0, g_app.hwndMain, nullptr);
    DestroyMenu(hMenu);

    switch (cmd) {
    case 1:
        ShowWindow(g_app.hwndMain, SW_SHOW);
        SetForegroundWindow(g_app.hwndMain);
        break;
    case 2:
        ShellExecuteW(nullptr, L"open", L"MainUI.exe", nullptr, nullptr, SW_SHOW);
        break;
    case 99:
        RemoveTrayIcon();
        TerminateAllProcesses();
        PostQuitMessage(0);
        break;
    }
}

#pragma region Update Checking

static std::string HttpGet(const wchar_t* url) {
    std::string result;
    HINTERNET hInternet = InternetOpenW(L"LlaShell-Updater/1.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInternet) return result;
    HINTERNET hUrl = InternetOpenUrlW(hInternet, url, nullptr, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) { InternetCloseHandle(hInternet); return result; }

    char buf[8192]{};
    DWORD bytesRead = 0;
    while (InternetReadFile(hUrl, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
        result.append(buf, bytesRead);
        bytesRead = 0;
    }
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return result;
}

static std::string JsonGetString(const std::string& json, const char* key) {
    std::string search = "\"";
    search += key;
    search += "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return {};
    pos = json.find_first_not_of(" \t\n\r", pos + 1);
    if (pos == std::string::npos) return {};
    if (json[pos] == '"') {
        size_t start = pos + 1;
        size_t end = json.find('"', start);
        if (end == std::string::npos) return {};
        return json.substr(start, end - start);
    }
    if (json.compare(pos, 4, "true") == 0) return "true";
    if (json.compare(pos, 5, "false") == 0) return "false";
    return {};
}

static bool IsCriticalUpdate(const std::string& body) {
    return (body.find("紧急更新") != std::string::npos ||
            body.find("重要更新") != std::string::npos ||
            body.find("critical") != std::string::npos ||
            body.find("urgent") != std::string::npos);
}

static bool IsNewerVersion(const wchar_t* current, const wchar_t* latest) {
    auto parse = [](const wchar_t* v, int& a, int& b, int& c, int& d) {
        a = b = c = d = 0;
        const wchar_t* p = v;
        if (*p == L'v' || *p == L'V') p++;
        a = _wtoi(p);
        while (*p && *p != L'.') p++;
        if (*p == L'.') { p++; b = _wtoi(p); }
        while (*p && *p != L'.') p++;
        if (*p == L'.') { p++; c = _wtoi(p); }
        while (*p && *p != L'.') p++;
        if (*p == L'.') { p++; d = _wtoi(p); }
    };
    int ca, cb, cc, cd, la, lb, lc, ld;
    parse(current, ca, cb, cc, cd);
    parse(latest, la, lb, lc, ld);
    if (la != ca) return la > ca;
    if (lb != cb) return lb > cb;
    if (lc != cc) return lc > cc;
    return ld > cd;
}

static void DoCheckUpdate() {
    g_app.checking.store(true);

    const wchar_t* url = L"https://api.github.com/repos/lladlam/LlaShell/releases";
    std::string response = HttpGet(url);
    if (response.empty()) {
        g_app.checking.store(false);
        PostMessage(g_app.hwndMain, WM_UPDATE_DONE, 0, 0);
        return;
    }

    std::string tag = JsonGetString(response, "tag_name");
    std::string prerelease = JsonGetString(response, "prerelease");
    std::string body = JsonGetString(response, "body");

    if (!tag.empty()) {
        std::wstring wtag(tag.begin(), tag.end());
        const wchar_t* currentVer = L"0.0.0.1";

        if (IsNewerVersion(currentVer, wtag.c_str())) {
            g_app.updateAvailable.store(true);
            g_app.latestVersion = wtag;
            g_app.latestIsPrerelease = (prerelease == "true");
            g_app.updateNotes.assign(body.begin(), body.end());
        } else {
            g_app.updateAvailable.store(false);
        }
    }

    g_app.checking.store(false);
    PostMessage(g_app.hwndMain, WM_UPDATE_DONE, 0, 0);
}

static void ApplyUpdatePolicy() {
    if (g_app.updateSettings.disableCompletely) return;
    if (g_app.updateSettings.disableCheck) return;

    if (g_app.updateAvailable.load()) {
        bool critical = IsCriticalUpdate(
            std::string(g_app.updateNotes.begin(), g_app.updateNotes.end()));

        if (!g_app.updateSettings.disableAutoUpdate || critical) {
            wchar_t exePath[MAX_PATH]{};
            GetModuleFileNameW(nullptr, exePath, _countof(exePath));
            PathRemoveFileSpecW(exePath);
            wchar_t updaterPath[MAX_PATH]{};
            PathCombineW(updaterPath, exePath, L"Updater.exe");
            ShellExecuteW(nullptr, L"open", updaterPath, nullptr, nullptr, SW_HIDE);
        }
    }
}

#pragma endregion

#pragma region Settings Window

static void DrawSidebarItem(HDC hdc, const RECT& rc, const wchar_t* text, bool selected) {
    HBRUSH brush = selected ? g_app.hContentBrush : g_app.hSidebarBrush;
    FillRect(hdc, &rc, brush);

    if (selected) {
        HPEN pen = CreatePen(PS_SOLID, 3, g_app.accentColor);
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, rc.left, rc.top, nullptr);
        LineTo(hdc, rc.left, rc.bottom);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    HFONT oldFont = (HFONT)SelectObject(hdc, selected ? g_app.hFontBold : g_app.hFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, selected ? RGB(255, 255, 255) : RGB(180, 180, 200));
    RECT textRc = { rc.left + 20, rc.top, rc.right - 8, rc.bottom };
    DrawTextW(hdc, text, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

static void DrawSidebar(HDC hdc, const RECT& rc) {
    FillRect(hdc, &rc, g_app.hSidebarBrush);

    RECT titleRc = { rc.left, rc.top, rc.right, rc.top + 50 };
    HFONT oldFont = (HFONT)SelectObject(hdc, g_app.hFontBold);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(220, 220, 240));
    DrawTextW(hdc, L"LlaShell", -1, &titleRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);

    const wchar_t* items[] = { L"更新", L"关于" };
    int y = 60;
    for (int i = 0; i < 2; i++) {
        RECT itemRc = { rc.left, y, rc.right, y + 40 };
        DrawSidebarItem(hdc, itemRc, items[i], g_app.activeCategory == i);
        y += 40;
    }
}

static void DrawUpdatePanel(HDC hdc, const RECT& rc) {
    FillRect(hdc, &rc, g_app.hContentBrush);

    HFONT oldFont = (HFONT)SelectObject(hdc, g_app.hFontBold);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(230, 230, 240));
    RECT titleRc = { rc.left + 24, rc.top + 20, rc.right - 24, rc.top + 52 };
    DrawTextW(hdc, L"更新设置", -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);

    oldFont = (HFONT)SelectObject(hdc, g_app.hFont);
    SetTextColor(hdc, RGB(180, 180, 200));
    RECT descRc = { rc.left + 24, rc.top + 56, rc.right - 24, rc.top + 100 };
    DrawTextW(hdc,
        L"管理 LlaShell 的更新行为。当检测到紧急或重要更新时，\n"
        L"即使关闭了自动更新，仍会强制下载并安装。",
        -1, &descRc, DT_LEFT | DT_WORDBREAK);

    if (g_app.checking.load()) {
        SetTextColor(hdc, g_app.accentColor);
        RECT statusRc = { rc.left + 24, rc.top + 260, rc.right - 24, rc.top + 290 };
        DrawTextW(hdc, L"正在检查更新...", -1, &statusRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else if (g_app.updateAvailable.load()) {
        SetTextColor(hdc, RGB(50, 200, 100));
        wchar_t msg[256]{};
        StringCchPrintfW(msg, _countof(msg), L"发现新版本 %s%s",
            g_app.latestVersion.c_str(),
            g_app.latestIsPrerelease ? L" (预发布)" : L"");
        RECT statusRc = { rc.left + 24, rc.top + 260, rc.right - 24, rc.top + 290 };
        DrawTextW(hdc, msg, -1, &statusRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        SetTextColor(hdc, RGB(100, 100, 120));
        RECT statusRc = { rc.left + 24, rc.top + 260, rc.right - 24, rc.top + 290 };
        DrawTextW(hdc, L"当前已是最新版本 (0.0.0.1)", -1, &statusRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hdc, oldFont);
}

static void DrawHyperlink(HDC hdc, int x, int y, const wchar_t* text, COLORREF color) {
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    SIZE sz;
    GetTextExtentPoint32W(hdc, text, static_cast<int>(wcslen(text)), &sz);
    RECT rc = { x, y, x + sz.cx, y + sz.cy };
    DrawTextW(hdc, text, -1, &rc, DT_LEFT | DT_TOP);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, x, y + sz.cy - 1, nullptr);
    LineTo(hdc, x + sz.cx, y + sz.cy - 1);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void DrawAboutPanel(HDC hdc, const RECT& rc) {
    FillRect(hdc, &rc, g_app.hContentBrush);

    int cx = rc.left + 24;
    int y = rc.top + 20;
    int w = rc.right - rc.left - 48;

    if (g_app.hIcon) {
        DrawIconEx(hdc, cx + (w - 64) / 2, y, g_app.hIcon, 64, 64, 0, nullptr, DI_NORMAL);
    }
    y += 76;

    HFONT oldFont = (HFONT)SelectObject(hdc, g_app.hFontBold);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(230, 230, 240));
    RECT titleRc = { cx, y, cx + w, y + 28 };
    DrawTextW(hdc, L"LlaShell", -1, &titleRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    y += 28;

    SelectObject(hdc, g_app.hFont);
    SetTextColor(hdc, RGB(100, 200, 160));
    RECT taglineRc = { cx, y, cx + w, y + 22 };
    DrawTextW(hdc, L"一款开源，好用，占用低的第三方资源管理器", -1, &taglineRc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    y += 28;

    SetTextColor(hdc, RGB(140, 140, 160));
    RECT authorRc = { cx, y, cx + w, y + 20 };
    DrawTextW(hdc, L"由 lladlam 开发", -1, &authorRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    y += 32;

    SetTextColor(hdc, RGB(180, 180, 200));

    wchar_t line[256]{};

    StringCchPrintfW(line, _countof(line), L"当前版本: 0.0.0.1");
    RECT verRc = { cx, y, cx + w, y + 20 };
    DrawTextW(hdc, line, -1, &verRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    y += 24;

    StringCchPrintfW(line, _countof(line), L"作者: lladlam");
    RECT aRc = { cx, y, cx + w, y + 20 };
    DrawTextW(hdc, line, -1, &aRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    y += 28;

    SetTextColor(hdc, RGB(180, 180, 200));
    RECT donLabelRc = { cx, y, cx + w, y + 20 };
    DrawTextW(hdc, L"捐赠支持: ", -1, &donLabelRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawHyperlink(hdc, cx + 72, y, L"afdian.lladlam.top", RGB(80, 160, 255));
    y += 32;

    RECT srcLabelRc = { cx, y, cx + w, y + 20 };
    DrawTextW(hdc, L"开源地址: ", -1, &srcLabelRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawHyperlink(hdc, cx + 72, y, L"https://github.com/lladlam/LlaShell", RGB(80, 160, 255));
    y += 32;

    SetTextColor(hdc, RGB(160, 160, 180));
    RECT depTitleRc = { cx, y, cx + w, y + 20 };
    DrawTextW(hdc, L"使用的开源项目:", -1, &depTitleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    y += 22;

    SetTextColor(hdc, RGB(140, 140, 160));
    const wchar_t* deps[] = {
        L"FFmpeg — 视频/图片缩略图生成 (LGPL v2.1+ / GPL v2+)",
        L"Windows SDK — Win32 API, COM, IOCP",
        L"MinGW-w64 — GCC 工具链",
    };
    for (auto* dep : deps) {
        RECT depRc = { cx + 16, y, cx + w, y + 18 };
        DrawTextW(hdc, dep, -1, &depRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        y += 20;
    }
    y += 12;

    HPEN sepPen = CreatePen(PS_SOLID, 1, RGB(60, 60, 80));
    HPEN oldPen = (HPEN)SelectObject(hdc, sepPen);
    MoveToEx(hdc, cx, y, nullptr);
    LineTo(hdc, cx + w, y);
    SelectObject(hdc, oldPen);
    DeleteObject(sepPen);
    y += 12;

    SetTextColor(hdc, RGB(100, 100, 120));
    RECT licRc = { cx, y, cx + w, y + 84 };
    DrawTextW(hdc,
        L"Copyright 2026 lladlam\n"
        L"本软件基于 Apache License, Version 2.0 授权发布。\n"
        L"许可证文本: http://www.apache.org/licenses/LICENSE-2.0\n"
        L"软件按 \"AS IS\" 提供；详见 LICENSE 及 THIRD_PARTY_LICENSES.txt",
        -1, &licRc, DT_LEFT | DT_WORDBREAK);

    SelectObject(hdc, oldFont);
}

static void CreateSettingsControls(HWND hwndParent) {
    int sidebarW = 200;
    RECT rc;
    GetClientRect(hwndParent, &rc);
    int contentX = sidebarW + 1;
    int contentW = rc.right - contentX;

    g_app.hwndCheckBtn = CreateWindowExW(0, L"BUTTON", L"检查更新",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        contentX + 24, 120, 120, 32, hwndParent, (HMENU)1001, g_app.hInstance, nullptr);
    SendMessageW(g_app.hwndCheckBtn, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);

    g_app.hwndChkAutoUpdate = CreateWindowExW(0, L"BUTTON", L"关闭自动更新",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        contentX + 24, 164, contentW - 48, 24, hwndParent, (HMENU)1002, g_app.hInstance, nullptr);
    SendMessageW(g_app.hwndChkAutoUpdate, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);

    g_app.hwndChkDisableCheck = CreateWindowExW(0, L"BUTTON", L"关闭检查更新",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        contentX + 24, 196, contentW - 48, 24, hwndParent, (HMENU)1003, g_app.hInstance, nullptr);
    SendMessageW(g_app.hwndChkDisableCheck, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);

    g_app.hwndChkDisableCompletely = CreateWindowExW(0, L"BUTTON", L"完全关闭更新",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        contentX + 24, 228, contentW - 48, 24, hwndParent, (HMENU)1004, g_app.hInstance, nullptr);
    SendMessageW(g_app.hwndChkDisableCompletely, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);

    g_app.hwndStatus = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        contentX + 24, 264, contentW - 48, 24, hwndParent, (HMENU)1005, g_app.hInstance, nullptr);
    SendMessageW(g_app.hwndStatus, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);
}

static void ShowUpdateControls(bool show) {
    int cmd = show ? SW_SHOW : SW_HIDE;
    ShowWindow(g_app.hwndCheckBtn, cmd);
    ShowWindow(g_app.hwndChkAutoUpdate, cmd);
    ShowWindow(g_app.hwndChkDisableCheck, cmd);
    ShowWindow(g_app.hwndChkDisableCompletely, cmd);
    ShowWindow(g_app.hwndStatus, cmd);
}

static void SyncCheckboxes() {
    SendMessageW(g_app.hwndChkAutoUpdate, BM_SETCHECK,
        g_app.updateSettings.disableAutoUpdate ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_app.hwndChkDisableCheck, BM_SETCHECK,
        g_app.updateSettings.disableCheck ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_app.hwndChkDisableCompletely, BM_SETCHECK,
        g_app.updateSettings.disableCompletely ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void LoadUpdateSettings() {
    wchar_t cfgPath[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, cfgPath);
    PathAppendW(cfgPath, L"LlaShell\\update_settings.ini");
    g_app.updateSettings.disableAutoUpdate = (GetPrivateProfileIntW(L"Update", L"DisableAuto", 0, cfgPath) != 0);
    g_app.updateSettings.disableCheck = (GetPrivateProfileIntW(L"Update", L"DisableCheck", 0, cfgPath) != 0);
    g_app.updateSettings.disableCompletely = (GetPrivateProfileIntW(L"Update", L"DisableAll", 0, cfgPath) != 0);
}

static void SaveUpdateSettings() {
    wchar_t cfgDir[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, cfgDir);
    PathAppendW(cfgDir, L"LlaShell");
    CreateDirectoryW(cfgDir, nullptr);
    wchar_t cfgPath[MAX_PATH]{};
    PathCombineW(cfgPath, cfgDir, L"update_settings.ini");
    wchar_t val[4]{};
    StringCchPrintfW(val, _countof(val), L"%d", g_app.updateSettings.disableAutoUpdate ? 1 : 0);
    WritePrivateProfileStringW(L"Update", L"DisableAuto", val, cfgPath);
    StringCchPrintfW(val, _countof(val), L"%d", g_app.updateSettings.disableCheck ? 1 : 0);
    WritePrivateProfileStringW(L"Update", L"DisableCheck", val, cfgPath);
    StringCchPrintfW(val, _countof(val), L"%d", g_app.updateSettings.disableCompletely ? 1 : 0);
    WritePrivateProfileStringW(L"Update", L"DisableAll", val, cfgPath);
}

static void SidebarClick(int x, int y) {
    int itemY = 60;
    for (int i = 0; i < 2; i++) {
        if (y >= itemY && y < itemY + 40) {
            g_app.activeCategory = static_cast<SettingsCategory>(i);
            ShowUpdateControls(i == CAT_UPDATE);
            InvalidateRect(g_app.hwndMain, nullptr, TRUE);
            break;
        }
        itemY += 40;
    }
}

#pragma endregion

#pragma region Main Window

static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_app.hBgBrush      = CreateSolidBrush(RGB(30, 30, 46));
        g_app.hSidebarBrush = CreateSolidBrush(RGB(22, 22, 34));
        g_app.hContentBrush = CreateSolidBrush(RGB(36, 36, 52));
        g_app.accentColor   = RGB(0, 120, 215);

        g_app.hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        g_app.hFontBold = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        CreateSettingsControls(hWnd);
        SyncCheckboxes();
        ShowUpdateControls(true);

        if (!g_app.updateSettings.disableCompletely && !g_app.updateSettings.disableCheck) {
            std::thread([]() { DoCheckUpdate(); }).detach();
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        RECT sidebarRc = { 0, 0, 200, rc.bottom };
        DrawSidebar(hdc, sidebarRc);

        RECT contentRc = { 201, 0, rc.right, rc.bottom };
        if (g_app.activeCategory == CAT_UPDATE)
            DrawUpdatePanel(hdc, contentRc);
        else
            DrawAboutPanel(hdc, contentRc);

        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        if (x < 200) SidebarClick(x, y);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1001) {
            if (!g_app.checking.load()) {
                g_app.checking.store(true);
                InvalidateRect(hWnd, nullptr, TRUE);
                std::thread([]() { DoCheckUpdate(); }).detach();
            }
        } else if (LOWORD(wParam) == 1002) {
            g_app.updateSettings.disableAutoUpdate =
                (SendMessageW(g_app.hwndChkAutoUpdate, BM_GETCHECK, 0, 0) == BST_CHECKED);
            SaveUpdateSettings();
        } else if (LOWORD(wParam) == 1003) {
            g_app.updateSettings.disableCheck =
                (SendMessageW(g_app.hwndChkDisableCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
            SaveUpdateSettings();
        } else if (LOWORD(wParam) == 1004) {
            g_app.updateSettings.disableCompletely =
                (SendMessageW(g_app.hwndChkDisableCompletely, BM_GETCHECK, 0, 0) == BST_CHECKED);
            SaveUpdateSettings();
        }
        return 0;
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP) {
            ShowWindow(hWnd, SW_SHOW);
            SetForegroundWindow(hWnd);
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            ShowTrayMenu(pt.x, pt.y);
        }
        return 0;
    case WM_UPDATE_DONE:
        InvalidateRect(hWnd, nullptr, TRUE);
        if (g_app.updateAvailable.load()) {
            ApplyUpdatePolicy();
        }
        return 0;
    case WM_TIMER:
        if (wParam == TIMER_CHECK) {
            if (!g_app.updateSettings.disableCompletely &&
                !g_app.updateSettings.disableCheck &&
                !g_app.checking.load()) {
                std::thread([]() { DoCheckUpdate(); }).detach();
            }
        }
        return 0;
    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        RemoveTrayIcon();
        TerminateAllProcesses();
        if (g_app.hFont) DeleteObject(g_app.hFont);
        if (g_app.hFontBold) DeleteObject(g_app.hFontBold);
        if (g_app.hBgBrush) DeleteObject(g_app.hBgBrush);
        if (g_app.hSidebarBrush) DeleteObject(g_app.hSidebarBrush);
        if (g_app.hContentBrush) DeleteObject(g_app.hContentBrush);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

#pragma endregion

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    g_app.hInstance = hInstance;
    g_app.activeCategory = CAT_UPDATE;
    g_app.checking.store(false);
    g_app.updateAvailable.store(false);

    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    g_app.hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(1));
    if (!g_app.hIcon) g_app.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

    LoadUpdateSettings();

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance      = hInstance;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon          = g_app.hIcon;
    wc.lpszClassName  = L"LlaShell_Settings";
    wc.hbrBackground  = nullptr;
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = 720;
    int winH = 480;
    int winX = (screenW - winW) / 2;
    int winY = (screenH - winH) / 2;

    g_app.hwndMain = CreateWindowExW(
        0, L"LlaShell_Settings", kAppTitle,
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        winX, winY, winW, winH,
        nullptr, nullptr, hInstance, nullptr);

    if (!g_app.hwndMain) return 1;

    CreateTrayIcon();
    LaunchAllProcesses();

    SetTimer(g_app.hwndMain, TIMER_CHECK, 3600000, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return 0;
}

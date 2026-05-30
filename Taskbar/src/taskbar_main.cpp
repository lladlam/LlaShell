#include "taskbar.h"

namespace LlaShell {

TaskbarWindow::TaskbarWindow()
    : m_taskbarHeight(kDefaultTaskbarHeight)
    , m_buttonWidth(kDefaultButtonWidth)
    , m_buttonHeight(kDefaultButtonHeight)
    , m_dpi(96)
    , m_hwnd(nullptr)
    , m_hInstance(nullptr)
    , m_running(false)
    , m_isHidden(false)
    , m_font(nullptr)
    , m_fontSmall(nullptr)
    , m_fontBold(nullptr)
    , m_bgBrush(nullptr)
    , m_btnBrush(nullptr)
    , m_hoverBrush(nullptr)
    , m_activeBrush(nullptr)
    , m_borderPen(nullptr)
    , m_activeBarPen(nullptr)
    , m_accentColor(RGB(0, 120, 215))
    , m_hoverIndex(-1)
    , m_activeIndex(-1)
    , m_ipcReady(false)
{
    m_clockText[0] = L'\0';
    m_dateText[0] = L'\0';
}

TaskbarWindow::~TaskbarWindow() {
    Shutdown();
}

void TaskbarWindow::InitDPI() {
    HDC hdc = GetDC(nullptr);
    m_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(nullptr, hdc);
    if (m_dpi <= 0) m_dpi = 96;
    m_taskbarHeight = Scale(kDefaultTaskbarHeight);
    m_buttonWidth   = Scale(kDefaultButtonWidth);
    m_buttonHeight  = Scale(kDefaultButtonHeight);
}

int TaskbarWindow::Scale(int value) const {
    return MulDiv(value, m_dpi, 96);
}

bool TaskbarWindow::Create(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    InitDPI();

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName  = L"LlaShell_Taskbar";
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"LlaShell_Taskbar",
        L"LlaShell Taskbar",
        WS_POPUP,
        0, screenH - m_taskbarHeight, screenW, m_taskbarHeight,
        nullptr, nullptr, hInstance, this
    );
    if (!m_hwnd) return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    m_bgBrush      = CreateSolidBrush(RGB(24, 24, 36));
    m_btnBrush     = CreateSolidBrush(RGB(32, 32, 48));
    m_hoverBrush   = CreateSolidBrush(RGB(48, 48, 72));
    m_activeBrush  = CreateSolidBrush(RGB(50, 50, 80));
    m_borderPen    = CreatePen(PS_SOLID, 1, RGB(55, 55, 75));
    m_activeBarPen = CreatePen(PS_SOLID, Scale(3), m_accentColor);

    int fontSize = Scale(12);
    m_font      = CreateFontW(-fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    m_fontSmall = CreateFontW(-MulDiv(fontSize, 8, 10), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    m_fontBold  = CreateFontW(-fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    RegisterGlobalHotkeys();
    LoadPinnedApps();

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_USER + 1;
    nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    StringCchCopyW(nid.szTip, _countof(nid.szTip), L"LlaShell");
    Shell_NotifyIconW(NIM_ADD, &nid);

    m_running = true;
    m_fsDetectThread = std::thread(&TaskbarWindow::FullscreenDetectThread, this);
    m_windowMonitorThread = std::thread(&TaskbarWindow::WindowMonitorThread, this);

    InitIPC();
    EnumerateWindows();
    return true;
}

void TaskbarWindow::Show() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(m_hwnd);
        SetTimer(m_hwnd, 1, 1000, nullptr);
    }
}

void TaskbarWindow::Shutdown() {
    ShutdownIPC();
    m_running = false;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);

    UnregisterHotKey(m_hwnd, 1);
    UnregisterHotKey(m_hwnd, 2);
    if (m_fsDetectThread.joinable()) m_fsDetectThread.join();
    if (m_windowMonitorThread.joinable()) m_windowMonitorThread.join();
    SavePinnedApps();

    if (m_font) DeleteObject(m_font);
    if (m_fontSmall) DeleteObject(m_fontSmall);
    if (m_fontBold) DeleteObject(m_fontBold);
    if (m_bgBrush) DeleteObject(m_bgBrush);
    if (m_btnBrush) DeleteObject(m_btnBrush);
    if (m_hoverBrush) DeleteObject(m_hoverBrush);
    if (m_activeBrush) DeleteObject(m_activeBrush);
    if (m_borderPen) DeleteObject(m_borderPen);
    if (m_activeBarPen) DeleteObject(m_activeBarPen);
    for (auto& p : m_pinnedApps) { if (p.icon) DestroyIcon(p.icon); }
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    UnregisterClassW(L"LlaShell_Taskbar", m_hInstance);
}

void TaskbarWindow::RegisterGlobalHotkeys() {
    RegisterHotKey(m_hwnd, 1, MOD_WIN, 0x45);
    RegisterHotKey(m_hwnd, 2, MOD_WIN, 0x44);
}

#pragma region Window Enumeration

BOOL CALLBACK TaskbarWindow::EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* self = reinterpret_cast<TaskbarWindow*>(lParam);
    if (!self->IsTaskbarWindow(hwnd)) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    wchar_t title[256]{};
    GetWindowTextW(hwnd, title, _countof(title));
    if (title[0] == L'\0') return TRUE;

    wchar_t className[64]{};
    GetClassNameW(hwnd, className, _countof(className));
    if (wcscmp(className, L"LlaShell_Taskbar") == 0) return TRUE;
    if (wcscmp(className, L"LlaShell_Desktop") == 0) return TRUE;

    HICON hIcon = reinterpret_cast<HICON>(SendMessageW(hwnd, WM_GETICON, ICON_SMALL2, 0));
    if (!hIcon) hIcon = reinterpret_cast<HICON>(SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0));
    if (!hIcon) hIcon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICONSM));
    if (!hIcon) hIcon = LoadIcon(nullptr, IDI_APPLICATION);

    std::lock_guard<std::mutex> lock(self->m_btnMutex);
    bool found = false;
    for (auto& btn : self->m_buttons) {
        if (btn.hwnd == hwnd) {
            btn.title = title;
            if (hIcon && hIcon != btn.hIcon) btn.hIcon = hIcon;
            btn.pid = pid;
            found = true;
            break;
        }
    }
    if (!found) {
        TaskButton btn{};
        btn.hwnd = hwnd; btn.pid = pid; btn.title = title;
        btn.hIcon = hIcon; btn.active = false; btn.pinned = false; btn.hovered = false;
        self->m_buttons.push_back(btn);
    }
    return TRUE;
}

bool TaskbarWindow::IsTaskbarWindow(HWND hwnd) const {
    if (!IsWindowVisible(hwnd)) return false;
    LONG exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) return false;
    LONG style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (!(style & WS_CAPTION)) return false;
    return true;
}

void TaskbarWindow::EnumerateWindows() {
    {
        std::lock_guard<std::mutex> lock(m_btnMutex);
        for (auto it = m_buttons.begin(); it != m_buttons.end(); ) {
            if (!it->pinned && !IsWindow(it->hwnd)) it = m_buttons.erase(it);
            else ++it;
        }
    }
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(this));
    RefreshTaskButtons();
}

void TaskbarWindow::RefreshTaskButtons() {
    std::lock_guard<std::mutex> lock(m_btnMutex);
    HWND fg = GetForegroundWindow();
    for (auto& btn : m_buttons) btn.active = (btn.hwnd == fg);
}

void TaskbarWindow::WindowMonitorThread() {
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        if (m_running) EnumerateWindows();
    }
}

#pragma endregion

#pragma region Drawing

void TaskbarWindow::DrawTaskbar(HDC hdc, const RECT& rc) {
    FillRect(hdc, &rc, m_bgBrush);
    int x = 0;

    RECT startRc = { x, 0, x + Scale(kStartBtnWidth), m_taskbarHeight };
    DrawStartButton(hdc, startRc);
    x += Scale(kStartBtnWidth);

    RECT searchRc = { x, 0, x + Scale(kSearchBtnWidth), m_taskbarHeight };
    DrawSearchButton(hdc, searchRc);
    x += Scale(kSearchBtnWidth);

    int trayLeft = rc.right - Scale(kTrayAreaMinWidth);
    RECT btnRc = { x, 0, trayLeft, m_taskbarHeight };
    DrawTaskButtons(hdc, btnRc);

    RECT trayRc = { trayLeft, 0, rc.right - Scale(kClockWidth) - Scale(kNotifBtnWidth), m_taskbarHeight };
    DrawTrayArea(hdc, trayRc);

    RECT clockRc = { rc.right - Scale(kClockWidth) - Scale(kNotifBtnWidth), 0,
                     rc.right - Scale(kNotifBtnWidth), m_taskbarHeight };
    DrawClock(hdc, clockRc);

    RECT notifRc = { rc.right - Scale(kNotifBtnWidth), 0, rc.right, m_taskbarHeight };
    DrawNotificationButton(hdc, notifRc);
}

void TaskbarWindow::DrawStartButton(HDC hdc, const RECT& rc) {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(m_hwnd, &pt);
    bool hovered = PtInRect(&rc, pt);

    FillRect(hdc, &rc, hovered ? m_hoverBrush : m_btnBrush);

    int cx = rc.left + (rc.right - rc.left) / 2;
    int cy = rc.top + (rc.bottom - rc.top) / 2;
    int s = Scale(8);

    HPEN pen = CreatePen(PS_SOLID, Scale(2), RGB(200, 200, 220));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    Rectangle(hdc, cx - s, cy - s, cx, cy);
    Rectangle(hdc, cx, cy - s, cx + s, cy);
    Rectangle(hdc, cx - s, cy, cx, cy + s);
    Rectangle(hdc, cx, cy, cx + s, cy + s);

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
}

void TaskbarWindow::DrawSearchButton(HDC hdc, const RECT& rc) {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(m_hwnd, &pt);
    FillRect(hdc, &rc, PtInRect(&rc, pt) ? m_hoverBrush : m_btnBrush);

    int cx = rc.left + (rc.right - rc.left) / 2;
    int cy = rc.top + (rc.bottom - rc.top) / 2;
    int r = Scale(5);

    HPEN pen = CreatePen(PS_SOLID, Scale(2), RGB(180, 180, 200));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
    MoveToEx(hdc, cx + r - 1, cy + r - 1, nullptr);
    LineTo(hdc, cx + r + Scale(3), cy + r + Scale(3));
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
}

void TaskbarWindow::DrawTaskButtons(HDC hdc, const RECT& rc) {
    std::lock_guard<std::mutex> lock(m_btnMutex);
    int x = rc.left + Scale(2);

    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(m_hwnd, &pt);

    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);
    SetBkMode(hdc, TRANSPARENT);

    for (auto& btn : m_buttons) {
        int bw = m_buttonWidth;
        RECT btnRc = { x, Scale(3), x + bw, m_taskbarHeight - Scale(3) };
        btn.rect = btnRc;
        bool hovered = PtInRect(&btnRc, pt);

        if (btn.active) {
            DrawActiveIndicator(hdc, btnRc, m_accentColor);
            FillRect(hdc, &btnRc, m_activeBrush);
        } else if (hovered) {
            FillRect(hdc, &btnRc, m_hoverBrush);
        }

        int iconX = btnRc.left + Scale(8);
        int iconY = btnRc.top + (m_buttonHeight - Scale(kButtonIconSize)) / 2;
        if (btn.hIcon)
            DrawIconEx(hdc, iconX, iconY, btn.hIcon, Scale(kButtonIconSize), Scale(kButtonIconSize), 0, nullptr, DI_NORMAL);

        SetTextColor(hdc, btn.active ? RGB(255, 255, 255) : RGB(200, 200, 220));
        RECT textRc = { iconX + Scale(kButtonIconSize) + Scale(kIconTextGap), btnRc.top, btnRc.right - Scale(4), btnRc.bottom };
        DrawTextW(hdc, btn.title.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        x += bw + Scale(kButtonPad);
        if (x > rc.right) break;
    }
    SelectObject(hdc, oldFont);
}

void TaskbarWindow::DrawActiveIndicator(HDC hdc, const RECT& btnRc, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, Scale(kActiveBarHeight), color);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, btnRc.left + Scale(4), btnRc.bottom - 1, nullptr);
    LineTo(hdc, btnRc.right - Scale(4), btnRc.bottom - 1);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void TaskbarWindow::DrawTrayArea(HDC hdc, const RECT& rc) {
    FillRect(hdc, &rc, m_bgBrush);

    HPEN oldPen = (HPEN)SelectObject(hdc, m_borderPen);
    MoveToEx(hdc, rc.left, rc.top + Scale(6), nullptr);
    LineTo(hdc, rc.left, rc.bottom - Scale(6));
    SelectObject(hdc, oldPen);

    int x = rc.left + Scale(8);

    wchar_t imeName[64]{};
    HKL hkl = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), nullptr));
    UINT langId = LOWORD(reinterpret_cast<UINT_PTR>(hkl));
    GetLocaleInfoW(langId, LOCALE_SABBREVLANGNAME, imeName, _countof(imeName));
    if (imeName[0] == L'\0') StringCchCopyW(imeName, _countof(imeName), L"EN");

    HFONT oldFont = (HFONT)SelectObject(hdc, m_fontSmall);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(180, 180, 200));
    RECT langRc = { x, 0, x + Scale(30), m_taskbarHeight };
    DrawTextW(hdc, imeName, -1, &langRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    x += Scale(36);

    SetTextColor(hdc, RGB(160, 160, 180));
    RECT volRc = { x, 0, x + Scale(20), m_taskbarHeight };
    DrawTextW(hdc, L"\xE995", -1, &volRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    x += Scale(24);

    RECT netRc = { x, 0, x + Scale(20), m_taskbarHeight };
    DrawTextW(hdc, L"\xE701", -1, &netRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldFont);
}

void TaskbarWindow::DrawClock(HDC hdc, const RECT& rc) {
    FillRect(hdc, &rc, m_bgBrush);

    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t timeBuf[16], dateBuf[16];
    StringCchPrintfW(timeBuf, _countof(timeBuf), L"%02d:%02d", st.wHour, st.wMinute);
    StringCchPrintfW(dateBuf, _countof(dateBuf), L"%d/%d/%d", st.wYear, st.wMonth, st.wDay);

    SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);

    SIZE timeSz, dateSz;
    GetTextExtentPoint32W(hdc, timeBuf, static_cast<int>(wcslen(timeBuf)), &timeSz);
    GetTextExtentPoint32W(hdc, dateBuf, static_cast<int>(wcslen(dateBuf)), &dateSz);

    int totalH = timeSz.cy + 2 + dateSz.cy;
    int startY = (m_taskbarHeight - totalH) / 2;

    int textX = rc.left + (rc.right - rc.left - timeSz.cx) / 2;

    SetTextColor(hdc, RGB(220, 220, 230));
    RECT timeRc = { textX, startY, textX + timeSz.cx, startY + timeSz.cy };
    DrawTextW(hdc, timeBuf, -1, &timeRc, DT_LEFT | DT_TOP);

    HFONT oldFont2 = (HFONT)SelectObject(hdc, m_fontSmall);
    GetTextExtentPoint32W(hdc, dateBuf, static_cast<int>(wcslen(dateBuf)), &dateSz);
    int dateX = rc.left + (rc.right - rc.left - dateSz.cx) / 2;
    SetTextColor(hdc, RGB(160, 160, 180));
    RECT dateRc = { dateX, startY + timeSz.cy + 2, dateX + dateSz.cx, startY + timeSz.cy + 2 + dateSz.cy };
    DrawTextW(hdc, dateBuf, -1, &dateRc, DT_LEFT | DT_TOP);

    SelectObject(hdc, oldFont);
    SelectObject(hdc, oldFont2);
}

void TaskbarWindow::DrawNotificationButton(HDC hdc, const RECT& rc) {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(m_hwnd, &pt);
    FillRect(hdc, &rc, PtInRect(&rc, pt) ? m_hoverBrush : m_btnBrush);

    HPEN oldPen = (HPEN)SelectObject(hdc, m_borderPen);
    MoveToEx(hdc, rc.left, rc.top + Scale(6), nullptr);
    LineTo(hdc, rc.left, rc.bottom - Scale(6));
    SelectObject(hdc, oldPen);

    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(200, 200, 220));
    DrawTextW(hdc, L"\xE7E7", -1, const_cast<RECT*>(&rc), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

#pragma endregion

#pragma region Input Handling

void TaskbarWindow::HandleClick(int x, int y) {
    int startEnd = Scale(kStartBtnWidth) + Scale(kSearchBtnWidth);
    if (x < Scale(kStartBtnWidth)) {
        POINT pt = { x, y };
        ClientToScreen(m_hwnd, &pt);
        ShowStartMenu(pt.x, pt.y);
        return;
    }
    if (x < startEnd) return;

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    if (x >= rc.right - Scale(kTrayAreaMinWidth)) return;

    std::lock_guard<std::mutex> lock(m_btnMutex);
    for (auto& btn : m_buttons) {
        if (PtInRect(&btn.rect, { x, y })) {
            HWND target = btn.hwnd;
            if (GetForegroundWindow() == target) {
                ShowWindow(target, SW_MINIMIZE);
            } else {
                if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
                SetForegroundWindow(target);
            }
            break;
        }
    }
}

void TaskbarWindow::HandleMiddleClick(int x, int y) {
    std::lock_guard<std::mutex> lock(m_btnMutex);
    for (auto& btn : m_buttons) {
        if (PtInRect(&btn.rect, { x, y })) {
            PostMessage(btn.hwnd, WM_CLOSE, 0, 0);
            break;
        }
    }
}

void TaskbarWindow::HandleRightClick(int x, int y) {
    if (x < Scale(kStartBtnWidth)) {
        POINT pt = { x, y };
        ClientToScreen(m_hwnd, &pt);
        ShowTaskbarContextMenu(pt.x, pt.y);
        return;
    }

    std::lock_guard<std::mutex> lock(m_btnMutex);
    for (size_t i = 0; i < m_buttons.size(); i++) {
        if (PtInRect(&m_buttons[i].rect, { x, y })) {
            POINT pt = { x, y };
            ClientToScreen(m_hwnd, &pt);
            HandleTaskButtonRightClick(static_cast<int>(i), pt.x, pt.y);
            return;
        }
    }

    POINT pt = { x, y };
    ClientToScreen(m_hwnd, &pt);
    ShowTaskbarContextMenu(pt.x, pt.y);
}

void TaskbarWindow::HandleTaskButtonRightClick(int index, int screenX, int screenY) {
    std::lock_guard<std::mutex> lock(m_btnMutex);
    if (index < 0 || index >= static_cast<int>(m_buttons.size())) return;

    auto& btn = m_buttons[index];
    std::wstring exePath = GetWindowExePath(btn.hwnd);
    bool pinned = IsPinned(exePath.c_str());

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 1, L"关闭窗口");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    if (pinned)
        AppendMenuW(hMenu, MF_STRING, 3, L"从任务栏取消固定");
    else
        AppendMenuW(hMenu, MF_STRING, 2, L"固定到任务栏");

    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, screenX, screenY, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);

    switch (cmd) {
    case 1: PostMessage(btn.hwnd, WM_CLOSE, 0, 0); break;
    case 2: if (!exePath.empty()) AddPinnedApp(exePath.c_str(), btn.title.c_str()); break;
    case 3: if (!exePath.empty()) RemovePinnedApp(exePath.c_str()); break;
    }
}

void TaskbarWindow::ShowStartMenu(int screenX, int screenY) {
    int menuW = Scale(300);
    int menuH = Scale(420);
    int x = 0;
    int y = screenY - menuH;

    HWND hMenuWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"LlaShell_Taskbar", L"",
        WS_POPUP,
        x, y, menuW, menuH,
        m_hwnd, nullptr, m_hInstance, nullptr);

    if (!hMenuWnd) return;

    SetWindowLongPtrW(hMenuWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    struct MenuItem { const wchar_t* label; int id; };
    MenuItem items[] = {
        { L"  \xE710  开始",            0 },
        { L"  \xE8B7  文件管理器",      1 },
        { L"  \xE756  命令提示符",      2 },
        { L"  \xE7C4  任务管理器",      3 },
        { L"",                           -1 },
        { L"  \xE713  设置",            4 },
        { L"  \xE946  关于 LlaShell",   5 },
        { L"",                           -1 },
        { L"  \xE7E8  退出 LlaShell",   99 },
    };

    int itemH = Scale(40);
    int iy = 0;

    HBRUSH menuBg = CreateSolidBrush(RGB(30, 30, 46));
    HBRUSH menuHover = CreateSolidBrush(RGB(50, 50, 80));
    HPEN menuBorder = CreatePen(PS_SOLID, 1, RGB(60, 60, 80));

    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hMenuWnd, &ps);
    RECT menuRc;
    GetClientRect(hMenuWnd, &menuRc);
    FillRect(hdc, &menuRc, menuBg);

    HPEN oldPen = (HPEN)SelectObject(hdc, menuBorder);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, menuRc.left, menuRc.top, menuRc.right, menuRc.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);

    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);
    SetBkMode(hdc, TRANSPARENT);

    for (auto& item : items) {
        if (item.id == -1) {
            iy += Scale(8);
            HPEN sepPen = CreatePen(PS_SOLID, 1, RGB(55, 55, 75));
            HPEN oldSepPen = (HPEN)SelectObject(hdc, sepPen);
            MoveToEx(hdc, Scale(12), iy, nullptr);
            LineTo(hdc, menuW - Scale(12), iy);
            SelectObject(hdc, oldSepPen);
            DeleteObject(sepPen);
            iy += Scale(8);
            continue;
        }

        RECT itemRc = { 0, iy, menuW, iy + itemH };
        FillRect(hdc, &itemRc, menuBg);

        if (item.id == 0) {
            SelectObject(hdc, m_fontBold);
            SetTextColor(hdc, RGB(220, 220, 240));
        } else {
            SelectObject(hdc, m_font);
            SetTextColor(hdc, RGB(190, 190, 210));
        }

        RECT textRc = { Scale(16), iy, menuW - Scale(16), iy + itemH };
        DrawTextW(hdc, item.label, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        iy += itemH;
    }
    SelectObject(hdc, oldFont);
    EndPaint(hMenuWnd, &ps);

    ShowWindow(hMenuWnd, SW_SHOWNOACTIVATE);
    SetForegroundWindow(hMenuWnd);

    MSG msg;
    bool closed = false;
    while (!closed && GetMessageW(&msg, nullptr, 0, 0)) {
        switch (msg.message) {
        case WM_LBUTTONUP: {
            int clickY = HIWORD(msg.lParam);
            int idx = clickY / itemH;
            int cmdId = -1;
            int curY = 0;
            for (auto& item : items) {
                if (item.id == -1) { curY += Scale(16); continue; }
                if (clickY >= curY && clickY < curY + itemH) { cmdId = item.id; break; }
                curY += itemH;
            }
            if (cmdId > 0) {
                switch (cmdId) {
                case 1: ShellExecuteW(m_hwnd, L"open", L"MainUI.exe", nullptr, nullptr, SW_SHOW); break;
                case 2: ShellExecuteW(m_hwnd, L"open", L"cmd.exe", nullptr, nullptr, SW_SHOW); break;
                case 3: ShellExecuteW(m_hwnd, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOW); break;
                case 4: break;
                case 5: break;
                case 99:
                    DestroyWindow(hMenuWnd);
                    PostQuitMessage(0);
                    return;
                }
            }
            closed = true;
            break;
        }
        case WM_KILLFOCUS:
            closed = true;
            break;
        case WM_KEYDOWN:
            if (msg.wParam == VK_ESCAPE) closed = true;
            break;
        default:
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            break;
        }
    }

    DestroyWindow(hMenuWnd);
    DeleteObject(menuBg);
    DeleteObject(menuHover);
    DeleteObject(menuBorder);
}

void TaskbarWindow::ShowTaskbarContextMenu(int screenX, int screenY) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 1, L"任务管理器");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 2, L"显示桌面");

    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, screenX, screenY, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);

    switch (cmd) {
    case 1: ShellExecuteW(m_hwnd, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOW); break;
    case 2: { HWND fg = GetForegroundWindow(); if (fg != m_hwnd) ShowWindow(fg, SW_MINIMIZE); break; }
    }
}

#pragma endregion

#pragma region Pinned Apps

std::wstring TaskbarWindow::GetWindowExePath(HWND hwnd) const {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return {};
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return {};
    wchar_t path[MAX_PATH]{}; DWORD size = MAX_PATH;
    QueryFullProcessImageNameW(hProc, 0, path, &size);
    CloseHandle(hProc);
    return path;
}

bool TaskbarWindow::IsPinned(const wchar_t* exePath) const {
    if (!exePath || !exePath[0]) return false;
    std::lock_guard<std::mutex> lock(m_pinnedMutex);
    for (const auto& p : m_pinnedApps)
        if (_wcsicmp(p.exePath.c_str(), exePath) == 0) return true;
    return false;
}

void TaskbarWindow::AddPinnedApp(const wchar_t* exePath, const wchar_t* title) {
    if (!exePath || !exePath[0]) return;
    std::lock_guard<std::mutex> lock(m_pinnedMutex);
    for (const auto& p : m_pinnedApps)
        if (_wcsicmp(p.exePath.c_str(), exePath) == 0) return;

    PinnedApp app{};
    app.exePath = exePath;
    app.title = title ? title : PathFindFileNameW(exePath);
    SHFILEINFOW sfi{};
    if (SHGetFileInfoW(exePath, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON))
        app.icon = sfi.hIcon;
    m_pinnedApps.push_back(app);
    SavePinnedApps();
}

void TaskbarWindow::RemovePinnedApp(const wchar_t* exePath) {
    std::lock_guard<std::mutex> lock(m_pinnedMutex);
    for (auto it = m_pinnedApps.begin(); it != m_pinnedApps.end(); ++it) {
        if (_wcsicmp(it->exePath.c_str(), exePath) == 0) {
            if (it->icon) DestroyIcon(it->icon);
            m_pinnedApps.erase(it);
            break;
        }
    }
    SavePinnedApps();
}

void TaskbarWindow::LoadPinnedApps() {
    wchar_t cfgPath[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, cfgPath);
    PathAppendW(cfgPath, L"LlaShell\\pinned.txt");
    HANDLE hFile = CreateFileW(cfgPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;
    char buf[4096]{}; DWORD bytesRead = 0;
    ReadFile(hFile, buf, sizeof(buf) - 1, &bytesRead, nullptr);
    CloseHandle(hFile);

    std::lock_guard<std::mutex> lock(m_pinnedMutex);
    std::string content(buf, bytesRead);
    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find('\n', pos);
        if (eol == std::string::npos) eol = content.size();
        std::string line = content.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = eol + 1;
        if (line.empty() || line[0] == '#') continue;
        std::wstring wline(line.begin(), line.end());
        PinnedApp app{};
        app.exePath = wline;
        app.title = PathFindFileNameW(wline.c_str());
        SHFILEINFOW sfi{};
        if (SHGetFileInfoW(wline.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON))
            app.icon = sfi.hIcon;
        m_pinnedApps.push_back(app);
    }
}

void TaskbarWindow::SavePinnedApps() {
    wchar_t cfgDir[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, cfgDir);
    PathAppendW(cfgDir, L"LlaShell");
    CreateDirectoryW(cfgDir, nullptr);
    wchar_t cfgPath[MAX_PATH]{};
    PathCombineW(cfgPath, cfgDir, L"pinned.txt");
    HANDLE hFile = CreateFileW(cfgPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;
    const char header[] = "# LlaShell pinned applications\r\n";
    DWORD written = 0;
    WriteFile(hFile, header, static_cast<DWORD>(strlen(header)), &written, nullptr);
    for (const auto& app : m_pinnedApps) {
        std::string line;
        for (wchar_t c : app.exePath) line += static_cast<char>(c);
        line += "\r\n";
        WriteFile(hFile, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
    }
    CloseHandle(hFile);
}

#pragma endregion

#pragma region Threads

void TaskbarWindow::FullscreenDetectThread() {
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        HWND fg = GetForegroundWindow();
        if (!fg || fg == m_hwnd) continue;
        RECT appRc, screenRc;
        GetWindowRect(fg, &appRc);
        screenRc = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        bool isFullscreen = (appRc.left <= 0 && appRc.top <= 0 && appRc.right >= screenRc.right && appRc.bottom >= screenRc.bottom);
        wchar_t cls[64];
        GetClassNameW(fg, cls, _countof(cls));
        if (wcscmp(cls, L"LlaShell_Taskbar") == 0 || wcscmp(cls, L"LlaShell_Desktop") == 0)
            isFullscreen = false;
        if (isFullscreen && !m_isHidden) {
            m_isHidden = true;
            PostMessage(m_hwnd, WM_USER + 2, 1, 0);
        } else if (!isFullscreen && m_isHidden) {
            m_isHidden = false;
            PostMessage(m_hwnd, WM_USER + 2, 0, 0);
        }
    }
}

#pragma endregion

#pragma region IPC

void TaskbarWindow::InitIPC() {
    IPC::IPCConfig cfg = IPC::DefaultConfig();
    cfg.processName = L"Taskbar";
    cfg.onMessage = OnIPCMessage;
    cfg.onConnected = OnPeerConnected;
    cfg.onDisconnected = OnPeerDisconnected;
    cfg.userData = this;
    if (!IsSuccess(IPC_Initialize(&cfg))) return;
    if (!IsSuccess(IPC_StartServer(L"Taskbar"))) { IPC_Shutdown(); return; }
    m_ipcReady.store(true, std::memory_order_relaxed);
    IPC::SessionId sid = IPC::kInvalidSession;
    IPC_Connect(L"Desktop", &sid);
    IPC_Connect(L"MainUI", &sid);
}

void TaskbarWindow::ShutdownIPC() {
    if (m_ipcReady.load(std::memory_order_relaxed)) {
        IPC_Shutdown();
        m_ipcReady.store(false, std::memory_order_relaxed);
    }
}

void TaskbarWindow::OnIPCMessage(IPC::SessionId, uint16_t msgType, const void* data, uint32_t size, void* userData) {
    auto* self = static_cast<TaskbarWindow*>(userData);
    if (msgType != static_cast<uint16_t>(IPC::MsgType::Application)) return;
    if (!data || size < sizeof(IPCMessageHeader)) return;
    const auto* hdr = static_cast<const IPCMessageHeader*>(data);
    const void* payload = static_cast<const uint8_t*>(data) + sizeof(IPCMessageHeader);
    uint32_t payloadSize = size - sizeof(IPCMessageHeader);
    switch (hdr->type) {
    case IPCMessageType::MainUIWindowCreated:
        if (payloadSize >= sizeof(WindowNotification)) { const auto* wn = static_cast<const WindowNotification*>(payload); self->AddTaskButtonFromIPC(wn->windowPid, wn->hwnd, wn->title); }
        break;
    case IPCMessageType::MainUIWindowDestroyed:
        if (payloadSize >= sizeof(WindowNotification)) { const auto* wn = static_cast<const WindowNotification*>(payload); self->RemoveTaskButtonFromIPC(wn->hwnd); }
        break;
    case IPCMessageType::MainUITabChanged:
        if (payloadSize >= sizeof(WindowNotification)) { const auto* wn = static_cast<const WindowNotification*>(payload); self->UpdateTaskButtonTitleFromIPC(wn->hwnd, wn->title); }
        break;
    default: break;
    }
}

void TaskbarWindow::OnPeerConnected(IPC::SessionId, IPC::ProcessId, void*) {}
void TaskbarWindow::OnPeerDisconnected(IPC::SessionId, void*) {}

void TaskbarWindow::AddTaskButtonFromIPC(uint32_t pid, uint64_t hwnd, const wchar_t* title) {
    std::lock_guard<std::mutex> lock(m_btnMutex);
    for (auto& btn : m_buttons) {
        if (btn.hwnd == reinterpret_cast<HWND>(hwnd)) {
            btn.title = title ? title : L"";
            InvalidateRect(m_hwnd, nullptr, TRUE);
            return;
        }
    }
    TaskButton btn{};
    btn.hwnd = reinterpret_cast<HWND>(hwnd); btn.pid = pid;
    btn.title = title ? title : L"";
    btn.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    btn.active = false; btn.pinned = false; btn.hovered = false;
    m_buttons.push_back(btn);
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void TaskbarWindow::RemoveTaskButtonFromIPC(uint64_t hwnd) {
    std::lock_guard<std::mutex> lock(m_btnMutex);
    for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it) {
        if (it->hwnd == reinterpret_cast<HWND>(hwnd)) { m_buttons.erase(it); break; }
    }
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void TaskbarWindow::UpdateTaskButtonTitleFromIPC(uint64_t hwnd, const wchar_t* title) {
    std::lock_guard<std::mutex> lock(m_btnMutex);
    for (auto& btn : m_buttons) {
        if (btn.hwnd == reinterpret_cast<HWND>(hwnd)) { btn.title = title ? title : L""; break; }
    }
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

#pragma endregion

#pragma region WndProc

LRESULT CALLBACK TaskbarWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TaskbarWindow* self = reinterpret_cast<TaskbarWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (self) self->DrawTaskbar(hdc, ps.rcPaint);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP:
        if (self) self->HandleClick(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_MBUTTONUP:
        if (self) self->HandleMiddleClick(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_RBUTTONUP:
        if (self) self->HandleRightClick(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_TIMER:
        if (self) InvalidateRect(hWnd, nullptr, TRUE);
        return 0;
    case WM_HOTKEY:
        if (wParam == 1) ShellExecuteW(hWnd, L"open", L"MainUI.exe", nullptr, nullptr, SW_SHOW);
        else if (wParam == 2) { HWND fg = GetForegroundWindow(); if (fg != self->m_hwnd) ShowWindow(fg, SW_MINIMIZE); }
        return 0;
    case WM_USER + 1: return 0;
    case WM_USER + 2:
        if (self) {
            if (wParam == 1) ShowWindow(hWnd, SW_HIDE);
            else { ShowWindow(hWnd, SW_SHOWNOACTIVATE); SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); }
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

#pragma endregion

} // namespace LlaShell

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    LlaShell::TaskbarWindow taskbar;
    if (!taskbar.Create(hInstance)) return 1;
    taskbar.Show();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    taskbar.Shutdown();
    return 0;
}

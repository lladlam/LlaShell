#include "taskbar.h"

namespace LlaShell {

TaskbarWindow::TaskbarWindow()
    : m_taskbarHeight(kDefaultTaskbarHeight)
    , m_buttonWidth(kDefaultButtonWidth)
    , m_buttonHeight(kDefaultButtonHeight)
    , m_dpi(96)
    , m_hwnd(nullptr)
    , m_hInstance(nullptr)
    , m_calendarHwnd(nullptr)
    , m_trayFlyoutHwnd(nullptr)
    , m_tooltipHwnd(nullptr)
    , m_popupMenuHwnd(nullptr)
    , m_running(false)
    , m_isHidden(false)
    , m_showingDesktop(false)
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
    , m_hoverTrayIconId(0)
    , m_dragTrayIconId(0)
    , m_dragTrayIconFromFlyout(false)
    , m_popupMenuKind(PopupMenuNone)
    , m_popupTaskButtonIndex(-1)
    , m_ipcReady(false)
{
    m_clockText[0] = L'\0';
    m_dateText[0] = L'\0';
    SetRectEmpty(&m_hiddenTrayButtonRect);
    SetRectEmpty(&m_trayAreaRect);
    SetRectEmpty(&m_clockRect);
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

    WNDCLASSEXW popupWc{};
    popupWc.cbSize        = sizeof(popupWc);
    popupWc.style         = CS_HREDRAW | CS_VREDRAW;
    popupWc.hInstance     = hInstance;
    popupWc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    popupWc.hbrBackground = nullptr;

    popupWc.lpfnWndProc   = CalendarWndProc;
    popupWc.lpszClassName = L"LlaShell_CalendarFlyout";
    RegisterClassExW(&popupWc);

    popupWc.lpfnWndProc   = TrayFlyoutWndProc;
    popupWc.lpszClassName = L"LlaShell_TrayFlyout";
    RegisterClassExW(&popupWc);

    popupWc.lpfnWndProc   = PopupMenuWndProc;
    popupWc.lpszClassName = L"LlaShell_PopupMenu";
    RegisterClassExW(&popupWc);

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

    m_bgBrush      = CreateSolidBrush(RGB(0, 8, 12));
    m_btnBrush     = CreateSolidBrush(RGB(0, 8, 12));
    m_hoverBrush   = CreateSolidBrush(RGB(38, 38, 38));
    m_activeBrush  = CreateSolidBrush(RGB(64, 64, 64));
    m_borderPen    = CreatePen(PS_SOLID, 1, RGB(82, 82, 82));
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
    HideTrayTooltip();
    HideCalendarFlyout();
    HideTrayFlyout();
    if (m_popupMenuHwnd) {
        DestroyWindow(m_popupMenuHwnd);
        m_popupMenuHwnd = nullptr;
    }

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
    UnregisterClassW(L"LlaShell_CalendarFlyout", m_hInstance);
    UnregisterClassW(L"LlaShell_TrayFlyout", m_hInstance);
    UnregisterClassW(L"LlaShell_PopupMenu", m_hInstance);
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
    SyncTrayIconsFromButtons();
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

    RECT taskViewRc = { x, 0, x + Scale(kTaskViewBtnWidth), m_taskbarHeight };
    DrawTaskViewButton(hdc, taskViewRc);
    x += Scale(kTaskViewBtnWidth);

    int showDesktopLeft = rc.right - Scale(kShowDesktopWidth);
    int notifLeft = showDesktopLeft - Scale(kNotifBtnWidth);
    int clockLeft = notifLeft - Scale(kClockWidth);
    int trayLeft = clockLeft - Scale(kTrayAreaMinWidth);
    if (trayLeft < x) trayLeft = x;

    RECT btnRc = { x, 0, trayLeft, m_taskbarHeight };
    DrawTaskButtons(hdc, btnRc);

    RECT trayRc = { trayLeft, 0, clockLeft, m_taskbarHeight };
    m_trayAreaRect = trayRc;
    DrawTrayArea(hdc, trayRc);

    RECT clockRc = { clockLeft, 0, notifLeft, m_taskbarHeight };
    m_clockRect = clockRc;
    DrawClock(hdc, clockRc);

    RECT notifRc = { notifLeft, 0, showDesktopLeft, m_taskbarHeight };
    DrawNotificationButton(hdc, notifRc);

    RECT showDesktopRc = { showDesktopLeft, 0, rc.right, m_taskbarHeight };
    DrawShowDesktop(hdc, showDesktopRc);
}

void TaskbarWindow::DrawStartButton(HDC hdc, const RECT& rc) {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(m_hwnd, &pt);
    bool hovered = PtInRect(&rc, pt);

    FillRect(hdc, &rc, hovered ? m_hoverBrush : m_btnBrush);

    int cx = rc.left + (rc.right - rc.left) / 2;
    int cy = rc.top + (rc.bottom - rc.top) / 2;
    int logo = Scale(16);
    int gap = Scale(1);
    int left = cx - logo / 2;
    int top = cy - logo / 2;

    HBRUSH brush = CreateSolidBrush(RGB(255, 255, 255));
    RECT pane = { left, top, left + logo / 2 - gap, top + logo / 2 - gap };
    FillRect(hdc, &pane, brush);
    pane = { left + logo / 2 + gap, top, left + logo, top + logo / 2 - gap };
    FillRect(hdc, &pane, brush);
    pane = { left, top + logo / 2 + gap, left + logo / 2 - gap, top + logo };
    FillRect(hdc, &pane, brush);
    pane = { left + logo / 2 + gap, top + logo / 2 + gap, left + logo, top + logo };
    FillRect(hdc, &pane, brush);
    DeleteObject(brush);
}

void TaskbarWindow::DrawSearchButton(HDC hdc, const RECT& rc) {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(m_hwnd, &pt);
    FillRect(hdc, &rc, PtInRect(&rc, pt) ? m_hoverBrush : m_btnBrush);

    int cx = rc.left + (rc.right - rc.left) / 2;
    int cy = rc.top + (rc.bottom - rc.top) / 2;
    int r = Scale(5);

    HPEN pen = CreatePen(PS_SOLID, Scale(2), RGB(245, 245, 245));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
    MoveToEx(hdc, cx + r - 1, cy + r - 1, nullptr);
    LineTo(hdc, cx + r + Scale(3), cy + r + Scale(3));
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
}

void TaskbarWindow::DrawTaskViewButton(HDC hdc, const RECT& rc) {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(m_hwnd, &pt);
    FillRect(hdc, &rc, PtInRect(&rc, pt) ? m_hoverBrush : m_btnBrush);

    int cx = rc.left + (rc.right - rc.left) / 2;
    int cy = rc.top + (rc.bottom - rc.top) / 2;
    int w = Scale(18);
    int h = Scale(14);

    HPEN pen = CreatePen(PS_SOLID, Scale(1), RGB(245, 245, 245));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    Rectangle(hdc, cx - w / 2, cy - h / 2, cx + Scale(2), cy + h / 2);
    MoveToEx(hdc, cx + Scale(6), cy - h / 2, nullptr);
    LineTo(hdc, cx + w / 2, cy - h / 2);
    MoveToEx(hdc, cx + Scale(6), cy, nullptr);
    LineTo(hdc, cx + w / 2, cy);
    MoveToEx(hdc, cx + Scale(6), cy + h / 2, nullptr);
    LineTo(hdc, cx + w / 2, cy + h / 2);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void TaskbarWindow::DrawTaskButtons(HDC hdc, const RECT& rc) {
    std::lock_guard<std::mutex> lock(m_btnMutex);
    int x = rc.left;

    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(m_hwnd, &pt);

    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);
    SetBkMode(hdc, TRANSPARENT);

    for (auto& btn : m_buttons) {
        int bw = m_buttonWidth;
        if (x + bw > rc.right) break;

        RECT btnRc = { x, 0, x + bw, m_taskbarHeight };
        btn.rect = btnRc;
        bool hovered = PtInRect(&btnRc, pt);

        if (btn.active) {
            FillRect(hdc, &btnRc, m_activeBrush);
        } else if (hovered) {
            FillRect(hdc, &btnRc, m_hoverBrush);
        }

        int iconX = btnRc.left + (bw - Scale(kButtonIconSize)) / 2;
        int iconY = btnRc.top + (m_taskbarHeight - Scale(kButtonIconSize)) / 2 - Scale(1);
        if (btn.hIcon)
            DrawIconEx(hdc, iconX, iconY, btn.hIcon, Scale(kButtonIconSize), Scale(kButtonIconSize), 0, nullptr, DI_NORMAL);

        DrawActiveIndicator(hdc, btnRc, m_accentColor);

        x += bw + Scale(kButtonPad);
    }
    SelectObject(hdc, oldFont);
}

void TaskbarWindow::DrawActiveIndicator(HDC hdc, const RECT& btnRc, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, Scale(kActiveBarHeight), color);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, btnRc.left + Scale(8), btnRc.bottom - Scale(2), nullptr);
    LineTo(hdc, btnRc.right - Scale(8), btnRc.bottom - Scale(2));
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void TaskbarWindow::DrawTrayArea(HDC hdc, const RECT& rc) {
    FillRect(hdc, &rc, m_bgBrush);
    if (rc.right <= rc.left) return;

    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(m_hwnd, &pt);

    int caretW = Scale(30);
    RECT caretRc = { rc.left, 0, rc.left + caretW, m_taskbarHeight };
    m_hiddenTrayButtonRect = caretRc;
    if (PtInRect(&caretRc, pt)) FillRect(hdc, &caretRc, m_hoverBrush);

    int cx = caretRc.left + caretW / 2;
    int cy = m_taskbarHeight / 2 + Scale(1);
    HPEN pen = CreatePen(PS_SOLID, Scale(1), RGB(245, 245, 245));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, cx - Scale(5), cy + Scale(2), nullptr);
    LineTo(hdc, cx, cy - Scale(3));
    LineTo(hdc, cx + Scale(5), cy + Scale(2));
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    int iconX = caretRc.right;
    int iconSlot = Scale(32);
    int imeLeft = rc.right - Scale(46);
    {
        std::lock_guard<std::mutex> lock(m_trayMutex);
        for (auto& icon : m_trayIcons) {
            SetRectEmpty(&icon.rect);
            if (icon.hidden) continue;
            if (iconX + iconSlot > imeLeft) break;

            RECT iconRc = { iconX, 0, iconX + iconSlot, m_taskbarHeight };
            icon.rect = iconRc;
            if (PtInRect(&iconRc, pt)) FillRect(hdc, &iconRc, m_hoverBrush);
            if (icon.icon) {
                DrawIconEx(hdc,
                    iconRc.left + (iconSlot - Scale(kTrayIconSize)) / 2,
                    iconRc.top + (m_taskbarHeight - Scale(kTrayIconSize)) / 2,
                    icon.icon,
                    Scale(kTrayIconSize), Scale(kTrayIconSize),
                    0, nullptr, DI_NORMAL);
            }
            iconX += iconSlot;
        }
    }

    std::wstring imeText = GetInputMethodIndicator();
    RECT imeRc = { imeLeft, 0, rc.right - Scale(4), m_taskbarHeight };
    if (imeRc.left < caretRc.right) imeRc.left = caretRc.right;
    if (PtInRect(&imeRc, pt)) FillRect(hdc, &imeRc, m_hoverBrush);

    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawTextW(hdc, imeText.c_str(), -1, &imeRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

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
    SelectObject(hdc, m_fontSmall);
    GetTextExtentPoint32W(hdc, dateBuf, static_cast<int>(wcslen(dateBuf)), &dateSz);

    int totalH = timeSz.cy + 2 + dateSz.cy;
    int startY = (m_taskbarHeight - totalH) / 2;

    int textX = rc.left + (rc.right - rc.left - timeSz.cx) / 2;

    SelectObject(hdc, m_font);
    SetTextColor(hdc, RGB(255, 255, 255));
    RECT timeRc = { textX, startY, textX + timeSz.cx, startY + timeSz.cy };
    DrawTextW(hdc, timeBuf, -1, &timeRc, DT_LEFT | DT_TOP);

    SelectObject(hdc, m_fontSmall);
    int dateX = rc.left + (rc.right - rc.left - dateSz.cx) / 2;
    SetTextColor(hdc, RGB(255, 255, 255));
    RECT dateRc = { dateX, startY + timeSz.cy + 2, dateX + dateSz.cx, startY + timeSz.cy + 2 + dateSz.cy };
    DrawTextW(hdc, dateBuf, -1, &dateRc, DT_LEFT | DT_TOP);

    SelectObject(hdc, oldFont);
}

void TaskbarWindow::DrawNotificationButton(HDC hdc, const RECT& rc) {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(m_hwnd, &pt);
    FillRect(hdc, &rc, PtInRect(&rc, pt) ? m_hoverBrush : m_btnBrush);

    int cx = rc.left + (rc.right - rc.left) / 2;
    int cy = rc.top + (rc.bottom - rc.top) / 2;
    int w = Scale(15);
    int h = Scale(13);

    HPEN pen = CreatePen(PS_SOLID, Scale(1), RGB(245, 245, 245));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
    MoveToEx(hdc, cx + w / 2 - Scale(4), cy + h / 2, nullptr);
    LineTo(hdc, cx + w / 2, cy + h / 2 + Scale(4));
    LineTo(hdc, cx + w / 2 - Scale(8), cy + h / 2);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void TaskbarWindow::DrawShowDesktop(HDC hdc, const RECT& rc) {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(m_hwnd, &pt);
    if (PtInRect(&rc, pt)) FillRect(hdc, &rc, m_hoverBrush);

    HPEN oldPen = (HPEN)SelectObject(hdc, m_borderPen);
    MoveToEx(hdc, rc.left, rc.top, nullptr);
    LineTo(hdc, rc.left, rc.bottom);
    SelectObject(hdc, oldPen);
}

std::wstring TaskbarWindow::GetInputMethodIndicator() const {
    HWND fg = GetForegroundWindow();
    DWORD threadId = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    HKL hkl = GetKeyboardLayout(threadId);
    LANGID langId = LOWORD(reinterpret_cast<UINT_PTR>(hkl));
    WORD primaryLang = PRIMARYLANGID(langId);

    if (primaryLang == LANG_CHINESE) return L"中";
    if (primaryLang == LANG_JAPANESE) return L"日";
    if (primaryLang == LANG_KOREAN) return L"한";

    wchar_t isoName[8]{};
    if (GetLocaleInfoW(langId, LOCALE_SISO639LANGNAME, isoName, _countof(isoName)) && isoName[0] != L'\0') {
        CharUpperBuffW(isoName, static_cast<DWORD>(wcslen(isoName)));
        if (_wcsicmp(isoName, L"EN") == 0) return L"ENG";
        return isoName;
    }
    return L"ENG";
}

void TaskbarWindow::DrawPopupMenu(HWND hwnd, HDC hdc, const RECT& rc) {
    HBRUSH bg = CreateSolidBrush(RGB(43, 43, 43));
    HBRUSH hover = CreateSolidBrush(RGB(62, 62, 62));
    HPEN border = CreatePen(PS_SOLID, 1, RGB(92, 92, 92));
    FillRect(hdc, &rc, bg);

    HPEN oldPen = (HPEN)SelectObject(hdc, border);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);

    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);

    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(245, 245, 245));

    int itemH = Scale(36);
    int y = Scale(1);
    for (const auto& item : m_popupItems) {
        RECT itemRc = { Scale(1), y, rc.right - Scale(1), y + itemH };
        if (PtInRect(&itemRc, pt)) FillRect(hdc, &itemRc, hover);

        RECT textRc = { Scale(34), y, rc.right - Scale(12), y + itemH };
        DrawTextW(hdc, item.label.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (m_popupMenuKind == PopupMenuTaskButton && item.id == 3) {
            HPEN closePen = CreatePen(PS_SOLID, 1, RGB(230, 230, 230));
            HPEN oldClosePen = (HPEN)SelectObject(hdc, closePen);
            int cx = Scale(17);
            int cy = y + itemH / 2;
            MoveToEx(hdc, cx - Scale(5), cy - Scale(5), nullptr);
            LineTo(hdc, cx + Scale(5), cy + Scale(5));
            MoveToEx(hdc, cx + Scale(5), cy - Scale(5), nullptr);
            LineTo(hdc, cx - Scale(5), cy + Scale(5));
            SelectObject(hdc, oldClosePen);
            DeleteObject(closePen);
        }

        y += itemH;
    }

    SelectObject(hdc, oldFont);
    DeleteObject(border);
    DeleteObject(hover);
    DeleteObject(bg);
}

static int DaysInMonth(int year, int month) {
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2) {
        bool leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
        return leap ? 29 : 28;
    }
    return days[month - 1];
}

static int DayOfWeekSundayFirst(int year, int month, int day) {
    if (month < 3) {
        month += 12;
        --year;
    }
    int k = year % 100;
    int j = year / 100;
    int h = (day + (13 * (month + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    return (h + 6) % 7;
}

void TaskbarWindow::DrawCalendarFlyout(HWND, HDC hdc, const RECT& rc) {
    HBRUSH bg = CreateSolidBrush(RGB(31, 31, 31));
    HBRUSH panel = CreateSolidBrush(RGB(36, 36, 36));
    HBRUSH todayBrush = CreateSolidBrush(m_accentColor);
    HPEN border = CreatePen(PS_SOLID, 1, RGB(72, 72, 72));
    FillRect(hdc, &rc, bg);

    HPEN oldPen = (HPEN)SelectObject(hdc, border);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);

    SYSTEMTIME st;
    GetLocalTime(&st);
    const wchar_t* weekNames[] = { L"星期日", L"星期一", L"星期二", L"星期三", L"星期四", L"星期五", L"星期六" };

    HFONT timeFont = CreateFontW(-Scale(48), 0, 0, 0, FW_LIGHT, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT monthFont = CreateFontW(-Scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, timeFont);
    SetTextColor(hdc, RGB(255, 255, 255));

    wchar_t timeBuf[32]{};
    StringCchPrintfW(timeBuf, _countof(timeBuf), L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    RECT timeRc = { Scale(22), Scale(20), rc.right - Scale(22), Scale(82) };
    DrawTextW(hdc, timeBuf, -1, &timeRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, m_font);
    SetTextColor(hdc, RGB(64, 170, 255));
    wchar_t dateBuf[96]{};
    StringCchPrintfW(dateBuf, _countof(dateBuf), L"%d年%d月%d日 %s", st.wYear, st.wMonth, st.wDay, weekNames[st.wDayOfWeek]);
    RECT dateRc = { Scale(24), Scale(86), rc.right - Scale(24), Scale(112) };
    DrawTextW(hdc, dateBuf, -1, &dateRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT lowerRc = { rc.left, Scale(116), rc.right, rc.bottom };
    FillRect(hdc, &lowerRc, panel);
    HPEN sep = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
    HPEN oldSep = (HPEN)SelectObject(hdc, sep);
    MoveToEx(hdc, rc.left, lowerRc.top, nullptr);
    LineTo(hdc, rc.right, lowerRc.top);
    SelectObject(hdc, oldSep);
    DeleteObject(sep);

    SelectObject(hdc, monthFont);
    SetTextColor(hdc, RGB(245, 245, 245));
    wchar_t monthBuf[32]{};
    StringCchPrintfW(monthBuf, _countof(monthBuf), L"%d年%d月", st.wYear, st.wMonth);
    RECT monthRc = { Scale(24), Scale(132), rc.right - Scale(110), Scale(162) };
    DrawTextW(hdc, monthBuf, -1, &monthRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    HPEN arrowPen = CreatePen(PS_SOLID, 1, RGB(220, 220, 220));
    HPEN oldArrow = (HPEN)SelectObject(hdc, arrowPen);
    int upX = rc.right - Scale(84);
    int downX = rc.right - Scale(38);
    int arrowY = Scale(146);
    MoveToEx(hdc, upX - Scale(7), arrowY + Scale(4), nullptr);
    LineTo(hdc, upX, arrowY - Scale(4));
    LineTo(hdc, upX + Scale(7), arrowY + Scale(4));
    MoveToEx(hdc, downX - Scale(7), arrowY - Scale(4), nullptr);
    LineTo(hdc, downX, arrowY + Scale(4));
    LineTo(hdc, downX + Scale(7), arrowY - Scale(4));
    SelectObject(hdc, oldArrow);
    DeleteObject(arrowPen);

    SelectObject(hdc, m_fontSmall);
    const wchar_t* weekdays[] = { L"一", L"二", L"三", L"四", L"五", L"六", L"日" };
    int gridLeft = Scale(22);
    int gridTop = Scale(174);
    int cellW = (rc.right - Scale(44)) / 7;
    int cellH = Scale(40);
    SetTextColor(hdc, RGB(245, 245, 245));
    for (int i = 0; i < 7; ++i) {
        RECT wRc = { gridLeft + i * cellW, gridTop, gridLeft + (i + 1) * cellW, gridTop + Scale(22) };
        DrawTextW(hdc, weekdays[i], -1, &wRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    int firstDow = DayOfWeekSundayFirst(st.wYear, st.wMonth, 1);
    int firstMondayIndex = (firstDow + 6) % 7;
    int daysThis = DaysInMonth(st.wYear, st.wMonth);
    int prevYear = st.wMonth == 1 ? st.wYear - 1 : st.wYear;
    int prevMonth = st.wMonth == 1 ? 12 : st.wMonth - 1;
    int daysPrev = DaysInMonth(prevYear, prevMonth);
    int dayTop = gridTop + Scale(28);

    for (int row = 0; row < 6; ++row) {
        for (int col = 0; col < 7; ++col) {
            int idx = row * 7 + col;
            int day = idx - firstMondayIndex + 1;
            bool currentMonth = day >= 1 && day <= daysThis;
            int shownDay = day;
            if (!currentMonth) {
                if (day < 1) shownDay = daysPrev + day;
                else shownDay = day - daysThis;
            }

            RECT cellRc = {
                gridLeft + col * cellW,
                dayTop + row * cellH,
                gridLeft + (col + 1) * cellW,
                dayTop + (row + 1) * cellH
            };

            if (currentMonth && shownDay == st.wDay) {
                RECT fillRc = {
                    cellRc.left + Scale(6), cellRc.top + Scale(3),
                    cellRc.right - Scale(6), cellRc.bottom - Scale(3)
                };
                FillRect(hdc, &fillRc, todayBrush);
                HPEN focusPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
                HPEN oldFocus = (HPEN)SelectObject(hdc, focusPen);
                HBRUSH oldFocusBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, fillRc.left + Scale(2), fillRc.top + Scale(2), fillRc.right - Scale(2), fillRc.bottom - Scale(2));
                SelectObject(hdc, oldFocusBrush);
                SelectObject(hdc, oldFocus);
                DeleteObject(focusPen);
            }

            SetTextColor(hdc, currentMonth ? RGB(245, 245, 245) : RGB(125, 125, 125));
            wchar_t dayBuf[8]{};
            StringCchPrintfW(dayBuf, _countof(dayBuf), L"%d", shownDay);
            DrawTextW(hdc, dayBuf, -1, &cellRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    SelectObject(hdc, oldFont);
    DeleteObject(monthFont);
    DeleteObject(timeFont);
    DeleteObject(border);
    DeleteObject(todayBrush);
    DeleteObject(panel);
    DeleteObject(bg);
}

void TaskbarWindow::DrawTrayFlyout(HWND hwnd, HDC hdc, const RECT& rc) {
    HBRUSH bg = CreateSolidBrush(RGB(243, 243, 243));
    HBRUSH hover = CreateSolidBrush(RGB(225, 225, 225));
    HPEN border = CreatePen(PS_SOLID, 1, RGB(150, 150, 150));
    FillRect(hdc, &rc, bg);

    HPEN oldPen = (HPEN)SelectObject(hdc, border);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);

    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);

    int slot = Scale(36);
    int cols = 5;
    int x0 = Scale(10);
    int y0 = Scale(10);
    int index = 0;

    std::lock_guard<std::mutex> lock(m_trayMutex);
    for (auto& icon : m_trayIcons) {
        if (!icon.hidden) {
            SetRectEmpty(&icon.rect);
            continue;
        }
        int col = index % cols;
        int row = index / cols;
        RECT iconRc = { x0 + col * slot, y0 + row * slot, x0 + (col + 1) * slot, y0 + (row + 1) * slot };
        icon.rect = iconRc;
        if (PtInRect(&iconRc, pt)) FillRect(hdc, &iconRc, hover);
        if (icon.icon) {
            DrawIconEx(hdc,
                iconRc.left + (slot - Scale(kTrayIconSize)) / 2,
                iconRc.top + (slot - Scale(kTrayIconSize)) / 2,
                icon.icon,
                Scale(kTrayIconSize), Scale(kTrayIconSize),
                0, nullptr, DI_NORMAL);
        }
        ++index;
    }

    if (index == 0) {
        HFONT oldFont = (HFONT)SelectObject(hdc, m_fontSmall);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(80, 80, 80));
        RECT textRc = { Scale(12), Scale(12), rc.right - Scale(12), rc.bottom - Scale(12) };
        DrawTextW(hdc, L"没有隐藏的托盘图标", -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
    }

    DeleteObject(border);
    DeleteObject(hover);
    DeleteObject(bg);
}

#pragma endregion

#pragma region Input Handling

void TaskbarWindow::HandleClick(int x, int y) {
    HideTrayTooltip();
    int searchEnd = Scale(kStartBtnWidth) + Scale(kSearchBtnWidth);
    int taskViewEnd = searchEnd + Scale(kTaskViewBtnWidth);
    if (x < Scale(kStartBtnWidth)) {
        HideCalendarFlyout();
        HideTrayFlyout();
        POINT pt = { x, y };
        ClientToScreen(m_hwnd, &pt);
        ShowStartMenu(pt.x, pt.y);
        return;
    }
    if (x < taskViewEnd) return;

    RECT rc;
    GetClientRect(m_hwnd, &rc);

    int showDesktopLeft = rc.right - Scale(kShowDesktopWidth);
    if (x >= showDesktopLeft) {
        HideCalendarFlyout();
        HideTrayFlyout();
        ToggleDesktop();
        return;
    }

    int notifLeft = showDesktopLeft - Scale(kNotifBtnWidth);
    int clockLeft = notifLeft - Scale(kClockWidth);
    int trayLeft = clockLeft - Scale(kTrayAreaMinWidth);
    if (trayLeft < taskViewEnd) trayLeft = taskViewEnd;
    if (x >= clockLeft && x < notifLeft) {
        HideTrayFlyout();
        ShowCalendarFlyout();
        return;
    }
    if (PtInRect(&m_hiddenTrayButtonRect, { x, y })) {
        HideCalendarFlyout();
        ShowTrayFlyout();
        return;
    }
    if (x >= trayLeft) return;

    HideCalendarFlyout();
    HideTrayFlyout();
    std::lock_guard<std::mutex> lock(m_btnMutex);
    for (auto& btn : m_buttons) {
        if (PtInRect(&btn.rect, { x, y })) {
            HWND target = btn.hwnd;
            {
                std::lock_guard<std::mutex> desktopLock(m_desktopMutex);
                m_showingDesktop.store(false, std::memory_order_relaxed);
                m_desktopRestoreWindows.clear();
            }
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

void TaskbarWindow::HandleMouseDown(int x, int y) {
    std::uintptr_t iconId = 0;
    if (HitTestTrayIcon(x, y, false, &iconId, nullptr, nullptr)) {
        BeginTrayIconDrag(iconId, false);
        SetCapture(m_hwnd);
    }
}

void TaskbarWindow::HandleMouseMove(int x, int y) {
    TRACKMOUSEEVENT tme{};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = m_hwnd;
    TrackMouseEvent(&tme);

    if (m_dragTrayIconId) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    std::uintptr_t iconId = 0;
    std::wstring title;
    RECT iconRc{};
    if (HitTestTrayIcon(x, y, false, &iconId, &title, &iconRc)) {
        if (m_hoverTrayIconId != iconId) {
            POINT tl = { iconRc.left, iconRc.top };
            POINT br = { iconRc.right, iconRc.bottom };
            ClientToScreen(m_hwnd, &tl);
            ClientToScreen(m_hwnd, &br);
            RECT screenRc = { tl.x, tl.y, br.x, br.y };
            ShowTrayTooltip(title, screenRc);
            m_hoverTrayIconId = iconId;
        }
    } else {
        HideTrayTooltip();
    }

    InvalidateRect(m_hwnd, nullptr, FALSE);
}

bool TaskbarWindow::HandleMouseUp(int x, int y) {
    if (!m_dragTrayIconId) return false;
    POINT pt = { x, y };
    ClientToScreen(m_hwnd, &pt);
    return FinishTrayIconDrag(pt);
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
        ShowStartContextMenu(pt.x, pt.y);
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
    ShowPopupMenu(PopupMenuTaskButton, index, screenX, screenY);
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
    ShowPopupMenu(PopupMenuTaskbar, -1, screenX, screenY);
}

void TaskbarWindow::ShowStartContextMenu(int screenX, int screenY) {
    ShowPopupMenu(PopupMenuStart, -1, screenX, screenY);
}

void TaskbarWindow::ShowPopupMenu(PopupMenuKind kind, int taskButtonIndex, int screenX, int screenY) {
    HideTrayTooltip();
    HideCalendarFlyout();
    HideTrayFlyout();
    if (m_popupMenuHwnd) {
        DestroyWindow(m_popupMenuHwnd);
        m_popupMenuHwnd = nullptr;
    }

    m_popupMenuKind = kind;
    m_popupTaskButtonIndex = taskButtonIndex;
    m_popupItems.clear();

    int menuW = Scale(230);
    if (kind == PopupMenuTaskbar) {
        m_popupItems.push_back({ 1, L"打开任务管理器", false });
        menuW = Scale(220);
    } else if (kind == PopupMenuTaskButton) {
        bool pinned = false;
        {
            std::lock_guard<std::mutex> lock(m_btnMutex);
            if (taskButtonIndex >= 0 && taskButtonIndex < static_cast<int>(m_buttons.size())) {
                std::wstring exePath = GetWindowExePath(m_buttons[taskButtonIndex].hwnd);
                pinned = IsPinned(exePath.c_str());
            }
        }
        m_popupItems.push_back({ 1, L"任务管理器", false });
        m_popupItems.push_back({ 2, pinned ? L"从任务栏取消固定" : L"固定到任务栏", false });
        m_popupItems.push_back({ 3, L"关闭窗口", false });
        menuW = Scale(230);
    } else if (kind == PopupMenuStart) {
        m_popupItems.push_back({ 10, L"Windows PowerShell", false });
        m_popupItems.push_back({ 1, L"任务管理器", false });
        m_popupItems.push_back({ 11, L"运行", false });
        m_popupItems.push_back({ 12, L"回到桌面", false });
        menuW = Scale(260);
    }

    int itemH = Scale(36);
    int menuH = static_cast<int>(m_popupItems.size()) * itemH + Scale(2);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    if (screenX + menuW > screenW) screenX = screenW - menuW;
    if (screenX < 0) screenX = 0;
    if (screenY + menuH > screenH - m_taskbarHeight) screenY -= menuH;
    if (screenY < 0) screenY = 0;

    m_popupMenuHwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"LlaShell_PopupMenu", L"",
        WS_POPUP,
        screenX, screenY, menuW, menuH,
        m_hwnd, nullptr, m_hInstance, this);
    if (!m_popupMenuHwnd) return;

    ShowWindow(m_popupMenuHwnd, SW_SHOW);
    SetForegroundWindow(m_popupMenuHwnd);
    UpdateWindow(m_popupMenuHwnd);
}

void TaskbarWindow::ExecutePopupCommand(int commandId) {
    PopupMenuKind kind = m_popupMenuKind;
    int taskButtonIndex = m_popupTaskButtonIndex;

    if (m_popupMenuHwnd) {
        DestroyWindow(m_popupMenuHwnd);
        m_popupMenuHwnd = nullptr;
    }

    switch (commandId) {
    case 1:
        ShellExecuteW(m_hwnd, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOW);
        break;
    case 2: {
        std::lock_guard<std::mutex> lock(m_btnMutex);
        if (kind == PopupMenuTaskButton && taskButtonIndex >= 0 && taskButtonIndex < static_cast<int>(m_buttons.size())) {
            auto& btn = m_buttons[taskButtonIndex];
            std::wstring exePath = GetWindowExePath(btn.hwnd);
            if (!exePath.empty()) {
                if (IsPinned(exePath.c_str())) RemovePinnedApp(exePath.c_str());
                else AddPinnedApp(exePath.c_str(), btn.title.c_str());
            }
        }
        break;
    }
    case 3: {
        std::lock_guard<std::mutex> lock(m_btnMutex);
        if (kind == PopupMenuTaskButton && taskButtonIndex >= 0 && taskButtonIndex < static_cast<int>(m_buttons.size())) {
            PostMessage(m_buttons[taskButtonIndex].hwnd, WM_CLOSE, 0, 0);
        }
        break;
    }
    case 10:
        ShellExecuteW(m_hwnd, L"open", L"powershell.exe", nullptr, nullptr, SW_SHOW);
        break;
    case 11:
        LaunchRunDialog();
        break;
    case 12:
        ToggleDesktop();
        break;
    default:
        break;
    }
}

BOOL CALLBACK TaskbarWindow::ToggleDesktopEnumProc(HWND hwnd, LPARAM lParam) {
    auto* self = reinterpret_cast<TaskbarWindow*>(lParam);
    if (!self || hwnd == self->m_hwnd) return TRUE;
    if (!self->IsTaskbarWindow(hwnd)) return TRUE;
    if (IsIconic(hwnd)) return TRUE;

    self->m_desktopRestoreWindows.push_back(hwnd);
    ShowWindow(hwnd, SW_MINIMIZE);
    return TRUE;
}

void TaskbarWindow::ToggleDesktop() {
    std::lock_guard<std::mutex> lock(m_desktopMutex);
    if (m_showingDesktop.load(std::memory_order_relaxed)) {
        for (auto it = m_desktopRestoreWindows.rbegin(); it != m_desktopRestoreWindows.rend(); ++it) {
            if (IsWindow(*it)) ShowWindow(*it, SW_RESTORE);
        }
        if (!m_desktopRestoreWindows.empty() && IsWindow(m_desktopRestoreWindows.back())) {
            SetForegroundWindow(m_desktopRestoreWindows.back());
        }
        m_desktopRestoreWindows.clear();
        m_showingDesktop.store(false, std::memory_order_relaxed);
    } else {
        m_desktopRestoreWindows.clear();
        EnumWindows(ToggleDesktopEnumProc, reinterpret_cast<LPARAM>(this));
        m_showingDesktop.store(true, std::memory_order_relaxed);
    }
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, TRUE);
}

void TaskbarWindow::ShowCalendarFlyout() {
    if (m_calendarHwnd) {
        HideCalendarFlyout();
        return;
    }

    int w = Scale(360);
    int h = Scale(500);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = screenW - w;
    int y = screenH - m_taskbarHeight - h;
    if (y < 0) y = 0;

    m_calendarHwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"LlaShell_CalendarFlyout", L"",
        WS_POPUP,
        x, y, w, h,
        m_hwnd, nullptr, m_hInstance, this);
    if (!m_calendarHwnd) return;

    SetTimer(m_calendarHwnd, 1, 1000, nullptr);
    ShowWindow(m_calendarHwnd, SW_SHOW);
    SetForegroundWindow(m_calendarHwnd);
    UpdateWindow(m_calendarHwnd);
}

void TaskbarWindow::HideCalendarFlyout() {
    if (m_calendarHwnd) {
        DestroyWindow(m_calendarHwnd);
        m_calendarHwnd = nullptr;
    }
}

void TaskbarWindow::ShowTrayFlyout() {
    if (m_trayFlyoutHwnd) {
        HideTrayFlyout();
        return;
    }

    int hiddenCount = 0;
    {
        std::lock_guard<std::mutex> lock(m_trayMutex);
        for (const auto& icon : m_trayIcons) {
            if (icon.hidden) ++hiddenCount;
        }
    }
    int cols = 5;
    int rows = hiddenCount > 0 ? ((hiddenCount + cols - 1) / cols) : 1;
    int w = Scale(200);
    int h = Scale(20) + rows * Scale(36);

    RECT caretRc = m_hiddenTrayButtonRect;
    POINT anchor = { caretRc.left, caretRc.top };
    ClientToScreen(m_hwnd, &anchor);
    int x = anchor.x;
    int y = anchor.y - h - Scale(4);
    if (y < 0) y = 0;

    m_trayFlyoutHwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"LlaShell_TrayFlyout", L"",
        WS_POPUP,
        x, y, w, h,
        m_hwnd, nullptr, m_hInstance, this);
    if (!m_trayFlyoutHwnd) return;

    ShowWindow(m_trayFlyoutHwnd, SW_SHOW);
    SetForegroundWindow(m_trayFlyoutHwnd);
    UpdateWindow(m_trayFlyoutHwnd);
}

void TaskbarWindow::HideTrayFlyout() {
    if (m_trayFlyoutHwnd) {
        DestroyWindow(m_trayFlyoutHwnd);
        m_trayFlyoutHwnd = nullptr;
    }
}

void TaskbarWindow::SyncTrayIconsFromButtons() {
    std::vector<TrayIconItem> next;
    {
        std::lock_guard<std::mutex> btnLock(m_btnMutex);
        std::lock_guard<std::mutex> trayLock(m_trayMutex);
        for (const auto& btn : m_buttons) {
            if (!btn.hwnd || !IsWindow(btn.hwnd)) continue;
            TrayIconItem item{};
            item.id = reinterpret_cast<std::uintptr_t>(btn.hwnd);
            item.title = btn.title.empty() ? L"应用" : btn.title;
            item.icon = btn.hIcon;
            item.hidden = true;
            SetRectEmpty(&item.rect);
            for (const auto& old : m_trayIcons) {
                if (old.id == item.id) {
                    item.hidden = old.hidden;
                    break;
                }
            }
            next.push_back(item);
        }
        m_trayIcons.swap(next);
    }
}

void TaskbarWindow::ShowTrayTooltip(const std::wstring& text, const RECT& screenRect) {
    if (text.empty()) return;
    if (!m_tooltipHwnd) {
        m_tooltipHwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"STATIC", L"",
            WS_POPUP | SS_CENTER,
            0, 0, 10, 10,
            m_hwnd, nullptr, m_hInstance, nullptr);
        if (m_tooltipHwnd) SendMessageW(m_tooltipHwnd, WM_SETFONT, (WPARAM)m_fontSmall, TRUE);
    }
    if (!m_tooltipHwnd) return;

    HDC hdc = GetDC(m_tooltipHwnd);
    SIZE sz{};
    GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &sz);
    ReleaseDC(m_tooltipHwnd, hdc);

    int w = sz.cx + Scale(10);
    int h = sz.cy + Scale(6);
    int x = screenRect.left + ((screenRect.right - screenRect.left) - w) / 2;
    int y = screenRect.top - h - Scale(2);
    if (y < 0) y = screenRect.bottom + Scale(2);

    SetWindowTextW(m_tooltipHwnd, text.c_str());
    SetWindowPos(m_tooltipHwnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

void TaskbarWindow::HideTrayTooltip() {
    if (m_tooltipHwnd) {
        DestroyWindow(m_tooltipHwnd);
        m_tooltipHwnd = nullptr;
        m_hoverTrayIconId = 0;
    }
}

void TaskbarWindow::BeginTrayIconDrag(std::uintptr_t iconId, bool fromFlyout) {
    if (!iconId) return;
    m_dragTrayIconId = iconId;
    m_dragTrayIconFromFlyout = fromFlyout;
    HideTrayTooltip();
}

bool TaskbarWindow::FinishTrayIconDrag(POINT screenPt) {
    if (!m_dragTrayIconId) return false;

    if (m_dragTrayIconFromFlyout) {
        if (IsPointInTaskbarTray(screenPt)) {
            SetTrayIconHidden(m_dragTrayIconId, false);
        }
    } else {
        if (IsPointInHiddenTray(screenPt)) {
            SetTrayIconHidden(m_dragTrayIconId, true);
        }
    }

    m_dragTrayIconId = 0;
    m_dragTrayIconFromFlyout = false;
    ReleaseCapture();
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, TRUE);
    if (m_trayFlyoutHwnd) InvalidateRect(m_trayFlyoutHwnd, nullptr, TRUE);
    return true;
}

bool TaskbarWindow::HitTestTrayIcon(int x, int y, bool hiddenOnly, std::uintptr_t* iconId, std::wstring* title, RECT* rect) const {
    std::lock_guard<std::mutex> lock(m_trayMutex);
    for (const auto& icon : m_trayIcons) {
        if (hiddenOnly != icon.hidden) continue;
        if (IsRectEmpty(&icon.rect)) continue;
        POINT pt = { x, y };
        if (PtInRect(&icon.rect, pt)) {
            if (iconId) *iconId = icon.id;
            if (title) *title = icon.title;
            if (rect) *rect = icon.rect;
            return true;
        }
    }
    return false;
}

bool TaskbarWindow::IsPointInTaskbarTray(POINT screenPt) const {
    RECT trayRc = m_trayAreaRect;
    POINT tl = { trayRc.left, trayRc.top };
    POINT br = { trayRc.right, trayRc.bottom };
    ClientToScreen(m_hwnd, &tl);
    ClientToScreen(m_hwnd, &br);
    RECT screenRc = { tl.x, tl.y, br.x, br.y };
    return PtInRect(&screenRc, screenPt);
}

bool TaskbarWindow::IsPointInHiddenTray(POINT screenPt) const {
    RECT caretRc = m_hiddenTrayButtonRect;
    POINT tl = { caretRc.left, caretRc.top };
    POINT br = { caretRc.right, caretRc.bottom };
    ClientToScreen(m_hwnd, &tl);
    ClientToScreen(m_hwnd, &br);
    RECT caretScreenRc = { tl.x, tl.y, br.x, br.y };
    if (PtInRect(&caretScreenRc, screenPt)) return true;

    if (m_trayFlyoutHwnd) {
        RECT flyoutRc{};
        GetWindowRect(m_trayFlyoutHwnd, &flyoutRc);
        if (PtInRect(&flyoutRc, screenPt)) return true;
    }
    return false;
}

void TaskbarWindow::SetTrayIconHidden(std::uintptr_t iconId, bool hidden) {
    std::lock_guard<std::mutex> lock(m_trayMutex);
    for (auto& icon : m_trayIcons) {
        if (icon.id == iconId) {
            icon.hidden = hidden;
            SetRectEmpty(&icon.rect);
            break;
        }
    }
}

void TaskbarWindow::LaunchRunDialog() {
    ShellExecuteW(m_hwnd, L"open", L"explorer.exe",
        L"shell:::{2559a1f3-21d7-11d4-bdaf-00c04f60b9f0}",
        nullptr, SW_SHOW);
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
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(m_btnMutex);
        for (auto& btn : m_buttons) {
            if (btn.hwnd == reinterpret_cast<HWND>(hwnd)) {
                btn.title = title ? title : L"";
                found = true;
                break;
            }
        }
        if (!found) {
            TaskButton btn{};
            btn.hwnd = reinterpret_cast<HWND>(hwnd); btn.pid = pid;
            btn.title = title ? title : L"";
            btn.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
            btn.active = false; btn.pinned = false; btn.hovered = false;
            m_buttons.push_back(btn);
        }
    }
    SyncTrayIconsFromButtons();
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void TaskbarWindow::RemoveTaskButtonFromIPC(uint64_t hwnd) {
    {
        std::lock_guard<std::mutex> lock(m_btnMutex);
        for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it) {
            if (it->hwnd == reinterpret_cast<HWND>(hwnd)) { m_buttons.erase(it); break; }
        }
    }
    SyncTrayIconsFromButtons();
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void TaskbarWindow::UpdateTaskButtonTitleFromIPC(uint64_t hwnd, const wchar_t* title) {
    {
        std::lock_guard<std::mutex> lock(m_btnMutex);
        for (auto& btn : m_buttons) {
            if (btn.hwnd == reinterpret_cast<HWND>(hwnd)) { btn.title = title ? title : L""; break; }
        }
    }
    SyncTrayIconsFromButtons();
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
    case WM_LBUTTONDOWN:
        if (self) self->HandleMouseDown(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_LBUTTONUP:
        if (self && !self->HandleMouseUp(LOWORD(lParam), HIWORD(lParam)))
            self->HandleClick(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_MBUTTONUP:
        if (self) self->HandleMiddleClick(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_RBUTTONUP:
        if (self) self->HandleRightClick(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_MOUSEMOVE:
        if (self) self->HandleMouseMove(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_MOUSELEAVE:
        if (self) {
            self->HideTrayTooltip();
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    case WM_TIMER:
        if (self) InvalidateRect(hWnd, nullptr, TRUE);
        return 0;
    case WM_HOTKEY:
        if (wParam == 1) ShellExecuteW(hWnd, L"open", L"MainUI.exe", nullptr, nullptr, SW_SHOW);
        else if (wParam == 2 && self) self->ToggleDesktop();
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

LRESULT CALLBACK TaskbarWindow::CalendarWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TaskbarWindow* self = reinterpret_cast<TaskbarWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<TaskbarWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        if (self) self->DrawCalendarFlyout(hWnd, hdc, rc);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_TIMER:
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) DestroyWindow(hWnd);
        return 0;
    case WM_KILLFOCUS:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hWnd, 1);
        if (self && self->m_calendarHwnd == hWnd) self->m_calendarHwnd = nullptr;
        return 0;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

LRESULT CALLBACK TaskbarWindow::TrayFlyoutWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TaskbarWindow* self = reinterpret_cast<TaskbarWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<TaskbarWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        if (self) self->DrawTrayFlyout(hWnd, hdc, rc);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (self) {
            std::uintptr_t iconId = 0;
            if (self->HitTestTrayIcon(LOWORD(lParam), HIWORD(lParam), true, &iconId, nullptr, nullptr)) {
                self->BeginTrayIconDrag(iconId, true);
                SetCapture(hWnd);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (self && self->m_dragTrayIconId) {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            ClientToScreen(hWnd, &pt);
            self->FinishTrayIconDrag(pt);
            return 0;
        }
        return 0;
    case WM_MOUSEMOVE:
        if (self) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);

            std::uintptr_t iconId = 0;
            std::wstring title;
            RECT iconRc{};
            if (self->HitTestTrayIcon(LOWORD(lParam), HIWORD(lParam), true, &iconId, &title, &iconRc)) {
                if (self->m_hoverTrayIconId != iconId) {
                    POINT tl = { iconRc.left, iconRc.top };
                    POINT br = { iconRc.right, iconRc.bottom };
                    ClientToScreen(hWnd, &tl);
                    ClientToScreen(hWnd, &br);
                    RECT screenRc = { tl.x, tl.y, br.x, br.y };
                    self->ShowTrayTooltip(title, screenRc);
                    self->m_hoverTrayIconId = iconId;
                }
            } else {
                self->HideTrayTooltip();
            }
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (self) self->HideTrayTooltip();
        return 0;
    case WM_KILLFOCUS:
        if (self && !self->m_dragTrayIconId) DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        if (self && self->m_trayFlyoutHwnd == hWnd) self->m_trayFlyoutHwnd = nullptr;
        return 0;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

LRESULT CALLBACK TaskbarWindow::PopupMenuWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TaskbarWindow* self = reinterpret_cast<TaskbarWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<TaskbarWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        if (self) self->DrawPopupMenu(hWnd, hdc, rc);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP:
        if (self) {
            int itemH = self->Scale(36);
            int y = HIWORD(lParam) - self->Scale(1);
            int idx = y >= 0 ? y / itemH : -1;
            if (idx >= 0 && idx < static_cast<int>(self->m_popupItems.size())) {
                self->ExecutePopupCommand(self->m_popupItems[idx].id);
            } else {
                DestroyWindow(hWnd);
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) DestroyWindow(hWnd);
        return 0;
    case WM_KILLFOCUS:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        if (self && self->m_popupMenuHwnd == hWnd) self->m_popupMenuHwnd = nullptr;
        return 0;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

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

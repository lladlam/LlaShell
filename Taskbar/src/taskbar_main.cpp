#include "taskbar.h"
#include <shellapi.h>
#include <strsafe.h>

static constexpr wchar_t kTaskbarClassName[] = L"LlaShell_Taskbar";
static constexpr UINT WM_TRAYICON = WM_USER + 1;
static constexpr UINT WM_FULLSCREEN_CHECK = WM_USER + 2;
static constexpr int HOTKEY_WIN_E  = 1;
static constexpr int HOTKEY_WIN_D  = 2;
static constexpr int HOTKEY_ALT_TAB = 3;

namespace LlaShell {

TaskbarWindow::TaskbarWindow()
    : m_hwnd(nullptr)
    , m_hInstance(nullptr)
    , m_running(false)
    , m_isHidden(false)
    , m_font(nullptr)
    , m_fontBold(nullptr)
    , m_bgBrush(nullptr)
    , m_btnBrush(nullptr)
    , m_activeBrush(nullptr)
    , m_borderPen(nullptr)
    , m_ipcReady(false)
{
    m_clockText[0] = L'\0';
}

TaskbarWindow::~TaskbarWindow() {
    Shutdown();
}

bool TaskbarWindow::Create(HINSTANCE hInstance) {
    m_hInstance = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName  = kTaskbarClassName;
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kTaskbarClassName,
        L"LlaShell Taskbar",
        WS_POPUP,
        0, screenW - kTaskbarHeight, screenW, kTaskbarHeight,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    m_bgBrush     = CreateSolidBrush(RGB(24, 24, 36));
    m_btnBrush    = CreateSolidBrush(RGB(40, 40, 60));
    m_activeBrush = CreateSolidBrush(RGB(60, 60, 100));
    m_borderPen   = CreatePen(PS_SOLID, 1, RGB(60, 60, 80));
    m_font        = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    m_fontBold    = CreateFontW(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    RegisterGlobalHotkeys();

    m_running = true;
    m_fsDetectThread = std::thread(&TaskbarWindow::FullscreenDetectThread, this);

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    StringCchCopyW(nid.szTip, _countof(nid.szTip), L"LlaShell");
    Shell_NotifyIconW(NIM_ADD, &nid);

    InitIPC();

    return true;
}

void TaskbarWindow::Show() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_SHOW);
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

    UnregisterHotKey(m_hwnd, HOTKEY_WIN_E);
    UnregisterHotKey(m_hwnd, HOTKEY_WIN_D);

    if (m_fsDetectThread.joinable()) {
        m_fsDetectThread.join();
    }

    if (m_font) { DeleteObject(m_font); m_font = nullptr; }
    if (m_fontBold) { DeleteObject(m_fontBold); m_fontBold = nullptr; }
    if (m_bgBrush) { DeleteObject(m_bgBrush); m_bgBrush = nullptr; }
    if (m_btnBrush) { DeleteObject(m_btnBrush); m_btnBrush = nullptr; }
    if (m_activeBrush) { DeleteObject(m_activeBrush); m_activeBrush = nullptr; }
    if (m_borderPen) { DeleteObject(m_borderPen); m_borderPen = nullptr; }

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    UnregisterClassW(kTaskbarClassName, m_hInstance);
}

void TaskbarWindow::RegisterGlobalHotkeys() {
    RegisterHotKey(m_hwnd, HOTKEY_WIN_E, MOD_WIN, 0x45);
    RegisterHotKey(m_hwnd, HOTKEY_WIN_D, MOD_WIN, 0x44);
}

void TaskbarWindow::DrawTaskbar(HDC hdc, const RECT& rc) {
    FillRect(hdc, &rc, m_bgBrush);

    RECT startRc = { 0, 0, kStartBtnWidth, kTaskbarHeight };
    DrawStartButton(hdc, startRc);

    RECT btnRc = { kStartBtnWidth, 0, rc.right - kClockWidth - 10, kTaskbarHeight };
    DrawTaskButtons(hdc, btnRc);

    RECT trayRc = { rc.right - kClockWidth - 10, 0, rc.right - kClockWidth, kTaskbarHeight };
    DrawTrayArea(hdc, trayRc);

    RECT clockRc = { rc.right - kClockWidth, 0, rc.right, kTaskbarHeight };
    DrawClock(hdc, clockRc);
}

void TaskbarWindow::DrawStartButton(HDC hdc, const RECT& rc) {
    HBRUSH brush = m_btnBrush;
    FillRect(hdc, &rc, brush);

    HPEN oldPen = (HPEN)SelectObject(hdc, m_borderPen);
    MoveToEx(hdc, rc.right, rc.top, nullptr);
    LineTo(hdc, rc.right, rc.bottom);
    SelectObject(hdc, oldPen);

    HFONT oldFont = (HFONT)SelectObject(hdc, m_fontBold);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(180, 180, 220));
    DrawTextW(hdc, L"开始", -1, const_cast<RECT*>(&rc),
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

void TaskbarWindow::DrawClock(HDC hdc, const RECT& rc) {
    FillRect(hdc, &rc, m_bgBrush);

    HPEN oldPen = (HPEN)SelectObject(hdc, m_borderPen);
    MoveToEx(hdc, rc.left, rc.top, nullptr);
    LineTo(hdc, rc.left, rc.bottom);
    SelectObject(hdc, oldPen);

    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t timeBuf[32], dateBuf[32];
    StringCchPrintfW(timeBuf, _countof(timeBuf), L"%02d:%02d", st.wHour, st.wMinute);
    StringCchPrintfW(dateBuf, _countof(dateBuf), L"%d/%d/%d",
        st.wYear, st.wMonth, st.wDay);

    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(200, 200, 200));

    RECT timeRc = { rc.left, rc.top, rc.right, rc.top + kTaskbarHeight / 2 };
    RECT dateRc = { rc.left, rc.top + kTaskbarHeight / 2, rc.right, rc.bottom };

    DrawTextW(hdc, timeBuf, -1, &timeRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, m_font);

    SetTextColor(hdc, RGB(160, 160, 180));
    DrawTextW(hdc, dateBuf, -1, &dateRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

void TaskbarWindow::DrawTaskButtons(HDC hdc, const RECT& rc) {
    std::lock_guard<std::mutex> lock(m_btnMutex);

    int x = rc.left + 4;
    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);
    SetBkMode(hdc, TRANSPARENT);

    for (auto& btn : m_buttons) {
        RECT btnRc = { x, 5, x + kButtonWidth, 5 + kButtonHeight };
        btn.rect = btnRc;

        HBRUSH brush = btn.active ? m_activeBrush : m_btnBrush;
        FillRect(hdc, &btnRc, brush);

        SetTextColor(hdc, btn.active ? RGB(255, 255, 255) : RGB(180, 180, 200));
        RECT textRc = { btnRc.left + 8, btnRc.top, btnRc.right - 4, btnRc.bottom };
        DrawTextW(hdc, btn.title.c_str(), -1, &textRc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        x += kButtonWidth + 2;
        if (x > rc.right) break;
    }

    SelectObject(hdc, oldFont);
}

void TaskbarWindow::DrawTrayArea(HDC hdc, const RECT& rc) {
    FillRect(hdc, &rc, m_bgBrush);

    HPEN oldPen = (HPEN)SelectObject(hdc, m_borderPen);
    MoveToEx(hdc, rc.left, rc.top, nullptr);
    LineTo(hdc, rc.left, rc.bottom);
    SelectObject(hdc, oldPen);
}

void TaskbarWindow::HandleClick(int x, int y) {
    if (x < kStartBtnWidth) {
        POINT pt = { x, y };
        ClientToScreen(m_hwnd, &pt);
        ShowStartMenu(pt.x, pt.y);
        return;
    }

    std::lock_guard<std::mutex> lock(m_btnMutex);
    for (const auto& btn : m_buttons) {
        if (x >= btn.rect.left && x < btn.rect.right &&
            y >= btn.rect.top && y < btn.rect.bottom) {
            HWND target = reinterpret_cast<HWND>(btn.hwnd);
            if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
            SetForegroundWindow(target);
            break;
        }
    }
}

void TaskbarWindow::ShowStartMenu(int x, int y) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 1, L"文件管理器");
    AppendMenuW(hMenu, MF_STRING, 2, L"命令提示符");
    AppendMenuW(hMenu, MF_STRING, 3, L"任务管理器");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 4, L"设置");
    AppendMenuW(hMenu, MF_STRING, 5, L"关于 LlaShell");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 99, L"退出 LlaShell");

    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                             x, y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);

    switch (cmd) {
    case 1:
        ShellExecuteW(m_hwnd, L"open", L"MainUI.exe", nullptr, nullptr, SW_SHOW);
        break;
    case 2:
        ShellExecuteW(m_hwnd, L"open", L"cmd.exe", nullptr, nullptr, SW_SHOW);
        break;
    case 3:
        ShellExecuteW(m_hwnd, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOW);
        break;
    case 99:
        PostQuitMessage(0);
        break;
    }
}

void TaskbarWindow::FullscreenDetectThread() {
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        HWND fg = GetForegroundWindow();
        if (!fg || fg == m_hwnd) continue;

        RECT appRc, screenRc;
        GetWindowRect(fg, &appRc);
        screenRc = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };

        bool isFullscreen = (appRc.left <= 0 && appRc.top <= 0 &&
                             appRc.right >= screenRc.right &&
                             appRc.bottom >= screenRc.bottom);

        wchar_t cls[64];
        GetClassNameW(fg, cls, _countof(cls));
        if (wcscmp(cls, kTaskbarClassName) == 0 || wcscmp(cls, L"LlaShell_Desktop") == 0)
            isFullscreen = false;

        if (isFullscreen && !m_isHidden) {
            m_isHidden = true;
            PostMessage(m_hwnd, WM_FULLSCREEN_CHECK, 1, 0);
            if (m_ipcReady.load(std::memory_order_relaxed)) {
                IPCMessageHeader hdr{};
                hdr.sourcePid = GetCurrentProcessId();
                hdr.type = IPCMessageType::FullscreenDetected;
                hdr.dataSize = 0;
                IPC_Broadcast(IPC::MsgType::Application, &hdr, sizeof(hdr));
            }
        } else if (!isFullscreen && m_isHidden) {
            m_isHidden = false;
            PostMessage(m_hwnd, WM_FULLSCREEN_CHECK, 0, 0);
            if (m_ipcReady.load(std::memory_order_relaxed)) {
                IPCMessageHeader hdr{};
                hdr.sourcePid = GetCurrentProcessId();
                hdr.type = IPCMessageType::FullscreenExited;
                hdr.dataSize = 0;
                IPC_Broadcast(IPC::MsgType::Application, &hdr, sizeof(hdr));
            }
        }
    }
}

void TaskbarWindow::InitIPC() {
    IPC::IPCConfig cfg = IPC::DefaultConfig();
    cfg.processName  = L"Taskbar";
    cfg.onMessage    = OnIPCMessage;
    cfg.onConnected  = OnPeerConnected;
    cfg.onDisconnected = OnPeerDisconnected;
    cfg.userData     = this;

    if (!IsSuccess(IPC_Initialize(&cfg))) return;

    if (!IsSuccess(IPC_StartServer(L"Taskbar"))) {
        IPC_Shutdown();
        return;
    }

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

void TaskbarWindow::OnIPCMessage(IPC::SessionId /*from*/, uint16_t msgType,
                                 const void* data, uint32_t size, void* userData) {
    auto* self = static_cast<TaskbarWindow*>(userData);
    if (msgType != static_cast<uint16_t>(IPC::MsgType::Application)) return;
    if (!data || size < sizeof(IPCMessageHeader)) return;

    const auto* hdr = static_cast<const IPCMessageHeader*>(data);
    const void* payload = static_cast<const uint8_t*>(data) + sizeof(IPCMessageHeader);
    uint32_t payloadSize = size - sizeof(IPCMessageHeader);

    switch (hdr->type) {
    case IPCMessageType::MainUIWindowCreated:
        if (payloadSize >= sizeof(WindowNotification)) {
            const auto* wn = static_cast<const WindowNotification*>(payload);
            self->AddTaskButton(wn->windowPid, wn->hwnd, wn->title);
        }
        break;
    case IPCMessageType::MainUIWindowDestroyed:
        if (payloadSize >= sizeof(WindowNotification)) {
            const auto* wn = static_cast<const WindowNotification*>(payload);
            self->RemoveTaskButton(wn->hwnd);
        }
        break;
    case IPCMessageType::MainUITabChanged:
        if (payloadSize >= sizeof(WindowNotification)) {
            const auto* wn = static_cast<const WindowNotification*>(payload);
            self->UpdateTaskButtonTitle(wn->hwnd, wn->title);
        }
        break;
    default:
        break;
    }
}

void TaskbarWindow::OnPeerConnected(IPC::SessionId /*peer*/, IPC::ProcessId /*pid*/, void* /*userData*/) {
}

void TaskbarWindow::OnPeerDisconnected(IPC::SessionId /*peer*/, void* /*userData*/) {
}

void TaskbarWindow::AddTaskButton(uint32_t pid, uint64_t hwnd, const wchar_t* title) {
    std::lock_guard<std::mutex> lock(m_btnMutex);
    for (const auto& btn : m_buttons) {
        if (btn.hwnd == hwnd) return;
    }
    TaskButton btn{};
    btn.pid = pid;
    btn.hwnd = hwnd;
    btn.title = title ? title : L"";
    btn.active = false;
    m_buttons.push_back(btn);
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void TaskbarWindow::RemoveTaskButton(uint64_t hwnd) {
    std::lock_guard<std::mutex> lock(m_btnMutex);
    for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it) {
        if (it->hwnd == hwnd) {
            m_buttons.erase(it);
            break;
        }
    }
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void TaskbarWindow::UpdateTaskButtonTitle(uint64_t hwnd, const wchar_t* title) {
    std::lock_guard<std::mutex> lock(m_btnMutex);
    for (auto& btn : m_buttons) {
        if (btn.hwnd == hwnd) {
            btn.title = title ? title : L"";
            break;
        }
    }
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

LRESULT CALLBACK TaskbarWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TaskbarWindow* self = reinterpret_cast<TaskbarWindow*>(
        GetWindowLongPtrW(hWnd, GWLP_USERDATA));

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
    case WM_TIMER:
        if (self) InvalidateRect(hWnd, nullptr, TRUE);
        return 0;
    case WM_HOTKEY:
        if (wParam == HOTKEY_WIN_E) {
            ShellExecuteW(hWnd, L"open", L"MainUI.exe", nullptr, nullptr, SW_SHOW);
        } else if (wParam == HOTKEY_WIN_D) {
            if (self) {
                HWND fg = GetForegroundWindow();
                if (fg != self->m_hwnd) {
                    ShowWindow(fg, SW_MINIMIZE);
                }
            }
        }
        return 0;
    case WM_FULLSCREEN_CHECK:
        if (self) {
            if (wParam == 1) {
                ShowWindow(hWnd, SW_HIDE);
            } else {
                ShowWindow(hWnd, SW_SHOW);
                SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
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

#include "desktop.h"
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>

#pragma comment(lib, "shell32.lib")

static constexpr wchar_t kDesktopClassName[] = L"LlaShell_Desktop";
static constexpr int kIconSize       = 32;
static constexpr int kIconSpacingX   = 75;
static constexpr int kIconSpacingY   = 75;
static constexpr int kIconPadX       = 12;
static constexpr int kIconPadY       = 12;
static constexpr int kTextHeight     = 32;

namespace LlaShell {

DesktopWindow::DesktopWindow()
    : m_hwnd(nullptr)
    , m_hInstance(nullptr)
    , m_running(false)
    , m_iconSize(kIconSize)
    , m_iconSpacingX(kIconSpacingX)
    , m_iconSpacingY(kIconSpacingY)
    , m_font(nullptr)
    , m_bgBrush(nullptr)
    , m_mainUISession(IPC::kInvalidSession)
    , m_ipcReady(false)
{
}

DesktopWindow::~DesktopWindow() {
    Shutdown();
}

bool DesktopWindow::Create(HINSTANCE hInstance) {
    m_hInstance = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName  = kDesktopClassName;
    wc.hbrBackground  = nullptr;
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    m_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kDesktopClassName,
        L"LlaShell Desktop",
        WS_POPUP,
        0, 0, screenW, screenH,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    m_bgBrush = CreateSolidBrush(RGB(30, 30, 46));
    m_font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    RegisterTouchWindow(m_hwnd, 0);

    m_running = true;
    m_worker = std::thread(&DesktopWindow::WorkerThread, this);

    ScanDesktopFolder();
    InitIPC();

    SetWindowPos(m_hwnd, HWND_BOTTOM, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);

    return true;
}

void DesktopWindow::Show() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);
    }
}

void DesktopWindow::Shutdown() {
    ShutdownIPC();

    m_running = false;

    if (m_worker.joinable()) {
        m_worker.join();
    }

    if (m_font) { DeleteObject(m_font); m_font = nullptr; }
    if (m_bgBrush) { DeleteObject(m_bgBrush); m_bgBrush = nullptr; }

    {
        std::lock_guard<std::mutex> lock(m_iconMutex);
        for (auto& icon : m_icons) {
            if (icon.hIcon) DestroyIcon(icon.hIcon);
        }
        m_icons.clear();
    }

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    UnregisterClassW(kDesktopClassName, m_hInstance);
}

void DesktopWindow::ScanDesktopFolder() {
    wchar_t desktopPath[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktopPath) != S_OK) {
        if (SHGetFolderPathW(nullptr, CSIDL_DESKTOP, nullptr, 0, desktopPath) != S_OK)
            return;
    }

    std::vector<DesktopIcon> newIcons;
    WIN32_FIND_DATAW fd;
    std::wstring search = std::wstring(desktopPath) + L"\\*";
    HANDLE hFind = FindFirstFileW(search.c_str(), &fd);

    if (hFind == INVALID_HANDLE_VALUE) return;

    int col = 0, row = 0;
    int maxRows = (GetSystemMetrics(SM_CYSCREEN) - 20) / kIconSpacingY;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        DesktopIcon icon;
        icon.name = fd.cFileName;
        icon.fullPath = std::wstring(desktopPath) + L"\\" + fd.cFileName;
        icon.isDirectory = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        icon.gridX = col;
        icon.gridY = row;
        icon.hIcon = nullptr;

        SHFILEINFOW sfi{};
        SHGetFileInfoW(icon.fullPath.c_str(), 0, &sfi, sizeof(sfi),
            SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES);
        icon.hIcon = sfi.hIcon;

        newIcons.push_back(icon);

        row++;
        if (row >= maxRows) {
            row = 0;
            col++;
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    std::lock_guard<std::mutex> lock(m_iconMutex);
    for (auto& old : m_icons) {
        if (old.hIcon) DestroyIcon(old.hIcon);
    }
    m_icons = std::move(newIcons);

    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, TRUE);
}

void DesktopWindow::RenderIcons(HDC hdc, const RECT& rc) {
    HBRUSH bgBrush = CreateSolidBrush(RGB(30, 30, 46));
    FillRect(hdc, &rc, bgBrush);
    DeleteObject(bgBrush);

    std::lock_guard<std::mutex> lock(m_iconMutex);

    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(220, 220, 220));

    for (const auto& icon : m_icons) {
        int x = kIconPadX + icon.gridX * kIconSpacingX;
        int y = kIconPadY + icon.gridY * kIconSpacingY;

        if (icon.hIcon) {
            DrawIconEx(hdc, x + (kIconSpacingX - m_iconSize) / 2, y,
                       icon.hIcon, m_iconSize, m_iconSize, 0, nullptr, DI_NORMAL);
        }

        RECT textRc = { x, y + m_iconSize + 2, x + kIconSpacingX, y + kIconSpacingY };
        DrawTextW(hdc, icon.name.c_str(), -1, &textRc,
            DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
    }

    SelectObject(hdc, oldFont);
}

void DesktopWindow::HandleDoubleClick(int x, int y) {
    std::lock_guard<std::mutex> lock(m_iconMutex);
    for (const auto& icon : m_icons) {
        int ix = kIconPadX + icon.gridX * kIconSpacingX;
        int iy = kIconPadY + icon.gridY * kIconSpacingY;

        if (x >= ix && x < ix + kIconSpacingX &&
            y >= iy && y < iy + kIconSpacingY) {
            if (icon.isDirectory) {
                if (m_ipcReady.load(std::memory_order_relaxed) &&
                    m_mainUISession != IPC::kInvalidSession) {
                    IPCMessageHeader hdr{};
                    hdr.sourcePid = GetCurrentProcessId();
                    hdr.type = IPCMessageType::DesktopFolderDoubleClick;
                    FolderOpenRequest req{};
                    wcsncpy_s(req.path, icon.fullPath.c_str(), _countof(req.path) - 1);
                    hdr.dataSize = sizeof(req);
                    std::vector<uint8_t> payload(sizeof(hdr) + sizeof(req));
                    memcpy(payload.data(), &hdr, sizeof(hdr));
                    memcpy(payload.data() + sizeof(hdr), &req, sizeof(req));
                    IPC_Send(m_mainUISession, IPC::MsgType::Application,
                             payload.data(), static_cast<uint32_t>(payload.size()));
                } else {
                    ShellExecuteW(m_hwnd, L"open", icon.fullPath.c_str(),
                        nullptr, nullptr, SW_SHOW);
                }
            } else {
                ShellExecuteW(m_hwnd, nullptr, icon.fullPath.c_str(),
                    nullptr, nullptr, SW_SHOW);
            }
            break;
        }
    }
}

void DesktopWindow::HandleRightClick(int x, int y) {
    POINT pt = { x, y };
    ClientToScreen(m_hwnd, &pt);

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 1, L"刷新桌面");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 2, L"新建文件夹");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 3, L"显示设置");
    AppendMenuW(hMenu, MF_STRING, 4, L"关于 LlaShell");

    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                             pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);

    switch (cmd) {
    case 1:
        ScanDesktopFolder();
        break;
    case 2: {
        wchar_t desktopPath[MAX_PATH];
        if (SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktopPath) == S_OK) {
            std::wstring newPath = std::wstring(desktopPath) + L"\\新建文件夹";
            CreateDirectoryW(newPath.c_str(), nullptr);
            ScanDesktopFolder();
        }
        break;
    }
    default:
        break;
    }
}

void DesktopWindow::WorkerThread() {
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (m_running) {
            ScanDesktopFolder();
        }
    }
}

void DesktopWindow::InitIPC() {
    IPC::IPCConfig cfg = IPC::DefaultConfig();
    cfg.processName  = L"Desktop";
    cfg.onMessage    = OnIPCMessage;
    cfg.onConnected  = OnPeerConnected;
    cfg.onDisconnected = OnPeerDisconnected;
    cfg.userData     = this;

    if (!IsSuccess(IPC_Initialize(&cfg))) return;

    if (!IsSuccess(IPC_StartServer(L"Desktop"))) {
        IPC_Shutdown();
        return;
    }

    m_ipcReady.store(true, std::memory_order_relaxed);

    IPC::SessionId session = IPC::kInvalidSession;
    if (IsSuccess(IPC_Connect(L"MainUI", &session))) {
        m_mainUISession = session;
    }
}

void DesktopWindow::ShutdownIPC() {
    if (m_ipcReady.load(std::memory_order_relaxed)) {
        IPC_Shutdown();
        m_ipcReady.store(false, std::memory_order_relaxed);
        m_mainUISession = IPC::kInvalidSession;
    }
}

void DesktopWindow::OnIPCMessage(IPC::SessionId /*from*/, uint16_t /*msgType*/,
                                 const void* /*data*/, uint32_t /*size*/, void* /*userData*/) {
}

void DesktopWindow::OnPeerConnected(IPC::SessionId peer, IPC::ProcessId /*pid*/, void* userData) {
    auto* self = static_cast<DesktopWindow*>(userData);
    if (!self->m_ipcReady.load(std::memory_order_relaxed)) return;
    self->m_mainUISession = peer;
}

void DesktopWindow::OnPeerDisconnected(IPC::SessionId peer, void* userData) {
    auto* self = static_cast<DesktopWindow*>(userData);
    if (peer == self->m_mainUISession) {
        self->m_mainUISession = IPC::kInvalidSession;
    }
}

LRESULT CALLBACK DesktopWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DesktopWindow* self = reinterpret_cast<DesktopWindow*>(
        GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (self) self->RenderIcons(hdc, ps.rcPaint);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        if (self) self->HandleDoubleClick(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_RBUTTONUP:
        if (self) self->HandleRightClick(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_DISPLAYCHANGE:
        if (self && self->m_hwnd) {
            int w = GetSystemMetrics(SM_CXSCREEN);
            int h = GetSystemMetrics(SM_CYSCREEN);
            SetWindowPos(self->m_hwnd, HWND_BOTTOM, 0, 0, w, h,
                SWP_NOMOVE | SWP_NOACTIVATE);
            self->ScanDesktopFolder();
        }
        return 0;
    case WM_TOUCH:
        if (self && LOWORD(wParam) > 0) {
            HTOUCHINPUT hTouch = reinterpret_cast<HTOUCHINPUT>(lParam);
            TOUCHINPUT ti{};
            if (GetTouchInputInfo(hTouch, 1, &ti, sizeof(TOUCHINPUT))) {
                POINT pt = { TOUCH_COORD_TO_PIXEL(ti.x), TOUCH_COORD_TO_PIXEL(ti.y) };
                ScreenToClient(self->m_hwnd, &pt);
                if (ti.dwFlags & TOUCHEVENTF_PRIMARY) {
                    if (ti.dwFlags & TOUCHEVENTF_DOWN) {
                        // long-press detection could be added here
                    } else if (ti.dwFlags & TOUCHEVENTF_UP) {
                        self->HandleDoubleClick(pt.x, pt.y);
                    }
                }
                CloseTouchInputHandle(hTouch);
            }
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

} // namespace LlaShell

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    LlaShell::DesktopWindow desktop;
    if (!desktop.Create(hInstance)) return 1;
    desktop.Show();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    desktop.Shutdown();
    CoUninitialize();
    return 0;
}

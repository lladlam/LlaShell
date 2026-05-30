#include "mainui.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <algorithm>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

static constexpr wchar_t kMainUIClassName[] = L"LlaShell_MainUI";

enum ToolbarCmd {
    CMD_BACK = 1,
    CMD_FORWARD,
    CMD_UP,
    CMD_REFRESH,
    CMD_NEW_TAB,
    CMD_CLOSE_TAB,
    CMD_HOME,
};

namespace LlaShell {

MainWindow::MainWindow()
    : m_hwnd(nullptr)
    , m_toolbar(nullptr)
    , m_statusBar(nullptr)
    , m_tabControl(nullptr)
    , m_treeView(nullptr)
    , m_hInstance(nullptr)
    , m_activeTab(-1)
    , m_historyIndex(-1)
    , m_running(false)
    , m_font(nullptr)
    , m_bgBrush(nullptr)
    , m_panelBrush(nullptr)
    , m_folderIcon(nullptr)
    , m_fileIcon(nullptr)
    , m_taskbarSession(IPC::kInvalidSession)
    , m_desktopSession(IPC::kInvalidSession)
    , m_shellHostSession(IPC::kInvalidSession)
    , m_mediaParserSession(IPC::kInvalidSession)
    , m_ipcReady(false)
    , m_nextThumbnailRequestId(1)
{
}

MainWindow::~MainWindow() {
    Shutdown();
}

bool MainWindow::Create(HINSTANCE hInstance, const wchar_t* initialPath) {
    m_hInstance = hInstance;

    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(icex);
    icex.dwICC  = ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_TREEVIEW_CLASSES |
                  ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName  = kMainUIClassName;
    wc.hIcon          = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    m_bgBrush    = CreateSolidBrush(RGB(30, 30, 46));
    m_panelBrush = CreateSolidBrush(RGB(24, 24, 36));
    m_font       = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    SHFILEINFOW sfi{};
    SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
        SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    m_folderIcon = sfi.hIcon;
    SHGetFileInfoW(L"file", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
        SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    m_fileIcon = sfi.hIcon;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = screenW * 3 / 4;
    int winH = screenH * 3 / 4;
    int winX = (screenW - winW) / 2;
    int winY = (screenH - winH) / 2;

    m_hwnd = CreateWindowExW(
        0,
        kMainUIClassName,
        L"LlaShell",
        WS_OVERLAPPEDWINDOW,
        winX, winY, winW, winH,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    RegisterTouchWindow(m_hwnd, 0);
    ChangeWindowMessageFilterEx(m_hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(m_hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(m_hwnd, 0x0049, MSGFLT_ALLOW, nullptr); // WM_COPYGLOBALDATA

    CreateControls();

    const wchar_t* startPath = initialPath ? initialPath : L"C:\\";
    AddTab(startPath);
    NavigateTo(startPath);

    m_running = true;
    m_scanThread = std::thread(&MainWindow::ScanWorkerThread, this);

    InitIPC();
    NotifyTaskbarWindowCreated();

    return true;
}

void MainWindow::Show(int nCmdShow) {
    if (m_hwnd) {
        ShowWindow(m_hwnd, nCmdShow);
        UpdateWindow(m_hwnd);
    }
}

void MainWindow::Shutdown() {
    NotifyTaskbarWindowDestroyed();
    ShutdownIPC();

    m_running = false;

    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }

    if (m_font) { DeleteObject(m_font); m_font = nullptr; }
    if (m_bgBrush) { DeleteObject(m_bgBrush); m_bgBrush = nullptr; }
    if (m_panelBrush) { DeleteObject(m_panelBrush); m_panelBrush = nullptr; }
    if (m_folderIcon) { DestroyIcon(m_folderIcon); m_folderIcon = nullptr; }
    if (m_fileIcon) { DestroyIcon(m_fileIcon); m_fileIcon = nullptr; }

    for (auto& tab : m_tabs) {
        for (auto& e : tab.entries) {
            if (e.icon) DestroyIcon(e.icon);
        }
    }
    m_tabs.clear();

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    UnregisterClassW(kMainUIClassName, m_hInstance);
}

void MainWindow::CreateControls() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);

    m_tabControl = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | TCS_FIXEDWIDTH,
        0, 0, rc.right, kTabHeight,
        m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_tabControl, WM_SETFONT, (WPARAM)m_font, TRUE);

    m_treeView = CreateWindowExW(0, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT,
        0, kTabHeight, kTreeViewWidth, rc.bottom - kTabHeight - kStatusBarHeight,
        m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_treeView, WM_SETFONT, (WPARAM)m_font, TRUE);

    CreateToolbar();
    CreateStatusBar();
    PopulateTreeView();
}

void MainWindow::CreateToolbar() {
    m_toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, L"",
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS,
        0, 0, 0, 0,
        m_hwnd, nullptr, m_hInstance, nullptr);

    SendMessageW(m_toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

    TBBUTTON buttons[] = {
        { 0, CMD_BACK,     TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"后退" },
        { 1, CMD_FORWARD,  TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"前进" },
        { 2, CMD_UP,       TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"上级" },
        { 0, 0,            0,               BTNS_SEP,    {0}, 0, 0 },
        { 3, CMD_REFRESH,  TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"刷新" },
        { 0, 0,            0,               BTNS_SEP,    {0}, 0, 0 },
        { 4, CMD_NEW_TAB,  TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"新标签" },
    };

    SendMessageW(m_toolbar, TB_ADDBUTTONSW, _countof(buttons), (LPARAM)buttons);
    SendMessageW(m_toolbar, TB_AUTOSIZE, 0, 0);
}

void MainWindow::CreateStatusBar() {
    m_statusBar = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0,
        m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_statusBar, WM_SETFONT, (WPARAM)m_font, TRUE);
}

void MainWindow::CreateTreeView() {
    TVINSERTSTRUCTW tvis{};
    tvis.hParent = TVI_ROOT;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
    tvis.item.pszText = const_cast<LPWSTR>(L"此电脑");
    tvis.item.lParam = 0;
    TreeView_InsertItem(m_treeView, &tvis);
}

void MainWindow::PopulateTreeView() {
    TreeView_DeleteAllItems(m_treeView);

    TVINSERTSTRUCTW tvis{};
    tvis.hParent = TVI_ROOT;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
    tvis.item.pszText = const_cast<LPWSTR>(L"此电脑");
    tvis.item.lParam = 0;
    HTREEITEM root = TreeView_InsertItem(m_treeView, &tvis);

    wchar_t drives[256];
    if (GetLogicalDriveStringsW(256, drives)) {
        wchar_t* drv = drives;
        while (*drv) {
            wchar_t volName[64] = {};
            GetVolumeInformationW(drv, volName, _countof(volName),
                nullptr, nullptr, nullptr, nullptr, 0);

            wchar_t label[128];
            if (volName[0])
                StringCchPrintfW(label, _countof(label), L"%s (%c:)", volName, drv[0]);
            else
                StringCchPrintfW(label, _countof(label), L"本地磁盘 (%c:)", drv[0]);

            tvis.hParent = root;
            tvis.item.pszText = label;
            tvis.item.lParam = (LPARAM)_wcsdup(drv);
            TreeView_InsertItem(m_treeView, &tvis);

            drv += wcslen(drv) + 1;
        }
    }

    TreeView_Expand(m_treeView, root, TVE_EXPAND);
}

void MainWindow::AddTab(const wchar_t* path) {
    TabInfo tab;
    tab.path = path;

    wchar_t name[MAX_PATH];
    StringCchCopyW(name, _countof(name), path);
    PathStripPathW(name);
    if (name[0] == L'\0') {
        tab.title = path;
    } else {
        tab.title = name;
    }

    std::lock_guard<std::mutex> lock(m_tabMutex);
    m_tabs.push_back(std::move(tab));
    m_activeTab = static_cast<int>(m_tabs.size()) - 1;

    TCITEMW tie{};
    tie.mask = TCIF_TEXT;
    tie.pszText = const_cast<LPWSTR>(m_tabs[m_activeTab].title.c_str());
    SendMessageW(m_tabControl, TCM_INSERTITEMW, m_activeTab, (LPARAM)&tie);
    SendMessageW(m_tabControl, TCM_SETCURSEL, m_activeTab, 0);
}

void MainWindow::NavigateTo(const wchar_t* path, int tabIndex) {
    int targetTab = (tabIndex >= 0) ? tabIndex : m_activeTab;
    if (targetTab < 0 || targetTab >= static_cast<int>(m_tabs.size())) return;

    {
        std::lock_guard<std::mutex> lock(m_tabMutex);
        m_tabs[targetTab].path = path;

        wchar_t name[MAX_PATH];
        StringCchCopyW(name, _countof(name), path);
        PathStripPathW(name);
        m_tabs[targetTab].title = name[0] ? name : path;

        TCITEMW tie{};
        tie.mask = TCIF_TEXT;
        tie.pszText = const_cast<LPWSTR>(m_tabs[targetTab].title.c_str());
        SendMessageW(m_tabControl, TCM_SETITEMW, targetTab, (LPARAM)&tie);

        for (auto& e : m_tabs[targetTab].entries) {
            if (e.icon) DestroyIcon(e.icon);
        }
        m_tabs[targetTab].entries.clear();
    }

    if (m_historyIndex < static_cast<int>(m_history.size()) - 1) {
        m_history.erase(m_history.begin() + m_historyIndex + 1, m_history.end());
    }
    m_history.push_back(path);
    m_historyIndex = static_cast<int>(m_history.size()) - 1;

    ScanDirectoryAsync(path, targetTab);

    wchar_t title[512];
    StringCchPrintfW(title, _countof(title), L"%s - LlaShell",
        m_tabs[targetTab].title.c_str());
    SetWindowTextW(m_hwnd, title);
}

void MainWindow::ScanDirectoryAsync(const wchar_t* path, int tabIndex) {
    std::lock_guard<std::mutex> lock(m_scanMutex);
    m_scanQueue.push({ path, tabIndex });
}

void MainWindow::ScanWorkerThread() {
    while (m_running) {
        ScanJob job;
        bool hasJob = false;

        {
            std::lock_guard<std::mutex> lock(m_scanMutex);
            if (!m_scanQueue.empty()) {
                job = m_scanQueue.front();
                m_scanQueue.pop();
                hasJob = true;
            }
        }

        if (!hasJob) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        std::vector<FileEntry> entries;
        WIN32_FIND_DATAW fd;
        std::wstring search = job.path + L"\\*";
        HANDLE hFind = FindFirstFileW(search.c_str(), &fd);

        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                    continue;

                FileEntry entry;
                entry.name = fd.cFileName;
                entry.fullPath = job.path + L"\\" + fd.cFileName;
                entry.isDirectory = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                entry.lastModified = fd.ftLastWriteTime;

                if (entry.isDirectory) {
                    entry.size = 0;
                    entry.icon = m_folderIcon ? m_folderIcon : nullptr;
                } else {
                    LARGE_INTEGER li;
                    li.LowPart = fd.nFileSizeLow;
                    li.HighPart = fd.nFileSizeHigh;
                    entry.size = li.QuadPart;

                    SHFILEINFOW sfi{};
                    SHGetFileInfoW(entry.fullPath.c_str(), 0, &sfi, sizeof(sfi),
                        SHGFI_ICON | SHGFI_SMALLICON);
                    entry.icon = sfi.hIcon ? sfi.hIcon : (m_fileIcon ? m_fileIcon : nullptr);
                }

                entries.push_back(entry);
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }

        std::sort(entries.begin(), entries.end(), [](const FileEntry& a, const FileEntry& b) {
            if (a.isDirectory != b.isDirectory) return a.isDirectory;
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        });

        {
            std::lock_guard<std::mutex> lock(m_tabMutex);
            if (job.tabIndex >= 0 && job.tabIndex < static_cast<int>(m_tabs.size())) {
                for (auto& e : m_tabs[job.tabIndex].entries) {
                    if (e.icon && e.icon != m_folderIcon && e.icon != m_fileIcon)
                        DestroyIcon(e.icon);
                }
                m_tabs[job.tabIndex].entries = std::move(entries);
            }
        }

        PostMessage(m_hwnd, WM_USER + 1, job.tabIndex, 0);
    }
}

void MainWindow::OnScanComplete(int tabIndex) {
    std::lock_guard<std::mutex> lock(m_tabMutex);
    if (tabIndex < 0 || tabIndex >= static_cast<int>(m_tabs.size())) return;

    const auto& tab = m_tabs[tabIndex];
    wchar_t status[256];
    StringCchPrintfW(status, _countof(status), L"%d 个项目",
        static_cast<int>(tab.entries.size()));
    SendMessageW(m_statusBar, SB_SETTEXTW, 0, (LPARAM)status);
    InvalidateRect(m_hwnd, nullptr, TRUE);
    RequestThumbnailsForTab(tabIndex);
}

void MainWindow::HandleResize() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);

    int toolbarH = kToolbarHeight;

    MoveWindow(m_toolbar, 0, 0, rc.right, toolbarH, TRUE);
    MoveWindow(m_tabControl, 0, toolbarH, rc.right, kTabHeight, TRUE);
    MoveWindow(m_treeView, 0, toolbarH + kTabHeight, kTreeViewWidth,
               rc.bottom - toolbarH - kTabHeight - kStatusBarHeight, TRUE);
    MoveWindow(m_statusBar, 0, 0, 0, 0, TRUE);
    SendMessageW(m_statusBar, WM_SIZE, 0, 0);
}

void MainWindow::GoBack() {
    if (m_historyIndex > 0) {
        m_historyIndex--;
        NavigateTo(m_history[m_historyIndex].c_str());
    }
}

void MainWindow::GoForward() {
    if (m_historyIndex < static_cast<int>(m_history.size()) - 1) {
        m_historyIndex++;
        NavigateTo(m_history[m_historyIndex].c_str());
    }
}

void MainWindow::GoUp() {
    if (m_activeTab < 0 || m_activeTab >= static_cast<int>(m_tabs.size())) return;

    wchar_t parent[MAX_PATH];
    StringCchCopyW(parent, _countof(parent), m_tabs[m_activeTab].path.c_str());
    PathRemoveFileSpecW(parent);
    if (parent[0]) NavigateTo(parent);
}

void MainWindow::InitIPC() {
    IPC::IPCConfig cfg = IPC::DefaultConfig();
    cfg.processName  = L"MainUI";
    cfg.onMessage    = OnIPCMessage;
    cfg.onConnected  = OnPeerConnected;
    cfg.onDisconnected = OnPeerDisconnected;
    cfg.userData     = this;

    if (!IsSuccess(IPC_Initialize(&cfg))) return;

    if (!IsSuccess(IPC_StartServer(L"MainUI"))) {
        IPC_Shutdown();
        return;
    }

    m_ipcReady.store(true, std::memory_order_relaxed);

    IPC::SessionId sid = IPC::kInvalidSession;
    if (IsSuccess(IPC_Connect(L"Desktop", &sid))) m_desktopSession = sid;
    sid = IPC::kInvalidSession;
    if (IsSuccess(IPC_Connect(L"Taskbar", &sid))) m_taskbarSession = sid;
    sid = IPC::kInvalidSession;
    if (IsSuccess(IPC_Connect(L"ShellHost", &sid))) m_shellHostSession = sid;
    sid = IPC::kInvalidSession;
    if (IsSuccess(IPC_Connect(L"MediaParser", &sid))) m_mediaParserSession = sid;
}

void MainWindow::ShutdownIPC() {
    if (m_ipcReady.load(std::memory_order_relaxed)) {
        IPC_Shutdown();
        m_ipcReady.store(false, std::memory_order_relaxed);
        m_taskbarSession = IPC::kInvalidSession;
        m_desktopSession = IPC::kInvalidSession;
        m_shellHostSession = IPC::kInvalidSession;
        m_mediaParserSession = IPC::kInvalidSession;
    }
}

void MainWindow::NotifyTaskbarWindowCreated() {
    if (!m_ipcReady.load(std::memory_order_relaxed)) return;
    if (m_taskbarSession == IPC::kInvalidSession) return;

    IPCMessageHeader hdr{};
    hdr.sourcePid = GetCurrentProcessId();
    hdr.type = IPCMessageType::MainUIWindowCreated;

    WindowNotification wn{};
    wn.windowPid = GetCurrentProcessId();
    wn.hwnd = reinterpret_cast<uint64_t>(m_hwnd);
    GetWindowTextW(m_hwnd, wn.title, _countof(wn.title));

    hdr.dataSize = sizeof(wn);
    std::vector<uint8_t> payload(sizeof(hdr) + sizeof(wn));
    memcpy(payload.data(), &hdr, sizeof(hdr));
    memcpy(payload.data() + sizeof(hdr), &wn, sizeof(wn));
    IPC_Send(m_taskbarSession, IPC::MsgType::Application,
             payload.data(), static_cast<uint32_t>(payload.size()));
}

void MainWindow::NotifyTaskbarWindowDestroyed() {
    if (!m_ipcReady.load(std::memory_order_relaxed)) return;
    if (m_taskbarSession == IPC::kInvalidSession) return;

    IPCMessageHeader hdr{};
    hdr.sourcePid = GetCurrentProcessId();
    hdr.type = IPCMessageType::MainUIWindowDestroyed;

    WindowNotification wn{};
    wn.windowPid = GetCurrentProcessId();
    wn.hwnd = reinterpret_cast<uint64_t>(m_hwnd);

    hdr.dataSize = sizeof(wn);
    std::vector<uint8_t> payload(sizeof(hdr) + sizeof(wn));
    memcpy(payload.data(), &hdr, sizeof(hdr));
    memcpy(payload.data() + sizeof(hdr), &wn, sizeof(wn));
    IPC_Send(m_taskbarSession, IPC::MsgType::Application,
             payload.data(), static_cast<uint32_t>(payload.size()));
}

void MainWindow::NotifyTaskbarTabChanged() {
    if (!m_ipcReady.load(std::memory_order_relaxed)) return;
    if (m_taskbarSession == IPC::kInvalidSession) return;

    IPCMessageHeader hdr{};
    hdr.sourcePid = GetCurrentProcessId();
    hdr.type = IPCMessageType::MainUITabChanged;

    WindowNotification wn{};
    wn.windowPid = GetCurrentProcessId();
    wn.hwnd = reinterpret_cast<uint64_t>(m_hwnd);
    {
        std::lock_guard<std::mutex> lock(m_tabMutex);
        if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size()))
            wcsncpy_s(wn.title, m_tabs[m_activeTab].title.c_str(), _countof(wn.title) - 1);
    }

    hdr.dataSize = sizeof(wn);
    std::vector<uint8_t> payload(sizeof(hdr) + sizeof(wn));
    memcpy(payload.data(), &hdr, sizeof(hdr));
    memcpy(payload.data() + sizeof(hdr), &wn, sizeof(wn));
    IPC_Send(m_taskbarSession, IPC::MsgType::Application,
             payload.data(), static_cast<uint32_t>(payload.size()));
}

void MainWindow::OnIPCMessage(IPC::SessionId /*from*/, uint16_t msgType,
                              const void* data, uint32_t size, void* userData) {
    auto* self = static_cast<MainWindow*>(userData);
    if (msgType != static_cast<uint16_t>(IPC::MsgType::Application)) return;
    if (!data || size < sizeof(IPCMessageHeader)) return;

    const auto* hdr = static_cast<const IPCMessageHeader*>(data);
    const void* payload = static_cast<const uint8_t*>(data) + sizeof(IPCMessageHeader);
    uint32_t payloadSize = size - sizeof(IPCMessageHeader);

    switch (hdr->type) {
    case IPCMessageType::DesktopFolderDoubleClick:
        if (payloadSize >= sizeof(FolderOpenRequest)) {
            const auto* req = static_cast<const FolderOpenRequest*>(payload);
            if (req->path[0]) {
                self->AddTab(req->path);
                self->NavigateTo(req->path);
            }
        }
        break;
    default:
        break;
    }
}

void MainWindow::OnPeerConnected(IPC::SessionId peer, IPC::ProcessId pid, void* userData) {
    auto* self = static_cast<MainWindow*>(userData);
    if (pid == 0) return;
    wchar_t exePath[MAX_PATH] = {};
    DWORD pathLen = MAX_PATH;
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
        if (QueryFullProcessImageNameW(hProc, 0, exePath, &pathLen)) {
            const wchar_t* name = wcsrchr(exePath, L'\\');
            if (name) {
                name++;
                if (_wcsicmp(name, L"Taskbar.exe") == 0) self->m_taskbarSession = peer;
                else if (_wcsicmp(name, L"Desktop.exe") == 0) self->m_desktopSession = peer;
                else if (_wcsicmp(name, L"ShellHost.exe") == 0) self->m_shellHostSession = peer;
                else if (_wcsicmp(name, L"MediaParser.exe") == 0) self->m_mediaParserSession = peer;
            }
        }
        CloseHandle(hProc);
    }
}

void MainWindow::OnPeerDisconnected(IPC::SessionId peer, void* userData) {
    auto* self = static_cast<MainWindow*>(userData);
    if (peer == self->m_taskbarSession) self->m_taskbarSession = IPC::kInvalidSession;
    else if (peer == self->m_desktopSession) self->m_desktopSession = IPC::kInvalidSession;
    else if (peer == self->m_shellHostSession) self->m_shellHostSession = IPC::kInvalidSession;
    else if (peer == self->m_mediaParserSession) self->m_mediaParserSession = IPC::kInvalidSession;
}

void MainWindow::RequestContextMenu(HWND parentHwnd, int screenX, int screenY, const wchar_t* filePath) {
    if (!m_ipcReady.load(std::memory_order_relaxed)) return;
    if (m_shellHostSession == IPC::kInvalidSession) return;

    IPCMessageHeader hdr{};
    hdr.sourcePid = GetCurrentProcessId();
    hdr.type = IPCMessageType::RequestContextMenu;

    ContextMenuRequest req{};
    req.hwndParent = reinterpret_cast<uint64_t>(parentHwnd);
    req.screenX = screenX;
    req.screenY = screenY;
    wcsncpy_s(req.filePath, filePath, _countof(req.filePath) - 1);

    hdr.dataSize = sizeof(req);
    std::vector<uint8_t> payload(sizeof(hdr) + sizeof(req));
    memcpy(payload.data(), &hdr, sizeof(hdr));
    memcpy(payload.data() + sizeof(hdr), &req, sizeof(req));
    IPC_Send(m_shellHostSession, IPC::MsgType::Application,
             payload.data(), static_cast<uint32_t>(payload.size()));
}

void MainWindow::RequestThumbnailsForTab(int tabIndex) {
    if (!m_ipcReady.load(std::memory_order_relaxed)) return;
    if (m_mediaParserSession == IPC::kInvalidSession) return;

    std::lock_guard<std::mutex> lock(m_tabMutex);
    if (tabIndex < 0 || tabIndex >= static_cast<int>(m_tabs.size())) return;

    for (const auto& entry : m_tabs[tabIndex].entries) {
        if (entry.isDirectory) continue;

        wchar_t ext[MAX_PATH];
        wcsncpy_s(ext, entry.fullPath.c_str(), _countof(ext) - 1);
        wchar_t* dot = wcsrchr(ext, L'.');
        if (!dot) continue;

        bool isMedia = (_wcsicmp(dot, L".jpg") == 0 || _wcsicmp(dot, L".jpeg") == 0 ||
                        _wcsicmp(dot, L".png") == 0 || _wcsicmp(dot, L".bmp") == 0 ||
                        _wcsicmp(dot, L".gif") == 0 || _wcsicmp(dot, L".mp4") == 0 ||
                        _wcsicmp(dot, L".avi") == 0 || _wcsicmp(dot, L".mkv") == 0);
        if (!isMedia) continue;

        uint32_t reqId = m_nextThumbnailRequestId.fetch_add(1, std::memory_order_relaxed);

        IPCMessageHeader hdr{};
        hdr.sourcePid = GetCurrentProcessId();
        hdr.type = IPCMessageType::RequestThumbnail;

        ThumbnailRequest tr{};
        tr.requestId = reqId;
        wcsncpy_s(tr.filePath, entry.fullPath.c_str(), _countof(tr.filePath) - 1);
        tr.maxWidth = 64;
        tr.maxHeight = 64;

        hdr.dataSize = sizeof(tr);
        std::vector<uint8_t> payload(sizeof(hdr) + sizeof(tr));
        memcpy(payload.data(), &hdr, sizeof(hdr));
        memcpy(payload.data() + sizeof(hdr), &tr, sizeof(tr));
        IPC_Send(m_mediaParserSession, IPC::MsgType::Application,
                 payload.data(), static_cast<uint32_t>(payload.size()));
    }
}

void MainWindow::HandleDoubleClick(int index) {
    std::wstring targetPath;
    bool isDir = false;
    {
        std::lock_guard<std::mutex> lock(m_tabMutex);
        if (m_activeTab < 0 || m_activeTab >= static_cast<int>(m_tabs.size())) return;
        if (index < 0 || index >= static_cast<int>(m_tabs[m_activeTab].entries.size())) return;

        const auto& entry = m_tabs[m_activeTab].entries[index];
        targetPath = entry.fullPath;
        isDir = entry.isDirectory;
    }

    if (isDir) {
        NavigateTo(targetPath.c_str());
    } else {
        ShellExecuteW(m_hwnd, nullptr, targetPath.c_str(), nullptr, nullptr, SW_SHOW);
    }
}

void MainWindow::CloseTab(int index) {
    std::lock_guard<std::mutex> lock(m_tabMutex);
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return;

    for (auto& e : m_tabs[index].entries) {
        if (e.icon && e.icon != m_folderIcon && e.icon != m_fileIcon)
            DestroyIcon(e.icon);
    }
    m_tabs.erase(m_tabs.begin() + index);
    SendMessageW(m_tabControl, TCM_DELETEITEM, index, 0);

    if (m_tabs.empty()) {
        PostQuitMessage(0);
        return;
    }

    if (m_activeTab >= static_cast<int>(m_tabs.size()))
        m_activeTab = static_cast<int>(m_tabs.size()) - 1;
    SendMessageW(m_tabControl, TCM_SETCURSEL, m_activeTab, 0);
}

void MainWindow::SwitchTab(int index) {
    {
        std::lock_guard<std::mutex> lock(m_tabMutex);
        if (index < 0 || index >= static_cast<int>(m_tabs.size())) return;
        m_activeTab = index;
        SendMessageW(m_tabControl, TCM_SETCURSEL, index, 0);
    }
    InvalidateRect(m_hwnd, nullptr, TRUE);
    NotifyTaskbarTabChanged();
}


LRESULT CALLBACK MainWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = reinterpret_cast<MainWindow*>(
        GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE:
        return 0;
    case WM_SIZE:
        if (self) self->HandleResize();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = kMinWidth;
        mmi->ptMinTrackSize.y = kMinHeight;
        return 0;
    }
    case WM_COMMAND:
        if (self) {
            switch (LOWORD(wParam)) {
            case CMD_BACK:    self->GoBack(); break;
            case CMD_FORWARD: self->GoForward(); break;
            case CMD_UP:      self->GoUp(); break;
            case CMD_REFRESH:
                if (self->m_activeTab >= 0 && self->m_activeTab < (int)self->m_tabs.size())
                    self->NavigateTo(self->m_tabs[self->m_activeTab].path.c_str());
                break;
            case CMD_NEW_TAB:
                self->AddTab(L"C:\\");
                self->NavigateTo(L"C:\\");
                break;
            }
        }
        return 0;
    case WM_NOTIFY: {
        NMHDR* nmhdr = (NMHDR*)lParam;
        if (self && nmhdr->hwndFrom == self->m_tabControl && nmhdr->code == TCN_SELCHANGE) {
            int sel = (int)SendMessageW(self->m_tabControl, TCM_GETCURSEL, 0, 0);
            self->SwitchTab(sel);
        }
        if (self && nmhdr->hwndFrom == self->m_treeView && nmhdr->code == TVN_SELCHANGEDW) {
            NMTREEVIEWW* nmtv = (NMTREEVIEWW*)lParam;
            wchar_t* path = (wchar_t*)nmtv->itemNew.lParam;
            if (path) self->NavigateTo(path);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (self) {
            RECT rc;
            GetClientRect(hWnd, &rc);

            int toolbarH = kToolbarHeight + kTabHeight;
            int listX = kTreeViewWidth;
            int listW = rc.right - kTreeViewWidth;
            int listH = rc.bottom - toolbarH - kStatusBarHeight;

            RECT listRc = { listX, toolbarH, listX + listW, toolbarH + listH };
            FillRect(hdc, &listRc, self->m_bgBrush);

            std::lock_guard<std::mutex> lock(self->m_tabMutex);
            if (self->m_activeTab >= 0 && self->m_activeTab < (int)self->m_tabs.size()) {
                const auto& tab = self->m_tabs[self->m_activeTab];
                HFONT oldFont = (HFONT)SelectObject(hdc, self->m_font);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(220, 220, 220));

                int y = toolbarH + 4;
                int iconSize = 16;
                int rowH = 22;

                for (size_t i = 0; i < tab.entries.size(); ++i) {
                    const auto& entry = tab.entries[i];
                    if (y + rowH > toolbarH + listH) break;

                    if (entry.icon) {
                        DrawIconEx(hdc, listX + 8, y + (rowH - iconSize) / 2,
                                   entry.icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
                    }

                    RECT textRc = { listX + 30, y, listX + listW - 8, y + rowH };
                    SetTextColor(hdc, entry.isDirectory ? RGB(100, 180, 255) : RGB(220, 220, 220));
                    DrawTextW(hdc, entry.name.c_str(), -1, &textRc,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                    if (entry.size > 0 && !entry.isDirectory) {
                        wchar_t sizeBuf[64];
                        if (entry.size > 1024 * 1024 * 1024)
                            StringCchPrintfW(sizeBuf, _countof(sizeBuf), L"%.1f GB",
                                entry.size / (1024.0 * 1024.0 * 1024.0));
                        else if (entry.size > 1024 * 1024)
                            StringCchPrintfW(sizeBuf, _countof(sizeBuf), L"%.1f MB",
                                entry.size / (1024.0 * 1024.0));
                        else if (entry.size > 1024)
                            StringCchPrintfW(sizeBuf, _countof(sizeBuf), L"%.1f KB",
                                entry.size / 1024.0);
                        else
                            StringCchPrintfW(sizeBuf, _countof(sizeBuf), L"%llu B", entry.size);

                        SetTextColor(hdc, RGB(140, 140, 160));
                        RECT sizeRc = { listX + listW - 100, y, listX + listW - 8, y + rowH };
                        DrawTextW(hdc, sizeBuf, -1, &sizeRc,
                            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                    }

                    y += rowH;
                }

                SelectObject(hdc, oldFont);
            }
        }
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_USER + 1:
        if (self) self->OnScanComplete((int)wParam);
        return 0;
    case WM_RBUTTONUP:
        if (self) {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            POINT pt = { x, y };
            ClientToScreen(hWnd, &pt);

            int toolbarH = kToolbarHeight + kTabHeight;
            if (y >= toolbarH) {
                std::lock_guard<std::mutex> lock(self->m_tabMutex);
                int rowH = 22;
                int itemIndex = (y - toolbarH - 4) / rowH;
                if (self->m_activeTab >= 0 && itemIndex >= 0 &&
                    itemIndex < static_cast<int>(self->m_tabs[self->m_activeTab].entries.size())) {
                    const auto& entry = self->m_tabs[self->m_activeTab].entries[itemIndex];
                    self->RequestContextMenu(hWnd, pt.x, pt.y, entry.fullPath.c_str());
                }
            }
        }
        return 0;
    case WM_LBUTTONDBLCLK:
        if (self) {
            int yPos = HIWORD(lParam);
            int toolbarH = kToolbarHeight + kTabHeight;
            if (yPos >= toolbarH) {
                int rowH = 22;
                int itemIndex = (yPos - toolbarH - 4) / rowH;
                self->HandleDoubleClick(itemIndex);
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

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    LlaShell::MainWindow mainWnd;
    const wchar_t* initialPath = (lpCmdLine && lpCmdLine[0]) ? lpCmdLine : nullptr;
    if (!mainWnd.Create(hInstance, initialPath)) return 1;
    mainWnd.Show(nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    mainWnd.Shutdown();
    CoUninitialize();
    return 0;
}

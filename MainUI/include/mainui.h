#pragma once
#ifndef LLASHELL_MAINUI_H
#define LLASHELL_MAINUI_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <CommCtrl.h>
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <queue>

#pragma comment(lib, "comctl32.lib")

namespace LlaShell {

struct FileEntry {
    std::wstring name;
    std::wstring fullPath;
    uint64_t     size;
    FILETIME     lastModified;
    bool         isDirectory;
    HICON        icon;
};

struct TabInfo {
    std::wstring path;
    std::wstring title;
    HWND         listView;
    std::vector<FileEntry> entries;
};

struct ScanJob {
    std::wstring path;
    int          tabIndex;
};

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    bool Create(HINSTANCE hInstance, const wchar_t* initialPath);
    void Show(int nCmdShow);
    void Shutdown();

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    static constexpr int kTreeViewWidth  = 200;
    static constexpr int kTabHeight      = 32;
    static constexpr int kToolbarHeight  = 36;
    static constexpr int kStatusBarHeight = 24;
    static constexpr int kMinWidth       = 800;
    static constexpr int kMinHeight      = 600;

    void CreateControls();
    void CreateToolbar();
    void CreateStatusBar();
    void CreateTreeView();
    void CreateListView(int tabIndex);

    void AddTab(const wchar_t* path);
    void CloseTab(int index);
    void SwitchTab(int index);
    void NavigateTo(const wchar_t* path, int tabIndex = -1);

    void PopulateTreeView();
    void AddTreeNode(HTREEITEM parent, const wchar_t* path);

    void ScanDirectoryAsync(const wchar_t* path, int tabIndex);
    void ScanWorkerThread();
    void OnScanComplete(int tabIndex);

    void HandleListViewNotify(LPARAM lParam);
    void HandleDoubleClick(int index);
    void HandleContextMenu(int x, int y);
    void UpdateStatusBar();
    void HandleResize();

    void GoBack();
    void GoForward();
    void GoUp();

    HWND                    m_hwnd;
    HWND                    m_toolbar;
    HWND                    m_statusBar;
    HWND                    m_tabControl;
    HWND                    m_treeView;
    HINSTANCE               m_hInstance;

    std::vector<TabInfo>    m_tabs;
    int                     m_activeTab;
    std::mutex              m_tabMutex;

    std::vector<std::wstring> m_history;
    int                       m_historyIndex;

    std::thread             m_scanThread;
    std::atomic<bool>       m_running;
    std::mutex              m_scanMutex;
    std::queue<ScanJob>     m_scanQueue;

    HFONT                   m_font;
    HBRUSH                  m_bgBrush;
    HBRUSH                  m_panelBrush;
    HICON                   m_folderIcon;
    HICON                   m_fileIcon;
};

} // namespace LlaShell

#endif // LLASHELL_MAINUI_H

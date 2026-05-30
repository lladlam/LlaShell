#pragma once
#ifndef LLASHELL_TASKBAR_H
#define LLASHELL_TASKBAR_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <CommCtrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <unordered_set>

#include "CoreIPC/CoreIPC.h"
#include "ipc_messages.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace LlaShell {

struct TaskButton {
    HWND        hwnd;
    DWORD       pid;
    std::wstring title;
    HICON       hIcon;
    bool        active;
    bool        pinned;
    bool        hovered;
    RECT        rect;
};

class TaskbarWindow {
public:
    TaskbarWindow();
    ~TaskbarWindow();

    bool Create(HINSTANCE hInstance);
    void Show();
    void Shutdown();

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    static constexpr int kDefaultTaskbarHeight = 40;
    static constexpr int kDefaultButtonWidth   = 180;
    static constexpr int kDefaultButtonHeight  = 34;
    static constexpr int kStartBtnWidth        = 48;
    static constexpr int kSearchBtnWidth       = 40;
    static constexpr int kTrayAreaMinWidth     = 300;
    static constexpr int kTrayIconSize         = 16;
    static constexpr int kClockWidth           = 70;
    static constexpr int kNotifBtnWidth        = 40;
    static constexpr int kShowDesktopWidth     = 6;
    static constexpr int kActiveBarHeight      = 3;
    static constexpr int kButtonPad            = 2;
    static constexpr int kIconTextGap          = 6;
    static constexpr int kButtonIconSize       = 24;

    int m_taskbarHeight;
    int m_buttonWidth;
    int m_buttonHeight;
    int m_dpi;

    void InitDPI();
    int  Scale(int value) const;

    void DrawTaskbar(HDC hdc, const RECT& rc);
    void DrawStartButton(HDC hdc, const RECT& rc);
    void DrawSearchButton(HDC hdc, const RECT& rc);
    void DrawTaskButtons(HDC hdc, const RECT& rc);
    void DrawTrayArea(HDC hdc, const RECT& rc);
    void DrawClock(HDC hdc, const RECT& rc);
    void DrawNotificationButton(HDC hdc, const RECT& rc);
    void DrawShowDesktop(HDC hdc, const RECT& rc);
    void DrawActiveIndicator(HDC hdc, const RECT& btnRc, COLORREF color);
    void DrawHoverHighlight(HDC hdc, const RECT& btnRc);

    void HandleClick(int x, int y);
    void HandleMiddleClick(int x, int y);
    void HandleRightClick(int x, int y);
    void HandleTaskButtonRightClick(int index, int screenX, int screenY);

    void EnumerateWindows();
    static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam);
    bool IsTaskbarWindow(HWND hwnd) const;
    void RefreshTaskButtons();

    void AddPinnedApp(const wchar_t* exePath, const wchar_t* title);
    void RemovePinnedApp(const wchar_t* exePath);
    void LoadPinnedApps();
    void SavePinnedApps();
    bool IsPinned(const wchar_t* exePath) const;
    std::wstring GetWindowExePath(HWND hwnd) const;

    void FullscreenDetectThread();
    void WindowMonitorThread();
    void RegisterGlobalHotkeys();
    void ShowStartMenu(int screenX, int screenY);
    void ShowTaskbarContextMenu(int screenX, int screenY);
    void OpenTaskManager();

    void InitIPC();
    void ShutdownIPC();

    static void OnIPCMessage(IPC::SessionId from, uint16_t msgType,
                             const void* data, uint32_t size, void* userData);
    static void OnPeerConnected(IPC::SessionId peer, IPC::ProcessId pid, void* userData);
    static void OnPeerDisconnected(IPC::SessionId peer, void* userData);

    void AddTaskButtonFromIPC(uint32_t pid, uint64_t hwnd, const wchar_t* title);
    void RemoveTaskButtonFromIPC(uint64_t hwnd);
    void UpdateTaskButtonTitleFromIPC(uint64_t hwnd, const wchar_t* title);

    HWND                            m_hwnd;
    HINSTANCE                       m_hInstance;
    std::vector<TaskButton>         m_buttons;
    mutable std::mutex              m_btnMutex;

    std::thread                     m_fsDetectThread;
    std::thread                     m_windowMonitorThread;
    std::atomic<bool>               m_running;
    std::atomic<bool>               m_isHidden;

    HFONT                           m_font;
    HFONT                           m_fontSmall;
    HFONT                           m_fontBold;
    HBRUSH                          m_bgBrush;
    HBRUSH                          m_btnBrush;
    HBRUSH                          m_hoverBrush;
    HBRUSH                          m_activeBrush;
    HPEN                            m_borderPen;
    HPEN                            m_activeBarPen;
    COLORREF                        m_accentColor;

    int                             m_hoverIndex;
    int                             m_activeIndex;

    struct PinnedApp {
        std::wstring exePath;
        std::wstring title;
        HICON        icon;
    };
    std::vector<PinnedApp>          m_pinnedApps;
    mutable std::mutex              m_pinnedMutex;

    wchar_t                         m_clockText[32];
    wchar_t                         m_dateText[32];
    std::mutex                      m_clockMutex;

    std::atomic<bool>               m_ipcReady;
    std::unordered_set<HWND>        m_knownWindows;
};

} // namespace LlaShell

#endif // LLASHELL_TASKBAR_H

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
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>

namespace LlaShell {

struct TrayIcon {
    uint32_t id;
    HWND     ownerHwnd;
    HICON    hIcon;
    wchar_t  tooltip[128];
    RECT     rect;
};

struct TaskButton {
    uint32_t    pid;
    uint64_t    hwnd;
    std::wstring title;
    bool        active;
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
    static constexpr int kTaskbarHeight  = 40;
    static constexpr int kTrayIconSize   = 16;
    static constexpr int kButtonWidth    = 160;
    static constexpr int kButtonHeight   = 30;
    static constexpr int kStartBtnWidth  = 60;
    static constexpr int kClockWidth     = 80;

    void DrawTaskbar(HDC hdc, const RECT& rc);
    void DrawStartButton(HDC hdc, const RECT& rc);
    void DrawClock(HDC hdc, const RECT& rc);
    void DrawTaskButtons(HDC hdc, const RECT& rc);
    void DrawTrayArea(HDC hdc, const RECT& rc);

    void HandleClick(int x, int y);
    void HandleRightClick(int x, int y);

    void FullscreenDetectThread();
    void RegisterGlobalHotkeys();
    void UpdateClock();
    void ShowStartMenu(int x, int y);

    HWND                    m_hwnd;
    HINSTANCE               m_hInstance;
    std::vector<TaskButton> m_buttons;
    std::mutex              m_btnMutex;
    std::vector<TrayIcon>   m_trayIcons;
    std::mutex              m_trayMutex;

    std::thread             m_fsDetectThread;
    std::atomic<bool>       m_running;
    std::atomic<bool>       m_isHidden;

    HFONT                   m_font;
    HFONT                   m_fontBold;
    HBRUSH                  m_bgBrush;
    HBRUSH                  m_btnBrush;
    HBRUSH                  m_activeBrush;
    HPEN                    m_borderPen;

    wchar_t                 m_clockText[32];
    std::mutex              m_clockMutex;
};

} // namespace LlaShell

#endif // LLASHELL_TASKBAR_H

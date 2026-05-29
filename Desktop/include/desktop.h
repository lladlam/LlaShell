#pragma once
#ifndef LLASHELL_DESKTOP_H
#define LLASHELL_DESKTOP_H

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

namespace LlaShell {

struct DesktopIcon {
    std::wstring name;
    std::wstring fullPath;
    bool         isDirectory;
    int          gridX;
    int          gridY;
    HICON        hIcon;
};

class DesktopWindow {
public:
    DesktopWindow();
    ~DesktopWindow();

    bool Create(HINSTANCE hInstance);
    void Show();
    void Shutdown();

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void ScanDesktopFolder();
    void RenderIcons(HDC hdc, const RECT& rc);
    void HandleDoubleClick(int x, int y);
    void HandleRightClick(int x, int y);
    void WorkerThread();
    void InitIPC();
    void ShutdownIPC();

    HWND                    m_hwnd;
    HINSTANCE               m_hInstance;
    std::vector<DesktopIcon>m_icons;
    std::mutex              m_iconMutex;
    std::thread             m_worker;
    std::atomic<bool>       m_running;
    int                     m_iconSize;
    int                     m_iconSpacingX;
    int                     m_iconSpacingY;
    HFONT                   m_font;
    HBRUSH                  m_bgBrush;
};

} // namespace LlaShell

#endif // LLASHELL_DESKTOP_H

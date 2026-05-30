#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <Shlwapi.h>
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>

#include "CoreIPC/CoreIPC.h"
#include "ipc_messages.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

namespace LlaShell {

struct LoadedExtension {
    HMODULE     hModule;
    IContextMenu* contextMenu;
    wchar_t     dllPath[MAX_PATH];
};

class ShellExtensionHost {
public:
    ShellExtensionHost() : m_hwnd(nullptr) {}
    ~ShellExtensionHost() { Cleanup(); }

    bool Initialize() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        return true;
    }

    void Cleanup() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& ext : m_extensions) {
            if (ext.contextMenu) {
                ext.contextMenu->Release();
                ext.contextMenu = nullptr;
            }
            if (ext.hModule) {
                FreeLibrary(ext.hModule);
                ext.hModule = nullptr;
            }
        }
        m_extensions.clear();
        CoUninitialize();
    }

    HMENU BuildContextMenu(const wchar_t* filePath) {
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) return nullptr;

        PIDLIST_ABSOLUTE pidl = nullptr;
        SFGAOF attrs = 0;
        HRESULT hr = SHParseDisplayName(filePath, nullptr, &pidl, 0, &attrs);
        if (FAILED(hr) || !pidl) {
            DestroyMenu(hMenu);
            return nullptr;
        }

        IShellFolder* folder = nullptr;
        PCUITEMID_CHILD child = nullptr;
        hr = SHBindToParent(pidl, IID_IShellFolder, (void**)&folder, &child);
        if (FAILED(hr) || !folder) {
            CoTaskMemFree(pidl);
            DestroyMenu(hMenu);
            return nullptr;
        }

        IContextMenu* cm = nullptr;
        hr = folder->GetUIObjectOf(nullptr, 1, &child, IID_IContextMenu, nullptr, (void**)&cm);
        if (SUCCEEDED(hr) && cm) {
            hr = cm->QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL | CMF_EXPLORE);
            if (FAILED(hr)) {
                cm->Release();
                cm = nullptr;
            }
        }

        folder->Release();
        CoTaskMemFree(pidl);

        if (!cm) {
            DestroyMenu(hMenu);
            return nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            LoadedExtension ext{};
            ext.contextMenu = cm;
            m_extensions.push_back(ext);
        }

        return hMenu;
    }

    UINT ShowContextMenu(HWND parentHwnd, const wchar_t* filePath, int screenX, int screenY) {
        HMENU hMenu = BuildContextMenu(filePath);
        if (!hMenu) return 0;

        SetForegroundWindow(parentHwnd);

        CMINVOKECOMMANDINFOEX ici{};
        ici.cbSize = sizeof(ici);
        ici.fMask = CMIC_MASK_UNICODE;
        ici.hwnd = parentHwnd;
        ici.nShow = SW_SHOWNORMAL;

        UINT cmd = TrackPopupMenu(hMenu,
            TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
            screenX, screenY, 0, parentHwnd, nullptr);

        if (cmd >= 1 && cmd <= 0x7FFF) {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& ext : m_extensions) {
                if (ext.contextMenu) {
                    CHAR verb[64];
                    if (SUCCEEDED(ext.contextMenu->GetCommandString(cmd - 1, GCS_VERBA,
                                                                    nullptr, verb, _countof(verb)))) {
                        ici.lpVerb = verb;
                        ici.lpVerbW = nullptr;
                        ext.contextMenu->InvokeCommand((LPCMINVOKECOMMANDINFO)&ici);
                        break;
                    }
                }
            }
        }

        DestroyMenu(hMenu);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& ext : m_extensions) {
                if (ext.contextMenu) {
                    ext.contextMenu->Release();
                    ext.contextMenu = nullptr;
                }
            }
            m_extensions.clear();
        }

        return cmd;
    }

private:
    HWND m_hwnd;
    std::mutex m_mutex;
    std::vector<LoadedExtension> m_extensions;
};

} // namespace LlaShell

static LlaShell::ShellExtensionHost* g_host = nullptr;
static std::atomic<bool> g_running(true);

static void OnIPCMessage(LlaShell::IPC::SessionId /*from*/, uint16_t msgType,
                         const void* data, uint32_t size, void* /*userData*/) {
    if (msgType != static_cast<uint16_t>(LlaShell::IPC::MsgType::Application)) return;
    if (!data || size < sizeof(LlaShell::IPCMessageHeader)) return;

    const auto* hdr = static_cast<const LlaShell::IPCMessageHeader*>(data);
    const void* payload = static_cast<const uint8_t*>(data) + sizeof(LlaShell::IPCMessageHeader);
    uint32_t payloadSize = size - sizeof(LlaShell::IPCMessageHeader);

    if (hdr->type == LlaShell::IPCMessageType::RequestContextMenu) {
        if (payloadSize >= sizeof(LlaShell::ContextMenuRequest) && g_host) {
            const auto* req = static_cast<const LlaShell::ContextMenuRequest*>(payload);
            HWND parentHwnd = reinterpret_cast<HWND>(req->hwndParent);
            g_host->ShowContextMenu(parentHwnd, req->filePath, req->screenX, req->screenY);
        }
    }
}

static LRESULT CALLBACK HiddenWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = hInstance; wc.lpszClassName = L"LlaShell_ShellHost";
    RegisterClassExW(&wc);
    HWND hHidden = CreateWindowExW(0, wc.lpszClassName, L"ShellHost", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);

    LlaShell::ShellExtensionHost host;
    g_host = &host;

    if (!host.Initialize()) return 1;

    LlaShell::IPC::IPCConfig cfg = LlaShell::IPC::DefaultConfig();
    cfg.processName = L"ShellHost";
    cfg.onMessage   = OnIPCMessage;

    if (LlaShell::IPC::IsSuccess(IPC_Initialize(&cfg))) {
        IPC_StartServer(L"ShellHost");

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        IPC_Shutdown();
    }

    g_host = nullptr;
    return 0;
}

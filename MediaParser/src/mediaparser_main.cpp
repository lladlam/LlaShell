#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <Shlwapi.h>
#include <strsafe.h>
#include <commctrl.h>
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <functional>

#include "CoreIPC/CoreIPC.h"
#include "ipc_messages.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace LlaShell {

struct ThumbnailJob {
    uint32_t requestId;
    wchar_t filePath[MAX_PATH];
    uint32_t maxWidth;
    uint32_t maxHeight;
};

struct ThumbResultInternal {
    uint32_t requestId;
    std::vector<uint8_t> bitmap;
    uint32_t width;
    uint32_t height;
    bool success;
};

class MediaParserService {
public:
    MediaParserService() : m_running(false), m_workerCount(0) {}
    ~MediaParserService() { Shutdown(); }

    bool Initialize(int workerCount = 4) {
        m_workerCount = workerCount;
        m_running = true;

        for (int i = 0; i < m_workerCount; i++) {
            m_workers.emplace_back(&MediaParserService::WorkerThread, this);
        }

        return true;
    }

    void Shutdown() {
        m_running = false;
        for (auto& t : m_workers) {
            if (t.joinable()) t.join();
        }
        m_workers.clear();
    }

    void SubmitJob(const ThumbnailJob& job) {
        std::lock_guard<std::mutex> lock(m_jobMutex);
        m_jobQueue.push(job);
    }

    bool TryGetResult(ThumbResultInternal& result) {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        if (m_resultQueue.empty()) return false;
        result = std::move(m_resultQueue.front());
        m_resultQueue.pop();
        return true;
    }

private:
    void WorkerThread() {
        while (m_running) {
            ThumbnailJob job;
            bool hasJob = false;

            {
                std::lock_guard<std::mutex> lock(m_jobMutex);
                if (!m_jobQueue.empty()) {
                    job = m_jobQueue.front();
                    m_jobQueue.pop();
                    hasJob = true;
                }
            }

            if (!hasJob) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            ThumbResultInternal result;
            result.requestId = job.requestId;
            result.success = GenerateThumbnail(job.filePath, job.maxWidth, job.maxHeight,
                                               result.bitmap, result.width, result.height);

            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_resultQueue.push(std::move(result));
        }
    }

    bool GenerateThumbnail(const wchar_t* filePath, uint32_t maxW, uint32_t maxH,
                           std::vector<uint8_t>& outBitmap, uint32_t& outW, uint32_t& outH) {
        wchar_t ext[MAX_PATH]{};
        const wchar_t* dot = wcsrchr(filePath, L'.');
        if (dot) wcsncpy_s(ext, dot, _countof(ext) - 1);

        bool isVideo = (_wcsicmp(ext, L".mp4") == 0 || _wcsicmp(ext, L".avi") == 0 ||
                        _wcsicmp(ext, L".mkv") == 0 || _wcsicmp(ext, L".mov") == 0 ||
                        _wcsicmp(ext, L".wmv") == 0 || _wcsicmp(ext, L".flv") == 0 ||
                        _wcsicmp(ext, L".webm") == 0 || _wcsicmp(ext, L".ts") == 0);
        bool isImage = (_wcsicmp(ext, L".jpg") == 0 || _wcsicmp(ext, L".jpeg") == 0 ||
                        _wcsicmp(ext, L".png") == 0 || _wcsicmp(ext, L".bmp") == 0 ||
                        _wcsicmp(ext, L".gif") == 0 || _wcsicmp(ext, L".webp") == 0 ||
                        _wcsicmp(ext, L".tiff") == 0 || _wcsicmp(ext, L".avif") == 0);

        if (isVideo || isImage) {
            if (TryFFmpegThumbnail(filePath, maxW, maxH, outBitmap, outW, outH))
                return true;
        }

        return TryShellIconThumbnail(filePath, outBitmap, outW, outH);
    }

    bool TryFFmpegThumbnail(const wchar_t* filePath, uint32_t maxW, uint32_t maxH,
                            std::vector<uint8_t>& outBitmap, uint32_t& outW, uint32_t& outH) {
        wchar_t ffmpegPath[MAX_PATH]{};
        if (!SearchPathW(nullptr, L"ffmpeg.exe", nullptr, _countof(ffmpegPath), ffmpegPath, nullptr)) {
            wchar_t selfDir[MAX_PATH]{};
            GetModuleFileNameW(nullptr, selfDir, _countof(selfDir));
            PathRemoveFileSpecW(selfDir);
            PathCombineW(ffmpegPath, selfDir, L"ffmpeg.exe");
            if (GetFileAttributesW(ffmpegPath) == INVALID_FILE_ATTRIBUTES) return false;
        }

        wchar_t tmpFile[MAX_PATH]{};
        GetTempPathW(_countof(tmpFile), tmpFile);
        wchar_t tmpName[MAX_PATH]{};
        GetTempFileNameW(tmpFile, L"llt", 0, tmpName);
        wcscat_s(tmpName, L".bmp");

        wchar_t scaleFilter[64]{};
        StringCchPrintfW(scaleFilter, _countof(scaleFilter), L"scale=%u:%u:force_original_aspect_ratio=decrease", maxW, maxH);

        wchar_t cmdLine[2048]{};
        StringCchPrintfW(cmdLine, _countof(cmdLine),
            L"\"%s\" -y -hide_banner -loglevel error -i \"%s\" -vframes 1 -vf \"%s\" \"%s\"",
            ffmpegPath, filePath, scaleFilter, tmpFile);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};

        std::vector<wchar_t> cmdBuf(cmdLine, cmdLine + wcslen(cmdLine) + 1);
        BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                                 CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        if (!ok) return false;

        WaitForSingleObject(pi.hProcess, 10000);
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exitCode != 0) {
            DeleteFileW(tmpFile);
            return false;
        }

        bool result = LoadBmpFile(tmpFile, outBitmap, outW, outH);
        DeleteFileW(tmpFile);
        return result;
    }

    bool LoadBmpFile(const wchar_t* path, std::vector<uint8_t>& outBitmap, uint32_t& outW, uint32_t& outH) {
        HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return false;

        BITMAPFILEHEADER bfh{};
        BITMAPINFOHEADER bih{};
        DWORD bytesRead = 0;

        ReadFile(hFile, &bfh, sizeof(bfh), &bytesRead, nullptr);
        ReadFile(hFile, &bih, sizeof(bih), &bytesRead, nullptr);

        if (bfh.bfType != 0x4D42 || bih.biBitCount < 24) {
            CloseHandle(hFile);
            return false;
        }

        outW = static_cast<uint32_t>(abs(bih.biWidth));
        outH = static_cast<uint32_t>(abs(bih.biHeight));
        uint32_t stride = outW * 4;
        outBitmap.resize(stride * outH);

        SetFilePointer(hFile, bfh.bfOffBits, nullptr, FILE_BEGIN);
        ReadFile(hFile, outBitmap.data(), static_cast<DWORD>(outBitmap.size()), &bytesRead, nullptr);
        CloseHandle(hFile);

        if (bih.biHeight > 0) {
            for (uint32_t row = 0; row < outH / 2; row++) {
                uint8_t* top = outBitmap.data() + row * stride;
                uint8_t* bot = outBitmap.data() + (outH - 1 - row) * stride;
                for (uint32_t i = 0; i < stride; i++) std::swap(top[i], bot[i]);
            }
        }

        for (uint32_t i = 0; i < outW * outH; i++) {
            uint8_t* px = outBitmap.data() + i * 4;
            std::swap(px[0], px[2]);
        }

        return true;
    }

    bool TryShellIconThumbnail(const wchar_t* filePath,
                               std::vector<uint8_t>& outBitmap, uint32_t& outW, uint32_t& outH) {
        HBITMAP hBitmap = nullptr;

        SHFILEINFOW sfi{};
        HIMAGELIST hImageList = (HIMAGELIST)SHGetFileInfoW(filePath, 0, &sfi, sizeof(sfi),
            SHGFI_SYSICONINDEX | SHGFI_LARGEICON);
        if (hImageList) {
            HICON hIcon = ImageList_GetIcon(hImageList, sfi.iIcon, ILD_NORMAL);
            if (hIcon) {
                ICONINFO iconInfo;
                if (GetIconInfo(hIcon, &iconInfo)) {
                    hBitmap = iconInfo.hbmColor;
                    DeleteObject(iconInfo.hbmMask);
                }
                DestroyIcon(hIcon);
            }
        }

        if (!hBitmap) return false;

        BITMAP bm;
        GetObject(hBitmap, sizeof(bm), &bm);

        outW = static_cast<uint32_t>(bm.bmWidth);
        outH = static_cast<uint32_t>(bm.bmHeight);

        BITMAPINFOHEADER bi{};
        bi.biSize = sizeof(bi);
        bi.biWidth = outW;
        bi.biHeight = -static_cast<LONG>(outH);
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;

        uint32_t stride = outW * 4;
        outBitmap.resize(stride * outH);

        HDC hdc = GetDC(nullptr);
        GetDIBits(hdc, hBitmap, 0, outH, outBitmap.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        ReleaseDC(nullptr, hdc);

        DeleteObject(hBitmap);

        return true;
    }

    std::atomic<bool> m_running;
    int m_workerCount;

    std::vector<std::thread> m_workers;

    std::mutex m_jobMutex;
    std::queue<ThumbnailJob> m_jobQueue;

    std::mutex m_resultMutex;
    std::queue<ThumbResultInternal> m_resultQueue;
};

} // namespace LlaShell

static LlaShell::MediaParserService* g_service = nullptr;
static std::atomic<bool> g_running(true);
static LlaShell::IPC::ShmHandle g_shmHandle = nullptr;

static void OnIPCMessage(LlaShell::IPC::SessionId /*from*/, uint16_t msgType,
                         const void* data, uint32_t size, void* /*userData*/) {
    if (msgType != static_cast<uint16_t>(LlaShell::IPC::MsgType::Application)) return;
    if (!data || size < sizeof(LlaShell::IPCMessageHeader)) return;

    const auto* hdr = static_cast<const LlaShell::IPCMessageHeader*>(data);
    const void* payload = static_cast<const uint8_t*>(data) + sizeof(LlaShell::IPCMessageHeader);
    uint32_t payloadSize = size - sizeof(LlaShell::IPCMessageHeader);

    if (hdr->type == LlaShell::IPCMessageType::RequestThumbnail) {
        if (payloadSize >= sizeof(LlaShell::ThumbnailRequest) && g_service) {
            const auto* req = static_cast<const LlaShell::ThumbnailRequest*>(payload);
            LlaShell::ThumbnailJob job{};
            job.requestId = req->requestId;
            wcsncpy_s(job.filePath, req->filePath, _countof(job.filePath) - 1);
            job.maxWidth = req->maxWidth;
            job.maxHeight = req->maxHeight;
            g_service->SubmitJob(job);
        }
    }
}

static LRESULT CALLBACK HiddenWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = hInstance; wc.lpszClassName = L"LlaShell_MediaParser";
    RegisterClassExW(&wc);
    HWND hHidden = CreateWindowExW(0, wc.lpszClassName, L"MediaParser", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);

    LlaShell::MediaParserService service;
    g_service = &service;

    int workers = 4;
    if (lpCmdLine && lpCmdLine[0]) workers = _wtoi(lpCmdLine);
    if (workers < 1) workers = 1;
    if (workers > 16) workers = 16;

    service.Initialize(workers);

    LlaShell::IPC::IPCConfig cfg = LlaShell::IPC::DefaultConfig();
    cfg.processName = L"MediaParser";
    cfg.onMessage   = OnIPCMessage;

    if (!LlaShell::IPC::IsSuccess(IPC_Initialize(&cfg))) {
        service.Shutdown();
        return 1;
    }

    IPC_StartServer(L"MediaParser");

    IPC_ShmCreate(L"Thumbnails", 0, 64, 256 * 256 * 4 + 64, &g_shmHandle);

    HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        while (g_running) {
            LlaShell::ThumbResultInternal result;
            if (g_service && g_service->TryGetResult(result)) {
                if (g_shmHandle && result.success) {
                    std::vector<uint8_t> slotData(sizeof(uint32_t) * 4 + result.bitmap.size());
                    uint32_t* header = reinterpret_cast<uint32_t*>(slotData.data());
                    header[0] = result.requestId;
                    header[1] = result.width;
                    header[2] = result.height;
                    header[3] = static_cast<uint32_t>(result.bitmap.size());
                    if (!result.bitmap.empty())
                        memcpy(slotData.data() + 16, result.bitmap.data(), result.bitmap.size());
                    IPC_ShmWrite(g_shmHandle, slotData.data(),
                                 static_cast<uint32_t>(slotData.size()), result.requestId);
                }
            } else {
                Sleep(10);
            }
        }
        return 0;
    }, nullptr, 0, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_running = false;
    if (hThread) { WaitForSingleObject(hThread, 5000); CloseHandle(hThread); }

    if (g_shmHandle) { IPC_ShmClose(g_shmHandle); g_shmHandle = nullptr; }
    IPC_Shutdown();
    service.Shutdown();
    g_service = nullptr;
    return 0;
}

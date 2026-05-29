#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <functional>
#include <fstream>

#pragma comment(lib, "gdi32.lib")

namespace LlaShell {

struct ThumbnailJob {
    uint32_t requestId;
    wchar_t filePath[MAX_PATH];
    uint32_t maxWidth;
    uint32_t maxHeight;
};

struct ThumbnailResult {
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

    bool TryGetResult(ThumbnailResult& result) {
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

            ThumbnailResult result;
            result.requestId = job.requestId;
            result.success = GenerateThumbnail(job.filePath, job.maxWidth, job.maxHeight,
                                               result.bitmap, result.width, result.height);

            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_resultQueue.push(std::move(result));
        }
    }

    bool GenerateThumbnail(const wchar_t* filePath, uint32_t maxW, uint32_t maxH,
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
    std::queue<ThumbnailResult> m_resultQueue;
};

} // namespace LlaShell

static constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\LlaShell\\MediaParser";
static constexpr wchar_t kShmName[]  = L"Local\\LlaShm_Thumbnails";

struct ShmThumbnailSlot {
    uint32_t requestId;
    uint32_t width;
    uint32_t height;
    uint32_t dataSize;
    uint32_t flags;
    uint8_t  data[256 * 256 * 4];
};

static LlaShell::MediaParserService* g_service = nullptr;
static std::atomic<bool> g_running(true);

static DWORD WINAPI PipeServerThread(LPVOID) {
    HANDLE hPipe = CreateNamedPipeW(
        kPipeName,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        4,
        4096,
        4096,
        0,
        nullptr
    );

    if (hPipe == INVALID_HANDLE_VALUE) return 1;

    while (g_running) {
        BOOL connected = ConnectNamedPipe(hPipe, nullptr) ?
            TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (!connected) {
            DisconnectNamedPipe(hPipe);
            continue;
        }

        LlaShell::ThumbnailJob job;
        DWORD bytesRead;
        if (ReadFile(hPipe, &job, sizeof(job), &bytesRead, nullptr) && bytesRead == sizeof(job)) {
            g_service->SubmitJob(job);
        }

        DisconnectNamedPipe(hPipe);
    }

    CloseHandle(hPipe);
    return 0;
}

int wmain(int argc, wchar_t* argv[]) {
    LlaShell::MediaParserService service;
    g_service = &service;

    int workers = 4;
    if (argc >= 2) workers = _wtoi(argv[1]);
    if (workers < 1) workers = 1;
    if (workers > 16) workers = 16;

    service.Initialize(workers);

    CreateThread(nullptr, 0, PipeServerThread, nullptr, 0, nullptr);

    while (g_running) {
        LlaShell::ThumbnailResult result;
        if (service.TryGetResult(result)) {
            HANDLE hMap = OpenFileMappingW(FILE_MAP_WRITE, FALSE, kShmName);
            if (hMap) {
                void* ptr = MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, 0);
                if (ptr) {
                    ShmThumbnailSlot* slots = static_cast<ShmThumbnailSlot*>(ptr);
                    uint32_t slotIndex = result.requestId % 64;

                    ShmThumbnailSlot& slot = slots[slotIndex];
                    slot.requestId = result.requestId;
                    slot.width = result.width;
                    slot.height = result.height;
                    slot.dataSize = static_cast<uint32_t>(result.bitmap.size());
                    slot.flags = result.success ? 1 : 0;

                    if (result.success && result.bitmap.size() <= sizeof(slot.data)) {
                        memcpy(slot.data, result.bitmap.data(), result.bitmap.size());
                    }

                    UnmapViewOfFile(ptr);
                }
                CloseHandle(hMap);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    service.Shutdown();
    g_service = nullptr;
    return 0;
}

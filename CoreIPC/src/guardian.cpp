#include "CoreIPC/guardian.h"
#include <algorithm>
#include <chrono>

namespace LlaShell::IPC {

Guardian::Guardian()
    : m_stopEvent(nullptr)
    , m_onCrashed(nullptr)
    , m_userData(nullptr)
    , m_checkIntervalMs(kDefaultCrashDetectMs)
    , m_running(false) {}

Guardian::~Guardian() {
    Stop();
}

void Guardian::Start(const IPCConfig* config, uint32_t checkIntervalMs) {
    if (m_running) return;

    m_onCrashed       = config ? config->onCrashed : nullptr;
    m_userData        = config ? config->userData : nullptr;
    m_checkIntervalMs = checkIntervalMs > 0 ? checkIntervalMs : kDefaultCrashDetectMs;
    m_running         = true;

    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    m_monitorThread = std::thread([this]() { MonitorThread(); });
}

void Guardian::Stop() {
    m_running = false;
    if (m_stopEvent) {
        SetEvent(m_stopEvent);
    }
    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }
    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& entry : m_entries) {
        if (entry.processHandle) {
            CloseHandle(entry.processHandle);
            entry.processHandle = nullptr;
        }
    }
    m_entries.clear();
}

Result Guardian::WatchProcess(SessionId session, ProcessId pid,
                               const wchar_t* exePath, const wchar_t* arguments,
                               bool autoRestart, uint32_t maxRestarts) {
    if (!exePath) return Result::InvalidArg;

    HANDLE hProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return Result::InvalidArg;

    GuardianEntry entry{};
    entry.session       = session;
    entry.pid           = pid;
    entry.processHandle = hProcess;
    entry.exePath       = exePath;
    entry.arguments     = arguments ? arguments : L"";
    entry.autoRestart   = autoRestart;
    entry.maxRestarts   = maxRestarts;
    entry.restartCount  = 0;
    entry.lastRestartTime = 0;

    std::lock_guard<std::mutex> lock(m_mutex);

    // Replace existing entry for same session
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
        [session](const GuardianEntry& e) { return e.session == session; });

    if (it != m_entries.end()) {
        if (it->processHandle) CloseHandle(it->processHandle);
        *it = std::move(entry);
    } else {
        m_entries.push_back(std::move(entry));
    }

    return Result::Success;
}

Result Guardian::UnwatchProcess(SessionId session) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
        [session](const GuardianEntry& e) { return e.session == session; });

    if (it == m_entries.end()) return Result::InvalidArg;

    if (it->processHandle) CloseHandle(it->processHandle);
    m_entries.erase(it);
    return Result::Success;
}

bool Guardian::IsAlive(SessionId session) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& entry : m_entries) {
        if (entry.session == session) {
            if (!entry.processHandle) return false;
            DWORD exitCode = 0;
            if (!GetExitCodeProcess(entry.processHandle, &exitCode)) return false;
            return (exitCode == STILL_ACTIVE);
        }
    }
    return false;
}

void Guardian::SetCallbacks(OnProcessCrashed onCrashed, void* userData) {
    m_onCrashed = onCrashed;
    m_userData  = userData;
}

void Guardian::MonitorThread() {
    while (m_running) {
        std::vector<HANDLE> handles;
        std::vector<SessionId> sessions;
        std::vector<size_t> indices;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (size_t i = 0; i < m_entries.size(); i++) {
                if (m_entries[i].processHandle) {
                    handles.push_back(m_entries[i].processHandle);
                    sessions.push_back(m_entries[i].session);
                    indices.push_back(i);
                }
            }
        }

        if (handles.empty()) {
            WaitForSingleObject(m_stopEvent, m_checkIntervalMs);
            continue;
        }

        // Add stop event at the end
        handles.push_back(m_stopEvent);

        DWORD result = WaitForMultipleObjects(
            static_cast<DWORD>(handles.size()),
            handles.data(),
            FALSE,   // wait for any
            m_checkIntervalMs);

        if (result == WAIT_TIMEOUT) {
            continue;
        }

        if (result == WAIT_OBJECT_0 + handles.size() - 1) {
            // Stop event signaled
            break;
        }

        if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + handles.size() - 1) {
            DWORD idx = result - WAIT_OBJECT_0;
            SessionId crashedSession = sessions[idx];

            // Process exited — check if it was graceful
            GuardianEntry* entry = nullptr;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                entry = &m_entries[indices[idx]];
            }

            DWORD exitCode = 0;
            GetExitCodeProcess(entry->processHandle, &exitCode);

            // Close old handle
            CloseHandle(entry->processHandle);
            entry->processHandle = nullptr;

            // Notify callback
            if (m_onCrashed) {
                m_onCrashed(crashedSession, m_userData);
            }

            // Auto-restart if enabled
            if (entry->autoRestart &&
                (entry->maxRestarts == 0 || entry->restartCount < entry->maxRestarts)) {
                ProcessId newPid = 0;
                if (RestartProcess(*entry, &newPid)) {
                    entry->pid = newPid;
                    entry->restartCount++;

                    // Re-acquire process handle
                    entry->processHandle = OpenProcess(
                        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, newPid);
                }
            }
        }
    }
}

bool Guardian::RestartProcess(GuardianEntry& entry, ProcessId* newPid) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::wstring cmdLine = L"\"";
    cmdLine += entry.exePath;
    cmdLine += L"\"";
    if (!entry.arguments.empty()) {
        cmdLine += L" ";
        cmdLine += entry.arguments;
    }

    // CreateProcessW requires a mutable command line buffer
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr,
        cmdBuf.data(),
        nullptr, nullptr,
        FALSE,
        CREATE_NEW_PROCESS_GROUP,
        nullptr, nullptr,
        &si, &pi);

    if (!ok) return false;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess); // We'll reopen with SYNCHRONIZE via WatchProcess

    *newPid = pi.dwProcessId;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    entry.lastRestartTime = now.QuadPart;

    return true;
}

} // namespace LlaShell::IPC

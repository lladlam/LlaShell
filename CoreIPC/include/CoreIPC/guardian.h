#pragma once

#ifndef LLASHELL_COREIPC_GUARDIAN_H
#define LLASHELL_COREIPC_GUARDIAN_H

#include "types.h"
#include <string>
#include <vector>
#include <mutex>
#include <thread>

namespace LlaShell::IPC {

struct GuardianEntry {
    SessionId   session;
    ProcessId   pid;
    HANDLE      processHandle;
    std::wstring exePath;
    std::wstring arguments;
    bool        autoRestart;
    uint32_t    restartCount;
    uint32_t    maxRestarts;
    int64_t     lastRestartTime;
};

class Guardian {
public:
    Guardian();
    ~Guardian();

    Guardian(const Guardian&) = delete;
    Guardian& operator=(const Guardian&) = delete;

    void Start(const IPCConfig* config, uint32_t checkIntervalMs);
    void Stop();

    Result WatchProcess(SessionId session, ProcessId pid,
                        const wchar_t* exePath, const wchar_t* arguments,
                        bool autoRestart, uint32_t maxRestarts);

    Result UnwatchProcess(SessionId session);

    bool IsAlive(SessionId session) const;

    void SetCallbacks(OnProcessCrashed onCrashed, void* userData);

private:
    void MonitorThread();
    bool RestartProcess(GuardianEntry& entry, ProcessId* newPid);
    static DWORD WINAPI LaunchProcess(GuardianEntry* entry, ProcessId* outPid);

    mutable std::mutex              m_mutex;
    std::vector<GuardianEntry>      m_entries;
    std::thread                     m_monitorThread;
    HANDLE                          m_stopEvent;
    OnProcessCrashed                m_onCrashed;
    void*                           m_userData;
    uint32_t                        m_checkIntervalMs;
    bool                            m_running;
};

} // namespace LlaShell::IPC

#endif // LLASHELL_COREIPC_GUARDIAN_H

#include "CoreIPC/pipe_channel.h"
#include <Aclapi.h>
#include <Sddl.h>

#pragma comment(lib, "advapi32.lib")

namespace LlaShell::IPC {

SECURITY_ATTRIBUTES* PipeChannel::GetSecurityAttributes() {
    if (m_securityInitialized) return &m_securityAttr;

    if (!InitializeSecurityDescriptor(&m_securityDesc, SECURITY_DESCRIPTOR_REVISION))
        return nullptr;

    if (!SetSecurityDescriptorDacl(&m_securityDesc, TRUE, NULL, FALSE))
        return nullptr;

    // ConvertStringSecurityDescriptorToSecurityDescriptorW(
    //     L"S:(ML;;NW;;;LW)", SDDL_REVISION_1, ...);
    // Low integrity label — allows AppContainer and low-integrity processes to connect.
    // For now, NULL DACL alone is sufficient for cross-UAC local pipe access.
    // The PIPE_REJECT_REMOTE_CLIENTS flag on pipe creation blocks any network access.

    m_securityAttr.nLength              = sizeof(SECURITY_ATTRIBUTES);
    m_securityAttr.lpSecurityDescriptor = &m_securityDesc;
    m_securityAttr.bInheritHandle       = FALSE;

    m_securityInitialized = true;
    return &m_securityAttr;
}

void ShmChannel::InitSecurity() {
    if (m_securityInitialized) return;

    if (InitializeSecurityDescriptor(&m_securityDesc, SECURITY_DESCRIPTOR_REVISION)) {
        if (SetSecurityDescriptorDacl(&m_securityDesc, TRUE, NULL, FALSE)) {
            m_securityAttr.nLength              = sizeof(SECURITY_ATTRIBUTES);
            m_securityAttr.lpSecurityDescriptor = &m_securityDesc;
            m_securityAttr.bInheritHandle       = FALSE;
            m_securityInitialized = true;
        }
    }
}

} // namespace LlaShell::IPC

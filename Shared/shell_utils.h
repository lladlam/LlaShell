#pragma once

#ifndef LLASHELL_SHARED_SHELL_UTILS_H
#define LLASHELL_SHARED_SHELL_UTILS_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <Shlobj.h>

namespace LlaShell::ShellUtils {

inline bool SetAsDefaultExplorer() {
    const wchar_t* subKeys[] = {
        L"Software\\Classes\\Folder\\shell\\open\\command",
        L"Software\\Classes\\Directory\\shell\\open\\command",
    };

    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, _countof(exePath));

    wchar_t cmdValue[MAX_PATH + 16]{};
    wsprintfW(cmdValue, L"\"%s\" \"%%1\"", exePath);

    for (const auto* subKey : subKeys) {
        HKEY hKey = nullptr;
        LONG result = RegCreateKeyExW(
            HKEY_CURRENT_USER, subKey, 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr);
        if (result != ERROR_SUCCESS) continue;

        RegSetValueExW(hKey, nullptr, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(cmdValue),
            static_cast<DWORD>((wcslen(cmdValue) + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

inline bool RestoreDefaultExplorer() {
    const wchar_t* subKeys[] = {
        L"Software\\Classes\\Folder\\shell\\open\\command",
        L"Software\\Classes\\Directory\\shell\\open\\command",
    };

    for (const auto* subKey : subKeys) {
        RegDeleteKeyW(HKEY_CURRENT_USER, subKey);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

inline bool IsDefaultExplorer() {
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Classes\\Folder\\shell\\open\\command",
        0, KEY_QUERY_VALUE, &hKey);
    if (result != ERROR_SUCCESS) return false;

    wchar_t value[MAX_PATH]{};
    DWORD valueSize = sizeof(value);
    DWORD type = 0;
    result = RegQueryValueExW(hKey, nullptr, nullptr, &type,
        reinterpret_cast<BYTE*>(value), &valueSize);
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS || type != REG_SZ) return false;

    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, _countof(exePath));
    return (wcsstr(value, exePath) != nullptr);
}

} // namespace LlaShell::ShellUtils

#endif // LLASHELL_SHARED_SHELL_UTILS_H

#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <Shlwapi.h>
#include <strsafe.h>
#include <WinInet.h>
#include <cstdint>
#include <string>
#include <vector>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shlwapi.lib")

static constexpr wchar_t kReleasesUrl[] = L"https://api.github.com/repos/lladlam/LlaShell/releases";
static constexpr wchar_t kUserAgent[]   = L"LlaShell-Updater/1.0";
static constexpr wchar_t kAppName[]     = L"LlaShell";

static std::string HttpGet(const wchar_t* url) {
    std::string result;
    HINTERNET hInternet = InternetOpenW(kUserAgent, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInternet) return result;
    HINTERNET hUrl = InternetOpenUrlW(hInternet, url, nullptr, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) { InternetCloseHandle(hInternet); return result; }

    char buf[8192]{};
    DWORD bytesRead = 0;
    while (InternetReadFile(hUrl, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
        result.append(buf, bytesRead);
        bytesRead = 0;
    }
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return result;
}

static bool HttpDownload(const wchar_t* url, const wchar_t* localPath) {
    HINTERNET hInternet = InternetOpenW(kUserAgent, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInternet) return false;
    HINTERNET hUrl = InternetOpenUrlW(hInternet, url, nullptr, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) { InternetCloseHandle(hInternet); return false; }

    HANDLE hFile = CreateFileW(localPath, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return false;
    }

    char buf[65536]{};
    DWORD bytesRead = 0;
    bool ok = true;
    while (InternetReadFile(hUrl, buf, sizeof(buf), &bytesRead) && bytesRead > 0) {
        DWORD written = 0;
        if (!WriteFile(hFile, buf, bytesRead, &written, nullptr) || written != bytesRead) {
            ok = false;
            break;
        }
        bytesRead = 0;
    }

    CloseHandle(hFile);
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return ok;
}

static std::string JsonGetString(const std::string& json, const char* key) {
    std::string search = "\"";
    search += key;
    search += "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return {};
    pos = json.find_first_not_of(" \t\n\r", pos + 1);
    if (pos == std::string::npos) return {};
    if (json[pos] == '"') {
        size_t start = pos + 1;
        size_t end = json.find('"', start);
        if (end == std::string::npos) return {};
        return json.substr(start, end - start);
    }
    if (json.compare(pos, 4, "true") == 0) return "true";
    if (json.compare(pos, 5, "false") == 0) return "false";
    return {};
}

static std::string FindAssetUrl(const std::string& assetsJson, const char* ext) {
    size_t pos = 0;
    while (pos < assetsJson.size()) {
        size_t urlPos = assetsJson.find("\"browser_download_url\"", pos);
        if (urlPos == std::string::npos) break;

        size_t colonPos = assetsJson.find(':', urlPos);
        size_t q1 = assetsJson.find('"', colonPos + 1);
        size_t q2 = assetsJson.find('"', q1 + 1);
        if (q1 == std::string::npos || q2 == std::string::npos) break;

        std::string url = assetsJson.substr(q1 + 1, q2 - q1 - 1);

        size_t dotPos = url.rfind('.');
        if (dotPos != std::string::npos) {
            std::string urlExt = url.substr(dotPos);
            for (auto& c : urlExt) c = static_cast<char>(tolower(c));
            if (urlExt == ext) return url;
        }

        pos = q2 + 1;
    }
    return {};
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), len);
    return ws;
}

static bool IsNewerVersion(const wchar_t* current, const wchar_t* latest) {
    auto parse = [](const wchar_t* v, int& a, int& b, int& c, int& d) {
        a = b = c = d = 0;
        const wchar_t* p = v;
        if (*p == L'v' || *p == L'V') p++;
        a = _wtoi(p);
        while (*p && *p != L'.') p++;
        if (*p == L'.') { p++; b = _wtoi(p); }
        while (*p && *p != L'.') p++;
        if (*p == L'.') { p++; c = _wtoi(p); }
        while (*p && *p != L'.') p++;
        if (*p == L'.') { p++; d = _wtoi(p); }
    };
    int ca, cb, cc, cd, la, lb, lc, ld;
    parse(current, ca, cb, cc, cd);
    parse(latest, la, lb, lc, ld);
    if (la != ca) return la > ca;
    if (lb != cb) return lb > cb;
    if (lc != cc) return lc > cc;
    return ld > cd;
}

static bool IsCriticalBody(const std::string& body) {
    return (body.find("紧急更新") != std::string::npos ||
            body.find("重要更新") != std::string::npos ||
            body.find("critical") != std::string::npos ||
            body.find("urgent") != std::string::npos);
}

static bool ReadSetting(const wchar_t* key, int defaultVal) {
    wchar_t cfgPath[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, cfgPath);
    PathAppendW(cfgPath, L"LlaShell\\update_settings.ini");
    return (GetPrivateProfileIntW(L"Update", key, defaultVal, cfgPath) != 0);
}

static void GetSelfDir(wchar_t* outDir, DWORD size) {
    GetModuleFileNameW(nullptr, outDir, size);
    PathRemoveFileSpecW(outDir);
}

static void CopyDirectoryRecursive(const wchar_t* srcDir, const wchar_t* dstDir) {
    CreateDirectoryW(dstDir, nullptr);

    wchar_t search[MAX_PATH]{};
    PathCombineW(search, srcDir, L"*");

    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW(search, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        wchar_t srcPath[MAX_PATH]{};
        wchar_t dstPath[MAX_PATH]{};
        PathCombineW(srcPath, srcDir, fd.cFileName);
        PathCombineW(dstPath, dstDir, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CopyDirectoryRecursive(srcPath, dstPath);
        } else {
            if (!CopyFileW(srcPath, dstPath, FALSE)) {
                wchar_t oldPath[MAX_PATH]{};
                StringCchCopyW(oldPath, MAX_PATH, dstPath);
                PathRemoveExtensionW(oldPath);
                PathAddExtensionW(oldPath, L".old");
                MoveFileExW(dstPath, oldPath, MOVEFILE_REPLACE_EXISTING);
                if (!CopyFileW(srcPath, dstPath, FALSE)) {
                    MoveFileExW(srcPath, dstPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_DELAY_UNTIL_REBOOT);
                }
            }
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    bool disableAll = ReadSetting(L"DisableAll", 0);
    if (disableAll) return 0;

    std::string response = HttpGet(kReleasesUrl);
    if (response.empty()) return 1;

    std::string tag = JsonGetString(response, "tag_name");
    std::string prerelease = JsonGetString(response, "prerelease");
    std::string body = JsonGetString(response, "body");
    if (tag.empty()) return 1;

    std::wstring wtag = Utf8ToWide(tag);
    bool isCritical = IsCriticalBody(body);

    const wchar_t* currentVer = L"0.0.0.1";
    if (!IsNewerVersion(currentVer, wtag.c_str())) return 0;

    bool disableAuto = ReadSetting(L"DisableAuto", 0);
    bool disableCheck = ReadSetting(L"DisableCheck", 0);

    if (disableCheck && !isCritical) return 0;
    if (disableAuto && !isCritical) return 0;

    std::string zipUrl = FindAssetUrl(response, ".zip");
    if (zipUrl.empty()) zipUrl = FindAssetUrl(response, ".rar");
    if (zipUrl.empty()) return 1;

    std::wstring downloadUrl = Utf8ToWide(zipUrl);

    wchar_t updateDir[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, updateDir);
    PathAppendW(updateDir, L"LlaShell\\update");
    CreateDirectoryW(updateDir, nullptr);

    wchar_t archivePath[MAX_PATH]{};
    bool isZip = (zipUrl.rfind(".zip") != std::string::npos);
    PathCombineW(archivePath, updateDir, isZip ? L"update.zip" : L"update.rar");

    if (!HttpDownload(downloadUrl.c_str(), archivePath)) return 1;

    wchar_t extractDir[MAX_PATH]{};
    PathCombineW(extractDir, updateDir, L"extracted");
    if (PathFileExistsW(extractDir)) {
        wchar_t delCmd[MAX_PATH + 32]{};
        StringCchPrintfW(delCmd, _countof(delCmd), L"cmd /c rmdir /s /q \"%s\"", extractDir);
        STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        CreateProcessW(nullptr, delCmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
    CreateDirectoryW(extractDir, nullptr);

    if (isZip) {
        wchar_t cmdLine[2048]{};
        StringCchPrintfW(cmdLine, _countof(cmdLine),
            L"tar -xf \"%s\" -C \"%s\"", archivePath, extractDir);
        STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> cmdBuf(cmdLine, cmdLine + wcslen(cmdLine) + 1);
        if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 60000);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        }
    } else {
        wchar_t unrarPath[MAX_PATH]{};
        GetSelfDir(unrarPath, MAX_PATH);
        PathAppendW(unrarPath, L"unrar.exe");
        if (!PathFileExistsW(unrarPath)) {
            SearchPathW(nullptr, L"unrar.exe", nullptr, MAX_PATH, unrarPath, nullptr);
        }
        if (PathFileExistsW(unrarPath)) {
            wchar_t cmdLine[2048]{};
            StringCchPrintfW(cmdLine, _countof(cmdLine),
                L"\"%s\" x -o+ \"%s\" \"%s\\\"", unrarPath, archivePath, extractDir);
            STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi{};
            std::vector<wchar_t> cmdBuf(cmdLine, cmdLine + wcslen(cmdLine) + 1);
            if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 60000);
                CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            }
        }
    }

    wchar_t selfDir[MAX_PATH]{};
    GetSelfDir(selfDir, MAX_PATH);

    wchar_t nestedDir[MAX_PATH]{};
    PathCombineW(nestedDir, extractDir, L"LlaShell");
    if (PathFileExistsW(nestedDir)) {
        CopyDirectoryRecursive(nestedDir, selfDir);
    } else {
        CopyDirectoryRecursive(extractDir, selfDir);
    }

    DeleteFileW(archivePath);

    if (!isCritical) {
        wchar_t msg[512]{};
        StringCchPrintfW(msg, _countof(msg),
            L"LlaShell %s 已更新完成。\n是否立即重启 LlaShell？", wtag.c_str());
        int result = MessageBoxW(nullptr, msg, kAppName, MB_YESNO | MB_ICONINFORMATION);
        if (result == IDYES) {
            wchar_t exePath[MAX_PATH]{};
            PathCombineW(exePath, selfDir, L"LlaShell.exe");
            ShellExecuteW(nullptr, L"open", exePath, nullptr, nullptr, SW_SHOW);
        }
    } else {
        wchar_t exePath[MAX_PATH]{};
        PathCombineW(exePath, selfDir, L"LlaShell.exe");
        ShellExecuteW(nullptr, L"open", exePath, nullptr, nullptr, SW_SHOW);
    }

    return 0;
}

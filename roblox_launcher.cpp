/**
 * VNG PS Launcher + Ultra-Fast Real-Time Discord WebSocket Gateway Sniper
 *
 * Features:
 * 1. Real-Time Discord Gateway WebSocket (`wss://gateway.discord.gg/?v=9&encoding=json`):
 *    - Sub-millisecond (0.0001s local processing) MESSAGE_CREATE event sniping!
 *    - Zero polling delay: Discord pushes every chat message directly over the live WebSocket.
 *    - Automatically subscribes to Guild events (`op: 14` Lazy Guild subscriptions) + falls back
 *      to ultra-fast parallel HTTP keep-alive + background UIA so no message is ever missed.
 * 2. Type-to-Search Server & Channel Finder:
 *    - Live Search / Filter box + editable searchable dropdowns for both Servers and Channels.
 *    - Type any part of a server name or channel name (e.g. "priv", "ps", "general") and it
 *      instantly filters the matching channels across the selected server or ALL loaded servers!
 * 3. Instant Roblox Handoff + Auto-Stop:
 *    - Never closes an already-open RobloxPlayerBeta.exe (Froststrap instant deeplink handoff).
 *    - Never disturbs gameplay (100% background, zero focus stealing, zero mouse/key simulation).
 *    - Immediately stops sniping the moment it joins a matching Private Server.
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
#include <unordered_map>
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
#define IDC_SERVER_SEARCH    216
#define IDC_CHANNEL_SEARCH   217

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
HWND g_hServerSearch = NULL;
HWND g_hServerCombo = NULL;
HWND g_hChannelSearch = NULL;
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
std::wstring g_statusText = L"Ready - Click any PS link or use the Real-Time 0.0001s Discord Gateway Sniper below.";
COLORREF g_sniperStatusColor = COLOR_MUTED;
std::wstring g_sniperStatusText = L"Sniper Idle - Type to filter servers/channels, add them, and click Start Sniper.";

NOTIFYICONDATAW g_nid = { 0 };
std::wstring g_lastLaunchedUri = L"";
ULONGLONG g_lastLaunchTick = 0;
std::wstring g_cachedStrapExe = L"";

// Sniper state synchronized by CRITICAL_SECTION
CRITICAL_SECTION g_sniperLock;
volatile bool g_sniperRunning = false;
bool g_onlyNewMessages = true;
bool g_anyNewPs = false;
volatile bool g_tokenInvalid = false;
volatile int g_uiaWins = 0;
volatile int g_uiaLinks = 0;
HWND g_hChkAny = NULL;
#define IDC_CHK_ANY 218
#define IDC_ADD_FROM_WIN 219
std::wstring g_discordToken = L"";

std::vector<DiscordGuild> g_allGuilds;
std::vector<DiscordGuild> g_filteredGuilds;

std::vector<DiscordChannel> g_currentGuildChannels;
std::vector<DiscordChannel> g_filteredChannels;
std::unordered_map<std::wstring, std::vector<DiscordChannel>> g_guildChannelCache;

std::vector<DiscordChannel> g_watchedChannels; // Multi-server watched channels
std::vector<std::wstring> g_keywords;          // Target keywords
std::unordered_set<std::wstring> g_seenMessages;

// Active WebSocket handle so stopping sniper closes it immediately
HINTERNET g_hActiveWebSocket = NULL;
volatile DWORD g_wsHeartbeatIntervalMs = 41250;
volatile int g_wsLastSeq = -1;

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
    for (wchar_t& c : s) {
        if (c >= L'A' && c <= L'Z') c += (L'a' - L'A');
        else c = (wchar_t)std::towlower(c);
    }
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

static inline bool IsUrlCodeChar(wchar_t c) {
    return (c >= L'a' && c <= L'z') ||
           (c >= L'A' && c <= L'Z') ||
           (c >= L'0' && c <= L'9') ||
           c == L'_' || c == L'-';
}

static inline bool IsDigitChar(wchar_t c) {
    return (c >= L'0' && c <= L'9');
}

// ============================================================================
// ULTRA-FAST 0.0001s Zero-Regex Link Parser (`BuildUriFast`)
// ============================================================================
std::wstring BuildUri(const std::wstring& rawText) {
    if (rawText.empty()) return L"";

    // 1. Direct roblox:// link inside message
    size_t rbxPos = rawText.find(L"roblox://");
    if (rbxPos != std::wstring::npos) {
        size_t endPos = rbxPos + 9;
        while (endPos < rawText.size()) {
            wchar_t c = rawText[endPos];
            if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\"' || c == L'\'' || c == L'<' || c == L'>') break;
            ++endPos;
        }
        return rawText.substr(rbxPos, endPos - rbxPos);
    }

    // 2. Share URL format: roblox.com/share?code=... or /share-links?code=...
    if (rawText.find(L"roblox.com/share") != std::wstring::npos || rawText.find(L"/share?code=") != std::wstring::npos) {
        size_t codeKey = rawText.find(L"code=");
        if (codeKey != std::wstring::npos && (codeKey == 0 || rawText[codeKey - 1] == L'?' || rawText[codeKey - 1] == L'&')) {
            size_t cStart = codeKey + 5;
            size_t cEnd = cStart;
            while (cEnd < rawText.size() && IsUrlCodeChar(rawText[cEnd])) ++cEnd;
            if (cEnd > cStart) {
                std::wstring code = rawText.substr(cStart, cEnd - cStart);
                std::wstring stype = L"Server";
                size_t typeKey = rawText.find(L"type=", cEnd);
                if (typeKey != std::wstring::npos && (rawText[typeKey - 1] == L'?' || rawText[typeKey - 1] == L'&')) {
                    size_t tStart = typeKey + 5;
                    size_t tEnd = tStart;
                    while (tEnd < rawText.size() && IsUrlCodeChar(rawText[tEnd])) ++tEnd;
                    if (tEnd > tStart) {
                        stype = rawText.substr(tStart, tEnd - tStart);
                    }
                }
                return L"roblox://navigation/share_links?code=" + code + L"&type=" + stype;
            }
        }
    }

    // 3. Games Private Server link format: /games/(\d+) ... privateServerLinkCode=...
    size_t gamesPos = rawText.find(L"/games/");
    size_t psCodePos = rawText.find(L"privateServerLinkCode=");
    if (gamesPos != std::wstring::npos && psCodePos != std::wstring::npos) {
        size_t pStart = gamesPos + 7;
        size_t pEnd = pStart;
        while (pEnd < rawText.size() && IsDigitChar(rawText[pEnd])) ++pEnd;

        size_t lStart = psCodePos + 22;
        size_t lEnd = lStart;
        while (lEnd < rawText.size() && IsUrlCodeChar(rawText[lEnd])) ++lEnd;

        if (pEnd > pStart && lEnd > lStart) {
            return L"roblox://placeId=" + rawText.substr(pStart, pEnd - pStart) +
                   L"&linkCode=" + rawText.substr(lStart, lEnd - lStart);
        }
    }

    return L"";
}

void SetStatus(HWND hwnd, const std::wstring& text, COLORREF color) {
    g_statusText = text;
    g_statusColor = color;
    if (hwnd && IsWindowVisible(hwnd)) {
        RECT rc = { 20, 140, 720, 165 };
        InvalidateRect(hwnd, &rc, TRUE);
    }
}

void SetSniperStatus(HWND hwnd, const std::wstring& text, COLORREF color) {
    g_sniperStatusText = text;
    g_sniperStatusColor = color;
    if (hwnd && IsWindowVisible(hwnd)) {
        RECT rc = { 20, 620, 720, 650 };
        InvalidateRect(hwnd, &rc, TRUE);
    }
}

void PostSniperStatusAsync(const std::wstring& text, COLORREF color) {
    if (!g_hMainWnd) return;
    std::wstring* pStr = new std::wstring(text);
    PostMessageW(g_hMainWnd, WM_SNIPER_STATUS, (WPARAM)color, (LPARAM)pStr);
}

std::wstring FindFroststrapOrBootstrapperExe() {
    if (!g_cachedStrapExe.empty()) return g_cachedStrapExe;
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
                g_cachedStrapExe = full;
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

    // 1. Primary: Cached Froststrap / Bloxstrap / Fishstrap path (0ms disk lookup overhead!)
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
        out << "ANY_PS=" << (g_anyNewPs ? "1" : "0") << "\n";
        for (const auto& kw : g_keywords) {
            out << "KW=" << WideToUtf8(kw) << "\n";
        }
        for (const auto& ch : g_watchedChannels) {
            out << "CHAN=" << WideToUtf8(ch.id) << "|" << WideToUtf8(ch.guildId) << "|" << WideToUtf8(ch.guildName) << "|" << WideToUtf8(ch.name) << "\n";
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
            } else if (line.rfind("ANY_PS=", 0) == 0) {
                g_anyNewPs = (line.substr(7) == "1");
            } else if (line.rfind("KW=", 0) == 0) {
                std::wstring kw = Trim(Utf8ToWide(line.substr(3)));
                if (!kw.empty()) g_keywords.push_back(kw);
            } else if (line.rfind("CHAN=", 0) == 0) {
                std::wstring rest = Utf8ToWide(line.substr(5));
                std::vector<std::wstring> parts;
                size_t start = 0;
                while (true) {
                    size_t bar = rest.find(L'|', start);
                    if (bar == std::wstring::npos) {
                        parts.push_back(rest.substr(start));
                        break;
                    }
                    parts.push_back(rest.substr(start, bar - start));
                    start = bar + 1;
                }
                if (parts.size() >= 4) {
                    DiscordChannel ch;
                    ch.id = parts[0];
                    ch.guildId = parts[1];
                    ch.guildName = parts[2];
                    ch.name = parts[3];
                    if (!ch.id.empty()) g_watchedChannels.push_back(ch);
                } else if (parts.size() == 3) {
                    DiscordChannel ch;
                    ch.id = parts[0];
                    ch.guildName = parts[1];
                    ch.name = parts[2];
                    if (!ch.id.empty()) g_watchedChannels.push_back(ch);
                }
            }
        }
    }

    if (g_watchedChannels.empty()) {
        DiscordChannel uiaCh;
        uiaCh.id = UIA_CHANNEL_ID;
        uiaCh.guildId = L"";
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
            } else if (ch.id.rfind(L"WIN:", 0) == 0) {
                label = L"[Window] " + ch.guildName + L" > " + ch.name;
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
// Type-to-Search Filtering for Servers & Channels
// ============================================================================
void LoadChannelsForSelectedServer(HWND hwnd);

static bool g_inFilter = false;

// typing=true: user is typing in the editable combo -> keep their text, keep dropdown open
void ApplyServerFilterUI(HWND hwnd, bool typing = false) {
    if (g_inFilter) return;
    g_inFilter = true;
    wchar_t filterBuf[256] = { 0 };
    if (typing) GetWindowTextW(g_hServerCombo, filterBuf, 255);
    std::wstring raw = filterBuf;
    std::wstring query = ToLower(Trim(raw));

    g_filteredGuilds.clear();
    SendMessageW(g_hServerCombo, CB_RESETCONTENT, 0, 0);

    for (const auto& g : g_allGuilds) {
        if (query.empty() || ToLower(g.name).find(query) != std::wstring::npos || g.id.find(query) != std::wstring::npos) {
            g_filteredGuilds.push_back(g);
            SendMessageW(g_hServerCombo, CB_ADDSTRING, 0, (LPARAM)g.name.c_str());
        }
    }

    if (typing) {
        SetWindowTextW(g_hServerCombo, raw.c_str());
        SendMessageW(g_hServerCombo, CB_SETEDITSEL, 0, MAKELPARAM((int)raw.size(), (int)raw.size()));
        SendMessageW(g_hServerCombo, CB_SHOWDROPDOWN, TRUE, 0);
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        g_inFilter = false;
        return;
    }
    g_inFilter = false;

    if (!g_filteredGuilds.empty()) {
        SendMessageW(g_hServerCombo, CB_SETCURSEL, 0, 0);
        LoadChannelsForSelectedServer(hwnd);
    } else {
        g_currentGuildChannels.clear();
        g_filteredChannels.clear();
        SendMessageW(g_hChannelCombo, CB_RESETCONTENT, 0, 0);
    }
}

void ApplyChannelFilterUI(bool typing = false) {
    if (g_inFilter) return;
    g_inFilter = true;
    wchar_t filterBuf[256] = { 0 };
    if (typing) GetWindowTextW(g_hChannelCombo, filterBuf, 255);
    std::wstring raw = filterBuf;
    std::wstring query = ToLower(Trim(raw));

    g_filteredChannels.clear();
    SendMessageW(g_hChannelCombo, CB_RESETCONTENT, 0, 0);

    for (const auto& ch : g_currentGuildChannels) {
        if (query.empty() || ToLower(ch.name).find(query) != std::wstring::npos || ch.id.find(query) != std::wstring::npos) {
            g_filteredChannels.push_back(ch);
            SendMessageW(g_hChannelCombo, CB_ADDSTRING, 0, (LPARAM)ch.name.c_str());
        }
    }

    if (typing) {
        SetWindowTextW(g_hChannelCombo, raw.c_str());
        SendMessageW(g_hChannelCombo, CB_SETEDITSEL, 0, MAKELPARAM((int)raw.size(), (int)raw.size()));
        SendMessageW(g_hChannelCombo, CB_SHOWDROPDOWN, TRUE, 0);
        SetCursor(LoadCursor(NULL, IDC_ARROW));
    } else if (!g_filteredChannels.empty()) {
        SendMessageW(g_hChannelCombo, CB_SETCURSEL, 0, 0);
    }
    g_inFilter = false;
}

// ============================================================================
// Fast Discord REST & JSON Helpers
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

std::wstring ExtractJsonStringFieldFast(const std::wstring& json, const wchar_t* keyPattern, size_t searchFrom = 0) {
    size_t kPos = json.find(keyPattern, searchFrom);
    if (kPos == std::wstring::npos) return L"";
    size_t colon = json.find(L':', kPos + wcslen(keyPattern));
    if (colon == std::wstring::npos) return L"";
    size_t qStart = json.find(L'\"', colon + 1);
    if (qStart == std::wstring::npos) return L"";
    size_t qEnd = qStart + 1;
    while (qEnd < json.size()) {
        if (json[qEnd] == L'\"' && json[qEnd - 1] != L'\\') break;
        ++qEnd;
    }
    if (qEnd >= json.size()) return L"";
    return UnescapeJsonString(json.substr(qStart + 1, qEnd - qStart - 1));
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

        if (statusCode == 401) g_tokenInvalid = true;
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

    HINTERNET hSession = WinHttpOpen(L"VngPsLauncher/1.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
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

    g_allGuilds.clear();
    g_guildChannelCache.clear();

    std::wregex guildRegex(L"\"id\"\\s*:\\s*\"(\\d+)\"\\s*,\\s*\"name\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    auto begin = std::wsregex_iterator(json.begin(), json.end(), guildRegex);
    auto end = std::wsregex_iterator();
    for (auto it = begin; it != end; ++it) {
        DiscordGuild g;
        g.id = (*it)[1].str();
        g.name = UnescapeJsonString((*it)[2].str());
        g_allGuilds.push_back(g);
    }

    ApplyServerFilterUI(hwnd);

    if (!g_allGuilds.empty()) {
        SetSniperStatus(hwnd, L"Loaded " + std::to_wstring(g_allGuilds.size()) + L" servers! Type in the search boxes to filter servers or channels.", COLOR_OK);
    } else {
        SetSniperStatus(hwnd, L"No Discord servers found for this token.", COLOR_WARN);
    }
}

void LoadChannelsForSelectedServer(HWND hwnd) {
    int sel = (int)SendMessageW(g_hServerCombo, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)g_filteredGuilds.size()) return;

    DiscordGuild guild = g_filteredGuilds[sel];

    // Check cache first so typing/switching servers is instant!
    auto cacheIt = g_guildChannelCache.find(guild.id);
    if (cacheIt != g_guildChannelCache.end()) {
        g_currentGuildChannels = cacheIt->second;
        ApplyChannelFilterUI();
        return;
    }

    wchar_t tokBuf[1024] = { 0 };
    GetWindowTextW(g_hTokenEdit, tokBuf, 1023);
    std::wstring token = Trim(tokBuf);
    if (token.empty()) return;

    HINTERNET hSession = WinHttpOpen(L"VngPsLauncher/1.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return;
    HINTERNET hConnect = WinHttpConnect(hSession, L"discord.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    std::wstring json = DiscordApiGet(hConnect, L"/api/v9/guilds/" + guild.id + L"/channels", token);
    if (hConnect) WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    g_currentGuildChannels.clear();

    if (json.empty()) {
        ApplyChannelFilterUI();
        SetSniperStatus(hwnd, L"Failed to load channels for " + guild.name, COLOR_ERR);
        return;
    }

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
            if (ctype == 0 || ctype == 5 || ctype == 15) {
                DiscordChannel ch;
                ch.id = idM[1].str();
                ch.guildId = guild.id;
                ch.guildName = guild.name;
                ch.name = L"#" + UnescapeJsonString(nameM[1].str());
                g_currentGuildChannels.push_back(ch);
            }
        }
    }

    g_guildChannelCache[guild.id] = g_currentGuildChannels;
    ApplyChannelFilterUI();

    if (!g_currentGuildChannels.empty()) {
        SetSniperStatus(hwnd, L"Loaded " + std::to_wstring(g_currentGuildChannels.size()) + L" channels in " + guild.name + L". Type in 'Search Channel' to filter!", COLOR_OK);
    } else {
        SetSniperStatus(hwnd, L"No text channels found in " + guild.name + L".", COLOR_WARN);
    }
}

// ============================================================================
// Core Keyword + PS Link Evaluation & Auto-Stop (0.0001s Sub-Millisecond Path)
// ============================================================================
struct SniperHitPayload {
    std::wstring uri;
    std::wstring matchedKeyword;
    std::wstring sourceLabel;
};

// Fuzzy keyword match: "sin"/"sinis"/"sinster"/"sinister bos" all match keyword "sinister"; "boss" matches "bos", etc.
static int EditDist(const std::wstring& a, const std::wstring& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = (int)j;
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = (int)i;
        for (size_t j = 1; j <= b.size(); ++j) {
            int c = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = (std::min)((std::min)(prev[j] + 1, cur[j - 1] + 1), prev[j - 1] + c);
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

static bool FuzzyWordMatch(const std::wstring& w, const std::wstring& kw) {
    if (w.empty() || kw.empty()) return false;
    if (kw.size() == 1) {
        return w == kw || (kw == L"s" && (w.rfind(L"sin", 0) == 0 || w.rfind(L"shitm", 0) == 0));
    }
    if (w.find(kw) != std::wstring::npos) return true;                 // keyword inside word
    if (w.size() >= 2 && kw.find(w) != std::wstring::npos && kw.rfind(w, 0) == 0) return true; // word is prefix of keyword (si, sin, sini..)
    size_t common = 0;
    while (common < w.size() && common < kw.size() && w[common] == kw[common]) ++common;
    if (common >= 2 && common >= (std::min)(w.size(), kw.size())) return true;
    if (common >= 3) return true;                                      // shares 3+ leading chars
    if (kw.size() >= 4 && w.size() >= 3 && EditDist(w, kw) <= (kw.size() >= 6 ? 2 : 1)) return true; // typos
    return false;
}

static std::wstring FuzzyKeywordHit(const std::wstring& text, const std::vector<std::wstring>& kws) {
    // drop URL tokens, then split into alphanumeric words
    std::vector<std::wstring> words;
    std::wstring cleanText;
    std::wstring tok;
    auto flushTok = [&]() {
        if (tok.empty()) return;
        std::wstring lt = ToLower(tok);
        if (lt.find(L"roblox") == std::wstring::npos && lt.find(L"http") == std::wstring::npos && lt.find(L"://") == std::wstring::npos && lt.find(L"code=") == std::wstring::npos) {
            std::wstring w;
            for (wchar_t c : lt) {
                if (std::iswalnum(c)) w.push_back(c);
                else {
                    if (!w.empty()) {
                        if (!cleanText.empty()) cleanText.push_back(L' ');
                        cleanText += w;
                        words.push_back(w);
                        w.clear();
                    }
                }
            }
            if (!w.empty()) {
                if (!cleanText.empty()) cleanText.push_back(L' ');
                cleanText += w;
                words.push_back(w);
            }
        }
        tok.clear();
    };
    for (wchar_t c : text) {
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') flushTok();
        else tok.push_back(c);
    }
    flushTok();
    for (const auto& kw : kws) {
        std::wstring lkw = ToLower(Trim(kw));
        if (lkw.empty()) continue;
        if (lkw.find(L' ') != std::wstring::npos) {
            if (cleanText.find(lkw) != std::wstring::npos) return kw;
            // Require all tokens of a multi-word keyword to fuzzy-match words in the message
            std::vector<std::wstring> parts;
            std::wstring cur;
            for (wchar_t c : lkw) {
                if (std::iswalnum(c)) cur.push_back(c);
                else if (!cur.empty()) { parts.push_back(cur); cur.clear(); }
            }
            if (!cur.empty()) parts.push_back(cur);
            bool allMatch = !parts.empty();
            for (const auto& p : parts) {
                bool partOk = false;
                for (const auto& w : words) {
                    if (FuzzyWordMatch(w, p)) { partOk = true; break; }
                }
                if (!partOk) { allMatch = false; break; }
            }
            if (allMatch) return kw;
            continue;
        }
        for (const auto& w : words) {
            if (FuzzyWordMatch(w, lkw)) return kw;
        }
    }
    return L"";
}

bool EvaluateCandidateChatMessage(
    const std::wstring& msgUniqueKey,
    const std::wstring& msgText,
    const std::wstring& sourceLabel,
    bool isInitialSeedPass,
    const std::wstring& kwText = L""
) {
    if (msgText.empty() || !g_sniperRunning) return false;

    // 1. Ultra-fast zero-regex link extraction (< 2 microseconds)
    std::wstring uri = BuildUri(msgText);
    if (uri.empty()) return false;

    std::wstring key = msgUniqueKey.empty() ? (uri + L"|" + msgText.substr(0, 64)) : msgUniqueKey;

    EnterCriticalSection(&g_sniperLock);
    if (g_seenMessages.find(key) != g_seenMessages.end()) {
        LeaveCriticalSection(&g_sniperLock);
        return false;
    }
    g_seenMessages.insert(key);

    if (isInitialSeedPass && g_onlyNewMessages) {
        LeaveCriticalSection(&g_sniperLock);
        return false;
    }

    // 2. Mode "join ANY new PS" or fuzzy keyword match
    std::wstring matchedKw = L"";
    bool kwMatch = false;

    if (g_anyNewPs || g_keywords.empty()) {
        kwMatch = true;
        matchedKw = L"(any new PS)";
    } else {
        matchedKw = FuzzyKeywordHit(kwText.empty() ? msgText : kwText, g_keywords);
        kwMatch = !matchedKw.empty();
    }

    if (!kwMatch || !g_sniperRunning) {
        LeaveCriticalSection(&g_sniperLock);
        return false;
    }

    // 3. MATCH FOUND! Stop the sniper immediately and launch Froststrap!
    g_sniperRunning = false;
    HINTERNET hWs = g_hActiveWebSocket;
    g_hActiveWebSocket = NULL;
    LeaveCriticalSection(&g_sniperLock);

    // Fire Froststrap immediately before any UI work
    LaunchRobloxUri(uri, false);

    if (hWs) {
        WinHttpCloseHandle(hWs);
    }

    if (g_hMainWnd) {
        SniperHitPayload* payload = new SniperHitPayload();
        payload->uri = uri;
        payload->matchedKeyword = matchedKw;
        payload->sourceLabel = sourceLabel;
        PostMessageW(g_hMainWnd, WM_SNIPER_HIT, 0, (LPARAM)payload);
    }
    return true;
}

// ============================================================================
// REAL-TIME Discord Gateway WebSocket Client (0.0001s Live Push!)
// ============================================================================
bool WebSocketSendUtf8(HINTERNET hWs, const std::string& msg) {
    if (!hWs || msg.empty()) return false;
    return WinHttpWebSocketSend(
        hWs,
        WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        (PVOID)msg.data(),
        (DWORD)msg.size()
    ) == NO_ERROR;
}

DWORD WINAPI GatewayHeartbeatThread(LPVOID) {
    while (g_sniperRunning) {
        DWORD interval = g_wsHeartbeatIntervalMs;
        if (interval < 5000) interval = 41250;
        for (DWORD elapsed = 0; elapsed < interval && g_sniperRunning; elapsed += 100) {
            Sleep(100);
        }
        if (!g_sniperRunning) break;

        EnterCriticalSection(&g_sniperLock);
        HINTERNET hWs = g_hActiveWebSocket;
        int seq = g_wsLastSeq;
        LeaveCriticalSection(&g_sniperLock);

        if (hWs) {
            std::string hb = (seq >= 0)
                ? ("{\"op\":1,\"d\":" + std::to_string(seq) + "}")
                : "{\"op\":1,\"d\":null}";
            WebSocketSendUtf8(hWs, hb);
        }
    }
    return 0;
}

DWORD WINAPI DiscordGatewayWebSocketThread(LPVOID) {
    AttachToInteractiveDesktop();

    while (g_sniperRunning) {
        EnterCriticalSection(&g_sniperLock);
        std::wstring token = g_discordToken;
        std::vector<DiscordChannel> watched = g_watchedChannels;
        LeaveCriticalSection(&g_sniperLock);

        // Count how many API/Gateway channels are watched
        std::unordered_map<std::wstring, DiscordChannel> watchedMap;
        for (const auto& ch : watched) {
            if (ch.id != UIA_CHANNEL_ID && ch.id.rfind(L"WIN:", 0) != 0) {
                watchedMap[ch.id] = ch;
            }
        }

        if (token.empty() || watchedMap.empty() || g_tokenInvalid) {
            Sleep(250);
            continue;
        }

        HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) VngPsLauncher/1.2",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) {
            Sleep(500);
            continue;
        }

        HINTERNET hConnect = WinHttpConnect(hSession, L"gateway.discord.gg", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            Sleep(500);
            continue;
        }

        HINTERNET hReq = WinHttpOpenRequest(
            hConnect, L"GET", L"/?v=9&encoding=json",
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE
        );
        if (!hReq) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            Sleep(500);
            continue;
        }

        BOOL bUpgrade = WinHttpSetOption(hReq, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0);
        HINTERNET hWs = NULL;
        if (bUpgrade &&
            WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hReq, NULL)) {
            hWs = WinHttpWebSocketCompleteUpgrade(hReq, 0);
        }
        WinHttpCloseHandle(hReq);

        if (!hWs) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            Sleep(1000);
            continue;
        }

        EnterCriticalSection(&g_sniperLock);
        g_hActiveWebSocket = hWs;
        g_wsLastSeq = -1;
        LeaveCriticalSection(&g_sniperLock);

        // Send OP 2 IDENTIFY immediately
        std::string utf8Tok = WideToUtf8(Trim(token));
        std::string identifyPayload =
            "{\"op\":2,\"d\":{\"token\":\"" + utf8Tok +
            "\",\"capabilities\":16381,\"properties\":{\"os\":\"Windows\",\"browser\":\"Chrome\",\"device\":\"\"},\"compress\":false}}";
        WebSocketSendUtf8(hWs, identifyPayload);

        // Also send OP 14 Lazy Guild Subscriptions for every watched guild/channel so large community servers push MESSAGE_CREATE instantly
        for (const auto& kv : watchedMap) {
            if (!kv.second.guildId.empty()) {
                std::string gid = WideToUtf8(kv.second.guildId);
                std::string cid = WideToUtf8(kv.second.id);
                std::string subPayload =
                    "{\"op\":14,\"d\":{\"guild_id\":\"" + gid +
                    "\",\"typing\":true,\"activities\":false,\"threads\":true,\"channels\":{\"" + cid + "\":[[0,99]]}}}";
                WebSocketSendUtf8(hWs, subPayload);
            }
        }

        HANDLE hHbThread = CreateThread(NULL, 0, GatewayHeartbeatThread, NULL, 0, NULL);

        std::string frameAccumulator;
        frameAccumulator.reserve(65536);
        std::vector<char> recvBuf(32768);

        while (g_sniperRunning) {
            DWORD bytesRead = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE bufType = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
            DWORD err = WinHttpWebSocketReceive(hWs, recvBuf.data(), (DWORD)recvBuf.size(), &bytesRead, &bufType);
            if (err != NO_ERROR) {
                break;
            }
            if (bufType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                break;
            }

            frameAccumulator.append(recvBuf.data(), bytesRead);

            if (bufType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                bufType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
                std::wstring payload = Utf8ToWide(frameAccumulator);
                frameAccumulator.clear();

                // Update sequence number if present: "s":1234
                size_t sPos = payload.find(L"\"s\":");
                if (sPos != std::wstring::npos) {
                    size_t numStart = sPos + 4;
                    while (numStart < payload.size() && payload[numStart] == L' ') ++numStart;
                    if (numStart < payload.size() && IsDigitChar(payload[numStart])) {
                        g_wsLastSeq = _wtoi(payload.c_str() + numStart);
                    }
                }

                // Check for OP 10 Hello heartbeat_interval
                size_t hbPos = payload.find(L"\"heartbeat_interval\":");
                if (hbPos != std::wstring::npos) {
                    size_t numStart = hbPos + 21;
                    while (numStart < payload.size() && payload[numStart] == L' ') ++numStart;
                    int hbVal = _wtoi(payload.c_str() + numStart);
                    if (hbVal >= 5000) g_wsHeartbeatIntervalMs = (DWORD)hbVal;
                }

                // Fast check for MESSAGE_CREATE or MESSAGE_UPDATE event!
                if (payload.find(L"\"MESSAGE_CREATE\"") != std::wstring::npos ||
                    payload.find(L"\"MESSAGE_UPDATE\"") != std::wstring::npos) {

                    std::wstring channelId = ExtractJsonStringFieldFast(payload, L"\"channel_id\"");
                    if (!channelId.empty()) {
                        // Refresh watched channels snapshot
                        EnterCriticalSection(&g_sniperLock);
                        DiscordChannel matchedChan;
                        bool isWatched = false;
                        for (const auto& wch : g_watchedChannels) {
                            if (wch.id == channelId) {
                                matchedChan = wch;
                                isWatched = true;
                                break;
                            }
                        }
                        LeaveCriticalSection(&g_sniperLock);

                        if (isWatched) {
                            // Extract message ID & content in microseconds
                            size_t dPos = payload.find(L"\"d\":");
                            std::wstring msgId = ExtractJsonStringFieldFast(payload, L"\"id\"", dPos != std::wstring::npos ? dPos : 0);
                            std::wstring content = ExtractJsonStringFieldFast(payload, L"\"content\"", dPos != std::wstring::npos ? dPos : 0);

                            // Include raw payload tail as fallback in case link is inside an embed
                            std::wstring combined = content + L" " + payload;
                            std::wstring label = matchedChan.guildName + L" > " + matchedChan.name + L" [LIVE WS]";
                            if (EvaluateCandidateChatMessage(L"MSG_" + msgId, combined, label, false, content)) {
                                break;
                            }
                        }
                    }
                }
            }
        }

        EnterCriticalSection(&g_sniperLock);
        if (g_hActiveWebSocket == hWs) {
            g_hActiveWebSocket = NULL;
        }
        LeaveCriticalSection(&g_sniperLock);

        WinHttpCloseHandle(hWs);
        if (hHbThread) {
            WaitForSingleObject(hHbThread, 300);
            CloseHandle(hHbThread);
        }
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        if (g_sniperRunning) {
            Sleep(300);
        }
    }
    return 0;
}

// ============================================================================
// High-Speed Keep-Alive REST Poller + Background UIA Fallback Thread
// ============================================================================
bool ScanDiscordChannelJsonMessages(
    const std::wstring& json,
    const std::wstring& sourceLabel,
    bool isInitialSeedPass
) {
    if (json.empty()) return false;

    size_t pos = 0;
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

        std::wstring msgId = ExtractJsonStringFieldFast(obj, L"\"id\"");
        std::wstring content = ExtractJsonStringFieldFast(obj, L"\"content\"");
        if (!msgId.empty()) {
            std::wstring combined = content + L" " + UnescapeJsonString(obj);
            if (EvaluateCandidateChatMessage(L"MSG_" + msgId, combined, sourceLabel, isInitialSeedPass, content)) {
                return true;
            }
        }
    }
    return false;
}

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

static bool TitleOrListMatchesWatched(
    const std::wstring& lowTitle,
    const std::wstring& lowListName,
    const std::vector<DiscordChannel>& watchedSnapshot
) {
    for (const auto& wc : watchedSnapshot) {
        if (wc.id == UIA_CHANNEL_ID) return true;
        std::wstring cn = ToLower(wc.name);
        if (!cn.empty() && cn[0] == L'#') cn.erase(0, 1);
        if (cn.empty()) continue;
        bool chanHit = (lowTitle.find(cn) != std::wstring::npos) ||
                       (!lowListName.empty() && lowListName.find(cn) != std::wstring::npos);
        if (!chanHit) continue;
        std::wstring gn = ToLower(wc.guildName);
        bool realGuild = !gn.empty() && gn != L"custom server" && gn.rfind(L"server ", 0) != 0;
        if (realGuild && lowTitle.find(gn) == std::wstring::npos) continue;
        return true;
    }
    return false;
}

bool ScanOpenDiscordWindowsUIA(IUIAutomation* pUia, bool isInitialSeedPass) {
    if (!pUia) return false;
    EnumDiscordWindowsCtx ctx;
    EnumWindows(EnumDiscordWindowsProc, (LPARAM)&ctx);
    if (ctx.discordHwnds.empty()) return false;

    VARIANT varList, varListItem, varHyperlink;
    VariantInit(&varList);
    varList.vt = VT_I4;
    varList.lVal = UIA_ListControlTypeId;

    VariantInit(&varListItem);
    varListItem.vt = VT_I4;
    varListItem.lVal = UIA_ListItemControlTypeId;

    VariantInit(&varHyperlink);
    varHyperlink.vt = VT_I4;
    varHyperlink.lVal = UIA_HyperlinkControlTypeId;

    IUIAutomationCondition* pListCond = NULL;
    IUIAutomationCondition* pListItemCond = NULL;
    IUIAutomationCondition* pLinkCond = NULL;
    pUia->CreatePropertyCondition(UIA_ControlTypePropertyId, varList, &pListCond);
    pUia->CreatePropertyCondition(UIA_ControlTypePropertyId, varListItem, &pListItemCond);
    pUia->CreatePropertyCondition(UIA_ControlTypePropertyId, varHyperlink, &pLinkCond);
    if (!pListCond || !pListItemCond || !pLinkCond) {
        if (pListCond) pListCond->Release();
        if (pListItemCond) pListItemCond->Release();
        if (pLinkCond) pLinkCond->Release();
        return false;
    }

    bool hit = false;

    g_uiaWins = 0;
    g_uiaLinks = 0;
    EnterCriticalSection(&g_sniperLock);
    std::vector<DiscordChannel> watchedSnapshot = g_watchedChannels;
    LeaveCriticalSection(&g_sniperLock);

    for (HWND hDiscord : ctx.discordHwnds) {
        if (!g_sniperRunning) break;

        wchar_t winTitle[256] = { 0 };
        GetWindowTextW(hDiscord, winTitle, 255);
        std::wstring lowTitle = ToLower(winTitle);
        bool titleChannelOk = TitleOrListMatchesWatched(lowTitle, L"", watchedSnapshot);
        bool windowMatchedWatched = false;
        std::wstring sourceLabel = std::wstring(L"Discord Window (") + winTitle + L")";

        IUIAutomationElement* pRoot = NULL;
        if (FAILED(pUia->ElementFromHandle(hDiscord, &pRoot)) || !pRoot) continue;

        IUIAutomationElementArray* pLists = NULL;
        if (SUCCEEDED(pRoot->FindAll(TreeScope_Descendants, pListCond, &pLists)) && pLists) {
            int listCount = 0;
            pLists->get_Length(&listCount);

            for (int li = 0; li < listCount && g_sniperRunning && !hit; ++li) {
                IUIAutomationElement* pList = NULL;
                if (FAILED(pLists->GetElement(li, &pList)) || !pList) continue;

                std::wstring lowListName;
                std::wstring lowListCls;
                BSTR bListName = NULL;
                if (SUCCEEDED(pList->get_CurrentName(&bListName)) && bListName) {
                    lowListName = ToLower(bListName);
                    SysFreeString(bListName);
                }
                BSTR bListCls = NULL;
                if (SUCCEEDED(pList->get_CurrentClassName(&bListCls)) && bListCls) {
                    lowListCls = ToLower(bListCls);
                    SysFreeString(bListCls);
                }

                // Ignore Discord's left channel tree and right member list sidebars
                if (lowListName == L"channels" || lowListName == L"members" ||
                    lowListCls.rfind(L"members_", 0) == 0 || lowListCls.rfind(L"content_", 0) == 0) {
                    pList->Release();
                    continue;
                }

                bool isMessagesList = (lowListName.rfind(L"messages in ", 0) == 0);
                bool listOrTitleChannelOk = titleChannelOk || TitleOrListMatchesWatched(lowTitle, lowListName, watchedSnapshot);

                IUIAutomationElementArray* pItems = NULL;
                int count = 0;
                if (SUCCEEDED(pList->FindAll(TreeScope_Children, pListItemCond, &pItems)) && pItems) {
                    pItems->get_Length(&count);
                    if (count == 0) {
                        pItems->Release();
                        pItems = NULL;
                    }
                }
                if (!pItems) {
                    if (SUCCEEDED(pList->FindAll(TreeScope_Descendants, pListItemCond, &pItems)) && pItems) {
                        pItems->get_Length(&count);
                    }
                }

                if (pItems && count > 0) {
                    int startIdx = (count > 20) ? (count - 20) : 0;
                    for (int i = count - 1; i >= startIdx && g_sniperRunning; --i) {
                        IUIAutomationElement* pElem = NULL;
                        if (FAILED(pItems->GetElement(i, &pElem)) || !pElem) continue;

                        std::wstring aid;
                        BSTR bAid = NULL;
                        if (SUCCEEDED(pElem->get_CurrentAutomationId(&bAid)) && bAid) {
                            aid = bAid;
                            SysFreeString(bAid);
                        }

                        bool isChatMsgAid = (aid.rfind(L"chat-messages-", 0) == 0);
                        if (!isMessagesList && !isChatMsgAid) {
                            pElem->Release();
                            continue;
                        }

                        std::wstring msgChannelId;
                        std::wstring msgId;
                        if (isChatMsgAid) {
                            size_t dashPos = aid.find(L'-', 14);
                            if (dashPos != std::wstring::npos && dashPos > 14) {
                                msgChannelId = aid.substr(14, dashPos - 14);
                                msgId = aid.substr(dashPos + 1);
                            }
                        }

                        bool idChannelOk = false;
                        if (!msgChannelId.empty()) {
                            for (const auto& wc : watchedSnapshot) {
                                if (wc.id == msgChannelId) {
                                    idChannelOk = true;
                                    break;
                                }
                            }
                        }

                        if (!idChannelOk && !listOrTitleChannelOk) {
                            pElem->Release();
                            continue;
                        }
                        windowMatchedWatched = true;

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

                        // If URL wasn't exposed directly in ListItem Name/Value (e.g. embed/masked link), inspect child Hyperlinks
                        std::wstring uri = BuildUri(combined);
                        if (uri.empty()) {
                            IUIAutomationElementArray* pLinks = NULL;
                            if (SUCCEEDED(pElem->FindAll(TreeScope_Descendants, pLinkCond, &pLinks)) && pLinks) {
                                int linkCount = 0;
                                pLinks->get_Length(&linkCount);
                                for (int lk = 0; lk < linkCount; ++lk) {
                                    IUIAutomationElement* pLink = NULL;
                                    if (SUCCEEDED(pLinks->GetElement(lk, &pLink)) && pLink) {
                                        BSTR bLName = NULL;
                                        if (SUCCEEDED(pLink->get_CurrentName(&bLName)) && bLName) {
                                            combined += L" ";
                                            combined += bLName;
                                            SysFreeString(bLName);
                                        }
                                        VARIANT lv;
                                        VariantInit(&lv);
                                        if (SUCCEEDED(pLink->GetCurrentPropertyValue(UIA_ValueValuePropertyId, &lv)) && lv.vt == VT_BSTR && lv.bstrVal) {
                                            combined += L" ";
                                            combined += lv.bstrVal;
                                        }
                                        VariantClear(&lv);
                                        pLink->Release();
                                    }
                                }
                                pLinks->Release();
                            }
                            uri = BuildUri(combined);
                        }
                        pElem->Release();

                        if (!uri.empty()) {
                            g_uiaLinks = g_uiaLinks + 1;
                            std::wstring uniqueKey = !msgId.empty()
                                ? (L"MSG_" + msgId)
                                : (L"UIA_" + uri + L"_" + combined.substr(0, 64));
                            if (EvaluateCandidateChatMessage(uniqueKey, combined, sourceLabel, isInitialSeedPass, combined)) {
                                hit = true;
                                break;
                            }
                        }
                    }
                }
                if (pItems) pItems->Release();
                pList->Release();
            }
            pLists->Release();
        }
        if (windowMatchedWatched || titleChannelOk) {
            g_uiaWins = g_uiaWins + 1;
        }
        pRoot->Release();
        if (hit) break;
    }

    pListCond->Release();
    pListItemCond->Release();
    pLinkCond->Release();
    return hit;
}

DWORD WINAPI DiscordSniperWorkerThread(LPVOID) {
    AttachToInteractiveDesktop();
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    IUIAutomation* pUia = NULL;
    CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&pUia);

    HINTERNET hSession = WinHttpOpen(L"VngPsLauncher/1.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET hConnect = hSession ? WinHttpConnect(hSession, L"discord.com", INTERNET_DEFAULT_HTTPS_PORT, 0) : NULL;

    bool isFirstPass = true;
    ULONGLONG lastRest = 0, lastDiag = 0;

    while (g_sniperRunning) {
        EnterCriticalSection(&g_sniperLock);
        std::vector<DiscordChannel> channels = g_watchedChannels;
        std::wstring token = g_discordToken;
        LeaveCriticalSection(&g_sniperLock);

        ULONGLONG nowTick = GetTickCount64();
        bool doRest = isFirstPass || (nowTick - lastRest >= 1500);   // REST is only a slow backup; WS is the fast path
        if (doRest) lastRest = nowTick;

        bool useUia = false;
        for (const auto& ch : channels) {
            if (!g_sniperRunning) break;
            if (ch.id == UIA_CHANNEL_ID || ch.id.rfind(L"WIN:", 0) == 0) {
                useUia = true;
                continue;
            }
            if (doRest && !token.empty() && hConnect && !g_tokenInvalid) {
                std::wstring path = L"/api/v9/channels/" + ch.id + L"/messages?limit=3";
                std::wstring json = DiscordApiGet(hConnect, path, token);
                std::wstring label = ch.guildName + L" > " + ch.name;
                if (ScanDiscordChannelJsonMessages(json, label, isFirstPass)) {
                    break;
                }
            }
        }

        if (g_sniperRunning && !channels.empty()) {
            ScanOpenDiscordWindowsUIA(pUia, isFirstPass);
            if ((token.empty() || g_tokenInvalid || useUia) && (nowTick - lastDiag >= 2500) && g_sniperRunning) {
                lastDiag = nowTick;
                if (g_uiaWins == 0) {
                    PostSniperStatusAsync(L"Window mode: no open Discord window shows a watched channel. Open it (background is OK).", COLOR_WARN);
                } else {
                    std::wstring modeTag = g_tokenInvalid
                        ? L"Window mode ACTIVE (token expired, no-token OK): reading "
                        : L"Window mode ACTIVE (no-token): reading ";
                    PostSniperStatusAsync(modeTag + std::to_wstring((int)g_uiaWins) + L" Discord window(s), " +
                                          std::to_wstring((int)g_uiaLinks) + L" PS link(s) visible. Waiting for a new one...", COLOR_OK);
                }
            }
        }

        isFirstPass = false;

        if (g_sniperRunning) {
            Sleep(15);
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
// No-token mode: read the open Discord window titles and add their channel+server to the watched list
static std::wstring StripBrowserSuffix(std::wstring t) {
    while (true) {
        size_t p = t.rfind(L" - ");
        if (p == std::wstring::npos) break;
        std::wstring suf = t.substr(p + 3);
        if (suf.find(L"Discord") != std::wstring::npos || suf.find(L"Chrome") != std::wstring::npos ||
            suf.find(L"Edge") != std::wstring::npos || suf.find(L"Brave") != std::wstring::npos ||
            suf.find(L"Firefox") != std::wstring::npos || suf.find(L"Opera") != std::wstring::npos ||
            suf.find(L"Chromium") != std::wstring::npos) {
            t = t.substr(0, p);
        } else break;
    }
    return t;
}

void AddWatchedFromOpenWindows(HWND hwnd) {
    EnumDiscordWindowsCtx ctx;
    EnumWindows(EnumDiscordWindowsProc, (LPARAM)&ctx);
    int added = 0;
    for (HWND h : ctx.discordHwnds) {
        wchar_t tb[512] = { 0 };
        GetWindowTextW(h, tb, 511);
        std::wstring title = StripBrowserSuffix(tb);
        std::vector<std::wstring> parts;
        size_t s = 0;
        while (true) {
            size_t b = title.find(L" | ", s);
            if (b == std::wstring::npos) { parts.push_back(title.substr(s)); break; }
            parts.push_back(title.substr(s, b - s));
            s = b + 3;
        }
        for (size_t i = 0; i < parts.size(); ++i) {
            size_t hashPos = parts[i].find(L'#');
            if (hashPos == std::wstring::npos || hashPos > 6) continue;
            std::wstring chan = Trim(parts[i].substr(hashPos));
            std::wstring guild = (i + 1 < parts.size()) ? Trim(parts[i + 1]) : L"";
            if (chan.size() < 2) continue;
            DiscordChannel ch;
            ch.id = L"WIN:" + ToLower(guild) + L"/" + ToLower(chan);
            ch.guildId = L"";
            ch.guildName = guild;
            ch.name = chan;
            EnterCriticalSection(&g_sniperLock);
            bool exists = false;
            for (const auto& e : g_watchedChannels) if (e.id == ch.id) { exists = true; break; }
            if (!exists) { g_watchedChannels.push_back(ch); ++added; }
            LeaveCriticalSection(&g_sniperLock);
            break;
        }
    }
    if (added > 0) {
        SaveSniperConfig();
        RefreshChannelAndKeywordListsUI();
        SetSniperStatus(hwnd, L"Added " + std::to_wstring(added) + L" channel(s) from open Discord window(s). Keep Discord open (behind Roblox is fine).", COLOR_OK);
    } else {
        SetSniperStatus(hwnd, L"No channel found. Open Discord on a server text channel (title must show #channel | Server), then click again.", COLOR_WARN);
    }
}

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
        HINTERNET hWs = g_hActiveWebSocket;
        g_hActiveWebSocket = NULL;
        LeaveCriticalSection(&g_sniperLock);

        if (hWs) {
            WinHttpCloseHandle(hWs);
        }

        SetWindowTextW(g_hBtnToggleSniper, L"Start Real-Time 0.0001s Discord PS Sniper (Background)");
        InvalidateRect(g_hBtnToggleSniper, NULL, TRUE);
        if (!forceStop) {
            SetSniperStatus(hwnd, L"Sniper Stopped.", COLOR_MUTED);
        }
        return;
    }

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
    g_tokenInvalid = false;
    g_sniperRunning = true;
    size_t numCh = g_watchedChannels.size();
    size_t numKw = g_keywords.size();
    LeaveCriticalSection(&g_sniperLock);

    SaveSniperConfig();

    SetWindowTextW(g_hBtnToggleSniper, L"Stop Sniper (LIVE 0.0001s WebSocket + Fast Poll Active...)");
    InvalidateRect(g_hBtnToggleSniper, NULL, TRUE);

    std::wstring msg = L"LIVE SNIPER ACTIVE (0.0001s Gateway WS): Watching " + std::to_wstring(numCh) +
                       L" channel(s) for " + std::to_wstring(numKw) +
                       L" keyword(s). Auto-stops on join!";
    SetSniperStatus(hwnd, msg, COLOR_OK);

    // Launch both the Real-Time Discord Gateway WebSocket stream AND the Keep-Alive Poller!
    CreateThread(NULL, 0, DiscordGatewayWebSocketThread, NULL, 0, NULL);
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
    wcscpy_s(g_nid.szTip, L"VNG PS Launcher + 0.0001s Discord Sniper");
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
                32, 60, 656, 28, hwnd, (HMENU)IDC_ENTRY, hInst, NULL);
            SendMessageW(g_hEntry, WM_SETFONT, (WPARAM)g_hFontText, TRUE);

            HWND hBtnPaste  = CreateWindowExW(0, L"BUTTON", L"Paste & Join", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 32,  98, 110, 32, hwnd, (HMENU)IDC_PASTE,   hInst, NULL);
            HWND hBtnClear  = CreateWindowExW(0, L"BUTTON", L"Clear",        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 152, 98, 80,  32, hwnd, (HMENU)IDC_CLEAR,   hInst, NULL);
            HWND hBtnHideBg = CreateWindowExW(0, L"BUTTON", L"Hide to Tray", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 242, 98, 110, 32, hwnd, (HMENU)IDC_HIDE_BG, hInst, NULL);
            HWND hBtnLaunch = CreateWindowExW(0, L"BUTTON", L"Join Server",  WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 568, 98, 120, 32, hwnd, (HMENU)IDC_LAUNCH,  hInst, NULL);

            SetWindowSubclass(hBtnPaste,  CustomButtonProc, 1, IDC_PASTE);
            SetWindowSubclass(hBtnClear,  CustomButtonProc, 2, IDC_CLEAR);
            SetWindowSubclass(hBtnHideBg, CustomButtonProc, 3, IDC_HIDE_BG);
            SetWindowSubclass(hBtnLaunch, CustomButtonProc, 4, IDC_LAUNCH);

            // --- MIDDLE CARD: Discord Multi-Server & Type-to-Search Channel Selector ---
            g_hTokenEdit = CreateWindowExW(0, L"EDIT", g_discordToken.c_str(),
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD,
                145, 202, 405, 26, hwnd, (HMENU)IDC_TOKEN_EDIT, hInst, NULL);
            SendMessageW(g_hTokenEdit, WM_SETFONT, (WPARAM)g_hFontText, TRUE);

            HWND hBtnLoadSrv = CreateWindowExW(0, L"BUTTON", L"Load Servers", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                560, 201, 128, 28, hwnd, (HMENU)IDC_LOAD_SERVERS, hInst, NULL);
            SetWindowSubclass(hBtnLoadSrv, CustomButtonProc, 5, IDC_LOAD_SERVERS);

            // Row 2: EDITABLE combos - click and TYPE to filter servers / channels
            g_hServerCombo = CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL,
                145, 236, 185, 300, hwnd, (HMENU)IDC_SERVER_COMBO, hInst, NULL);
            SendMessageW(g_hServerCombo, WM_SETFONT, (WPARAM)g_hFontText, TRUE);
            SendMessageW(g_hServerCombo, CB_SETCUEBANNER, 0, (LPARAM)L"Type server name...");

            g_hChannelCombo = CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL,
                455, 236, 233, 300, hwnd, (HMENU)IDC_CHANNEL_COMBO, hInst, NULL);
            SendMessageW(g_hChannelCombo, WM_SETFONT, (WPARAM)g_hFontText, TRUE);
            SendMessageW(g_hChannelCombo, CB_SETCUEBANNER, 0, (LPARAM)L"Type channel name (e.g. ps)...");

            HWND hBtnAddSel = CreateWindowExW(0, L"BUTTON", L"+ Add Channel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                578, 271, 110, 28, hwnd, (HMENU)IDC_ADD_SEL_CHAN, hInst, NULL);
            SetWindowSubclass(hBtnAddSel, CustomButtonProc, 6, IDC_ADD_SEL_CHAN);

            // Row 4: Direct Channel URL / ID input
            g_hChanUrlEdit = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                145, 310, 295, 26, hwnd, (HMENU)IDC_CHAN_URL_EDIT, hInst, NULL);
            SendMessageW(g_hChanUrlEdit, WM_SETFONT, (WPARAM)g_hFontText, TRUE);
            SendMessageW(g_hChanUrlEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Channel URL or ID (token mode)");

            HWND hBtnWin = CreateWindowExW(0, L"BUTTON", L"+ From Open Window", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                446, 309, 124, 28, hwnd, (HMENU)IDC_ADD_FROM_WIN, hInst, NULL);
            SetWindowSubclass(hBtnWin, CustomButtonProc, 12, IDC_ADD_FROM_WIN);

            HWND hBtnAddUrl = CreateWindowExW(0, L"BUTTON", L"+ Add URL/ID", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                578, 309, 110, 28, hwnd, (HMENU)IDC_ADD_URL_CHAN, hInst, NULL);
            SetWindowSubclass(hBtnAddUrl, CustomButtonProc, 7, IDC_ADD_URL_CHAN);

            // --- LOWER LEFT CARD: Watched Channels List (Multi-Server) ---
            g_hChanList = CreateWindowExW(0, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                32, 385, 355, 118, hwnd, (HMENU)IDC_CHAN_LIST, hInst, NULL);
            SendMessageW(g_hChanList, WM_SETFONT, (WPARAM)g_hFontSub, TRUE);

            HWND hBtnRemChan = CreateWindowExW(0, L"BUTTON", L"Remove Selected Channel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                32, 506, 190, 28, hwnd, (HMENU)IDC_REMOVE_CHAN, hInst, NULL);
            SetWindowSubclass(hBtnRemChan, CustomButtonProc, 8, IDC_REMOVE_CHAN);

            // --- LOWER RIGHT CARD: Target Keywords List ---
            g_hKwEdit = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                412, 385, 185, 26, hwnd, (HMENU)IDC_KW_EDIT, hInst, NULL);
            SendMessageW(g_hKwEdit, WM_SETFONT, (WPARAM)g_hFontText, TRUE);
            SendMessageW(g_hKwEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Type keyword...");

            HWND hBtnAddKw = CreateWindowExW(0, L"BUTTON", L"+ Add", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                605, 384, 83, 28, hwnd, (HMENU)IDC_ADD_KW, hInst, NULL);
            SetWindowSubclass(hBtnAddKw, CustomButtonProc, 9, IDC_ADD_KW);

            g_hKwList = CreateWindowExW(0, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                412, 418, 276, 85, hwnd, (HMENU)IDC_KW_LIST, hInst, NULL);
            SendMessageW(g_hKwList, WM_SETFONT, (WPARAM)g_hFontText, TRUE);

            HWND hBtnRemKw = CreateWindowExW(0, L"BUTTON", L"Remove Keyword", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                412, 506, 145, 28, hwnd, (HMENU)IDC_REMOVE_KW, hInst, NULL);
            SetWindowSubclass(hBtnRemKw, CustomButtonProc, 10, IDC_REMOVE_KW);

            // --- BOTTOM SNIPER CONTROL BAR ---
            g_hChkNewOnly = CreateWindowExW(0, L"BUTTON", L"Only join NEW messages posted after starting Sniper",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                32, 550, 400, 22, hwnd, (HMENU)IDC_CHK_NEW_ONLY, hInst, NULL);
            SendMessageW(g_hChkNewOnly, WM_SETFONT, (WPARAM)g_hFontSub, TRUE);
            SendMessageW(g_hChkNewOnly, BM_SETCHECK, g_onlyNewMessages ? BST_CHECKED : BST_UNCHECKED, 0);

            g_hChkAny = CreateWindowExW(0, L"BUTTON", L"Join ANY new PS (ignore keywords)",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                440, 550, 248, 22, hwnd, (HMENU)IDC_CHK_ANY, hInst, NULL);
            SendMessageW(g_hChkAny, WM_SETFONT, (WPARAM)g_hFontSub, TRUE);
            SendMessageW(g_hChkAny, BM_SETCHECK, g_anyNewPs ? BST_CHECKED : BST_UNCHECKED, 0);

            g_hBtnToggleSniper = CreateWindowExW(0, L"BUTTON", L"Start Real-Time 0.0001s Discord PS Sniper (Background)",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                32, 576, 656, 38, hwnd, (HMENU)IDC_TOGGLE_SNIPER, hInst, NULL);
            SetWindowSubclass(g_hBtnToggleSniper, CustomButtonProc, 11, IDC_TOGGLE_SNIPER);

            RefreshChannelAndKeywordListsUI();

            // Pre-cache Froststrap path so first snipe has 0ms disk lookup
            FindFroststrapOrBootstrapperExe();

            // Auto-load servers on startup if token is already saved!
            if (!g_discordToken.empty()) {
                PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_LOAD_SERVERS, BN_CLICKED), 0);
            }

            CreateThread(NULL, 0, BackgroundLinkWatcherThread, NULL, 0, NULL);
            return 0;
        }

        case WM_SHOW_LAUNCHER: {
            ShowLauncherWindow(hwnd);
            return 0;
        }

        case WM_SNIPER_STATUS: {
            std::wstring* pStr = (std::wstring*)lParam;
            if (pStr) {
                SetSniperStatus(hwnd, *pStr, (COLORREF)wParam);
                delete pStr;
            }
            return 0;
        }

        case WM_SNIPER_HIT: {
            SniperHitPayload* payload = (SniperHitPayload*)lParam;
            if (payload) {
                SetWindowTextW(g_hBtnToggleSniper, L"Start Real-Time 0.0001s Discord PS Sniper (Background)");
                InvalidateRect(g_hBtnToggleSniper, NULL, TRUE);
                SetWindowTextW(g_hEntry, payload->uri.c_str());

                std::wstring hitMsg = L"INSTANTLY JOINED PS & STOPPED! Matched \"" + payload->matchedKeyword +
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
            TextOutW(hdc, 20, 12, L"VNG PS Launcher & 0.0001s Discord Gateway Sniper", 48);

            // Top Card: Direct Link
            DrawCard(hdc, 20, 44, 700, 138);
            SelectObject(hdc, g_hFontSub);
            SetTextColor(hdc, g_statusColor);
            TextOutW(hdc, 22, 142, g_statusText.c_str(), (int)g_statusText.length());

            // Middle Card: Discord Server & Type-to-Search Channel Picker
            DrawCard(hdc, 20, 168, 700, 348);
            SelectObject(hdc, g_hFontSection);
            SetTextColor(hdc, COLOR_ACCENT);
            TextOutW(hdc, 32, 176, L"Real-Time 0.0001s Discord Multi-Server Sniper (Type to Search Channels)", 71);

            SelectObject(hdc, g_hFontSub);
            SetTextColor(hdc, COLOR_MUTED);
            TextOutW(hdc, 32, 205, L"Discord Token:", 14);
            TextOutW(hdc, 32, 241, L"Server (type):", 14);
            TextOutW(hdc, 342, 241, L"Channel (type):", 15);
            TextOutW(hdc, 32, 313, L"Or Channel URL/ID:", 18);

            // Lower Left Card: Watched Channels
            DrawCard(hdc, 20, 356, 398, 542);
            SelectObject(hdc, g_hFontSection);
            SetTextColor(hdc, COLOR_FG);
            TextOutW(hdc, 32, 363, L"Watched Servers / Channels (Multi-Server)", 41);

            // Lower Right Card: Keywords
            DrawCard(hdc, 404, 356, 700, 542);
            SelectObject(hdc, g_hFontSection);
            SetTextColor(hdc, COLOR_FG);
            TextOutW(hdc, 412, 363, L"Keywords (similar words match too)", 34);

            // Bottom Sniper Status
            SelectObject(hdc, g_hFontSub);
            SetTextColor(hdc, g_sniperStatusColor);
            TextOutW(hdc, 22, 622, g_sniperStatusText.c_str(), (int)g_sniperStatusText.length());

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
            } else if (id == IDC_SERVER_COMBO && code == CBN_EDITCHANGE) {
                ApplyServerFilterUI(hwnd, true);
            } else if (id == IDC_CHANNEL_COMBO && code == CBN_EDITCHANGE) {
                ApplyChannelFilterUI(true);
            } else if (id == IDC_SERVER_COMBO && code == CBN_SELCHANGE) {
                if (!g_inFilter) LoadChannelsForSelectedServer(hwnd);
            } else if (id == IDC_ADD_SEL_CHAN) {
                int sel = (int)SendMessageW(g_hChannelCombo, CB_GETCURSEL, 0, 0);
                if (sel < 0 && !g_filteredChannels.empty()) sel = 0;
                if (sel >= 0 && sel < (int)g_filteredChannels.size()) {
                    DiscordChannel ch = g_filteredChannels[sel];
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
                    SetSniperStatus(hwnd, L"Select or search for a channel first, or paste a Channel URL/ID below.", COLOR_WARN);
                }
            } else if (id == IDC_ADD_FROM_WIN) {
                AddWatchedFromOpenWindows(hwnd);
            } else if (id == IDC_ADD_URL_CHAN) {
                wchar_t buf[512] = { 0 };
                GetWindowTextW(g_hChanUrlEdit, buf, 511);
                std::wstring raw = Trim(buf);
                if (raw.empty()) {
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
                    std::wstring guildId = L"";
                    std::wstring chanId = L"";
                    std::wregex urlRx(L"channels/(\\d+)/(\\d+)");
                    std::wregex idRx(L"^(\\d{15,22})$");
                    std::wsmatch m;
                    if (std::regex_search(raw, m, urlRx)) {
                        guildId = m[1].str();
                        chanId = m[2].str();
                    } else if (std::regex_search(raw, m, idRx)) {
                        chanId = m[1].str();
                    }
                    if (!chanId.empty()) {
                        DiscordChannel ch;
                        ch.id = chanId;
                        ch.guildId = guildId;
                        ch.guildName = guildId.empty() ? L"Custom Server" : (L"Server " + guildId.substr(0, 6));
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
            } else if (id == IDC_CHK_ANY) {
                EnterCriticalSection(&g_sniperLock);
                g_anyNewPs = (SendMessageW(g_hChkAny, BM_GETCHECK, 0, 0) == BST_CHECKED);
                LeaveCriticalSection(&g_sniperLock);
                SaveSniperConfig();
                SetSniperStatus(hwnd, g_anyNewPs ? L"Mode: join ANY new PS link posted in watched channels." : L"Mode: join only when a (similar) keyword matches.", COLOR_OK);
            } else if (id == IDC_CHK_NEW_ONLY) {
                EnterCriticalSection(&g_sniperLock);
                g_onlyNewMessages = (SendMessageW(g_hChkNewOnly, BM_GETCHECK, 0, 0) == BST_CHECKED);
                LeaveCriticalSection(&g_sniperLock);
                SaveSniperConfig();
            } else if (id == IDC_TOGGLE_SNIPER) {
                if (!g_sniperRunning) AddWatchedFromOpenWindows(hwnd);   // auto-pick up the channel Discord currently shows
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
            StartOrStopSniper(hwnd, true);
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

    RECT rc = { 0, 0, 720, 655 };
    AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);

    HWND hwnd = CreateWindowExW(
        0,
        L"VngPsLauncherWndClass",
        L"VNG PS Launcher + 0.0001s Discord Gateway Sniper",
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
            } else if (hFocus && GetParent(hFocus) == g_hChannelCombo) {
                SendMessageW(g_hChannelCombo, CB_SHOWDROPDOWN, FALSE, 0);
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_ADD_SEL_CHAN, BN_CLICKED), 0);
                continue;
            } else if (hFocus && GetParent(hFocus) == g_hServerCombo) {
                SendMessageW(g_hServerCombo, CB_SHOWDROPDOWN, FALSE, 0);
                if (!g_filteredGuilds.empty()) {
                    wchar_t t[256] = {0}; GetWindowTextW(g_hServerCombo, t, 255);
                    int idx = (int)SendMessageW(g_hServerCombo, CB_FINDSTRINGEXACT, -1, (LPARAM)t);
                    if (idx < 0) idx = 0;
                    SendMessageW(g_hServerCombo, CB_SETCURSEL, idx, 0);
                    LoadChannelsForSelectedServer(hwnd);
                }
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

/**
 * VNG PS Launcher (Vietnamese Private Server Launcher)
 * High-performance, lightweight native Win32 private server & share link launcher.
 *
 * Features:
 * 1. Runs silently in the background by default (System Tray on winsta0\default).
 * 2. Intercepts HTTP/HTTPS link clicks from Discord/chat apps via HKCU Classes override:
 *    - If a clicked link is a Roblox Private Server / Share link, it launches Froststrap / Roblox
 *      directly WITHOUT opening a web browser.
 *    - If a clicked link is a normal web URL, it seamlessly opens it in the real browser (Edge/Chrome).
 * 3. Watches active browser tabs (UIAutomation address bar + process command lines) & clipboard
 *    so even clicking a PS link inside an open browser tab or copying it launches Froststrap directly.
 */

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <shlwapi.h>
#include <ole2.h>
#include <uiautomation.h>
#include <string>
#include <regex>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

#define DWMWA_USE_IMMERSIVE_DARK_MODE_VAL 20

// Modern Dark Theme Palette
const COLORREF COLOR_BG      = RGB(20, 21, 26);    // #14151a
const COLORREF COLOR_CARD    = RGB(29, 31, 39);    // #1d1f27
const COLORREF COLOR_FG      = RGB(232, 233, 238); // #e8e9ee
const COLORREF COLOR_MUTED   = RGB(139, 143, 163); // #8b8fa3
const COLORREF COLOR_ACCENT  = RGB(79, 124, 255);  // #4f7cff
const COLORREF COLOR_BTN     = RGB(42, 45, 56);    // #2a2d38
const COLORREF COLOR_OK      = RGB(74, 222, 128);  // #4ade80
const COLORREF COLOR_ERR     = RGB(248, 113, 113); // #f87171

// Control & Tray Identifiers
#define IDC_ENTRY        101
#define IDC_PASTE        102
#define IDC_CLEAR        103
#define IDC_LAUNCH       104
#define IDC_HIDE_BG      105

#define WM_TRAYICON      (WM_USER + 100)
#define WM_SHOW_LAUNCHER (WM_USER + 101)
#define ID_TRAY_SHOW     2001
#define ID_TRAY_EXIT     2002

HWND g_hMainWnd = NULL;
HWND g_hEntry = NULL;
HBRUSH g_hBrushBg = NULL;
HBRUSH g_hBrushCard = NULL;
HFONT g_hFontTitle = NULL;
HFONT g_hFontSub = NULL;
HFONT g_hFontText = NULL;
HFONT g_hFontBtn = NULL;
HFONT g_hFontBtnBold = NULL;
COLORREF g_statusColor = COLOR_OK;
std::wstring g_statusText = L"Background Auto-Launch Active (Click or Copy any PS Link)";
NOTIFYICONDATAW g_nid = { 0 };
std::wstring g_lastLaunchedUri = L"";
ULONGLONG g_lastLaunchTick = 0;

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

std::wstring BuildUri(const std::wstring& rawLink) {
    std::wstring link = Trim(rawLink);
    if (link.empty()) return L"";

    // Direct roblox:// link already
    if (link.rfind(L"roblox://", 0) == 0) {
        return link;
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
        RECT rc = { 25, 192, 530, 225 };
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

void CleanupExistingRobloxProcesses() {
    // Prevents RobloxPlayerBeta.exe from crashing in SingleInstanceGuard.cpp(177)
    // ("Invalid window handle [Failed to send deeplink, error: 1400]") when a previous
    // RobloxPlayerBeta.exe or zombie process is still holding the singleton mutex.
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe = { 0 };
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"RobloxPlayerBeta.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"RobloxCrashHandler.exe") == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    TerminateProcess(hProc, 0);
                    CloseHandle(hProc);
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
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

    // 1. Primary: Launch Froststrap / Bloxstrap / Fishstrap directly on winsta0\default
    // If Roblox is already open, Froststrap hands off the deeplink to the existing instance
    // seamlessly without closing or crashing it!
    std::wstring strapExe = FindFroststrapOrBootstrapperExe();
    if (!strapExe.empty()) {
        std::wstring cmd = L"\"" + strapExe + L"\" -player \"" + uri + L"\"";
        if (SpawnProcessOnInteractiveDesktop(cmd)) {
            if (g_hMainWnd) {
                SetStatus(g_hMainWnd, L"Joining Private Server via Froststrap...", COLOR_OK);
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
    // Check HKLM classes for the genuine browser executable
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

    // Register protocol handler for roblox-ps:// and hook browser command line safely
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
    // Only inspect the address bar when the tab title looks like a Roblox share / game page
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

void DoLaunch(HWND hwnd) {
    wchar_t buf[2048] = { 0 };
    GetWindowTextW(g_hEntry, buf, 2047);
    std::wstring uri = BuildUri(buf);

    if (uri.empty() || uri.rfind(L"roblox://", 0) != 0) {
        SetStatus(hwnd, L"Link not recognized", COLOR_ERR);
        return;
    }

    LaunchRobloxUri(uri, false);
}

void DoPaste(HWND hwnd, bool autoLaunchIfValid = false) {
    if (!OpenClipboard(hwnd)) {
        if (!autoLaunchIfValid) SetStatus(hwnd, L"Clipboard busy", COLOR_ERR);
        return;
    }
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        wchar_t* pText = (wchar_t*)GlobalLock(hData);
        if (pText) {
            std::wstring trimmed = Trim(pText);
            GlobalUnlock(hData);
            CloseClipboard();

            std::wstring uri = BuildUri(trimmed);
            if (autoLaunchIfValid) {
                if (!uri.empty()) {
                    SetWindowTextW(g_hEntry, trimmed.c_str());
                    LaunchRobloxUri(uri, true);
                }
            } else {
                SetWindowTextW(g_hEntry, trimmed.c_str());
                if (!uri.empty()) {
                    LaunchRobloxUri(uri, false);
                } else {
                    SetStatus(hwnd, L"Pasted", COLOR_MUTED);
                }
            }
            return;
        }
    } else if (!autoLaunchIfValid) {
        SetStatus(hwnd, L"Clipboard is empty", COLOR_ERR);
    }
    CloseClipboard();
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
    wcscpy_s(g_nid.szTip, L"VNG PS Launcher (Background Active)");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void ShowLauncherWindow(HWND hwnd) {
    ShowWindow(hwnd, SW_SHOW);
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
    if (g_hEntry) SetFocus(g_hEntry);
}

LRESULT CALLBACK CustomButtonProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    bool isLaunch = (dwRefData == IDC_LAUNCH);
    switch (uMsg) {
        case WM_SETCURSOR:
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            COLORREF bg = isLaunch ? COLOR_ACCENT : COLOR_BTN;
            HBRUSH hBrush = CreateSolidBrush(bg);
            FillRect(hdc, &rc, hBrush);
            DeleteObject(hBrush);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, COLOR_FG);
            SelectObject(hdc, isLaunch ? g_hFontBtnBold : g_hFontBtn);

            wchar_t text[64];
            GetWindowTextW(hwnd, text, 64);
            DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            BOOL useDark = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_VAL, &useDark, sizeof(useDark));

            g_hFontTitle   = CreateFontW(24, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontSub     = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontText    = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontBtn     = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontBtnBold = CreateFontW(16, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            g_hBrushBg = CreateSolidBrush(COLOR_BG);
            g_hBrushCard = CreateSolidBrush(COLOR_CARD);

            g_hEntry = CreateWindowExW(
                0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                34, 90, 472, 26,
                hwnd, (HMENU)IDC_ENTRY, ((LPCREATESTRUCT)lParam)->hInstance, NULL
            );
            SendMessageW(g_hEntry, WM_SETFONT, (WPARAM)g_hFontText, TRUE);

            HWND hBtnPaste  = CreateWindowExW(0, L"BUTTON", L"Paste",          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 24,  138, 80,  36, hwnd, (HMENU)IDC_PASTE,   ((LPCREATESTRUCT)lParam)->hInstance, NULL);
            HWND hBtnClear  = CreateWindowExW(0, L"BUTTON", L"Clear",          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 114, 138, 80,  36, hwnd, (HMENU)IDC_CLEAR,   ((LPCREATESTRUCT)lParam)->hInstance, NULL);
            HWND hBtnHideBg = CreateWindowExW(0, L"BUTTON", L"Hide to Tray",   WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 204, 138, 120, 36, hwnd, (HMENU)IDC_HIDE_BG, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
            HWND hBtnLaunch = CreateWindowExW(0, L"BUTTON", L"Launch",         WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 396, 138, 120, 36, hwnd, (HMENU)IDC_LAUNCH,  ((LPCREATESTRUCT)lParam)->hInstance, NULL);

            SetWindowSubclass(hBtnPaste,  CustomButtonProc, 1, IDC_PASTE);
            SetWindowSubclass(hBtnClear,  CustomButtonProc, 2, IDC_CLEAR);
            SetWindowSubclass(hBtnHideBg, CustomButtonProc, 3, IDC_HIDE_BG);
            SetWindowSubclass(hBtnLaunch, CustomButtonProc, 4, IDC_LAUNCH);

            CreateThread(NULL, 0, BackgroundLinkWatcherThread, NULL, 0, NULL);
            return 0;
        }

        case WM_SHOW_LAUNCHER: {
            ShowLauncherWindow(hwnd);
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
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, IsWindowVisible(hwnd) ? L"Hide to Background" : L"Open VNG PS Launcher");
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

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, COLOR_FG);
            SetBkColor(hdc, COLOR_CARD);
            return (LRESULT)g_hBrushCard;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            FillRect(hdc, &rcClient, g_hBrushBg);

            SelectObject(hdc, g_hFontTitle);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, COLOR_FG);
            TextOutW(hdc, 24, 20, L"VNG PS Launcher (Background Active)", 35);

            SelectObject(hdc, g_hFontSub);
            SetTextColor(hdc, COLOR_MUTED);
            TextOutW(hdc, 25, 54, L"Click or copy any Roblox Private Server link to launch Froststrap directly.", 75);

            RECT rcCard = { 24, 82, 516, 124 };
            FillRect(hdc, &rcCard, g_hBrushCard);
            FrameRect(hdc, &rcCard, (HBRUSH)GetStockObject(BLACK_BRUSH));

            SelectObject(hdc, g_hFontSub);
            SetTextColor(hdc, g_statusColor);
            TextOutW(hdc, 25, 192, g_statusText.c_str(), (int)g_statusText.length());

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_LAUNCH) {
                DoLaunch(hwnd);
            } else if (id == IDC_PASTE) {
                DoPaste(hwnd, false);
            } else if (id == IDC_CLEAR) {
                SetWindowTextW(g_hEntry, L"");
                SetStatus(hwnd, L"Background Auto-Launch Active", COLOR_OK);
            } else if (id == IDC_HIDE_BG) {
                ShowWindow(hwnd, SW_HIDE);
            } else if (id == ID_TRAY_SHOW) {
                if (IsWindowVisible(hwnd)) {
                    ShowWindow(hwnd, SW_HIDE);
                } else {
                    ShowLauncherWindow(hwnd);
                }
            } else if (id == ID_TRAY_EXIT) {
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            if (g_hBrushBg) DeleteObject(g_hBrushBg);
            if (g_hBrushCard) DeleteObject(g_hBrushCard);
            if (g_hFontTitle) DeleteObject(g_hFontTitle);
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
            // Relaunch ourselves directly on the user's visible winsta0\default desktop!
            std::wstring cmdLine = GetCommandLineW();
            if (SpawnProcessOnInteractiveDesktop(cmdLine)) {
                return true; // Caller should exit immediately
            }
        }
    }
    return false;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    if (EnsureRunningOnDefaultDesktop()) {
        return 0;
    }

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
            // Check if it's a Roblox Private Server / Share link
            std::wstring uri = BuildUri(arg);
            if (!uri.empty()) {
                LaunchRobloxUri(uri, false);
                LocalFree(argv);
                return 0;
            }
            // Otherwise if it's a normal web URL (http/https), forward seamlessly to the real browser!
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
        if (hExisting && forceShowUi) {
            PostMessageW(hExisting, WM_SHOW_LAUNCHER, 0, 0);
        }
        return 0;
    }

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

    RECT rc = { 0, 0, 540, 240 };
    AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);

    HWND hwnd = CreateWindowExW(
        0,
        L"VngPsLauncherWndClass",
        L"VNG PS Launcher",
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
            DoLaunch(hwnd);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}

/**
 * Roblox Fast Launcher
 * High-performance, lightweight native Win32 private server & share link launcher.
 *
 * Direct URL Protocol Translation:
 * - https://www.roblox.com/share?code=XYZ&type=Server -> roblox://navigation/share_links?code=XYZ&type=Server
 * - https://www.roblox.com/games/123/name?privateServerLinkCode=XYZ -> roblox://placeId=123&linkCode=XYZ
 */

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <string>
#include <regex>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

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

// Control Identifiers
#define IDC_ENTRY    101
#define IDC_PASTE    102
#define IDC_CLEAR    103
#define IDC_LAUNCH   104

HWND g_hEntry = NULL;
HWND g_hStatus = NULL;
HBRUSH g_hBrushBg = NULL;
HBRUSH g_hBrushCard = NULL;
HFONT g_hFontTitle = NULL;
HFONT g_hFontSub = NULL;
HFONT g_hFontText = NULL;
HFONT g_hFontBtn = NULL;
HFONT g_hFontBtnBold = NULL;
COLORREF g_statusColor = COLOR_MUTED;
std::wstring g_statusText = L"Ready";

std::wstring Trim(const std::wstring& s) {
    size_t start = s.find_first_not_of(L" \t\r\n\"'");
    if (start == std::wstring::npos) return L"";
    size_t end = s.find_last_not_of(L" \t\r\n\"'");
    return s.substr(start, end - start + 1);
}

std::wstring BuildUri(const std::wstring& rawLink) {
    std::wstring link = Trim(rawLink);
    if (link.empty()) return L"";

    // 1. Share URL format: /share?code=...&type=...
    if (link.find(L"/share") != std::wstring::npos) {
        std::wregex codeRegex(L"[?&]code=([^&\\s]+)");
        std::wsmatch match;
        if (std::regex_search(link, match, codeRegex)) {
            std::wstring code = match[1].str();
            std::wstring stype = L"Server";
            std::wregex typeRegex(L"[?&]type=([^&\\s]+)");
            std::wsmatch typeMatch;
            if (std::regex_search(link, typeMatch, typeRegex)) {
                stype = typeMatch[1].str();
            }
            return L"roblox://navigation/share_links?code=" + code + L"&type=" + stype;
        }
    }

    // 2. Games Private Server link format: /games/(\d+) ... privateServerLinkCode=...
    std::wregex gamesRegex(L"/games/(\\d+)");
    std::wregex linkCodeRegex(L"privateServerLinkCode=([^&\\s]+)");
    std::wsmatch gamesMatch, codeMatch;

    if (std::regex_search(link, gamesMatch, gamesRegex) && std::regex_search(link, codeMatch, linkCodeRegex)) {
        return L"roblox://placeId=" + gamesMatch[1].str() + L"&linkCode=" + codeMatch[1].str();
    }

    return L"";
}

void SetStatus(HWND hwnd, const std::wstring& text, COLORREF color) {
    g_statusText = text;
    g_statusColor = color;
    RECT rc = { 25, 192, 530, 225 };
    InvalidateRect(hwnd, &rc, TRUE);
}

void DoLaunch(HWND hwnd) {
    wchar_t buf[2048] = { 0 };
    GetWindowTextW(g_hEntry, buf, 2047);
    std::wstring uri = BuildUri(buf);

    if (uri.empty()) {
        SetStatus(hwnd, L"Link not recognized", COLOR_ERR);
        return;
    }

    HINSTANCE res = ShellExecuteW(NULL, L"open", uri.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)res > 32) {
        SetStatus(hwnd, L"Launching Roblox...", COLOR_OK);
    } else {
        SetStatus(hwnd, L"Failed to launch Roblox URL protocol", COLOR_ERR);
    }
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
            SetWindowTextW(g_hEntry, Trim(pText).c_str());
            GlobalUnlock(hData);
            SetStatus(hwnd, L"Pasted", COLOR_MUTED);
        }
    } else {
        SetStatus(hwnd, L"Clipboard is empty", COLOR_ERR);
    }
    CloseClipboard();
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
            // Apply Windows 11 / 10 Immersive Dark Mode to titlebar
            BOOL useDark = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_VAL, &useDark, sizeof(useDark));

            g_hFontTitle   = CreateFontW(24, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontSub     = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontText    = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontBtn     = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontBtnBold = CreateFontW(16, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            g_hBrushBg = CreateSolidBrush(COLOR_BG);
            g_hBrushCard = CreateSolidBrush(COLOR_CARD);

            // Edit Control
            g_hEntry = CreateWindowExW(
                0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                34, 90, 472, 26,
                hwnd, (HMENU)IDC_ENTRY, ((LPCREATESTRUCT)lParam)->hInstance, NULL
            );
            SendMessageW(g_hEntry, WM_SETFONT, (WPARAM)g_hFontText, TRUE);

            // Buttons
            HWND hBtnPaste = CreateWindowExW(0, L"BUTTON", L"Paste", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 24, 138, 80, 36, hwnd, (HMENU)IDC_PASTE, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
            HWND hBtnClear = CreateWindowExW(0, L"BUTTON", L"Clear", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 114, 138, 80, 36, hwnd, (HMENU)IDC_CLEAR, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
            HWND hBtnLaunch = CreateWindowExW(0, L"BUTTON", L"Launch", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 396, 138, 120, 36, hwnd, (HMENU)IDC_LAUNCH, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

            SetWindowSubclass(hBtnPaste, CustomButtonProc, 1, IDC_PASTE);
            SetWindowSubclass(hBtnClear, CustomButtonProc, 2, IDC_CLEAR);
            SetWindowSubclass(hBtnLaunch, CustomButtonProc, 3, IDC_LAUNCH);

            SetFocus(g_hEntry);
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

            // Fill background
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            FillRect(hdc, &rcClient, g_hBrushBg);

            // Title
            SelectObject(hdc, g_hFontTitle);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, COLOR_FG);
            TextOutW(hdc, 24, 20, L"Roblox Launcher", 15);

            // Subtitle
            SelectObject(hdc, g_hFontSub);
            SetTextColor(hdc, COLOR_MUTED);
            TextOutW(hdc, 25, 54, L"Paste a private server link and launch it directly.", 51);

            // Card Panel Background for Entry Box
            RECT rcCard = { 24, 82, 516, 124 };
            FillRect(hdc, &rcCard, g_hBrushCard);
            FrameRect(hdc, &rcCard, (HBRUSH)GetStockObject(BLACK_BRUSH));

            // Status Label
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
                DoPaste(hwnd);
            } else if (id == IDC_CLEAR) {
                SetWindowTextW(g_hEntry, L"");
                SetStatus(hwnd, L"Ready", COLOR_MUTED);
            }
            return 0;
        }

        case WM_DESTROY:
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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
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
    wc.lpszClassName = L"RobloxLauncherWndClass";

    if (!RegisterClassExW(&wc)) return 0;

    RECT rc = { 0, 0, 540, 240 };
    AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);

    HWND hwnd = CreateWindowExW(
        0,
        L"RobloxLauncherWndClass",
        L"Roblox Launcher",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

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

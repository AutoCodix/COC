#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <string>
#include <cmath>
#include <atomic>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

static const wchar_t* kClassName = L"GranuleEffectsWindow";
static const UINT WM_TRAY = WM_APP + 1;
static const UINT TIMER_TICK = 1;

struct Settings {
    bool enabled = true;
    int strength = 36;
    int bounce = 58;
    int settle = 68;
};

struct WobbleState {
    HWND target = nullptr;
    RECT lastObserved{};
    bool moving = false;
    bool haveLast = false;
    double ox = 0.0, oy = 0.0;
    double vx = 0.0, vy = 0.0;
    ULONGLONG ignoreEventsUntil = 0;
};

static Settings g_settings;
static WobbleState g_state;
static HWND g_main = nullptr;
static HWINEVENTHOOK g_hookStart = nullptr;
static HWINEVENTHOOK g_hookEnd = nullptr;
static HWINEVENTHOOK g_hookLoc = nullptr;
static NOTIFYICONDATAW g_nid{};
static std::atomic<bool> g_quitting{false};

static bool IsEligibleWindow(HWND hwnd) {
    if (!hwnd || hwnd == g_main || !IsWindowVisible(hwnd)) return false;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return false;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (!(style & WS_CAPTION)) return false;
    if (IsZoomed(hwnd) || IsIconic(hwnd)) return false;
    wchar_t cls[128]{};
    GetClassNameW(hwnd, cls, 127);
    if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0 || wcscmp(cls, L"Shell_TrayWnd") == 0) return false;
    return true;
}

static void ResetState(HWND hwnd = nullptr) {
    g_state.target = hwnd;
    g_state.moving = false;
    g_state.haveLast = false;
    g_state.ox = g_state.oy = 0.0;
    g_state.vx = g_state.vy = 0.0;
    ZeroMemory(&g_state.lastObserved, sizeof(g_state.lastObserved));
}

static void BeginMove(HWND hwnd) {
    if (!g_settings.enabled || !IsEligibleWindow(hwnd)) return;
    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return;
    ResetState(hwnd);
    g_state.moving = true;
    g_state.haveLast = true;
    g_state.lastObserved = r;
}

static void EndMove(HWND hwnd) {
    if (hwnd != g_state.target) return;
    g_state.moving = false;
}

static void OnObservedLocation(HWND hwnd) {
    if (!g_settings.enabled || hwnd != g_state.target || !g_state.moving) return;
    ULONGLONG now = GetTickCount64();
    if (now < g_state.ignoreEventsUntil) return;

    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return;
    if (!g_state.haveLast) {
        g_state.lastObserved = r;
        g_state.haveLast = true;
        return;
    }

    int dx = r.left - g_state.lastObserved.left;
    int dy = r.top - g_state.lastObserved.top;
    g_state.lastObserved = r;

    const double s = g_settings.strength / 100.0;
    g_state.vx += dx * (0.09 + 0.23 * s);
    g_state.vy += dy * (0.09 + 0.23 * s);
    g_state.vx = (g_state.vx > 28.0) ? 28.0 : (g_state.vx < -28.0 ? -28.0 : g_state.vx);
    g_state.vy = (g_state.vy > 28.0) ? 28.0 : (g_state.vy < -28.0 ? -28.0 : g_state.vy);
}

static void ApplySpringTick() {
    if (!g_settings.enabled || !g_state.target || !IsWindow(g_state.target)) {
        ResetState();
        return;
    }

    const double bounce = g_settings.bounce / 100.0;
    const double settle = g_settings.settle / 100.0;
    const double spring = 0.12 + 0.19 * bounce;
    const double damping = 0.72 + 0.23 * settle;

    g_state.vx += (-g_state.ox) * spring;
    g_state.vy += (-g_state.oy) * spring;
    g_state.vx *= damping;
    g_state.vy *= damping;
    g_state.ox += g_state.vx;
    g_state.oy += g_state.vy;

    if (!g_state.moving && std::abs(g_state.ox) < 0.18 && std::abs(g_state.oy) < 0.18 &&
        std::abs(g_state.vx) < 0.18 && std::abs(g_state.vy) < 0.18) {
        RECT r = g_state.lastObserved;
        g_state.ignoreEventsUntil = GetTickCount64() + 20;
        SetWindowPos(g_state.target, nullptr, r.left, r.top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
        ResetState();
        return;
    }

    if (!g_state.haveLast) return;
    int x = g_state.lastObserved.left + (int)std::lround(g_state.ox);
    int y = g_state.lastObserved.top + (int)std::lround(g_state.oy);
    g_state.ignoreEventsUntil = GetTickCount64() + 12;
    SetWindowPos(g_state.target, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
}

static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
    if (g_quitting.load() || idObject != OBJID_WINDOW || !hwnd) return;
    switch (event) {
        case EVENT_SYSTEM_MOVESIZESTART: BeginMove(hwnd); break;
        case EVENT_SYSTEM_MOVESIZEEND: EndMove(hwnd); break;
        case EVENT_OBJECT_LOCATIONCHANGE: OnObservedLocation(hwnd); break;
    }
}

static void InstallHooks() {
    g_hookStart = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZESTART, nullptr, WinEventProc, 0, 0,
                                  WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    g_hookEnd = SetWinEventHook(EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND, nullptr, WinEventProc, 0, 0,
                                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    g_hookLoc = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, WinEventProc, 0, 0,
                                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

static void UninstallHooks() {
    if (g_hookStart) UnhookWinEvent(g_hookStart);
    if (g_hookEnd) UnhookWinEvent(g_hookEnd);
    if (g_hookLoc) UnhookWinEvent(g_hookLoc);
    g_hookStart = g_hookEnd = g_hookLoc = nullptr;
}

static void SetControlFont(HWND h) { SendMessageW(h, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE); }
static HWND MakeLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    HWND c = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent, nullptr, nullptr, nullptr);
    SetControlFont(c); return c;
}
static HWND MakeTrack(HWND parent, int id, int x, int y, int w, int value) {
    HWND c = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                           x, y, w, 36, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(c, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(c, TBM_SETPOS, TRUE, value);
    return c;
}
static void UpdateStatusText() {
    HWND s = GetDlgItem(g_main, 105);
    if (s) SetWindowTextW(s, g_settings.enabled ? L"Wobble is ON" : L"Wobble is OFF");
}
static void AddTrayIcon(HWND hwnd) {
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Granule Effects");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}
static void ShowTrayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 201, L"Open Granule Effects");
    AppendMenuW(menu, MF_STRING | (g_settings.enabled ? MF_CHECKED : 0), 202, L"Enable wobble");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 203, L"Exit");
    POINT p{}; GetCursorPos(&p);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            MakeLabel(hwnd, L"Granule Effects", 20, 16, 250, 26);
            MakeLabel(hwnd, L"Elastic window movement for normal Windows apps.", 20, 44, 360, 22);
            HWND check = CreateWindowW(L"BUTTON", L"Enable wobble", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                       20, 78, 160, 24, hwnd, (HMENU)101, nullptr, nullptr);
            SetControlFont(check);
            SendMessageW(check, BM_SETCHECK, BST_CHECKED, 0);
            MakeLabel(hwnd, L"Strength", 20, 118, 100, 20);
            MakeTrack(hwnd, 102, 112, 108, 250, g_settings.strength);
            MakeLabel(hwnd, L"Bounce", 20, 162, 100, 20);
            MakeTrack(hwnd, 103, 112, 152, 250, g_settings.bounce);
            MakeLabel(hwnd, L"Settle", 20, 206, 100, 20);
            MakeTrack(hwnd, 104, 112, 196, 250, g_settings.settle);
            HWND status = MakeLabel(hwnd, L"Wobble is ON", 20, 246, 200, 22);
            SetWindowLongPtrW(status, GWLP_ID, 105);
            MakeLabel(hwnd, L"Tip: drag a normal window by its title bar. Maximized windows are ignored.", 20, 276, 390, 40);
            AddTrayIcon(hwnd);
            SetTimer(hwnd, TIMER_TICK, 8, nullptr);
            return 0;
        }
        case WM_HSCROLL: {
            HWND from = (HWND)lp;
            int id = GetDlgCtrlID(from);
            int pos = (int)SendMessageW(from, TBM_GETPOS, 0, 0);
            if (id == 102) g_settings.strength = pos;
            if (id == 103) g_settings.bounce = pos;
            if (id == 104) g_settings.settle = pos;
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == 101) {
                g_settings.enabled = SendMessageW(GetDlgItem(hwnd, 101), BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (!g_settings.enabled) ResetState();
                UpdateStatusText();
            } else if (id == 201) {
                ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
            } else if (id == 202) {
                g_settings.enabled = !g_settings.enabled;
                SendMessageW(GetDlgItem(hwnd, 101), BM_SETCHECK, g_settings.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
                if (!g_settings.enabled) ResetState();
                UpdateStatusText();
            } else if (id == 203) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_TIMER:
            if (wp == TIMER_TICK) ApplySpringTick();
            return 0;
        case WM_TRAY:
            if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) ShowTrayMenu(hwnd);
            if (lp == WM_LBUTTONDBLCLK) { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); }
            return 0;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            g_quitting.store(true);
            KillTimer(hwnd, TIMER_TICK);
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            UninstallHooks();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = CreateSolidBrush(RGB(245, 245, 247));
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    g_main = CreateWindowExW(0, kClassName, L"Granule Effects - Wobble", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, 440, 370, nullptr, nullptr, hInst, nullptr);
    if (!g_main) return 1;
    InstallHooks();
    ShowWindow(g_main, nCmdShow);
    UpdateWindow(g_main);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

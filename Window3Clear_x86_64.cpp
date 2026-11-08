#define _WIN32_WINNT 0x0600
#define UNICODE
#define _UNICODE
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <map>
#include <math.h>
#include <stdlib.h>

#ifndef ECM_FIRST
#define ECM_FIRST 0x1500
#endif
#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER (ECM_FIRST + 1)
#endif

#define APP_NAME     L"Window3Clear"
#define APP_VERSION  L"v19.0-beta"
#define APP_TAGLINE  L"轻量级 Windows 窗口透明度管理"

// ================= 亚克力 API =================
enum ACCENT_STATE { ACCENT_DISABLED = 0, ACCENT_ENABLE_GRADIENT = 1, ACCENT_ENABLE_TRANSPARENTGRADIENT = 2, ACCENT_ENABLE_BLURBEHIND = 3, ACCENT_ENABLE_ACRYLICBLURBEHIND = 4, ACCENT_ENABLE_HOSTBACKDROP = 5 };
struct ACCENT_POLICY { ACCENT_STATE AccentState; DWORD AccentFlags; DWORD GradientColor; DWORD AnimationId; };
struct WINDOWCOMPOSITIONATTRIBDATA { DWORD Attrib; PVOID pvData; SIZE_T cbData; };
typedef BOOL (WINAPI *PFN_SetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);
#define WCA_ACCENT_POLICY 19

void EnableAcrylic(HWND hwnd, DWORD color, bool enable = true) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    PFN_SetWindowCompositionAttribute SetWindowCompositionAttribute = (PFN_SetWindowCompositionAttribute)GetProcAddress(user32, "SetWindowCompositionAttribute");
    if (!SetWindowCompositionAttribute) return;
    ACCENT_POLICY policy = { enable ? ACCENT_ENABLE_ACRYLICBLURBEHIND : ACCENT_DISABLED, 0, enable ? color : 0, 0 };
    WINDOWCOMPOSITIONATTRIBDATA data = { WCA_ACCENT_POLICY, &policy, sizeof(policy) };
    SetWindowCompositionAttribute(hwnd, &data);
}

// ================= 常量 =================
#define WM_TRAYICON         (WM_USER + 1)
#define WM_MENU_SELECTED    (WM_APP + 200)
#define WM_UI_REFRESH       (WM_APP + 201)
#define TIMER_MASK_RESTORE  1001
#define TIMER_MASK_DELAY_APPLY 1002

#define MIN_ALPHA 10
#define MAX_ALPHA 255
#define STEP_ALPHA 20

#define HK_DOWN 1
#define HK_UP 2
#define HK_RESET 3
#define HK_P1 5
#define HK_P2 6
#define HK_P3 7
#define HK_P4 8
#define HK_PICK 9
#define HK_BOSS 10
#define HK_MOYU 11
#define HK_MASK 15

#define WIN_W 800
#define WIN_H 520
#define SIDEBAR_W 240

struct Theme { COLORREF accent, text, textDim, cardBg, hover; };
Theme g_theme = { RGB(10, 132, 255), RGB(255, 255, 255), RGB(150, 150, 160), RGB(40, 40, 45), RGB(60, 60, 70) };

int S(int v) {
    static int dpi = 96;
    if (dpi == 96) { HDC hdc = GetDC(NULL); dpi = GetDeviceCaps(hdc, LOGPIXELSX); ReleaseDC(NULL, hdc); if (dpi == 0) dpi = 96; }
    return (int)((float)v * dpi / 96.0f);
}

struct WindowState { int alpha; bool topmost; bool clickThrough; };
std::map<std::wstring, WindowState> g_profiles;

CRITICAL_SECTION g_cs;
volatile LONG g_bThreadRunning = 1;

// ================= 配置持久化 =================
struct AppConfig {
    int  lastFadeSec   = 5;
    int  lastFadePct   = 50;
    bool autoStart     = false;
    bool restoreOnExit = true;
    std::wstring maskKey = L"";
    int  maskAlpha     = 128;
    bool maskEnabled   = false;
};
AppConfig g_cfg;

std::wstring GetConfigDir() {
    wchar_t buf[MAX_PATH] = {0};
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, buf) != S_OK) return L"";
    std::wstring dir = buf; dir += L"\\Window3Clear";
    CreateDirectoryW(dir.c_str(), NULL);
    return dir;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    std::string s(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], len, NULL, NULL);
    return s;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::wstring w(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
    return w;
}

void LoadConfig() {
    std::wstring dir = GetConfigDir();
    if (dir.empty()) return;
    std::wstring path = dir + L"\\config.ini";
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD sz = GetFileSize(h, NULL);
    std::string buf(sz, 0);
    DWORD read = 0;
    ReadFile(h, &buf[0], sz, &read, NULL);
    CloseHandle(h);
    buf.resize(read);

    std::string section;
    size_t pos = 0;
    while (pos < buf.size()) {
        size_t eol = buf.find('\n', pos);
        if (eol == std::string::npos) eol = buf.size();
        std::string line = buf.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[' && line.back() == ']') { section = line.substr(1, line.size() - 2); continue; }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        
        if (section == "General") {
            if      (k == "FadeSec")       g_cfg.lastFadeSec = atoi(v.c_str());
            else if (k == "FadePct")       g_cfg.lastFadePct = atoi(v.c_str());
            else if (k == "RestoreOnExit") g_cfg.restoreOnExit = atoi(v.c_str()) != 0;
            else if (k == "MaskAlpha")     g_cfg.maskAlpha = atoi(v.c_str());
            else if (k == "MaskEnabled")   g_cfg.maskEnabled = atoi(v.c_str()) != 0;
            else if (k == "MaskKey")       g_cfg.maskKey = Utf8ToWide(v);
        } else if (section == "Profiles") {
            std::wstring wk = Utf8ToWide(k);
            int a = 255, tm = 0, ct = 0;
            sscanf(v.c_str(), "%d,%d,%d", &a, &tm, &ct);
            if (a < MIN_ALPHA) a = MIN_ALPHA; if (a > MAX_ALPHA) a = MAX_ALPHA;
            g_profiles[wk].alpha = a;
            g_profiles[wk].topmost = tm != 0;
            g_profiles[wk].clickThrough = ct != 0;
        }
    }
}

void SaveConfig() {
    std::wstring dir = GetConfigDir();
    if (dir.empty()) return;
    std::wstring path = dir + L"\\config.ini";

    std::string content = "; Window3Clear " + WideToUtf8(APP_VERSION) + "\n";
    content += "[General]\n";
    {
        char b[128];
        sprintf(b, "FadeSec=%d\n", g_cfg.lastFadeSec);                  content += b;
        sprintf(b, "FadePct=%d\n", g_cfg.lastFadePct);                  content += b;
        sprintf(b, "RestoreOnExit=%d\n", g_cfg.restoreOnExit ? 1 : 0);  content += b;
        sprintf(b, "MaskAlpha=%d\n", g_cfg.maskAlpha);                  content += b;
        sprintf(b, "MaskEnabled=%d\n", g_cfg.maskEnabled ? 1 : 0);      content += b;
        content += "MaskKey=" + WideToUtf8(g_cfg.maskKey) + "\n";
    }
    content += "\n[Profiles]\n";
    EnterCriticalSection(&g_cs);
    for (auto& kv : g_profiles) {
        if (kv.second.alpha == 255 && !kv.second.topmost && !kv.second.clickThrough) continue;
        char b[32];
        sprintf(b, "=%d,%d,%d\n", kv.second.alpha, kv.second.topmost ? 1 : 0, kv.second.clickThrough ? 1 : 0);
        content += WideToUtf8(kv.first);
        content += b;
    }
    LeaveCriticalSection(&g_cs);

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, content.c_str(), (DWORD)content.size(), &written, NULL);
    CloseHandle(h);
}

// ================= 开机自启 =================
bool IsAutoStartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    wchar_t buf[MAX_PATH * 2] = {0};
    DWORD sz = sizeof(buf), type = 0;
    LONG r = RegQueryValueExW(hKey, L"Window3Clear", NULL, &type, (BYTE*)buf, &sz);
    RegCloseKey(hKey);
    if (r != ERROR_SUCCESS || type != REG_SZ) return false;
    wchar_t exe[MAX_PATH] = {0}; GetModuleFileNameW(NULL, exe, MAX_PATH);
    return wcsstr(buf, exe) != NULL;
}

void SetAutoStart(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) return;
    if (enable) {
        wchar_t exe[MAX_PATH] = {0}; GetModuleFileNameW(NULL, exe, MAX_PATH);
        std::wstring quoted = L"\"" + std::wstring(exe) + L"\"";
        RegSetValueExW(hKey, L"Window3Clear", 0, REG_SZ, (const BYTE*)quoted.c_str(), (DWORD)((quoted.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, L"Window3Clear");
    }
    RegCloseKey(hKey);
}

// ================= 全局状态 =================
HWND g_hwnd = NULL, g_hSearchEdit = NULL;
HWND g_hOverlay = NULL;
HWND g_bossHide = NULL;

HWND g_locked_target = NULL;
std::wstring g_locked_title = L"未选择目标...";
std::wstring g_locked_key;

HFONT g_fTitle = NULL, g_fBig = NULL, g_fNormal = NULL, g_fSmall = NULL, g_fMenu = NULL;
std::vector<HWND> g_wins;
std::vector<std::wstring> g_titles;
std::wstring g_searchText;

volatile int g_render_alpha = 255, g_target_alpha = 255;
bool g_draggingSlider = false;
int g_hoverBtn = -1, g_pressBtn = -1;
bool g_hoverTarget = false, g_hoverClose = false, g_hoverMin = false;
bool g_hoverTopmost = false, g_hoverClick = false, g_hoverMore = false, g_hoverSettings = false, g_hoverPick = false;
bool g_hoverSelf = false, g_hoverMaskBtn = false;
int g_hoverPreset = -1;
bool g_tracking = false;

volatile bool      g_breathActive   = false;
volatile bool      g_fadeActive     = false;
volatile int       g_breathTick     = 0;
volatile int       g_fadeFrom       = 255;
volatile int       g_fadeTo         = 255;
volatile ULONGLONG g_fadeStartTick  = 0;
volatile int       g_fadeDurationMs = 5000;

int g_lastFadeSec = 5;
int g_lastFadePct = 50;
int g_self_alpha = 255;

bool g_moyuActive = false;
HANDLE g_hAnimThread = NULL;

// 面具功能全局变量
HWND g_mask_window = NULL;
bool g_isPickingMask = false;
WINDOWPLACEMENT g_saved_wp;
bool g_saved_topmost = false;
volatile int g_pendingMaskAlpha = 255;
HWND g_pendingMaskTarget = NULL;

struct MenuItem { std::wstring text; int id; bool checked; bool disabled; bool isSep; };
std::vector<MenuItem> g_menuItems;
HWND g_hMenu = NULL, g_hMenuOwner = NULL;
int g_menuHover = -1;

HICON g_hIcon = NULL;
NOTIFYICONDATAW g_nid;
bool g_helpHoverClose = false, g_helpTracking = false;

// 渐隐设置对话框
HWND    g_hFadeDlg      = NULL;
HWND    g_hFadeSecEdit  = NULL;
HWND    g_hFadePctEdit  = NULL;
WNDPROC g_oldEditProc   = NULL;
int     g_fadeDlgHover  = -1;

// 设置对话框
HWND    g_hSettingsDlg  = NULL;
int     g_settingsHover = -1;
bool    g_settingsAutoStartTmp = false;
bool    g_settingsRestoreTmp   = true;

// 面具设置对话框
HWND    g_hMaskDlg      = NULL;
HWND    g_hMaskPctEdit  = NULL;
int     g_maskDlgHover  = -1;
WNDPROC g_oldMaskEditProc = NULL;

// 拾取器缓存
int     g_overlayW = 0, g_overlayH = 0;
HDC     g_overlayDC = NULL;
HBITMAP g_overlayBmp = NULL;
void*   g_overlayBits = NULL;
HWND    g_pickerHover = NULL;

void RestoreApp(HWND hwnd);

// ================= 核心逻辑 =================
typedef BOOL (WINAPI *PFN_QueryFullProcessImageNameW)(HANDLE, DWORD, LPWSTR, PDWORD);

std::wstring GetKey(HWND h) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    wchar_t path[MAX_PATH] = {0}; DWORD sz = MAX_PATH;
    if (p) {
        static PFN_QueryFullProcessImageNameW fn = NULL;
        if (!fn) { void* ptr = (void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "QueryFullProcessImageNameW"); fn = reinterpret_cast<PFN_QueryFullProcessImageNameW>(ptr); }
        if (fn) fn(p, 0, path, &sz);
        CloseHandle(p);
    }
    std::wstring name = path; size_t pos = name.find_last_of(L"\\");
    if (pos != std::wstring::npos) name = name.substr(pos + 1);
    wchar_t cls[256] = {0}; GetClassNameW(h, cls, 256);
    return name + L"|" + cls;
}

void ApplyAlpha(HWND h, int a, bool save = true) {
    if (!IsWindow(h) || h == g_hwnd) return;
    if (a < MIN_ALPHA) a = MIN_ALPHA; if (a > MAX_ALPHA) a = MAX_ALPHA;
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (!(ex & WS_EX_LAYERED)) {
        SetWindowLongPtrW(h, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        SetWindowPos(h, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    SetLayeredWindowAttributes(h, 0, (BYTE)a, LWA_ALPHA);
    if (save) {
        std::wstring k = GetKey(h);
        if (!k.empty()) {
            EnterCriticalSection(&g_cs);
            g_profiles[k].alpha = a;
            LeaveCriticalSection(&g_cs);
        }
    }
}

HWND g_find_result = NULL;
BOOL CALLBACK FindWindowProc(HWND h, LPARAM lp) {
    if (IsWindowVisible(h) && GetKey(h) == *(std::wstring*)lp) { g_find_result = h; return FALSE; }
    return TRUE;
}

HWND GetRealTarget_Internal() {
    if (g_locked_key.empty()) return NULL;
    if (IsWindow(g_locked_target) && GetKey(g_locked_target) == g_locked_key) return g_locked_target;
    for (size_t i = 0; i < g_wins.size(); i++) {
        if (IsWindow(g_wins[i]) && GetKey(g_wins[i]) == g_locked_key) { g_locked_target = g_wins[i]; return g_locked_target; }
    }
    g_find_result = NULL;
    EnumWindows(FindWindowProc, (LPARAM)&g_locked_key);
    if (g_find_result) { g_locked_target = g_find_result; return g_locked_target; }
    return NULL;
}

HWND GetRealTarget() {
    EnterCriticalSection(&g_cs);
    HWND r = GetRealTarget_Internal();
    LeaveCriticalSection(&g_cs);
    return r;
}

HWND GetMaskWindow() {
    if (g_cfg.maskKey.empty()) return NULL;
    if (IsWindow(g_mask_window) && GetKey(g_mask_window) == g_cfg.maskKey) return g_mask_window;
    g_find_result = NULL;
    EnumWindows(FindWindowProc, (LPARAM)&g_cfg.maskKey);
    if (g_find_result) { g_mask_window = g_find_result; return g_mask_window; }
    return NULL;
}

void LockTarget(HWND h, const std::wstring& t) {
    EnterCriticalSection(&g_cs);
    g_locked_target = h;
    g_locked_title = t;
    if (h) {
        g_locked_key = GetKey(h);
        if (g_profiles.count(g_locked_key)) g_target_alpha = g_profiles[g_locked_key].alpha;
        else g_target_alpha = 255;
        g_render_alpha = g_target_alpha;
    } else {
        g_locked_key.clear();
        g_target_alpha = 255;
        g_render_alpha = 255;
    }
    LeaveCriticalSection(&g_cs);
}

bool HasTarget() {
    EnterCriticalSection(&g_cs);
    bool r = !g_locked_key.empty();
    LeaveCriticalSection(&g_cs);
    return r;
}

static BOOL CALLBACK EnumWindowsProc(HWND h, LPARAM) {
    if (h == g_hwnd || h == g_hOverlay || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    if (GetWindowTextLengthW(h) == 0) return TRUE;
    wchar_t buf[256]; GetWindowTextW(h, buf, 256);
    std::wstring t = buf;
    if (t == L"Program Manager" || t == L"Windows 输入体验") return TRUE;
    g_wins.push_back(h); g_titles.push_back(t);
    return TRUE;
}

void RefreshList() {
    EnterCriticalSection(&g_cs);
    g_wins.clear(); g_titles.clear();
    EnumWindows(EnumWindowsProc, 0);
    if (g_locked_key.empty() || !GetRealTarget_Internal()) LockTarget(NULL, L"未选择目标...");
    LeaveCriticalSection(&g_cs);
}

// ================= 面具核心机制 =================
void ExecuteWithMask(HWND target, int targetAlpha, bool instant = true) {
    if (targetAlpha > MAX_ALPHA) targetAlpha = MAX_ALPHA;
    if (targetAlpha < MIN_ALPHA) targetAlpha = MIN_ALPHA;

    // 如果面具未开启，直接应用并同步界面
    if (!g_cfg.maskEnabled) {
        if (IsWindow(target)) ApplyAlpha(target, targetAlpha, true);
        EnterCriticalSection(&g_cs);
        g_render_alpha = targetAlpha;
        g_target_alpha = targetAlpha;
        LeaveCriticalSection(&g_cs);
        InvalidateRect(g_hwnd, NULL, FALSE);
        return;
    }

    HWND mask = GetMaskWindow();
    if (!IsWindow(mask)) {
        // 如果因为某些原因找不到面具窗口，降级直接应用
        if (IsWindow(target)) ApplyAlpha(target, targetAlpha, true);
        EnterCriticalSection(&g_cs);
        g_render_alpha = targetAlpha;
        g_target_alpha = targetAlpha;
        LeaveCriticalSection(&g_cs);
        InvalidateRect(g_hwnd, NULL, FALSE);
        return;
    }

    // 1. 界面数据瞬间同步（用户看到 100%，底层现在还是 10%）
    EnterCriticalSection(&g_cs);
    g_render_alpha = targetAlpha;
    g_target_alpha = targetAlpha;
    g_fadeActive = false;
    g_breathActive = false;
    LeaveCriticalSection(&g_cs);
    InvalidateRect(g_hwnd, NULL, FALSE);

    // 2. 记录面具位置
    ZeroMemory(&g_saved_wp, sizeof(g_saved_wp));
    g_saved_wp.length = sizeof(g_saved_wp);
    GetWindowPlacement(mask, &g_saved_wp);
    g_saved_topmost = (GetWindowLongPtrW(mask, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;

    // 3. 瞬间拉起面具全屏
    ShowWindow(mask, SW_RESTORE);
    SetWindowPos(mask, HWND_TOPMOST, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_SHOWWINDOW);
    SetForegroundWindow(mask);
    SetActiveWindow(mask);

    // 4. 准备延迟改透明度
    g_pendingMaskAlpha = targetAlpha;
    g_pendingMaskTarget = target;

    KillTimer(g_hwnd, TIMER_MASK_DELAY_APPLY);
    KillTimer(g_hwnd, TIMER_MASK_RESTORE);

    // 延迟 2 秒应用，让老师以为你在看代码
    SetTimer(g_hwnd, TIMER_MASK_DELAY_APPLY, 2000, NULL);
}

// 拦截所有调透明度的操作
void ApplyAlphaSafe(HWND target, int alpha, bool save = true) {
    if (alpha > MAX_ALPHA) alpha = MAX_ALPHA;
    if (alpha < MIN_ALPHA) alpha = MIN_ALPHA;
    
    if (g_cfg.maskEnabled && IsWindow(target)) {
        ExecuteWithMask(target, alpha, true);
    } else {
        ApplyAlpha(target, alpha, save);
        EnterCriticalSection(&g_cs);
        g_render_alpha = alpha;
        g_target_alpha = alpha;
        LeaveCriticalSection(&g_cs);
        InvalidateRect(g_hwnd, NULL, FALSE);
    }
}

void StartCustomFade(int seconds, int percent) {
    if (seconds < 1)   seconds = 1;
    if (seconds > 120) seconds = 120;
    if (percent < 1)   percent = 1;
    if (percent > 100) percent = 100;
    g_lastFadeSec = seconds;
    g_lastFadePct = percent;
    g_cfg.lastFadeSec = seconds;
    g_cfg.lastFadePct = percent;
    int targetAlpha = (percent * 255 + 50) / 100;

    if (g_cfg.maskEnabled && GetMaskWindow()) {
        // 面具模式下，渐隐直接变成瞬间更改（并带面具动画）
        ExecuteWithMask(GetRealTarget(), targetAlpha, true);
    } else {
        EnterCriticalSection(&g_cs);
        if (!g_locked_key.empty()) {
            g_fadeActive     = true;
            g_breathActive   = false;
            g_fadeFrom       = g_render_alpha;
            g_fadeTo         = targetAlpha;
            g_fadeStartTick  = GetTickCount64();
            g_fadeDurationMs = seconds * 1000;
        }
        LeaveCriticalSection(&g_cs);
    }
}

void ToggleBreath() {
    if (g_cfg.maskEnabled && GetMaskWindow()) {
        MessageBoxW(g_hwnd, L"开启面具时无法使用呼吸灯特效，请先关闭面具。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    bool turnOff = false;
    EnterCriticalSection(&g_cs);
    if (!g_locked_key.empty()) {
        g_breathActive = !g_breathActive;
        g_fadeActive = false;
        if (g_breathActive) g_breathTick = 0;
        else { g_target_alpha = 255; g_render_alpha = 255; turnOff = true; }
    }
    LeaveCriticalSection(&g_cs);
    if (turnOff) {
        HWND t = GetRealTarget();
        if (IsWindow(t)) ApplyAlpha(t, 255, true);
    }
}

void ResetAllWindows() {
    EnterCriticalSection(&g_cs);
    std::vector<HWND> wins = g_wins;
    for (auto& kv : g_profiles) kv.second.alpha = 255;
    g_render_alpha = 255;
    g_target_alpha = 255;
    g_fadeActive = false;
    g_breathActive = false;
    LeaveCriticalSection(&g_cs);
    
    for (auto w : wins) if (IsWindow(w)) ApplyAlpha(w, 255, false);
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
}

// ================= 后台动画线程 =================
DWORD WINAPI AnimThreadProc(LPVOID lpParam) {
    (void)lpParam;
    while (g_bThreadRunning) {
        Sleep(16);
        HWND target = NULL;
        int aToApply = 0;
        bool doApply = false, needPost = false;

        EnterCriticalSection(&g_cs);
        target = GetRealTarget_Internal();

        if (g_fadeActive) {
            ULONGLONG now = GetTickCount64();
            ULONGLONG elapsed = now - g_fadeStartTick;
            int a;
            if (elapsed >= (ULONGLONG)g_fadeDurationMs) { 
                a = g_fadeTo; 
                g_fadeActive = false; 
                if (IsWindow(target)) ApplyAlpha(target, a, true);
            }
            else a = g_fadeFrom + (int)((long long)(g_fadeTo - g_fadeFrom) * (long long)elapsed / (long long)g_fadeDurationMs);
            g_render_alpha = a; g_target_alpha = a; aToApply = a; doApply = true; needPost = true;
        }
        else if (g_breathActive) {
            g_breathTick++;
            double rad = g_breathTick * 3.1415926 / 125.0;
            int a = 190 + (int)(65.0 * sin(rad));
            g_render_alpha = a; g_target_alpha = a; aToApply = a; doApply = true; needPost = true;
        }
        else {
            int r = g_render_alpha, t = g_target_alpha;
            // ★ 修复：面具开启时，禁止后台线程自作主张平滑过渡
            if (r != t && !g_cfg.maskEnabled) {
                int diff = t - r;
                if (diff > 0) r += (diff * 0.25 > 1 ? (int)(diff * 0.25) : 1);
                else r -= (-diff * 0.25 > 1 ? (int)(-diff * 0.25) : 1);
                if (abs(t - r) <= 1) r = t;
                g_render_alpha = r; aToApply = r; doApply = true; needPost = true;
            }
        }
        LeaveCriticalSection(&g_cs);
        if (doApply && IsWindow(target) && !g_cfg.maskEnabled) ApplyAlpha(target, aToApply, false);
        if (needPost) PostMessageW(g_hwnd, WM_UI_REFRESH, 0, 0);
    }
    return 0;
}

// ================= GDI 工具 =================
void FillRound(HDC hdc, RECT r, int radius, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c); HPEN pen = CreatePen(PS_NULL, 0, 0);
    SelectObject(hdc, br); SelectObject(hdc, pen); RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius*2, radius*2);
    DeleteObject(br); DeleteObject(pen);
}
void StrokeRound(HDC hdc, RECT r, int radius, COLORREF c, int width) {
    HPEN pen = CreatePen(PS_SOLID, width, c); HBRUSH br = (HBRUSH)GetStockObject(NULL_BRUSH);
    SelectObject(hdc, pen); SelectObject(hdc, br); RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius*2, radius*2);
    DeleteObject(pen);
}
void DrawTextC(HDC hdc, const wchar_t* t, RECT r, HFONT f, COLORREF c, UINT fmt) {
    SelectObject(hdc, f); SetTextColor(hdc, c); SetBkMode(hdc, TRANSPARENT); DrawTextW(hdc, t, -1, &r, fmt);
}
void FillCircle(HDC hdc, int cx, int cy, int r, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c); HPEN pen = CreatePen(PS_NULL, 0, 0);
    SelectObject(hdc, br); SelectObject(hdc, pen); Ellipse(hdc, cx-r, cy-r, cx+r+1, cy+r+1);
    DeleteObject(br); DeleteObject(pen);
}
void DrawCrosshair(HDC hdc, int cx, int cy, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 2, color); SelectObject(hdc, pen);
    int s = 4, d = 8;
    MoveToEx(hdc, cx - d, cy - d + s, NULL); LineTo(hdc, cx - d, cy - d); LineTo(hdc, cx - d + s, cy - d);
    MoveToEx(hdc, cx + d - s, cy - d, NULL); LineTo(hdc, cx + d, cy - d); LineTo(hdc, cx + d, cy - d + s);
    MoveToEx(hdc, cx - d, cy + d - s, NULL); LineTo(hdc, cx - d, cy + d); LineTo(hdc, cx - d + s, cy + d);
    MoveToEx(hdc, cx + d - s, cy + d, NULL); LineTo(hdc, cx + d, cy + d); LineTo(hdc, cx + d, cy + d - s);
    FillCircle(hdc, cx, cy, 2, color); DeleteObject(pen);
}
void DrawCheckbox(HDC hdc, RECT r, bool checked, bool hover) {
    int size = r.bottom - r.top;
    RECT box = {r.left, r.top + (r.bottom - r.top - size) / 2, r.left + size, r.top + (r.bottom - r.top + size) / 2};
    FillRound(hdc, box, S(4), checked ? g_theme.accent : (hover ? RGB(60,60,70) : RGB(50,50,55)));
    StrokeRound(hdc, box, S(4), checked ? g_theme.accent : RGB(80,80,90), 1);
    if (checked) {
        HPEN pen = CreatePen(PS_SOLID, S(2), RGB(255,255,255)); SelectObject(hdc, pen);
        int cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
        MoveToEx(hdc, cx - S(5), cy, NULL); LineTo(hdc, cx - S(1), cy + S(4)); LineTo(hdc, cx + S(6), cy - S(4));
        DeleteObject(pen);
    }
}

// ================= 拾取器 =================
void InitOverlayBacking() {
    if (g_overlayBmp) return;
    g_overlayW = GetSystemMetrics(SM_CXSCREEN);
    g_overlayH = GetSystemMetrics(SM_CYSCREEN);
    HDC hScreen = GetDC(NULL);
    g_overlayDC = CreateCompatibleDC(hScreen);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = g_overlayW;
    bi.bmiHeader.biHeight = -g_overlayH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_overlayBmp = CreateDIBSection(hScreen, &bi, DIB_RGB_COLORS, &g_overlayBits, NULL, 0);
    SelectObject(g_overlayDC, g_overlayBmp);
    ReleaseDC(NULL, hScreen);
}

void UpdatePickerOverlay(HWND hwnd) {
    if (!g_overlayBits) return;
    DWORD* p = (DWORD*)g_overlayBits;
    int w = g_overlayW, h = g_overlayH;
    for (int i = 0; i < w * h; i++) p[i] = 0x55000000;

    HWND hover = g_pickerHover;
    if (hover && IsWindow(hover)) {
        RECT wr; GetWindowRect(hover, &wr);
        int x0 = wr.left, y0 = wr.top, x1 = wr.right, y1 = wr.bottom;
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
        if (x1 > w) x1 = w; if (y1 > h) y1 = h;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) p[y*w+x] = 0;

        DWORD accent = 0xFF0A84FF;
        int b = 3;
        for (int i = 0; i < b; i++) {
            int yy0 = y0 + i, yy1 = y1 - 1 - i;
            int xx0 = x0 + i, xx1 = x1 - 1 - i;
            if (yy0 >= 0 && yy0 < h) for (int x = x0; x < x1; x++) p[yy0*w+x] = accent;
            if (yy1 >= 0 && yy1 < h) for (int x = x0; x < x1; x++) p[yy1*w+x] = accent;
            if (xx0 >= 0 && xx0 < w) for (int y = y0; y < y1; y++) p[y*w+xx0] = accent;
            if (xx1 >= 0 && xx1 < w) for (int y = y0; y < y1; y++) p[y*w+xx1] = accent;
        }
    }

    HDC hScreen = GetDC(NULL);
    POINT ptSrc = {0, 0};
    SIZE size = {w, h};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd, hScreen, NULL, &size, g_overlayDC, &ptSrc, 0, &bf, ULW_ALPHA);
    ReleaseDC(NULL, hScreen);
}

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SETCURSOR: SetCursor(LoadCursorW(NULL, IDC_CROSS)); return TRUE;
    case WM_MOUSEMOVE: {
        SetCursor(LoadCursorW(NULL, IDC_CROSS));
        POINT pt; GetCursorPos(&pt);
        HWND h = WindowFromPoint(pt);
        if (h) h = GetAncestor(h, GA_ROOT);
        if (h == g_hwnd || h == hwnd) h = NULL;
        if (h != g_pickerHover) { g_pickerHover = h; UpdatePickerOverlay(hwnd); }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT pt; GetCursorPos(&pt);
        ShowWindow(hwnd, SW_HIDE);
        HWND target = WindowFromPoint(pt);
        if (target) target = GetAncestor(target, GA_ROOT);
        
        if (target && target != g_hwnd && IsWindowVisible(target)) {
            if (g_isPickingMask) {
                g_cfg.maskKey = GetKey(target);
                wchar_t buf[256] = {0}; GetWindowTextW(target, buf, 256);
                if (g_hMaskDlg && IsWindow(g_hMaskDlg)) {
                    SetWindowTextW(g_hMaskDlg, (L"面具设置 - " + std::wstring(buf)).c_str());
                    InvalidateRect(g_hMaskDlg, NULL, FALSE);
                }
                g_isPickingMask = false;
            } else {
                bool found = false;
                EnterCriticalSection(&g_cs);
                for (size_t i=0; i<g_wins.size(); i++) {
                    if (g_wins[i] == target) {
                        wchar_t buf[256] = {0}; GetWindowTextW(target, buf, 256);
                        LockTarget(target, buf);
                        found = true; break;
                    }
                }
                if (!found) {
                    wchar_t buf[256] = {0}; GetWindowTextW(target, buf, 256);
                    g_wins.push_back(target); g_titles.push_back(buf);
                    LockTarget(target, buf);
                }
                LeaveCriticalSection(&g_cs);
                SaveConfig();
            }
        }
        g_pickerHover = NULL;
        DestroyWindow(hwnd);
        g_hOverlay = NULL;
        RestoreApp(g_hwnd);
        return 0;
    }
    case WM_RBUTTONDOWN: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: g_hOverlay = NULL; g_pickerHover = NULL; RestoreApp(g_hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void StartPicker() {
    if (g_hOverlay && IsWindow(g_hOverlay)) return;
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = {sizeof(wc)}; wc.lpfnWndProc = OverlayProc; wc.hInstance = GetModuleHandleW(NULL);
        wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH); wc.lpszClassName = L"Win3COverlay";
        RegisterClassExW(&wc); reg = true;
    }
    InitOverlayBacking();
    int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
    g_pickerHover = NULL;
    g_hOverlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"Win3COverlay", L"", WS_POPUP, 0, 0, w, h, NULL, NULL, GetModuleHandleW(NULL), NULL);
    UpdatePickerOverlay(g_hOverlay);
    ShowWindow(g_hOverlay, SW_SHOW); SetForegroundWindow(g_hOverlay);
    ShowWindow(g_hwnd, SW_HIDE);
}

// ================= 菜单 =================
void PaintMenu(HWND hwnd) {
    PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(hdc); HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom); SelectObject(mem, bmp);
    FillRound(mem, rc, S(8), RGB(30, 30, 35)); StrokeRound(mem, rc, S(8), RGB(60, 60, 65), 1);
    int y = S(8);
    for (size_t i = 0; i < g_menuItems.size(); i++) {
        auto& item = g_menuItems[i]; int ih = item.isSep ? S(9) : S(36);
        if (item.isSep) {
            RECT line = {S(10), y + ih/2, rc.right - S(10), y + ih/2 + 1}; HBRUSH b = CreateSolidBrush(RGB(60, 60, 65));
            FillRect(mem, &line, b); DeleteObject(b);
        } else {
            RECT ir = {S(4), y, rc.right - S(4), y + ih};
            if (((int)i == g_menuHover) && !item.disabled) FillRound(mem, ir, S(6), g_theme.hover);
            else if (item.checked) FillRound(mem, ir, S(6), RGB(40, 80, 150));
            if (item.checked) {
                HPEN pen = CreatePen(PS_SOLID, 2, g_theme.accent); SelectObject(mem, pen);
                MoveToEx(mem, S(16), y+ih/2, NULL); LineTo(mem, S(20), y+ih/2+S(4)); LineTo(mem, S(28), y+ih/2-S(4)); DeleteObject(pen);
            }
            DrawTextC(mem, item.text.c_str(), {S(36), y, rc.right-S(10), y+ih}, g_fMenu, item.disabled ? g_theme.textDim : (i==g_menuHover ? g_theme.accent : g_theme.text), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        y += ih;
    }
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY); DeleteObject(bmp); DeleteDC(mem); EndPaint(hwnd, &ps);
}

LRESULT CALLBACK MenuProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
    case WM_PAINT: PaintMenu(hwnd); return 0;
    case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int y = S(8), idx = -1;
        for (size_t i = 0; i < g_menuItems.size(); i++) {
            int ih = g_menuItems[i].isSep ? S(9) : S(36);
            if (pt.y >= y && pt.y < y + ih) { idx = i; break; }
            y += ih;
        }
        if (idx != g_menuHover) { g_menuHover = idx; InvalidateRect(hwnd, NULL, FALSE); }
        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE: g_menuHover = -1; InvalidateRect(hwnd, NULL, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        if (g_menuHover >= 0 && !g_menuItems[g_menuHover].isSep && !g_menuItems[g_menuHover].disabled) {
            int id = g_menuItems[g_menuHover].id;
            DestroyWindow(hwnd);
            PostMessageW(g_hMenuOwner, WM_MENU_SELECTED, id, 0);
        }
        return 0;
    }
    case WM_ACTIVATE: if (LOWORD(wParam) == WA_INACTIVE) DestroyWindow(hwnd); return 0;
    case WM_DESTROY: g_hMenu = NULL; return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowMenu(HWND owner, int x, int y, int w) {
    if (g_hMenu) DestroyWindow(g_hMenu);
    static bool menuReg = false;
    if (!menuReg) {
        WNDCLASSEXW wc = {sizeof(wc)}; wc.lpfnWndProc = MenuProc; wc.hInstance = GetModuleHandleW(NULL); wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH); wc.lpszClassName = L"Win3CMenu";
        RegisterClassExW(&wc); menuReg = true;
    }
    int h = S(8)*2; for (auto& m : g_menuItems) h += m.isSep ? S(9) : S(36);
    g_hMenuOwner = owner;
    g_hMenu = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"Win3CMenu", L"", WS_POPUP, x, y, w, h, owner, NULL, GetModuleHandleW(NULL), NULL);
    SetWindowRgn(g_hMenu, CreateRoundRectRgn(0, 0, w+1, h+1, S(16), S(16)), TRUE);
    ShowWindow(g_hMenu, SW_SHOWNOACTIVATE); SetForegroundWindow(g_hMenu);
}

// ================= 帮助 =================
LRESULT CALLBACK HelpProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        HDC mem = CreateCompatibleDC(hdc); HBITMAP bmp = CreateCompatibleBitmap(hdc, S(520), S(420)); SelectObject(mem, bmp);
        RECT rc = {0, 0, S(520), S(420)};
        FillRound(mem, rc, S(12), RGB(35, 35, 40)); StrokeRound(mem, rc, S(12), RGB(60, 60, 65), 1);
        RECT title = {0, 0, S(520), S(48)}; HBRUSH tb = CreateSolidBrush(RGB(28, 28, 32)); FillRect(mem, &title, tb); DeleteObject(tb);
        RECT btnClose = {S(520) - S(46), 0, S(520), S(48)};
        if (g_helpHoverClose) { HBRUSH cb = CreateSolidBrush(RGB(232, 17, 35)); FillRect(mem, &btnClose, cb); DeleteObject(cb); }
        HPEN penC = CreatePen(PS_SOLID, 1, g_helpHoverClose ? RGB(255,255,255) : g_theme.textDim); SelectObject(mem, penC);
        int ccx = btnClose.left + S(23), ccy = S(24);
        MoveToEx(mem, ccx-S(4), ccy-S(4), NULL); LineTo(mem, ccx+S(5), ccy+S(5)); MoveToEx(mem, ccx-S(4), ccy+S(5), NULL); LineTo(mem, ccx+S(5), ccy-S(4)); DeleteObject(penC);
        DrawTextC(mem, L"使用说明", {S(20), 0, S(200), S(48)}, g_fTitle, g_theme.text, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        
        DrawTextC(mem, L"【面具防窥功能】", {S(30), S(60), S(490), S(80)}, g_fTitle, g_theme.accent, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        DrawTextC(mem, L"点击主界面“面具”按钮，选择一个伪装窗口并设置透明度。\n"
                       L"开启后，以后你通过快捷键或滑块调节当前锁定窗口的透明度时，\n"
                       L"系统会先用伪装窗口全屏挡住 2 秒，在底层偷偷修改，然后再悄悄撤走，老师绝对看不到。",
                  {S(30), S(90), S(490), S(160)}, g_fNormal, g_theme.text, DT_LEFT|DT_TOP|DT_WORDBREAK);

        DrawTextC(mem, L"【全局快捷键】", {S(30), S(180), S(490), S(200)}, g_fTitle, g_theme.accent, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        DrawTextC(mem, L"Ctrl + Alt + P      ：鼠标拾取目标窗口（带高亮框）\n"
                       L"Ctrl + Alt + B      ：极速触发一次面具伪装\n"
                       L"Ctrl + Alt + H      ：老板键（隐藏/恢复目标）\n"
                       L"Ctrl + Alt + M      ：摸鱼模式（全局变透明）\n"
                       L"Ctrl + Alt + 0      ：一键恢复全部窗口透明度",
                  {S(30), S(210), S(490), S(320)}, g_fNormal, g_theme.text, DT_LEFT|DT_TOP|DT_WORDBREAK);
                  
        DrawTextC(mem, L"【功能说明】", {S(30), S(330), S(490), S(350)}, g_fTitle, g_theme.accent, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        DrawTextC(mem, L"所有窗口的透明度配置会自动保存，重启后还原。",
                  {S(30), S(360), S(490), S(400)}, g_fNormal, g_theme.text, DT_LEFT|DT_TOP|DT_WORDBREAK);
                  
        BitBlt(hdc, 0, 0, S(520), S(420), mem, 0, 0, SRCCOPY); DeleteObject(bmp); DeleteDC(mem); EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!g_helpTracking) { TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&t); g_helpTracking = true; }
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        bool nClose = pt.x > S(520 - 46) && pt.y < S(48);
        if (nClose != g_helpHoverClose) { g_helpHoverClose = nClose; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE: g_helpTracking = false; g_helpHoverClose = false; InvalidateRect(hwnd, NULL, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (pt.x > S(520 - 46) && pt.y < S(48)) { DestroyWindow(hwnd); return 0; }
        if (pt.y < S(48)) SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowHelpWindow() {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = {sizeof(wc)}; wc.lpfnWndProc = HelpProc; wc.hInstance = GetModuleHandleW(NULL); wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH); wc.lpszClassName = L"Win3CHelp";
        RegisterClassExW(&wc); reg = true;
    }
    int x = (GetSystemMetrics(SM_CXSCREEN) - S(520)) / 2; int y = (GetSystemMetrics(SM_CYSCREEN) - S(420)) / 2;
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"Win3CHelp", L"使用说明", WS_POPUP, x, y, S(520), S(420), g_hwnd, NULL, GetModuleHandleW(NULL), NULL);
    SetWindowRgn(h, CreateRoundRectRgn(0, 0, S(520)+1, S(420)+1, S(24), S(24)), TRUE);
    ShowWindow(h, SW_SHOW);
}

// ================= 渐隐对话框 =================
void ParseFadeInputs(int* outSec, int* outPct) {
    wchar_t buf[32];
    if (outSec) { GetWindowTextW(g_hFadeSecEdit, buf, 32); int s = _wtoi(buf); if (s < 1) s = 1; if (s > 120) s = 120; *outSec = s; }
    if (outPct) { GetWindowTextW(g_hFadePctEdit, buf, 32); int p = _wtoi(buf); if (p < 1) p = 1; if (p > 100) p = 100; *outPct = p; }
}

LRESULT CALLBACK FadeEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) { PostMessageW(GetParent(hwnd), WM_COMMAND, 9001, 0); return 0; }
        if (wParam == VK_ESCAPE) { PostMessageW(GetParent(hwnd), WM_COMMAND, 9002, 0); return 0; }
        if (wParam == VK_TAB) { HWND next = (hwnd == g_hFadeSecEdit) ? g_hFadePctEdit : g_hFadeSecEdit; SetFocus(next); SendMessageW(next, EM_SETSEL, 0, -1); return 0; }
    }
    if (msg == WM_CHAR) {
        if ((wParam >= '0' && wParam <= '9') || wParam == VK_BACK || wParam < 32) return CallWindowProcW(g_oldEditProc, hwnd, msg, wParam, lParam);
        return 0;
    }
    return CallWindowProcW(g_oldEditProc, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK FadeDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const int W = 400, H = 240;
    switch (msg) {
    case WM_CREATE: {
        g_fadeDlgHover = -1;
        wchar_t secBuf[16], pctBuf[16];
        wsprintfW(secBuf, L"%d", g_lastFadeSec);
        wsprintfW(pctBuf, L"%d", g_lastFadePct);
        g_hFadeSecEdit = CreateWindowExW(0, L"EDIT", secBuf, WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_RIGHT | ES_AUTOHSCROLL | WS_TABSTOP, S(128), S(70), S(114), S(24), hwnd, (HMENU)7001, NULL, NULL);
        g_hFadePctEdit = CreateWindowExW(0, L"EDIT", pctBuf, WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_RIGHT | ES_AUTOHSCROLL | WS_TABSTOP, S(128), S(120), S(114), S(24), hwnd, (HMENU)7002, NULL, NULL);
        SendMessageW(g_hFadeSecEdit, WM_SETFONT, (WPARAM)g_fNormal, TRUE);
        SendMessageW(g_hFadePctEdit, WM_SETFONT, (WPARAM)g_fNormal, TRUE);
        SendMessageW(g_hFadeSecEdit, EM_SETLIMITTEXT, 3, 0);
        SendMessageW(g_hFadePctEdit, EM_SETLIMITTEXT, 3, 0);
        g_oldEditProc = (WNDPROC)SetWindowLongPtrW(g_hFadeSecEdit, GWLP_WNDPROC, (LONG_PTR)FadeEditProc);
        SetWindowLongPtrW(g_hFadePctEdit, GWLP_WNDPROC, (LONG_PTR)FadeEditProc);
        SetFocus(g_hFadeSecEdit);
        SendMessageW(g_hFadeSecEdit, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam; SetTextColor(hdc, g_theme.text); SetBkColor(hdc, RGB(45, 45, 50));
        static HBRUSH b = NULL; if (!b) b = CreateSolidBrush(RGB(45, 45, 50)); return (LRESULT)b;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        HDC mem = CreateCompatibleDC(hdc); HBITMAP bmp = CreateCompatibleBitmap(hdc, S(W), S(H)); SelectObject(mem, bmp);
        RECT rc = {0, 0, S(W), S(H)};
        FillRound(mem, rc, S(12), RGB(35, 35, 40)); StrokeRound(mem, rc, S(12), RGB(60, 60, 65), 1);
        RECT title = {0, 0, S(W), S(48)}; HBRUSH tb = CreateSolidBrush(RGB(28, 28, 32)); FillRect(mem, &title, tb); DeleteObject(tb);
        RECT btnClose = {S(W) - S(46), 0, S(W), S(48)};
        if (g_fadeDlgHover == 2) { HBRUSH cb = CreateSolidBrush(RGB(232, 17, 35)); FillRect(mem, &btnClose, cb); DeleteObject(cb); }
        HPEN penC = CreatePen(PS_SOLID, 1, g_fadeDlgHover == 2 ? RGB(255,255,255) : g_theme.textDim); SelectObject(mem, penC);
        int ccx = btnClose.left + S(23), ccy = S(24);
        MoveToEx(mem, ccx-S(4), ccy-S(4), NULL); LineTo(mem, ccx+S(5), ccy+S(5)); MoveToEx(mem, ccx-S(4), ccy+S(5), NULL); LineTo(mem, ccx+S(5), ccy-S(4)); DeleteObject(penC);
        DrawTextC(mem, L"渐隐设置", {S(20), 0, S(200), S(48)}, g_fTitle, g_theme.text, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        DrawTextC(mem, L"渐隐时长：", {S(28), S(66), S(120), S(98)}, g_fNormal, g_theme.text, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        DrawTextC(mem, L"目标透明度：", {S(28), S(116), S(120), S(148)}, g_fNormal, g_theme.text, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT secFrame = {S(120), S(66), S(250), S(98)}; FillRound(mem, secFrame, S(6), RGB(45, 45, 50)); StrokeRound(mem, secFrame, S(6), RGB(70, 70, 75), 1);
        RECT pctFrame = {S(120), S(116), S(250), S(148)}; FillRound(mem, pctFrame, S(6), RGB(45, 45, 50)); StrokeRound(mem, pctFrame, S(6), RGB(70, 70, 75), 1);
        DrawTextC(mem, L"秒", {S(258), S(66), S(300), S(98)}, g_fNormal, g_theme.textDim, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        DrawTextC(mem, L"%",  {S(258), S(116), S(300), S(148)}, g_fNormal, g_theme.textDim, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        DrawTextC(mem, L"时长 1~120 秒，目标透明度 1~100%", {S(28), S(156), S(W)-S(28), S(178)}, g_fSmall, g_theme.textDim, DT_LEFT|DT_SINGLELINE);
        int by = S(190), bw2 = S(80), bh = S(34);
        RECT cancelRc = {S(W) - S(30) - bw2*2 - S(10), by, S(W) - S(30) - bw2 - S(10), by+bh};
        RECT okRc     = {S(W) - S(30) - bw2,            by, S(W) - S(30),                by+bh};
        FillRound(mem, cancelRc, S(8), (g_fadeDlgHover==0) ? g_theme.hover : RGB(50, 50, 55));
        StrokeRound(mem, cancelRc, S(8), RGB(70, 70, 75), 1);
        DrawTextC(mem, L"取消", cancelRc, g_fNormal, (g_fadeDlgHover==0) ? g_theme.accent : g_theme.text, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        FillRound(mem, okRc, S(8), (g_fadeDlgHover==1) ? RGB(30, 150, 255) : g_theme.accent);
        DrawTextC(mem, L"确定", okRc, g_fNormal, RGB(255, 255, 255), DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        BitBlt(hdc, 0, 0, S(W), S(H), mem, 0, 0, SRCCOPY); DeleteObject(bmp); DeleteDC(mem); EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int hb = -1;
        if (pt.x > S(W) - S(46) && pt.y < S(48)) hb = 2;
        else if (pt.y >= S(190) && pt.y <= S(224)) {
            int bw2 = S(80);
            if (pt.x >= S(W) - S(30) - bw2*2 - S(10) && pt.x <= S(W) - S(30) - bw2 - S(10)) hb = 0;
            else if (pt.x >= S(W) - S(30) - bw2 && pt.x <= S(W) - S(30)) hb = 1;
        }
        if (hb != g_fadeDlgHover) { g_fadeDlgHover = hb; InvalidateRect(hwnd, NULL, FALSE); }
        TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE: g_fadeDlgHover = -1; InvalidateRect(hwnd, NULL, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (pt.x > S(W) - S(46) && pt.y < S(48)) { DestroyWindow(hwnd); return 0; }
        if (pt.y >= S(190) && pt.y <= S(224)) {
            int bw2 = S(80);
            if (pt.x >= S(W) - S(30) - bw2*2 - S(10) && pt.x <= S(W) - S(30) - bw2 - S(10)) { DestroyWindow(hwnd); return 0; }
            if (pt.x >= S(W) - S(30) - bw2 && pt.x <= S(W) - S(30)) { PostMessageW(hwnd, WM_COMMAND, 9001, 0); return 0; }
        }
        if (pt.y < S(48) && pt.x < S(W) - S(46)) SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == 9001) { int s = 5, p = 50; ParseFadeInputs(&s, &p); DestroyWindow(hwnd); StartCustomFade(s, p); SaveConfig(); return 0; }
        if (LOWORD(wParam) == 9002) { DestroyWindow(hwnd); return 0; }
        return 0;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: g_hFadeDlg = NULL; g_hFadeSecEdit = NULL; g_hFadePctEdit = NULL; g_fadeDlgHover = -1; return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowFadeDialog() {
    if (g_hFadeDlg && IsWindow(g_hFadeDlg)) { SetForegroundWindow(g_hFadeDlg); return; }
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = {sizeof(wc)}; wc.lpfnWndProc = FadeDlgProc; wc.hInstance = GetModuleHandleW(NULL);
        wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH); wc.lpszClassName = L"Win3CFadeDlg";
        RegisterClassExW(&wc); reg = true;
    }
    int w = S(400), h = S(240);
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    g_hFadeDlg = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"Win3CFadeDlg", L"渐隐设置", WS_POPUP, x, y, w, h, g_hwnd, NULL, GetModuleHandleW(NULL), NULL);
    SetWindowRgn(g_hFadeDlg, CreateRoundRectRgn(0, 0, w+1, h+1, S(24), S(24)), TRUE);
    ShowWindow(g_hFadeDlg, SW_SHOW); SetForegroundWindow(g_hFadeDlg);
}

// ================= 面具设置对话框 =================
LRESULT CALLBACK MaskEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) { PostMessageW(GetParent(hwnd), WM_COMMAND, 9201, 0); return 0; }
        if (wParam == VK_ESCAPE) { PostMessageW(GetParent(hwnd), WM_COMMAND, 9202, 0); return 0; }
    }
    if (msg == WM_CHAR) {
        if ((wParam >= '0' && wParam <= '9') || wParam == VK_BACK || wParam < 32) return CallWindowProcW(g_oldMaskEditProc, hwnd, msg, wParam, lParam);
        return 0;
    }
    return CallWindowProcW(g_oldMaskEditProc, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK MaskDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const int W = 420, H = 260;
    switch (msg) {
    case WM_CREATE: {
        g_maskDlgHover = -1;
        wchar_t pctBuf[16];
        wsprintfW(pctBuf, L"%d", (g_cfg.maskAlpha * 100 + 127) / 255);
        g_hMaskPctEdit = CreateWindowExW(0, L"EDIT", pctBuf, WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_RIGHT | ES_AUTOHSCROLL, S(160), S(142), S(100), S(24), hwnd, (HMENU)7003, NULL, NULL);
        SendMessageW(g_hMaskPctEdit, WM_SETFONT, (WPARAM)g_fNormal, TRUE);
        SendMessageW(g_hMaskPctEdit, EM_SETLIMITTEXT, 3, 0);
        g_oldMaskEditProc = (WNDPROC)SetWindowLongPtrW(g_hMaskPctEdit, GWLP_WNDPROC, (LONG_PTR)MaskEditProc);
        
        std::wstring title = L"面具设置";
        if (!g_cfg.maskKey.empty()) title += L" - 已就绪";
        SetWindowTextW(hwnd, title.c_str());
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam; SetTextColor(hdc, g_theme.text); SetBkColor(hdc, RGB(45, 45, 50));
        static HBRUSH b = NULL; if (!b) b = CreateSolidBrush(RGB(45, 45, 50)); return (LRESULT)b;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        HDC mem = CreateCompatibleDC(hdc); HBITMAP bmp = CreateCompatibleBitmap(hdc, S(W), S(H)); SelectObject(mem, bmp);
        RECT rc = {0, 0, S(W), S(H)};
        FillRound(mem, rc, S(12), RGB(35, 35, 40)); StrokeRound(mem, rc, S(12), RGB(60, 60, 65), 1);
        RECT title = {0, 0, S(W), S(48)}; HBRUSH tb = CreateSolidBrush(RGB(28, 28, 32)); FillRect(mem, &title, tb); DeleteObject(tb);
        
        RECT btnClose = {S(W) - S(46), 0, S(W), S(48)};
        if (g_maskDlgHover == 3) { HBRUSH cb = CreateSolidBrush(RGB(232, 17, 35)); FillRect(mem, &btnClose, cb); DeleteObject(cb); }
        HPEN penC = CreatePen(PS_SOLID, 1, g_maskDlgHover == 3 ? RGB(255,255,255) : g_theme.textDim); SelectObject(mem, penC);
        int ccx = btnClose.left + S(23), ccy = S(24);
        MoveToEx(mem, ccx-S(4), ccy-S(4), NULL); LineTo(mem, ccx+S(5), ccy+S(5)); MoveToEx(mem, ccx-S(4), ccy+S(5), NULL); LineTo(mem, ccx+S(5), ccy-S(4)); DeleteObject(penC);
        
        DrawTextC(mem, L"面具防窥设置", {S(20), 0, S(200), S(48)}, g_fTitle, g_theme.text, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        
        DrawTextC(mem, L"1. 选择用于防窥的窗口：", {S(30), S(60), S(300), S(85)}, g_fNormal, g_theme.text, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT selBtn = {S(30), S(90), S(W)-S(30), S(120)};
        FillRound(mem, selBtn, S(8), (g_maskDlgHover==0) ? g_theme.hover : RGB(45,45,50));
        StrokeRound(mem, selBtn, S(8), RGB(70,70,75), 1);
        std::wstring maskName = L"点击此处拾取伪装窗口...";
        if (!g_cfg.maskKey.empty()) {
            size_t pos = g_cfg.maskKey.find(L"|");
            maskName = (pos != std::wstring::npos) ? g_cfg.maskKey.substr(0, pos) : g_cfg.maskKey;
            maskName = L"已选面具： " + maskName;
        }
        DrawTextC(mem, maskName.c_str(), {selBtn.left + S(10), selBtn.top, selBtn.right - S(10), selBtn.bottom}, g_fNormal, (g_maskDlgHover==0) ? g_theme.accent : g_theme.text, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        
        DrawTextC(mem, L"2. 目标透明度：", {S(30), S(140), S(150), S(170)}, g_fNormal, g_theme.text, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT pctFrame = {S(155), S(140), S(265), S(170)}; FillRound(mem, pctFrame, S(6), RGB(45, 45, 50)); StrokeRound(mem, pctFrame, S(6), RGB(70, 70, 75), 1);
        DrawTextC(mem, L"%", {S(270), S(140), S(300), S(170)}, g_fNormal, g_theme.textDim, DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        int by = S(200), bw2 = S(80), bh = S(34);
        RECT cancelRc = {S(W) - S(30) - bw2*2 - S(10), by, S(W) - S(30) - bw2 - S(10), by+bh};
        RECT okRc     = {S(W) - S(30) - bw2,            by, S(W) - S(30),                by+bh};
        FillRound(mem, cancelRc, S(8), (g_maskDlgHover==1) ? g_theme.hover : RGB(50, 50, 55));
        StrokeRound(mem, cancelRc, S(8), RGB(70, 70, 75), 1);
        DrawTextC(mem, L"取消", cancelRc, g_fNormal, (g_maskDlgHover==1) ? g_theme.accent : g_theme.text, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        FillRound(mem, okRc, S(8), (g_maskDlgHover==2) ? RGB(30, 150, 255) : g_theme.accent);
        DrawTextC(mem, L"确定开启", okRc, g_fNormal, RGB(255, 255, 255), DT_CENTER|DT_VCENTER|DT_SINGLELINE);

        BitBlt(hdc, 0, 0, S(W), S(H), mem, 0, 0, SRCCOPY); DeleteObject(bmp); DeleteDC(mem); EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int hb = -1;
        if (pt.x > S(W) - S(46) && pt.y < S(48)) hb = 3;
        else if (pt.y >= S(90) && pt.y <= S(120) && pt.x >= S(30) && pt.x <= S(W)-S(30)) hb = 0;
        else if (pt.y >= S(200) && pt.y <= S(234)) {
            int bw2 = S(80);
            if (pt.x >= S(W) - S(30) - bw2*2 - S(10) && pt.x <= S(W) - S(30) - bw2 - S(10)) hb = 1;
            else if (pt.x >= S(W) - S(30) - bw2 && pt.x <= S(W) - S(30)) hb = 2;
        }
        if (hb != g_maskDlgHover) { g_maskDlgHover = hb; InvalidateRect(hwnd, NULL, FALSE); }
        TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE: g_maskDlgHover = -1; InvalidateRect(hwnd, NULL, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (pt.x > S(W) - S(46) && pt.y < S(48)) { DestroyWindow(hwnd); return 0; }
        if (pt.y >= S(90) && pt.y <= S(120) && pt.x >= S(30) && pt.x <= S(W)-S(30)) {
            g_isPickingMask = true;
            StartPicker();
            return 0;
        }
        if (pt.y >= S(200) && pt.y <= S(234)) {
            int bw2 = S(80);
            if (pt.x >= S(W) - S(30) - bw2*2 - S(10) && pt.x <= S(W) - S(30) - bw2 - S(10)) { DestroyWindow(hwnd); return 0; }
            if (pt.x >= S(W) - S(30) - bw2 && pt.x <= S(W) - S(30)) { PostMessageW(hwnd, WM_COMMAND, 9201, 0); return 0; }
        }
        if (pt.y < S(48) && pt.x < S(W) - S(46)) SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == 9201) { // 确定开启
            if (g_cfg.maskKey.empty()) {
                MessageBoxW(hwnd, L"请先选择一个面具窗口！", L"提示", MB_OK | MB_ICONWARNING);
                return 0;
            }
            wchar_t buf[16];
            GetWindowTextW(g_hMaskPctEdit, buf, 16);
            int pct = _wtoi(buf);
            if (pct < 1) pct = 1; if (pct > 100) pct = 100;
            g_cfg.maskAlpha = pct * 255 / 100;
            g_cfg.maskEnabled = true;
            SaveConfig();
            DestroyWindow(hwnd);
            
            // 立刻测试一下（戴个面具）
            HWND t = GetRealTarget();
            if (IsWindow(t)) ExecuteWithMask(t, g_cfg.maskAlpha, true);
            return 0;
        }
        if (LOWORD(wParam) == 9202) { DestroyWindow(hwnd); return 0; }
        return 0;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: g_hMaskDlg = NULL; g_hMaskPctEdit = NULL; g_maskDlgHover = -1; return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowMaskDialog() {
    if (g_hMaskDlg && IsWindow(g_hMaskDlg)) { SetForegroundWindow(g_hMaskDlg); return; }
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = {sizeof(wc)}; wc.lpfnWndProc = MaskDlgProc; wc.hInstance = GetModuleHandleW(NULL);
        wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH); wc.lpszClassName = L"Win3CMaskDlg";
        RegisterClassExW(&wc); reg = true;
    }
    int w = S(420), h = S(260);
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    g_hMaskDlg = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"Win3CMaskDlg", L"面具设置", WS_POPUP, x, y, w, h, g_hwnd, NULL, GetModuleHandleW(NULL), NULL);
    SetWindowRgn(g_hMaskDlg, CreateRoundRectRgn(0, 0, w+1, h+1, S(24), S(24)), TRUE);
    ShowWindow(g_hMaskDlg, SW_SHOW); SetForegroundWindow(g_hMaskDlg);
}

// ================= 设置窗口 =================
LRESULT CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const int W = 440, H = 380;
    switch (msg) {
    case WM_CREATE:
        g_settingsHover = -1;
        g_settingsAutoStartTmp = g_cfg.autoStart;
        g_settingsRestoreTmp = g_cfg.restoreOnExit;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        HDC mem = CreateCompatibleDC(hdc); HBITMAP bmp = CreateCompatibleBitmap(hdc, S(W), S(H)); SelectObject(mem, bmp);
        RECT rc = {0, 0, S(W), S(H)};
        FillRound(mem, rc, S(12), RGB(35, 35, 40)); StrokeRound(mem, rc, S(12), RGB(60, 60, 65), 1);
        RECT title = {0, 0, S(W), S(48)}; HBRUSH tb = CreateSolidBrush(RGB(28, 28, 32)); FillRect(mem, &title, tb); DeleteObject(tb);
        RECT btnClose = {S(W) - S(46), 0, S(W), S(48)};
        if (g_settingsHover == 6) { HBRUSH cb = CreateSolidBrush(RGB(232, 17, 35)); FillRect(mem, &btnClose, cb); DeleteObject(cb); }
        HPEN penC = CreatePen(PS_SOLID, 1, g_settingsHover==6 ? RGB(255,255,255) : g_theme.textDim); SelectObject(mem, penC);
        int ccx = btnClose.left + S(23), ccy = S(24);
        MoveToEx(mem, ccx-S(4), ccy-S(4), NULL); LineTo(mem, ccx+S(5), ccy+S(5));
        MoveToEx(mem, ccx-S(4), ccy+S(5), NULL); LineTo(mem, ccx+S(5), ccy-S(4)); DeleteObject(penC);
        DrawTextC(mem, L"设置", {S(20), 0, S(200), S(48)}, g_fTitle, g_theme.text, DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        int rowY = S(70), rowH = S(32);
        RECT r1 = {S(30), rowY, S(W) - S(30), rowY + rowH};
        DrawCheckbox(mem, {r1.left, r1.top, r1.left + S(20), r1.bottom}, g_settingsAutoStartTmp, g_settingsHover == 0);
        DrawTextC(mem, L"开机自启（写入 HKCU\\Run）", {r1.left + S(30), r1.top, r1.right, r1.bottom}, g_fNormal, g_theme.text, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT r2 = {S(30), rowY + S(38), S(W) - S(30), rowY + S(38) + rowH};
        DrawCheckbox(mem, {r2.left, r2.top, r2.left + S(20), r2.bottom}, g_settingsRestoreTmp, g_settingsHover == 1);
        DrawTextC(mem, L"退出时自动恢复所有窗口透明度", {r2.left + S(30), r2.top, r2.right, r2.bottom}, g_fNormal, g_theme.text, DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        RECT sep = {S(30), S(180), S(W) - S(30), S(181)};
        HBRUSH sb = CreateSolidBrush(RGB(60, 60, 65)); FillRect(mem, &sep, sb); DeleteObject(sb);

        RECT hbR = {S(30), S(200), S(180), S(236)};
        RECT rsR = {S(190), S(200), S(W) - S(30), S(236)};
        FillRound(mem, hbR, S(8), (g_settingsHover == 2) ? g_theme.hover : RGB(45, 45, 50));
        StrokeRound(mem, hbR, S(8), (g_settingsHover == 2) ? g_theme.accent : RGB(70, 70, 75), 1);
        DrawTextC(mem, L"打开使用说明", hbR, g_fNormal, (g_settingsHover == 2) ? g_theme.accent : g_theme.text, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        FillRound(mem, rsR, S(8), (g_settingsHover == 3) ? RGB(80, 40, 40) : RGB(45, 45, 50));
        StrokeRound(mem, rsR, S(8), (g_settingsHover == 3) ? RGB(220, 80, 80) : RGB(70, 70, 75), 1);
        DrawTextC(mem, L"重置所有窗口透明度", rsR, g_fNormal, (g_settingsHover == 3) ? RGB(255, 140, 140) : g_theme.text, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

        RECT sep2 = {S(30), S(252), S(W) - S(30), S(253)};
        HBRUSH sb2 = CreateSolidBrush(RGB(60, 60, 65)); FillRect(mem, &sep2, sb2); DeleteObject(sb2);

        DrawTextC(mem, APP_NAME, {S(30), S(266), S(W) - S(30), S(294)}, g_fTitle, g_theme.accent, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        DrawTextC(mem, APP_VERSION, {S(30), S(294), S(W) - S(30), S(316)}, g_fNormal, g_theme.textDim, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        DrawTextC(mem, APP_TAGLINE, {S(30), S(316), S(W) - S(30), S(338)}, g_fSmall, g_theme.textDim, DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        int by = S(H) - S(56), bw2 = S(86), bh = S(36);
        RECT cancelRc = {S(W) - S(30) - bw2*2 - S(10), by, S(W) - S(30) - bw2 - S(10), by + bh};
        RECT okRc     = {S(W) - S(30) - bw2,            by, S(W) - S(30),                 by + bh};
        FillRound(mem, cancelRc, S(8), (g_settingsHover == 5) ? g_theme.hover : RGB(50, 50, 55));
        StrokeRound(mem, cancelRc, S(8), RGB(70, 70, 75), 1);
        DrawTextC(mem, L"取消", cancelRc, g_fNormal, (g_settingsHover == 5) ? g_theme.accent : g_theme.text, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        FillRound(mem, okRc, S(8), (g_settingsHover == 4) ? RGB(30, 150, 255) : g_theme.accent);
        DrawTextC(mem, L"确定", okRc, g_fNormal, RGB(255, 255, 255), DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        BitBlt(hdc, 0, 0, S(W), S(H), mem, 0, 0, SRCCOPY); DeleteObject(bmp); DeleteDC(mem); EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int hb = -1;
        if (pt.x > S(W) - S(46) && pt.y < S(48)) hb = 6;
        else if (pt.y >= S(70) && pt.y < S(70) + S(32) && pt.x >= S(30) && pt.x < S(W) - S(30)) hb = 0;
        else if (pt.y >= S(108) && pt.y < S(108) + S(32) && pt.x >= S(30) && pt.x < S(W) - S(30)) hb = 1;
        else if (pt.y >= S(200) && pt.y < S(236)) {
            if (pt.x >= S(30) && pt.x < S(180)) hb = 2;
            else if (pt.x >= S(190) && pt.x < S(W) - S(30)) hb = 3;
        } else if (pt.y >= S(H) - S(56) && pt.y < S(H) - S(20)) {
            int bw2 = S(86);
            if (pt.x >= S(W) - S(30) - bw2*2 - S(10) && pt.x < S(W) - S(30) - bw2 - S(10)) hb = 5;
            else if (pt.x >= S(W) - S(30) - bw2 && pt.x < S(W) - S(30)) hb = 4;
        }
        if (hb != g_settingsHover) { g_settingsHover = hb; InvalidateRect(hwnd, NULL, FALSE); }
        TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE: g_settingsHover = -1; InvalidateRect(hwnd, NULL, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (pt.x > S(W) - S(46) && pt.y < S(48)) { DestroyWindow(hwnd); return 0; }
        if (pt.y >= S(70) && pt.y < S(70) + S(32) && pt.x >= S(30) && pt.x < S(W) - S(30)) {
            g_settingsAutoStartTmp = !g_settingsAutoStartTmp; InvalidateRect(hwnd, NULL, FALSE); return 0;
        }
        if (pt.y >= S(108) && pt.y < S(108) + S(32) && pt.x >= S(30) && pt.x < S(W) - S(30)) {
            g_settingsRestoreTmp = !g_settingsRestoreTmp; InvalidateRect(hwnd, NULL, FALSE); return 0;
        }
        if (pt.y >= S(200) && pt.y < S(236)) {
            if (pt.x >= S(30) && pt.x < S(180)) { ShowHelpWindow(); return 0; }
            if (pt.x >= S(190) && pt.x < S(W) - S(30)) { ResetAllWindows(); SaveConfig(); return 0; }
        }
        if (pt.y >= S(H) - S(56) && pt.y < S(H) - S(20)) {
            int bw2 = S(86);
            if (pt.x >= S(W) - S(30) - bw2*2 - S(10) && pt.x < S(W) - S(30) - bw2 - S(10)) { DestroyWindow(hwnd); return 0; }
            if (pt.x >= S(W) - S(30) - bw2 && pt.x < S(W) - S(30)) { PostMessageW(hwnd, WM_COMMAND, 9101, 0); return 0; }
        }
        if (pt.y < S(48) && pt.x < S(W) - S(46)) SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == 9101) {
            SetAutoStart(g_settingsAutoStartTmp);
            g_cfg.autoStart = g_settingsAutoStartTmp;
            g_cfg.restoreOnExit = g_settingsRestoreTmp;
            SaveConfig();
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == 9102) { DestroyWindow(hwnd); return 0; }
        return 0;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: g_hSettingsDlg = NULL; g_settingsHover = -1; return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowSettingsDialog() {
    if (g_hSettingsDlg && IsWindow(g_hSettingsDlg)) { SetForegroundWindow(g_hSettingsDlg); return; }
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = {sizeof(wc)}; wc.lpfnWndProc = SettingsDlgProc; wc.hInstance = GetModuleHandleW(NULL);
        wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH); wc.lpszClassName = L"Win3CSettings";
        RegisterClassExW(&wc); reg = true;
    }
    int w = S(440), h = S(380);
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    g_hSettingsDlg = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"Win3CSettings", L"设置", WS_POPUP, x, y, w, h, g_hwnd, NULL, GetModuleHandleW(NULL), NULL);
    SetWindowRgn(g_hSettingsDlg, CreateRoundRectRgn(0, 0, w+1, h+1, S(24), S(24)), TRUE);
    ShowWindow(g_hSettingsDlg, SW_SHOW); SetForegroundWindow(g_hSettingsDlg);
}

// ================= 主界面绘制 =================
void PaintMain(HWND hwnd) {
    std::wstring titleLocal;
    bool has;
    HWND lockedTargetLocal;
    int renderAlpha;
    EnterCriticalSection(&g_cs);
    titleLocal = g_locked_title;
    has = !g_locked_key.empty();
    lockedTargetLocal = g_locked_target;
    renderAlpha = g_render_alpha;
    LeaveCriticalSection(&g_cs);

    PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
    HDC mem = CreateCompatibleDC(hdc); HBITMAP bmp = CreateCompatibleBitmap(hdc, S(WIN_W), S(WIN_H)); SelectObject(mem, bmp); SetBkMode(mem, TRANSPARENT);

    RECT sidebar = {0, 0, S(SIDEBAR_W), S(WIN_H)}; HBRUSH sbB = CreateSolidBrush(RGB(28, 28, 32)); FillRect(mem, &sidebar, sbB); DeleteObject(sbB);
    RECT line = {S(SIDEBAR_W), 0, S(SIDEBAR_W)+1, S(WIN_H)}; HBRUSH lb = CreateSolidBrush(RGB(50, 50, 55)); FillRect(mem, &line, lb); DeleteObject(lb);

    FillCircle(mem, S(30), S(40), S(6), g_theme.accent); FillCircle(mem, S(30), S(40), S(3), RGB(255, 255, 255));
    DrawTextC(mem, APP_NAME, {S(45), S(25), S(200), S(45)}, g_fTitle, g_theme.text, DT_LEFT|DT_SINGLELINE);
    DrawTextC(mem, APP_VERSION, {S(45), S(45), S(200), S(65)}, g_fSmall, g_theme.textDim, DT_LEFT|DT_SINGLELINE);

    DrawTextC(mem, L"搜索目标窗口", {S(20), S(100), S(200), S(120)}, g_fSmall, g_theme.textDim, DT_LEFT|DT_SINGLELINE);
    RECT sb = {S(20), S(125), S(SIDEBAR_W - 20), S(125 + 36)};
    FillRound(mem, sb, S(8), RGB(40, 40, 45)); StrokeRound(mem, sb, S(8), RGB(60, 60, 65), 1);

    DrawTextC(mem, L"已锁定窗口", {S(20), S(175), S(200), S(195)}, g_fSmall, g_theme.textDim, DT_LEFT|DT_SINGLELINE);
    RECT tb = {S(20), S(200), S(SIDEBAR_W - 20), S(200 + 44)};
    FillRound(mem, tb, S(8), g_hoverTarget ? RGB(45,45,50) : RGB(35, 35, 40)); StrokeRound(mem, tb, S(8), g_hoverTarget ? g_theme.accent : RGB(60, 60, 65), g_hoverTarget ? 2 : 1);
    DrawTextC(mem, titleLocal.c_str(), {tb.left+S(12), tb.top, tb.right-S(30), tb.bottom}, g_fNormal, has?g_theme.text:g_theme.textDim, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

    RECT br = {S(20), S(260), S(SIDEBAR_W - 20) - 46, S(260 + 36)};
    FillRound(mem, br, S(8), g_hoverBtn==0 ? RGB(50, 50, 55) : RGB(38, 38, 42));
    DrawTextC(mem, L"刷新窗口", br, g_fNormal, g_hoverBtn==0?g_theme.accent:g_theme.text, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    RECT prBtn = {S(SIDEBAR_W - 20) - 36, S(260), S(SIDEBAR_W - 20), S(260 + 36)};
    FillRound(mem, prBtn, S(8), g_hoverPick ? RGB(50, 50, 55) : RGB(38, 38, 42));
    DrawCrosshair(mem, (prBtn.left+prBtn.right)/2, (prBtn.top+prBtn.bottom)/2, g_hoverPick ? g_theme.accent : g_theme.textDim);

    // 底部三个按钮
    int bw = (SIDEBAR_W - S(40) - S(20)) / 3;
    RECT selfRc = {S(20), S(WIN_H - 60), S(20) + bw, S(WIN_H - 20)};
    RECT minRc = {S(30) + bw, S(WIN_H - 60), S(30) + bw * 2, S(WIN_H - 20)};
    RECT clsRc = {S(40) + bw * 2, S(WIN_H - 60), S(40) + bw * 3, S(WIN_H - 20)};
    
    FillRound(mem, selfRc, S(6), g_hoverSelf ? RGB(50, 50, 55) : RGB(38, 38, 42));
    DrawTextC(mem, L"自透", selfRc, g_fNormal, g_hoverSelf ? g_theme.accent : g_theme.textDim, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    
    FillRound(mem, minRc, S(6), g_hoverMin ? RGB(50, 50, 55) : RGB(38, 38, 42));
    DrawTextC(mem, L"隐藏", minRc, g_fNormal, g_hoverMin ? g_theme.accent : g_theme.textDim, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    
    FillRound(mem, clsRc, S(6), g_hoverClose ? RGB(60, 30, 30) : RGB(38, 38, 42));
    DrawTextC(mem, L"退出", clsRc, g_fNormal, g_hoverClose ? RGB(255, 100, 100) : g_theme.textDim, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    int cx = S(SIDEBAR_W) + S(40); int cw = S(WIN_W) - cx - S(40);
    COLORREF valCol = has ? g_theme.accent : g_theme.textDim;

    DrawTextC(mem, L"透明度", {cx, S(50), cx+cw, S(80)}, g_fNormal, g_theme.textDim, DT_LEFT|DT_SINGLELINE);
    wchar_t val[16]; if (has) wsprintfW(val, L"%d%%", (renderAlpha*100+127)/255); else wsprintfW(val, L"--");
    DrawTextC(mem, val, {cx, S(90), cx+cw, S(220)}, g_fBig, valCol, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

    if (!has) DrawTextC(mem, L"← 请先在左侧选择目标窗口", {cx, S(235), cx+cw, S(255)}, g_fSmall, RGB(200, 80, 80), DT_RIGHT|DT_SINGLELINE);

    int sy = S(260), sh = S(8);
    RECT tr = {cx, sy, cx+cw, sy+sh}; FillRound(mem, tr, sh/2, RGB(50, 50, 55));
    int kx = cx + cw * (renderAlpha - MIN_ALPHA) / (MAX_ALPHA - MIN_ALPHA);
    if (has && renderAlpha > MIN_ALPHA) { RECT fr = {cx, sy, kx, sy+sh}; FillRound(mem, fr, sh/2, g_theme.accent); }
    FillCircle(mem, kx, sy+sh/2, S(12), RGB(25, 25, 30)); FillCircle(mem, kx, sy+sh/2, S(10), has ? g_theme.accent : RGB(80,80,85));
    if (has) FillCircle(mem, kx, sy+sh/2, S(4), RGB(255, 255, 255));

    COLORREF btnBg = has ? RGB(38,38,42) : RGB(28,28,32);
    COLORREF btnHover = has ? RGB(50,50,55) : RGB(28,28,32);
    COLORREF btnText = has ? g_theme.text : RGB(80,80,85);

    const wchar_t* pre[] = {L"25%", L"50%", L"75%", L"100%"};
    int pw = (cw - S(30)) / 4;
    for(int i=0; i<4; i++) {
        RECT prc = {cx + i*(pw+S(10)), S(320), cx + i*(pw+S(10)) + pw, S(320+44)};
        bool hov = has && (g_hoverPreset == i);
        FillRound(mem, prc, S(8), hov ? btnHover : btnBg); StrokeRound(mem, prc, S(8), hov ? g_theme.accent : RGB(60,60,65), 1);
        DrawTextC(mem, pre[i], prc, g_fNormal, hov ? g_theme.accent : btnText, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    // 5个功能按钮
    int bx = cx;
    const wchar_t* fb[] = {L"置顶", L"穿透", L"面具", L"更多...", L"设置"};
    bool* hovs[] = {&g_hoverTopmost, &g_hoverClick, &g_hoverMaskBtn, &g_hoverMore, &g_hoverSettings};
    int bwu = (cw - S(40)) / 5;

    for(int i=0; i<5; i++) {
        RECT fr = {bx + i*(bwu+S(10)), S(400), bx + i*(bwu+S(10)) + bwu, S(400+44)};
        bool active = false;
        if (has) {
            if (i==0) active = (GetWindowLongPtrW(lockedTargetLocal, GWL_EXSTYLE) & WS_EX_TOPMOST);
            else if (i==1) active = (GetWindowLongPtrW(lockedTargetLocal, GWL_EXSTYLE) & WS_EX_TRANSPARENT);
            else if (i==2) active = g_cfg.maskEnabled;
        }
        bool hov = (i == 4 || has) && *hovs[i];

        COLORREF bg = active ? g_theme.accent : (hov ? btnHover : btnBg);
        COLORREF text = active ? RGB(255,255,255) : (hov ? g_theme.accent : btnText);
        if (i == 4) { bg = hov ? btnHover : RGB(38,38,42); text = hov ? g_theme.accent : g_theme.text; }

        FillRound(mem, fr, S(8), bg); StrokeRound(mem, fr, S(8), active ? g_theme.accent : RGB(60,60,65), 1);
        DrawTextC(mem, fb[i], fr, g_fNormal, text, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    BitBlt(hdc, 0, 0, S(WIN_W), S(WIN_H), mem, 0, 0, SRCCOPY); DeleteObject(bmp); DeleteDC(mem); EndPaint(hwnd, &ps);
}

void RestoreApp(HWND hwnd) {
    EnableAcrylic(hwnd, 0, false); ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    EnableAcrylic(hwnd, 0x99000000, true); RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME); InvalidateRect(hwnd, NULL, TRUE);
}

// ================= 主窗口消息处理 =================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        InitializeCriticalSection(&g_cs);
        InterlockedExchange(&g_bThreadRunning, 1);

        g_fTitle = CreateFontW(S(-18), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        g_fBig   = CreateFontW(S(-90), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        g_fNormal= CreateFontW(S(-14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        g_fSmall = CreateFontW(S(-12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        g_fMenu  = CreateFontW(S(-14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");

        g_hSearchEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, S(26), S(131), S(SIDEBAR_W - 52), S(24), hwnd, (HMENU)1001, NULL, NULL);
        SendMessageW(g_hSearchEdit, WM_SETFONT, (WPARAM)g_fNormal, TRUE);
        SendMessageW(g_hSearchEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"输入进程名...");

        g_hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1)); if (!g_hIcon) g_hIcon = LoadIconW(NULL, IDI_APPLICATION);
        ZeroMemory(&g_nid, sizeof(g_nid));
        g_nid.cbSize = sizeof(g_nid);
        g_nid.hWnd = hwnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON;
        g_nid.hIcon = g_hIcon;
        lstrcpynW(g_nid.szTip, APP_NAME L" " APP_VERSION, 128);
        Shell_NotifyIconW(NIM_ADD, &g_nid);

        LoadConfig();
        g_cfg.autoStart = IsAutoStartEnabled();
        g_lastFadeSec   = g_cfg.lastFadeSec;
        g_lastFadePct   = g_cfg.lastFadePct;

        g_hAnimThread = CreateThread(NULL, 0, AnimThreadProc, NULL, 0, NULL);
        RefreshList();
        return 0;
    }

    case WM_PAINT: PaintMain(hwnd); return 0;
    case WM_ERASEBKGND: return 1;

    case WM_TIMER: {
        if (wParam == TIMER_MASK_DELAY_APPLY) {
            KillTimer(hwnd, TIMER_MASK_DELAY_APPLY);
            if (IsWindow(g_pendingMaskTarget)) {
                // 在底下偷偷修改透明度
                ApplyAlpha(g_pendingMaskTarget, g_pendingMaskAlpha, true);
                EnterCriticalSection(&g_cs);
                g_render_alpha = g_pendingMaskAlpha; 
                g_target_alpha = g_pendingMaskAlpha;
                g_fadeActive = false; 
                g_breathActive = false;
                LeaveCriticalSection(&g_cs);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            // 再等 1 秒撤走面具
            SetTimer(hwnd, TIMER_MASK_RESTORE, 1000, NULL);
            return 0;
        }
        if (wParam == TIMER_MASK_RESTORE) {
            KillTimer(hwnd, TIMER_MASK_RESTORE);
            HWND mask = GetMaskWindow();
            if (IsWindow(mask)) {
                SetWindowPos(mask, g_saved_topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 
                             g_saved_wp.rcNormalPosition.left, g_saved_wp.rcNormalPosition.top, 
                             g_saved_wp.rcNormalPosition.right - g_saved_wp.rcNormalPosition.left, 
                             g_saved_wp.rcNormalPosition.bottom - g_saved_wp.rcNormalPosition.top, 
                             SWP_NOACTIVATE);
                             
                if (g_saved_wp.showCmd == SW_SHOWMAXIMIZED) ShowWindow(mask, SW_SHOWMAXIMIZED);
                else if (g_saved_wp.showCmd == SW_SHOWMINIMIZED) ShowWindow(mask, SW_SHOWMINIMIZED);
                else ShowWindow(mask, SW_SHOWNORMAL);
                
                HWND target = GetRealTarget();
                if (IsWindow(target)) SetForegroundWindow(target);
            }
            return 0;
        }
        break;
    }

    case WM_UI_REFRESH:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam; SetTextColor(hdc, g_theme.text); SetBkColor(hdc, RGB(40, 40, 45));
        static HBRUSH b = NULL; if (!b) b = CreateSolidBrush(RGB(40, 40, 45)); return (LRESULT)b;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == 1001 && HIWORD(wParam) == EN_CHANGE) {
            wchar_t buf[256]; GetWindowTextW(g_hSearchEdit, buf, 256); g_searchText = buf;
        }
        return 0;

    case WM_MENU_SELECTED: {
        int id = (int)wParam;
        if (id == 2002) { ShowFadeDialog(); return 0; }
        if (id == 2003) { ToggleBreath(); return 0; }
        if (id == 1)    { RefreshList(); return 0; }
        if (id >= 1000 && id < 2000) {
            int idx = id - 1000;
            EnterCriticalSection(&g_cs);
            if (idx >= 0 && idx < (int)g_wins.size()) LockTarget(g_wins[idx], g_titles[idx]);
            LeaveCriticalSection(&g_cs);
            SaveConfig();
            return 0;
        }
        return 0;
    }

    case WM_NCHITTEST: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; ScreenToClient(hwnd, &pt);
        if (pt.y < S(80)) return HTCAPTION;
        return HTCLIENT;
    }

    case WM_MOUSEMOVE: {
        if (!g_tracking) { TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&t); g_tracking = true; }
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

        if (g_draggingSlider && HasTarget()) {
            int cx = S(SIDEBAR_W) + S(40); int cw = S(WIN_W) - cx - S(40);
            int v = MIN_ALPHA + (pt.x - cx) * (MAX_ALPHA - MIN_ALPHA) / cw;
            if (v < MIN_ALPHA) v = MIN_ALPHA; if (v > MAX_ALPHA) v = MAX_ALPHA;
            EnterCriticalSection(&g_cs);
            if (v != g_target_alpha) g_target_alpha = v;
            LeaveCriticalSection(&g_cs);
            return 0;
        }

        bool has = HasTarget();
        bool nT = pt.y > S(200) && pt.y < S(200+44) && pt.x > S(20) && pt.x < S(SIDEBAR_W - 20);
        RECT br = {S(20), S(260), S(SIDEBAR_W - 20) - 46, S(260 + 36)};
        int nB = PtInRect(&br, pt) ? 0 : -1;
        RECT prBtn = {S(SIDEBAR_W - 20) - 36, S(260), S(SIDEBAR_W - 20), S(260 + 36)};
        bool nPick = PtInRect(&prBtn, pt) != FALSE;
        
        int bw = (SIDEBAR_W - S(40) - S(20)) / 3;
        RECT selfRc = {S(20), S(WIN_H - 60), S(20) + bw, S(WIN_H - 20)};
        bool nSelf = PtInRect(&selfRc, pt) != FALSE;
        RECT minRc = {S(30) + bw, S(WIN_H - 60), S(30) + bw * 2, S(WIN_H - 20)};
        bool nMin = PtInRect(&minRc, pt) != FALSE;
        RECT clsRc = {S(40) + bw * 2, S(WIN_H - 60), S(40) + bw * 3, S(WIN_H - 20)};
        bool nClose = PtInRect(&clsRc, pt) != FALSE;

        int cx = S(SIDEBAR_W) + S(40); int cw = S(WIN_W) - cx - S(40); 
        int bwu = (cw - S(40)) / 5;
        int pw = (cw - S(30)) / 4;
        int nP = -1;
        if (has) {
            for (int i=0; i<4; i++)
                if (pt.y>S(320) && pt.y<S(364) && pt.x>cx+i*(pw+S(10)) && pt.x<cx+i*(pw+S(10))+pw) nP = i;
        }
        
        bool nTop = has && pt.y>S(400) && pt.y<S(444) && pt.x>cx && pt.x<cx+bwu;
        bool nClk = has && pt.y>S(400) && pt.y<S(444) && pt.x>cx+bwu+S(10) && pt.x<cx+2*bwu+S(10);
        bool nMask= has && pt.y>S(400) && pt.y<S(444) && pt.x>cx+2*(bwu+S(10)) && pt.x<cx+3*bwu+S(10);
        bool nMore= has && pt.y>S(400) && pt.y<S(444) && pt.x>cx+3*(bwu+S(10)) && pt.x<cx+4*bwu+S(10);
        bool nSet = pt.y>S(400) && pt.y<S(444) && pt.x>cx+4*(bwu+S(10)) && pt.x<cx+5*bwu+S(20);

        if (nT!=g_hoverTarget || nB!=g_hoverBtn || nP!=g_hoverPreset || nTop!=g_hoverTopmost ||
            nClk!=g_hoverClick || nMask!=g_hoverMaskBtn || nMore!=g_hoverMore || nSet!=g_hoverSettings || 
            nSelf!=g_hoverSelf || nMin!=g_hoverMin || nClose!=g_hoverClose || nPick!=g_hoverPick) {
            g_hoverTarget=nT; g_hoverBtn=nB; g_hoverPreset=nP; g_hoverTopmost=nTop; g_hoverClick=nClk;
            g_hoverMaskBtn=nMask; g_hoverMore=nMore; g_hoverSettings=nSet; 
            g_hoverSelf=nSelf; g_hoverMin=nMin; g_hoverClose=nClose; g_hoverPick=nPick;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_tracking = false;
        g_hoverTarget=g_hoverTopmost=g_hoverClick=g_hoverMore=g_hoverSettings=g_hoverSelf=g_hoverMin=g_hoverClose=g_hoverPick=g_hoverMaskBtn=false;
        g_hoverBtn = g_hoverPreset = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (g_hoverSelf) {
            g_self_alpha -= 20;
            if (g_self_alpha < 50) g_self_alpha = 255;
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
            SetLayeredWindowAttributes(hwnd, 0, g_self_alpha, LWA_ALPHA);
            return 0;
        }
        if (g_hoverClose) { DestroyWindow(hwnd); return 0; }
        if (g_hoverMin) { ShowWindow(hwnd, SW_HIDE); return 0; }
        if (g_hoverSettings) { ShowSettingsDialog(); return 0; }
        if (g_hoverPick) { StartPicker(); return 0; }

        if (g_hoverTarget) {
            g_menuItems.clear();
            EnterCriticalSection(&g_cs);
            for (size_t i=0; i<g_wins.size(); i++) {
                if (!g_searchText.empty() &&
                    g_titles[i].find(g_searchText) == std::wstring::npos &&
                    GetKey(g_wins[i]).find(g_searchText) == std::wstring::npos) continue;
                MenuItem m;
                m.text = g_titles[i]; m.id = 1000+i; m.checked = (g_wins[i] == g_locked_target); m.disabled = false; m.isSep = false;
                g_menuItems.push_back(m);
            }
            LeaveCriticalSection(&g_cs);
            if (g_menuItems.empty()) {
                MenuItem m; m.text = L"无匹配窗口"; m.id = 0; m.checked = false; m.disabled = true; m.isSep = false;
                g_menuItems.push_back(m);
            }
            MenuItem sep; sep.isSep = true; sep.id = 0; sep.checked = false; sep.disabled = false;
            g_menuItems.push_back(sep);
            MenuItem rf; rf.text = L"刷新列表"; rf.id = 1; rf.checked = false; rf.disabled = false; rf.isSep = false;
            g_menuItems.push_back(rf);

            RECT wr; GetWindowRect(hwnd, &wr);
            ShowMenu(hwnd, wr.left + S(20), wr.top + S(200+44), S(SIDEBAR_W - 40));
            return 0;
        }

        if (g_hoverTopmost && HasTarget()) {
            HWND target = GetRealTarget();
            if (target) {
                LONG_PTR ex = GetWindowLongPtrW(target, GWL_EXSTYLE);
                SetWindowPos(target, (ex&WS_EX_TOPMOST)?HWND_NOTOPMOST:HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE);
                EnterCriticalSection(&g_cs);
                if (!g_locked_key.empty()) g_profiles[g_locked_key].topmost = !(ex&WS_EX_TOPMOST);
                LeaveCriticalSection(&g_cs);
                SaveConfig();
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (g_hoverClick && HasTarget()) {
            HWND target = GetRealTarget();
            if (target) {
                LONG_PTR ex = GetWindowLongPtrW(target, GWL_EXSTYLE);
                if (ex & WS_EX_TRANSPARENT) ex &= ~WS_EX_TRANSPARENT; else ex |= WS_EX_TRANSPARENT;
                SetWindowLongPtrW(target, GWL_EXSTYLE, ex | WS_EX_LAYERED);
                EnterCriticalSection(&g_cs);
                if (!g_locked_key.empty()) g_profiles[g_locked_key].clickThrough = (ex & WS_EX_TRANSPARENT) != 0;
                LeaveCriticalSection(&g_cs);
                SaveConfig();
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        
        if (g_hoverMaskBtn && HasTarget()) {
            if (g_cfg.maskEnabled) {
                g_cfg.maskEnabled = false; // 关闭面具
                
                // 清理可能还在潜伏的定时器
                KillTimer(hwnd, TIMER_MASK_DELAY_APPLY);
                KillTimer(hwnd, TIMER_MASK_RESTORE);

                // 强制把真实窗口变成当前界面显示的透明度
                HWND t = GetRealTarget();
                if (IsWindow(t)) {
                    EnterCriticalSection(&g_cs);
                    int finalA = g_target_alpha;
                    g_render_alpha = finalA;
                    LeaveCriticalSection(&g_cs);
                    ApplyAlpha(t, finalA, true);
                }
                InvalidateRect(hwnd, NULL, FALSE);
            } else {
                ShowMaskDialog();
            }
            SaveConfig();
            return 0;
        }

        if (g_hoverMore && HasTarget()) {
            g_menuItems.clear();
            g_menuItems.push_back({L"自定义渐隐…", 2002, false, false, false});
            bool breathNow;
            EnterCriticalSection(&g_cs); breathNow = g_breathActive; LeaveCriticalSection(&g_cs);
            g_menuItems.push_back({L"开启呼吸灯特效", 2003, breathNow, false, false});
            int cx = S(SIDEBAR_W) + S(40); int cw = S(WIN_W) - cx - S(40); int bwu = (cw - S(40)) / 5;
            RECT wr; GetWindowRect(hwnd, &wr);
            ShowMenu(hwnd, wr.left + cx+3*(bwu+S(10)), wr.top + S(444), S(220));
            return 0;
        }

        if (pt.y > S(250) && pt.y < S(280) && pt.x > S(SIDEBAR_W)+S(20) && pt.x < S(WIN_W)-S(20) && HasTarget()) {
            g_draggingSlider = true;
            SetCapture(hwnd);
            return 0;
        }
        
        if (g_hoverPreset != -1 && HasTarget()) {
            int a[] = {64, 128, 191, 255};
            HWND t = GetRealTarget();
            if (g_cfg.maskEnabled) {
                ExecuteWithMask(t, a[g_hoverPreset], true);
            } else {
                EnterCriticalSection(&g_cs); g_target_alpha = a[g_hoverPreset]; LeaveCriticalSection(&g_cs);
            }
            return 0;
        }
        if (g_hoverBtn == 0) { RefreshList(); return 0; }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_draggingSlider) {
            g_draggingSlider = false;
            ReleaseCapture();
            HWND t = GetRealTarget();
            if (IsWindow(t)) {
                if (g_cfg.maskEnabled) ExecuteWithMask(t, g_target_alpha, true);
                else ApplyAlpha(t, g_target_alpha, true);
            }
            SaveConfig();
            return 0;
        }
        return 0;
    }

    case WM_HOTKEY: {
        if (wParam == HK_MASK) {
            HWND t = GetRealTarget();
            if (!t) t = GetForegroundWindow();
            if (t == g_hwnd || t == GetMaskWindow()) return 0;
            
            int curA = 255;
            std::wstring k = GetKey(t);
            EnterCriticalSection(&g_cs);
            if (g_profiles.count(k)) curA = g_profiles[k].alpha;
            LeaveCriticalSection(&g_cs);
            
            ExecuteWithMask(t, curA, true);
            return 0;
        }

        if (wParam == HK_PICK) { StartPicker(); return 0; }

        if (wParam == HK_BOSS) {
            if (g_bossHide && IsWindow(g_bossHide)) {
                ShowWindow(g_bossHide, SW_SHOW); SetForegroundWindow(g_bossHide); g_bossHide = NULL;
            } else {
                HWND fg = GetForegroundWindow();
                if (fg != hwnd) { g_bossHide = fg; ShowWindow(fg, SW_HIDE); }
            }
            return 0;
        }

        if (wParam == HK_MOYU) {
            g_moyuActive = !g_moyuActive;
            EnterCriticalSection(&g_cs);
            std::vector<HWND> winsCopy = g_wins;
            LeaveCriticalSection(&g_cs);
            for (auto& w : winsCopy)
                if (IsWindowVisible(w)) ApplyAlpha(w, g_moyuActive ? 40 : 255, false);
            return 0;
        }

        if (wParam == HK_RESET) { ResetAllWindows(); SaveConfig(); return 0; }

        HWND fg = GetForegroundWindow();
        if (g_cfg.maskEnabled) fg = GetRealTarget(); // 如果面具开启，强制锁死目标
        
        if (!fg || fg == hwnd) return 0;
        int cur = 255;
        if (GetWindowLongPtrW(fg, GWL_EXSTYLE) & WS_EX_LAYERED) {
            std::wstring k = GetKey(fg);
            EnterCriticalSection(&g_cs);
            if (g_profiles.count(k)) cur = g_profiles[k].alpha;
            LeaveCriticalSection(&g_cs);
        }
        if (wParam == HK_DOWN)          ApplyAlphaSafe(fg, cur - STEP_ALPHA);
        else if (wParam == HK_UP)       ApplyAlphaSafe(fg, cur + STEP_ALPHA);
        else if (wParam >= HK_P1 && wParam <= HK_P4) {
            int a[] = {64, 128, 191, 255};
            ApplyAlphaSafe(fg, a[wParam - HK_P1]);
        }
        return 0;
    }

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK) {
            RestoreApp(hwnd);
        } else if (lParam == WM_RBUTTONUP) {
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, 1, L"显示窗口");
            AppendMenuW(m, MF_STRING, 3, L"重置所有窗口透明度");
            AppendMenuW(m, MF_STRING | (g_cfg.restoreOnExit ? MF_CHECKED : MF_UNCHECKED), 4, L"退出时自动恢复窗口");
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING, 2, L"彻底退出");

            POINT pt; GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(m, TPM_RETURNCMD|TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(m);

            if (cmd == 1) { RestoreApp(hwnd); }
            else if (cmd == 2) { DestroyWindow(hwnd); }
            else if (cmd == 3) { ResetAllWindows(); SaveConfig(); }
            else if (cmd == 4) { g_cfg.restoreOnExit = !g_cfg.restoreOnExit; SaveConfig(); }
        }
        return 0;

    case WM_DESTROY: {
        InterlockedExchange(&g_bThreadRunning, 0);
        if (g_hAnimThread) {
            WaitForSingleObject(g_hAnimThread, 2000);
            CloseHandle(g_hAnimThread);
            g_hAnimThread = NULL;
        }

        Shell_NotifyIconW(NIM_DELETE, &g_nid);

        if (g_cfg.restoreOnExit) {
            EnterCriticalSection(&g_cs);
            std::vector<HWND> wins = g_wins;
            LeaveCriticalSection(&g_cs);
            for (auto w : wins) if (IsWindow(w)) ApplyAlpha(w, 255, false);
        }

        SaveConfig();
        DeleteCriticalSection(&g_cs);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hPrev; (void)lpCmd;
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc.lpszClassName = L"Win3CPro19";
    RegisterClassExW(&wc);

    std::wstring winTitle = std::wstring(APP_NAME) + L" " + APP_VERSION;

    int x = (GetSystemMetrics(SM_CXSCREEN) - S(WIN_W)) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - S(WIN_H)) / 2;
    HWND hwnd = CreateWindowExW(0, L"Win3CPro19", winTitle.c_str(), WS_POPUP | WS_VISIBLE, x, y, S(WIN_W), S(WIN_H), NULL, NULL, hInst, NULL);
    EnableAcrylic(hwnd, 0x99000000);

    typedef HRESULT (WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        PFN_DwmSetWindowAttribute DwmSetWindowAttribute = (PFN_DwmSetWindowAttribute)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (DwmSetWindowAttribute) { int pref = 2; DwmSetWindowAttribute(hwnd, 33, &pref, sizeof(pref)); }
    }

    RegisterHotKey(hwnd, HK_DOWN, MOD_CONTROL|MOD_ALT, VK_LEFT);
    RegisterHotKey(hwnd, HK_UP,   MOD_CONTROL|MOD_ALT, VK_RIGHT);
    RegisterHotKey(hwnd, HK_RESET, MOD_CONTROL|MOD_ALT, '0');
    RegisterHotKey(hwnd, HK_P1, MOD_CONTROL|MOD_SHIFT, '1');
    RegisterHotKey(hwnd, HK_P2, MOD_CONTROL|MOD_SHIFT, '2');
    RegisterHotKey(hwnd, HK_P3, MOD_CONTROL|MOD_SHIFT, '3');
    RegisterHotKey(hwnd, HK_P4, MOD_CONTROL|MOD_SHIFT, '4');
    RegisterHotKey(hwnd, HK_PICK, MOD_CONTROL|MOD_ALT, 'P');
    RegisterHotKey(hwnd, HK_BOSS, MOD_CONTROL|MOD_ALT, 'H');
    RegisterHotKey(hwnd, HK_MOYU, MOD_CONTROL|MOD_ALT, 'M');
    RegisterHotKey(hwnd, HK_MASK, MOD_CONTROL|MOD_ALT, 'B');

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

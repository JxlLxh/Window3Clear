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

#ifndef ECM_FIRST
#define ECM_FIRST 0x1500
#endif
#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER (ECM_FIRST + 1)
#endif

#define WM_TRAYICON         (WM_USER + 1)
#define WM_MENU_SELECTED    (WM_APP + 200)
#define TIMER_CLEANUP       1
#define TIMER_FADE          2

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

#define WIN_W 760
#define WIN_H 520
#define TITLE_H 44
#define TITLE_BTN_W 46

#define CARD_L_X 20
#define CARD_L_Y 60
#define CARD_L_W 350
#define CARD_L_H 440

#define CARD_R_X 390
#define CARD_R_Y 60
#define CARD_R_W 350
#define CARD_R_H 440

#define MENU_ITEM_H 36
#define MENU_SEP_H 9
#define MENU_PAD 8

#define HELP_W 500
#define HELP_H 360

int S(int v) {
    static int dpi = 96;
    if (dpi == 96) {
        HDC hdc = GetDC(NULL);
        dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(NULL, hdc);
        if (dpi == 0) dpi = 96;
    }
    return (int)((float)v * dpi / 96.0f);
}

struct Theme {
    COLORREF bg, bgTitle, card, cardEdge, editBg;
    COLORREF accent, accentHover, text, textDim;
    COLORREF btn, btnHover, btnEdge;
};
// GitHub 暗黑风格
Theme g_theme = {
    RGB(13,17,23), RGB(22,27,34), RGB(22,27,34), RGB(48,54,61), RGB(13,17,23),
    RGB(88,166,255), RGB(120,185,255), RGB(201,209,217), RGB(139,148,158),
    RGB(33,38,45), RGB(48,54,61), RGB(48,54,61)
};

struct WindowState { int alpha; bool topmost; bool clickThrough; };
std::map<std::wstring, WindowState> g_profiles;

HWND g_hwnd = NULL, g_hSearchEdit = NULL;
HFONT g_fTitle = NULL, g_fBig = NULL, g_fNormal = NULL, g_fSmall = NULL, g_fMenu = NULL;
std::vector<HWND> g_wins;
std::vector<std::wstring> g_titles;
int g_selected = -1, g_alpha = 255;
std::wstring g_searchText;

bool g_draggingSlider = false;
int g_hoverBtn = -1, g_pressBtn = -1;
bool g_hoverTarget = false, g_hoverCheck = false, g_checkOn = true;
bool g_hoverClose = false, g_hoverMin = false;
bool g_hoverTopmost = false, g_hoverClick = false, g_hoverMore = false, g_hoverHelp = false;
int g_hoverPreset = -1, g_pressPreset = -1;
bool g_tracking = false;

struct MenuItem { std::wstring text; int id; bool checked; bool disabled; bool isSep; };
std::vector<MenuItem> g_menuItems;
HWND g_hMenu = NULL, g_hMenuOwner = NULL;
int g_menuHover = -1;

bool g_fadeActive = false;
int g_fadeFrom, g_fadeTo, g_fadeSteps, g_fadeTotal;
HWND g_fadeHwnd = NULL;

HICON g_hIcon = NULL;
NOTIFYICONDATAW g_nid;

bool g_helpHoverClose = false;
bool g_helpTracking = false;

typedef BOOL (WINAPI *PFN_QueryFullProcessImageNameW)(HANDLE, DWORD, LPWSTR, PDWORD);

std::wstring GetKey(HWND h) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    wchar_t path[MAX_PATH] = {0}; DWORD sz = MAX_PATH;
    if (p) {
        static PFN_QueryFullProcessImageNameW fn = NULL;
        if (!fn) {
            void* ptr = (void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "QueryFullProcessImageNameW");
            fn = reinterpret_cast<PFN_QueryFullProcessImageNameW>(ptr);
        }
        if (fn) fn(p, 0, path, &sz);
        CloseHandle(p);
    }
    std::wstring name = path;
    size_t pos = name.find_last_of(L"\\");
    if (pos != std::wstring::npos) name = name.substr(pos + 1);
    wchar_t cls[256] = {0}; GetClassNameW(h, cls, 256);
    return name + L"|" + cls;
}

void ApplyAlpha(HWND h, int a, bool save = true) {
    if (!IsWindow(h) || h == g_hwnd) return;
    if (a < MIN_ALPHA) a = MIN_ALPHA;
    if (a > MAX_ALPHA) a = MAX_ALPHA;
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (!(ex & WS_EX_LAYERED)) SetWindowLongPtrW(h, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    SetLayeredWindowAttributes(h, 0, (BYTE)a, LWA_ALPHA);
    if (save) {
        std::wstring k = GetKey(h);
        if (!k.empty()) g_profiles[k].alpha = a;
    }
}

bool HasTarget() { return g_selected >= 0 && g_selected < (int)g_wins.size(); }

static BOOL CALLBACK EnumWindowsProc(HWND h, LPARAM) {
    if (h == g_hwnd || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    int len = GetWindowTextLengthW(h);
    if (len == 0) return TRUE;
    wchar_t buf[256]; GetWindowTextW(h, buf, 256);
    std::wstring t = buf;
    if (t == L"Program Manager" || t == L"Windows 输入体验") return TRUE;
    g_wins.push_back(h); g_titles.push_back(t);
    return TRUE;
}

void RefreshList() {
    g_wins.clear(); g_titles.clear();
    EnumWindows(EnumWindowsProc, 0);
    g_selected = -1; g_alpha = 255;
}

void FillRound(HDC hdc, RECT r, int radius, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c); HPEN pen = CreatePen(PS_NULL, 0, 0);
    SelectObject(hdc, br); SelectObject(hdc, pen);
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius*2, radius*2);
    DeleteObject(br); DeleteObject(pen);
}
void StrokeRound(HDC hdc, RECT r, int radius, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 1, c); HBRUSH br = (HBRUSH)GetStockObject(NULL_BRUSH);
    SelectObject(hdc, pen); SelectObject(hdc, br);
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius*2, radius*2);
    DeleteObject(pen);
}
void DrawTextC(HDC hdc, const wchar_t* t, RECT r, HFONT f, COLORREF c, UINT fmt) {
    SelectObject(hdc, f); SetTextColor(hdc, c); SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, t, -1, &r, fmt);
}
void FillCircle(HDC hdc, int cx, int cy, int r, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c); HPEN pen = CreatePen(PS_NULL, 0, 0);
    SelectObject(hdc, br); SelectObject(hdc, pen);
    Ellipse(hdc, cx-r, cy-r, cx+r+1, cy+r+1);
    DeleteObject(br); DeleteObject(pen);
}

// ================= 界面绘制相关 =================

void PaintMenu(HWND hwnd) {
    PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    SelectObject(mem, bmp);
    
    FillRound(mem, rc, S(6), g_theme.card);
    StrokeRound(mem, rc, S(6), g_theme.cardEdge);
    
    int y = S(MENU_PAD);
    for (size_t i = 0; i < g_menuItems.size(); i++) {
        auto& item = g_menuItems[i];
        int ih = item.isSep ? S(MENU_SEP_H) : S(MENU_ITEM_H);
        if (item.isSep) {
            RECT line = {S(10), y + ih/2, rc.right - S(10), y + ih/2 + 1};
            HBRUSH b = CreateSolidBrush(g_theme.cardEdge);
            FillRect(mem, &line, b); DeleteObject(b);
        } else {
            RECT ir = {S(4), y, rc.right - S(4), y + ih};
            bool hov = ((int)i == g_menuHover) && !item.disabled;
            if (hov) FillRound(mem, ir, S(4), g_theme.btnHover);
            else if (item.checked) FillRound(mem, ir, S(4), RGB(35, 60, 100));
            
            if (item.checked) {
                HPEN pen = CreatePen(PS_SOLID, 2, g_theme.accent);
                SelectObject(mem, pen);
                MoveToEx(mem, S(16), y+ih/2, NULL); LineTo(mem, S(20), y+ih/2+S(4));
                LineTo(mem, S(28), y+ih/2-S(4)); DeleteObject(pen);
            }
            DrawTextC(mem, item.text.c_str(), {S(36), y, rc.right-S(10), y+ih}, g_fMenu, 
                item.disabled ? g_theme.textDim : (hov ? g_theme.accent : g_theme.text), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        y += ih;
    }
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    DeleteObject(bmp); DeleteDC(mem); EndPaint(hwnd, &ps);
}

LRESULT CALLBACK MenuProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
    case WM_PAINT: PaintMenu(hwnd); return 0;
    case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int y = S(MENU_PAD), idx = -1;
        for (size_t i = 0; i < g_menuItems.size(); i++) {
            int ih = g_menuItems[i].isSep ? S(MENU_SEP_H) : S(MENU_ITEM_H);
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
        WNDCLASSEXW wc; ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc); wc.lpfnWndProc = MenuProc; wc.hInstance = GetModuleHandleW(NULL);
        wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH); wc.lpszClassName = L"Win3CMenu";
        RegisterClassExW(&wc); menuReg = true;
    }
    int h = S(MENU_PAD)*2;
    for (auto& m : g_menuItems) h += m.isSep ? S(MENU_SEP_H) : S(MENU_ITEM_H);
    g_hMenuOwner = owner;
    g_hMenu = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"Win3CMenu", L"", WS_POPUP, x, y, w, h, owner, NULL, GetModuleHandleW(NULL), NULL);
    SetWindowRgn(g_hMenu, CreateRoundRectRgn(0, 0, w+1, h+1, S(12), S(12)), TRUE);
    ShowWindow(g_hMenu, SW_SHOWNOACTIVATE); SetForegroundWindow(g_hMenu);
}

void PaintMain(HWND hwnd) {
    PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, S(WIN_W), S(WIN_H));
    SelectObject(mem, bmp);
    
    RECT full = {0, 0, S(WIN_W), S(WIN_H)};
    HBRUSH bgB = CreateSolidBrush(g_theme.bg); FillRect(mem, &full, bgB); DeleteObject(bgB);

    RECT titleBg = {0, 0, S(WIN_W), S(TITLE_H)};
    HBRUSH tbB = CreateSolidBrush(g_theme.bgTitle); FillRect(mem, &titleBg, tbB); DeleteObject(tbB);

    // 画左侧小图标
    if (g_hIcon) {
        DrawIconEx(mem, S(16), S((TITLE_H - 16)/2), g_hIcon, S(16), S(16), 0, NULL, DI_NORMAL);
    }

    // 动态计算文字宽度，绝对不截断
    int textX = S(40);
    DrawTextC(mem, L"Window3Clear", {textX, 0, textX + S(200), S(TITLE_H)}, g_fTitle, g_theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    
    SIZE sz;
    SelectObject(mem, g_fTitle);
    GetTextExtentPoint32W(mem, L"Window3Clear", 11, &sz);
    int verX = textX + sz.cx + S(12); // 加上间距
    DrawTextC(mem, L"V18 Pro", {verX, 0, verX + S(100), S(TITLE_H)}, g_fNormal, g_theme.accent, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 右上角按钮：修正像素导致的不对称
    int bx = S(WIN_W) - S(TITLE_BTN_W);
    RECT btnClose = {bx, 0, S(WIN_W), S(TITLE_H)};
    if (g_hoverClose) {
        HBRUSH cb = CreateSolidBrush(RGB(232, 17, 35)); FillRect(mem, &btnClose, cb); DeleteObject(cb);
    }
    HPEN penC = CreatePen(PS_SOLID, 1, g_hoverClose ? RGB(255,255,255) : g_theme.textDim);
    SelectObject(mem, penC);
    int ccx = bx + S(TITLE_BTN_W)/2, ccy = S(TITLE_H)/2;
    int cr = S(5); // 半径放大一点
    
    // 画 \ 和 /，使用 +1 保证像素居中对称
    MoveToEx(mem, ccx - cr, ccy - cr, NULL); LineTo(mem, ccx + cr + 1, ccy + cr + 1);
    MoveToEx(mem, ccx - cr, ccy + cr, NULL); LineTo(mem, ccx + cr + 1, ccy - cr);
    DeleteObject(penC);

    bx -= S(TITLE_BTN_W);
    RECT btnMin = {bx, 0, bx + S(TITLE_BTN_W), S(TITLE_H)};
    if (g_hoverMin) {
        HBRUSH mb = CreateSolidBrush(g_theme.btnHover); FillRect(mem, &btnMin, mb); DeleteObject(mb);
    }
    HPEN penM = CreatePen(PS_SOLID, 1, g_hoverMin ? g_theme.text : g_theme.textDim);
    SelectObject(mem, penM);
    int mcx = bx + S(TITLE_BTN_W)/2;
    
    // 画 -，+1 保证对称
    MoveToEx(mem, mcx - cr, ccy, NULL); LineTo(mem, mcx + cr + 1, ccy);
    DeleteObject(penM);

    // ======== 下面卡片绘制保持不变 ========
    RECT lcard = {S(CARD_L_X), S(CARD_L_Y), S(CARD_L_X + CARD_L_W), S(CARD_L_Y + CARD_L_H)};
    FillRound(mem, lcard, S(8), g_theme.card); StrokeRound(mem, lcard, S(8), g_theme.cardEdge);
    
    DrawTextC(mem, L"当前目标窗口", {S(CARD_L_X+20), S(CARD_L_Y+20), S(200), S(CARD_L_Y+40)}, g_fSmall, g_theme.textDim, DT_LEFT|DT_SINGLELINE);
    
    RECT tb = {S(CARD_L_X+20), S(CARD_L_Y+45), S(CARD_L_X+20+310), S(CARD_L_Y+45+44)};
    FillRound(mem, tb, S(6), g_hoverTarget ? g_theme.btnHover : g_theme.btn);
    StrokeRound(mem, tb, S(6), g_hoverTarget ? g_theme.accent : g_theme.cardEdge);
    std::wstring tName = HasTarget() ? g_titles[g_selected] : L"点击选择...";
    DrawTextC(mem, tName.c_str(), {tb.left+S(12), tb.top, tb.right-S(30), tb.bottom}, g_fNormal, HasTarget()?g_theme.text:g_theme.textDim, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    POINT pts[3] = {{tb.right-S(20), tb.top+S(19)}, {tb.right-S(10), tb.top+S(19)}, {tb.right-S(15), tb.top+S(25)}};
    HBRUSH pB = CreateSolidBrush(g_theme.textDim); HPEN pP = CreatePen(PS_NULL, 0, 0);
    SelectObject(mem, pB); SelectObject(mem, pP); Polygon(mem, pts, 3); DeleteObject(pB); DeleteObject(pP);

    RECT sb = {S(CARD_L_X+20), S(CARD_L_Y+105), S(CARD_L_X+20+310), S(CARD_L_Y+105+36)};
    FillRound(mem, sb, S(6), g_theme.editBg); StrokeRound(mem, sb, S(6), g_theme.cardEdge);

    const wchar_t* btns[] = {L"刷新", L"恢复", L"全恢复"};
    for(int i=0; i<3; i++) {
        RECT br = {S(CARD_L_X+20 + i*106), S(CARD_L_Y+155), S(CARD_L_X+20 + i*106 + 98), S(CARD_L_Y+155+40)};
        FillRound(mem, br, S(6), g_hoverBtn==i?g_theme.btnHover:g_theme.btn);
        StrokeRound(mem, br, S(6), g_theme.cardEdge);
        DrawTextC(mem, btns[i], br, g_fNormal, g_hoverBtn==i?g_theme.accent:g_theme.text, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    bool isTop = HasTarget() && (GetWindowLongPtrW(g_wins[g_selected], GWL_EXSTYLE) & WS_EX_TOPMOST);
    bool isClick = HasTarget() && (GetWindowLongPtrW(g_wins[g_selected], GWL_EXSTYLE) & WS_EX_TRANSPARENT);
    const wchar_t* fb[] = {L"置顶", L"穿透", L"更多..."};
    bool* hovs[] = {&g_hoverTopmost, &g_hoverClick, &g_hoverMore};
    int fw[] = {98, 98, 98};
    int fx = CARD_L_X + 20;
    for(int i=0; i<3; i++) {
        RECT fr = {S(fx), S(CARD_L_Y+210), S(fx+fw[i]), S(CARD_L_Y+210+40)};
        bool active = (i==0 && isTop) || (i==1 && isClick);
        FillRound(mem, fr, S(6), active?g_theme.accent:(*hovs[i]?g_theme.btnHover:g_theme.btn));
        StrokeRound(mem, fr, S(6), active?g_theme.accent:g_theme.cardEdge);
        DrawTextC(mem, fb[i], fr, g_fNormal, active?RGB(255,255,255):(*hovs[i]?g_theme.text:g_theme.textDim), DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        fx += fw[i] + 8;
    }

    RECT cb = {S(CARD_L_X+20), S(CARD_L_Y+275), S(CARD_L_X+20+20), S(CARD_L_Y+275+20)};
    if (g_checkOn) {
        FillRound(mem, cb, S(4), g_theme.accent);
        HPEN p = CreatePen(PS_SOLID, 2, RGB(255,255,255)); SelectObject(mem, p);
        MoveToEx(mem, cb.left+S(4), cb.top+S(10), NULL); LineTo(mem, cb.left+S(8), cb.top+S(14));
        LineTo(mem, cb.left+S(16), cb.top+S(6)); DeleteObject(p);
    } else {
        StrokeRound(mem, cb, S(4), g_theme.textDim);
    }
    DrawTextC(mem, L"退出时自动恢复", {S(CARD_L_X+50), S(CARD_L_Y+273), S(200), S(CARD_L_Y+295)}, g_fNormal, g_theme.text, DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    RECT hr = {S(CARD_L_X+20), S(CARD_L_Y+330), S(CARD_L_X+20+310), S(CARD_L_Y+330+44)};
    FillRound(mem, hr, S(6), g_hoverHelp?g_theme.btnHover:g_theme.btn);
    StrokeRound(mem, hr, S(6), g_theme.cardEdge);
    DrawTextC(mem, L"查看使用说明", hr, g_fNormal, g_hoverHelp?g_theme.accent:g_theme.text, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    RECT rcard = {S(CARD_R_X), S(CARD_R_Y), S(CARD_R_X + CARD_R_W), S(CARD_R_Y + CARD_R_H)};
    FillRound(mem, rcard, S(8), g_theme.card); StrokeRound(mem, rcard, S(8), g_theme.cardEdge);

    DrawTextC(mem, L"透明度", {S(CARD_R_X+20), S(CARD_R_Y+20), S(700), S(CARD_R_Y+40)}, g_fSmall, g_theme.textDim, DT_LEFT|DT_SINGLELINE);
    
    wchar_t val[16]; wsprintfW(val, L"%d%%", (g_alpha*100+127)/255);
    DrawTextC(mem, val, {S(CARD_R_X+20), S(CARD_R_Y+70), S(CARD_R_X+CARD_R_W-20), S(CARD_R_Y+180)}, g_fBig, HasTarget()?g_theme.accent:g_theme.textDim, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

    int sx = S(CARD_R_X+20), sy = S(CARD_R_Y+220), sw = S(310), sh = S(6);
    RECT tr = {sx, sy, sx+sw, sy+sh};
    FillRound(mem, tr, sh/2, g_theme.cardEdge);
    
    int kx = sx + sw * (g_alpha - MIN_ALPHA) / (MAX_ALPHA - MIN_ALPHA);
    if (g_alpha > MIN_ALPHA) {
        RECT fr = {sx, sy, kx, sy+sh};
        FillRound(mem, fr, sh/2, g_theme.accent);
    }
    FillCircle(mem, kx, sy+sh/2, S(10), g_theme.bg);
    FillCircle(mem, kx, sy+sh/2, S(8), g_theme.accent);

    const wchar_t* pre[] = {L"25%", L"50%", L"75%", L"100%"};
    for(int i=0; i<4; i++) {
        RECT pr = {S(CARD_R_X+20 + i*79), S(CARD_R_Y+270), S(CARD_R_X+20 + i*79 + 71), S(CARD_R_Y+270+44)};
        FillRound(mem, pr, S(6), g_hoverPreset==i?g_theme.btnHover:g_theme.btn);
        StrokeRound(mem, pr, S(6), g_theme.cardEdge);
        DrawTextC(mem, pre[i], pr, g_fNormal, g_hoverPreset==i?g_theme.accent:g_theme.textDim, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    BitBlt(hdc, 0, 0, S(WIN_W), S(WIN_H), mem, 0, 0, SRCCOPY);
    DeleteObject(bmp); DeleteDC(mem); EndPaint(hwnd, &ps);
}

// ================= 说明窗口 =================

LRESULT CALLBACK HelpProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, S(HELP_W), S(HELP_H));
        SelectObject(mem, bmp);
        
        RECT rc = {0, 0, S(HELP_W), S(HELP_H)};
        FillRound(mem, rc, S(8), g_theme.card);
        StrokeRound(mem, rc, S(8), g_theme.cardEdge);
        
        RECT title = {0, 0, S(HELP_W), S(TITLE_H)};
        HBRUSH tb = CreateSolidBrush(g_theme.bgTitle); FillRect(mem, &title, tb); DeleteObject(tb);
        
        RECT btnClose = {S(HELP_W) - S(TITLE_BTN_W), 0, S(HELP_W), S(TITLE_H)};
        if (g_helpHoverClose) {
            HBRUSH cb = CreateSolidBrush(RGB(232, 17, 35)); FillRect(mem, &btnClose, cb); DeleteObject(cb);
        }
        HPEN penC = CreatePen(PS_SOLID, 1, g_helpHoverClose ? RGB(255,255,255) : g_theme.textDim);
        SelectObject(mem, penC);
        int ccx = btnClose.left + S(TITLE_BTN_W)/2, ccy = S(TITLE_H)/2;
        MoveToEx(mem, ccx-S(4), ccy-S(4), NULL); LineTo(mem, ccx+S(4)+1, ccy+S(4)+1);
        MoveToEx(mem, ccx-S(4), ccy+S(4)+1, NULL); LineTo(mem, ccx+S(4)+1, ccy-S(4));
        DeleteObject(penC);

        DrawTextC(mem, L"使用说明", {S(20), 0, S(200), S(TITLE_H)}, g_fTitle, g_theme.text, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        
        DrawTextC(mem, L"【快捷键】", {S(30), S(60), S(470), S(80)}, g_fTitle, g_theme.accent, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        DrawTextC(mem, L"Ctrl + Alt + ← / →  ：调节前台窗口透明度\n"
                       L"Ctrl + Alt + 0      ：恢复前台窗口\n"
                       L"Ctrl + Shift + 1/2/3/4 ：预设 25/50/75/100%", 
                  {S(30), S(90), S(470), S(160)}, g_fNormal, g_theme.text, DT_LEFT|DT_TOP|DT_WORDBREAK);
                  
        DrawTextC(mem, L"【功能说明】", {S(30), S(180), S(470), S(200)}, g_fTitle, g_theme.accent, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        DrawTextC(mem, L"1. “选择目标”下拉菜单可锁定窗口。\n"
                       L"2. “置顶”将目标固定在最前。\n"
                       L"3. “穿透”让窗口不再接收鼠标点击。\n"
                       L"4. 退出时勾选“自动恢复”可还原透明度。", 
                  {S(30), S(210), S(470), S(300)}, g_fNormal, g_theme.text, DT_LEFT|DT_TOP|DT_WORDBREAK);
                  
        DrawTextC(mem, L"配置文件保存在 %APPDATA%\\Window3Clear\\config.ini", {S(30), S(320), S(470), S(340)}, g_fSmall, g_theme.textDim, DT_LEFT|DT_TOP|DT_SINGLELINE);

        BitBlt(hdc, 0, 0, S(HELP_W), S(HELP_H), mem, 0, 0, SRCCOPY);
        DeleteObject(bmp); DeleteDC(mem); EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!g_helpTracking) { TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&t); g_helpTracking = true; }
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        bool nClose = pt.x > S(HELP_W - TITLE_BTN_W) && pt.y < S(TITLE_H);
        if (nClose != g_helpHoverClose) { g_helpHoverClose = nClose; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE: g_helpTracking = false; g_helpHoverClose = false; InvalidateRect(hwnd, NULL, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (pt.x > S(HELP_W - TITLE_BTN_W) && pt.y < S(TITLE_H)) {
            DestroyWindow(hwnd); return 0;
        }
        if (pt.y < S(TITLE_H)) {
            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowHelpWindow() {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc; ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc); wc.lpfnWndProc = HelpProc; wc.hInstance = GetModuleHandleW(NULL);
        wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH); wc.lpszClassName = L"Win3CHelp";
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        RegisterClassExW(&wc); reg = true;
    }
    int x = (GetSystemMetrics(SM_CXSCREEN) - S(HELP_W)) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - S(HELP_H)) / 2;
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"Win3CHelp", L"使用说明", WS_POPUP, x, y, S(HELP_W), S(HELP_H), g_hwnd, NULL, GetModuleHandleW(NULL), NULL);
    SetWindowRgn(h, CreateRoundRectRgn(0, 0, S(HELP_W)+1, S(HELP_H)+1, S(16), S(16)), TRUE);
    ShowWindow(h, SW_SHOW);
}

// ================= 主窗口 =================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        g_fTitle = CreateFontW(S(-16), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        g_fBig   = CreateFontW(S(-80), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        g_fNormal= CreateFontW(S(-14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        g_fSmall = CreateFontW(S(-12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        g_fMenu  = CreateFontW(S(-14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");

        g_hSearchEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            S(CARD_L_X+24), S(CARD_L_Y+110), S(302), S(26), hwnd, (HMENU)1001, NULL, NULL);
        SendMessageW(g_hSearchEdit, WM_SETFONT, (WPARAM)g_fNormal, TRUE);
        SendMessageW(g_hSearchEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"搜索进程...");

        g_hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
        if (!g_hIcon) g_hIcon = LoadIconW(NULL, IDI_APPLICATION);
        
        ZeroMemory(&g_nid, sizeof(g_nid));
        g_nid.cbSize = sizeof(g_nid); g_nid.hWnd = hwnd; g_nid.uID = 1;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON; g_nid.hIcon = g_hIcon;
        lstrcpyW(g_nid.szTip, L"Window3Clear V18 Pro");
        Shell_NotifyIconW(NIM_ADD, &g_nid);

        RefreshList();
        SetTimer(hwnd, TIMER_CLEANUP, 1000, NULL);
        return 0;
    }

    case WM_PAINT: PaintMain(hwnd); return 0;
    case WM_ERASEBKGND: return 1;

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, g_theme.text);
        SetBkColor(hdc, g_theme.editBg);
        static HBRUSH b = NULL;
        if (!b) b = CreateSolidBrush(g_theme.editBg);
        return (LRESULT)b;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == 1001 && HIWORD(wParam) == EN_CHANGE) {
            wchar_t buf[256]; GetWindowTextW(g_hSearchEdit, buf, 256);
            g_searchText = buf;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_MENU_SELECTED: {
        int id = (int)wParam;
        if (id >= 1000) {
            g_selected = id - 1000;
            std::wstring k = GetKey(g_wins[g_selected]);
            if (g_profiles.count(k)) g_alpha = g_profiles[k].alpha;
            else g_alpha = 255;
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (id == 1) {
            RefreshList(); InvalidateRect(hwnd, NULL, FALSE);
        } else if (id == 2001) {
            std::wstring k = GetKey(g_wins[g_selected]);
            std::wstring proc = k.substr(0, k.find(L"|"));
            for (auto& w : g_wins) if (GetKey(w).find(proc) == 0) ApplyAlpha(w, g_alpha, false);
        } else if (id == 2002) {
            g_fadeHwnd = g_wins[g_selected];
            g_fadeFrom = g_alpha; g_fadeTo = 128; g_fadeSteps = 0; g_fadeTotal = 50;
            g_fadeActive = true; SetTimer(hwnd, TIMER_FADE, 100, NULL);
        }
        return 0;
    }

    case WM_TIMER:
        if (wParam == TIMER_CLEANUP) {
            if (HasTarget() && !IsWindow(g_wins[g_selected])) { g_selected = -1; InvalidateRect(hwnd, NULL, FALSE); }
        } else if (wParam == TIMER_FADE) {
            if (!g_fadeActive) return 0;
            g_fadeSteps++;
            g_alpha = g_fadeFrom + (g_fadeTo - g_fadeFrom) * g_fadeSteps / g_fadeTotal;
            ApplyAlpha(g_fadeHwnd, g_alpha, false);
            InvalidateRect(hwnd, NULL, FALSE);
            if (g_fadeSteps >= g_fadeTotal) { g_fadeActive = false; KillTimer(hwnd, TIMER_FADE); }
        }
        return 0;

    case WM_NCHITTEST: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);
        if (pt.y < S(TITLE_H)) {
            if (pt.x > S(WIN_W - TITLE_BTN_W*2)) return HTCLIENT;
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_MOUSEMOVE: {
        if (!g_tracking) { TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&t); g_tracking = true; }
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        
        if (g_draggingSlider && HasTarget()) {
            int sx = S(CARD_R_X+20), sw = S(310);
            int v = MIN_ALPHA + (pt.x - sx) * (MAX_ALPHA - MIN_ALPHA) / sw;
            if (v < MIN_ALPHA) v = MIN_ALPHA;
            if (v > MAX_ALPHA) v = MAX_ALPHA;
            if (v != g_alpha) { g_alpha = v; ApplyAlpha(g_wins[g_selected], v); InvalidateRect(hwnd, NULL, FALSE); }
            return 0;
        }
        
        bool nClose = pt.x > S(WIN_W - TITLE_BTN_W) && pt.y < S(TITLE_H);
        bool nMin = pt.x > S(WIN_W - TITLE_BTN_W*2) && pt.x < S(WIN_W - TITLE_BTN_W) && pt.y < S(TITLE_H);
        
        bool nT = pt.y > S(CARD_L_Y+45) && pt.y < S(CARD_L_Y+45+44) && pt.x > S(CARD_L_X+20) && pt.x < S(CARD_L_X+330);
        int nB = -1;
        for(int i=0; i<3; i++) if (pt.y>S(CARD_L_Y+155) && pt.y<S(CARD_L_Y+155+40) && pt.x>S(CARD_L_X+20+i*106) && pt.x<S(CARD_L_X+20+i*106+98)) nB = i;
        bool nTop = pt.y>S(CARD_L_Y+210) && pt.y<S(CARD_L_Y+210+40) && pt.x>S(CARD_L_X+20) && pt.x<S(CARD_L_X+20+98);
        bool nClk = pt.y>S(CARD_L_Y+210) && pt.y<S(CARD_L_Y+210+40) && pt.x>S(CARD_L_X+20+106) && pt.x<S(CARD_L_X+20+106+98);
        bool nMore= pt.y>S(CARD_L_Y+210) && pt.y<S(CARD_L_Y+210+40) && pt.x>S(CARD_L_X+20+212) && pt.x<S(CARD_L_X+20+330);
        bool nH   = pt.y>S(CARD_L_Y+330) && pt.y<S(CARD_L_Y+330+44) && pt.x>S(CARD_L_X+20) && pt.x<S(CARD_L_X+330);
        int nP = -1;
        for(int i=0; i<4; i++) if (pt.y>S(CARD_R_Y+270) && pt.y<S(CARD_R_Y+270+44) && pt.x>S(CARD_R_X+20+i*79) && pt.x<S(CARD_R_X+20+i*79+71)) nP = i;
        
        if (nClose!=g_hoverClose || nMin!=g_hoverMin || nT!=g_hoverTarget || nB!=g_hoverBtn || nP!=g_hoverPreset || nTop!=g_hoverTopmost || nClk!=g_hoverClick || nMore!=g_hoverMore || nH!=g_hoverHelp) {
            g_hoverClose=nClose; g_hoverMin=nMin; g_hoverTarget=nT; g_hoverBtn=nB; g_hoverPreset=nP; g_hoverTopmost=nTop; g_hoverClick=nClk; g_hoverMore=nMore; g_hoverHelp=nH;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_tracking = false; g_hoverTarget=g_hoverTopmost=g_hoverClick=g_hoverMore=g_hoverClose=g_hoverMin=g_hoverHelp=false;
        g_hoverBtn = g_hoverPreset = -1;
        InvalidateRect(hwnd, NULL, FALSE); return 0;

    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (g_hoverClose) { ShowWindow(hwnd, SW_HIDE); return 0; }
        if (g_hoverMin) { ShowWindow(hwnd, SW_MINIMIZE); return 0; }
        
        if (g_hoverHelp) { ShowHelpWindow(); return 0; }
        
        if (g_hoverTarget) {
            g_menuItems.clear();
            for(size_t i=0; i<g_wins.size(); i++) {
                if (!g_searchText.empty() && g_titles[i].find(g_searchText) == std::wstring::npos && GetKey(g_wins[i]).find(g_searchText) == std::wstring::npos) continue;
                MenuItem m; m.text = g_titles[i]; m.id = 1000+i; m.checked = ((int)i == g_selected); m.disabled = false; m.isSep = false;
                g_menuItems.push_back(m);
            }
            if (g_menuItems.empty()) { MenuItem m; m.text = L"无匹配窗口"; m.id = 0; m.checked = false; m.disabled = true; m.isSep = false; g_menuItems.push_back(m); }
            MenuItem sep; sep.isSep = true; g_menuItems.push_back(sep);
            MenuItem rf; rf.text = L"刷新列表"; rf.id = 1; rf.checked = false; rf.disabled = false; rf.isSep = false; g_menuItems.push_back(rf);
            
            RECT wr; GetWindowRect(hwnd, &wr);
            ShowMenu(hwnd, wr.left + S(CARD_L_X+20), wr.top + S(CARD_L_Y+45+44), S(310));
            return 0;
        }
        
        if (g_hoverBtn != -1) { g_pressBtn = g_hoverBtn; InvalidateRect(hwnd, NULL, FALSE); return 0; }
        
        if (g_hoverTopmost && HasTarget()) {
            LONG_PTR ex = GetWindowLongPtrW(g_wins[g_selected], GWL_EXSTYLE);
            SetWindowPos(g_wins[g_selected], (ex&WS_EX_TOPMOST)?HWND_NOTOPMOST:HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE);
            g_profiles[GetKey(g_wins[g_selected])].topmost = !(ex&WS_EX_TOPMOST);
            InvalidateRect(hwnd, NULL, FALSE); return 0;
        }
        if (g_hoverClick && HasTarget()) {
            LONG_PTR ex = GetWindowLongPtrW(g_wins[g_selected], GWL_EXSTYLE);
            if (ex & WS_EX_TRANSPARENT) ex &= ~WS_EX_TRANSPARENT;
            else ex |= WS_EX_TRANSPARENT;
            SetWindowLongPtrW(g_wins[g_selected], GWL_EXSTYLE, ex | WS_EX_LAYERED);
            g_profiles[GetKey(g_wins[g_selected])].clickThrough = (ex & WS_EX_TRANSPARENT) != 0;
            InvalidateRect(hwnd, NULL, FALSE); return 0;
        }
        if (g_hoverMore && HasTarget()) {
            g_menuItems.clear();
            MenuItem m1; m1.text = L"应用到同进程所有窗口"; m1.id = 2001; m1.checked = false; m1.disabled = false; m1.isSep = false; g_menuItems.push_back(m1);
            MenuItem m2; m2.text = L"5秒渐隐至50%"; m2.id = 2002; m2.checked = false; m2.disabled = false; m2.isSep = false; g_menuItems.push_back(m2);
            RECT wr; GetWindowRect(hwnd, &wr);
            ShowMenu(hwnd, wr.left + S(CARD_L_X+20+212), wr.top + S(CARD_L_Y+210+40), S(200));
            return 0;
        }

        if (pt.y > S(CARD_R_Y+210) && pt.y < S(CARD_R_Y+230) && pt.x > S(CARD_R_X+20) && pt.x < S(CARD_R_X+330) && HasTarget()) {
            g_draggingSlider = true; SetCapture(hwnd); return 0;
        }
        if (g_hoverPreset != -1 && HasTarget()) { g_pressPreset = g_hoverPreset; InvalidateRect(hwnd, NULL, FALSE); return 0; }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_draggingSlider) { g_draggingSlider = false; ReleaseCapture(); return 0; }
        if (g_pressBtn != -1) {
            int b = g_pressBtn; g_pressBtn = -1;
            if (b == 0) { RefreshList(); }
            else if (b == 1 && HasTarget()) { g_profiles[GetKey(g_wins[g_selected])].alpha = 255; ApplyAlpha(g_wins[g_selected], 255); g_alpha = 255; }
            else if (b == 2) { for(auto& p : g_profiles) p.second.alpha = 255; g_alpha = 255; RefreshList(); }
            InvalidateRect(hwnd, NULL, FALSE); return 0;
        }
        if (g_pressPreset != -1) {
            int a[] = {64, 128, 191, 255};
            g_alpha = a[g_pressPreset]; ApplyAlpha(g_wins[g_selected], g_alpha);
            g_pressPreset = -1; InvalidateRect(hwnd, NULL, FALSE); return 0;
        }
        return 0;
    }

    case WM_HOTKEY: {
        HWND fg = GetForegroundWindow();
        if (fg == hwnd) return 0;
        int cur = 255;
        if (GetWindowLongPtrW(fg, GWL_EXSTYLE) & WS_EX_LAYERED) {
            std::wstring k = GetKey(fg);
            if (g_profiles.count(k)) cur = g_profiles[k].alpha;
        }
        if (wParam == HK_DOWN) ApplyAlpha(fg, cur - STEP_ALPHA);
        else if (wParam == HK_UP) ApplyAlpha(fg, cur + STEP_ALPHA);
        else if (wParam == HK_RESET) { ApplyAlpha(fg, 255); }
        else if (wParam >= HK_P1 && wParam <= HK_P4) {
            int a[] = {64, 128, 191, 255};
            ApplyAlpha(fg, a[wParam - HK_P1]);
        }
        return 0;
    }

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK) { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); }
        else if (lParam == WM_RBUTTONUP) {
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, 1, L"显示窗口"); AppendMenuW(m, MF_STRING, 2, L"彻底退出");
            POINT pt; GetCursorPos(&pt); SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(m, TPM_RETURNCMD|TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(m);
            if (cmd == 1) { ShowWindow(hwnd, SW_SHOW); }
            else if (cmd == 2) DestroyWindow(hwnd);
        }
        return 0;

    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        for (auto& w : g_wins) ApplyAlpha(w, 255, false);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hPrev; (void)lpCmd;

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc.lpszClassName = L"Win3CPro";
    RegisterClassExW(&wc);

    int x = (GetSystemMetrics(SM_CXSCREEN) - S(WIN_W)) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - S(WIN_H)) / 2;
    
    HWND hwnd = CreateWindowExW(0, L"Win3CPro", L"Window3Clear V18 Pro", WS_POPUP | WS_VISIBLE,
        x, y, S(WIN_W), S(WIN_H), NULL, NULL, hInst, NULL);
    
    SetWindowRgn(hwnd, CreateRoundRectRgn(0, 0, S(WIN_W)+1, S(WIN_H)+1, S(16), S(16)), TRUE);

    RegisterHotKey(hwnd, HK_DOWN, MOD_CONTROL|MOD_ALT, VK_LEFT);
    RegisterHotKey(hwnd, HK_UP, MOD_CONTROL|MOD_ALT, VK_RIGHT);
    RegisterHotKey(hwnd, HK_RESET, MOD_CONTROL|MOD_ALT, '0');
    RegisterHotKey(hwnd, HK_P1, MOD_CONTROL|MOD_SHIFT, '1');
    RegisterHotKey(hwnd, HK_P2, MOD_CONTROL|MOD_SHIFT, '2');
    RegisterHotKey(hwnd, HK_P3, MOD_CONTROL|MOD_SHIFT, '3');
    RegisterHotKey(hwnd, HK_P4, MOD_CONTROL|MOD_SHIFT, '4');

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    return 0;
}

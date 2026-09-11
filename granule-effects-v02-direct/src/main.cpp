#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <cmath>
#include <algorithm>

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"shell32.lib")

static const wchar_t* CLS=L"GranuleEffectsV02Direct";
static HWND g_main=nullptr;
static HWINEVENTHOOK g_start=nullptr,g_end=nullptr,g_loc=nullptr,g_show=nullptr;
static HBRUSH g_bg=nullptr;

struct Settings{
    bool enabled=true;
    bool corners=false;
    bool border=false;
    int strength=45;
    int bounce=58;
    int settle=62;
    int length=35;
}g;

struct State{
    HWND hwnd=nullptr;
    RECT finalRect{},lastRect{};
    bool moving=false,animating=false,haveLast=false;
    double dragVX=0,dragVY=0,vx=0,vy=0,ox=0,oy=0;
    ULONGLONG lastTick=0,startTick=0;
}s;

static bool Eligible(HWND h){
    if(!h||h==g_main||!IsWindow(h)||!IsWindowVisible(h)||IsIconic(h)||IsZoomed(h)) return false;
    if(GetWindow(h,GW_OWNER)) return false;
    LONG_PTR st=GetWindowLongPtrW(h,GWL_STYLE);
    if(!(st&WS_CAPTION)) return false;
    wchar_t c[96]{}; GetClassNameW(h,c,95);
    return wcscmp(c,L"Progman")&&wcscmp(c,L"WorkerW")&&wcscmp(c,L"Shell_TrayWnd")&&wcscmp(c,L"Shell_SecondaryTrayWnd");
}

static void Restore(){
    if(s.animating&&s.hwnd&&IsWindow(s.hwnd))
        SetWindowPos(s.hwnd,nullptr,s.finalRect.left,s.finalRect.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOSENDCHANGING);
}
static void Reset(bool restore=false){ if(restore) Restore(); s={}; }

static void BeginMove(HWND h){
    if(!g.enabled||!Eligible(h)) return;
    Reset(true);
    RECT r{}; if(!GetWindowRect(h,&r)) return;
    s.hwnd=h; s.finalRect=s.lastRect=r; s.haveLast=true; s.moving=true; s.lastTick=GetTickCount64();
}

static void TrackMove(HWND h){
    if(!g.enabled||h!=s.hwnd||!s.moving) return;
    RECT r{}; if(!GetWindowRect(h,&r)) return;
    ULONGLONG now=GetTickCount64();
    if(!s.haveLast){ s.lastRect=r; s.haveLast=true; s.lastTick=now; return; }
    double dt=(double)std::max<ULONGLONG>(1,now-s.lastTick);
    double dx=(double)(r.left-s.lastRect.left),dy=(double)(r.top-s.lastRect.top);
    double ivx=dx*(16.0/dt), ivy=dy*(16.0/dt);
    s.dragVX=s.dragVX*.62+ivx*.38; s.dragVY=s.dragVY*.62+ivy*.38;
    s.dragVX=std::clamp(s.dragVX,-40.0,40.0); s.dragVY=std::clamp(s.dragVY,-40.0,40.0);
    s.lastRect=s.finalRect=r; s.lastTick=now;
}

static void StartWobble(HWND h,bool forced=false){
    if(!g.enabled||!Eligible(h)){ Reset(); return; }
    RECT r{}; if(!GetWindowRect(h,&r)){ Reset(); return; }
    s.hwnd=h; s.finalRect=r; s.moving=false; s.animating=true; s.startTick=GetTickCount64(); s.ox=s.oy=0;
    double p=.18+(g.strength/100.0)*.52;
    double x=s.dragVX*p,y=s.dragVY*p;
    if(forced||(std::abs(x)<1.1&&std::abs(y)<1.1)){ x=9*p; y=-3*p; }
    s.vx=std::clamp(x,-20.0,20.0); s.vy=std::clamp(y,-20.0,20.0);
}

static void EndMove(HWND h){ if(h==s.hwnd&&s.moving){ TrackMove(h); StartWobble(h); } }

static void Tick(){
    if(!g.enabled||!s.animating||!s.hwnd||!IsWindow(s.hwnd)) return;
    ULONGLONG elapsed=GetTickCount64()-s.startTick;
    ULONGLONG maxMs=(ULONGLONG)(120+g.length*5.3);
    double spring=.16+.22*(g.bounce/100.0);
    double damping=.70+.20*(g.settle/100.0);
    s.vx+=(-s.ox)*spring; s.vy+=(-s.oy)*spring; s.vx*=damping; s.vy*=damping; s.ox+=s.vx; s.oy+=s.vy;
    bool done=elapsed>=maxMs||(elapsed>100&&std::abs(s.ox)<.2&&std::abs(s.oy)<.2&&std::abs(s.vx)<.2&&std::abs(s.vy)<.2);
    if(done){ Restore(); Reset(); return; }
    SetWindowPos(s.hwnd,nullptr,s.finalRect.left+(int)std::lround(s.ox),s.finalRect.top+(int)std::lround(s.oy),0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOSENDCHANGING);
}

static void StyleWindow(HWND h){
    if(!Eligible(h)) return;
    DWM_WINDOW_CORNER_PREFERENCE cp=g.corners?DWMWCP_ROUND:DWMWCP_DEFAULT;
    DwmSetWindowAttribute(h,DWMWA_WINDOW_CORNER_PREFERENCE,&cp,sizeof(cp));
    COLORREF color=g.border?RGB(119,0,255):0xFFFFFFFF;
    DwmSetWindowAttribute(h,DWMWA_BORDER_COLOR,&color,sizeof(color));
}
static BOOL CALLBACK StyleEnum(HWND h,LPARAM){ StyleWindow(h); return TRUE; }
static void RefreshStyles(){ EnumWindows(StyleEnum,0); }

static void ToggleTopmost(HWND h){
    if(!h||h==g_main||!IsWindow(h)) return;
    bool top=(GetWindowLongPtrW(h,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
    SetWindowPos(h,top?HWND_NOTOPMOST:HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
}
static void Center(HWND h){
    if(!Eligible(h)) return; RECT r{}; if(!GetWindowRect(h,&r)) return;
    HMONITOR m=MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST); MONITORINFO mi{sizeof(mi)}; if(!GetMonitorInfoW(m,&mi)) return;
    int w=r.right-r.left,hg=r.bottom-r.top;
    int x=mi.rcWork.left+((mi.rcWork.right-mi.rcWork.left)-w)/2;
    int y=mi.rcWork.top+((mi.rcWork.bottom-mi.rcWork.top)-hg)/2;
    SetWindowPos(h,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
}

static void CALLBACK EventProc(HWINEVENTHOOK,DWORD e,HWND h,LONG obj,LONG,DWORD,DWORD){
    if(obj!=OBJID_WINDOW||!h) return;
    if(e==EVENT_SYSTEM_MOVESIZESTART) BeginMove(h);
    else if(e==EVENT_SYSTEM_MOVESIZEEND) EndMove(h);
    else if(e==EVENT_OBJECT_LOCATIONCHANGE) TrackMove(h);
    else if(e==EVENT_OBJECT_SHOW) StyleWindow(h);
}

static HWND Label(HWND p,const wchar_t*t,int x,int y,int w,int h){ return CreateWindowW(L"STATIC",t,WS_CHILD|WS_VISIBLE,x,y,w,h,p,nullptr,nullptr,nullptr); }
static HWND Check(HWND p,const wchar_t*t,int id,int x,int y,int w,bool on){ HWND c=CreateWindowW(L"BUTTON",t,WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,x,y,w,24,p,(HMENU)(INT_PTR)id,nullptr,nullptr); SendMessageW(c,BM_SETCHECK,on?BST_CHECKED:BST_UNCHECKED,0); return c; }
static HWND Slider(HWND p,int id,int x,int y,int w,int v){ HWND c=CreateWindowW(TRACKBAR_CLASSW,L"",WS_CHILD|WS_VISIBLE|TBS_NOTICKS,x,y,w,28,p,(HMENU)(INT_PTR)id,nullptr,nullptr); SendMessageW(c,TBM_SETRANGE,TRUE,MAKELPARAM(0,100)); SendMessageW(c,TBM_SETPOS,TRUE,v); return c; }
static HWND Btn(HWND p,const wchar_t*t,int id,int x,int y,int w){ return CreateWindowW(L"BUTTON",t,WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,x,y,w,30,p,(HMENU)(INT_PTR)id,nullptr,nullptr); }
static void Status(){ HWND h=GetDlgItem(g_main,150); if(h) SetWindowTextW(h,g.enabled?L"ACTIVE - wobble only happens after release":L"PAUSED - windows are untouched"); }
static void SetEnabled(bool v){ g.enabled=v; SendMessageW(GetDlgItem(g_main,101),BM_SETCHECK,v?BST_CHECKED:BST_UNCHECKED,0); if(!v) Reset(true); Status(); }

static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_CREATE:
        Label(h,L"Granule Effects v0.2",20,16,250,24);
        Label(h,L"Release-only wobble + lightweight window tweaks",20,42,420,20);
        Check(h,L"Release wobble",101,20,78,150,true); Label(h,L"Dragging stays normal. Wobble starts when you let go.",180,81,360,20);
        Label(h,L"Strength",20,122,80,20); Slider(h,102,105,114,310,g.strength);
        Label(h,L"Bounce",20,160,80,20); Slider(h,103,105,152,310,g.bounce);
        Label(h,L"Settle",20,198,80,20); Slider(h,104,105,190,310,g.settle);
        Label(h,L"Length",20,236,80,20); Slider(h,105,105,228,310,g.length);
        Btn(h,L"Test wobble",106,430,150,120);
        Label(h,L"Window tweaks",20,284,160,20);
        Check(h,L"Rounded corners",110,20,310,170,false); Check(h,L"Purple accent border",111,205,310,190,false);
        Label(h,L"Quick actions",20,354,160,20);
        Btn(h,L"Toggle topmost",120,20,380,160); Btn(h,L"Center active window",121,190,380,180); Btn(h,L"Pause / Resume",122,380,380,150);
        Label(h,L"Ctrl+Alt+T topmost   Ctrl+Alt+C center   Ctrl+Alt+G pause   Ctrl+Alt+W test",20,426,540,34);
        Label(h,L"ACTIVE - wobble only happens after release",20,474,500,22); SetWindowLongPtrW(GetDlgItem(h,0),GWLP_ID,0);
        { HWND st=Label(h,L"ACTIVE - wobble only happens after release",20,474,500,22); SetWindowLongPtrW(st,GWLP_ID,150); }
        Label(h,L"Closing this window exits completely. No hidden stuck process.",20,510,520,20);
        SetTimer(h,1,8,nullptr); SetTimer(h,2,1500,nullptr); return 0;
    case WM_HSCROLL:{ HWND c=(HWND)l; int id=GetDlgCtrlID(c),v=(int)SendMessageW(c,TBM_GETPOS,0,0); if(id==102)g.strength=v; else if(id==103)g.bounce=v; else if(id==104)g.settle=v; else if(id==105)g.length=v; return 0; }
    case WM_COMMAND:{
        int id=LOWORD(w);
        if(id==101) SetEnabled(SendMessageW(GetDlgItem(h,101),BM_GETCHECK,0,0)==BST_CHECKED);
        else if(id==106){ s.dragVX=18;s.dragVY=-5;StartWobble(h,true); }
        else if(id==110){ g.corners=SendMessageW(GetDlgItem(h,110),BM_GETCHECK,0,0)==BST_CHECKED; RefreshStyles(); }
        else if(id==111){ g.border=SendMessageW(GetDlgItem(h,111),BM_GETCHECK,0,0)==BST_CHECKED; RefreshStyles(); }
        else if(id==120) ToggleTopmost(GetForegroundWindow());
        else if(id==121) Center(GetForegroundWindow());
        else if(id==122) SetEnabled(!g.enabled);
        return 0; }
    case WM_HOTKEY:
        if(w==1) SetEnabled(!g.enabled); else if(w==2) ToggleTopmost(GetForegroundWindow()); else if(w==3) Center(GetForegroundWindow()); else if(w==4){ HWND f=GetForegroundWindow(); s.dragVX=16;s.dragVY=-4;StartWobble(f,true); } return 0;
    case WM_TIMER: if(w==1) Tick(); else if(w==2&&(g.corners||g.border)) RefreshStyles(); return 0;
    case WM_CTLCOLORSTATIC:{ HDC dc=(HDC)w; SetTextColor(dc,RGB(232,232,238)); SetBkColor(dc,RGB(24,24,29)); return (LRESULT)g_bg; }
    case WM_ERASEBKGND:{ RECT r{};GetClientRect(h,&r);FillRect((HDC)w,&r,g_bg);return 1; }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        Reset(true); g.corners=false;g.border=false;RefreshStyles();
        if(g_start)UnhookWinEvent(g_start); if(g_end)UnhookWinEvent(g_end); if(g_loc)UnhookWinEvent(g_loc); if(g_show)UnhookWinEvent(g_show);
        UnregisterHotKey(h,1);UnregisterHotKey(h,2);UnregisterHotKey(h,3);UnregisterHotKey(h,4); KillTimer(h,1);KillTimer(h,2);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,m,w,l);
}

int WINAPI wWinMain(HINSTANCE i,HINSTANCE,PWSTR,int n){
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_BAR_CLASSES};InitCommonControlsEx(&ic); g_bg=CreateSolidBrush(RGB(24,24,29));
    WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=WndProc;wc.hInstance=i;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=g_bg;wc.lpszClassName=CLS;RegisterClassExW(&wc);
    g_main=CreateWindowExW(0,CLS,L"Granule Effects v0.2",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,590,585,nullptr,nullptr,i,nullptr);if(!g_main)return 1;
    DWORD f=WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS;
    g_start=SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART,EVENT_SYSTEM_MOVESIZESTART,nullptr,EventProc,0,0,f);
    g_end=SetWinEventHook(EVENT_SYSTEM_MOVESIZEEND,EVENT_SYSTEM_MOVESIZEEND,nullptr,EventProc,0,0,f);
    g_loc=SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE,nullptr,EventProc,0,0,f);
    g_show=SetWinEventHook(EVENT_OBJECT_SHOW,EVENT_OBJECT_SHOW,nullptr,EventProc,0,0,f);
    RegisterHotKey(g_main,1,MOD_CONTROL|MOD_ALT,'G');RegisterHotKey(g_main,2,MOD_CONTROL|MOD_ALT,'T');RegisterHotKey(g_main,3,MOD_CONTROL|MOD_ALT,'C');RegisterHotKey(g_main,4,MOD_CONTROL|MOD_ALT,'W');
    ShowWindow(g_main,n);UpdateWindow(g_main);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}DeleteObject(g_bg);return (int)msg.wParam;
}

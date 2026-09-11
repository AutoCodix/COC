#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <unordered_map>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

static constexpr UINT WM_APP_REFRESH = WM_APP + 10;
static constexpr UINT TIMER_FRAME = 1;
static constexpr UINT TIMER_STYLE = 2;
static constexpr int HOTKEY_PANIC = 1;
static constexpr int HOTKEY_TOPMOST = 2;
static constexpr int HOTKEY_CENTER = 3;
static constexpr int HOTKEY_TEST = 4;

static const wchar_t* MAIN_CLASS = L"GranuleEffectsV03Main";
static const wchar_t* GHOST_CLASS = L"GranuleEffectsV03Ghost";

static HWND g_main = nullptr;
static HWINEVENTHOOK g_moveStart = nullptr, g_moveEnd = nullptr, g_moveLoc = nullptr;
static HWINEVENTHOOK g_minStart = nullptr, g_minEnd = nullptr, g_show = nullptr;
static HINSTANCE g_inst = nullptr;
static HFONT g_font = nullptr, g_fontSmall = nullptr, g_fontTitle = nullptr, g_fontMono = nullptr;
static int g_page = 0;
static POINT g_dragOrigin{};
static bool g_draggingUI = false;

struct Settings {
    bool enabled = true;
    bool releaseWobble = true;
    bool taskbarMinimize = true;
    bool taskbarRestore = true;
    bool roundedCorners = false;
    bool accentBorder = false;
    bool skipFullscreen = true;
    bool startWithWindows = false;
    int wobbleStrength = 45;
    int wobbleBounce = 58;
    int wobbleSettle = 64;
    int wobbleLength = 34;
    int taskbarSpeed = 52;
    int taskbarCurve = 48;
    int accentR = 119, accentG = 0, accentB = 255;
} g;

struct WobbleState {
    HWND hwnd = nullptr;
    RECT finalRect{}, lastRect{};
    bool moving = false, active = false, haveLast = false;
    double dragVX = 0, dragVY = 0, vx = 0, vy = 0, ox = 0, oy = 0;
    ULONGLONG lastTick = 0, startTick = 0;
} wob;

struct GhostAnim {
    HWND overlay = nullptr;
    HWND target = nullptr;
    HBITMAP bitmap = nullptr;
    RECT from{}, to{};
    ULONGLONG start = 0;
    DWORD duration = 280;
    bool restoring = false;
    bool active = false;
};
static GhostAnim ghost;
static HBITMAP g_lastMinBitmap = nullptr;
static HWND g_lastMinHwnd = nullptr;
static RECT g_lastMinRect{};

static COLORREF RGBc(int r,int gg,int b){ return RGB(r,gg,b); }
static RECT Rc(int l,int t,int r,int b){ return RECT{l,t,r,b}; }
static bool PtIn(const RECT&r,POINT p){ return PtInRect(&r,p)!=FALSE; }

static RECT NavRect(int i){ return Rc(18,86+i*58,190,132+i*58); }
static RECT ToggleRect(int x,int y){ return Rc(x,y,x+46,y+24); }
static RECT SliderRect(int x,int y,int w){ return Rc(x,y,x+w,y+18); }
static RECT CloseRect(){ return Rc(854,16,882,44); }
static RECT MinRect(){ return Rc(818,16,846,44); }

static bool IsShellClass(const wchar_t* cls){
    return wcscmp(cls,L"Progman")==0||wcscmp(cls,L"WorkerW")==0||wcscmp(cls,L"Shell_TrayWnd")==0||wcscmp(cls,L"Shell_SecondaryTrayWnd")==0;
}
static bool IsFullscreen(HWND h){
    RECT r{}; if(!GetWindowRect(h,&r)) return false;
    HMONITOR m=MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST); MONITORINFO mi{sizeof(mi)}; if(!GetMonitorInfoW(m,&mi)) return false;
    return r.left<=mi.rcMonitor.left && r.top<=mi.rcMonitor.top && r.right>=mi.rcMonitor.right && r.bottom>=mi.rcMonitor.bottom;
}
static bool Eligible(HWND h, bool allowIconic=false){
    if(!h||h==g_main||h==ghost.overlay||!IsWindow(h)) return false;
    if(!allowIconic && (!IsWindowVisible(h)||IsIconic(h))) return false;
    if(GetWindow(h,GW_OWNER)) return false;
    LONG_PTR st=GetWindowLongPtrW(h,GWL_STYLE); if(!(st&WS_CAPTION)) return false;
    wchar_t cls[128]{}; GetClassNameW(h,cls,127); if(IsShellClass(cls)) return false;
    if(g.skipFullscreen && IsFullscreen(h)) return false;
    return true;
}

static std::wstring ConfigPath(){
    wchar_t path[MAX_PATH]{}; SHGetFolderPathW(nullptr,CSIDL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,path);
    std::wstring dir=std::wstring(path)+L"\\Granule"; CreateDirectoryW(dir.c_str(),nullptr); return dir+L"\\effects.ini";
}
static void SaveSettings(){
    std::wstring p=ConfigPath();
    auto W=[&](const wchar_t*k,int v){ wchar_t b[32]; wsprintfW(b,L"%d",v); WritePrivateProfileStringW(L"effects",k,b,p.c_str()); };
    W(L"enabled",g.enabled); W(L"releaseWobble",g.releaseWobble); W(L"taskbarMinimize",g.taskbarMinimize); W(L"taskbarRestore",g.taskbarRestore);
    W(L"roundedCorners",g.roundedCorners); W(L"accentBorder",g.accentBorder); W(L"skipFullscreen",g.skipFullscreen); W(L"startWithWindows",g.startWithWindows);
    W(L"wobbleStrength",g.wobbleStrength); W(L"wobbleBounce",g.wobbleBounce); W(L"wobbleSettle",g.wobbleSettle); W(L"wobbleLength",g.wobbleLength);
    W(L"taskbarSpeed",g.taskbarSpeed); W(L"taskbarCurve",g.taskbarCurve);
}
static void LoadSettings(){
    std::wstring p=ConfigPath(); auto R=[&](const wchar_t*k,int d){return GetPrivateProfileIntW(L"effects",k,d,p.c_str());};
    g.enabled=R(L"enabled",1)!=0; g.releaseWobble=R(L"releaseWobble",1)!=0; g.taskbarMinimize=R(L"taskbarMinimize",1)!=0; g.taskbarRestore=R(L"taskbarRestore",1)!=0;
    g.roundedCorners=R(L"roundedCorners",0)!=0; g.accentBorder=R(L"accentBorder",0)!=0; g.skipFullscreen=R(L"skipFullscreen",1)!=0; g.startWithWindows=R(L"startWithWindows",0)!=0;
    g.wobbleStrength=R(L"wobbleStrength",45); g.wobbleBounce=R(L"wobbleBounce",58); g.wobbleSettle=R(L"wobbleSettle",64); g.wobbleLength=R(L"wobbleLength",34);
    g.taskbarSpeed=R(L"taskbarSpeed",52); g.taskbarCurve=R(L"taskbarCurve",48);
}
static void SetStartup(bool on){
    HKEY k{}; if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)!=ERROR_SUCCESS) return;
    if(on){ wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr,exe,MAX_PATH); std::wstring q=L"\""+std::wstring(exe)+L"\""; RegSetValueExW(k,L"Granule Effects",0,REG_SZ,(BYTE*)q.c_str(),(DWORD)((q.size()+1)*sizeof(wchar_t))); }
    else RegDeleteValueW(k,L"Granule Effects");
    RegCloseKey(k);
}

static void RestoreWobble(){ if(wob.active&&wob.hwnd&&IsWindow(wob.hwnd)) SetWindowPos(wob.hwnd,nullptr,wob.finalRect.left,wob.finalRect.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOSENDCHANGING); }
static void ResetWobble(bool restore=false){ if(restore)RestoreWobble(); wob={}; }
static void BeginMove(HWND h){
    if(!g.enabled||!g.releaseWobble||!Eligible(h)) return; ResetWobble(true); RECT r{}; if(!GetWindowRect(h,&r))return;
    wob.hwnd=h; wob.finalRect=wob.lastRect=r; wob.haveLast=true; wob.moving=true; wob.lastTick=GetTickCount64();
}
static void TrackMove(HWND h){
    if(!g.enabled||h!=wob.hwnd||!wob.moving) return; RECT r{}; if(!GetWindowRect(h,&r))return; ULONGLONG now=GetTickCount64();
    double dt=(double)std::max<ULONGLONG>(1,now-wob.lastTick); double dx=(double)(r.left-wob.lastRect.left),dy=(double)(r.top-wob.lastRect.top);
    wob.dragVX=wob.dragVX*.62+(dx*(16.0/dt))*.38; wob.dragVY=wob.dragVY*.62+(dy*(16.0/dt))*.38;
    wob.dragVX=std::clamp(wob.dragVX,-40.0,40.0); wob.dragVY=std::clamp(wob.dragVY,-40.0,40.0); wob.lastRect=wob.finalRect=r; wob.lastTick=now;
}
static void StartWobble(HWND h,bool forced=false){
    if(!g.enabled||!g.releaseWobble||!Eligible(h)){ResetWobble();return;} RECT r{}; if(!GetWindowRect(h,&r)){ResetWobble();return;}
    wob.hwnd=h; wob.finalRect=r; wob.moving=false; wob.active=true; wob.startTick=GetTickCount64(); wob.ox=wob.oy=0;
    double p=.18+(g.wobbleStrength/100.0)*.52; double x=wob.dragVX*p,y=wob.dragVY*p; if(forced||(std::abs(x)<1.1&&std::abs(y)<1.1)){x=9*p;y=-3*p;}
    wob.vx=std::clamp(x,-20.0,20.0); wob.vy=std::clamp(y,-20.0,20.0);
}
static void EndMove(HWND h){ if(h==wob.hwnd&&wob.moving){TrackMove(h);StartWobble(h);} }
static void TickWobble(){
    if(!g.enabled||!wob.active||!wob.hwnd||!IsWindow(wob.hwnd))return; ULONGLONG e=GetTickCount64()-wob.startTick;
    ULONGLONG maxMs=(ULONGLONG)(120+g.wobbleLength*5.3); double spring=.16+.22*(g.wobbleBounce/100.0); double damping=.70+.20*(g.wobbleSettle/100.0);
    wob.vx+=(-wob.ox)*spring; wob.vy+=(-wob.oy)*spring; wob.vx*=damping; wob.vy*=damping; wob.ox+=wob.vx; wob.oy+=wob.vy;
    bool done=e>=maxMs||(e>100&&std::abs(wob.ox)<.2&&std::abs(wob.oy)<.2&&std::abs(wob.vx)<.2&&std::abs(wob.vy)<.2);
    if(done){RestoreWobble();ResetWobble();return;}
    SetWindowPos(wob.hwnd,nullptr,wob.finalRect.left+(int)std::lround(wob.ox),wob.finalRect.top+(int)std::lround(wob.oy),0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOSENDCHANGING);
}

static HBITMAP CaptureWindow(HWND h){
    RECT r{}; if(!GetWindowRect(h,&r))return nullptr; int w=r.right-r.left,hg=r.bottom-r.top; if(w<20||hg<20||w>6000||hg>4000)return nullptr;
    HDC sdc=GetDC(nullptr), mdc=CreateCompatibleDC(sdc); HBITMAP bmp=CreateCompatibleBitmap(sdc,w,hg); HGDIOBJ old=SelectObject(mdc,bmp);
    BOOL ok=PrintWindow(h,mdc,0x00000002); if(!ok) BitBlt(mdc,0,0,w,hg,sdc,r.left,r.top,SRCCOPY|CAPTUREBLT);
    SelectObject(mdc,old); DeleteDC(mdc); ReleaseDC(nullptr,sdc); return bmp;
}
static RECT TaskbarTarget(HWND h,const RECT& wr){
    HMONITOR mon=MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST); MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(mon,&mi);
    int cx=(wr.left+wr.right)/2; int cy=(wr.top+wr.bottom)/2; int size=54;
    int bottomGap=mi.rcMonitor.bottom-mi.rcWork.bottom, topGap=mi.rcWork.top-mi.rcMonitor.top, leftGap=mi.rcWork.left-mi.rcMonitor.left, rightGap=mi.rcMonitor.right-mi.rcWork.right;
    if(bottomGap>=topGap&&bottomGap>=leftGap&&bottomGap>=rightGap) return Rc(cx-size/2,mi.rcMonitor.bottom-std::max(8,bottomGap/2)-size/2,cx+size/2,mi.rcMonitor.bottom-std::max(8,bottomGap/2)+size/2);
    if(topGap>=leftGap&&topGap>=rightGap) return Rc(cx-size/2,mi.rcMonitor.top+std::max(8,topGap/2)-size/2,cx+size/2,mi.rcMonitor.top+std::max(8,topGap/2)+size/2);
    if(leftGap>=rightGap) return Rc(mi.rcMonitor.left+std::max(8,leftGap/2)-size/2,cy-size/2,mi.rcMonitor.left+std::max(8,leftGap/2)+size/2,cy+size/2);
    return Rc(mi.rcMonitor.right-std::max(8,rightGap/2)-size/2,cy-size/2,mi.rcMonitor.right-std::max(8,rightGap/2)+size/2,cy+size/2);
}
static void DestroyGhost(){ if(ghost.overlay&&IsWindow(ghost.overlay))DestroyWindow(ghost.overlay); ghost.overlay=nullptr; if(ghost.bitmap)DeleteObject(ghost.bitmap); ghost.bitmap=nullptr; ghost.active=false; }
static void StartGhost(HWND target,HBITMAP bmp,RECT from,RECT to,bool restoring){
    DestroyGhost(); if(!bmp)return; ghost.bitmap=bmp;ghost.target=target;ghost.from=from;ghost.to=to;ghost.restoring=restoring;ghost.active=true;ghost.start=GetTickCount64();ghost.duration=(DWORD)(430-(g.taskbarSpeed*2.6));
    ghost.overlay=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,GHOST_CLASS,L"",WS_POPUP,from.left,from.top,std::max<int>(1,(int)(from.right-from.left)),std::max<int>(1,(int)(from.bottom-from.top)),nullptr,nullptr,g_inst,nullptr);
    SetLayeredWindowAttributes(ghost.overlay,0,255,LWA_ALPHA); ShowWindow(ghost.overlay,SW_SHOWNOACTIVATE); UpdateWindow(ghost.overlay);
}
static double EaseOutBack(double t){ double s=1.15+(g.taskbarCurve/100.0)*.75; double u=t-1.0; return 1.0+(s+1.0)*u*u*u+s*u*u; }
static double EaseInCubic(double t){ return t*t*t; }
static void TickGhost(){
    if(!ghost.active||!ghost.overlay)return; double t=(double)(GetTickCount64()-ghost.start)/std::max<DWORD>(1,ghost.duration); if(t>=1.0){DestroyGhost();return;} t=std::clamp(t,0.0,1.0);
    double q=ghost.restoring?EaseOutBack(t):EaseInCubic(t); auto L=[&](LONG a,LONG b){return (LONG)std::lround(a+(b-a)*q);}; RECT r{L(ghost.from.left,ghost.to.left),L(ghost.from.top,ghost.to.top),L(ghost.from.right,ghost.to.right),L(ghost.from.bottom,ghost.to.bottom)};
    BYTE alpha=(BYTE)std::clamp<int>((int)(255*(ghost.restoring?(0.15+0.85*t):(1.0-0.72*t))),20,255); SetLayeredWindowAttributes(ghost.overlay,0,alpha,LWA_ALPHA);
    SetWindowPos(ghost.overlay,HWND_TOPMOST,r.left,r.top,std::max<int>(2,(int)(r.right-r.left)),std::max<int>(2,(int)(r.bottom-r.top)),SWP_NOACTIVATE|SWP_SHOWWINDOW); InvalidateRect(ghost.overlay,nullptr,FALSE);
}
static void OnMinimizeStart(HWND h){
    if(!g.enabled||!g.taskbarMinimize||!Eligible(h))return; RECT r{}; if(!GetWindowRect(h,&r))return; HBITMAP b=CaptureWindow(h); if(!b)return;
    if(g_lastMinBitmap)DeleteObject(g_lastMinBitmap); g_lastMinBitmap=CaptureWindow(h); g_lastMinHwnd=h; g_lastMinRect=r; StartGhost(h,b,r,TaskbarTarget(h,r),false);
}
static void OnMinimizeEnd(HWND h){
    if(!g.enabled||!g.taskbarRestore)return; WINDOWPLACEMENT wp{sizeof(wp)}; if(!GetWindowPlacement(h,&wp))return; RECT to=wp.rcNormalPosition;
    HBITMAP b=nullptr; if(h==g_lastMinHwnd&&g_lastMinBitmap){ b=(HBITMAP)CopyImage(g_lastMinBitmap,IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION); }
    if(!b)b=CaptureWindow(h); if(!b)return; RECT from=TaskbarTarget(h,to); StartGhost(h,b,from,to,true);
}

static void ApplyStyle(HWND h){
    if(!Eligible(h))return; DWM_WINDOW_CORNER_PREFERENCE cp=g.roundedCorners?DWMWCP_ROUND:DWMWCP_DEFAULT; DwmSetWindowAttribute(h,DWMWA_WINDOW_CORNER_PREFERENCE,&cp,sizeof(cp));
    COLORREF c=g.accentBorder?RGB(g.accentR,g.accentG,g.accentB):0xFFFFFFFF; DwmSetWindowAttribute(h,DWMWA_BORDER_COLOR,&c,sizeof(c));
}
static BOOL CALLBACK StyleEnum(HWND h,LPARAM){ApplyStyle(h);return TRUE;}
static void RefreshStyles(){EnumWindows(StyleEnum,0);}
static void ResetStyles(){ bool a=g.roundedCorners,b=g.accentBorder;g.roundedCorners=false;g.accentBorder=false;RefreshStyles();g.roundedCorners=a;g.accentBorder=b;}
static void Panic(){g.enabled=false;ResetWobble(true);DestroyGhost();ResetStyles();InvalidateRect(g_main,nullptr,TRUE);SaveSettings();}
static void ToggleTopmost(HWND h){ if(!Eligible(h))return; bool top=(GetWindowLongPtrW(h,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;SetWindowPos(h,top?HWND_NOTOPMOST:HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE); }
static void Center(HWND h){ if(!Eligible(h))return;RECT r{};GetWindowRect(h,&r);HMONITOR m=MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(m,&mi);int w=r.right-r.left,hg=r.bottom-r.top;SetWindowPos(h,nullptr,mi.rcWork.left+((mi.rcWork.right-mi.rcWork.left)-w)/2,mi.rcWork.top+((mi.rcWork.bottom-mi.rcWork.top)-hg)/2,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);}

static void CALLBACK EventProc(HWINEVENTHOOK,DWORD e,HWND h,LONG obj,LONG,DWORD,DWORD){
    if(!h||obj!=OBJID_WINDOW)return; if(e==EVENT_SYSTEM_MOVESIZESTART)BeginMove(h); else if(e==EVENT_SYSTEM_MOVESIZEEND)EndMove(h); else if(e==EVENT_OBJECT_LOCATIONCHANGE)TrackMove(h); else if(e==EVENT_SYSTEM_MINIMIZESTART)OnMinimizeStart(h); else if(e==EVENT_SYSTEM_MINIMIZEEND)OnMinimizeEnd(h); else if(e==EVENT_OBJECT_SHOW)ApplyStyle(h);
}

static void RoundRectFill(HDC dc,RECT r,int radius,COLORREF c){HBRUSH b=CreateSolidBrush(c);HBRUSH old=(HBRUSH)SelectObject(dc,b);HPEN p=CreatePen(PS_NULL,0,c);HPEN op=(HPEN)SelectObject(dc,p);RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);SelectObject(dc,op);SelectObject(dc,old);DeleteObject(p);DeleteObject(b);}
static void Text(HDC dc,const wchar_t* s,int x,int y,int w,int h,COLORREF c,HFONT f,UINT fmt=DT_LEFT|DT_VCENTER|DT_SINGLELINE){SetBkMode(dc,TRANSPARENT);SetTextColor(dc,c);SelectObject(dc,f);RECT r{x,y,x+w,y+h};DrawTextW(dc,s,-1,&r,fmt);}
static void TogglePaint(HDC dc,int x,int y,bool on){RECT r=ToggleRect(x,y);RoundRectFill(dc,r,20,on?RGBc(119,0,255):RGBc(62,62,72));int d=18;int cx=on?r.right-3-d:r.left+3;HBRUSH b=CreateSolidBrush(RGBc(245,245,248));HBRUSH old=(HBRUSH)SelectObject(dc,b);Ellipse(dc,cx,r.top+3,cx+d,r.top+3+d);SelectObject(dc,old);DeleteObject(b);}
static void SliderPaint(HDC dc,int x,int y,int w,int v){RoundRectFill(dc,Rc(x,y+7,x+w,y+11),6,RGBc(51,51,60));int fill=(int)(w*(v/100.0));RoundRectFill(dc,Rc(x,y+7,x+fill,y+11),6,RGBc(119,0,255));HBRUSH b=CreateSolidBrush(RGBc(242,242,247));HBRUSH old=(HBRUSH)SelectObject(dc,b);Ellipse(dc,x+fill-6,y+3,x+fill+6,y+15);SelectObject(dc,old);DeleteObject(b);}
static void Card(HDC dc,RECT r,const wchar_t* title,const wchar_t* sub,bool on,int tag){RoundRectFill(dc,r,18,RGBc(28,28,35));Text(dc,title,r.left+18,r.top+12,r.right-r.left-90,22,RGBc(245,245,248),g_font);Text(dc,sub,r.left+18,r.top+38,r.right-r.left-85,34,RGBc(150,150,162),g_fontSmall,DT_LEFT|DT_WORDBREAK);TogglePaint(dc,r.right-64,r.top+17,on);if(tag>=0){wchar_t b[32];wsprintfW(b,L"%d",tag);Text(dc,b,r.right-58,r.bottom-30,40,18,RGBc(104,104,118),g_fontMono,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);}}
static void DrawLogo(HDC dc){RoundRectFill(dc,Rc(22,18,54,50),10,RGBc(119,0,255));Text(dc,L"G",22,17,32,32,RGBc(255,255,255),g_fontTitle,DT_CENTER|DT_VCENTER|DT_SINGLELINE);Text(dc,L"granule",64,15,150,24,RGBc(245,245,248),g_fontTitle);Text(dc,L"effects / 0.3",65,38,150,18,RGBc(123,123,138),g_fontSmall);}
static void DrawNav(HDC dc){const wchar_t* names[]={L"Motion",L"Appearance",L"Utilities",L"Safety"};const wchar_t* glyph[]={L"~",L"◐",L"+",L"!"};for(int i=0;i<4;i++){RECT r=NavRect(i);if(g_page==i)RoundRectFill(dc,r,14,RGBc(39,34,51));Text(dc,glyph[i],r.left+12,r.top,28,r.bottom-r.top,g_page==i?RGBc(180,125,255):RGBc(119,119,132),g_fontTitle,DT_CENTER|DT_VCENTER|DT_SINGLELINE);Text(dc,names[i],r.left+48,r.top,r.right-r.left-50,r.bottom-r.top,g_page==i?RGBc(246,246,248):RGBc(156,156,168),g_font);}}
static void PaintMotion(HDC dc){
    Text(dc,L"Motion",220,72,200,34,RGBc(248,248,250),g_fontTitle);Text(dc,L"Make Windows feel physical, not robotic.",220,104,450,24,RGBc(139,139,153),g_fontSmall);
    RECT preview=Rc(220,142,860,286);RoundRectFill(dc,preview,22,RGBc(24,24,30));Text(dc,L"LIVE PREVIEW",240,156,140,18,RGBc(111,111,126),g_fontSmall);
    RECT mini=Rc(420,182,660,258);RoundRectFill(dc,mini,14,RGBc(45,43,55));RoundRectFill(dc,Rc(420,182,660,204),14,RGBc(62,56,78));Text(dc,L"preview.app",437,183,120,22,RGBc(222,222,230),g_fontSmall);Text(dc,L"drag → release → settle",438,218,200,20,RGBc(165,165,178),g_fontSmall);
    Card(dc,Rc(220,306,530,388),L"Release wobble",L"Normal 1:1 dragging. Elastic motion begins only after you let go.",g.releaseWobble,1);
    Card(dc,Rc(548,306,860,388),L"Minimize to taskbar",L"A visual copy shrinks and dives toward the taskbar instead of vanishing.",g.taskbarMinimize,2);
    Card(dc,Rc(220,402,530,484),L"Restore from taskbar",L"Bring windows back with a curved Linux-style pop from the taskbar.",g.taskbarRestore,3);
    Text(dc,L"Wobble strength",548,408,150,18,RGBc(215,215,222),g_fontSmall);SliderPaint(dc,548,434,280,g.wobbleStrength);Text(dc,L"Animation speed",548,464,150,18,RGBc(215,215,222),g_fontSmall);SliderPaint(dc,548,490,280,g.taskbarSpeed);
}
static void PaintAppearance(HDC dc){
    Text(dc,L"Appearance",220,72,230,34,RGBc(248,248,250),g_fontTitle);Text(dc,L"Small changes that make every window feel like Granule.",220,104,500,24,RGBc(139,139,153),g_fontSmall);
    Card(dc,Rc(220,150,530,232),L"Rounded corners",L"Ask Windows DWM for consistently rounded top-level windows.",g.roundedCorners,1);
    Card(dc,Rc(548,150,860,232),L"Granule border",L"Use the Granule violet accent around supported windows.",g.accentBorder,2);
    RoundRectFill(dc,Rc(220,254,860,430),20,RGBc(24,24,30));Text(dc,L"ACCENT",242,270,100,18,RGBc(111,111,126),g_fontSmall);RoundRectFill(dc,Rc(244,307,332,395),24,RGBc(119,0,255));Text(dc,L"#7700FF",360,318,160,28,RGBc(245,245,248),g_fontTitle);Text(dc,L"Granule violet",360,350,180,22,RGBc(150,150,162),g_fontSmall);
    Text(dc,L"Border color is intentionally simple in v0.3. Custom color picker comes next.",360,380,422,28,RGBc(129,129,143),g_fontSmall,DT_LEFT|DT_WORDBREAK);
}
static void PaintUtilities(HDC dc){
    Text(dc,L"Utilities",220,72,220,34,RGBc(248,248,250),g_fontTitle);Text(dc,L"Fast controls for the window you are using right now.",220,104,500,24,RGBc(139,139,153),g_fontSmall);
    RoundRectFill(dc,Rc(220,150,860,262),20,RGBc(24,24,30));Text(dc,L"CURRENT WINDOW",242,166,180,18,RGBc(111,111,126),g_fontSmall);
    Text(dc,L"Always on top",244,204,160,22,RGBc(236,236,241),g_font);Text(dc,L"Ctrl + Alt + T",410,204,160,22,RGBc(145,145,160),g_fontMono);
    Text(dc,L"Center on monitor",580,204,170,22,RGBc(236,236,241),g_font);Text(dc,L"Ctrl + Alt + C",735,204,110,22,RGBc(145,145,160),g_fontMono,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    Card(dc,Rc(220,282,530,364),L"Start with Windows",L"Launch Granule Effects automatically after you sign in.",g.startWithWindows,1);
    Card(dc,Rc(548,282,860,364),L"Skip fullscreen",L"Avoid touching fullscreen games and borderless fullscreen windows.",g.skipFullscreen,2);
    RoundRectFill(dc,Rc(220,386,860,470),20,RGBc(31,27,41));Text(dc,L"TEST MOTION",242,404,140,18,RGBc(175,125,245),g_fontSmall);Text(dc,L"Ctrl + Alt + W",242,430,190,26,RGBc(245,245,248),g_fontTitle);Text(dc,L"Triggers a quick release wobble on the active window.",460,429,360,26,RGBc(155,155,168),g_fontSmall);
}
static void PaintSafety(HDC dc){
    Text(dc,L"Safety",220,72,220,34,RGBc(248,248,250),g_fontTitle);Text(dc,L"Nothing should ever leave your desktop stuck again.",220,104,500,24,RGBc(139,139,153),g_fontSmall);
    RoundRectFill(dc,Rc(220,150,860,254),20,RGBc(45,29,36));Text(dc,L"PANIC KEY",242,168,150,18,RGBc(245,132,160),g_fontSmall);Text(dc,L"Ctrl + Alt + G",242,196,240,28,RGBc(250,246,248),g_fontTitle);Text(dc,L"Stops animations, restores moved windows and resets Granule styling.",500,193,330,44,RGBc(188,159,169),g_fontSmall,DT_LEFT|DT_WORDBREAK);
    Card(dc,Rc(220,278,530,360),L"Master switch",L"Disable all motion effects without closing the app.",g.enabled,1);
    RoundRectFill(dc,Rc(548,278,860,360),18,RGBc(24,24,30));Text(dc,L"CLOSE BEHAVIOR",568,294,170,18,RGBc(111,111,126),g_fontSmall);Text(dc,L"X means EXIT",568,320,180,24,RGBc(245,245,248),g_font);Text(dc,L"No tray ghost. No hidden process.",568,342,250,18,RGBc(148,148,161),g_fontSmall);
    RoundRectFill(dc,Rc(220,386,860,470),20,g.enabled?RGBc(28,39,33):RGBc(45,29,36));Text(dc,g.enabled?L"ENGINE ACTIVE":L"ENGINE PAUSED",242,405,180,22,g.enabled?RGBc(133,236,177):RGBc(245,132,160),g_font);Text(dc,g.enabled?L"Granule is watching window events. Idle usage stays tiny.":L"Windows are untouched until you enable the engine again.",242,434,570,20,RGBc(157,157,170),g_fontSmall);
}
static void PaintMain(HWND h,HDC dc){
    RECT c{};GetClientRect(h,&c);HBRUSH b=CreateSolidBrush(RGBc(17,17,22));FillRect(dc,&c,b);DeleteObject(b);
    DrawLogo(dc);Text(dc,g.enabled?L"ACTIVE":L"PAUSED",710,22,90,20,g.enabled?RGBc(126,230,173):RGBc(244,135,160),g_fontSmall,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    RoundRectFill(dc,MinRect(),12,RGBc(28,28,35));Text(dc,L"—",818,15,28,28,RGBc(170,170,182),g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);RoundRectFill(dc,CloseRect(),12,RGBc(45,28,34));Text(dc,L"×",854,15,28,28,RGBc(242,149,169),g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    RoundRectFill(dc,Rc(12,72,198,548),20,RGBc(21,21,27));DrawNav(dc);Text(dc,L"Ctrl Alt G",30,505,120,18,RGBc(105,105,118),g_fontMono);Text(dc,L"panic",30,526,80,16,RGBc(78,78,90),g_fontSmall);
    if(g_page==0)PaintMotion(dc);else if(g_page==1)PaintAppearance(dc);else if(g_page==2)PaintUtilities(dc);else PaintSafety(dc);
    Text(dc,L"Granule Effects 0.3  •  native Win32  •  no background service",220,535,640,18,RGBc(78,78,90),g_fontSmall);
}

static void ToggleSettingByPoint(POINT p){
    if(g_page==0){if(PtIn(Rc(220,306,530,388),p))g.releaseWobble=!g.releaseWobble; else if(PtIn(Rc(548,306,860,388),p))g.taskbarMinimize=!g.taskbarMinimize; else if(PtIn(Rc(220,402,530,484),p))g.taskbarRestore=!g.taskbarRestore;}
    else if(g_page==1){if(PtIn(Rc(220,150,530,232),p))g.roundedCorners=!g.roundedCorners; else if(PtIn(Rc(548,150,860,232),p))g.accentBorder=!g.accentBorder;RefreshStyles();}
    else if(g_page==2){if(PtIn(Rc(220,282,530,364),p)){g.startWithWindows=!g.startWithWindows;SetStartup(g.startWithWindows);} else if(PtIn(Rc(548,282,860,364),p))g.skipFullscreen=!g.skipFullscreen;}
    else if(g_page==3){if(PtIn(Rc(220,278,530,360),p)){g.enabled=!g.enabled;if(!g.enabled){ResetWobble(true);DestroyGhost();}}}
    SaveSettings();InvalidateRect(g_main,nullptr,FALSE);
}
static void SetSliderByPoint(POINT p){ if(g_page!=0)return; if(p.x<548||p.x>828)return; if(p.y>=430&&p.y<=454)g.wobbleStrength=std::clamp<int>((int)((p.x-548)*100/280),0,100); else if(p.y>=486&&p.y<=512)g.taskbarSpeed=std::clamp<int>((int)((p.x-548)*100/280),0,100); else return;SaveSettings();InvalidateRect(g_main,nullptr,FALSE);}

static LRESULT CALLBACK GhostProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_PAINT){PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);RECT c{};GetClientRect(h,&c);if(ghost.bitmap){BITMAP bm{};GetObject(ghost.bitmap,sizeof(bm),&bm);HDC mem=CreateCompatibleDC(dc);HGDIOBJ old=SelectObject(mem,ghost.bitmap);SetStretchBltMode(dc,HALFTONE);StretchBlt(dc,0,0,c.right,c.bottom,mem,0,0,bm.bmWidth,bm.bmHeight,SRCCOPY);SelectObject(mem,old);DeleteDC(mem);}EndPaint(h,&ps);return 0;}
    if(m==WM_NCHITTEST)return HTTRANSPARENT; return DefWindowProcW(h,m,w,l);
}

static LRESULT CALLBACK MainProc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_NCHITTEST:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);if(p.y<62&&!PtIn(CloseRect(),p)&&!PtIn(MinRect(),p))return HTCAPTION;return HTCLIENT;}
    case WM_PAINT:{PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);PaintMain(h,dc);EndPaint(h,&ps);return 0;}
    case WM_LBUTTONDOWN:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};if(PtIn(CloseRect(),p)){SendMessageW(h,WM_CLOSE,0,0);return 0;}if(PtIn(MinRect(),p)){ShowWindow(h,SW_MINIMIZE);return 0;}for(int i=0;i<4;i++)if(PtIn(NavRect(i),p)){g_page=i;InvalidateRect(h,nullptr,FALSE);return 0;}ToggleSettingByPoint(p);SetSliderByPoint(p);SetCapture(h);return 0;}
    case WM_MOUSEMOVE:if(w&MK_LBUTTON){POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};SetSliderByPoint(p);}return 0;
    case WM_LBUTTONUP:ReleaseCapture();return 0;
    case WM_TIMER:if(w==TIMER_FRAME){TickWobble();TickGhost();}else if(w==TIMER_STYLE&&(g.roundedCorners||g.accentBorder))RefreshStyles();return 0;
    case WM_HOTKEY:if(w==HOTKEY_PANIC)Panic();else if(w==HOTKEY_TOPMOST)ToggleTopmost(GetForegroundWindow());else if(w==HOTKEY_CENTER)Center(GetForegroundWindow());else if(w==HOTKEY_TEST){HWND f=GetForegroundWindow();wob.dragVX=18;wob.dragVY=-5;StartWobble(f,true);}return 0;
    case WM_CLOSE:DestroyWindow(h);return 0;
    case WM_DESTROY:SaveSettings();ResetWobble(true);DestroyGhost();ResetStyles();if(g_lastMinBitmap)DeleteObject(g_lastMinBitmap);g_lastMinBitmap=nullptr;
        if(g_moveStart)UnhookWinEvent(g_moveStart);if(g_moveEnd)UnhookWinEvent(g_moveEnd);if(g_moveLoc)UnhookWinEvent(g_moveLoc);if(g_minStart)UnhookWinEvent(g_minStart);if(g_minEnd)UnhookWinEvent(g_minEnd);if(g_show)UnhookWinEvent(g_show);
        UnregisterHotKey(h,HOTKEY_PANIC);UnregisterHotKey(h,HOTKEY_TOPMOST);UnregisterHotKey(h,HOTKEY_CENTER);UnregisterHotKey(h,HOTKEY_TEST);KillTimer(h,TIMER_FRAME);KillTimer(h,TIMER_STYLE);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,m,w,l);
}

int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,PWSTR,int){
    g_inst=inst;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);LoadSettings();
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_STANDARD_CLASSES};InitCommonControlsEx(&ic);
    g_font=CreateFontW(-18,0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    g_fontSmall=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    g_fontTitle=CreateFontW(-23,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI Variable Display");
    g_fontMono=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,L"Cascadia Mono");
    WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=MainProc;wc.hInstance=inst;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.lpszClassName=MAIN_CLASS;wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);RegisterClassExW(&wc);
    WNDCLASSEXW gc{sizeof(gc)};gc.lpfnWndProc=GhostProc;gc.hInstance=inst;gc.hCursor=LoadCursor(nullptr,IDC_ARROW);gc.lpszClassName=GHOST_CLASS;gc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);RegisterClassExW(&gc);
    g_main=CreateWindowExW(WS_EX_APPWINDOW,MAIN_CLASS,L"Granule Effects",WS_POPUP|WS_MINIMIZEBOX,0,0,900,570,nullptr,nullptr,inst,nullptr);if(!g_main)return 1;
    RECT wa{};SystemParametersInfoW(SPI_GETWORKAREA,0,&wa,0);SetWindowPos(g_main,nullptr,wa.left+(wa.right-wa.left-900)/2,wa.top+(wa.bottom-wa.top-570)/2,900,570,SWP_NOZORDER|SWP_NOACTIVATE);
    DWORD f=WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS;
    g_moveStart=SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART,EVENT_SYSTEM_MOVESIZESTART,nullptr,EventProc,0,0,f);g_moveEnd=SetWinEventHook(EVENT_SYSTEM_MOVESIZEEND,EVENT_SYSTEM_MOVESIZEEND,nullptr,EventProc,0,0,f);g_moveLoc=SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE,nullptr,EventProc,0,0,f);
    g_minStart=SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART,EVENT_SYSTEM_MINIMIZESTART,nullptr,EventProc,0,0,f);g_minEnd=SetWinEventHook(EVENT_SYSTEM_MINIMIZEEND,EVENT_SYSTEM_MINIMIZEEND,nullptr,EventProc,0,0,f);g_show=SetWinEventHook(EVENT_OBJECT_SHOW,EVENT_OBJECT_SHOW,nullptr,EventProc,0,0,f);
    RegisterHotKey(g_main,HOTKEY_PANIC,MOD_CONTROL|MOD_ALT,'G');RegisterHotKey(g_main,HOTKEY_TOPMOST,MOD_CONTROL|MOD_ALT,'T');RegisterHotKey(g_main,HOTKEY_CENTER,MOD_CONTROL|MOD_ALT,'C');RegisterHotKey(g_main,HOTKEY_TEST,MOD_CONTROL|MOD_ALT,'W');
    SetTimer(g_main,TIMER_FRAME,8,nullptr);SetTimer(g_main,TIMER_STYLE,1600,nullptr);ShowWindow(g_main,SW_SHOW);UpdateWindow(g_main);RefreshStyles();
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}DeleteObject(g_font);DeleteObject(g_fontSmall);DeleteObject(g_fontTitle);DeleteObject(g_fontMono);return (int)msg.wParam;
}

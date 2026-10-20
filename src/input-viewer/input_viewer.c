/* Minimal Wine input-path viewer: Win32 messages, WM_INPUT, and DirectInput. */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <string.h>

static LPDIRECTINPUT8A di;
static LPDIRECTINPUTDEVICE8A keyboard, mouse;
static BYTE key_state[256];
static DIMOUSESTATE mouse_state;
static char last_raw[160] = "Waiting for DirectInput", last_win[96] = "(none)";
static char di_key_result[32] = "not ready", di_mouse_result[32] = "not ready";
static unsigned raw_count, win_count;
static HWND main_window;

static void stage(const char *name)
{
    HANDLE file=CreateFileA("input-viewer-stage.log",FILE_APPEND_DATA,
                            FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,NULL);
    if(file!=INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(file,name,(DWORD)strlen(name),&written,NULL);
        WriteFile(file,"\r\n",2,&written,NULL);
        FlushFileBuffers(file);
        CloseHandle(file);
    }
}

static char state(unsigned code) { return key_state[code]&0x80?'D':'.'; }

static void event_win(const char *name, WPARAM w, LPARAM l)
{
    snprintf(last_win, sizeof(last_win), "%s w=%08lx l=%08lx", name,
             (unsigned long)w, (unsigned long)l);
    win_count++;
    InvalidateRect(main_window, NULL, FALSE);
}

static void paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc=BeginPaint(hwnd,&ps);
    RECT r; GetClientRect(hwnd,&r);
    HBRUSH bg=CreateSolidBrush(RGB(18,27,37));
    FillRect(dc,&r,bg); DeleteObject(bg);
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(235,242,248));
    HFONT font=CreateFontA(22,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,ANSI_CHARSET,
                           OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
                           FIXED_PITCH|FF_MODERN,"Consolas");
    HFONT old=(HFONT)SelectObject(dc,font);
    const char *lines[20]; char buf[20][192]; int n=0;
    lines[n++]="WINE INPUT PATH VIEWER";
    lines[n++]="DirectInput owns keyboard/mouse input (exclusive).";
    lines[n++]="Legacy WM_KEY / WM_MOUSE should stay quiet, as in Oblivion.";
    snprintf(buf[n],sizeof buf[n],"WM_INPUT count=%u; last: %s",raw_count,last_raw); lines[n]=buf[n]; n++;
    snprintf(buf[n],sizeof buf[n],"Win32 legacy messages: %u",win_count); lines[n]=buf[n]; n++;
    snprintf(buf[n],sizeof buf[n],"Last Win32: %s",last_win); lines[n]=buf[n]; n++;
    snprintf(buf[n],sizeof buf[n],"DirectInput keyboard: %s",di_key_result); lines[n]=buf[n]; n++;
    snprintf(buf[n],sizeof buf[n],"DI keys SP:%c EN:%c ESC:%c TAB:%c F:%c E:%c ALT:%c CTRL:%c",
             state(DIK_SPACE),state(DIK_RETURN),state(DIK_ESCAPE),state(DIK_TAB),
             state(DIK_F),state(DIK_E),state(DIK_LALT),state(DIK_LCONTROL));
    lines[n]=buf[n]; n++;
    snprintf(buf[n],sizeof buf[n],"DI keys Z:%c C:%c R:%c T:%c UP:%c DOWN:%c LEFT:%c RIGHT:%c",
             state(DIK_Z),state(DIK_C),state(DIK_R),state(DIK_T),
             state(DIK_UP),state(DIK_DOWN),state(DIK_LEFT),state(DIK_RIGHT));
    lines[n]=buf[n]; n++;
    snprintf(buf[n],sizeof buf[n],"DirectInput mouse: %s",di_mouse_result); lines[n]=buf[n]; n++;
    snprintf(buf[n],sizeof buf[n],"DI buttons L=%s R=%s  delta=%ld,%ld",
             mouse_state.rgbButtons[0]&0x80?"DOWN":"up",
             mouse_state.rgbButtons[1]&0x80?"DOWN":"up",
             (long)mouse_state.lX,(long)mouse_state.lY); lines[n]=buf[n]; n++;
    lines[n++]="Press Cross / R1 / D-pad / right stick; close from PS menu.";
    for(int i=0;i<n;i++) TextOutA(dc,18,16+i*38,lines[i],(int)strlen(lines[i]));
    SelectObject(dc,old); DeleteObject(font); EndPaint(hwnd,&ps);
}

static void read_directinput(void)
{
    if(keyboard) {
        HRESULT hr=IDirectInputDevice8_GetDeviceState(keyboard,sizeof key_state,key_state);
        if(FAILED(hr)) {
            if(hr==DIERR_INPUTLOST||hr==DIERR_NOTACQUIRED) {
                IDirectInputDevice8_Acquire(keyboard);
                hr=IDirectInputDevice8_GetDeviceState(keyboard,sizeof key_state,key_state);
            }
        }
        snprintf(di_key_result,sizeof di_key_result,"%s 0x%08lx",
                 SUCCEEDED(hr)?"OK":"FAIL",(unsigned long)hr);
    }
    if(mouse) {
        HRESULT hr=IDirectInputDevice8_GetDeviceState(mouse,sizeof mouse_state,&mouse_state);
        if(FAILED(hr)) {
            if(hr==DIERR_INPUTLOST||hr==DIERR_NOTACQUIRED) {
                IDirectInputDevice8_Acquire(mouse);
                hr=IDirectInputDevice8_GetDeviceState(mouse,sizeof mouse_state,&mouse_state);
            }
        }
        snprintf(di_mouse_result,sizeof di_mouse_result,"%s 0x%08lx",
                 SUCCEEDED(hr)?"OK":"FAIL",(unsigned long)hr);
    }
    InvalidateRect(main_window,NULL,FALSE);
}

static void init_directinput(HWND hwnd)
{
    stage("before DirectInput8Create");
    HRESULT hr=DirectInput8Create(GetModuleHandleA(NULL),DIRECTINPUT_VERSION,
                                  &IID_IDirectInput8A,(void**)&di,NULL);
    stage("after DirectInput8Create");
    if(FAILED(hr)) {
        snprintf(di_key_result,sizeof di_key_result,"CREATE FAIL 0x%08lx",(unsigned long)hr);
        snprintf(di_mouse_result,sizeof di_mouse_result,"CREATE FAIL 0x%08lx",(unsigned long)hr);
        return;
    }
    stage("before keyboard CreateDevice");
    hr=IDirectInput8_CreateDevice(di,&GUID_SysKeyboard,&keyboard,NULL);
    stage("after keyboard CreateDevice");
    if(SUCCEEDED(hr)) {
        hr=IDirectInputDevice8_SetDataFormat(keyboard,&c_dfDIKeyboard);
        if(SUCCEEDED(hr))hr=IDirectInputDevice8_SetCooperativeLevel(keyboard,hwnd,
                                         DISCL_FOREGROUND|DISCL_EXCLUSIVE);
        if(SUCCEEDED(hr))hr=IDirectInputDevice8_Acquire(keyboard);
        snprintf(di_key_result,sizeof di_key_result,"INIT %s 0x%08lx",
                 SUCCEEDED(hr)?"OK":"FAIL",(unsigned long)hr);
    } else snprintf(di_key_result,sizeof di_key_result,"CREATE FAIL 0x%08lx",(unsigned long)hr);

    stage("before mouse CreateDevice");
    hr=IDirectInput8_CreateDevice(di,&GUID_SysMouse,&mouse,NULL);
    stage("after mouse CreateDevice");
    if(SUCCEEDED(hr)) {
        hr=IDirectInputDevice8_SetDataFormat(mouse,&c_dfDIMouse);
        if(SUCCEEDED(hr))hr=IDirectInputDevice8_SetCooperativeLevel(mouse,hwnd,
                                         DISCL_FOREGROUND|DISCL_EXCLUSIVE);
        if(SUCCEEDED(hr))hr=IDirectInputDevice8_Acquire(mouse);
        snprintf(di_mouse_result,sizeof di_mouse_result,"INIT %s 0x%08lx",
                 SUCCEEDED(hr)?"OK":"FAIL",(unsigned long)hr);
    } else snprintf(di_mouse_result,sizeof di_mouse_result,"CREATE FAIL 0x%08lx",(unsigned long)hr);
}

static LRESULT CALLBACK wndproc(HWND hwnd,UINT msg,WPARAM w,LPARAM l)
{
    if(msg==WM_CREATE) {
        SetTimer(hwnd,1,100,NULL); return 0;
    }
    if(msg==WM_INPUT) {
        RAWINPUT raw; UINT size=sizeof raw;
        if(GetRawInputData((HRAWINPUT)l,RID_INPUT,&raw,&size,sizeof(RAWINPUTHEADER))==(UINT)-1)
            strcpy(last_raw,"GetRawInputData FAILED");
        else if(raw.header.dwType==RIM_TYPEKEYBOARD)
            snprintf(last_raw,sizeof last_raw,"KEY vk=%02x scan=%02x flags=%x msg=%x",
                     raw.data.keyboard.VKey,raw.data.keyboard.MakeCode,
                     raw.data.keyboard.Flags,raw.data.keyboard.Message);
        else if(raw.header.dwType==RIM_TYPEMOUSE)
            snprintf(last_raw,sizeof last_raw,"MOUSE flags=%x buttons=%x data=%x delta=%ld,%ld",
                     raw.data.mouse.usFlags,raw.data.mouse.usButtonFlags,
                     raw.data.mouse.usButtonData,(long)raw.data.mouse.lLastX,
                     (long)raw.data.mouse.lLastY);
        raw_count++;
        InvalidateRect(hwnd,NULL,FALSE); DefWindowProcA(hwnd,msg,w,l); return 0;
    }
    switch(msg) {
    case WM_PAINT: paint(hwnd); return 0;
    case WM_TIMER: read_directinput(); return 0;
    case WM_KEYDOWN: event_win("KEYDOWN",w,l); return 0;
    case WM_KEYUP: event_win("KEYUP",w,l); return 0;
    case WM_CHAR: event_win("CHAR",w,l); return 0;
    case WM_MOUSEMOVE: event_win("MOUSEMOVE",w,l); return 0;
    case WM_LBUTTONDOWN: event_win("LBUTTONDOWN",w,l); return 0;
    case WM_LBUTTONUP: event_win("LBUTTONUP",w,l); return 0;
    case WM_RBUTTONDOWN: event_win("RBUTTONDOWN",w,l); return 0;
    case WM_RBUTTONUP: event_win("RBUTTONUP",w,l); return 0;
    case WM_DESTROY:
        KillTimer(hwnd,1);
        if(keyboard){IDirectInputDevice8_Unacquire(keyboard);IDirectInputDevice8_Release(keyboard);}
        if(mouse){IDirectInputDevice8_Unacquire(mouse);IDirectInputDevice8_Release(mouse);}
        if(di)IDirectInput8_Release(di);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hwnd,msg,w,l);
}

int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR command,int show)
{
    (void)previous;(void)command;(void)show;
    stage("entered WinMain");
    WNDCLASSA wc={0}; wc.lpfnWndProc=wndproc; wc.hInstance=instance;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName="PwInputPathViewer";
    if(!RegisterClassA(&wc)){stage("RegisterClass failed");return 1;}
    stage("RegisterClass succeeded");
    main_window=CreateWindowExA(0,wc.lpszClassName,"Wine Input Path Viewer",
        WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1160,650,
        NULL,NULL,instance,NULL);
    if(!main_window){stage("CreateWindow failed");return 2;}
    stage("CreateWindow succeeded");
    ShowWindow(main_window,SW_SHOW); SetForegroundWindow(main_window); UpdateWindow(main_window);
    stage("window shown; initializing DirectInput");
    init_directinput(main_window);
    stage("DirectInput init returned; entering message loop");
    MSG msg; while(GetMessageA(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageA(&msg);}
    return (int)msg.wParam;
}

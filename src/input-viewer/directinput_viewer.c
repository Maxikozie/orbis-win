/* PE64 DirectInput viewer using bundled bitmap glyphs (no font dependency). */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <string.h>
#include "bitmap_font.h"

static HWND window_handle;
static LPDIRECTINPUT8A direct_input;
static LPDIRECTINPUTDEVICE8A keyboard, mouse;
static BYTE keys[256];
static DIMOUSESTATE mouse_state;
static char keyboard_status[48]="NOT INITIALIZED";
static char mouse_status[48]="NOT INITIALIZED";
static char last_key_event[64]="(none)";
static char last_mouse_event[64]="(none)";
static unsigned key_event_count, mouse_event_count;

static void draw_text(HDC dc,int x,int y,const char *text,COLORREF color)
{
    for(;*text;text++,x+=9) {
        unsigned char ch=(unsigned char)*text;
        if(ch<32||ch>127)continue;
        const unsigned char *glyph=pw_font8x16[ch-32];
        for(int row=0;row<16;row++)
            for(int col=0;col<8;col++)
                if(glyph[row]&(1u<<col))SetPixelV(dc,x+col,y+row,color);
    }
}

static char key_state(int scan) { return keys[scan]&0x80?'D':'.'; }

static void init_device(LPDIRECTINPUTDEVICE8A *device,REFGUID guid,
                        const DIDATAFORMAT *format,HWND hwnd,char *status)
{
    HRESULT hr=IDirectInput8_CreateDevice(direct_input,guid,device,NULL);
    if(SUCCEEDED(hr))hr=IDirectInputDevice8_SetDataFormat(*device,format);
    if(SUCCEEDED(hr))hr=IDirectInputDevice8_SetCooperativeLevel(*device,hwnd,
                                            DISCL_FOREGROUND|DISCL_EXCLUSIVE);
    if(SUCCEEDED(hr)) {
        DIPROPDWORD buffer={{sizeof(DIPROPDWORD),sizeof(DIPROPHEADER),0,DIPH_DEVICE},32};
        hr=IDirectInputDevice8_SetProperty(*device,DIPROP_BUFFERSIZE,&buffer.diph);
    }
    if(SUCCEEDED(hr))hr=IDirectInputDevice8_Acquire(*device);
    snprintf(status,48,"%s 0x%08lx",SUCCEEDED(hr)?"OK":"FAIL",(unsigned long)hr);
}

static void init_directinput(HWND hwnd)
{
    HRESULT hr=DirectInput8Create(GetModuleHandleA(NULL),DIRECTINPUT_VERSION,
                                  &IID_IDirectInput8A,(void**)&direct_input,NULL);
    if(FAILED(hr)) {
        snprintf(keyboard_status,sizeof keyboard_status,"CREATE FAIL 0x%08lx",(unsigned long)hr);
        snprintf(mouse_status,sizeof mouse_status,"CREATE FAIL 0x%08lx",(unsigned long)hr);
        return;
    }
    init_device(&keyboard,&GUID_SysKeyboard,&c_dfDIKeyboard,hwnd,keyboard_status);
    init_device(&mouse,&GUID_SysMouse,&c_dfDIMouse,hwnd,mouse_status);
}

static void read_device(LPDIRECTINPUTDEVICE8A device,DWORD size,void *state)
{
    if(!device)return;
    HRESULT hr=IDirectInputDevice8_GetDeviceState(device,size,state);
    if(hr==DIERR_INPUTLOST||hr==DIERR_NOTACQUIRED) {
        IDirectInputDevice8_Acquire(device);
        hr=IDirectInputDevice8_GetDeviceState(device,size,state);
    }
}

static void read_events(LPDIRECTINPUTDEVICE8A device,BOOL is_mouse)
{
    if(!device)return;
    DIDEVICEOBJECTDATA events[32];
    DWORD count=32;
    HRESULT hr=IDirectInputDevice8_GetDeviceData(device,sizeof(events[0]),events,&count,0);
    if(hr!=DI_OK&&hr!=DI_BUFFEROVERFLOW) {
        if(hr==DIERR_INPUTLOST||hr==DIERR_NOTACQUIRED)IDirectInputDevice8_Acquire(device);
        return;
    }
    for(DWORD i=0;i<count;i++) {
        if(is_mouse) {
            mouse_event_count++;
            snprintf(last_mouse_event,sizeof last_mouse_event,"OBJ %lu DATA %08lx",
                     (unsigned long)events[i].dwOfs,(unsigned long)events[i].dwData);
        } else {
            key_event_count++;
            snprintf(last_key_event,sizeof last_key_event,"SCAN %02lx %s",
                     (unsigned long)events[i].dwOfs,
                     events[i].dwData&0x80?"DOWN":"UP");
        }
    }
}

static void paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc=BeginPaint(hwnd,&ps);
    RECT r; GetClientRect(hwnd,&r);
    HBRUSH bg=CreateSolidBrush(RGB(12,20,34));
    FillRect(dc,&r,bg); DeleteObject(bg);
    COLORREF white=RGB(235,245,255),green=RGB(80,255,120),red=RGB(255,110,100);
    char line[160];
    draw_text(dc,24,18,"WINE DIRECTINPUT TEST - PE64 / BITMAP TEXT",white);
    snprintf(line,sizeof line,"KEYBOARD INIT: %s",keyboard_status); draw_text(dc,24,52,line,white);
    snprintf(line,sizeof line,"MOUSE INIT: %s",mouse_status); draw_text(dc,24,78,line,white);
    snprintf(line,sizeof line,"DI KEY EVENTS: %u  LAST: %s",key_event_count,last_key_event);
    draw_text(dc,24,112,line,white);
    draw_text(dc,24,146,"ENTER   ESC   TAB   F   E   ALT   CTRL   Z   C   R   T",white);
    snprintf(line,sizeof line,"  %c      %c     %c   %c  %c    %c     %c    %c  %c  %c  %c",
        key_state(DIK_RETURN),key_state(DIK_ESCAPE),key_state(DIK_TAB),key_state(DIK_F),
        key_state(DIK_E),key_state(DIK_LALT),key_state(DIK_LCONTROL),key_state(DIK_Z),
        key_state(DIK_C),key_state(DIK_R),key_state(DIK_T));
    draw_text(dc,24,172,line,white);
    snprintf(line,sizeof line,"ARROWS: UP %c DOWN %c LEFT %c RIGHT %c",
        key_state(DIK_UP),key_state(DIK_DOWN),key_state(DIK_LEFT),key_state(DIK_RIGHT));
    draw_text(dc,24,208,line,white);
    snprintf(line,sizeof line,"DI MOUSE: L=%c R=%c DELTA=%ld,%ld  EVENTS=%u",
        mouse_state.rgbButtons[0]&0x80?'D':'.',mouse_state.rgbButtons[1]&0x80?'D':'.',
        (long)mouse_state.lX,(long)mouse_state.lY,mouse_event_count);
    draw_text(dc,24,244,line,white);
    snprintf(line,sizeof line,"LAST DI MOUSE EVENT: %s",last_mouse_event);
    draw_text(dc,24,270,line,white);
    draw_text(dc,24,318,"PRESS ONE PAD BUTTON; WATCH DI STATE / EVENT COUNTS",green);
    draw_text(dc,24,352,"EXCLUSIVE DI MAY SUPPRESS NORMAL WM_KEY / WM_MOUSE",red);
    EndPaint(hwnd,&ps);
}

static LRESULT CALLBACK wndproc(HWND hwnd,UINT msg,WPARAM w,LPARAM l)
{
    switch(msg) {
    case WM_CREATE: SetTimer(hwnd,1,50,NULL); return 0;
    case WM_PAINT: paint(hwnd); return 0;
    case WM_TIMER:
        read_device(keyboard,sizeof keys,keys);
        read_device(mouse,sizeof mouse_state,&mouse_state);
        read_events(keyboard,FALSE);
        read_events(mouse,TRUE);
        InvalidateRect(hwnd,NULL,FALSE);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd,1);
        if(keyboard){IDirectInputDevice8_Unacquire(keyboard);IDirectInputDevice8_Release(keyboard);}
        if(mouse){IDirectInputDevice8_Unacquire(mouse);IDirectInputDevice8_Release(mouse);}
        if(direct_input)IDirectInput8_Release(direct_input);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hwnd,msg,w,l);
}

int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR command,int show)
{
    (void)previous;(void)command;(void)show;
    WNDCLASSA wc={0}; wc.lpfnWndProc=wndproc; wc.hInstance=instance;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName="PwDirectInputViewer";
    if(!RegisterClassA(&wc))return 1;
    window_handle=CreateWindowExA(0,wc.lpszClassName,"Wine DirectInput Test",
        WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1100,560,
        NULL,NULL,instance,NULL);
    if(!window_handle)return 2;
    ShowWindow(window_handle,SW_SHOW); SetForegroundWindow(window_handle); UpdateWindow(window_handle);
    init_directinput(window_handle);
    MSG msg; while(GetMessageA(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageA(&msg);}
    return (int)msg.wParam;
}

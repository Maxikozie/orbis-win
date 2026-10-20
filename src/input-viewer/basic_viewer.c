/* Minimal PE32 Wine input viewer: Win32 window messages and async key state. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "bitmap_font.h"

static HWND main_window;
static HANDLE stage_file=INVALID_HANDLE_VALUE;
static unsigned key_messages;
static unsigned mouse_moves;
static unsigned long last_state;
static char last_message[96]="(none)";
static int mouse_x,mouse_y;
static const int watched_keys[]={VK_RETURN,VK_ESCAPE,VK_TAB,'F','E',VK_MENU,
    VK_CONTROL,'Z','C','R','T',VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,VK_LBUTTON};

static void stage(const char *text)
{
    if(stage_file==INVALID_HANDLE_VALUE)
        stage_file=CreateFileA("C:\\Games\\OblivionGOG\\input-basic-stage.log",
            FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,NULL);
    if(stage_file!=INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(stage_file,text,(DWORD)strlen(text),&written,NULL);
        WriteFile(stage_file,"\r\n",2,&written,NULL);
        FlushFileBuffers(stage_file);
    }
    OutputDebugStringA(text);
}

static char key_down(int vk)
{
    return (GetAsyncKeyState(vk)&0x8000)?'D':'.';
}

static unsigned long read_state(void)
{
    unsigned long mask=0;
    for(unsigned i=0;i<sizeof watched_keys/sizeof watched_keys[0];i++)
        if(GetAsyncKeyState(watched_keys[i])&0x8000)mask|=1ul<<i;
    return mask;
}

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

static void paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc=BeginPaint(hwnd,&ps);
    RECT r; GetClientRect(hwnd,&r);
    HBRUSH bg=CreateSolidBrush(RGB(12,20,34));
    FillRect(dc,&r,bg); DeleteObject(bg);
    COLORREF white=RGB(235,245,255),green=RGB(80,255,120),red=RGB(255,110,100);
    char line[160];
    draw_text(dc,24,20,"BASIC WINE INPUT TEST - PE64 / NO DIRECTINPUT",white);
    snprintf(line,sizeof line,"WIN32 KEY MESSAGES: %u",key_messages);
    draw_text(dc,24,60,line,white);
    draw_text(dc,24,104,"ENTER       ESC         TAB         F           E",white);
    snprintf(line,sizeof line,"%s       %s       %s       %s       %s",
        key_down(VK_RETURN)?"DOWN":"UP  ",key_down(VK_ESCAPE)?"DOWN":"UP  ",
        key_down(VK_TAB)?"DOWN":"UP  ",key_down('F')?"DOWN":"UP  ",
        key_down('E')?"DOWN":"UP  ");
    draw_text(dc,24,128,line,read_state()&(1ul<<0)?green:white);
    draw_text(dc,24,176,"ALT         CTRL        Z           C           R           T",white);
    snprintf(line,sizeof line,"%s       %s       %s       %s       %s       %s",
        key_down(VK_MENU)?"DOWN":"UP  ",key_down(VK_CONTROL)?"DOWN":"UP  ",
        key_down('Z')?"DOWN":"UP  ",key_down('C')?"DOWN":"UP  ",
        key_down('R')?"DOWN":"UP  ",key_down('T')?"DOWN":"UP  ");
    draw_text(dc,24,200,line,white);
    draw_text(dc,24,248,"UP          DOWN        LEFT        RIGHT       MOUSE LEFT",white);
    snprintf(line,sizeof line,"%s       %s       %s       %s       %s",
        key_down(VK_UP)?"DOWN":"UP  ",key_down(VK_DOWN)?"DOWN":"UP  ",
        key_down(VK_LEFT)?"DOWN":"UP  ",key_down(VK_RIGHT)?"DOWN":"UP  ",
        key_down(VK_LBUTTON)?"DOWN":"UP  ");
    draw_text(dc,24,272,line,white);
    snprintf(line,sizeof line,"LAST EVENT: %s",last_message);
    draw_text(dc,24,328,line,white);
    snprintf(line,sizeof line,"MOUSE MOVE COUNT: %u  LAST X,Y: %d,%d",
             mouse_moves,mouse_x,mouse_y);
    draw_text(dc,24,352,line,white);
    draw_text(dc,24,376,"PRESS CROSS / R1 / D-PAD; CLOSE FROM PS MENU",red);
    EndPaint(hwnd,&ps);
}

static LRESULT CALLBACK wndproc(HWND hwnd,UINT msg,WPARAM w,LPARAM l)
{
    switch(msg) {
    case WM_CREATE:
        SetTimer(hwnd,1,100,NULL);
        stage("WM_CREATE succeeded");
        return 0;
    case WM_PAINT:
        paint(hwnd);
        return 0;
    case WM_TIMER:
        { unsigned long state=read_state();
          if(state!=last_state){last_state=state;InvalidateRect(hwnd,NULL,FALSE);} }
        return 0;
    case WM_KEYDOWN:
        key_messages++;
        snprintf(last_message,sizeof last_message,"KEYDOWN vk=%04lx",(unsigned long)w);
        InvalidateRect(hwnd,NULL,FALSE);
        return 0;
    case WM_KEYUP:
        key_messages++;
        snprintf(last_message,sizeof last_message,"KEYUP vk=%04lx",(unsigned long)w);
        InvalidateRect(hwnd,NULL,FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        key_messages++;
        snprintf(last_message,sizeof last_message,"MOUSE %s",msg==WM_LBUTTONDOWN?"LEFT DOWN":"LEFT UP");
        InvalidateRect(hwnd,NULL,FALSE);
        return 0;
    case WM_MOUSEMOVE:
        mouse_moves++;
        mouse_x=(short)LOWORD(l);
        mouse_y=(short)HIWORD(l);
        snprintf(last_message,sizeof last_message,"MOUSE MOVE X=%d Y=%d",mouse_x,mouse_y);
        InvalidateRect(hwnd,NULL,FALSE);
        return 0;
    case WM_DESTROY:
        if(stage_file!=INVALID_HANDLE_VALUE)CloseHandle(stage_file);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd,msg,w,l);
}

int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR command,int show)
{
    (void)previous;(void)command;(void)show;
    stage("entered WinMain");
    WNDCLASSA wc={0};
    wc.lpfnWndProc=wndproc;
    wc.hInstance=instance;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName="PwBasicInputViewer";
    if(!RegisterClassA(&wc)){stage("RegisterClass failed");return 1;}
    stage("RegisterClass succeeded");
    main_window=CreateWindowExA(0,wc.lpszClassName,"Basic Wine Input Test",
        WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1000,480,
        NULL,NULL,instance,NULL);
    if(!main_window){stage("CreateWindow failed");return 2;}
    stage("CreateWindow succeeded");
    ShowWindow(main_window,SW_SHOW);
    SetForegroundWindow(main_window);
    UpdateWindow(main_window);
    stage("window shown; entering message loop");
    last_state=read_state();
    MSG msg;
    while(GetMessageA(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageA(&msg);}
    return (int)msg.wParam;
}

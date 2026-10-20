/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Standalone PS4 pad and keyboard-map visualizer. It tests controller reads
 * and the selected mapping without starting Wine or a game. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>

#include "pw_videoout_ps4.h"
#include "pw_pad_ps4.h"

enum { W = 320, H = 180, N = 16 };
enum { WHITE = 0xffffff, MUTED = 0x91a3b0, BLACK = 0x101820,
       PANEL = 0x1b2a36, BLUE = 0x2574a8, GREEN = 0x32c875,
       YELLOW = 0xf2c14e, RED = 0xe65a5a };
#define BGRX(r,g,b) (((uint32_t)(r)<<16) | ((uint32_t)(g)<<8) | (uint32_t)(b))
#define RGB(c) BGRX(((c)>>16)&255,((c)>>8)&255,(c)&255)

typedef struct { char c; uint8_t row[7]; } Glyph;
static const Glyph font[] = {
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,18,18,12}},
    {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
    {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
    {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
    {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
    {'-',{0,0,0,31,0,0,0}}, {':',{0,4,4,0,4,4,0}},
    {'>',{16,8,4,2,4,8,16}}, {'<',{1,2,4,8,4,2,1}},
    {'/',{1,2,2,4,8,8,16}}, {'.',{0,0,0,0,0,12,12}},
    {' ',{0,0,0,0,0,0,0}}, {'?',{14,17,1,2,4,0,4}},
};

typedef struct { const char *pad, *key; uint32_t mask; uint16_t vk; } Control;
static Control controls[N] = {
    {"CROSS","CLICK",0x4000,1}, {"CIRCLE","TAB",0x2000,0x09},
    {"SQUARE","F",0x8000,'F'}, {"TRIANGLE","E",0x1000,'E'},
    {"L1","ALT",0x0400,0x12}, {"R1","CLICK",0x0800,1},
    {"L2","Z",0x0100,'Z'}, {"R2","C",0x0200,'C'},
    {"L3","CTRL",0x0002,0x11}, {"R3","R",0x0004,'R'},
    {"UP","UP",0x0010,0x26}, {"DOWN","DOWN",0x0040,0x28},
    {"LEFT","LEFT",0x0080,0x25}, {"RIGHT","RIGHT",0x0020,0x27},
    {"OPTIONS","ESC",0x0008,0x1b}, {"CREATE","T",0x0001,'T'},
};
static const struct { const char *name; uint16_t vk; } targets[] = {
    {"CLICK",1}, {"ENTER",0x0d}, {"SPACE",0x20}, {"ESC",0x1b},
    {"TAB",0x09}, {"ALT",0x12}, {"CTRL",0x11}, {"SHIFT",0x10},
    {"UP",0x26}, {"DOWN",0x28}, {"LEFT",0x25}, {"RIGHT",0x27},
    {"A",'A'}, {"B",'B'}, {"C",'C'}, {"D",'D'}, {"E",'E'},
    {"F",'F'}, {"G",'G'}, {"H",'H'}, {"I",'I'}, {"J",'J'},
    {"K",'K'}, {"L",'L'}, {"M",'M'}, {"N",'N'}, {"O",'O'},
    {"P",'P'}, {"Q",'Q'}, {"R",'R'}, {"S",'S'}, {"T",'T'},
    {"U",'U'}, {"V",'V'}, {"W",'W'}, {"X",'X'}, {"Y",'Y'},
    {"Z",'Z'},
};
static uint32_t pixels[W*H];

static void rect(int x,int y,int w,int h,uint32_t c)
{
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
    if(x+w>W)w=W-x; if(y+h>H)h=H-y;
    for(int j=0;j<h;j++) for(int i=0;i<w;i++) pixels[(y+j)*W+x+i]=c;
}
static void text(int x,int y,const char *s,uint32_t color)
{
    for(;*s;s++,x+=6) {
        char c=*s; if(c>='a'&&c<='z')c-=32;
        const Glyph *g=NULL;
        for(unsigned n=0;n<sizeof(font)/sizeof(font[0]);n++) if(font[n].c==c){g=&font[n];break;}
        if(!g)continue;
        for(int yy=0;yy<7;yy++)for(int xx=0;xx<5;xx++)
            if(g->row[yy]&(1u<<(4-xx))) rect(x+xx,y+yy,1,1,color);
    }
}
static void keycap(int x,int y,int w,int h,const char *label,int active)
{
    rect(x,y,w,h,active?GREEN:0x526575); rect(x+1,y+1,w-2,h-2,active?0x153927:PANEL);
    text(x+2,y+(h-7)/2,label,active?WHITE:MUTED);
}
static int key_held(uint32_t held,uint16_t vk)
{
    for(int i=0;i<N;i++)if(controls[i].vk==vk&&(held&controls[i].mask))return 1;
    return 0;
}
static void render(uint32_t held,int selected,int edit,int connected,int rc)
{
    memset(pixels,0,sizeof(pixels));
    rect(0,0,W,H,BLACK); rect(5,5,W-10,14,BLUE);
    text(10,9,"CONTROLLER MAP - PS4",WHITE);
    text(12,24,"DUALSHOCK 4",YELLOW); text(169,24,"KEYBOARD / MOUSE",YELLOW);
    rect(8,35,148,105,PANEL); rect(163,35,149,105,PANEL);
    /* Simple controller silhouette and live button indicators. */
    rect(35,55,92,62,0x354552); rect(42,62,78,48,0x202c35);
    for(int i=0;i<N;i++) {
        int x=0,y=0,w=20,h=11;
        switch(i) {
        case 0:x=111;y=88;break; case 1:x=127;y=72;break;
        case 2:x=95;y=72;break; case 3:x=111;y=56;break;
        case 4:x=43;y=44;w=32;break; case 5:x=87;y=44;w=32;break;
        case 6:x=43;y=57;w=32;break; case 7:x=87;y=57;w=32;break;
        case 8:x=54;y=94;break; case 9:x=96;y=94;break;
        case 10:x=17;y=72;break; case 11:x=17;y=96;break;
        case 12:x=7;y=84;break; case 13:x=27;y=84;break;
        case 14:x=72;y=74;w=10;break; case 15:x=83;y=74;w=10;break;
        }
        int active=(held&controls[i].mask)!=0;
        rect(x,y,w,h,active?GREEN:(i==selected?YELLOW:0x526575));
        const char *symbol=controls[i].pad;
        if(i==0)symbol="X"; else if(i==1)symbol="O";
        else if(i==2)symbol="[]"; else if(i==3)symbol="^";
        else if(i==14)symbol="OP"; else if(i==15)symbol="CR";
        text(x+1,y+2,symbol,active?WHITE:BLACK);
    }
    /* Keyboard rows. Assigned keys light when their physical button is held. */
    const char *rows[]={"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};
    int rowx[]={169,173,181}, rowy[]={43,58,73};
    int any_held=0;
    for(int r=0;r<3;r++)for(int k=0;rows[r][k];k++) {
        char c=rows[r][k]; int on=key_held(held,(uint16_t)c);
        keycap(rowx[r]+k*14,rowy[r],13,12,(char[]){c,0},on);
    }
    keycap(179,89,49,13,"SPACE",key_held(held,0x20));
    keycap(231,89,39,13,"TAB",key_held(held,0x09));
    keycap(273,89,31,13,"ESC",key_held(held,0x1b));
    keycap(169,106,34,13,"ALT",key_held(held,0x12));
    keycap(206,106,37,13,"CTRL",key_held(held,0x11));
    keycap(246,106,58,13,"ENTER",key_held(held,0x0d));
    /* Mouse with the left button lit for mapped click controls. */
    rect(276,54,20,28,0x7a8991); rect(278,56,7,11,0x253440);
    for(int i=0;i<N;i++)if(controls[i].vk==1&&(held&controls[i].mask))any_held=1;
    rect(278,56,7,11,any_held?GREEN:0x253440);
    text(169,126,"PAD",MUTED); text(193,126,connected?"CONNECTED":"NO PAD",connected?GREEN:RED);
    text(248,126,"RC",MUTED); char n[8]; snprintf(n,sizeof n,"%d",rc); text(267,126,n,WHITE);
    rect(8,145,W-16,28,selected>=0?0x263746:PANEL);
    text(13,149,edit?"EDIT MAP: UP/DOWN SELECT, LEFT/RIGHT ASSIGN":"VIEW: PRESS BUTTONS TO TEST",WHITE);
    char line[48]; snprintf(line,sizeof line,"%s  ->  %s",controls[selected].pad,controls[selected].key);
    text(13,160,line,YELLOW);
    text(198,160,"OPTIONS: TOGGLE EDIT",MUTED);
}

static int target_index(uint16_t vk)
{
    for(unsigned i=0;i<sizeof(targets)/sizeof(targets[0]);i++)if(targets[i].vk==vk)return (int)i;
    return 0;
}

int main(void)
{
    sceKernelDebugOutText(0,"=== Gladius Controller Map: native pad -> on-screen mapping ===\n");
    PwPadKeyMap map[N];
    for(int i=0;i<N;i++)map[i]=(PwPadKeyMap){controls[i].mask,controls[i].vk,0,0,controls[i].key};
    PwPadPs5Ops ops; PwPadPs5 pad;
    int ops_rc=pw_pad_ps4_platform_ops(&ops);
    int pad_rc=ops_rc==PW_OK?pw_pad_ps5_open(&pad,&ops,map,N):ops_rc;
    sceKernelDebugOutText(0,"pad open rc=%d\n",pad_rc);
    PwVideoOutPs4 video; int video_rc=pw_videoout_ps4_open(&video);
    sceKernelDebugOutText(0,"video open rc=%d\n",video_rc);
    if(video_rc!=PW_OK)for(;;)sleep(1);
    if(pad_rc!=PW_OK)memset(&pad,0,sizeof pad);
    int selected=0,edit=0;
    for(;;) {
        if(pad_rc==PW_OK)pw_pad_ps5_read(&pad);
        uint32_t held=pad_rc==PW_OK?pad.core.previous_buttons:0;
        uint32_t pressed=pad_rc==PW_OK?pad.core.pressed_edges:0;
        int connected=pad_rc==PW_OK&&pad.core.connected;
        if(edit) {
            if(pressed&0x10)selected=(selected+N-1)%N;
            if(pressed&0x40)selected=(selected+1)%N;
            if(pressed&0x80 || pressed&0x20) {
                int n=target_index(controls[selected].vk);
                int count=(int)(sizeof(targets)/sizeof(targets[0]));
                n=(n+((pressed&0x20)?1:count-1))%count;
                controls[selected].vk=targets[n].vk;
                controls[selected].key=targets[n].name;
                map[selected].virtual_key=controls[selected].vk;
                map[selected].action=controls[selected].key;
                char msg[96];snprintf(msg,sizeof msg,"map %s -> %s\n",controls[selected].pad,controls[selected].key);
                sceKernelDebugOutText(0,msg);
            }
        }
        if(pressed&0x8)edit=!edit;
        if(pressed&0x4000) {
            char msg[96];snprintf(msg,sizeof msg,"cross edge; mapped action=%s\n",controls[0].key);
            sceKernelDebugOutText(0,msg);
        }
        render(held,selected,edit,connected,pad_rc);
        PwPresentFrame f={.pixels=(const uint8_t*)pixels,.width=W,.height=H,
            .stride=W*4,.format=PW_PRESENT_BGRX8};
        (void)pw_videoout_ps4_present_scaled(&video,&f,PW_PRESENT_SCALE_INTEGER,RGB(BLACK));
        sceVideoOutWaitVblank();
    }
    return 0;
}

/* CPU framebuffer chat. No GXM/vita2d or system dialog renderer. */
#include <stdio.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include "llama_worker.h"
#include "dbg.h"
#include "debugScreen.h"
#include "local_calc.h"
int _newlib_heap_size_user=80*1024*1024;
void engine_set_mm_threads(int n);
void engine_set_batch_enabled(int on);
#define MAX_TURNS 24
#define COLS 58
static char users[MAX_TURNS][257],answers[MAX_TURNS][LLAMA_RESPONSE_MAX];
static char draft[257];
static char lines[1800][COLS+1];
static int turns,keyboard,row,col,shifted,scroll,nlines;
static int sample;
static const char *samples[]={"What is 17 + 28?","Explain what gravity is in one sentence.","Write three facts about the Moon."};
static const char *keys[2][4]={
 {"abcdefghij","klmnopqrst","uvwxyz0123","456789.,?!"},
 {"ABCDEFGHIJ","KLMNOPQRST","UVWXYZ:;+-","*/()@_='\"!"}
};
static const char *status="Ready. Each question uses a fresh model context.";
static void add_text(const char *s){
 int x=0;
 if(nlines>=1790)return;
 lines[nlines][0]=0;
 for(;*s && nlines<1790;s++){
  unsigned char c=(unsigned char)*s;
  if(c=='\r')continue;
  if(c=='\n'||x==COLS){lines[nlines][x]=0;nlines++;x=0;lines[nlines][0]=0;if(c=='\n')continue;}
  /* Debug font is ASCII. Preserve exact UTF-8 in response/log; render ? for unsupported glyphs. */
  if(c>=128){if((c&0xc0)==0x80)continue;c='?';}
  if(c<32)c=' ';
  lines[nlines][x++]=(char)c;lines[nlines][x]=0;
 }
 nlines++;
}
static void draw(int busy){
 psvDebugScreenPrintf("\x1b[2J\x1b[H\x1b[96mVitaLM Chat\x1b[0m  |  LFM2.5 230M Q8  |  Fully local\n");
 psvDebugScreenPrintf("%s\n",status);
 psvDebugScreenPrintf("X type | Start send | O stop/cancel | Up/Down scroll\nSelect clear | R sample\n");
 nlines=0;
 for(int i=0;i<turns;i++){add_text("You:");add_text(users[i]);add_text("Assistant:");add_text(answers[i]);add_text("");}
 int height=keyboard?17:24;
 int last=nlines-height;if(last<0)last=0;if(scroll>last)scroll=last;
 int begin=last-scroll;
 for(int i=0;i<height;i++)psvDebugScreenPrintf("%s\n",begin+i<nlines?lines[begin+i]:"");
 if(keyboard){
  psvDebugScreenPrintf("Draft: %.48s\n",strlen(draft)>48?draft+strlen(draft)-48:draft);
  for(int r=0;r<4;r++){
   for(int c=0;c<10;c++)psvDebugScreenPrintf(r==row&&c==col?"[%c]":" %c ",keys[shifted][r][c]);
   psvDebugScreenPrintf("\n");
  }
  psvDebugScreenPrintf("D-pad choose | X add | Square delete | Triangle space\nL shift\n");
  psvDebugScreenPrintf("Start send | O close keyboard (keeps draft)\n");
 }else psvDebugScreenPrintf(busy?"Generating... Circle stops; scrolling stays available.\n":"Press X to write a question. R: sample question.\nCalc: 2+3 runs the local calculator.\n");
}
static void submit(void){
 if(!draft[0]||llama_worker_get_state()==LLAMA_WORKER_RUNNING)return;
 if(turns==MAX_TURNS){memmove(users,users+1,sizeof(users)-sizeof(users[0]));memmove(answers,answers+1,sizeof(answers)-sizeof(answers[0]));turns--;}
 snprintf(users[turns],sizeof(users[turns]),"%s",draft);answers[turns][0]=0;
 char exact[96];unsigned long long calc_start=sceKernelGetProcessTimeWide();
 if(local_calculate(draft,exact,sizeof(exact))){
  snprintf(answers[turns],sizeof(answers[turns]),"Local calculator (deterministic): %s",exact);
  dbg_log("LOCAL_CALCULATOR prompt=%s result=%s latency_us=%llu",draft,exact,(unsigned long long)(sceKernelGetProcessTimeWide()-calc_start));
  turns++;draft[0]=0;keyboard=0;scroll=0;status="Local calculator completed. Model was not used.";return;
 }
 if(llama_worker_start(draft)<0){status=llama_worker_get_error();return;}
 turns++;draft[0]=0;keyboard=0;scroll=0;status="Generating... Circle stops. Fresh question context.";
}
int main(void){
 psvDebugScreenInit();psvDebugScreenSetFont(psvDebugScreenScaleFont2x(psvDebugScreenGetFont()));dbg_init();sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
 psvDebugScreenPrintf("VitaLM Chat\nLoading local model...\n");
 engine_set_batch_enabled(1);
 if(llama_worker_init()<0){psvDebugScreenPrintf("%s\n",llama_worker_get_error());while(1)sceKernelDelayThread(100000);}
 engine_set_mm_threads(3);
 dbg_log("CHAT_READY LFM2.5 procedural LoRA Q8; original tokenizer; CPU framebuffer; 3 participants");
 unsigned prev=0;int tick=0,redraw=1,was_busy=0;
 for(;;){
  SceCtrlData pad={0};sceCtrlPeekBufferPositive(0,&pad,1);unsigned pressed=pad.buttons&~prev;prev=pad.buttons;
  int busy=llama_worker_get_state()==LLAMA_WORKER_RUNNING;
  if(turns&&(busy||was_busy)){snprintf(answers[turns-1],sizeof(answers[0]),"%s",llama_worker_get_response());if(!busy){status="Ready. Each question uses a fresh model context.";redraw=1;}}
  was_busy=busy;
  if(pressed){redraw=1;
   if(keyboard){
    if(pressed&SCE_CTRL_UP)row=(row+3)%4;
    if(pressed&SCE_CTRL_DOWN)row=(row+1)%4;
    if(pressed&SCE_CTRL_LEFT)col=(col+9)%10;
    if(pressed&SCE_CTRL_RIGHT)col=(col+1)%10;
    if(pressed&SCE_CTRL_LTRIGGER)shifted=!shifted;
    size_t len=strlen(draft);
    if((pressed&SCE_CTRL_CROSS)&&len<256){draft[len]=keys[shifted][row][col];draft[len+1]=0;}
    if((pressed&SCE_CTRL_TRIANGLE)&&len<256){draft[len]=' ';draft[len+1]=0;}
    if((pressed&SCE_CTRL_SQUARE)&&len)draft[len-1]=0;
    if(pressed&SCE_CTRL_CIRCLE)keyboard=0;
    if(pressed&SCE_CTRL_START)submit();
   }else{
    if(pressed&SCE_CTRL_UP)scroll+=3;
    if(pressed&SCE_CTRL_DOWN){scroll-=3;if(scroll<0)scroll=0;}
    if((pressed&SCE_CTRL_CROSS)&&!busy)keyboard=1;
    if((pressed&SCE_CTRL_RTRIGGER)&&!busy){snprintf(draft,sizeof(draft),"%s",samples[sample++%3]);keyboard=1;}
    if((pressed&SCE_CTRL_CIRCLE)&&busy){llama_worker_stop();status="Stopped. Press X for a new question.";}
    if((pressed&SCE_CTRL_SELECT)&&!busy){turns=0;scroll=0;status="Conversation display cleared.";}
    if((pressed&SCE_CTRL_START)&&!busy)submit();
   }
  }
  if(redraw||(busy&&tick%12==0)){draw(busy);redraw=0;}
  tick++;sceDisplayWaitVblankStart();
 }
}

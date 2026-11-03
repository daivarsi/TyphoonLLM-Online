#include "online.h"
#include "chat_view.h"
#include "canvas.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "typhoon_logo.inc"
#include "storm_art.inc"
#define VIEW_HEIGHT 338
static uint32_t bg,ink,muted,green,line,user_bg,surface,composer,action;
static uint32_t*storm_pixels;static int storm_allocated,storm_theme=-1;
static void storm_background(uint32_t*p,int dark){
 if(!storm_allocated){storm_allocated=1;storm_pixels=malloc(960*544*sizeof(*storm_pixels));}
 if(!storm_pixels){canvas_rect(0,0,960,544,bg);return;}
 if(storm_theme!=dark){storm_theme=dark;const unsigned char*idx=dark?storm_dark:storm_light;for(int i=0;i<960*544;i++)storm_pixels[i]=storm_palette[idx[i]];}
 for(int y=0;y<544;y++)memcpy(p+y*1024,storm_pixels+y*960,960*4);
}
static void storm_mark(int x,int y,int size){for(int j=0;j<size;j++)for(int i=0;i<size;i++){int dx=2*i-size,dy=2*j-size;if(dx*dx+dy*dy<=size*size)canvas_rect(x+i,y+j,1,1,typhoon_logo[(j*128/size)*128+i*128/size]);}}
static int max_top(ChatScroll*s){int m=s->total-VIEW_HEIGHT;return m>0?m:0;}
void chat_scroll_update(ChatScroll*s,int total){s->total=total;int m=max_top(s);if(s->follow||s->top>m)s->top=m;if(s->top<0)s->top=0;}
void chat_scroll_move(ChatScroll*s,int delta){int m=max_top(s);s->top+=delta;if(s->top<0)s->top=0;if(s->top>m)s->top=m;s->follow=s->top==m;}
void chat_scroll_latest(ChatScroll*s){s->follow=1;s->top=max_top(s);}

int chat_message(const char*text,int user,int y,int draw){
 int w=user?620:800,x=user?308:76;
 const char*source=0;char body[CHAT_ANSWER];const char*main=text;
 if(!user){const char*next=text;while((next=strstr(next,"\nSource: "))){source=next;next++;}
  if(source){unsigned n=source-text;while(n&&(text[n-1]=='\n'||text[n-1]=='\r'))n--;if(n>=sizeof(body))n=sizeof(body)-1;memcpy(body,text,n);body[n]=0;main=body;source++;}}
 int one=canvas_width(main,22)+40;
 if(source){int sw=canvas_width(source,14)+40;if(sw>one)one=sw;}
 if(one<100)one=100;
 if(one<w){if(user)x+=w-one;w=one;}
 int main_h=canvas_wrap(0,0,w-40,main,22,ink,0);
 int source_h=source?10+canvas_wrap(0,0,w-40,source,14,muted,0):0;
 int h=main_h+source_h+30;
 if(draw){if(user)canvas_round(x,y,w,h,17,user_bg);else storm_mark(30,y+14,27);
  canvas_wrap(x+20,y+14,w-40,main,22,ink,1);
  if(source)canvas_wrap(x+20,y+14+main_h+10,w-40,source,14,muted,1);}
 return h+24;
}

void chat_view(uint32_t*p,char users[][1025],char answers[][CHAT_ANSWER],int turns,const char*draft,const char*status,int busy,int options,int phase,ChatScroll*scroll,const char*debug,int dark){
 bg=dark?RGB(11,19,29):RGB(246,244,238);ink=dark?RGB(237,240,235):RGB(31,42,45);muted=dark?RGB(161,178,184):RGB(87,105,108);green=dark?RGB(131,197,202):RGB(35,101,110);line=dark?RGB(46,60,69):RGB(206,211,204);user_bg=dark?RGB(29,48,61):RGB(224,232,226);surface=dark?RGB(22,34,46):RGB(234,236,229);composer=dark?RGB(22,35,47):RGB(235,237,230);action=dark?RGB(221,180,115):RGB(112,77,27);
 canvas_begin(p);storm_background(p,dark);
 storm_mark(28,14,35);canvas_text(75,14,"TyphoonLLM Online",28,ink);
 int title_dot=75+canvas_width("TyphoonLLM Online",28)+14,badge=online_badge_state();if(badge==ONLINE_BADGE_ONLINE)canvas_round(title_dot,27,7,7,3,RGB(68,200,110));canvas_text(title_dot+16,22,online_badge_label(badge),12,muted);
 canvas_round(710,12,162,42,21,surface);canvas_text(735,23,"+ New chat",16,ink);
 canvas_round(886,12,48,42,21,surface);for(int x=899;x<=919;x+=10)canvas_round(x,31,3,3,1,ink);
 canvas_rect(32,65,896,1,line);
 int total=0;for(int i=0;i<turns;i++){total+=chat_message(users[i],1,0,0);total+=chat_message(answers[i][0]?answers[i]:busy&&i==turns-1?"Thinking...":"",0,0,0);}
 chat_scroll_update(scroll,total);canvas_clip(0,76,940,414);int y=76-scroll->top;
 if(!turns){canvas_text(64,128,"Where shall",44,ink);canvas_text(64,177,"we begin?",44,ink);
  canvas_wrap(67,263,530,"Ask a question or work through an idea.",20,muted,1);
  canvas_rect(67,323,72,1,line);canvas_round(148,320,5,5,1,action);canvas_rect(162,323,30,1,line);}
 for(int i=0;i<turns;i++){y+=chat_message(users[i],1,y,1);y+=chat_message(answers[i][0]?answers[i]:busy&&i==turns-1?"Thinking...":"",0,y,1);}
 canvas_clip(0,0,960,544);
 if(total>VIEW_HEIGHT){int h=VIEW_HEIGHT*VIEW_HEIGHT/total;if(h<20)h=20;canvas_round(947,76,3,VIEW_HEIGHT,1,line);canvas_round(947,76+(VIEW_HEIGHT-h)*scroll->top/(total-VIEW_HEIGHT),3,h,1,muted);}
 if(!scroll->follow&&scroll->top<max_top(scroll)){canvas_round(751,369,165,36,18,action);canvas_text(774,378,"Latest below",16,dark?RGB(11,19,29):RGB(255,255,255));}
 canvas_rect(0,415,960,129,bg);canvas_round(32,429,5,5,2,busy?(phase%3?green:action):muted);canvas_text(45,420,status,16,muted);
 canvas_round(24,449,766,55,24,line);canvas_round(25,450,764,53,23,composer);
 canvas_clip(42,455,770,497);canvas_text(45,466,draft[0]?draft:"Message Typhoon LLM...",20,draft[0]?ink:muted);canvas_clip(0,0,960,544);
 canvas_round(806,449,130,55,27,busy?RGB(149,61,60):action);canvas_text(845,466,busy?"Stop":"Send",20,busy||!dark?RGB(255,255,255):RGB(11,19,29));
 canvas_text(32,521,"X Write     START Send     O Stop     SELECT New chat",14,muted);canvas_text(824,521,"L Settings",14,muted);
 if(options){canvas_round(130,87,700,338,18,line);canvas_round(131,88,698,336,17,bg);
  canvas_text(154,103,"Settings & session",24,ink);canvas_wrap(154,148,650,debug,16,ink,1);
  canvas_round(154,350,302,40,20,surface);canvas_text(172,361,dark?"Square: switch to Light":"Square: switch to Dark",16,ink);
  canvas_text(154,400,busy?"Up/Down scroll. R latest. L close. O stop.":"Up/Down scroll. R latest. L or O close.",14,muted);}
}


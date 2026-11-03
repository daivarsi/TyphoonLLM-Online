#include "ui_gesture.h"
static int distance(int a,int b){return a>b?a-b:b-a;}
void ui_gesture_cancel(UiGesture*g){*g=(UiGesture){0};}
UiGestureEvent ui_gesture_step(UiGesture*g,int down,int x,int y,UiAction hit){
 UiGestureEvent event={UI_NONE,0};
 if(down){
  if(!g->active){
   g->active=1;g->moved=0;g->start_x=x;g->start_y=y;g->last_y=y;g->action=hit;
   /* Cancellation remains immediate, even if the finger stays down. */
   if(hit==UI_STOP||hit==UI_NEW){event.action=hit;g->action=UI_NONE;}
  }else{
   if(distance(x,g->start_x)>10||distance(y,g->start_y)>10)g->moved=1;
   if(g->action==UI_SCROLL){event.scroll=g->last_y-y;g->last_y=y;}
   else if(hit!=g->action)g->moved=1;
  }
 }else if(g->active){
  if(!g->moved&&g->action!=UI_SCROLL)event.action=g->action;
  ui_gesture_cancel(g);
 }
 return event;
}

/* Native IME, bounded worker, and a view independent of model state. */
#include <stdio.h>
#include "src/session.h"
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/display.h>
#include <psp2/power.h>
#include <psp2/kernel/processmgr.h>
#include "llama_worker.h"
#include "ui_platform.h"
#include "chat_view.h"
#include "ui_gesture.h"
#include "app_paths.h"
#include "dbg.h"
#include "online.h"
int _newlib_heap_size_user=80*1024*1024;
void engine_set_mm_threads(int);void engine_set_batch_enabled(int);
#define TURNS CHAT_TURNS
static char users[TURNS][1025],answers[TURNS][LLAMA_RESPONSE_MAX],draft[1025];
static int turns,multi=1,stats,dirty=1,was_busy,stopped,reset_pending,response_pending,dark_mode=1;
static ChatScroll viewport={0,1,0};
static const char*status="Loading model...";
static uint64_t request_started,stress_wait_until;
static UiGesture gesture;static int keyboard_pending,input_block;
static FILE*stress_file;static int test_cancel_ms;static uint64_t test_at;static int test_multi,test_policy,test_hard=160,test_soft=120;static int stress_index,stress_pending,ime_age,stress_cancel;
static char stress_prompt[1025];static uint64_t stress_started,stress_token_start;
static int test_pause_requested(void){
 static uint64_t next_check;static int paused;uint64_t now=sceKernelGetProcessTimeWide();
 if(now>=next_check){char path[180];snprintf(path,sizeof(path),"%s/test.pause",app_data_dir());FILE*f=fopen(path,"r");paused=f!=0;if(f)fclose(f);next_check=now+250000;}
 return paused;
}

static void render(int busy){
 char detail[1000],label[96];llama_worker_statistics(detail,sizeof(detail));
 size_t details_used=strlen(detail);snprintf(detail+details_used,sizeof(detail)-details_used,"\nOnline: %s / Triangle changes mode",online_mode_name());
 snprintf(label,sizeof(label),"%s",status);
 if(busy)snprintf(label,sizeof(label),"%s / %llu s",turns&&answers[turns-1][0]?"Generating":"Thinking",(unsigned long long)((sceKernelGetProcessTimeWide()-request_started)/1000000));
 if(*online_status())snprintf(label,sizeof(label),"%s / %s%s%s",online_mode_name(),online_status(),busy?"":" / ",busy?"":status);
 else if(!busy)snprintf(label,sizeof(label),"%s / %s",online_mode_name(),status);
 if(stopped&&busy)snprintf(label,sizeof(label),"Stopping...");
 if(reset_pending)snprintf(label,sizeof(label),"Stopping to start a new chat...");
 chat_view(ui_pixels(),users,answers,turns,draft,label,busy,stats,(int)(sceKernelGetProcessTimeWide()/350000),&viewport,detail,dark_mode);ui_present();
 dbg_log("UI state=%s busy=%d turns=%d top=%d follow=%d total=%d multi=%d reset_pending=%d",label,busy,turns,viewport.top,viewport.follow,viewport.total,multi,reset_pending);
}
static void clear_chat(void){
 keyboard_pending=0;input_block=1;ui_gesture_cancel(&gesture);
 if(llama_worker_get_state()==LLAMA_WORKER_RUNNING){reset_pending=1;llama_worker_stop();dirty=1;return;}
 llama_worker_reset();memset(users,0,sizeof(users));memset(answers,0,sizeof(answers));memset(draft,0,sizeof(draft));reset_pending=0;response_pending=0;turns=0;multi=1;viewport=(ChatScroll){0,1,0};status="Ready for a new conversation";stopped=0;dirty=1;
 dbg_log("UI clear mode=%d heap=%u",multi,(unsigned)mallinfo().uordblks);
}
static void submit(int budget){
 (void)budget;if(!draft[0]||reset_pending||llama_worker_get_state()==LLAMA_WORKER_RUNNING)return;
 if(llama_worker_request(draft,multi,stress_file?test_soft:120,stress_file?test_hard:160,stress_file?test_policy:3)<0){status="Unable to start. See session details.";dirty=1;return;}
 if(turns==TURNS){memmove(users,users+1,sizeof(users)-sizeof(users[0]));memmove(answers,answers+1,sizeof(answers)-sizeof(answers[0]));turns--;}
 snprintf(users[turns],sizeof(users[0]),"%s",draft);answers[turns][0]=0;turns++;response_pending=1;draft[0]=0;chat_scroll_latest(&viewport);status="Thinking";stopped=0;request_started=sceKernelGetProcessTimeWide();dirty=1;
}
/* Manual continuation remains available only to private debug workloads. */
static void continue_answer(void){if(turns&&llama_worker_get_state()!=LLAMA_WORKER_RUNNING&&llama_worker_continue()==0){response_pending=1;status="Generating";request_started=sceKernelGetProcessTimeWide();dirty=1;}}
static void stop_answer(void){if(llama_worker_get_state()==LLAMA_WORKER_RUNNING){llama_worker_stop();stopped=1;status="Stopping...";dirty=1;}}
static UiAction hit_test(int x,int y,int busy){
 if(x<0||x>=960||y<0||y>=544)return UI_NONE;
 if(stats&&x>=154&&x<456&&y>=350&&y<390)return UI_THEME;
 if(y<65&&x>=886)return UI_MENU;
 if(y<65&&x>=710)return UI_NEW;
 if(y>=449&&y<508)return x>=806?(busy?UI_STOP:UI_SEND):UI_COMPOSE;
 if(stats)return UI_NONE;
 if(y>=369&&y<=405&&x>=751&&!viewport.follow)return UI_LATEST;
 if(y>=76&&y<414)return UI_SCROLL;
 return UI_NONE;
}
static void open_keyboard(void){if(llama_worker_get_state()==LLAMA_WORKER_RUNNING||reset_pending)return;if(ui_ime_open(draft)<0)status="Keyboard unavailable";dirty=1;}
int main(void){
 if(app_paths_init()<0)return 1;dbg_init();online_load_settings();int rc=ui_platform_init();if(rc<0){dbg_log("UI_INIT_FAILURE rc=%x",rc);return 1;}
 online_start();render(0);engine_set_batch_enabled(1);if(llama_worker_init()<0){status="Model could not load";render(0);while(1)sceDisplayWaitVblankStart();}
 dbg_log("RUNTIME_CLOCKS override=none sdk_cpu_MHz=%d sdk_gpu_MHz=%d sdk_xbar_MHz=%d sdk_bus_MHz=%d",scePowerGetArmClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency(),scePowerGetBusClockFrequency());
 engine_set_mm_threads(3);llama_worker_prepare_prefix();status="Ready";dirty=1;sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,SCE_TOUCH_SAMPLING_STATE_START);
 SceTouchPanelInfo panel={0};int panel_rc=sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT,&panel);
 if(panel_rc<0||panel.maxDispX<=panel.minDispX||panel.maxDispY<=panel.minDispY){panel.minDispX=panel.minDispY=0;panel.maxDispX=1919;panel.maxDispY=1087;}
 dbg_log("INPUT panel_rc=%x min=%d,%d max=%d,%d",panel_rc,panel.minDispX,panel.minDispY,panel.maxDispX,panel.maxDispY);
 unsigned prev=0;int frame=0,last_badge=-1;uint64_t last_poll=0;int gap_logs=0,previous_contacts=0;
 if(app_test_path())stress_file=fopen(app_test_path(),"r");if(stress_file){stress_started=sceKernelGetProcessTimeWide();dbg_log("STRESS start heap=%u",(unsigned)mallinfo().uordblks);}
 dbg_log("PHASE4_UI_READY version=00.17 model=Q8 native_ime=1 default_multi=%d",multi);
 while(1){
 int state=llama_worker_get_state(),busy=state==LLAMA_WORKER_RUNNING;
 if(turns&&(busy||response_pending)){
  const char*r=llama_worker_get_response();if(strcmp(answers[turns-1],r)){snprintf(answers[turns-1],sizeof(answers[0]),"%s",r);dirty=1;}
  if(!busy){response_pending=0;status=state==LLAMA_WORKER_ERROR?"Generation error":llama_worker_completion();dirty=1;if(stress_file)dbg_log("STRESS complete index=%d mode=%s heap=%u output_bytes=%u",stress_index,multi?"multi":"single",(unsigned)mallinfo().uordblks,(unsigned)strlen(r));}
 }
 if(!busy&&reset_pending)clear_chat();was_busy=busy;
 int done=ui_ime_poll(draft,sizeof(draft));if(done){ui_gesture_cancel(&gesture);keyboard_pending=0;input_block=1;
 SceTouchSamplingState sampling=SCE_TOUCH_SAMPLING_STATE_STOP;int sr=sceTouchGetSamplingState(SCE_TOUCH_PORT_FRONT,&sampling);int rr=sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,SCE_TOUCH_SAMPLING_STATE_START);
 if(app_test_path())dbg_log("INPUT ime_return sampling_rc=%x prior_state=%d restart_rc=%x",sr,sampling,rr);
 dirty=1;if(stress_pending){snprintf(draft,sizeof(draft),"%s",stress_prompt);stress_pending=0;stress_index++;submit(test_hard);test_at=sceKernelGetProcessTimeWide();stress_token_start=sceKernelGetProcessTimeWide();stress_cancel=test_cancel_ms>0;}else if(done==1)submit(160);}
 busy=llama_worker_get_state()==LLAMA_WORKER_RUNNING;
 if(stress_file&&!test_pause_requested()&&!busy&&!ui_ime_active()&&!stress_pending&&!was_busy&&sceKernelGetProcessTimeWide()>=stress_wait_until){if(fgets(stress_prompt,sizeof(stress_prompt),stress_file)){stress_prompt[strcspn(stress_prompt,"\r\n")]=0;
 if(!strncmp(stress_prompt,"ONLINE_MODE ",12)){online_set_mode(atoi(stress_prompt+12));dbg_log("ONLINE_TEST mode=%s",online_mode_name());}
 else if(!strncmp(stress_prompt,"WAIT ",5)){int ms=atoi(stress_prompt+5);if(ms<0)ms=0;if(ms>30000)ms=30000;stress_wait_until=sceKernelGetProcessTimeWide()+(uint64_t)ms*1000;dbg_log("TEST wait_ms=%d",ms);}
 else if(!strncmp(stress_prompt,"AUTO ",5)){llama_worker_set_auto(atoi(stress_prompt+5));dbg_log("TEST auto=%d",atoi(stress_prompt+5));}
 else if(!strcmp(stress_prompt,"ASSERT_DEFAULT")){dbg_log("TEST default_multi=%d",multi);}
 else if(!strncmp(stress_prompt,"EVIDENCE_PACK ",14)){llama_worker_set_evidence_pack(atoi(stress_prompt+14));dbg_log("TEST evidence_pack=%d",atoi(stress_prompt+14));}
 else if(!strncmp(stress_prompt,"PREFILL_HEAD_SKIP ",18)){session_set_prefill_head_skip(atoi(stress_prompt+18));dbg_log("TEST prefill_head_skip=%d",atoi(stress_prompt+18));}
 else if(!strcmp(stress_prompt,"CLEAR")){clear_chat();dbg_log("TEST clear");}
 else if(!strncmp(stress_prompt,"CONTINUE",8)){test_cancel_ms=atoi(stress_prompt+8);test_at=sceKernelGetProcessTimeWide();stress_token_start=test_at;stress_cancel=test_cancel_ms>0;stress_index++;continue_answer();dbg_log("TEST continue index=%d cancel_ms=%d",stress_index,test_cancel_ms);}
 else{char*q=stress_prompt;char*fields[7];int valid=1;for(int i=0;i<7;i++){fields[i]=q;char*t=strchr(q,'\t');if(!t){valid=0;break;}*t=0;q=t+1;}
 if(valid){test_hard=atoi(fields[0]);test_soft=atoi(fields[1]);test_policy=atoi(fields[2]);if(atoi(fields[3])>=0)multi=atoi(fields[3]);llama_worker_set_reuse(atoi(fields[4]));test_cancel_ms=atoi(fields[5]);dbg_log("TEST request next=%d hard=%d soft=%d policy=%d multi=%d reuse=%s cancel_ms=%d label=%s",stress_index+1,test_hard,test_soft,test_policy,multi,fields[4],test_cancel_ms,fields[6]);snprintf(draft,sizeof(draft),"%s",q);memmove(stress_prompt,q,strlen(q)+1);open_keyboard();stress_pending=1;ime_age=0;}
 else dbg_log("TEST invalid workload row");}
}else{fclose(stress_file);stress_file=0;rename(app_test_path(),app_test_done());dbg_log("STRESS COMPLETE prompts=%d elapsed_us=%llu heap=%u",stress_index,(unsigned long long)(sceKernelGetProcessTimeWide()-stress_started),(unsigned)mallinfo().uordblks);status="Stability run complete";dirty=1;}}
 if(stress_pending&&ui_ime_active()&&ime_age++==20)ui_ime_abort();if(stress_file&&busy&&stress_cancel&&sceKernelGetProcessTimeWide()-stress_token_start>(uint64_t)test_cancel_ms*1000){llama_worker_stop();stopped=1;stress_cancel=0;}

 SceCtrlData p={0};sceCtrlPeekBufferPositive(0,&p,1);unsigned keys=p.buttons&~prev;prev=p.buttons;
 if(!ui_ime_active()){
 if((keys&SCE_CTRL_TRIANGLE)&&!busy){online_cycle_mode();dirty=1;}
 if((keys&SCE_CTRL_CROSS)&&!busy&&!reset_pending&&!input_block)keyboard_pending=1;if(keys&SCE_CTRL_START)submit(160);
 if(keys&SCE_CTRL_CIRCLE){if(busy)stop_answer();else{stats=0;dirty=1;}}
 if(keys&SCE_CTRL_SELECT)clear_chat();if(keys&SCE_CTRL_LTRIGGER){stats=!stats;dirty=1;}
 if((keys&SCE_CTRL_SQUARE)&&stats){dark_mode=!dark_mode;dirty=1;dbg_log("UI theme=%s",dark_mode?"dark":"light");}
 if((p.buttons&SCE_CTRL_UP)&&frame%6==0){chat_scroll_move(&viewport,-42);dirty=1;}
 if((p.buttons&SCE_CTRL_DOWN)&&frame%6==0){chat_scroll_move(&viewport,42);dirty=1;}
 if(keys&SCE_CTRL_RTRIGGER){chat_scroll_latest(&viewport);dirty=1;}
 SceTouchData t={0};int touch_rc=sceTouchPeek(SCE_TOUCH_PORT_FRONT,&t,1);uint64_t poll=sceKernelGetProcessTimeWide();
 if(app_test_path()&&last_poll&&poll-last_poll>80000&&gap_logs++<64)dbg_log("INPUT poll_gap_us=%llu busy=%d",(unsigned long long)(poll-last_poll),busy);last_poll=poll;
 if(touch_rc>0){
  int down=t.reportNum>0,x=0,y=0;
  if(down){x=(t.report[0].x-panel.minDispX)*960/(panel.maxDispX-panel.minDispX+1);y=(t.report[0].y-panel.minDispY)*544/(panel.maxDispY-panel.minDispY+1);}
  if(app_test_path()&&previous_contacts!=down)dbg_log("INPUT edge=%s raw=%d,%d mapped=%d,%d contacts=%u",down?"down":"up",down?t.report[0].x:0,down?t.report[0].y:0,x,y,t.reportNum);previous_contacts=down;
  if(input_block){ui_gesture_cancel(&gesture);if(!down&&!(p.buttons&SCE_CTRL_CROSS))input_block=0;}
  else if(t.reportNum>1){ui_gesture_cancel(&gesture);input_block=1;keyboard_pending=0;}
  else{
   UiGestureEvent e=ui_gesture_step(&gesture,down,x,y,hit_test(x,y,busy));
   if(e.scroll){chat_scroll_move(&viewport,e.scroll);dirty=1;}
   if(e.action!=UI_NONE&&app_test_path())dbg_log("INPUT action=%d down=%d",e.action,down);
   switch(e.action){
    case UI_COMPOSE:if(!busy&&!reset_pending)keyboard_pending=1;break;
    case UI_SEND:submit(160);break;
    case UI_STOP:stop_answer();break;
    case UI_NEW:keyboard_pending=0;clear_chat();break;
    case UI_MENU:stats=!stats;dirty=1;break;
    case UI_THEME:dark_mode=!dark_mode;dirty=1;dbg_log("UI theme=%s",dark_mode?"dark":"light");break;
    case UI_LATEST:chat_scroll_latest(&viewport);dirty=1;break;
    default:break;
   }
  }
  if(keyboard_pending&&!down&&!(p.buttons&SCE_CTRL_CROSS)&&!input_block){keyboard_pending=0;open_keyboard();}
 }else if(touch_rc<0){ui_gesture_cancel(&gesture);input_block=1;keyboard_pending=0;}

 }
 if(frame%60==0){int badge=online_badge_state();if(badge!=last_badge){last_badge=badge;dirty=1;}}
 if(dirty||ui_ime_active()||(busy&&frame%15==0)){render(busy);dirty=0;}else sceDisplayWaitVblankStart();frame++;
 }
}

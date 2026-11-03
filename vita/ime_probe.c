#include "ui_platform.h"
#include "dbg.h"
#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr.h>
#include <malloc.h>
int _newlib_heap_size_user=8*1024*1024;
int main(void){dbg_init();int rc=ui_platform_init();dbg_log("IME_PROBE init=%x heap=%u",rc,(unsigned)mallinfo().uordblks);if(rc<0)while(1)sceKernelDelayThread(1000000);sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);unsigned prev=0;char out[1024]={0};int auto_cycles=0,cycles=0,age=0;
 while(1){SceCtrlData p={0};sceCtrlPeekBufferPositive(0,&p,1);unsigned keys=p.buttons&~prev;prev=p.buttons;if(keys&SCE_CTRL_CROSS)ui_ime_open("Hello");if(keys&SCE_CTRL_START)auto_cycles=60;
 int done=ui_ime_poll(out,sizeof(out));if(done){cycles++;dbg_log("IME_PROBE cycle=%d done=%d text=%s heap=%u",cycles,done,out,(unsigned)mallinfo().uordblks);}
 if(auto_cycles&&!ui_ime_active()){if(ui_ime_open("probe")>=0){auto_cycles--;age=0;}}if(auto_cycles&&ui_ime_active()&&age++==12)ui_ime_abort();
 uint32_t*b=ui_pixels();for(int i=0;i<1024*544;i++)b[i]=0xff20180f;ui_present();
 }
}

/* CPU-drawn chat, official minimal common-dialog GXM presentation only.
 * Based on VitaSDK samples/ime; no vita2d, shaders, old renderer or GXM context. */
#include "ui_platform.h"
#include "dbg.h"
#include <string.h>
#include <psp2/apputil.h>
#include <psp2/sysmodule.h>
#include <psp2/ime_dialog.h>
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
enum {STRIDE=1024,HEIGHT=544,SIZE=((STRIDE*HEIGHT*4+262143)&~262143)};
static struct {void *data;SceUID uid;SceGxmSyncObject*sync;SceGxmColorSurface surface;} fb[2];
static int back,front,active,initialized;
static SceWChar16 input[257],initial16[257];
static void display_cb(const void *p){SceDisplayFrameBuf f={sizeof(f),*(void**)p,STRIDE,0,960,544};sceDisplaySetFrameBuf(&f,SCE_DISPLAY_SETBUF_NEXTFRAME);}
static void utf16(const char*s,SceWChar16*d){int n=0;while(*s&&n<255){unsigned c=(unsigned char)*s++;if(c>=0xf0){c&=7;for(int i=0;i<3&&*s;i++)c=(c<<6)|(*s++&63);}else if(c>=0xe0){c&=15;for(int i=0;i<2&&*s;i++)c=(c<<6)|(*s++&63);}else if(c>=0xc0){c=(c&31)<<6;if(*s)c|=*s++&63;}if(c>0xffff){c-=0x10000;d[n++]=0xd800+(c>>10);d[n++]=0xdc00+(c&1023);}else d[n++]=c;}d[n]=0;}
static void utf8(const SceWChar16*s,char*d,int max){int n=0;for(int i=0;s[i]&&n+4<max;i++){unsigned c=s[i];if(c>=0xd800&&c<0xdc00&&s[i+1]>=0xdc00&&s[i+1]<0xe000)c=0x10000+((c-0xd800)<<10)+(s[++i]-0xdc00);if(c<128)d[n++]=c;else if(c<2048){d[n++]=0xc0|(c>>6);d[n++]=0x80|(c&63);}else if(c<65536){d[n++]=0xe0|(c>>12);d[n++]=0x80|((c>>6)&63);d[n++]=0x80|(c&63);}else{d[n++]=0xf0|(c>>18);d[n++]=0x80|((c>>12)&63);d[n++]=0x80|((c>>6)&63);d[n++]=0x80|(c&63);}}d[n]=0;}
int ui_platform_init(void){
 sceAppUtilInit(&(SceAppUtilInitParam){0},&(SceAppUtilBootParam){0});
 sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
 SceCommonDialogConfigParam cfg;sceCommonDialogConfigParamInit(&cfg);cfg.language=SCE_SYSTEM_PARAM_LANG_ENGLISH_US;cfg.enterButtonAssign=SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;int r=sceCommonDialogSetConfigParam(&cfg);dbg_log("IME config rc=%x",r);
 SceGxmInitializeParams g={0,1,display_cb,sizeof(void*),SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE};r=sceGxmInitialize(&g);dbg_log("IME gxm initialize rc=%x",r);if(r<0)return r;
 for(int i=0;i<2;i++){fb[i].uid=sceKernelAllocMemBlock("VitaLM UI",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,SIZE,0);if(fb[i].uid<0)return fb[i].uid;sceKernelGetMemBlockBase(fb[i].uid,&fb[i].data);r=sceGxmMapMemory(fb[i].data,SIZE,SCE_GXM_MEMORY_ATTRIB_READ|SCE_GXM_MEMORY_ATTRIB_WRITE);if(r<0)return r;memset(fb[i].data,0,SIZE);r=sceGxmColorSurfaceInit(&fb[i].surface,SCE_GXM_COLOR_FORMAT_A8B8G8R8,SCE_GXM_COLOR_SURFACE_LINEAR,SCE_GXM_COLOR_SURFACE_SCALE_NONE,SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,960,544,STRIDE,fb[i].data);if(r<0)return r;sceGxmSyncObjectCreate(&fb[i].sync);}
 initialized=1;return 0;
}
uint32_t*ui_pixels(void){return fb[back].data;}
int ui_ime_active(void){return active;}
int ui_ime_open(const char*text){if(active)return -1;utf16(text,initial16);memset(input,0,sizeof(input));SceImeDialogParam p;sceImeDialogParamInit(&p);p.supportedLanguages=SCE_IME_LANGUAGE_ENGLISH;p.languagesForced=SCE_TRUE;p.type=SCE_IME_TYPE_DEFAULT;p.dialogMode=SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;p.title=(const SceWChar16*)u"Message TyphoonLLM";p.maxTextLength=256;p.initialText=initial16;p.inputTextBuffer=input;int r=sceImeDialogInit(&p);dbg_log("IME open rc=%x",r);if(r>=0)active=1;return r;}
int ui_ime_poll(char*out,int max){if(!active||sceImeDialogGetStatus()!=SCE_COMMON_DIALOG_STATUS_FINISHED)return 0;SceImeDialogResult r={0};int rc=sceImeDialogGetResult(&r);if(rc>=0&&r.button==SCE_IME_DIALOG_BUTTON_ENTER)utf8(input,out,max);int t=sceImeDialogTerm();active=0;dbg_log("IME close result=%d button=%d term=%x",r.result,r.button,t);return r.button==SCE_IME_DIALOG_BUTTON_ENTER?1:-1;}
void ui_ime_abort(void){if(active)sceImeDialogAbort();}
void ui_present(void){if(active){SceCommonDialogUpdateParam p={0};p.renderTarget.colorSurfaceData=fb[back].data;p.renderTarget.width=960;p.renderTarget.height=544;p.renderTarget.strideInPixels=STRIDE;p.renderTarget.colorFormat=SCE_GXM_COLOR_FORMAT_A8B8G8R8;p.renderTarget.surfaceType=SCE_GXM_COLOR_SURFACE_LINEAR;p.displaySyncObject=fb[back].sync;sceCommonDialogUpdate(&p);}sceGxmPadHeartbeat(&fb[back].surface,fb[back].sync);sceGxmDisplayQueueAddEntry(fb[front].sync,fb[back].sync,&fb[back].data);front=back;back=1-back;sceDisplayWaitVblankStart();}
void ui_platform_shutdown(void){if(!initialized)return;if(active){ui_ime_abort();sceImeDialogTerm();active=0;}sceGxmDisplayQueueFinish();for(int i=0;i<2;i++){sceGxmSyncObjectDestroy(fb[i].sync);sceGxmUnmapMemory(fb[i].data);sceKernelFreeMemBlock(fb[i].uid);}sceGxmTerminate();initialized=0;}

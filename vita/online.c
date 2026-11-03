#include "online.h"
#include "app_paths.h"
#include "dbg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
static volatile int mode=ONLINE_AUTO,initialized,network_verified;
static volatile int state,connection_checked,network_initializing;
static int health_thread_id=-1;
static int network_init(void);
static int online_rpc(const char*,const char*,RetrievalResult*,volatile int*,int);
static const char *states[]={"","Searching web...","Reading context...","Generating...","Web context used","PC companion unavailable","No useful web context","Source busy","Lookup unavailable","Answer incomplete","Using conversation","No usable web context; answering locally","Preparing lookup..."};
int online_mode(void){return mode;}
const char*online_mode_name(void){return mode?"ONLINE":"OFFLINE";}
const char*online_status(void){return states[state>=0&&state<13?state:0];}
int online_badge_state(void){
 if(!mode)return ONLINE_BADGE_OFFLINE;
 if(!connection_checked)return ONLINE_BADGE_CHECKING;
 if(!initialized)return ONLINE_BADGE_OFFLINE;
 int net=0;if(sceNetCtlInetGetState(&net)<0||net!=SCE_NETCTL_STATE_CONNECTED)return ONLINE_BADGE_OFFLINE;
 return network_verified?ONLINE_BADGE_ONLINE:ONLINE_BADGE_OFFLINE;
}
const char*online_badge_label(int badge){return badge==ONLINE_BADGE_ONLINE?"online":badge==ONLINE_BADGE_CHECKING?"checking":"offline";}
void online_load_settings(void){char path[160];snprintf(path,sizeof(path),"%s/mode.bin",app_data_dir());FILE*f=fopen(path,"rb");if(f){int value=fgetc(f);fclose(f);if(value>=0&&value<=2)mode=value?1:0;}}
void online_set_mode(int value){if(value<0||value>2)return;mode=value?1:0;state=0;char path[160];snprintf(path,sizeof(path),"%s/mode.bin",app_data_dir());FILE*f=fopen(path,"wb");if(f){fputc(value,f);fclose(f);}}
void online_cycle_mode(void){online_set_mode(!mode);}
void online_generating(int used){state=used?3:0;}
void online_complete(int used){state=used?4:0;}
void online_incomplete(void){state=9;}
void online_conversation(void){state=10;}
void online_failure(int insufficient){if(state!=7&&state!=8)state=insufficient?6:5;}
static char conversation_topic[129],conversation_facts[1025];
static unsigned conversation_epoch;
static int fresh_lookup=1;
void online_reset_context(void){memset(conversation_topic,0,sizeof(conversation_topic));memset(conversation_facts,0,sizeof(conversation_facts));fresh_lookup=1;conversation_epoch++;state=0;dbg_log("ONLINE_CONTEXT_RESET epoch=%u topic_bytes=0 facts_bytes=0 fresh_lookup=1",conversation_epoch);}
void online_fallback(void){state=11;}
void online_planning(void){state=12;}
void online_discard_reference(void){memset(conversation_topic,0,sizeof(conversation_topic));memset(conversation_facts,0,sizeof(conversation_facts));}
void online_remember_topic(const char*topic){if(strcmp(topic,conversation_topic))conversation_facts[0]=0;snprintf(conversation_topic,sizeof(conversation_topic),"%s",topic);}
const char*online_context_topic(void){return conversation_topic;}
void online_remember_facts(const char*facts){snprintf(conversation_facts,sizeof(conversation_facts),"%s",facts);}
const char*online_reference_notes(void){return conversation_facts;}
/* Every nonempty message is eligible; the companion classifies the requested action before lookup. */
int online_should_retrieve(const char*q){return mode!=0&&q&&*q;}
static unsigned u16(const unsigned char*p){return (unsigned)p[0]*256+p[1];}
static uint32_t u32(const unsigned char*p){return (uint32_t)u16(p)*65536+u16(p+2);}
static uint64_t u64(const unsigned char*p){return (uint64_t)u32(p)*4294967296ULL+u32(p+4);}
static int transfer(int s,void*buffer,unsigned n,int writing,uint64_t deadline,volatile int*cancel){
 unsigned char*p=buffer;
 while(n){if(*cancel)return -2;if(sceKernelGetProcessTimeWide()>deadline)return -1;
 int r=writing?sceNetSend(s,p,n,0):sceNetRecv(s,p,n,0);
 if(r>0){p+=r;n-=r;continue;}if(r==0)return -1;
 int error=*sceNetErrnoLoc();if(error!=SCE_NET_EWOULDBLOCK&&error!=SCE_NET_EINPROGRESS)return -1;
 sceKernelDelayThread(10000);
 }return 0;
}
static int network_init(void){
 while(__sync_lock_test_and_set(&network_initializing,1))sceKernelDelayThread(1000);
 if(initialized){__sync_lock_release(&network_initializing);return 0;}
 int rc=sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
 if(rc<0){__sync_lock_release(&network_initializing);return -1;}
 SceNetInitParam net={0};net.size=2*1024*1024;net.memory=malloc(net.size);
 if(!net.memory){__sync_lock_release(&network_initializing);return -1;}
 rc=sceNetInit(&net);
 if(rc<0){free(net.memory);__sync_lock_release(&network_initializing);return -1;}
 sceNetCtlInit();initialized=1;__sync_lock_release(&network_initializing);return 0;
}
static int online_rpc(const char*query,const char*message,RetrievalResult*out,volatile int*cancel,int health){
 memset(out,0,sizeof(*out));unsigned nq=strlen(query),nm=message?strlen(message):0,length=health?0:nq;
 unsigned char payload[1670];const void*data=query;
 if(!health&&(!nq||nq>512||!nm||nm>1024))return -1;
 if(!health){unsigned nt=strlen(online_context_topic());if(nt>128)return -1;
  length=6+nm+nq+nt;payload[0]=nm>>8;payload[1]=nm;payload[2]=nq>>8;payload[3]=nq;payload[4]=nt>>8;payload[5]=nt;
  memcpy(payload+6,message,nm);memcpy(payload+6+nm,query,nq);memcpy(payload+6+nm+nq,online_context_topic(),nt);data=payload;}

 char path[160],ip[20]={0};snprintf(path,sizeof(path),"%s/host.txt",app_data_dir());FILE*f=fopen(path,"r");
 if(!f)return -1;if(!fgets(ip,sizeof(ip),f)){fclose(f);return -1;}fclose(f);ip[strcspn(ip,"\r\n ")]=0;
 if(network_init()<0)return -1;
 SceNetSockaddrIn address={0};address.sin_len=sizeof(address);address.sin_family=SCE_NET_AF_INET;address.sin_port=sceNetHtons(28500);
 if(sceNetInetPton(SCE_NET_AF_INET,ip,&address.sin_addr)!=1){state=5;return -1;}
 unsigned a=sceNetNtohl(address.sin_addr.s_addr);if((a>>24)!=10&&(a>>20)!=0xac1&&(a>>16)!=0xc0a8){state=5;return -1;}
 if(!health)state=1;uint64_t start=sceKernelGetProcessTimeWide(),deadline=start+(health?4000000:45000000);
 int s=sceNetSocket("Typhoon retrieval",SCE_NET_AF_INET,SCE_NET_SOCK_STREAM,0);if(s<0){state=5;return -1;}
 int yes=1;sceNetSetsockopt(s,SCE_NET_SOL_SOCKET,SCE_NET_SO_NBIO,&yes,sizeof(yes));
 int ep=sceNetEpollCreate("Typhoon connect",0),result=-1;
 if(ep<0)goto done;
 SceNetEpollEvent event={0};event.events=SCE_NET_EPOLLOUT|SCE_NET_EPOLLERR|SCE_NET_EPOLLHUP;
 if(sceNetEpollControl(ep,SCE_NET_EPOLL_CTL_ADD,s,&event)<0)goto done;
 int connected=sceNetConnect(s,(SceNetSockaddr*)&address,sizeof(address));
 if(connected<0){int error=*sceNetErrnoLoc();if(error!=SCE_NET_EWOULDBLOCK&&error!=SCE_NET_EINPROGRESS)goto done;
 while(1){if(*cancel){result=-2;goto done;}if(sceKernelGetProcessTimeWide()>deadline)goto done;
 SceNetEpollEvent ready={0};int n=sceNetEpollWait(ep,&ready,1,10000);if(n>0){int error=0;unsigned size=sizeof(error);sceNetGetsockopt(s,SCE_NET_SOL_SOCKET,SCE_NET_SO_ERROR,&error,&size);if(error)goto done;break;}sceKernelDelayThread(10000);}}
 int requested_fresh=!health&&fresh_lookup;
 unsigned char request[8]={'T','Y',health?'H':'B',health?'P':requested_fresh?'N':'Q',0,2,(unsigned char)(length>>8),(unsigned char)length};
 if(!health)dbg_log("ONLINE_REQUEST epoch=%u fresh=%d topic_bytes=%u facts_bytes=%u",conversation_epoch,requested_fresh,(unsigned)strlen(conversation_topic),(unsigned)strlen(conversation_facts));
 result=transfer(s,request,8,1,deadline,cancel);if(result)goto done;
 result=transfer(s,(void*)data,length,1,deadline,cancel);if(result)goto done;
 if(!health)state=2;unsigned char header[32];result=transfer(s,header,32,0,deadline,cancel);if(result)goto done;
 unsigned status=u16(header+6),nt=u16(header+8),ns=u16(header+10),ne=u32(header+12);
 if(memcmp(header,"TYRS",4)||u16(header+4)!=2||status>6||nt>128||ns>384||ne>1024){result=-1;goto done;}
 if(!ne&&status==0){result=-1;goto done;}
 result=transfer(s,out->title,nt,0,deadline,cancel);if(result)goto done;
 result=transfer(s,out->source,ns,0,deadline,cancel);if(result)goto done;
 result=transfer(s,out->evidence,ne,0,deadline,cancel);if(result)goto done;
 if(memchr(out->title,0,nt)||memchr(out->source,0,ns)||memchr(out->evidence,0,ne)){result=-1;goto done;}
 if(requested_fresh&&status!=4){fresh_lookup=0;dbg_log("ONLINE_FRESH_LOOKUP_ACK epoch=%u status=%u",conversation_epoch,status);}
 if(status==6&&!health){state=0;result=RETRIEVAL_LOCAL;goto done;}
 if(status!=0){if(!health)state=status==5?7:status==6?8:status==1||status==2?6:5;result=status==3||status==4?-1:-3;goto done;}
 out->raw_bytes=u32(header+24);out->elapsed_ms=u32(header+28);out->timestamp=u64(header+16);
 for(unsigned i=0;i<ne;i++){unsigned char c=out->evidence[i];if(c<32&&c!='\n'&&c!='\t')out->evidence[i]=' ';if(c=='<'||c=='>')out->evidence[i]=' ';}
 if(!health)dbg_log("ONLINE_RETRIEVAL query=%s title=%s source=%s raw_bytes=%u evidence_bytes=%u server_ms=%u total_us=%llu",query,out->title,out->source,out->raw_bytes,ne,out->elapsed_ms,(unsigned long long)(sceKernelGetProcessTimeWide()-start));
 if(!health){network_verified=1;connection_checked=1;}result=0;
done:
 if(ep>=0)sceNetEpollDestroy(ep);
 sceNetSocketClose(s);
 if(!health&&result==-1){state=5;network_verified=0;connection_checked=1;}
 if(!health&&result==-2)state=0;
 if(!health&&result<0)dbg_log("ONLINE_RETRIEVAL_FAILED result=%d errno=%d",result,*sceNetErrnoLoc());
 return result;
}

int retrieval_query(const char*q,const char*message,RetrievalResult*out,volatile int*cancel){return online_rpc(q,message,out,cancel,0);}
static int health_worker(SceSize args,void*argp){
 (void)args;(void)argp;int prior=-1;
 for(;;){
  if(mode){RetrievalResult response;volatile int cancel=0;int ok=online_rpc("",0,&response,&cancel,1)==0;
   network_verified=ok;connection_checked=1;if(ok&&state==5)state=0;
   if(ok!=prior){dbg_log("ONLINE_CONNECTIVITY connected=%d",ok);prior=ok;}
  }
  sceKernelDelayThread(10000000);
 }
 return 0;
}
void online_start(void){
 if(health_thread_id>=0)return;
 health_thread_id=sceKernelCreateThread("Typhoon connectivity",health_worker,0x10000100,32768,0,0,0);
 dbg_log("ONLINE_HEALTH_THREAD create=%x",health_thread_id);
 if(health_thread_id>=0){int rc=sceKernelStartThread(health_thread_id,0,0);dbg_log("ONLINE_HEALTH_THREAD start=%x",rc);if(rc<0){connection_checked=1;network_verified=0;}}
 else{connection_checked=1;network_verified=0;}
}

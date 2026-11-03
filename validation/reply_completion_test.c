#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "session.h"
#include "profile.h"
uint64_t prof_us[31][P_COUNT],prof_layer[30],prof_forward_us,prof_calls,prof_batch_calls;
static float scores[8];static int script[512],script_n,index_;
void dbg_log(const char*f,...){(void)f;}
int engine_get_context(const Transformer*t){(void)t;return 512;}
void engine_reset_context(Transformer*t){(void)t;index_=0;}
static float *next(void){for(int i=0;i<8;i++)scores[i]=-1;scores[index_<script_n?script[index_]:7]=1;return scores;}
float *engine_prefill(Transformer*t,int token,int p,int need){(void)t;(void)token;(void)p;(void)need;return next();}
float *engine_prefill_batch4(Transformer*t,const int*x,int p,int need){(void)t;(void)x;(void)p;(void)need;return next();}
float *engine_forward(Transformer*t,int token,int p){(void)t;(void)token;(void)p;index_++;return next();}
char *engine_decode(Tokenizer*t,int id){(void)t;static char*v[]={"two words ","ends."," next","```\n","\n```\n","<end>","<start>","<eos>"};return v[id];}
static void run(Session*s){while(s->reason==END_NONE)assert(session_step(s)>=0);}
int main(void){
 Transformer m={0};m.config.vocab_size=8;Tokenizer t={0};t.eos_id=7;t.im_start_id=6;t.im_end_id=5;int prompt[400]={0};Session s;
 for(int i=0;i<192;i++)script[i]=0;script_n=192;
 session_init(&s,&m,&t);assert(!session_start(&s,prompt,381,129,128,0));run(&s);assert(s.reason==END_HARD);assert(!session_continue(&s,129,128));run(&s);assert(s.reason==END_CONTEXT&&s.total_count==129);
 session_init(&s,&m,&t);assert(!session_start(&s,prompt,318,129,128,0));run(&s);assert(s.reason==END_HARD);assert(!session_continue(&s,65,64));run(&s);assert(s.reason==END_EOS&&s.total_count==192&&s.pos==510);
 script[0]=0;script[1]=1;script[2]=2;script_n=3;session_init(&s,&m,&t);assert(!session_start(&s,prompt,20,2,20,0));run(&s);assert(s.reason==END_SOFT&&s.count==2);
 script[0]=3;script[1]=0;script[2]=1;script[3]=2;script_n=4;session_init(&s,&m,&t);assert(!session_start(&s,prompt,20,2,20,0));run(&s);assert(s.reason==END_EOS&&s.count==4);
 script[0]=3;script[1]=0;script[2]=1;script[3]=4;script[4]=0;script[5]=1;script[6]=2;script_n=7;session_init(&s,&m,&t);assert(!session_start(&s,prompt,20,2,20,0));run(&s);assert(s.reason==END_SOFT&&s.count==6);
 puts("5 actual-session context/completion controls passed");return 0;
}

/* Single live LFM session. Attention KV AND engine-owned convolution state
 * remain in place. A prefix mismatch resets both, never just the attention KV.
 * pos always counts tokens actually forwarded; pending is sampled but un-fed. */
#include "session.h"
#include "profile.h"
#include "dbg.h"
static int s_prefill_head_skip=1;
void session_set_prefill_head_skip(int enabled){s_prefill_head_skip=enabled!=0;}
#include <string.h>
#include <ctype.h>
int engine_batch_prepare(Transformer*);
float*engine_forward_batch4(Transformer*,const int*,int);
static int greedy(Session*s,const float*x){int best=0;TIMED(30,P_SAMPLE,for(int i=1;i<s->model->config.vocab_size;i++)if(x[i]>x[best])best=i;);return best;}
static int eos(Session*s,int id){Tokenizer*t=s->tokenizer;return id==t->eos_id||id==t->im_end_id||id==t->im_start_id;}
static int capacity(Session*s){int n=engine_get_context(s->model);return n<SESSION_CONTEXT?n:SESSION_CONTEXT;}
void session_init(Session*s,Transformer*m,Tokenizer*t){memset(s,0,sizeof(*s));s->model=m;s->tokenizer=t;}
void session_reset(Session*s){engine_reset_context(s->model);s->pos=0;s->has_pending=0;s->reason=END_NONE;s->text[0]=0;s->total_count=0;}
static void budget(Session*s,int soft,int hard){s->hard=hard<1?1:(hard>256?256:hard);s->soft=soft<1?s->hard:soft;if(s->soft>s->hard)s->soft=s->hard;s->count=0;s->reason=END_NONE;}
int session_start(Session*s,const int*ids,int n,int soft,int hard,int reuse){
 if(n<1||n>=capacity(s))return -1;
 for(int i=0;i<n;i++)if(ids[i]<0||ids[i]>=s->model->config.vocab_size)return -2;
 int match=reuse&&s->pos>0&&s->pos<n&&!memcmp(s->fed,ids,s->pos*sizeof(int));
 s->reused=match?s->pos:0;s->rebuilt=!match;
 if(!match)session_reset(s);
 s->text[0]=0;s->total_count=0;s->has_pending=0;budget(s,soft,hard);
 float*logits=0;int head_evals=0,head_skips=0,start_pos=s->pos;
 while(s->pos<n){if(s->cancelled&&s->cancelled(s->emit_user)){s->reason=END_STOP;return -4;}int p=s->pos;
  if(p+4<=n){int need=!s_prefill_head_skip||p+4==n;head_evals+=need;head_skips+=!need;logits=engine_prefill_batch4(s->model,ids+p,p,need);if(!logits)return -3;memcpy(s->fed+p,ids+p,4*sizeof(int));s->pos+=4;}
  else{int need=!s_prefill_head_skip||p+1==n;head_evals+=need;head_skips+=!need;logits=engine_prefill(s->model,ids[p],p,need);if(!logits)return -3;s->fed[s->pos++]=ids[p];}
 }
 dbg_log("PREFILL_HEAD_OPT enabled=%d forwarded=%d head_evals=%d head_skips=%d",s_prefill_head_skip,n-start_pos,head_evals,head_skips);
 s->pending=greedy(s,logits);s->has_pending=1;return 0;
}
int session_can_continue(const Session*s){return s->has_pending&&(s->reason==END_HARD||s->reason==END_SOFT||s->reason==END_STOP||s->reason==END_BUFFER)&&s->pos<SESSION_CONTEXT-2&&strlen(s->text)<SESSION_TEXT-256;}
int session_continue(Session*s,int soft,int hard){if(!session_can_continue(s))return -1;budget(s,soft,hard);s->reused=s->pos;s->rebuilt=0;return 0;}
/* Boundary is confirmed against the following piece. This avoids ending at a
 * decimal point or within a word. Known abbreviations/initials are excluded.
 * Conservative boundaries may run to the hard cap; Continue remains exact. */
static int complete(const char*text,const char*next){
 size_t n=strlen(text);while(n&&isspace((unsigned char)text[n-1]))n--;
 if(!n||!strchr(".!?",text[n-1]))return 0;
 size_t line=n;while(line&&text[line-1]!='\n')line--;int alpha_words=0,in_alpha=0;
 for(size_t i=line;i<n;i++){if(isalpha((unsigned char)text[i])){if(!in_alpha)alpha_words++;in_alpha=1;}else in_alpha=0;}
 if(alpha_words<2)return 0; /* Never treat a fresh list marker such as "3." as a sentence. */
 if(*next&&!isspace((unsigned char)*next))return 0;
 const char*p=next;while(isspace((unsigned char)*p))p++;if(isdigit((unsigned char)*p))return 0;
 int words=0,in=0;for(size_t i=0;i<n;i++){if(isalnum((unsigned char)text[i])){if(!in)words++;in=1;}else in=0;}if(words<3)return 0;
 if(text[n-1]=='.'){
  size_t a=n-1;while(a&&isalpha((unsigned char)text[a-1]))a--;
  size_t len=n-a;char last[16];if(len<sizeof(last)){memcpy(last,text+a,len);last[len]=0;for(char*q=last;*q;q++)*q=tolower((unsigned char)*q);
   const char*abbr[]={"mr.","mrs.","ms.","dr.","prof.","st.","vs.","etc.","e.g.","i.e."};
   if(len==2)return 0;for(unsigned i=0;i<sizeof(abbr)/sizeof(*abbr);i++)if(!strcmp(last,abbr[i]))return 0;
  }
 }
 return 1;
}
int session_step(Session*s){
 if(s->reason!=END_NONE)return 0;if(!s->has_pending){s->reason=END_ERROR;return -1;}
 int next=s->pending;const char*piece=engine_decode(s->tokenizer,next);
 if(eos(s,next)){s->reason=END_EOS;return 0;}
 int fences=0;for(const char*p=s->text;(p=strstr(p,"```"));p+=3)fences++;
 if(!(fences%2)&&s->count>=s->soft&&complete(s->text,piece)){s->reason=END_SOFT;return 0;}
 if(s->count>=s->hard){s->reason=END_HARD;return 0;}
 if(s->pos>=capacity(s)-2){s->reason=END_CONTEXT;return 0;} /* Reserve im_end + newline. */
 size_t used=strlen(s->text),len=strlen(piece);
 if(used+len>=sizeof(s->text)){s->reason=END_BUFFER;return 0;}
 memcpy(s->text+used,piece,len+1);s->generated[s->count++]=next;s->total_count++;
 if(s->emit)s->emit(s->text,s->emit_user);
 /* Forward emitted token immediately, preserving a valid resumable state even
  * at a hard limit. No generated token is fed twice on Continue. */
 s->fed[s->pos]=next;float*logits=engine_forward(s->model,next,s->pos++);
 if(!logits){s->reason=END_ERROR;return -1;}
 s->pending=greedy(s,logits);s->has_pending=1;return 1;
}
void session_stop(Session*s){if(s->reason==END_NONE)s->reason=END_STOP;}
const char*session_reason(EndReason r){static const char*v[]={"running","eos","soft_sentence","user_stop","hard_limit","context_exhaustion","output_buffer","error","local_result"};return r>=END_NONE&&r<=END_LOCAL?v[r]:"error";}

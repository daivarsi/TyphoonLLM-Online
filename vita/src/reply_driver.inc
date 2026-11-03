/* Logical response remains one RUNNING job. Do not reset cancellation, clear
 * publication, re-tokenize text, or sample again at a segment boundary. */
#include "reply_policy.h"
static int s_auto_reply=1;
static char s_completion[32]="Ready";
void llama_worker_set_auto(int on){if(s_state!=LLAMA_WORKER_RUNNING)s_auto_reply=!!on;}
const char*llama_worker_completion(void){return s_completion;}
static void generate_reply(const char*prompt){
 int soft=s_soft_job,hard=s_hard_job,total=0,segments=0;
 snprintf(s_completion,sizeof(s_completion),"Generating");
 do {
  int remaining=REPLY_TOKEN_LIMIT-total;
  s_hard_job=hard<remaining?hard:remaining;
  if(s_hard_job<1)s_hard_job=1;
  s_soft_job=soft<s_hard_job?soft:s_hard_job;
  generate_to_buffer(prompt);segments++;total+=s_session.count;
  /* Structured-answer policy may raise soft to hard for this response. */
  soft=s_soft_job;
  dbg_log("REPLY_SEGMENT segment=%d tokens=%d total=%d reason=%s cancel=%d",segments,s_session.count,total,session_reason(s_session.reason),s_cancel);
  if(s_state==LLAMA_WORKER_ERROR)break;
  if(!reply_resume(s_auto_reply,s_cancel,s_session.reason,total,segments))break;
  s_continue_job=1;
 }while(1);
 const char*end=s_state==LLAMA_WORKER_ERROR?"Error":s_cancel?"Stopped":
  s_session.reason==END_HARD?"Response limit":s_session.reason==END_CONTEXT?"Context limit":
  s_session.reason==END_BUFFER?"Text limit":s_session.reason==END_ERROR?"Error":"Completed";
 snprintf(s_completion,sizeof(s_completion),"%s",end);
 dbg_log("REPLY_COMPLETE status=%s segments=%d tokens=%d auto=%d",end,segments,total,s_auto_reply);
 if(!s_cancel&&s_state!=LLAMA_WORKER_ERROR)search_plan_remember(prompt,s_session.text);
 s_soft_job=soft;s_hard_job=hard;s_continue_job=0;
}

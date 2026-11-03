#include "profile.h"
#include "dbg.h"
#include <string.h>
uint64_t prof_us[31][P_COUNT],prof_layer[30],prof_forward_us,prof_calls,prof_batch_calls;
static const char *names[P_COUNT]={"embedding","rmsnorm","quantize","q","k","v","rope","kv_write_copy","attention_score_kv_read","softmax","attention_value_kv_read_zero","attention_out","gate","up","activation","down","residual","final_norm","logits","tokenizer","sampler","context_reset","batch_copy","conv_in","conv_gate_depthwise","conv_out","qk_norm"};
void prof_reset(void){memset(prof_us,0,sizeof(prof_us));memset(prof_layer,0,sizeof(prof_layer));prof_forward_us=prof_calls=prof_batch_calls=0;}
void prof_dump(void){
 dbg_log("PROFILE total_forward_us=%llu positions=%llu single_calls=%llu batch4_calls=%llu",(unsigned long long)prof_forward_us,(unsigned long long)prof_calls,(unsigned long long)(prof_calls-4*prof_batch_calls),(unsigned long long)prof_batch_calls);
 for(int l=0;l<31;l++){
  if(l<30)dbg_log("PROFILE layer=%d total_us=%llu",l,(unsigned long long)prof_layer[l]);
  for(int o=0;o<P_COUNT;o++)if(prof_us[l][o])dbg_log("PROFILE layer=%d op=%s us=%llu",l,names[o],(unsigned long long)prof_us[l][o]);
 }
}

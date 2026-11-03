#pragma once
#include <stdint.h>
#include <psp2/kernel/processmgr.h>
enum { P_EMBED,P_NORM,P_QUANT,P_Q,P_K,P_V,P_ROPE,P_KV,P_SCORE,P_SOFTMAX,P_VALUE,P_OUT,P_GATE,P_UP,P_ACT,P_DOWN,P_RESID,P_FINAL,P_LOGITS,P_TOKENIZER,P_SAMPLE,P_RESET,P_COPY,P_CONV_IN,P_CONV_STEP,P_CONV_OUT,P_QKNORM,P_COUNT };
extern uint64_t prof_us[31][P_COUNT], prof_layer[30], prof_forward_us, prof_calls, prof_batch_calls;
void prof_reset(void);
void prof_dump(void);
#define TIMED(L,O,...) do { uint64_t pt=sceKernelGetProcessTimeWide(); __VA_ARGS__; prof_us[L][O]+=sceKernelGetProcessTimeWide()-pt; } while(0)

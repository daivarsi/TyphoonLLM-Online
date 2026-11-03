#ifndef VITALM_SESSION_H
#define VITALM_SESSION_H
#include "llama_engine.h"
#define SESSION_CONTEXT 512
#define SESSION_TEXT 8192
typedef enum { END_NONE, END_EOS, END_SOFT, END_STOP, END_HARD, END_CONTEXT, END_BUFFER, END_ERROR, END_LOCAL } EndReason;
typedef struct {
 Transformer *model; Tokenizer *tokenizer;
 int fed[SESSION_CONTEXT], pos, pending, has_pending;
 int generated[SESSION_CONTEXT], count, total_count, soft, hard, reused, rebuilt;
 char text[SESSION_TEXT]; EndReason reason;
 void (*emit)(const char*,void*); void *emit_user;
 int (*cancelled)(void*);
} Session;
void session_set_prefill_head_skip(int enabled);
void session_init(Session*,Transformer*,Tokenizer*);
void session_reset(Session*);
int session_start(Session*,const int*,int,int,int,int);
int session_continue(Session*,int,int);
int session_step(Session*);
void session_stop(Session*);
const char*session_reason(EndReason);
int session_can_continue(const Session*);
#endif

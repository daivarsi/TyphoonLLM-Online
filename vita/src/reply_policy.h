#pragma once
#include "session.h"
#define REPLY_TOKEN_LIMIT 192
#define REPLY_SEGMENT_LIMIT 8
/* EOS, sentence completion, Stop, and exhausted context are terminal. Keeping
 * context exhaustion terminal avoids automatic destructive history eviction. */
static inline int reply_resume(int enabled,int cancel,EndReason reason,int total,int segments){
 return enabled&&!cancel&&reason==END_HARD&&total<REPLY_TOKEN_LIMIT&&segments<REPLY_SEGMENT_LIMIT;
}

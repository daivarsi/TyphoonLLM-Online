/*
 * WoozyLLM Vita - asynchronous llama2.c worker.
 */

#ifndef WOOZYLLM_LLAMA_WORKER_H
#define WOOZYLLM_LLAMA_WORKER_H

#define LLAMA_MODEL_PATH "ux0:data/TyphoonOnline/model.vlp"
#define LLAMA_TOKENIZER_PATH "ux0:data/TyphoonOnline/tokenizer.bin"
#define LLAMA_RESPONSE_MAX 8192

enum llama_worker_state {
    LLAMA_WORKER_EMPTY = 0,
    LLAMA_WORKER_READY,
    LLAMA_WORKER_RUNNING,
    LLAMA_WORKER_DONE,
    LLAMA_WORKER_ERROR
};

int llama_worker_init(void);
int llama_worker_start(const char *prompt);
void llama_worker_stop(void);
enum llama_worker_state llama_worker_get_state(void);
const char *llama_worker_get_response(void);
const char *llama_worker_get_error(void);
const char *llama_worker_get_model_info(void);
void llama_worker_shutdown(void);

int llama_worker_start_chat(const char*,const char*,int);
void llama_worker_statistics(char*,int);
void llama_worker_prepare_prefix(void);
int llama_worker_request(const char*,int,int,int,int);
int llama_worker_can_continue(void);
int llama_worker_continue(void);
void llama_worker_reset(void);
void llama_worker_set_reuse(int);
const char*llama_worker_end_reason(void);
void llama_worker_set_auto(int);
const char*llama_worker_completion(void);
#endif /* WOOZYLLM_LLAMA_WORKER_H */

void llama_worker_set_evidence_pack(int enabled);

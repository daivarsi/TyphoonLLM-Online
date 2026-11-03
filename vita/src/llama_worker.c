/*
 * WoozyLLM Vita - asynchronous Q8_0 inference worker.
 *
 * Loads the SmolLM Q8_0 model (v2 format) from ux0:data/woozyllm/model.bin and
 * runs generation in a worker thread using the llama_engine.c backend. Each
 * prompt is rendered with the SmolLM chat template and answered statelessly.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <malloc.h>

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/stat.h>

#include "dbg.h"
#include "llama_engine.h"
#include "profile.h"
#include "llama_worker.h"
#include "../app_paths.h"
#include "../online.h"
int engine_batch_prepare(Transformer *t);
float *engine_forward_batch4(Transformer *t,const int tokens[4],int pos);

#define LLAMA_STEPS 96
#define LLAMA_THREAD_STACK (512 * 1024)
#define LLAMA_THREAD_PRIORITY 0x10000100
#define LLAMA_PROMPT_MAX 4096      /* roomy: template + user text < 4 KiB */
#define LLAMA_MAX_MODEL_BYTES (1024u * 1024u * 1024u)

/* ChatML control-token IDs are resolved from tokenizer.bin at load time
 * (they are NOT guaranteed to be 0/1/2). These variables cache them. */
static int g_eos_id = 0;
static int g_im_start_id = 1;
static int g_im_end_id = 2;

static Transformer s_transformer;
static Tokenizer s_tokenizer;
static int s_initialized;
static SceUID s_thread_id = -1;
static SceUID s_jobs=-1;
static volatile int s_quit;
static volatile enum llama_worker_state s_state = LLAMA_WORKER_EMPTY;
static char s_prompt[LLAMA_PROMPT_MAX];
static int s_used_context;
static volatile int s_cancel;
static double s_ttft,s_decode;
static char s_response[LLAMA_RESPONSE_MAX];
static char s_error[192];
static char s_model_info[96];
static SceUID s_lock = -1; /* guards s_response / s_error across threads */

static void free_transformer_from_malloc(Transformer *transformer);

static void lock_io(void)
{
    if (s_lock >= 0)
        sceKernelLockMutex(s_lock, 1, NULL);
}

static void unlock_io(void)
{
    if (s_lock >= 0)
        sceKernelUnlockMutex(s_lock, 1);
}

static void set_error(const char *message)
{
    lock_io();
    snprintf(s_error, sizeof(s_error), "%s", message ? message : "Unknown llama error");
    unlock_io();
    s_state = LLAMA_WORKER_ERROR;
    dbg_log("llama_worker: %s", s_error);
}

static void ensure_data_dir(void)
{
    sceIoMkdir(app_data_dir(), 0777);
}

static int load_file_to_memory(const char *path, void **out_data, size_t *out_size, int *out_uid,
                               int *out_alloc_rc)
{
    FILE *file;
    long file_size;
    void *data;
    size_t aligned;
    SceUID uid;

    *out_data = NULL;
    *out_size = 0;
    if (out_uid)
        *out_uid = -1;
    if (out_alloc_rc)
        *out_alloc_rc = 0;

    file = fopen(path, "rb");
    if (!file)
        return -1;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -2;
    }

    file_size = ftell(file);
    if (file_size <= 0) {
        fclose(file);
        return -3;
    }

    if ((unsigned long)file_size > LLAMA_MAX_MODEL_BYTES) {
        fclose(file);
        return -7;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -4;
    }

    *out_size = (size_t)file_size;

    /* Allocate the model buffer outside the newlib heap so a single large
     * contiguous block is obtained directly from the system pool. */
    aligned = ((size_t)file_size + 0xFFFu) & ~(size_t)0xFFFu;
    uid = sceKernelAllocMemBlock("woozyllm_model", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                 (SceSize)aligned, NULL);
    if (uid < 0) {
        if (out_alloc_rc)
            *out_alloc_rc = (int)uid;
        fclose(file);
        return -5;
    }

    if (sceKernelGetMemBlockBase(uid, &data) < 0 || !data) {
        sceKernelFreeMemBlock(uid);
        fclose(file);
        return -6;
    }

    if (fread(data, 1, (size_t)file_size, file) != (size_t)file_size) {
        sceKernelFreeMemBlock(uid);
        fclose(file);
        return -6;
    }

    fclose(file);
    *out_data = data;
    *out_size = (size_t)file_size;
    if (out_uid)
        *out_uid = (int)uid;
    return 0;
}

static int build_transformer_from_malloc(Transformer *transformer, const char *checkpoint_path)
{
    void *model_data;
    size_t model_size;
    int shared;
    int ret;
    int data_uid = -1;
    int alloc_rc = 0;

    memset(transformer, 0, sizeof(*transformer));
    transformer->data_uid = -1;

    ret = load_file_to_memory(checkpoint_path, &model_data, &model_size, &data_uid, &alloc_rc);
    if (ret < 0) {
        if (ret == -7) {
            snprintf(s_error, sizeof(s_error), "Model is over %u MB",
                     LLAMA_MAX_MODEL_BYTES / 1024u / 1024u);
        } else if (ret == -1) {
            snprintf(s_error, sizeof(s_error),
                     "Model not found: %s (copy the .bin to ux0:data/woozyllm/)",
                     checkpoint_path);
        } else if (ret == -5) {
            SceKernelFreeMemorySizeInfo mem;
            memset(&mem, 0, sizeof(mem));
            mem.size = sizeof(mem);
            if (sceKernelGetFreeMemorySize(&mem) < 0)
                mem.size_user = -1;
            snprintf(s_error, sizeof(s_error),
                     "Out of memory for model (%s, %.1f MB); alloc=0x%08X free_user=%d MB",
                     checkpoint_path, (double)model_size / (1024.0 * 1024.0),
                     (unsigned int)alloc_rc, mem.size_user);
        } else {
            snprintf(s_error, sizeof(s_error), "Cannot load %s (%d)", checkpoint_path, ret);
        }
        return ret;
    }

    transformer->data = model_data;
    transformer->data_uid = data_uid;

    ret = engine_parse_header(model_data, model_size, &transformer->config, &shared);
    if (ret < 0) {
        free_transformer_from_malloc(transformer);
        snprintf(s_error, sizeof(s_error), "Unsupported VitaLM model container (%d)", ret);
        return -10;
    }

    engine_map_weights(&transformer->weights, &transformer->config,
                       (char *)model_data + 256, (uint8_t)shared);
    if (engine_malloc_run_state(&transformer->state, &transformer->config) < 0) {
        free_transformer_from_malloc(transformer);
        snprintf(s_error, sizeof(s_error), "Cannot allocate llama run state");
        return -11;
    }

    transformer->file_size = model_size;
    return 0;
}

static void free_transformer_from_malloc(Transformer *transformer)
{
    if (transformer->data) {
        if (transformer->data_uid >= 0)
            sceKernelFreeMemBlock((SceUID)transformer->data_uid);
        else
            free(transformer->data);
        transformer->data = NULL;
        transformer->data_uid = -1;
    }
    engine_free_weights(&transformer->weights);
    engine_free_run_state(&transformer->state);
}

/* Neutral Q&A system prompt: WoozyLLM identity, strict 1-2 sentence length,
 * a direct-answer instruction and an explicit turn-end / no-echo rule.
 * Deliberately free of office/service/helpdesk trigger words so SmolLM2
 * never drifts into support roleplay. */

#include "phase4_generation.inc"

static void generate_to_buffer(const char*prompt){phase4_generate(prompt);}

#include "reply_driver.inc"

static int worker_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;

    for(;;){
    sceKernelWaitSema(s_jobs,1,NULL);
    if(s_quit)break;
    lock_io();
    s_response[0] = 0;
    unlock_io();
    generate_reply(s_prompt);

    if (s_state == LLAMA_WORKER_RUNNING) {
        lock_io();
        if (!s_response[0])
            snprintf(s_response, sizeof(s_response), "(no response)");
        unlock_io();
        __sync_synchronize();
        s_state = LLAMA_WORKER_DONE;
    }

    }
    return 0;
}

int llama_worker_init(void)
{
    int ret;

    if (s_initialized)
        return 0;

    ensure_data_dir();

    s_state = LLAMA_WORKER_EMPTY;
    s_error[0] = 0;

    if (s_lock < 0)
        s_lock = sceKernelCreateMutex("woozyllm_worker_lock", 0, 0, NULL);

    const char *model_path=LLAMA_MODEL_PATH;
    dbg_log("MODEL path=%s",model_path);
    ret = build_transformer_from_malloc(&s_transformer, model_path);
    if (ret < 0) {
        s_state = LLAMA_WORKER_ERROR;
        dbg_log("llama_worker_init: %s", s_error);
        return ret;
    }

    ret = engine_build_tokenizer(&s_tokenizer, LLAMA_TOKENIZER_PATH,
                                 s_transformer.config.vocab_size);
    if (ret < 0) {
        snprintf(s_error, sizeof(s_error), "Cannot load %s (%d)", LLAMA_TOKENIZER_PATH, ret);
        free_transformer_from_malloc(&s_transformer);
        s_state = LLAMA_WORKER_ERROR;
        dbg_log("llama_worker_init: %s", s_error);
        return -20;
    }

    /* parse_special = 1: resolve the real ChatML control-token IDs for THIS
     * tokenizer.bin instead of assuming fixed 0/1/2 slots. They gate both
     * stop-detection and (via engine_encode) correct prompt tokenization.
     * engine_encode() already honours them: it scans the prompt for the
     * literal tags and emits these single IDs (frozen from BPE merges), so
     * the model ALWAYS sees real control tokens, never split-up text.      */
    {
        int eos = 0, istart = 1, iend = 2;
        engine_tokenizer_special(&s_tokenizer, &eos, &istart, &iend);
        g_eos_id      = eos;
        g_im_start_id = istart;
        g_im_end_id   = iend;

        /* GUARD: verify the resolved IDs really round-trip to the tags.
         * If tokenizer.bin lacks them, engine_tokenizer_special silently
         * falls back to the last vocab slots and the whole ChatML boundary
         * would be garbage - fail loudly instead of silently continuing
         * the user's text. */
        if (g_eos_id < 0 || g_im_start_id < 0 || g_im_end_id < 0 ||
            strcmp(engine_decode(&s_tokenizer, g_eos_id), "<|endoftext|>") != 0 ||
            strcmp(engine_decode(&s_tokenizer, g_im_start_id), "<|im_start|>") != 0 ||
            strcmp(engine_decode(&s_tokenizer, g_im_end_id), "<|im_end|>") != 0) {
            snprintf(s_error, sizeof(s_error),
                     "tokenizer.bin lacks ChatML control tokens "
                     "(eos=%d im_start=%d im_end=%d); rebuild it with "
                     "build_tokenizer.py", g_eos_id, g_im_start_id, g_im_end_id);
            engine_free_tokenizer(&s_tokenizer);
            free_transformer_from_malloc(&s_transformer);
            s_state = LLAMA_WORKER_ERROR;
            dbg_log("llama_worker_init: %s", s_error);
            return -22;
        }

        dbg_log("llama_worker: ChatML ids eos=%d im_start=%d im_end=%d",
                g_eos_id, g_im_start_id, g_im_end_id);
    }

    /* Session uses exact greedy argmax; no unused sampler allocation. */
    snprintf(s_model_info, sizeof(s_model_info),
             "LFM2.5: dim=%d L%d hdr=%d kv=%d v%d seq%d %dMB",
             s_transformer.config.dim, s_transformer.config.n_layers,
             s_transformer.config.n_heads, s_transformer.config.n_kv_heads,
             s_transformer.config.vocab_size, s_transformer.config.seq_len,
             (int)(s_transformer.file_size / (1024 * 1024)));
    dbg_log("%s", s_model_info);

    if(engine_batch_prepare(&s_transformer)<0)dbg_log("BATCH allocation unavailable; single-position prefill retained");
    s_initialized = 1;
    s_state = LLAMA_WORKER_READY;
    dbg_log("llama_worker_init: model loaded");
    return 0;
}

int llama_worker_start(const char*prompt){
 if(!s_initialized||s_state==LLAMA_WORKER_RUNNING)return -1;
 if(s_thread_id<0){
  s_jobs=sceKernelCreateSema("VitaLM jobs",0,0,1,NULL);
  if(s_jobs<0){set_error("Cannot create generation queue");return s_jobs;}
  s_quit=0;s_thread_id=sceKernelCreateThread("VitaLM generation",worker_thread,LLAMA_THREAD_PRIORITY,LLAMA_THREAD_STACK,0,0,NULL);
  if(s_thread_id<0){int rc=s_thread_id;sceKernelDeleteSema(s_jobs);s_jobs=-1;set_error("Cannot create generation worker");return rc;}
  int rc=sceKernelStartThread(s_thread_id,0,NULL);if(rc<0){sceKernelDeleteThread(s_thread_id);s_thread_id=-1;sceKernelDeleteSema(s_jobs);s_jobs=-1;set_error("Cannot start generation worker");return rc;}
  dbg_log("GENERATION_POOL created thread=%x",s_thread_id);
 }
 if(prompt!=s_prompt)snprintf(s_prompt,sizeof(s_prompt),"%s",prompt?prompt:"");s_response[0]=0;s_error[0]=0;s_cancel=0;s_state=LLAMA_WORKER_RUNNING;
 int rc=sceKernelSignalSema(s_jobs,1);if(rc<0)set_error("Cannot signal generation worker");return rc;
}

enum llama_worker_state llama_worker_get_state(void)
{
    return s_state;
}

/* Ask the running worker to stop. The generation loop checks
 * `s_state == RUNNING` on every step, so flipping to DONE makes it exit
 * cleanly after the current token and keep the partial response. */
void llama_worker_stop(void)
{
    if (s_state == LLAMA_WORKER_RUNNING)
        s_cancel = 1;
}

const char *llama_worker_get_response(void)
{
    static char snap[LLAMA_RESPONSE_MAX];
    static const char ass_marker[] = "<|im_start|>assistant";

    lock_io();
    memcpy(snap, s_response, sizeof(snap));
    unlock_io();

    /* The model occasionally regenerates the whole ChatML template (system +
     * user lines) before its actual answer. Anything echoed before the
     * LATEST "<|im_start|>assistant" marker is template leakage, not a real
     * response - drop it so the UI never shows the system prompt.
     * (Workers/void of marker: keep the output as-is.) */
    const char *p = snap;
    const char *last = NULL;
    while ((p = strstr(p, ass_marker)) != NULL) {
        last = p;
        p += sizeof(ass_marker) - 1;
    }
    if (last) {
        const char *body = last + sizeof(ass_marker) - 1;
        memmove(snap, body, strlen(body) + 1);
    }

    /* drop a few leading control chars produced right after the tag */
    char *t = snap;
    while (*t == '\n' || *t == '\r' || *t == ' ')
        t++;
    if (t != snap)
        memmove(snap, t, strlen(t) + 1);

    return snap;
}

const char *llama_worker_get_error(void)
{
    static char snap[192];

    lock_io();
    memcpy(snap, s_error, sizeof(snap));
    unlock_io();
    return snap;
}

const char *llama_worker_get_model_info(void)
{
    return s_model_info;
}

void llama_worker_shutdown(void)
{
    if (s_thread_id >= 0) {
        s_cancel=1;s_quit=1;sceKernelSignalSema(s_jobs,1);
        int stat = 0;
        sceKernelWaitThreadEnd(s_thread_id, &stat, NULL);
        sceKernelDeleteThread(s_thread_id);
        s_thread_id = -1;
        sceKernelDeleteSema(s_jobs);s_jobs=-1;
    }

    free(s_prefix_snapshot);s_prefix_snapshot=NULL;s_session_ready=0;
    if (s_initialized) {
        engine_free_tokenizer(&s_tokenizer);
        free_transformer_from_malloc(&s_transformer);
        s_initialized = 0;
    }

    s_state = LLAMA_WORKER_EMPTY;
}

void engine_thread_probe(Transformer *t);
void engine_layout_experiment(Transformer *t);
void llama_worker_kernel_experiments(void){engine_thread_probe(&s_transformer);engine_layout_experiment(&s_transformer);}

void engine_prefetch_probe(Transformer *t);
void llama_worker_prefetch_experiment(void){engine_prefetch_probe(&s_transformer);}

void engine_batch_probe(Transformer *t);
void llama_worker_batch_experiment(void){engine_batch_probe(&s_transformer);}

int llama_worker_request(const char*prompt,int multi,int soft,int hard,int policy){
 if(s_state==LLAMA_WORKER_RUNNING)return -1;
 s_multi_job=multi;s_soft_job=soft;s_hard_job=hard;s_policy_job=policy;s_continue_job=0;
 return llama_worker_start(prompt);
}
int llama_worker_start_chat(const char*prompt,const char*history,int new_tokens){return llama_worker_request(prompt,history&&*history,new_tokens*3/4,new_tokens,0);}
int llama_worker_can_continue(void){return s_session_ready&&(session_can_continue(&s_session)||(s_session.reason==END_CONTEXT&&s_turn_count>1));}
int llama_worker_continue(void){if(s_state==LLAMA_WORKER_RUNNING||!llama_worker_can_continue())return -1;s_continue_job=1;return llama_worker_start(s_prompt);}
void llama_worker_reset(void){
 if(s_state==LLAMA_WORKER_RUNNING)return;
 online_reset_context();
 if(s_session_ready){session_reset(&s_session);memset(s_session.fed,0,sizeof(s_session.fed));memset(s_session.generated,0,sizeof(s_session.generated));memset(s_session.text,0,sizeof(s_session.text));}
 memset(s_ledger,0,sizeof(s_ledger));s_turn_count=0;s_ledger_n=0;s_last_multi=0;s_continue_job=0;
 s_online_used=0;s_online_inference_at=0;
 memset(s_online_title,0,sizeof(s_online_title));memset(s_online_source,0,sizeof(s_online_source));
 lock_io();memset(s_prompt,0,sizeof(s_prompt));memset(s_response,0,sizeof(s_response));unlock_io();
 snprintf(s_route_name,sizeof(s_route_name),"neutral");memset(s_source,0,sizeof(s_source));snprintf(s_path_name,sizeof(s_path_name),"neural");
 dbg_log("CHAT_CONTEXT_RESET neural_tokens=0 ledger_tokens=0 turns=0 source_bytes=0");
}
void llama_worker_set_reuse(int on){s_reuse_enabled=on;}
const char*llama_worker_end_reason(void){return s_session_ready?session_reason(s_session.reason):"ready";}
void llama_worker_statistics(char*out,int n){SceKernelFreeMemorySizeInfo m={0};m.size=sizeof(m);sceKernelGetFreeMemorySize(&m);snprintf(out,n,"First output: %.2f s / Decode: %.2f pieces/s\nContext: %d / 512 / Reused: %d / Rebuild: %d\nResponse soft/hard: %d / %d / End: %s\nHeap: %.2f MiB / Free user: %.2f MiB\nTone: %s / Reference lookup: %llu us\nModel: LFM2.5-230M Q8 / Fixed 12 MiB KV",s_ttft,s_decode,s_session.pos,s_cache_reused,s_cache_rebuilt,s_soft_job,s_hard_job,llama_worker_end_reason(),mallinfo().uordblks/1048576.0,m.size_user/1048576.0,s_route_name,(unsigned long long)s_lookup_us);}

void llama_worker_prepare_prefix(void){if(s_state!=LLAMA_WORKER_RUNNING){phase4_prefix_init();}}

void llama_worker_set_evidence_pack(int enabled){s_evidence_pack=enabled!=0;}

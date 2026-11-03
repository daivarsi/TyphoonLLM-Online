/*
 * WoozyLLM Vita - Q8_0 inference engine public interface (llama_engine.c).
 */

#ifndef WOOZYLLM_LLAMA_ENGINE_H
#define WOOZYLLM_LLAMA_ENGINE_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int dim;
    int hidden_dim;
    int n_layers;
    int n_heads;
    int n_kv_heads;
    int vocab_size;
    int seq_len;
} Config;

typedef struct {
    int8_t *q;   // quantized values
    float *s;    // scaling factors
    uint8_t bits; // weights: 4 or 8; activation descriptors use 0
} QuantizedTensor;

typedef struct {
    QuantizedTensor *q_tokens; // (vocab_size, dim)

    float *rms_att_weight;     // (layer, dim)
    float *rms_ffn_weight;     // (layer, dim)
    QuantizedTensor *wq;       // (layer, dim, dim)
    QuantizedTensor *wk;       // (layer, dim, kv_dim)
    QuantizedTensor *wv;       // (layer, dim, kv_dim)
    QuantizedTensor *wo;       // (layer, dim, dim)
    QuantizedTensor *w1;       // (layer, hidden_dim, dim)
    QuantizedTensor *w2;       // (layer, dim, hidden_dim)
    QuantizedTensor *w3;       // (layer, hidden_dim, dim)
    float *rms_final_weight;   // (dim,)
    QuantizedTensor *wcls;     // shared with q_tokens when tied
} TransformerWeights;

typedef struct {
    float *x;
    float *xb;
    float *xb2;
    float *hb;
    float *hb2;
    QuantizedTensor xq;
    QuantizedTensor hq;
    float *q;
    float *k;
    float *v;
    float *att;
    float *logits;
    float *key_cache;   // (layer, ctx_len, kv_dim)
    float *value_cache; // (layer, ctx_len, kv_dim)
    float *rope_c;      // (ctx_len, head_size) precomputed cosines
    float *rope_s;      // (ctx_len, head_size) precomputed sines
    int    ctx_len;     // effective KV/generation context length
} RunState;

typedef struct {
    Config config;
    TransformerWeights weights;
    RunState state;
    float *data;         // model bytes (dedicated memblock)
    size_t file_size;
    int data_uid;        // SceUID of the model memblock, -1 if not set
} Transformer;

typedef struct {
    char *str;
    int id;
} TokenIndex;

typedef struct {
    char **vocab;
    float *vocab_scores;
    TokenIndex *sorted_vocab;
    int vocab_size;
    unsigned int max_token_length;
    unsigned int byte2id[256];
    /* resolved ChatML control-token IDs (parsed from the vocab at load) */
    int eos_id;      // "<|endoftext|>"
    int im_start_id; // "<|im_start|>"
    int im_end_id;   // "<|im_end|>"
} Tokenizer;

typedef struct {
    float prob;
    int index;
} ProbIndex;

typedef struct {
    int vocab_size;
    ProbIndex *probindex;
    float temperature;
    float topp;
    int top_k;
    float repeat_penalty;
    int repeat_last_n;
    int *repeat_tokens;
    int repeat_head;
    int repeat_count;
    unsigned long long rng_state;
} Sampler;

size_t engine_payload_offset(void);
int engine_parse_header(const void *data, size_t size, Config *config, int *shared_out);
void engine_map_weights(TransformerWeights *w, const Config *p, void *ptr,
                        uint8_t shared_classifier);
int engine_malloc_run_state(RunState *s, const Config *p);
void engine_free_run_state(RunState *s);
int engine_get_context(const Transformer *t);
void engine_free_weights(TransformerWeights *w);
float *engine_forward(Transformer *transformer, int token, int pos);

int engine_build_tokenizer(Tokenizer *t, const char *tokenizer_path, int vocab_size);
void engine_free_tokenizer(Tokenizer *t);
char *engine_decode(Tokenizer *t, int token);
void engine_encode(Tokenizer *t, const char *text, int *tokens, int *n_tokens);
void engine_tokenizer_special(Tokenizer *t, int *eos, int *im_start, int *im_end);

int engine_build_sampler(Sampler *sampler, int vocab_size, float temperature,
                         float topp, float repeat_penalty, unsigned long long rng_seed);
void engine_free_sampler(Sampler *sampler);
int engine_sample(Sampler *sampler, float *logits);
void engine_sampler_record(Sampler *sampler, int token);
void engine_sampler_reset(Sampler *sampler);

/* clear the KV caches / generation context before a fresh prompt so the
 * model never attends to leftover keys/values from previous answers    */
void engine_reset_context(Transformer *transformer);

/* Scores may be omitted only when another prompt position will follow. */
float*engine_prefill(Transformer*,int,int,int);
float*engine_prefill_batch4(Transformer*,const int*,int,int);
#endif /* WOOZYLLM_LLAMA_ENGINE_H */

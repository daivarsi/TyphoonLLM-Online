/*
 * WoozyLLM Vita - Q8_0 quantized inference engine (SmolLM / Llama-2 arch).
 *
 * Adapted from llama2.c runq.c for the Vita:
 *  - no mmap / file IO / exit() (weights are pointed into a malloc'd buffer)
 *  - no fp32 copy of the token embedding table (row dequantized on the fly)
 *  - GQA supported (n_kv_heads < n_heads)
 *  - GPT-2 style byte-level BPE tokenizer with a byte->id lookup table
 */

#include <stdint.h>
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <math.h>

#include <psp2/kernel/threadmgr.h>

#include "llama_engine.h"
#include "profile.h"
#include "dbg.h"
#include <psp2/power.h>

/* Cap the working KV context so the run state stays out of the tight newlib
 * heap. The model supports up to seq_len positions, but a 512-token window is
 * plenty for prompts + 96 generated tokens and halves the KV cache footprint. */
#define ENGINE_KV_CONTEXT 1024
#include "vitalm_format.h"
static VitaMetadata s_vitalm;
static int s_lfm=0,s_mixed_head=0;static float *s_conv_weights=NULL;static float s_conv_state[14][3][1024],s_conv_bcx[12*1024];
static int lfm_conv_layer(int l){return l<2||(l&1);}
static int s_qk=0;static float *s_qnorm=NULL,*s_knorm=NULL;
int engine_eos=2,engine_eot=2;
static float s_theta=100000.0f,s_eps=1e-5f;
size_t engine_payload_offset(void){return s_vitalm.payload;}

/* Owned by the single live engine thread; prefill wrappers restore on every return. */
static int s_compute_vocab_head=1;
// ----------------------------------------------------------------------------
// Globals

#ifndef WOOZY_PREFETCH_DISTANCE
#define WOOZY_PREFETCH_DISTANCE 0
#endif
static int s_prefetch_distance=WOOZY_PREFETCH_DISTANCE;
static int s_packed_weights;
static int s_model_version,s_tensor_index,s_model_layers,s_q4_alloc_failed;
static float *s_q4_scale_owners[1024];static int s_q4_scale_count;
static float q4_half(uint16_t h){
 uint32_t sign=(uint32_t)(h&0x8000)<<16,ex=(h>>10)&31,m=h&1023,bits;
 if(!ex){if(!m)bits=sign;else{int e=113;while(!(m&1024)){m<<=1;e--;}bits=sign|((uint32_t)e<<23)|((m&1023)<<13);}}
 else if(ex==31)bits=sign|0x7f800000|(m<<13);
 else bits=sign|((ex+112)<<23)|(m<<13);
 float f;memcpy(&f,&bits,4);return f;
}
int engine_q4_allocation_failed(void){return s_q4_alloc_failed;}

static int GS = 64; // group size for quantization of the weights

#define MM_THREADS 3
static SceUID s_mm_sem[2]={-1,-1},s_mm_done=-1,s_mm_thread[2]={-1,-1};
static int s_mm_ready,s_mm_threads=1,s_mm_shutdown;
static float *s_mm_out;
static const QuantizedTensor *s_mm_x,*s_mm_w;
static int s_mm_n,s_mm_d;
static volatile int s_mm_batch_job;
static const QuantizedTensor *s_mm_batch_x[4];
void engine_set_mm_threads(int n){s_mm_threads=n>=1&&n<=3?n:1;}
static void quantize(QuantizedTensor *qx, const float *x, int n)
{
    int num_groups = n / GS;
    float Q_MAX = 127.0f;

    for (int group = 0; group < num_groups; group++) {
        float wmax = 0.0f;
        for (int i = 0; i < GS; i++) {
            float val = fabsf(x[group * GS + i]);
            if (val > wmax)
                wmax = val;
        }
        float scale = wmax / Q_MAX;
        if (scale == 0.0f)
            scale = 1.0f;
        qx->s[group] = scale;
        for (int i = 0; i < GS; i++) {
            float quant_value = x[group * GS + i] / scale;
            int8_t quantized = (int8_t)roundf(quant_value);
            qx->q[group * GS + i] = quantized;
        }
    }
}

static QuantizedTensor *init_quantized_tensors(void **ptr, int n, int size_each)
{
    void *p = *ptr;
    QuantizedTensor *res = calloc(n, sizeof(QuantizedTensor));
    if (!res){s_q4_alloc_failed=1;return NULL;}
    if(s_q4_alloc_failed){free(res);return NULL;}
    for (int i = 0; i < n; i++) {
        int index=s_tensor_index++;
        int use4=(s_model_version==6 && (!s_mixed_head||index>0)) || (s_model_version==7 && index>=1+4*s_model_layers && index<1+7*s_model_layers);
        res[i].bits=use4?4:8;
        if(use4){
            size_t groups=(size_t)size_each/32;res[i].q=p;
            if(s_q4_scale_count>=1024 || !(res[i].s=malloc(groups*4))){s_q4_alloc_failed=1;return res;}
            s_q4_scale_owners[s_q4_scale_count++]=res[i].s;
            for(size_t g=0;g<groups;g++){uint16_t h;memcpy(&h,(int8_t*)p+g*18,2);res[i].s[g]=q4_half(h);}
            p=(int8_t*)p+groups*18;continue;
        }
        if(s_packed_weights){
            res[i].q=(int8_t*)p;res[i].s=NULL;
            p=(int8_t*)p+(size_t)(size_each/64)*68;continue;
        }
        res[i].q = (int8_t *)p;
        p = (int8_t *)p + size_each;
        res[i].s = (float *)p;
        p = (float *)p + size_each / GS;
    }
    *ptr = p;
    return res;
}

static QuantizedTensor *lfm_map_list(void **ptr,const Config*p,int kind){
 QuantizedTensor *out=calloc(p->n_layers,sizeof(*out));if(!out){s_q4_alloc_failed=1;return NULL;}
 for(int l=0;l<p->n_layers;l++){
  int conv=lfm_conv_layer(l),rows=kind==0?(conv?3*p->dim:p->dim):(conv?0:p->dim*p->n_kv_heads/p->n_heads);
  if(rows){QuantizedTensor *one=init_quantized_tensors(ptr,1,rows*p->dim);if(!one){s_q4_alloc_failed=1;return out;}out[l]=one[0];free(one);}
 }
 return out;
}

/* Point the weight pointers into the (already malloc'd) model bytes. */
void engine_map_weights(TransformerWeights *w, const Config *p, void *ptr,
                               uint8_t shared_classifier)
{
    s_tensor_index=0;s_model_layers=p->n_layers;s_q4_alloc_failed=0;
    int head_size = p->dim / p->n_heads;
    float *fptr = (float *)ptr;
    w->rms_att_weight = fptr;
    fptr += p->n_layers * p->dim;
    w->rms_ffn_weight = fptr;
    fptr += p->n_layers * p->dim;
    w->rms_final_weight = fptr;
    fptr += p->dim;

    ptr = (void *)fptr;
    w->q_tokens = init_quantized_tensors(&ptr, 1, p->vocab_size * p->dim);
    w->wq = s_lfm?lfm_map_list(&ptr,p,0):init_quantized_tensors(&ptr, p->n_layers, p->dim * p->dim);
    w->wk = s_lfm?lfm_map_list(&ptr,p,1):init_quantized_tensors(&ptr, p->n_layers, p->dim * (p->n_kv_heads * head_size));
    w->wv = s_lfm?lfm_map_list(&ptr,p,2):init_quantized_tensors(&ptr, p->n_layers, p->dim * (p->n_kv_heads * head_size));
    w->wo = init_quantized_tensors(&ptr, p->n_layers, p->dim * p->dim);
    w->w1 = init_quantized_tensors(&ptr, p->n_layers, p->dim * p->hidden_dim);
    w->w2 = init_quantized_tensors(&ptr, p->n_layers, p->hidden_dim * p->dim);
    w->w3 = init_quantized_tensors(&ptr, p->n_layers, p->dim * p->hidden_dim);
    w->wcls = shared_classifier ? w->q_tokens
                                : init_quantized_tensors(&ptr, 1, p->dim * p->vocab_size);
    s_qnorm=s_qk?(float*)ptr:NULL;s_knorm=s_qk?s_qnorm+p->n_layers*head_size:NULL;
    s_conv_weights=s_lfm?s_knorm+p->n_layers*head_size:NULL;
}

/* Validate a v2 header at the start of the malloc'd model buffer.
 * Returns 0 on success; fills config and sets GS. */
int engine_parse_header(const void *data, size_t size, Config *config, int *shared_out)
{
    const unsigned char *p = (const unsigned char *)data;

    if(!p||size<256)return -1;s_lfm=!memcmp(p,"VLFM2001",8);if(!s_lfm&&memcmp(p,"VLPRE001",8))return -1;
    uint32_t d=vl_u32(p+8),f=vl_u32(p+12),l=vl_u32(p+16),h=vl_u32(p+20),k=vl_u32(p+24),v=vl_u32(p+28),ctx=vl_u32(p+32),bits=vl_u32(p+36),tied=vl_u32(p+40),qk=vl_u32(p+44);
    if(d<64||d>1024||d%64||f<64||f>4096||f%64||!l||l>30||!h||h>16||!k||k>h||d%h||h%k||d/h!=64||v<256||v>65536||!ctx||ctx>1024||(bits!=4&&bits!=5&&bits!=8)||tied>1||qk>1)return -2;
    s_theta=vl_float(p+56);s_eps=vl_float(p+60);
    if(!isfinite(s_theta)||s_theta<1||s_theta>1e7||!isfinite(s_eps)||s_eps<1e-8f||s_eps>1e-3f)return -3;
    uint64_t dense=(uint64_t)v*d*(2-tied)+(uint64_t)l*(2*d*d+2*d*(d*k/h)+3*d*f);
    if(s_lfm){if(d!=1024||f!=2560||l!=14||h!=16||k!=8||v!=65536||!tied||!qk)return -7;dense=(uint64_t)v*d+6ULL*(2*d*d+2*d*(d*k/h))+8ULL*4*d*d+3ULL*l*d*f;}
    uint64_t want=256+4*((uint64_t)(2*l+1)*d+2*qk*l*(d/h))+dense/(bits==8?64:32)*(bits==8?68:18);
    if(bits==5){if(!s_lfm)return -8;uint64_t emb=(uint64_t)v*d;want=256+4*((uint64_t)(2*l+1)*d+2*qk*l*(d/h))+(dense-emb)/32*18+emb/64*68;}
    if(s_lfm)want+=8ULL*3*d*4;
    if(want!=size||vl_u64(p+64)!=want)return -4;
    engine_eos=(int)vl_u32(p+48);engine_eot=(int)vl_u32(p+52);
    if(engine_eos<0||engine_eos>=(int)v||engine_eot<0||engine_eot>=(int)v)return -5;
    for(int i=72;i<256;i++)if(p[i])return -6;
    *config=(Config){d,f,l,h,k,v,ctx};s_vitalm.payload=256;s_qk=qk;
    s_mixed_head=bits==5;s_model_version=bits==8?5:6;s_packed_weights=bits==8||bits==5;GS=64;*shared_out=tied;
    return 0;
}

int engine_malloc_run_state(RunState *s, const Config *p)
{
    int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;
    int head_size = p->dim / p->n_heads;
    int ctx = p->seq_len < ENGINE_KV_CONTEXT ? p->seq_len : ENGINE_KV_CONTEXT;

    memset(s, 0, sizeof(*s));
    s->ctx_len = ctx;
    s->x = calloc(p->dim, sizeof(float));
    s->xb = calloc(p->dim, sizeof(float));
    s->xb2 = calloc(p->dim, sizeof(float));
    s->hb = calloc(p->hidden_dim, sizeof(float));
    s->hb2 = calloc(p->hidden_dim, sizeof(float));
    s->xq.q = calloc(p->dim, sizeof(int8_t));
    s->xq.s = calloc(p->dim, sizeof(float));
    s->hq.q = calloc(p->hidden_dim, sizeof(int8_t));
    s->hq.s = calloc(p->hidden_dim, sizeof(float));
    s->q = calloc(p->dim, sizeof(float));
    s->k = calloc(kv_dim, sizeof(float));
    s->v = calloc(kv_dim, sizeof(float));
    s->att = calloc(p->n_heads * ctx, sizeof(float));
    s->logits = calloc(p->vocab_size, sizeof(float));
    s->key_cache = calloc((size_t)(s_lfm?6:p->n_layers) * ctx * kv_dim, sizeof(float));
    s->value_cache = calloc((size_t)(s_lfm?6:p->n_layers) * ctx * kv_dim, sizeof(float));
    s->rope_c = calloc((size_t)ctx * head_size, sizeof(float));
    s->rope_s = calloc((size_t)ctx * head_size, sizeof(float));

    if (!s->x || !s->xb || !s->xb2 || !s->hb || !s->hb2 ||
        !s->xq.q || !s->xq.s || !s->hq.q || !s->hq.s ||
        !s->q || !s->k || !s->v || !s->att || !s->logits ||
        !s->key_cache || !s->value_cache || !s->rope_c || !s->rope_s) {
        return -1;
    }

    // precompute RoPE cos/sin tables for every position
    for (int ipos = 0; ipos < ctx; ipos++) {
        for (int hd = 0; hd < head_size; hd++) {
            float freq = 1.0f / powf(s_theta, hd / (float)head_size);
            float val = (float)ipos * freq;
            s->rope_c[ipos * head_size + hd] = cosf(val);
            s->rope_s[ipos * head_size + hd] = sinf(val);
        }
    }
    return 0;
}

int engine_get_context(const Transformer *t)
{
    return t->state.ctx_len;
}

static void mm_pool_destroy(void);
static void engine_batch_free(void);
void engine_free_run_state(RunState *s)
{
    mm_pool_destroy();
    engine_batch_free();
    free(s->x);
    free(s->xb);
    free(s->xb2);
    free(s->hb);
    free(s->hb2);
    free(s->xq.q);
    free(s->xq.s);
    free(s->hq.q);
    free(s->hq.s);
    free(s->q);
    free(s->k);
    free(s->v);
    free(s->att);
    free(s->logits);
    free(s->key_cache);
    free(s->value_cache);
    free(s->rope_c);
    free(s->rope_s);
    memset(s, 0, sizeof(*s));
}

void engine_free_weights(TransformerWeights *w)
{
    for(int i=0;i<s_q4_scale_count;i++)free(s_q4_scale_owners[i]);
    s_q4_scale_count=0;
    free(w->q_tokens);
    free(w->wq);
    free(w->wk);
    free(w->wv);
    free(w->wo);
    free(w->w1);
    free(w->w2);
    free(w->w3);
    if (w->wcls != w->q_tokens)
        free(w->wcls);
}

// ----------------------------------------------------------------------------
// neural net blocks

static void rmsnorm(float *o, const float *x, const float *weight, int size)
{
    float ss = 0.0f;
    for (int j = 0; j < size; j++)
        ss += x[j] * x[j];
    ss /= size;
    ss += s_eps;
    ss = 1.0f / sqrtf(ss);
    for (int j = 0; j < size; j++)
        o[j] = weight[j] * (ss * x[j]);
}

static void softmax(float *x, int size)
{
    float max_val = x[0];
    for (int i = 1; i < size; i++)
        if (x[i] > max_val)
            max_val = x[i];
    float sum = 0.0f;
    for (int i = 0; i < size; i++) {
        x[i] = expf(x[i] - max_val);
        sum += x[i];
    }
    for (int i = 0; i < size; i++)
        x[i] /= sum;
}

#ifdef __ARM_NEON
static inline int32_t dot64_neon(const int8_t *x,const int8_t *w){
 int32x4_t a=vdupq_n_s32(0),b=vdupq_n_s32(0);
 #define DOT16(O) do { int8x16_t xx=vld1q_s8(x+(O)),ww=vld1q_s8(w+(O)); \
 a=vpadalq_s16(a,vmull_s8(vget_low_s8(xx),vget_low_s8(ww))); \
 b=vpadalq_s16(b,vmull_s8(vget_high_s8(xx),vget_high_s8(ww))); }while(0)
 DOT16(0);DOT16(16);DOT16(32);DOT16(48);
 #undef DOT16
 a=vaddq_s32(a,b);int32x2_t c=vadd_s32(vget_low_s32(a),vget_high_s32(a));
 c=vpadd_s32(c,c);return vget_lane_s32(c,0);
}
#endif
static float packed_scale(const int8_t *block){float v;memcpy(&v,block+64,4);return v;}
static void packed_rows_one(float *out,const QuantizedTensor *x,const QuantizedTensor *w,int n,int i0,int i1){
 int groups=n/64;
 for(int i=i0;i<i1;i++){
  const int8_t *row=w->q+(size_t)i*groups*68;float val=0;
  for(int g=0;g<groups;g++){
   const int8_t *b=row+g*68;int32_t dot=0;
   #ifdef __ARM_NEON
   dot=dot64_neon(x->q+g*64,b);
   #else
   for(int k=0;k<64;k++)dot+=(int32_t)x->q[g*64+k]*b[k];
   #endif
   val+=(float)dot*packed_scale(b)*x->s[g];
  }out[i]=val;
 }
}
/* Four output rows reuse each activation load. Accumulation within each
 * quantization group is INT32, with the original FP32 group order preserved. */
static void packed_rows_four(float *out,const QuantizedTensor *x,const QuantizedTensor *w,int n,int i0,int i1){
#ifdef __ARM_NEON
 if(GS==64 && n%64==0){
  int groups=n/64,i=i0;
  for(;i+3<i1;i+=4){
   float32x4_t val=vdupq_n_f32(0);
   for(int g=0;g<groups;g++){
    int ahead=s_prefetch_distance;
    if(ahead>0 && ahead<groups-g)for(int r=0;r<4;r++)
        __builtin_prefetch(w->q+((size_t)(i+r)*groups+g+ahead)*68,0,3);
    int32x4_t a0=vdupq_n_s32(0),a1=a0,a2=a0,a3=a0;
    for(int k=0;k<64;k+=16){
     int8x16_t xx=vld1q_s8(x->q+g*64+k);
     #define R4(A,R) do{int8x16_t ww=vld1q_s8(w->q+((size_t)(i+(R))*groups+g)*68+k); \
      A=vpadalq_s16(A,vmull_s8(vget_low_s8(xx),vget_low_s8(ww))); \
      A=vpadalq_s16(A,vmull_s8(vget_high_s8(xx),vget_high_s8(ww)));}while(0)
     R4(a0,0);R4(a1,1);R4(a2,2);R4(a3,3);
     #undef R4
    }
    int32x2_t p0=vpadd_s32(vget_low_s32(a0),vget_high_s32(a0));
    int32x2_t p1=vpadd_s32(vget_low_s32(a1),vget_high_s32(a1));
    int32x2_t p2=vpadd_s32(vget_low_s32(a2),vget_high_s32(a2));
    int32x2_t p3=vpadd_s32(vget_low_s32(a3),vget_high_s32(a3));
    int32x4_t dots=vcombine_s32(vpadd_s32(p0,p1),vpadd_s32(p2,p3));
    float32x4_t scales=vdupq_n_f32(packed_scale(w->q+((size_t)i*groups+g)*68));
    scales=vsetq_lane_f32(packed_scale(w->q+((size_t)(i+1)*groups+g)*68),scales,1);
    scales=vsetq_lane_f32(packed_scale(w->q+((size_t)(i+2)*groups+g)*68),scales,2);
    scales=vsetq_lane_f32(packed_scale(w->q+((size_t)(i+3)*groups+g)*68),scales,3);
    val=vmlaq_f32(val,vmulq_f32(vcvtq_f32_s32(dots),scales),vdupq_n_f32(x->s[g]));
   }
   vst1q_f32(out+i,val);
  }
  packed_rows_one(out,x,w,n,i,i1);return;
 }
#endif
 packed_rows_one(out,x,w,n,i0,i1);
}

#include "q4-kernels.inc"

static void matmul_rows_one(float *xout, const QuantizedTensor *x, const QuantizedTensor *w,
                        int n, int i0, int i1)
{
#ifdef __ARM_NEON
    if(GS==64 && n%64==0){
     int groups=n/64;
     for(int i=i0;i<i1;i++){
      float val=0;const int8_t *row=w->q+(size_t)i*n;
      const float *ws=w->s+(size_t)i*groups;
      for(int g=0;g<groups;g++){
       int32_t dot=dot64_neon(x->q+g*64,row+g*64);
       val+=(float)dot*ws[g]*x->s[g];
      }xout[i]=val;
     }return;
    }
#endif
    // W (d,n) @ x (n,) -> xout (d,), both inputs quantized, rows [i0, i1)
    for (int i = i0; i < i1; i++) {
        float val = 0.0f;
        int32_t ival = 0;
        int in = i * n;
        const float *ws=w->s + in / GS;
        const float *xs=x->s;
        for (int j = 0; j <= n - GS; j += GS) {
            for (int k = 0; k < GS; k++)
                ival += ((int32_t)x->q[j + k]) * ((int32_t)w->q[in + j + k]);
            val += ((float)ival) * (*ws++) * (*xs++);
            ival = 0;
        }
        xout[i] = val;
    }
}

#include "row4-experiment.inc"
#ifdef __ARM_NEON
#include "batch4-kernel.inc"
#else
static void batch4_core(float *out,const QuantizedTensor *const batch[4],const QuantizedTensor *w,int n,int d,int i0,int i1){for(int b=0;b<4;b++)matmul_core(out+b*d,batch[b],w,n,i0,i1);}
#endif

#include "kernel-selftest.inc"

static int mm_worker(SceSize args,void *argp){
 if(args!=sizeof(int)||!argp)return -1;
 int id=*(const int*)argp;
 for(;;){
  if(sceKernelWaitSema(s_mm_sem[id],1,NULL)<0)break;
  if(s_mm_shutdown)break;
  int n=s_mm_threads;
  if(s_mm_batch_job)batch4_core(s_mm_out,s_mm_batch_x,s_mm_w,s_mm_n,s_mm_d,s_mm_d*id/n,s_mm_d*(id+1)/n);
  else matmul_core(s_mm_out,s_mm_x,s_mm_w,s_mm_n,s_mm_d*id/n,s_mm_d*(id+1)/n);
  sceKernelSignalSema(s_mm_done,1);
 }
 return 0;
}
static void mm_pool_destroy(void){
 s_mm_shutdown=1;
 for(int i=0;i<2;i++)if(s_mm_thread[i]>=0){
  if(s_mm_ready){sceKernelSignalSema(s_mm_sem[i],1);sceKernelWaitThreadEnd(s_mm_thread[i],NULL,NULL);}
  sceKernelDeleteThread(s_mm_thread[i]);s_mm_thread[i]=-1;
 }
 for(int i=0;i<2;i++)if(s_mm_sem[i]>=0){sceKernelDeleteSema(s_mm_sem[i]);s_mm_sem[i]=-1;}
 if(s_mm_done>=0){sceKernelDeleteSema(s_mm_done);s_mm_done=-1;}
 s_mm_ready=0;
}
static int engine_start_mm_pool(void){
 if(s_mm_ready)return 0;
 uint64_t t=sceKernelGetProcessTimeWide();s_mm_shutdown=0;
 s_mm_done=sceKernelCreateSema("mm_done",0,0,2,NULL);
 if(s_mm_done<0)return -1;
 for(int i=0;i<2;i++){
  s_mm_sem[i]=sceKernelCreateSema("mm_work",0,0,1,NULL);
  if(s_mm_sem[i]<0){mm_pool_destroy();return -1;}
  s_mm_thread[i]=sceKernelCreateThread("mm_worker",mm_worker,0x10000100,64*1024,0,0,NULL);
  if(s_mm_thread[i]<0){mm_pool_destroy();return -1;}
 }
 int started=0;
 for(int i=0;i<2;i++){
  int id=i;
  if(sceKernelStartThread(s_mm_thread[i],sizeof(id),&id)<0){
   s_mm_shutdown=1;
   for(int k=0;k<started;k++){sceKernelSignalSema(s_mm_sem[k],1);sceKernelWaitThreadEnd(s_mm_thread[k],NULL,NULL);}
   mm_pool_destroy();return -1;
  }started++;
 }
 s_mm_ready=1;
 dbg_log("POOL startup_us=%llu workers=2",(unsigned long long)(sceKernelGetProcessTimeWide()-t));
 return 0;
}
static void matmul(float *out,const QuantizedTensor *x,const QuantizedTensor *w,int n,int d){
 if(s_mm_threads==1 || (size_t)n*d<65536 || engine_start_mm_pool()<0){matmul_core(out,x,w,n,0,d);return;}
 s_mm_batch_job=0;s_mm_out=out;s_mm_x=x;s_mm_w=w;s_mm_n=n;s_mm_d=d;
 for(int i=0;i<s_mm_threads-1;i++)sceKernelSignalSema(s_mm_sem[i],1);
 matmul_core(out,x,w,n,d*(s_mm_threads-1)/s_mm_threads,d);
 for(int i=0;i<s_mm_threads-1;i++)sceKernelWaitSema(s_mm_done,1,NULL);
}

#include "thread-probe.inc"
#ifdef __ARM_NEON
#include "packed-experiment.inc"
#else
void engine_layout_experiment(Transformer *t){(void)t;}
#endif

#include "prefetch-probe.inc"

#include "batch4-dispatch.inc"
#include "batch4-selftest.inc"
#include "batch4-probe.inc"
#include "q4-selftest.inc"

static void model_embedding(float *x,const TransformerWeights *w,int token,int dim){
 if(w->q_tokens->bits==4){q4_embedding(x,w->q_tokens,token,dim);return;}
 const int8_t *eq=w->q_tokens->q+(size_t)token*dim;const float *es=w->q_tokens->s;
 if(es){for(int i=0;i<dim;i++)x[i]=(float)eq[i]*es[((size_t)token*dim+i)/64];}
 else{const int8_t *base=w->q_tokens->q+(size_t)token*(dim/64)*68;for(int i=0;i<dim;i++){const int8_t *b=base+(i/64)*68;x[i]=(float)b[i%64]*packed_scale(b);}}
}

static void lfm_conv_step(float*out,const float*bcx,int layer,int dim){
 int ci=0;for(int l=0;l<layer;l++)if(lfm_conv_layer(l))ci++;
 const float*w=s_conv_weights+ci*dim*3;
 for(int i=0;i<dim;i++){
  s_conv_state[layer][0][i]=s_conv_state[layer][1][i];s_conv_state[layer][1][i]=s_conv_state[layer][2][i];s_conv_state[layer][2][i]=bcx[i]*bcx[2*dim+i];
  float v=s_conv_state[layer][0][i]*w[3*i];v+=s_conv_state[layer][1][i]*w[3*i+1];v+=s_conv_state[layer][2][i]*w[3*i+2];out[i]=bcx[dim+i]*v;
 }
}

static void apply_qk_norm(float*q,float*k,int layer,const Config*p){
 if(!s_qk)return;
 int head=p->dim/p->n_heads;
 for(int i=0;i<p->n_heads;i++)rmsnorm(q+i*head,q+i*head,s_qnorm+layer*head,head);
 for(int i=0;i<p->n_kv_heads;i++)rmsnorm(k+i*head,k+i*head,s_knorm+layer*head,head);
}

float *engine_forward(Transformer *transformer, int token, int pos)
{
    uint64_t ft=sceKernelGetProcessTimeWide(); prof_calls++;
    Config *p = &transformer->config;
    TransformerWeights *w = &transformer->weights;
    RunState *s = &transformer->state;
    float *x = s->x;
    int dim = p->dim;
    int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;
    int kv_mul = p->n_heads / p->n_kv_heads;
    int hidden_dim = p->hidden_dim;
    int head_size = dim / p->n_heads;
    float inv_head_size = 1.0f / sqrtf((float)head_size);
    int ctx = s->ctx_len;   // effective KV context (<= seq_len)

    TIMED(30,P_EMBED,model_embedding(x,w,token,dim););

    for (int l = 0; l < p->n_layers; l++) {
        uint64_t lt=sceKernelGetProcessTimeWide();

        TIMED(l,P_NORM,rmsnorm(s->xb, x, w->rms_att_weight + l * dim, dim););

        TIMED(l,P_QUANT,quantize(&s->xq, s->xb, dim););
        if(s_lfm&&lfm_conv_layer(l)){
            TIMED(l,P_CONV_IN,matmul(s_conv_bcx,&s->xq,w->wq+l,dim,3*dim););
            TIMED(l,P_CONV_STEP,lfm_conv_step(s->xb,s_conv_bcx,l,dim););TIMED(l,P_QUANT,quantize(&s->xq,s->xb,dim););
            TIMED(l,P_CONV_OUT,matmul(s->xb2,&s->xq,w->wo+l,dim,dim););
            for(int i=0;i<dim;i++)x[i]+=s->xb2[i];
        }else{
        TIMED(l,P_Q,matmul(s->q, &s->xq, w->wq + l, dim, dim););
        TIMED(l,P_K,matmul(s->k, &s->xq, w->wk + l, dim, kv_dim););
        TIMED(l,P_V,matmul(s->v, &s->xq, w->wv + l, dim, kv_dim););

        TIMED(l,P_QKNORM,apply_qk_norm(s->q,s->k,l,p););
        // RoPE: rotate adjacent pairs (i, i+1) in each head
        TIMED(l,P_ROPE,{
            int ridx = pos * head_size;
            for (int i = 0; i < dim; i += 2) {
                int hd = i % head_size;
                float fcr = s->rope_c[ridx + hd];
                float fci = s->rope_s[ridx + hd];
                int rotn = i < kv_dim ? 2 : 1;
                for (int v = 0; v < rotn; v++) {
                    float *vec = v == 0 ? s->q : s->k;
                    float v0 = vec[i];
                    float v1 = vec[i + 1];
                    vec[i] = v0 * fcr - v1 * fci;
                    vec[i + 1] = v0 * fci + v1 * fcr;
                }
            }
        });

        // save key,value at this time step to the kv cache
        int loff = (s_lfm?l/2-1:l) * ctx * kv_dim;
        float *key_cache_row = s->key_cache + loff + pos * kv_dim;
        float *value_cache_row = s->value_cache + loff + pos * kv_dim;
        TIMED(l,P_KV,memcpy(key_cache_row, s->k, kv_dim * sizeof(float));
        memcpy(value_cache_row, s->v, kv_dim * sizeof(float)););

        // multihead attention
        for (int h = 0; h < p->n_heads; h++) {
            float *q = s->q + h * head_size;
            float *att = s->att + h * ctx;
            TIMED(l,P_SCORE,for (int t = 0; t <= pos; t++) {
                float *k = s->key_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                float score = 0.0f;
                for (int i = 0; i < head_size; i++)
                    score += q[i] * k[i];
                score *= inv_head_size;
                att[t] = score;
            }
            );
            TIMED(l,P_SOFTMAX,softmax(att, pos + 1););
            float *xb = s->xb + h * head_size;
            TIMED(l,P_VALUE,memset(xb, 0, head_size * sizeof(float));
            for (int t = 0; t <= pos; t++) {
                float *v = s->value_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                float a = att[t];
                for (int i = 0; i < head_size; i++)
                    xb[i] += a * v[i];
            }
            );
        }

        TIMED(l,P_QUANT,quantize(&s->xq, s->xb, dim););
        TIMED(l,P_OUT,matmul(s->xb2, &s->xq, w->wo + l, dim, dim););
        TIMED(l,P_RESID,for (int i = 0; i < dim; i++) x[i] += s->xb2[i];);

        }
        TIMED(l,P_NORM,rmsnorm(s->xb, x, w->rms_ffn_weight + l * dim, dim););

        TIMED(l,P_QUANT,quantize(&s->xq, s->xb, dim););
        TIMED(l,P_GATE,matmul(s->hb, &s->xq, w->w1 + l, dim, hidden_dim););
        TIMED(l,P_UP,matmul(s->hb2, &s->xq, w->w3 + l, dim, hidden_dim););

        // SwiGLU
        TIMED(l,P_ACT,for (int i = 0; i < hidden_dim; i++) {
            float val = s->hb[i];
            val *= (1.0f / (1.0f + expf(-val)));
            val *= s->hb2[i];
            s->hb[i] = val;
        });

        TIMED(l,P_QUANT,quantize(&s->hq, s->hb, hidden_dim););
        TIMED(l,P_DOWN,matmul(s->xb, &s->hq, w->w2 + l, hidden_dim, dim););
        TIMED(l,P_RESID,for (int i = 0; i < dim; i++) x[i] += s->xb[i];);
        prof_layer[l]+=sceKernelGetProcessTimeWide()-lt;
    }

        TIMED(30,P_FINAL,rmsnorm(x, x, w->rms_final_weight, dim););

        TIMED(30,P_QUANT,quantize(&s->xq, x, dim););
        if(s_compute_vocab_head){TIMED(30,P_LOGITS,matmul(s->logits, &s->xq, w->wcls, dim, p->vocab_size););}
    prof_forward_us+=sceKernelGetProcessTimeWide()-ft;
    return s->logits;
}

/* Zero the KV caches so a brand-new prompt never attends to key/values
 * left over from a previous generation (the "mixed conversation" bug). */
void engine_reset_context(Transformer *transformer)
{
    if(s_lfm)memset(s_conv_state,0,sizeof(s_conv_state));
    RunState *s = &transformer->state;
    if (!s->key_cache || !s->value_cache)
        return;
    int kv_dim = (transformer->config.dim * transformer->config.n_kv_heads)
               / transformer->config.n_heads;
    size_t bytes = (size_t)(s_lfm?6:transformer->config.n_layers) * (size_t)s->ctx_len
                 * (size_t)kv_dim * sizeof(float);
    memset(s->key_cache, 0, bytes);
    memset(s->value_cache, 0, bytes);
}

#include "prefix_checkpoint.inc"

void engine_sampler_reset(Sampler *sampler)
{
    if (sampler->repeat_tokens) {
        memset(sampler->repeat_tokens, 0,
               (size_t)sampler->repeat_last_n * sizeof(int));
    }
    sampler->repeat_head = 0;
    sampler->repeat_count = 0;
}

// ----------------------------------------------------------------------------
// Byte-level BPE tokenizer (GPT-2 style)

static int compare_tokens(const void *a, const void *b)
{
    return strcmp(((const TokenIndex *)a)->str, ((const TokenIndex *)b)->str);
}

/* Read tokenizer.bin: int32 max_token_length, then per token (float score,
 * int32 len, bytes), then an optional 256x uint32 byte->id table. */
/* Parallel browser: real ChatML control-token IDs live in the vocab and
 * MUST be resolved from the file - `storage does not guarantee fixed 0/1/2
 * slots. This is the "parse_special = 1" equivalent for this engine.     */
static int vocab_lookup(char **vocab, int vocab_size, const char *needle)
{
    for (int i = 0; i < vocab_size; i++) {
        if (vocab[i] && strcmp(vocab[i], needle) == 0)
            return i;
    }
    return -1;
}

void engine_tokenizer_special(Tokenizer *t, int *eos, int *im_start, int *im_end)
{
    *eos      = t->eos_id;
    *im_start = t->im_start_id;
    *im_end   = t->im_end_id;
}

static void resolve_special_ids(Tokenizer *t)
{
    t->eos_id      = vocab_lookup(t->vocab, t->vocab_size, "<|endoftext|>");
    t->im_start_id = vocab_lookup(t->vocab, t->vocab_size, "<|im_start|>");
    t->im_end_id   = vocab_lookup(t->vocab, t->vocab_size, "<|im_end|>");

    if (t->eos_id < 0)      t->eos_id      = 0;
    if (t->im_start_id < 0) t->im_start_id = t->vocab_size - 2;
    if (t->im_end_id < 0)   t->im_end_id   = t->vocab_size - 1;
}

int engine_build_tokenizer(Tokenizer *t, const char *tokenizer_path, int vocab_size)
{
    FILE *file;
    int len;

    memset(t, 0, sizeof(*t));
    t->vocab_size = vocab_size;
    t->vocab = calloc(vocab_size, sizeof(char *));
    t->vocab_scores = calloc(vocab_size, sizeof(float));
    t->sorted_vocab = NULL;
    t->eos_id = 0; t->im_start_id = 0; t->im_end_id = 0;
    for (int i = 0; i < 256; i++)
        t->byte2id[i] = 0xFFFFFFFFu;
    if (!t->vocab || !t->vocab_scores)
        return -1;

    file = fopen(tokenizer_path, "rb");
    if (!file)
        return -2;
    if (fread(&t->max_token_length, sizeof(int), 1, file) != 1) {
        fclose(file);
        return -2;
    }
    for (int i = 0; i < vocab_size; i++) {
        if (fread(t->vocab_scores + i, sizeof(float), 1, file) != 1) {
            fclose(file);
            return -2;
        }
        if (fread(&len, sizeof(int), 1, file) != 1) {
            fclose(file);
            return -2;
        }
        t->vocab[i] = malloc((size_t)len + 1);
        if (!t->vocab[i]) {
            fclose(file);
            return -1;
        }
        if (fread(t->vocab[i], (size_t)len, 1, file) != 1) {
            fclose(file);
            return -2;
        }
        t->vocab[i][len] = '\0';
    }
    // optional byte table (256 x uint32)
    {
        unsigned int table[256];
        if (fread(table, sizeof(unsigned int), 256, file) == 256) {
            for (int i = 0; i < 256; i++)
                t->byte2id[i] = table[i];
        } else {
            for (int i = 0; i < 256; i++)
                t->byte2id[i] = (unsigned int)(i + 3); // legacy llama2.c fallback
        }
    }
    fclose(file);
    resolve_special_ids(t);
    return 0;
}

void engine_free_tokenizer(Tokenizer *t)
{
    for (int i = 0; i < t->vocab_size; i++)
        free(t->vocab[i]);
    free(t->vocab);
    free(t->vocab_scores);
    free(t->sorted_vocab);
    memset(t, 0, sizeof(*t));
}

char *engine_decode(Tokenizer *t, int token)
{
    return t->vocab[token];
}

static int str_lookup(const char *str, const TokenIndex *sorted_vocab, int vocab_size)
{
    TokenIndex tok;
    tok.str = (char *)str;
    const TokenIndex *res = bsearch(&tok, sorted_vocab, vocab_size, sizeof(TokenIndex),
                                    compare_tokens);
    return res != NULL ? res->id : -1;
}

/* Special tokens of the SmolLM tokenizer (string forms; real IDs are
 * resolved from the vocab at load time, see resolve_special_ids()). */
static const char *const s_specials[3] = { "<|endoftext|>", "<|im_start|>", "<|im_end|>" };

/* Encode UTF-8 text into tokens. Byte-level BPE: initial tokens are single
 * bytes (via byte2id), then greedily merged by score (= -merge rank).
 *
 * ChatML control tokens (<|im_start|>, <|im_end|>, <|endoftext|>) are
 * emitted as THEIR REAL vocab IDs (resolved at tokenizer load) and are then
 * FROZEN: the BPE merge loop skips pairs that include a special token, so a
 * control tag can never be merged away into an ordinary text token.      */
#include "lfm_merges.inc"
static void engine_encode_raw(Tokenizer *t, const char *text, int *tokens, int *n_tokens)
{
    const unsigned char *c;

    if (t->sorted_vocab == NULL) {
        t->sorted_vocab = malloc((size_t)t->vocab_size * sizeof(TokenIndex));
        for (int i = 0; i < t->vocab_size; i++) {
            t->sorted_vocab[i].str = t->vocab[i];
            t->sorted_vocab[i].id = i;
        }
        qsort(t->sorted_vocab, t->vocab_size, sizeof(TokenIndex), compare_tokens);
    }

    size_t buflen = (size_t)t->max_token_length * 2 + 1 + 2;
    char *str_buffer = malloc(buflen);
    if (!str_buffer) {
        *n_tokens = 0;
        return;
    }

    *n_tokens = 0;

    // split into raw byte tokens, honoring special tokens verbatim
    for (c = (const unsigned char *)text; *c;) {
        int matched = 0;
        for (int si = 0; si < 3; si++) {
            const char *sp = s_specials[si];
            size_t splen = strlen(sp);
            if (strncmp((const char *)c, sp, splen) == 0) {
                /* parse_special=true: emit the RESOLVED vocab id */
                int id = (si == 0) ? t->eos_id
                        : (si == 1) ? t->im_start_id
                        :              t->im_end_id;
                if (id >= 0)
                    tokens[(*n_tokens)++] = id;
                c += splen;
                matched = 1;
                break;
            }
        }
        if (matched)
            continue;

        unsigned int id = t->byte2id[*c];
        if (id != 0xFFFFFFFFu)
            tokens[(*n_tokens)++] = (int)id;
        c++;
    }

    if (*n_tokens == 0) {
        free(str_buffer);
        return;
    }

    // greedily merge the best consecutive pair each iteration
    while (1) {
        float best_score = -1e10f;
        int best_id = -1;
        int best_idx = -1;

        for (int i = 0; i < (*n_tokens - 1); i++) {
            /* never merge a ChatML control token into a text token */
            int a = tokens[i];
            int b = tokens[i + 1];
            if (a == t->eos_id || a == t->im_start_id || a == t->im_end_id ||
                b == t->eos_id || b == t->im_start_id || b == t->im_end_id)
                continue;

            if(s_lfm){
                int id=-1,rank=lfm_pair(a,b,&id);
                if(rank>=0&&-(float)rank>best_score){best_score=-(float)rank;best_id=id;best_idx=i;}
                continue;
            }
            snprintf(str_buffer, buflen, "%s%s", t->vocab[a], t->vocab[b]);
            int id = str_lookup(str_buffer, t->sorted_vocab, t->vocab_size);
            if (id != -1 && t->vocab_scores[id] > best_score) {
                best_score = t->vocab_scores[id];
                best_id = id;
                best_idx = i;
            }
        }

        if (best_idx == -1)
            break;

        tokens[best_idx] = best_id;
        for (int i = best_idx + 1; i < (*n_tokens - 1); i++)
            tokens[i] = tokens[i + 1];
        (*n_tokens)--;
    }

    free(str_buffer);
}

// ----------------------------------------------------------------------------
#include "lfm_tokenizer.inc"
void engine_enable_lfm_tokenizer(void){s_lfm=1;}
// Sampler

static int sample_argmax(const float *probabilities, int n)
{
    int max_i = 0;
    float max_p = probabilities[0];
    for (int i = 1; i < n; i++) {
        if (probabilities[i] > max_p) {
            max_i = i;
            max_p = probabilities[i];
        }
    }
    return max_i;
}

static int sample_mult(const float *probabilities, int n, float coin)
{
    float cdf = 0.0f;
    for (int i = 0; i < n; i++) {
        cdf += probabilities[i];
        if (coin < cdf)
            return i;
    }
    return n - 1;
}

static int compare(const void *a, const void *b)
{
    const ProbIndex *a_ = (const ProbIndex *)a;
    const ProbIndex *b_ = (const ProbIndex *)b;
    if (a_->prob > b_->prob) return -1;
    if (a_->prob < b_->prob) return 1;
    return 0;
}

static int sample_topp(float *probabilities, int n, float topp, ProbIndex *probindex,
                       float coin)
{
    int n0 = 0;
    const float cutoff = (1.0f - topp) / (n - 1);
    for (int i = 0; i < n; i++) {
        if (probabilities[i] >= cutoff) {
            probindex[n0].index = i;
            probindex[n0].prob = probabilities[i];
            n0++;
        }
    }
    qsort(probindex, (size_t)n0, sizeof(ProbIndex), compare);

    float cumulative_prob = 0.0f;
    int last_idx = n0 - 1;
    for (int i = 0; i < n0; i++) {
        cumulative_prob += probindex[i].prob;
        if (cumulative_prob > topp) {
            last_idx = i;
            break;
        }
    }

    float r = coin * cumulative_prob;
    float cdf = 0.0f;
    for (int i = 0; i <= last_idx; i++) {
        cdf += probindex[i].prob;
        if (r < cdf)
            return probindex[i].index;
    }
    return probindex[last_idx].index;
}

static unsigned int random_u32(unsigned long long *state)
{
    *state ^= *state >> 12;
    *state ^= *state << 25;
    *state ^= *state >> 27;
    return (unsigned int)((*state * 0x2545F4914F6CDD1Dull) >> 32);
}

static float random_f32(unsigned long long *state)
{
    return (random_u32(state) >> 8) / 16777216.0f;
}

int engine_build_sampler(Sampler *sampler, int vocab_size, float temperature,
                                float topp, float repeat_penalty, unsigned long long rng_seed)
{
    memset(sampler, 0, sizeof(*sampler));
    sampler->vocab_size = vocab_size;
    sampler->temperature = temperature;
    sampler->topp = topp;
    sampler->top_k = 0;                  /* 0 = disabled (auto)            */
    sampler->repeat_penalty = repeat_penalty;
    sampler->repeat_last_n = 64;
    sampler->repeat_head = 0;
    sampler->repeat_count = 0;
    sampler->rng_state = rng_seed;
    sampler->probindex = malloc((size_t)vocab_size * sizeof(ProbIndex));
    sampler->repeat_tokens = malloc((size_t)sampler->repeat_last_n * sizeof(int));
    if (!sampler->probindex || !sampler->repeat_tokens) {
        engine_free_sampler(sampler);
        return -1;
    }
    return 0;
}

void engine_free_sampler(Sampler *sampler)
{
    free(sampler->probindex);
    free(sampler->repeat_tokens);
    memset(sampler, 0, sizeof(*sampler));
}

void engine_sampler_record(Sampler *sampler, int token)
{
    if (sampler->repeat_last_n <= 0 || !sampler->repeat_tokens)
        return;
    sampler->repeat_tokens[sampler->repeat_head % sampler->repeat_last_n] = token;
    sampler->repeat_head++;
    sampler->repeat_count++;
}

/* Lower logits of tokens seen in the recent context (before sampling). */
static void apply_repeat_penalty(Sampler *sampler, float *logits)
{
    if (sampler->repeat_penalty == 1.0f || sampler->repeat_count <= 0)
        return;
    float rp = sampler->repeat_penalty;
    int n = sampler->repeat_count;
    int cap = sampler->repeat_last_n;
    for (int i = 0; i < n; i++) {
        int t = sampler->repeat_tokens[i % cap];
        if (t < 0 || t >= sampler->vocab_size)
            continue;
        if (logits[t] > 0.0f)
            logits[t] /= rp;
        else
            logits[t] *= rp;
    }
}

/* Keep only the k highest logits; the rest become -inf so they can never
 * be sampled. Mirrors llama.cpp's top-k sampler for small models.        */
static void apply_top_k(Sampler *sampler, float *logits)
{
    int n = sampler->vocab_size;
    int k = sampler->top_k;
    if (k <= 1 || k >= n)
        return;

    /* threshold = the k-th largest logit */
    for (int i = 0; i < n; i++) {
        sampler->probindex[i].index = i;
        sampler->probindex[i].prob = logits[i];
    }
    qsort(sampler->probindex, (size_t)n, sizeof(ProbIndex), compare);
    float threshold = sampler->probindex[k - 1].prob;
    for (int i = 0; i < n; i++) {
        if (logits[i] < threshold)
            logits[i] = -1e30f;
    }
}

int engine_sample(Sampler *sampler, float *logits)
{
    int next;
    apply_repeat_penalty(sampler, logits);
    if (sampler->temperature == 0.0f) {
        next = sample_argmax(logits, sampler->vocab_size);
    } else {
        apply_top_k(sampler, logits);
        for (int q = 0; q < sampler->vocab_size; q++)
            logits[q] /= sampler->temperature;
        softmax(logits, sampler->vocab_size);
        float coin = random_f32(&sampler->rng_state);
        if (sampler->topp <= 0 || sampler->topp >= 1) {
            next = sample_mult(logits, sampler->vocab_size, coin);
        } else {
            next = sample_topp(logits, sampler->vocab_size, sampler->topp,
                               sampler->probindex, coin);
        }
    }
    return next;
}

#include "batch4-forward.inc"

/* Intermediate prompt logits have no consumer. All transformer/state work still runs. */
float*engine_prefill(Transformer*t,int token,int pos,int need_logits){
 int saved=s_compute_vocab_head;s_compute_vocab_head=need_logits!=0;
 float*result=engine_forward(t,token,pos);s_compute_vocab_head=saved;return result;
}
float*engine_prefill_batch4(Transformer*t,const int*tokens,int pos,int need_logits){
 int saved=s_compute_vocab_head;s_compute_vocab_head=need_logits!=0;
 float*result=engine_forward_batch4(t,tokens,pos);s_compute_vocab_head=saved;return result;
}

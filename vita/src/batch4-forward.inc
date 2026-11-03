/* Layer-major causal prefill. Every layer/state update runs for all four tokens.
 * Intermediate prompt batches may omit their unused vocabulary projection. */
typedef struct {
 float *storage,*scales,*x,*q,*k,*v,*proj,*hb,*hb2,*logits;
 int8_t *quant;
 QuantizedTensor xq[4],hq[4];
 int dim,hidden,kv,vocab;
} BatchWorkspace;
static BatchWorkspace s_batch;
static int s_batch_enabled=1;
void engine_set_batch_enabled(int on){s_batch_enabled=on!=0;}
static void engine_batch_free(void){free(s_batch.storage);free(s_batch.scales);free(s_batch.quant);memset(&s_batch,0,sizeof(s_batch));}
int engine_batch_prepare(Transformer *t){
 Config *p=&t->config;int dim=p->dim,h=p->hidden_dim,kv=dim*p->n_kv_heads/p->n_heads;
 if(GS!=64 || dim<=0 || h<=0 || dim%64 || h%64)return -1;
 if(s_batch.storage)return 0;
 uint64_t at=sceKernelGetProcessTimeWide();
 size_t count=4*((size_t)3*dim+2*(size_t)kv+2*(size_t)h+p->vocab_size);
 if(count>SIZE_MAX/sizeof(float))return -1;
 s_batch.storage=calloc(count,sizeof(float));
 s_batch.scales=calloc(4*((size_t)dim+h)/64,sizeof(float));
 s_batch.quant=memalign(64,4*((size_t)dim+h));
 if(!s_batch.storage||!s_batch.scales||!s_batch.quant){engine_batch_free();return -1;}
 float *cur=s_batch.storage;
 s_batch.x=cur;cur+=4*dim;s_batch.q=cur;cur+=4*dim;
 s_batch.k=cur;cur+=4*kv;s_batch.v=cur;cur+=4*kv;
 s_batch.proj=cur;cur+=4*dim;s_batch.hb=cur;cur+=4*h;
 s_batch.hb2=cur;cur+=4*h;s_batch.logits=cur;
 for(int b=0;b<4;b++){
  s_batch.xq[b].q=s_batch.quant+b*dim;s_batch.xq[b].s=s_batch.scales+b*dim/64;
  s_batch.hq[b].q=s_batch.quant+4*dim+b*h;s_batch.hq[b].s=s_batch.scales+4*dim/64+b*h/64;
 }
 s_batch.dim=dim;s_batch.hidden=h;s_batch.kv=kv;s_batch.vocab=p->vocab_size;
 dbg_log("BATCH workspace_bytes=%u allocation_us=%llu activation_alignment=64",(unsigned)(count*4+4*((size_t)dim+h)+4*((size_t)dim+h)/64*4),(unsigned long long)(sceKernelGetProcessTimeWide()-at));
 return 0;
}
static void batch_embedding(float *x,const TransformerWeights *w,int token,int dim){
 model_embedding(x,w,token,dim);
}
float *engine_forward_batch4(Transformer *t,const int tokens[4],int pos){
 Config *p=&t->config;TransformerWeights *w=&t->weights;RunState *s=&t->state;BatchWorkspace *b=&s_batch;
 int dim=p->dim,hid=p->hidden_dim,kv=dim*p->n_kv_heads/p->n_heads,head=dim/p->n_heads,ctx=s->ctx_len,mul=p->n_heads/p->n_kv_heads;
 if(!s_batch_enabled||!b->storage||GS!=64||ctx<4||pos<0||pos>ctx-4)return NULL;
 for(int j=0;j<4;j++)if(tokens[j]<0||tokens[j]>=p->vocab_size)return NULL;
 uint64_t ft=sceKernelGetProcessTimeWide();prof_calls+=4;prof_batch_calls++;
 float inv_head=1.0f/sqrtf((float)head);
 TIMED(30,P_EMBED,for(int j=0;j<4;j++)batch_embedding(b->x+j*dim,w,tokens[j],dim););
 for(int l=0;l<p->n_layers;l++){
  uint64_t lt=sceKernelGetProcessTimeWide();
  for(int j=0;j<4;j++){
   TIMED(l,P_NORM,rmsnorm(s->xb,b->x+j*dim,w->rms_att_weight+l*dim,dim););
   TIMED(l,P_QUANT,quantize(b->xq+j,s->xb,dim););
  }
  if(s_lfm&&lfm_conv_layer(l)){
   TIMED(l,P_CONV_IN,matmul_batch4(s_conv_bcx,b->xq,w->wq+l,dim,3*dim););
   for(int j=0;j<4;j++){TIMED(l,P_CONV_STEP,lfm_conv_step(s->xb,s_conv_bcx+j*3*dim,l,dim););TIMED(l,P_QUANT,quantize(b->xq+j,s->xb,dim););}
  }else{
  TIMED(l,P_Q,matmul_batch4(b->q,b->xq,w->wq+l,dim,dim););
  TIMED(l,P_K,matmul_batch4(b->k,b->xq,w->wk+l,dim,kv););
  TIMED(l,P_V,matmul_batch4(b->v,b->xq,w->wv+l,dim,kv););
  for(int j=0;j<4;j++){
   int position=pos+j;float *q=b->q+j*dim,*k=b->k+j*kv,*v=b->v+j*kv;
   TIMED(l,P_QKNORM,apply_qk_norm(q,k,l,p););
   TIMED(l,P_ROPE,{
    int ridx=position*head;
    for(int i=0;i<dim;i+=2){int hd=i%head;float cr=s->rope_c[ridx+hd],ci=s->rope_s[ridx+hd];int count=i<kv?2:1;
     for(int z=0;z<count;z++){float *vec=z==0?q:k;float v0=vec[i],v1=vec[i+1];vec[i]=v0*cr-v1*ci;vec[i+1]=v0*ci+v1*cr;}
    }
   });
   int loff=(s_lfm?l/2-1:l)*ctx*kv;
   TIMED(l,P_KV,memcpy(s->key_cache+loff+position*kv,k,kv*4);memcpy(s->value_cache+loff+position*kv,v,kv*4););
   for(int h=0;h<p->n_heads;h++){
    float *query=q+h*head,*att=s->att+h*ctx;
    TIMED(l,P_SCORE,for(int time=0;time<=position;time++){
     float *key=s->key_cache+loff+time*kv+(h/mul)*head;float score=0;
     for(int i=0;i<head;i++)score+=query[i]*key[i];att[time]=score*inv_head;
    });
    TIMED(l,P_SOFTMAX,softmax(att,position+1););
    float *xb=s->xb+h*head;
    TIMED(l,P_VALUE,memset(xb,0,head*4);for(int time=0;time<=position;time++){
     float *value=s->value_cache+loff+time*kv+(h/mul)*head;float a=att[time];for(int i=0;i<head;i++)xb[i]+=a*value[i];
    });
   }
   TIMED(l,P_QUANT,quantize(b->xq+j,s->xb,dim););
  }
  }
  TIMED(l,P_OUT,matmul_batch4(b->proj,b->xq,w->wo+l,dim,dim););
  for(int j=0;j<4;j++){
   float *x=b->x+j*dim,*proj=b->proj+j*dim;
   TIMED(l,P_RESID,for(int i=0;i<dim;i++)x[i]+=proj[i];);
   TIMED(l,P_NORM,rmsnorm(s->xb,x,w->rms_ffn_weight+l*dim,dim););
   TIMED(l,P_QUANT,quantize(b->xq+j,s->xb,dim););
  }
  TIMED(l,P_GATE,matmul_batch4(b->hb,b->xq,w->w1+l,dim,hid););
  TIMED(l,P_UP,matmul_batch4(b->hb2,b->xq,w->w3+l,dim,hid););
  for(int j=0;j<4;j++){
   float *hb=b->hb+j*hid,*hb2=b->hb2+j*hid;
   TIMED(l,P_ACT,for(int i=0;i<hid;i++){float val=hb[i];val*=1.0f/(1.0f+expf(-val));val*=hb2[i];hb[i]=val;});
   TIMED(l,P_QUANT,quantize(b->hq+j,hb,hid););
  }
  TIMED(l,P_DOWN,matmul_batch4(b->proj,b->hq,w->w2+l,hid,dim););
  TIMED(l,P_RESID,for(int j=0;j<4;j++)for(int i=0;i<dim;i++)b->x[j*dim+i]+=b->proj[j*dim+i];);
  prof_layer[l]+=sceKernelGetProcessTimeWide()-lt;
 }
 for(int j=3;j<4;j++){
  float *x=b->x+j*dim;
  TIMED(30,P_FINAL,rmsnorm(x,x,w->rms_final_weight,dim););
  TIMED(30,P_QUANT,quantize(b->xq+j,x,dim););
 }
 if(s_compute_vocab_head){TIMED(30,P_LOGITS,matmul(b->logits+(size_t)3*p->vocab_size,b->xq+3,w->wcls,dim,p->vocab_size););}
 TIMED(30,P_COPY,memcpy(s->x,b->x+3*dim,dim*4););
 prof_forward_us+=sceKernelGetProcessTimeWide()-ft;
 return b->logits+(size_t)3*p->vocab_size;
}

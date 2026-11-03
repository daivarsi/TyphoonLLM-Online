/* Compact checkpoint of a known fixed prefix. Future KV slots need not be
 * copied: causal attention never reads them until overwritten. All recurrent
 * convolution delay lines are copied; scratch activations/logits are not state.
 * Same loaded model/config only, one live engine, all workers already joined. */
size_t engine_prefix_bytes(const Transformer*t,int n){
 if(!t||n<1||n>t->state.ctx_len)return 0;int kv=t->config.dim*t->config.n_kv_heads/t->config.n_heads,layers=s_lfm?6:t->config.n_layers;
 return sizeof(s_conv_state)+2*(size_t)layers*n*kv*sizeof(float);
}
static int prefix_copy(Transformer*t,int n,void*buffer,size_t bytes,int restore){
 size_t wanted=engine_prefix_bytes(t,n);if(!wanted||!buffer||bytes!=wanted)return -1;
 char*p=buffer;int kv=t->config.dim*t->config.n_kv_heads/t->config.n_heads,layers=s_lfm?6:t->config.n_layers;
 if(restore)memcpy(s_conv_state,p,sizeof(s_conv_state));else memcpy(p,s_conv_state,sizeof(s_conv_state));p+=sizeof(s_conv_state);
 for(int l=0;l<layers;l++)for(int v=0;v<2;v++){
  float*cache=(v?t->state.value_cache:t->state.key_cache)+(size_t)l*t->state.ctx_len*kv;size_t size=(size_t)n*kv*sizeof(float);
  if(restore)memcpy(cache,p,size);else memcpy(p,cache,size);p+=size;
 }
 return 0;
}
int engine_prefix_save(Transformer*t,int n,void*b,size_t bytes){return prefix_copy(t,n,b,bytes,0);}
int engine_prefix_restore(Transformer*t,int n,void*b,size_t bytes){return prefix_copy(t,n,b,bytes,1);}

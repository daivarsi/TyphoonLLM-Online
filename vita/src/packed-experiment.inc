/* Layout experiment only: actual resident matrices, one-time packing outside
 * measured kernel time. Does not change inference's model format or path. */
static void packed_core(float *out,const QuantizedTensor *x,const unsigned char *packed,int n,int d){
 int groups=n/64;
 for(int i=0;i<d;i++){
  float val=0;const unsigned char *row=packed+(size_t)i*groups*68;
  for(int g=0;g<groups;g++){
   float scale;memcpy(&scale,row+g*68+64,4);
   int32_t dot=dot64_neon(x->q+g*64,(const int8_t*)row+g*68);
   val+=(float)dot*scale*x->s[g];
  }out[i]=val;
 }
}
void engine_layout_experiment(Transformer *t){
 Config *c=&t->config;TransformerWeights *w=&t->weights;
 if(!w->wq->s){dbg_log("LAYOUT planar-only probe skipped for packed model");return;}
 const QuantizedTensor *matrices[]={w->wq,w->w1,w->w2,w->wcls};
 int ns[]={c->dim,c->dim,c->hidden_dim,c->dim};
 int ds[]={c->dim,c->hidden_dim,c->dim,c->vocab_size};
 const char *names[]={"q","gate","down","logits"};
 for(int m=0;m<4;m++){
  int n=ns[m],d=ds[m],groups=n/64;
  size_t bytes=(size_t)n*d+(size_t)n*d/64*4;
  uint64_t at=sceKernelGetProcessTimeWide();
  unsigned char *packed=malloc(bytes);float *a=malloc(d*4),*b=malloc(d*4),*xf=malloc(n*4);
  QuantizedTensor x={malloc(n),malloc(n/64*4)};
  if(!packed||!a||!b||!xf||!x.q||!x.s){dbg_log("LAYOUT allocation_failed matrix=%s",names[m]);free(packed);free(a);free(b);free(xf);free(x.q);free(x.s);continue;}
  uint64_t alloc=sceKernelGetProcessTimeWide()-at;
  for(int i=0;i<n;i++)xf[i]=(float)((i*17)%127-63)/64;
  quantize(&x,xf,n);at=sceKernelGetProcessTimeWide();
  for(int i=0;i<d;i++)for(int g=0;g<groups;g++){
   unsigned char *p=packed+((size_t)i*groups+g)*68;
   memcpy(p,matrices[m]->q+(size_t)i*n+g*64,64);
   memcpy(p+64,matrices[m]->s+(size_t)i*groups+g,4);
  }
  dbg_log("LAYOUT matrix=%s alloc_us=%llu pack_us=%llu bytes=%u",names[m],(unsigned long long)alloc,(unsigned long long)(sceKernelGetProcessTimeWide()-at),(unsigned)bytes);
  for(int trial=0;trial<4;trial++){
   at=sceKernelGetProcessTimeWide();matmul_core(a,&x,matrices[m],n,0,d);uint64_t original=sceKernelGetProcessTimeWide()-at;
   at=sceKernelGetProcessTimeWide();packed_core(b,&x,packed,n,d);uint64_t candidate=sceKernelGetProcessTimeWide()-at;
   dbg_log("LAYOUT matrix=%s trial=%d original_us=%llu packed_us=%llu exact=%d threads=1 cpu=%d bus=%d gpu=%d xbar=%d",names[m],trial,(unsigned long long)original,(unsigned long long)candidate,memcmp(a,b,d*4)==0,scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
  }
  free(packed);free(a);free(b);free(xf);free(x.q);free(x.s);
 }
}

void engine_prefetch_probe(Transformer *t){
 int saved_distance=s_prefetch_distance,saved_threads=s_mm_threads;s_mm_threads=3;
 Config *c=&t->config;TransformerWeights *w=&t->weights;
 const QuantizedTensor *matrices[]={w->wq,w->w1,w->w2,w->wcls};
 int ns[]={c->dim,c->dim,c->hidden_dim,c->dim},ds[]={c->dim,c->hidden_dim,c->dim,c->vocab_size};
 const char *names[]={"q","gate","down","logits"};const int distances[]={0,1,2,4};
 for(int m=0;m<4;m++){
  int n=ns[m],d=ds[m];float *xf=malloc(n*4),*out=malloc(d*4),*ref=malloc(d*4);QuantizedTensor x={malloc(n),malloc(n/64*4)};
  if(!xf||!out||!ref||!x.q||!x.s){free(xf);free(out);free(ref);free(x.q);free(x.s);continue;}
  for(int i=0;i<n;i++)xf[i]=(float)((i*17)%127-63)/64;
  quantize(&x,xf,n);s_prefetch_distance=0;matmul(ref,&x,matrices[m],n,d);
  for(int trial=0;trial<4;trial++)for(int v=0;v<4;v++){
   s_prefetch_distance=distances[(v+trial)%4];
   uint64_t at=sceKernelGetProcessTimeWide();matmul(out,&x,matrices[m],n,d);uint64_t us=sceKernelGetProcessTimeWide()-at;
   dbg_log("PREFETCH matrix=%s distance_groups=%d trial=%d us=%llu exact=%d threads=3 cpu=%d bus=%d gpu=%d xbar=%d",names[m],s_prefetch_distance,trial,(unsigned long long)us,memcmp(out,ref,d*4)==0,scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
  }
  free(xf);free(out);free(ref);free(x.q);free(x.s);
 }
 s_prefetch_distance=saved_distance;s_mm_threads=saved_threads;
}

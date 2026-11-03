void engine_batch_probe(Transformer *t){
 int saved=s_mm_threads;s_mm_threads=3;Config *c=&t->config;TransformerWeights *w=&t->weights;
 const QuantizedTensor *matrices[]={w->wq,w->w1,w->w2,w->wcls};
 int ns[]={c->dim,c->dim,c->hidden_dim,c->dim},ds[]={c->dim,c->hidden_dim,c->dim,c->vocab_size};
 const char *names[]={"q","gate","down","logits"};
 for(int m=0;m<4;m++){
  int n=ns[m],d=ds[m];float *xf=malloc(n*4),*out=malloc(4*d*4),*ref=malloc(4*d*4);int8_t *qx=malloc(4*n);float *xs=malloc(4*n/64*4);QuantizedTensor bx[4];
  if(!xf||!out||!ref||!qx||!xs){free(xf);free(out);free(ref);free(qx);free(xs);continue;}
  for(int b=0;b<4;b++){bx[b].q=qx+b*n;bx[b].s=xs+b*n/64;for(int i=0;i<n;i++)xf[i]=(float)((i*17+b*11)%127-63)/64;quantize(bx+b,xf,n);}
  for(int trial=0;trial<4;trial++){
   uint64_t at=sceKernelGetProcessTimeWide();for(int b=0;b<4;b++)matmul(ref+b*d,bx+b,matrices[m],n,d);uint64_t seq=sceKernelGetProcessTimeWide()-at;
   at=sceKernelGetProcessTimeWide();matmul_batch4(out,bx,matrices[m],n,d);uint64_t bat=sceKernelGetProcessTimeWide()-at;
   dbg_log("BATCH_PROBE matrix=%s trial=%d sequential4_us=%llu batch4_us=%llu exact=%d threads=3 cpu=%d bus=%d gpu=%d xbar=%d",names[m],trial,(unsigned long long)seq,(unsigned long long)bat,memcmp(out,ref,4*d*4)==0,scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
  }
  free(xf);free(out);free(ref);free(qx);free(xs);
 }
 s_mm_threads=saved;
}

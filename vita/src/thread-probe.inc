void engine_thread_probe(Transformer *t){
 int saved=s_mm_threads;
 for(int threads=2;threads<=3;threads++){
  s_mm_threads=threads;if(engine_start_mm_pool()<0){dbg_log("SYNC pool_unavailable");continue;}
  s_mm_n=0;s_mm_d=0;s_mm_out=NULL;s_mm_x=NULL;s_mm_w=NULL;
  uint64_t at=sceKernelGetProcessTimeWide();
  for(int trial=0;trial<1000;trial++){
   for(int i=0;i<threads-1;i++)sceKernelSignalSema(s_mm_sem[i],1);
   for(int i=0;i<threads-1;i++)sceKernelWaitSema(s_mm_done,1,NULL);
  }
  dbg_log("SYNC threads=%d empty_dispatch_us=%llu repeats=1000",threads,(unsigned long long)(sceKernelGetProcessTimeWide()-at));
 }
 Config *c=&t->config;TransformerWeights *w=&t->weights;
 const QuantizedTensor *matrices[]={w->wk,w->wq,w->w1,w->w2,w->wcls};
 int ns[]={c->dim,c->dim,c->dim,c->hidden_dim,c->dim};
 int ds[]={c->dim*c->n_kv_heads/c->n_heads,c->dim,c->hidden_dim,c->dim,c->vocab_size};
 const char*names[]={"k","q","gate","down","logits"};
 for(int m=0;m<5;m++){
  int n=ns[m],d=ds[m];float *xf=malloc(n*4),*out=malloc(d*4);QuantizedTensor x={malloc(n),malloc(n/64*4)};
  if(!xf||!out||!x.q||!x.s){free(xf);free(out);free(x.q);free(x.s);continue;}
  for(int i=0;i<n;i++)xf[i]=(float)((i*17)%127-63)/64;
  quantize(&x,xf,n);
  for(int threads=1;threads<=3;threads++){
   s_mm_threads=threads;
   for(int trial=0;trial<4;trial++){
    uint64_t at=sceKernelGetProcessTimeWide();matmul(out,&x,matrices[m],n,d);uint64_t us=sceKernelGetProcessTimeWide()-at;
    double checksum=0;for(int i=0;i<d;i++)checksum+=out[i];
    dbg_log("THREAD_KERNEL matrix=%s threads=%d trial=%d us=%llu checksum=%.9g cpu=%d bus=%d gpu=%d xbar=%d",names[m],threads,trial,(unsigned long long)us,checksum,scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
   }
  }
  free(xf);free(out);free(x.q);free(x.s);
 }
 s_mm_threads=saved;
}

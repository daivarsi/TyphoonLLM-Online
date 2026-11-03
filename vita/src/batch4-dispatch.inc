static uint64_t s_batch_parallel_calls[4];
static void matmul_batch4(float *out,const QuantizedTensor batch[4],const QuantizedTensor *w,int n,int d){
 const QuantizedTensor *bx[4]={batch,batch+1,batch+2,batch+3};
 if(GS!=64){for(int b=0;b<4;b++)matmul(out+b*d,bx[b],w,n,d);return;}
 if(s_mm_threads==1 || (size_t)n*d<65536 || engine_start_mm_pool()<0){batch4_core(out,bx,w,n,d,0,d);return;}
 s_batch_parallel_calls[s_mm_threads]++;
 s_mm_out=out;s_mm_w=w;s_mm_n=n;s_mm_d=d;s_mm_batch_job=1;
 for(int b=0;b<4;b++)s_mm_batch_x[b]=bx[b];
 for(int i=0;i<s_mm_threads-1;i++)sceKernelSignalSema(s_mm_sem[i],1);
 batch4_core(out,bx,w,n,d,d*(s_mm_threads-1)/s_mm_threads,d);
 for(int i=0;i<s_mm_threads-1;i++)sceKernelWaitSema(s_mm_done,1,NULL);
}

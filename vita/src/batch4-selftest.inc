int engine_batch_kernel_selftest(void){
 int saved=s_mm_threads,oldgs=GS,pool_was_ready=s_mm_ready;GS=64;int cases=0,failures=0;uint32_t rng=1234;
 uint64_t before2=s_batch_parallel_calls[2],before3=s_batch_parallel_calls[3];
 const int widths[]={64,128,576,1536};
 for(int z=0;z<4;z++)for(int pat=0;pat<4;pat++)for(int off=0;off<2;off++){
  int n=widths[z],d=129,groups=n/64;
  int8_t *qx=malloc(4*(n+1)),*qw=malloc(n*d+1),*qp=malloc((size_t)n*d/64*68+1);
  float *xs=malloc(4*groups*4),*ws=malloc(n*d/64*4),*out=malloc(4*d*4),*ref=malloc(4*d*4);
  if(!qx||!qw||!qp||!xs||!ws||!out||!ref){free(qx);free(qw);free(qp);free(xs);free(ws);free(out);free(ref);s_mm_threads=saved;GS=oldgs;return -1;}
  QuantizedTensor bx[4];int8_t *w=qw+off,*packed=qp+off;
  #define BRAND() (rng=rng*1664525u+1013904223u)
  for(int b=0;b<4;b++){
   bx[b].q=qx+b*(n+1)+off;bx[b].s=xs+b*groups;
   for(int i=0;i<n;i++)bx[b].q[i]=pat==0?-128:pat==1?127:pat==2?0:(int8_t)(BRAND()>>16);
   for(int g=0;g<groups;g++)bx[b].s[g]=(BRAND()%10007+1)/10009.0f;
  }
  for(int i=0;i<n*d;i++)w[i]=pat==0?-128:pat==1?127:pat==2?0:(int8_t)(BRAND()>>16);
  for(int g=0;g<n*d/64;g++)ws[g]=(BRAND()%983+1)/997.0f;
  for(int b=0;b<4;b++)for(int i=0;i<d;i++){
   float v=0;for(int g=0;g<groups;g++){int32_t dot=0;for(int k=0;k<64;k++)dot+=(int32_t)bx[b].q[g*64+k]*w[i*n+g*64+k];v+=(float)dot*ws[i*groups+g]*bx[b].s[g];}ref[b*d+i]=v;
  }
  for(int g=0;g<n*d/64;g++){memcpy(packed+g*68,w+g*64,64);memcpy(packed+g*68+64,ws+g,4);}
  for(int format=0;format<2;format++)for(int threads=1;threads<=3;threads++){
   QuantizedTensor wt={format?packed:w,format?NULL:ws};s_mm_threads=threads;
   matmul_batch4(out,bx,&wt,n,d);
   for(int i=0;i<4*d;i++){if(memcmp(out+i,ref+i,4))failures++;cases++;}
  }
  #undef BRAND
  free(qx);free(qw);free(qp);free(xs);free(ws);free(out);free(ref);
 }
 s_mm_threads=saved;GS=oldgs;if(!pool_was_ready)mm_pool_destroy();
 uint64_t par2=s_batch_parallel_calls[2]-before2,par3=s_batch_parallel_calls[3]-before3;
 #ifdef __ARM_NEON
 if(!par2||!par3)failures++;
 #endif
 dbg_log("BATCH_KERNEL cases=%d failures=%d formats=planar,packed threads=1,2,3 comparison=bitwise unaligned=yes parallel2_calls=%llu parallel3_calls=%llu",cases,failures,(unsigned long long)par2,(unsigned long long)par3);return failures;
}

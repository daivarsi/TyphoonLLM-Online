int engine_q4_kernel_selftest(void){
 const uint16_t halves[]={0x0000,0x8000,0x3c00,0xbc00,0x1400,0x0001,0x8001,0x7bff};
 const float values[]={0.0f,-0.0f,1.0f,-1.0f,0x1p-10f,0x1p-24f,-0x1p-24f,65504.0f};
 int failures=0,cases=0,saved=s_mm_threads,was_ready=s_mm_ready;uint32_t rng=1234;
 for(int i=0;i<8;i++){float f=q4_half(halves[i]);if(memcmp(&f,values+i,4))failures++;cases++;}
 uint64_t before2=s_batch_parallel_calls[2],before3=s_batch_parallel_calls[3];
 const int widths[]={64,128,576,1536};
 for(int z=0;z<4;z++)for(int pat=0;pat<4;pat++)for(int off=0;off<2;off++){
  int n=widths[z],d=129,groups=n/32;
  int8_t *raw=malloc(n*d),*packed_base=malloc((size_t)n*d/32*18+1),*qx=malloc(4*(n+1));
  float *ws=malloc(n*d/32*4),*xs=malloc(4*n/64*4),*out=malloc(4*d*4),*ref=malloc(4*d*4),*one=malloc(d*4);
  if(!raw||!packed_base||!qx||!ws||!xs||!out||!ref||!one){free(raw);free(packed_base);free(qx);free(ws);free(xs);free(out);free(ref);free(one);return -1;}
  int8_t *packed=packed_base+off;QuantizedTensor bx[4];
  #define Q4RAND() (rng=rng*1664525u+1013904223u)
  for(int i=0;i<n*d;i++)raw[i]=pat==0?-8:pat==1?7:pat==2?0:(int8_t)((Q4RAND()>>16)%16-8);
  for(int g=0;g<n*d/32;g++){
   int scale=g%8;memcpy(packed+g*18,halves+scale,2);ws[g]=values[scale];
   for(int k=0;k<16;k++)packed[g*18+2+k]=(int8_t)((raw[g*32+k]+8)|((raw[g*32+k+16]+8)<<4));
  }
  for(int b=0;b<4;b++){
   bx[b].q=qx+b*(n+1)+off;bx[b].s=xs+b*n/64;bx[b].bits=0;
   for(int i=0;i<n;i++)bx[b].q[i]=pat==0?-128:pat==1?127:pat==2?0:(int8_t)(Q4RAND()>>16);
   for(int g=0;g<n/64;g++)bx[b].s[g]=(Q4RAND()%10007+1)/10009.0f;
   for(int i=0;i<d;i++){float val=0;for(int g=0;g<groups;g++){int32_t dot=0;for(int k=0;k<32;k++)dot+=(int32_t)raw[i*n+g*32+k]*bx[b].q[g*32+k];val+=(float)dot*ws[i*groups+g]*bx[b].s[g/2];}ref[b*d+i]=val;}
  }
  QuantizedTensor w={packed,ws,4};
  for(int threads=1;threads<=3;threads++){
   s_mm_threads=threads;matmul(one,bx,&w,n,d);matmul_batch4(out,bx,&w,n,d);
   for(int i=0;i<d;i++){if(memcmp(one+i,ref+i,4))failures++;cases++;}
   for(int i=0;i<4*d;i++){if(memcmp(out+i,ref+i,4))failures++;cases++;}
  }
  #undef Q4RAND
  free(raw);free(packed_base);free(qx);free(ws);free(xs);free(out);free(ref);free(one);
 }
 uint64_t p2=s_batch_parallel_calls[2]-before2,p3=s_batch_parallel_calls[3]-before3;
 #ifdef __ARM_NEON
 if(!p2||!p3)failures++;
 #endif
 s_mm_threads=saved;if(!was_ready)mm_pool_destroy();
 dbg_log("Q4_KERNEL cases=%d failures=%d half_decode=known_values unaligned=yes parallel2=%llu parallel3=%llu",cases,failures,(unsigned long long)p2,(unsigned long long)p3);return failures;
}

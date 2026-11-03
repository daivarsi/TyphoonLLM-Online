/* Included immediately after the production matmul_core: tests that function,
 * not a separate candidate implementation. No model or benchmark state used. */
int engine_kernel_selftest(void){
 int oldgs=GS,failures=0,cases=0;uint32_t seed=1234;
 const int widths[]={64,128,576,1536};
 for(int gg=0;gg<2;gg++)for(int z=0;z<4;z++)for(int pat=0;pat<4;pat++)for(int off=0;off<2;off++){
  GS=gg?32:64;int n=widths[z],d=17;
  int8_t *xa=malloc(n+1),*wa=malloc(n*d+1);float *xs=malloc(n/GS*sizeof(float)),*ws=malloc(n*d/GS*sizeof(float)),out[17],reference[17];
  if(!xa||!wa||!xs||!ws){free(xa);free(wa);free(xs);free(ws);GS=oldgs;return -1;}
  int8_t *x=xa+off,*w=wa+off;
  #define KRAND() (seed=seed*1664525u+1013904223u)
  for(int i=0;i<n;i++)x[i]=pat==0?-128:pat==1?127:pat==2?0:(int8_t)(KRAND()>>16);
  for(int i=0;i<n*d;i++)w[i]=pat==0?-128:pat==1?127:pat==2?0:(int8_t)(KRAND()>>16);
  for(int i=0;i<n/GS;i++)xs[i]=(KRAND()%127+1)/128.0f;
  for(int i=0;i<n*d/GS;i++)ws[i]=(KRAND()%127+1)/128.0f;
  QuantizedTensor xt={x,xs},wt={w,ws};
  matmul_core(out,&xt,&wt,n,0,d);
  for(int i=0;i<d;i++){
   float ref=0;for(int j=0;j<n;j+=GS){int32_t sum=0;for(int k=0;k<GS;k++)sum+=(int32_t)x[j+k]*w[i*n+j+k];ref+=(float)sum*ws[(i*n+j)/GS]*xs[j/GS];}
   reference[i]=ref;
   if(memcmp(&ref,&out[i],sizeof(float)))failures++;
   cases++;
  }
  if(GS==64){
   size_t groups=(size_t)n*d/64;int8_t *pa=malloc(groups*68+1);
   if(!pa){free(xa);free(wa);free(xs);free(ws);GS=oldgs;return -1;}
   int8_t *packed=pa+off;
   for(size_t g=0;g<groups;g++){memcpy(packed+g*68,w+g*64,64);memcpy(packed+g*68+64,ws+g,4);}
   QuantizedTensor pw={packed,NULL};matmul_core(out,&xt,&pw,n,0,d);
   for(int i=0;i<d;i++){if(memcmp(reference+i,out+i,4))failures++;cases++;}
   free(pa);
  }
  free(xa);free(wa);free(xs);free(ws);
  #undef KRAND
 }
 GS=oldgs;dbg_log("KERNEL cases=%d failures=%d comparison=bitwise group_sizes=32,64 unaligned_q=yes",cases,failures);return failures;
}

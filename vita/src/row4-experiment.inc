/* Four output rows reuse each activation load. Accumulation within each
 * quantization group is INT32, with the original FP32 group order preserved. */
static void matmul_core(float *out,const QuantizedTensor *x,const QuantizedTensor *w,int n,int i0,int i1){
 if(w->bits==4){q4_rows_four(out,x,w,n,i0,i1);return;}
 if(!w->s){packed_rows_four(out,x,w,n,i0,i1);return;}
#ifdef __ARM_NEON
 if(GS==64 && n%64==0){
  int groups=n/64,i=i0;
  for(;i+3<i1;i+=4){
   float32x4_t val=vdupq_n_f32(0);
   for(int g=0;g<groups;g++){
    int ahead=s_prefetch_distance;
    if(ahead>0 && ahead<groups-g)for(int r=0;r<4;r++)
        __builtin_prefetch(w->q+(size_t)(i+r)*n+(g+ahead)*64,0,3);
    int32x4_t a0=vdupq_n_s32(0),a1=a0,a2=a0,a3=a0;
    for(int k=0;k<64;k+=16){
     int8x16_t xx=vld1q_s8(x->q+g*64+k);
     #define R4(A,R) do{int8x16_t ww=vld1q_s8(w->q+(size_t)(i+(R))*n+g*64+k); \
      A=vpadalq_s16(A,vmull_s8(vget_low_s8(xx),vget_low_s8(ww))); \
      A=vpadalq_s16(A,vmull_s8(vget_high_s8(xx),vget_high_s8(ww)));}while(0)
     R4(a0,0);R4(a1,1);R4(a2,2);R4(a3,3);
     #undef R4
    }
    int32x2_t p0=vpadd_s32(vget_low_s32(a0),vget_high_s32(a0));
    int32x2_t p1=vpadd_s32(vget_low_s32(a1),vget_high_s32(a1));
    int32x2_t p2=vpadd_s32(vget_low_s32(a2),vget_high_s32(a2));
    int32x2_t p3=vpadd_s32(vget_low_s32(a3),vget_high_s32(a3));
    int32x4_t dots=vcombine_s32(vpadd_s32(p0,p1),vpadd_s32(p2,p3));
    float32x4_t scales=vdupq_n_f32(w->s[(size_t)i*groups+g]);
    scales=vsetq_lane_f32(w->s[(size_t)(i+1)*groups+g],scales,1);
    scales=vsetq_lane_f32(w->s[(size_t)(i+2)*groups+g],scales,2);
    scales=vsetq_lane_f32(w->s[(size_t)(i+3)*groups+g],scales,3);
    val=vmlaq_f32(val,vmulq_f32(vcvtq_f32_s32(dots),scales),vdupq_n_f32(x->s[g]));
   }
   vst1q_f32(out+i,val);
  }
  matmul_rows_one(out,x,w,n,i,i1);return;
 }
#endif
 matmul_rows_one(out,x,w,n,i0,i1);
}

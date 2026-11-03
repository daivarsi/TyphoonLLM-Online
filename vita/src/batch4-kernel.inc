/* Four known prompt positions share a weight load. Each output lane retains
 * the original per-token FP32 group accumulation order. No attention here. */
static void batch4_core(float *out,const QuantizedTensor *const batch[4],const QuantizedTensor *w,int n,int d,int i0,int i1){
 if(w->bits==4){q4_batch4(out,batch,w,n,d,i0,i1);return;}
 int groups=n/64;
 for(int i=i0;i<i1;i++){
  float32x4_t val=vdupq_n_f32(0);
  for(int g=0;g<groups;g++){
   const int8_t *weight=w->s?w->q+(size_t)i*n+g*64:w->q+((size_t)i*groups+g)*68;
   float ws=w->s?w->s[(size_t)i*groups+g]:packed_scale(weight);
   int32x4_t a0=vdupq_n_s32(0),a1=a0,a2=a0,a3=a0;
   for(int k=0;k<64;k+=16){
    int8x16_t ww=vld1q_s8(weight+k);
    #define B4(A,R) do{int8x16_t xx=vld1q_s8(batch[R]->q+g*64+k); \
     A=vpadalq_s16(A,vmull_s8(vget_low_s8(xx),vget_low_s8(ww))); \
     A=vpadalq_s16(A,vmull_s8(vget_high_s8(xx),vget_high_s8(ww)));}while(0)
    B4(a0,0);B4(a1,1);B4(a2,2);B4(a3,3);
    #undef B4
   }
   int32x2_t p0=vpadd_s32(vget_low_s32(a0),vget_high_s32(a0));
   int32x2_t p1=vpadd_s32(vget_low_s32(a1),vget_high_s32(a1));
   int32x2_t p2=vpadd_s32(vget_low_s32(a2),vget_high_s32(a2));
   int32x2_t p3=vpadd_s32(vget_low_s32(a3),vget_high_s32(a3));
   int32x4_t dots=vcombine_s32(vpadd_s32(p0,p1),vpadd_s32(p2,p3));
   float32x4_t scales=vdupq_n_f32(batch[0]->s[g]);
   scales=vsetq_lane_f32(batch[1]->s[g],scales,1);
   scales=vsetq_lane_f32(batch[2]->s[g],scales,2);
   scales=vsetq_lane_f32(batch[3]->s[g],scales,3);
   val=vmlaq_f32(val,vmulq_f32(vcvtq_f32_s32(dots),vdupq_n_f32(ws)),scales);
  }
  out[i]=vgetq_lane_f32(val,0);out[d+i]=vgetq_lane_f32(val,1);
  out[2*d+i]=vgetq_lane_f32(val,2);out[3*d+i]=vgetq_lane_f32(val,3);
 }
}

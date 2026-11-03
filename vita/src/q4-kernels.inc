/* GGML Q4_0 blocks: fp16 d, 16 packed bytes for 32 weights. Scales are
 * expanded once at load; weights remain packed throughout inference. */
static int q4_code(const int8_t *b,int k){uint8_t p=(uint8_t)b[2+(k&15)];return (int)(k<16?(p&15):(p>>4))-8;}
static void q4_rows_one(float *out,const QuantizedTensor *x,const QuantizedTensor *w,int n,int i0,int i1){
 int groups=n/32;
 for(int i=i0;i<i1;i++){float val=0;for(int g=0;g<groups;g++){
  const int8_t *b=w->q+((size_t)i*groups+g)*18;int32_t dot=0;
  for(int k=0;k<32;k++)dot+=(int32_t)x->q[g*32+k]*q4_code(b,k);
  val+=(float)dot*w->s[(size_t)i*groups+g]*x->s[g/2];
 }out[i]=val;}
}
#ifdef __ARM_NEON
static inline void q4_unpack(const int8_t *b,int8x16_t *lo,int8x16_t *hi){
 uint8x16_t p=vld1q_u8((const uint8_t*)b+2),eight=vdupq_n_u8(8);
 *lo=vreinterpretq_s8_u8(vsubq_u8(vandq_u8(p,vdupq_n_u8(15)),eight));
 *hi=vreinterpretq_s8_u8(vsubq_u8(vshrq_n_u8(p,4),eight));
}
static inline int32x4_t q4_mac32(int32x4_t a,int8x16_t x0,int8x16_t x1,int8x16_t w0,int8x16_t w1){
 a=vpadalq_s16(a,vmull_s8(vget_low_s8(x0),vget_low_s8(w0)));
 a=vpadalq_s16(a,vmull_s8(vget_high_s8(x0),vget_high_s8(w0)));
 a=vpadalq_s16(a,vmull_s8(vget_low_s8(x1),vget_low_s8(w1)));
 return vpadalq_s16(a,vmull_s8(vget_high_s8(x1),vget_high_s8(w1)));
}
static inline float32x4_t q4_reduce4(int32x4_t a0,int32x4_t a1,int32x4_t a2,int32x4_t a3){
 int32x2_t p0=vpadd_s32(vget_low_s32(a0),vget_high_s32(a0)),p1=vpadd_s32(vget_low_s32(a1),vget_high_s32(a1));
 int32x2_t p2=vpadd_s32(vget_low_s32(a2),vget_high_s32(a2)),p3=vpadd_s32(vget_low_s32(a3),vget_high_s32(a3));
 return vcvtq_f32_s32(vcombine_s32(vpadd_s32(p0,p1),vpadd_s32(p2,p3)));
}
#endif
static void q4_rows_four(float *out,const QuantizedTensor *x,const QuantizedTensor *w,int n,int i0,int i1){
#ifdef __ARM_NEON
 int groups=n/32,i=i0;
 for(;i+3<i1;i+=4){float32x4_t val=vdupq_n_f32(0);
  for(int g=0;g<groups;g++){
   int ahead=s_prefetch_distance;if(ahead>0&&ahead<groups-g)for(int r=0;r<4;r++)__builtin_prefetch(w->q+((size_t)(i+r)*groups+g+ahead)*18,0,3);
   int8x16_t x0=vld1q_s8(x->q+g*32),x1=vld1q_s8(x->q+g*32+16),lo,hi;
   int32x4_t a0=vdupq_n_s32(0),a1=a0,a2=a0,a3=a0;
   #define QR(A,R) q4_unpack(w->q+((size_t)(i+(R))*groups+g)*18,&lo,&hi);A=q4_mac32(A,x0,x1,lo,hi)
   QR(a0,0);QR(a1,1);QR(a2,2);QR(a3,3);
   #undef QR
   float32x4_t scale=vdupq_n_f32(w->s[(size_t)i*groups+g]);
   scale=vsetq_lane_f32(w->s[(size_t)(i+1)*groups+g],scale,1);scale=vsetq_lane_f32(w->s[(size_t)(i+2)*groups+g],scale,2);scale=vsetq_lane_f32(w->s[(size_t)(i+3)*groups+g],scale,3);
   val=vmlaq_f32(val,vmulq_f32(q4_reduce4(a0,a1,a2,a3),scale),vdupq_n_f32(x->s[g/2]));
  }vst1q_f32(out+i,val);
 }
 q4_rows_one(out,x,w,n,i,i1);
#else
 q4_rows_one(out,x,w,n,i0,i1);
#endif
}
static void q4_batch4(float *out,const QuantizedTensor *const bx[4],const QuantizedTensor *w,int n,int d,int i0,int i1){
#ifdef __ARM_NEON
 int groups=n/32;
 for(int i=i0;i<i1;i++){float32x4_t val=vdupq_n_f32(0);
  for(int g=0;g<groups;g++){
   const int8_t *block=w->q+((size_t)i*groups+g)*18;int8x16_t lo,hi;q4_unpack(block,&lo,&hi);
   int32x4_t a0=vdupq_n_s32(0),a1=a0,a2=a0,a3=a0;
   #define QB(A,R) A=q4_mac32(A,vld1q_s8(bx[R]->q+g*32),vld1q_s8(bx[R]->q+g*32+16),lo,hi)
   QB(a0,0);QB(a1,1);QB(a2,2);QB(a3,3);
   #undef QB
   float32x4_t xs=vdupq_n_f32(bx[0]->s[g/2]);xs=vsetq_lane_f32(bx[1]->s[g/2],xs,1);xs=vsetq_lane_f32(bx[2]->s[g/2],xs,2);xs=vsetq_lane_f32(bx[3]->s[g/2],xs,3);
   val=vmlaq_f32(val,vmulq_f32(q4_reduce4(a0,a1,a2,a3),vdupq_n_f32(w->s[(size_t)i*groups+g])),xs);
  }
  out[i]=vgetq_lane_f32(val,0);out[d+i]=vgetq_lane_f32(val,1);out[2*d+i]=vgetq_lane_f32(val,2);out[3*d+i]=vgetq_lane_f32(val,3);
 }
#else
 for(int b=0;b<4;b++)q4_rows_one(out+b*d,bx[b],w,n,i0,i1);
#endif
}
static void q4_embedding(float *x,const QuantizedTensor *w,int token,int dim){
 size_t first=(size_t)token*dim/32;
 for(int i=0;i<dim;i++){size_t block=first+i/32;x[i]=(float)q4_code(w->q+block*18,i%32)*w->s[block];}
}

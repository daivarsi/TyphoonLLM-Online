#ifndef VITALM_FORMAT_H
#define VITALM_FORMAT_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
typedef struct {uint32_t d,f,l,h,k,v,ctx,bits;float theta,eps;size_t payload,tokenizer,tokenizer_size;} VitaMetadata;
static uint32_t vl_u32(const unsigned char*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t vl_u64(const unsigned char*p){return (uint64_t)vl_u32(p)|(uint64_t)vl_u32(p+4)<<32;}
static float vl_float(const unsigned char*p){uint32_t u=vl_u32(p);float f;memcpy(&f,&u,4);return f;}
/* Strict bounds before allocation or pointer arithmetic. Hash integrity is checked
 * by the host manifest/deployment SHA; this function validates memory safety. */
static int vitalm_validate(const void*bytes,size_t size,VitaMetadata*m){
 const unsigned char*p=bytes;if(!p||!m||size<256||memcmp(p,"VITALM\0\0",8))return -1;
 if(vl_u32(p+8)!=1||vl_u32(p+12)!=1||vl_u32(p+16)!=1||vl_u32(p+56)!=256)return -2;
 for(int i=60;i<72;i+=4)if(vl_u32(p+i))return -2;
 m->d=vl_u32(p+20);m->f=vl_u32(p+24);m->l=vl_u32(p+28);m->h=vl_u32(p+32);m->k=vl_u32(p+36);
 m->v=vl_u32(p+40);m->ctx=vl_u32(p+44);m->bits=vl_u32(p+48);
 if(m->d<64||m->d>1024||m->d%64||m->f<64||m->f>4096||m->f%64||!m->l||m->l>30||!m->h||m->h>16||!m->k||m->k>m->h)return -3;
 if(m->d%m->h||m->h%m->k||(m->d/m->h)%2||m->v<256||m->v>32768||!m->ctx||m->ctx>1024||(m->bits!=4&&m->bits!=8))return -3;
 m->theta=vl_float(p+72);m->eps=vl_float(p+76);
 if(!isfinite(m->theta)||m->theta<1||m->theta>1e7||!isfinite(m->eps)||m->eps<1e-8f||m->eps>1e-3f)return -4;
 uint32_t nt=vl_u32(p+52);uint64_t po=vl_u64(p+80),ko=vl_u64(p+88),ks=vl_u64(p+96),total=vl_u64(p+104);
 if(nt!=9*m->l+2||po!=((256+32*(uint64_t)nt+255)/256)*256||total!=size||ko>total||ks!=total-ko||ks>16*1024*1024||ko<po||po>size)return -5;
 uint64_t cursor=po;uint32_t kv=m->d*m->k/m->h;
 for(uint32_t i=0;i<nt;i++){
  uint32_t role,layer,r,c;
  if(i<2*m->l){role=1+i/m->l;layer=i%m->l;r=m->d;c=1;}
  else if(i==2*m->l){role=3;layer=0;r=m->d;c=1;}
  else if(i==2*m->l+1){role=4;layer=0;r=m->v;c=m->d;}
  else {uint32_t j=i-2*m->l-2;role=5+j/m->l;layer=j%m->l;r=(role==6||role==7)?kv:(role==9||role==11)?m->f:m->d;c=role==10?m->f:m->d;}
  const unsigned char*t=p+256+32*i;uint64_t n=(uint64_t)r*c;
  uint64_t want=role<=3?n*4:n/(m->bits==8?64:32)*(m->bits==8?68:18);
  if(vl_u32(t)!=role||vl_u32(t+4)!=layer||vl_u32(t+8)!=r||vl_u32(t+12)!=c||vl_u64(t+16)!=cursor||vl_u64(t+24)!=want||cursor>ko||want>ko-cursor)return -6;
  cursor+=want;
 }
 if(cursor!=ko)return -7;
 m->payload=(size_t)po;m->tokenizer=(size_t)ko;m->tokenizer_size=(size_t)ks;return 0;
}
#endif

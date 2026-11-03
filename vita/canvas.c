#include "canvas.h"
#include <string.h>
#include "font_data.inc"
static uint32_t*pix;static int cx0,cy0,cx1,cy1;
void canvas_clip(int a,int b,int c,int d){cx0=a;cy0=b;cx1=c;cy1=d;}
void canvas_begin(uint32_t*p){pix=p;canvas_clip(0,0,960,544);}
void canvas_rect(int x,int y,int w,int h,uint32_t c){int x1=x+w,y1=y+h;if(x<cx0)x=cx0;if(y<cy0)y=cy0;if(x1>cx1)x1=cx1;if(y1>cy1)y1=cy1;for(int j=y;j<y1;j++)for(int i=x;i<x1;i++)pix[j*1024+i]=c;}
void canvas_round(int x,int y,int w,int h,int r,uint32_t c){for(int j=0;j<h;j++){int inset=0;if(j<r){int q=r-j;while(inset<r&&(r-inset)*(r-inset)+q*q>r*r)inset++;}else if(j>=h-r){int q=j-(h-r);while(inset<r&&(r-inset)*(r-inset)+q*q>r*r)inset++;}canvas_rect(x+inset,y+j,w-2*inset,1,c);}}
static int next(const char**s){const unsigned char*p=(const unsigned char*)*s;unsigned c=*p++;if(c<128){*s=(const char*)p;return c;}int n=c>=0xc2&&c<=0xdf?1:c>=0xe0&&c<=0xef?2:c>=0xf0&&c<=0xf4?3:0;if(!n){*s=(const char*)p;return '?';}c&=(1u<<(6-n))-1;for(int i=0;i<n;i++){if((p[i]&0xc0)!=0x80){*s=(const char*)p;return '?';}c=(c<<6)|(p[i]&63);}*s=(const char*)p+n;return c;}
typedef struct{const unsigned char*data,*width;const signed char*kern;int w,h;} Font;
#define FACE(n) (Font){font##n##_data,font##n##_width,font##n##_kern,FONT##n##_W,FONT##n##_H}
static Font font(int size){switch(size){case 14:return FACE(14);case 16:return FACE(16);case 22:return FACE(22);case 24:return FACE(24);case 28:return FACE(28);case 32:return FACE(32);case 44:return FACE(44);default:return FACE(20);}}
static int index_of(int c){if(c>=32&&c<=126)return c-32;for(unsigned i=0;i<sizeof(font_extra)/sizeof(*font_extra);i++)if((unsigned)c==font_extra[i])return 95+i;return '?'-32;}
static const char*fraction_text(int c);
static int pair_space(int size,int a,int b){Font f=font(size);if(a>='0'&&a<='9'&&fraction_text(b))return f.width[0];return a>=32&&a<=126&&b>=32&&b<=126?f.kern[(a-32)*95+b-32]:0;}
static const char*fraction_text(int c){switch(c){case 0x00bc:return "1/4";case 0x00bd:return "1/2";case 0x00be:return "3/4";case 0x2153:return "1/3";case 0x2154:return "2/3";case 0x215b:return "1/8";case 0x215c:return "3/8";case 0x215d:return "5/8";case 0x215e:return "7/8";default:return 0;}}
static int glyph(int x,int y,int c,int size,uint32_t color,int draw){const char*fraction=fraction_text(c);if(fraction){int width=0;for(int i=0;fraction[i];i++)width+=glyph(x+width,y,fraction[i],size,color,draw);return width;}Font f=font(size);int idx=index_of(c),w=f.w,h=f.h;const unsigned char*d=f.data+idx*w*h;if(draw)for(int j=0;j<h;j++)for(int i=0;i<w;i++){int xx=x+i,yy=y+j;if(xx<cx0||xx>=cx1||yy<cy0||yy>=cy1)continue;unsigned a=d[j*w+i];if(!a)continue;uint32_t old=pix[yy*1024+xx];unsigned r=(((color&255)*a+(old&255)*(255-a))/255),g=((((color>>8)&255)*a+((old>>8)&255)*(255-a))/255),b=((((color>>16)&255)*a+((old>>16)&255)*(255-a))/255);pix[yy*1024+xx]=RGB(r,g,b);}return f.width[idx];}
int canvas_width(const char*s,int size){int n=0,prev=0;while(*s){int c=next(&s);n+=pair_space(size,prev,c)+glyph(0,0,c,size,0,0);prev=c;}return n;}
void canvas_text(int x,int y,const char*s,int size,uint32_t color){int prev=0;while(*s){int c=next(&s);x+=pair_space(size,prev,c);x+=glyph(x,y,c,size,color,1);prev=c;}}
int canvas_wrap(int x,int y,int width,const char*s,int size,uint32_t color,int draw){int xx=0,yy=0,line=size+8;while(*s){if(*s=='\n'){s++;xx=0;yy+=line;continue;}if(*s=='\r'){s++;continue;}if(*s==' '||*s=='\t'){s++;if(xx)xx+=glyph(0,0,' ',size,0,0);continue;}const char*end=s;int ww=0,prev=0;while(*end&&*end!=' '&&*end!='\n'&&*end!='\r'&&*end!='\t'){int c=next(&end);ww+=pair_space(size,prev,c)+glyph(0,0,c,size,0,0);prev=c;}if(xx&&xx+ww>width){xx=0;yy+=line;}prev=0;while(s<end){int c=next(&s),cw=glyph(0,0,c,size,0,0),k=pair_space(size,prev,c);if(xx+k+cw>width&&xx){xx=0;yy+=line;k=0;}xx+=k;glyph(x+xx,y+yy,c,size,color,draw);xx+=cw;prev=c;}}return yy+line;}

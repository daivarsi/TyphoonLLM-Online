#pragma once
#include <stdint.h>
void canvas_begin(uint32_t* pixels);
void canvas_clip(int x0,int y0,int x1,int y1);
void canvas_rect(int x,int y,int w,int h,uint32_t color);
void canvas_round(int x,int y,int w,int h,int radius,uint32_t color);
void canvas_text(int x,int y,const char*s,int size,uint32_t color);
int canvas_width(const char*s,int size);
int canvas_wrap(int x,int y,int width,const char*s,int size,uint32_t color,int draw);
#define RGB(r,g,b) (0xff000000u|((b)<<16)|((g)<<8)|(r))

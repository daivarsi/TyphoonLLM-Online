#pragma once
#include <stdint.h>
#define CHAT_TURNS 24
#define CHAT_ANSWER 8192
typedef struct {int top,follow,total;} ChatScroll;
void chat_scroll_update(ChatScroll*,int);
void chat_scroll_move(ChatScroll*,int);
void chat_scroll_latest(ChatScroll*);
int chat_message(const char*,int,int,int);
void chat_view(uint32_t*,char[][1025],char[][CHAT_ANSWER],int,const char*,const char*,int,int,int,ChatScroll*,const char*,int);

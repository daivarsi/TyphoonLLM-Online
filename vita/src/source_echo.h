#ifndef TYPHOON_SOURCE_ECHO_H
#define TYPHOON_SOURCE_ECHO_H
#include <ctype.h>
/* A literal input echo is not an answer. This never classifies a question. */
static int source_echo(const char *answer,const char *request){
 int seen=0;
 for(;;){
  while(*answer&&(isspace((unsigned char)*answer)||ispunct((unsigned char)*answer)))answer++;
  while(*request&&(isspace((unsigned char)*request)||ispunct((unsigned char)*request)))request++;
  if(!*answer||!*request)return seen&&!*answer&&!*request;
  if(tolower((unsigned char)*answer)!=tolower((unsigned char)*request))return 0;
  seen=1;answer++;request++;
 }
}
#endif

#ifndef TYPHOON_REFERENCE_HISTORY_H
#define TYPHOON_REFERENCE_HISTORY_H
#include <string.h>
/* Exact byte matching across decoded token boundaries; no topic heuristics. */
static int reference_history_contains(const char *text,char *const *vocab,int vocab_n,const int *ids,int n){
 size_t length=strlen(text);if(!length||length>1024)return 0;
 unsigned short prefix[1024];prefix[0]=0;
 for(size_t i=1,j=0;i<length;i++){while(j&&text[i]!=text[j])j=prefix[j-1];if(text[i]==text[j])j++;prefix[i]=(unsigned short)j;}
 size_t matched=0;
 for(int i=0;i<n;i++){
  if(ids[i]<0||ids[i]>=vocab_n)return 0;
  const char *piece=vocab[ids[i]];if(!piece)return 0;
  for(;*piece;piece++){while(matched&&*piece!=text[matched])matched=prefix[matched-1];if(*piece==text[matched])matched++;if(matched==length)return 1;}
 }
 return 0;
}
#endif

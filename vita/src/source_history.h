/* Sourced inference excludes old generated claims; canonical chat stays intact. */
#ifndef TYPHOON_SOURCE_HISTORY_H
#define TYPHOON_SOURCE_HISTORY_H
#include <string.h>
static int source_history_input(const int *chat,int count,const int *assistant,int prefix_count,int end_id,int *out,int capacity){
 if(count<0||prefix_count<1||capacity<count)return -1;
 int used=0;
 for(int i=0;i<count;){
  if(i+prefix_count<=count&&!memcmp(chat+i,assistant,(size_t)prefix_count*sizeof(int))){
   int end=i+prefix_count;while(end<count&&chat[end]!=end_id)end++;
   /* Only closed historical replies are omitted. Keep the current open role. */
   if(end<count){i=end+1;continue;}
  }
  out[used++]=chat[i++];
 }
 return used;
}
static int source_history_commit(int *chat,int capacity,int canonical_input,const int *fed,int fed_count,int inference_input){
 if(canonical_input<0||inference_input<0||fed_count<inference_input||canonical_input>capacity||fed_count-inference_input>capacity-canonical_input)return -1;
 int added=fed_count-inference_input;
 memcpy(chat+canonical_input,fed+inference_input,(size_t)added*sizeof(int));
 return canonical_input+added;
}
#endif

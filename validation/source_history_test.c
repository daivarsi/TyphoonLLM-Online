#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../vita/src/source_history.h"
int main(void){
 const int role[]={6,10,8};
 const int chat[]={6,20,8,40,7,8,6,30,8,41,7,8,6,10,8,99,98,7,8,6,30,8,42,7,8,6,10,8};
 int before[28],out[512],canonical[512];memcpy(before,chat,sizeof(chat));
 int n=source_history_input(chat,28,role,3,7,out,512);
 assert(n==22);assert(!memcmp(before,chat,sizeof(chat))); /* no canonical mutation */
 assert(out[3]==40&&out[9]==41&&out[16]==42); /* system, old user, new user retained */
 assert(!memcmp(out+n-3,role,sizeof(role))); /* current assistant role retained */
 for(int i=0;i<n;i++)assert(out[i]!=99&&out[i]!=98); /* old generated claims absent */
 assert(source_history_input(chat,28,role,3,7,out,27)==-1);
 assert(source_history_input(chat,-1,role,3,7,out,512)==-1);
 assert(source_history_input(chat,28,role,0,7,out,512)==-1);
 const int plain[]={6,30,8,45,7,8,6,10,8};
 assert(source_history_input(plain,9,role,3,7,out,512)==9);
 memcpy(canonical,chat,sizeof(chat));int fed[]={1,2,3,50,51};
 assert(source_history_commit(canonical,512,28,fed,5,3)==30);
 assert(!memcmp(canonical,chat,sizeof(chat))&&canonical[28]==50&&canonical[29]==51);
 /* Continuation rebuilds from the same canonical prefix, without duplication. */
 int continued[]={1,2,3,50,51,52};
 assert(source_history_commit(canonical,512,28,continued,6,3)==31&&canonical[30]==52);
 assert(source_history_commit(canonical,29,28,fed,5,3)==-1);
 assert(source_history_commit(canonical,512,28,fed,2,3)==-1);
 puts("Source-history filtering, canonical preservation, continuation and bounds checks pass");return 0;
}

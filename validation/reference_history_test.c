#include <assert.h>
#include <stdio.h>
#include "../vita/src/reference_history.h"
int main(void){
 char *v[]={"prefix abc","ab","cab tail","\xe2","\x98\x81",""};int ids[]={0,1,2,3,4};
 assert(reference_history_contains("abcabc",v,6,ids,5));
 assert(reference_history_contains("abcab tail",v,6,ids,5));
 assert(reference_history_contains("\xe2\x98\x81",v,6,ids,5));
 assert(!reference_history_contains("absent",v,6,ids,5));
 assert(!reference_history_contains("abcabcab",v,6,ids,1));
 assert(!reference_history_contains("",v,6,ids,5));
 int bad[]={7};assert(!reference_history_contains("x",v,6,bad,1));
 int neg[]={-1};assert(!reference_history_contains("x",v,6,neg,1));
 char boundary[1026];memset(boundary,'x',1025);boundary[1025]=0;
 assert(!reference_history_contains(boundary,v,6,ids,5));
 char *a[]={"x"};int many[1024]={0};boundary[1024]=0;
 assert(reference_history_contains(boundary,a,1,many,1024));
 puts("10 reference-history boundary checks passed");return 0;
}

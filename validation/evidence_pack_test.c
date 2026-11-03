#include "../vita/src/evidence_pack.h"
#include <assert.h>
int main(void){
 char s[1025],original[1025];int checks=0;
 const char*values[]={"Ingredients: 3 eggs\nSteps: Cook gently.","Tutorial excerpt: x = 3; print(x)","A single complete statement. ☁","First fact.\n\nNo detections have been confirmed.","First fact.\n\nThese depend on the previous fact."};
 for(unsigned i=0;i<sizeof(values)/sizeof(*values);i++){
  snprintf(s,sizeof(s),"%s",values[i]);snprintf(original,sizeof(original),"%s",s);evidence_pack_prepare(s,sizeof(s));assert(!strcmp(s,original));checks++;
  assert(!evidence_pack_drop_last(s,i<2));assert(!strcmp(s,original));checks++;
 }
 snprintf(s,sizeof(s),"Background facts, not live status or a complete procedure: Coffee beans contain caffeine.");evidence_pack_prepare(s,sizeof(s));assert(!strcmp(s,"Background (not live; not a complete guide):\nCoffee beans contain caffeine."));checks++;
 snprintf(s,sizeof(s),"Background facts, not live status or a complete procedure: Coffee beans contain caffeine.");snprintf(original,sizeof(original),"%s",s);evidence_pack_prepare(s,8);assert(!strcmp(s,original));checks++;
 snprintf(s,sizeof(s),"Complete first fact.\n\nComplete second fact.\n\nLast complete fact.");assert(evidence_pack_drop_last(s,0));assert(!strcmp(s,"Complete first fact.\n\nComplete second fact."));checks++;
 assert(evidence_pack_drop_last(s,0));assert(!strcmp(s,"Complete first fact."));checks++;
 assert(!evidence_pack_drop_last(s,0));checks++;
 const char*protected[]={"Ingredients: 3 eggs\n\nSteps: Cook gently.","```python\nprint(3)\n```\n\nExplanation of the code.","Oak is a tree.\n\nPine is a conifer."};
 for(unsigned j=0;j<sizeof(protected)/sizeof(*protected);j++){
  snprintf(s,sizeof(s),"%s",protected[j]);snprintf(original,sizeof(original),"%s",s);
  assert(!evidence_pack_drop_last(s,j==2?evidence_pack_comparison("Compare oak and pine"):1));assert(!strcmp(s,original));checks++;
 }
 snprintf(s,sizeof(s),"Café facts remain complete.\n\nA final optional paragraph.");assert(evidence_pack_drop_last(s,0));assert(!strcmp(s,"Café facts remain complete."));checks++;
 const char*qs[]={"COMPARE copper and iron","What is the DIFFERENCE between copper and iron?","copper versus iron","Copper vs iron"};
 for(unsigned i=0;i<sizeof(qs)/sizeof(*qs);i++){assert(evidence_pack_comparison(qs[i]));checks++;}
 assert(!evidence_pack_comparison("What is copper?"));checks++;
 printf("%d focused evidence boundary/protected-content checks pass\n",checks);return 0;
}

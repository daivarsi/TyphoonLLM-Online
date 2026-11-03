/* Bounded evidence packing: no rewriting, sentence cutting or invented facts. */
#ifndef TYPHOON_EVIDENCE_PACK_H
#define TYPHOON_EVIDENCE_PACK_H
#include <string.h>
#include <stdio.h>
#include <ctype.h>
static void evidence_pack_prepare(char *text, size_t capacity) {
 const char *old="Background facts, not live status or a complete procedure: ";
 if(!strncmp(text,old,strlen(old))){
  char tmp[1025];int n=snprintf(tmp,sizeof(tmp),"Background (not live; not a complete guide):\n%s",text+strlen(old));
  if(n<0||(size_t)n>=sizeof(tmp)||(size_t)n>=capacity)return;
  memcpy(text,tmp,(size_t)n+1);
 }
}
static int evidence_pack_comparison(const char *query) {
 char lower[1100];size_t i=0;for(;query[i]&&i+1<sizeof(lower);i++)lower[i]=(char)tolower((unsigned char)query[i]);lower[i]=0;
 return strstr(lower,"compare")!=0||strstr(lower,"difference")!=0||strstr(lower," versus ")!=0||strstr(lower," vs ")!=0;
}
static int evidence_pack_drop_last(char *text, int atomic) {
 if(atomic)return 0;
 char *cut=0;
 for(char*p=text;(p=strstr(p,"\n\n"));p+=2)if(p>text&&p[2])cut=p;
 if(!cut)return 0;
 /* Qualifiers and context-dependent units must not be dropped independently. */
 const char *last=cut+2;char lower[1025];size_t i=0;
 for(;last[i]&&i+1<sizeof(lower);i++){lower[i]=(char)tolower((unsigned char)last[i]);}
 lower[i]=0;
 const char*guards[]={"not ","no ","never ","must ","avoid ","except ","unless ","only ","warning","uncertain","unknown","unconfirmed","may ","might ","however","but ","this ","it ","they ","these ","those ",0};
 for(int g=0;guards[g];g++)if(strstr(lower,guards[g]))return 0;
 *cut=0;return 1;
}
#endif

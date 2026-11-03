/* LFM's original split regex, implemented as a Unicode scanner before BPE.
 * Properties are generated; no vocabulary substitution or embedding surgery. */
#include "lfm_unicode.inc"
static unsigned utf_cp(const char*p,int*n){
 unsigned char a=(unsigned char)*p;*n=1;if(a<128)return a;
 if((a&224)==192&&p[1]){*n=2;return ((a&31)<<6)|(p[1]&63);}
 if((a&240)==224&&p[1]&&p[2]){*n=3;return ((a&15)<<12)|((p[1]&63)<<6)|(p[2]&63);}
 if((a&248)==240&&p[1]&&p[2]&&p[3]){*n=4;return ((a&7)<<18)|((p[1]&63)<<12)|((p[2]&63)<<6)|(p[3]&63);}
 return 0xfffd;
}
static int in_ranges(unsigned cp,const unsigned r[][2],size_t n){size_t a=0,b=n;while(a<b){size_t m=(a+b)/2;if(cp<r[m][0])b=m;else if(cp>r[m][1])a=m+1;else return 1;}return 0;}
static int uletter(unsigned cp){return in_ranges(cp,unicode_L,sizeof(unicode_L)/sizeof(unicode_L[0]));}
static int unumber(unsigned cp){return in_ranges(cp,unicode_N,sizeof(unicode_N)/sizeof(unicode_N[0]));}
static int uspace(unsigned cp){return (cp>=9&&cp<=13)||cp==32||cp==133||cp==160||cp==5760||(cp>=8192&&cp<=8202)||cp==8232||cp==8233||cp==8239||cp==8287||cp==12288;}
static int added_at(const char*p,int*len){int id=-1;*len=0;for(size_t i=0;i<sizeof(added_tokens)/sizeof(added_tokens[0]);i++){int n=strlen(added_tokens[i].text);if(n>*len&&!strncmp(p,added_tokens[i].text,n)){id=added_tokens[i].id;*len=n;}}return id;}
static int lower_ascii(int c){return c>='A'&&c<='Z'?c+32:c;}
static size_t lfm_split(const char*p){
 int n;unsigned cp=utf_cp(p,&n);
 if(cp=='\''){
  const char*ends[]={"s","t","re","ve","m","ll","d"};
  for(int i=0;i<7;i++){size_t j=0;for(;ends[i][j]&&p[j+1]&&lower_ascii((unsigned char)p[j+1])==ends[i][j];j++);if(!ends[i][j])return j+1;}
 }
 const char*q=p;int w=n;
 if(!uletter(cp)&&!unumber(cp)&&cp!='\r'&&cp!='\n')q+=n;
 int qn;unsigned qc=*q?utf_cp(q,&qn):0;
 if(*q&&uletter(qc)){do{q+=qn;if(!*q)break;qc=utf_cp(q,&qn);}while(uletter(qc));return q-p;}
 if(unumber(cp)){q=p;int count=0;do{q+=n;count++;if(!*q||count==3)break;cp=utf_cp(q,&n);}while(unumber(cp));return q-p;}
 q=p;if(cp==' '&&p[1])q++;
 qc=*q?utf_cp(q,&qn):0;
 if(*q&&!uspace(qc)&&!uletter(qc)&&!unumber(qc)){
  do{q+=qn;if(!*q)break;qc=utf_cp(q,&qn);}while(!uspace(qc)&&!uletter(qc)&&!unumber(qc));
  while(*q=='\r'||*q=='\n')q++;return q-p;
 }
 q=p;const char*last_nl=NULL,*last_cp=p;
 while(*q){qc=utf_cp(q,&qn);if(!uspace(qc))break;last_cp=q;q+=qn;if(qc=='\r'||qc=='\n')last_nl=q;}
 if(last_nl)return last_nl-p;
 if(q>p){if(*q&&last_cp>p)return last_cp-p;return q-p;}
 return w;
}
void engine_encode(Tokenizer*t,const char*text,int*tokens,int*n_tokens){
 if(!s_lfm){engine_encode_raw(t,text,tokens,n_tokens);return;}
 *n_tokens=0;const char*p=text;
 while(*p){
  int len,id=added_at(p,&len);if(id>=0){tokens[(*n_tokens)++]=id;p+=len;continue;}
  size_t take=lfm_split(p);if(!take)take=1;
  /* Added tokens split regular runs even when embedded inside a regex chunk. */
  for(size_t i=1;i<take;i++){int sp_len;if(added_at(p+i,&sp_len)>=0){take=i;break;}}
  char*piece=malloc(take+1);if(!piece){*n_tokens=0;return;}memcpy(piece,p,take);piece[take]=0;
  int count=0;engine_encode_raw(t,piece,tokens+*n_tokens,&count);free(piece);*n_tokens+=count;p+=take;
 }
}

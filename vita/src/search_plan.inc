/* The same local LLM plans a search. No question-phrase classifier. */
static const char s_search_system[]="<|startoftext|><|im_start|>system\nWrite a short web search query for the user's message. Use only names and words from the message or conversation topic. Do not answer the question. Output only the query, or NONE if web information would not help.<|im_end|>\n";
static Session s_plan_session;
static int s_plan_ids[128],s_plan_n;
static void *s_plan_prefix,*s_chat_checkpoint;
static size_t s_plan_bytes,s_chat_checkpoint_capacity;
static char s_previous_message[257],s_previous_answer[513];
static void search_plan_reset(void){
 memset(&s_plan_session,0,sizeof(s_plan_session));memset(s_previous_message,0,sizeof(s_previous_message));memset(s_previous_answer,0,sizeof(s_previous_answer));
 if(s_chat_checkpoint)memset(s_chat_checkpoint,0,s_chat_checkpoint_capacity);
}
static void search_plan_remember(const char*message,const char*answer){
 snprintf(s_previous_message,sizeof(s_previous_message),"%s",message);s_previous_message[utf8_prefix(s_previous_message)]=0;
 snprintf(s_previous_answer,sizeof(s_previous_answer),"%s",answer);s_previous_answer[utf8_prefix(s_previous_answer)]=0;
}
static void search_plan_prefix_init(void){
 if(s_plan_prefix)return;
 int ids[1024],n=encode_bounded(s_search_system,ids);n-=n%4;
 if(n<1||n>128)return;
 s_plan_bytes=engine_prefix_bytes(&s_transformer,n);s_plan_prefix=malloc(s_plan_bytes);
 if(!s_plan_prefix)return;
 session_init(&s_plan_session,&s_transformer,&s_tokenizer);
 if(session_start(&s_plan_session,ids,n,1,1,0)||engine_prefix_save(&s_transformer,n,s_plan_prefix,s_plan_bytes)){
  free(s_plan_prefix);s_plan_prefix=0;
 }else{memcpy(s_plan_ids,ids,n*sizeof(int));s_plan_n=n;dbg_log("SEARCH_PLAN_PREFIX tokens=%d bytes=%u",n,(unsigned)s_plan_bytes);}
 if(s_prefix_snapshot){engine_prefix_restore(&s_transformer,s_prefix_n,s_prefix_snapshot,s_prefix_bytes);s_session.pos=s_prefix_n;memcpy(s_session.fed,s_prefix_ids,s_prefix_n*sizeof(int));s_session.has_pending=0;}
 else session_reset(&s_session);
}
static int plan_word_char(unsigned char c){return isalnum(c)||c>=128;}
static int plan_word_in(const char*context,const char*word,size_t length){
 for(const char*p=context;*p;){while(*p&&!plan_word_char((unsigned char)*p))p++;const char*start=p;while(*p&&plan_word_char((unsigned char)*p))p++;
  if((size_t)(p-start)==length&&!strncasecmp(start,word,length))return 1;
 }
 return 0;
}
static int plan_grounded(const char*plan,const char*context){
 int words=0;for(const char*p=plan;*p;){while(*p&&!plan_word_char((unsigned char)*p))p++;const char*start=p;while(*p&&plan_word_char((unsigned char)*p))p++;size_t n=p-start;
  if(n){if(!plan_word_in(context,start,n))return 0;words++;}
 }return words>0;
}
static int search_plan_query(const char*message,const char*topic,char*out,unsigned capacity){
 char question[1100],context[2200],body[3072];int ids[2048];
 snprintf(question,sizeof(question),"%s",message);escape_roles(question,sizeof(question));
 snprintf(context,sizeof(context),"%s\n%s\n%s\n%s",topic?topic:"",s_previous_message,s_previous_answer,message);
 snprintf(body,sizeof(body),"%s<|im_start|>user\nConversation topic: %s\nPrevious message: %s\nPrevious answer: %s\nMessage: %s<|im_end|>\n<|im_start|>assistant\n",s_search_system,topic?topic:"",s_previous_message,s_previous_answer,question);
 int n=encode_bounded(body,ids);if(n<1||n+24>=512)return -1;
 int oldpos=s_session.pos,saved=0;size_t bytes=oldpos>0?engine_prefix_bytes(&s_transformer,oldpos):0;
 if(bytes){if(bytes>s_chat_checkpoint_capacity){void*p=realloc(s_chat_checkpoint,bytes);if(p){s_chat_checkpoint=p;s_chat_checkpoint_capacity=bytes;}}
  if(s_chat_checkpoint&&s_chat_checkpoint_capacity>=bytes&&!engine_prefix_save(&s_transformer,oldpos,s_chat_checkpoint,bytes))saved=1;}
 session_init(&s_plan_session,&s_transformer,&s_tokenizer);s_plan_session.cancelled=cancelled;
 int reuse=0;if(s_plan_prefix&&n>s_plan_n&&!memcmp(ids,s_plan_ids,s_plan_n*sizeof(int))){
  engine_prefix_restore(&s_transformer,s_plan_n,s_plan_prefix,s_plan_bytes);memcpy(s_plan_session.fed,s_plan_ids,s_plan_n*sizeof(int));s_plan_session.pos=s_plan_n;reuse=1;}
 int rc=session_start(&s_plan_session,ids,n,24,24,reuse);
 if(!rc)while(!s_cancel&&s_plan_session.reason==END_NONE){if(session_step(&s_plan_session)<0)break;}
 if(saved)engine_prefix_restore(&s_transformer,oldpos,s_chat_checkpoint,bytes);
 else{s_session.pos=0;s_session.has_pending=0;}
 if(s_cancel)return -2;if(rc)return -1;
 char *p=s_plan_session.text;while(isspace((unsigned char)*p)||*p=='"'||*p=='\'')p++;
 char *end=strpbrk(p,"\r\n");if(end)*end=0;size_t length=strlen(p);
 while(length&&(isspace((unsigned char)p[length-1])||strchr("\"'.",p[length-1])))p[--length]=0;
 dbg_log("SEARCH_PLAN raw=%s restored_chat=%d old_tokens=%d",p,saved,oldpos);
 if(!strcasecmp(p,"NONE")){out[0]=0;return 0;}
 if(!plan_grounded(p,context))return -1;
 snprintf(out,capacity,"%s",p);out[utf8_prefix(out)]=0;return out[0]?1:-1;
}

#include <psp2/appmgr.h>
#include <string.h>
#include "app_paths.h"
int app_paths_init(void){char title[32]={0};return sceAppMgrAppParamGetString(0,SCE_APPMGR_APP_PARAM_TITLE_ID,title,sizeof(title))<0||strcmp(title,"VLMNET001")?-1:0;}
const char*app_data_dir(void){return "ux0:data/TyphoonOnline";}
const char*app_log_path(void){return "ux0:data/TyphoonOnline/chat.log";}
const char*app_test_path(void){return "ux0:data/TyphoonOnline/online-tests.txt";}
const char*app_test_done(void){return "ux0:data/TyphoonOnline/online-tests.done";}

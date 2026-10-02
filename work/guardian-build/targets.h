/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <stdio.h>
#ifndef DYNAMIC_TIDS_PATH
#define DYNAMIC_TIDS_PATH "/data/adb/.config/encore_pixel_cp41/dynamic-tids"
#endif
static int dynamic_tid(int tid){
 FILE *f=fopen(DYNAMIC_TIDS_PATH,"r");
 if(!f)return 0;
 int v,ok=0;
 while(fscanf(f,"%d",&v)==1)if(v==tid){ok=1;break;}
 fclose(f);
 return ok;
}
static int selected(const char*name,int tid,int pid){
 (void)name;(void)pid;
 return dynamic_tid(tid);
}
static int allowed_policy(unsigned policy){return policy==0||policy==3;}

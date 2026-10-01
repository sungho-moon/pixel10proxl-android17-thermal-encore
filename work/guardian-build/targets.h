/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <stdio.h>
static int dynamic_tid(int tid){
 FILE *f=fopen("/data/adb/.config/encore_pixel_cp41/dynamic-tids","r");
 if(!f)return 0; int v,ok=0; while(fscanf(f,"%d",&v)==1)if(v==tid){ok=1;break;} fclose(f); return ok;
}
static int numbered_render(const char *name){
 const char *prefix="RenderThread ";size_t n=strlen(prefix);
 if(strncmp(name,prefix,n)||!name[n])return 0;
 for(const char *p=name+n;*p;p++)if(*p<'0'||*p>'9')return 0;
 return 1;
}
static int selected(const char*name,int tid,int pid){
 return dynamic_tid(tid)||!strcmp(name,"RHIThread")||!strcmp(name,"RenderThread")||numbered_render(name)||!strncmp(name,"TaskGraphNP ",12)||
 (tid!=pid&&!strcmp(name,"MainThread-UE4"))||!strcmp(name,"GameThread")||
 !strcmp(name,"MainThread")||!strcmp(name,"UnityMain")||!strncmp(name,"UnityPreload",12)||
 !strncmp(name,"UnityMultiRende",15)||!strncmp(name,"UnityChoreograp",15)||
 !strcmp(name,"GfxDeviceWorker")||!strncmp(name,"UnityGfxDeviceW",15)||
 !strcmp(name,"Cocos2dxGLThread")||!strcmp(name,"GLThread")||!strcmp(name,"NativeThread")||
 !strcmp(name,"SDLThread")||!strncmp(name,"Worker Thread",13)||!strncmp(name,"CoreThread",10)||
 !strncmp(name,"JobThread",9)||!strncmp(name,"Job.Worker ",11)||!strncmp(name,"JobSystem ",10);
}
static int allowed_policy(unsigned policy){return policy==0||policy==3;}

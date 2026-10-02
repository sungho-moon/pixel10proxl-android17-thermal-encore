#include <assert.h>
#include <stdio.h>
#include "policy.h"
int main(void){
 int targets[]={30,60,90,120,144};
 for(unsigned j=0;j<sizeof(targets)/sizeof(targets[0]);j++){
  int t=targets[j];double b=1000.0/t;struct policy p={0};
  for(int i=0;i<20;i++)assert(decide(&p,t,t,b,b*1.03,3,390,t,1)==0);
  p=(struct policy){0};
  for(int i=0;i<4;i++)assert(decide(&p,30,t*.7,b/0.7,b*1.8,80,390,t,1)==0);
  assert(decide(&p,30,t*.7,b/0.7,b*1.8,80,390,t,1)==1);
  assert(decide(&p,30,t*.7,b/0.7,b*1.8,80,390,t,1)==2);
  for(int i=0;i<7;i++)assert(decide(&p,30,t*.7,b/0.7,b*1.8,80,390,t,1)==3);
  assert(decide(&p,30,t*.7,b/0.7,b*1.8,80,390,t,1)==1&&p.cooldown==8);
  assert(decide(&p,30,t*.7,b/0.7,b*1.8,80,390,t,1)==1&&p.cooldown==7);
  assert(decide(&p,30,t*.7,b/0.7,b*1.8,80,390,t,1)==2&&p.cooldown==0);
  p=(struct policy){.level=1,.cooldown=8};
  for(int i=0;i<8;i++)assert(decide(&p,30,t*.94,b/.94,b*1.1,10,390,t,1)==1);
  assert(p.base_n==4&&p.cooldown==0);
 }
 struct policy p={0};
 /* Stable 60 FPS is not proof of a cap when the target is 120. */
 for(int i=0;i<4;i++)assert(decide(&p,60,60,16.67,17,100,390,120,1)==0);
 assert(decide(&p,60,60,16.67,17,100,390,120,1)==1);
 assert(decide(&p,60,60,16.67,17,100,390,120,0)==0&&p.base_n==0);
 assert(decide(&p,60,60,16.67,17,100,480,120,1)==0);
 assert(decide(&p,60,60,16.67,17,100,-1,120,1)==0);
 assert(decide(&p,0,0,0,0,0,390,120,1)==0);
 p=(struct policy){0};
 for(int i=0;i<4;i++)assert(decide(&p,80,80,12.5,17,60,390,120,1)==0);
 for(int i=0;i<4;i++)assert(decide(&p,80,80,12.5,17,60,455,120,1)==1);
 puts("POLICY_TEST_PASS configured_targets smooth_backoff early_reentry temperature invalid_measurement");
 return 0;
}

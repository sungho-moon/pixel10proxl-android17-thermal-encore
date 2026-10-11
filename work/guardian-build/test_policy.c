#include <assert.h>
#include <stdio.h>
#include "policy.h"
int main(void){
 assert(inferred_target(NAN)==0&&inferred_target(INFINITY)==0);
 assert(inferred_target(-INFINITY)==0&&inferred_target(-1)==0);
 assert(inferred_target(23.99)==0&&inferred_target(24)==30);
 assert(inferred_target(41.99)==30&&inferred_target(42)==60);
 assert(inferred_target(60)==60&&inferred_target(74.99)==60);
 assert(inferred_target(75)==90&&inferred_target(90)==90);
 assert(inferred_target(104.99)==90&&inferred_target(105)==120);
 assert(inferred_target(120)==120);
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

/* SPDX-License-Identifier: GPL-3.0-or-later. Frame feedback with measured backoff. */
struct policy {int level,good,capped,cooldown,base_n,high_n;double base_fps,base_p95,high_fps,high_p95;};
static void reset_measure(struct policy*p){p->base_n=p->high_n=0;p->base_fps=p->base_p95=p->high_fps=p->high_p95=0;}
static int decide(struct policy*p,int samples,double fps,double avg,double p95,double late,int temp,int target,int valid){
 (void)avg;
 if(!valid||target<24||target>240||samples<20||temp<0||temp>=480){p->level=p->good=0;reset_measure(p);return 0;}
 int maximum=temp>=450?1:temp>=430?2:3;
 double budget=1000.0/target;
 if(p->cooldown>0){
  int severe=fps<target*.90&&p95>budget*1.35&&late>25;
  if(severe&&++p->capped>=2){p->cooldown=p->capped=0;reset_measure(p);p->level=maximum<2?maximum:2;return p->level;}
  if(!severe)p->capped=0;
  p->level=maximum<1?maximum:1;p->cooldown--;
  if(p->cooldown<4){p->base_fps+=fps;p->base_p95+=p95;p->base_n++;}
  return p->level;
 }
 if(p->base_n<4){p->level=0;p->base_n++;p->base_fps+=fps;p->base_p95+=p95;return 0;}
 if(fps<target*0.958333&&p95>budget*1.176&&late>15){if(p->level<maximum)p->level++;p->good=0;}
 else if(fps>=target*0.975&&p95<budget*1.2){if(++p->good>=5){if(p->level)p->level--;p->good=0;}}
 else p->good=0;
 if(p->level>maximum)p->level=maximum;
 if(p->level==maximum){
  p->high_n++;p->high_fps+=fps;p->high_p95+=p95;
  if(p->high_n>=8){
   double baseline_fps=p->base_fps/p->base_n,baseline_p95=p->base_p95/p->base_n;
   int benefit=p->high_fps/p->high_n>=baseline_fps*1.02||p->high_p95/p->high_n<=baseline_p95*0.95;
   if(!benefit){p->level=maximum<1?maximum:1;p->cooldown=8;p->capped=0;reset_measure(p);}
   else {p->high_n=0;p->high_fps=p->high_p95=0;}
  }
 }else {p->high_n=0;p->high_fps=p->high_p95=0;}
 return p->level;
}

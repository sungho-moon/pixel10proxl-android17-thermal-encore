/* SPDX-License-Identifier: GPL-3.0-or-later
 * Only per-thread uclamp.min through sched_setattr; no HAL/sysfs writes.
 * The guardian owns all changes. The separate sampler is read-only.
 */
#include "common.h"
#include "fork-watch.h"
#include "policy.h"
#include "targets.h"
#include <sys/stat.h>
#include <sys/file.h>
#define LIMIT 2048
struct record {int tid;uint64_t start;uint32_t original;int target;};
static struct record records[LIMIT];static int count;
static const uint32_t floors[]={0,257,385,513};
static char directory[768];static volatile sig_atomic_t alive=1;
static int session_fd=-1;
static int inferred_target(double fps) {
 static const int tiers[]={24,30,40,45,60,75,90,120,144,165,240};
 int best=0; double distance=1e9;
 if(fps<15)return 0;
 for(unsigned i=0;i<sizeof(tiers)/sizeof(tiers[0]);i++){double d=fps-tiers[i];if(d<0)d=-d;if(d<distance){distance=d;best=tiers[i];}}
 return best;
}
static int session_lock(void){
 char path[900];snprintf(path,sizeof(path),"%s/session.lock",directory);
 session_fd=open(path,O_CREAT|O_RDWR|O_CLOEXEC,0600);
 return session_fd<0||flock(session_fd,LOCK_EX|LOCK_NB)?-1:0;
}
static int boot_id(char*text,size_t n){FILE*f=fopen("/proc/sys/kernel/random/boot_id","r");if(!f)return -1;int ok=fgets(text,n,f)!=0;fclose(f);text[strcspn(text,"\n")]=0;return ok?0:-1;}
static void stop(int s){(void)s;alive=0;}
static int ours(uint32_t n){return n==257||n==385||n==513;}
static int save(void){
 char path[900],tmp[900];snprintf(path,sizeof(path),"%s/journal",directory);snprintf(tmp,sizeof(tmp),"%s/journal.new",directory);
 FILE*f=fopen(tmp,"w");if(!f)return -1;
 char boot[64];if(boot_id(boot,sizeof(boot))){fclose(f);return -1;}
 fprintf(f,"BOOT=%s\n",boot);
 for(int i=0;i<count;i++)fprintf(f,"%d %llu %u %d\n",records[i].tid,(unsigned long long)records[i].start,records[i].original,records[i].target);
 int bad=fflush(f)||fsync(fileno(f));if(fclose(f))bad=1;
 if(bad||rename(tmp,path))return -1;
 return 0;
}
static void load(void){
 char path[900];snprintf(path,sizeof(path),"%s/journal",directory);FILE*f=fopen(path,"r");if(!f)return;
 char header[80],boot[64],expected[80];
 if(boot_id(boot,sizeof(boot))||!fgets(header,sizeof(header),f)){fclose(f);return;}
 snprintf(expected,sizeof(expected),"BOOT=%s\n",boot);
 if(strcmp(header,expected)){fclose(f);puts("RECOVERY_SKIP different_boot_or_legacy_journal=1");return;}
 struct record r;unsigned long long start;
 while(count<LIMIT&&fscanf(f,"%d %llu %u %d",&r.tid,&start,&r.original,&r.target)==4){r.start=start;records[count++]=r;}
 fclose(f);
}
static int restore_one(struct record*r){
 if(birth(r->tid)!=r->start)return 0;
 struct attr a;if(get_attr(r->tid,&a))return 0;
 if(ours(a.min)){
  if(set_min(r->tid,r->original)){fprintf(stderr,"RESTORE_FAIL tid=%d %s\n",r->tid,strerror(errno));return -1;}
  if(get_attr(r->tid,&a)||a.min!=r->original)return -1;
  printf("RESTORED tid=%d min=%u\n",r->tid,a.min);
 }
 return 0;
}
static int restore_all(void){int error=0;for(int i=0;i<count;i++)if(restore_one(&records[i]))error=1;return error;}
static int find(int tid){for(int i=0;i<count;i++)if(records[i].tid==tid)return i;return -1;}
static int add(int tid,uint32_t original,int target){
 if(count==LIMIT)return -1;
 uint64_t start=birth(tid);if(!start)return 0;
 int old=find(tid);if(old>=0){if(records[old].start==start)return 0;records[old]=(struct record){tid,start,original,target};}
 else records[count++]=(struct record){tid,start,original,target};
 return save();
}
static int discover(int pid){
 char path[128];snprintf(path,sizeof(path),"/proc/%d/task",pid);DIR*d=opendir(path);if(!d)return -1;
 struct dirent*e;int added=0;
 while((e=readdir(d))){int tid=atoi(e->d_name);if(tid<=0)continue;
  int known=find(tid);
  if(known>=0&&records[known].start==birth(tid)){
   if(records[known].target==0&&dynamic_tid(tid)){records[known].target=1;added++;}
   continue;
  }
  snprintf(path,sizeof(path),"/proc/%d/comm",tid);FILE*f=fopen(path,"r");char name[64]={0};
  if(f){if(!fgets(name,sizeof(name),f))name[0]=0;fclose(f);}name[strcspn(name,"\n")]=0;
  if(!selected(name,tid,pid))continue;
  struct attr a;if(get_attr(tid,&a)||!allowed_policy(a.policy)||a.min!=0||a.max!=1024)continue;
  if(add(tid,a.min,1)){closedir(d);return -1;}added++;
  printf("TARGET tid=%d name=%s nice=%d min=%u max=%u flags=%llu\n",tid,name,a.nice,a.min,a.max,(unsigned long long)a.flags);
 }
 for(int i=0;i<count;i++){
  struct record*r=&records[i];
  if(r->target!=1||dynamic_tid(r->tid))continue;
  if(restore_one(r)){closedir(d);return -1;}
  r->target=0;
 }
 if(save()){closedir(d);return -1;}
 closedir(d);return added;
}
static int forks(struct watch*w){
 struct fork_event e;int result;
 while((result=next_fork(w,&e))>0){if(result==2)continue;
  int parent=find((uint32_t)e.parent);if(parent<0)continue;
  int child=(int)e.child;if(child<=1||child==getpid())continue;
  struct attr a;if(get_attr(child,&a)||!ours(a.min))continue;
  uint32_t original=records[parent].original;
  if(add(child,original,0))return -1;
  int index=find(child);if(index>=0&&restore_one(&records[index]))return -1;
  printf("INHERIT_RESTORED parent=%u child=%d\n",(uint32_t)e.parent,child);
 }
 return result<0?-1:0;
}
static int apply(int level){
 int active=0;
 for(int i=0;i<count;i++){
  struct record*r=&records[i];if(r->target!=1||birth(r->tid)!=r->start)continue;
  struct attr a;if(get_attr(r->tid,&a))continue;
  if(!allowed_policy(a.policy)||a.max!=1024||(!ours(a.min)&&a.min!=r->original)){
   /* Yield ownership if ADPF or another actor changed the request. */
   if(restore_one(r))return -1;
   r->target=2;printf("YIELD tid=%d external_attr_change=1\n",r->tid);continue;
  }
  uint32_t wanted=level?floors[level]:r->original;
  if(a.min!=wanted){
   uint32_t max=a.max,policy=a.policy;int32_t nice=a.nice;uint64_t flags=a.flags;
   if(set_min(r->tid,wanted)||get_attr(r->tid,&a)){perror("UCLAMP_APPLY");return -1;}
   if(a.min!=wanted||a.max!=max||a.policy!=policy||a.nice!=nice||a.flags!=flags){fprintf(stderr,"ATTR_INVARIANT_FAILED tid=%d\n",r->tid);return -1;}
  }
  active++;
 }
 printf("APPLIED level=%d uclamp_min=%u targets=%d\n",level,floors[level],active);return active;
}
static int enabled(const char*module){
 char path[900];const char*flags[]={"disable","remove","state/paused"};
 for(unsigned i=0;i<3;i++){snprintf(path,sizeof(path),"%s/%s",module,flags[i]);if(!access(path,F_OK))return 0;}
 return 1;
}
static int selftest(const char*binary){
 struct watch w;struct rlimit rl={RLIM_INFINITY,RLIM_INFINITY};setrlimit(RLIMIT_MEMLOCK,&rl);
 if(open_watch(&w)){perror("SELFTEST_FORK_WATCH");close_watch(&w);return 2;}
 int commands[2],results[2];if(pipe(commands)||pipe(results))return 3;
 pid_t target=fork();if(target<0)return 4;
 if(!target){close_watch(&w);close(commands[1]);close(results[0]);char cmd;
  if(read(commands[0],&cmd,1)!=1)_exit(5);
  pid_t child=fork();if(child<0)_exit(6);
  if(!child){close(commands[0]);close(results[1]);for(;;)pause();}
  if(write(results[1],&child,sizeof(child))!=(ssize_t)sizeof(child))_exit(7);
  for(;;)pause();
 }
 close(commands[0]);close(results[1]);
 struct attr before,after;int valid=get_attr(target,&before)==0&&!before.min&&before.max==1024;
 if(valid)valid=add(target,0,1)==0;
 if(valid)valid=set_min(target,257)==0;
 char cmd='F';if(write(commands[1],&cmd,1)!=1)valid=0;
 pid_t child=0;if(read(results[0],&child,sizeof(child))!=(ssize_t)sizeof(child))valid=0;
 uint64_t end=now_ns()+1000000000ull;
 while(now_ns()<end){if(forks(&w))valid=0;struct pollfd p={w.map,POLLIN,0};poll(&p,1,20);}
 if(child>0){valid=valid&&find(child)>=0&&get_attr(child,&after)==0&&after.min==0;kill(child,SIGKILL);}
 valid=restore_all()==0&&valid&&get_attr(target,&after)==0&&after.min==before.min&&after.max==before.max&&after.nice==before.nice&&after.policy==before.policy&&after.flags==before.flags;
 /* Prove that a separate process can recover a live target from the journal. */
 if(set_min(target,385))valid=0;
 pid_t recovery=fork();if(!recovery){close_watch(&w);execl(binary,binary,"--restore",directory,(char*)0);_exit(127);}
 int status=0;if(recovery<0||waitpid(recovery,&status,0)<0||!WIFEXITED(status)||WEXITSTATUS(status))valid=0;
 valid=valid&&get_attr(target,&after)==0&&after.min==before.min&&after.flags==before.flags;
 kill(target,SIGKILL);if(waitpid(target,0,0)<0)valid=0;close(commands[1]);close(results[0]);close_watch(&w);
 printf("SELFTEST=%s inheritance_and_restore=1 journal_recovery=1\n",valid?"PASS":"FAIL");return valid?0:9;
}
int main(int argc,char**argv){
 umask(0077);setvbuf(stdout,0,_IOLBF,0);signal(SIGINT,stop);signal(SIGTERM,stop);prctl(PR_SET_PDEATHSIG,SIGTERM);
 if(argc==3&&(!strcmp(argv[1],"--restore")||!strcmp(argv[1],"--selftest"))){
  if(strlen(argv[2])>=sizeof(directory))return 2;
  strcpy(directory,argv[2]);
  if(!strcmp(argv[1],"--selftest"))return selftest(argv[0]);
  if(session_lock()){puts("RECOVERY_DEFER active_guardian=1");return 75;}
  load();return restore_all();
 }
 if(argc!=6){fprintf(stderr,"Usage: guardian PID SAMPLER MODULE_DIR SECONDS TARGET_FPS\n");return 2;}
 int target=atoi(argv[5]); int automatic=target==0,candidate=0,windows=0;if(target!=0&&(target<24||target>240))return 2;
 int pid=atoi(argv[1]),seconds=atoi(argv[4]);if(pid<=1||seconds<1||seconds>300||strlen(argv[3])>700)return 2;
 snprintf(directory,sizeof(directory),"%s/state",argv[3]);
 if(session_lock()){puts("SESSION_ALREADY_ACTIVE");return 75;}
 load();if(restore_all())return 3;count=0;if(save())return 3;
 if(!enabled(argv[3])||!top_app(pid))return 0;
 struct rlimit rl={RLIM_INFINITY,RLIM_INFINITY};setrlimit(RLIMIT_MEMLOCK,&rl);
 struct watch w;if(open_watch(&w)){perror("FORK_WATCH");close_watch(&w);return 4;}
 if(discover(pid)<0){close_watch(&w);return 5;}
 int pipefd[2];if(pipe(pipefd)){close_watch(&w);return 6;}
 pid_t worker=fork();if(worker<0){close_watch(&w);return 6;}
 if(!worker){
  prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()==1)_exit(1);
  close_watch(&w);close(pipefd[0]);dup2(pipefd[1],1);close(pipefd[1]);
  execl(argv[2],argv[2],argv[1],"1220304",argv[4],argv[5],(char*)0);_exit(127);
 }
 close(pipefd[1]);fcntl(pipefd[0],F_SETFL,O_NONBLOCK);
 char path[900];snprintf(path,sizeof(path),"%s/guardian.pid",directory);FILE*f=fopen(path,"w");if(f){fprintf(f,"%d\n",getpid());fclose(f);}
 snprintf(path,sizeof(path),"%s/sampler.pid",directory);f=fopen(path,"w");if(f){fprintf(f,"%d\n",worker);fclose(f);}
 printf("GUARDIAN_READY pid=%d worker=%d targets=%d\n",pid,worker,count);
 struct policy policy={0};int error=0,eof=0;uint64_t start=now_ns(),last=start;
 char buffer[8192];size_t used=0;
 while(alive&&now_ns()-start<(uint64_t)(seconds+3)*1000000000ull){
  if(!enabled(argv[3])||!top_app(pid)){puts("STOP foreground_or_enabled=0");break;}
  if(forks(&w)){error=7;break;}
  if(now_ns()-last>2500000000ull){puts("WATCHDOG sampler_timeout=1");error=8;break;}
  struct pollfd p[2]={{pipefd[0],POLLIN,0},{w.map,POLLIN,0}};poll(p,2,100);
  ssize_t got=read(pipefd[0],buffer+used,sizeof(buffer)-used-1);
  if(got==0){puts("SAMPLER_EXIT");eof=1;break;}
  if(got<0){if(errno!=EAGAIN&&errno!=EINTR){error=9;break;}continue;}
  used+=(size_t)got;buffer[used]=0;char*begin=buffer,*end;
  while((end=strchr(begin,'\n'))){*end=0;
   if(!strncmp(begin,"FRAME_STATS",11)){
    int samples=0,valid=0;double fps=0,avg=0,p95=0,late=0;
    int fields=sscanf(begin,"FRAME_STATS samples=%d fps=%lf avg_ms=%lf p95_ms=%lf late_pct=%lf valid=%d",&samples,&fps,&avg,&p95,&late,&valid);
    if(fields!=6&&samples!=0){error=10;alive=0;break;}
    last=now_ns();int temp=number("/sys/class/power_supply/battery/temp");
    if(automatic&&valid) {
     int tier=inferred_target(fps);
     if(tier>target){if(tier==candidate)windows++;else{candidate=tier;windows=1;}
      if(windows>=2){target=tier;memset(&policy,0,sizeof(policy));printf("TARGET_AUTO target=%d\n",target);}
     }else{candidate=windows=0;}
    }
    /* late_pct came from an automatic sampler budget; always use our latched target. */
    late=p95>1100.0/(target?target:1)?100:0;
    int level=decide(&policy,samples,fps,avg,p95,late,temp,target,valid);
    printf("%s battery_c=%.1f cooldown=%d\n",begin,temp/10.0,policy.cooldown);
    if(discover(pid)<0||apply(level)<0){error=11;alive=0;break;}
   }else if(!strncmp(begin,"STREAM ",7)||!strncmp(begin,"COVERAGE ",9)||!strncmp(begin,"PROBE_",6))puts(begin);
   begin=end+1;
  }
  size_t left=used-(size_t)(begin-buffer);memmove(buffer,begin,left);used=left;
  if(used==sizeof(buffer)-1){error=12;break;}
 }
 kill(worker,SIGKILL);int worker_status=0;
 if(waitpid(worker,&worker_status,0)<0&&errno!=ECHILD)error=13;
 if(eof&&(!WIFEXITED(worker_status)||WEXITSTATUS(worker_status)))error=15;
 if(forks(&w)||restore_all())error=14;
 close(pipefd[0]);close_watch(&w);
 snprintf(path,sizeof(path),"%s/guardian.pid",directory);unlink(path);
 snprintf(path,sizeof(path),"%s/sampler.pid",directory);unlink(path);
 printf("GUARDIAN_END code=%d journal_records=%d\n",error,count);return error;
}

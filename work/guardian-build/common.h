/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Shared guardian helpers imported from the uclamp021 implementation in the
 * earlier pixel-scheduler work tree. Kept local so guardian builds are
 * reproducible without depending on files outside this repository.
 */
#define _GNU_SOURCE
#include <linux/bpf.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <poll.h>
#include <dirent.h>
#include <fcntl.h>
struct attr {uint32_t size,policy;uint64_t flags;int32_t nice;uint32_t priority;uint64_t runtime,deadline,period;uint32_t min,max;};
static int get_attr(int tid,struct attr*a){memset(a,0,sizeof(*a));a->size=sizeof(*a);return syscall(__NR_sched_getattr,tid,a,sizeof(*a),0);}
static int set_min(int tid,uint32_t min){struct attr old;if(get_attr(tid,&old))return -1;struct attr a={.size=sizeof(a),.flags=(old.flags&1)|8|16|32,.min=min};return syscall(__NR_sched_setattr,tid,&a,0);}
static uint64_t now_ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
static int number(const char*p){FILE*f=fopen(p,"r");int n=-1;if(f){if(fscanf(f,"%d",&n)!=1)n=-1;fclose(f);}return n;}
static uint64_t birth(int tid){
 char path[80],line[4096];snprintf(path,sizeof(path),"/proc/%d/stat",tid);FILE*f=fopen(path,"r");if(!f)return 0;
 if(!fgets(line,sizeof(line),f)){fclose(f);return 0;}fclose(f);
 char*s=strrchr(line,')');if(!s)return 0;s+=2;
 char*save=0;char*t=strtok_r(s," ",&save);
 for(int field=3;t&&field<22;field++)t=strtok_r(0," ",&save);
 return t?strtoull(t,0,10):0;
}
static int top_app(int pid){
 char path[96];snprintf(path,sizeof(path),"/proc/%d/task",pid);DIR*d=opendir(path);if(!d)return 0;
 FILE*f=fopen("/dev/cpuset/top-app/tasks","r");if(!f){closedir(d);return 0;}
 struct dirent*e;int n,yes=0;
 while((e=readdir(d))){int tid=atoi(e->d_name);if(tid<=0)continue;rewind(f);while(fscanf(f,"%d",&n)==1)if(n==tid){yes=1;break;}if(yes)break;}
 fclose(f);closedir(d);return yes;
}
#define REG(d,s) .dst_reg=(d),.src_reg=(s)
#define INS(c,d,s,o,i) ((struct bpf_insn){.code=(c),REG(d,s),.off=(o),.imm=(i)})
#define MOV(d,s) INS(BPF_ALU64|BPF_MOV|BPF_X,d,s,0,0)
#define IMM(d,i) INS(BPF_ALU64|BPF_MOV|BPF_K,d,0,0,i)
#define CALL(i) INS(BPF_JMP|BPF_CALL,0,0,0,i)
#define ST(d,s,o) INS(BPF_STX|BPF_MEM|BPF_DW,d,s,o,0)
#define LD(d,s,o) INS(BPF_LDX|BPF_MEM|BPF_DW,d,s,o,0)
static int bpf_call(int cmd,union bpf_attr*a){return syscall(__NR_bpf,cmd,a,sizeof(*a));}

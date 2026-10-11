/* SPDX-License-Identifier: GPL-3.0-or-later. PID offset is pinned to this kernel BTF. */
/*
 * Fork watcher imported from the uclamp021 implementation in the earlier
 * pixel-scheduler work tree; this local copy is the guardian build input.
 */
struct fork_event {uint64_t time,parent,child;};
struct watch {int map,prog,link;uint64_t*consumer;uint8_t*producer;size_t page;};
static void close_watch(struct watch*w){
 if(w->link>=0)close(w->link);
 if(w->prog>=0)close(w->prog);
 if(w->map>=0)close(w->map);
 if(w->consumer&&w->consumer!=MAP_FAILED)munmap(w->consumer,w->page);
 if(w->producer&&w->producer!=MAP_FAILED)munmap(w->producer,w->page+131072);
}
static int open_watch(struct watch*w){
 memset(w,0,sizeof(*w));w->map=w->prog=w->link=-1;w->page=sysconf(_SC_PAGESIZE);
 union bpf_attr a={.map_type=BPF_MAP_TYPE_RINGBUF,.max_entries=65536};w->map=bpf_call(BPF_MAP_CREATE,&a);if(w->map<0)return -1;
 struct bpf_insn p[64];int n=0;
 p[n++]=MOV(6,1);p[n++]=CALL(BPF_FUNC_ktime_get_ns);p[n++]=MOV(7,0);p[n++]=CALL(BPF_FUNC_get_current_pid_tgid);p[n++]=MOV(8,0);
 p[n++]=LD(3,6,8);p[n++]=INS(BPF_ALU64|BPF_ADD|BPF_K,3,0,0,1800);
 p[n++]=MOV(1,10);p[n++]=INS(BPF_ALU64|BPF_ADD|BPF_K,1,0,0,-8);p[n++]=IMM(2,4);p[n++]=CALL(BPF_FUNC_probe_read_kernel);
 int failed=n;p[n++]=INS(BPF_JMP|BPF_JNE|BPF_K,0,0,0,0);
 p[n++]=INS(BPF_LD|BPF_DW|BPF_IMM,1,BPF_PSEUDO_MAP_FD,0,w->map);p[n++]=INS(0,0,0,0,0);p[n++]=IMM(2,24);p[n++]=IMM(3,0);p[n++]=CALL(BPF_FUNC_ringbuf_reserve);
 int empty=n;p[n++]=INS(BPF_JMP|BPF_JEQ|BPF_K,0,0,0,0);
 p[n++]=MOV(6,0);p[n++]=ST(6,7,0);p[n++]=ST(6,8,8);p[n++]=INS(BPF_LDX|BPF_MEM|BPF_W,2,10,-8,0);p[n++]=ST(6,2,16);
 p[n++]=MOV(1,6);p[n++]=IMM(2,0);p[n++]=CALL(BPF_FUNC_ringbuf_submit);
 int end=n;p[n++]=IMM(0,0);p[n++]=INS(BPF_JMP|BPF_EXIT,0,0,0,0);p[failed].off=end-failed-1;p[empty].off=end-empty-1;
 static char log[65536];memset(&a,0,sizeof(a));a.prog_type=BPF_PROG_TYPE_RAW_TRACEPOINT;a.insn_cnt=n;a.insns=(uintptr_t)p;a.license=(uintptr_t)"GPL";a.log_buf=(uintptr_t)log;a.log_size=sizeof(log);a.log_level=1;
 w->prog=bpf_call(BPF_PROG_LOAD,&a);if(w->prog<0){fprintf(stderr,"FORK_BPF_LOAD %s\n%s\n",strerror(errno),log);return -1;}
 memset(&a,0,sizeof(a));a.raw_tracepoint.name=(uintptr_t)"sched_process_fork";a.raw_tracepoint.prog_fd=w->prog;w->link=bpf_call(BPF_RAW_TRACEPOINT_OPEN,&a);if(w->link<0)return -1;
 w->consumer=mmap(0,w->page,PROT_READ|PROT_WRITE,MAP_SHARED,w->map,0);
 w->producer=mmap(0,w->page+131072,PROT_READ,MAP_SHARED,w->map,w->page);
 return w->consumer==MAP_FAILED||w->producer==MAP_FAILED?-1:0;
}
static int next_fork(struct watch*w,struct fork_event*e){
 uint64_t c=__atomic_load_n(w->consumer,__ATOMIC_RELAXED),p=__atomic_load_n((uint64_t*)w->producer,__ATOMIC_ACQUIRE);
 if(c>=p)return 0;
 uint8_t*d=w->producer+w->page+(c&65535);uint32_t len=__atomic_load_n((uint32_t*)d,__ATOMIC_ACQUIRE);
 if(len&(1u<<31))return 0;
 uint32_t sz=len&((1u<<30)-1);if(sz>65528)return -1;
 int valid=!(len&(1u<<30))&&sz==sizeof(*e);if(valid)memcpy(e,d+8,sizeof(*e));
 __atomic_store_n(w->consumer,c+((sz+15)&~7ull),__ATOMIC_RELEASE);return valid?1:2;
}

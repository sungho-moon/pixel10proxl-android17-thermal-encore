// Host integration test: exec a fake helper; never touch Android or sysfs.
#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static std::string mod;
static std::string readstr(const std::string& path) { std::ifstream f(path); std::ostringstream s; s << f.rdbuf(); return s.str(); }
static int configured_target(const std::string&) { return 0; }
static void log(const std::string& s) { fprintf(stderr, "%s\n", s.c_str()); }
#include "fas-rs-observer.h"
int main(int argc, char** argv) {
    if (argc == 5) {
        timespec t{}; clock_gettime(CLOCK_MONOTONIC, &t);
        uint64_t stamp = static_cast<uint64_t>(t.tv_sec)*1000 + t.tv_nsec/1000000;
        const std::string mode = getenv("FASRS_FAKE_MODE");
        if (mode == "exit") return 17;
        if (mode == "stale") stamp -= 3000;
        printf("FASRS1 %s %s %llu 1 120 119000 1 2 0 1036000 2 1785000\n", argv[1], argv[2], static_cast<unsigned long long>(stamp));
        fflush(stdout);
        if (mode == "dies") { usleep(400000); return 18; }
        sleep(10); return 0;
    }
    char dir[] = "/tmp/pixel-fasrs-test.XXXXXX"; assert(mkdtemp(dir)); mod = dir;
    assert(mkdir((mod+"/bin").c_str(),0700)==0);
    std::string exe = readstr("/proc/self/exe");
    char real[4096]; ssize_t len=readlink("/proc/self/exe",real,sizeof(real)-1); assert(len>0); real[len]=0;
    const std::string helper=mod+"/bin/pixel-fas-rs-bridge"; assert(symlink(real,helper.c_str())==0);
    {
        setenv("FASRS_FAKE_MODE","dies",1); FrameObserver o("test");
        assert(o.sample(getpid(),true,"game.test")==0); usleep(150000);
        assert(o.sample(getpid(),true,"game.test")==1); assert(o.cpu_requests().at(2)==1785000);
        usleep(400000); assert(o.sample(getpid(),true,"game.test")==0); assert(o.cpu_requests().empty());
    }
    for (const char* mode : {"stale", "exit"}) {
        setenv("FASRS_FAKE_MODE",mode,1); FrameObserver o("test");
        o.sample(getpid(),true,"game.test"); usleep(150000);
        assert(o.sample(getpid(),true,"game.test")==0); assert(o.cpu_requests().empty());
        o.stop(); assert(waitpid(-1,nullptr,WNOHANG)==-1 && errno==ECHILD);
    }
    {
        setenv("FASRS_FAKE_MODE","healthy",1); FrameObserver o("test");
        o.sample(getpid(),true,"game.test"); usleep(150000); assert(o.sample(getpid(),true,"game.test")==1);
        assert(o.sample(getpid(),false,"game.test")==0); assert(o.cpu_requests().empty());
        assert(waitpid(-1,nullptr,WNOHANG)==-1 && errno==ECHILD);
    }
    unlink(helper.c_str()); rmdir((mod+"/bin").c_str()); rmdir(mod.c_str());
    puts("FASRS_OBSERVER PASS helper_exit stale_data foreground_exit child_reaping");
}

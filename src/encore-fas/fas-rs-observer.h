// Copyright 2026 Pixel adaptation contributors. Apache-2.0.
// Pipe-only adapter to the separate GPL fas-rs process. No upstream code is linked here.
#include "fas-rs-protocol.h"
class FrameObserver {
    int pipefd = -1, game_pid = 0, helper = -1, user_target = 0;
    uint64_t game_start = 0, sequence = 0, last_stamp = 0;
    bool blocked = false, verbose = false;
    std::string pending;
    std::map<int, long> requested;
    static uint64_t nowms() {
        timespec t{}; clock_gettime(CLOCK_MONOTONIC, &t);
        return static_cast<uint64_t>(t.tv_sec) * 1000 + t.tv_nsec / 1000000;
    }
    static uint64_t birth(int pid) {
        std::string text = readstr("/proc/" + std::to_string(pid) + "/stat");
        auto end = text.rfind(')'); if (end == std::string::npos) return 0;
        std::istringstream fields(text.substr(end + 1)); std::string value;
        for (int i = 0; i < 19; ++i) if (!(fields >> value)) return 0;
        uint64_t start = 0; fields >> start; return start;
    }
    void clean() {
        if (helper > 0) {
            kill(helper, SIGTERM);
            bool done = false;
            for (int i = 0; i < 30; ++i) {
                int status = 0;
                pid_t result = waitpid(helper, &status, WNOHANG);
                if (result == helper || (result < 0 && errno == ECHILD)) { done = true; break; }
                usleep(10000);
            }
            if (!done) { kill(helper, SIGKILL); waitpid(helper, nullptr, 0); }
        }
        helper = -1;
        if (pipefd >= 0) close(pipefd);
        pipefd = -1; game_pid = 0; game_start = sequence = last_stamp = 0;
        pending.clear(); requested.clear();
    }
    bool start(int pid, const std::string& package) {
        if (blocked) return false;
        const std::string binary = mod + "/bin/pixel-fas-rs-bridge";
        if (access(binary.c_str(), X_OK) != 0) { blocked = true; log("FASRS missing bridge; baseline only"); return false; }
        game_start = birth(pid); if (!game_start) return false;
        int fds[2]; if (pipe2(fds, O_CLOEXEC) != 0) return false;
        user_target = configured_target(package);
        const std::string pid_text = std::to_string(pid), start_text = std::to_string(game_start), target_text = std::to_string(user_target);
        const pid_t parent = getpid();
        helper = fork();
        if (helper == 0) {
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (getppid() != parent) _exit(127);
            close(fds[0]); dup2(fds[1], STDOUT_FILENO); close(fds[1]);
            execl(binary.c_str(), binary.c_str(), pid_text.c_str(), start_text.c_str(), package.c_str(), target_text.c_str(), nullptr);
            _exit(127);
        }
        close(fds[1]);
        if (helper < 0) { close(fds[0]); return false; }
        pipefd = fds[0]; fcntl(pipefd, F_SETFL, O_NONBLOCK);
        game_pid = pid;
        log("FASRS helper started pid=" + std::to_string(helper) + " game=" + package);
        return true;
    }
public:
    explicit FrameObserver(std::string, bool log_samples = false) : verbose(log_samples) {}
    ~FrameObserver() { clean(); }
    void stop() { clean(); blocked = false; }
    const std::map<int, long>& cpu_requests() const { return requested; }
    int sample(int pid, bool eligible, const std::string& package = "", long = 0) {
        if (!eligible || pid <= 0) { stop(); return 0; }
        if (game_pid && (game_pid != pid || birth(pid) != game_start || configured_target(package) != user_target)) stop();
        if (helper < 0 && !start(pid, package)) return 0;
        int status = 0;
        if (waitpid(helper, &status, WNOHANG) == helper) {
            helper = -1; clean(); blocked = true;
            log("FASRS helper exited; baseline only status=" + std::to_string(status)); return 0;
        }
        bool failed = false; size_t bytes = 0; char buffer[4096];
        for (;;) {
            ssize_t n = read(pipefd, buffer, sizeof(buffer));
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { failed = true; break; }
            bytes += n; if (bytes > 65536) { failed = true; break; }
            pending.append(buffer, n);
            size_t end;
            while ((end = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, end); pending.erase(0, end + 1);
                FasRsSnapshot snapshot;
                // Old queued records are expected on a two-second parent loop.
                // Identity/protocol failures cannot become frequency requests.
                if (!parse_fas_rs(line, pid, game_start, nowms(), sequence, snapshot)) continue;
                sequence = snapshot.sequence; last_stamp = snapshot.stamp;
                requested = snapshot.ready ? snapshot.cpu : std::map<int, long>{};
                if (verbose) fprintf(stderr, "FASRS_SAMPLE target=%d fps=%.3f ready=%d requests=%zu\n", snapshot.target, snapshot.milli_fps / 1000.0, snapshot.ready, requested.size());
            }
            if (pending.size() > 4096) { failed = true; break; }
        }
        if (failed) { clean(); blocked = true; log("FASRS pipe failed; baseline only"); return 0; }
        if (!last_stamp || nowms() < last_stamp || nowms() - last_stamp > 1500) requested.clear();
        return requested.empty() ? 0 : 1; // Activity indicator; upstream exports actual per-policy kHz.
    }
};

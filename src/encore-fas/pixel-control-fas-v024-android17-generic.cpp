// Copyright 2026 Pixel CP41 adaptation contributors. Apache-2.0.
// Owns only seven minimum-frequency requests. Journal precedes every write.
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <deque>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <regex>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <sys/wait.h>
#include <unistd.h>

static const std::string cfg = "/data/adb/.config/encore_pixel_cp41";
static const std::string mod = "/data/adb/modules/encore_pixel_cp41";
static volatile sig_atomic_t stopping = 0;
static void stop(int) { stopping = 1; }
static long monotime() { timespec t{}; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec; }
static std::string readstr(const std::string& p) {
    std::ifstream f(p); std::ostringstream s; s << f.rdbuf(); return s.str();
}
static int configured_target(const std::string& package) {
    const std::string games = readstr(cfg + "/gamelist.json");
    const std::regex entry("\\\"" + package + "\\\"\\s*:\\s*\\{[^}]*\\\"target_fps\\\"\\s*:\\s*(\\d+)");
    std::smatch match;
    if (!std::regex_search(games, match, entry) || match.size() < 2) return 0;
    const int rate = std::atoi(match[1].str().c_str());
    return rate >= 24 && rate <= 240 ? rate : 0;
}
static long number(const std::string& p) {
    std::istringstream s(readstr(p)); long n = -1; s >> n; return n;
}
static bool writeval(const std::string& p, long n) {
    int fd = open(p.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) return false;
    std::string v = std::to_string(n) + "\n";
    bool ok = write(fd, v.data(), v.size()) == static_cast<ssize_t>(v.size());
    int saved_errno = errno;
    close(fd);
    long after = number(p);
    if (!ok || after != n) fprintf(stderr, "writeval %s requested=%ld readback=%ld write_ok=%d errno=%d\n", p.c_str(), n, after, ok, saved_errno);
    return ok && after == n;
}
static bool atomicfile(const std::string& p, const std::string& s) {
    std::string tmp = p + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    bool ok = write(fd, s.data(), s.size()) == static_cast<ssize_t>(s.size()) && fsync(fd) == 0;
    close(fd);
    if (!ok || rename(tmp.c_str(), p.c_str()) != 0) return false;
    int dir = open(p.substr(0, p.rfind('/')).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0) return false;
    ok = fsync(dir) == 0; close(dir); return ok;
}
static void log(const std::string& s) {
    FILE* f = fopen((cfg + "/controller.log").c_str(), "a");
    if (f) { fprintf(f, "%ld %s\n", monotime(), s.c_str()); fclose(f); }
    fprintf(stderr, "%s\n", s.c_str());
}
static int smooth_thermal_cap(int current, long temp) {
    if (current < 0 || current > 3) current = 3;
    // Temperature is reported in deci-degrees Celsius. Escalation is
    // aggressive; recovery requires a 1 C margin to prevent oscillation.
    if (temp >= 430) return 0;
    if (temp >= 420) return std::min(current, 0);
    if (temp >= 410) return std::min(current, 1);
    if (temp >= 400) return std::min(current, 2);
    if (current == 0 && temp <= 410) return 1;
    if (current <= 1 && temp <= 400) return 2;
    if (current <= 2 && temp <= 390) return 3;
    return current;
}
struct Node {
    std::string path, table, maximum;
    long full, lite, boost = 0, original = -1, last = -1, prior = -1;
    bool active = false, blocked = false;
};
static std::vector<long> frequencies(const std::string& path) {
    std::vector<long> out; std::istringstream s(readstr(path)); long v;
    while (s >> v) if (v > 0) out.push_back(v);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
static long table_fraction(const std::string& path, double fraction, long fallback) {
    auto values = frequencies(path);
    if (values.empty()) return fallback;
    fraction = std::max(0.0, std::min(1.0, fraction));
    size_t index = static_cast<size_t>(std::llround(fraction * (values.size() - 1)));
    return values[index];
}
static std::vector<Node> make_nodes() {
    std::vector<Node> n;
    int ids[] = {0, 2, 5, 7};
    long full[] = {729000, 1401000, 1401000, 1305000};
    long lite[] = {533000, 1075000, 1075000, 1036000};
    long boost[] = {1036000, 1785000, 1785000, 1766000};
    for (int i = 0; i < 4; ++i) {
        std::string p = "/sys/devices/system/cpu/cpufreq/policy" + std::to_string(ids[i]) + "/";
        // Select from the live table. The fallback preserves the known-good
        // CP41 values when the node is temporarily unavailable during boot.
        n.push_back({p + "vote_manager/debug_min_freq", p + "scaling_available_frequencies", p + "scaling_max_freq",
            // Start games from a moderate floor; FAS raises this in stages.
            table_fraction(p + "scaling_available_frequencies", .34, full[i]),
            table_fraction(p + "scaling_available_frequencies", .32, lite[i]),
            table_fraction(p + "scaling_available_frequencies", .70, boost[i])});
    }
    const char* dev[] = {"34f00000.gpu0", "200c0780.dsufreq", "irm_gmc_freq"};
    // Raise only the userspace floor request. Thermal HAL remains authoritative
    // for the actual ceiling; this cannot bypass hardware thermal protection.
    long df[] = {691000000, 691000000, 844000000};
    long dl[] = {512000000, 537000000, 672000000};
    long db[] = {806000000, 806000000, 921000000};
    for (int i = 0; i < 3; ++i) {
        std::string p = "/sys/class/devfreq/" + std::string(dev[i]) + "/";
        // Match the aggressive thermal profile's 633 MHz GPU ceiling so the
        // controller does not release the request when Thermal HAL derates.
        // GPU starts near the 512 MHz OPP instead of jumping to 633 MHz.
        // Sustained frame deficit is handled by staged_floor()/FAS advice.
        double game_fraction = i == 0 ? .36 : .34;
        n.push_back({p + "vote_manager/debug_min_freq", p + "available_frequencies", p + "max_freq",
            table_fraction(p + "available_frequencies", game_fraction, df[i]),
            table_fraction(p + "available_frequencies", .32, dl[i]),
            table_fraction(p + "available_frequencies", .70, db[i])});
    }
    return n;
}
class Requests {
public:
    std::vector<Node> nodes;
    std::string journal, boot;
    Requests(std::vector<Node> n, std::string j, std::string b) : nodes(std::move(n)), journal(std::move(j)), boot(std::move(b)) {}
    long cpu_boost_span_khz() const {
        long span = 0;
        for (const auto& n : nodes) {
            if (n.path.find("/cpu/cpufreq/") != std::string::npos)
                span = std::max(span, n.boost - n.full);
        }
        return span;
    }
    bool save() {
        std::ostringstream s; s << boot << '\n';
        for (const auto& n : nodes) s << n.original << ' ' << n.last << ' ' << n.active << ' ' << n.blocked << ' ' << n.prior << '\n';
        return atomicfile(journal, s.str());
    }
    bool recover() {
        if (access(journal.c_str(), F_OK) != 0) return true;
        std::istringstream s(readstr(journal)); std::string id; s >> id;
        if (id != boot) return true; // sysfs requests reset on a different boot.
        auto backup = nodes;
        for (auto& n : nodes) {
            if (!(s >> n.original >> n.last >> n.active >> n.blocked >> n.prior) || (n.active && (n.original < 0 || n.last < 0 || n.prior < 0))) {
                nodes = backup; log("INVALID journal; refuse frequency control"); return false;
            }
        }
        bool ok = restore();
        if (ok) for (auto& n : nodes) n.blocked = false;
        return ok;
    }
    bool restore() {
        bool ok = true, changed = false;
        for (auto& n : nodes) {
            if (!n.active) continue;
            changed = true;
            long cur = number(n.path);
            if (cur == n.last || cur == n.prior) {
                if (!writeval(n.path, n.original)) { log("RESTORE FAILED " + n.path); ok = false; continue; }
                log("RESTORE " + n.path + "=" + std::to_string(n.original));
            } else {
                log("YIELD changed request " + n.path + "=" + std::to_string(cur));
                n.blocked = true;
            }
            n.active = false;
        }
        return (changed ? save() : true) && ok;
    }
    static long staged_floor(const Node& n, int level) {
        if (level <= 0 || n.boost <= n.full) return n.full;
        if (level >= 3) return n.boost;
        std::vector<long> choices;
        long value;
        std::istringstream table(readstr(n.table));
        while (table >> value) if (value >= n.full && value <= n.boost) choices.push_back(value);
        if (choices.empty()) return n.full;
        std::sort(choices.begin(), choices.end());
        choices.erase(std::unique(choices.begin(), choices.end()), choices.end());
        size_t index = ((choices.size() - 1) * static_cast<size_t>(level) + 2) / 3;
        return choices[std::min(index, choices.size() - 1)];
    }
    static long clamp_floor(const Node& n, long requested, long ceiling) {
        if (ceiling <= 0) return 0;
        std::vector<long> choices;
        long value;
        std::istringstream table(readstr(n.table));
        while (table >> value && value <= ceiling) choices.push_back(value);
        if (choices.empty()) return 0;
        return std::min(requested, choices.back());
    }
    bool apply(bool lite, int boost_level = 0) {
        for (auto& n : nodes) {
            if (n.blocked) continue;
            long cur = number(n.path), ceiling = number(n.maximum);
            if (cur < 0 || ceiling < 0) { restore(); return false; }
            if (n.active && cur != n.last) { n.active = false; n.blocked = true; save(); log("YIELD external writer " + n.path); continue; }
            if (!n.active) n.original = cur;
            long floor = lite ? n.lite : staged_floor(n, boost_level);
            long requested = std::max(n.original, floor);
            long want = clamp_floor(n, requested, ceiling);
            // Dynamic clamping: keep a valid floor at the thermal ceiling
            // instead of releasing to zero when Thermal HAL derates.
            if (want != requested) log("CLAMP " + n.path + " requested=" + std::to_string(requested) + " ceiling=" + std::to_string(ceiling) + " target=" + std::to_string(want));
            if (cur == want) continue;
            n.prior = cur;
            n.last = want; n.active = true;
            if (!save()) { restore(); return false; }
            if (!writeval(n.path, want)) { log("WRITE FAILED " + n.path); restore(); return false; }
            n.prior = want;
            if (!save()) { restore(); return false; }
            log("APPLY " + n.path + "=" + std::to_string(want));
        }
        return true;
    }
    bool preflight(bool same_value_write) {
        for (const auto& n : nodes) {
            std::istringstream s(readstr(n.table)); long v;
            bool full = false, lite = false, boost = n.boost == 0;
            while (s >> v) { full |= v == n.full; lite |= v == n.lite; boost |= v == n.boost; }
            long min = number(n.path), max = number(n.maximum);
            if (!full || !lite || !boost || min < 0 || max < min || access(n.path.c_str(), W_OK) != 0) {
                log("PREFLIGHT FAILED " + n.path); return false;
            }
            if (same_value_write && !writeval(n.path, min)) return false;
            printf("OK %s original=%ld full=%ld lite=%ld max=%ld\n", n.path.c_str(), min, n.full, n.lite, max);
        }
        return true;
    }
};
// Bounded native subprocess: never leave a hung dumpsys child or block restoration.
static std::string capture(const std::vector<std::string>& args, int seconds = 2, bool cleanup = false) {
    int p[2]; if (pipe2(p, O_CLOEXEC) != 0) return "";
    pid_t pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); return ""; }
    if (pid == 0) {
        setpgid(0, 0); dup2(p[1], STDOUT_FILENO);
        int nullfd = open("/dev/null", O_WRONLY); dup2(nullfd, STDERR_FILENO);
        close(p[0]); close(p[1]);
        std::vector<char*> av; for (const auto& a : args) av.push_back(const_cast<char*>(a.c_str())); av.push_back(nullptr);
        execv(av[0], av.data()); _exit(127);
    }
    close(p[1]); fcntl(p[0], F_SETFL, O_NONBLOCK);
    std::string result; long start = monotime(); bool eof = false;
    while (!eof && (!stopping || cleanup) && monotime() - start < seconds) {
        pollfd q{p[0], POLLIN | POLLHUP, 0}; poll(&q, 1, 100);
        char b[8192]; ssize_t r;
        while ((r = read(p[0], b, sizeof(b))) > 0) {
            result.append(b, static_cast<size_t>(r));
            if (result.size() > 4 * 1024 * 1024) { eof = true; result.clear(); break; }
        }
        if (r == 0) eof = true;
    }
    close(p[0]);
    int status = 0;
    bool reaped = false;
    if (eof) for (int i = 0; i < 20; ++i) {
        if (waitpid(pid, &status, WNOHANG) == pid) { reaped = true; break; }
        usleep(10000);
    }
    if (!reaped) { kill(-pid, SIGKILL); kill(pid, SIGKILL); waitpid(pid, &status, 0); }
    if (!eof || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return "";
    return result;
}
// Android's default game frame-rate policy is global. Own it only while an
// eligible foreground game is active, recording the previous value first.
struct FramePolicyIO {
    virtual ~FramePolicyIO() = default;
    virtual std::string get() = 0;
    virtual int resolve() = 0;
    virtual bool toggle(int tx, bool enabled) = 0;
    virtual bool text(const std::string& value) = 0;
};
struct DeviceFramePolicyIO : FramePolicyIO {
    static constexpr const char* property = "debug.graphics.game_default_frame_rate.disabled";
    std::string get() override {
        char value[PROP_VALUE_MAX]{}; __system_property_get(property, value); return value;
    }
    int resolve() override {
        std::string query = "android.app.IGameManagerService.Stub::TRANSACTION_toggleGameDefaultFrameRate";
        auto result = capture({"/system/bin/sh", "-c", "printf '%s\\n' '" + query + "' | /system/bin/app_process -Djava.class.path=" + mod + "/binder_resolver.apk / com.rem01gaming.binderresolver.MainKt"}, 5, true);
        std::istringstream s(result); std::string name, extra; int tx = 0;
        if (!(s >> name >> tx) || name != query || (s >> extra) || tx < 1 || tx > 256) return 0;
        return tx;
    }
    bool toggle(int tx, bool enabled) override {
        auto reply = capture({"/system/bin/service", "call", "game", std::to_string(tx), "i32", enabled ? "1" : "0"}, 3, true);
        return reply.size() < 128 && reply.find("Result: Parcel(") != std::string::npos
            && reply.find("00000000") != std::string::npos && reply.find("Exception") == std::string::npos
            && get() == (enabled ? "false" : "true");
    }
    bool text(const std::string& value) override {
        capture({"/system/bin/setprop", property, value}, 2, true);
        return get() == value;
    }
};
class FramePolicy {
    FramePolicyIO& io;
    std::string journal, boot, original;
    int tx = 0;
    bool active = false, blocked = false;
    static bool enabled(const std::string& v) { return v.empty() || v == "false" || v == "0"; }
    bool save() {
        return atomicfile(journal, boot + "\n" + (original.empty() ? "-" : original) + " "
            + std::to_string(tx) + " " + std::to_string(active) + " " + std::to_string(blocked) + "\n");
    }
public:
    FramePolicy(FramePolicyIO& backend, std::string path, std::string id)
        : io(backend), journal(std::move(path)), boot(std::move(id)) {}
    bool restore() {
        if (!active) return true;
        auto current = io.get();
        if (current == "true") {
            if (!io.toggle(tx, true) || !io.text(original)) { log("FRAME_POLICY RESTORE FAILED"); return false; }
            log("FRAME_POLICY RESTORE original=" + (original.empty() ? std::string("<empty>") : original));
        } else if (current != original) {
            blocked = true; log("FRAME_POLICY YIELD external change=" + current);
        }
        active = false; return save();
    }
    bool recover() {
        if (access(journal.c_str(), F_OK) != 0) return true;
        std::istringstream s(readstr(journal)); std::string id, value, extra; int a = -1, b = -1;
        if (!(s >> id)) { log("FRAME_POLICY INVALID journal"); return false; }
        if (id != boot) return true; // Nonpersistent policy resets across boots.
        if (!(s >> value >> tx >> a >> b) || (s >> extra) || (a != 0 && a != 1) || (b != 0 && b != 1)) return false;
        original = value == "-" ? "" : value;
        if (!enabled(original) || tx < 0 || tx > 256 || (a && tx == 0)) return false;
        if (a && io.resolve() != tx) { log("FRAME_POLICY journal transaction mismatch"); return false; }
        active = a; blocked = b;
        bool ok = restore(); if (ok) blocked = false; return ok;
    }
    bool apply() {
        if (blocked || stopping) return true;
        auto current = io.get();
        if (active) {
            if (current == "true") return true;
            active = false; blocked = true; log("FRAME_POLICY YIELD external writer"); return save();
        }
        if (current == "true" || current == "1") return true; // Already disabled by its owner.
        if (!enabled(current)) { blocked = true; log("FRAME_POLICY unknown original; skip"); return true; }
        if (!tx) tx = io.resolve();
        if (!tx) { log("FRAME_POLICY transaction unavailable"); return false; }
        original = current; active = true;
        if (!save()) { active = false; return false; }
        if (!io.toggle(tx, false)) { log("FRAME_POLICY APPLY FAILED"); return false; }
        log("FRAME_POLICY APPLY default disabled transaction=" + std::to_string(tx)); return true;
    }
};
struct Scene { std::string package = "NULL"; int pid = 0, uid = 0; bool awake = false, saver = true; };
static Scene scene() {
    Scene out;
    auto activity = capture({"/system/bin/dumpsys", "activity", "activities"});
    std::istringstream lines(activity); std::string line, fallback;
    while (std::getline(lines, line)) {
        if (line.find("mResumedActivity:") != std::string::npos) fallback = line;
        if (line.find("topResumedActivity=") != std::string::npos && line.find("ActivityRecord{") != std::string::npos) { fallback = line; break; }
    }
    auto slash = fallback.find('/');
    if (slash != std::string::npos) {
        auto space = fallback.rfind(' ', slash);
        if (space != std::string::npos) out.package = fallback.substr(space + 1, slash - space - 1);
    }
    if (out.package != "NULL") {
        std::istringstream pids(capture({"/system/bin/pidof", out.package})); pids >> out.pid;
        std::istringstream status(readstr("/proc/" + std::to_string(out.pid) + "/status"));
        while (std::getline(status, line)) if (line.rfind("Uid:", 0) == 0) { std::istringstream u(line.substr(4)); u >> out.uid; }
    }
    auto power = capture({"/system/bin/dumpsys", "power"});
    out.awake = power.find("mWakefulness=Awake") != std::string::npos;
    // Fail closed if the saver state cannot be read.
    auto saver = capture({"/system/bin/settings", "get", "global", "low_power"});
    out.saver = saver != "0\n";
    return out;
}
static bool compatible() {
    auto product = capture({"/system/bin/getprop", "ro.product.device"});
    auto sdk = capture({"/system/bin/getprop", "ro.build.version.sdk"});
    // Android 17 Pixel 10 Pro XL family gate. Frequency tables and the
    // libgui uprobe are checked separately; unknown libgui builds keep the
    // baseline Encore controller but do not guess a trace offset.
    return product == "mustang\n" && sdk == "37\n";
}
static bool conflict() {
    for (const auto& id : {"pixel_pubg_uclamp_cp41", "encore", "fas_rs", "fas_rs_pixel_cp41"}) {
        std::string p = "/data/adb/modules/" + std::string(id);
        if (access((p + "/module.prop").c_str(), F_OK) == 0 && access((p + "/disable").c_str(), F_OK) != 0 && access((p + "/remove").c_str(), F_OK) != 0) return true;
    }
    return false;
}
// Reimplements fas-rs 4.9.1's proportional frame-error control in Encore's
// single-writer path. Upstream uses kp=0.0003 on normalized nanosecond error;
// its direct cpufreq writer is deliberately not used here.
static int fas_rs_advice_level(double frame_seconds, int target_fps, long span_khz) {
    if (!std::isfinite(frame_seconds) || frame_seconds <= 0 || target_fps <= 0 || span_khz <= 0) return 0;
    double error_khz = (frame_seconds * static_cast<double>(target_fps) - 1.0) * 300000.0;
    if (error_khz <= 0) return 0;
    return static_cast<int>(std::clamp(std::ceil(error_khz * 3.0 / static_cast<double>(span_khz)), 1.0, 3.0));
}
static int combine_fas_rs_advice(int base_level, int advice_level) {
    // Aggressive profile: react to the first proportional deficit and allow
    // a severe deficit to add two OPP steps. Thermal caps still apply later.
    int extra = advice_level >= 3 ? 2 : advice_level >= 1 ? 1 : 0;
    return std::clamp(base_level + extra, 0, 3);
}
// Experimental userspace adaptation of encore_fas' frame-signal concept.
// tracefs observes libgui calls; this controller remains the only vote writer.
class FrameObserver {
    static constexpr const char* trace = "/sys/kernel/tracing";
    static constexpr const char* group = "encore_pixel_fas";
    static constexpr const char* instance = "/sys/kernel/tracing/instances/encore_pixel_fas";
    static constexpr const char* event = "/sys/kernel/tracing/instances/encore_pixel_fas/events/encore_pixel_fas/qb_hook/enable";
    static constexpr const char* marker = "/data/adb/.config/encore_pixel_cp41/fas-probe-owner";
    static constexpr const char* libhash = "20163138d23043abf3d755602483b212b6f9ff321e9810814b213e8655b5057a";
    std::string boot, pending;
    std::deque<double> frame_intervals;
    int pipefd = -1, game_pid = 0, render_tid = 0, warm = 0, good = 0, quiet = 0;
    int target_fps = 0, candidate_tier = 0, candidate_windows = 0, last_advice = 0;
    bool fixed_target = false;
    int user_target = 0;
    bool owned = false, blocked = false, armed = false, boosted = false, degraded = false, verbose = false;
    int boost_level = 0;
    double last = 0, last_frame = 0, pressure = 0, filtered_fps = 0;
    static double nowsec() {
        timespec t{}; clock_gettime(CLOCK_MONOTONIC, &t);
        return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) / 1e9;
    }
    static int nearest_tier(double fps) {
        constexpr int rates[] = {24, 30, 40, 45, 60, 75, 90, 120, 144};
        if (fps < 15.0) return 0;
        int best = rates[0];
        double distance = std::abs(std::log(fps / best));
        for (int rate : rates) {
            double d = std::abs(std::log(fps / rate));
            if (d < distance) { best = rate; distance = d; }
        }
        return best;
    }
    static bool emit(const std::string& path, const std::string& value, bool append = false) {
        int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC | (append ? O_APPEND : 0));
        if (fd < 0) return false;
        bool ok = write(fd, value.data(), value.size()) == static_cast<ssize_t>(value.size());
        close(fd); return ok;
    }
    static std::string path(const char* suffix) { return std::string(instance) + suffix; }
    static std::unordered_set<int> threads(int pid) {
        std::unordered_set<int> out;
        DIR* d = opendir(("/proc/" + std::to_string(pid) + "/task").c_str());
        if (!d) return out;
        while (auto* e = readdir(d)) {
            char* end = nullptr; long n = strtol(e->d_name, &end, 10);
            if (*e->d_name && *end == '\0' && n > 0 && n <= INT32_MAX) out.insert(static_cast<int>(n));
        }
        closedir(d); return out;
    }
    void clean() {
        if (!owned) return;
        emit(path("/tracing_on"), "0\n");
        emit(event, "0\n");
        if (pipefd >= 0) { close(pipefd); pipefd = -1; }
        emit(std::string(trace) + "/uprobe_events", std::string("-:") + group + "/qb_hook\n", true);
        rmdir(instance);
        if (access(instance, F_OK) != 0 && access((std::string(trace) + "/events/" + group).c_str(), F_OK) != 0) unlink(marker);
        owned = false; pending.clear(); frame_intervals.clear(); game_pid = render_tid = 0; warm = good = quiet = 0;
        armed = boosted = degraded = false; boost_level = 0;
        last = last_frame = pressure = filtered_fps = 0;
        target_fps = candidate_tier = candidate_windows = last_advice = 0; fixed_target = false;
    }
    bool start(int pid, const std::string& package) {
        if (blocked || access(instance, F_OK) == 0 || access((std::string(trace) + "/events/" + group).c_str(), F_OK) == 0) return false;
        auto hash = capture({"/system/bin/sha256sum", "/system/lib64/libgui.so"}, 5);
        if (hash.rfind(libhash, 0) != 0) { log("FAS observer libgui hash mismatch; disabled"); blocked = true; return false; }
        if (!atomicfile(marker, boot + "\n")) return false;
        owned = true;
        if (mkdir(instance, 0700) != 0 || !emit(path("/tracing_on"), "0\n")
            || !emit(path("/buffer_size_kb"), "256\n")
            || !emit(std::string(trace) + "/uprobe_events", std::string("p:") + group + "/qb_hook /system/lib64/libgui.so:0x129ed0\n", true)
            || !emit(event, "1\n") || !emit(path("/trace"), "\n")
            || (pipefd = open(path("/trace_pipe").c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0
            || !emit(path("/tracing_on"), "1\n")) {
            log("FAS observer attach failed; disabled for this game session"); clean(); blocked = true; return false;
        }
        game_pid = pid; last = nowsec();
        // The WebUI stores the per-game target in gamelist.json. Missing or
        // zero means automatic inference; there is no module-wide fallback.
        user_target = target_fps = configured_target(package); fixed_target = target_fps > 0;
        log("FAS observer attached pid=" + std::to_string(pid)); return true;
    }
    int drain(const std::unordered_set<int>& tids, std::unordered_map<int, std::vector<double>>& streams) {
        int count = 0; size_t bytes = 0; char buf[8192];
        while (true) {
            ssize_t n = read(pipefd, buf, sizeof(buf));
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (n <= 0) return -1;
            bytes += static_cast<size_t>(n);
            if (bytes > 1024 * 1024) return -1;
            pending.append(buf, static_cast<size_t>(n));
            if (pending.size() > 1024 * 1024) return -1;
            size_t pos;
            while ((pos = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, pos); pending.erase(0, pos + 1);
                if (line.find(": qb_hook:") == std::string::npos) continue;
                size_t bracket = line.find('[');
                if (bracket == std::string::npos) continue;
                size_t dash = line.rfind('-', bracket);
                if (dash == std::string::npos) continue;
                char* end = nullptr; long tid = strtol(line.c_str() + dash + 1, &end, 10);
                if (end == line.c_str() + dash + 1 || tid <= 0 || tid > INT32_MAX) continue;
                if (!tids.count(static_cast<int>(tid))) continue;
                size_t hook = line.find(": qb_hook:");
                size_t space = line.rfind(' ', hook);
                if (space == std::string::npos) continue;
                char* stamp_end = nullptr;
                double stamp = strtod(line.c_str() + space + 1, &stamp_end);
                if (stamp_end != line.c_str() + hook || !std::isfinite(stamp)) continue;
                streams[static_cast<int>(tid)].push_back(stamp);
                ++count;
            }
        }
        return count;
    }
public:
    explicit FrameObserver(std::string id, bool log_samples = false) : boot(std::move(id)), verbose(log_samples) {
        // A controller crash can leave a tracefs event. Only remove our own
        // same-boot event when the durable ownership marker matches.
        if (readstr(marker) == boot + "\n") { owned = true; clean(); }
    }
    ~FrameObserver() { clean(); }
    void stop() { clean(); blocked = false; }
    int sample(int pid, bool eligible, const std::string& package = "", long cpu_span_khz = 0) {
        if (!eligible || pid <= 0) { stop(); return 0; }
        if (game_pid != 0 && (game_pid != pid || user_target != configured_target(package))) stop();
        if (!owned && !start(pid, package)) return 0;
        auto tids = threads(pid);
        if (tids.empty()) { log("FAS observer game threads missing"); stop(); return 0; }
        std::unordered_map<int, std::vector<double>> streams;
        int count = drain(tids, streams);
        if (count < 0) { log("FAS observer trace overflow/read failure"); clean(); blocked = true; return 0; }
        double now = nowsec(), span = now - last; last = now;
        if (span > 8.0) { boosted = degraded = false; boost_level = 0; pressure = filtered_fps = 0; return 0; }
        if (span < 1.0) return boost_level;
        if (render_tid == 0 || !streams.count(render_tid)) {
            int selected = 0; size_t most = 0;
            for (const auto& [tid, stamps] : streams) if (stamps.size() > most) { selected = tid; most = stamps.size(); }
            if (selected != 0 && selected != render_tid) {
                render_tid = selected; last_frame = pressure = filtered_fps = 0;
                frame_intervals.clear();
                degraded = boosted = armed = false; boost_level = 0;
                warm = good = quiet = last_advice = 0;
                candidate_tier = candidate_windows = 0;
                // A render-thread handoff does not prove the user changed the
                // in-game FPS setting. Keep the session target latched.
                log("FAS render thread=" + std::to_string(render_tid));
            }
        }
        const auto stream = streams.find(render_tid);
        size_t frames = stream == streams.end() ? 0 : stream->second.size();
        if (stream != streams.end()) for (double stamp : stream->second) {
            if (last_frame > 0 && stamp > last_frame) {
                double interval = stamp - last_frame;
                if (interval >= 1.0) { pressure = 0; degraded = false; }
                else if (!degraded && target_fps > 0) {
                    // Upstream FAS' clipped, one-sided frame-deficit CUSUM.
                    pressure = std::max(0.0, pressure + std::min(interval * target_fps, 1.5) - 1.05);
                    if (pressure >= 1.0) { degraded = true; pressure = 0; }
                }
                if (interval > 0 && interval < 1.0) {
                    frame_intervals.push_back(interval);
                    size_t limit = static_cast<size_t>(std::max(target_fps, 24) * 5);
                    while (frame_intervals.size() > limit) frame_intervals.pop_front();
                }
            }
            last_frame = stamp;
        }
        double fps = static_cast<double>(frames) / span;
        filtered_fps = filtered_fps > 0 ? 0.3 * filtered_fps + 0.7 * fps : fps;
        if (verbose) printf("FAS_SAMPLE pid=%d tid=%d frames=%zu seconds=%.3f fps=%.2f target=%d deficit=%d\n", pid, render_tid, frames, span, fps, target_fps, degraded);
        if (target_fps == 0) {
            int tier = nearest_tier(fps);
            if (tier != 0) {
                if (candidate_tier == tier) ++candidate_windows;
                else { candidate_tier = tier; candidate_windows = 1; }
            } else candidate_tier = candidate_windows = 0;
            if (candidate_windows >= 2) {
                target_fps = tier; armed = true; degraded = false; pressure = 0;
                candidate_tier = candidate_windows = 0;
                log("FAS auto target=" + std::to_string(target_fps));
            }
            return 0;
        }
        double minimum_active = std::max(12.0, target_fps * 0.55);
        if (!armed) {
            if (fps >= minimum_active && fps <= target_fps * 1.45) ++warm;
            else warm = 0;
            if (warm >= 2) {
                armed = true; degraded = false; pressure = 0;
                log("FAS observer armed target=" + std::to_string(target_fps));
            }
            return 0;
        }
        if (fps < std::max(8.0, target_fps * 0.35)) ++quiet; else quiet = 0;
        if (quiet >= 2) {
            armed = boosted = degraded = false; boost_level = last_advice = 0;
            pressure = filtered_fps = 0; good = quiet = warm = 0;
            // A quiet menu is not evidence of a lower game FPS setting.
            return 0;
        }
        if (!fixed_target && !boosted) {
            int tier = nearest_tier(fps);
            bool upward = tier > target_fps && fps > target_fps * 1.12;
            if (upward) {
                if (candidate_tier == tier) ++candidate_windows;
                else { candidate_tier = tier; candidate_windows = 1; }
                if (candidate_windows >= 2) {
                    target_fps = tier; degraded = false; pressure = 0; good = 0;
                    candidate_tier = candidate_windows = 0;
                    log("FAS auto target switch=" + std::to_string(target_fps));
                }
            } else candidate_tier = candidate_windows = 0;
        }
        if (fps >= target_fps * 0.98) ++good; else good = 0;
        if (degraded && good >= 1) { degraded = false; pressure = 0; }
        int next_level = 0;
        double boost_floor = std::max(12.0, target_fps * (fixed_target ? 0.40 : 0.55));
        if (degraded && fps >= boost_floor && fps < target_fps * 0.95) {
            double error = 1.0 - filtered_fps / target_fps;
            next_level = error >= 0.28 ? 3 : error >= 0.14 ? 2 : 1;
        }
        int base_level = next_level;
        int advice = 0;
        if (access((cfg + "/disable-fas-rs-advisor").c_str(), F_OK) != 0
            && degraded && fps >= boost_floor && fps < target_fps * 0.98
            && frame_intervals.size() >= 30) {
            // A shorter history cuts initial response latency roughly in half
            // at 60 FPS while retaining a tail-percentile noise filter.
            size_t count = std::min(frame_intervals.size(), static_cast<size_t>(60));
            std::vector<double> recent(frame_intervals.end() - count, frame_intervals.end());
            std::sort(recent.begin(), recent.end());
            double p90 = recent[(count - 1) * 9 / 10];
            advice = fas_rs_advice_level(p90, target_fps, cpu_span_khz);
            // Add one step for any deficit and two for a severe one. Encore
            // still owns every vote and enforces its level-3 ceiling.
            next_level = combine_fas_rs_advice(base_level, advice);
        }
        if (advice != last_advice) {
            log("FAS_RS_ADVICE level=" + std::to_string(advice) + " target=" + std::to_string(target_fps)
                + " fps=" + std::to_string(static_cast<int>(std::lround(fps)))
                + " base=" + std::to_string(base_level) + " final=" + std::to_string(next_level));
            last_advice = advice;
        }
        if (next_level != boost_level) {
            log("FAS adaptive level=" + std::to_string(next_level) + " target="
                + std::to_string(target_fps) + " fps=" + std::to_string(static_cast<int>(std::lround(fps))));
        }
        boost_level = next_level; boosted = next_level > 0;
        return boost_level;
    }
};
static int selftest() {
    std::string d = "/data/local/tmp/encore-pixel-selftest." + std::to_string(getpid());
    mkdir(d.c_str(), 0700);
    std::string min = d + "/min", max = d + "/max", table = d + "/table";
    atomicfile(min, "100\n"); atomicfile(max, "1000\n"); atomicfile(table, "100 200 400 500 600\n");
    std::vector<Node> nodes{{min, table, max, 400, 200, 600}};
    Requests a(nodes, d + "/journal", "test-boot");
    bool ok = a.preflight(false) && a.apply(false) && number(min) == 400;
    Requests b(nodes, d + "/journal", "test-boot");
    ok &= b.recover() && number(min) == 100; // crash/restart journal recovery.
    ok &= b.apply(true) && number(min) == 200;
    ok &= b.apply(false, 1) && number(min) == 500;
    ok &= b.apply(false, 3) && number(min) == 600;
    ok &= b.apply(false) && number(min) == 400;
    ok &= fas_rs_advice_level(1.0 / 120.0, 120, 461000) == 0;
    ok &= fas_rs_advice_level(0.012, 120, 461000) == 1;
    ok &= fas_rs_advice_level(1.0 / 60.0, 120, 461000) == 2;
    ok &= fas_rs_advice_level(0.020, 120, 461000) == 3;
    ok &= fas_rs_advice_level(0.020, 0, 461000) == 0;
    ok &= combine_fas_rs_advice(2, 2) == 3;
    ok &= combine_fas_rs_advice(2, 1) == 3;
    ok &= combine_fas_rs_advice(1, 3) == 3;
    ok &= combine_fas_rs_advice(3, 2) == 3;
    writeval(min, 300); ok &= b.restore() && number(min) == 300; // external owner preserved.
    atomicfile(d + "/journal", "test-boot\nbroken\n");
    Requests c(nodes, d + "/journal", "test-boot"); ok &= !c.recover();
    struct FakePolicyIO : FramePolicyIO {
        std::string value; int toggles = 0; bool fail_after_disable = false;
        std::string get() override { return value; }
        int resolve() override { return 37; }
        bool toggle(int tx, bool enabled) override {
            if (tx != 37) return false;
            ++toggles; value = enabled ? "false" : "true";
            return enabled || !fail_after_disable;
        }
        bool text(const std::string& v) override { value = v; return true; }
    } fake;
    FramePolicy f1(fake, d + "/fps-journal", "test-boot");
    ok &= f1.apply() && fake.value == "true";
    FramePolicy f2(fake, d + "/fps-journal", "test-boot");
    ok &= f2.recover() && fake.value.empty(); // Recovery after process death.
    ok &= f2.apply(); fake.value = "false"; int count = fake.toggles;
    ok &= f2.restore() && fake.value == "false" && fake.toggles == count;
    fake.value = "true";
    FramePolicy f3(fake, d + "/fps-user-owned", "test-boot");
    ok &= f3.apply() && f3.restore() && fake.value == "true" && fake.toggles == count;
    fake.value = ""; fake.fail_after_disable = true;
    FramePolicy f4(fake, d + "/fps-failed-reply", "test-boot");
    ok &= !f4.apply() && fake.value == "true";
    FramePolicy f5(fake, d + "/fps-failed-reply", "test-boot");
    ok &= f5.recover() && fake.value.empty();
    fake.fail_after_disable = false;
    atomicfile(d + "/fps-invalid", "test-boot\n- 999 1 0\n");
    FramePolicy f6(fake, d + "/fps-invalid", "test-boot");
    ok &= !f6.recover() && fake.value.empty();
    atomicfile(d + "/fps-before-call", "test-boot\n- 37 1 0\n");
    FramePolicy f7(fake, d + "/fps-before-call", "test-boot");
    count = fake.toggles;
    ok &= f7.recover() && fake.value.empty() && fake.toggles == count;
    atomicfile(d + "/fps-wrong-method", "test-boot\n- 36 1 0\n");
    FramePolicy f8(fake, d + "/fps-wrong-method", "test-boot");
    ok &= !f8.recover() && fake.value.empty() && fake.toggles == count;
    printf("SELFTEST %s directory=%s\n", ok ? "PASS" : "FAIL", d.c_str()); return ok ? 0 : 1;
}
int main(int argc, char** argv) {
    if (getuid() != 0 || argc < 2) return 2;
    signal(SIGTERM, stop); signal(SIGINT, stop);
    mkdir("/data/adb/.config", 0700);
    mkdir(cfg.c_str(), 0700);
    std::string command = argv[1];
    if (command == "selftest") return selftest();
    Requests requests(make_nodes(), cfg + "/journal", readstr("/proc/sys/kernel/random/boot_id"));
    requests.boot.erase(std::remove(requests.boot.begin(), requests.boot.end(), '\n'), requests.boot.end());
    if (argc == 3 && std::string(argv[1]) == "target-fps") { printf("%d\n", configured_target(argv[2])); return 0; }
    DeviceFramePolicyIO fps_io;
    FramePolicy frame_policy(fps_io, cfg + "/frame-policy-journal", requests.boot);
    // Installation uses a read-only probe while the previous controller may run.
    if (command == "probe") {
        if (!compatible()) return 4;
        return requests.preflight(false) ? 0 : 1;
    }
    if (command == "fas-observe") {
        if (!compatible()) return 4;
        if (argc != 3 && argc != 4) return 2;
        char* end = nullptr; long target = strtol(argv[2], &end, 10);
        if (!*argv[2] || *end || target <= 0 || target > INT32_MAX) return 2;
        FrameObserver observer(requests.boot, true);
        for (int i = 0; i < 7 && !stopping; ++i) {
            observer.sample(static_cast<int>(target), true, argc == 4 ? argv[3] : "", requests.cpu_boost_span_khz());
            sleep(2);
        }
        observer.stop(); return 0;
    }
    int lock = open((cfg + "/controller.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) != 0) { fprintf(stderr, "controller lock failed: %s\n", strerror(errno)); return 3; }
    FrameObserver frame_observer(requests.boot);
    if (command == "restore") {
        bool frequency_ok = requests.recover();
        bool frame_ok = frame_policy.recover();
        return frequency_ok && frame_ok ? 0 : 1;
    }
    if (!compatible()) { log("Unsupported build; disabled"); return 4; }
    if (command == "probe-write") return requests.preflight(true) ? 0 : 1;
    if (command == "exercise-frame-policy") {
        if (!frame_policy.recover()) return 9;
        bool applied = frame_policy.apply();
        if (applied && !stopping) sleep(2);
        bool restored = frame_policy.restore();
        printf("FRAME_POLICY_EXERCISE applied=%d restored=%d\n", applied, restored);
        return applied && restored ? 0 : 1;
    }
    if (command == "exercise") {
        if (number("/sys/class/power_supply/battery/temp") >= 420 || !requests.recover() || !requests.preflight(false)) return 9;
        bool applied = requests.apply(false);
        if (applied) sleep(2);
        bool restored = requests.restore();
        printf("EXERCISE applied=%d restored=%d\n", applied, restored);
        return applied && restored ? 0 : 1;
    }
    bool observe = command == "observe";
    if (!observe && command != "run") return 2;
    if (!observe && conflict()) { log("Conflict: disable old uclamp / upstream Encore and reboot"); return 5; }
    if (!frame_policy.recover() || !requests.recover() || !requests.preflight(false)) return 6;
    unlink((cfg + "/request").c_str());
    atomicfile(cfg + "/controller.pid", std::to_string(getpid()) + "\n");
    pid_t parent = getppid();
    pid_t controller_pid = getpid();
    pid_t brain = fork();
    if (brain == 0) {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != controller_pid) _exit(127);
        execl((mod + "/bin/encored").c_str(), "encored", "daemon", nullptr); _exit(127);
    }
    if (brain < 0) return 7;
    atomicfile(cfg + "/brain.pid", std::to_string(brain) + "\n");
    bool hot = false; int thermal_cap_state = 3; std::string previous;
    while (!stopping && getppid() == parent && access((cfg + "/pause").c_str(), F_OK) != 0 && (observe || access((mod + "/disable").c_str(), F_OK) != 0) && access((mod + "/remove").c_str(), F_OK) != 0 && access((mod + "/update").c_str(), F_OK) != 0) {
        int status; if (waitpid(brain, &status, WNOHANG) == brain) { log("Brain exited; restoring"); brain = -1; break; }
        Scene sc = scene();
        std::ostringstream ss; ss << monotime() << ' ' << sc.awake << ' ' << sc.saver << ' ' << sc.package << ' ' << sc.pid << ' ' << sc.uid << '\n';
        if (!atomicfile(cfg + "/scene", ss.str())) break;
        int mode = 0, lite = 0, pid = 0; unsigned uid = 0; std::string pkg;
        std::istringstream request(readstr(cfg + "/request")); request >> mode >> lite >> pkg >> pid >> uid;
        long temp = number("/sys/class/power_supply/battery/temp"), capacity = number("/sys/class/power_supply/battery/capacity");
        if (temp >= 430) hot = true; else if (temp >= 100 && temp <= 410) hot = false;
        long heartbeat = number(cfg + "/heartbeat"), now = monotime();
        bool game = mode == 1 && now >= heartbeat && now - heartbeat <= 5 && sc.awake && !sc.saver && pid > 0 && pid == sc.pid && pkg == sc.package && uid == static_cast<unsigned>(sc.uid) && capacity >= 15 && temp >= 100 && temp <= 600 && !hot;
        bool reduced = lite || temp >= 420;
        bool fas_eligible = !observe && game && !reduced && temp < 420 && capacity >= 20
            && access((cfg + "/disable-fas").c_str(), F_OK) != 0;
        int frame_level = frame_observer.sample(sc.pid, fas_eligible, sc.package, requests.cpu_boost_span_khz());
        thermal_cap_state = smooth_thermal_cap(thermal_cap_state, temp);
        int thermal_cap = thermal_cap_state;
        int frame_boost = std::min(frame_level, thermal_cap);
        std::string state = observe ? "OBSERVE" : "RESTORED";
        if (game) state = observe ? (reduced ? "OBSERVE_LITE" : "OBSERVE_GAME") : (reduced ? "GAME_LITE" : "GAME");
        std::ostringstream report; report << state << " package=" << sc.package << " request=" << mode << " temp=" << temp << " capacity=" << capacity << " pid=" << pid << '\n';
        atomicfile(cfg + "/status", report.str());
        if (state + sc.package != previous) { log(report.str()); previous = state + sc.package; }
        if (!observe) {
            bool frequency_ok = game ? requests.apply(reduced, frame_boost) : requests.restore();
            // An opt-out file immediately restores the original frame policy.
            bool high_fps = game && frequency_ok && access((cfg + "/disable-high-fps").c_str(), F_OK) != 0;
            bool frame_ok = high_fps ? frame_policy.apply() : frame_policy.restore();
            if (!frequency_ok || !frame_ok) { log("Controller write/restore failure; stopping"); break; }
        }
        for (int i = 0; i < 20 && !stopping; ++i) usleep(100000);
    }
    if (brain > 0) {
        kill(brain, SIGTERM);
        for (int i = 0; i < 20; ++i) { if (waitpid(brain, nullptr, WNOHANG) == brain) { brain = -1; break; } usleep(100000); }
        if (brain > 0) { kill(brain, SIGKILL); waitpid(brain, nullptr, 0); }
    }
    bool ok = requests.restore();
    ok = frame_policy.restore() && ok;
    atomicfile(cfg + "/status", ok ? "STOPPED restored\n" : "STOPPED RESTORE_FAILED\n");
    unlink((cfg + "/controller.pid").c_str()); unlink((cfg + "/brain.pid").c_str());
    return ok ? 0 : 8;
}


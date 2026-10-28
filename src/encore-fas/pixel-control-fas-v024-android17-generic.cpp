// Copyright 2026 Pixel CP41 adaptation contributors. Apache-2.0.
// Owns only seven minimum-frequency requests. Journal precedes every write.
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <deque>
#include <elf.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <regex>
#include <map>
#include "fas-rs-frequency.h"
#include <sys/file.h>
#include <sys/mman.h>
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

static bool elf_range(size_t file_size, uint64_t offset, uint64_t length) {
    return offset <= file_size && length <= static_cast<uint64_t>(file_size) - offset;
}

// Resolve the uprobe target from libgui's exported dynamic symbol table.  The
// system library is stripped of its full .symtab on some builds, so .dynsym
// is preferred and .symtab is accepted as a harmless fallback.  No fixed
// build hash or function offset is trusted here: each QPR can move the entry.
static bool elf_queuebuffer_offset(const char* filename, std::string& offset_text) {
    offset_text.clear();
    int fd = open(filename, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat file_stat{};
    if (fstat(fd, &file_stat) != 0 || file_stat.st_size < static_cast<off_t>(sizeof(Elf64_Ehdr))) {
        close(fd); return false;
    }
    const size_t file_size = static_cast<size_t>(file_stat.st_size);
    void* mapping = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mapping == MAP_FAILED) return false;
    const auto* bytes = static_cast<const unsigned char*>(mapping);
    const auto* header = reinterpret_cast<const Elf64_Ehdr*>(bytes);
    bool found = false;
    bool bad = false;
    uint64_t found_value = 0;
    do {
        if (memcmp(header->e_ident, ELFMAG, SELFMAG) != 0
            || header->e_ident[EI_CLASS] != ELFCLASS64
            || header->e_ident[EI_DATA] != ELFDATA2LSB
            || header->e_machine != EM_AARCH64
            || header->e_type != ET_DYN
            || header->e_shentsize != sizeof(Elf64_Shdr)
            || header->e_shoff == 0 || header->e_shnum == 0
            || !elf_range(file_size, header->e_shoff,
                          static_cast<uint64_t>(header->e_shentsize) * header->e_shnum)) {
            break;
        }
        if (header->e_phentsize != sizeof(Elf64_Phdr) || header->e_phnum == 0
            || header->e_phoff == 0
            || !elf_range(file_size, header->e_phoff,
                          static_cast<uint64_t>(header->e_phentsize) * header->e_phnum)) {
            break;
        }
        const auto* programs = reinterpret_cast<const Elf64_Phdr*>(bytes + header->e_phoff);
        const auto* sections = reinterpret_cast<const Elf64_Shdr*>(bytes + header->e_shoff);
        const uint64_t section_count = header->e_shnum;
        for (uint64_t section_index = 0; section_index < section_count && !bad; ++section_index) {
            const auto& symbols_section = sections[section_index];
            if (symbols_section.sh_type != SHT_DYNSYM && symbols_section.sh_type != SHT_SYMTAB)
                continue;
            if (symbols_section.sh_link >= section_count
                || symbols_section.sh_entsize != sizeof(Elf64_Sym)
                || symbols_section.sh_size < sizeof(Elf64_Sym)
                || !elf_range(file_size, symbols_section.sh_offset, symbols_section.sh_size))
                continue;
            const auto& strings_section = sections[symbols_section.sh_link];
            if (strings_section.sh_type != SHT_STRTAB
                || !elf_range(file_size, strings_section.sh_offset, strings_section.sh_size))
                continue;
            const auto* symbols = bytes + symbols_section.sh_offset;
            const auto* strings = reinterpret_cast<const char*>(bytes + strings_section.sh_offset);
            const uint64_t symbol_count = symbols_section.sh_size / symbols_section.sh_entsize;
            for (uint64_t symbol_index = 0; symbol_index < symbol_count; ++symbol_index) {
                const uint64_t symbol_offset = symbol_index * symbols_section.sh_entsize;
                if (!elf_range(file_size, symbols_section.sh_offset + symbol_offset, sizeof(Elf64_Sym)))
                    break;
                const auto* symbol = reinterpret_cast<const Elf64_Sym*>(symbols + symbol_offset);
                if (symbol->st_name >= strings_section.sh_size || symbol->st_shndx == SHN_UNDEF
                    || symbol->st_value == 0 || symbol->st_size < 4
                    || (symbol->st_value & 3) != 0 || ELF64_ST_TYPE(symbol->st_info) != STT_FUNC)
                    continue;
                const char* name = strings + symbol->st_name;
                const size_t remaining = strings_section.sh_size - symbol->st_name;
                const void* terminator = memchr(name, '\0', remaining);
                if (!terminator) continue;
                const std::string_view symbol_name(name,
                    static_cast<const char*>(terminator) - name);
                // Android's current name includes the ANativeWindow parameter;
                // matching the stable Surface method prefix also handles ABI
                // spelling changes while excluding hook_queueBuffer_DEPRECATE.
                if (symbol_name.find("_ZN7android7Surface16hook_queueBuffer") == 0
                    && symbol_name.find("_DEPRECATE") == std::string_view::npos) {
                    bool mapped = false;
                    for (uint16_t program_index = 0; program_index < header->e_phnum; ++program_index) {
                        const auto& program = programs[program_index];
                        if (program.p_type != PT_LOAD || !(program.p_flags & PF_X)
                            || symbol->st_value < program.p_vaddr)
                            continue;
                        const uint64_t delta = symbol->st_value - program.p_vaddr;
                        if (delta > program.p_filesz || 4 > program.p_filesz - delta
                            || !elf_range(file_size, program.p_offset, program.p_filesz))
                            continue;
                        const uint64_t offset = program.p_offset + delta;
                        if (offset & 3) continue;
                        if (found && found_value != offset) bad = true;
                        found_value = offset;
                        found = mapped = true;
                    }
                    if (!mapped) bad = true;
                }
            }
        }
    } while (false);
    munmap(mapping, file_size);
    if (!found || bad) return false;
    std::ostringstream value;
    value << "0x" << std::hex << found_value;
    offset_text = value.str();
    return true;
}
static std::string regex_escape_package(const std::string& package) {
    static constexpr std::string_view special = R"(.^$|()[]*+?\\{})";
    std::string escaped;
    escaped.reserve(package.size() * 2);
    for (char ch : package) {
        if (special.find(ch) != std::string_view::npos) escaped.push_back('\\');
        escaped.push_back(ch);
    }
    return escaped;
}
static int configured_target(const std::string& package) {
    const std::string games = readstr(cfg + "/gamelist.json");
    const std::regex entry("\\\"" + regex_escape_package(package) + "\\\"\\s*:\\s*\\{[^}]*\\\"target_fps\\\"\\s*:\\s*(\\d+)\\s*[,}]");
    std::smatch match;
    if (!std::regex_search(games, match, entry) || match.size() < 2) return 0;
    const std::string rate = match[1].str();
    if (rate == "0") return 0;
    if (rate == "30" || rate == "60" || rate == "90" || rate == "120") return std::atoi(rate.c_str());
    return 0;
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
    if (temp < 0 || temp > 600) return current;
    // Temperature is reported in deci-degrees Celsius. Escalation is
    // aggressive; recovery requires a 1 C margin to prevent oscillation.
    if (current == 0 && temp <= 410) return 1;
    if (current <= 1 && temp <= 400) return 2;
    if (current <= 2 && temp <= 390) return 3;
    if (temp >= 430) return 0;
    if (temp >= 420) return std::min(current, 0);
    if (temp >= 410) return std::min(current, 1);
    if (temp >= 400) return std::min(current, 2);
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
        while (table >> value) choices.push_back(value);
        return pixel_quantize_floor(std::move(choices), requested, ceiling);
    }
    bool apply(bool lite, int boost_level = 0, const std::map<int, long>* upstream = nullptr, int thermal_cap = 3) {
        for (auto& n : nodes) {
            if (n.blocked) continue;
            long cur = number(n.path), ceiling = number(n.maximum);
            if (cur < 0 || ceiling < 0) { restore(); return false; }
            if (n.active && cur != n.last) { n.active = false; n.blocked = true; save(); log("YIELD external writer " + n.path); continue; }
            if (!n.active) n.original = cur;
            long floor = lite ? n.lite : staged_floor(n, upstream ? 0 : boost_level);
            if (!lite && upstream && n.path.find("/cpu/cpufreq/policy") != std::string::npos) {
                auto at = n.path.find("/policy") + 7;
                int policy = std::atoi(n.path.c_str() + at);
                auto request = upstream->find(policy);
                if (request != upstream->end()) {
                    // Upstream computes kHz; Pixel enforces the existing bounded
                    // boost envelope, thermal cap, journal, and current HAL ceiling.
                    long top = staged_floor(n, thermal_cap);
                    floor = std::clamp(request->second, n.lite, std::max(n.lite, top));
                }
            }
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
    auto sdk = capture({"/system/bin/getprop", "ro.build.version.sdk"});
    // Device-independent Android 17 gate. Frequency domains are probed at
    // runtime; unknown libgui builds keep baseline control and disable FAS.
    return sdk == "37\n";
}
static bool conflict() {
    for (const auto& id : {"pixel_pubg_uclamp_cp41", "encore", "fas_rs", "fas_rs_pixel_cp41"}) {
        std::string p = "/data/adb/modules/" + std::string(id);
        if (access((p + "/module.prop").c_str(), F_OK) == 0 && access((p + "/disable").c_str(), F_OK) != 0 && access((p + "/remove").c_str(), F_OK) != 0) return true;
    }
    return false;
}
#include "fas-rs-observer.h"
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
    ok &= smooth_thermal_cap(3, 420) == 0;
    ok &= smooth_thermal_cap(0, 410) == 1;
    ok &= smooth_thermal_cap(1, 400) == 2;
    ok &= smooth_thermal_cap(2, 390) == 3;
    ok &= smooth_thermal_cap(0, -1) == 0;
    std::string queuebuffer_offset;
    ok &= elf_queuebuffer_offset("/system/lib64/libgui.so", queuebuffer_offset)
        && queuebuffer_offset.size() > 2 && queuebuffer_offset.rfind("0x", 0) == 0;
    if (!queuebuffer_offset.empty()) fprintf(stderr, "ELF_QUEUEBUFFER_OFFSET %s\n", queuebuffer_offset.c_str());
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
        std::ostringstream report; report << state << " package=" << sc.package << " request=" << mode << " temp=" << temp << " capacity=" << capacity << " pid=" << pid << " FAS_LEVEL=" << frame_boost << '\n';
        atomicfile(cfg + "/status", report.str());
        if (state + sc.package != previous) { log(report.str()); previous = state + sc.package; }
        if (!observe) {
            bool frequency_ok = game ? requests.apply(reduced, frame_boost, &frame_observer.cpu_requests(), thermal_cap) : requests.restore();
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


// Copyright 2026 Pixel adaptation contributors. Apache-2.0.
#pragma once
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
struct FasRsSnapshot {
    uint64_t stamp = 0, sequence = 0;
    int target = 0, milli_fps = 0;
    bool ready = false;
    std::map<int, long> cpu;
};
inline bool parse_fas_rs(const std::string& line, int pid, uint64_t start,
                         uint64_t now, uint64_t previous, FasRsSnapshot& result) {
    std::istringstream in(line);
    std::string magic, extra;
    int got_pid = 0, ready = -1, count = -1;
    uint64_t got_start = 0;
    FasRsSnapshot next;
    if (!(in >> magic >> got_pid >> got_start >> next.stamp >> next.sequence
          >> next.target >> next.milli_fps >> ready >> count)) return false;
    if (magic != "FASRS1" || got_pid != pid || got_start != start
        || next.sequence <= previous || next.stamp > now || now - next.stamp > 1500
        || next.target < 0 || next.target > 120 || next.milli_fps < 0 || next.milli_fps > 1000000
        || (ready != 0 && ready != 1) || count < 0 || count > 32) return false;
    if (next.target && next.target != 24 && next.target != 25 && next.target != 30
        && next.target != 40 && next.target != 45 && next.target != 48 && next.target != 60
        && next.target != 72 && next.target != 80 && next.target != 90 && next.target != 120) return false;
    if ((ready && (next.target == 0 || count == 0)) || (!ready && count)) return false;
    for (int i = 0; i < count; ++i) {
        int policy = -1; long frequency = 0;
        if (!(in >> policy >> frequency) || policy < 0 || policy > 255
            || frequency <= 0 || frequency > 10000000 || next.cpu.count(policy)) return false;
        next.cpu.emplace(policy, frequency);
    }
    if (in >> extra) return false;
    next.ready = ready;
    result = std::move(next);
    return true;
}

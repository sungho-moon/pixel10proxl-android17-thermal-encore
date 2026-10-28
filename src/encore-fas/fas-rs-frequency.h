// Copyright 2026 Pixel adaptation contributors. Apache-2.0.
#pragma once
#include <algorithm>
#include <vector>
inline long pixel_quantize_floor(std::vector<long> choices, long requested, long ceiling) {
    choices.erase(std::remove_if(choices.begin(), choices.end(), [ceiling](long f) { return f <= 0 || f > ceiling; }), choices.end());
    if (choices.empty()) return 0;
    std::sort(choices.begin(), choices.end());
    auto at = std::lower_bound(choices.begin(), choices.end(), requested);
    return at == choices.end() ? choices.back() : *at;
}

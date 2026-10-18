// Reconstructed third-party behavior; provenance and license boundary: VoiceRandomizer.h.
#include "VoiceRandomizer.h"
#include <intrin.h>

namespace voice {
const IndexEntry kIndex[827] = {
#include "voice_index.inc"
};
void Rules::Configure(const Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (target_ != config.targetOld || guarantee_ != config.guarantee || radiance_ != config.radiance) {
        target_ = config.targetOld;
        guarantee_ = config.guarantee;
        radiance_ = config.radiance;
        count_ = 0;
        pending_ = false;
        ++epoch_;
    }
}
Decision Rules::Choose(const std::string& name, const Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    Decision result;
    result.event = ++sequence_;
    result.epoch = epoch_;
    result.countBefore = count_;
    result.pendingBefore = pending_;
    result.targetOld = target_;
    result.guarantee = guarantee_;
    result.radiance = radiance_;
    if (config.mode != Mode::Random) {
        if (config.mode == Mode::Alternate) {
            result.selectedOld = (++alternate_[name] % 2) == 0;
            result.reason = 6;
        } else {
            result.selectedOld = config.mode == Mode::Old;
            result.reason = result.selectedOld ? 5u : 4u;
        }
    } else if (guarantee_ && pending_) {
        pending_ = false;
        result.selectedOld = target_;
        result.reason = 2;
    } else {
        // Original mt19937_64 multiply-high mapping, with rejection threshold 16.
        uint64_t low = 0, high = 0;
        do { low = _umul128(random_(), 100, &high); } while (low < 16);
        result.roll = static_cast<int>(high);
        if (radiance_ && count_ >= 3) {
            count_ = 0;
            result.selectedOld = target_;
            result.reason = 3;
        } else {
            result.selectedOld = result.roll < config.oldPercent;
            result.reason = result.selectedOld ? 1u : 0u;
            if (radiance_) {
                if (result.selectedOld == target_) { if (count_) --count_; }
                else ++count_;
            }
            pending_ = guarantee_ && result.selectedOld != target_;
        }
    }
    result.countAfter = count_;
    result.pendingAfter = pending_;
    return result;
}
}

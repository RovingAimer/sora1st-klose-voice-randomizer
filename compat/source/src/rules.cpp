// Reconstructed third-party behavior; provenance and license boundary: VoiceRandomizer.h.
#include "VoiceRandomizer.h"
#include <algorithm>
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
        softCount_ = 0;
        ++epoch_;
    }
    if (softEnabled_ != config.softPity || softStart_ != config.softPityStart || softStep_ != config.softPityStep) {
        softEnabled_ = config.softPity;
        softStart_ = config.softPityStart;
        softStep_ = config.softPityStep;
        softCount_ = 0;
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
    result.softBefore = softCount_;
    const int baseTarget = target_ ? config.oldPercent : 100 - config.oldPercent;
    uint64_t targetPercent = static_cast<uint64_t>(baseTarget);
    if (softEnabled_ && softCount_ >= static_cast<uint32_t>(softStart_)) {
        targetPercent += (static_cast<uint64_t>(softCount_) - softStart_ + 1) * softStep_;
    }
    result.targetPercent = static_cast<int>(std::min(targetPercent, uint64_t{100}));
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
            const int oldPercent = target_ ? result.targetPercent : 100 - result.targetPercent;
            result.selectedOld = result.roll < oldPercent;
            result.reason = result.selectedOld ? 1u : 0u;
            if (radiance_) {
                if (result.selectedOld == target_) { if (count_) --count_; }
                else ++count_;
            }
            pending_ = guarantee_ && result.selectedOld != target_;
        }
    }
    if (config.mode == Mode::Random) {
        if (result.selectedOld == target_) softCount_ = 0;
        else if (softCount_ < UINT32_MAX) ++softCount_;
    }
    result.countAfter = count_;
    result.pendingAfter = pending_;
    result.softAfter = softCount_;
    return result;
}
}

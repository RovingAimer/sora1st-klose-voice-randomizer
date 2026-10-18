#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>

namespace vr {
// Verified from 0x4A2015..0x4A2086 and the caller at 0x41CC40.
struct alignas(8) PlayRequest {
    uint32_t category;
    uint32_t reserved;
    const char* name;
    std::array<std::byte, 0xc0> rest;
};
static_assert(sizeof(PlayRequest) == 0xd0);
static_assert(offsetof(PlayRequest, name) == 8);

enum class Mode { Random, Alternate, New, Old };
struct Rules {
    bool target_old = false;
    bool guarantee = true;
    bool radiance = true;
    bool operator==(const Rules& other) const {
        return target_old == other.target_old && guarantee == other.guarantee && radiance == other.radiance;
    }
    bool operator!=(const Rules& other) const { return !(*this == other); }
};
enum class Reason { RandomNew, RandomOld, Guarantee, CapturingRadiance, ForcedNew, ForcedOld, Alternate };
inline const char* reason_name(Reason reason) {
    switch (reason) {
    case Reason::RandomNew: return "random_new";
    case Reason::RandomOld: return "random_old";
    case Reason::Guarantee: return "guarantee";
    case Reason::CapturingRadiance: return "capturing_radiance";
    case Reason::ForcedNew: return "forced_new";
    case Reason::ForcedOld: return "forced_old";
    default: return "alternate";
    }
}
struct Selection {
    bool old = false;
    uint64_t sequence = 0;
    Reason reason = Reason::RandomNew;
    int roll = -1; // -1: no RNG draw (guarantee or a diagnostic mode).
    unsigned count_before = 0, count_after = 0;
    bool guarantee_before = false, guarantee_after = false;
    Rules rules;
    uint64_t rules_epoch = 0;
};
class Selector {
    std::mutex mutex_;
    std::mt19937_64 random_;
    std::function<unsigned()> draw_override_;
    std::unordered_map<std::string, uint64_t> per_name_;
    uint64_t sequence_ = 0;
    // Shared across all eligible voice names, scoped to this game process.
    unsigned radiance_count_ = 0;
    bool guarantee_pending_ = false;
    Rules rules_;
    uint64_t rules_epoch_ = 0;
public:
    explicit Selector(uint64_t seed, std::function<unsigned()> draw_override = {})
        : random_(seed), draw_override_(std::move(draw_override)) {}
    void configure_rules(Rules rules) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (rules == rules_) return;
        rules_ = rules;
        radiance_count_ = 0;
        guarantee_pending_ = false;
        ++rules_epoch_;
    }
    Selection choose(const std::string& name, Mode mode, unsigned percent) {
        std::lock_guard<std::mutex> lock(mutex_);
        Selection result;
        result.sequence = ++sequence_;
        result.rules = rules_;
        result.rules_epoch = rules_epoch_;
        result.count_before = radiance_count_;
        result.guarantee_before = guarantee_pending_;
        if (mode == Mode::Random) {
            if (rules_.guarantee && guarantee_pending_) {
                // This event has priority over radiance and consumes no RNG draw.
                guarantee_pending_ = false;
                result.old = rules_.target_old;
                result.reason = Reason::Guarantee;
            } else {
                result.roll = static_cast<int>(draw_override_ ? draw_override_() : std::uniform_int_distribution<unsigned>(0, 99)(random_));
                if (rules_.radiance && radiance_count_ >= 3) {
                    // Override this random opportunity regardless of its raw result.
                    // It neither uses nor creates a guarantee, even if the roll was old.
                    radiance_count_ = 0;
                    result.old = rules_.target_old;
                    result.reason = Reason::CapturingRadiance;
                } else {
                    result.old = static_cast<unsigned>(result.roll) < percent;
                    const bool missed_target = result.old != rules_.target_old;
                    if (rules_.radiance) {
                        if (missed_target) ++radiance_count_;
                        else if (radiance_count_) --radiance_count_;
                    }
                    guarantee_pending_ = rules_.guarantee && missed_target;
                    result.reason = result.old ? Reason::RandomOld : Reason::RandomNew;
                }
            }
        } else if (mode == Mode::Old) {
            result.old = true;
            result.reason = Reason::ForcedOld;
        } else if (mode == Mode::Alternate) {
            result.old = (++per_name_[name] % 2) == 0;
            result.reason = Reason::Alternate;
        } else {
            result.reason = Reason::ForcedNew;
        }
        // Diagnostic modes pause the random-mode state rather than changing it.
        result.count_after = radiance_count_;
        result.guarantee_after = guarantee_pending_;
        return result;
    }
};

inline bool voice_name(const std::string& s) {
    // Syntax only: the runtime asset map is the authoritative allowlist.
    // Longest alias prefix has 8 bytes; game cache keys hold 63 plus terminator.
    if (s.size() < 6 || s.size() > 55 || s[0] != 'v' || s[4] != '_') return false;
    for (size_t i = 1; i < 4; ++i) if (s[i] < '0' || s[i] > '9') return false;
    for (size_t i = 5; i < s.size(); ++i)
        if (!(s[i] >= 'a' && s[i] <= 'z') && !(s[i] >= '0' && s[i] <= '9') && s[i] != '_') return false;
    return true;
}
inline std::string normalized(std::string s) {
    for (char& c : s) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    return s;
}
inline bool parse_voice_path(const std::string& path, std::string& name, bool& old) {
    const std::string p = normalized(path);
    const std::string prefix = "voice/wav/";
    if (p.compare(0, prefix.size(), prefix) != 0) return false;
    std::string tail = p.substr(prefix.size());
    const bool placeholder = tail.compare(0, 8, "vr_test_") == 0;
    old = placeholder || tail.compare(0, 7, "vr_old_") == 0;
    if (old) tail.erase(0, placeholder ? 8 : 7);
    if (tail.size() < 4 || tail.substr(tail.size()-4) != ".wav") return false;
    tail.resize(tail.size()-4);
    if (!voice_name(tail)) return false;
    name = tail;
    return true;
}
inline PlayRequest redirected(const PlayRequest& input, const char* new_name) {
    PlayRequest copy;
    std::memcpy(&copy, &input, sizeof(copy));
    copy.name = new_name;
    return copy;
}
} // namespace vr

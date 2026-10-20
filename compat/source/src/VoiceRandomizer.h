// Local reconstruction from Sora1st-克萝赛随机语音 v1.2.0 (original DLL
// SHA256 c0d344f13742234bb448749baa1eed262e1fee6aad289ed3a9c8c2d7eacb5c92).
// See analysis/binary-static/reconstruction-core.md and ui-static/reconstruction-ui.md.
// Third-party derived behavior; no new license grant or public redistribution permission.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace voice {
constexpr size_t kMaxHistory = 50;
enum class Mode { Random, Alternate, New, Old };
struct OverlayConfig {
    bool enabled = true;
    float left = .02f, center = .40f, height = .50f, scrollMs = 180.f;
    int maxEntries = 10;
    float transparency = 0.f, waiScale = 1.f;
    bool hideOnOtherVoice = false;
};
struct Config {
    bool enabled = true, logCache = false, targetOld = false;
    bool guarantee = true, radiance = true;
    Mode mode = Mode::Random;
    int oldPercent = 50;
    bool softPity = false;
    int softPityStart = 10, softPityStep = 5;
    bool stereo = false, stereoSwap = false;
    int stereoLeft = 100, stereoRight = 100;
    OverlayConfig overlay;
};
struct Decision {
    bool selectedOld = false;
    uint64_t event = 0, epoch = 0;
    uint32_t reason = 4, countBefore = 0, countAfter = 0;
    int roll = -1;
    bool pendingBefore = false, pendingAfter = false, targetOld = false;
    bool guarantee = true, radiance = true;
    uint32_t softBefore = 0, softAfter = 0;
    int targetPercent = 0;
};
class Rules {
public:
    explicit Rules(uint64_t seed) : random_(seed) {}
    void Configure(const Config& config);
    Decision Choose(const std::string& name, const Config& config);
private:
    std::mutex mutex_;
    std::mt19937_64 random_;
    std::unordered_map<std::string, uint64_t> alternate_;
    uint64_t sequence_ = 0, epoch_ = 0;
    uint32_t count_ = 0;
    bool pending_ = false, target_ = false, guarantee_ = true, radiance_ = true;
    uint32_t softCount_ = 0;
    bool softEnabled_ = false;
    int softStart_ = 10, softStep_ = 5;
};
struct IndexEntry { const char* name; uint32_t id; };
extern const IndexEntry kIndex[827];
struct Resource {
    uint32_t id;
    std::string alias;
    std::wstring path;
};
struct HistoryEntry {
    uint64_t key;
    uint32_t id;
    std::string name;
    bool old;
    int badge;
    bool stereo = false, swap = false;
};
void Log(const char* format, ...) noexcept;
void SetLogPath(const std::wstring& root);
void ConfigureOverlay(const OverlayConfig& config);
void InitializeOverlay(const std::wstring& root);
void AppendHistory(uint64_t ticket, uint32_t id, const std::string& name,
                   bool old, uint32_t reason, uint32_t handle, bool targetOld,
                   bool stereo = false, bool swap = false);
void NotifyVoicePlayed(bool klose) noexcept;
void DrawHistory(IDXGISwapChain* chain) noexcept;
}

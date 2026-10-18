#pragma once
#include "core.h"
#include <algorithm>
#include <vector>
#include <string>

struct IDXGISwapChain;
namespace vr::ui {
enum class Badge { None, Radiance, Wai };
inline Badge badge_for(Reason reason,bool target_old=false) {
    return reason==Reason::CapturingRadiance ? Badge::Radiance :
           reason==(target_old?Reason::RandomNew:Reason::RandomOld) ? Badge::Wai : Badge::None;
}
struct Entry {
    uint64_t ticket=0;
    unsigned voice_id=0;
    std::string name;
    bool old=false;
    Badge badge=Badge::None;
};
class History {
    std::atomic<uint64_t> sequence_{0};
    mutable std::mutex mutex_;
    std::vector<Entry> rows_;
public:
    uint64_t ticket() { return ++sequence_; }
    void push(Entry row) {
        std::lock_guard<std::mutex> guard(mutex_);
        auto at=std::lower_bound(rows_.begin(),rows_.end(),row.ticket,[](const Entry& a,uint64_t b){return a.ticket<b;});
        if (at!=rows_.end() && at->ticket==row.ticket) return;
        rows_.insert(at,std::move(row));
        if (rows_.size()>10) rows_.erase(rows_.begin());
    }
    std::vector<Entry> snapshot() const { std::lock_guard<std::mutex> guard(mutex_); return rows_; }
};
struct Config { bool enabled=true; float left=.02f, center=.5f, height=.5f, scroll_ms=180.f; };
struct Layout {
    float left, top, bottom, pitch, side, gap;
    static Layout make(float width,float height,const Config& c) {
        const float span=height*c.height;
        const float top=std::clamp(height*c.center-span*.5f,0.f,height-span);
        const float pitch=span/10.f;
        return {std::clamp(width*c.left,0.f,std::max(0.f,width-pitch*2)),top,top+span,pitch,pitch*.86f,pitch*.13f};
    }
    float center_for_rank(size_t rank) const { return bottom-pitch*(.5f+static_cast<float>(rank)); }
};
History& history();
using Logger=void (*)(const char*,...);
void initialize(const std::wstring& directory,Logger logger);
void configure(Config config);
void draw(IDXGISwapChain* swap);
} // namespace vr::ui

#include "overlay_renderer.h"
#include <dxgi.h>
#include <chrono>

namespace vr::ui {
namespace {
History records;
std::mutex settings_mutex,render_mutex;
Config settings;
std::wstring images;
Logger logger=nullptr;
Renderer renderer;
bool announced=false;
double retry_after=0;
}
History& history() { return records; }
void initialize(const std::wstring& directory,Logger log) { images=directory+L"/assets/ui"; logger=log; }
void configure(Config config) {
    config.left=std::clamp(config.left,0.f,.9f); config.height=std::clamp(config.height,.1f,1.f);
    config.center=std::clamp(config.center,0.f,1.f); config.scroll_ms=std::clamp(config.scroll_ms,0.f,2000.f);
    std::lock_guard<std::mutex> guard(settings_mutex); settings=config;
}
void draw(IDXGISwapChain* swap) {
    Config config; { std::lock_guard<std::mutex> guard(settings_mutex); config=settings; }
    if(!swap || !config.enabled) return;
    auto rows=records.snapshot(); if(rows.empty()) return;
    std::lock_guard<std::mutex> guard(render_mutex);
    const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if(now<retry_after) return;
    try {
        ComPtr<ID3D11Texture2D> target;
        HRESULT result=swap->GetBuffer(0,IID_PPV_ARGS(&target));
        if(SUCCEEDED(result)) result=renderer.render(target.Get(),images,rows,config,now);
        if(FAILED(result)) {
            if(logger) logger("UI_ERROR hresult=%08lx; retry_in=5s; voice selection unaffected",static_cast<unsigned long>(result));
            renderer.reset(); retry_after=now+5;
        } else if(result==S_OK && !announced) {
            D3D11_TEXTURE2D_DESC desc{}; target->GetDesc(&desc);
            if(logger) logger("UI_READY width=%u height=%u images=4 history=10 newest=bottom",desc.Width,desc.Height);
            announced=true;
        }
    } catch(...) {
        if(logger) logger("UI_ERROR exception; retry_in=5s; voice selection unaffected");
        renderer.reset(); retry_after=now+5;
    }
}
} // namespace vr::ui

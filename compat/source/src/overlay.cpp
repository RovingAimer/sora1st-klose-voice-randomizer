// Reconstructed third-party behavior; provenance and license boundary: VoiceRandomizer.h.
#include "VoiceRandomizer.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace voice {
namespace {
using Microsoft::WRL::ComPtr;
std::mutex configMutex, historyMutex, gpuMutex;
OverlayConfig configuration;
std::vector<HistoryEntry> history;
std::wstring uiRoot;
struct Animation { uint64_t key; float row; double born; };
struct Vertex { float x, y, u, v, alpha; };
static_assert(sizeof(Vertex) == 20);
struct Draw { UINT first; size_t image; };
struct Renderer {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> buffer;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depth;
    std::array<ComPtr<ID3D11ShaderResourceView>, 4> images;
    std::vector<Animation> animations;
    double last = 0;
    void Reset() {
        device.Reset(); vs.Reset(); ps.Reset(); layout.Reset(); buffer.Reset();
        sampler.Reset(); blend.Reset(); raster.Reset(); depth.Reset();
        for (auto& image : images) image.Reset();
        animations.clear(); last = 0;
    }
} renderer;
double retryAt = 0;
bool readyLogged = false;
void Check(HRESULT result) { if (FAILED(result)) throw result; }
double Seconds() {
    LARGE_INTEGER now{}, frequency{};
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
    return static_cast<double>(now.QuadPart) / static_cast<double>(frequency.QuadPart);
}
struct ComScope {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
};
ComPtr<ID3D11ShaderResourceView> DecodePng(ID3D11Device* device, const std::wstring& path) {
    ComScope com;
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder));
    ComPtr<IWICBitmapFrameDecode> frame;
    Check(decoder->GetFrame(0, &frame));
    UINT width = 0, height = 0;
    Check(frame->GetSize(&width, &height));
    if (!width || !height || width > 2048 || height > 2048) throw HRESULT(E_INVALIDARG);
    ComPtr<IWICFormatConverter> converter;
    Check(factory->CreateFormatConverter(&converter));
    Check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                               nullptr, 0.0, WICBitmapPaletteTypeCustom));
    std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
    Check(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width; description.Height = height;
    description.MipLevels = 1; description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA input{pixels.data(), width * 4, 0};
    ComPtr<ID3D11Texture2D> texture;
    Check(device->CreateTexture2D(&description, &input, &texture));
    ComPtr<ID3D11ShaderResourceView> image;
    Check(device->CreateShaderResourceView(texture.Get(), nullptr, &image));
    return image;
}
void CreateRenderer(ID3D11Device* device) {
    renderer.Reset();
    constexpr char shader[] =
        "Texture2D tex:register(t0); SamplerState samp:register(s0);"
        "struct V {float2 p:POSITION; float2 uv:TEXCOORD0; float a:TEXCOORD1;};"
        "struct P {float4 p:SV_POSITION; float2 uv:TEXCOORD0; float a:TEXCOORD1;};"
        "P VSMain(V v){P o; o.p=float4(v.p,0,1); o.uv=v.uv; o.a=v.a; return o;}"
        "float4 PSMain(P p):SV_TARGET {float4 c=tex.Sample(samp,p.uv);c.a*=p.a;return c;}";
    ComPtr<ID3DBlob> vs, ps;
    Check(D3DCompile(shader, sizeof(shader)-1, nullptr, nullptr, nullptr, "VSMain", "vs_4_0",
                     D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, nullptr));
    Check(D3DCompile(shader, sizeof(shader)-1, nullptr, nullptr, nullptr, "PSMain", "ps_4_0",
                     D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, nullptr));
    Check(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &renderer.vs));
    Check(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &renderer.ps));
    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
    Check(device->CreateInputLayout(elements, 3, vs->GetBufferPointer(), vs->GetBufferSize(), &renderer.layout));
    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = 2400; buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER; buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Check(device->CreateBuffer(&buffer, nullptr, &renderer.buffer));
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MaxLOD = FLT_MAX;
    Check(device->CreateSamplerState(&sampler, &renderer.sampler));
    D3D11_BLEND_DESC blend{};
    auto& target = blend.RenderTarget[0];
    target.BlendEnable = TRUE; target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; target.BlendOp = D3D11_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D11_BLEND_ONE; target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOpAlpha = D3D11_BLEND_OP_ADD; target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    Check(device->CreateBlendState(&blend, &renderer.blend));
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE; raster.ScissorEnable = TRUE;
    Check(device->CreateRasterizerState(&raster, &renderer.raster));
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    Check(device->CreateDepthStencilState(&depth, &renderer.depth));
    constexpr const wchar_t* names[] = {L"avatar_new.png", L"avatar_old.png", L"badge_radiance.png", L"badge_wai.png"};
    for (size_t i = 0; i < renderer.images.size(); ++i) renderer.images[i] = DecodePng(device, uiRoot + L"\\" + names[i]);
    renderer.device = device;
}
template<class T, size_t N> void ReleaseAll(T* (&values)[N]) noexcept {
    for (T* value : values) if (value) value->Release();
}
// Save exactly the modified pipeline range, including shader class instances,
// predicate and OM UAVs. Getter references are released after restoration.
struct Pipeline {
    ID3D11DeviceContext* context;
    ID3D11RenderTargetView* targets[8]{};
    ID3D11DepthStencilView* dsv = nullptr;
    ID3D11UnorderedAccessView* uavs[64]{};
    UINT uavLimit, targetCount = 0;
    ID3D11BlendState* blend = nullptr;
    FLOAT factors[4]{}; UINT mask = 0;
    ID3D11DepthStencilState* depth = nullptr; UINT stencil = 0;
    ID3D11RasterizerState* raster = nullptr;
    D3D11_VIEWPORT viewports[16]{}; UINT viewportCount = 16;
    D3D11_RECT scissors[16]{}; UINT scissorCount = 16;
    ID3D11InputLayout* layout = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology{};
    ID3D11Buffer* buffer = nullptr; UINT stride = 0, offset = 0;
    ID3D11ShaderResourceView* resource = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11Predicate* predicate = nullptr; BOOL predicateValue = FALSE;
    ID3D11VertexShader* vs = nullptr; ID3D11ClassInstance* vc[256]{}; UINT vn = 256;
    ID3D11PixelShader* ps = nullptr; ID3D11ClassInstance* pc[256]{}; UINT pn = 256;
    ID3D11GeometryShader* gs = nullptr; ID3D11ClassInstance* gc[256]{}; UINT gn = 256;
    ID3D11HullShader* hs = nullptr; ID3D11ClassInstance* hc[256]{}; UINT hn = 256;
    ID3D11DomainShader* ds = nullptr; ID3D11ClassInstance* dc[256]{}; UINT dn = 256;
    Pipeline(ID3D11DeviceContext* ctx, D3D_FEATURE_LEVEL feature) : context(ctx), uavLimit(feature >= D3D_FEATURE_LEVEL_11_1 ? 64u : 8u) {
        ctx->OMGetRenderTargets(8, targets, &dsv);
        for (UINT i = 0; i < 8; ++i) if (targets[i]) targetCount = i+1;
        ctx->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, uavLimit, uavs);
        ctx->OMGetBlendState(&blend, factors, &mask); ctx->OMGetDepthStencilState(&depth, &stencil);
        ctx->RSGetState(&raster); ctx->RSGetViewports(&viewportCount, viewports); ctx->RSGetScissorRects(&scissorCount, scissors);
        ctx->IAGetInputLayout(&layout); ctx->IAGetPrimitiveTopology(&topology); ctx->IAGetVertexBuffers(0,1,&buffer,&stride,&offset);
        ctx->PSGetShaderResources(0,1,&resource); ctx->PSGetSamplers(0,1,&sampler);
        ctx->GetPredication(&predicate,&predicateValue);
        ctx->VSGetShader(&vs,vc,&vn); ctx->PSGetShader(&ps,pc,&pn); ctx->GSGetShader(&gs,gc,&gn);
        ctx->HSGetShader(&hs,hc,&hn); ctx->DSGetShader(&ds,dc,&dn);
    }
    ~Pipeline() {
        context->OMSetRenderTargets(targetCount,targets,dsv);
        UINT counts[64]; std::fill(std::begin(counts),std::end(counts),UINT32_MAX);
        context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,
            nullptr,nullptr,targetCount,uavLimit-targetCount,uavs+targetCount,counts+targetCount);
        context->OMSetBlendState(blend,factors,mask); context->OMSetDepthStencilState(depth,stencil);
        context->RSSetState(raster); context->RSSetViewports(viewportCount,viewports); context->RSSetScissorRects(scissorCount,scissors);
        context->IASetInputLayout(layout); context->IASetPrimitiveTopology(topology); context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
        context->PSSetShaderResources(0,1,&resource); context->PSSetSamplers(0,1,&sampler);
        context->SetPredication(predicate,predicateValue);
        context->VSSetShader(vs,vc,vn); context->PSSetShader(ps,pc,pn); context->GSSetShader(gs,gc,gn);
        context->HSSetShader(hs,hc,hn); context->DSSetShader(ds,dc,dn);
        ReleaseAll(targets); ReleaseAll(uavs); ReleaseAll(vc); ReleaseAll(pc); ReleaseAll(gc); ReleaseAll(hc); ReleaseAll(dc);
        if(dsv)dsv->Release(); if(blend)blend->Release(); if(depth)depth->Release(); if(raster)raster->Release();
        if(layout)layout->Release(); if(buffer)buffer->Release(); if(resource)resource->Release(); if(sampler)sampler->Release();
        if(predicate)predicate->Release(); if(vs)vs->Release(); if(ps)ps->Release(); if(gs)gs->Release(); if(hs)hs->Release(); if(ds)ds->Release();
    }
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
};
HRESULT Render(ID3D11Texture2D* backbuffer, const std::vector<HistoryEntry>& entries, const OverlayConfig& cfg, double now) {
    if (entries.size() > 10) return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC description{}; backbuffer->GetDesc(&description);
    if (!description.Width || !description.Height) return S_FALSE;
    ComPtr<ID3D11Device> device; backbuffer->GetDevice(&device);
    if (renderer.device.Get() != device.Get()) CreateRenderer(device.Get());
    ComPtr<ID3D11DeviceContext> context; device->GetImmediateContext(&context);
    ComPtr<ID3D11RenderTargetView> target;
    Check(device->CreateRenderTargetView(backbuffer,nullptr,&target));
    const float width = static_cast<float>(description.Width), height = static_cast<float>(description.Height);
    const float regionHeight = height * cfg.height;
    const float top = std::clamp(height * cfg.center - regionHeight * .5f,0.f,height-regionHeight);
    const float bottom = top + regionHeight, rowHeight = regionHeight / 10.f;
    const float side = rowHeight * .86f, gap = rowHeight * .13f;
    const float x = std::clamp(width * cfg.left,0.f,std::max(0.f,width-2.f*rowHeight));
    const double duration = std::max(static_cast<double>(cfg.scrollMs)/1000.,.001);
    const double dt = std::max(now-renderer.last,0.);
    const float factor = static_cast<float>(1.-std::exp(-5.*dt/duration));
    renderer.last = now;
    std::vector<Animation> animations; animations.reserve(entries.size());
    std::array<Vertex,120> vertices{};
    std::vector<Draw> draws; draws.reserve(20);
    UINT vertexCount = 0;
    const auto image = [&](float left, float y, float alpha, size_t index) {
        const float l=2*left/width-1, r=2*(left+side)/width-1, t=1-2*y/height, b=1-2*(y+side)/height;
        draws.push_back({vertexCount,index});
        const Vertex quad[]={{l,t,0,0,alpha},{r,t,1,0,alpha},{r,b,1,1,alpha},{l,t,0,0,alpha},{r,b,1,1,alpha},{l,b,0,1,alpha}};
        memcpy(vertices.data()+vertexCount,quad,sizeof(quad)); vertexCount+=6;
    };
    for (size_t i=0;i<entries.size();++i) {
        const auto& entry=entries[i];
        const float row=static_cast<float>(entries.size()-i-1);
        const auto found=std::find_if(renderer.animations.begin(),renderer.animations.end(),[&](const Animation& a){return a.key==entry.key;});
        Animation animation=found==renderer.animations.end()?Animation{entry.key,row-1,now}:*found;
        float alpha=1;
        if (cfg.scrollMs<=0) animation.row=row;
        else {
            if(found!=renderer.animations.end()) animation.row+=(row-animation.row)*factor;
            alpha=static_cast<float>(std::clamp((now-animation.born)/duration,0.,1.));
        }
        animations.push_back(animation);
        const float y=bottom-(animation.row+.5f)*rowHeight-.5f*side;
        image(x,y,alpha,entry.old?1u:0u);
        if(entry.badge)image(x+side+gap,y,alpha,entry.badge==1?2u:3u);
    }
    renderer.animations=std::move(animations);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(context->Map(renderer.buffer.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped));
    memcpy(mapped.pData,vertices.data(),static_cast<size_t>(vertexCount)*sizeof(Vertex));
    context->Unmap(renderer.buffer.Get(),0);
    Pipeline saved(context.Get(),device->GetFeatureLevel());
    ID3D11Buffer* buffer=renderer.buffer.Get(); UINT stride=sizeof(Vertex), offset=0;
    context->IASetInputLayout(renderer.layout.Get()); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
    context->VSSetShader(renderer.vs.Get(),nullptr,0); context->PSSetShader(renderer.ps.Get(),nullptr,0);
    context->GSSetShader(nullptr,nullptr,0); context->HSSetShader(nullptr,nullptr,0); context->DSSetShader(nullptr,nullptr,0);
    context->SetPredication(nullptr,FALSE);
    ID3D11RenderTargetView* rtv=target.Get(); context->OMSetRenderTargets(1,&rtv,nullptr);
    const FLOAT factors[4]{}; context->OMSetBlendState(renderer.blend.Get(),factors,UINT32_MAX);
    context->OMSetDepthStencilState(renderer.depth.Get(),0); context->RSSetState(renderer.raster.Get());
    const D3D11_VIEWPORT viewport{0,0,width,height,0,1}; context->RSSetViewports(1,&viewport);
    const D3D11_RECT scissor{0,static_cast<LONG>(top),static_cast<LONG>(description.Width),static_cast<LONG>(bottom)};
    context->RSSetScissorRects(1,&scissor);
    ID3D11SamplerState* sampler=renderer.sampler.Get(); context->PSSetSamplers(0,1,&sampler);
    for(const auto& draw:draws) {
        ID3D11ShaderResourceView* resource=renderer.images[draw.image].Get();
        context->PSSetShaderResources(0,1,&resource); context->Draw(6,draw.first);
    }
    if(!readyLogged) { Log("UI_READY width=%u height=%u images=4 history=10 newest=bottom",description.Width,description.Height); readyLogged=true; }
    return S_OK;
}
}
void InitializeOverlay(const std::wstring& root) { uiRoot=root+L"\\assets\\ui"; }
void ConfigureOverlay(const OverlayConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex);
    configuration=config;
    configuration.left=std::clamp(config.left,0.f,.9f);
    configuration.center=std::clamp(config.center,0.f,1.f);
    configuration.height=std::clamp(config.height,.1f,1.f);
    configuration.scrollMs=std::clamp(config.scrollMs,0.f,2000.f);
}
void AppendHistory(uint64_t ticket,uint32_t id,const std::string& name,bool old,uint32_t reason,uint32_t handle,bool targetOld) {
    if(!ticket || handle==UINT32_MAX)return;
    const int badge=reason==3?1:(reason==static_cast<uint32_t>(!targetOld)?2:0);
    std::lock_guard<std::mutex> lock(historyMutex);
    const auto position=std::lower_bound(history.begin(),history.end(),ticket,[](const HistoryEntry& entry,uint64_t key){return entry.key<key;});
    if(position!=history.end() && position->key==ticket)return;
    history.insert(position,HistoryEntry{ticket,id,name,old,badge});
    if(history.size()>10)history.erase(history.begin());
}
void DrawHistory(IDXGISwapChain* chain) noexcept {
    if(!chain)return;
    try {
        OverlayConfig cfg;
        {std::lock_guard<std::mutex> lock(configMutex);cfg=configuration;}
        if(!cfg.enabled)return;
        std::vector<HistoryEntry> entries;
        {std::lock_guard<std::mutex> lock(historyMutex);entries=history;}
        if(entries.empty())return;
        const double now=Seconds();
        std::lock_guard<std::mutex> lock(gpuMutex);
        if(now<retryAt)return;
        HRESULT result=S_OK;
        try {
            ComPtr<ID3D11Texture2D> backbuffer;
            Check(chain->GetBuffer(0,IID_PPV_ARGS(&backbuffer)));
            result=Render(backbuffer.Get(),entries,cfg,now);
        } catch(HRESULT error) {result=error;}
        catch(...) {result=E_FAIL;}
        if(FAILED(result)) {
            Log("UI_ERROR hresult=%08lx; retry_in=5s; voice selection unaffected",static_cast<unsigned long>(result));
            renderer.Reset(); retryAt=now+5.;
        }
    } catch(...) {Log("UI_ERROR snapshot failed; voice selection unaffected");}
}
}

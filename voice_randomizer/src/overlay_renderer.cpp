#include "overlay_renderer.h"
#include <d3dcompiler.h>
#include <wincodec.h>
#include <cmath>
#include <cstring>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"ole32.lib")

#define VR_TRY(call) do { HRESULT result=(call); if (FAILED(result)) return result; } while(0)
namespace vr::ui {
namespace {
struct Vertex { float x,y,u,v,alpha; };
struct ComScope {
    HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
};
HRESULT load_image(ID3D11Device* device,const std::wstring& path,ID3D11ShaderResourceView** output) {
    ComScope com;
    ComPtr<IWICImagingFactory> factory;
    VR_TRY(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmapDecoder> decoder;
    VR_TRY(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder));
    ComPtr<IWICBitmapFrameDecode> frame; VR_TRY(decoder->GetFrame(0,&frame));
    UINT width=0,height=0; VR_TRY(frame->GetSize(&width,&height));
    if (!width || !height || width>2048 || height>2048) return E_INVALIDARG;
    ComPtr<IWICFormatConverter> converter; VR_TRY(factory->CreateFormatConverter(&converter));
    VR_TRY(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    std::vector<unsigned char> pixels(width*height*4);
    VR_TRY(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(pixels.size()),pixels.data()));
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=width; desc.Height=height; desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_IMMUTABLE; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{pixels.data(),width*4,0};
    ComPtr<ID3D11Texture2D> texture; VR_TRY(device->CreateTexture2D(&desc,&data,&texture));
    return device->CreateShaderResourceView(texture.Get(),nullptr,output);
}

// Restore exactly the pipeline slots changed by this renderer, including shader
// class instances, render targets/UAVs and predication. No backbuffer references
// survive render(), so the game can ResizeBuffers without an extra resize hook.
struct Pipeline {
    ID3D11DeviceContext* c;
    ID3D11RenderTargetView* rt[8]{}; ID3D11DepthStencilView* dsv=nullptr;
    ID3D11UnorderedAccessView* uav[64]{}; UINT uav_count=8,rt_count=0;
    ComPtr<ID3D11BlendState> blend; FLOAT factors[4]{}; UINT mask=0;
    ComPtr<ID3D11DepthStencilState> depth; UINT stencil=0;
    ComPtr<ID3D11RasterizerState> raster;
    D3D11_VIEWPORT viewports[16]{}; UINT viewport_count=16;
    D3D11_RECT scissors[16]{}; UINT scissor_count=16;
    ComPtr<ID3D11InputLayout> layout; D3D11_PRIMITIVE_TOPOLOGY topology{};
    ComPtr<ID3D11Buffer> vb; UINT stride=0,offset=0;
    ComPtr<ID3D11ShaderResourceView> srv; ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Predicate> predicate; BOOL predicate_value=FALSE;
#define VR_SHADER_FIELDS(stage,type) ComPtr<type> stage; ID3D11ClassInstance* stage##_instances[256]{}; UINT stage##_count=256;
    VR_SHADER_FIELDS(VS,ID3D11VertexShader)
    VR_SHADER_FIELDS(PS,ID3D11PixelShader)
    VR_SHADER_FIELDS(GS,ID3D11GeometryShader)
    VR_SHADER_FIELDS(HS,ID3D11HullShader)
    VR_SHADER_FIELDS(DS,ID3D11DomainShader)
#undef VR_SHADER_FIELDS
    explicit Pipeline(ID3D11DeviceContext* context,UINT count):c(context),uav_count(count) {
        c->OMGetRenderTargets(8,rt,&dsv);
        for (UINT i=0;i<8;++i) if(rt[i]) rt_count=i+1;
        c->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,uav_count,uav);
        c->OMGetBlendState(&blend,factors,&mask); c->OMGetDepthStencilState(&depth,&stencil);
        c->RSGetState(&raster); c->RSGetViewports(&viewport_count,viewports); c->RSGetScissorRects(&scissor_count,scissors);
        c->IAGetInputLayout(&layout); c->IAGetPrimitiveTopology(&topology); c->IAGetVertexBuffers(0,1,&vb,&stride,&offset);
        c->PSGetShaderResources(0,1,&srv); c->PSGetSamplers(0,1,&sampler); c->GetPredication(&predicate,&predicate_value);
#define VR_CAPTURE(stage) c->stage##GetShader(&stage,stage##_instances,&stage##_count);
        VR_CAPTURE(VS) VR_CAPTURE(PS) VR_CAPTURE(GS) VR_CAPTURE(HS) VR_CAPTURE(DS)
#undef VR_CAPTURE
    }
    ~Pipeline() {
        c->OMSetRenderTargets(rt_count,rt,dsv);
        UINT counts[64]; std::fill(std::begin(counts),std::end(counts),UINT(-1));
        c->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,
                                                    rt_count,uav_count-rt_count,uav+rt_count,counts);
        c->OMSetBlendState(blend.Get(),factors,mask); c->OMSetDepthStencilState(depth.Get(),stencil);
        c->RSSetState(raster.Get()); c->RSSetViewports(viewport_count,viewports); c->RSSetScissorRects(scissor_count,scissors);
        c->IASetInputLayout(layout.Get()); c->IASetPrimitiveTopology(topology); auto buffer=vb.Get();
        c->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
        auto view=srv.Get(); auto sample=sampler.Get(); c->PSSetShaderResources(0,1,&view); c->PSSetSamplers(0,1,&sample);
        c->SetPredication(predicate.Get(),predicate_value);
#define VR_RESTORE(stage) c->stage##SetShader(stage.Get(),stage##_instances,stage##_count); for(UINT i=0;i<stage##_count;++i) if(stage##_instances[i]) stage##_instances[i]->Release();
        VR_RESTORE(VS) VR_RESTORE(PS) VR_RESTORE(GS) VR_RESTORE(HS) VR_RESTORE(DS)
#undef VR_RESTORE
        for(auto p:rt) if(p) p->Release(); if(dsv) dsv->Release();
        for(UINT i=0;i<uav_count;++i) if(uav[i]) uav[i]->Release();
    }
};
}

void Renderer::reset() {
    device_.Reset(); vs_.Reset(); ps_.Reset(); input_.Reset(); vertices_.Reset(); sampler_.Reset();
    blend_.Reset(); raster_.Reset(); depth_.Reset(); for(auto& image:images_) image.Reset();
    motion_.clear(); last_time_=0;
}
HRESULT Renderer::initialize_device(ID3D11Device* device,const std::wstring& image_dir) {
    reset();
    static const char shader[]=R"(
struct In { float2 pos:POSITION; float2 uv:TEXCOORD0; float alpha:TEXCOORD1; };
struct Out { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; float alpha:TEXCOORD1; };
Out VSMain(In x) { Out y; y.pos=float4(x.pos,0,1); y.uv=x.uv; y.alpha=x.alpha; return y; }
Texture2D tex:register(t0); SamplerState samp:register(s0);
float4 PSMain(Out x):SV_TARGET { float4 c=tex.Sample(samp,x.uv); c.a*=x.alpha; return c; }
)";
    ComPtr<ID3DBlob> vertex,pixel,error;
    VR_TRY(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"VSMain","vs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vertex,&error));
    error.Reset();
    VR_TRY(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"PSMain","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&pixel,&error));
    VR_TRY(device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&vs_));
    VR_TRY(device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&ps_));
    const D3D11_INPUT_ELEMENT_DESC elements[]{
        {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
    VR_TRY(device->CreateInputLayout(elements,3,vertex->GetBufferPointer(),vertex->GetBufferSize(),&input_));
    D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth=sizeof(Vertex)*6*20; buffer.Usage=D3D11_USAGE_DYNAMIC;
    buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER; buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    VR_TRY(device->CreateBuffer(&buffer,nullptr,&vertices_));
    D3D11_SAMPLER_DESC sampler{}; sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sampler.MaxLOD=D3D11_FLOAT32_MAX;
    VR_TRY(device->CreateSamplerState(&sampler,&sampler_));
    D3D11_BLEND_DESC blend{}; auto& target=blend.RenderTarget[0]; target.BlendEnable=TRUE;
    target.SrcBlend=D3D11_BLEND_SRC_ALPHA; target.DestBlend=D3D11_BLEND_INV_SRC_ALPHA; target.BlendOp=D3D11_BLEND_OP_ADD;
    target.SrcBlendAlpha=D3D11_BLEND_ONE; target.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA; target.BlendOpAlpha=D3D11_BLEND_OP_ADD;
    target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL; VR_TRY(device->CreateBlendState(&blend,&blend_));
    D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE;
    raster.ScissorEnable=TRUE; raster.DepthClipEnable=TRUE; VR_TRY(device->CreateRasterizerState(&raster,&raster_));
    D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable=FALSE; depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc=D3D11_COMPARISON_ALWAYS; VR_TRY(device->CreateDepthStencilState(&depth,&depth_));
    const wchar_t* names[]{L"avatar_new.png",L"avatar_old.png",L"badge_radiance.png",L"badge_wai.png"};
    for(size_t i=0;i<4;++i) VR_TRY(load_image(device,image_dir+L"/"+names[i],&images_[i]));
    device_=device;
    return S_OK;
}

HRESULT Renderer::render(ID3D11Texture2D* target,const std::wstring& image_dir,
                         const std::vector<Entry>& rows,const Config& config,double now) {
    if (!target || !config.enabled || rows.empty()) return S_FALSE;
    if (rows.size()>10) return E_INVALIDARG;
    ComPtr<ID3D11Device> device; target->GetDevice(&device);
    if (device.Get()!=device_.Get()) VR_TRY(initialize_device(device.Get(),image_dir));
    D3D11_TEXTURE2D_DESC desc{}; target->GetDesc(&desc);
    if (!desc.Width || !desc.Height) return S_FALSE;
    ComPtr<ID3D11DeviceContext> context; device->GetImmediateContext(&context);
    ComPtr<ID3D11RenderTargetView> rtv; VR_TRY(device->CreateRenderTargetView(target,nullptr,&rtv));
    const auto layout=Layout::make(static_cast<float>(desc.Width),static_cast<float>(desc.Height),config);
    const float step=static_cast<float>(std::max(0.0,now-last_time_)); last_time_=now;
    const float duration=std::max(.001f,config.scroll_ms/1000.f);
    const float smooth=1.f-std::exp(-step*5.f/duration);
    std::vector<Motion> next; std::vector<Vertex> vertices; std::vector<unsigned> textures;
    auto quad=[&](float left,float top,float alpha,unsigned texture) {
        float x0=left/desc.Width*2.f-1.f,x1=(left+layout.side)/desc.Width*2.f-1.f;
        float y0=1.f-top/desc.Height*2.f,y1=1.f-(top+layout.side)/desc.Height*2.f;
        const Vertex v[]{{x0,y0,0,0,alpha},{x1,y0,1,0,alpha},{x1,y1,1,1,alpha},
                         {x0,y0,0,0,alpha},{x1,y1,1,1,alpha},{x0,y1,0,1,alpha}};
        vertices.insert(vertices.end(),std::begin(v),std::end(v)); textures.push_back(texture);
    };
    for(size_t i=0;i<rows.size();++i) {
        const auto& row=rows[i]; const float rank=static_cast<float>(rows.size()-1-i);
        auto old=std::find_if(motion_.begin(),motion_.end(),[&](const Motion& m){return m.ticket==row.ticket;});
        Motion motion=old==motion_.end()?Motion{row.ticket,rank-1.f,now}:*old;
        if(config.scroll_ms<=0) motion.rank=rank;
        else if(old!=motion_.end()) motion.rank+=(rank-motion.rank)*smooth;
        const float alpha=config.scroll_ms<=0?1.f:std::clamp(static_cast<float>((now-motion.born)/duration),0.f,1.f);
        next.push_back(motion);
        const float y=layout.bottom-layout.pitch*(.5f+motion.rank)-layout.side*.5f;
        quad(layout.left,y,alpha,row.old?1:0);
        if(row.badge!=Badge::None) quad(layout.left+layout.side+layout.gap,y,alpha,row.badge==Badge::Radiance?2:3);
    }
    motion_=std::move(next);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    VR_TRY(context->Map(vertices_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped));
    std::memcpy(mapped.pData,vertices.data(),vertices.size()*sizeof(Vertex)); context->Unmap(vertices_.Get(),0);
    Pipeline saved(context.Get(),device->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?64:8);
    auto* c=context.Get(); c->SetPredication(nullptr,FALSE);
    auto view=rtv.Get(); c->OMSetRenderTargets(1,&view,nullptr); const FLOAT blend_factor[4]{};
    c->OMSetBlendState(blend_.Get(),blend_factor,0xffffffff); c->OMSetDepthStencilState(depth_.Get(),0);
    c->RSSetState(raster_.Get()); D3D11_VIEWPORT viewport{0,0,static_cast<FLOAT>(desc.Width),static_cast<FLOAT>(desc.Height),0,1};
    c->RSSetViewports(1,&viewport);
    D3D11_RECT clip{0,static_cast<LONG>(layout.top),static_cast<LONG>(desc.Width),static_cast<LONG>(layout.bottom)};
    c->RSSetScissorRects(1,&clip); c->IASetInputLayout(input_.Get()); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto vb=vertices_.Get(); UINT stride=sizeof(Vertex),offset=0; c->IASetVertexBuffers(0,1,&vb,&stride,&offset);
    c->VSSetShader(vs_.Get(),nullptr,0); c->PSSetShader(ps_.Get(),nullptr,0);
    c->GSSetShader(nullptr,nullptr,0); c->HSSetShader(nullptr,nullptr,0); c->DSSetShader(nullptr,nullptr,0);
    auto sampler=sampler_.Get(); c->PSSetSamplers(0,1,&sampler);
    for(UINT i=0;i<static_cast<UINT>(textures.size());++i) {
        auto texture=images_[textures[i]].Get(); c->PSSetShaderResources(0,1,&texture); c->Draw(6,i*6);
    }
    return S_OK;
}
} // namespace vr::ui
#undef VR_TRY

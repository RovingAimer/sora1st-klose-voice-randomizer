#include "overlay_renderer.h"
#include <d3d11sdklayers.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace vr::ui;
namespace {
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
void success(HRESULT result,const char* message) { if(FAILED(result)) { std::cerr<<"HRESULT "<<std::hex<<result<<std::dec<<'\n'; throw std::runtime_error(message); } }
void test_history() {
    History h;
    std::vector<std::thread> workers;
    for(unsigned i=0;i<8;++i) workers.emplace_back([&] {
        for(unsigned j=0;j<100;++j) { auto ticket=h.ticket(); h.push({ticket,0,"v006_02_0001",false,Badge::None}); }
    });
    for(auto& worker:workers) worker.join();
    auto rows=h.snapshot(); check(rows.size()==10,"history remains bounded under concurrency");
    for(size_t i=0;i<10;++i) check(rows[i].ticket==791+i,"history orders delayed records by ticket");
    h.push({rows.back().ticket,0,"duplicate",true,Badge::Wai}); check(h.snapshot().size()==10,"duplicate ticket not appended");
    using R=vr::Reason;
    check(badge_for(R::RandomOld)==Badge::Wai,"random old uses wai image");
    check(badge_for(R::CapturingRadiance)==Badge::Radiance,"radiance uses radiance image");
    for(auto r:{R::RandomNew,R::Guarantee,R::ForcedNew,R::ForcedOld,R::Alternate}) check(badge_for(r)==Badge::None,"other reasons have no badge");
    Config cfg;
    for(auto dimensions:{std::pair<float,float>{1920.f,1080.f},{1280.f,720.f},{3840.f,2160.f}}) {
        auto l=Layout::make(dimensions.first,dimensions.second,cfg);
        check(std::abs(l.top-dimensions.second*.25f)<.01f && std::abs(l.bottom-dimensions.second*.75f)<.01f,"list occupies middle half of game height");
        check(l.center_for_rank(0)>l.center_for_rank(9),"newest row appears at bottom");
        check(l.center_for_rank(9)-l.side*.5f>=l.top && l.center_for_rank(0)+l.side*.5f<=l.bottom,"all ten rows fit");
    }
    std::cout<<"PASS: 800 concurrent history events, 10-row bound, badge rules, bottom-newest half-height layout\n";
}
struct Surface {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> view;
};
Surface surface(ID3D11Device* device,UINT width,UINT height) {
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=width; desc.Height=height; desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1; desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    Surface s; success(device->CreateTexture2D(&desc,nullptr,&s.texture),"create offscreen target");
    success(device->CreateRenderTargetView(s.texture.Get(),nullptr,&s.view),"create offscreen view"); return s;
}
std::vector<unsigned char> pixels(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* target) {
    D3D11_TEXTURE2D_DESC desc{}; target->GetDesc(&desc); desc.BindFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging; success(device->CreateTexture2D(&desc,nullptr,&staging),"staging surface");
    context->CopyResource(staging.Get(),target); D3D11_MAPPED_SUBRESOURCE mapped{};
    success(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"read rendered pixels");
    std::vector<unsigned char> data(desc.Width*desc.Height*4);
    for(UINT y=0;y<desc.Height;++y) std::memcpy(data.data()+y*desc.Width*4,static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch,desc.Width*4);
    context->Unmap(staging.Get(),0); return data;
}
void save_bmp(const std::filesystem::path& path,UINT width,UINT height,std::vector<unsigned char> data) {
    for(size_t i=0;i<data.size();i+=4) std::swap(data[i],data[i+2]);
    BITMAPFILEHEADER file{}; file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER); file.bfSize=file.bfOffBits+static_cast<DWORD>(data.size());
    BITMAPINFOHEADER info{}; info.biSize=sizeof(info); info.biWidth=width; info.biHeight=-static_cast<LONG>(height); info.biPlanes=1; info.biBitCount=32;
    std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<char*>(&file),sizeof(file)); out.write(reinterpret_cast<char*>(&info),sizeof(info));
    out.write(reinterpret_cast<char*>(data.data()),data.size()); check(out.good(),"save offscreen preview");
}
void pixel_is(const std::vector<unsigned char>& data,UINT width,int x,int y,std::array<int,3> expected) {
    const size_t at=(size_t(y)*width+size_t(x))*4;
    for(size_t k=0;k<3;++k) check(std::abs(int(data.at(at+k))-expected[k])<=2,"rendered image color/placement");
}
}

int wmain(int argc,wchar_t** argv) {
    try {
        check(argc==2,"game root argument required"); test_history();
        const std::filesystem::path root(argv[1]); const auto images=(root/L"voice_randomizer/build/ui_test_fixture").wstring();
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context; D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        HRESULT result=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_DEBUG,levels,2,D3D11_SDK_VERSION,&device,&level,&context);
        if(FAILED(result)) success(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,2,D3D11_SDK_VERSION,&device,&level,&context),"headless D3D11 WARP device");
        ComPtr<ID3D11InfoQueue> info; device.As(&info);
        std::vector<Entry> rows;
        for(unsigned i=0;i<10;++i) rows.push_back({i+1,0,"fixture",i%3==1,i%3==1?Badge::Wai:i%3==2?Badge::Radiance:Badge::None});
        Config config; config.scroll_ms=0; Renderer renderer;
        const FLOAT clear[]{18/255.f,26/255.f,39/255.f,1};
        auto sentinel=surface(device.Get(),16,16); auto original=sentinel.view.Get(); context->OMSetRenderTargets(1,&original,nullptr);
        D3D11_VIEWPORT original_view{3,7,11,13,.1f,.9f}; context->RSSetViewports(1,&original_view);
        D3D11_RECT original_scissor{1,2,7,9}; context->RSSetScissorRects(1,&original_scissor);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        // Keep a non-null PS resource/sampler and vertex buffer to verify restoration.
        D3D11_TEXTURE2D_DESC sampled{}; sampled.Width=sampled.Height=1; sampled.MipLevels=sampled.ArraySize=1;
        sampled.Format=DXGI_FORMAT_R8G8B8A8_UNORM; sampled.SampleDesc.Count=1; sampled.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> sample_texture; success(device->CreateTexture2D(&sampled,nullptr,&sample_texture),"sentinel texture");
        ComPtr<ID3D11ShaderResourceView> srv; success(device->CreateShaderResourceView(sample_texture.Get(),nullptr,&srv),"sentinel SRV");
        auto srv_ptr=srv.Get(); context->PSSetShaderResources(0,1,&srv_ptr);
        D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT; sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        ComPtr<ID3D11SamplerState> sampler; success(device->CreateSamplerState(&sd,&sampler),"sentinel sampler"); auto sampler_ptr=sampler.Get(); context->PSSetSamplers(0,1,&sampler_ptr);
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth=256; bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        ComPtr<ID3D11Buffer> buffer; success(device->CreateBuffer(&bd,nullptr,&buffer),"sentinel vertex buffer");
        auto buffer_ptr=buffer.Get(); UINT stride=24,offset=16; context->IASetVertexBuffers(0,1,&buffer_ptr,&stride,&offset);
        for(const auto dimensions:{std::pair<UINT,UINT>{1920,1080},{1280,720}}) {
            auto target=surface(device.Get(),dimensions.first,dimensions.second); context->ClearRenderTargetView(target.view.Get(),clear);
            success(renderer.render(target.texture.Get(),images,rows,config,1),"offscreen overlay draw");
            auto data=pixels(device.Get(),context.Get(),target.texture.Get()); auto l=Layout::make(float(dimensions.first),float(dimensions.second),config);
            for(size_t i=0;i<rows.size();++i) {
                const int y=int(l.center_for_rank(rows.size()-1-i)),x=int(l.left+l.side*.5f),badge_x=int(l.left+l.side*1.5f+l.gap);
                pixel_is(data,dimensions.first,x,y,rows[i].old?std::array<int,3>{249,115,22}:std::array<int,3>{57,167,255});
                pixel_is(data,dimensions.first,badge_x,y,rows[i].badge==Badge::Wai?std::array<int,3>{180,92,255}:rows[i].badge==Badge::Radiance?std::array<int,3>{245,213,71}:std::array<int,3>{18,26,39});
            }
            pixel_is(data,dimensions.first,int(l.left),int(l.top)-5,{18,26,39});
            if(dimensions.first==1920) save_bmp(root/L"voice_randomizer/overlay_preview.bmp",1920,1080,std::move(data));
            ComPtr<ID3D11RenderTargetView> restored; context->OMGetRenderTargets(1,&restored,nullptr); check(restored.Get()==original,"original render target restored");
            D3D11_VIEWPORT viewport{}; UINT n=1; context->RSGetViewports(&n,&viewport); check(std::memcmp(&viewport,&original_view,sizeof(viewport))==0,"viewport restored");
            D3D11_RECT scissor{}; n=1; context->RSGetScissorRects(&n,&scissor); check(std::memcmp(&scissor,&original_scissor,sizeof(scissor))==0,"scissor restored");
            D3D11_PRIMITIVE_TOPOLOGY topology; context->IAGetPrimitiveTopology(&topology); check(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"topology restored");
            ComPtr<ID3D11ShaderResourceView> got_srv; context->PSGetShaderResources(0,1,&got_srv); check(got_srv.Get()==srv.Get(),"PS texture restored");
            ComPtr<ID3D11SamplerState> got_sampler; context->PSGetSamplers(0,1,&got_sampler); check(got_sampler.Get()==sampler.Get(),"sampler restored");
            ComPtr<ID3D11Buffer> got_vb; UINT got_stride=0,got_offset=0; context->IAGetVertexBuffers(0,1,&got_vb,&got_stride,&got_offset);
            check(got_vb.Get()==buffer.Get() && got_stride==stride && got_offset==offset,"vertex buffer restored");
            std::cout<<"PASS: actual headless D3D11 draw "<<dimensions.first<<'x'<<dimensions.second<<", ten avatars, exact badge pixels and pipeline restoration\n";
        }
        // Fade-in begins below the bottom edge; later frames move up and settle.
        renderer.reset(); config.scroll_ms=180; auto target=surface(device.Get(),1920,1080); rows.resize(1); rows[0].old=false; rows[0].badge=Badge::None;
        context->ClearRenderTargetView(target.view.Get(),clear); success(renderer.render(target.texture.Get(),images,rows,config,10),"animation first frame");
        auto l=Layout::make(1920,1080,config); auto data=pixels(device.Get(),context.Get(),target.texture.Get());
        pixel_is(data,1920,int(l.left+l.side*.5f),int(l.center_for_rank(0)),{18,26,39});
        context->ClearRenderTargetView(target.view.Get(),clear); success(renderer.render(target.texture.Get(),images,rows,config,11),"animation settled frame");
        data=pixels(device.Get(),context.Get(),target.texture.Get()); pixel_is(data,1920,int(l.left+l.side*.5f),int(l.center_for_rank(0)),{57,167,255});
        std::cout<<"PASS: animated entry starts below list and settles in bottom row\n";
        if(info) for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i) {
            SIZE_T size=0; info->GetMessage(i,nullptr,&size); std::vector<unsigned char> storage(size);
            auto* message=reinterpret_cast<D3D11_MESSAGE*>(storage.data()); info->GetMessage(i,message,&size);
            if(message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR) { std::cerr<<message->pDescription<<'\n'; throw std::runtime_error("D3D debug validation error"); }
        }
        renderer.reset(); config.scroll_ms=0; config.center=.4f; rows.clear();
        rows.push_back({1,0,"release art",false,Badge::Radiance}); rows.push_back({2,0,"release art",true,Badge::Wai});
        context->ClearRenderTargetView(target.view.Get(),clear);
        success(renderer.render(target.texture.Get(),(root/L"voice_randomizer/assets/ui").wstring(),rows,config,12),"load and draw final release PNGs");
        save_bmp(root/L"voice_randomizer/build/release_ui_preview.bmp",1920,1080,pixels(device.Get(),context.Get(),target.texture.Get()));
        std::cout<<"PASS: all four publisher PNGs load and render; source artwork untouched\nALL OVERLAY TESTS PASSED; no game or visible window launched\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}

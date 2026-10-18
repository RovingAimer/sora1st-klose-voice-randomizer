#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <wrl/client.h>
#include "overlay.h"

namespace vr::ui {
using Microsoft::WRL::ComPtr;
class Renderer {
    struct Motion { uint64_t ticket; float rank; double born; };
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11InputLayout> input_;
    ComPtr<ID3D11Buffer> vertices_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11BlendState> blend_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11DepthStencilState> depth_;
    std::array<ComPtr<ID3D11ShaderResourceView>,4> images_;
    std::vector<Motion> motion_;
    double last_time_=0;
    HRESULT initialize_device(ID3D11Device*,const std::wstring&);
public:
    HRESULT render(ID3D11Texture2D* target,const std::wstring& image_dir,
                   const std::vector<Entry>& rows,const Config& config,double now);
    void reset();
};
} // namespace vr::ui

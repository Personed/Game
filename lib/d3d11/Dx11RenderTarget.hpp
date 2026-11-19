#pragma once
#ifndef DAYZ_D3D11_RENDER_TARGET_HPP
#define DAYZ_D3D11_RENDER_TARGET_HPP

#include <Es/Common/win.h>
#include <d3d11.h>

// Owns a single-sample color target and an optional depth/stencil target.
// Color can be sampled after it is removed from the output-merger stage.
class Dx11RenderTarget
{
public:
  Dx11RenderTarget();
  ~Dx11RenderTarget();
  Dx11RenderTarget(const Dx11RenderTarget &) = delete;
  Dx11RenderTarget &operator=(const Dx11RenderTarget &) = delete;

  HRESULT Create(ID3D11Device *device, UINT width, UINT height,
    DXGI_FORMAT colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT depthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT);
  HRESULT Resize(UINT width, UINT height);
  HRESULT Bind(ID3D11DeviceContext *context);
  HRESULT BindShaderResource(ID3D11DeviceContext *context, UINT pixelShaderSlot);
  HRESULT BindShaderResourceSRGB(ID3D11DeviceContext *context, UINT pixelShaderSlot);
  HRESULT Unbind(ID3D11DeviceContext *context);
  // Only the color target clears by default; request depth/stencil clears explicitly.
  HRESULT Clear(ID3D11DeviceContext *context, const float color[4],
    bool clearColor, float depth = 1.0f, UINT8 stencil = 0,
    bool clearDepth = false, bool clearStencil = false);
  void Reset();

  ID3D11Texture2D *ColorTexture() const { return _colorTexture; }
  ID3D11RenderTargetView *RenderTargetView() const { return _renderTargetView; }
  ID3D11ShaderResourceView *ShaderResourceView() const { return _shaderResourceView; }
  ID3D11ShaderResourceView *ShaderResourceViewSRGB() const { return _shaderResourceViewSRGB; }
  ID3D11Texture2D *DepthTexture() const { return _depthTexture; }
  ID3D11DepthStencilView *DepthStencilView() const { return _depthStencilView; }
  UINT Width() const { return _width; }
  UINT Height() const { return _height; }
  DXGI_FORMAT ColorFormat() const { return _colorFormat; }
  DXGI_FORMAT DepthFormat() const { return _depthFormat; }

private:
  HRESULT CreateResources(ID3D11Device *device, UINT width, UINT height,
    DXGI_FORMAT colorFormat, DXGI_FORMAT depthFormat);

  ID3D11Device *_device;
  ID3D11Texture2D *_colorTexture;
  ID3D11RenderTargetView *_renderTargetView;
  ID3D11ShaderResourceView *_shaderResourceView;
  ID3D11ShaderResourceView *_shaderResourceViewSRGB;
  ID3D11Texture2D *_depthTexture;
  ID3D11DepthStencilView *_depthStencilView;
  UINT _width;
  UINT _height;
  DXGI_FORMAT _colorFormat;
  DXGI_FORMAT _depthFormat;
};

#endif

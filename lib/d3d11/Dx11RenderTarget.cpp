#include "Dx11RenderTarget.hpp"

#include <string.h>

namespace
{
bool IsValidDimension(UINT value)
{
  return value != 0 && value <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
}

bool HasStencil(DXGI_FORMAT format)
{
  return format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
    format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
}

bool IsSameResource(ID3D11View *view, ID3D11Resource *resource)
{
  if (!view || !resource) return false;
  ID3D11Resource *viewResource = NULL;
  view->GetResource(&viewResource);
  const bool same = viewResource == resource;
  if (viewResource) viewResource->Release();
  return same;
}

void ReleaseViews(ID3D11ShaderResourceView **views, UINT count)
{
  for (UINT i = 0; i < count; ++i)
    if (views[i]) views[i]->Release();
}

void UnbindShaderResource(ID3D11DeviceContext *context,
  ID3D11ShaderResourceView **views, ID3D11Resource *resource,
  UINT stage)
{
  if (stage == 0) context->VSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, views);
  else if (stage == 1) context->PSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, views);
  else if (stage == 2) context->GSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, views);
  else if (stage == 3) context->HSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, views);
  else if (stage == 4) context->DSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, views);
  else context->CSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, views);

  for (UINT slot = 0; slot < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++slot)
  {
    if (IsSameResource(views[slot], resource))
    {
      ID3D11ShaderResourceView *nullView = NULL;
      if (stage == 0) context->VSSetShaderResources(slot, 1, &nullView);
      else if (stage == 1) context->PSSetShaderResources(slot, 1, &nullView);
      else if (stage == 2) context->GSSetShaderResources(slot, 1, &nullView);
      else if (stage == 3) context->HSSetShaderResources(slot, 1, &nullView);
      else if (stage == 4) context->DSSetShaderResources(slot, 1, &nullView);
      else context->CSSetShaderResources(slot, 1, &nullView);
    }
  }
  ReleaseViews(views, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
}

void RemoveOutputBinding(ID3D11DeviceContext *context,
  ID3D11Resource *colorResource, ID3D11Resource *depthResource)
{
  ID3D11RenderTargetView *current[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11DepthStencilView *currentDepth = NULL;
  context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
    current, &currentDepth);

  ID3D11RenderTargetView *remaining[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  UINT viewCount = 0;
  bool changed = false;
  for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
  {
    if (IsSameResource(current[i], colorResource) || IsSameResource(current[i], depthResource))
      changed = true;
    else if (current[i])
    {
      remaining[i] = current[i];
      viewCount = i + 1;
    }
  }

  ID3D11DepthStencilView *remainingDepth = currentDepth;
  if (currentDepth && (IsSameResource(currentDepth, colorResource) ||
      IsSameResource(currentDepth, depthResource)))
  {
    remainingDepth = NULL;
    changed = true;
  }

  if (changed)
    context->OMSetRenderTargets(viewCount, remaining, remainingDepth);
  for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    if (current[i]) current[i]->Release();
  if (currentDepth) currentDepth->Release();
}

void UnbindShaderResourceEverywhere(ID3D11DeviceContext *context,
  ID3D11ShaderResourceView *view, ID3D11Resource *resource)
{
  if (!context || !view || !resource) return;
  ID3D11ShaderResourceView *views[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
  for (UINT stage = 0; stage < 6; ++stage)
  {
    memset(views, 0, sizeof(views));
    UnbindShaderResource(context, views, resource, stage);
  }
}
}

Dx11RenderTarget::Dx11RenderTarget()
  : _device(NULL), _colorTexture(NULL), _renderTargetView(NULL),
    _shaderResourceView(NULL), _shaderResourceViewSRGB(NULL), _depthTexture(NULL), _depthStencilView(NULL),
    _width(0), _height(0), _colorFormat(DXGI_FORMAT_UNKNOWN),
    _depthFormat(DXGI_FORMAT_UNKNOWN)
{
}

Dx11RenderTarget::~Dx11RenderTarget()
{
  Reset();
}

HRESULT Dx11RenderTarget::Create(ID3D11Device *device, UINT width, UINT height,
  DXGI_FORMAT colorFormat, DXGI_FORMAT depthFormat)
{
  if (!device || !IsValidDimension(width) || !IsValidDimension(height) ||
      colorFormat == DXGI_FORMAT_UNKNOWN)
    return E_INVALIDARG;
  return CreateResources(device, width, height, colorFormat, depthFormat);
}

HRESULT Dx11RenderTarget::CreateResources(ID3D11Device *device, UINT width,
  UINT height, DXGI_FORMAT colorFormat, DXGI_FORMAT depthFormat)
{
  UINT support = 0;
  HRESULT hr = device->CheckFormatSupport(colorFormat, &support);
  if (FAILED(hr)) return hr;
  if ((support & D3D11_FORMAT_SUPPORT_RENDER_TARGET) == 0 ||
      (support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) == 0)
    return DXGI_ERROR_UNSUPPORTED;

  if (depthFormat != DXGI_FORMAT_UNKNOWN)
  {
    support = 0;
    hr = device->CheckFormatSupport(depthFormat, &support);
    if (FAILED(hr)) return hr;
    if ((support & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL) == 0)
      return DXGI_ERROR_UNSUPPORTED;
  }

  DXGI_FORMAT textureFormat = colorFormat;
  DXGI_FORMAT rtvFormat = colorFormat;
  DXGI_FORMAT srvFormat = colorFormat;
  DXGI_FORMAT srgbSrvFormat = DXGI_FORMAT_UNKNOWN;

  if (colorFormat == DXGI_FORMAT_R8G8B8A8_UNORM)
  {
    textureFormat = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    srgbSrvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  }
  else if (colorFormat == DXGI_FORMAT_B8G8R8A8_UNORM)
  {
    textureFormat = DXGI_FORMAT_B8G8R8A8_TYPELESS;
    rtvFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    srvFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    srgbSrvFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
  }

  D3D11_TEXTURE2D_DESC colorDesc = {};
  colorDesc.Width = width;
  colorDesc.Height = height;
  colorDesc.MipLevels = 1;
  colorDesc.ArraySize = 1;
  colorDesc.Format = textureFormat;
  colorDesc.SampleDesc.Count = 1;
  colorDesc.Usage = D3D11_USAGE_DEFAULT;
  colorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

  ID3D11Texture2D *colorTexture = NULL;
  ID3D11RenderTargetView *renderTargetView = NULL;
  ID3D11ShaderResourceView *shaderResourceView = NULL;
  ID3D11ShaderResourceView *shaderResourceViewSRGB = NULL;
  ID3D11Texture2D *depthTexture = NULL;
  ID3D11DepthStencilView *depthStencilView = NULL;

  hr = device->CreateTexture2D(&colorDesc, NULL, &colorTexture);
  if (SUCCEEDED(hr))
  {
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = rtvFormat;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    hr = device->CreateRenderTargetView(colorTexture, &rtvDesc, &renderTargetView);
  }
  if (SUCCEEDED(hr))
  {
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = srvFormat;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    hr = device->CreateShaderResourceView(colorTexture, &srvDesc, &shaderResourceView);
  }
  if (SUCCEEDED(hr) && srgbSrvFormat != DXGI_FORMAT_UNKNOWN)
  {
    D3D11_SHADER_RESOURCE_VIEW_DESC srgbDesc = {};
    srgbDesc.Format = srgbSrvFormat;
    srgbDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srgbDesc.Texture2D.MipLevels = 1;
    hr = device->CreateShaderResourceView(colorTexture, &srgbDesc, &shaderResourceViewSRGB);
  }

  if (SUCCEEDED(hr) && depthFormat != DXGI_FORMAT_UNKNOWN)
  {
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = depthFormat;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr = device->CreateTexture2D(&depthDesc, NULL, &depthTexture);
    if (SUCCEEDED(hr))
      hr = device->CreateDepthStencilView(depthTexture, NULL, &depthStencilView);
  }

  if (FAILED(hr))
  {
    if (shaderResourceViewSRGB) shaderResourceViewSRGB->Release();
    if (depthStencilView) depthStencilView->Release();
    if (depthTexture) depthTexture->Release();
    if (shaderResourceView) shaderResourceView->Release();
    if (renderTargetView) renderTargetView->Release();
    if (colorTexture) colorTexture->Release();
    return hr;
  }

  Reset();
  _device = device;
  _device->AddRef();
  _colorTexture = colorTexture;
  _renderTargetView = renderTargetView;
  _shaderResourceView = shaderResourceView;
  _shaderResourceViewSRGB = shaderResourceViewSRGB;
  _depthTexture = depthTexture;
  _depthStencilView = depthStencilView;
  _width = width;
  _height = height;
  _colorFormat = colorFormat;
  _depthFormat = depthFormat;
  return S_OK;
}

HRESULT Dx11RenderTarget::Resize(UINT width, UINT height)
{
  if (!_device) return E_FAIL;
  if (!IsValidDimension(width) || !IsValidDimension(height)) return E_INVALIDARG;
  if (width == _width && height == _height) return S_OK;
  return CreateResources(_device, width, height, _colorFormat, _depthFormat);
}

HRESULT Dx11RenderTarget::Bind(ID3D11DeviceContext *context)
{
  if (!context || !_renderTargetView) return E_INVALIDARG;
  UnbindShaderResourceEverywhere(context, _shaderResourceView, _colorTexture);

  context->OMSetRenderTargets(1, &_renderTargetView, _depthStencilView);
  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(_width);
  viewport.Height = static_cast<float>(_height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  context->RSSetViewports(1, &viewport);
  return S_OK;
}

HRESULT Dx11RenderTarget::BindShaderResource(ID3D11DeviceContext *context,
  UINT pixelShaderSlot)
{
  if (!context || !_shaderResourceView ||
      pixelShaderSlot >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
    return E_INVALIDARG;
  ID3D11Resource *colorResource = NULL;
  _shaderResourceView->GetResource(&colorResource);
  if (!colorResource) return E_FAIL;
  RemoveOutputBinding(context, colorResource, NULL);
  colorResource->Release();
  context->PSSetShaderResources(pixelShaderSlot, 1, &_shaderResourceView);
  return S_OK;
}

HRESULT Dx11RenderTarget::BindShaderResourceSRGB(ID3D11DeviceContext *context,
  UINT pixelShaderSlot)
{
  if (!context || !_shaderResourceViewSRGB ||
      pixelShaderSlot >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
    return E_INVALIDARG;
  ID3D11Resource *colorResource = NULL;
  _shaderResourceViewSRGB->GetResource(&colorResource);
  if (!colorResource) return E_FAIL;
  RemoveOutputBinding(context, colorResource, NULL);
  colorResource->Release();
  context->PSSetShaderResources(pixelShaderSlot, 1, &_shaderResourceViewSRGB);
  return S_OK;
}

HRESULT Dx11RenderTarget::Unbind(ID3D11DeviceContext *context)
{
  if (!context) return E_INVALIDARG;
  RemoveOutputBinding(context, _colorTexture, _depthTexture);
  UnbindShaderResourceEverywhere(context, _shaderResourceView, _colorTexture);
  UnbindShaderResourceEverywhere(context, _shaderResourceViewSRGB, _colorTexture);
  return S_OK;
}

HRESULT Dx11RenderTarget::Clear(ID3D11DeviceContext *context,
  const float color[4], bool clearColor, float depth, UINT8 stencil,
  bool clearDepth, bool clearStencil)
{
  if (!context || (clearColor && (!color || !_renderTargetView)) ||
      ((clearDepth || clearStencil) && !_depthStencilView) ||
      (clearStencil && !HasStencil(_depthFormat)) || depth < 0.0f || depth > 1.0f)
    return E_INVALIDARG;

  if (clearColor)
    context->ClearRenderTargetView(_renderTargetView, color);
  UINT clearFlags = 0;
  if (clearDepth) clearFlags |= D3D11_CLEAR_DEPTH;
  if (clearStencil) clearFlags |= D3D11_CLEAR_STENCIL;
  if (clearFlags)
    context->ClearDepthStencilView(_depthStencilView, clearFlags, depth, stencil);
  return S_OK;
}

void Dx11RenderTarget::Reset()
{
  if (_depthStencilView) { _depthStencilView->Release(); _depthStencilView = NULL; }
  if (_depthTexture) { _depthTexture->Release(); _depthTexture = NULL; }
  if (_shaderResourceViewSRGB) { _shaderResourceViewSRGB->Release(); _shaderResourceViewSRGB = NULL; }
  if (_shaderResourceView) { _shaderResourceView->Release(); _shaderResourceView = NULL; }
  if (_renderTargetView) { _renderTargetView->Release(); _renderTargetView = NULL; }
  if (_colorTexture) { _colorTexture->Release(); _colorTexture = NULL; }
  if (_device) { _device->Release(); _device = NULL; }
  _width = _height = 0;
  _colorFormat = _depthFormat = DXGI_FORMAT_UNKNOWN;
}

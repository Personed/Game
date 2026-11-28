#include "../wpch.hpp"

#if _ARMA3_SIMUL_WEATHER && _ENABLE_DX11

#include "SimulCommonDX11.hpp"
#include "../d3d11/engdd11.hpp"

namespace DX11
{
  //========================================================================
  // TextureSysGPUDX11::LockedData implementation
  //========================================================================
  TextureSysGPUDX11::LockedData::LockedData(Ref<TextureSysGPUDX11> texture)
    :_texture(texture)
  {
    if (_texture)
    {
      _texture->EnterCriticalSection();
      _texture->Lock();
      _sysMemTex = _texture->GetCurrentSysMemTex();
      if (_sysMemTex)
        _data = _sysMemTex->GetLockedData();

    }
  };

  TextureSysGPUDX11::LockedData::~LockedData()
  {
    if (_texture)
      _texture->LeaveCriticalSection();
  };

  bool TextureSysGPUDX11::LockedData::CheckSize()
  {
    if (_sysMemTex)
      return _sysMemTex->CheckSize();
    else
      return false;
  }

  bool TextureSysGPUDX11::LockedData::IsPitchCorrespondingSize(int texelSizeInBytes)
  {
    if (_sysMemTex)
      return _sysMemTex->IsPitchCorrespondingSize(texelSizeInBytes);
    else
      return false;
  }

  int TextureSysGPUDX11::LockedData::GetMappedTextureOffset(int texelIndex, int texelSizeInBytes) const
  {
    if (_sysMemTex)
      return _sysMemTex->GetMappedTextureOffset(texelIndex, texelSizeInBytes);
    else
      return 0;
  }

  //========================================================================
  // TextureSysGPUDX11::SysMemTex implementation
  //========================================================================

  TextureSysGPUDX11::SysMemTex::SysMemTex(int width, int height, int depth)
    :_locked(false), _width(width), _height(height), _depth(depth)
  {
    ResetLocked();
  }

  TextureSysGPUDX11::SysMemTex::~SysMemTex()
  {
    Unlock();
  }

  void TextureSysGPUDX11::SysMemTex::Lock()
  {
    if (_locked)
      return; //already locked

    Assert(GEngineDD11);
    if (!GEngineDD11)
      return;
    Assert(_texture);
    if (!_texture)
      return;

    Assert(!GEngineDD11->GetD3DDeviceRec().IsNull())
      if (GEngineDD11->GetD3DDeviceRec().IsNull())
        return;

    // we cannot use D3D11_MAP_WRITE_DISCARD, because SW textures are updated incrementally (we cannot discard already created texture)
    HRESULT hr = GEngineDD11->GetD3DDeviceRec()->Map(_texture.GetRef(), 0, D3D11_MAP_WRITE, 0, &_mappedSubresource );
    if(FAILED(hr))
    {
      GEngineDD11->DDError11("SimulWeather - volume texture - Map failed: ", hr);
      return;
    }    

    _locked = true;
  }

  void TextureSysGPUDX11::SysMemTex::Unlock()
  {
    if (!_locked)
      return; //not locked

    // reset locked status
    ResetLocked();

    Assert(GEngineDD11);
    if (!GEngineDD11)
      return;
    Assert(_texture);
    if (!_texture)
      return;

    Assert(!GEngineDD11->GetD3DDeviceRec().IsNull())
      if (GEngineDD11->GetD3DDeviceRec().IsNull())
        return;

    GEngineDD11->GetD3DDeviceRec()->Unmap(_texture.GetRef(), 0);
  }

  void TextureSysGPUDX11::SysMemTex::ResetLocked()
  {
    _locked = false;
    _mappedSubresource.pData = 0;
    _mappedSubresource.RowPitch = 0;
    _mappedSubresource.DepthPitch = 0;
  }

  int TextureSysGPUDX11::SysMemTex::GetMappedTextureOffset(int texelIndex, int texelSizeInBytes) const
  {
    Assert(_locked);
    Assert(_width > 0 && _height > 0 && _depth > 0);

    int widthHeight = _width * _height;
    int depthSlice = texelIndex / widthHeight;
    int indexInDepthSlice = texelIndex % widthHeight;
    int x = indexInDepthSlice % _width;
    int y = indexInDepthSlice / _width;

    int offset = (
      depthSlice * _mappedSubresource.DepthPitch + 
      y * _mappedSubresource.RowPitch + 
      x * texelSizeInBytes
      );

    Assert(offset < _depth * _mappedSubresource.DepthPitch);

    return offset;
  }

  /// returns if pitch of the mapped texture corresponds to its size
  bool TextureSysGPUDX11::SysMemTex::IsPitchCorrespondingSize(int texelSizeInBytes)
  {
    return _mappedSubresource.RowPitch == _width * texelSizeInBytes
      && _mappedSubresource.DepthPitch == _height * _mappedSubresource.RowPitch;
  }

  //========================================================================
  // TextureSysGPUDX11 implementation
  //========================================================================

  TextureSysGPUDX11::TextureSysGPUDX11()
    :_needsUpdate(true), _currentSysMemTex(0)
  {
  }

  TextureSysGPUDX11::~TextureSysGPUDX11()
  {
  }

  void TextureSysGPUDX11::Lock()
  {
    _needsUpdate = true;
    Assert(_currentSysMemTex < NumSysMemTextures);
    if (_currentSysMemTex >= NumSysMemTextures)
      return;
    Assert(_sysMemTex[_currentSysMemTex]);
    if (!_sysMemTex[_currentSysMemTex])
      return;

    _sysMemTex[_currentSysMemTex]->Lock();
  }

  void TextureSysGPUDX11::Unlock()
  {
    Assert(_currentSysMemTex < NumSysMemTextures);
    if (_currentSysMemTex >= NumSysMemTextures)
      return;
    Assert(_sysMemTex[_currentSysMemTex]);
    if (!_sysMemTex[_currentSysMemTex])
      return;
    _sysMemTex[_currentSysMemTex]->Unlock();
  }

  void TextureSysGPUDX11::UpdateGPUTex(bool forceUpdate)
  {
    PROFILE_SCOPE_EX(updGPU, SW);

    if (!forceUpdate && !_needsUpdate)
      return; //no need to update

    _needsUpdate = false;

    Assert(GEngineDD11);
    if (!GEngineDD11)
      return;

    GEngineDD11->UpdateSimulWeatherTexture(this, _currentSysMemTex);
    // advance current texture
    _currentSysMemTex = (_currentSysMemTex + 1) % NumSysMemTextures;
  }

  void TextureSysGPUDX11::UpdateGPUTexInternal(unsigned sysMemTextureIndex)
  {
    // we need to protect this with critical section, so that our sys mem texture can not be updated during this function
    ScopeLockSection lock(_criticalSection);

    Assert(GEngineDD11);
    if (!GEngineDD11)
      return;
    Assert(sysMemTextureIndex < NumSysMemTextures);
    if (sysMemTextureIndex >= NumSysMemTextures)
      return;
    Assert(_sysMemTex[sysMemTextureIndex]);
    if (!_sysMemTex[sysMemTextureIndex])
      return;
    Assert(_sysMemTex[sysMemTextureIndex]->GetTexture());
    if (!_sysMemTex[sysMemTextureIndex]->GetTexture())
      return;
    Assert(_surface._texture3D);
    if (!_surface._texture3D)
      return;

    // unlock first
    _sysMemTex[sysMemTextureIndex]->Unlock();

    Assert(!GEngineDD11->GetD3DDeviceRec().IsNull());
    if (GEngineDD11->GetD3DDeviceRec().IsNull())
      return;
    GEngineDD11->GetD3DDeviceRec()->CopyResource(_surface._texture3D.GetRef(), _sysMemTex[sysMemTextureIndex]->GetTexture().GetRef());
  }

  SurfaceDataD3D11& TextureSysGPUDX11::GetSurface()
  {
    UpdateGPUTex(); //we need to update GPU tex when user needs it
    return _surface;
  }

  HRESULT TextureSysGPUDX11::CreateTextures(int width, int height, int depth, DXGI_FORMAT format)
  {
    ScopeLockSection lock(_criticalSection);

    // create gpu texture
    D3D11_TEXTURE3D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.Depth = depth;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    Assert(GEngineDD11);
    if (!GEngineDD11)
      return E_FAIL;
    Assert(!GEngineDD11->GetD3DDeviceRec().IsNull());
    if (GEngineDD11->GetD3DDeviceRec().IsNull())
      return E_FAIL;

    HRESULT hr = GEngineDD11->CreateTextureSimple3D(desc, _surface);
    if (FAILED(hr))
    {
      GEngineDD11->DDError11("Failed to create volume texture: ", hr);
      return hr;
    }

    // create sys mem texture
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.BindFlags = 0;

    for (unsigned i=0; i<NumSysMemTextures; ++i)
    {
      _sysMemTex[i] = new SysMemTex(width, height, depth);

      hr = GEngineDD11->GetD3DDeviceRec()->CreateTexture3D(&desc, NULL, _sysMemTex[i]->GetTexture().Init());
      if (FAILED(hr))
      {
        GEngineDD11->DDError11("Failed to create 3d texture: ", hr);
        return hr;
      }
    }
    return hr;
  }


}//namespace DX11

#endif //#if _ARMA3_SIMUL_WEATHER && _ENABLE_DX11

#ifdef _MSC_VER
#pragma once
#endif

#ifndef _ARMA3_SIMULCOMMONDX11_HPP
#define _ARMA3_SIMULCOMMONDX11_HPP

#if _ARMA3_SIMUL_WEATHER && _ENABLE_DX11

#include "../d3d11/d3d11defs.hpp"
#include "../d3d11/txtd3d11.hpp"

namespace DX11
{

  /** class represents 3d texture with better support for mapping/unmapping*/
  class TextureSysGPUDX11: public RefCount, private NoCopy
  {
  public:
    /// texture in system memory
    class SysMemTex: public RefCount, private NoCopy
    {
    public:
      SysMemTex(int width, int height, int depth);
      ~SysMemTex();

      // get testure
      ComRef<ID3D11Texture3D>& GetTexture() {return _texture;}

      /// lock texture
      void Lock();
      /// unlock texture
      void Unlock();
      /// reset locked status
      void ResetLocked();

      int GetWidth() const {return _width;}
      int GetHeight() const {return _height;}
      int GetDepth() const {return _depth;}
      bool CheckSize() {return _width * _height * _depth > 0;}

      /// returns if pitch of the mapped texture corresponds to its size
      bool IsPitchCorrespondingSize(int texelSizeInBytes);
      /// returns offset (in bytes) to mapped texture based on texel index and texel size in bytes
      int GetMappedTextureOffset(int texelIndex, int texelSizeInBytes) const;

      void* GetLockedData() {Assert(_locked); return _mappedSubresource.pData;}

    private:
      ComRef<ID3D11Texture3D> _texture;
      /// if system texture is locked
      bool _locked;
      /// locked box info
      D3D11_MAPPED_SUBRESOURCE _mappedSubresource;

      /// size
      int _width, _height, _depth;

    };

    class LockedData: public RefCount, private NoCopy
    {
    public:
      LockedData(Ref<TextureSysGPUDX11> texture);
      ~LockedData();

      /// gets locked data
      void* GetData() {return _data;}

      bool CheckSize();
      /// returns if pitch of the mapped texture corresponds to its size
      bool IsPitchCorrespondingSize(int texelSizeInBytes);
      /// returns offset (in bytes) to mapped texture based on texel index and texel size in bytes
      int GetMappedTextureOffset(int texelIndex, int texelSizeInBytes) const;

    private:
      /// reference to texture
      Ref<TextureSysGPUDX11> _texture;
      /// reference to sysmem texture
      Ref<TextureSysGPUDX11::SysMemTex> _sysMemTex;
      /// pointer to locked data
      void *_data;
    };

    friend class LockedData;
    friend class EngineDD11;

    TextureSysGPUDX11();
    ~TextureSysGPUDX11();

    SurfaceDataD3D11& GetSurface();

    /// returns GPU texture without updating it first (use with caution!)
    SurfaceDataD3D11& GetSurfaceNoUpdate() {return _surface;}

    static Ref<LockedData> GetLockedData(Ref<TextureSysGPUDX11> texture) { return new LockedData(texture);}

    /// create textures
    HRESULT CreateTextures(int width, int height, int depth, DXGI_FORMAT format);

  private: 
    Ref<SysMemTex> GetCurrentSysMemTex() 
    {
      Assert(_currentSysMemTex < NumSysMemTextures);
      if (_currentSysMemTex >= NumSysMemTextures)
        return Ref<SysMemTex>();
      else
        return _sysMemTex[_currentSysMemTex];
    }


    /// updates GPU texture with system memory texture
    void UpdateGPUTex(bool forceUpdate = false);

    /// used for updating gpu texture
    void UpdateGPUTexInternal(unsigned sysMemTextureIndex);

    /// lock current system memory texture
    void Lock();
    /// unlock current  system memory texture
    void Unlock();

    void EnterCriticalSection() {_criticalSection.Lock();}
    void LeaveCriticalSection() {_criticalSection.Unlock();}

    /// if GPU texture needs to be updated
    bool _needsUpdate;

    /// LUKE - we cannot use more than 1 system texture (because of the way the SimulWeather fills textures)
    /// Simul weather does not fill whole texture, but fills only part of the texture - that is why we cannot switch system memory textures
    /// Critical section should be enough -> we just have to prevent filling system memory texture while GPU texture is being updated
    static const unsigned NumSysMemTextures = 1;

    /// texture in system memory
    Ref<SysMemTex> _sysMemTex[NumSysMemTextures];
    /// current texture in system memory
    unsigned _currentSysMemTex;
    /// surface on GPU
    SurfaceDataD3D11 _surface;

    /// critical section used to synchronize texture updates
    mutable CriticalSection _criticalSection;
  };

}//namespace DX11

#endif //#if _ARMA3_SIMUL_WEATHER && _ENABLE_DX11
#endif //_ARMA3_SIMULCOMMONDX11_HPP
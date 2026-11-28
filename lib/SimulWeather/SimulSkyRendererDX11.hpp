#ifdef _MSC_VER
#pragma once
#endif

#ifndef _ARMA3_SIMULSKYRENDERERDX11_HPP
#define _ARMA3_SIMULSKYRENDERERDX11_HPP

#if _ARMA3_SIMUL_WEATHER && _ENABLE_DX11

#include "SimulSkyRendererBase.hpp"
#include "SimulCommonDX11.hpp"

namespace DX11
{


/** DX11 simul weather sky renderer 
Now we use sky renderer only for generating textures used for cloud fading, not for actual sky rendering
(we still use old Arma skybox). That's why we don't actually implement rendering functions.
*/
class SimulSkyRendererDX11: public SimulSkyRendererBase
{
public:
  /**
  \param engine pointer to rendering engine
  \param renderSky if we want to render the sky (otherwise old Arma skybox is used) 
  \param useFadeTextures if we want to use fade textures (used for cloud distance fading)
  \param useColourSky if we want to use coulour sky instead of normal sky
  */
  SimulSkyRendererDX11(DX11::EngineDD11* engine, bool renderSky = false, bool useFadeTextures = true, bool useColourSky = false);
  virtual ~SimulSkyRendererDX11();

  //@{ Implementation of SkyTexturesCallback
  virtual void FillSkyTexture(int alt_index,int texture_index,int texel_index,int num_texels,const float *float4_array);
  virtual void FillFadeTexturesSequentially(int texture_index,int texel_index,int num_texels,
    const float *loss_float4_array,
    const float *inscatter_float4_array);
  virtual void FillSunlightTexture(int texture_index,int texel_index,int num_texels,const float *float4_array);
  virtual void CycleTexturesForward();
  //@}

  /// gets textures used for distance fading
  void GetLossAndInscatterTextures(Ref<TextureSysGPUDX11>& l1,Ref<TextureSysGPUDX11>&l2,
    Ref<TextureSysGPUDX11>&i1,Ref<TextureSysGPUDX11>&i2);

private:
  //@{ Implementation of SimulSkyRendererBase
  /// initializes renderer - creates all resources,..., returns if succeeded
  virtual bool InitRenderer();
  /// deinitializes renderer - destroys all resources,...
  virtual void DeInitRenderer();
  /// creates textures used for fading
  virtual void CreateFadeTextures();
  /// creates textures used for sky rendering
  virtual void CreateSkyTextures();
  /// creates sunlight textures
  virtual void CreateSunlightTextures();
  //@}

  /// format of all textures
  DXGI_FORMAT _textureFormat;

  // Three sky textures - 2 to interpolate and one to fill
  Ref<TextureSysGPUDX11> _skyTextures[TextureCount];
  // Three sunlight textures.
  Ref<TextureSysGPUDX11> _sunlightTextures[TextureCount];

  /// textures used for distance fading
  Ref<TextureSysGPUDX11> _lossTextures[TextureCount];
  Ref<TextureSysGPUDX11> _inscatterTextures[TextureCount]; 

  /// pointer to engine
  DX11::EngineDD11* _engine;
};

}//namespace DX11

#endif //#if _ARMA3_SIMUL_WEATHER && _ENABLE_DX11

#endif //_ARMA3_SIMULSKYRENDERERDX11_HPP
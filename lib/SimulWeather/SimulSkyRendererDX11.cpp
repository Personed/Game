#include "../wpch.hpp"

#if _ARMA3_SIMUL_WEATHER && _ENABLE_DX11

#include "SimulSkyRendererDX11.hpp"
#include "../d3d11/engdd11.hpp"

namespace DX11
{

SimulSkyRendererDX11::SimulSkyRendererDX11(DX11::EngineDD11* engine, bool renderSky, bool useFadeTextures, bool useColourSky)
:SimulSkyRendererBase(renderSky, useFadeTextures, useColourSky), _engine(engine)
, _textureFormat(DXGI_FORMAT_UNKNOWN)
{
  Assert(_engine);
}

SimulSkyRendererDX11::~SimulSkyRendererDX11()
{
}

bool SimulSkyRendererDX11::InitRenderer()
{
  Assert(_engine);
  if (!_engine)
    return false;

  /// check supported texture format
  DXGI_FORMAT formats[] = 
  {
    DXGI_FORMAT_R16G16B16A16_FLOAT,
    DXGI_FORMAT_R32G32B32A32_FLOAT
  };

  _textureFormat = DXGI_FORMAT_UNKNOWN;
  for (int i=0; i< lenof(formats); ++i)
  {
    UINT support = _engine->GetFormatSupport(formats[i]);
    if ((support & D3D11_FORMAT_SUPPORT_TEXTURE3D)
      && (support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
    {
      _textureFormat = formats[i];
      break;
    }
  }

  if (DXGI_FORMAT_UNKNOWN == _textureFormat)
  {
    // no appropriate texture format found
    Fail("No appropriate texture format for Simul Weather sky renderer.")
      return false;
  }  

  LogF("SimulWeather - SkyRenderer initialized.");
  return true;
}

void SimulSkyRendererDX11::DeInitRenderer()
{
  for(int i=0 ; i<TextureCount ; ++i)
  {
    _lossTextures[i].Free();
    _inscatterTextures[i].Free();
    _skyTextures[i].Free();
    _sunlightTextures[i].Free();
  }
  _textureFormat = DXGI_FORMAT_UNKNOWN;
  LogF("SimulWeather - SkyRenderer deinitialized.");
}

void SimulSkyRendererDX11::FillSkyTexture(int alt_index,int texture_index,int texel_index,int num_texels,const float *float4_array)
{
  if (!GetInitialized())
    return;
  if (!GetRenderSky())
    return;

  Assert(_engine);
  if (!_engine)
    return;

  // SIMUL WEATHER TODO
}

void SimulSkyRendererDX11::FillFadeTexturesSequentially(int texture_index,int texel_index,int num_texels,
                                                       const float *loss_float4_array,
                                                       const float *inscatter_float4_array)
{
  if (!GetInitialized())
    return;
  if (!GetUseFadeTextures())
    return;

  Assert(_engine);
  if (!_engine)
    return;

  Assert(texture_index >= 0 && texture_index < TextureCount);
  if (texture_index < 0 || texture_index >= TextureCount)
    return;

  for (int i=0; i<=1; ++i)
  {
    // texture we want to update
    Ref<TextureSysGPUDX11> tex = NULL;
    // array with update values
    const float *updateArray = NULL;

    switch (i)
    {
    case 0:
      tex = _lossTextures[texture_index];
      updateArray = loss_float4_array;
      break;
    case 1:
      tex = _inscatterTextures[texture_index];
      updateArray = inscatter_float4_array;
      break;
    default:
      Fail("Invalid index!");
      break;
    }
    if (!tex|| !updateArray)
      continue; //no texture to update

    Ref<TextureSysGPUDX11::LockedData> lockedData = TextureSysGPUDX11::GetLockedData(tex);
    Assert(lockedData);
    if (!lockedData)
      return;

    void *tex_ptr=lockedData->GetData();
    Assert(tex_ptr);
    if (!tex_ptr)
      continue;
    Assert(lockedData->CheckSize());
    if (!lockedData->CheckSize())
      return;

    // fill texture
    if(_textureFormat==DXGI_FORMAT_R16G16B16A16_FLOAT)
    {
      // Convert the array of floats into float16 values for the texture.
      short *short_ptr=(short *)(tex_ptr);

      int texelSizeInBytes = 8;
      if (lockedData->IsPitchCorrespondingSize(texelSizeInBytes))
      {
        // perform fast copy
        short_ptr+=4*texel_index;
        for(int i=0;i<num_texels*4;i++)
          *short_ptr++=simul::sky::TextureGenerator::ToFloat16(*updateArray++);
      }
      else
      {
        for(int i=0;i<num_texels;i++)
        {
          int offset = lockedData->GetMappedTextureOffset(texel_index+i, texelSizeInBytes) / sizeof(short);
          short_ptr[offset] = simul::sky::TextureGenerator::ToFloat16(*updateArray++);
          short_ptr[offset+1] = simul::sky::TextureGenerator::ToFloat16(*updateArray++);
          short_ptr[offset+2] = simul::sky::TextureGenerator::ToFloat16(*updateArray++);
          short_ptr[offset+3] = simul::sky::TextureGenerator::ToFloat16(*updateArray++);
        }
      }

    }
    else
    {
      // Copy the array of floats into the texture.
      float *float_ptr=(float *)(tex_ptr);

      int texelSizeInBytes = 16;
      if (lockedData->IsPitchCorrespondingSize(texelSizeInBytes))
      {
        // perform fast copy
        float_ptr+=4*texel_index;
        for(int i=0;i<num_texels*4;i++)
          *float_ptr++=(*updateArray++);
      }
      else
      {
        for(int i=0;i<num_texels;i++)
        {
          int offset = lockedData->GetMappedTextureOffset(texel_index+i, texelSizeInBytes) / sizeof(float);
          float_ptr[offset] = simul::sky::TextureGenerator::ToFloat16(*updateArray++);
          float_ptr[offset+1] = simul::sky::TextureGenerator::ToFloat16(*updateArray++);
          float_ptr[offset+2] = simul::sky::TextureGenerator::ToFloat16(*updateArray++);
          float_ptr[offset+3] = simul::sky::TextureGenerator::ToFloat16(*updateArray++);
        }
      }

    }

    // no need to unlock and update (will be done when the texture is actually used)
  }

#if SIMUL_WEATHER_LOG_TEXTURE_FILLS
  LogF("SimulWeather - FillFadeTexturesSequentially done, texture index: %d.", texture_index);
#endif
}

void SimulSkyRendererDX11::FillSunlightTexture(int texture_index,int texel_index,int num_texels,const float *float4_array)
{
  if (!GetInitialized())
    return;
  if (!GetRenderSky())
    return;

  Assert(_engine);
  if (!_engine)
    return;

  // SIMUL WEATHER TODO
}

void SimulSkyRendererDX11::CycleTexturesForward()
{
  swap(_skyTextures[0], _skyTextures[1]);
  swap(_skyTextures[1], _skyTextures[2]);

  swap(_lossTextures[0], _lossTextures[1]);
  swap(_lossTextures[1], _lossTextures[2]);

  swap(_inscatterTextures[0], _inscatterTextures[1]);
  swap(_inscatterTextures[1], _inscatterTextures[2]);

  swap(_sunlightTextures[0], _sunlightTextures[1]);
  swap(_sunlightTextures[1], _sunlightTextures[2]);
}

void SimulSkyRendererDX11::CreateFadeTextures()
{
  if (!GetInitialized())
    return;
  if (!GetUseFadeTextures())
    return;

  // create textures
  for(int i=0;i<TextureCount;++i)
  {
    _lossTextures[i] = new TextureSysGPUDX11;
    _inscatterTextures[i] = new TextureSysGPUDX11;

    HRESULT hr = _lossTextures[i]->CreateTextures(fadeTexWidth,fadeTexHeight, numAltitudes, _textureFormat );
    if (FAILED(hr))
    {
      _engine->DDError11("SimulWeather - Creating loss texture failed!", hr);
    }

    hr = _inscatterTextures[i]->CreateTextures(fadeTexWidth,fadeTexHeight, numAltitudes, _textureFormat );
    if (FAILED(hr))
    {
      _engine->DDError11("SimulWeather - Creating inscatter texture failed!", hr);
    }
  }
  LogF("SimulWeather - SkyRenderer - fade textures successfully created");
}

void SimulSkyRendererDX11::CreateSkyTextures()
{
  if (!GetInitialized())
    return;
  if (!GetRenderSky())
    return;

  Assert(_engine);
  if (!_engine)
    return;

  for (int i=0; i < TextureCount; ++i)
  {
    _skyTextures[i].Free();
  }
  // SIMUL WEATHER TODO
}

void SimulSkyRendererDX11::CreateSunlightTextures()
{
  if (!GetInitialized())
    return;
  if (!GetRenderSky())
    return;

  Assert(_engine);
  if (!_engine)
    return;

  for (int i=0; i < TextureCount; ++i)
  {
    _sunlightTextures[i].Free();
  }

  // SIMUL WEATHER TODO
}

void SimulSkyRendererDX11::GetLossAndInscatterTextures(Ref<TextureSysGPUDX11>& l1,Ref<TextureSysGPUDX11>&l2,
                                                      Ref<TextureSysGPUDX11>&i1,Ref<TextureSysGPUDX11>&i2)
{
  // GetSurface also automatically unmaps texture if needed
  l1=_lossTextures[0];
  l2=_lossTextures[1];
  i1=_inscatterTextures[0];
  i2=_inscatterTextures[1];
}

}//namespace DX11

#endif //#if _ARMA3_SIMUL_WEATHER && _ENABLE_DX11
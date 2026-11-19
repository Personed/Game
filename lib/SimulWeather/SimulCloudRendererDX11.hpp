#pragma once
#ifndef DAYZ_SIMUL_CLOUD_RENDERER_DX11_HPP
#define DAYZ_SIMUL_CLOUD_RENDERER_DX11_HPP

#if _ARMA3_SIMUL_WEATHER

#include "SimulCloudRendererBase.hpp"
#include "SimulCommonDX11.hpp"
#include "../d3d11/Dx11Mesh.hpp"
#include "../d3d11/Dx11Texture.hpp"
#include "../d3d11/Dx11ConstantBuffer.hpp"
#include <vector>

// D3D11 resource/callback layer for Simul cloud simulation. The base class
// continues to own the shared Simul keyframer and geometry helper.
class SimulCloudRendererDX11 : public SimulCloudRendererBase
{
public:
  explicit SimulCloudRendererDX11(Dx11DeviceAccess *engine);
  virtual ~SimulCloudRendererDX11();

  virtual void FillCloudTextureSequentially(int textureIndex, int texelIndex,
    int numTexels, const unsigned *values);
  virtual void CycleTexturesForward();
  virtual void FillIlluminationSequentially(int sourceIndex, int texelIndex,
    int numTexels, const unsigned char *values);
  virtual void Render(int cb, bool cubemap = false);

  void **GetCloudTextures();
  void SetLossTextures(Ref<TextureSysGPUDX11> &t1,
    Ref<TextureSysGPUDX11> &t2);
  void SetInscatterTextures(Ref<TextureSysGPUDX11> &t1,
    Ref<TextureSysGPUDX11> &t2);
  void SetAltitudeTextureCoordinate(float value) { altitude_tex_coord = value; }
  void SetFadeInterpolation(float value) { fade_interp = value; }
  ID3D11ShaderResourceView *NoiseTextureView() const
    { return _noiseTexture.ResourceView(); }

private:
  virtual bool InitRenderer();
  virtual void DeInitRenderer();
  virtual void CreateNoiseTexture(RString fileName);
  virtual void CreateCloudTexture();
  virtual void CreateIlluminationTexture();

  Dx11DeviceAccess *_engine;
  DXGI_FORMAT _textureFormat;
  Ref<TextureSysGPUDX11> _cloudTextures[TextureCount];
  ID3D11ShaderResourceView *_cloudTextureViews[TextureCount];
  Ref<TextureSysGPUDX11> _illuminationTexture;
  Ref<TextureSysGPUDX11> _skyLossTexture1, _skyLossTexture2;
  Ref<TextureSysGPUDX11> _skyInscatterTexture1, _skyInscatterTexture2;
  Dx11Texture _noiseTexture;
  RString _noiseTextureFileName;
  float altitude_tex_coord;
  float fade_interp;
  ID3D11VertexShader *_cloudVertexShader;
  ID3D11PixelShader *_cloudPixelShader;
  ID3D11InputLayout *_cloudInputLayout;
  Dx11Mesh _cloudVertexStream;
  ID3D11SamplerState *_cloudSampler;
  ID3D11SamplerState *_noiseSampler;
  ID3D11SamplerState *_fadeSampler;
  ID3D11BlendState *_cloudBlendState;
  ID3D11DepthStencilState *_cloudDepthState;
  Dx11ConstantBuffer _cloudConstants;
  std::vector<Vertex_t> _vertices;
};

#endif
#endif

#include "../wpch.hpp"

#if _ARMA3_SIMUL_WEATHER

#include "SimulCloudRendererDX11.hpp"
#include "../scene.hpp"
#include "../world.hpp"
#include "../depMake.hpp"
#include "../camera.hpp"
#include "../d3d11/Dx11ShaderCompiler.hpp"

namespace
{
const char CloudVertexShaderSource[] =
  "cbuffer CloudConstants : register(b0) {"
  "row_major float4x4 worldViewProj; float4 eyePosition; float4 fractalScale;"
  "float4 fogPars; float4 lightResponse; float4 mieRayleighRatio;"
  "float4 skyLightColour; float4 lightDir; float4 params; float4 fadeParams;"
  "float4 lightningMultipliers; float4 lightningColour;"
  "float4 illuminationOrigin; float4 illuminationScales; };"
  "struct VSIn { float3 position:POSITION; float3 texCoords:TEXCOORD0;"
  "float layerFade:TEXCOORD1; float2 noiseUV:TEXCOORD2;"
  "float3 sunlight:TEXCOORD3; };"
  "struct VSOut { float4 position:SV_POSITION; float3 texCoords:TEXCOORD0;"
  "float2 noiseUV:TEXCOORD1; float layerFade:TEXCOORD2;"
  "float3 relativePosition:TEXCOORD3; float3 fadeUV:TEXCOORD4;"
  "float3 illuminationUV:TEXCOORD5; float3 sunlight:TEXCOORD6;"
  "float fog:TEXCOORD7; };"
  "VSOut main(VSIn i) { VSOut o; float4 p=float4(i.position,1);"
  "o.position=mul(worldViewProj,p); o.texCoords=i.texCoords;"
  "o.noiseUV=i.noiseUV; o.layerFade=i.layerFade;"
  "o.relativePosition=i.position-eyePosition.xyz; o.sunlight=i.sunlight;"
  "float d=length(o.relativePosition); float3 v=normalize(o.relativePosition);"
  "o.fadeUV=float3(d/max(fadeParams.z,1.0),0.5*(1.0-v.y),fadeParams.y);"
  "o.illuminationUV=(i.position.xzy-illuminationOrigin.xyz)/"
  "max(abs(illuminationScales.xyz),float3(0.001,0.001,0.001));"
  "o.illuminationUV.z=0.5; o.fog=saturate((d-fogPars.x)*fogPars.y);"
  "o.fog*=o.fog; return o; }";

const char CloudPixelShaderSource[] =
  "cbuffer CloudConstants : register(b0) {"
  "row_major float4x4 worldViewProj; float4 eyePosition; float4 fractalScale;"
  "float4 fogPars; float4 lightResponse; float4 mieRayleighRatio;"
  "float4 skyLightColour; float4 lightDir; float4 params; float4 fadeParams;"
  "float4 lightningMultipliers; float4 lightningColour;"
  "float4 illuminationOrigin; float4 illuminationScales; };"
  "Texture3D cloud0:register(t0); Texture3D cloud1:register(t1);"
  "Texture2D noiseTex:register(t2); Texture3D illumination:register(t3);"
  "Texture3D loss0:register(t4); Texture3D loss1:register(t5);"
  "Texture3D inscatter0:register(t6); Texture3D inscatter1:register(t7);"
  "SamplerState cloudS:register(s0); SamplerState noiseS:register(s1);"
  "SamplerState illuminationS:register(s2); SamplerState fadeS:register(s3);"
  "struct PSIn { float4 position:SV_POSITION; float3 texCoords:TEXCOORD0;"
  "float2 noiseUV:TEXCOORD1; float layerFade:TEXCOORD2;"
  "float3 relativePosition:TEXCOORD3; float3 fadeUV:TEXCOORD4;"
  "float3 illuminationUV:TEXCOORD5; float3 sunlight:TEXCOORD6;"
  "float fog:TEXCOORD7; };"
  "float HG(float g,float c) { float g2=g*g; float q=max(1+g2-2*g*c,0.0001);"
  "return 0.5*0.079577+0.5*(1-g2)/(4*3.1415926536*pow(q,1.5)); }"
  "float4 main(PSIn i):SV_TARGET { float3 n=noiseTex.Sample(noiseS,i.noiseUV).rgb-"
  "0.4980392; n+=(noiseTex.Sample(noiseS,i.noiseUV*8).rgb-0.4980392)*0.5;"
  "n*=0.5+0.5*saturate(i.texCoords.z); float3 tc=i.texCoords+fractalScale.xyz*n;"
  "float4 d=lerp(cloud0.Sample(cloudS,tc),cloud1.Sample(cloudS,tc),params.y);"
  "d.x=saturate(d.x*i.layerFade*(1+params.w)-params.w);"
  "if(d.x<=0 && d.z<=0) discard; float3 v=normalize(i.relativePosition);"
  "float c=dot(normalize(lightDir.xyz),v); float beta=lightResponse.x*"
  "HG(params.z*d.z,c); float3 loss=lerp(loss0.Sample(fadeS,i.fadeUV).rgb,"
  "loss1.Sample(fadeS,i.fadeUV).rgb,fadeParams.x);"
  "float4 ins=lerp(inscatter0.Sample(fadeS,i.fadeUV),"
  "inscatter1.Sample(fadeS,i.fadeUV),fadeParams.x);"
  "float br=0.0596831*(1+c*c); float bm=HG(params.x,c);"
  "float3 phase=(br+bm*ins.a*mieRayleighRatio.rgb)/"
  "(1+ins.a*mieRayleighRatio.rgb)*ins.rgb;"
  "float3 ambient=d.w*skyLightColour.rgb; float3 color=(d.z*beta+"
  "lightResponse.y*d.y)*i.sunlight+ambient; color=color*loss+phase;"
  "color=lerp(color,skyLightColour.rgb,i.fog);"
  "return float4(color*d.x,d.x); }";

struct CloudConstants
{
  float worldViewProj[16];
  float eyePosition[4];
  float fractalScale[4];
  float fogPars[4];
  float lightResponse[4];
  float mieRayleighRatio[4];
  float skyLightColour[4];
  float lightDir[4];
  float params[4];
  float fadeParams[4];
  float lightningMultipliers[4];
  float lightningColour[4];
  float illuminationOrigin[4];
  float illuminationScales[4];
};
}

SimulCloudRendererDX11::SimulCloudRendererDX11(Dx11DeviceAccess *engine)
  : SimulCloudRendererBase(), _engine(engine),
    _textureFormat(DXGI_FORMAT_UNKNOWN), altitude_tex_coord(0.0f),
    fade_interp(0.0f), _cloudVertexShader(NULL), _cloudPixelShader(NULL),
    _cloudInputLayout(NULL), _cloudSampler(NULL),
    _noiseSampler(NULL), _fadeSampler(NULL), _cloudBlendState(NULL),
    _cloudDepthState(NULL), _vertices(MaxVertices)
{
  for (int i = 0; i < TextureCount; ++i) _cloudTextureViews[i] = NULL;
}

SimulCloudRendererDX11::~SimulCloudRendererDX11()
{
  DeInitRenderer();
}

bool SimulCloudRendererDX11::InitRenderer()
{
  if (!_engine || !_engine->D3D11Device()) return false;
  UINT support = 0;
  HRESULT hr = _engine->D3D11Device()->CheckFormatSupport(
    DXGI_FORMAT_B8G8R8A8_UNORM, &support);
  if (FAILED(hr) || !(support & D3D11_FORMAT_SUPPORT_TEXTURE3D) ||
      !(support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
  {
    RptF("DX11 SimulWeather: BGRA8 volume textures are unsupported");
    return false;
  }
  _textureFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

  ID3DBlob *vsCode = NULL, *psCode = NULL, *diagnostics = NULL;
  hr = Dx11ShaderCompiler::CompileVertexShader(_engine->D3D11Device(),
    CloudVertexShaderSource, sizeof(CloudVertexShaderSource) - 1, "main",
    &_cloudVertexShader, &vsCode, &diagnostics);
  if (diagnostics) { RptF("DX11 cloud VS: %s", (const char *)diagnostics->GetBufferPointer()); diagnostics->Release(); diagnostics = NULL; }
  if (FAILED(hr)) return false;
  hr = Dx11ShaderCompiler::CompilePixelShader(_engine->D3D11Device(),
    CloudPixelShaderSource, sizeof(CloudPixelShaderSource) - 1, "main",
    &_cloudPixelShader, &psCode, &diagnostics);
  if (diagnostics) { RptF("DX11 cloud PS: %s", (const char *)diagnostics->GetBufferPointer()); diagnostics->Release(); diagnostics = NULL; }
  if (FAILED(hr)) { vsCode->Release(); return false; }

  D3D11_INPUT_ELEMENT_DESC layout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
      (UINT)offsetof(Vertex_t, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
      (UINT)offsetof(Vertex_t, texCoords), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0,
      (UINT)offsetof(Vertex_t, layerFade), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 0,
      (UINT)offsetof(Vertex_t, texCoordsNoise), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 3, DXGI_FORMAT_R32G32B32_FLOAT, 0,
      (UINT)offsetof(Vertex_t, sunlightColour), D3D11_INPUT_PER_VERTEX_DATA, 0}
  };
  hr = _engine->D3D11Device()->CreateInputLayout(layout, lenof(layout),
    vsCode->GetBufferPointer(), vsCode->GetBufferSize(), &_cloudInputLayout);
  vsCode->Release();
  psCode->Release();
  if (FAILED(hr)) return false;

  hr = _cloudConstants.Initialize(_engine->D3D11Device(),
    sizeof(CloudConstants));
  if (FAILED(hr)) return false;

  hr = _cloudVertexStream.InitializeVertexStream(_engine->D3D11Device(),
    MaxVertices, sizeof(Vertex_t));
  if (FAILED(hr)) return false;

  D3D11_SAMPLER_DESC sampler = {};
  sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
  sampler.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
  sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sampler.MaxAnisotropy = 1;
  sampler.MaxLOD = D3D11_FLOAT32_MAX;
  hr = _engine->D3D11Device()->CreateSamplerState(&sampler, &_cloudSampler);
  if (FAILED(hr)) return false;
  sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
  hr = _engine->D3D11Device()->CreateSamplerState(&sampler, &_noiseSampler);
  if (FAILED(hr)) return false;
  sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
  sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  hr = _engine->D3D11Device()->CreateSamplerState(&sampler, &_fadeSampler);
  if (FAILED(hr)) return false;

  D3D11_BLEND_DESC blend = {};
  blend.RenderTarget[0].BlendEnable = TRUE;
  blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
  blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
  blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  hr = _engine->D3D11Device()->CreateBlendState(&blend, &_cloudBlendState);
  if (FAILED(hr)) return false;

  D3D11_DEPTH_STENCIL_DESC depth = {};
  depth.DepthEnable = TRUE;
  depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
  hr = _engine->D3D11Device()->CreateDepthStencilState(&depth,
    &_cloudDepthState);
  if (FAILED(hr)) return false;

  if (!_noiseTextureFileName.IsEmpty())
    CreateNoiseTexture(_noiseTextureFileName);
  return true;
}

void SimulCloudRendererDX11::DeInitRenderer()
{
  if (_cloudVertexShader) { _cloudVertexShader->Release(); _cloudVertexShader = NULL; }
  if (_cloudPixelShader) { _cloudPixelShader->Release(); _cloudPixelShader = NULL; }
  if (_cloudInputLayout) { _cloudInputLayout->Release(); _cloudInputLayout = NULL; }
  _cloudVertexStream.Reset();
  if (_cloudSampler) { _cloudSampler->Release(); _cloudSampler = NULL; }
  if (_noiseSampler) { _noiseSampler->Release(); _noiseSampler = NULL; }
  if (_fadeSampler) { _fadeSampler->Release(); _fadeSampler = NULL; }
  if (_cloudBlendState) { _cloudBlendState->Release(); _cloudBlendState = NULL; }
  if (_cloudDepthState) { _cloudDepthState->Release(); _cloudDepthState = NULL; }
  _cloudConstants.Reset();
  for (int i = 0; i < TextureCount; ++i)
  {
    _cloudTextures[i].Free();
    _cloudTextureViews[i] = NULL;
  }
  _illuminationTexture.Free();
  _skyLossTexture1.Free();
  _skyLossTexture2.Free();
  _skyInscatterTexture1.Free();
  _skyInscatterTexture2.Free();
  _noiseTexture.Reset();
  _textureFormat = DXGI_FORMAT_UNKNOWN;
}

void SimulCloudRendererDX11::CreateNoiseTexture(RString fileName)
{
  _noiseTexture.Reset();
  _noiseTextureFileName = fileName;
  if (!_engine || !_engine->D3D11Device() || fileName.IsEmpty()) return;

  RString resolved = GlobResolveTexture(fileName);
  HRESULT hr = _noiseTexture.InitializeFromSource(_engine->D3D11Device(),
    (const char *)resolved);
  if (FAILED(hr))
    RptF("DX11 SimulWeather: could not load noise texture %s (0x%08x)",
      cc_cast(resolved), (unsigned)hr);
}

void SimulCloudRendererDX11::CreateCloudTexture()
{
  if (!_engine || _textureFormat == DXGI_FORMAT_UNKNOWN) return;
  for (int i = 0; i < TextureCount; ++i)
  {
    _cloudTextures[i] = new TextureSysGPUDX11(_engine, 4);
    HRESULT hr = _cloudTextures[i]->CreateTextures(cloud_tex_width_x,
      cloud_tex_length_y, cloud_tex_depth_z, _textureFormat);
    if (FAILED(hr))
    {
      RptF("DX11 SimulWeather: cloud volume %d creation failed (0x%08x)",
        i, (unsigned)hr);
      for (int j = 0; j < TextureCount; ++j) _cloudTextures[j].Free();
      return;
    }
    _cloudTextureViews[i] = _cloudTextures[i]->GetResourceView();
  }
}

void SimulCloudRendererDX11::CreateIlluminationTexture()
{
  if (!_engine || _textureFormat == DXGI_FORMAT_UNKNOWN ||
      _illuminationTexWidth <= 0 || _illuminationTexHeight <= 0 ||
      _illuminationTexDepth <= 0)
    return;
  _illuminationTexture = new TextureSysGPUDX11(_engine, 4);
  HRESULT hr = _illuminationTexture->CreateTextures(_illuminationTexWidth,
    _illuminationTexHeight, _illuminationTexDepth, _textureFormat);
  if (FAILED(hr))
  {
    RptF("DX11 SimulWeather: illumination volume creation failed (0x%08x)",
      (unsigned)hr);
    _illuminationTexture.Free();
  }
}

void SimulCloudRendererDX11::FillCloudTextureSequentially(int textureIndex,
  int texelIndex, int numTexels, const unsigned *values)
{
  if (numTexels <= 0 || !values || textureIndex < 0 ||
      textureIndex >= TextureCount)
    return;
  Ref<TextureSysGPUDX11> texture = _cloudTextures[textureIndex];
  if (!texture) return;
  Ref<TextureSysGPUDX11::LockedData> locked =
    TextureSysGPUDX11::GetLockedData(texture);
  if (!locked || !locked->CheckSize() || !locked->GetData()) return;
  const size_t texelCount = (size_t)texture->Width() * texture->Height() *
    texture->Depth();
  if (texelIndex < 0 || (size_t)texelIndex > texelCount ||
      (size_t)numTexels > texelCount - (size_t)texelIndex)
    return;
  memcpy((unsigned *)locked->GetData() + texelIndex, values,
    (size_t)numTexels * sizeof(unsigned));
}

void SimulCloudRendererDX11::CycleTexturesForward()
{
  Swap(_cloudTextures[0], _cloudTextures[1]);
  Swap(_cloudTextures[1], _cloudTextures[2]);
  for (int i = 0; i < TextureCount; ++i)
    _cloudTextureViews[i] = _cloudTextures[i]
      ? _cloudTextures[i]->GetResourceView() : NULL;
}

void SimulCloudRendererDX11::FillIlluminationSequentially(int sourceIndex,
  int texelIndex, int numTexels, const unsigned char *values)
{
  if (numTexels <= 0 || !values || sourceIndex < 0 || sourceIndex > 3 ||
      !_illuminationTexture)
    return;
  Ref<TextureSysGPUDX11::LockedData> locked =
    TextureSysGPUDX11::GetLockedData(_illuminationTexture);
  if (!locked || !locked->CheckSize() || !locked->GetData()) return;
  const size_t texelCount = (size_t)_illuminationTexture->Width() *
    _illuminationTexture->Height() * _illuminationTexture->Depth();
  if (texelIndex < 0 || (size_t)texelIndex > texelCount ||
      (size_t)numTexels > texelCount - (size_t)texelIndex)
    return;
  unsigned *dst = (unsigned *)locked->GetData() + texelIndex;
  static const unsigned channelShift[4] = {16, 8, 0, 24};
  const unsigned shift = channelShift[sourceIndex];
  const unsigned mask = ~(255u << shift);
  for (int i = 0; i < numTexels; ++i)
    dst[i] = (dst[i] & mask) | ((unsigned)values[i] << shift);
}

void SimulCloudRendererDX11::SetLossTextures(Ref<TextureSysGPUDX11> &t1,
  Ref<TextureSysGPUDX11> &t2)
{
  _skyLossTexture1 = t1;
  _skyLossTexture2 = t2;
}

void SimulCloudRendererDX11::SetInscatterTextures(
  Ref<TextureSysGPUDX11> &t1, Ref<TextureSysGPUDX11> &t2)
{
  _skyInscatterTexture1 = t1;
  _skyInscatterTexture2 = t2;
}

void **SimulCloudRendererDX11::GetCloudTextures()
{
  for (int i = 0; i < TextureCount; ++i)
    _cloudTextureViews[i] = _cloudTextures[i]
      ? _cloudTextures[i]->GetResourceView() : NULL;
  return (void **)_cloudTextureViews;
}

void SimulCloudRendererDX11::Render(int, bool cubemap)
{
  if (!_engine || !_engine->D3D11Context() || !_cloudVertexShader ||
      !_cloudPixelShader || !_cloudVertexStream.VertexBuffer() || !GScene ||
      !GScene->GetCamera() || !cloudKeyframer || !cloudInterface ||
      !skyInterface || !helper)
    return;

  Camera *camera = GScene->GetCamera();
  int count = FillVertices(this, &_vertices[0], MaxVertices, sizeof(Vertex_t),
    *camera, camera->ProjectionNormal(), cubemap, false, false);
  if (count < 3) return;

  ID3D11DeviceContext *context = _engine->D3D11Context();
  UINT firstVertex = 0;
  void *mappedVertices = NULL;
  HRESULT hr = _cloudVertexStream.LockVertices(context, (UINT)count,
    firstVertex, &mappedVertices);
  if (FAILED(hr)) return;
  memcpy(mappedVertices, &_vertices[0], (size_t)count * sizeof(Vertex_t));
  hr = _cloudVertexStream.UnlockVertices(context);
  if (FAILED(hr)) return;

  CloudConstants constants = {};
  Matrix4 wvp = camera->GetInvTransform() * camera->ProjectionNormal();
  for (int row = 0; row < 4; ++row)
    for (int col = 0; col < 4; ++col)
      constants.worldViewProj[row * 4 + col] = wvp(col, row);
  const Vector3 &eye = camera->Position();
  constants.eyePosition[0] = eye.X();
  constants.eyePosition[1] = eye.Y();
  constants.eyePosition[2] = eye.Z();
  simul::sky::float4 fractal = helper->GetFractalScales(cloudInterface);
  if (cubemap) fractal *= 0.0f;
  for (int i = 0; i < 4; ++i) constants.fractalScale[i] = fractal[i];
  constants.fogPars[0] = _fogStart;
  constants.fogPars[1] = _fogEnd > _fogStart
    ? 1.0f / (_fogEnd - _fogStart) : 1.0f;
  constants.lightResponse[0] = _directLightCoef *
    cloudInterface->GetLightResponse();
  constants.lightResponse[1] = _indirectLightCoef *
    cloudInterface->GetSecondaryLightResponse();
  simul::sky::float4 mie = skyInterface->GetMieRayleighRatio();
  for (int i = 0; i < 4; ++i) constants.mieRayleighRatio[i] = mie[i];
  simul::sky::float4 ambient = skyInterface->GetAmbientLight(
    camera->Position()[1] * 0.001f) * cloudInterface->GetAmbientLightResponse();
  for (int i = 0; i < 4; ++i) constants.skyLightColour[i] = ambient[i];
  simul::sky::float4 sun = skyInterface->GetDirectionToLight();
  Swap(sun.y, sun.z);
  for (int i = 0; i < 4; ++i) constants.lightDir[i] = sun[i];
  constants.params[0] = skyInterface->GetMieEccentricity();
  constants.params[1] = cloudKeyframer->GetInterpolation();
  constants.params[2] = cloudInterface->GetMieAsymmetry();
  constants.params[3] = cloudInterface->GetAlphaSharpness();
  constants.fadeParams[0] = fade_interp;
  constants.fadeParams[1] = altitude_tex_coord;
  constants.fadeParams[2] = _maxFadeDistanceMetres;
  hr = _cloudConstants.Update(context, &constants, sizeof(constants));
  if (FAILED(hr)) return;

  ID3D11ShaderResourceView *views[8] = {
    _cloudTextures[0] ? _cloudTextures[0]->GetResourceView() : NULL,
    _cloudTextures[1] ? _cloudTextures[1]->GetResourceView() : NULL,
    _noiseTexture.ResourceView(),
    _illuminationTexture ? _illuminationTexture->GetResourceView() : NULL,
    _skyLossTexture1 ? _skyLossTexture1->GetResourceView() : NULL,
    _skyLossTexture2 ? _skyLossTexture2->GetResourceView() : NULL,
    _skyInscatterTexture1 ? _skyInscatterTexture1->GetResourceView() : NULL,
    _skyInscatterTexture2 ? _skyInscatterTexture2->GetResourceView() : NULL
  };
  context->PSSetShaderResources(0, lenof(views), views);
  ID3D11SamplerState *samplers[4] = {
    _cloudSampler, _noiseSampler, _cloudSampler, _fadeSampler
  };
  context->PSSetSamplers(0, lenof(samplers), samplers);
  _cloudConstants.BindVS(context, 0);
  _cloudConstants.BindPS(context, 0);
  context->IASetInputLayout(_cloudInputLayout);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  UINT stride = sizeof(Vertex_t), offset = 0;
  ID3D11Buffer *cloudVertexBuffer = _cloudVertexStream.VertexBuffer();
  context->IASetVertexBuffers(0, 1, &cloudVertexBuffer, &stride, &offset);
  context->VSSetShader(_cloudVertexShader, NULL, 0);
  context->PSSetShader(_cloudPixelShader, NULL, 0);

  ID3D11BlendState *prevBlendState = NULL;
  float prevBlendFactor[4] = {0, 0, 0, 0};
  UINT prevSampleMask = 0xffffffff;
  context->OMGetBlendState(&prevBlendState, prevBlendFactor, &prevSampleMask);

  ID3D11DepthStencilState *prevDepthState = NULL;
  UINT prevStencilRef = 0;
  context->OMGetDepthStencilState(&prevDepthState, &prevStencilRef);

  const float blendFactor[4] = {0, 0, 0, 0};
  context->OMSetBlendState(_cloudBlendState, blendFactor, 0xffffffff);
  context->OMSetDepthStencilState(_cloudDepthState, 0);
  context->Draw((UINT)count, firstVertex);

  context->OMSetBlendState(prevBlendState, prevBlendFactor, prevSampleMask);
  if (prevBlendState) prevBlendState->Release();
  context->OMSetDepthStencilState(prevDepthState, prevStencilRef);
  if (prevDepthState) prevDepthState->Release();

  ID3D11ShaderResourceView *nullViews[8] = {};
  context->PSSetShaderResources(0, lenof(nullViews), nullViews);
  ID3D11SamplerState *nullSamplers[4] = {};
  context->PSSetSamplers(0, lenof(nullSamplers), nullSamplers);
  ID3D11Buffer *nullCB[1] = {NULL};
  context->VSSetConstantBuffers(0, 1, nullCB);
  context->PSSetConstantBuffers(0, 1, nullCB);
  ID3D11Buffer *nullVB[1] = {NULL};
  UINT nullStride = 0, nullOffset = 0;
  context->IASetVertexBuffers(0, 1, nullVB, &nullStride, &nullOffset);
}

#endif

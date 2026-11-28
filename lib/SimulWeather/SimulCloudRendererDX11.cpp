#include "../wpch.hpp"

#if _ARMA3_SIMUL_WEATHER && _ENABLE_DX11

#include "SimulCloudRendererDX11.hpp"
#include "../d3d11/engdd11.hpp"
#include "../d3d11/txtd3d11.hpp"
#include "../scene.hpp"
#include "../world.hpp"
#include "../depMake.hpp"
#include "SimulWeatherBase.hpp"

namespace DX11
{

//=====================================================================================
// Implementation of SimulCloudVertexBuffer11
//=====================================================================================
SimulCloudVertexBuffer11::SimulCloudVertexBuffer11(int maxCount, int vertexSize)
  :_maxCount(maxCount), _vertexSize(vertexSize), _usedFrameId(-1), _curWritePos(0)
{
}

SimulCloudVertexBuffer11::~SimulCloudVertexBuffer11()
{

}

void SimulCloudVertexBuffer11::CreateBuffer()
{
  _buffer.Free();

  Assert(GEngineDD11);
  if (!GEngineDD11)
    return;

  Assert(_maxCount > 0);
  if (_maxCount<= 0) return;

  Assert(_vertexSize > 0);
  if (_vertexSize <= 0)
    return;

  MyD3DDeviceCB &dev = GEngineDD11->GetD3DDeviceRec();
  int byteSize = _maxCount*_vertexSize;

  // make sure there is no extensive VRAM memory overhead
  GEngineDD11->TextBankDD()->ReserveMemory(byteSize,3);

  D3D11_BUFFER_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.ByteWidth = byteSize;
  desc.Usage = D3D11_USAGE_DYNAMIC;
  desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; 
  desc.MiscFlags = 0;
  desc.StructureByteStride = 0;
RetryV:
  // note: goto before the constructor causes destruction
  PROFILE_SCOPE_DETAIL_EX(swCVBD,SW);
  EngineDD11::CheckMemTypeScope scope(GEngineDD11,"VBdynamic",byteSize);
  HRESULT err = dev->CreateBuffer(&desc, NULL, _buffer.Init());
  if (err!=D3D_OK)
  {
    if (err==E_OUTOFMEMORY)
    {
      GEngineDD11->DDError11("CreateVertexBuffer - dynamic",err);
      bool doRetry = false;
      if (GEngineDD11->TextBankDD()->ForcedReserveMemory(byteSize))
      {
        doRetry = true;
      }
      size_t freed = GEngineDD11->FreeOnDemandSystemMemoryLowLevel(byteSize);
      if (freed!=0 || doRetry) goto RetryV;
    }
    // debugging opportunity
    LogF("Error during v-buffer creation: error %x",err);
    size_t freed = GEngineDD11->FreeOnDemandSystemMemoryLowLevel(byteSize);
    if (freed!=0) goto RetryV;
    RptF("Error during v-buffer creation: error %x",err);
    GEngineDD11->DDError11("CreateVertexBuffer",err);
    GEngineDD11->RequestFlushMemoryCritical();
    return;
  }
  DoAssert(_buffer);
}

int SimulCloudVertexBuffer11::FillVertices(void *data, int vertexSize, int numVertices)
{
  PROFILE_SCOPE_EX(swVBFill, SW);
  Assert(_buffer);
  if (!_buffer)
    return -1;

  // mark as used
  SetUsed();

  Assert(data);
  if (!data)
    return -1;

  Assert(vertexSize == _vertexSize);
  if (vertexSize != _vertexSize)
    return -1;

  Assert(GEngineDD11);
  if (!GEngineDD11)
    return -1;

  D3D11_MAP mapType;

  if (numVertices <= GetNumFree())
  {
    // there is still enough space
    mapType = D3D11_MAP_WRITE_NO_OVERWRITE;
  }
  else
  {
    // not enoug space - need to discard and start from beginning
    _curWritePos = 0;
    mapType = D3D11_MAP_WRITE_DISCARD;
  }

  // lock
  void *vbData = 0;
  HRESULT hr;
  {
    PROFILE_SCOPE_EX(swVBLck, SW);
    hr = GEngineDD11->Map(_buffer, 0, mapType, 0, &vbData);
  }
  if (FAILED(hr) || !vbData)
  {
    GEngineDD11->DDError11("SimulWeather - VB Lock failed!", hr);
    return -1;
  }

  // fill
  memcpy(((unsigned char*)vbData) + _curWritePos*vertexSize, data, vertexSize*numVertices);

  // unlock
  GEngineDD11->Unmap(_buffer,0);

  // reemember old write pos
  int oldWritePos = _curWritePos;
  // advance write pos
  _curWritePos += numVertices;

  return oldWritePos;

}


//=====================================================================================
// Implementation of SimulCloudRendererDX11
//=====================================================================================

const bool big_endian=false;
static unsigned bits[4]={	(unsigned)0x0F00,
(unsigned)0x000F,
(unsigned)0x00F0,
(unsigned)0xF000};
static unsigned bits8[4]={	(unsigned)0x00FF0000,
(unsigned)0x000000FF,
(unsigned)0x0000FF00,
(unsigned)0xFF000000};

static void SetBits8()
{
  simul::clouds::TextureGenerator::SetBits(bits8[0],bits8[1],bits8[2],bits8[3],(unsigned)4,big_endian);
}

SimulCloudRendererDX11::SimulCloudRendererDX11(DX11::EngineDD11* engine)
:SimulCloudRendererBase()
,_engine(engine)
,_textureFormat(DXGI_FORMAT_UNKNOWN)
{
  Assert(_engine);
  cam_pos.x = cam_pos.y = cam_pos.z = cam_pos.w = 0;
}

SimulCloudRendererDX11::~SimulCloudRendererDX11()
{
}


void SimulCloudRendererDX11::FillCloudTextureSequentially(int texture_index,int texel_index,int num_texels,const unsigned *uint32_array)
{
  if(!num_texels)
    return;

  Assert(texture_index >= 0 && texture_index < TextureCount);
  if (texture_index < 0 || texture_index >= TextureCount)
    return;

  Ref<TextureSysGPUDX11> texture = _cloudTextures[texture_index];
  Assert(texture);
  if (!texture)
    return;

  /// fill texture 
  Ref<TextureSysGPUDX11::LockedData> lockedData = TextureSysGPUDX11::GetLockedData(texture);
  Assert(lockedData);
  if (!lockedData)
    return;

  void *data = lockedData->GetData();
  Assert(data);
  if (!data)
    return;

  Assert(lockedData->CheckSize());
  if (!lockedData->CheckSize())
    return;

  unsigned *ptr=(unsigned *)(data);
  const int texelSizeInBytes = 4;

  if (lockedData->IsPitchCorrespondingSize(texelSizeInBytes))
  {
    // perform fast copy
    ptr+=texel_index;
    memcpy(ptr,uint32_array,num_texels*sizeof(unsigned));
  }
  else
  {
    // perform slow copy (pitch does not correspond size)
    for (int i=0; i < num_texels; ++i)
    {
      int offset = lockedData->GetMappedTextureOffset(texel_index+i, texelSizeInBytes) / sizeof(unsigned);
      ptr[offset] = *uint32_array++;
    }
  }

#if SIMUL_WEATHER_LOG_TEXTURE_FILLS
  LogF("SimulWeather - CloudRenderer - cloud texture %d filled.", texture_index);
#endif
}

void SimulCloudRendererDX11::CycleTexturesForward()
{
  swap(_cloudTextures[0], _cloudTextures[1]);
  swap(_cloudTextures[1], _cloudTextures[2]);
}

void SimulCloudRendererDX11::FillIlluminationSequentially(int source_index,int texel_index,int num_texels,const unsigned char *uchar8_array)
{
  if(!num_texels)
    return;

  Assert(_illuminationTexture);
  if (!_illuminationTexture)
    return;

  Ref<TextureSysGPUDX11::LockedData> lockedData = TextureSysGPUDX11::GetLockedData(_illuminationTexture);
  Assert(lockedData);
  if (!lockedData)
    return;

  Assert(lockedData->CheckSize());
  if (!lockedData->CheckSize())
    return;

  unsigned *ptr=(unsigned *)(lockedData->GetData());
  Assert(ptr);
  if (!ptr)
    return;

  unsigned offset[]={16,8,0,24};//*(source_index); // xyzw

  const int texelSizeInBytes = 4;

  if (lockedData->IsPitchCorrespondingSize(texelSizeInBytes))
  {
    // perform fast copy
    ptr+=texel_index;
    for(int i=0;i<num_texels;i++)
    {
      unsigned ui=(unsigned)(*uchar8_array);
      ui<<=offset[source_index];
      unsigned msk=255<<offset[source_index];
      msk=~msk;
      (*ptr)&=msk;
      (*ptr)|=ui;
      uchar8_array++;
      ptr++;
    }
  }
  else
  {
    // perform slow copy
    for(int i=0;i<num_texels;i++)
    {
      int texOffset = lockedData->GetMappedTextureOffset(texel_index+i, texelSizeInBytes) / sizeof(unsigned);

      unsigned ui=(unsigned)(*uchar8_array);
      ui<<=offset[source_index];
      unsigned msk=255<<offset[source_index];
      msk=~msk;
      ptr[texOffset] &=msk;
      ptr[texOffset] |=ui;
      uchar8_array++;
      ptr++;
    }
  }


  // no need to unlock or update texture - it is done when the texture is needed

#if SIMUL_WEATHER_LOG_TEXTURE_FILLS
  LogF("SimulWeather - CloudRenderer - illumination texture filled.");
#endif
}

bool SimulCloudRendererDX11::InitRenderer()
{
  Assert(_engine);
  if (!_engine)
    return false;

  /// check supported texture format
  DXGI_FORMAT formats[] = 
  {
    DXGI_FORMAT_R8G8B8A8_UNORM
  };

  DXGI_FORMAT textureFormat = DXGI_FORMAT_UNKNOWN;

  for (int i=0; i< lenof(formats); ++i)
  {
    UINT support = _engine->GetFormatSupport(formats[i]);
    if ((support & D3D11_FORMAT_SUPPORT_TEXTURE2D)
      && (support & D3D11_FORMAT_SUPPORT_TEXTURE3D)
      && (support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
    {
      textureFormat = formats[i];
      break;
    }
  }

  if (DXGI_FORMAT_UNKNOWN == textureFormat)
  {
    // no appropriate texture format found
    Fail("No appropriate texture format for Simul Weather cloud renderer.")
      return false;
  }

  _textureFormat = textureFormat;

  if (!_noiseTextureFileName.IsEmpty())
    CreateNoiseTexture(_noiseTextureFileName);

  LogF("SimulWeather - CloudRenderer initialized.");
  return true;
}

void SimulCloudRendererDX11::DeInitRenderer()
{
  for (int i=0; i < TextureCount; ++i)
  {
    _cloudTextures[i].Free();
  }
  _illuminationTexture.Free();
  _noiseTexture.Free();
  _noiseTextureCreated.Free();
  _skyLossTexture1.Free();
  _skyLossTexture2.Free();
  _skyInscatterTexture1.Free();
  _skyInscatterTexture2.Free();

  _illuminationTexWidth = _illuminationTexHeight = _illuminationTexDepth = 0;
  cloud_tex_width_x = cloud_tex_length_y = cloud_tex_depth_z = 0;
  _textureFormat = DXGI_FORMAT_UNKNOWN;

  _vertexBuffers.Clear();
  _vertexBuffersCPU.Clear();

  if (cloudKeyframer)
    cloudKeyframer->SetRenderCallback(NULL);

  LogF("SimulWeather - CloudRenderer deinitialized.");
}

void SimulCloudRendererDX11::CreateNoiseTexture(RString fileName)
{
  if (_noiseTexture && fileName == _noiseTextureFileName)
    return; //already created

  _noiseTexture.Free();
  _noiseTextureFileName = fileName;

  Assert(_engine);
  if (!_engine)
    return;

  Assert(!_engine->GetD3DDeviceRec().IsNull());
  if (_engine->GetD3DDeviceRec().IsNull())
    return;

  if (!fileName.IsEmpty())
  {
    // try to load as ordinary texture
    _noiseTexture = GlobLoadTexture(GlobResolveTexture(fileName));
    if (!_noiseTexture)
    {
      RptF("Simul Weather - Cloud Renderer - failed to load texture for noise! (%s)", cc_cast(fileName));
    }
    else
    {
      //_noiseTexture->SetMipmapRange(0,0);
      _engine->TextBank()->UseMipmap(_noiseTexture, 0, 0);
      return; //ok, texture created
    }
  }
  else
  {
    RptF("SimulWeather - Cloud Renderer - noise texture file is not specified!\n");
  }

#define _SIMUL_WEATHER_CREATE_NOISE_TEXTURE 1

#if _SIMUL_WEATHER_CREATE_NOISE_TEXTURE 
  // try to create texture

  if (!_noiseTextureCreated.IsNull())
    return; //already created

  D3D11_TEXTURE2D_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.Width = noise_texture_size;
  desc.Height = noise_texture_size;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = _textureFormat;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Usage = D3D11_USAGE_DYNAMIC;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  desc.MiscFlags = 0;

  HRESULT hr = _engine->CreateTextureSimple2D(desc, _noiseTextureCreated);

  if (FAILED(hr))
  {
    _engine->DDError11("SimulWeather - Creating noise texture failed!", hr);
  }

  void *data = 0;
  hr = _engine->Map(_noiseTextureCreated._texture.GetRef(), 0, D3D11_MAP_WRITE_DISCARD, 0, &data);
  if (FAILED(hr) || !data)
  {
    _engine->DDError11("SimulWeather - Cannot map noise texture:", hr);
    return;
  }

  SetBits8();
  simul::clouds::TextureGenerator::Make2DNoiseTexture((unsigned char *)(data),
    noise_texture_size,noise_texture_frequency,texture_octaves,texture_persistence,0,0);

  _engine->Unmap(_noiseTextureCreated._texture.GetRef(), 0);

  LogF("SimulWeather - CloudRenderer noise texture succesfully created.");
#endif
}

void SimulCloudRendererDX11::CreateCloudTexture()
{
  for (int i=0; i<TextureCount; ++i)
  {
    _cloudTextures[i] = new TextureSysGPUDX11;

    HRESULT hr = _cloudTextures[i]->CreateTextures(cloud_tex_width_x,cloud_tex_length_y,cloud_tex_depth_z , _textureFormat );
    if (FAILED(hr))
    {
      _engine->DDError11("SimulWeather - Creating of cloud texture failed!", hr);
      return;
    }
  }

  LogF("SimulWeather - CloudRenderer cloud textures successfully created.");
}

void SimulCloudRendererDX11::CreateIlluminationTexture()
{
  _illuminationTexture = new TextureSysGPUDX11;
  HRESULT hr = _illuminationTexture->CreateTextures(_illuminationTexWidth, _illuminationTexHeight, _illuminationTexDepth, _textureFormat);

  if (FAILED(hr))
  {
    _engine->DDError11("SimulWeather - Creating of illumination texture failed!", hr);
    return;
  }

  /// fill system memory texture
  Ref<TextureSysGPUDX11::LockedData> lockedData = TextureSysGPUDX11::GetLockedData(_illuminationTexture);
  Assert(lockedData);
  if (!lockedData)
    return;

  void *data = lockedData->GetData();
  Assert(data);
  if (!data)
    return;

  memset(data,0,4*_illuminationTexWidth*_illuminationTexHeight*_illuminationTexDepth);

  LogF("SimulWeather - CloudRenderer - illumination textures successfully created.");
}

void SimulCloudRendererDX11::SetLossTextures(Ref<TextureSysGPUDX11>& t1,Ref<TextureSysGPUDX11>& t2)
{
  _skyLossTexture1=t1;
  _skyLossTexture2=t2;
}

void SimulCloudRendererDX11::SetInscatterTextures(Ref<TextureSysGPUDX11>& t1, Ref<TextureSysGPUDX11>& t2)
{
  _skyInscatterTexture1=t1;
  _skyInscatterTexture2=t2;
}

inline void SetupD3DVector(D3DXVECTOR4& v, const simul::math::Vector3 &from)
{
  v[0] = from.x;
  v[1] = from.y;
  v[2] = from.z;
  v[3] = from.w;
}

inline void SetupD3DVector(D3DXVECTOR4& v, const simul::sky::float4 &from)
{
  v[0] = from.x;
  v[1] = from.y;
  v[2] = from.z;
  v[3] = from.w;
}

inline void SetupD3DVector(D3DXVECTOR4& v, const Vector3P& from)
{
  v[0] = from[0];
  v[1] = from[1];
  v[2] = from[2];
}

void SimulCloudRendererDX11::SetupShaderConstants(int cb, bool cubemap)
{
  Assert(GScene);
  if (!GScene)
    return;

  Camera *camera = GScene->GetCamera();
  Assert(camera);
  if (!camera)
    return;    

  Assert(_engine);
  if (!_engine)
    return;

  // setup matrices
  PositionRender modelToWorld;
  modelToWorld.position.SetIdentity();
  modelToWorld.camSpace = false;

  _engine->SetBias(cb, 0);
  _engine->SetupWorldViewProjCam(cb, modelToWorld);

  // VS parameters (see VSSimulWeatherClouds)
  static const int VSParsSize = 5;
  D3DXVECTOR4 VSPars[VSParsSize];

  D3DXVECTOR4& illuminationOrigin = VSPars[0]; //ok
  float& altitudeTexCoord = VSPars[0][3]; //ok
  D3DXVECTOR4& illuminationScales = VSPars[1]; //ok
  float& maxFadeDistanceMetres = VSPars[1][3]; //ok
  D3DXVECTOR4& fractalScaleVS = VSPars[2]; //ok
  D3DXVECTOR4& eyePosition = VSPars[3]; //ok
  D3DXVECTOR4& fogPars = VSPars[4]; //ok

  // PS parameters (see PSSimulWeatherCloudsWithLightning)
  static const int PSParsSize = 8;
  D3DXVECTOR4 PSPars[PSParsSize];

  float& hazeEccentricity = PSPars[0][0]; //ok
  float& interp = PSPars[0][1]; //ok
  float& cloudEccentricity = PSPars[0][2]; //ok
  float& alphaSharpness = PSPars[0][3]; //ok
  D3DXVECTOR4& mieRayleighRatio = PSPars[1]; //ok
  D3DXVECTOR4& fractalScalePS = PSPars[2]; //ok
  D3DXVECTOR4& lightResponse = PSPars[3]; //ok
  D3DXVECTOR4& lightningMultipliers = PSPars[4]; //ok
  D3DXVECTOR4& lightningColour  = PSPars[5]; //ok
  D3DXVECTOR4& skylightColour = PSPars[6]; //ok
  D3DXVECTOR4& lightDir = PSPars[7]; //ok
  float& fadeInterp = PSPars[7][3]; //ok


  static const float lightningEffectOnCloud=20.f;

  if (enable_lightning && lightningRenderInterface)
  {
    static float bb=.1f;
    simul::sky::float4 lightning_multipliers;
    lightningColour=lightningRenderInterface->GetLightningColour();
    for(unsigned i=0;i<4;i++)
    {
      if(i<lightningRenderInterface->GetNumLightSources())
        lightningMultipliers[i]=bb*lightningRenderInterface->GetLightSourceBrightness(i);
      else 
        lightningMultipliers[i]=0;
    }
    lightningColour.w=lightningEffectOnCloud;

    simul::math::Vector3 light_X1=lightningRenderInterface->GetIlluminationOrigin();
    simul::math::Vector3 light_DX=lightningRenderInterface->GetIlluminationScales();

    SetupD3DVector(illuminationOrigin, light_X1);
    SetupD3DVector(illuminationScales, light_DX);
  }

  altitudeTexCoord = altitude_tex_coord;
  maxFadeDistanceMetres = _maxFadeDistanceMetres;

  Assert(helper);
  if (!helper)
    return;

  simul::sky::float4 fractalScales=(cubemap?0.f:1.f)*helper->GetFractalScales(cloudInterface);
  SetupD3DVector(fractalScaleVS, fractalScales);
  SetupD3DVector(fractalScalePS, fractalScales);

  SetupD3DVector(eyePosition, camera->Position());

  Assert(skyInterface);
  if (!skyInterface)
    return;

  hazeEccentricity = skyInterface->GetMieEccentricity();

  Assert(cloudKeyframer);
  if (!cloudKeyframer)
    return;

  interp = cloudKeyframer->GetInterpolation();

  Assert(cloudInterface);
  if (!cloudInterface)
    return;

  cloudEccentricity = cloudInterface->GetMieAsymmetry();
  alphaSharpness = cloudInterface->GetAlphaSharpness();

  SetupD3DVector(mieRayleighRatio, skyInterface->GetMieRayleighRatio());

  simul::sky::float4 light_response(_directLightCoef*cloudInterface->GetLightResponse(),
    _indirectLightCoef*cloudInterface->GetSecondaryLightResponse(),
    0,
    0);

  simul::sky::float4 sun_dir=skyInterface->GetDirectionToLight();
  swap(sun_dir.y,sun_dir.z); //Arma has y as vertical axis

  // Get the overall ambient light at this altitude, and multiply it by the cloud's ambient response.
  float altKM = camera->Position()[1] * 0.001f;
  simul::sky::float4 sky_light_colour=skyInterface->GetAmbientLight(altKM)*cloudInterface->GetAmbientLightResponse();

  // Get the sunlight at this altitude:
  simul::sky::float4 sunlight=skyInterface->GetLocalIrradiance(altKM);

  SetupD3DVector(lightResponse, light_response);
  SetupD3DVector(skylightColour, sky_light_colour);
  SetupD3DVector(lightDir, sun_dir);

  fadeInterp = fade_interp;

  // setup fog params
  fogPars.x = _fogStart;
  fogPars.y = 1.0f;
  if (_fogEnd - _fogStart > 0)
  {
    fogPars.y = 1.0f/(_fogEnd - _fogStart);
  }

  // setup constants
  _engine->SetVertexShaderConstantF(cb, DX11::VSC_SimulWeatherPars, (float*)VSPars, VSParsSize);
  _engine->SetPixelShaderConstantF(cb, DX11::PSC_SimulWeatherPars, (float*)PSPars, PSParsSize);

  // setup cloud light modifier
  Color cloudColorModifier = _cloudLighting * _engine->GetAccomodateEye().Brightness() * _engine->_modColor.Brightness();
  _engine->SetPixelShaderConstantF(cb, PSC_AE, cloudColorModifier.GetArray(), 1);
}

void SimulCloudRendererDX11::SetupTextures(int cb)
{
  Assert(_engine);
  if (!_engine)
    return;

  for (int i=0; i<8; ++i)
  {
    // Disable any in-texture gamma-correction that might be lingering from some other bit of rendering:
    _engine->D3DSetSamplerState(cb, i, D3DSAMP_SRGBTEXTURE_OLD, 0);

    _engine->D3DSetSamplerState(cb, i, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
    _engine->D3DSetSamplerState(cb, i, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
    _engine->D3DSetSamplerState(cb, i, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);
  }

  // setup textures
  bool wrap = cloudInterface ? cloudInterface->GetWrap() : false;
  DWORD wrapMode = wrap? D3DTADDRESS_WRAP_OLD : D3DTADDRESS_CLAMP_OLD;

  // GetGPUTex also automatically updates GPU texture, so it is ready when we want to render
  if (_cloudTextures[0])
    _engine->SetTexture(cb, 0, _cloudTextures[0]->GetSurface().GetResourceView(false));
  else
    _engine->SetTexture(cb, 0, 0);
  _engine->D3DSetSamplerState(cb, 0, D3DSAMP_ADDRESSU_OLD, wrapMode);
  _engine->D3DSetSamplerState(cb, 0, D3DSAMP_ADDRESSV_OLD, wrapMode);
  _engine->D3DSetSamplerState(cb, 0, D3DSAMP_ADDRESSW_OLD, D3DTADDRESS_CLAMP_OLD);

  if (_cloudTextures[1])
    _engine->SetTexture(cb, 1, _cloudTextures[1]->GetSurface().GetResourceView(false));
  else
    _engine->SetTexture(cb, 1, 0);

  _engine->D3DSetSamplerState(cb, 1, D3DSAMP_ADDRESSU_OLD, wrapMode);
  _engine->D3DSetSamplerState(cb, 1, D3DSAMP_ADDRESSV_OLD, wrapMode);
  _engine->D3DSetSamplerState(cb, 1, D3DSAMP_ADDRESSW_OLD, D3DTADDRESS_CLAMP_OLD);

  if (_noiseTexture)
  {
    _engine->TextBank()->UseMipmap(_noiseTexture, 0, 0);
    TextureD3D11* textureD11 = (TextureD3D11*)_noiseTexture.GetRef();
    _engine->SetTexture(cb, 2, textureD11->GetHandle(false));
  }
  else if (!_noiseTextureCreated.IsNull())
  {
    _engine->SetTexture(cb, 2, _noiseTextureCreated.GetResourceView(false));
  }
  _engine->D3DSetSamplerState(cb, 2, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_WRAP_OLD);
  _engine->D3DSetSamplerState(cb, 2, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_WRAP_OLD);
  _engine->D3DSetSamplerState(cb, 2, D3DSAMP_ADDRESSW_OLD, D3DTADDRESS_WRAP_OLD);

  if (_illuminationTexture)
    _engine->SetTexture(cb, 3, _illuminationTexture->GetSurface().GetResourceView(false));
  else
    _engine->SetTexture(cb, 3, 0);

  _engine->D3DSetSamplerState(cb, 3, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_BORDER_OLD);
  _engine->D3DSetSamplerState(cb, 3, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_BORDER_OLD);
  _engine->D3DSetSamplerState(cb, 3, D3DSAMP_ADDRESSW_OLD, D3DTADDRESS_BORDER_OLD);

  if(fade_mode==FRAGMENT)
  {
    if (_skyLossTexture1)
      _engine->SetTexture(cb, 4, _skyLossTexture1->GetSurface().GetResourceView(false));
    else
      _engine->SetTexture(cb, 4, 0);

    if (_skyLossTexture2)
      _engine->SetTexture(cb, 5, _skyLossTexture2->GetSurface().GetResourceView(false));
    else
      _engine->SetTexture(cb, 5, 0);


    if (_skyInscatterTexture1)
      _engine->SetTexture(cb, 6, _skyInscatterTexture1->GetSurface().GetResourceView(false));
    else
      _engine->SetTexture(cb, 6, 0);

    if (_skyInscatterTexture2)
      _engine->SetTexture(cb, 7, _skyInscatterTexture2->GetSurface().GetResourceView(false));
    else
      _engine->SetTexture(cb, 7, 0);

    for (int i=4; i<8; ++i)
    {
      _engine->D3DSetSamplerState(cb, i, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
      _engine->D3DSetSamplerState(cb, i, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_MIRROR_OLD);
      _engine->D3DSetSamplerState(cb, i, D3DSAMP_ADDRESSW_OLD, D3DTADDRESS_CLAMP_OLD);
    }
  }      
}

void SimulCloudRendererDX11::SetupRenderStates(int cb)
{
  Assert(_engine);
  if (!_engine)
    return;
 
  _engine->D3DSetRenderState(cb,  D3DRS_ZENABLE_OLD, TRUE);
  _engine->D3DSetRenderState(cb,  D3DRS_ZFUNC_OLD, D3DCMP_LESSEQUAL_OLD);
  _engine->D3DSetRenderState(cb,  D3DRS_ZWRITEENABLE_OLD, FALSE);
  _engine->D3DSetRenderState(cb,  D3DRS_DEPTHBIAS_OLD, 0);
  float zero = 0.0f;
  DWORD dwZero = *(const DWORD*)&zero;
  _engine->D3DSetRenderState(cb, D3DRS_SLOPESCALEDEPTHBIAS_OLD, dwZero);

  _engine->D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | 
    D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);

  _engine->D3DSetRenderState(cb, D3DRS_ALPHABLENDENABLE_OLD, TRUE);
  _engine->D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE_OLD, FALSE);
  _engine->D3DSetRenderState(cb, D3DRS_CULLMODE_OLD, D3DCULL_NONE_OLD);
  _engine->D3DSetRenderState(cb, D3DRS_FILLMODE_OLD, D3DFILL_SOLID_OLD);

  _engine->D3DSetRenderState(cb, D3DRS_SRCBLEND_OLD,D3DBLEND_ONE_OLD);
  _engine->D3DSetRenderState(cb, D3DRS_DESTBLEND_OLD,D3DBLEND_INVSRCALPHA_OLD);
  _engine->D3DSetRenderState(cb, D3DRS_BLENDOP_OLD, D3DBLENDOP_ADD_OLD);

  // blending for alpha:
  _engine->D3DSetRenderState(cb, D3DRS_SEPARATEALPHABLENDENABLE_OLD, TRUE);
  _engine->D3DSetRenderState(cb, D3DRS_SRCBLENDALPHA_OLD,D3DBLEND_ZERO_OLD);
  _engine->D3DSetRenderState(cb, D3DRS_DESTBLENDALPHA_OLD,D3DBLEND_INVSRCALPHA_OLD);
  _engine->D3DSetRenderState(cb, D3DRS_BLENDOPALPHA_OLD, D3DBLENDOP_ADD_OLD);

  if (_engine->_useAToC)
    _engine->D3DEnableAlphaToCoverage(cb, false);
}

void SimulCloudRendererDX11::RestoreRenderStates(int cb)
{
  Assert(_engine);
  if (!_engine)
    return;

  // we have to restore D3DRS_SEPARATEALPHABLENDENABLE
  _engine->D3DSetRenderState(cb, D3DRS_SEPARATEALPHABLENDENABLE_OLD, FALSE);

}

void SimulCloudRendererDX11::SetupShaders(int cb)
{
  Assert(_engine);
  if (!_engine)
    return;

  ID3D11PixelShader *pixelShader = NULL;
  VertexShaderInfo *vertexShader = NULL;

  if (FRAGMENT == fade_mode)
  {
    vertexShader = &_engine->_vertexShaderSpecial[VSSSimulWeatherClouds];
  }
  else
  {
    vertexShader = &_engine->_vertexShaderSpecial[VSSSimulWeatherCloudsCPU];
  }

  if (enable_lightning && lightningRenderInterface)
  {
    if (FRAGMENT == fade_mode)
    {
      pixelShader = _engine->_pixelShader[PSSimulWeatherCloudsWithLightning][PSTBasic];
    }
    else
    {
      pixelShader = _engine->_pixelShader[PSSimulWeatherCloudsWithLightningCPU][PSTBasic];
    }
  }
  else
  {
    if (FRAGMENT == fade_mode)
    {
      pixelShader = _engine->_pixelShader[PSSimulWeatherClouds][PSTBasic];
    }
    else
    {
      pixelShader = _engine->_pixelShader[PSSimulWeatherCloudsCPU][PSTBasic];
    }
  }

  Assert(pixelShader);
  if (!pixelShader)
  {
    LogF("SimulWeather - CloudRenderer - no pixel shader found.");
  }
  Assert(vertexShader);
  if (!vertexShader)
  {
    LogF("SimulWeather - CloudRenderer - no vertex shader found.");
  }

  _engine->SetVertexShader(cb, *vertexShader);
  _engine->SetPixelShader(cb, pixelShader);
}

void SimulCloudRendererDX11::Render(int cb, bool cubemap)
{
  Assert(cloudKeyframer);
  if (!cloudKeyframer)
    return;

  enable_lightning = false;

  Assert(_engine);
  if (!_engine)
    return;

  if (_engine->ResetNeeded()) return;
  _engine->SwitchTL(cb,EngineDD11::TLEnabled);

  SetupShaderConstants(cb, cubemap);
  SetupTextures(cb);
  SetupRenderStates(cb);
  SetupShaders(cb);

  RenderGeometry(cb, cubemap);  

  RestoreRenderStates(cb);
}

void SimulCloudRendererDX11::RenderGeometryVertexBuffer(int cb, bool cubemap, bool async)
{
  // protect buffer creation with critical section
  ScopeLockSection lock(_renderVBCritSection);

  PROFILE_SCOPE_EX(swRdGeomVB, SW);

  Assert(GScene);
  if (!GScene)
    return;

  Camera* camera = GScene->GetCamera();
  Assert(camera);
  if (!camera)
    return;

  Assert(_engine);
  if (!_engine)
    return;

  /// create data
  void *data = 0;
  int vertexSize = 0;
  if(fade_mode!=CPU)
  {
    _vertices.Resize(MaxVertices);
    data = (void*)_vertices.Data();
    vertexSize = sizeof(Vertex_t);
  }
  else
  {
    _cpuFadeVertices.Resize(MaxVertices);
    data = (void*)_cpuFadeVertices.Data();
    vertexSize = sizeof(CPUFadeVertex_t);
  }

  int numVertices = FillVertices(this, data, MaxVertices, vertexSize, *camera, camera->ProjectionNormal(), cubemap, fade_mode==CPU, enable_lightning);
  if (numVertices <= 2)
    return; //nothing to do...

  // get free vertex buffer
  Ref<SimulCloudVertexBuffer11> buffer = GetFreeVertexBuffer(numVertices);
  if (!buffer)
    return; //no free buffer

  {
    PROFILE_SCOPE_EX(swGetBuf, SW);

    Assert(buffer->GetBuffer());
    if (!buffer->GetBuffer())
      return;
  }

  // fill buffer
  int startPos = buffer->FillVertices(data, vertexSize, numVertices);

  if (startPos < 0 || startPos >= buffer->GetMaxCount())
    return; //something went wrong...

  // size really does not matter here
  _engine->SetStreamSource(cb, VertexStaticData11::BufferFuture(buffer->GetBuffer()),vertexSize, buffer->GetMaxCount()); 
  _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, startPos, numVertices-2);
}

void SimulCloudRendererDX11::RenderGeometry(int cb, bool cubemap)
{
  PROFILE_SCOPE_EX(swRdGeom, SW);

  switch (GetRenderMode())
  {
  case RMPrimitiveUP:
  case RMVertexBuffer:
    RenderGeometryVertexBuffer(cb, cubemap, false);
    break;
  case RMVertexBufferAsync:
    RenderGeometryVertexBuffer(cb, cubemap, true);
    break;
  default:
    LogF("SimulWeather - CloudRenderer - invalid render mode for simul weather clouds: %d", (int)GetRenderMode());
    break;
  }
}

/// returns free vertex buffer that we can use for drawing
Ref<SimulCloudVertexBuffer11 > SimulCloudRendererDX11::GetFreeVertexBuffer(int numVertices)
{
  PROFILE_SCOPE_EX(swGFreeVB, SW);

  bool useCPUFade = (fade_mode == CPU);

  TVertexBufferList* vertexBuffers = 0;
  int vertexSize = 0;

  if (useCPUFade)
  {
    vertexSize = sizeof(CPUFadeVertex_t);
    vertexBuffers = &_vertexBuffersCPU;
  }
  else
  {
    vertexSize = sizeof(Vertex_t);
    vertexBuffers = &_vertexBuffers;
  }

  Assert(vertexBuffers);
  if (!vertexBuffers)
    return NULL;

  // try to find free VB
  for (int i=0; i<vertexBuffers->Size(); ++i)
  {
    Ref<SimulCloudVertexBuffer11 > buffer = (*vertexBuffers)[i];

    if (!buffer)
      continue;

    if (buffer->IsBusy())
    {
      // busy - lets check if there is space for our vertices
      if (buffer->GetNumFree() >= numVertices)     
      {
        return buffer; // we still have a space to write
      }
    }
    else
    {
      return buffer;
    }
  }

  // create new
  if (vertexBuffers->Size() < MaxVertexBuffers)
  {
    SimulCloudVertexBuffer11* buffer = new SimulCloudVertexBuffer11(MaxVBVertices, vertexSize);
    buffer->CreateBuffer();
    vertexBuffers->Add(buffer);
    return buffer;
  }
  else
  {
    LogF("SimulWeather - CloudRenderer - cannot find non-busy vertex buffer for cloud rendering. Clouds will not be rendered.");
    return NULL;
  }
}

void **SimulCloudRendererDX11::GetCloudTextures()
{
  for (int i=0; i<TextureCount; ++i)
  {
    if (_cloudTextures[i])
      _cloudTexturesPtrs[i] = _cloudTextures[i]->GetSurfaceNoUpdate()._texture3D.GetRef();
    else
      _cloudTexturesPtrs[i] = 0;
  }

  return (void **)_cloudTexturesPtrs;
}

}//namespace DX11

#endif //#if _ARMA3_SIMUL_WEATHER && _ENABLE_DX11
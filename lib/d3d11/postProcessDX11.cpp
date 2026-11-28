#include "../wpch.hpp"

#if _ENABLE_DX11

#if !defined _SERVER
#include "postProcessDX11.hpp"
#include "engdd11.hpp"
#include "txtd3d11.hpp"
#include "../textbank.hpp"
#include "../diagModes.hpp"
#include <Es/Strings/bString.hpp>
#include <Es/Common/mathUtils.hpp>
#include <DirectXPackedVector.h>
#include "../Shape/shape.hpp"
#include "../appFrameExt.hpp"
#include "../global.hpp"
#include <El/Math/mathStore.hpp>
#include "../keyInput.hpp"
#include "../dikCodes.h"
#include "ShaderSources_4_0/common.h"
#include "../scene.hpp"
#include "../camera.hpp"

//because rain needs to cooperate with camera inside vehicle
#include "../world.hpp"
//...and sealevel
#include "../landscape.hpp"

#include <El/Evaluator/express.hpp>
#include <El/Modules/modules.hpp>
#include <El/Common/randomGen.hpp>

#if _ENABLE_COMPILED_SHADER_CACHE
#include "ShaderCompileCacheDX11.h"
#endif

extern bool ForceTIAlive;
extern bool ForceTIMovement;
extern bool ForceTIMetabolism;

namespace DX11
{

EngineDD11 *EngineDD11::PostProcess::_engine;

static void ConvertFloat32ToHalfArray(DirectX::PackedVector::HALF *output, const float *input, int count)
{
  for (int i = 0; i < count; ++i)
    output[i] = DirectX::PackedVector::XMConvertFloatToHalf(input[i]);
}

static bool CreateTextureHelper(MyD3DDevice& dev, UINT width, UINT height, UINT mipLevels, DXGI_FORMAT format, 
                                D3D11_SUBRESOURCE_DATA *initData, SurfaceDataD3D11& result)
{
  D3D11_TEXTURE2D_DESC desc;
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = mipLevels;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  desc.CPUAccessFlags = 0;
  desc.MiscFlags = 0;

  HRESULT hr = dev->CreateTexture2D(&desc, initData, result._texture.Init());
  if (FAILED(hr))
  {
    GEngineDD11->DDError11("Failed to create texture", hr);
    return false;
  }
  else
  {
    // create view
    hr = dev->CreateShaderResourceView(result._texture, 0, result._resView.Init());
    if (FAILED(hr))
    {
      GEngineDD11->DDError11("Failed to create shader resource view", hr);
      return false;
    }
  }

  return true;
}

static bool CreateTextureHelper(MyD3DDevice& dev, UINT width, UINT height, DXGI_FORMAT format, 
                                const void *initData, UINT initDataPitch, SurfaceDataD3D11& result)
{
  D3D11_SUBRESOURCE_DATA srData;
  srData.pSysMem = initData;
  srData.SysMemPitch = initDataPitch;
  srData.SysMemSlicePitch = 0;

  return CreateTextureHelper(dev, width, height, 1, format, initData? &srData : 0, result);
}

void EngineDD11::PostProcess::Prepare()
{
  const int cb = -1;
  _engine->FlushAndFreeAllQueues();
  // we want all render states to be set as needed
  // postprocess coordinates are given in 3D, but there is no camera space
  _engine->SwitchTL(cb,TLViewspace);

  // Set the src blend and dest blend somehow (they are not going to be set in the DoPrepareTriangle and the temporary setting returning would set a undefined value)
  _engine->D3DSetRenderState(cb, D3DRS_SRCBLEND_OLD,D3DBLEND_ONE_OLD);
  _engine->D3DSetRenderState(cb, D3DRS_DESTBLEND_OLD,D3DBLEND_ONE_OLD);

  // Set standard render states (it is possible to change it in every descendant, but
  // it must return it also back)
  // No alpha blending - we will do everything in the pixel shader
  // Use point filter
  EngineShapeProperties prop;
  _engine->DoPrepareTriangle(cb,NULL,TexMaterialLODInfo(NULL, 0),0,NoZBuf|NoZWrite|ClampU|ClampV|FilterPoint,prop);
  _engine->DoShadowsStatesSettings(cb,SS_Transparent,false);
}

void EngineDD11::PostProcess::Finish()
{
  int cb = -1;
  // Restore usual rendering settings
  _engine->SwitchTL(cb, TLDisabled);
  
  #if _DEBUG || _PROFILE
    if (!_engine->_hlRecording && Glob.config._renderThread==Config::ThreadingEmulated)
    {
      _engine->KickOffCBDebug();
    }
  #endif
}

bool EngineDD11::PostProcess::CompilePixelShader(
  const LazyLoadFile &hlsl, const char *psName, ComRef<ID3D11PixelShader> &ps,
  bool fullPrec
) const
{
  // define macros for the shaders
  D3DXMacroList defines;
  _engine->CreateBasicDefines(defines);

  D3DXCompileShader_Result result;  

  DWORD flags = fullPrec ? 0 : D3D10_SHADER_PARTIAL_PRECISION;

  flags |= D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY;

  HRESULT hr = D3DX11CompileShader_Cached
    (
    hlsl,
    0,
    defines.Data(),
    &_engine->_hlslInclude,
    psName,
    _engine->_caps._psVersion,
    flags,
    0,
    NULL,
    NULL,
    result
    );
  
  if (FAILED(hr))
  {
    LogF("Error: %x in D3DX11CompileFromMemory (%s).", hr, psName);
    if (result.errorMsgs.NotNull())
    {
      EngineDD11::ReportShaderErrors(result.errorMsgs);
    }
    ErrorMessage("Error: %s shader creation failed.",psName);
    return false;
  };

  const DWORD *psData = (const DWORD *)result.shader->GetBufferPointer();
  DWORD psDataSize = result.shader->GetBufferSize();

  hr = _engine->GetD3DDeviceNonRec()->CreatePixelShader(psData, psDataSize, NULL, ps.Init());
  if (FAILED(hr))
  {
    LogF("Error: %s shader creation failed.",psName);
    return false;
  }

  return true;
}

bool EngineDD11::PostProcess::CompileVertexShader(
  const LazyLoadFile &hlsl, const char *vsName, VertexShaderInfo &vs, const D3D11_INPUT_ELEMENT_DESC *iDesc, int numDescElements
) const
{
  // define macros for the shaders
  D3DXMacroList defines;
  _engine->CreateBasicDefines(defines);

  D3DXCompileShader_Result result;  

  DWORD flags = DebugVS ? D3D10_SHADER_SKIP_OPTIMIZATION|D3D10_SHADER_DEBUG : 0;

  flags |= D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY;

  HRESULT hr = D3DX11CompileShader_Cached
    (
    hlsl,
    0,
    defines.Data(),
    &_engine->_hlslInclude,
    vsName,
    _engine->_caps._vsVersion,
    flags,
    0,
    NULL,
    NULL,
    result
    );

  if (FAILED(hr))
  {
    LogF("Error: %x in D3DX11CompileFromMemory (%s).", hr, vsName);
    if (result.errorMsgs.NotNull())
    {
      EngineDD11::ReportShaderErrors(result.errorMsgs);
    }
    ErrorMessage("Error: %s shader creation failed.",vsName);
    return false;
  };

  const DWORD *vsData = (const DWORD *)result.shader->GetBufferPointer();
  DWORD vsDataSize = result.shader->GetBufferSize();

  hr = _engine->GetD3DDeviceNonRec()->CreateVertexShader(vsData, vsDataSize, NULL, vs._shader.Init());
  if (FAILED(hr))
  {
    LogF("Error: %s shader creation failed.",vsName);
    return false;
  }

  hr = _engine->GetD3DDeviceNonRec()->CreateInputLayout(
    iDesc, numDescElements,
    vsData, vsDataSize, 
    vs._inputLayout.Init());

  if (FAILED(hr))
  {
    RptF("Error: Creating input layout for shader %s failed", vsName);
    ErrorMessage("Error: Creating input layout for shader %s failed", vsName);      
    return false;
  }

  return true;  
}

//////////////////////////////////////////////////////////////////////////
void EngineDD11::PPSimple::Prepare()
{
  base::Prepare();

  const int cb = -1;
  // Set the stream source
  _engine->SetStreamSource(cb,_vb, sizeof(Vertex), 4);

  // Set the vertex shader
  _engine->SetVertexShader(cb,_vs);

  // Set the pixel shader
  _engine->SetPixelShader(cb,_ps);
}
bool EngineDD11::PPSimple::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, const char *vsName)
{
  if (!base::Init(hlsl,vs)) return false;

  // setup initial data
  Vertex v[4];
  float offsetU = 0.0f;
  float offsetV = 0.0f;
  v[0].pos = Vector3P(-0.5, -0.5, 0.5); v[0].t0.u = 0 + offsetU; v[0].t0.v = 1 + offsetV;
  v[1].pos = Vector3P(-0.5,  0.5, 0.5); v[1].t0.u = 0 + offsetU; v[1].t0.v = 0 + offsetV;
  v[2].pos = Vector3P( 0.5, -0.5, 0.5); v[2].t0.u = 1 + offsetU; v[2].t0.v = 1 + offsetV;
  v[3].pos = Vector3P( 0.5,  0.5, 0.5); v[3].t0.u = 1 + offsetU; v[3].t0.v = 0 + offsetV;

  // Create the VB
  ComRef<IDirect3DVertexBuffer9Old> vb;

  D3D11_BUFFER_DESC bd;
  bd.Usage = D3D11_USAGE_DEFAULT;
  bd.ByteWidth = sizeof(Vertex) * 4;
  bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = 0;
  bd.MiscFlags = 0;

  D3D11_SUBRESOURCE_DATA srData;
  srData.pSysMem = v;
  srData.SysMemPitch = 0;
  srData.SysMemSlicePitch = 0;

  HRESULT hr = _engine->GetD3DDeviceNonRec()->CreateBuffer(&bd, &srData, vb.Init());
  if (FAILED(hr))
  {
    LogF("Error: Vertex buffer creation failed.");
    return false;
  }

  _vb = VertexStaticData11::BufferFuture(vb);

  // Create the VD
  const D3D11_INPUT_ELEMENT_DESC vertexDecl[] =
  {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  // create vertex shader
  if (!CompileVertexShader(vs,vsName,_vs, vertexDecl, sizeof(vertexDecl)/sizeof(vertexDecl[0])))
  {
    return false;
  }

  return true;
}

bool EngineDD11::PPFinal::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs)
{
  if (!base::Init(hlsl,vs)) return false;

  if (!CompilePixelShader(hlsl,"PSPostProcessCopy", _ps))
  {
    return false;
  }
  return true;
}


void EngineDD11::PPFinal::Do(bool isLast)
{
  Do(isLast,true);
}

void EngineDD11::PPFinal::Do(bool isLast,  bool forceNoSRGBOnRead)
{
  const int cb = -1;
  
  // not much to do if current render target is back-buffer (common situation, happens whenever we were doing 3D rendering)
  if (_engine->_currRenderTarget._resource!=_engine->_backBuffer._resource)
  {
    PROFILE_SCOPE_GRF_EX(ppFin,pp,SCOPE_COLOR_YELLOW);

    
    bool inScene = _engine->_d3dFrameOpen;
    if (!inScene) _engine->D3DBeginScene();
    // StretchRect not possible here?
    // Prepare drawing
    Prepare();

    // Set temporary render states
    SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, 
      D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);

    #ifdef _XBOX
      _engine->ResolveRenderTarget();
    #endif
    // Remember the current render target (to be used as a texture later)
    TextureHandle11* rt = _engine->GetCurrRenderTargetAsTexture();
    DoAssert(rt);

    // Use the back buffer as render target
    _engine->SwitchToBackBufferRenderTarget();

    // Use the texture
    SetTextureTemp tRenderTarget(0, rt);

    bool rescale = _engine->_wRT!=_engine->_h || _engine->_hRT!=_engine->_h;
    
    // note: stage0 is set to point sampling by default (see FilterPoint in EngineDD11::PostProcess::Prepare)
    D3DTEXTUREFILTERTYPE_OLD filter = rescale ? D3DTEXF_LINEAR_OLD : D3DTEXF_POINT_OLD;
    SetSamplerStateTemp sampFilterMag0(0, D3DSAMP_MAGFILTER_OLD, filter);
    SetSamplerStateTemp sampFilterMin0(0, D3DSAMP_MINFILTER_OLD, filter);
    SetSamplerStateTemp sampFilterMip0(0, D3DSAMP_MIPFILTER_OLD, filter);

#ifndef _X360SHADERGEN
    BOOL rtIsSRGB = _engine->_caps._rtIsSRGB && _engine->_caps._canReadSRGB && !forceNoSRGBOnRead;
    SetSamplerStateTemp tempSRGB0(0, D3DSAMP_SRGBTEXTURE_OLD, rtIsSRGB);
#endif

    
    // DX11 TODO remove or repair 
    //PushVertexShaderConstant pushC0(0);
    //PushVertexShaderConstant pushC1(1);
    //PushVertexShaderConstant pushC2(2);
    //PushVertexShaderConstant pushC3(3);
    //PushVertexShaderConstant pushC4(4);
    //PushVertexShaderConstant pushC5(5);
    //PushVertexShaderConstant pushC6(6);
    //PushVertexShaderConstant pushC7(7);
    //PushVertexShaderConstant pushC8(8);
    
    // Prepare the UV coordinates offset coefficients
    static const float baseU[4] = {0, 0, 1, 1};
    static const float baseV[4] = {1, 0, 1, 0};

    // older version, well tuned
    const float tgtOffsetU = 0;
    const float tgtOffsetV = 0;
    const float srcOffsetU = 0;  
    const float srcOffsetV = 0;  

    //const RenderTarget& glow = _engine->_pp._bloom->GetBlurredTexture();
    //D3DSURFACE_DESC glowDesc;
    //glow->GetLevelDesc(0, &glowDesc);

    float U[4][4];
    float V[4][4];
    for (int vertex = 0; vertex < 4; vertex++)
    {
      // Texture 0 - source image
      U[vertex][0] = baseU[vertex] + srcOffsetU;
      V[vertex][0] = baseV[vertex] + srcOffsetV;
      // Texture 1
      U[vertex][1] = 0;
      V[vertex][1] = 0;
      // Texture 2
      U[vertex][2] = 0;
      V[vertex][2] = 0;
      // Texture 3
      U[vertex][3] = 0;
      V[vertex][3] = 0;
    }
    const float posOffset[4] = {tgtOffsetU,tgtOffsetV,0,0};
    _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeU, U[0], 4);
    _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeV, V[0], 4);
    _engine->SetVertexShaderConstantF(cb, VSC_PPEdgePosOffset, posOffset, 1);
    _engine->SetPixelShader(cb,_ps);


    // WIP: RESCALER remove _vsCustomEdge from PPGlow (always use point sampling there)

    // Drawing
    _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);
    
    if (!inScene) _engine->D3DEndScene();
  }
  Finish();
}

bool EngineDD11::PPDiagDepthBuffer::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs)
{
  if (!base::Init(hlsl,vs)) return false;

  if (!CompilePixelShader(hlsl,"PSPostProcessDiagDepthBuffer", _ps))
  {
    return false;
  }
  return true;
}


void EngineDD11::PPDiagDepthBuffer::Do(bool isLast)
{
  const int cb = -1;

  PROFILE_SCOPE_GRF_EX(ppDDB,pp,SCOPE_COLOR_YELLOW);

  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();

  // Prepare drawing
  Prepare();

  // Set temporary render states
  SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);

#ifdef _XBOX
  _engine->ResolveRenderTarget();
#endif
  // Use the depth texture
  SetTextureTemp tRenderTarget(0, _engine->GetDepthAsTexture());
  SetSamplerStateTemp sampFilterMag0(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);

  // DX11 TODO - remove or repair
  //PushVertexShaderConstant pushC0(0);
  //PushVertexShaderConstant pushC1(1);
  //PushVertexShaderConstant pushC2(2);
  //PushVertexShaderConstant pushC3(3);
  //PushVertexShaderConstant pushC4(4);
  //PushVertexShaderConstant pushC5(5);
  //PushVertexShaderConstant pushC6(6);
  //PushVertexShaderConstant pushC7(7);
  //PushVertexShaderConstant pushC8(8);

  // Prepare the UV coordinates offset coefficients
  static const float baseU[4] = {0, 0, 1, 1};
  static const float baseV[4] = {1, 0, 1, 0};

  // older version, well tuned
  const float tgtOffsetU = 0;
  const float tgtOffsetV = 0;
  const float srcOffsetU = 0.5f / _engine->_wRT;
  const float srcOffsetV = 0.5f / _engine->_hRT;

  float U[4][4];
  float V[4][4];
  for (int vertex = 0; vertex < 4; vertex++)
  {
    // Texture 0 - source image
    U[vertex][0] = baseU[vertex] + srcOffsetU;
    V[vertex][0] = baseV[vertex] + srcOffsetV;
    // Texture 1
    U[vertex][1] = 0;
    V[vertex][1] = 0;
    // Texture 2
    U[vertex][2] = 0;
    V[vertex][2] = 0;
    // Texture 3
    U[vertex][3] = 0;
    V[vertex][3] = 0;
  }
  const float posOffset[4] = {tgtOffsetU,tgtOffsetV,0,0};
  _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeU, U[0], 4);
  _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeV, V[0], 4);
  _engine->SetVertexShaderConstantF(cb, VSC_PPEdgePosOffset, posOffset, 1);
  _engine->SetPixelShader(cb,_ps);

  // Drawing
  _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);

  if (!inScene) _engine->D3DEndScene();
  Finish();
}

/*
	Generate coefficients used in filtering process. Coefficients define offsets and weights for neighbouring
	samples used for sample determination. More information about coefficients can be found in GPU Gems 2,
	page 316.
*/
static void GenCoefficients(AutoArray<float> &vec, int itemsNum)
{
	const float wMult = 1.0f / 6.0f;

  float invDivider = 1.0f/ (itemsNum - 1);
	for (int i = 0; i < itemsNum; i++)
	{
		float alpha = i * invDivider; // alpha [0, 1], fractional part of texel coords
		float alpha2 = alpha * alpha;
		float alpha3 = alpha2 * alpha;

		// B-spline kernel - Coons cubics
		float w0 =		-alpha3 + 3.0f * alpha2 - 3.0f * alpha + 1.0f;		// w0 = -1*a^3 + 3*a^2 - 3*a + 1
		float w1 =  3.0f * alpha3 - 6.0f * alpha2 + 4.0f;						// w1 =  3*a^3 - 6*a^2 + 0*a + 4
		//w1 =  3.0f * alpha3 - 6.0f * alpha2 + 3.0f * alpha;
		float w2 = -3.0f * alpha3 + 3.0f * alpha2 + 3.0f * alpha + 1.0f;		// w2 = -3*a^3 + 3*a^2 + 3*a + 0
		//w2 = - 3.0f * alpha3 + 3.0f * alpha2;
		float w3 =		 alpha3;											// w3 =  1*a^3 + 0*a^2 + 0*a + 0

		w0 *= wMult;
		w1 *= wMult;
		w2 *= wMult;
		w3 *= wMult;

		// weights
		float g0 = w0 + w1;
		float g1 = w2 + w3;

		// offsets
		float h0 = 1 - (w1 / g0) + alpha;
		float h1 = 1 + (w3 / g1) - alpha;

		//std::cout << h1 << ", " << h0 << ", " << g1 << ", " << g0 << std::endl;

		vec.Add(h1);
		vec.Add(h0);
		vec.Add(g1);
		vec.Add(g0);
	}
}


bool EngineDD11::PPRescaleBicubic::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs)
{
  if (!base::Init(hlsl,vs,"VSPostProcessRescaleBicubic")) return false;

  if (!CompilePixelShader(hlsl,"PSPostProcessRescaleBicubic", _ps))
  {
    return false;
  }

  // create lookup texture
  {
    const int width = 128;
	  const int lookupTextureSize = width * 4;
	  
    MyD3DDevice& dev = _engine->GetD3DDeviceNonRec();
    #define FLOAT_COEFS 1
    
    #if FLOAT_COEFS
      DXGI_FORMAT coefFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
      //D3DFORMAT coefFormat = D3DFMT_A16B16G16R16F;
    #else
  	  // it seems all coefficients are 0..1, 8b precision should be enough for blending weights
      DXGI_FORMAT coefFormat = DXGI_FORMAT_R8G8B8A8_UINT;
      // D3DFORMAT coefFormat = D3DFMT_A8B8G8R8;
    #endif
    
    #ifdef _XBOX
      coefFormat = (D3DFORMAT)MAKELINFMT(coefFormat);
    #endif

    // create texture data
    AutoArray<float> vec;
    vec.Realloc(lookupTextureSize);

    AutoArray<DirectX::PackedVector::HALF> vec16;
    vec16.Resize(lookupTextureSize);

    //prepare coefficients vector (h0, h1, g0 & g1)
    GenCoefficients(vec, width);

    // convert to float16
    ConvertFloat32ToHalfArray(vec16.Data(), vec.Data(), lookupTextureSize);

    if (!CreateTextureHelper(dev, width, 1, coefFormat, vec16.Data(), 
      width * 4 * sizeof(DirectX::PackedVector::HALF), _lookUpTexture))
      return false;
  }
  return true;
}


void EngineDD11::PPRescaleBicubic::Do(bool isLast)
{
  const int cb = -1;
  
  PROFILE_SCOPE_GRF_EX(ppBic,pp,SCOPE_COLOR_YELLOW);

  
  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();
  // StretchRect not possible here?
  // Prepare drawing
  Prepare();

  // Set temporary render states
  SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);

  #ifdef _XBOX
    _engine->ResolveRenderTarget();
  #endif
  // Remember the current render target (to be used as a texture later)
  TextureHandle11* rt = _engine->GetCurrRenderTargetAsTexture();
  DoAssert(rt);

  // Use the back buffer as render target
  _engine->SwitchToBackBufferRenderTarget();

  Assert(_engine->_wRT<_engine->_w || _engine->_hRT<_engine->_h);
  
#if !XBOX_D3D
  BOOL rtIsSRGB = _engine->_caps._rtIsSRGB && _engine->_caps._canReadSRGB;
  SetSamplerStateTemp tempSRGB0(0, D3DSAMP_SRGBTEXTURE_OLD, rtIsSRGB);
#endif

  
  // DX11 TODO remove or repair 
  //PushVertexShaderConstant pushC0(0);
  //PushVertexShaderConstant pushC1(1);
  //PushVertexShaderConstant pushC2(2);
  //PushVertexShaderConstant pushC3(3);
  //PushVertexShaderConstant pushC4(4);
  //PushVertexShaderConstant pushC5(5);
  //PushVertexShaderConstant pushC6(6);
  //PushVertexShaderConstant pushC7(7);
  //PushVertexShaderConstant pushC8(8);
  
  //PushPixelShaderConstant pushP0(0);
  //PushPixelShaderConstant pushP1(1);

	// sampler 0 - texture to filtering
  SetTextureTemp tRenderTarget(0, rt);
	SetSamplerStateTemp s0Mag(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
	SetSamplerStateTemp s0Min(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
	SetSamplerStateTemp s0Mip(0, D3DSAMP_MIPFILTER_OLD, 0);
	SetSamplerStateTemp s0U(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
	SetSamplerStateTemp s0V(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

	// sampler 1 - lookup texture
	SetTextureTemp t1(1, _lookUpTexture._resView);
	SetSamplerStateTemp s1Mag(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
	SetSamplerStateTemp s1Min(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
	SetSamplerStateTemp s1Mip(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_WRAP_OLD);

  int srcW = _engine->_wRT, srcH = _engine->_hRT;
	D3DXVECTOR4 srcTexSize(float(srcW), float(srcH), 0.0f, 0.0f);
	D3DXVECTOR4 texelSize(1.0f / (float) srcW, 0.0f, 0.0f, 1.0f / (float) srcH);

  _engine->SetPixelShaderConstantF(cb, PSC_BicubicSourceSize, (const float *)&srcTexSize, 1);
  _engine->SetPixelShaderConstantF(cb, PSC_BicubicTexelSize, (const float *)&texelSize, 1);
  _engine->SetPixelShader(cb,_ps);

  // WIP: RESCALER remove _vsCustomEdge from PPGlow (always use point sampling there)

  // Drawing
  _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);
  
  if (!inScene) _engine->D3DEndScene();
  
  Finish();
}

//////////////////////////////////////////////////////////////////////////

bool EngineDD11::PostProcessCustomEdge::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs)
{
  // setup initial data
  VertexCustomEdge v[4];
  v[0].pos = Vector3P(-0.5, -0.5, 0.5); v[0].index = 0;
  v[1].pos = Vector3P(-0.5,  0.5, 0.5); v[1].index = 1;
  v[2].pos = Vector3P( 0.5, -0.5, 0.5); v[2].index = 2;
  v[3].pos = Vector3P( 0.5,  0.5, 0.5); v[3].index = 3;

  // Create the VB
  ComRef<IDirect3DVertexBuffer9Old> vb;

  D3D11_BUFFER_DESC bd;
  bd.Usage = D3D11_USAGE_DEFAULT;
  bd.ByteWidth = sizeof(VertexCustomEdge) * 4;
  bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = 0;
  bd.MiscFlags = 0;

  D3D11_SUBRESOURCE_DATA srData;
  srData.pSysMem = v;
  srData.SysMemPitch = 0;
  srData.SysMemSlicePitch = 0;

  HRESULT hr = _engine->GetD3DDeviceNonRec()->CreateBuffer(&bd, &srData, vb.Init());
  if (FAILED(hr))
  {
    LogF("Error: Vertex buffer creation failed.");
    return false;
  }

  _vbCustomEdge = VertexStaticData11::BufferFuture(vb);

  // Create the VD
  const D3D11_INPUT_ELEMENT_DESC vertexDecl[] =
  {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"BLENDINDICES", 0, DXGI_FORMAT_R32_FLOAT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  // create vertex shader
  if (!CompileVertexShader(vs,"VSPostProcessCustomEdge4T",_vsCustomEdge, vertexDecl, sizeof(vertexDecl)/sizeof(vertexDecl[0])))
  {
    return false;
  }

  return true;
}

void EngineDD11::PostProcessCustomEdge::Prepare()
{
  base::Prepare();
  
  int cb = -1;
  // Set the vertex shader + stream source
  _engine->SetStreamSource(cb,_vbCustomEdge, sizeof(VertexCustomEdge), 4);
  _engine->SetVertexShader(cb,_vsCustomEdge);
}

static const int GBSizeDivisor = 4;

bool EngineDD11::PPGaussianBlur::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs)
{
  if (!base::Init(hlsl,vs)) return false;

  // Create pixel shaders
  {
    if (!CompilePixelShader(hlsl,"PSPostProcessGaussianBlurH", _psHorizontal, true)) return false;
    if (!CompilePixelShader(hlsl,"PSPostProcessGaussianBlurV", _psBlur, true)) return false;
  }

  // Create horizontal blur and final blur render targets
  #ifndef _X360SHADERGEN
  {
    int fullWidth = _engine->Width();
    int fullHeight = _engine->Height();
    int partWidth = fullWidth / GBSizeDivisor;
    int partHeight = fullHeight / GBSizeDivisor;
    HRESULT err;

    err = _engine->CreateRenderTarget(partWidth, fullHeight, _engine->_rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, _rtHorizontal);
    if (FAILED(err)) {_engine->DDError11("Error: Render target texture creation failed.", err); return false;}

    err = _engine->CreateRenderTarget(partWidth, partHeight, _engine->_rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, _rtBlur);
    if (FAILED(err)) {_engine->DDError11("Error: Render target texture creation failed.", err); return false;}
  }
  #endif

  _toBlur = 0;
  // Success
  return true;
}

// perform blur on specified texture (toBlur) instead back buffer
void EngineDD11::PPGaussianBlur::Do(bool isLast, TextureHandle11* toBlur)
{
  Assert(_toBlur == 0);
  _toBlur = toBlur; // set blur source
  Do(isLast);
  _toBlur = 0;  // remove
}

void EngineDD11::PPGaussianBlur::Do(bool isLast)
{
  // Draw
  PROFILE_SCOPE_GRF_EX(ppGSB,pp,SCOPE_COLOR_YELLOW);

  // If scene is not being rendered initialize scene
  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();

  // Prepare drawing
  Prepare();

  // Save current render target and depth stencil
  _engine->PushRenderTargetDepthStencil();

  const int cb = -1;
  
  // Perform blur in 2 steps
  {
    // Protect VS constants from changing
    // DX11 TODO remove or repair 
    //PushVertexShaderConstant pushC0(0);
    //PushVertexShaderConstant pushC1(1);
    //PushVertexShaderConstant pushC2(2);
    //PushVertexShaderConstant pushC3(3);
    //PushVertexShaderConstant pushC4(4);
    //PushVertexShaderConstant pushC5(5);
    //PushVertexShaderConstant pushC6(6);
    //PushVertexShaderConstant pushC7(7);
    //PushVertexShaderConstant pushC8(8);

    // Prepare the UV coordinates offset coefficients
    static const float baseU[4] = {0, 0, 1, 1};
    static const float baseV[4] = {1, 0, 1, 0};

    // No posOffset needed
    const float posOffset[4] = {0, 0, 0, 0};
    _engine->SetVertexShaderConstantF(cb,VSC_PPEdgePosOffset, posOffset, 1);

    TextureHandle11* rt = (_toBlur == 0) ? _engine->CreateRenderTargetCopyAsTexture() : _toBlur;

    // Set temporarily render target and depth stencil to horizontally shrunk texture
    _engine->SetRenderTargetAndDepthStencil(_rtHorizontal, DepthStencil::DepthStencilNULL, 0, 1);

    // Set temporary render states
    SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);
#ifndef _X360SHADERGEN

    // Use SRGB conversion in texture read
    SetSamplerStateTemp ssSRGBTEXTURE(0, D3DSAMP_SRGBTEXTURE_OLD, _engine->_caps._rtIsSRGB);

    // Write in the same space as you read // _VBS3_TI
    SetRenderStateTemp rsSRGBSRGBWRITEENABLE(D3DRS_SRGBWRITEENABLE_OLD, _engine->_caps._rtIsSRGB);

#endif

    // Use linear sampling and clamping for the texture on stage 0
    SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
    SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
    SetSamplerStateTemp ss0MIPFILTER(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);
    SetSamplerStateTemp ss0ADDRESSU(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
    SetSamplerStateTemp ss0ADDRESSV(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

    // First step - render horizontally shrunk texture
    {
      // Prepare the UV coordinates offsets
      const float srcOffsetU = 0;
      const float srcOffsetV = 0;
      float U[4][4];
      float V[4][4];
      for (int vertex = 0; vertex < 4; vertex++)
      {
        // Texture 0 - source image
        U[vertex][0] = baseU[vertex] + srcOffsetU;
        V[vertex][0] = baseV[vertex] + srcOffsetV;
        // Rest
        U[vertex][1] = U[vertex][2] = U[vertex][3] = 0;
        V[vertex][1] = V[vertex][2] = V[vertex][3] = 0;
      }
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeU, U[0], 4);
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeV, V[0], 4);

      // Set the pixel shader for final blur
      _engine->SetPixelShader(cb, _psHorizontal);

      // Set current render target as temporary texture (we know we don't render into it)
      SetTextureTemp tTex0(0, rt);

      // Prepare PS constants
      float invW = 1.0f / _engine->Width();
      SetShaderConstantTemp spc0(PSC_InvWidth, D3DXVECTOR4(invW, invW, invW, invW));

      // Drawing
      _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#ifdef _XBOX
      // Resolve content of the render target
      CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _rtHorizontal, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif
    }

    // Rewrite render target and depth stencil surfaces by the blurred ones
    _engine->SetRenderTargetAndDepthStencil(_rtBlur, DepthStencil::DepthStencilNULL);

    // Second step - render finally blurred texture from the horizontally shrunked one
    {
      // Prepare the UV coordinates offsets
      const float srcOffsetU = 0;
      const float srcOffsetV = 0;
      float U[4][4];
      float V[4][4];
      for (int vertex = 0; vertex < 4; vertex++)
      {
        // Texture 0 - source image
        U[vertex][0] = baseU[vertex] + srcOffsetU;
        V[vertex][0] = baseV[vertex] + srcOffsetV;
        // Rest
        U[vertex][1] = U[vertex][2] = U[vertex][3] = 0;
        V[vertex][1] = V[vertex][2] = V[vertex][3] = 0;
      }
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeU, U[0], 4);
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeV, V[0], 4);

      // Set the pixel shader for final blur
      _engine->SetPixelShader(cb, _psBlur);

      // Set current render target as temporary texture (we know we don't render into it)
      SetTextureTemp tTex0(0, _rtHorizontal._srView);

      // Prepare PS constants
      float invH = 1.0f / _engine->Height();
      SetShaderConstantTemp spc0(PSC_InvHeight, D3DXVECTOR4(invH, invH, invH, invH));

      // Drawing
      _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#ifdef _XBOX
      // Resolve content of the render target
      CALL_D3D_MEMBER(_engine, Resolve, D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _rtBlur, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif
    }

  }

  // Restore the original render target and depth stencil buffer
  _engine->PopRenderTargetDepthStencil();

  // If scene is not being rendered finish initialized scene
  if (!inScene) _engine->D3DEndScene();

  // Restore usual rendering settings
  _engine->SwitchTL(cb,TLDisabled);
}

//////////////////////////////////////////////////////////////////////////

static const int BloomSizeDivisor = 8;

bool EngineDD11::PPBloom::InitCustomEdge(const LazyLoadFile &vs)
{
  // setup initial data
  VertexCustomEdge v[4];
  v[0].pos = Vector3P(-1.0, -1.0, 1.0); v[0].index = 0;
  v[1].pos = Vector3P(-1.0,  1.0, 1.0); v[1].index = 1;
  v[2].pos = Vector3P( 1.0, -1.0, 1.0); v[2].index = 2;
  v[3].pos = Vector3P( 1.0,  1.0, 1.0); v[3].index = 3;

  // Create the VB
  ComRef<IDirect3DVertexBuffer9Old> vb;

  D3D11_BUFFER_DESC bd;
  bd.Usage = D3D11_USAGE_DEFAULT;
  bd.ByteWidth = sizeof(VertexCustomEdge) * 4;
  bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = 0;
  bd.MiscFlags = 0;

  D3D11_SUBRESOURCE_DATA srData;
  srData.pSysMem = v;
  srData.SysMemPitch = 0;
  srData.SysMemSlicePitch = 0;

  HRESULT hr = _engine->GetD3DDeviceNonRec()->CreateBuffer(&bd, &srData, vb.Init());
  if (FAILED(hr))
  {
    LogF("Error: Vertex buffer creation failed.");
    return false;
  }

  _vbCustomEdge = VertexStaticData11::BufferFuture(vb);

  // Create the VD
  const D3D11_INPUT_ELEMENT_DESC vertexDecl[] =
  {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"BLENDINDICES", 0, DXGI_FORMAT_R32_FLOAT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  // create vertex shader
  if (!CompileVertexShader(vs,"VSPostProcessCustomEdge4T",_vsCustomEdge, vertexDecl, sizeof(vertexDecl)/sizeof(vertexDecl[0])))
  {
    return false;
  }

  // Create horizontal blur and final blur render targets
#ifndef _X360SHADERGEN
  {
    int fullWidth = _engine->Width();
    int fullHeight = _engine->Height();
    int partWidth = fullWidth / BloomSizeDivisor;
    int partHeight = fullHeight / BloomSizeDivisor;
    HRESULT err;

    err = _engine->CreateRenderTarget(fullWidth/2, fullHeight/2, _engine->_rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, _rtBright);
    if (FAILED(err)) {_engine->DDError11("Error: Render target texture creation failed.", err); return false;}

    err = _engine->CreateRenderTarget(partWidth, partHeight, _engine->_rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, _rtHorizontal);
    if (FAILED(err)) {_engine->DDError11("Error: Render target texture creation failed.", err); return false;}

    err = _engine->CreateRenderTarget(partWidth, partHeight, _engine->_rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, _rtBlur);
    if (FAILED(err)) {_engine->DDError11("Error: Render target texture creation failed.", err); return false;}
  }
#endif

  // Success
  return true;
}

int EngineDD11::PPBloom::SetParams(const float *pars, int nPars)
{
  if(nPars > 0)
    _threshold = pars[0];

  if(nPars > 1)
    _blurScale = pars[1];

  if(nPars > 2)
    _imageScale = pars[2];

  if(nPars > 3)
    _overbrightCoef = pars[3];

  if(nPars > 4)
    _blurKernelSize = pars[4];

  return 0;
}

bool EngineDD11::PPBloom::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs)
{
  if (!base::Init(hlsl,vs)) return false;

  _imageScale = 1.0f;
  _blurScale = 1.0f;
  _threshold = 1.0f;
  _overbrightCoef = 5.0f;
  _blurKernelSize = 9;

  // Create custom edge vertex resources
  if (!InitCustomEdge(vs)) return false;

  // Create pixel shaders
  {
    if (!CompilePixelShader(hlsl,"PSPostProcessGaussBlur", _psBlur, true)) return false;
    if (!CompilePixelShader(hlsl,"PSPostProcessBloomDownsample2", _psDownsample2, true)) return false;
    if (!CompilePixelShader(hlsl,"PSPostProcessBloomDownsample4", _psDownsample4, true)) return false;
    if (!CompilePixelShader(hlsl,"PSPostProcessBloomCombine", _psCombine, true)) return false;
  }

  // Success
  return true;
}


float ComputeGaussianValue( float x, float mean, float std_deviation )
{
  // The gaussian equation is defined as such:
  /*    
  -(x - mean)^2
  -------------
  1.0               2*std_dev^2
  f(x,mean,std_dev) = -------------------- * e^
  sqrt(2*pi*std_dev^2)

  */

  return ( 1.0f / sqrt( 2.0f * 3.14159265f * std_deviation * std_deviation ) ) 
    * expf( (-((x-mean)*(x-mean)))/(2.0f * std_deviation * std_deviation) );
}

const float GAUSS_MEAN = 0.0f;
const float GAUSS_DEVIATION = 0.5f;
const int KERNEL_SIZE = 9;

typedef float Vec4[4];

static void GaussKernel(Vec4* hkernel, Vec4* vkernel, int kernelSize, int width, int height, float scale = 1.0f)
{
  float	biwidth = 1.0f / (float)width;
  float biheight = 1.0f / (float)height;
  float hsize = (float)(kernelSize - 1) * 0.5f;


  for(int i = 0; i < kernelSize; i++ )
  {
    float f = ((float)i - hsize) / hsize;
    float gauss = ComputeGaussianValue( f, GAUSS_MEAN, GAUSS_DEVIATION );
    hkernel[i][0] = scale * ((float)i - hsize) * biwidth;
    hkernel[i][1] = 0.0f;
    hkernel[i][2] = 0.0f;
    hkernel[i][3] = gauss;

    vkernel[i][0] = 0.0f;
    vkernel[i][1] = scale * ((float)i - hsize) * biheight;
    vkernel[i][2] = 0.0f;
    vkernel[i][3] = gauss;
  }

  // the kernel needs to be normalized, so that we do not change image brightness
  float kernelSum = 0;
  for (int i=0; i<kernelSize; i++)
  {
    kernelSum += hkernel[i][3];
  }
  float normF = 1.0f/kernelSum;
  for (int i=0; i<kernelSize; i++)
  {
    hkernel[i][3] *= normF;
    vkernel[i][3] = hkernel[i][3];
  }
}

void EngineDD11::PPBloom::Do(bool isLast)
{ 
  // If scene is not being rendered initialize scene
  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();

  // Prepare drawing
  Prepare();
  
  // Remember the current render target
  TextureHandle11* rt = _engine->CreateRenderTargetCopyAsTexture();
  Do(isLast,rt);

  // If scene is not being rendered finish initialized scene
  if (!inScene) _engine->D3DEndScene();

  // Restore usual rendering settings
  Finish();
}

void EngineDD11::PPBloom::Do(bool isLast, TextureHandle11* rt)
{
  // Draw
  PROFILE_SCOPE_GRF_EX(ppBlo,pp,SCOPE_COLOR_YELLOW);

  const int cb = -1;
  
  // Protect VS constants from changing
  // DX11 TODO remove or repair 
  //PushVertexShaderConstant pushC0(0);
  //PushVertexShaderConstant pushC1(1);
  //PushVertexShaderConstant pushC2(2);
  //PushVertexShaderConstant pushC3(3);
  //PushVertexShaderConstant pushC4(4);
  //PushVertexShaderConstant pushC5(5);
  //PushVertexShaderConstant pushC6(6);
  //PushVertexShaderConstant pushC7(7);
  //PushVertexShaderConstant pushC8(8);

  //PushPixelShaderConstant ppushC0(0);
  //PushPixelShaderConstant ppushC1(1);
  //PushPixelShaderConstant ppushC2(2);
  //PushPixelShaderConstant ppushC3(3);
  //PushPixelShaderConstant ppushC4(4);
  //PushPixelShaderConstant ppushC5(5);
  //PushPixelShaderConstant ppushC6(6);
  //PushPixelShaderConstant ppushC7(7);
  //PushPixelShaderConstant ppushC8(8);
  //PushPixelShaderConstant ppushC9(9);
  //PushPixelShaderConstant ppushC10(10);
  //PushPixelShaderConstant ppushC11(11);
  //PushPixelShaderConstant ppushC12(12);
  //PushPixelShaderConstant ppushC13(13);
  //PushPixelShaderConstant ppushC14(14);
  //PushPixelShaderConstant ppushC15(15);

  // Use clamping for the textures
  SetSamplerStateTemp ss0(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss1(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss2(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss3(1, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

  // Save current render target and depth stencil
  _engine->PushRenderTargetDepthStencil();

  float scales[16][4];
  int fullWidth = _engine->Width();
  int fullHeight = _engine->Height();
  int brightWidth = fullWidth / 2;
  int brightHeight = fullHeight / 2;
  int partWidth = fullWidth / BloomSizeDivisor;
  int partHeight = fullHeight / BloomSizeDivisor;

  {
    // Set the vertex shader + stream source
    _engine->SetStreamSource(cb, _vbCustomEdge, sizeof(VertexCustomEdge), 4);
    _engine->SetVertexShader(cb, _vsCustomEdge);

    // Prepare the UV coordinates offset coefficients
    static const float baseU[4] = {0, 0, 1, 1};
    static const float baseV[4] = {1, 0, 1, 0};

    // Set temporarily render target and depth stencil to 2x2 shrunk texture
    _engine->SetRenderTargetAndDepthStencil(_rtBright, DepthStencil::DepthStencilNULL, 0, 1);

    // Set temporary render states
    SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);
#ifndef _X360SHADERGEN

    // Use SRGB conversion in texture read
    SetSamplerStateTemp ssSRGBTEXTURE0(0, D3DSAMP_SRGBTEXTURE_OLD, _engine->_caps._rtIsSRGB);
    SetSamplerStateTemp ssSRGBTEXTURE1(0, D3DSAMP_SRGBTEXTURE_OLD, FALSE);

    // Write in the same space as you read // _VBS3_TI
    SetRenderStateTemp rsSRGBSRGBWRITEENABLE(D3DRS_SRGBWRITEENABLE_OLD, _engine->_caps._rtIsSRGB);

#endif

    // setup pixel shader params
    float params[4] = {_threshold, _overbrightCoef, _blurScale, _imageScale};
    _engine->SetPixelShaderConstantF(cb, PSC_BloomPars, params, 1);

    // First step - render bright pass
    {
      // Use point sampling for the textures
      SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
      SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
      SetSamplerStateTemp ss0MIPFILTER(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD);
      SetSamplerStateTemp ss1MINFILTER(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
      SetSamplerStateTemp ss1MAGFILTER(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
      SetSamplerStateTemp ss1MIPFILTER(1, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD);

      // Prepare the UV coordinates offsets
      const float srcOffsetU = 0;
      const float srcOffsetV = 0;
      float U[4][4];
      float V[4][4];
      for (int vertex = 0; vertex < 4; vertex++)
      {
        // Texture 0 - source image
        U[vertex][0] = baseU[vertex] + srcOffsetU;
        V[vertex][0] = baseV[vertex] + srcOffsetV;
        // Rest
        U[vertex][1] = U[vertex][2] = U[vertex][3] = 0;
        V[vertex][1] = V[vertex][2] = V[vertex][3] = 0;
      }
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeU, U[0], 4);
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeV, V[0], 4);
      
      // No posOffset needed
      const float posOffset[4] = {0, 0, 0, 0};
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgePosOffset, posOffset, 1);

      // Set the pixel shader for bright pass
      _engine->SetPixelShader(cb, _psDownsample2);

      // Set current render target as temporary texture (we know we don't render into it)
      SetTextureTemp tTex0(0, rt);
      SetTextureTemp tTex2(1, _engine->_pp._glow->GetApertureRT()._srView); // aperture control texture

      // Prepare PS constants - PSSceneOffsets2;
      int index = 0;
      for(int y = -1; y < 1; y++)
      {
        for(int x = -1; x < 1; x++)
        {
          scales[index][0] = (1.0f / (float)fullWidth) * ((float)y + 0.5f);
          scales[index][1] = (1.0f / (float)fullHeight) * ((float)x + 0.5f);
          scales[index][2] = 0;
          scales[index][3] = 0;
          index++;
        }
      }
      _engine->SetPixelShaderConstantF(cb, PSC_SceneOffsets2, scales[0], 4);

      // Drawing
      _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#ifdef _XBOX
      // Resolve content of the render target
      CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _rtBright, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif
    }

    // Second step - 4x4 downscale
    {
      _engine->SetRenderTargetAndDepthStencil(_rtBlur, DepthStencil::DepthStencilNULL, 0, 1);

      // Use point sampling for the textures
      SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
      SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
      SetSamplerStateTemp ss0MIPFILTER(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD);

      // Prepare the UV coordinates offsets
      const float srcOffsetU = 0;
      const float srcOffsetV = 0;
      float U[4][4];
      float V[4][4];
      for (int vertex = 0; vertex < 4; vertex++)
      {
        // Texture 0 - source image
        U[vertex][0] = baseU[vertex] + srcOffsetU;
        V[vertex][0] = baseV[vertex] + srcOffsetV;
        // Rest
        U[vertex][1] = U[vertex][2] = U[vertex][3] = 0;
        V[vertex][1] = V[vertex][2] = V[vertex][3] = 0;
      }
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeU, U[0], 4);
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeV, V[0], 4);
      // No posOffset needed
      const float posOffset[4] = {0, 0, 0, 0};
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgePosOffset, posOffset, 1);

      // Set the pixel shader for downsample pass
      _engine->SetPixelShader(cb, _psDownsample4);

      // Set current render target as temporary texture (we know we don't render into it)
      SetTextureTemp tTex0(0, _rtBright._srView);

      // Prepare PS constants - PSSceneOffsets4;
      int index = 0;
      index = 0;
      for(int y = -2; y < 2; y++)
      {
        for(int x = -2; x < 2; x++)
        {
          scales[index][0] = (1.0f / (float)brightWidth) * ((float)x + 0.5f);
          scales[index][1] = (1.0f / (float)brightHeight) * ((float)y + 0.5f);
          scales[index][2] = 0.0f;
          scales[index][3] = 0.0f;
          index++;
        }
      }
      _engine->SetPixelShaderConstantF(cb, PSC_SceneOffsets4, scales[0], 16);

      // Drawing
      _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#ifdef _XBOX
      // Resolve content of the render target
      CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _rtBlur, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif
    }

    // repair blur kernel size
    if (_blurKernelSize <= 0)
      _blurKernelSize = 0;
    else
    {
      if ((_blurKernelSize % 2) == 0 )
        _blurKernelSize += 1; //must be ODD size
    }

    if (_blurKernelSize > PSBLUR_MAX_KERNEL_SIZE)
      _blurKernelSize = PSBLUR_MAX_KERNEL_SIZE;


    if (_blurKernelSize > 1)
    {
      Vec4 hkernel[PSBLUR_MAX_KERNEL_SIZE];
      Vec4 vkernel[PSBLUR_MAX_KERNEL_SIZE];

      GaussKernel(hkernel, vkernel, _blurKernelSize, partWidth, partHeight, 1.0f);

      // Third step - horizontal blur
      {
        _engine->SetRenderTargetAndDepthStencil(_rtHorizontal, DepthStencil::DepthStencilNULL, 0, 1);

        // We need to set the viewport as it was set to previous values inside the SetTemporaryRenderTarget.
        // We cannot use the SetDepthRange method here, because we need to use custom Width and Height
        {
          // DX11 - should be already set in SetRenderTargetAndDepthStencil
          //_engine->SetViewport(partWidth, partHeight, 0, 1);
        }

        // Use point sampling for the textures
        SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
        SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
        SetSamplerStateTemp ss0MIPFILTER(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD);

        // Prepare the UV coordinates offsets
        const float srcOffsetU = 0;
        const float srcOffsetV = 0;
        float U[4][4];
        float V[4][4];
        for (int vertex = 0; vertex < 4; vertex++)
        {
          // Texture 0 - source image
          U[vertex][0] = baseU[vertex] + srcOffsetU;
          V[vertex][0] = baseV[vertex] + srcOffsetV;
          // Rest
          U[vertex][1] = U[vertex][2] = U[vertex][3] = 0;
          V[vertex][1] = V[vertex][2] = V[vertex][3] = 0;
        }
        _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeU, U[0], 4);
        _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeV, V[0], 4);
        // No posOffset needed
        const float posOffset[4] = {0, 0, 0, 0};
        _engine->SetVertexShaderConstantF(cb, VSC_PPEdgePosOffset, posOffset, 1);

        // Set the pixel shader for blur pass
        _engine->SetPixelShader(cb, _psBlur);

        // Set current render target as temporary texture (we know we don't render into it)
        SetTextureTemp tTex0(0, _rtBlur._srView);

        int kernelSize[4] = {_blurKernelSize, _blurKernelSize, _blurKernelSize, _blurKernelSize};
        _engine->SetPixelShaderConstantI(cb, PSC_BlurKernelSize, kernelSize, 1);
        _engine->SetPixelShaderConstantF(cb, PSC_BlurKernel, hkernel[0], KERNEL_SIZE);

        // Drawing
        _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#ifdef _XBOX
        // Resolve content of the render target
        CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _rtHorizontal, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif
      }

      // Fourth step - vertical blur
      {
        // unset texture so we can set it as render target
        _engine->SetTextureDirect(cb, 0, NULL);

        _engine->SetRenderTargetAndDepthStencil(_rtBlur, DepthStencil::DepthStencilNULL, 0, 1);

        // We need to set the viewport as it was set to previous values inside the SetTemporaryRenderTarget.
        // We cannot use the SetDepthRange method here, because we need to use custom Width and Height
        {
          // DX11 - should be already set in SetRenderTargetAndDepthStencil
          //_engine->SetViewport(partWidth, partHeight, 0, 1);
        }

        // Use point sampling for the textures
        SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
        SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
        SetSamplerStateTemp ss0MIPFILTER(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD);

        // Set current render target as temporary texture (we know we don't render into it)
        SetTextureTemp tTex0(0, _rtHorizontal._srView);

        _engine->SetPixelShaderConstantF(cb, PSC_BlurKernel, vkernel[0], KERNEL_SIZE);

        // Drawing
        _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#ifdef _XBOX
        // Resolve content of the render target
        CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _rtBlur, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif
      }

    }


    // Restore the original render target and depth stencil buffer
    _engine->PopRenderTargetDepthStencil();
 
    // Fifth step - combine blurred image and the original one
    if(isLast)
    {
      SetTextureTemp tTex0(0, rt);
      SetTextureTemp tTex1(1, _rtBlur._srView);

      // Use point sampling for the textures
      SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
      SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
      SetSamplerStateTemp ss1MINFILTER(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp ss1MAGFILTER(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);

      // Prepare the UV coordinates offsets
      const float srcOffsetU1 = 0;
      const float srcOffsetV1 = 0;
      const float srcOffsetU2 = 0;
      const float srcOffsetV2 = 0;
      float U[4][4];
      float V[4][4];
      for (int vertex = 0; vertex < 4; vertex++)
      {
        // Texture 0 - blurred image
        U[vertex][0] = baseU[vertex] + srcOffsetU1;
        V[vertex][0] = baseV[vertex] + srcOffsetV1;
        // Texture 1 - source image
        U[vertex][1] = baseU[vertex] + srcOffsetU2;
        V[vertex][1] = baseV[vertex] + srcOffsetV2;
        // Rest
        U[vertex][2] = U[vertex][3] = 0;
        V[vertex][2] = V[vertex][3] = 0;
      }
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeU, U[0], 4);
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeV, V[0], 4);
     
      // Set the pixel shader for combine pass
      _engine->SetPixelShader(cb, _psCombine);

      // Drawing
      _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);
    }
  }

}

//////////////////////////////////////////////////////////////////////////
bool EngineDD11::PPRain3D::Init(PPResources *resources, bool firstInit)
{
  _textureReady = false;
  _rainStrength = 0.0f;
  _currentOccluder = 0.0f;
  _targetOccluder = 0.0f;

  return true;
}

//////////////////////////////////////////////////////////////////////////
bool EngineDD11::PPRain3D::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs)
{
  if (!base::Init(hlsl,vs)) return false;

  _textureReady = false;
  
  _resrc = new PPRain3DResources;
  _rainStrength = 0.0f;
  _currentOccluder = 0.0f;
  _targetOccluder = 0.0f;

  //not initialized yet
  if(GEngine == NULL)
    GEngine = _engine;

  // create initial data
  static const int numVertices = numLayers * 4 * 3 * 2;
  Vertex vertsArr[numVertices]; 
  Vertex* verts = vertsArr;
  
  float voff = 0;
  for(int b = 0; b < numLayers; b++)
  {
    Vector3 v0, v1, v2, v3;

    float layer = b / (float)(numLayers - 1);
    //we must go from largest to smallest, because of alpha blending
    layer = 1.0f - layer;
    //X+
		v0 = Vector3( 1.0f, -1.0f, -1.0f);
		v1 = Vector3( 1.0f,  1.0f, -1.0f);
		v2 = Vector3( 1.0f,  1.0f,  1.0f);
		v3 = Vector3( 1.0f, -1.0f,  1.0f);
    verts[0].Copy(v0);
    verts[0].u = -1.0f;
    verts[0].v = -1.0f + voff;
    verts[0].layer = layer;
    verts[1].Copy(v1);
    verts[1].u =  1.0f;
    verts[1].v = -1.0f + voff;
    verts[1].layer = layer;
    verts[2].Copy(v2);
    verts[2].u =  1.0f;
    verts[2].v =  1.0f + voff;
    verts[2].layer = layer;
    verts[3].Copy(v0);
    verts[3].u = -1.0f;
    verts[3].v = -1.0f + voff;
    verts[3].layer = layer;
    verts[4].Copy(v2);
    verts[4].u =  1.0f;
    verts[4].v =  1.0f + voff;
    verts[4].layer = layer;
    verts[5].Copy(v3);
    verts[5].u = -1.0f;
    verts[5].v =  1.0f + voff;
    verts[5].layer = layer;
    verts += 6;

    //X-
	  v0 = Vector3(-1.0f, -1.0f, -1.0f);
	  v1 = Vector3(-1.0f,  1.0f, -1.0f);
	  v2 = Vector3(-1.0f,  1.0f,  1.0f);
	  v3 = Vector3(-1.0f, -1.0f,  1.0f);
    verts[0].Copy(v0);
    verts[0].u =  1.0f;
    verts[0].v = -1.0f + voff;
    verts[0].layer = layer;
    verts[1].Copy(v1);
    verts[1].u = -1.0f;
    verts[1].v = -1.0f + voff;
    verts[1].layer = layer;
    verts[2].Copy(v2);
    verts[2].u = -1.0f;
    verts[2].v =  1.0f + voff;
    verts[2].layer = layer;
    verts[3].Copy(v0);
    verts[3].u =  1.0f;
    verts[3].v = -1.0f + voff;
    verts[3].layer = layer;
    verts[4].Copy(v2);
    verts[4].u = -1.0f;
    verts[4].v =  1.0f + voff;
    verts[4].layer = layer;
    verts[5].Copy(v3);
    verts[5].u =  1.0f;
    verts[5].v =  1.0f + voff;
    verts[5].layer = layer;
    verts += 6;

    //Z+
		v0 = Vector3(-1.0f,  1.0f, -1.0f);
		v1 = Vector3( 1.0f,  1.0f, -1.0f);
		v2 = Vector3( 1.0f,  1.0f,  1.0f);
		v3 = Vector3(-1.0f,  1.0f,  1.0f);
    verts[0].Copy(v0);
    verts[0].u =  1.0f;
    verts[0].v = -1.0f + voff;
    verts[0].layer = layer;
    verts[1].Copy(v1);
    verts[1].u = -1.0f;
    verts[1].v = -1.0f + voff;
    verts[1].layer = layer;
    verts[2].Copy(v2);
    verts[2].u = -1.0f;
    verts[2].v =  1.0f + voff;
    verts[2].layer = layer;
    verts[3].Copy(v0);
    verts[3].u =  1.0f;
    verts[3].v = -1.0f + voff;
    verts[3].layer = layer;
    verts[4].Copy(v2);
    verts[4].u = -1.0f;
    verts[4].v =  1.0f + voff;
    verts[4].layer = layer;
    verts[5].Copy(v3);
    verts[5].u =  1.0f;
    verts[5].v =  1.0f + voff;
    verts[5].layer = layer;
    verts += 6;

    //Z-
		v0 = Vector3(-1.0f, -1.0f, -1.0f);
		v1 = Vector3( 1.0f, -1.0f, -1.0f);
		v2 = Vector3( 1.0f, -1.0f,  1.0f);
		v3 = Vector3(-1.0f, -1.0f,  1.0f);
    verts[0].Copy(v0);
    verts[0].u = -1.0f;
    verts[0].v = -1.0f + voff;
    verts[0].layer = layer;
    verts[1].Copy(v1);
    verts[1].u =  1.0f;
    verts[1].v = -1.0f + voff;
    verts[1].layer = layer;
    verts[2].Copy(v2);
    verts[2].u =  1.0f;
    verts[2].v =  1.0f + voff;
    verts[2].layer = layer;
    verts[3].Copy(v0);
    verts[3].u = -1.0f;
    verts[3].v = -1.0f + voff;
    verts[3].layer = layer;
    verts[4].Copy(v2);
    verts[4].u =  1.0f;
    verts[4].v =  1.0f + voff;
    verts[4].layer = layer;
    verts[5].Copy(v3);
    verts[5].u = -1.0f;
    verts[5].v =  1.0f + voff;
    verts[5].layer = layer;
    verts += 6;
    voff += 0.25f;
  }

  // Create the VB
  ComRef<IDirect3DVertexBuffer9Old> vb;

  D3D11_BUFFER_DESC bd;
  bd.Usage = D3D11_USAGE_DEFAULT;
  bd.ByteWidth = sizeof(Vertex) * numVertices;
  bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = 0;
  bd.MiscFlags = 0;

  D3D11_SUBRESOURCE_DATA srData;
  srData.pSysMem = vertsArr;
  srData.SysMemPitch = 0;
  srData.SysMemSlicePitch = 0;

  HRESULT hr = _engine->GetD3DDeviceNonRec()->CreateBuffer(&bd, &srData, vb.Init());
  if (FAILED(hr))
  {
    LogF("Error: Vertex buffer creation failed.");
    return false;
  }

  _resrc->_vbRainGeometry = VertexStaticData11::BufferFuture(vb);

  // Create the VD
  const D3D11_INPUT_ELEMENT_DESC vertexDecl[] =
  {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}
  };

  if (!CompilePixelShader(hlsl,"PSPostProcessRain3D", _resrc->_psRain, true)) 
    return false;
  if (!CompileVertexShader(vs,"VSPostProcessRain3D", _resrc->_vsRain, vertexDecl, sizeof(vertexDecl)/sizeof(vertexDecl[0])))
    return false;

  return true;
}

void EngineDD11::PPRain3D::SetTexture(TextureD3D11 *tex)
{
  _resrc->_rainTexture = tex;
  // no need to preload or make permanent - we can handle it async
  //if (_rainTexture) _rainTexture->PreloadTexture(1, 1, true);
}

void ConvertMatrixTransposed(PASSMATRIXTYPE &mat, Matrix4Val src);
void ConvertProjectionMatrix(D3DMATRIX &mat, Matrix4Val src, float zBias);

//#ifdef _PROFILE
//#pragma optimize("", off)
//#endif
void EngineDD11::PPRain3D::Prepare(float deltaT)
{
  if(_rainStrength == 0 || _resrc->_rainTexture.IsNull())
    return;

	//no data, no rain
	MipInfo info = _engine->TextBank()->UseMipmap(_resrc->_rainTexture, 0, 0);
	_textureReady = (info._texture == _resrc->_rainTexture && info._level==0);

  const Camera* camera = GScene->GetCamera();
  
  //trace above us, and find the "rain occluder"
  CollisionBuffer col;
  Object *camOn = GWorld->GetCameraEffect() ? NULL : GWorld->CameraOn();
  Object* obj = GWorld->GetCamInsideVehicle();
  VisualStateAge cameraAge = camera->GetAge();
  GScene->GetLandscape()->ObjectCollisionLine(cameraAge,col,Landscape::FilterIgnoreTwo(camOn,obj),camera->Position(),camera->Position() + Vector3(0,10,0),0.15f,ObjIntersectView);
  float crad = 0;
  if(col.Size() != 0)
  {
    for(int n = 0; n < col.Size(); n++)
    {
      const Object* cObject = col[n].object;
      // check the size of the component above us
      const ConvexComponents* comps = cObject->GetShape()->GetConvexComponents(col[n].geomLevel);
      if(comps)
      {
        const ConvexComponent* comp = (*comps)[col[n].component];
        if(comp)
        {
          // we are interested in horizontal radius only
          // the distincion is important for tall objects like trees
          float xzRad = comp->Min().DistanceXZ(comp->Max())*0.5f;
          crad = floatMax(crad, floatMin(comp->GetRadius(),xzRad));
        }
      }
    }
  }

  static float maxRadius = 25.0f;
  //don't make it too large, otherwise the rain is barely visible
  _targetOccluder = floatMin(crad, maxRadius);

  static float fadespeed = 10.0f;
  _currentOccluder = (_targetOccluder - _currentOccluder) * deltaT * fadespeed + _currentOccluder;
}

void EngineDD11::PPRain3D::Do(bool isLast)
{
  if(!_textureReady || _rainStrength == 0)
    return;

  // If scene is not being rendered initialize scene
  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();

	Matrix4 viewWorld;
	const int cb = -1;

	PASSMATRIXTYPE viewWorldMatrix;

	//we are at camera position
	Matrix4 modelToWorldOffset;
  modelToWorldOffset.SetIdentity();
  //here we can set orientation of rain
  //modelToWorldOffset.SetOrientation(xxx);
  // DX11 TODO remove or repair 
	//PushVertexShaderConstant vsc0(0);
	//PushVertexShaderConstant vsc1(1);
	//PushVertexShaderConstant vsc2(2);
	//PushVertexShaderConstant vsc3(3);
	//PushVertexShaderConstant vsc4(4);
	//PushVertexShaderConstant vsc5(5);
	//PushVertexShaderConstant vsc6(6);
	//PushVertexShaderConstant vsc7(7);
	//PushVertexShaderConstant vsc8(8);
	//PushVertexShaderConstant vsc9(9);
	//PushVertexShaderConstant vsc10(10);
	//PushPixelShaderConstant psc0(0);
	//PushPixelShaderConstant psc1(1);
	//PushPixelShaderConstant psc2(2);
	//PushPixelShaderConstant psc3(3);
  const Camera* camera = GScene->GetCamera();
  float intensity = GScene->MainLight()->GetAmbient().Brightness() + GScene->MainLight()->GetDiffuse().Brightness();

  //float eyeaccom = 0.006f;
  float eyeaccom = GWorld->GetEyeAccom();
  //limit this. We don't want to shine in dark
  eyeaccom = floatMin(eyeaccom, 0.05f);
  intensity *= eyeaccom;
  saturate(intensity, 0, 1);

  static float affectByLight = 0.8f;
  if(!_engine->GetNightVision())
    intensity = intensity * affectByLight + (1.0f - affectByLight);

	Matrix4 viewZero;
	viewZero.SetOrientation(camera->InverseScaled().Orientation());
	viewZero.SetPosition(VZero);
	viewWorld = viewZero * modelToWorldOffset;
  viewWorld.SetPosition(VZero);
	ConvertMatrixTransposed(viewWorldMatrix, viewWorld);
	_engine->SetVertexShaderConstantF(cb, VSC_RainViewMatrix, (float*)&viewWorldMatrix, 3);

	D3DXMATRIX projMatrix;
	ConvertProjectionMatrix(projMatrix, camera->ProjectionNormal(), 0.0f);

	_engine->SetVertexShaderConstantF(cb, VSC_RainProjMatrix, (float*)&projMatrix, 4);

  //static so we have opportunity to tune values during debugging
	static float shiftScale[4] = {0.0f, 0.0f, 2.0f, 0.5f};
  static float rainspeed = 2.0f;
	shiftScale[1] = Glob.time.toFloat() * rainspeed * shiftScale[3];
	_engine->SetVertexShaderConstantF(cb, VSC_RainShiftScale, shiftScale, 1);

  static float minMax[2][4] = {
    {0.4f, 8.0f, 0.4f, 0.0f},
    {2.5f, 8.0f, 2.5f, 0.0f}
  };

  float minMax2[2][4];
  float radius = 0;
  //when we are inside some object, make the geometry bigger
  Object* obj = GWorld->GetCamInsideVehicle();
  if(obj)
  {
    Vector3 camMinMax[2];
    float lradius = obj->ClippingInfo(obj->RenderVisualState(),camMinMax,Object::ClipGeometry);
    //we must make it smaller, otherwise even player as a man has 2meters radius
    lradius = floatMax(lradius - 2.0f, 0.0f);
    radius += lradius;
  }

  radius += _currentOccluder;
  for(int m = 0; m < 2; m++)
  {
    for(int n = 0; n < 4; n++)
    {
      minMax2[m][n] = minMax[m][n] + radius;
    }
  }
  _engine->SetVertexShaderConstantF(cb, VSC_RainMinBox, minMax2[0], 2);

  float sealevel = GScene->GetLandscape()->GetSeaLevel(camera->Position().X(), camera->Position().Z()) - camera->Position().Y();

  float seaLevel[4] = {sealevel,sealevel, sealevel, sealevel};
  _engine->SetVertexShaderConstantF(cb, VSC_RainSeaLevel, seaLevel, 1);
	_engine->SetVertexShader(cb, _resrc->_vsRain);

  //affect intensity by light - more during day, less during night
	float alpha[4] = {1.0f, 1.0f, 1.0f, _rainStrength * intensity};
  saturate(alpha[3], 0, 1);

	_engine->SetPixelShaderConstantF(cb, PSC_RainAlpha, alpha, 1);

	float screenRes[4] = {1.0f / (float)_engine->Width(), 1.0f / (float)_engine->Height(), 0.0f, 0.0f};
  _engine->SetPixelShaderConstantF(cb, PSC_RainScreenRes, screenRes, 1);
	_engine->SetPixelShader(cb, _resrc->_psRain);

	SetRenderStateTemp rs0(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);
	SetRenderStateTemp rs1(D3DRS_ALPHABLENDENABLE_OLD, true);
	SetRenderStateTemp rs2(D3DRS_ALPHATESTENABLE_OLD, false);
  // note: cannot use Temp here - they are not going to be set in the DoPrepareTriangle and the temporary setting returning would set a undefined value
  _engine->D3DSetRenderState(cb, D3DRS_SRCBLEND_OLD,D3DBLEND_ONE_OLD);
  _engine->D3DSetRenderState(cb, D3DRS_DESTBLEND_OLD,D3DBLEND_INVSRCALPHA_OLD);
#ifndef _X360SHADERGEN
  // DX11 TODO PP
	//SetRenderStateTemp rs5(D3DRS_FOGENABLE, false);
#endif
	SetRenderStateTemp rs6(D3DRS_CULLMODE_OLD, D3DCULL_NONE_OLD);
	//SetRenderStateTemp rs7(D3DRS_FILLMODE_OLD, D3DFILL_SOLID_OLD);
  //SetRenderStateTemp rs8(D3DRS_ZENABLE_OLD, true);
	SetRenderStateTemp rs8(D3DRS_ZENABLE_OLD, FALSE);
	SetRenderStateTemp rs9(D3DRS_ZFUNC_OLD, D3DCMP_LESSEQUAL_OLD);
	SetRenderStateTemp rs10(D3DRS_ZWRITEENABLE_OLD, FALSE);
	SetRenderStateTemp rs11(D3DRS_STENCILENABLE_OLD, FALSE);
	SetSamplerStateTemp ss0(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_WRAP_OLD);
	SetSamplerStateTemp ss1(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_WRAP_OLD);
	SetSamplerStateTemp ss2(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
	SetSamplerStateTemp ss3(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
	SetSamplerStateTemp ss4(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);
	SetSamplerStateTemp ss6(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
	SetSamplerStateTemp ss7(1, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);
	SetSamplerStateTemp ss8(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
	SetSamplerStateTemp ss9(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
	SetSamplerStateTemp ss10(1, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);

  
  #if ENCAPSULATE_TEXTURE_REF
	SetTextureTemp tTex1(0, _resrc->_rainTexture->GetHandle() ? _resrc->_rainTexture->GetHandle()->_surface : NULL);
	#else
	SetTextureTemp tTex1(0, _resrc->_rainTexture->GetHandle(false));
	#endif
  SetTextureTemp tTex2(1, _engine->GetDepthAsTexture());

#ifndef _X360SHADERGEN
  SetSamplerStateTemp ss1SRGBTEXTURE(0, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
  SetSamplerStateTemp ss2SRGBTEXTURE(1, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
#endif

	_engine->SetStreamSource(cb, _resrc->_vbRainGeometry, sizeof(Vertex), numLayers * 4 * 2 * 3);
	_engine->DrawPrimitive(cb, D3DPT_TRIANGLELIST_OLD, 0, numLayers * 4 * 2);

  // restore state into a default
  _engine->D3DSetRenderState(cb, D3DRS_STENCILENABLE_OLD, TRUE);
  _engine->D3DSetRenderState(cb, D3DRS_ZFUNC_OLD,D3DCMP_LESSEQUAL_OLD);
  _engine->D3DSetRenderState(cb, D3DRS_ZWRITEENABLE_OLD,TRUE);
  _engine->D3DSetRenderState(cb, D3DRS_ZENABLE_OLD,TRUE);
  _engine->D3DSetRenderState(cb, D3DRS_CULLMODE_OLD,D3DCULL_CCW_OLD);
  _engine->D3DSetRenderState(cb, D3DRS_DESTBLEND_OLD,D3DBLEND_INVSRCALPHA_OLD);
  //_engine->D3DSetRenderState(cb, D3DRS_SRCBLEND_OLD,D3DBLEND_ONE_OLD);
  _engine->D3DSetRenderState(cb, D3DRS_ALPHABLENDENABLE_OLD,FALSE);
  _engine->D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE_OLD,D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);

  // If scene is not being rendered finish initialized scene
  if (!inScene) _engine->D3DEndScene();
}
//#ifdef _PROFILE
//#pragma optimize("", on)
//#endif

int EngineDD11::PPRain3D::SetParams(const float *pars, int nPars)
{
  if(pars && nPars > 0)
    _rainStrength = pars[0];

  return 0;
}

void EngineDD11::PPRain3D::Cleanup()
{
  _resrc->_rainTexture.Free();
}


//////////////////////////////////////////////////////////////////////////
void EngineDD11::PPSSSM::Prepare()
{
  // This method overrides the PPSimple::Prepare method, that's why we call the PostProcess::Prepare here instead of usual base::Prepare
  PostProcess::Prepare();

  const int cb = -1;
  
  // Set the stream source
  _engine->SetStreamSource(cb, _vb, sizeof(Vertex), 4);

  // Set the vertex shader
  _engine->SetVertexShader(cb, _vs);

  // Set the pixel shader
  _engine->SetPixelShader(cb, _ps);
}

bool EngineDD11::PPSSSM::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs)
{
  if (!base::Init(hlsl,vs,"VSPostProcess")) return false;
  if (!CompilePixelShader(hlsl,"PSPostProcessSSSM_Default", _ps, true)) return false;
  return true;
}

void EngineDD11::PPSSSM::Do(float minShadowsZ, float maxShadowsZ, bool firstLayer, bool lastLayer)
{
  // Draw
  PROFILE_SCOPE_GRF_EX(ppSSSM,pp,SCOPE_COLOR_YELLOW);

  const int cb = -1;

  // If scene is not being rendered initialize scene
  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();

  // Prepare drawing
  Prepare();

  // Pass the FOV into the shader
  float fovW = 1.0f;
  float fovH = 1.0f;
  if (_engine->CBState(cb)._proj.IsFinite())
  {
    fovW = 1.0f / _engine->CBState(cb)._proj(0, 0);
    fovH = 1.0f / _engine->CBState(cb)._proj(1, 1);
  }
  SetShaderConstantTemp spc0(PSC_FOV, D3DXVECTOR4(fovW, fovH, 0, 0));

  // Influence density of trees by multiplying the final alpha
  Color pscSBTSize_invSBTSize_X_AlphaMult(_engine->_sbSize, 1.0f / _engine->_sbSize, 0, 1.5f); // Empirical coefficient controlling density of trees
  SetShaderConstantTemp spc1(PSC_SBTSize_invSBTSize_X_AlphaMult, (float*)&pscSBTSize_invSBTSize_X_AlphaMult);

  // Setup the matrix that converts the transformedPosition into shadow map
  _engine->SetPixelShaderConstantF(cb, PSC_ShadowmapMatrix, (float*)&_engine->_shadowMapMatrixDX, 3);

  // Setup the SB depth scale
  Color pscMaxDepth(_engine->_maxDepth, _engine->_maxDepth, _engine->_maxDepth, _engine->_maxDepth);
  _engine->SetPixelShaderConstantF(cb, PSC_MaxDepth, (float*)&pscMaxDepth, 1);

  // Setup the border constants
  D3DXVECTOR4 layerBorder;
  layerBorder = D3DXVECTOR4(maxShadowsZ, maxShadowsZ/*lastLayer ? 1.0f : 0.0f*/, maxShadowsZ, minShadowsZ);
  _engine->SetPixelShaderConstantF(cb, PSC_ShadowmapLayerBorder, layerBorder, 1);

  // Set temporary render states
  SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);
  SetRenderStateTemp rsSRGBSRGBWRITEENABLE(D3DRS_SRGBWRITEENABLE_OLD, FALSE);

  // unset shadow map texture from PS stage
  _engine->SetTextureDirect(-1, TEXID_SHADOWMAP, NULL);

  // Save current render target and depth stencil
  _engine->PushRenderTargetDepthStencil();

  // We're going to render to SSSM
  _engine->SetRenderTargetAndDepthStencil(_engine->_sssmRenderTarget, DepthStencil::DepthStencilNULL);

#ifndef _XBOX
  // Clear the SSSM, as it's possible nothing will be drawn on certain places
  if (firstLayer)
  {
    Color fColor(1.0f, 0, 0, 0);
    _engine->Clear(false, true, PackedColor(fColor), false);
  }
#endif

  // Set depth map as a input
  SetTextureTemp tTex0(0, _engine->_renderTargetDepthInfo._srView);
  SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
  SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
  SetSamplerStateTemp ss0MIPFILTER(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);
  SetSamplerStateTemp ss0SRGBTEXTURE(0, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
  
#ifdef _XBOX
  // Set SSSM as source - X360 needs it, as previous content of SSSM in EDRAM was replaced when rendering to SB
  SetTextureTemp tTex1(1, _engine->_sssm);
  SetSamplerStateTemp ss1MINFILTER(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
  SetSamplerStateTemp ss1MAGFILTER(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
  SetSamplerStateTemp ss1MIPFILTER(1, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);
  SetSamplerStateTemp ss1SRGBTEXTURE(1, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
#endif

  // Set the SB as shadow map
  _engine->UseShadowMapSB(cb);

  /*
  // because of VSM we want bilinear sampling - UseShadowMapSB set point
  _engine->D3DSetSamplerState(TEXID_SHADOWMAP, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  _engine->D3DSetSamplerState(TEXID_SHADOWMAP, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  */

  // Drawing
  _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#ifdef _XBOX
  // Resolve content of the render target
  CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _engine->_sssm, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif

  // Restore the original render target and depth stencil buffer
  _engine->PopRenderTargetDepthStencil();

  // If scene is not being rendered finish initialized scene
  if (!inScene) _engine->D3DEndScene();

  
  #if !defined _XBOX && !defined _X360SHADERGEN
  // if it was the last layer, and we are using multisampling, it is a time to Resolve / StretchRect
  // we are currently not using multisampling on SSSM on the PC, and Xbox handles it elsewhere
//     if (_engine->GetFSAA()>0)
//     {
//       // TODO: make compatible with background rendering
//       //DoAssert(!_engine->_backgroundD3D);
//       ComRef<IDirect3DSurface9> sssmTexLevel0;
//       _engine->_sssm->GetSurfaceLevel(0,sssmTexLevel0.Init());
//       CALL_D3D_MEMBER(_engine,StretchRect,_engine->_sssmSurface,NULL,sssmTexLevel0,NULL,D3DTEXF_POINT_OLD);
//     }
  #endif

  Finish();
}

//////////////////////////////////////////////////////////////////////////

bool EngineDD11::PPSSSMStencil::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs)
{
  if (!base::Init(hlsl,vs,"VSPostProcess")) return false;
  if (!CompilePixelShader(hlsl,"PSPostProcessSSSMStencil", _ps, true)) return false;
  return true;
}


void EngineDD11::PPSSSMStencil::Do(bool isLast)
{
  PROFILE_SCOPE_GRF_EX(ppSSSMStencil,pp,SCOPE_COLOR_YELLOW);

  // If scene is not being rendered initialize scene
  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();

  // Prepare drawing
  Prepare();

  const int cb = -1;
  
  {
    // make sure SSSM is not mapped as a texture
    _engine->SetTextureDirect(cb, TEXID_SHADOWMAP,NULL);
    // SetTextureTemp did not work well - caused strange error "Texture not created with this device. SetTexture failed."
    //SetTextureTemp noSSSM(TEXID_SHADOWMAP,NULL);


    // Save current render target and depth stencil
    _engine->PushRenderTargetDepthStencil();

    if (_engine->IsFSAAEnabled())
    {
       // on PC SSSM is never multisampled
        _engine->SetRenderTargetAndDepthStencil(_engine->_sssmRenderTarget, _engine->_depthStencilDepthInfo);
    }
    else
    {
       _engine->SetRenderTargetAndDepthStencil(_engine->_sssmRenderTarget, _engine->_depthStencilS);
    }

#ifdef _XBOX
    // Clear the SSSM map to 1 in R (no shadows)
    if (restore || !_engine->IsSBEnabled()) CALL_D3D_MEMBER(_engine,Clear,0,NULL,D3DCLEAR_TARGET,PackedColor(255, 0, 0, 0),0,0);
#else
    if (isLast)
    {
      Color fColor(1.0f, 0, 0, 0);
      _engine->Clear(false, true, PackedColor(fColor), false);
    }
#endif

    // Set usual PP render states
    SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);
    SetRenderStateTemp rsSRGBSRGBWRITEENABLE(D3DRS_SRGBWRITEENABLE_OLD, FALSE);

    // Set render states to control the stencil visualization
    SetRenderStateTemp rsSTENCILFUNC(D3DRS_STENCILFUNC_OLD, D3DCMP_LESS_OLD);
    SetRenderStateTemp rsSTENCILMASK(D3DRS_STENCILMASK_OLD, STENCIL_COUNTER);
    SetRenderStateTemp rsSTENCILREF(D3DRS_STENCILREF_OLD, 0);
    SetRenderStateTemp rsZTEST(D3DRS_ZENABLE_OLD,D3DZB_FALSE_OLD);
    SetRenderStateTemp rsALPHABLENDENABLE(D3DRS_ALPHABLENDENABLE_OLD, FALSE);
    SetRenderStateTemp rsSRCBLEND(D3DRS_SRCBLEND_OLD, D3DBLEND_ONE_OLD);
    SetRenderStateTemp rsDESTBLEND(D3DRS_DESTBLEND_OLD, D3DBLEND_ONE_OLD);

    // Drawing
    _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#ifdef _XBOX
  #if 0 // PRT_SHADOWS
    // Resolve rendertarget into _renderTargetD
    for (int i = 0; i < _engine->_tilingRectsCount; i++)
    {
      // Set predication to tile i.
      CALL_D3D_MEMBER(_engine,SetPredication, D3DPRED_TILE( i ) );

      // Destination point is the upper left corner of the tiling rect.
      D3DPOINT* pDestPoint = (D3DPOINT*)&_engine->_tilingRects[i];

      D3DVECTOR4 clearColor;
      clearColor.x = _engine->_clearColor.R();
      clearColor.y = _engine->_clearColor.G();
      clearColor.z = _engine->_clearColor.B();
      clearColor.w = _engine->_clearColor.A();
      
      // Resolve render target into _renderTargetD, clear the source tile to the clear values
      CALL_D3D_MEMBER(_engine,Resolve,
        D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS|D3DRESOLVE_CLEARRENDERTARGET|D3DRESOLVE_CLEARDEPTHSTENCIL,
        &_engine->_tilingRects[i], _engine->_sssm, pDestPoint, 0, 0, &clearColor, 1.0f, 0L, NULL
      );
    }
  #else
    CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _engine->_sssm, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
  #endif
#endif

    // Restore the original render target and depth stencil buffer
    _engine->PopRenderTargetDepthStencil();
  }

  // If scene is not being rendered finish initialized scene
  if (!inScene) _engine->D3DEndScene();
  // Restore usual rendering settings
  Finish();
}

//////////////////////////////////////////////////////////////////////////
#ifdef SSSMBLUR
bool EngineDD11::PPSSSMB::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs)
{
  if (!PostProcess::Init(hlsl,vs)) return false;

  // Create custom edge vertex resources
  if (!InitCustomEdge(vs)) return false;

  // Create pixel shaders
  {
    if (!CompilePixelShader(hlsl,"PSPostProcessSSSMBH", _psHorizontal, true)) return false;
    if (!CompilePixelShader(hlsl,"PSPostProcessSSSMBV", _psBlur, true)) return false;
    if (!CompilePixelShader(hlsl,"PSPostProcessSSSMBFinalBlur", _psFinalBlur, true)) return false;
  }

  // Success
  return true;
}

void EngineDD11::PPSSSMB::Do(bool isLast)
{
  // Draw
  PROFILE_SCOPE_GRF_EX(ppSSSMB,pp,SCOPE_COLOR_YELLOW);

  // If scene is not being rendered initialize scene
  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();

  // Prepare drawing
  Prepare();

  // Perform blur in 2 steps
  {
    // Protect VS constants from changing
    PushVertexShaderConstant pushC0(0);
    PushVertexShaderConstant pushC1(1);
    PushVertexShaderConstant pushC2(2);
    PushVertexShaderConstant pushC3(3);
    PushVertexShaderConstant pushC4(4);
    PushVertexShaderConstant pushC5(5);
    PushVertexShaderConstant pushC6(6);
    PushVertexShaderConstant pushC7(7);
    PushVertexShaderConstant pushC8(8);

    // Set the vertex shader + stream source
    _engine->SetStreamSource(_vbCustomEdge, sizeof(VertexCustomEdge), 4);
    _engine->SetVertexDeclaration(_vdCustomEdge);
    _engine->SetVertexShader(_vsCustomEdge);

    // Prepare the UV coordinates offset coefficients
    static const float baseU[4] = {0, 0, 1, 1};
    static const float baseV[4] = {1, 0, 1, 0};
    int fullWidth = _engine->Width();
    int fullHeight = _engine->Height();
    int partWidth = fullWidth / GBSizeDivisor;
    int partHeight = fullHeight / GBSizeDivisor;

    // No posOffset needed
    const float posOffset[4] = {0, 0, 0, 0};
    _engine->SetVertexShaderConstantF(8, posOffset, 1);

    // Set temporarily render target and depth stencil to horizontally shrunk texture
    ComRef<IDirect3DSurface9> zeroDepth;
    _engine->SetTemporaryRenderTarget(_rtHorizontal, _rtsHorizontal, zeroDepth);

    // We need to set the viewport as it was set to previous values inside the SetTemporaryRenderTarget.
    // We cannot use the SetDepthRange method here, because we need to use custom Width and Height
    {
      D3DVIEWPORT9 viewData;
      memset(&viewData,0,sizeof(viewData));
      viewData.X = 0;
      viewData.Y = 0;
      viewData.Width  = partWidth;
      viewData.Height = fullHeight;
      viewData.MinZ = 0.0f;
      viewData.MaxZ = 1.0f;
      CALL_D3D_MEMBER(_engine,SetViewport,viewData);
    }

    // Set temporary render states
    SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);
#ifndef _X360SHADERGEN

    // Use SRGB conversion in texture read
    SetSamplerStateTemp ss0SRGBTEXTURE(0, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
    SetSamplerStateTemp ss1SRGBTEXTURE(1, D3DSAMP_SRGBTEXTURE_OLD, FALSE);

    // Write in the same space as you read
    SetRenderStateTemp rsSRGBSRGBWRITEENABLE(D3DRS_SRGBWRITEENABLE_OLD, FALSE);

#endif

    // Use linear sampling and clamping for the texture on stage 0
    SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
    SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
    SetSamplerStateTemp ss0ADDRESSU(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
    SetSamplerStateTemp ss0ADDRESSV(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);
    SetSamplerStateTemp ss1MINFILTER(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
    SetSamplerStateTemp ss1MAGFILTER(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
    SetSamplerStateTemp ss1ADDRESSU(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
    SetSamplerStateTemp ss1ADDRESSV(1, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

    // First step - render horizontally shrunk texture
    {
      // Prepare the UV coordinates offsets
      const float srcOffsetU = 0.5f / partWidth;
      const float srcOffsetV = 0.5f / fullHeight;
      float U[4][4];
      float V[4][4];
      for (int vertex = 0; vertex < 4; vertex++)
      {
        // Texture 0 - source image
        U[vertex][0] = baseU[vertex] + srcOffsetU;
        V[vertex][0] = baseV[vertex] + srcOffsetV;
        // Rest
        U[vertex][1] = U[vertex][2] = U[vertex][3] = 0;
        V[vertex][1] = V[vertex][2] = V[vertex][3] = 0;
      }
      _engine->SetVertexShaderConstantF(0, U[0], 4);
      _engine->SetVertexShaderConstantF(4, V[0], 4);

      // Set the pixel shader for final blur
      _engine->SetPixelShader(_psHorizontal);

      // Set sssm as temporary texture
      SetTextureTemp tTex0(0, _engine->_sssm);

      // Prepare PS constants
      float invW = 1.0f / _engine->Width();
      SetShaderConstantTemp spc0(0, D3DXVECTOR4(invW, invW, invW, invW));

      // Drawing
      _engine->DrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#ifdef _XBOX
      // Resolve content of the render target
      CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _rtHorizontal, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif
    }

    // Rewrite render target and depth stencil surfaces by the blurred ones
    _engine->SetRenderTargetAndDepthStencil(_rtsBlur, _rtBlur, zeroDepth);

    // Second step - render finally blurred texture from the horizontally shrunked one
    {
      // Prepare the UV coordinates offsets
      const float srcOffsetU = 0.5f / partWidth;
      const float srcOffsetV = 0.5f / partHeight;
      float U[4][4];
      float V[4][4];
      for (int vertex = 0; vertex < 4; vertex++)
      {
        // Texture 0 - source image
        U[vertex][0] = baseU[vertex] + srcOffsetU;
        V[vertex][0] = baseV[vertex] + srcOffsetV;
        // Rest
        U[vertex][1] = U[vertex][2] = U[vertex][3] = 0;
        V[vertex][1] = V[vertex][2] = V[vertex][3] = 0;
      }
      _engine->SetVertexShaderConstantF(0, U[0], 4);
      _engine->SetVertexShaderConstantF(4, V[0], 4);

      // Set the pixel shader for final blur
      _engine->SetPixelShader(_psBlur);

      // Set current render target as temporary texture (we know we don't render into it)
      SetTextureTemp tTex0(0, _rtHorizontal);

      // Prepare PS constants
      float invH = 1.0f / _engine->Height();
      SetShaderConstantTemp spc0(0, D3DXVECTOR4(invH, invH, invH, invH));

      // Drawing
      _engine->DrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#ifdef _XBOX
      // Resolve content of the render target
      CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _rtBlur, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif
    }

    // Third step - create the SSSMB map
    {
      // Rewrite render target and depth stencil surfaces by the SSSMB
      _engine->SetRenderTargetAndDepthStencil(_engine->_sssmBSurface, _engine->_sssmB, zeroDepth);

      // Prepare the UV coordinates offsets
      const float srcOffsetU = 0.5f / fullWidth;
      const float srcOffsetV = 0.5f / fullHeight;
      float U[4][4];
      float V[4][4];
      for (int vertex = 0; vertex < 4; vertex++)
      {
        // Texture 0 - source image
        U[vertex][0] = baseU[vertex] + srcOffsetU;
        V[vertex][0] = baseV[vertex] + srcOffsetV;
        // Rest
        U[vertex][1] = U[vertex][2] = U[vertex][3] = 0;
        V[vertex][1] = V[vertex][2] = V[vertex][3] = 0;
      }
      _engine->SetVertexShaderConstantF(0, U[0], 4);
      _engine->SetVertexShaderConstantF(4, V[0], 4);

      // Set the pixel shader for final blur
      _engine->SetPixelShader(_psFinalBlur);

      // Set the textures
      SetTextureTemp tTex0(0, _engine->_sssm);
      SetTextureTemp tTex1(1, _rtBlur);

      // Drawing
      _engine->DrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#ifdef _XBOX
      // Resolve content of the render target
      CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _rtBlur, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif
    }

    // Restore the original render target and depth stencil buffer
    _engine->RestoreOldRenderTarget();
  }

  // If scene is not being rendered finish initialized scene
  if (!inScene) _engine->D3DEndScene();

  // Restore usual rendering settings
  _engine->SwitchTL(TLDisabled);
}
#endif


//////////////////////////////////////////////////////////////////////////
//
// SSAO postprocess effect
//
//////////////////////////////////////////////////////////////////////////
void EngineDD11::PPSSAO::Prepare()
{
  const PPSSAOResources *res = Resources();
  if (!res) return;

  // const int cb = -1;
  
  // TODO
  // base::Prepare();

  _engine->GetD3DDeviceNonRec()->IASetVertexBuffers(0, res->_vb, sizeof(VertexP3T2), 0);
  _engine->GetD3DDeviceNonRec()->VSSetShader(res->_vsSSAO._shader);
  _engine->GetD3DDeviceNonRec()->IASetInputLayout(res->_vsSSAO._inputLayout);

  /*
  // Set the stream source
  _engine->SetStreamSource(cb, res->_vb, sizeof(VertexP3T2), 4);
  // Set the vertex declaration
  _engine->SetVertexDeclaration(cb, res->_vd);
  // Set the vertex shader
  _engine->SetVertexShader(cb, res->_vsSSAO);
  */
}

//==================================================================================================
bool EngineDD11::PPSSAO::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, bool firstInit)
{
  _bDebug = false;

  if (!base::Init(hlsl,vs)) return false;

  if (firstInit)
  {
    SetDefaultValues();
    _enabled = false;
    _pInterp.Initialize();
  }

  Ref<PPSSAOResources> resrc = new PPSSAOResources;

  // setup initial data
  Vertex v[4];
  v[0].pos = Vector3P(-0.5, -0.5, 0.5); v[0].t0.u = 0; v[0].t0.v = 1;
  v[1].pos = Vector3P(-0.5,  0.5, 0.5); v[1].t0.u = 0; v[1].t0.v = 0;
  v[2].pos = Vector3P( 0.5, -0.5, 0.5); v[2].t0.u = 1; v[2].t0.v = 1;
  v[3].pos = Vector3P( 0.5,  0.5, 0.5); v[3].t0.u = 1; v[3].t0.v = 0;

  // Create the VB
  ComRef<IDirect3DVertexBuffer9Old> vb;

  D3D11_BUFFER_DESC bd;
  bd.Usage = D3D11_USAGE_DEFAULT;
  bd.ByteWidth = sizeof(Vertex) * 4;
  bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = 0;
  bd.MiscFlags = 0;

  D3D11_SUBRESOURCE_DATA srData;
  srData.pSysMem = v;
  srData.SysMemPitch = 0;
  srData.SysMemSlicePitch = 0;

  HRESULT hr = _engine->GetD3DDeviceNonRec()->CreateBuffer(&bd, &srData, vb.Init());
  if (FAILED(hr))
  {
    LogF("Error: Vertex buffer creation failed.");
    return false;
  }

  // Create the VD
  const D3D11_INPUT_ELEMENT_DESC vertexDecl[] =
  {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  // create vertex shader
  if (!CompileVertexShader(vs,"VSPostProcessSSAO",resrc->_vsSSAO, vertexDecl, sizeof(vertexDecl)/sizeof(vertexDecl[0])))
  {
    return false;
  }
  if (!CompileVertexShader(vs,"VSPostProcessSSAODown",resrc->_vsSSAODown, vertexDecl, sizeof(vertexDecl)/sizeof(vertexDecl[0])))
  {
    return false;
  }

  //pixel shaders
  if (!CompilePixelShader(hlsl,"PSPostProcessSSAO", resrc->_psSSAO, false))
    return false;

  if (!CompilePixelShader(hlsl,"PSPostProcessSSAOBlurVert", resrc->_psBlurVert, false))
    return false;

  if (!CompilePixelShader(hlsl,"PSPostProcessSSAOBlurHorz", resrc->_psBlurHorz, false))
    return false;

  if (!CompilePixelShader(hlsl,"PSPostProcessSSAOBlurHorzDepth", resrc->_psBlurHorzDepth, false))
    return false;

  if (!CompilePixelShader(hlsl,"PSPostProcessSSAOBlurVertDepth", resrc->_psBlurVertDepth, false))
    return false;

  if (!CompilePixelShader(hlsl,"PSPostProcessSSAOFinal", resrc->_psSSAOFinal, false))
    return false;

  if (!CompilePixelShader(hlsl,"PSPostProcessSSAODebug", resrc->_psSSAODebug, false))
    return false;

  if (!CompilePixelShader(hlsl,"PSPostProcessSSAODown", resrc->_psSSAODown, false))
    return false;

  _bHalfResOld = _pInterp._lastPars._halfres;
  _bBlurHalfResOld = _pInterp._lastPars._blurhalfres;

  if (_engine->GetD3DDeviceRec().IsNull()) return false;

  // render targets
  if (!SetRT(_engine->GetD3DDeviceRec(), resrc, _bHalfResOld, _bBlurHalfResOld))
  {
    return false;
  }

  //create noise texture
  const int texsize = 128;

  // create initial data
  AutoArray<DWORD> initData;
  initData.Resize(texsize * texsize);

  //for each texel
  for (int y = 0; y < texsize; y++)
  {
    for (int x = 0; x < texsize; x++)
    {
      unsigned char *data = (unsigned char*)(initData.Data() + x + y*texsize);

      float angle = GRandGen.RandomValue()*3.14159265f*2.0f;

      float s = sin(angle);
      float c = cos(angle);

      //random matrix
      data[0] = (unsigned char) (( c*0.5f + 0.5f)*255.0f);
      data[1] = (unsigned char) (( s*0.5f + 0.5f)*255.0f);
      data[2] = (unsigned char) (( s*0.5f + 0.5f)*255.0f);
      data[3] = (unsigned char) ((-c*0.5f + 0.5f)*255.0f);
    }
  }

  // create texture
  SurfaceDataD3D11 noiseTexture;
  if (!CreateTextureHelper(_engine->GetD3DDeviceNonRec(), texsize, texsize, DXGI_FORMAT_R8G8B8A8_UNORM, initData.Data(),
    texsize*sizeof(DWORD), noiseTexture))
    return false;


  // assign to shared resources
  resrc->_vb = VertexStaticData11::BufferFuture(vb);
  resrc->_noiseTexture = noiseTexture;

  resrc->_texWidth  = texsize;
  resrc->_texHeight = texsize;

  _resrc = resrc;

  return true;
}

//==================================================================================================
bool EngineDD11::PPSSAO::SetRT(MyD3DDeviceCB &device, PPSSAOResources *res, bool halfres, bool blurhalfres)
{
  int wantedheight = _engine->Height() >> (halfres ? 1 : 0);
  int wantedwidth  = _engine->Width() >> (halfres ? 1 : 0);

  res->_SSAOWidth = wantedwidth;
  res->_SSAOHeight= wantedheight;

  _bHalfResOld = halfres;
  _bBlurHalfResOld = blurhalfres;

  DXGI_FORMAT rtFmt = DXGI_FORMAT_R8G8B8A8_UNORM;

  HRESULT err; 
  err = _engine->CreateRenderTarget(wantedwidth, wantedheight, rtFmt, 1, 0, D3D11_USAGE_DEFAULT, true, res->_rtSSAO);
  if (FAILED(err))
  {
    _engine->DDError11("Error: Render target texture creation failed.", err); 
    return false;
  }

  wantedheight = _engine->Height() >> ((halfres || blurhalfres) ? 1 : 0);
  wantedwidth  = _engine->Width() >> ((halfres || blurhalfres) ? 1 : 0);

  res->_blurWidth = wantedwidth;
  res->_blurHeight= wantedheight;
  
  err = _engine->CreateRenderTarget(wantedwidth, wantedheight, rtFmt, 1, 0, D3D11_USAGE_DEFAULT, true, res->_rtBlur);
  if (FAILED(err))
  {
    _engine->DDError11("Error: Render target texture creation failed.", err); 
    return false;
  }
   
  if (!halfres && blurhalfres)
  {
    err = _engine->CreateRenderTarget(wantedwidth, wantedheight, rtFmt, 1, 0, D3D11_USAGE_DEFAULT, true, res->_rtSmall);
    if (FAILED(err))
    {
      _engine->DDError11("Error: Render target texture creation failed.", err); 
      return false;
    }
  }

  return true;
}


//==================================================================================================
void EngineDD11::PPSSAO::SetDefaultValues()
{
  _pInterp._pars.SetHighestSetting();
}

//==================================================================================================
bool EngineDD11::PPSSAO::Init(PPResources *resources, bool firstInit)
{
  DoAssert(false);

  if (resources)
  {
    _resrc = resources;

    if (firstInit)
    {
      _bDebug = false;

      SetDefaultValues();
      _enabled = false;
      _pInterp.Initialize();
    }

    return true;
  }

  return false;
}


//==================================================================================================
bool EngineDD11::PPSSAO::UnInit()
{
  if (_resrc)
  {
    _resrc->UnInit();

    return true;
  }

  return false;
}

//==================================================================================================
int EngineDD11::PPSSAO::SetParams(const float *pars, int nPars)
{
  if(nPars > 0)
  {
    if (pars[0] < 0)
    {
      _bDebug = !_bDebug;
      return 0;
    }

    setIntensity(pars[0]);
  }

  if(nPars > 1)
    setTreshold0(pars[1]);

  if(nPars > 2)
    setTreshold1(pars[2]);

  if(nPars > 3)
    setNearRadius(pars[3]);

  if(nPars > 4)
    setFarRadius(pars[4]);

  if(nPars > 5)
    setNearDist(pars[5]);

  if(nPars > 6)
    setFarDist(pars[6]);

  if(nPars > 7)
    setDepthBlurDist(pars[7]);

  if(nPars > 8)
    setBlurPasses(pars[8]);

  if(nPars > 9)
    SetHalfRes(fabs(pars[9]) < 0.01f);

  if(nPars > 10)
    SetBlurHalfRes(fabs(pars[10]) < 0.01f);

  return 0;
}

//==================================================================================================
int EngineDD11::PPSSAO::SetParams(const AutoArray<float> &pars)
{
  int nPars = pars.Size();

  if(nPars > 0)
  {
    if (pars[0] < 0)
    {
      _bDebug = !_bDebug;
      return 0;
    }

    setIntensity(pars[0]);
  }

  if(nPars > 1)
    setTreshold0(pars[1]);

  if(nPars > 2)
    setTreshold1(pars[2]);

  if(nPars > 3)
    setNearRadius(pars[3]);

  if(nPars > 4)
    setFarRadius(pars[4]);

  if(nPars > 5)
    setNearDist(pars[5]);

  if(nPars > 6)
    setFarDist(pars[6]);

  if(nPars > 7)
    setDepthBlurDist(pars[7]);

  if(nPars > 8)
    setBlurPasses(pars[8]);

  if(nPars > 9)
    SetHalfRes(fabs(pars[9]) < 0.01f);

  if(nPars > 10)
    SetBlurHalfRes(fabs(pars[10]) < 0.01f);

  return 0;
}

//==================================================================================================
LSError EngineDD11::PPSSAO::Serialize( ParamArchive &ar )
{
  CHECK(ar.Serialize("enabled",_enabled,0,false));
  CHECK(ar.Serialize("interp", _pInterp, 0));
  CHECK(ar.Serialize("prior", _priority, 0, -1));

  return LSOK;
}


//==================================================================================================
void EngineDD11::PPSSAO::SetQuality(int quality)
{
  switch (quality)
  {
    case 5 : _pInterp._lastPars.SetHighestSetting(); break;
    case 4 : _pInterp._lastPars.SetHighSetting(); break;
    case 3 : _pInterp._lastPars.SetMediumSetting(); break;
  }
}

//==================================================================================================

#if 0 // _ENABLE_CHEATS
  #define SSAO_DEBUG
#endif

#ifdef SSAO_DEBUG
  static bool ssaoon = true;
  static bool ssaoblurdepth = false;
  static int ssaomode = 0;
  static bool ssaotest = false;
  static bool ssaosave = false;
  static int ssaoparam = 0;
#endif

//==================================================================================================
void EngineDD11::PPSSAO::Do(float fogEnd, float fogStart, float fogCoef)
{
  #ifdef SSAO_DEBUG
    int plus = 0, minus = 0;

    // lctrl + lalt + 
    if((::GetAsyncKeyState(VK_LCONTROL) & 0x8000) && (::GetAsyncKeyState(VK_LMENU) & 0x8000))
    {
       /* O */ if (::GetAsyncKeyState(0x4F) & 0x8000)    {  ssaoon = !ssaoon; ::Sleep(100); LogF("SSAO %s", ssaoon ? "on" : "off"); }
       /* I */ if (::GetAsyncKeyState(0x49) & 0x8000)    {  ssaomode = ssaomode == 2 ? 0 : ssaomode + 1; ::Sleep(100); LogF("SSAO mode : %s", ssaomode == 0 ? "full (Highest setting)" : ssaomode == 1 ? "full ssao, half blur (High setting)" : "half all (medium setting)" );       switch (ssaomode) 
                   {
                      case 0 : _pInterp._lastPars.SetHighestSetting(); break;
                      case 1 : _pInterp._lastPars.SetHighSetting(); break;
                      case 2 : _pInterp._lastPars.SetMediumSetting(); break;
                   }} 
       /* T */ if (::GetAsyncKeyState('T') & 0x8000)    {  _pInterp._lastPars._intensity += 0.05f; ::Sleep(100); }
       /* U */ if (::GetAsyncKeyState('U') & 0x8000)    {  _pInterp._lastPars._intensity -= 0.05f; ::Sleep(100); }

       /* D */ if (::GetAsyncKeyState(0x44) & 0x8000)    {  ssaotest = !ssaotest; ::Sleep(100); LogF("%s", ssaotest ?  "SSAO debug mode" : "SSAO normal mode"); }
      // /* U */ if (::GetAsyncKeyState(0x55) & 0x8000)    {  ssaoblurdepth= !ssaoblurdepth; ::Sleep(100); LogF("%s", ssaoblurdepth ?  "SSAO bluring with edge preserving" : "SSAO blur normal mode"); }
       /* S */ if (::GetAsyncKeyState(0x53) & 0x8000)    {  ssaosave = true; ::Sleep(100); LogF("SSAO data saved into c:/"); }
       /* P */ if (::GetAsyncKeyState(0x50) & 0x8000)    {  ssaoparam = ssaoparam == 8 ? ssaoparam = 0 : ssaoparam+1; ::Sleep(100); int p=ssaoparam; LogF("SSAO param : %s = %3.3g", 
                    p==0?"intensity":
                    p==1?"treshold0":
                    p==2?"treshold1":
                    p==3?"near radius":
                    p==4?"far radius":
                    p==5?"near distance":
                    p==6?"far distance":
                    p==7?"blur depth distance":
                    "blur passes",
                    
                    p==0?_pInterp._lastPars._intensity:
                    p==1?_pInterp._lastPars._treshold0:
                    p==2?_pInterp._lastPars._treshold1:
                    p==3?_pInterp._lastPars._nearradius:
                    p==4?_pInterp._lastPars._farradius:
                    p==5?_pInterp._lastPars._neardist:
                    p==6?_pInterp._lastPars._fardist:
                    p==7?_pInterp._lastPars._depthblurdist:
                    _pInterp._lastPars._ssaoblurpasses); }

       /* *=+ /=- */
       plus  = GetAsyncKeyState(0xBE/*0x6A*/) & 0x8000;
       minus = GetAsyncKeyState(0xBC/*0x6F*/) & 0x8000; 
    }

    if (!ssaoon)
    { 
      return;
    }

    if (plus|minus)
    {
      switch (ssaoparam)
      {
      case 0 : _pInterp._lastPars._intensity      = plus ? _pInterp._lastPars._intensity + 0.01 : floatMax(0.0, _pInterp._lastPars._intensity - 0.01); LogF("SSAO intensity %3.3g", _pInterp._lastPars._intensity); break;
      case 1 : _pInterp._lastPars._treshold0      = plus ? floatMin(1.0f, _pInterp._lastPars._treshold0 + 0.001f) : floatMax(0.0, _pInterp._lastPars._treshold0 - 0.001); LogF("SSAO treshold0 %3.3g", _pInterp._lastPars._treshold0); break;
      case 2 : _pInterp._lastPars._treshold1      = plus ? floatMin(1.0f, _pInterp._lastPars._treshold1 + 0.001) : floatMax(0.0, _pInterp._lastPars._treshold1 - 0.001); LogF("SSAO treshold1 %3.3g", _pInterp._lastPars._treshold1); break;
      case 3 : _pInterp._lastPars._nearradius     = plus ? floatMin(200.0f, _pInterp._lastPars._nearradius + 1.0) : floatMax(2.0, _pInterp._lastPars._nearradius - 1.0f); LogF("SSAO near radius %3.3g", _pInterp._lastPars._nearradius); break;
      case 4 : _pInterp._lastPars._farradius      = plus ? floatMin(200.0f, _pInterp._lastPars._farradius + 1.0) : floatMax(1.0, _pInterp._lastPars._farradius - 1.0f); LogF("SSAO far radius %3.3g", _pInterp._lastPars._farradius); break;
      case 5 : _pInterp._lastPars._neardist       = plus ? floatMin(2.0f, _pInterp._lastPars._neardist + 0.005) : floatMax(0.0f, _pInterp._lastPars._neardist - 0.005f); LogF("SSAO near distance %3.3g", _pInterp._lastPars._neardist); break;
      case 6 : _pInterp._lastPars._fardist        = plus ? floatMin(200.0f, _pInterp._lastPars._fardist + 0.1) : floatMax(0.1f, _pInterp._lastPars._fardist - 0.1f); LogF("SSAO far distance %3.3g", _pInterp._lastPars._fardist); break;
      case 7 : _pInterp._lastPars._depthblurdist  = plus ? floatMin(20.0f, _pInterp._lastPars._depthblurdist + 0.1) : floatMax(0.05f, _pInterp._lastPars._depthblurdist - 0.05f); LogF("SSAO depth blur distance %3.3g", _pInterp._lastPars._depthblurdist); break;
      case 8 : _pInterp._lastPars._ssaoblurpasses = plus ? floatMin(4, _pInterp._lastPars._ssaoblurpasses + 1) : floatMax(0, _pInterp._lastPars._ssaoblurpasses - 1); LogF("SSAO blur passes %d", _pInterp._lastPars._ssaoblurpasses); Sleep(200); break;
      }
    }
  #endif

  
  // we have to do all commands directly and not to record them, because
  // we are in background thread and replaying DrawSSAO command

  //DoAssert(!CheckMainThread()); // can be main thread when not HL recording
  MyD3DDeviceCB& device = _engine->GetD3DDeviceRec();

  if (device.IsNull()) return;

  if (_params._intensity < 0.001f || !_enabled) return;

  const PPSSAOResources *res = Resources();
  if (!res) return;

  PROFILE_SCOPE_GRF_EX(ppSSAO,pp,SCOPE_COLOR_RED);

  if (_bHalfResOld != _params._halfres || _bBlurHalfResOld != _params._blurhalfres)
  {
    SetRT(device, (PPSSAOResources *) res, _params._halfres, _params._blurhalfres);
  }

  bool halfres  = _params._halfres;
  bool blurhalfres = _params._blurhalfres;
  bool smallres = blurhalfres && !halfres;

  // If scene is not being rendered initialize scene
  bool inScene = _engine->_d3dFrameOpen;
  //if (!inScene) device->BeginScene();
  DoAssert(inScene);

  // Prepare drawing
  _engine->DoSetStreamSource(res->_vb, sizeof(VertexP3T2), 0); //setting 0 as size is ok, it is only used for optimizations
  _engine->DoSetVertexShader(res->_vsSSAO._shader, res->_vsSSAO._inputLayout);

  _engine->DoSetRenderState(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);

  //render states
  _engine->DoSetRenderState(D3DRS_ZWRITEENABLE_OLD, FALSE);
  _engine->DoSetRenderState(D3DRS_ZENABLE_OLD, FALSE);
  _engine->DoSetRenderState(D3DRS_CULLMODE_OLD, D3DCULL_NONE_OLD);
  _engine->DoSetRenderState(D3DRS_FILLMODE_OLD, D3DFILL_SOLID_OLD);
  _engine->DoSetRenderState(D3DRS_ALPHATESTENABLE_OLD, FALSE);
  _engine->DoSetRenderState(D3DRS_ALPHABLENDENABLE_OLD, FALSE);
  _engine->DoSetRenderState(D3DRS_SRGBWRITEENABLE_OLD, FALSE);

  //sampler states, depth texture
  TextureHandle11* depth = _engine->GetDepthAsTexture();

#ifdef SSAO_DEBUG
  if (ssaosave)
  {
    D3DXSaveTextureToFile("c:/depth.dds", D3DXIFF_DDS, depth, NULL);
  }
#endif

  _engine->DoSetTexture(0, depth);

  _engine->DoSetSamplerState(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  _engine->DoSetSamplerState(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  _engine->DoSetSamplerState(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);
  _engine->DoSetSamplerState(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_BORDER_OLD);
  _engine->DoSetSamplerState(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_BORDER_OLD);
  _engine->DoSetSamplerState(0, D3DSAMP_BORDERCOLOR_OLD, 0xFFFFFFFF);
  _engine->DoSetSamplerState(0, D3DSAMP_SRGBTEXTURE_OLD, 0);

  //computed ssao
  _engine->DoSetSamplerState(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  _engine->DoSetSamplerState(1, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);
  _engine->DoSetSamplerState(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  _engine->DoSetSamplerState(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  _engine->DoSetSamplerState(1, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);
  _engine->DoSetSamplerState(1, D3DSAMP_SRGBTEXTURE_OLD, 0);

  //noise
  _engine->DoSetTexture(2, res->_noiseTexture._resView);

  _engine->DoSetSamplerState(2, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_WRAP_OLD);
  _engine->DoSetSamplerState(2, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_WRAP_OLD);
  _engine->DoSetSamplerState(2, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
  _engine->DoSetSamplerState(2, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
  _engine->DoSetSamplerState(2, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD);
  _engine->DoSetSamplerState(2, D3DSAMP_SRGBTEXTURE_OLD, 0);

  float depthwidth	= _engine->Width();
  float depthheight	= _engine->Height();

  //---------------------------------------------------------------------------------------
  //render SSAO info RTTSSAO buffer
  //---------------------------------------------------------------------------------------
  float rendwidth	  = res->_SSAOWidth;
  float rendheight	= res->_SSAOHeight;

  float noisewidth	= res->_texWidth;
  float noiseheight	= res->_texHeight;

  D3DXVECTOR4 vec191192[2];
  //half pixel offsets for current and for noise texture, VSH
  vec191192[0] = D3DXVECTOR4(
    0.5f/depthwidth,
    0.5f/depthheight,
    0.5f/noisewidth,
    0.5f/noiseheight);

  //to convert from <0..1> screen space into noise texture space
  vec191192[1] = D3DXVECTOR4(
    rendwidth/noisewidth,
    rendheight/noiseheight,
    0.0f,
    0.0f);

  D3DXVECTOR4 vec[5];

  vec[0] = D3DXVECTOR4(
    1.0f/depthwidth,
    1.0f/depthheight,
    0.0f,
    0.0f);

  float radnear = _params._nearradius;
  float radfar  = _params._farradius;;
  vec[1] = D3DXVECTOR4(
    radnear*radfar,
    radnear - radfar,
    radfar,
    0.0f);

  float dfarplane = Glob.config.GetShadowZ();
  float distfar   = max(0.01f, _params._fardist);
  float distnear  = min(_params._neardist, distfar - 0.05f);
  float sqrdist   = distfar/dfarplane;
  float sqrblurdepth = _params._depthblurdist/dfarplane;
  vec[2] = D3DXVECTOR4(
    dfarplane/distfar,
    distnear/dfarplane,
    (1.0f - 0.025f)/(0.025f*sqrdist*sqrdist),
    (1.0f - 0.025f)/(0.025f*sqrblurdepth*sqrblurdepth));  //0.025 is a param in equation 1/(1 + a*d^2)

  //fog
  float endf   = fogEnd / dfarplane;
  float startf = fogStart / dfarplane;
  float fogexp = fogCoef;

  vec[4] = D3DXVECTOR4(
    endf,
    1.0f / (endf - startf),
    fogexp*dfarplane,
    0.0f);

  //image operations
  float x0		= _params._treshold0;
  float x1		= _params._treshold1;
  float delta = 1.0f/max((x1 - x0), 0.001f);
  float intensity = _params._intensity*exp(fogexp*10);  //fog in 10 m distance
  vec[3] = D3DXVECTOR4(
    delta,
    -delta*x0,
    intensity,
    0.0f);

  //set constants
  _engine->DoSetShaderConstantF(&PSC_ssaoPixelOffset, 5*4, 0, Array<float>(vec[0], 5*4));
  _engine->DoSetShaderConstantF(&VSC_SSAOPixelOffsets, 2*4, 0, Array<float>(vec191192[0], 2 * 4));

  //remember old render targets
  //we cannot use Push/Pop functions here, because recording threads may be using them
  RenderTargetDepthStencilInfo oldRTinfo(_engine->_currRenderTarget, _engine->_currDepthStencil, _engine->_minZRange, _engine->_maxZRange);

#ifdef SSAO_DEBUG
  if (ssaosave)
    D3DXSaveSurfaceToFile("C:\\origRT.png", D3DXIFF_PNG, origRT, 0, 0);
#endif

  _engine->SetRenderTargetAndDepthStencilDirect(res->_rtSSAO, DepthStencil::DepthStencilNULL, 0, 1);
  Color clearColor(HBlack);
  device->ClearRenderTargetView(res->_rtSSAO._rtView, Array<float>((float*)&clearColor, 4));

  _engine->DoSetVertexShader(res->_vsSSAO._shader, res->_vsSSAO._inputLayout);
  _engine->DoSetPixelShader(res->_psSSAO);

  //draw 
  _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);
  
  #ifdef _XBOX
    CALL_D3D_MEMBER(_engine, Resolve, D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, res->_rtSSAO, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
  #endif

  #ifdef SSAO_DEBUG
  if (ssaosave)
  {
    D3DXSaveTextureToFile("c:/ssao.tga", D3DXIFF_TGA, res->_rtSSAO, NULL);
    D3DXSaveTextureToFile("c:/depth.dds", D3DXIFF_DDS, depth, NULL);
  }
  #endif
  //---------------------------------------------------------------------------------------
  //to half resolution
  //---------------------------------------------------------------------------------------
  if (smallres)
  {
    _engine->SetRenderTargetAndDepthStencilDirect(res->_rtSmall, DepthStencil::DepthStencilNULL, 0, 1);

     D3DXVECTOR4 vec191b = D3DXVECTOR4 (
      0.5/rendwidth, 
      0.5/rendheight, 
      1.0/rendwidth, 
      1.0/rendheight);

    _engine->DoSetTexture(0, res->_rtSSAO._srView);
    _engine->DoSetShaderConstantF(&VSC_SSAOPixelOffsets,1*4, 0, Array<float>(&vec191b[0], 1*4));

    _engine->DoSetVertexShader(res->_vsSSAODown._shader, res->_vsSSAODown._inputLayout);
    _engine->DoSetPixelShader(res->_psSSAODown);

    //draw 
    _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);

    #ifdef _XBOX
      CALL_D3D_MEMBER(_engine, Resolve, D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, res->_rtSmall, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
    #endif

    #ifdef SSAO_DEBUG
      if (ssaosave)
      {
        D3DXSaveTextureToFile("c:/small.tga", D3DXIFF_TGA, res->_rtSmall, NULL);
      }
    #endif
  }

  //---------------------------------------------------------------------------------------
  //blur image
  //---------------------------------------------------------------------------------------
  vec191192[0] = D3DXVECTOR4(
    smallres ? 0.5/res->_blurWidth: 0.5/rendwidth, 
    smallres ? 0.5/res->_blurHeight : 0.5/rendheight, 
    0.5/depthwidth, 
    0.5/depthheight);	

  vec191192[1] = D3DXVECTOR4(
    1.0, 
    1.0,
    1.0, 
    1.0);	

  D3DXVECTOR4 vec0a = D3DXVECTOR4(
    smallres ? 1.0/res->_blurWidth: 1.0/rendwidth, 
    smallres ? 1.0/res->_blurHeight: 1.0/rendheight, 
    1.0/depthwidth, 
    1.0/depthheight);	

  _engine->DoSetShaderConstantF(&PSC_ssaoPixelOffset, 1 * 4, 0, Array<float>(&vec0a[0], 1 * 4));
  _engine->DoSetShaderConstantF(&VSC_SSAOPixelOffsets,  2 * 4, 0, Array<float>(vec191192[0], 2 * 4));

  for (int i = 0; i < _pInterp._lastPars._ssaoblurpasses; i++)
  {
    //horizontal blur   
    _engine->DoSetTextureDirect(1, 0); //unset texture before setting render target
    _engine->SetRenderTargetAndDepthStencilDirect(res->_rtBlur, DepthStencil::DepthStencilNULL, 0, 1);

    #ifdef SSAO_DEBUG
      _engine->DoSetPixelShader((ssaoblurdepth && _params._depthblurdist > 0) ? res->_psBlurHorzDepth : res->_psBlurHorz);
    #else
      _engine->DoSetPixelShader(_params._depthblurdist > 0 ? res->_psBlurHorzDepth : res->_psBlurHorz);
    #endif
    
    _engine->DoSetTexture(1, smallres ? res->_rtSmall._srView : res->_rtSSAO._srView);
    _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2); 

    #ifdef _XBOX
      CALL_D3D_MEMBER(_engine, Resolve, D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, res->_rtBlur, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
    #endif
   
    //vertical blur
    _engine->DoSetTextureDirect(1, 0); //unset texture before setting render target
    _engine->SetRenderTargetAndDepthStencilDirect(smallres ? res->_rtSmall : res->_rtSSAO, DepthStencil::DepthStencilNULL, 0, 1);

    #ifdef SSAO_DEBUG
      _engine->DoSetPixelShader((ssaoblurdepth && _params._depthblurdist > 0) ? res->_psBlurVertDepth : res->_psBlurVert);
    #else
      _engine->DoSetPixelShader(_params._depthblurdist > 0 ? res->_psBlurVertDepth : res->_psBlurVert);
    #endif
   
    _engine->DoSetTexture(1, res->_rtBlur._srView);
    _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);

    #ifdef _XBOX
      CALL_D3D_MEMBER(_engine, Resolve, D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, res->_rtSSAO, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
    #endif
  }

  //---------------------------------------------------------------------------------------
  //render SSAO into final image
  //---------------------------------------------------------------------------------------
  #ifdef SSAO_DEBUG
  if (ssaosave)
  {
    D3DXSaveTextureToFile("c:/blur.tga", D3DXIFF_TGA, smallres ? res->_rtSmall : res->_rtSSAO, NULL);  
    ssaosave = false;
  }
  #endif

  // Restore original RT & Depth
  _engine->SetRenderTargetAndDepthStencilDirect(oldRTinfo._renderTarget, oldRTinfo._depthStencil, oldRTinfo._minZRange, oldRTinfo._maxZRange);

   D3DXVECTOR4 vec191c = D3DXVECTOR4(
     smallres ? 0.5/res->_blurWidth: 0.5/rendwidth, 
     smallres ? 0.5/res->_blurHeight : 0.5/rendheight, 
     0.0, 
     0.0);	
 
   _engine->DoSetShaderConstantF(&VSC_SSAOPixelOffsets, 1*4, 0, Array<float>(&vec191c[0], 1*4));
 
   _engine->DoSetTexture(1, smallres ? res->_rtSmall._srView : res->_rtSSAO._srView);
   
   _engine->DoSetRenderState(D3DRS_BLENDOP_OLD, D3DBLENDOP_ADD_OLD);
   _engine->DoSetRenderState(D3DRS_ALPHABLENDENABLE_OLD, TRUE);
   _engine->DoSetRenderState(D3DRS_SRCBLEND_OLD, _bDebug ? D3DBLEND_ONE_OLD : D3DBLEND_ZERO_OLD);
   _engine->DoSetRenderState(D3DRS_DESTBLEND_OLD, _bDebug ? D3DBLEND_ZERO_OLD : D3DBLEND_SRCCOLOR_OLD);
 
   _engine->DoSetPixelShader(_bDebug ? res->_psSSAODebug : res->_psSSAOFinal);
 
   _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);
  
  _engine->DoSetPixelShader(NULL);
  _engine->DoSetVertexShader(NULL, NULL);

  // restore SamplerState 0, fixed: news:hrc1q6$5ah$1@new-server.localdomain
  _engine->DoSetSamplerState(0, D3DSAMP_BORDERCOLOR_OLD, 0x0);

  // restore RenderState
  _engine->DoSetRenderState(D3DRS_ZWRITEENABLE_OLD, TRUE);
  _engine->DoSetRenderState(D3DRS_ZENABLE_OLD, TRUE);
  _engine->DoSetRenderState(D3DRS_CULLMODE_OLD, D3DCULL_CCW_OLD);
  _engine->DoSetRenderState(D3DRS_ALPHABLENDENABLE_OLD, FALSE);
    
  // SetPixelShaderConstant_A_DFORCED_E_Black
  Color newE = HBlack;
  Color zero = Color(0, 0, 0, 0);

  _engine->DoSetShaderConstantF(&PSC_AE, 1*4, 0, Array<float>((float*)&newE, 1*4));
  _engine->DoSetShaderConstantF(&PSC_GE, 1*4, 0, Array<float>((float*)&newE, 1*4));
  _engine->DoSetShaderConstantF(&PSC_DForced, 1*4, 0, Array<float>((float*)&zero, 1*4));

  // SetPixelShaderConstantDBSZero
  static const Color zeroes[3]={Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0)};
  _engine->DoSetShaderConstantF(&PSC_Diffuse, 3*4, 0, Array<float>((float*)zeroes, 3*4));
}

//////////////////////////////////////////////////////////////////////////
//
// FilmGrain postprocess effect
//
//////////////////////////////////////////////////////////////////////////
void EngineDD11::PPFilmGrain::Prepare()
{
  const PPFilmGrainResources *res = Resources();
  if (!res) return;

  const int cb = -1;
  base::Prepare();

  // Set the stream source
  _engine->SetStreamSource(cb, res->_vb, sizeof(VertexP3T2), 4);
  // Set the vertex shader
  _engine->SetVertexShader(cb, res->_vs);
}

bool EngineDD11::PPFilmGrain::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, bool firstInit)
{
  if (!base::Init(hlsl,vs)) return false;
  
  Ref<PPFilmGrainResources> resrc = new PPFilmGrainResources;

  resrc->_invtexWidth    = 1.0f;
  resrc->_invtexHeight   = 1.0f;

  resrc->_texWidth       = 1;
  resrc->_texHeight      = 1;

  // setup initial data
  Vertex v[4];
  float offsetU = 0;
  float offsetV = 0;
  v[0].pos = Vector3P(-0.5, -0.5, 0.5); v[0].t0.u = 0 + offsetU; v[0].t0.v = 1 + offsetV;
  v[1].pos = Vector3P(-0.5,  0.5, 0.5); v[1].t0.u = 0 + offsetU; v[1].t0.v = 0 + offsetV;
  v[2].pos = Vector3P( 0.5, -0.5, 0.5); v[2].t0.u = 1 + offsetU; v[2].t0.v = 1 + offsetV;
  v[3].pos = Vector3P( 0.5,  0.5, 0.5); v[3].t0.u = 1 + offsetU; v[3].t0.v = 0 + offsetV;

  // Create the VB
  ComRef<IDirect3DVertexBuffer9Old> vb;

  D3D11_BUFFER_DESC bd;
  bd.Usage = D3D11_USAGE_DEFAULT;
  bd.ByteWidth = sizeof(Vertex) * 4;
  bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = 0;
  bd.MiscFlags = 0;

  D3D11_SUBRESOURCE_DATA srData;
  srData.pSysMem = v;
  srData.SysMemPitch = 0;
  srData.SysMemSlicePitch = 0;

  HRESULT hr = _engine->GetD3DDeviceNonRec()->CreateBuffer(&bd, &srData, vb.Init());
  if (FAILED(hr))
  {
    LogF("Error: Vertex buffer creation failed.");
    return false;
  }

  // Create the VD
  const D3D11_INPUT_ELEMENT_DESC vertexDecl[] =
  {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  // create vertex shader
  if (!CompileVertexShader(vs,"VSPostProcess",resrc->_vs, vertexDecl, sizeof(vertexDecl)/sizeof(vertexDecl[0])))
  {
    return false;
  }

  if (!CompilePixelShader(hlsl,"PSPostProcessFilmGrainColor", resrc->_psColor, false))
    return false;

  if (!CompilePixelShader(hlsl,"PSPostProcessFilmGrainMono", resrc->_psMono, false))
    return false;

  //create noise texture
  const int texsize = 128;
 
  // create initial data
  AutoArray<AutoArray<DWORD>> initData;

  //for each mipmap 
  int mipSize = texsize << 1;
  while (mipSize >>= 1)
  {
    int index = initData.Add();
    AutoArray<DWORD>& iData = initData[index];
    iData.Resize(mipSize*mipSize);

    //for each texel
    for (int y = 0; y < mipSize; y++)
    {
      for (int x = 0; x < mipSize; x++)
      {
        iData[x + mipSize*y] = 
          (((unsigned int) (GRandGen.RandomValue()*255.0f)) << 16) + 
          (((unsigned int) (GRandGen.RandomValue()*255.0f)) << 8) + 
          ((unsigned int) (GRandGen.RandomValue()*255.0f)); 
      }
    }
  }

  AutoArray<D3D11_SUBRESOURCE_DATA> srTexData;
  srTexData.Resize(initData.Size());
  mipSize = texsize;
  for (int i=0; i < initData.Size(); ++i)
  {
    srTexData[i].pSysMem = initData[i].Data();
    srTexData[i].SysMemPitch = mipSize * sizeof(DWORD);
    srTexData[i].SysMemSlicePitch = 0;

    mipSize >>= 1;
  }

  // create texture
  SurfaceDataD3D11 noiseTexture;
  if (!CreateTextureHelper(_engine->GetD3DDeviceNonRec(), texsize, texsize, 0, DXGI_FORMAT_R8G8B8A8_UNORM, 
    srTexData.Data(), noiseTexture))
    return false;


  // assign to shared resources
  resrc->_vb = VertexStaticData11::BufferFuture(vb);
  resrc->_noiseTexture = noiseTexture;

  resrc->_invtexWidth  = 1.0f/texsize;
  resrc->_invtexHeight = 1.0f/texsize;

  resrc->_texWidth     = texsize;
  resrc->_texHeight    = texsize;

  _resrc = resrc;

  if (firstInit)
  {
    SetDefaultValues();
    _enabled = false;
    _pInterp.Initialize();
  }

  return true;
}

//==================================================================================================
void EngineDD11::PPFilmGrain::SetDefaultValues()
{
  _pInterp._pars._sharpness     = 1.5f;
  _pInterp._pars._intensity	    = 0.1f;
  _pInterp._pars._grainSize	    = 1.7f;
  _pInterp._pars._intensityX0	  = 0.2f;
  _pInterp._pars._intensityX1	  = 1.0f;

  _pInterp._pars._monochromatic = true;
}

//==================================================================================================
bool EngineDD11::PPFilmGrain::Init(PPResources *resources, bool firstInit)
{
  if (resources)
  {
    _resrc = resources;

    if (firstInit)
    {
      SetDefaultValues();
      _enabled = false;
      _pInterp.Initialize();
    }

    return true;
  }

  return false;
}


//==================================================================================================
bool EngineDD11::PPFilmGrain::UnInit()
{
  if (_resrc)
  {
    _resrc->UnInit();

    return true;
  }

  return false;
}

//==================================================================================================
int EngineDD11::PPFilmGrain::SetParams(const float *pars, int nPars)
{
  if(nPars > 0)
    setIntensity(pars[0]);

  if(nPars > 1)
    setSharpness(pars[1]);

  if(nPars > 2)
    setGrainSize(pars[2]);

  if(nPars > 3)
    setIntensityX0(pars[3]);

  if(nPars > 4)
    setIntensityX1(pars[4]);

  if(nPars > 5)
    SetMonochromatic(fabs(pars[4]) < 0.01f);

  return 0;
}

//==================================================================================================
int EngineDD11::PPFilmGrain::SetParams(const AutoArray<float> &pars)
{
  int nPars = pars.Size();

  if(nPars > 0)
    setIntensity(pars[0]);

  if(nPars > 1)
    setSharpness(pars[1]);

  if(nPars > 2)
    setGrainSize(pars[2]);

  if(nPars > 3)
    setIntensityX0(pars[3]);

  if(nPars > 4)
    setIntensityX1(pars[4]);

  if(nPars > 5)
    SetMonochromatic(fabs(pars[4]) < 0.01f);

  return 0;
}

//==================================================================================================
LSError EngineDD11::PPFilmGrain::Serialize( ParamArchive &ar )
{
  CHECK(ar.Serialize("enabled",_enabled,0,false));
  CHECK(ar.Serialize("interp", _pInterp, 0));
  CHECK(ar.Serialize("prior", _priority, 0, -1));

  return LSOK;
}


//==================================================================================================
void EngineDD11::PPFilmGrain::Do(bool isLast)
{
  if (!_enabled)
    return;

  // Draw
  PROFILE_SCOPE_GRF_EX(ppFilmGrain,pp,SCOPE_COLOR_YELLOW);

  // If scene is not being rendered initialize scene
  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();

  const PPFilmGrainResources *res = Resources();
  if (!res) return;
  
  //interpolated parameters
  FilmGrainPar pars = _pInterp.Simulate();

  // Prepare drawing
  Prepare();
  const int cb = -1;

  SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);

  // Stage 0 - base texture - do the copy (to make sure same data is both in source texture and destination render target)
  TextureHandle11* rt = _engine->CreateRenderTargetCopyAsTexture();

  // if effect is last then switch to back buffer
  if (isLast) _engine->SwitchToBackBufferRenderTarget();

  //0 texture is source frame buffer
  SetTextureTemp tTex0(0, rt);
  SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0MIPFILTER(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);
  SetSamplerStateTemp ss0ADDRESSU(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss0ADDRESSV(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

  //1 texture is uniform noise
  SetTextureTemp tTex1(1, res->_noiseTexture._resView);
  SetSamplerStateTemp ss1MINFILTER(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss1MAGFILTER(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss1MIPFILTER(1, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss1ADDRESSU(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_WRAP_OLD);
  SetSamplerStateTemp ss1ADDRESSV(1, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_WRAP_OLD);

  //noise texture 
  float randmovew = toIntFloor(GRandGen.RandomValue()*res->_texWidth);
  float randmoveh = toIntFloor(GRandGen.RandomValue()*res->_texHeight);

  float texw      = (float) _engine->Width()/ (float) res->_texWidth;
  float texh      = (float) _engine->Height()/ (float) res->_texHeight;

  //parameters
  static bool  stop = false;

  if (stop)
  {
    randmoveh = 0.0f;
    randmovew = 0.0f;
  }

  D3DXVECTOR4 vec1 = D3DXVECTOR4(
    pars._intensity, 
    pars._sharpness, 
    pars._grainSize - 1.0f, 
    0.0f);

  //to compute intensity of pixel according the line, function y = kx + q = saturate((X1 - X0)*x + X0),
  //where X0 is intensity of black pixel and X1 is intensity of white pixel
  D3DXVECTOR4 vec2 = D3DXVECTOR4(
    pars._intensityX1 - pars._intensityX0, 
    pars._intensityX0, 
    0.0f, 
    0.0f);

  SetShaderConstantTemp PS0(PSC_ScreenNoise, D3DXVECTOR4(texw, texh, texw*res->_invtexWidth*(0.5f + randmovew), texh*res->_invtexHeight*(0.5f + randmoveh)));
  SetShaderConstantTemp PS1(PSC_FilmGrainParams, vec1);
  SetShaderConstantTemp PS2(PSC_FilmGrainParams2, vec2);

  //pixel shader according the mode
  _engine->SetPixelShader(cb, pars._monochromatic ? res->_psMono : res->_psColor);

  //draw 
  _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);

  #ifdef _XBOX
    // Resolve content of the render target
    CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _engine->_currRenderTarget, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
  #endif

  if (!inScene) _engine->D3DEndScene();
  Finish();
}


//////////////////////////////////////////////////////////////////////////
bool EngineDD11::PPDOF::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, const Ref<PPGaussianBlur> &ppGaussianBlur)
{
  if (!base::Init(hlsl,vs,"VSPostProcess")) return false;
  if (!CompilePixelShader(hlsl,"PSPostProcessDOF", _ps, true)) return false;
  _ppGaussianBlur = ppGaussianBlur;
  _useFarDistance = false;
  return true;
}

void EngineDD11::PPDOF::Do(bool isLast)
{
  // Draw
  PROFILE_SCOPE_GRF_EX(ppDOF,pp,SCOPE_COLOR_YELLOW);

  const int cb = -1;
  
  // If scene is not being rendered initialize scene
  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();

  // Prepare drawing
  Prepare();

  // Set temporary render states
  SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);

  // Stage 0 - base texture - do the copy (to make sure same data is both in source texture and destination render target)
  TextureHandle11* rt = _engine->CreateRenderTargetCopyAsTexture();
  SetTextureTemp tTex0(0, rt);
#ifndef _X360SHADERGEN
  SetSamplerStateTemp ss0SRGBTEXTURE(0, D3DSAMP_SRGBTEXTURE_OLD, _engine->_caps._rtIsSRGB);
#endif
  SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0MIPFILTER(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0ADDRESSU(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss0ADDRESSV(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

  // Stage 1 - blurred texture
  SetTextureTemp tTex1(1, _ppGaussianBlur->GetBlurredRt()._srView);
#ifndef _X360SHADERGEN
  SetSamplerStateTemp ss1SRGBTEXTURE(1, D3DSAMP_SRGBTEXTURE_OLD, _engine->_caps._rtIsSRGB);
#endif
  SetSamplerStateTemp ss1MINFILTER(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss1MAGFILTER(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss1MIPFILTER(1, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss1ADDRESSU(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss1ADDRESSV(1, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

  // Stage 2 - depth map
  SetTextureTemp tTex2(2, _engine->GetDepthAsTexture());
#ifndef _X360SHADERGEN
  SetSamplerStateTemp ss2SRGBTEXTURE(2, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
#endif
  SetSamplerStateTemp ss2MINFILTER(2, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
  SetSamplerStateTemp ss2MAGFILTER(2, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
  SetSamplerStateTemp ss2MIPFILTER(2, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD);
  SetSamplerStateTemp ss2ADDRESSU(2, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss2ADDRESSV(2, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

  // Prepare PS constants
  // with zoomed camera / binoculars there is more blur
  Matrix4Val proj = _engine->CBState(cb)._proj;
  
  AspectSettings as;
  GEngine->GetAspectSettings(as);

  static float adjustZoom = 0.5f;
  float zoom = proj(0,0)/as.leftFOV*adjustZoom;

  //1.3 TOH - frenkee
  float blurFactor = _useFarDistance ? 1.5f : zoom;

  static float minBlurFactor = 1;
  static float maxBlurFactor = 10;
  saturate(blurFactor,minBlurFactor,maxBlurFactor);
  static float minZoom = 1;
  static float maxZoom = 2.5; // we want to avoid too large offsets
  saturate(zoom,minZoom,maxZoom);

  const float blurRadiusInPixels = 3.0f*zoom*_engine->_blurCoefThis;
  
  float invW = 1.0f / _engine->Width();
  float invH = 1.0f / _engine->Height();

  #ifdef _M_PPC
    float zfar		= GScene->GetCamera()->Far();
    float znear		= GScene->GetCamera()->Near();
    float zdelta	= zfar - znear;

    const float depth		  = 110.0f;
    const float testdepth	= 100.0f;

    float maxdepth	= zdelta/depth;

    float invFocalPlane = _engine->_focusDist/testdepth;

    //to recompute from nonlinear z buffer to linear
    SetShaderConstantTemp spc2(2, D3DXVECTOR4(maxdepth*znear, zfar, zdelta, 0));

    //do not know now why on xbox the effect is more visible, so make it less visible
    blurFactor *= 0.18f;

  #else
   float invFocalPlane = 1.0f/_engine->_focusDistThis;

   static float blurmulfar	= 1.25f;
   static float blurmulzoom	= 2.0f;

   blurFactor = blurFactor*(_useFarDistance ? blurmulfar : blurmulzoom);
  #endif

//  SetShaderConstantTemp spc1(1, D3DXVECTOR4(invFocalPlane, invFocalPlane, invFocalPlane, invFocalPlane));
  static float xblurfactor = 1.0f;
  static float xblurpower  = 2.0f;

  SetShaderConstantTemp spc0(PSC_BlurRadius, D3DXVECTOR4(invW * blurRadiusInPixels, invH * blurRadiusInPixels, blurFactor, 0.0f));
  SetShaderConstantTemp spc1(PSC_PlaneXBlur, D3DXVECTOR4(invFocalPlane, xblurfactor, xblurpower, 0.0f));

  // Drawing
  _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#ifdef _XBOX
  // Resolve content of the render target
  CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _engine->_currRenderTarget, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif

  // If scene is not being rendered finish initialized scene
  if (!inScene) _engine->D3DEndScene();

  // Restore usual rendering settings
  Finish();
}

//////////////////////////////////////////////////////////////////////////

bool EngineDD11::PPDistanceDOF::Init(
  const LazyLoadFile &hlsl, const LazyLoadFile &vs, PPGaussianBlur *ppGaussianBlur
)
{
  if (!grandBase::Init(hlsl,vs,"VSPostProcess")) return false;
  if (!CompilePixelShader(hlsl,"PSPostProcessDistanceDOF", _ps, true)) return false;
  _ppGaussianBlur = ppGaussianBlur;
  _useFarDistance = true;
  return true;
}

bool EngineDD11::PPRain::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs)
{
  if (!base::Init(hlsl,vs,"VSRain")) return false;
  if (!CompilePixelShader(hlsl,"PSRain", _ps, true)) return false;
  return true;
}



//////////////////////////////////////////////////////////////////////////

struct EdgeParams
{
  float u[4][4];
  float v[4][4];
  float posOffset[4];

  // \param minSrcTexCoord - minimal texture coord of source
  // \param maxSrcTexCoord - maximal texture coord of source
  void SetupBilinear(int dstW, int dstH, int srcW, int srcH, float minSrcTexCoord = 0.0f, float maxSrcTexCoord = 1.0f)
  {
    // for pos. offset explanation cf. DX docs:
    // Directly Mapping Texels to Pixels 
    // Texture Coordinates
    // TODO: verify if this works well for 2x and 4x decimation case as well
    posOffset[0] = 0; //-0.5f/dstW;
    posOffset[1] = 0; //-0.5f/dstH;
    posOffset[2] = posOffset[3] = 0;
#if _ARMA3_HDR_IMPROVEMENTS
    float srcSizeCoef = maxSrcTexCoord - minSrcTexCoord;
    srcW *= srcSizeCoef;
    saturateMax(srcW, 1);
    srcH *= srcSizeCoef;
    saturateMax(srcH, 1);
#endif
    // we may perform any decimation, like 2x or 4x decimation
    int decimationW = srcW/dstW;
    int decimationH = srcH/dstH;

    const float sampOffsetU = 1.0/srcW;
    const float sampOffsetV = 1.0/srcH;

    // Computation differs from DX9.
    // We have to shift coordinates because of the way the texel coordinate system in DX11
    // This should work for both linear and point filtering
    // See Coordinate Systems (Direct3D 10)
    const float centerTexelOffsetU = (1.0f/dstW) * 0.5f;
    const float centerTexelOffsetV = (1.0f/dstH) * 0.5f;

    float baseU[4] = {minSrcTexCoord, minSrcTexCoord, maxSrcTexCoord, maxSrcTexCoord};
    float baseV[4] = {maxSrcTexCoord, minSrcTexCoord, maxSrcTexCoord, minSrcTexCoord};
    static const float nU[4] = {0.25, 0.25,  0.75, 0.75};
    static const float nV[4] = {0.25, 0.75, 0.25,  0.75};
    for (int vertex = 0; vertex < 4; vertex++)
    {
      for (int t = 0; t < 4; t++)
      {
        u[vertex][t] = baseU[vertex] + nU[t] * sampOffsetU * decimationW - centerTexelOffsetU;
        v[vertex][t] = baseV[vertex] + nV[t] * sampOffsetV * decimationH - centerTexelOffsetV;
      }
    }
  }
};

bool EngineDD11::PPDecimate::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs)
{
  if (!base::Init(hlsl,vs)) return false;

  // Create the low resolution textures cascade

  // we need textures for 4x average decimation
  // and then some for 2x avg/min/max decimation
  // we want to start calculating min/max once the picture is coarse enough
  const int startMinMaxAtSize = 80;

  int fullWidth = _engine->Width();
  int fullHeight = _engine->Height();

  // decimation 2x uses point sampling and should therefore be very accurate
  // we will round to nearest bigger pow2
  int w = roundTo2PowerNCeil(fullWidth);
  int h = roundTo2PowerNCeil(fullHeight);

  int maxSize = max(w,h);
  int minSize = min(w,h);

  DXGI_FORMAT rtFormat = _engine->_downsampleFormat;
  int level = 0;
  // we need at least one 4x decimation step
  do
  {
    DecimationPass &dec = _dec[level];
    w = w>>2;
    h = h>>2;
    maxSize = maxSize>>2;
    minSize = minSize>>2;
    // create the supplementary RT
    int effW = max(w,1);
    int effH = max(h,1);
    LogF("Decimation 4x: %d,%d",effW,effH);
    HRESULT err = _engine->CreateRenderTarget(effW, effH, rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, dec._rt);
    if (FAILED(err))
    {
      _engine->DDError11("Post process Low resolution texture creation failed",err);
      return false;
    }
    dec._w = effW;
    dec._h = effH;
    // proceed to next level
    level++;
  } while (minSize>startMinMaxAtSize);
  _decimations = level;

  // Create measurement textures
  const DecimationPass &smallestDec = _dec[_decimations - 1];

  HRESULT err = _engine->CreateRenderTarget(smallestDec._w, smallestDec._h, rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, _rtMeasure);
  if (FAILED(err))
  {
    _engine->DDError11("Post process Low resolution texture creation failed",err);
    return false;
  }

  // Create depth buffer (as it must be set in order the query to work with some ATI graphics cards)
  err = _engine->CreateDepthStencil(smallestDec._w, smallestDec._h, DXGI_FORMAT_D24_UNORM_S8_UINT, 1, 0, D3D11_USAGE_DEFAULT, false, _rtMeasureDepth);
  if (FAILED(err))
  {
    _engine->DDError11("Measure depth buffer creation failed",err);
    return false;
  }

  //////////////////////////////////////////////////////////////////////////

#if _HELISIM_RENDER_TO_TEXTURE
  w = h = R2TMaxResolution >> 2;

  DecimationPass &dec0 = _decR2T[0];
  err = _engine->CreateRenderTarget(w, h, rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, dec0._rt);
  if (FAILED(err))
  {
    _engine->DDError11("Post process Low resolution r2t texture creation failed", err);
    return false;
  }
  _decR2T[0]._w = w;
  _decR2T[0]._h = h;

  w = h = R2TMinResolution;
  DecimationPass &dec1 = _decR2T[1];
  err = _engine->CreateRenderTarget(w, h, rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, dec1._rt);
  if (FAILED(err))
  {
    _engine->DDError11("Post process Low resolution r2t texture creation failed", err);
    return false;
  }
  _decR2T[1]._w = w;
  _decR2T[1]._h = h;

  err = _engine->CreateRenderTarget(R2TMinResolution, R2TMinResolution, rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, _rtMeasureR2T);
  if (FAILED(err))
  {
    _engine->DDError11("Post process Low resolution r2t texture creation failed", err);
    return false;
  }

  // Create depth buffer (as it must be set in order the query to work with some ATI graphics cards)
  err = _engine->CreateDepthStencil(R2TMinResolution, R2TMinResolution, DXGI_FORMAT_D24_UNORM_S8_UINT, 1, 0, D3D11_USAGE_DEFAULT, false, _rtsMeasureDepthR2T);
  if (FAILED(err))
  {
    _engine->DDError11("Measure r2t depth buffer creation failed",err);
    return false;
  }
#endif

  //////////////////////////////////////////////////////////////////////////

  {
    // setup initial data
    VertexCustomEdge v[4];
    v[0].pos = Vector3P(-0.5, -0.5, 0.5); v[0].index = 0;
    v[1].pos = Vector3P(-0.5,  0.5, 0.5); v[1].index = 1;
    v[2].pos = Vector3P( 0.5, -0.5, 0.5); v[2].index = 2;
    v[3].pos = Vector3P( 0.5,  0.5, 0.5); v[3].index = 3;

    // Create the VB
    ComRef<IDirect3DVertexBuffer9Old> vb;

    D3D11_BUFFER_DESC bd;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(VertexCustomEdge) * 4;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;
    bd.MiscFlags = 0;

    D3D11_SUBRESOURCE_DATA srData;
    srData.pSysMem = v;
    srData.SysMemPitch = 0;
    srData.SysMemSlicePitch = 0;

    HRESULT hr = _engine->GetD3DDeviceNonRec()->CreateBuffer(&bd, &srData, vb.Init());
    if (FAILED(hr))
    {
      LogF("Error: Vertex buffer creation failed.");
      return false;
    }

    _vbCustomEdge = VertexStaticData11::BufferFuture(vb);

    // Create the VD
    const D3D11_INPUT_ELEMENT_DESC vertexDecl[] =
    {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"BLENDINDICES", 0, DXGI_FORMAT_R32_FLOAT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    // create vertex shader
    // note: vertex shader is stored in PS.hlsl for simple effects
    if (!CompileVertexShader(vs,"VSPostProcessCustomEdge4T",_vsCustomEdge, vertexDecl, sizeof(vertexDecl)/sizeof(vertexDecl[0])))
    {
      return false;
    }
  }

  // Create the shaders
  if (!CompilePixelShader(hlsl,"PSPostProcessDownSampleThermalLuminance",_psLuminance))
  {
    return false;
  }
  if (!CompilePixelShader(hlsl,"PSPostProcessDownSampleAvgLuminance",_psAvgLuminance))
  {
    return false;
  }
  if (!CompilePixelShader(hlsl,"PSPostProcessQuerySimple",_psQuery))
  {
    return false;
  }

  // Initialize the histogram values
  for (int i = 0; i < NBrightQueries; i++)
  {
    _histogram[i] = -1.0f;
  }

  return true;
}

#define _SAVE_RTs_TO_FILES_ 0

#if _HELISIM_RENDER_TO_TEXTURE
void EngineDD11::PPDecimate::DoDirect(const RECT *rect, int idx)
{
  PROFILE_SCOPE_GRF_EX(ppDec,ppR2t,SCOPE_COLOR_YELLOW);

  int width = rect->right - rect->left;
  int height = rect->bottom - rect->top;

  // remember all we can
  PushStreamSource pshStreamSource(true);
  PushVertexShader pshVertexShader(true);
  PushPixelShader pshPixelShader(true);
  PushRenderTargetAndView pushRTV(true);

  // Set the vertex shader + stream source
  _engine->DoSetVertexShader(_vsCustomEdge._shader, _vsCustomEdge._inputLayout);

  DoAssert(_engine->_r2tTargets[idx]._tiPars.NotNull());
  R2TTiPars &tiPars = *(_engine->_r2tTargets[idx]._tiPars.GetRef());

  _engine->DoSetStreamSource(_vbCustomEdge, sizeof(VertexCustomEdge), 0); //size not needed, so we can set it to 0

  // Set temporary render states
  SetRenderStateTemp rstemp1(D3DRS_ZENABLE_OLD, FALSE, true);
  SetRenderStateTemp rstemp2(D3DRS_ZENABLE_OLD, FALSE, true);
  SetRenderStateTemp rstemp3(D3DRS_ZWRITEENABLE_OLD, FALSE, true);
  // Set the src blend and dest blend somehow (they are not going to be set in the DoPrepareTriangle and the temporary setting returning would set a undefined value)
  SetRenderStateTemp rstemp4(D3DRS_ALPHABLENDENABLE_OLD, FALSE, true);
  SetRenderStateTemp rstemp5(D3DRS_SRGBWRITEENABLE_OLD, FALSE, true);
  SetRenderStateTemp rstemp6(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD, true);

  SetSamplerStateTemp sstemp1(0, D3DSAMP_SRGBTEXTURE_OLD, FALSE, true);
  SetSamplerStateTemp sstemp2(1, D3DSAMP_SRGBTEXTURE_OLD, FALSE, true);
  SetSamplerStateTemp sstemp3(2, D3DSAMP_SRGBTEXTURE_OLD, FALSE, true);
  SetSamplerStateTemp sstemp4(3, D3DSAMP_SRGBTEXTURE_OLD, FALSE, true);

  // by default assume bi-linear filtering
  SetSamplerStateTemp sstemp5(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
  SetSamplerStateTemp sstemp6(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
  SetSamplerStateTemp sstemp7(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
  SetSamplerStateTemp sstemp8(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
  SetSamplerStateTemp sstemp9(2, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
  SetSamplerStateTemp sstemp10(2, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
  SetSamplerStateTemp sstemp11(3, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
  SetSamplerStateTemp sstemp12(3, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD, true);

  SetSamplerStateTemp sstemp13(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD, true);
  SetSamplerStateTemp sstemp14(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD, true);
  SetSamplerStateTemp sstemp15(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD, true);
  SetSamplerStateTemp sstemp16(1, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD, true);
  SetSamplerStateTemp sstemp17(2, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD, true);
  SetSamplerStateTemp sstemp18(2, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD, true);
  SetSamplerStateTemp sstemp19(3, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD, true);
  SetSamplerStateTemp sstemp20(3, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD, true);

  MyD3DDeviceCB& device = _engine->GetD3DDeviceRec();

  EdgeParams pars;
  {
#if _ARMA3_HDR_IMPROVEMENTS
    float texCoef = 1.0f - _engine->GetHDRLuminanceRectCoef();
    saturate(texCoef, 0.0f, 1.0f);
    texCoef *= 0.5f;

    float minTexCoord = 0.0f + texCoef;
    float maxTexCoord = 1.0f - texCoef;
    pars.SetupBilinear(_decR2T[0]._w, _decR2T[0]._h, width,height, minTexCoord, maxTexCoord);
#else
    pars.SetupBilinear(_decR2T[0]._w, _decR2T[0]._h, width, height);
#endif

    // Create RT copy
    ComRef<TextureHandle11> rtCopy = _engine->CreateR2TRenderTargetCopyAsTexture(rect);

#if _SAVE_RTs_TO_FILES_
    D3DXSaveTextureToFile("C:\\rtCopy.dds", D3DXIFF_DDS, rtCopy, NULL);
#endif

    // Set temporal RT
    _engine->SetRenderTargetAndDepthStencilDirect(_decR2T[0]._rt, DepthStencil::DepthStencilNULL, 0.0f, 1.0f);
    Color clearColor(HBlack);
    device->ClearRenderTargetView(_decR2T[0]._rt._rtView, Array<float>((float*)&clearColor, 4));

    // Set the pixel shader
    _engine->DoSetPixelShader(_psLuminance);
    SetShaderConstantTemp sConst(VSC_PPEdgeU, pars.u[0], 9*4, true);

    SetTextureTemp textTemp0(0, rtCopy, true);
    SetTextureTemp textTemp1(1, rtCopy, true);
    SetTextureTemp textTemp2(2, rtCopy, true);
    SetTextureTemp textTemp3(3, rtCopy, true);

    _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);
  }

  // perform 4x decimation + average calculation
  {
    // Set RT
    _engine->SetRenderTargetAndDepthStencilDirect(_decR2T[1]._rt, DepthStencil::DepthStencilNULL, 0.0f, 1.0f);
    Color clearColor(HBlack);
    device->ClearRenderTargetView(_decR2T[1]._rt._rtView, Array<float>((float*)&clearColor, 4));

#if _SAVE_RTs_TO_FILES_
    D3DXSaveSurfaceToFile("C:\\psLuminance.dds", D3DXIFF_DDS, _decR2T[0]._rts, NULL, NULL);
#endif

    _engine->DoSetPixelShader(_psAvgLuminance);

    SetTextureTemp textTemp0(0, _decR2T[0]._rt._srView, true);
    SetTextureTemp textTemp1(1, _decR2T[0]._rt._srView, true);
    SetTextureTemp textTemp2(2, _decR2T[0]._rt._srView, true);
    SetTextureTemp textTemp3(3, _decR2T[0]._rt._srView, true);

    _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);
  }

  // Do the measurement
  {
    // Issue a query
    static float Alphas[NBrightQueries] = 
    {
      0.0f,
      1.0f / NBrightQueries,
      2.0f / NBrightQueries,
      3.0f / NBrightQueries,
      4.0f / NBrightQueries,
      5.0f / NBrightQueries,
      6.0f / NBrightQueries,
      7.0f / NBrightQueries,
      8.0f / NBrightQueries,
      9.0f / NBrightQueries,
      10.0f / NBrightQueries,
      11.0f / NBrightQueries,
      12.0f / NBrightQueries,
      13.0f / NBrightQueries,
      14.0f / NBrightQueries,
      15.0f / NBrightQueries
    };

    
    SetRenderStateTemp rsTemp7(D3DRS_ZENABLE_OLD, TRUE, true);
    SetRenderStateTemp rsTemp8(D3DRS_ZFUNC_OLD, D3DCMP_ALWAYS_OLD, true);

    _engine->SetRenderTargetAndDepthStencilDirect(_rtMeasureR2T, _rtsMeasureDepthR2T, 0.0f, 1.0f);
    Color clearColor(HBlack);
    device->ClearRenderTargetView(_rtMeasureR2T._rtView, Array<float>((float*)&clearColor, 4));

    
    SetTextureTemp textTemp0(0, _decR2T[1]._rt._srView, true);
    _engine->DoSetPixelShader(_psQuery);

#if _SAVE_RTs_TO_FILES_
    D3DXSaveSurfaceToFile("C:\\avgLum.dds", D3DXIFF_DDS, _decR2T[1]._rts, NULL, NULL);
#endif
    tiPars._queryR2T->IssueDirect(NULL, Alphas, -1.0f);
    tiPars._queryR2T->GetNumberOfPixelsDrawn(tiPars._temperaturesR2T, NULL, true);
  }
}
#endif //#if _HELISIM_RENDER_TO_TEXTURE


void EngineDD11::PPDecimate::Do(bool isLast)
{
  PROFILE_SCOPE_GRF_EX(ppDec,pp,SCOPE_COLOR_YELLOW);
  
  const int cb = -1;
  
  Prepare();

  // Set the vertex shader + stream source
  _engine->SetStreamSource(cb,_vbCustomEdge, sizeof(VertexCustomEdge), 4);
  _engine->SetVertexShader(cb,_vsCustomEdge);

  // Set temporary states
  SetRenderStateTemp rsSRGBSRGBWRITEENABLE(D3DRS_SRGBWRITEENABLE_OLD, FALSE);
  SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);
  // no alpha or fog done here
  // DX11 TODO
  //SetRenderStateTemp rsFOGENABLE(D3DRS_FOGENABLE, FALSE);
  SetRenderStateTemp rsALPHATESTENABLE(D3DRS_ALPHATESTENABLE_OLD, FALSE);
  SetRenderStateTemp rsALPHABLENDENABLE(D3DRS_ALPHABLENDENABLE_OLD, FALSE);
  // no SRGB
  SetSamplerStateTemp ss0SRGBTEXTURE(0, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
  SetSamplerStateTemp ss1SRGBTEXTURE(1, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
  SetSamplerStateTemp ss2SRGBTEXTURE(2, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
  SetSamplerStateTemp ss3SRGBTEXTURE(3, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
  // by default assume bi-linear filtering
  SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss1MINFILTER(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss1MAGFILTER(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss2MINFILTER(2, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss2MAGFILTER(2, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss3MINFILTER(3, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss3MAGFILTER(3, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  // tri-linear filtering never done 
  SetSamplerStateTemp ss0MIPFILTER(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD);
  SetSamplerStateTemp ss1MIPFILTER(1, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD);
  SetSamplerStateTemp ss2MIPFILTER(2, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD);
  SetSamplerStateTemp ss3MIPFILTER(3, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD);
  // clamping all stages
  SetSamplerStateTemp ss0ADDRESSU(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss0ADDRESSV(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss1ADDRESSU(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss1ADDRESSV(1, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss2ADDRESSU(2, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss2ADDRESSV(2, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss3ADDRESSU(3, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss3ADDRESSV(3, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

  // Save VS constants and restore them at the end
  // DX11 TODO remove or repair 
  //PushVertexShaderConstant pushC0(0);
  //PushVertexShaderConstant pushC1(1);
  //PushVertexShaderConstant pushC2(2);
  //PushVertexShaderConstant pushC3(3);
  //PushVertexShaderConstant pushC4(4);
  //PushVertexShaderConstant pushC5(5);
  //PushVertexShaderConstant pushC6(6);
  //PushVertexShaderConstant pushC7(7);
  //PushVertexShaderConstant pushC8(8);

  // Save current render target and depth stencil
  _engine->PushRenderTargetDepthStencil();

  // perform initial luminance calculation + 4x decimation
  {
    EdgeParams pars;

#if _ARMA3_HDR_IMPROVEMENTS
    float texCoef = 1.0f - _engine->GetHDRLuminanceRectCoef();
    saturate(texCoef, 0.0f, 1.0f);
    texCoef *= 0.5f;

    float minTexCoord = 0.0f + texCoef;
    float maxTexCoord = 1.0f - texCoef;
    pars.SetupBilinear(_dec[0]._w,_dec[0]._h,_engine->Width(),_engine->Height(), minTexCoord, maxTexCoord);
#else
    pars.SetupBilinear(_dec[0]._w,_dec[0]._h,_engine->Width(),_engine->Height());
#endif
    _engine->SetVertexShaderConstantF(cb,VSC_PPEdgeU, pars.u[0], 9);

    // Set the pixel shader
    _engine->SetPixelShader(cb,_psLuminance);

    // Remember the current render target (prior we set the temporary one)
    TextureHandle11* rt = _engine->CreateRenderTargetCopyAsTexture();

     // Set temporarily render target and depth stencil to horizontally shrunk texture
    _engine->SetRenderTargetAndDepthStencil(_dec[0]._rt, DepthStencil::DepthStencilNULL, 0, 1);

    {
      // DX11 - should be already set in SetRenderTargetAndDepthStencil
      //_engine->SetViewport(_dec[0]._w, _dec[0]._h, 0, 1);
    }

    // set the source as 4 textures    
    SetTextureTemp tTex0(0, rt);
    SetTextureTemp tTex1(1, rt);
    SetTextureTemp tTex2(2, rt);
    SetTextureTemp tTex3(3, rt);

    _engine->DrawPrimitive(cb,D3DPT_TRIANGLESTRIP_OLD, 0, 2);
    
    _engine->ResolveRenderTarget();
  }

  // perform 4x decimation + average calculation
  {
    for (int i=1; i<_decimations; i++)
    {
      // perform initial luminance calculation + 4x decimation
      EdgeParams pars;
      pars.SetupBilinear(_dec[i]._w,_dec[i]._h,_dec[i-1]._w,_dec[i-1]._h);

      _engine->SetVertexShaderConstantF(cb,VSC_PPEdgeU, pars.u[0], 9);

      // Set the pixel shader
      _engine->SetPixelShader(cb,_psAvgLuminance);

      // Set the render target (we can use the SetRenderTarget method here, because we know the original is already stored and w)
      _engine->SetRenderTargetAndDepthStencil(_dec[i]._rt, DepthStencil::DepthStencilNULL, 0, 1);

      {
        // DX11 TODO - should be already set in SetRenderTargetAndDepthStencil
        //_engine->SetViewport(_dec[i]._w, _dec[i]._h, 0, 1);
      }

      TextureHandle11* tex = _dec[i-1]._rt._srView;

      // set the source as 4 textures    
      SetTextureTemp tTex0(0, tex);
      SetTextureTemp tTex1(1, tex);
      SetTextureTemp tTex2(2, tex);
      SetTextureTemp tTex3(3, tex);

      _engine->DrawPrimitive(cb,D3DPT_TRIANGLESTRIP_OLD, 0, 2);
      _engine->ResolveRenderTarget();
    }
  }

  // Do the measurement
  {
    // It looks Z-buffer must be enabled in order the occlusion query to work on some ATI HW
    // see http://discussms.hosting.lsoft.com/SCRIPTS/WA-MSD.EXE?A2=ind0502B&L=DIRECTXDEV&D=0&I=-3&X=60AD7D5C1D013612AF&P=3658
    SetRenderStateTemp rsZENABLE(D3DRS_ZENABLE_OLD, TRUE);
    SetRenderStateTemp rsZFUNC(D3DRS_ZFUNC_OLD, D3DCMP_ALWAYS_OLD);

    _engine->SetRenderTargetAndDepthStencil(_rtMeasure, _rtMeasureDepth);
    _engine->SetTexture(cb,0, _dec[_decimations-1]._rt._srView);
    _engine->SetPixelShader(cb,_psQuery);

    // Issue a query
    float alphas[NBrightQueries];
    for (int i = 0; i < NBrightQueries; i++)
    {
      alphas[i] = (float)i / NBrightQueries;
    }
    _query.Issue(NULL, alphas, -1.0f);
  }

  // Build histogram
  {
    // Get pixel counts for every layer
    int numberOfPixelsDrawn[NBrightQueries];
    _query.GetNumberOfPixelsDrawn(numberOfPixelsDrawn, NULL, true);

    // Build the histogram
    int totalPixels = _dec[_decimations - 1]._w * _dec[_decimations - 1]._h;
    for (int i = 0; i < NBrightQueries - 1; i++)
    {
      _histogram[i] = (float)(numberOfPixelsDrawn[i] - numberOfPixelsDrawn[i + 1]) / totalPixels;
      _temperatures[i] = (numberOfPixelsDrawn[i]);
    }
    _histogram[NBrightQueries - 1] = (float)numberOfPixelsDrawn[NBrightQueries - 1] / totalPixels;
    _temperatures[NBrightQueries - 1] = numberOfPixelsDrawn[NBrightQueries - 1];

    if (CHECK_DIAG(DETIHistogram))
    {
      DIAG_MESSAGE(200, Format(" 0: %f, %d", _histogram[0], _temperatures[0]));
      DIAG_MESSAGE(200, Format(" 1: %f, %d", _histogram[1], _temperatures[1]));
      DIAG_MESSAGE(200, Format(" 2: %f, %d", _histogram[2], _temperatures[2]));
      DIAG_MESSAGE(200, Format(" 3: %f, %d", _histogram[3], _temperatures[3]));
      DIAG_MESSAGE(200, Format(" 4: %f, %d", _histogram[4], _temperatures[4]));
      DIAG_MESSAGE(200, Format(" 5: %f, %d", _histogram[5], _temperatures[5]));
      DIAG_MESSAGE(200, Format(" 6: %f, %d", _histogram[6], _temperatures[6]));
      DIAG_MESSAGE(200, Format(" 7: %f, %d", _histogram[7], _temperatures[7]));
      DIAG_MESSAGE(200, Format(" 8: %f, %d", _histogram[8], _temperatures[8]));
      DIAG_MESSAGE(200, Format(" 9: %f, %d", _histogram[9], _temperatures[9]));
      DIAG_MESSAGE(200, Format("10: %f, %d", _histogram[10], _temperatures[10]));
      DIAG_MESSAGE(200, Format("11: %f, %d", _histogram[11], _temperatures[11]));
      DIAG_MESSAGE(200, Format("12: %f, %d", _histogram[12], _temperatures[12]));
      DIAG_MESSAGE(200, Format("13: %f, %d", _histogram[13], _temperatures[13]));
      DIAG_MESSAGE(200, Format("14: %f, %d", _histogram[14], _temperatures[14]));
      DIAG_MESSAGE(200, Format("15: %f, %d", _histogram[15], _temperatures[15]));
    }
  }

  // Restore the original render target and depth stencil buffer
  _engine->PopRenderTargetDepthStencil();
}

int EngineDD11::PPFinalThermal::SetParams(const float *pars, int nPars)
{
  Assert(nPars==1);
  _deltaT = pars[0];

  return 0;
}

bool EngineDD11::PPFinalThermal::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs)
{
  if (!base::Init(hlsl,vs,"VSPostProcess")) return false;
  if (!CompilePixelShader(hlsl,"PSPostProcessThermalPresent", _ps)) return false;
  if (!CompilePixelShader(hlsl,"PSPostProcessThermal", _psThermal)) return false;
  if (!_ppDecimate.Init(hlsl, vs)) return false;
  if (!_ppGaussianBlur.Init(hlsl, vs)) return false;

  UINT width = _engine->Width();
  UINT height = _engine->Height();

  HRESULT err = _engine->CreateRenderTarget(width, height, _engine->_rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, _rt);
  if (err != S_OK)
  {
    _engine->DDError11("Error: Render target texture creation failed.", err); 
    return false;
  }

  //////////////////////////////////////////////////////////////////////////

#if _HELISIM_RENDER_TO_TEXTURE

  err = _engine->CreateRenderTarget(R2TMaxResolution, R2TMaxResolution, _engine->_rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, _rtTextureR2T);
  if (err != S_OK)
  {
    _engine->DDError11("Error: Render target texture creation failed.", err); 
    return false;
  }

  err = _engine->CreateRenderTarget(R2TMaxResolution / 4, R2TMaxResolution / 4, _engine->_rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, _blurRT);
  if (err != S_OK)
  {
    _engine->DDError11("Error: Blur texture creation failed.", err);
    return false;
  }

#endif

  //////////////////////////////////////////////////////////////////////////

  _deltaT = 0;

  return true;
}

// TODO: avoid scene here
#include "../scene.hpp"
#include "../txtPreload.hpp"

#if _HELISIM_RENDER_TO_TEXTURE
void EngineDD11::PPFinalThermal::DoDirect(const RECT *rect, int idx)
{

  PROFILE_SCOPE_GRF_EX(ppTR2,ppR2t,SCOPE_COLOR_YELLOW);

  MyD3DDevice& device = _engine->GetD3DDeviceNonRec();

  // remember all we can
  PushStreamSource pshStreamSource(true);
  PushVertexShader pshVertexShader(true);
  PushPixelShader pshPixelShader(true);
  PushRenderTargetAndView pushRTV(true);

  R2TTiPars &tiPars = *(_engine->_r2tTargets[idx]._tiPars.GetRef());

  // Perform temperature calculations
  _ppDecimate.DoDirect(rect, idx);

  float dTemp = (float) (TempMax - TempMin) / NBrightQueries;
  const int MinPixelsDrawn = 25;
  float maxTemperature = 0.0f;

  for (int i = 0; i < NBrightQueries; i++)
  {
    if (tiPars._temperaturesR2T[i] > MinPixelsDrawn) maxTemperature = (i + 1) * dTemp;
  }

  static float latency = 0.85f;
  maxTemperature += (dTemp * 0.25f);
  tiPars._maxTemperatureR2T = (tiPars._maxTemperatureR2T + (maxTemperature - tiPars._maxTemperatureR2T) * tiPars._deltaT* latency);

  const float TemperatureMin = 0.5f;
  const float TemperatureMax = 30.0f;
  const float TemeperatureInvDiff = 1.0f / (TemperatureMax - TemperatureMin);

  float tempDiff = (tiPars._maxTemperatureR2T - _engine->_airTemperature);
  saturate(tempDiff, TemperatureMin, TemperatureMax);
  float dT = (tempDiff - TemperatureMin) * TemeperatureInvDiff;
  tempDiff *= (1.0f - sqrtf(dT)) + 1.0f;

  float tempRangeMin = _engine->_airTemperature - tempDiff;
  float tempRangeMax = _engine->_airTemperature + tempDiff;
  saturateMax(tempRangeMin, 0.0f);

  float tempRangeMin01 = (tempRangeMin - TempMin) * InvTempDiff;
  float tempRangeMax01 = (tempRangeMax - TempMin) * InvTempDiff;
  float invTempRangeDiff = 1.0f / (tempRangeMax01 - tempRangeMin01);

  {
    SetRenderStateTemp rsTemp0(D3DRS_ZENABLE_OLD, FALSE, true);

    // Set stream
    _engine->DoSetStreamSource(_vb, sizeof(Vertex), 0); //size not needed, so we set it to 0
    // Set the vertex declaration
    _engine->DoSetVertexShader(_vs._shader, _vs._inputLayout);
    // Set pixel shader
    _engine->DoSetPixelShader(_psThermal);

    _engine->SetRenderTargetAndDepthStencilDirect(_rtTextureR2T, DepthStencil::DepthStencilNULL, 0, 1);
    Color clearColor(HBlack);
    device->ClearRenderTargetView(_rtTextureR2T._rtView, Array<float>((float*)&clearColor, 4));

    ComRef<TextureHandle11> rt = _engine->CreateR2TRenderTargetCopyAsTexture(rect);
    SetTextureTemp texTemp0(0, rt, true);
    SetTextureTemp texTemp1(1, _engine->_tiConversion._resView, true);

    SetRenderStateTemp rsTemp1(D3DRS_ZENABLE_OLD, FALSE, true);

    SetSamplerStateTemp ssTemp1(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD, true);
    SetSamplerStateTemp ssTemp2(1, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD, true);

    float psConst[4] = { tempRangeMin01, tempRangeMax01, invTempRangeDiff, tiPars._tiMode };
    _engine->DoSetShaderConstantF(&PSC_TempRangeMin01_TempRangeMax01_InvTempRangeDiff_TIMode, 1*4, 0, Array<float>(psConst, 4));
    _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);
  }

  // Switch to back buffer
  _engine->SetRenderTargetAndDepthStencilDirect(_engine->_r2tRT._renderTargetS, DepthStencil::DepthStencilNULL, 0, 1);
  Color clearColor(HBlack);
  device->ClearRenderTargetView(_engine->_r2tRT._renderTargetS._rtView, Array<float>((float*)&clearColor, 4));

  // Set viewport
  _engine->SetViewportDirect(0, 0, rect->right - rect->left, rect->bottom - rect->top, 0, 1);

  // Fake blur
  if (_engine->_pp._stretchRect)
    _engine->_pp._stretchRect->DoDirect(_rtTextureR2T._srView, NULL, _blurRT, NULL);

#if _SAVE_RTs_TO_FILES_
  D3DXSaveSurfaceToFile("C:\\FakeBlur.dds", D3DXIFF_DDS, _blurRTS, NULL, NULL);
#endif

  SetTextureTemp texTemp0(0, _rtTextureR2T._srView, true);
  SetTextureTemp texTemp1(1, _blurRT._srView, true);

  SetSamplerStateTemp ssTemp0(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD, true);
  SetSamplerStateTemp ssTemp1(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD, true);
  SetSamplerStateTemp ssTemp2(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
  SetSamplerStateTemp ssTemp3(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD, true);

  _engine->DoSetPixelShader(_ps);

  static float TiBlurCoeff = 0.5f;
  float psConst[4] = { 0, 0, 0, TiBlurCoeff };
  _engine->DoSetShaderConstantF(&PSC_BlurFactor, 1*4, 0, Array<float>(psConst, 1*4));

  _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#if _SAVE_RTs_TO_FILES_
  D3DXSaveSurfaceToFile("C:\\final.dds", D3DXIFF_DDS, _engine->_r2tRT._renderTargetSurfaceS, NULL, NULL);
#endif

}
#endif //#if _HELISIM_RENDER_TO_TEXTURE

void EngineDD11::PPFinalThermal::Do(bool isLast)
{
  PROFILE_SCOPE_GRF_EX(ppThr,pp,SCOPE_COLOR_YELLOW);
 
  const int cb = -1;
  
  // If the _currRenderTargetSurface is equal to _backBuffer, then it's strange and we probably have got a problem here
  DoAssert(_engine->_currRenderTarget._resource != _engine->_backBuffer._resource);

#if _ENABLE_CHEATS
  /*if (GInput.GetCheat2ToDo(DIK_F9))
  {
    _engine->SetTIBrightnessRelative(_engine->GetTIBrightness() + 1.0f);
    DIAG_MESSAGE(500, Format("TI Brightness: %f", _engine->GetTIBrightness()));
  }
  if (GInput.GetCheat2ToDo(DIK_F10))
  {
    _engine->SetTIBrightnessRelative(_engine->GetTIBrightness() - 1.0f);
    DIAG_MESSAGE(500, Format("TI Brightness: %f", _engine->GetTIBrightness()));
  }
  if (GInput.GetCheat2ToDo(DIK_F11))
  {
    _engine->_tiContrast += 1.0f;
    DIAG_MESSAGE(500, Format("TI Contrast: %f", _engine->_tiContrast));
  }
  if (GInput.GetCheat2ToDo(DIK_F12))
  {
    _engine->_tiContrast -= 1.0f;
    DIAG_MESSAGE(500, Format("TI Contrast: %f", _engine->_tiContrast));
  }*/
  if (GInput.GetCheat2ToDo(DIK_F8))
  {
    _engine->_tiMode++;
    DIAG_MESSAGE(500, Format("TI Mode: %d", _engine->_tiMode));
  }
  if (GInput.GetCheat2ToDo(DIK_F7))
  {
    _engine->_tiMode--;
    DIAG_MESSAGE(500, Format("TI Mode: %d", _engine->_tiMode));
  }
  if (GInput.GetCheat2ToDo(DIK_F6))
  {
    _engine->_tiBlurCoef += 0.1f;
    if (_engine->_tiBlurCoef > 1.001f) _engine->_tiBlurCoef = 0.0f;
    DIAG_MESSAGE(500, Format("TI Blur level: %f", _engine->_tiBlurCoef));
  }
  if (GInput.GetCheat2ToDo(DIK_F1))
  {
    ForceTIAlive = !ForceTIAlive;
    DIAG_MESSAGE(500, Format("ForceTIAlive: %s", ForceTIAlive ? "on" : "off"));
  }
  if (GInput.GetCheat2ToDo(DIK_F2))
  {
    ForceTIMovement = !ForceTIMovement;
    DIAG_MESSAGE(500, Format("ForceTIMovement: %s", ForceTIMovement ? "on" : "off"));
  }
  if (GInput.GetCheat2ToDo(DIK_F3))
  {
    ForceTIMetabolism = !ForceTIMetabolism;
    DIAG_MESSAGE(500, Format("ForceTIMetabolism: %s", ForceTIMetabolism ? "on" : "off"));
  }
  if (GInput.GetCheat2ToDo(DIK_F5))
  {
    _engine->_tiAutoBC = !_engine->_tiAutoBC;
    DIAG_MESSAGE(500, Format("TI AutoContrast: %s", _engine->_tiAutoBC ? "on" : "off"));
  }
#endif

  // Perform the automatic setting of brightness and contrast
  if (_engine->_tiAutoBC)
  {
    // Get the temperature histogram (decode the temperature)
    _ppDecimate.Do(isLast);

    // Calculate the expected value
    //float e = 0.0f;
    float maxTemperature = 0.0f;

    float dTemp = (float) (TempMax - TempMin) / NBrightQueries;

    // 1% per pixels from image(4096)
    static int MinPixelsDrawn = 100;

    for (int i = 0; i < NBrightQueries; i++)
    {
      //e += i * _ppDecimate._histogram[i];
      if (_ppDecimate._temperatures[i] > MinPixelsDrawn) maxTemperature = (i + 1) * dTemp;
    }

#if 0
    // Calculate the variance
    float var = 0.0f;
    for (int i = 0; i < NBrightQueries; i++)
    {
      var += (i-e) * (i-e) * _ppDecimate._histogram[i];
    }
   
    // Convert e and var to temperatures
    float tempE = TempMin + (TempMax - TempMin) * (e + 0.5f) / NBrightQueries;
    float tempVAR = TempMin + (TempMax - TempMin) * var / NBrightQueries;

    // Calculate the desired brightness and contrast
    static float ConstrastMin = 8.0f;
    static float ContrastMult = 5.0f;

    // Calculate the desired brightness and contrast
    float brightnessWanted = tempE;
    float contrastWanted = max(tempVAR * ContrastMult, ConstrastMin);
#endif

    float maxTempWanted = maxTemperature + (dTemp * 0.25f);

    // Consider some latency and change the real values accordingly
    static float latency = 0.85f;

#if 0
    _engine->SetTIBrightness(_engine->_tiBrightness + (brightnessWanted - _engine->_tiBrightness) * _deltaT * latency);
    _engine->SetTIContrast(_engine->_tiContrast + (contrastWanted - _engine->_tiContrast) * _deltaT * latency);
#endif
    _engine->_maxTemperature = (_engine->_maxTemperature + (maxTempWanted - _engine->_maxTemperature) * _deltaT * latency);
  }
  else
  {
    _engine->SetTIBrightness(_engine->_tiBrightness);
    _engine->SetTIContrast(_engine->_tiContrast);
    _engine->_maxTemperature = 30.0f;
  }

  // Do the thermal visualization (decode the temperature)
  {
    // Start the scene
    bool inScene = _engine->_d3dFrameOpen;
    if (!inScene) _engine->D3DBeginScene();

    // Prepare drawing
    Prepare();

    // Save current render target and depth stencil
    _engine->PushRenderTargetDepthStencil();

    // Rewrite the pixel shader
    _engine->SetPixelShader(cb,_psThermal);

    // Current render target as texture
    TextureHandle11* rt = _engine->CreateRenderTargetCopyAsTexture();
    DoAssert(rt);

    // Rewrite render target and depth stencil surfaces
    _engine->SetRenderTargetAndDepthStencil(_rt, DepthStencil::DepthStencilNULL);

    {
      int width = 0;
      int height = 0;
      _engine->GetShaderResourceDimensions(rt, width, height);
      _engine->SetViewport(width, height, 0, 1);
    }

    SetTextureTemp tTex0(0, rt);
    SetSamplerStateTemp ss0SRGBTEXTURE(0, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
    SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
    SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
    SetSamplerStateTemp ss0ADDRESSU(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
    SetSamplerStateTemp ss0ADDRESSV(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

    // Get the TIConversion texture to the input
    SetTextureTemp tTex1(1, _engine->_tiConversion._resView);
    SetSamplerStateTemp ss1SRGBTEXTURE(1, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
    SetSamplerStateTemp ss1MINFILTER(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
    SetSamplerStateTemp ss1MAGFILTER(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
    SetSamplerStateTemp ss1ADDRESSU(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
    SetSamplerStateTemp ss1ADDRESSV(1, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

    // Set temporary render states
    SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);
    SetRenderStateTemp rsZENABLE(D3DRS_ZENABLE_OLD, FALSE);
#ifndef _X360SHADERGEN
    // DX11 TODO
    //SetRenderStateTemp rsFOGENABLE(D3DRS_FOGENABLE, FALSE);
#endif
    SetRenderStateTemp rsSRGBWRITEENABLE(D3DRS_SRGBWRITEENABLE_OLD, FALSE);

    // Control the temperature range to be displayed by constants
    // const float tempRangeMin = 0.0f;
    // const float tempRangeMax = 60.0f;
    // #define TempRangeMin01 0.18181818182f //(tempRangeMin - TempMin) * InvTempDiff
    // #define TempRangeMax01 0.63636363636f //(tempRangeMax - TempMin) * InvTempDiff
    // #define InvTempRangeDiff 2.2f // 1.0f / (tempRangeMax01 - tempRangeMin01)

    static float TemperatureMin = 0.5f;
    static float TemperatureMax = 30.0f;
    static float TemeperatureInvDiff = 1.0f / (TemperatureMax - TemperatureMin);

    float tempDiff = (_engine->_maxTemperature - _engine->_airTemperature);
    saturate(tempDiff, TemperatureMin, TemperatureMax);
    float dT = (tempDiff - TemperatureMin) * TemeperatureInvDiff;
    tempDiff *= (1.0f - sqrtf(dT)) + 1.0f;

    float tempRangeMin = _engine->_airTemperature - tempDiff;
    float tempRangeMax = _engine->_airTemperature + tempDiff;
    saturateMax(tempRangeMin, 0.0f);

    float tempRangeMin01 = (tempRangeMin - TempMin) * InvTempDiff;
    float tempRangeMax01 = (tempRangeMax - TempMin) * InvTempDiff;
    float invTempRangeDiff = 1.0f / (tempRangeMax01 - tempRangeMin01);

    if (CHECK_DIAG(DETIHistogram))
    {
      DIAG_MESSAGE(150, "dT: %.3f, temp: %.3f", dT, tempDiff);
      DIAG_MESSAGE(150, "min: %.3f, max: %.3f", tempRangeMin, tempRangeMax);
      DIAG_MESSAGE(150, "min: %.3f, max: %.3f, diff: %.3f", tempRangeMin01, tempRangeMax01, invTempRangeDiff);
    }

    SetShaderConstantTemp spc0(PSC_TempRangeMin01_TempRangeMax01_InvTempRangeDiff_TIMode, D3DXVECTOR4(tempRangeMin01, tempRangeMax01, invTempRangeDiff, _engine->_tiMode));

    // Drawing
    _engine->DrawPrimitive(cb,D3DPT_TRIANGLESTRIP_OLD, 0, 2);

    // Restore the original render target and depth stencil buffer
    _engine->PopRenderTargetDepthStencil();
  }

  {
    // Blur the render texture
    _ppGaussianBlur.Do(false, _rt._srView);


    PROFILE_SCOPE_GRF_EX(ppFin,pp,SCOPE_COLOR_YELLOW);

    bool inScene = _engine->_d3dFrameOpen;
    if (!inScene) _engine->D3DBeginScene();

    // Prepare drawing
    Prepare();

    // Set temporary render states
    SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);
    SetRenderStateTemp rsZENABLE(D3DRS_ZENABLE_OLD, FALSE);
#ifndef _X360SHADERGEN
    // DX11 TODO
    //SetRenderStateTemp rsFOGENABLE(D3DRS_FOGENABLE, FALSE);
#endif

    // Use the back buffer as render target
    _engine->SwitchToBackBufferRenderTarget();

    int width = 0;
    int height = 0;
    _engine->GetRenderTargetDimensions(_engine->_backBuffer, width, height);
    _engine->SetViewport(width, height, 0, 1);

    // get blurred target
    TextureHandle11* rtBlur = _ppGaussianBlur.GetBlurredRt()._srView;
    DoAssert(rtBlur);

    // Use the textures
    SetTextureTemp tRenderTarget0(0, _rt._srView);
    SetTextureTemp tRenderTarget1(1, rtBlur);

    // Disable SRGB
#ifndef _X360SHADERGEN
    SetSamplerStateTemp tempNoSRGB0(0, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
    SetSamplerStateTemp tempNoSRGB1(1, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
    SetSamplerStateTemp tempMINFILTER0(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
    SetSamplerStateTemp tempMAGFILTER0(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
    SetSamplerStateTemp tempMINFILTER1(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
    SetSamplerStateTemp tempMAGFILTER1(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
#endif

    // Set the blur factor
    SetShaderConstantTemp spc0(PSC_BlurFactor, D3DXVECTOR4(0, 0, 0, _engine->_tiBlurCoef));

    // Drawing
    _engine->DrawPrimitive(cb,D3DPT_TRIANGLESTRIP_OLD, 0, 2);

    if (!inScene) _engine->D3DEndScene();
  }

  Finish();
}

//////////////////////////////////////////////////////////////////////////

EngineDD11 *EngineDD11::QueryManager::_engine;

EngineDD11::QueryManager::QueryManager()
{
#ifndef _X360SHADERGEN
  D3D11_QUERY_DESC desc;
  desc.Query = D3D11_QUERY_OCCLUSION;
  desc.MiscFlags = 0;

  // Create the query objects
  for (int i = 0; i < NItems; i++)
  {
    for (int b=0; b<NBrightQueries; b++)
    {
      if (FAILED(_engine->GetD3DDeviceNonRec()->CreateQuery(&desc, _query[i][b].Init())))
      {
        LogF("Warning: Current HW doesn't support queries");
        goto Break;
      }
    }
  }
  Break:
#endif

  // Initialize the startup values
  ResetQueries();
}

void EngineDD11::QueryManager::ResetQueries()
{
  // Initialize the startup values
  _newQuery = 0;
  _oldestQuery = 0;
  for (int b=0; b<NBrightQueries; b++)
  {
    _mostRecentResult[b] = -1;
  }
  _queriesIssued = 0;
  _currentResultIndex = 0;
}

bool EngineDD11::QueryManager::Issue(const float *value, const float *alpha, float masterValue)
{
  Assert(!_engine->IsCBRecording());
  const int cb = -1;
  
  if (_query[_newQuery][0].IsNull()) return false;
  if (_queriesIssued>=NItems) return false;

  // DX11 TODO - repair or remove
  //PushPixelShaderConstant psc7(7);
  for (int b=0; b<NBrightQueries; b++)
  {
    const float queryDot[4]={b,0,0,alpha[b]};
    _engine->SetPixelShaderConstantF(cb,PSC_QueryDot, queryDot,1);
    _engine->GetD3DDeviceNonRec()->Begin(_query[_newQuery][b]);
    _value[_newQuery][b] = (value) ? value[b] : -1.0f;
    _engine->DrawPrimitive(cb,D3DPT_TRIANGLESTRIP_OLD, 0, 2);
    _engine->GetD3DDeviceNonRec()->End(_query[_newQuery][b]);
  }
  _masterValue[_newQuery] = masterValue;
  _queriesIssued++;
  DoAssert(_queriesIssued<=NItems);
  _newQuery = Next(_newQuery);
  return true;
}

bool EngineDD11::QueryManager::IssueDirect(const float *value, const float *alpha, float masterValue)
{
  if (_query[_newQuery][0].IsNull()) return false;
  if (_queriesIssued>=NItems) return false;

  // DX11 TODO - repair or remove
  //PushPixelShaderConstant psc7(7);
  for (int b=0; b<NBrightQueries; b++)
  {
    const float queryDot[4]={b,0,0,alpha[b]};
    _engine->DoSetShaderConstantF(&PSC_QueryDot, 1*4, 0, Array<float>((float*)queryDot, 1*4));
    _engine->GetD3DDeviceRec()->Begin(_query[_newQuery][b]);
    _value[_newQuery][b] = (value) ? value[b] : -1.0f;
    _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);
    _engine->GetD3DDeviceRec()->End(_query[_newQuery][b]);
  }
  _masterValue[_newQuery] = masterValue;
  _queriesIssued++;
  DoAssert(_queriesIssued<=NItems);
  _newQuery = Next(_newQuery);
  return true;
}

float EngineDD11::QueryManager::GetNumberOfPixelsDrawn(int *npd, float *value, bool usePixelCount)
{
  #if _DEBUG
  // we may want to introduce artificial latency when debugging
  static int latency = 1;
  #else
  const int latency = 0;
  #endif
  while (_queriesIssued>latency)
  {
    // Get the newest available query result
    // check from beginning to the last
    // once the last one is done, all are done
    Assert(_currentResultIndex<NBrightQueries);
    UINT64 nPixels;
    HRESULT result = _engine->GetD3DDeviceNonRec()->GetData(_query[_oldestQuery][_currentResultIndex], &nPixels, sizeof(UINT64), 0);
    if (FAILED(result))
    {
      // if query is not ready, break and report what we already have
      break;
    }
    _results[_currentResultIndex] = (usePixelCount) ? nPixels : nPixels>0; // we know we are sampling 1x1 - either 1 or 0 can pass
    _currentResultIndex++;
    if (_currentResultIndex>=NBrightQueries)
    {
      for (int b=0; b<NBrightQueries; b++)
      {
        _mostRecentResult[b] = _results[b];
        _mostRecentResultValue[b] = _value[_oldestQuery][b];
      }
      _mostRecentMasterValue = _masterValue[_oldestQuery];
      // we picked up the result - report it
      // we assume we will be able to pick all other results as well now
      _queriesIssued--;
      _oldestQuery = Next(_oldestQuery);
      _currentResultIndex = 0;
    }
  }
  for (int b=0; b<NBrightQueries; b++)
  {
    if (value) value[b] = _mostRecentResultValue[b];
    npd[b] = _mostRecentResult[b];
  }
  return _mostRecentMasterValue;
}

//////////////////////////////////////////////////////////////////////////

EngineDD11 *EngineDD11::QueryManagerSinglePass::_engine;

EngineDD11::QueryManagerSinglePass::QueryManagerSinglePass()
{
  D3D11_QUERY_DESC desc;
  desc.Query = D3D11_QUERY_OCCLUSION;
  desc.MiscFlags = 0;

#ifndef _X360SHADERGEN
  // Create the query objects
  for (int i = 0; i < NItems; i++)
  {
    if (FAILED(_engine->GetD3DDeviceNonRec()->CreateQuery(&desc, _query[i].Init())))
    {
      LogF("Warning: Current HW doesn't support queries");
      goto Break;
    }
  }
  Break:
#endif

  // Initialize the startup values
  ResetQueries();
}

void EngineDD11::QueryManagerSinglePass::ResetQueries()
{
  // Initialize the startup values
  _newQuery = 0;
  _oldestQuery = 0;
  for (int b=0; b<NBrightQueries; b++)
  {
    _mostRecentResult[b] = -1;
  }
  _queriesIssued = 0;
}

bool EngineDD11::QueryManagerSinglePass::Issue(const float *value, float masterValue, TextureHandle11* alphaTex)
{
  Assert(!_engine->IsCBRecording());
  const int cb = -1;
  
  if (_query[_newQuery].IsNull()) return false;
  if (_queriesIssued>=NItems) return false;
  
  const int alphaStage = 1;
  // DX11 TODO - repair or remove
  //PushPixelShaderConstant psc7(7);
  SetTextureTemp sampling(alphaStage,alphaTex);
  SetSamplerStateTemp samplingMagX(alphaStage,D3DSAMP_MAGFILTER_OLD,D3DTEXF_POINT_OLD);
  SetSamplerStateTemp samplingMinX(alphaStage,D3DSAMP_MINFILTER_OLD,D3DTEXF_POINT_OLD);
  SetSamplerStateTemp samplingMipX(alphaStage,D3DSAMP_MIPFILTER_OLD,D3DTEXF_POINT_OLD);
  SetSamplerStateTemp samplingSrgb(alphaStage,D3DSAMP_SRGBTEXTURE_OLD,FALSE);
  SetSamplerStateTemp samplingClampU(alphaStage,D3DSAMP_ADDRESSU_OLD,D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp samplingClampV(alphaStage,D3DSAMP_ADDRESSV_OLD,D3DTADDRESS_CLAMP_OLD);
  
  _engine->GetD3DDeviceNonRec()->Begin(_query[_newQuery]);
  for (int b=0; b<NBrightQueries; b++)
  {
    _value[_newQuery][b] = (value) ? value[b] : -1.0f;
  }
  _engine->DrawPrimitive(cb,D3DPT_TRIANGLESTRIP_OLD, 0, 2);
  _engine->GetD3DDeviceNonRec()->End(_query[_newQuery]);
  _masterValue[_newQuery] = masterValue;
  _queriesIssued++;
  DoAssert(_queriesIssued<=NItems);
  _newQuery = Next(_newQuery);
  return true;
}

bool EngineDD11::QueryManagerSinglePass::IssueDirect(const float *value, float masterValue, TextureHandle11 *alphaTex)
{
  if (_query[_newQuery].IsNull()) return false;
  if (_queriesIssued >= NItems) return false;

  const int alphaStage = 1;
  SetTextureTemp ttemp(alphaStage, alphaTex, true);
  SetSamplerStateTemp ssTemp0(alphaStage, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD,true);
  SetSamplerStateTemp ssTemp1(alphaStage,D3DSAMP_MINFILTER_OLD,D3DTEXF_POINT_OLD,true);
  SetSamplerStateTemp ssTemp2(alphaStage,D3DSAMP_MIPFILTER_OLD,D3DTEXF_POINT_OLD,true);
  SetSamplerStateTemp ssTemp3(alphaStage,D3DSAMP_SRGBTEXTURE_OLD,FALSE,true);
  SetSamplerStateTemp ssTemp4(alphaStage,D3DSAMP_ADDRESSU_OLD,D3DTADDRESS_CLAMP_OLD,true);
  SetSamplerStateTemp ssTemp5(alphaStage,D3DSAMP_ADDRESSV_OLD,D3DTADDRESS_CLAMP_OLD,true);

  _engine->GetD3DDeviceRec()->Begin(_query[_newQuery]);
  for (int b=0; b<NBrightQueries; b++)
  {
    _value[_newQuery][b] = (value) ? value[b] : -1.0f;
  }
  _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);
  _engine->GetD3DDeviceRec()->End(_query[_newQuery]);
  _masterValue[_newQuery] = masterValue;
  _queriesIssued++;
  DoAssert(_queriesIssued<=NItems);
  _newQuery = Next(_newQuery);
  return true;
}

static inline int AddSaturated(int a, int b)
{
  long long sum = (long long)a+b;
  if (sum>INT_MAX) sum = INT_MAX;
  return (int)sum;
}

// #ifdef _PROFILE
// #pragma optimize("", off)
// #endif

float EngineDD11::QueryManagerSinglePass::GetNumberOfPixelsDrawn(int *npd, float *value)
{
  #if _DEBUG
  // we may want to introduce artificial latency when debugging
  static int latency = 0;
  #else
  const int latency = 0;
  #endif
  // even with 0 frames ahead we expect to get query latency 1, hence latency here needs to be one frame higher
  int maxLatency = AddSaturated(_engine->_framesAhead,1); 
  
  PROFILE_SCOPE_EX(ppQWt,pp);
  while (_queriesIssued>latency)
  {
    UINT64 nPixels;
    HRESULT ret;
    do 
    {
      // check oldest query first - it has to be completed first
      ret = _engine->GetD3DDeviceNonRec()->GetData(_query[_oldestQuery], &nPixels, sizeof(UINT64), 0);
      if (ret==S_OK) break;
    } while (_queriesIssued>maxLatency); // prevent query latency being too high - this results in mouse latency
    // if query has succeeded, process it
    if (ret==S_OK)
    {
      // value nPixels means nPixels has succeeded, rest has failed

#if 1
      if (nPixels > NBrightQueries)
      {
        LogF("DirectX Query failed - returned %d pixels.", nPixels);
        // This sometimes happens on ATI cards, not sure why - GetData returns invalid number of pixels (random value it seems...).
        // This caused excessive blinking of picture.

        // So it is better to ignore this query... (because eye accom computation was wrong because of it)
      }
      else
#endif
      {
        for (int i=0; i<NBrightQueries; i++)
        {
          _mostRecentResultValue[i] = _value[_oldestQuery][i];
          _mostRecentResult[i] = i<(int)nPixels;
        }
        _mostRecentMasterValue = _masterValue[_oldestQuery];
      }
      // we picked up the result - report it
      // we assume we will be able to pick all other results as well now
      _queriesIssued--;
      _oldestQuery = Next(_oldestQuery);
    }
    else
    {
      break;
    }
  }
  for (int b=0; b<NBrightQueries; b++)
  {
    if (value) value[b] = _mostRecentResultValue[b];
    if (npd) npd[b] = _mostRecentResult[b];
  }
  saturateMax(_engine->_framesAheadDetected,_queriesIssued-1);
  return _mostRecentMasterValue;
}

// #ifdef _PROFILE
// #pragma optimize("", on)
// #endif

/**
we are not interested about very small changes
on the other hand, GPU exposure control does big changes not very often
the change we do not adjust does gradually build up into a bigger change
*/

static const float HistInit[NBrightQueries]=
{
  0.1f,0.36f,0.50f,0.70f,0.80f,0.89f,0.93f,0.95f,
  1.05f,1.07f,1.11f,1.20f,1.30f,1.45f,1.64f,1.99f,
};

/*
// table could be approximated by following function.
// However, experiments led to numerical instability, and as it was for Arma2 patch,
// change in this area was considered too risky

static float HistInit(int x)
{
  // 0 1 2 3 4 5 6 7 8 9 A B C D E F
  //                 *
  //-7-6-5-4-3-2-1 0 1 2 3 4 5 6 7 8
  Assert(x>=0 && x<NBrightQueries);
  const int half = NBrightQueries/2;
  if (x>=half)
  {
    return 1+abs((x-(half-1))*(1.0f/half));
  }
  else
  {
    return 1-abs((x-(half-1))*(1.0f/half));
  }
}
*/
bool EngineDD11::PPGlow::Init(
  const LazyLoadFile &hlsl, const LazyLoadFile &vs, PPGaussianBlur *ppGaussianBlur
)
{
  if (!base::Init(hlsl,vs)) return false;
  
  _ppGaussianBlur = ppGaussianBlur;

  // Create the low resolution textures cascade
  
  // we need textures for 4x average decimation
  // and then some for 2x avg/min/max decimation
  // we want to start calculating min/max once the picture is coarse enough
  const int startMinMaxAtSize = 80;
  
  int fullWidth = _engine->Width();
  int fullHeight = _engine->Height();
  
  // decimation 2x uses point sampling and should therefore be very accurate
  // we will round to nearest bigger pow2
  int w = roundTo2PowerNCeil(fullWidth);
  int h = roundTo2PowerNCeil(fullHeight);

  int maxSize = max(w,h);
  int minSize = min(w,h);
  
  DXGI_FORMAT rtFormat = _engine->_downsampleFormat;
  int level = 0;
  // we need at least one 4x decimation step
  do
  {
    DecimationPass &dec = _dec[level];
    w = w>>2;
    h = h>>2;
    maxSize = maxSize>>2;
    minSize = minSize>>2;
    // create the supplementary RT
    int effW = max(w,1);
    int effH = max(h,1);
    LogF("Decimation 4x: %d,%d",effW,effH);

    HRESULT err = _engine->CreateRenderTarget(effW, effH, rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, dec._rt);
    if (FAILED(err))
    {
      _engine->DDError11("Post process Low resolution texture creation failed",err);
      return false;
    }
    dec._w = effW;
    dec._h = effH;
    // proceed to next level
    level++;
  } while (minSize>startMinMaxAtSize);
  _decimation4x = level;
  // we need at least one 2x decimation step
  do
  {
    DecimationPass &dec = _dec[level];
    w = w>>1;
    h = h>>1;
    maxSize = maxSize>>1;
    minSize = minSize>>1;
    // create the supplementary RT
    // never create less then 1x1
    int effW = max(w,1);
    int effH = max(h,1);
    LogF("Decimation 2x: %d,%d",effW,effH);
    HRESULT err = _engine->CreateRenderTarget(effW, effH, rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, dec._rt);
    if (FAILED(err))
    {
      _engine->DDError11("Post process Low resolution texture creation failed",err);
      return false;
    }
    dec._w = effW;
    dec._h = effH;
    // proceed to next level
    level++;
  } while (maxSize>1);
  _decimation2x = level;

  HRESULT err = _engine->CreateRenderTarget(NBrightQueries, 1, rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, _rtMeasure);
  if (FAILED(err))
  {
    _engine->DDError11("Post process Low resolution texture creation failed",err);
    return false;
  }
  
  // stencil/depth buffer no longer needed here - we can disable it
  {
    HRESULT err = _engine->CreateDepthStencil(NBrightQueries, 1, DXGI_FORMAT_D24_UNORM_S8_UINT, 1, 0, D3D11_USAGE_DEFAULT, false, _rtMeasureDepth);
    if (FAILED(err))
    {
      _engine->DDError11("Measure depth buffer creation failed",err);
      return false;
    }
  }
  
  _rtApertureCurrent = 0;
  DXGI_FORMAT apFormat = _engine->_apertureFormat;
  for (int i=0; i<2; i++)  
  {
    // create the texture
    HRESULT err = _engine->CreateRenderTarget(1, 1, apFormat, 1, 0, D3D11_USAGE_DEFAULT, true, _rtAperture[i]);
    if (FAILED(err))
    {
      _engine->DDError11("Post process aperture texture creation failed",err);
      return false;
    }
    
#ifndef _X360SHADERGEN
    // clear the target to avoid uninitialized value
    {
      float color[4] = {0,0,0,0};
      _engine->GetD3DDeviceNonRec()->ClearRenderTargetView(_rtAperture[i]._rtView, Array<float>(color, 4));
    }
#endif
  }

  {// init the histogram texture
    int histogramSize = 16;

#ifdef _XBOX
    D3DFORMAT histFormat = (D3DFORMAT)MAKELINFMT(_engine->_apertureFormat);
#else
    DXGI_FORMAT histFormat = _engine->_apertureFormat;
#endif
    
    if (_engine->_apertureFormat==DXGI_FORMAT_R16_FLOAT)
    {
      AutoArray<DirectX::PackedVector::HALF> vec16;
      vec16.Resize(histogramSize);
      ConvertFloat32ToHalfArray(vec16.Data(), HistInit, histogramSize);
      if (!CreateTextureHelper(_engine->GetD3DDeviceNonRec(), histogramSize, 1, histFormat, 
        vec16.Data(), histogramSize * sizeof(DirectX::PackedVector::HALF), _histogramTholds))
        return false;
    }
    else if (_engine->_apertureFormat==DXGI_FORMAT_R32_FLOAT)
    {
      if (!CreateTextureHelper(_engine->GetD3DDeviceNonRec(), histogramSize, 1, histFormat, HistInit, 
        histogramSize * sizeof(float), _histogramTholds))
        return false;
    }
    else
    {
      Fail("Aperture format %s not supported by queries");
      if (!CreateTextureHelper(_engine->GetD3DDeviceNonRec(), histogramSize, 1, histFormat, 0, 
        0, _histogramTholds))
        return false;
    } 

  }  

  #if _DEBUG && !defined _XBOX
  // DX11 TODO
  //{
  //  HRESULT err = _engine->GetD3DDeviceNonRec()->CreateOffscreenPlainSurface(1, 1, apFormat, D3DPOOL_SYSTEMMEM, _rtsDebugAp.Init(), NULL);
  //  if (FAILED(err))
  //  {
  //    // failed as a plain surface, try to create as a lockable render target
  //    err = _engine->GetD3DDeviceNonRec()->CreateRenderTarget(1, 1, apFormat, D3DMULTISAMPLE_NONE, 0, TRUE, _rtsDebugAp.Init(), NULL);
  //    if (FAILED(err))
  //    {
  //      _engine->DDError9("Post process Low resolution texture creation failed",err);
  //      return false;
  //    }
  //  }
  //  err = _engine->GetD3DDeviceNonRec()->CreateOffscreenPlainSurface(1, 1, rtFormat, D3DPOOL_SYSTEMMEM, _rtsDebugDec.Init(), NULL);
  //  if (FAILED(err))
  //  {
  //    err = _engine->GetD3DDeviceNonRec()->CreateRenderTarget(1, 1, rtFormat, D3DMULTISAMPLE_NONE, 0, TRUE, _rtsDebugDec.Init(), NULL);
  //    if (FAILED(err))
  //    {
  //      _engine->DDError9("Post process Low resolution texture creation failed",err);
  //      return false;
  //    }
  //  }
  //}
  #endif
  
  
  // stencil/depth buffer no longer needed here - we can disable it
  /*
  if (_engine->_depthBufferRT)
  {
    D3DSURFACE_DESC desc;
    _rtRough[0]->GetLevelDesc(0,&desc);
    int roughWidth = desc.Width;
    int roughHeight = desc.Height;
    if (
      FAILED(
        _engine->CreateDepthStencilSurface(
          roughWidth,roughHeight,D3DFMT_D24S8,
          D3DMULTISAMPLE_NONE,0,TRUE,_rtsRoughDepth,XXX
        )
      )
    )
    {
      LogF("PPGlow: Cannot create depth buffer");
      return false;
    }
  }
  */

#if _HELISIM_RENDER_TO_TEXTURE

  {
    // 1st 4x decimation step (128x128), each R2T is scaled to 128x128
    DecimationPass &dec0 = _decR2T[0];
    dec0._w = dec0._h = R2TMaxResolution / 4;

    HRESULT err = _engine->CreateRenderTarget(dec0._w, dec0._h, rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, dec0._rt);
    if (FAILED(err))
    {
      _engine->DDError11("Post process Low resolution texture creation failed", err);
      return false;
    }

    // 2nd 4x decimation step (32x32)
    DecimationPass &dec1 = _decR2T[1];
    dec1._w = dec1._h = dec0._w / 4;
    err = _engine->CreateRenderTarget(dec1._w, dec1._h, rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, dec1._rt);
    if (FAILED(err))
    {
      _engine->DDError11("Post process Low resolution texture creation failed", err);
      return false;
    }

    // decimation 2x
    int level = 2;
    int w = _decR2T[1]._w;
    int h = _decR2T[1]._h;
    do 
    {
      DecimationPass &dec = _decR2T[level];
      w = w >> 1;
      h = h >> 1;
      // create the supplementary RT
      // never create less then 1x1
      int effW = max(w,1);
      int effH = max(h,1);
      err = _engine->CreateRenderTarget(effW, effH, rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, dec._rt);
      if (FAILED(err))
      {
        _engine->DDError11("Post process Low resolution texture creation failed",err);
        return false;
      }
      level++;
      dec._w = effW;
      dec._h = effH;
    } while (w > 1);
  }

#endif

  {
    // setup initial data
    VertexCustomEdge v[4];
    v[0].pos = Vector3P(-0.5, -0.5, 0.5); v[0].index = 0;
    v[1].pos = Vector3P(-0.5,  0.5, 0.5); v[1].index = 1;
    v[2].pos = Vector3P( 0.5, -0.5, 0.5); v[2].index = 2;
    v[3].pos = Vector3P( 0.5,  0.5, 0.5); v[3].index = 3;

    // Create the VB
    ComRef<IDirect3DVertexBuffer9Old> vb;

    D3D11_BUFFER_DESC bd;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(VertexCustomEdge) * 4;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;
    bd.MiscFlags = 0;

    D3D11_SUBRESOURCE_DATA srData;
    srData.pSysMem = v;
    srData.SysMemPitch = 0;
    srData.SysMemSlicePitch = 0;

    HRESULT hr = _engine->GetD3DDeviceNonRec()->CreateBuffer(&bd, &srData, vb.Init());
    if (FAILED(hr))
    {
      LogF("Error: Vertex buffer creation failed.");
      return false;
    }

    _vbCustomEdge = VertexStaticData11::BufferFuture(vb);

    // Create the VD
    const D3D11_INPUT_ELEMENT_DESC vertexDecl[] =
    {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"BLENDINDICES", 0, DXGI_FORMAT_R32_FLOAT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    // create vertex shader
    if (!CompileVertexShader(vs,"VSPostProcessCustomEdge4T",_vsCustomEdge, vertexDecl, sizeof(vertexDecl)/sizeof(vertexDecl[0])))
    {
      return false;
    }
  }

  // Create the shaders  
  if (!CompilePixelShader(hlsl,"PSPostProcessDownSampleLuminance",_psLuminance))
  {
    return false;
  }
  if (!CompilePixelShader(hlsl,"PSPostProcessDownSampleAvgLuminance",_psAvgLuminance))
  {
    return false;
  }
  if (!CompilePixelShader(hlsl,"PSPostProcessDownSampleMaxAvgMinLuminance",_psMaxAvgMinLuminance))
  {
    return false;
  }
  ComRef<ID3D11PixelShader> ppal;
  if (!CompilePixelShader(hlsl,"PSPostProcessAssumedLuminance",ppal,true))
  {
    return false;
  }
  ComRef<ID3D11PixelShader> ppalDirect;
  if (!CompilePixelShader(hlsl,"PSPostProcessAssumedLuminanceDirect",ppalDirect,false))
  {
    return false;
  }

  if (_engine->_apertureFormat==DXGI_FORMAT_R8G8B8A8_UNORM)
  {
    /**
    if there is no float render target support,
    we cannot calculate change on the GPU
    */
    _psAssumedLuminance = ppalDirect;
  }
  else
  {
    _psAssumedLuminance = ppal;
  }

  if (!CompilePixelShader(hlsl,"PSPostProcessQuery",_psQuery))
  {
    return false;
  }


  if (!CompilePixelShader(hlsl,"PSPostProcessGlow",_psGlow))
  {
    return false;
  };

  if (!CompilePixelShader(hlsl,"PSPostProcessGlowNight",_psGlowNight))
  {
    return false;
  };
  if (!CompilePixelShader(hlsl,"PSPostProcessNVG",_psGlowNVG))
  {
    return false;
  };

  return true;
}


int EngineDD11::PPGlow::SetParams(const float *pars, int nPars)
{
  Assert(nPars==3);
  _deltaT = pars[0];
  _eyeAdaptMin = pars[1];
  _eyeAdaptMax = pars[2];

  return 0;
}

#if _DEBUG && !defined _XBOX
// DX11 TODO
//Color EngineDD11::PPGlow::GetContent(IDirect3DSurface9 *surf, IDirect3DSurface9 *temp, D3DFORMAT format)
//{
//  _engine->GetD3DDeviceNonRec()->GetRenderTargetData(surf,temp);
//
//  D3DLOCKED_RECT locked;
//  temp->LockRect(&locked,NULL,D3DLOCK_READONLY);
//  // assume ARGB8888 format
//  Color color;
//  switch (format)
//  {
//    case D3DFMT_A8R8G8B8:
//      {
//        PackedColor c = PackedColor(*(DWORD *)locked.pBits);
//        color = Color(c);
//      }
//      break;
//    case D3DFMT_R32F:
//      {
//        const float *c = (const float *)locked.pBits;
//        color = Color(c[0],0,0,1);
//      }
//      break;
//    case D3DFMT_G32R32F:
//      {
//        const float *c = (const float *)locked.pBits;
//        color = Color(c[0],c[1],0,1);
//      }
//      break;
//    case D3DFMT_A32B32G32R32F:
//      {
//        const float *c = (const float *)locked.pBits;
//        color = Color(c[0],c[1],c[2],c[3]);
//      }
//      break;
//    case D3DFMT_A16B16G16R16F:
//      {
//        const Float16b<10> *c = (const Float16b<10> *)locked.pBits;
//        color = Color(c[0],c[1],c[2],c[3]);
//      }
//      break;
//    case D3DFMT_G16R16F:
//      {
//        const Float16b<10> *c = (const Float16b<10> *)locked.pBits;
//        color = Color(c[0],c[1],0,1);
//      }
//      break;
//    case D3DFMT_R16F:
//      {
//        const Float16b<10> *c = (const Float16b<10> *)locked.pBits;
//        color = Color(c[0],0,0,1);
//      }
//      break;
//    case D3DFMT_L16:
//      {
//        const unsigned short *c = (const unsigned short *)locked.pBits;
//        float l = c[0]*(1.0f/0xffff);
//        color = Color(l,l,l,1);
//      }
//      break;
//    case D3DFMT_G16R16:
//      {
//        const unsigned short *c = (const unsigned short *)locked.pBits;
//        float r = c[0]*(1.0f/0xffff);
//        float g = c[1]*(1.0f/0xffff);
//        color = Color(r,g,0,1);
//      }
//      break;
//    default:
//      color = HBlack;
//      break;
//  }
//  temp->UnlockRect();
//  return color;
//}
//
//static Color DecodeApertureChangeF(Color color)
//{
//  return Color(
//    DecodeApertureChange(color.R()),
//    DecodeApertureChange(color.G()),
//    DecodeApertureChange(color.B()),
//    DecodeApertureChange(color.A())
//  );
//}
//
//Color EngineDD11::PPGlow::GetContentAp(IDirect3DSurface9 *surf)
//{
//  // aperture change is stored as logarithm
//  Color raw = GetContent(surf,_rtsDebugAp,_engine->_apertureFormat);
//  return DecodeApertureChangeF(raw);
//}
//Color EngineDD11::PPGlow::GetContentDec(IDirect3DSurface9 *surf)
//{
//  return GetContent(surf,_rtsDebugDec,D3DFMT_A8R8G8B8);
//}

#endif

// note: MS HDR uses sensitivity as follows:
//(0.2125, 0.7154, 0.0721);
//(0.2125, 0.7154, 0.0721);
#define R_EYE_DARK 0.05f
#define G_EYE_DARK 0.55f
#define B_EYE_DARK 0.40f
static const Color DaySens(R_EYE,G_EYE,B_EYE);

/*!
\patch 5129 Date 2/20/2007 by Ondra
- Improved: Improved night vision goggles rendering.
*/
static const Color NVGSens(0.6,0.2,0.2);
static const Color NightSens(R_EYE_DARK,G_EYE_DARK,B_EYE_DARK);

#if _HELISIM_RENDER_TO_TEXTURE
void EngineDD11::PPGlow::DoDirect(const RECT *rect, int rtIdx)
{

  static const float zero9[9*4] =
  {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };

  // glow is typically a last effect, but in some cases some other may follow
  PROFILE_SCOPE_GRF_EX(ppGlw, ppr2t, SCOPE_COLOR_YELLOW);

  // remember all we can
  PushStreamSource pshStreamSource(true);
  PushVertexShader pshVertexShader(true);
  PushPixelShader pshPixelShader(true);
  PushRenderTargetAndView pushRTV(true);

  int width = rect->right - rect->left;
  int height = rect->bottom - rect->top;

  MyD3DDeviceCB& device = _engine->GetD3DDeviceRec();

  // We will need a source as a texture
  TextureHandle11* rtCopy = _engine->CreateR2TRenderTargetCopyAsTexture(rect);

  BOOL rtIsSRGB = _engine->_caps._rtIsSRGB && _engine->_caps._canReadSRGB;

  // Set the vertex shader + stream source
  _engine->DoSetStreamSource(_vbCustomEdge, sizeof(VertexCustomEdge), 0); // we dont need size, so we set it to 0
  _engine->DoSetVertexShader(_vsCustomEdge._shader, _vsCustomEdge._inputLayout);

  R2THDRPars &hdrPars = *(_engine->_r2tTargets[rtIdx]._hdrPars);
  {
    float hdrMultiply = _engine->GetPostHDRBrightness() / _engine->GetHDRFactor();
    float spc7[] = {hdrMultiply, hdrMultiply, hdrMultiply, hdrMultiply};

    _engine->DoSetShaderConstantF(&PSC_HDRMultiply, 4, 0, Array<float>(spc7, 4));

    // decide if decimated surfaces are SRGB or not
    const bool decimatedSRGB = _engine->_downsampleFormat==DXGI_FORMAT_R8G8B8A8_UNORM;

    SetRenderStateTemp rsTemp0(D3DRS_ZENABLE_OLD, FALSE, true);
#ifndef _X360SHADERGEN      
    SetRenderStateTemp rsTemp1(D3DRS_SRGBWRITEENABLE_OLD, decimatedSRGB, true);
#endif
    SetRenderStateTemp rsTemp2(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD, true);
    // no alpha or fog done here
    SetRenderStateTemp rsTemp3(D3DRS_ALPHATESTENABLE_OLD, FALSE, true);
    SetRenderStateTemp rsTemp4(D3DRS_ALPHABLENDENABLE_OLD, FALSE, true);
    // by default assume bi-linear filtering
    SetSamplerStateTemp ssTemp0(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
    SetSamplerStateTemp ssTemp1(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
    SetSamplerStateTemp ssTemp2(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
    SetSamplerStateTemp ssTemp3(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
    SetSamplerStateTemp ssTemp4(2, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
    SetSamplerStateTemp ssTemp5(2, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
    SetSamplerStateTemp ssTemp6(3, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
    SetSamplerStateTemp ssTemp7(3, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
    // tri-linear filtering never done 
    SetSamplerStateTemp ssTemp8(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD, true);
    SetSamplerStateTemp ssTemp9(1, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD, true);
    SetSamplerStateTemp ssTemp10(2, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD, true);
    SetSamplerStateTemp ssTemp11(3, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD, true);
    // clamping all stages
    SetSamplerStateTemp ssTemp12(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD, true);
    SetSamplerStateTemp ssTemp13(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD, true);
    SetSamplerStateTemp ssTemp14(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD, true);
    SetSamplerStateTemp ssTemp15(1, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD, true);
    SetSamplerStateTemp ssTemp16(2, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD, true);
    SetSamplerStateTemp ssTemp17(2, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD, true);
    SetSamplerStateTemp ssTemp18(3, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD, true);
    SetSamplerStateTemp ssTemp19(3, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD, true);

    // perform initial luminance calculation + 4x decimation
    {
      PROFILE_SCOPE_GRF_EX(ppGLu, ppr2t, SCOPE_COLOR_YELLOW);

      EdgeParams pars;
#if _ARMA3_HDR_IMPROVEMENTS
      float texCoef = 1.0f - _engine->GetHDRLuminanceRectCoef();
      saturate(texCoef, 0.0f, 1.0f);
      texCoef *= 0.5f;

      float minTexCoord = 0.0f + texCoef;
      float maxTexCoord = 1.0f - texCoef;
      pars.SetupBilinear(_decR2T[0]._w,_decR2T[0]._h,width,height, minTexCoord, maxTexCoord);
#else
      pars.SetupBilinear(_decR2T[0]._w,_decR2T[0]._h,width,height);
#endif

      _engine->DoSetShaderConstantF(&VSC_PPEdgeU, 9*4, 0, Array<float>(pars.u[0], 9*4));
      // Set the pixel shader
      _engine->DoSetPixelShader(_psLuminance);

      // Set the render target
      _engine->SetRenderTargetAndDepthStencilDirect(_decR2T[0]._rt, DepthStencil::DepthStencilNULL, 0, 1);
      Color clearColor(HBlack);
      device->ClearRenderTargetView(_decR2T[0]._rt._rtView, Array<float>((float*)&clearColor, 4));

      // set the source as 4 textures    
      SetTextureTemp texTemp0(0, rtCopy, true);
      SetTextureTemp texTemp1(1, rtCopy, true);
      SetTextureTemp texTemp2(2, rtCopy, true);
      SetTextureTemp texTemp3(3, rtCopy, true);

#ifndef _X360SHADERGEN
      SetSamplerStateTemp ssTemp20(0, D3DSAMP_SRGBTEXTURE_OLD, rtIsSRGB, true);
      SetSamplerStateTemp ssTemp21(1, D3DSAMP_SRGBTEXTURE_OLD, rtIsSRGB, true);
      SetSamplerStateTemp ssTemp22(2, D3DSAMP_SRGBTEXTURE_OLD, rtIsSRGB, true);
      SetSamplerStateTemp ssTemp23(3, D3DSAMP_SRGBTEXTURE_OLD, rtIsSRGB, true);
#endif

      _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);
    }

#ifndef _X360SHADERGEN
    SetSamplerStateTemp ssTemp24(0, D3DSAMP_SRGBTEXTURE_OLD, decimatedSRGB, true);
    SetSamplerStateTemp ssTemp25(1, D3DSAMP_SRGBTEXTURE_OLD, decimatedSRGB, true);
    SetSamplerStateTemp ssTemp26(2, D3DSAMP_SRGBTEXTURE_OLD, decimatedSRGB, true);
    SetSamplerStateTemp ssTemp27(3, D3DSAMP_SRGBTEXTURE_OLD, decimatedSRGB, true);
#endif

    {
      PROFILE_SCOPE_GRF_EX(ppGAv, ppr2t, SCOPE_COLOR_YELLOW);

      // perform 4x decimation + average calculation
      EdgeParams pars;
      pars.SetupBilinear(_decR2T[1]._w, _decR2T[1]._h, _decR2T[0]._w, _decR2T[0]._h);
      _engine->DoSetShaderConstantF(&VSC_PPEdgeU, 9*4, 0, Array<float>(pars.u[0], 9*4));

      // Set the pixel shader
      _engine->DoSetPixelShader(_psAvgLuminance);

      // Set the render target
      _engine->SetRenderTargetAndDepthStencilDirect(_decR2T[1]._rt, DepthStencil::DepthStencilNULL, 0, 1);
      Color clearColor(HBlack);
      device->ClearRenderTargetView(_decR2T[1]._rt._rtView, Array<float>((float*)&clearColor, 4));

      TextureHandle11 *tex = _decR2T[0]._rt._srView;

      // set the source as 4 textures    
      SetTextureTemp texTemp0(0, tex, true);
      SetTextureTemp texTemp1(1, tex, true);
      SetTextureTemp texTemp2(2, tex, true);
      SetTextureTemp texTemp3(3, tex, true);

      _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);
    }

    {
      // perform 2x decimation + min/max/avg calculation
      PROFILE_SCOPE_GRF_EX(ppGMM, ppr2t, SCOPE_COLOR_YELLOW);

      // note: we know other temp setting is done, which will return original values once done
      for (int s=0; s<4; s++) 
      {
        SetSamplerStateTemp ssTemp0(s, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD, true);
        SetSamplerStateTemp ssTemp1(s, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD, true);
      }

      // set pixel shader
      _engine->DoSetPixelShader(_psMaxAvgMinLuminance);

      // 128, 32, 16, 8, 4, 2, 1
      for (int i = 2; i < 7; i++)
      {
        EdgeParams pars;
        pars.SetupBilinear(_decR2T[i]._w, _decR2T[i]._h, _decR2T[i-1]._w, _decR2T[i-1]._h);

        _engine->DoSetShaderConstantF(&VSC_PPEdgeU, 9*4, 0, Array<float>(pars.u[0], 9*4));

        // Set the render target
        _engine->SetRenderTargetAndDepthStencilDirect(_decR2T[i]._rt, DepthStencil::DepthStencilNULL, 0, 1);
        Color clearColor(HBlack);
        device->ClearRenderTargetView(_decR2T[i]._rt._rtView, Array<float>((float*)&clearColor, 4));

        TextureHandle11 *tex = _decR2T[i-1]._rt._srView;

        // set the source as 4 textures   
        SetTextureTemp texTemp0(0, tex, true);
        SetTextureTemp texTemp1(1, tex, true);
        SetTextureTemp texTemp2(2, tex, true);
        SetTextureTemp texTemp3(3, tex, true);

        _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);
      }

      // perform some calculations with 1x1 surfaces
      // this is to adjust aperture
      {
        _engine->DoSetPixelShader(_psAssumedLuminance);

        float eyeAccomLastToThis =  hdrPars._accomFactorPrev / hdrPars._accomFactor;
        // this controls overall scene brightness
        // we need to know where the user selected midpoint is
#if _ARMA3_HDR_IMPROVEMENTS
        float desiredLuminance = hdrPars._nightVisionR2T ? 0.781f : (_engine->GetPreHDRBrightness() * _engine->GetHDRFactor() * _engine->GetDesiredLuminanceCoef());
#else
        float desiredLuminance = hdrPars._nightVisionR2T ? 0.781f : (_engine->GetPreHDRBrightness() * _engine->GetHDRFactor());
#endif
        float invAccom = 1.0f / hdrPars._accomFactor;

        // adaptation time depends on how we are adapting
        // if we are reacting with iris, the adaptations is very fast
        // if we are reacting with retina, it is much slower
        // even when retina is adapted, if light level is raised, the iris will react
        // this allows quite quick re-adaptation for higher light levels
        // we expect something like:
        const float irisUsedToDark = 1; // time to make change 2x
        const float irisUsedToLight = 0.5; // time to make change 0.5x

        const float retinaUsedToDark = 20; // time to make change 2x
        const float retinaUsedToLight = 1.0; // time to make change 0.5x

        // retina adaptation dynamic range is probably much smaller (100x?)
        // when retina adaptation is important:
        // 1) iris fully open
        // 2) once rods get adapted, they go down slowly
        // we do not model this, we assume under certain level retina acts
        // and above it iris acts
        // assume retina starts reacting once iris adaptation is 1.0
        const float crossBeg = 1.0f;
        const float crossEnd = 4.0f;

        float retinaFactor = InterpolativC(hdrPars._accomFactorPrev, crossBeg, crossEnd, 0, 1);

        // time to make change 2x
        const float getUsedToDark = retinaFactor*retinaUsedToDark+(1-retinaFactor)*irisUsedToDark;
        // time to make change 0.5x
        const float getUsedToLight = retinaFactor*retinaUsedToLight+(1-retinaFactor)*irisUsedToLight;

        const float log2 = 0.69314718056f;

        float minRatio = exp(-hdrPars._deltaTR2T*(log2/getUsedToLight));
        float maxRatio = exp(+hdrPars._deltaTR2T*(log2/getUsedToDark));

        // make sure the number still can be represented with float16
        saturateMin(minRatio,0.999f);
        saturateMax(maxRatio,1.001f);

        // we need the value to be representable in float16 (m10e6) format
        float minAdapt = 1, maxAdapt = 1;

        if (!hdrPars._accomFactorFixed)
        {
          minAdapt = hdrPars._eyeAdaptMinR2T*invAccom;
          maxAdapt = hdrPars._eyeAdaptMaxR2T*invAccom;
        }

        const float pars[8] = {
          eyeAccomLastToThis, hdrPars._deltaTR2T, desiredLuminance, 0, minAdapt, maxAdapt, minRatio, maxRatio
        };

        _engine->DoSetShaderConstantF(&PSC_AssumedLuminancePars1, 2*4, 0, Array<float>((float*)pars, 2*4));
        _engine->DoSetShaderConstantF(&VSC_PPEdgeU, 9*4, 0, Array<float>((float*)zero9, 9*4));

        int oldAperture = hdrPars._rtApertureCurrentR2T;
        // switch to next aperture texture
        hdrPars._rtApertureCurrentR2T = hdrPars._rtApertureCurrentR2T == 0;

        // Set the render target
        _engine->SetRenderTargetAndDepthStencilDirect(hdrPars._rtApertureR2T[hdrPars._rtApertureCurrentR2T], DepthStencil::DepthStencilNULL, 0, 1);
        Color clearColor(HBlack);
        device->ClearRenderTargetView(hdrPars._rtApertureR2T[hdrPars._rtApertureCurrentR2T]._rtView, Array<float>((float*)&clearColor, 4));

        // 1x1 maxAvgMin as source
        SetTextureTemp texTemp0(0, _decR2T[6]._rt._srView, true);
        // old aperture value as another source
        SetTextureTemp texTemp1(1, hdrPars._rtApertureR2T[oldAperture]._srView, true);
        SetTextureTemp texTemp2(2, NULL, true);
        SetTextureTemp texTemp3(3, NULL, true);

        // render target is always linear
#ifndef _X360SHADERGEN
        SetRenderStateTemp rsTemp0(D3DRS_SRGBWRITEENABLE_OLD, FALSE, true);

        // old aperture is always linear
        // source down-sampled scene is SRGB
        SetSamplerStateTemp ssTemp0(0, D3DSAMP_SRGBTEXTURE_OLD, decimatedSRGB, true);
        SetSamplerStateTemp ssTemp1(1, D3DSAMP_SRGBTEXTURE_OLD, FALSE, true);
#endif

        _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);
      }

      // Measure the brightness of the image
      {
        PROFILE_SCOPE_GRF_EX(ppGQy, ppr2t, SCOPE_COLOR_GREEN);

        static const float baseU[4] = {0, 0, 1, 1};
        static const float baseV[4] = {1, 0, 1, 0};

        const float tgtOffsetU = 0;
        const float tgtOffsetV = 0;
        const float offsetU = 0.5f / NBrightQueries;
        const float offsetV = 0;

        float U[4][4];
        float V[4][4];
        for (int vertex = 0; vertex < 4; vertex++)
        {
          // Texture 0 - source image - does not matter, not used
          U[vertex][0] = baseU[vertex] + offsetU;
          V[vertex][0] = baseV[vertex] + offsetV;
          // Texture 1 - histohram thresholds
          U[vertex][1] = baseU[vertex] + offsetU;
          V[vertex][1] = baseV[vertex] + offsetV;
          // Texture 2
          U[vertex][2] = 0;
          V[vertex][2] = 0;
          // Texture 3
          U[vertex][3] = 0;
          V[vertex][3] = 0;
        }

        const float posOffset[4] = { tgtOffsetU, tgtOffsetV, 0, 0 };
        _engine->DoSetShaderConstantF(&VSC_PPEdgeU, 4*4, 0, Array<float>(U[0], 4*4));
        _engine->DoSetShaderConstantF(&VSC_PPEdgeV, 4*4, 0, Array<float>(V[0], 4*4));
        _engine->DoSetShaderConstantF(&VSC_PPEdgePosOffset, 1*4, 0, Array<float>((float *)posOffset, 1*4));

        float histPars[NBrightQueries+1];

        // we want half of the samples to be under 1 and half above 1
        // sample position relative to the expected value
        // last one is always 1 and it is always ignored
        for (int i = 0; i < NBrightQueries; i++)
        {
          histPars[i] = HistInit[i];
        }

        // always ignored
        histPars[NBrightQueries] = 1;

        SetRenderStateTemp rsTemp0(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD, true);
        SetRenderStateTemp rsTemp1(D3DRS_ALPHATESTENABLE_OLD, FALSE, true);
#ifndef _X360SHADERGEN
        SetRenderStateTemp rsTemp2(D3DRS_SRGBWRITEENABLE_OLD, FALSE, true);
        SetSamplerStateTemp ssTemp0(0, D3DSAMP_SRGBTEXTURE_OLD, FALSE, true);
#endif

        _engine->SetRenderTargetAndDepthStencilDirect(_rtMeasure, _rtMeasureDepth,0,1);
        Color clearColor(HBlack);
        device->ClearRenderTargetView(_rtMeasure._rtView, Array<float>((float*)&clearColor, 4));
        device->ClearDepthStencilView(_rtMeasureDepth._dsView, D3D11_CLEAR_DEPTH, 1.0f, 0);

        SetTextureTemp texTemp0(0, hdrPars._rtApertureR2T[hdrPars._rtApertureCurrentR2T]._srView, true);

        _engine->DoSetPixelShader(_psQuery);

        float values[NBrightQueries];
        for (int b = 0; b < NBrightQueries; b++)
        {
          values[b] = histPars[b] * hdrPars._accomFactor;
        }
        hdrPars._querySinglePassR2T->IssueDirect(values, hdrPars._accomFactor, _histogramTholds._resView);

        // Build the histogram and check the intensity of the image
        {
          // Each level includes all the following levels
          struct HistogramItem
          {
            float minAlpha;
            int pixels;
          } histogram[NBrightQueries+2];

          bool valid = true;
          int numberOfPixelsDrawn[NBrightQueries];
          float value[NBrightQueries];
          float masterValue = hdrPars._querySinglePassR2T->GetNumberOfPixelsDrawn(numberOfPixelsDrawn,value);
          (void)masterValue;

          for (int b=0; b<NBrightQueries; b++)
          {
            histogram[b+1].minAlpha = value[b];
            histogram[b + 1].pixels = numberOfPixelsDrawn[b];
            if (numberOfPixelsDrawn[b] < 0) valid = false;
          }

          if (valid)
          {
            // as we test 1x1 surface, "histogram" is actually one point only
            float totalAlpha = 0;
            // all pixels are over 0 limit
            histogram[0].minAlpha = 0;
            histogram[0].pixels = 1 * 1;
            // no pixels are over 1 limit
            histogram[NBrightQueries + 1].minAlpha = histogram[NBrightQueries].minAlpha;
            histogram[NBrightQueries + 1].pixels = 0;
            float totalError = 0;
            // verify we got expected results, i.e. line of 1s followed by line of 0s
            int edge = -1;
            for (int b = 0; b < NBrightQueries + 1; b++)
            {
              float alphaMin = histogram[b].minAlpha;
              float alphaMax = histogram[b+1].minAlpha;
              // exclude brighter levels
              int thisCount = histogram[b].pixels - histogram[b + 1].pixels;
              if (thisCount>0)
              {
                // verify the edge is 1 pixel only
                Assert(thisCount==1);
                // verify there is only one edge
                Assert(edge<0);
                edge = b;
              }
              float sumAlpha = thisCount * (alphaMin + alphaMax) * 0.5f;
              float error = thisCount * (alphaMax - alphaMin) * 0.5f;
              totalAlpha += sumAlpha;
              totalError += error;
            }

            // if the resulting value is negative, something went wrong
            DoAssert(totalAlpha>=0);

            // we cannot use masterValue to detect changes, as due to latency this could cause
            // two values alternating and never reaching a stable state
            // if the value did not change significantly from the master value, use the master value

            // if the value did not change significantly, do not change it, as changing aperture estimate often can lead to oscillation
            //const float changeThold = 0.04f;
            static float changeThold = 0.1f;
            if (hdrPars._lastKnownAvgIntensity <= 0 ||
              fabs(totalAlpha - hdrPars._lastKnownAvgIntensity) / totalAlpha>changeThold)
            {
              hdrPars._lastKnownAvgIntensity = totalAlpha;
            }
          }
        }
      }

      // Final step
      {
        static const float baseU[4] = {0, 0, 1, 1};
        static const float baseV[4] = {1, 0, 1, 0};

        // point sampling requires 0.5 offset to avoid numerical inaccuracy
        // previous version, well tuned
        const float tgtOffsetU = 0;
        const float tgtOffsetV = 0;
        const float srcOffsetU = 0.5f / width;
        const float srcOffsetV = 0.5f / height;

        const float glowOffsetU = 0.0f;
        const float glowOffsetV = 0.0f;
        float U[4][4];
        float V[4][4];
        for (int vertex = 0; vertex < 4; vertex++)
        {
          // Texture 0 - source image
          U[vertex][0] = baseU[vertex] + srcOffsetU;
          V[vertex][0] = baseV[vertex] + srcOffsetV;
          // Texture 1 - glow buffer
          U[vertex][1] = baseU[vertex] + glowOffsetU;
          V[vertex][1] = baseV[vertex] + glowOffsetV;
          // Texture 2
          U[vertex][2] = 0;
          V[vertex][2] = 0;
          // Texture 3
          U[vertex][3] = 0;
          V[vertex][3] = 0;
        }

        const float posOffset[4] = { tgtOffsetU,tgtOffsetV, 0, 0 };
        _engine->DoSetShaderConstantF(&VSC_PPEdgeU, 4*4, 0, Array<float>(U[0], 4*4));
        _engine->DoSetShaderConstantF(&VSC_PPEdgeV, 4*4, 0, Array<float>(V[0], 4*4));
        _engine->DoSetShaderConstantF(&VSC_PPEdgePosOffset, 1*4, 0, Array<float>((float *)posOffset, 1*4));
 
        SetRenderStateTemp rsTemp0(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD, true);
        SetSamplerStateTemp ssTemp0(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD, true);
        SetSamplerStateTemp ssTemp1(1, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD, true);
        SetSamplerStateTemp ssTemp2(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
        SetSamplerStateTemp ssTemp3(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
        SetSamplerStateTemp ssTemp4(2, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD, true);
        SetSamplerStateTemp ssTemp5(2, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD, true);

        _engine->SetRenderTargetAndDepthStencilDirect(_engine->_r2tRT._renderTargetS, DepthStencil::DepthStencilNULL, 0, 1);
        Color clearColor(HBlack);
        device->ClearRenderTargetView(_engine->_r2tRT._renderTargetS._rtView, Array<float>((float*)&clearColor, 4));

        SetTextureTemp texTemp0(0, rtCopy, true); // Set the original render target as texture 0
        SetTextureTemp texTemp1(1, NULL, true); // glow texture here
        SetTextureTemp texTemp2(2, hdrPars._rtApertureR2T[hdrPars._rtApertureCurrentR2T]._srView, true); // aperture control texture
        SetTextureTemp texTemp3(3, _decR2T[7]._rt._srView, true); // 1x1 maxAvgMin

#ifndef _X360SHADERGEN
        SetSamplerStateTemp ssTemp6(0, D3DSAMP_SRGBTEXTURE_OLD, rtIsSRGB, true);
        SetSamplerStateTemp ssTemp7(1, D3DSAMP_SRGBTEXTURE_OLD, rtIsSRGB, true);
        SetSamplerStateTemp ssTemp8(2, D3DSAMP_SRGBTEXTURE_OLD, FALSE, true);
        SetSamplerStateTemp ssTemp9(3, D3DSAMP_SRGBTEXTURE_OLD, decimatedSRGB, true);
#endif
        {
          _engine->SetViewportDirect(0,0,width, height, 0, 1);
        }

        // setup image bloom composition pars (no bloom)
        const float pars[4]={0, 1, 0, 0};
        _engine->DoSetShaderConstantF(&PSC_BloomPars, 1*4, 0, Array<float>((float *)pars, 1*4));

        // compute r,g,b eye sensitivity
        if (hdrPars._nightEye > 1e-3f)
        {
          // Set the pixel shader
          _engine->DoSetPixelShader(_psGlowNight);

          // Value designed for night blue shift
          static float coneAdjust = 60.0f;
          float lumScale = coneAdjust / hdrPars._accomFactor;

          // Get cones
          static float coneFull = 0.5f;
          static float coneNo = 1.1f;
          float cones = InterpolativC(hdrPars._accomFactor,coneFull,coneNo,1,-0.2);

          // Get Eye sens
          Color eyeSens = EyeSens(cones);

          // Set the temporary shader constant
          D3DXVECTOR4  spc6(eyeSens.R(), eyeSens.G(), eyeSens.B(), 1);
          _engine->DoSetShaderConstantF(&PSC_RgbEyeCoef, 1*4, 0, Array<float>(spc6, 1*4));

          D3DXVECTOR4 spc8(lumScale, cones, 0, 0);
          _engine->DoSetShaderConstantF(&PSC_NightControl, 1*4, 0, Array<float>(spc8, 1*4));

          // Drawing
          _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);
        }
        else
        {
          // Set the pixel shader
          ColorVal sens = hdrPars._nightVisionR2T ? NVGSens : DaySens;
          // Set the temporary shader constant
          _engine->DoSetPixelShader(hdrPars._nightVisionR2T ? _psGlowNVG : _psGlow);

          // Set the temporary shader constant
          // Drawing
          D3DXVECTOR4 spc6(sens.R(), sens.G(), sens.B(), 1);
          _engine->DoSetShaderConstantF(&PSC_RgbEyeCoef, 1*4, 0, Array<float>(spc6, 1*4));
          _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);
        }

      }
    }
  }

  // remember properties of this frame
  hdrPars._accomFactorPrev = hdrPars._accomFactor;

  _engine->DoSetTexture(0, NULL);
  _engine->DoSetTexture(1, NULL);
  _engine->DoSetTexture(2, NULL);
  _engine->DoSetTexture(3, NULL);

}
#endif //#if _HELISIM_RENDER_TO_TEXTURE

void EngineDD11::PPGlow::Do(bool isLast)
{
#if 1 // ndef _XBOX
  static const float zero9[9*4] =
  {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };
  
  // glow is typically a last effect, but in some cases some other may follow
  PROFILE_SCOPE_GRF_EX(ppGlw,pp,SCOPE_COLOR_YELLOW);

  const int cb = -1;
  
  // Prepare drawing
  Prepare();

  // Save current render target and depth stencil
  _engine->PushRenderTargetDepthStencil();

  // We will need a source as a texture
  TextureHandle11* rt;
  
  // if possible, use previous render target directly for texturing
  if (isLast && _engine->_currRenderTarget._srView)
  {
    rt = _engine->GetCurrRenderTargetAsTexture();
  }
  else
  {
    rt = _engine->CreateRenderTargetCopyAsTexture();
  }
  
  // depending on render target format it may or may not be SRGB
  Assert(_engine->_linearSpace);
  BOOL rtIsSRGB = _engine->_caps._rtIsSRGB && _engine->_caps._canReadSRGB;
  // Set the vertex shader + stream source
  _engine->SetStreamSource(cb, _vbCustomEdge, sizeof(VertexCustomEdge), 4);
  _engine->SetVertexShader(cb, _vsCustomEdge);


  {
    float hdrMultiply = _engine->GetPostHDRBrightness()/_engine->GetHDRFactor();
#if _ENABLE_CHEATS && _ENABLE_REPORT && _ENABLE_PERFLOG
    if (CHECK_DIAG(DEHDR) && CheckMainThread())
    {
      DIAG_MESSAGE(100,Format("Post HDR multiply %.2f",hdrMultiply));
    }
#endif
    SetShaderConstantTemp spc7(PSC_HDRMultiply, D3DXVECTOR4(hdrMultiply,hdrMultiply,hdrMultiply,hdrMultiply));
    
    // DX11 TODO remove or repair 
    //PushVertexShaderConstant pushC0(0);
    //PushVertexShaderConstant pushC1(1);
    //PushVertexShaderConstant pushC2(2);
    //PushVertexShaderConstant pushC3(3);
    //PushVertexShaderConstant pushC4(4);
    //PushVertexShaderConstant pushC5(5);
    //PushVertexShaderConstant pushC6(6);
    //PushVertexShaderConstant pushC7(7);
    //PushVertexShaderConstant pushC8(8);
    
    // decide if decimated surfaces are SRGB or not
    // DX11 TODO
    const bool decimatedSRGB = _engine->_downsampleFormat==DXGI_FORMAT_R8G8B8A8_UNORM;
  
    // Decimate the original into rough texture and measure aperture
    {
      // Set temporary states
#ifndef _X360SHADERGEN      
      SetRenderStateTemp rsSRGBSRGBWRITEENABLE(D3DRS_SRGBWRITEENABLE_OLD, decimatedSRGB);
#endif
      SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);
      // no alpha or fog done here
      SetRenderStateTemp rsALPHATESTENABLE(D3DRS_ALPHATESTENABLE_OLD, FALSE);
      SetRenderStateTemp rsALPHABLENDENABLE(D3DRS_ALPHABLENDENABLE_OLD, FALSE);
      // by default assume bi-linear filtering
      SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp ss1MINFILTER(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp ss1MAGFILTER(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp ss2MINFILTER(2, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp ss2MAGFILTER(2, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp ss3MINFILTER(3, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp ss3MAGFILTER(3, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
      // tri-linear filtering never done 
      SetSamplerStateTemp ss0MIPFILTER(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD);
      SetSamplerStateTemp ss1MIPFILTER(1, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD);
      SetSamplerStateTemp ss2MIPFILTER(2, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD);
      SetSamplerStateTemp ss3MIPFILTER(3, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD);
      // clamping all stages
      SetSamplerStateTemp ss0ADDRESSU(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
      SetSamplerStateTemp ss0ADDRESSV(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);
      SetSamplerStateTemp ss1ADDRESSU(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
      SetSamplerStateTemp ss1ADDRESSV(1, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);
      SetSamplerStateTemp ss2ADDRESSU(2, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
      SetSamplerStateTemp ss2ADDRESSV(2, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);
      SetSamplerStateTemp ss3ADDRESSU(3, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
      SetSamplerStateTemp ss3ADDRESSV(3, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);


      
      #if 0 //_ENABLE_CHEATS
        D3DSURFACE_DESC desc;
        rt->GetLevelDesc(0,&desc);
        DoAssert(desc.Width==_engine->Width());
        DoAssert(desc.Height==_engine->Height());
      #endif

      {
        PROFILE_SCOPE_GRF_EX(ppGLu,pp,SCOPE_COLOR_YELLOW);
    
        // perform initial luminance calculation + 4x decimation
        EdgeParams pars;
#if _ARMA3_HDR_IMPROVEMENTS
        float texCoef = 1.0f - _engine->GetHDRLuminanceRectCoef();
        saturate(texCoef, 0.0f, 1.0f);
        texCoef *= 0.5f;

        float minTexCoord = 0.0f + texCoef;
        float maxTexCoord = 1.0f - texCoef;
        pars.SetupBilinear(_dec[0]._w,_dec[0]._h,_engine->Width(),_engine->Height(), minTexCoord, maxTexCoord);
#else
        pars.SetupBilinear(_dec[0]._w,_dec[0]._h,_engine->Width(),_engine->Height());
#endif
        _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeU, pars.u[0], 9);

        // Set the pixel shader
        _engine->SetPixelShader(cb, _psLuminance);

        // Set the render target
        _engine->SetRenderTargetAndDepthStencil(_dec[0]._rt, DepthStencil::DepthStencilNULL);

        // set the source as 4 textures    
        SetTextureTemp tTex0(0, rt);
        SetTextureTemp tTex1(1, rt);
        SetTextureTemp tTex2(2, rt);
        SetTextureTemp tTex3(3, rt);
#ifndef _X360SHADERGEN
        SetSamplerStateTemp tempSRGB0(0, D3DSAMP_SRGBTEXTURE_OLD, rtIsSRGB);
        SetSamplerStateTemp tempSRGB1(1, D3DSAMP_SRGBTEXTURE_OLD, rtIsSRGB);
        SetSamplerStateTemp tempSRGB2(2, D3DSAMP_SRGBTEXTURE_OLD, rtIsSRGB);
        SetSamplerStateTemp tempSRGB3(3, D3DSAMP_SRGBTEXTURE_OLD, rtIsSRGB);
#endif
        
        _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);
        _engine->ResolveRenderTarget();
      }
    
      // source is always linear space since here
#ifndef _X360SHADERGEN
      SetSamplerStateTemp tempSRGB0(0, D3DSAMP_SRGBTEXTURE_OLD, decimatedSRGB);
      SetSamplerStateTemp tempSRGB1(1, D3DSAMP_SRGBTEXTURE_OLD, decimatedSRGB);
      SetSamplerStateTemp tempSRGB2(2, D3DSAMP_SRGBTEXTURE_OLD, decimatedSRGB);
      SetSamplerStateTemp tempSRGB3(3, D3DSAMP_SRGBTEXTURE_OLD, decimatedSRGB);
#endif
      
      { // perform 4x decimation + average calculation
        PROFILE_SCOPE_GRF_EX(ppGAv,pp,SCOPE_COLOR_YELLOW);
        for (int i=1; i<_decimation4x; i++)
        {
          // perform initial luminance calculation + 4x decimation
          EdgeParams pars;
          pars.SetupBilinear(_dec[i]._w,_dec[i]._h,_dec[i-1]._w,_dec[i-1]._h);
          
          _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeU, pars.u[0], 9);

          // Set the pixel shader
          _engine->SetPixelShader(cb, _psAvgLuminance);

          // Set the render target
          _engine->SetRenderTargetAndDepthStencil(_dec[i]._rt, DepthStencil::DepthStencilNULL);

          TextureHandle11* tex = _dec[i-1]._rt._srView;
          // set the source as 4 textures    
          SetTextureTemp tTex0(0, tex);
          SetTextureTemp tTex1(1, tex);
          SetTextureTemp tTex2(2, tex);
          SetTextureTemp tTex3(3, tex);

          _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);
          _engine->ResolveRenderTarget();
        }
      }

      { // perform 2x decimation + min/max/avg calculation
        PROFILE_SCOPE_GRF_EX(ppGMM,pp,SCOPE_COLOR_YELLOW);
        // note: we know other temp setting is done, which will return original values once done
        for (int s=0; s<4; s++) 
        {
          _engine->D3DSetSamplerState(cb, s, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
          _engine->D3DSetSamplerState(cb, s, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
        }
        
        // Set the pixel shader
        _engine->SetPixelShader(cb, _psMaxAvgMinLuminance);
        for (int i=_decimation4x; i<_decimation2x; i++)
        {
          EdgeParams pars;
          pars.SetupBilinear(_dec[i]._w,_dec[i]._h,_dec[i-1]._w,_dec[i-1]._h);
          
          _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeU, pars.u[0], 9);

          // Set the render target
          _engine->SetRenderTargetAndDepthStencil(_dec[i]._rt, DepthStencil::DepthStencilNULL);

          TextureHandle11* tex = _dec[i-1]._rt._srView;
          // set the source as 4 textures    
          SetTextureTemp tTex0(0, tex);
          SetTextureTemp tTex1(1, tex);
          SetTextureTemp tTex2(2, tex);
          SetTextureTemp tTex3(3, tex);
          
          _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);
          _engine->ResolveRenderTarget();
        }
        
        // perform some calculations with 1x1 surfaces
        // this is to adjust aperture
        {
          _engine->SetPixelShader(cb, _psAssumedLuminance);
          
          Assert(_engine->_accomFactorThis>0);
          // values in last frame were converted to absolute by multiplying by:
          // _engine->_accomFactorPrev * lastFrameAp
          // values in this frame are converted to absolute by multiplying by:
          //  _engine->_accomFactorThis * thisFrameAp
          // absAp = lastFrameAp * _engine->_accomFactorPrev
          // absAp = thisFrameAp * _engine->_accomFactorThis

          // lastFrameAp * _engine->_accomFactorPrev/_engine->_accomFactorThis = thisFrameAp
          
          
          float eyeAccomLastToThis = _engine->_accomFactorPrev/_engine->_accomFactorThis;
          // this controls overall scene brightness
          // we need to know where the user selected midpoint is
#if _ARMA3_HDR_IMPROVEMENTS
          float desiredLuminance = _engine->_nightVisionThis ? 0.781f : (_engine->GetPreHDRBrightness() * _engine->GetHDRFactor() * _engine->GetDesiredLuminanceCoef());
#else
          float desiredLuminance = _engine->_nightVisionThis ? 0.781f : (_engine->GetPreHDRBrightness() * _engine->GetHDRFactor());
#endif
          float invAccom=1/_engine->_accomFactorThis;

          // adaptation time depends on how we are adapting
          // if we are reacting with iris, the adaptations is very fast
          // if we are reacting with retina, it is much slower
          // even when retina is adapted, if light level is raised, the iris will react
          // this allows quite quick re-adaptation for higher light levels
          // we expect something like:
          const float irisUsedToDark = 1; // time to make change 2x
          const float irisUsedToLight = 0.5; // time to make change 0.5x
          
          const float retinaUsedToDark = 20; // time to make change 2x
          const float retinaUsedToLight = 1.0; // time to make change 0.5x
          
          // retina adaptation dynamic range is probably much smaller (100x?)
          // when retina adaptation is important:
          // 1) iris fully open
          // 2) once rods get adapted, they go down slowly
          // we do not model this, we assume under certain level retina acts
          // and above it iris acts
          // assume retina starts reacting once iris adaptation is 1.0
          const float crossBeg = 1.0f;
          const float crossEnd = 4.0f;
          
          float retinaFactor = InterpolativC(_engine->_accomFactorPrev,crossBeg,crossEnd,0,1);
          
          // time to make change 2x
          const float getUsedToDark = retinaFactor*retinaUsedToDark+(1-retinaFactor)*irisUsedToDark;
          // time to make change 0.5x
          const float getUsedToLight = retinaFactor*retinaUsedToLight+(1-retinaFactor)*irisUsedToLight;
          
          const float log2 = 0.69314718056f;
          
          
          float minRatio = exp(-_deltaT*(log2/getUsedToLight));
          float maxRatio = exp(+_deltaT*(log2/getUsedToDark));

          // make sure the number still can be represented with float16
          saturateMin(minRatio,0.999f);
          saturateMax(maxRatio,1.001f);
          
          // we need the value to be representable in float16 (m10e6) format
          float minAdapt = 1, maxAdapt = 1;
          
          if (!_engine->_accomFactorFixedThis)
          {
            minAdapt = _eyeAdaptMin*invAccom;
            maxAdapt = _eyeAdaptMax*invAccom;
          }

          const float pars[8]={
            eyeAccomLastToThis,_deltaT,desiredLuminance,0,
            minAdapt,maxAdapt,minRatio,maxRatio
          };
          
          #if _ENABLE_CHEATS
            if (CHECK_DIAG(DEHDR))
            {
              Log("Adapt. factor: last %g, this %g",_engine->_accomFactorPrev,_engine->_accomFactorThis);
            }
          #endif

            // DX11 TODO - repair or remove
          //PushPixelShaderConstant psc8(8),psc(9);
          _engine->SetPixelShaderConstantF(cb, PSC_AssumedLuminancePars1, pars, 2);
          
          _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeU, zero9, 9);

          int oldAperture = _rtApertureCurrent;
          // switch to next aperture texture
          _rtApertureCurrent = _rtApertureCurrent==0;
          // Set the render target
          _engine->SetRenderTargetAndDepthStencil(_rtAperture[_rtApertureCurrent], DepthStencil::DepthStencilNULL);

          // 1x1 maxAvgMin as source
          SetTextureTemp tTex0(0, _dec[_decimation2x-1]._rt._srView);
          // old aperture value as another source
          SetTextureTemp tTex1(1, _rtAperture[oldAperture]._srView);
          SetTextureTemp tTex2(2, NULL);
          SetTextureTemp tTex3(3, NULL);
          
          // render target is always linear
#ifndef _X360SHADERGEN
          SetRenderStateTemp rsSRGBSRGBWRITEENABLE(D3DRS_SRGBWRITEENABLE_OLD, FALSE);

          // old aperture is always linear
          // source down-sampled scene is SRGB
          SetSamplerStateTemp tempSRGB0(0, D3DSAMP_SRGBTEXTURE_OLD, decimatedSRGB);
          SetSamplerStateTemp tempSRGB1(1, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
#endif
          
          _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);
          
          _engine->ResolveRenderTarget();

          #if _DEBUG && !defined _XBOX
          // DX11 TODO
            //if (CHECK_DIAG(DEHDR))
            //{
            //  // lock textures and read debugging data from them
            //  Color dec = GetContentDec(_dec[_decimation2x-1]._rts);
            //  
            //  Color oldAp = GetContentAp(_rtsAperture[oldAperture]);
            //  Color newAp = GetContentAp(_rtsAperture[_rtApertureCurrent]);
            //  Color oldApAbs = oldAp*_engine->_accomFactorPrev;
            //  Color newApAbs = oldAp*_engine->_accomFactorThis;
            //  Log(
            //    "Aperture: %g,%g,%g,%g -> %g,%g,%g,%g",
            //    oldAp.R(),oldAp.G(),oldAp.B(),oldAp.A(),
            //    newAp.R(),newAp.G(),newAp.B(),newAp.A()
            //  );
            //  Log(
            //    "Aperture (abs): %g,%g,%g,%g -> %g,%g,%g,%g",
            //    oldApAbs.R(),oldApAbs.G(),oldApAbs.B(),oldApAbs.A(),
            //    newApAbs.R(),newApAbs.G(),newApAbs.B(),newApAbs.A()
            //  );
            //  Log("Dec: %g,%g,%g,%g",dec.R(),dec.G(),dec.B(),dec.A());
            //}
          #endif
        }
      }
      
      // note: linear space temp goes off scope here (cf. tempNoSRGB0)
    }

    
    // once we have the aperture, we may perform the Bloom surface preparation
    if (_engine->_doBloom)
    {
      _engine->_pp._bloom->Do(false,rt);
    }
    
     
    // Measure the brightness of the image
    {
      PROFILE_SCOPE_GRF_EX(ppGQy,pp,SCOPE_COLOR_GREEN);

      // DX11 TODO remove or repair 
      //PushVertexShaderConstant pushC0(0);
      //PushVertexShaderConstant pushC1(1);
      //PushVertexShaderConstant pushC2(2);
      //PushVertexShaderConstant pushC3(3);
      //PushVertexShaderConstant pushC4(4);
      //PushVertexShaderConstant pushC5(5);
      //PushVertexShaderConstant pushC6(6);
      //PushVertexShaderConstant pushC7(7);
      //PushVertexShaderConstant pushC8(8);
      
      // source is 1 x NBrightQueries image
      // set UV mapping so that we read pixels using point filtering

      static const float baseU[4] = {0, 0, 1, 1};
      static const float baseV[4] = {1, 0, 1, 0};
      
      const float tgtOffsetU = 0;
      const float tgtOffsetV = 0;
      const float offsetU = 0;
      const float offsetV = 0;
      
      float U[4][4];
      float V[4][4];
      for (int vertex = 0; vertex < 4; vertex++)
      {
        // Texture 0 - source image - does not matter, not used
        U[vertex][0] = baseU[vertex] + offsetU;
        V[vertex][0] = baseV[vertex] + offsetV;
        // Texture 1 - histohram thresholds
        U[vertex][1] = baseU[vertex] + offsetU;
        V[vertex][1] = baseV[vertex] + offsetV;
        // Texture 2
        U[vertex][2] = 0;
        V[vertex][2] = 0;
        // Texture 3
        U[vertex][3] = 0;
        V[vertex][3] = 0;
      }
      const float posOffset[4] = {tgtOffsetU,tgtOffsetV,0,0};
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeU, U[0], 4);
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeV, V[0], 4);
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgePosOffset, posOffset, 1);
      
      // _engine->_lastKnownAvgIntensity is the average from the last frame
      // _engine->_accomFactorThis is current exposure settings
      
      float histPars[NBrightQueries+1];
      // CPU reads eyeAccom as determined by the GPU
      // we are using log representation in the pixels shaders for great precision and range
      
      // we want half of the samples to be under 1 and half above 1
      // sample position relative to the expected value
      // last one is always 1 and it is always ignored
      for (int i=0; i<NBrightQueries; i++)
      {
        histPars[i] = HistInit[i];
      }
      // always ignored
      histPars[NBrightQueries]=1;
      
      // we want most probes to be around the expected values
      // if the result it too high, the system will avoid too fast adaptation anyway
      
      // Draw
      // Set temporary render states
      SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);
      //SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, 0);
      SetRenderStateTemp rsALPHATESTENABLE(D3DRS_ALPHATESTENABLE_OLD, FALSE);
#ifndef _X360SHADERGEN
      SetRenderStateTemp rsSRGBSRGBWRITEENABLE(D3DRS_SRGBWRITEENABLE_OLD, FALSE);
      SetSamplerStateTemp tempNoSRGB0(0, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
#endif
      
      // Set the render target
      // note: we need depth buffer, otherwise query does not work at all
      // It looks Z-buffer must be enabled in order the occlusion query to work on some ATI HW
      // see http://discussms.hosting.lsoft.com/SCRIPTS/WA-MSD.EXE?A2=ind0502B&L=DIRECTXDEV&D=0&I=-3&X=60AD7D5C1D013612AF&P=3658
      SetRenderStateTemp rsZENABLE(D3DRS_ZENABLE_OLD, TRUE);
      SetRenderStateTemp rsZFUNC(D3DRS_ZFUNC_OLD, D3DCMP_ALWAYS_OLD);

      _engine->SetRenderTargetAndDepthStencil(_rtMeasure, _rtMeasureDepth);
      _engine->SetTexture(cb, 0, _rtAperture[_rtApertureCurrent]._srView);
      
      _engine->SetPixelShader(cb, _psQuery);
      
      // Drawing
      float values[NBrightQueries];
      for (int b = 0; b < NBrightQueries; b++)
      {
        values[b] = histPars[b]*_engine->_accomFactorThis;
      }
      _querySinglePass.Issue(values,_engine->_accomFactorThis,_histogramTholds._resView);

      // Build the histogram and check the intensity of the image
      {

        // Each level includes all the following levels
        struct HistogramItem
        {
          float minAlpha;
          int pixels;
        } histogram[NBrightQueries+2];

        bool valid = true;
        int numberOfPixelsDrawn[NBrightQueries];
        float value[NBrightQueries];
        float masterValue = _querySinglePass.GetNumberOfPixelsDrawn(numberOfPixelsDrawn,value);
        (void)masterValue;
        for (int b=0; b<NBrightQueries; b++)
        {
          histogram[b+1].minAlpha = value[b];
          histogram[b + 1].pixels = numberOfPixelsDrawn[b];
          if (numberOfPixelsDrawn[b] < 0) valid = false;
        }

        if (valid)
        {
          // as we test 1x1 surface, "histogram" is actually one point only
          float totalAlpha = 0;
          // all pixels are over 0 limit
          histogram[0].minAlpha = 0;
          histogram[0].pixels = 1 * 1;
          // no pixels are over 1 limit
          histogram[NBrightQueries + 1].minAlpha = histogram[NBrightQueries].minAlpha;
          histogram[NBrightQueries + 1].pixels = 0;
          float totalError = 0;
          // verify we got expected results, i.e. line of 1s followed by line of 0s
          int edge = -1;
          for (int b = 0; b < NBrightQueries + 1; b++)
          {
            float alphaMin = histogram[b].minAlpha;
            float alphaMax = histogram[b+1].minAlpha;
            // exclude brighter levels
            int thisCount = histogram[b].pixels - histogram[b + 1].pixels;
            if (thisCount>0)
            {
              // verify the edge is 1 pixel only
              Assert(thisCount==1);
              // verify there is only one edge
              Assert(edge<0);
              edge = b;
            }
            float sumAlpha = thisCount * (alphaMin + alphaMax) * 0.5f;
            float error = thisCount * (alphaMax - alphaMin) * 0.5f;
            totalAlpha += sumAlpha;
            totalError += error;
          }

          //LogF("Avg %.3f", totalAlpha / ((float(MeasureWidth * MeasureHeight) * 255.0f) / 5.0f));

          #if _ENABLE_CHEATS && _ENABLE_REPORT && _ENABLE_PERFLOG
            if (CHECK_DIAG(DEHDR) && CheckMainThread())
            {
              DIAG_MESSAGE(
                500,Format(
                  "Avg expected measured %.4f+(%.4f), %.4f..%.4f",
                  totalAlpha,totalError,totalAlpha-totalError,totalAlpha+totalError
                )
              );
            }
          #endif
          #if _ENABLE_CHEATS
            if (CHECK_DIAG(DEHDR))
            {
              Log("Measured %g (was for eyeAccom %g)",totalAlpha,masterValue);
            }
          #endif
          // if the resulting value is negative, something went wrong
          DoAssert(totalAlpha>=0);
          /*
          // we cannot use masterValue to detect changes, as due to latency this could cause
          // two values alternating and never reaching a stable state
          // if the value did not change significantly from the master value, use the master value
          float newEyeAccom = masterValue;
          if (
            fabs(totalAlpha-masterValue)/totalAlpha>4e-2
          )
          {
            newEyeAccom = totalAlpha;
          }
          _engine->_lastKnownAvgIntensity = newEyeAccom;
          */
          // if the value did not change significantly, do not change it, as changing aperture estimate often can lead to oscillation
          //const float changeThold = 0.04f;
          static float changeThold = 0.1f;
          if (
            _engine->_lastKnownAvgIntensity<=0 ||
            fabs(totalAlpha-_engine->_lastKnownAvgIntensity)/totalAlpha>changeThold
          )
          {
            _engine->_lastKnownAvgIntensity = totalAlpha;
          }
        }
      }
    }

    // Final step - blending of original and the glow
    {
      // TODO: use EdgeParams
      //EdgeParams pars;
      //pars.SetupPoint(_engine->Width(),_engine->Height(),_engine->Width(),_engine->Height());
      static const float baseU[4] = {0, 0, 1, 1};
      static const float baseV[4] = {1, 0, 1, 0};

      // when rescaling, we want to allow filtering
      bool rescale = false;
      //static bool forcePoint = false;
      //if (forcePoint) rescale = false;
      
      // point sampling requires 0.5 offset to avoid numerical inaccuracy
      // previous version, well tuned
      const float tgtOffsetU = 0;
      const float tgtOffsetV = 0;
      const float srcOffsetU = 0;
      const float srcOffsetV = 0;

      TextureHandle11* glow = _engine->_pp._bloom->GetBlurredTexture()._srView; 

      const float glowOffsetU = 0;
      const float glowOffsetV = 0;
      float U[4][4];
      float V[4][4];
      for (int vertex = 0; vertex < 4; vertex++)
      {
        // Texture 0 - source image
        U[vertex][0] = baseU[vertex] + srcOffsetU;
        V[vertex][0] = baseV[vertex] + srcOffsetV;
        // Texture 1 - glow buffer
        U[vertex][1] = baseU[vertex] + glowOffsetU;
        V[vertex][1] = baseV[vertex] + glowOffsetV;
        // Texture 2
        U[vertex][2] = 0;
        V[vertex][2] = 0;
        // Texture 3
        U[vertex][3] = 0;
        V[vertex][3] = 0;
      }
      const float posOffset[4] = {tgtOffsetU,tgtOffsetV,0,0};
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeU, U[0], 4);
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeV, V[0], 4);
      _engine->SetVertexShaderConstantF(cb, VSC_PPEdgePosOffset, posOffset, 1);


      // Set temporary states
      SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);
      SetSamplerStateTemp ss1ADDRESSU(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
      SetSamplerStateTemp ss1ADDRESSV(1, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);
      SetSamplerStateTemp ss1MINFILTER(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp ss1MAGFILTER(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp ss1MIPFILTER(1, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp ss2MINFILTER(2, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
      SetSamplerStateTemp ss2MAGFILTER(2, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
      SetSamplerStateTemp ss2MIPFILTER(2, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);
      // Restore the original render target and depth stencil buffer
      _engine->PopRenderTargetDepthStencil();

      if (isLast)
      {
        // set backbuffer as target
        _engine->SwitchToBackBufferRenderTarget();
      }

      // reset viewport z-range to default
      _engine->_minZRange = 0;
      _engine->_maxZRange = 1;
    
      SetTextureTemp tTex0(0, rt); // Set the original render target as texture 0
      SetTextureTemp tTex1(1, _engine->_doBloom ? glow : NULL); // glow texture here
      SetTextureTemp tTex2(2, _rtAperture[_rtApertureCurrent]._srView); // aperture control texture
      SetTextureTemp tTex3(3, _dec[_decimation2x-1]._rt._srView); // 1x1 maxAvgMin

      // note: stage0 is set to point sampling by default (see FilterPoint in EngineDD11::PostProcess::Prepare)
      D3DTEXTUREFILTERTYPE_OLD filter = rescale ? D3DTEXF_LINEAR_OLD : D3DTEXF_POINT_OLD;
      SetSamplerStateTemp sampFilterMag0(0, D3DSAMP_MAGFILTER_OLD, filter);
      SetSamplerStateTemp sampFilterMin0(0, D3DSAMP_MINFILTER_OLD, filter);
      SetSamplerStateTemp sampFilterMip0(0, D3DSAMP_MIPFILTER_OLD, filter);
      SetSamplerStateTemp sampFilterMag1(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp sampFilterMin1(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp sampFilterMip1(1, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);

#ifndef _X360SHADERGEN
      SetSamplerStateTemp tempSRGB0(0, D3DSAMP_SRGBTEXTURE_OLD, rtIsSRGB);
      SetSamplerStateTemp tempSRGB1(1, D3DSAMP_SRGBTEXTURE_OLD, rtIsSRGB);
      SetSamplerStateTemp tempSRGB2(2, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
      SetSamplerStateTemp tempSRGB3(3, D3DSAMP_SRGBTEXTURE_OLD, decimatedSRGB);
#endif

      // compute r,g,b eye sensitivity

      // setup bloom and original blend factors
      if (_engine->_doBloom && _engine->_pp._bloom)
      {
        const float pars[4]={_engine->_pp._bloom->GetBlurScale(), _engine->_pp._bloom->GetImageScale(), 0, 0};
        _engine->SetPixelShaderConstantF(cb, PSC_BloomPars, pars, 1);
      }
      else
      {
        const float pars[4]={0, 1, 0, 0};
        _engine->SetPixelShaderConstantF(cb, PSC_BloomPars, pars, 1);
      }

      if (_engine->_nightEyeThis>1e-3f)
      {
        // Set the pixel shader
        _engine->SetPixelShader(cb, _psGlowNight);

        // Value designed for night blue shift
        static float coneAdjust = 60.0f;
        float lumScale = coneAdjust/_engine->_accomFactorThis;

        // Get cones
        float cones = Cones();

        // Get Eye sens
        Color eyeSens = EyeSens(cones);
        
        // Set the temporary shader constant
        SetShaderConstantTemp spc6(PSC_RgbEyeCoef, D3DXVECTOR4(eyeSens.R(), eyeSens.G(), eyeSens.B(), 1) );
        SetShaderConstantTemp spc8(PSC_NightControl, D3DXVECTOR4(lumScale, cones, 0,0));

        // Drawing
        _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);
      }
      else
      {
        // Set the pixel shader
        ColorVal sens = _engine->_nightVisionThis ? NVGSens : DaySens;
        // Set the temporary shader constant
        _engine->SetPixelShader(cb, _engine->_nightVisionThis ? _psGlowNVG : _psGlow);

        // Set the temporary shader constant
        // Drawing
        SetShaderConstantTemp spc6(PSC_RgbEyeCoef, D3DXVECTOR4(sens.R(), sens.G(), sens.B(), 1));
        _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);
      }
      
      if (!isLast)
      {
        _engine->ResolveRenderTarget();
      }

    }

    // unbind any textures to avoid any hanging states
    _engine->SetTexture(cb, 0, NULL);
    _engine->SetTexture(cb, 1, NULL);
    _engine->SetTexture(cb, 2, NULL);
    _engine->SetTexture(cb, 3, NULL);
    
    // remember properties of this frame
    _engine->_accomFactorPrev = _engine->_accomFactorThis;
  }
 
  Finish();
#else
  _engine->NoPostprocess();
#endif
  // Switching TL off
}

float EngineDD11::PPGlow::Cones() const
{
  // when adaptation is under 1, we assume iris is fully open
  // under certain level light is received only by rods
  // when accomFactor is high, we are receiving lower

  // subjectiveIntensity = realIntensity*_accomFactorThis
  // realIntensity = subjectiveIntensity/_accomFactorThis;
  static float coneFull = 0.5f;
  static float coneNo = 1.1f;
  float cones = InterpolativC(_engine->_accomFactorThis,coneFull,coneNo,1,-0.2);

  return cones;
}

Color EngineDD11::PPGlow::EyeSens(float cones) const
{
  // make sure we calculate luminance taking rods/cell sensitivity into account
  Color eyeSens = DaySens*cones+NightSens*(1-cones);

  return eyeSens;
}

bool EngineDD11::PPFlareIntensity::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs)
{
#if defined(_XBOX) || defined(_X360SHADERGEN)
  if (!base::Init(hlsl,vs,"VSPostProcess")) return false;
  // copy pixel shader is enough for the flare effect, we are downsizing only 4x
  if (!CompilePixelShader(hlsl,"PSPostProcessCopy", _ps))
  {
    return false;
  }
#else
  if (!base::Init(hlsl,vs)) return false;
#endif
  // Create flare intensity textures + render targets
  // Flare rendering is done in the back buffer - use same format as backbuffer
  for (int i = 0; i < RenderTargetNum; i++)
  {
    int size = 1 << (RenderTargetNum - i - 1);
    HRESULT err;
    err = _engine->CreateRenderTarget(size, size, _engine->_swapChainDesc.BufferDesc.Format, 1, 0, D3D11_USAGE_DEFAULT, true, _rtLightValues[i]);
    if (err != D3D_OK) {_engine->DDError11("Error: Render target texture creation failed.", err); return false;}
  }

  //Success
  return true;
}

void EngineDD11::PPFlareIntensity::Prepare()
{
  // base can be PostProcess (PC) or PPSimple (X360). PPSimple will set shaders
  base::Prepare();
}

static inline void saturate( LONG &a, LONG minV, LONG maxV )
{
  if( a<minV ) a=minV;
  if( a>maxV ) a=maxV;
}

void EngineDD11::PPFlareIntensity::Do(int x, int y, int sizeX, int sizeY)
{
  #ifndef _XBOX
  // TODOX360: flare visibility testing using visibility predication
  // Draw
  PROFILE_SCOPE_GRF_EX(ppFlr,pp,SCOPE_COLOR_YELLOW);

  const int cb = -1;
  // If scene is not being rendered initialize scene
  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();

  // Prepare drawing
  Prepare();
  

  // Set temporary render states
  SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);

  // Copy original render target surface into small texture
  RECT rect;
  rect.left = x - sizeX / 2;
  rect.right = x + sizeX / 2;
  rect.top = y - sizeY / 2;
  rect.bottom = y + sizeY / 2;
  // flares are rendered after Postprocess, therefore we are already running at backbuffer resolution
  int w = _engine->WidthBB();
  int h = _engine->HeightBB();
  saturate(rect.left,0,w);
  saturate(rect.right,0,w);
  saturate(rect.top,0,h);
  saturate(rect.bottom,0,h);
  if (rect.bottom<=rect.top || rect.right<=rect.left)
  {
    // singular case - no flare
    float color[4] = {0,0,0,0};
    _engine->GetD3DDeviceNonRec()->ClearRenderTargetView(_rtLightValues[0]._rtView, Array<float>(color, 4));
  }
  else
  {
#ifdef _XBOX
    _engine->GetD3DDeviceNonRec()->SetPixelShader(_ps);

    // store current render target
    ComRef<IDirect3DSurface9> oldRT;
    _engine->GetD3DDeviceNonRec()->GetRenderTarget(0,oldRT.Init());
    // start by resolving a corresponding part of the screen
    _engine->GetD3DDeviceNonRec()->Resolve(
      D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS,
      (const D3DRECT *)&rect,_rtLightValues[0],NULL,0,0,NULL,0,0,NULL
      );
#else
  #ifndef _X360SHADERGEN
    _engine->_pp._stretchRect->Do(_engine->_currRenderTarget._srView, &rect, _rtLightValues[0], NULL);
  #endif
#endif
  }

  // Downsize somewhat (16x for RenderTargetNum=4)
  for (int i = 1; i < RenderTargetNum; i++)
  {
    #if defined(_XBOX) || defined(_X360SHADERGEN)
    // render from _rtLightValues[i-1] into _rtsLightValues[i - 1]
    _engine->SetTexture(cb, 0, _rtLightValues[i-i]);
    _engine->GetD3DDeviceNonRec()->SetRenderTarget(0,_rtsLightValues[i - 1]);
    // render a full-screen quad with a downscaling pixel shader 
    _engine->GetD3DDeviceNonRec()->DrawPrimitive(D3DPT_TRIANGLELIST_OLD, 0, 2);

    _engine->SetTexture(cb, 0, NULL);

    // now resolve into _rtLightValues[i]
    _engine->GetD3DDeviceNonRec()->Resolve(
      D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS,
      NULL,_rtLightValues[i],NULL,0,0,NULL,0,0,NULL
      );
    #else
    _engine->_pp._stretchRect->Do(_rtLightValues[i-1]._srView, NULL, _rtLightValues[i], NULL);
    #endif
  }

  // Set light values texture to stage 12
  _engine->SetTexture(cb, 12, _rtLightValues[RenderTargetNum - 1]._srView);

  // If scene is not being rendered finish initialized scene
  if (!inScene) _engine->D3DEndScene();

  #ifdef _XBOX
  _engine->GetD3DDeviceNonRec()->SetRenderTarget(0,oldRT);
  #endif
  
  // Restore usual rendering settings
  Finish();
#endif
}

//==================================================================================================
// ou, ov ...texcoords sampling offsets
static bool CreateScreenPlane( VertexP3T2 *v, int segsX, int segsY, float ou, float ov )
{
  float x0 = -1.0f; // -0.5
  float y0 = -1.0f;
  float z = 1.0f;
  float tu0 = 0.0f;
  float tv0 = 1.0f;
  float dx = 2.0 / segsX;
  float dy = 2.0 / segsY;
  float du = 1.0 / segsX;
  float dv = -1.0 / segsY;
  float ya = y0;
  float yb = y0 + dy;
  float tva = tv0 + ov;
  float tvb = tva + dv;
  float x;
  float tu;
  int i, j;
  for( j = 0; j < segsY; j++ )
  {
    x = x0;
    tu = tu0 + ou;
    for( i = 0; i <= segsX; i++ )
    {
      v->pos = Vector3P( x, ya, z ); v->tc.u = tu; v->tc.v = tva; ++v;
      v->pos = Vector3P( x, yb, z ); v->tc.u = tu; v->tc.v = tvb; ++v;
      if( i == segsX )
      {
        if( j != segsY - 1 )
        {
          v->pos = Vector3P( x, yb, z ); v->tc.u = tu; v->tc.v = tvb; ++v;
          v->pos = Vector3P( x0, yb, z ); v->tc.u = tu0; v->tc.v = tvb; ++v;
        }
      }
      else
      {
        x += dx;
        tu += du;
      }
    }
    ya = yb; yb += dy;
    tva = tvb; tvb += dv;
  }
  return true;
}

//==================================================================================================
static bool CreateScreenPlane2( VertexP3T2 *v, int segsX, int segsY )
{
  float randmaxr = 2 * H_PI / RAND_MAX;
  int n = 3; // num randoms per vertex
  int ls = (segsX+2) * n; // line size
  int size = ls * (segsY+2);

  AutoArray<float, MemAllocLocal<float, 4096> > a;
  a.Resize(size);
  for( int i = 0; i < size; i++ )
    a[i] = rand() * randmaxr - H_PI;
  
  bool even = true;
  float dx = 2.0 / segsX;
  float dy = 2.0 / segsY;
  float xa, xb, ya, yb;
  float xa0 = -1.0f - dx * 0.5f;
  float xb0 = -1.0f;
  int i, j;
  ya = -1.0f;
  yb = ya + dy;
  for( j = 0; j < segsY; j++ )
  {
    if( even )
    {
      xa = xa0; xb = xb0;
      for( i = 0; i <= (segsX+1); i++ )
      {
        v->pos = Vector3P( xa, ya, a[ls*j + i*n] ); v->tc.u = a[ls*j + i*n + 1]; v->tc.v = a[ls*j + i*n + 2]; ++v;
        v->pos = Vector3P( xb, yb, a[ls*(j+1) + i*n] ); v->tc.u = a[ls*(j+1) + i*n + 1]; v->tc.v = a[ls*(j+1) + i*n + 2]; ++v;
        xa += dx; xb += dx;
      }
    }
    else
    {
      xa = xb0 - dx; xb = xa0;
      for( i = 0; i <= (segsX+1); i++ )
      {
        v->pos = Vector3P( xa, ya, a[ls*j + (i-1)*n] ); v->tc.u = a[ls*j + (i-1)*n + 1]; v->tc.v = a[ls*j + (i-1)*n + 2]; ++v;
        v->pos = Vector3P( xb, yb, a[ls*(j+1) + i*n] ); v->tc.u = a[ls*(j+1) + i*n + 1]; v->tc.v = a[ls*(j+1) + i*n + 2]; ++v;
        xa += dx; xb += dx;
      }
    }
    ya = yb; yb += dy;
    even = !even;
  }
  return true;
}

//==================================================================================================
void EngineDD11::PPRotBlur::SetThresholdAngle( float angle )
{
  minCos = cos( angle * H_PI / 180 );
}
//==================================================================================================
bool EngineDD11::PPRotBlur::Init( const LazyLoadFile &hlsl, const LazyLoadFile &vs )
{
  //if( !base::Init( hlsl, vs, "VsPpRotBlur" ) ) return false;
  if (!base::Init(hlsl,vs)) return false;
  enabled = false;
  SetDefaultValues();

  // screen plane construction
  static const int segsX = 16;
  static const int segsY = 10;
  static const int vertexCountConst = ( segsX + 1 + 1 ) * 2 * segsY - 2;
  vertexCount = vertexCountConst;
  stripSize = vertexCount - 2;

  // setup initial data
  VertexP3T2 v[vertexCountConst];
  // pixel offsets
  float ou = 0; //See the Nearest-Point Sampling in the DX9 documentation
  float ov = 0;
  CreateScreenPlane( v, segsX, segsY, ou, ov );

  // Create the VB
  ComRef<IDirect3DVertexBuffer9Old> vb;

  D3D11_BUFFER_DESC bd;
  bd.Usage = D3D11_USAGE_DEFAULT;
  bd.ByteWidth = sizeof(VertexP3T2) * vertexCount;
  bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = 0;
  bd.MiscFlags = 0;

  D3D11_SUBRESOURCE_DATA srData;
  srData.pSysMem = v;
  srData.SysMemPitch = 0;
  srData.SysMemSlicePitch = 0;

  HRESULT hr = _engine->GetD3DDeviceNonRec()->CreateBuffer(&bd, &srData, vb.Init());
  if (FAILED(hr))
  {
    LogF("Error: Vertex buffer creation failed.");
    return false;
  }

  // Create the VD
  const D3D11_INPUT_ELEMENT_DESC vertexDecl[] =
  {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  // create vertex shader
  if (!CompileVertexShader(vs,"VsPpRotBlur",_vs, vertexDecl, sizeof(vertexDecl)/sizeof(vertexDecl[0])))
  {
    return false;
  }


  _vb = VertexStaticData11::BufferFuture(vb);

  if( !CompilePixelShader( hlsl, "PsPpRotBlur", _ps ) ) return false;
  //_ppGaussianBlur = ppGaussianBlur;
  //cam = camera;
  
  _lastTime = Glob.time;

  _oldView = MIdentity;

  // angle upper bound
  _upperBound = 10.0f * (H_PI / 180.0f);

  return true;
}
//==================================================================================================
void EngineDD11::PPRotBlur::Prepare()
{
  const int cb = -1;
  base::Prepare();
  // Set the stream source
  _engine->SetStreamSource( cb, _vb, sizeof(VertexP3T2), vertexCount );
  // Set the vertex shader
  _engine->SetVertexShader(cb, _vs);
  // Set the pixel shader
  _engine->SetPixelShader(cb, _ps);
}

//==================================================================================================
void EngineDD11::PPRotBlur::Do( bool isLast )
{
  ///if( !enabled ) return;
  // Draw
  PROFILE_SCOPE_GRF_EX( ppRotBlur, pp, SCOPE_COLOR_YELLOW );

  const int cb = -1;
  // threshold
  // current cam matrix
  Matrix4 &m1 = _engine->CBState(cb)._view;

  //  last frame view matrix, inverse current view matrix
  Matrix3 final3 = _oldView.Orientation() * m1.Orientation().InverseGeneral();

  // convert matrix to axis-align form, for this we use quaternion
  Quaternion<float> quatern;
  quatern.FromMatrixRotation(final3);
  quatern.Normalize();

  float cosAngle = quatern.W();
  // rotation angle, which is used as lower and upper bound
  float angle = acos(cosAngle) * 2.0f;

  static float BPMult = 0.33f;
  float bPower = (_engine->_nightVisionThis) ? (blurPower * BPMult) : blurPower;

  // apply rotation blur only when rotation angle is greater than lower bound
  if (angle < minCos)
  {
    // If scene is not being rendered initialize scene
    bool inScene = _engine->_d3dFrameOpen;
    if (!inScene) _engine->D3DBeginScene();

    // Prepare drawing
    Prepare();

    // Set temporary render states
    SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);

    // Stage 0 - base texture - do the copy
    TextureHandle11* rt = _engine->CreateRenderTargetCopyAsTexture();
    SetTextureTemp tTex0(0, rt);
#ifndef _X360SHADERGEN
    SetSamplerStateTemp ss0SRGBTEXTURE(0, D3DSAMP_SRGBTEXTURE_OLD, _engine->_caps._rtIsSRGB);
#endif
    SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
    SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
    SetSamplerStateTemp ss0MIPFILTER(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);
    SetSamplerStateTemp ss0ADDRESSU(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
    SetSamplerStateTemp ss0ADDRESSV(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

    // Stage 1 - depth map
    SetTextureTemp tTex1(1, _engine->GetDepthAsTexture());
#ifndef _X360SHADERGEN
    SetSamplerStateTemp ss1SRGBTEXTURE(1, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
#endif
    SetSamplerStateTemp ss1MINFILTER(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
    SetSamplerStateTemp ss1MAGFILTER(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
    SetSamplerStateTemp ss1MIPFILTER(1, D3DSAMP_MIPFILTER_OLD, D3DTEXF_POINT_OLD);
    SetSamplerStateTemp ss1ADDRESSU(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
    SetSamplerStateTemp ss1ADDRESSV(1, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

    /*
    -----------------------------------------------------------------------------------------------------------
    Originally was motion vector determined by difference between position in current frame and last frame and 
    for this purpose were both matrices send into vertex shader ...
    
      posA = pos * matrixA ... old view matrix
      posB = pos * matrixB ... current view matrix
      dir = posB - posA
      dir = dir * inverseMatrixB
    
    Now we used improved method based on single matrix, way to determine this matrix is shown in next lines ...
    
      dir = (posB - posA) * inverseMatrixB
      dir = (pos * matrixB - pos * matrixA) * inverseMatrixB
      dir = pos - (pos * matrixA * inverseMatrixB);
    -----------------------------------------------------------------------------------------------------------
    */

    Vector3 vec(quatern.X(), quatern.Y(), quatern.Z());
    float dt = Glob.time - _lastTime;

    // fps > 30
    if (dt < 0.033333f)
    {
      if (angle >_upperBound)
      {
        final3.SetRotationAxis(vec, _upperBound);
      }
    }
    else
    {
      // rotation bound is select as min(maximum rotational angle, angular velocity with regard to 30fps)
      float currBound = min(_upperBound, (angle / dt) * 0.033333f);

      final3.SetRotationAxis(vec, currBound);
    }

    //tune opportunity
    //static float depthThreshold_ = 1.5f;
    //SetShaderConstantTemp svc0( 0, D3DXVECTOR4( final3(0, 0), final3(0, 1), final3(0, 2), depthThreshold_ ) );
    SetShaderConstantTemp svc0( VSC_RotBlur_mA0, D3DXVECTOR4( final3(0, 0), final3(0, 1), final3(0, 2), depthThreshold ) );
    SetShaderConstantTemp svc1( VSC_RotBlur_mA1, D3DXVECTOR4( final3(1, 0), final3(1, 1), final3(1, 2), bPower ) );
    SetShaderConstantTemp svc2( VSC_RotBlur_mA2, D3DXVECTOR4( final3(2, 0), final3(2, 1), final3(2, 2), 0 ) );    

    float kx = 1.0f / _engine->CBState(cb)._proj( 0, 0 );
    float ky = 1.0f / _engine->CBState(cb)._proj( 1, 1 );
    float ktc = _engine->CBState(cb)._proj( 1, 1 )*kx;
    SetShaderConstantTemp svc6( VSC_ProjConsts, D3DXVECTOR4( kx, ky, ktc, 0 ) );
    //SetShaderConstantTemp svc6( 6, D3DXVECTOR4( 1.0, 1.0, 1.0, 0 ) );

    // Drawing
    _engine->DrawPrimitive( cb, D3DPT_TRIANGLESTRIP_OLD, 0, stripSize );

#ifdef _XBOX
    // Resolve content of the render target
    CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _engine->_currRenderTarget, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif

    // If scene is not being rendered finish initialized scene
    if (!inScene) _engine->D3DEndScene();

    // Restore usual rendering settings
    Finish();
  }

  _lastTime = Glob.time;
  _oldView = m1;
}
//==================================================================================================
void EngineDD11::PPSimpleExt::Prepare()
{
  base::Prepare();
  const int cb = -1;
  const PPResourcesExt *res = Resources();
  if (res)
  {
    // Set the stream source
    _engine->SetStreamSource(cb,res->_vb, sizeof(Vertex), 4);

    // Set the vertex shader
    _engine->SetVertexShader(cb,res->_vs);

    // Set the pixel shader
    _engine->SetPixelShader(cb,res->_ps);
  }
}
//==================================================================================================
bool EngineDD11::PPSimpleExt::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, const char *vsName)
{
  if (!base::Init(hlsl,vs)) return false;

  Ref<PPResourcesExt> res = new PPResourcesExt;

  // setup initial data
  Vertex v[4];
  float offsetU = 0;
  float offsetV = 0;
  v[0].pos = Vector3P(-0.5, -0.5, 0.5); v[0].t0.u = 0 + offsetU; v[0].t0.v = 1 + offsetV;
  v[1].pos = Vector3P(-0.5,  0.5, 0.5); v[1].t0.u = 0 + offsetU; v[1].t0.v = 0 + offsetV;
  v[2].pos = Vector3P( 0.5, -0.5, 0.5); v[2].t0.u = 1 + offsetU; v[2].t0.v = 1 + offsetV;
  v[3].pos = Vector3P( 0.5,  0.5, 0.5); v[3].t0.u = 1 + offsetU; v[3].t0.v = 0 + offsetV;

  // Create the VB
  ComRef<IDirect3DVertexBuffer9Old> vb;

  D3D11_BUFFER_DESC bd;
  bd.Usage = D3D11_USAGE_DEFAULT;
  bd.ByteWidth = sizeof(Vertex) * 4;
  bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = 0;
  bd.MiscFlags = 0;

  D3D11_SUBRESOURCE_DATA srData;
  srData.pSysMem = v;
  srData.SysMemPitch = 0;
  srData.SysMemSlicePitch = 0;

  HRESULT hr = _engine->GetD3DDeviceNonRec()->CreateBuffer(&bd, &srData, vb.Init());
  if (FAILED(hr))
  {
    LogF("Error: Vertex buffer creation failed.");
    return false;
  }

  // Create the VD
  const D3D11_INPUT_ELEMENT_DESC vertexDecl[] =
  {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  // create vertex shader
  if (!CompileVertexShader(vs,vsName,res->_vs, vertexDecl, sizeof(vertexDecl)/sizeof(vertexDecl[0])))
  {
    return false;
  }

  res->_vb = VertexStaticData11::BufferFuture(vb);
  _resrc = res;

  return true;
}
//==================================================================================================
bool EngineDD11::PPSimpleExt::Init(PPResources *resources, bool initFirst)
{
  if (resources)
  {
    SetLevel(0xFF); // default level
    _resrc = resources;
    return true;
  }

  return false;
}

//==================================================================================================
bool EngineDD11::PPRadialBlur::Init( const LazyLoadFile &hlsl, const LazyLoadFile &vs, bool firstInit )
{
  if( !base::Init( hlsl, vs, "VSPostProcess" ) ) return false;
  PPResourcesExt *res = const_cast<PPResourcesExt *> (base::Resources());
  if( !CompilePixelShader( hlsl, "PsPpRadialBlur", res->_ps, true) ) return false;
  
  if (firstInit)
  {
    SetDefaultValues();
    enabled = false;
    _pInterp.Initialize();
  }

  return true;
}

bool EngineDD11::PPRadialBlur::Init(PPResources *resources, bool firstInit)
{
  if (!base::Init(resources, firstInit)) return false;

  if (firstInit)
  {
    SetDefaultValues();
    enabled = false;
    _pInterp.Initialize();
  }

  return true;
}

int EngineDD11::PPRadialBlur::SetParams(const AutoArray<float> &pars)
{
  if (pars.Size() == 4)
  {
    SetBlurPower( pars[0], pars[1] );
    SetBlurOffset( pars[2], pars[3] );

    return 0;
  }

  return 4;
}

int EngineDD11::PPRadialBlur::SetParams(const float* pars, int nPars)
{
  if (nPars == 4)
  {
    SetBlurPower( pars[0], pars[1] );
    SetBlurOffset( pars[2], pars[3] );

    return 0;
  }
  return 4;
}
//==================================================================================================
bool EngineDD11::PPRadialBlur::UnInit()
{
  if (_resrc) 
  { 
    _resrc->UnInit(); 
    return true;
  }

  return false;
}
//==================================================================================================
void EngineDD11::PPRadialBlur::Do( bool isLast )
{
  if( !enabled )
    return;

  // Draw
  PROFILE_SCOPE_GRF_EX( ppRadialBlur, pp, SCOPE_COLOR_YELLOW );

  // linearly interpolated parameters
  RadBlurPar pars = _pInterp.Simulate();
  const PPResourcesExt *res = Resources();
  const int cb = -1;

  if (!res) return;

  if (!isLast)
  {
    if (pars._powerX <= 0 && pars._powerY <= 0)
      return; //no need to do anything - zero effect strength
    if (pars._offsetX >= 0.5f && pars._offsetY >= 0.5f)
      return; //no need to do anything - zero effect strength
  }


  // If scene is not being rendered initialize scene
  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();

  // Prepare drawing
  Prepare();

  // Set temporary render states
  SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);

  // Stage 0 - base texture - do the copy (to make sure same data is both in source texture and destination render target)
  TextureHandle11* rt = _engine->CreateRenderTargetCopyAsTexture();
  SetTextureTemp tTex0(0, rt);
#ifndef _X360SHADERGEN
  SetSamplerStateTemp ss0SRGBTEXTURE(0, D3DSAMP_SRGBTEXTURE_OLD, _engine->_caps._rtIsSRGB);
#endif
  SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0MIPFILTER(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0ADDRESSU(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_MIRROR_OLD);
  SetSamplerStateTemp ss0ADDRESSV(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_MIRROR_OLD);

  // if effect is last then switch to back buffer
  if (isLast) _engine->SwitchToBackBufferRenderTarget();

  // Prepare PS constants
  //SetShaderConstantTemp spc0( 0, D3DXVECTOR4( blurPowerX, blurPowerY, blurOffsetX, blurOffsetY ) );
  SetShaderConstantTemp spc0( PSC_RadialBlurPars, D3DXVECTOR4( pars._powerX, pars._powerY, pars._offsetX, pars._offsetY ) );
  float kx = 1.0 / _engine->CBState(cb)._proj( 0, 0 );
  float ky = 1.0 / _engine->CBState(cb)._proj( 1, 1 );
  float ktc = _engine->CBState(cb)._proj( 1, 1 )*kx;
  SetShaderConstantTemp spc6( PSC_ProjConsts, D3DXVECTOR4( kx, ky, ktc, 0 ) );

  // Drawing
  _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#ifdef _XBOX
  // Resolve content of the render target
  CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _engine->_currRenderTarget, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif

  // If scene is not being rendered finish initialized scene
  if (!inScene) _engine->D3DEndScene();

  // Restore usual rendering settings
  Finish();
}

LSError EngineDD11::PPRadialBlur::Serialize( ParamArchive &ar )
{
  CHECK(ar.Serialize("enabled",enabled,0,false));
  CHECK(ar.Serialize("interp", _pInterp, 0));
  CHECK(ar.Serialize("prior", _priority, 0, -1));

  return LSOK;
}
//==================================================================================================
bool EngineDD11::PPColors::Init(PPResources *resources, bool firstInit)
{
  if (resources)
  {
    _resrc = resources;

    if (firstInit)
    {
      enabled = false;
      SetDefaultValues();
      _pInterp.Initialize();
    }

    return true;
  }

  return false;
}
//==================================================================================================
bool EngineDD11::PPColors::Init( const LazyLoadFile &hlsl, const LazyLoadFile &vs, bool firstInit )
{
  if (!base::Init(hlsl,vs)) return false;
  // sepia test
  //SetColorizeColor( Color( 1.25f, 0.85f, 0.60f, 0.0f ) );

  Ref<PPColorResources> resrc = new PPColorResources;

  // screen plane construction
  const int segsX = 1;
  const int segsY = 1;
  const int vertexCountConst = ( segsX + 1 + 1 ) * 2 * segsY - 2;
  resrc->_vertexCount = vertexCountConst;
  resrc->_stripSize = resrc->_vertexCount - 2;

  VertexP3T2 v[vertexCountConst];
  // pixel offsets
  float ou = 0;
  float ov = 0;
  CreateScreenPlane( v, segsX, segsY, ou, ov );

  // Create the VB
  ComRef<IDirect3DVertexBuffer9Old> vb;

  D3D11_BUFFER_DESC bd;
  bd.Usage = D3D11_USAGE_DEFAULT;
  bd.ByteWidth = sizeof(VertexP3T2) * resrc->_vertexCount;
  bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = 0;
  bd.MiscFlags = 0;

  D3D11_SUBRESOURCE_DATA srData;
  srData.pSysMem = v;
  srData.SysMemPitch = 0;
  srData.SysMemSlicePitch = 0;

  HRESULT hr = _engine->GetD3DDeviceNonRec()->CreateBuffer(&bd, &srData, vb.Init());
  if (FAILED(hr))
  {
    LogF("Error: Vertex buffer creation failed.");
    return false;
  }

  // Create the VD
  const D3D11_INPUT_ELEMENT_DESC vertexDecl[] =
  {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  // create vertex shader
  // note: vertex shader is stored in PS.hlsl for simple effects
  if (!CompileVertexShader(vs,"VsPpColors",resrc->_vs, vertexDecl, sizeof(vertexDecl)/sizeof(vertexDecl[0])))
  {
    return false;
  }

  resrc-> _vb = VertexStaticData11::BufferFuture(vb);
  if( !CompilePixelShader( hlsl, "PsPpColors", resrc->_ps ) ) return false;

#if _ARMA3_RADIAL_COLOR_PP
  if( !CompilePixelShader( hlsl, "PsPpColorsRadial", resrc->_psRadial ) ) return false;
#endif  

  _resrc = resrc;

  if (firstInit)
  {
    SetDefaultValues();
    enabled = false;
    _pInterp.Initialize();
  }

  return true;
}

//==================================================================================================
#if _ARMA3_RADIAL_COLOR_PP

bool EngineDD11::PPColors::UseRadialShader(const ColorPar* pars) 
{
  if (pars)
  {
    return (pars->_radialMajorAxisRadius > 0 && pars->_radialMinorAxisRadius > 0 );
  }

  ColorPar parsLocal = _pInterp.Simulate();
  return (parsLocal._radialMajorAxisRadius > 0 && parsLocal._radialMinorAxisRadius > 0 );
}

void EngineDD11::PPColors::SetupRadialShaderConsts(D3DXVECTOR4* vectors, const ColorPar& pars)
{
  vectors[0].x = pars._radialCenterX;
  vectors[0].y = pars._radialCenterY;

  // compute scale values
  if (pars._radialMajorAxisRadius <= 0 || pars._radialMinorAxisRadius <= 0)
  {
    vectors[0].z = 1000.0f;
    vectors[0].w = 1000.0f;
  }
  else
  {
    //float maxVal = floatMax(pars._radialMajorAxisRadius, pars._radialMinorAxisRadius);

    //vectors[0].z = (0.5f / pars._radialMajorAxisRadius) * a/maxVal;
    //vectors[0].w = (0.5f / pars._radialMinorAxisRadius) * b/maxVal;
    vectors[0].z = 1.0f / pars._radialMajorAxisRadius;
    vectors[0].w = 1.0f / pars._radialMinorAxisRadius;
  }

  vectors[1].x = sin(DEG2RAD * pars._radialRotationDeg);
  vectors[1].y = cos(DEG2RAD * pars._radialRotationDeg);
  vectors[1].z = pars._radialInnerRadiusCoef;
  vectors[1].w = pars._radialInterpCoef;
}
#endif //_ARMA3_RADIAL_COLOR_PP


//==================================================================================================
void EngineDD11::PPColors::DoDirect(const RECT *rect, int idx)
{
#if _HELISIM_RENDER_TO_TEXTURE
  DoAssert(_engine->_r2tTargets[idx]._clrPars);

  if (!_engine->_r2tTargets[idx]._clrPars)
    return;

  const PPColorResources* res = Resources();
  DoAssert(res);

  if (!res)
    return;

  // remember all we can
  PushStreamSource pshStreamSource(true);
  PushVertexShader pshVertexShader(true);
  PushPixelShader pshPixelShader(true);
  PushRenderTargetAndView pushRTV(true);

  MyD3DDeviceCB& device = _engine->GetD3DDeviceRec();

  R2TColorsPars &clrPars =  *_engine->_r2tTargets[idx]._clrPars.GetRef();
  {

    // We will need a source as a texture
    ComRef<TextureHandle11> rtCopy = _engine->CreateR2TRenderTargetCopyAsTexture(rect);
    _engine->DoSetTexture(0, rtCopy);

    _engine->SetRenderTargetAndDepthStencilDirect(_engine->_r2tRT._renderTargetS, DepthStencil::DepthStencilNULL, 0.0f, 1.0f);
    Color clearColor(HBlack);
    device->ClearRenderTargetView(_engine->_r2tRT._renderTargetS._rtView, Array<float>((float*)&clearColor, 4));

  
    _engine->DoSetStreamSource(res->_vb, sizeof(VertexP3T2), 0); //setting 0 as size is ok, it is only used for optimizations
    _engine->DoSetVertexShader(res->_vs._shader, res->_vs._inputLayout);
    _engine->DoSetPixelShader(res->_ps);

#if _ENABLE_CHEATS
    //static bool SaveToTexture = false;

    //if (GInput.GetCheat3ToDo(DIK_F))
    //  SaveToTexture = true;

    //if (SaveToTexture)
    //  D3DXSaveTextureToFile("rtCopy.png", D3DXIFF_PNG, rtCopy, NULL);
#endif

    SetRenderStateTemp rstemp1(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD, true);
    SetRenderStateTemp rstemp2(D3DRS_ZWRITEENABLE_OLD, FALSE, true);
    SetRenderStateTemp rstemp3(D3DRS_ZENABLE_OLD, FALSE, true);
    SetRenderStateTemp rstemp4(D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
    SetRenderStateTemp rstemp5(D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD, true);
    SetRenderStateTemp rstemp6(D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD, true);
    SetRenderStateTemp rstemp7(D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD, true);

    // Prepare PS constants
    SetShaderConstantTemp ssPpColorsPars( PSC_PPColorsPars, D3DXVECTOR4(clrPars._brightness, clrPars._contrast, clrPars._offset, 0) );
    SetShaderConstantTemp ssLerpColor( PSC_LerpColor, D3DXVECTOR4(clrPars._lerpColor.R(), clrPars._lerpColor.G(), clrPars._lerpColor.B(), clrPars._lerpColor.A()) );
    SetShaderConstantTemp ssColorizeColor( PSC_ColorizeColor, D3DXVECTOR4(clrPars._colorizeColor.R(), clrPars._colorizeColor.G(), clrPars._colorizeColor.B(), clrPars._colorizeColor.A()) );
    SetShaderConstantTemp ssRgbWeights( PSC_RGBWeights, D3DXVECTOR4(clrPars._rgbWeights.R(), clrPars._rgbWeights.G(), clrPars._rgbWeights.B(), 0) );
    SetShaderConstantTemp spc6( PSC_ProjConsts, D3DXVECTOR4(clrPars._coeffs.X(), clrPars._coeffs.Y(), clrPars._coeffs.Z(), 0) );

    // Drawing
    _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, res->_stripSize );

    _engine->DoSetTexture(0, 0);

  }
#endif
}

//==================================================================================================
void EngineDD11::PPColors::Do( bool isLast )
{
  if( !enabled )
    return;

  // Draw
  PROFILE_SCOPE_GRF_EX( ppColors, pp, SCOPE_COLOR_YELLOW );

  ColorPar pars = _pInterp.Simulate();
  const PPColorResources* res = Resources();
  const int cb = -1;

  if (!res) return;

  // If scene is not being rendered initialize scene
  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();

  // Prepare drawing
  Prepare();

  // Set temporary render states
  SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);

  // Stage 0 - base texture - do the copy (to make sure same data is both in source texture and destination render target)
  TextureHandle11* rt = _engine->CreateRenderTargetCopyAsTexture();
  SetTextureTemp tTex0(0, rt);

  // if effect is last then switch to back buffer
  if (isLast) _engine->SwitchToBackBufferRenderTarget();

#ifndef _X360SHADERGEN
  SetSamplerStateTemp ss0SRGBTEXTURE(0, D3DSAMP_SRGBTEXTURE_OLD, _engine->_caps._rtIsSRGB);
#endif
  SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0MIPFILTER(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);
  SetSamplerStateTemp ss0ADDRESSU(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss0ADDRESSV(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

  // Prepare PS constants
  SetShaderConstantTemp ssPpColorsPars( PSC_PPColorsPars, D3DXVECTOR4( pars._brightness, pars._contrast, pars._offset, 0 ) );
  SetShaderConstantTemp ssLerpColor( PSC_LerpColor, D3DXVECTOR4( pars._lerpColor.R(), pars._lerpColor.G(), pars._lerpColor.B(), pars._lerpColor.A() ) );
  SetShaderConstantTemp ssColorizeColor( PSC_ColorizeColor, D3DXVECTOR4( pars._colorizeColor.R(), pars._colorizeColor.G(), pars._colorizeColor.B(), pars._colorizeColor.A() ) );
  SetShaderConstantTemp ssRgbWeights( PSC_RGBWeights, D3DXVECTOR4( pars._rgbWeights.R(), pars._rgbWeights.G(), pars._rgbWeights.B(), 0 ) );

  float kx = 1.0 / _engine->CBState(cb)._proj( 0, 0 );
  float ky = 1.0 / _engine->CBState(cb)._proj( 1, 1 );
  float ktc = _engine->CBState(cb)._proj( 1, 1 )*kx;
  SetShaderConstantTemp spc6( PSC_ProjConsts, D3DXVECTOR4( kx, ky, ktc, 0 ) );

#if _ARMA3_RADIAL_COLOR_PP
  // radial shader params
  D3DXVECTOR4 radPars[2];
  SetupRadialShaderConsts(radPars, pars);
  SetShaderConstantTemp ssPpColorsParsRad1( PSC_PPColorRadPars1, radPars[0]);
  SetShaderConstantTemp ssPpColorsParsRad2( PSC_PPColorRadPars2, radPars[1]);
#endif


  // Drawing
  _engine->DrawPrimitive( cb, D3DPT_TRIANGLESTRIP_OLD, 0, res->_stripSize );

#ifdef _XBOX
  if (!isLast)
  {
    // Resolve content of the render target
    CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _engine->_currRenderTarget, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
  }
#endif

  // If scene is not being rendered finish initialized scene
  if (!inScene) _engine->D3DEndScene();

  // Restore usual rendering settings
  Finish();
}
//==================================================================================================
void EngineDD11::PPColors::Prepare()
{
  const int cb = -1;
  const PPColorResources *res = Resources();

  if (!res) return;

  base::Prepare();
  // Set the stream source
  _engine->SetStreamSource(cb, res->_vb, sizeof(VertexP3T2), res->_vertexCount);
  // Set the vertex shader
  _engine->SetVertexShader(cb, res->_vs);
  // Set the pixel shader
#if _ARMA3_RADIAL_COLOR_PP
  if (UseRadialShader())
  {
    // setup radial shader
    _engine->SetPixelShader(cb, res->_psRadial);
  }
  else
#endif
  {
    // setup simple shader
    _engine->SetPixelShader(cb, res->_ps);
  }
}
//==================================================================================================
int EngineDD11::PPColors::SetParams(const AutoArray<float> &pars)
{
  if (pars.Size() >= 15)
  {
    SetBrightness(pars[0]);
    SetContrast(pars[1]);
    SetOffset(pars[2]);
    SetBlendColor(Color( pars[3], pars[4], pars[5], pars[6] ));
    SetColorizeColor(Color( pars[7], pars[8], pars[9], pars[10] ));
    SetRGBWeights(Color( pars[11], pars[12], pars[13], pars[14] ));

#if _ARMA3_RADIAL_COLOR_PP
    if (pars.Size() >= 22 )
    {
      // setup radial pars
      SetRadialMajorAxisRadius(pars[15]);
      SetRadialMinorAxisRadius(pars[16]);
      SetRadialRotationDeg(pars[17]);
      SetRadialCenterX(pars[18]);
      SetRadialCenterY(pars[19]);
      SetRadialInnerRadiusCoef(pars[20]);
      SetRadialInterpCoef(pars[21]);
    }
#endif

    return 0;
  }

  return 15;
}

int EngineDD11::PPColors::SetParams(const float* pars, int nPars)
{
  if (nPars >= 15)
  {
    SetBrightness(pars[0]);
    SetContrast(pars[1]);
    SetOffset(pars[2]);
    SetBlendColor(Color( pars[3], pars[4], pars[5], pars[6] ));
    SetColorizeColor(Color( pars[7], pars[8], pars[9], pars[10] ));
    SetRGBWeights(Color( pars[11], pars[12], pars[13], pars[14] ));

#if _ARMA3_RADIAL_COLOR_PP
    if (nPars >= 22 )
    {
      // setup radial pars
      SetRadialMajorAxisRadius(pars[15]);
      SetRadialMinorAxisRadius(pars[16]);
      SetRadialRotationDeg(pars[17]);
      SetRadialCenterX(pars[18]);
      SetRadialCenterY(pars[19]);
      SetRadialInnerRadiusCoef(pars[20]);
      SetRadialInterpCoef(pars[21]);
    }
#endif

    return 0;
  }

  return 15;
}

//==================================================================================================
bool EngineDD11::PPColors::UnInit()
{
  if (_resrc) 
  {
    _resrc->UnInit();
    return true;
  }

  return false;
}
//==================================================================================================
LSError EngineDD11::PPColors::Serialize( ParamArchive &ar )
{
  CHECK(ar.Serialize("enabled",enabled,0,false));
  CHECK(ar.Serialize("pInterp",_pInterp,0));
  CHECK(ar.Serialize("prior", _priority, 0, -1));

  return LSOK;
}
//==================================================================================================
bool EngineDD11::PPChromAber::Init( const LazyLoadFile &hlsl, const LazyLoadFile &vs, bool firstInit )
{
  if( !base::Init( hlsl, vs, "VSPostProcess" ) ) return false;
  PPResourcesExt *res = const_cast<PPResourcesExt *> (base::Resources());
  if( !CompilePixelShader( hlsl, "PsPpChromAber", res->_ps, true) ) return false;

  if (firstInit)
  {
    SetDefaultValues();
    enabled = false;
    _pInterp.Initialize();
  }

  return true;
}

bool EngineDD11::PPChromAber::Init(PPResources *resources, bool firstInit)
{
  if (!base::Init(resources, firstInit)) return false;
  
  if (firstInit)
  {
    SetDefaultValues();
    enabled = false;
    _pInterp.Initialize();
  }

  return true;
}

//==================================================================================================
int EngineDD11::PPChromAber::SetParams(const AutoArray<float> &pars)
{
  if (pars.Size() == 3)
  {
    SetAberration( pars[0], pars[1] );
    SetAspectCorrection( pars[2] > 0.0f );

    return 0;
  }

  return 3;
}

int EngineDD11::PPChromAber::SetParams(const float* pars, int nPars)
{
  if (nPars == 3)
  {
    SetAberration( pars[0], pars[1] );
    SetAspectCorrection( pars[2] > 0.0f );

    return 0;
  }

  return 3;
}
//==================================================================================================
bool EngineDD11::PPChromAber::UnInit()
{
  if (_resrc)
  {
    _resrc->UnInit();
    return true;
  }

  return false;
}
//==================================================================================================
void EngineDD11::PPChromAber::Do( bool isLast )
{
  if( !enabled )
    return;

  // Draw
  PROFILE_SCOPE_GRF_EX( ppChromAber, pp, SCOPE_COLOR_YELLOW );
  
  // linearly interpolated parameters 
  ChromAberPar pars = _pInterp.Simulate();
  const int cb = -1;


  // If scene is not being rendered initialize scene
  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();

  // Prepare drawing
  Prepare();

  // if effect is last then switch to back buffer
  if (isLast) _engine->SwitchToBackBufferRenderTarget();
  
  // Set temporary render states
  SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);

  // Stage 0 - base texture - do the copy
  TextureHandle11* rt = _engine->CreateRenderTargetCopyAsTexture();
  SetTextureTemp tTex0(0, rt);
#ifndef _X360SHADERGEN
  SetSamplerStateTemp ss0SRGBTEXTURE(0, D3DSAMP_SRGBTEXTURE_OLD, _engine->_caps._rtIsSRGB);
#endif
  SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0ADDRESSU(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss0ADDRESSV(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

  float ac = 1;
  if( pars._aspectCorrection )
    ac = _engine->CBState(cb)._proj( 0, 0 ) / _engine->CBState(cb)._proj( 1, 1 );
  //SetShaderConstantTemp spc6( 6, D3DXVECTOR4( ac, 0, 0, 0 ) );
  // Prepare PS constants
  SetShaderConstantTemp spc0( PSC_ChromAbberPars, D3DXVECTOR4( pars._powerX * ac, pars._powerY, 0, 0 ) );

  // Drawing
  _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#ifdef _XBOX
  // Resolve content of the render target
  CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _engine->_currRenderTarget, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif

  
  // If scene is not being rendered finish initialized scene
  if (!inScene) _engine->D3DEndScene();

  // Restore usual rendering settings
  Finish();
}

LSError EngineDD11::PPChromAber::Serialize( ParamArchive &ar )
{
  CHECK(ar.Serialize("enabled",enabled,0,false));
  CHECK(ar.Serialize("interp", _pInterp, 0));
  CHECK(ar.Serialize("prior", _priority, 0, -1));

  return LSOK;
}

bool EngineDD11::PPWetDistort::Init(PPResources *resources, PPGaussianBlur *ppGaussianBlur, bool firstInit)
{
  if (resources)
  {
    _resrc = resources;
    _ppGaussianBlur = ppGaussianBlur;

    if (firstInit)
    {
      SetDefaultValues();
      enabled = false;
      _pInterp.Initialize();
    }

    return true;
  }

  return false;
}

//////////////////////////////////////////////////////////////////////////

bool EngineDD11::PPColorsInversion::Init( const LazyLoadFile &hlsl, const LazyLoadFile &vs, bool firstInit )
{
  if( !base::Init( hlsl, vs, "VSPostProcess" ) ) return false;
  PPResourcesExt *res = const_cast<PPResourcesExt *> (base::Resources());
  if( !CompilePixelShader( hlsl, "PsPpClrInversion", res->_ps, true) ) return false;

  if (firstInit)
  {
    SetDefaultValues();
    _enabled = false;
    _pInterp.Initialize();
  }

  return true;
}

bool EngineDD11::PPColorsInversion::Init(PPResources *resources, bool firstInit)
{
  if (!base::Init(resources, firstInit)) return false;

  if (firstInit)
  {
    SetDefaultValues();
    _enabled = false;
    _pInterp.Initialize();
  }

  return true;
}

bool EngineDD11::PPColorsInversion::UnInit()
{
  if (_resrc)
  {
    _resrc->UnInit();
    return true;
  }

  return false;
}

void EngineDD11::PPColorsInversion::Do(bool isLast)
{
  if (!_enabled) return;

  // Draw
  PROFILE_SCOPE_GRF_EX(ppColorInvers, pp, SCOPE_COLOR_YELLOW);

  // linearly interpolated parameters 
  ColorInversionPar pars = _pInterp.Simulate();
  const int cb = -1;

  // If scene is not being rendered initialize scene
  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();

  // Prepare drawing
  Prepare();

  // Set temporary render states
  SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);

  // Stage 0 - base texture - do the copy
  TextureHandle11* rt = _engine->CreateRenderTargetCopyAsTexture();
  SetTextureTemp tTex0(0, rt);

  // if effect is last then switch to back buffer
  if (isLast) _engine->SwitchToBackBufferRenderTarget();

#ifndef _X360SHADERGEN
  SetSamplerStateTemp ss0SRGBTEXTURE(0, D3DSAMP_SRGBTEXTURE_OLD, _engine->_caps._rtIsSRGB);
#endif
  SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0ADDRESSU(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss0ADDRESSV(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

  // Prepare PS constants
  SetShaderConstantTemp spc0(PSC_ClrInversPars, D3DXVECTOR4(pars._invColor.R(), pars._invColor.G(), pars._invColor.B(), 0));

  // Drawing
  _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#ifdef _XBOX
  // Resolve content of the render target
  CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _engine->_currRenderTarget, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif


  // If scene is not being rendered finish initialized scene
  if (!inScene) _engine->D3DEndScene();

  // Restore usual rendering settings
  Finish();
}

int EngineDD11::PPColorsInversion::SetParams(const AutoArray<float> &pars)
{
  if (pars.Size() == 3)
  {
    SetInversion(Color(pars[0], pars[1], pars[2], 0));
    return 0;
  }

  return 1;
}

int EngineDD11::PPColorsInversion::SetParams(const float* pars, int nPars)
{
  if (nPars == 3)
  {
    SetInversion(Color(pars[0], pars[1], pars[2], 0));
    return 0;
  }

  return 1;
}

LSError EngineDD11::PPColorsInversion::Serialize( ParamArchive &ar )
{
  CHECK(ar.Serialize("enabled", _enabled,0,false));
  CHECK(ar.Serialize("interp", _pInterp, 0));
  CHECK(ar.Serialize("prior", _priority, 0, -1));

  return LSOK;
}

//==================================================================================================
bool EngineDD11::PPWetDistort::Init( const LazyLoadFile &hlsl, const LazyLoadFile &vs, PPGaussianBlur *ppGaussianBlur, bool firstInit )
{
  if( !base::Init(hlsl,vs) )
    return false;
  _ppGaussianBlur = ppGaussianBlur;	

  Ref<PPWetDistortResources> res = new PPWetDistortResources;

  // screen plane construction
  const int segsX = 80;
  const int segsY = 50;
  const int vertexCountConst = ( segsX + 1 + 1 ) * 2 * segsY;
  res->_vertexCount = vertexCountConst;
  res->_stripSize = res->_vertexCount - 2;

  VertexP3T2 v[vertexCountConst];
  CreateScreenPlane2( v, segsX, segsY );

  // Create the VB
  ComRef<IDirect3DVertexBuffer9Old> vb;

  D3D11_BUFFER_DESC bd;
  bd.Usage = D3D11_USAGE_DEFAULT;
  bd.ByteWidth = sizeof(VertexP3T2) * res->_vertexCount;
  bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = 0;
  bd.MiscFlags = 0;

  D3D11_SUBRESOURCE_DATA srData;
  srData.pSysMem = v;
  srData.SysMemPitch = 0;
  srData.SysMemSlicePitch = 0;

  HRESULT hr = _engine->GetD3DDeviceNonRec()->CreateBuffer(&bd, &srData, vb.Init());
  if (FAILED(hr))
  {
    LogF("Error: Vertex buffer creation failed.");
    return false;
  }

  // Create the VD
  const D3D11_INPUT_ELEMENT_DESC vertexDecl[] =
  {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  // create vertex shader
  if (!CompileVertexShader(vs,"VsPpWetDistort",res->_vs, vertexDecl, sizeof(vertexDecl)/sizeof(vertexDecl[0])))
  {
    return false;
  }
  res->_vb = VertexStaticData11::BufferFuture(vb);
  if( !CompilePixelShader( hlsl, "PsPpWetDistort", res->_ps ) ) return false;

  _resrc = res;

  if (firstInit)
  {
    SetDefaultValues();
    enabled = false;
    _pInterp.Initialize();
  }

  return true;

}

int EngineDD11::PPWetDistort::SetParams(const AutoArray<float> &pars)
{
  if (pars.Size() == 15)
  {
    SetBlurriness( pars[0] );
    SetEffectPower( pars[1], pars[2] );
    SetWaveSpeeds( pars[3], pars[4], pars[5], pars[6] );
    SetWaveAmplitudes( pars[7], pars[8], pars[9], pars[10] );
    SetPhaseCoefs( pars[11], pars[12], pars[13], pars[14] );

    return 0;
  }

  return 15;
}

int EngineDD11::PPWetDistort::SetParams(const float* pars, int nPars)
{
  if (nPars == 15)
  {
    SetBlurriness( pars[0] );
    SetEffectPower( pars[1], pars[2] );
    SetWaveSpeeds( pars[3], pars[4], pars[5], pars[6] );
    SetWaveAmplitudes( pars[7], pars[8], pars[9], pars[10] );
    SetPhaseCoefs( pars[11], pars[12], pars[13], pars[14] );

    return 0;
  }

  return 15;
}

bool EngineDD11::PPWetDistort::UnInit()
{
  _ppGaussianBlur.Free();
  
  if (_resrc) 
  {
    _resrc->UnInit();
    return true;
  }

  return false;
}
//==================================================================================================
void EngineDD11::PPWetDistort::Prepare()
{
  const int cb = -1;
  const PPWetDistortResources *res = Resources();
  if (res)
  {
    base::Prepare();
    // Set the stream source
    _engine->SetStreamSource( cb, res->_vb, sizeof(VertexP3T2), res->_vertexCount );
    // Set the vertex shader
    _engine->SetVertexShader(cb, res->_vs);
    // Set the pixel shader
    _engine->SetPixelShader(cb, res->_ps);
  }
}

//==================================================================================================
void EngineDD11::PPWetDistort::Do( bool isLast )
{
  if( !enabled )
    return;

  // Draw
  PROFILE_SCOPE_GRF_EX( ppWetDistort, pp, SCOPE_COLOR_YELLOW );
  
  WetDistortPar pars = _pInterp.Simulate();
  const int cb = -1;
  const PPWetDistortResources *res = Resources();

  if (!res) return;
 
  // If scene is not being rendered initialize scene
  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();

  // Prepare drawing
  Prepare();

  // Set temporary render states
  SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);

  // Stage 0 - base texture - do the copy
  TextureHandle11* rt = _engine->CreateRenderTargetCopyAsTexture();
  SetTextureTemp tTex0(0, rt);
#ifndef _X360SHADERGEN
  SetSamplerStateTemp ss0SRGBTEXTURE(0, D3DSAMP_SRGBTEXTURE_OLD, _engine->_caps._rtIsSRGB);
#endif
  SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0ADDRESSU(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss0ADDRESSV(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

  // Stage 1 - blurred texture
  SetTextureTemp tTex1(1, _ppGaussianBlur->GetBlurredRt()._srView);
#ifndef _X360SHADERGEN
  SetSamplerStateTemp ss1SRGBTEXTURE(1, D3DSAMP_SRGBTEXTURE_OLD, _engine->_caps._rtIsSRGB);
#endif
  SetSamplerStateTemp ss1MINFILTER(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss1MAGFILTER(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss1ADDRESSU(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss1ADDRESSV(1, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

  float t = Glob.time.toFloat();
  SetShaderConstantTemp svc0( VSC_PPWetDistortPars, D3DXVECTOR4( t, pars._blurriness, pars._powerTop, pars._powerBottom) ); // time, blur, powerTop, powerBottom
  SetShaderConstantTemp svc1( VSC_PPWDSpeeds, D3DXVECTOR4( pars._waveSpeeds[0], pars._waveSpeeds[1], pars._waveSpeeds[2], pars._waveSpeeds[3] ) ); // speeds
  SetShaderConstantTemp svc2( VSC_PPWDAmps, D3DXVECTOR4( pars._waveAmps[0], pars._waveAmps[1], pars._waveAmps[2], pars._waveAmps[3] ) ); // amplitudes
  SetShaderConstantTemp svc3( VSC_PPWDPhases, D3DXVECTOR4( pars._phaseCoefs[0], pars._phaseCoefs[1], pars._phaseCoefs[2], pars._phaseCoefs[3] ) ); // randX, randY, posX, posY

  // chromatic aberration params
  SetShaderConstantTemp spc4( PSC_PPWDAberration, D3DXVECTOR4( 0.8, 1.0, 1.2, 0 ) );

  // if effect is last then switch to back buffer
  if (isLast) _engine->SwitchToBackBufferRenderTarget();
  
  float kx = 1.0 / _engine->CBState(cb)._proj( 0, 0 );
  float ky = 1.0 / _engine->CBState(cb)._proj( 1, 1 );
  float ktc = _engine->CBState(cb)._proj( 1, 1 ) *kx;
  SetShaderConstantTemp svc6( VSC_ProjConsts, D3DXVECTOR4( kx, ky, ktc, 0 ) );
  // Drawing
  _engine->DrawPrimitive( cb, D3DPT_TRIANGLESTRIP_OLD, 0, res->_stripSize );

#ifdef _XBOX
  // Resolve content of the render target
  CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _engine->_currRenderTarget, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif
  
  // If scene is not being rendered finish initialized scene
  if (!inScene) _engine->D3DEndScene();
  // Restore usual rendering settings
  Finish();
}

LSError EngineDD11::PPWetDistort::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("enabled",enabled,0,false));
  CHECK(ar.Serialize("pInterp",_pInterp,0));  
  CHECK(ar.Serialize("prior", _priority, 0, -1));

  return LSOK;
}

//==================================================================================================
bool EngineDD11::PPDynamicBlur::Init( const LazyLoadFile &hlsl, const LazyLoadFile &vs, PPGaussianBlur *ppGaussianBlur, bool firstInit )
{
  if( !base::Init( hlsl,vs ) )
    return false;
  _ppGaussianBlur = ppGaussianBlur;

  Ref<PPDynBlurResources> res = new PPDynBlurResources;

  // screen plane construction
  const int segsX = 1;
  const int segsY = 1;
  const int vertexCountConst = ( segsX + 1 + 1 ) * 2 * segsY - 2;
  res->_vertexCount = vertexCountConst;
  res->_stripSize = res->_vertexCount - 2;

  VertexP3T2 v[vertexCountConst];
  CreateScreenPlane( v, segsX, segsY, 0, 0 ); // ou, ov

  // Create the VB
  ComRef<IDirect3DVertexBuffer9Old> vb;

  D3D11_BUFFER_DESC bd;
  bd.Usage = D3D11_USAGE_DEFAULT;
  bd.ByteWidth = sizeof(VertexP3T2) * res->_vertexCount;
  bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = 0;
  bd.MiscFlags = 0;

  D3D11_SUBRESOURCE_DATA srData;
  srData.pSysMem = v;
  srData.SysMemPitch = 0;
  srData.SysMemSlicePitch = 0;

  HRESULT hr = _engine->GetD3DDeviceNonRec()->CreateBuffer(&bd, &srData, vb.Init());
  if (FAILED(hr))
  {
    LogF("Error: Vertex buffer creation failed.");
    return false;
  }

  res->_vb = VertexStaticData11::BufferFuture(vb);

  // Create the VD
  const D3D11_INPUT_ELEMENT_DESC vertexDecl[] =
  {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  // create vertex shader
  // note: vertex shader is stored in PS.hlsl for simple effects
  if (!CompileVertexShader(vs,"VsPpDynamicBlurFinal",res->_vsFinal, vertexDecl, sizeof(vertexDecl)/sizeof(vertexDecl[0])))
  {
    return false;
  }
  if( !CompileVertexShader( vs,"VsPpDynamicBlur", res->_vsBlur, vertexDecl, sizeof(vertexDecl)/sizeof(vertexDecl[0])))
    return false;

  if( !CompilePixelShader( hlsl, "PsPpDynamicBlurFinal", res->_psFinal ) )
    return false;
  if( !CompilePixelShader( hlsl, "PsPpDynamicBlur", res->_psBlur ) )
    return false;

  _resrc = res;

  if (firstInit)
  {
    SetDefaultValues();
    _enabled = false;
    _pInterp.Initialize();
  }

  return true;
}

bool EngineDD11::PPDynamicBlur::Init(PPResources *resources, PPGaussianBlur *ppGaussianBlur, bool firstInit)
{
  if (resources)
  {
    _ppGaussianBlur = ppGaussianBlur;
    _resrc = resources;

    if (firstInit)
    {
      SetDefaultValues();
      _enabled = false;
      _pInterp.Initialize();
    }

    return true;
  }

  return false;
}

int EngineDD11::PPDynamicBlur::SetParams(const AutoArray<float> &pars) 
{
  if (pars.Size() == 1)
  {
    SetBlurrines(pars[0]);

    return 0;
  }

  return 1;
}

int EngineDD11::PPDynamicBlur::SetParams(const float* pars, int nPars)
{
  if (nPars == 1)
  {
    SetBlurrines(pars[0]);
    return 0;
  }

  return 1;
}
//==================================================================================================
bool EngineDD11::PPDynamicBlur::UnInit()
{
  _ppGaussianBlur.Free();
  
  if (_resrc)
  {
    _resrc->UnInit();
  }

  return true;
}
//==================================================================================================
void EngineDD11::PPDynamicBlur::Do( bool isLast )
{
  if (!_enabled) return;

  DynamicBlurPar pars = _pInterp.Simulate();

  // Draw
  PROFILE_SCOPE_GRF_EX( ppDynamicBlur, pp, SCOPE_COLOR_YELLOW );

  const int cb = -1;
  const PPDynBlurResources *res = Resources();

  if (!res) return;

  // TESTING
  /*float t = Glob.time.toFloat();
  blurriness = ( sinf( t * 2 ) + 1 ) * 20 - 5 ;
  if( blurriness < 0 )
  blurriness = 0;
  if( blurriness > 30 )
  blurriness = 30;*/

  // If scene is not being rendered initialize scene
  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();

  // Prepare drawing
  Prepare();

  // Protect VS constants from changing
  // DX11 TODO remove or repair 
  //PushVertexShaderConstant pushC0(0);
  //PushVertexShaderConstant pushC1(1);
  //PushVertexShaderConstant pushC2(2);
  //PushVertexShaderConstant pushC3(3);
  //PushVertexShaderConstant pushC4(4);
  //PushVertexShaderConstant pushC5(5);
  //PushVertexShaderConstant pushC6(6);
  //PushVertexShaderConstant pushC7(7);
  //PushVertexShaderConstant pushC8(8);

  int fullWidth = _engine->Width();
  int fullHeight = _engine->Height();
  int partWidth = fullWidth / GBSizeDivisor;
  int partHeight = fullHeight / GBSizeDivisor;

  //int modPartH = fullHeight % GBSizeDivisor;
  //partHeight += modPartH / (GBSizeDivisor / 2);

  float invDiv = 1.0f / GBSizeDivisor;
  // blur direction
  float dirX = 2;
  float dirY = 1;
  // normalize to length  (0.5 / 100 / halfSteps)
  float d = 0.00091f / sqrtf( dirX * dirX + dirY * dirY );
  dirX *= d; dirY *= d;
  //
  float blurLim = 100 * 10.0f / fullWidth;
  // blur coef for blend phase
  float blur0 = pars._blurriness / blurLim;
  if( blur0 > 1 )
    blur0 = 1;
  // blur coef for blur phase
  float blur1 = pars._blurriness - blurLim;
  if( blur1 < 0 )
    blur1 = 0;
  dirX *= blur1; dirY *= blur1;

  float aspect = (float)fullWidth / (float)fullHeight;
  // pixel offsets
  float ks = 0.5f;
  float ou = 0;
  float ov = 0;


  ////////////////////////////////////////////////
  // GAUSS BLUR PASSES
  if( blur1 > 0 )
  {
    const int kernelSize = 6;
    float weights[kernelSize][4];
    float suma = 0;
    for( int i = 0; i < kernelSize; i++ )
    {
      weights[i][0] = ComputeGaussianValue( (float)i, 0, 2.8f );
      if( i > 0 )
        suma += weights[i][0] * 2;
      else
        suma += weights[i][0];
    }
    // normalize kernel weights
    float divider = 1.0f/suma; //1.0f/660;
    for( int i = 0; i < kernelSize; i++ )
      weights[i][0] *= divider;
    
    // Set the stream source
    _engine->SetStreamSource( cb, res->_vb, sizeof(VertexP3T2), res->_vertexCount );
    // Set the vertex shader
    _engine->SetVertexShader(cb, res->_vsBlur);
    // Set the pixel shader
    _engine->SetPixelShader(cb, res->_psBlur);

    ////////////////////////////////////////////////
    // BLUR PASS 1
    {
      // Save current render target and depth stencil
      _engine->PushRenderTargetDepthStencil();

      //_engine->SetTemporaryRenderTarget(_rtHorizontal, _rtsHorizontal, zeroDepth);
      _engine->SetRenderTargetAndDepthStencil( _ppGaussianBlur->GetFirstRt(), DepthStencil::DepthStencilNULL, 0, 1);

      // set viewport - it has to be done, because it is different from the size of whole RT!
      _engine->SetViewport(partWidth, partHeight, 0, 1);


      // Set temporary render states
      SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);
#ifndef _X360SHADERGEN
      // Use SRGB conversion in texture read
      SetSamplerStateTemp ssSRGBTEXTURE(0, D3DSAMP_SRGBTEXTURE_OLD, _engine->_caps._rtIsSRGB);
      // Write in the same space as you read // _VBS3_TI
      SetRenderStateTemp rsSRGBSRGBWRITEENABLE(D3DRS_SRGBWRITEENABLE_OLD, _engine->_caps._rtIsSRGB);
#endif
      // source texture is second RT of PPGaussianBlur
      SetTextureTemp tTex0( 0, _ppGaussianBlur->GetBlurredRt()._srView );
      SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp ss0MIPFILTER(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp ss0ADDRESSU(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
      SetSamplerStateTemp ss0ADDRESSV(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

      // set shader params
      SetShaderConstantTemp svc0( VSC_PPDynBlurPars0, D3DXVECTOR4( blur0, 1, 1, 0 ) ); // blurriness, 
      SetShaderConstantTemp svc1( VSC_PPDynBlurPars1, D3DXVECTOR4( dirX, dirY * aspect, ou*GBSizeDivisor, ov*GBSizeDivisor) );
      _engine->SetPixelShaderConstantF( cb, PSC_PPDynBlurWeights, *weights, kernelSize );

      // Drawing
      _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#ifdef _XBOX
      // Resolve content of the render target
      CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL,
        _ppGaussianBlur->GetFirstRtTexture(), NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif
    }
    //////////////////////////////////////////////
     //PASS 2
    {
      // render to second RT of PPGaussianBlur
      _engine->SetRenderTargetAndDepthStencil( _ppGaussianBlur->GetSecondRt(), DepthStencil::DepthStencilNULL );
      // source texture is first RT of PPGaussianBlur
      SetTextureTemp tTex0( 0, _ppGaussianBlur->GetFirstRt()._srView );
      SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp ss0MIPFILTER(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);
      SetSamplerStateTemp ss0ADDRESSU(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
      SetSamplerStateTemp ss0ADDRESSV(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);
      // set shader params
      SetShaderConstantTemp svc0( VSC_PPDynBlurPars0, D3DXVECTOR4( blur0, invDiv, invDiv - 0.001f, 0 ) );
      SetShaderConstantTemp svc1( VSC_PPDynBlurPars1, D3DXVECTOR4( -dirY, -dirX * aspect * invDiv, ou * GBSizeDivisor, ov) ); //  * invDiv
      _engine->SetPixelShaderConstantF( cb, PSC_PPDynBlurWeights, *weights, kernelSize );

      // Drawing
      _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);

#ifdef _XBOX
      // Resolve content of the render target
      CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _ppGaussianBlur->GetBlurredTexture(), NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif
    }		
    // Restore the original render target and depth stencil buffer
    _engine->PopRenderTargetDepthStencil();
  }
  // FINAL PASS
  // pixel offsets
  ks = 0.34f;
  ou = 0;
  ov = 0;

  // Set the stream source
  _engine->SetStreamSource(cb, res->_vb, sizeof(VertexP3T2), res->_vertexCount);
  // Set the vertex shader
  _engine->SetVertexShader(cb, res->_vsFinal);
  // Set the pixel shader
  _engine->SetPixelShader(cb, res->_psFinal);

  // Set temporary render states
  SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);

  // Stage 0 - base texture - do the copy
  TextureHandle11* rt = _engine->CreateRenderTargetCopyAsTexture();

  SetTextureTemp tTex0( 0, rt );
#ifndef _X360SHADERGEN
  SetSamplerStateTemp ss0SRGBTEXTURE(0, D3DSAMP_SRGBTEXTURE_OLD, _engine->_caps._rtIsSRGB);
#endif
  SetSamplerStateTemp ss0MINFILTER(0, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0MAGFILTER(0, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0MIPFILTER(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss0ADDRESSU(0, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss0ADDRESSV(0, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);

  // if effect is last then switch to back buffer
  if (isLast) _engine->SwitchToBackBufferRenderTarget();
  
  // Stage 1 - blurred texture
  SetTextureTemp tTex1(1, _ppGaussianBlur->GetBlurredRt()._srView);
#ifndef _X360SHADERGEN
  SetSamplerStateTemp ss1SRGBTEXTURE(1, D3DSAMP_SRGBTEXTURE_OLD, _engine->_caps._rtIsSRGB);
#endif
  SetSamplerStateTemp ss1MINFILTER(1, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss1MAGFILTER(1, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss1MIPFILTER(1, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);
  SetSamplerStateTemp ss1ADDRESSU(1, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD);
  SetSamplerStateTemp ss1ADDRESSV(1, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD);
  /*SetShaderConstantTemp svc0( 0, D3DXVECTOR4( blur0, 1, ou, ov ) ); // blurriness, 
  SetShaderConstantTemp svc1( 1, D3DXVECTOR4( dirX * 0.5, dirY * 0.5 * aspect, ou, ov) ); // blurriness,*/
  SetShaderConstantTemp svc0( VSC_PPDynBlurPars0, D3DXVECTOR4( blur0, aspect, ou, ov ) ); // blurriness, 
  SetShaderConstantTemp svc1( VSC_PPDynBlurPars1, D3DXVECTOR4( dirX * 0.5, dirY * 0.5, ou*GBSizeDivisor, ov*GBSizeDivisor ) ); // blurriness,*/

  // Drawing
  _engine->DrawPrimitive( cb, D3DPT_TRIANGLESTRIP_OLD, 0, res->_stripSize );
#ifdef _XBOX
  // Resolve content of the render target
  CALL_D3D_MEMBER(_engine,Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _engine->_currRenderTarget, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif

  // If scene is not being rendered finish initialized scene
  if (!inScene) _engine->D3DEndScene();
  // Restore usual rendering settings
  Finish();
}

LSError EngineDD11::PPDynamicBlur::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("enable", _enabled, 0, false));
  CHECK(ar.Serialize("pInterp",_pInterp,0));
  CHECK(ar.Serialize("prior", _priority, 0, -1));

  return LSOK;
}

//////////////////////////////////////////////////////////////////////////

bool EngineDD11::PPStretchRect::Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs)
{
  if (!base::Init(hlsl,vs)) return false;
  if (!CompilePixelShader(hlsl,"PSPostProcessCopy", _ps))
  {
    return false;
  }
  return true;
}


void EngineDD11::PPStretchRect::Do(bool isLast)
{
  Assert("Not supported");
}

void EngineDD11::PPStretchRect::DoDirect(const RECT *rect, int index)
{
  Assert("Not supported");
}

void EngineDD11::PPStretchRect::DoDirect(ID3D11ShaderResourceView* source, CONST RECT * sourceRect, const RenderTarget& destination, CONST RECT * destRect)
{
  Assert(source);
  if (!source)
    return;

  Assert(destination._rtView.NotNull());
  if (destination._rtView.IsNull())
    return;

  Assert(_engine);
  if (!_engine)
    return;

  PROFILE_SCOPE_GRF_EX(ppStR,pp,SCOPE_COLOR_YELLOW);

  // remember all we can
  PushStreamSource pshStreamSource(true);
  PushVertexShader pshVertexShader(true);
  PushPixelShader pshPixelShader(true);
  PushRenderTargetAndView pushRTV(true);

  // Prepare drawing
  _engine->DoSetStreamSource(_vbCustomEdge, sizeof(VertexCustomEdge), 4);
  _engine->DoSetVertexShader(_vsCustomEdge._shader, _vsCustomEdge._inputLayout);

  // set new render target
  _engine->SetRenderTargetAndDepthStencilDirect(destination, DepthStencil::DepthStencilNULL, 0, 1);

  int destWidth = 0;
  int destHeight = 0;
  if (destRect)
  {
    // setup destination rectangle if needed
    _engine->SetViewportDirect(destRect->left, destRect->top, destRect->right - destRect->left, destRect->bottom - destRect->top, 0, 1);
  }
  else
  {
    _engine->GetRenderTargetDimensions(destination, destWidth, destHeight);
  }

  // Prepare the UV coordinates offset coefficients
  float U[4][4];
  float V[4][4];

  int srcTotalWidth = 0;
  int srcTotalHeight = 0;

  _engine->GetShaderResourceDimensions(source, srcTotalWidth, srcTotalHeight);
  Assert(srcTotalWidth > 0 && srcTotalHeight > 0);

  int srcWidth = 0;
  int srcHeight = 0;

  float minU = 0.0f;
  float maxU = 1.0f;
  float minV = 0.0f;
  float maxV = 1.0f;

  if (sourceRect)
  {
    srcWidth = sourceRect->right - sourceRect->left;
    srcHeight = sourceRect->bottom - sourceRect->top;

    if (srcTotalWidth > 0)
    {
      minU = sourceRect->left / (float)srcTotalWidth;
      maxU = sourceRect->right / (float)srcTotalWidth;
    }
    if (srcTotalHeight > 0)
    {
      minV = sourceRect->top / (float)srcTotalHeight;
      maxV = sourceRect->bottom / (float)srcTotalHeight;
    }
  }
  else
  {
    srcWidth = srcTotalWidth;
    srcHeight = srcTotalHeight;
  }

  // Texture 0 - source image
  U[0][0] = U[1][0] = minU;
  U[2][0] = U[3][0] = maxU;
  V[0][0] = V[2][0] = maxV;
  V[1][0] = V[3][0] = minV;

  for (int vertex = 1; vertex < 4; vertex++)
  {
    // Texture 1
    U[vertex][1] = 0;
    V[vertex][1] = 0;
    // Texture 2
    U[vertex][2] = 0;
    V[vertex][2] = 0;
    // Texture 3
    U[vertex][3] = 0;
    V[vertex][3] = 0;
  }

  // Use the texture
  SetTextureTemp tRenderTarget(0, source, true);

  // Set temporary render states
  SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, 
    D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD, true);

  bool rescale = (srcWidth != destWidth) || (srcHeight != destHeight);
  D3DTEXTUREFILTERTYPE_OLD filter = rescale ? D3DTEXF_LINEAR_OLD : D3DTEXF_POINT_OLD;

  SetSamplerStateTemp sampFilterMag0(0, D3DSAMP_MAGFILTER_OLD, filter, true);
  SetSamplerStateTemp sampFilterMin0(0, D3DSAMP_MINFILTER_OLD, filter, true);
  SetSamplerStateTemp sampFilterMip0(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD, true);

  SetRenderStateTemp rsZTEST(D3DRS_ZENABLE_OLD,D3DZB_FALSE_OLD, true);
  SetRenderStateTemp rsALPHABLENDENABLE(D3DRS_ALPHABLENDENABLE_OLD, FALSE, true);

#ifndef _X360SHADERGEN
  // DX11 TODO SRGB
  BOOL rtIsSRGB = _engine->_caps._rtIsSRGB && _engine->_caps._canReadSRGB ;
  SetSamplerStateTemp tempSRGB0(0, D3DSAMP_SRGBTEXTURE_OLD, rtIsSRGB, true);
#endif

  const float posOffset[4] = {0,0,0,0};
  SetShaderConstantTemp(VSC_PPEdgeU, U[0], 4*4, true);
  SetShaderConstantTemp(VSC_PPEdgeV, V[0], 4*4, true);
  SetShaderConstantTemp(VSC_PPEdgePosOffset, posOffset, 1*4, true);
  _engine->DoSetPixelShader(_ps);

  // Drawing
  _engine->DoDrawPrimitive(D3DPT_TRIANGLESTRIP_OLD, 0, 2);
}

void EngineDD11::PPStretchRect::Do(ID3D11ShaderResourceView* source, CONST RECT * sourceRect, const RenderTarget& destination, CONST RECT * destRect)
{
  Assert(source);
  if (!source)
    return;

  Assert(destination._rtView.NotNull());
  if (destination._rtView.IsNull())
    return;

  Assert(_engine);
  if (!_engine)
    return;

  const int cb = -1;

  PROFILE_SCOPE_GRF_EX(ppStR,pp,SCOPE_COLOR_YELLOW);

  bool inScene = _engine->_d3dFrameOpen;
  if (!inScene) _engine->D3DBeginScene();

  // remember stream source and shaders
  PushStreamSource pshStreamSource;
  PushVertexShader pshVertexShader;
  PushPixelShader pshPixelShader;

  // Prepare drawing
  Prepare();

  // remember old render target and depth stencil (will be popped automatically)
  PushPopRenderTargetDepthStencilGuard pushPopGuard(_engine);

  // set new render target
  _engine->SetRenderTargetAndDepthStencil(destination, DepthStencil::DepthStencilNULL, 0, 1);

  int destWidth = 0;
  int destHeight = 0;
  if (destRect)
  {
    // setup destination rectangle if needed
    D3D11_VIEWPORT viewData;
    ZeroMemory(&viewData, sizeof(viewData));
    viewData.TopLeftX = destRect->left;
    viewData.TopLeftY = destRect->top;
    viewData.Width = destRect->right - destRect->left;
    viewData.Height = destRect->bottom - destRect->top;
    viewData.MinDepth = 0;
    viewData.MaxDepth = 1;
    CALL_D3D_MEMBER(_engine, RSSetViewports, &viewData);    
  }
  else
  {
    _engine->GetRenderTargetDimensions(destination, destWidth, destHeight);
  }

  // Prepare the UV coordinates offset coefficients
  float U[4][4];
  float V[4][4];

  int srcTotalWidth = 0;
  int srcTotalHeight = 0;

  _engine->GetShaderResourceDimensions(source, srcTotalWidth, srcTotalHeight);
  Assert(srcTotalWidth > 0 && srcTotalHeight > 0);

  int srcWidth = 0;
  int srcHeight = 0;

  float minU = 0.0f;
  float maxU = 1.0f;
  float minV = 0.0f;
  float maxV = 1.0f;

  if (sourceRect)
  {
    srcWidth = sourceRect->right - sourceRect->left;
    srcHeight = sourceRect->bottom - sourceRect->top;

    if (srcTotalWidth > 0)
    {
      minU = sourceRect->left / (float)srcTotalWidth;
      maxU = sourceRect->right / (float)srcTotalWidth;
    }
    if (srcTotalHeight > 0)
    {
      minV = sourceRect->top / (float)srcTotalHeight;
      maxV = sourceRect->bottom / (float)srcTotalHeight;
    }
  }
  else
  {
    srcWidth = srcTotalWidth;
    srcHeight = srcTotalHeight;
  }

  // Texture 0 - source image
  U[0][0] = U[1][0] = minU;
  U[2][0] = U[3][0] = maxU;
  V[0][0] = V[2][0] = maxV;
  V[1][0] = V[3][0] = minV;

  for (int vertex = 1; vertex < 4; vertex++)
  {
    // Texture 1
    U[vertex][1] = 0;
    V[vertex][1] = 0;
    // Texture 2
    U[vertex][2] = 0;
    V[vertex][2] = 0;
    // Texture 3
    U[vertex][3] = 0;
    V[vertex][3] = 0;
  }

  // Use the texture
  SetTextureTemp tRenderTarget(0, source);

  // Set temporary render states
  SetRenderStateTemp rsCOLORWRITEENABLE(D3DRS_COLORWRITEENABLE_OLD, 
    D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);

  bool rescale = (srcWidth != destWidth) || (srcHeight != destHeight);
  D3DTEXTUREFILTERTYPE_OLD filter = rescale ? D3DTEXF_LINEAR_OLD : D3DTEXF_POINT_OLD;

  SetSamplerStateTemp sampFilterMag0(0, D3DSAMP_MAGFILTER_OLD, filter);
  SetSamplerStateTemp sampFilterMin0(0, D3DSAMP_MINFILTER_OLD, filter);
  SetSamplerStateTemp sampFilterMip0(0, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);

  SetRenderStateTemp rsZTEST(D3DRS_ZENABLE_OLD,D3DZB_FALSE_OLD);
  SetRenderStateTemp rsALPHABLENDENABLE(D3DRS_ALPHABLENDENABLE_OLD, FALSE);

#ifndef _X360SHADERGEN
  // DX11 TODO SRGB
  BOOL rtIsSRGB = _engine->_caps._rtIsSRGB && _engine->_caps._canReadSRGB ;
  SetSamplerStateTemp tempSRGB0(0, D3DSAMP_SRGBTEXTURE_OLD, rtIsSRGB);
#endif

  // DX11 TODO remove or repair 
  //PushVertexShaderConstant pushC0(SC_PPEdgeU + 0);
  //PushVertexShaderConstant pushC1(SC_PPEdgeU + 1);
  //PushVertexShaderConstant pushC2(SC_PPEdgeU + 2);
  //PushVertexShaderConstant pushC3(SC_PPEdgeU + 3);
  //PushVertexShaderConstant pushC4(SC_PPEdgeU + 4);
  //PushVertexShaderConstant pushC5(SC_PPEdgeU + 5);
  //PushVertexShaderConstant pushC6(SC_PPEdgeU + 6);
  //PushVertexShaderConstant pushC7(SC_PPEdgeU + 7);
  //PushVertexShaderConstant pushC8(SC_PPEdgeU + 8);

  const float posOffset[4] = {0,0,0,0};
  _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeU, U[0], 4);
  _engine->SetVertexShaderConstantF(cb, VSC_PPEdgeV, V[0], 4);
  _engine->SetVertexShaderConstantF(cb, VSC_PPEdgePosOffset, posOffset, 1);
  _engine->SetPixelShader(cb,_ps);

  // Drawing
  _engine->DrawPrimitive(cb, D3DPT_TRIANGLESTRIP_OLD, 0, 2);

  if (!inScene) _engine->D3DEndScene();
  Finish();
}

//////////////////////////////////////////////////////////////////////////
void EngineDD11::ISpecialEffect::DestroyHndls()
{
  // suppressed assertion: ~Engine than _pp._effects are destroyed before ~ISpecialEffect - int hat case we try to 
  // destroy post effects which are destroyed yet
  if (_engine && _engine->_pp._effects.Size() > 0)
  {
    if (_handlesValid)
    {
      for (int i = 0; i< _hndls.Size(); i++)
      {
        if (_hndls[i] != InvalidPPHndl._hndl) _engine->DestroyPostprocess(_hndls[i]);
      }
    }
    Reset();
  }
  else
  {
    Reset();
  }
}

void EngineDD11::ISpecialEffect::Reset()
{
  _hndls.Clear();
  _handlesValid = false;
  _active = false;
  _timeStamp = Time(0);
  _lastUpdate = Time(0);
}

LSError EngineDD11::ISpecialEffect::Serialize(ParamArchive &ar)
{ 
  CHECK(ar.Serialize("active", _active, 0));
  CHECK(ar.Serialize("valid", _handlesValid, 0));
  CHECK(ar.Serialize("hndls", _hndls, 0));
  CHECK(::Serialize(ar, "timeStamp", _timeStamp, 0, Time(0)));
  CHECK(::Serialize(ar, "lastTime", _lastUpdate, 0, Time(0)));

  return LSOK; 
}


bool EngineDD11::FireSpecEffect::Init()
{
	Assert(!_handlesValid);

	_hndls.Realloc(1);
	_hndls.Resize(1);

	_hndls[0] = _engine->CreatePostprocess(PEColorCorrections, CCPriority);

	_handlesValid = (_hndls[0] != InvalidPPHndl._hndl);

	_pars.Realloc(15);
	_pars.Resize(15);
	// color correction pars
	_pars[0] = 1; _pars[1] = 1; _pars[2] = 0;
	_pars[3] = 0.1; _pars[4] = 0; _pars[5] = 0; _pars[6] = 0.3;
	_pars[7] = 1.0; _pars[8] = 0.5; _pars[9] = 0.5; _pars[10] = 0.1;
	_pars[11] = 0.199f; _pars[12] = 0.587f; _pars[13] = 0.144f; _pars[14] = 0;

	_timeStamp = Glob.time;
	_active = false;

	return _handlesValid;
}

void EngineDD11::FireSpecEffect::Update()
{
	
}

void EngineDD11::FireSpecEffect::SetParams(const float *pars, int nPars)
{
	if (!_handlesValid || _hndls.Size() != 1) return;

	_engine->EnablePostprocess(_hndls[0], true);
	_engine->SetPostprocessParams(_hndls[0], _pars);
	_engine->CommitPostprocess(_hndls[0], 0.0);

	_timeStamp = Glob.time;
	_active = false;
  if (nPars>=0)
  {
    _active = pars[0]>0.0f;
  }
  _engine->EnablePostprocess(_hndls[0], _active); 
}



bool EngineDD11::HitSpecEffect::Init()
{
  Assert(!_handlesValid);

  _hndls.Realloc(1);
  _hndls.Resize(1);
  
  _hndls[0] = _engine->CreatePostprocess(PEDynamicBlur, DBPriority);

  _handlesValid = (_hndls[0] != InvalidPPHndl._hndl);

  _timeStamp = Glob.time;
  _active = false;

  return _handlesValid;
}

void EngineDD11::HitSpecEffect::Update()
{
  if (_active && (_timeStamp + 2 < Glob.time) && _hndls.Size() == 1)
  {
    _engine->EnablePostprocess(_hndls[0], false);    
    _active = false;
  }
}

void EngineDD11::HitSpecEffect::SetParams(const float *pars, int nPars)
{
  if (!_handlesValid || _hndls.Size() != 1) return;
  
  const static float fadeOut = 1.25f;

  AutoArray<float> locPars;
  locPars.Realloc(1);    
  locPars.Resize(1);
  locPars[0] = pars[0];

  _engine->EnablePostprocess(_hndls[0], true);
  _engine->SetPostprocessParams(_hndls[0], locPars);
  _engine->CommitPostprocess(_hndls[0], 0);

  locPars[0] = 0;

  _engine->SetPostprocessParams(_hndls[0], locPars);
  _engine->CommitPostprocess(_hndls[0], fadeOut);

  _timeStamp = Glob.time;
  _active = true;
}

bool EngineDD11::MovementSpecEffect::Init()
{
  Assert(!_handlesValid);

  _hndls.Realloc(1);
  _hndls.Resize(1);
  _hndls[0] = _engine->CreatePostprocess(PERadialBlur, RBPriority);

  _pars.Realloc(4);
  _pars.Resize(4);

  _pars[0] = 0; _pars[1] = 0; 
#if _ARMA3_RADIAL_BLUR_MOD
  // modified radial blur - values need to be changed 
  _pars[2] = _pars[3] = 0.3f;
#else
  _pars[2] = 0.25; _pars[3] = 0.25;
#endif

  _handlesValid = (_hndls[0] != InvalidPPHndl._hndl);

  _timeStamp = Time(0) - 0.2;
  _active = false;

  return _handlesValid;
}

bool EngineDD11::TiredSpecEffect::Init()
{
  Assert(!_handlesValid);

  _hndls.Realloc(2);
  _hndls.Resize(2);

  // create post effects
  _hndls[0] = _engine->CreatePostprocess(PEColorCorrections, CCPriority); 
  _hndls[1] = _engine->CreatePostprocess(PEWetDistortion, WPriority);

  _handlesValid = (_hndls[0] != InvalidPPHndl._hndl && _hndls[1] != InvalidPPHndl._hndl);
  _timeStamp = Time(0);
  _lastUpdate = Time(0);
  _active = false;

  _pars.Realloc(15);
  _pars.Resize(15);
  // color correction pars
  _pars[0] = 1; _pars[1] = 1; _pars[2] = 0;
  _pars[3] = 0; _pars[4] = 0; _pars[5] = 0; _pars[6] = 0;
  _pars[7] = 0.9; _pars[8] = 0.8; _pars[9] = 0.7; _pars[10] = 0.75;
  _pars[11] = 0.199f; _pars[12] = 0.587f; _pars[13] = 0.144f; _pars[14] = 0;

  _parsCa.Realloc(15);
  _parsCa.Resize(15);
  // wet distortion
  _parsCa[0] = 0.1; _parsCa[1] = 0.15;_parsCa[2] = 0;
  _parsCa[3] = 2.1; _parsCa[4] = 3.7; _parsCa[5] = 2.5; _parsCa[6] = 1.85; 
  _parsCa[7] = 0.0051; _parsCa[8] = 0.0051; _parsCa[9] = 0.0051; _parsCa[10] = 0.0051; 
  _parsCa[11] = 0.5; _parsCa[12] = 0.3; _parsCa[13] = 10; _parsCa[14] = 4;

  return _handlesValid;
}

void EngineDD11::TiredSpecEffect::SetParams(const float *pars, int nPars)
{
  if (!_handlesValid || _hndls.Size() != 2) return;

  static const float tiredLB = 0.25f;
  static const float tiredUB = 1.0f;

  // low bound for applying effect
  if (pars[0] > tiredLB)
  {
    // update pars each 100ms
    if (_timeStamp < Glob.time)
    {
      static const float delay = 0.10;

      _pars[1] = InterpolativC(pars[0], tiredLB, tiredUB, 1, 1.5);   // 1 -> 1.5
      _pars[7] = InterpolativC(pars[0], tiredLB, tiredUB, 1, 0.9);   // 1 -> 0.9
      _pars[8] = InterpolativC(pars[0], tiredLB, tiredUB, 1, 0.8);   // 1 -> 0.8
      _pars[9] = InterpolativC(pars[0], tiredLB, tiredUB, 1, 0.7);   // 1 -> 0.7
      _pars[10] = InterpolativC(pars[0], tiredLB, tiredUB, 1, 0.75); // 1 -> 0.75

      _parsCa[0] = InterpolativC(pars[0], tiredLB, tiredUB, 0, 0.1);
      _parsCa[1] = InterpolativC(pars[0], tiredLB, tiredUB, 0, 0.15);

      _engine->SetPostprocessParams(_hndls[0], _pars);
      _engine->SetPostprocessParams(_hndls[1], _parsCa);

      _engine->CommitPostprocess(_hndls[0], 0);
      _engine->CommitPostprocess(_hndls[1], 0);

      _engine->EnablePostprocess(_hndls[0], true);
      _engine->EnablePostprocess(_hndls[1], true);

      _timeStamp = Glob.time + delay;
      _lastUpdate = Glob.time + 2*delay;

      _active = true;
    }
  }
  else
  {
    if (_active)
    {
      _engine->EnablePostprocess(_hndls[0], false);
      _engine->EnablePostprocess(_hndls[1], false);
      _active = false;
    }
  }
}

void EngineDD11::TiredSpecEffect::Update() 
{
  // when active and not updated then stop PPE
  if (_active && Glob.time > _lastUpdate && _hndls.Size() == 2)
  {
    _engine->EnablePostprocess(_hndls[0], false);
    _engine->EnablePostprocess(_hndls[1], false);
    _active = false;
  }
}

void EngineDD11::MovementSpecEffect::SetParams(const float *pars, int nPars)
{
  if (!_handlesValid || _hndls.Size() != 1) return;

  // update 1 times per 100ms
  if (_timeStamp < Glob.time)
  {
    static const float speedLB = 3.5f;
    static const float delay = 0.1;
    bool valid = (pars[0] > speedLB);

    if (valid)
    {
      //_pars[0] = InterpolativC(pars[0], speedLB, 10, 0, 0.005);
#if _ARMA3_RADIAL_BLUR_MOD
      // modified radial blur - values need to be changed 
      _pars[0] = InterpolativC(pars[0], speedLB, 10, 0, 0.003);
#else
      _pars[0] = InterpolativC(pars[0], speedLB, 10, 0, 0.01);
#endif
      _pars[1] = _pars[0];

      _engine->SetPostprocessParams(_hndls[0], _pars);
      _engine->CommitPostprocess(_hndls[0], 0);
      _engine->EnablePostprocess(_hndls[0], true);

      // next update
      _timeStamp = Glob.time + delay;
      _active = true;
    }
    else
    {
      if (_active)
      {
        _pars[0] = 0;
        _pars[1] = 0;

        static const float fadeOut = 0.33f;

        // fade out
        _engine->SetPostprocessParams(_hndls[0], _pars);
        _engine->CommitPostprocess(_hndls[0], fadeOut);
        
        _timeStamp = Glob.time + fadeOut;
        _active = false;
      }
      else
      {
        if (Glob.time < _timeStamp)
        {
          // disable ppe
          _engine->EnablePostprocess(_hndls[0], false);
        }
      }
    }
  }
}

void EngineDD11::MovementSpecEffect::Update() {}

}//namespace DX11
//////////////////////////////////////////////////////////////////////////

// post process enum
#define POSTFX_TYPE_ENUM_DX11(type,prefix,XX) \
  XX(type, prefix, RadialBlur) \
  XX(type, prefix, FilmGrain) \
  XX(type, prefix, SSAO) \
  XX(type, prefix, ChromAberration) \
  XX(type, prefix, WetDistortion) \
  XX(type, prefix, ColorCorrections) \
  XX(type, prefix, DynamicBlur) \
  XX(type, prefix, ColorInversion)

#ifndef DECL_ENUM_POSTFX_TYPE_DX11
#define DECL_ENUM_POSTFX_TYPE_DX11
DECL_ENUM(PostFxTypeDX11)
#endif
DECLARE_DEFINE_ENUM_STATIC(PostFxTypeDX11,PFX,POSTFX_TYPE_ENUM_DX11)

#define CHECK_INTERNAL(command) \
  {LSError err = command; if (err != LSOK) return err;}
  
template <>
LSError Serialize(ParamArchive &ar, const RStringB &name, DX11::FourFloats &value, int minVersion)
{
  if (ar.GetArVersion() < minVersion) return LSOK;
  if (ar.IsSaving())
  {
    AUTO_STATIC_ARRAY(float, array, 4);
    array.Resize(4);
    array[0] = value[0];
    array[1] = value[1];
    array[2] = value[2];
    array[3] = value[3];
    CHECK_INTERNAL(ar.SerializeArray(name, array, minVersion))
  }
  else
  {
    if (ar.GetPass() != ParamArchive::PassFirst) return LSOK;
    AUTO_STATIC_ARRAY(float, array, 4);
    CHECK_INTERNAL(ar.SerializeArray(name, array, minVersion))
    if (array.Size() < 4)
      return LSNoEntry; // default value
    Assert(array.Size() == 4);
    value[0] = array[0];
    value[1] = array[1];
    value[2] = array[2];
    value[3] = array[3];
  }
  return LSOK;
}

namespace DX11
{


/*!
  Enable/disable post process effect. Type of effect is given by the 1.st parameter.
*/
void EnableSinglePPEffect(PostFxTypeDX11 effectType, bool enable)
{
  switch (effectType)
  {
  case PFXRadialBlur:
    GEngine->EnableRadialBlur(enable);
    break;

  case PFXFilmGrain:
    GEngine->EnableFilmGrain(enable);
    break;

  case PFXSSAO:
    GEngine->EnableSSAO(enable);
    break;

  case PFXChromAberration:
    GEngine->EnableChromAberration(enable);
    break;

  case PFXWetDistortion:
    GEngine->EnableWetDistortion(enable);
    break;

  case PFXColorCorrections:
    GEngine->EnableColorCorrections(enable);
    break;

  case PFXDynamicBlur:
    GEngine->EnableDynamicBlur(enable);
    break;

  case PFXColorInversion:
    GEngine->EnableClrInvers(enable);
    break;
  }
}

/* Create post effect and return handle to effect, if creation failed return -1 */
int CreatePostprocess(PostFxTypeDX11 type, int priority)
{
  int hndl = InvalidPPHndl._hndl;

  switch (type)
  {
  case PFXRadialBlur:
    hndl = GEngine->CreatePostprocess(PERadialBlur, priority);    
    break;

  case PFXFilmGrain:
    hndl = GEngine->CreatePostprocess(PEFilmGrain, priority);
    break;

  case PFXSSAO:
    hndl = GEngine->CreatePostprocess(PESSAO, priority);
    break;

  case PFXChromAberration:
    hndl = GEngine->CreatePostprocess(PEChromAberration, priority);
    break;

  case PFXWetDistortion:
    hndl = GEngine->CreatePostprocess(PEWetDistortion, priority);
    break;

  case PFXColorCorrections:
    hndl = GEngine->CreatePostprocess(PEColorCorrections, priority);
    break;

  case PFXDynamicBlur:
    hndl = GEngine->CreatePostprocess(PEDynamicBlur, priority);
    break;

  case PFXColorInversion:
    hndl = GEngine->CreatePostprocess(PEColorInv, priority);
    break;
  }

  return hndl;
}


int ConvertGameScalarToInt(GameValuePar par)
{
  if (par.GetType() != GameScalar)
  {
    RptF("Invalid post effect parameter type.");
    return InvalidPPHndl._hndl;
  }

  float fHndl = par;
  return toInt(fHndl);
}

int CreatePPEffectHndl(const GameState *state, GameValuePar oper1)
{
  int hndl = InvalidPPHndl._hndl;

  if (oper1.GetType() == GameArray)
  {
    // array of effect
    const GameArrayType &array = oper1;

    if (array.Size() != 2) 
    {
      state->SetError(EvalDim, array.Size(), 2);
      return hndl; 
    }

    // effect type
    RString effectName = array[0];
    PostFxTypeDX11 effectType = GetEnumValue<PostFxTypeDX11>(cc_cast(effectName));

    if (effectType == INT_MIN) return hndl;

    int priority = ConvertGameScalarToInt(array[1]);

    return CreatePostprocess(effectType, priority);
  }

  return hndl;
}

/* Create array of post effects */
GameValue PPEffectCreateArr(const GameState *state, GameValuePar oper1)
{
  // [hndl1, ..., hndlN] ppEffectCreate [["effect type1", priority1], .. ["effect typeN", priorityN]]
  const GameArrayType &srcArr = oper1;

  GameArrayType destArr;
  destArr.Realloc(srcArr.Size());
  destArr.Resize(srcArr.Size());

  int i = 0;
  for (; i < srcArr.Size(); i++)
  {
    if (srcArr[i].GetType() == GameArray)
    {
      int hndl = CreatePPEffectHndl(state, srcArr[i]);

      // check handle
      if (hndl == InvalidPPHndl._hndl)
      {
        RptF("Post process effect creation failed");
        break;
      }
      destArr[i] = (float)hndl;
    }
  }

  // if not all effects successfully created
  if (i < srcArr.Size())
  {
    // destroy successfully created post processes
    for (int j = 0; j < i; j++)
    {
      int hndl = ConvertGameScalarToInt(destArr[j]);
      GEngine->DestroyPostprocess(hndl);
    }

    destArr.Clear();

    return NOTHING;
  }

  return destArr;
}

/* Create post effect */
GameValue PPEffectCreate(const GameState *state, GameValuePar oper1)
{
  const GameArrayType &srcArr = oper1;

  // ["effect type", priority]
  if (srcArr.Size() == 2 && srcArr[0].GetType() == GameString)
  {
    int hndl = CreatePPEffectHndl(state, oper1);

    if (hndl == InvalidPPHndl._hndl) 
      RptF("Post process effect creation failed");

    return (float)hndl;
  }
  else
  {
    // [["effect type1", priority1], ..., ["effect typeN", priorityN]]
    return PPEffectCreateArr(state, oper1);
  }
}

/* Destroy effect given by hndl */
GameValue PPEffectDestroy(const GameState *state, GameValuePar oper1)
{
  int hndl = ConvertGameScalarToInt(oper1);

  if (hndl == InvalidPPHndl._hndl) 
  {
    RptF("Invalid post effect handle.");
    return NOTHING;
  }

  GEngine->DestroyPostprocess(hndl);

  return NOTHING;
}

/* Destroy array of given hndls */
GameValue PPEffectDestroyArr(const GameState *state, GameValuePar oper1)
{
  GameArrayType srcArr = oper1;

  for (int i = 0; i < srcArr.Size(); i++)
  {
    PPEffectDestroy(state, srcArr[i]);
  }

  return NOTHING;
}

/* Enable / disable post process effect */
GameValue PPEffectEnable1(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{  
  int hndl = ConvertGameScalarToInt(oper1);

  if (hndl == InvalidPPHndl._hndl) 
  {
    RptF("Invalid post effect handle.");
    return NOTHING;
  }

  GEngine->EnablePostprocess(hndl, oper2);

  return NOTHING;
}

int GetPPParsLenght(const GameArrayType &srcArr)
{
  int index = 0;
  for (int i = 0; i < srcArr.Size(); i++)
  {
    if (srcArr[i].GetType() == GameBool || srcArr[i].GetType() == GameScalar) 
    { 
      index++; 
    }
    else if (srcArr[i].GetType() == GameArray)
    {
      const GameArrayType &subArray = srcArr[i];
      for (int j = 0; j < subArray.Size(); j++)
      {        
        // array can contain only bool & float
        if (subArray[j].GetType() == GameBool || subArray[j].GetType() == GameScalar) 
        { 
          index++; 
          continue;
        }

        return 0;
      }
    }
    else
      return 0;
  }

  return index;
}

/* Adjust post effect params */
GameValue PPEffectAdjust1(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  int hndl = ConvertGameScalarToInt(oper1);

  if (hndl == InvalidPPHndl._hndl) 
  {
    RptF("Invalid post effect handle.");
    return NOTHING;
  }

  const GameArrayType &pars = oper2;
  
  const int maxSize = 32;
  int length = GetPPParsLenght(pars);
  
  if (length == 0 || length > maxSize)
  {
    RptF("Too many post effect params set");
    state->SetError(EvalDim, pars.Size(), maxSize);
    return NOTHING;
  }

  AutoArray<float, MemAllocLocal<float, maxSize> > locPars;
  locPars.Resize(32);
  
  int index = 0;
  for (int i = 0; i < pars.Size(); i++)
  {
    if (pars[i].GetType() == GameScalar)
    {
      locPars[index++] = pars[i];
    }
    else if (pars[i].GetType() == GameBool)
    {
      bool bValue = pars[i];
      locPars[index++] = bValue ? 1.0f : -1.0f;
    }
    else if (pars[i].GetType() == GameArray)
    {
      const GameArrayType &subArray = pars[i];

      for (int j = 0; j < subArray.Size(); j++)
      {
        if (subArray[j].GetType() == GameScalar)
          locPars[index++] = subArray[j];
        else
        {
          state->TypeError(GameScalar, subArray[i].GetType());
          return NOTHING;
        }
      }
    }
    else
    {
      state->TypeError(GameScalar, pars[i].GetType());
      return NOTHING;
    }
  }

  // if return != 0 then number of params is incorrect
  int parsCount = GEngine->SetPostprocessParams(hndl, locPars.Data(), index);
  if (parsCount != 0)
  {
    RptF("Incorrect number of post effect params");
    state->SetError(EvalDim, pars.Size(), parsCount);
  }

  return NOTHING;
}

/* Set time when old pars are replaced by last set, if 0 set immediately */
GameValue PPEffectCommit1(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  int hndl = ConvertGameScalarToInt(oper1);

  if (hndl == InvalidPPHndl._hndl) 
  {
    RptF("Invalid post effect handle.");
    return NOTHING;
  }

  float time = oper2;
  float comTime = max(0.0f, time);

  GEngine->CommitPostprocess(hndl, comTime);
 
  return NOTHING;
}

/* Overloaded version of PPEffectCommit1 */
GameValue PPEffectCommitArr(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  // [hndl1, ..., hndlN] ppEffectCommit time
  GameArrayType srcArr = oper1;

  for (int i = 0; i < srcArr.Size(); i++)
  {
    PPEffectCommit1(state, srcArr[i], oper2);
  }

  return NOTHING;
}

// check if the specified post process effect is committed
GameValue PPEffectCommitted1(const GameState *state, GameValuePar oper1)
{
  int hndl = ConvertGameScalarToInt(oper1);

  // hndl ppEffectCommit time
  if (hndl == InvalidPPHndl._hndl) 
  {
    RptF("Invalid post effect handle.");
    return true;
  }

  return GEngine->CommittedPostprocess(hndl);
};

/*!
  Function enable/disable specified post process effect. Function may be called for different oper1 parameters,
  valid parameters are GameString and GameArray.
*/
GameValue PPEffectEnable(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  // operator type ... valid types are GameString and GameArray
  const GameType &operType = oper1.GetType();

  // enable/disable
  bool enable = oper2;

  // single post process
  if (operType == GameString)
  {
    // effect type
    RString effectName = oper1;
    PostFxTypeDX11 effectType = GetEnumValue<PostFxTypeDX11>(cc_cast(effectName));

    // is enum valid
    if (effectType == INT_MIN) return NOTHING;

    EnableSinglePPEffect(effectType, enable);

    return NOTHING;
  }

  // enable multiple post processes
  if (operType == GameArray)
  {
    // array of effect
    const GameArrayType &array = oper1;

    for (int i = 0; i < array.Size(); i++)
    {
      // effect specified by string
      if (array[i].GetType() == GameString)
      {
        // effect type
        RString effectName = array[i];
        PostFxTypeDX11 effectType = GetEnumValue<PostFxTypeDX11>(cc_cast(effectName));

        // unrecognized effect is skip
        if (effectType == INT_MIN) continue;

        EnableSinglePPEffect(effectType, enable);
      }

      // effect specified by handle
      if (array[i].GetType() == GameScalar)
      {
        PPEffectEnable1(state, array[i], oper2);
      }
    }
  }

  return NOTHING;
}

/*!
  Function set parameters for specified post process effect. Effect is specified by first argument. 
  Second argument represent effect parameters. Parameters number depends on effect type.
*/
GameValue PPEffectAdjust( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  // effect type
  RString effectName = oper1;
  PostFxTypeDX11 effectType = GetEnumValue<PostFxTypeDX11>(cc_cast(effectName));
  // is enum valid
  if (effectType == INT_MIN) return NOTHING;

  // array of parameters
  const GameArrayType &array = oper2;

  // set params to specified post process, each post process has different parameters count
  switch (effectType)
  {
  case PFXRadialBlur:
    // params count test
    if (array.Size() != 4)
    {
      state->SetError(EvalDim, array.Size(), 4);
      return NOTHING;
    }

    // param types test
    for (int i = 0; i < 4; i++)
    {
      if (array[i].GetType() != GameScalar)
      {
        state->TypeError(GameScalar, array[i].GetType());
        return NOTHING; 
      }
    }

    // PowerX   - float array[0]
    // PowerY   - float array[1]
    // OffsetX  - float array[2]
    // OffsetY  - float array[3]
    GEngine->SetRadialBlurrParams(array[0], array[1], array[2], array[3]);
    break;

  case PFXWetDistortion:
    // params count test
    if (array.Size() != 15)
    {
      state->SetError(EvalDim, array.Size(), 4);
      return NOTHING;
    }

    // params type test
    for (int i = 0; i < 15; i++)
    {
      if (array[i].GetType() != GameScalar)
      {
        state->TypeError(GameScalar, array[i].GetType());
        return NOTHING; 
      }
    }

    // SetBlurriness  - float value
    // SetEffectPower - float top, float bottom
    // SetWaveSpeeds  - float horizontal1, float horizontal2, float vertical1, float vertical2
    // SetWaveAmplitudes  - float horizontal1, float horizontal2, float vertical1, float vertical2
    // SetPhaseCoefs  - float randX, float randY, float posX, float posY
    GEngine->SetWetDistortionParams(array[0], array[1], array[2], array[3], array[4], array[5], array[6],
      array[7], array[8], array[9], array[10], array[11], array[12], array[13], array[14]);

    break;

  case PFXFilmGrain:
    {
      const int FILMGRAIN_PARAMS = 6;

      // takes max five params
      if (array.Size() > FILMGRAIN_PARAMS || array.Size() < 1)
      {
        state->SetError(EvalDim, array.Size(), FILMGRAIN_PARAMS);
        return NOTHING;
      } 

      float params[FILMGRAIN_PARAMS];

      // params type test
      for (int i = 0; i < array.Size() && i < FILMGRAIN_PARAMS - 1; i++)
      {
        if ((array[i].GetType() != GameScalar))
        {
          state->TypeError(GameScalar, array[i].GetType());
          return NOTHING; 
        }

        params[i] = array[i];
      }

      if (array.Size() == FILMGRAIN_PARAMS)
      {
        if (array[FILMGRAIN_PARAMS - 1].GetType() != GameBool)
        {
          state->TypeError(GameBool, array[FILMGRAIN_PARAMS - 1].GetType());
          return NOTHING; 
        }

        params[FILMGRAIN_PARAMS - 1] = array[FILMGRAIN_PARAMS - 1] ? 0.0f : 1.0f;
      }

      // intensity        - float array[0]
      // sharpness        - float array[1]
      // grainsize        - float array[2]
      // intensity at X0  - float array[3]
      // intensity at X1  - float array[4]
      // monochrom        - bool array[5]
      GEngine->SetFilmGrainParams(params, array.Size());
    }
    break;

  case PFXSSAO:
    {
      const int SSAO_PARAMS = 11;

      if (array.Size() > SSAO_PARAMS || array.Size() < 1)
      {
        state->SetError(EvalDim, array.Size(), SSAO_PARAMS);
        return NOTHING;
      } 

      float params[SSAO_PARAMS];

      // params type test
      for (int i = 0; i < array.Size() && i < SSAO_PARAMS- 2; i++)
      {
        if ((array[i].GetType() != GameScalar))
        {
          state->TypeError(GameScalar, array[i].GetType());
          return NOTHING; 
        }

        params[i] = array[i];
      }

      //bool
      if (array.Size() >= 9)
      {
        for (int i = 9; i < array.Size(); i++)
        {
          if (array[i].GetType() != GameBool)
          {
           state->TypeError(GameBool, array[i].GetType());
           return NOTHING; 
          }

          params[i] = array[i] ? 0.0f : 1.0f;
        }
      }

      GEngine->SetSSAOParams(params, array.Size());
    }

    break;

  case PFXChromAberration:
    // takes three params
    if (array.Size() != 3)
    {
      state->SetError(EvalDim, array.Size(), 3);
      return NOTHING;
    } 

    // params type test
    if ((array[0].GetType() != GameScalar) || (array[1].GetType() != GameScalar))
    {
      state->TypeError(GameScalar, array[0].GetType());
      return NOTHING; 
    }

    if (array[2].GetType() != GameBool)
    {
      state->TypeError(GameScalar, array[2].GetType());
      return NOTHING; 
    }

    // aberationPowerX - float array[0]
    // aberationPowerY - float array[1]
    // aspectCorrection - bool array[2]
    GEngine->SetChromaticAberrationParams(array[0], array[1], array[2]);
    break;

  case PFXColorInversion:
    {
      const int reqSize = 3;

      if (array.Size() != reqSize)
      {
        state->SetError(EvalDim, array.Size(), reqSize);
        return NOTHING;
      }

      for (int i = 0; i < reqSize; i++)
      {
        // params type test
        if ((array[i].GetType() != GameScalar))
        {
          state->TypeError(GameScalar, array[i].GetType());
          return NOTHING; 
        }
      }

      Color invColor(array[0], array[1], array[2], 0);
      GEngine->SetClrInversParams(invColor);
    }
    break;

  case PFXDynamicBlur:
    // pars count test
    if (array.Size() != 1)
    {
      state->SetError(EvalDim, array.Size(), 1);
      return NOTHING;
    } 

    // type test 
    if (array[0].GetType() != GameScalar)
    {
      state->TypeError(GameScalar, array[0].GetType());
      return NOTHING; 
    }

    // blurrines - float array[0]
    GEngine->SetDynamicBlurParameters(array[0]);
    break;

  case PFXColorCorrections:
    // three floats and three colors
    if (array.Size() < 6)
    {
      state->SetError(EvalDim, array.Size(), 6);
      return NOTHING;
    }

    // first three param types test
    for (int i = 0; i < 3; i++)
    {
      if (array[i].GetType() != GameScalar)
      {
        state->TypeError(GameScalar, array[i].GetType());
        return NOTHING; 
      }
    }

    // test color parameters, color is set as array of four floats
    for (int i = 3; i < 6; i++)
    {
      if (array[i].GetType() != GameArray)
      {
        state->TypeError(GameArray, array[i].GetType());
        return NOTHING;         
      }

      // array of color components
      const GameArrayType &clrArray = array[i];
      
      // color consist from 4 floats
      if (clrArray.Size() != 4)
      {
        state->SetError(EvalDim, clrArray.Size(), 4);
        return NOTHING;
      }

      for (int j = 0; j < 4; j++)
      {
        if (clrArray[j].GetType() != GameScalar)
        {
          state->TypeError(GameScalar, clrArray[j].GetType());
          return NOTHING; 
        }
      }
    }

    const GameArrayType &tmp = array[3];
    Color blendColor(tmp[0], tmp[1], tmp[2], tmp[3]);

    const GameArrayType &tmp1 = array[4];
    Color colorizedColor(tmp1[0], tmp1[1], tmp1[2], tmp1[3]);

    const GameArrayType &tmp2 = array[5];
    Color weightsColor(tmp2[0], tmp2[1], tmp2[2], tmp2[3]);

    // SetBrightness  - float value
    // SetContrast    - float value
    // SetOffset      - float value
    // SetBlendColor  - Color color ... array of 3 x float
    // SetColorizeColor - Color color ... array of 3 x float
    // SetRGBWeights  - Color weights ... array of 3 x float
    GEngine->SetColorCorrectionsParameters(array[0], array[1], array[2], blendColor, colorizedColor, weightsColor);

#if _ARMA3_RADIAL_COLOR_PP
    if (array.Size() == 7)
    {
      const int index = 6;
      if (array[index].GetType() != GameArray)
      {
        state->TypeError(GameArray, array[index].GetType());
        return NOTHING;         
      }

      // array of color components
      const GameArrayType &radArray = array[index];

      if (radArray.Size() != ColorPar::NumRadialPars)
      {
        state->SetError(EvalDim, radArray.Size(), ColorPar::NumRadialPars);
        return NOTHING;
      }
      for (int j = 0; j < ColorPar::NumRadialPars; j++)
      {
        if (radArray[j].GetType() != GameScalar)
        {
          state->TypeError(GameScalar, radArray[j].GetType());
          return NOTHING; 
        }
      }

      GEngine->SetColorCorrectionsParametersRadial(radArray[0], radArray[1], radArray[2], radArray[3], radArray[4], radArray[5], radArray[6]);

    }
#endif

    break;
  }

  return NOTHING;
}

/*
  Commit for specified post process effect.
*/
GameValue PPEffectCommit( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  // effect type
  RString effectName = oper1;
  PostFxTypeDX11 effectType = GetEnumValue<PostFxTypeDX11>(cc_cast(effectName));

  // is enum valid
  if (effectType == INT_MIN) return NOTHING;

  // positive values
  float time = oper2;
  float comTime = max(0.0f, time);

  switch(effectType)
  {
  case PFXRadialBlur:
      GEngine->CommitRadBlur(comTime);
    break;

  case PFXFilmGrain:
    GEngine->CommitFilmGrain(comTime);
    break;

  case PFXSSAO:
    GEngine->CommitSSAO(comTime);
    break;

  case PFXChromAberration:
    GEngine->CommitChromAberration(comTime);
    break;

  case PFXWetDistortion:
    GEngine->CommitWetDistortion(comTime);
    break;

  case PFXColorCorrections:
    GEngine->CommitColorCorrections(comTime);
    break;

  case PFXDynamicBlur:
    GEngine->CommitDynamicBlur(comTime);
    break;

  case PFXColorInversion:
    GEngine->CommitClrInvers(comTime);
    break;
  }

  return NOTHING;
}

// check if the specified post process effect is committed
GameValue PPEffectCommitted(const GameState *state, GameValuePar oper1)
{
  // effect type
  RString effectName = oper1;
  PostFxTypeDX11 effectType = GetEnumValue<PostFxTypeDX11>(cc_cast(effectName));

  switch (effectType)
  {
  case PFXRadialBlur:
    return GEngine->CommittedRadBlur();
    break;
  case PFXFilmGrain:
    return GEngine->CommittedFilmGrain();
    break;
  case PFXSSAO:
    return GEngine->CommittedSSAO();
    break;
  case PFXChromAberration:
    return GEngine->CommittedChromAberration();
    break;
  case PFXWetDistortion:
    return GEngine->CommittedWetDistortion();
    break;
  case PFXColorCorrections:
    return GEngine->CommittedColorCorrections();
    break;
  case PFXDynamicBlur:
    return GEngine->CommittedDynamicBlur();
    break;
  case PFXColorInversion:
    return GEngine->CommittedClrInvers();
    break;
  }

  return true;
}

#else
#include <El/Evaluator/express.hpp>
#include <El/Modules/modules.hpp>

#define EMPTY_PP_IMPLEMENTATION {return NOTHING;}

GameValue PPEffectCommitted(const GameState *state, GameValuePar oper1) EMPTY_PP_IMPLEMENTATION
GameValue PPEffectCommitted1(const GameState *state, GameValuePar oper1) EMPTY_PP_IMPLEMENTATION
GameValue PPEffectCreate(const GameState *state, GameValuePar oper1) EMPTY_PP_IMPLEMENTATION
GameValue PPEffectDestroy(const GameState *state, GameValuePar oper1) EMPTY_PP_IMPLEMENTATION
GameValue PPEffectDestroyArr(const GameState *state, GameValuePar oper1) EMPTY_PP_IMPLEMENTATION
GameValue PPEffectEnable(const GameState *state, GameValuePar oper1, GameValuePar oper2) EMPTY_PP_IMPLEMENTATION
GameValue PPEffectEnable1(const GameState *state, GameValuePar oper1, GameValuePar oper2) EMPTY_PP_IMPLEMENTATION
GameValue PPEffectAdjust( const GameState *state, GameValuePar oper1, GameValuePar oper2 ) EMPTY_PP_IMPLEMENTATION
GameValue PPEffectAdjust1(const GameState *state, GameValuePar oper1, GameValuePar oper2) EMPTY_PP_IMPLEMENTATION
GameValue PPEffectCommit( const GameState *state, GameValuePar oper1, GameValuePar oper2 ) EMPTY_PP_IMPLEMENTATION
GameValue PPEffectCommit1(const GameState *state, GameValuePar oper1, GameValuePar oper2) EMPTY_PP_IMPLEMENTATION
GameValue PPEffectCommitArr(const GameState *state, GameValuePar oper1, GameValuePar oper2) EMPTY_PP_IMPLEMENTATION

#endif

const GameType GameScalarOrArray(GameScalar | GameArray);
extern GameTypeType GTypeGameScalarOrArray;

template<>
class ConvertTypes<&GTypeGameScalarOrArray> : public ConvertTypes<&GTypeGameAny> {};

#define FUNCTIONS_VISUAL(XX, Category) \
  XX(GameBool, "ppEffectCommitted", PPEffectCommitted, GameString, "effect", "Check whether given post process effect is commited", "", "", "5501", "", Category) \
  XX(GameBool, "ppEffectCommitted", PPEffectCommitted1, GameScalar, "effect", "Check whether given post process effect is commited", "", "", "5501", "", Category) \
  XX(GameScalarOrArray, "ppEffectCreate", PPEffectCreate, GameArray, "effect", "Create post process effect specified by name and priority", "", "", "5501", "", Category) \
  XX(GameNothing, "ppEffectDestroy", PPEffectDestroy, GameScalar, "effect", "Destroy post process effect given by handle", "", "", "5501", "", Category) \
  XX(GameNothing, "ppEffectDestroy", PPEffectDestroyArr, GameArray, "effect", "Create post process effect specified by name and priority", "", "", "5501", "", Category)


FUNCTIONS_VISUAL(JNI_FUNCTION, "Visual")

static const GameFunction PPUnary[]=
{
  FUNCTIONS_VISUAL(REGISTER_FUNCTION, "Visual")
};

#define OPERATORS_VISUAL(XX, Category) \
  XX(GameNothing, "ppEffectEnable", function, PPEffectEnable, GameString, GameBool, "string", "bool", "Enable/disable specified post process effect, is overloaded form multiple effects.", "\"radialBlurr\" ppEffectEnable true, [\"chromAberration\", \"radialBlurr\"] ppEffectEnable true", "", "2.92", "", Category) \
  XX(GameNothing, "ppEffectEnable", function, PPEffectEnable, GameArray, GameBool, "array", "bool", "Enable/disable specified post process effect, is overloaded form multiple effects.", "\"radialBlurr\" ppEffectEnable true, [\"chromAberration\", \"radialBlurr\"] ppEffectEnable true", "", "2.92", "", Category) \
  XX(GameNothing, "ppEffectEnable", function, PPEffectEnable1, GameScalar, GameBool, "number", "bool", "Enable / disable post process effect", "hndl ppEffectEnable true", "", "2.92", "", Category) \
  XX(GameNothing, "ppEffectAdjust", function, PPEffectAdjust, GameString, GameArray, "string", "array", "Adjust parameters for specified post process effect", "\"radialBlurr\" ppEffectEnable []", "", "2.92", "", Category) \
  XX(GameNothing, "ppEffectAdjust", function, PPEffectAdjust1, GameScalar, GameArray, "number", "array", "Set post process effect parameters", "hndl ppEffectAdjust [0.0075, 0.0075, 0.1, 0.1]", "", "2.92", "", Category) \
  XX(GameNothing, "ppEffectCommit", function, PPEffectCommit, GameString, GameScalar, "string", "number", "Set time when old pars are replaced by last set, if 0 set immediately", "...", "", "2.92", "", Category) \
  XX(GameNothing, "ppEffectCommit", function, PPEffectCommit1, GameScalar, GameScalar, "number", "number", "Set time when old pars are replaced by last set, if 0 set immediately", "hndl ppEffectCommit 0", "", "2.92", "", Category) \
  XX(GameNothing, "ppEffectCommit", function, PPEffectCommitArr, GameArray, GameScalar, "array", "scalar", "Set time when old pars are replaced by last set, if 0 set immediately", "hndl ppEffectCommit 0", "", "2.92", "", Category)

OPERATORS_VISUAL(JNI_OPERATOR, "Visual")
 
static const GameOperator PPBinary[]=
{
  OPERATORS_VISUAL(REGISTER_OPERATOR, "Visual")
};

#if DOCUMENT_COMREF
static ComRefFunc PPComRefFunc[] =
{
  FUNCTIONS_VISUAL(COMREF_FUNCTION, "Visual")
  OPERATORS_VISUAL(COMREF_OPERATOR, "Visual")
};
#endif

INIT_MODULE(PostProcess, 2)
{
  GGameState.NewFunctions(PPUnary, lenof(PPUnary));
  GGameState.NewOperators(PPBinary, lenof(PPBinary));
#if DOCUMENT_COMREF
  for (int i=0; i<lenof(PPComRefFunc); i++)
  {
    GGameState.AddComRefFunction(PPComRefFunc[i]);
  }
#endif
};

} //namespace DX11
#endif //#if _ENABLE_DX11

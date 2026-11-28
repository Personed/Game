#include "../wpch.hpp"

#if _ENABLE_DX11
#if !_DISABLE_GUI

#include "engdd11.hpp"
#include <DxErr.h>
#include "el/Debugging/debugTrap.hpp"
#include "el/QStream/qbStream.hpp"
#include "el/Evaluator/expressImpl.hpp"
#include "el/ParamFile/paramFile.hpp"
#include "el/Statistics/statistics.hpp"
#include "../keyInput.hpp"
#include "../dikCodes.h"
#include "El/Multicore/jobs.hpp"
#include "../tlVertex.hpp"
#include "../Shape/shape.hpp"
#include "Es/Common/delegate.hpp"
#include "Es/ReportStack/reportStack.hpp"
#include "../rtAnimation.hpp"
#include "../scene.hpp"
#include "../camera.hpp"
#include "../txtPreload.hpp"
#include "../clip2d.hpp"
#include "../landImpl.hpp"
#include "postProcessDX11.hpp"
#include "d3d11utils.hpp"
#include "../world.hpp" 

#include "../textbank.hpp"

RString GetConfigSection();

#if _ENABLE_COMPILED_SHADER_CACHE
#include "ShaderCompileCacheDX11.h"
#endif

#if _ARMA3_SIMUL_WEATHER
#include "../SimulWeather/SimulWeatherDX11.hpp"
#endif

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3d10.lib")
#pragma comment(lib,"DXGI.lib")

#if _DEBUG
#pragma comment(lib,"d3dx11d.lib")
#pragma comment(lib,"d3dx10d.lib")
#else
#pragma comment(lib,"d3dx11.lib")
#pragma comment(lib,"d3dx10.lib")
#endif

#define V_OFFSET_CUSTOM 1
#define SHADOW_LEVELS 1

/// when PS blend is active, pixel shader is assumed to perform source alpha multiplitation
#define PS_BLEND 1
#define _RAIN3D

#if _RELEASE
#define DO_COUNTERS 0
#else
#define DO_COUNTERS 0
#endif

#if PS_BLEND
/**
When only (1-alpha)*dst blending is used for alpha blending, the shader can control add/blend modes
*/
# define D3DBLEND_SRCDEF D3DBLEND_ONE_OLD
#else
# define D3DBLEND_SRCDEF D3DBLEND_SRCALPHA
#endif

#define LOG_LOCKS 0

#ifdef _XBOX
#define _XMEMCPY 1
#endif

#define MaxVertexCount(type) (MaxStaticVertexBufferSizeB/sizeof(type))

#if 0
#define ValidateCoord(c)
#else
#define LOG_INVALID 0
inline void ValidateCoord( float &c, float max )
{
#if LOG_INVALID
  static int total=0;
  static int valid=0;
  total++;
#endif
#if 0
  const float minC=-2047,maxC=+2048;
  // coordinate must be within range -2047...2048
  if( c<minC ) c=minC;
  else if( c>maxC ) c=maxC;
#else
  if( c<0 ) c=0;
  else if( c>max ) c=max;
#endif

#if LOG_INVALID
  else valid++;
  if( total>=100000 )
  {
    if( valid<total ) LogF("Invalid %d of %d",total-valid,total);
    valid=0;
    total=0;
  }
#endif
}
#endif

#if _DEBUG
#define VALIDATE_HNDL_ENABLE 1
#else
#define VALIDATE_HNDL_ENABLE 0
#endif

//#define COMMAND_BUFFER_CALCULATION
#ifdef COMMAND_BUFFER_CALCULATION
#ifdef _XBOX
static DWORD tilUsed = 0;
static DWORD tilRemaining = 0;
#endif
#endif

//! Enumerate program definitions for vertex shader compilation
#define PRGDEF_ENUM(type,prefix,XX) \
  XX(type, prefix, PRGDEFName) \
  XX(type, prefix, PRGDEFUVMapTex1) \
  XX(type, prefix, PRGDEFMainLight) \
  XX(type, prefix, PRGDEFRenderingMode) \
  XX(type, prefix, PRGDEFSkinning) \
  XX(type, prefix, PRGDEFInstancing) \
  XX(type, prefix, PRGDEFUsesTangents) \
  XX(type, prefix, PRGDEFUses2TexCoords) \

//! Create enum and array of the same names
#ifndef DECL_ENUM_PRGDEF11
#define DECL_ENUM_PRGDEF11
DECL_ENUM(PRGDef11)
#endif
DECLARE_DEFINE_ENUM_STATIC(PRGDef11, , PRGDEF_ENUM)

HWND AppInit(
             HINSTANCE hInst,HINSTANCE hPrev,int sw, bool fullscreen,
             int x, int y, int width, int height
             );
void AppStateChangeBeg(HWND hwndApp, bool fullscreen, bool enableSize, int x, int y, int w, int h);
void AppStateChangeEnd(HWND hwndApp, bool fullscreen, bool enableSize, int x, int y, int w, int h);

BOOL AppDone(HWND hwndApp, HINSTANCE hInstance);

bool ParseUserParams(ParamFile &cfg, GameDataNamespace *globals);
void SaveUserParams(ParamFile &cfg);

void ParseFlashpointCfg(ParamFile &file);
void SaveFlashpointCfg(ParamFile &file);

bool CheckExThreads(int mask, bool autodetected);

bool IsAppPaused();

extern int D3D11Adapter;
extern bool WinXP;
extern bool EnableSM3;

#ifndef _XBOX

#if WINVER<0x500

typedef struct tagMONITORINFO {  
  DWORD  cbSize; 
  RECT   rcMonitor; 
  RECT   rcWork; 
  DWORD  dwFlags; 
} MONITORINFO, *LPMONITORINFO; 

#define SM_XVIRTUALSCREEN       76
#define SM_YVIRTUALSCREEN       77
#define SM_CXVIRTUALSCREEN      78
#define SM_CYVIRTUALSCREEN      79


#endif

typedef BOOL WINAPI GetMonitorInfoT(HMONITOR mon, LPMONITORINFO mi);

#endif


namespace DX11
{

static void MultiplyMatrix4(PASSMATRIXTYPE &result, const PASSMATRIXTYPE &left, const PASSMATRIXTYPE &right)
{
  float product[4][4];
  for (int row = 0; row < 4; ++row)
  {
    for (int column = 0; column < 4; ++column)
    {
      product[row][column] =
        left.m[row][0] * right.m[0][column] +
        left.m[row][1] * right.m[1][column] +
        left.m[row][2] * right.m[2][column] +
        left.m[row][3] * right.m[3][column];
    }
  }
  for (int row = 0; row < 4; ++row)
    for (int column = 0; column < 4; ++column)
      result.m[row][column] = product[row][column];
}

// Renderer tuning flags from the original DX11 branch. The DayZ engine does
// not expose these switches, so keep the original default behavior locally.
static bool D3DUseDynamicConstantBuffers = true;
static bool D3DUseMultiSizeConstantBuffers = true;
static bool D3DCreateSingleThreaded = false;
static bool D3DCreateNoThreadOpt = false;
#if PERFORM_DX11_LOCKING
CriticalSection DevCriticalSection;
#endif

#if PIX_NAMED_CB_SCOPES
# define LOG_RESULTS_ALLOC 0

  int EngineDD11::RenderCBToPlay::_currDebugId = 0;
#endif

  void LogTextureF( const char *t, const TextureD3D11 *x )
  {
    LogF("Texture %s %s",t,x ? x->Name() : "--");
  }

#if 0
#define LogTexture(t,x) LogTextureF(t,x)
#else
#define LogTexture(t,x)
#endif

#if _ENABLE_CHEATS

  static int EngDiagMode;
  static int LimitVSCCount=-1;
  int LimitTriCount=-1;
  int LimitDIPCount=-1;
  static int LimitSecCount=-1;
  bool ForceStaticVB=false;
#else
#define CHECK_ENG_DIAG(x) false
  const int LimitVSCCount=-1;
  const int LimitTriCount=-1;
  const int LimitDIPCount=-1;
  const int LimitSecCount=-1;
#endif

#if _ENABLE_PERFLOG && _ENABLE_CHEATS && (_DEBUG || _PROFILE) //&& !defined _XBOX
# define DO_TEX_STATS 2
#endif

#if _ENABLE_PERFLOG 
  int DPrimIndex;
#endif

#if DO_TEX_STATS
#include "../keyInput.hpp"
#include "../dikCodes.h"
  static StatisticsByName TexStats;
  static bool EnableTexStats = false;
#endif

#if 0 //_ENABLE_REPORT
#define REPORT_ADDSHAPE \
  LogF( \
  "Resource %p: shape %d added (%d of %d): %d", \
  this,_nShapes,item->Size(),_manager.Size(), \
  Glob.frameID \
  );
#else
#define REPORT_ADDSHAPE
#endif

  // track patching in Debug (All) or Profile (PC only, not Xbox)
#if _DEBUG || _PROFILE && !defined _XBOX
#define TRACK_SHADER_PATCH 0
#endif

#if TRACK_SHADER_PATCH
  /// statistics of what shader/stream/VD declarations are used most often
  static StatisticsByName ShaderPatchStats;

  struct PatchId
  {
    IDirect3DVertexShader9 *_vs;
    IDirect3DVertexDeclaration9 *_vd;
    int _stride;
    IDirect3DPixelShader9 *_ps;

    PatchId(IDirect3DVertexShader9 *vs, IDirect3DVertexDeclaration9 *vd, int stride, IDirect3DPixelShader9 *ps)
      :_vs(vs),_vd(vd),_stride(stride),_ps(ps)
    {

    }
  };

  static void TrackShaderPatch(const D3DBoundShaderName &name)
  {
    // TODO: group pixel shaders with identical input signatures
    // create ID out of all data we have
    ShaderPatchStats.Count(&name,sizeof(name));
  }

  static void ReportId(const PatchId &id, int count)
  {
    // we need to ask the engine to interpret the values for us
    RString vsName = GEngineDD11->GetVSName(id._vs);
    RString vdName = GEngineDD11->GetVDName(id._vd);
    RString psName = GEngineDD11->GetPSName(id._ps);
    LogF("BIND_SHADER(%s,%s,%d,%s,   %d)",cc_cast(vsName),cc_cast(vdName),id._stride,cc_cast(psName),count);
  }
  void PatchIdCallback(const void *id, int idSize, int count)
  {
    Assert(idSize==sizeof(PatchId));
    ReportId(*(PatchId *)id,count);
  }

  static void ReportShaderPatches()
  {
    ShaderPatchStats.Report(PatchIdCallback);
  }
  static void ResetShaderPatches()
  {
    ShaderPatchStats.Clear();
  }
#endif

#define ENABLED_ASYNC_TEXTURES_Ondra 1

#if 1 // ENABLED_FEATURE(ASYNC_TEXTURES)


#if PERFORM_DX11_LOCKING
  static bool MipLoadingAsync = true;
#else
  static bool MipLoadingAsync = false;
#endif
#else
  static bool MipLoadingAsync = false;
#endif

#define LOG_LOCKS 0

// TODOX360: check what size is really needed for dynamic buffers
const int MaxShapeVertices = 4*1024;
const int MaxStaticVertexBufferSizeB = 0; /** zero means no coalescing */

#define MaxVertexCount(type) (MaxStaticVertexBufferSizeB/sizeof(type))

void ReportMemoryStatusWithVRAM()
{
  void ReportMemoryStatus();
  ReportMemoryStatus();
  const char *err = GEngineDD11->GetMemErrorStatus();
  RptF("%s",err);
}

enum RescalerMode
{
  RescaleBicubic,
  RescaleBilinear,
  NRescalerModes
};

#if _ENABLE_CHEATS
static RescalerMode FinalRescalerMode = RescaleBicubic;
#else
static const RescalerMode FinalRescalerMode = RescaleBicubic;
#endif

static SSupportedFormats supportedFormats[] = {
  {DXGI_FORMAT_R8G8B8A8_UNORM, 32}
};

static const struct {DXGI_FORMAT format;Engine::HDRPrec prec;} PreferredRTFormat[] = {
  {DXGI_FORMAT_R16G16B16A16_FLOAT,Engine::HDR16},
  {DXGI_FORMAT_R32G32B32A32_FLOAT,Engine::HDR32},
};

const char *SurfaceFormatNameDD11(int format)
{
#define FN(x) case DXGI_FORMAT_##x: return #x

  switch (format)
  {
    FN(UNKNOWN);
    FN(R32G32B32A32_TYPELESS);
    FN(R32G32B32A32_FLOAT);
    FN(R32G32B32A32_UINT);
    FN(R32G32B32A32_SINT);
    FN(R32G32B32_TYPELESS);
    FN(R32G32B32_FLOAT);
    FN(R32G32B32_UINT);
    FN(R32G32B32_SINT);
    FN(R16G16B16A16_TYPELESS);
    FN(R16G16B16A16_FLOAT);
    FN(R16G16B16A16_UNORM);
    FN(R16G16B16A16_UINT);
    FN(R16G16B16A16_SNORM);
    FN(R16G16B16A16_SINT);
    FN(R32G32_TYPELESS);
    FN(R32G32_FLOAT);
    FN(R32G32_UINT);
    FN(R32G32_SINT);
    FN(R32G8X24_TYPELESS);
    FN(D32_FLOAT_S8X24_UINT);
    FN(R32_FLOAT_X8X24_TYPELESS);
    FN(X32_TYPELESS_G8X24_UINT);
    FN(R10G10B10A2_TYPELESS);
    FN(R10G10B10A2_UNORM);
    FN(R10G10B10A2_UINT);
    FN(R11G11B10_FLOAT);
    FN(R8G8B8A8_TYPELESS);
    FN(R8G8B8A8_UNORM);
    FN(R8G8B8A8_UNORM_SRGB);
    FN(R8G8B8A8_UINT);
    FN(R8G8B8A8_SNORM);
    FN(R8G8B8A8_SINT);
    FN(R16G16_TYPELESS);
    FN(R16G16_FLOAT);
    FN(R16G16_UNORM);
    FN(R16G16_UINT);
    FN(R16G16_SNORM);
    FN(R16G16_SINT);
    FN(R32_TYPELESS);
    FN(D32_FLOAT);
    FN(R32_FLOAT);
    FN(R32_UINT);
    FN(R32_SINT);
    FN(R24G8_TYPELESS);
    FN(D24_UNORM_S8_UINT);
    FN(R24_UNORM_X8_TYPELESS);
    FN(X24_TYPELESS_G8_UINT);
    FN(R8G8_TYPELESS);
    FN(R8G8_UNORM);
    FN(R8G8_UINT);
    FN(R8G8_SNORM);
    FN(R8G8_SINT);
    FN(R16_TYPELESS);
    FN(R16_FLOAT);
    FN(D16_UNORM);
    FN(R16_UNORM);
    FN(R16_UINT);
    FN(R16_SNORM);
    FN(R16_SINT);
    FN(R8_TYPELESS);
    FN(R8_UNORM);
    FN(R8_UINT);
    FN(R8_SNORM);
    FN(R8_SINT);
    FN(A8_UNORM);
    FN(R1_UNORM);
    FN(R9G9B9E5_SHAREDEXP);
    FN(R8G8_B8G8_UNORM);
    FN(G8R8_G8B8_UNORM);
    FN(BC1_TYPELESS);
    FN(BC1_UNORM);
    FN(BC1_UNORM_SRGB);
    FN(BC2_TYPELESS);
    FN(BC2_UNORM);
    FN(BC2_UNORM_SRGB);
    FN(BC3_TYPELESS);
    FN(BC3_UNORM);
    FN(BC3_UNORM_SRGB);
    FN(BC4_TYPELESS);
    FN(BC4_UNORM);
    FN(BC4_SNORM);
    FN(BC5_TYPELESS);
    FN(BC5_UNORM);
    FN(BC5_SNORM);
    FN(B5G6R5_UNORM);
    FN(B5G5R5A1_UNORM);
    FN(B8G8R8A8_UNORM);
    FN(B8G8R8X8_UNORM);
    FN(R10G10B10_XR_BIAS_A2_UNORM);
    FN(B8G8R8A8_TYPELESS);
    FN(B8G8R8A8_UNORM_SRGB);
    FN(B8G8R8X8_TYPELESS);
    FN(B8G8R8X8_UNORM_SRGB);
    FN(BC6H_TYPELESS);
    FN(BC6H_UF16);
    FN(BC6H_SF16);
    FN(BC7_TYPELESS);
    FN(BC7_UNORM);
    FN(BC7_UNORM_SRGB);
    FN(FORCE_UINT);
  default:
    LogF("Invalid DXGI format");
  }
  return "OTHER";
}
// format list, bpp hand filled. For some exotic formats bpp may be wrong
static SSupportedFormats KnownFormats[]=
{
  {DXGI_FORMAT_R32G32B32A32_TYPELESS, 128}, 
  {DXGI_FORMAT_R32G32B32A32_FLOAT, 128}, 
  {DXGI_FORMAT_R32G32B32A32_UINT, 128}, 
  {DXGI_FORMAT_R32G32B32A32_SINT, 128}, 
  {DXGI_FORMAT_R32G32B32_TYPELESS, 96}, 
  {DXGI_FORMAT_R32G32B32_FLOAT, 96}, 
  {DXGI_FORMAT_R32G32B32_UINT, 96}, 
  {DXGI_FORMAT_R32G32B32_SINT, 96}, 
  {DXGI_FORMAT_R16G16B16A16_TYPELESS, 64}, 
  {DXGI_FORMAT_R16G16B16A16_FLOAT, 64}, 
  {DXGI_FORMAT_R16G16B16A16_UNORM, 64}, 
  {DXGI_FORMAT_R16G16B16A16_UINT, 64}, 
  {DXGI_FORMAT_R16G16B16A16_SNORM, 64}, 
  {DXGI_FORMAT_R16G16B16A16_SINT, 64}, 
  {DXGI_FORMAT_R32G32_TYPELESS, 64}, 
  {DXGI_FORMAT_R32G32_FLOAT, 64}, 
  {DXGI_FORMAT_R32G32_UINT, 64}, 
  {DXGI_FORMAT_R32G32_SINT, 64}, 
  {DXGI_FORMAT_R32G8X24_TYPELESS, 64}, 
  {DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 64}, 
  {DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, 64}, 
  {DXGI_FORMAT_X32_TYPELESS_G8X24_UINT, 64}, 
  {DXGI_FORMAT_R10G10B10A2_TYPELESS, 32}, 
  {DXGI_FORMAT_R10G10B10A2_UNORM, 32}, 
  {DXGI_FORMAT_R10G10B10A2_UINT, 32}, 
  {DXGI_FORMAT_R11G11B10_FLOAT, 32}, 
  {DXGI_FORMAT_R8G8B8A8_TYPELESS, 32}, 
  {DXGI_FORMAT_R8G8B8A8_UNORM, 32}, 
  {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 32}, 
  {DXGI_FORMAT_R8G8B8A8_UINT, 32}, 
  {DXGI_FORMAT_R8G8B8A8_SNORM, 32}, 
  {DXGI_FORMAT_R8G8B8A8_SINT, 32}, 
  {DXGI_FORMAT_R16G16_TYPELESS, 32}, 
  {DXGI_FORMAT_R16G16_FLOAT, 32}, 
  {DXGI_FORMAT_R16G16_UNORM, 32}, 
  {DXGI_FORMAT_R16G16_UINT, 32}, 
  {DXGI_FORMAT_R16G16_SNORM, 32}, 
  {DXGI_FORMAT_R16G16_SINT, 32}, 
  {DXGI_FORMAT_R32_TYPELESS, 32}, 
  {DXGI_FORMAT_D32_FLOAT, 32}, 
  {DXGI_FORMAT_R32_FLOAT, 32}, 
  {DXGI_FORMAT_R32_UINT, 32}, 
  {DXGI_FORMAT_R32_SINT, 32}, 
  {DXGI_FORMAT_R24G8_TYPELESS, 32}, 
  {DXGI_FORMAT_D24_UNORM_S8_UINT, 32}, 
  {DXGI_FORMAT_R24_UNORM_X8_TYPELESS, 32}, 
  {DXGI_FORMAT_X24_TYPELESS_G8_UINT, 32}, 
  {DXGI_FORMAT_R8G8_TYPELESS, 16}, 
  {DXGI_FORMAT_R8G8_UNORM, 16}, 
  {DXGI_FORMAT_R8G8_UINT, 16}, 
  {DXGI_FORMAT_R8G8_SNORM, 16}, 
  {DXGI_FORMAT_R8G8_SINT, 16}, 
  {DXGI_FORMAT_R16_TYPELESS, 16}, 
  {DXGI_FORMAT_R16_FLOAT, 16}, 
  {DXGI_FORMAT_D16_UNORM, 16}, 
  {DXGI_FORMAT_R16_UNORM, 16}, 
  {DXGI_FORMAT_R16_UINT, 16}, 
  {DXGI_FORMAT_R16_SNORM, 16}, 
  {DXGI_FORMAT_R16_SINT, 16}, 
  {DXGI_FORMAT_R8_TYPELESS, 8}, 
  {DXGI_FORMAT_R8_UNORM, 8}, 
  {DXGI_FORMAT_R8_UINT, 8}, 
  {DXGI_FORMAT_R8_SNORM, 8}, 
  {DXGI_FORMAT_R8_SINT, 8}, 
  {DXGI_FORMAT_A8_UNORM, 8}, 
  {DXGI_FORMAT_R1_UNORM, 1}, 
  {DXGI_FORMAT_R9G9B9E5_SHAREDEXP, 32}, 
  {DXGI_FORMAT_R8G8_B8G8_UNORM, 32}, 
  {DXGI_FORMAT_G8R8_G8B8_UNORM, 32}, 
  {DXGI_FORMAT_BC1_TYPELESS, 8}, 
  {DXGI_FORMAT_BC1_UNORM, 8}, 
  {DXGI_FORMAT_BC1_UNORM_SRGB, 8}, 
  {DXGI_FORMAT_BC2_TYPELESS, 8}, 
  {DXGI_FORMAT_BC2_UNORM, 8}, 
  {DXGI_FORMAT_BC2_UNORM_SRGB, 8}, 
  {DXGI_FORMAT_BC3_TYPELESS, 8}, 
  {DXGI_FORMAT_BC3_UNORM, 8}, 
  {DXGI_FORMAT_BC3_UNORM_SRGB, 8}, 
  {DXGI_FORMAT_BC4_TYPELESS, 8}, 
  {DXGI_FORMAT_BC4_UNORM, 8}, 
  {DXGI_FORMAT_BC4_SNORM, 8}, 
  {DXGI_FORMAT_BC5_TYPELESS, 8}, 
  {DXGI_FORMAT_BC5_UNORM, 8}, 
  {DXGI_FORMAT_BC5_SNORM, 8}, 
  {DXGI_FORMAT_B5G6R5_UNORM, 16}, 
  {DXGI_FORMAT_B5G5R5A1_UNORM, 16}, 
  {DXGI_FORMAT_B8G8R8A8_UNORM, 32}, 
  {DXGI_FORMAT_B8G8R8X8_UNORM, 32}, 
  {DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM, 32}, 
  {DXGI_FORMAT_B8G8R8A8_TYPELESS, 32}, 
  {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, 32}, 
  {DXGI_FORMAT_B8G8R8X8_TYPELESS, 32}, 
  {DXGI_FORMAT_B8G8R8X8_UNORM_SRGB, 32}, 
  {DXGI_FORMAT_BC6H_TYPELESS, 16}, 
  {DXGI_FORMAT_BC6H_UF16, 16}, 
  {DXGI_FORMAT_BC6H_SF16, 16}, 
  {DXGI_FORMAT_BC7_TYPELESS, 16}, 
  {DXGI_FORMAT_BC7_UNORM, 16}, 
  {DXGI_FORMAT_BC7_UNORM_SRGB, 16} 
};

static int FormatToBpp(DXGI_FORMAT format)
{
#if defined(_XBOX) || defined(_X360SHADERGEN)
  return XGBitsPerPixelFromFormat(format);
#else
  for (int i = 0; i < lenof(KnownFormats); i++) {
    if (KnownFormats[i]._format == format) return KnownFormats[i]._bpp;
  }
  return 0;
#endif
}

/** converts refresh rate from rational to int */
int ConvertRefreshRate(DXGI_RATIONAL rate)
{
  if (rate.Denominator == 0)
    return 0;

  return rate.Numerator / (float)rate.Denominator + 0.5f;
}

static const PASSMATRIXTYPE D3DMatIdentity(
  1,0,0, 0,
  0,1,0, 0,
  0,0,1, 0,
  0,0,0, 1
  );

static const PASSMATRIXTYPE D3DMatZero(
                                       0,0,0, 0,
                                       0,0,0, 0,
                                       0,0,0, 0,
                                       0,0,0, 0
                                       );

bool NotInRecording()
{
  return !GEngineDD11 || !GEngineDD11->IsCBRecording();
}


static void EncodeTemperatureToRGB1(float temperature, float &R, float &G, float &B) 
{
  float temperature03 = (temperature - TempMin) * InvTempDiff;
  saturate(temperature03, 0.0f, 1.0f);

  temperature03 *= 3.0f;

  if (temperature03 < 1)
  {
    R = temperature03;
    G = 0.0f;
    B = 0.0f;
  }
  else if (temperature03 < 2)
  {
    R = 1.0f;
    G = temperature03 - 1.0f;
    B = 0.0f;
  }
  else
  {
    R = 1.0f;
    G = 1.0f;
    B = temperature03 - 2.0f;
  }
}

static const char *SBTFileName[] =  
{
  "Shaders_Def",
  "Shaders_nV", // nVidia
  "Shaders_Def", // nVidiaZINT
  "Shaders_Def",
  "Shaders_Def",
  "Shaders_Def",
};
namespace Unique1
{
  COMPILETIME_COMPARE(lenof(SBTFileName), SBTCount)
}

static AutoArray<RString> SourcesPP;
static AutoArray<RString> SourcesPS;
static AutoArray<RString> SourcesVS;

static const char HLSLCacheDir[] = "bin\\";

/// used for Shader Model 3.0 shaders
#define RelShaderPath_4_0 "ShaderSources_4_0\\"

//! Folder that contains all the shader sources
static RString HLSLDir;

//! Pixel shader source files
static RString PSHeader;
static RString PSFile;
static RString PSBasicFile;
static RString PSBasicSSSMFile;
static RString PSSR_DefaultFile;
static RString PSSR_nVidiaFile;
static RString PSThermalFile;

//! Vertex shader source files
static RString FPShaders;
static RString VSHeader;
static RString VSFile;
static RString VSTerrainFile;
static RString VSSpriteFile;
static RString VSPointFile;
static RString VSWaterFile;
static RString VSShoreFile;
static RString VSTerrainGrassFile;
static RString VSShadowVolumeFile;
static RString VSCalmWaterFile;
static RString VSVolCloudFile;
static RString VSRoadFile;
#if _ARMA3_SIMUL_WEATHER
static RString VSSimulWeatherFile;
#endif

/// informative - dependency information used for the cache
static RString ShaderInclude;
//! File common for both the shaders and program
static RString HLSLPRGCommon;

#if _ENABLE_CHEATS
/// sometimes we want to conform to API requirements even when debug API is not used
static bool StrictAPI = false;
#else
static const bool StrictAPI = false;
#endif

/// Influence density of trees by adjusting the final alpha - increase density, AToC otherwise does silly things
/**
half alpha = pow(diffuse.a*PSC_TreeAdjust.x,PSC_TreeAdjust.y)*PSC_TreeAdjust.z + PSC_TreeAdjust.w;
|| note: using x or z is redundant 
half alpha = pow(diffuse.a,PSC_TreeAdjust.y)*pow(PSC_TreeAdjust.x,PSC_TreeAdjust.y)*PSC_TreeAdjust.z + PSC_TreeAdjust.w;
*/
static float TreeAdjustPars[4] = {1,1,1,0};
#if _XBOX
static float TreeAdjustParsAToC[4] = {1,0.2,1,0.124};
#else
/// increase density, AToC otherwise does silly things
static float TreeAdjustParsAToC[4] = {1.45,0.75,1.0,0.0}; // used for Tree Adv shaders
static float TreeAdjustParsAToCExt[4] = {1.0,0.4,1.5,0}; // used for PSTreeAToC shader
#endif

  struct SLPoint
  {
    D3DXVECTOR4 TransformedPos;
    D3DXVECTOR4 Atten;
    D3DXVECTOR4 D;
    D3DXVECTOR4 A;
  };

  struct SLSpot
  {
    D3DXVECTOR4 TransformedPos_CosPhiHalf;
    D3DXVECTOR4 TransformedDir_CosThetaHalf;
    D3DXVECTOR4 D_InvCTHMCPH;
    D3DXVECTOR4 A_Atten;
  };

  /// conversion table SRGB (float, ~12b precision) to linear space (float)
  struct ConvertFromSRGB
  {
    float _table[255*4+1];

    ConvertFromSRGB()
    {
      for (int i=0; i<lenof(_table); i++)
      {
        float srgb = i*(1.0f/(lenof(_table)-1));
        float l = pow((srgb+0.055f)*(1.0f/1.055f),2.4f);
        saturate(l,0,1);
        _table[i] = l;
      }
    }
    float operator () (float x) const
    {
      int index = toInt(x*(lenof(_table)-1));
      saturate(index,0,lenof(_table)-1);
      return _table[index];
    }
  } FromSRGB;

  /// conversion table linear (float, ~12b precision) to SRGB
  struct ConvertToSRGB
  {
    float _table[255*4+1];

    ConvertToSRGB()
    {
      for (int i=0; i<lenof(_table); i++)
      {
        float l = i*(1.0f/(lenof(_table)-1));
        float srgb = 1.055f*pow(l,1/2.4f)-0.055f;
        saturate(srgb,0,1);
        _table[i] = srgb;
      }
    }
    float operator () (float x) const
    {
      int index = toInt(x*(lenof(_table)-1));
      saturate(index,0,lenof(_table)-1);
      return _table[index];
    }
  } ToSRGB;

  /// convert from SRGB to linear space
  static inline Color SRGBToLinear(ColorVal color)
  {
    return Color(
      FromSRGB(color.R()),
      FromSRGB(color.G()),
      FromSRGB(color.B()),
      color.A()
      );
  }

  /// convert from linear to SRGB  space
  static inline Color LinearToSRGB(ColorVal color)
  {
    return Color(
      ToSRGB(color.R()),
      ToSRGB(color.G()),
      ToSRGB(color.B()),
      color.A()
      );
  }


/* Search hndl between existing effects. Return true if hndl is valid, else return false. */
static bool ValidatePPEHndl(const FindArrayKey<PostProcessHndl> &array, int hndl)
{
  for (int i = 0; i < array.Size(); i++)
  {
    if (hndl == array[i]._hndl) return true;
  }

  return false;
}


#if _DEBUG
#define VALIDATE_HNDL_ENABLE 1
#else
#define VALIDATE_HNDL_ENABLE 0
#endif


static void InitShaderDirs()
{
#ifndef _XBOX
  BString<256> shaderDir = __FILE__; // should resolve to the renderer source directory
  const char *nam = GetFilenameExt(shaderDir);
  if (nam)
  {
    // replace the filename part with the Direct3D 11 shader directory
    strcpy(LString(shaderDir).Mid(nam),RelShaderPath_4_0);
  }
  else
  {
    // no failure expected, but if there was one, try to recover
    Fail("Unexpected file path");
    strcpy(shaderDir,".\\" RelShaderPath_4_0);
  }

  HLSLDir = shaderDir.cstr();
#else
  HLSLDir = "#:\\d3d11\\" RelShaderPath_4_0;
#endif

  FPShaders = HLSLDir + "FPShaders.hlsl";

  PSHeader = HLSLDir + "PS.h";
  PSFile = HLSLDir + "PS.hlsl";
  PSBasicFile = HLSLDir + "PSBasic.hlsl";
  PSBasicSSSMFile = HLSLDir + "PSBasicSSSM.hlsl";
  PSSR_DefaultFile = HLSLDir + "PSSR_Default.hlsl";
  PSSR_nVidiaFile = HLSLDir + "PSSR_nVidia.hlsl";
  PSThermalFile = HLSLDir + "PSThermal.hlsl";

  VSHeader = HLSLDir + "VS.h";
  VSFile = HLSLDir + "VS.hlsl";
  VSTerrainFile = HLSLDir + "VSTerrain.hlsl";
  VSSpriteFile = HLSLDir + "VSSprite.hlsl";
  VSPointFile = HLSLDir + "VSPoint.hlsl";
  VSWaterFile = HLSLDir + "VSWater.hlsl";
  VSShoreFile = HLSLDir + "VSShore.hlsl";
  VSTerrainGrassFile = HLSLDir + "VSTerrainGrass.hlsl";
  VSShadowVolumeFile = HLSLDir + "VSShadowVolume.hlsl";
  VSCalmWaterFile = HLSLDir + "VSCalmWater.hlsl";
  VSVolCloudFile = HLSLDir + "VSVolCloud.hlsl";
  VSRoadFile = HLSLDir + "VSRoad.hlsl";
#if _ARMA3_SIMUL_WEATHER
  VSSimulWeatherFile = HLSLDir + "VSSimulWeather.hlsl";
#endif
  ShaderInclude = HLSLDir + "psvs.h";
  HLSLPRGCommon = HLSLDir + "common.h";

  SourcesVS.Clear();
  SourcesPS.Clear();
  SourcesPP.Clear();
  SourcesPP.Add(FPShaders);
  SourcesPP.Add(PSFile);
  SourcesPS.Add(PSFile);
  SourcesPP.Add(PSHeader);
  SourcesPS.Add(PSHeader);
  SourcesPS.Add(PSBasicFile);
  SourcesPS.Add(PSBasicSSSMFile);
  SourcesPS.Add(PSSR_DefaultFile);
  SourcesPS.Add(PSSR_nVidiaFile);
  SourcesPS.Add(PSThermalFile);
  SourcesVS.Add(VSHeader);
  SourcesVS.Add(VSFile);
  SourcesVS.Add(VSTerrainFile);
  SourcesVS.Add(VSSpriteFile);
  SourcesVS.Add(VSPointFile);
  SourcesVS.Add(VSWaterFile);
  SourcesVS.Add(VSShoreFile);
  SourcesVS.Add(VSTerrainGrassFile);
  SourcesVS.Add(VSShadowVolumeFile);
  SourcesVS.Add(VSCalmWaterFile);
  SourcesVS.Add(VSVolCloudFile);
  SourcesVS.Add(VSRoadFile);
#if _ARMA3_SIMUL_WEATHER
  SourcesVS.Add(VSSimulWeatherFile);
#endif
  SourcesPS.Add(ShaderInclude);
  SourcesVS.Add(ShaderInclude);
  SourcesPP.Add(ShaderInclude);
  SourcesPS.Add(HLSLPRGCommon);
  SourcesVS.Add(HLSLPRGCommon);
  SourcesPP.Add(HLSLPRGCommon);
  SourcesPS.Compact();
  SourcesVS.Compact();
  SourcesPP.Compact();
}

static RString ShaderCacheName(RString section)
{
  return RString("Shaders_")+section + RString("_DX11");
}

/// allow setting D3DXMACRO in one function call - a kind of "external constructor"
static inline void SetMacro(D3D10_SHADER_MACRO &ret, const char *name, const char *value)
{
  ret.Name = name;
  ret.Definition = value;
}


__forceinline static unsigned char CalculatePostEffectsMask(int quality)
{
  unsigned char postFXLevelMask = (1 << (quality + 1)) - 1;

  return postFXLevelMask;
}

static __forceinline PackedColor ColorScaleD2(PackedColor col)
{
  // multiply RGB by 0.5, keep A
  return PackedColor(((col&0xfefefe)>>1)|(col&0xff000000));
}

static __forceinline PackedColor ColorScale(PackedColor col) {return col;}

/// fill skinning weight and indices into a single weighting entry
inline void FillWeightsAndIndices(BYTE *weights, BYTE *indices, const AnimationRTWeight &ai)
{
  int wsize = ai.Size();
  // Copy indices and weights
  for (int w = 0; w < wsize; w++)
  {
    weights[w] = ai[w].GetWeight8b();
  }
  for (int w = wsize; w < 4; w++)
  {
    weights[w] = 0;
  }
  for (int w = 0; w < wsize; w++)
  {
    indices[w] = GetSubSkeletonIndex(ai[w].GetSel());
  }
  // Zero remaining indices and weights
  for (int w = wsize; w < 4; w++)
  {
    indices[w] = 0;
  }
}

#if defined _M_PPC && defined _MATH3DK_HPP
static const __vector4 Vec0001={0,0,0,1};
#endif

void ConvertMatrixTransposed(PASSMATRIXTYPE &mat, Matrix4Val src)
{
#if defined _M_PPC && defined _MATH3DK_HPP
  COMPILETIME_COMPARE(sizeof(XMMATRIX),sizeof(PASSMATRIXTYPE));
  XMMATRIX transposed = XMMatrixTranspose(*(const XMMATRIX *)&src);
  *(XMVECTOR *)&mat.m[0] = transposed.r[0];
  *(XMVECTOR *)&mat.m[1] = transposed.r[1];
  *(XMVECTOR *)&mat.m[2] = transposed.r[2];
  *(XMVECTOR *)&mat.m[3] = Vec0001;
#else

  mat._11 = src.DirectionAside()[0];
  mat._21 = src.DirectionAside()[1];
  mat._31 = src.DirectionAside()[2];
  mat._41 = 0;

  mat._12 = src.DirectionUp()[0];
  mat._22 = src.DirectionUp()[1];
  mat._32 = src.DirectionUp()[2];
  mat._42 = 0;

  mat._13 = src.Direction()[0];
  mat._23 = src.Direction()[1];
  mat._33 = src.Direction()[2];
  mat._43 = 0;

  mat._14 = src.Position()[0];
  mat._24 = src.Position()[1];
  mat._34 = src.Position()[2];
  mat._44 = 1;
#endif
}

void ConvertMatrixTransposed3(MatrixFloat34 &mat, Matrix4Val src)
{
#if defined _M_PPC && defined _MATH3DK_HPP
  COMPILETIME_COMPARE(sizeof(XMMATRIX),sizeof(Matrix4));
  XMMATRIX transposed = XMMatrixTranspose(*(const XMMATRIX *)&src);
  mat[0] = transposed.r[0];
  mat[1] = transposed.r[1];
  mat[2] = transposed.r[2];
#else
  mat[0][0] = src.DirectionAside()[0];
  mat[1][0] = src.DirectionAside()[1];
  mat[2][0] = src.DirectionAside()[2];

  mat[0][1] = src.DirectionUp()[0];
  mat[1][1] = src.DirectionUp()[1];
  mat[2][1] = src.DirectionUp()[2];

  mat[0][2] = src.Direction()[0];
  mat[1][2] = src.Direction()[1];
  mat[2][2] = src.Direction()[2];

  mat[0][3] = src.Position()[0];
  mat[1][3] = src.Position()[1];
  mat[2][3] = src.Position()[2];
#endif
}

static void ConvertMatrixTransposed3Offset(MatrixFloat34 &mat, Matrix4Val src, Vector3Par offset)
{
#if defined _M_PPC && defined _MATH3DK_HPP
  Matrix4 off;
  off.SetDirectionAside(src.DirectionAside());
  off.SetDirectionUp(src.DirectionUp());
  off.SetDirection(src.Direction());
  off.SetPosition(src.Position()-offset);
  COMPILETIME_COMPARE(sizeof(XMMATRIX),sizeof(Matrix4));
  XMMATRIX transposed = XMMatrixTranspose(*(const XMMATRIX *)&off);
  mat[0] = transposed.r[0];
  mat[1] = transposed.r[1];
  mat[2] = transposed.r[2];
#else
  mat[0][0] = src.DirectionAside()[0];
  mat[1][0] = src.DirectionAside()[1];
  mat[2][0] = src.DirectionAside()[2];

  mat[0][1] = src.DirectionUp()[0];
  mat[1][1] = src.DirectionUp()[1];
  mat[2][1] = src.DirectionUp()[2];

  mat[0][2] = src.Direction()[0];
  mat[1][2] = src.Direction()[1];
  mat[2][2] = src.Direction()[2];

  mat[0][3] = src.Position()[0]-offset[0];
  mat[1][3] = src.Position()[1]-offset[1];
  mat[2][3] = src.Position()[2]-offset[2];
#endif
}

/// threshold used for high-pass filter
static float BloomThreshold = 0.75f;
/// contribution of bloom image in composition pass
static float BloomBlurScale = 0.5f;
/// contribution of original image in composition pass
static float BloomImgScale = 1.0f;
/// coef used to multiply thresholded value
static float BloomOverbrightCoef = 5.0f;
/// kernel size for gaussian blur 
static int BloomBlurKernelSize = 7;

///=========================================================
// Input layouts
///=========================================================
#define D3DDECLTYPESPEC_UVDX11 DXGI_FORMAT_R16G16_SINT
#define D3DDECLTYPESPEC_VEDX11 DXGI_FORMAT_R8G8B8A8_UINT


static D3D11_INPUT_ELEMENT_DESC vertexDeclNonTL[] =
{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"COLOR",    0, DXGI_FORMAT_R8G8B8A8_UINT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"COLOR",    1, DXGI_FORMAT_R8G8B8A8_UINT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};


// Standard vertex declarations

static D3D11_INPUT_ELEMENT_DESC vertexDeclPoint[] =
{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, D3DDECLTYPESPEC_UVDX11,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"COLOR",    0, DXGI_FORMAT_R8G8B8A8_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

// Standard vertex declarations

static D3D11_INPUT_ELEMENT_DESC vertexDeclBasic[] =
{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",   0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
#if DX11_DUMMY_VALUES
  {"TEXCOORD", 1, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, //DX11 DUMMY VALUES
  {"TANGENT",  0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, //DX11 DUMMY VALUES
  {"TANGENT",  1, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, //DX11 DUMMY VALUES
#endif
};

static D3D11_INPUT_ELEMENT_DESC vertexDeclNormal[] =
{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",   0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
#if DX11_DUMMY_VALUES
  {"TEXCOORD", 1, D3DDECLTYPESPEC_UVDX11,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, // DX11 DUMMY VALUES
#endif
  {"TANGENT",  0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",  1, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

static D3D11_INPUT_ELEMENT_DESC vertexDeclBasicUV2[] =
{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",   0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 1, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
#if DX11_DUMMY_VALUES
  {"TANGENT",  0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, //DX11 DUMMY VALUES
  {"TANGENT",  1, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, //DX11 DUMMY VALUES
#endif
};

static D3D11_INPUT_ELEMENT_DESC vertexDeclNormalUV2[] =
{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",   0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 1, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",  0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",  1, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

static D3D11_INPUT_ELEMENT_DESC vertexDeclNormalCustom[] =
{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",   0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",  0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",  1, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"POSITION", 1, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
#if _VBS3_MORETERRAINLODS
  {"POSITION", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
#endif
#if _VBS3_CPU_LAND_LIGHT
  {"TEXCOORD", 1, DXGI_FORMAT_R8G8B8A8_UINT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
#endif
#if _VBS3_LAND_SHADOW
  {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
#endif
};

static D3D11_INPUT_ELEMENT_DESC vertexDeclShadowVolume[] =
{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",   0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

static D3D11_INPUT_ELEMENT_DESC vertexDeclSkinning[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      0, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
#if DX11_DUMMY_VALUES
  {"TEXCOORD",      1, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, //DX11 DUMMY VALUES
  {"TANGENT",       0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, //DX11 DUMMY VALUES
  {"TANGENT",       1, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, //DX11 DUMMY VALUES
#endif
  {"BLENDWEIGHT",   0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

static D3D11_INPUT_ELEMENT_DESC vertexDeclNormalSkinning[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      0, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
#if DX11_DUMMY_VALUES
  {"TEXCOORD",      1, D3DDECLTYPESPEC_UVDX11,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, //DX11 DUMMY VALUES
#endif
  {"TANGENT",       0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",       1, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDWEIGHT",   0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

static D3D11_INPUT_ELEMENT_DESC vertexDeclUV2Skinning[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      0, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      1, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
#if DX11_DUMMY_VALUES
  {"TANGENT",       0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, //DX11 DUMMY VALUES
  {"TANGENT",       1, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, //DX11 DUMMY VALUES
#endif
  {"BLENDWEIGHT",   0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

static D3D11_INPUT_ELEMENT_DESC vertexDeclNormalUV2Skinning[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      0, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      1, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",       0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",       1, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDWEIGHT",   0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

static D3D11_INPUT_ELEMENT_DESC vertexDeclShadowVolumeSkinning[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDWEIGHT",   0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"POSITION",      1, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDWEIGHT",   1, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  1, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"POSITION",      2, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDWEIGHT",   2, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  2, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

static D3D11_INPUT_ELEMENT_DESC vertexDeclInstancing[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      0, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
#if DX11_DUMMY_VALUES
  {"TEXCOORD",      1, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, //DX11 DUMMY VALUES
  {"TANGENT",       0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, //DX11 DUMMY VALUES
  {"TANGENT",       1, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, //DX11 DUMMY VALUES
#endif
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

static D3D11_INPUT_ELEMENT_DESC vertexDeclNormalInstancing[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      0, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
#if DX11_DUMMY_VALUES
  {"TEXCOORD",      1, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, //DX11 DUMMY VALUES
#endif
  {"TANGENT",       0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",       1, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

static D3D11_INPUT_ELEMENT_DESC vertexDeclUV2Instancing[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      0, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      1, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
#if DX11_DUMMY_VALUES
  {"TANGENT",       0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, //DX11 DUMMY VALUES
  {"TANGENT",       1, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}, //DX11 DUMMY VALUES
#endif
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

static D3D11_INPUT_ELEMENT_DESC vertexDeclNormalUV2Instancing[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      0, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      1, D3DDECLTYPESPEC_UVDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",       0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",       1, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

static D3D11_INPUT_ELEMENT_DESC vertexDeclShadowVolumeInstancing[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX11,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

static D3D11_INPUT_ELEMENT_DESC vertexDeclSpriteInstancing[] =
{
  {"TEXCOORD", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

// VD for simul weather clouds with fragment distance fading
static D3D11_INPUT_ELEMENT_DESC vertexDeclSimulWeatherClouds[] =
{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 3, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

// VD for simul weather clouds with CPU distance fading
static D3D11_INPUT_ELEMENT_DESC vertexDeclSimulWeatherCloudsCPU[] =
{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 3, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 4, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 5, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

#define VERTEX_DECL11(decl) VertexDeclaration(decl, sizeof(decl)/sizeof(decl[0]), #decl)
#define VERTEX_DECL11_NULL VertexDeclaration::NULLDeclaration

static VertexDeclaration VertexDeclsDX11[][VSDVCount] =
{
  //VD_Tex0
  {VERTEX_DECL11_NULL,                    VERTEX_DECL11_NULL,                             VERTEX_DECL11(vertexDeclSpriteInstancing)},
  //VD_Position_Tex0_Color
  {VERTEX_DECL11(vertexDeclPoint),        VERTEX_DECL11_NULL,                             VERTEX_DECL11_NULL},
  //VD_Position_Normal_Tex0
  {VERTEX_DECL11(vertexDeclBasic),        VERTEX_DECL11(vertexDeclSkinning),              VERTEX_DECL11(vertexDeclInstancing)},
  //VD_Position_Normal_Tex0_ST
  {VERTEX_DECL11(vertexDeclNormal),       VERTEX_DECL11(vertexDeclNormalSkinning),        VERTEX_DECL11(vertexDeclNormalInstancing)},
  //VD_Position_Normal_Tex0_Tex1
  {VERTEX_DECL11(vertexDeclBasicUV2),     VERTEX_DECL11(vertexDeclUV2Skinning),           VERTEX_DECL11(vertexDeclUV2Instancing)},
  //VD_Position_Normal_Tex0_Tex1_ST
  {VERTEX_DECL11(vertexDeclNormalUV2),    VERTEX_DECL11(vertexDeclNormalUV2Skinning),     VERTEX_DECL11(vertexDeclNormalUV2Instancing)},
  //VD_Position_Normal_Tex0_ST_Float3
  {VERTEX_DECL11(vertexDeclNormalCustom), VERTEX_DECL11_NULL,                             VERTEX_DECL11_NULL},
  //VD_Position_Normal_Tex0_WeightsAndIndices
  {VERTEX_DECL11_NULL,                    VERTEX_DECL11_NULL,                             VERTEX_DECL11_NULL},
  //VD_Position_Normal_Tex0_ST_WeightsAndIndices
  {VERTEX_DECL11_NULL,                    VERTEX_DECL11_NULL,                             VERTEX_DECL11_NULL},
  //VD_Position_Normal_Tex0_Tex1_WeightsAndIndices
  {VERTEX_DECL11_NULL,                    VERTEX_DECL11_NULL,                             VERTEX_DECL11_NULL},
  //VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices
  {VERTEX_DECL11_NULL,                    VERTEX_DECL11_NULL,                             VERTEX_DECL11_NULL},
  //VD_ShadowVolume
  {VERTEX_DECL11(vertexDeclShadowVolume), VERTEX_DECL11(vertexDeclShadowVolumeSkinning),  VERTEX_DECL11(vertexDeclShadowVolumeInstancing)},
  //VD_ShadowVolumeSkinned
  {VERTEX_DECL11_NULL,                    VERTEX_DECL11_NULL,                             VERTEX_DECL11_NULL},
  //VD_SimulWeatherClouds
  {VERTEX_DECL11(vertexDeclSimulWeatherClouds),  VERTEX_DECL11_NULL,                             VERTEX_DECL11_NULL},
  //VD_SimulWeatherCloudsCPU
  {VERTEX_DECL11(vertexDeclSimulWeatherCloudsCPU), VERTEX_DECL11_NULL,                             VERTEX_DECL11_NULL},
};

// NON-TL vertex declaration
static VertexDeclaration VertDeclNonTL = VERTEX_DECL11(vertexDeclNonTL);

namespace Unique2
{
  COMPILETIME_COMPARE(lenof(VertexDeclsDX11), VDCount)
}

//! Array to address both dimensions of the UV set
static int UV2[][2] =
{
  {VD_Tex0,                                             -1},
  {VD_Position_Tex0_Color,                              -1},
  {VD_Position_Normal_Tex0,                             VD_Position_Normal_Tex0_Tex1},
  {VD_Position_Normal_Tex0_ST,                          VD_Position_Normal_Tex0_Tex1_ST},
  {VD_Position_Normal_Tex0_Tex1,                        -1},
  {VD_Position_Normal_Tex0_Tex1_ST,                     -1},
  {VD_Position_Normal_Tex0_ST_Float3,                   -1},
  {VD_Position_Normal_Tex0_WeightsAndIndices,           VD_Position_Normal_Tex0_Tex1_WeightsAndIndices},
  {VD_Position_Normal_Tex0_ST_WeightsAndIndices,        VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices},
  {VD_Position_Normal_Tex0_Tex1_WeightsAndIndices,      -1},
  {VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices,   -1},
  {VD_ShadowVolume,                                     -1},
  {VD_ShadowVolumeSkinned,                              -1},
  {VD_SimulWeatherClouds,                               VD_SimulWeatherClouds},
  {VD_SimulWeatherCloudsCPU,                            VD_SimulWeatherCloudsCPU},
};

namespace Unique3
{
  COMPILETIME_COMPARE(lenof(UV2), VDCount)
}

//////////////////////////////////////////////////////////////////////////
// Vertex declarations

//!{ Basic

struct SVertexPoint9
{
  Vector3P pos;
  UVPairCompressed t0;
  D3DCOLOR color;
};

struct SVertex9;

struct SVertexNormal9
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
#if DX11_DUMMY_VALUES
  UVPairCompressed t1; // DX11 DUMMY VALUES
#endif
  VectorCompressed s;
  VectorCompressed t;
};

struct SVertexUV29
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  UVPairCompressed t1;
#if DX11_DUMMY_VALUES
  VectorCompressed s; // DX11 DUMMY VALUES
  VectorCompressed t; // DX11 DUMMY VALUES
#endif
};

struct SVertexNormalUV29
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  UVPairCompressed t1;
  VectorCompressed s;
  VectorCompressed t;
};

struct SVertexNormalCustom9
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  VectorCompressed s;
  VectorCompressed t;
  Vector3P custom;
#if _VBS3_MORETERRAINLODS
  float lod36[4]; // height values for LODs 3-6
#endif
#if _VBS3_CPU_LAND_LIGHT
  DWORD light; // encoded static terrain lighting
#endif
#if _VBS3_LAND_SHADOW
  float landShadow;
#endif
};

struct SVertexShadowVolume9
{
  Vector3P pos;
  VectorCompressed norm;
};

//!}

//!{ Skinned

struct SVertexSkinned9
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
#if DX11_DUMMY_VALUES
  UVPairCompressed t1; // DX11 DUMMY VALUES
  VectorCompressed s; //DX11 DUMMY VALUES
  VectorCompressed t; //DX11 DUMMY VALUES
#endif
  BYTE weights[4];
  BYTE indices[4];
};

struct SVertexNormalSkinned9
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
#if DX11_DUMMY_VALUES
  UVPairCompressed t1; // DX11 DUMMY VALUES
#endif
  VectorCompressed s;
  VectorCompressed t;
  BYTE weights[4];
  BYTE indices[4];
};

struct SVertexSkinnedUV29
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  UVPairCompressed t1;
#if DX11_DUMMY_VALUES
  VectorCompressed s; // DX11 DUMMY VALUES
  VectorCompressed t; // DX11 DUMMY VALUES
#endif
  BYTE weights[4];
  BYTE indices[4];
};

struct SVertexNormalSkinnedUV29
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  UVPairCompressed t1;
  VectorCompressed s;
  VectorCompressed t;
  BYTE weights[4];
  BYTE indices[4];
};

struct SVertexShadowVolumeSkinned9
{
  Vector3P pos;
  BYTE weights[4];
  BYTE indices[4];
  Vector3P posA;
  BYTE weightsA[4];
  BYTE indicesA[4];
  Vector3P posB;
  BYTE weightsB[4];
  BYTE indicesB[4];
};

//!}

//!{ Instanced version

struct SVertexInstanced9
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
#if DX11_DUMMY_VALUES
  UVPairCompressed t1; // DX11 DUMMY VALUES
  VectorCompressed s; // DX11 DUMMY VALUES
  VectorCompressed t; // DX11 DUMMY VALUES
#endif
  BYTE index[4]; // Just w is used, the rest is dummy
};

struct SVertexNormalInstanced9
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
#if DX11_DUMMY_VALUES
  UVPairCompressed t1; // DX11 DUMMY VALUES
#endif
  VectorCompressed s;
  VectorCompressed t;
  BYTE index[4]; // Just w is used, the rest is dummy
};

struct SVertexInstancedUV29
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  UVPairCompressed t1;
#if DX11_DUMMY_VALUES
  VectorCompressed s; // DX11 DUMMY VALUES
  VectorCompressed t; // DX11 DUMMY VALUES
#endif
  BYTE index[4]; // Just w is used, the rest is dummy
};

struct SVertexNormalInstancedUV29
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  UVPairCompressed t1;
  VectorCompressed s;
  VectorCompressed t;
  BYTE index[4]; // Just w is used, the rest is dummy
};

struct SVertexShadowVolumeInstanced9
{
  Vector3P pos;
  VectorCompressed norm;
  BYTE index[4]; // Just w is used, the rest is dummy
};

struct SVertexSpriteInstanced9
{
  DWORD encoded;
  //   BYTE t0u; // x86 LE: R x
  //   BYTE t0v; // G y
  //   BYTE index; // B z
  //   BYTE dummy; // A w
};

template <int vertexDecl>
struct VertexDeclTraits
{
};

template <>
struct VertexDeclTraits<VD_Position_Normal_Tex0>
{
  typedef SVertex9 VertexType;
  const static bool UV0Present = true;
  const static bool UV1Present = false;
};

template <>
struct VertexDeclTraits<VD_Position_Normal_Tex0_ST>
{
  typedef SVertexNormal9 VertexType;
  const static bool UV0Present = true;
  const static bool UV1Present = false;
};

template <>
struct VertexDeclTraits<VD_Position_Normal_Tex0_Tex1>
{
  typedef SVertexUV29 VertexType;
  const static bool UV0Present = true;
  const static bool UV1Present = true;
};

template <>
struct VertexDeclTraits<VD_Position_Normal_Tex0_Tex1_ST>
{
  typedef SVertexNormalUV29 VertexType;
  const static bool UV0Present = true;
  const static bool UV1Present = true;
};

template <>
struct VertexDeclTraits<VD_Position_Normal_Tex0_ST_Float3>
{
  typedef SVertexNormalCustom9 VertexType;
  const static bool UV0Present = true;
  const static bool UV1Present = false;
};

template <>
struct VertexDeclTraits<VD_ShadowVolume>
{
  typedef SVertexShadowVolume9 VertexType;
  const static bool UV0Present = false;
  const static bool UV1Present = false;
};

template <>
struct VertexDeclTraits<VD_Position_Normal_Tex0_WeightsAndIndices>
{
  typedef SVertexSkinned9 VertexType;
  const static bool UV0Present = true;
  const static bool UV1Present = false;
};

template <>
struct VertexDeclTraits<VD_Position_Normal_Tex0_ST_WeightsAndIndices>
{
  typedef SVertexNormalSkinned9 VertexType;
  const static bool UV0Present = true;
  const static bool UV1Present = false;
};

template <>
struct VertexDeclTraits<VD_Position_Normal_Tex0_Tex1_WeightsAndIndices>
{
  typedef SVertexSkinnedUV29 VertexType;
  const static bool UV0Present = true;
  const static bool UV1Present = true;
};

template <>
struct VertexDeclTraits<VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices>
{
  typedef SVertexNormalSkinnedUV29 VertexType;
  const static bool UV0Present = true;
  const static bool UV1Present = true;
};

template <>
struct VertexDeclTraits<VD_ShadowVolumeSkinned>
{
  typedef SVertexShadowVolumeSkinned9 VertexType;
  const static bool UV0Present = false;
  const static bool UV1Present = false;
};

template <int vertexDecl>
struct VertexDeclInstancedTraits
{
};

template <>
struct VertexDeclInstancedTraits<VD_Position_Normal_Tex0>
{
  typedef SVertexInstanced9 VertexType;
  const static bool UV0Present = true;
  const static bool UV1Present = false;
};

template <>
struct VertexDeclInstancedTraits<VD_Position_Normal_Tex0_ST>
{
  typedef SVertexNormalInstanced9 VertexType;
  const static bool UV0Present = true;
  const static bool UV1Present = false;
};

template <>
struct VertexDeclInstancedTraits<VD_Position_Normal_Tex0_Tex1>
{
  typedef SVertexInstancedUV29 VertexType;
  const static bool UV0Present = true;
  const static bool UV1Present = true;
};

template <>
struct VertexDeclInstancedTraits<VD_Position_Normal_Tex0_Tex1_ST>
{
  typedef SVertexNormalInstancedUV29 VertexType;
  const static bool UV0Present = true;
  const static bool UV1Present = true;
};

template <>
struct VertexDeclInstancedTraits<VD_ShadowVolume>
{
  typedef SVertexShadowVolumeInstanced9 VertexType;
  const static bool UV0Present = false;
  const static bool UV1Present = false;
};

// helper for async / sync creation of vertex / index buffer data
#define CREATE_BUFFER_DATA(Data, Type, Args) \
  if (CanCreateBuffersAsync()) \
{ \
  Type *op = new Type Args; \
  Ref<Data> result = op->CreateData(true); \
  _vis.CallDelegate(op); \
  if (Glob.config._renderThread==Config::ThreadingEmulated) VisThreadProcess(); \
  return result; \
} \
  else \
{ \
  Type op##Args; \
  return op.CreateData(false); \
}

///=========================================================
// Implementation of RenderTarget
///=========================================================
const RenderTarget RenderTarget::RenderTargetNULL = RenderTarget();

RenderTarget::RenderTarget(const RenderTargetFake& rt)
{
  _resource += rt._resource.GetRef();
  _rtView += rt._rtView.GetRef();
  _srView += rt._srView.GetRef();
}


///=========================================================
// Implementation of DepthStencil
///=========================================================
const DepthStencil DepthStencil::DepthStencilNULL = DepthStencil();

DepthStencil::DepthStencil(const DepthStencilFake& ds)
{
  _resource += ds._resource.GetRef();
  _dsView += ds._dsView.GetRef();
  _srView += ds._srView.GetRef();
}

///=========================================================
// Implementation of VertexDeclaration
///=========================================================
VertexDeclaration VertexDeclaration::NULLDeclaration(NULL, 0, "NULL");

VertexDeclaration::VertexDeclaration()
:_inputElements(0), _size(0), _usesTangent(false), _uses2TexCoords(false)
{
}

VertexDeclaration::VertexDeclaration(D3D11_INPUT_ELEMENT_DESC* inputElements, int size, const char *name)
:_inputElements(inputElements), _size(size), _usesTangent(false), _uses2TexCoords(false), _name(name)
{
  Assert(_inputElements || (!_inputElements && !_size));
  if (_inputElements)
    CheckInputElements();
}

void VertexDeclaration::CheckInputElements()
{
  Assert(_inputElements);
  if (!_inputElements)
    return;

  Assert(_size > 0);

  _usesTangent = false;
  _uses2TexCoords = false;

  for (int i=0; i<_size; ++i)
  {
    D3D11_INPUT_ELEMENT_DESC& desc = _inputElements[i];
    if (_stricmp(desc.SemanticName, "TEXCOORD") == 0 && desc.SemanticIndex > 0)
      _uses2TexCoords = true;
    else if (_stricmp(desc.SemanticName, "TANGENT") == 0)
      _usesTangent = true;
  }
}

///=========================================================
// Implementation of Queue11
///=========================================================
Queue11::Queue11()
{
  _triUsed = false;
  _vertexBufferUsed = 0;
  _indexBufferUsed = 0;
  _meshBase = 0;
  _meshSize = 0;
  _firstVertex = true;
  _firstIndex = true;
  _actualVBIndex=0;
  _actualIBIndex=0;

}

int Queue11::Allocate(TextureD3D11 *tex, int level, const TexMaterial *mat, int spec)
{
#if DO_TEX_STATS
  int free = -1;
#endif
  if (_triUsed)
  {
    if (tex==_tri._texture && mat==_tri._material && spec==_tri._special)
    {
      // append to queue index
      saturateMin(_tri._level,level);
      Assert (_triUsed);
      return 0;
    }
  }
  else
  {
    _tri._special = spec;
    _tri._texture = tex;
    _tri._level = level;
    _tri._material = mat;
    Assert (_tri._triangleQueue.Size()==0);
    _tri._triangleQueue.Resize(0);
    _triUsed = true;
#if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("Allocated new queue %d: %s",free,tex ? tex->Name() : "<NULL>");
    }
#endif
    return 0;
  }
  return -1;
}

void Queue11::Free()
{
  Assert (_tri._triangleQueue.Size()==0);
  Assert( _triUsed );
  _triUsed = false;
#if DO_TEX_STATS
  if (LogStatesOnce)
  {
    LogF("Released queue");
  }
#endif
}

///=========================================================
// Implementation of VertexDynamicData11
///=========================================================

static inline int FAST_D3DRGBA_SAT(float r, float g, float b, float a)
{
  int ia = toLargeInt(a * 255);
  int ir = toLargeInt(r * 255);
  int ig = toLargeInt(g * 255);
  int ib = toLargeInt(b * 255);
  saturateMin(ia,255);
  saturateMin(ir,255);
  saturateMin(ig,255);
  saturateMin(ib,255);
  // DX11 WARNING - this differs from DX9 version (argb->abgr), so we don't have to convert in shader
  //return (ia<<24)|(ir<<16)|(ig<<8)|ib;
  return (ia<<24)|(ib<<16)|(ig<<8)|ir;
}
 
#ifdef _XBOX
static const Vector3 V3MOne(-1,-1,-1);
#else
static const Vector3 V3One(1,1,1);
#endif

#if _PROFILE || !defined(_XBOX)
# define INLINE_PGO_BUG __forceinline
#else
/// with PGO we see a bug when the function is marked as inline
# define INLINE_PGO_BUG 
#endif

static INLINE_PGO_BUG void CopyNormal(VectorCompressed &tgt, Vector3Compressed src)
{
#ifdef _XBOX
  // vectors are already stored as XMXDECN4
  tgt.v = src.GetRaw();
#else
  // TODO: optimization possible
  Vector3 scaledX = (V3One - src.Get()) * 0.5f;
  tgt.v = FAST_D3DRGBA_SAT(scaledX.X(), scaledX.Y(), scaledX.Z(), 0);
#endif
}

VertexDynamicData11::VertexDynamicData11()
{
#ifdef DONT_USE_DYNBUFFERS
#else
  _actualVBIndex = 0;
  _vBufferSize = 0;
  _vBufferUsed = 0;
#endif
}

void VertexDynamicData11::Init(int size, MyD3DDeviceCB &dev)
{
#ifdef DONT_USE_DYNBUFFERS
  _geometry.Realloc(size);
  _geometry.Resize(0);
  _verticesOnly.Realloc(size);
  _verticesOnly.Resize(0);
#else
  DWORD usage = WRITEONLYVB|D3DUSAGE_DYNAMIC;
  int byteSize = size*sizeof(SVertex9);
  for (int i=0; i<NumDynamicVB3D; i++)
  {
RetryV:
    HRESULT err=dev->CreateVertexBuffer
      (
      byteSize,usage,0,D3DPOOL_DEFAULT,
      _vBuffer[i].Init(),NULL
      );    
    if (err!=D3D_OK)
    {
      if (err==D3DERR_OUTOFVIDEOMEMORY)
      {
        GEngineDD11->DDError9("CreateVertexBuffer - dynamic",err);
        if (GEngineDD11->TextBankDD()->ForcedReserveMemory(byteSize))
        {
          goto RetryV;
        }
      }
      GEngineDD11->DDError9("CreateVertexBuffer",err);
      return;
      // 
    }
  }
  _actualVBIndex = 0;
  _vBufferSize = size;
  _vBufferUsed = 0;
#endif
}

void VertexDynamicData11::Dealloc()
{
#ifdef DONT_USE_DYNBUFFERS
  _geometry.Clear();
  _verticesOnly.Clear();
#else
  for (int i=0; i<NumDynamicVB3D; i++)
  {
    _vBuffer[i].Free();
  }
#endif
}

int VertexDynamicData11::AddVertices(int cb, const VertexTableAnimationContext *animContext, const Shape &src)
{
  // Calculate the vertex buffer maximum and minimum UV's for first or for both stages
  // Note that even first stage computing is sometimes useless (shadow volume)
  // dynamic VB has a fixed format, never using 2nd UV
  UVPair minUV[MAX_UV_SETS], maxUV[MAX_UV_SETS];
  minUV[0] = src.MinUV();
  maxUV[0] = src.MaxUV();
  //if (src.NUVStages() > 1) src.CalculateMaxAndMinUV(1, maxUV[1], minUV[1]);

  // Calculate the vertex buffer UV's scale and offset and set it to the engine
  UVPair scale[MAX_UV_SETS];
  {
    int i = 0;
    scale[i].u = maxUV[i].u - minUV[i].u;
    scale[i].v = maxUV[i].v - minUV[i].v;
    saturateMax(scale[i].u, 1e-6f);
    saturateMax(scale[i].v, 1e-6f);
    Assert(maxUV[i].u != -1e6);
    Assert(maxUV[i].v != -1e6);
    Assert(minUV[i].u != 1e6);
    Assert(minUV[i].v != 1e6);
  }
  maxUV[1] = maxUV[0];
  minUV[1] = minUV[0];
  scale[1] = scale[0];
  GEngineDD11->SetTexGenScaleAndOffset(cb,scale, minUV);

#ifdef DONT_USE_DYNBUFFERS

  // Lock the shape data
  DoAssert(src.IsGeometryLocked());

  // Retrieve the vertex data into some auxiliary structure
  {
    // Prepare the auxiliary structure
    const int nVertex = src.NVertex();
    _verticesOnly.Access(nVertex - 1);
    SVertex9 *sData = _verticesOnly.Data();

    const UVPairs *uv = &src.GetUVArray();
    if (animContext && animContext->AreVerticesLoaded())
    {
      const Array<Vector3> *pos = &animContext->GetPosArray(&src);
      const Array<Vector3Compressed> *norm = &animContext->GetNormArray();
      for (int i = 0; i < nVertex; i++)
      {
        sData->pos = ConvertToP((*pos)[i]);
        CopyNormal(sData->norm,(*norm)[i]);
        sData->t0 = (*uv).GetUVRaw(i);
        sData++;
      }
    }
    else
    {
      const AutoArray<Vector3> *pos = &src.GetPosArray();
      const CondensedAutoArray<Vector3Compressed> *norm = &src.GetNormArray();
      for (int i = 0; i < nVertex; i++)
      {
        sData->pos = ConvertToP((*pos)[i]);
        CopyNormal(sData->norm,(*norm)[i]);
        sData->t0 = (*uv).GetUVRaw(i);
        sData++;
      }
    }
  }

  // Form the array of vertices organized to triangles
  {
    // Clear the array
    _geometry.Resize(0);

    // Go through all the sections
    // Normally the sections are ordered so the cur.end = next.beg
    // If not, sections are sharing triangles
    // - this is done for terrain mesh, which is sharing triangles and indices between sections
    // we avoid adding the same face twice
    Offset coveredSoFar = Offset(0);
    for (int s = 0; s < src.NSections(); s++)
    {
      const ShapeSection &sec = src.GetSection(s);
      // if the triangles from the section are already covered, do not convert them again
      if (sec.beg<coveredSoFar)
      {
        // if part of the section is covered, the whole section should be covered as well
        Assert(sec.end<=coveredSoFar);
        continue;
      }
      coveredSoFar = sec.end;
      for (Offset o=sec.beg; o<sec.end; src.NextFace(o))
      {
        const Poly &poly = src.Face(o);
        if (poly.N()<3) continue;
        if ((poly.N() == 3) &&
          ((poly.GetVertex(0) == poly.GetVertex(1)) || 
          (poly.GetVertex(1) == poly.GetVertex(2)) ||
          (poly.GetVertex(2) == poly.GetVertex(0)))) continue;
        for (int i=2; i<poly.N(); i++)
        {
#if V_OFFSET_CUSTOM
          _geometry.Add(_verticesOnly[poly.GetVertex(0)]);
          _geometry.Add(_verticesOnly[poly.GetVertex(i-1)]);
          _geometry.Add(_verticesOnly[poly.GetVertex(i)]);
#else
          Fail("Error: Not supported");
#endif
        }
      }
    }
  }

  // Return value in case of DONT_USE_DYNBUFFERS is not used
  return -1;
#else
  // this cannot be done while recording
  Assert(cb<0);
  const int nVertex = src.NVertex();
  SVertex9 *sData;
  int vOffset = Lock(sData, nVertex);
  if (vOffset >= 0)
  {
    ShapeGeometryLock<> lock(&src);


    const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
    if (animContext && animContext->AreVerticesLoaded())
    {
      const Array<Vector3> *pos = &animContext->GetPosArray(&src);
      const Array<Vector3> *norm = &animContext->GetNormArray();
      for (int i = 0; i < nVertex; i++)
      {
        sData->pos = ConvertToP((*pos)[i]);
        CopyNormal(sData->norm,(*norm)[i]);
        CopyUV(sData->t0, (*uv)[i], maxUV[0], minUV[0], invUV);
        sData++;
      }
    }
    else
    {
      const AutoArray<Vector3> *pos = &src.GetPosArray();
      const CondensedAutoArray<Vector3> *norm = &src.GetNormArray();
      for (int i = 0; i < nVertex; i++)
      {
        sData->pos = ConvertToP((*pos)[i]);
        CopyNormal(sData->norm,(*norm)[i]);
        CopyUV(sData->t0, (*uv)[i], maxUV[0], minUV[0], invUV);
        sData++;
      }
    }

    Unlock();
  }
  return vOffset;
#endif
}

int VertexDynamicData11::Lock(SVertex9 *&vertex, int n)
{
#ifdef DONT_USE_DYNBUFFERS
  // Resize the _geometry array
  _geometry.Resize(n);

  // Fill out the data pointer, return the offset
  vertex = _geometry.Data();

  // Return zero - we always start from the beginning
  return 0;
#else
  DWORD flags = NoSysLockFlag;
  if (_vBufferUsed + n >= _vBufferSize)
  {
    // discard old - start a new buffer
#if CUSTOM_VB_DISCARD
    if (++_actualVBIndex >= NumDynamicVB3D)
    {
      _actualVBIndex = 0;
    }
#else
    flags |= D3DLOCK_DISCARD;
#endif
    _vBufferUsed = 0;
    if (n > _vBufferSize)
    {
      ErrorMessage("Cannot lock vertices - v-buffer too small (%d<%d)", _vBufferSize, n);
      return -1;
    }   
  }
  else
  {
    // fit in current buffer
    flags |= D3DLOCK_NOOVERWRITE;
  }

  int size = sizeof(SVertex9) * n;
  int offset = sizeof(SVertex9) * _vBufferUsed;
Retry:
  void *data = NULL;
  HRESULT err;
  {
    PROFILE_DX_SCOPE(3ddvL);
#if defined _XBOX && _XBOX_VER>=2
    // range lock not supported - emulate it
    err = GEngineDD11->Lock(_vBuffer[_actualVBIndex], 0, _vBufferSize*sizeof(SVertex9), &data, flags);
    data = (char *)data+offset;
#else
    err = GEngineDD11->Lock(_vBuffer[_actualVBIndex], offset, size, &data, flags);
#endif
  }
  if (err!=D3D_OK || !data)
  {
    if (GEngineDD11->FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto Retry;
#ifndef _XBOX
    ErrF("VB Lock failed, %x,%s",err,DXGetErrorString(err));
#else
    ErrF("VB Lock failed, %x",err);
#endif
    ReportMemoryStatusWithVRAM();
    return -1;
  }

  int ret = _vBufferUsed;
  _vBufferUsed += n;
  vertex = (SVertex9 *)data;
  return ret;
#endif
}

void VertexDynamicData11::Unlock()
{
#ifdef DONT_USE_DYNBUFFERS
#else
  HRESULT err;
  err = _vBuffer[_actualVBIndex]->Unlock();
  if (err != D3D_OK)
  {
    GEngineDD11->DDError9("Cannot unlock vertex buffer.", err);
  }
#endif
}

///=========================================================
// Implementation of IndexStaticData11
///=========================================================

IndexStaticData11::IndexStaticData11(int indices) : _iBuffer(BufferFuture::Null)
{
  _count = indices;
  GEngineDD11->AddIBuffersAllocated(_count * sizeof(VertexIndex));
}

IndexStaticData11::~IndexStaticData11()
{
  Dealloc();
  GEngineDD11->AddIBuffersAllocated(-_count * sizeof(VertexIndex));
}

void IndexStaticData11::Init(ComRef<IDirect3DIndexBuffer9Old> buffer)
{
  _iBuffer = BufferFuture(buffer);
}
void IndexStaticData11::InitFuture()
{
  _iBuffer = BufferFuture();
}
void IndexStaticData11::SetFuture(ComRef<IDirect3DIndexBuffer9Old> buffer)
{
  _iBuffer.SetResult(buffer);
}
ComRef<IDirect3DIndexBuffer9Old> IndexStaticData11::CreateBuffer()
{
  ComRef<IDirect3DIndexBuffer9Old> buffer;

  if (_count <= 0) return buffer;

  MyD3DDeviceCB &dev = GEngineDD11->GetD3DDeviceRec();

  int byteSize = _count*sizeof(VertexIndex);
RetryI:
  EngineDD11::CheckMemTypeScope scope(GEngineDD11, "IBstatic", byteSize);
  PROFILE_SCOPE_DETAIL_EX(ddCIB,dd11);

  D3D11_BUFFER_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.ByteWidth = byteSize;
  desc.Usage = D3D11_USAGE_DYNAMIC;
  desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; 
  desc.MiscFlags = 0;
  desc.StructureByteStride = 0;
  HRESULT err = dev->CreateBuffer(&desc, NULL, buffer.Init());
  //HRESULT err = dev->CreateIndexBuffer(byteSize,WRITEONLYIB,D3DFMT_INDEX16,D3DPOOL_DEFAULT,buffer.Init(),NULL);
  if (err!=D3D_OK)
  {
    if (err==E_OUTOFMEMORY)
    {
      LogF("Out of memory during i-buffer creation");
      bool doRetry = false;
      if (GEngineDD11->TextBankDD()->ForcedReserveMemory(byteSize))
      {
        doRetry = true;
      }
      size_t freed = GEngineDD11->FreeOnDemandSystemMemoryLowLevel(byteSize);
      if (freed!=0 || doRetry) goto RetryI;
    }

    LogF("Error during i-buffer creation: error %x",err);
    // debugging opportunity
    size_t freed = GEngineDD11->FreeOnDemandSystemMemoryLowLevel(byteSize);
    if (freed!=0) goto RetryI;
    RptF("Error during i-buffer creation: error %x",err);
    ReportMemoryStatusWithVRAM();
    GEngineDD11->RequestFlushMemoryCritical();
    GEngineDD11->DDError11("CreateIndexBuffer",err);
    return buffer;
  }
  DoAssert(buffer);
  return buffer;
}

#define V_OFFSET_CUSTOM 1


void IndexStaticData11::Dealloc()
{
#ifdef _XBOX
  ComRef<IDirect3DIndexBuffer9> buffer = _iBuffer.GetResult();
  if (buffer)
  {
    if (buffer->IsBusy())
    {
      LOG_WAIT_GPU("index buffer", buffer);
      buffer->BlockUntilNotBusy();
    }
    buffer.Free();
  }
#endif
}

int IndexStaticData11::IndicesNeededTotal(const FaceData &src, int nInstances)
{
  if (src.GetCanonical())
  {
    return src.Size()*3*nInstances;
  }
  else
  {
    int indices = 0;
    for (Offset o=src.Begin(); o<src.End(); src.Next(o))
    {
      const Poly &poly = src[o];
      if (poly.N() < 3) continue;
      if ((poly.N() == 3) &&
        ( (poly.GetVertex(0) == poly.GetVertex(1)) || 
        (poly.GetVertex(1) == poly.GetVertex(2)) ||
        (poly.GetVertex(2) == poly.GetVertex(0)))) continue;
      indices += (poly.N()-2)*3;
    }
    return indices * nInstances;
  }
}

int IndexStaticData11::IndicesNeededStrippedTotal(const FaceArray &src, int nInstances)
{
  int indices = 0;
  for (int i = 0; i < src.NSections(); i++)
  {
    const ShapeSection &sec = src.GetSection(i);
    Assert((OffsetDiff(sec.end, sec.beg) % PolyVerticesSize(3)) == 0);
    int trianglesCount = OffsetDiff(sec.end, sec.beg) / PolyVerticesSize(3);
    indices += (2 + trianglesCount) * nInstances + (nInstances - 1) * 2;
    if (trianglesCount & 1)
    {
      indices += (nInstances - 1) * 1;
    }
  }
  return indices;
}


void *IndexStaticData11::Lock(ComRef<IDirect3DIndexBuffer9Old> buffer, int size)
{
RetryI:
  void *data = NULL;
  HRESULT err = GEngineDD11->Map(buffer, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &data);
  if (err!=D3D_OK || !data)
  {
    if (GEngineDD11->FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto RetryI;
#ifndef _XBOX
    ErrF("IB Lock failed, %x:%s",err,DXGetErrorString(err));
#else
    ErrF("IB Lock failed, %x",err);
#endif
    ReportMemoryStatusWithVRAM();
    return NULL;
  }
  return data;
}

bool IndexStaticData11::AddShape(ComRef<IDirect3DIndexBuffer9Old> buffer, const FaceData &srcDta, int indicesTotal, bool dynamic, int nInstances, int instanceVertices, const AutoArray<Offset> &sectionsEnds)
{
  // when adding dynamic data, add only indices

  if (indicesTotal > 0)
  {
    PROFILE_DX_SCOPE(3dibC);

    void *data = Lock(buffer, indicesTotal * sizeof(VertexIndex));
    if (!data)
      return false;

    VertexIndex *iData = (VertexIndex *)data;

    // Go through all the sections
    Offset secBeg = Offset(0);
    for (int s = 0; s < sectionsEnds.Size(); s++)
    {
      Offset secEnd = sectionsEnds[s];
      if (srcDta.GetCanonical() && nInstances==1)
      {
        PROFILE_DX_SCOPE(3diCC);
        // optimize common case (terrain)
        for (Offset o=secBeg; o<secEnd; srcDta.NextCanonical(o))
        {
          const Poly &poly = srcDta[o];
          *iData++ = poly.GetVertex(0);
          *iData++ = poly.GetVertex(1);
          *iData++ = poly.GetVertex(2);
        }
      }
      else
      {
        PROFILE_DX_SCOPE(3diIC);
        for (int instance=0; instance<nInstances; instance++)
        {
          int instanceOffset = instanceVertices*instance;
          for (Offset o=secBeg; o<secEnd; srcDta.Next(o))
          {
            // HOTFIX: index out of range
            if (o >= srcDta.End())
            {
              GEngineDD11->Unmap(buffer, 0);
              return false;
            }

            const Poly &poly = srcDta[o];
            if (poly.N()<3) continue;
            if ((poly.N() == 3) &&
              ((poly.GetVertex(0) == poly.GetVertex(1)) || 
              (poly.GetVertex(1) == poly.GetVertex(2)) ||
              (poly.GetVertex(2) == poly.GetVertex(0)))) continue;
            for (int i=2; i<poly.N(); i++)
            {
#if V_OFFSET_CUSTOM
              *iData++ = poly.GetVertex(0)+instanceOffset;
              *iData++ = poly.GetVertex(i-1)+instanceOffset;
              *iData++ = poly.GetVertex(i)+instanceOffset;
#else
              *iData++ = poly.GetVertex(0)+vIndex+instanceOffset;
              *iData++ = poly.GetVertex(i-1)+vIndex+instanceOffset;
              *iData++ = poly.GetVertex(i)+vIndex+instanceOffset;
#endif
            }
          }
        }
      }
      secBeg = secEnd;
    }

    DoAssert(iData == &((VertexIndex *)data)[indicesTotal]);
    GEngineDD11->Unmap(buffer, 0);
  }
  return true;
}

/*
void IndexStaticData11::ScanMinMax(int &minI, int &maxI, int start, int size) const
{
void *iData = NULL;
_iBuffer->Lock(start*sizeof(short),size*sizeof(short),&iData,D3DLOCK_READONLY);
short *indices = (short *)iData;
minI = INT_MAX;
maxI = 0;
for (int i=0; i<size; i++)
{
int ii = indices[i];
if (minI>ii) minI = ii;
if (maxI<ii) maxI = ii;
}
_iBuffer->Unlock();
}
*/

bool IndexStaticData11::AddShapeSprite(ComRef<IDirect3DIndexBuffer9Old> buffer, const FaceData &src, int indicesTotal, bool dynamic, int nInstances)
{
  // when adding dynamic data, add only indices

  if (indicesTotal > 0)
  {
    PROFILE_DX_SCOPE(3dibC);


    void *data = Lock(buffer, indicesTotal * sizeof(VertexIndex));
    if (!data)
      return false;

    VertexIndex *iData = (VertexIndex *)data;
    for (int instance=0; instance<nInstances; instance++)
    {
      int instanceOffset = 4 * instance;
#if V_OFFSET_CUSTOM
      *iData++ = 0 + instanceOffset;
      *iData++ = 1 + instanceOffset;
      *iData++ = 2 + instanceOffset;
      *iData++ = 2 + instanceOffset;
      *iData++ = 1 + instanceOffset;
      *iData++ = 3 + instanceOffset;
#else
      *iData++ = 0 + vIndex + instanceOffset;
      *iData++ = 1 + vIndex + instanceOffset;
      *iData++ = 2 + vIndex + instanceOffset;
      *iData++ = 2 + vIndex + instanceOffset;
      *iData++ = 1 + vIndex + instanceOffset;
      *iData++ = 3 + vIndex + instanceOffset;
#endif
    }

    DoAssert(iData == &((VertexIndex *)data)[indicesTotal]);
    GEngineDD11->Unmap(buffer, 0);
  }
  return true;
}

bool IndexStaticData11::AddShapePoint(ComRef<IDirect3DIndexBuffer9Old> buffer, const FaceData &src, int indicesTotal, bool dynamic, int nInstances, int instanceVertices)
{
  // when adding dynamic data, add only indices

  if (indicesTotal > 0)
  {
    PROFILE_DX_SCOPE(3dibC);

    void *data = Lock(buffer, indicesTotal * sizeof(VertexIndex));
    if (!data)
      return false;

    VertexIndex *iData = (VertexIndex *)data;
    for (int i = 0; i < instanceVertices; i++)
    {
      int instanceOffset = 4 * i;
#if V_OFFSET_CUSTOM
      *iData++ = 0 + instanceOffset;
      *iData++ = 1 + instanceOffset;
      *iData++ = 2 + instanceOffset;
      *iData++ = 2 + instanceOffset;
      *iData++ = 1 + instanceOffset;
      *iData++ = 3 + instanceOffset;
#else
      *iData++ = 0 + vIndex + instanceOffset;
      *iData++ = 1 + vIndex + instanceOffset;
      *iData++ = 2 + vIndex + instanceOffset;
      *iData++ = 2 + vIndex + instanceOffset;
      *iData++ = 1 + vIndex + instanceOffset;
      *iData++ = 3 + vIndex + instanceOffset;
#endif
    }

    DoAssert(iData == &((VertexIndex *)data)[indicesTotal]);
    GEngineDD11->Unmap(buffer, 0);
  }
  return true;
}

///=========================================================
// Implementation of VertexBufferD3D11
///=========================================================

VertexBufferD3D11::VertexBufferD3D11()
{
  for (int i = 0; i < MAX_UV_SETS; i++)
  {
    _maxUV[i].u = _maxUV[i].v = -1e6;
    _minUV[i].u = _minUV[i].v = 1e6;
  }
}

VertexBufferD3D11::~VertexBufferD3D11()
{
  //LogF("Destroying VB: %x, Track=%x", this, Track());
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*GEngineDD11->TextBankDD());

  if (_sharedV)
  {
    GEngineDD11->DeleteVB(_sharedV);
    _sharedV.Free();
  }
  if (_sharedI)
  {
    GEngineDD11->DeleteIB(_sharedI);
    _sharedI.Free();
  }

#if 0
  if (_vBuffer)
  {
    char name[256];
    sprintf(name,"VB %x (%x)",_vBuffer,this);
    vbStats.Count(name,-1);

    //LogF("Destroyed VB %x (%x)",_vBuffer,this);
  }
#endif

  if (IsInList())
  {
    GEngineDD11->BufferReleased(this);
  }

  // not needed, object will be destroyed anyway, but we prefer destroying pointers explicitely
  SetOwner(NULL);
}

size_t VertexBufferD3D11::GetMemoryControlled() const
{
  size_t vram = 0;
  if (_sharedV) vram += _sharedV->Size() * _sharedV->VertexSize();
  if (_sharedI) vram += _sharedI->GetSize();
  Assert(vram<10*1024*1024);
  return vram;
}

size_t VertexBufferD3D11::GetBufferSize() const
{
  return 0;
}

template <int vertexDecl>
void VertexBufferD3D11::PrepareUV(const VertexTable &src)
{
  UVPair minUV0,maxUV0;
  UVPair minUV1,maxUV1;

  if (VertexDeclTraits<vertexDecl>::UV0Present)
  {
    minUV0 = src.MinUV(), maxUV0 = src.MaxUV();
  }

  if (VertexDeclTraits<vertexDecl>::UV1Present)
  {
    if (src.NUVStages() > 1)
    {
      minUV1 = src.MinUV(1), maxUV1 = src.MaxUV(1);
    }
    else
    {
      minUV1 = UVPair(0,0), maxUV1 = UVPair(1,1);
    }
  }

  // Calculate the vertex buffer maximum and minimum UV's for first or for both stages
  // Note that even first stage computing is sometimes useless (shadow volume)
  if (VertexDeclTraits<vertexDecl>::UV0Present)
    SetUVRange<0>(minUV0, maxUV0);
  else
    SetUVRangeDummy<0>();

  if (VertexDeclTraits<vertexDecl>::UV1Present)
    SetUVRange<1>(minUV1, maxUV1);
  else
    SetUVRangeDummy<1>();

}


bool VertexBufferD3D11::Init(EngineDD11 *engine, const Shape &src, VBType type, bool frequent)
{
  // note: when device is reset, all buffers need to be released and created again
  if (src.NVertex()<=0)
  {
    LogF("Empty vertices.");
    return false;
  }
  if (engine->ResetNeeded())
  {
    Fail("Unable to create vertex buffer - Reset needed");
    return false;
  }
  // geometry needs to be locked, as we need to get indices
  DoAssert(src.IsGeometryLocked());
  // currently only dynamic buffers are used 
  //dynamic = true;
  int nInstances = 1;
  // create and fill index buffer
  // note: index buffer is almost never changed
  switch (type)
  {
    /*
    case VBSmallDiscardable:
    _dynamic = true;
    _separate = true;
    break;
    */
  default:
    Fail("Unknown v-buffer type");

#if _ENABLE_SKINNEDINSTANCING
  case VBStatic:
  case VBStaticSkinned:
    {
      const int maxVertices = frequent ? 32000 : 8000;

      // Count number of possible instances, make sure it is never 0 (even though maxVertices is smaller)
      if (src.NVertex() <= maxVertices / 2)
      {
        nInstances = maxVertices/src.NVertex();
      }
    }
    _dynamic = false;
#else
  case VBStatic:
    {
      const int maxVertices = frequent ? 32000 : 8000;

      // Count number of possible instances, make sure it is never 0 (even though maxVertices is smaller)
      if (src.NVertex() <= maxVertices / 2)
      {
        nInstances = maxVertices/src.NVertex();
      }
    }
  case VBStaticSkinned:
    _dynamic = false;
#endif
    break;
    //_dynamic = true;
    //break;
  case VBStaticReused:
    // used for water and other buffers which are used multiple times
    frequent = true;
    _dynamic = false;
    break;
  case VBBigDiscardable:
    _dynamic = false;
    break;
  case VBSmallDiscardable:
    _dynamic = false;
    break;
  case VBNone: case VBNotOptimized:
    Fail("Cannot generate vertex buffer of given type");
  case VBDynamic:
    _dynamic = true;
    break;
  case VBDynamicDiscardable:
    _dynamic = true;
    break;
  }

  if (_dynamic && (src.NVertex() > MaxShapeVertices))
  {
    LogF("Error: Number of vertices too large for the dynamic vertex buffer.");
    return false;
  }

  if (!src.CanBeInstanced())
  {
    nInstances = 1;
  }

  if (type == VBStaticSkinned)
  {

#if _ENABLE_SKINNEDINSTANCING
    // Retrieve the largest bone interval
    int largestInterval = 0;
    for (int i = 0; i < src.NSections(); i++)
    {
      const ShapeSection &sec = src.GetSection(i);
      if (sec._bonesCount > largestInterval) largestInterval = sec._bonesCount;
    }

    // Limit the number of instances by constants available
    saturateMin(nInstances, CFragmentTransformSkinnedInstanced::GetMaxInstances(largestInterval));
    DoAssert(CFragmentTransformSkinnedInstanced::GetMaxInstances(largestInterval) == CFragmentShadowVolumeSkinnedInstanced::GetMaxInstances(largestInterval));
    DoAssert(nInstances > 0);
#else
    nInstances = 1;
#endif

    switch (src.GetVertexDeclaration())
    {
    case VD_Position_Normal_Tex0:
#if _ENABLE_SKINNEDINSTANCING
      saturateMin(nInstances,  GEngineDD11->MaxInstances());
      if (nInstances <= 1)
      {
        _sharedV = engine->AddShapeVertices<VD_Position_Normal_Tex0_WeightsAndIndices>(src, _dynamic, false, frequent);
      }
      else
      {
        _sharedV = engine->AddShapeVerticesSkinnedInstanced(src, _dynamic, false, nInstances);
      }
#else
      _sharedV = engine->AddShapeVertices<VD_Position_Normal_Tex0_WeightsAndIndices>(src, *this, _dynamic, false, frequent);
#endif
      break;
    case VD_Position_Normal_Tex0_ST:
#if _ENABLE_SKINNEDINSTANCING
      saturateMin(nInstances,  GEngineDD11->MaxInstances());
      if (nInstances <= 1)
      {
        _sharedV = engine->AddShapeVertices<VD_Position_Normal_Tex0_ST_WeightsAndIndices>(src, *this, _dynamic, false, frequent);
      }
      else
      {
        _sharedV = engine->AddShapeVerticesNormalSkinnedInstanced(src, _dynamic, false, nInstances, frequent);
      }
#else
      _sharedV = engine->AddShapeVertices<VD_Position_Normal_Tex0_ST_WeightsAndIndices>(src, *this, _dynamic, false, frequent);
#endif
      break;
    case VD_Position_Normal_Tex0_Tex1:
      _sharedV = engine->AddShapeVertices<VD_Position_Normal_Tex0_Tex1_WeightsAndIndices>(src, *this, _dynamic, false, frequent);
      break;
    case VD_Position_Normal_Tex0_Tex1_ST:
      _sharedV = engine->AddShapeVertices<VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices>(src, *this, _dynamic, false, frequent);
      break;
    case VD_ShadowVolume:
#if _ENABLE_SKINNEDINSTANCING
      saturateMin(nInstances,  GEngineDD11->MaxInstances());
      if (nInstances <= 1)
      {
        _sharedV = engine->AddShapeVertices<VD_ShadowVolumeSkinned>(src, *this, _dynamic, false, frequent);
      }
      else
      {
        _sharedV = engine->AddShapeVerticesShadowVolumeSkinnedInstanced(src, *this, _dynamic, false, nInstances);
      }
#else
      _sharedV = engine->AddShapeVertices<VD_ShadowVolumeSkinned>(src, *this, _dynamic, false, frequent);
#endif
      break;
    default:
      Fail("Unknown vertex declaration in the source shape");
    }
  }
  else
  {
    switch (src.GetVertexDeclaration())
    {
    case VD_Tex0:
      saturateMin(nInstances,engine->MaxInstancesSprite());
      if (nInstances <= 1)
      {
        Fail("Error: Non instanced sprite?");
      }
      else
      {
        _sharedV = engine->AddShapeVerticesSpriteInstanced(src, *this, _dynamic, type == VBBigDiscardable, frequent, nInstances);
      }
      break;
    case VD_Position_Tex0_Color:
      nInstances = 1;
      _sharedV = engine->AddShapeVerticesPoint(src, *this, _dynamic, type == VBBigDiscardable, frequent);
      break;
    case VD_Position_Normal_Tex0:
      saturateMin(nInstances,  GEngineDD11->MaxInstances());
      if (nInstances <= 1)
      {
        _sharedV = engine->AddShapeVertices<VD_Position_Normal_Tex0>(src, *this, _dynamic, type == VBBigDiscardable, frequent);
      }
      else
      {
        _sharedV = engine->AddShapeVerticesInstanced<VD_Position_Normal_Tex0>(src, *this, _dynamic, type == VBBigDiscardable, frequent, nInstances);
      }
      break;
    case VD_Position_Normal_Tex0_ST:
      saturateMin (nInstances, engine->MaxInstances());
      if (nInstances <= 1)
      {
        _sharedV = engine->AddShapeVertices<VD_Position_Normal_Tex0_ST>(src, *this, _dynamic, type == VBBigDiscardable, frequent);
      }
      else
      {
        _sharedV = engine->AddShapeVerticesInstanced<VD_Position_Normal_Tex0_ST>(src, *this, _dynamic, type == VBBigDiscardable, frequent, nInstances);
      }
      break;
    case VD_Position_Normal_Tex0_Tex1:
      saturateMin(nInstances,  GEngineDD11->MaxInstances());
      if (nInstances <= 1)
      {
        _sharedV = engine->AddShapeVertices<VD_Position_Normal_Tex0_Tex1>(src, *this, _dynamic, type == VBBigDiscardable, frequent);
      }
      else
      {
        _sharedV = engine->AddShapeVerticesInstanced<VD_Position_Normal_Tex0_Tex1>(src, *this, _dynamic, type == VBBigDiscardable, frequent, nInstances);
      }
      break;
    case VD_Position_Normal_Tex0_Tex1_ST:
      saturateMin (nInstances, engine->MaxInstances());
      if (nInstances <= 1)
      {
        _sharedV = engine->AddShapeVertices<VD_Position_Normal_Tex0_Tex1_ST>(src, *this, _dynamic, type == VBBigDiscardable, frequent);
      }
      else
      {
        _sharedV = engine->AddShapeVerticesInstanced<VD_Position_Normal_Tex0_Tex1_ST>(src, *this, _dynamic, type == VBBigDiscardable, frequent, nInstances);
      }
      break;
    case VD_Position_Normal_Tex0_ST_Float3:
      nInstances = 1;
      _sharedV = engine->AddShapeVertices<VD_Position_Normal_Tex0_ST_Float3>(src, *this, _dynamic, type == VBBigDiscardable, frequent);
      break;
    case VD_ShadowVolume:
      saturateMin(nInstances, engine->MaxInstancesShadowVolume());
      if (nInstances <= 1)
      {
        _sharedV = GEngineDD11->AddShapeVertices<VD_ShadowVolume>(src, *this, _dynamic, type == VBBigDiscardable, frequent);
      }
      else
      {
        _sharedV = GEngineDD11->AddShapeVerticesInstanced<VD_ShadowVolume>(src, *this, _dynamic, type == VBBigDiscardable, frequent, nInstances);
      }
      break;
    default:
      Fail("Unknown vertex declaration in the source shape");
    }
  }

  if (!_dynamic && !_sharedV)
  {
    return false;
  }

  // Finish in case of zero sections
  if (src.NSections() == 0)
  {
    return true;
  }

  if (src.GetVertexDeclaration() == VD_Tex0)
  {
    _sharedI = GEngineDD11->AddShapeIndicesSprite(src.Faces(), _dynamic, nInstances);
  }
  else if (src.GetVertexDeclaration() == VD_Position_Tex0_Color)
  {
    _sharedI = GEngineDD11->AddShapeIndicesPoint(src.Faces(), _dynamic, nInstances, src.NVertex());
  }
  else
  {
    _sharedI = GEngineDD11->AddShapeIndices(src.Faces(), _dynamic, nInstances, src.NVertex());
  }
  if (!_sharedI)
  {
    if (_sharedV)
    {
      GEngineDD11->DeleteVB(_sharedV);
      _sharedV = NULL;
    }
    return false;
  }

  PROFILE_SCOPE_DETAIL_EX(ddVIS,dd11);
  DoAssert ( (_sharedV || _dynamic) && _sharedI);
  // scan sections
  _sections.Realloc(src.NSections() * nInstances);
  _sections.Resize(src.NSections() * nInstances);

  // Array to remember start index of the section
  AutoArray<int,MemAllocLocal<int,64> > sectionStart;
  sectionStart.Realloc(src.NSections());
  sectionStart.Resize(src.NSections());

  int start = 0;
  Offset covered = Offset(0);

  // Go through all the sections
  for (int i=0; i<src.NSections(); i++)
  {
    const ShapeSection &sec = src.GetSection(i);

    // Set start back if there is a section referencing back
    if (sec.beg < covered)
    {
      // Find previous section with the same beg
      int s;
      for (s = 0; s < i; s++)
      {
        if (src.GetSection(s).beg == sec.beg)
        {
          break;
        }
      }

      // Make sure there was already section with the same beg and set start accordingly
      Assert(s < i);
      start = sectionStart[s];
    }

    if (sec.beg>covered)
    {
      // the copy may be skipping some sections
      // in such case we need to skip its indices as well
      // search which sections are skipped
      int skippedSize = 0;
      for (int s = 0; s < i; s++)
      {
        if (src.GetSection(s).beg >=covered && src.GetSection(s).end<=sec.beg)
        {
          Assert(nInstances==1);
          int dstSection = 0; 
          skippedSize += _sections[dstSection + s].end - _sections[dstSection + s].beg;
        }
      }
      start += skippedSize;
    }
    // Update the covered variable
    covered = sec.end;

    // Update sectionStart array
    sectionStart[i] = start;

    // Go through all the instances
    for (int instance = 0; instance < nInstances; instance++)
    {
      // calculate how much indices is used for this section
      int size = 0;
      // Minimum and maximum vertex index
      int minV = INT_MAX;
      int maxV = 0;

      for (Offset o=sec.beg; o<sec.end; src.NextFace(o))
      {
        const Poly &face = src.Face(o);
        if (face.N() < 3) continue;
        if  ((face.N() == 3) &&
          ((face.GetVertex(0) == face.GetVertex(1)) || 
          (face.GetVertex(1) == face.GetVertex(2)) ||
          (face.GetVertex(2) == face.GetVertex(0)))) continue;

        // check vertex indices used
        for (int vv=0; vv<face.N(); vv++)
        {
          int vi = face.GetVertex(vv);
          saturateMin(minV,vi);
          saturateMax(maxV,vi);
        }
        size += (face.N()-2)*3;
      }

      // In case of zero faces, consider the vertex buffer to belong to stars
      if (src.NFaces() == 0)
      {
        minV = 0;
        maxV = src.NVertex() * 4 - 1;
        size = src.NVertex() * 2 * 3;
      }

      // record section info
      int dstSection = src.NSections()*instance;
      _sections[dstSection + i].beg = start;
      _sections[dstSection + i].end = start + size;
#if V_OFFSET_CUSTOM
      _sections[dstSection + i].begVertex = minV;
      _sections[dstSection + i].endVertex = maxV + 1;
#else
      _sections[dstSection + i].begVertex = minV + vOffset;
      _sections[dstSection + i].endVertex = maxV + vOffset + 1;
#endif
      DoAssert(_sections[dstSection + i].endVertex >= _sections[dstSection + i].begVertex);

#if VERIFY_INDICES
      if (nInstances==1) // the following test is not valid for instanced data - offset would be needed
      {
        // verify the indices make sense
        int minVI,maxVI;
        _sharedI->ScanMinMax(minVI,maxVI,start,size);
        if (maxVI>minVI)
        {
          // check the whole section is the same as seen in the index buffer
          DoAssert(minV<=minVI);
          DoAssert(maxV>=maxVI);
          DoAssert(minV==minVI);
          DoAssert(maxV==maxVI);

          //_sections[dstSection + i].begVertex = minVI;
          //_sections[dstSection + i].endVertex = maxVI + 1;
        }
      }
#endif
      start += size;
    }
  }

  // index buffer remains constant - animation is done on vertices
  // or whole sections
  return true;

}

void VertexBufferD3D11::Update(int cb, const VertexTableAnimationContext *animContext, const Shape &src, const EngineShapeProperties &prop, VSDVersion vsdVersion)
{
  if (!_dynamic)
  {
    // use static buffer as source
    GEngineDD11->SetVSourceStatic(cb, src, prop, *this, vsdVersion);
  }
  else
  {
    // update dynamic buffer - and use it as source
    GEngineDD11->SetVSourceDynamic(cb,animContext, src, prop, *this);
  }

  // note: no need to set indices / vertices when drawing same shape again
  // this can be quite common

}

void VertexBufferD3D11::Detach(const VertexTable &src)
{
  DoAssert(GetOwner()==NULL || &src==GetOwner());
  // we need to detach destructed Shape, it can be accessed from _vBufferLRU if other reference to this persists
  SetOwner(NULL);
}

void VertexBufferD3D11::RemoveIndexBuffer()
{
  if (_sharedI)
  {
    GEngineDD11->DeleteIB(_sharedI);
    _sharedI.Free();
  }
}

bool VertexBufferD3D11::IsGeometryLoaded() const
{
  // dynamic buffers do not contain any geometry information
  return !_dynamic;
}

void VertexBufferD3D11::OnSizeChanged()
{
  GEngineDD11->OnSizeChanged(this);
}

///=========================================================
// AddShapeIndices...
///=========================================================

// predecessor of functors creating the index buffer
class AddShapeIndicesOpBase : public VirtDelegate0<void>
{
protected:
  Ref<const FaceData> _data;
  bool _dynamic;
  int _nInstances;

  Ref<IndexStaticData11> _result;

public:
  AddShapeIndicesOpBase(const FaceArray &src, bool dynamic, int nInstances)
    : _data(src.GetData()), _dynamic(dynamic), _nInstances(nInstances)
  {
  }
};

class AddShapeIndicesOp : public AddShapeIndicesOpBase
{
protected:
  typedef AddShapeIndicesOpBase base;
  int _instanceVertices;
  AutoArray<Offset> _sectionsEnds;

public:
  AddShapeIndicesOp(const FaceArray &src, bool dynamic, int nInstances, int instanceVertices)
    : base(src, dynamic, nInstances), _instanceVertices(instanceVertices)
  {
    // walking by sections is needed because of instancing - for instancing each section needs to be repeated
    // for this however it is enough to create a list of section boundaries

    // Normally the sections are ordered so the cur.end = next.beg
    // If not, sections are sharing triangles
    // - this is done for terrain mesh, which is sharing triangles and indices between sections
    // we avoid adding the same face twice
    Offset coveredSoFar = Offset(0);
    Assert(src.GetSection(0).beg==Offset(0));
    for (int s = 0; s < src.NSections(); s++)
    {
      const ShapeSection &sec = src.GetSection(s);
      // if the triangles from the section are already covered, do not convert them again
      if (sec.beg<coveredSoFar)
      {
        Assert(sec.end<=coveredSoFar);
        continue;
      }
      _sectionsEnds.Add(sec.end);
      coveredSoFar = sec.end;
    }
  }

  Ref<IndexStaticData11> CreateData(bool async)
  {
    // Get the number of indices
    int indicesTotal = IndexStaticData11::IndicesNeededTotal(*_data, _nInstances);
    // It's a nonsense to have zero indices
    Assert(indicesTotal > 0);

    _result = new IndexStaticData11(indicesTotal);
    if (async)
    {
      _result->InitFuture();
      return _result;
    }
    else
    {
      ComRef<IDirect3DIndexBuffer9Old> buffer = _result->CreateBuffer();
      // If we failed to create index buffer for the static data, then free static data and return error
      if (!buffer)
      {
        _result.Free();
        return NULL;
      }

      if (!_result->AddShape(buffer, *_data, indicesTotal, _dynamic, _nInstances, _instanceVertices, _sectionsEnds))
      {
        _result.Free();
        return NULL;
      }

      _result->Init(buffer);
      return _result;
    }
  }

  // Create the buffer when used asynchronously
  void operator () () const
  {
    PROFILE_SCOPE_EX(issCB,dd11);
    ComRef<IDirect3DIndexBuffer9Old> buffer = _result->CreateBuffer();

    if (buffer)
    {
      int indicesTotal = _result->NIndices();

      if (!_result->AddShape(buffer, *_data, indicesTotal, _dynamic, _nInstances, _instanceVertices, _sectionsEnds))
      {
        buffer.Free();
      }
    }

    // set the Future even when buffer == NULL, the failure will be detected this way
    _result->SetFuture(buffer);
  }
};

class AddShapeIndicesSpriteOp : public AddShapeIndicesOpBase
{
protected:
  typedef AddShapeIndicesOpBase base;

public:
  AddShapeIndicesSpriteOp(const FaceArray &src, bool dynamic, int nInstances)
    : base(src, dynamic, nInstances)
  {
  }

  Ref<IndexStaticData11> CreateData(bool async)
  {
    // Get the number of indices
    int indicesTotal = 6 * _nInstances;
    // It's a nonsense to have zero indices
    Assert(indicesTotal > 0);

    _result = new IndexStaticData11(indicesTotal);
    if (async)
    {
      _result->InitFuture();
      return _result;
    }
    else
    {
      ComRef<IDirect3DIndexBuffer9Old> buffer = _result->CreateBuffer();
      // If we failed to create index buffer for the static data, then free static data and return error
      if (!buffer)
      {
        _result.Free();
        return NULL;
      }

      if (!_result->AddShapeSprite(buffer, *_data, indicesTotal, _dynamic, _nInstances))
      {
        _result.Free();
        return NULL;
      }

      _result->Init(buffer);
      return _result;
    }
  }

  // Create the buffer when used asynchronously
  void operator () () const
  {
    PROFILE_SCOPE_EX(issCB,dd11);
    ComRef<IDirect3DIndexBuffer9Old> buffer = _result->CreateBuffer();

    if (buffer)
    {
      int indicesTotal = _result->NIndices();

      if (!_result->AddShapeSprite(buffer, *_data, indicesTotal, _dynamic, _nInstances))
      {
        buffer.Free();
      }
    }

    // set the Future even when buffer == NULL, the failure will be detected this way
    _result->SetFuture(buffer);
  }
};

class AddShapeIndicesPointOp : public AddShapeIndicesOpBase
{
protected:
  typedef AddShapeIndicesOpBase base;
  int _instanceVertices;

public:
  AddShapeIndicesPointOp(const FaceArray &src, bool dynamic, int nInstances, int instanceVertices)
    : base(src, dynamic, nInstances), _instanceVertices(instanceVertices)
  {
  }

  Ref<IndexStaticData11> CreateData(bool async)
  {
    // Get the number of indices
    int indicesTotal = 6 * _instanceVertices;
    // It's a nonsense to have zero indices
    Assert(indicesTotal > 0);

    _result = new IndexStaticData11(indicesTotal);
    if (async)
    {
      _result->InitFuture();
      return _result;
    }
    else
    {
      ComRef<IDirect3DIndexBuffer9Old> buffer = _result->CreateBuffer();
      // If we failed to create index buffer for the static data, then free static data and return error
      if (!buffer)
      {
        _result.Free();
        return NULL;
      }

      if (!_result->AddShapePoint(buffer, *_data, indicesTotal, _dynamic, _nInstances, _instanceVertices))
      {
        _result.Free();
        return NULL;
      }

      _result->Init(buffer);
      return _result;
    }
  }

  // Create the buffer when used asynchronously
  void operator () () const
  {
    PROFILE_SCOPE_EX(issCB,dd11);
    ComRef<IDirect3DIndexBuffer9Old> buffer = _result->CreateBuffer();

    if (buffer)
    {
      int indicesTotal = _result->NIndices();

      if (!_result->AddShapePoint(buffer, *_data, indicesTotal, _dynamic, _nInstances, _instanceVertices))
      {
        buffer.Free();
      }
    }

    // set the Future even when buffer == NULL, the failure will be detected this way
    _result->SetFuture(buffer);
  }
};

///=========================================================
// Implementation of VertexStaticData11
///=========================================================
/*!
\param vertices Required number of vertices in the buffer
\param vertexSize Required size of the buffer in bytes
\param frequent indicated high performance is preferred (VRAM instead of AGP)
*/
VertexStaticData11::VertexStaticData11(int vertices, int vertexSize, bool frequent)
{
  _vertexSize = vertexSize;
  _count = vertices;
  _usageDynamic = false;

  if (_count > 0)
  {
    _usageDynamic = !frequent && !GEngineDD11->GetVBuffersStatic();
#if _ENABLE_CHEATS
    if (ForceStaticVB) _usageDynamic = false;
#endif

    GEngineDD11->AddVBuffersAllocated(_count*_vertexSize,_usageDynamic);
  }
}

VertexStaticData11::~VertexStaticData11()
{
  Dealloc();
  GEngineDD11->AddVBuffersAllocated(-_count*_vertexSize,_usageDynamic);
}


void VertexStaticData11::Init(ComRef<IDirect3DVertexBuffer9Old> buffer
#if _ARMA3_VERTEX_BUFFER_EX
                             , int numVertices
#endif
                             )
{
#if _ARMA3_VERTEX_BUFFER_EX
  _vBuffer = BufferFuture(VertexBuffer11Ex(buffer, numVertices));
#else
  _vBuffer = BufferFuture(buffer);
#endif
}

void VertexStaticData11::InitFuture()
{
  _vBuffer = BufferFuture();
}

void VertexStaticData11::SetFuture(ComRef<IDirect3DVertexBuffer9Old> buffer
#if _ARMA3_VERTEX_BUFFER_EX
                                  , int numVertices
#endif
                                  )
{
#if _ARMA3_VERTEX_BUFFER_EX
  _vBuffer.SetResult(VertexBuffer11Ex(buffer, numVertices));
#else
  _vBuffer.SetResult(buffer);
#endif
}

ComRef<IDirect3DVertexBuffer9Old> VertexStaticData11::CreateBuffer()
{
  ComRef<IDirect3DVertexBuffer9Old> buffer;

  if (_count <= 0) return buffer;

  MyD3DDeviceCB &dev = GEngineDD11->GetD3DDeviceRec();
  int byteSize = _count*_vertexSize;

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
  PROFILE_SCOPE_DETAIL_EX(ddCVB,dd11);
  EngineDD11::CheckMemTypeScope scope(GEngineDD11,_usageDynamic ? "VBdynamic" : "VBstatic",byteSize);
  HRESULT err = dev->CreateBuffer(&desc, NULL, buffer.Init());
  if (err!=D3D_OK)
  {
    if (err==E_OUTOFMEMORY)
    {
      GEngineDD11->DDError11("CreateVertexBuffer - static",err);
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
    ReportMemoryStatusWithVRAM();
    return buffer;
  }
  DoAssert(buffer);
  return buffer;
}

#define V_OFFSET_CUSTOM 1



void VertexStaticData11::Dealloc()
{
#ifdef _XBOX
  ComRef<IDirect3DVertexBuffer9> buffer = _vBuffer.GetResult();
  if (buffer)
  {
    if (buffer->IsBusy())
    {
      LOG_WAIT_GPU("vertex buffer",buffer);
      buffer->BlockUntilNotBusy();
    }
    buffer.Free();
  }
#endif
}

void *VertexStaticData11::Lock(ComRef<IDirect3DVertexBuffer9Old> buffer, int size)
{
RetryV:
  DoAssert(GEngineDD11);
  if (!GEngineDD11)
    return 0;

  void *data = NULL;

  HRESULT err = GEngineDD11->Map(buffer, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &data);
  if (err!=D3D_OK || !data)
  {
    if (GEngineDD11->FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto RetryV;
    ErrF("VB Lock failed, %x,%s",err,DXGetErrorString(err));
    ReportMemoryStatusWithVRAM();
    return NULL;
  }
  return data;
}

void VertexStaticData11::Unlock(ComRef<IDirect3DVertexBuffer9Old> buffer)
{
  DoAssert(GEngineDD11);
  if (GEngineDD11)
    GEngineDD11->Unmap(buffer, 0);
}
template <>
void VertexStaticData11::AddShapeData<VD_Position_Normal_Tex0>(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0>::VertexType VertexType;

  VertexType *sData = (VertexType *)data;
  const UVPairs *uv = &src.GetUVArray();
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3Compressed> &norm = src.GetNormArray();
  int nVertex = src.NVertex();
  for (int i = 0; i < nVertex; i++)
  {
    sData->pos = ConvertToP(pos[i]);
    CopyNormal(sData->norm, norm[i]);
    sData->t0 = (*uv).GetUVRaw(i);
    sData++;
  }
}

template <>
void VertexStaticData11::AddShapeData<VD_Position_Normal_Tex0_ST>(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0_ST>::VertexType VertexType;

  const UVPairs *uv = &src.GetUVArray();
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const AutoArray<STPair> &stData = src.GetSTArray();
  const CondensedAutoArray<Vector3Compressed> &norm = src.GetNormArray();

  // If ST is present in the source, then use it, else generate it
  if (src.IsSTPresent())
  {
    VertexType *sData = (VertexType*)data;
    const STPair *st = &stData[0];
    int nVertex = src.NVertex();
    for (int i = 0; i < nVertex; i++)
    {
      sData->pos = ConvertToP(pos[i]);
      CopyNormal(sData->norm, norm[i]);
      sData->t0 = (*uv).GetUVRaw(i);
      CopyNormal(sData->s, st->s);
      CopyNormal(sData->t, st->t);
      sData++;
      st++;
    }
  }
  else
  {
    ErrF("Generating ST on the fly is very slow - %s",GETREPORTSTACK());

    // Copy at least some reasonable values into the VB
    VertexType *sData = (VertexType*)data;
    int nVertex = src.NVertex();
    for (int i = 0; i < nVertex; i++)
    {
      sData->pos = ConvertToP(pos[i]);
      CopyNormal(sData->norm, norm[i]);
      sData->t0 = (*uv).GetUVRaw(i);
      CopyNormal(sData->s, norm[i]);
      CopyNormal(sData->t, norm[i]);
      sData++;
    }
  }
}

template <>
void VertexStaticData11::AddShapeData<VD_Position_Normal_Tex0_Tex1>(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0_Tex1>::VertexType VertexType;

  VertexType *sData = (VertexType *)data;
  const UVPairs *uv = &src.GetUVArray();
  const UVPairs *uv1 = &src.GetUVArray(1);
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3Compressed> &norm = src.GetNormArray();

  const UVPair *minUV1 = &vbuf->_minUV[1];
  const UVPair *maxUV1 = &vbuf->_maxUV[1];
  if (uv1->Size()!=src.NVertex())
  {
    ErrF("2nd UV set needed, but not defined in %s",GETREPORTSTACK());
    uv1 = uv;
    minUV1 = &vbuf->_minUV[0];
    maxUV1 = &vbuf->_maxUV[0];
  }

  int nVertex = src.NVertex();
  for (int i = 0; i < nVertex; i++)
  {
    sData->pos = ConvertToP(pos[i]);
    CopyNormal(sData->norm, norm[i]);
    sData->t0 = (*uv).GetUVRaw(i);
    sData->t1 = (*uv1).GetUVRaw(i);
    sData++;
  }
}

template <>
void VertexStaticData11::AddShapeData<VD_Position_Normal_Tex0_Tex1_ST>(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0_Tex1_ST>::VertexType VertexType;

  const UVPairs *uv = &src.GetUVArray();
  const UVPairs *uv1 = &src.GetUVArray(1);
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3Compressed> &norm = src.GetNormArray();

  // If ST is present in the source, then use it, else generate it
  if (src.IsSTPresent())
  {
    VertexType *sData = (VertexType*)data;
    const AutoArray<STPair> &stData = src.GetSTArray();
    const STPair *st = &stData[0];
    int nVertex = src.NVertex();
    for (int i = 0; i < nVertex; i++)
    {
      sData->pos = ConvertToP(pos[i]);
      CopyNormal(sData->norm, norm[i]);
      sData->t0 = (*uv).GetUVRaw(i);
      sData->t1 = (*uv1).GetUVRaw(i);
      CopyNormal(sData->s, st->s);
      CopyNormal(sData->t, st->t);
      sData++;
      st++;
    }
  }
  else
  {
    Fail("Generating ST on the fly is very slow");

    // Copy at least some reasonable values into the VB
    VertexType *sData = (VertexType*)data;
    int nVertex = src.NVertex();
    for (int i = 0; i < nVertex; i++)
    {
      sData->pos = ConvertToP(pos[i]);
      CopyNormal(sData->norm, norm[i]);
      sData->t0 = (*uv).GetUVRaw(i);
      sData->t1 = (*uv1).GetUVRaw(i);
      CopyNormal(sData->s, norm[i]);
      CopyNormal(sData->t, norm[i]);
      sData++;
    }
  }
}

template <>
void VertexStaticData11::AddShapeData<VD_Position_Normal_Tex0_ST_Float3>(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0_ST_Float3>::VertexType VertexType;

  VertexType *sData = (VertexType*)data;
  const UVPairs *uv = &src.GetUVArray();
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3Compressed> &norm = src.GetNormArray();

  DoAssert (src.IsSTPresent());
  DoAssert (src.IsCustomPresent());
  sData = (VertexType *)data;
#if _VBS3_MORETERRAINLODS
  Assert(VertexCustomItems == 7); // as shaders are tuned to such amount of LODs now (see custom and lod36 parameters and their usage in VTransformLodEx())
  const VertexCustomInfo *custom = src.GetCustomArray().Data();
#else
  const Vector3 *custom = src.GetCustomArray().Data();
#endif
  const AutoArray<STPair> &stData = src.GetSTArray();
  const STPair *st = &stData[0];

  int nVertex = src.NVertex();
  for (int i = 0; i < nVertex; i++)
  {
    sData->pos = ConvertToP(pos[i]);
    CopyNormal(sData->norm, norm[i]);
    sData->t0 = (*uv).GetUVRaw(i);
    CopyNormal(sData->s, st->s);
    CopyNormal(sData->t, st->t);

#if _VBS3_LAND_SHADOW
    sData->landShadow = custom->_landShadow;
#endif

#if _VBS3_MORETERRAINLODS
    sData->custom = Vector3(custom->_item[1], custom->_item[2], custom->_item[0]);
    sData->lod36[0] = custom->_item[3];
    sData->lod36[1] = custom->_item[4];
    sData->lod36[2] = custom->_item[5];
    sData->lod36[3] = custom->_item[6];
#if _VBS3_CPU_LAND_LIGHT
    sData->light = custom->_light;
#endif
#else
    sData->custom = ConvertToP(*custom);
#endif

    custom++;
    sData++;
    st++;
  }
}

template <>
void VertexStaticData11::AddShapeData<VD_ShadowVolume>(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf)
{
  typedef VertexDeclTraits<VD_ShadowVolume>::VertexType VertexType;

  VertexType *sData = (VertexType*)data;
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3Compressed> &norm = src.GetNormArray();

  int nVertex = src.NVertex();
  for (int i = 0; i < nVertex; i++)
  {
    sData->pos = ConvertToP(pos[i]);
    CopyNormal(sData->norm, norm[i]);
    sData++;
  }
}

template <>
void VertexStaticData11::AddShapeData<VD_Position_Normal_Tex0_WeightsAndIndices>(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0_WeightsAndIndices>::VertexType VertexType;

  // Copy position, normal and UV
  VertexType *sData = (VertexType *)data;
  const UVPairs *uv = &src.GetUVArray();
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3Compressed> &norm = src.GetNormArray();

  // Copy weights and indices
  const AutoArray<AnimationRTWeight> &a = src.GetVertexBoneRef();

  if (a.Size() > 0)
  {
    int nVertex = src.NVertex();
    for (int i = 0; i < nVertex; i++)
    {
      sData[i].pos = ConvertToP(pos[i]);
      CopyNormal(sData[i].norm, norm[i]);
      sData[i].t0 = (*uv).GetUVRaw(i);
      FillWeightsAndIndices(sData[i].weights,sData[i].indices,a[i]);
    }
  }
  else
  {
    RptF("Warning: Object is animated, but no skeleton is defined");
    for (int i=src.NVertex(); --i>=0;)
    {
      sData[i].pos = ConvertToP(pos[i]);
      CopyNormal(sData[i].norm, norm[i]);
      sData[i].t0 = (*uv).GetUVRaw(i);
      sData[i].indices[0] = 0;
      sData[i].weights[0] = 0;
      sData[i].indices[1] = sData[i].indices[2] = sData[i].indices[3] = 0;
      sData[i].weights[1] = sData[i].weights[2] = sData[i].weights[3] = 0;
    }
  }
}

template <>
void VertexStaticData11::AddShapeData<VD_Position_Normal_Tex0_ST_WeightsAndIndices>(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0_ST_WeightsAndIndices>::VertexType VertexType;

  // Copy position, normal and UV
  VertexType *sData = (VertexType *)data;
  const UVPairs *uv = &src.GetUVArray();
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3Compressed> &norm = src.GetNormArray();

  const AutoArray<AnimationRTWeight> &a = src.GetVertexBoneRef();

  // If ST is present in the source, then use it, else generate it
  if (src.IsSTPresent())
  {
    const AutoArray<STPair> &stData = src.GetSTArray();
    const STPair *st = &stData[0];
    if (a.Size() > 0)
    {
      int nVertex = src.NVertex();
      for (int i = 0; i < nVertex; i++)
      {
        sData[i].pos = ConvertToP(pos[i]);
        CopyNormal(sData[i].norm, norm[i]);
        sData[i].t0 = (*uv).GetUVRaw(i);
        CopyNormal(sData[i].s, st->s);
        CopyNormal(sData[i].t, st->t);
        FillWeightsAndIndices(sData[i].weights,sData[i].indices,a[i]);
        st++;
      }
    }
    else
    {
      RptF("Warning: Object is animated, but no skeleton is defined");
      int nVertex = src.NVertex();
      for (int i = 0; i < nVertex; i++)
      {
        sData[i].pos = ConvertToP(pos[i]);
        CopyNormal(sData[i].norm, norm[i]);
        sData[i].t0 = (*uv).GetUVRaw(i);
        sData[i].indices[0] = 0;
        sData[i].weights[0] = 0;
        sData[i].indices[1] = sData[i].indices[2] = sData[i].indices[3] = 0;
        sData[i].weights[1] = sData[i].weights[2] = sData[i].weights[3] = 0;
        CopyNormal(sData[i].s, st->s);
        CopyNormal(sData[i].t, st->t);
        st++;
      }
    }
  }
  else
  {
    Fail("Generating ST on the fly is very slow");

    // Copy at least some reasonable values into the VB
    if (a.Size() > 0)
    {
      int nVertex = src.NVertex();
      for (int i = 0; i < nVertex; i++)
      {
        sData[i].pos = ConvertToP(pos[i]);
        CopyNormal(sData[i].norm, norm[i]);
        sData[i].t0 = (*uv).GetUVRaw(i);
        FillWeightsAndIndices(sData[i].weights,sData[i].indices,a[i]);
        CopyNormal(sData[i].s, norm[i]);
        CopyNormal(sData[i].t, norm[i]);
      }
    }
    else
    {
      RptF("Warning: Object is animated, but no skeleton is defined");
      for (int i=src.NVertex(); --i>=0;)
      {
        sData[i].pos = ConvertToP(pos[i]);
        CopyNormal(sData[i].norm, norm[i]);
        sData[i].t0 = (*uv).GetUVRaw(i);
        sData[i].indices[0] = 0;
        sData[i].weights[0] = 0;
        sData[i].indices[1] = sData[i].indices[2] = sData[i].indices[3] = 0;
        sData[i].weights[1] = sData[i].weights[2] = sData[i].weights[3] = 0;
        CopyNormal(sData[i].s, norm[i]);
        CopyNormal(sData[i].t, norm[i]);
      }
    }
  }
}

template <>
void VertexStaticData11::AddShapeData<VD_Position_Normal_Tex0_Tex1_WeightsAndIndices>(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0_Tex1_WeightsAndIndices>::VertexType VertexType;

  // Copy position, normal and UV
  VertexType *sData = (VertexType *)data;
  const UVPairs *uv = &src.GetUVArray();
  const UVPairs *uv1 = &src.GetUVArray(1);
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3Compressed> &norm = src.GetNormArray();

  // Copy weights and indices
  const AutoArray<AnimationRTWeight> &a = src.GetVertexBoneRef();

  if (a.Size() > 0)
  {
    int nVertex = src.NVertex();
    for (int i = 0; i < nVertex; i++)
    {
      sData[i].pos = ConvertToP(pos[i]);
      CopyNormal(sData[i].norm, norm[i]);
      sData[i].t0 = (*uv).GetUVRaw(i);
      sData[i].t1 = (*uv1).GetUVRaw(i);

      int wsize = a[i].Size();
      // Copy indices and weights
      for (int w = 0; w < wsize; w++)
      {
        sData[i].indices[w] = GetSubSkeletonIndex(a[i][w].GetSel());
        sData[i].weights[w] = a[i][w].GetWeight8b();
      }
      // Zero remaining indices and weights
      for (int w = wsize; w < 4; w++)
      {
        sData[i].indices[w] = 0;
        sData[i].weights[w] = 0;
      }
    }
  }
  else
  {
    RptF("Warning: Object is animated, but no skeleton is defined");
    for (int i=src.NVertex(); --i>=0;)
    {
      sData[i].pos = ConvertToP(pos[i]);
      CopyNormal(sData[i].norm, norm[i]);
      sData[i].t0 = (*uv).GetUVRaw(i);
      sData[i].t1 = (*uv1).GetUVRaw(i);
      sData[i].indices[0] = 0;
      sData[i].weights[0] = 0;
      sData[i].indices[1] = sData[i].indices[2] = sData[i].indices[3] = 0;
      sData[i].weights[1] = sData[i].weights[2] = sData[i].weights[3] = 0;
    }
  }
}

template <>
void VertexStaticData11::AddShapeData<VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices>(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices>::VertexType VertexType;

  // Copy position, normal and UV
  VertexType *sData = (VertexType *)data;
  const UVPairs *uv = &src.GetUVArray();
  const UVPairs *uv1 = &src.GetUVArray(1);
  const UVPair *minUV1 = &vbuf->_minUV[1];
  const UVPair *maxUV1 = &vbuf->_maxUV[1];
  if (uv1->Size()!=src.NVertex())
  {
    ErrF("2nd UV set needed, but not defined in %s",GETREPORTSTACK());
    uv1 = uv;
    minUV1 = &vbuf->_minUV[0];
    maxUV1 = &vbuf->_maxUV[0];
  }
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3Compressed> &norm = src.GetNormArray();

  const AutoArray<AnimationRTWeight> &a = src.GetVertexBoneRef();

  // If ST is present in the source, then use it, else generate it
  if (src.IsSTPresent())
  {
    const AutoArray<STPair> &stData = src.GetSTArray();
    const STPair *st = &stData[0];
    if (a.Size() > 0)
    {
      int nVertex = src.NVertex();
      for (int i = 0; i < nVertex; i++)
      {
        sData[i].pos = ConvertToP(pos[i]);
        CopyNormal(sData[i].norm, norm[i]);
        sData[i].t0 = (*uv).GetUVRaw(i);
        sData[i].t1 = (*uv1).GetUVRaw(i);
        CopyNormal(sData[i].s, st->s);
        CopyNormal(sData[i].t, st->t);
        // Copy indices and weights
        FillWeightsAndIndices(sData[i].weights,sData[i].indices,a[i]);
        st++;
      }
    }
    else
    {
      RptF("Warning: Object is animated, but no skeleton is defined");
      int nVertex = src.NVertex();
      for (int i = 0; i < nVertex; i++)
      {
        sData[i].pos = ConvertToP(pos[i]);
        CopyNormal(sData[i].norm, norm[i]);
        sData[i].t0 = (*uv).GetUVRaw(i);
        sData[i].t1 = (*uv1).GetUVRaw(i);
        sData[i].indices[0] = 0;
        sData[i].weights[0] = 0;
        sData[i].indices[1] = sData[i].indices[2] = sData[i].indices[3] = 0;
        sData[i].weights[1] = sData[i].weights[2] = sData[i].weights[3] = 0;
        CopyNormal(sData[i].s, st->s);
        CopyNormal(sData[i].t, st->t);
        st++;
      }
    }
  }
  else
  {
    Fail("Generating ST on the fly is very slow");

    // Copy at least some reasonable values into the VB
    if (a.Size() > 0)
    {
      int nVertex = src.NVertex();
      for (int i = 0; i < nVertex; i++)
      {
        sData[i].pos = ConvertToP(pos[i]);
        CopyNormal(sData[i].norm, norm[i]);
        sData[i].t0 = (*uv).GetUVRaw(i);
        sData[i].t1 = (*uv1).GetUVRaw(i);
        int wsize = a[i].Size();

        // Copy indices and weights
        for (int w = 0; w < wsize; w++)
        {
          sData[i].indices[w] = GetSubSkeletonIndex(a[i][w].GetSel());
          sData[i].weights[w] = a[i][w].GetWeight8b();
        }
        // Zero remaining indices and weights
        for (int w = wsize; w < 4; w++)
        {
          sData[i].indices[w] = 0;
          sData[i].weights[w] = 0;
        }
        CopyNormal(sData[i].s, norm[i]);
        CopyNormal(sData[i].t, norm[i]);
      }
    }
    else
    {
      RptF("Warning: Object is animated, but no skeleton is defined");
      for (int i=src.NVertex(); --i>=0;)
      {
        sData[i].pos = ConvertToP(pos[i]);
        CopyNormal(sData[i].norm, norm[i]);
        sData[i].t0 = (*uv).GetUVRaw(i);
        sData[i].t1 = (*uv1).GetUVRaw(i);
        sData[i].indices[0] = 0;
        sData[i].weights[0] = 0;
        sData[i].indices[1] = sData[i].indices[2] = sData[i].indices[3] = 0;
        sData[i].weights[1] = sData[i].weights[2] = sData[i].weights[3] = 0;
        CopyNormal(sData[i].s, norm[i]);
        CopyNormal(sData[i].t, norm[i]);
      }
    }
  }
}

template <>
void VertexStaticData11::AddShapeData<VD_ShadowVolumeSkinned>(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf)
{
  typedef VertexDeclTraits<VD_ShadowVolumeSkinned>::VertexType VertexType;

  VertexType *sData = (VertexType*)data;
  const Vector3 *pos = src.GetPosArray().Data();
  const AutoArray<AnimationRTWeight> &a = src.GetVertexBoneRef();

  if (a.Size() > 0)
  {
    const AutoArray<VertexNeighborInfo> &neighborBoneRef = src.GetNeighborBoneRef();
    int nVertex = src.NVertex();
    for (int i = 0; i < nVertex; i++)
    {
      // Original vertex
      sData[i].pos = ConvertToP(pos[i]);
      FillWeightsAndIndices(sData[i].weights,sData[i].indices,a[i]);

      VertexIndex aIndex = neighborBoneRef[i]._posA;
      if ((aIndex >= 0) && (aIndex < nVertex))
      {
        sData[i].posA = ConvertToP(pos[aIndex]);
      }
      else
      {
        Fail("Error: Neighbour index out of bounds");
      }
      FillWeightsAndIndices(sData[i].weightsA,sData[i].indicesA,neighborBoneRef[i]._rtwA);

      VertexIndex bIndex = neighborBoneRef[i]._posB;
      if ((bIndex >= 0) && (bIndex < nVertex))
      {
        sData[i].posB = ConvertToP(pos[bIndex]);
      }
      else
      {
        Fail("Error: Neighbour index out of bounds");
      }

      FillWeightsAndIndices(sData[i].weightsB,sData[i].indicesB,neighborBoneRef[i]._rtwB);
    }
  }
  else
  {
    Fail("Warning: Object is animated, but no skeleton is defined");
    // Go through all the sections
    int nVertex = src.NVertex();
    for (int i = 0; i < nVertex; i++)
    {
      // Original vertex
      sData[i].pos = ConvertToP(pos[i]);
      FillWeightsAndIndices(sData[i].weights,sData[i].indices,a[i]);
      sData[i].posA = ConvertToP(pos[i]);
      FillWeightsAndIndices(sData[i].weightsA,sData[i].indices,a[i]);
      sData[i].posB = ConvertToP(pos[i]);
      FillWeightsAndIndices(sData[i].weightsB,sData[i].indices,a[i]);
    }
  }
}

template <int vertexDecl, class VertexType>
class AddShapeOpBase : public VirtDelegate0<void>
{
protected:
  mutable Ref<VertexStaticData11> _result;

  Ref<const VertexTableData> _data;
  Ref<VertexBufferD3D11> _vbuf;
  bool _frequent;
  bool _dynamic;

protected:
  AddShapeOpBase(const VertexTable &src, VertexBufferD3D11 *vbuf, bool frequent, bool dynamic)
    :_data(src.GetVertexTableData()),_vbuf(vbuf),_frequent(frequent),_dynamic(dynamic)
  {
    vbuf->PrepareUV<vertexDecl>(src);
  }

};

template <int vertexDecl>
class AddShapeInstancedOp: public AddShapeOpBase<vertexDecl, typename VertexDeclInstancedTraits<vertexDecl>::VertexType >
{
  typedef AddShapeOpBase<vertexDecl, typename VertexDeclInstancedTraits<vertexDecl>::VertexType > base;

  int _instancesCount;

public:
  AddShapeInstancedOp(const VertexTable &src, VertexBufferD3D11 *vbuf, int instancesCount, bool frequent, bool dynamic)
    :base(src,vbuf,frequent,dynamic),_instancesCount(instancesCount)
  {
  }

  Ref<VertexStaticData11> CreateData(bool async)
  {
    Assert(!_dynamic); // dynamic data are never instanced

    // Either we didn't fit into existing or we use a separate buffer, so create a new buffer
    _result = new VertexStaticData11(_data->NVertex() * _instancesCount, sizeof(VertexDeclInstancedTraits<vertexDecl>::VertexType), _frequent);

    if (async)
    {
      _result->InitFuture();
      return _result;
    }
    else
    {
      ComRef<IDirect3DVertexBuffer9Old> buffer = CreateBuffer();
      // If we failed to init vertex buffer for the static data, then free static data and return error
      if (!buffer)
      {
        _result.Free();
        return NULL;
      }

      _result->Init(buffer);
      return _result;
    }
  }

  // Create the buffer when used asynchronously
  void operator () () const
  {
    PROFILE_SCOPE_EX(vssCB,dd11);
    ComRef<IDirect3DVertexBuffer9Old> buffer = CreateBuffer();
    _result->SetFuture(buffer);
  }

protected:
  ComRef<IDirect3DVertexBuffer9Old> CreateBuffer() const
  {
    ComRef<IDirect3DVertexBuffer9Old> buffer = _result->CreateBuffer();
    if (!buffer)
    {
      buffer.Free();
      return buffer;
    }

    PROFILE_DX_SCOPE(3dvbC);
    void *dataMem = _result->Lock(buffer, _result->Size() * _result->VertexSize());
    if (!dataMem)
    {
      buffer.Free();
      return buffer;
    }

    // Fill out the particular data
    VertexStaticData11::AddShapeInstancedData<vertexDecl>(dataMem, *_data, _vbuf, _instancesCount);

    // Process the data (Unlock)
    GEngineDD11->Unmap(buffer, 0);

    return buffer;
  }
};

template <>
void VertexStaticData11::AddShapeInstancedData<VD_Position_Normal_Tex0>(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf, int instancesCount)
{
  typedef VertexDeclInstancedTraits<VD_Position_Normal_Tex0>::VertexType VertexType;

  // Copy position, normal and UV
  VertexType *sData = (VertexType *)data;
  const UVPairs *uv = &src.GetUVArray();
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3Compressed> &norm = src.GetNormArray();

  int nVertex = src.NVertex();
  for (int instance = 0; instance < instancesCount; instance++)
  {
    for (int i = 0; i < nVertex; i++)
    {
      sData->pos = ConvertToP(pos[i]);
      CopyNormal(sData->norm, norm[i]);
      sData->t0 = (*uv).GetUVRaw(i);
      for (int j = 0; j < 4; j++) sData->index[j] = instance;
      sData++;
    }
  }
}

template <>
void VertexStaticData11::AddShapeInstancedData<VD_Position_Normal_Tex0_ST>(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf, int instancesCount)
{
  typedef VertexDeclInstancedTraits<VD_Position_Normal_Tex0_ST>::VertexType VertexType;

  // Copy position, normal and UV
  const UVPairs *uv = &src.GetUVArray();
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3Compressed> &norm = src.GetNormArray();

  // If ST is present in the source, then use it, else generate it
  if (src.IsSTPresent())
  {
    VertexType *sData = (VertexType*)data;
    int nVertex = src.NVertex();
    for (int instance = 0; instance < instancesCount; instance++)
    {
      const AutoArray<STPair> &stData = src.GetSTArray();
      const STPair *st = &stData[0];
      for (int i = 0; i < nVertex; i++)
      {
        sData->pos = ConvertToP(pos[i]);
        CopyNormal(sData->norm, norm[i]);
        sData->t0 = (*uv).GetUVRaw(i);
        CopyNormal(sData->s, st->s);
        CopyNormal(sData->t, st->t);
        sData->index[0] = sData->index[1] = 
          sData->index[2] = sData->index[3] = instance;
        sData++;
        st++;
      }
    }
  }
  else
  {
    Fail("Error: ST shouldn't be calculated anymore");
  }
}

template <>
void VertexStaticData11::AddShapeInstancedData<VD_Position_Normal_Tex0_Tex1>(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf, int instancesCount)
{
  typedef VertexDeclInstancedTraits<VD_Position_Normal_Tex0_Tex1>::VertexType VertexType;

  // Copy position, normal and UV
  VertexType *sData = (VertexType *)data;
  const UVPairs *uv = &src.GetUVArray();
  const UVPairs *uv1 = &src.GetUVArray(1);
  const UVPair *minUV1 = &vbuf->_minUV[1];
  const UVPair *maxUV1 = &vbuf->_maxUV[1];
  if (uv1->Size()!=src.NVertex())
  {
    ErrF("2nd UV set needed, but not defined in %s",GETREPORTSTACK());
    uv1 = uv;
    minUV1 = &vbuf->_minUV[0];
    maxUV1 = &vbuf->_maxUV[0];
  }
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3Compressed> &norm = src.GetNormArray();

  int nVertex = src.NVertex();
  for (int instance = 0; instance < instancesCount; instance++)
  {
    for (int i = 0; i < nVertex; i++)
    {
      sData->pos = ConvertToP(pos[i]);
      CopyNormal(sData->norm, norm[i]);
      sData->t0 = (*uv).GetUVRaw(i);
      sData->t1 = (*uv).GetUVRaw(i);
      for (int j = 0; j < 4; j++) sData->index[j] = instance;
      sData++;
    }
  }
}

template <>
void VertexStaticData11::AddShapeInstancedData<VD_Position_Normal_Tex0_Tex1_ST>(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf, int instancesCount)
{
  typedef VertexDeclInstancedTraits<VD_Position_Normal_Tex0_Tex1_ST>::VertexType VertexType;

  // Copy position, normal and UV
  const UVPairs *uv = &src.GetUVArray();
  const UVPairs *uv1 = &src.GetUVArray(1);

  const UVPair *minUV1 = &vbuf->_minUV[1];
  const UVPair *maxUV1 = &vbuf->_maxUV[1];
  if (uv1->Size()!=src.NVertex())
  {
    ErrF("2nd UV set needed, but not defined in %s",GETREPORTSTACK());
    uv1 = uv;
    minUV1 = &vbuf->_minUV[0];
    maxUV1 = &vbuf->_maxUV[0];
  }


  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3Compressed> &norm = src.GetNormArray();

  // If ST is present in the source, then use it, else generate it
  if (src.IsSTPresent())
  {
    VertexType *sData = (VertexType*)data;
    int nVertex = src.NVertex();
    for (int instance = 0; instance < instancesCount; instance++)
    {
      const AutoArray<STPair> &stData = src.GetSTArray();
      const STPair *st = &stData[0];
      for (int i = 0; i < nVertex; i++)
      {
        sData->pos = ConvertToP(pos[i]);
        CopyNormal(sData->norm, norm[i]);
        sData->t0 = (*uv).GetUVRaw(i);
        sData->t1 = (*uv1).GetUVRaw(i);

        CopyNormal(sData->s, st->s);
        CopyNormal(sData->t, st->t);
        sData->index[0] = sData->index[1] = sData->index[2] = sData->index[3] = instance;
        sData++;
        st++;
      }
    }
  }
  else
  {
    Fail("Error: ST shouldn't be calculated anymore");
  }
}

template <>
void VertexStaticData11::AddShapeInstancedData<VD_ShadowVolume>(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf, int instancesCount)
{
  typedef VertexDeclInstancedTraits<VD_ShadowVolume>::VertexType VertexType;

  VertexType *sData = (VertexType*)data;
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3Compressed> &norm = src.GetNormArray();

  for (int instance = 0; instance < instancesCount; instance++)
  {
    int nVertex = src.NVertex();
    for (int i = 0; i < nVertex; i++)
    {
      sData->pos = ConvertToP(pos[i]);
      CopyNormal(sData->norm, norm[i]);
      for (int j = 0; j < 4; j++) sData->index[j] = instance;
      sData++;
    }
  }
}

void VertexStaticData11::AddShapeSpriteInstanced(void *data, const VertexTableData &src, int instancesCount)
{
  SVertexSpriteInstanced9 *sData = (SVertexSpriteInstanced9 *)data;
  // prepare components so that we can OR them together
  // x86 byte layout: RGBA, i.e (R<<0)|(G<<8)|(B<<16)|(A<<24)
  // U into R/X, V into R/Y, instance index into B/Z
  const DWORD u1 = 255,u0 = 0;
  const DWORD v1 = 255<<8,v0 = 0;

  for (int instance = 0; instance < instancesCount; instance++)
  {
    DWORD index = instance<<16;
    // XYZW = UVI0
    sData[instance * 4 + 0].encoded = index|u0|v0;
    sData[instance * 4 + 1].encoded = index|u0|v1;
    sData[instance * 4 + 2].encoded = index|u1|v0;
    sData[instance * 4 + 3].encoded = index|u1|v1;
  }

  REPORT_ADDSHAPE
}

void VertexStaticData11::AddShapePoint(void *data, const VertexTableData &src)
{
  // when adding dynamic data, add only indices
  SVertexPoint9 *sData = (SVertexPoint9 *)data;
  const AutoArray<Vector3> &pos = src.GetPosArray();

  int nVertex = src.NVertex();
  for (int i = 0; i < nVertex; i++)
  {
    // use different brightness for different stars
    ClipFlags clip=src.Clip(i);
    int user=clip&ClipUserMask;
    float brightness=user*(1.0f/MaxUserValue/ClipUserStep);

    int index = i * 4;

    int color = FAST_D3DRGBA_SAT(0,0,0,brightness);

    sData[index + 0].pos = ConvertToP(pos[i]);
    sData[index + 0].t0.u = 0;
    sData[index + 0].t0.v = 0;
    sData[index + 0].color = color;

    sData[index + 1].pos = ConvertToP(pos[i]);
    sData[index + 1].t0.u = 0;
    sData[index + 1].t0.v = 1;
    sData[index + 1].color = color;

    sData[index + 2].pos = ConvertToP(pos[i]);
    sData[index + 2].t0.u = 1;
    sData[index + 2].t0.v = 0;
    sData[index + 2].color = color;

    sData[index + 3].pos = ConvertToP(pos[i]);
    sData[index + 3].t0.u = 1;
    sData[index + 3].t0.v = 1;
    sData[index + 3].color = color;
  }

  REPORT_ADDSHAPE
}

#if _ENABLE_SKINNEDINSTANCING

bool VertexStaticData11::AddShapeSkinnedInstanced(const Shape &src, bool dynamic, int instancesCount)
{

  // when adding dynamic data, add only indices

  DWORD flags = NoSysLockFlag;

  Assert (_vertexSize == sizeof(SVertexSkinned9));

  if (!dynamic)
  {
    VertexHeap9::HeapItem *item = _manager.Alloc(src.NVertex() * instancesCount);
    if (!item)
    {
      return false; 
    }

    ShapeGeometryLock<> lock(&src);

    PROFILE_DX_SCOPE(3dvbC);

RetryV:
    void *data = Lock();
    if (!data)
      return false;

    // Copy position, normal and UV
    SVertexSkinned9 *sData = (SVertexSkinned9 *)data;
    const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
    const AutoArray<Vector3> &pos = src.GetPosArray();
    const CondensedAutoArray<Vector3> &norm = src.GetNormArray();

    // Copy weights and indices
    const AutoArray<AnimationRTWeight> &a = src.GetVertexBoneRef();

    int nVertex = src.NVertex();
    int bonesStride = src.GetSubSkeletonSize();
    if (a.Size() > 0)
    {
      // TODO: sequential access
      for (int instance = 0; instance < instancesCount; instance++)
      {
        for (int i = 0; i < nVertex; i++)
        {
          sData->pos = ConvertToP(pos[i]);
          CopyNormal(sData->norm, norm[i]);
          sData->t0 = (*uv)[i];

          int wsize = ai.Size();
          // Copy indices and weights
          for (int w = 0; w < wsize; w++)
          {
            sData->indices[w] = instance * bonesStride + GetSubSkeletonIndex(ai[w].GetSel());
            sData->weights[w] = ai[w].GetWeight8b();
          }
          // Zero remaining indices and weights
          for (int w = wsize; w < 4; w++)
          {
            sData->indices[w] = instance * bonesStride;
            sData->weights[w] = 0;
          }

          // Increment the data pointer
          sData++;
        }
      }
    }
    else
    {
      RptF("Warning: Object is animated, but no skeleton is defined");
      for (int instance = 0; instance < instancesCount; instance++)
      {
        for (int i = 0; i < nVertex; i++)
        {
          sData->pos = ConvertToP(pos[i]);
          CopyNormal(sData->norm, norm[i]);
          sData->t0 = (*uv)[i];
          sData->indices[0] = sData->indices[1] = sData->indices[2] = sData->indices[3] = instance * bonesStride;
          sData->weights[0] = sData->weights[1] = sData->weights[2] = sData->weights[3] = 0;

          // Increment the data pointer
          sData++;
        }
      }
    }

    _vBuffer->Unlock();

    REPORT_ADDSHAPE
  }

  return true;
}

bool VertexStaticData11::AddShapeNormalSkinnedInstanced(const Shape &src, bool dynamic, int instancesCount)
{

  // when adding dynamic data, add only indices

  DWORD flags = NoSysLockFlag;

  Assert(_vertexSize == sizeof(SVertexNormalSkinned9));

  if (!dynamic)
  {
    VertexHeap9::HeapItem *item = _manager.Alloc(src.NVertex() * instancesCount);
    if (!item)
    {
      return false; 
    }

    ShapeGeometryLock<> lock(&src);

    PROFILE_DX_SCOPE(3dvbC);

    vIndex = item;
    void *data = Lock();
    if (!data)
      return false;

    // Copy position, normal and UV
    SVertexNormalSkinned9 *sData = (SVertexNormalSkinned9*)data;
    const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
    const AutoArray<Vector3> &pos = src.GetPosArray();
    const CondensedAutoArray<Vector3> &norm = src.GetNormArray();

    // If ST is present in the source, then use it, else generate it
    if (src.IsSTPresent())
    {

      // ST vectors
      const STPair *st = &src.ST(0);

      // Copy weights and indices
      const AutoArray<AnimationRTWeight> &a = src.GetVertexBoneRef();

      int nVertex = src.NVertex();
      int bonesStride = src.GetSubSkeletonSize();
      if (a.Size() > 0)
      {
        for (int instance = 0; instance < instancesCount; instance++)
        {
          for (int i = 0; i < nVertex; i++)
          {
            sData->pos = ConvertToP(pos[i]);
            CopyNormal(sData->norm, norm[i]);
            sData->t0 = (*uv)[i];
            CopyNormal(sData->s, st[i].s);
            CopyNormal(sData->t, st[i].t);

            int wsize = a[i].Size();
            // Copy indices and weights
            for (int w = 0; w < wsize; w++)
            {
              sData->indices[w] = instance * bonesStride + GetSubSkeletonIndex(a[i][w].GetSel());
              sData->weights[w] = a[i][w].GetWeight8b();
            }
            // Zero remaining indices and weights
            for (int w = wsize; w < 4; w++)
            {
              sData->indices[w] = instance * bonesStride;
              sData->weights[w] = 0;
            }

            // Increment the data pointer
            sData++;
          }
        }
      }
      else
      {
        RptF("Warning: Object is animated, but no skeleton is defined");
        for (int instance = 0; instance < instancesCount; instance++)
        {
          for (int i = 0; i < nVertex; i++)
          {
            sData->pos = ConvertToP(pos[i]);
            CopyNormal(sData->norm, norm[i]);
            sData->t0 = (*uv)[i];
            CopyNormal(sData->s, st[i].s);
            CopyNormal(sData->t, st[i].t);
            sData->indices[0] = sData->indices[1] = sData->indices[2] = sData->indices[3] = instance * bonesStride;
            sData->weights[0] = sData->weights[1] = sData->weights[2] = sData->weights[3] = 0;

            // Increment the data pointer
            sData++;
          }
        }
      }
    }
    else
    {
      Fail("Error: ST shouldn't be calculated anymore");
    }

    _vBuffer->Unlock();

    _nShapes++;
    REPORT_ADDSHAPE
  }
  else
  {
    vIndex = NULL;
  }

  return true;
}

bool VertexStaticData11::AddShapeShadowVolumeSkinnedInstanced(const Shape &src, bool dynamic, int instancesCount)
{

  // when adding dynamic data, add only indices

  DWORD flags = NoSysLockFlag;

  Assert(_vertexSize == sizeof(SVertexShadowVolumeSkinned9));

  if (!dynamic)
  {
    VertexHeap9::HeapItem *item = _manager.Alloc(src.NVertex() * instancesCount);
    if (!item)
    {
      return false; 
    }

    ShapeGeometryLock<> lock(&src);

    PROFILE_DX_SCOPE(3dvbC);

    vIndex = item;
RetryV:
    void *data = Lock();
    if (!data)
      return false;

    SVertexShadowVolumeSkinned9 *sData = (SVertexShadowVolumeSkinned9*)data;
    const Vector3 *pos = src.GetPosArray().Data();
    const AutoArray<AnimationRTWeight> &a = src.GetVertexBoneRef();

    if (a.Size() > 0)
    {
      const AutoArray<VertexNeighborInfo> &neighborBoneRef = src.GetNeighborBoneRef();
      int nVertex = src.NVertex();
      int bonesStride = src.GetSubSkeletonSize();
      for (int instance = 0; instance < instancesCount; instance++)
      {
        for (int i = 0; i < nVertex; i++)
        {
          sData->pos = pos[i];
          sData->posA = pos[neighborBoneRef[i]._posA];
          sData->posB = pos[neighborBoneRef[i]._posB];

          int wsize;

          // Original vertex
          wsize = a[i].Size();
          // Copy indices and weights
          for (int w = 0; w < wsize; w++)
          {
            sData->indices[w] = instance * bonesStride + GetSubSkeletonIndex(a[i][w].GetSel());
            sData->weights[w] = a[i][w].GetWeight8b();
          }
          // Zero remaining indices and weights
          for (int w = wsize; w < 4; w++)
          {
            sData->indices[w] = instance * bonesStride;
            sData->weights[w] = 0;
          }

          // Vertex A
          wsize = neighborBoneRef[i]._rtwA.Size();
          // Copy indices and weights
          for (int w = 0; w < wsize; w++)
          {
            sData->indicesA[w] = instance * bonesStride + GetSubSkeletonIndex(neighborBoneRef[i]._rtwA[w].GetSel());
            sData->weightsA[w] = neighborBoneRef[i]._rtwA[w].GetWeight8b();
          }
          // Zero remaining indices and weights
          for (int w = wsize; w < 4; w++)
          {
            sData->indicesA[w] = instance * bonesStride;
            sData->weightsA[w] = 0;
          }

          // Vertex B
          wsize = neighborBoneRef[i]._rtwB.Size();
          // Copy indices and weights
          for (int w = 0; w < wsize; w++)
          {
            sData->indicesB[w] = instance * bonesStride + GetSubSkeletonIndex(neighborBoneRef[i]._rtwB[w].GetSel());
            sData->weightsB[w] = neighborBoneRef[i]._rtwB[w].GetWeight8b();
          }
          // Zero remaining indices and weights
          for (int w = wsize; w < 4; w++)
          {
            sData->indicesB[w] = instance * bonesStride;
            sData->weightsB[w] = 0;
          }

          // Increment the data pointer
          sData++;
        }
      }
    }
    else
    {
      RptF("Warning: Object is animated, but no skeleton is defined - slow version of VB creation");
      int nVertex = src.NVertex();
      int bonesStride = src.GetSubSkeletonSize();
      for (int instance = 0; instance < instancesCount; instance++)
      {
        // Go through all the sections
        for (int i = 0; i < nVertex; i++)
        {
          sData[i].pos = ConvertToP(pos[i]);
          FillWeightsAndIndices(sData[i].weights,sData[i].indices,a[i]);
          sData[i].posA = ConvertToP(pos[i]);
          FillWeightsAndIndices(sData[i].weightsA,sData[i].indices,a[i]);
          sData[i].posB = ConvertToP(pos[i]);
          FillWeightsAndIndices(sData[i].weightsB,sData[i].indices,a[i]);
        }
      }
    }

    _vBuffer->Unlock();

    _nShapes++;
    REPORT_ADDSHAPE
  }
  else
  {
    vIndex = NULL;
  }

  return true;
}

#endif //#if _ENABLE_SKINNEDINSTANCING
///=========================================================
// Implementation of ResourceBusy11
///=========================================================

void ResourceBusy11::Used(int numFrames)
{
  _busyUntil = Glob.frameID+numFrames;
}
bool ResourceBusy11::IsBusy() const
{
  return Glob.frameID<=_busyUntil;
}

///=========================================================
// Implementation of SetRenderStateTemp, etc
///=========================================================

EngineDD11 *EngineDD11::SetRenderStateTemp::_engine = 0;
EngineDD11 *EngineDD11::SetSamplerStateTemp::_engine = 0;
EngineDD11 *EngineDD11::PushShaderConstant::_engine = 0;
EngineDD11 *EngineDD11::SetTextureTemp::_engine = 0;
EngineDD11 *EngineDD11::ISpecialEffect::_engine = 0;
EngineDD11 *EngineDD11::PushStreamSource::_engine = 0;
EngineDD11 *EngineDD11::PushVertexShader::_engine = 0;
EngineDD11 *EngineDD11::PushPixelShader::_engine = 0;
EngineDD11 *EngineDD11::PushViewport::_engine = 0;

////////////////////////////////////////////////////////////////////////////////////////////////////
// CALM WATER STUFF
////////////////////////////////////////////////////////////////////////////////////////////////////

// calm water shader params structure instance
static StructCalmWaterParams cwp;

//
SCalmWaterIn& GetCalmWaterInPars()
{
  return cwp.inPars;
}

/// rets fraction part of float f
inline float Frac( float f ) { return  f - (float)( (int)f ); }

// computes params for calm water shader
void ComputeCalmWaterPars( EngineDD11 *engine, SCalmWaterIn *cwmp, ColorVal matAmbient, ColorVal matDiffuse,
                          ColorVal matForcedDiffuse, ColorVal matEmmisive, ColorVal matSpecular, const float matPower )
{
  // from material: cbState._matAmbient, cbState._matDiffuse,
  // cbState._matForcedDiffuse, cbState._matEmmisive, cbState._matSpecular, 
  const float kc = 2.0f; // mul param for material colors (they are halved)
  // weather condition <0;1>
  float weather = cwp.inPars.weather[3] * 2.87f - 0.25f;
  float t0 = Glob.time.toFloat();
  //t0 = 0;

  // vert anim A
  cwp.vertAnimPhaseXMulA = 0.30;
  cwp.vertAnimPhaseYMulA = 0.90;
  cwp.vertAnimZMulA = 0.040 * weather + 0.002; // 0.035
  cwp.vertAnimPhaseTA = t0 * 1.3;
  // vert anim B
  cwp.vertAnimPhaseXMulB = 0.70;
  cwp.vertAnimPhaseYMulB = 0.20;
  cwp.vertAnimZMulB = 0.030 * weather + 0.001; // 0.025
  cwp.vertAnimPhaseTB = t0 * 0.9;

  cwp.invBBResX = 1.0f / (float)engine->Width();
  cwp.invBBResY = 1.0f / (float)engine->Height();
  cwp.baseWrapScaleX = 0.1;
  cwp.baseWrapScaleY = 0.1;

  // delta wave wrap A,B
  float waveDirA[2] = { 0.50, 0.86 }; // prefer y direction // 0.5, 0.86
  float waveDirB[2] = { 0.86, 0.50 }; // prefer x direction
  float waveSpeedA = 0.025;
  float waveSpeedB = 0.029;
  float detailScaleX = 15; // 15
  float detailScaleY = 17; // 17
  float detailSpeedA = waveSpeedA * 11;
  float detailSpeedB = waveSpeedA * 13;
  cwp.dWrapA[0] = Frac( t0 * waveSpeedA * waveDirA[0] );
  cwp.dWrapA[1] = Frac( t0 * waveSpeedA * waveDirA[1] );
  cwp.dWrapB[0] = Frac( t0 * waveSpeedB * waveDirB[1] );
  cwp.dWrapB[1] = Frac( t0 * waveSpeedB * waveDirB[0] );
  // delta detail map wrap
  cwp.dDetailWrapA[0] = Frac( t0 * detailSpeedA * waveDirB[0] );
  cwp.dDetailWrapA[1] = Frac( t0 * detailSpeedA * waveDirB[1] );
  cwp.dDetailWrapB[0] = Frac( t0 * detailSpeedB * waveDirA[1] );
  cwp.dDetailWrapB[1] = Frac( t0 * detailSpeedB * waveDirA[0] );
  cwp.detailScale[0] = detailScaleX;
  cwp.detailScale[1] = detailScaleY;

  // NORMAL MAP BLENDING COEFS A, B
  // coefs variates in interval < 0; sinBlendAmp >
  // input params:
  float sinBlendAmpA = 0.60f * weather;// + 0.01f; //0.2f;
  float sinBlendAmpB = 0.75f * weather;// + 0.02f; //0.4f;
  float sinBlendSpeedA = 1.4f;
  float sinBlendSpeedB = 1.5f;
  //
  float tsbA = t0 * sinBlendSpeedA;
  float tsbB = t0 * sinBlendSpeedB;
  float sbkA = sinBlendAmpA * 0.5f;
  float sbkB = sinBlendAmpB * 0.5f;
  cwp.blendAnimPhaseA[0] = sinf( tsbA + H_PI * 0.0f ) * sbkA + sbkA;
  cwp.blendAnimPhaseA[1] = sinf( tsbA + H_PI * 0.5f ) * sbkA + sbkA;
  cwp.blendAnimPhaseA[2] = sinf( tsbA + H_PI * 1.0f ) * sbkA + sbkA;
  cwp.blendAnimPhaseA[3] = sinf( tsbA + H_PI * 1.5f ) * sbkA + sbkA;
  cwp.blendAnimPhaseB[0] = sinf( tsbB + H_PI * 1.0f ) * sbkB + sbkB;
  cwp.blendAnimPhaseB[1] = sinf( tsbB + H_PI * 0.0f ) * sbkB + sbkB;
  cwp.blendAnimPhaseB[2] = sinf( tsbB + H_PI * 1.5f ) * sbkB + sbkB;
  cwp.blendAnimPhaseB[3] = sinf( tsbB + H_PI * 0.5f ) * sbkB + sbkB;

  // DETAIL BLEND
  float sinBlendAmpDetail = 0.1f * weather;// + 0.001f; //0.1f;
  float sinBlendSpeedDetailA = 4.5f;
  float sinBlendSpeedDetailB = sinBlendSpeedDetailA;
  float tsbDA = t0 * sinBlendSpeedDetailA;
  float tsbDB = t0 * sinBlendSpeedDetailB;
  float sbkD = sinBlendAmpDetail * 0.5f;
  cwp.blendAnimDatails[0] = sinf( tsbDA ) * sbkD + sbkD;
  cwp.blendAnimDatails[1] = sinf( tsbDA + H_PI * 1.0f ) * sbkD + sbkD;
  cwp.blendAnimDatails[2] = sinf( tsbDB + H_PI * 0.5f ) * sbkD + sbkD;
  cwp.blendAnimDatails[3] = sinf( tsbDB + H_PI * 1.5f ) * sbkD + sbkD;
  cwp.finalBlendOffset = -( sinBlendAmpA + sinBlendAmpB + sinBlendAmpDetail);
  cwp.sinBlendAmpDetail = sinBlendAmpDetail; // 1.0

  /*tc1.xy -= t0 * Speeds.z * 0.0003 * normalize( WaveDirs.xy );
  tc2.xy -= t0 * Speeds.w * 0.0003 * normalize( WaveDirs.wz );*/

  // FRESNEL
  cwp.fresnelParams[0] = 0.03f;
  cwp.fresnelParams[1] = 1.0f;
  cwp.fresnelParams[2] = 1.0f;
  cwp.fresnelParams[3] = 5.0f;

  // EDGE FOAM
  cwp.edgeFoamTcPars[0] = 6.0f * 2.0 * matEmmisive.B() * kc;
  cwp.edgeFoamTcPars[1] = 7.0f * 2.0 * matEmmisive.B() * kc;
  cwp.edgeFoamTcPars[2] = weather * 0.035 + 0.040; //0.021f;
  cwp.edgeFoamTcPars[3] = weather * 0.030 + 0.030; //0.017f;

  cwp.edgeFoamColor[0] = matDiffuse.R() * kc;
  cwp.edgeFoamColor[1] = matDiffuse.G() * kc;
  cwp.edgeFoamColor[2] = matDiffuse.B() * kc;
  cwp.edgeFoamDistVanish = matDiffuse.A(); // 0.15

  cwp.edgeFoamDepthPars[0] = -matEmmisive.R() * kc; // 0.25
  cwp.edgeFoamDepthPars[1] = 10.0f * matEmmisive.G() * kc; // 0.25
  cwp.edgeFoamDepthPars[2] = 1.0f; //10 * matEmmisive.B() * kc; // 0.1
  cwp.edgeFoamDepthPars[3] = 10.0f; //50 * matEmmisive.A(); // 0.2
  /*cwp.edgeFoamDepthPars[0] = -0.24f;
  cwp.edgeFoamDepthPars[1] = 2.5f;
  cwp.edgeFoamDepthPars[2] = 1.0f;
  cwp.edgeFoamDepthPars[3] = 10.0f;*/

  // LIGHT SCATTER & FOG
  cwp.waterFogPars[0] = matForcedDiffuse.R() * kc * 0.5f; // 0.03f;
  cwp.waterFogPars[1] = matForcedDiffuse.G() * kc * 2.0f; // 0.9f;
  cwp.waterFogPars[2] = 4.0f; // N/A ( pow 4 hard coded in shader )
  cwp.lightSumCoef = 0.1f;
  //
  cwp.waterScatterPars[0] = matForcedDiffuse.B() * kc * 0.5f; // 0.03f;
  cwp.waterScatterPars[1] = matForcedDiffuse.A() * 2.0f; // 1.0f;
  cwp.waterScatterPars[2] = 4.0f; // N/A ( pow 4 hard coded in shader )
  cwp.waterScatterPars[3] = 0.5f;
  //
  // CAUSTICS
  cwp.causticsPars0[0] = -1.0f * matSpecular.G() * kc; //-0.3;
  cwp.causticsPars0[1] = 2.0 * matSpecular.R() * kc; // 1.0;
  cwp.causticsPars0[2] = -4.0f * matSpecular.B() * kc; //-0.4;
  cwp.causticsPars0[3] = 1.2f; // 1.2;
  cwp.causticsPars1[0] = 0.045 * weather + 0.013;
  cwp.causticsPars1[1] = 0.06 * weather + 0.017;
  cwp.causticsPars1[2] = 2.9 * t0;
  cwp.causticsPars1[3] = 3.7 * t0;
  cwp.causticsPars2[0] = 0.3;
  cwp.causticsPars2[1] = 6.0;
  cwp.causticsPars2[2] = 0.7;
  cwp.causticsPars2[3] = 0.3;
  //cwp.causticsPars3 NA

  // parallax level
  float parallaxPower = 0.2; //0.01;
  cwp.parallaxParsXZ[0] = sinBlendAmpA * parallaxPower;
  cwp.parallaxParsXZ[1] = sinBlendAmpB * parallaxPower;
}

const float* GetCalmWaterPars()
{
  return (const float*)&cwp;
}

const float* GetCalmWaterPars2()
{
  return CALMWATER_PARS_SIZE_PS1 * 4 + (float*)&cwp;
}

///=========================================================
// Implementation of PostProcessHndl
///=========================================================


// save = serialize, load = create effect & load pars
LSError PostProcessHndl::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("hndl", _hndl, 0));

  int tmpType = (int)_type;
  CHECK(ar.Serialize("type", tmpType, 0));
  _type = (PostEffectType)tmpType;

  // save
  if (ar.IsSaving())
  {
    EngineDD11::PostProcess *tmpPP = static_cast<EngineDD11::PostProcess*> (_pp.GetRef());
    if (tmpPP) CHECK(ar.Serialize("ppEffect", *tmpPP, 0));
  }
  // load && post process not created yet
  if (ar.IsLoading() && _pp.IsNull())
  {
    Ref<EngineDD11::PostProcess> tmpPP;

    switch(_type)
    {
    case PEFilmGrain:
      tmpPP = new EngineDD11::PPFilmGrain;
      break;

    case PEChromAberration:
      tmpPP = new EngineDD11::PPChromAber;
      break;

    case PEColorCorrections:
      tmpPP = new EngineDD11::PPColors;
      break;

    case PERadialBlur:
      tmpPP = new EngineDD11::PPRadialBlur;
      break;

    case PEDynamicBlur:
      tmpPP = new EngineDD11::PPDynamicBlur;
      break;

    case PEWetDistortion:
      tmpPP = new EngineDD11::PPWetDistort;
      break;

    case PEColorInv:
      tmpPP = new EngineDD11::PPColorsInversion;
      break;

    default:
      return LSNoEntry;
    }

    if (tmpPP) 
    {
      CHECK(ar.Serialize("ppEffect", *tmpPP, 0));
      _pp = tmpPP;
    }
  }

  return LSOK;
}

///=========================================================
// Implementation of D3DDeviceServant
///=========================================================

#if _ENABLE_REPORT

#if _DEBUG
# define LOG_RENDER_API 0
#elif _PROFILE
# define LOG_RENDER_API 0
#else
# define LOG_RENDER_API 0
#endif

static AutoArray<char> RenderLog;

/// print into a memory buffer instead of debug log
static void LogRender(const char *format, ...)
{
#if LOG_RENDER_API
  va_list arglist;
  va_start(arglist, format);

  BString<256> buf;
  vsprintf(buf,format,arglist);
  strcat(buf,"\n");

  // assume the buffer is already NULL terminated
  int offset = RenderLog.Size()-1;
  if (offset<0) offset = 0;
  // make sure there is a space for null termination
  RenderLog.Resize(offset+strlen(buf)+1);
  strcpy(RenderLog.Data()+offset,buf);

  va_end(arglist);
#endif
}



#define FORMAT_0  ""
#define FORMAT_1  FORMAT_0 " %s"
#define FORMAT_2  FORMAT_1 " %s"
#define FORMAT_3  FORMAT_2 " %s"
#define FORMAT_4  FORMAT_3 " %s"
#define FORMAT_5  FORMAT_4 " %s"
#define FORMAT_6  FORMAT_5 " %s"
#define FORMAT_7  FORMAT_6 " %s"
#define FORMAT_8  FORMAT_7 " %s"
#define FORMAT_9  FORMAT_8 " %s"
#define FORMAT_10 FORMAT_9 " %s"

#define ARGF(x) cc_cast(ArgumentToPrintF(x).FormatArg(x))

#define ARGF_0 
#define ARGF_1 ARGF_0, ARGF(args.arg1)
#define ARGF_2 ARGF_1, ARGF(args.arg2)
#define ARGF_3 ARGF_2, ARGF(args.arg3)
#define ARGF_4 ARGF_3, ARGF(args.arg4)
#define ARGF_5 ARGF_4, ARGF(args.arg5)
#define ARGF_6 ARGF_5, ARGF(args.arg6)
#define ARGF_7 ARGF_6, ARGF(args.arg7)
#define ARGF_8 ARGF_7, ARGF(args.arg8)
#define ARGF_9 ARGF_8, ARGF(args.arg9)
#define ARGF_10 ARGF_9, ARGF(args.arg10)

#else

# define LOG_RENDER_API 0

#endif

#if LOG_RENDER_API>=2
# define LOG_DEVICE_N_EX(Name,NArgs,...) LogRender(#Name FORMAT_##NArgs , __VA_ARGS__)
# define LOG_DEVICE_N(Name,NArgs) LogRender(#Name FORMAT_##NArgs ARGF_##NArgs)
#else
# define LOG_DEVICE_N_EX(Name,NArgs,...) (void)0 // avoid empty statement
# define LOG_DEVICE_N(Name,NArgs) (void)0 // avoid empty statement
#endif

#if LOG_RENDER_API
# define RESET_LOG_DEVICE() RenderLog.Clear()
#else
# define RESET_LOG_DEVICE() (void)0 // avoid empty statement
#endif

#define LOG_DEVICE(Name) LOG_DEVICE_N(Name,0)

#define CALL_DEVICE_0(Name)  _engine->GetD3DDeviceRec()->Name()
#define CALL_DEVICE_1(Name)  _engine->GetD3DDeviceRec()->Name(args.arg1)
#define CALL_DEVICE_2(Name)  _engine->GetD3DDeviceRec()->Name(args.arg1,args.arg2)
#define CALL_DEVICE_3(Name)  _engine->GetD3DDeviceRec()->Name(args.arg1,args.arg2,args.arg3)
#define CALL_DEVICE_4(Name)  _engine->GetD3DDeviceRec()->Name(args.arg1,args.arg2,args.arg3,args.arg4)
#define CALL_DEVICE_5(Name)  _engine->GetD3DDeviceRec()->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5)
#define CALL_DEVICE_6(Name)  _engine->GetD3DDeviceRec()->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5,args.arg6)
#define CALL_DEVICE_7(Name)  _engine->GetD3DDeviceRec()->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5,args.arg6,args.arg7)
#define CALL_DEVICE_8(Name)  _engine->GetD3DDeviceRec()->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5,args.arg6,args.arg7,args.arg8)
#define CALL_DEVICE_9(Name)  _engine->GetD3DDeviceRec()->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5,args.arg6,args.arg7,args.arg8,args.arg9)
#define CALL_DEVICE_10(Name) _engine->GetD3DDeviceRec()->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5,args.arg6,args.arg7,args.arg8,args.arg9,args.arg10)

#define CALL_DEVICE(Name,NArgs) LOG_DEVICE_N(Name,NArgs);CALL_DEVICE_##NArgs(Name)

#define CALL_ENGINE_0(Name)  DoAssert(_engine);_engine->Name()
#define CALL_ENGINE_1(Name)  DoAssert(_engine);_engine->Name(args.arg1)
#define CALL_ENGINE_2(Name)  DoAssert(_engine);_engine->Name(args.arg1,args.arg2)
#define CALL_ENGINE_3(Name)  DoAssert(_engine);_engine->Name(args.arg1,args.arg2,args.arg3)
#define CALL_ENGINE_4(Name)  DoAssert(_engine);_engine->Name(args.arg1,args.arg2,args.arg3,args.arg4)
#define CALL_ENGINE_5(Name)  DoAssert(_engine);_engine->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5)
#define CALL_ENGINE_6(Name)  DoAssert(_engine);_engine->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5,args.arg6)
#define CALL_ENGINE_7(Name)  DoAssert(_engine);_engine->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5,args.arg6,args.arg7)
#define CALL_ENGINE_8(Name)  DoAssert(_engine);_engine->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5,args.arg6,args.arg7,args.arg8)
#define CALL_ENGINE_9(Name)  DoAssert(_engine);_engine->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5,args.arg6,args.arg7,args.arg8,args.arg9)
#define CALL_ENGINE_10(Name) DoAssert(_engine);_engine->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5,args.arg6,args.arg7,args.arg8,args.arg9,args.arg10)

#define CALL_ENGINE(Name,NArgs) LOG_DEVICE_N(Name,NArgs);CALL_ENGINE_##NArgs(Name)


Void D3DDeviceServant::IASetIndexBuffer(const IASetIndexBufferAllArgs& args)
{
  LOG_DEVICE_N(IASetIndexBuffer, 3);
  IDirect3DIndexBuffer9Old *buffer = args.arg1.GetResult();
  _ibOK = buffer != NULL; // ignore DrawIndexedPrimitive when failed
  if (_ibOK)
  {
    CALL_DEVICE(IASetIndexBuffer, 3);
  }
  return Void();
}

Void D3DDeviceServant::IASetVertexBuffers(const IASetVertexBuffersAllArgs& args)
{
#if _ARMA3_VERTEX_BUFFER_EX
  IDirect3DVertexBuffer9Old *buffer = args.arg2.GetResult().GetBuffer();
#else
  IDirect3DVertexBuffer9Old *buffer = args.arg2.GetResult();
#endif
  _vbOK = buffer != NULL; // ignore DrawIndexedPrimitive and DrawPrimitive when failed
  if (_vbOK)
  {
    CALL_DEVICE(IASetVertexBuffers, 4);
  }
  return Void();
}

Void D3DDeviceServant::ResolveSubresource(const ResolveSubresourceAllArgs& args)
{
  CALL_DEVICE(ResolveSubresource, 5);
  return Void();
}

Void D3DDeviceServant::CopyResource(const CopyResourceAllArgs& args)
{
  CALL_DEVICE(CopyResource, 2);
  return Void();
}

Void D3DDeviceServant::RSSetViewports(const RSSetViewportsAllArgs& args)
{
  CALL_DEVICE(RSSetViewports, 1);
  return Void();
}

Void D3DDeviceServant::ClearRenderTargetView(const ClearRenderTargetViewAllArgs& args)
{
  Assert(args.arg2.Size() == 4);
  FlushGPU(_dev);
  CALL_DEVICE(ClearRenderTargetView, 2);
  FlushGPU(_dev);
  return Void();
}

Void D3DDeviceServant::ClearDepthStencilView(const ClearDepthStencilViewAllArgs& args)
{
  FlushGPU(_dev);
  CALL_DEVICE(ClearDepthStencilView, 4);
  FlushGPU(_dev);
  return Void();
}

Void D3DDeviceServant::OMSetRenderTargets(const OMSetRenderTargetsAllArgs& args)
{
  CALL_DEVICE(OMSetRenderTargets, 2);
  return Void();
}

Void D3DDeviceServant::Flush(const FlushAllArgs& args)
{
  CALL_DEVICE(Flush, 0);
  return Void();
}


Void D3DDeviceServant::SetShaderConstantF(const SetShaderConstantFAllArgs& args)
{
  CALL_ENGINE(DoSetShaderConstantF, 4);
  return Void();
}

Void D3DDeviceServant::SetTexture(const SetTextureAllArgs& args)
{
  CALL_ENGINE(DoSetTexture, 2);
  return Void();
}

Void D3DDeviceServant::SetTextureDirect(const SetTextureDirectAllArgs& args)
{
  CALL_ENGINE(DoSetTextureDirect, 2);
  return Void();
}

Void D3DDeviceServant::SetRenderState(const SetRenderStateAllArgs& args)
{
  CALL_ENGINE(DoSetRenderState, 2);
  return Void();
}

Void D3DDeviceServant::SetSamplerState(const SetSamplerStateAllArgs& args)
{
  CALL_ENGINE(DoSetSamplerState, 3);
  return Void();
}

Void D3DDeviceServant::SetupAllBeforeDraw(const SetupAllBeforeDrawAllArgs& args)
{
  CALL_ENGINE(DoSetupAllBeforeDraw, 0);
  return Void();
}

Void D3DDeviceServant::DrawIndexedPrimitive(const DrawIndexedPrimitiveAllArgs& args)
{
  if (_vbOK && _ibOK)
    CALL_ENGINE(DoDrawIndexedPrimitive, 6);
  return Void();
}

Void D3DDeviceServant::DrawPrimitive(const DrawPrimitiveAllArgs& args)
{
  if (_vbOK)
    CALL_ENGINE(DoDrawPrimitive, 3);
  return Void();
}

Void D3DDeviceServant::SetPrimitiveTopology(const SetPrimitiveTopologyAllArgs& args)
{
  CALL_ENGINE(DoSetPrimitiveTopology, 1);
  return Void();
}

Void D3DDeviceServant::SetVertexShader(const SetVertexShaderAllArgs& args)
{
  CALL_ENGINE(DoSetVertexShader, 2);
  return Void();
}

Void D3DDeviceServant::SetPixelShader(const SetPixelShaderAllArgs& args)
{
  CALL_ENGINE(DoSetPixelShader, 1);
  return Void();
}
 
static inline int ProfileBeginGraphScope(unsigned int color, const char *name)
{
  (void)color;
  (void)name;
  return -1;
}

static inline int ProfileEndGraphScope()
{
  return -1;
}

Void D3DDeviceServant::ProfileBeginGraphScope(const ProfileBeginGraphScopeAllArgs &args)
{
  DX11::ProfileBeginGraphScope(args.arg1,args.arg2);
  return Void();
}

Void D3DDeviceServant::ProfileEndGraphScope(const ProfileEndGraphScopeAllArgs &args)
{
  DX11::ProfileEndGraphScope();
  return Void();
}

Void D3DDeviceServant::Draw2D(const Draw2DAllArgs &args)
{
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD11->_backgroundD3D==EngineDD11::RecHighLevel);
  //TrapFor0x8000Scope lock("D3DDeviceServant::Draw2D");
  GEngineDD11->DoDraw2D(args.arg1._pars,args.arg2,args.arg3);
  return Void();
}

Void D3DDeviceServant::Postprocess(const PostprocessAllArgs &args)
{
  //TrapFor0x8000Scope lock("D3DDeviceServant::Postprocess");
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD11->_backgroundD3D==EngineDD11::RecHighLevel);
  GEngineDD11->DoPostprocess(args.arg1,args.arg2);
  return Void();
}

Void D3DDeviceServant::NoPostprocess(const NoPostprocessAllArgs &args)
{
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD11->_backgroundD3D==EngineDD11::RecHighLevel);
  GEngineDD11->DoNoPostprocess();
  return Void();
}

Void D3DDeviceServant::DrawSSAO(const DrawSSAOAllArgs &args)
{
  GEngineDD11->DoDrawSSAO(args.arg1, args.arg2, args.arg3);
  return Void();
}

Void D3DDeviceServant::SetFogColor(const SetFogColorAllArgs &args)
{
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD11->_backgroundD3D==EngineDD11::RecHighLevel);
  GEngineDD11->DoSetFogColor(args.arg1, args.arg2);
  return Void();
}

Void D3DDeviceServant::FinishRendering(const FinishRenderingAllArgs &args)
{
  //TrapFor0x8000Scope lock("D3DDeviceServant::FinishRendering");
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD11->_backgroundD3D==EngineDD11::RecHighLevel);
  GEngineDD11->DoFinishRendering();
  return Void();
}


Void D3DDeviceServant::DrawPolyPrepare(const DrawPolyPrepareAllArgs &args)
{
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD11->_backgroundD3D==EngineDD11::RecHighLevel);
  //TrapFor0x8000Scope lock("D3DDeviceServant::DrawPolyPrepare");
  GEngineDD11->DoDrawPolyPrepare(args.arg1._pars,args.arg2);
  return Void();
}

Void D3DDeviceServant::DrawLineDo(const DrawLineDoAllArgs &args)
{
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD11->_backgroundD3D==EngineDD11::RecHighLevel);
  GEngineDD11->DoDrawLineDo(args.arg1,args.arg2,args.arg3,args.arg4);
  return Void();
}

Void D3DDeviceServant::DrawPolyAbsDo(const DrawPolyAbsDoAllArgs &args)
{
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD11->_backgroundD3D==EngineDD11::RecHighLevel);
  GEngineDD11->DoDrawPolyDo(args.arg1.Data(),args.arg1.Size(),args.arg2);
  return Void();
}

Void D3DDeviceServant::DrawPolyPixelDo(const DrawPolyPixelDoAllArgs &args)
{
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD11->_backgroundD3D==EngineDD11::RecHighLevel);
  GEngineDD11->DoDrawPolyDo(args.arg1.Data(),args.arg1.Size(),args.arg2);
  return Void();
}

Void D3DDeviceServant::FlushQueues(const FlushQueuesAllArgs &args)
{
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD11->_backgroundD3D==EngineDD11::RecHighLevel);
  GEngineDD11->FlushAndFreeAllQueues();
  return Void();
}

Void D3DDeviceServant::DrawDecal(const DrawDecalAllArgs &args)
{
  //TrapFor0x8000Scope lock("D3DDeviceServant::DrawDecal");
  GEngineDD11->DoDrawDecal(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5,args.arg6,args.arg7._pars,args.arg8,args.arg9);
  return Void();
}

Void D3DDeviceServant::CalculateFlareIntensity(const CalculateFlareIntensityAllArgs &args)
{
  //TrapFor0x8000Scope lock("D3DDeviceServant::CalculateFlareIntensity");
  GEngineDD11->DoCalculateFlareIntensity(args.arg1,args.arg2,args.arg3,args.arg4);
  return Void();
}

Void D3DDeviceServant::PresentBackbuffer(const PresentBackbufferAllArgs &args)
{
  //TrapFor0x8000Scope lock("D3DDeviceServant::PresentBackbuffer");
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD11->_backgroundD3D==EngineDD11::RecHighLevel);

  // We do not want to call DoPresentBackbuffer, because we have to call
  // Present() in main thread (because of DXGI deadlock)
  // see http://msdn.microsoft.com/en-us/library/ee417025%28v=vs.85%29.aspx (Multithreading and DXGI)
  // We now call Present in EndHighLevelRecordingPart1 and only set flag here
  
  GEngineDD11->DoPresentBackbuffer();
  //GEngineDD11->_hlPresentRequired = true;
  return Void();
}


Void D3DDeviceServant::ForgetAvgIntensity(const ForgetAvgIntensityAllArgs &args)
{
  //TrapFor0x8000Scope lock("D3DDeviceServant::ForgetAvgIntensity");
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD11->_backgroundD3D==EngineDD11::RecHighLevel);
  GEngineDD11->DoForgetAvgIntensity();
  return Void();
}

Void D3DDeviceServant::FreeSurface(const FreeSurfaceAllArgs &args)
{
#if !XBOX_D3D
  //TrapFor0x8000Scope lock("D3DDeviceServant::FreeSurface");
  // do nothing here, the ref will be destroyed by automatic destruction of StoredComRef / StoredRef
#endif
  return Void();
}

Void D3DDeviceServant::R2TCopyTarget(const R2TCopyTargetAllArgs &args)
{
#if _HELISIM_RENDER_TO_TEXTURE
  CALL_ENGINE(DoR2TCopyTarget, 4);
#endif
  return Void();
}

Void D3DDeviceServant::R2TPostProcess(const R2TPostProcessAllArgs &args)
{
#if _HELISIM_RENDER_TO_TEXTURE
  _engine->DoR2TPostProcess(args.arg1, args.arg2, (RenderTargetID)args.arg3);
#endif
  return Void();
}

Void D3DDeviceServant::R2TClearTarget(const R2TClearTargetAllArgs &args)
{
#if _HELISIM_RENDER_TO_TEXTURE
  CALL_ENGINE(DoR2TClearTarget, 1);
#endif
  return Void();
}

Void D3DDeviceServant::UpdateSimulWeatherTexture(const UpdateSimulWeatherTextureAllArgs &args)
{
#if _ARMA3_SIMUL_WEATHER
  GEngineDD11->DoUpdateSimulWeatherTexture(args.arg1, args.arg2);
#endif
  return Void();
}


///=========================================================
// Implementation of VisualServant
///=========================================================

WaitableVoid VisualServant::PerformMipmapLoading(const PerformMipmapLoadingAllArgs &args)
{
  GEngineDD11->TextBankDD()->PerformMipmapLoading();
  return WaitableVoid();
}

Void VisualServant::DoLoadHeaders(const DoLoadHeadersAllArgs &args)
{
  TextureD3D11 *texture = static_cast<TextureD3D11 *>(args.arg1.GetRef());
  if (!texture->HeadersReadyFast())
    texture->LoadHeadersServantCallback();
  return Void();
}
Void VisualServant::UseMipmap(const UseMipmapAllArgs &args)
{
  GEngineDD11->TextBankDD()->UseMipmap(args.arg1,args.arg2,args.arg3);
  return Void();
}

Void VisualServant::CallDelegate(const CallDelegateAllArgs &args)
{
  (*args.arg1)();
  return Void();
}


///=========================================================
// Implementation of EngineDD11::ConstantBufferInfo
///=========================================================

void EngineDD11::ConstantBufferInfo::CreateBuffer(EngineDD11* engine, int size)
{
  Assert(engine);
  if (!engine)
    return;
  Assert(!engine->GetD3DDeviceRec().IsNull());
  if (engine->GetD3DDeviceRec().IsNull())
    return;

  D3D11_BUFFER_DESC bd;
  bd.ByteWidth = size;
  if (D3DUseDynamicConstantBuffers)
  {
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.CPUAccessFlags   = D3D11_CPU_ACCESS_WRITE;
  }
  else
  {
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.CPUAccessFlags   = 0;
  }
  bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  bd.MiscFlags        = 0;

  HRESULT hr = engine->GetD3DDeviceRec()->CreateBuffer(&bd, 0, _buffer.Init());
  hr;
  DoAssert(SUCCEEDED(hr));

  _size = size;
}

void EngineDD11::ConstantBufferInfo::Free()
{
  _buffer.Free();
  _size = 0;
}

///=========================================================
// Implementation of EngineDD11::MultiSizeConstantBuffer
///=========================================================

ComRef<ID3D11Buffer> EngineDD11::MultiSizeConstantBuffer::GetBuffer(int size)
{
  for (int i=0; i< _bufferList.Size(); ++i)
  {
    const EngineDD11::ConstantBufferInfo& info = _bufferList[i];
    if (info._size >= size && info._buffer)
      return info._buffer;
  }

  DoAssert(false);
  return ComRef<ID3D11Buffer>();
}

void EngineDD11::MultiSizeConstantBuffer::CreateBuffers(EngineDD11* engine, int maxSize, bool createMulti)
{
  Free();
  Assert(maxSize % 16 == 0); //all constant buffers must have a size divisible by 16
  
  static const int MaxMultiBuffers = 16;

  int numToCreate = createMulti ? MaxMultiBuffers : 1;

  for (int i=0; i < numToCreate; ++i)
  {
    int size = maxSize;
    
    if (i < numToCreate - 1)
    {
      size = ((float)maxSize/numToCreate) * (i+1);
      //all constant buffers must have a size divisible by 16
      size = size & (~15);
    }

    if (size <= 0)
      break;
    
    int index = _bufferList.Add();
    EngineDD11::ConstantBufferInfo& info = _bufferList[index];
    info.CreateBuffer(engine, size);
  }
}

void EngineDD11::MultiSizeConstantBuffer::Free()
{
  for (int i=0; i< _bufferList.Size(); ++i)
  {
    _bufferList[i].Free();
  }
  _bufferList.Clear();
}

///=========================================================
// Implementation of EngineDD11
///=========================================================

/// global flag - if DX11 should be used
bool EngineDD11::UseDX11 = true;

/// creation of dx11 engine
EngineDD11* EngineDD11::CreateEngine( CreateEnginePars &pars )
{
  HideCursor=true;
  if (!pars.UseWindow) EnableDesktopCursor(false);

  EngineDD11 *newEngine = new EngineDD11;
  if (!newEngine) 
  {
    return 0;
  }

  if (!newEngine->Init(pars.hInst,pars.hPrev,pars.sw, pars.UseWindow, pars.bpp, pars.GenerateShaders))
  {
    newEngine->Done();
    delete newEngine;
    return 0;
  }

  // Return created and initialized engine
  return newEngine;
}

EngineDD11::EngineDD11()
{
  // LUKE - little hack - we need to initialize GEngine
  GEngine = this;

  // Set Initialized flag
  _initialized = false;
  _sbQuality = 0;
  _dbQuality = 0;
  _cursorX = 0;
  _cursorY = 0;
  _cursorSet = false;
  _cursorShown = false;
  _cursorOwned = true;
  _doBloom = true;
  // unless proven otherwise, assume worst
  _deviceLevel = 0;
  _caps._vbuffersStatic = false;
  _caps._localPredictionSet = false;
  _cbCount = -1;
  _backgroundD3D = RecNone;
  _hlRecording = GetCPUCount()>1;
  _inVideoOptions = false;
#if _ARMA3_REFRACTION
  _numBgCaptures = 0;
#endif

#ifdef _XBOX
  const int memSize = 4*1024*1024;
#else
  const int memSize = 16*1024*1024;
#endif
  _bg.InitMem(memSize);

  _resetStatus = S_OK;
  _resetRequested = false;

  _d3dDebugMode = false;
  _hlPresentRequired = false;
  _curCursor = 0;
}

EngineDD11::~EngineDD11()
{
  Done();
  _bg.ClearMem();
  struct VerifyNoRef
  {
    bool operator () (RefCount *item) const
    {
      Fail("No items should be present in _bgRefToRelease when destructing");
      return true;
    }
  };
  EngineDDBGStoredRefs11.ForEach(VerifyNoRef());
#if SAFE_RESOURCE_UNSET
  DoAssert(_bgResToRelease.Size()==0);
  DoAssert(_bgTexHandleToRelease.Size()==0);
#endif
}


bool EngineDD11::InitCB()
{
  // set cb even here so that we can use it consistently everywhere
  Assert(!IsCBRecording());
  // we need immediate CB + one per each CPU (thread)
  _cbState.Resize(GetCPUCount()+1);

  Assert(_cbState.Size()>1);
  Assert(_cbState.Size()<=32);

  const int cb = -1;
  RendState &cbState = CBState(cb);

  // Set initial value for pixel shader
  cbState._pixelShaderSel = PSUninitialized;
  cbState._pixelShaderTypeSel = PSTUninitialized;

  cbState._bias = 0;
  cbState._tlActive = TLDisabled;

  return true;
}

bool EngineDD11::InitDXGI()
{
  // create DXGI factory
  HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)_dxgiFactory.Init());
  if (FAILED(hr) || _dxgiFactory.IsNull())
  {
    Fail("DX11 - failed to create DXGI factory.");
    return false;
  }

  if (!InitDXGIAdapter())
  {
    Fail("DX11 - failed to initialize DXGI adapter.");
    return false;  
  }

  if (!InitDXGIOutput())
  {
    Fail("DX11 - failed to initialize DXGI output.");
    return false;  
  }

  return true;
}

void EngineDD11::DeInitDXGI()
{
  _dxgiOutput.Free();
  _dxgiAdapter.Free();
  _dxgiFactory.Free();
}

bool EngineDD11::InitAppWindow(HINSTANCE hInst, HINSTANCE hPrev, int sw, bool windowed)
{
  _windowed = windowed;
  _hInst = hInst;

  // raster parameters
  if (_windowed)
  {
    // search for a location that is handled by selected adapter
    SearchMonitor(_winX, _winY, _winW, _winH);
    _hwndApp=AppInit(hInst, hPrev, sw, false, _winX, _winY, _winW, _winH);
    if( !_hwndApp ) {
      Fail("Failed to create a window");
      return false;
    }

    RECT client;
    // adjust width, height to match client area
    GetClientRect(_hwndApp,&client);
    _w = client.right-client.left;
    _h = client.bottom-client.top;
  }
  else
  {
    _hwndApp=AppInit(hInst,hPrev,sw,true,0,0,160,160);
    if( !_hwndApp ) {
      Fail("Failed to create a window");
      return false;
    }

    //resolution already taken from SetDefault or loadconfig
    Assert(_w!=0);
    Assert(_h!=0);
    _fullW = _w;
    _fullH = _h;

    if (_dxgiOutput.NotNull())
    {
      DXGI_OUTPUT_DESC desc;
      HRESULT hr = _dxgiOutput->GetDesc(&desc);
      if (FAILED(hr))
      {
        Fail("Cannot get monitor desc!");
      }
      else
      {
        HMONITOR mon = desc.Monitor;
        if (mon)
        {
          MONITORINFO info;
          memset(&info, 0, sizeof(info));
          info.cbSize = sizeof(info);

          if (GetMonitorInfo(mon, &info))
          {
            _monitorOffsetX = info.rcMonitor.left;
            _monitorOffsetY = info.rcMonitor.top;
          }
        }
      }
    }
  }

  return true;
}

bool EngineDD11::InitShaders(bool generateShaders)
{
  InitShaderModel();

  if (generateShaders)
  {
    _vsCache.Clear();
    _psCache.Clear();
  }

  // pixel shaders
  if (generateShaders)
  {
    RString cacheName = ShaderCacheName("PS");
    ShaderCompileCache cacheInit(SourcesPS, cacheName, RStringB(HLSLCacheDir), true);
    if (!InitPixelShaders())
    {
      Fail("Error while creating pixel shaders");
      return false;
    }
  }
  else
  {
#if _ENABLE_COMPILED_SHADER_CACHE
    RString cacheName = ShaderCacheName("PS");
    ShaderCompileCache cacheInit(SourcesPS, cacheName, RStringB(HLSLCacheDir));
#endif
    if (!InitPixelShaders())
    {
      Fail("Error while creating pixel shaders");
      return false;
    }
  }

  // vertex shaders
  if (generateShaders)
  {
    RString cacheName = ShaderCacheName("VS");
    ShaderCompileCache cacheInit(SourcesVS, cacheName, RStringB(HLSLCacheDir), true);
    if (!InitVertexShaders())
    {
      Fail("Error while creating vertex shaders");
      return false;
    }
  }
  else
  {
#if _ENABLE_COMPILED_SHADER_CACHE
    RString cacheName = ShaderCacheName("VS");
    ShaderCompileCache cacheInit(SourcesVS, cacheName, RStringB(HLSLCacheDir));
#endif
    if (!InitVertexShaders())
    {
      Fail("Error while creating vertex shaders");
      return false;
    }
  }

  // shader declarations
  if (!CreateShaderDeclarations())
  {
    Fail("Error while creating shader declarations");
    return false;
  }

  // PP shaders and stuff
  if (generateShaders)
  {
    RString cacheName = ShaderCacheName("PP");
    ShaderCompileCache cacheInit(SourcesPP, cacheName, RStringB(HLSLCacheDir), true);
    if (!CreatePostProcessStuff())
    {
      Fail("Error while creating postprocess stuff");
      return false;
    }
  }
  else
  {
#if _ENABLE_COMPILED_SHADER_CACHE
    RString cacheName = ShaderCacheName("PP");
    ShaderCompileCache cacheInit(SourcesPP, cacheName, RStringB(HLSLCacheDir));
#endif
    if (!CreatePostProcessStuff())
    {
      Fail("Error while creating postprocess stuff");
      return false;
    }
  }

  // we can clear caches now, because we don't need it anymore - shaders will be reloaded
  // only when device is recreated - and in that case they have to be also recreated
  _vsCache.Clear();
  _psCache.Clear();  

  return true;
}

void EngineDD11::DeInitShaders()
{
  DeInitPixelShaders();
  DeInitVertexShaders();
  DestroyShaderDeclarations();
}

bool EngineDD11::InitD3D()
{
  memset(&_swapChainDesc,0,sizeof(_swapChainDesc));

  FindMode();

  // Possibly not needed in DX11 - it is slower with this)
  #if 0
  #define RESTORE_AFFINITY 1
    // remember current affinity so that we can restore it later
    DWORD oldAffinity = ~0;
    DWORD sysAffinity = ~0;
    GetProcessAffinityMask(GetCurrentProcess(),&oldAffinity,&sysAffinity);

    // limit us to 2 CPU to fool driver detection and prevent driver using threaded optimizations
    // note: limiting to 1 CPU has caused game freezing (perhaps driver was using some not thread safe optimizations?)
    int usedCpus = 0;
    DWORD limitedAffinity = 0;
    for (int cpu=0; cpu<32 && usedCpus<2; cpu++)
    {
      if ((1<<cpu)&oldAffinity) usedCpus++,limitedAffinity |= 1<<cpu;
    }
    SetProcessAffinityMask(GetCurrentProcess(),limitedAffinity);
  #endif

  ComRef<ID3D11Device> d3dDevice;
  ComRef<IDXGISwapChain> d3dSwapChain;
  ComRef<ID3D11DeviceContext> d3dImmediateDeviceContext;

#if _ENABLE_CHEATS
  _d3dDebugMode = Engine::D3DDebugMode;
#else
  _d3dDebugMode = false;
#endif

  UINT flags = 0;
  if (D3DCreateSingleThreaded)
    flags |= D3D11_CREATE_DEVICE_SINGLETHREADED; 
  if (D3DCreateNoThreadOpt)
    flags |= D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS; 
  if (_d3dDebugMode)
    flags |= D3D11_CREATE_DEVICE_DEBUG;

  if (_devType == D3D_DRIVER_TYPE_HARDWARE && _dxgiAdapter.NotNull())
  {
    // This has to be done, because when adapter is not NULL, _devType must be D3D_DRIVER_TYPE_UNKNOWN,
    // otherwise D3D11CreateDeviceAndSwapChain will fail with EInvalidArg
    _devType = D3D_DRIVER_TYPE_UNKNOWN;
  }

  IDXGIAdapter* usedAdapter = 0;
  if (_devType == D3D_DRIVER_TYPE_UNKNOWN)
  {
    usedAdapter = _dxgiAdapter;
  }

  // We want to create swapchain always in windowed mode and switch to fullscreen later (if needed)
  // Reason: when we create swapchain in fullscreen, it will have problems with dual monitor setup 
  // (it will span over both monitors).
  bool wasWindowed = (_swapChainDesc.Windowed == TRUE);
  _swapChainDesc.Windowed = true;

  HRESULT hr = D3D11CreateDeviceAndSwapChain(
    usedAdapter, 
    _devType,
    NULL,
    flags,
    NULL,
    0,
    D3D11_SDK_VERSION,
    &_swapChainDesc,
    d3dSwapChain.Init(), 
    d3dDevice.Init(),
    &_featureLevel,        
    d3dImmediateDeviceContext.Init()
    );

  // Possibly not needed in DX11 - it is slower with this
  #if RESTORE_AFFINITY
    SetProcessAffinityMask(GetCurrentProcess(),oldAffinity);
  #endif

  DXGI_ADAPTER_DESC adapterDesc;
  ZeroMemory(&adapterDesc, sizeof(adapterDesc));
  Assert(_dxgiAdapter);
  if (_dxgiAdapter)
  {
    HRESULT err = _dxgiAdapter->GetDesc(&adapterDesc);
    if (FAILED(err))
    {
      DDError11("IDXGIAdapter::GetDesc failed:", err);
    }
  }


  if (FAILED(hr) || !d3dDevice || !d3dSwapChain || !d3dImmediateDeviceContext)
  {

    DDError11("Cannot create DX11 device", hr);
    RptF
      (
      "Resolution failed: %dx%dx (%d Hz)",
      _swapChainDesc.BufferDesc.Width,_swapChainDesc.BufferDesc.Height,ConvertRefreshRate(_swapChainDesc.BufferDesc.RefreshRate)
      );
    ErrorMessage(
      "Cannot create DX11 device:\n"
      "  Adapter %d (%S) %s\n"
      "  Resolution %dx%d, format %s, refresh %d Hz\n"
      "  Error %s",
      (int)D3D11Adapter, adapterDesc.Description,
      _swapChainDesc.Windowed ? "Windowed" : "Fullscreen",
      _swapChainDesc.BufferDesc.Width, _swapChainDesc.BufferDesc.Height,
      SurfaceFormatNameDD11(_swapChainDesc.BufferDesc.Format),
      ConvertRefreshRate(_swapChainDesc.BufferDesc.RefreshRate),
      DXGetErrorString(hr)
      );

    return false;
  }

  if (_featureLevel < D3D_FEATURE_LEVEL_10_0)
  {
    RptF("DirectX 10 is not supported by your adapter.");
    ErrorMessage("DirectX 10 is not supported by your adapter.");
    return false;
  }

  // reinint DXGI output (since it can change when switching to fullscreen)
  InitDXGIOutput();

  // attach device
  _d3dDeviceNonRec.AttachDevice(d3dDevice, d3dImmediateDeviceContext, d3dSwapChain, this);
  _d3dDeviceRec.AttachDevice(d3dDevice, d3dImmediateDeviceContext, d3dSwapChain);

  if (_dxgiFactory)
  {
	  // we do not want DXGI to handle window changes
    HRESULT hr = _dxgiFactory->MakeWindowAssociation(_hwndApp, 
      DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER  );
    if (FAILED(hr))
    {
      Fail("MakeWindowAssociation failed");
    }
  }

  // swap to fullscreen if needed
  _d3dDeviceRec->SetFullscreenState(!wasWindowed, _dxgiOutput);
  // get output (can be changed after swap)
  InitDXGIOutput();

  if (_d3dDebugMode)
  {
    // get info queue
    hr = d3dDevice->QueryInterface(__uuidof(ID3D11InfoQueue),  (void**)(_d3dInfoQueue.Init()) );
    if (FAILED(hr))
    {
      DDError11("Failed to get ID3D11InfoQueue", hr);
    }
  }

  RptF("D3D11 si being used");

  ReportGRAM("After device creation");
  D3D11_FEATURE_DATA_THREADING threadingSupport;

  hr = d3dDevice->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threadingSupport, sizeof(threadingSupport));
  if (!FAILED(hr))
  {
    LogF("D3D11 multithreading support - creation = %d, command lists = %d", 
      (int)threadingSupport.DriverConcurrentCreates, (int)threadingSupport.DriverCommandLists);
  }

  /// find render target and depth formats, fill various caps
  FindRTFormats();

  // suppose minimum shader model 4
  _caps._canHWMouse = true;

  LogF("A. Local %d, nonLocal %d",_caps._localVramAmount,_caps._nonlocalVramAmount);

  _resetDirty = 0;
  _resetFrame = 0;
  _freeEstDirty = 0;
  _freeEstCheckTime = ::GlobalTickCount();

  _maxAFLevel = 16;
  _maxAF = 0;
  if (_maxAFLevel>=4) _maxAF = 4;
  else if (_maxAFLevel>=2) _maxAF = 2;

  LogF(
    "Format %s/%s/%s, %dx%d/%dx%d, shadows %s, Downsample %s, Aperture %s",
    SurfaceFormatNameDD11(_swapChainDesc.BufferDesc.Format),
    SurfaceFormatNameDD11(_rtFormat),
    SurfaceFormatNameDD11(_depthFormat),
    _w,_h,_wRT,_hRT,
    SurfaceFormatNameDD11(_smFormat),
    SurfaceFormatNameDD11(_downsampleFormat),
    SurfaceFormatNameDD11(_apertureFormat)
    );

  _bgRecPredication = false;
  // CPU #2 - the same as for rendering micro-jobs
  _bg.Init(64 * 1024, BGThreadCallback, this, 2, THREAD_PRIORITY_BELOW_NORMAL, "Render");
  // CPU #4 - the same as for rendering micro-jobs
  _vis.Init( CreateThreadOnCPU(64 * 1024, VisThreadCallback, this, 4, THREAD_PRIORITY_BELOW_NORMAL, "Visualize", &_vis._threadId) );

  // we have to create constant buffers ASAP
  CreateShaderConstantBuffers();

  // create state lists
  InitStateLists();

  return true;
}

void EngineDD11::DeInitD3D()
{
  // signal background rendering thread should terminate
  _bg.Done();
  _vis.Done();

  // destroy constant buffers
  DestroyShaderConstantBuffers();

  _d3dInfoQueue.Free();
  _d3dDeviceNonRec.Free();
  _d3dDeviceRec.Free();
}
  

bool EngineDD11::InitResources(ParamEntryPar cfg, ParamEntryPar fcfg)
{
  _caps._rtAllocated = 0;
  _wRT = _wRTRounded;
  _hRT = _hRTRounded;

  if (!InitVB())
  {
    Fail("Failed to initialize vertex buffers.");
    return false;
  }

  if (!InitRenderTargets())
  {
    Fail("Failed to initialize render targets.");
    return false;
  }

  if (!CreateTextBank(cfg, fcfg))
  {
    Fail("Failed to create texture bank.");
    return false;
  }

  // Create common resources
  if (!CreateResources())
  {
    Fail("Failed to create resources.");
    return false;
  }

  // Create TI resources
  if (!CreateTIResources())
  {
    Fail("Failed to TI create resources.");
    return false;
  }

#if _HELISIM_RENDER_TO_TEXTURE
  CreateR2TResources();
#endif
#if _ARMA3_SIMUL_WEATHER
  InitSimulWeather();
#endif


  Clear(false);
  BackToFront();
  Clear(false);
  BackToFront();
  Clear(false);
  BackToFront();

  ReportGRAM("After creating resources");

  return true;
}

void EngineDD11::DeInitResources(bool destroyTexBank)
{
  Assert(!IsCBRecording());
  // this needs to be done for each CB
  for (int cb=-1; cb<CBCount(); cb++)
  {
    RendState &cbState = CBState(cb);
    ForgetDeviceBuffers(cb);

    // Free device buffers in case of reset - Should the ForgetDeviceBuffers function release them, these lines are not necessary
    cbState._vBufferLast = VertexStaticData11::BufferFuture(VertexStaticData11::BufferFuture::Null); // last ource
    cbState._iBufferLast = IndexStaticData11::BufferFuture(IndexStaticData11::BufferFuture::Null); // last SetIndices
    cbState._strideLast = 0;
  }

  DeInitRenderTargets();
  DestroyTIResources();
#if _HELISIM_RENDER_TO_TEXTURE
  DestroyR2TResources();
#endif
  DestroyResources();
  DestroySBResources();

#if _ARMA3_SIMUL_WEATHER
  DeInitSimulWeather();
#endif

  DeInitVB();
  if (destroyTexBank) DestroyTextBank();
}

bool EngineDD11::InitVB()
{
  _d3dFrameOpen = false;
  _d3dFrameOpenRequested = false;

  static StaticStorage<WORD> TriangleQueueStorageNo;
  TriQueue11 &triqNo = _queueNo._tri;
  triqNo._triangleQueue.SetStorage(TriangleQueueStorageNo.Init(TriQueueSize));

  CreateVB();
  CreateVBTL();
  return true;
}

void EngineDD11::DeInitVB()
{
  Assert(!IsCBRecording());
  DestroyVBTL();
  DestroyVB();
}

bool EngineDD11::InitRenderTargets()
{
  Assert(!_d3dDeviceRec.IsNull());
  if (_d3dDeviceRec.IsNull())
    return false;

  // DX11 TODO
  //#if !defined(_XBOX) && !defined(_X360SHADERGEN)
  //  long long texMemBeforeInit = _d3dDevice->GetAvailableTextureMem();
  //  LogF("T. GetAvailableTextureMem %lld",texMemBeforeInit);
  //#endif

  // initialize back buffer
  HRESULT err = _d3dDeviceRec->GetBuffer(0, __uuidof(ID3D11Texture2D),
    reinterpret_cast<void**>(_backBuffer._resource.Init()));
  if (FAILED(err))
  {
    DDError11("*** GetBuffer failed:%x",err);
    ErrorMessage("*** GetBuffer failed:%x",err);
    return false;
  }

  // Create the rendertarget view
  err = _d3dDeviceRec->CreateRenderTargetView(_backBuffer._resource, NULL, _backBuffer._rtView.Init());
  if (FAILED(err))
  {
    DDError11("Cannot create rendertarget view:%x", err);
    ErrorMessage("Cannot create rendertarget view:%x", err);
    return false;
  }

  // create shader resource view
  err = _d3dDeviceRec->CreateShaderResourceView(_backBuffer._resource, NULL, _backBuffer._srView.Init());
  if (FAILED(err))
  {
    DDError11("Cannot create shader resource view:%x", err);
    ErrorMessage("Cannot create shader resource view:%x", err);
    return false;
  }

  // Set rendertarget and depth-stencil surface
  _d3dDeviceRec->OMSetRenderTargets(_backBuffer._rtView, NULL);

  Assert(_caps._rtAllocated==0);
  // front-buffer+back-buffer+depth-buffer
  _caps._rtAllocated += 2*FormatToBpp(_swapChainDesc.BufferDesc.Format)*(_swapChainDesc.BufferDesc.Width*_swapChainDesc.BufferDesc.Width/8);

  _currRenderTarget = _backBuffer;
  _currDepthStencil.Free();

  // create main render target (MSAA)
  err = CreateRenderTarget(_wRT, _hRT, _rtFormat, _rtMultiSampleCount, _rtMultiSampleQuality, 
    D3D11_USAGE_DEFAULT, true, _renderTargetS);
  Log("_renderTargetS %dx%d",_wRT,_hRT);
  if (err != D3D_OK)
  {
    DDError11("Error: Render target surface creation failed.", err);
    return false;
  }

  // Create main depth buffer
  err = CreateDepthStencil(_wRT, _hRT, _depthFormat, 
    _rtMultiSampleCount, _rtMultiSampleQuality, D3D11_USAGE_DEFAULT,
    false, _depthStencilS);

  Log("_depthStencilS %dx%d",_wRT,_hRT);
  if (err != D3D_OK)
  {
    DDError11("Error: Depth buffer creation failed.", err);
    return false;
  }

  // Create DepthInfo surfaces - no multisampling used, used later for SSSM rendering
  {
    // Create render target containing depth
    // Create render target
    err = CreateRenderTarget(_wRT, _hRT, DXGI_FORMAT_R32_FLOAT, 1, 0, 
      D3D11_USAGE_DEFAULT, true, _renderTargetDepthInfo);

    Log("_renderTargetDepthInfo %dx%d",_wRT,_hRT);
    if (err != D3D_OK) {DDError11("Error: Render target depth texture creation failed.", err); return false;}

    if (_rtMultiSampleCount > 1)
    {
      // if MSAA if enabled, we want to create depth/stencil without MSAA
      err = CreateDepthStencil(_wRT, _hRT, _depthFormat, 
        1, 0, D3D11_USAGE_DEFAULT, false, 
        _depthStencilDepthInfo);

      Log("_depthStencilDepthInfo %dx%d",_wRT,_hRT);
      if (err != D3D_OK) {DDError11("Error: Render target depth surface Z creation failed.", err); return false;}
    }
    else
    {
      // if no MSAA, then we can use _depthStencilS
      _depthStencilDepthInfo.Free();
    }
  }

  // Create render target for PP if MSAA is enabled
  if (_rtMultiSampleCount > 1)
  {
    err = CreateRenderTarget(_wRT, _hRT, _rtFormat, 1, 0, 
      D3D11_USAGE_DEFAULT, true, _renderTargetPP);
  }
  else
  {
    _renderTargetPP.Free();
  }

  if (err != D3D_OK) {DDError11("Error: Render target texture or surface creation failed.", err); return false;}

  // Create render target destination (no MSAA)
  err = CreateRenderTarget(_wRT, _hRT, _rtFormat, 1, 0, 
    D3D11_USAGE_DEFAULT, true, _renderTargetD);

  if (err != D3D_OK) {DDError11("Error: Render target texture or surface creation failed.", err); return false;}

  _dbAvailable = false;

  // All DXT formats are supported
  _caps._dxtFormats = 0;
  _caps._dxtFormats |= 1<<1;
  _caps._dxtFormats |= 1<<2;
  _caps._dxtFormats |= 1<<3;
  _caps._dxtFormats |= 1<<4;
  _caps._dxtFormats |= 1<<5;

  _caps._canReadSRGB = true;

  // we want a high-resolution format for aperture control
  static const DXGI_FORMAT apertureFormats[]=
  {
    DXGI_FORMAT_R32_FLOAT,
    DXGI_FORMAT_R16_FLOAT,
    DXGI_FORMAT_R16G16_FLOAT,
    DXGI_FORMAT_R32G32_FLOAT
  };

  _apertureFormat = DXGI_FORMAT_R8G8B8A8_UINT;

  for (int i=0; i<lenof(apertureFormats); i++)
  {
    // we do not require any alpha blending or alpha testing
    if (IsSuitableRT(apertureFormats[i], false))
    {
      _apertureFormat = apertureFormats[i];
      break;
    }
  }

  LogF("Aperture surface format %s",SurfaceFormatNameDD11(_apertureFormat));

  _caps._maxTextureSize = 8192;
  _modColor = HWhite;
  _caps._canReadSRGB = true;
  _caps._canWriteSRGB = true;
  _linearSpace = true;
  _caps._rtIsSRGB = true;

  _w=_swapChainDesc.BufferDesc.Width;
  _h=_swapChainDesc.BufferDesc.Height;

  _dbAvailable = true;

  _minGuardX = 0; // guard band clipping properties
  _maxGuardX = _w;
  _minGuardY = 0;
  _maxGuardY = _h;

  _nightEye = 0;

  _pixelSize = 32;
  _depthBpp = 32;

  // Setup the viewport for a reasonable viewing area
  _viewW = _wRT;
  _viewH = _hRT;

  SetViewport(_w, _h, DepthBorderCommonMin, DepthBorderCommonMax);

  // Set default render state
  _minZRange = 0;
  _maxZRange = 1;

  _caps._can565 = true;
  _caps._can88 = true;
  _caps._can8888 = true;

  // note: with shared device, other processes may be allocating/releasing meanwhile
  // following calculation is therefore only estimation
  // DX11 TODO
  //DWORD texMemAfterInit = _d3dDevice->GetAvailableTextureMem();
  //_caps._rtAllocated += Clip32B(texMemBeforeInit-texMemAfterInit);
  //if (_caps._rtAllocated<0) _caps._rtAllocated = 0;
  //LogF("%d MB used for render targets",_caps._rtAllocated/1024/1024);

  return true;
}

void EngineDD11::DeInitRenderTargets()
{
  _backBuffer.Free();
  _renderTargetS.Free();
  _depthStencilS.Free();

  _renderTargetD.Free();
  _renderTargetPP.Free();

  _renderTargetDepthInfo.Free();
  _depthStencilDepthInfo.Free();

  _renderTargetS.Free();
  _renderTargetPP.Free();

  _currRenderTarget.Free();
  _currDepthStencil.Free();
}

bool EngineDD11::InitD3DState()
{
  // set gamma after creation 
  DoSetGamma();

  // initialize current state
  InitCurrentRenderState();

  for (int i=0; i<CBCount(); i++)
  {
    SetHWToUndefinedState(i);
    // note: we may have gathered some instructions in the invocations lists now
    // we do not care about them at all - all we wanted to do was to set the shadow state
    // this is done here for clarity only - StartCBRecording would do the same anyway 
    CBState(i)._rec.Reset();
  }

  // always init the immediate device state
  InitDeviceState(-1);

  RendStateExt& cbState = CBState(-1);
  // Back culling
  cbState._backCullDisabled = false;

  // Crater rendering

  // The main light
  cbState._mainLight = ML_Sun;

  // The fog mode
  cbState._fogMode = FM_Fog;

  // Set initial value for vertex shader
  cbState._vertexShaderID = VSBasic;

  // Set initial value for UV sources
  cbState._uvSourceCount = 0;
  cbState._uvSourceOnlyTex0 = true;

  // Set initial values for light related variables
  cbState._pointLightsCount = -1;
  cbState._spotLightsCount = -1;

  // Skinning parameters
  cbState._skinningType = STNone;
  cbState._bones.Clear();
  cbState._bonesWereSetDirectly = false;

  // Instancing parameters
  cbState._zPrimingDone = false;
  cbState._lastZPrimingDone = false;

  // Shadow buffer parameters
  _renderingMode = RMCommon;
  for (int i = 0; i < ShadowBufferCascadeLayers; i++)
  {
    _oldLRSP[i] = MIdentity;
  }

  // Single instance parameters
  SetInstanceInfo(-1, HWhite, 1.0f, false);


  return true;
}

void EngineDD11::InitStateLists()
{
  // Create state lists
#if !DX11_USE_HASHED_STATE_OBJECTS
  _depthStencilStateList.CreateStates();
  _blendStateList.CreateStates();
  _rasterizerStateList.CreateStates();
  _samplerStateList.CreateStates();
#endif
}

void EngineDD11::DeInitStateLists()
{
#if !DX11_USE_HASHED_STATE_OBJECTS
  _depthStencilStateList.Clear();
  _blendStateList.Clear();
  _rasterizerStateList.Clear();
  _samplerStateList.Clear();
#endif
}

void EngineDD11::UnsetAllResources()
{
  Assert(!IsCBRecording());

  // unset references from cb
  for (int cb = -1; cb < CBCount(); ++cb)
  {
    RendStateExt cbState = CBState(cb);

    for (int i = 0; i < MaxSamplerStages; ++i)
    {
      cbState._lastHandle[i] = (TextureHandle11*)(-1);
    }
    cbState._vBufferLast = VertexStaticData11::BufferFuture(VertexStaticData11::BufferFuture::Null);
    cbState._iBufferLast = IndexStaticData11::BufferFuture(IndexStaticData11::BufferFuture::Null);

    for (int i=0; i<NPredicationModes; ++i)
    {
      cbState._pixelShaderLast[i].Free();
    }
  }

  _currentRenderState.Reset();

  // unset from device
  if (!_d3dDeviceRec.IsNull())
  {
    _d3dDeviceRec->OMSetRenderTargets(0, 0);
    _d3dDeviceRec->VSSetShader(0);
    _d3dDeviceRec->PSSetShader(0);

    ID3D11Buffer* buffers[ShaderConstantBuffer::MaxConstantBufferCount] = {0};
    _d3dDeviceRec->VSSetConstantBuffers(0, ShaderConstantBuffer::MaxConstantBufferCount, 
      Array<ID3D11Buffer*>(buffers, ShaderConstantBuffer::MaxConstantBufferCount));
    _d3dDeviceRec->PSSetConstantBuffers(0, ShaderConstantBuffer::MaxConstantBufferCount, 
      Array<ID3D11Buffer*>(buffers, ShaderConstantBuffer::MaxConstantBufferCount));

    float dummy[4] = {0.0f};
    _d3dDeviceRec->OMSetDepthStencilState(0, 0);
    _d3dDeviceRec->RSSetState(0);
    _d3dDeviceRec->OMSetBlendState(0, Array<float>(dummy, 4),0);

    ID3D11SamplerState* samplers[MaxSamplerStages] = {0};
    _d3dDeviceRec->PSSetSamplers(0, MaxSamplerStages, Array<ID3D11SamplerState*>(samplers, MaxSamplerStages));

    _d3dDeviceRec->IASetIndexBuffer(0, DXGI_FORMAT_R16_UINT, 0);
    
    const UINT dummyInt = 0;
    ID3D11Buffer* const dummyBuff = NULL;
    _d3dDeviceRec->IASetVertexBuffers(0, 1, &dummyBuff, &dummyInt, &dummyInt);

    TextureHandle11* textures[MaxSamplerStages] = {0};
    _d3dDeviceRec->PSSetShaderResources(0, MaxSamplerStages, Array<TextureHandle11 *>(textures, MaxSamplerStages));
  }
}

bool EngineDD11::Init(HINSTANCE hInst, HINSTANCE hPrev, int sw, bool windowed, int bpp, bool generateShaders)
{
  // set this engine to various classes
  EngineDD11::SetRenderStateTemp::SetEngine(this);
  EngineDD11::SetSamplerStateTemp::SetEngine(this);
  EngineDD11::SetShaderConstantTemp::SetEngine(this);
  EngineDD11::SetTextureTemp::SetEngine(this);
  EngineDD11::PostProcess::SetEngine(this);
  EngineDD11::PPGaussianBlur::SetEngine(this);
#ifdef SSSMBLUR
  EngineDD11::PPSSSMB::SetEngine(this);
#endif
  EngineDD11::PPDOF::SetEngine(this);
  EngineDD11::PPDistanceDOF::SetEngine(this);
  EngineDD11::PPFilmGrain::SetEngine(this);
  EngineDD11::PPSSAO::SetEngine(this);
  EngineDD11::PPGlow::SetEngine(this);
  EngineDD11::PPFlareIntensity::SetEngine(this);
  EngineDD11::ISpecialEffect::SetEngine(this);

  EngineDD11::PushStreamSource::SetEngine(this);
  EngineDD11::PushVertexShader::SetEngine(this);
  EngineDD11::PushPixelShader::SetEngine(this);
  EngineDD11::PushViewport::SetEngine(this);
  EngineDD11::PushRenderTargetAndView::SetEngine(this);
  // Instance handle
  _hInst = hInst;

  // Set 
#if _ARMA3_SIMUL_WEATHER
  _swLayersCount = DefaultSWLayers;
#endif

#if _HELISIM_RENDER_TO_TEXTURE
  _enabledR2T = true;
#endif
  _vsync = true;
  _AToCMask = AToC_EnableAll;
  _pixelSize = bpp;
  _refreshRate = 0;
  _fsAAWanted = 0;
  _afWanted = 0;
  _maxFSAA = 15;
  _maxAF = 4;
  _postFxQuality = 0;
  _postFXLevelMask = CalculatePostEffectsMask(0);
  _hdrPrecWanted = HDR8;
  _gamma = 1.0f;
  //_mesh = NULL;
  _textBank = NULL;
  _caps._maxLights = MAX_LIGHTS;
  _caps._can565 = false;
  _caps._can88 = false;
  _caps._can8888 = false;
  _caps._maxTextureSize = 2048;
  _caps._hasStencilBuffer = false;
  _iBuffersAllocated = 0;
  _vBuffersAllocated = 0;
  _caps._rtAllocated = 0;
  _resetStatus = S_OK;
  _resetDone = false;
  _lastKnownAvgIntensity = -1.0f;
  _accomFactorPrev = -1;
  _clearColor = Color(1, 0, 1, 1); // Use the clear color purple to see potential problem

  //_formatSet=SingleTex;
  _renderMode = RMTris;
  _windowed = windowed;
  _visTestCount = 0;

  // DOF parameters
  _focusDist = 0.0f;
  _blurCoef = 0.0f;
  _farOnly = true;

  _airTemperature = 15.0f;
  for (int i = 0; i < TIConversionDimensionH; i++) _tiBlackHot[i] = false;
  _tiBlurCoef = 0.5f;
  _tiBrightness = 15.0f;
  _tiContrast = 25.0f;
  _maxTemperature = TempMax;
  _tiMode = 0.0f;
  _tiAutoBC = true;
  _tiAutoBCContrastCoef = 3.0f;
  _tiVisualizationDone = false;

  // 3D rendering state parameter
  _3DRenderingInProgress = false;
  _end3DRequested = true;

  _renderingMode = RMCommon;

  // Set the device type
  if (generateShaders)
  {
    // The D3D reference rasterizer is not installed on many Windows systems.
    // WARP is the supported software device for shader-cache generation.
    _devType = D3D_DRIVER_TYPE_WARP;
  }
  else
  {
    _devType = D3D_DRIVER_TYPE_HARDWARE;
  }


  // using textures and VBs for free on demand handling is somewhat risky, and makes MT safety harder (system allocator used)
  // however, we need time to be recorded for VBs
  RegisterFreeOnDemandSystemMemory(this);

  // initialize command buffers
  if (!InitCB())
  {
    return false;
  }

  // initialize DXGI - factory, adapter and output
  if (!InitDXGI())
  {
    return false;
  }

  // memory values
  DoAssert(_dxgiAdapter);
  bool memoryDetected = false;
  if (_dxgiAdapter)
  {
    DXGI_ADAPTER_DESC adapterDesc;
    HRESULT err = _dxgiAdapter->GetDesc(&adapterDesc);
    if (FAILED(err))
    {
      DDError11("DXGIAdapter GetDesc failed:", err);
    }
    else
    {
      memoryDetected = true;
      _caps._localVramAmount = Clip32B(adapterDesc.DedicatedVideoMemory);
      _caps._nonlocalVramAmount = Clip32B((long long)adapterDesc.DedicatedSystemMemory+(long long)adapterDesc.SharedSystemMemory);
    }
  }

  if (!memoryDetected)
  {
    _caps._localVramAmount = 256*1024*1024;
    _caps._nonlocalVramAmount = 256*1024*1024;
  }
  _lastDeviceId = RString();
  _freeLocalVramAmount = _caps._localVramAmount;
  _freeNonlocalVramAmount = _caps._nonlocalVramAmount;
  _freeEstDirty = 0;
  _freeEstCheckTime = 0;

  int vramAmount = _caps._localVramAmount;
  if (vramAmount<256*1024*1024) vramAmount = 256*1024*1024;
  // avoid 32b overflow
  if (vramAmount>INT_MAX) vramAmount = INT_MAX;
  // set memory before calling SetDefaultSettingsLevel
  SetTextureMemory(TextBankD3D11::SelectTextureMemory(int(vramAmount)));

  // make sure values are autodetected once engine is created
  SetDefaultSettingsLevel(GetDeviceLevel());

  // Config loading
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  ParseUserParams(cfg, &globals);
  ParamFile fcfg;
  ParseFlashpointCfg(fcfg);
  LoadConfig(cfg,fcfg,GetConfigSection());

  // initialize window
  if (!InitAppWindow(hInst, hPrev, sw, windowed))
  {
    Done();
    return false;
  }

  // inititalize D3D device
  if (!InitD3D())
  {
    Done();
    return false;
  }

  // initialize resources - render targets and other resources
  if (!InitResources(cfg, fcfg))
  {
    Done();
    return false;
  }

  // create shaders
  if (!InitShaders(generateShaders))
  {
    Done();
    return false;
  }

  if (generateShaders)
  {
    // we are done...
    Done();
    return false;
  }

  // initialize device state
  if (!InitD3DState())
  {
    Done();
    return false;
  }

#if defined _XBOX && _ENABLE_COMPILED_SHADER_CACHE
  XFlushUtilityDrive();
#endif

#if defined _XBOX || TRACK_SHADER_PATCH
  {
    // all shaders ready - now it is a time to bind them
#define BIND_SHADER_NONTL(vsi,vdi,stride,ps) \
    { \
    D3DBoundShaderName name(_vsNonTL,_vdNonTL,stride,PSFromCache(#ps)); \
    _boundShaders.Bind(this,name); \
  }
#define BIND_SHADER_POOL(vsi,vdi,stride,ps) \
    { \
    D3DBoundShaderName name(_vertexShaderPool vsi, _vertexDeclD vdi, stride, PSFromCache(#ps)); \
    _boundShaders.Bind(this,name); \
  }
#define BIND_SHADER_TERRAIN(vsi,vdi,stride,ps) \
    { \
    D3DBoundShaderName name(_vertexShaderTerrain vsi, _vertexDeclD vdi, stride, PSFromCache(#ps)); \
    _boundShaders.Bind(this,name); \
  }
#define BIND_SHADER_TERRAINGRASS(vsi,vdi,stride,ps) \
    { \
    D3DBoundShaderName name(_vertexShaderTerrainGrass vsi, _vertexDeclD vdi, stride, PSFromCache(#ps)); \
    _boundShaders.Bind(this,name); \
  }

#ifdef _XBOX
    /// no PS for shadows on Xbox
#define SHADOW_SHADER(ps) NULL 
#else
#define SHADOW_SHADER(ps) PSFromCache(#ps)
#endif
#define BIND_SHADER_SHADOWVOL(vsi,vdi,stride,ps) \
    { \
    D3DBoundShaderName name(_vertexShaderShadowVolume vsi, _vertexDeclD vdi, stride, SHADOW_SHADER(ps)); \
    _boundShaders.Bind(this,name); \
  }
#define BIND_SHADER_SPECIAL(vsi,vdi,stride,ps) \
    { \
    D3DBoundShaderName name(_vertexShaderSpecial vsi, _vertexDeclD vdi, stride, PSFromCache(#ps)); \
    _boundShaders.Bind(this,name); \
  }
    /** count is ignored, it is defined only because we want it in the stats output */
#define BIND_SHADER(XXXX,vsi,vdi,stride,ps,count) BIND_SHADER_##XXXX(vsi,vdi,stride,ps)

    // OUTPUT of cheat Alt-Win-Z (ReportShaderPatches) follows
    BIND_SHADER(TERRAIN,[RMCommon],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSTerrainX,   203)
      BIND_SHADER(POOL,[VSPSuper][0][1][RMDPrime][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSAlphaOnlyMod,   137)
      BIND_SHADER(POOL,[VSPTree][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSTree,   101)
      BIND_SHADER(POOL,[VSPSuper][0][1][RMCommon][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSSSSMSuper,   94)
      BIND_SHADER(POOL,[VSPNormalMapAS][0][1][RMDPrime][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSAlphaOnly,   73)
      BIND_SHADER(SHADOWVOL,[SISkin],[VD_ShadowVolume][VSDV_Skinning],60,PSWhite,   67)
      BIND_SHADER(POOL,[VSPNormalMapAS][0][1][RMCommon][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSSSSMNormalMapMacroASSpecularMap,   57)
      BIND_SHADER(POOL,[VSPSuper][0][1][RMCommon][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSSuper,   43)
      BIND_SHADER(POOL,[VSPGrass][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0][VSDV_Instancing],24,PSSSSMGrass,   37)
      BIND_SHADER(POOL,[VSPGrass][0][1][RMDPrime][SIInst],[VD_Position_Normal_Tex0][VSDV_Instancing],24,PSAlphaOnlyMod,   37)
      BIND_SHADER(POOL,[VSPSuper][0][1][RMDPrime][SISingle],[VD_Position_Normal_Tex0_ST][VSDV_Basic],36,PSAlphaOnlyMod,   26)
      BIND_SHADER(POOL,[VSPSuper][0][1][RMCommon][SISingle],[VD_Position_Normal_Tex0_ST][VSDV_Basic],36,PSSSSMSuper,   25)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMDPrime][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSAlphaOnly,   24)
      BIND_SHADER(TERRAIN,[RMCommon],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSRoad2Pass,   22)
      BIND_SHADER(TERRAIN,[RMCommon],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSRoad,   22)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMDPrime][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSAlphaOnlyMod,   20)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSSSSMSpecularAlpha,   20)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMCommon][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSSSSMNormalMapSpecularDIMap,   16)
      BIND_SHADER(POOL,[VSPNormalMapAS][0][1][RMCommon][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSSSSMNormalMapMacroASSpecularDIMap,   16)
      BIND_SHADER(POOL,[VSPTreeNoFade][1][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSTree,   14)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0][VSDV_Instancing],24,PSSpecularAlpha,   14)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SISingle],[VD_Position_Normal_Tex0][VSDV_Basic],24,PSSpecularAlpha,   11)
      BIND_SHADER(TERRAIN,[RMCommon],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSSSSMTerrainX,   10)
      BIND_SHADER(SHADOWVOL,[SISingle],[VD_ShadowVolume][VSDV_Basic],20,PSWhite,   9)
      BIND_SHADER(POOL,[VSPTree][1][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSTree,   8)
      BIND_SHADER(POOL,[VSPTreeNoFade][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSTree,   8)
      BIND_SHADER(POOL,[VSPSuper][1][1][RMCommon][SISingle],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Basic],36,PSSuper,   8)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMCommon][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSSSSMNormalMapDetailSpecularDIMap,   7)
      BIND_SHADER(POOL,[VSPMulti][1][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSMulti,   6)
      BIND_SHADER(SPECIAL,[VSSWater],[VD_Position_Normal_Tex0_ST][VSDV_Basic],28,PSWater,   4)
      BIND_SHADER(POOL,[VSPBasic][0][3][RMCommon][SISingle],[VD_Position_Normal_Tex0][VSDV_Basic],24,PSCloud,   4)
      BIND_SHADER(POOL,[VSPTree][1][1][RMDPrime][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSAlphaOnlyTree,   3)
      BIND_SHADER(SHADOWVOL,[SIInst],[VD_ShadowVolume][VSDV_Instancing],20,PSWhite,   3)
      BIND_SHADER(POOL,[VSPTree][1][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSSSSMTree,   3)
      BIND_SHADER(POOL,[VSPSuper][1][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSSuper,   2)
      BIND_SHADER(POOL,[VSPTree][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSSSSMTree,   2)
      BIND_SHADER(TERRAIN,[RMCommon],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSSSSMRoad2Pass,   2)
      BIND_SHADER(SPECIAL,[VSSShore],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSShoreFoam,   2)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SISingle],[VD_Position_Normal_Tex0][VSDV_Basic],24,PSHorizon,   2)
      BIND_SHADER(TERRAIN,[RMCommon],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSSSSMRoad,   2)
      BIND_SHADER(SPECIAL,[VSSShore],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSShoreWet,   2)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMCommon][SISingle],[VD_Position_Normal_Tex0_ST][VSDV_Basic],36,PSSSSMNormalMapSpecularDIMap,   2)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMDPrime][SISingle],[VD_Position_Normal_Tex0_ST][VSDV_Basic],36,PSAlphaOnly,   2)
      BIND_SHADER(TERRAIN,[RMDPrime],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSAlphaOnly,   2)
      BIND_SHADER(POOL,[VSPTree][0][1][RMDPrime][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSAlphaOnlyTree,   2)
      BIND_SHADER(SPECIAL,[VSSShore],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSShore,   2)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSNormalMapSpecularMap,   1)
      BIND_SHADER(POOL,[VSPBasic][0][2][RMCommon][SISingle],[VD_Position_Normal_Tex0][VSDV_Basic],24,PSInterpolation,   1)
      BIND_SHADER(POOL,[VSPSuper][0][1][RMCommon][SISingle],[VD_Position_Normal_Tex0_ST][VSDV_Basic],32,PSSuper,   1)
      BIND_SHADER(POOL,[VSPGlass][0][1][RMDPrime][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSAlphaOnlyMod,   1)
      BIND_SHADER(POOL,[VSPSuper][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSSuper,   1)
      BIND_SHADER(POOL,[VSPSuper][0][1][RMCommon][SISingle],[VD_Position_Normal_Tex0_ST][VSDV_Basic],36,PSSuper,   1)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMDPrime][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSAlphaOnly,   1)
      BIND_SHADER(POOL,[VSPTreeNoFade][0][1][RMDPrime][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSAlphaOnlyTree,   1)
      BIND_SHADER(POOL,[VSPGlass][0][1][RMCommon][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSSSSMGlass,   1)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMCommon][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSNormalMapDetailSpecularDIMap,   1)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSReflect,   1)
      BIND_SHADER(POOL,[VSPTreeNoFade][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSSSSMTree,   1)
      BIND_SHADER(POOL,[VSPTreeNoFade][1][1][RMDPrime][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSAlphaOnlyTree,   1)
      BIND_SHADER(POOL,[VSPNormalMapAS][1][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSNormalMapMacroASSpecularDIMap,   1)
      BIND_SHADER(POOL,[VSPTreeNoFade][1][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSSSSMTree,   1)
      BIND_SHADER(NONTL,[VSNonTL],[NonTL],40,PSNonTL,   1)

      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSNormalMapDetailSpecularMap,   2)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SISkin],[VD_Position_Normal_Tex0][VSDV_Skinning],28,PSSpecularAlpha,   2)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMDPrime][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSAlphaOnly,   1)
      BIND_SHADER(POOL,[VSPGlass][0][1][RMDPrime][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSAlphaOnlyMod,   1)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SISingle],[VD_Position_Normal_Tex0][VSDV_Basic],20,PSSpecularAlpha,   1)
      BIND_SHADER(POOL,[VSPGlass][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSGlass,   1)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSReflect,   1)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMDPrime][SISingle],[VD_Position_Normal_Tex0][VSDV_Basic],20,PSAlphaOnlyMod,   1)

      BIND_SHADER(SPECIAL,[VSSWater],[VD_Position_Normal_Tex0_ST][VSDV_Basic],28,PSWater,   58)
      BIND_SHADER(TERRAIN,[RMCommon],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSTerrainX,   57)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0][VSDV_Instancing],24,PSGrass,   41)
      BIND_SHADER(SHADOWVOL,[SIInst],[VD_ShadowVolume][VSDV_Instancing],20,PSWhite,   32)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMDPrime][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSAlphaOnly,   30)
      BIND_SHADER(TERRAIN,[RMCommon],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSSSSMTerrainX,   25)
      BIND_SHADER(POOL,[VSPGrass][0][1][RMDPrime][SIInst],[VD_Position_Normal_Tex0][VSDV_Instancing],24,PSAlphaOnlyMod,   25)
      BIND_SHADER(POOL,[VSPGrass][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0][VSDV_Instancing],24,PSSSSMGrass,   25)
      BIND_SHADER(POOL,[VSPNormalMapAS][1][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSNormalMapMacroASSpecularMap,   21)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMDPrime][SIInst],[VD_Position_Normal_Tex0][VSDV_Instancing],24,PSAlphaOnlyMod,   21)
      BIND_SHADER(POOL,[VSPNormalMapAS][1][1][RMDPrime][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSAlphaOnly,   21)
      BIND_SHADER(POOL,[VSPNormalMapAS][1][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSSSSMNormalMapMacroASSpecularMap,   21)
      BIND_SHADER(POOL,[VSPNormalMapSpecularThroughNoFade][0][1][RMDPrime][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSAlphaOnlyTree,   20)
      BIND_SHADER(POOL,[VSPNormalMapSpecularThroughNoFade][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSSSSMSpecularNormalMapSpecularThrough,   20)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMCommon][SISingle],[VD_Position_Normal_Tex0_ST][VSDV_Basic],28,PSNormalMapSpecularDIMap,   17)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSSSSMNormalMapSpecularDIMap,   17)
      BIND_SHADER(SHADOWVOL,[SISingle],[VD_ShadowVolume][VSDV_Basic],20,PSWhite,   17)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0][VSDV_Instancing],24,PSSpecularAlpha,   14)
      BIND_SHADER(POOL,[VSPNormalMapSpecularThroughNoFade][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSSpecularNormalMapSpecularThrough,   14)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0][VSDV_Instancing],24,PSSSSMDetailSpecularAlpha,   12)
      BIND_SHADER(POOL,[VSPNormalMapAS][0][1][RMCommon][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSSSSMNormalMapMacroASSpecularMap,   10)
      BIND_SHADER(POOL,[VSPNormalMapAS][0][1][RMDPrime][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSAlphaOnly,   10)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0][VSDV_Instancing],24,PSSSSMSpecularAlpha,   9)
      BIND_SHADER(SPECIAL,[VSSShore],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSShoreWet,   8)
      BIND_SHADER(SPECIAL,[VSSShore],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSShoreFoam,   8)
      BIND_SHADER(SPECIAL,[VSSShore],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSShore,   8)
      BIND_SHADER(SHADOWVOL,[SISkin],[VD_ShadowVolume][VSDV_Skinning],60,PSWhite,   7)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0][VSDV_Instancing],24,PSDetailSpecularAlpha,   6)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSSSSMNormalMapDetailSpecularMap,   5)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSSSSMNormalMapDetailSpecularDIMap,   5)
      BIND_SHADER(TERRAIN,[RMCommon],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSSSSMRoad2Pass,   4)
      BIND_SHADER(POOL,[VSPBasic][0][3][RMCommon][SISingle],[VD_Position_Normal_Tex0][VSDV_Basic],24,PSCloud,   4)
      BIND_SHADER(POOL,[VSPSuper][0][1][RMCommon][SISingle],[VD_Position_Normal_Tex0_ST][VSDV_Basic],36,PSSSSMSuper,   4)
      BIND_SHADER(POOL,[VSPNormalMapSpecularThrough][0][1][RMDPrime][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSAlphaOnlyTree,   4)
      BIND_SHADER(TERRAIN,[RMCommon],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSSSSMRoad,   4)
      BIND_SHADER(POOL,[VSPSuper][0][1][RMDPrime][SISingle],[VD_Position_Normal_Tex0_ST][VSDV_Basic],36,PSAlphaOnlyMod,   4)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSNormalMapSpecularDIMap,   4)
      BIND_SHADER(POOL,[VSPNormalMapSpecularThrough][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSSpecularNormalMapSpecularThrough,   3)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSSSSMNormalMapSpecularMap,   3)
      BIND_SHADER(POOL,[VSPNormalMapSpecularThrough][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSSSSMSpecularNormalMapSpecularThrough,   3)
      BIND_SHADER(NONTL,[VSNonTL],[NonTL],40,PSNonTL,   3)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSSSSMSpecularAlpha,   2)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SISingle],[VD_Position_Normal_Tex0][VSDV_Basic],24,PSHorizon,   2)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMDPrime][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSAlphaOnlyMod,   2)
      BIND_SHADER(TERRAIN,[RMDPrime],[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic],40,PSAlphaOnly,   2)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SISingle],[VD_Position_Normal_Tex0][VSDV_Basic],24,PSSpecularAlpha,   2)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSSpecularAlpha,   1)
      BIND_SHADER(POOL,[VSPBasicAS][1][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSSSSMDetailSpecularAlphaMacroAS,   1)
      BIND_SHADER(POOL,[VSPBasicAS][1][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSDetailSpecularAlphaMacroAS,   1)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMDPrime][SISingle],[VD_Position_Normal_Tex0_ST][VSDV_Basic],36,PSAlphaOnlyMod,   1)
      BIND_SHADER(POOL,[VSPSuper][0][1][RMDPrime][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSAlphaOnlyMod,   1)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMDPrime][SISingle],[VD_Position_Normal_Tex0_ST][VSDV_Basic],36,PSAlphaOnly,   1)
      BIND_SHADER(POOL,[VSPBasicAS][1][1][RMDPrime][SIInst],[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing],36,PSAlphaOnly,   1)
      BIND_SHADER(POOL,[VSPBasic][0][2][RMCommon][SISingle],[VD_Position_Normal_Tex0][VSDV_Basic],24,PSInterpolation,   1)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMCommon][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSNormalMapSpecularMap,   1)
      BIND_SHADER(POOL,[VSPNormalMapDiffuse][0][1][RMDPrime][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSAlphaOnlyMod,   1)
      BIND_SHADER(POOL,[VSPNormalMapDiffuse][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSSSSMSpecularNormalMapDiffuse,   1)
      BIND_SHADER(POOL,[VSPBasic][0][1][RMCommon][SISingle],[VD_Position_Normal_Tex0_ST][VSDV_Basic],36,PSSSSMSpecularAlpha,   1)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMCommon][SISingle],[VD_Position_Normal_Tex0_ST][VSDV_Basic],36,PSSSSMNormalMapSpecularDIMap,   1)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSNormalMapDetailSpecularDIMap,   1)
      BIND_SHADER(POOL,[VSPSuper][0][1][RMCommon][SISkin],[VD_Position_Normal_Tex0_ST][VSDV_Skinning],36,PSSSSMSuper,   1)
      BIND_SHADER(POOL,[VSPNormalMap][0][1][RMCommon][SIInst],[VD_Position_Normal_Tex0_ST][VSDV_Instancing],32,PSNormalMapSpecularMap,   1)


  }
#endif

  // Set Initialized flag
  _initialized = true;

  InitTexMemEstimations();

  // _vramAllocLimit may have changed in InitTexMemEstimations - we want to react
  if (_textBank) _textBank->RecalculateTextureMemoryLimits();

  // remember undefined state for later use: switching to undefined means only copying the state
  //_undefined = _cbState[0];

  // Success
  return true;
}

void EngineDD11::CreateShaderConstantBuffers()
{
  // create buffers for all CBs
  for (int i=0; i < _cbState.Size(); ++i)
  {
    _cbState[i]._shaderConsts.Create();
  }

  _currentRenderState._shaderConstants.Create();


  // Create constant buffers 
  ShaderConstantBuffersCreationInfo& info = GetShaderConstantBuffersCreationInfo();
  for (int shaderType = 0; shaderType < ShaderConstant::SCTCount; ++shaderType)
  {
    // find last non-zero size ID
    int lastID;
    for (lastID = ShaderConstantBuffer::MaxConstantBufferCount-1; lastID >= 0; --lastID)
    {
      if (info.GetBufferInfo((ShaderConstant::ShaderConstantType)shaderType, lastID)._size > 0)
        break;
    }

    Assert(lastID >= 0);
    if (lastID < 0)
      continue;

    _shaderConstantBuffer[shaderType].Resize(lastID+1);

    for (int cbId = 0; cbId <= lastID; ++cbId)
    {
      const ShaderConstantBuffersCreationInfo::BufferInfo& bufferInfo = info.GetBufferInfo((ShaderConstant::ShaderConstantType)shaderType, cbId);
      int size = bufferInfo._size;
      bool isMulti = D3DUseMultiSizeConstantBuffers && bufferInfo._multisize;

      _shaderConstantBuffer[shaderType][cbId].CreateBuffers(this, size, isMulti);
    }
  }    

  for (int shaderType = 0; shaderType < ShaderConstant::SCTCount; ++shaderType)
  {
    for (int i=0; i< ShaderConstantBuffer::MaxConstantBufferCount; ++i)
    {
      _currentRenderState._constantBufferLast[shaderType][i] = 0;
    }
  }
}

bool EngineDD11::InitDXGIOutput()
{
  Assert(_dxgiAdapter.NotNull());
  if (_dxgiAdapter.IsNull())
    return false;

  _dxgiOutput = GetAdapterOutput(_dxgiAdapter);   
  Assert(_dxgiOutput);

  return _dxgiOutput.NotNull();
}

bool EngineDD11::InitDXGIAdapter()
{
  Assert(_dxgiFactory.NotNull());
  if (_dxgiFactory.IsNull())
    return false;

  int adapterIndex = D3D11Adapter;

  // NVPerfHUD not active by default
  _nvPerfHUDActive = false;

#ifdef DEBUG_PERFHUD
  UINT nAdapter = 0;
  ComRef<IDXGIAdapter> curAdapter;
  while (_dxgiFactory->EnumAdapters(nAdapter, curAdapter.Init()) !=
    DXGI_ERROR_NOT_FOUND)
  {
    if (curAdapter)
    {
      DXGI_ADAPTER_DESC adaptDesc;
      if (SUCCEEDED(curAdapter->GetDesc(&adaptDesc)))
      {
        if (wcscmp(adaptDesc.Description, L"NVIDIA PerfHUD") == 0)
        {
          _nvPerfHUDActive = true;
          _devType = D3D_DRIVER_TYPE_HARDWARE;
          _dxgiAdapter = curAdapter;
          adapterIndex = nAdapter;
          break;
        }
      }
    }
    ++nAdapter;
  }
#endif

  if (!_nvPerfHUDActive)
  {
    ComRef<IDXGIAdapter> curAdapter;

    if (adapterIndex < 0 || _dxgiFactory->EnumAdapters(adapterIndex, curAdapter.Init()) ==
      DXGI_ERROR_NOT_FOUND)
    {
      // auto-detect best adapter
      int index = 0;
      int maxLevel = 0;
      while (_dxgiFactory->EnumAdapters(index, curAdapter.Init()) !=
        DXGI_ERROR_NOT_FOUND)
      {
        if (curAdapter)
        {
          int level = GetAdapterLevel(curAdapter);
          if (level>maxLevel)
          {
            maxLevel = level;
            adapterIndex = index;
          }
        }

        index++;
      }
    }

    if (adapterIndex<0)
    {
      // if no adapter is recognized, select the first one
      adapterIndex = 0;
    }

    HRESULT hr = _dxgiFactory->EnumAdapters(adapterIndex, _dxgiAdapter.Init());
    hr; 
    Assert(SUCCEEDED(hr) && _dxgiAdapter.NotNull());

    _deviceLevel = GetAdapterLevel(_dxgiAdapter);
  }

  D3D11Adapter = adapterIndex;

  return _dxgiAdapter.NotNull();
}

int EngineDD11::GetAdapterLevel(IDXGIAdapter* adapter) const
{
  // TODO - for now it is just simple computation based on
  // supported interfaces and video memory
  const int interfaceLevelFactor = 1000000;

  int level = 0;

  Assert(adapter);
  if (!adapter)
    return level;

#ifdef _XBOX
  // for start let us assume Xbox is very weak device
  return 0x200;
#else
  // supported feature levels
  D3D_FEATURE_LEVEL supportedFeatureLevel = (D3D_FEATURE_LEVEL)0;

  LARGE_INTEGER dummy;
  // It seems that CheckInterfaceSupport does not work for ID3D11Device
  // So we try ID3D10Device and then try to create ID3D11Device
  HRESULT hr = adapter->CheckInterfaceSupport(__uuidof(ID3D10Device), &dummy);
  if (hr == S_OK)
  {
    // create device and check for features
    ComRef<ID3D11Device> device;
    ComRef<ID3D11DeviceContext> deviceContext;
    hr = D3D11CreateDevice(
      adapter,
      D3D_DRIVER_TYPE_UNKNOWN, //must be UNKNOWN, otherwise creating device will fail when adapter is non-NULL
      0,
      0,
      0,
      0,
      D3D11_SDK_VERSION,
      device.Init(),
      &supportedFeatureLevel,  
      deviceContext.Init()
      );

    deviceContext.Free();
    device.Free();
  }

  switch (supportedFeatureLevel)
  {
  case D3D_FEATURE_LEVEL_11_0:
    level = 3*interfaceLevelFactor;
    break;
  case D3D_FEATURE_LEVEL_10_1:
    level = 2*interfaceLevelFactor;
    break;
  case D3D_FEATURE_LEVEL_10_0:
    level = 1*interfaceLevelFactor;
    break;
  default:
    level = 0;
    break;
  }

  DXGI_ADAPTER_DESC desc;
  hr = adapter->GetDesc(&desc);
  if (FAILED(hr))
  {
    Fail("Failed to get adapter desc.");
    return level;
  }

  level += desc.DedicatedVideoMemory / (1024*1024);
  return level;
#endif  
}

void EngineDD11::Init()
{
  base::Init();
  Assert(_textBank);
  if (_textBank)
    _textBank->ResetMaxTextureQuality(true);
  // no need to do it here, it will be done later. Doing it here causes flush in EnableSBShadowsCommit
  //_textBank->InitDetailTextures();
}

void EngineDD11::Done()
{
  //release textures before TextBank is destroyed!
  //TODO: should it be done for all postprocesses?
  if(_pp._rain3D)
    _pp._rain3D->Cleanup();

  _vBufferToRelease.Clear();

  UnsetAllDiscardableResources();
  UnsetAllResources();

  if (!_windowed)
  {
	// go back from fullscreen if needed 
    if (!_d3dDeviceRec.IsNull())
      _d3dDeviceRec->SetFullscreenState(FALSE, NULL);
  }


  // perform destruction which might still submit more work to a worker thread
  _fonts.Clear();

  DeInitStateLists();
  DestroyPostProcessStuff();
  DeInitResources(true);
  DeInitD3D();
  DeInitDXGI();
  DeInitCursors();

  // Save config
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  if (ParseUserParams(cfg, &globals))
  {
    SaveConfig(cfg,GetConfigSection());
    SaveUserParams(cfg);
  }

  // Deinit application (unregister wnd class f.i.)
  AppDone(_hwndApp, _hInst);

  // Test whether there remains some memory allocated for index and vertex buffers
  Assert(_iBuffersAllocated==0);
  Assert(_vBuffersAllocated==0);
  //Assert(_rtAllocated==0);
  _caps._rtAllocated = 0;

  // Set the flag
  _initialized = false;
}

bool EngineDD11::CreateTextBank(ParamEntryPar cfg, ParamEntryPar fcfg)
{
  if (_textBank.IsNull())
  {
    _textBank = new TextBankD3D11(this, cfg, fcfg);
    if (_textBank.IsNull())
    {
      ErrorMessage("Cannot create texture bank.");
      return false;
    }
    // apply the settings which we only remembered while _textBank did not exists yet
    _textBank->SetTextureQuality(_textureQuality);
    _textBank->SetTextureMemory(_textureMemory);
  }
  return true;
}

void EngineDD11::DestroyTextBank()
{
  _textBank.Free();
}

void EngineDD11::InitVRAMPixelFormat( DXGI_FORMAT &pf, PacFormat format, bool isSRGB )
{
  // We don't need to differentiate non-srgb and srgb formats here, because
  // we crete both non-SRGB and SRGB views of textures and set them as needed.

  switch (format)
  {
  case PacDXT1:
      pf = DXGI_FORMAT_BC1_UNORM;
    break;
  case PacDXT2:
  case PacDXT3:
      pf = DXGI_FORMAT_BC2_UNORM;
    break;
  case PacDXT4:
  case PacDXT5:
      pf = DXGI_FORMAT_BC3_UNORM;
    break;
  case PacAI88:
    Assert(!isSRGB);
    pf = DXGI_FORMAT_R8G8_UNORM;
    break;
  case PacARGB8888:
    pf = DXGI_FORMAT_R8G8B8A8_UNORM;
    break;
  case PacARGB1555:
    Assert(!isSRGB);
    pf = DXGI_FORMAT_R8G8_UNORM;
    break;
  default:
    Fail("Texture has bad pixel format (VRAM).");
    break;
  }
}

void EngineDD11::DDError11( const char *text, int err )
{
  LogF("%s:%s",text,DXGetErrorString(err));
  if (GDebugger.IsDebugger())
  {
    BREAK();
  }
#if _ENABLE_REPORT
  int texUsed = _textBank ? _textBank->GetVRamTexAllocation() : 0;
  LogF("Total allocated: VB/IB/Tex: %d/%d/%d",_vBuffersAllocated,_iBuffersAllocated,texUsed);
  LogF("Mem Status: %s",GetMemErrorStatus());
#endif
}

void EngineDD11::ReportGRAM(const char *name)
{
  if (name) LogF("VRAM report: %s",name);

  // DX11 TODO
  // LUKE TEST
  Assert(_dxgiAdapter);
  if (!_dxgiAdapter)
    return;
  
  DXGI_ADAPTER_DESC adapterDesc;
  _dxgiAdapter->GetDesc(&adapterDesc);

  LogF("Dedicated video memory: %u ", adapterDesc.DedicatedVideoMemory);
  LogF("Dedicated system memory: %u ", adapterDesc.DedicatedSystemMemory);
  LogF("Shared system memory: %u ", adapterDesc.SharedSystemMemory);
  // DX11 TODO
  //if (name) LogF("VRAM report: %s",name);
  //UINT dwFree = _d3dImmediateDeviceContext->GetAvailableTextureMem();
  //if (name) LogF("  VRAM free %u",dwFree);
}


/// Stream with ready data, prefixed with #line 1 filename line prefix
class QOStrStreamDataPrefixedLine: public LazyLoadFile
{
  mutable QOStrStreamData _stream;
  mutable bool _loaded;

  const char *_fileName;

  void WriteString(const char *string) const
  {
    _stream.write(string,strlen(string));
  }

  void DoLoad() const
  {
    _loaded = true;
    // first write a prefix line: #line 1 "filename"
    WriteString("#line 1 \"");
    WriteString(_fileName);
    WriteString("\"\r\n");
    // then append a file
    _stream.CopyFile(_fileName);
  }

  QOStrStreamData &Load() const {if (!_loaded) DoLoad();return _stream;}

public:
  QOStrStreamDataPrefixedLine(const char *fileName):_loaded(false),_fileName(fileName)
  {
  }

  virtual const char *Load(int &size) const
  {
    Load();
    size = _stream.pcount();
    return _stream.str();
  }
};

// simplified adding of whole lines
#define PS_VARIANTS_EX(name,nameAlpha) \
{PSPairHL(name),PSPairHL(SSSM##name),PSPairHL(SR##name),PSPairHL(ShadowBufferAlpha),PSPairHL(InvDepthAlphaTest),PSPairHL(Thermal##name),PSPairHL(nameAlpha)}
#define PS_VARIANTS(name) PS_VARIANTS_EX(name,AlphaOnly)
// simplified adding of whole lines
#define PS_VARIANTSMOD(name) PS_VARIANTS_EX(name,AlphaOnlyMod)
#define PS_VARIANTSMODATOC(name) PS_VARIANTS_EX(name,AlphaOnlyModAToC)
#define PS_VARIANTSTREE(name) PS_VARIANTS_EX(name,AlphaOnlyTree)
#define PS_VARIANTSTREEATOC(name) PS_VARIANTS_EX(name,AlphaOnlyTreeAToC)
#define PS_VARIANTSTREEADV(name) PS_VARIANTS_EX(name,AlphaOnlyTreeAdv)
#define PS_VARIANTSTREEADVATOC(name) PS_VARIANTS_EX(name,AlphaOnlyTreeAdvAToC)
// simplified adding of whole lines
#define PS_NOVARIANTS(name) \
{PSPairHL(name),PSPairHL(name),PSPairHL(name),PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest),PSPairHL(name),PSPairHL(AlphaOnly)}
// simplified adding of whole lines
#define PS_NOVARIANTSSIMPLE(name) \
{PSPairHL(name),PSPairHL(name),PSPairHL(name),PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest),PSPairHL(name),PSPairHL(name)}
// Just MRT variants created (suitable f.i. for craters)
#define PS_MRTVARIANTS(name) \
{PSPairHL(name),PSPairHL(name),PSPairHL(name),PSPairHL(ShadowBufferAlpha),PSPairHL(InvDepthAlphaTest),PSPairHL(Thermal##name),PSPairHL(AlphaOnly)}
/// simplified adding of whole lines
#define PS_DUMMY {PSPairDummy,PSPairDummy,PSPairDummy,PSPairDummy,PSPairDummy,PSPairDummy,PSPairDummy}
/// all variants names
#define PS_VARIANTS_FULL(name,nameSSSM,nameSR,nameSBA,nameDepth,nameThermal,nameAlpha ) \
{ PSPairHL(name),PSPairHL(nameSSSM),PSPairHL(nameSR),PSPairHL(nameSBA),PSPairHL(nameDepth),PSPairHL(nameThermal),PSPairHL(nameAlpha) }

/// extend PS_VARIANTS_EX about thermal shader parameter
#define PS_VARIANTS_THERMAL_EX(name,nameThermal,nameAlpha) \
{PSPairHL(name),PSPairHL(SSSM##name),PSPairHL(SR##name),PSPairHL(ShadowBufferAlpha),PSPairHL(InvDepthAlphaTest),PSPairHL(nameThermal),PSPairHL(nameAlpha)}
/// extend PS_VARIANTS about thermal shader argument
#define PS_VARIANTS_THERMAL(name,nameThermal) PS_VARIANTS_THERMAL_EX(name,nameThermal,AlphaOnly)
/// extend PS_VARIANTSMOD
#define PS_VARIANTSMOD_THERMAL(name,nameThermal) PS_VARIANTS_THERMAL_EX(name,nameThermal,AlphaOnlyMod)
/// extend PS_NOVARIANTS
#define PS_NOVARIANTS_THERMAL(name, nameThermal) \
{PSPairHL(name),PSPairHL(name),PSPairHL(name),PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest),PSPairHL(nameThermal),PSPairHL(AlphaOnly)}
// Special thermal version for no-variant shaders (suitable f.i. for sky)
#define PS_NOVARIANTS_THERMAL_DUMMY(name) \
{PSPairHL(name),PSPairHL(name),PSPairHL(name),PSPairHL(ShadowBufferAlpha),PSPairHL(InvDepthAlphaTest),PSPairDummy,PSPairHL(AlphaOnly)}

/// adding of a terrain shader family
#define TERRAIN_LINES(XXXX,base) \
  XXXX(base##15),XXXX(base##15),XXXX(base##15), \
  XXXX(base##15),XXXX(base##15),XXXX(base##15),XXXX(base##15), \
  XXXX(base##15),XXXX(base##15),XXXX(base##15),XXXX(base##15), \
  XXXX(base##15),XXXX(base##15),XXXX(base##15),XXXX(base##15)

#define CRATER_LINES \
  PS_MRTVARIANTS(Crater1), PS_MRTVARIANTS(Crater2), PS_MRTVARIANTS(Crater3), PS_MRTVARIANTS(Crater4), PS_MRTVARIANTS(Crater5), \
  PS_MRTVARIANTS(Crater6), PS_MRTVARIANTS(Crater7), PS_MRTVARIANTS(Crater8), PS_MRTVARIANTS(Crater9), PS_MRTVARIANTS(Crater10), \
  PS_MRTVARIANTS(Crater11),PS_MRTVARIANTS(Crater12),PS_MRTVARIANTS(Crater13),PS_MRTVARIANTS(Crater14)

#define PSPairHL(name) {"PS" #name}
#define PSPairDummy {NULL}

static const PSName PSNames[NPixelShaderID][NPixelShaderType] =
{
  PS_VARIANTSMOD_THERMAL(SpecularAlpha, SpecularAlphaThermal),
  PS_VARIANTSMOD(NormalDXTA),
  PS_VARIANTSMOD(NormalMap),
  PS_VARIANTSTREE(SpecularNormalMapThrough),
  PS_VARIANTSMOD(SpecularNormalMapGrass),
  PS_VARIANTSMOD(SpecularNormalMapDiffuse),
  PS_VARIANTSMOD(DetailSpecularAlpha),
  PS_NOVARIANTS_THERMAL(Interpolation, ThermalInterpolation), // use for sky, no shadow buffer support
  PS_VARIANTS(Water),
  PS_NOVARIANTS(WaterSimple),
  PS_NOVARIANTSSIMPLE(White), // special purpose shader (shadow volume) - no shadow buffer support, simple alpha-only
  PS_NOVARIANTSSIMPLE(WhiteAlpha), // special purpose shader (projected shadows) - no shadow buffer support, simple alpha-only
  PS_NOVARIANTS(Reflect), // 2nd pass reflection - no shadow buffer support
  PS_NOVARIANTS(ReflectNoShadow), // 2nd pass reflection - no shadow buffer support
  PS_DUMMY,
  PS_VARIANTS(DetailSpecularAlphaMacroAS),
  PS_VARIANTS(NormalMapMacroAS),
  PS_VARIANTS(SpecularNormalMapDiffuseMacroAS),
  PS_VARIANTS(NormalMapSpecularMap),
  PS_VARIANTS(NormalMapDetailSpecularMap),
  PS_VARIANTS(NormalMapMacroASSpecularMap),
  PS_VARIANTS(NormalMapDetailMacroASSpecularMap),
  PS_VARIANTSMOD_THERMAL(NormalMapSpecularDIMap, NormalMapSpecularDIMapThermal), //PS_VARIANTS(NormalMapSpecularDIMap),
  PS_VARIANTS(NormalMapDetailSpecularDIMap),
  PS_VARIANTS(NormalMapMacroASSpecularDIMap),
  PS_VARIANTS(NormalMapDetailMacroASSpecularDIMap),
  TERRAIN_LINES(PS_VARIANTS,Terrain),
  TERRAIN_LINES(PS_VARIANTS,TerrainSimple),
  PS_VARIANTSMOD(Glass),
  PS_NOVARIANTS(NonTL), // special purpose shader, no shadow buffer support
  PS_VARIANTSTREE(SpecularNormalMapSpecularThrough),
  PS_VARIANTSMOD(Grass),
  PS_VARIANTSTREE(SpecularNormalMapThroughSimple), //PS_VARIANTSTREE(SpecularNormalMapThroughSimple),
  PS_VARIANTSTREE(SpecularNormalMapSpecularThroughSimple),
  PS_VARIANTS(Road),
  PS_NOVARIANTS(Shore),
  PS_NOVARIANTS(ShoreWet),
  PS_VARIANTS(Road2Pass),
  PS_NOVARIANTS(ShoreFoam),
  PS_NOVARIANTS(NonTLFlare),
  PS_DUMMY, // was NormalMapThroughLowEnd
  TERRAIN_LINES(PS_VARIANTSMOD,TerrainGrass),
  CRATER_LINES,
  PS_VARIANTS(Sprite),
  PS_VARIANTS(SpriteSimple),
  PS_NOVARIANTS_THERMAL(Cloud, CloudThermal),//PS_NOVARIANTS_THERMAL_DUMMY(Cloud),
  PS_NOVARIANTS_THERMAL(Horizon, HorizonThermal),
  PS_VARIANTSMOD_THERMAL(Super, SuperThermal),
  PS_VARIANTSMOD(Multi),
  PS_VARIANTS(TerrainX),
  PS_VARIANTS(TerrainSimpleX),
  PS_VARIANTS_EX(TerrainGrassX,TerrainGrassAlphaX),
  PS_VARIANTSTREE(Tree),
  PS_VARIANTSMOD(TreePRT),
  PS_VARIANTSTREE(TreeSimple),
  PS_VARIANTSMOD_THERMAL(Skin, ThermalSkin),
  PS_VARIANTS_FULL(CalmWater, SSSMCalmWater, SRCalmWater, Empty, Empty, CalmWaterTi, Empty),
  PS_VARIANTSTREEATOC(TreeAToC),
  PS_VARIANTSMODATOC(GrassAToC),
  PS_VARIANTSTREEADV(TreeAdv),
  PS_VARIANTSTREEADV(TreeAdvSimple),
  PS_VARIANTSTREEADV(TreeAdvTrunk),
  PS_VARIANTSTREEADV(TreeAdvTrunkSimple),
  PS_VARIANTSTREEADVATOC(TreeAdvAToC),
  PS_VARIANTSTREEADVATOC(TreeAdvSimpleAToC),
  PS_VARIANTSTREEADV(TreeSN),
  PS_VARIANTS(SpriteExtTi),
  PS_VARIANTS(TerrainSNX),
#if _ARMA3_SIMUL_WEATHER
  PS_NOVARIANTS(SimulWeatherClouds),  // SimulWeather clouds
  PS_NOVARIANTS(SimulWeatherCloudsWithLightning),  // SimulWeather clouds with lightning
  PS_NOVARIANTS(SimulWeatherCloudsCPU), // SimulWeather clouds with CPU distance fading
  PS_NOVARIANTS(SimulWeatherCloudsWithLightningCPU), // SimulWeather clouds with lightning and CPU distance fading
#else
  PS_DUMMY,
  PS_DUMMY,
  PS_DUMMY,
  PS_DUMMY,
#endif
  PS_VARIANTSMOD(SuperExt),
  PS_VARIANTSMODATOC(SuperAToC),
  PS_DUMMY, // NonTLHeatHaze has no DX11 pixel shader; keep PixelShaderID alignment.
  PS_NOVARIANTS_THERMAL(InterpolationAlpha, ThermalInterpolationAlpha), // use for sky clouds
  PS_VARIANTS(VolCloud),
  PS_VARIANTS(VolCloudSimple),
  PS_NOVARIANTS_THERMAL(UnderwaterOcclusion, UnderwaterOcclusionThermal),
  PS_VARIANTSTREEADV(SuperHair),  /// we can use tree adv computation for super hair shader (since we are using same function to compute alpha)
  PS_VARIANTSTREEADVATOC(SuperHairAtoC),  /// we can use tree adv computation for super hair shader (since we are using same function to compute alpha)
  PS_NOVARIANTS(Caustics),  // Caustics shader
  PS_VARIANTS(Refract),  // Refract shader - _ARMA3_REFRACTION
  PS_VARIANTS(SpriteRefract), //_ARMA3_REFRACTION_SPRITES
  PS_VARIANTS(SpriteRefractSimple) //_ARMA3_REFRACTION_SPRITES
};

HRESULT EngineDD11::HLSLInclude::Open(
                                     D3D10_INCLUDE_TYPE includeType, LPCSTR fileName,
                                     LPCVOID parentData,
                                     LPCVOID * data, UINT * bytes
                                     )
{
  // any shader files should be search for in the bin directory
  BString<512> fullName;
  if (fileName[0]=='.' && fileName[1]=='.' && fileName[2]=='\\')
  {
    const char *prevFolder = strrchr(HLSLDir,'\\');
    // if it is the terminating character, we need to search on
    if (prevFolder[1]==0)
    {
      // search for the previous backslash
      const char *beg = HLSLDir;
      while (prevFolder>beg)
      {
        prevFolder--;
        if (*prevFolder=='\\') break;
      }
    }
    // 
    if (prevFolder)
    {
      // copy up to directory, not including the backslash before it
      int prevSize = prevFolder-HLSLDir;
      strncpy(fullName,HLSLDir,prevSize);
      fullName[prevSize]=0;
    }
    // skip .. part of the filename
    // backslash will be used from the filename, not from the HLSLDir
    fileName += 2;
  }
  else
  {
    strcpy(fullName,HLSLDir);
  }

  // filename may begin with ..
  // in such case we need to shorten the HLSLDir and skip the ..
  // otherwise the resulting patch is invalid

  strcat(fullName,fileName);
#ifdef _XBOX
  strncpy(fullPath,fullName,fullPathSize);
  fullPath[fullPathSize-1] = 0;
#endif
  QIFStreamB in;
  in.AutoOpen(fullName);
  if (in.fail())
  {
    RptF("Cannot open HLSL include %s",cc_cast(fullName));
    data = NULL;
    bytes = 0;
    return S_FALSE;
  }
  int size = in.rest();
  in.PreReadSequential();
  char *buf = new char[size];
  int rd = in.read(buf,size);
  if (rd!=size)
  {
    delete[] buf;
    data = NULL;
    bytes = 0;
    return S_FALSE;
  }
  *data = buf;
  *bytes = size;
  return S_OK;
}

HRESULT EngineDD11::HLSLInclude::Close(LPCVOID data)
{
  if (data)
  {
    delete[] (char *)data;
  }
  return S_OK;
}

bool EngineDD11::InitPixelShaders(PixelShaderID from, PixelShaderID to)
{
  EndHighLevelRecording();

#if _ENABLE_REPORT
  DWORD start = ::GetTickCount();
#endif
  Debugger::PauseCheckingScope scope(GDebugger);

  D3DXMacroList definesBasic;
  CreateBasicDefines(definesBasic, false);

  // Go through the shaders and it's types and compile them
  for (int t=0; t<NPixelShaderType; t++)
  {
    // Choose the source file according to type
    RString sourceFileName;
    if (t == PSTBasic)
    {
      sourceFileName = PSBasicFile;
    }
    else if (t == PSTBasicSSSM)
    {
      sourceFileName = PSBasicSSSMFile;
    }
    else if (t == PSTShadowReceiver1)
    {
      sourceFileName = PSSR_DefaultFile;
    }
    else if (t == PSTShadowCaster)
    {
      sourceFileName = PSFile;
    }
    else if (t == PSTInvDepthAlphaTest)
    {
      sourceFileName = PSFile;
    }
    else if (t == PSTThermal)
    {
      sourceFileName = PSThermalFile;
    }
    else if (t == PSTAlphaOnly)
    {
      sourceFileName = PSFile;
    }
    else
    {
      Fail("Error: Unhandled pixel shader type");
    }

    // Use the source file
    QOStrStreamDataPrefixedLine hlslSource(sourceFileName);

    // Go through the individual shaders
    for (int i=from; i<=to; i++)
    {
      D3DXMacroList defines = definesBasic;

      const PSName &psName = PSNames[i][t];
      int index = _psCache.FindKey(psName);
      if (index>=0)
      {
        _pixelShader[i][t] = _psCache[index].ps;
      }
      else
      {
        const DWORD *psData = NULL;
        DWORD psDataSize = -1;
        D3DXCompileShader_Result result;
        if (psName.name != NULL)
        {
          const char *name = psName.name;
          int flags = D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY; //D3DXSHADER_PARTIALPRECISION;
          BString<256> psFullName;
          switch (t)
          {
          case PSTShadowCaster:
            strcpy(psFullName,name);
            strcat(psFullName,"_Default");
            break;
          case PSTShadowReceiver1:
            strcpy(psFullName,name);
            // some shader may be unused / undefined
            // we cannot add suffix to them
            if (strncmp(name,"PSSR",4)==0)
            {
                flags = D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY;
                strcat(psFullName,"_Default");
            }
            break;
          default:
            strcpy(psFullName,name);
            break;
          }

#if _ARMA3_SIMUL_WEATHER
          // add defines for simulweather shaders
          PixelShaderID pixelShaderId = (PixelShaderID)i;
          switch (pixelShaderId)
          {
          case PSSimulWeatherClouds:
          case PSSimulWeatherCloudsWithLightning:
            SetMacro(defines.Append(),"FADE_MODE","1");
            SetMacro(defines.Append(),"DETAIL_NOISE","1");
            break;
          case PSSimulWeatherCloudsCPU:
          case PSSimulWeatherCloudsWithLightningCPU:
            SetMacro(defines.Append(),"FADE_MODE","0");
            SetMacro(defines.Append(),"DETAIL_NOISE","1");
            break;
          }
#endif

          // close defines
          SetMacro(defines.Append(),NULL,NULL);

          ComRef<ID3D10Blob> errorMsgs;         

          HRESULT ok = D3DX11CompileShader_Cached
            (
              hlslSource,
              0,
              defines.Data(),
              &_hlslInclude,
              psFullName,
              _caps._psVersion,
              flags,
              0,
              NULL,
              NULL,
              result
            );

          if (FAILED(ok))
          {
            RptF(
              "DX11 shader compilation failed (%x) for %s:%d (%s)",
              ok, cc_cast(psFullName), t, cc_cast(sourceFileName)
              );
            if (result.errorMsgs.NotNull())
            {
              ReportShaderErrors(result.errorMsgs);

              // if there is already some shader it means we are reloading shader only
              // and we may ignore the error
              if (_pixelShader[i][t].IsNull())
              {
                ErrorMessage("Error compiling pixel shader %s:%d",cc_cast(psFullName), t);
              }
            }
          }
          else
          {
            psData = (const DWORD *)result.shader->GetBufferPointer();
            psDataSize = result.shader->GetBufferSize();
          }
        }
        if (psData)
        {
          ComRef<ID3D11PixelShader> ps;
          HRESULT hr = _d3dDeviceRec->CreatePixelShader(psData, psDataSize, NULL, ps.Init());
          if (
            // some pixels shader combinations are never used, we do not mind if we fail creating them
            !(t == PSTShadowReceiver1 && (i==PSTerrainX || i==PSTerrainSNX ))
            && (FAILED(hr) || ps.IsNull())
            )
          {
            RStringB psEnumName = FindEnumName(PixelShaderID(i));
            ErrF("Cannot create pixel shader %s in %s, error %x",cc_cast(psEnumName),cc_cast(sourceFileName),hr);
            return false;
          }
          else
          {
            int cid = _psCache.Add();
            PSCachedItem &item = _psCache[cid];
            item.name = psName;
            item.ps = ps;
            _pixelShader[i][t] = ps;
          }
        }
      }
    }
  }

  // Set a basic pixel shader (for example on XBOX a PS must be set before setting of PS constant)
  SelectPixelShader(-1, PSNormal);
  SelectPixelShaderType(-1, PSTBasic);
#ifndef _X360SHADERGEN
  DoSelectPixelShader(-1);
#endif
#if _ENABLE_REPORT
  DWORD time = ::GetTickCount()-start;
  LogF("InitPixelShaders: %d ms",time);
#endif

  return true;
}

void EngineDD11::CreateBasicDefines(D3DXMacroList &list, bool close)
{
  /* It is not required to define _XBOX - it's being defined by the shader compiler automatically
  #ifdef _XBOX
  SetMacro(list.Append(),"_XBOX","1");
  #else
  SetMacro(list.Append(),"_PC","1");
  #endif
  */

#if _VBS3_SM2 && !defined(_DX11_ONLY)
  if (_caps._sm2Used)
  {
    SetMacro(list.Append(),"SHADER_MODEL","0x200");
    SetMacro(list.Append(),"SHADER_MODEL_NAME","_2_0");
    SetMacro(list.Append(),"_SM_2_0","1");
    SetMacro(list.Append(),"_SM2","1");
  }
  else
#endif
  {
    SetMacro(list.Append(),"SHADER_MODEL","0x300");
    SetMacro(list.Append(),"SHADER_MODEL_NAME","_3_0");
    SetMacro(list.Append(),"_SM_3_0","1");
  }
#if SHADER_DBG_LEVEL>0
#define STRINGIZE_X(x) #x
#define STRINGIZE(x) STRINGIZE_X(x)
  SetMacro(list.Append(),"_DEBUG",STRINGIZE(SHADER_DBG_LEVEL));
#endif

#if _VBS3
  SetMacro(list.Append(),"_VBS3","1");
#endif

#if _VBS3_PERPIXELPSLIGHTS
  if (!_caps._sm2Used)
  {
    SetMacro(list.Append(),"_VBS3_PERPIXELPSLIGHTS","1");
  }
#endif

#if _VBS3_MORETERRAINLODS
  SetMacro(list.Append(),"_VBS3_MORETERRAINLODS_SHADER","1");
#endif

#if _VBS3_CPU_LAND_LIGHT
  SetMacro(list.Append(),"_VBS3_CPU_LAND_LIGHT_SHADER","1");
#endif

#if _VBS3_LAND_SHADOW
  SetMacro(list.Append(),"_VBS3_LAND_SHADOW_SHADER","1");
#endif

#if _VBS3_NO_SEABED
  SetMacro(list.Append(),"_VBS3_NO_SEABED_SHADER","1");
#endif


  if (close)
  {
    SetMacro(list.Append(),NULL,NULL);
  }
}

// TODO: pass defines in a symbolic binary form instead to reduce memory allocations
static void CreateShaderName(VSName &name, const char *pFunctionName, CONST D3D10_SHADER_MACRO *pDefines)
{
  name.name = pFunctionName;
  while (pDefines && pDefines->Name)
  {
    name.defines.Add(pDefines->Name);
    name.defines.Add(pDefines->Definition);
    pDefines++;
  }
}

void EngineDD11::CreateShader(RString filename, CONST D3D10_SHADER_MACRO *pDefines,
                             LPCSTR pFunctionName, LPCSTR pProfile, DWORD flags,
                             const VertexDeclaration &vertDecl,
                             VertexShaderInfo &vertexShader)
{
  VSName name;
  CreateShaderName(name,pFunctionName,pDefines);
  const VSCachedItem &item = _vsCache.Get(name);
  if (_vsCache.NotNull(item))
  {
    vertexShader = item.vs;
    return;
  }
  QOStrStreamDataPrefixedLine stream(filename);
  CreateShader(stream,pDefines,pFunctionName,pProfile,flags, vertDecl, vertexShader);
}

void EngineDD11::CreateShader(LazyLoadFile &hlslSource, CONST D3D10_SHADER_MACRO *pDefines,
                             LPCSTR pFunctionName, LPCSTR pProfile, DWORD flags,
                             const VertexDeclaration &vertDecl,
                             VertexShaderInfo &vertexShader)
{
  VSName name;
  CreateShaderName(name,pFunctionName,pDefines);
  const VSCachedItem &item = _vsCache.Get(name);
  if (_vsCache.NotNull(item))
  {
    vertexShader = item.vs;
    return;
  }

  // Compile shader
  ComRef<ID3D10Blob> shader;
  ComRef<ID3D10Blob> errorMsgs;

  D3DXCompileShader_Result result;
  HRESULT hr = D3DX11CompileShader_Cached
    (
    hlslSource,
    0,
    pDefines,
    &_hlslInclude,
    pFunctionName,
    _caps._vsVersion,
    flags,
    0,
    NULL,
    NULL,
    result
    );

  // Create shader and save it
  if (FAILED(hr))
  {
    LogF("DX11 shader compilation failed (%x) for vertex shader %s", hr, pFunctionName);
    if (result.errorMsgs.NotNull())
    {
      ReportShaderErrors(result.errorMsgs);
      if (vertexShader._shader.IsNull())
      {
        ErrorMessage("Error compiling vertex shader %s", pFunctionName);
      }
    }
  }
  else
  {
    const DWORD *vsData;
    vsData = (const DWORD *)result.shader->GetBufferPointer();
    DWORD vsDataSize = result.shader->GetBufferSize();
    ComRef<ID3D11VertexShader> vs;
    HRESULT hr = _d3dDeviceRec->CreateVertexShader(vsData, vsDataSize, 0, vs.Init());

    if (FAILED(hr) || vs.IsNull())
    {
      ErrF("Cannot create vertex shader %s, error %x", pFunctionName, hr);
    }
    else
    {
      // Create input layout for vertex shader
      vertexShader._inputLayout = GetInputLayout(vertDecl, result.shader, pFunctionName);
#if _ENABLE_REPORT
      vertexShader._declaration = vertDecl;
#endif
      vertexShader._shader = vs;
      VSCachedItem save;
      save.name = name;
      save.vs = vertexShader;
      _vsCache.Add(save);
    }
  }
}

ComRef<ID3D11InputLayout> EngineDD11::GetInputLayout(const VertexDeclaration &vertDecl, ComRef<ID3D10Blob>& shader, LPCSTR pFunctionName)
{
  DoAssert(vertDecl.IsValid());

  // first try to find input layout
  ComRef<ID3D11InputLayout>* foundItem = 0;
  if (vertDecl.GetInputElements() == VertDeclNonTL.GetInputElements())
  {
    foundItem = &_vdNonTL;
  }
  else
  {
    //hash map would be better, but number of declarations so quite small, so
    //linear searching should be ok
    for (int i=0; i<VDCount; ++i)
    {
      for (int j=0; j<VSDVCount; ++j)
      {
        if (vertDecl.GetInputElements() == VertexDeclsDX11[i][j].GetInputElements())
        {
          foundItem = &(_vertexDeclD[i][j]);
          break;
        }
      }
      if (foundItem)
        break;
    }
  }

  Assert(foundItem);
  if (!foundItem)
  {
    Fail("Invalid input layout!");
    return ComRef<ID3D11InputLayout>();
  }

  if (foundItem->NotNull())
    return *foundItem;

  // create new layout
  HRESULT hr = _d3dDeviceRec->CreateInputLayout(vertDecl.GetInputElements(), 
    vertDecl.GetSize(), shader->GetBufferPointer(), shader->GetBufferSize(), 
    (*foundItem).Init());
  if (FAILED(hr))
  {
    RptF("Error: Creating input layout for shader %s failed: %s", pFunctionName, DXGetErrorString(hr));
    ErrorMessage("Error: Creating input layout for shader %s failed : %s", pFunctionName, DXGetErrorString(hr));      
  }

  return *foundItem;
}

bool EngineDD11::InitVertexShaders(VertexShaderID vsID)
{
  EndHighLevelRecording();

#if _ENABLE_REPORT
  DWORD start = ::GetTickCount();
#endif
  //vsID = VSTreePRT;
  // Create a compilation flag
  int flags = 0;
  flags |= D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY;

  D3DXMacroList definesBasic;
  CreateBasicDefines(definesBasic,false);

  LogF("Creating special purpose shaders");
  static const RString VSVersion = "vs_4_0";
  // Terrain
  if (vsID==NVertexShaderID || vsID==VSTerrain)
  {

    // Create the shader
    for (int rm = 0; rm < SHP_RenderingMode; rm++)
    {
      // copy and complete defines
      D3DXMacroList defines = definesBasic;
      BString<32> rmNumber;
      sprintf(rmNumber,"%d",rm);
      SetMacro(defines.Append(),FindEnumName(PRGDEFRenderingMode),rmNumber);
      SetMacro(defines.Append(),NULL,NULL);
      CreateShader(VSTerrainFile, defines.Data(), "VSTerrain", VSVersion, flags, 
        VertexDeclsDX11[ShaderVertexDeclarations[VSTerrain]][VSDV_Basic], 
        _vertexShaderTerrain[rm]);
    }
  }

  // Terrain grass layer
  if (vsID==NVertexShaderID || vsID==VSTerrainGrass)
  {

    // Create the shader
    for (int rm = 0; rm < SHP_RenderingMode; rm++)
    {
      // copy and complete defines
      D3DXMacroList defines = definesBasic;
      BString<32> rmNumber;
      sprintf(rmNumber,"%d",rm);
      SetMacro(defines.Append(),FindEnumName(PRGDEFRenderingMode),rmNumber);
      SetMacro(defines.Append(),NULL,NULL);
      CreateShader(VSTerrainGrassFile, defines.Data(), "VSTerrainGrass", VSVersion, flags, 
        VertexDeclsDX11[ShaderVertexDeclarations[VSTerrainGrass]][VSDV_Basic],
        _vertexShaderTerrainGrass[rm]);
    }
  }

  // Road
  if (vsID==NVertexShaderID || vsID==VSRoad)
  {

    // Create the shader
    for (int rm = 0; rm < SHP_RenderingMode; rm++)
    {
      // copy and complete defines
      D3DXMacroList defines = definesBasic;
      BString<32> rmNumber;
      sprintf(rmNumber,"%d",rm);
      SetMacro(defines.Append(),FindEnumName(PRGDEFRenderingMode),rmNumber);
      SetMacro(defines.Append(),NULL,NULL);
      CreateShader(VSRoadFile, defines.Data(), "VSRoad", VSVersion, flags, 
        VertexDeclsDX11[ShaderVertexDeclarations[VSRoad]][VSDV_Basic],
        _vertexShaderRoad[rm]);
    }
  }

  { // other special purpose shaders

    // copy and complete defines
    D3DXMacroList defines = definesBasic;
    SetMacro(defines.Append(),NULL,NULL);

    // Sprite
    if (vsID==NVertexShaderID || vsID==VSSprite)
    {
      CreateShader(VSSpriteFile, defines.Data(), "VSSprite", VSVersion, flags,
        VertexDeclsDX11[ShaderVertexDeclarations[VSSprite]][VSDV_Instancing],
        _vertexShaderSpecial[VSSSprite]);
    }

    // SpriteOnSurface
    if (vsID==NVertexShaderID || vsID==VSSpriteOnSurface)
    {
      CreateShader(VSSpriteFile, defines.Data(), "VSSpriteOnSurface", VSVersion, flags,
        VertexDeclsDX11[ShaderVertexDeclarations[VSSpriteOnSurface]][VSDV_Instancing],
        _vertexShaderSpecial[VSSSpriteOnSurface]);
    }


    // Water
    if (vsID==NVertexShaderID || vsID==VSWater)
    {
      CreateShader(VSWaterFile, defines.Data(), "VSWater", VSVersion, flags, 
        VertexDeclsDX11[ShaderVertexDeclarations[VSWater]][VSDV_Basic], 
        _vertexShaderSpecial[VSSWater]);
    }

    // WaterSimple
    if (vsID==NVertexShaderID || vsID==VSSWaterSimple)
    {
      CreateShader(VSWaterFile, defines.Data(), "VSWater", VSVersion, flags, 
        VertexDeclsDX11[ShaderVertexDeclarations[VSWaterSimple]][VSDV_Basic], 
        _vertexShaderSpecial[VSSWaterSimple]);
    }

    // Shore
    if (vsID==NVertexShaderID || vsID==VSShore)
    {
      CreateShader(VSShoreFile, defines.Data(), "VSShore", VSVersion, flags, 
        VertexDeclsDX11[ShaderVertexDeclarations[VSShore]][VSDV_Basic],
        _vertexShaderSpecial[VSSShore]);
    }

    // Point
    if (vsID==NVertexShaderID || vsID==VSPoint)
    {
      CreateShader(VSPointFile, defines.Data(), "VSPoint", VSVersion, flags, 
        VertexDeclsDX11[ShaderVertexDeclarations[VSPoint]][VSDV_Basic], 
        _vertexShaderSpecial[VSSPoint]);
    }

    // Shadow volume
    if (vsID==NVertexShaderID || vsID==VSShadowVolume)
    {
      CreateShader(VSShadowVolumeFile, defines.Data(), "VSShadowVolume",          VSVersion, flags, 
        VertexDeclsDX11[ShaderVertexDeclarations[VSShadowVolume]][VSDV_Basic], 
        _vertexShaderShadowVolume[0]);
      CreateShader(VSShadowVolumeFile, defines.Data(), "VSShadowVolumeSkinned",   VSVersion, flags, 
        VertexDeclsDX11[ShaderVertexDeclarations[VSShadowVolume]][VSDV_Skinning], 
        _vertexShaderShadowVolume[1]);
      CreateShader(VSShadowVolumeFile, defines.Data(), "VSShadowVolumeInstanced", VSVersion, flags, 
        VertexDeclsDX11[ShaderVertexDeclarations[VSShadowVolume]][VSDV_Instancing], 
        _vertexShaderShadowVolume[2]);
    }
    // CalmWater
    if( vsID==NVertexShaderID || vsID==VSCalmWater )
    {
      CreateShader(VSCalmWaterFile, defines.Data(), "VSCalmWater", VSVersion, flags, 
        VertexDeclsDX11[ShaderVertexDeclarations[VSCalmWater]][VSDV_Basic], 
        _vertexShaderSpecial[VSSCalmWater]);
    }

    // Volumetric cloud
    if (vsID==NVertexShaderID || vsID==VSVolCloud)
    {
      CreateShader(VSVolCloudFile, defines.Data(), "VSVolCloud", VSVersion, flags, 
        VertexDeclsDX11[ShaderVertexDeclarations[VSVolCloud]][VSDV_Instancing], 
        _vertexShaderSpecial[VSSVolCloud]);
    }

#if _ARMA3_SIMUL_WEATHER
    // Simul Weather
    if (vsID==NVertexShaderID ||  vsID==VSSimulWeatherClouds)
    {
      // setup fade mode to fragment
      D3DXMacroList defines = definesBasic;
      SetMacro(defines.Append(),"FADE_MODE","1");
      SetMacro(defines.Append(),NULL,NULL);
      CreateShader(VSSimulWeatherFile, defines.Data(), "VSSimulWeatherClouds", VSVersion, flags,
        VertexDeclsDX11[ShaderVertexDeclarations[VSSimulWeatherClouds]][VSDV_Basic],
        _vertexShaderSpecial[VSSSimulWeatherClouds]);
    }
    if (vsID==NVertexShaderID ||  vsID==VSSimulWeatherCloudsCPU)
    {
      // setup fade mode to CPU
      D3DXMacroList defines = definesBasic;
      SetMacro(defines.Append(),"FADE_MODE","0");
      SetMacro(defines.Append(),NULL,NULL);
      CreateShader(VSSimulWeatherFile, defines.Data(), "VSSimulWeatherClouds", VSVersion, flags,
        VertexDeclsDX11[ShaderVertexDeclarations[VSSSimulWeatherCloudsCPU]][VSDV_Basic],
        _vertexShaderSpecial[VSSSimulWeatherCloudsCPU]);
    }
#endif

  }

  // Common shaders
  {
    // Get the HLSL source stream
    QOStrStreamDataPrefixedLine hlslSource(VSFile);

    // Prepare array of defines to be passed to the compilation
    // complete defines, reserve spaces for variable fields
    D3DXMacroList defines = definesBasic;
    int base = defines.Size();
    defines.Resize(base+NPRGDef11);      
    SetMacro(defines.Append(),NULL,NULL);

    // Create all possible compilation combinations
    for (int vs = 0; vs < SHP_VertexShaderID; vs++)
    {
      // If the vertex shader is not the one we want to compile, then continue
      if (vsID < NVertexShaderID && vs != VSToVSP[vsID]) continue;

      RStringB vsName = FindEnumName((VertexShaderPoolID)vs);
      LogF("Creating generic shader %s : %d (of %d)",cc_cast(vsName),vs,SHP_VertexShaderID);

      defines[base+PRGDEFName].Name = FindEnumName(PRGDEFName);
      defines[base+PRGDEFName].Definition = vsName;

      for (int uvi = 0; uvi < SHP_UVInput; uvi++)
      {
        static const char *uviName[SHP_UVInput] = {"TEXCOORD0", "TEXCOORD1"};
        defines[base+PRGDEFUVMapTex1].Name = FindEnumName(PRGDEFUVMapTex1);
        defines[base+PRGDEFUVMapTex1].Definition = uviName[uvi];

        for (int ml = 0; ml < SHP_MainLight; ml++)
        {
          static const char *mlName[SHP_MainLight] = {"0", "1", "2", "3"};
          defines[base+PRGDEFMainLight].Name = FindEnumName(PRGDEFMainLight);
          defines[base+PRGDEFMainLight].Definition = mlName[ml];

          for (int rm = 0; rm < SHP_RenderingMode; rm++)
          {
            BString<32> rmNumber;
            sprintf(rmNumber,"%d",rm);
            defines[base+PRGDEFRenderingMode].Name = FindEnumName(PRGDEFRenderingMode);
            defines[base+PRGDEFRenderingMode].Definition = rmNumber;

            for (int si = 0; si < SHP_SkinningInstancing; si++)
            {
              static const char *skin[SHP_SkinningInstancing] = {"0", "1",  "0"};
              static const char *inst[SHP_SkinningInstancing] = {"0", "0", "1"};
              defines[base+PRGDEFSkinning].Name = FindEnumName(PRGDEFSkinning);
              defines[base+PRGDEFSkinning].Definition = skin[si];
              defines[base+PRGDEFInstancing].Name = FindEnumName(PRGDEFInstancing);
              defines[base+PRGDEFInstancing].Definition = inst[si];

              // We have problems to compile certain combinations (f.i. they have too much instructions),
              // but we know we're not going to need them - skip them here
              if ((vs == VSPNormalMapThrough) ||
                (vs == VSPNormalMapSpecularThrough) ||
                (vs == VSPNormalMapThroughNoFade) ||
                (vs == VSPNormalMapSpecularThroughNoFade))
              {
                if (si == 1) // Skinned version
                {
                  continue;
                }
                if (uvi == 1) // Usage of TEXCOORD1
                {
                  continue;
                }
              }

              // determine vertex declaration
              int declEnumIndex = UV2[ShaderVertexDeclarations[VSPToVS[vs]]][uvi];
              DoAssert(declEnumIndex >= 0 && declEnumIndex < VDCount);
              const VertexDeclaration& decl = VertexDeclsDX11[declEnumIndex][si];
              DoAssert(decl.IsValid());

              static const char* falseTrue[] = {"0", "1"};
              defines[base+PRGDEFUsesTangents].Name = FindEnumName(PRGDEFUsesTangents);
              defines[base+PRGDEFUsesTangents].Definition = decl.GetUsesTangent() ? falseTrue[1] : falseTrue[0];
              defines[base+PRGDEFUses2TexCoords].Name = FindEnumName(PRGDEFUses2TexCoords);
              defines[base+PRGDEFUses2TexCoords].Definition = decl.GetUses2TexCoords() ? falseTrue[1] : falseTrue[0];

#if _ENABLE_CHEATS
#if 0
              // Here we can force to recompile specific shader
              // Useful with -forceShaderCache command line option
              // This way all shaders are taken from cache, only this shader is recompiled
              ShaderCompileCache::ForceCompileShader = false;
              if (vs == VSPTreeAdv)
                ShaderCompileCache::ForceCompileShader = true;
#endif
#endif

              // Create the shader
              CreateShader(hlslSource, defines.Data(), "VSShaderPool", VSVersion, flags, 
                decl, _vertexShaderPool[vs][uvi][ml][rm][si]);
            }
          }
        }
      }
    }
  }
  // Create NonTL shader and declaration
  CreateShader(FPShaders, NULL, "VSNonTL", VSVersion, flags, VertDeclNonTL, _vsNonTL);

#if _ENABLE_REPORT
  DWORD time = ::GetTickCount()-start;
  LogF("InitVertexShaders: %d ms",time);
#endif

  return true;
}

void EngineDD11::DeInitVertexShaders()
{
  // remove all references from CBs
  for (int cb = -1; cb < CBCount(); ++cb)
  {
    RendState &cbState = CBState(cb);

    SelectPixelShader(cb, PSNone);
    DoSelectPixelShader(cb);
    // make sure no dangling pointers/references exists
    for (int p=0; p<NPredicationModes; p++) cbState._vertexShaderLast[p].Free();
  }
  _currentRenderState._lastVertexShader.Free();

  for (int vs = 0; vs < SHP_VertexShaderID; vs++)
  {
    for (int uvi = 0; uvi < SHP_UVInput; uvi++)
    {
      for (int ml = 0; ml < SHP_MainLight; ml++)
      {
        for (int rm = 0; rm < SHP_RenderingMode; rm++)
        {
          for (int si = 0; si < SHP_SkinningInstancing; si++)
          {
            _vertexShaderPool[vs][uvi][ml][rm][si].Free();
          }
        }
      }
    }
  }
  for (int si = 0; si < SHP_SkinningInstancing; si++)
  {
    _vertexShaderShadowVolume[si].Free();
  }
  for (int i = 0; i < NVertexShaderSpecialID; i++)
  {
    _vertexShaderSpecial[i].Free();
  }
  for (int i = 0; i < SHP_RenderingMode; i++)
  {
    _vertexShaderTerrain[i].Free();
  }

  _vsNonTL.Free();

  // we dont need vertex declarations anymore
  DestroyShaderDeclarations();
}

void EngineDD11::SearchMonitor(int &x, int &y, int &w, int &h)
{
#if !defined(_XBOX) && !defined(_X360SHADERGEN)
  bool monFound = false;
  MONITORINFO mi;
  Assert(_dxgiAdapter.NotNull());
  if (_dxgiAdapter.NotNull())
  {
    HMONITOR mon = GetAdapterMonitor(_dxgiAdapter);
    // if running in windowed mode on multimon window, search all monitors
    HMODULE lib = LoadLibrary("user32.dll");
    if (lib)
    {
      GetMonitorInfoT *getmon = (GetMonitorInfoT *)GetProcAddress(lib,"GetMonitorInfoA");
      if (getmon)
      {
        memset(&mi,0,sizeof(mi));
        mi.cbSize = sizeof(mi);
        getmon(mon,&mi);
        // we have monitor information, adjust window position
        int maxW = mi.rcWork.right-mi.rcWork.left;
        int maxH = mi.rcWork.bottom-mi.rcWork.top;
        if (w>maxW) w=maxW;
        if (h>maxH) h=maxH;
        if (x>mi.rcWork.right-w) x = mi.rcWork.right-w;
        if (y>mi.rcWork.bottom-h) y = mi.rcWork.bottom-h;
        if (x<mi.rcWork.left) x = mi.rcWork.left;
        if (y<mi.rcWork.top) y = mi.rcWork.top;
        monFound = true;
      }
      FreeLibrary(lib);
    }
  }

  if (!monFound)
  {
    int minX = GetSystemMetrics(SM_XVIRTUALSCREEN), minY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int maxW = GetSystemMetrics(SM_CXVIRTUALSCREEN), maxH = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (w>maxW) w=maxW;
    if (h>maxH) h=maxH;
    if (x>minX+maxW-w) x = minX+maxW-w;
    if (y>minY+maxH-h) y = minY+maxH-h;
    if (x<minX) x = minX;
    if (y<minY) y = minY;
  }
#endif
}

/*!
\patch 5106 Date 12/20/2006 by Flyman
- Fixed: Filtering was not working properly when using pushbuffers (square artefact on ground were visible under certain conditions)
*/
void EngineDD11::SetHWToUndefinedState(int cb)
{
  /**
  note: we do not want really to set the device into some particular state,
  we only want to set all shadowed values in such a way that any recording will not assume some particular value to be present
  */

  // check if we already are in the "undefined" state
  // such calls would be best avoided

  PROFILE_SCOPE_DETAIL_EX(hwUnd,dd11)
#if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("SetHWToUndefinedState");
    }
#endif
    RendState &cbState = CBState(cb);
    // Various HW states
    {
      // Render states
      // Set render states to undefined state
      for (int p=0; p<NPredicationModes; p++)
      {
        memset(cbState._renderState[p].Data(),-1,cbState._renderState[p].Size()*sizeof(RenderStateInfo));
      }

      // Sampler stages all levels apart of last one
      for (int j = 0; j < MaxSamplerStages; j++)
      {
#if _ARMA3_PREDICATED_SAMPLER_STATES
        for (int p=0; p<NPredicationModes; p++)
        {
          memset(cbState._samplerState[p][j].Data(),-1,cbState._samplerState[p][j].Size()*sizeof(RenderStateInfo));
        }
#else
        memset(cbState._samplerState[j].Data(),-1,cbState._samplerState[j].Size()*sizeof(RenderStateInfo));
#endif
      }
    }

    // Set tex0 max and avg colors to impossible values
    cbState._texture0MaxColor = Color(-1,-1,-1,-1);
    cbState._texture0AvgColor = Color(-1,-1,-1,-1);

    // Textures to NULL
    // do not really set textures, only forget what was set before
    for (int i = 0; i < MaxSamplerStages; i++) 
    {
      cbState._lastHandle[i] = (TextureHandle11*)(-1);
    }

    // Vertex shader
    for (int p=0; p<NPredicationModes; p++) cbState._vertexShaderLast[p].Free();

    // Pixel shader
    // caution: on X360 NULL pixel shader is a valid value (no RT update)
    // we need to provide some value to force the update
    // the most portable would be some "invalid" shader
    // however as we know _pixelShaderLast is a plain pointer, we will use invalid pointer value instead
    for (int p=0; p<NPredicationModes; p++) cbState._pixelShaderLast[p] = (ID3D11PixelShader *)-1;

    // force a new setup in SetStreamSource - for buffers NULL is invalid enough
    cbState._vBufferLast = VertexStaticData11::BufferFuture(VertexStaticData11::BufferFuture::Null);
    cbState._iBufferLast = IndexStaticData11::BufferFuture(IndexStaticData11::BufferFuture::Null);
    cbState._strideLast = 0;

    // Bias
    cbState._bias = INT_MAX;

    // primitive topology
    cbState._lastPrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

    // Last stuff set to undefined
    PBDrawingCleanup(cb);

    // Reset vertex and pixel shader constants
    ResetShaderConstantValues(cb);
}

void EngineDD11::SetCommandBuffersToUndefinedState()
{
  for (int i=-1; i<CBCount(); i++)
  {
    SetHWToUndefinedState(i);
  }
}

void EngineDD11::PBDrawingCleanup(int cb)
{
  RendState &cbState = CBState(cb);
  // High-level last stuff set to undefined (the previous PB playing might have
  // changed the low-level and the high-level remained unchanged)
  cbState._lastSpec = -1;
  cbState._lastMat = TexMaterialLODInfo((TexMaterial *)-1, 0);
  cbState._lastRenderingMode = (RenderingMode)-1;
  cbState._texGenScaleOrOffsetHasChanged = true;
  cbState._currentShadowStateSettings = SS_None;
}

void EngineDD11::ResetShaderConstantValues(int cb)
{
  CBState(cb)._shaderConsts.ResetValues();
}

/*!
\patch 5156 Date 5/15/2007 by Ondra
- Fixed: Better VRAM allocation estimations for nVidia 8800 card, resulting in less frequent missing textures.

\patch 5160 Date 5/23/2007 by Ondra
- Fixed: Improved VRAM managment for ATI cards with Catalyst 7.3 or newer.
*/
void EngineDD11::InitTexMemEstimations()
{
  // make sure the experiment is done only once
  if (_caps._localPredictionSet) return;
  _caps._localPredictionSet = true;

  _caps._localPrediction = false;
  _caps._texAlignment = 4096;
  _caps._vbAlignment = 4096;
  _caps._ibNonLocal = false;
  _caps._dynamicVBNonLocal = false;
  _caps._staticVBNonLocal = false;

  long long vramAllocLimit = _caps._localVramAmount> 0 ? to64bInt(1.2f*_caps._localVramAmount) : _caps._nonlocalVramAmount;
  long long maxVram = _caps._localVramAmount+to64bInt(0.8f*_caps._nonlocalVramAmount);
  if (vramAllocLimit>maxVram) vramAllocLimit = maxVram;
  vramAllocLimit -= _caps._rtAllocated;
  if (vramAllocLimit>INT_MAX) vramAllocLimit = INT_MAX;
  _caps._vramAllocLimit = int(vramAllocLimit);
  LogF("DX11 static Tex/VB VRAM limit %d B",_caps._vramAllocLimit);
}

void EngineDD11::DeInitPixelShaders()
{
  Assert(!IsCBRecording());

  // remove all references from CBs
  for (int cb = -1; cb < CBCount(); ++cb)
  {
    RendState &cbState = CBState(cb);

    SelectPixelShader(cb, PSNone);
    DoSelectPixelShader(cb);
    // make sure no dangling pointers/references exists
    for (int p=0; p<NPredicationModes; p++) cbState._pixelShaderLast[p].Free();
  }
  _currentRenderState._lastPixelShader.Free();

  // free all shaders
  for (int i=0; i<NPixelShaderID; i++) 
  {
    for (int t=0; t<NPixelShaderType; t++)
    {
        _pixelShader[i][t].Free();
    }
  }
  
  // clear cache
  _psCache.Clear();
}

void EngineDD11::DestroyShaderDeclarations()
{
  Assert(!IsCBRecording());

  // remove all references from CBs
  _currentRenderState._lastInputLayout.Free();

  for (int i = 0; i < VDCount; i++)
  {
    for (int j = 0; j < VSDVCount; j++)
    {
      _vertexDeclD[i][j].Free();
    }
  }
  _vdNonTL.Free();
}

// release post effects
class DestroyPPHndlFunc: public IPPHndlFunc
{
  bool operator () (PostProcessHndl &hndl)
  {
    if (hndl._pp) { hndl._pp.Free(); }    
    return true;
  }
};

void EngineDD11::DestroyPostProcessStuff()
{
   _pp._flareIntensity.Free();
  _pp._bloom.Free();
  _pp._glow.Free();
  _pp._finalThermal.Free();
  _pp._distantDOF.Free();
  _pp._DOF.Free();
#ifdef SSSMBLUR
  _pp._SSSMB.Free();
#endif
  _pp._SSSMStencil.Free();
  _pp._SSSM.Free();
  _pp._gaussianBlur.Free();
  _pp._filmGrain.Free();
  _pp._ssao.Free();
  _pp._final.Free();
  _pp._bicubic.Free();
  _pp._rotBlur.Free();
  _pp._radialBlur.Free();
  _pp._colors.Free();
  _pp._chromAber.Free();
  _pp._wetDistort.Free();
  _pp._dynamicBlur.Free();
  _pp._clrInvers.Free();
  _pp._stretchRect.Free();
#if ENABLE_NVG_NOISE
  _pp._nvgNoise.Free();
#endif
  _pp._rain3D.Free();
  _pp._diagDepthBuffer.Free();

  _spp._hit.Free();
  _spp._movement.Free();
  _spp._tired.Free();
  _spp._fire.Free();

  DestroyPPHndlFunc destFunc;
  ForEachPostEffectHndl(destFunc);

  _pp._effects.Clear();
  _pp._effectsThis.Clear();
}

bool EngineDD11::CreateShaderDeclarations()
{
  // DX11 - vertex declarations in DX11 are moved to VertexShaderInfo and are created when the shaders are created

  Assert(!IsCBRecording());
  const int cb = -1;

  // Set TL to defined state
#ifndef _X360SHADERGEN
  DoSwitchTL(cb,TLDisabled);
#endif
  return true;
}

bool EngineDD11::CreatePostProcessStuff()
{
#if _ENABLE_REPORT
  DWORD start = ::GetTickCount();
#endif
  Log("CreatePostProcessStuff");
  Debugger::PauseCheckingScope scope(GDebugger);
  if (!NewPostProcessStuff(true))
  {
    return false;
  }

#if _ENABLE_REPORT
  DWORD time = ::GetTickCount()-start;
  LogF("CreatePostProcessStuff: %d ms",time);
#endif

  ForgetAvgIntensity();

  return true;
}

bool EngineDD11::NewPostProcessStuff(bool firstInit)
{
  // Get fragments from specified file
  QOStrStreamDataPrefixedLine psSource(PSFile);
  QOStrStreamDataPrefixedLine vsSource(FPShaders);

  // Create post processes
  Ref<PPFinal> ppFinal = new PPFinal;
  if (!ppFinal->Init(psSource,vsSource))
  {
    LogF("Error: Post process effect creation failed.");
    return false;
  }
  Ref<PPRescaleBicubic> ppBicubic = new PPRescaleBicubic;
  if (!ppBicubic->Init(psSource,vsSource))
  {
    LogF("Error: Post process effect creation failed.");
    return false;
  }
  Ref<PPGaussianBlur> ppGaussianBlur = new PPGaussianBlur;
  if (!ppGaussianBlur->Init(psSource,vsSource))
  {
    LogF("Error: Post process effect creation failed.");
    return false;
  }
  Ref<PPBloom> ppBloom = new PPBloom;
  if (!ppBloom->Init(psSource,vsSource))
  {
    LogF("Error: Post process effect creation failed.");
    return false;
  }
#ifdef _RAIN3D
  Ref<PPRain3D> ppRain3D = new PPRain3D;
  if (!ppRain3D->Init(psSource,vsSource))
  {
    LogF("Error: Post process effect creation failed.");
    return false;
  }
#endif
  Ref<PPSSSM> ppSSSM = new PPSSSM;
  if (!ppSSSM->Init(psSource, vsSource))
  {
    LogF("Error: Post process effect creation failed.");
    return false;
  }
  Ref<PPSSSMStencil> ppSSSMStencil = new PPSSSMStencil;
  if (!ppSSSMStencil->Init(psSource, vsSource))
  {
    LogF("Error: Post process effect creation failed.");
    return false;
  }
#ifdef SSSMBLUR
  Ref<PPSSSMB> ppSSSMB = new PPSSSMB;
  if (!ppSSSMB->Init(psSource, vsSource))
  {
    LogF("Error: Post process effect creation failed.");
    return false;
  }
#endif
  Ref<PPDOF> ppDOF = new PPDOF;
  if (!ppDOF->Init(psSource, vsSource, ppGaussianBlur))
  {
    LogF("Error: Post process effect creation failed.");
    return false;
  }
  Ref<PPDistanceDOF> ppDistanceDOF = new PPDistanceDOF;
  if (!ppDistanceDOF->Init(psSource, vsSource, ppGaussianBlur))
  {
    LogF("Error: Post process effect creation failed.");
    return false;
  }
  Ref<PPFilmGrain> ppFilmGrain = new PPFilmGrain;
  if (!ppFilmGrain->Init(psSource, vsSource, firstInit))
  {
    LogF("Error: Post process effect (FilmGrain) creation failed.");
    return false;
  }
  Ref<PPSSAO> ppSSAO = new PPSSAO;
  if (!ppSSAO->Init(psSource, vsSource, firstInit))
  {
    LogF("Error: Post process effect (SSAO) creation failed.");
    return false;
  }
  Ref<PPFinalThermal> ppFinalThermal = new PPFinalThermal;
  if (!ppFinalThermal->Init(psSource, vsSource))
  {
    LogF("Error: Post process effect creation failed.");
    return false;
  }
  Ref<PPGlow> ppGlow = new PPGlow;
  if (!ppGlow->Init(psSource,vsSource, ppGaussianBlur))
  {
    LogF("Error: Post process effect creation failed.");
    return false;
  }
  Ref<PPFlareIntensity> ppFlareIntensity = new PPFlareIntensity;
  if (!ppFlareIntensity->Init(psSource,vsSource))
  {
    LogF("Error: Post process effect creation failed.");
    return false;
  }
  Ref<PPRotBlur> ppRotBlur = new PPRotBlur;
  if( !ppRotBlur->Init( psSource, vsSource ) )
  {
    LogF("Error: Post process effect 'PPRotBlur' creation failed.");
    return false;
  }
  Assert(firstInit == _pp._radialBlur.IsNull());
  Ref<PPRadialBlur> ppRadialBlur = (firstInit) ? new PPRadialBlur : _pp._radialBlur;
  if( !ppRadialBlur->Init( psSource, vsSource, firstInit) )
  {
    LogF("Error: Post process effect 'PPRadialBlur' creation failed.");
    return false;
  }
  Assert(firstInit == _pp._colors.IsNull());
  Ref<PPColors> ppColors = (firstInit) ? new PPColors : _pp._colors;
  if( ppColors && !ppColors->Init( psSource, vsSource, firstInit ) )
  {
    LogF("Error: Post process effect 'PPColors' creation failed.");
    return false;
  }
  Assert(firstInit == _pp._chromAber.IsNull());
  Ref<PPChromAber> ppChromAber = (firstInit) ? new PPChromAber : _pp._chromAber;
  if( ppChromAber && !ppChromAber->Init( psSource, vsSource, firstInit ) )
  {
    LogF("Error: Post process effect 'PPChromAber' creation failed.");
    return false;
  }
  Assert(firstInit == _pp._wetDistort.IsNull());
  Ref<PPWetDistort> ppWetDistort = (firstInit) ? new PPWetDistort : _pp._wetDistort;
  if( !ppWetDistort->Init( psSource, vsSource, ppGaussianBlur, firstInit ) )
  {
    LogF("Error: Post process effect 'PPWetDistort' creation failed.");
    return false;
  }
  Assert(firstInit == _pp._dynamicBlur.IsNull());
  Ref<PPDynamicBlur> ppDynamicBlur = (firstInit) ? new PPDynamicBlur : _pp._dynamicBlur;
  if( !ppDynamicBlur->Init( psSource, vsSource, ppGaussianBlur, firstInit ) )
  {
    LogF("Error: Post process effect 'PPDynamicBlur' creation failed.");
    return false;
  }
  Assert(firstInit == _pp._clrInvers.IsNull());
  Ref<PPColorsInversion> ppColorsInvers = (firstInit) ? new PPColorsInversion : _pp._clrInvers;
  if (!ppColorsInvers->Init(psSource, vsSource, firstInit))
  {
    LogF("Error: Post process effect 'PPColorsInversion' creation failed.");
    return false;
  }
  Assert(firstInit == _pp._stretchRect.IsNull());
  Ref<PPStretchRect> ppStretchRect = (firstInit) ? new PPStretchRect : _pp._stretchRect;
  if (!ppStretchRect->Init(psSource, vsSource))
  {
    LogF("Error: Post process effect 'PPStretchRect' creation failed.");
    return false;
  }

#if ENABLE_NVG_NOISE
  Ref<PPFilmGrain> ppNVGNoiseFilm = new PPFilmGrain;
  if (ppFilmGrain)
  {
    PPFilmGrainResources *ppres = const_cast<PPFilmGrainResources *>(ppFilmGrain->Resources());
    if (!ppNVGNoiseFilm->Init(ppres, true))
    {
      LogF("Error: Post process effect 'PPFilmGrain' creation failed.");
      return false;
    }
    else
    {
      float pars[] = { 0.05, 0.1, 1.1, 0.1, 0.1, 0 };
      ppNVGNoiseFilm->SetParams(pars, 6);
      ppNVGNoiseFilm->Commit(0);
      ppNVGNoiseFilm->Enable(false);
    }
  }
#endif

  Ref<PPDiagDepthBuffer> ppDiagDepthBuffer = new PPDiagDepthBuffer;
  if (!ppDiagDepthBuffer->Init(psSource, vsSource))
  {
    LogF("Error: Post process effect creation failed.");
    return false;
  }

  ppRain3D->SetLevel(EngineDD11::PPLForce);
  ppBloom->SetLevel(EngineDD11::PPLLow);
  ppGlow->SetLevel(EngineDD11::PPLLow);
  ppDOF->SetLevel(EngineDD11::PPLNormal);
  ppSSAO->SetLevel(EngineDD11::PPLNormal);
  ppDistanceDOF->SetLevel(EngineDD11::PPLNormal);
  ppRotBlur->SetLevel(EngineDD11::PPLLow);
  ppFilmGrain->SetLevel(EngineDD11::PPLVeryLow);
  ppRadialBlur->SetLevel(EngineDD11::PPLVeryLow);
  ppColors->SetLevel(EngineDD11::PPLForce);
  ppChromAber->SetLevel(EngineDD11::PPLLow);
  ppWetDistort->SetLevel(EngineDD11::PPLLow);
  ppDynamicBlur->SetLevel(EngineDD11::PPLVeryLow);
  ppColorsInvers->SetLevel(EngineDD11::PPLLow);
  ppStretchRect->SetLevel(PPLVeryLow);
#if ENABLE_NVG_NOISE
  ppNVGNoiseFilm->SetLevel(EngineDD11::PPLLow);
#endif

  ppFilmGrain->SetPriority(PPFilmGrainPriority);
  ppGlow->SetPriority(PPGlowPriority);
  ppRadialBlur->SetPriority(PPRadBlurPriority);
  ppColors->SetPriority(PPColorPriority);
  ppChromAber->SetPriority(PPChrAbberPriority);
  ppWetDistort->SetPriority(PPWetDistortPriority);
  ppDynamicBlur->SetPriority(PPDynBlurPriority);
  ppColorsInvers->SetPriority(PPColorInvPriority);
#if ENABLE_NVG_NOISE
  ppNVGNoiseFilm->SetPriority(PPFilmGrainPriority);
#endif
#ifdef _RAIN3D
  ppRain3D->SetPriority(ppRain3DPriority);
#endif
  // We are going to succeed - save the created objects
  _pp._final = ppFinal;
  _pp._bicubic = ppBicubic;
  _pp._gaussianBlur = ppGaussianBlur;
  _pp._bloom = ppBloom;
#ifdef _RAIN3D
  _pp._rain3D = ppRain3D;
#endif
  _pp._SSSM = ppSSSM;
  _pp._SSSMStencil = ppSSSMStencil;
#ifdef SSSMBLUR
  _pp._SSSMB = ppSSSMB;
#endif
  _pp._DOF = ppDOF;
  _pp._distantDOF = ppDistanceDOF;
  _pp._filmGrain = ppFilmGrain;
  _pp._ssao = ppSSAO;
  SetSSAOQuality(_postFxQuality);
  _pp._finalThermal = ppFinalThermal;
  _pp._glow = ppGlow;
  _pp._flareIntensity = ppFlareIntensity;
  _pp._rotBlur = ppRotBlur;

  _pp._radialBlur = ppRadialBlur;
  _pp._chromAber = ppChromAber;
  _pp._wetDistort = ppWetDistort;
  _pp._dynamicBlur = ppDynamicBlur;
  _pp._clrInvers = ppColorsInvers;
  _pp._colors = ppColors;
  _pp._stretchRect = ppStretchRect;

#if ENABLE_NVG_NOISE
  _pp._nvgNoise = ppNVGNoiseFilm;
#endif

  _pp._diagDepthBuffer = ppDiagDepthBuffer;

  if (_spp._hit.IsNull())
  {
    _spp._hit = new HitSpecEffect;
    _spp._hit->Init();
  }

  if (_spp._movement.IsNull())
  {
    _spp._movement = new MovementSpecEffect;
    _spp._movement->Init();
  }

  if (_spp._tired.IsNull())
  {
    _spp._tired = new TiredSpecEffect;
    _spp._tired->Init();
  }

  if (_spp._fire.IsNull())
  {
    _spp._fire = new FireSpecEffect;
    _spp._fire->Init();
  }

  return true;
}

/**
@param select -1 default, 0..0x2ff = force SM 2.0, 0x300.. = force SM 3.0
*/
void EngineDD11::InitShaderModel(int select)
{
  // we are now using shader model 4, even in dx11
  _caps._vsVersion = "vs_4_0";
  _caps._psVersion = "ps_4_0";
  _caps._vsVersionLink = "vs_4_0";
  int maxFreeConstants = 168; // see also maxFreeConstants in FPShaders.hlsl
  InitShaderDirs();

  _caps._maxBones = maxFreeConstants/3;
  _caps._maxInstancesShadowVolume = _caps._maxBones;
  _caps._maxInstances = maxFreeConstants/4;
  _caps._maxInstancesSprite = maxFreeConstants/3;
  saturateMin(_caps._maxBones,LimitBones);
  saturateMin(_caps._maxInstances,LimitInstances);
  saturateMin(_caps._maxInstancesShadowVolume,LimitInstancesShadowVolume);
  saturateMin(_caps._maxInstancesSprite,LimitInstancesSprite);

  _caps._maxNightInstances = _caps._maxInstances;
  _caps._maxNightInstancesSprite = _caps._maxInstancesSprite;
}

void EngineDD11::InitDirectDraw7()
{
  Assert(_dxgiAdapter);
  if (!_dxgiAdapter)
    return;

  // DX11 TODO
//#if !defined(_XBOX) && !defined(_X360SHADERGEN)
//  D3DADAPTER_IDENTIFIER9 adid;
//  _direct3D->GetAdapterIdentifier(adapter, 0, &adid);
//
//  RString deviceId = Format("%d,%d,%d",adid.VendorId,adid.DeviceId,adid.SubSysId);
//
//  // decide about DDraw device based on monitor handle
//  DDEnumCallbackExContext ctx;
//  ctx.hm = _direct3D->GetAdapterMonitor(adapter);
//  DirectDrawEnumerateEx(
//    DDEnumCallbackEx,&ctx,
//    DDENUM_ATTACHEDSECONDARYDEVICES|DDENUM_DETACHEDSECONDARYDEVICES|DDENUM_NONDISPLAYDEVICES
//    );
//  //if (strcmp(deviceId,_lastDeviceId))
//  {
//    // before creating device we want to check its memory properties using DX7
//    // we do not know how adapters ID maps to ddraw GUIDS
//    // we can handle only the simple case - default adapter is used
//
//    DirectDrawCreateEx(ctx.guidPtr,(void **)_ddraw.Init(),IID_IDirectDraw7,NULL);
//
//    _lastDeviceId = RString();
//    _caps._localVramAmount = 0;
//    _caps._nonlocalVramAmount = 0;
//    _freeLocalVramAmount = 0;
//    _freeNonlocalVramAmount = 0;
//    _freeEstDirty = 0;
//    _freeEstCheckTime = 0;
//
//    // if this is plain wrong, we will use heuristics:
//    // assume half of VRAM, other half AGP
//    // round to nearest pow2
//    _lastDeviceId = deviceId;
//  }
//#endif
}

#define DEBUG_CURSOR 0

void EngineDD11::ShowDeviceCursor(bool showIt)
{
  _cursorShown = showIt;
  // infinite cycle guard
  const int maxCounter = 1000;
  int counter = 0;
  if (_cursorShown)
  {
    int value = 0;
    while ((value = ::ShowCursor(true)) < 0 && counter <= maxCounter)
    {
      ++counter;
    }

    // now reduce the value to 0 
    // this is needed so that we dont increase internal counter value when cursor was already shown
    while (value > 0 && counter <= maxCounter)
    {
      value = ::ShowCursor(false);
      ++counter;
    }
  }
  else
  {
    int value = 0;
    while ((value = ::ShowCursor(false)) >= 0 && counter <= maxCounter)
    {
      ++counter;
    }

    // now reduce the value to -1 
    // this is needed so that we dont decrease internal counter value when cursor was already hidden
    while (value < -1 && counter <= maxCounter)
    {
      value = ::ShowCursor(true);
      ++counter;
    }
  }

  DoAssert(counter < maxCounter);
}

void EngineDD11::DoPresentBackbuffer()
{

  if (!_cursorSet)
  {
    ShowDeviceCursor(false);
  }

  PROFILE_SCOPE_GRF_EX(3dSwp,*,0);
#ifdef _XBOX
  // PIX advice: set to a vertex count of 32 and a pixel count of 96, rendering of entire scene would be 2.97% faster.
  _d3dDevice->SetShaderGPRAllocation(0,0,0);

  // adapted from PredicatedTiling sample, as per doc advice:
  /*
  Alternatively, if your title performs either full screen effects or rendering after tiling, only one front
  buffer texture is needed, plus one scene resolve texture. The memory requirements are the same as for two
  front buffers, but the need to switch between front buffers at every frame is eliminated. Simply perform
  the final Resolve to the single front buffer after calling SynchronizeToPresentationInterval, and there is
  no tearing. The PredicatedTiling sample in the XDK demonstrates this configuration, which is described
  in more detail in the Advanced Predicated Tiling Topics white paper.
  */
  _d3dDevice->SynchronizeToPresentationInterval();

  // TODO: consider clearing backbuffer for free here
  D3DVECTOR4 clearColor;
  clearColor.x = 0;
  clearColor.y = 0;
  clearColor.z = 0;
  clearColor.w = 0;
  // Resolve the rendered scene back to the front buffer.
  _d3dDevice->Resolve(
    D3DRESOLVE_RENDERTARGET0 /*| D3DRESOLVE_CLEARRENDERTARGET*/,
    NULL,_frontBuffer,NULL,
    0, 0, 
    &clearColor, 1.0f, 0, NULL
    );


  _d3dDevice._lastFence = _d3dDevice.GetRefForCB()->GetCurrentFence();
  _d3dDevice->Swap(_frontBuffer,NULL);
#else

  HRESULT result = _d3dDeviceRec->Present(0, 0);
  switch (result)
  {
  case S_OK:
    // everything OK
    _currRenderTarget = _backBuffer;
    // no depth/stencil associated with backbuffer
    _currDepthStencil.Free();

#if _ENABLE_REPORT
    {
      ComRef<ID3D11Texture2D> bb;
      HRESULT err = _d3dDeviceRec->GetBuffer(0, __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(bb.Init()));
      if (FAILED(err))
      {
        DDError11("GetBuffer failed",err);
      }
      Assert(bb==_backBuffer._resource);
    }
#endif
    break;
  case DXGI_ERROR_DEVICE_REMOVED:
    // device was removed or drivers were updated - need to recreate device
    LogF("*** Present: Device removed.");
    _resetStatus = result;
    break;
  case DXGI_ERROR_DEVICE_RESET:
    // device was reset due to some badly formed command - need to recreate device
    LogF("*** Present: Device reset.");
    _resetStatus = result;
    break;
  case DXGI_STATUS_OCCLUDED:
    LogF("*** Present: Window occluded.");
    // do nothing...
    break;
  default:
    // some other error
    DDError11("Present failed",result);
    break;
  }

#endif
  ResetLogDevice();
}

void EngineDD11::BackToFront()
{
  if (FAILED(_resetStatus)) return;

  _resetFrame++;

  if (_backgroundD3D!=RecHighLevel) DoPresentBackbuffer();
  else
  {
    RecordHighlevel()->PresentBackbuffer();

    // make sure any instructions which were pending yet are kicked off now
    SubmitBackground(_bg,true,true,CleanUpNone,"hlFls");
  }

#if _ENABLE_CHEATS
  // at this point we are sure the rendering queue is empty (it was flushed) and we can safely change any settings
  if (GInput.GetCheat2ToDo(DIK_L) || GInput.GetCheatXToDo(CXTInstancing))
  {
    Glob.config.enableInstancing = !Glob.config.enableInstancing;
    GlobalShowMessage(1000, "Instancing %s", Glob.config.enableInstancing ? "On" : "Off");
  }
  if (GInput.GetCheat3ToDo(DIK_P) || GInput.GetCheatXToDo(CXTUseCB))
  {
    Glob.config._useCB = !Glob.config._useCB;
    GlobalShowMessage(1000, "%s Command Buffers", Glob.config._useCB ? "Using" : "Not using");
  }
  if (int dif = GInput.GetCheatXToDo(CXTUseBG)+GInput.GetCheat3ToDo(DIK_I))
  {
    if (GetCPUCount()>1)
    {
      // switching not possible while any data are still pending
      // before switching we need to flush
      EndHighLevelRecording();
      int newRt = Glob.config._renderThread+dif;
      int minRTMode = Config::ThreadingEmulated; // no threading no longer supported
      int maxRTMode = Config::ThreadingReal;
      if (newRt<minRTMode) newRt = maxRTMode;
      if (newRt>maxRTMode) newRt = minRTMode;
      Glob.config._renderThread = (Config::ThreadingMode)newRt;
      static const char *text[]={"Immediate","Emulated background","Background"};
      GlobalShowMessage(1000, "%s rendering", text[Glob.config._renderThread]);
    }
  }
  if (GInput.GetCheat3ToDo(DIK_E))
  {
    Glob.config._mtSerial = !Glob.config._mtSerial;
    GlobalShowMessage(1000, "%s Command Buffers", Glob.config._mtSerial ? "Single-core" : "Multi-core");
  }
  if (GInput.GetCheat3ToDo(DIK_M))
  {
    Glob.config._mtSerialMat = !Glob.config._mtSerialMat;
    GlobalShowMessage(1000, "%s Matrix Animations", Glob.config._mtSerialMat ? "Single-core" : "Multi-core");
  }
  if (GInput.GetCheat3ToDo(DIK_U) || GInput.GetCheatXToDo(CXTPredication))
  {
    Glob.config._predication = !Glob.config._predication;
    GlobalShowMessage(1000, "%s Predication", Glob.config._predication ? "SW" : "No");
  }
  if (GInput.GetCheat3ToDo(DIK_K))
  {
    // switching not possible while any data are still pending
    // before switching we need to flush
    EndHighLevelRecording();
    _hlRecording = !_hlRecording;
    GlobalShowMessage(1000, "High Level Recording: %s", _hlRecording ? "On" : "Off");
  }
#ifndef _XBOX
  if (GInput.GetCheat3ToDo(DIK_A))
  {
    // switching not possible while any data are still pending
    // before switching we need to flush
    {
      EndHighLevelRecording();
      if (_AToCMask) _useAToC = !_useAToC;
      GlobalShowMessage(1000, "Alpha to Coverage: %s", _useAToC ? "On" : "Off");
    }
  }
#endif
  if (int dif=GInput.GetCheatXToDo(CXTMJCPULimit)-GInput.GetCheat3ToDo(DIK_O))
  {
    int limit = GJobManager.GetMicroJobCountLimit();
    int cpus = GetCPUCount();
    if (limit>cpus) limit = cpus; // anything above cpus handle like cpus (no limit)
    limit += dif;
    // now limit to valid values only
    if (limit<=0) limit = cpus;
    if (limit>cpus) limit = 1;
    GlobalShowMessage(1000, "Micro Job Concurrency Limit: %d", limit);
    // following code makes sure "no limit" means no limit even if CPU count is dynamically changed
    if (limit==cpus) limit = INT_MAX;
    GJobManager.SetMicroJobCountLimit(limit);
  }
  if (GInput.GetCheat1ToDo(DIK_NUMPADSLASH))
  {
    EndHighLevelRecording();
    _vsCache.Clear();
    _psCache.Clear();  
#if _ENABLE_COMPILED_SHADER_CACHE
    RString cacheName = ShaderCacheName("PP");
    ShaderCompileCache cacheInit(SourcesPP, cacheName, RStringB(HLSLCacheDir));
#endif

    CreatePostProcessStuff();
  }
  if (GInput.GetCheat1ToDo(DIK_NUMPADSTAR))
  {
    EndHighLevelRecording();
    _vsCache.Clear();
    _psCache.Clear();  
#if _ENABLE_COMPILED_SHADER_CACHE
    RString cacheName = ShaderCacheName("PS");
    ShaderCompileCache cacheInit(SourcesPS, cacheName, RStringB(HLSLCacheDir));
#endif

    InitPixelShaders();
  }

  if (GInput.GetCheat1ToDo(DIK_NUMPADMINUS))
  {
    EndHighLevelRecording();
    _vsCache.Clear();
    _psCache.Clear();  
#if _ENABLE_COMPILED_SHADER_CACHE
    RString cacheName = ShaderCacheName("VS");
    ShaderCompileCache cacheInit(SourcesVS, cacheName, RStringB(HLSLCacheDir));
#endif

    CreateShaderDeclarations();
    InitVertexShaders();
  }
  if (GInput.GetCheat3ToDo(DIK_B))
  {
    int newRescalerMode = FinalRescalerMode+1;
    if (newRescalerMode>=NRescalerModes) newRescalerMode = 0;
    FinalRescalerMode = RescalerMode(newRescalerMode);
  }

  if (GWorld && GWorld->_enableBloomEditing)
  {
    if(::GetAsyncKeyState(VK_LSHIFT) & 0x8000)
    {
      static bool released = true;

      if(::GetAsyncKeyState(0x30) & 0x8000)
      {
        if(released)
        {
          _doBloom = !_doBloom;
          released = false;
        }
      }
      else
      {
        released = true;
      }

      static bool released1 = true;
      if(::GetAsyncKeyState(0x39) & 0x8000)
      {
        if(released1)
        {
          if (BloomImgScale > 0.0f)
            BloomImgScale = 0.0f;
          else
            BloomImgScale = 1.0f;
          released1 = false;
        }
      }
      else
      {
        released1 = true;
      }

      //    float tt = deltaT * 0.5f;
      float tt = 0.01;

      float ptheshold = BloomThreshold;
      float pimgscale = BloomImgScale;
      float pblurscale = BloomBlurScale;
      float pblurOverbrigtCoef = BloomOverbrightCoef;
      int pblurKernelSize = BloomBlurKernelSize;


      static bool released2 = true;
      if(::GetAsyncKeyState(0x37) & 0x8000)
      {
        if(released2)
        {
          BloomBlurKernelSize -= 2;
          released2 = false;
        }
      }
      else
      {
        released2 = true;
      }

      static bool released3 = true;
      if(::GetAsyncKeyState(0x38) & 0x8000)
      {
        if(released3)
        {
          BloomBlurKernelSize += 2;
          released3 = false;
        }
      }
      else
      {
        released3 = true;
      }

      saturate(BloomBlurKernelSize, 1, PSBLUR_MAX_KERNEL_SIZE);

      if(::GetAsyncKeyState(0x31) & 0x8000)
        BloomThreshold += tt;
      if(::GetAsyncKeyState(0x32) & 0x8000)
        BloomThreshold -= tt;


      saturate(BloomThreshold, 0.0f, 2.0f);

      if(::GetAsyncKeyState(0x33) & 0x8000)
        BloomOverbrightCoef -= tt*10.0f;
      if(::GetAsyncKeyState(0x34) & 0x8000)
        BloomOverbrightCoef += tt*10.0f;

      saturate(BloomOverbrightCoef, 0.0f, 10.0f);

      if(::GetAsyncKeyState(0x35) & 0x8000)
        BloomBlurScale -= tt;
      if(::GetAsyncKeyState(0x36) & 0x8000)
        BloomBlurScale += tt;

      saturate(BloomBlurScale, 0.0f, 1.0f);

      if(ptheshold != BloomThreshold ||
        pimgscale != BloomImgScale ||
        pblurscale != BloomBlurScale ||
        pblurOverbrigtCoef != BloomOverbrightCoef ||
        pblurKernelSize != BloomBlurKernelSize)
      {
        LogF("Bloom settings: threshold %f overbrightCoef %f  blurscale %f blurKernel %d", 
          BloomThreshold, BloomOverbrightCoef, BloomBlurScale, BloomBlurKernelSize);
        DIAG_MESSAGE(1000,Format("Bloom: (1,2): threshold %f, (3-4) overbrightCoef: %f, (5-6) blurscale: %f, (7-8) blurKernel: %d", 
          BloomThreshold, BloomOverbrightCoef, BloomBlurScale, BloomBlurKernelSize));
      }
    }
  }
#endif
}

void EngineDD11::FindMode()
{
  Assert(_dxgiOutput);
  if (!_dxgiOutput)
    return;

  // prepare presentation parameters based on _w, _h, _pixelSize
  // first select backbuffer format
  DXGI_MODE_DESC bestDisplayMode;
  // we may want a different render target
  int bestCost = INT_MAX;
  bestDisplayMode.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  bestDisplayMode.RefreshRate.Numerator = 0;
  bestDisplayMode.RefreshRate.Denominator = 0;
  bestDisplayMode.Width = _w;
  bestDisplayMode.Height = _h;
  bestDisplayMode.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
  bestDisplayMode.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

  if (!_windowed)
  {
    for (int formatIndex = 0; formatIndex < lenof(supportedFormats); formatIndex++) 
    {
      UINT flags = 0;
      UINT numModes = 0;
      // first get number of modes
      HRESULT hr = _dxgiOutput->GetDisplayModeList(supportedFormats[formatIndex]._format, flags, &numModes, 0);
      if (FAILED(hr))
      {
        Fail("GetDisplayModeList failed.");
        continue;
      }

      AutoArray<DXGI_MODE_DESC> modes;
      modes.Resize(numModes);
      // get available modes
      hr = _dxgiOutput->GetDisplayModeList(supportedFormats[formatIndex]._format, flags, &numModes, modes.Data());
      if (FAILED(hr))
      {
        Fail("GetDisplayModeList failed.");
        continue;
      }

      // Go through available modes
      for (int mode = 0; mode < numModes; mode++)
      {
        DXGI_MODE_DESC& displayMode = modes[mode];

        int bpp = 16;
        int cost = 0;
        switch (displayMode.Format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM: cost += 180; bpp = 32; break;
        default: cost += 500; break; // unknown format - last resort
        }
        cost +=
          (
          abs(_w*_h-displayMode.Width*displayMode.Height)*10 + 
          abs(_w-displayMode.Width)*10 + 
          abs(_h-displayMode.Height)*10 +
          abs(_pixelSize-bpp)*200
          );
        
        int displayRefreshRate = ConvertRefreshRate(displayMode.RefreshRate);

        if (_refreshRate>0)
        {
          cost += abs(_refreshRate-displayRefreshRate);
        }
        else
        {
          cost += abs(75-displayRefreshRate); // higher refresh rate is always better
        }
        if (cost<bestCost)
        {
          bestCost = cost;
          bestDisplayMode = displayMode;
        }
      }
    }
    LogF
      (
      "mode %dx%d (%d Hz) (%s)",
      bestDisplayMode.Width,bestDisplayMode.Height,
      ConvertRefreshRate(bestDisplayMode.RefreshRate),
      SurfaceFormatNameDD11(bestDisplayMode.Format)
      );
  }

  // fill swap chain description
  ZeroMemory(&_swapChainDesc, sizeof(_swapChainDesc));

  if (_windowed)
  {
    _swapChainDesc.BufferDesc.Width = _w;
    _swapChainDesc.BufferDesc.Height = _h;
    _swapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
    _swapChainDesc.BufferDesc.RefreshRate.Denominator = 0;
    _refreshRate = 0;
  }
  else
  {
    _swapChainDesc.BufferDesc.Width = bestDisplayMode.Width;
    _swapChainDesc.BufferDesc.Height = bestDisplayMode.Height;
    _swapChainDesc.BufferDesc.RefreshRate = bestDisplayMode.RefreshRate;
    _w = bestDisplayMode.Width;
    _h = bestDisplayMode.Height;
    _refreshRate = ConvertRefreshRate(bestDisplayMode.RefreshRate);
  }

  // back buffer format is non-srgb, we perform srgb conversion during tone mapping
  _swapChainDesc.BufferDesc.Format = bestDisplayMode.Format; 
  _swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
  _swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
  _swapChainDesc.SampleDesc.Count = 1; // NO MSAA for backbuffer! (MSAA is used for RTs only)
  _swapChainDesc.SampleDesc.Quality = 0; // NO MSAA for backbuffer! (MSAA is used for RTs only)
  _swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
  _swapChainDesc.BufferCount = 1;
  _swapChainDesc.OutputWindow = _hwndApp;
  _swapChainDesc.Windowed = _windowed;
  _swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  _swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

  _caps._canWriteSRGB = true;

  if (!_d3dDeviceNonRec.IsNull())
    FindRTFormats();
}

void EngineDD11::FindRTFormats()
{
  Assert(!_d3dDeviceNonRec.IsNull())


    if (_d3dDeviceNonRec.IsNull())
      return;

  _hdrPrecSupported = HDR8;
  for (int i=0; i<lenof(PreferredRTFormat); i++)
  {
    DXGI_FORMAT rt = PreferredRTFormat[i].format;
    
    // check if it can be used as a render target
    if (IsSuitableRT(rt))
    {
      _hdrPrecSupported |= PreferredRTFormat[i].prec;
    }
  }

  // maxTextureSize used in FindFillrateControl to determine 3D resolution
  _caps._maxTextureSize = 8192; //8192 for DX10 and better
  _hdrPrecWanted = ValidateHDRPrecision(_hdrPrecWanted);

  DXGI_FORMAT bestRTFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

  // try using user's preferred choice
  switch (_hdrPrecWanted)
  {
  case HDR32:
    bestRTFormat = SelectRTFormat(128);
    if (bestRTFormat!=DXGI_FORMAT_R8G8B8A8_UNORM) break;
    // may fall-through
  case HDR16:
    bestRTFormat = SelectRTFormat(64);
    if (bestRTFormat!=DXGI_FORMAT_R8G8B8A8_UNORM) break;
    // may fall-through
  default:
    bestRTFormat = SelectRTFormat(0);
  }

  _caps._canWriteSRGB = true; //in DX11 we expect SRGB by default

  // select suitable depth-buffer formats
  _depthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

  _caps._hasStencilBuffer = true;
  _caps._canTwoSidedStencil = true;


#if _XBOX
  if (_w==1280)
  {
    DoAssert(_h==720);
    _wRT = 1024;
    _hRT = 576;
  }
  else
  {
    _wRT = _w;
    _hRT = _h;
  }
#else
  FindFillrateControl(_wRTPreferred,_hRTPreferred);
  _wRT = _wRTRounded;
  _hRT = _hRTRounded;
#endif

  /// initialize fsaa modes
  InitFSAAModes(bestRTFormat, _depthFormat);

  // shadow map format
  _smFormat = DXGI_FORMAT_R32_FLOAT;

  _rtFormat = bestRTFormat;
  _rtMultiSampleCount = 1; //1 = no MSAA
  _rtMultiSampleQuality = 0;
  _maxFSAA = _fsaaModes.Size();


  saturate(_fsAAWanted, 0, _maxFSAA);

  if (_fsAAWanted>0)
  {
    FSAAInfo& info = _fsaaModes[_fsAAWanted-1];
    _rtMultiSampleCount = info._samplesCount;
    _rtMultiSampleQuality = info._quality;
  }

  if (_rtMultiSampleCount < 1)
    _rtMultiSampleCount = 1; //always at least 1 sample (no MSAA)

  _useAToC = (_rtMultiSampleCount > 1 && _AToCMask != 0);

  // downsample format
  _downsampleFormat = _rtFormat;
  LogF("Down-sample surface format %s",SurfaceFormatNameDD11(_downsampleFormat));
}


void EngineDD11::ResetLogDevice()
{
#if LOG_RENDER_API
  // before resetting the log, we may optionally output it

  if (GInput.GetCheat3ToDo(DIK_Y))
  {
    if (CheckMainThread())
    {
      BString<128> outName;
      sprintf(
        outName,"render-%s-%s.log",
        Glob.config._predication ? "pred" : "norm",
        Glob.config._useCB ? "useCB" : "noCB"
        );
      QOFStream out(outName);
      out.write(RenderLog.Data(),RenderLog.Size()-1); // -1 to avoid saving zero terminator
    }
    else
    {
      LogF("Cannot write RenderLog from background thread");
    }
  }


  RESET_LOG_DEVICE();
#endif
}

void EngineDD11::DoSetGamma()
{
#ifdef _XBOX
  // Initialize gamma value
  float eGamma = 1.0f / (_gamma * 2.2f);

  // Either use SetPWLGamma or SetGammaRamp (depending on front buffer format)
  if (
    _rtFormat == D3DFMT_A2B10G10R10 ||
    _rtFormat == D3DFMT_X2R10G10B10
    )
  {
    D3DPWLGAMMA pwg;
    for (int i = 0; i < 128; i++)
    {
      // Get x from <0, 1> range
      float xa = i * (1.0f / 128.0f);
      float xb = (i + 1) * (1.0f / 128.0f);

      // Get gamma function value
      float fxa = pow(xa, eGamma);
      float fxb = pow(xb, eGamma);

      // Convert to 16b
      int ifxa = toInt(fxa*65535.0); // both PC and X360 gamma ramp is 16b
      int ifxb = toInt(fxb*65535.0); // both PC and X360 gamma ramp is 16b

      // Get the base and delta
      pwg.red[i].Base = pwg.green[i].Base = pwg.blue[i].Base = ifxa;
      pwg.red[i].Delta = pwg.green[i].Delta = pwg.blue[i].Delta = (ifxb - ifxa);
    }
    _d3dDevice->SetPWLGamma(0, &pwg);
  }
  else
  {
    D3DGAMMARAMP ramp;
    for( int i=0; i<256; i++ )
    {
      float x=i*(1/255.0);
      float fx=pow(x,eGamma);
      int ifx=toInt(fx*65535.0); // both PC and X360 gamma ramp is 16b
      saturate(ifx,0,65535.0);
      ramp.red[i]=ramp.green[i]=ramp.blue[i]=ifx;
    }
    _d3dDevice->SetGammaRamp(0, 0,&ramp);
  }
#else

  // DX11 TODO - not working
  if (_windowed)
    return; //works only in fullscreen

  Assert(_dxgiOutput);
  if (!_dxgiOutput)
    return;

  Assert(!_d3dDeviceNonRec.IsNull());
  if (_d3dDeviceNonRec.IsNull())
    return;

  DXGI_GAMMA_CONTROL gc;
  gc.Scale.Red = 1.0f;
  gc.Scale.Green = 1.0f;
  gc.Scale.Blue = 1.0f;
  gc.Offset.Red = 0.0f;
  gc.Offset.Green = 0.0f;
  gc.Offset.Blue = 0.0f;

  float eGamma=1/_gamma;
  static const int numValues = 1024;
  for( int i=0; i<numValues; i++ )
  {
    float x=i*(1.0f/numValues);
    float fx=pow(x,eGamma);
    gc.GammaCurve[i].Red = gc.GammaCurve[i].Green = gc.GammaCurve[i].Blue = fx;
  }

  {
    // better lock DX
    //LOCK_DX11;
    //HRESULT hr = _dxgiOutput->TakeOwnership(_d3dDeviceNonRec.GetDeviceRef(), TRUE);
    //if (FAILED(hr))
    //{
    //  DDError11("TakeOwnership failed:", hr);
    //}
    //else
    //{
    //  hr = _dxgiOutput->SetGammaControl(&gc);
    //  if (FAILED(hr))
    //  {
    //    DDError11("SetGammaControl failed:", hr);
    //  }

    //  _dxgiOutput->ReleaseOwnership();
    //}
  }
#endif

  LogF("Info: Set gamma %.3f", _gamma);
}

void EngineDD11::SetGamma( float gamma )
{
  saturate(gamma,1e-3,1e3);
  _gamma=gamma;
  if (!_d3dDeviceRec.IsNull())
  {
    DoSetGamma();
  }

}

void EngineDD11::CreateVB()
{
  ReportGRAM("Before CreateVertexBuffer");
  for (int i=0; i<NumDynamicVB2D; i++)
  {
    HRESULT err;

    // Create NonTL vertex buffer
    {
      D3D11_BUFFER_DESC bd;
      bd.Usage = D3D11_USAGE_DYNAMIC;
      bd.ByteWidth = MeshBufferLength * sizeof(TLVertex);
      bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
      bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      bd.MiscFlags = 0;

      ComRef<IDirect3DVertexBuffer9Old> vBuffer;
      err = _d3dDeviceRec->CreateBuffer(&bd, NULL, vBuffer.Init());
      _queueNo._meshBuffer[i].SetResult(vBuffer);
      if (err!=D3D_OK || !vBuffer)
      {
        DDError11("Error: Cannot create vertex buffer", err);
        return;
      }

    }

    // Create NonTL index buffer
    {
      D3D11_BUFFER_DESC bd;
      bd.Usage = D3D11_USAGE_DYNAMIC;
      bd.ByteWidth = IndexBufferLength * sizeof(VertexIndex);
      bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
      bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      bd.MiscFlags = 0;
      ComRef<IDirect3DIndexBuffer9Old> iBuffer;
      err = _d3dDeviceRec->CreateBuffer(&bd, NULL, iBuffer.Init());
      _queueNo._indexBuffer[i].SetResult(iBuffer);
      if (err!=D3D_OK || !iBuffer)
      {
        ReportMemoryStatusWithVRAM();
        DDError11("Error: Cannot create index buffer", err);
        return;
      }
    }


  }
  ReportGRAM("After CreateVertexBuffer");
  _queueNo._vertexBufferUsed = 0;
  _queueNo._indexBufferUsed = 0;
  _queueNo._actualIBIndex;
  _queueNo._actualVBIndex;
} 

void EngineDD11::DestroyVB()
{
  for (int i=0; i<NumDynamicVB2D; i++)
  {
    _queueNo._meshBuffer[i].SetResult(ComRef<IDirect3DVertexBuffer9Old>());
    _queueNo._indexBuffer[i].SetResult(ComRef<IDirect3DIndexBuffer9Old>());
  }
  //_d3dDevice->SetStreamSource(0,NULL,0);
}

void EngineDD11::CreateVBTL()
{
  _dynVBuffer.Init(MaxShapeVertices,_d3dDeviceRec);
  //CreatePostProcessStuff();
} 

void EngineDD11::DestroyVBTL()
{
  //DestroyPostProcessStuff();
  _dynVBuffer.Dealloc();
}


void EngineDD11::DeleteVB(VertexStaticData11 *vb)
{
  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  for (int cb=0; cb<CBStateCount(); cb++)
  {
    // if someone is releasing a buffer we have still set, we have a problem
    // if the buffer used it permanent, we can be safe it is not the one we are discarding
    // we can release VBs during simulation, at which point HL recording is turned on, owning the state
    //DoAssert(IsPermanent(_cbState[cb]._vBufferLast));
    // following condition is actually all we need, but with above we can be sure this is true as well,
    // and it is easier to enforce it consistently
    DoAssert(!_cbState[cb]._vBufferLast.IsSameFuture(vb->_vBuffer));
  }
}
void EngineDD11::DeleteIB(IndexStaticData11 *ib)
{
  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  // similar to DeleteVB
  for (int cb=0; cb<CBStateCount(); cb++)
  {
    //DoAssert(IsPermanent(_cbState[cb]._iBufferLast));
    DoAssert(!_cbState[cb]._iBufferLast.IsSameFuture(ib->_iBuffer));
  }
}

void EngineDD11::BufferReleased(VertexBufferD3D11 *buf)
{
  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  _vBufferLRU.Delete(buf);
}

void EngineDD11::SetVSourceStatic(int cb, const Shape &src, const EngineShapeProperties &prop, const VertexBufferD3D11 &buf, VSDVersion vsdVersion)
{
  RendState &cbState = CBState(cb);
  PROFILE_DX_SCOPE_DETAIL(3dvbS);
  // Separate part

  // if shape requires any special properties, set them
  if (src.GetShapePropertiesNeeded()&ESPTreeCrown)
  {
    prop.GetTreeCrown(cbState._treeCrown);
  }


  // Set UV dynamic range constants
  UVPair scale[MAX_UV_SETS];
  for (int i = 0; i < MAX_UV_SETS; i++)
  {
    scale[i].u = buf._maxUV[i].u - buf._minUV[i].u;
    scale[i].v = buf._maxUV[i].v - buf._minUV[i].v;
    saturateMax(scale[i].u,1e-6f);
    saturateMax(scale[i].v,1e-6f);
  }
  SetTexGenScaleAndOffset(cb,scale, buf._minUV);

  VertexStaticData11 *dtaV = buf.GetSharedV();
  if (dtaV)
  {
    SetStreamSource(cb,dtaV->_vBuffer, dtaV->VertexSize(), dtaV->Size()); // TODO: get Future value later
    //_vertexDecl = _vertexDeclD[src.GetVertexDeclaration()][vsdVersion];

#if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("SetVBSourceStatic shared, vertex size %d",dtaV->VertexSize());
    }
#endif

    dtaV->Used();
  }
  IndexStaticData11 *dtaI = buf.GetSharedI();
  if (dtaI)
  {
    dtaI->Used();
  }
  int vOffset = 0;

  if (dtaI && (!cbState._iBufferLast.IsSameFuture(dtaI->_iBuffer) || cbState._vOffsetLast!=vOffset))
  {
    ADD_COUNTER(d3xIB,1);
    CALL_CB(cb,IASetIndexBuffer,dtaI->_iBuffer, DXGI_FORMAT_R16_UINT, 0);
    cbState._iBufferLast = dtaI->_iBuffer;
    cbState._vOffsetLast = vOffset;
#if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("SetIBSourceStatic shared");
    }
#endif
  }
}

void EngineDD11::SetVSourceDynamic(
                                  int cb, const VertexTableAnimationContext *animContext, const Shape &src, const EngineShapeProperties &prop, const VertexBufferD3D11 &buf
                                  )
{
  PROFILE_DX_SCOPE_DETAIL(3dvbD);

  // Zero vertex and index buffer in the device, forget _iBufferLast and _vBufferLast.
  ForgetDeviceBuffers(cb);

#ifdef DONT_USE_DYNBUFFERS
  _dynVBuffer.AddVertices(cb,animContext, src);
#else
  RendState &cbState = CBState(cb);
  // when recording CBs, we cannot use dynamic buffers
  Assert(cb<0);
  int vOffset = _dynVBuffer.AddVertices(cb,animContext, src);
  const ComRef<IDirect3DVertexBuffer9> &dbuf = _dynVBuffer.GetVBuffer();
  SetStreamSource(cb,dbuf, sizeof(SVertex9), _dynVBuffer._vBufferSize);

  if (buf._separate)
  {

    if (cbState._iBufferLast!=buf._iBuffer || vOffset!=cbState._vOffsetLast)
    {

#if DO_TEX_STATS
      if (LogStatesOnce)
      {
        LogF("SetIBSourceDynamic separate");
      }
#endif

      ADD_COUNTER(d3xIB,1);
      CALL_CB(cb,SetIndices,buf._iBuffer);
      cbState._iBufferLast = buf._iBuffer;
      cbState._vOffsetLast = vOffset;
    }
  }
  else
  {
    IndexStaticData11 *dtaI = buf._sharedI;
    if (dtaI && (cbState._iBufferLast!=dtaI->_iBuffer || vOffset!=cbState._vOffsetLast))
    {
#if DO_TEX_STATS
      if (LogStatesOnce)
      {
        LogF("SetIBSourceDynamic shared");
      }
#endif

      ADD_COUNTER(d3xIB,1);
      CALL_CB(cb,SetIndices,dtaI->_iBuffer);
      cbState._iBufferLast = dtaI->_iBuffer;
      cbState._vOffsetLast = vOffset;
    }
  }
#endif
}

void EngineDD11::OnSizeChanged(VertexBufferD3D11 *buf)
{
  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  _vBufferLRU.RefreshMemoryControlled(buf);
}

void EngineDD11::SetStreamSource(int cb, const VertexStaticData11::BufferFuture &buf, int stride, int bufSize, bool optimize)
{
  ADD_COUNTER(d3xVB,1);
  RendState &cbState = CBState(cb);

  if (!optimize || !cbState._vBufferLast.IsSameFuture(buf))
  {
    CALL_CB(cb,IASetVertexBuffers, 0, buf, stride, 0);
#if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("Stream source change");
    }
#endif
  }

  cbState._vBufferLast = buf;
  cbState._vBufferSize = bufSize;
  cbState._strideLast = stride;
}

void EngineDD11::DoSetStreamSource(const VertexStaticData11::BufferFuture &buf, int stride, int bufSize)
{
  AssertSpecialLevelThreadAccess();
  _d3dDeviceRec->IASetVertexBuffers(0, buf, stride, 0);
}

size_t EngineDD11::FreeOnDemandSystemMemoryLowLevel(size_t toRelease) const
{
  /*
  When any texture / VB allocation or lock failed because of memory allocation this means:
  - on Xbox: there is no more physical memory left
  - on PC: there is no more virtual address space left
  */
#if _XBOX
  // we need more physical memory
  return ::FreeOnDemandSystemMemoryLowLevel(toRelease);
#else
  // most likely we are out of virtual address space
  // this is something the game memory manager is currently unable to release
  // we will try to release some VRAM resources as well and hope it will fix it
  size_t releasedSys = ::FreeOnDemandSystemMemoryLowLevel(toRelease);
  size_t releasedVRAM = _textBank->ForcedReserveMemory(toRelease);
  if (releasedSys>releasedVRAM) return releasedSys;
  return releasedVRAM;
#endif
}

HRESULT EngineDD11::Map(ID3D11Resource *resource, UINT subResource, D3D11_MAP mapType, UINT mapFlags, void **ppData)
{
  Assert(resource);
  if (!resource)
    return E_FAIL;

  D3D11_MAPPED_SUBRESOURCE mappedSub;
  HRESULT res = _d3dDeviceRec->Map(resource, subResource, mapType, mapFlags, &mappedSub);
  if (res == S_OK)
  {
    *ppData = mappedSub.pData;
  }
  else
  {
    *ppData = 0;
  }
  return res; 
}

void EngineDD11::Unmap(ID3D11Resource *resource, UINT subResource)
{
  Assert(resource);
  if (!resource)
    return;
  return _d3dDeviceRec->Unmap(resource, subResource);
}


Ref<IndexStaticData11> EngineDD11::AddShapeIndices(const FaceArray &src, bool dynamic, int nInstances, int instanceVertices)
{
  PROFILE_SCOPE_DETAIL_EX(ddShI,d3d);

  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());

  CREATE_BUFFER_DATA(IndexStaticData11, AddShapeIndicesOp, (src, dynamic, nInstances, instanceVertices));
}


Ref<IndexStaticData11> EngineDD11::AddShapeIndicesSprite(const FaceArray &src, bool dynamic, int nInstances)
{
  PROFILE_SCOPE_DETAIL_EX(ddSIS,d3d);

  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());

  CREATE_BUFFER_DATA(IndexStaticData11, AddShapeIndicesSpriteOp, (src, dynamic, nInstances));
}


Ref<IndexStaticData11> EngineDD11::AddShapeIndicesPoint(const FaceArray &src, bool dynamic, int nInstances, int instanceVertices)
{
  PROFILE_SCOPE_DETAIL_EX(ddSIP,d3d);
  DoAssert(nInstances == 1);

  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());

  CREATE_BUFFER_DATA(IndexStaticData11, AddShapeIndicesPointOp, (src, dynamic, nInstances, instanceVertices));
}

bool EngineDD11::CanCreateBuffersAsync() const
{
  return MipLoadingAsync && Glob.config._useCB && GFileServerFunctions->OtherThreadsCanUse() && CheckExThreads(4,GetCPUCount()>=4);
}

template <int vertexDecl>
class AddShapeOp: public AddShapeOpBase<vertexDecl, typename VertexDeclTraits<vertexDecl>::VertexType >
{
  typedef AddShapeOpBase<vertexDecl, typename VertexDeclTraits<vertexDecl>::VertexType > base;

public:
  AddShapeOp(const VertexTable &src, VertexBufferD3D11 *vbuf, bool frequent, bool dynamic)
    :base(src,vbuf,frequent,dynamic)
  {
  }

  Ref<VertexStaticData11> CreateData(bool async)
  {
    // Either we didn't fit into existing or we use a separate buffer, so create a new buffer
    _result = new VertexStaticData11(_data->NVertex(), sizeof(VertexDeclTraits<vertexDecl>::VertexType), _frequent);

    if (async)
    {
      _result->InitFuture();
      return _result;
    }
    else
    {
      ComRef<IDirect3DVertexBuffer9Old> buffer = CreateBuffer();
      // If we failed to init vertex buffer for the static data, then free static data and return error
      if (!buffer)
      {
        _result.Free();
        return NULL;
      }

      _result->Init(buffer);
      return _result;
    }
  }

  // Create the buffer when used asynchronously
  void operator () () const
  {
    PROFILE_SCOPE_EX(vssCB,dd11);
    ComRef<IDirect3DVertexBuffer9Old> buffer = CreateBuffer();
    _result->SetFuture(buffer);
  }

protected:
  ComRef<IDirect3DVertexBuffer9Old> CreateBuffer() const
  {
    PROFILE_SCOPE_EX(vssCB,dd11);
    ComRef<IDirect3DVertexBuffer9Old> buffer = _result->CreateBuffer();
    if (_dynamic) return buffer;

    if (!buffer)
    {
      buffer.Free();
      return buffer;
    }

    PROFILE_DX_SCOPE(3dvbC);
    void *dataMem = _result->Lock(buffer, _result->Size() * _result->VertexSize());
    if (!dataMem)
    {
      buffer.Free();
      return buffer;
    }

    // Fill out the particular data
    VertexStaticData11::AddShapeData<vertexDecl>(dataMem, *_data, _vbuf);

    // Process the data (Unlock)
    GEngineDD11->Unmap(buffer, 0);

    return buffer;
  }
};

template <int vertexDecl>
Ref<VertexStaticData11> EngineDD11::AddShapeVertices(
  const Shape &src, VertexBufferD3D11 &vbuf, bool dynamic, bool separate, bool frequent
  )
{
  PROFILE_SCOPE_DETAIL_EX(ddShV,dd11);

  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());

  CREATE_BUFFER_DATA(VertexStaticData11, AddShapeOp<vertexDecl>, (src, &vbuf, frequent, dynamic));
}


template <int vertexDecl>
Ref<VertexStaticData11> EngineDD11::AddShapeVerticesInstanced(
  const Shape &src, VertexBufferD3D11 &vbuf, bool dynamic, bool separate, bool frequent,
  int instancesCount
  )
{
  PROFILE_SCOPE_DETAIL_EX(ddSVI,dd11);

  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());

  CREATE_BUFFER_DATA(VertexStaticData11, AddShapeInstancedOp<vertexDecl>, (src, &vbuf, instancesCount, frequent, dynamic));
}

class AddShapeSpriteInstancedOp : public VirtDelegate0<void>
{
protected:
  mutable Ref<VertexStaticData11> _result;

  Ref<const VertexTableData> _data;
  bool _frequent;
  int _instancesCount;

public:

  AddShapeSpriteInstancedOp(const VertexTable &src, bool frequent, int instancesCount)
    :_data(src.GetVertexTableData()),_frequent(frequent),_instancesCount(instancesCount) {}

  Ref<VertexStaticData11> CreateData(bool async)
  {
    int bufferVertexCount = 4 * _instancesCount;

    _result = new VertexStaticData11(bufferVertexCount, sizeof(SVertexSpriteInstanced9), _frequent);

    if (async)
    {
      _result->InitFuture();
      return _result;
    }
    else
    {
      ComRef<IDirect3DVertexBuffer9Old> buffer = CreateBuffer();

      // If we failed to init vertex buffer for the static data, then free static data and return error
      if (!buffer)
      {
        _result.Free();
        return NULL;
      }

      _result->Init(buffer);
      return _result;
    }
  }

  // Create the buffer when used asynchronously
  void operator () () const
  {
    PROFILE_SCOPE_EX(vssCB,dd11);
    ComRef<IDirect3DVertexBuffer9Old> buffer = CreateBuffer();
    _result->SetFuture(buffer);
  }

protected:
  ComRef<IDirect3DVertexBuffer9Old> CreateBuffer() const
  {
    ComRef<IDirect3DVertexBuffer9Old> buffer = _result->CreateBuffer();
    if (!buffer)
    {
      buffer.Free();
      return buffer;
    }

    Assert(_result->VertexSize() == sizeof(SVertexSpriteInstanced9));
    DoAssert(_data->NVertex() == 4);

    PROFILE_DX_SCOPE(3dvbC);

    void *dataMem = _result->Lock(buffer, _result->Size() * _result->VertexSize());
    if (!dataMem)
    {
      buffer.Free();
      return buffer;
    }

    // Add shape into the new buffer
    VertexStaticData11::AddShapeSpriteInstanced(dataMem, *_data, _instancesCount);

    // Process the data (Unlock)
    GEngineDD11->Unmap(buffer, 0);

    return buffer;
  }
};


Ref<VertexStaticData11> EngineDD11::AddShapeVerticesSpriteInstanced(
  const Shape &src, VertexBufferD3D11 &vbuf, bool dynamic, bool separate, bool frequent, int instancesCount
  )
{
  Assert(!dynamic);
  if (dynamic) return NULL;

  CREATE_BUFFER_DATA(VertexStaticData11, AddShapeSpriteInstancedOp, (src, frequent, instancesCount));
}


class AddShapePointOp : public VirtDelegate0<void>
{
  mutable Ref<VertexStaticData11> _result;

  Ref<const VertexTableData> _data;
  bool _frequent;

public:

  AddShapePointOp(const VertexTable &src, bool frequent)
    :_data(src.GetVertexTableData()), _frequent(frequent) {}

  Ref<VertexStaticData11> CreateData(bool async)
  {
    int bufferVertexCount = 4 * _data->NVertex();

    _result = new VertexStaticData11(bufferVertexCount, sizeof(SVertexPoint9), _frequent);

    if (async)
    {
      _result->InitFuture();
      return _result;
    }
    else
    {
      ComRef<IDirect3DVertexBuffer9Old> buffer = CreateBuffer();

      // If we failed to init vertex buffer for the static data, then free static data and return error
      if (!buffer)
      {
        _result.Free();
        return NULL;
      }

      _result->Init(buffer);
      return _result;
    }
  }

  // Create the buffer when used asynchronously
  void operator () () const
  {
    PROFILE_SCOPE_EX(vssCB,dd11);
    ComRef<IDirect3DVertexBuffer9Old> buffer = CreateBuffer();
    _result->SetFuture(buffer);
  }

protected:
  ComRef<IDirect3DVertexBuffer9Old> CreateBuffer() const
  {
    ComRef<IDirect3DVertexBuffer9Old> buffer = _result->CreateBuffer();
    if (!buffer)
    {
      buffer.Free();
      return buffer;
    }

    Assert(_result->VertexSize() == sizeof(SVertexPoint9));
    PROFILE_DX_SCOPE(3dvbC);

    void *dataMem = _result->Lock(buffer, _result->Size() * _result->VertexSize());
    if (!dataMem)
    {
      buffer.Free();
      return buffer;
    }

    // Add shape into the new buffer
    VertexStaticData11::AddShapePoint(dataMem, *_data);

    // Process the data (Unlock)
    GEngineDD11->Unmap(buffer, 0);

    return buffer;
  }
};

Ref<VertexStaticData11> EngineDD11::AddShapeVerticesPoint(const Shape &src, VertexBufferD3D11 &vbuf, bool dynamic, bool separate, bool frequent)
{
  if (dynamic) return NULL;

  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());

  CREATE_BUFFER_DATA(VertexStaticData11, AddShapePointOp, (src, frequent));
}

#if _ENABLE_SKINNEDINSTANCING

VertexStaticData11 *EngineDD11::AddShapeVerticesSkinnedInstanced(
  const Shape &src, bool dynamic, bool separate, bool frequent,
  int instancesCount
  )
{

  // Handle special cases
  //  if (src.GetSubSkeletonSize() == 0)
  //  {
  if (instancesCount == 1)
  {
    return AddShapeVertices(src, vIndex, dynamic, separate);
  }
  //    else
  //    {
  //      return AddShapeVerticesInstanced(src, vIndex, dynamic, separate, instancesCount);
  //    }
  //  }

  vIndex = NULL;
  if (dynamic) return NULL;

  // Number of items in the new vertex buffer
  int bufferVertexCount = src.NVertex() * instancesCount;

  // Either we didn't fit into existing or we use a separate buffer, so create a new buffer
  VertexStaticData11 *sd = new VertexStaticData11;
  sd->Init(this, bufferVertexCount, sizeof(SVertexSkinned9), _d3dDevice);

  // If we failed to init vertex buffer for the static data, then free static data and return error
  if (!sd->_vBuffer)
  {
    delete sd;
    return NULL;
  }

  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  // Add shape into the new buffer
  if (sd->AddShapeSkinnedInstanced(src, vIndex, dynamic, instancesCount))
  {
    _statVBuffer.Add(sd);
    return sd;
  }
  delete sd;
  return NULL;
}

VertexStaticData11 *EngineDD11::AddShapeVerticesNormalSkinnedInstanced(
  const Shape &src, bool dynamic, bool separate, bool frequent,
  int instancesCount
  )
{

  // Handle special cases
  //  if (src.GetSubSkeletonSize() == 0)
  //  {
  if (instancesCount == 1)
  {
    return AddShapeVerticesNormal(src, vIndex, dynamic, separate);
  }
  //    else
  //    {
  //      return AddShapeVerticesNormalInstanced(src, vIndex, dynamic, separate, instancesCount);
  //    }
  //  }

  vIndex = NULL;
  if (dynamic) return NULL;

  // Number of items in the new vertex buffer
  int bufferVertexCount = src.NVertex() * instancesCount;

  // Either we didn't fit into existing or we use a separate buffer, so create a new buffer
  VertexStaticData11 *sd = new VertexStaticData11;
  sd->Init(this, bufferVertexCount, sizeof(SVertexNormalSkinned9), _d3dDevice);

  // If we failed to init vertex buffer for the static data, then free static data and return error
  if (!sd->_vBuffer)
  {
    delete sd;
    return NULL;
  }

  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  // Add shape into the new buffer
  if (sd->AddShapeNormalSkinnedInstanced(src, vIndex, dynamic, instancesCount))
  {
    _statVBuffer.Add(sd);
    return sd;
  }
  delete sd;
  return NULL;
}

VertexStaticData11 *EngineDD11::AddShapeVerticesShadowVolumeSkinnedInstanced(
  const Shape &src, bool dynamic, bool separate, bool frequent,
  int instancesCount
  )
{

  // Handle special cases
  //  if (src.GetSubSkeletonSize() == 0)
  //  {
  if (instancesCount == 1)
  {
    return AddShapeVerticesShadowVolume(src, vIndex, dynamic, separate);
  }
  //    else
  //    {
  //      return AddShapeVerticesNormalInstanced(src, vIndex, dynamic, separate, instancesCount);
  //    }
  //  }

  vIndex = NULL;
  if (dynamic) return NULL;

  // Number of items in the new vertex buffer
  int bufferVertexCount = src.NVertex() * instancesCount;

  // Either we didn't fit into existing or we use a separate buffer, so create a new buffer
  VertexStaticData11 *sd = new VertexStaticData11;
  sd->Init(this, bufferVertexCount, sizeof(SVertexShadowVolumeSkinned9), _d3dDevice);

  // If we failed to init vertex buffer for the static data, then free static data and return error
  if (!sd->_vBuffer)
  {
    delete sd;
    return NULL;
  }

  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  // Add shape into the new buffer
  if (sd->AddShapeShadowVolumeSkinnedInstanced(src, vIndex, dynamic, instancesCount))
  {
    _statVBuffer.Add(sd);
    return sd;
  }
  delete sd;
  return NULL;
}

#endif //#if _ENABLE_SKINNEDINSTANCING


void EngineDD11::SetTexGenScaleAndOffset(int cb, const UVPair *scale, const UVPair *offset)
{
  for (int i = 0; i < MAX_UV_SETS; i++)
  {
    if (CBState(cb)._texGenOffset[i]!=offset[i] || CBState(cb)._texGenScale[i]!=scale[i])
    {
      Assert(scale[i].u>0);
      Assert(scale[i].v>0);
      CBState(cb)._texGenScale[i] = scale[i];
      CBState(cb)._texGenOffset[i] = offset[i];
      CBState(cb)._texGenScaleOrOffsetHasChanged = true;
    }
  }
}

void EngineDD11::Clear(bool clearZ, bool clear, PackedColor color, bool clearStencil)
{
  // Clear render target if required
  if (clear && _currRenderTarget.NotNull())
  {
    Color fColor = color;
    CALL_D3D(ClearRenderTargetView, _currRenderTarget._rtView, Array<float>((float*)&fColor, 4));
  }

  // Clear depth-stencil if required
  if (_currDepthStencil.NotNull() && clearZ)
  {
    UINT clearFlags = D3D11_CLEAR_DEPTH;
    if (clearStencil)
      clearFlags |= D3D11_CLEAR_STENCIL;
    CALL_D3D(ClearDepthStencilView, _currDepthStencil._dsView, clearFlags, 1.0f, 0);
  }
}

HRESULT EngineDD11::CreateTextureSimple2D(UINT width, UINT height,
                                  UINT mipLevels,
                                  DXGI_FORMAT format,
                                  D3D11_USAGE usage,
                                  UINT cpuAccessFlags,
                                  SurfaceDataD3D11& result)
{
  D3D11_TEXTURE2D_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = mipLevels;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1; 
  desc.SampleDesc.Quality = 0; 
  desc.Usage = usage;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  desc.CPUAccessFlags = cpuAccessFlags;
  desc.MiscFlags = 0;

  return CreateTextureSimple2D(desc, result);
}

HRESULT EngineDD11::CreateTextureSimple2D(const D3D11_TEXTURE2D_DESC& desc,
                                          SurfaceDataD3D11& result)
{
  HRESULT err = _d3dDeviceRec->CreateTexture2D(&desc, NULL, result._texture.Init());
  if (FAILED(err))
  {
    DDError11("Failed to create texture: ", err);
    return err;
  }

  // resource view
  err = _d3dDeviceRec->CreateShaderResourceView(result._texture, NULL, result._resView.Init());
  if (FAILED(err))
  {
    DDError11("Error: Create Texture resource view error", err);
    return err;
  }

  return err;
}

HRESULT EngineDD11::CreateTextureSimple3D(UINT width, UINT height, UINT depth,
                                          UINT mipLevels,
                                          DXGI_FORMAT format,
                                          D3D11_USAGE usage,
                                          UINT cpuAccessFlags,
                                          SurfaceDataD3D11& result)
{
  D3D11_TEXTURE3D_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.Width = width;
  desc.Height = height;
  desc.Depth = depth;
  desc.MipLevels = mipLevels;
  desc.Format = format;
  desc.Usage = usage;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  desc.CPUAccessFlags = cpuAccessFlags;
  desc.MiscFlags = 0;

  return CreateTextureSimple3D(desc, result);
}

HRESULT EngineDD11::CreateTextureSimple3D(const D3D11_TEXTURE3D_DESC& desc,
                                          SurfaceDataD3D11& result)
{
  HRESULT err = _d3dDeviceRec->CreateTexture3D(&desc, NULL, result._texture3D.Init());
  if (FAILED(err))
  {
    DDError11("Failed to create 3d texture: ", err);
    return err;
  }

  // resource view
  err = _d3dDeviceRec->CreateShaderResourceView(result._texture3D, NULL, result._resView.Init());
  if (FAILED(err))
  {
    DDError11("Error: Create Texture resource view error", err);
    return err;
  }

  return err;
}

HRESULT EngineDD11::CreateTexture(UINT width, UINT height,
                                  UINT mipLevels,
                                  DXGI_FORMAT format,
                                  D3D11_USAGE usage, 
                                  SurfaceDataD3D11& result,
                                  bool createSRGBView,
                                  bool createRTView                                 
                                  )
{
  // can texture be accessed by cpu?
  bool cpuAccess = usage!=D3D11_USAGE_DEFAULT;

  // Texture format - we want to create typeless texture and than use
  // SRGB and non-SRGB views as needed.
  DXGI_FORMAT textureFormat = GetCorrespondingTypelessFormat(format);
  DXGI_FORMAT viewFormat = format;
  DXGI_FORMAT srgbViewFormat = GetCorrespondingSRGBFormat(format);

  D3D11_TEXTURE2D_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = mipLevels;
  desc.ArraySize = 1;
  desc.Format = textureFormat;
  desc.SampleDesc.Count = 1; 
  desc.SampleDesc.Quality = 0; 
  desc.Usage = usage;
  if (cpuAccess)
  {
    desc.BindFlags = 0;
  }
  else
  {
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (createRTView)
      desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
  }
  desc.CPUAccessFlags = cpuAccess ? (D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ) : 0;
  desc.MiscFlags = 0;
  HRESULT err = _d3dDeviceRec->CreateTexture2D(&desc, NULL, result._texture.Init());
  if (FAILED(err))
  {
    Fail("Error: Texture creation failed");
    LogF("Dimensions %d:%d,%d",width,height,mipLevels);
    DDError11("Failed to create texture: ", err);
  }
  else if (!cpuAccess)
  {
    // Texture was created, now create the resource views

    // standard resource view
    D3D11_SHADER_RESOURCE_VIEW_DESC srv;
    ZeroMemory(&srv, sizeof(srv));
    srv.Format = viewFormat;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = mipLevels;
    err = _d3dDeviceRec->CreateShaderResourceView(result._texture, &srv, result._resView.Init());
    if (err!=S_OK)
    {
      Fail("Error: Texture resource view creation failed");
      DDError11("Error: Create Texture resource view error", err);
    }

    if (createSRGBView)
    {
      // SRGB resource view
      if (viewFormat == srgbViewFormat)
      {
        // same format, no need to create new resource view
        result._resViewSRGB = result._resView;     
      }
      else
      {
        D3D11_SHADER_RESOURCE_VIEW_DESC srv;
        ZeroMemory(&srv, sizeof(srv));
        srv.Format = srgbViewFormat;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = mipLevels;
        err = _d3dDeviceRec->CreateShaderResourceView(result._texture, &srv, result._resViewSRGB.Init());
        if (err!=S_OK)
        {
          Fail("Error: Texture resource view creation failed");
          DDError11("Error: Create Texture SRGB resource view error", err);
        }
      }
    }

    // create render target view if needed
    if (createRTView)
    {
      D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
      ZeroMemory(&rtvDesc, sizeof(rtvDesc));
      rtvDesc.Format = viewFormat;
      rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
      rtvDesc.Texture2D.MipSlice = 0;
      err = _d3dDeviceRec->CreateRenderTargetView(result._texture, &rtvDesc, result._rtView.Init());
      if (err!=S_OK)
      {
        Fail("Error: Texture resource view creation failed");
        DDError11("Error: Create Texture render target view error", err);
      }
    }
  }

  return err;
}

HRESULT EngineDD11::CreateRenderTarget(
  UINT width, UINT height,
  DXGI_FORMAT format,
  UINT multisampleCount, UINT multisampleQuality,
  D3D11_USAGE usage, 
  bool createShaderResource,
  RenderTarget& result,
  DXGI_FORMAT rtFormat,
  DXGI_FORMAT srFormat
  )
{
  bool isMSAA = multisampleCount > 1;

  // Create description
  D3D11_TEXTURE2D_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = multisampleCount; 
  desc.SampleDesc.Quality = multisampleQuality; 
  desc.Usage = usage;
  if (createShaderResource)
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  else
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  desc.CPUAccessFlags = 0;
  desc.MiscFlags = 0;
  HRESULT hr = _d3dDeviceRec->CreateTexture2D(&desc, NULL, result._resource.Init());
  if (FAILED(hr))
  {
    DDError11("Failed to create render target buffer: ", hr);
    ErrorMessage("Failed to create render target buffer");
  }
  else
  {
    // Create the view

    D3D11_RENDER_TARGET_VIEW_DESC rtDesc;
    ZeroMemory(&rtDesc, sizeof(rtDesc));
    rtDesc.Format = rtFormat; // this is ok - if UNKNOWN, then format of the parent resource is used
    if (isMSAA)
      rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
    else
      rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtDesc.Texture2D.MipSlice = 0;

    hr = _d3dDeviceRec->CreateRenderTargetView(result._resource, &rtDesc, result._rtView.Init());
    if (FAILED(hr))
    {
      DDError11("Cannot create render target view: ", hr);
      ErrorMessage("Cannot create render target view");
    }    
    else if (createShaderResource)
    {
      // Create shader resource view if requested

      D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;
      ZeroMemory(&srDesc, sizeof(srDesc));
      srDesc.Format = srFormat; // this is ok - if UNKNOWN, then format of the parent resource is used
      if (isMSAA)
        srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
      else
        srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;

      srDesc.Texture2D.MipLevels = 1;
      srDesc.Texture2D.MostDetailedMip = 0;

      hr = _d3dDeviceRec->CreateShaderResourceView(result._resource, &srDesc, result._srView.Init());
      if (FAILED(hr))
      {
        DDError11("Cannot create shader resource view for render target: ", hr);
        ErrorMessage("Cannot create shader resource view for render target");
      }    
    }
  }
  return hr;
}

HRESULT EngineDD11::CreateDepthStencil(
                                       UINT width, UINT height,
                                       DXGI_FORMAT format,
                                       UINT multisampleCount, UINT multisampleQuality,
                                       D3D11_USAGE usage, 
                                       bool createShaderResource,
                                       DepthStencil& result,
                                       DXGI_FORMAT dsFormat,
                                       DXGI_FORMAT srFormat
                                       )
{
  bool isMSAA = multisampleCount > 1;

  // Create description
  D3D11_TEXTURE2D_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = multisampleCount; 
  desc.SampleDesc.Quality = multisampleQuality; 
  desc.Usage = usage;
  if (createShaderResource)
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
  else
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  desc.CPUAccessFlags = 0; 
  desc.MiscFlags = 0;
  HRESULT hr = _d3dDeviceRec->CreateTexture2D(&desc, NULL, result._resource.Init());
  if (FAILED(hr))
  {
    DDError11("Failed to create depth/stencil buffer: ", hr);
    ErrorMessage("Failed to create depth/stencil buffer");
  }
  else
  {
    // create depth stencil view

    D3D11_DEPTH_STENCIL_VIEW_DESC dsDesc;
    ZeroMemory(&dsDesc, sizeof(dsDesc));
    dsDesc.Format = dsFormat; // this is ok - if UNKNOWN, then format of the parent resource is used
    if (isMSAA)
      dsDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
    else
      dsDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsDesc.Texture2D.MipSlice = 0;
    hr = _d3dDeviceRec->CreateDepthStencilView(result._resource, &dsDesc, result._dsView.Init());

    if (FAILED(hr))
    {
      DDError11("Cannot depth/stencil view: ", hr);
      ErrorMessage("Cannot depth/stencil view");
    }    
    else if (createShaderResource)
    {
      // Create shader resource view if requested

      D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;
      ZeroMemory(&srDesc, sizeof(srDesc));
      srDesc.Format = srFormat; // this is ok - if UNKNOWN, then format of the parent resource is used
      if (isMSAA)
        srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
      else
        srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srDesc.Texture2D.MipLevels = 1;
      srDesc.Texture2D.MostDetailedMip = 0;

      hr = _d3dDeviceRec->CreateShaderResourceView(result._resource, &srDesc, result._srView.Init());
      if (FAILED(hr))
      {
        DDError11("Cannot create shader resource view for depth/stencil: ", hr);
        ErrorMessage("Cannot create shader resource view for depth/stencil");
      }    
    }
  }
  return hr;
} 
  
void EngineDD11::CreateSBResources()
{
  for (int i = 0; i < ShadowBufferCascadeLayers; i++)
  {
    _oldLRSP[i] = MIdentity;
  }

  SBTR_DefaultCreate();
}

void EngineDD11::DestroySBResources()
{
  _depthStencilSB.Free();
}

bool EngineDD11::CreateResources()
{
  // Create the screen space shadow maps
  HRESULT err;

  DXGI_FORMAT sssmFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
#ifdef _XBOX
  {
    if ((err = _d3dDevice->CreateTexture(_wRT, _hRT, 1, D3DUSAGE_RENDERTARGET, sssmFormat, D3DPOOL_DEFAULT, _sssm.Init(), NULL)) == D3D_OK)
    {
      D3DSURFACE_PARAMETERS sp;
      memset(&sp, 0, sizeof(D3DSURFACE_PARAMETERS));
      sp.Base = 0;
      if ((err = CreateRenderTarget(_wRT, _hRT, sssmFormat, _rtMultiSampleType, _rtMultiSampleQuality, FALSE, _sssmSurface, false, &sp)) != D3D_OK)
      {
        DDError9("Error: _sssm surface creation failed", err);
      }
    }
    else
    {
      DDError9("Error: _sssm texture creation failed", err);
    }
  }
#else
  {
    // SSSM - never multisampled on PC
    err = CreateRenderTarget(_wRT, _hRT,sssmFormat, 1, 0, D3D11_USAGE_DEFAULT,
      true, _sssmRenderTarget);
    if (FAILED(err))
    {
      DDError11("Error: _sssm texture creation failed", err);
      return false;
    }

  }
#endif
#ifdef SSSMBLUR
  // SSSM blurred
  err = CreateRenderTarget(_wRT, _hRT,sssmFormat, 1, 0, D3D11_USAGE_DEFAULT,
    true, _sssmBRenderTarget);
  if (FAILED(err))
  {
    DDError11("Error: _sssmB surface creation failed", err);
  }
#endif

  return true;
}

void EngineDD11::DestroyResources()
{
#ifdef SSSMBLUR
  _sssmBRenderTarget.Free();
#endif
  _sssmRenderTarget.Free();

  if (_pp._ssao.NotNull())
    _pp._ssao->UnInit();

  _pp._ssao.Free();

  _ssaoRenderTarget.Free();
}

bool EngineDD11::CreateTIResources()
{
  HRESULT err;
  err = CreateTexture(16, 16, 1, DXGI_FORMAT_R32_FLOAT,
    D3D11_USAGE_DEFAULT, _tableTc, false, false);
  if (err ==  D3D_OK)
  {    
    err = CreateTexture(16, 16, 1, DXGI_FORMAT_R32_FLOAT,
      D3D11_USAGE_STAGING, _tableTcSystem, false, false);
    if (err == D3D_OK)
    {
      err = CreateTexture(TIConversionDimensionW, TIConversionDimensionH, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D11_USAGE_DEFAULT, _tiConversion, false, false);
      if (err == D3D_OK)
      {
        // In case the conversion texture name was already specified, load it
        if (!_tiConversionName.IsEmpty())
        {
          LoadTIConversionTexture(_tiConversionName);
        }
        return true;
      }
      else
      {
        RptF("Error: TIConversion texture creation failed");
      }
    }
    else
    {
      RptF("Error: Tc table system texture creation failed");
    }
  }
  else
  {
    RptF("Error: Tc table texture creation failed");
  }

  return false;
}

void EngineDD11::DestroyTIResources()
{
  _tiConversion.Free();
  _tiConversionSystem.Free();
  _tableTcSystem.Free();
  _tableTc.Free();
  _temperatureTable.Resize(0);
}

void EngineDD11::ForgetDeviceBuffers(int cb)
{
#ifdef _XBOX
  // Zero index buffer
  if (CBState(cb)._iBufferLast.NotNull())
  {
    ComRef<IDirect3DIndexBuffer9> empty;
    CALL_CB(cb,SetIndices,IndexStaticData9::BufferFuture(empty));
    CBState(cb)._iBufferLast = IndexStaticData9::BufferFuture(IndexStaticData9::BufferFuture::Null);
  }

  // Zero vertex buffer
  ComRef<IDirect3DVertexBuffer9> empty;
  SetStreamSource(cb, VertexStaticData9::BufferFuture(empty), 0, 0);
#endif
}

void EngineDD11::DoSwitchTL(int cb, TLMode active)
{
  // TODO:MC: move this outside of MC rendering, make TL always active
  FlushAndFreeAllQueues();
  CBState(cb)._tlActive = active;
  if (active)
  {
    PROFILE_DX_SCOPE_DETAIL(3deTL);
#if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("SetVertexShader T&L");
    }
#endif

#if !defined(_XBOX) && !defined(_X360SHADERGEN)
    D3DSetRenderState(cb, D3DRS_CLIPPING_OLD,TRUE);
#endif

    if (active!=TLViewspace)
    {

      // setting fog color disabled - not compatible with CB, fog needs to be scene-wide
      //// Set the fog color (change made because of _VBS3_TI)
      //SetFogColor(cb);
    }
  }
  else
  {
    // we do not expect to switch T&L off while recording, only on
    Assert(cb<0);
#if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("SetVertexShader dummy");
    }
#endif
    PROFILE_DX_SCOPE_DETAIL(3dTL);

    DoAssert(_vsNonTL.NotNull());
    SetVertexShader(cb,&_vsNonTL);
#if !defined(_XBOX) && !defined(_X360SHADERGEN)
    D3DSetRenderState(cb, D3DRS_CLIPPING_OLD,FALSE);
#else
    D3DSetRenderState(cb, D3DRS_FOGTABLEMODE,D3DFOG_NONE);
#endif

    // Zero vertex and index buffer in the device, forget _iBufferLast and _vBufferLast
    ForgetDeviceBuffers(cb);

#if 0
    // leave fog color register intact - fog color not used at all for any 2D rendering
    // 2D rendering has its own dedicated shaders
    Color fogScaled = HBlack;
    SetPixelShaderConstantF(cb,PSC_FogColor, (const float*)&fogScaled, 1);
#endif
    // Don't use any light
    SetPixelShaderConstantDBSZero(cb);
    SetPixelShaderConstant_A_DFORCED_E_Black(cb);
  }

}

void EngineDD11::InitCurrentRenderState()
{
  // constants already created in CreateShaderConstants
  //_currentRenderState._shaderConstants.Create();
  _currentRenderState.Reset();
}


void EngineDD11::InitDeviceState(int cb)
{
  Assert(!IsCBRecording());
  RendState &cbState = CBState(cb);

  cbState._bias = 0;
  cbState._fogStart = 0;
  cbState._fogEnd = 1000;
  cbState._aFogStart = 0;
  cbState._aFogEnd = 100;
  cbState._applyAplhaFog = false;
  cbState._expFogCoef = -0.00062383;

  D3DSetRenderState(cb, D3DRS_ZFUNC_OLD,D3DCMP_LESSEQUAL_OLD,false);
  D3DSetRenderState(cb, D3DRS_ZWRITEENABLE_OLD,TRUE,false);

#if !defined(_XBOX) && !defined(_X360SHADERGEN)
  D3DSetRenderState(cb, D3DRS_SHADEMODE_OLD,D3DSHADE_GOURAUD_OLD,false);
#endif

  D3DSetRenderState(cb, D3DRS_CULLMODE_OLD,D3DCULL_CCW_OLD,false);

#if !defined(_XBOX) && !defined(_X360SHADERGEN)
  D3DSetRenderState(cb, D3DRS_CLIPPING_OLD,FALSE,false);
  D3DSetRenderState(cb, D3DRS_LIGHTING_OLD,FALSE,false);
  // on 16-bit devices use dithering
  D3DSetRenderState(cb, D3DRS_DITHERENABLE_OLD,_pixelSize<=16,false);
#endif

  D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE_OLD,FALSE,false);
  D3DSetRenderState(cb, D3DRS_ALPHAFUNC_OLD,D3DCMP_GREATEREQUAL_OLD,false);
  D3DSetRenderState(cb, D3DRS_ALPHAREF_OLD,0x1,false);

#ifdef _XBOX
  D3DSetRenderState(cb, D3DRS_HIGHPRECISIONBLENDENABLE, FALSE, false);
  D3DSetRenderState(cb, D3DRS_HIGHPRECISIONBLENDENABLE1, FALSE, false);
#endif
  D3DSetRenderState(cb, D3DRS_ALPHABLENDENABLE_OLD,FALSE,false);
  D3DSetRenderState(cb, D3DRS_SRCBLEND_OLD,D3DBLEND_SRCDEF,false);
  D3DSetRenderState(cb, D3DRS_DESTBLEND_OLD,D3DBLEND_INVSRCALPHA_OLD,false);
  D3DSetRenderState(cb, D3DRS_BLENDOP_OLD,D3DBLENDOP_ADD_OLD,false);

  D3DSetRenderState(cb, D3DRS_SEPARATEALPHABLENDENABLE_OLD,FALSE,false);

  SetPixelShaderConstantF(cb,PSC_FogColor,_fogColor.GetArray(),1,false);

  // to do: we should not use bilinear filtering on devices
  // which do not support clamping


  // try to use trilinear interpolation if possible
  // always use bilinear interpolation

  for (int i = 0; i < MaxSamplerStages; i++)
  {
    D3DSetSamplerState(cb, i, D3DSAMP_MAGFILTER_OLD,      D3DTEXF_LINEAR_OLD,   false);
    D3DSetSamplerState(cb, i, D3DSAMP_MINFILTER_OLD,      D3DTEXF_LINEAR_OLD,   false);
    D3DSetSamplerState(cb, i, D3DSAMP_MIPFILTER_OLD,      D3DTEXF_LINEAR_OLD,   false);
    D3DSetSamplerState(cb, i, D3DSAMP_MAXANISOTROPY_OLD,  2,                false);
    D3DSetSamplerState(cb, i, D3DSAMP_ADDRESSU_OLD,       D3DTADDRESS_WRAP_OLD, false);
    D3DSetSamplerState(cb, i, D3DSAMP_ADDRESSV_OLD,       D3DTADDRESS_WRAP_OLD, false);
    D3DSetSamplerState(cb, i, D3DSAMP_ADDRESSW_OLD,       D3DTADDRESS_WRAP_OLD, false);
    D3DSetSamplerState(cb, i, D3DSAMP_SRGBTEXTURE_OLD,    TRUE,             false);
  }

  D3DSetRenderState(cb, D3DRS_SRGBWRITEENABLE_OLD, FALSE, false);

  SetTexture0Color(cb,HWhite,HWhite,false);

  // Set the default values
  D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE_OLD, 0x0000000F, false);
  D3DSetRenderState(cb, D3DRS_STENCILENABLE_OLD, TRUE, false);
  D3DSetRenderState(cb, D3DRS_STENCILREF_OLD, 0, false);
  D3DSetRenderState(cb, D3DRS_STENCILMASK_OLD, STENCIL_SHADOW_LEVEL_MASK, false);
  D3DSetRenderState(cb, D3DRS_STENCILWRITEMASK_OLD, STENCIL_FULL, false);
  D3DSetRenderState(cb, D3DRS_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD, false);
  D3DSetRenderState(cb, D3DRS_CCW_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD, false);


  // Hack in the nVidia driver that makes G80 equivalent to G7x what concerns fog
  //D3DSetRenderStateOpt(D3DRS_POINTSIZE, MAKEFOURCC('H', 'L', '2', 'A'), false);

#if defined _XBOX && _XBOX_VER<2
  D3DSetRenderState(cb, D3DRS_ROPZREAD,TRUE,false);
  D3DSetRenderState(cb, D3DRS_DONOTCULLUNCOMPRESSED,FALSE,false);
#endif

#if defined _XBOX
  D3DSetRenderState(cb, D3DRS_CCW_STENCILREF, 0, false);
  D3DSetRenderState(cb, D3DRS_CCW_STENCILMASK, STENCIL_SHADOW_LEVEL_MASK, false);
  D3DSetRenderState(cb, D3DRS_CCW_STENCILWRITEMASK, STENCIL_FULL, false);
#endif
  // Shadow states settings
  D3DSetRenderState(cb, D3DRS_STENCILPASS_OLD, D3DSTENCILOP_REPLACE_OLD, false);
  D3DSetRenderState(cb, D3DRS_CULLMODE_OLD, D3DCULL_CCW_OLD, false);
  DoShadowsStatesSettings(cb,SS_Disabled,false);

  D3DSetRenderState(cb, D3DRS_STENCILPASS_OLD, D3DSTENCILOP_REPLACE_OLD, false);

  const float zero = 0.0f;
  DWORD dwZero = *(const DWORD*)&zero;
  D3DSetRenderState(cb, D3DRS_DEPTHBIAS_OLD, 0);
  D3DSetRenderState(cb, D3DRS_SLOPESCALEDEPTHBIAS_OLD, dwZero);


  for (int i = 0; i < MAX_UV_SETS; i++)
  {
    cbState._texGenScale[i].u = cbState._texGenScale[i].v = 1.0f;
    cbState._texGenOffset[i].u = cbState._texGenOffset[i].v = 0.0f;
  }
  cbState._texGenScaleOrOffsetHasChanged = true;

  cbState._uvSource[0] = UVTex;
  cbState._uvSourceCount = 1;
  cbState._uvSourceOnlyTex0 = true;

  for (int i = lenof(cbState._lastHandle) - 1; i >= 0; i--) SetTexture(cb, i, NULL, false);

  cbState._lastSpec = 0;
  cbState._lastMat = TexMaterialLODInfo();
  cbState._lastAToC = false;
  cbState._lastRenderingMode = RMCommon;
  cbState._prepSpec = 0;
  cbState._mainLight = ML_Sun;
  cbState._fogMode = FM_Fog;

  cbState._world.SetIdentity();
  cbState._invWorld.SetIdentity();

  // Set tex0 max and avg colors to impossible values (this is here because we want _texture0MaxColor to be reseted if requied)
  cbState._texture0MaxColor = Color(-1,-1,-1,-1);
  cbState._texture0AvgColor = Color(-1,-1,-1,-1);

  cbState._lightDiffuse = HBlack;
  cbState._lightAmbient = HBlack;
  cbState._lightDiffuse = HBlack;
  cbState._lightSpecular = HBlack;
  cbState._direction = VZero;
  cbState._shadowDirection = VZero;
  cbState._matAmbient = HBlack;
  cbState._matDiffuse = HBlack;
  cbState._matForcedDiffuse = HBlack;
  cbState._matSpecular = HBlack;
  cbState._matEmmisive = HBlack;
  cbState._matPower = 0;
  ResetShaderConstantValues(cb);
}

template <class Type>
static Type LoadCfgDef(ParamEntryPar cfg, const char *name, Type def)
{
  return cfg.ReadValue(name,def);
}


void EngineDD11::LoadConfig(ParamEntryPar cfg,ParamEntryPar fcfg, const char *section)
{
  // we want to perform raw access - flush background thread first
  if (_backgroundD3D!=RecNone)
  {
    EndBackgroundScope(true,"ldCfg");
  }
  // first of all attempt auto-detection
  //SetDefaultSettingsLevel(GetDeviceLevel());

  bool changed = false;

#if !XBOX_D3D
  // read windows position / size (Flashpoint.cfg?)
  {
    _winX = fcfg.ReadValue("winX",16);
    _winY = fcfg.ReadValue("winY",32);
    _winW = fcfg.ReadValue("winW",Glob.config.wantW);
    _winH = fcfg.ReadValue("winH",Glob.config.wantH);
    _fullW = fcfg.ReadValue("Resolution_W",_w);
    _fullH = fcfg.ReadValue("Resolution_H",_h);
    // avoid using corrupted values
    if(_winW<64) _winW = 64;
    if(_winH<48) _winH = 48;
    _WindowAspectW = fcfg.ReadValue("winDefW",Glob.config.wantW);
    _WindowAspectH = fcfg.ReadValue("winDefH",Glob.config.wantH);
  }

  _refreshRate = LoadCfgDef(fcfg,"refresh",_refreshRate);
  if (!_windowed)
  {
    int bpp = LoadCfgDef(fcfg,"Resolution_Bpp",_pixelSize);

    if (SetResolutionWanted(_fullW,_fullH,bpp)) changed = true;
  }
#else
  int  w = fcfg.ReadValue("Resolution_W",_w);
  int  h = fcfg.ReadValue("Resolution_H",_h);
  int bpp = 32;
  _refreshRate = 60;
  if (SetResolutionWanted(w,h,bpp)) changed = true;
#endif



  // store values obtained from autodetection
  int wRTPreferred = LoadCfgDef(fcfg,"Render_W",_wRTPreferred);
  int hRTPreferred = LoadCfgDef(fcfg,"Render_H",_hRTPreferred);
#if _ENABLE_CHEATS
  // previous version did not handle zero correctly, resulting in extreme fillrate values
  // detect them and fix them
  if (wRTPreferred<_w/3) wRTPreferred = _wRTPreferred;
  if (hRTPreferred<_h/3) hRTPreferred = _hRTPreferred;
#endif
  // when we detect zeroes, use autodetected defaults instead
  // this is to maintain compatibility with previous versions, where 0 was a valid value
  if (wRTPreferred>0) _wRTPreferred = wRTPreferred;
  if (hRTPreferred>0) _hRTPreferred = hRTPreferred;

  _framesAhead = LoadCfgDef(fcfg,"GPU_MaxFramesAhead",1000);
  _framesAheadDetected = 0;

  changed |= FindFillrateControl(_wRTPreferred,_hRTPreferred);


  if (!_initialized)  
  {
    // we want to load this only when not initialized yet
    // otherwise it would overwrite the settings we have already made
    // load unique ID to check if video adapter has changed
    // if it did, we may want to reset some default settings?

    // we certainly want to recheck amount of VRAM used

    // dont do this in DX11
    //_lastDeviceId = LoadCfgDef(fcfg,"lastDeviceId",RString());
    //_caps._localVramAmount = LoadCfgDef(fcfg,"localVRAM",0);
    //_caps._nonlocalVramAmount = LoadCfgDef(fcfg,"nonlocalVRAM",0);
  }

  {
    // user config
    SetGamma(LoadCfgDef(cfg,"gamma",GetDefaultGamma()));
  }

  int fsAAWanted = LoadCfgDef(fcfg,"FSAA_DX11",_fsAAWanted);
  int afWanted = cfg.ReadValue("anisoFilter",_afWanted);
  int postFxQuality = LoadCfgDef(fcfg,"postFX",_postFxQuality);
  int hdrPrecWanted = LoadCfgDef(fcfg,"HDRPrecision",8);
  int texQuality = cfg.ReadValue("TexQuality",GetTextureQuality());
  int texMemory = cfg.ReadValue("TexMemory",texQuality);
  bool vsync = LoadCfgDef(fcfg, "vsync", true);
  _AToCMask = LoadCfgDef(fcfg, "AToC",(int)AToC_EnableAll);
  if (_AToCMask == AToC_EnableAllOld)
    _AToCMask = AToC_EnableAll; //convert old value to new one

  saturate(_AToCMask, AToC_Disable, AToC_EnableAll);

#if _ARMA3_SIMUL_WEATHER
  int swLayersCount = LoadCfgDef(fcfg, "SWLayersCount", (int)DefaultSWLayers);
  SetSWGeometryLayersCount(swLayersCount);
#endif

#if _HELISIM_RENDER_TO_TEXTURE
  bool enableR2T = LoadCfgDef(fcfg, "PiPEnabled", true);
  EnableR2T(enableR2T);
#endif

  HDRPrec hdrWanted = HDR8;
  switch (hdrPrecWanted)
  {
  case 16: hdrWanted = HDR16; break;
  case 32: hdrWanted = HDR32; break;
  }
  changed |= SetFSAAWanted(fsAAWanted);
  SetAnisotropyQuality(afWanted);
  changed |= SetHDRPrecisionWanted(hdrWanted);
  changed |= SetTextureQualityWanted(texQuality);
  changed |= SetTextureMemoryWanted(texMemory);
  changed |= SetPostprocessEffectsWanted(postFxQuality);
  changed |= SetVerticalSynchronization(vsync);

  // DX11 TODO - vsync

  if (changed && _initialized)
  {
    ApplyWanted();
  }

  base::LoadConfig(cfg,fcfg,section);
}
void EngineDD11::SaveConfig(ParamFile &cfg, const char *section)
{
  if (!IsOutOfMemory())
  {
#if !XBOX_D3D
    {
      // global config
      ParamFile fcfg;
      ParseFlashpointCfg(fcfg);

      fcfg.Add("Resolution_W",_fullW);
      fcfg.Add("Resolution_H",_fullH);
      if (!_windowed)
      {
        fcfg.Add("Resolution_Bpp",_pixelSize);
        fcfg.Add("refresh",_refreshRate);
      }
      fcfg.Add("winX", _winX);
      fcfg.Add("winY", _winY);
      fcfg.Add("winW", _winW);
      fcfg.Add("winH", _winH);

      fcfg.Add("winDefW",_WindowAspectW);
      fcfg.Add("winDefH",_WindowAspectH);
      fcfg.Add("Render_W",_wRTPreferred);
      fcfg.Add("Render_H",_hRTPreferred);
      fcfg.Add("FSAA_DX11",_fsAAWanted);
      fcfg.Add("postFX",_postFxQuality);
      fcfg.Add("GPU_MaxFramesAhead",_framesAhead);
      fcfg.Add("GPU_DetectedFramesAhead",_framesAheadDetected);
      int prec = 8;
      switch (_hdrPrecWanted)
      {
      case HDR16: prec = 16; break;
      case HDR32: prec = 32; break;
      }
      fcfg.Add("HDRPrecision",prec);

      fcfg.Add("lastDeviceId",_lastDeviceId);

      fcfg.Add("localVRAM",_caps._localVramAmount);
      fcfg.Add("nonlocalVRAM",_caps._nonlocalVramAmount);
      fcfg.Add("vsync", _vsync);
      fcfg.Add("AToC", _AToCMask);
#if _ARMA3_SIMUL_WEATHER
      fcfg.Add("SWLayersCount", _swLayersCount);
#endif
#if _HELISIM_RENDER_TO_TEXTURE
      fcfg.Add("PiPEnabled", _enabledR2T);
#endif

      SaveFlashpointCfg(fcfg);
    }

    cfg.Add("anisoFilter",_afWanted);
    cfg.Add("TexQuality", _textureQuality);
    cfg.Add("TexMemory", _textureMemory);

#endif
    {
      // user config
      cfg.Add("gamma",_gamma);
    }
    base::SaveConfig(cfg,section);
  }
}

void EngineDD11::StartBackgroundScope()
{
  const bool doBG = Glob.config._renderThread!=Config::ThreadingNone;
  Assert(_backgroundD3D!=RecHighLevel);
  if (_backgroundD3D==RecNone && doBG)
  {
#ifdef _XBOX
    Assert(_cbState[0]._rec.GetDataSize()==0);
    _d3dDevice->ReleaseThreadOwnership();
#endif
    _backgroundD3D = RecLowLevel;
    // worker thread must be finished now
    Assert(_bg._results.CountPending(ProcessResults)==0);
    // TODO: wait for worker thread finishing someplace
    // make sure we own the shadow state - see _backgroundD3D
    MemorySubscribe();
  }

  if (!doBG)
  {
    // at this point there should be no memory allocated
    _bg._resultsMem.Compact();
    Assert(_bg._resultsMem.Allocated()==0);
    Assert(_bg._resultsMem.UsedPtr()==0);
  }
}

void EngineDD11::EndBackgroundScope(bool flushMem, const char *pixScope)
{
  if (_backgroundD3D!=RecNone)
  {
    // we need to flush
    FlushBackground(_bg,flushMem,pixScope);
    _backgroundD3D = RecNone;
#ifdef _XBOX
    _d3dDevice->AcquireThreadOwnership();
#endif
    // TODO: consider calling this even for RecNone?
    BGReleaseResourcesAndTextures();
    BGReleaseRefs();
  }
  UnsetAllDiscardableResources();

  if (Glob.config._renderThread==Config::ThreadingEmulated)
  {
    BGReleaseResourcesAndTextures();
    BGReleaseRefs();
  }
}

bool EngineDD11::SetTextureQualityWanted(int value)
{
  if (value==_textureQuality) return false;
  bool needApply = value<_textureQuality; // when reducing textures, we need to "apply" (Reset the device)
  _textureQuality = value;
  if (_textBank)
  {
    _textBank->SetTextureQuality(value);
  }

  if (_textureMemory<_textureQuality)
  {
    needApply |= SetTextureMemoryWanted(value);
  }
  return needApply;
}

void EngineDD11::SetTextureQuality(int value)
{
  if (SetTextureQualityWanted(value) && !_d3dDeviceRec.IsNull())
  {
    ApplyWanted();
  }
}

int EngineDD11::GetMaxTextureQuality() const
{
  return _textBank ? _textBank->GetMaxTextureQuality() : 2;
}

bool EngineDD11::SetTextureMemoryWanted(int value)
{
  if (value==_textureMemory) return false;
  _textureMemory = value;

  if (_textBank)
  {
    _textBank->SetTextureMemory(value);
  }
  if (_textureQuality>_textureMemory)
  {
    SetTextureQualityWanted(value);
  }
  return true;
}

void EngineDD11::SetTextureMemory(int value)
{
  if (SetTextureMemoryWanted(value) && !_d3dDeviceRec.IsNull())
  {
    ApplyWanted();
  }
}

int EngineDD11::GetMaxTextureMemory() const
{
  return _textBank ? _textBank->GetMaxTextureMemory() : 2;
}

int EngineDD11::GetTextureMemoryDefault() const
{
  // check DirectDraw7 stats to tell how much memory would we recommend

  int localVRAM = GetLocalVramAmount();

  return TextBankD3D11::SelectTextureMemory(localVRAM);
}


float EngineDD11::GetHDRFactor() const
{
  float hdrFactor = 0.5;
  return hdrFactor;
}

int EngineDD11::GetDeviceLevel() const
{
  return _deviceLevel;
}

int EngineDD11::GetCurrentDeviceLevel() const
{
  // check settings to learn what quality level we correspond to?
  // some settings are higher than High - we call them Custom (or Very High?)
  int maxILevel = -1;

  if (GetTextureQuality()>1)
  {
    saturateMax(maxILevel,1);
  }

  if (HDRPrecision()>HDR8) saturateMax(maxILevel,3);

  //if (!IsWindowed())
  {
    // resolution not that important any more in ArmA 2
    // what is important is a render target resolution
    if (_wRT*_hRT>1280*1024*3/4) saturateMax(maxILevel,2);
    else if (_wRT*_hRT>1024*768*3/4) saturateMax(maxILevel,1);
  }

  if (GetFSAA()>1)
  {
    saturateMax(maxILevel,GetFSAA());
  }
  if (GetAnisotropyQuality()>0)
  {
    saturateMax(maxILevel,GetAnisotropyQuality());
  }
  if (GetPostprocessEffects()>1)
  {
    saturateMax(maxILevel,GetPostprocessEffects()-2);
  }
  return (maxILevel+1)<<8;
}

void EngineDD11::SetDefaultSettingsLevel(int level)
{
  //int _deviceLevel;
  // scan settings one by one
  // based on level select the most appropriate one, and set it (if supported)

  int iLevel = (level>>8)-1;
  saturate(iLevel,-1,3);

  // never set texture quality higher than normal by default
  // if texture memory large enough, prefer using PPLNormal

  int texQ = intMin(1,intMax(iLevel,_textureMemory));
  bool changed = SetTextureQualityWanted(texQ);

  // do not set texture memory more than one degree up from Texture Quality
  if (_textureMemory>_textureQuality+1)
  {
    changed |= SetTextureMemoryWanted(_textureQuality+1);
  }

  // use fullscreen size even for a window
  if (!IsWindowed())
  {
#ifdef _XBOX
    if (iLevel<=0) changed |= SetResolutionWanted(800,600,32);
    else changed |= SetResolutionWanted(1024,768,32);
#else
    //auto-select resolution
    RECT rect = GetMonitorResolution();
    changed |= SetResolutionWanted((rect.right - rect.left) ,(rect.bottom - rect.top),32);
#endif
  }
#ifndef _XBOX
  changed |= SetFillrateLevelWanted(iLevel);
#endif
  // with high-end prefer HDR=High if supported
  // most cards cannot do HDR16 and FSAA at the same time. We prefer FSAA
  changed |= SetHDRPrecisionWanted(HDR8);

  changed |= SetFSAAWanted(iLevel>-1 ? intMax(iLevel,1) : 0); // unless very low, we always want to get at least some FSAA

  SetAnisotropyQuality(iLevel);
  // we want postprocess High with overall High
  changed |= SetPostprocessEffectsWanted(intMin(intMax(0, iLevel+1), 4));
  // both gamma and brightness do not affect performance - do not set here
  if (changed && _initialized)
  {
    ApplyWanted();
  }
}

void EngineDD11::ApplyWanted()
{  
  DXGI_MODE_DESC oldDesc = _swapChainDesc.BufferDesc;
  FindMode();

  bool resize = (
    _swapChainDesc.BufferDesc.Width != oldDesc.Width  ||
    _swapChainDesc.BufferDesc.Height != oldDesc.Height 
    );

  //// note: complete reset may be not necessary - deleting all surfaces might do
  Reset(false, resize, false);
  CommitSettings();
}

bool EngineDD11::SetPostprocessEffectsWanted(int quality)
{
  // pfx level mask
  _postFXLevelMask = CalculatePostEffectsMask(quality);

  //ssao, must be before test for the same setting
  SetSSAOQuality(quality);

  if (_postFxQuality==quality) return false;

  _postFxQuality = quality;
  _doBloom = _postFxQuality > 1; // disable bloom when post FX disabled
  EnableDepthOfField(_postFxQuality>1 ? 1 : 0);

  return false; // resources are always created, no need to reset because of them
}

bool EngineDD11::SetHDRPrecisionWanted(HDRPrec prec)
{
  prec = ValidateHDRPrecision(prec);
  if (prec==_hdrPrecWanted) return false;

  _hdrPrecWanted=prec;
  return true;
}

void EngineDD11::SetHDRPrecision(HDRPrec prec)
{
  if (SetHDRPrecisionWanted(prec))
  {
    ApplyWanted();
  }
}

bool EngineDD11::SetFSAAWanted(int quality)
{
  if (quality>_maxFSAA) quality = _maxFSAA;
  if (quality==_fsAAWanted) return false;
  _fsAAWanted = quality;
  return true;
}

static const float FillRateRatio[][2]={{1,2},{2,3},{3,4},{4,5},{5,6},{7,8},{1,1},{8,7},{6,5},{5,4},{4,3},{3,2},{2,1}};

bool EngineDD11::FindFillrateControl(int wRTPreferred, int hRTPreferred)
{
  int bestWRT = 0;
  int bestHRT = 0;

  /** fillrate resolution definition has changed - so this should be no more required **/

  // select from a few natural ratios the one closest to the desired value
  int bestDist = INT_MAX;
  for (int i=0; i<lenof(FillRateRatio); i++)
  {
    int nom = FillRateRatio[i][0], denom = FillRateRatio[i][1];

    // select not the ratio itself, but rather the resulting size in pixels
    // first divide, multiply later - this makes sure the pixels match as frequently as possible
    int ratioW = _w/denom*nom;
    int ratioH = _h/denom*nom;
    if (ratioW>_caps._maxTextureSize || ratioH>_caps._maxTextureSize) continue;
    int dist = abs(ratioW*ratioH-wRTPreferred*hRTPreferred);
    if (bestDist>dist)
    {
      bestDist = dist;
      bestWRT = ratioW;
      bestHRT = ratioH;
    }
  }
  // rescaling is done using a bilinear filter
  // having more than 2x resolution is prone to aliasing
  // WIP: RESCALER: implement (and allow here) 4x downsampling

  saturateMin(bestWRT,_w*2);
  saturateMin(bestHRT,_h*2);

  if (_wRTRounded!=bestWRT || _hRTRounded!=bestHRT)
  {
    _wRTRounded =  bestWRT;
    _hRTRounded =  bestHRT;
    return true;
  }
  return false;
}

bool EngineDD11::SetResolutionWanted(int w, int h, int bpp)
{
  if (w==_w && h==_h && _pixelSize==bpp) return false;
  _w = w;
  _h = h;
#if !XBOX_D3D
  if (!_windowed)
  {
    _fullW = w;
    _fullH = h;
  }
#endif
  _pixelSize = bpp;
  return true;
}

bool EngineDD11::SetFillrateLevelWanted(int level)
{
  static int resolution[][2]={{800,600},{1024,700},{1280,900},{1680,1024}};
  saturate(level,0,lenof(resolution)-1);
  if (_wRTPreferred!=resolution[level][0] || _hRTPreferred!=resolution[level][1])
  {
    _wRTPreferred = resolution[level][0];
    _hRTPreferred = resolution[level][1];
    return true;
  }
  return false;
}

Engine::HDRPrec EngineDD11::ValidateHDRPrecision(HDRPrec prec) const
{
  if (prec!=HDR8)
  {
    if ((prec&_hdrPrecSupported)==0)
    {
      if (_hdrPrecSupported&HDR16) prec = HDR16;
      else if (_hdrPrecSupported&HDR32) prec = HDR32;
      else prec = HDR8;
    }
  }
  return prec;
}

float EngineDD11::Priority() const
{
  return 0.5;
}

size_t EngineDD11::MemoryControlledRecent() const
{
  return 0;
}

size_t EngineDD11::MemoryControlled() const
{
  return 0;
}

void EngineDD11::MemoryControlledFrame()
{
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  _vBufferLRU.Frame();
}

size_t EngineDD11::FreeOneItem(int minAge)
{
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  for (VertexBufferD3D11LRUItem *last = _vBufferLRU.First(); last; last = _vBufferLRU.Next(last))
  {
    int age = _vBufferLRU.FrameID()-last->GetFrameNum();
    if (age<minAge) break;
    // free vertex buffer that was not used for longest time
    VertexTable *shape = unconst_cast(last->GetOwner());
    if (!shape) continue;  // owner can be set to NULL if ReleaseVBuffer() was called already
    // if the buffer is used, we cannot discard it
    // buffer may be used and still not be the recent one
    // this is because it is made "recent" quite late, in the BeginMeshTL
    // we may still be before this call
    size_t estSize = last->GetStoredMemoryControlled();
    VertexBuffer *vb = shape->GetVertexBuffer();
    DoAssert(static_cast<VertexBufferD3D11 *>(last)==vb);
    if (minAge<=0)
    {
      // do not test lock, release everything
      shape->ReleaseVBuffer();
    }
    if (vb && minAge>0)
    {
      if (!vb->ReleaseIfNotLocked(shape)) continue;
      // vb could be deleted by ReleaseIfNotLocked, shape was holding its primary Ref
      // if deleted, last is gone as well
    }
    // if we released, but no VRAM was freed when doing so, pretend it was,
    // to avoid caller thinking there is nothing more to be freed
    return estSize>0 ? estSize : 1;
  }
  return 0;
}

size_t EngineDD11::FreeOneItem()
{
  return FreeOneItem(0);
}

size_t EngineDD11::Free(size_t size, int minAge)
{
  // free first candidate
  size_t freedTotal = 0;
  for(;;)
  {
    size_t freed = FreeOneItem(minAge);
    if (freed==0) break;
    freedTotal += freed;
    if (size<=freedTotal) break;
  }
  return freedTotal;
}

/**
@return description string, pointer into a static local buffer.
*/
const char *EngineDD11::GetMemErrorStatus() const
{
  Fail("DX11 TODO");
  return "DX11 TODO";
  // DX11 TODO
//  if (!_textBank) return "";
//  static BString<128> errorBuf;
//  int local,nonlocal;
//  CheckFreeTextureMemory(local,nonlocal);
//#if !XBOX_D3D
//  MEMORYSTATUSEX memStat;
//  memStat.dwLength = sizeof(memStat);
//  GlobalMemoryStatusEx(&memStat);
//  long long virtFree = memStat.ullAvailVirtual;
//  long long texMemDX9 = _d3dDevice->GetAvailableTextureMem();
//#else
//  MEMORYSTATUS memStat;
//  memStat.dwLength = sizeof(memStat);
//  GlobalMemoryStatus(&memStat);
//  long long virtFree = memStat.dwAvailVirtual;
//  long long texMemDX9 = 0;
//#endif
//  sprintf(
//    errorBuf,"VID: alloc %u, limit %d, free: local %d, nonlocal %d, DX9 %lld, virt. %lld",
//    _textBank->GetVRamTexAllocation()+UsedVRAM(),
//    GetVRAMAllocLimit(),local,nonlocal,texMemDX9,virtFree
//    );
//  return errorBuf;
}

void EngineDD11::CheckFreeTextureMemory(int &local, int &nonlocal) const
{
  local = nonlocal = 0;
}

void EngineDD11::AddIBuffersAllocated( int n )
{
  _iBuffersAllocated += n;
  // be pessimistic: assume index buffer are always local
  if (_caps._ibNonLocal)
  {
    _freeNonlocalVramAmount -= AlignVRAMVBSigned(n);
  }
  else
  {
    _freeLocalVramAmount -= AlignVRAMVBSigned(n);
  }
  _freeEstDirty++;
  _resetDirty++;
}

void EngineDD11::AddVBuffersAllocated( int n, bool dynamic )
{
  _vBuffersAllocated += n;
  if (dynamic ? _caps._dynamicVBNonLocal : _caps._staticVBNonLocal)
  {
    // the value is actually not used - we are tracking it only for diagnostics
    _freeNonlocalVramAmount -= AlignVRAMVBSigned(n);
  }
  else
  {
    // local VRAM value is the one we are tracking
    _freeLocalVramAmount -= AlignVRAMVBSigned(n);
  }
  // for each MB of allocated/freed we want the check to be performed a little bit sooner
  _freeEstDirty += (abs(n)>>20)+1;
  _resetDirty += (abs(n)>>20)+1;
}

void EngineDD11::RequestFlushMemoryCritical()
{
  // request a reset, until done very recently (with 30 fps at least 120 seconds)
  const int minFrames = 30*120;
  if (_resetFrame>minFrames)
  {
    _resetRequested = true;
  }
}

void EngineDD11::RequestFlushMemory()
{
  const int minFrames = 30*120;
  if (_resetFrame>minFrames && !_resetRequested)
  {
    // request a reset
    _resetRequested = GetResetFactor()>=1.0f;
  }
}

void EngineDD11::FlushMemory(bool deep, bool allowAutoDeep)
{
  if (allowAutoDeep)
  {
    if (_textBank->CheckDeepFlushNeeded())
    {
      deep = true;
    }

    float resetFactor = GetResetFactor();
    if (resetFactor>=1.0f)
    {
      deep = true;
    }
  }
  // Reset should be unnecessary on Vista, but we are not sure
  // to stay safe and avoid process virtual space fragmentation we rather reset
  if (deep /*&& !_direct3DEx*/)
  {
    Reset(false, false, false);
  }
  else
  {
    _textBank->FlushTextures(deep,allowAutoDeep);
  }
}

float EngineDD11::GetResetFactor() const
{
  // assuming 400 allocations per second, we want to run about 60 minutes between resets
  const int dirtyToReset = 400*60*60;
  // assuming 30 fps, we want to run about 4 hours between resets
  const int framesToReset = 30*4*60*60;
  return float(_resetFrame)/framesToReset + float(_resetDirty)/dirtyToReset;
}

float EngineDD11::GetLastKnownAvgIntensity() const
{
  // verify the background thread has finished and will no longer change this value
  Assert(_backgroundD3D!=RecHighLevel);
  return _lastKnownAvgIntensity;
}

void EngineDD11::DoForgetAvgIntensity()
{
  _lastKnownAvgIntensity = -1;
  // force update
  _accomFactorPrev = -1;

  // Reset old queries (_pp._glow can be null in case the reset is in progress)
  if (_pp._glow.NotNull()) _pp._glow->CancelQueries();
}
void EngineDD11::ForgetAvgIntensity()
{
  // verify the background thread has finished and will no longer change this value
  if(_backgroundD3D!=RecHighLevel)
  {
    DoForgetAvgIntensity();
  }
  else
  {
    RecordHighlevel()->ForgetAvgIntensity();
  }

}

const int SmoothFrames = 3;

float EngineDD11::GetSmoothFrameDuration() const
{
  // average for last three frames is a good basis for simulation timing
  // using last frame time can be bad, because:
  //   - D3D uses 3 frames command buffer with periodic flush
  //   - there can be some other one frame spikes
  return GetAvgFrameDuration(SmoothFrames)*0.001f;
}
float EngineDD11::GetSmoothFrameDurationMax() const
{
  // apply same smoothing as in GetSmoothFrameDuration
  DWORD maxFrame = 0;
  for (int i=0; i<NFrameDurations-SmoothFrames+1; i++)
  {
    DWORD sum = 0;
    for (int f=0; f<SmoothFrames; f++)
    {
      sum += _frameDurations[i+f];
    }
    if (maxFrame<sum) maxFrame = sum;
  }
  return maxFrame*(0.001f/3);
}

#if _ENABLE_CHEATS

static RString FormatByteSizeNoZero(size_t size)
{
  if (size) return FormatByteSize(size);
  return RString();
}

RString EngineDD11::GetStat(int statId, RString &statVal, RString &statVal2)
{
  int id = 0; // compiler will eliminate the ++ done on this into constants
  if (statId==id++)
  {
    statVal = FormatByteSizeNoZero(_iBuffersAllocated+_vBuffersAllocated);
    statVal2 = RString();
    return "Buffers";
  }
  else if (statId==id++)
  {
    statVal = FormatByteSizeNoZero(_vBufferLRU.MemoryControlled());
    statVal2 = FormatByteSizeNoZero(_vBufferLRU.MemoryControlledRecent());
    return "VBuffers";
  }
  else if (statId==id++)
  {
    statVal = Format("%d",_vBufferLRU.ItemCount());
    return "#VB";
  }
  else if (statId==id++)
  {
    statVal = FormatByteSizeNoZero(_caps._rtAllocated);
    statVal2 = RString();
    return "RT";
  }
  else if (statId==id++)
  {
    statVal = FormatByteSizeNoZero(_caps._rtAllocated+_textBank->GetVRamTexAllocation()+UsedVRAM());
    statVal2 = RString();
    return "Total";
  }
#ifndef _XBOX
  else if (UseLocalPrediction() && statId==id++)
  {
    statVal = Format("%d",_freeEstDirty);
    statVal2 = Format("%.1f",(::GlobalTickCount()-_freeEstCheckTime)*0.001f);
    return "Est recalc";
  }
  else if (statId==id++)
  {
    statVal = Format("%d",_resetDirty);
    statVal2 = Format("%d",_resetFrame);
    return "Reset alloc/frames";
  }
  else if (statId==id++)
  {
    statVal = Format("%.4f",GetResetFactor());
    statVal2 = RString();
    return "Reset factor";
  }
#endif
  else if (statId==id++)
  {
    statVal = RString();
    statVal2 = RString();
    return " ";
  }
  else if (statId==id++)
  {
    statVal = FormatByteSize(_bg._resultsMem.AllocationTurnover());
    statVal2 = FormatByteSize(_bg._resultsMem.MaxSize());
    return "CB Mem";
  }
  else if (statId==id++)
  {
    statVal = Format("%d",_bg._resultsMaxUsed);
    statVal2 = Format("%d",_bg._results.TotalSize());
    return "CB Slots";
  }
  else if (statId==id++)
  {
    statVal = Format("%d",_bg._resultsMaxUsedHistory.Min());
    statVal2 = Format("%d",_bg._resultsMaxUsedHistory.Max());
    return "CB Slots min/max";
  }
  return RString();
}
#endif

bool EngineDD11::IsWBuffer() const
{
  return false;
}

bool EngineDD11::CanWBuffer() const
{
  return false;
}

void EngineDD11::SetWBuffer(bool val)
{
}

void EngineDD11::EnableNightEye(float night)
{
  if (_nightVision) night = 0;
  // select normal or night pixel shader
  if (fabs(_nightEye-night)<0.01f) return;
  FlushQueues();
  _nightEye = night;
}

void EngineDD11::SetZPrimingDone(int cb, bool zPrimingDone)
{ // TODO:MC: sometimes needs to affect all CBs, sometimes only one
  CBState(cb)._zPrimingDone = zPrimingDone;
}

void EngineDD11::SetDebugMarker(int id)
{
  //D3DPERF_SetMarker();
  //_d3dDevice->SetDebugMarker(id);
}


int EngineDD11::ProfileBeginGraphScope(unsigned int color,const char *name) const
{
  if (IsCBRecording())
  {
    return 0;
  }
  if (IsLLRecording())
  {
    unconst_cast(this)->CB(-1).ProfileBeginGraphScope(color,name);
    return 0;
  }
  return DX11::ProfileBeginGraphScope(color,name);
}
int EngineDD11::ProfileEndGraphScope() const
{
  if (IsCBRecording())
  {
    return 0;
  }
  if (IsLLRecording())
  {
    unconst_cast(this)->CB(-1).ProfileEndGraphScope();
    return 0;
  }
  return DX11::ProfileEndGraphScope();
}

void EngineDD11::SetInstanceInfo(int cb, ColorVal color, float shadowIntensity, bool optimize)
{
  // Note: Don't try to implement lazy setting of PS constants here -
  // - potential reset would kill them

  // Remember instance parameters
  CBState(cb)._instanceColor = color;
  CBState(cb)._instanceShadowIntensity = shadowIntensity;

  // Set the VS constants with instance parameters
  float si[4]={shadowIntensity, shadowIntensity, shadowIntensity, shadowIntensity};
  SetVertexShaderConstantF(cb,VSC_InstanceColor, (float*)&color, 1, optimize);
  SetVertexShaderConstantF(cb,VSC_InstanceLandShadowIntensity, si, 1, optimize);
}

void EngineDD11::SetSkinningType(int cb, ESkinningType skinningType, const Matrix4 *bones, int nBones)
{
  RendState &cbState = CBState(cb);
  cbState._skinningType = skinningType;

  // If some bone exists, remember them
  if (bones)
  {
    // If we have reasonable number of bones then set them at once, else copy them into bone array
    if (nBones <= _caps._maxBones)
    {
      // Send bones to the GPU
      SetBones(cb, 0, bones, nBones);

      // Zero bones array, set flag
      cbState._bones.Resize(0);
      cbState._bonesWereSetDirectly = true;
    }
    else
    {
      cbState._bones.Resize(nBones);
      for (int i = 0; i < nBones; i++)
      {
        cbState._bones[i] = bones[i];
      }
      cbState._bonesWereSetDirectly = false;
    }
  }
  else
  {
    // Set bone values somehow
    cbState._bones.Resize(0);
    cbState._bonesWereSetDirectly = true;
  }
}

#if _ENABLE_SKINNEDINSTANCING

void EngineDD11::SetSkinningTypeInstance(ESkinningType skinningType, int instanceIndex, const Matrix4 *bones, int nBones)
{
  _skinningType = skinningType;
  if (bones)
  {
    _bonesInstanced.Access(instanceIndex);
    _bonesInstanced[instanceIndex].Resize(nBones);
    _bones.Resize(nBones);
    for (int i = 0; i < nBones; i++)
    {
      _bonesInstanced[instanceIndex][i] = bones[i];
    }
  }
}

#endif

float EngineDD11::GetMaxStarIntensity() const
{
  return MAX_STAR_INTENSITY;
}

bool EngineDD11::CanSeaUVMapping(int waterSegSize, float &mulUV, float &addUV) const
{ 
  // we want to transform from 0..waterSegSize to 
  mulUV = float(UVCompressedMax-UVCompressedMin)/waterSegSize;
  addUV = UVCompressedMin;
  return true;
}

void EngineDD11::SetWaterWaves(int cb, const WaterDepthQuad *water, int xRange, int zRange)
{
  RendState &cbState = CBState(cb);
  // prepare constants for water "skinning"
  cbState._skinningType = STNone; // this will reset any skinning currently set
#if _VBS3_LARGE_WATERSEG
  DoAssert(zRange<=12); // this is the maximum array size due to the number of free VS constants
#else
  const int waterSegSize = 9;
  DoAssert(xRange==zRange);
  DoAssert(xRange<=waterSegSize);
#endif
  cbState._waves.Realloc(xRange*zRange);
  COMPILETIME_COMPARE(sizeof(WaterDepthQuad),sizeof(FloatVSConstant));
  cbState._waves.Copy((const FloatVSConstant *)water,xRange*zRange);
}

void EngineDD11::SetWaterDepth(
                              float seaLevel, float shoreTop, float shoreBottom, float peakWaveTop, float peakWaveBottom,
                              int xRange, int zRange, float grid
                              )
{
  _waterPar._shoreTop = shoreTop;
  _waterPar._shoreBottom = shoreBottom;
  _waterPar._peakWaveTop = peakWaveTop;
  _waterPar._peakWaveBottom = peakWaveBottom;
  _waterPar._seaLevel = seaLevel;
  _waterPar._grid = grid;
  _waterPar._waterSegSize = xRange;


#if _VBS3_LARGE_WATERSEG
  DoAssert(zRange<=12); // this is the maximum array size due to the number of free VS constants
  _waterPar._waterArraySize = zRange;
#else
  DoAssert(xRange==zRange);
  DoAssert(xRange<=9);
#endif
}

void EngineDD11::SetUnderwaterPars(float occlusionDistance, float underWaterDepth, float waterDepthFogStart, const Color& fogColor)
{
  _underwaterPars._occlusionDistance = occlusionDistance;
  _underwaterPars._underwaterDepth = underWaterDepth;
  _underwaterPars._waterDepthFogStart = waterDepthFogStart;
  if (underWaterDepth > waterDepthFogStart )
  {
    // we are under water - fog is correctly computed
    _underwaterPars._fogColor = _fogColor * GetHDRFactor();
  }
  else
  {
    // use supploed fog color
    _underwaterPars._fogColor = fogColor * GetHDRFactor();
  }
} 

#if _ARMA3_REFRACTION
void EngineDD11::SetRefractionPars(float intensity, const Vector3& scales, const Vector3& timeCoefs)
{
  _refractionPars._intensity = intensity;
  _refractionPars._scales = scales;
  _refractionPars._timeCoefs = timeCoefs;
}
#endif

#if _ARMA3_REFRACTION_SPRITES
void EngineDD11::SetSpriteRefractionPars(float intensity, const Vector3& scales, const Vector3& timeCoefs)
{
  _spriteRefractionPars._intensity = intensity;
  _spriteRefractionPars._scales = scales;
  _spriteRefractionPars._timeCoefs = timeCoefs;
}
#endif


#if !_VBS3_LAND_SHADOW
void EngineDD11::SetLandShadow(int cb, const float *shadow, int xRange, int zRange, float grid)
{
  // prepare constants for water "skinning"
  CBState(cb)._skinningType = STNone; // this will reset any skinning currently set
  const int shadowSegSize = 12;
  DoAssert(xRange==zRange);
  DoAssert(xRange<=shadowSegSize+1);
  DoAssert(zRange<=shadowSegSize+1);
  float landParams[shadowSegSize*shadowSegSize][4];
  //memset(landParams,0,(xRange-1)*zRange*sizeof(float)*4);
  // make sure each entry contains difference to following value as well
  for (int z=0; z<zRange-1; z++) for (int x=0; x<xRange-1; x++)
  {
    int di = z*(xRange-1)+x;
    int si = z*xRange+x;
    landParams[di][0] = shadow[si];
    landParams[di][1] = shadow[si+1];
    landParams[di][2] = shadow[si+xRange];
    landParams[di][3] = shadow[si+xRange+1];
  }
  SetVertexShaderConstantF(cb,VSC_LAND_SHADOW, (float*)landParams, (xRange-1)*(zRange-1));
  // add small eps. because we want to avoid truncating below 0
  // inv. grid size is less then 1/grid, because we want to avoid overflowing above seg. size
  float gridVal[4]={0.9999f/grid,xRange-1,1,(xRange-1)*0.5f+0.0001f};
  SetVertexShaderConstantF(cb,VSC_LAND_SHADOWGRID_GRID__1__GRIDd2, (float*)gridVal, 1);
}
#endif

AbilityToDraw EngineDD11::IsAbleToDraw(bool wantReset)
{
  Assert(CheckMainThread());
  if (_d3dDeviceRec.IsNull()) return ATDUnable;
  if (!_swapChainDesc.Windowed && IsAppPaused()) return ATDUnable;

  if (_resetRequested)
  {
    LogF("*** Device reset recovery requested");
    if (Reset(false, false, false))
    {
      if (wantReset)
      {
        // report and unset immediately
        _resetDone = false;
        return ATDAbleReset;
      }
      // called is not interested about Reset, he wants to render
      // typical usage is UI screen, which does not perform any scene preloading
      return ATDAble;
    }
    else
    {
      return ATDUnable;
    }
  }

  switch (_resetStatus)
  {
  case S_OK:
    // verify the status is still OK
    if (_resetDone && wantReset)
    {
      // report reset only once
      _resetDone = false;
      return ATDAbleReset;
    }
    return ATDAble;
  case DXGI_ERROR_DEVICE_REMOVED:
    // device was removed or drivers were updated - need to recreate device
    // allowed to fall through
  case DXGI_ERROR_DEVICE_RESET:
    // device was reset due to some badly formed command - need to recreate device
    if (Reset(true, false, false))
    {
      _resetStatus = S_OK;
      if (wantReset)
      {
        _resetDone = false;
        return ATDAbleReset;
      }
      // called is not interested about Reset, he wants to render
      // typical usage is UI screen, which does not perform any scene preloading
      return ATDAble;
    }
    else
    {
      return ATDUnable;
    }
    break;
  default:
    Assert("Invalid _resetStatus");
    break;
  }

  return ATDAble;
}

bool EngineDD11::IsAbleToDrawCheckOnly()
{
#ifndef _XBOX
  return SUCCEEDED(_resetStatus) && !_resetRequested;
#else
  return true;
#endif
}

void EngineDD11::FlushBackgroundScope()
{
  Assert(_backgroundD3D!=RecHighLevel);
  if (_backgroundD3D!=RecNone)
  {
    EndBackgroundScope(true,"flush");
    BGReleaseResourcesAndTextures();
    BGReleaseRefs();
  }

}

/// verify MT safety (no two threads may enter at the same time
class NotMTSafe: private NoCopy
{
  static AtomicInt _counter;

public:
  NotMTSafe(const char *name)
  {
    if(++_counter!=1)
    {
      ErrF("MT safety broken in %s",name);
    }
  }
  ~NotMTSafe(){--_counter;}
};

AtomicInt NotMTSafe::_counter;
StoredRefContainer EngineDDBGStoredRefs11;

void EngineDD11::BGAddRefToRelease(RefCount* item)
{
  if (item)
  {
    // we must be sure nobody starts reading at this moment
    // this would switch the write buffer under our hands
    EngineDDBGStoredRefs11.Add(item);
  }
}

void EngineDD11::BGReleaseResourcesAndTextures()
{
  NotMTSafe check("BGReleaseResourcesAndTextures");
  //TrapFor0x8000Scope lock("EngineDD11::BGReleaseResourcesAndTextures");

#if SAFE_RESOURCE_UNSET
  // decrease reference count to release the tracked resources
  for (int i=0; i<_bgResToRelease.Size(); i++)
  {
    _bgResToRelease[i]->Release();
  }
  _bgResToRelease.CompactIfNeeded(2,256);
  _bgResToRelease.Resize(0);
  for (int i=0; i<_bgTexHandleToRelease.Size(); i++)
  {
    _bgTexHandleToRelease[i]->Release();
  }
  // clear the lists, but prevent their memory to be reclaimed unless it is excessive
  _bgTexHandleToRelease.CompactIfNeeded(2,256);
  _bgTexHandleToRelease.Resize(0);
#endif
}

void EngineDD11::BGReleaseRefs()
{
  AssertMainThread();

  struct ReleaseRef
  {
    bool operator () (RefCount *ref) const {ref->Release();return false;}
  };

  EngineDDBGStoredRefs11.ForEach(ReleaseRef());
}

void EngineDD11::Postprocess(float deltaT, const PostFxSettings &pfx)
{
  DoAssert(!_end3DRequested);
  _end3DRequested = true;

#ifdef _RAIN3D
  //prepare texture
  if(_pp._rain3D)
  {
    _pp._rain3D->Prepare(deltaT);
  }
#endif
  // switch recording to high level instructions (postprocess, 2D rendering, present)
  SwitchRecordingToHighLevel();

  // it would be cleaner to pass the value via some parameter of Postprocess call, but it would be more work to implement
  _accomFactorThis  = _accomFactor;
  _accomFactorFixedThis = _accomFactorFixed;
  _nightEyeThis = _nightEye;
  _nightVisionThis = GetNightVision();
  _pp._effectsThis = _pp._effects;
  _postFxEnabledThis = _postFxQuality != 0;
  _postFXLevelMaskThis = _postFXLevelMask;
  _focusDistThis = _focusDist;
  _blurCoefThis = _blurCoef;
  _farOnlyThis = _farOnly;
  _thermalVisionThis = GetThermalVision();
  if (_backgroundD3D==RecHighLevel)
  {
    // record postprocess call only
    RecordHighlevel()->Postprocess(deltaT,pfx);
    //EndHighLevelRecording();
  }
  else
  {
    // note: normally PreparePostprocess was already called some time ago to avoid waiting here
    EndBackgroundScope(true,"end3D");

    DoPostprocess(deltaT,pfx);
  }
}

void EngineDD11::NoPostprocess()
{
  DoAssert(!_end3DRequested);
  _end3DRequested = true;
  // End rendering of the 3D part of the scene

  // switch recording to high level instructions (postprocess, 2D rendering, present)
  if (_hlRecording && _backgroundD3D!=RecNone)
  {
    _backgroundD3D = RecHighLevel;
  }
  if (_backgroundD3D==RecHighLevel)
  {
    // record postprocess call only
    RecordHighlevel()->NoPostprocess();
  }
  else
  {
    // note: normally PreparePostprocess was already called some time ago to avoid waiting here
    EndBackgroundScope(true,"end3D");

    DoNoPostprocess();
  }
}

bool EngineDD11::CanPostprocessBeEnabled(int hndl)
{
#if VALIDATE_HNDL_ENABLE
  bool hndlFound = ValidatePPEHndl(_pp._effects, hndl);
  Assert(hndlFound);
#endif

  PostProcessHndl &ppHndl = AccessPostprocess(hndl);

  if (ppHndl == InvalidPPHndl) return false;

  if (ppHndl._pp)
  {
    PostProcess *tmpPP = static_cast<PostProcess*> (ppHndl._pp.GetRef());
    if (tmpPP) 
      return ((tmpPP->GetLevel() & _postFXLevelMask) != 0);
  }

  return false;
}

void EngineDD11::DoNoPostprocess()
{
  End3DRendering();
 
  // Final post process step (copying data to back buffer)
  // make sure no SRGB conversion is done during the copy
  _linearSpace = false;
  _caps._rtIsSRGB = false;
   if (_pp._final)
  {
    _pp._final->Do(true);
  }

  // SRGB write conversion could be on - turn it off
  if (_caps._canWriteSRGB)
  {
#ifndef _X360SHADERGEN
    D3DSetRenderState(-1,D3DRS_SRGBWRITEENABLE_OLD,FALSE);
#endif
  }
}

void EngineDD11::Begin3DRendering()
{
  // Under certain situations (the error message window appear at the beginning?) the Begin3DRendering might be called twice. Finish it first
  //if (_3DRenderingInProgress) End3DRendering();

  // Mark we are going to render 3D
  DoAssert(!_3DRenderingInProgress);
  _3DRenderingInProgress = true;
  //DoAssert(_end3DRequested); // TODO: make this assert working
  _end3DRequested = false;

#ifdef _XBOX
  // Start tiling - consider the first tile was cleared by the usual clear
  D3DVECTOR4 clearColor;
  clearColor.x = _clearColor.R();
  clearColor.y = _clearColor.G();
  clearColor.z = _clearColor.B();
  clearColor.w = _clearColor.A();

  D3DVECTOR4 clearColorBlack;
  clearColorBlack.x = 0;
  clearColorBlack.y = 0;
  clearColorBlack.z = 0;
  clearColorBlack.w = 1;

  if (_tilingRectsCount>1)
  {
    CALL_D3D(BeginTiling,D3DTILING_SKIP_FIRST_TILE_CLEAR, _tilingRectsCount, _tilingRects, &clearColor, 1.0f, 0L);
  }

  // Set the predicated pixel shader constants for screenspace offsets (required by SSSM maps)
  // See label GpuOwnPixelShaderConstantF
  CALL_D3D(GpuOwnPixelShaderConstantF,PSCGPU_TOffX_TOffY_X_X, 4);
  for (int i = 0; i < _tilingRectsCount; i++)
  {
    // Set predication to tile i.
    if (_tilingRectsCount>1) CALL_D3D(SetPredication, D3DPRED_TILE( i ) );

    XMVECTOR tilePos = XMVectorSet( (FLOAT)_tilingRects[i].x1, (FLOAT)_tilingRects[i].y1, 0, 0 );

    CALL_D3D(GpuPixelShaderConstantF4_1,PSCGPU_TOffX_TOffY_X_X, tilePos);

  }

  //_d3dDevice->SetPredication( D3DPRED_TILE( 0 ) );
  //_d3dDevice->ClearF(D3DCLEAR_TARGET0 | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, &_tilingRects[0], &clearColor, 1.0f, 0L);
  if (_tilingRectsCount>1) CALL_D3D(SetPredication, 0 );

#endif
}

void EngineDD11::End3DRendering()
{
#ifdef _XBOX
  // if there is no tiling, there is no buffer to record and there is no need to check it
  if (_tilingRectsCount>1)
  {
    Assert(!IsCBRecording());
#if defined(COMMAND_BUFFER_CALCULATION) || _ENABLE_REPORT
    // Get the command buffer utilization, report error if we run out of command buffer
    DWORD newUsed;
    DWORD newRemaining;
    _d3dDevice->QueryBufferSpace(&newUsed, &newRemaining);
#endif
#if defined(COMMAND_BUFFER_CALCULATION)
    if (newUsed > tilUsed)
    {
      tilUsed = newUsed;
      tilRemaining = newRemaining;
      LogF("Info: Max tiling command buffer used: %d, remaining %d", tilUsed, tilRemaining);
    }
#endif
#if _ENABLE_REPORT
    if (newRemaining <= 0) Fail("Error: Maximum command buffer size exceeded");
#endif
  }

  // Finish tiling - clear the resolved source to the desired color
  D3DVECTOR4 clearColor;
  clearColor.x = _clearColor.R();
  clearColor.y = _clearColor.G();
  clearColor.z = _clearColor.B();
  clearColor.w = _clearColor.A();

  // Resolve rendertarget into _renderTargetD
  for (int i = 0; i < _tilingRectsCount; i++)
  {
    // Set predication to tile i.
    if (_tilingRectsCount>1) _d3dDevice->SetPredication( D3DPRED_TILE( i ) );

    // Destination point is the upper left corner of the tiling rect.
    D3DPOINT* pDestPoint = (D3DPOINT*)&_tilingRects[i];

    // Resolve render target into _renderTargetD, clear the source tile to the clear values
    CALL_D3D(Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS|D3DRESOLVE_CLEARRENDERTARGET | D3DRESOLVE_CLEARDEPTHSTENCIL, &_tilingRects[i], _renderTargetD, pDestPoint, 0, 0, &clearColor, 1.0f, 0L, NULL);
  }

  // Restore predication to default.
  if (_tilingRectsCount>1) _d3dDevice->SetPredication( 0 );

  // Release ownership of the screenspace offset shader constants. See label GpuOwnPixelShaderConstantF
  CALL_D3D(GpuDisownAll,);

  // Finish tiling
  if (_tilingRectsCount>1)
  {
    _d3dDevice->EndTiling(0, NULL, NULL, NULL, 1.0f, 0L, NULL);
  }

  // From now on we want to use _renderTargetD as the texture and possibly render to _renderTargetSurfaceD which should contain the image data as well.
  // We must not use the depth buffer, or at least the tiled one, since it may share the EDRAM memory with _renderTargetD
  SetRenderTargetAndDepthStencil(_renderTargetSurfaceD, _renderTargetD, NULL);
#endif

  // Mark we finished 3D rendering
  DoAssert(_3DRenderingInProgress);
  _3DRenderingInProgress = false;

  // Queues handling moved from World::Draw (to be behind the End3DRendering call)
  FlushAndFreeAllQueues();

  if (IsFSAAEnabled())
  {
    // resolve MSAA render target to render target used for postprocess (non-MSAA)
    DoAssert(_renderTargetPP._resource);
    DoAssert(_currRenderTarget._resource == _renderTargetS._resource);
    if (_renderTargetPP._resource)
    {
      CALL_D3D(ResolveSubresource, _renderTargetPP._resource, 0, _renderTargetS._resource, 0, _rtFormat);
      SetRenderTargetAndDepthStencil(_renderTargetPP, DepthStencil::DepthStencilNULL, _minZRange, _maxZRange);
    }
  }
}

void EngineDD11::CalculateFlareIntensity(int x, int y, int sizeX, int sizeY)
{
  if (!_pp._flareIntensity || !_pp._glow) return;

  if (_backgroundD3D!=RecHighLevel)
  {
    DoCalculateFlareIntensity(x,y,sizeX,sizeY);
  }
  else
  {
    RecordHighlevel()->CalculateFlareIntensity(x,y,sizeX,sizeY);
  }
}

void EngineDD11::Pause()
{
}

void EngineDD11::Restore()
{
  // it should not be necessary to reset the state here, however it should do not harm
  // and it should be more robust in case of driver errors (drivers not maintaining the state correctly)
  Assert(!IsCBRecording());
  const int cb = -1;
  RendState &cbState = CBState(cb);
  for (int p=0; p<NPredicationModes; p++) cbState._renderState[p].Clear();
  //cbState._samplerState
}

void EngineDD11::StopAll()
{
#if 1
  // we prefer to avoid any threading during shutdown
  EndHighLevelRecording();
  FlushBackgroundScope();
  _hlRecording = false;
#endif
}

void EngineDD11::FogColorChanged(ColorVal fogColor)
{
  SetFogColor();
}

bool EngineDD11::SwitchRes(int x, int y, int w, int h, int bpp)
{
  LogF("SwitchRes: %d, %d, %d, %d, %d", x, y, w, h, bpp);
  // resolution changed


#if !XBOX_D3D
  if (_windowed)
  {
    _winX = x;
    _winY = y;
    _winW = w;
    _winH = h;
  }
#endif

  if (!SetResolutionWanted(w,h,bpp)) return true;
  DoSwitchRes(true, false);
  return true;
}

void EngineDD11::SetFogColor()
{
  // the only thread which should be setting the fog color is the main thread
  Assert(!IsInCBScope());
  if( !_d3dDeviceRec.IsNull() && !ResetNeeded())
  {
    Color fogScaled;
    Color waterFogScaled;

    if (_thermalVision)
    {
      // Fog color is derived from the air temperature in thermal case
      float fR, fG, fB;
      EncodeTemperatureToRGB1(_airTemperature, fR, fG, fB);
      fogScaled = waterFogScaled = Color(fR, fG, fB, 1.0f);
    }
    else
    {
      fogScaled = _fogColor*GetHDRFactor();
      waterFogScaled = fogScaled;
    }

    // when running PS 3 or better, fog saturate no longer needed
    fogScaled.Saturate();
    waterFogScaled.Saturate();
    if (_backgroundD3D==RecHighLevel) RecordHighlevel()->SetFogColor(fogScaled, waterFogScaled);
    else DoSetFogColor(fogScaled, waterFogScaled);
  }
}

void EngineDD11::DoSwitchRes(bool reset3DSize, bool setFullscreenState)
{
  FindMode();
  if (reset3DSize) SetDefaultFillrateControl();
  // some old drivers were buggy - ResetHard was used here as a workaround for fullscreen chain
  Reset(false, true, setFullscreenState);
}

void EngineDD11::DoSetFogColor(ColorVal fog, ColorVal waterFogColor)
{
  SetPixelShaderConstantF(-1, PSC_FogColor,fog.GetArray(),1);
  SetPixelShaderConstantF(-1, PSC_WaterFogColor,waterFogColor.GetArray(),1);
}

bool EngineDD11::SwitchWindowed(bool windowed, int w, int h, int bpp)
{
  LogF("SwitchWindowed: %d, %d, %d, %d", windowed, w, h, bpp);

#if !XBOX_D3D
  //if (windowed == _windowed)
  //{
  //  if (!windowed && w>0 && h>0 && bpp>0) return SwitchRes(_winX,_winY,w,h,bpp);
  //  return true;
  //}

  _windowed = windowed;
  _swapChainDesc.Windowed = windowed;

  FindMode();

  void SetUseWindow(bool value);
  if (windowed)
  {
    LogF("Switching to window %d,%d: %dx%d",_winX,_winY,_winW,_winH);

    AppStateChangeBeg(_hwndApp,!windowed,!_inVideoOptions,_winX,_winY,_winW,_winH);
    _w = _winW;
    _h = _winH;
    DoSwitchRes(false, true);
    /*
    _presentPars.BackBufferWidth = _w;
    _presentPars.BackBufferHeight = _h;
    Reset();
    */
    AppStateChangeEnd(_hwndApp,!windowed,!_inVideoOptions,_winX,_winY,_winW,_winH);

    RECT client;
    GetClientRect(_hwndApp,&client);
    Assert(_w==client.right-client.left);
    Assert(_h==client.bottom-client.top);

    // call when we are windowed again
    SetUseWindow(windowed);
  }
  else
  {
    if (w>0) _fullW = w;
    if (h>0) _fullH = h;
    if (bpp>0) _pixelSize = bpp;
    _w = _fullW;
    _h = _fullH;

    // call while still windowed to prevent subclassing issues
    SetUseWindow(windowed);

    //AppStateChangeBeg(_hwndApp,!windowed,!_inVideoOptions,0,0,_w,_h);
    DoSwitchRes(false, true);
    //AppStateChangeEnd(_hwndApp,!windowed,!_inVideoOptions,0,0,_w,_h);
  }
#endif
  return true;
}

bool EngineDD11::SwitchRefreshRate(int refresh)
{
  if (_swapChainDesc.Windowed) return false;
  if (refresh==0) return false;
  if (_refreshRate==refresh) return true;
  _refreshRate = refresh;
  DoSwitchRes(false, false);
  return true;
}

RString EngineDD11::GetDebugName() const
{
  // DX11 TODO
  RString ret = "D3D11";
  return ret;

//#ifdef _XBOX
//  RString ret = "Legacy";
//  return ret;
//#else
//  RString ret = "Legacy";
//
//  if (_d3dDevice.IsNull())
//  {
//    ret = ret + ": <null>";
//  }
//  else
//  {
//    D3DDEVICE_CREATION_PARAMETERS cp;
//    //PROFILE_DX_SCOPE(3dGDN);
//    _d3dDevice->GetCreationParameters(&cp);
//    D3DADAPTER_IDENTIFIER9 id;
//    _direct3D->GetAdapterIdentifier(cp.AdapterOrdinal, 0, &id);
//    char driverVersion[256];
//    sprintf
//      (
//      driverVersion,"%d.%d.%d.%d",
//      HIWORD(id.DriverVersion.HighPart),LOWORD(id.DriverVersion.HighPart),
//      HIWORD(id.DriverVersion.LowPart),LOWORD(id.DriverVersion.LowPart)
//      );
//    ret = ret + ", Device: " + RString(id.Description);
//    ret = ret + ", Driver:" + RString(id.Driver) + RString(" ") + RString(driverVersion);
//  }
//
//  return ret;
//#endif
}

void EngineDD11::KeepAspect(int &newW, int &newH)
{
#if !XBOX_D3D
  int oldW = _WindowAspectW;
  int oldH = _WindowAspectH;

  // check size of new client area
  // current client area?
  // calculate NC are size
  int w = newW;
  int h = newH;
  // find w,h so that oldW*h = oldH*w
  if (float(w)/oldW>float(h)/oldH)
  {
    // w is primary
    h = toInt(float(oldH)*w/oldW);
  }
  else
  {
    w = toInt(float(oldW)*h/oldH);
  }
  LogF
    (
    "New aspect ratio: %g, wxh: %dx%d -> %dx%d -> %dx%d",float(h)/w,
    oldW,oldH,newW,newH,w,h
    );

  newW = w;
  newH = h;
#endif
}

void EngineDD11::SetAspect(int newW, int newH)
{
#if !XBOX_D3D
  _WindowAspectW = newW;
  _WindowAspectH = newH;
#endif
}

void EngineDD11::AdjustWindowAttributes()
{
#if !XBOX_D3D
  if (_windowed)
  {
    AppStateChangeBeg(_hwndApp,!_windowed,!_inVideoOptions,_winX,_winY,_winW,_winH);
    AppStateChangeEnd(_hwndApp,!_windowed,!_inVideoOptions,_winX,_winY,_winW,_winH);
  }

  RECT client;
  GetClientRect(_hwndApp,&client);
  Assert(_w==client.right-client.left);
  Assert(_h==client.bottom-client.top);
#endif
}

void EngineDD11::BeginOptionsVideo()
{
  Assert(!_inVideoOptions);
  _inVideoOptions = true;
  //AdjustWindowAttributes();
}
void EngineDD11::EndOptionsVideo()
{
  Assert(_inVideoOptions);
  _inVideoOptions = false;
  //AdjustWindowAttributes();
}

static void Optimize(FindArray<ResolutionInfo> &ret)
{
  // first of all remove too low resolutions
  while (ret.Size()>4)
  {
    bool someDeleted = false;
    for (int s=0; s<ret.Size(); s++)
    {
      if (ret[s].w*ret[s].h<640*400)
      {
        ret.DeleteAt(s);
        someDeleted = true;
        break;
      }
    }
    if (!someDeleted) break;
  }
  // remove 16b resolutions if there are corresponding 32b ones
  while (ret.Size()>4)
  {
    bool someDeleted = false;
    for (int s=0; s<ret.Size(); s++)
    {
      if (ret[s].bpp<32)
      {
        ResolutionInfo find = ret[s];
        find.bpp = 32;
        if (ret.Find(find)>=0)
        {
          // we have the same 16 and 32, remove the 16 one
          ret.DeleteAt(s);
          someDeleted = true;
          break;
        }
      }
    }
    if (!someDeleted) break;
  }
}

#if 1 // _ENABLE_CHEATS
# define ENABLE_WINDOWED_SWITCH 1
#endif

void EngineDD11::ListResolutions(FindArray<ResolutionInfo> &ret)
{
  ret.Clear();

#if !ENABLE_WINDOWED_SWITCH
  if (_swapChainDesc.Windowed)
  {
    // No resolution change possible
    ResolutionInfo info;
    info.w = info.h = 0;
    info.bpp = 0;
    ret.AddUnique(info);
    return;
  }
#endif

#if !defined(_XBOX) && !defined(_X360SHADERGEN)
  // Enumerate resolutions available on current device
  Assert(_dxgiOutput);
  if (!_dxgiOutput)
    return;

  // Go through supported formats
  for (int formatIndex = 0; formatIndex < lenof(supportedFormats); formatIndex++) 
  {
    UINT flags = 0;
    UINT numModes = 0;
    // first get number of modes
    HRESULT hr = _dxgiOutput->GetDisplayModeList(supportedFormats[formatIndex]._format, flags, &numModes, 0);
    if (FAILED(hr))
    {
      Fail("GetDisplayModeList failed.");
      continue;
    }

    AutoArray<DXGI_MODE_DESC> modes;
    modes.Resize(numModes);
    // get available modes
    hr = _dxgiOutput->GetDisplayModeList(supportedFormats[formatIndex]._format, flags, &numModes, modes.Data());
    if (FAILED(hr))
    {
      Fail("GetDisplayModeList failed.");
      continue;
    }
      
    // Go through available modes
    for (int i = 0; i < numModes; ++i)
    {
      DXGI_MODE_DESC& mode = modes[i];

      // Mode contains resolution and refresh rate
      ResolutionInfo info;
      info.w = mode.Width;
      info.h = mode.Height;
      info.bpp = FormatToBpp(mode.Format);
      if (info.bpp) ret.AddUnique(info);
    }
  }
#endif

  // if there are too many resolutions, drop some of them
  Optimize(ret);

#if ENABLE_WINDOWED_SWITCH
  ResolutionInfo info;
  info.w = info.h = 0;
  info.bpp = 0;
  ret.AddUnique(info);
#endif
}

void EngineDD11::ListRefreshRates(FindArray<int> &ret)
{
#if !defined(_XBOX) && !defined(_X360SHADERGEN)
  if (_swapChainDesc.Windowed)
  {
    ret.Add(0); // Return default
    // No refresh change possible
    return;
  }

  Assert(_dxgiOutput);
  if (!_dxgiOutput)
    return;

  // Go through supported formats
  for (int formatIndex = 0; formatIndex < lenof(supportedFormats); formatIndex++) 
  {
    UINT flags = 0;
    UINT numModes = 0;
    // first get number of modes
    HRESULT hr = _dxgiOutput->GetDisplayModeList(supportedFormats[formatIndex]._format, flags, &numModes, 0);
    if (FAILED(hr))
    {
      Fail("GetDisplayModeList failed.");
      continue;
    }

    AutoArray<DXGI_MODE_DESC> modes;
    modes.Resize(numModes);
    // get available modes
    hr = _dxgiOutput->GetDisplayModeList(supportedFormats[formatIndex]._format, flags, &numModes, modes.Data());
    if (FAILED(hr))
    {
      Fail("GetDisplayModeList failed.");
      continue;
    }

    // Go through available modes
    for (int i = 0; i < numModes; ++i)
    {
      DXGI_MODE_DESC& mode = modes[i];

      // Mode contains resolution and refresh rate
      if
        (
        mode.Width==Width2D() && mode.Height==Height2D() &&
        FormatToBpp(mode.Format)==PixelSize()
        )
      {
        int val = ConvertRefreshRate(mode.RefreshRate);        
        ret.AddUnique(val);
      }
    }

  }
#endif
}

int EngineDD11::GetFSAA() const
{
  return _fsAAWanted;
}


void EngineDD11::SetFSAA(int level)
{
  if (SetFSAAWanted(level))
  {
    ApplyWanted();
  }
}

/// gets FSAA info.If this info cannot be get, returns false
bool EngineDD11::GetFSAAInfo(int fsaaMode, FSAAInfo &info)
{
  Assert(fsaaMode >= 0 && fsaaMode < _fsaaModes.Size());
  if (fsaaMode >= 0 && fsaaMode < _fsaaModes.Size())
  {
    info = _fsaaModes[fsaaMode];
    return true;
  }
  else
  {
    return false;
  }
}

int EngineDD11::GetMaxFSAA() const
{
  return _maxFSAA;
}

int EngineDD11::GetAnisotropyQuality() const
{
  return _afWanted;
}

int EngineDD11::GetMaxAnisotropyQuality() const
{
  return _maxAF;
}

void EngineDD11::SetPostprocessEffects(int quality)
{
  if (SetPostprocessEffectsWanted(quality))
  {
    ApplyWanted();
  }
}

int EngineDD11::GetPostprocessEffects() const
{
  return _postFxQuality;
}

void EngineDD11::SetAnisotropyQuality(int level)
{
  if (level>_maxAF) level = _maxAF;
  if (level==_afWanted) return;
  _afWanted = level;

  // we need to discard all pushbuffers, as they have the filtering information recorded
  FlushPushBuffers();
}

void EngineDD11::SetFillrateControl(ResolutionInfo info)
{
  if (SetFillrateControlWanted(info))
  {
    Reset(false, false, false);
  }
}

ResolutionInfo EngineDD11::GetFillrateControl() const
{
  ResolutionInfo info;

  info.w = _wRTRounded;
  info.h = _hRTRounded;
  info.bpp = GEngine->PixelSize(); 
  return info;
}

float EngineDD11::GetDefaultFillrateControl(float &fMin, float &fMax) const
{
  // WIP: RESCALER: make sure the RT size is supported by the device
  fMin = 0.5f;
  fMax = 2.0f;
  return 1.0f; // TODO: add detection based on device class / benchmark
  // ratios in both w and h should be the same
}

void EngineDD11::SetDefaultFillrateControl() 
{
  int level = GetDeviceLevel();
  int iLevel = (level>>8)-1;
  saturate(iLevel,-1,3);
#ifndef _XBOX
  if(SetFillrateLevelWanted(iLevel))
  {
    FindFillrateControl(_wRTPreferred,_hRTPreferred);
  }
#endif
}

void EngineDD11::ListFillrateResolutions(FindArray<ResolutionInfo> &ret)
{
  ret.Clear();

  for (int i=0; i<lenof(FillRateRatio); i++)
  {
    int nom = FillRateRatio[i][0], denom = FillRateRatio[i][1];

    ResolutionInfo info;
    info.w = _w/denom*nom;
    info.h = _h/denom*nom;
    info.bpp = GEngine->PixelSize();

    if(info.w > _caps._maxTextureSize || info.h > _caps._maxTextureSize) continue;
    if((info.w * info.h) >= (320* 200) || (i==lenof(FillRateRatio)-1))
      ret.AddUnique(info);
  }
}

static float InterpretUsrBrightness(float usrBrightness)
{
  // was: exp(usrBrightness-1);
  // we wanted to change it so that normal is what was usrBrightness = 0.7
  // exp(0.7-1) ~= 0.74
  const float scale = 0.74f;
  float expAdjust = usrBrightness>1 ? exp(usrBrightness-1) : usrBrightness;
  return expAdjust*scale;
}

// we want at least 20% to be left for HDR rendering
//const float MinHDRRange = 1/1.2f;
const float MinHDRRange = 1.4f;

// we do not want more then 40 % to be left for the HDR
//const float MaxHDRRange = 1/1.4f;
const float MaxHDRRange = 1.7f;

float EngineDD11::GetPreHDRBrightness() const
{
  // convert brightness from log to linear scale
  float expBrightness = InterpretUsrBrightness(_usrBrightness);
  float postHDR = GetPostHDRBrightness();
  return expBrightness/postHDR;
}

float EngineDD11::GetPostHDRBrightness() const
{
  // convert brightness from log to linear scale
  float expBrightness = InterpretUsrBrightness(_usrBrightness);
  float factor = GetHDRFactor();
  float minBrightness = MinHDRRange*factor;
  float maxBrightness = MaxHDRRange*factor;
  float brightness = expBrightness;
  saturate(brightness,minBrightness,maxBrightness);
  return brightness;
}

void EngineDD11::FlushQueues()
{
  if (_backgroundD3D!=RecHighLevel)
  {
    FlushAndFreeAllQueues();
  }
  else
  {
    RecordHighlevel()->FlushQueues();
  }
}

static const float ZBiasEpsilon = 0.1f;

void ConvertProjectionMatrix(D3DMATRIX &mat, Matrix4Val src, float zBias)
{
  // note: D3D notation _11 is element (0,0) in C notation
  mat._11 = src(0,0);
  mat._12 = 0;
  mat._13 = 0;
  mat._14 = 0;

  mat._21 = 0;
  mat._22 = src(1,1);
  mat._23 = 0;
  mat._24 = 0;

  /*
  // note: D3D projection calculation
  w = 1/z
  z = src(2,2)+src.Pos(2)/z
  */

  float c = src(2,2);
  float d = src.Position()[2];
  if (fabs(zBias) > ZBiasEpsilon)
  {
    if (GEngine->GetTL() && GEngine->HasWBuffer() && GEngine->IsWBuffer())
    {
      // w-buffer adjustments based on near / far values
      // calculate near and far
      float n = -d/c;
      float f = d/(1-c);
      // adjust near and far depending on z-bias
      //float mult = 1.0f-zBias*1e-6f;
      float add = 0; //zBias*1e-6f;
      float mult = 1+zBias*1e-5f;
      //float add = 0;

      f *= mult;
      n *= mult;
      n += add;

      // set adjusted coefs
      c = f/(f-n);
      d = -c*n;
    }
    else
    {
      // values used for SW T&L
      //float zMult = 1.0f-zBias*1e-7f;
      //float zAdd = zBias*-2e-7f;
      float zMult = 1.0f-zBias*1e-7f;
      float zAdd = -zBias*1e-6f;

      c = src(2,2)*zMult+zAdd;
      d = src.Position()[2]*zMult;
    }
  }

  mat._31 = 0;
  mat._32 = 0;
  mat._33 = c;
  mat._34 = 1;

  mat._41 = 0;
  mat._42 = 0;
  mat._43 = d;
  mat._44 = 0;
  // source w is 1
  // result homogenous z: x*m13 + y*m23 + z*m33 + m43
  // result homogenous w:                 z
  // m13 and m23 is zero
  // result projected  z: m33 + m43/z

#if DO_TEX_STATS
  if (LogStatesOnce)
  {
    LogF("Set z-bias %d, z1 = %10g/z0 + %10g",zBias,mat._43,mat._33);
  }
#endif

}

/*!
\patch 5137 Date 3/2/2007 by Ondra
- Fixed: Haze was too strong with large viewdistance.

\patch 5164 Date 8/15/2007 by Flyman
- Fixed: Fog on SM3 was too strong - it has got waveform similar to that one in SM2 now
*/
void EngineDD11::ProjectionChanged(int cb)
{
  // TODO:MC: while bias is local to CB, basic projection matrix should be common 
  // possible solution: each CB setting bias needs to revert back to the default settings?
  if (!CBState(cb)._tlActive!=TLDisabled || !GScene) return;

  //! If shadow buffer is being rendered, a special projection matrix was set and we don't want to rewrite it (see label SUPPRESSPROJCHANGE)
  if (_renderingMode == RMShadowBuffer) return;

#ifdef _VIEWPROJMATRIXPREMULTIPLIED
  // Remember the projection matrix
  Camera *camera = GScene->GetCamera();
  _proj = camera->ProjectionNormal();
#else
  // Set the projection matrix
  D3DMATRIX projMatrix;
  // set projection (based on camera)
  Camera *camera = GScene->GetCamera();

#if _VBS3_BIASFIX
  // Specified value was tested on 3D crater with corpse lying on it, viewing from a various heights and with various clipNear value (by turning
  // floating near plane feature on and off).
  const float biasCoef = 0.2f;
  float camZTransform = biasCoef;
#else
  // empirical, camZTransform should be around 1 for typical scenes
  float camZTransform = camera->ClipNear()*1.25f;
#endif

  float bias = CBState(cb)._bias * camZTransform;
  CBState(cb)._proj = camera->ProjectionNormal();
  ConvertProjectionMatrix(projMatrix,camera->ProjectionNormal(),bias);
  SetVertexShaderConstantF(cb,VSC_ProjMatrix, (float*)&projMatrix, 4);
#endif

  FogParams fog = _sceneProps->ComputeFogParams(camera->ClipFarFog());

  CBState(cb)._fogStart = fog.start;
  CBState(cb)._fogEnd = fog.end;
  CBState(cb)._expFogCoef = fog.expCoef;
}

void EngineDD11::InitDraw( bool clear, Color color )
{
  // If frame in progress then finish
  if( _d3dFrameOpenRequested )
  {
    LogF("InitDraw done twice");
    return;
  }

#if _ARMA3_REFRACTION
  _numBgCaptures = 0;
#endif
#if _ARMA3_DX11_PROFILING
  ProfileNextFrame();
#endif

  // to be safe, always make sure Present has completed before starting rendering a new frame
  // during normal flow FinishPresent was already called, but there are some exceptions like progress bars
  if (_textBank->FrameNotFinishedYet())
  {
    FinishPresent();
    FinishFinishPresent();
  }

  _textBank->StartFrame();

  // if nobody will set the cursor within a frame, we will hide it
  _cursorSet = false;

  const int cb = -1;
  RendState &cbState = CBState(cb);

  // check saved render targets - there should be none
  DoAssert(_savedRenderTargetInfoQueue.Size() == 0);
  _savedRenderTargetInfoQueue.Clear(); 


#if _ENABLE_REPORT
  if (_backgroundD3D==RecNone)
  {
    // verify the back-buffer information we have is correct
    ComRef<ID3D11Texture2D> bb;
    HRESULT err = _d3dDeviceRec->GetBuffer(0, __uuidof(ID3D11Texture2D),
      reinterpret_cast<void**>(bb.Init()));
    if (FAILED(err))
    {
      DDError11("*** GetBuffer failed:%x",err);
      ErrorMessage("*** GetBuffer failed:%x",err);
    }
    DoAssert(bb==_backBuffer._resource);
  }
#endif

#if 0
  if (_rtFormat==D3DFMT_X8R8G8B8)
  {
    // we may use the back buffer as a render target
    // this is not possible when render target alpha is used for shadow intensity
    // as backbuffer never contains alpha
    _currRenderTarget = NULL;
    _currRenderTargetSurface = _backBuffer;
  }
  else
#endif
  {
    // use auxiliary render target
    // sometimes it may be usable as a texture at the same time
    SetRenderTargetAndDepthStencil(_renderTargetS, _depthStencilS, _minZRange, _maxZRange);
  }

  // reset viewport z-range to default
  _minZRange = 0;
  _maxZRange = 1;

  if (_caps._canWriteSRGB)
  {
    // if using float render targets, perform SRGB writing only during final visualization
#ifdef _XBOX
    _caps._rtIsSRGB = (
      (_rtFormat&(D3DFORMAT_SIGNX_MASK | D3DFORMAT_SIGNY_MASK | D3DFORMAT_SIGNZ_MASK))==
      ((GPUSIGN_GAMMA<<D3DFORMAT_SIGNX_SHIFT)|(GPUSIGN_GAMMA<<D3DFORMAT_SIGNY_SHIFT)|(GPUSIGN_GAMMA<<D3DFORMAT_SIGNZ_SHIFT))
      )!=0;

#else
    _caps._rtIsSRGB = _rtFormat==DXGI_FORMAT_R8G8B8A8_UNORM;
#endif
    // If we use thermal vision, we don't want to use SRGB on output (because the output holds encoded temperatures and we want to read them as they are)
    if (_thermalVision)
    {
      D3DSetRenderState(cb, D3DRS_SRGBWRITEENABLE_OLD,FALSE);
    }
    else
    {
      D3DSetRenderState(cb, D3DRS_SRGBWRITEENABLE_OLD,_caps._rtIsSRGB);
    }
  }
  else
  {
    _caps._rtIsSRGB = false;
  }
  // most of the scene rendering is done in linear space
  _linearSpace = true;

  // we want all rendering to be redirected to the background thread if possible
  // one particular situation where this is important is map, without this it will not start HL recording
  StartBackgroundScope();

  // TODO: unify for FSAA as well
  //   if (GetFSAA()>0)
  //   {
  //     flags |= D3DCLEAR_ZBUFFER;
  //     if (_caps._hasStencilBuffer) flags|=D3DCLEAR_STENCIL;
  //   }
  // sometimes we want to keep the old backbuffer content
  if (!ResetNeeded())
  {
    PROFILE_DX_SCOPE(3dclr);
    if (_thermalVision)
    {
      // Fog color is derived from the air temperature in thermal case
      float fR, fG, fB;
      EncodeTemperatureToRGB1(_airTemperature, fR, fG, fB);
      color = Color(fR, fG, fB, 1.0f);

      // Set texture and sampling parameters
      SetTexture(-1,TEXID_TC, _tableTc._resView, false);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
    }
    else
    {
      // color needs to be halved - it will be double in HDR glow pass
      color = color * GetHDRFactor();
    }
#ifdef _XBOX
    // on Xbox no buffer clear seems to be neeeded
    //D3DVECTOR4 clearColor;
    //clearColor.x = color.R(),clearColor.y = color.G(),clearColor.z = color.B(),clearColor.w = color.A();
    //_d3dDevice->ClearF(flags, NULL, &clearColor, 1.0f, 0L);
#else
    if (clear)
    {
      //DoAssert(_currRenderTarget._rtView.NotNull());
      // we can not clean depth buffer ! depth buffer has info from depth priming!)
      Clear(false, true, PackedColor(color));
    }
#endif
    _clearColor = color;

#if _ENABLE_CHEATS
    SetViewport(_w, _h, _minZRange, _maxZRange);
#endif

    if (_freeEstDirty>5000)
    {
      // if there is too much dirty information, we need to perform a new estimation
      CheckLocalNonlocalTextureMemory();
    }
  }
  else
  {
    Fail("Unable to begin rendering - Reset needed");
  }

  D3DSetRenderState(cb, D3DRS_STENCILWRITEMASK_OLD, STENCIL_FULL);

  base::InitDraw();

#if LOG_LOCKS
  LogF("BeginScene");
#endif

#if DO_TEX_STATS
  LogStatesOnce = false;
  if (GInput.GetCheat1ToDo(DIK_E))
  {
    LogF("---------------------------------");
    LogF("Report texture switch totals");
    TexStats.Report();
    LogF("---------------------------------");
    TexStats.Clear();
    EnableTexStats = true;
#if 1
    LogStatesOnce = true;
    DPrimIndex = 0;
#endif
  }
  if (EnableTexStats) TexStats.Count("** BeginScene **");
  if (LogStatesOnce)
  {
    LogF("---- BeginScene");
  }
#endif
  D3DBeginScene();

  _d3dFrameOpen = true;
  _d3dFrameOpenRequested = true;

  // Invalidate the material
  TLMaterial invalidMat;
  invalidMat.diffuse = Color(-1,-1,-1,-1);
  invalidMat.ambient = Color(-1,-1,-1,-1);
  invalidMat.forcedDiffuse = Color(-1,-1,-1,-1);
  invalidMat.emmisive = Color(-1,-1,-1,-1);
  invalidMat.specFlags = 0;
  DoSetMaterial(-1,invalidMat);

  // mark material as invalid
  cbState._lastMat = TexMaterialLODInfo((TexMaterial *)-1, 0);
  cbState._lastRenderingMode = RMCommon;

  // Zero the counter
  _dipCount = 0;

  InitSceneInvariants(cb);

  // set some global rendering constants
  //   float hdrMultiply = GetPostHDRBrightness()/GetHDRFactor();
  //   SetPixelShaderConstantF(PSC_HDRMultiply, D3DXVECTOR4(hdrMultiply,hdrMultiply,hdrMultiply,hdrMultiply), 1);

  // Begin rendering of the 3D part of the scene
  Begin3DRendering();

#ifdef _XBOX
  // PIX advice: set to a vertex count of 32 and a pixel count of 96, rendering of entire scene would be 2.97% faster.
  CALL_D3D(SetShaderGPRAllocation,0,32,96);
#endif

  _tiVisualizationDone = false;
}

void EngineDD11::FinishDraw(int wantedDurationVSync, bool allBackBuffers, RString fileName)
{
  if( _d3dFrameOpen )
  {
    // End rendering of the 3D part of the scene (in case it was not finished yet f.i. by some post process)
    SwitchRecordingToHighLevel();

    //_sMesh =NULL;
    base::FinishDraw(wantedDurationVSync,allBackBuffers);

    base::DrawFinishTexts();

#if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("---- EndScene");
    }
#endif
#if TRACK_SHADER_PATCH
    if (GInput.GetCheat3ToDo(DIK_Z))
    {
      ReportShaderPatches();
    }
    ResetShaderPatches();
#endif

    // fill shadows (using stencil buffer)

#if LOG_LOCKS
    LogF("[[[ EndScene");
#endif
    if (_backgroundD3D!=RecHighLevel) DoFinishRendering();
    else RecordHighlevel()->FinishRendering();

    // _d3dFrameOpen may stay true for a while, but we know we have already requested it to be false
    _d3dFrameOpenRequested = false;


#if LOG_LOCKS
    LogF("]]] EndScene");
#endif

#ifdef _XBOX
    D3DPERF_SetMarker(0,"BegFrame");
#endif

    if (fileName != RStringB(""))
    {
      // DX11 TODO
      //D3DXSaveSurfaceToFile(fileName + RStringB(".png"), D3DXIFF_PNG, _backBuffer, NULL, NULL);
      ////       D3DXSaveSurfaceToFile("C:\\scrBack.png", D3DXIFF_PNG, _backBuffer, NULL, NULL);
      ////       D3DXSaveSurfaceToFile("C:\\scrDepth.tga", D3DXIFF_TGA, _renderTargetSurfaceDB, NULL, NULL);
    }
  }

#if _GAMES_FOR_WINDOWS
  D3DBeginScene();
  HRESULT result = XLiveRender();
  if (FAILED(result)) DIAG_MESSAGE(100, Format("XLiveRender failed with 0x%x", result));
  D3DEndScene();
  SetHWToUndefinedState();
#endif
}

bool EngineDD11::InitDrawDone()
{
  // if frame is still open, but 3D rendering was already closed (postfx done or skipped), 
  // the state is not valid for rendering and nobody should attempt to render
  DoAssert (!_d3dFrameOpenRequested || !_end3DRequested);
  //if (_d3dFrameOpenRequested) LogF("???Open");
  return _d3dFrameOpenRequested;
}

void EngineDD11::InitNoDraw()
{
  if (_textBank) _textBank->PerformAsyncMaintenance();
  base::InitNoDraw();
}
void EngineDD11::FinishNoDraw(int wantedDurationVSync)
{
  base::FinishNoDraw(wantedDurationVSync);
}

bool EngineDD11::IsMipLoadingAsync() const
{
  return MipLoadingAsync && Glob.config._useCB && GFileServerFunctions->OtherThreadsCanUse() && CheckExThreads(2,GetCPUCount()>=2);
}

void EngineDD11::FinishPresent()
{

  // once we own the state again, we can load the textures
  // do this only once per frame
  if (_textBank->FrameNotFinishedYet())
  {
    // wait until previous Present is completed
    // after this call we can be sure we own the device state
    bool part1Result = EndHighLevelRecordingPart1();
    EndHighLevelRecordingPart2(part1Result);

    // UnsetAllDiscardableResources was called by EndHighLevelRecording
    if (IsMipLoadingAsync() && Glob.config._useCB && GFileServerFunctions->OtherThreadsCanUse() && CheckExThreads(2,GetCPUCount()>=2))
    {
      _textBank->RegisterVisualizeThread(
        Glob.config._renderThread==Config::ThreadingReal ? _vis._threadId : CurrentAppFrameFunctions->GetMainThreadId()
        );
      _performMipmapLoadingWait = _vis.PerformMipmapLoading();
#if VIS_JOURNAL
      _visJournal.Add("_vis.PerformMipmapLoading");
#endif
      if (Glob.config._renderThread==Config::ThreadingEmulated) VisThreadProcess();
    }
    else
    {
      _textBank->PerformMipmapLoading();
      _textBank->FinishFrame();

      _vBufferToRelease.Resize(0);
      _vBufferToRelease.CompactIfNeeded(2,16);
    }
  }
}

void EngineDD11::FinishFinishPresent()
{
  if (_textBank->FrameNotFinishedYet())
  {
    PROFILE_SCOPE_EX(drwVi,*);
    Assert(_backgroundD3D==RecNone);
    // wait until PerformMipmapLoading is finished
    _performMipmapLoadingWait.GetResult();

#if VIS_JOURNAL
    _visJournal.Add("_performMipmapLoadingWait.GetResult");
#endif

    // some texture loading instruction might be submitted meanwhile
    // the trouble is not only texture loading instructions, but other _vis instructions as well
    // we do not want to wait for all of them
    {
      PROFILE_SCOPE_DETAIL_EX(drwVF,dd11);
      // wait one more time to be sure the _vis flush is complete
      _performMipmapLoadingWait = _vis.PerformMipmapLoading();
      if (Glob.config._renderThread==Config::ThreadingEmulated) VisThreadProcess();
      _performMipmapLoadingWait.GetResult();
    }

    // switch back to the main thread
    // TODO: avoid calling WinAPI here, remember the thread ID instead
    _textBank->RegisterVisualizeThread(GetCurrentThreadId());

    _textBank->FinishFrame();

    _vBufferToRelease.Resize(0);
    _vBufferToRelease.CompactIfNeeded(2,16);
  }
}

void EngineDD11::BackBufferToAllBuffers()
{
  /*
  ComRef<IDirect3DSurface9> back;
  ComRef<IDirect3DSurface9> front;
  _d3dDevice->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,back.Init());
  _d3dDevice->GetFrontBuffer(0,front.Init());
  _d3dDevice->CopyRects(back,NULL,0,front,NULL);
  for (int i=1; i<_presentPars.BackBufferCount; i++)
  {
  ComRef<IDirect3DSurface9> backAdd;
  _d3dDevice->GetBackBuffer(i,D3DBACKBUFFER_TYPE_MONO,backAdd.Init());
  _d3dDevice->CopyRects(back,NULL,0,backAdd,NULL);
  }
  */
}

/*!
\patch 5156 Date 5/12/2007 by Ondra
- Fixed: Position of Present call in the frame changed to improve Alt-Tab stability and work around possible driver errors.
*/
void EngineDD11::NextFrame(bool doPresent)
{
  PROFILE_SCOPE_EX(wFram,*);
  // if we are still in low-level recording, we need to leave it now
  // High Level recording is OK here
  SwitchRecordingToHighLevel();
  if (doPresent)
  {
    // experimental - several BackToFront positions in code possible
    // swap frames - get ready for next frame
    BackToFront();
  }

#ifdef _XBOX
  D3DPERF_SetMarker(0,"NextFrame");
#endif
  base::NextFrame(doPresent);
}

void EngineDD11::PrepareTriangle(int cb, const MipInfo &absMip, const TexMaterial *mat, int specFlags0)
{
  Assert(cb<0);
  TextureD3D11 *tex = static_cast<TextureD3D11 *>(absMip._texture);
  SwitchRenderMode(RMTris);
  // allocate some queue
  SwitchTL(cb,TLDisabled);
  int level = absMip._level;
  AllocateQueue(tex,level,mat,specFlags0);
  Assert (_queueNo._triUsed);
  CBState(cb)._prepSpec = specFlags0;

}

void EngineDD11::PrepareTriangleTL(
                                  int cb, const PolyProperties *section,
                                  const MipInfo &mip, const TexMaterialLODInfo &mat, int specFlags,
                                  const TLMaterial &tlMat, int spec, const EngineShapeProperties &prop
                                  )
{
  Assert(CBState(cb)._tlActive==TLEnabled);

  // Set the new vertex shader
  if (mat._mat)
  {
    // Set the material shader
    TexMaterial::Loaded m(mat._mat);

    // consider material LOD
    CBState(cb)._vertexShaderID = m.GetVertexShaderID();
    if ((CBState(cb)._vertexShaderID < 0) || (CBState(cb)._vertexShaderID >= NVertexShaderID))
    {
      Fail("Error: Vertex shader ID out of limits");
    }

    if ((m.GetMainLight() == ML_Sun) || (m.GetMainLight() == ML_None))
    {
      EnableSunLight(cb, ((spec&(DisableSun|SunPrecalculated))==0)?ML_Sun:ML_None);
    }
    else
    {
      EnableSunLight(cb, m.GetMainLight());
    }

    // Set the material fog
    SetFogMode(cb, m.GetFogMode());
  }
  else
  {
    // Set the default shader
    CBState(cb)._vertexShaderID = VSBasic;

    // Enable sunlight based on flags and the TexMaterial
    EnableSunLight(cb, ((spec&(DisableSun|SunPrecalculated))==0)?ML_Sun:ML_None);

#if _VBS3_CRATERS_DEFORM_TERRAIN
    // Decide upon flag what fog to use
    if (specFlags&IsAlphaFog)
    {
      SetFogMode(cb, FM_Alpha);
    }
    else
    {
      SetFogMode(cb, FM_Fog);
    }
#else    // Fog is usual without material
    SetFogMode(cb, FM_Fog);
#endif
  }

  TextureD3D11 *tex = static_cast<TextureD3D11 *>(mip._texture);
  int level = mip._level;


  if (_renderingMode == RMShadowBuffer)
  {
    // the only important information for shadows from the texture is the alpha channel
    int specFlagsSB = specFlags;
    if (tex)
    {
      if (tex->IsTransparent() || tex->IsAlpha())
      {
        specFlagsSB |= IsTransparent;
      }
      else
      {
        tex = NULL;
        level = 0;
      }
    }
    // no material setup needed for shadows
    DoPrepareTriangle(cb, tex,TexMaterialLODInfo(),level,specFlagsSB,prop);
  }
  else
  {
    DoPrepareTriangle(cb, tex,mat,level,specFlags,prop);
  }

  // make color information from the texture
  // to be a part of the material
  TLMaterial matMod = tlMat;
  matMod.ambient = matMod.ambient*_modColor;
  matMod.diffuse = matMod.diffuse*_modColor;
  matMod.forcedDiffuse = matMod.forcedDiffuse*_modColor;
  matMod.emmisive = matMod.emmisive*_modColor;
  matMod.specular = matMod.specular*_modColor;

  // Set the material and combine material and main light color
  DoSetMaterial(cb, matMod);
}

void EngineDD11::DrawDecal(
                          int cb, Vector3Par screen, float rhw, float sizeX, float sizeY, PackedColor color,
                          const MipInfo &mip, const TexMaterial *mat, int specFlags
                          )
{
  if (_backgroundD3D!=RecHighLevel)
  {
    DoDrawDecal(cb,screen,rhw,sizeX,sizeY,color,mip,mat,specFlags);
  }
  else
  {
    RecordHighlevel()->DrawDecal(cb,screen,rhw,sizeX,sizeY,color,mip,mat,specFlags);
  }
}

void EngineDD11::DoSwitchRenderMode(RenderMode mode)
{
#if DO_TEX_STATS
  if (LogStatesOnce)
  {
    LogF("DoSwitchRenderMode %d",mode);
  }
#endif
  FlushAndFreeAllQueues();
  _renderMode = mode;
}

WORD *EngineDD11::QueueAdd(int n )
{
  Assert (_queueNo._triUsed);
  TriQueue11 &triq = _queueNo._tri;
  if( triq._triangleQueue.Size()+n>TriQueueSize )
  {
    // flush but keep allocated
#if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("Queue full");
    }
#endif
    FlushQueue();
  }

  int index = triq._triangleQueue.Size();
  /*
  if (index==0)
  {
  // starting a new queue content
  triq._queueBase = _queueNo._meshBase;
  triq._queueSize = 0;
  }
  */
  triq._triangleQueue.Resize(index+n);
  return triq._triangleQueue.Data()+index;
}

void EngineDD11::QueueFan( const VertexIndex *ii, int n )
{
  DoAssert(!ResetNeeded());
  int addN = (n-2)*3;
  WORD *tgt = QueueAdd(addN);

  if (!tgt)
  {
    Fail("No target in QueueFan");
    return;
  }
  if (!ii)
  {
    Fail("No source in QueueFan");
    return;
  }

  int offset = _queueNo._meshBase;

  Assert( offset>=0 );
#if DIAG_QUEUE
  LogF
    (
    "Queue9 fan %d - meshBase %d, queueBase %d, offset %d",
    n,_queueNo._meshBase,triq._queueBase,offset
    );
  for (int i=0; i<n; i++)
  {
    LogF("  %d -> %d",ii[0],ii[0]+offset);
  }
#endif

  int oii0 = ii[0]+offset;
  int oiip = ii[1]+offset;

  //saturateMax(triq._queueSize,oii0+1);
  //saturateMax(triq._queueSize,oiip+1);

  // add all triangles
  /*
  if (LogStatesOnce)
  {
  LogF("  queue %d",n);
  for( int i=0; i<n; i++ )
  {
  LogF("    queue %d - %d",ii[0],ii[0]+offset);
  }
  }
  */
  // BUG: crash:
  // tgt (eax) == NULL, ii (ecx) == NULL
  // i (esi) ==2, n (edi) == 4
  // optimize calculations
  const VertexIndex *vi = ii+2;
  for( int i=2; i<n; i++ )
  {
    int oiin = (*vi++) + offset;
    tgt[0] = oii0;
    tgt[1] = oiip;
    tgt[2] = oiin;
    tgt += 3;
    //saturateMax(triq._queueSize,oiin+1);
    oiip = oiin;
  }
}

void EngineDD11::Queue2DPoly( const TLVertex *v0, int n )
{

  int addN = (n-2)*3;

  Assert (_queueNo._triUsed);
  WORD *tgt = QueueAdd(addN);

  int offset = _queueNo._meshBase;
  Assert( offset>=0 );

  // add all triangles
  for( int i=2; i<n; i++ )
  {
    *tgt++ = 0+offset;
    *tgt++ = i-1+offset;
    *tgt++ = i+offset;
  }
}

void EngineDD11::AllocateQueue(TextureD3D11 *tex, int level, const TexMaterial *mat, int spec)
{
  // scan if there is some queue with same attributes
  // note: reordering is no longer used
  int index = _queueNo.Allocate(tex,level,mat,spec);
  if (index>=0)
  {
    Assert(_queueNo._triUsed);
  }
  else
  {
    // we must free some queue
    FlushAndFreeQueue();

    index = _queueNo.Allocate(tex,level,mat,spec);

    Assert (index==0); // some queues is free now (after FlushAndFreeQueue)

    Assert(_queueNo._triUsed);
  }
}

void EngineDD11::FlushAndFreeQueue()
{
  FlushQueue();
  FreeQueue();
}

void EngineDD11::FreeQueue()
{
  // queue is already flushed
  Assert(_queueNo._tri._triangleQueue.Size()==0);
  _queueNo.Free();
}

void EngineDD11::FlushAllQueues()
{
#if DO_TEX_STATS>=2
  if (LogStatesOnce)
  {
    LogF("FlushAllQueues");
  }
#endif
  if (_queueNo._triUsed)
  {
    FlushQueue();
  }
  Assert(_queueNo._tri._triangleQueue.Size()==0);
}

void EngineDD11::FreeAllQueues()
{
#if DO_TEX_STATS>=2
  if (LogStatesOnce)
  {
    LogF("FreeAllQueues");
  }
#endif
  if (_queueNo._triUsed)
  {
    // simulate flushing - ignore rest of the queue
    _queueNo._tri._triangleQueue.Clear();
    FreeQueue();
  }
  else
  {
    Assert(_queueNo._tri._triangleQueue.Size()==0);
  }
  Assert(!_queueNo._triUsed);
  Assert(_queueNo._tri._triangleQueue.Size()==0);
}

void EngineDD11::FlushAndFreeAllQueues()
{
  // when HL recording, this can be called only from background thread, never from the main thread
  DoAssert(VerifyLowLevelThreadAccess());

#if DO_TEX_STATS>=2
  if (LogStatesOnce)
  {
    LogF("FlushAndFreeAllQueues");
  }
#endif
  if (_queueNo._triUsed)
  {
    FlushAndFreeQueue();
  }
  else
  {
    Assert(_queueNo._tri._triangleQueue.Size()==0);
  }
  Assert(!_queueNo._triUsed);
  Assert(_queueNo._tri._triangleQueue.Size()==0);
}

void EngineDD11::CloseAllQueues()
{
  FlushAndFreeAllQueues();
  _queueNo._firstVertex = true; // reset counter to avoid overflow
}

static inline float SafeInv(float x)
{
  return fabs(x)>FLT_MIN ? 1.0f / x : fSign(x)*FLT_MAX;
}


void EngineDD11::FlushQueue()
{
  DoAssert(VerifyLowLevelThreadAccess());
  //SwitchTL(false);
  TriQueue11 &triq = _queueNo._tri;
  int n = triq._triangleQueue.Size();
  if( n>0 )
  {
#if _ENABLE_PERFLOG && DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF
        (
        "Flush queue %s %x",
        triq._texture ? triq._texture->Name() : "<NULL>",triq._special
        );
    }
#endif

#if _VBS3
    // We don't know what was rendered before, therefore we need to set the pixel shader type
    SelectPixelShaderType(-1,PSTBasic);
#endif

    EngineShapeProperties prop;
    DoPrepareTriangle(-1,triq._texture, TexMaterialLODInfo(triq._material, 0), triq._level, triq._special, prop);

    // Make sure the intended pixel shader was set
    if (triq._material)
    {
      DoAssert(triq._material->GetPixelShaderID() == CBState(-1)._pixelShaderSel);
    }

    {
      RendState &cbState = CBState(-1);
      PixelShaderID pixelID = cbState._pixelShaderSel;

      static float Mult = 0.01f;
      static float Intensity = 0.7f;
      static bool ForceShadowsUI = false;

#if 0//_ENABLE_CHEATS
      static UITime ForceShadowsUITime = (UITime) 0;

      // ctrl + u
      if ((::GetAsyncKeyState(VK_LCONTROL) & 0x8000) && (::GetAsyncKeyState('U') & 0x8000))
      {
        if (Glob.uiTime > (ForceShadowsUITime + 1.0f))
        {
          ForceShadowsUI = !ForceShadowsUI;
          ForceShadowsUITime = Glob.uiTime;
        }
      }

      // left ctrl + i
      if ((::GetAsyncKeyState(VK_LCONTROL) & 0x8000) && (::GetAsyncKeyState('I') & 0x8000))
      {
        if (Glob.uiTime > (ForceShadowsUITime + 0.25f))
        {
          Intensity += 0.1f;
          saturateMin(Intensity, 1.0f);
          ForceShadowsUITime = Glob.uiTime;
        }
      }

      // right ctrl + i
      if ((::GetAsyncKeyState(VK_RCONTROL) & 0x8000) && (::GetAsyncKeyState('I') & 0x8000))
      {
        if (ForceShadowsUI && Glob.uiTime > (ForceShadowsUITime + 0.25f))
        {
          Intensity -= 0.1f;
          saturateMax(Intensity, 0.0f);
          ForceShadowsUITime = Glob.uiTime;
        }
      }

      // left ctrl + o
      if ((::GetAsyncKeyState(VK_LCONTROL) & 0x8000) && (::GetAsyncKeyState('O') & 0x8000))
      {
        if (ForceShadowsUI && Glob.uiTime > (ForceShadowsUITime + 0.25f))
        {
          Mult += 0.1f;
          ForceShadowsUITime = Glob.uiTime;
        }
      }

      // right ctrl + o
      if ((::GetAsyncKeyState(VK_RCONTROL) & 0x8000) && (::GetAsyncKeyState('O') & 0x8000))
      {
        if (Glob.uiTime > (ForceShadowsUITime + 0.25f))
        {
          Mult -= 0.1f;
          saturateMax(Mult, 0.01f);
          ForceShadowsUITime = Glob.uiTime;
        }
      }
#endif

      if (pixelID == PSNonTL)
      {
        Vector3 vec = VZero;
        if (triq._texture)
        {
          vec[0] = (triq._special & UISHADOW) ? 0.1f : 0.0f;

          if (ForceShadowsUI) vec[0] = Mult;

          vec[1] = SafeInv( triq._texture->Width(triq._level) );
          vec[2] = SafeInv( triq._texture->Height(triq._level) );
        }
        SetPixelShaderConstantUI(-1, vec, Intensity);
      }
    }

    DoSelectPixelShader(-1);

    // Zero vertex and index buffer in the device, forget _iBufferLast and _vBufferLast.
    // The locked buffers cannot be used by device anyway (at least on XBOX) and we will set the buffers anyway right after the unlock
    ForgetDeviceBuffers(-1);

    //if( triq._queueType!=QueuePoints )
    {
#if DIAG_QUEUE
      LogF("Flushing tri queue: base %d, size %d",_queue._queueBase,_queue._queueSize);
#endif
      // set index stream / vertex buffer

      HRESULT ret;

      // create index buffer
      int size = n*sizeof(VertexIndex);


      int indexOffset = 0;
      void *data;
      // DX11 TODO
      //DWORD flags = NoSysLockFlag;
      D3D11_MAP flags;
      IndexStaticData11::BufferFuture ibuf(IndexStaticData11::BufferFuture::Null);
      if (n+_queueNo._indexBufferUsed<=IndexBufferLength && !_queueNo._firstIndex)
      {
#if LOG_LOCKS
        LogF("Index add D3DLOCK_NOOVERWRITE %d + %d",_queueNo._indexBufferUsed,n);
#endif
        // append data
        indexOffset = _queueNo._indexBufferUsed;
        flags = D3D11_MAP_WRITE_NO_OVERWRITE;

        ibuf = _queueNo.GetIBuffer();
        ret = LockDynBuffer(ibuf.GetResult(), data, flags, indexOffset, size); // Futures in _queueNo are always ready
      }
      else
      {
#if LOG_LOCKS
        LogF("Index add D3DLOCK_DISCARD %d + %d",0,n);
#endif
        _queueNo._firstIndex = false;
        // reset index buffer
        indexOffset = 0;
        // DX11 TODO not sure if this is right
        flags = D3D11_MAP_WRITE_DISCARD;
#if CUSTOM_VB_DISCARD
        if (++_queueNo._actualIBIndex>=NumDynamicVB2D)
        {
          _queueNo._actualIBIndex = 0;
        }
#else
        flags |= D3DLOCK_DISCARD;
#endif
        PROFILE_SCOPE_DETAIL_EX(lbDsc,dd11);
        ibuf = _queueNo.GetIBuffer();
        ret = LockDynBuffer(ibuf.GetResult(), data, flags, indexOffset, size);  // Futures in _queueNo are always ready
      }


      _queueNo._indexBufferUsed = indexOffset+n;
      if (ret==D3D_OK)
      {
        // DX11 - data are always mapped from offset 0, so we have to add indexOffset
        //memcpy((unsigned char*)data + indexOffset,triq._triangleQueue.Data(),size);             
        memcpy(&((VertexIndex*)data)[indexOffset],triq._triangleQueue.Data(),size);             
        Unmap(ibuf.GetResult(), 0);  // Futures in _queueNo are always ready
      }
      else
      {
        DDError11("iBuffer->Lock",ret);
      }

      // Fill out vertex buffer
      const VertexStaticData11::BufferFuture &vbuf = _queueNo.GetVBuffer();
      SetStreamSource(-1,vbuf, sizeof(TLVertex), 0);
      //_vertexDecl = NULL;

      // Fill out index buffer
      if (!CBState(-1)._iBufferLast.IsSameFuture(ibuf))
      {
        CBState(-1)._iBufferLast = ibuf;
        CBState(-1)._vOffsetLast = 0;
        ADD_COUNTER(d3xIB,1);
        //PROFILE_DX_SCOPE(3dIND);
        _d3dDeviceRec->IASetIndexBuffer(ibuf, DXGI_FORMAT_R16_UINT, 0);
      }

      PROFILE_DX_SCOPE_DETAIL(3dDI2);
      DoAssert(_d3dFrameOpen || _renderingMode==RMDPrime || _renderingMode==RMZPrime || _backgroundD3D==RecHighLevel);
#ifdef _XBOX
      DoAssert(!_3DRenderingInProgress);
#endif
      DrawIndexedPrimitive(
        -1, 
        D3DPT_TRIANGLELIST_OLD,
        0,
        0,
        //triq._queueSize,
        _queueNo._vertexBufferUsed,
        indexOffset,n/3
        );
#if _ENABLE_PERFLOG && DO_TEX_STATS
      if (LogStatesOnce)
      {
        LogF("  %d: Draw .... %d tris",DPrimIndex++,triq._triangleQueue.Size()/3);
      }
#endif
      ADD_COUNTER(tris,n/3);
      ADD_COUNTER(dPrim,1);
      //triq._queueSize = 0;
    }
    triq._triangleQueue.Clear();
  }
  Assert(!triq._triangleQueue.Size());
  DoAssert(VerifyLowLevelThreadAccess());
}

void EngineDD11::SetPrimitiveTopology(int cb, D3D11_PRIMITIVE_TOPOLOGY topology, bool optimize)
{
  RendStateExt& cbState = CBState(cb);
  if (!optimize || cbState._lastPrimitiveTopology != topology)
  {
    CALL_CB_ENGINE(cb, SetPrimitiveTopology, topology);
    cbState._lastPrimitiveTopology = topology;
  }  
}

void EngineDD11::DoSetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY topology)
{
  AssertSpecialLevelThreadAccess();
  if (topology != _currentRenderState._lastTopology)
  {
    _d3dDeviceRec->IASetPrimitiveTopology(topology);
    _currentRenderState._lastTopology = topology;
  }
}

void EngineDD11::DrawIndexedPrimitive(int cb, D3DPRIMITIVETYPE_OLD primitiveType, INT baseVertexIndex, UINT minIndex, UINT numVertices, UINT startIndex, UINT primitiveCount)
{
#if _ENABLE_CHEATS
  for (int i=0; i < DuplicateDrawCalls; ++i)
#endif
  {
    CALL_CB_ENGINE(cb, DrawIndexedPrimitive, primitiveType, baseVertexIndex, minIndex, numVertices, startIndex, primitiveCount);
  }
}

void EngineDD11::DoDrawIndexedPrimitive(D3DPRIMITIVETYPE_OLD primitiveType, INT baseVertexIndex, UINT minIndex, UINT numVertices, UINT startIndex, UINT primitiveCount)
{
  AssertSpecialLevelThreadAccess();
  DoSetupAllBeforeDraw();

  // Set the primitive topology
  switch (primitiveType)
  {
  case D3DPT_TRIANGLELIST_OLD:
    DoSetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    numVertices = primitiveCount * 3;
    break;
  case D3DPT_TRIANGLESTRIP_OLD:
    DoSetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    numVertices = primitiveCount + 2;
    break;
  default:
    DoAssert("Error: Not handled")
      Fail("Error: Not handled");
    return;
  }

  {
#if _ENABLE_REPORT
    D3DInfoQueueGuard guard(_d3dInfoQueue);
#endif
    // Draw the geometry
    _d3dDeviceRec->DrawIndexed(numVertices, startIndex, baseVertexIndex);
  }
}

void EngineDD11::DrawPrimitive(int cb, 
                                 D3DPRIMITIVETYPE_OLD primitiveType,
                                 UINT startVertex,
                                 UINT primitiveCount)
{
  CALL_CB_ENGINE(cb, DrawPrimitive, primitiveType, startVertex, primitiveCount);
}

void EngineDD11::DoDrawPrimitive(D3DPRIMITIVETYPE_OLD primitiveType,
                               UINT startVertex,
                               UINT primitiveCount
)
{
  PROFILE_SCOPE_EX(drwPrm, dd11)

  AssertSpecialLevelThreadAccess();
  DoSetupAllBeforeDraw();

  int numVertices = 0;

  // Set the primitive topology
  switch (primitiveType)
  {
  case D3DPT_TRIANGLELIST_OLD:
    DoSetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    numVertices = primitiveCount * 3;
    break;
  case D3DPT_TRIANGLESTRIP_OLD:
    DoSetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    numVertices = primitiveCount + 2;
    break;
  default:
    DoAssert("Error: Not handled")
    Fail("Error: Not handled");
    return;
  }

  {
#if _ENABLE_REPORT
    D3DInfoQueueGuard guard(_d3dInfoQueue);
#endif
    _d3dDeviceRec->Draw(numVertices, startVertex);
  }

}


void EngineDD11::DoSetupAllBeforeDraw()
{
#if _ARMA3_DX11_PROFILING
  _profileInfoCur._profileNumSetups++;
#endif

  PROFILE_SCOPE_EX(stpAll, dd11)

  AssertSpecialLevelThreadAccess();
  SetupDepthStencilStates();
  SetupBlendStates();
  SetupRasterizerStates();
  SetupSamplerStates();
  SetupAlphaTest();
  SetupConstantBuffers();
  SetupTextures();
}

#if DX11_USE_HASHED_STATE_OBJECTS
// Get, possibly create the hash table item
#define GET_HASHTABLE_ITEM(statesType,states,keyData, createStateFnc,comObj) \
  Ref<statesType::TableItem> item; \
{ \
  const Ref<statesType::TableItem> &hashItem = states.Get(keyData); \
  if (states.IsNull(hashItem)) \
{ \
  ComRef<comObj> newState; \
  HRESULT hr = _d3dDevice->createStateFnc(&keyData, newState.Init()); \
  if (FAILED(hr)) \
  {\
    DDError11(#createStateFnc " failed:",hr);  \
  }\
  item = new statesType::TableItem(keyData, newState); \
  states.Add(item); \
} \
  else \
{ \
  item = hashItem; \
} \
}
#endif //DX11_USE_HASHED_STATE_OBJECTS

void EngineDD11::SetupAlphaTest()
{
  float value[4];
  if (GetRSValue(D3DRS_ALPHATESTENABLE_OLD) == TRUE)
  {
    value[0] = 1.0f;
    value[1] = value[2] = value[3] = GetRSValue(D3DRS_ALPHAREF_OLD) / 255.0f;
  }
  else
  {
    value[0] = value[1] = value[2] = value[3] = 0.0f;
  }

  DoSetShaderConstantF(&PSC_AlphaTest, 4, 0, Array<float>(value, 4));
}

void EngineDD11::SetupDepthStencilStates()
{
  PROFILE_SCOPE_DETAIL_EX(stpDSS, dd11)

#if DX11_USE_HASHED_STATE_OBJECTS
  D3D11_DEPTH_STENCIL_DESC dsd;

  // DepthEnable
  if (GetRSValue(D3DRS_ZENABLE_OLD))
  {
    dsd.DepthEnable = TRUE;
  }
  else
  {
    dsd.DepthEnable = FALSE;
  }

  // DepthWriteMask
  if (GetRSValue(D3DRS_ZWRITEENABLE_OLD) == TRUE)
  {
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  }
  else
  {
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  }

  // DepthFunc
  switch (GetRSValue(D3DRS_ZFUNC_OLD))
  {
  case D3DCMP_EQUAL_OLD:
    dsd.DepthFunc = D3D11_COMPARISON_EQUAL;
    break;
  case D3DCMP_LESSEQUAL_OLD:
    dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    break;
  case D3DCMP_GREATEREQUAL_OLD:
    dsd.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    break;
  case D3DCMP_ALWAYS_OLD:
    dsd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    break;
  default:
    dsd.DepthFunc = (D3D11_COMPARISON_FUNC)0;
    Fail("DX11 Error: Not handled");
  }

  // StencilEnable
  dsd.StencilEnable = GetRSValue(D3DRS_STENCILENABLE_OLD);
  // StencilReadMask
  dsd.StencilReadMask = GetRSValue(D3DRS_STENCILMASK_OLD);
  // StencilWriteMask
  dsd.StencilWriteMask = GetRSValue(D3DRS_STENCILWRITEMASK_OLD);

  dsd.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
  dsd.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;

  // FrontFace.StencilPassOp
  switch (GetRSValue(D3DRS_STENCILPASS_OLD))
  {
  case D3DSTENCILOP_KEEP_OLD:
    dsd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    break;
  case D3DSTENCILOP_ZERO_OLD:
    dsd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_ZERO;
    break;
  case D3DSTENCILOP_REPLACE_OLD:
    dsd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
    break;
  case D3DSTENCILOP_INCR_OLD:
    dsd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_INCR;
    break;
  case D3DSTENCILOP_DECR_OLD:
    dsd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_DECR;
    break;
  default:
    dsd.FrontFace.StencilPassOp = (D3D11_STENCIL_OP)0;
    Fail("DX11 Error: Not handled");
  }

  // FrontFace.StencilFunc
  switch (GetRSValue(D3DRS_STENCILFUNC_OLD))
  {
  case D3DCMP_NEVER_OLD:
    dsd.FrontFace.StencilFunc = D3D11_COMPARISON_NEVER;
    break;
  case D3DCMP_LESS_OLD:
    dsd.FrontFace.StencilFunc = D3D11_COMPARISON_LESS;
    break;
  case D3DCMP_EQUAL_OLD:
    dsd.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
    break;
  case D3DCMP_LESSEQUAL_OLD:
    dsd.FrontFace.StencilFunc = D3D11_COMPARISON_LESS_EQUAL;
    break;
  case D3DCMP_GREATEREQUAL_OLD:
    dsd.FrontFace.StencilFunc = D3D11_COMPARISON_GREATER_EQUAL;
    break;
  case D3DCMP_ALWAYS_OLD:
    dsd.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    break;
  default:
    dsd.FrontFace.StencilFunc = (D3D11_COMPARISON_FUNC)0;
    Fail("DX11 Error: Not handled");
  }

  dsd.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
  dsd.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;

  if (GetRSValue(D3DRS_TWOSIDEDSTENCILMODE_OLD))
  {
    // BackFace.StencilPassOp
    switch (GetRSValue(D3DRS_CCW_STENCILPASS_OLD))
    {
    case D3DSTENCILOP_KEEP_OLD:
      dsd.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
      break;
    case D3DSTENCILOP_ZERO_OLD:
      dsd.BackFace.StencilPassOp = D3D11_STENCIL_OP_ZERO;
      break;
    case D3DSTENCILOP_REPLACE_OLD:
      dsd.BackFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
      break;
    case D3DSTENCILOP_INCR_OLD:
      dsd.BackFace.StencilPassOp = D3D11_STENCIL_OP_INCR;
      break;
    case D3DSTENCILOP_DECR_OLD:
      dsd.BackFace.StencilPassOp = D3D11_STENCIL_OP_DECR;
      break;
    default:
      dsd.BackFace.StencilPassOp = (D3D11_STENCIL_OP)0;
      Fail("DX11 Error: Not handled");
    }

    // BackFace.StencilFunc
    switch (GetRSValue(D3DRS_CCW_STENCILFUNC_OLD))
    {
    case D3DCMP_NEVER_OLD:
      dsd.BackFace.StencilFunc = D3D11_COMPARISON_NEVER;
      break;
    case D3DCMP_LESS_OLD:
      dsd.BackFace.StencilFunc = D3D11_COMPARISON_LESS;
      break;
    case D3DCMP_EQUAL_OLD:
      dsd.BackFace.StencilFunc = D3D11_COMPARISON_EQUAL;
      break;
    case D3DCMP_LESSEQUAL_OLD:
      dsd.BackFace.StencilFunc = D3D11_COMPARISON_LESS_EQUAL;
      break;
    case D3DCMP_GREATEREQUAL_OLD:
      dsd.BackFace.StencilFunc = D3D11_COMPARISON_GREATER_EQUAL;
      break;
    case D3DCMP_ALWAYS_OLD:
      dsd.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
      break;
    default:
      dsd.BackFace.StencilFunc = (D3D11_COMPARISON_FUNC)0;
      Fail("DX11 Error: Not handled");
    }
  }
  else
  {
    dsd.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    dsd.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
  }

  dsd.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
  dsd.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;

  // Get the item from the pool (or create it) and return it
  GET_HASHTABLE_ITEM(TDepthStencilStates, _depthStencilStates, dsd, CreateDepthStencilState, ID3D11DepthStencilState)
  // Set the render state
  if (item && item->_object)
  {
    _d3dDeviceRec->OMSetDepthStencilState(item->_object, GetRSValue(D3DRS_STENCILREF_OLD) );
  }
#else //#if DX11_USE_HASHED_STATE_OBJECTS
  ID3D11DepthStencilState *state = (ID3D11DepthStencilState *)_depthStencilStateList.GetState(
    _currentRenderState._renderState);
  if (state && state != _currentRenderState._depthStencilStateLast)
  {
    _d3dDeviceRec->OMSetDepthStencilState((ID3D11DepthStencilState *)state, GetRSValue(D3DRS_STENCILREF_OLD) );
    _currentRenderState._depthStencilStateLast = state;
  }
#endif //#if DX11_USE_HASHED_STATE_OBJECTS else
}

void EngineDD11::SetupBlendStates()
{
  PROFILE_SCOPE_DETAIL_EX(stpBS, dd11)

#if DX11_USE_HASHED_STATE_OBJECTS
  D3D11_BLEND_DESC bd;

  // DX11 TODO - ATOC
  bd.AlphaToCoverageEnable = FALSE;
  bd.IndependentBlendEnable = FALSE;

  // BlendEnable
  if (GetRSValue(D3DRS_ALPHABLENDENABLE_OLD) == TRUE)
  {
    bd.RenderTarget[0].BlendEnable = TRUE;
  }
  else
  {
    bd.RenderTarget[0].BlendEnable = FALSE;
  }

  // SrcBlend
  switch (GetRSValue(D3DRS_SRCBLEND_OLD))
  {
  case D3DBLEND_ZERO_OLD:
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
    break;
  case D3DBLEND_ONE_OLD:
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    break;
  case D3DBLEND_SRCALPHA_OLD:
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    break;
  case D3DBLEND_INVSRCALPHA_OLD:
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_INV_SRC_ALPHA;
    break;
  default:
    bd.RenderTarget[0].SrcBlend =  (D3D11_BLEND)0;
    Fail("DX11 Error: Not handled");
  }

  // DestBlend
  switch (GetRSValue(D3DRS_DESTBLEND_OLD))
  {
  case D3DBLEND_ZERO_OLD:
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    break;
  case D3DBLEND_ONE_OLD:
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    break;
  case D3DBLEND_SRCALPHA_OLD:
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_ALPHA;
    break;
  case D3DBLEND_INVSRCALPHA_OLD:
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    break;
  default:
    bd.RenderTarget[0].DestBlend = (D3D11_BLEND)0;
    Fail("DX11 Error: Not handled");
  }

  // BlendOp
  switch (GetRSValue(D3DRS_BLENDOP_OLD))
  {
  case D3DBLENDOP_ADD_OLD:
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    break;
  default:
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    Fail("DX11 Error: Not handled");
  }

  // DX11 TODO - alpha blend values?
  bd.RenderTarget[0].SrcBlendAlpha = bd.RenderTarget[0].SrcBlend;
  bd.RenderTarget[0].DestBlendAlpha = bd.RenderTarget[0].DestBlend;
  bd.RenderTarget[0].BlendOpAlpha = bd.RenderTarget[0].BlendOp;

  // write mask
  DWORD writeMask = GetRSValue(D3DRS_COLORWRITEENABLE_OLD);
  UINT8 newWriteMask = 0;
  if ((writeMask & D3DCOLORWRITEENABLE_RED_OLD) != 0)
    newWriteMask |= D3D11_COLOR_WRITE_ENABLE_RED;
  if ((writeMask & D3DCOLORWRITEENABLE_GREEN_OLD) != 0)
    newWriteMask |= D3D11_COLOR_WRITE_ENABLE_GREEN;
  if ((writeMask & D3DCOLORWRITEENABLE_BLUE_OLD) != 0)
    newWriteMask |= D3D11_COLOR_WRITE_ENABLE_BLUE;
  if ((writeMask & D3DCOLORWRITEENABLE_ALPHA_OLD) != 0)
    newWriteMask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;
  bd.RenderTarget[0].RenderTargetWriteMask = newWriteMask;

  // DX11 TODO - alpha test
  // LUKE HACK - temporary solution
  //if (GetRSValue(D3DRS_ALPHATESTENABLE_OLD))
  //{
  //  bd.RenderTarget[0].BlendEnable = TRUE;
  //  bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  //  bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  //  bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  //}

  for (int i = 1; i < 8; i++) 
  {
    bd.RenderTarget[i].BlendEnable = FALSE;
    bd.RenderTarget[i].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[i].DestBlend = D3D11_BLEND_ZERO;
    bd.RenderTarget[i].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[i].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[i].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[i].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  }

  GET_HASHTABLE_ITEM(TBlendStates, _blendStates, bd, CreateBlendState, ID3D11BlendState)

  float blendFactor[4] = {0,0,0,0};
  if (item && item->_object)
  {
    _d3dDeviceRec->OMSetBlendState(item->_object, Array<float>(blendFactor, 4), 0xffffffff );
  }

#else //#if DX11_USE_HASHED_STATE_OBJECTS
  float blendFactor1[4] = {0,0,0,0};
  ID3D11BlendState* blendState = (ID3D11BlendState* )_blendStateList.GetState(
    _currentRenderState._renderState);
  if (blendState && blendState != _currentRenderState._blendStateLast)
  {
    _d3dDeviceRec->OMSetBlendState(blendState, Array<float>(blendFactor1, 4), 0xffffffff );
    _currentRenderState._blendStateLast = blendState;
  }
#endif //#if DX11_USE_HASHED_STATE_OBJECTS else
}

void EngineDD11::SetupRasterizerStates()
{
  PROFILE_SCOPE_DETAIL_EX(stpRS, dd11)

#if DX11_USE_HASHED_STATE_OBJECTS
  D3D11_RASTERIZER_DESC rd;

  // FillMode
  switch (GetRSValue(D3DRS_FILLMODE_OLD))
  {
  case D3DFILL_WIREFRAME_OLD:
    rd.FillMode = D3D11_FILL_WIREFRAME;
    break;
  case D3DFILL_SOLID_OLD:
    rd.FillMode = D3D11_FILL_SOLID;
    break;
  default:
    rd.FillMode = D3D11_FILL_SOLID;
    Fail("DX11 Error: Not handled");
  }

  // CullMode
  switch (GetRSValue(D3DRS_CULLMODE_OLD))
  {
  case D3DCULL_NONE_OLD:
    rd.CullMode = D3D11_CULL_NONE;
    rd.FrontCounterClockwise = TRUE;
    break;
  case D3DCULL_CW_OLD:
    rd.FrontCounterClockwise = TRUE;
    rd.CullMode = D3D11_CULL_BACK;
    break;
  case D3DCULL_CCW_OLD:
    rd.FrontCounterClockwise = FALSE;
    rd.CullMode = D3D11_CULL_BACK;
    break;
  default:
    rd.CullMode = (D3D11_CULL_MODE)0;
    Fail("DX11 Error: Not handled");
  }

  // DX11 TODO DEPTH BIAS
  rd.DepthBias = GetRSValue(D3DRS_DEPTHBIAS_OLD);
  rd.DepthBiasClamp = 0;
  DWORD slopeScaledDepthBias = GetRSValue(D3DRS_SLOPESCALEDEPTHBIAS_OLD);
  rd.SlopeScaledDepthBias = *(const float*)&slopeScaledDepthBias;

  rd.DepthClipEnable = TRUE;
  rd.ScissorEnable = FALSE;
  rd.MultisampleEnable = TRUE;
  rd.AntialiasedLineEnable = FALSE;

  // Get the item from the pool (or create it) and return it
  GET_HASHTABLE_ITEM(TRasterizerStates, _rasterizerStates, rd, CreateRasterizerState, ID3D11RasterizerState)
    if (item && item->_object)
    {
      _d3dDeviceRec->RSSetState(item->_object);
    }

#else //#if DX11_USE_HASHED_STATE_OBJECTS
  ID3D11RasterizerState* state= (ID3D11RasterizerState*)_rasterizerStateList.GetState(
    _currentRenderState._renderState);
  if (state && _currentRenderState._rasterizerStateLast != state)
  {
    _d3dDeviceRec->RSSetState(state);
    _currentRenderState._rasterizerStateLast = state;
  }
#endif //#if DX11_USE_HASHED_STATE_OBJECTS else
}

void EngineDD11::SetupSamplerStates()
{
  PROFILE_SCOPE_DETAIL_EX(stpSS, dd11)

#if DX11_USE_HASHED_STATE_OBJECTS
 
  ID3D11SamplerState* samplerStatesPtr[MaxSamplerStages];
  for (int i = 0; i < MaxSamplerStages; i++)
  {
    D3D11_SAMPLER_DESC sd;

    // Filter
    if (_currentRenderState._samplerState[i][D3DSAMP_MINFILTER_OLD] == D3DTEXF_POINT_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MAGFILTER_OLD] == D3DTEXF_POINT_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MIPFILTER_OLD] == D3DTEXF_POINT_OLD)
    {
      sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    }
    else if (_currentRenderState._samplerState[i][D3DSAMP_MINFILTER_OLD] == D3DTEXF_POINT_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MAGFILTER_OLD] == D3DTEXF_POINT_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MIPFILTER_OLD] == D3DTEXF_LINEAR_OLD)
    {
      sd.Filter = D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    }

    else if (_currentRenderState._samplerState[i][D3DSAMP_MINFILTER_OLD] == D3DTEXF_POINT_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MAGFILTER_OLD] == D3DTEXF_LINEAR_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MIPFILTER_OLD] == D3DTEXF_POINT_OLD)
    {
      sd.Filter = D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
    }
    else if (_currentRenderState._samplerState[i][D3DSAMP_MINFILTER_OLD] == D3DTEXF_POINT_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MAGFILTER_OLD] == D3DTEXF_LINEAR_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MIPFILTER_OLD] == D3DTEXF_LINEAR_OLD)
    {
      sd.Filter = D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR;
    }
    else if (_currentRenderState._samplerState[i][D3DSAMP_MINFILTER_OLD] == D3DTEXF_LINEAR_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MAGFILTER_OLD] == D3DTEXF_POINT_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MIPFILTER_OLD] == D3DTEXF_POINT_OLD)
    {
      sd.Filter = D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    }
    else if (_currentRenderState._samplerState[i][D3DSAMP_MINFILTER_OLD] == D3DTEXF_LINEAR_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MAGFILTER_OLD] == D3DTEXF_POINT_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MIPFILTER_OLD] == D3DTEXF_LINEAR_OLD)
    {
      sd.Filter = D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
    }
    else if (_currentRenderState._samplerState[i][D3DSAMP_MINFILTER_OLD] == D3DTEXF_LINEAR_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MAGFILTER_OLD] == D3DTEXF_LINEAR_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MIPFILTER_OLD] == D3DTEXF_POINT_OLD)
    {
      sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    }
    else if (_currentRenderState._samplerState[i][D3DSAMP_MINFILTER_OLD] == D3DTEXF_LINEAR_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MAGFILTER_OLD] == D3DTEXF_LINEAR_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MIPFILTER_OLD] == D3DTEXF_LINEAR_OLD)
    {
      sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    }

    else if (_currentRenderState._samplerState[i][D3DSAMP_MINFILTER_OLD] == D3DTEXF_ANISOTROPIC_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MAGFILTER_OLD] == D3DTEXF_ANISOTROPIC_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MIPFILTER_OLD] == D3DTEXF_ANISOTROPIC_OLD)
    {
      sd.Filter = D3D11_FILTER_ANISOTROPIC;
    }
    else if (_currentRenderState._samplerState[i][D3DSAMP_MINFILTER_OLD] == D3DTEXF_POINT_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MAGFILTER_OLD] == D3DTEXF_POINT_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MIPFILTER_OLD] == D3DTEXF_NONE_OLD)
    {
      sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    }
    else if (_currentRenderState._samplerState[i][D3DSAMP_MINFILTER_OLD] == D3DTEXF_LINEAR_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MAGFILTER_OLD] == D3DTEXF_LINEAR_OLD &&
      _currentRenderState._samplerState[i][D3DSAMP_MIPFILTER_OLD] == D3DTEXF_NONE_OLD)
    {
      sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    }
    else
    {
      sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
      Fail("DX11 Error: Not handled");
    }

    // AddressU
    switch (_currentRenderState._samplerState[i][D3DSAMP_ADDRESSU_OLD])
    {
    case D3DTADDRESS_WRAP_OLD:
      sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
      break;
    case D3DTADDRESS_CLAMP_OLD:
      sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
      break;
    case D3DTADDRESS_BORDER_OLD:
      sd.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
      break;
    default:
      sd.AddressU = (D3D11_TEXTURE_ADDRESS_MODE)0;
      Fail("DX11 Error: Not handled");
    }

    // AddressV
    switch (_currentRenderState._samplerState[i][D3DSAMP_ADDRESSV_OLD])
    {
    case D3DTADDRESS_WRAP_OLD:
      sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
      break;
    case D3DTADDRESS_CLAMP_OLD:
      sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
      break;
    default:
      sd.AddressV = (D3D11_TEXTURE_ADDRESS_MODE)0;
      Fail("DX11 Error: Not handled");
    }

    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MipLODBias = 0;

    // MaxAnisotropy
    sd.MaxAnisotropy = _currentRenderState._samplerState[i][D3DSAMP_MAXANISOTROPY_OLD];

    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.BorderColor[0] = 1.0f;
    sd.BorderColor[1] = 1.0f;
    sd.BorderColor[2] = 1.0f;
    sd.BorderColor[3] = 1.0f;
    if (_currentRenderState._samplerState[i][D3DSAMP_MIPFILTER_OLD] == D3DTEXF_NONE_OLD)
    {
      sd.MinLOD = 0;
      sd.MaxLOD = 0;
    }
    else
    {
      sd.MinLOD = -D3D11_FLOAT32_MAX;
      sd.MaxLOD = D3D11_FLOAT32_MAX;
    }

    // Get the item from the pool (or create it) and return it
    GET_HASHTABLE_ITEM(TSamplerStates, _samplerStates, sd, CreateSamplerState, ID3D11SamplerState)
      if (item && item->_object)
      {
        samplerStatesPtr[i] = item->_object.GetRef();
      }
      else
      {
        samplerStatesPtr[i] = 0;
      }
  }

  // Set the render states
  _d3dDeviceRec->PSSetSamplers(0, MaxSamplerStages, 
    Array<ID3D11SamplerState*>(samplerStatesPtr, MaxSamplerStages));

#else //#if DX11_USE_HASHED_STATE_OBJECTS

  ID3D11SamplerState* samplerStatesPtr[MaxSamplerStages];
  for (int i = 0; i < MaxSamplerStages; i++)
  {
    if (!_currentRenderState._samplerStateChanged[i])
    {
      samplerStatesPtr[i] = _currentRenderState._samplerStateLast[i];
    }
    else
    {
      samplerStatesPtr[i] = (ID3D11SamplerState*)_samplerStateList.GetState(
        _currentRenderState._samplerState[i]);
    }

    _currentRenderState._samplerStateChanged[i] = false;
  }

  // Find the first difference
  int firstDiff;
  for (firstDiff = 0; firstDiff < MaxSamplerStages; firstDiff++)
  {
    if (_currentRenderState._samplerStateLast[firstDiff] != samplerStatesPtr[firstDiff]) break;
  }

  // If some differences found, find the upper boundary 
  if (firstDiff < MaxSamplerStages)
  {
    // Find the last difference 
    int lastDiff;
    for (lastDiff = MaxSamplerStages - 1; lastDiff >= 0; lastDiff--)
    {
      if (_currentRenderState._samplerStateLast[lastDiff] != samplerStatesPtr[lastDiff]) break;
    }
    DoAssert(firstDiff <= lastDiff);

    // setup samplers
    int count = lastDiff - firstDiff + 1;
   _d3dDeviceRec->PSSetSamplers(firstDiff,count, 
      Array<ID3D11SamplerState*>(&samplerStatesPtr[firstDiff], count));

    // Update the array
    for (int i = firstDiff; i <= lastDiff; i++)
    {
     _currentRenderState._samplerStateLast[i] = samplerStatesPtr[i];
    }
  }

#endif //#if DX11_USE_HASHED_STATE_OBJECTS else
}

void EngineDD11::SetupConstantBuffers()
{
  PROFILE_SCOPE_EX(stpCB, dd11)
  AssertSpecialLevelThreadAccess();
 
  // helper array to accumulate buffers to send
  ID3D11Buffer *dirtyBuffers[ShaderConstantBuffer::MaxConstantBufferCount]; 

  for (int shaderType = 0; shaderType < ShaderConstant::SCTCount; ++shaderType)
  {
    // number of constant buffers of given type
    int numBuffers = _currentRenderState._shaderConstants.GetBufferCount((ShaderConstant::ShaderConstantType)shaderType);

    Assert(numBuffers > 0);
    Assert(numBuffers = _shaderConstantBuffer[shaderType].Size());
    saturateMin(numBuffers, _shaderConstantBuffer[shaderType].Size());

    // index of the first buffer to send
    int firstDirtyBuffer = -1;
    // index of the last buffer to send
    int lastDirtyBuffer = -1;

    for (int i=0; i<numBuffers; ++i)
    {
      // get the buffer
      ShaderConstantBuffer *buffer = _currentRenderState._shaderConstants.GetBuffer((ShaderConstant::ShaderConstantType)shaderType, i);
      Assert(buffer);
      if (!buffer) 
        continue;

      if (buffer->IsDirty())
      {
        PROFILE_SCOPE_EX(stpCBUpd, dd11)

        // check if this is multi-size constant buffer
        bool isMulti = _shaderConstantBuffer[shaderType][i].IsMulti();
        int bufferSize = 0;
        if (isMulti)
        {
          //multi sized buffer - we can update only the part of the buffer which is dirty (but we have to update from start, not from GetDirtyStartOffset)
          bufferSize = buffer->GetDirtyStartOffset() + buffer->GetDirtySize();
          Assert(bufferSize <= buffer->GetData().Size());
          if (bufferSize > buffer->GetData().Size())
            bufferSize = buffer->GetData().Size();
        }
        else
        {
          //non-multi sized buffer - we have to update all
          // we need to copy whole buffer because of DISCARD
          bufferSize = buffer->GetData().Size();
        }

#if _ARMA3_DX11_PROFILING
        buffer->_profileInfoCur._profileNumBytesUpdateDone += bufferSize;
        buffer->_profileInfoCur._profileNumBytesUpdateNeeded += buffer->GetDirtySize();
        buffer->_profileInfoCur._profileNumUpdatesDone++;
#endif

        ComRef<ID3D11Buffer> constantBuffer = _shaderConstantBuffer[shaderType][i].GetBuffer(bufferSize);
        Assert(constantBuffer);
        if (constantBuffer)
        {
          if (D3DUseDynamicConstantBuffers)
          {
            void *data = NULL;
            HRESULT hr = Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &data);
            if (SUCCEEDED(hr) && data)
            {
              // we need to copy whole buffer because of DISCARD
              memcpy(data , buffer->GetData().Data(), bufferSize);
              Unmap(constantBuffer, 0);
            }
            else
            {
              Fail("Error: Mapping of buffer failed");
            }
          }
          else
          {
            // we cannot partially update constant buffer, we have to update whole (see Remarks in help for UpdateSubresource)
            _d3dDeviceRec->UpdateSubresource(constantBuffer, 0, 0, 0, 0,
              Array<unsigned char>((unsigned char *)buffer->GetData().Data(), bufferSize));
          }

          dirtyBuffers[i] = constantBuffer;
          if (_currentRenderState._constantBufferLast[shaderType][i] != constantBuffer)
          {
            _currentRenderState._constantBufferLast[shaderType][i] = constantBuffer;
            if (firstDirtyBuffer < 0)
            {
              firstDirtyBuffer = i;
            }
            lastDirtyBuffer = i;
          }
        }
      }
      else
      {
        // buffer is not dirty, use the last one
        dirtyBuffers[i] =  _currentRenderState._constantBufferLast[shaderType][i];
      }
    }

    if (firstDirtyBuffer >= 0 && lastDirtyBuffer >= firstDirtyBuffer)
    {
      Assert(firstDirtyBuffer < numBuffers);
      Assert(lastDirtyBuffer < numBuffers);
      int numToSend = lastDirtyBuffer - firstDirtyBuffer + 1;

      switch (shaderType)
      {
      case ShaderConstant::SCTVertex:
        _d3dDeviceRec->VSSetConstantBuffers(firstDirtyBuffer, numToSend, dirtyBuffers + firstDirtyBuffer);
        break;
      case ShaderConstant::SCTPixel:
        _d3dDeviceRec->PSSetConstantBuffers(firstDirtyBuffer, numToSend, dirtyBuffers + firstDirtyBuffer);
        break;
      default:
        Fail("Invalid shader constant buffer type.");
        break;
      }
    }
  }

  // reset dirty flags
  _currentRenderState._shaderConstants.ResetDirty();
}

void EngineDD11::SetupTextures()
{
  PROFILE_SCOPE_EX(stpTxt, dd11)

  // Find the first difference in texture stages
  int firstDiff;
  for (firstDiff = 0; firstDiff < MaxSamplerStages; firstDiff++)
  {
    if (_currentRenderState._lastHandle[firstDiff] != _currentRenderState._setHandle[firstDiff]) break;
  }

  // If some differences found, find the upper boundary and set the textures
  if (firstDiff < MaxSamplerStages)
  {
    // Find the last difference in texture stages
    int lastDiff;
    for (lastDiff = MaxSamplerStages - 1; lastDiff >= 0; lastDiff--)
    {
      if (_currentRenderState._lastHandle[lastDiff] != _currentRenderState._setHandle[lastDiff]) break;
    }
    DoAssert(firstDiff <= lastDiff);

    int count = lastDiff - firstDiff + 1;

    {
      PROFILE_SCOPE_DETAIL_EX(stpTxtSet, dd11)
        _d3dDeviceRec->PSSetShaderResources(firstDiff,count, 
        Array<TextureHandle11 *>(&_currentRenderState._lastHandle[firstDiff], count));
    }

    // update array
    for (int i = firstDiff; i <= lastDiff; ++i)    
    {
      _currentRenderState._setHandle[i] = _currentRenderState._lastHandle[i];
    }

  }
}

/*!
\patch 5138 Date 3/13/2007 by Ondra
- Fixed: Measures implemented to avoid textures being placed in AGP,
which often caused degraded performance after prolonged playing.
*/

void EngineDD11::CheckLocalNonlocalTextureMemory()
{
  // nothing needed here in DX11
}

void EngineDD11::DrawPolygon( const VertexIndex *ii, int n )
{
  if (ResetNeeded()) return;
  QueueFan(ii,n);
}

void EngineDD11::DrawSection
(
 const FaceArray &face, Offset beg, Offset end
 )
{
  if (ResetNeeded()) return;
  for( Offset i=beg; i<end; face.Next(i) )
  {
    const Poly &f=face[i];
    QueueFan(f.GetVertexList(),f.N());
  }
}

void EngineDD11::SetupInstances(int cb, const ObjectInstanceInfo *instances, int nInstances)
{
  // Do check we set a reasonable number of instances
  DoAssert(nInstances > 0);
  DoAssert(nInstances <= MaxInstances());

  // Prepare structure aligned to VS constants
  struct
  {
    MatrixFloat34 matrix;
    float color[4];
    float shadow_x_x_x[4];
  } matrices[LimitInstances];
  const int vectorsNum = sizeof(matrices[0])/4/4;
  Assert(vectorsNum == 5); // Currently we use 5 vectors

  // Fill out the structure
  for (int i = 0; i < nInstances; i++)
  {
    /// see CAM_REL_SPACE 
    ConvertMatrixTransposed3Offset(matrices[i].matrix, instances[i]._origin, CBState(cb)._invView.Position());
    matrices[i].color[0] = instances[i]._color.R();
    matrices[i].color[1] = instances[i]._color.G();
    matrices[i].color[2] = instances[i]._color.B();
    matrices[i].color[3] = instances[i]._color.A();
    matrices[i].shadow_x_x_x[0] =
      matrices[i].shadow_x_x_x[1] =
      matrices[i].shadow_x_x_x[2] =
      matrices[i].shadow_x_x_x[3] = instances[i]._shadowCoef;
  }
  SetVertexShaderConstantF(cb,VSC_SkinningInstancingData, (float*)matrices, nInstances * vectorsNum);
}

void EngineDD11::SetupInstancesSprite(int cb, const ObjectInstanceInfo *instances, int nInstances)
{
  // Do check we set a reasonable number of instances
  DoAssert(nInstances > 0);
  DoAssert(nInstances <= MaxInstancesSprite());

  // Prepare structure aligned to VS constants
  float matrices[LimitInstancesSprite][3][4];
  const int vectorsNum = sizeof(matrices[0])/4/4;
  Assert(vectorsNum == 3); // Currently we use 3 vectors

  // Fill out the structure
  for (int i = 0; i < nInstances; i++)
  {
    Object::ProtectedVisualState<const ObjectVisualState> vs = instances[i]._object->FutureVisualStateScope();

    /// see CAM_REL_SPACE 
    // Position and scale
    Vector3 invPos = CBState(cb)._invView.Position();
    matrices[i][0][0] = vs->Position().X() - invPos.X();
    matrices[i][0][1] = vs->Position().Y() - invPos.Y();
    matrices[i][0][2] = vs->Position().Z() - invPos.Z();
    matrices[i][0][3] = vs->Scale();
    if (!instances[i]._limitSpriteSize) matrices[i][0][3] *= -1;

    // Color
    ColorVal color = instances[i]._object->GetConstantColor();
    memcpy(&matrices[i][1], &color, sizeof(float)*4);

    // Mapping vector
    FrameSource frameSource;
    if (instances[i]._object->GetFrameSource(frameSource))
    {
      // Get the animation index
      int animationIndex;
      {
        // Read the animation index from the animation phase
        float animationPhase = instances[i]._object->GetAnimationPhase();
        animationIndex = toIntFloor(animationPhase * frameSource._frameCount);

        // Saturate the animationIndex to make sure it is in reasonable interval
        saturate(animationIndex, 0, frameSource._frameCount - 1);

        // The animationPhase can be greater than 1 - make a loop from the values above 1
        //animationIndex = animationIndex % frameSource._frameCount;
      }

      // Split the animation index to multi-line (2 coordinates)
      int animationIndexY = animationIndex / frameSource._ntieth;
      int animationIndexX = animationIndex - animationIndexY * frameSource._ntieth;

      // Calculate the mapping coordinates
      float frameSize = 1.0f / frameSource._ntieth;
      matrices[i][2][0] = frameSize * animationIndexX;
      matrices[i][2][1] = frameSize * (frameSource._index + animationIndexY);
      matrices[i][2][2] = frameSize;
    }
    else
    {
      matrices[i][2][0] = 0.0f;
      matrices[i][2][1] = 0.0f;
      matrices[i][2][2] = 1.0f;
    }

    // Angle
    matrices[i][2][3] = instances[i]._object->GetAngle();
  }
  SetVertexShaderConstantF(cb,VSC_SkinningInstancingData, (float*)matrices, nInstances * vectorsNum);
}

void EngineDD11::SetupInstancesVolCloud(int cb, const ObjectInstanceInfo *instances, int nInstances)
{
  // Do check we set a reasonable number of instances
  DoAssert(nInstances > 0);
  DoAssert(nInstances <= MaxInstancesSprite());

  // Prepare structure aligned to VS constants
  float matrices[LimitInstancesSprite][3][4];
  const int vectorsNum = sizeof(matrices[0])/4/4;
  Assert(vectorsNum == 3); // Currently we use 3 vectors

  // Fill out the structure
  for (int i = 0; i < nInstances; i++)
  {
    Object::ProtectedVisualState<const ObjectVisualState> vs = instances[i]._object->FutureVisualStateScope();

    /// see CAM_REL_SPACE 
    // Position and scale
    Vector3 invPos = CBState(cb)._invView.Position();
    matrices[i][0][0] = vs->Position().X() - invPos.X();
    matrices[i][0][1] = vs->Position().Y() - invPos.Y();
    matrices[i][0][2] = vs->Position().Z() - invPos.Z();
    matrices[i][0][3] = vs->Scale();
    if (!instances[i]._limitSpriteSize) matrices[i][0][3] *= -1;

    // Cloud center and size
    Vector3 center(0,0,0);
    float height = 1.0f; //to prevent division by zero
    if (LODShape *shape = instances[i]._object->GetShape())
    {
      center = shape->BoundingCenter();
      height = 2.0f * shape->BoundingSphere() * vs->Scale();
      if (height <= 0)
        height = 1.0f;
    }

    // we want the center to be relative to camera pos (because object positions are also relative)
    matrices[i][1][0] = center[0] - invPos[0];
    matrices[i][1][1] = center[1] - invPos[1];
    matrices[i][1][2] = center[2] - invPos[2];
    matrices[i][1][3] = height;

    // Mapping vector
    FrameSource frameSource;
    if (instances[i]._object->GetFrameSource(frameSource))
    {
      // Get the animation index
      int animationIndex;
      {
        // Read the animation index from the animation phase
        float animationPhase = instances[i]._object->GetAnimationPhase();
        animationIndex = toIntFloor(animationPhase * frameSource._frameCount);

        // Saturate the animationIndex to make sure it is in reasonable interval
        saturate(animationIndex, 0, frameSource._frameCount - 1);

        // The animationPhase can be greater than 1 - make a loop from the values above 1
        //animationIndex = animationIndex % frameSource._frameCount;
      }

      // Split the animation index to multi-line (2 coordinates)
      int animationIndexY = animationIndex / frameSource._ntieth;
      int animationIndexX = animationIndex - animationIndexY * frameSource._ntieth;

      // Calculate the mapping coordinates
      float frameSize = 1.0f / frameSource._ntieth;
      matrices[i][2][0] = frameSize * animationIndexX;
      matrices[i][2][1] = frameSize * (frameSource._index + animationIndexY);
      matrices[i][2][2] = frameSize;
    }
    else
    {
      matrices[i][2][0] = 0.0f;
      matrices[i][2][1] = 0.0f;
      matrices[i][2][2] = 1.0f;
    }

    // Angle
    matrices[i][2][3] = instances[i]._object->GetAngle();
  }
  SetVertexShaderConstantF(cb,VSC_SkinningInstancingData, (float*)matrices, nInstances * vectorsNum);
}

void EngineDD11::SetupInstancesShadowVolume(int cb, const ObjectInstanceInfo *instances, int nInstances)
{
  // Do check we set a reasonable number of instances
  DoAssert(nInstances > 0);
  DoAssert(nInstances <= MaxInstancesShadowVolume());

  // Prepare structure aligned to VS constants
  MatrixFloat34 matrices[LimitInstancesShadowVolume];
  const int vectorsNum = 3;
  Assert(vectorsNum*sizeof(float)*4==sizeof(matrices[0]));

  // Fill out the structure
  for (int i = 0; i < nInstances; i++)
  {
    /// see CAM_REL_SPACE 
    ConvertMatrixTransposed3Offset(matrices[i], instances[i]._origin, CBState(cb)._invView.Position());
  }
  SetVertexShaderConstantF(cb,VSC_SkinningInstancingData, (float*)matrices, nInstances * vectorsNum);
}

void EngineDD11::DrawMeshTLInstanced(
                                    int cb, const Shape &sMesh, const SectionMaterialLODs &matLOD, const EngineShapeProperties &prop, int spec, const LightList &lights, int bias,
                                    const ObjectInstanceInfo *instances, int nInstances, float minDist2, const DrawParameters &dp
                                    )
{
  DoAssert(nInstances > 0);

  // Go through all instances in chunks
  VertexBufferD3D11 *vb = static_cast<VertexBufferD3D11 *>(sMesh.GetVertexBuffer());
  if (!vb)
  {
    // this may happen when D3DERR_OUTOFVIDEOMEMORY or a similar error prevents VB creation
    return;
  }
  int nInstancesInOneStep = vb->GetVBufferSize() / sMesh.NVertex();
  DoAssert((vb->GetVBufferSize() % sMesh.NVertex()) == 0); // The numbers should be dividable without leaving a remainder

  // In case of night (or better - if there are any lights) reduce the number of instances in one step - to not to overwrite the light constants
  if (lights.Size() > 0)
  {
    switch (sMesh.GetVertexDeclaration())
    {
    case VD_Tex0:
      saturateMin(nInstancesInOneStep, _caps._maxNightInstancesSprite);
      break;
    case VD_Position_Normal_Tex0:
    case VD_Position_Normal_Tex0_ST:
    case VD_Position_Normal_Tex0_Tex1:
    case VD_Position_Normal_Tex0_Tex1_ST:
      saturateMin(nInstancesInOneStep, _caps._maxNightInstances);
      break;
    }
  }

#if _ENABLE_SKINNEDINSTANCING
  // Instances in one step must be >= 1 (in some cases the vb->GetVBufferSize() is uninitialized)
  if (nInstancesInOneStep >= 1)
#else
  // Instances in one step must be > 1 (we don't create VB for smaller size)
  if (nInstancesInOneStep > 1)
#endif
  {
    bool ok = BeginMeshTLInstanced(cb,sMesh, prop, spec, lights, bias, minDist2, dp);
    if (ok)
    {
      // Either use a program to draw or draw it the usual way
      int drawnInstances = 0;
      while (drawnInstances < nInstances)
      {
        // Get number of instances
        int newInstances = intMin(nInstancesInOneStep, nInstances - drawnInstances);

        // Set instances parameters
        if (instances[drawnInstances]._object)
        {
          instances[drawnInstances]._object->SetupInstances(cb,sMesh, &instances[drawnInstances], newInstances);
        }
        else
        {
          SetupInstances(cb,&instances[drawnInstances], newInstances);
        }

        DrawMeshTLSectionsInstanced(cb,sMesh, matLOD, prop, spec, bias, drawnInstances, newInstances);

        // Update the number of drawn instances
        drawnInstances += newInstances;
      }

      // Finish mesh
      EndMeshTL(cb,sMesh);
    }
  }
  else
  {
    DoAssert(instances);
    Engine::DrawMeshTLInstanced(cb,sMesh, prop, spec, lights, bias, instances, nInstances, minDist2, dp);
    return;
  }
}

void EngineDD11::DrawShadowVolumeInstanced(
  int cb, LODShape *shape, int level, const ObjectInstanceInfo *instances, int instanceCount, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
  , int drawListId
#endif
  )
{
  DoAssert(instanceCount > 0);

  Assert(shape->IsLevelLocked(level));
  const Shape *shadow = shape->GetLevelLocked(level);

  // Return immediately in some cases
  if (shadow->GetVertexBuffer() == NULL) return;

  // Go through all instances in chunks
  VertexBufferD3D11 *vb = static_cast<VertexBufferD3D11 *>(shadow->GetVertexBuffer());
  int nInstancesInOneStep = vb->GetVBufferSize() / shadow->NVertex();

#if _ENABLE_SKINNEDINSTANCING
  if ((instanceCount < 1) || (nInstancesInOneStep < 1))
#else
  if ((instanceCount <= 1) || (nInstancesInOneStep <= 1))
#endif
  {
    Engine::DrawShadowVolumeInstanced(cb,shape,level, instances, instanceCount,oi
#if _HELISIM_RENDER_TO_TEXTURE
      ,drawListId
#endif      
      );
    return;
  }

  // if shadow volume is complex, we want to limit its length as much as possible

  float maxShadowSize = Glob.config.GetMaxShadowSize();
  float shadowSize = maxShadowSize;

  //if (shape->LevelRef(level).NFaces()>500)
  { // check max. shadow for all instances
    float knownShadowSize = 0;
    // walk though the instances, check projection against ground
    for (int i=0; i<instanceCount; i++)
    {
      // Calculate shadow distance
      Vector3 minMax[2];
      instances[i]._object->ClippingInfo(instances[i]._object->RenderVisualState(),minMax,Object::ClipShadow);
      Matrix4Par frame = instances[i]._origin;

      float shadowSize = GScene->CheckShadowSize(minMax,frame,knownShadowSize);

      saturateMax(knownShadowSize,shadowSize);
      // if the shadow is long enough, break - it will not be any longer
      if (knownShadowSize>=maxShadowSize*0.99f) break;
    }
    shadowSize = knownShadowSize;


  }

  // Prepare shadow volume drawing
  BeginShadowVolume(cb,shadowSize);

  ADD_COUNTER(oShdV,instanceCount);
  DPRIM_STAT_SCOPE(cb<0 ? (shape)->ShapeName((level)) : "",ShdV);

  // Set shader constants
  int spec=IsShadow|IsAlphaFog;
  const LightList noLights;
  bool ok = BeginMeshTLInstanced(cb,*shadow, *shape, spec, noLights, ShadowBias, -1.0f, DrawParameters::_default);
  if (ok)
  {

    // Set shadow material
    TLMaterial shadowMat;
    shadowMat.diffuse = HBlack;
    shadowMat.ambient = HBlack;
    shadowMat.emmisive = HBlack;
    shadowMat.forcedDiffuse = HBlack;
    shadowMat.specFlags = 0;

    int drawnInstances;

    // ---------------
    // Draw back faces

    EngineShapeProperties prop;
    drawnInstances = 0;
    while (drawnInstances < instanceCount)
    {
      // Get number of instances
      int newInstances = intMin(nInstancesInOneStep, instanceCount - drawnInstances);

      // Set the instances
      SetupInstancesShadowVolume(cb,&instances[drawnInstances], newInstances);

      // Go through all the sections
      for (int i = 0; i < shadow->NSections(); i++)
      {
        ShapeSection sec = shadow->GetSection(i);

        // Skip sections not designed for drawing
        if (sec.Special() & (IsHidden|IsHiddenProxy|NoShadow)) continue;

        // Draw section
        sec.OrSpecial(IsShadow | IsShadowVolume);
        sec.SetTexture(NULL);
        sec.PrepareTL(cb,shadowMat, 0, prop);
        DrawSectionTL(cb,*shadow, i, i + 1, ShadowBias, drawnInstances, newInstances);
      }

      // Update the number of drawn instances
      drawnInstances += newInstances;
    }

    if (!GEngine->CanTwoSidedStencil())
    {
      // ----------------
      // Draw front faces

      drawnInstances = 0;
      while (drawnInstances < instanceCount)
      {
        // Get number of instances
        int newInstances = intMin(nInstancesInOneStep, instanceCount - drawnInstances);

        // Set the instances
        SetupInstancesShadowVolume(cb,&instances[drawnInstances], newInstances);

        // Go through all the sections
        for (int i = 0; i < shadow->NSections(); i++)
        {
          ShapeSection sec = shadow->GetSection(i);

          // Skip sections not designed for drawing
          if (sec.Special() & (IsHidden|IsHiddenProxy|NoShadow)) continue;

          // Draw section
          sec.OrSpecial(IsShadow | IsShadowVolume | ShadowVolumeFrontFaces);
          sec.SetTexture(NULL);
          sec.PrepareTL(cb,shadowMat, 0, prop);
          DrawSectionTL(cb,*shadow, i, i + 1, ShadowBias, drawnInstances, newInstances);
        }

        // Update the number of drawn instances
        drawnInstances += newInstances;
      }
    }


    // Finish the mesh drawing
    EndMeshTL(cb,*shadow);
  }
}

void EngineDD11::BeginShadowVolume(int cb, float shadowVolumeSize)
{
  float constants[4] = {shadowVolumeSize, shadowVolumeSize, shadowVolumeSize , shadowVolumeSize};
  SetVertexShaderConstantF(cb,VSC_ShadowVolumeLength, constants, 1);
}

void EngineDD11::BeginShadowVolumeRendering()
{
  // we are already inside a "SetTemporaryRenderTarget" bracket
  // while rendering volume shadows, we need to avoid writing into a color buffer

  // dirty hack: leave _currRenderTargetSurface untouched, just point the real RT to something else

#ifdef _XBOX
  // the safest way to disable rendering on Xbox is to use no RT at all
  CALL_D3D(SetRenderTarget, 0, NULL);
#else
  // on PC we need to use a format which is known to support post pixel blending
  // we know SSSM uses X8R8G8B8
  // with FSAA we cannot use _sssmSurface
  //   if (GetFSAA()>0)
  //   {
  //     // _renderTargetS should do - we are not rendering anything into it anyway
  //     CALL_D3D(SetRenderTarget, 0, _renderTargetSurfaceS);
  //   }
  //   else
  {
    PushRenderTargetDepthStencil();
    SetRenderTargetAndDepthStencil(RenderTarget::RenderTargetNULL, _currDepthStencil, _minZRange, _maxZRange);
  }
#endif

  // make sure SSSM is not used as a source at the same time
  //SetTexture(-1,TEXID_SHADOWMAP, NULL);
  // Set the depth range to make sure the depth range has not been changed, sets also viewport dimensions
  // DX11 TODO - should not be needed
  //SetDepthRange(_minZRange,_maxZRange);
}
void EngineDD11::EndShadowVolumeRendering()
{
  PopRenderTargetDepthStencil();
  // Set the depth range to make sure the depth range has not been changed
  // DX11 TODO - should not be needed
  //SetDepthRange(_minZRange,_maxZRange);

#if _DEBUG || _PROFILE
  if (Glob.config._renderThread==Config::ThreadingEmulated)
  {
    KickOffCBDebug();
  }
#endif
}

float EngineDD11::GetClosestZ2(float minDist2)
{
  // Get the FOV for shadows
  float sFOVu = _aspectSettings.leftFOV;
  float sFOVv = _aspectSettings.topFOV;

  // Get the g - half diagonal on front plane
  float gu = sFOVu * 1.0f;
  float gv = sFOVv * 1.0f;
  float g = sqrt(gu*gu + gv*gv);

  // Calculate the square of distance of the edge of the plane in the distance of 1
  float a2 = g*g + 1.0f*1.0f;

  // Get the Z value closest to the camera
  return minDist2 / a2;
}
/// setups world/view/projection matrix and camera position
void EngineDD11::SetupWorldViewProjCam(int cb, const PositionRender &modelToWorld)
{
  SetWorldView(cb);

  RendState &cbState = CBState(cb);

  // Setup the camera position
  Camera *camera = GScene->GetCamera();
  Assert(camera);
  if (!camera)
    return;

  // Setup the world->view matrix (remember pointer to it)
  Matrix4 viewWorld;
#ifdef _VIEWPROJMATRIXPREMULTIPLIED
  const Matrix4 *pWorld2View;
#endif
  if (!modelToWorld.camSpace)
  {
    // Setup the view-world matrix
    // natural calculation would be
    //viewWorld = _view * modelToWorld;
    // this exhibits serious precision problems for large world space coordinates
    // instead of using inverse, subtract position and use inverse orientation only


    // VW = VR * VT * WR * WT
    Matrix4 modelToWorldOffset;
    modelToWorldOffset.SetOrientation(modelToWorld.position.Orientation());
    modelToWorldOffset.SetPosition(modelToWorld.position.Position()-cbState._invView.Position());
    Matrix4 viewZero;
    viewZero.SetOrientation(cbState._view.Orientation());
    viewZero.SetPosition(VZero);
    viewWorld = viewZero * modelToWorldOffset;

    //Matrix4 viewWorldTest = _view * modelToWorld;
    //static bool testOld = false;
    //if (testOld) viewWorld = viewWorldTest;

    PASSMATRIXTYPE viewWorldMatrix;
    ConvertMatrixTransposed(viewWorldMatrix, viewWorld);
    SetVertexShaderConstantF(cb,VSC_ViewMatrix, (float*)&viewWorldMatrix, 3);

#ifdef _VIEWPROJMATRIXPREMULTIPLIED
    // Remember the world2view matrix
    pWorld2View = &viewWorld;
#endif

    // Remember the world matrix (to determine the object space)
    cbState._world = modelToWorld;
  }
  else
  {
    // modelToWorld is actually viewWorld matrix (model to camera)
    // Setup the view-world matrix
    PASSMATRIXTYPE viewWorldMatrix;
    ConvertMatrixTransposed(viewWorldMatrix, modelToWorld.position);
    SetVertexShaderConstantF(cb,VSC_ViewMatrix, (float*)&viewWorldMatrix, 3);

#ifdef _VIEWPROJMATRIXPREMULTIPLIED
    // Remember the world2view matrix
    pWorld2View = &modelToWorld;
#endif

    // Construct the world matrix from camera and modelToWorld
    // cameraToWorld*modelToCamera
    cbState._world = camera->Transform()*modelToWorld.position;
  }
  cbState._invWorld = cbState._world.InverseGeneral();

  /// setup world matrix shader constant
  PASSMATRIXTYPE worldMatrix;
  ConvertMatrixTransposed(worldMatrix, cbState._world);
  SetVertexShaderConstantF(cb,VSC_WorldMatrix, (float*)&worldMatrix, 3);

#ifdef _VIEWPROJMATRIXPREMULTIPLIED
  // Set the view2Projection matrix
  if (_renderingMode != RMShadowBuffer) // If shadow buffer is being rendered, a special projection matrix was set and we don't want to rewrite it (see label SUPPRESSPROJCHANGE)
  {
    // Get the bias
    float camZTransform = camera->ClipNear() * 1.25f; // empirical, camZTransform should be around 1 for typical scenes
    float bias = _bias * camZTransform;

    // Get the world2View matrix
    PASSMATRIXTYPE world2ViewTransposed;
    ConvertMatrixTransposed(world2ViewTransposed, (*pWorld2View));
    PASSMATRIXTYPE world2View;
    D3DXMatrixTranspose(&world2View, &world2ViewTransposed);

    // Get the view2Proj matrix
    PASSMATRIXTYPE view2Proj;
    ConvertProjectionMatrix(view2Proj, _proj, bias);

    // Multiply both matrices, get the world2Projection matrix
    PASSMATRIXTYPE world2Projection;
    MultiplyMatrix4(world2Projection, world2View, view2Proj);

    // Setup the world2Projection matrix
    SetVertexShaderConstantF(VSC_ProjMatrix, (float*)&world2Projection, 4);
  }
#endif

  // Set camera position and direction in model (object) space to VS constants
  {
    // Camera position
    Vector3 cameraPosition = camera->Position();
    Vector3 cameraObjectPosition = TransformPositionToObjectSpace(cb,cameraPosition);
#if _VBS3_NO_SEABED
    float underwater = GScene->UnderWater() ? 0.0f : 1.0f;
    D3DXVECTOR4 cpos(cameraObjectPosition.X(), cameraObjectPosition.Y(), cameraObjectPosition.Z(), underwater);
#else
    D3DXVECTOR4 cpos(cameraObjectPosition.X(), cameraObjectPosition.Y(), cameraObjectPosition.Z(), 1.0f);
#endif
    SetVertexShaderConstantF(cb,VSC_CameraPosition, (float*)&cpos, 1);

    // Camera direction
    Vector3 cameraDirection = camera->Direction();
    Vector3 cameraObjectDirection = TransformAndNormalizeVectorToObjectSpace(cb,cameraDirection);
    D3DXVECTOR4 cdir(cameraObjectDirection.X(), cameraObjectDirection.Y(), cameraObjectDirection.Z(), 1.0f);
    SetVertexShaderConstantF(cb,VSC_CameraDirection, (float*)&cdir, 1);
  }
}

void EngineDD11::BeginInstanceTL(int cb, const PositionRender &modelToWorld, float minDist2, int spec, const LightList &lights, const DrawParameters &dp)
{
  if (ResetNeeded()) return;

  RendState &cbState = CBState(cb);

  SwitchTL(cb,TLEnabled);
  SetupWorldViewProjCam(cb, modelToWorld);

  PredicationMode pass = CurrentPredicationMode();

  // Selecting of pixel shader type
  {
    // If we render the depth map, then use the appropriate pixel shader
    if (_renderingMode == RMShadowBuffer)
    {
      SelectPixelShaderType(cb,PSTShadowCaster);
    }
    else
    {
      // Decide whether the shadow buffer should be casted on object or not
      // Objects with minDist2 smaller than 0 are not usual (like shadows) and we don't want
      // shadow to be casted on them. We also don't want to cast shadows in case shadow map
      // rendering is in progress. SREC (don't delete this label)

      // if z priming is not done for us, we cannot receive any shadows
      if (minDist2 >= 0.0f && cbState._zPrimingDone)
      {
        // Get the Z value closest to the camera
        float closestZ2 = GetClosestZ2(minDist2);

        // Select appropriate pixel shader type. Note that the _pixelShaderTypeSel value is also used for VS selection
        if (closestZ2 < Glob.config.GetShadowZ() * Glob.config.GetShadowZ())
        {
          // even when priming we can select SSSM, it will be overridden by PSTAlphaOnly anyway (in DoSelectPixelShader)
          // this makes it easier - no predication needed here
          SelectPixelShaderType(cb,PSTBasicSSSM);
        }
        else
        {
          SelectPixelShaderType(cb,PSTBasic);
        }
      }
      else
      {
        // Not designed as shadow receiver
        SelectPixelShaderType(cb,PSTBasic);
      }

      // Set the PSC_Shadow_Factor_ZHalf constant - used when visualizing shaders
      if (BeginPredicationAdvisory<RenderingPass>(cb,pass))
      {
        // Pass the shadowsZ value to the PS. _shadowFactor is here to make the shadow disappear
        // during day/night transition (the similar thing works in SV shadows too)
        float sZ = Glob.config.GetShadowZ();
        D3DXVECTOR4 vector4(_shadowFactor, _shadowFactor, _shadowFactor, sZ * 0.5f);
        SetPixelShaderConstantF(cb,PSC_Shadow_Factor_ZHalf, vector4, 1);
        EndPredicationAdvisory(cb);
      }
    }
  }

  // Setup the thermal constants
  if (cbState._pixelShaderTypeSel == PSTThermal)
  {
    DoAssert(dp._htMin >= 0);
    DoAssert(dp._htMax >= 0);
    DoAssert(dp._afMax >= 0);
    DoAssert(dp._mfMax >= 0);
    DoAssert(dp._mFact >= 0);
    DoAssert(dp._tBody >= 0);

    D3DXVECTOR4 vector4_0(dp._htMin, dp._htMax, dp._afMax, dp._mfMax);
    //D3DXVECTOR4 vector4_0(0, 0, 0, 0);
    SetPixelShaderConstantF(cb,PSC_HTMin_HTMax_AFMax_MFMax, vector4_0, 1);
    D3DXVECTOR4 vector4_1(dp._mFact, dp._tBody, 1.0f, dp._useOwnHeatSource ? dp._heatSourceDirection.Size() : 1.0f);    
    //D3DXVECTOR4 vector4_1(0, 0, 0, 0);
    SetPixelShaderConstantF(cb,PSC_MFact_TBody_X_HSDiffCoef, vector4_1, 1);
  }

  // Setup lights
  SetupLights(cb,lights, spec, dp);
}

void EngineDD11::SetWorldView(int cb)
{
  // Setup view and projection matrices only in case it wasn't set by shadow buffer rendering process already
  if (_renderingMode != RMShadowBuffer && GScene)
  {
    Camera *camera = GScene->GetCamera();
    CBState(cb)._invView = camera->Transform();
    CBState(cb)._view = camera->InverseScaled();
  }

  // Setup projection matrix
  ProjectionChanged(cb);
}

void EngineDD11::SetupLights(int cb, const LightList &lights, int spec, const DrawParameters &dp)
{
  // no scene - no lights
  if (!GScene) return;

  RendState &cbState = CBState(cb);
  // Main light handling
  {
    // TODO: lights persistent
    // disable all other lights
    // setup sun
    LightSun *sun = GScene->MainLight();

    // In case of thermal vision light is behaving differently - only sun is the heat source, usual light is not visible
    // See HEATSOURCEDIRECTION
    if (GetThermalVision())
    {
      cbState._lightAmbient = HBlack;
      cbState._lightDiffuse = HBlack;
      cbState._lightSpecular = HBlack;
      if (dp._useOwnHeatSource)
      {
        Vector3 worldHeatSourceDirection = cbState._world.Orientation() * dp._heatSourceDirection;
        cbState._direction = worldHeatSourceDirection.Normalized();
      }
      else
      {
        cbState._direction = sun->Direction();
        //_direction = -VUp;
      }
    }
    else
    {
      // Remember main light parameters
      cbState._lightAmbient = sun->GetAmbient() * GetAccomodateEye();
      cbState._lightDiffuse = sun->GetDiffuse() * GetAccomodateEye();
      cbState._lightSpecular = sun->GetDiffuse() * GetAccomodateEye();
      cbState._direction = sun->LightDirection();
    }
    cbState._shadowDirection = sun->ShadowDirection();
  }

  // P&S light handling
  if (GetThermalVision())
  {
    // Don't use local lights in case of thermal vision
    cbState._lights.Resize(0);
    cbState._pointLightsCount = 0;
    cbState._spotLightsCount = 0;
  }
  else
  {
    // Get the number of original lights - we may shrink it down in next steps
    int nLights = lights.Size();

    // Get the T&L lights factor
    float night = GScene->MainLight()->NightEffect();

    // If there is no sun casted on object, the P&S lights should be used with a full strength.
    // This is for instance the UI objects case
    if (spec&DisableSun) night = 1;

    // No night, no lights
    if (night<=0) nLights = 0;

    // limit lights by device caps
    saturateMin(nLights, _caps._maxLights-1);

    // Fill out the lights we want to use on the object,
    // and calculate number of point and spot lights
    cbState._lights.Resize(nLights);
    cbState._pointLightsCount = 0;
    cbState._spotLightsCount = 0;
    for (int i=0; i<nLights; i++)
    {
      // Remember the light
      const Light *light = lights[i];
      _night = night;
      cbState._lights[i] = const_cast<Light *>(light);

      // Update number of P&S lights
      LightDescription ldesc;
      light->GetDescription(ldesc, GetAccomodateEye());
      if (ldesc.type == LTPoint) cbState._pointLightsCount++;
      if (ldesc.type == LTSpotLight) cbState._spotLightsCount++;
    }
  }
}

/*!
\patch 1.01 Date 6/18/2001 by Ondra
- Fixed: T&L lights in daytime
*/

void EngineDD11::SetupMaterialLights(int cb, const TLMaterial &mat)
{
  // no scene - no lights
  if (!GScene) return;

  // Get the scene main light
  LightSun *sun = GScene->MainLight();

  RendState &cbState = CBState(cb);

  // Remember the material parameters
  cbState._matAmbient = mat.ambient;
  cbState._matDiffuse = mat.diffuse;
  cbState._matForcedDiffuse = mat.forcedDiffuse;
  cbState._matSpecular = mat.specular;
  cbState._matEmmisive = mat.emmisive;
  cbState._matPower = mat.specularPower;

  // Set the shader constants according to mainlight
  if (cbState._tlActive!=TLDisabled)
  {
    if (_renderingMode==RMShadowBuffer)
    {
    }
    else switch(cbState._mainLight)
    {
    case ML_Sun:
      {
        //SetVertexShaderConstantMLSunR(cbState._matAmbient, cbState._matDiffuse, cbState._matForcedDiffuse, cbState._matSpecular, cbState._matEmmisive);

        // Setup directional light constants
        Color diffuse = cbState._lightDiffuse * cbState._matDiffuse;
        diffuse.SetA(cbState._matDiffuse.A());
        SetVertexShaderConstantF(cb,VSC_LDirectionD, (float*)&diffuse, 1);
        Color specular = cbState._lightSpecular * cbState._matSpecular;
        SetVertexShaderConstantF(cb,VSC_LDirectionS, (float*)&specular, 1);
        Vector3 tDirection = TransformAndNormalizeVectorToObjectSpace(cb,cbState._direction);
        D3DXVECTOR4 direction(tDirection.X(), tDirection.Y(), tDirection.Z(), 1.0f);
        SetVertexShaderConstantF(cb,VSC_LDirectionTransformedDir, (float*)&direction, 1);
        Vector3 tDirectionUp = TransformAndNormalizeVectorToObjectSpace(cb,VUp);
        D3DXVECTOR4 directionUp(tDirectionUp.X(), tDirectionUp.Y(), tDirectionUp.Z(), 1.0f);
        SetVertexShaderConstantF(cb,VSC_UDirectionTransformedDir, (float*)&directionUp, 1);

        // Setup constant for ground color (for back reflection purposes)
        // See label GROUNDREFLECTION
#if _ARMA3_NEW_LIGHTING_CFG
        ColorVal groundColor = ComputeBackReflection(diffuse);
#else
        // Get the scene main light
        LightSun *sun = GScene->MainLight();
        ColorVal groundColor = sun->GroundReflection() * diffuse;
#endif
        SetVertexShaderConstantF(cb,VSC_LDirectionGround, (float*)&groundColor, 1);
      }
      break;

    case ML_Sky:
      {
        Color accom=GetAccomodateEye();
        Color sky,aroundSun;
        _sceneProps->GetSkyColor(sky,aroundSun);
        Color sunSkyColor = aroundSun * accom * _modColor;
        SetVertexShaderConstantF(cb,VSC_LDirectionSunSkyColor, (float*)&sunSkyColor, 1);

        Color skyColor = sky * accom * _modColor;
        SetVertexShaderConstantF(cb,VSC_LDirectionSkyColor, (float*)&skyColor, 1);

        // we always want to consider sun here, never the moon
        Vector3 tDirection = TransformAndNormalizeVectorToObjectSpace(cb,sun->SunDirection());
        D3DXVECTOR4 direction(tDirection.X(), tDirection.Y(), tDirection.Z(), 1.0f);
        SetVertexShaderConstantF(cb,VSC_LDirectionTransformedDirSky, (float*)&direction, 1);
      }
      break;

    case ML_Horizon:
      {
        Color accom=GetAccomodateEye();
        Color sky,aroundSun;
        _sceneProps->GetSkyColor(sky,aroundSun);
        Color skyColor = sky * accom * _modColor;
        SetVertexShaderConstantF(cb,VSC_LDirectionSetColorColor, (float*)&skyColor, 1);
      }
      break;
    case ML_SunObject:
      {
        Color accom = GetAccomodateEye();
        Color color = sun->SunObjectColor() * accom * _modColor;
        color.SetA(color.A() * mat.diffuse.A());
        SetVertexShaderConstantF(cb,VSC_LDirectionSetColorColor, (float*)&color, 1);
      }
      break;

    case ML_SunHaloObject:
      {
        Color accom = GetAccomodateEye();
        Color color = sun->SunHaloObjectColor() * accom * _modColor;
        color.SetA(color.A() * mat.diffuse.A());
        SetVertexShaderConstantF(cb,VSC_LDirectionSetColorColor, (float*)&color, 1);
      }
      break;

    case ML_MoonObject:
      {
        Color accom = GetAccomodateEye();
        Color color = sun->MoonObjectColor() * accom * _modColor;
        color.SetA(color.A() * mat.diffuse.A());
        SetVertexShaderConstantF(cb,VSC_LDirectionSetColorColor, (float*)&color, 1);
      }
      break;

    case ML_MoonHaloObject:
      {
        Color accom = GetAccomodateEye();
        Color color = sun->MoonHaloObjectColor() * accom * _modColor;
        color.SetA(color.A() * mat.diffuse.A());
        SetVertexShaderConstantF(cb,VSC_LDirectionSetColorColor, (float*)&color, 1);
      }
      break;

    case ML_None:
      {
        Color black = HBlack;
        SetVertexShaderConstantF(cb,VSC_LDirectionS, (float*)&black, 1);
        SetVertexShaderConstantF(cb,VSC_LDirectionD, (float*)&black, 1);
        //PB_SetVertexShaderConstantF(VSC_LDirectionGround, (float*)&black, 1);

        Vector3 tDirection = TransformAndNormalizeVectorToObjectSpace(cb,cbState._direction);
        D3DXVECTOR4 direction(tDirection.X(), tDirection.Y(), tDirection.Z(), 1.0f);
        SetVertexShaderConstantF(cb,VSC_LDirectionTransformedDir, (float*)&direction, 1);
        Vector3 tDirectionUp = TransformAndNormalizeVectorToObjectSpace(cb,VUp);
        D3DXVECTOR4 directionUp(tDirectionUp.X(), tDirectionUp.Y(), tDirectionUp.Z(), 1.0f);
        SetVertexShaderConstantF(cb,VSC_UDirectionTransformedDir, (float*)&directionUp, 1);
      }
      break;

    default:
      Fail("Unknown type of main light");
    }
  }
}

int EngineDD11::ShapePropertiesNeeded(const TexMaterial *mat) const
{
  switch (mat->GetVertexShaderID())
  {
  case VSNormalMapThrough:
  case VSNormalMapSpecularThrough:
  case VSNormalMapThroughNoFade:
  case VSNormalMapSpecularThroughNoFade:
    return ESPTreeCrown;
  }
  return 0;
}

bool EngineDD11::BeginMeshTL(
                            int cb, const VertexTableAnimationContext *animContext, const Shape &sMesh, const EngineShapeProperties &prop, int spec, const LightList &lights, int bias
                            )
{
  if (ResetNeeded()) return false;

  // no need to do it here, it was already done in BeginInstanceTL
  //SwitchTL(cb,TLEnabled);
  //SetWorldView(cb);

  if (_renderingMode==RMShadowBuffer)
  {
    PrepareShadowOffset(cb,prop);
  }

  VertexBufferD3D11 *vb = static_cast<VertexBufferD3D11 *>(sMesh.GetVertexBuffer());
  if (!vb)
  {
    // this may happen when D3DERR_OUTOFVIDEOMEMORY or a similar error prevents VB creation
    return false;
  }

  // it should be added while preparing the rendering
  //Assert(vb->IsInList()); // note: the check is not MT safe, another thread may be updating
  // update if necessary
  vb->Lock(&sMesh);

  vb->Update(cb,animContext, sMesh, prop, VSDV_Basic);

  return true;
}

void EngineDD11::EndMeshTL( int cb, const Shape &sMesh )
{
  //_sBuffer = NULL;
  // turn off all lights
  ClearLights(cb);
  VertexBufferD3D11 *vb = static_cast<VertexBufferD3D11 *>(sMesh.GetVertexBuffer());
  vb->Unlock(&sMesh);
}

void EngineDD11::ClearLights(int cb)
{
  CBState(cb)._lights.Resize(0); // currently active lights
  if (CBState(cb)._lights.MaxSize()>64) CBState(cb)._lights.Clear();
}

#if _ENABLE_CHEATS
static bool UseDepthBias = true;
static float DepthBiasOffsetCoef = 0.05f;
#else
const bool UseDepthBias = true;
const float DepthBiasOffsetCoef = 0.05f;
#endif

void EngineDD11::PrepareShadowOffset(int cb, const EngineShapeProperties &prop)
{
  float offset = prop.GetShadowOffset();

  // techniques which use sloped depth may use a lot lower shadow offset
  if (UseDepthBias) offset *= DepthBiasOffsetCoef;

  float offsetC[4] = {offset,offset,offset,offset};
  SetVertexShaderConstantF(cb,VSC_ShadowVolumeLength, offsetC, 1);
}

void EngineDD11::EnableSBShadows(int quality)
{
  _sbQualityWanted = quality;
}

bool EngineDD11::IsSBEnabled() const
{
  return _depthStencilSB._dsView.NotNull();
}

void EngineDD11::PrepareShadowMapRendering(float sunClosestDistanceFrom0, int layerIndex, float shadowsZ, bool firstLayer)
{
  if (_sbQuality > 0)
  {
    Matrix4 shadowPlaneOrigin = ComputeShadowPlaneOrigin(shadowsZ, sunClosestDistanceFrom0, layerIndex);
    ComputeShadowConstants(shadowsZ, sunClosestDistanceFrom0, shadowPlaneOrigin);
  }
}

void EngineDD11::BeginShadowMapRendering(float sunClosestDistanceFrom0, int layerIndex, float shadowsZ, bool firstLayer, bool rainMap)
{
  // The optional diagnostic rain-map pass is not implemented by this backend.
  // IsRainMapEnabled() remains false, but keep the entry point safe if called.
  if (rainMap) return;

  const Camera *cam = GScene->GetCamera();

  const int cb = -1;
  // Calculate position and radius of the frustum bounding sphere
  Matrix4 shadowPlaneOrigin = ComputeShadowPlaneOrigin(shadowsZ, sunClosestDistanceFrom0, layerIndex);

  //for (int c=0; c<_cbState.Size(); c++)
  // shadow rendering will always read from CB -1.
  // Replication for individual CBs is handled in StartCBScope - see _undefined
  int c = cb+1;
  {
    _cbState[c]._invView = shadowPlaneOrigin;
    _cbState[c]._view = shadowPlaneOrigin.InverseRotation();
    _cbState[c]._proj = cam->ProjectionNormal();
  }

  // Set rendering parameters 
  // Set the depth biases in render states
#if _ENABLE_CHEATS
  // to prevent misunderstand, allow tuning values only when they are used
  // DX11 - will not work now because of state objects lists
  //if (UseDepthBias)
  //{
  //  if( GInput.GetCheat3ToDo(DIK_UP))
  //  {
  //    fDepthBias *= 1.1f;
  //    GlobalShowMessage(500,"DepthBias %f", fDepthBias);
  //  }
  //  if( GInput.GetCheat3ToDo(DIK_DOWN))
  //  {
  //    fDepthBias *= 1/1.1f;
  //    GlobalShowMessage(500,"DepthBias %f", fDepthBias);
  //  }
  //  if( GInput.GetCheat3ToDo(DIK_LEFT))
  //  {
  //    fSlopeScaleDepthBias *= 1.1f;
  //    GlobalShowMessage(500,"SlopeScaleDepthBias %f", fSlopeScaleDepthBias);
  //  }
  //  if( GInput.GetCheat3ToDo(DIK_RIGHT))
  //  {
  //    fSlopeScaleDepthBias *= 1/1.1f;
  //    GlobalShowMessage(500,"SlopeScaleDepthBias %f", fSlopeScaleDepthBias);
  //  }
  //  if( GInput.GetCheat3ToDo(DIK_PGUP))
  //  {
  //    DepthBiasOffsetCoef *= 1.1f;
  //    GlobalShowMessage(500,"Shadow offset scale %f", DepthBiasOffsetCoef);
  //  }
  //  if( GInput.GetCheat3ToDo(DIK_PGDN))
  //  {
  //    DepthBiasOffsetCoef *= 1/1.1f;
  //    GlobalShowMessage(500,"Shadow offset scale %f", DepthBiasOffsetCoef);
  //  }
  //}
  //if( GInput.GetCheat3ToDo(DIK_END))
  //{
  //  UseDepthBias = !UseDepthBias;
  //  GlobalShowMessage(1000, "Depth bias %s", UseDepthBias? "On" : "Off");
  //}
#endif
  if (UseDepthBias)
  {
    DWORD dwDepthBias = ShadowMapDepthBias;
    DWORD dwSlopeScaleDepthBias = *(const DWORD*)&ShadowMapSlopeScaleDepthBias;
    D3DSetRenderState(cb, D3DRS_DEPTHBIAS_OLD, dwDepthBias);
    D3DSetRenderState(cb, D3DRS_SLOPESCALEDEPTHBIAS_OLD, dwSlopeScaleDepthBias);
  }
  else
  {
    const float zero = 0.0f;
    DWORD dwZero = *(const DWORD*)&zero;
    D3DSetRenderState(cb, D3DRS_DEPTHBIAS_OLD, 0);
    D3DSetRenderState(cb, D3DRS_SLOPESCALEDEPTHBIAS_OLD, dwZero);
  }

  float radius = ComputeShadowConstants(shadowsZ, sunClosestDistanceFrom0, shadowPlaneOrigin);

  // Set the SB rendering flag
  _renderingMode = RMShadowBuffer;

  // Set own projection matrix parameters - projection matrix for shadow map rendering
  {
    float invRadius = 1.0f / radius;
    Matrix4 smProjection = Matrix4(
      invRadius,    0,          0,                0,
      0,            invRadius,  0,                0,
      0,            0,          1.0f / _maxDepth,  0);
    PASSMATRIXTYPE smProjectionDX;
    ConvertMatrixTransposed(smProjectionDX, smProjection);
    SetVertexShaderConstantF(-1,VSC_ProjMatrix, (float*)&smProjectionDX, 4);
  }

  {
    Vector3Val shadowDirection = GScene->MainLight()->ShadowDirection();
    Vector3 tDirection = CBState(cb)._view.Orientation() * shadowDirection;
    //     Vector3 tDirection = shadowPlaneOrigin * shadowDirection;
    //     tDirection.Normalize();
    D3DXVECTOR4 direction(tDirection.X(), tDirection.Y(), tDirection.Z(), 1.0f);
    SetVertexShaderConstantF(cb,VSC_LDirectionTransformedDir, (float*)direction, 1);
  }

  // Set the SM texture to NULL
  SetTextureDirect(cb,TEXID_SHADOWMAP, NULL);

  PushRenderTargetDepthStencil();
  // Set shadow buffer as temporary render target
  SetRenderTargetAndDepthStencil(RenderTarget::RenderTargetNULL, _depthStencilSB, 0, 1);

#ifdef _XBOX
  // Clear the z-buffer
  CALL_D3D(ClearF, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, NULL, NULL, 1.0f, 0L);
#else
  // Clear the buffer
  Clear( true, false, PackedColor(HWhite));
#endif
}

float EngineDD11::ComputeShadowConstants(float shadowsZ, float sunClosestDistanceFrom0, Matrix4Val shadowPlaneOrigin)
{
  const Camera *cam = GScene->GetCamera();

  Vector3 position;
  float radius = ComputeShadowFrustumBoundingSphere(shadowsZ,position);

  float shadowPlaneOffset = ComputeShadowPlaneOffset(position, sunClosestDistanceFrom0, radius);

  // Set the shadow plane projection parameters
  Matrix4 shadowPlaneProjection;
  float maxDepth = radius + shadowPlaneOffset;
  // avoid exactly Glob.config.shadowsZ, as this is the deepest value represented in the buffer
  //float maxDepth = Glob.config.shadowsZ*1.01f; // radius + shadowPlaneOffset;
  //float maxDepth = Glob.config.shadowsZ;
  {
    float diameter = radius * 2.0f;
    float invDiameter = 1.0f / diameter;
    shadowPlaneProjection = Matrix4(
      invDiameter,  0,            0,                                      0.5,
      0,            -invDiameter, 0,                                      0.5,
      0,            0,            1.0f / maxDepth,                        0);
  }

  // Remember the SM matrix (matrix from view space to shadow plane space)
  // FLY unknown reason for scaling the camera origin - can we use cam->InverseRotation everywhere?
  Matrix4 shadowMapMatrix = shadowPlaneProjection * (cam->InverseScaled() * shadowPlaneOrigin).InverseScaled();
  ConvertMatrixTransposed(_shadowMapMatrixDX, shadowMapMatrix);

#if 0
  if (layerIndex==ShadowBufferCascadeLayers - 1)
  {
    DIAG_MESSAGE(100,Format(
      "Shadow depth %.2f, origin %.2f,%.2f,%.2f",
      maxDepth,
      shadowPlaneOrigin.Position().X(),shadowPlaneOrigin.Position().Y(),shadowPlaneOrigin.Position().Z()
      ));
  }
  if (layerIndex==0)
  {
    DIAG_MESSAGE(100,Format(
      "Shadow depth 0 %.2f, origin %.2f,%.2f,%.2f",
      maxDepth,
      shadowPlaneOrigin.Position().X(),shadowPlaneOrigin.Position().Y(),shadowPlaneOrigin.Position().Z()
      ));
  }
#endif
  // Remember the SB depth scale
  _maxDepth = maxDepth;

  return radius;
}


/*!
\patch 5126 Date 2/2/2007 by Ondra
- Fixed: When using shadow detail high with some nVidia cards (Nv3x chip-set)
loading screen could happen sometimes mid-game.
The problem was caused by bad usage of D3DSAMP_SRGBTEXTURE, which was wrongly handled by the driver.
*/

void EngineDD11::EndShadowMapRendering(float minShadowsZ, float maxShadowsZ, bool firstLayer, bool lastLayer, bool rainMap)
{
  if (rainMap) return;

  const int cb = -1;
#ifdef _XBOX
  // Resolve to _renderTargetDepthBufferSB
  CALL_D3D(Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, _renderTargetSB, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif

  //CALL_D3D(EndScene,);

  // Restore the old render target
  PopRenderTargetDepthStencil();

  // Restore original setting of the viewport
  // See the label SBSVP
  {
    SetViewport(_wRT, _hRT, 0, 1);
  }

  // Restore default bias values
  D3DSetRenderState(cb, D3DRS_DEPTHBIAS_OLD, 0);
  float zero = 0.0f;
  DWORD dwZero = *(const DWORD*)&zero;
  D3DSetRenderState(cb, D3DRS_SLOPESCALEDEPTHBIAS_OLD, dwZero);

  // reset the SB rendering flag
  _renderingMode = RMCommon;

  // Create the SSSM
  _pp._SSSM->Do(minShadowsZ, maxShadowsZ, firstLayer, lastLayer);

#ifndef _XBOX
  // Add stencil shadows to the SSSM map
  if (lastLayer)
  {
    _pp._SSSMStencil->Do(false);
  }
#endif

#ifdef SSSMBLUR
  // Create the SSSM blurred map
  if (lastLayer)
  {
    _pp._SSSMB->Do(false);
  }
#endif


  // Restore the camera projection setting. Note this must be called after _renderingMode was set
  ProjectionChanged(-1);

  // Set the _view back
  DoSwitchTL(-1,TLEnabled);
}

float EngineDD11::ComputeShadowFrustumBoundingSphere(float shadowsZ, Vector3 &position)
{
  const Camera *cam = GScene->GetCamera();
  // Get the near and far frustum boundaries
  float frustumFront = cam->Near();
  float frustumBack = shadowsZ;

  // Get the FOV for shadows
  // note: cam Left / Top are calculated from cameraFOV from the vehicle
  // cam->Left() = _aspectSettings.leftFOV * cameraFOV
  // cam->Top() = _aspectSettings.topFOV * cameraFOV
  float sFOVu = floatMax(_aspectSettings.leftFOV, cam->Left());
  float sFOVv = floatMax(_aspectSettings.topFOV, cam->Top());

  // Shadow receiver estimation will not work properly - see the SREC label
  // Assert(cameraFOV <= 1.0f);

  // Get the g and c - half diagonals on front and back plane
  float gu = sFOVu * frustumFront;
  float gv = sFOVv * frustumFront;
  float g = sqrt(gu*gu + gv*gv);
  float cu = sFOVu * frustumBack;
  float cv = sFOVv * frustumBack;
  float c = sqrt(cu*cu + cv*cv);

  // Calculate the center s
  float s = (frustumFront * (frustumFront + frustumBack) + g * (g + c)) / (2.0f * frustumFront);
  if (s>frustumBack) s = frustumBack;

  // Calculate the radius of the frustum bounding sphere
  float backDistance = frustumBack - s;
  position  = cam->Position() + cam->Direction() * s;

  return sqrt(backDistance*backDistance + c*c);


}


float EngineDD11::ComputeShadowPlaneOffset(Vector3Par position, float sunClosestDistanceFrom0, float radius)
{
  // Get the shadow direction
  Vector3Val shadowDirection = GScene->MainLight()->ShadowDirection();

  // Calculate the sphere position distance from 0 in the sun direction
  float d = position.DotProduct(shadowDirection);

  // Difference between this distance and closest-object-to-sun distance is the required offset
  return floatMax(d - sunClosestDistanceFrom0, radius);
}

static Vector3 RoundVector(Vector3Par point, Vector3Par roundpoint, float smTexelSize)
{
  return Vector3(
    roundpoint.X() + toLargeInt((point.X() - roundpoint.X()) / smTexelSize) * smTexelSize,
    roundpoint.Y() + toLargeInt((point.Y() - roundpoint.Y()) / smTexelSize) * smTexelSize,
    roundpoint.Z() + toLargeInt((point.Z() - roundpoint.Z()) / smTexelSize) * smTexelSize
    );
}

Matrix4 EngineDD11::ComputeShadowPlaneOrigin(float shadowsZ, float sunClosestDistanceFrom0, int layerIndex)
{
  const Camera *cam = GScene->GetCamera();

  Vector3 position;
  float radius = ComputeShadowFrustumBoundingSphere(shadowsZ,position);

  float shadowPlaneOffset = ComputeShadowPlaneOffset(position, sunClosestDistanceFrom0, radius);
  Vector3Val shadowDirection = GScene->MainLight()->ShadowDirection();

  // Calculate the shadow plane origin
  Matrix4 shadowPlaneOrigin;
  {
    // Move in the local rounded shadow plane space according to current camera position
    Vector3 lrspCameraPos = _oldLRSP[layerIndex].InverseRotation().FastTransform(cam->Position());
    float diameter = radius * 2.0f;
    float smTexelSize = diameter / _sbSize;
    Vector3 roundedLRSPCameraPos = RoundVector(lrspCameraPos, VZero, smTexelSize);
    _oldLRSP[layerIndex].SetPosition(_oldLRSP[layerIndex].FastTransform(roundedLRSPCameraPos));

    // Set the origin to fit the current light direction
    _oldLRSP[layerIndex].SetDirectionAndUp(shadowDirection, VForward);

    // Get the shadow plane origin in world coordinates...
    Vector3 worldSPOrigin = position - shadowDirection * shadowPlaneOffset;

    // ...express it in LRSP space...
    Vector3 lrspSPOriginPos = _oldLRSP[layerIndex].InverseRotation().FastTransform(worldSPOrigin);

    // ...round it there and convert it back.
    Vector3 roundedLRSPSPOriginPos = RoundVector(lrspSPOriginPos, VZero, smTexelSize);

    // Save the results
    shadowPlaneOrigin.SetPosition(_oldLRSP[layerIndex].FastTransform(roundedLRSPSPOriginPos));
    shadowPlaneOrigin.SetOrientation(_oldLRSP[layerIndex].Orientation());
  }

  return shadowPlaneOrigin;
}

void EngineDD11::RenderStencilShadows()
{
#if _VBS3_SM2
  // In case of shader model 2 do nothing
  if (_caps._sm2Used) return;
#endif

  _pp._SSSMStencil->Do(true);

#ifdef SSSMBLUR
  // Create the SSSM blurred map
  _pp._SSSMB->Do(false);
#endif
}

void EngineDD11::CommitSettings()
{
  EndHighLevelRecording();
  EndBackgroundScope(true,"commit");

  EnableSBShadowsCommit();
  if (_textBank) _textBank->InitDetailTextures();
}

void EngineDD11::EnableDepthOfField(float focusDist, float blur, bool farOnly)
{
  // prevent focusing to zero
  if (focusDist<1e-2f) focusDist = 1e-2f;
  _focusDist = focusDist;
  _blurCoef = blur;
  _farOnly = farOnly;
}

void EngineDD11::EnableDepthOfField(int quality)
{
  // we currently support one quality only
  saturate(quality,0,1);
  // Quality hasn't changed, return
  if (quality == _dbQuality) return;
  // Remember the DOF quality
  _dbQuality = quality;

  // the depth buffer is always created anyway
}

bool EngineDD11::IsDepthOfFieldEnabled() const
{
  if (_blurCoef <= 0.001f) return false;
  if (_dbQuality==0) return false;
  // Engine can't use depth buffer, return
  if (!_dbAvailable) return false;
  return _renderTargetDepthInfo._rtView.NotNull();
}

void EngineDD11::EnableSBShadowsCommit()
{
#ifdef _XBOX
  _sbQuality = 0; // TODO: use SB on Xbox
#endif

#if _VBS3_SM2
  // In case of shader model 2 do nothing
  if (_caps._sm2Used) return;
#endif

  int quality = _sbQualityWanted;
  if (quality==_sbQuality) return;
  _sbQuality = quality;
  if (quality>0)
  {
    // avoid creating the surface in AGP memory
    // for this we need to make sure device VRAM is free
    // this is not needed on X360, which is UMA architecture, with no AGP
#ifndef _XBOX
    if (_textBank->GetVRamTexAllocation()>0 || _vBuffersAllocated>0)
    {
      /* we might even perform full or partial Reset() here
      PreReset(false);
      // PostReset will perform CreateSBResources();
      PostReset();
      */
      DisplayWaitScreen();

      FinishPresent();
      //UnsetAllDiscardableResources();
      _textBank->ReleaseAllTextures();
      ReleaseAllVertexBuffers();
      // pretend the reset was done - this will make sure preload is done
      _resetDone = true;
    }
#endif
    CreateSBResources();
  }
  else
  {
    DestroySBResources();
  }
  _textBank->ResetMaxTextureQuality(false);
}

void EngineDD11::InitSceneInvariants(const int cb)
{
  //   // Set the dithering coefficient upon MSAA level
  //   // Label: AlphaToCoverage
  //   float dc = 1.0f / 4.0f; // 5 levels of transparency
  // #if defined _XBOX && _XBOX_VER>=2
  //   switch (_rtMultiSampleType)
  //   {
  //   case D3DMULTISAMPLE_2_SAMPLES: // 9 levels of transparency
  //     dc = 1.0f / 8.0f;
  //     break;
  //   case D3DMULTISAMPLE_4_SAMPLES: // 17 levels of transparency
  //     dc = 1.0f / 16.0f;
  //     break;
  //   }
  // #endif
  //   Color ditherCoef(dc, dc, dc, dc);
  //   SetPixelShaderConstantF(PSC_DitherCoef, (float*)&ditherCoef, 1);

  // Influence density of trees by multiplying the final alpha
  Color alphaMult; // Empirical coefficient controlling density of trees
  if (_sbQuality>0)
  {
    alphaMult=Color(_sbSize, 1.0f/_sbSize, 0, 1.5f);
  }
  else
  {
    alphaMult=Color(1024.0f, 1.0f/1024.0f, 0, 1.5f); 
  }
  SetPixelShaderConstantF(-1, PSC_SBTSize_invSBTSize_X_AlphaMult, (float*)&alphaMult, 1);

  // pass video detail setting to pixel shaders so that they can statically branch based on them
  // int ShadingQualityAccess::_values[NValues]={0, 3, 7, 10, 100};
  bool detail[4]= {
    GScene ? GScene->GetShadingQuality()>=3 : true,
    GScene ? GScene->GetShadingQuality()>=7 : true,
    GScene ? GScene->GetShadingQuality()>=10 : true,
    GScene ? GScene->GetShadingQuality()>=100 : true,
  };
  SetPixelShaderConstantB(-1, PSC_ShadingDetail,detail,4);

  // Set inverse screen resolution
  Color screenRes(1.0f / _wRT, 1.0f / _hRT, 0.0f, 0.0f);
  SetPixelShaderConstantF(-1, PSC_InvW_InvH_X_X, (float*)&screenRes, 1);

#if _ARMA3_CAUSTICS
  // setup light transform camera
  if (GScene && GScene->DrawCaustics() && GScene->MainLight())
  {
    Matrix4 lightMatrix;
    //lightMatrix.SetDirectionAndUp(GScene->MainLight()->Direction(), VForward);
    //lightMatrix.SetPosition(Vector3(0,0,0));
    // LUKE TEST
    Vector3 direction(0,-1,0);
    lightMatrix.SetDirectionAndUp(direction, VForward);
    lightMatrix.SetPosition(Vector3(0,0,0));

    PASSMATRIXTYPE lightMatrixDX;
    ConvertMatrixTransposed(lightMatrixDX, lightMatrix.InverseGeneral());
    SetVertexShaderConstantF(-1,VSC_CausticsMatrix, (float*)&lightMatrixDX, 3);   
  }
#endif
}

void EngineDD11::SetRenderTargetAndDepthStencil(
  const RenderTarget& rt, const DepthStencil& dt, float minZ, float maxZ, bool optimize)
{
  // We cannot record this whole function, because _currRenderTarget is being used
  // in many subsequent functions (see CopyResource calls)

  // Lazy set of render target
  if (_currRenderTarget._rtView != rt._rtView || _currDepthStencil._dsView != dt._dsView)
  {
    PROFILE_DX_SCOPE_DETAIL(3dSRT);
    {
#if _ENABLE_REPORT
      D3DInfoQueueGuard guard(_d3dInfoQueue);
#endif
      CALL_D3D(OMSetRenderTargets, rt._rtView, dt._dsView);
    }

    // get current render target dimensions
    int curWidth = 0;
    int curHeight = 0;

    if (_currRenderTarget.NotNull())
      GetRenderTargetDimensions(_currRenderTarget, curWidth, curHeight);
    else if (_currDepthStencil.NotNull())
      GetDepthStencilDimensions(_currDepthStencil, curWidth, curHeight);

    // get new render target dimensions
    int newWidth = 0;
    int newHeight = 0;
    if (rt.NotNull())
      GetRenderTargetDimensions(rt, newWidth, newHeight);
    else if (dt.NotNull())
      GetDepthStencilDimensions(dt, newWidth, newHeight);

    // set viewport if needed
    if (newWidth != curWidth 
      || newHeight != curHeight 
      || _minZRange != minZ 
      || _maxZRange != maxZ)
    {
      SetViewport(newWidth, newHeight, minZ, maxZ);
    }   
  }
  else
  {
    // we must check if z changed, even if render targets are the same
    if (_minZRange != minZ || _maxZRange != maxZ)
    {
      int newWidth = 0;
      int newHeight = 0;
      if (rt.NotNull())
        GetRenderTargetDimensions(rt, newWidth, newHeight);
      else if (dt.NotNull())
        GetDepthStencilDimensions(dt, newWidth, newHeight);

      SetViewport(newWidth, newHeight, minZ, maxZ);
    }
  }

  _currRenderTarget = rt;
  _currDepthStencil = dt;

  // Check render target is not set as any texture now
#if _ENABLE_REPORT
  //Assert(!IsCBRecording());
  const int cb = -1;
  if (rt._resource) for (int i=0; i<lenof(CBState(cb)._lastHandle); i++)
  {
    ComRef<ID3D11Resource> res;
    if (CBState(cb)._lastHandle[i] && CBState(cb)._lastHandle[i] != (TextureHandle11*)-1)
      CBState(cb)._lastHandle[i]->GetResource(res.Init());

    DoAssert(res!=rt._resource)
  }
#endif
}

void EngineDD11::SetRenderTargetAndDepthStencilDirect(
  const RenderTarget& rt, const DepthStencil& dt, float minZ, float maxZ)
{
  PROFILE_DX_SCOPE_DETAIL(3dSRT);
  {
#if _ENABLE_REPORT
    D3DInfoQueueGuard guard(_d3dInfoQueue);
#endif
    _d3dDeviceRec->OMSetRenderTargets(rt._rtView, dt._dsView);
  }

  // get new render target dimensions
  int newWidth = 0;
  int newHeight = 0;
  if (rt.NotNull())
    GetRenderTargetDimensions(rt, newWidth, newHeight);

  // set viewport 
  D3D11_VIEWPORT viewData;
  ZeroMemory(&viewData, sizeof(viewData));
  viewData.TopLeftX = 0;
  viewData.TopLeftY = 0;
  viewData.Width = newWidth;
  viewData.Height = newHeight;
  viewData.MinDepth = minZ;
  viewData.MaxDepth = maxZ;
  _d3dDeviceRec->RSSetViewports(&viewData);

  // we do NOT want to remember rt and dt as current
}

void EngineDD11::PushRenderTargetDepthStencil()
{
  // we cannot record this command 
  _savedRenderTargetInfoQueue.Add(
    RenderTargetDepthStencilInfo(
    _currRenderTarget,
    _currDepthStencil,
    _minZRange,
    _maxZRange));
}


void EngineDD11::PopRenderTargetDepthStencil()
{
  // we cannot record this command 

  DoAssert(_savedRenderTargetInfoQueue.Size() > 0);
  if (_savedRenderTargetInfoQueue.Size() == 0)
    return;

  // get info from queue
  int index = _savedRenderTargetInfoQueue.Size()-1;
  RenderTargetDepthStencilInfo& info = _savedRenderTargetInfoQueue[index];

  // setup render target and depth/stencil
  SetRenderTargetAndDepthStencil(info._renderTarget, info._depthStencil,
    info._minZRange, info._maxZRange);

  // delete info from queue
  _savedRenderTargetInfoQueue.Delete(index);
}

void EngineDD11::BeginDepthMapRendering(bool zOnly
#if _HELISIM_RENDER_TO_TEXTURE
                                       , bool useR2T
#endif
                                       )
{
  PushRenderTargetDepthStencil();
  // Set depth buffer as temporary render target
  if (zOnly)
  {
    // we use this branch for depth priming when performing depth rendering before MSAA
    // this is done on PC only
#ifdef _XBOX // PRT_SHADOWS
    Fail("Not supported on Xbox");
#endif
    Assert(IsFSAAEnabled());

    // on PC with FSAA 2nd depth priming pass is z-only, no need to write color buffer
    _renderingMode = RMZPrime;

    // we want the DIP throttling to act the same way in depth/normal rendering
    _dipCount = 0;
    InitSceneInvariants(-1);

    // We're rendering prior the main scope, thus we need the BeginScene/EndScene scope
    // DX11 BeginScene is gone
    //CALL_D3D(BeginScene,);

#if _HELISIM_RENDER_TO_TEXTURE  
    if (useR2T)
      SetRenderTargetAndDepthStencil(_r2tRT._renderTargetS, _depthStencilS);
    else
#endif
    SetRenderTargetAndDepthStencil(RenderTarget::RenderTargetNULL, _depthStencilS);

    // Clear the color buffer (the float value), clear the Z buffer
    Clear(true, false, PackedColor(HBlack));
  }
  else
  {
    // Depth priming - rendering of a depth map to be used for SSSM shadows (may be z-priming scene at the same time)
    _renderingMode = RMDPrime;

    // we want the DIP throttling to act the same way in depth/normal rendering
    _dipCount = 0;
    InitSceneInvariants(-1);

    // We're rendering prior the main scope, thus we need the BeginScene/EndScene scope
    // DX11 no BeginScene()
    //CALL_D3D(BeginScene,);

#ifdef _XBOX // PRT_SHADOWS
    // we do not care about render target at this stage - we are not interested in it
    // however it seems alpha to mask is not working without it
    SetTemporaryRenderTarget(_renderTargetS, _renderTargetSurfaceS, _depthBufferRT);
#else
    // use depth target, but reuse the same depth buffer as the main scene
    if (IsFSAAEnabled())
    {
      SetRenderTargetAndDepthStencil(_renderTargetDepthInfo, _depthStencilDepthInfo);
    }
    else 
    {
      SetRenderTargetAndDepthStencil(_renderTargetDepthInfo,_depthStencilS);
    }
#endif

#ifdef _XBOX // PRT_SHADOWS
    // Start tiling - consider the first tile was cleared by the usual clear
    D3DVECTOR4 clearColor;
    clearColor.x = _clearColor.R();
    clearColor.y = _clearColor.G();
    clearColor.z = _clearColor.B();
    clearColor.w = _clearColor.A();

    D3DVECTOR4 clearColorBlack;
    clearColorBlack.x = 0;
    clearColorBlack.y = 0;
    clearColorBlack.z = 0;
    clearColorBlack.w = 1;

    if (_tilingRectsCount>1)
    {
      _d3DDevice->BeginTiling(D3DTILING_SKIP_FIRST_TILE_CLEAR, _tilingRectsCount, _tilingRects, &clearColor, 1.0f, 0L);
    }
    // Set the predicated pixel shader constants for screenspace offsets (required by SSSM maps)
    // See label GpuOwnPixelShaderConstantF
    _d3DDevice->GpuOwnPixelShaderConstantF(PSCGPU_TOffX_TOffY_X_X, 4);
    for (int i = 0; i < _tilingRectsCount; i++)
    {
      // Set predication to tile i.
      if (_tilingRectsCount>1) _d3DDevice->SetPredication( D3DPRED_TILE( i ) );

      // Request an allocation of pixel shader constants from the command buffer.
      XMVECTOR tilePos = XMVectorSet( (FLOAT)_tilingRects[i].x1, (FLOAT)_tilingRects[i].y1, 0, 0 );
      _d3DDevice->GpuPixelShaderConstantF4_1(PSCGPU_TOffX_TOffY_X_X, tilePos);
    }
    if (_tilingRectsCount>1) _d3DDevice->SetPredication( D3DPRED_TILE( 0 ) );


    //_d3DDevice->ClearF(D3DCLEAR_TARGET0 | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, &_tilingRects[0], &clearColor, 1.0f, 0L);
    _d3DDevice->ClearF(D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, &_tilingRects[0], &clearColor, 1.0f, 0L);
    if (_tilingRectsCount>1) _d3DDevice->SetPredication( 0 );
#else
    // WIP: FSAA: do not clear color buffer twice
    // Clear the color buffer (the float value), clear the Z buffer
    Clear(true, true, PackedColor(HBlack));
#endif
  }

  // mark as if we are rendering in linear space - this is needed because we are recording normal rendering at the same time
  _linearSpace = true;
#if _DEBUG || _PROFILE
  if (Glob.config._renderThread==Config::ThreadingEmulated)
  {
    KickOffCBDebug();
  }
#endif
}

void EngineDD11::EndDepthMapRendering(bool zOnly)
{
  if (zOnly)
  {
#ifdef _XBOX
    Fail("Not supported on Xbox");
#endif
    // We're rendering prior the main scope, thus we need the BeginScene/EndScene scope
    // DX11 no EndScene
    //CALL_D3D(EndScene,);
  }
  else
  {
#ifdef _XBOX
#if 1 // PRT_SHADOWS
    // Finish tiling - clear the resolved source to the desired color
    D3DVECTOR4 clearColor;
    clearColor.x = _clearColor.R();
    clearColor.y = _clearColor.G();
    clearColor.z = _clearColor.B();
    clearColor.w = _clearColor.A();

    // Resolve rendertarget into _renderTargetD
    for (int i = 0; i < _tilingRectsCount; i++)
    {
      // Set predication to tile i.
      if (_tilingRectsCount>1) _d3DDevice->SetPredication( D3DPRED_TILE( i ) );

      // Destination point is the upper left corner of the tiling rect.
      D3DPOINT* pDestPoint = (D3DPOINT*)&_tilingRects[i];

      // Resolve render target into _renderTargetD, clear the source tile to the clear values
      //_d3DDevice->Resolve(D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS|D3DRESOLVE_CLEARRENDERTARGET, &_tilingRects[i], _renderTargetDepthInfo, pDestPoint, 0, 0, &clearColor, 1.0f, 0L, NULL);
      // depth resolve can resolve 1 fragment only
      // this suits us well, as we want to use centroid sampling to handle triangle edges anyway
      CALL_D3D(Resolve,D3DRESOLVE_DEPTHSTENCIL|D3DRESOLVE_FRAGMENT0, &_tilingRects[i], _renderTargetDepthInfo, pDestPoint, 0, 0, &clearColor, 1.0f, 0L, NULL);
    }

    // while still in the tiling, we need to render stencil shadows

    // Restore predication to default.
    if (_tilingRectsCount>1) CALL_D3D(SetPredication,0);

    // Release ownership of the screen-space offset shader constants. See label GpuOwnPixelShaderConstantF
    CALL_D3D(GpuDisownAll,);

    //RenderStencilShadows();

    // Finish tiling
    if (_tilingRectsCount>1)
    {
      CALL_D3D(EndTiling,0, NULL, NULL, NULL, 1.0f, 0L, NULL);
    }


#else
    // Resolve content of the depth buffer texture to _renderTargetDepthInfo texture
    CALL_D3D(Resolve,D3DRESOLVE_DEPTHSTENCIL|D3DRESOLVE_ALLFRAGMENTS, NULL, _renderTargetDepthInfo, NULL, 0, 0, NULL, 1.0f, 0L, NULL);
#endif

#endif

    // We're rendering prior the main scope, thus we need the BeginScene/EndScene scope
    // DX11 no EndScene
    //CALL_D3D(EndScene,);
  }

#if _HELISIM_RENDER_TO_TEXTURE
  if (_r2tBegin)
    CreateR2TDepthAsTexture();
#endif


  // Restore the old render target
  PopRenderTargetDepthStencil();

  // Set the SB rendering flag
  _renderingMode = RMCommon;

#if _DEBUG || _PROFILE
  if (Glob.config._renderThread==Config::ThreadingEmulated)
  {
    KickOffCBDebug();
  }
#endif
}

void EngineDD11::SetDepthClip(float zClip)
{
  Assert(!IsInCBScope());
  SetPixelShaderConstantF(-1,PSC_DepthClip, D3DXVECTOR4(1.0f, 0.0f, 0.0f, zClip), 1);
}

void EngineDD11::SetDepthRange(float minZ, float maxZ)
{
  _minZRange = minZ;
  _maxZRange = maxZ;
  // unless overridden by a cheat, _viewW == _wRT, _viewH == _hRT
  SetViewport(_viewW, _viewH, _minZRange, _maxZRange);
}

void EngineDD11::SetViewport(int width, int height, float minZ, float maxZ)
{
  _minZRange = minZ;
  _maxZRange = maxZ;

  D3D11_VIEWPORT viewData;
  ZeroMemory(&viewData, sizeof(viewData));
  viewData.TopLeftX = 0;
  viewData.TopLeftY = 0;
  viewData.Width = width;
  viewData.Height = height;
  viewData.MinDepth = _minZRange;
  viewData.MaxDepth = _maxZRange;
  CALL_D3D(RSSetViewports, &viewData);
}

void EngineDD11::SetViewportDirect(int x, int y, int width, int height, float minZ, float maxZ)
{
  _minZRange = minZ;
  _maxZRange = maxZ;

  D3D11_VIEWPORT viewData;
  ZeroMemory(&viewData, sizeof(viewData));
  viewData.TopLeftX = x;
  viewData.TopLeftY = y;
  viewData.Width = width;
  viewData.Height = height;
  viewData.MinDepth = _minZRange;
  viewData.MaxDepth = _maxZRange;
  _d3dDeviceRec->RSSetViewports(&viewData);
}

void EngineDD11::SetTemperatureTable(const float *table, int gSize, int dotSize, int kSize)
{
  int tableSize = gSize*dotSize*kSize;
  if (!TemperatureTableUpdateNeeded(table, tableSize)) return;

  Assert(_tableTcSystem._texture);
  if (!_tableTcSystem._texture)
    return;

  D3D11_MAPPED_SUBRESOURCE mapSub;
  HRESULT hr = _d3dDeviceRec->Map(_tableTcSystem._texture, 0, D3D11_MAP_WRITE, 0, &mapSub);
  if (SUCCEEDED(hr) && mapSub.pData)
  {
    _temperatureTable.Realloc(tableSize);
    _temperatureTable.Resize(tableSize);

    int index = 0;
    for (int j = 0; j < gSize*dotSize; j++)
    {
      float *data = (float*)&(((unsigned char*)mapSub.pData)[mapSub.RowPitch * j]);
      for (int i = 0; i < kSize; i++)
      {
        data[i] = table[i + j * kSize];
        _temperatureTable[index++] = data[i];
      }
    }

    _d3dDeviceRec->Unmap(_tableTcSystem._texture, 0);
    _d3dDeviceRec->CopyResource(_tableTc._texture, _tableTcSystem._texture);

    // Save the temperature of the air (UnlitWhite value)
    _airTemperature = table[0 + 2 * kSize];
  }

}

bool EngineDD11::TemperatureTableUpdateNeeded(const float *table, int length)
{
  if (_temperatureTable.Size() == 0) return true;

  for (int i = 0; i < length; i++)
  {
    if (fabsf(table[i] - _temperatureTable[i]) > 0.001)
    {
      return true;
    }
  }

  return false;
}

void EngineDD11::SetTIBrightnessRelative(float brightness)
{
  // Consider black-hot case and decrease brightness instead
  if (_tiBlackHot[GetTIMode()])
  {
    brightness = 2.0f * GetTIBrightness() - brightness;
  }

  // Set the TI brightness
  SetTIBrightness(brightness);
}

void EngineDD11::LoadTIConversionTexture(RString name)
{
  // Remember the texture name
  _tiConversionName = name;

  // Name is not valid, finish
  if (name.IsEmpty())
  {
    RptF("Error: Name of the TIConversion texture was not specified");
    return;
  }

  // LUKE HACK
  // DX11 TODO - TGA not supported, add texture to core/data directory!
  _tiConversionName = name.Substring(0, name.GetLength()-4) + ".png";


  // Open stream and load data
  QOStrStreamData textureSource(_tiConversionName);

  // load texture
  D3DX11_IMAGE_LOAD_INFO loadInfo;
  loadInfo.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  loadInfo.Usage = D3D11_USAGE_STAGING;
  loadInfo.CpuAccessFlags = D3D11_CPU_ACCESS_READ;   
  loadInfo.MipLevels = 1;
  loadInfo.BindFlags = 0;

  HRESULT hr = D3DX11CreateTextureFromMemory(_d3dDeviceRec->GetDeviceRef(), 
    textureSource.str(), textureSource.pcount(),  &loadInfo, NULL, _tiConversionSystem.Init(), NULL);

  if (FAILED(hr))
  {
    RptF("Error: load surface from memory failed");
    return;
  }

  // Check the dimension to match the expected one
  if ((loadInfo.Width != TIConversionDimensionW) || (loadInfo.Height != TIConversionDimensionH))
  {
    RptF("Warning: TIConversion texture dimension (%dx%d) is not expected, it should be (%dx%d)", loadInfo.Width, loadInfo.Height, TIConversionDimensionW, TIConversionDimensionH);
  }

  // Determine the black-hot rows (so that TI brightness adjusting can work correctly)
  {
    D3D11_MAPPED_SUBRESOURCE mappedSub;
    hr = _d3dDeviceRec->Map(_tiConversionSystem, 0, D3D11_MAP_READ, 0, &mappedSub);
    // Go through all the rows, if the last texel on a row is darker than the first texel, we've got a black-hot
    if (SUCCEEDED(hr) && mappedSub.pData)
    {
      for (int i = 0; i < TIConversionDimensionH; i++)
      {
        const DWORD *row = (const DWORD *)(&((char*)mappedSub.pData)[i * mappedSub.RowPitch]);
        Color first = (PackedColor)row[0];
        Color last = (PackedColor)row[TIConversionDimensionW - 1];
        _tiBlackHot[i] = (first.Brightness() <= last.Brightness()) ? false : true;
      }
      _d3dDeviceRec->Unmap(_tiConversionSystem, 0);
    }
    else
    {
      RptF("Error: TI conversion surface lock failed");
      return;
    }
  }

  // Copy texture
  _d3dDeviceRec->CopyResource(_tiConversion._texture, _tiConversionSystem);
}

void EngineDD11::UseCraters(int cb, const AutoArray<CraterProperties> &craters, int maxCraters, const Color &craterColor)
{
  // Get the actual number of craters to draw
  int cratersCount = intMin(craters.Size(), maxCraters);

  // Create array of craters (fill out rest with zero)
  DoAssert(cratersCount <= MAX_CRATERS);
  Color cr[MAX_CRATERS];

  float visiblity[MAX_CRATERS][4];
  const int TimeToCoolInSec = 60 * 1000;
  const float InvTimeToCoolInSec = 1.0f / TimeToCoolInSec;

  for (int i = 0; i < cratersCount; i++)
  {
    // Calculate the disappear coefficient (number between 0 and 1). Note we know it is never zero
    float disappearCoef = floatMin(2.0f * float(i + 1 + maxCraters - cratersCount) / maxCraters, 1.0f);
    float invDisappearCoef2 = 1.0f / (disappearCoef * disappearCoef);

    // Fill out the position and scaled size
    int itemIndex = craters.Size() - cratersCount + i;
    cr[i] = Color(craters[itemIndex]._pos.X(), craters[itemIndex]._pos.Y(), craters[itemIndex]._pos.Z(), craters[itemIndex]._invRadius2 * invDisappearCoef2);
    int dT = Glob.time.toInt() - craters[itemIndex]._time;
    visiblity[i][0] = (dT < TimeToCoolInSec) ? (1.0f - dT * InvTimeToCoolInSec) : 0.0f;
  }

  // Set the craters
  SetPixelShaderConstantF(cb,PSC_Crater, (float *)cr, cratersCount);
  SetPixelShaderConstantF(cb, PSC_CraterTemp, (float*)visiblity, cratersCount);

  Color lighting(1.0f, 1.0f, 1.0f, 1.0f);
  if (!GetThermalVision())
  {
    // Pass on light color multiplied by crater color
    LightSun *sun = GScene->MainLight();
    lighting = sun->SimulateDiffuse(0.5) * GetAccomodateEye() * _modColor * craterColor;
    lighting.SetA(craterColor.A());
  }
  SetPixelShaderConstantF(cb,PSC_GlassEnvColor, (float*)&lighting, 1);
}

void EngineDD11::DrawSectionTL(int cb, const Shape &sMesh, int beg, int end, int bias, int instancesOffset, int instancesCount, const DrawParameters &dp)
{
  PROFILE_DX_SCOPE(ddDST);
  if (ResetNeeded()) return;

  if (LimitSecCount>=0 && end>=LimitSecCount)
  {
    if (beg>=LimitSecCount)
    {
      return;
    }
    end = LimitSecCount;
  }

  RendStateExt &cbState = CBState(cb);
  // Set bias specified for this section
  SetBias(cb, bias);


  // Determine we use instancing
  const bool instancing = instancesCount>=1;

  // as a result of material animation, we may be requested to render an impossible combination
  // if we detect this, we will use minimal shader
  int vertexDeclarationMaskRequested = VertexDeclarations[ShaderVertexDeclarations[cbState._vertexShaderID]];
  int vertexDeclarationMaskUsed = VertexDeclarations[sMesh.GetVertexDeclaration()];
  if ((vertexDeclarationMaskRequested&vertexDeclarationMaskUsed)!=vertexDeclarationMaskRequested)
  {
    // fallback - use normal shader
    cbState._vertexShaderID = VSBasic;
    // we need to use normal pixel shader as well
    cbState._pixelShaderSel = PSNormal;
  }



  // ---------------------------------------------
  // Shader setting ------------------------------

  // Identify the shader to be shadow receiver or not
  bool shadowReceiver = ((cbState._pixelShaderTypeSel == PSTBasicSSSM) || (cbState._pixelShaderTypeSel == PSTShadowReceiver1));

#if 0
  if (cbState._vertexShaderID==VSSkin || cbState._vertexShaderID==VSSuper
#if _ARMA3_REFRACTION
    || cbState._vertexShaderID == VSRefract
#endif       
    )
  {
    // verify the declaration contains the ST for some specific shader types
    EVertexDecl decl = sMesh.GetVertexDeclaration();
    Assert(decl==VD_Position_Normal_Tex0_ST 
      || decl==VD_Position_Normal_Tex0_Tex1_ST
      || decl==VD_Position_Normal_Tex0_ST_WeightsAndIndices
      || decl==VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices);
    // 
  }
#endif
  // Get minBoneIndex and bonesCount of this section
  // Note that it is OK to use just the "beg" section, because we're sure all other sections have the same parameters
  const ShapeSection &sec = sMesh.GetSection(beg);

  // For VS located in the pool set the VS constants instantly
  SetupVSConstants(cb, sec._minBoneIndex, sec._bonesCount);

  // Setup PS constants
  SetupPSConstants(cb);

  SetupSectionTL(
    cb, cbState._mainLight, cbState._fogMode,
    cbState._vertexShaderID, cbState._uvSource, cbState._uvSourceCount, cbState._uvSourceOnlyTex0,
    cbState._skinningType,
    instancing, instancesCount, cbState._pointLightsCount, cbState._spotLightsCount,
    shadowReceiver, _renderingMode, cbState._backCullDisabled, dp._cratersCount, sec.GetMaterialExt()
    );

  PredicationMode pass = CurrentPredicationMode();
  // note: when VS was set to NULL in some predication pass, it means the primitive should not be rendered
  // in that pass at all - this is common for crater rendering and some other special cases
  Assert (!_bgRecPredication || cbState._vertexShaderLast[RenderingPass]!=NULL);

  int vsMask = 0;
  const int vsMaskFull = (1<<NPredicationModes)-1;
  for (int i=0; i<NPredicationModes; i++)
  {
    if (cbState._vertexShaderLast[i]) vsMask |= 1<<i;

  }

  Assert(dp._cratersCount==0 || vsMask==(1<<RenderingPass)); // craters should have rendering VS only

  if (vsMask!=vsMaskFull)
  {
    if (BeginPredicationMask(cb,vsMask,pass))
    {
      DrawIndexedPrimitive(cb,beg, end, sMesh, instancesOffset, instancesCount);
      EndPredication(cb);
    }
  }
  else
  {
    DrawIndexedPrimitive(cb,beg, end, sMesh, instancesOffset, instancesCount);
  }
}

void EngineDD11::DrawStencilShadows(bool interior, int quality)
{
}

void EngineDD11::Draw2D(const Draw2DParsExt &pars, const Rect2DAbs &rect, const Rect2DAbs &clip)
{
  if (_backgroundD3D==RecHighLevel)
  {
    RecordHighlevel()->Draw2D(pars,rect,clip);
  }
  else
  {
    DoDraw2D(pars,rect,clip);
  }
}

void EngineDD11::DoDraw2D(const Draw2DParsExt &pars, const Rect2DAbs &rect, const Rect2DAbs &clip)
{
  Assert( pars.mip.IsOK() );
  if( !pars.mip.IsOK() ) return;

  if (ResetNeeded()) return;

  // cannot render zero area
  if (rect.w<=0 || rect.h<=0) return;

#if _DEBUG
  float scale = fabs(pars.vTR)+fabs(pars.vBL)+fabs(pars.vTL)+fabs(pars.vBR);
  Assert( fabs(pars.vTR+pars.vBL-pars.vTL-pars.vBR)<scale*1e-6f);
  Assert( fabs(pars.uTR+pars.uBL-pars.uTL-pars.uBR)<scale*1e-6f);
#endif

  // perform simple clipping
  float xBeg,yBeg,xEnd,yEnd,uBeg,vBeg,uEnd,vEnd;
  bool clipped = ClipDraw2D(xBeg,yBeg,xEnd,yEnd,uBeg,vBeg,uEnd,vEnd,pars,rect,clip);

  if( xBeg>=xEnd || yBeg>=yEnd ) return;

  // note: colors should be "clipped" as well
  TLVertex pos[4];
  pos[0].rhw=1;
  pos[0].color=pars.colorTL;
  pos[0].specular=PackedColor(0xff000000);
  pos[0].pos[2]=0.5;

  pos[1].rhw=1;
  pos[1].color=pars.colorTR;
  pos[1].specular=PackedColor(0xff000000);
  pos[1].pos[2]=0.5;

  pos[2].rhw=1;
  pos[2].color=pars.colorBR;
  pos[2].specular=PackedColor(0xff000000);
  pos[2].pos[2]=0.5;

  pos[3].rhw=1;
  pos[3].color=pars.colorBL;
  pos[3].specular=PackedColor(0xff000000);
  pos[3].pos[2]=0.5;

  if (!clipped)
  {
    // optimized common version
    pos[0].t0.u = pars.uTL;
    pos[1].t0.u = pars.uTR;
    pos[3].t0.u = pars.uBL;
    pos[2].t0.u = pars.uBR;

    pos[0].t0.v = pars.vTL;
    pos[1].t0.v = pars.vTR;
    pos[3].t0.v = pars.vBL;
    pos[2].t0.v = pars.vBR;
  }
  else
  {
    pos[0].t0.u = pars.uTL+uBeg*(pars.uTR-pars.uTL)+vBeg*(pars.uBL-pars.uTL);
    pos[1].t0.u = pars.uTL+uEnd*(pars.uTR-pars.uTL)+vBeg*(pars.uBL-pars.uTL);
    pos[3].t0.u = pars.uTL+uBeg*(pars.uTR-pars.uTL)+vEnd*(pars.uBL-pars.uTL);
    pos[2].t0.u = pars.uTL+uEnd*(pars.uTR-pars.uTL)+vEnd*(pars.uBL-pars.uTL);

    pos[0].t0.v = pars.vTL+uBeg*(pars.vTR-pars.vTL)+vBeg*(pars.vBL-pars.vTL);
    pos[1].t0.v = pars.vTL+uEnd*(pars.vTR-pars.vTL)+vBeg*(pars.vBL-pars.vTL);
    pos[3].t0.v = pars.vTL+uBeg*(pars.vTR-pars.vTL)+vEnd*(pars.vBL-pars.vTL);
    pos[2].t0.v = pars.vTL+uEnd*(pars.vTR-pars.vTL)+vEnd*(pars.vBL-pars.vTL);
  }



  pos[0].pos[0]=xBeg,pos[0].pos[1]=yBeg;
  pos[1].pos[0]=xEnd,pos[1].pos[1]=yBeg;
  pos[2].pos[0]=xEnd,pos[2].pos[1]=yEnd;
  pos[3].pos[0]=xBeg,pos[3].pos[1]=yEnd;

  // Zero vertex and index buffer in the device (because the actual ones can be locked in AddVertices which is a problem at least on XBOX).
  // Also, it is better to do that before SwitchTL, as it can hypothetically free last instance of the buffer that is set (which is bad as well).
  ForgetDeviceBuffers(-1);

  SwitchRenderMode(RM2DTris);
  Assert(!IsCBRecording());
  SwitchTL(-1,TLDisabled);

  AddVertices(pos,4); // note: may flush all queues

  QueuePrepareTriangle(pars.mip,GlobPreloadMaterial(NonTLMaterial),pars.spec);

  Queue2DPoly(pos,4);
}

void EngineDD11::Draw2D(const Draw2DPars &pars, const Rect2DAbs &rect, const Rect2DAbs &clip)
{
  // TODO: special case optimization
  Draw2D(Draw2DParsExt(pars),rect,clip);
}
void EngineDD11::Draw2D(const Draw2DParsNoTex &pars, const Rect2DAbs &rect, const Rect2DAbs &clip)
{
  // TODO: special case optimization
  Draw2D(Draw2DParsExt(pars),rect,clip);
}

inline bool EngineDD11::ClipDraw2D
(
 float &xBeg, float &yBeg, float &xEnd, float &yEnd,
 float &uBeg, float &vBeg, float &uEnd, float &vEnd,
 const Draw2DParsExt &pars, const Rect2DAbs &rect, const Rect2DAbs &clip
 )
{
  // perform simple clipping
  xBeg=rect.x;//-0.5f; DX11 - no shift needed (see Texture Coordinate Systems (Direct3D 10)
  xEnd=xBeg+rect.w;
  yBeg=rect.y;//-0.5f; DX11 - no shift needed (see Texture Coordinate Systems (Direct3D 10)
  yEnd=yBeg+rect.h;

  uBeg=0;
  vBeg=0;
  uEnd=1;
  vEnd=1;

  // veirfy mapping is linear
  //Assert( fabs(pars.vTL+uEnd*(pars.vTR-pars.vTL)+vEnd*(pars.vBL-pars.vTL)-pars.vBR)<1e-10);
  //Assert( fabs(pars.uTL+uEnd*(pars.uTR-pars.uTL)+vEnd*(pars.uBL-pars.uTL)-pars.uBR)<1e-10);

  float xc=floatMax(clip.x,0);
  float yc=floatMax(clip.y,0);
  float xec=floatMin(clip.x+clip.w,_w);
  float yec=floatMin(clip.y+clip.h,_h);

  bool clipped = false;
  if( xBeg<xc )
  {
    // -xBeg is out, side length is 2*sizeX
    uBeg=(xc-xBeg)/rect.w;
    xBeg=xc;
    clipped = true;
  }
  if( xEnd>xec )
  {
    // xEnd-_w is out, side length is 2*sizeX
    uEnd=1-(xEnd-xec)/rect.w;
    xEnd=xec;
    clipped = true;
  }
  if( yBeg<yc )
  {
    // -yBeg is out, side length is 2*sizeY
    vBeg=(yc-yBeg)/rect.h;
    yBeg=yc;
    clipped = true;
  }
  if( yEnd>yec )
  {
    // yEnd-_h is out, side length is 2*sizeY
    vEnd=1-(yEnd-yec)/rect.h;
    yEnd=yec;
    clipped = true;
  }
  return clipped;
}

bool EngineDD11::AddVertices( const TLVertex *v, int n )
{
  if( n<=0 ) return true;

  DoAssert(NotInRecording());

  // keep meshbuffer allocated as long as possible
  // fill vertex buffer with vertices
  HRESULT err;

  void *data = NULL;

  int size=sizeof(TLVertex)*n;

#if 1
  int lockOffset = 0;
  D3D11_MAP lockFlags = (D3D11_MAP)0;
  ComRef<IDirect3DVertexBuffer9Old> buf;
  // check if we fit within the current VB
  if (_queueNo._vertexBufferUsed+n<=MeshBufferLength && !_queueNo._firstVertex)
  {
    lockOffset = _queueNo._vertexBufferUsed*sizeof(TLVertex);
    lockFlags = D3D11_MAP_WRITE_NO_OVERWRITE;
#if _ARMA3_VERTEX_BUFFER_EX
    buf = _queueNo.GetVBuffer().GetResult().GetBuffer();  // we know this Future is ready
#else
    buf = _queueNo.GetVBuffer().GetResult();  // we know this Future is ready
#endif

    err = LockDynBuffer(buf, data, lockFlags, lockOffset, size);
  }
  else
#endif
  {
    // when we do not fit (does not happen very often, start a new vertex buffer)
    if (n>MeshBufferLength)
    {
      RptF("Vertex Buffer too small (%d<%d)",n,MeshBufferLength);
      return false;
    }
    _queueNo._firstVertex = false;

    FlushAllQueues();

    // Zero vertex and index buffer in the device, forget _iBufferLast and _vBufferLast.
    ForgetDeviceBuffers(-1);

    // DX11 TODO - ?
    lockFlags = D3D11_MAP_WRITE_DISCARD;
#if CUSTOM_VB_DISCARD
    if (++_queueNo._actualVBIndex>=NumDynamicVB2D)
    {
      _queueNo._actualVBIndex = 0;
    }
#else
    lockFlags = D3D11_MAP_WRITE_DISCARD;
#endif
    _queueNo._vertexBufferUsed = 0;
#if _ARMA3_VERTEX_BUFFER_EX
    buf = _queueNo.GetVBuffer().GetResult().GetBuffer();  // we know this Future is ready
#else
    buf = _queueNo.GetVBuffer().GetResult();  // we know this Future is ready
#endif
    PROFILE_SCOPE_DETAIL_EX(lbDsc,dd11);
    err = LockDynBuffer(buf, data, lockFlags, lockOffset, size);

  }

  if( err!=D3D_OK )
  {
    return false;
  }
  else
  {

    if (data)
    {
      // Precalculate w and h coefs
      float doubleInvW = 2.0f / _w;
      float doubleInvH = 2.0f / _h;

      // memcpy not possible if we want to apply factor 0.5
      // if would be more efficient to apply it sooner,
      // but for this we probably need to change Engine interface
      // DX11 - data are always mapped from offset 0, so we have to add lockOffset
      TLVertex *vData = (TLVertex *)((unsigned char*)data + lockOffset);
      for (int i=0; i<n; i++)
      {
        const TLVertex &s = v[i];
        TLVertex &d = vData[i];
        d.pos = Vector3P(s.pos.X() * doubleInvW - 1.0f, -(s.pos.Y() * doubleInvH - 1.0f), s.pos.Z());
        d.rhw = s.rhw;
        d.color = ColorScale(s.color);
        d.specular = ColorScale(s.specular);
        d.t0 = s.t0;
        d.t1 = s.t1;
      }
      //memcpy(data,v,size);
    }
    Unmap(buf, 0);
    //if( err!=D3D_OK )  DDError11("Cannot unlock vertex buffer.",err);
  }
  _queueNo._meshBase = _queueNo._vertexBufferUsed;
  _queueNo._meshSize = n;
  _queueNo._vertexBufferUsed += n;
  return true;
}

void EngineDD11::QueuePrepareTriangle( const MipInfo &absMip, const TexMaterial *mat, int specFlags )
{
  TextureD3D11 *tex = static_cast<TextureD3D11 *>(absMip._texture);
  int level = absMip._level;
  AllocateQueue(tex,level,mat,specFlags);
  Assert (_queueNo._triUsed);
}

void EngineDD11::DrawPolyPrepare(const MipInfo &mip, int specFlags)
{
  if (_backgroundD3D!=RecHighLevel) DoDrawPolyPrepare(mip,specFlags);
  else RecordHighlevel()->DrawPolyPrepare(mip,specFlags);
}

void EngineDD11::DoDrawPolyPrepare(const MipInfo &mip, int specFlags)
{
  if (ResetNeeded()) return;

  SwitchRenderMode(RM2DTris);
  Assert(!IsCBRecording());
  SwitchTL(-1,TLDisabled);
  //FlushAllQueues(_queueNo);
  //AddVertices(gv,n); // note: may flush all queues
  QueuePrepareTriangle(mip,GlobPreloadMaterial(NonTLMaterial),specFlags);
}

void EngineDD11::DrawLinePrepare()
{
  // use line texture
  Texture *tex = GPreloadedTextures.New(TextureLine);
  const MipInfo &mip = _textBank->UseMipmap(tex,1,1);
  int specFlags = NoZBuf|IsAlpha|ClampU|ClampV|IsAlphaFog;

  DrawPolyPrepare(mip,specFlags);
}

void EngineDD11::DrawPolyDo(const Vertex2DPixel *vertices, int n, const Rect2DPixel &clipRect)
{
  if (_backgroundD3D!=RecHighLevel) DoDrawPolyDo(vertices,n,clipRect);
  else
  {
    // no intention to write to vertices, but Array is not const aware
    Array<Vertex2DPixel> temp(unconst_cast(vertices),n);
    RecordHighlevel()->DrawPolyPixelDo(temp,clipRect);
  }
}

void EngineDD11::DrawPolyDo(const Vertex2DAbs *vertices, int n, const Rect2DAbs &clipRect)
{
  if (_backgroundD3D!=RecHighLevel) DoDrawPolyDo(vertices,n,clipRect);
  else
  {
    // no intention to write to vertices, but Array is not const aware
    Array<Vertex2DAbs> temp(unconst_cast(vertices),n);
    RecordHighlevel()->DrawPolyAbsDo(temp,clipRect);
  }
}
void EngineDD11::DoDrawPolyDo(const Vertex2DPixel *vertices, int n, const Rect2DPixel &clipRect)
{
  if (ResetNeeded()) return;

  const int maxN=32;

  // reject poly if fully outside or invalid
  ClipFlags orClip=0;
  ClipFlags andClip=ClipAll;
  ClipFlags clipV[maxN];
  for( int i=0; i<n; i++ )
  {
    const Vertex2DPixel &vs=vertices[i];
    float x=vs.x;
    float y=vs.y;
    ClipFlags clip=0;
    if( x<clipRect.x ) clip|=ClipLeft;
    else if( x>clipRect.x+clipRect.w ) clip|=ClipRight;
    if( y<clipRect.y ) clip|=ClipTop;
    else if( y>clipRect.y+clipRect.h ) clip|=ClipBottom;
    clipV[i]=clip;
    orClip|=clip;
    andClip&=clip;
  }
  if( andClip ) return;
  // 2D clipping (with orClip flags)
  TLVertex gv[maxN];
  float x2d = Left2D();
  float y2d = Top2D();
  if( orClip )
  {
    Vertex2DPixel clippedVertices1[maxN]; // temporay buffer to keep clipped result
    Vertex2DPixel clippedVertices2[maxN]; // temporay buffer to keep clipped result

    Vertex2DPixel *free=clippedVertices1;
    Vertex2DPixel *used=clippedVertices2;
    // perform clipping
    for( int i=0; i<n; i++ ) used[i]=vertices[i];
    if( orClip&ClipTop ) n=Clip2D(clipRect,free,used,n,InsideTopPixel,InterpolateVertexPixel),swap(free,used);
    if( orClip&ClipBottom ) n=Clip2D(clipRect,free,used,n,InsideBottomPixel,InterpolateVertexPixel),swap(free,used);
    if( orClip&ClipLeft ) n=Clip2D(clipRect,free,used,n,InsideLeftPixel,InterpolateVertexPixel),swap(free,used);
    if( orClip&ClipRight ) n=Clip2D(clipRect,free,used,n,InsideRightPixel,InterpolateVertexPixel),swap(free,used);
    // use result
    if (n<3) return; // nothing to draw
    vertices=used;

    if( n>maxN )
    {
      n=maxN;
      Fail("Poly: Too much vertices");
    }

    for( int i=0; i<n; i++ )
    {
      TLVertex *v=&gv[i];
      const Vertex2DPixel &vs=vertices[i];

      v->pos[0]=vs.x+x2d;
      v->pos[1]=vs.y+y2d;
      v->pos[2]=vs.z;
      v->rhw=vs.w;
      v->color=vs.color;
      v->specular=PackedColor(0xff000000);
      //v->fog=0;
      v->t0.u=vs.u;
      v->t0.v=vs.v;
      // tmu1vtx?
    }
  }
  else
  {
    if( n>maxN )
    {
      n=maxN;
      Fail("Poly: Too much vertices");
    }

    for( int i=0; i<n; i++ )
    {
      TLVertex *v=&gv[i];
      const Vertex2DPixel &vs=vertices[i];

      v->pos[0]=vs.x+x2d;
      v->pos[1]=vs.y+y2d;
      v->pos[2]=vs.z;
      v->rhw=vs.w;
      v->color=vs.color;
      v->specular=PackedColor(0xff000000);
      //v->fog=0;
      v->t0.u=vs.u;
      v->t0.v=vs.v;
      // tmu1vtx?
    }
  }

  // Zero vertex and index buffer in the device, forget _iBufferLast and _vBufferLast.
  ForgetDeviceBuffers(-1);

  AddVertices(gv,n); // note: may flush all queues - but it must not free them - can be dangerous?
  Queue2DPoly(gv,n);
}

void EngineDD11::DoDrawPolyDo(const Vertex2DAbs *vertices, int n, const Rect2DAbs &clipRect)
{
  if (ResetNeeded()) return;

  const int maxN=32;

  // reject poly if fully outside or invalid
  ClipFlags orClip=0;
  ClipFlags andClip=ClipAll;
  ClipFlags clipV[maxN];
  for( int i=0; i<n; i++ )
  {
    const Vertex2DAbs &vs=vertices[i];
    float x=vs.x;
    float y=vs.y;
    ClipFlags clip=0;
    if( x<clipRect.x ) clip|=ClipLeft;
    else if( x>clipRect.x+clipRect.w ) clip|=ClipRight;
    if( y<clipRect.y ) clip|=ClipTop;
    else if( y>clipRect.y+clipRect.h ) clip|=ClipBottom;
    clipV[i]=clip;
    orClip|=clip;
    andClip&=clip;
  }
  if( andClip ) return;
  // 2D clipping (with orClip flags)
  TLVertex gv[maxN];
  if( orClip )
  {
    Vertex2DAbs clippedVertices1[maxN]; // temporay buffer to keep clipped result
    Vertex2DAbs clippedVertices2[maxN]; // temporay buffer to keep clipped result

    Vertex2DAbs *free=clippedVertices1;
    Vertex2DAbs *used=clippedVertices2;
    // perform clipping
    for( int i=0; i<n; i++ ) used[i]=vertices[i];
    if( orClip&ClipTop ) n=Clip2D(clipRect,free,used,n,InsideTopAbs,InterpolateVertexAbs),swap(free,used);
    if( orClip&ClipBottom ) n=Clip2D(clipRect,free,used,n,InsideBottomAbs,InterpolateVertexAbs),swap(free,used);
    if( orClip&ClipLeft ) n=Clip2D(clipRect,free,used,n,InsideLeftAbs,InterpolateVertexAbs),swap(free,used);
    if( orClip&ClipRight ) n=Clip2D(clipRect,free,used,n,InsideRightAbs,InterpolateVertexAbs),swap(free,used);
    // use result
    if (n<3) return; // nothing to draw
    vertices=used;

    if( n>maxN )
    {
      n=maxN;
      Fail("Poly: Too much vertices");
    }


    for( int i=0; i<n; i++ )
    {
      TLVertex *v=&gv[i];
      const Vertex2DAbs &vs=vertices[i];

      v->pos[0]=vs.x, v->pos[1]=vs.y, v->pos[2]=vs.z;
      v->rhw=vs.w;
      v->color=vs.color;
      v->specular=PackedColor(0xff000000);
      v->t0.u=vs.u;
      v->t0.v=vs.v;
    }
  }
  else
  {
    if( n>maxN )
    {
      n=maxN;
      Fail("Poly: Too much vertices");
    }


    for( int i=0; i<n; i++ )
    {
      TLVertex *v=&gv[i];
      const Vertex2DAbs &vs=vertices[i];

      v->pos[0]=vs.x, v->pos[1]=vs.y, v->pos[2]=vs.z;
      v->rhw=vs.w;
      v->color=vs.color;
      v->specular=PackedColor(0xff000000);
      v->t0.u=vs.u;
      v->t0.v=vs.v;
    }
  }

  // Zero vertex and index buffer in the device, forget _iBufferLast and _vBufferLast.
  ForgetDeviceBuffers(-1);

  AddVertices(gv,n); // note: may flush all queues, but it cannot free them
  Queue2DPoly(gv,n);
}

void EngineDD11::DrawLineDo(const Line2DAbs &line, PackedColor c0, PackedColor c1, const Rect2DAbs &clip)
{
  if (_backgroundD3D!=RecHighLevel) DoDrawLineDo(line,c0,c1,clip);
  else
  {
    RecordHighlevel()->DrawLineDo(line,c0,c1,clip);
  }
}
void EngineDD11::DoDrawLineDo(const Line2DAbs &line, PackedColor c0, PackedColor c1, const Rect2DAbs &clip)
{
  float x0 = line.beg.x;
  float y0 = line.beg.y;
  float x1 = line.end.x;
  float y1 = line.end.y;

  // convert line to poly;
  float dx = x1-x0;
  float dy = y1-y0;
  float dSize2 = dx*dx+dy*dy;
  float invDSize = dSize2 >0 ? InvSqrt(dSize2) : 1;

  // direction perpendicular dx, dy
  // TODO: use color alpha as width
  // 2D line drawing
  float pdx = +dy*invDSize, pdy = -dx*invDSize;
  float w = 3.0f;
  x0 -= pdx*(w*0.5);
  x1 -= pdx*(w*0.5);
  y0 -= pdy*(w*0.5);
  y1 -= pdy*(w*0.5);
  float x0Side = x0+pdx*w, y0Side = y0+pdy*w;
  float x1Side = x1+pdx*w, y1Side = y1+pdy*w;


  Vertex2DAbs vertices[4];
  float off = 0;
  //float off = 0.5f;
  vertices[0].x = x0-off;
  vertices[0].y = y0-off;
  vertices[0].u = 0;
  vertices[0].v = 0.25;
  vertices[0].color = c0;

  vertices[1].x = x0Side-off;
  vertices[1].y = y0Side-off;
  vertices[1].u = 0;
  vertices[1].v = 1;
  vertices[1].color = c0;

  vertices[3].x = x1-off;
  vertices[3].y = y1-off;
  vertices[3].u = 0.1;
  vertices[3].v = 0.25;
  vertices[3].color = c1;

  vertices[2].x = x1Side-off;
  vertices[2].y = y1Side-off;
  vertices[2].u = 0.1;
  vertices[2].v = 1;
  vertices[2].color = c1;

  DoDrawPolyDo(vertices,4,clip);
}

void EngineDD11::DrawLine( const TLVertexTable &mesh, int beg, int end, int orLine )
{
  if (ResetNeeded()) return;

  const TLVertex &v0 = mesh.GetVertex(beg);
  const TLVertex &v1 = mesh.GetVertex(end);

  float x0 = v0.pos.X();
  float y0 = v0.pos.Y();
  float x1 = v1.pos.X();
  float y1 = v1.pos.Y();

  float z0 = v0.pos.Z();
  float z1 = v1.pos.Z();
  float w0 = v0.rhw;
  float w1 = v1.rhw;

  // use line texture
  Texture *tex = GPreloadedTextures.New(TextureLine);
  const MipInfo &mip = _textBank->UseMipmap(tex,1,1);

  // convert line to poly;
  int specFlags = NoZWrite|IsAlpha|ClampU|ClampV|IsAlphaFog|orLine;
  float dx = x1-x0;
  float dy = y1-y0;
  float dSize2 = dx*dx+dy*dy;
  float invDSize = dSize2 >0 ? InvSqrt(dSize2) : 1;

  float dSize = dSize2*invDSize;

  // direction perpendicular dx, dy
  // TODO: use color alpha as width
  // 2D line drawing
  float pdx = +dy*invDSize, pdy = -dx*invDSize;
  float w = 3.0f;
  x0 -= pdx*(w*0.5);
  x1 -= pdx*(w*0.5);
  y0 -= pdy*(w*0.5);
  y1 -= pdy*(w*0.5);
  float x0Side = x0+pdx*w, y0Side = y0+pdy*w;
  float x1Side = x1+pdx*w, y1Side = y1+pdy*w;

  Vertex2DAbs vertices[4];
  float off = 0.0f;
  //float off = 0.5f;
  vertices[0].x = x0-off;
  vertices[0].y = y0-off;
  vertices[0].z = z0;
  vertices[0].w = w0;
  vertices[0].u = 0;
  vertices[0].v = 0.25;
  vertices[0].color = v0.color;

  vertices[1].x = x0Side-off;
  vertices[1].y = y0Side-off;
  vertices[1].z = z0;
  vertices[1].w = w0;
  vertices[1].u = 0;
  vertices[1].v = 1;
  vertices[1].color = v0.color;

  vertices[2].x = x1Side-off;
  vertices[2].y = y1Side-off;
  vertices[2].z = z1;
  vertices[2].w = w1;
  vertices[2].u = dSize;
  vertices[2].v = 1;
  vertices[2].color = v1.color;

  vertices[3].x = x1-off;
  vertices[3].y = y1-off;
  vertices[3].z = z1;
  vertices[3].w = w1;
  vertices[3].u = dSize;
  vertices[3].v = 0.25;
  vertices[3].color = v1.color;

  Rect2DAbs clip(0,0,_w,_h);

  DrawPoly(mip,vertices,4,clip,specFlags);
}

void EngineDD11::PrepareMesh( int cb, int spec )
{
  // prepare internal variables
  SwitchTL(cb,TLDisabled);
}

bool EngineDD11::BeginMesh( int cb, TLVertexTable &mesh, int spec )
{
  Assert(cb<0); // no recording possible here
  SwitchTL(cb,TLEnabled);
  SetWorldView(cb);
  //_mesh = &mesh;

  // Zero vertex and index buffer in the device, forget _iBufferLast and _vBufferLast.
  ForgetDeviceBuffers(cb);

  return AddVertices(mesh.VertexData(),mesh.NVertex());
}

void EngineDD11::EndMesh( int cb, TLVertexTable &mesh )
{
  //_mesh = NULL;
}

IVisibilityQuery *EngineDD11::BegPixelCounting(IVisibilityQuery *query)
{
  return NULL;
  /*
  D3DVisibilityQuery *q = static_cast<D3DVisibilityQuery *>(query);
  if (!q) q = new D3DVisibilityQuery;
  // check the result of all queries which are currently running
  q->CheckResults(this);

  int id = _ids.Open();
  if (id>=0)
  {
  }
  else
  {
  }
  return q;
  */
}
void EngineDD11::EndPixelCounting(IVisibilityQuery *query)
{
  //D3DVisibilityQuery *q = static_cast<D3DVisibilityQuery *>(query);
  //if (!q) return;
}

void EngineDD11::RefreshBuffer(const VertexTable &sMesh)
{
  VertexBufferD3D11 *vb = static_cast<VertexBufferD3D11 *>(sMesh.LockVertexBuffer());
  // this would be otherwise done by ConvertToVBuffer
  if (IsInCBScope())
  {
    // handle using SLL
    if (vb->AddIntoSList())
    {
      _vbLRURequest.Add(vb);
    }
    return;
  }

  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  if (!vb->IsInList())
  {
    Fail("Refreshed buffer should be already in LRU");
    _vBufferLRU.Add(vb);
    vb->SetOwner(&sMesh);
  }
  else
  {
    _vBufferLRU.Refresh(vb);
  }

}

Ref<VertexBuffer> EngineDD11::CreateVertexBuffer(const Shape &src, VBType type, bool frequent)
{
  //ThrottleVRAMAllocation(_textBank->GetMaxVRAMAllocation(),_textBank->GetVRamTexAllocation());
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  Ref<VertexBufferD3D11> buffer = new VertexBufferD3D11;
  if (buffer->Init(this, src, type, frequent))
  {
    Assert(!IsInCBScope());
    // insert it into the LRU cache
    // when we are creating it, it is because we need it - add it as a most recent one
    Assert(!buffer->IsInList());
    _vBufferLRU.Add(buffer);
    buffer->SetOwner(&src);
    return buffer.GetRef();
  }
  else
  {
    // buffer is Ref, so it cannot be deleted this way!
    //delete buffer;
    return NULL;
  }
}

void EngineDD11::ReleaseAllVertexBuffers()
{
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  EndHighLevelRecording();
  for (VertexBufferD3D11LRUItem *last = _vBufferLRU.First(); last; )
  {
    // free vertex buffer that was not used for longest time
    VertexTable *shape = unconst_cast(last->GetOwner());

    // this one may be destroyed in ReleaseVBuffer - process to a next one
    last = _vBufferLRU.Next(last);
    // release v-buffer
    if (shape) shape->ReleaseVBuffer(); // owner can be set to NULL if ReleaseVBuffer() was called already
  }
}

void EngineDD11::GetZCoefs(int cb, float &zAdd, float &zMult)
{
  int zBias = CBState(cb)._bias;
  zMult = 1.0f-zBias*1e-7f;
  zAdd = zBias*-2e-7f;
}

void EngineDD11::SetBias( int cb, int bias )
{
  // we always implement z-bias manipulating the matrix
  if (bias==CBState(cb)._bias) return;
  CBState(cb)._bias=bias;
  // Simulate z-bias using perspective matrix
  if (CBState(cb)._tlActive==TLEnabled)
  {
    // Setup projection matrix
    ProjectionChanged(cb);
  }
}

void EngineDD11::ResetGameState()
{
  if (_pp._radialBlur)
  {
    _pp._radialBlur->Enable(false);
    _pp._radialBlur->SetDefaultValues();
  }

  if (_pp._chromAber)
  {
    _pp._chromAber->Enable(false);
    _pp._chromAber->SetDefaultValues();
  }

  if (_pp._wetDistort)
  {
    _pp._wetDistort->Enable(false);
    _pp._wetDistort->SetDefaultValues();
  }

  if (_pp._colors)
  {
    _pp._colors->Enable(false);
    _pp._colors->SetDefaultValues();
  }

  if (_pp._dynamicBlur)
  {
    _pp._dynamicBlur->Enable(false);
    _pp._dynamicBlur->SetDefaultValues();
  }

  if (_pp._filmGrain)
  {
    _pp._filmGrain->Enable(false);
    _pp._filmGrain->SetDefaultValues();
  }

  if (_pp._clrInvers)
  {
    _pp._clrInvers->Enable(false);
    _pp._clrInvers->SetDefaultValues();
  }

  _spp._hit->DestroyHndls();
  _spp._movement->DestroyHndls();
  _spp._tired->DestroyHndls();
  _spp._fire->DestroyHndls();

  DestroyPPHndlFunc destFunc;
  ForEachPostEffectHndl(destFunc);

  _pp._effects.Clear();
  _pp._effectsThis.Clear();

  if (_spp._hit) _spp._hit->Init();
  if (_spp._movement) _spp._movement->Init();
  if (_spp._tired) _spp._tired->Init();
  if (_spp._fire) _spp._fire->Init();
}

// initialize post effect - called after device reset
class InitPPHndlFunc: public IPPHndlFunc
{
private:
  EngineDD11 *_EngineDD11;
  Ref<EngineDD11::PPGaussianBlur> _gBlur;

public:
  InitPPHndlFunc(EngineDD11 *EngineDD11, Ref<EngineDD11::PPGaussianBlur> gBlur): _EngineDD11(EngineDD11), _gBlur(gBlur) {};

  bool operator () (PostProcessHndl &hndl)
  {
    if (hndl == InvalidPPHndl || _EngineDD11 == NULL) return false;

    switch (hndl._type)
    {
#ifdef _RAIN3D
case PERain3D:
  {
    // original effect 
    EngineDD11::PPRain3D *chAPP = dynamic_cast<EngineDD11::PPRain3D*> (hndl._pp.GetRef());

    if (chAPP && _EngineDD11->_pp._rain3D)
    {
      // get resources
      EngineDD11::PPRain3DResources *res = const_cast<EngineDD11::PPRain3DResources *> (_EngineDD11->_pp._rain3D->Resources());

      if (res && !chAPP->Init(res, false))
      {
        LogF("Error: Post process effect 'PPRain3D' initialization failed");
        return false;
      }
      chAPP->SetLevel(EngineDD11::PPLVeryLow);
      return true;
    }
  }
  return false;
#endif
case PEFilmGrain:
  {
    // original effect 
    EngineDD11::PPFilmGrain *chAPP = dynamic_cast<EngineDD11::PPFilmGrain*> (hndl._pp.GetRef());

    if (chAPP && _EngineDD11->_pp._filmGrain)
    {
      // get resources
      EngineDD11::PPFilmGrainResources *res = const_cast<EngineDD11::PPFilmGrainResources *> (_EngineDD11->_pp._filmGrain->Resources());

      if (res && !chAPP->Init(res, false))
      {
        LogF("Error: Post process effect 'PPFilmGrain' initialization failed");
        return false;
      }
      chAPP->SetLevel(EngineDD11::PPLVeryLow);
      return true;
    }
  }
  return false;

case PEChromAberration:
  {
    // original effect 
    EngineDD11::PPChromAber *chAPP = dynamic_cast<EngineDD11::PPChromAber*> (hndl._pp.GetRef());

    if (chAPP && _EngineDD11->_pp._chromAber)
    {
      // get resources
      EngineDD11::PPResourcesExt *res = const_cast<EngineDD11::PPResourcesExt *> (_EngineDD11->_pp._chromAber->Resources());

      if (res && !chAPP->Init(res, false))
      {
        LogF("Error: Post process effect 'PPChromAber' initialization failed");
        return false;
      }
      chAPP->SetLevel(EngineDD11::PPLLow);
      return true;
    }
  }
  return false;

case PEColorCorrections:
  {
    EngineDD11::PPColors *cPP = dynamic_cast<EngineDD11::PPColors*> (hndl._pp.GetRef());

    if (cPP && _EngineDD11->_pp._colors)
    {
      // get resources
      EngineDD11::PPColorResources *res = const_cast<EngineDD11::PPColorResources *> (_EngineDD11->_pp._colors->Resources());

      if (res && !cPP->Init(res, false))
      {
        LogF("Error: Post process effect 'PPColors' initialization failed");
        return false;
      }
      cPP->SetLevel(EngineDD11::PPLForce);
      return true;
    }
  }
  return false;

case PERadialBlur:
  {
    EngineDD11::PPRadialBlur *rbPP = dynamic_cast<EngineDD11::PPRadialBlur*> (hndl._pp.GetRef());

    if (rbPP && _EngineDD11->_pp._radialBlur)
    {
      EngineDD11::PPResourcesExt *res = const_cast<EngineDD11::PPResourcesExt *>(_EngineDD11->_pp._radialBlur->Resources());

      if (res && !rbPP->Init(res, false))
      {
        LogF("Error: Post process effect 'PPRadialBlur' initialization failed");
        return false;
      }
      rbPP->SetLevel(EngineDD11::PPLVeryLow);
      return true;
    }
  }
  return false;

case PEDynamicBlur:
  {
    EngineDD11::PPDynamicBlur *dbPP = dynamic_cast<EngineDD11::PPDynamicBlur *> (hndl._pp.GetRef());

    if (dbPP && _EngineDD11->_pp._dynamicBlur)
    {
      EngineDD11::PPDynBlurResources *res = const_cast<EngineDD11::PPDynBlurResources *> (_EngineDD11->_pp._dynamicBlur->Resources());

      if (res && !dbPP->Init(res, _gBlur, false))
      {
        LogF("Error: Post process effect 'PPDynamicBlur' initialization failed");
        return false;
      }
      dbPP->SetLevel(EngineDD11::PPLVeryLow);
      return true;
    }
  }
  return false;

case PEWetDistortion:
  {
    EngineDD11::PPWetDistort *wdPP = dynamic_cast<EngineDD11::PPWetDistort *> (hndl._pp.GetRef());

    if (wdPP && _EngineDD11->_pp._wetDistort)
    {
      EngineDD11::PPWetDistortResources *res = const_cast<EngineDD11::PPWetDistortResources *> (_EngineDD11->_pp._wetDistort->Resources());

      if (res && !wdPP->Init(res, _gBlur, false))
      {
        LogF("Error: Post process effect 'PPWetDistort' initialization failed");
        return false;
      }
      wdPP->SetLevel(EngineDD11::PPLLow);
      return true;
    }
  }
  return false;

case PEColorInv:
  {
    // original effect 
    EngineDD11::PPColorsInversion *cliPP = dynamic_cast<EngineDD11::PPColorsInversion*> (hndl._pp.GetRef());

    if (cliPP && _EngineDD11->_pp._clrInvers)
    {
      // get resources
      EngineDD11::PPResourcesExt *res = const_cast<EngineDD11::PPResourcesExt *> (_EngineDD11->_pp._clrInvers->Resources());

      if (res && !cliPP->Init(res, false))
      {
        LogF("Error: Post process effect 'PPColorInversion' initialization failed");
        return false;
      }
      cliPP->SetLevel(EngineDD11::PPLLow);
      return true;
    }
  }
  return false;

default:
  {
    LogF("Error: Unknown post process effect");
    return false;
  }
    }
  }
};

#if _ARMA3_SIMUL_WEATHER
void EngineDD11::InitSimulWeather()
{
  if (GLandscape && GLandscape->GetSimulWeather())
    GLandscape->GetSimulWeather()->InitResources();
}
void EngineDD11::DeInitSimulWeather()
{
  if (GLandscape && GLandscape->GetSimulWeather())
    GLandscape->GetSimulWeather()->DeInitResources();
}

SimulWeatherBase* EngineDD11::CreateSimulWeather()
{
  return new SimulWeatherDX11(this);
}

void EngineDD11::UpdateSimulWeatherTexture(TextureSysGPUDX11 *tex, unsigned textureIndex)
{
  CALL_CB_DIRECT(this, UpdateSimulWeatherTexture, tex, textureIndex);
}

void EngineDD11::DoUpdateSimulWeatherTexture(TextureSysGPUDX11 *tex, unsigned textureIndex)
{
  if (tex)
    tex->UpdateGPUTexInternal(textureIndex);
}

void EngineDD11::SetSWGeometryLayersCount(int layers) 
{
  saturate(layers, MinSWLayers, MaxSWLayers);
  _swLayersCount = layers; 
}

#endif

LSError EngineDD11::SerializeGameState(ParamArchive &ar)
{
  if (_pp._filmGrain)   { CHECK(ar.SerializeDef("filmGrain",*_pp._filmGrain,0)); }
  if (_pp._radialBlur)  { CHECK(ar.SerializeDef("radialBlur",*_pp._radialBlur,0)); }
  if (_pp._chromAber)   { CHECK(ar.SerializeDef("chromAber",*_pp._chromAber, 0)); }
  if (_pp._wetDistort)  { CHECK(ar.SerializeDef("wetDistort",*_pp._wetDistort, 0)); }
  if (_pp._colors)      { CHECK(ar.SerializeDef("colors",*_pp._colors, 0)); }
  if (_pp._dynamicBlur) { CHECK(ar.SerializeDef("dynBlur",*_pp._dynamicBlur, 0)); }
  if (_pp._clrInvers)   { CHECK(ar.SerializeDef("clrInv", *_pp._clrInvers, 0)); }

  if (_spp._hit) { CHECK(ar.Serialize("hitSpp", *_spp._hit, 0)); }
  if (_spp._movement) { CHECK(ar.Serialize("movementSpp", *_spp._movement, 0)); }
  if (_spp._tired) { CHECK(ar.Serialize("tirredSpp", *_spp._tired, 0)); }
  if (_spp._fire) { CHECK(ar.Serialize("fireSpp", *_spp._fire, 0)); }

  if (ar.IsLoading() && (ar.GetPass() == ParamArchive::PassFirst))
  {
    // release stuff
    DestroyPPHndlFunc destFunc;
    ForEachPostEffectHndl(destFunc);    
    _pp._effects.Clear();
    _pp._effectsThis.Clear();

    CHECK(ar.Serialize("ppEffects", _pp._effects, 0));

    // initialize effect resources
    if (_pp._gaussianBlur)
    {
      // initialize effect resources
      InitPPHndlFunc initFunc(this, _pp._gaussianBlur);
      ForEachPostEffectHndl(initFunc);
    }
  }

  if (ar.IsSaving())
  {
    CHECK(ar.Serialize("ppEffects", _pp._effects, 0));
  }

  return LSOK;
}

#ifdef TRAP_FOR_0x8000
void EngineDD11::TrapFor0x8000(const char *str)
{
  if (CheckMainThread())
    if (_textBank) _textBank->TrapFor0x8000(str);
}

#endif

void EngineDD11::CommitRadBlur(float time)
{
  if (_pp._radialBlur)
  {
    _pp._radialBlur->Commit(time);
  }
}

void EngineDD11::CommitSSAO(float time)
{
  if (_pp._ssao)
  {
    _pp._ssao->Commit(time);
  }
}

void EngineDD11::CommitFilmGrain(float time)
{
  if (_pp._filmGrain)
  {
    _pp._filmGrain->Commit(time);
  }
}

void EngineDD11::CommitChromAberration(float time)
{
  if (_pp._chromAber)
  {
    _pp._chromAber->Commit(time);
  }
}

void EngineDD11::CommitClrInvers(float time)
{
  if (_pp._clrInvers)
  {
    _pp._clrInvers->Commit(time);
  }
}

void EngineDD11::CommitWetDistortion(float time)
{
  if (_pp._wetDistort)
  {
    _pp._wetDistort->Commit(time);
  }
}

void EngineDD11::CommitColorCorrections(float time)
{
  if (_pp._colors)
  {
    _pp._colors->Commit(time);
  }
}

void EngineDD11::CommitDynamicBlur(float time)
{
  if (_pp._dynamicBlur)
  {
    _pp._dynamicBlur->Commit(time);
  }
}

bool EngineDD11::CommittedRadBlur() const
{
  if (_pp._radialBlur) return _pp._radialBlur->Committed();
  return true;
}

bool EngineDD11::CommittedChromAberration() const
{
  if (_pp._chromAber) return _pp._chromAber->Committed();
  return true;
}

bool EngineDD11::CommittedClrInvers() const
{
  if (_pp._clrInvers) return _pp._clrInvers->Committed();
  return true;
}

bool EngineDD11::CommittedSSAO() const
{
  if (_pp._ssao) return _pp._ssao->Committed();
  return true;
}

bool EngineDD11::CommittedFilmGrain() const
{
  if (_pp._filmGrain) return _pp._filmGrain->Committed();
  return true;
}


bool EngineDD11::CommittedWetDistortion() const
{
  if (_pp._wetDistort) return _pp._wetDistort->Committed();
  return true;
}

bool EngineDD11::CommittedColorCorrections() const
{
  if (_pp._colors) return _pp._colors->Committed();
  return true;
}

bool EngineDD11::CommittedDynamicBlur() const
{
  if (_pp._dynamicBlur) return _pp._dynamicBlur->Committed();
  return true;
}

/// Enable/disable radial blur post process effect
void EngineDD11::EnableRadialBlur(bool enable)
{
  if (_pp._radialBlur)
  {
    _pp._radialBlur->Enable(enable);
  }
}

/// Enable/disable radial blur post process effect
void EngineDD11::EnableChromAberration(bool enable)
{
  if (_pp._chromAber)
  {
    _pp._chromAber->Enable(enable);
  }
}

void EngineDD11::EnableClrInvers(bool enable)
{
  if (_pp._clrInvers)
  {
    _pp._clrInvers->Enable(enable);
  }
}

/// Enable/disable SSAO post process effect
void EngineDD11::EnableSSAO(bool enable)
{
  if (_pp._ssao)
  {
    _pp._ssao->Enable(enable);
  }
}

void EngineDD11::SetSSAOQuality(int quality)
{
  if (_pp._ssao)
  {
    _pp._ssao->Enable((_pp._ssao->GetLevel() & _postFXLevelMask) != 0);
    _pp._ssao->SetQuality(quality);
  }
}


/// Enable/disable film grain post process effect
void EngineDD11::EnableFilmGrain(bool enable)
{
  if (_pp._filmGrain)
  {
    _pp._filmGrain->Enable(enable);
  }
}


/// Enable/disable wet distortion post process effect
void EngineDD11::EnableWetDistortion(bool enable)
{
  if (_pp._wetDistort)
  {
    _pp._wetDistort->Enable(enable);
  }
}

/// Enable/disable color correction post process effect
void EngineDD11::EnableColorCorrections(bool enable)
{
  if (_pp._colors)
  {
    _pp._colors->Enable(enable);
  }
}

/// Enable/disable dynamic blur post process effect
void EngineDD11::EnableDynamicBlur(bool enable)
{
  if (_pp._dynamicBlur)
  {
    _pp._dynamicBlur->Enable(enable);
  }
}

/// Set parameters for post process effect
void EngineDD11::SetRadialBlurrParams(float pwrX, float pwrY, float offsX, float offsY)
{
  if (_pp._radialBlur)
  {
    _pp._radialBlur->SetBlurPower(pwrX, pwrY);
    _pp._radialBlur->SetBlurOffset(offsX, offsY);
  }
}

/// Set parameters for post process effect
void EngineDD11::SetWetDistortionParams(float blur,
                                       float pwrX, float pwrY,
                                       float sh1, float sh2, float sv1, float sv2,
                                       float ah1, float ah2, float av1, float av2,
                                       float rndX, float rndY, float posX, float posY)
{
  if (_pp._wetDistort)
  {
    _pp._wetDistort->SetBlurriness(blur);
    _pp._wetDistort->SetEffectPower(pwrX, pwrY);
    _pp._wetDistort->SetWaveSpeeds(sh1, sh2, sv1, sv2);
    _pp._wetDistort->SetWaveAmplitudes(ah1, ah2, av1, av2);
    _pp._wetDistort->SetPhaseCoefs(rndX, rndY, posX, posY);
  }
}

/// Set parameters for post process effect
void EngineDD11::SetSSAOParams(const float *params, int nums)
{
  if (_pp._ssao)
  {
    _pp._ssao->SetParams(params, nums);
  }
}

/// Set parameters for post process effect
void EngineDD11::SetFilmGrainParams(const float *params, int nums)
{
  if (_pp._filmGrain)
  {
    _pp._filmGrain->SetParams(params, nums);
  }
}

/// Set parameters for post process effect
void EngineDD11::SetChromaticAberrationParams(float aberationPowerX, float aberationPowerY, bool value)
{
  if (_pp._chromAber)
  {
    _pp._chromAber->SetAberration(aberationPowerX, aberationPowerY);
    _pp._chromAber->SetAspectCorrection(value);
  }
}

void EngineDD11::SetClrInversParams(Color invColor)
{
  if (_pp._clrInvers)
  {
    _pp._clrInvers->SetInversion(invColor);
  }
}

void EngineDD11::SetRainTexture(Texture *tex)
{
  if (_pp._rain3D)
  {
    _pp._rain3D->SetTexture(static_cast<TextureD3D11 *>(tex));
  }
}

void EngineDD11::SetRainDensity(float density)
{
  if (_pp._rain3D)
  {
    //the same treshold as rain clutter!
    const float densityFactor = 0.8f;
    float rainDensity = density * densityFactor;
    if (rainDensity<0.05f) rainDensity = 0;

    //value from landscape is too strong
    float pars[] = {rainDensity*0.4f};
    _pp._rain3D->SetParams(pars, 1);
  }
}

/// Set parameters for post process effect
void EngineDD11::SetColorCorrectionsParameters(float brightness, float contrast, float offset, Color blendClr,
                                              Color colClr, Color weights)
{
  if (_pp._colors)
  {
    _pp._colors->SetBrightness(brightness);
    _pp._colors->SetContrast(contrast);
    _pp._colors->SetOffset(offset);
    _pp._colors->SetBlendColor(blendClr);
    _pp._colors->SetColorizeColor(colClr);
    _pp._colors->SetRGBWeights(weights);
  }

}

#if _ARMA3_RADIAL_COLOR_PP
void EngineDD11::SetColorCorrectionsParametersRadial(float radialMajorAxisRadius, float radialMinorAxisRadius, float radialRotationDeg, 
                                                    float radialCenterX, float radialCenterY, float radialInnerRadiusCoef, float radialInterpCoef) 
{
  if (_pp._colors)
  {
    _pp._colors->SetRadialMajorAxisRadius(radialMajorAxisRadius);
    _pp._colors->SetRadialMinorAxisRadius(radialMinorAxisRadius);
    _pp._colors->SetRadialRotationDeg(radialRotationDeg);
    _pp._colors->SetRadialCenterX(radialCenterX);
    _pp._colors->SetRadialCenterY(radialCenterY);
    _pp._colors->SetRadialInnerRadiusCoef(radialInnerRadiusCoef);
    _pp._colors->SetRadialInterpCoef(radialInterpCoef);
  }
}
#endif


/// Set parameters for post process effect
void EngineDD11::SetDynamicBlurParameters(float bluriness)
{
  if (_pp._dynamicBlur)
  {
    _pp._dynamicBlur->SetBlurrines(bluriness);
  }
}

/* Create post process effect */
int EngineDD11::CreatePostprocess(PostEffectType pEType, int priority)
{
  // custom effect with same priority exit yet
  if (!ValidatePPPriority(priority))
  {
    for (int i = 0; i < _pp._effects.Size(); i++)
    {
      const PostProcessHndl &ppHndl = _pp._effects[i];

      if (ppHndl._type == pEType)
      {
        PostProcess *pp = static_cast<PostProcess*>(ppHndl._pp.GetRef());
        if (pp && pp->GetPriority() == priority && ppHndl._hndl != -1)
        {
          // reuse custom effect, usually happen when handle is lost
          return ppHndl._hndl;
        }
      }
    }

    RptF("Error: Cannot create custom post effect(type: %d), PE with same priority(%i) already exist.", pEType, priority);
    return InvalidPPHndl._hndl;
  }

  Ref<PostProcess> postProcess = CreatePostprocess(pEType);

  if (postProcess)
  {
    postProcess->SetPriority(priority);

    PostProcessHndl &pPars = _pp._effects.Append();
    pPars._hndl = GetPostEffectHandle();
    pPars._type = pEType;
    pPars._pp = postProcess;

    return pPars._hndl;
  }

  return InvalidPPHndl._hndl;
};

// Convert string to post effect type
static PostEffectType ConvertStrToPE(const char *str)
{
  if (strcmpi(str, "RadialBlur") == 0)
  {
    return PERadialBlur;
  }
  else if(strcmpi(str, "FilmGrain") == 0)
  {
    return PEFilmGrain;
  }
  else if (strcmpi(str, "ChromAberration") == 0)
  {
    return PEChromAberration;
  }
  else if (strcmpi(str, "WetDistortion") == 0)
  {
    return PEWetDistortion;
  }
  else if (strcmpi(str, "ColorCorrections") == 0)
  {
    return PEColorCorrections;
  }
  else if (strcmpi(str, "DynamicBlur") == 0)
  {
    return PEDynamicBlur;
  }
  else if (strcmpi(str, "ColorInversion") == 0)
  {
    return PEColorInv;
  }

  return PENone;
}

int EngineDD11::CreatePostprocess(RString peTypeName, int priority)
{
  PostEffectType peType = ConvertStrToPE(peTypeName);

  if (peType == PENone) return InvalidPPHndl._hndl;

  return CreatePostprocess(peType, priority);
}

void EngineDD11::DestroyPostprocess(int hndl)
{
#if VALIDATE_HNDL_ENABLE
  bool hndlFound = ValidatePPEHndl(_pp._effects, hndl);
  Assert(hndlFound);
#endif

  PostProcessHndl &ppHndl = AccessPostprocess(hndl);

  if (ppHndl != InvalidPPHndl) { _pp._effects.Delete(ppHndl); }
}

/* Enable / disable post effect */
void EngineDD11::EnablePostprocess(int hndl, bool enable)
{

#if VALIDATE_HNDL_ENABLE
  bool hndlFound = ValidatePPEHndl(_pp._effects, hndl);
  Assert(hndlFound);
#endif

  PostProcessHndl &ppHndl = AccessPostprocess(hndl);

  if (ppHndl != InvalidPPHndl)
  {
    if (ppHndl._pp)
    {
      PostProcess *pProc = static_cast<PostProcess*> (ppHndl._pp.GetRef());
      if (pProc) pProc->Enable(enable);
    }
  }
}

int EngineDD11::SetPostprocessParams(int hndl, AutoArray<float> &pars)
{
#if VALIDATE_HNDL_ENABLE
  bool hndlFound = ValidatePPEHndl(_pp._effects, hndl);
  Assert(hndlFound);
#endif

  PostProcessHndl &ppHndl = AccessPostprocess(hndl);

  if (ppHndl == InvalidPPHndl) return 0;

  if (ppHndl._pp)
  {
    PostProcess *tmpPP = static_cast<PostProcess*> (ppHndl._pp.GetRef());
    if (tmpPP) return tmpPP->SetParams(pars);    
  }

  return 0;
}

int EngineDD11::SetPostprocessParams(int hndl, const float* pars, int nPars)
{
#if VALIDATE_HNDL_ENABLE
  bool hndlFound = ValidatePPEHndl(_pp._effects, hndl);
  Assert(hndlFound);
#endif

  PostProcessHndl &ppHndl = AccessPostprocess(hndl);

  if (ppHndl == InvalidPPHndl) return 0;

  if (ppHndl._pp)
  {
    PostProcess *tmpPP = static_cast<PostProcess*> (ppHndl._pp.GetRef());
    if (tmpPP) return tmpPP->SetParams(pars, nPars);    
  }

  return 0;
}

void EngineDD11::CommitPostprocess(int hndl, float time)
{
#if VALIDATE_HNDL_ENABLE
  bool hndlFound = ValidatePPEHndl(_pp._effects, hndl);
  Assert(hndlFound);
#endif

  PostProcessHndl &ppHndl = AccessPostprocess(hndl);

  if (ppHndl == InvalidPPHndl) return;

  if (ppHndl._pp)
  {
    PostProcess *tmpPP = static_cast<PostProcess*> (ppHndl._pp.GetRef());
    if (tmpPP) tmpPP->Commit(time);
  }
}

bool EngineDD11::CommittedPostprocess(int hndl)
{

#if VALIDATE_HNDL_ENABLE
  bool hndlFound = ValidatePPEHndl(_pp._effects, hndl);
  Assert(hndlFound);
#endif

  PostProcessHndl &ppHndl = AccessPostprocess(hndl);

  if (ppHndl == InvalidPPHndl) return false;

  if (ppHndl._pp)
  {
    PostProcess *tmpPP = static_cast<PostProcess*> (ppHndl._pp.GetRef());
    if (tmpPP) return tmpPP->Committed();
  }

  return true;
}

void EngineDD11::SetSpecialEffectPars(int index, const float *pars, int nPars)
{
  if (index == 0)
  {
    if (_spp._hit) _spp._hit->SetParams(pars, nPars);
  }

  if (index == 1)
  {
    if (_spp._movement) _spp._movement->SetParams(pars, nPars);
  }

  if (index == 2)
  {
    if (_spp._tired) _spp._tired->SetParams(pars, nPars);
  }
  if (index == 3)
  {
    if (_spp._fire) _spp._fire->SetParams(pars, nPars);
  }
}

void EngineDD11::PerformTaskOnMainThread(MainThreadRenderTask * task)
{
  if (!_mainThreadTasks.PutMultipleProducers(task))
  {
    LogF("Out of space for 3D async texts");
    delete task;
  }
}

struct EngineDD11::DrawText3DCenteredAsyncPars: public MainThreadRenderTask
{
  Vector3 center,up,aside;
  ClipFlags clip;
  Font *font;
  PackedColor color;
  int spec;
  BString<16> text; // avoid RString here, as we are accessing from multiple threads
  float x1c,y1c,x2c,y2c;

  void operator () (Engine *engine) const
  {
    static_cast<EngineDD11 *>(engine)->DoDrawText3DCenteredAsync(*this);
  }
};

void EngineDD11::DrawText3DCenteredAsync(
                                        Vector3Par center, Vector3Par up, Vector3Par aside, ClipFlags clip,
                                        Font *font, PackedColor color, int spec, const char *text,
                                        float x1c, float y1c, float x2c, float y2c
                                        )
{
  DrawText3DCenteredAsyncPars pars;
  pars.center = center;
  pars.up = up;
  pars.aside = aside;
  pars.clip = clip;
  pars.font = font;
  pars.color = color;
  pars.spec = spec;
  pars.text = text;
  pars.x1c = x1c;
  pars.y1c = y1c;
  pars.x2c = x2c;
  pars.y2c = y2c;
  if (!Glob.config._mtSerial || Glob.config._useCB)
  {
    PerformTaskOnMainThread(new DrawText3DCenteredAsyncPars(pars));
  }
  else
  {
    DoDrawText3DCenteredAsync(pars);
  }
}

void EngineDD11::DoDrawText3DCenteredAsync(const DrawText3DCenteredAsyncPars &pars)
{
  if (IsFontReady3D(pars.font))
  {
    Vector3 width = GEngine->GetText3DWidth(pars.aside, pars.font, pars.text);
    GEngine->DrawText3D(
      -1,pars.center + pars.up * 0.5 - width * 0.5, pars.up, pars.aside, pars.clip,
      pars.font, pars.color, 0, pars.text, pars.x1c, pars.y1c, pars.x2c, pars.y2c
      );
  }
}

void EngineDD11::Activate()
{

}
bool EngineDD11::Deactivate()
{
  if (_textBank)
  {
    PROFILE_SCOPE_EX(deact,dd11);
    _textBank->FlushHandles();
    if (_textBank->FrameNotFinishedYet())
    {
      LogF("Frame not finished yet");
      FinishPresent();
      FinishFinishPresent();
    }
  }
  return true;

}

#if _ENABLE_CHEATS && !defined _XBOX
// DX11 TODO
//#include <Es/Memory/normalNew.hpp>
//#include <Es/Memory/debugNew.hpp>

void EngineDD11::Screenshot(RString filename)
{
  // DX11 TODO
  //HRESULT result;
  //result = D3DXSaveSurfaceToFile(filename, D3DXIFF_BMP, _backBuffer, NULL, NULL);
  //if (result != D3D_OK) DDError9("Error: Screenshot front buffer saving failed", result);
  //result = D3DXSaveSurfaceToFile(filename + RString(".tga"), D3DXIFF_TGA, _renderTargetDepthInfoSurface, NULL, NULL);
  //if (result != D3D_OK) DDError9("Error: Screenshot depth buffer saving failed", result);
}

#else

void EngineDD11::Screenshot(RString filename)
{
}

#endif

VideoPlayback *EngineDD11::VideoOpen(const char *path, float volume)
{
  return NULL;
}

void EngineDD11::VideoClose(VideoPlayback *video)
{

}

void EngineDD11::VideoFrame(VideoPlayback *video)
{

}

/*
COMMAND_BUFFERS

==Command Buffer overview==

Command buffer recording allows recording multiple command buffers in parallel.
This is done as a multi-core optimization of the rendering pipeline.

Each CB being recorded uses a separate "rendering state" (see CBState(cb)) which also includes a recording pseudo-device.

=== CB ID===

Calls which may be contributing to a CB use a CB ID.
CB ID <0 means the command should be executed right away.
CB ID >=0 identifies the Command Buffer slot.
*/

void EngineDD11::StartCBScope(int numThreads, int nResults)
{
  // verify there are no 3D text requests pending from outside of the scope
  _mainThreadTasks.Compact();

  Assert(_backgroundD3D!=RecNone || _cbState[0]._rec.GetDataSize()==0);
  //if (!Glob.config._renderThread) RESET_LOG_DEVICE();
  Assert(_backgroundD3D!=RecHighLevel);
  if (_backgroundD3D!=RecNone)
  {
    //Assert(_resultsMem.UsedPtr()==_resultsMemUsedStart);
    //Assert(_cbState[0]._rec.GetDataSize()==0);

    // if there are already any background tasks waiting, flush them
    bool somethingSubmitted = SubmitBackground(_bg,true,true,CleanUpNeeded,"begCB");
    // if predication is active, we must not submit any data now, they must already have been submitted before
    //Assert(!_bgRecPredication || !somethingSubmitted);
    (void)somethingSubmitted;

    // wait until there is enough of free space to write
    DoAssert(nResults<=_bg._results.TotalSize());
    WaitUntilSpaceAvailable(_bg,nResults);
  }

  _bgResultsInThisScope = nResults;
  _bgWriteInThisScope = _bg._results.GetWriteCursor();

  // anything which is rendered immediately in this scope should be captured instead
  StartBackgroundScope();
  // no need to create more threads than results
  // in extreme case of no results this can mean no threads are created, saving even SetHWToUndefinedState overhead
  if (nResults==0) numThreads = nResults;
  //if (numThreads>nResults) numThreads = nResults;
  _cbCount = numThreads;
  // if there is only one CB, there is no need to set into undefined state
  if (_cbCount>0)
  {
    PROFILE_SCOPE_DETAIL_EX(hwIni,dd11)
    // make sure we are ready to T&L, and do not have to switch it on later
    SwitchTL(-1,TLEnabled);
    // will reset vertex decl. as well
    SetHWToUndefinedState(-1);
    // now we can record this as an "undefined" state
    // starting from an undefined state means only copying this later
    // this is also used a way how to inherit settings from main thread to other threads
    _undefined = _cbState[0];
    // for each thread track if we are rendering in sequence
    for (int i=0; i<_cbCount; i++)
    {
      // for first rendering we force the undefined state
      CBState(i)._lastResult = INT_MAX;
    }
  }

  // make sure no discardable resources are tracked in some CB state, so that we can discard them as needed
  // them may be discarding during CB preparation, as individual microjobs are created
  // StartCBScope is called with (0,0) before microjob creation
  UnsetAllDiscardableResourcesFromCBState();
}

void EngineDD11::EndCBScope()
{
  Assert(_backgroundD3D!=RecNone || _cbState[0]._rec.GetDataSize()==0);

  int cbCount = _cbCount;
  if (cbCount>0)
  {
    // if we are capturing the API calls and there were no rendering instructions submitted, we are OK
    if (_backgroundD3D==RecNone || _bgWriteInThisScope<_bg._results.GetWriteCursor())
    {
      // some rendering was done from the main thread - we need to set it back to unknown
      // because the CB playback will place it in a different (unknown) state
      PROFILE_SCOPE_DETAIL_EX(hwCln,dd11)
      SetHWToUndefinedState(-1);
    }
  }

  Assert(_backgroundD3D!=RecNone || _cbState[0]._rec.GetDataSize()==0);

  _cbCount = -1;

  bool forceCleanUp = false;
  { // now replay all CBs

    PROFILE_SCOPE_EX(drwCB,dd11);

    _bg._results.PutEnd(_bgResultsInThisScope);

    // mark we can destroy the results
    // TODOPRIMING: when priming, do not mark
    if (!UsePredication())
    {
      _bg._results.AdvanceToWrite(CanDestroyResults);
    }


    Assert(_backgroundD3D!=RecHighLevel);
    if (_backgroundD3D!=RecNone)
    {
      // report the data are there
      if (_bgResultsInThisScope>0)
      {
        _bg.SetDataPending(true);
        forceCleanUp = true;
      }
    }
    else
    {
      // unset needed only when some CB was really played
      bool unsetAll = false;
      // consume all buffers and process them
      while (_bg._results.EmptySpace(ProcessResults)<_bg._results.TotalSize())
      {
        const RenderCBToPlay &res = _bg._results.Get(ProcessResults);
        if (res.Size()>0)
        {
          PlayCB(res);
          unsetAll = true;
        }
        _bg._results.Advance(ProcessResults);
      }
      // when not running in background, we can flush after each batch of CBs
      FlushBackground(_bg,true,"");

      if (unsetAll) UnsetAllDiscardableResources();
    }
  }  

  // if there are any 3D text requests pending, flush them now
  while(_mainThreadTasks.CountPending(0)>0)
  {
    MainThreadRenderTask *&pars = _mainThreadTasks.Get(0);
    if (pars)
    {
      (*pars)(this);
      delete pars;
      pars = NULL;
    }

    _mainThreadTasks.Advance(0);
  }
  _mainThreadTasks.Compact();

  Assert(_backgroundD3D!=RecNone || _cbState[0]._rec.GetDataSize()==0);

  Assert(_backgroundD3D!=RecHighLevel);
  // flush, but stay in the "background processing mode" as long as possible
  if (_backgroundD3D!=RecNone)
  {
    // some rendering done, mark we will need to unset once done with rendering
    SubmitBackground(_bg,true,true,forceCleanUp ? CleanUpForced : CleanUpNeeded,"endCB");
#if _DEBUG || _PROFILE
    if (Glob.config._renderThread==Config::ThreadingEmulated)
    {
      KickOffCBDebug();
    }
#endif
  }

  // we are sure no CB threads are running now, therefore we can safely process the LRU requests
  ProcessLRURequests();

  if (Glob.config._renderThread==Config::ThreadingEmulated)
  {
    BGReleaseResourcesAndTextures();
    BGReleaseRefs();
  }
  if (!Glob.config._predication)
  {
    Assert(_bg._resultsMem.UsedPtr()==_bg._resultsMemUsedStart);
  }
  Assert(_cbState[0]._rec.GetDataSize()==0);
}
bool EngineDD11::IsInCBScope() const
{
  // we do not care when immediate CB is being recorded, that does not cause any MT safety issues to calls like Create
  // as API is serializing them via a critical section
  return _cbCount>0;
}

/**
@return CB ID (see COMMAND_BUFFERS)
*/
int EngineDD11::StartCBRecording(int resultIndex, int thrId, int debugId)
{
  // thread ID is the same as the result CB ID
  DoAssert(thrId<CBCount());
  int cb = thrId;
  RendStateExt &cbState = CBState(cb);
  if (cb>=0)
  {
    Assert(resultIndex>=0 && resultIndex<_bgResultsInThisScope);
#if _ENABLE_REPORT
    InterlockedIncrement(&_d3dDeviceNonRec._cbCount);
#endif
    Assert(cbState._rec._predicationIndex.Size()==0); // no predication outside of the CB scope?
    cbState._rec.Reset();
    // often tasks are being processed sequentially by the thread (this is the default scheduling strategy)
    // in such case there is no need to reset the state
    if (resultIndex!=cbState._lastResult+1)
    {
      PROFILE_SCOPE_DETAIL_EX(hwPrp,dd11)
        // note: EndCommandBuffer calls UnsetAll
        // make sure we are recording with no dependencies to the previous CB in this thread
        // restore the CB -1 state, which should be the "undefined" state - was set by StartCBScope
        // TODO:MC: we cannot assign state here without modifying the CB recording device state
        if (cbState._lastResult!=INT_MAX)
        {
          // for continued rendering (work stealing) SetHWToUndefinedState should be enough
          SetHWToUndefinedState(cb);
        }
        else
        {
          // for the first operation we need to copy the state as currently set
          // the state contains many things which SetHWToUndefinedState is not touching, but we need it to be OK
          cbState.RendState::operator =(_undefined);
        }
    }
  }
  else
  {
    Assert(_backgroundD3D!=RecHighLevel);
    // flush the immediate CB - may flush rendering done not via CB
    // no need to set the event - will be set later by EndCBScope
    if (_backgroundD3D!=RecNone)
    {
      SubmitBackground(_bg,true,false,CleanUpNeeded,"cbPrp");
    }
  }
#if PIX_NAMED_CB_SCOPES
  cbState._debugId = debugId;
#endif
  return cb;
}


void EngineDD11::StopCBRecording(int cb, int resultIndex, const char *name)
{
  if (cb<0)
  {
    // simulated immediate CB flush
    Assert(_backgroundD3D!=RecHighLevel);
    if (_backgroundD3D!=RecNone)
    {
      // no need to set the event - will be set later by EndCBScope
      SubmitBackground(_bg,true,false,CleanUpNeeded,name);
    }
    // no CB should also mean no result slot is reserved
    Assert(resultIndex<0);
    return;
  }
#if _ENABLE_REPORT
  InterlockedDecrement(&_d3dDeviceNonRec._cbCount);
#endif
  Assert(resultIndex>=0 && resultIndex<_bgResultsInThisScope);
  RendStateExt &cbState = CBState(cb);
  // store the result
  int size;
  const void *data = cbState._rec.GetData(size);
  RenderCBToPlay &res = *_bg._results.PutBegin(resultIndex);
  res.Resize(size,_bg._resultsMem);
  if (size>0)
  {
    PrefetchT0Off(data, 0);

#if _XMEMCPY //_XBOX
    XMemCpy(res.Data(),data,size);
#else
    memcpy(res.Data(),data,size);
#endif
    res._predication = cbState._rec.GetPredication();

    for (int i=0; i<res._predication.Size(); i++)
    {
      Assert(res._predication[i].end<=size);
    }
  }
  else
  {
    res._predication.Clear();
  }

  // predicate depending on current rendering mode
  res._predicate = CurrentPredicationMode();
#if _DEBUG
  // we can access _debug RString from one thread only
  if (Glob.config._mtSerial) res._debug = cbState._rec.GetDebugText();
#endif

  // when recording for predication, mark data as not destructible

  res.MarkNoDestroy(_bgRecPredication);

#if PIX_NAMED_CB_SCOPES
  res._scopeName = name;
  res._debugId = ++RenderCBToPlay::_currDebugId;
#endif
#if LOG_RESULTS_ALLOC
  if (size>0) {LogF("%s (%d): Allocate %d",name,res._debugId,size);}
#endif
  res._memToClear = 0;
  cbState._lastResult = resultIndex;
  cbState._rec.Reset();
}

bool EngineDD11::Allow3DUI() const
{
  if (_backgroundD3D!=RecHighLevel) return true;
  // we cannot render 3D UI objects while capturing HL instruction
  // when some will ask us, we will terminate HL recording and return true to allow 3D
  unconst_cast(this)->EndHighLevelRecording();
  return true;
}

bool EngineDD11::CopyCBPredicatedScope(PredicatedCBScope &scope, PredicationMode pred, bool canDestroy, bool canAdvance)
{
  if (!UsePredication()) return false;
  Assert(scope.beg>=0); // the scope may be empty, but it needs to be recorded
  // if the scope is empty, we do not have to do anything, but we return true to avoid overhead of the default solution
  if (scope.end<=scope.beg) return true;
  Assert(_backgroundD3D!=RecHighLevel);
  if (_backgroundD3D!=RecNone)
  {
    // if there are already any background tasks waiting, flush them
    SubmitBackground(_bg,true,true,CleanUpNone,"cpyCB");
  }

  // copy the data, mark original as destructible
  // we need to make sure there is enough space for the results
  WaitUntilSpaceAvailable(_bg,scope.end-scope.beg);

  int copyBeg = _bg._results.GetWriteCursor();
  for (int i=scope.beg; i<scope.end; i++)
  {
    RenderCBToPlay &res = *_bg._results.PutBegin(i-scope.beg);
    RenderCBToPlay &src = _bg._results.Check(i); 
    res = src;
    // the CB we are copying must be recorded with different predication
    Assert(res._predicate!=pred);
    res._predicate = pred;
    // verify the source data were marked as non-destroyable
    Assert(src._noDestroy);
    // the copied data need to be destroyed unless we are still recording predication
    res.MarkNoDestroy(!canDestroy || _bgRecPredication);
    // invalidate the request for memory clearing - safe, processed by main (this) thread
    src._memToClear = 0;
    // it is tempting to invalidate the source data, however they may be not processed yet
    //     src._data = NULL;
    //     src._size = 0;
  }


  // the target data in place - we may start processing them
  _bg._results.PutEnd(scope.end-scope.beg);
  int copyEnd = _bg._results.GetWriteCursor();

  if (scope.end>scope.beg)
  {
    _bg.SetDataPending(true);
  }

  // the source data no longer needed - can be released now if desired
  if (canAdvance)
    _bg._results.AdvanceTo(CanDestroyResults,scope.end);

  // move the source scope to the copied version - needed in case more copies will be done
  scope.beg = copyBeg;
  scope.end = copyEnd;

  Assert(CheckThreadMemory(_bg));

  // once we have copied the data, we do not know the exact device state
  SetHWToUndefinedState(-1);

  return true;
}

bool EngineDD11::StartCBPredicatedScope(PredicatedCBScope &scope, PredicationMode pred)
{
  Assert(pred==CurrentPredicationMode());

  // TODO: consider merging StartCBPredicatedScope and CopyCBPredicatedScope for a more symetrical usage
  if (!UsePredication()) return true;
  Assert(_backgroundD3D!=RecHighLevel);

  if (_backgroundD3D!=RecNone)
  {
    // if there are already any background tasks waiting, flush them
    // we need to make sure they are not predicated, flushing them in StartCBScope would be too late
    SubmitBackground(_bg,true,true,CleanUpNone,"begCB"); // cleanup already recorded

    Assert(CheckThreadMemory(_bg));
  }

  Assert(!_bgRecPredication);
  _bgRecPredication = UsePredication();

  if (scope.beg>=0)
  {

    // if scope was already recorded, do not record it again
    // this time we use the same predication
    Verify( CopyCBPredicatedScope(scope,pred, true, true) );

    _bgRecPredication = false;
    return false;
  }

  scope.beg = _bg._results.GetWriteCursor();
  scope.end = 0;

  SetHWToUndefinedState(-1); /// WIP: 3DUI
  return true;
}
void EngineDD11::EndCBPredicatedScope(PredicatedCBScope &scope)
{
  if (!UsePredication()) return;
  if (_backgroundD3D!=RecNone)
  {
    // if there are already any background tasks waiting, flush them to make sure they are predicated
    SubmitBackground(_bg,true,true,CleanUpNone,"endCB"); // cleanup already recorded

    Assert(CheckThreadMemory(_bg));
  }
  scope.end = _bg._results.GetWriteCursor();
  Assert(scope.end>=scope.beg);
  _bgRecPredication = false;

  Assert(_backgroundD3D!=RecHighLevel);
  if (_backgroundD3D!=RecNone)
  {
    Assert(CheckThreadMemory(_bg));
  }
}

void EngineDD11::DrawPoly3D(const PositionRender &space, const MipInfo &mip, const Vertex2DFloat *vertices, int nVertices, const Rect2DFloat &clip, int specFlags, const TLMaterial &matType, const Pars3D *pars3D)
{
  // TODO: remove matType
  if (ResetNeeded()) return;

  const LightList noLights;
  const LightList *lights = &noLights;
  int cb;

  if (pars3D)
  {
    lights = pars3D->_lights;
    cb = pars3D->_cb;
  }
  else
  {
    Assert(!IsCBRecording());
    cb = -1;
  }
  BeginInstanceTL(cb,space, -1.0f, specFlags, *lights, DrawParameters::_default);
  SetBias(cb,0);

  // Clip the input polygon
  const int maxN = 32;
  Vertex2DFloat clippedVertices1[maxN]; // temporay buffer to keep clipped result
  Vertex2DFloat clippedVertices2[maxN]; // temporay buffer to keep clipped result
  Vertex2DFloat *free=clippedVertices1;
  Vertex2DFloat *used=clippedVertices2;
  for (int i = 0; i < nVertices; i++) used[i] = vertices[i];
  int n = nVertices;
  n = Clip2D(clip, free, used, n, InsideTopFloat, InterpolateVertexFloat); swap(free,used);
  n = Clip2D(clip, free, used, n, InsideBottomFloat, InterpolateVertexFloat); swap(free,used);
  n = Clip2D(clip, free, used, n, InsideLeftFloat, InterpolateVertexFloat); swap(free,used);
  n = Clip2D(clip, free, used, n, InsideRightFloat, InterpolateVertexFloat); swap(free,used);
  if (n < 3) return; // nothing to draw
  vertices = used;
  if (n > maxN)
  {
    n = maxN;
    Fail("Poly: Too much vertices");
  }
  nVertices = n;

  // Zero vertex and index buffer in the device, forget _iBufferLast and _vBufferLast.
  ForgetDeviceBuffers(cb);
  RendState &cbState = CBState(cb);

  // Lock the vertex buffer
  SVertex9 *vertex;
  //TODO:MC: optimize Lock to always use local memory when DONT_USE_DYNBUFFERS 
  int vOffset = _dynVBuffer.Lock(vertex, nVertices);
  if (vOffset >= 0)
  {
    // Scale and offset uv pair
    UVPair uv1[MAX_UV_SETS];
    UVPair uv0[MAX_UV_SETS];
    for (int i = 0; i < MAX_UV_SETS; i++)
    {
      uv1[i].u = 1;
      uv1[i].v = 1;
      uv0[i].u = 0;
      uv0[i].v = 0;
    }

    // Fill out the vertex buffer with data
    for (int i = 0; i < nVertices; i++)
    {
      const Vertex2DFloat &vs = vertices[i];
      Vector3P pos(vs.x, 0.0f, vs.y);
      vertex->pos = pos;
      CopyNormal(vertex->norm, Vector3Compressed::Up);
      UVPair uv; uv.u = vs.u; uv.v = vs.v;
      vertex->t0.u = toInt(((vs.u - 0) * 1) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
      vertex->t0.v = toInt(((vs.v - 0) * 1) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
      vertex++;
    }
    _dynVBuffer.Unlock();

    // Set texgen scale and offset
    SetTexGenScaleAndOffset(cb, uv1, uv0);

#ifdef DONT_USE_DYNBUFFERS
#else
    // Set the vertex buffer
    const ComRef<IDirect3DVertexBuffer9> &dbuf = _dynVBuffer.GetVBuffer();
    SetStreamSource(cb, dbuf, sizeof(SVertex9), _dynVBuffer._vBufferSize);
    cbState._vOffsetLast = vOffset;
#endif

    // Prepare triangle
    TLMaterial mat = matType;
    mat.diffuse = mat.diffuse*vertices[0].color;
    mat.ambient = mat.ambient*vertices[0].color;
    mat.emmisive = mat.emmisive*vertices[0].color;
    mat.specular = mat.specular*vertices[0].color;
    EngineShapeProperties prop;
    PrepareTriangleTL(cb, NULL, mip, TexMaterialLODInfo(NULL, 0), specFlags, mat, specFlags&DisableSun, prop);

    // Draw
    UVSource uv = UVTex;
    SetupSectionTL(
      cb, cbState._mainLight, FM_None, VSBasic, &uv, 1, true, STNone, false, 0, cbState._pointLightsCount, cbState._spotLightsCount,
      false, RMCommon, false, 0, NULL);
    SetupVSConstants(cb, 0, 0);
    SetupPSConstants(cb);

#ifdef _XBOX
    DoAssert(!_3DRenderingInProgress);
#endif

#ifdef DONT_USE_DYNBUFFERS
    // we will need to make a copy of data
    // DX11 TODO DRAW
    //Array<char> tmp((char *)&_dynVBuffer._geometry[vOffset],nVertices*sizeof(SVertex9));
    //CALL_CB(cb,DrawPrimitiveUP,D3DPT_TRIANGLEFAN, nVertices - 2, tmp, sizeof(SVertex9));
    // DX11 TODO DRAW - trianglefan no longer supported
    DrawPrimitive(cb, D3DPT_TRIANGLEFAN_OLD, vOffset, nVertices - 2);
#else
    CALL_CB(cb,DrawPrimitive,D3DPT_TRIANGLEFAN, cbState._vOffsetLast, nVertices - 2);
#endif
  }
}

bool EngineDD11::IsCursorSupported() const
{
  return true;
}

HCURSOR EngineDD11::GetCursor(Texture *cursorTex, float hsX, float hsY, PackedColor color)
{
  // first try to find cursor in cache
  CursorKey key;
  key._tex = cursorTex;
  key._color = color;
  int index = _cursors.FindKey(key);
  if (index != -1)
  {
    return _cursors[index]._handle;
  }

  // create new cursor
  Assert(cursorTex);
  if (!cursorTex)
    return NULL;
  
  Assert(dynamic_cast<TextureD3D11*>(cursorTex));
  TextureD3D11 *newCursorTex = static_cast<TextureD3D11 *>(cursorTex);
  newCursorTex->LoadHeaders();

  if (newCursorTex->Width(0)!=32 || newCursorTex->Height(0)!=32)
  {
    LogF("Cursor texture %s not 32x32",cc_cast(newCursorTex->GetName()));
  }

  // find level closest to 32x32 mipmap
  int level = 0;
  int areaWanted = 32*32;
  int nMipmaps = newCursorTex->NMipmaps();
  for (;level<nMipmaps; level++)
  {
    if (level>=newCursorTex->_smallLevel) break;
    int area = newCursorTex->Width(level)*newCursorTex->Height(level);
    if (area<=areaWanted) break;
  }

  // get mipmap data into system memory
  AutoArray<char, MemAllocDataStack<char,64*64*4> > mem;

  const PacLevelMem &mip = newCursorTex->_mipmaps[level];
  int w = mip._w;
  int h = mip._h;

  // only PacARGB8888 supported
  Assert(mip._dFormat == PacARGB8888);
  if (mip._dFormat != PacARGB8888)
    return NULL;

  int size = PacLevelMem::MipmapSize(mip._dFormat,mip._w,mip._h);
  Assert(size >= w*h*4 );
  if (size < w*h*4)
    size = w*h*4;

  mem.Realloc(size);
  mem.Resize(size);

  // get data from the texture source
  newCursorTex->_src->GetMipmapData(mem.Data(),mip,level,0);


  BITMAPV5HEADER bi;
  ZeroMemory(&bi,sizeof(BITMAPV5HEADER));
  bi.bV5Size           = sizeof(BITMAPV5HEADER);
  bi.bV5Width           = w;
  bi.bV5Height          = h;
  bi.bV5Planes = 1;
  bi.bV5BitCount = 32;
  bi.bV5Compression = BI_BITFIELDS;
  // The following mask specification specifies a supported 32 BPP
  // alpha format for Windows XP.
  bi.bV5RedMask   =  0x00FF0000;
  bi.bV5GreenMask =  0x0000FF00;
  bi.bV5BlueMask  =  0x000000FF;
  bi.bV5AlphaMask =  0xFF000000; 

  HDC hdc = GetDC(_hwndApp);

  // Create the DIB section with an alpha channel.
  void *lpBits = 0;
  HBITMAP hBitmap = CreateDIBSection(hdc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, 
    (void **)&lpBits, NULL, (DWORD)0);

  ReleaseDC(_hwndApp,hdc);

  // Set the alpha values for each pixel in the cursor so that
  // the complete cursor is semi-transparent.
  DWORD *destData= (DWORD*)lpBits;
  DWORD *srcData = (DWORD*)mem.Data();

  Color col(color);

  for (int x=0; x<w; ++x)
  {
    for (int y=0; y<h; ++y)
    {
      DWORD srcPixel = srcData[(h-y-1)*w + x];
      DWORD *destPixel = destData + y*w + x;

      DWORD a = (srcPixel >> 24) & 0xff;
      DWORD r = (srcPixel >> 16) & 0xff;
      r *= col.R();
      DWORD g = (srcPixel >> 8) & 0xff;
      g *= col.G();
      DWORD b = (srcPixel) & 0xff;
      b *= col.B();

      *destPixel = (a << 24) | (r << 16) | (g << 8) | b;
    }
  }

  // Create an empty mask bitmap.
  HBITMAP hMonoBitmap = CreateBitmap(w,h,1,1,NULL);

  ICONINFO ii;
  ii.fIcon = FALSE; 
  ii.xHotspot = hsX * w;
  ii.yHotspot = hsY * h;
  ii.hbmMask = hMonoBitmap;
  ii.hbmColor = hBitmap;

  // Create the alpha cursor with the alpha DIB section.
  HCURSOR hCursor = CreateIconIndirect(&ii);
  Assert(hCursor);

  DeleteObject(hBitmap);          
  DeleteObject(hMonoBitmap); 

  // add cursor to cache
  CursorInfo info;
  info._key = key;
  info._handle = hCursor;
  _cursors.Add(info);

  return hCursor;
}

void EngineDD11::SetCursor(Texture *cursor, int x, int y, float hsX, float hsY, PackedColor color, int shadow)
{
  if (cursor!=NULL) _cursorSet = true;

  _cursorX = x;
  _cursorY = y;

  if (cursor)
  {
    HCURSOR newCursor = 0;
    if (!_curCursor || cursor != _cursorTex || color != _cursorColor)
    {
      newCursor = GetCursor(cursor, hsX, hsY, color);
      Assert(newCursor);
    }

    _cursorTex = cursor;
    _cursorColor = color;

    if (newCursor && newCursor != _curCursor)
    {
      _curCursor = newCursor;
    }
    ::SetCursor(_curCursor);
    ShowDeviceCursor(true);
  }
  else
  {
    _curCursor = 0;
    ::SetCursor(NULL);
    ShowDeviceCursor(false);
  }
}

bool EngineDD11::IsCursorShown() const
{
  // if cursor is currently not shown, no need to clip
  return _cursorShown;
}

void EngineDD11::UpdateCursorPos(int x, int y)
{
  _cursorX = x;
  _cursorY = y;
}

void EngineDD11::CursorOwned(bool owned)
{
  _cursorOwned = owned;
  //DIAG_MESSAGE(5000,Format("Cursor %sowned",owned ? "" : "not "));
}


RECT EngineDD11::GetMonitorResolution()
{
  RECT rect;
  // in case detection is not executed, use some default values
  rect.left = 0;
  rect.top = 0;
  rect.right = 800;
  rect.bottom = 600;
#if !defined(_XBOX) && !defined(_X360SHADERGEN)
  if (_dxgiOutput)
  {
    DXGI_OUTPUT_DESC desc;
    HRESULT hr = _dxgiOutput->GetDesc(&desc);
    if (FAILED(hr))
    {
      Fail("Cannot get monitor desc!");
    }
    else
    {
      HMONITOR mon = desc.Monitor;
      MONITORINFO info;
      memset(&info, 0, sizeof(info));
      info.cbSize = sizeof(info);

      if (GetMonitorInfo(mon, &info))
      {
        rect.left = info.rcMonitor.left;
        rect.top = info.rcMonitor.top;
        rect.right = info.rcMonitor.right;
        rect.bottom = info.rcMonitor.bottom;
      }
    }
  }
#endif
  return rect;
}

void EngineDD11::FlushPushBuffers()
{
#if USE_PUSHBUFFERS
  if (!TextBankDD()) return;
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());

  for (VertexBufferD3D11LRUItem *vb = _vBufferLRU.First(); vb; vb=_vBufferLRU.Next(vb))
  {
    const Shape *shape = vb->_owner;
    if (!shape) continue;
    VertexBuffer *buf = shape->GetVertexBuffer();
    if (!buf) continue;
    buf->DiscardPushBuffers();
  }
#endif
}

void EngineDD11::EnableSunLight(int cb, EMainLight mainLight)
{
  CBState(cb)._mainLight = mainLight;
}

void EngineDD11::SetFogMode(int cb, EFogMode fogMode)
{
  CBState(cb)._fogMode = fogMode;
}

bool EngineDD11::BeginMeshTLInstanced(
                                     int cb, const Shape &sMesh, const EngineShapeProperties &prop, int spec, const LightList &lights, int bias, float minDist2, const DrawParameters &dp
                                     )
{
  if (ResetNeeded()) return false;


  Vector3 camTranslate;
  if (_renderingMode != RMShadowBuffer && GScene)
  {
    camTranslate = GScene->GetCamera()->Position();
  }
  else
  {
    // used for shadow buffer rendering - view matrix not changing at all
    camTranslate = CBState(cb)._invView.Position();  

  }

  // We need to use some common "model" space for all instances
  // naive approach is to use identity (common space is world space)
  // We want to avoid world space offsets, as they cause significant numerical error
  // when multiplying with "worldToView" matrix.
  // To avoid large positions we convert all matrices to be relative to camera position
  // See also other CAM_REL_SPACE occurrences
  // offset which will be applied to individual object matrices in SetupInstances
  BeginInstanceTL(cb,Matrix4(MTranslation,camTranslate), minDist2, spec, lights, dp);


  if (_renderingMode == RMShadowBuffer)
  {
    PrepareShadowOffset(cb, prop);
  }

  //////////////////////////////////////////////////////////////////////////

  // update if necessary
  VertexBufferD3D11 *vb = static_cast<VertexBufferD3D11 *>(sMesh.GetVertexBuffer());
  if (!vb || !vb->GetSharedV() || !vb->GetSharedI())
  {
    // this may happen when D3DERR_OUTOFVIDEOMEMORY or a similar error prevents VB creation
    return false;
  }

  // check the loaded vertex / index buffer
  VertexStaticData11::BufferFuture vBuffer = vb->GetSharedV()->_vBuffer;
#if _ARMA3_VERTEX_BUFFER_EX
  if (vBuffer.IsReady() && vBuffer.GetResult().GetBuffer() == NULL)
#else
  if (vBuffer.IsReady() && vBuffer.GetResult() == NULL)
#endif
  {
    sMesh.ReleaseVBuffer();
    return false;
  }
  IndexStaticData11::BufferFuture iBuffer = vb->GetSharedI()->_iBuffer;
  if (iBuffer.IsReady() && iBuffer.GetResult() == NULL)
  {
    sMesh.ReleaseVBuffer();
    return false;
  }

  // it should be added while preparing the rendering
  //Assert(vb->IsInList()); // note: the check is not MT safe, another thread may be updating

  vb->Lock(&sMesh);

  // Update also sets the _posScale and _posOffset values
  vb->Update(cb, NULL, sMesh, prop, VSDV_Instancing);

  return true;
}

int EngineDD11::FrameTime() const
{
  return GlobalTickCount()-_frameTime;
}

void EngineDD11::UnsetAllDiscardableResources()
{
  //TrapFor0x8000Scope lock("EngineDD11::UnsetAllDiscardableResources");
  // no HL or CB recording must be active at this point - we need to be sure we own all the device states
  Assert(!IsInCBScope());
  Assert(_backgroundD3D!=RecHighLevel);
  UnsetAllDiscardableResourcesFromCBState();
#if SAFE_RESOURCE_UNSET
  // when needed, perform the resource cleanup
  Device_UnsetAllDiscardableResources(_d3dDeviceNonRec);
  // we can release the resource only after they are unset from the device
#endif

  // unset textures from current render state
  for (int i=0; i<UsableSampleStages; ++i)
  {
    _currentRenderState._lastHandle[i] = 0;
    _currentRenderState._setHandle[i] = 0;
  }
}

void EngineDD11::UnsetAllDiscardableResourcesFromCBState()
{
  AssertLowLevelThreadAccess();
  // make sure we are not referencing any old textures any more
  for (int cb=0; cb<CBStateCount(); cb++)
  {
    RendState &cbState = _cbState[cb];
    for (int i = 0; i < lenof(cbState._lastHandle); i++)
    {
      cbState._lastHandle[i] = (TextureHandle11*)(-1);
    }
    cbState._vBufferLast = VertexStaticData11::BufferFuture(VertexStaticData11::BufferFuture::Null);
    cbState._iBufferLast = IndexStaticData11::BufferFuture(IndexStaticData11::BufferFuture::Null);
  }
}

#if SAFE_RESOURCE_UNSET
void Device_UnsetAllDiscardableResources(MyD3DDevice& dev)
{
  if (dev.IsNull()) return; // when not initialized yet, no need to unset
  // unset all textures on normal texture stages

  ID3D11ShaderResourceView *textures[EngineDD11::UsableSampleStages] = {0};
  dev->PSSetShaderResources(0, EngineDD11::UsableSampleStages, Array<TextureHandle11*>(textures, EngineDD11::UsableSampleStages));

  // unset IB
  dev->IASetIndexBuffer(0, DXGI_FORMAT_R16_UINT, 0);
  // unset VB
  ID3D11Buffer * buf = 0;
  UINT dummy = 0;
  dev->IASetVertexBuffers(0, 1, &buf, &dummy, &dummy);
}


#endif

static int GetLayerUsageMask(const TexMaterial *mat)
{
  if (!mat) return 0;
  int layerMask = mat->GetCustomInt();
  // TODO: verify stages conform to layer mask, or even better, gather layer mask from stages
  return layerMask;
}

void EngineDD11::DoSelectPixelShader(int cb, const TexMaterial *mat, int cratersCount)
{
  RendState &cbState = CBState(cb);
  Assert(cbState._pixelShaderSel != PSUninitialized);
  Assert(cbState._pixelShaderTypeSel != PSTUninitialized);
  if (cbState._pixelShaderSel < PSNone)
  {
    // Get the pixel shader
    PixelShaderID pixelShaderSel;
    if (cratersCount > 0)
    {
      DoAssert(cratersCount <= 14);
      // Use crater visualization shader
      pixelShaderSel = (PixelShaderID)(PSCrater1-1 + cratersCount);
    }
    else
    {
      // Use original shader
      pixelShaderSel = cbState._pixelShaderSel;
    }

    if (pixelShaderSel == PSSprite || pixelShaderSel == PSSpriteExtTi)
    {
      // Modify pixel shader of Sprites in case we don't have the depth buffer
#ifndef _XBOX
      TextureHandle11* tex = GetDepthAsTexture();
#else
      // TODO: implement depth for X360 sprites
      IDirect3DTexture9 * tex = NULL;
#endif
      if (!tex)
      {
        pixelShaderSel = PSSpriteSimple;
      }
      else
      {
        SetTexture(cb, 1, tex);
        SetFiltering(cb, 1, TFPoint, 0);
      }
    }

#if _ARMA3_REFRACTION_SPRITES
    if (pixelShaderSel == PSSpriteRefract)
    {
      // Modify pixel shader of Sprites in case we don't have the depth buffer
#ifndef _XBOX
      TextureHandle11 * tex = GetDepthAsTexture();
#else
      // TODO: implement depth for X360 sprites
      IDirect3DTexture9 * tex = NULL;
#endif
      if (!tex)
      {
        pixelShaderSel = PSSpriteRefractSimple;
      }
    }
#endif //_ARMA3_REFRACTION_SPRITES

    if (pixelShaderSel == PSVolCloud)
    {
      // Modify pixel shader of volumetic clouds in case we don't have the depth buffer
#ifndef _XBOX
      TextureHandle11* tex = GetDepthAsTexture();
#else
      // TODO: implement depth for X360 sprites
      IDirect3DTexture9 * tex = NULL;
#endif
      if (!tex)
      {
        pixelShaderSel = PSVolCloudSimple;
      }
      else
      {
        SetTexture(cb, 1, tex);
        SetFiltering(cb, 1, TFPoint, 0);
      }
    }

    PixelShaderID pixelShaderSelDepth = pixelShaderSel;
    PredicationMode pass = CurrentPredicationMode();
    if (_useAToC && !_thermalVision)
    {
      // TODO: on PC we require a different pixel shader in zpriming/dpriming passes
      if (pixelShaderSel == PSGrass && (_AToCMask & AToC_Grass) != 0)
      {
        pixelShaderSel = PSGrassAToC;
      }
      else if (pixelShaderSel == PSTree && (_AToCMask & AToC_TreeOld) != 0)
      {
        pixelShaderSel = PSTreeAToC;
      }
      else if (pixelShaderSel == PSTreeSN && (_AToCMask & AToC_TreeOld) != 0)
      {
        pixelShaderSel = PSTreeAToC;
      }
      else if (pixelShaderSel == PSTreeAdv && (_AToCMask & AToC_TreeNew) != 0)
      {
        pixelShaderSel = PSTreeAdvAToC;
      }
      else if (pixelShaderSel == PSTreeAdvSimple && (_AToCMask & AToC_TreeNew) != 0)
      {
        pixelShaderSel = PSTreeAdvSimpleAToC;
      }
      else if (pixelShaderSel == PSSuperHair && (_AToCMask & AToC_SuperHair) != 0)
      {
        pixelShaderSel = PSSuperHairAtoC;
      }
    }


    // If not using SSSM shadows, switch to SB shadow
    PixelShaderType pixelShaderTypeSel = cbState._pixelShaderTypeSel;
    if (!cbState._doSSSM)
    {
      if (cbState._pixelShaderTypeSel == PSTBasicSSSM)
      {
        pixelShaderTypeSel = IsSBEnabled() ? PSTShadowReceiver1 : PSTBasic;
      }
    }

    if (_renderingMode==RMShadowBuffer || cbState._tlActive==TLDisabled)
    {
      SetPixelShader(cb,_pixelShader[pixelShaderSel][pixelShaderTypeSel]);
    }
    else
    {
      // when priming, use a simplified pixel shader variant
      if (pixelShaderSelDepth!=pixelShaderSel)
      {
        if (BeginPredication<PrimingPass>(cb,pass))
        {
          SetPixelShader(cb,_pixelShader[pixelShaderSel][PSTAlphaOnly]);
          EndPredication(cb);
        }
        if (BeginPredication<DepthPass>(cb,pass))
        {
          SetPixelShader(cb,_pixelShader[pixelShaderSelDepth][PSTAlphaOnly]);
          EndPredication(cb);
        }
      }
      else
      {
        if (BeginPredication<PrimingPass,DepthPass>(cb,pass))
        {
          SetPixelShader(cb,_pixelShader[pixelShaderSel][PSTAlphaOnly]);
          EndPredication(cb);
        }
      }
      if (BeginPredication<RenderingPass>(cb,pass))
      {
        SetPixelShader(cb,_pixelShader[pixelShaderSel][pixelShaderTypeSel]);
        EndPredication(cb);
      }
#if _ARMA3_CAUSTICS
      if (BeginPredication<CausticsPass>(cb, pass))
      {
        SetPixelShader(cb,_pixelShader[PSCaustics][PSTBasic]);
        EndPredication(cb);
      }
#endif
    }


    if ((pixelShaderSel == PSWater) || (pixelShaderSel == PSShore) || (pixelShaderSel == PSShoreFoam))
    {
      SetPixelShaderConstantWater(cb);
    }
    else if (pixelShaderSel == PSWaterSimple)
    {
      SetPixelShaderConstantWaterSimple(cb);
    }
#if _ARMA3_REFRACTION
    else if (pixelShaderSel == PSRefract)
    {
      SetPixelShaderConstantsRefract(cb, cbState._matSpecular);
    }
#endif
#if _ARMA3_REFRACTION_SPRITES
    else if (pixelShaderSel == PSSpriteRefract || pixelShaderSel == PSSpriteRefractSimple )
    {
      SetPixelShaderConstantsRefractSprite(cb);
    }
#endif
    else if (pixelShaderSel == PSGlass)
    {
      // Environmental map color modificator
      LightSun *sun = GScene->MainLight();
      Color lighting = sun->SimulateDiffuse(0.5) * GetAccomodateEye() * _modColor * EnvMapScale;
      SetPixelShaderConstantF(cb,PSC_GlassEnvColor, (float*)&lighting, 1);

      // Material specular color
      SetPixelShaderConstantF(cb,PSC_GlassMatSpecular, (float*)&cbState._matSpecular, 1);
    }
    else if (pixelShaderSel == PSSuper || pixelShaderSel == PSSuperHair || pixelShaderSel == PSSuperHairAtoC)
    {
      SetPixelShaderConstantSuper(cb,cbState._matSpecular);
    }
#if _HELISIM_FAKE_NIGHT_LIGHTS
    else if (pixelShaderSel == PSSuperExt || pixelShaderSel == PSSuperAToC)
    {
      SetPixelShaderConstantSuper(cb,cbState._matSpecular);
      SetPixelShaderNightEmissiveConst(cb);
    }
#endif
    else if (pixelShaderSel == PSSkin)
    {
      // temporary solution: matForcedDiffuse & matEmissive used for skin effect params
      SetPixelShaderConstantSkin( cb, cbState._matAmbient, cbState._matDiffuse, cbState._matForcedDiffuse,
        cbState._matEmmisive, cbState._matSpecular );
    }
    else if (pixelShaderSel == PSCalmWater)
    {
      SetPixelShaderParamsCalmWater( cb );
    }
    else if( pixelShaderSel == PSTreeAdv || pixelShaderSel == PSTreeAdvTrunk || pixelShaderSel == PSTreeAdvSimple || pixelShaderSel == PSTreeAdvTrunkSimple 
      || pixelShaderSel == PSTreeAdvAToC || pixelShaderSel == PSTreeAdvSimpleAToC
      || pixelShaderSel == PSSuperHair || pixelShaderSel == PSSuperHairAtoC)
    {
      SetPixelShaderConstantTreeAdv( cb, cbState._matAmbient, cbState._matDiffuse, cbState._matForcedDiffuse,
        cbState._matEmmisive, cbState._matSpecular );

      SetPixelShaderConstantF(cb, PSC_TreeAdjust, _useAToC ? TreeAdjustParsAToC : TreeAdjustPars, 1);
    }
    else if (pixelShaderSel == PSTreeAToC)
    {
      SetPixelShaderConstantF(cb, PSC_TreeAdjust, _useAToC ? TreeAdjustParsAToCExt : TreeAdjustPars, 1);
    }
    else if (pixelShaderSel == PSTreePRT)
    {
      SetPixelShaderConstantTreePRT(cb,cbState._matEmmisive, cbState._matAmbient, cbState._matDiffuse);
    }
    else if (pixelShaderSel==PSTerrainX || pixelShaderSel==PSTerrainSNX || pixelShaderSel==PSTerrainSimpleX || pixelShaderSel==PSTerrainGrassX)
    {
      int layerMask = GetLayerUsageMask(mat);
      SetPixelShaderConstantLayers(cb,layerMask,mat);
    }
#if _ENABLE_REPORT
    else if (pixelShaderSel>=PSTerrainGrass1 && pixelShaderSel<=PSTerrainGrass15)
    {
      Fail("Old terrain shaders no longer supported for rendering")
    }
    else if (pixelShaderSel>=PSTerrain1 && pixelShaderSel<=PSTerrain15)
    {
      Fail("Old terrain shaders no longer supported for rendering")
    }
    else if (pixelShaderSel>=PSTerrainSimple1 && pixelShaderSel<=PSTerrainSimple15)
    {
      Fail("Old terrain shaders no longer supported for rendering")
    }
#endif

#if _ARMA3_CAUSTICS
    if (GScene && GScene->DrawCaustics())
      SetPixelShaderConstantCaustic(cb);
#endif

  }
  else
  {
    const ComRef<ID3D11PixelShader> empty;
    SetPixelShader(cb,empty);
  }
}

#if _ARMA3_CAUSTICS
void EngineDD11::SetPixelShaderConstantCaustic(int cb)
{
  PredicationMode pass = CurrentPredicationMode();
  if (BeginPredication<CausticsPass>(cb, pass))
  {
    if (GLandscape)
    {
      for (int i=0; i<=0; ++i)
      {
        Ref<Texture> causticsTexture;
        int causticsSampler = 0; 
        switch (i)
        {
        case 0:
          causticsTexture = GLandscape->GetCausticsTexture();
          causticsSampler = 14; 
          break;
        default:
          Assert(!"Invalid index for caustics texture!");
          break;
        }
        if (causticsTexture)
        {
          TextureD3D11* textureD9 = (TextureD3D11*)causticsTexture.GetRef();
          // LUKE TODO - setting of textures is NOT predicated, but this should work
          // we are setting caustic textures to higher sampler, so that the 0 texture does not change so much
          SetTexture(cb, causticsSampler, textureD9->GetHandle(false));
          D3DSetSamplerState( cb, causticsSampler, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
          D3DSetSamplerState( cb, causticsSampler, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_WRAP_OLD );
          D3DSetSamplerState( cb, causticsSampler, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_WRAP_OLD );
          D3DSetSamplerState( cb, causticsSampler, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
          D3DSetSamplerState( cb, causticsSampler, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
          D3DSetSamplerState( cb, causticsSampler, D3DSAMP_MIPFILTER_OLD, D3DTEXF_LINEAR_OLD);
        }
      }
    }
    EndPredication(cb);
  }

  D3DXVECTOR4 causticsPars[CAUSTICS_PARS_SIZE];

  if (GLandscape)
  {
    int numRows = GLandscape->GetCausticsTextureNumRows();
    Assert(numRows > 0);
    if (numRows <= 0)
      numRows = 1;

    int current = GLandscape->GetCausticsTextureCurrent();
    int curX = current % numRows;
    int curY = current / numRows;

    float area = GLandscape->GetCausticsTextureArea();
    Assert(area > 0);
    if (area <= 0)
      area = 1.0f;

    float areaDeep = GLandscape->GetCausticsTextureAreaDeep();
    Assert(areaDeep > 0);
    if (areaDeep <= 0)
      areaDeep = 1.0f;

    float depthFadeCoef = GLandscape->GetCausticsDepthFadeCoef();
    Assert(depthFadeCoef >= 0);
    if (depthFadeCoef < 0)
      depthFadeCoef = 1.0f;

    float depthGranularity = GLandscape->GetCausticsTextureDepthGranularity();
    Assert(depthGranularity > 0);
    if (depthGranularity <= 0)
      depthGranularity = 1.0f;

    float brightnessCoef = GLandscape->GetCausticsBrightnessCoef();
    Assert(brightnessCoef > 0);
    if (brightnessCoef <= 0)
      brightnessCoef = 1.0f;

    causticsPars[0] = D3DXVECTOR4(
      area,
      areaDeep,
      depthFadeCoef,      
      Glob.time.toFloat()
      );

    causticsPars[1] = D3DXVECTOR4(
      GLandscape->GetCausticsDistanceLimit(), 
      GLandscape->GetCausticsDepthLimit(), 
      GLandscape->GetSeaLevelNoWave(),
      brightnessCoef
      );

    causticsPars[2] = D3DXVECTOR4(
      1.0f / numRows,
      curX,
      curY, 
      depthGranularity
      );
  }
  else
  {
    // set some default values
    causticsPars[0] = D3DXVECTOR4(1, 1, 1,Glob.time.toFloat());
    causticsPars[1] = D3DXVECTOR4(200.0f, 100.0f, 0.0f, 0.2f);
    causticsPars[2] = D3DXVECTOR4(1, 1, 1, 1);
  }

  SetPixelShaderConstantF(cb, PSC_CausticsPars, (float*)causticsPars, CAUSTICS_PARS_SIZE);
}
#endif

void EngineDD11::SetTexture(int cb, int stage, TextureHandle11* tex, bool optimize)
{
  RendState &cbState = CBState(cb);
  if (!optimize || cbState._lastHandle[stage]  != tex)
  {
    CALL_CB_ENGINE(cb, SetTexture, stage, tex);
    cbState._lastHandle[stage]  = tex;
  }
#ifndef _XBOX
    // on Xbox textures exist in memory, render targets in EDRAM, there can be no conflict between them
    DoAssert(!tex || tex!=_currRenderTarget._srView);
#endif
}

void EngineDD11::SetTextureDirect(int cb, int stage, TextureHandle11* tex, bool optimize)
{
  // we cannot optimize, because we don't know if _lastHandle was already set to GPU
  RendState &cbState = CBState(cb);
  CALL_CB_ENGINE(cb,SetTextureDirect,stage,tex);
  cbState._lastHandle[stage] = tex;
}

void EngineDD11::DoSetTextureDirect(int stage, TextureHandle11* tex)
{
  AssertSpecialLevelThreadAccess();
  Assert(stage >= 0 && stage < MaxSamplerStages);
  if (_currentRenderState._setHandle[stage] != tex)
  {
    TextureHandle11* texArray[1] = {tex};
    _d3dDeviceRec->PSSetShaderResources(stage, 1, Array<TextureHandle11 *>(texArray, 1));
  }
  _currentRenderState._lastHandle[stage] = tex;
  _currentRenderState._setHandle[stage] = tex;
}

void EngineDD11::DoSetTexture(int stage, TextureHandle11* tex)
{
  AssertSpecialLevelThreadAccess();
  Assert(stage >= 0 && stage < MaxSamplerStages);
  _currentRenderState._lastHandle[stage] = tex;
}

void EngineDD11::SetPixelShader(int cb, ID3D11PixelShader *shader, bool optimize)
{
  // DX11 TODO
  //if (!CHECK_ENG_DIAG(EDENoSetupPS))
  {
    RendStateExt &cbState = CBState(cb);
    // check if we match in all currently active branches, if we do, we can skip
    if (!optimize || CheckPSNeedsToBeSet(shader,cbState,cbState._rec._currPredication))
    {
      CALL_CB_ENGINE(cb,SetPixelShader,shader);
    }
  }
}

void EngineDD11::DoSetPixelShader(ID3D11PixelShader *shader)
{
  AssertSpecialLevelThreadAccess();
  if (_currentRenderState._lastPixelShader != shader)
  {
    _d3dDeviceRec->PSSetShader(shader);
    _currentRenderState._lastPixelShader = shader;
  }
}

void EngineDD11::SelectPixelShader(int cb, PixelShaderID ps)
{
  CBState(cb)._pixelShaderSel = ps;
#if _ENABLE_REPORT
  // Log the irradiance shader if present
  switch (ps)
  {
  case PSNormalMap:
  case PSNormalMapMacroAS:
    RPTSTACK();
    RptF("Info: Irradiance map used - shaders with irradiance maps are obsolete");
  }
#endif
}
void EngineDD11::SelectPixelShaderType(int cb, PixelShaderType pixelShaderType)
{
  if (_thermalVision /*&& !_tiVisualizationDone*/)
  {
    // Convert pixel shared type to corresponding thermal type
    switch (pixelShaderType)
    {
    case PSTBasic:
    case PSTBasicSSSM:
    case PSTShadowReceiver1:
      CBState(cb)._pixelShaderTypeSel = PSTThermal;
      break;
    default:
      CBState(cb)._pixelShaderTypeSel = pixelShaderType;
    }
  }
  else
  {
    CBState(cb)._pixelShaderTypeSel = pixelShaderType;
  }
}

void EngineDD11::EndHighLevelRecording()
{
  bool part1Result = EndHighLevelRecordingPart1();
  EndHighLevelRecordingPart2(part1Result);
}

void EngineDD11::PreReset(bool hard, bool resizeTargets, bool setFullscreenState)
{
  // if reset is not really need, we might display some nice wait screen here
  // this may happen with user initiated Reset (resolution switch...)
  EndHighLevelRecording();
  EndBackgroundScope(true,"reset");

  DisplayWaitScreen();

  EndHighLevelRecording();
  EndBackgroundScope(true,"reset");

  // flush any rendering instructions which might still be pending
  FreeAllQueues();

  Assert(!IsCBRecording());
  UnsetAllResources();

  // destroy all video memory surfaces
  LogF(
    "Releasing vertex buffers: %d, %d, %d",
    _vBuffersAllocated+_iBuffersAllocated,MemoryControlled(),MemoryControlledRecent()
    );
  _textBank->DoneDetailTextures();
  _textBank->ReleaseAllTextures(hard); // release all textures
  // release all vertex buffers
  ReleaseAllVertexBuffers();
  _vBufferToRelease.Clear();

  DeInitResources(false);
  if (hard)
  {
    DestroyPostProcessStuff();
    DeInitStateLists();
    DeInitShaders();
    DeInitD3D();    
    DeInitDXGI();
  }
  else
  {
    DestroyPostProcessStuff();

    InitDXGIOutput(); //just to be sure we have current output
    if (!_d3dDeviceRec.IsNull() && _dxgiOutput)
    {
      if (setFullscreenState)
      {
        //_swapChainDesc.BufferDesc should be already filled with right values
        HRESULT hr = _d3dDeviceRec->ResizeTarget(&_swapChainDesc.BufferDesc);
        if (FAILED(hr)) 
        {
          DDError11("ResizeTarget failed:", hr);
        }

        // resize buffers
        hr = _d3dDeviceRec->ResizeBuffers(_swapChainDesc.BufferCount,
          _swapChainDesc.BufferDesc.Width, _swapChainDesc.BufferDesc.Height,
          _swapChainDesc.BufferDesc.Format, _swapChainDesc.Flags);
        if (FAILED(hr))
        {
          DDError11("ResizeBuffers failed:", hr);
        }

        // set fullscreen state
        if (_windowed)
          hr = _d3dDeviceRec->SetFullscreenState(FALSE, NULL);
        else
          hr = _d3dDeviceRec->SetFullscreenState(TRUE, _dxgiOutput);
        if (FAILED(hr))
        {
          DDError11("SetFullscreenState failed:", hr);
        }
      }
      else
      {
        if (resizeTargets)
        {
          // resize backbuffer
          HRESULT hr = _d3dDeviceRec->ResizeBuffers(_swapChainDesc.BufferCount,
            _swapChainDesc.BufferDesc.Width, _swapChainDesc.BufferDesc.Height,
            _swapChainDesc.BufferDesc.Format, _swapChainDesc.Flags);
          if (FAILED(hr))
          {
            DDError11("ResizeBuffers failed:", hr);
          }
        }
      }
    }
  }

  Assert(_iBuffersAllocated==0);
  Assert(_vBuffersAllocated==0);
  //Assert(_rtAllocated==0);
  _caps._rtAllocated = 0;
}

void EngineDD11::PostReset(bool hard)
{
  _wRT = _wRTRounded;
  _hRT = _hRTRounded;

#if 0 //_ENABLE_CHEATS
  PrintVMMap(1,"PostReset Start");
#endif

  // Config loading
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  ParseUserParams(cfg, &globals);
  ParamFile fcfg;
  ParseFlashpointCfg(fcfg);

  if (hard)
  {
    InitDXGI();
    InitD3D();
  }

  InitDXGIOutput();
  
  // initialize resources
  InitResources(cfg, fcfg);

  if (hard)
  {
    InitShaders(false);
  }
  else
  {
#if _ENABLE_COMPILED_SHADER_CACHE
    RString cacheName = ShaderCacheName("PP");
    ShaderCompileCache cacheInit(SourcesPP, cacheName, RStringB(HLSLCacheDir));
#endif
    //if (!CreatePostProcessStuff()) fail = true;
    CreatePostProcessStuff();
  }

  InitD3DState();

  CheckLocalNonlocalTextureMemory();

  if (_sbQuality>0)
  {
    CreateSBResources();
  }

  _dbQuality = _postFxQuality>1 ? 1 : 0;

  _textBank->InitDetailTextures();

  CheckLocalNonlocalTextureMemory();
  // TODO: reset fog
  // frames took very long, but we need to ignore it
  ResetTimingHistory(50);
  _resetDone = true;
  _resetFrame = 0;
  _resetDirty = 0;
  _resetStatus = D3D_OK;
  _resetRequested = false;

  _cursorSet = false;
  _cursorShown = false;
#if 0 //_ENABLE_CHEATS
  PrintVMMap(1,"PostReset End");
#endif
}

bool EngineDD11::Reset(bool hard, bool resizeTargets, bool setFullscreenState)
{
  Assert(CheckMainThread());

  Debugger::PauseCheckingScope scope(GDebugger);

  bool inScene = InitDrawDone();
  if (inScene) FinishDraw(1);

  LogF("Reset to resolution %dx%d", _swapChainDesc.BufferDesc.Width,_swapChainDesc.BufferDesc.Height);

  PreReset(hard, resizeTargets, setFullscreenState);
  LogF("*** Reset done");
  // Recreate all surfaces
  PostReset(hard);

  if (inScene) InitDraw();
  return true;
}


inline EngineDD11::D3DCommandBufRecWithSubmit EngineDD11::RecordHighlevel()
{
  Assert (_backgroundD3D==RecHighLevel); // we must be in the correct recorning mode
  Assert (CheckMainThread()); // recording must be done from the main thread
  return _cbState[0]._rec;
}

inline void EngineDD11::KickOffHighLevel()
{
#if _DEBUG
  if (Glob.config._renderThread==Config::ThreadingEmulated)
  {
    // to allow easier debugging kick off each instruction immediately
    EndBackgroundScope(true,"hlDbg");
    return;
  }
#endif
  // check if there is enough to warrant a kick-off
  // without this check _results space was frequently exhausted
#if _DEBUG
  const int kickOffLimitBytes = 0; // keep this below D3DCommandBufRec to prevent dynamic allocation
#else
  const int kickOffLimitBytes = 12*1024; // keep this below D3DCommandBufRec to prevent dynamic allocation
#endif
  if (_cbState[0]._rec.GetDataSize()>kickOffLimitBytes)
  {
    SubmitBackground(_bg,true,true,CleanUpNone,"hlRec");
  }
}

bool EngineDD11::CheckVSNeedsToBeSet(ID3D11VertexShader *shader, RendStateExt &cbState, int mask)
{
  bool diff = false;
  for (int i=0; i<NPredicationModes; i++)
  {
    if ((mask&(1<<i)) && shader!=cbState._vertexShaderLast[i])
    {
      cbState._vertexShaderLast[i] = shader;
      diff = true;
    }
  }
  return diff;
}
bool EngineDD11::CheckPSNeedsToBeSet(ID3D11PixelShader *shader, RendStateExt &cbState, int mask)
{
  bool diff = false;
  for (int i=0; i<NPredicationModes; i++)
  {
    if ((mask&(1<<i)) && shader!=cbState._pixelShaderLast[i])
    {
      cbState._pixelShaderLast[i] = shader;
      diff = true;
    }
  }
  return diff;
}
bool EngineDD11::CheckRSNeedsToBeSet(RendStateExt &cbState, int index, DWORD value, int mask)
{
  bool diff = false;
  for (int i=0; i<NPredicationModes; i++)
  {
    if ((mask&(1<<i)) && cbState._renderState[i][index]!=value)
    {
      cbState._renderState[i][index]=value;
      diff = true;
    }
  }
  return diff;
}

#if _ARMA3_PREDICATED_SAMPLER_STATES
bool EngineDD11::CheckSSNeedsToBeSet(RendStateExt &cbState, int stage, int index, DWORD value, int mask)
{
  bool diff = false;
  for (int i=0; i<NPredicationModes; i++)
  {
    if ((mask&(1<<i)) && cbState._samplerState[i][stage][index]!=value)
    {
      cbState._samplerState[i][stage][index]=value;
      diff = true;
    }
  }
  return diff;
}
#endif

DWORD EngineDD11::GetRSValue(int index)
{
  Assert(index >= 0 && index < _currentRenderState._renderState.Size());
  return _currentRenderState._renderState[index];
}

size_t EngineDD11::UsedVRAM() const
{
  return _vBuffersAllocated;
}

inline void EngineDD11::SetVertexShaderConstantSAFR(int cb, float matPower, Color matAmbient)
{
  float value[4]={
    matPower, matAmbient.A(), CBState(cb)._fogEnd, 1.0f / (CBState(cb)._fogEnd - CBState(cb)._fogStart)
  };
  SetVertexShaderConstantF(cb,VSC_SpecularPower_Alpha_FogEnd_RCPFogEndMinusFogStart, value, 1);
}

inline void EngineDD11::SetVertexShaderConstantFFAR(int cb)
{
  if (_renderingMode==RMShadowBuffer) return;
  float value[4]={
    0,CBState(cb)._expFogCoef, CBState(cb)._aFogEnd,
    CBState(cb)._aFogEnd > CBState(cb)._aFogStart ? 1.0f / (CBState(cb)._aFogEnd - CBState(cb)._aFogStart) : 1.0f
  };
  SetVertexShaderConstantF(cb,VSC_Free_ExpFog_AFogEnd_RCPAFogEndMinusAFogStart, value, 1);
}

inline void EngineDD11::SetVertexShaderConstantLWSM(int cb)
{
  // Calculate the local world space matrix
  Matrix4 localSpace;
  localSpace.SetOrientation(M3Identity);
  localSpace.SetPosition(_sceneProps->GetCameraPosition());
  Matrix4 modelToLWS = localSpace.InverseRotation() * CBState(cb)._world;

  // Pass the LWS matrix to the shader
  PASSMATRIXTYPE lwsMatrix;
  ConvertMatrixTransposed(lwsMatrix, modelToLWS);
  SetVertexShaderConstantF(cb,VSC_LWSMatrix, (float*)&lwsMatrix, 3);
}

inline void EngineDD11::SetVertexShaderConstantCalmWater( int cb, const RendState &cbState )
{
  // get light dir in object space
  const Vector3 mainLightDirO = -TransformAndNormalizeVectorToObjectSpace( cb, cbState._direction );
  const Color& mainLightColor = cbState._lightDiffuse;
  const Color& skyLightColor = cbState._lightAmbient; // * Color( 0.9, 1.0, 1.1, 0.0 );

  // Saturate the result by SunObjectColor + SunHaloObjectColor (to reflect no more than them)
  LightSun *sun = GScene->MainLight();
  static float sunBrightnessCoef = 2.0f;
  float sunBrightness = (sun->SunObjectColor().Brightness() + sun->SunHaloObjectColor().Brightness()) * GetAccomodateEye().Brightness() * _modColor.Brightness() * sunBrightnessCoef;
  float specularCoef = 10; // just try some value // 20
  float specularCoefSat = floatMin(specularCoef, sunBrightness);
  // Calculate specular color (which includes specular light from the Sun only, not from the environment)
  Color specular = CBState(cb)._lightSpecular * Color(specularCoefSat, specularCoefSat, specularCoefSat, 1.0f) * cbState._matAmbient.A() * 2.0f; //  * matSpecular * 2.0

  // Setup the camera position
  Camera *camera = GScene->GetCamera();
  Vector3 cameraPosition = camera->Position();
  Vector3 cameraObjectPosition = TransformPositionToObjectSpace( cb, cameraPosition );
  D3DXVECTOR4 cpos(cameraObjectPosition.X(), cameraObjectPosition.Y(), cameraObjectPosition.Z(), 1.0f);
  SetVertexShaderConstantF( cb, VSC_CameraPosition, (float*)&cpos, 1 );

  const float kc = 2.0f; // mul param for RGB of material colors (they are halved)

  // params for calm water
  //static SCalmWaterIn cwmp;
  SCalmWaterIn &cwmp = GetCalmWaterInPars();
  cwmp.skyLightColor[0] = skyLightColor.R();
  cwmp.skyLightColor[1] = skyLightColor.G();
  cwmp.skyLightColor[2] = skyLightColor.B();

  cwmp.mainLightDir[0] = mainLightDirO.X();
  cwmp.mainLightDir[1] = mainLightDirO.Y();
  cwmp.mainLightDir[2] = mainLightDirO.Z();

  cwmp.mainLightColor[0] = mainLightColor.R();
  cwmp.mainLightColor[1] = mainLightColor.G();
  cwmp.mainLightColor[2] = mainLightColor.B();

  cwmp.mainLightSpecular[0] = specular.R();
  cwmp.mainLightSpecular[1] = specular.G();
  cwmp.mainLightSpecular[2] = specular.B();

  cwmp.waterColorR = cbState._matAmbient.R() * kc; // 0.2
  cwmp.waterColorG = cbState._matAmbient.G() * kc; // 0.3
  cwmp.waterColorB = cbState._matAmbient.B() * kc; // 0.4

  cwmp.cameraPositionX = cameraPosition.X();
  cwmp.cameraPositionZ = cameraPosition.Z();
  cwmp.specularPower = cbState._matPower;

  // get weather from sea
  float waveXScale, waveZScale; //,waveHeight;
  _sceneProps->GetSeaWavePars( waveXScale, waveZScale, cwmp.weather[3] );

  ComputeCalmWaterPars( this, &cwmp, cbState._matAmbient, cbState._matDiffuse, cbState._matForcedDiffuse, cbState._matEmmisive, cbState._matSpecular, cbState._matPower );
  SetVertexShaderConstantF( cb, VSC_WaterPars, GetCalmWaterPars(), CALMWATER_PARS_SIZE );

  // Point and spot lights
  //SetupVSConstantsPSLights(cb);
}

bool EngineDD11::SetShaderConstantF(int cb, const ShaderConstant& constant, const float *data, int floatCount, bool optimize, int additionalFloat4Offset)
{
  PROFILE_DX_SCOPE_DETAIL(setShdrCnst);
  RendStateExt& cbState = CBState(cb);

  if (cbState._shaderConsts.SetShaderConstantF(constant, data, floatCount, additionalFloat4Offset))
  {
    // constant changed
    // we can pass constant pointer - it is safe, because constant lives to the end of the program
    CALL_CB_ENGINE(cb, SetShaderConstantF, &constant, floatCount, additionalFloat4Offset, Array<float>((float*)data, floatCount));
    return true;
  }
  return false;
}

void EngineDD11::DoSetShaderConstantF(const ShaderConstant* constant, int floatCount, int additionalFloat4Offset, const Array<float>& data)
{
  AssertSpecialLevelThreadAccess();
  Assert(constant);
  Assert(floatCount == data.Size());
  if (constant)
    _currentRenderState._shaderConstants.SetShaderConstantF(*constant, data.Data(), floatCount, additionalFloat4Offset, true); 
}

bool EngineDD11::SetShaderConstantI(int cb, const ShaderConstant& constant, const int *data, int floatCount, bool optimize)
{
  // DX11 TODO
  // horrible hack!
  static const int maxCount = 256*4;
  float consts[maxCount];
  DoAssert(floatCount < maxCount);
  if (floatCount > maxCount)
    return false;
  for (int i=0; i< floatCount; ++i)
  {
    consts[i] = data[i];
  }
  return SetShaderConstantF(cb, constant, (const float *)consts, floatCount);
}

bool EngineDD11::SetShaderConstantB(int cb, const ShaderConstant& constant, const bool *data, int boolCount, bool optimize)
{
  // DX11 TODO
  // horrible hack!
  static const int maxCount = 256;
  float consts[maxCount];
  DoAssert(boolCount < maxCount);
  if (boolCount > maxCount)
    return false;
  for (int i=0; i< boolCount; ++i)
  {
    consts[i] = data[i] ? 1:0;
  }
  return SetShaderConstantF(cb, constant, consts, boolCount);
}

/**
@param cb CB ID - see COMMAND_BUFFERS
*/
bool EngineDD11::SetVertexShaderConstantF(int cb, const ShaderConstant& constant, const float *data, int vector4fCount, bool optimize, int additionalFloat4Offset)
{
  return SetShaderConstantF(cb, constant, data, vector4fCount*4, optimize, additionalFloat4Offset);
}

bool EngineDD11::SetVertexShaderConstantI(int cb, const ShaderConstant& constant, const int *data, int vector4fCount, bool optimize)
{
  return SetShaderConstantI(cb, constant, data, vector4fCount*4, optimize);
}

bool EngineDD11::SetVertexShaderConstantB(int cb, const ShaderConstant& constant, const bool *data, int boolCount, bool optimize)
{
  return SetShaderConstantB(cb, constant, data, boolCount, optimize);
}

bool EngineDD11::SetPixelShaderConstantF(int cb, const ShaderConstant& constant, const float *data, int vector4fCount, bool optimize, int additionalFloat4Offset)
{
  return SetShaderConstantF(cb, constant, data, vector4fCount*4, optimize, additionalFloat4Offset);
}

bool EngineDD11::SetPixelShaderConstantI(int cb, const ShaderConstant& constant, const int *data, int vector4fCount, bool optimize)
{
  return SetShaderConstantI(cb, constant, data, vector4fCount*4, optimize);
}

bool EngineDD11::SetPixelShaderConstantB(int cb, const ShaderConstant& constant, const bool *data, int boolCount, bool optimize)
{
  return SetShaderConstantB(cb, constant, data, boolCount, optimize);
}

void EngineDD11::SetBones(int cb, int minBoneIndex, const Matrix4 *bones, int nBones)
{
  // Set the view matrices and matrix offset
  if (nBones > 0)
  {
    // Set the view matrices
    MatrixFloat34 matrices[EngineDD11::LimitBones];
    const int vectorsNum = sizeof(matrices[0])/4/4;
    DoAssert(vectorsNum == 3); // Currently we use 3 vectors
    DoAssert(minBoneIndex >= 0);
    DoAssert(nBones <= _caps._maxBones);
    DoAssert(nBones <= LimitBones);
    if (nBones>_caps._maxBones) nBones = _caps._maxBones;
    for (int i = 0; i < nBones; i++)
    {
      ConvertMatrixTransposed3(matrices[i], bones[minBoneIndex + i]);
    }
    SetVertexShaderConstantF(cb,VSC_SkinningInstancingData, (float*)matrices, nBones * vectorsNum);
  }

  // Set the matrix offset
  const float offset = minBoneIndex / 255.0f;
  D3DXVECTOR4 vector4(offset, offset, offset, offset);
  SetVertexShaderConstantF(cb,VSC_MatrixOffset, vector4, 1);
}

bool EngineDD11::SetFillrateControlWanted(ResolutionInfo info)
{
  // find area best fitting the _wRTPreferred x _hRTPreferred
  int wRT = info.w;
  int hRT = info.h;
  // keep some reasonable resolution of the render target
  if (wRT<4) wRT = 4;
  if (hRT<4) hRT = 4;

  if (FindFillrateControl(wRT,hRT))
  {
    _wRTPreferred = wRT;
    _hRTPreferred = hRT;
    return true;
  }
  return false;
}

void EngineDD11::DoFinishRendering()
{
  if (_3DRenderingInProgress) End3DRendering();

  CloseAllQueues();
  _d3dFrameOpen=false;

  D3DEndScene();
  // Do the final post process  (copying data to back buffer)
  // note: it should mostly do nothing, as data should be already there
  if (_pp._final)
  {
    _pp._final->Do(true);
  }
}

void EngineDD11::D3DEndScene()
{
  //CALL_D3D(EndScene,);
}
void EngineDD11::D3DBeginScene()
{
  LogTexture("XXX Start XXX",NULL);
  //CALL_D3D(BeginScene,);
}

void EngineDD11::SwitchRecordingToHighLevel()
{
  if (_backgroundD3D==RecLowLevel)
  {
    if (_hlRecording)
    {
      // submit any low-level instructions which may be still waiting
      SubmitBackground(_bg,true,true,CleanUpNeeded,"swiHL");
      _backgroundD3D = RecHighLevel;
    }
    else
    {
      EndBackgroundScope(true,"swiHL");
    }
  }
}

bool EngineDD11::EndHighLevelRecordingPart1()
{
  // we use this to terminate both high- and low- level recording 
  if(_backgroundD3D!=RecNone)
  {
    Assert(CheckMainThread());
    // wait until instructions are executed
    // we need to flush

    PROFILE_SCOPE_EX(drwBg,dd11);
    FlushBackground(_bg,true,"endHL",true);

    // why we call DoPresentBackBuffer here? - see BackToFront function
    if (_hlPresentRequired)
    {
      DoPresentBackbuffer();
      _hlPresentRequired = false;
    }

    // problem here: we cannot set CB state, as high-level instructions in the background thread may be still using it 
    //UnsetAllDiscardableResourcesFromCBState();
    return true;
  }
  return false;
}

void EngineDD11::EndHighLevelRecordingPart2(bool part1Result)
{
  if (part1Result)
  {
    Assert(_backgroundD3D == RecNone);
#ifdef _XBOX
    _d3DDevice->AcquireThreadOwnership();
#endif
    BGReleaseResourcesAndTextures();
    BGReleaseRefs();
  }
  UnsetAllDiscardableResources();
}

void EngineDD11::DoSetMaterial(int cb, const TLMaterial &mat)
{
  // set corresponding render states
  // combine material with light


  //PROFILE_DX_SCOPE(3dmat);

#if DO_TEX_STATS
  if (LogStatesOnce)
  {
    LogF
      (
      "Set material da %.2f, aa %.2f, em %.2f,%.2f,%.2f",
      mat.diffuse.A(),mat.ambient.A(),
      mat.emmisive.R(),mat.emmisive.G(),mat.emmisive.B()
      );
  }
#endif

  // Combine material color and light color
  SetupMaterialLights(cb,mat);
}

void EngineDD11::DoPrepareTriangle(
                                  int cb, TextureD3D11 *tex, const TexMaterialLODInfo &mat, int level, int spec, const EngineShapeProperties &prop
                                  )
{
  PROFILE_SCOPE_EX(prepT, dd11);

  bool clampU=false,clampV=false;

  RendState &cbState = CBState(cb);
  //Assert( (spec&(NoClamp|ClampU|ClampV))!=0 );
  // all triangles are marked for clamping
  if( spec&ClampU ) clampU=true;
  if( spec&ClampV ) clampV=true;
  D3DTEXTUREADDRESS_OLD addressU=( clampU ? D3DTADDRESS_CLAMP_OLD : D3DTADDRESS_WRAP_OLD );
  D3DSetSamplerState(cb, 0, D3DSAMP_ADDRESSU_OLD, addressU);
  D3DTEXTUREADDRESS_OLD addressV=( clampV ? D3DTADDRESS_CLAMP_OLD : D3DTADDRESS_WRAP_OLD );
  D3DSetSamplerState(cb, 0, D3DSAMP_ADDRESSV_OLD, addressV);
  D3DSetSamplerState(cb, 0, D3DSAMP_ADDRESSW_OLD, D3DTADDRESS_CLAMP_OLD);

  int specFlags=spec&
    (
    NoZBuf|NoZWrite|NoAlphaWrite|NoStencilWrite|
    IsAlphaFog|
    IsShadow|IsShadowVolume|ShadowVolumeFrontFaces|
    IsAlpha|IsTransparent|
    DstBlendOne|DstBlendZero|FilterMask|
    NoColorWrite
    );

  TexMaterial::Loaded m(mat._mat);
  if (mat._mat)
  {
    if (m.SomeRenderFlags())
    {
      if (m.GetRenderFlag(RFNoZWrite)) specFlags |= NoZWrite;
      if (m.GetRenderFlag(RFNoColorWrite)) specFlags |= NoColorWrite;
      if (m.GetRenderFlag(RFNoAlphaWrite)) specFlags |= NoAlphaWrite;
      if (m.GetRenderFlag(RFAddBlend)) specFlags |= DstBlendOne;
      if (m.GetRenderFlag(RFNoTiWrite) && GEngine->GetThermalVision()) specFlags |= (IsHidden | NoZWrite);
      // alpha test is used to force alpha testing with continuous alpha texture
      if (m.GetRenderFlag(RFAlphaTest64))
      {
        specFlags = (specFlags&~IsAlpha)|IsTransparent;
      }
      else if (m.GetRenderFlag(RFAlphaTest32))
      {
        specFlags = (specFlags&~IsAlpha)|IsTransparent;
      }
      else if (m.GetRenderFlag(RFAlphaTest128))
      {
        specFlags = (specFlags&~IsAlpha)|IsTransparent;
      }
    }
  }

  if (cbState._lastMat!=mat || cbState._texGenScaleOrOffsetHasChanged)
  {
    cbState._lastMat = mat;

    SetupMaterialTextures(mat, cb, prop, specFlags);

    // verify all reasons to change are resolved now
    Assert(!cbState._texGenScaleOrOffsetHasChanged);

  }

  bool aToC = false;

  // Label: AlphaToCoverage
  // foliage and grass requires alpha to coverage
  // TODO: on PC aToC should not be done in zPriming pass - predication needed
  // this means a different ref value, and different pixel shader
  if (_useAToC && !_thermalVision)
  {
    if (mat._mat)
    {
      PixelShaderID ps = m.GetPixelShaderID();
      switch (ps)
      {
      case PSGrass: aToC = (_AToCMask & AToC_Grass) != 0; break;

      case PSTree: aToC = (_AToCMask & AToC_TreeOld) != 0; break;          
      case PSTreeSimple: aToC = (_AToCMask & AToC_TreeOld) != 0; break;      
      case PSTreeSN: aToC = (_AToCMask & AToC_TreeOld) != 0; break;

      case PSTreeAdv: aToC = (_AToCMask & AToC_TreeNew) != 0; break;
      case PSTreeAdvSimple: aToC = (_AToCMask & AToC_TreeNew) != 0; break;
      case PSSuperHair: aToC = (_AToCMask & AToC_SuperHair) != 0; break;

      case PSSuperAToC: aToC = true; break;
      }
    }
  }

  PredicationMode pass = CurrentPredicationMode();

  D3DSetRenderState(cb, D3DRS_SEPARATEALPHABLENDENABLE_OLD,FALSE);

  if (
    cbState._lastSpec!=specFlags || cbState._lastAToC!=aToC ||
    cbState._lastRenderingMode!=_renderingMode || cbState._lastZPrimingDone!=cbState._zPrimingDone ||
    cbState._currentShadowStateSettings==SS_None
    )
  {
    cbState._lastSpec = specFlags;
    cbState._lastAToC = aToC;
    cbState._lastRenderingMode = _renderingMode;
    cbState._lastZPrimingDone = cbState._zPrimingDone;

    // some non-material changes can trigger some changes in the stuff set by SetupMaterialTextures as well
    SetupMaterialTextures(mat, cb, prop, specFlags);

    // we do not want to discretize alpha by default
    D3DXVECTOR4 vector4(0, 0, 0, 0);
    SetPixelShaderConstantF(cb,PSC_DiscretizeAlpha, vector4, 1); 

    if (specFlags&IsShadow)
    {
      D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE_OLD,0);
      D3DSetRenderState(cb, D3DRS_ZENABLE_OLD, TRUE);
      if (specFlags&IsShadowVolume)
      {
        // Shadow volume
        D3DSetRenderState(cb, D3DRS_ZFUNC_OLD, D3DCMP_GREATEREQUAL_OLD);
        D3DSetRenderState(cb, D3DRS_ZWRITEENABLE_OLD, FALSE);
      }
      else
      {
        // Ordinary shadow
        D3DSetRenderState(cb, D3DRS_ZFUNC_OLD,D3DCMP_LESSEQUAL_OLD);
        if (!_caps._hasStencilBuffer)
        {
          D3DSetRenderState(cb, D3DRS_ZWRITEENABLE_OLD,TRUE);
        }
        else
        {
          D3DSetRenderState(cb, D3DRS_ZWRITEENABLE_OLD,FALSE);
        }
      }
    }
    else
    {
      Assert(_renderingMode!=RMCrater);
      if (specFlags&IsHidden)
      {
        D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE_OLD,0);
      }
      else if (_renderingMode == RMShadowBuffer)
      { /// _Default technique requires us to write into the render target as well
        D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE_OLD, 0);
      }
      else
      {
        // local functor (closure) to prevent code duplication in predicated branches
        struct SetColorWriteEnable
        {
          void operator () (EngineDD11 *engine, int cb, int specFlags) const
          {
            if (specFlags&(NoZWrite|NoAlphaWrite))
            {
              if (specFlags&NoColorWrite)
              {
                // sometimes we may want to write alpha only 
                engine->D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE_OLD,(specFlags&NoAlphaWrite) ? 0 : D3DCOLORWRITEENABLE_ALPHA_OLD);
              }
              else
              {
                engine->D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE_OLD,D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD);
              }
            }
            else if (specFlags&NoColorWrite)
            {
              engine->D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE_OLD,D3DCOLORWRITEENABLE_ALPHA_OLD);
            }
            else
            {
              engine->D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE_OLD,D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD);
            }
          }
        };
        // WIP: FSAA: with z priming we never write color buffer
        //         if (BeginPredication<DepthPass>(cb,pass))
        //         {
        //           // with z priming we never write color buffer
        //           D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE, 0);
        //           EndPredication(cb);
        //         }
        if ((specFlags&NoZWrite))
        {
          if (BeginPredication<PrimingPass,DepthPass>(cb,pass))
          {
            // with depth priming color buffer is actually a depth buffer - disabling z writes disables writing to it as well
            D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE_OLD,0);
            EndPredication(cb);
          }
          if (BeginPredication<RenderingPass
#if _ARMA3_CAUSTICS
            , CausticsPass
#endif
          >(cb,pass))
          {
            SetColorWriteEnable()(this,cb,specFlags);
            EndPredication(cb);
          }
        }
        else
        {
          //if (BeginPredication<PrimingPass,RenderingPass>(cb,pass))
          {
            SetColorWriteEnable()(this,cb,specFlags);
            //EndPredication(cb);
          }
        }

      }

      if( specFlags&NoZBuf )
      {
        D3DSetRenderState(cb, D3DRS_ZFUNC_OLD,D3DCMP_ALWAYS_OLD);
        D3DSetRenderState(cb, D3DRS_ZWRITEENABLE_OLD,FALSE);
        D3DSetRenderState(cb, D3DRS_ZENABLE_OLD,FALSE);
        if (_renderingMode!=RMShadowBuffer)
        {
          if (BeginPredicationAdvisory<RenderingPass>(cb,pass))
          {
            SetTextureSBR(cb, true);
            EndPredicationAdvisory(cb);
          }
        }
      }
      else if( specFlags&NoZWrite )
      {
        // road
        D3DSetRenderState(cb, D3DRS_ZFUNC_OLD,D3DCMP_LESSEQUAL_OLD);
        D3DSetRenderState(cb, D3DRS_ZWRITEENABLE_OLD,FALSE);
        D3DSetRenderState(cb, D3DRS_ZENABLE_OLD,TRUE);
        if (_renderingMode!=RMShadowBuffer)
        {
          if (BeginPredicationAdvisory<RenderingPass>(cb,pass))
          {
            SetTextureSBR(cb, false);
            EndPredicationAdvisory(cb);
          }
        }
      }
      else
      {

        if (BeginPredication<RenderingPass
#if _ARMA3_CAUSTICS
          , CausticsPass
#endif
        >(cb,pass))
        {
          if(cbState._zPrimingDone && !(specFlags&IsAlpha) && ZPrimed()) // see LABEL_ZPRIME
          {
            D3DSetRenderState(cb, D3DRS_ZFUNC_OLD,D3DCMP_EQUAL_OLD);
            if (GetFSAA() > 0 && _useAToC)
            {
              // When we are using ATOC, we have to write to z-buffer (even if the z-value is the same)
              // Reason: CSAA only updates coverage samples when we write to z-buffer
              // Not writing to z-buffer caused artifacts
              // For more info see news:ijjlln$baq$1@new-server.localdomain
              D3DSetRenderState(cb, D3DRS_ZWRITEENABLE_OLD,TRUE);              
            }
            else
            {
              D3DSetRenderState(cb, D3DRS_ZWRITEENABLE_OLD,FALSE); 
            }
          }
          else
          {
            // note: for alpha blended primitives we still need to perform full z buffer compare + update when rendering
            D3DSetRenderState(cb, D3DRS_ZFUNC_OLD,D3DCMP_LESSEQUAL_OLD);
            D3DSetRenderState(cb, D3DRS_ZWRITEENABLE_OLD,TRUE);
          }
          EndPredication(cb);
        }

        if (BeginPredication<PrimingPass,DepthPass>(cb,pass))
        {
          // when priming we need to perform full z-test
          D3DSetRenderState(cb, D3DRS_ZFUNC_OLD,D3DCMP_LESSEQUAL_OLD);
          D3DSetRenderState(cb, D3DRS_ZWRITEENABLE_OLD,TRUE);
          EndPredication(cb);
        }

        D3DSetRenderState(cb, D3DRS_ZENABLE_OLD,TRUE);

        // SB texture can be used only in the normal rendering mode
        // in particular, it must never be used while rendering SB (i.e. _renderingMode==RMShadowBuffer)
        if (_renderingMode!=RMShadowBuffer)
        {
          if (BeginPredicationAdvisory<RenderingPass>(cb,pass))
          {
            // Use either SSSM or regular SB
            SetTextureSBR(cb, (specFlags&IsAlpha)!=0);
            EndPredicationAdvisory(cb);
          }
        }
      }
    }



    // Detect back face culling is disabled
    bool noCull = (specFlags&NoBackfaceCull)!=0;
    if (specFlags&IsShadow)
    {

#ifdef _XBOX
      D3DSetRenderState(cb, D3DRS_HIGHPRECISIONBLENDENABLE, FALSE);
      D3DSetRenderState(cb, D3DRS_HIGHPRECISIONBLENDENABLE1, FALSE);
#endif
      D3DSetRenderState(cb, D3DRS_ALPHABLENDENABLE_OLD,FALSE);
      if (_renderingMode == RMShadowBuffer)
      {
        ShadowsStatesSettings(cb, SS_ShadowBuffer,noCull);
      }
      else if (specFlags&IsShadowVolume)
      {
        D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE_OLD,FALSE);
        if (specFlags&ShadowVolumeFrontFaces)
        {
          ShadowsStatesSettings(cb, SS_Volume_Front,false);
        }
        else
        {
          ShadowsStatesSettings(cb, SS_Volume_Back,false);
        }
      }
      else
      {
        Fail("Projected shadows no longer supported");
      }
      if (_useAToC)
      {
        D3DEnableAlphaToCoverage(cb,false);
      }
    }
    else
    {

      if( specFlags&IsAlpha )
      {
        // no AToC is possible when real alpha blending is done
        if (_useAToC)
        {
          D3DEnableAlphaToCoverage(cb, false);
        }

        // Pass the alpha reference value on to the pixel shader (only fully opaque parts will survive in priming pass)
        if (BeginPredicationAdvisory<PrimingPass,DepthPass>(cb,pass))
        {
          Color alphaRefValue(0, 0, 0, 1);
          SetPixelShaderConstantF(cb, PSC_X_X_X_AlphaRef, (float*)&alphaRefValue, 1);
          EndPredicationAdvisory(cb);
        }

        if (BeginPredication<PrimingPass,DepthPass>(cb,pass))
        {

          // for z-priming we need to render only fully opaque parts
          D3DSetRenderState(cb, D3DRS_ALPHAREF_OLD,0xff);
#ifdef _XBOX
          D3DSetRenderState(cb, D3DRS_HIGHPRECISIONBLENDENABLE, FALSE);
          D3DSetRenderState(cb, D3DRS_HIGHPRECISIONBLENDENABLE1, FALSE);
#endif
          D3DSetRenderState(cb, D3DRS_ALPHABLENDENABLE_OLD,FALSE);
          D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE_OLD,FALSE);
          EndPredication(cb);
        }
        if (BeginPredication<RenderingPass>(cb,pass))
        {
          D3DSetRenderState(cb, D3DRS_ALPHAREF_OLD,1);
          D3DSetRenderState(cb, D3DRS_ALPHABLENDENABLE_OLD,TRUE);
#ifdef _XBOX
          D3DSetRenderState(cb, D3DRS_HIGHPRECISIONBLENDENABLE, TRUE);
          D3DSetRenderState(cb, D3DRS_HIGHPRECISIONBLENDENABLE1, TRUE);
#endif
          D3DSetRenderState(cb, D3DRS_SRCBLEND_OLD,D3DBLEND_SRCDEF);
          switch (specFlags&(DstBlendOne|DstBlendZero))
          {
          default:
          case 0:
            D3DSetRenderState(cb, D3DRS_DESTBLEND_OLD,D3DBLEND_INVSRCALPHA_OLD);
            D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE_OLD,TRUE);
            break;
          case DstBlendOne:
            // Disabled alpha test - otherwise moon halo causes artifacts
            D3DSetRenderState(cb, D3DRS_DESTBLEND_OLD,D3DBLEND_ONE_OLD);
            D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE_OLD,FALSE);
            break;
          case DstBlendZero:
            D3DSetRenderState(cb, D3DRS_DESTBLEND_OLD,D3DBLEND_ZERO_OLD);
            D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE_OLD,FALSE);
            break;
          }
          EndPredication(cb);
        }
      }
      else
      {
        // check if alpha testing is required
        if (specFlags&IsTransparent)
        {
          if (aToC)
          {
#ifdef _XBOX
            // in AToC mode we use only atoc or texkill, never alpha test
            D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE,FALSE);
            // Label: AlphaToCoverage
            if (BeginPredication<DepthPass>(cb,pass))
            {
              D3DEnableAlphaToCoverage(cb, false);
              // not accurate, but this is the best approximation we can do
              Color alphaRefValue(0, 0, 0, 128 * (1.0f / 256.0f));
              SetPixelShaderConstantF(cb, PSC_X_X_X_AlphaRef, (float*)&alphaRefValue, 1);
              EndPredication(cb);
            }
            else if (!cbState._zPrimingDone || !ZPrimed()) // WIP: ATOC
              //else if (true)
            {
              if (BeginPredication<PrimingPass,RenderingPass>(cb,pass))
              {
                D3DEnableAlphaToCoverage(cb, true);
                D3DSetRenderState(cb, D3DRS_ALPHATOMASKOFFSETS, D3DALPHATOMASK_DITHERED);
                EndPredication(cb);
              }
            }
            else
            {
              if (BeginPredication<PrimingPass>(cb,pass))
              {
                D3DEnableAlphaToCoverage(cb, true);
                D3DSetRenderState(cb, D3DRS_ALPHATOMASKOFFSETS, D3DALPHATOMASK_DITHERED);
                EndPredication(cb);
              }
              if (BeginPredication<RenderingPass>(cb,pass))
              {
                // when depth priming was already done, no need to repeat the alpha to mask, z test equal should do
                D3DEnableAlphaToCoverage(cb, false);
                EndPredication(cb);
              }
            }
#else
            if (BeginPredicationAdvisory<DepthPass> (cb, pass))
            {
              // not accurate, but this is the best approximation we can do
              Color alphaRefValue(0, 0, 0, 128 * (1.0f / 256.0f));
              SetPixelShaderConstantF(cb, PSC_X_X_X_AlphaRef, (float*)&alphaRefValue, 1);
              EndPredicationAdvisory(cb);
            }

            if (BeginPredication<DepthPass>(cb, pass))
            {
              D3DEnableAlphaToCoverage(cb, false);
              EndPredication(cb);
            }

            if (BeginPredication<PrimingPass, RenderingPass>(cb, pass))
            {
              D3DEnableAlphaToCoverage(cb, true);
              EndPredication(cb);
            }
#endif
          }
          else
          {
            if (_useAToC)
            {
              D3DEnableAlphaToCoverage(cb, false);
            }

            if (BeginPredicationAdvisory<PrimingPass,DepthPass>(cb,pass))
            {
              Color alphaRefValue(0, 0, 0, 128 * 1.0f / 256.0f);
              SetPixelShaderConstantF(cb, PSC_X_X_X_AlphaRef, (float*)&alphaRefValue, 1);
              EndPredicationAdvisory(cb);
            }
            if (BeginPredication<PrimingPass,DepthPass>(cb,pass))
            {
              D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE_OLD,FALSE);
              EndPredication(cb);
            }
            if (BeginPredication<RenderingPass>(cb,pass))
            {
              // We want to discretize alpha for alpha-tested surfaces.
              // Reason - alpha tested textures are drawn with z-test set to equal, so no alpha blending can occur
              // (because everything behind the surface will not be drawn). That would cause the problem with fog, if
              // the alpha was not discrete (texture filtering causes the alpha to be contiguous even for 1-bit alpha tests)
              D3DXVECTOR4 vector4(0, 0, 0, 1);
              SetPixelShaderConstantF(cb,PSC_DiscretizeAlpha, vector4, 1);

              D3DSetRenderState(cb, D3DRS_ALPHAREF_OLD, 128);
              // note: even with z-priming we can do alpha test - we are not writing to z anyway
              // this would prevent problems like when two identical polygons
              // with different alpha are rendered on the same place

              // however, doing alpha test with slightly different values during priming/rendering can cause issues
              // if we would like to do this, we would need to make sure no pixel which passes during priming
              // fails during the rendering

              // when alpha test is not done, problems appear when rendering two identical geometries over each other
              // (observed with tree LOD blending). If shader knows it is used to render this type of primitives,
              // it can prevent the issue by using clip

              // we are inside of BeginPredication<RenderingPass>
              // 
              // used to detect shadow buffers vs. normal rendering - with shadows we need alpha test, with rendering we do not
              if (_renderingMode!=RMShadowBuffer && cbState._zPrimingDone && ZPrimed()) // see LABEL_ZPRIME
              {
                D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE_OLD,FALSE);
              }
              else
              {
                D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE_OLD,TRUE);
              }
              EndPredication(cb);
            }
          }
        }
        else
        {
          if (_useAToC)
          {
            D3DEnableAlphaToCoverage(cb, false);
          }

          if (BeginPredicationAdvisory<PrimingPass,DepthPass>(cb,pass))
          {
            Color alphaRefValue(0, 0, 0, 0); // 0 means it will never be clipped
            SetPixelShaderConstantF(cb, PSC_X_X_X_AlphaRef, (float*)&alphaRefValue, 1);
            EndPredicationAdvisory(cb);
          }
          D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE_OLD,FALSE);
        }
#ifdef _XBOX
        D3DSetRenderState(cb, D3DRS_HIGHPRECISIONBLENDENABLE, FALSE);
        D3DSetRenderState(cb, D3DRS_HIGHPRECISIONBLENDENABLE1, FALSE);
#endif
        D3DSetRenderState(cb, D3DRS_ALPHABLENDENABLE_OLD,FALSE);
      }

      if (_renderingMode == RMShadowBuffer)
      {
        ShadowsStatesSettings(cb,SS_ShadowBuffer,noCull);
      }
      else
      {
        ShadowsStatesSettings(
          cb,specFlags&(NoZWrite|NoZBuf|NoAlphaWrite|NoStencilWrite) ? SS_Transparent : SS_Receiver,
          noCull
          );
      }


    }
  }

#if 1
  LogTexture("Tri",tex);
  SetTextureAndMaterial(cb,tex,mat._mat,spec,specFlags&FilterMask);
  //HRESULT err=D3D_OK;
  //if( err ) DDError9("Cannot set texture",err);
#endif

#if _ENABLE_CHEATS
  if (GetDrawWireframe())
  {
    if (BeginPredication<RenderingPass>(cb,pass))
    {
      // wireframe mode
      D3DSetRenderState(cb, D3DRS_FILLMODE_OLD, D3DFILL_WIREFRAME_OLD);
      D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE_OLD,FALSE);
      D3DSetRenderState(cb, D3DRS_ALPHABLENDENABLE_OLD,FALSE);
      D3DSetRenderState(cb, D3DRS_ZFUNC_OLD,D3DCMP_LESSEQUAL_OLD);
      D3DSetRenderState(cb, D3DRS_ZWRITEENABLE_OLD,FALSE);
      SelectPixelShader(cb, PSWhite);
      EndPredication(cb);
    }
  }
  else
  {
    D3DSetRenderState(cb, D3DRS_FILLMODE_OLD, D3DFILL_SOLID_OLD);
  }
#endif

#if _ARMA3_CAUSTICS
  if (GScene && GScene->DrawCaustics())
  {
    if (BeginPredication<CausticsPass>(cb,pass))
    {
      // caustics
      if (_useAToC)
        D3DEnableAlphaToCoverage(cb, false);

      D3DSetRenderState(cb, D3DRS_FILLMODE_OLD, D3DFILL_SOLID_OLD);
      D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE_OLD,FALSE);

      D3DSetRenderState(cb, D3DRS_ALPHABLENDENABLE_OLD,TRUE);
      D3DSetRenderState(cb, D3DRS_SRCBLEND_OLD,D3DBLEND_DESTCOLOR_OLD);
      D3DSetRenderState(cb, D3DRS_DESTBLEND_OLD,D3DBLEND_ONE_OLD);

      if(cbState._zPrimingDone && !(specFlags&IsAlpha) && ZPrimed()) // see LABEL_ZPRIME
      {
        D3DSetRenderState(cb, D3DRS_ZFUNC_OLD,D3DCMP_EQUAL_OLD);
      }
      else
      {
        D3DSetRenderState(cb, D3DRS_ZFUNC_OLD,D3DCMP_LESSEQUAL_OLD);
      }
      D3DSetRenderState(cb, D3DRS_ZWRITEENABLE_OLD,FALSE);

      EndPredication(cb);
    }
  }
#endif

}

void EngineDD11::DoDrawDecal(
                            int cb, Vector3Par screen, float rhw, float sizeX, float sizeY, PackedColor color,
                            const MipInfo &mip, const TexMaterial *mat, int specFlags
                            )
{


  float vx=screen.X();
  float vy=screen.Y();
  float z=screen.Z();

  float oow=rhw;

  // perform simple clipping
  float xBeg=vx-sizeX;
  float xEnd=vx+sizeX;
  float yBeg=vy-sizeY;
  float yEnd=vy+sizeY;
  float uBeg=0;
  float vBeg=0;
  float uEnd=1;
  float vEnd=1;

  if( xBeg<0 )
  {
    // -xBeg is out, side length is 2*sizeX
    uBeg=-xBeg/(2*sizeX);
    xBeg=0;
  }
  if( xEnd>_w )
  {
    // xEnd-_w is out, side length is 2*sizeX
    uEnd=1-(xEnd-_w)/(2*sizeX);
    xEnd=_w;
  }
  if( yBeg<0 )
  {
    // -yBeg is out, side length is 2*sizeY
    vBeg=-yBeg/(2*sizeY);
    yBeg=0;
  }
  if( yEnd>_h )
  {
    // yEnd-_h is out, side length is 2*sizeY
    vEnd=1-(yEnd-_h)/(2*sizeY);
    yEnd=_h;
  }

  if( xBeg>=xEnd || yBeg>=yEnd ) return;

  TLVertex v[4];

  // set vertex 0
  v[0].pos[0]=xBeg;
  v[0].pos[1]=yBeg;
  v[0].pos[2]=z;
  v[0].t0.u=uBeg;
  v[0].t0.v=vBeg;
  // set vertex 1
  v[1].pos[0]=xEnd;
  v[1].pos[1]=yBeg;
  v[1].pos[2]=z;
  v[1].t0.u=uEnd;
  v[1].t0.v=vBeg;
  // set vertex 2
  v[2].pos[0]=xEnd;
  v[2].pos[1]=yEnd;
  v[2].pos[2]=z;
  v[2].t0.u=uEnd;
  v[2].t0.v=vEnd;
  // set vertex 3
  v[3].pos[0]=xBeg;
  v[3].pos[1]=yEnd;
  v[3].pos[2]=z;
  v[3].t0.u=uBeg;
  v[3].t0.v=vEnd;

  // ColorScaleD2 is incorrect when _modColor is not 0.5
  Assert(fabs((_modColor-Color(0.5,0.5,0.5)).Brightness())<0.01f);
  // color scaled down - glow will scale it up again
  color = ColorScaleD2(color);

  // add vertices to vertex buffer

  // check active queue

  if( specFlags&IsAlphaFog )
  {
    v[0].color=color;
    v[0].specular=PackedColor(0xff000000);
  }
  else
  {
    // use fog with z
    v[0].specular=PackedColor(0xff000000-(color&0xff000000));
    v[0].color=PackedColor(color|0xff000000);
  }

  int i;
  for( i=0; i<4; i++ )
  {
    v[i].rhw=oow;
    v[i].pos[2]=z;
    v[i].color=v[0].color;
    v[i].specular=v[0].specular;
  }

  // Zero vertex and index buffer in the device (because the actual ones can be locked in AddVertices which is a problem at least on XBOX).
  // Also, it is better to do that before SwitchTL, as it can hypothetically free last instance of the buffer that is set (which is bad as well).
  ForgetDeviceBuffers(cb);

  Assert(cb<0);
  SwitchRenderMode(RMTris);
  // allocate some queue
  SwitchTL(cb, TLDisabled);

  AddVertices(v,4);

  QueuePrepareTriangle(mip, mat, specFlags);
  static const VertexIndex indices[4] = {0,1,2,3};
  QueueFan(indices,4);
}

HRESULT EngineDD11::LockDynBuffer(const ComRef<IDirect3DVertexBuffer9Old> &buf, void * &data, D3D11_MAP lockFlags, int lockOffset, int size)
{
  PROFILE_DX_SCOPE_DETAIL(3davC);
Retry:
#if defined _XBOX && _XBOX_VER>=2
  // range lock not supported - emulate it
  HRESULT err=Lock(buf,0,MeshBufferLength*sizeof(TLVertex),&data,lockFlags);
  data = (char *)data+lockOffset;
#else
  D3D11_MAPPED_SUBRESOURCE sr;
  HRESULT err = _d3dDeviceRec->Map(buf, 0, lockFlags, 0, &sr);
#endif
  if (err!=D3D_OK || !sr.pData)
  {
    if (CheckMainThread() && FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto Retry;
#ifndef _XBOX
    ErrF("VB Lock failed, %x,%s",err,DXGetErrorString(err));
#else
    ErrF("VB Lock failed, %x",err);
#endif
    ReportMemoryStatusWithVRAM();
  }
  data = sr.pData;
  return err;
}

void EngineDD11::SetPixelShaderConstantUI(int cb, Vector3Val val, float intensity)
{
  SetPixelShaderConstantF(cb, PSC_UIPars, D3DXVECTOR4(val.X(), val.Y(), val.Z(), intensity), 1);
}

void EngineDD11::SetPixelShaderConstantDBS(int cb, ColorVal matDiffuse, ColorVal matForcedDiffuse, ColorVal matSpecular, float matPower, bool useGroundBidir, bool useNewSpecular)
{
  // Prepare array of colors to use only one call later
  Color difBakSpec[3];
  Color &diffuse = difBakSpec[0];
  Color &diffuseBack = difBakSpec[1];
  Color &specular = difBakSpec[2];

  // Calculate diffuse color
  {
    // Combine light diffuse and material diffuse
    diffuse = CBState(cb)._lightDiffuse * matDiffuse;

    // Copy alpha from material diffuse alpha
    diffuse.SetA(matDiffuse.A());
  }

  // Calculate diffuse back color
  // See label GROUNDREFLECTION
  if (useGroundBidir)
  {
#if _ARMA3_NEW_LIGHTING_CFG
    diffuseBack  = ComputeBackReflection(diffuse);
#else
    LightSun *sun = GScene->MainLight();
    diffuseBack = sun->GroundReflection() * diffuse;
#endif
  }
  else
  {
    float backCoef = matForcedDiffuse.A();
    diffuseBack = diffuse * backCoef;
    diffuseBack.SetA(backCoef);
  }

  // Calculate specular coefficient (make the specular reflection stronger), consider specular power
  float specularCoef;
  if (useNewSpecular && (matPower > 0))
  {
    static float b = 0.5f;
    static float c = 1000.0f;
    specularCoef = c / Square(2.0f * acos(pow(b, 1.0f / matPower)) * 360.0f/(2.0f*H_PI));
  }
  else
  {
    specularCoef = 1.0f;
  }

  // Saturate the result by SunObjectColor + SunHaloObjectColor (to reflect no more than them)
  LightSun *sun = GScene->MainLight();
  static float sunBrightnessCoef = 2.0f;
  float sunBrightness = (sun->SunObjectColor().Brightness() + sun->SunHaloObjectColor().Brightness()) * GetAccomodateEye().Brightness() * _modColor.Brightness() * sunBrightnessCoef;
  float specularCoefSat = floatMin(specularCoef, sunBrightness);

  // Calculate specular color (which includes specular light from the Sun only, not from the environment)
  /// multiply by 2 so that pixel shader can avoid _x2
  specular = CBState(cb)._lightSpecular * matSpecular * 2.0f * Color(specularCoefSat, specularCoefSat, specularCoefSat, 1.0f);
  specular.SetA(matPower);

  // Set the pixel shader constant
  SetPixelShaderConstantF(cb, PSC_Diffuse, (float*)difBakSpec, 3);
}

void EngineDD11::SetPixelShaderConstantDBSZero(int cb)
{
  static const Color zeroes[3]={Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0)};
  SetPixelShaderConstantF(cb, PSC_Diffuse, (float*)zeroes, 3);
}

void EngineDD11::SetPixelShaderConstant_A_DFORCED_E_Sun(int cb, ColorVal matEmmisive, ColorVal matAmbient, ColorVal matDiffuse, ColorVal matForcedDiffuse)
{
  // temporary solution for SkinShader - _matAmbient, _matEmmisive and matForcedDiffuse holds skin params
  Color matEmissiveCoef;
  Color matForcedDiff;
  Color matAmbi;
  if( ( CBState(cb)._vertexShaderID == VSSkin ) 
    || ( CBState(cb)._vertexShaderID == VSTreeAdv ) 
    || ( CBState(cb)._vertexShaderID == VSTreeAdvTrunk )
    || ( CBState(cb)._vertexShaderID == VSTreeAdvModNormals ))
  {
    matEmissiveCoef = Color( 0, 0, 0, 1 );
    matForcedDiff = Color( 0, 0, 0, 1 );
    matAmbi = matDiffuse;
  }
  else
  {
    matEmissiveCoef = matEmmisive * GetAccomodateEye();
    matForcedDiff = matForcedDiffuse;
    matAmbi = matAmbient;
  }

  // AE
  Color newAE = matEmissiveCoef + CBState(cb)._lightAmbient * matAmbi;

  // Set the diffuse back coefficient
  float backCoef = matForcedDiffuse.A();
  newAE.SetA(backCoef);

  // G
#if _ARMA3_NEW_LIGHTING_CFG
  Color newGE = matEmissiveCoef + ComputeGroundReflection(CBState(cb)._lightAmbient, matAmbi);
#else
  LightSun *sun = GScene->MainLight();
  Color newGE = matEmissiveCoef + sun->GroundReflection() * CBState(cb)._lightAmbient * matAmbi;
#endif

  // DForced
  Color newDForced = HBlack + CBState(cb)._lightDiffuse * matForcedDiff; //matForcedDiffuse;

  // Set the PS constants
  SetPixelShaderConstantF(cb, PSC_AE, (float*)&newAE, 1);
  SetPixelShaderConstantF(cb, PSC_GE, (float*)&newGE, 1);
  SetPixelShaderConstantF(cb, PSC_DForced, (float*)&newDForced, 1);
}

/** simple variant used for initial device setup */
void EngineDD11::SetPixelShaderConstant_A_DFORCED_E_Black(int cb)
{
  // Zero
  Color newE = HBlack;
  Color zero = Color(0, 0, 0, 0);

  // Set the PS constants
  SetPixelShaderConstantF(cb, PSC_AE, (float*)&newE, 1);
  SetPixelShaderConstantF(cb, PSC_GE, (float*)&newE, 1);
  SetPixelShaderConstantF(cb, PSC_DForced, (float*)&zero, 1);
}


void EngineDD11::SetPixelShaderConstant_A_DFORCED_E_NoSun(int cb, ColorVal matEmmisive)
{
  // temporary solution for SkinShader - _matEmmisive holds skin params
  Color matEmissiveCoef;
  if( ( CBState(cb)._vertexShaderID == VSSkin ) 
    || ( CBState(cb)._vertexShaderID == VSTreeAdv ) 
    || ( CBState(cb)._vertexShaderID == VSTreeAdvTrunk ) 
    || ( CBState(cb)._vertexShaderID == VSTreeAdvModNormals ) )
    matEmissiveCoef = Color( 0, 0, 0, 1 );
  else
    matEmissiveCoef = matEmmisive * GetAccomodateEye();

  Color newE = matEmissiveCoef;

  // Zero
  Color zero = Color(0, 0, 0, 0);

  // Set the PS constants
  SetPixelShaderConstantF(cb, PSC_AE, (float*)&newE, 1);
  SetPixelShaderConstantF(cb, PSC_GE, (float*)&newE, 1);
  SetPixelShaderConstantF(cb, PSC_DForced, (float*)&zero, 1);
}

void EngineDD11::SetPixelShaderConstantLayers(int cb, int layerMask, const TexMaterial *mat)
{
  /// size of the height field (nopx) texture used in given layer
  float heigthTexSizeLog2[6];

  // if this is version with satellite normal
  bool isWithSatNorm = mat->GetPixelShaderID() == PSTerrainSNX;

  for (int i=0; i<lenof(heigthTexSizeLog2); i++)
  {
    // provide a default init in case no other value is available
    heigthTexSizeLog2[i] = 0;

    if (isWithSatNorm && i == LandMatLayerCount - 1)
      continue; //last layer is reserved for satellite normal map

    int stage = LandMatStageLayers+LandMatStagePerLayer*i+LandMatNoInStage;
    if (stage<mat->_stageCount+1)
    {
      const Texture *noMap = mat->_stage[i].GetTextureForDraw();
      if (noMap)
      {
        // we are interested in texture "size"
        // the pixel shader is not caring about x / y separately, therefore we need to provide a single number
        // it is not sure how (unclear crash dump), but it happenned noMap size was zero
        float area = floatMax(1,noMap->AWidth()*noMap->AHeight());
        const float log2 = 0.69314718056f;
        // compute log2 of sqrt(area)
        heigthTexSizeLog2[i] = log(area)*(0.5f/log2);
      }
    }
  }

  // Set the PS constants
  float layers[LandMatLayerCount][4];
  for (int i=0; i<LandMatLayerCount; i++)
  {
    layers[i][0] = layers[i][1] = layers[i][2] = layers[i][3] = (layerMask&(1<<i))!=0;
  }

  if (isWithSatNorm)
  {
    // remember if we want to show satellite normal map in layers[5]
    layers[LandMatLayerCount-1][0] = layers[LandMatLayerCount-1][1] = layers[LandMatLayerCount-1][2] = 
      layers[LandMatLayerCount-1][3] = Landscape::ShowSatelliteNormal ? 1.0f : 0.0f;

#if _HELISIM_TERRAIN_RIVERS
    int waterStageIdx = -1;

    for (int i = 3; i < mat->_stageCount - 2; i++)
    {
      const TexStageInfo &tsi = mat->_stage[i];

      if (tsi._tex && tsi._tex->Name())
      {
        if (strstr(tsi._tex->Name(), "water"))
        {
          waterStageIdx = (i - 3) / 2;
          break;
        }
      }
    }

    if (waterStageIdx != -1)
      layers[waterStageIdx][0] = layers[waterStageIdx][1] = layers[waterStageIdx][2] = layers[waterStageIdx][3] = -1.0f;

#endif
  }

  SetPixelShaderConstantF(cb, PSC_Layers, layers[0], LandMatLayerCount);

  // pack 4 floats in one constant
  const int floatPerVector = 4;
  float texSize[(lenof(heigthTexSizeLog2)+floatPerVector-1)/floatPerVector*floatPerVector];
  COMPILETIME_COMPARE_ISLESSOREQUAL(lenof(heigthTexSizeLog2),lenof(texSize));
  for (int i=0; i<lenof(heigthTexSizeLog2); i++) texSize[i] = heigthTexSizeLog2[i];
  for (int i=lenof(heigthTexSizeLog2); i<lenof(texSize); i++) texSize[i] = 0;

  SetPixelShaderConstantF(cb, PSC_NoTexSizeLog2, texSize, lenof(texSize)/floatPerVector);

  if (isWithSatNorm)
  {
    // setup satellite normal map blending distances
    float start = Landscape::DefaultSatelliteNormalBlendStart;
    float end = Landscape::DefaultSatelliteNormalBlendEnd;
    bool normalOnDetail = false;
    if (GLandscape)
    {
      start = GLandscape->GetSatelliteNormalBlendStart();
      end = GLandscape->GetSatelliteNormalBlendEnd();
    }

    float length = end-start;
    if (length <= 0)
      length = 1.0f; // we do not want zero length

    D3DXVECTOR4 vector4(start, length, normalOnDetail ? 1.0f : 0.0f, 0);
    SetPixelShaderConstantF(cb,PSC_TerrainSatNormDist, vector4, 1);
  }

  SetPixelShaderNightEmissiveConst(cb);
}

#if _ARMA3_REFRACTION
void EngineDD11::SetPixelShaderConstantsRefract(int cb, ColorVal matSpecular)
{
  SetPixelShaderConstantSuper(cb, matSpecular);

  // set current frame texture
  TextureHandle11 *rt = _renderTargetD._srView; // must have been prepared by CaptureRefractionsBackground before

  SetTexture( cb, 10, rt ); 
  D3DSetSamplerState( cb, 10, D3DSAMP_SRGBTEXTURE_OLD, FALSE );
  D3DSetSamplerState( cb, 10, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD );
  D3DSetSamplerState( cb, 10, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD );
  D3DSetSamplerState( cb, 10, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
  D3DSetSamplerState( cb, 10, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
  D3DSetSamplerState( cb, 10, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);

  // set depth texture
  SetTexture( cb, 11, GetDepthAsTexture()); 
  D3DSetSamplerState( cb, 11, D3DSAMP_SRGBTEXTURE_OLD, FALSE );
  D3DSetSamplerState( cb, 11, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD );
  D3DSetSamplerState( cb, 11, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD );
  D3DSetSamplerState( cb, 11, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
  D3DSetSamplerState( cb, 11, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
  D3DSetSamplerState( cb, 11, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);

  D3DXVECTOR4 params[PS_REFRACT_PARS_SIZE];

  //render target size
  params[0] = D3DXVECTOR4(1.0f/_wRT, 1.0f/_hRT, 0.5f/_wRT, 0.5f/_hRT);
  params[1] = D3DXVECTOR4(_refractionPars._intensity, _refractionPars._scales[0], _refractionPars._scales[1], _refractionPars._scales[2]);

  bool useAberration = true;
  if (floatApproxEq(_refractionPars._scales[0], _refractionPars._scales[0]) &&
    floatApproxEq(_refractionPars._scales[1], _refractionPars._scales[2])
    )
  {
    useAberration = false;
  }

  params[2] = D3DXVECTOR4(
    Glob.time.toFloat() * _refractionPars._timeCoefs[0], 
    Glob.time.toFloat() * _refractionPars._timeCoefs[1], 
    useAberration? 1.0f : 0.0f, 
    0.0f);

  SetPixelShaderConstantF(cb, PSC_RefractPars, (const float*) &params[0], PS_REFRACT_PARS_SIZE);      
}
#endif //#if _ARMA3_REFRACTION

#if _ARMA3_REFRACTION_SPRITES
void EngineDD11::SetPixelShaderConstantsRefractSprite(int cb)
{
  // set current frame texture
  TextureHandle11 *rt = _renderTargetD._srView; // must have been prepared by CaptureRefractionsBackground before
  
  SetTexture( cb, 10, rt ); 
  D3DSetSamplerState( cb, 10, D3DSAMP_SRGBTEXTURE_OLD, FALSE );
  D3DSetSamplerState( cb, 10, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD );
  D3DSetSamplerState( cb, 10, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD );
  D3DSetSamplerState( cb, 10, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
  D3DSetSamplerState( cb, 10, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
  D3DSetSamplerState( cb, 10, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);

  // set depth texture
  SetTexture( cb, 11, GetDepthAsTexture()); 
  D3DSetSamplerState( cb, 11, D3DSAMP_SRGBTEXTURE_OLD, FALSE );
  D3DSetSamplerState( cb, 11, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD );
  D3DSetSamplerState( cb, 11, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD );
  D3DSetSamplerState( cb, 11, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
  D3DSetSamplerState( cb, 11, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
  D3DSetSamplerState( cb, 11, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);

  D3DXVECTOR4 params[PS_REFRACT_PARS_SIZE];

  //render target size
  params[0] = D3DXVECTOR4(1.0f/_wRT, 1.0f/_hRT, 0.5f/_wRT, 0.5f/_hRT);
  params[1] = D3DXVECTOR4(_spriteRefractionPars._intensity, _spriteRefractionPars._scales[0], _spriteRefractionPars._scales[1], _spriteRefractionPars._scales[2]);

  bool useAberration = true;
  if (floatApproxEq(_spriteRefractionPars._scales[0], _spriteRefractionPars._scales[0]) &&
    floatApproxEq(_spriteRefractionPars._scales[1], _spriteRefractionPars._scales[2])
    )
  {
    useAberration = false;
  }

  params[2] = D3DXVECTOR4(
    Glob.time.toFloat() * _spriteRefractionPars._timeCoefs[0], 
    Glob.time.toFloat() * _spriteRefractionPars._timeCoefs[1], 
    useAberration? 1.0f : 0.0f, 
    0.0f);

  SetPixelShaderConstantF(cb, PSC_RefractPars, (const float*) &params[0], PS_REFRACT_PARS_SIZE);      
}
#endif //#if _ARMA3_REFRACTION_SPRITES


void EngineDD11::SetPixelShaderConstantWater(int cb)
{
  // Environmental map color modificator
  Color sky, aroundSun;
  _sceneProps->GetSkyColor(sky, aroundSun);
  Color skyColor = sky * GetAccomodateEye() * _modColor * EnvMapScale;
  SetPixelShaderConstantF(cb,PSC_GlassEnvColor, (float*)&skyColor, 1);

  // Wave color
  LightSun *sun = GScene->MainLight();
  float hdr = GetHDRFactor();
  Color waveColor = GetAccomodateEye()*(sun->GetDiffuse()+sun->GetAmbient())*0.5f*hdr;
  waveColor.SaturateMinMax();
  static float waveA = 0.4;
  waveColor.SetA(-waveA);
  SetPixelShaderConstantF(cb,PSC_WaveColor, waveColor.GetArray(), 1);

  //light direction in WS
  Vector3 lightdir = GScene->MainLight()->LightDirection();
  D3DXVECTOR4 cdir(lightdir.Z(), -lightdir.X(), -lightdir.Y(), 1.0f);
  SetPixelShaderConstantF(cb, PSC_LDirectionGround_DiffuseBack, (const float*) &cdir, 1);

  //coeficients for normals computation - 1 without clouds, 0 = clouds
  float invwheather = (1.0 - GLandscape->GetOvercast());

  static float speed1s  = 2.0;
  static float speed2s  = 2.01;

  static float speed1o  = 3.0;
  static float speed2o  = 3.011;

  static float height  = 0.6;
  static float height2 = 0.5;

  float time  = Glob.time.toFloat();
  float time1 = (speed1s*invwheather + speed1o)*time;

  static const int WATER_PARAMS_SIZE = 8;
  D3DXVECTOR4 waterParams[WATER_PARAMS_SIZE];

  waterParams[0] = D3DXVECTOR4(
    sin(time1 + H_PI*0.0f)*height + height,	//to have just positive values
    sin(time1 + H_PI*0.5f)*height + height,
    sin(time1 + H_PI*1.0f)*height + height,
    sin(time1 + H_PI*1.5f)*height + height);

  float time2 = (speed2s*invwheather + speed2o)*time;  

  waterParams[1] = D3DXVECTOR4(
    sin(time2 + H_PI*0.0f)*height2 + height2,	//to have just positive values
    sin(time2 + H_PI*0.5f)*height2 + height2,
    sin(time2 + H_PI*1.0f)*height2 + height2,
    sin(time2 + H_PI*1.5f)*height2 + height2);

  //render target size
  waterParams[2] = D3DXVECTOR4(1.0f/_wRT, 1.0f/_hRT, 0.5f/_wRT, 0.5f/_hRT);

  //weather params
  static float stormystartT   = 2.5;
  static float stormyoffsetT  = 4.0;

  static float stormystartV   = 0.5;
  static float stormyoffsetV  = -0.5;

  static float maxspecularspot= 200.0;
  static float offspecularspot= 150.0;
  static float maxspecularvis = 25.0;
  float linspec = invwheather*invwheather;

  //lower the specular influence according the sun height
  static float sunspeclimit1  = 0.2620;   //(15 degrees)
  static float sunspeclimit0  = -0.03489; //(-5 degrees)

  //specular influence according the weather (1..0) and sun height
  float sunzenith    = -sun->LightDirection().Y();
  float specw			= 3.0*linspec - 2.0*linspec*invwheather;
  float specsun		= (sunzenith - sunspeclimit0)/(sunspeclimit1 - sunspeclimit0);
  saturate(specsun, 0.0, 1.0);

  float spec    = specw*specsun;

  waterParams[3] = D3DXVECTOR4(
    invwheather*stormyoffsetV + stormystartV,					//power of vertex normals
    invwheather*stormyoffsetT + stormystartT,					//power of texture normals
    spec*maxspecularvis,												//specular visibility
    maxspecularspot - offspecularspot*GLandscape->GetOvercast());		//specular power

  //influence of LdotN according the sun 
  static float LdotNlimit1  = 0.4226;   //(25 degrees)
  static float LdotNlimit0  = -0.1736; //(-10 degrees)

  float LdotNscale   = (sunzenith - LdotNlimit0)/(LdotNlimit1 - LdotNlimit0);
  saturate(LdotNscale, 0.0, 1.0);
  float LdotNoffset  = 1 - LdotNscale;

  //wrap it
  float wraptime = time;

  waterParams[4] = D3DXVECTOR4(
    invwheather, 
    LdotNscale, 
    LdotNoffset, 
    wraptime);

  //setup inverse projection values for x and y 
  float invX = 1.0f;
  float invY = 1.0f;
  Assert(GScene);
  if (GScene)
  {
    Camera *camera = GScene->GetCamera();
    Assert(camera);
    if (camera)
    {
      invX = camera->ProjectionNormal()(0,0);
      if (invX != 0.0f)
        invX = 1.0f/invX;
      else
        invX = 1.0f;

      invY = camera->ProjectionNormal()(1,1);
      if (invY != 0.0f)
        invY = 1.0f/invY;
      else
        invY = 1.0f;
    }
  }

  // underwater coefficient
  float underwaterCoef = _underwaterPars._underwaterDepth >= _underwaterPars._waterDepthFogStart ? 1.0f : 0.0f;

  waterParams[5] = D3DXVECTOR4(invX, invY, underwaterCoef, 1.0f);


  waterParams[6] = D3DXVECTOR4(0.01f, 0.65f, 0.07f, 0.2f);
  waterParams[7] = D3DXVECTOR4(100.0f, 20.0f, 1.0f, 1.0f);

  SetPixelShaderConstantF(cb, PSC_CalmWaterPars1, (const float*) &waterParams[0], WATER_PARAMS_SIZE);      

  // render copy
  //SetFiltering( cb, 10, TFPoint, 2);
  D3DSetSamplerState( cb, 10, D3DSAMP_SRGBTEXTURE_OLD, FALSE );
  D3DSetSamplerState( cb, 10, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD );
  D3DSetSamplerState( cb, 10, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD );
  D3DSetSamplerState( cb, 10, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
  D3DSetSamplerState( cb, 10, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
  D3DSetSamplerState( cb, 10, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);

  TextureHandle11 *rt = _renderTargetD._srView; // must have been prepared by CaptureRefractionsBackground before
  SetTexture( cb, 10, rt ); 

  SetTexture( cb, 11, GetDepthAsTexture()); 
  D3DSetSamplerState( cb, 11, D3DSAMP_SRGBTEXTURE_OLD, FALSE );
  D3DSetSamplerState( cb, 11, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD );
  D3DSetSamplerState( cb, 11, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD );
  D3DSetSamplerState( cb, 11, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
  D3DSetSamplerState( cb, 11, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
  D3DSetSamplerState( cb, 11, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);

  /*  SetPixelShaderConstantF( cb, PSC_SeaWaterPars1, (float*)GetSeaWaterParsPS1(), SEAWATER_PSPARS_SIZE_1 );

  // set samplers (temporary solution)
  for( int i = 0; i <= 7; i++ )
  {
  SetFiltering( cb, i, TFTrilinear, 2);
  D3DSetSamplerState( cb, i, D3DSAMP_SRGBTEXTURE, FALSE );
  D3DSetSamplerState( cb, i, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP );
  D3DSetSamplerState( cb, i, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP );
  }
  // environmental maps
  SetFiltering( cb, 8, TFTrilinear, 2);
  D3DSetSamplerState( cb, 8, D3DSAMP_SRGBTEXTURE, FALSE );
  D3DSetSamplerState( cb, 8, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
  D3DSetSamplerState( cb, 8, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );
  SetFiltering( cb, 9, TFTrilinear, 2);
  D3DSetSamplerState( cb, 9, D3DSAMP_SRGBTEXTURE, FALSE );
  D3DSetSamplerState( cb, 9, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
  D3DSetSamplerState( cb, 9, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );

  // render copy
  //SetFiltering( cb, 10, TFPoint, 2);
  D3DSetSamplerState( cb, 10, D3DSAMP_SRGBTEXTURE, FALSE );
  D3DSetSamplerState( cb, 10, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
  D3DSetSamplerState( cb, 10, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );
  // depth
  //SetFiltering( cb, 11, TFPoint, 2);
  D3DSetSamplerState( cb, 11, D3DSAMP_SRGBTEXTURE, FALSE );
  D3DSetSamplerState( cb, 11, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
  D3DSetSamplerState( cb, 11, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );
  // set render target copy and depth to samplers 10 and 11
  IDirect3DTexture9 *rt = _renderTargetD; // must have been prepared by CaptureRefractionsBackground before
  SetTexture( cb, 10, rt );
  SetTexture( cb, 11, GetDepthAsTexture() );
  */
}

void EngineDD11::SetPixelShaderConstantWaterSimple(int cb)
{
  LightSun *sun = GScene->MainLight();
  // water pixel shader requires some special constants to be set
  // fog color is quite good approximation of sky color
  Color lighting = sun->SimulateDiffuse(0.5)*GetAccomodateEye()*_modColor;
  float hdr = GetHDRFactor();
  Color groundLitColor=Color(0.15,0.2,0.1)*lighting;
  float groundColor[]={groundLitColor.R(),groundLitColor.G(),groundLitColor.B(),0.5f};
  float skyTopColor[]={_skyTopColor.R()*hdr,_skyTopColor.G()*hdr,_skyTopColor.B()*hdr,0.5f};
  SetPixelShaderConstantF(cb, PSC_GroundReflColor,groundColor,1);
  SetPixelShaderConstantF(cb, PSC_SkyReflColor,skyTopColor,1);
  Color waveColor = GetAccomodateEye()*(sun->GetDiffuse()+sun->GetAmbient())*0.5*_modColor;
  waveColor.SaturateMinMax();
  static float waveA = 0.4;
  waveColor.SetA(-waveA);
  SetPixelShaderConstantF(cb, PSC_WaveColor, waveColor.GetArray(), 1);
}

void EngineDD11::SetPixelShaderConstantSkin( int cb, ColorVal matAmbient, ColorVal matDiffuse, 
                                           ColorVal matForcedDiffuse, ColorVal matEmissive, ColorVal matSpecular )
{
  // Environmental map color modificator
  LightSun *sun = GScene->MainLight();
  Color lighting = sun->SimulateDiffuse(0.5) * GetAccomodateEye() * _modColor * EnvMapScale;
  SetPixelShaderConstantF(cb,PSC_GlassEnvColor, (float*)&lighting, 1);

  // Material specular color
  SetPixelShaderConstantF(cb, PSC_GlassMatSpecular, (float*)&matSpecular, 1);

  // Main light in world coordinates
  Vector3Val dir = CBState(cb)._direction;
  D3DXVECTOR4 direction(-dir.X(), -dir.Y(), -dir.Z(), 1.0f);
  SetPixelShaderConstantF(cb, PSC_WLight, (float*)&direction, 1);


  // skin shader specific params:
  const float k = 2.0; // rescale const. - material colors are halfed (because HDR)
  Color skinPars[4];
  // fetch skin effect params from matForcedDiffuse & matEmissive (temporary solution):
  //Color unscatteredColor = Color( 0.6f, 0.8f, 1.0f, 1.0f );
  /*Color unscatteredColor = Color( 111 / 255.0f, 70 / 255.0f, 43 / 255.0f, 1.0f ); // new color by Gugla
  float unscatteredWeight = matForcedDiffuse.R() * k;
  float epidermalWeight = matForcedDiffuse.G() * k;
  float subdermalWeight = matForcedDiffuse.B() * k;
  float hemisphericSpecLevel = matForcedDiffuse.A() * 2.0f;
  float epidermalMul = matEmissive.R() * k;
  float epidermalAdd = 1.0 - matEmissive.R() * k;
  float darkRimFactorEp = matEmissive.G() * k;
  float subdermalMul = matEmissive.B() * k;
  float subdermalAdd = 1.0 - matEmissive.B() * k;
  float darkRimFactorSu = matEmissive.A();*/

  // NEW PARAMS MAPPING
  // matAmbient
  Color unscatteredColor = Color( matAmbient.R() * k, matAmbient.G() * k, matAmbient.B() * k, matAmbient.A() ); // new color by Gugla
  float unscatteredWeight = matAmbient.A();
  // matForcedDiffuse
  float epidermalMul = matForcedDiffuse.R() * k * 2.0f;
  float epidermalAdd = matForcedDiffuse.G() * k;
  float darkRimFactorEp = matForcedDiffuse.B() * k;
  float epidermalWeight = matForcedDiffuse.A();
  // matEmissive
  float subdermalMul = matEmissive.R() * k * 2.0f;
  float subdermalAdd = matEmissive.G() * k;
  float darkRimFactorSu = matEmissive.B() * k;
  float subdermalWeight = matEmissive.A();
  // matSpecular
  float hemisphericSpecLevel = matSpecular.A() * 2.0f;

  // compose params to float4 [4]
  skinPars[0] = unscatteredColor;
  skinPars[1] = Color( unscatteredWeight, epidermalWeight, subdermalWeight, hemisphericSpecLevel );
  skinPars[2] = Color( epidermalMul, epidermalAdd, -darkRimFactorEp, 1.0f - darkRimFactorEp );
  skinPars[3] = Color( subdermalMul, subdermalAdd, -darkRimFactorSu, 1.0f - darkRimFactorSu );
  //skinPars[3] = Color( matSpecular.R(), matSpecular.G(), matSpecular.A(), 1 ); // TEST
  SetPixelShaderConstantF(cb, PSC_SkinEffectPars, (float*)skinPars, 4);
}

void EngineDD11::SetPixelShaderParamsCalmWater( int cb ) //, ColorVal matSpecular, ColorVal matForcedDiffuse, ColorVal matEmissive)
{
  // set constant regs params (12 * float4 + 16 * float4) - must be separated because of size
  SetPixelShaderConstantF( cb, PSC_CalmWaterPars1, (float*)GetCalmWaterPars(), CALMWATER_PARS_SIZE_PS1 );
  SetPixelShaderConstantF( cb, PSC_CalmWaterPars2, (float*)GetCalmWaterPars2(), CALMWATER_PARS_SIZE_PS2 );

  // set samplers (temporary solution)
  for( int i = 0; i <= 5; i++ )
  {
    SetFiltering( cb, i, TFTrilinear, 2);
    D3DSetSamplerState( cb, i, D3DSAMP_SRGBTEXTURE_OLD, FALSE );
    D3DSetSamplerState( cb, i, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_WRAP_OLD );
    D3DSetSamplerState( cb, i, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_WRAP_OLD );
  }
  SetFiltering( cb, 6, TFPoint, 2);
  D3DSetSamplerState( cb, 6, D3DSAMP_SRGBTEXTURE_OLD, FALSE );
  D3DSetSamplerState( cb, 6, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_WRAP_OLD );
  D3DSetSamplerState( cb, 6, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_WRAP_OLD );
  // environmental map
  SetFiltering( cb, 7, TFTrilinear, 2);
  D3DSetSamplerState( cb, 7, D3DSAMP_SRGBTEXTURE_OLD, TRUE );
  D3DSetSamplerState( cb, 7, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD );
  D3DSetSamplerState( cb, 7, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD );
  // render copy
  SetFiltering( cb, 10, TFPoint, 2);
  D3DSetSamplerState( cb, 10, D3DSAMP_SRGBTEXTURE_OLD, FALSE );
  D3DSetSamplerState( cb, 10, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD );
  D3DSetSamplerState( cb, 10, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD );
  // depth
  SetFiltering( cb, 11, TFPoint, 2);
  D3DSetSamplerState( cb, 11, D3DSAMP_SRGBTEXTURE_OLD, FALSE );
  D3DSetSamplerState( cb, 11, D3DSAMP_ADDRESSU_OLD, D3DTADDRESS_CLAMP_OLD );
  D3DSetSamplerState( cb, 11, D3DSAMP_ADDRESSV_OLD, D3DTADDRESS_CLAMP_OLD );
  // set render target copy and depth to samplers 10 and 11
  // must have been prepared by CaptureRefractionsBackground before
  DoAssert(_renderTargetD._srView.NotNull());
  SetTexture( cb, 10, _renderTargetD._srView );
  SetTexture( cb, 11, GetDepthAsTexture() );
}


void EngineDD11::SetPixelShaderConstantTreeAdv( int cb, ColorVal matAmbient, ColorVal matDiffuse, 
                                              ColorVal matForcedDiffuse, ColorVal matEmissive, ColorVal matSpecular )
{
  Color treeAdvPars[5];
  // NOTE: all material color components rgb are halved!!!
  // unchanged:
  // matDiffuse: rgb ..original ambient and diffuse, a ..alpha mul ( x2 for new noise alpha )
  // specular: rgb ...specular color; a ...specularAmbiColorMul (specularAmbiColor = specularColor * a)
  // matAmbient: x ..crownBleed MUL, y ..crownBleed add, z ..ppBleedLevel, w ..ppBleedShift
  // matForcedDiffuse: x ..pp translucency level, y ..pp translucency shift, z ..crownCenterAttMul, w ..crownCenterAttAdd
  // matEmissive: rgb ..translucency color, crownTranslucencyMainLevel

  const float k2 = 2.0f;
  // crown bleed
  /*float crownBleedLevel = matAmbient.R() * k2;
  float crownBleedShift = matAmbient.G() * k2;
  float crownBleedMul = 1.0 - crownBleedLevel;
  float crownBleedAdd = crownBleedLevel + crownBleedShift;*/

  // per-pixel bleed
  float ppBleedLevel = matAmbient.B() * k2;
  float ppBleedShift = matAmbient.A();
  float ppBleedMul = 1.0 - ppBleedLevel;
  float ppBleedAdd = ppBleedLevel + ppBleedShift;
  // per-pixel translucency
  float ppTranslucencyMul = -2.0f * k2 * matForcedDiffuse.R();
  //float ppTranslucencyAdd = 1.0f - matForcedDiffuse.G() * 2.0f;
  float ppTranslucencyAdd = 2.0f * k2 * matForcedDiffuse.G() - 1.0f;
  //
  float crownCenterAttMul = matForcedDiffuse.B() * k2 * 10.0;
  float crownCenterAttAdd = matForcedDiffuse.A() * 5.0 - 2.5;
  //
  float tk1 = k2;
  float tk2 = k2 * 2.0f * matEmissive.A();
  // translucency color for sky
  Color crownTranslucencyColor1 = matEmissive * tk1;
  // translucency color for sun
  Color crownTranslucencyColor2 = matEmissive * tk2;
  Color specularAmbiColor = matSpecular * matSpecular.A();
  // compose params to float4 [5]
  treeAdvPars[0] = Color( 0.9, 0.5, ppBleedMul, ppBleedAdd );  
  treeAdvPars[1] = Color( ppTranslucencyMul, ppTranslucencyAdd, crownCenterAttMul, crownCenterAttAdd );
  treeAdvPars[2] = crownTranslucencyColor1;
  treeAdvPars[3] = crownTranslucencyColor2;
  treeAdvPars[4] = specularAmbiColor;
  SetPixelShaderConstantF(cb, PSC_TreeAdvPars, (float*)treeAdvPars, 5);
}

void EngineDD11::SetPixelShaderNightEmissiveConst(int cb)
{
#if _HELISIM_FAKE_NIGHT_LIGHTS
  static float EmissiveCoeff = 1.0f;

#if _ENABLE_CHEATS
  if (GInput.GetCheat3ToDo(DIK_R))
  {
    EmissiveCoeff += 0.05f;
    saturateMax(EmissiveCoeff, 0.0f);
    DIAG_MESSAGE_ID(500, 177, Format("Night light coef: %.4f", EmissiveCoeff));
  }

  if (GInput.GetCheat3ToDo(DIK_T))
  {
    EmissiveCoeff -= 0.05f;
    saturateMax(EmissiveCoeff, 0.0f);
    DIAG_MESSAGE_ID(500, 177, Format("Night light coef: %.4f", EmissiveCoeff));
  }
#endif

  float nighEffect = GScene->MainLight()->NightEffect() * EmissiveCoeff;
  nighEffect = sqrtf(nighEffect);
  D3DXVECTOR4 lightEmissiveCoef(nighEffect, nighEffect, nighEffect, nighEffect);
  SetPixelShaderConstantF(cb, PSC_NightEmissiveCoef, (float*)&lightEmissiveCoef, 1);
#else
  D3DXVECTOR4 lightEmissiveCoef(0, 0, 0, 0);
  SetPixelShaderConstantF(cb, PSC_NightEmissiveCoef, (float*)&lightEmissiveCoef, 1);
#endif
}

void EngineDD11::SetPixelShaderConstantSuper(int cb, ColorVal matSpecular)
{
  // Environmental map color modificator
  LightSun *sun = GScene->MainLight();
  Color lighting = sun->SimulateDiffuse(0.5) * GetAccomodateEye() * _modColor * EnvMapScale;
  SetPixelShaderConstantF(cb,PSC_GlassEnvColor, (float*)&lighting, 1);

  // Material specular color
  SetPixelShaderConstantF(cb, PSC_GlassMatSpecular, (float*)&matSpecular, 1);

  // Main light in world coordinates
  Vector3Val dir = CBState(cb)._direction;
  D3DXVECTOR4 direction(-dir.X(), -dir.Y(), -dir.Z(), 1.0f);
  SetPixelShaderConstantF(cb, PSC_WLight, (float*)&direction, 1);
}

void EngineDD11::SetPixelShaderConstantTreePRT(int cb, ColorVal matEmmisive, ColorVal matAmbient, ColorVal matDiffuse)
{
  // Local base vectors for RGBA (RGB vector are 35 degrees from XZ plane and thus roughly 45 degrees from each other)
  Vector3 localVector[4];
  localVector[0] = Vector3(0.81915204429,  0.57357643635,  0);
  localVector[1] = Vector3(-0.40957602214, 0.57357643635,  0.70940647991);
  localVector[2] = Vector3(-0.40957602214, 0.57357643635,  -0.70940647991);
  localVector[3] = Vector3(0,              -1,             0);

  // Get the light and up direction in the object space
  Vector3 tDirection = TransformAndNormalizeVectorToObjectSpace(cb,CBState(cb)._direction);
  Vector3 tDirectionUp = TransformAndNormalizeVectorToObjectSpace(cb,VUp);

  // Get required light values
  Color diffuse = CBState(cb)._lightDiffuse * matDiffuse;
#if _ARMA3_NEW_LIGHTING_CFG
  Color ground = ComputeBackReflection(diffuse);
#else
  LightSun *sun = GScene->MainLight();
  Color ground = sun->GroundReflection() * diffuse;
#endif
  Color ae = matEmmisive * GetAccomodateEye() + CBState(cb)._lightAmbient * matAmbient;
#if _ARMA3_NEW_LIGHTING_CFG
  Color ge = matEmmisive * GetAccomodateEye() + ComputeGroundReflection(CBState(cb)._lightAmbient,matAmbient);
#else
  Color ge = matEmmisive * GetAccomodateEye() + sun->GroundReflection() * CBState(cb)._lightAmbient * matAmbient;
#endif

  // Calculate light values for the local vectors
  Color localLightValueDirect[4];
  Color localLightValueIndirect[4];
  for (int i = 0; i < 4; i++)
  {
    localLightValueDirect[i] = HBlack;
    localLightValueIndirect[i] = HBlack;

    // Include diffuse color
    localLightValueDirect[i] += diffuse * floatMax(localVector[i].DotProduct(-tDirection), 0.0f);

    // Include diffuse back color
    localLightValueIndirect[i] += ground * floatMax(-localVector[i].DotProduct(-tDirection), 0.0f);

    // Include ambient color
    float lerpFactor = (localVector[i].DotProduct(tDirectionUp) + 1.0f) * 0.5f;
    localLightValueIndirect[i] += ge * (1.0f-lerpFactor) + ae * lerpFactor;
  }

  // Set up the PRT constants
  Color prtC[3*2];
  prtC[0] = Color(localLightValueDirect[0].R(), localLightValueDirect[1].R(), localLightValueDirect[2].R(), localLightValueDirect[3].R());
  prtC[1] = Color(localLightValueDirect[0].G(), localLightValueDirect[1].G(), localLightValueDirect[2].G(), localLightValueDirect[3].G());
  prtC[2] = Color(localLightValueDirect[0].B(), localLightValueDirect[1].B(), localLightValueDirect[2].B(), localLightValueDirect[3].B());
  prtC[3] = Color(localLightValueIndirect[0].R(), localLightValueIndirect[1].R(), localLightValueIndirect[2].R(), localLightValueIndirect[3].R());
  prtC[4] = Color(localLightValueIndirect[0].G(), localLightValueIndirect[1].G(), localLightValueIndirect[2].G(), localLightValueIndirect[3].G());
  prtC[5] = Color(localLightValueIndirect[0].B(), localLightValueIndirect[1].B(), localLightValueIndirect[2].B(), localLightValueIndirect[3].B());
  SetPixelShaderConstantF(cb, PSC_PRTConstantDirect, (float*)prtC, 3*2); // PSC_PRTConstantIndirect is filled out as well
}

void EngineDD11::DrawMeshTLSectionsInstanced(int cb, const Shape &sMesh, const SectionMaterialLODs &matLOD, const EngineShapeProperties &prop, int spec, int bias, int instancesOffset, int instancesCount)
{
  // Go through the sections
  for (int i = 0; i < sMesh.NSections(); i++)
  {
    const ShapeSection &sec = sMesh.GetSection(i);

    // Proxy or hidden - don't draw
    if (sec.Special()&(IsHidden|IsHiddenProxy)) continue;

    // Calculate the bias for section
    int secBias = ((sec.Special()&ZBiasMask)/ZBiasStep) * 5;
    int maxBias = intMax(bias, secBias);

    // Print material

    // distribute by predefined materials
    TLMaterial mat;
    CreateMaterial(mat, HWhite);

    // Prepare and draw section
    int materialLevel = (matLOD.Size() > 0) ? matLOD[i] : 0;
    sec.PrepareTL(cb,mat, spec, prop);
    DrawSectionTL(cb,sMesh, i, i+1, maxBias, instancesOffset, instancesCount);
  }
}

Vector3 EngineDD11::TransformPositionToObjectSpace(int cb, Vector3Par position)
{
  return CBState(cb)._invWorld * position;
}

Vector3 EngineDD11::TransformAndNormalizePositionToObjectSpace(int cb, Vector3Par position)
{
  // Get the original distance
  float positionToModelDistance = position.Distance(CBState(cb)._world.Position());

  // Transform the position
  Vector3 newPosition = CBState(cb)._invWorld * position;

  // Normalize the position
  if (newPosition.SquareSize()>0)
  {
    float coef = positionToModelDistance * newPosition.InvSize();
    newPosition = newPosition * coef;
  }

  return newPosition;
}

Vector3 EngineDD11::TransformAndNormalizeVectorToObjectSpace(int cb, Vector3Par vector)
{
  Vector3 newVector = CBState(cb)._invWorld.Orientation() * vector;
  return newVector.Normalized();
}

void EngineDD11::SetFiltering(int cb, int stage, TexFilter filter, int maxAFLevel)
{
  switch (filter)
  {
  case TFAnizotropic:
  case TFAnizotropic2:
  case TFAnizotropic4:
  case TFAnizotropic8:
  case TFAnizotropic16:
    if (maxAFLevel>1)
    {
      D3DSetSamplerState(cb, stage,D3DSAMP_MINFILTER_OLD,D3DTEXF_ANISOTROPIC_OLD);
      D3DSetSamplerState(cb, stage,D3DSAMP_MAGFILTER_OLD,D3DTEXF_ANISOTROPIC_OLD); 
      D3DSetSamplerState(cb, stage,D3DSAMP_MIPFILTER_OLD,D3DTEXF_ANISOTROPIC_OLD);
      D3DSetSamplerState(cb, stage,D3DSAMP_MAXANISOTROPY_OLD,maxAFLevel);
      break;
    }
    // when AF disabled, fall-through
  case TFTrilinear:
    D3DSetSamplerState(cb, stage,D3DSAMP_MINFILTER_OLD,D3DTEXF_LINEAR_OLD);
    D3DSetSamplerState(cb, stage,D3DSAMP_MAGFILTER_OLD,D3DTEXF_LINEAR_OLD); 
    D3DSetSamplerState(cb, stage,D3DSAMP_MIPFILTER_OLD,D3DTEXF_LINEAR_OLD);
    break;
  case TFLinear:
    D3DSetSamplerState(cb, stage,D3DSAMP_MINFILTER_OLD,D3DTEXF_LINEAR_OLD);
    D3DSetSamplerState(cb, stage,D3DSAMP_MAGFILTER_OLD,D3DTEXF_LINEAR_OLD); 
    D3DSetSamplerState(cb, stage,D3DSAMP_MIPFILTER_OLD,D3DTEXF_POINT_OLD);
    break;
  case TFPoint:
    D3DSetSamplerState(cb, stage,D3DSAMP_MINFILTER_OLD,D3DTEXF_POINT_OLD);
    D3DSetSamplerState(cb, stage,D3DSAMP_MAGFILTER_OLD,D3DTEXF_POINT_OLD);  
    D3DSetSamplerState(cb, stage,D3DSAMP_MIPFILTER_OLD,D3DTEXF_POINT_OLD);
    break;
  }
}

void EngineDD11::SBTR_DefaultCreate()
{
  _sbSize = ShadowBufferSize;
  if (_sbQuality>1 && _caps._maxTextureSize>=ShadowBufferSizeHQ)
  {
    _sbSize = ShadowBufferSizeHQ;
  }
  HRESULT err;
  bool creationFailed = true;

#ifdef _XBOX
  // Render target is not needed on XBOX
  _renderTargetSB.Free();
  _renderTargetSurfaceSB.Free();

  D3DSURFACE_PARAMETERS sp;
  memset(&sp, 0, sizeof(D3DSURFACE_PARAMETERS));
  sp.Base = 0;
  if ((err = CreateDepthStencilSurface(_sbSize, _sbSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, _renderTargetDepthBufferSurfaceSB, _renderTargetSurfaceSB, &sp)) == D3D_OK)
  {
    // Create texture the SB to be resolved into
    if ((err = _d3DDevice->CreateTexture(_sbSize, _sbSize, 1, 0, D3DFMT_G16R16F, D3DPOOL_DEFAULT, _renderTargetSB.Init(), NULL)) == D3D_OK)
    {
      creationFailed = false;
    }
    else
    {
      DDError9("Error: Shadow buffer depth stencil texture creation failed.", err);
    }
    // Create texture the SB to be resolved into
    sp.Base += _renderTargetDepthBufferSurfaceSB->Size / GPU_EDRAM_TILE_SIZE;
    if ((err = _d3DDevice->CreateRenderTarget(_sbSize, _sbSize, D3DFMT_G16R16F, D3DMULTISAMPLE_NONE, 0, FALSE, _renderTargetSurfaceSB.Init(), &sp)) == D3D_OK)
    {
      creationFailed = false;
    }
    else
    {
      DDError9("Error: Shadow buffer render target creation failed.", err);
    }
  }
  else
  {
    DDError9("Error: Shadow buffer depth stencil surface creation failed.", err);
  }

#else
  err = CreateDepthStencil(_sbSize, _sbSize, DXGI_FORMAT_R32_TYPELESS, 1, 0, D3D11_USAGE_DEFAULT,
    true, _depthStencilSB, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT);

  if (err == D3D_OK)
  {
    creationFailed = false;
  }
  else
  {
    DDError11("Error: Shadow buffer depth stencil surface creation failed.", err);
  }
#endif

  if (creationFailed)
  {
    DestroySBResources();
    return;
  }
}

void EngineDD11::DisplayWaitScreen()
{
  //  _thermalVision = false;

  if (!ResetNeeded())
  {
    // we should be outside the frame rendering now

    if (!InitDrawDone())
    {
      // perform full frame rendering
      InitDraw(true);
      // TODO: render some text message here
      FinishDraw(1,true);
      NextFrame(true);
    }

  }
}

bool EngineDD11::BeginPredicationMask(int cb, int mask,PredicationMode pass)
{
  RendStateExt &cbState = CBState(cb);
  if (!_bgRecPredication)
  {
    // not recording for prediction - use record time implementation
    if (mask&(1<<pass))
    {
      cbState.StartPredication(mask);
      return true;
    }
    return false;
  }
  else
  {
    // verify someone is able to play the predication
    Assert(Glob.config._useCB || Glob.config._renderThread!=Config::ThreadingNone);
    Assert(Glob.config._useCB || _backgroundD3D==RecLowLevel);
    // record predicate
#if 0 // _DEBUG
    cbState._rec.AppendDebugText(Format("Pred %d {\n",mode));
#endif
    cbState._rec.FlushPredication();
    cbState.StartPredication(mask);
    return true;
  }
}

void EngineDD11::EndPredication( int cb )
{
  RendStateExt &cbState = CBState(cb);
  if (!_bgRecPredication)
  {
    // record-time implementation
    cbState.StopPredication();
  }
  else
  {
    cbState._rec.FlushPredication();
    CBState(cb).StopPredication();
#if 0 //_DEBUG
    cbState._rec.AppendDebugText("Pred}\n");
#endif
  }
}

/** can use more sections, can use more than 65536 vertices */
void EngineDD11::DrawIndexedPrimitive(int cb, int beg, int end, const Shape &sMesh, int instancesOffset, int instancesCount)
{
  // no queueing, direct drawing
  VertexBufferD3D11 *buf = static_cast<VertexBufferD3D11 *>(sMesh.GetVertexBuffer());

  DoAssert(buf->_sections.Size() >= end);

#ifdef DONT_USE_DYNBUFFERS
  if (buf->_dynamic)
  {
    // DX11 TODO DRAW
    //Array<char> vertexData((char *)_dynVBuffer._geometry.Data(),_dynVBuffer._geometry.Size()*sizeof(SVertex9));
    //CALL_CB(cb,DrawPrimitiveUP,D3DPT_TRIANGLELIST,_dynVBuffer._geometry.Size() / 3,vertexData,sizeof(SVertex9));
    DrawPrimitive(cb, D3DPT_TRIANGLELIST_OLD, 0, _dynVBuffer._geometry.Size() / 3);
    // using DrawPrimitiveUP means we have reset stream 0
    CBState(cb)._vBufferLast = VertexStaticData11::BufferFuture(VertexStaticData11::BufferFuture::Null);
    CBState(cb)._iBufferLast = IndexStaticData11::BufferFuture(IndexStaticData11::BufferFuture::Null);
  }
  else
#endif
  {
    RendStateExt &cbState =  CBState(cb);

#if 0 // _ENABLE_REPORT
    // debugging opportunity - at this point we want to detect which model is rendering,
    // and when it is the one we are interested in, then capture (or compare) its state
    //static int captureStateId=4006;
    static int captureStateId=0x1003c044;
    static int captureBeg=0;
    static RenderingMode captureRenderingMode = RMCommon;
    static RendState capturedState;
    static bool doCapture = true;
    extern bool MTSerial;
    if (
      captureStateId>=0 &&
      cbState._debugId==captureStateId &&
      // rendering mode needs to match - in different passes the state would not match
      cbState._lastRenderingMode==captureRenderingMode &&
      beg==captureBeg
      )
    {
      if (MTSerial)
      {
        // CB not used - capture reference state if not captured yet
        if (doCapture)
        {
          capturedState = CBState(cb);
          doCapture = false;
        }
      }
      else
      {
        // CB used, compare to the reference state - if we already have it
        if (!doCapture)
        {
          DoAssert(capturedState.ReportDifferences(CBState(cb)));
        }
      }
    }
#endif

    while (beg < end)
    {
      // check which sections can be concatenated could be performed here

      // scan sections to provide reasonable values for vertex range
      int endDraw;
      const VBSectionInfo &siBeg = buf->_sections[beg];
      int begVertex = siBeg.begVertex, endVertex = siBeg.endVertex;
      for (endDraw = beg + 1; endDraw < end; endDraw++)
      {
        const VBSectionInfo &siCur = buf->_sections[endDraw-1];
        const VBSectionInfo &siNxt = buf->_sections[endDraw];
        if (siCur.end!=siNxt.beg)  break;
        saturateMin(begVertex,siCur.begVertex);
        saturateMax(endVertex,siCur.endVertex);
      }
      Assert (endDraw>beg);
      Assert (endVertex>=begVertex);
      const VBSectionInfo &siEnd = buf->_sections[endDraw-1];

      // all attributes prepared


      //const ShapeSection &sec = sMesh.GetSection(section);
      // actualy draw all data
      // note: min index, max index is not used by HW T&L
      //LogF("TLStart");
      // get vbuffer size
      //PROFILE_DX_SCOPE(3drTL);
#if !_RELEASE
      if (endVertex + cbState._vOffsetLast > cbState._vBufferSize)
      {
        LogF("vrange %d..%d",begVertex,endVertex);
        LogF("offset %d",cbState._vOffsetLast);
        LogF("vb size %d",cbState._vBufferSize);
        Fail("Vertex out of range");
        return;
      }
#endif
      //DoAssert(_d3dFrameOpen);
      int nInstances = instancesCount > 0 ? instancesCount : 1;
      // note: begVertex and endVertex do not work when nInstances>1
#if DO_TEX_STATS
      if (LogStatesOnce)
      {
        LogF
          (
          "  %d: Draw TL .... %dx%d tris (%d..%d,%s)",
          DPrimIndex++,
          (siEnd.end-siBeg.beg)/3,nInstances,
          beg,end,
          (const char *)sMesh.GetSection(beg).GetDebugText()
          );
      }
#endif

      int triangleCount;

#if 0 // DO_TEX_STATS
      if (LogStatesOnce)
      {
        LogF("Render states used for this section:");
        for (int i = 0; i < _renderState.Size(); i++)
        {
          if (_renderState[i].value != 0xFFFFFFFF)
          {
            LogF("%d, %d", i, _renderState[i].value);
          }
        }
      }
#endif

      {
        PROFILE_SCOPE_EX(drwIdxPrm, dd11);
        triangleCount = ((siEnd.end-siBeg.beg)/3) * nInstances;
        if (LimitTriCount>=0 && triangleCount>LimitTriCount)
        {
          triangleCount = LimitTriCount;
        }
        if ((LimitDIPCount < 0) || (_dipCount < LimitDIPCount))
        {
          int instanceOffset = sMesh.NVertex() * (nInstances - 1);

          // Draw primitives (in more steps if necessary)
          const int MaxPrimitiveCount = 65535;
          int drawnPrimitives = 0;
          do
          {
            int tCount = intMin(triangleCount - drawnPrimitives, MaxPrimitiveCount);

            DrawIndexedPrimitive(cb, 
              D3DPT_TRIANGLELIST_OLD,
              cbState._vOffsetLast,
              begVertex,endVertex - begVertex + instanceOffset,
              siBeg.beg+drawnPrimitives*3, tCount
              );
            _dipCount++;
#if _ENABLE_PERFLOG
            ADD_COUNTER(dPrim,1);
#endif
            drawnPrimitives += tCount;
          } while (drawnPrimitives < triangleCount);

#if _ENABLE_PERFLOG
          ADD_COUNTER(tris,triangleCount);
#endif
        }
        else
        {
          // No draw call is needed for this branch.
        }
      }
      //LogF("TLEnd");
      beg = endDraw;
    }
  }
}

/**
the function is very short and no lazy calling of it has any sense.
Comparing current state is more difficult than finding a shader
*/

void EngineDD11::SetupSectionTL(
                               int cb, EMainLight mainLight, EFogMode fogMode,
                               VertexShaderID vertexShaderID, const UVSource *uvSource, int uvSourceCount, bool uvSourceOnlyTex0,
                               ESkinningType skinningType,
                               bool instancing, int instancesCount, int pointLightsCount, int spotLightsCount,
                               bool shadowReceiver, RenderingMode renderingMode, bool backCullDisabled, int cratersCount,
                               const TexMaterial *mat
                               )
{
  //ADD_COUNTER(sSecTL,1);
  PROFILE_SCOPE_EX(stpSctnTL, dd11);

  if (vertexShaderID < 0 || vertexShaderID >= NVertexShaderID)
  {
    Fail("Error: Vertex shader ID out of limits");
    //cbState._vertexShader.Free();
    return;
  }

  // Set vertex shader
  PredicationMode pass = CurrentPredicationMode();

  // select vertex shaders for rendering and priming
  VertexShaderInfo* vs[NPredicationModes];
  bool noPredication = false;

  // Setup VS constants for branching and select desired vertex shader
  {
    // Rendering mode
    RenderingMode rMode = cratersCount>0 ? RMCrater : renderingMode==RMShadowBuffer ? renderingMode : RMCommon;

    // Craters on terrain most likely will not work properly (as we use the non-blended position in the vertex shader) (on the other hand, the blending doesn't
    // occur on close distances). Anyway, terrain use own craters.
    DoAssert(rMode != RMCrater || (vertexShaderID != VSTerrain && vertexShaderID != VSTerrainGrass));

    // Setup the boolean constants for VS branches. This is important for all
    // common shaders and some special ones. It is not necessary for shadow volumes,
    // but it's still performed because of simplicity
    {
      // FogModeA & FogModeB
      bool fogModeB[2];
      switch (fogMode)
      {
      case FM_None:
        fogModeB[0] = false;
        fogModeB[1] = false;
        break;
      case FM_Fog:
        fogModeB[0] = false;
        fogModeB[1] = true;
        break;
      case FM_Alpha:
        fogModeB[0] = true;
        fogModeB[1] = false;
        break;
      case FM_FogAlpha:
        fogModeB[0] = true;
        fogModeB[1] = true;
        break;
      }
      SetVertexShaderConstantB(cb, VSC_FogModeA, fogModeB, 2);

      // ShadowReceiverFlag
      // SBTechniqueDefault
      // EnableAlignNormal
      bool sbTechniqueDefault = false; 
      bool sssmShadowReceiver = false;
      bool c4567[]={shadowReceiver,sbTechniqueDefault,backCullDisabled, sssmShadowReceiver};
      SetVertexShaderConstantB(cb, VSC_ShadowReceiverFlag, c4567, 4);
    }

    // Get the vertex shader
    if (VSToVSP[vertexShaderID] < NVertexShaderPoolID)
    {
      // Pick up the proper shader
      {
        // UVInput
        int uvInputN = uvSourceOnlyTex0 ? 0 : 1;

        // MainLight
        int mainLightN;
        switch (mainLight)
        {
        case ML_None: mainLightN = VML_None;break;
        case ML_Sun: mainLightN = VML_Sun;break;
        case ML_Sky: mainLightN = VML_Sky;break;
        default: mainLightN = VML_SetColor;break;
        }

        // SkinningInstancing
        int siN;
        if (skinningType == STNone && !instancing)
        {
          siN = 0;
        }
        else if (skinningType != STNone && !instancing)
        {
          siN = 1;
        }
        else if (skinningType == STNone && instancing)
        {
          siN = 2;
        }
        else
        {
          Fail("Error: No skinning and instancing supported together");
          siN = 0;
        }

        // Use the vertex shader
        vs[RenderingPass] = &_vertexShaderPool[VSToVSP[vertexShaderID]][uvInputN][mainLightN][rMode][siN];
#if _ARMA3_CAUSTICS
        vs[CausticsPass] = &_vertexShaderPool[VSToVSP[vertexShaderID]][uvInputN][mainLightN][RMCaustics][siN];
#endif
        if (rMode!=RMCrater && rMode!=RMShadowBuffer)
        {
          vs[PrimingPass] = &_vertexShaderPool[VSToVSP[vertexShaderID]][uvInputN][mainLightN][RMDPrime][siN];
          vs[DepthPass] = vs[PrimingPass]; // WIP: ATOC: do not generate RMZPrime VS at all, use the same as used for RMDPrime
        }
        else
        {
          // no priming possible here - mark to be predicated out
          vs[PrimingPass] = NULL;
          vs[DepthPass] = NULL;
        }
      }
    }
    else if (vertexShaderID == VSShadowVolume)
    {
      // SkinningInstancing
      int siN;
      if (skinningType == STNone && !instancing)
      {
        siN = 0;
      }
      else if (skinningType != STNone && !instancing)
      {
        siN = 1;
      }
      else if (skinningType == STNone && instancing)
      {
        siN = 2;
      }
      else
      {
        Fail("Error: No skinning and instancing supported together");
        siN = 0;
      }

      // Use the vertex shader
      VertexShaderInfo& shader = _vertexShaderShadowVolume[siN];
      for (int i=0; i<NPredicationModes; i++) vs[i] = &shader; // pass all the same to indicate no predication
      noPredication = true;
    }
    else if (VSToVSS[vertexShaderID] < NVertexShaderSpecialID)
    {
      // Use the vertex shader
      VertexShaderInfo &shader = _vertexShaderSpecial[VSToVSS[vertexShaderID]];
      for (int i=0; i<NPredicationModes; i++) vs[i] = &shader; // pass all the same to indicate no predication
      noPredication = true;
      // should not be called in priming pass at all
      Assert(pass!=PrimingPass);
    }
    else if (vertexShaderID == VSTerrain)
    {
      // Use the vertex shader
      vs[RenderingPass] = &_vertexShaderTerrain[rMode];
#if _ARMA3_CAUSTICS
      vs[CausticsPass] = &_vertexShaderTerrain[RMCaustics];
#endif
      vs[PrimingPass] = &_vertexShaderTerrain[RMDPrime];
      vs[DepthPass] = vs[PrimingPass];
    }
    else if (vertexShaderID == VSTerrainGrass)
    {
      // Use the vertex shader
      vs[RenderingPass] = &_vertexShaderTerrainGrass[rMode];
#if _ARMA3_CAUSTICS
      vs[CausticsPass] = &_vertexShaderTerrainGrass[RMCaustics];
#endif
      vs[PrimingPass] = &_vertexShaderTerrainGrass[RMDPrime];
      vs[DepthPass] = vs[PrimingPass];
    }
    else if (vertexShaderID == VSRoad)
    {
      // Use the vertex shader
      vs[RenderingPass] = &_vertexShaderRoad[rMode];
#if _ARMA3_CAUSTICS
      vs[CausticsPass] = &_vertexShaderRoad[RMCaustics];
#endif
      vs[PrimingPass] = &_vertexShaderRoad[RMDPrime];
      vs[DepthPass] = vs[PrimingPass];
    }
  }

  // Set pixel shader and some pixel shader constants
  DoSelectPixelShader(cb,mat,cratersCount);

#if defined _XBOX || TRACK_SHADER_PATCH
  IDirect3DVertexShader9 *vsToSet[NPredicationModes];
  {
    RendState &cbState = CBState(cb);
    // try to provide a bound VS if possible
    for (int i=0; i<NPredicationModes; i++)
    {
      D3DBoundShaderName name(vs[i],cbState._vertexDeclLast,cbState._strideLast,cbState._pixelShaderLast[i]);
      vsToSet[i] = _boundShaders.GetBoundShader(name);
      if (!vsToSet[i])
      {
        vsToSet[i] = vs[i];
#if TRACK_SHADER_PATCH
        TrackShaderPatch(name);
#endif
      }
    }
  }
#else
  VertexShaderInfo* (&vsToSet)[NPredicationModes] = vs;
#endif

  if (_renderingMode==RMShadowBuffer || noPredication)
  {
    SetVertexShader(cb, vsToSet[RenderingPass]);
  }
  else
  {
    for (int i=0; i<NPredicationModes; i++)
    {
      if (BeginPredication(cb,PredicationMode(i),pass))
      {
        SetVertexShader(cb, vsToSet[i]);
        EndPredication(cb);
      }
    }
  }  
}


/*!
\patch 5126 Date 2/5/2007 by Flyman
- Fixed: Integer light constants as I_POINTLOOPCOUNT were not recorded and there was a problem when using compass during the day
*/
void EngineDD11::SetupVSConstants(int cb, int minBoneIndex, int bonesCount)
{
  RendState &cbState = CBState(cb);
  if (VSToVSP[cbState._vertexShaderID] < NVertexShaderPoolID)
  {
    SetupVSConstantsPool(cb,minBoneIndex, bonesCount);
    if (cbState._vertexShaderID == VSUnderwaterOcclusion)
    {
      SetupVSConstantsUnderwaterOcclusion(cb); //setup additional constant for underwater occlusion object
    }
  }
  else if (cbState._vertexShaderID == VSShadowVolume)
  {
    SetupVSConstantsShadowVolume(cb,minBoneIndex, bonesCount);
  }
  else if (VSToVSS[cbState._vertexShaderID] < NVertexShaderSpecialID)
  {
    if (cbState._vertexShaderID == VSSprite || cbState._vertexShaderID == VSSpriteOnSurface)
    {
      SetupVSConstantsSprite(cb);
    }
    else if (cbState._vertexShaderID == VSVolCloud)
    {
      SetupVSConstantsVolCloud(cb);
    }
    else if (cbState._vertexShaderID == VSPoint)
    {
      SetupVSConstantsPoint(cb);
    }
    else if (cbState._vertexShaderID == VSWater)
    {
      SetupVSConstantsWater(cb);
    }
    else if (cbState._vertexShaderID == VSWaterSimple)
    {
      SetupVSConstantsWater(cb);
    }
    else if (cbState._vertexShaderID == VSShore)
    {
      SetupVSConstantsShore(cb);
    }
    else if( cbState._vertexShaderID == VSCalmWater )
    {
      SetVertexShaderConstantCalmWater( cb, cbState );
    }
  }
  else if (cbState._vertexShaderID == VSTerrain)
  {
    SetupVSConstantsTerrain(cb);
  }
  else if (cbState._vertexShaderID == VSTerrainGrass)
  {
    SetupVSConstantsTerrainGrass(cb);
  }
  else if (cbState._vertexShaderID == VSRoad)
  {
    // roads uses same constants as terrain
    SetupVSConstantsTerrain(cb);
  }
  else
  {
    Fail("Error: unknown vertex shader ID");
  }
}

void EngineDD11::SetupPSConstants(int cb)
{
  const RendState &cbState = CBState(cb);
  if (cbState._mainLight == ML_Sun)
  {
    // Determine the diffuseBack should be used (simulate light transmitted through the other side - like leafs) - trees, grass
    bool useGroundBidir;
    VertexShaderID vs = cbState._vertexShaderID;
    if ((vs == VSNormalMapThrough) ||
      (vs == VSNormalMapSpecularThrough) ||
      (vs == VSNormalMapThroughNoFade) ||
      (vs == VSNormalMapSpecularThroughNoFade) ||
      (vs == VSTree) ||
      /*(vs == VSTerrainGrass) ||*/
      (vs == VSTreeNoFade) ||
      (vs == VSTreeAdv) ||
      (vs == VSTreeAdvModNormals)
      )
    {
      useGroundBidir = false;
    }
    else
    {
      useGroundBidir = true;
    }

    // Determine the new specular calculation should be used
    bool useNewSpecular = vs == VSSuper || vs==VSSkin
#if _ARMA3_REFRACTION
      || vs == VSRefract
#endif     
      ;

    SetPixelShaderConstantDBS(
      cb,cbState._matDiffuse,cbState._matForcedDiffuse,cbState._matSpecular,cbState._matPower,
      useGroundBidir,
      useNewSpecular);
    SetPixelShaderConstant_A_DFORCED_E_Sun(cb,cbState._matEmmisive, cbState._matAmbient, cbState._matDiffuse, cbState._matForcedDiffuse);
  }
  else
  {
    SetPixelShaderConstantDBSZero(cb);
    SetPixelShaderConstant_A_DFORCED_E_NoSun(cb,cbState._matEmmisive);
  }
}

void EngineDD11::SetVertexDeclaration(int cb, ID3D11InputLayout *decl, bool optimize)
{
  // DX11 - vertex declaration (input layout) is now set while setting shader
}

TextureHandle11* EngineDD11::GetDepthAsTexture() const
{
#if _HELISIM_RENDER_TO_TEXTURE
  return _r2tBegin ? _r2tRT._renderTargetDepthInfo._srView : _renderTargetDepthInfo._srView;
#else
  return _renderTargetDepthInfo._srView;
#endif
}

void EngineDD11::SetupVSConstantsPSLights(int cb, bool setupPSInstead)
{
  // Array of zeroes to be used to zero some registers
  static const int zeroes[4] = {0, 0, 0, 0};

  if (_renderingMode!=RMCommon && !Glob.config._predication || _renderingMode==RMShadowBuffer)
  {
    // no light setup needed when lights are not used at all
    SetVertexShaderConstantI(cb,VSC_SpotLoopCount, zeroes, 1);
    SetPixelShaderConstantI(cb,PSC_SpotLoopCount, zeroes, 1);
    return;
  }
  // Point lights
  {
    SLPoint LPointData[MAX_LIGHTS];
    int pointLightIndex = 0;
    for (int i = 0; i < CBState(cb)._lights.Size(); i++)
    {
      LightDescription ldesc;
      CBState(cb)._lights[i]->GetDescription(ldesc, GetAccomodateEye());
      if (ldesc.type == LTPoint)
      {
        // temporary solution for SkinShader - _matEmmisive and matForcedDiffuse holds skin params
        Color matForcedDiff;
        Color matAmbient;
        if( ( CBState(cb)._vertexShaderID == VSSkin ) 
          || ( CBState(cb)._vertexShaderID == VSTreeAdv ) 
          || ( CBState(cb)._vertexShaderID == VSTreeAdvTrunk )
          || ( CBState(cb)._vertexShaderID == VSTreeAdvModNormals ) ) //|| ( CBState(cb)._vertexShaderID == VSTreeAdvSimple ) )

        {
          matAmbient = CBState(cb)._matDiffuse;
          matForcedDiff = Color( 0, 0, 0, 1 );
        }
        else
        {
          matAmbient = CBState(cb)._matAmbient;
          matForcedDiff = CBState(cb)._matForcedDiffuse;
        }

        // LPoint_TransformedPos
        Vector3 tPos = TransformAndNormalizePositionToObjectSpace(cb,ldesc.pos);
        LPointData[pointLightIndex].TransformedPos = D3DXVECTOR4(tPos.X(), tPos.Y(), tPos.Z(), 1.0f);

        // LPoint_Atten
        float sa = Square(1.0f / ldesc.invRadius);
        LPointData[pointLightIndex].Atten = D3DXVECTOR4(sa, sa, sa, sa);

        // LPoint_D
        Color pointD = ldesc.diffuse * _night * CBState(cb)._matDiffuse;
        LPointData[pointLightIndex].D = D3DXVECTOR4(pointD.R(), pointD.G(), pointD.B(), CBState(cb)._matDiffuse.A());

        // LPoint_A
        Color pointA = ldesc.ambient * _night * matAmbient + ldesc.diffuse * matForcedDiff * _night;
        LPointData[pointLightIndex].A = D3DXVECTOR4(pointA.R(), pointA.G(), pointA.B(), pointA.A());

        // Increment index
        pointLightIndex++;

        // There might be more lights in the array than we require, so break whenever we reach the expected count
        if (pointLightIndex >= CBState(cb)._pointLightsCount) break;
      }
    }

    // Set the VS constants
    if (pointLightIndex > 0)
    {
      const int vectorsPerLight = sizeof(SLPoint) / sizeof(D3DXVECTOR4);
      if (setupPSInstead)
      {
        SetPixelShaderConstantF(cb,PSC_LPSData, (float*)LPointData, pointLightIndex * vectorsPerLight);
      }
      else
      {
        SetVertexShaderConstantF(cb,VSC_LPSData, (float*)LPointData, pointLightIndex * vectorsPerLight);
      }
    }

    // Set the number of point lights for shader's static looping purposes - see X360_INT_CONSTANT
    int pointLoopCount[4]={pointLightIndex,0,1,0};
    if (setupPSInstead)
    {
      SetVertexShaderConstantI(cb,VSC_PointLoopCount, zeroes, 1);
      SetPixelShaderConstantI(cb,PSC_PointLoopCount, pointLoopCount, 1);
    }
    else
    {
      SetVertexShaderConstantI(cb,VSC_PointLoopCount, pointLoopCount, 1);
      SetPixelShaderConstantI(cb,PSC_PointLoopCount, zeroes, 1);
    }
  }

  // Spot lights
  {
    SLSpot LSpotData[MAX_LIGHTS];
    int spotLightIndex = 0;
    for (int i = 0; i < CBState(cb)._lights.Size(); i++)
    {
      LightDescription ldesc;
      CBState(cb)._lights[i]->GetDescription(ldesc, GetAccomodateEye());
      if (ldesc.type == LTSpotLight)
      {
        // temporary solution for SkinShader - _matEmmisive and matForcedDiffuse holds skin params
        Color matForcedDiff;
        if( ( CBState(cb)._vertexShaderID == VSSkin ) 
          || ( CBState(cb)._vertexShaderID == VSTreeAdv ) 
          || ( CBState(cb)._vertexShaderID == VSTreeAdvTrunk )
          || ( CBState(cb)._vertexShaderID == VSTreeAdvModNormals ) ) //|| ( CBState(cb)._vertexShaderID == VSTreeAdvSimple ) )
          matForcedDiff = Color( 0, 0, 0, 1 );
        else
          matForcedDiff = CBState(cb)._matForcedDiffuse;

        // LSpot_TransformedPos
        Vector3 tPos = TransformAndNormalizePositionToObjectSpace(cb,ldesc.pos);
        LSpotData[spotLightIndex].TransformedPos_CosPhiHalf = D3DXVECTOR4(tPos.X(), tPos.Y(), tPos.Z(), ldesc.spotK);

        // LSpot_TransformedDir
        Vector3 tDir = TransformAndNormalizeVectorToObjectSpace(cb,ldesc.dir);
        LSpotData[spotLightIndex].TransformedDir_CosThetaHalf = D3DXVECTOR4(tDir.X(), tDir.Y(), tDir.Z(), ldesc.spotQ);

        // LSpot_D
        Color spotD = ldesc.diffuse * _night * CBState(cb)._matDiffuse;
        LSpotData[spotLightIndex].D_InvCTHMCPH = D3DXVECTOR4(spotD.R(), spotD.G(), spotD.B(), 1.0f / (ldesc.spotQ - ldesc.spotK));

        // LSpot_A
        Color spotA = ldesc.ambient * _night * CBState(cb)._matAmbient * 0.3f + ldesc.diffuse * matForcedDiff * _night;
        LSpotData[spotLightIndex].A_Atten = D3DXVECTOR4(spotA.R(), spotA.G(), spotA.B(), Square(1.0f / ldesc.invRadius) / 4.0f);

        // Increment index
        spotLightIndex++;

        // There might be more lights in the array than we require, so break whenever we reach the expected count
        if (spotLightIndex >= CBState(cb)._spotLightsCount) break;
      }
    }

    // Set the VS constants
    if (spotLightIndex > 0)
    {
      const int vectorsPerLight = sizeof(SLSpot) / sizeof(D3DXVECTOR4);
      if (setupPSInstead)
      {
        SetPixelShaderConstantF(cb,PSC_LPSData, (float*)LSpotData, spotLightIndex * vectorsPerLight, true, (MAX_LIGHTS - spotLightIndex) * vectorsPerLight);
      }
      else
      {
        SetVertexShaderConstantF(cb,VSC_LPSData, (float*)LSpotData, spotLightIndex * vectorsPerLight, true, (MAX_LIGHTS - spotLightIndex) * vectorsPerLight);
      }
    }

    // Set the number of spot lights for shader's static looping purposes - see X360_INT_CONSTANT
    int spotLoopCount[4]={spotLightIndex,0,1,0};
    if (setupPSInstead)
    {
      SetVertexShaderConstantI(cb,VSC_SpotLoopCount, zeroes, 1);
      SetPixelShaderConstantI(cb,PSC_SpotLoopCount, spotLoopCount, 1);
    }
    else
    {
      SetVertexShaderConstantI(cb,VSC_SpotLoopCount, spotLoopCount, 1);
      SetPixelShaderConstantI(cb,PSC_SpotLoopCount, zeroes, 1);
    }
  }
}

void EngineDD11::SetupVSConstantsLODPars(int cb)
{
  float lodPars[4];
  _sceneProps->GetTerrainLODPars(lodPars);
  SetVertexShaderConstantF(cb,VSC_TerrainLODPars, lodPars, 1);
}

void EngineDD11::SetupVSConstantsPool(int cb, int minBoneIndex, int bonesCount)
{
  RendState &cbState = CBState(cb);

  // temporary solution for SkinShader - _matAmbient, _matEmmisive and matForcedDiffuse holds specific params
  Color matAmbient;
  Color matEmissiveCoef;
  if( ( CBState(cb)._vertexShaderID == VSSkin ) 
    || ( CBState(cb)._vertexShaderID == VSTreeAdv ) 
    || ( CBState(cb)._vertexShaderID == VSTreeAdvTrunk ) 
    || ( CBState(cb)._vertexShaderID == VSTreeAdvModNormals ) ) //|| ( CBState(cb)._vertexShaderID == VSTreeAdvSimple ) )
  {
    matAmbient = cbState._matDiffuse;
    matEmissiveCoef = Color( 0, 0, 0, 0 );
  }
  else
  {
    matAmbient = cbState._matAmbient;
    matEmissiveCoef = cbState._matEmmisive * GetAccomodateEye();
  }
  // Shader specific constants
  {
    // NormalMapThrough
    if ((cbState._vertexShaderID == VSNormalMapThrough) ||
      (cbState._vertexShaderID == VSNormalMapSpecularThrough) ||
      (cbState._vertexShaderID == VSNormalMapThroughNoFade) ||
      (cbState._vertexShaderID == VSNormalMapSpecularThroughNoFade))
    {
      const Vector3 *treeCrown = GetTreeCrown(cb);
      Vector3Val min = treeCrown[0];
      Vector3Val max = treeCrown[1];
      // scale using bounding box
      // this converts imaginary bounding ellipsoid to unit sphere
      Vector3Val span = max-min;
      Vector3Val radius = span*0.5f; // make the sphere a little bit smaller
      Vector3Val center = min+radius;
      // scale.InvSize is used to convert result in meters to 0..1
      const float sqrt3 = 1.7320508076f;
      // bounding sphere is probably a little bit too large
      const float resize = 1.1f;

      if (radius.X()>0 && radius.Y()>0 && radius.Z()>0)
      {
        D3DXVECTOR4 centerSize[2]={
          D3DXVECTOR4(center.X(), center.Y(), center.Z(), 1.0f),
          D3DXVECTOR4(1/radius.X(),1/radius.Y(),1/radius.Z(),span.InvSize()*(sqrt3*resize))
        };
        SetVertexShaderConstantF(cb,VSC_TreeCrown, (float*)centerSize, 2);
      }
      else
      {
        D3DXVECTOR4 centerSize[2]={
          D3DXVECTOR4(center.X(),center.Y(),center.Z(),1),
          D3DXVECTOR4(1e9,1e9,1e9,1e9)
        };
        SetVertexShaderConstantF(cb,VSC_TreeCrown, (float*)centerSize, 2);
      }
    }
    else if( (cbState._vertexShaderID == VSTreeAdv) || (cbState._vertexShaderID == VSTreeAdvTrunk)|| (cbState._vertexShaderID == VSTreeAdvModNormals) )
    {
      const float k2 = 2.0f;
      float crownBleedMul = cbState._matAmbient.R() * k2 * 10.0f;
      float crownBleedAdd = cbState._matAmbient.G() * k2 * 10.0f - 5.0f;
      Color data = Color( crownBleedMul, crownBleedAdd, 0, 0 );
      SetVertexShaderConstantF( cb, VSC_TreeAdvPars, (float*)&data, 1 );
    }
    // Glass
    if (cbState._vertexShaderID == VSGlass || cbState._vertexShaderID == VSSuper || cbState._vertexShaderID == VSSkin
#if _ARMA3_REFRACTION
      || cbState._vertexShaderID == VSRefract
#endif         
      )
    {
      SetVertexShaderConstantLWSM(cb);
    }
  }

  // Common constants
  {
    // In case of skinning and many bones (that they weren't set directly) set the section bones
    if (cbState._skinningType != STNone)
    {
      if (!cbState._bonesWereSetDirectly)
      {
        DoAssert(minBoneIndex + bonesCount <= cbState._bones.Size());
        SetBones(cb,minBoneIndex, cbState._bones.Data(), bonesCount);
      }
    }

    // Some fog modes need to have some constants set
    if (cbState._fogMode == FM_Fog || cbState._fogMode == FM_FogAlpha)
    {
      SetVertexShaderConstantFFAR(cb);
    }

    // InitLights HLSL function uses the following constants
    if ((cbState._mainLight != ML_SunObject) && (cbState._mainLight != ML_SunHaloObject) && (cbState._mainLight != ML_MoonObject) && (cbState._mainLight != ML_MoonHaloObject))
    {
      SetVertexShaderConstantSAFR(cb,cbState._matPower, matAmbient );
    }

    // Point and spot lights
    if (cbState._mainLight < ML_Sky || cbState._mainLight > ML_MoonHaloObject)
    {
      SetupVSConstantsPSLights(cb, cbState._vertexShaderID == VSSuper || cbState._vertexShaderID == VSMulti
#if _ARMA3_REFRACTION
        || cbState._vertexShaderID == VSRefract
#endif        
        );
    }

    if (cbState._uvSourceCount > 0)
    {
      if (!cbState._uvSourceOnlyTex0)
      {
        // Fill out the VSC_TexCoordType
        DoAssert(cbState._uvSourceCount <= MaxStages);
        DoAssert((MaxStages % 4) == 0);
        float texCoordType[MaxStages];
        for (int i = 0; i < cbState._uvSourceCount; i++)
        {
          switch (cbState._uvSource[i])
          {
          case UVTex:
          case UVTexWaterAnim:
          case UVTexShoreAnim:
            texCoordType[i] = 0;
            break;
          case UVTex1:
            texCoordType[i] = 1;
            break;
          case UVWorldPos:
            Fail("TexGen source UVWorldPos not supported");
            texCoordType[i] = 0;
            break;
          case UVPos:
            texCoordType[i] = 0;
            break;
          case UVNone:
            texCoordType[i] = 0;
            break;
          default:
            ErrF("UV source %s not supported",cc_cast(FindEnumName(cbState._uvSource[i])));
            texCoordType[i] = 0;
            break;
          }
        }
        int constCount = (cbState._uvSourceCount+3)/4;
        Assert(constCount <= 2);
        SetVertexShaderConstantF(cb,VSC_TexCoordType, texCoordType, constCount);
      }
    }

    // Ambient color
    if ((cbState._mainLight != ML_SunObject) && (cbState._mainLight != ML_SunHaloObject) && (cbState._mainLight != ML_MoonObject) && (cbState._mainLight != ML_MoonHaloObject))
    {
      // AE
      Color newAE = matEmissiveCoef + cbState._lightAmbient * matAmbient;

      // Set the diffuse back coefficient
      float backCoef = cbState._matForcedDiffuse.A();
      newAE.SetA(backCoef);

      // G
#if _ARMA3_NEW_LIGHTING_CFG
      Color newGE = matEmissiveCoef + ComputeGroundReflection(cbState._lightAmbient, matAmbient);
#else
      LightSun *sun = GScene->MainLight();
      Color newGE = matEmissiveCoef + sun->GroundReflection() * cbState._lightAmbient * matAmbient;
#endif

      // Set the PS constants
      SetVertexShaderConstantF(cb,VSC_AE, (float*)&newAE, 1);
      SetVertexShaderConstantF(cb,VSC_GE, (float*)&newGE, 1);
    }
  }
}

void EngineDD11::SetupVSConstantsShadowVolume(int cb, int minBoneIndex, int bonesCount)
{
  Assert(_renderingMode!=RMShadowBuffer);
  // In case of skinning and many bones (that they weren't set directly) set the section bones
  if (CBState(cb)._skinningType != STNone)
  {
    if (!CBState(cb)._bonesWereSetDirectly)
    {
      DoAssert(minBoneIndex + bonesCount <= CBState(cb)._bones.Size());
      SetBones(cb,minBoneIndex, CBState(cb)._bones.Data(), bonesCount);
    }
  }

  // Set the light direction
  Vector3 tDirection = TransformAndNormalizeVectorToObjectSpace(cb,CBState(cb)._shadowDirection);
  D3DXVECTOR4 direction(tDirection.X(), tDirection.Y(), tDirection.Z(), 1.0f);
  SetVertexShaderConstantF(cb,VSC_LDirectionTransformedDir, (float*)direction, 1);
}

void EngineDD11::SetupVSConstantsSprite(int cb)
{
  // Some fog modes need to have some constants set
  if ((CBState(cb)._fogMode == FM_Fog) || (CBState(cb)._fogMode == FM_FogAlpha))
  {
    SetVertexShaderConstantFFAR(cb);
  }

  // InitLights HLSL function uses the following constants
  SetVertexShaderConstantSAFR(cb,CBState(cb)._matPower, CBState(cb)._matAmbient);

  // Point and spot lights
  SetupVSConstantsPSLights(cb);
}

void EngineDD11::SetupVSConstantsVolCloud(int cb)
{
  // LUKE TODO

  // Some fog modes need to have some constants set
  if ((CBState(cb)._fogMode == FM_Fog) || (CBState(cb)._fogMode == FM_FogAlpha))
  {
    SetVertexShaderConstantFFAR(cb);
  }

  // InitLights HLSL function uses the following constants
  SetVertexShaderConstantSAFR(cb,CBState(cb)._matPower, CBState(cb)._matAmbient);

  // Point and spot lights
  SetupVSConstantsPSLights(cb);
}

void EngineDD11::SetupVSConstantsPoint(int cb)
{
  // AE
  Color newA_E = CBState(cb)._matEmmisive * GetAccomodateEye();
  SetVertexShaderConstantF(cb,VSC_AE, (float*)&newA_E, 1);

  // Some fog modes need to have some constants set
  if ((CBState(cb)._fogMode == FM_Fog) || (CBState(cb)._fogMode == FM_FogAlpha))
  {
    SetVertexShaderConstantFFAR(cb);
  }

  // InitLights HLSL function uses the following constants
  SetVertexShaderConstantSAFR(cb,CBState(cb)._matPower, CBState(cb)._matAmbient);

  // Set resolution and FOV parameters
  Camera *cam = GScene->GetCamera();
  float resFov[4] = {_wRT,_hRT,cam->InvLeft(),cam->InvTop()};
  SetVertexShaderConstantF(cb,VSC_ResolX_ResolY_FovX_FovY, resFov, 1);
}

void EngineDD11::SetupVSConstantsWater(int cb)
{
  RendState &cbState = CBState(cb);
  // AE
  {
    Color newA_E = cbState._matEmmisive * GetAccomodateEye();

    // Add ambient color
    newA_E = newA_E + cbState._lightAmbient * cbState._matAmbient;

    // Set the diffuse back coefficient
    float backCoef = cbState._matForcedDiffuse.A();
    newA_E.SetA(backCoef);
    SetVertexShaderConstantF(cb,VSC_AE, (float*)&newA_E, 1);
  }

  // Fog constants
  SetVertexShaderConstantFFAR(cb);

  // InitLights HLSL function uses the following constants
  SetVertexShaderConstantSAFR(cb,cbState._matPower, cbState._matAmbient);

  // Set the LWS matrix
  SetVertexShaderConstantLWSM(cb);

  SetVertexShaderConstantF(cb,VSC_WaterDepth, (const float *)cbState._waves.Data(), cbState._waves.Size());

  float waveXScale,waveZScale,waveHeight;
  _sceneProps->GetSeaWavePars(waveXScale,waveZScale,waveHeight);

  const float waveHeightVal[]=
  {
    waveHeight,waveHeight,
    waveXScale*(2*H_PI)*waveHeight,waveZScale*(2*H_PI)*waveHeight
  };

#if _VBS3_LARGE_WATERSEG
  float coef = (_waterPar._waterArraySize-1) / (_waterPar._waterSegSize-1);
  const float gridVal[4]={
    coef / _waterPar._grid,
    _waterPar._waterArraySize,
    _waterPar._waterArraySize-1,
    (_waterPar._waterArraySize-1)*0.5f
  };
#else
  const float gridVal[4]={
    1/_waterPar._grid,
    _waterPar._waterSegSize,
    _waterPar._waterSegSize-1,
    (_waterPar._waterSegSize-1)*0.5f
  };
#endif
  const float peakVal[4]={
    // peak visibility is: (level-peakWaveBottom)/(peakWaveTop-peakWaveBottom)
    // absolute - we must consider seaLevel here
    1.0/(_waterPar._peakWaveTop-_waterPar._peakWaveBottom), // mul. part
    -(_waterPar._peakWaveBottom+_waterPar._seaLevel)/(_waterPar._peakWaveTop-_waterPar._peakWaveBottom), // add. part
    // shore visibility is: (depth-shoreBottom)/(shoreTop-shoreBottom)
    1.0/(_waterPar._shoreTop-_waterPar._shoreBottom), // mul. part
    -_waterPar._shoreBottom/(_waterPar._shoreTop-_waterPar._shoreBottom), // add. part
  };

  float waterBright = _waterPar._peakWaveBottom;
  float waterDark = _waterPar._peakWaveBottom*5;
  float seaLevelVal[4]={
    _waterPar._seaLevel, // neutral sea level
    // water depth calculation
    1.0/(waterBright-waterDark), // mul. part
    -(waterDark+_waterPar._seaLevel)/(waterBright-waterDark), // add. part
    0.90 // alpha of deep water
  };

  SetVertexShaderConstantF(cb,VSC_WaveHeight,      waveHeightVal, 1);
  SetVertexShaderConstantF(cb,VSC_WaveGrid,        gridVal, 1);
  SetVertexShaderConstantF(cb,VSC_WaterPeakWhite,  peakVal, 1);
  SetVertexShaderConstantF(cb,VSC_WaterSeaLevel,   seaLevelVal, 1);

  // Point and spot lights
  SetupVSConstantsPSLights(cb, true);


  //MIVA
  Camera *camera = GScene->GetCamera();
  Vector3 cameraPosition = camera->Position();

  D3DXVECTOR4 pos(
    cameraPosition.X(),
    cameraPosition.Y(),
    cameraPosition.Z(),
    Glob.time.toFloat());

  SetVertexShaderConstantF(cb, VSC_WaterPars, (const float*) &pos, 1);

  /////////////////////////////////////<<<
  /*
  SSeaWaterIn &swip = GetSeaWaterInPars();

  const Color& mainLightColor = cbState._lightDiffuse;
  const Color& skyLightColor = cbState._lightAmbient;

  // get light dir in object space
  Vector3 mainLightDirO = -TransformAndNormalizeVectorToObjectSpace( cb, cbState._direction );

  Camera *camera = GScene->GetCamera();
  Vector3 cameraPosition = camera->Position();
  Vector3 cameraObjectPosition = TransformPositionToObjectSpace( cb, cameraPosition );
  D3DXVECTOR4 cpos(cameraObjectPosition.X(), cameraObjectPosition.Y(), cameraObjectPosition.Z(), 1.0f);
  SetVertexShaderConstantF( cb, VSC_CameraPosition, (float*)&cpos, 1 );

  swip.cameraPosition[0] = cameraPosition.X();
  swip.cameraPosition[1] = cameraPosition.Y();
  swip.cameraPosition[2] = cameraPosition.Z();

  swip.mainLightDirO[0] = mainLightDirO.X();
  swip.mainLightDirO[1] = mainLightDirO.Y();
  swip.mainLightDirO[2] = mainLightDirO.Z();

  swip.skyLightColor[0] = skyLightColor.R();
  swip.skyLightColor[1] = skyLightColor.G();
  swip.skyLightColor[2] = skyLightColor.B();

  swip.mainLightColor[0] = mainLightColor.R();
  swip.mainLightColor[1] = mainLightColor.G();
  swip.mainLightColor[2] = mainLightColor.B();

  swip.waveHeight = waveHeight;


  ComputeSeaWaterPars( this, swip, cbState._matAmbient, cbState._matDiffuse, cbState._matForcedDiffuse, cbState._matEmmisive, cbState._matSpecular, cbState._matPower );

  SetVertexShaderConstantF( cb, VSC_WaterPars, (const float *)GetSeaWaterParsVS(), SEAWATER_VSPARS_SIZE );
  */
}

void EngineDD11::SetupVSConstantsShore(int cb)
{
  //  RendState &cbState = CBState(cb);
  // AE
  {
    Color newA_E = CBState(cb)._matEmmisive * GetAccomodateEye();

    // Add ambient color
    newA_E = newA_E + CBState(cb)._lightAmbient * CBState(cb)._matAmbient;

    // Set the diffuse back coefficient
    float backCoef = CBState(cb)._matForcedDiffuse.A();
    newA_E.SetA(backCoef);
    SetVertexShaderConstantF(cb,VSC_AE, (float*)&newA_E, 1);
  }

  // Terrain LOD
  SetupVSConstantsLODPars(cb);

  // Fog constants
  SetVertexShaderConstantFFAR(cb);

  // InitLights HLSL function uses the following constants
  SetVertexShaderConstantSAFR(cb,CBState(cb)._matPower, CBState(cb)._matAmbient);

  // Set the LWS matrix
  SetVertexShaderConstantLWSM(cb);

  float lodPars[4];

  // Wave period
  const float ShoreWaveXDuration = 8000;
  lodPars[0] = Glob.time.ModMs(ShoreWaveXDuration) * (1.0f / ShoreWaveXDuration) * 2.0f * H_PI;

  // Unused
  lodPars[1] = lodPars[2] = 0.0f;

  // Sea level
  lodPars[3] = _sceneProps->GetSeaLevel();

  // Period
  SetVertexShaderConstantF(cb,VSC_Period_X_X_SeaLevel, lodPars, 1);

  // Point and spot lights
  SetupVSConstantsPSLights(cb);

  //MIVA
  Camera *camera = GScene->GetCamera();
  Vector3 cameraPosition = camera->Position();

  D3DXVECTOR4 pos(
    cameraPosition.X(),
    cameraPosition.Y(),
    cameraPosition.Z(),
    Glob.time.toFloat());

  SetVertexShaderConstantF(cb, VSC_WaterPars, (const float*) &pos, 1);

#if _VBS3_CPU_LAND_LIGHT
  SetupVSConstantsLandLights(cb);
#endif

  /////////////////////////////////////<<<
  /*  float waveXScale,waveZScale,waveHeight;
  _sceneProps->GetSeaWavePars(waveXScale,waveZScale,waveHeight);

  Camera *camera = GScene->GetCamera();
  Vector3 cameraPosition = camera->Position();
  Vector3 cameraObjectPosition = TransformPositionToObjectSpace( cb, cameraPosition );
  D3DXVECTOR4 cpos(cameraObjectPosition.X(), cameraObjectPosition.Y(), cameraObjectPosition.Z(), 1.0f);
  SetVertexShaderConstantF( cb, VSC_CameraPosition, (float*)&cpos, 1 );

  SSeaWaterParamsVS &swvsp = GetSeaWaterParsVSRef();
  swvsp.cameraPosition[0] = cameraPosition.X();
  swvsp.cameraPosition[1] = cameraPosition.Z();

  SetVertexShaderConstantF( cb, VSC_WaterPars, (const float *)GetSeaWaterParsVS(), SEAWATER_VSPARS_SIZE );*/

  /*SSeaWaterIn &swip = GetSeaWaterInPars();
  Camera *camera = GScene->GetCamera();
  Vector3 cameraPosition = camera->Position();
  Vector3 cameraObjectPosition = TransformPositionToObjectSpace( cb, cameraPosition );
  D3DXVECTOR4 cpos(cameraObjectPosition.X(), cameraObjectPosition.Y(), cameraObjectPosition.Z(), 1.0f);
  SetVertexShaderConstantF( cb, VSC_CameraPosition, (float*)&cpos, 1 );
  swip.cameraPosition[0] = cameraPosition.X();
  swip.cameraPosition[1] = cameraPosition.Z();
  ComputeSeaWaterPars( this, swip, cbState._matAmbient, cbState._matDiffuse, cbState._matForcedDiffuse, cbState._matEmmisive, cbState._matSpecular, cbState._matPower );
  SetVertexShaderConstantF( cb, VSC_WaterPars, (const float *)GetSeaWaterParsVS(), SEAWATER_VSPARS_SIZE );*/

  /*  SSeaWaterIn &swip = GetSeaWaterInPars();

  const Color& mainLightColor = cbState._lightDiffuse;
  const Color& skyLightColor = cbState._lightAmbient;

  // get light dir in object space
  Vector3 mainLightDirO = -TransformAndNormalizeVectorToObjectSpace( cb, cbState._direction );

  Camera *camera = GScene->GetCamera();
  Vector3 cameraPosition = camera->Position();
  Vector3 cameraObjectPosition = TransformPositionToObjectSpace( cb, cameraPosition );
  D3DXVECTOR4 cpos(cameraObjectPosition.X(), cameraObjectPosition.Y(), cameraObjectPosition.Z(), 1.0f);
  SetVertexShaderConstantF( cb, VSC_CameraPosition, (float*)&cpos, 1 );

  swip.cameraPosition[0] = cameraPosition.X();
  swip.cameraPosition[1] = cameraPosition.Y();
  swip.cameraPosition[2] = cameraPosition.Z();

  swip.mainLightDirO[0] = mainLightDirO.X();
  swip.mainLightDirO[1] = mainLightDirO.Y();
  swip.mainLightDirO[2] = mainLightDirO.Z();

  swip.skyLightColor[0] = skyLightColor.R();
  swip.skyLightColor[1] = skyLightColor.G();
  swip.skyLightColor[2] = skyLightColor.B();

  swip.mainLightColor[0] = mainLightColor.R();
  swip.mainLightColor[1] = mainLightColor.G();
  swip.mainLightColor[2] = mainLightColor.B();

  swip.waveHeight = waveHeight;

  ComputeSeaWaterPars( this, swip, cbState._matAmbient, cbState._matDiffuse, cbState._matForcedDiffuse, cbState._matEmmisive, cbState._matSpecular, cbState._matPower );

  SetVertexShaderConstantF( cb, VSC_WaterPars, (const float *)GetSeaWaterParsVS(), SEAWATER_VSPARS_SIZE );
  */
}

void EngineDD11::SetupVSConstantsTerrain(int cb)
{
  // Terrain LOD
  SetupVSConstantsLODPars(cb);

  // Fog constants
  SetVertexShaderConstantFFAR(cb);

  // InitLights HLSL function uses the following constants
  SetVertexShaderConstantSAFR(cb,CBState(cb)._matPower, CBState(cb)._matAmbient);

  // Point and spot lights
  SetupVSConstantsPSLights(cb,true);

#if _VBS3_CPU_LAND_LIGHT
  SetupVSConstantsLandLights(cb);
#endif
}

#if _VBS3_CPU_LAND_LIGHT
void EngineDD11::SetupVSConstantsLandLights(int cb)
{
  float staticModulate[4];
  float modulate = GetHDRFactor() * GScene->MainLight()->NightEffect();
  Color accom = GEngine->GetAccomodateEye();
  staticModulate[0] = accom.R() * modulate;
  staticModulate[1] = accom.G() * modulate;
  staticModulate[2] = accom.B() * modulate;
  staticModulate[3] = 0.0f;

  SetVertexShaderConstantF(cb, VSC_TerrainStaticModulate_X, (const float*) &staticModulate, 1);
}
#endif //_VBS3_CPU_LAND_LIGHT

void EngineDD11::SetupVSConstantsTerrainGrass(int cb)
{
  // Terrain LOD
  SetupVSConstantsLODPars(cb);

  // Grass alpha
  float grassPars[8];
  _sceneProps->GetGrassPars(grassPars);
  SetVertexShaderConstantF(cb,VSC_TerrainAlphaAdd, grassPars, 2);

  // InitLights HLSL function uses the following constants
  SetVertexShaderConstantSAFR(cb,CBState(cb)._matPower, CBState(cb)._matAmbient);

  // Fog constants
  SetVertexShaderConstantFFAR(cb);

  // Point and spot lights
  SetupVSConstantsPSLights(cb);

#if _VBS3_CPU_LAND_LIGHT
  SetupVSConstantsLandLights(cb);
#endif
}

void EngineDD11::SetupVSConstantsUnderwaterOcclusion(int cb)
{
  // max view distance
  float maxViewDistance = Glob.config.horizontZ;
  Assert(maxViewDistance > 0);
  if (maxViewDistance <= 0)
    maxViewDistance = 100.0f; 

  /// height (in world space)
  float blendHeight = _waterPar._seaLevel;

  //float coef = (_underwaterPars._underwaterDepth > _underwaterPars._waterDepthFogStart) ? 1.0f : 0.0f;
  float coef = 1.0f;

  // pars for shader
  float pars[4]={coef, blendHeight, blendHeight , blendHeight};
  SetVertexShaderConstantF(cb,VSC_UnderwaterOcclusionPars, pars, 1);
}


#ifdef _XBOX
void YieldProcessorNiceFunc() {YieldProcessorNice();};
#else
#define YieldProcessorNiceFunc() YieldProcessorNice()
#endif

template <class WorkerThreadClass>
void EngineDD11::WaitUntilSpaceAvailable(WorkerThreadClass &bg, int nResults)
{

  // spincount = 1000 corresponds approx. to 0.1 ms on 2.4 GHz Intel Core CPU
  int spinCount = 1000;
#ifndef _XBOX
  int countToIdle = GetCPUCount()==1 ? -20000 : -200000 ; // cca 1 ms : cca 10 ms
#if _ENABLE_REPORT
  //DWORD time = GlobalTickCount();
#endif
#endif

#if _ENABLE_CHEATS
  int used = bg._results.TotalSize()-bg._results.EmptySpace(CanDestroyResults);
  if (bg._resultsMaxUsed<used) bg._resultsMaxUsed = used; 
#endif

  while (bg._results.EmptySpace(DestroyResults)<nResults)
  {
    if (!bg.ProcessBackground())
    {
#ifndef _XBOX // no reason to do this on Xbox, nobody can use CPUs without us knowing
      if (--spinCount<=0)
      {
        // when spinning too long with no result, lower priority
        // this should help when no CPU is ready
        // priority of the background thread is Below normal, we want to wait for it
        // once it finishes its work, it will wait using WaitForMultipleObjects
        if (spinCount==0)
        {
          SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_LOWEST);
        }
        else if (spinCount==countToIdle)
        {
          // done to workaround nVidia priority inversion driver bug (IDLE thread holding RtlHeap critical section)
          //LogF("Background thread posible deadlock detected (%d ms), going IDLE to recover",GlobalTickCount()-time);
          SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_IDLE);
        }
      }
#endif
      // prevent possible deadlock: when a lot of data was submitted,
      // and _bgDataPendingNeeded set instead of _bgDataPending, we may wait forever
      bg.ProcessLazyDataPending();
      YieldProcessorNiceFunc();
      bg.DoWorkWhileWaiting();
    }
  }
  if (spinCount<=0)
  {
    // if we have changed the priority, go back to normal
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_NORMAL);
  }

}

template <class WorkerThreadClass>
bool EngineDD11::CheckThreadMemory(WorkerThreadClass &bg) const
{
  // compute how much is left is to process
  int sumMemToClear = 0;
  // we can call this - we are the consumer thread for DestroyResults
  int toDestroyAndProcess = bg._results.CountPending(DestroyResults);
  int getFrom = bg._results.CheckPointer(DestroyResults);
  while (--toDestroyAndProcess>=0)
  {
    const typename WorkerThreadClass::CBType &res = bg._results.Check(getFrom++);
    sumMemToClear += res._memToClear;
  }
  int allocated = bg._resultsMem.Allocated();
  return allocated==sumMemToClear;
}

template <class WorkerThreadClass>
void EngineDD11::FlushWaitUntilDone(WorkerThreadClass &bg, bool flushMem)
{
  if (CheckAsync<WorkerThreadClass>())
  {
    WaitUntilSpaceAvailable(bg,bg._results.TotalSize());
    Assert(CheckThreadMemory(bg));
    ReportDone<WorkerThreadClass>();
  }

  if (flushMem)
  {
    DoAssert(bg._resultsMem.Allocated()==0);
    int adjust = bg._resultsMem.Compact();
#if LOG_RESULTS_ALLOC
    LogF("*** FLUSH");
#endif
    //if (adjust) {LogF("--- Flushed---- %d",-adjust);}
    // we may need to adjust the tracking variable
    bg._resultsMemUsedStart += adjust;
#if _ENABLE_CHEATS
    bg._resultsMaxUsedHistory.AddValue(bg._resultsMaxUsed);
    bg._resultsMaxUsed = 0;
#endif
    if (Glob.config._renderThread!=Config::ThreadingReal)
    {
      // to be safe we call only when emulating, as otherwise it is hard to be sure BGThread is waiting
      bg._results.Compact();
    }
  }

}

template <class WorkerThreadClass>
void EngineDD11::FlushBackground(WorkerThreadClass &bg, bool flushMem, const char *pixScope, bool waitUntilDone)
{
  if (CheckAsync<WorkerThreadClass>())
  {
    // in a typical scenario the cleanup was already performed by PreparePostprocess
    // thread termination will perform the cleanup on its own
    //UnsetAllDiscardableResources();

    bg.ProcessLazyDataPending();

    SubmitBackground<WorkerThreadClass>(bg,flushMem,true,CleanUpNeeded,pixScope);

    Assert(CheckThreadMemory(bg));

    // we can be sure all data can be destroyed now
    bg._results.AdvanceToWrite(CanDestroyResults);

    // wait until all data are processed
    if (waitUntilDone)
    {
      FlushWaitUntilDone(bg,flushMem);
    }
  }
  else
  {
    // we need to free each memory - in case someone was using the dynamic fall-back
    while (bg._results.EmptySpace(DestroyResults)<bg._results.TotalSize())
    {
      // verify there is still something to destroy
      Assert(bg._results.HowMuchBehind(DestroyResults,ProcessResults)>0);
      // no need to call Destruct of the servant, this was already done by Execute in this case
      // however we still need to free the memory
      typename WorkerThreadClass::CBType &res = bg._results.Get(DestroyResults);
      Assert(res.IsReadyToDestroy());
#if LOG_RESULTS_ALLOC
      res.LogResultsDealloc();
#endif
      res.Clear(bg._resultsMem);
      // advance
      bg._results.Advance(DestroyResults);
    }
    // when not running in background, we always free all memory we have used, as we are sure we have consumed it
    bg._resultsMem.Clear(bg._resultsMem.Allocated());
    // no memory should be left allocated after previous Clear
    Assert(bg._resultsMem.Allocated()==0);
    bg._resultsMem.Compact();
    Assert(bg._resultsMem.UsedPtr()==0);

    FlushWaitUntilDone(bg,flushMem);
  }

  WorkerThreadClass::CBType::OnFlush();

}

template <class WorkerThreadClass>
bool EngineDD11::SubmitBackground(WorkerThreadClass &bg, bool flushMem, bool setEvent, CleanUpRequest needCleanUp, const char *pixScope)
{
  bool somethingSubmitted = false;
  Assert(CheckAsync<WorkerThreadClass>());

  typename WorkerThreadClass::BGDataType &bgData = GetBackgroundData<WorkerThreadClass>();
  int dtaSize = 0;
  const void *dta = bgData.GetData(dtaSize);

  // if there is some command, or if we are requested to flush and there are some data pending, we need to submit
  if (dtaSize>0 || LOG_RENDER_API>=2 /*|| needCleanUp>=CleanUpForced*/ || flushMem && bg._resultsMem.UsedPtr()-bg._resultsMemUsedStart>0)
  {
    /* // WIP: BGResLoad: check if this is needed, or specialize
    if (needCleanUp>CleanUpNone)
    {
    // when submitting any data, make sure we terminate them with a cleanup
    CB(-1).BGResourceCleanUp();
    if (_backgroundD3D==RecLowLevel) UnsetAllDiscardableResourcesFromCBState();
    }
    */

    PrefetchT0Off(dta, 0);
    // high level instructions are never predicated
    bool notPredicating = CanDestroyImmediately<WorkerThreadClass>();
    if (notPredicating)
    {
      // make sure we are not waiting for a space which will never be reclaimed
      bg._results.AdvanceToWrite(CanDestroyResults);
    }
    WaitUntilSpaceAvailable(bg,1);

    typename WorkerThreadClass::CBType &res = *bg._results.PutBegin(0);
    res.Resize(dtaSize,bg._resultsMem);
#if _XMEMCPY //_XBOX
    XMemCpy(res.Data(), dta, dtaSize);
#else
    memcpy(res.Data(),dta,dtaSize);
#endif

    // compute the memory after we have allocated this item
    if (flushMem)
    {
      // report how much of the _resultsMem did we use
      // this can be done only in defined points (when we know we are not inside of a CBScope)
      // note: this should track any memory allocated by the fallback method as well, as that adds into _used as well
      int used = bg._resultsMem.UsedPtr();
      res._memToClear = used-bg._resultsMemUsedStart;
      // and mark the memory used as already reported
      bg._resultsMemUsedStart = used;
      DoAssert(used==bg._resultsMem.UsedPtr());
      //LogF("_resultsMem %s - new start at %d, diff %d",pixScope,_resultsMemUsedStart,res._memToClear);
    }
    else
    {
      res._memToClear = 0;
    }

    res.FinishSubmit(this,bgData,pixScope);

#if LOG_RESULTS_ALLOC
    res.LogResultsAlloc(pixScope,dtaSize);
#endif
    // report the data are there
    bg._results.PutEnd(1);
    if (notPredicating)
    {
      bg._results.AdvanceToWrite(CanDestroyResults);
    }
    // when not setting event, mark it for setting later - wait could deadlock if this is not done
    bg.SetDataPending(setEvent);

    somethingSubmitted = true;
  }

  // free to clear now - we have copied the memory
  bgData.Reset();

  return somethingSubmitted;
}

void EngineDD11::SetTextureAndMaterial( int cb, const TextureD3D11 *tex, const TexMaterial *mat, int specFlags, int filterFlags )
{
  PROFILE_DX_SCOPE_DETAIL(stpTexNMat);

  if (!tex && mat)
  {
    tex = static_cast<TextureD3D11*>(mat->_stage[0].GetTextureForDraw());
  }

  SetTexture0(cb,tex);

  // TI addressing taken from the spec flags at the beginning (it might be rewritten later by the material)
  // Same code used here: ADDRESSSTAGE0
  bool clampU=false,clampV=false;
  if( specFlags&ClampU ) clampU=true;
  if( specFlags&ClampV ) clampV=true;
  D3DTEXTUREADDRESS_OLD tiAddressU=( clampU ? D3DTADDRESS_CLAMP_OLD : D3DTADDRESS_WRAP_OLD );
  D3DTEXTUREADDRESS_OLD tiAddressV=( clampV ? D3DTADDRESS_CLAMP_OLD : D3DTADDRESS_WRAP_OLD );

  // TI filter and level of anisotropy taken from the mate
  // Same values used here: FILTERANDLEVEL
  TexFilter tiFilter = TFAnizotropic;
  int tiMaxAFLevel = AnisotropyLevel(PSNormal, false, TFAnizotropic4);

  if (!mat)
  {
    // no material - assume low anisotropy is enough here
    // Same values used here: FILTERANDLEVEL
    SetFiltering(cb, 0,TFAnizotropic,AnisotropyLevel(PSNormal, false, TFAnizotropic4));
    SetMultiTexturing(cb, NULL, 0);
  }
  else
  {
    mat->Load();
    mat->AutodetectAnisotropy();
    Assert(UsableSampleStages<=TexMaterial::NStages);
    Assert(MaxStages<=TexMaterial::NTexGens);
    TextureD3D11 *secTex[UsableSampleStages-1];
    //int nStages = UsableSampleStages-1;
    int nStages = mat->_stageCount;
    if (nStages>lenof(secTex))
    {
      nStages = lenof(secTex);
      ErrF("Material %s stage count overflow (%d)",cc_cast(mat->GetName()),nStages);
    }
    for (int i = 0; i < nStages; i++)
    {
      Texture *tex = mat->_stage[i+1].GetTextureForDraw();
      secTex[i] = static_cast<TextureD3D11*>(tex);
      if (tex)
      {
        int clamp = tex->GetClamp();
        D3DTEXTUREADDRESS_OLD addressU=( clamp&TexClampU ? D3DTADDRESS_CLAMP_OLD : D3DTADDRESS_WRAP_OLD );    
        D3DTEXTUREADDRESS_OLD addressV=( clamp&TexClampV ? D3DTADDRESS_CLAMP_OLD : D3DTADDRESS_WRAP_OLD );

        D3DSetSamplerState(cb, i+1,D3DSAMP_ADDRESSU_OLD,addressU);
        D3DSetSamplerState(cb, i+1,D3DSAMP_ADDRESSV_OLD,addressV);
        D3DSetSamplerState(cb, i+1,D3DSAMP_ADDRESSW_OLD,D3DTADDRESS_CLAMP_OLD);

        // Get the TI mate addressing parameters
        if (TITexSource[mat->GetPixelShaderID()] == (i+1))
        {
          tiAddressU = addressU;
          tiAddressV = addressV;
        }
      }
    }

    // both FilterTrilinear and FilterAnizotrop are handled as anisotropy enabled
    TexFilter filterWanted = TFAnizotropic;
    if (filterFlags)
    {
      if (filterFlags==FilterPoint) filterWanted = TFPoint;
      else if (filterFlags==FilterLinear) filterWanted = TFLinear;
    }

    for (int i=0; i<nStages+1; i++)
    {
      // set max. anisotropy level to enable / disable anisotropy on given stage
      TexFilter filter = mat->_stage[i]._filter;
      if (filterWanted<TFAnizotropic && filter>filterWanted) filter = filterWanted;

      // Determine level of anisotropic filter
      int maxAFLevel = filter<TFAnizotropic ? 1 : AnisotropyLevel(mat->GetPixelShaderID(), mat->GetRenderFlag(RFRoad), filter);

      // Set the texture filter
      SetFiltering(cb, i, filter, maxAFLevel);

      // Get the TI mate filtering parameters
      if (TITexSource[mat->GetPixelShaderID()] == i)
      {
        tiFilter = filter;
        tiMaxAFLevel = maxAFLevel;
      }
    }

    // Calculate keepUntouchedLimit upon PS
    int keepUntouchedLimit;
    if (mat->GetPixelShaderID() == PSNonTLFlare)
    {
      keepUntouchedLimit = 12;
    }
    else
    {
      keepUntouchedLimit = TEXID_FIRST;
    }

    SetMultiTexturing(cb, secTex, nStages, keepUntouchedLimit);
  }

  // Check the pixel shader type, if thermal them set the TI texture
  if (CBState(cb)._pixelShaderTypeSel == PSTThermal)
  {
    // Use the mate's addressing, regardless on the TI texture preferences
    D3DSetSamplerState(cb, TEXID_TI, D3DSAMP_ADDRESSU_OLD, tiAddressU);
    D3DSetSamplerState(cb, TEXID_TI, D3DSAMP_ADDRESSV_OLD, tiAddressV);
    D3DSetSamplerState(cb, TEXID_TI,D3DSAMP_ADDRESSW_OLD,D3DTADDRESS_CLAMP_OLD);

    // Use the mate's filtering
    SetFiltering(cb, TEXID_TI, tiFilter, tiMaxAFLevel);

    // Use the SRGB, if possible
    if (_caps._canReadSRGB)
    {
      D3DSetSamplerState(cb, TEXID_TI, D3DSAMP_SRGBTEXTURE_OLD, _linearSpace);
    }

    // If TI texture explicitly set then use it, else use the white texture
    TextureD3D11 *textureTI;
    if (mat)
    {
      if (mat->_stageTI._tex.NotNull())
      {
        textureTI = static_cast<TextureD3D11*>(mat->_stageTI._tex.GetRef());
      }
      else
      {
        textureTI = _textBank->GetWhiteTexture();
      }
    }
    else
    {
      textureTI = _textBank->GetWhiteTexture();
    }

    // Set the TI texture
    DoAssert(textureTI);
    SetTextureR(cb, TEXID_TI, textureTI);
  }
}

/*!
\patch 5163 Date 06/04/2007 by Ondra
- Fixed: Bridges roads had different anisotropic filtering level than usual roads
*/
inline int EngineDD11::AnisotropyLevel(PixelShaderID ps, bool road, TexFilter filter) const
{
  // based on filter and current settings we derive the level
  if (filter<TFAnizotropic) return 1;
  if (_afWanted<=0) return 1;

  int level = 2;
  if (filter==TFAnizotropic4) level = 4;
  else if (filter==TFAnizotropic8) level = 8;
  else if (filter==TFAnizotropic16) level = 16;

  // offset means for some shaders we can reduce the anisotropy
  // because it is not that important for given shaders

  // for some pixel shaders we want little or no offset
  // compute offset based on current preference
  int offset = 4-_afWanted;
  if (road || (ps == PSRoad))
  {
    offset -= 2;
  }
  else
  {
    if ((ps>=PSTerrain1 && ps<=PSTerrain15) || ps==PSTerrainX || ps == PSTerrainSNX)
    {
      offset -= 1;
    }
    else if (ps>=PSTerrainSimple1 && ps<=PSTerrainSimple15 || ps==PSTerrainSimpleX)
    {
      offset -= 1;
    }
    else if (ps>=PSTerrainGrass1 && ps<=PSTerrainGrass15 || ps==PSTerrainGrassX)
    {
      offset -= 1;
    }
  }
  if (offset<0) offset =0;
  level >>= offset;
  if (level<2) return 1;
  if (level>_maxAFLevel) return _maxAFLevel;
  return level;
}

void EngineDD11::D3DEnableAlphaToCoverage(int cb, bool value, bool optimize)
{
  //D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE_OLD, value);
  D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE_OLD, FALSE);
  // we use D3DRS_POINTSIZE to signal the DX11 that we want alpha to coverage to be set in blend state
  D3DSetRenderState(cb, D3DRS_POINTSIZE_OLD, value ? 1 : 0,optimize);
}

void EngineDD11::SetTextureSBR(int cb, bool alphaPrimitives)
{
  Assert(_renderingMode!=RMShadowBuffer); // should be catched earlier
  Assert(_renderingMode!=RMCrater); // crater RM should be only temporary inside of shader selection functions
  RendState &cbState = CBState(cb);
  if (cbState._tlActive!=EngineDD11::TLEnabled)  // when no 3D, no shadow maps used
  {
    SetTextureDirect(cb,TEXID_SHADOWMAP, NULL, false);
    return;
  }
  // Determine whether we want to use the SSSM technique, or just the SB
  cbState._doSSSM = cbState._zPrimingDone && !alphaPrimitives;

  // predication for RMCommon should already be done on a higher level  
  if (cbState._doSSSM)
  {
    // Set the SSSM as shadow map
    UseShadowMapSSSM(cb);
  }
  else
  {
    // Set the SB as shadow map
    UseShadowMapSB(cb);
  }
}

void EngineDD11::UseShadowMapSSSM(int cb)
{
  // Finish in case of thermal vision (as this function would overwrite the TC table which is bound with TEXID_SHADOWMAP)
  if (_thermalVision) return;

  // Set the SSSM as shadow map
#ifdef SSSMBLUR
  SetTexture(cb,TEXID_SHADOWMAP, _sssmBRenderTarget._srView);
#else
  SetTexture(cb,TEXID_SHADOWMAP, _sssmRenderTarget._srView);
#endif

  // We want to use the point sampling
  D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
  D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
  D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);
  D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
}

void EngineDD11::UseShadowMapSB(int cb)
{
  // Finish in case of thermal vision (as this function would overwrite the TC table which is bound with TEXID_SHADOWMAP)
  if (_thermalVision) return;

  if (_renderingMode==RMShadowBuffer)
  {
    LogF("Casting SB while rendering SB?");
    return;
  }
  // Set the shadow map matrix
  SetVertexShaderConstantF(cb,VSC_ShadowmapMatrix, (float*)&_shadowMapMatrixDX, 3);

  // Set the SB map
  SetTexture(cb,TEXID_SHADOWMAP, _depthStencilSB._srView);

  D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
  D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
  D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);
  D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
}

void EngineDD11::D3DSetRenderState(int cb, D3DRENDERSTATETYPE_OLD state, DWORD value, bool optimize)
{
  PROFILE_DX_SCOPE_DETAIL(setRS);

#ifdef _XBOX
  // TODOX360: eliminate states which do not exists
  if (state>=D3DRS_FORCE_DWORD || state<0) return;
  // on Xbox 360:
  Assert(state>=D3DRS_ZENABLE); // D3DRS_ZENABLE is the lowest value of the enum (40)
  Assert(state%4==0); // all render states are a multiply of 4
  int index = (state-D3DRS_ZENABLE)>>2;
#else
  int index = state;
#endif

  // DX11 TODO DIAG
  //if (CHECK_ENG_DIAG(EDENoFilterRS)) optimize = false;
  RendStateExt &cbState = CBState(cb);
  // assume small values of state (in DX6 max. state was about 40)
  // TODO: reverse array order: array of structures instead of structure of arrays
  for (int p=0; p<NPredicationModes; p++)
  {
    // make sure all uninitialized values are marked with a nonsense value
    int oldSize = cbState._renderState[p].Size();
    cbState._renderState[p].Access(index);
    int newSize = cbState._renderState[p].Size();
    for (int s=oldSize; s<newSize; s++)
    {
      cbState._renderState[p][s] = -1;
    }
  }
  // CheckRSNeedsToBeSet will also set values if needed
  if (!optimize || CheckRSNeedsToBeSet(cbState, index, value, cbState._rec._currPredication))
  {
    CALL_CB_ENGINE(cb,SetRenderState,state,value);
  }

  // Check if state is handled
  switch (state)
  {
    case( D3DRS_ZFUNC_OLD) :
    case( D3DRS_FILLMODE_OLD) :
    case( D3DRS_ZWRITEENABLE_OLD) :
    case( D3DRS_ALPHABLENDENABLE_OLD) :
    case( D3DRS_SRCBLEND_OLD) :
    case( D3DRS_DESTBLEND_OLD) :
    case( D3DRS_ZENABLE_OLD) :
    case( D3DRS_CULLMODE_OLD) :
    case( D3DRS_CLIPPING_OLD) : // DX11 TODO
    case( D3DRS_ALPHATESTENABLE_OLD) : // DX11 TODO
    case( D3DRS_ALPHAFUNC_OLD) : // DX11 TODO
    case( D3DRS_ALPHAREF_OLD) : // DX11 TODO
    case( D3DRS_BLENDOP_OLD) :
    case( D3DRS_SRGBWRITEENABLE_OLD) : // Direct3D 10 NEEDS UPDATE 
    case( D3DRS_COLORWRITEENABLE_OLD) :
    case( D3DRS_STENCILENABLE_OLD) :
    case( D3DRS_STENCILREF_OLD) :
    case( D3DRS_STENCILMASK_OLD) :
    case( D3DRS_STENCILWRITEMASK_OLD) :
    case( D3DRS_STENCILFUNC_OLD) :
    case( D3DRS_STENCILPASS_OLD) :
    case( D3DRS_CCW_STENCILFUNC_OLD) :
    case( D3DRS_CCW_STENCILPASS_OLD) :
    case( D3DRS_TWOSIDEDSTENCILMODE_OLD) :
    case( D3DRS_LIGHTING_OLD) :  //DX11 deprecated
    case( D3DRS_SHADEMODE_OLD) :  //DX11 deprecated
    case( D3DRS_DITHERENABLE_OLD) :  //DX11 deprecated
    case( D3DRS_DEPTHBIAS_OLD) :
    case( D3DRS_SLOPESCALEDEPTHBIAS_OLD) :
    case (D3DRS_POINTSIZE_OLD): //used for alpha to coverage 
    case (D3DRS_SEPARATEALPHABLENDENABLE_OLD): 
    case (D3DRS_SRCBLENDALPHA_OLD): 
    case (D3DRS_DESTBLENDALPHA_OLD): 
    case (D3DRS_BLENDOPALPHA_OLD): 
      break;
    default:
      Fail("DX11 TODO");
  }

}

void EngineDD11::D3DSetSamplerState(int cb, DWORD stage, D3DSAMPLERSTATETYPE_OLD state, DWORD value, bool optimize)
{
#ifdef _XBOX
  // TODOX360: eliminate states which do not exists
  if (state>=D3DSAMP_FORCE_DWORD || state<0) return;
  // on Xbox 360:
  Assert(state%4==0); // all sampler states are a multiply of 4
  int index = (state>>2);
#else
  int index = state;
#endif

#if DO_COUNTERS
  static OptimizeCounter opt("SamplerState");
#endif
  // assume small values of state (in DX6 max. state was about 25)

#if _ARMA3_PREDICATED_SAMPLER_STATES
  Assert(stage >= 0 && stage < MaxSamplerStages);
  if (stage < 0 || stage >= MaxSamplerStages)
    return;

  RendStateExt &cbState = CBState(cb);
  for (int p=0; p<NPredicationModes; p++)
  {
    // make sure all uninitialized values are marked with a nonsense value
    int oldSize = cbState._samplerState[p][stage].Size();
    cbState._samplerState[p][stage].Access(index);
    int newSize = cbState._samplerState[p][stage].Size();
    for (int s=oldSize; s<newSize; s++)
    {
      cbState._samplerState[p][stage][s] = -1;
    }
  }

  if(optimize && !CheckSSNeedsToBeSet(cbState, stage, index, value, cbState._rec._currPredication))
  {
#if DO_COUNTERS
    opt.Skip();
#endif
    return;
  }
  else
  {
#if DO_COUNTERS
    opt.Perform();
#endif
  }

#else
  AutoArray<SamplerStateInfo> &samplerState=CBState(cb)._samplerState[stage];
  samplerState.Access(index);
  SamplerStateInfo &info=samplerState[index];

  // DX11 TODO DIAG
  if( info==value && optimize /* && !CHECK_ENG_DIAG(EDENoFilterRS)*/)
  {
#if DO_COUNTERS
    opt.Skip();
#endif
    return;
  }

  info = value;
#endif
#if DO_COUNTERS
  opt.Perform();
#endif
#if _ENABLE_PERFLOG && DO_TEX_STATS
  if (LogStatesOnce)
  {
#if RS_DIAGS
    LogF("SetSamplerState %d,%s,%d",stage,name,value);
#else
    LogF("SetSamplerState %d,%d,%d",stage,state,value);
#endif
  }
#endif
  PROFILE_DX_SCOPE_DETAIL(3dsS);

  CALL_CB_ENGINE(cb, SetSamplerState, stage, state, value);

  // Check if state is handled
  switch(state)
  {
    case( D3DSAMP_MINFILTER_OLD) :
    case( D3DSAMP_MAGFILTER_OLD) :
    case( D3DSAMP_MIPFILTER_OLD) :
    case( D3DSAMP_MAXANISOTROPY_OLD) :
    case( D3DSAMP_ADDRESSU_OLD) :
    case( D3DSAMP_ADDRESSV_OLD) :
    case( D3DSAMP_ADDRESSW_OLD) :
    case( D3DSAMP_SRGBTEXTURE_OLD) :
    case( D3DSAMP_BORDERCOLOR_OLD) :
      break;
    default:
      Fail("DX11 TODO");
      break;
  }
}

void EngineDD11::DoSetRenderState(DWORD state, DWORD value)
{
  AssertSpecialLevelThreadAccess();
  // make sure all uninitialized values are marked with a nonsense value
  int oldSize =  _currentRenderState._renderState.Size();
  _currentRenderState._renderState.Access(state);
  int newSize = _currentRenderState._renderState.Size();
  for (int s=oldSize; s<newSize; s++)
  {
    _currentRenderState._renderState[s] = -1;
  }
  _currentRenderState._renderState[state] = value;
}

void EngineDD11::DoSetSamplerState(DWORD stage, DWORD state, DWORD value)
{
  AssertSpecialLevelThreadAccess();
  Assert(stage >= 0 && stage < MaxSamplerStages);
  if (stage >= 0 && stage < MaxSamplerStages)
  {
    AutoArray<SamplerStateInfo> &samplerState=_currentRenderState._samplerState[stage];

    // make sure all uninitialized values are marked with a nonsense value
    int oldSize =  samplerState.Size();
    samplerState.Access(state);
    int newSize = samplerState.Size();
    for (int s=oldSize; s<newSize; s++)
    {
      samplerState[s] = -1;
    }

    if (samplerState[state] != value)
    {
      _currentRenderState._samplerStateChanged[stage] = true;
      samplerState[state] = value;
    }
  }
}

void EngineDD11::SetupMaterialTextures(const TexMaterialLODInfo &mat, int cb, const EngineShapeProperties & prop, int specFlags)
{
  PROFILE_DX_SCOPE_DETAIL(stpMatTex);

  const RendState &cbState = CBState(cb);
  if (mat._mat && cbState._tlActive)
  {
    PrepareDetailTex(cb, mat,prop);
  }
  else
  {
    if (specFlags&IsShadow)
    {
      // single texturing
      EnableCustomTexGenZero(cb, true);
      SelectPixelShader(cb, PSWhiteAlpha);
    }
    else
    {
      // single texturing
      PrepareSingleTexDiffuseA(cb, mat);
    }
  }
}

void EngineDD11::DoShadowsStatesSettings(int cb, EShadowState ss, bool noBackCull)
{
  RendState &cbState = CBState(cb);
  cbState._currentShadowStateSettings = ss;
  cbState._backCullDisabled = noBackCull;

  switch(ss)
  {
  case SS_Transparent:
    D3DSetRenderState(cb, D3DRS_STENCILPASS_OLD, D3DSTENCILOP_KEEP_OLD);
    D3DSetRenderState(cb, D3DRS_STENCILWRITEMASK_OLD, STENCIL_FULL);
    D3DSetRenderState(cb, D3DRS_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
    D3DSetRenderState(cb, D3DRS_CCW_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
    D3DSetRenderState(cb, D3DRS_CCW_STENCILPASS_OLD, D3DSTENCILOP_KEEP_OLD);
    D3DSetRenderState(cb, D3DRS_CULLMODE_OLD, noBackCull ? D3DCULL_NONE_OLD : D3DCULL_CCW_OLD) ;
    if (_caps._canTwoSidedStencil)
    {
      D3DSetRenderState(cb, D3DRS_TWOSIDEDSTENCILMODE_OLD, FALSE);
    }
    break;
  case SS_Receiver:
    // when drawing object on top of shadow, reset the shadow
    // when perform rendering which was z-primed, we do not need to write stencil at all
    // actually, we can test if the stencil value present is the one we are expecting it to be!
    // note: while the idea is interesting, it does not work on nVidia HW,
    // as changing stencil ref disables stencil cull
    // on ATI it does not seem to be needed, as z-equal already works fine
    /*
    if (cbState._zPrimingDone)
    {
    D3DSetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
    D3DSetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
    }
    else
    */
    {
      D3DSetRenderState(cb, D3DRS_STENCILPASS_OLD, D3DSTENCILOP_REPLACE_OLD);
      D3DSetRenderState(cb, D3DRS_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
      D3DSetRenderState(cb, D3DRS_CCW_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
      D3DSetRenderState(cb, D3DRS_CCW_STENCILPASS_OLD, D3DSTENCILOP_KEEP_OLD);
    }
    D3DSetRenderState(cb, D3DRS_STENCILWRITEMASK_OLD, STENCIL_FULL);
    D3DSetRenderState(cb, D3DRS_STENCILREF_OLD, 0);
    D3DSetRenderState(cb, D3DRS_CULLMODE_OLD, noBackCull ? D3DCULL_NONE_OLD : D3DCULL_CCW_OLD);
    if (_caps._canTwoSidedStencil)
    {
      D3DSetRenderState(cb, D3DRS_TWOSIDEDSTENCILMODE_OLD, FALSE);
    }
    break;
  case SS_Disabled:
    D3DSetRenderState(cb, D3DRS_STENCILPASS_OLD, D3DSTENCILOP_REPLACE_OLD);
    D3DSetRenderState(cb, D3DRS_STENCILWRITEMASK_OLD, STENCIL_FULL);
    D3DSetRenderState(cb, D3DRS_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
    D3DSetRenderState(cb, D3DRS_STENCILREF_OLD, 0);
    D3DSetRenderState(cb, D3DRS_CCW_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
    D3DSetRenderState(cb, D3DRS_CCW_STENCILPASS_OLD, D3DSTENCILOP_KEEP_OLD);
    D3DSetRenderState(cb, D3DRS_CULLMODE_OLD, noBackCull ? D3DCULL_NONE_OLD : D3DCULL_CCW_OLD);
    if (_caps._canTwoSidedStencil)
    {
      D3DSetRenderState(cb, D3DRS_TWOSIDEDSTENCILMODE_OLD, FALSE);
    }
    break;
  case SS_Volume_Front:
    //DoAssert(!_recording);
    D3DSetRenderState(cb, D3DRS_STENCILWRITEMASK_OLD, STENCIL_COUNTER);
    D3DSetRenderState(cb, D3DRS_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
    D3DSetRenderState(cb, D3DRS_STENCILREF_OLD, 0);
    D3DSetRenderState(cb, D3DRS_CCW_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
    D3DSetRenderState(cb, D3DRS_CCW_STENCILPASS_OLD, D3DSTENCILOP_KEEP_OLD);
    Assert (!_caps._canTwoSidedStencil);
    D3DSetRenderState(cb, D3DRS_CULLMODE_OLD, D3DCULL_CCW_OLD);
    D3DSetRenderState(cb, D3DRS_STENCILPASS_OLD, D3DSTENCILOP_DECR_OLD);
    break;
  case SS_Volume_Back:
    // this should never be used with two-sided stencil
    D3DSetRenderState(cb, D3DRS_STENCILWRITEMASK_OLD, STENCIL_COUNTER);
    D3DSetRenderState(cb, D3DRS_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
    D3DSetRenderState(cb, D3DRS_STENCILREF_OLD, 0);
    if(_caps._canTwoSidedStencil)
    {
#ifdef _XBOX
      D3DSetRenderState(cb, D3DRS_CCW_STENCILWRITEMASK, STENCIL_COUNTER);
      D3DSetRenderState(cb, D3DRS_CCW_STENCILREF, 0);
#endif
      D3DSetRenderState(cb, D3DRS_CCW_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
      D3DSetRenderState(cb, D3DRS_CULLMODE_OLD, D3DCULL_NONE_OLD);
      D3DSetRenderState(cb, D3DRS_TWOSIDEDSTENCILMODE_OLD, TRUE);

      D3DSetRenderState(cb, D3DRS_STENCILPASS_OLD, D3DSTENCILOP_INCR_OLD);
      D3DSetRenderState(cb, D3DRS_CCW_STENCILPASS_OLD, D3DSTENCILOP_DECR_OLD);
      // 
      // D3DRS_CCW_STENCILFAIL
      // D3DRS_CCW_STENCILZFAIL
    }
    else
    {
      D3DSetRenderState(cb, D3DRS_STENCILPASS_OLD, D3DSTENCILOP_INCR_OLD);
      D3DSetRenderState(cb, D3DRS_CULLMODE_OLD, D3DCULL_CW_OLD);
      D3DSetRenderState(cb, D3DRS_CCW_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
      D3DSetRenderState(cb, D3DRS_CCW_STENCILPASS_OLD, D3DSTENCILOP_KEEP_OLD);
    }

    break;
  case SS_ShadowBuffer:
    D3DSetRenderState(cb, D3DRS_STENCILPASS_OLD, D3DSTENCILOP_REPLACE_OLD);
    D3DSetRenderState(cb, D3DRS_STENCILWRITEMASK_OLD, STENCIL_FULL);
    D3DSetRenderState(cb, D3DRS_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
    D3DSetRenderState(cb, D3DRS_CULLMODE_OLD, D3DCULL_CCW_OLD);
    D3DSetRenderState(cb, D3DRS_STENCILREF_OLD, 0);
    D3DSetRenderState(cb, D3DRS_CCW_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
    D3DSetRenderState(cb, D3DRS_CCW_STENCILPASS_OLD, D3DSTENCILOP_KEEP_OLD);
    if (_caps._canTwoSidedStencil)
    {
      D3DSetRenderState(cb, D3DRS_TWOSIDEDSTENCILMODE_OLD, FALSE);
    }
    break;
  default:
    LogF("Error: Unknown shadow state");
  }
}

void EngineDD11::SetVertexShader(int cb, const VertexShaderInfo &sInfo, bool optimize)
{
  SetVertexShader(cb, &sInfo, optimize);
}

void EngineDD11::SetVertexShader(int cb, const VertexShaderInfo *sInfo, bool optimize)
{
  ID3D11VertexShader* shader = sInfo  ? sInfo->_shader.GetRef() : 0;
  ID3D11InputLayout* layout = sInfo ? sInfo->_inputLayout.GetRef() : 0;

  RendStateExt &cbState = CBState(cb);
  // check if we match in all currently active branches, if we do, we can skip
  if (!optimize || CheckVSNeedsToBeSet(shader,cbState,cbState._rec._currPredication))
  {
    CALL_CB_ENGINE(cb,SetVertexShader,shader, layout);
  }
}

void EngineDD11::DoSetVertexShader(ID3D11VertexShader* shader, ID3D11InputLayout* layout)
{
  AssertSpecialLevelThreadAccess();
  if (_currentRenderState._lastVertexShader != shader)
  {
    _d3dDeviceRec->VSSetShader(shader);
    _currentRenderState._lastVertexShader = shader;
  }
  if (_currentRenderState._lastInputLayout != layout)
  {
    _d3dDeviceRec->IASetInputLayout(layout);
    _currentRenderState._lastInputLayout = layout;
  }

}

void EngineDD11::SetTextureR(int cb, DWORD stage, const TextureD3D11 *texture)
{
  TextureHandle11* tHandle;
  if (texture)
  {
    tHandle = texture->GetHandle(_linearSpace);
    if (!tHandle)
    {
      const TextureD3D11 *defTexture = texture->GetDefaultTexture();
      tHandle = defTexture->GetHandle(_linearSpace);
    }
#if _HELISIM_RENDER_TO_TEXTURE
    else
    {
      if (texture->Type() == TT_R2T)
      {
        TrackR2TTexUsage(texture);
      }
    }
#endif
  }
  else
  {
    tHandle = NULL;
  }
  SetTexture(cb, stage, tHandle);
}

static inline float CombineAvgAndMaxColor(float ac, float mc)
{
  if (ac<=mc*1e-6) return 0;
  if (ac>=mc*10) return 10;
  float ret = ac/mc;
  Assert(_finite(ret));
  return ret;
}

/// avoid division zero by zero during color combining
static inline Color CombineAvgAndMaxColor(ColorVal avgColor, ColorVal maxColor)
{
  return Color(
    CombineAvgAndMaxColor(avgColor.R(),maxColor.R()),
    CombineAvgAndMaxColor(avgColor.G(),maxColor.G()),
    CombineAvgAndMaxColor(avgColor.B(),maxColor.B()),
    CombineAvgAndMaxColor(avgColor.A(),maxColor.A())
    );
}

void EngineDD11::SetTexture0(int cb, const TextureD3D11 *tex)
{
  if (!tex)
  {
    tex = _textBank->GetWhiteTexture();
    DoAssert(tex);
  }

  bool srgb = false;
  switch (tex->Type())
  {
    //case TT_Detail: break; // detail texture assumed linear - 0.5 is midpoint
  case TT_Diffuse: 
  case TT_Macro:
    srgb = true;
    break;
  }

  if (_caps._canReadSRGB)
  {
    D3DSetSamplerState(cb, 0, D3DSAMP_SRGBTEXTURE_OLD, srgb && _linearSpace);
  }

  Color maxColor = tex->GetMaxColor();
  const TextureD3D11 *loadedTexture;
  if (tex->GetHandle(_linearSpace))
  {
    loadedTexture = tex;
#if _HELISIM_RENDER_TO_TEXTURE
    if (tex->Type() == TT_R2T)
    {
      TrackR2TTexUsage(tex);
    }
#endif
  }
  else
  {
    maxColor = tex->GetColor();
    loadedTexture = tex->GetDefaultTexture();
  }
  SetTexture0Color(cb, maxColor, CombineAvgAndMaxColor(tex->GetColor(),maxColor),true);
  SetTexture(cb, 0, loadedTexture->GetHandle(_linearSpace));
}

static float ColorDistance2(ColorVal c1, ColorVal c2)
{
  return Square(c1.R()-c2.R())+Square(c1.G()-c2.G())+Square(c1.B()-c2.B())+Square(c1.A()-c2.A());
}

void EngineDD11::SetTexture0Color(int cb, ColorVal maxColor, ColorVal avgColor, bool optimize)
{
  RendState &cbState = CBState(cb);
  //PROFILE_DX_SCOPE(3dPsC);
  bool toLinear = _caps._canReadSRGB && _linearSpace;
  Color maxLinear = toLinear ? SRGBToLinear(maxColor) : maxColor;
  if (!optimize || ColorDistance2(cbState._texture0MaxColor,maxLinear)>=Square(0.005))
  {
    //PROFILE_DX_SCOPE(3dPiC);
    cbState._texture0MaxColor=maxLinear;
    // Some shaders (those who use more than 1 diffuse map - f.i. macro maps) needs
    // the value in pixel shader, others use it in vertex shader
    // - vertex shader constant will be set later depending on pixel shader ID
    SetPixelShaderConstantF(cb,PSC_MaxColor,maxLinear.GetArray(),1);
  }
  if (!optimize || ColorDistance2(cbState._texture0AvgColor,avgColor)>=Square(0.005))
  {
    //PROFILE_DX_SCOPE(3dPiC);
    cbState._texture0AvgColor=avgColor;
    Color linear = toLinear ? SRGBToLinear(avgColor) : maxColor;
    SetPixelShaderConstantF(cb,PSC_T0AvgColor,linear.GetArray(),1);
  }
}

void EngineDD11::SetMultiTexturing(int cb, TextureD3D11 **tex, int count, int keepUntouchedLimit)
{
  // shadow map stage must not be used here
  Assert(count<=TEXID_FIRST-1);
  //PROFILE_DX_SCOPE(3dMuT);
  // Set specified textures
  for (int i = 0; i < count; i++)
  {
    if (tex[i])
    {
      bool srgb = false;
      // Either get the texture or the default texture
      switch (tex[i]->Type())
      {
      case TT_Diffuse:
      case TT_Macro:
        srgb = true;
        break;
      }
      if (_caps._canReadSRGB)
      {
        D3DSetSamplerState(cb, i+1, D3DSAMP_SRGBTEXTURE_OLD, srgb && _linearSpace);
      }
    }

    // Set the specified texture
    SetTextureR(cb, i + 1, tex[i]);
  }

  // Set the rest to NULL
  int maxUntouchedStageCount = intMin(keepUntouchedLimit, TEXID_FIRST) - 1;
  for (int i = count; i < maxUntouchedStageCount; i++)
  {
    // Perform quick rejection
    if (CBState(cb)._lastHandle[i + 1])
    {
      SetTexture(cb, i + 1, NULL);
    }
  }
}

void EngineDD11::PrepareSingleTexDiffuseA(int cb, const TexMaterialLODInfo &mat)
{
  EnableCustomTexGenZero(cb,true);

  // Select the pixel shader
  const TexMaterial *texMat = mat._mat;
  if (texMat)
  {
    SelectPixelShader(cb,texMat->GetPixelShaderID());
  }
  else
  {
    SelectPixelShader(cb,PSNormal);
  }
}

void EngineDD11::EnableCustomTexGenZero(int cb, bool optimize)
{
  RendState &cbState = CBState(cb);
  // Set the _uvSource and _uvSourceCount. Note that this must be done prior
  // EnableCustomTexGen because it uses the result
  cbState._uvSource[0] = UVTex;
  cbState._uvSourceCount = 1;
  cbState._uvSourceOnlyTex0 = true;

  // Set the texgen matrices
  // TODO: why 3? 1 should be enough
  const PASSMATRIXTYPE *matrix[3] = {NULL,NULL,NULL};
  EnableCustomTexGen(cb, matrix, 3, optimize);
  // prevent assert
  cbState._texGenScaleOrOffsetHasChanged = false;
}

void EngineDD11::PrepareDetailTex(int cb, const TexMaterialLODInfo &mat, const EngineShapeProperties &prop)
{
  const TexMaterial *texMat = mat._mat;
  if (texMat)
  {
    texMat->Load();
    PASSMATRIXTYPE matrix[MaxStages];
    const PASSMATRIXTYPE *pMatrix[MaxStages];
    pMatrix[0] = NULL;
    pMatrix[1] = NULL;
    pMatrix[2] = NULL;
    // TODO: 1 should be enough as a default
    int nMatrices = 3;

    // if applicable, apply tex-gen for stage 0
    // note: all stages have UVTex as a source by default
    switch (texMat->_texGen[0]._uvSource )
    {
    case UVNone:
      break;
    default:
      Fail("Unsupported UV source for stage 0");
    case UVPos:
    case UVNorm:
    case UVWorldPos:
    case UVWorldNorm:
    case UVTex:
    case UVTex1:
      ConvertMatrixTransposed(matrix[0], texMat->_texGen[0]._uvTransform);
      pMatrix[0] = &matrix[0];
      break;
    }

    // Handle UV animated texture
    if (mat._UVchanged)
    {
      ConvertMatrixTransposed(matrix[0], M4Identity);
      matrix[0]._14 += mat._offsetU; 
      matrix[0]._24 += mat._offsetV; 

      // if we are overriding, strange things can happen
      pMatrix[0] = &matrix[0];
    }

    for (int stage=1; stage<texMat->_nTexGen; stage++)
    {
      if (stage >= MaxStages) break;
      //PASSMATRIXTYPE &mat = matrix[stage];
      switch (texMat->_texGen[stage]._uvSource)
      {
      case UVTexWaterAnim:
      case UVTexShoreAnim:
        if (stage==1)
        {
          // Make sure following two UV sources are Water or Shore as well
          DoAssert((texMat->_texGen[stage + 1]._uvSource == UVTexWaterAnim) || (texMat->_texGen[stage + 1]._uvSource == UVTexShoreAnim));
          DoAssert((texMat->_texGen[stage + 2]._uvSource == UVTexWaterAnim) || (texMat->_texGen[stage + 2]._uvSource == UVTexShoreAnim));

          // Calculate 4 sea matrices
          Matrix4 m0 = MIdentity;
          Matrix4 m1 = texMat->_texGen[stage]._uvTransform;
          Matrix4 m2 = texMat->_texGen[stage]._uvTransform;
          Matrix4 m3 = texMat->_texGen[stage]._uvTransform;
          _sceneProps->GetSeaWaveMatrices(m0, m1, m2, m3, prop);
          ConvertMatrixTransposed(matrix[0], m0);
          ConvertMatrixTransposed(matrix[1], m1);
          ConvertMatrixTransposed(matrix[2], m2);
          ConvertMatrixTransposed(matrix[3], m3);
          pMatrix[0] = &matrix[0];
          pMatrix[1] = &matrix[1];
          pMatrix[2] = &matrix[2];
          pMatrix[3] = &matrix[3];
        }
        if (stage>=nMatrices) nMatrices = stage+1;
        break;
      case UVPos:
      case UVNorm:
      case UVWorldPos:
      case UVWorldNorm:
      case UVTex:
      case UVTex1:
        ConvertMatrixTransposed(matrix[stage], texMat->_texGen[stage]._uvTransform);
        pMatrix[stage] = &matrix[stage];
        if (stage>=nMatrices) nMatrices = stage+1;
        break;
      default:
        pMatrix[stage] = NULL;
        break;
      }

      // Handle UV animated texture
      switch (texMat->_texGen[stage]._uvSource)
      {
      case UVTex:
      case UVTex1:
        if (mat._UVchanged)
        {
          ConvertMatrixTransposed(matrix[stage], M4Identity);
          matrix[stage]._14 += mat._offsetU; 
          matrix[stage]._24 += mat._offsetV; 

          // if we are overriding, strange things can happen
          pMatrix[stage] = &matrix[stage];
        }
        break;
      }
    }

    // Set the _uvSource and _uvSourceCount. Note that this must be done prior
    // EnableCustomTexGen because it uses the result
    {
      // TODO: scan tex-gens instead of stages
      // Select UVSources
      int nStages = texMat->_nTexGen;
      if (nStages > MaxStages)
      {
        Fail("Error: Number of texgen stages is greater than the number of stages the engine can handle");
        nStages = MaxStages;
      }
      // Usually is stage 0 not filled in the rvmat and implicit value is UVNone
      // but we know we need there the UVTex
      if (texMat->_texGen[0]._uvSource==UVNone)
      {
        CBState(cb)._uvSource[0] = UVTex;
      }
      else
      {
        CBState(cb)._uvSource[0] = texMat->_texGen[0]._uvSource;
      }

      // Fill out the VU sources, determine the last that is not UVNone (this will be used to calculate the _uvSourceCount)
      int lastNotNone = 0;
      bool onlyTex0 = true;
      for (int i = 1; i < nStages; i++)
      {
        UVSource src = texMat->_texGen[i]._uvSource;
        CBState(cb)._uvSource[i] = src;
        if (src != UVNone && src!=UVPos) lastNotNone = i;
        if (src == UVTex1) onlyTex0 = false;
      }
      CBState(cb)._uvSourceCount = lastNotNone + 1;
      CBState(cb)._uvSourceOnlyTex0 = onlyTex0;
    }

    // Set the texgen matrices
    EnableCustomTexGen(cb,pMatrix, nMatrices, true);

    SelectPixelShader(cb,texMat->GetPixelShaderID());

    return;
  }

  PrepareSingleTexDiffuseA(cb,TexMaterialLODInfo());
}

void EngineDD11::EnableCustomTexGen(int cb, const PASSMATRIXTYPE **matrix, int nMatrices, bool optimize)
{
  RendState &cbState = CBState(cb);
  if (!cbState._tlActive) return;

  // texGenScaleOrOffset is going to be used
  cbState._texGenScaleOrOffsetHasChanged = false;

  // only uv source is currently supported
#if DO_TEX_STATS
  if (LogStatesOnce)
  {
    //LogF("Set tex gen %.1f,%.1f",matrix0 ? matrix0->_11 : 0.0f,matrix1 ? matrix1->_11 : 0.0f );
  }
#endif
  for (int i = 0; i < nMatrices; i++)
  {
    // Get either source matrix or identity
    const PASSMATRIXTYPE *pMatrix = matrix[i];
    if (pMatrix == NULL) pMatrix = &D3DMatIdentity;

    // If UV source is specified for given i, then include the UV decompression matrix
    PASSMATRIXTYPE scaledMatrix;
    if (i < cbState._uvSourceCount)
    {
      int uvSetIndex = -1;
      switch (cbState._uvSource[i])
      {
      case UVTexWaterAnim:
      case UVTex:
        uvSetIndex = 0;
        break;
      case UVTex1:
        uvSetIndex = 1;
        break;
      }
      if (uvSetIndex >= 0)
      {
        // UV decompression matrix - opposite to label UVCOMPDECOMP
        PASSMATRIXTYPE sao = D3DMatIdentity; // Scale And Offset matrix
        sao._11 = cbState._texGenScale[uvSetIndex].u * (1.0f / (UVCompressedMax - UVCompressedMin));
        sao._22 = cbState._texGenScale[uvSetIndex].v * (1.0f / (UVCompressedMax - UVCompressedMin));
        sao._14 = cbState._texGenOffset[uvSetIndex].u - UVCompressedMin / float(UVCompressedMax - UVCompressedMin) * cbState._texGenScale[uvSetIndex].u;
        sao._24 = cbState._texGenOffset[uvSetIndex].v - UVCompressedMin / float(UVCompressedMax - UVCompressedMin) * cbState._texGenScale[uvSetIndex].v;
        MultiplyMatrix4(scaledMatrix, *pMatrix, sao);
        pMatrix = &scaledMatrix;
      }
    }

    // Set the texgen matrix
    SetVertexShaderConstantF(cb, VSC_TexTransform, (float*)pMatrix, 2, true, 2*i);
  }
#if _ENABLE_REPORT
  // done to make sure values which cannot be relied upon contain zeros, so that they cause visual errors
  for (int i=nMatrices; i<MaxStages; i++)
  {
    const PASSMATRIXTYPE *pMatrix = &D3DMatZero;
    SetVertexShaderConstantF(cb, VSC_TexTransform, (float*)pMatrix, 2, true, 2*i);
  }
#endif
}

void EngineDD11::AddLocalVRAMAllocated(int size)
{
  _freeLocalVramAmount -= size;
  _freeEstDirty += (abs(size)>>20)+1;
  _resetDirty += (abs(size)>>20)+1;
}

void EngineDD11::D3DTextureDestroyed( SurfaceInfoD3D11& surface )
{
  // this assert is stronger than necessary, but as we use UnsetAllDiscardableResources
  // to make sure we guarantee the real condition, it is the easier way to check if we do it everywhere as needed
  Assert(CheckAllDiscardableResourcesUnset());

  for (int cb=0; cb<CBStateCount(); cb++)
  {
    for (int i = 0; i < lenof(_cbState[cb]._lastHandle); i++)
    {
      // if someone is releasing a texture we have still set, we have a problem
      // to prevent this, we unset all textures when we end rendering
      // no textures should be released while rendering
      DoAssert(_cbState[cb]._lastHandle[i] != surface.GetResourceView(true)
        && _cbState[cb]._lastHandle[i] != surface.GetResourceView(false));
    }
  }
}

/**
@param limit max. VRAM+AGP allocation allowed for vertex buffers (textures already deduced)
*/
size_t EngineDD11::ThrottleVRAMAllocation(size_t limit, int minAge)
{
  // _vBuffersAllocated is the real VRAM usage
  if ((size_t)_vBuffersAllocated<=limit) return 0;
  // this does not distinguish between used/needed, though
  size_t used = MemoryControlled();
  size_t needed = MemoryControlledRecent();
  int neededVRAM = toLargeInt(_vBuffersAllocated*float(needed)/used);
  // how much we need to free to get into the limit
  size_t wantFree = _vBuffersAllocated-limit;
  // we will not free so much that we would be freeing what is needed
  size_t canFree = _vBuffersAllocated-neededVRAM;
  if (canFree<=0) return 0;
  if (wantFree>canFree) wantFree = canFree;
  return Free(wantFree,minAge);
}

float EngineDD11::VRAMDiscardMetrics() const
{
  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  VertexBufferD3D11LRUItem *toDiscard = _vBufferLRU.First();
  if (!toDiscard) return 0;
  return _vBufferLRU.FrameID()-toDiscard->GetFrameNum();
}

int EngineDD11::VRAMDiscardCountAge(int minAge, int *totalSize) const
{
  int count = 0;
  int size = 0;

  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  for (VertexBufferD3D11LRUItem *last = _vBufferLRU.First(); last; last = _vBufferLRU.Next(last))
  {
    int age = _vBufferLRU.FrameID()-last->GetFrameNum();
    if (age<minAge) break;

    count++;
    size += last->GetStoredMemoryControlled();
  }
  if (totalSize) *totalSize = size;
  return count;
}

void EngineDD11::KickOffCBDebug()
{
  if(_backgroundD3D==RecNone) return;
  // with predication we cannot flush, as the scope may need to be copied
  if (!Glob.config._predication)
  {
    FlushBackground(_bg,true,"cbDbg");
  }
  else
  { // cannot flush - we can process, but not destroy
    // submit the data
    _bg.ProcessLazyDataPending();
    SubmitBackground(_bg,true,true,CleanUpNone,"cbDbg");

    // wait until all data are processed
    while (_bg._results.CountPending(ProcessResults)>0)
    {
      BGThreadProcess();
    }

  }
}


bool EngineDD11::CheckAllDiscardableResourcesUnset() const
{
  for (int cb=0; cb<CBStateCount(); cb++)
  {
    for (int i = 0; i < lenof(_cbState[cb]._lastHandle); i++)
    {
      if (_cbState[cb]._lastHandle[i] != (TextureHandle11*)(-1)) return false;
    }
    if (_cbState[cb]._vBufferLast.NotNull()) return false;
    if (_cbState[cb]._iBufferLast.NotNull()) return false;
  }
  return true;
}

bool EngineDD11::VerifyLowLevelThreadAccess() const
{
#if _ENABLE_REPORT
  if (Glob.config._renderThread!=Config::ThreadingReal) return true;

  if (_backgroundD3D==RecHighLevel)
  {
    // when recording high level, only worker thread may use low-level functionality
    return GetCurrentThreadId() == _bg._threadId;
  }
  else
  {
    // when not recording high level, only main thread or jobs may use low-level functionality
    if (GetCurrentThreadId() == _bg._threadId)
    {
      // background thread access detection - we have a problem
      return false;
    }
    if (Glob.config._mtSerial)
    {
      // if rendering jobs are serial, we can test the main thread ID
      return CheckMainThread();
    }
    return true;
  }
#else
  return true;
#endif
}

bool EngineDD11::VerifySpecialLevelThreadAccess() const
{
#if _ENABLE_REPORT
  if (Glob.config._renderThread!=Config::ThreadingReal) return true;

  if (_backgroundD3D==RecHighLevel || _backgroundD3D==RecLowLevel)
  {
    // when recording high level or low level, only worker thread may use special level functionality
    return GetCurrentThreadId() == _bg._threadId;
  }
  else
  {
    return CheckMainThread();
  }
#else
  return true;
#endif
}

void EngineDD11::SwitchToBackBufferRenderTarget()
{
  const int cb = -1;
  // if there is depth buffer, it is associated with render target, not back buffer
  // Set the render target
  SetRenderTargetAndDepthStencil(_backBuffer, DepthStencil::DepthStencilNULL, _minZRange, _maxZRange);

  if (_caps._canWriteSRGB)
  {
    D3DSetRenderState(cb, D3DRS_SRGBWRITEENABLE_OLD,_linearSpace);
  }
  // once we render to the backbuffer with SRGB conversion, we are no longer in linear space
  _linearSpace = false;
}

void EngineDD11::ReportShaderErrors(ID3D10Blob* errorMsgs)
{
  if (!errorMsgs)
    return;

  const char * errMsg = (const char*)errorMsgs->GetBufferPointer();
  if (!errMsg)
    return;

  int errMsgSize = errorMsgs->GetBufferSize();
  if (errMsgSize == 0)
    return;

  // error messages are sometimes too large, we have to split them
  int offset = 0;
  static const int chunkSize = 512;
  while (errMsgSize > 0)
  {
    size_t length = errMsgSize > chunkSize ? chunkSize : errMsgSize;
    RString err(errMsg + offset, length);
    RptF("%s", err.Data());
    offset += length;
    errMsgSize -= length;
  }
}

TextureHandle11* EngineDD11::GetCurrRenderTargetAsTexture()
{
  DoAssert(_currRenderTarget._resource);
  if (!_currRenderTarget._resource)
    return 0;

  if (IsFSAAEnabled() && _currRenderTarget._resource == _renderTargetS._resource)
  {
     // resolve MSAA render target
     CALL_D3D(ResolveSubresource, _renderTargetD._resource, 0, _renderTargetS._resource, 0, _rtFormat);
     return _renderTargetD._srView;
  }
  else if (_currRenderTarget._srView)
  {
    return _currRenderTarget._srView;
  }
  else if (_currRenderTarget._resource != _renderTargetD._resource)
  {
    // resources must be the same size, or this will NOT work!
    CALL_D3D(CopyResource, _renderTargetD._resource, _currRenderTarget._resource);
    return _renderTargetD._srView;
  }
  else
  {
    Fail("No other render target available");
  }
  return _renderTargetD._srView;
}

TextureHandle11* EngineDD11::CreateRenderTargetCopyAsTexture(
#if _ARMA3_REFRACTION
  int cb
#endif
  )
{
  DoAssert(_currRenderTarget._resource);
  if (!_currRenderTarget._resource)
    return 0;

  if (IsFSAAEnabled() && _currRenderTarget._resource == _renderTargetS._resource)
  {
    // resolve MSAA render target
#if _ARMA3_REFRACTION
    CALL_CB(cb, ResolveSubresource, _renderTargetD._resource, 0, _renderTargetS._resource, 0, _rtFormat);
#else
    CALL_D3D(ResolveSubresource, _renderTargetD._resource, 0, _renderTargetS._resource, 0, _rtFormat);
#endif
  }
  else if (_currRenderTarget._srView != _renderTargetD._srView)
  {
    // resources must be the same size, or this will NOT work!
#if _ARMA3_REFRACTION
    CALL_CB(cb, CopyResource, _renderTargetD._resource, _currRenderTarget._resource);
#else
    CALL_D3D(CopyResource, _renderTargetD._resource, _currRenderTarget._resource);
#endif
  }
  else
  {
    Fail("No other render target available");
  }

  return _renderTargetD._srView;
}

void EngineDD11::ForEachPostEffectHndl(IPPHndlFunc &func)
{
  for (int i = 0; i < _pp._effects.Size(); i++)
  {
    func(_pp._effects[i]);
  }
}

bool EngineDD11::ValidatePPPriority(int priority) const
{
  for (int i = 0; i < _pp._effects.Size(); i++)
  {
    const PostProcessHndl &ppHndl = _pp._effects[i];
    PostProcess *pp = static_cast<PostProcess*>(ppHndl._pp.GetRef());

    if (pp && pp->GetPriority() == priority) return false;
  }

  return true;
}

/* Locate process effect on the base of hndl, if hndl not found return InvalidPPHndl. */
PostProcessHndl& EngineDD11::AccessPostprocess(int hndl)
{
#if VALIDATE_HNDL_ENABLE
  bool hndlFound = ValidatePPEHndl(_pp._effects, hndl);
  Assert(hndlFound);
#endif

  int index = _pp._effects.FindKey(hndl);

  if (index >= 0)
  {
    PostProcessHndl &ppHndl = _pp._effects[index];

    if (hndl == ppHndl._hndl)
    {
      return ppHndl;
    }
  }

  // not valid handle set
  return InvalidPPHndl;
}

/* Used for post effect sort */
struct PPPriorities
{
  int _priority;
  Ref<EngineDD11::PostProcess> _pp;
};

static int ComparePriorities(const PPPriorities *pp0, const PPPriorities *pp1)
{
  int diff = pp0->_priority - pp1->_priority;
  return sign(diff);
}


void EngineDD11::DoPostprocess(float deltaT, const PostFxSettings &pfx)
{

#ifdef _RAIN3D
  if (!_thermalVisionThis && _pp._rain3D && (_pp._rain3D->GetLevel() & _postFXLevelMaskThis))
  {
    _pp._rain3D->Do(false);
  }
#endif

  End3DRendering(); // End rendering of the 3D part of the scene

  if (_useAToC)
  {
    D3DEnableAlphaToCoverage(-1, false);
  }


  //return; //<<< SKIP ALL PP FOR DEBUG (by Medart)<<<

  DoAssert(_linearSpace);
  // no depth buffer will be required during glow or final
  if (LogStatesOnce) LogF("Postprocess: Set Depth to NULL");
  SetRenderTargetAndDepthStencil(_currRenderTarget, DepthStencil::DepthStencilNULL, _minZRange, _maxZRange);

  bool gaussBlurDone = false;

  // Creating of Gaussian blur texture if required
  if (IsDepthOfFieldEnabled() || _pp._dynamicBlur->IsActive() || _pp._wetDistort->IsEnabled()/*|| IsGlowEnabled() */)
  {
    if (_pp._gaussianBlur)
    {
      _pp._gaussianBlur->Do(false);
      gaussBlurDone = true;
    }
  }

  // when rescaling is needed, we force a separate final pass
  // thermal imaging contains its own resampling
  bool forceFinal = (_wRT!=_w || _hRT!=_h) && !_thermalVisionThis;

  if (_thermalVisionThis)
  {
    // Final post process step (copying data to back buffer)
    // make sure no SRGB conversion is done during the copy
    _linearSpace = false; 
    _caps._rtIsSRGB = false;

    // Do the post process
    static bool blurred = true;
    if (blurred)
    {
      if (_pp._finalThermal)
      {
        _pp._finalThermal->SetParams(&deltaT, 1);
        _pp._finalThermal->Do(true);
      }
    }
    _tiVisualizationDone = true;
  }
  else
  {
    // Depth of field
    // We need the glow buffer if we want to perform Glow HDR
    if (IsDepthOfFieldEnabled())
    {
      if (_farOnlyThis)
      {
        if (_pp._distantDOF && (_pp._distantDOF->GetLevel() & _postFXLevelMaskThis))
        {
          _pp._distantDOF->Do(false);
        }
      }
      else
      {
        if (_pp._DOF && (_pp._DOF->GetLevel() & _postFXLevelMaskThis))
        {
          _pp._DOF->Do(false);
        }
      }
    }

    if (_pp._rotBlur && pfx.rotBlur && (_pp._rotBlur->GetLevel() & _postFXLevelMaskThis))
    {
      _pp._rotBlur->Do( false );
    }

    AUTO_STATIC_ARRAY(PPPriorities, sort, 16);

    if (_nightVisionThis /*&& _postFxEnabledThis*/)
    {
#if ENABLE_NVG_NOISE
      // NVG suppress post effects
      if (_pp._nvgNoise && (_pp._nvgNoise->GetLevel() & _postFXLevelMaskThis)) 
      {
        _pp._nvgNoise->Enable(true);

        PPPriorities nvgNoise = { _pp._nvgNoise->GetPriority(), _pp._nvgNoise };
        sort.Add(nvgNoise);
      }
#endif
    }
    else
    {
#if ENABLE_NVG_NOISE
      if (_pp._nvgNoise && (_pp._nvgNoise->GetLevel() & _postFXLevelMaskThis)) _pp._nvgNoise->Enable(false);
#endif
    }

    // add effects if set to enabled in video options
    if (_pp._filmGrain && (_pp._filmGrain->GetLevel() & _postFXLevelMaskThis) && _pp._filmGrain->IsActive())
    {
      //film grain should have the biggest priority
      PPPriorities filmGrainPrior = { _pp._filmGrain->GetPriority(), _pp._filmGrain };
      sort.Add(filmGrainPrior);
    }

    if (_pp._radialBlur && (_pp._radialBlur->GetLevel() & _postFXLevelMaskThis))
    {
      PPPriorities radBlurPrior = { _pp._radialBlur->GetPriority(), _pp._radialBlur };
      sort.Add(radBlurPrior);
    }

    if (_pp._chromAber && (_pp._chromAber->GetLevel() & _postFXLevelMaskThis))
    {
      PPPriorities chAbbPrior = { _pp._chromAber->GetPriority(), _pp._chromAber };
      sort.Add(chAbbPrior);
    }

    if (_pp._wetDistort && (_pp._wetDistort->GetLevel() & _postFXLevelMaskThis))
    {
      PPPriorities wDistPrior = { _pp._wetDistort->GetPriority(), _pp._wetDistort };
      sort.Add(wDistPrior);
    }
    if (_pp._dynamicBlur && (_pp._dynamicBlur->GetLevel() & _postFXLevelMaskThis) && _pp._dynamicBlur->IsActive())
    {
      PPPriorities dBlurPrior = { _pp._dynamicBlur->GetPriority(), _pp._dynamicBlur };
      sort.Add(dBlurPrior);
    }
    if (!_nightVisionThis)
    {
      if (_pp._clrInvers && (_pp._clrInvers->GetLevel() & _postFXLevelMaskThis) && _pp._clrInvers->IsEnabled())
      {
        PPPriorities cliPrior = { _pp._clrInvers->GetPriority(), _pp._clrInvers };
        sort.Add(cliPrior);
      }

      if (_pp._colors && (_pp._colors->GetLevel() & _postFXLevelMaskThis) && _pp._colors->IsActive())
      {
        PPPriorities colorPrior = { _pp._colors->GetPriority(), _pp._colors };
        sort.Add(colorPrior);
      }
    }

    int glowPrior = _pp._glow->GetPriority();
    bool gaussBlurNeeded = false;

    // include custom effects
    for (int i = 0; i < _pp._effectsThis.Size(); i++)
    {
      PostProcessHndl &pHndl = _pp._effectsThis[i];

      PostProcess *pProces = static_cast<PostProcess*> (pHndl._pp.GetRef());
      if (pProces && pProces->IsEnabled() && (pProces->GetLevel() & _postFXLevelMaskThis))
      {
        // color correction & color inversion not allowed in NV
        if (_nightVisionThis && (pHndl._type == PEColorCorrections || pHndl._type == PEColorInv)) continue;
        if (pHndl._type == PEDynamicBlur || pHndl._type == PEWetDistortion) gaussBlurNeeded = true;

        PPPriorities prior = { pProces->GetPriority(), pProces };
        sort.Add(prior);          
      }
    }

    // post effect (dynamic blur or wet distortion) are used and need results from gaussian blur
    if (!gaussBlurDone && gaussBlurNeeded)
    {
      if (_pp._gaussianBlur) { _pp._gaussianBlur->Do(false); }
    }

    // ascending sort by priority
    QSort(sort.Data(), sort.Size(), ComparePriorities);

    // find if there are any effects after glow
    int afterGlow = sort.Size();
    for (int i=0; i<sort.Size(); i++)
    {
      if (sort[i]._priority>=glowPrior)
      {
        afterGlow = i;
        break;
      }
    }

    bool someAfterGlow = afterGlow<sort.Size();
    // apply post effects with priorities less than glow priority (1000)
    for (int index = 0; index < afterGlow; index++)
    {
      sort[index]._pp->Do(false);
    }

    if(_pp._bloom && _doBloom)
    {
      //Test Bloom before HDR
      float pars[5] = {BloomThreshold, BloomBlurScale, BloomImgScale, BloomOverbrightCoef, BloomBlurKernelSize};
      _pp._bloom->SetParams(pars, 5);
    }

    if (_linearSpace)
    {
      if (_pp._glow)
      {
        float pars[3];
        pars[0] = deltaT;
        pars[1] = GScene->MainLight()->GetEyeAdaptMin();
        pars[2] = GScene->MainLight()->GetEyeAdaptMax();

        _pp._glow->SetParams(pars,lenof(pars));
        _pp._glow->Do(!someAfterGlow && !forceFinal);
      }
    }

    // PostProcess::Do(true) isLast == true -> postprocess to back-buffer - valid for PColors
    for (int index=afterGlow;index < sort.Size(); index++)
    {
      sort[index]._pp->Do(index==sort.Size()-1 && !forceFinal);
    }
  }

  // DX11 TODO DIAG
  //if (CHECK_ENG_DIAG(EDEDepthBuffer))
  //{
  //  // copy depth buffer...
  //  if (_pp._diagDepthBuffer)
  //    _pp._diagDepthBuffer->Do(false);
  //}


  // calling final here should not be necessary, as it should be called later anyone
  // however it seems to be clearer to call it here, so that we can be sure we leave this function in a state where:
  // back-buffer is set as the current render target
  if (forceFinal)
  {
    // check if bicubic can help (enlarging?)
    if ((_wRT<_w || _hRT<_h) && FinalRescalerMode==RescaleBicubic)
    {
      _pp._bicubic->Do(true);
    }
    else
    {
      // WIP: RESCALE - PPFinal can handle up to 2x2 - implement 4x4 downsampling as well
      _pp._final->Do(true,false);
    }
  }

  DoAssert(!_linearSpace);
  if (!_linearSpace && _caps._canWriteSRGB)
  {
    // SRGB write conversion was on before here - turn it off
#ifndef _X360SHADERGEN
    D3DSetRenderState(-1,D3DRS_SRGBWRITEENABLE_OLD,FALSE);
#endif
  }

  // some UI rendering may require depth buffer again
  /*
  // TODO: we need a depthbuffer suitable for the backbuffer here
  if (_depthBufferRT)
  {
  _d3DDevice->SetDepthStencilSurface(_depthBufferRT);
  _currRenderTargetDepthBufferSurface = _depthBufferRT;
  }
  */

  if (_spp._hit) _spp._hit->Update();
  if (_spp._tired) _spp._tired->Update();
  if (_spp._fire) _spp._fire->Update();
}

void EngineDD11::DoCalculateFlareIntensity(int x, int y, int sizeX, int sizeY)
{

  // Calculate flare intensity and set texture on stage 15
  _pp._flareIntensity->Do(x, y, sizeX, sizeY);

  const int cb = -1;
  // Set aperture texture on stage 14
  SetTexture(cb, 14, _pp._glow->GetApertureRT()._srView);
  D3DSetSamplerState(cb, 14, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
  D3DSetSamplerState(cb, 14, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
  D3DSetSamplerState(cb, 14, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);

  // Set decimation texture on stage 13
  SetTexture(cb, 13, _pp._glow->GetDecimationRT()._srView);

  // DX11 TODO PP
  //const bool decimatedSRGB = _downsampleFormat==D3DFMT_A8B8G8R8;
  //D3DSetSamplerState(cb, 13, D3DSAMP_SRGBTEXTURE_OLD, decimatedSRGB);

  // Set PS Constant 6
  Color eyeSens = _pp._glow->EyeSens(_pp._glow->Cones());
  // DX11 TODO - not sure if this is the right constant...
  SetPixelShaderConstantF(cb, PSC_RgbEyeCoef, D3DXVECTOR4(eyeSens.R(), eyeSens.G(), eyeSens.B(), 1), 1);
  //SetPixelShaderConstantF(cb, 6, D3DXVECTOR4(eyeSens.R(), eyeSens.G(), eyeSens.B(), 1), 1);

  float hdrMultiply = GetPostHDRBrightness() / GetHDRFactor();
  SetPixelShaderConstantF(cb, PSC_HDRMultiply, D3DXVECTOR4(hdrMultiply,hdrMultiply,hdrMultiply,hdrMultiply), 1);
}

Ref<EngineDD11::PostProcess> EngineDD11::CreatePostprocess(PostEffectType pEType)
{
  Ref<PostProcess> postProcess;

  switch (pEType)
  {
  case PEColorCorrections:
    {
      if (_pp._colors)
      {
        PPColorResources *ppRes = const_cast<PPColorResources *>(_pp._colors->Resources());

        if (ppRes)
        {
          Ref<PPColors> clrPP = new PPColors;
          if (clrPP && clrPP->Init(ppRes, true)) { clrPP->SetLevel(EngineDD11::PPLForce); postProcess = clrPP; }
        }
      }
    }
    break;

  case PERadialBlur:
    {
      PPResourcesExt *ppRes = const_cast<PPResourcesExt *>(_pp._radialBlur->Resources());

      if (ppRes)
      {
        Ref<PPRadialBlur> rbPP = new PPRadialBlur;
        if (rbPP && rbPP->Init(ppRes, true)) { rbPP->SetLevel(EngineDD11::PPLVeryLow); postProcess = rbPP; }
      }
    }
    break;

  case PEDynamicBlur:
    {
      PPDynBlurResources *ppRes = const_cast<PPDynBlurResources *>(_pp._dynamicBlur->Resources());

      if (ppRes)
      {
        Ref<PPDynamicBlur> wdPP = new PPDynamicBlur;
        if (wdPP && wdPP->Init(ppRes, _pp._gaussianBlur, true)) { wdPP->SetLevel(EngineDD11::PPLVeryLow); postProcess = wdPP; }
      }
    }
    break;

  case PEWetDistortion:
    {
      PPWetDistortResources *ppRes = const_cast<PPWetDistortResources *>(_pp._wetDistort->Resources());

      if (ppRes)
      {
        Ref<PPWetDistort> wdPP = new PPWetDistort;
        if (wdPP && wdPP->Init(ppRes, _pp._gaussianBlur, true)) { wdPP->SetLevel(EngineDD11::PPLLow); postProcess = wdPP; }
      }
    }
    break;

  case PEChromAberration:
    {
      PPResourcesExt *ppRes = const_cast<PPResourcesExt *>(_pp._chromAber->Resources());

      if (ppRes)
      {
        Ref<PPChromAber> wdPP = new PPChromAber;
        if (wdPP && wdPP->Init(ppRes, true)) { wdPP->SetLevel(EngineDD11::PPLLow); postProcess = wdPP; }
      }
    }
    break;

  case PEFilmGrain:
    {
      PPFilmGrainResources *ppRes = const_cast<PPFilmGrainResources *>(_pp._filmGrain->Resources());

      if (ppRes)
      {
        Ref<PPFilmGrain> wdPP = new PPFilmGrain;
        if (wdPP && wdPP->Init(ppRes, true)) { wdPP->SetLevel(EngineDD11::PPLVeryLow); postProcess = wdPP; }
      }
    }
    break;

  case PEColorInv:
    {
      PPResourcesExt *ppRes = const_cast<PPResourcesExt *>(_pp._clrInvers->Resources());

      if (ppRes)
      {
        Ref<PPColorsInversion> cliPP = new PPColorsInversion;
        if (cliPP && cliPP->Init(ppRes, true)) { cliPP->SetLevel(EngineDD11::PPLLow); postProcess = cliPP; }
      }
    }
    break;

  default:
    {
      RptF("Unrecognized post process type");
    }
  }

  return postProcess;
}

#if _XBOX
/**
On X360 we need the render target to be resolved when we want to use it as a texture
*/
void EngineDD11::ResolveRenderTarget()
{
  // _currRenderTargetSurface contains rendering results
  // _currRenderTarget contains the texture we would like to use
  _d3DDevice->Resolve(
    D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS,
    NULL,_currRenderTarget,NULL,0,0,NULL,0,0,NULL
    );
}
#endif

bool EngineDD11::GetRenderTargetDimensions(const RenderTarget& rt, int &outWidth, int &outHeight) const
{
  // first try resource
  ID3D11Texture2D* texture = rt._resource.GetRef();
  Assert(texture);

  // no resource - get it from the view
  if (!texture && rt._rtView)
  {
    D3D11_RENDER_TARGET_VIEW_DESC rtDesc;
    rt._rtView->GetDesc(&rtDesc);
    DoAssert(rtDesc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D);
    if (rtDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D)
      return false;

    ID3D11Resource *resource;
    rt._rtView->GetResource(&resource);
    DoAssert(resource);
    if (resource)
      texture = static_cast<ID3D11Texture2D*>(resource);
    Assert(texture);
  }
  
  if (!texture)
  {
    outWidth = 0;
    outHeight = 0;
    return false;
  }
  
  // get texture desc
  D3D11_TEXTURE2D_DESC desc;
  texture->GetDesc(&desc);
  outWidth = desc.Width;
  outHeight = desc.Height;

  return true;
}

bool EngineDD11::GetDepthStencilDimensions(const DepthStencil& ds, int &outWidth, int &outHeight) const
{
  // first try resource
  ID3D11Texture2D* texture = ds._resource.GetRef();
  Assert(texture);

  // no resource - get it from the view
  if (!texture && ds._dsView)
  {
    D3D11_DEPTH_STENCIL_VIEW_DESC dsDesc;
    ds._dsView->GetDesc(&dsDesc);
    DoAssert(dsDesc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D);
    if (dsDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D)
      return false;

    ID3D11Resource *resource;
    ds._dsView->GetResource(&resource);
    DoAssert(resource);
    if (resource)
      texture = static_cast<ID3D11Texture2D*>(resource);
    Assert(texture);
  }

  if (!texture)
  {
    outWidth = 0;
    outHeight = 0;
    return false;
  }

  // get texture desc
  D3D11_TEXTURE2D_DESC desc;
  texture->GetDesc(&desc);
  outWidth = desc.Width;
  outHeight = desc.Height;

  return true;
}

bool EngineDD11::GetShaderResourceDimensions(TextureHandle11* sr, int &outWidth, int &outHeight) const
{
  outWidth = 0;
  outHeight = 0;

  Assert(sr);
  if (!sr)
    return false;

  D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;
  sr->GetDesc(&srDesc);
  DoAssert(srDesc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D);
  if (srDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D)
    return false;

  ID3D11Resource *resource;
  sr->GetResource(&resource);
  DoAssert(resource);
  if (!resource)
    return false;

  ID3D11Texture2D *texture = static_cast<ID3D11Texture2D*>(resource);

  // get texture desc
  D3D11_TEXTURE2D_DESC desc;
  texture->GetDesc(&desc);
  outWidth = desc.Width;
  outHeight = desc.Height;

  return true;
}

void EngineDD11::CaptureRefractionsBackground()
{
#if _ARMA3_REFRACTION
  _numBgCaptures++;
  if (_numBgCaptures >= MaxBgCaptures)
  {
    ASSERT_DBG(false, ("Maximal number of bg captures for refraction exceeded (%d)!", _numBgCaptures));
    return;
  }
#endif

  TextureHandle11* rt = CreateRenderTargetCopyAsTexture();
  (void)rt;
  // make sure the refraction is prepared OK
  DoAssert(rt);
}

#if _ARMA3_REFRACTION
/// command buffer version
void EngineDD11::CaptureRefractionsBackground(int cb)
{
  _numBgCaptures++;
  if (_numBgCaptures >= MaxBgCaptures)
  {
    ASSERT_DBG(false, ("Maximal number of bg captures for refraction exceeded (%d)!", _numBgCaptures));
    return;
  }

  TextureHandle11* rt = CreateRenderTargetCopyAsTexture();
  (void)rt;
  // make sure the refraction is prepared OK
  DoAssert(rt);
}
#endif


/* Define unique handle. Handle is used for post efect instance identification. */
int EngineDD11::GetPostEffectHandle() const
{ 
  static int Hndl = 1; 
  return (Hndl++); 
}

void EngineDD11::DestroyShaderConstantBuffers()
{
  for (int type = 0; type < ShaderConstant::SCTCount; ++type)
  {
    for (int i=0; i<_shaderConstantBuffer[type].Size(); ++i)
    {
      _shaderConstantBuffer[type][i].Free();
    }
    for (int i=0; i< ShaderConstantBuffer::MaxConstantBufferCount; ++i)
    {
      _currentRenderState._constantBufferLast[type][i] = 0;
    }
  }
}

void EngineDD11::TextureDestroyed( Texture *tex )
{
  TextureD3D11 *texture = static_cast<TextureD3D11 *>(tex);
  SurfaceInfoD3D11* surface = texture->GetSurface();
  if (surface)
    D3DTextureDestroyed(*surface);
}

inline void EngineDD11::BGThreadProcess()
{
  PROFILE_SCOPE_EX(bgD3D,dd11)

    while (_bg._results.CountPending(ProcessResults)>0)
    {
      // consume one item
      const RenderCBToPlay &toRender = _bg._results.Get(ProcessResults);

      if (toRender.Size()>0)
      {
#ifdef _XBOX
        // release must be done before Advance, hence Acquire/Release is here
        _d3DDevice->AcquireThreadOwnership();
#endif
        // subscribe to data - see _backgroundD3D
        MemorySubscribe();

        PlayCB(toRender);

        // advancing is a way to mark data are consumed. Before doing so we must publish data - see _backgroundD3D
        MemoryPublish();

#ifdef _XBOX
        // make sure the data are submitted in a timely manner to prevent waiting for fences which were not submitted
        _d3dDevice->InsertFence();
        _d3dDevice->ReleaseThreadOwnership();
#endif
      }

      // report data as consumed
      _bg._results.Advance(ProcessResults);
    }
}

inline void EngineDD11::VisThreadProcess()
{
  PROFILE_SCOPE_EX(visul,dd11)

    // consume all items currently present
    _vis.Update<false>();
}


void EngineDD11::BGThread()
{
  while (true)
  {
    if (!_bg.OneIteration())
      break;

    if (Glob.config._renderThread!=Config::ThreadingEmulated) BGThreadProcess();
  }
  // before terminating process any data still pending
  // this is important because some data may be destructed during processing
  if (Glob.config._renderThread!=Config::ThreadingEmulated) BGThreadProcess();

  // there is no guarantee each sequence will finish by an explicit cleanup
  BGReleaseResourcesAndTextures();
}

void EngineDD11::VisThread()
{
  while (_vis.OneIteration())
  {
    if (Glob.config._renderThread!=Config::ThreadingEmulated) VisThreadProcess();
  }
  // before terminating process any data still pending
  // this is important because some data may be destructed during processing
  if (Glob.config._renderThread!=Config::ThreadingEmulated) VisThreadProcess();
}

bool EngineDD11::UsePredication() const
{
  // we cannot use predication when wireframe on solid rendering is on (we need to render wireframe objects after normal render)
#if _ENABLE_CHEATS
  bool wireframe = GetWireframeMode() == WMWireframeOnSolid && GetDrawWireframe();
#else
  bool wireframe = false;
#endif

  return Glob.config._renderThread!=Config::ThreadingNone && Glob.config._useCB && Glob.config._predication && !wireframe;
}

template <class CB_Type, int cbCount, int consumerCount>
bool EngineDD11::WorkerThread<CB_Type,cbCount,consumerCount>::ProcessBackground()
{
  // simulate background thread - this is useful for PIX, as its support of multithreaded rendering is not very good
  if (Glob.config._renderThread==Config::ThreadingEmulated) GEngineDD11->ThreadProcess<WorkerThread>();

  Assert(GEngineDD11->CheckAsync<WorkerThread>());
  // we can destruct while waiting - everything up to _bgRead can be destructed
  // this may improve latency, as after we can destruct most of the command while waiting
  int toDestroy = _results.HowMuchBehind(DestroyResults,ProcessResults);
  if (toDestroy==0) return false;
  // check how many results can be destroyed
  int canDestroy = _results.HowMuchBehind(DestroyResults,CanDestroyResults);
  if (canDestroy<toDestroy)
  {
    toDestroy = canDestroy;
    if (canDestroy<=0)
    {
      ErrorMessage("Internal error: Rendering command buffer to small");
      return false;
    }
  }
  Assert(GEngineDD11->CheckThreadMemory(*this));
  while (--toDestroy>=0)
  {
    typename WorkerThread::CBType &res = _results.Get(DestroyResults);
    // if the CB is marked as noDestroy, skip it
    if (res.IsReadyToDestroy())
    {
      //TrapFor0x8000Scope lock("EngineDD11::ProcessBackground if (!res._noDestroy)");
#if LOG_RESULTS_ALLOC
      res.LogResultsDestroy();
#endif
      // we need to free each memory - in case it was using the dynamic fall-back
      res.Clear(_resultsMem);
      // if there is any memory to free, do it
      _resultsMem.Clear(res._memToClear);
      // once done, we can advance
      _results.Advance(DestroyResults);
      Assert(GEngineDD11->CheckThreadMemory(*this));
    }
    else
    {
      //TrapFor0x8000Scope lock("EngineDD11::ProcessBackground * else");
      // once done, we can advance
#if _DEBUG
      int wasEmpty = _results.EmptySpace(DestroyResults);
#endif
      _results.Advance(DestroyResults);
#if _DEBUG
      int isEmpty = _results.EmptySpace(DestroyResults);
      (void)isEmpty,(void)wasEmpty;
#endif
      Assert(GEngineDD11->CheckThreadMemory(*this));
      // mark the data as destroyed for easier debugging
      res._data = NULL;
      res._size = 0;
      res._memToClear = 0;
      Assert(GEngineDD11->CheckThreadMemory(*this));
    }
  }
  Assert(GEngineDD11->CheckThreadMemory(*this));

#if 0 // _ENABLE_REPORT
  DoAssert(GEngineDD11->CheckThreadMemory(*this));
#endif
  return true;
}

void EngineDD11::ProcessLRURequests()
{
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  while(Ref<VertexBufferD3D11> vb = _vbLRURequest.Get())
  {
    DoAssert(vb->RefCounter()>0);
    vb->RemoveFromSList();

    if (!vb->IsInList())
    {
      Fail("Refreshed buffer should be already in LRU");
    }
    else
    {
      _vBufferLRU.Refresh(vb);
    }
  }
  _vbLRURequest.Clear();
}

template <>
typename EngineDD11::BGWorkerThread::BGDataType &EngineDD11::GetBackgroundData<EngineDD11::BGWorkerThread>()
{
  return _cbState[0]._rec;
}

void EngineDD11::PlayCB(const RenderCBToPlay &cb)
{
#if PIX_NAMED_CB_SCOPES
  struct PIXScope
  {
    MyD3DDevice &device;

    PIXScope(MyD3DDevice &device, const char *name, DWORD color)
      :device(device)
    {
      DX11::ProfileBeginGraphScope(color,name);
    }
    ~PIXScope()
    {
      DX11::ProfileEndGraphScope();
    }
  } scope(_d3dDeviceNonRec,cb._scopeName,0);
#endif
#if LOG_RENDER_API
  LogRender("--- Obj %s, pred %d",cc_cast(cb._scopeName),cb._predicate);
#endif
  // render the job - respect predication
  // what is left is not predicated
  int predicateWanted = cb._predicate>=0 ? (1<<cb._predicate) : ~0;
  //int predicateWanted = -1;
  for(int offset = 0,predIndex=0; offset<cb.Size(); predIndex++)
  {
    int end;
    int predicate;
    if (predIndex<cb._predication.Size())
    {
      end = cb._predication[predIndex].end;
      Assert(end<=cb.Size());
      predicate = cb._predication[predIndex].predicate;
    }
    else
    {
      end = cb.Size();
      predicate = ~0;
    }

    // check if predicate matches
    if (predicate&predicateWanted)
    {
      // unconst_cast - Array<char> is hard to get working with const
      Array<char> cbTemp(unconst_cast(cb.Data()+offset),end-offset);
      SerialArrayData tempSerial(cbTemp);
      if (!cb._noDestroy)
      {
        // no need to delay destruction - we can safely destroy the resources while processing them
        // 1) for rendering we are guaranteeing D3D exclusive access anyway
        // 2) any memory allocations are MT safe
        _d3dDeviceNonRec._cb.Execute<true>(tempSerial);
      }
      else
      {
        _d3dDeviceNonRec._cb.Execute<false>(tempSerial);
      }
    }

    offset = end;
  }
}

void EngineDD11::DrawSSAO()
{
  // no ssao in Ti vision
  if (_thermalVisionThis) return;

  Assert (_backgroundD3D!=RecHighLevel); // we must be in the correct recording mode
  Assert (CheckMainThread()); // recording must be done from the main thread

  if (_pp._ssao)
  {
    _pp._ssao->CollectParams(); // separate params during ssao processing

    float fogEnd = CBState(0)._fogEnd;
    float fogStart = CBState(0)._fogStart;
    float fogCoef = CBState(0)._expFogCoef;

    CALL_CB_DIRECT(this, DrawSSAO, fogEnd, fogStart, fogCoef);

    // SSAO is calling all command directly to device, so we have to invalidate all states in immediate CB
    SetHWToUndefinedState(-1);
  }
}

void EngineDD11::DoDrawSSAO(float fogEnd, float fogStart, float fogCoef)
{
  if (_renderingMode != RMCommon) return;

  if (_pp._ssao.NotNull())
  {
    _pp._ssao->Do(fogEnd, fogStart, fogCoef);
  }
}

ComRef<IDXGIOutput> EngineDD11::GetAdapterOutput(IDXGIAdapter* adapter)
{
  ComRef<IDXGIOutput> output;

  Assert(adapter);
  if (!adapter)
    return output;

  // DX11 TODO
  // return first monitor
  HRESULT hr = adapter->EnumOutputs(0, output.Init());
  if (FAILED(hr) || output.IsNull())
  {
    Fail("No monitor found!");
  }

  return output;
}

HMONITOR EngineDD11::GetAdapterMonitor(IDXGIAdapter* adapter)
{
  Assert(adapter);
  if (!adapter)
    return 0;

  ComRef<IDXGIOutput> output = GetAdapterOutput(adapter);

  DXGI_OUTPUT_DESC desc;
  HRESULT hr = output->GetDesc(&desc);
  if (FAILED(hr))
  {
    Fail("Cannot get monitor desc!");
    return 0;
  }
  return desc.Monitor;
}

bool EngineDD11::IsSuitableRT( DXGI_FORMAT format, bool mustBeBlendable ) 
{
  UINT support = GetFormatSupport(format);

  // DX11 TODO - not sure about these...
  return (
    (support & D3D11_FORMAT_SUPPORT_RENDER_TARGET)
    && (support & D3D11_FORMAT_SUPPORT_BLENDABLE) 
    && (support & D3D11_FORMAT_SUPPORT_TEXTURE2D)
    && (support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)
    );
}

UINT EngineDD11::GetFormatSupport(DXGI_FORMAT format) 
{
  UINT support = 0;

  Assert(!_d3dDeviceNonRec.IsNull());
  if (_d3dDeviceNonRec.IsNull())
    return support;

  HRESULT hr = _d3dDeviceNonRec->CheckFormatSupport(format, &support);
  Assert(SUCCEEDED(hr));
  if (FAILED(hr))
    support = 0;

  return support;
}

int EngineDD11::GetMaxMultisampleLevels( DXGI_FORMAT format) 
{
  Assert(!_d3dDeviceNonRec.IsNull());
  if (_d3dDeviceNonRec.IsNull())
    return 0;

  int maxLevel = 0;
  for (int i=0; i <= D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT; ++i)
  {
    UINT dummy = 0;
    HRESULT hr = _d3dDeviceNonRec->CheckMultisampleQualityLevels(format, i, &dummy);
    if (SUCCEEDED(hr) && dummy > 0)
    {
      maxLevel = i;
    }
  }


  return maxLevel;
}

bool EngineDD11::CreateFSAAModeName(int numSamples, int quality, int maxQuality, RString& name)
{
  // see http://developer.nvidia.com/object/coverage-sampled-aa.html

  if (numSamples == 2 && quality == 0)
  {
    // MSAA
    name = "2x";
    return true;
  }
  else if (numSamples == 4 && quality == 0)
  {
    // MSAA
    name = "4x";
    return true;
  }
  else if (numSamples == 4 && quality == 8)
  {
    // nvidia CSAA
    name = "8x";
    return true;
  }
  else if (numSamples == 4 && quality == 16)
  {
    // nvidia CSAA
    name = "16x";
    return true;
  }
  else if (numSamples == 4 && quality == 32)
  {
    // nvidia CSAA
    name = "32x";
    return true;
  }
  else if (numSamples == 8 && quality == 8)
  {
    // nvidia CSAA
    name = "8xQ";
    return true;
  }
  else if (numSamples == 8 && quality == 16)
  {
    // nvidia CSAA
    name = "16xQ";
    return true;
  }
  else if (numSamples == 8 && quality == 32)
  {
    // nvidia CSAA
    name = "32xQ";
    return true;
  }
  else if (numSamples == 8 && quality == 0 && maxQuality < 2)
  {
    // ATI MSAA
    name = "8x";
    return true;
  }
  else if (quality == 0 && maxQuality < 2)
  {
    // some other type
    name = Format("%dx", numSamples);
    return true;
  }


  return false;  
}

void EngineDD11::InitFSAAModes(DXGI_FORMAT rtFormat, DXGI_FORMAT depthFormat)
{
  _fsaaModes.Clear();

  Assert(!_d3dDeviceNonRec.IsNull());
  if (_d3dDeviceNonRec.IsNull())
    return;

  // min. sample count for FSAA (1=no fsaa)
  int minSamplesCount = 2;

  UINT support = GetFormatSupport(rtFormat);
  if ((support & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET) &&
    (support & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE))
  {
    for (int i=minSamplesCount; i <= D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT; ++i)
    {
      UINT qualityRT = 0;
      HRESULT hr = _d3dDeviceNonRec->CheckMultisampleQualityLevels(rtFormat, i, &qualityRT);
      bool isRTOk = SUCCEEDED(hr) && qualityRT > 0;

      UINT qualityDepth = 0;
      hr = _d3dDeviceNonRec->CheckMultisampleQualityLevels(depthFormat, i, &qualityDepth);
      bool isDepthOk = SUCCEEDED(hr) && qualityDepth > 0;

      if (isRTOk && isDepthOk)
      {
        // determine maximum possible quality level
        int maxQualitylevel = intMin(qualityRT, qualityDepth);

        for (int q=0; q< maxQualitylevel; ++q)
        {
          RString name;
          if (CreateFSAAModeName(i, q, maxQualitylevel, name))
          {
            _fsaaModes.Add(FSAAInfo(i, q, name));
          }
        }
      }
    }

  }
}

DXGI_FORMAT EngineDD11::SelectRTFormat( int minBpp ) 
{
  // we prefer a high-resolution format for the render target
  DXGI_FORMAT bestRTFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  static const DXGI_FORMAT rtFormats[]=
  {
    DXGI_FORMAT_R10G10B10A2_UNORM,
    DXGI_FORMAT_R32G32B32A32_FLOAT,
    DXGI_FORMAT_R16G16B16A16_UNORM,
    DXGI_FORMAT_R16G16B16A16_FLOAT,
    DXGI_FORMAT_R8G8B8A8_UNORM
  };
  for (int i=0; i<lenof(rtFormats); i++)
  {
    if (FormatToBpp(rtFormats[i])<minBpp) continue;

    if (IsSuitableRT(rtFormats[i]))
    {
      bestRTFormat = rtFormats[i];
      break;
    }
  }
  return bestRTFormat;
}

void EngineDD11::OnAppActivated()
{
  if (!_d3dDeviceRec.IsNull())
  {
    InitDXGIOutput();
    Assert(_dxgiOutput);
    if (!_windowed && _dxgiOutput)
      _d3dDeviceRec->SetFullscreenState(TRUE, _dxgiOutput);
  }
}

void EngineDD11::DeInitCursors()
{
  _cursorTex = 0;
  _curCursor = 0;

  for (int i=0; i<_cursors.Size(); ++i)
  {
    HCURSOR cursor = _cursors[i]._handle;
    if (cursor)
    {
      BOOL ok = DestroyCursor(cursor);
      Assert(ok);
      ok;
    }
  }

  _cursors.Clear();
}

void EngineDD11::DeferVBufferRelease( VertexBuffer *buffer )
{
  // store the Ref, defer release to the moment we know we own the texture manager and D3D CS
  // a suitable moment is after worker thread flush (FinishFinishPresent)
  _vBufferToRelease.Add(buffer);
}

#if _HELISIM_RENDER_TO_TEXTURE

void EngineDD11::CreateR2TResources()
{  
  const DWORD width = R2TMaxResolution;
  const DWORD height = R2TMaxResolution;

  HRESULT err = CreateRenderTarget(width, height, _rtFormat, _rtMultiSampleCount, _rtMultiSampleQuality, D3D11_USAGE_DEFAULT, true, _r2tRT._renderTargetS);
  if (err != D3D_OK) 
  {
    DDError11("Error: R2T Render target texture creation failed.", err); 
    return;
  }

  // used for RT copy -> CreateR2TRenderTargetCopyAsTexture()
  err = CreateRenderTarget(width, height, _rtFormat, 1, 0, D3D11_USAGE_DEFAULT, true, _r2tRT._rtCopy);
  if (err != D3D_OK) 
  {
    DDError11("Error: R2T Render target texture creation failed.", err); 
    return;
  }

  // depth texture used as texture
  err = CreateRenderTarget(width, height, DXGI_FORMAT_R32_FLOAT, 1, 0, D3D11_USAGE_DEFAULT, true,  _r2tRT._renderTargetDepthInfo);
  Log("_renderTargetDepthInfo %dx%d", width, height);
  if (err != D3D_OK) 
  {
    DDError11("Error: R2T Render target depth texture creation failed.", err);
    return;
  }

  InitializeR2TTargets();
}


void EngineDD11::DestroyR2TResources()
{
  UnRegisterAllR2TSourceTextures();
  DestroyR2TTargets();

  _r2tRT._rtCopy.Free();
  _r2tRT._renderTargetS.Free();
  _r2tRT._renderTargetDepthInfo.Free();
}

void EngineDD11::ConvertFullToLite(R2TTargetDesc &full, R2TTargetDescLite &lite)
{
  lite._name = full._desc._name;
  lite._width = full._desc._width;
  lite._height = full._desc._height;
  lite._texture = full._desc._texture;
}

bool EngineDD11::ConvertLiteToFull(R2TTargetDesc &full, R2TTargetDescLite &lite)
{
  full._desc._name = lite._name;
  full._desc._width = lite._width;
  full._desc._height = lite._height;
  full._desc._texture = lite._texture;
  return true;
}

void EngineDD11::ClearR2TTarget(R2TTargetDescLite &desc)
{
  if (desc._name.GetLength() > 0 && desc._texture._rtView)
  {
    CALL_D3D_ENGINE(R2TClearTarget, desc._texture._rtView);
  }
}

void EngineDD11::InvalidateR2TTarget(R2TTargetDesc &tgt, bool registerTexture)
{
  DoAssert(!tgt._active);

  if (tgt._active)
  {
    tgt._releaseWanted = true;
  }
  else
  {
    if (tgt._desc._texture.NotNull())
    {
      if (registerTexture)
      {
        // move texture back to RT list
        R2TTargetDescLite &descLite = _r2tSrcTargets.Append();
        ConvertFullToLite(tgt, descLite);
        ClearR2TTarget(descLite);
      }
    }
  }

  tgt._desc._texture.Free();
  tgt._id = R2TUninitialized;
}

void EngineDD11::InitializeR2TTargets()
{
  _r2tTargets.Realloc(R2TRenderTargetCount);
  _r2tTargets.Resize(R2TRenderTargetCount);

  for (int i = 0; i < _r2tTargets.Size(); i++)
  {
    R2TTargetDesc &desc = _r2tTargets[i];
    desc._id = R2TUninitialized;
    // create HDR aperture textures
    desc._hdrPars = new R2THDRPars();
    desc._hdrPars->Initialize(this);
    desc._tiPars = new R2TTiPars();
    desc._tiPars->Initialize(this);
    desc._active = false;
    desc._releaseWanted = false;
  }

  _r2tBegin = false;
  _r2tCamsSyncWanted = true;
}

bool EngineDD11::R2TCamsSyncNeeded()
{
  if (!_r2tCamsSyncWanted) return false;

  _r2tCamsSyncWanted = false;  
  return true;
}

void EngineDD11::DestroyR2TTargets()
{
  for (int i = 0; i < _r2tTargets.Size(); i++)
  {
    R2TTargetDesc &tgt = _r2tTargets[i];

    tgt._desc._texture.Free();
    tgt._hdrPars.Free();
    tgt._tiPars.Free();
    tgt._clrPars.Free();
    tgt._active = false;
  }
}

void EngineDD11::TrackR2TTexUsage(const TextureD3D11* texture)
{
  if (!_r2tBegin)
  {
    for (int i = 0; i < _r2tTargets.Size(); i++)
    {
      if (_r2tTargets[i]._desc._texture._srView == texture->GetHandle(true)
        || _r2tTargets[i]._desc._texture._srView == texture->GetHandle(false))
      {
        _r2tTargets[i]._frameId = FrameId;
        break;
      }
    }
  }
}

void EngineDD11::CreateR2TDepthAsTexture()
{
  DoAssert(_r2tBegin);

  RECT rect = { 0, 0, _viewW, _viewH };
  Assert(_pp._stretchRect);
  if (_pp._stretchRect)
    _pp._stretchRect->Do(_renderTargetDepthInfo._srView, &rect, _r2tRT._renderTargetDepthInfo, NULL);
  _r2tRT._renderTargetDepthInfoSurfaceSet = true;
}

ComRef<TextureHandle11> EngineDD11::CreateR2TRenderTargetCopyAsTexture(const RECT *rect)
{
#if !_DEBUG
  DoAssert(!CheckMainThread());
#endif

  Assert(_pp._stretchRect);
  if (_pp._stretchRect)
    _pp._stretchRect->DoDirect(_r2tRT._renderTargetS._srView, rect, _r2tRT._rtCopy, NULL);

  return _r2tRT._rtCopy._srView;
}

void EngineDD11::RegisterR2TSourceTexture(const SurfaceDataD3D11 &texture, int w, int h, RString name)
{
#if _ENABLE_REPORT
  LogF("Register r2t texture %s, %x", cc_cast(name), texture._texture.GetRef());
#endif

  if (texture._texture.IsNull() || name.IsEmpty()) return;

  R2TTargetDescLite &src = _r2tSrcTargets.Append();
  src._texture._resource += texture._texture;
  src._texture._srView += texture._resView;
  src._texture._rtView += texture._rtView;
  src._width = w;
  src._height = h;
  src._name = name;
}

void EngineDD11::UnRegisterR2TSourceTexture(const SurfaceDataD3D11 &texture)
{
  if (texture._texture.IsNull()) return;

  int index = -1;
  for (int i = 0; i < _r2tSrcTargets.Size(); i++)
  {
    if (_r2tSrcTargets[i]._texture._resource.GetRef() == texture._texture.GetRef())
    {
      index = i;
      break;
    }
  }

  // release texture
  if (index != -1)
  {
#if _ENABLE_REPORT
    LogF("Unregister r2t texture %x", texture._texture.GetRef());
#endif
    _r2tSrcTargets.Delete(index);
  }
  else
  {
    // texture is bind to any render target
    for (int i = 0; i < _r2tTargets.Size(); i++)
    {
      R2TTargetDesc &tgt = _r2tTargets[i];
      if (tgt._desc._texture._resource.GetRef() == texture._texture.GetRef())
      {
        if (tgt._active)
        {
          //invalidate RT
#if _ENABLE_REPORT
          LogF("Render target %s bind, cannot be released now.", cc_cast(_r2tSrcTargets[i]._name));
#endif
          _r2tTargets[i]._releaseWanted = true;
        }
        else
        {
          InvalidateR2TTarget(_r2tTargets[i]);
        }

        break;
      }
    }
  }
}

void EngineDD11::UnRegisterAllR2TSourceTextures()
{
  _r2tSrcTargets.Clear();
}

RenderTargetID EngineDD11::BindR2T(const RString &name)
{
  DoAssert(!_r2tBegin);

  if (!_r2tSrcTargets.Size()) return R2TUninitialized;

  int index = -1;
  for (int i = 0; i < _r2tSrcTargets.Size(); i++)
  {
    if (_r2tSrcTargets[i]._name == name)
    {
      index = i;
      break;
    }
  }

  if (index != -1)
  {
    // reuse index
    int index1 = -1;
    // locate free RT
    for (int i = 0; i < _r2tTargets.Size(); i++)
    {
      if (_r2tTargets[i]._id == R2TUninitialized)
      {
        index1 = i;
        break;
      }
    }

    if (index1 != -1)
    {
      DoAssert(index1 >= R2TRenderTarget0 && index1 < R2TRenderTargetCount);

      R2TTargetDescLite &src = _r2tSrcTargets[index];
      R2TTargetDesc &dst = _r2tTargets[index1];

      if (!ConvertLiteToFull(dst, src))
      {
#if _ENABLE_REPORT
        LogF("Error: R2T target surface creation failed");
#endif
        return R2TUninitialized;
      }
      dst._active = false;
      dst._id = (RenderTargetID) index1;
      dst._frameId = 0;

      _r2tSrcTargets.Delete(index);
      return dst._id;
    }

#if _ENABLE_REPORT
    LogF("No free render target found, max RT is set to %d", R2TRenderTargetCount);
#endif
    return R2TUninitialized;
  }

#if _ENABLE_REPORT
  LogF("Render target not found, %s", cc_cast(name));
#endif

  return R2TUninitialized;
}

__forceinline bool IsR2TValid(RenderTargetID rtid) 
{
  return rtid >= R2TRenderTarget0 && rtid < R2TRenderTargetCount;
}

bool EngineDD11::UnBindR2T(RenderTargetID rtid)
{
  DoAssert(!_r2tBegin);

  if (IsR2TValid(rtid))
  {
    InvalidateR2TTarget(_r2tTargets[rtid], true);

    return true;
  }

  return false;
}

bool EngineDD11::IsBindR2T(RenderTargetID rtid) const
{
  if (IsR2TValid(rtid))
  {
    RenderTargetID id = _r2tTargets[rtid]._id;  
    return (!_r2tTargets[rtid]._releaseWanted && id != R2TUninitialized);
  }

  return false;
}

//! Check whether target was used in main scene
bool EngineDD11::WasR2TUsed(RenderTargetID rtid) const
{
#if _ENABLE_REPORT
  DoAssert(IsR2TValid(rtid));
#endif

  return FrameId < (_r2tTargets[rtid]._frameId + R2TRenderTargetCount);
}

void EngineDD11::InitR2TDraw() 
{
  if (_thermalVision)
  {
    // Fog color is derived from the air temperature in thermal case
    float fR, fG, fB;
    EncodeTemperatureToRGB1(_airTemperature, fR, fG, fB);
    _clearColor = Color(fR, fG, fB, 1.0f);

    // Set texture and sampling parameters
    SetTexture(-1,TEXID_TC, _tableTc._resView, false);
    D3DSetSamplerState(-1, TEXID_SHADOWMAP, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
    D3DSetRenderState(-1, D3DRS_SRGBWRITEENABLE_OLD,FALSE);
  }
}

#define R2T_ASSIGN_PARS(from, to, name) from##name = to##name;

#define R2T_PARS_TO_ASSIGN(from, to) \
  R2T_ASSIGN_PARS(from, to, _lightGain) \
  R2T_ASSIGN_PARS(from, to, _accomFactor) \
  R2T_ASSIGN_PARS(from, to, _accomFactorPrev) \
  R2T_ASSIGN_PARS(from, to, _accomFactorFixed) \
  R2T_ASSIGN_PARS(from, to, _accomodateEye) \
  R2T_ASSIGN_PARS(from, to, _lastKnownAvgIntensity) \
  R2T_ASSIGN_PARS(from, to, _nightEye) \

void EngineDD11::BeginR2T(RenderTargetID rtid)
{
  DoAssert(!_r2tBegin);

  _r2tRT._viewWSaved = _viewW;
  _r2tRT._viewHSaved = _viewH;
  _r2tRT._minZRange = _minZRange;
  _r2tRT._maxZRange = _maxZRange;
  _r2tRT._wSaved = _w;
  _r2tRT._hSaved = _h;

  // save current GEngine values
  R2T_PARS_TO_ASSIGN(_r2tRT., )

    _r2tRT._globTIPars._tiMode = _tiMode;

  R2TTargetDesc &target = _r2tTargets[rtid];

  _viewW = target._desc._width;
  _viewH = target._desc._height;
  _minZRange = 0.0f;
  _maxZRange = 1.0f;
  _w = target._desc._width;
  _h = target._desc._height;

  // replace Engine hdr values with rt
  R2T_PARS_TO_ASSIGN(, target._hdrPars->)

    _tiMode = target._tiPars->_tiMode;

  _r2tBegin = true;
  _r2tRT._atocUsed = _useAToC;
  _useAToC = false;

  for (int i = 0; i < R2TRenderTargetCount; i++)
  {
    R2TTargetDesc &tgt = _r2tTargets[i];
    if (tgt._releaseWanted)
    {
      tgt._releaseWanted = false;
      InvalidateR2TTarget(tgt, true);
    }
  }
}

void EngineDD11::EndR2T(RenderTargetID rtid)
{
  DoAssert(_r2tBegin);
  _r2tBegin = false;

  _minZRange = _r2tRT._minZRange;
  _maxZRange = _r2tRT._maxZRange;
  _viewW = _r2tRT._viewWSaved;
  _viewH = _r2tRT._viewHSaved;

  _w = _r2tRT._wSaved;
  _h = _r2tRT._hSaved;

  // restore GEngine values
  R2T_PARS_TO_ASSIGN(,_r2tRT.)

    // restore original
    _tiMode = _r2tRT._globTIPars._tiMode;

  _clearColor = _r2tRT._clearColor;
  _useAToC = _r2tRT._atocUsed;

}

void EngineDD11::SaveR2TToFile(const RenderTarget& surfToSave) 
{
#if _ENABLE_REPORT
  if (surfToSave._resource.NotNull())
  {
    BString<64> str;
    str.PrintF("%s%p.dds", "RenderTarget", static_cast<void *>(surfToSave._resource.GetRef()));
    D3DX11SaveTextureToFile(GetD3DDeviceRec().GetDeviceContextRef(), surfToSave._resource, D3DX11_IFF_DDS, str.cstr());
  }

  if (_r2tRT._renderTargetS._resource.NotNull())
  {
    BString<64> str;
    str.PrintF("%s%p.dds", "RenderTargetSurf", static_cast<void *>(_r2tRT._renderTargetS._resource.GetRef()));
    D3DX11SaveTextureToFile(GetD3DDeviceRec().GetDeviceContextRef(), _r2tRT._renderTargetS._resource, D3DX11_IFF_DDS, str.cstr());
  }
#endif
}

void EngineDD11::R2TPostProcess(int effect, float deltaT, RenderTargetID rtid)
{
  if (IsR2TValid(rtid))
  {
    R2TTargetDesc &desc = _r2tTargets[rtid];

    // get current hdr settings
    R2T_PARS_TO_ASSIGN(desc._hdrPars->, );

    // gather current
    desc._tiPars->_tiMode = _tiMode;

    if (IsLLRecording())
      CALL_D3D_ENGINE(R2TPostProcess, effect, deltaT, rtid);
  }
}

float EngineDD11::GetLastKnownR2TAvgIntensity(RenderTargetID rtid) const
{
  DoAssert(IsR2TValid(rtid));

  return _r2tTargets[rtid]._hdrPars->_lastKnownAvgIntensity;
}

bool EngineDD11::R2THDRPars::Initialize(EngineDD11 *engine) 
{
  _accomFactor = 1.0f;    
  _lastKnownAvgIntensity = -1.0f;
  _accomFactorPrev = -1.0f;  

  // aperture index
  _rtApertureCurrentR2T = 0;

  // aperture textures
  for (int i = 0; i < 2; i++)  
  {
    // create the texture
    HRESULT err = engine->CreateRenderTarget(1, 1, engine->_apertureFormat, 1, 0, D3D11_USAGE_DEFAULT, true, _rtApertureR2T[i]);
    if (FAILED(err))
    {
      engine->DDError11("Post process aperture texture creation failed", err);
      return false;
    }
  }

  DoAssert(_querySinglePassR2T == NULL);
  _querySinglePassR2T = new QueryManagerSinglePass();
  _querySinglePassR2T->ResetQueries();

  return true;
}

void EngineDD11::R2THDRPars::UnInitialize()
{
  for (int i = 0; i < 2; i++)
  {
    _rtApertureR2T[i].Free();
  }

  if (_querySinglePassR2T)
    delete _querySinglePassR2T;

  _querySinglePassR2T = NULL;
}

bool EngineDD11::R2TTiPars::Initialize(EngineDD11 *engine)
{
  _tiMode = 0;
  _deltaT = 0.0f;
  _maxTemperatureR2T = TempMin;
  ZeroMemory(_temperaturesR2T, sizeof(int) * lenof(_temperaturesR2T));

  DoAssert(_queryR2T == NULL);
  _queryR2T = new QueryManager();

  return true;
}

void EngineDD11::R2TTiPars::UnInitialize()
{
  if (_queryR2T)
    delete _queryR2T;

  _queryR2T = NULL;
}

EngineDD11::R2TColorsPars::R2TColorsPars()
{
  Initialize(NULL);
}

bool EngineDD11::R2TColorsPars::Initialize(EngineDD11 *engine)
{
  _enabled = false;
  _brightness = 1.0f;
  _contrast = 1.0f;
  _offset = 0.0f;
  _lerpColor = Color(0, 0, 0, 0);
  _colorizeColor = HWhite;
  _rgbWeights = Color(R_EYE, G_EYE, B_EYE);

  return true;
}

void EngineDD11::R2TColorsPars::Update(const float *pars, int count)
{
  DoAssert(count == 16);

  _enabled = pars[0] > 0.5f;
  _brightness = pars[1];
  _contrast = pars[2];
  _offset = pars[3];
  _lerpColor = Color(pars[4], pars[5], pars[6], pars[7]);
  _colorizeColor = Color(pars[8], pars[9], pars[10], pars[11]);
  _rgbWeights = Color(pars[12], pars[13], pars[14], pars[15]);
}

void EngineDD11::DoR2TPostProcess(int vMode, float deltaT, RenderTargetID rtid)
{
  DoAssert(IsR2TValid(rtid));

#if !_DEBUG
  DoAssert(!CheckMainThread());
#endif

  R2TTargetDesc &target = _r2tTargets[rtid];

  target._active = true;

  RECT rect = { 0, 0, target._desc._width, target._desc._height };

  if (vMode == 2)
  {
    if (_pp._finalThermal)
    {
      target._tiPars->_deltaT = deltaT;
      target._tiPars->_tiMode = _tiMode;

      _pp._finalThermal->DoDirect(&rect, rtid);
    }
  }
  else
  {
    if (_pp._glow)
    {
      // update params
      target._hdrPars->_deltaTR2T = deltaT;
      target._hdrPars->_nightVisionR2T = vMode == 1;
      target._hdrPars->_eyeAdaptMinR2T = GScene->MainLight()->GetEyeAdaptMin();
      target._hdrPars->_eyeAdaptMaxR2T = GScene->MainLight()->GetEyeAdaptMax();

      _pp._glow->DoDirect(&rect, rtid);
    }

    // post colors effect
    if (_pp._colors)
    {
      if (target._clrPars && target._clrPars->_enabled)
      {
        // set scale 
        target._clrPars->_coeffs[0] = 1.0 / CBState(-1)._proj(0, 0);
        target._clrPars->_coeffs[1] = 1.0 / CBState(-1)._proj(1, 1);
        target._clrPars->_coeffs[2] = CBState(-1)._proj(1, 1) * target._clrPars->_coeffs[0];

        _pp._colors->DoDirect(&rect, rtid);
      }
    }
  }

  target._active = false;

}

void EngineDD11::R2TSetPEColorsPars(RenderTargetID rtid, const float* pars, int count)
{
  DoAssert(IsR2TValid(rtid) && CheckMainThread());

  R2TTargetDesc &target = _r2tTargets[rtid];

  if (target._clrPars.IsNull()) target._clrPars = new R2TColorsPars();

  if (target._clrPars)
  {
    target._clrPars->Update(pars, count);
  }
}


void EngineDD11::DoR2TCopyTarget(ID3D11ShaderResourceView *source, CONST RECT *srcRect, const RenderTarget& dest, bool save)
{

  Assert(_pp._stretchRect);
  Assert(source);
  if (_pp._stretchRect && source)
  {
    _pp._stretchRect->DoDirect(source, srcRect, dest, NULL);
  }
  if (save)
    SaveR2TToFile(dest);
}

void EngineDD11::DoR2TClearTarget(ID3D11RenderTargetView *rtView)
{
  if (rtView)
  {
    Color clearColor(0.0f, 0.0f, 0.0f, 1.0f);
    GetD3DDeviceRec()->ClearRenderTargetView(rtView, Array<float>((float*)&clearColor, 4));
  }
}

void EngineDD11::SetR2TRenderTargets()
{
  DoAssert(_r2tRT._renderTargetSaved.IsNull());
  DoAssert(_r2tRT._renderTargetDepthBufferSurfaceSaved.IsNull());

  _r2tRT._renderTargetSaved = _currRenderTarget;
  _r2tRT._renderTargetDepthBufferSurfaceSaved = _currDepthStencil;

  _r2tRT._minZRangeSaved = _minZRange;
  _r2tRT._maxZRangeSaved = _maxZRange;

  SetRenderTargetAndDepthStencil(
    _r2tRT._renderTargetS,
    _depthStencilS, _minZRange, _maxZRange);

  Clear(false, true, PackedColor(HBlack));

  // note: viewport is reset here, z is set to 0..1 range by the SetRenderTarget call
  _minZRange = 0;
  _maxZRange = 1;

  D3DBeginScene();
}

void EngineDD11::RestoreR2TRenderTargets(RenderTargetID id, bool saveEnable)
{
  R2TTargetDesc &r2tTarget = _r2tTargets[id];

  if (r2tTarget._id != R2TUninitialized)
  {
    RECT rect = { 0, 0, r2tTarget._desc._width, r2tTarget._desc._height };

    if (_r2tRT._renderTargetS._rtView)
      CALL_D3D_ENGINE(R2TCopyTarget, _r2tRT._renderTargetS._srView , &rect, r2tTarget._desc._texture, saveEnable);

  }

  SetRenderTargetAndDepthStencil(
    _r2tRT._renderTargetSaved, _r2tRT._renderTargetDepthBufferSurfaceSaved,_r2tRT._minZRangeSaved, _r2tRT._maxZRangeSaved );

  _r2tRT._renderTargetSaved.Free();
  _r2tRT._renderTargetDepthBufferSurfaceSaved.Free();
  _clearColor = _r2tRT._clearColor;

  D3DEndScene();
}

#endif //_HELISIM_RENDER_TO_TEXTURE


bool EngineDD11::SetVerticalSynchronization(bool vsync)
{
  if (_vsync != vsync) 
  {
    _vsync = vsync;
    return true;
  }

  return false;
}

void EngineDD11::SetVSync(bool vsync)
{
  if (SetVerticalSynchronization(vsync))
  {
    ApplyWanted();
  }
}

#if _ARMA3_CAUSTICS
void EngineDD11::BeginCausticsRendering()
{
  if (!UsePredication())
    SetCommandBuffersToUndefinedState(); //in case we are not using predication, we have to reset state
  _renderingMode = RMCaustics;
}

void EngineDD11::EndCausticsRendering()
{
  if (!UsePredication())
    SetCommandBuffersToUndefinedState(); //in case we are not using predication, we have to reset state
  _renderingMode = RMCommon;
}
#endif

#if _ARMA3_NEW_LIGHTING_CFG
Color EngineDD11::ComputeBackReflection(const Color& diffuse)
{
  if (GScene)
  {
    if (GLandscape && GLandscape->UseNewLandLighting())
    {
      return GScene->GetCurrentLighting()._bidirect * diffuse;
    }
    else if (GScene->MainLight())
    {
      return GScene->MainLight()->GroundReflection()*diffuse;
    }
  }
  return Color(0,0,0,0);
}

Color EngineDD11::ComputeGroundReflection(const Color& lightAmbient, const Color& matAmbient)
{
  if (GScene)
  {
    if (GLandscape && GLandscape->UseNewLandLighting())
    {
      return GScene->GetCurrentLighting()._groundReflection * GetAccomodateEye() * matAmbient;
    }
    else if (GScene->MainLight())
    {
      return GScene->MainLight()->GroundReflection()*lightAmbient*matAmbient;
    }
  }
  return Color(0,0,0,0);
}
#endif //#if _ARMA3_NEW_LIGHTING_CFG


#if _ARMA3_DX11_PROFILING
void  EngineDD11::ProfileNextFrame()
{
  _profileInfoPrev = _profileInfoCur;
  _profileInfoCur.Clear();

  _currentRenderState._shaderConstants.ProfileNextFrame();
}

void EngineDD11::ProfileReport()
{
  // we report the previous state, because it is always valid

  LogF("=========================================================");
  LogF("Engine profile report");
  LogF("Number of setups: %d", _profileInfoPrev._profileNumSetups);
  _currentRenderState._shaderConstants.ProfileReport();  
  LogF("=========================================================");
}

#endif

} //namespace DX11

#if !_DISABLE_GUI
Engine *CreateEngineD3D11(CreateEnginePars &pars)
{
  return DX11::EngineDD11::CreateEngine(pars);
}
#endif

TypeIsMovable(DX11::PPPriorities)

#endif //!_DISABLE_GUI
#endif //_ENABLE_DX11

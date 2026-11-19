//# pragma optimize( "", off ) 
#include "../wpch.hpp"

#if !_DISABLE_GUI && !defined _SERVER
#include "engdd9.hpp"
#include "postProcess.hpp"

#include <El/Statistics/statistics.hpp>
#include <El/common/perfLog.hpp>
#include <El/common/randomGen.hpp>
#include <El/QStream/qbstream.hpp>
#include <El/Multicore/multicore.hpp>
#include <Es/ReportStack/reportStack.hpp>
#include <El/Evaluator/express.hpp>

#include "txtD3D9.hpp"
#include "../Shape/poly.hpp"
#include "../tlVertex.hpp"
#include "../txtPreload.hpp"
#include "../Shape/shape.hpp"
#include "../Shape/material.hpp"
#include "../clip2D.hpp"
#include "../global.hpp"
#include "../keyInput.hpp"
#include "../dikCodes.h"
#include "../landImpl.hpp"
#include <El/Multicore/jobs.hpp>

#if _ARMA3_SIMUL_WEATHER
#include "../SimulWeather/SimulWeatherDX9.hpp"
#endif

/*!
True = stencil is always on, false = stencil is on when needed. This is a test part of INTEL report,
I need to test the speed on INTEL system as on normal GPU the speed increase is very small (36 vs 35.8).
INTEL system has separated stencil and depth buffer even on format D24S8 and stencil is quite expensive.
*/
bool FULL_STENCIL = false;

/*!
vertex for heat haze effect
*/
struct HeatHazeVertex
{
	float pos[4]; 
	float t0[3]; 
};


void ReportMemoryStatusWithVRAM()
{
  void ReportMemoryStatus();
  ReportMemoryStatus();
  const char *err = GEngineDD->GetMemErrorStatus();
  RptF("%s",err);
}


void PrintVMMap(int extended, const char *title=NULL);

/// when PS blend is active, pixel shader is assumed to perform source alpha multiplitation
#define PS_BLEND 1

#if PS_BLEND
/**
When only (1-alpha)*dst blending is used for alpha blending, the shader can control add/blend modes
*/
# define D3DBLEND_SRCDEF D3DBLEND_ONE
#else
# define D3DBLEND_SRCDEF D3DBLEND_SRCALPHA
#endif


#if _ENABLE_COMPILED_SHADER_CACHE
#include "ShaderCompileCache.h"
#endif

static const char HLSLCacheDir[] = "bin\\";

/// used for Shader Model 3.0 shaders
#define RelShaderPath_3_0 "ShaderSources_3_0\\"

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
#if _ARMA3_SIMUL_WEATHER
static RString VSSimulWeatherFile;
#endif


/// informative - dependency information used for the cache
static RString ShaderInclude;
//! File common for both the shaders and program
static RString HLSLPRGCommon;

static AutoArray<RString> SourcesPP;
static AutoArray<RString> SourcesPS;
static AutoArray<RString> SourcesVS;

#if _ENABLE_CHEATS
/// sometimes we want to conform to API requirements even when debug API is not used
static bool StrictAPI = false;
#else
static const bool StrictAPI = false;
#endif

static void InitShaderDirs()
{
  BString<256> shaderDir = __FILE__; // should be something like PROJECT_ROOT "\\d3d9\\engdd9.cpp"
  const char *nam = GetFilenameExt(shaderDir);
  if (nam)
  {
    // replace the filename part with RelShaderPath_3_0
    strcpy(LString(shaderDir).Mid(nam),RelShaderPath_3_0);
  }
  else
  {
    // no failure expected, but if there was one, try to recover
    Fail("Unexpected file path");
    strcpy(shaderDir,".\\" RelShaderPath_3_0);
  }
  
  HLSLDir = shaderDir.cstr();
  
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

#if defined(_X360SHADERGEN)
//! Shader file names
static const char *SBTFileName[] =
{
  "Shaders",
  "Shaders_nV", // nVidia
  "Shaders", // nVidiaZINT
  "Shaders",
  "Shaders",
  "Shaders",
};
#else
//! Shader file names
static const char *SBTFileName[] =  
{
  "Shaders_Def",
  "Shaders_nV", // nVidia
  "Shaders_Def", // nVidiaZINT
  "Shaders_Def",
  "Shaders_Def",
  "Shaders_Def",
};
#endif
namespace Unique1
{
  COMPILETIME_COMPARE(lenof(SBTFileName), SBTCount)
}

static RString ShaderCacheName(SBTechnique sbt, bool sm2Used, RString section)
{
#if _VBS3_SM2
  if (sm2Used)
  {
    return SBTFileName[sbt] + section + RString("_SM2");
  }
  else
#endif
  {
    return SBTFileName[sbt]+section;
  }
}

#if !defined(_X360SHADERGEN)
# include <winuser.h>
# if _DEBUG
#   define NoSysLockFlag D3DLOCK_NOSYSLOCK
# else
// No sys lock is usually slower
#   define NoSysLockFlag 0
# endif
# define NoClipUsageFlag D3DUSAGE_DONOTCLIP
#else
# define NoSysLockFlag 0
# define NoClipUsageFlag 0
# include <XGraphics.h>
#endif

#if _DEBUG
// when VERIFY_INDICES enabled, index ranges are checked before calling DIP
# define VERIFY_INDICES 0
#elif _ENABLE_REPORT
# define VERIFY_INDICES 0
#else
# define VERIFY_INDICES 0
#endif

#if VERIFY_INDICES
# define WRITEONLYIB 0
#else
# define WRITEONLYIB D3DUSAGE_WRITEONLY
#endif

#if _DEBUG
# define WRITEONLYVB D3DUSAGE_WRITEONLY
#else
# define WRITEONLYVB D3DUSAGE_WRITEONLY
#endif

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
  return (ia<<24)|(ir<<16)|(ig<<8)|ib;
}


#define LOG_LOCKS 0

// TODOX360: check what size is really needed for dynamic buffers
const int MaxShapeVertices = 4*1024;
const int MaxStaticVertexBufferSizeB = 0; /** zero means no coalescing */

#define MaxVertexCount(type) (MaxStaticVertexBufferSizeB/sizeof(type))


// source vertex buffer format
//#define MYSFVF (D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1)

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

/// Influence density of trees by adjusting the final alpha - increase density, AToC otherwise does silly things
/**
half alpha = pow(diffuse.a*PSC_TreeAdjust.x,PSC_TreeAdjust.y)*PSC_TreeAdjust.z + PSC_TreeAdjust.w;
|| note: using x or z is redundant 
half alpha = pow(diffuse.a,PSC_TreeAdjust.y)*pow(PSC_TreeAdjust.x,PSC_TreeAdjust.y)*PSC_TreeAdjust.z + PSC_TreeAdjust.w;
*/
static float TreeAdjustPars[4] = {1,1,1,0};
/// increase density, AToC otherwise does silly things
static float TreeAdjustParsAToC[4] = {1.45,0.75,1.0,0.0}; // used for Tree Adv shaders
static float TreeAdjustParsAToCExt[4] = {1.0,0.4,1.5,0}; // used for PSTreeAToC shader

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


#if _ENABLE_CHEATS

#include <El/Evaluator/expressImpl.hpp>
#include <El/Modules/modules.hpp>

#define ENG_DIAG_ENABLE(type,prefix,XX) \
  XX(type, prefix, NoFilterVSC) /* do not filter vertex shader constants */\
  XX(type, prefix, NoFilterRS) /* do not filter render states */\
  XX(type, prefix, NoSetupPS) /* do not set pixel shaders and PS contants */\


#ifndef DECL_ENUM_ENG_DIAG_ENABLE
#define DECL_ENUM_ENG_DIAG_ENABLE
DECL_ENUM(EngDiagEnable)
#endif
DECLARE_DEFINE_ENUM(EngDiagEnable,EDE,ENG_DIAG_ENABLE)

static int EngDiagMode;
static int LimitVSCCount=-1;
int LimitTriCount=-1;
int LimitDIPCount=-1;
static int LimitSecCount=-1;
bool ForceStaticVB=false;

#define CHECK_ENG_DIAG(x) ( (EngDiagMode&(1<<(x)))!=0 )

static void ReportEngDiagMode(int mode)
{
  int mask = 1<<mode;
  const char *status = (EngDiagMode&mask)!=0 ? "On" : "Off";
  const char *name = FindEnumName(EngDiagEnable(mode));
  GlobalShowMessage(2000,"Engine diag mode %s %s",name,status);
}

static GameValue SetEngDiagEnable(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  const char *modeStr = (RString)oper1;
  bool onOff = oper2;
  int modeMask = ~0;
  int mode = -1;
  if (strcmpi(modeStr,"all"))
  {
    mode = GetEnumValue<EngDiagEnable>(modeStr);
    if (mode==-1) return NOTHING;
    modeMask = 1<<mode;
  }

  if (onOff) EngDiagMode |= modeMask;
  else EngDiagMode &= ~modeMask;
  if (mode>=0)
  {
    ReportEngDiagMode(mode);
  }
  return NOTHING;
}

static GameValue SetEngDiagToggle(const GameState *state, GameValuePar oper1)
{
  const char *modeStr = (RString)oper1;
  int mode = GetEnumValue<EngDiagEnable>(modeStr);
  if (mode==-1) return NOTHING;
  int modeMask = 1<<mode;

  EngDiagMode ^= modeMask;

  ReportEngDiagMode(mode);

  return NOTHING;
}

static GameValue SetEngLimitVSC(const GameState *state, GameValuePar oper1)
{
  float limit = oper1;
  LimitVSCCount = toInt(limit);
  return NOTHING;
}

static GameValue SetEngLimitTri(const GameState *state, GameValuePar oper1)
{
  float limit = oper1;
  LimitTriCount = toInt(limit);
  return NOTHING;
}

/// limit DIP in the scene
static GameValue SetEngLimitDIP(const GameState *state, GameValuePar oper1)
{
  float limit = oper1;
  LimitDIPCount = toInt(limit);
  return NOTHING;
}

/// limit DIP in one Shape
static GameValue SetEngLimitSec(const GameState *state, GameValuePar oper1)
{
  float limit = oper1;
  LimitSecCount = toInt(limit);
  return NOTHING;
}



static GameValue SetEngViewport(const GameState *state, GameValuePar oper1)
{
  float scale = oper1;
  GEngineDD->SetLimitedViewport(scale);
  return NOTHING;
}

static GameValue ClearVMStress(const GameState *state)
{
  GEngineDD->TextBankDD()->ClearVMStress();
  return NOTHING;
}


static GameValue ClearTexStress(const GameState *state)
{
  GEngineDD->TextBankDD()->ClearTexStress();
  return NOTHING;
}

static GameValue FlushTexMem(const GameState *state)
{
  GEngineDD->TextBankDD()->ReleaseAllTextures();
  return NOTHING;
}

static GameValue FlushVBMem(const GameState *state)
{
  GEngineDD->ReleaseAllVertexBuffers();
  return NOTHING;
}

static GameValue FlushTexMemPrior(const GameState *state, GameValuePar oper1)
{
  float limit = oper1;
  GEngineDD->TextBankDD()->ReleaseTexturesWithPerformance(toInt(limit));
  return NOTHING;
}

static GameValue UseStaticVB(const GameState *state, GameValuePar oper1)
{
  ForceStaticVB = oper1;
  GEngineDD->ReleaseAllVertexBuffers();
  return NOTHING;
}

static GameValue ReloadVS(const GameState *state, GameValuePar oper1)
{
  const char *shader = (RString)oper1;
  VertexShaderID vs = GetEnumValue<VertexShaderID>(shader);
  if (vs==-1) return NOTHING;
  GEngineDD->ReloadVertexShader(vs);
  return NOTHING;
}

static GameValue ReloadPS(const GameState *state, GameValuePar oper1)
{
  const char *shader = (RString)oper1;
  // check all pixel shader which match the mask given in oper1
  for(int ps=0; ps<NPixelShaderID; ps++)
  {
    PixelShaderID psid = PixelShaderID(ps);
    RStringB name = FindEnumName(psid);
    if (Matches(name,shader))
    {
      GEngineDD->ReloadPixelShader(psid);
    }
  }
  return NOTHING;
}

static GameValue AdjustTree(const GameState *state, GameValuePar oper1)
{
  const GameArrayType &array = oper1;
  if (array.Size() > 4)
  {
    if (state) state->SetError(EvalDim,array.Size(),2);
    return GameValue();
  }
  for (int i=0; i<array.Size(); i++)
  {
    if (array[i].GetType() != GameScalar)
    {
      if (state) state->TypeError(GameScalar,array[i].GetType());
      return GameValue();
    }
  }
  for (int i=0; i<array.Size(); i++)
  {
    //(GEngineDD->_useAToC ? TreeAdjustParsAToC : TreeAdjustPars)[i] = array[i];
    TreeAdjustParsAToC[i] = array[i];
    //TreeAdjustParsAToC : TreeAdjustPars)[i] = array[i];
  }
  return GameValue();
}

static GameValue AdjustTreeExt(const GameState *state, GameValuePar oper1)
{
  const GameArrayType &array = oper1;
  if (array.Size() > 4)
  {
    if (state) state->SetError(EvalDim,array.Size(),2);
    return GameValue();
  }
  for (int i=0; i<array.Size(); i++)
  {
    if (array[i].GetType() != GameScalar)
    {
      if (state) state->TypeError(GameScalar,array[i].GetType());
      return GameValue();
    }
  }
  for (int i=0; i<array.Size(); i++)
  {
    //(GEngineDD->_useAToC ? TreeAdjustParsAToC : TreeAdjustPars)[i] = array[i];
    TreeAdjustParsAToCExt[i] = array[i];
    //TreeAdjustParsAToC : TreeAdjustPars)[i] = array[i];
  }
  return GameValue();
}


/// create one stress test memory region
static GameValue AddVMStress(const GameState *state, GameValuePar oper1)
{
  float stress = oper1;
  GEngineDD->TextBankDD()->AddVMStress(toLargeInt(stress));
  return NOTHING;
}

/// create stress test memory regions until VM space gets under given limit
static GameValue AddVMStressLimit(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  size_t itemSize = toLargeInt(safe_cast<float>(oper1));
  size_t limit = toLargeInt(safe_cast<float>(oper2));
  

  for(;;)
  {
      MEMORYSTATUSEX memstat;
      memstat.dwLength = sizeof(memstat);
      GlobalMemoryStatusEx(&memstat);
      long long vmFree = memstat.ullAvailVirtual;
    if (vmFree<=limit) break;
    if (!GEngineDD->TextBankDD()->AddVMStress(itemSize)) break;
  }
  return NOTHING;
}


/// create one stress test texture 
static GameValue AddTexStress(const GameState *state, GameValuePar oper1)
{
  float stress = oper1;
  GEngineDD->TextBankDD()->AddTexStress(toInt(stress));
  return NOTHING;
}

/// create one stress test texture with RT usage
static GameValue AddRTStress(const GameState *state, GameValuePar oper1)
{
  float stress = oper1;
  GEngineDD->TextBankDD()->AddTexStress(toInt(stress),true);
  return NOTHING;
}

static bool GetStressSize(const GameState *state, GameValuePar oper1, int &iCount, int &iSize)
{
  const GameArrayType &array = oper1;
  if (array.Size() != 2)
  {
    if (state) state->SetError(EvalDim,array.Size(),2);
    return false;
  }
  if (array[0].GetType() != GameScalar)
  {
    if (state) state->TypeError(GameScalar,array[0].GetType());
    return false;
  }
  if (array[1].GetType() != GameScalar)
  {
    if (state) state->TypeError(GameScalar,array[1].GetType());
    return false;
  }
  float count = array[0];
  float size = array[1];
  iSize = toInt(size);
  iCount = toInt(count);
  return true;
}

/// create multiple stress test textures
static GameValue AddTexStressAr(const GameState *state, GameValuePar oper1)
{
  int size,count;
  if (!GetStressSize(state,oper1,count,size)) 
  {
    return NOTHING;
  }
  for (int i=0; i<count; i++)
  {
    GEngineDD->TextBankDD()->AddTexStress(size);
  }
  return NOTHING;
}

/// create one multiple test textures with RT usage
static GameValue AddRTStressAr(const GameState *state, GameValuePar oper1)
{
  int size,count;
  if (!GetStressSize(state,oper1,count,size)) 
  {
    return NOTHING;
  }
  for (int i=0; i<count; i++)
  {
    GEngineDD->TextBankDD()->AddTexStress(size,true);
  }
  return NOTHING;
}

/// change texture stress test mode
static GameValue TexStressMode(const GameState *state, GameValuePar oper1)
{
  float stress = oper1;
  GEngineDD->TextBankDD()->SetStressMode(toInt(stress));
  return NOTHING;
}

/// change texture stress test mode
static GameValue TexMemLimit(const GameState *state, GameValuePar oper1)
{
  float value = oper1;
  GEngineDD->TextBankDD()->SetTextureMemoryDiag(toLargeInt(value));
  return NOTHING;
}

static const GameNular DD9Nular[]=
{
  GameNular(GameNothing,"diag_engStressTexClear",ClearTexStress TODO_NULAR_DOCUMENTATION),
  GameNular(GameNothing,"diag_engStressVMClear",ClearVMStress TODO_NULAR_DOCUMENTATION),
  GameNular(GameNothing,"diag_engFlushTex",FlushTexMem TODO_NULAR_DOCUMENTATION),
  GameNular(GameNothing,"diag_engFlushVB",FlushVBMem TODO_NULAR_DOCUMENTATION),
};

static const GameFunction DD9Unary[]=
{
  GameFunction(GameNothing,"diag_engToggle",SetEngDiagToggle,GameString TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engLimitVSC",SetEngLimitVSC,GameScalar TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engLimitTri",SetEngLimitTri,GameScalar TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engLimitDIP",SetEngLimitDIP,GameScalar TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engLimitSec",SetEngLimitSec,GameScalar TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engViewport",SetEngViewport,GameScalar TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engStressTex",AddTexStress,GameScalar TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engStressVM",AddVMStress,GameScalar TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engStressRT",AddRTStress,GameScalar TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engStressTex",AddTexStressAr,GameArray TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engStressRT",AddRTStressAr,GameArray TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engStressTexMode",TexStressMode,GameScalar TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engFlushTexPerf",FlushTexMemPrior,GameScalar TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engUseStaticVB",UseStaticVB,GameBool TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engReloadVS",ReloadVS,GameString TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engReloadPS",ReloadPS,GameString TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engAdjustTree",AdjustTree,GameArray TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engAdjustTreeExt",AdjustTreeExt,GameArray TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_engTexMemLimit",TexMemLimit,GameScalar TODO_FUNCTION_DOCUMENTATION),
};
static const GameOperator DD9Binary[]=
{
  GameOperator(GameNothing,"diag_engEnable",function,SetEngDiagEnable,GameString,GameBool TODO_OPERATOR_DOCUMENTATION),
  GameOperator(GameNothing,"diag_engStressVM",function,AddVMStressLimit,GameScalar,GameScalar TODO_OPERATOR_DOCUMENTATION),
};

INIT_MODULE(GameStateDD9, 3)
{
  GGameState.NewOperators(DD9Binary,lenof(DD9Binary));
  GGameState.NewFunctions(DD9Unary,lenof(DD9Unary));
  GGameState.NewNularOps(DD9Nular,lenof(DD9Nular));
};

#else

#define CHECK_ENG_DIAG(x) false
const int LimitVSCCount=-1;
const int LimitTriCount=-1;
const int LimitDIPCount=-1;
const int LimitSecCount=-1;
#endif

#if _ENABLE_PERFLOG && _ENABLE_CHEATS && (_DEBUG || _PROFILE)
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

// track patching in Debug (All) or Profile (PC only, not Xbox)
#if _DEBUG || _PROFILE 
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
    RString vsName = GEngineDD->GetVSName(id._vs);
    RString vdName = GEngineDD->GetVDName(id._vd);
    RString psName = GEngineDD->GetPSName(id._ps);
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

extern int D3DAdapter;
extern bool WinXP;

EngineDD9 *EngineDD9::SetRenderStateTemp::_engine;
EngineDD9 *EngineDD9::SetSamplerStateTemp::_engine;
EngineDD9 *EngineDD9::PushInternall::_engine;
EngineDD9 *EngineDD9::PushVertexShaderConstant::_engine;
EngineDD9 *EngineDD9::PushPixelShaderConstant::_engine;
EngineDD9 *EngineDD9::PushTextureState::_engine;
EngineDD9 *EngineDD9::ISpecialEffect::_engine;

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

#define CALL_DEVICE_0(Name)  _dev->Name()
#define CALL_DEVICE_1(Name)  _dev->Name(args.arg1)
#define CALL_DEVICE_2(Name)  _dev->Name(args.arg1,args.arg2)
#define CALL_DEVICE_3(Name)  _dev->Name(args.arg1,args.arg2,args.arg3)
#define CALL_DEVICE_4(Name)  _dev->Name(args.arg1,args.arg2,args.arg3,args.arg4)
#define CALL_DEVICE_5(Name)  _dev->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5)
#define CALL_DEVICE_6(Name)  _dev->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5,args.arg6)
#define CALL_DEVICE_7(Name)  _dev->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5,args.arg6,args.arg7)
#define CALL_DEVICE_8(Name)  _dev->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5,args.arg6,args.arg7,args.arg8)
#define CALL_DEVICE_9(Name)  _dev->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5,args.arg6,args.arg7,args.arg8,args.arg9)
#define CALL_DEVICE_10(Name) _dev->Name(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5,args.arg6,args.arg7,args.arg8,args.arg9,args.arg10)


#define CALL_DEVICE(Name,NArgs) LOG_DEVICE_N(Name,NArgs);CALL_DEVICE_##NArgs(Name)

Void D3DDeviceServant::DrawIndexedPrimitive(const DrawIndexedPrimitiveAllArgs &args)
{
  if (_vbOK && _ibOK)
  {
  FlushGPU(_dev);
  CALL_DEVICE(DrawIndexedPrimitive,6);
  FlushGPU(_dev);
  }
  return Void();
}

Void D3DDeviceServant::DrawPrimitive(const DrawPrimitiveAllArgs &args)
{
  if (_vbOK)
  {
  FlushGPU(_dev);
  UINT numPrimitives = args.arg3;
#if _ARMA3_VERTEX_BUFFER_EX
  if (_lastStreamSourceNumPrimitives != VertexBuffer9Ex::UndefinedValue)
  {
    // override number of primitives - value was specified later
    DoAssert(_lastStreamSourceNumPrimitives >= 1);
    if (_lastStreamSourceNumPrimitives >= 1)
      numPrimitives = _lastStreamSourceNumPrimitives;
  }
#endif

  _dev->DrawPrimitive(args.arg1, args.arg2, numPrimitives);
  FlushGPU(_dev);
  }
  return Void();
}
Void D3DDeviceServant::DrawPrimitiveUP(const DrawPrimitiveUPAllArgs &args)
{
  FlushGPU(_dev);
  LOG_DEVICE(DrawPrimitiveUP);
  _dev->DrawPrimitiveUP(args.arg1,args.arg2,args.arg3.Data(),args.arg4);
  FlushGPU(_dev);
  return Void();
}
Void D3DDeviceServant::SetSamplerState(const SetSamplerStateAllArgs &args)
{
  CALL_DEVICE(SetSamplerState,3);
  return Void();
}
Void D3DDeviceServant::SetRenderState(const SetRenderStateAllArgs &args)
{
  CALL_DEVICE(SetRenderState,2);
  return Void();
}
Void D3DDeviceServant::SetTextureHandle(const SetTextureHandleAllArgs &args)
{
  #if ENCAPSULATE_TEXTURE_REF
  LOG_DEVICE(SetTexture);
  _dev->SetTexture(args.arg1,args.arg2 ? args.arg2->_surface.GetRef() : NULL);
  return Void();
  #else
  LOG_DEVICE(SetTexture);
  //LOG_DEVICE_N(SetTexture,2);
  _dev->SetTexture(args.arg1,args.arg2);
  return Void();
  #endif
}
Void D3DDeviceServant::SetTexture(const SetTextureAllArgs &args)
{
  CALL_DEVICE(SetTexture,2);
  return Void();
}

Void D3DDeviceServant::SetIndices(const SetIndicesAllArgs &args)
{
  LOG_DEVICE_N(SetIndices, 1);
  IDirect3DIndexBuffer9 *buffer = args.arg1.IsNull() ? (IDirect3DIndexBuffer9*) NULL : args.arg1.GetResult();
  _ibOK = buffer != NULL; // ignore DrawIndexedPrimitive when failed
  _dev->SetIndices(buffer);
  return Void();
}

Void D3DDeviceServant::SetStreamSource(const SetStreamSourceAllArgs &args)
{
  LOG_DEVICE_N(SetStreamSource, 4);
  // get result first (so that buffer is created)
#if _ARMA3_VERTEX_BUFFER_EX
  IDirect3DVertexBuffer9 *buffer = NULL;
	if (!args.arg2.IsNull())
		buffer = args.arg2.GetResult().GetBuffer();
#else
  IDirect3DVertexBuffer9 *buffer = args.arg2.GetResult();
#endif

	_vbOK = buffer != NULL; // ignore DrawIndexedPrimitive and DrawPrimitive when failed

#if _ARMA3_VERTEX_BUFFER_EX
  _lastStreamSourceNumPrimitives = buffer ? args.arg2.GetResult().GetNumPrimitivesToDraw() : 0;
  DoAssert(_lastStreamSourceNumPrimitives == VertexBuffer9Ex::UndefinedValue || _lastStreamSourceNumPrimitives >= 1);
#endif
  _dev->SetStreamSource(args.arg1, buffer, args.arg3, args.arg4);
  return Void();
}

Void D3DDeviceServant::SetVertexDeclaration(const SetVertexDeclarationAllArgs &args)
{
  CALL_DEVICE(SetVertexDeclaration,1);
  return Void();
}

Void D3DDeviceServant::SetVertexShader(const SetVertexShaderAllArgs &args)
{
  CALL_DEVICE(SetVertexShader,1);
  return Void();
}
Void D3DDeviceServant::SetPixelShader(const SetPixelShaderAllArgs &args)
{
  CALL_DEVICE(SetPixelShader,1);
  return Void();
}

Void D3DDeviceServant::SetPixelShaderConstantsF(const SetPixelShaderConstantsFAllArgs &args)
{
  LOG_DEVICE_N_EX(
    SetPixelShaderConstantsF,2,
    cc_cast(GEngineDD->GetPSConstantFDebugName(args.arg1)),
    cc_cast(Format("%d %g,%g,%g,%g",args.arg2.num,args.arg2.val[0][0],args.arg2.val[0][1],args.arg2.val[0][2],args.arg2.val[0][3]))
  );
  _dev->SetPixelShaderConstantF(args.arg1,(float *)args.arg2.val,args.arg2.num);
  return Void();
}
Void D3DDeviceServant::SetVertexShaderConstantsF(const SetVertexShaderConstantsFAllArgs &args)
{
  LOG_DEVICE_N_EX(
    SetVertexShaderConstantsF,2,
    cc_cast(GEngineDD->GetVSConstantFDebugName(args.arg1)),
    cc_cast(Format("%d %g,%g,%g,%g",args.arg2.num,args.arg2.val[0][0],args.arg2.val[0][1],args.arg2.val[0][2],args.arg2.val[0][3]))
  );
  _dev->SetVertexShaderConstantF(args.arg1,(float *)args.arg2.val,args.arg2.num);
  return Void();
}
Void D3DDeviceServant::SetVertexShaderConstantF(const SetVertexShaderConstantFAllArgs &args)
{
  LOG_DEVICE_N_EX(SetVertexShaderConstantF,2,cc_cast(GEngineDD->GetVSConstantFDebugName(args.arg1)),cc_cast(Format("%d",args.arg2.Size())));
  _dev->SetVertexShaderConstantF(args.arg1,(float *)args.arg2.Data(),args.arg2.Size());
  return Void();
}
Void D3DDeviceServant::SetPixelShaderConstantsB(const SetPixelShaderConstantsBAllArgs &args)
{
  LOG_DEVICE_N_EX(SetPixelShaderConstantsB,2,cc_cast(GEngineDD->GetPSConstantBDebugName(args.arg1)),cc_cast(Format("%d",args.arg2.num)));
  _dev->SetPixelShaderConstantB(args.arg1,args.arg2.val,args.arg2.num);
  return Void();
}
Void D3DDeviceServant::SetVertexShaderConstantsB(const SetVertexShaderConstantsBAllArgs &args)
{
  LOG_DEVICE_N_EX(SetVertexShaderConstantsB,2,cc_cast(GEngineDD->GetVSConstantBDebugName(args.arg1)),cc_cast(Format("%d",args.arg2.num)));
  _dev->SetVertexShaderConstantB(args.arg1,args.arg2.val,args.arg2.num);
  return Void();
}
Void D3DDeviceServant::SetPixelShaderConstantsI(const SetPixelShaderConstantsIAllArgs &args)
{
  Assert(args.arg1>=0);
  Assert(args.arg1+args.arg2.num<=256);
  Assert(CheckValidIntegerConstant(args.arg2.val,args.arg2.num));
  LOG_DEVICE_N_EX(SetPixelShaderConstantsI,2,cc_cast(GEngineDD->GetPSConstantIDebugName(args.arg1)),cc_cast(Format("%d",args.arg2.num)));
  _dev->SetPixelShaderConstantI(args.arg1,reinterpret_cast<const int *>(args.arg2.val),args.arg2.num);
  return Void();
}
Void D3DDeviceServant::SetVertexShaderConstantsI(const SetVertexShaderConstantsIAllArgs &args)
{
  Assert(CheckValidIntegerConstant(args.arg2.val,args.arg2.num));
  LOG_DEVICE_N_EX(SetVertexShaderConstantsI,2,cc_cast(GEngineDD->GetVSConstantIDebugName(args.arg1)),cc_cast(Format("%d",args.arg2.num)));
  _dev->SetVertexShaderConstantI(args.arg1,reinterpret_cast<const int *>(args.arg2.val),args.arg2.num);
  return Void();
}

Void D3DDeviceServant::SetRenderTarget(const SetRenderTargetAllArgs &args)
{
  // clear the log
  //if (Glob.config._renderThread) RESET_LOG_DEVICE();
  CALL_DEVICE(SetRenderTarget,2);
  return Void();
}

Void D3DDeviceServant::SetDepthStencilSurface(const SetDepthStencilSurfaceAllArgs &args)
{
  CALL_DEVICE(SetDepthStencilSurface,1);
  return Void();
}

Void D3DDeviceServant::Clear(const ClearAllArgs &args)
{
  FlushGPU(_dev);
  CALL_DEVICE(Clear,6);
  FlushGPU(_dev);
  return Void();
}

Void D3DDeviceServant::SetViewport(const SetViewportAllArgs &args)
{
  LOG_DEVICE_N(SetViewport,1);
  _dev->SetViewport(&args.arg1);
  return Void();
}

Void D3DDeviceServant::SetCursorPosition(const SetCursorPositionAllArgs &args)
{
  _dev->SetCursorPosition(args.arg1, args.arg2, args.arg3);
  return Void();
}

Void D3DDeviceServant::BeginScene(const BeginSceneAllArgs &args)
{
  CALL_DEVICE(BeginScene,0);
  return Void();
}
Void D3DDeviceServant::EndScene(const EndSceneAllArgs &args)
{
  CALL_DEVICE(EndScene,0);
  return Void();
}

Void D3DDeviceServant::ClearF(const ClearFAllArgs &args)
{
  return Void();
}

Void D3DDeviceServant::Resolve(const ResolveAllArgs &args) 
{
  return Void();
}

Void D3DDeviceServant::SetPredication(const SetPredicationAllArgs &args) 
{
  return Void();
}
Void D3DDeviceServant::GpuDisownAll(const GpuDisownAllAllArgs &args) 
{
  return Void();
}

Void D3DDeviceServant::BGResourceCleanUp(const BGResourceCleanUpAllArgs &args)
{
  //TrapFor0x8000Scope lock("D3DDeviceServant::BGResourceCleanUp");
  #if SAFE_RESOURCE_UNSET
    Device_UnsetAllDiscardableResources(_dev);
    // we can release the resource only after they are unset from the device
    GEngineDD->BGReleaseResourcesAndTextures();
  #endif
  return Void();
}

bool EngineDD9::CheckAllDiscardableResourcesUnset() const
{
  for (int cb=0; cb<CBStateCount(); cb++)
  {
    for (int i = 0; i < lenof(_cbState[cb]._lastHandle); i++)
    {
      if (_cbState[cb]._lastHandle[i] != NULL) return false;
    }
    if (_cbState[cb]._vBufferLast.NotNull()) return false;
    if (_cbState[cb]._iBufferLast.NotNull()) return false;
  }
  return true;
}


void EngineDD9::UnsetAllDiscardableResourcesFromCBState()
{
  AssertLowLevelThreadAccess();
  // make sure we are not referencing any old textures any more
  for (int cb=0; cb<CBStateCount(); cb++)
  {
    RendState &cbState = _cbState[cb];
    for (int i = 0; i < lenof(cbState._lastHandle); i++)
    {
      cbState._lastHandle[i] = NULL;
    }
    cbState._vBufferLast = VertexStaticData9::BufferFuture(VertexStaticData9::BufferFuture::Null);
    cbState._iBufferLast = IndexStaticData9::BufferFuture(IndexStaticData9::BufferFuture::Null);
  }
}



void EngineDD9::UnsetAllDiscardableResources()
{
  //TrapFor0x8000Scope lock("EngineDD9::UnsetAllDiscardableResources");
  // no HL or CB recording must be active at this point - we need to be sure we own all the device states
  Assert(!IsInCBScope());
  Assert(_backgroundD3D!=RecHighLevel);
  UnsetAllDiscardableResourcesFromCBState();
  #if SAFE_RESOURCE_UNSET
  // when needed, perform the resource cleanup
  Device_UnsetAllDiscardableResources(_d3DDevice.GetRefForCB());
  // we can release the resource only after they are unset from the device
  #endif
}

#if SAFE_RESOURCE_UNSET
void Device_UnsetAllDiscardableResources(IDirect3DDevice9 *dev)
{
  if (!dev) return; // when not initialized yet, no need to unset
  // unset all textures on normal texture stages
  for (int i=0; i<EngineDD9::UsableSampleStages; i++)
  {
    dev->SetTexture(i,NULL);
  }
  // unset IB
  dev->SetIndices(NULL);
  // unset VB
  dev->SetStreamSource(0,NULL,0,0);
}


#endif

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

StoredRefContainer EngineDDBGStoredRefs;

void EngineDD9::BGAddRefToRelease(Texture *item)
{
  if (item)
  {
    // we must be sure nobody starts reading at this moment
    // this would switch the write buffer under our hands
    EngineDDBGStoredRefs.Add(item);
  }
}
  

void EngineDD9::BGReleaseResourcesAndTextures()
{
  NotMTSafe check("BGReleaseResourcesAndTextures");
  //TrapFor0x8000Scope lock("EngineDD9::BGReleaseResourcesAndTextures");

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


void EngineDD9::BGReleaseRefs()
{
  AssertMainThread();
  struct ReleaseRef
  {
    bool operator () (RefCount *ref) const {ref->Release();return false;}
  };
  
  EngineDDBGStoredRefs.ForEach(ReleaseRef());
}

Void D3DDeviceServant::BeginTiling(const BeginTilingAllArgs &args) 
{
  return Void();
}
Void D3DDeviceServant::EndTiling(const EndTilingAllArgs &args) 
{
  return Void();
}

Void D3DDeviceServant::GpuOwnPixelShaderConstantF(const GpuOwnPixelShaderConstantFAllArgs &args) 
{
  return Void();
}

Void D3DDeviceServant::GpuPixelShaderConstantF4_1(const GpuPixelShaderConstantF4_1AllArgs &args) 
{
  return Void();
}

Void D3DDeviceServant::SetShaderGPRAllocation(const SetShaderGPRAllocationAllArgs &args)
{
  return Void();
}


Void D3DDeviceServant::StretchRect(const StretchRectAllArgs &args)
{
  #if !XBOX_D3D
  CALL_DEVICE(StretchRect,5);
  #endif
  return Void();
}

Void D3DDeviceServant::ShowCursor(const ShowCursorAllArgs &args)
{
  #if !XBOX_D3D
  CALL_DEVICE(ShowCursor,1);
  #endif
  return Void();
}

static inline int ProfileBeginGraphScope(unsigned int color, const char *name)
{
  #if _ENABLE_REPORT
    // name is limited to 31 chars
    const int maxName = 31;
    int len = strlen(name);
    if (len>maxName) name += len-maxName;
		#ifndef _X360SHADERGEN
			wchar_t c[256];
			mbstowcs(c, name, 256);
			return D3DPERF_BeginEvent(color,c);
		#else
			return -1;
		#endif
  #else
    return -1;
  #endif
}

static inline int ProfileEndGraphScope()
{
  #if _ENABLE_REPORT
    #ifndef _X360SHADERGEN
      return D3DPERF_EndEvent();
    #else
      return -1;
    #endif
  #else
    return -1;
  #endif
}

Void D3DDeviceServant::ProfileBeginGraphScope(const ProfileBeginGraphScopeAllArgs &args)
{
  ::ProfileBeginGraphScope(args.arg1,args.arg2);
  return Void();
}

Void D3DDeviceServant::ProfileEndGraphScope(const ProfileEndGraphScopeAllArgs &args)
{
  ::ProfileEndGraphScope();
  return Void();
}

Void D3DDeviceServant::Draw2D(const Draw2DAllArgs &args)
{
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD->_backgroundD3D==EngineDD9::RecHighLevel);
  //TrapFor0x8000Scope lock("D3DDeviceServant::Draw2D");
  GEngineDD->DoDraw2D(args.arg1._pars,args.arg2,args.arg3);
  return Void();
}

Void D3DDeviceServant::Postprocess(const PostprocessAllArgs &args)
{
  //TrapFor0x8000Scope lock("D3DDeviceServant::Postprocess");
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD->_backgroundD3D==EngineDD9::RecHighLevel);
  GEngineDD->DoPostprocess(args.arg1,args.arg2);
  return Void();
}

Void D3DDeviceServant::NoPostprocess(const NoPostprocessAllArgs &args)
{
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD->_backgroundD3D==EngineDD9::RecHighLevel);
  GEngineDD->DoNoPostprocess();
  return Void();
}

Void D3DDeviceServant::DrawSSAO(const DrawSSAOAllArgs &args)
{
  GEngineDD->DoDrawSSAO(args.arg1, args.arg2, args.arg3);
  return Void();
}

Void D3DDeviceServant::DrawEnfDebugShapes(const DrawEnfDebugShapesAllArgs &args)
{
	GEngineDD->DoDrawEnfDebugShapes();
	return Void();
}

Void D3DDeviceServant::R2TCopyTarget(const R2TCopyTargetAllArgs &args)
{
#if _HELISIM_RENDER_TO_TEXTURE
  _dev->StretchRect(args.arg1, args.arg2, args.arg3, NULL, D3DTEXF_NONE);

  if (args.arg4)
    GEngineDD->SaveR2TToFile(args.arg3);
#endif
  return Void();
}

Void D3DDeviceServant::R2TPostProcess(const R2TPostProcessAllArgs &args)
{
#if _HELISIM_RENDER_TO_TEXTURE
  GEngineDD->DoR2TPostProcess(args.arg1, args.arg2, (RenderTargetID)args.arg3);
#endif
  return Void();
}

Void D3DDeviceServant::R2TClearTarget(const R2TClearTargetAllArgs &args)
{
  if (args.arg1)
  {
    ComRef<IDirect3DSurface9> origRT;
    _dev->GetRenderTarget(0, origRT.Init());

    ComRef<IDirect3DSurface9> surfToClear;
    args.arg1->GetSurfaceLevel(0, surfToClear.Init());

    HRESULT hr = _dev->SetRenderTarget(0, surfToClear);
    DoAssert(hr == S_OK);
    _dev->Clear(0, 0, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);

    _dev->SetRenderTarget(0, origRT);
  }
  return Void();
}


Void D3DDeviceServant::QueryIssue(const QueryIssueAllArgs& args)
{
	GEngineDD->DoQueryIssue(args.arg1, args.arg2);
	return Void();
}

Void D3DDeviceServant::SetFogColor(const SetFogColorAllArgs &args)
{
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD->_backgroundD3D==EngineDD9::RecHighLevel);
  GEngineDD->DoSetFogColor(args.arg1);
  return Void();
}

Void D3DDeviceServant::FinishRendering(const FinishRenderingAllArgs &args)
{
  //TrapFor0x8000Scope lock("D3DDeviceServant::FinishRendering");
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD->_backgroundD3D==EngineDD9::RecHighLevel);
  GEngineDD->DoFinishRendering();
  return Void();
}


Void D3DDeviceServant::DrawPolyPrepare(const DrawPolyPrepareAllArgs &args)
{
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD->_backgroundD3D==EngineDD9::RecHighLevel);
  //TrapFor0x8000Scope lock("D3DDeviceServant::DrawPolyPrepare");
  GEngineDD->DoDrawPolyPrepare(args.arg1._pars,args.arg2);
  return Void();
}

Void D3DDeviceServant::DrawLineDo(const DrawLineDoAllArgs &args)
{
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD->_backgroundD3D==EngineDD9::RecHighLevel);
  GEngineDD->DoDrawLineDo(args.arg1,args.arg2,args.arg3,args.arg4);
  return Void();
}

Void D3DDeviceServant::DrawPolyAbsDo(const DrawPolyAbsDoAllArgs &args)
{
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD->_backgroundD3D==EngineDD9::RecHighLevel);
  GEngineDD->DoDrawPolyDo(args.arg1.Data(),args.arg1.Size(),args.arg2);
  return Void();
}

Void D3DDeviceServant::DrawPolyPixelDo(const DrawPolyPixelDoAllArgs &args)
{
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD->_backgroundD3D==EngineDD9::RecHighLevel);
  GEngineDD->DoDrawPolyDo(args.arg1.Data(),args.arg1.Size(),args.arg2);
  return Void();
}

Void D3DDeviceServant::FlushQueues(const FlushQueuesAllArgs &args)
{
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD->_backgroundD3D==EngineDD9::RecHighLevel);
  GEngineDD->FlushAndFreeAllQueues();
  return Void();
}

Void D3DDeviceServant::DrawDecal(const DrawDecalAllArgs &args)
{
  //TrapFor0x8000Scope lock("D3DDeviceServant::DrawDecal");
  GEngineDD->DoDrawDecal(args.arg1,args.arg2,args.arg3,args.arg4,args.arg5,args.arg6,args.arg7._pars,args.arg8,args.arg9);
  return Void();
}

Void D3DDeviceServant::CalculateFlareIntensity(const CalculateFlareIntensityAllArgs &args)
{
  //TrapFor0x8000Scope lock("D3DDeviceServant::CalculateFlareIntensity");
  GEngineDD->DoCalculateFlareIntensity(args.arg1,args.arg2,args.arg3,args.arg4);
  return Void();
}

Void D3DDeviceServant::PresentBackbuffer(const PresentBackbufferAllArgs &args)
{
  //TrapFor0x8000Scope lock("D3DDeviceServant::PresentBackbuffer");
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD->_backgroundD3D==EngineDD9::RecHighLevel);
  GEngineDD->DoPresentBackbuffer();
  return Void();
}


Void D3DDeviceServant::ForgetAvgIntensity(const ForgetAvgIntensityAllArgs &args)
{
  //TrapFor0x8000Scope lock("D3DDeviceServant::ForgetAvgIntensity");
  // this is high level instruction, we should be in the high level recording mode
  //Assert(GEngineDD->_backgroundD3D==EngineDD9::RecHighLevel);
  GEngineDD->DoForgetAvgIntensity();
  return Void();
}

Void D3DDeviceServant::CopyToVRAM(const CopyToVRAMAllArgs &args)
{
  #if !XBOX_D3D
  //TrapFor0x8000Scope lock("D3DDeviceServant::ForgetAvgIntensity");

  // Cover the whole source texture as dirty to make sure it will be copied
  args.arg1->AddDirtyRect(NULL);
  CALL_DEVICE(UpdateTexture, 2);
  #endif
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

WaitableVoid VisualServant::PerformMipmapLoading(const PerformMipmapLoadingAllArgs &args)
{
  GEngineDD->TextBankDD()->PerformMipmapLoading();
  return WaitableVoid();
}

Void VisualServant::DoLoadHeaders(const DoLoadHeadersAllArgs &args)
{
  TextureD3D9 *texture = static_cast<TextureD3D9 *>(args.arg1.GetRef());
  if (!texture->HeadersReadyFast())
    texture->LoadHeadersServantCallback();
  return Void();
}
/*
Void VisualServant::UseMipmap(const UseMipmapAllArgs &args)
{
  GEngineDD->TextBankDD()->UseMipmap(args.arg1,args.arg2,args.arg3);
  return Void();
}
*/

Void VisualServant::CallDelegate(const CallDelegateAllArgs &args)
{
  (*args.arg1)();
  return Void();
}

Void D3DDeviceServant::UpdateSimulWeatherTexture(const UpdateSimulWeatherTextureAllArgs &args)
{
#if _ARMA3_SIMUL_WEATHER
  GEngineDD->DoUpdateSimulWeatherTexture(args.arg1, args.arg2);
#endif
  return Void();
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
  VectorCompressed s;
  VectorCompressed t;
};

struct SVertexUV29
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  UVPairCompressed t1;
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
  BYTE weights[4];
  BYTE indices[4];
};

struct SVertexNormalSkinned9
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
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
  BYTE index[4]; // Just w is used, the rest is dummy
};

struct SVertexNormalInstanced9
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
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

//!}

//////////////////////////////////////////////////////////////////////////

#include <El/Debugging/debugTrap.hpp>
#pragma comment(lib,"dxerr9")

void EngineDD9::DDError9( const char *text, int err )
{
  LogF("%s:%s",text,DXGetErrorString9(err));
  if (GDebugger.IsDebugger())
  {
    //BREAK();
  }
  #if _ENABLE_REPORT
    int texUsed = _textBank ? _textBank->GetVRamTexAllocation() : 0;
    LogF("Total allocated: VB/IB/Tex: %d/%d/%d",_vBuffersAllocated,_iBuffersAllocated,texUsed);
    LogF("Mem Status: %s",GetMemErrorStatus());
  #endif
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

void ConvertMatrix3(MatrixFloat34 &mat, Matrix4Val src)
{
#if defined _M_PPC && defined _MATH3DK_HPP
	DoAssert(false);// TODO
#else
	mat[0][0] = src.DirectionAside()[0];
	mat[0][1] = src.DirectionAside()[1];
	mat[0][2] = src.DirectionAside()[2];

	mat[1][0] = src.DirectionUp()[0];
	mat[1][1] = src.DirectionUp()[1];
	mat[1][2] = src.DirectionUp()[2];

	mat[2][0] = src.Direction()[0];
	mat[2][1] = src.Direction()[1];
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

void EngineDD9::GetZCoefs(int cb, float &zAdd, float &zMult)
{
  int zBias = CBState(cb)._bias;
  zMult = 1.0f-zBias*1e-7f;
  zAdd = zBias*-2e-7f;
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

// TODO: avoid scene here
#include "../scene.hpp"
#include "../lights.hpp"
#include "../camera.hpp"


void LogTextureF( const char *t, const TextureD3D9 *x )
{
  LogF("Texture %s %s",t,x ? x->Name() : "--");
}

#if 0
#define LogTexture(t,x) LogTextureF(t,x)
#else
#define LogTexture(t,x)
#endif

#if DO_TEX_STATS
void LogSetProj9(const D3DMATRIX &mat)
{
  if (LogStatesOnce)
  {
    LogF("Set Projection %g,%g,%g,%g",mat._11,mat._22,mat._33,mat._43);
  }
  /*
  float c = mat._33;
  float d = mat._43;
  float n = -d/c;
  float f = d/(1-c);

  float Q = f/(f-n);
  float mQZn = -Q*n;
  LogF("Set c=%g (%g), d=%g (%g), n=%.2f, f=%.1f",c,Q,d,mQZn,n,f);
  */

}
#else

#define LogSetProj9(mat)

#endif

void EngineDD9::SetBias( int cb, int bias )
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


void EngineDD9::KickOffCBDebug()
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

inline void EngineDD9::KickOffHighLevel()
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

void EngineDD9::FlushQueues()
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

bool EngineDD9::IsAbleToDrawCheckOnly()
{
  LOG_MEM("IsAbleToDrawCheckOnly %x,%d",_resetStatus,_resetRequested);
  return SUCCEEDED(_resetStatus) && !_resetRequested;
}

void EngineDD9::SetBones(int cb, int minBoneIndex, const Matrix4 *bones, int nBones)
{
  // Set the view matrices and matrix offset
  if (nBones > 0)
  {
    // Set the view matrices
    MatrixFloat34 matrices[EngineDD9::LimitBones];
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
    SetVertexShaderConstantF(cb,FREESPACE_START_REGISTER, (float*)matrices, nBones * vectorsNum);
  }

  // Set the matrix offset
  const float offset = minBoneIndex / 255.0f;
  D3DXVECTOR4 vector4(offset, offset, offset, offset);
  SetVertexShaderConstantF(cb,VSC_MatrixOffset, vector4, 1);
}

void EngineDD9::SetSkinningType(int cb, ESkinningType skinningType, const Matrix4 *bones, int nBones)
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

void EngineDD9::SetSkinningTypeInstance(ESkinningType skinningType, int instanceIndex, const Matrix4 *bones, int nBones)
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

float EngineDD9::GetMaxStarIntensity() const
{
  return MAX_STAR_INTENSITY;
}

bool EngineDD9::CanSeaUVMapping(int waterSegSize, float &mulUV, float &addUV) const
{ 
  // we want to transform from 0..waterSegSize to 
  mulUV = float(UVCompressedMax-UVCompressedMin)/waterSegSize;
  addUV = UVCompressedMin;
  return true;
}

void EngineDD9::SetWaterWaves(int cb, const WaterDepthQuad *water, int xRange, int zRange)
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

void EngineDD9::SetWaterDepth(
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
#if !_VBS3_LAND_SHADOW
void EngineDD9::SetLandShadow(int cb, const float *shadow, int xRange, int zRange, float grid)
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

void EngineDD9::SetInstanceInfo(int cb, ColorVal color, float shadowIntensity, bool optimize)
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

void EngineDD9::PreparePostprocess()
{
  //EndBackgroundScope(true,"flush");
  
  // note: when PreparePostprocess is used, the next call to SubmitBackground is normally not submitting anything,
  // as the buffer is already empty
  Assert(_backgroundD3D!=RecHighLevel);
  if (_backgroundD3D!=RecNone)
  {
    // make sure the stream is terminated by a cleanup instruction
    SubmitBackground(_bg,true,true,CleanUpNeeded,"flush");
  }
}

void EngineDD9::FlushBackgroundScope()
{
  Assert(_backgroundD3D!=RecHighLevel);
  if (_backgroundD3D!=RecNone)
  {
    EndBackgroundScope(true,"flush");
    BGReleaseResourcesAndTextures();
    BGReleaseRefs();
  }

}

void EngineDD9::StopAll()
{
  #if 1
  // we prefer to avoid any threading during shutdown
  EndHighLevelRecording();
  FlushBackgroundScope();
  _hlRecording = false;
  #endif
}


inline EngineDD9::D3DCommandBufRecWithSubmit EngineDD9::RecordHighlevel()
{
  Assert (_backgroundD3D==RecHighLevel); // we must be in the correct recorning mode
  Assert (CheckMainThread()); // recording must be done from the main thread
  return _cbState[0]._rec;
}


void EngineDD9::SwitchRecordingToHighLevel()
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

bool EngineDD9::EndHighLevelRecordingPart1()
{
  // we use this to terminate both high- and low- level recording 
  if(_backgroundD3D!=RecNone)
  {
    Assert(CheckMainThread());
    // wait until instructions are executed
    // we need to flush
    
    PROFILE_SCOPE_EX(drwBg,*);
    FlushBackground(_bg,true,"endHL",true);

    // problem here: we cannot set CB state, as high-level instructions in the background thread may be still using it 
    //UnsetAllDiscardableResourcesFromCBState();
    return true;
  }
  return false;
}

void EngineDD9::EndHighLevelRecordingPart2(bool part1Result)
{
  if (part1Result)
  {
    Assert(_backgroundD3D == RecNone);
    BGReleaseResourcesAndTextures();
    BGReleaseRefs();
  }
  UnsetAllDiscardableResources();
}

void EngineDD9::EndHighLevelRecording()
{
  bool part1Result = EndHighLevelRecordingPart1();
  EndHighLevelRecordingPart2(part1Result);
}


bool EngineDD9::Allow3DUI() const
{
  if (_backgroundD3D!=RecHighLevel) return true;
  // we cannot render 3D UI objects while capturing HL instruction
  // when some will ask us, we will terminate HL recording and return true to allow 3D
  unconst_cast(this)->EndHighLevelRecording();
  return true;
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


void EngineDD9::CaptureRefractionsBackground()
{
  IDirect3DTexture9 *rt = CreateRenderTargetCopyAsTexture();
  (void)rt;
  // make sure the refraction is prepared OK
  DoAssert(rt);
}

void EngineDD9::SetBloom(float bloom) 
{ // negative value sets Default
  if (bloom>=0) _bloomThreshold = bloom; 
  else   _bloomThreshold = DefaultBloomThreshold; 
}

float EngineDD9::GetBloom() const
{
  return _bloomThreshold;
}

void EngineDD9::SetRotBlurPower(float blurPow)
{
  if (blurPow>=0) _rotBlurPower = blurPow;
  else _rotBlurPower = DefaultRotBlurPower;
}

float EngineDD9::GetRotBlurPower() const
{
  return _rotBlurPower;
}

void EngineDD9::Postprocess(float deltaT, const PostFxSettings &pfx)
{
  DoAssert(!_end3DRequested);
  _end3DRequested = true;

	//prepare
	if(_pp._rain)
	{
		_pp._rain->Prepare(deltaT);
	}

  // switch recording to high level instructions (postprocess, 2D rendering, present)
  SwitchRecordingToHighLevel();

  if (_spp._hit) _spp._hit->Update();
  if (_spp._tired) _spp._tired->Update();
  if (_spp._movement) _spp._movement->Update();

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

/* Used for post effect sort */
struct PPPriorities
{
  int _priority;
  Ref<EngineDD9::PostProcess> _pp;
};

TypeIsMovable(PPPriorities)

static int ComparePriorities(const PPPriorities *pp0, const PPPriorities *pp1)
{
  int diff = pp0->_priority - pp1->_priority;
  return sign(diff);
}

void EngineDD9::DoPostprocess(float deltaT, const PostFxSettings &pfx)
{
  static float BloomBlurScale = 1.0f;
  static float BloomImgScale = 1.0f;

  End3DRendering(); // End rendering of the 3D part of the scene

  // Queues handling moved from World::Draw (to be behind the End3DRendering call)
  FlushAndFreeAllQueues();

  //return; //<<< SKIP ALL PP FOR DEBUG (by Medart)<<<

  DoAssert(_linearSpace);
  // no depth buffer will be required during glow or final
  if (_depthBufferRT)
  {
    if (LogStatesOnce) LogF("Postprocess: Set Depth to NULL");
    _d3DDevice->SetDepthStencilSurface(NULL);
    _currRenderTargetDepthBufferSurface.Free();
  }

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
  
  bool ppaaEnabled = (_pp._smaa && _pp._smaa->IsEnabled() || _pp._fxaa && _pp._fxaa->IsEnabled()) && !_nightVisionThis;

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
		if (_pp._godRays&& (_pp._godRays->GetLevel() & _postFXLevelMaskThis) && _pp._godRays->IsActive())
		{
			//film grain should have the biggest priority
			PPPriorities godRaysPrior = { _pp._godRays->GetPriority(), _pp._godRays };
			sort.Add(godRaysPrior);
		}

		// add effects if set to enabled in video options
		if (_pp._rain&& (_pp._rain->GetLevel() & _postFXLevelMaskThis) && _pp._rain->IsActive())
		{
			PPPriorities rainPrior = { _pp._rain->GetPriority(), _pp._rain};
			sort.Add(rainPrior);
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
      float pars[3] = {BloomBlurScale, BloomImgScale, _bloomThreshold};
      _pp._bloom->SetParams(pars, 3);
    }
    if (_pp._rotBlur)
    {
      _pp._rotBlur->SetBlurPower( _rotBlurPower );
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
        _pp._glow->Do(!someAfterGlow && !forceFinal && !ppaaEnabled);
      }
    }

    // PostProcess::Do(true) isLast == true -> postprocess to back-buffer - valid for PColors
    for (int index=afterGlow;index < sort.Size(); index++)
    {
      sort[index]._pp->Do(index==sort.Size()-1 && !forceFinal && !ppaaEnabled);
    }
  }

#if defined _HELISIM_EXTENDED_RESOLUTION && (_ENABLE_CHEATS != 0)
  static bool MakeScreenShot = false;

  if (GInput.GetCheat3ToDo(DIK_HOME))
    MakeScreenShot = true;

  if (MakeScreenShot)
  {
    DoSaveSurfaceIntoFile(_currRenderTargetSurface);
    MakeScreenShot = !MakeScreenShot;
  }
#endif

	//GScene->RenderDebugShapes();

  // calling final here should not be necessary, as it should be called later anyone
  // however it seems to be clearer to call it here, so that we can be sure we leave this function in a state where:
  // back-buffer is set as the current render target
  if (forceFinal && !ppaaEnabled)
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

  // 
  if (ppaaEnabled)
  {
    DoAssert(_linearSpace);

    if (_pp._smaa->IsEnabled())
      _pp._smaa->Do(true);
    else
      _pp._fxaa->Do(true);
  }

  DoAssert(!_linearSpace);
  if (!_linearSpace && _caps._canWriteSRGB)
  {
    // SRGB write conversion was on before here - turn it off
    D3DSetRenderState(-1,D3DRS_SRGBWRITEENABLE,FALSE);
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
}

void EngineDD9::ForEachPostEffectHndl(IPPHndlFunc &func)
{
  for (int i = 0; i < _pp._effects.Size(); i++)
  {
    func(_pp._effects[i]);
  }
}

/* Check if there is post effect with same priority. */
bool EngineDD9::ValidatePPPriority(int priority) const
{
  for (int i = 0; i < _pp._effects.Size(); i++)
  {
    const PostProcessHndl &ppHndl = _pp._effects[i];
    PostProcess *pp = static_cast<PostProcess*>(ppHndl._pp.GetRef());

    if (pp && pp->GetPriority() == priority) return false;
  }

  return true;
}

/* Define unique handle. Handle is used for post efect instance identification. */
int EngineDD9::GetPostEffectHandle() const
{ 
  static int Hndl = 1; 
  return (Hndl++); 
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

/* Locate process effect on the base of hndl, if hndl not found return InvalidPPHndl. */
PostProcessHndl& EngineDD9::AccessPostprocess(int hndl)
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

/* Release post effect determined by hndl */
void EngineDD9::DestroyPostprocess(int hndl)
{
#if VALIDATE_HNDL_ENABLE
  bool hndlFound = ValidatePPEHndl(_pp._effects, hndl);
  Assert(hndlFound);
#endif

  PostProcessHndl &ppHndl = AccessPostprocess(hndl);

  if (ppHndl != InvalidPPHndl) { _pp._effects.Delete(ppHndl); }
}

/* Enable / disable post effect */
void EngineDD9::EnablePostprocess(int hndl, bool enable)
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

void EngineDD9::SetSpecialEffectPars(int index, const float *pars, int nPars)
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
}

/* Set post effect params */
int EngineDD9::SetPostprocessParams(int hndl, AutoArray<float> &pars)
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

int EngineDD9::SetPostprocessParams(int hndl, const float* pars, int nPars)
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

void EngineDD9::CommitPostprocess(int hndl, float time)
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

bool EngineDD9::CommittedPostprocess(int hndl)
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

void EngineDD9::DoNoPostprocess()
{
  End3DRendering();
  
  // Queues handling moved from World::Draw (to be behind the End3DRendering call)
  FlushAndFreeAllQueues();
  
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
    D3DSetRenderState(-1,D3DRS_SRGBWRITEENABLE,FALSE);
#endif
  }
}

void EngineDD9::NoPostprocess()
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

void EngineDD9::CalculateFlareIntensity(int x, int y, int sizeX, int sizeY)
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

void EngineDD9::DoCalculateFlareIntensity(int x, int y, int sizeX, int sizeY)
{
  
  // Calculate flare intensity and set texture on stage 15
  _pp._flareIntensity->Do(x, y, sizeX, sizeY);

  const int cb = -1;
  // Set aperture texture on stage 14
  SetTexture(cb, 14, _pp._glow->GetApertureTexture());
  D3DSetSamplerState(cb, 14, D3DSAMP_SRGBTEXTURE, FALSE);
  D3DSetSamplerState(cb, 14, D3DSAMP_MINFILTER, D3DTEXF_POINT);
  D3DSetSamplerState(cb, 14, D3DSAMP_MAGFILTER, D3DTEXF_POINT);

  // Set decimation texture on stage 13
  SetTexture(cb, 13, _pp._glow->GetDecimationTexture());
  const bool decimatedSRGB = _downsampleFormat==D3DFMT_A8B8G8R8;
  D3DSetSamplerState(cb, 13, D3DSAMP_SRGBTEXTURE, decimatedSRGB);

  // Set PS Constant 6
  Color eyeSens = _pp._glow->EyeSens(_pp._glow->Cones());
  SetPixelShaderConstantF(cb, 6, D3DXVECTOR4(eyeSens.R(), eyeSens.G(), eyeSens.B(), 1), 1);

  float hdrMultiply = GetPostHDRBrightness() / GetHDRFactor();
  SetPixelShaderConstantF(cb, PSC_HDRMultiply, D3DXVECTOR4(hdrMultiply,hdrMultiply,hdrMultiply,hdrMultiply), 1);
}

bool IsAppPaused();

AbilityToDraw EngineDD9::IsAbleToDraw(bool wantReset)
{
  Assert(CheckMainThread());
#if defined(_X360SHADERGEN)
  return ATDAble;
#else
  if (_d3DDevice.IsNull()) return ATDUnable;
  if (!_presentPars.Windowed && IsAppPaused()) return ATDUnable;


  if (_resetRequested)
  {
    LogF("*** Device reset recovery requested");
    // consider: in Direct3DEx recreate the whole Direct3D object
    // if (_direct3DEx ? ResetHard() : Reset())
    if (Reset())
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



  /// loop through the FSM  
  for(;;)
  {
    switch (_resetStatus)
    {
    case D3D_OK:
      // verify the status is still OK
      if (_resetDone && wantReset)
      {
        // report reset only once
        _resetDone = false;
        return ATDAbleReset;
      }
      return ATDAble;
    case D3DERR_DEVICELOST:
      LogF("*** Device lost");
      _resetStatus = _d3DDevice->TestCooperativeLevel();
      // wait until status is changed
      if (_resetStatus==D3DERR_DEVICELOST)
      {
        return ATDUnable;
      }
      LogF("*** Device lost - status changed to %x",_resetStatus);
      // status changed - process the change
      break;
    case D3DERR_DRIVERINTERNALERROR: // Internal error - perform Reset
      RptF("*** Device lost - Driver Internal Error");
      _resetStatus = D3DERR_DEVICENOTRESET;
      break;
    case D3DERR_DEVICEHUNG: // Internal error - perform Reset
      RptF("*** Device hung");
      _resetStatus = D3DERR_DEVICENOTRESET;
      break;
    case D3DERR_DEVICEREMOVED: // Internal error - perform Reset
      RptF("*** Device removed");
      _resetStatus = D3DERR_DEVICENOTRESET;
      break;
    default: // Unknown state - perform Reset
      RptF("*** Device lost - unknown reason %x",_resetStatus);
      _resetStatus = D3DERR_DEVICENOTRESET;
      break;
    case D3DERR_DEVICENOTRESET: // Device reports Reset should be performed
      // in Direct3DEx we need not only to reset the device, we need to recreate the whole Direct3D object
      if (_direct3DEx ? ResetHard() : Reset())
      {
        _resetStatus = D3D_OK;
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
    #if !XBOX_D3D
    case S_PRESENT_MODE_CHANGED: // Device reports Reset should be performed
      if (wantReset)
      {
        #if 0 // simulate lost device on Vista to allow debugging it
        _resetStatus = D3DERR_DEVICELOST;
        ResetHard();
        #else
        // we need to Reset, otherwise the error code will return again and again
        // for Reset we should enumerate device modes again
        // when running Windowed, there is no problem at all - Windowed is always supported
        if (_windowed)
        {
          LogF("*** Present: Mode changed");
          // before caling reset, we need to be sure any references to the old backbuffer / depthbuffer are processed
          EndHighLevelRecording();
          EndBackgroundScope(true,"reset");
          
          HRESULT resErr = _d3DDevice->Reset(&_presentPars);
          if (!SUCCEEDED(resErr))
          {
            LogF("Reset failed: %x",resErr);
          }
          else
          {
            Assert(_renderTargetSurfaceSaved!=_backBuffer);
            Assert(_renderTargetDepthBufferSurfaceSaved!=_depthBuffer);
            
            // we need to re-query for back-buffer and its associated depth buffer
            LogF("  Old Buffers: %p,%p",_backBuffer.GetRef(),_depthBuffer.GetRef());
            _d3DDevice->GetDepthStencilSurface(_depthBuffer.Init());
            _d3DDevice->GetRenderTarget(0, _backBuffer.Init());
            _currRenderTargetSurface = _backBuffer;
            _currRenderTargetDepthBufferSurface = _depthBuffer;
            _resetStatus = D3D_OK;
            LogF("  New Buffers: %p,%p",_backBuffer.GetRef(),_depthBuffer.GetRef());
          }
        }
        else
        {
          // TODO: implement fullscreen reaction
          // in fullscreen we could react by re-enumerating display modes and selecting a new one as close
          // however, as lost/hung devices are very rare anyway and reaction to S_PRESENT_MODE_CHANGED is not really needed
          // rendering can continue with no reaction at all, we silently ignore the message
          // to ignore it we have to at least reset the status
          _resetStatus = D3D_OK;
        }
        #endif
      }
      else
      {
        // inside progress bar - we can continue with no reaction at all, we silently ignore the message
        // to ignore it we have to at least reset the status
        _resetStatus = D3D_OK;
      }
      break;
    #endif
    }
  }
#endif
}

#define SHADOW_LEVELS 1

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

void EngineDD9::InitDraw( bool clear, Color color )
{
  LOG_MEM(__FUNCTION__);
  
  // If frame in progress then finish
  if( _d3dFrameOpenRequested )
  {
    LogF("InitDraw done twice");
    LOG_MEM("InitDraw done twice");
    return;
  }

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
  
  
  #if _ENABLE_REPORT
  if (_backgroundD3D==RecNone)
  {
    // verify the back-buffer information we have is correct
    ComRef<IDirect3DSurface9> bb;
    _d3DDevice->GetRenderTarget(0, bb.Init());
    DoAssert(bb==_backBuffer);
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
    SetRenderTarget(_renderTargetSurfaceS,_renderTargetS,_depthBufferRT);	//MRT here, set next RTs
  }

  // reset viewport z-range to default
  _minZRange = 0;
  _maxZRange = 1;

  if (_caps._canWriteSRGB)
  {
    // if using float render targets, perform SRGB writing only during final visualization
    _caps._rtIsSRGB = _rtFormat==D3DFMT_A8R8G8B8 || _rtFormat==D3DFMT_X8R8G8B8;
    // If we use thermal vision, we don't want to use SRGB on output (because the output holds encoded temperatures and we want to read them as they are)
    if (_thermalVision)
    {
      D3DSetRenderState(cb, D3DRS_SRGBWRITEENABLE,FALSE);
    }
    else
    {
      D3DSetRenderState(cb, D3DRS_SRGBWRITEENABLE,_caps._rtIsSRGB);
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
  
  D3DRECT rect;
  rect.x1=0,rect.y1=0;
  rect.x2=_w,rect.y2=_h;
  int flags=0;

  // TODO: unify for FSAA as well
//   if (GetFSAA()>0)
//   {
//     flags |= D3DCLEAR_ZBUFFER;
//     if (_caps._hasStencilBuffer) flags|=D3DCLEAR_STENCIL;
//   }
  // sometimes we want to keep the old backbuffer content
  if( clear ) flags|=D3DCLEAR_TARGET;

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
      SetTexture(-1,TEXID_TC, _tableTc, false);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MINFILTER, D3DTEXF_POINT);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_SRGBTEXTURE, FALSE);
    }
    else
    {
      // color needs to be halved - it will be double in HDR glow pass
      color = color * GetHDRFactor();
    }
    if (flags)
    {
      CALL_D3D(Clear,0,NULL,flags,PackedColor(color),1,0);
    }
    _clearColor = color;

#if _ENABLE_CHEATS
    // set viewport as needed
    // by default viewport is set to contain whole render target?
    D3DVIEWPORT9 viewData;
    memset(&viewData,0,sizeof(viewData));
    viewData.X = 0;
    viewData.Y = 0;
    viewData.MinZ = _minZRange;
    viewData.MaxZ = _maxZRange;
    // this is the 2D viewport - use 2D dimensions here
    viewData.Width  = _w;
    viewData.Height = _h;
    CALL_D3D(SetViewport,viewData);
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

  D3DSetRenderState(cb, D3DRS_STENCILWRITEMASK, STENCIL_FULL);

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
  cbState._lastMat = TexMaterialLODInfo((TexMaterial *)-1);
  cbState._lastRenderingMode = RMCommon;

  // Zero the counter
  _dipCount = 0;

  InitSceneInvariants(cb);

  // set some global rendering constants
  //   float hdrMultiply = GetPostHDRBrightness()/GetHDRFactor();
  //   SetPixelShaderConstantF(PSC_HDRMultiply, D3DXVECTOR4(hdrMultiply,hdrMultiply,hdrMultiply,hdrMultiply), 1);

  // Begin rendering of the 3D part of the scene
  Begin3DRendering();

  _tiVisualizationDone = false;
}

void EngineDD9::ResetLogDevice()
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
void EngineDD9::InitSceneInvariants(const int cb)
{
  //   // Set the dithering coefficient upon MSAA level
  //   // Label: AlphaToCoverage
  //   float dc = 1.0f / 4.0f; // 5 levels of transparency
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
  BOOL detail[4]= {
    GScene ? GScene->GetShadingQuality()>=3 : true,
    GScene ? GScene->GetShadingQuality()>=7 : true,
    GScene ? GScene->GetShadingQuality()>=10 : true,
    GScene ? GScene->GetShadingQuality()>=100 : true,
  };
  SetPixelShaderConstantB(-1, PSC_ShadingDetail,detail,4);

  // Set inverse screen resolution
  Color screenRes(1.0f / _wRT, 1.0f / _hRT, Glob.config.GetMaxLinearDepth(), 0.0f);
  SetPixelShaderConstantF(-1, PSC_InvW_InvH_MaxLineDepth_X, (float*)&screenRes, 1);
}

void EngineDD9::DoFinishRendering()
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

const bool MipLoadingAsync = true;

bool CheckExThreads(int mask, bool autodetected);

bool EngineDD9::IsMipLoadingAsync() const
{
  return MipLoadingAsync && Glob.config._useCB && GFileServerFunctions->OtherThreadsCanUse() && CheckExThreads(2,GetCPUCount()>=2);
}

void EngineDD9::FinishPresent()
{
  LOG_MEM(__FUNCTION__);
  // once we own the state again, we can load the textures
  // do this only once per frame
  if (_textBank->FrameNotFinishedYet())
  {
    // wait until previous Present is completed
    // after this call we can be sure we own the device state
    bool part1Result = EndHighLevelRecordingPart1();
    EndHighLevelRecordingPart2(part1Result);

    // UnsetAllDiscardableResources was called by EndHighLevelRecording
    if (IsMipLoadingAsync())
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

void EngineDD9::FinishFinishPresent()
{
  LOG_MEM(__FUNCTION__);
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
      PROFILE_SCOPE_DETAIL_EX(drwVF,dd9);
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
    #if LOG_RESULTS_ALLOC
    _memJournal.Page();
    #endif
  }
}

void EngineDD9::InitNoDraw()
{
  LOG_MEM(__FUNCTION__);
  if (_textBank) _textBank->PerformAsyncMaintenance();
  base::InitNoDraw();
}
void EngineDD9::FinishNoDraw(int wantedDurationVSync)
{
  LOG_MEM(__FUNCTION__);
  base::FinishNoDraw(wantedDurationVSync);
}

void EngineDD9::FinishDraw(int wantedDurationVSync, bool allBackBuffers, RString fileName)
{
  LOG_MEM(__FUNCTION__);
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

    if (fileName != RStringB(""))
    {
      D3DXSaveSurfaceToFile(fileName + RStringB(".png"), D3DXIFF_PNG, _backBuffer, NULL, NULL);
      //       D3DXSaveSurfaceToFile("C:\\scrBack.png", D3DXIFF_PNG, _backBuffer, NULL, NULL);
      //       D3DXSaveSurfaceToFile("C:\\scrDepth.tga", D3DXIFF_TGA, _renderTargetSurfaceDB, NULL, NULL);
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

bool EngineDD9::InitDrawDone()
{
  // if frame is still open, but 3D rendering was already closed (postfx done or skipped), 
  // the state is not valid for rendering and nobody should attempt to render
  DoAssert (!_d3dFrameOpenRequested || !_end3DRequested);
  //if (_d3dFrameOpenRequested) LogF("???Open");
  return _d3dFrameOpenRequested;
}

void EngineDD9::BackBufferToAllBuffers()
{
  /*
  ComRef<IDirect3DSurface9> back;
  ComRef<IDirect3DSurface9> front;
  _d3DDevice->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,back.Init());
  _d3DDevice->GetFrontBuffer(0,front.Init());
  _d3DDevice->CopyRects(back,NULL,0,front,NULL);
  for (int i=1; i<_presentPars.BackBufferCount; i++)
  {
  ComRef<IDirect3DSurface9> backAdd;
  _d3DDevice->GetBackBuffer(i,D3DBACKBUFFER_TYPE_MONO,backAdd.Init());
  _d3DDevice->CopyRects(back,NULL,0,backAdd,NULL);
  }
  */
}

/*!
\patch 5156 Date 5/12/2007 by Ondra
- Fixed: Position of Present call in the frame changed to improve Alt-Tab stability and work around possible driver errors.
*/
void EngineDD9::NextFrame(bool doPresent)
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

  base::NextFrame(doPresent);
}


void EngineDD9::DebugTestTexture(IDirect3DTexture9* tex)
{
	for (int i=0; i<_bgTexHandleToRelease.Size(); i++)
	{
		if (_bgTexHandleToRelease[i] == tex)
		{
			LogF("fuck TestTexture");
		}
	}
}

void EngineDD9::DebugTestIndexBuffers(IDirect3DResource9* ib)
{
	for (int i=0; i<_bgResToRelease.Size(); i++)
	{
		if (_bgResToRelease[i] == ib)
		{
			LogF("fuck TestIndexBuffers");
		}
	}
}

void EngineDD9::D3DTextureDestroyed( TextureHandle9 *tex )
{
  // this assert is stronger than necessary, but as we use UnsetAllDiscardableResources
  // to make sure we guarantee the real condition, it is the easier way to check if we do it everywhere as needed
  Assert(CheckAllDiscardableResourcesUnset());
  
  #if ENCAPSULATE_TEXTURE_REF
  IDirect3DTexture9 *texRef = tex ? tex->_surface.GetRef() : NULL;
  #else
  IDirect3DTexture9 *texRef = tex;
  #endif
  for (int cb=0; cb<CBStateCount(); cb++)
  {
    for (int i = 0; i < lenof(_cbState[cb]._lastHandle); i++)
    {
      // if someone is releasing a texture we have still set, we have a problem
      // to prevent this, we unset all textures when we end rendering
      // no textures should be released while rendering
      DoAssert(_cbState[cb]._lastHandle[i] != texRef);
    }
  }
}

void EngineDD9::TextureDestroyed( Texture *tex )
{
  TextureD3D9 *texture = static_cast<TextureD3D9 *>(tex);
  TextureHandle9 *handle = texture->GetHandle();
  if (handle) D3DTextureDestroyed(handle);
}


void EngineDD9::D3DEndScene()
{
  CALL_D3D(EndScene,);
}
void EngineDD9::D3DBeginScene()
{
  LogTexture("XXX Start XXX",NULL);
  CALL_D3D(BeginScene,);
}

void EngineDD9::SetDebugMarker(int id, const char *text)
{
  LOG_MEM("SetDebugMarker %d (%s)",id,text);
  //D3DPERF_SetMarker();
  //_d3DDevice->SetDebugMarker(id);
}


int EngineDD9::ProfileBeginGraphScope(unsigned int color,const char *name) const
{
  LOG_MEM("BegScope [ %s",name);
  if (IsCBRecording())
  {
		#if (_PROFILE || _DEBUG)
			//works reasonably good when cpu count is at 1
			if (GetCPUCount() == 1)
				unconst_cast(this)->CB(-1).ProfileBeginGraphScope(color,name);
		#endif

		return 0;
  }
  if (IsLLRecording())
  {
    unconst_cast(this)->CB(-1).ProfileBeginGraphScope(color,name);
		return 0;
  }
  return ::ProfileBeginGraphScope(color,name);
}

int EngineDD9::ProfileEndGraphScope() const
{
  LOG_MEM("EndScope ]");
  if (IsCBRecording())
  {
		#if (_PROFILE || _DEBUG)
			//works reasonably good when cpu count is at 1
			if (GetCPUCount() == 1)
				unconst_cast(this)->CB(-1).ProfileEndGraphScope();
		#endif

		return 0;
  }
  if (IsLLRecording())
  {
    unconst_cast(this)->CB(-1).ProfileEndGraphScope();
    return 0;
}
  return ::ProfileEndGraphScope();
}

void EngineDD9::SetZPrimingDone(int cb, bool zPrimingDone)
{ // TODO:MC: sometimes needs to affect all CBs, sometimes only one
  CBState(cb)._zPrimingDone = zPrimingDone;
}

void EngineDD9::PrepareMesh( int cb, int spec )
{
  // prepare internal variables
  SwitchTL(cb,TLDisabled);
}

void EngineDD9::AddIBuffersAllocated( int n )
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

void EngineDD9::AddVBuffersAllocated( int n, bool dynamic )
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

void EngineDD9::AddLocalVRAMAllocated(int size)
{
  _freeLocalVramAmount -= size;
  _freeEstDirty += (abs(size)>>20)+1;
  _resetDirty += (abs(size)>>20)+1;
}

/*!
\param vertices Required number of vertices in the buffer
\param vertexSize Required size of the buffer in bytes
\param frequent indicated high performance is preferred (VRAM instead of AGP)
*/
VertexStaticData9::VertexStaticData9(int vertices, int vertexSize, bool frequent)
{
  _vertexSize = vertexSize;
  _count = vertices;
  _usageDynamic = false;

  if (_count > 0)
  {
    _usageDynamic = !frequent && !GEngineDD->GetVBuffersStatic();
#if _ENABLE_CHEATS
    if (ForceStaticVB) _usageDynamic = false;
#endif

    GEngineDD->AddVBuffersAllocated(_count*_vertexSize,_usageDynamic);
  }
}

VertexStaticData9::~VertexStaticData9()
{
  Dealloc();
  GEngineDD->AddVBuffersAllocated(-_count*_vertexSize,_usageDynamic);
}


void VertexStaticData9::Init(ComRef<IDirect3DVertexBuffer9> buffer
#if _ARMA3_VERTEX_BUFFER_EX
                             , int numPrimitives
#endif
                             )
{
#if _ARMA3_VERTEX_BUFFER_EX
  _vBuffer = BufferFuture(VertexBuffer9Ex(buffer, numPrimitives));
#else
  _vBuffer = BufferFuture(buffer);
#endif
}
void VertexStaticData9::InitFuture()
  {
  _vBuffer = BufferFuture();
}
void VertexStaticData9::SetFuture(ComRef<IDirect3DVertexBuffer9> buffer
#if _ARMA3_VERTEX_BUFFER_EX
                                  , int numPrimitives
#endif
                                  )
{
#if _ARMA3_VERTEX_BUFFER_EX
  _vBuffer.SetResult(VertexBuffer9Ex(buffer, numPrimitives));
#else
  _vBuffer.SetResult(buffer);
#endif
}
ComRef<IDirect3DVertexBuffer9> VertexStaticData9::CreateBuffer()
{
  ComRef<IDirect3DVertexBuffer9> buffer;

  if (_count <= 0) return buffer;

  MyD3DDevice &dev = GEngineDD->GetDirect3DDevice();
  int byteSize = _count*_vertexSize;

    // make sure there is no extensive VRAM memory overhead
  GEngineDD->TextBankDD()->ReserveMemory(byteSize,3);

    DWORD flags = WRITEONLYVB;

  if (_usageDynamic) flags |= D3DUSAGE_DYNAMIC;
RetryV:
    // note: goto before the constructor causes destruction
    PROFILE_SCOPE_DETAIL_EX(ddCVB,dd9);
    EngineDD9::CheckMemTypeScope scope(GEngineDD,_usageDynamic ? "VBdynamic" : "VBstatic",byteSize);
    HRESULT err = dev->CreateVertexBuffer(byteSize,flags,0,D3DPOOL_DEFAULT,buffer.Init(),NULL);
    if (err!=D3D_OK)
    {
      if (err==D3DERR_OUTOFVIDEOMEMORY)
      {
      GEngineDD->DDError9("CreateVertexBuffer - static",err);
        if (GEngineDD->TextBankDD()->ForcedReserveMemory(byteSize))
        {
          goto RetryV;
        }
      }
      else if (err==E_OUTOFMEMORY)
      {
      size_t freed = GEngineDD->FreeOnDemandSystemMemoryLowLevel(byteSize);
        if (freed!=0) goto RetryV;
      }
      // debugging opportunity
      LogF("Error during v-buffer creation: error %x",err);
      size_t freed = GEngineDD->FreeOnDemandSystemMemoryLowLevel(byteSize);
      if (freed!=0) goto RetryV;
      RptF("Error during v-buffer creation: error %x",err);
      GEngineDD->DDError9("CreateVertexBuffer",err);
      GEngineDD->RequestFlushMemoryCritical();
      ReportMemoryStatusWithVRAM();
      return buffer;
    }
  DoAssert(buffer);
  return buffer;
}

#define V_OFFSET_CUSTOM 1



void VertexStaticData9::Dealloc()
{
}

static const Vector3 V3One(1,1,1);

#if _PROFILE
# define INLINE_PGO_BUG __forceinline
#else
/// with PGO we see a bug when the function is marked as inline
# define INLINE_PGO_BUG 
#endif

static INLINE_PGO_BUG void CopyNormal(VectorCompressed &tgt, Vector3Compressed src)
{
  // TODO: optimization possible
  Vector3 scaledX = (V3One - src.Get()) * 0.5f;
  tgt.v = FAST_D3DRGBA_SAT(scaledX.X(), scaledX.Y(), scaledX.Z(), 0);
}

void *VertexStaticData9::Lock(ComRef<IDirect3DVertexBuffer9> buffer, int size)
{
  DWORD flags = NoSysLockFlag;
RetryV:
  void *data = NULL;
  HRESULT err = GEngineDD->Lock(buffer, 0, size, &data, flags);
  if (err!=D3D_OK || !data)
  {
    if (GEngineDD->FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto RetryV;
    ErrF("VB Lock failed, %x,%s",err,DXGetErrorString9(err));
    ReportMemoryStatusWithVRAM();
    return NULL;
  }
  return data;
}

/*!
\patch 1.24 Date 9/21/2001 by Ondra
- Fixed: Lighting of polygons by point lights was reversed on HW T&L.
\patch 1.32 Date 11/26/2001 by Ondra
- Fixed: Destroyed buildings did not collapse with HW T&L.
*/


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

bool EngineDD9::CanCreateBuffersAsync() const
{
  return MipLoadingAsync && Glob.config._useCB && GFileServerFunctions->OtherThreadsCanUse() && CheckExThreads(4,GetCPUCount()>=4);
}

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

template <int vertexDecl>
void VertexBufferD3D9::PrepareUV(const VertexTable &src)
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

template <int vertexDecl, class VertexType>
class AddShapeOpBase : public VirtDelegate0<void>
{
protected:
  mutable Ref<VertexStaticData9> _result;

  Ref<const VertexTableData> _data;
  Ref<VertexBufferD3D9> _vbuf;
  bool _frequent;
  bool _dynamic;
  
protected:
  AddShapeOpBase(const VertexTable &src, VertexBufferD3D9 *vbuf, bool frequent, bool dynamic)
  :_data(src.GetVertexTableData()),_vbuf(vbuf),_frequent(frequent),_dynamic(dynamic)
  {
    vbuf->PrepareUV<vertexDecl>(src);
  }

};



template <int vertexDecl>
class AddShapeOp: public AddShapeOpBase<vertexDecl, typename VertexDeclTraits<vertexDecl>::VertexType >
    {
  typedef AddShapeOpBase<vertexDecl, typename VertexDeclTraits<vertexDecl>::VertexType > base;
  
  public:
  AddShapeOp(const VertexTable &src, VertexBufferD3D9 *vbuf, bool frequent, bool dynamic)
  :base(src,vbuf,frequent,dynamic)
    {
    }
    
  Ref<VertexStaticData9> CreateData(bool async)
    {
    // Either we didn't fit into existing or we use a separate buffer, so create a new buffer
    _result = new VertexStaticData9(_data->NVertex(), sizeof(VertexDeclTraits<vertexDecl>::VertexType), _frequent);
    
    if (async)
    {
      _result->InitFuture();
      return _result;
    }
    else
    {
      ComRef<IDirect3DVertexBuffer9> buffer = CreateBuffer();
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
    PROFILE_SCOPE_EX(vssCB,dd9);
    ComRef<IDirect3DVertexBuffer9> buffer = CreateBuffer();
    _result->SetFuture(buffer);
  }

protected:
  ComRef<IDirect3DVertexBuffer9> CreateBuffer() const
  {
    PROFILE_SCOPE_EX(vssCB,dd9);
    ComRef<IDirect3DVertexBuffer9> buffer = _result->CreateBuffer();
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
    VertexStaticData9::AddShapeData<vertexDecl>(dataMem, *_data, _vbuf);

    // Process the data (Unlock)
    buffer->Unlock();

    return buffer;
  }
};


/// used when filling info for a unused stage

template <>
void VertexStaticData9::AddShapeData<VD_Position_Normal_Tex0>(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf)
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
void VertexStaticData9::AddShapeData<VD_Position_Normal_Tex0_ST>(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf)
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
void VertexStaticData9::AddShapeData<VD_Position_Normal_Tex0_Tex1>(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf)
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
void VertexStaticData9::AddShapeData<VD_Position_Normal_Tex0_Tex1_ST>(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf)
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
void VertexStaticData9::AddShapeData<VD_Position_Normal_Tex0_ST_Float3>(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf)
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
#else
    sData->custom = ConvertToP(*custom);
#endif
    custom++;
    sData++;
    st++;
  }
}

template <>
void VertexStaticData9::AddShapeData<VD_ShadowVolume>(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf)
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

#include "../rtAnimation.hpp"

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

template <>
void VertexStaticData9::AddShapeData<VD_Position_Normal_Tex0_WeightsAndIndices>(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf)
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
void VertexStaticData9::AddShapeData<VD_Position_Normal_Tex0_ST_WeightsAndIndices>(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf)
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
void VertexStaticData9::AddShapeData<VD_Position_Normal_Tex0_Tex1_WeightsAndIndices>(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf)
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
void VertexStaticData9::AddShapeData<VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices>(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf)
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
void VertexStaticData9::AddShapeData<VD_ShadowVolumeSkinned>(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf)
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



template <int vertexDecl>
class AddShapeInstancedOp: public AddShapeOpBase<vertexDecl, typename VertexDeclInstancedTraits<vertexDecl>::VertexType >
{
  typedef AddShapeOpBase<vertexDecl, typename VertexDeclInstancedTraits<vertexDecl>::VertexType > base;

  int _instancesCount;

public:
  AddShapeInstancedOp(const VertexTable &src, VertexBufferD3D9 *vbuf, int instancesCount, bool frequent, bool dynamic)
  :base(src,vbuf,frequent,dynamic),_instancesCount(instancesCount)
  {
  }

  Ref<VertexStaticData9> CreateData(bool async)
    {
    Assert(!_dynamic); // dynamic data are never instanced

    // Either we didn't fit into existing or we use a separate buffer, so create a new buffer
    _result = new VertexStaticData9(_data->NVertex() * _instancesCount, sizeof(VertexDeclInstancedTraits<vertexDecl>::VertexType), _frequent);

    if (async)
    {
      _result->InitFuture();
      return _result;
    }
    else
    {
      ComRef<IDirect3DVertexBuffer9> buffer = CreateBuffer();
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
    PROFILE_SCOPE_EX(vssCB,dd9);
    ComRef<IDirect3DVertexBuffer9> buffer = CreateBuffer();
    _result->SetFuture(buffer);
  }

protected:
  ComRef<IDirect3DVertexBuffer9> CreateBuffer() const
  {
    ComRef<IDirect3DVertexBuffer9> buffer = _result->CreateBuffer();
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
    VertexStaticData9::AddShapeInstancedData<vertexDecl>(dataMem, *_data, _vbuf, _instancesCount);

    // Process the data (Unlock)
    buffer->Unlock();

    return buffer;
  }
};

template <>
void VertexStaticData9::AddShapeInstancedData<VD_Position_Normal_Tex0>(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf, int instancesCount)
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
void VertexStaticData9::AddShapeInstancedData<VD_Position_Normal_Tex0_ST>(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf, int instancesCount)
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
void VertexStaticData9::AddShapeInstancedData<VD_Position_Normal_Tex0_Tex1>(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf, int instancesCount)
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
      sData->t1 = (*uv1).GetUVRaw(i);
      for (int j = 0; j < 4; j++) sData->index[j] = instance;
      sData++;
    }
  }
}

template <>
void VertexStaticData9::AddShapeInstancedData<VD_Position_Normal_Tex0_Tex1_ST>(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf, int instancesCount)
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
void VertexStaticData9::AddShapeInstancedData<VD_ShadowVolume>(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf, int instancesCount)
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

void VertexStaticData9::AddShapeSpriteInstanced(void *data, const VertexTableData &src, int instancesCount)
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

void VertexStaticData9::AddShapePoint(void *data, const VertexTableData &src)
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

bool VertexStaticData9::AddShapeSkinnedInstanced(const Shape &src, bool dynamic, int instancesCount)
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

bool VertexStaticData9::AddShapeNormalSkinnedInstanced(const Shape &src, bool dynamic, int instancesCount)
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

bool VertexStaticData9::AddShapeShadowVolumeSkinnedInstanced(const Shape &src, bool dynamic, int instancesCount)
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

#endif

bool EngineDD9::IsPermanent(IDirect3DVertexBuffer9 *b)
{
  // NULL is always safe (can be considered permanent)
  if (!b) return true;
  // 2D dynamic buffers are also permanent (never discarded)
  for (int i=0; i<lenof(_queueNo._meshBuffer); i++)
  {
#if _ARMA3_VERTEX_BUFFER_EX
    ComRef<IDirect3DVertexBuffer9> buffer = _queueNo._meshBuffer[i].GetResult().GetBuffer(); // we know this Future is ready
#else
    ComRef<IDirect3DVertexBuffer9> buffer = _queueNo._meshBuffer[i].GetResult(); // we know this Future is ready
#endif
    if (buffer == b) return true;
  }
  return false;
}
bool EngineDD9::IsPermanent(IDirect3DIndexBuffer9 *b)
{
  // similar to IsPermanent for IDirect3DVertexBuffer9
  if (!b) return true;
  for (int i=0; i<lenof(_queueNo._indexBuffer); i++)
  {
    ComRef<IDirect3DIndexBuffer9> buffer = _queueNo._indexBuffer[i].GetResult(); // we know this Future is ready
    if (buffer == b) return true;
  }
  return false;
}

void EngineDD9::DeleteVB(VertexStaticData9 *vb)
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
void EngineDD9::DeleteIB(IndexStaticData9 *ib)
{
  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  // similar to DeleteVB
  for (int cb=0; cb<CBStateCount(); cb++)
  {
    //DoAssert(IsPermanent(_cbState[cb]._iBufferLast));
    DoAssert(!_cbState[cb]._iBufferLast.IsSameFuture(ib->_iBuffer));
  }
}


void ResourceBusy9::Used(int numFrames)
{
  _busyUntil = Glob.frameID+numFrames;
}
bool ResourceBusy9::IsBusy() const
{
  return Glob.frameID<=_busyUntil;
}

template <int vertexDecl>
Ref<VertexStaticData9> EngineDD9::AddShapeVertices(
  const Shape &src, VertexBufferD3D9 &vbuf, bool dynamic, bool separate, bool frequent
  )
{
  PROFILE_SCOPE_DETAIL_EX(ddShV,dd9);

  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());

  CREATE_BUFFER_DATA(VertexStaticData9, AddShapeOp<vertexDecl>, (src, &vbuf, frequent, dynamic));
}

template <int vertexDecl>
Ref<VertexStaticData9> EngineDD9::AddShapeVerticesInstanced(
  const Shape &src, VertexBufferD3D9 &vbuf, bool dynamic, bool separate, bool frequent,
  int instancesCount
  )
  {
  PROFILE_SCOPE_DETAIL_EX(ddSVI,dd9);

  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());

  CREATE_BUFFER_DATA(VertexStaticData9, AddShapeInstancedOp<vertexDecl>, (src, &vbuf, instancesCount, frequent, dynamic));
  }

class AddShapeSpriteInstancedOp : public VirtDelegate0<void>
{
protected:
  mutable Ref<VertexStaticData9> _result;

  Ref<const VertexTableData> _data;
  bool _frequent;
  int _instancesCount;
  
  public:
  
  AddShapeSpriteInstancedOp(const VertexTable &src, bool frequent, int instancesCount)
  :_data(src.GetVertexTableData()),_frequent(frequent),_instancesCount(instancesCount) {}

  Ref<VertexStaticData9> CreateData(bool async)
  {
    int bufferVertexCount = 4 * _instancesCount;

    _result = new VertexStaticData9(bufferVertexCount, sizeof(SVertexSpriteInstanced9), _frequent);

    if (async)
    {
      _result->InitFuture();
      return _result;
    }
    else
    {
      ComRef<IDirect3DVertexBuffer9> buffer = CreateBuffer();

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
    PROFILE_SCOPE_EX(vssCB,dd9);
    ComRef<IDirect3DVertexBuffer9> buffer = CreateBuffer();
    _result->SetFuture(buffer);
  }

protected:
  ComRef<IDirect3DVertexBuffer9> CreateBuffer() const
  {
    ComRef<IDirect3DVertexBuffer9> buffer = _result->CreateBuffer();
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
    VertexStaticData9::AddShapeSpriteInstanced(dataMem, *_data, _instancesCount);

    // Process the data (Unlock)
    buffer->Unlock();

    return buffer;
  }
};


Ref<VertexStaticData9> EngineDD9::AddShapeVerticesSpriteInstanced(
  const Shape &src, VertexBufferD3D9 &vbuf, bool dynamic, bool separate, bool frequent, int instancesCount
  )
{
  Assert(!dynamic);
  if (dynamic) return NULL;

  CREATE_BUFFER_DATA(VertexStaticData9, AddShapeSpriteInstancedOp, (src, frequent, instancesCount));
}


class AddShapePointOp : public VirtDelegate0<void>
{
  mutable Ref<VertexStaticData9> _result;

  Ref<const VertexTableData> _data;
  bool _frequent;
  
public:
  
  AddShapePointOp(const VertexTable &src, bool frequent)
  :_data(src.GetVertexTableData()), _frequent(frequent) {}
  
  Ref<VertexStaticData9> CreateData(bool async)
  {
    int bufferVertexCount = 4 * _data->NVertex();

    _result = new VertexStaticData9(bufferVertexCount, sizeof(SVertexPoint9), _frequent);

    if (async)
    {
      _result->InitFuture();
      return _result;
    }
    else
    {
      ComRef<IDirect3DVertexBuffer9> buffer = CreateBuffer();

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
    PROFILE_SCOPE_EX(vssCB,dd9);
    ComRef<IDirect3DVertexBuffer9> buffer = CreateBuffer();
    _result->SetFuture(buffer);
  }

protected:
  ComRef<IDirect3DVertexBuffer9> CreateBuffer() const
  {
    ComRef<IDirect3DVertexBuffer9> buffer = _result->CreateBuffer();
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
    VertexStaticData9::AddShapePoint(dataMem, *_data);

    // Process the data (Unlock)
    buffer->Unlock();

    return buffer;
  }
};

Ref<VertexStaticData9> EngineDD9::AddShapeVerticesPoint(const Shape &src, VertexBufferD3D9 &vbuf, bool dynamic, bool separate, bool frequent)
  {
  if (dynamic) return NULL;
  
  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());

  CREATE_BUFFER_DATA(VertexStaticData9, AddShapePointOp, (src, frequent));
  }

#if _ENABLE_SKINNEDINSTANCING

VertexStaticData9 *EngineDD9::AddShapeVerticesSkinnedInstanced(
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
  VertexStaticData9 *sd = new VertexStaticData9;
  sd->Init(this, bufferVertexCount, sizeof(SVertexSkinned9), _d3DDevice);

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

VertexStaticData9 *EngineDD9::AddShapeVerticesNormalSkinnedInstanced(
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
  VertexStaticData9 *sd = new VertexStaticData9;
  sd->Init(this, bufferVertexCount, sizeof(SVertexNormalSkinned9), _d3DDevice);

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

VertexStaticData9 *EngineDD9::AddShapeVerticesShadowVolumeSkinnedInstanced(
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
  VertexStaticData9 *sd = new VertexStaticData9;
  sd->Init(this, bufferVertexCount, sizeof(SVertexShadowVolumeSkinned9), _d3DDevice);

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

#endif

//////////////////////{{


IndexStaticData9::IndexStaticData9(int indices) : _iBuffer(BufferFuture::Null)
{
  _count = indices;
  GEngineDD->AddIBuffersAllocated(_count * sizeof(VertexIndex));
}

IndexStaticData9::~IndexStaticData9()
{
  Dealloc();
  GEngineDD->AddIBuffersAllocated(-_count * sizeof(VertexIndex));
}

void IndexStaticData9::Init(ComRef<IDirect3DIndexBuffer9> buffer)
{
  _iBuffer = BufferFuture(buffer);
}
void IndexStaticData9::InitFuture()
  {
  _iBuffer = BufferFuture();
}
void IndexStaticData9::SetFuture(ComRef<IDirect3DIndexBuffer9> buffer)
{
  _iBuffer.SetResult(buffer);
}
ComRef<IDirect3DIndexBuffer9> IndexStaticData9::CreateBuffer()
{
  ComRef<IDirect3DIndexBuffer9> buffer;

  if (_count <= 0) return buffer;

  MyD3DDevice &dev = GEngineDD->GetDirect3DDevice();

  int byteSize = _count*sizeof(VertexIndex);
RetryI:
  EngineDD9::CheckMemTypeScope scope(GEngineDD, "IBstatic", byteSize);
    PROFILE_SCOPE_DETAIL_EX(ddCIB,dd9);
    HRESULT err = dev->CreateIndexBuffer(byteSize,WRITEONLYIB,D3DFMT_INDEX16,D3DPOOL_DEFAULT,buffer.Init(),NULL);
    if (err!=D3D_OK)
    {
      if (err==D3DERR_OUTOFVIDEOMEMORY)
      {
        LogF("Out of VRAM during i-buffer creation");
        if (GEngineDD->TextBankDD()->ForcedReserveMemory(byteSize))
        {
          goto RetryI;
        }
      }
      else if (err==E_OUTOFMEMORY)
      {
      size_t freed = GEngineDD->FreeOnDemandSystemMemoryLowLevel(byteSize);
        if (freed!=0) goto RetryI;
      }
      LogF("Error during i-buffer creation: error %x",err);
      // debugging opportunity
    size_t freed = GEngineDD->FreeOnDemandSystemMemoryLowLevel(byteSize);
      if (freed!=0) goto RetryI;
      RptF("Error during i-buffer creation: error %x",err);
      ReportMemoryStatusWithVRAM();
    GEngineDD->RequestFlushMemoryCritical();
    GEngineDD->DDError9("CreateIndexBuffer",err);
    return buffer;
    }
  DoAssert(buffer);
  return buffer;
  }

#define V_OFFSET_CUSTOM 1


void IndexStaticData9::Dealloc()
{
}

int IndexStaticData9::IndicesNeededTotal(const FaceData &src, int nInstances)
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

int IndexStaticData9::IndicesNeededStrippedTotal(const FaceArray &src, int nInstances)
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


void *IndexStaticData9::Lock(ComRef<IDirect3DIndexBuffer9> buffer, int size)
  {
RetryI:
    void *data = NULL;
  HRESULT err = GEngineDD->Lock(buffer, 0, size, &data,NoSysLockFlag);
    if (err!=D3D_OK || !data)
    {
    if (GEngineDD->FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto RetryI;
      ErrF("IB Lock failed, %x:%s",err,DXGetErrorString9(err));
    ReportMemoryStatusWithVRAM();
    return NULL;
  }
  return data;
}

bool IndexStaticData9::AddShape(ComRef<IDirect3DIndexBuffer9> buffer, const FaceData &srcDta, int indicesTotal, bool dynamic, int nInstances, int instanceVertices, const AutoArray<Offset> &sectionsEnds)
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
              buffer->Unlock();
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
    buffer->Unlock();
  }
  return true;
}

/*
void IndexStaticData9::ScanMinMax(int &minI, int &maxI, int start, int size) const
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

bool IndexStaticData9::AddShapeSprite(ComRef<IDirect3DIndexBuffer9> buffer, const FaceData &src, int indicesTotal, bool dynamic, int nInstances)
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
    buffer->Unlock();
  }
  return true;
}

bool IndexStaticData9::AddShapePoint(ComRef<IDirect3DIndexBuffer9> buffer, const FaceData &src, int indicesTotal, bool dynamic, int nInstances, int instanceVertices)
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
    buffer->Unlock();
  }
  return true;
}

// predecessor of functors creating the index buffer
class AddShapeIndicesOpBase : public VirtDelegate0<void>
{
protected:
  Ref<const FaceData> _data;
  bool _dynamic;
  int _nInstances;

  Ref<IndexStaticData9> _result;

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

  Ref<IndexStaticData9> CreateData(bool async)
  {
    // Get the number of indices
    int indicesTotal = IndexStaticData9::IndicesNeededTotal(*_data, _nInstances);
    // It's a nonsense to have zero indices
    Assert(indicesTotal > 0);

    _result = new IndexStaticData9(indicesTotal);
    if (async)
    {
      _result->InitFuture();
      return _result;
    }
    else
    {
      ComRef<IDirect3DIndexBuffer9> buffer = _result->CreateBuffer();
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
    PROFILE_SCOPE_EX(issCB,dd9);
    ComRef<IDirect3DIndexBuffer9> buffer = _result->CreateBuffer();

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

Ref<IndexStaticData9> EngineDD9::AddShapeIndices(const FaceArray &src, bool dynamic, int nInstances, int instanceVertices)
{
  PROFILE_SCOPE_DETAIL_EX(ddShI,d3d);

  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());

  CREATE_BUFFER_DATA(IndexStaticData9, AddShapeIndicesOp, (src, dynamic, nInstances, instanceVertices));
}

class AddShapeIndicesSpriteOp : public AddShapeIndicesOpBase
{
protected:
  typedef AddShapeIndicesOpBase base;

public:
  AddShapeIndicesSpriteOp(const FaceArray &src, bool dynamic, int nInstances)
    : base(src, dynamic, nInstances)
  {
  }

  Ref<IndexStaticData9> CreateData(bool async)
{
  // Get the number of indices
    int indicesTotal = 6 * _nInstances;
  // It's a nonsense to have zero indices
  Assert(indicesTotal > 0);

    _result = new IndexStaticData9(indicesTotal);
    if (async)
    {
      _result->InitFuture();
      return _result;
    }
    else
    {
      ComRef<IDirect3DIndexBuffer9> buffer = _result->CreateBuffer();
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
    PROFILE_SCOPE_EX(issCB,dd9);
    ComRef<IDirect3DIndexBuffer9> buffer = _result->CreateBuffer();

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

Ref<IndexStaticData9> EngineDD9::AddShapeIndicesSprite(const FaceArray &src, bool dynamic, int nInstances)
{
  PROFILE_SCOPE_DETAIL_EX(ddSIS,d3d);

  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());

  CREATE_BUFFER_DATA(IndexStaticData9, AddShapeIndicesSpriteOp, (src, dynamic, nInstances));
}

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

  Ref<IndexStaticData9> CreateData(bool async)
  {
  // Get the number of indices
    int indicesTotal = 6 * _instanceVertices;
  // It's a nonsense to have zero indices
  Assert(indicesTotal > 0);

    _result = new IndexStaticData9(indicesTotal);
    if (async)
    {
      _result->InitFuture();
      return _result;
    }
    else
    {
      ComRef<IDirect3DIndexBuffer9> buffer = _result->CreateBuffer();
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
    PROFILE_SCOPE_EX(issCB,dd9);
    ComRef<IDirect3DIndexBuffer9> buffer = _result->CreateBuffer();

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

Ref<IndexStaticData9> EngineDD9::AddShapeIndicesPoint(const FaceArray &src, bool dynamic, int nInstances, int instanceVertices)
{
  PROFILE_SCOPE_DETAIL_EX(ddSIP,d3d);
  DoAssert(nInstances == 1);

  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());

  CREATE_BUFFER_DATA(IndexStaticData9, AddShapeIndicesPointOp, (src, dynamic, nInstances, instanceVertices));
}

///////////////////}}

//-----------------------------------------------------------------------------
template<class T> VertexDynamicData9<T>::VertexDynamicData9()
{
#ifdef DONT_USE_DYNBUFFERS
#else
  _actualVBIndex = 0;
  _vBufferSize = 0;
  _vBufferUsed = 0;
#endif
}

//-----------------------------------------------------------------------------
template<class T> void VertexDynamicData9<T>::Init(int size, MyD3DDevice &dev)
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
					GEngineDD->DDError9("CreateVertexBuffer - dynamic",err);
					if (GEngineDD->TextBankDD()->ForcedReserveMemory(byteSize))
					{
						goto RetryV;
					}
				}
				GEngineDD->DDError9("CreateVertexBuffer",err);
				return;
				// 
			}
		}
		_actualVBIndex = 0;
		_vBufferSize = size;
		_vBufferUsed = 0;
	#endif
}

//-----------------------------------------------------------------------------
template<class T> void VertexDynamicData9<T>::Dealloc()
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

#ifdef DONT_USE_DYNBUFFERS

//-----------------------------------------------------------------------------
void VertexDynamicData9<SVertex9>::FillBuffers(const VertexTableAnimationContext *animContext, const Shape &src)
{
	DoAssert(src.IsGeometryLocked());

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

//-----------------------------------------------------------------------------
void VertexDynamicData9<SVertex9_ST>::FillBuffers(const VertexTableAnimationContext *animContext, const Shape &src)
{
	DoAssert(src.IsGeometryLocked());

	// Prepare the auxiliary structure
	const int nVertex = src.NVertex();
	_verticesOnly.Access(nVertex - 1);
	SVertex9_ST *sData = _verticesOnly.Data();

	const UVPairs *uv = &src.GetUVArray();
	const STPair* st  = src.GetSTArray().Data();
	
	if (animContext && animContext->AreVerticesLoaded())
	{
		const Array<Vector3> *pos = &animContext->GetPosArray(&src);
		const Array<Vector3Compressed> *norm = &animContext->GetNormArray();
		for (int i = 0; i < nVertex; i++)
		{
			sData->pos = ConvertToP((*pos)[i]);
			CopyNormal(sData->norm,(*norm)[i]);
			sData->t0 = (*uv).GetUVRaw(i);

			//this will not work correctly as the ST is not animated
			sData->st = st[i];
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
			sData->st = st[i];
			sData++;
		}
	}
}


#endif


//-----------------------------------------------------------------------------
template<class T> int VertexDynamicData9<T>::AddVertices(int cb, const VertexTableAnimationContext *animContext, const Shape &src)
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
  GEngineDD->SetTexGenScaleAndOffset(cb,scale, minUV);

#ifdef DONT_USE_DYNBUFFERS

  // Lock the shape data
  DoAssert(src.IsGeometryLocked());

  // Retrieve the vertex data into some auxiliary structure
	FillBuffers(animContext, src);

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
  T *sData;
  int vOffset = Lock(sData, nVertex);
  if (vOffset >= 0)
  {
    ShapeGeometryLock<> lock(&src);

//To Be Done, but is it necessary?
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

//-----------------------------------------------------------------------------
template<class T> int VertexDynamicData9<T>::Lock(T *&vertex, int n)
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
    err = GEngineDD->Lock(_vBuffer[_actualVBIndex], offset, size, &data, flags);
  }
  if (err!=D3D_OK || !data)
  {
    if (GEngineDD->FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto Retry;
    ErrF("VB Lock failed, %x,%s",err,DXGetErrorString9(err));
    ReportMemoryStatusWithVRAM();
    return -1;
  }

  int ret = _vBufferUsed;
  _vBufferUsed += n;
  vertex = (SVertex9 *)data;
  return ret;
#endif
}

//-----------------------------------------------------------------------------
template<class T> void VertexDynamicData9<T>::Unlock()
{
#ifdef DONT_USE_DYNBUFFERS
#else
  HRESULT err;
  err = _vBuffer[_actualVBIndex]->Unlock();
  if (err != D3D_OK)
  {
    GEngineDD->DDError9("Cannot unlock vertex buffer.", err);
  }
#endif
}

//-----------------------------------------------------------------------------
VertexBufferD3D9::VertexBufferD3D9()
{
  _frameRefreshed = INT_MIN;
  for (int i = 0; i < MAX_UV_SETS; i++)
  {
    _maxUV[i].u = _maxUV[i].v = -1e6;
    _minUV[i].u = _minUV[i].v = 1e6;
  }
}

VertexBufferD3D9::~VertexBufferD3D9()
{
  //LogF("Destroying VB: %x, Track=%x", this, Track());
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*GEngineDD->TextBankDD());

  if (_sharedV)
  {
    GEngineDD->DeleteVB(_sharedV);
    _sharedV.Free();
  }
  if (_sharedI)
  {
    GEngineDD->DeleteIB(_sharedI);
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
    GEngineDD->BufferReleased(this);
  }
  
  // not needed, object will be destroyed anyway, but we prefer destroying pointers explicitely
  SetOwner(NULL);
}

size_t VertexBufferD3D9::GetMemoryControlled() const
{
  size_t vram = 0;
  if (_sharedV) vram += _sharedV->Size() * _sharedV->VertexSize();
  if (_sharedI) vram += _sharedI->GetSize();
  Assert(vram<10*1024*1024);
  return vram;
}

size_t VertexBufferD3D9::GetBufferSize() const
{
  return 0;
}

void EngineDD9::BufferReleased(VertexBufferD3D9 *buf)
{
  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  _vBufferLRU.Delete(buf);
}

void EngineDD9::ProcessLRURequests()
{
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  while(Ref<VertexBufferD3D9> vb = _vbLRURequest.Get())
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


void EngineDD9::OnSizeChanged(VertexBufferD3D9 *buf)
{
  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  _vBufferLRU.RefreshMemoryControlled(buf);
}

Ref<VertexBuffer> EngineDD9::CreateVertexBuffer(const Shape &src, VBType type, bool frequent)
{
  //ThrottleVRAMAllocation(_textBank->GetMaxVRAMAllocation(),_textBank->GetVRamTexAllocation());
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  Ref<VertexBufferD3D9> buffer = new VertexBufferD3D9;
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
    delete buffer;
    return NULL;
  }
}

void EngineDD9::DeferVBufferRelease( VertexBuffer *buffer )
{
  // store the Ref, defer release to the moment we know we own the texture manager and D3D CS
  // a suitable moment is after worker thread flush (FinishFinishPresent)
  _vBufferToRelease.Add(buffer);
}

void EngineDD9::RefreshBuffer(const VertexTable &sMesh)
{
  VertexBufferD3D9 *vb = static_cast<VertexBufferD3D9 *>(sMesh.LockVertexBuffer());
  int now = FreeOnDemandFrameID();
  if (vb->_frameRefreshed==now)
  {
    return;
  }
  vb->_frameRefreshed = now;
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

bool VertexBufferD3D9::Init(EngineDD9 *engine, const Shape &src, VBType type, bool frequent)
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
      saturateMin(nInstances,  GEngineDD->MaxInstances());
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
      saturateMin(nInstances,  GEngineDD->MaxInstances());
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
      saturateMin(nInstances,  GEngineDD->MaxInstances());
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
      saturateMin(nInstances,  GEngineDD->MaxInstances());
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
      saturateMin(nInstances,  GEngineDD->MaxInstances());
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
        _sharedV = GEngineDD->AddShapeVertices<VD_ShadowVolume>(src, *this, _dynamic, type == VBBigDiscardable, frequent);
      }
      else
      {
        _sharedV = GEngineDD->AddShapeVerticesInstanced<VD_ShadowVolume>(src, *this, _dynamic, type == VBBigDiscardable, frequent, nInstances);
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
    _sharedI = GEngineDD->AddShapeIndicesSprite(src.Faces(), _dynamic, nInstances);
  }
  else if (src.GetVertexDeclaration() == VD_Position_Tex0_Color)
  {
    _sharedI = GEngineDD->AddShapeIndicesPoint(src.Faces(), _dynamic, nInstances, src.NVertex());
  }
  else
  {
    _sharedI = GEngineDD->AddShapeIndices(src.Faces(), _dynamic, nInstances, src.NVertex());
  }
  if (!_sharedI)
  {
    if (_sharedV)
    {
      GEngineDD->DeleteVB(_sharedV);
      _sharedV = NULL;
    }
    return false;
  }

  PROFILE_SCOPE_DETAIL_EX(ddVIS,dd9);
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

void VertexBufferD3D9::Update(int cb, const VertexTableAnimationContext *animContext, const Shape &src, const EngineShapeProperties &prop, VSDVersion vsdVersion)
{
  if (!_dynamic)
  {
    // use static buffer as source
    GEngineDD->SetVSourceStatic(cb, src, prop, *this, vsdVersion);
  }
  else
  {
    // update dynamic buffer - and use it as source
    GEngineDD->SetVSourceDynamic(cb,animContext, src, prop, *this);
  }

  // note: no need to set indices / vertices when drawing same shape again
  // this can be quite common

}

void VertexBufferD3D9::Detach(const VertexTable &src)
{
  DoAssert(GetOwner()==NULL || &src==GetOwner());
  // we need to detach destructed Shape, it can be accessed from _vBufferLRU if other reference to this persists
  SetOwner(NULL);
}

void VertexBufferD3D9::RemoveIndexBuffer()
{
  if (_sharedI)
  {
    GEngineDD->DeleteIB(_sharedI);
    _sharedI.Free();
  }
}

bool VertexBufferD3D9::IsGeometryLoaded() const
{
  // dynamic buffers do not contain any geometry information
  return !_dynamic;
}

void VertexBufferD3D9::OnSizeChanged()
{
  GEngineDD->OnSizeChanged(this);
}

int EngineDD9::ShapePropertiesNeeded(const TexMaterial *mat) const
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

void EngineDD9::SetTexGenScaleAndOffset(int cb, const UVPair *scale, const UVPair *offset)
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

void EngineDD9::SetVSourceStatic(int cb, const Shape &src, const EngineShapeProperties &prop, const VertexBufferD3D9 &buf, VSDVersion vsdVersion)
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

  VertexStaticData9 *dtaV = buf.GetSharedV();
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
  IndexStaticData9 *dtaI = buf.GetSharedI();
  if (dtaI)
  {
    dtaI->Used();
  }
  int vOffset = 0;

  if (dtaI && (!cbState._iBufferLast.IsSameFuture(dtaI->_iBuffer) || cbState._vOffsetLast!=vOffset))
  {
    ADD_COUNTER(d3xIB,1);
    CALL_CB(cb,SetIndices,dtaI->_iBuffer);
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

void EngineDD9::SetVSourceDynamic(
  int cb, const VertexTableAnimationContext *animContext, const Shape &src, const EngineShapeProperties &prop, const VertexBufferD3D9 &buf
)
{
  PROFILE_DX_SCOPE_DETAIL(3dvbD);

  // Zero vertex and index buffer in the device, forget _iBufferLast and _vBufferLast.
  ForgetDeviceBuffers(cb);

#ifdef DONT_USE_DYNBUFFERS
	if (src.GetVertexDeclaration() == VD_Position_Normal_Tex0_ST)
		_dynVBuffer_SVertex9_ST.AddVertices(cb,animContext, src);
	else
		_dynVBuffer_SVertex9.AddVertices(cb,animContext, src);

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
    IndexStaticData9 *dtaI = buf._sharedI;
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
/*
#define SET_COLOR(d,s) d.r = s.R(),d.g = s.G(),d.b = s.B(),d.a = s.A()
#define SET_WHITE(d,av) d.r = d.g = d.b = 1, d.a = av
#define SET_BLACK(d,av) d.r = d.g = d.b = 0, d.a = av
#define SET_POS(d,s) d.x = s.X(),d.y = s.Y(),d.z = s.Z()
*/

float EngineDD9::GetLastKnownAvgIntensity() const
{
  // verify the background thread has finished and will no longer change this value
  Assert(_backgroundD3D!=RecHighLevel);
  return _lastKnownAvgIntensity;
}

void EngineDD9::DoForgetAvgIntensity()
{
  _lastKnownAvgIntensity = -1;
  // force update
  _accomFactorPrev = -1;

  // Reset old queries (_pp._glow can be null in case the reset is in progress)
  if (_pp._glow.NotNull()) _pp._glow->CancelQueries();
}
void EngineDD9::ForgetAvgIntensity()
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

float EngineDD9::GetSmoothFrameDuration() const
{
  // average for last three frames is a good basis for simulation timing
  // using last frame time can be bad, because:
  //   - D3D uses 3 frames command buffer with periodic flush
  //   - there can be some other one frame spikes
  return GetAvgFrameDuration(SmoothFrames)*0.001f;
}
float EngineDD9::GetSmoothFrameDurationMax() const
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

#if _VBS3
void EngineDD9::SetFlashBang(float duration)
{
  // Make the duration non zero (avoid zero division later)
  saturateMax(duration, 0.001f);

  // In case some FB is in progress, keep at least it's duration
  if (_fbCurrentIntensity > 0.0f)
  {
    // Get the number of seconds that remains to the previous FB effect
    float previousFBTimeLeft = _fbCurrentIntensity / _fbInvDuration;

    // Saturate the duration to consider the previous effect
    saturateMax(duration, previousFBTimeLeft);
  }

  // Set the FB effect parameters
  _fbCurrentIntensity = 1.0f;
  _fbInvDuration = 1.0f / duration;
  _fbGetScreen = true;
}
#endif

bool EngineDD9::IsWBuffer() const
{
  return false;
}

bool EngineDD9::CanWBuffer() const
{
  return false;
}

void EngineDD9::SetWBuffer(bool val)
{
}

void EngineDD9::SetupLights(int cb, const LightList &lights, int spec, const DrawParameters &dp)
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

		_night = GScene->MainLight()->NightEffect();

		#ifndef ENABLE_DAY_LIGHTS
			// If there is no sun casted on object, the P&S lights should be used with a full strength.
			// This is for instance the UI objects case
			if (spec & DisableSun)
				_night = 1.0f;
			
			if (_night <= 0)
				nLights = 0;
		#endif

    // limit lights by device caps
    saturateMin(nLights, _caps._maxLights - 1);

    // Fill out the lights we want to use on the object,
    // and calculate number of point and spot lights
    cbState._lights.Resize(nLights);
    cbState._pointLightsCount = 0;
    cbState._spotLightsCount = 0;
    for (int i=0; i<nLights; i++)
    {
      // Remember the light
      const Light *light = lights[i];
      cbState._lights[i] = const_cast<Light *>(light);

      // Update number of P&S lights
			LightKind lType = light->GetType();
      if (lType == LTPoint) cbState._pointLightsCount++;
      if (lType == LTSpotLight) cbState._spotLightsCount++;
    }
  }
}

/*!
\patch 1.01 Date 6/18/2001 by Ondra
- Fixed: T&L lights in daytime
*/

#if _ARMA3_NEW_LIGHTING_CFG
Color EngineDD9::ComputeBackReflection(const Color& diffuse)
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

Color EngineDD9::ComputeGroundReflection(const Color& lightAmbient, const Color& matAmbient)
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

void EngineDD9::SetupMaterialLights(int cb, const TLMaterial &mat)
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

inline float PlaneDistance2(const Plane &p1, const Plane &p2)
{
  float d = Square(p1.D()-p2.D());
  d += p1.Normal().Distance2(p2.Normal());
  return d;
}

class Matrix4x4
{
public:
  float _o[4][4];
  //       c  r
  // columns corresponds to aside,up,dir,pos

  // Matrix3 operator has identical argument ordering: row column
  float &Set(int r, int c) {return _o[c][r];}

  float &operator () (int r, int c) {return _o[c][r];}
  const float &operator () (int r, int c) const {return _o[c][r];}

  void SetZero()
  {
    for (int i=0; i<4; i++)
    {
      _o[i][0]=_o[i][1]=_o[i][2]=_o[i][3] = 0;
    }
  }
  void SetIdentity()
  {
    SetZero();
    _o[0][0]=_o[1][1]=_o[2][2]=_o[3][3] = 1;
  }

  void SetNormal(const Matrix4 &m)
  {
    Set(0,0) = m(0,0);
    Set(1,0) = m(1,0);
    Set(2,0) = m(2,0);
    Set(3,0) = 0;

    Set(0,1) = m(0,1);
    Set(1,1) = m(1,1);
    Set(2,1) = m(2,1);
    Set(3,1) = 0;

    Set(0,2) = m(0,2);
    Set(1,2) = m(1,2);
    Set(2,2) = m(2,2);
    Set(3,2) = 0;

    Set(0,3) = m.Position()[0];
    Set(1,3) = m.Position()[1];
    Set(2,3) = m.Position()[2];;
    Set(3,3) = 1;
  }
  void SetPerspective(const Matrix4 &m)
  {
    Set(0,0) = m(0,0);
    Set(1,0) = m(1,0);
    Set(2,0) = m(2,0);
    Set(3,0) = 0;

    Set(0,1) = m(0,1);
    Set(1,1) = m(1,1);
    Set(2,1) = m(2,1);
    Set(3,1) = 0;

    Set(0,2) = m(0,2);
    Set(1,2) = m(1,2);
    Set(2,2) = m(2,2);
    Set(3,2) = 1;

    Set(0,3) = m.Position()[0];
    Set(1,3) = m.Position()[1];
    Set(2,3) = m.Position()[2];;
    Set(3,3) = 0;
  }
  void SetMultiply(const Matrix4x4 &a, const Matrix4x4 &b)
  {
    for (int i=0; i<4; i++ ) for(int j=0; j<4; j++)
    {
      Set(i,j) =
        (
        a(i,0)*b(0,j)+
        a(i,1)*b(1,j)+
        a(i,2)*b(2,j)+
        a(i,3)*b(3,j)
        );
    }

  }
  void Transpose()
  {
    Swap(Set(0,1),Set(1,0));Swap(Set(0,2),Set(2,0));Swap(Set(0,3),Set(3,0));
    Swap(Set(1,2),Set(2,1));Swap(Set(1,3),Set(3,1));
    Swap(Set(2,3),Set(3,2));
  }
};

void EngineDD9::SetWorldView(int cb)
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

void EngineDD9::DoSwitchTL(int cb, TLMode active)
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

    // -----------------------------------
    if (CBState(cb)._vertexDecl.NotNull())
    {
      SetVertexDeclaration(cb,CBState(cb)._vertexDecl);
    }
    // -----------------------------------

#if !defined(_X360SHADERGEN)
    D3DSetRenderState(cb, D3DRS_CLIPPING,TRUE);
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

    DoAssert(_vdNonTL.NotNull());
    SetVertexDeclaration(cb,_vdNonTL);
    DoAssert(_vsNonTL.NotNull());
    SetVertexShader(cb,_vsNonTL);
#if !defined(_X360SHADERGEN)
    D3DSetRenderState(cb, D3DRS_CLIPPING,FALSE);
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
    SetPixelShaderConstantDBSZero(cb, CBState(cb)._matPower);
    SetPixelShaderConstant_A_DFORCED_E_Black(cb);
  }

}

void EngineDD9::DoSetMaterial(int cb, const TLMaterial &mat)
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

// To enable this, change in shaders is required as well (VSC_ProjMatrix multiplication must use the skinnedPosition instead of transformedPosition)
// based on experimental results it seems there is little performance gained, therefore it is probably not worth the effort
//#define _VIEWPROJMATRIXPREMULTIPLIED

//TypeIsSimple(Light *);

/*!
\patch 5137 Date 3/2/2007 by Ondra
- Fixed: Haze was too strong with large viewdistance.

\patch 5164 Date 8/15/2007 by Flyman
- Fixed: Fog on SM3 was too strong - it has got waveform similar to that one in SM2 now
*/
void EngineDD9::ProjectionChanged(int cb)
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

void EngineDD9::EnableSunLight(int cb, EMainLight mainLight)
{
  CBState(cb)._mainLight = mainLight;
}

void EngineDD9::SetFogMode(int cb, EFogMode fogMode)
{
  CBState(cb)._fogMode = fogMode;
}


#if _ENABLE_CHEATS
  static bool UseDepthBias = true;
  static float DepthBiasOffsetCoef = 0.05f;
#else
  const bool UseDepthBias = true;
  const float DepthBiasOffsetCoef = 0.05f;
#endif

void EngineDD9::PrepareShadowOffset(int cb, const EngineShapeProperties &prop)
{
  float offset = prop.GetShadowOffset();
  
  switch (_sbTechnique)
  {
    case SBT_Default:
      break;
    default:
      // techniques which use sloped depth may use a lot lower shadow offset
      if (UseDepthBias) offset *= DepthBiasOffsetCoef;
      break;
  }
  float offsetC[4] = {offset, offset, offset, offset};
  SetVertexShaderConstantF(cb,VSC_ShadowVolumeLength, offsetC, 1);
}

bool EngineDD9::BeginMeshTLInstanced(
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
  VertexBufferD3D9 *vb = static_cast<VertexBufferD3D9 *>(sMesh.LockVertexBuffer());
  if (!vb || !vb->GetSharedV() || !vb->GetSharedI())
  {
    // this may happen when D3DERR_OUTOFVIDEOMEMORY or a similar error prevents VB creation
    return false;
  }

  // check the loaded vertex / index buffer
  VertexStaticData9::BufferFuture vBuffer = vb->GetSharedV()->_vBuffer;
#if _ARMA3_VERTEX_BUFFER_EX
  if (vBuffer.IsReady() && vBuffer.GetResult().GetBuffer() == NULL)
#else
  if (vBuffer.IsReady() && vBuffer.GetResult() == NULL)
#endif
  {
    sMesh.ReleaseVBuffer();
    return false;
  }
  IndexStaticData9::BufferFuture iBuffer = vb->GetSharedI()->_iBuffer;
  if (iBuffer.IsReady() && iBuffer.GetResult() == NULL)
  {
    sMesh.ReleaseVBuffer();
    return false;
  }

  // it should be added while preparing the rendering
  //Assert(vb->IsInList()); // note: the check is not MT safe, another thread may be updating

  // Update also sets the _posScale and _posOffset values
  vb->Update(cb, NULL, sMesh, prop, VSDV_Instancing);

  return true;
}

void EngineDD9::SetupInstances(int cb, const ObjectInstanceInfo *instances, int nInstances)
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
  SetVertexShaderConstantF(cb,FREESPACE_START_REGISTER, (float*)matrices, nInstances * vectorsNum);
}

void EngineDD9::SetupInstancesSprite(int cb, const ObjectInstanceInfo *instances, int nInstances)
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
  SetVertexShaderConstantF(cb,FREESPACE_START_REGISTER, (float*)matrices, nInstances * vectorsNum);
}

void EngineDD9::SetupInstancesShadowVolume(int cb, const ObjectInstanceInfo *instances, int nInstances)
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
  SetVertexShaderConstantF(cb,FREESPACE_START_REGISTER, (float*)matrices, nInstances * vectorsNum);
}

void EngineDD9::DrawMeshTLSectionsInstanced(int cb, const Shape &sMesh, const EngineShapeProperties &prop, int spec, int bias, int instancesOffset, int instancesCount)
{
  // Go through the sections
  for (int i = 0; i < sMesh.NSections(); i++)
  {
    const ShapeSection &sec = sMesh.GetSection(i);

    // Proxy or hidden - don't draw
    if (sec.Special()&(IsHidden|IsHiddenProxy)) continue;

    // Calculate the bias for section
    int secBias = ((sec.Special()&ZBiasMask)/ZBiasStep) * 5;
    int maxBias = max(bias, secBias);

    // Print material

    // distribute by predefined materials
    TLMaterial mat;
    CreateMaterial(mat, HWhite);

    // Prepare and draw section
    sec.PrepareTL(cb,mat, spec, prop);
    DrawSectionTL(cb,sMesh, i, i+1, maxBias, instancesOffset, instancesCount);
  }
}

void EngineDD9::DrawMeshTLInstanced(
  int cb, const Shape &sMesh, const EngineShapeProperties &prop, int spec, const LightList &lights, int bias,
  const ObjectInstanceInfo *instances, int nInstances, float minDist2, const DrawParameters &dp
)
{
  DoAssert(nInstances > 0);

  // Go through all instances in chunks
  VertexBufferD3D9 *vb = static_cast<VertexBufferD3D9 *>(sMesh.LockVertexBuffer());
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
        int newInstances = min(nInstancesInOneStep, nInstances - drawnInstances);

        // Set instances parameters
        if (instances[drawnInstances]._object)
        {
          instances[drawnInstances]._object->SetupInstances(cb,sMesh, &instances[drawnInstances], newInstances);
        }
        else
        {
          SetupInstances(cb,&instances[drawnInstances], newInstances);
        }

        DrawMeshTLSectionsInstanced(cb,sMesh, prop, spec, bias, drawnInstances, newInstances);

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

void EngineDD9::DrawShadowVolumeInstanced(
  int cb, LODShape *shape, int level, const ObjectInstanceInfo *instances, int instanceCount, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
  , int drawListId
#endif
)
{
  DoAssert(instanceCount > 0);

  Assert(shape->IsLevelLocked(level));
  const Shape *shadow = shape->GetLevelLocked(level);

  // Go through all instances in chunks
  VertexBufferD3D9 *vb = static_cast<VertexBufferD3D9 *>(shadow->LockVertexBuffer());
  if (!vb) return;

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
      int newInstances = min(nInstancesInOneStep, instanceCount - drawnInstances);

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

    if (!CanTwoSidedStencil())
    {
      // ----------------
      // Draw front faces

      drawnInstances = 0;
      while (drawnInstances < instanceCount)
      {
        // Get number of instances
        int newInstances = min(nInstancesInOneStep, instanceCount - drawnInstances);

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

void EngineDD9::BeginShadowVolume(int cb, float shadowVolumeSize)
{
	float constants[4] = {shadowVolumeSize, shadowVolumeSize, shadowVolumeSize, shadowVolumeSize};
	SetVertexShaderConstantF(cb,VSC_ShadowVolumeLength, constants, 1);
}

void EngineDD9::BeginShadowVolumeRendering()
{
  // we are already inside a "SetTemporaryRenderTarget" bracket
  // while rendering volume shadows, we need to avoid writing into a color buffer

  // dirty hack: leave _currRenderTargetSurface untouched, just point the real RT to something else

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
    CALL_D3D(SetRenderTarget, 0, _sssmSurface);
	}

	if (!FULL_STENCIL)
		D3DSetRenderState(-1, D3DRS_STENCILENABLE, TRUE);

  // make sure SSSM is not used as a source at the same time
  //SetTexture(-1,TEXID_SHADOWMAP, NULL);
  // Set the depth range to make sure the depth range has not been changed, sets also viewport dimensions
  SetDepthRange(_minZRange,_maxZRange);
}
void EngineDD9::EndShadowVolumeRendering()
{
  CALL_D3D(SetRenderTarget,0, _currRenderTargetSurface);

  // Set the depth range to make sure the depth range has not been changed
  SetDepthRange(_minZRange,_maxZRange);

	if (!FULL_STENCIL)
		D3DSetRenderState(-1, D3DRS_STENCILENABLE, FALSE);

  #if _DEBUG || _PROFILE
    if (Glob.config._renderThread==Config::ThreadingEmulated)
    {
      KickOffCBDebug();
    }
  #endif
}

float EngineDD9::GetClosestZ2(float minDist2)
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
void EngineDD9::SetupWorldViewProjCam(int cb, const PositionRender &modelToWorld)
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
    D3DXMatrixMultiply(&world2Projection, &world2View, &view2Proj);

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
		D3DXVECTOR4 cdir(cameraObjectDirection.X(), cameraObjectDirection.Y(), cameraObjectDirection.Z(), Glob.config.GetMaxInvLinearDepth());
    SetVertexShaderConstantF(cb,VSC_CameraDirectionInvDepth, (float*)&cdir, 1);
  }
}

void EngineDD9::BeginInstanceTL(int cb, const PositionRender &modelToWorld, float minDist2, int spec, const LightList &lights, const DrawParameters &dp)
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

bool EngineDD9::BeginMeshTL(
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
  
  VertexBufferD3D9 *vb = static_cast<VertexBufferD3D9 *>(sMesh.LockVertexBuffer());
  if (!vb)
  {
    // this may happen when D3DERR_OUTOFVIDEOMEMORY or a similar error prevents VB creation
    return false;
  }

  // it should be added while preparing the rendering
  //Assert(vb->IsInList()); // note: the check is not MT safe, another thread may be updating
  // update if necessary
  vb->Update(cb,animContext, sMesh, prop, VSDV_Basic);

  return true;
}

void EngineDD9::ClearLights(int cb)
{
  CBState(cb)._lights.Resize(0); // currently active lights
  if (CBState(cb)._lights.MaxSize()>64) CBState(cb)._lights.Clear();
}

void EngineDD9::EndMeshTL( int cb, const Shape &sMesh )
{
  //_sBuffer = NULL;
  // turn off all lights
  ClearLights(cb);
  VertexBufferD3D9 *vb = static_cast<VertexBufferD3D9 *>(sMesh.GetVertexBuffer());
  vb->Unlock(&sMesh);
}

static Vector3 RoundVector(Vector3Par point, Vector3Par roundpoint, float smTexelSize)
{
  return Vector3(
    roundpoint.X() + toLargeInt((point.X() - roundpoint.X()) / smTexelSize) * smTexelSize,
    roundpoint.Y() + toLargeInt((point.Y() - roundpoint.Y()) / smTexelSize) * smTexelSize,
    roundpoint.Z() + toLargeInt((point.Z() - roundpoint.Z()) / smTexelSize) * smTexelSize
    );
}


float EngineDD9::ComputeShadowFrustumBoundingSphere(float shadowsZ, Vector3 &position, bool rainMap)
{
  const Camera *cam = GScene->GetCamera();

	#ifdef TEST_RAIN_MAP
		if (rainMap)
		{
			position = cam->Position();
			return sqrt(shadowsZ*shadowsZ + shadowsZ*shadowsZ);
		}
	#endif

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


float EngineDD9::ComputeShadowPlaneOffset(Vector3Par position, float sunClosestDistanceFrom0, float radius, bool rainMap)
{
  // Get the shadow direction
	#ifdef TEST_RAIN_MAP
		Vector3Val shadowDirection = rainMap ? Vector3(0.0f, -1.0f, 0.0f) : GScene->MainLight()->ShadowDirection();
	#else
		Vector3Val shadowDirection = GScene->MainLight()->ShadowDirection();
	#endif

  // Calculate the sphere position distance from 0 in the sun direction
  float d = position.DotProduct(shadowDirection);

  // Difference between this distance and closest-object-to-sun distance is the required offset
  return floatMax(d - sunClosestDistanceFrom0, radius);
}


Matrix4 EngineDD9::ComputeShadowPlaneOrigin(float shadowsZ, float sunClosestDistanceFrom0, int layerIndex)
{
  const Camera *cam = GScene->GetCamera();

	#ifdef TEST_RAIN_MAP
		bool rainMap = layerIndex == RAIN_MAP_FAKE_LAYER;
		int sbSize = rainMap ? RAIN_MAP_SB_SIZE : _sbSize;
		Vector3Val shadowDirection = rainMap ? Vector3(0.0f, -1.0f, 0.0f) : GScene->MainLight()->ShadowDirection();
	#else
		bool rainMap = false;
		int sbSize = _sbSize;
		Vector3Val shadowDirection = GScene->MainLight()->ShadowDirection();
	#endif

  Vector3 position;
  float radius = ComputeShadowFrustumBoundingSphere(shadowsZ, position, rainMap);

  float shadowPlaneOffset = ComputeShadowPlaneOffset(position, sunClosestDistanceFrom0, radius, rainMap);
  
  // Calculate the shadow plane origin
  Matrix4 shadowPlaneOrigin;
  {
    // Move in the local rounded shadow plane space according to current camera position
    Vector3 lrspCameraPos = _oldLRSP[layerIndex].InverseRotation().FastTransform(cam->Position());
    float diameter = radius * 2.0f;
    float smTexelSize = diameter / sbSize;
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

void EngineDD9::PrepareShadowMapRendering(float sunClosestDistanceFrom0, int layerIndex, float shadowsZ, bool firstLayer)
{
  if (_sbQuality > 0)
  {
    Matrix4 shadowPlaneOrigin = ComputeShadowPlaneOrigin(shadowsZ, sunClosestDistanceFrom0, layerIndex);

		bool rainMap = false;
		#ifdef TEST_RAIN_MAP
			rainMap = layerIndex == RAIN_MAP_FAKE_LAYER;
		#endif

    ComputeShadowConstants(shadowsZ, sunClosestDistanceFrom0, shadowPlaneOrigin, rainMap);
  }
}

void EngineDD9::BeginShadowMapRendering(float sunClosestDistanceFrom0, int layerIndex, float shadowsZ, bool firstLayer, bool rainMap)
{
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

  // Set rendering parameters according to selected technique
  switch (_sbTechnique)
  {
  case SBT_Default:
    // offset is now per model - no longer set here
    break;
  case SBT_nVidia:
  case SBT_nVidiaINTZ:
  case SBT_ATIDF16:
  case SBT_ATIDF24:
    {
      // Set the depth biases in render states
      static float fDepthBias = 0.0005f;
      static float fSlopeScaleDepthBias = 0.7f;
#if 0 // _ENABLE_CHEATS
      // to prevent misunderstand, allow tuning values only when they are used
      if (UseDepthBias)
      {
        if( GInput.GetCheat3ToDo(DIK_UP))
        {
          fDepthBias *= 1.1f;
          GlobalShowMessage(500,"DepthBias %f", fDepthBias);
        }
        if( GInput.GetCheat3ToDo(DIK_DOWN))
        {
          fDepthBias *= 1/1.1f;
          GlobalShowMessage(500,"DepthBias %f", fDepthBias);
        }
        if( GInput.GetCheat3ToDo(DIK_LEFT))
        {
          fSlopeScaleDepthBias *= 1.1f;
          GlobalShowMessage(500,"SlopeScaleDepthBias %f", fSlopeScaleDepthBias);
        }
        if( GInput.GetCheat3ToDo(DIK_RIGHT))
        {
          fSlopeScaleDepthBias *= 1/1.1f;
          GlobalShowMessage(500,"SlopeScaleDepthBias %f", fSlopeScaleDepthBias);
        }
        if( GInput.GetCheat3ToDo(DIK_PGUP))
        {
          DepthBiasOffsetCoef *= 1.1f;
          GlobalShowMessage(500,"Shadow offset scale %f", DepthBiasOffsetCoef);
        }
        if( GInput.GetCheat3ToDo(DIK_PGDN))
        {
          DepthBiasOffsetCoef *= 1/1.1f;
          GlobalShowMessage(500,"Shadow offset scale %f", DepthBiasOffsetCoef);
        }
      }
      if( GInput.GetCheat3ToDo(DIK_END))
      {
        UseDepthBias = !UseDepthBias;
        GlobalShowMessage(1000, "Depth bias %s", UseDepthBias? "On" : "Off");
      }
#endif
      if (UseDepthBias)
      {
        DWORD dwDepthBias = *(const DWORD*)&fDepthBias;
        DWORD dwSlopeScaleDepthBias = *(const DWORD*)&fSlopeScaleDepthBias;
        D3DSetRenderState(cb, D3DRS_DEPTHBIAS, dwDepthBias);
        D3DSetRenderState(cb, D3DRS_SLOPESCALEDEPTHBIAS, dwSlopeScaleDepthBias);
      }
      else
      {
        const float zero = 0.0f;
        DWORD dwZero = *(const DWORD*)&zero;
        D3DSetRenderState(cb, D3DRS_DEPTHBIAS, dwZero);
        D3DSetRenderState(cb, D3DRS_SLOPESCALEDEPTHBIAS, dwZero);
      
      }
    }
    break;
  default:
    Fail("Error: Unknown SB technique");
  }

  float radius = ComputeShadowConstants(shadowsZ, sunClosestDistanceFrom0, shadowPlaneOrigin, rainMap);

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
    Vector3Val shadowDirection = rainMap ? Vector3(0.0f, -1.0f, 0.0f) : GScene->MainLight()->ShadowDirection();
    Vector3 tDirection = CBState(cb)._view.Orientation() * shadowDirection;
//     Vector3 tDirection = shadowPlaneOrigin * shadowDirection;
//     tDirection.Normalize();
    D3DXVECTOR4 direction(tDirection.X(), tDirection.Y(), tDirection.Z(), 1.0f);
    SetVertexShaderConstantF(cb,VSC_LDirectionTransformedDir, (float*)direction, 1);
  }

  // Set the SM texture to NULL
  SetTexture(cb,TEXID_SHADOWMAP, NULL);

  // Set shadow buffer as temporary render target
	int sbSize = _sbSize;

	#ifdef TEST_RAIN_MAP 
		if (rainMap)
		{
			SetTemporaryRenderTarget(_rtRainMap, _rtRainMapSurface, _rtRainMapDepthSurface);
			sbSize = RAIN_MAP_SB_SIZE;
		}
		else
	#endif
  SetTemporaryRenderTarget(_renderTargetSB, _renderTargetSurfaceSB, _renderTargetDepthBufferSurfaceSB);

  CALL_D3D(BeginScene,);
  // Some SB techniques use the _renderTargetSB of size 1x1 (whereas the depth-buffer is of proper size)
  // In such cases the viewport will be set during the rendertarget setting with 1x1 values. We want
  // it to have the back buffer resolution.
  // See the label SBSVP
  {
    D3DVIEWPORT9 viewData;
    memset(&viewData,0,sizeof(viewData));
    viewData.X = 0;
    viewData.Y = 0;
    viewData.Width  = sbSize;
    viewData.Height = sbSize;
    viewData.MinZ = 0.0f;
    viewData.MaxZ = 1.0f;
    CALL_D3D(SetViewport,viewData);
  }

  // Clear the buffer
  Clear( true, true, PackedColor(HWhite));
}

float EngineDD9::ComputeShadowConstants(float shadowsZ, float sunClosestDistanceFrom0, Matrix4Val shadowPlaneOrigin, bool rainMap)
{
  const Camera *cam = GScene->GetCamera();

  Vector3 position;
  float radius = ComputeShadowFrustumBoundingSphere(shadowsZ, position, rainMap);

  float shadowPlaneOffset = ComputeShadowPlaneOffset(position, sunClosestDistanceFrom0, radius, rainMap);

  // Set the shadow plane projection parameters
  Matrix4 shadowPlaneProjection;
  float maxDepth = radius + shadowPlaneOffset;
  // avoid exactly Glob.config.shadowsZ, as this is the deepest value represented in the buffer
  //float maxDepth = Glob.config.shadowsZ*1.01f; // radius + shadowPlaneOffset;
  //float maxDepth = Glob.config.shadowsZ;
  float diameter = radius * 2.0f;
  float invDiameter = 1.0f / diameter;

	shadowPlaneProjection = Matrix4(
		invDiameter,  0,            0,                                      0.5,
		0,            -invDiameter, 0,                                      0.5,
		0,            0,            1.0f / maxDepth,                        0);

  // Remember the SM matrix (matrix from view space to shadow plane space)
	if (rainMap)
	{
		//wpos in rain is relative to camera
		const Vector3& camPos = cam->Position();

		Vector3 shPos = shadowPlaneOrigin.Position();
		_rainMapParams._shadowPos = shPos - camPos;
		_rainMapParams._projTrans = Vector3(invDiameter, maxDepth, 0.0f);
	}

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

void EngineDD9::EndShadowMapRendering(float minShadowsZ, float maxShadowsZ, bool firstLayer, bool lastLayer, bool rainMap)
{
  const int cb = -1;

  CALL_D3D(EndScene,);

  // Restore the old render target
  RestoreOldRenderTarget();

  // Restore original setting of the viewport
  // See the label SBSVP
  {
    D3DVIEWPORT9 viewData;
    memset(&viewData,0,sizeof(viewData));
    viewData.X = 0;
    viewData.Y = 0;
    viewData.Width  = _wRT;
    viewData.Height = _hRT;
    viewData.MinZ = 0.0f;
    viewData.MaxZ = 1.0f;
    CALL_D3D(SetViewport,viewData);
  }

  // Restore default bias values
  switch (_sbTechnique)
  {
  case SBT_Default:
    break;
  case SBT_nVidia:
  case SBT_nVidiaINTZ:
  case SBT_ATIDF16:
  case SBT_ATIDF24:
    {
      // Restore default bias values
      D3DSetRenderState(cb, D3DRS_DEPTHBIAS, 0);
      D3DSetRenderState(cb, D3DRS_SLOPESCALEDEPTHBIAS, 0);
    }
    break;
  default:
    Fail("Error: Unknown SB technique");
  }

  // reset the SB rendering flag
  _renderingMode = RMCommon;
		
	#ifdef TEST_RAIN_MAP
		if (rainMap) 
		{
			_pp._SSSM->DoRainMapPP();
		}
		else
	#endif
	{
		// Create the SSSM
		_pp._SSSM->Do(minShadowsZ, maxShadowsZ, firstLayer, lastLayer);

		// Add stencil shadows to the SSSM map
		if (lastLayer && !rainMap)
		{
			_pp._SSSMStencil->Do(false);
		}

		#ifdef SSSMBLUR
			// Create the SSSM blurred map
			if (lastLayer && !rainMap)
			{
				_pp._SSSMB->Do(false);
			}
		#endif
	}

  // Restore the camera projection setting. Note this must be called after _renderingMode was set
  ProjectionChanged(-1);

  // Set the _view back
  DoSwitchTL(-1,TLEnabled);
}

void EngineDD9::RenderStencilShadows()
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

void EngineDD9::EnableDepthOfField(float focusDist, float blur, bool farOnly)
{
  // prevent focusing to zero
  if (focusDist<1e-2f) focusDist = 1e-2f;
  _focusDist = focusDist;
  _blurCoef = blur;
  _farOnly = farOnly;
}

void EngineDD9::EnableDepthOfField(int quality)
{
  // we currently support one quality only
  saturate(quality,0,1);
  // Quality hasn't changed, return
  if (quality == _dbQuality) return;
  // Remember the DOF quality
  _dbQuality = quality;

  // the depth buffer is always created anyway
}

bool EngineDD9::IsDepthOfFieldEnabled() const
{
  if (_blurCoef <= 0.001f) return false;
  if (_dbQuality==0) return false;
  // Engine can't use depth buffer, return
  if (!_dbAvailable) return false;
  return _renderTargetDepthInfo.NotNull();
}

void EngineDD9::BeginDepthMapRendering(bool zOnly
#if _HELISIM_RENDER_TO_TEXTURE
                                       , bool useR2T
#endif
                                       )
{
  // Set depth buffer as temporary render target
  if (zOnly)
  {
    // we use this branch for depth priming when performing depth rendering before MSAA
    // this is done on PC only
    Assert(GetFSAA()>0);
    
    // on PC with FSAA 2nd depth priming pass is z-only, no need to write color buffer
    _renderingMode = RMZPrime;

    // we want the DIP throttling to act the same way in depth/normal rendering
    _dipCount = 0;
    InitSceneInvariants(-1);
    
    // We're rendering prior the main scope, thus we need the BeginScene/EndScene scope
    CALL_D3D(BeginScene,);

    // Set color render target - depth render target must have been already acquired separately
    // color render target is therefore placeholder only, we are not doing any rendering into it
    // it must match the depth stencil however, therefore we use the main RT
#if _HELISIM_RENDER_TO_TEXTURE  
    if (useR2T)
      SetTemporaryRenderTarget(_r2tRT._renderTargetS, _r2tRT._renderTargetSurfaceS, _depthBufferRT/*_r2tRT._depthBufferRT*/);
    else
#endif
      SetTemporaryRenderTarget(_renderTargetS, _renderTargetSurfaceS, _depthBufferRT);

    // Clear the color buffer (the float value), clear the Z buffer
    Clear(true, false, PackedColor(HWhite));
  }
  else
  {
    // Depth priming - rendering of a depth map to be used for SSSM shadows (may be z-priming scene at the same time)
    _renderingMode = RMDPrime;

    // we want the DIP throttling to act the same way in depth/normal rendering
    _dipCount = 0;
    InitSceneInvariants(-1);
    
    // We're rendering prior the main scope, thus we need the BeginScene/EndScene scope
    CALL_D3D(BeginScene,);

    // use depth target, but reuse the same depth buffer as the main scene
    if (GetFSAA()>0)
    {
        SetTemporaryRenderTarget(_renderTargetDepthInfo, _renderTargetDepthInfoSurface, _renderTargetDepthInfoZ);
    }
    else if (_depthBufferRT)
    {
        SetTemporaryRenderTarget(_renderTargetDepthInfo, _renderTargetDepthInfoSurface, _depthBufferRT);
    }
    else
    {
        SetTemporaryRenderTarget(_renderTargetDepthInfo, _renderTargetDepthInfoSurface, _currRenderTargetDepthBufferSurface);
    }

    // WIP: FSAA: do not clear color buffer twice
    // Clear the color buffer (the float value), clear the Z buffer
    Clear(true, true, PackedColor(HWhite));
  }

	//some of objects 
	D3DXVECTOR4 cdir(0, 0, 0, Glob.config.GetMaxInvLinearDepth());
	SetVertexShaderConstantF(-1, VSC_CameraDirectionInvDepth, (float*)&cdir, 1);

	if (!FULL_STENCIL)
		D3DSetRenderState(-1, D3DRS_STENCILENABLE, FALSE);
	
  // mark as if we are rendering in linear space - this is needed because we are recording normal rendering at the same time
  _linearSpace = true;
  #if _DEBUG || _PROFILE
    if (Glob.config._renderThread==Config::ThreadingEmulated)
    {
      KickOffCBDebug();
    }
  #endif
}

void EngineDD9::EndDepthMapRendering(bool zOnly)
{
  if (zOnly)
  {
    // We're rendering prior the main scope, thus we need the BeginScene/EndScene scope
    CALL_D3D(EndScene,);
  }
  else
  {
    // We're rendering prior the main scope, thus we need the BeginScene/EndScene scope
    CALL_D3D(EndScene,);
  }

#if _HELISIM_RENDER_TO_TEXTURE
  if (_r2tBegin)
    CreateR2TDepthAsTexture();
#endif

  // Restore the old render target
  RestoreOldRenderTarget();

  // Set the SB rendering flag
  _renderingMode = RMCommon;

  #if _DEBUG || _PROFILE
    if (Glob.config._renderThread==Config::ThreadingEmulated)
    {
      KickOffCBDebug();
    }
  #endif
}

void EngineDD9::SetDepthRange(float minZ, float maxZ)
{
  _minZRange = minZ;
  _maxZRange = maxZ;

  D3DVIEWPORT9 viewData;
  memset(&viewData,0,sizeof(viewData));
  viewData.X = 0;
  viewData.Y = 0;
  viewData.MinZ = _minZRange;
  viewData.MaxZ = _maxZRange;
  // unless overridden by a cheat, _viewW == _wRT, _viewH == _hRT
  viewData.Width  = _viewW;
  viewData.Height = _viewH;
  CALL_D3D(SetViewport,viewData);
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

void EngineDD9::SetTemperatureTable(const float *table, int gSize, int dotSize, int kSize)
{
  int tableSize = gSize*dotSize*kSize;
  //if (!TemperatureTableUpdateNeeded(table, tableSize)) return;

  D3DLOCKED_RECT locked;
  if (SUCCEEDED(_tableTcSystem->LockRect(0, &locked, NULL, 0)))
  {
    _temperatureTable.Realloc(tableSize);
    _temperatureTable.Resize(tableSize);

    int index = 0;
  for (int j = 0; j < gSize*dotSize; j++)
  {
    float *data = (float*)&(((unsigned char*)locked.pBits)[locked.Pitch * j]);
    for (int i = 0; i < kSize; i++)
    {
      data[i] = table[i + j * kSize];
        _temperatureTable[index++] = data[i];
    }
  }

    _tableTcSystem->UnlockRect(0);

    if (FAILED(_d3DDevice->UpdateTexture(_tableTcSystem, _tableTc)))
    {
      RptF("Error: update texture failed");
    }
  }

  // Save the temperature of the air (UnlitWhite value)
  _airTemperature = table[0 + 2 * kSize];
}

bool EngineDD9::TemperatureTableUpdateNeeded(const float *table, int length)
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

void EngineDD9::SetTIBrightnessRelative(float brightness)
{
  // Consider black-hot case and decrease brightness instead
  if (_tiBlackHot[GetTIMode()])
  {
    brightness = 2.0f * GetTIBrightness() - brightness;
  }

  // Set the TI brightness
  SetTIBrightness(brightness);
}

void EngineDD9::LoadTIConversionTexture(RString name)
{
  // Remember the texture name
  _tiConversionName = name;

  // Name is not valid, finish
  if (name.IsEmpty())
  {
    RptF("Error: Name of the TIConversion texture was not specified");
    return;
  }

  // Open stream and load data
  QOStrStreamData textureSource(name);

  // Get the surface
  ComRef<IDirect3DSurface9> tiConversionSurface;
  if (FAILED(_tiConversion->GetSurfaceLevel(0, tiConversionSurface.Init())))
  {
    RptF("Error: couldn't lock surface of the TIConversion texture");
    return;
  }

  // Load the texture
  D3DXIMAGE_INFO ii;
  if (FAILED(D3DXLoadSurfaceFromFileInMemory(tiConversionSurface, NULL, NULL, textureSource.str(), textureSource.pcount(), NULL, D3DX_FILTER_NONE, 0, &ii)))
  {
      RptF("Error: load surface from memory failed");
      return;
  }

  // Check the dimension to match the expected one
  if ((ii.Width != TIConversionDimensionW) || (ii.Height != TIConversionDimensionH))
  {
    RptF("Warning: TIConversion texture dimension (%dx%d) is not expected, it should be (%dx%d)", ii.Width, ii.Height, TIConversionDimensionW, TIConversionDimensionH);
  }

  // Determine the black-hot rows (so that TI brightness adjusting can work correctly)
  {
    // Create texture in system memory
    ComRef<IDirect3DTexture9> tiConversionSystem;
    if (FAILED(_d3DDevice->CreateTexture(TIConversionDimensionW, TIConversionDimensionH, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, tiConversionSystem.Init(), NULL)))
    {
      RptF("Error: TIConversion texture creation in system memory failed");
      return;
    }

    // Get the surface
    ComRef<IDirect3DSurface9> tiConversionSystemSurface;
    if (FAILED(tiConversionSystem->GetSurfaceLevel(0, tiConversionSystemSurface.Init())))
    {
      RptF("Error: couldn't lock surface in system memory of the TIConversion texture");
      return;
    }

    // Load the texture
    if (FAILED(D3DXLoadSurfaceFromFileInMemory(tiConversionSystemSurface, NULL, NULL, textureSource.str(), textureSource.pcount(), NULL, D3DX_FILTER_NONE, 0, NULL)))
    {
      RptF("Error: load surface from file in memory failed");
      return;
    }

    // Go through all the rows, if the last texel on a row is darker than the first texel, we've got a black-hot
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(tiConversionSystemSurface->LockRect(&lr, NULL, 0)))
    {
    for (int i = 0; i < TIConversionDimensionH; i++)
    {
      const DWORD *row = (const DWORD *)(&((char*)lr.pBits)[i * lr.Pitch]);
      Color first = (PackedColor)row[0];
      Color last = (PackedColor)row[TIConversionDimensionW - 1];
      _tiBlackHot[i] = (first.Brightness() <= last.Brightness()) ? false : true;
    }
    tiConversionSystemSurface->UnlockRect();
  }
    else
    {
      RptF("Error: TI conversion surface lock failed");
      return;
    }
  }
}

void EngineDD9::SetDepthClip(float zClip)
{
  Assert(!IsInCBScope());
  SetPixelShaderConstantF(-1,PSC_DepthClip, D3DXVECTOR4(1.0f, 0.0f, 0.0f, zClip), 1);
}

void EngineDD9::Begin3DRendering()
{
  // Under certain situations (the error message window appear at the beginning?) the Begin3DRendering might be called twice. Finish it first
  //if (_3DRenderingInProgress) End3DRendering();

  // Mark we are going to render 3D
  DoAssert(!_3DRenderingInProgress);
  _3DRenderingInProgress = true;
  //DoAssert(_end3DRequested); // TODO: make this assert working
  _end3DRequested = false;
}

//#define COMMAND_BUFFER_CALCULATION
#ifdef COMMAND_BUFFER_CALCULATION
#endif

void EngineDD9::End3DRendering()
{
  // Mark we finished 3D rendering
  DoAssert(_3DRenderingInProgress);
  _3DRenderingInProgress = false;

}

void EngineDD9::UseCraters(int cb, const AutoArray<CraterProperties> &craters, int maxCraters, const Color &craterColor)
{
  // Get the actual number of craters to draw
  int cratersCount = min(craters.Size(), maxCraters);

  // Create array of craters (fill out rest with zero)
  DoAssert(cratersCount <= MAX_CRATERS);
  Color cr[MAX_CRATERS];

  float visiblity[MAX_CRATERS][4];
  const int TimeToCoolInSec = 60 * 1000;
  const float InvTimeToCoolInSec = 1.0f / TimeToCoolInSec;

  for (int i = 0; i < cratersCount; i++)
  {
    // Calculate the disappear coefficient (number between 0 and 1). Note we know it is never zero
    float disappearCoef = min(2.0f * float(i + 1 + maxCraters - cratersCount) / maxCraters, 1.0f);
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

static __forceinline PackedColor ColorScaleD2(PackedColor col)
{
  // multiply RGB by 0.5, keep A
  return PackedColor(((col&0xfefefe)>>1)|(col&0xff000000));
}

static __forceinline PackedColor ColorScale(PackedColor col) {return col;}


HRESULT EngineDD9::LockDynBuffer(const ComRef<IDirect3DVertexBuffer9> &buf, void * &data, int lockFlags, int lockOffset, int size)
{
  PROFILE_DX_SCOPE_DETAIL(3davC);
  Retry:
  HRESULT err=Lock(buf,lockOffset,size,&data,lockFlags);
  if (err!=D3D_OK || !data)
  {
    if (CheckMainThread() && FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto Retry;
    ErrF("VB Lock failed, %x,%s",err,DXGetErrorString9(err));
    ReportMemoryStatusWithVRAM();
  }
  return err;
}

HRESULT EngineDD9::LockDynBuffer(const ComRef<IDirect3DIndexBuffer9> & ibuf, void * &data, DWORD flags, int indexOffset, int size)
{
  PROFILE_DX_SCOPE_DETAIL(3dfiC);
  HRESULT ret = Lock(ibuf,indexOffset*sizeof(VertexIndex),size,&data,flags);
  return ret;
}

bool EngineDD9::AddVertices( const TLVertex *v, int n )
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
  int lockFlags = 0;
  ComRef<IDirect3DVertexBuffer9> buf;
  // check if we fit within the current VB
  if (_queueNo._vertexBufferUsed+n<=MeshBufferLength && !_queueNo._firstVertex)
  {
    lockOffset = _queueNo._vertexBufferUsed*sizeof(TLVertex);
    lockFlags = NoSysLockFlag|D3DLOCK_NOOVERWRITE;
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

    lockFlags = NoSysLockFlag;
#if CUSTOM_VB_DISCARD
    if (++_queueNo._actualVBIndex>=NumDynamicVB2D)
    {
      _queueNo._actualVBIndex = 0;
    }
#else
    lockFlags |= D3DLOCK_DISCARD;
#endif
    _queueNo._vertexBufferUsed = 0;
#if _ARMA3_VERTEX_BUFFER_EX
    buf = _queueNo.GetVBuffer().GetResult().GetBuffer();  // we know this Future is ready
#else
    buf = _queueNo.GetVBuffer().GetResult();  // we know this Future is ready
#endif
    PROFILE_SCOPE_EX(lbDsc,d3d);
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
      TLVertex *vData = (TLVertex *)data;
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
    err=buf->Unlock();
    if( err!=D3D_OK )  DDError9("Cannot unlock vertex buffer.",err);
  }
  _queueNo._meshBase = _queueNo._vertexBufferUsed;
  _queueNo._meshSize = n;
  _queueNo._vertexBufferUsed += n;
  return true;
}

bool EngineDD9::BeginMesh( int cb, TLVertexTable &mesh, int spec )
{
  Assert(cb<0); // no recording possible here
  SwitchTL(cb,TLEnabled);
  SetWorldView(cb);
  //_mesh = &mesh;

  // Zero vertex and index buffer in the device, forget _iBufferLast and _vBufferLast.
  ForgetDeviceBuffers(cb);

  return AddVertices(mesh.VertexData(),mesh.NVertex());
}

void EngineDD9::EndMesh( int cb, TLVertexTable &mesh )
{
  //_mesh = NULL;
}

Queue9::Queue9()
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

int Queue9::Allocate(TextureD3D9 *tex, int level, const TexMaterial *mat, int spec)
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

void Queue9::Free()
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

void EngineDD9::DoSwitchRenderMode(RenderMode mode)
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

WORD *EngineDD9::QueueAdd(int n )
{
  Assert (_queueNo._triUsed);
  TriQueue9 &triq = _queueNo._tri;
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

void EngineDD9::QueueFan( const VertexIndex *ii, int n )
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

void EngineDD9::AllocateQueue(TextureD3D9 *tex, int level, const TexMaterial *mat, int spec)
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

void EngineDD9::FreeQueue()
{
  // queue is already flushed
  Assert(_queueNo._tri._triangleQueue.Size()==0);
  _queueNo.Free();
}

void EngineDD9::FlushAllQueues()
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

void EngineDD9::FreeAllQueues()
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

void EngineDD9::FlushAndFreeAllQueues()
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

void EngineDD9::CloseAllQueues()
{
  FlushAndFreeAllQueues();
  _queueNo._firstVertex = true; // reset counter to avoid overflow
}

static inline float SafeInv(float x)
{
  return fabs(x)>FLT_MIN ? 1.0f / x : fSign(x)*FLT_MAX;
}

void EngineDD9::FlushQueue()
{
  DoAssert(VerifyLowLevelThreadAccess());
  //SwitchTL(false);
  TriQueue9 &triq = _queueNo._tri;
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
    DoPrepareTriangle(-1,triq._texture, TexMaterialLODInfo(triq._material), triq._level, triq._special, prop);

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
      DWORD flags = NoSysLockFlag;
      IndexStaticData9::BufferFuture ibuf(IndexStaticData9::BufferFuture::Null);
      if (n+_queueNo._indexBufferUsed<=IndexBufferLength && !_queueNo._firstIndex)
      {
#if LOG_LOCKS
        LogF("Index add D3DLOCK_NOOVERWRITE %d + %d",_queueNo._indexBufferUsed,n);
#endif
        // append data
        indexOffset = _queueNo._indexBufferUsed;
        flags |= D3DLOCK_NOOVERWRITE;

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
#if CUSTOM_VB_DISCARD
        if (++_queueNo._actualIBIndex>=NumDynamicVB2D)
        {
          _queueNo._actualIBIndex = 0;
        }
#else
        flags |= D3DLOCK_DISCARD;
#endif
        PROFILE_SCOPE_EX(lbDsc,d3d);
        ibuf = _queueNo.GetIBuffer();
        ret = LockDynBuffer(ibuf.GetResult(), data, flags, indexOffset, size);  // Futures in _queueNo are always ready
      }


      _queueNo._indexBufferUsed = indexOffset+n;
      if (ret==D3D_OK)
      {
        memcpy(data,triq._triangleQueue.Data(),size);
        ibuf.GetResult()->Unlock();  // Futures in _queueNo are always ready
      }
      else
      {
        DDError9("iBuffer->Lock",ret);
      }

      // Fill out vertex buffer
      const VertexStaticData9::BufferFuture &vbuf = _queueNo.GetVBuffer();
      SetStreamSource(-1,vbuf, sizeof(TLVertex), 0);
      //_vertexDecl = NULL;

      // Fill out index buffer
      if (!CBState(-1)._iBufferLast.IsSameFuture(ibuf))
      {
        CBState(-1)._iBufferLast = ibuf;
        CBState(-1)._vOffsetLast = 0;
        ADD_COUNTER(d3xIB,1);
        //PROFILE_DX_SCOPE(3dIND);
        _d3DDevice->SetIndices(ibuf); // Futures in _queueNo are always ready
      }

      PROFILE_DX_SCOPE_DETAIL(3dDI2);
      DoAssert(_d3dFrameOpen || _renderingMode==RMDPrime || _renderingMode==RMZPrime || _backgroundD3D==RecHighLevel);
      _d3DDevice->DrawIndexedPrimitive(
        D3DPT_TRIANGLELIST,
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

void EngineDD9::FlushAndFreeQueue()
{
  FlushQueue();
  FreeQueue();
}

void EngineDD9::Queue2DPoly( const TLVertex *v0, int n )
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

#if _RELEASE
#define DO_COUNTERS 0
#else
#define DO_COUNTERS 0
#endif

#if DO_COUNTERS
struct OptimizeCounter
{
  const char *name;
  int done,skipped;
  int counter;
  OptimizeCounter( const char *n):name(n),done(0),skipped(0),counter(0){}
  ~OptimizeCounter(){ReportSingle();}

  void Skip( int i=1 ){skipped+=i,counter+=i;}
  void Perform( int i=1 ){done+=i,counter+=i;ReportEvery();}
  void ReportSingle()
  {
    LogF("%s: %d optimized to %d",name,skipped+done,done);
    skipped=0;
    done=0;
    counter=0;
  }
  void ReportEvery( int n=1000 )
  {
    if( counter>n ) ReportSingle();
  }
};
#endif


//! Constants that identify occupied bits in dynamic variable identifier
enum DVI
{
  DVIInstancing = 0,
  DVIPointLightsCount = 1,
  DVISpotLightsCount = 3,
  DVIPixelShaderType = 5,
  //DVIDummy = 9,
  DVIRenderingMode = 10,
  DVISectionZPrimingDone = 13,
  DVISectionMaterialLODs = 14,
  // DVISectionNext = DVISectionMaterialLODs+16
  // 
};


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

/*!
\patch 5106 Date 12/20/2006 by Flyman
- Fixed: Filtering was not working properly when using pushbuffers (square artefact on ground were visible under certain conditions)
*/
void EngineDD9::SetHWToUndefinedState(int cb)
{
  /**
  note: we do not want really to set the device into some particular state,
  we only want to set all shadowed values in such a way that any recording will not assume some particular value to be present
  */
  
  // check if we already are in the "undefined" state
  // such calls would be best avoided
  
  PROFILE_SCOPE_DETAIL_EX(hwUnd,*)
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
    //for (int i = 0; i < cbState._renderState.Size(); i++) cbState._renderState[i] = -1;
    for (int p=0; p<NPredicationModes; p++)
    {
      memset(cbState._renderState[p].Data(),-1,cbState._renderState[p].Size()*sizeof(RenderStateInfo));
    }

    // Sampler stages all levels apart of last one
    for (int j = 0; j < TEXID_FIRST; j++)
    {
      // Set render states to undefined state
      //for (int i = 0; i < cbState._samplerState[j].Size(); i++) cbState._samplerState[j][i] = -1;
      memset(cbState._samplerState[j].Data(),-1,cbState._samplerState[j].Size()*sizeof(RenderStateInfo));
    }
  }

  // Set tex0 max and avg colors to impossible values
  cbState._texture0MaxColor = Color(-1,-1,-1,-1);
  cbState._texture0AvgColor = Color(-1,-1,-1,-1);

  // Textures to NULL
  //for (int i = 0; i < MaxSamplerStages; i++) SetTexture(cb, i, NULL);
  // do not really set textures, only forget what was set before
  for (int i = 0; i < MaxSamplerStages; i++) cbState._lastHandle[i] = NULL;

  // Vertex declaration
  cbState._vertexDeclLast.Free();

  // Vertex shader
  for (int p=0; p<NPredicationModes; p++) cbState._vertexShaderLast[p].Free();

  // Pixel shader
  // caution: on X360 NULL pixel shader is a valid value (no RT update)
  // we need to provide some value to force the update
  // the most portable would be some "invalid" shader
  // however as we know _pixelShaderLast is a plain pointer, we will use invalid pointer value instead
  for (int p=0; p<NPredicationModes; p++) cbState._pixelShaderLast[p] = (IDirect3DPixelShader9 *)-1;
  
  // force a new setup in SetStreamSource - for buffers NULL is invalid enough
  cbState._vBufferLast = VertexStaticData9::BufferFuture(VertexStaticData9::BufferFuture::Null);
  cbState._iBufferLast = IndexStaticData9::BufferFuture(IndexStaticData9::BufferFuture::Null);
  cbState._strideLast = 0;

  // Bias
  cbState._bias = INT_MAX;

  // Last stuff set to undefined
  PBDrawingCleanup(cb);

  // Reset vertex and pixel shader constants
  ResetShaderConstantValues(cb);
}

/// allow adding [NN] to a string by using operator []
struct FormatIndex
{
  RString _base;
  FormatIndex(){}
  FormatIndex(RString base):_base(base)
  {
  }
  
  FormatIndex operator [] (int index) const
  {
    BString<256> ret;
    sprintf(ret,"[%d]",index);
    return FormatIndex(_base+RString(ret));
  }
  
  operator const char *() const {return _base;}
};

bool EngineDD9::RendState::ReportDifferences(const RendState &with) const
{
  /**
  @param post indexing information
  
  The macro uses indexing information for both formatting output (using FormatIndex []) and for indexing (direct)
  */
  #define MATCH(a,post,f,DISPLAY) \
    if (a post!=with.a post) \
    { \
      LogF(#a "%s not matching, " #f " != " #f,cc_cast(FormatIndex()post),DISPLAY(a post),DISPLAY(with.a post)); \
      ret = false; \
    }
  #define PASS(x) x
  #define DISPLAY_TEXTURE(x) cc_cast(GEngineDD->GetTextureDebugName(x))
    
  bool ret = true;
  for (int i=0; i<lenof(_lastHandle); i++)
  {
    MATCH(_lastHandle,[i],%s,DISPLAY_TEXTURE)
  }
  for (int i=0; i<lenof(_shaderConstValuesB); i++)
  {
    MATCH(_shaderConstValuesB,[i],%d,PASS)
  }
  for (int i=0; i<lenof(_shaderConstValuesI); i++) for (int j=0; j<4; j++)
  {
    MATCH(_shaderConstValuesI,[i][j],%d,PASS)
  }
  #if 0
  for (int i=0; i<lenof(_shaderConstValues); i++) for (int j=0; j<4; j++)
  {
    MATCH(_shaderConstValues,[i][j],%g,PASS)
  }
  for (int i=0; i<lenof(_pixelShaderConstValues); i++) for (int j=0; j<4; j++)
  {
    MATCH(_pixelShaderConstValues,[i][j],%g,PASS)
  }
  #endif

  for (int i=0; i<lenof(_samplerState); i++) for (int j=0; j<_samplerState[i].Size(); j++)
  {
    MATCH(_samplerState,[i][j],%d,PASS)
  }
  // commented out - predication required a different implementation
//   for (int i=0; i<_renderState.Size(); i++)
//   {
//     MATCH(_renderState,[i],%d,PASS)
//   }
  MATCH(_lastZPrimingDone,,%d,PASS)
  MATCH(_lastRenderingMode,,%d,PASS)
  MATCH(_doSSSM,,%d,PASS)
  MATCH(_currentShadowStateSettings,,%d,PASS)
  

  DoAssert(ret);
  return ret;
    
}

void EngineDD9::PBDrawingCleanup(int cb)
{
  RendState &cbState = CBState(cb);
  // High-level last stuff set to undefined (the previous PB playing might have
  // changed the low-level and the high-level remained unchanged)
  cbState._lastSpec = -1;
  cbState._lastMat = TexMaterialLODInfo((TexMaterial *)-1);
  cbState._lastRenderingMode = (RenderingMode)-1;
  cbState._texGenScaleOrOffsetHasChanged = true;
  cbState._currentShadowStateSettings = SS_None;
}

/**
we assume function name + set of defines is enough to identify the shader
*/

struct ShaderName
{
  RString name;
  AutoArray<RString> defines;
};


struct VSCachedItem
{
  ShaderName name;
  ComRef<IDirect3DVertexShader9> vs;
};

TypeIsMovableZeroed(VSCachedItem);

template <>
struct MapClassTraits<VSCachedItem>: public DefMapClassTraits<VSCachedItem>
{
  /// key type
  typedef const ShaderName &KeyType;
  typedef VSCachedItem Type;
  static unsigned int CalculateHashValue(KeyType key)
  {
    int val = CalculateStringHashValue(key.name);
    for (int i=0; i<key.defines.Size(); i++)
    {
      val = CalculateStringHashValue(key.defines[i],val);
    }
    return val;
  }

  /// compare keys, return negative when k1<k2, positive when k1>k2, zero when equal
  static int CmpKey(KeyType k1, KeyType k2)
  {
    int d = strcmp(k1.name,k2.name);
    if (d) return d;
    d = k1.defines.Size()-k2.defines.Size();
    if (d) return d;
    for (int i=0; i<k1.defines.Size(); i++)
    {
      d = strcmp(k1.defines[i],k2.defines[i]);
      if (d) return d;
    }
    return 0;
  }
  
  static KeyType GetKey(const Type &item) {return item.name;}
  
};

/**
The purpose of this cache is to speed-up subsequent Resets by keeping the shader objects created.
*/
static MapStringToClass<VSCachedItem, AutoArray<VSCachedItem> > VSCache;

// TODO: pass defines in a symbolic binary form instead to reduce memory allocations
static void CreateShaderName(ShaderName &name, const char *pFunctionName, CONST D3DXMACRO *pDefines)
{
  name.name = pFunctionName;
  while (pDefines && pDefines->Name)
  {
    name.defines.Add(pDefines->Name);
    name.defines.Add(pDefines->Definition);
    pDefines++;
  }
}

void EngineDD9::CreateShader(RString filename, CONST D3DXMACRO *pDefines,
  LPCSTR pFunctionName, LPCSTR pProfile, DWORD flags,
  ComRef<IDirect3DVertexShader9> &vertexShader)
{
  ShaderName name;
  CreateShaderName(name,pFunctionName,pDefines);
  const VSCachedItem &item = VSCache.Get(name);
  if (VSCache.NotNull(item))
  {
    vertexShader = item.vs;
    return;
  }
  QOStrStreamDataPrefixedLine stream(filename);
  CreateShader(stream,pDefines,pFunctionName,pProfile,flags,vertexShader);
}

void EngineDD9::CreateShader(LazyLoadFile &hlslSource, CONST D3DXMACRO *pDefines,
                             LPCSTR pFunctionName, LPCSTR pProfile, DWORD flags,
                             ComRef<IDirect3DVertexShader9> &vertexShader)
{
  ShaderName name;
  CreateShaderName(name,pFunctionName,pDefines);
  const VSCachedItem &item = VSCache.Get(name);
  if (VSCache.NotNull(item))
  {
    vertexShader = item.vs;
    return;
  }
  
  // Compile shader
  D3DXCompileShader_Result result;
  HRESULT hr = D3DXCompileShader_Cached(hlslSource, pDefines, &_hlslInclude, pFunctionName, pProfile, flags, result);

  // Create shader and save it
  if (FAILED(hr))
  {
    LogF("Error: %x in D3DXCompileShader while compiling the %s vertex shader", hr, pFunctionName);
    if (result.errorMsgs.NotNull())
    {
    // if there is already some shader it means we are reloading shader only
    // and we may ignore the error
      if (vertexShader.IsNull())
      {
        LogF("%s", (const char*)result.errorMsgs->GetBufferPointer());
        ErrorMessage("Error compiling vertex shader %s", pFunctionName);
      }
    }
  }
  else
  {
    const DWORD *vsData;
    vsData = (const DWORD *)result.shader->GetBufferPointer();
    ComRef<IDirect3DVertexShader9> vs;
    HRESULT hr = _d3DDevice->CreateVertexShader(vsData, vs.Init());

    if (FAILED(hr) || vs.IsNull())
    {
      ErrF("Cannot create vertex shader %s, error %x", pFunctionName, hr);
    }
    else
    {
      vertexShader = vs;
      
      VSCachedItem save;
      save.name = name;
      save.vs = vs;
      VSCache.Add(save);
      
      //       ComRef<ID3DXBuffer> disass;
      //       D3DXDisassembleShader(vsData, false, NULL, disass.Init());
      //       FILE* f = fopen("disassemble.txt", "w");
      //       fwrite(disass->GetBufferPointer(), 1, disass->GetBufferSize(), f);
      //       fclose(f);
    }
  }
}

#define THREADED_SHADER_COMPILER 0

#if THREADED_SHADER_COMPILER
class ShaderCompiler
{
  struct Job: public SLIST_ENTRY
  {
    LazyLoadFile &hlslSource;
    AutoArray<D3DXMACRO> defines;
    ID3DXInclude *hlslInclude;
    LPCTSTR functionName;
    LPCSTR profile;
    DWORD flags;
    Future< ComRef<ID3DXBuffer> > &vertexShader;
    
    Job(
      LazyLoadFile &hlslSource, const Array<D3DXMACRO> &defines, ID3DXInclude *hlslInclude, LPCTSTR functionName, LPCSTR profile,
      DWORD flags, Future< ComRef<ID3DXBuffer> > &vertexShader
    ):hlslSource(hlslSource),defines(defines),hlslInclude(hlslInclude),functionName(functionName),profile(profile),
    flags(flags),vertexShader(vertexShader)
    {
    
    }
  };
  SListMT<Job> _queue;
  Semaphore _queueCount;

  struct ThreadHandle: public SignaledObject
  {
    ThreadHandle():SignaledObject(NoInitHandle){}
  } _thread[32];
  struct ManualResetEvent: public Event
  {
    ManualResetEvent():Event(true){}
  } _terminate;
  DWORD _threadId[32];
  
  static DWORD WINAPI ThreadCallback(void *context);
  void ThreadBody();

  public:
  void StartThreads();
  void StopThreads();
  void AddJob(Job *job){_queue.Push(job);_queueCount.Unlock();}
} GShaderCompiler;

void ShaderCompiler::ThreadBody()
{
  SignaledObject *objects[] ={&_queueCount,&_terminate};
  
  while (SignaledObject::WaitForMultiple(objects,lenof(objects))==0)
  {
    Job *job = _queue.Pop();
    // based on previous EngineDD9::CreateShader
    // no internal caching here - there is no sharing inside of the pool anyway
    
    // Compile shader
    D3DXCompileShader_Result result;
    HRESULT hr = D3DXCompileShader_Cached(
      job->hlslSource.str(), job->hlslSource.pcount(), job->defines.Data(), job->hlslInclude, job->functionName,
      job->profile, job->flags, result);
    // Create shader and save it
    if (FAILED(hr))
    {
      RptF("Error: %x in D3DXCompileShader while compiling the %s vertex shader", hr, job->functionName);
      if (result.errorMsgs.NotNull())
      {
        LogF("%s", (const char*)result.errorMsgs->GetBufferPointer());
      }
    }
    job->vertexShader.SetResult(result.shader);
  }
}



DWORD ShaderCompiler::ThreadCallback(void *context)
{
  ((ShaderCompiler *)context)->ThreadBody();
  return 0;
}

void ShaderCompiler::StartThreads()
{
  _terminate.Reset();
  for (int i=0; i<GetCPUCount(); i++)
  {
    _thread[i].Init( CreateThreadOnCPU(
      256*1024, &ShaderCompiler::ThreadCallback, this, i, THREAD_PRIORITY_NORMAL, Format("ShaderComp%d",i), &_threadId[i]
    ));
  
  }
}

void ShaderCompiler::StopThreads()
{
  _terminate.Set();
  for (int i=0; i<GetCPUCount(); i++)
  {
    _thread[i].Wait();
  }

}

void EngineDD9::CreateShader(LazyLoadFile &hlslSource, const Array<D3DXMACRO> &pDefines,
                             LPCSTR pFunctionName, LPCSTR pProfile, DWORD flags,
                             Future< ComRef<ID3DXBuffer> > &vertexShader)
{
  GShaderCompiler.AddJob(new ShaderCompiler::Job(hlslSource,pDefines,&_hlslInclude,pFunctionName,pProfile,flags,vertexShader));
}

#endif

static inline int MemCmp32(const void *m1, const void *m2, int dwordCount)
{
  const int *mem1 = (const int *)m1;
  const int *mem2 = (const int *)m2;
  while (--dwordCount>=0)
  {
    if (*mem1++!=*mem2++)
    {
      return 1;
    }
  }
  return 0;
}

inline void EngineDD9::SetVertexShaderConstantSAFR(int cb, float matPower, Color matAmbient)
{
  float value[4]={
    matPower, matAmbient.A(), CBState(cb)._fogEnd, 1.0f / (CBState(cb)._fogEnd - CBState(cb)._fogStart)
  };
  SetVertexShaderConstantF(cb,VSC_SpecularPower_Alpha_FogEnd_RCPFogEndMinusFogStart, value, 1);
}

inline void EngineDD9::SetVertexShaderConstantFFAR(int cb)
{
  if (_renderingMode==RMShadowBuffer) return;
  float value[4]={
    0,CBState(cb)._expFogCoef, CBState(cb)._aFogEnd,
    CBState(cb)._aFogEnd > CBState(cb)._aFogStart ? 1.0f / (CBState(cb)._aFogEnd - CBState(cb)._aFogStart) : 1.0f
  };
  SetVertexShaderConstantF(cb,VSC_Free_ExpFog_AFogEnd_RCPAFogEndMinusAFogStart, value, 1);
}

inline void EngineDD9::SetVertexShaderConstantLWSM(int cb)
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

inline void EngineDD9::SetVertexShaderConstantCalmWater( int cb, const RendState &cbState )
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
  float specularCoefSat = min(specularCoef, sunBrightness);
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
  SetVertexShaderConstantF( cb, VSC_CalmWaterPars, GetCalmWaterPars(), CALMWATER_PARS_SIZE );

  // Point and spot lights
  //SetupVSConstantsPSLights(cb);
}

void EngineDD9::SetTextureSBR(int cb, bool alphaPrimitives)
{
  Assert(_renderingMode!=RMShadowBuffer); // should be catched earlier
  Assert(_renderingMode!=RMCrater); // crater RM should be only temporary inside of shader selection functions
  RendState &cbState = CBState(cb);
  if (cbState._tlActive!=EngineDD9::TLEnabled)  // when no 3D, no shadow maps used
  {
    SetTexture(cb,TEXID_SHADOWMAP, NULL, false);
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

void EngineDD9::SetPixelShaderConstantUI(int cb, Vector3Val val, float intensity)
{
  SetPixelShaderConstantF(cb, 25, D3DXVECTOR4(val.X(), val.Y(), val.Z(), intensity), 1);
}

void EngineDD9::SetPixelShaderConstantDBS(int cb, ColorVal matDiffuse, ColorVal matForcedDiffuse, ColorVal matSpecular, float matPower, bool useGroundBidir, bool useNewSpecular)
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
  float specularCoefSat = min(specularCoef, sunBrightness);

  // Calculate specular color (which includes specular light from the Sun only, not from the environment)
  /// multiply by 2 so that pixel shader can avoid _x2
  specular = CBState(cb)._lightSpecular * matSpecular * 2.0f * Color(specularCoefSat, specularCoefSat, specularCoefSat, 1.0f);
  specular.SetA(matPower);

  // Set the pixel shader constant
  SetPixelShaderConstantF(cb, PSC_Diffuse, (float*)difBakSpec, 3);
}

void EngineDD9::SetPixelShaderConstantDBSZero(int cb, float matPower)
{
	Color zeroes[3];
	memset(zeroes, 0, 3*sizeof(Color));
	zeroes[2].SetA(matPower);
  SetPixelShaderConstantF(cb, PSC_Diffuse, (float*) &zeroes[0], 3);
}

void EngineDD9::SetPixelShaderConstant_A_DFORCED_E_Sun(int cb, ColorVal matEmmisive, ColorVal matAmbient, ColorVal matDiffuse, ColorVal matForcedDiffuse)
{
  // temporary solution for SkinShader - _matAmbient, _matEmmisive and matForcedDiffuse holds skin params
  Color matEmissiveHDR;
  Color matForcedDiff;
  Color matAmbi;

	//super shader uses now special emissive texture thus cannot add emissive and ambient color together
	bool superShader = CBState(cb)._vertexShaderID == VSSuper;
		
  if( ( CBState(cb)._vertexShaderID == VSSkin ) || ( CBState(cb)._vertexShaderID == VSTreeAdv ) || ( CBState(cb)._vertexShaderID == VSTreeAdvTrunk ) )
  {
    matEmissiveHDR = HBlack;
    matForcedDiff = HBlack;
    matAmbi = matDiffuse;
  }
  else
  {
    matEmissiveHDR = superShader ? HBlack : matEmmisive * GetAccomodateEye();
    matForcedDiff = matForcedDiffuse;
    matAmbi = matAmbient;
  }

  // AE
  Color newAE = matEmissiveHDR + CBState(cb)._lightAmbient * matAmbi;

  // Set the diffuse back coefficient
  float backCoef = matForcedDiffuse.A();
  newAE.SetA(backCoef);

  // G
#if _ARMA3_NEW_LIGHTING_CFG
  Color newGE = matEmissiveHDR + ComputeGroundReflection(CBState(cb)._lightAmbient, matAmbi);
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
void EngineDD9::SetPixelShaderConstant_A_DFORCED_E_Black(int cb)
{
  // Zero
  Color newE = HBlack;
  Color zero = Color(0, 0, 0, 0);

  // Set the PS constants
  SetPixelShaderConstantF(cb, PSC_AE, (float*)&newE, 1);
  SetPixelShaderConstantF(cb, PSC_GE, (float*)&newE, 1);
  SetPixelShaderConstantF(cb, PSC_DForced, (float*)&zero, 1);
}


void EngineDD9::SetPixelShaderConstant_A_DFORCED_E_NoSun(int cb, ColorVal matEmmisive)
{
  // temporary solution for SkinShader - _matEmmisive holds skin params
  Color matEmissiveCoef;
  if( ( CBState(cb)._vertexShaderID == VSSkin ) || ( CBState(cb)._vertexShaderID == VSTreeAdv ) || ( CBState(cb)._vertexShaderID == VSTreeAdvTrunk ) )
    matEmissiveCoef = Color( 0, 0, 0, 1 );
  else
	{
		//super shader uses now special emissive texture thus cannot add emissive and ambient color together
		bool superShader = CBState(cb)._vertexShaderID == VSSuper;
		matEmissiveCoef  = superShader ? HBlack : matEmmisive * GetAccomodateEye();
	}

  Color newE = matEmissiveCoef;

  // Zero
  Color zero = Color(0, 0, 0, 0);

  // Set the PS constants
  SetPixelShaderConstantF(cb, PSC_AE, (float*)&newE, 1);
  SetPixelShaderConstantF(cb, PSC_GE, (float*)&newE, 1);
  SetPixelShaderConstantF(cb, PSC_DForced, (float*)&zero, 1);
}

void EngineDD9::SetPixelShaderConstantLayers(int cb, int layerMask, const TexMaterial *mat)
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
    float start;
    float end;
    static float SatteliteXYNormalMult = 1.25f;
    if (GLandscape)
    {
      start = GLandscape->GetSatelliteNormalBlendStart();
      end = GLandscape->GetSatelliteNormalBlendEnd();
      
#if _ENABLE_CHEATS

      if (GInput.GetCheat3ToDo(DIK_I))
      {
        SatteliteXYNormalMult += 0.05f;
        DIAG_MESSAGE(250, Format("NormalMult: %.3f", SatteliteXYNormalMult));
      }
    if (GInput.GetCheat3ToDo(DIK_O))
      {
        SatteliteXYNormalMult -= 0.05f;
        DIAG_MESSAGE(250, Format("NormalMult: %.3f", SatteliteXYNormalMult));
        saturateMax(SatteliteXYNormalMult, 0.0f);
      }
#endif
    }
    else
    {
      start = Landscape::DefaultSatelliteNormalBlendStart;
      end = Landscape::DefaultSatelliteNormalBlendEnd;
    }

    float length = end-start;
    if (length <= 0)
      length = 1.0f; // we do not want zero length

    D3DXVECTOR4 vector4(start, length, 0, SatteliteXYNormalMult);
    SetPixelShaderConstantF(cb,PSC_TerrainSatNormDist, vector4, 1);
  }

	#if _HELISIM_FAKE_NIGHT_LIGHTS
		SetPixelShaderNightEmissiveConst(cb, HBlack, true);
	#else
		SetPixelShaderNightEmissiveConst(cb, HBlack, false);
	#endif
}


void EngineDD9::SetPixelShaderConstantWater(int cb)
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
  SetPixelShaderConstantF(cb, PSC_LDirectionGround, (const float*) &cdir, 1);

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

  D3DXVECTOR4 waterwaves1(
	  sin(time1 + H_PI*0.0f)*height + height,	//to have just positive values
	  sin(time1 + H_PI*0.5f)*height + height,
	  sin(time1 + H_PI*1.0f)*height + height,
	  sin(time1 + H_PI*1.5f)*height + height);

  float time2 = (speed2s*invwheather + speed2o)*time;  

  D3DXVECTOR4 waterwaves2(
	  sin(time2 + H_PI*0.0f)*height2 + height2,	//to have just positive values
	  sin(time2 + H_PI*0.5f)*height2 + height2,
	  sin(time2 + H_PI*1.0f)*height2 + height2,
	  sin(time2 + H_PI*1.5f)*height2 + height2);

  SetPixelShaderConstantF(cb, PSC_CalmWaterPars1, (const float*) &waterwaves1, 1);
  SetPixelShaderConstantF(cb, PSC_CalmWaterPars1 + 1, (const float*) &waterwaves2, 1);

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

  D3DXVECTOR4 wheatherparam(
	  invwheather*stormyoffsetV + stormystartV,					//power of vertex normals
	  invwheather*stormyoffsetT + stormystartT,					//power of texture normals
	  spec*maxspecularvis,												//specular visibility
	  maxspecularspot - offspecularspot*GLandscape->GetOvercast());		//specular power
  SetPixelShaderConstantF(cb, PSC_CalmWaterPars1 + 3, (const float*) &wheatherparam, 1);	


  //influence of LdotN according the sun 
  static float LdotNlimit1  = 0.4226;   //(25 degrees)
  static float LdotNlimit0  = -0.1736; //(-10 degrees)

  float LdotNscale   = (sunzenith - LdotNlimit0)/(LdotNlimit1 - LdotNlimit0);
  saturate(LdotNscale, 0.0, 1.0);
  float LdotNoffset  = 1 - LdotNscale;

  //wrap it
  float wraptime = time;

  D3DXVECTOR4 wheatherparam2(
	  invwheather, 
	  LdotNscale, 
	  LdotNoffset, 
	  wraptime);
  SetPixelShaderConstantF(cb, PSC_CalmWaterPars1 + 4, (const float*) &wheatherparam2, 1);	

  //render target size
  D3DXVECTOR4 rtparam(1.0f/_wRT, 1.0f/_hRT, 0.5f/_wRT, 0.5f/_hRT);
  SetPixelShaderConstantF(cb, PSC_CalmWaterPars1 + 2, (const float*) &rtparam, 1);

  // render copy
  //SetFiltering( cb, 10, TFPoint, 2);
  D3DSetSamplerState( cb, 10, D3DSAMP_SRGBTEXTURE, FALSE );
  D3DSetSamplerState( cb, 10, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
  D3DSetSamplerState( cb, 10, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );
  D3DSetSamplerState( cb, 10, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
  D3DSetSamplerState( cb, 10, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
  D3DSetSamplerState( cb, 10, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

  IDirect3DTexture9 *rt = _renderTargetD; // must have been prepared by CaptureRefractionsBackground before
  SetTexture( cb, 10, rt ); 

  SetTexture( cb, 11, GetDepthAsTexture()); 
  D3DSetSamplerState( cb, 11, D3DSAMP_SRGBTEXTURE, FALSE );
  D3DSetSamplerState( cb, 11, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
  D3DSetSamplerState( cb, 11, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );
  D3DSetSamplerState( cb, 11, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
  D3DSetSamplerState( cb, 11, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
  D3DSetSamplerState( cb, 11, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

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

void EngineDD9::SetPixelShaderConstantWaterSimple(int cb)
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

void EngineDD9::SetPixelShaderConstantSkin( int cb, ColorVal matAmbient, ColorVal matDiffuse, 
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

void EngineDD9::SetPixelShaderParamsCalmWater( int cb ) //, ColorVal matSpecular, ColorVal matForcedDiffuse, ColorVal matEmissive)
{
  // set constant regs params (12 * float4 + 16 * float4) - must be separated because of size
  SetPixelShaderConstantF( cb, PSC_CalmWaterPars1, (float*)GetCalmWaterPars(), CALMWATER_PARS_SIZE_PS1 );
  SetPixelShaderConstantF( cb, PSC_CalmWaterPars2, (float*)GetCalmWaterPars2(), CALMWATER_PARS_SIZE_PS2 );

  // set samplers (temporary solution)
  for( int i = 0; i <= 5; i++ )
  {
    SetFiltering( cb, i, TFTrilinear, 2);
    D3DSetSamplerState( cb, i, D3DSAMP_SRGBTEXTURE, FALSE );
    D3DSetSamplerState( cb, i, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP );
    D3DSetSamplerState( cb, i, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP );
  }
  SetFiltering( cb, 6, TFPoint, 2);
  D3DSetSamplerState( cb, 6, D3DSAMP_SRGBTEXTURE, FALSE );
  D3DSetSamplerState( cb, 6, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP );
  D3DSetSamplerState( cb, 6, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP );
  // environmental map
  SetFiltering( cb, 7, TFTrilinear, 2);
  D3DSetSamplerState( cb, 7, D3DSAMP_SRGBTEXTURE, TRUE );
  D3DSetSamplerState( cb, 7, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
  D3DSetSamplerState( cb, 7, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );
  // render copy
  SetFiltering( cb, 10, TFPoint, 2);
  D3DSetSamplerState( cb, 10, D3DSAMP_SRGBTEXTURE, FALSE );
  D3DSetSamplerState( cb, 10, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
  D3DSetSamplerState( cb, 10, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );
  // depth
  SetFiltering( cb, 11, TFPoint, 2);
  D3DSetSamplerState( cb, 11, D3DSAMP_SRGBTEXTURE, FALSE );
  D3DSetSamplerState( cb, 11, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
  D3DSetSamplerState( cb, 11, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );
  // set render target copy and depth to samplers 10 and 11
  IDirect3DTexture9 *rt = _renderTargetD; // must have been prepared by CaptureRefractionsBackground before
  SetTexture( cb, 10, rt );
  SetTexture( cb, 11, GetDepthAsTexture() );
}


void EngineDD9::SetPixelShaderConstantTreeAdv( int cb, ColorVal matAmbient, ColorVal matDiffuse, 
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

void EngineDD9::SetPixelShaderNightEmissiveConst(int cb, ColorVal matEmissive, bool useNightCoef)
{
  static float EmissiveCoeff = 0.15f;

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
	 
	Color emmCol;
	
	if (matEmissive.A() > 1.0f)
	{
		emmCol = matEmissive*matEmissive.A();
	}
	else
	{
		emmCol = matEmissive*GetAccomodateEye();
	}

	float nighEffect = useNightCoef ? sqrtf(GScene->MainLight()->NightEffect() * EmissiveCoeff): 0.0f;
	
	D3DXVECTOR4 lightEmissiveCoef(emmCol.R() + nighEffect, emmCol.G() + nighEffect, emmCol.B() + nighEffect, 0);
	SetPixelShaderConstantF(cb, PSC_NightEmissiveCoef, (float*)&lightEmissiveCoef, 1);
}

void EngineDD9::SetPixelShaderConstantSuper(int cb, ColorVal matSpecular)
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

void EngineDD9::SetPixelShaderConstantTreePRT(int cb, ColorVal matEmmisive, ColorVal matAmbient, ColorVal matDiffuse)
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
    localLightValueDirect[i] += diffuse * max(localVector[i].DotProduct(-tDirection), 0.0f);

    // Include diffuse back color
    localLightValueIndirect[i] += ground * max(-localVector[i].DotProduct(-tDirection), 0.0f);

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

/** can use more sections, can use more than 65536 vertices */
void EngineDD9::DrawIndexedPrimitive(int cb, int beg, int end, const Shape &sMesh, int instancesOffset, int instancesCount)
{
  // no queueing, direct drawing
  // VB should be already locked - TODO: assert
  VertexBufferD3D9 *buf = static_cast<VertexBufferD3D9 *>(sMesh.GetVertexBuffer());

  DoAssert(buf->_sections.Size() >= end);

#ifdef DONT_USE_DYNBUFFERS
  if (buf->_dynamic)
  {
		const void *data;
		int size, stride, triangles;
		
		if (sMesh.GetVertexDeclaration() == VD_Position_Normal_Tex0_ST)
		{
			data			= _dynVBuffer_SVertex9_ST.GetData();
			size			= _dynVBuffer_SVertex9_ST.GetDataSize();
			triangles = _dynVBuffer_SVertex9_ST.GetVerticesCount()/3;
			stride		= _dynVBuffer_SVertex9_ST.GetStride();
		}
		else
		{
			data			= _dynVBuffer_SVertex9.GetData();
			size			= _dynVBuffer_SVertex9.GetDataSize();
			triangles = _dynVBuffer_SVertex9.GetVerticesCount()/3;
			stride		= _dynVBuffer_SVertex9.GetStride();
		}

		#ifndef SUPER_RELEASE
			if (sMesh.GetVertexDeclaration() != VD_Position_Normal_Tex0_ST && sMesh.GetVertexDeclaration() != VD_Position_Normal_Tex0)
			{
				RptF("Error: dynamic buffer render fail, vertex declaration doesn't match : %d", sMesh.GetVertexDeclaration());
			}
		#endif

    Array<char> vertexData(static_cast<char*>(const_cast<void*>(data)), size);
    
		CALL_CB(cb, DrawPrimitiveUP, D3DPT_TRIANGLELIST, triangles, vertexData, stride);

		// using DrawPrimitiveUP means we have reset stream 0
    CBState(cb)._vBufferLast = VertexStaticData9::BufferFuture(VertexStaticData9::BufferFuture::Null);
    CBState(cb)._iBufferLast = IndexStaticData9::BufferFuture(IndexStaticData9::BufferFuture::Null);
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
        PROFILE_DX_SCOPE_DETAIL(3dDIP);
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
            int tCount = min(triangleCount - drawnPrimitives, MaxPrimitiveCount);
            
            CALL_CB(cb,DrawIndexedPrimitive,
              D3DPT_TRIANGLELIST,
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
          __asm nop;
        }
      }
      //LogF("TLEnd");
      beg = endDraw;
    }
  }
}

void EngineDD9::SetTextureR(int cb, DWORD stage, const TextureD3D9 *texture)
{
  TextureHandle9 *tHandle;
  if (texture)
  {
    tHandle = texture->GetHandle();
    if (!tHandle)
    {
      const TextureD3D9 *defTexture = texture->GetDefaultTexture();
      tHandle = defTexture->GetHandle();
    }
#if _HELISIM_RENDER_TO_TEXTURE
    else
    {
      if (texture->Type() == TT_R2T)
      {
        TrackR2TTexUsage(texture->GetHandle());
      }
    }
#endif
  }
  else
  {
    tHandle = NULL;
  }
  SetTextureHandle(cb, stage, tHandle);
}


//static bool FDB = false;

void EngineDD9::ForgetDeviceBuffers(int cb)
{
}

void EngineDD9::EnableCustomTexGenZero(int cb, bool optimize)
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

void EngineDD9::EnableCustomTexGen(int cb, const PASSMATRIXTYPE **matrix, int nMatrices, bool optimize)
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
        scaledMatrix = (*pMatrix) * sao;
        pMatrix = &scaledMatrix;
      }
    }

    // Set the texgen matrix
    SetVertexShaderConstantF(cb, VSC_TexTransform+2*i, (float*)pMatrix, 2);
  }
#if _ENABLE_REPORT
  // done to make sure values which cannot be relied upon contain zeros, so that they cause visual errors
  for (int i=nMatrices; i<MaxStages; i++)
  {
    const PASSMATRIXTYPE *pMatrix = &D3DMatZero;
    SetVertexShaderConstantF(cb, VSC_TexTransform+2*i, (float*)pMatrix, 2);
  }
#endif
}

void EngineDD9::PrepareSingleTexDiffuseA(int cb, const TexMaterialLODInfo &mat)
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

void EngineDD9::PrepareDetailTex(int cb, const TexMaterialLODInfo &mat, const EngineShapeProperties &prop)
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
          //DoAssert((texMat->_texGen[stage + 1]._uvSource == UVTexWaterAnim) || (texMat->_texGen[stage + 1]._uvSource == UVTexShoreAnim));
          //DoAssert((texMat->_texGen[stage + 2]._uvSource == UVTexWaterAnim) || (texMat->_texGen[stage + 2]._uvSource == UVTexShoreAnim));

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

void EngineDD9::DoShadowsStatesSettings(int cb, EShadowState ss, bool noBackCull)
{
  RendState &cbState = CBState(cb);
  cbState._currentShadowStateSettings = ss;
  cbState._backCullDisabled = noBackCull;

  switch(ss)
  {
  case SS_Transparent:
    D3DSetRenderState(cb, D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
    D3DSetRenderState(cb, D3DRS_STENCILWRITEMASK, STENCIL_FULL);
    D3DSetRenderState(cb, D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
    D3DSetRenderState(cb, D3DRS_CULLMODE, noBackCull ? D3DCULL_NONE : D3DCULL_CCW) ;
    if (_caps._canTwoSidedStencil)
    {
      D3DSetRenderState(cb, D3DRS_TWOSIDEDSTENCILMODE, FALSE);
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
      D3DSetRenderState(cb, D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
      D3DSetRenderState(cb, D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
    }
    D3DSetRenderState(cb, D3DRS_STENCILWRITEMASK, STENCIL_FULL);
    D3DSetRenderState(cb, D3DRS_STENCILREF, 0);
    D3DSetRenderState(cb, D3DRS_CULLMODE, noBackCull ? D3DCULL_NONE : D3DCULL_CCW);
    if (_caps._canTwoSidedStencil)
    {
      D3DSetRenderState(cb, D3DRS_TWOSIDEDSTENCILMODE, FALSE);
    }
    break;
  case SS_Disabled:
    D3DSetRenderState(cb, D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
    D3DSetRenderState(cb, D3DRS_STENCILWRITEMASK, STENCIL_FULL);
    D3DSetRenderState(cb, D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
    D3DSetRenderState(cb, D3DRS_STENCILREF, 0);
    D3DSetRenderState(cb, D3DRS_CULLMODE, noBackCull ? D3DCULL_NONE : D3DCULL_CCW);
    if (_caps._canTwoSidedStencil)
    {
      D3DSetRenderState(cb, D3DRS_TWOSIDEDSTENCILMODE, FALSE);
    }
    break;
  case SS_Volume_Front:
    //DoAssert(!_recording);
    D3DSetRenderState(cb, D3DRS_STENCILWRITEMASK, STENCIL_COUNTER);
    D3DSetRenderState(cb, D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
    D3DSetRenderState(cb, D3DRS_STENCILREF, 0);
    Assert (!_caps._canTwoSidedStencil);
    D3DSetRenderState(cb, D3DRS_CULLMODE, D3DCULL_CCW);
    D3DSetRenderState(cb, D3DRS_STENCILPASS, D3DSTENCILOP_DECR);
    break;
  case SS_Volume_Back:
    // this should never be used with two-sided stencil
    D3DSetRenderState(cb, D3DRS_STENCILWRITEMASK, STENCIL_COUNTER);
    D3DSetRenderState(cb, D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
    D3DSetRenderState(cb, D3DRS_STENCILREF, 0);

    if(_caps._canTwoSidedStencil)
    {
      D3DSetRenderState(cb, D3DRS_CCW_STENCILFUNC, D3DCMP_ALWAYS);
      D3DSetRenderState(cb, D3DRS_CULLMODE, D3DCULL_NONE);
      D3DSetRenderState(cb, D3DRS_TWOSIDEDSTENCILMODE, TRUE);

      D3DSetRenderState(cb, D3DRS_STENCILPASS, D3DSTENCILOP_INCR);
      D3DSetRenderState(cb, D3DRS_CCW_STENCILPASS, D3DSTENCILOP_DECR);
      // 
      // D3DRS_CCW_STENCILFAIL
      // D3DRS_CCW_STENCILZFAIL
    }
    else
    {
      D3DSetRenderState(cb, D3DRS_STENCILPASS, D3DSTENCILOP_INCR);
      D3DSetRenderState(cb, D3DRS_CULLMODE, D3DCULL_CW);
    }

    break;
  case SS_ShadowBuffer:
    D3DSetRenderState(cb, D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
    D3DSetRenderState(cb, D3DRS_STENCILWRITEMASK, STENCIL_FULL);
    D3DSetRenderState(cb, D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
    D3DSetRenderState(cb, D3DRS_CULLMODE, D3DCULL_CCW);
    D3DSetRenderState(cb, D3DRS_STENCILREF, 0);
    if (_caps._canTwoSidedStencil)
    {
      D3DSetRenderState(cb, D3DRS_TWOSIDEDSTENCILMODE, FALSE);
    }
    break;
  default:
    LogF("Error: Unknown shadow state");
  }
}

void EngineDD9::SetMultiTexturing(int cb, TextureD3D9 **tex, int count, int keepUntouchedLimit)
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
        D3DSetSamplerState(cb, i+1, D3DSAMP_SRGBTEXTURE, srgb && _linearSpace);
      }
    }

    // Set the specified texture
    SetTextureR(cb, i + 1, tex[i]);
  }

  // Set the rest to NULL
  int maxUntouchedStageCount = min(keepUntouchedLimit, TEXID_FIRST) - 1;
  for (int i = count; i < maxUntouchedStageCount; i++)
  {
    // Perform quick rejection
    if (CBState(cb)._lastHandle[i + 1])
    {
      SetTexture(cb, i + 1, NULL);
    }
  }
}

Vector3 EngineDD9::TransformPositionToObjectSpace(int cb, Vector3Par position)
{
  return CBState(cb)._invWorld * position;
}

Vector3 EngineDD9::TransformAndNormalizePositionToObjectSpace(int cb, Vector3Par position)
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

Vector3 EngineDD9::TransformAndNormalizeVectorToObjectSpace(int cb, Vector3Par vector)
{
  Vector3 newVector = CBState(cb)._invWorld.Orientation() * vector;
  return newVector.Normalized();
}

void EngineDD9::SetFiltering(int cb, int stage, TexFilter filter, int maxAFLevel)
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
      D3DSetSamplerState(cb, stage,D3DSAMP_MINFILTER,D3DTEXF_ANISOTROPIC);
      D3DSetSamplerState(cb, stage,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR); 
      D3DSetSamplerState(cb, stage,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);
      D3DSetSamplerState(cb, stage,D3DSAMP_MAXANISOTROPY,maxAFLevel);
      break;
    }
    // when AF disabled, fall-through
  case TFTrilinear:
    D3DSetSamplerState(cb, stage,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
    D3DSetSamplerState(cb, stage,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR); 
    D3DSetSamplerState(cb, stage,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);
    break;
  case TFLinear:
    D3DSetSamplerState(cb, stage,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
    D3DSetSamplerState(cb, stage,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR); 
    D3DSetSamplerState(cb, stage,D3DSAMP_MIPFILTER,D3DTEXF_POINT);
    break;
  case TFPoint:
    D3DSetSamplerState(cb, stage,D3DSAMP_MINFILTER,D3DTEXF_POINT);
    D3DSetSamplerState(cb, stage,D3DSAMP_MAGFILTER,D3DTEXF_POINT);  
    D3DSetSamplerState(cb, stage,D3DSAMP_MIPFILTER,D3DTEXF_POINT);
    break;
  }
}

static float ColorDistance2(ColorVal c1, ColorVal c2)
{
  return Square(c1.R()-c2.R())+Square(c1.G()-c2.G())+Square(c1.B()-c2.B())+Square(c1.A()-c2.A());
}

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

void EngineDD9::SetTexture0Color(int cb, ColorVal maxColor, ColorVal avgColor, bool optimize)
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

void EngineDD9::SetTexture0(int cb, const TextureD3D9 *tex)
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
    D3DSetSamplerState(cb, 0, D3DSAMP_SRGBTEXTURE, srgb && _linearSpace);
  }

  Color maxColor = tex->GetMaxColor();
  const TextureD3D9 *loadedTexture;
  if (tex->GetHandle())
  {
    loadedTexture = tex;
#if _HELISIM_RENDER_TO_TEXTURE
    if (tex->Type() == TT_R2T)
    {
      TrackR2TTexUsage(tex->GetHandle());
    }
#endif
  }
  else
  {
    maxColor = tex->GetColor();
    loadedTexture = tex->GetDefaultTexture();
  }
  SetTexture0Color(cb, maxColor, CombineAvgAndMaxColor(tex->GetColor(),maxColor),true);
  SetTextureHandle(cb, 0, loadedTexture->GetHandle());
}

HRESULT EngineDD9::Lock(const ComRef<IDirect3DVertexBuffer9> &buf, UINT offsetToLock, UINT sizeToLock, void **ppbData, DWORD flags)
{
#if _ENABLE_REPORT
  //Assert(!IsCBRecording());
#endif
  PROFILE_DX_SCOPE(3DVLo);
  return buf->Lock(offsetToLock, sizeToLock, ppbData, flags);
}

HRESULT EngineDD9::Lock(const ComRef<IDirect3DIndexBuffer9> &buf, UINT offsetToLock, UINT sizeToLock, void **ppbData, DWORD flags)
{
#if _ENABLE_REPORT
  //Assert(!IsCBRecording());
#endif
  PROFILE_DX_SCOPE(3DILo);
  return buf->Lock(offsetToLock, sizeToLock, ppbData, flags);
}

RString EngineDD9::GetTextureDebugName(IDirect3DBaseTexture9 * tex) const
{
  if (!tex) return "<null>";
  if (tex==_sssm) return "<sssm>";
  else if (tex==_renderTargetSB) return "<SB>";
  else if (tex==_renderTargetDepthBufferSB) return "<DSB>";
  else if (tex==_renderTargetD) return "<D>";
  else if (tex==_renderTargetS) return "<S>";
  else
  {
    // try to find the texture in the list of all textures
    RString ret = _textBank->GetTextureDebugName(tex);
    if (!ret.IsEmpty()) return ret;
    return Format("%p",tex);
  }
}

RString EngineDD9::GetSurfaceDebugName(IDirect3DSurface9 *surf) const
{
  if (!surf) return "<null>";
  if (surf==_sssmSurface) return "<sssm>";
  else if (surf==_renderTargetSurfaceSB) return "<SB>";
  else if (surf==_renderTargetDepthBufferSurfaceSB) return "<DSB>";
  else if (surf==_renderTargetDepthInfoSurface) return "<DIS>";
  else if (surf==_renderTargetDepthInfoZ) return "<DIZ>";
  else if (surf==_renderTargetSurfaceD) return "<D>";
  else if (surf==_renderTargetSurfaceS) return "<S>";
  else if (surf==_backBuffer) return "<BB>";
  else if (surf==_depthBuffer) return "<DB>";
  else if (surf==_depthBufferRT) return "<DB-RT>";
  return Format("%p",surf);
}

//@{ used to suppress lines with constant type not matching (like i or b when interested about f only)
#define CASE_SH_CONSTANTDO(name) if (index==name) return #name;

#define CASE_SH_CONSTANT(type,name,kind,...) CASE_SH_CONSTANT##kind(name)
//@}

RString EngineDD9::GetPSConstantFDebugName(int index) const
{
  #define CASE_SH_CONSTANTc(name) CASE_SH_CONSTANTDO(name)
  #define CASE_SH_CONSTANTi(name)
  #define CASE_SH_CONSTANTb(name)
  PSC_LIST(CASE_SH_CONSTANT,CASE_SH_CONSTANT)
  if (index==LIGHTSPACE_START_REGISTER_PS) return "LIGHTSPACE_START_REGISTER_PS";
  for (int i=1; i<=MAX_LIGHTS; i++)
  {
    const int vectorsPerLight = sizeof(SLPoint) / sizeof(D3DXVECTOR4);
    if (index==LIGHTSPACE_START_REGISTER_PS+(MAX_LIGHTS-i)*vectorsPerLight) return Format("Light %d",i);
  }
  return Format("%d",index);
  #undef CASE_SH_CONSTANTc
  #undef CASE_SH_CONSTANTi
  #undef CASE_SH_CONSTANTb
}

RString EngineDD9::GetPSConstantIDebugName(int index) const
{
  #define CASE_SH_CONSTANTc(name)
  #define CASE_SH_CONSTANTi(name) CASE_SH_CONSTANTDO(name)
  #define CASE_SH_CONSTANTb(name)
  PSC_LIST(CASE_SH_CONSTANT,CASE_SH_CONSTANT)
  return Format("%d",index);
  #undef CASE_SH_CONSTANTc
  #undef CASE_SH_CONSTANTi
  #undef CASE_SH_CONSTANTb
}

RString EngineDD9::GetPSConstantBDebugName(int index) const
{
  #define CASE_SH_CONSTANTc(name)
  #define CASE_SH_CONSTANTi(name)
  #define CASE_SH_CONSTANTb(name) CASE_SH_CONSTANTDO(name)
  PSC_LIST(CASE_SH_CONSTANT,CASE_SH_CONSTANT)
  return Format("%d",index);
  #undef CASE_SH_CONSTANTc
  #undef CASE_SH_CONSTANTi
  #undef CASE_SH_CONSTANTb
}

RString EngineDD9::GetVSConstantFDebugName(int index) const
{
  #define CASE_SH_CONSTANTc(name) CASE_SH_CONSTANTDO(name)
  #define CASE_SH_CONSTANTi(name)
  #define CASE_SH_CONSTANTb(name)
  VSC_LIST(CASE_SH_CONSTANT,CASE_SH_CONSTANT)
  const int vectorsPerLight = sizeof(SLPoint) / sizeof(D3DXVECTOR4);
  for (int i=1; i<=MAX_LIGHTS; i++)
  {
    if (index==LIGHTSPACE_START_REGISTER_PS+(MAX_LIGHTS-i)*vectorsPerLight) return Format("Light %d",i);
  }
  return Format("%d",index);
  #undef CASE_SH_CONSTANTc
  #undef CASE_SH_CONSTANTi
  #undef CASE_SH_CONSTANTb
}

RString EngineDD9::GetVSConstantIDebugName(int index) const
{
  #define CASE_SH_CONSTANTc(name)
  #define CASE_SH_CONSTANTi(name) CASE_SH_CONSTANTDO(name)
  #define CASE_SH_CONSTANTb(name)
  VSC_LIST(CASE_SH_CONSTANT,CASE_SH_CONSTANT)
  return Format("%d",index);
  #undef CASE_SH_CONSTANTc
  #undef CASE_SH_CONSTANTi
  #undef CASE_SH_CONSTANTb
}

RString EngineDD9::GetVSConstantBDebugName(int index) const
{
  #define CASE_SH_CONSTANTc(name)
  #define CASE_SH_CONSTANTi(name)
  #define CASE_SH_CONSTANTb(name) CASE_SH_CONSTANTDO(name)
  VSC_LIST(CASE_SH_CONSTANT,CASE_SH_CONSTANT)
  return Format("%d",index);
  #undef CASE_SH_CONSTANTc
  #undef CASE_SH_CONSTANTi
  #undef CASE_SH_CONSTANTb
}

void EngineDD9::SetTextureHandle(int cb, int stage, TextureHandle9 *tex, bool optimize)
{
  RendState &cbState = CBState(cb);
  #if ENCAPSULATE_TEXTURE_REF
  IDirect3DTexture9 *refTex = tex ? tex->_surface.GetRef() : NULL;
  #else
  IDirect3DTexture9 *refTex = tex;
  #endif
  if (!optimize || cbState._lastHandle[stage] != refTex)
  {
    cbState._lastHandle[stage] = refTex;
    //PROFILE_DX_SCOPE(3dSeT);
    CALL_CB(cb,SetTextureHandle,stage, tex);
  }
}

void EngineDD9::SetTexture(int cb, int stage, IDirect3DBaseTexture9 *tex, bool optimize)
{
  RendState &cbState = CBState(cb);
  if (!optimize || cbState._lastHandle[stage] != tex)
  {
    cbState._lastHandle[stage] = tex;
    //PROFILE_DX_SCOPE(3dSeT);
    CALL_CB(cb,SetTexture,stage, tex);
    // cannot bind current RT as a texture
    // on Xbox textures exist in memory, render targets in EDRAM, there can be no conflict between them
    DoAssert(!tex || tex!=_currRenderTarget);
  }
}

void EngineDD9::SetStreamSource(int cb, const VertexStaticData9::BufferFuture &buf, int stride, int bufSize, bool optimize)
{
  ADD_COUNTER(d3xVB,1);
  RendState &cbState = CBState(cb);

  if (!optimize || !cbState._vBufferLast.IsSameFuture(buf))
  {
    CALL_CB(cb,SetStreamSource, 0, buf, 0, stride);
    cbState._vBufferLast = buf;
    cbState._vBufferSize = bufSize;
    cbState._strideLast = stride;
#if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("Stream source change");
    }
#endif
  }
}


void EngineDD9::SetVertexDeclaration(int cb, IDirect3DVertexDeclaration9 *decl, bool optimize)
{
  RendState &cbState = CBState(cb);
  if (!optimize || cbState._vertexDeclLast != decl)
  {
#if DO_TEX_STATS>=2
    if (LogStatesOnce)
    {
      /*
      D3DVERTEXELEMENT9 decl[64];
      UINT numElements = lenof(decl);
      decl->GetDeclaration()
      LogF("SetVertexDeclaration %d",dtaV->VertexSize());
      */
      RString name = GetVDName(decl);
      LogF("SetVertexDeclaration %s",cc_cast(name));
    }
#endif
    //PROFILE_DX_SCOPE(3dSVD);
    CALL_CB(cb,SetVertexDeclaration,decl);
    cbState._vertexDeclLast = decl;
  }
}

void EngineDD9::SetVertexShader(int cb, IDirect3DVertexShader9 *shader, bool optimize)
{
  RendStateExt &cbState = CBState(cb);
  // check if we match in all currently active branches, if we do, we can skip
  if (!optimize || CheckVSNeedsToBeSet(shader,cbState,cbState._rec._currPredication))
  {
    CALL_CB(cb,SetVertexShader,shader);
  }
}

bool EngineDD9::BeginPredicationMask(int cb, int mask,PredicationMode pass)
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

void EngineDD9::EndPredication( int cb )
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

/**
@param cb CB ID - see COMMAND_BUFFERS
*/
bool EngineDD9::SetVertexShaderConstantF(int cb, int startRegister, const float *data, int count, bool optimize)
{
  if (CHECK_ENG_DIAG(EDENoFilterVSC))
  {
    optimize = false;
  }
  if (LimitVSCCount>=0 && count>LimitVSCCount) count = LimitVSCCount;

  // If constant is not a light, it should not reach the light space
  if (startRegister < LIGHTSPACE_START_REGISTER_VS)
  {
    DoAssert(startRegister + count - 1 < LIGHTSPACE_START_REGISTER_VS);
  }

#define MEASURE_CACHE 0
#if MEASURE_CACHE
  // measure caching efficiency in various parts of the constant space
  static const int segs[][2]={
    {0,FREESPACE_START_REGISTER},
    {FREESPACE_START_REGISTER,LIGHTSPACE_START_REGISTER_VS},
    {LIGHTSPACE_START_REGISTER_VS,256},
  };
  static int cacheIndex = 0;
  if (startRegister<segs[cacheIndex][0] || startRegister>=segs[cacheIndex][1])
  {
    optimize = false;
  }
  if (optimize)
  {
    ADD_COUNTER(3dTVC,1);
    ADD_COUNTER(3dTVS, count);
  }
#endif
  //  PROFILE_DX_SCOPE(3dTVC);
  COMPILETIME_COMPARE(sizeof(float),sizeof(int));
  if (
    !optimize
    // setting constants outside of the shadowed region
    || startRegister+count>lenof(CBState(cb)._shaderConstValues)
    // or some difference is found
    || MemCmp32(&CBState(cb)._shaderConstValues[startRegister][0], data, count * 4)
  )
  {
    if (LimitVSCCount!=0)
    {
#if MEASURE_CACHE
      if (optimize)
      {
        ADD_COUNTER(3dSVC,1);
        ADD_COUNTER(3dSVS, count);
      }
#endif

      PROFILE_DX_SCOPE_DETAIL(3dSVC);
      //ADD_COUNTER(pbSVC,count);
      if (count<=MaxVSC)
      {
        CALL_CB(cb,SetVertexShaderConstantsF,startRegister, Array<FloatConstant>((FloatConstant *)data, count));
      }
      else
      {
        CALL_CB(cb,SetVertexShaderConstantF,startRegister, Array<FloatConstant>((FloatConstant *)data, count));
      }

    }
    if (!CHECK_ENG_DIAG(EDENoFilterVSC))
    {
      // write the part which is cached
      int countCached = intMin(count,lenof(CBState(cb)._shaderConstValues)-startRegister);
      if (countCached>0)
      {
        #if _XMEMCPY
          XMemCpy(&CBState(cb)._shaderConstValues[startRegister][0], data, countCached * 4 * sizeof(float));
        #else
          memcpy(&CBState(cb)._shaderConstValues[startRegister][0], data, countCached * 4 * sizeof(float));
        #endif
      }
    }
    return true;
  }
  return false;
}

bool EngineDD9::SetVertexShaderConstantI(int cb, int startRegister, const int *data, int count, bool optimize)
{
  if (CHECK_ENG_DIAG(EDENoFilterVSC))
  {
    optimize = false;
  }

  Assert(startRegister+count<=lenof(CBState(cb)._shaderConstValuesI));
  if (!optimize || MemCmp32(&CBState(cb)._shaderConstValuesI[startRegister][0], data, count * 4) != 0)
  {
    PROFILE_DX_SCOPE_DETAIL(3dSVC);
    if (count<=IntConstantsSet::maxNum)
    {
      CALL_CB(cb,SetVertexShaderConstantsI,startRegister,Array<IntConstant>((IntConstant *)data, count));
    }
    else
    {
      Assert(cb<0);
      _d3DDevice->SetVertexShaderConstantI(startRegister, data, count);
    }
    if (!CHECK_ENG_DIAG(EDENoFilterVSC))
    {
      memcpy(&CBState(cb)._shaderConstValuesI[startRegister][0], data, count * 4 * sizeof(int));
    }
    return true;
  }
  return false;
}

bool EngineDD9::SetVertexShaderConstantB(int cb, int startRegister, const bool *data, int count, bool optimize)
{
  if (CHECK_ENG_DIAG(EDENoFilterVSC))
  {
    optimize = false;
  }
  Assert(startRegister+count<=lenof(CBState(cb)._shaderConstValuesB));
  if (!optimize || memcmp(&CBState(cb)._shaderConstValuesB[startRegister], data, count * sizeof(bool)) != 0)
  {
    BOOL bArray[BoolConstantsSet::maxNum];
    for (int i = 0; i < count; i++) bArray[i] = data[i];
    DoAssert (count<=BoolConstantsSet::maxNum);
    CALL_CB(cb,SetVertexShaderConstantsB,startRegister, Array<BOOL>(bArray, count));
    if (!CHECK_ENG_DIAG(EDENoFilterVSC))
    {
      memcpy(&CBState(cb)._shaderConstValuesB[startRegister], data, count * sizeof(bool));
    }
    return true;
  }
  return false;
}

void EngineDD9::SetPixelShader(int cb, IDirect3DPixelShader9 *shader, bool optimize)
{
  if (!CHECK_ENG_DIAG(EDENoSetupPS))
  {
    RendStateExt &cbState = CBState(cb);
    // check if we match in all currently active branches, if we do, we can skip
    if (!optimize || CheckPSNeedsToBeSet(shader,cbState,cbState._rec._currPredication))
    {
      CALL_CB(cb,SetPixelShader,shader);
    }
  }
}

bool EngineDD9::SetPixelShaderConstantF(int cb, int startRegister, const float *data, int count, bool optimize)
{
  if (CHECK_ENG_DIAG(EDENoSetupPS)) return false;

  // If constant is not a light, it should not reach the light space
  if (startRegister < LIGHTSPACE_START_REGISTER_PS)
  {
    DoAssert(startRegister + count - 1 < LIGHTSPACE_START_REGISTER_PS);
  }
  
  COMPILETIME_COMPARE(sizeof(float),sizeof(int));

  if (
    !optimize
    || startRegister+count>lenof(CBState(cb)._pixelShaderConstValues)
    || MemCmp32(&CBState(cb)._pixelShaderConstValues[startRegister][0], data, count * 4)
  )
  {
#if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF(
        "SetPixelShaderConstantF %d,%d,%g,%g,%g,%g",
        startRegister,count,data[0],data[1],data[2],data[3]
      );
    }
#endif
    if (count<=MaxPSC)
    {
      CALL_CB(cb,SetPixelShaderConstantsF,startRegister, Array<FloatConstant>((FloatConstant *)data, count));
    }
    else
    {
      Assert(cb<0);
      _d3DDevice->SetPixelShaderConstantF(startRegister, data, count);
    }
    int countCached = intMin(count,lenof(CBState(cb)._pixelShaderConstValues)-startRegister);
    if (countCached>0)
    {
      memcpy(&CBState(cb)._pixelShaderConstValues[startRegister][0], data, countCached * 4 * sizeof(float));
    }
  }
  return true;
}

bool EngineDD9::SetPixelShaderConstantI(int cb, int startRegister, const int *data, int count, bool optimize)
{
  if (CHECK_ENG_DIAG(EDENoSetupPS)) return false;
  
  if (CHECK_ENG_DIAG(EDENoFilterVSC))
  {
    optimize = false;
  }

  Assert(startRegister+count<=lenof(CBState(cb)._pixelShaderConstValuesI));
  if (!optimize || MemCmp32(&CBState(cb)._pixelShaderConstValuesI[startRegister][0], data, count * 4) != 0)
  {
    PROFILE_DX_SCOPE_DETAIL(3dSVC);
    if (count<=IntConstantsSet::maxNum)
    {
      CALL_CB(cb,SetPixelShaderConstantsI,startRegister,Array<IntConstant>((IntConstant *)data, count));
  }
  else
  {
    Assert(cb<0);
    _d3DDevice->SetPixelShaderConstantI(startRegister, data, count);
  }
    if (!CHECK_ENG_DIAG(EDENoFilterVSC))
    {
      memcpy(&CBState(cb)._pixelShaderConstValuesI[startRegister][0], data, count * 4 * sizeof(int));
    }
  return true;
  }

  return false;
}

bool EngineDD9::SetPixelShaderConstantB(int cb, int startRegister, const BOOL *data, int count, bool optimize)
{
  if (CHECK_ENG_DIAG(EDENoSetupPS)) return false;

  // TODO: lazy state optimization
  if (count<=MaxPSC)
  {
    CALL_CB(cb,SetPixelShaderConstantsB,startRegister, Array<BOOL>((BOOL *)data, count));
  }
  else
  {
    Assert(cb<0);
    _d3DDevice->SetPixelShaderConstantB(startRegister, data, count);
  }
  return true;
}


void EngineDD9::SetRenderTarget(IDirect3DSurface9 *rts, IDirect3DTexture9 *rt, IDirect3DSurface9 *dt, bool optimize)
{
  if (!optimize || _currRenderTargetSurface != rts)
  {
    PROFILE_DX_SCOPE_DETAIL(3dSRT);
    CALL_D3D(SetRenderTarget, 0, rts);
    
    #if _DEBUG
      if (rts==_renderTargetSurfaceS.GetRef())
      {
        __asm nop;
      }
    #endif
    _currRenderTargetSurface = rts;
    // note: viewport is reset here, z is set to 0..1 range by the SetRenderTarget call
    _minZRange = 0;
    _maxZRange = 1;
    if (_depthBufferRT) // This statement is true only in case multisampling is being used
    {
      if (LogStatesOnce) LogF("SetRenderTarget: Set Depth to %p",dt);
      CALL_D3D(SetDepthStencilSurface,dt);
      _currRenderTargetDepthBufferSurface = dt;
    }
  }
  _currRenderTarget = rt;
#if _ENABLE_REPORT
  // we cannot be recording, but we can be operating in the background
  Assert(!IsCBRecording() || _backgroundD3D==RecLowLevel);
  const int cb = -1;
  if (rt) for (int i=0; i<lenof(CBState(cb)._lastHandle); i++)
  {
    DoAssert(CBState(cb)._lastHandle[i]!=rt)
  }
#endif
}


void EngineDD9::SetRenderTargetAndDepthStencil(
  IDirect3DSurface9 *rts, IDirect3DTexture9 *rt, IDirect3DSurface9 *dts,  
  bool optimize
  )
{
  // Lazy set of render target
  if (!optimize || _currRenderTargetSurface != rts)
  {
    PROFILE_DX_SCOPE_DETAIL(3dSRT);
    CALL_D3D(SetRenderTarget, 0, rts);
    _currRenderTargetSurface = rts;
    // note: viewport is reset here, z is set to 0..1 range by the SetRenderTarget call
    _minZRange = 0;
    _maxZRange = 1;
  }
  _currRenderTarget = rt;

  // Lazy set of depth stencil
  if (!optimize || _currRenderTargetDepthBufferSurface != dts)
  {
    PROFILE_DX_SCOPE_DETAIL(3dSDB);
    if (LogStatesOnce) LogF("SetRenderTargetAndDepthStencil: Set Depth to %p",dts);
    CALL_D3D(SetDepthStencilSurface,dts);
    _currRenderTargetDepthBufferSurface = dts;
  }

  // Check render target is not set as any texture now
#if _ENABLE_REPORT
  //Assert(!IsCBRecording());
  const int cb = -1;
  if (rt) for (int i=0; i<lenof(CBState(cb)._lastHandle); i++)
  {
    DoAssert(CBState(cb)._lastHandle[i]!=rt)
  }
#endif
}

void EngineDD9::DrawPrimitive(int cb, D3DPRIMITIVETYPE primitiveType, UINT startVertex, UINT primitiveCount)
{
  CALL_D3D(DrawPrimitive, primitiveType, startVertex, primitiveCount);
}

struct VertexSimple
{
  Vector3P pos;
};

void EngineDD9::FillOutRenderTarget(const ComRef<IDirect3DSurface9> &rt, const ComRef<IDirect3DSurface9> &db)
{
#if 0
  // Remember old render target
  ComRef<IDirect3DSurface9> oldRT;
  GetDirect3DDevice()->GetRenderTarget(0, oldRT.Init());
  ComRef<IDirect3DSurface9> oldDB;
  GetDirect3DDevice()->GetDepthStencilSurface(oldDB.Init());

  // Set the render target as temporary
  ComRef<IDirect3DTexture9> empty;
  GetDirect3DDevice()->SetRenderTarget(0, rt);
  GetDirect3DDevice()->SetDepthStencilSurface(db);

  // Create the VB
  ComRef<IDirect3DVertexBuffer9> vb;
  if (FAILED(GetDirect3DDevice()->CreateVertexBuffer(sizeof(VertexSimple) * 4, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, vb.Init(), NULL)))
  {
    LogF("Error: Vertex buffer creation failed.");
    return;
  }
  VertexSimple *v;
  Lock(vb, 0, 0, (void**)&v, 0);
  v[0].pos = Vector3(-0.5, -0.5, 0.5);
  v[1].pos = Vector3(-0.5,  0.5, 0.5);
  v[2].pos = Vector3( 0.5, -0.5, 0.5);
  v[3].pos = Vector3( 0.5,  0.5, 0.5);
  vb->Unlock();
  SetStreamSource(vb, sizeof(VertexSimple), 4);

  // Create the VD
  const D3DVERTEXELEMENT9 vertexDecl[] =
  {
    {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    D3DDECL_END()
  };
  ComRef<IDirect3DVertexDeclaration9> vd;
  if (FAILED(GetDirect3DDevice()->CreateVertexDeclaration(vertexDecl, vd.Init())))
  {
    LogF("Error: Vertex declaration creation failed.");
    return;
  }
  SetVertexDeclaration(vd);

  // Rendering preparation
  {
    FlushQueues();
    // we want all render states to be set as needed
    // postprocess coordinates are given in 3D, but there is no camera space
    SwitchTL(TLViewspace);
  }

  // Shader sources
  QOStrStreamDataPrefixedLine psSource(PSFile);
  QOStrStreamDataPrefixedLine vsSource(FPShaders);

  // define macros for the shaders
  D3DXMacroList defines;
  CreateBasicDefines(defines);
  // Set vertex shader
  {
    // Assemble shader
    ComRef<ID3DXBuffer> shader;
    ComRef<ID3DXBuffer> errorMsgs;
    DWORD flags = DebugVS ? D3DXSHADER_SKIPOPTIMIZATION|D3DXSHADER_DEBUG : 0;
    flags |= _sm2 ? SM2FLAGS : SM3FLAGS;
    HRESULT hr;
    hr = D3DXCompileShader_Cached(vsSource.str(), vsSource.pcount(), defines.Data(), &_hlslInclude, "VSPostProcessCustomEdge4T", _caps._vsVersion, flags, shader.Init(), errorMsgs.Init(), NULL);
    DoAssert(!FAILED(hr));

    // Create shader
    ComRef<IDirect3DVertexShader9> vs;
    hr = GetDirect3DDevice()->CreateVertexShader((const DWORD*)shader->GetBufferPointer(), vs.Init());
    DoAssert(!FAILED(hr));

    // Set shader
    SetVertexShader(vs);

    // No posOffset needed
    const float posOffset[4] = {0, 0, 0, 0};
    SetVertexShaderConstantF(8, posOffset, 1);
  }

  // Set pixel shader
  {
    // Assemble shader
    ComRef<ID3DXBuffer> shader;
    ComRef<ID3DXBuffer> errorMsgs;
    HRESULT hr;
    DWORD flags = flags = _sm2 ? SM2FLAGS : SM3FLAGS;
    hr = D3DXCompileShader_Cached(psSource.str(), psSource.pcount(), defines.Data(), &_hlslInclude, "PSWhite", _psVersion, flags, shader.Init(), errorMsgs.Init(), NULL);
    DoAssert(!FAILED(hr));

    // Create shader
    ComRef<IDirect3DPixelShader9> ps;
    hr = GetDirect3DDevice()->CreatePixelShader((const DWORD*)shader->GetBufferPointer(), ps.Init());
    DoAssert(!FAILED(hr));

    // Set shader
    SetPixelShader(ps);
  }

  // Draw
  DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);

  // Restore old render target
  GetDirect3DDevice()->SetRenderTarget(0, oldRT);
  GetDirect3DDevice()->SetDepthStencilSurface(oldDB);
#endif
}

HRESULT EngineDD9::CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multiSample, DWORD multisampleQuality, BOOL lockable, ComRef<IDirect3DSurface9> &surface, bool initialize, void* pSharedHandle)
{
  HRESULT hr = _d3DDevice->CreateRenderTarget(width, height, format, multiSample, multisampleQuality, lockable, surface.Init(), NULL);
  if (!FAILED(hr) && initialize)
  {
    ComRef<IDirect3DSurface9> empty;
    FillOutRenderTarget(surface, empty);
  }
  return hr;
}

HRESULT EngineDD9::CreateTextureRT(UINT width,UINT height, UINT levels, D3DFORMAT format, D3DPOOL pool, ComRef<IDirect3DTexture9> &texture, bool initialize)
{
  HRESULT hr = _d3DDevice->CreateTexture(width, height, levels, D3DUSAGE_RENDERTARGET, format, pool, texture.Init(), NULL);
  if (!FAILED(hr) && initialize)
  {
    ComRef<IDirect3DSurface9> rts;
    HRESULT hrs = texture->GetSurfaceLevel(0, rts.Init());
    if(FAILED(hrs))
    {
      texture.Free();
      return hrs;
    }

    ComRef<IDirect3DSurface9> empty;
    FillOutRenderTarget(rts, empty);
  }
  return hr;
}

HRESULT EngineDD9::CreateTextureRT(UINT width,UINT height, UINT levels, D3DFORMAT format, D3DPOOL pool, ComRef<IDirect3DTexture9> &texture, ComRef<IDirect3DSurface9> &rts, bool initialize)
{
  // TODOX360: better EDRAM management
  // we need to create a render target (EDRAM) + texture (normal RAM)
  HRESULT hr = _d3DDevice->CreateTexture(width, height, levels, D3DUSAGE_RENDERTARGET, format, pool, texture.Init(), NULL);
  if (!FAILED(hr))
  {
    HRESULT hrs = texture->GetSurfaceLevel(0, rts.Init());
    if(FAILED(hrs))
    {
      texture.Free();
      return hrs;
    }
    if (initialize)
    {
      ComRef<IDirect3DSurface9> empty;
      FillOutRenderTarget(rts, empty);
    }
  }
  return hr;
}

HRESULT EngineDD9::CreateTextureDS(UINT width,UINT height, UINT levels, D3DFORMAT format, D3DPOOL pool, ComRef<IDirect3DTexture9> &texture, const ComRef<IDirect3DSurface9> &rt)
{
  HRESULT hr = _d3DDevice->CreateTexture(width, height, levels, D3DUSAGE_DEPTHSTENCIL, format, pool, texture.Init(), NULL);
  if (!FAILED(hr))
  {
    ComRef<IDirect3DSurface9> dss;
    HRESULT hrs = texture->GetSurfaceLevel(0, dss.Init());
    DoAssert(!FAILED(hrs));

    FillOutRenderTarget(rt, dss);
  }
  return hr;
}

HRESULT EngineDD9::CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multiSample, DWORD multisampleQuality, BOOL discard, ComRef<IDirect3DSurface9> &surface, const ComRef<IDirect3DSurface9> &rt, void* pSharedHandle)
{
  HRESULT hr = _d3DDevice->CreateDepthStencilSurface(width, height, format, multiSample, multisampleQuality, discard, surface.Init(), NULL);
  if (!FAILED(hr))
  {
    FillOutRenderTarget(rt, surface);
  }
  return hr;
}

IDirect3DTexture9 *EngineDD9::GetDepthAsTexture() const
{
#if _HELISIM_RENDER_TO_TEXTURE
  return _r2tBegin ? _r2tRT._renderTargetDepthInfo : _renderTargetDepthInfo;
#else
  return _renderTargetDepthInfo;
#endif
}

IDirect3DTexture9 *EngineDD9::GetCurrRenderTargetAsTexture()
{
#if 1 
  // if current render target cannot be used as a texture, use StretchRect to copy it
  if (_currRenderTarget)
  {
    return _currRenderTarget;
  }
  else if (_currRenderTargetSurface!=_renderTargetSurfaceD)
  {
#if defined(_X360SHADERGEN)
    CALL_D3D(Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS,NULL,_renderTargetD,NULL,0,0,NULL,0,0,NULL);
#else
    CALL_D3D(StretchRect,_currRenderTargetSurface,NULL,_renderTargetSurfaceD,NULL,D3DTEXF_NONE);
#endif
    return _renderTargetD;
  }
  else
  {
    Fail("No other render target available");
    return _renderTargetD;
  }
#else
  return _currRenderTarget;
#endif
}

void EngineDD9::SwitchToBackBufferRenderTarget()
{
  const int cb = -1;
  // if there is depth buffer, it is associated with render target, not back buffer
  // Set the render target
  SetRenderTarget(_backBuffer,NULL,_depthBuffer);

  if (_caps._canWriteSRGB)
  {
    D3DSetRenderState(cb, D3DRS_SRGBWRITEENABLE,_linearSpace);
  }
  // once we render to the backbuffer with SRGB conversion, we are no longer in linear space
  _linearSpace = false;
}

IDirect3DTexture9 *EngineDD9::CreateRenderTargetCopyAsTexture()
{
  if (_currRenderTarget!=_renderTargetD)
  {
#if defined(_X360SHADERGEN)
    CALL_D3D(Resolve,D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS,NULL,_renderTargetD,NULL,0,0,NULL,0,0,NULL);
#else
    CALL_D3D(StretchRect,_currRenderTargetSurface,NULL,_renderTargetSurfaceD,NULL,D3DTEXF_NONE);
#endif
    return _renderTargetD;
  }
  else
  {
#if defined(_X360SHADERGEN)
    // It's ok for XBOX to use _renderTargetD directly as a texture. The fact the _renderTargetD is the current render target means it was already
    // resolved to _renderTargetD (see EngineDD9::End3DRendering - it was most likely the previous step).
#else
    // This case doesn't occur on PC - there is not switch to _renderTargetD in EngineDD9::End3DRendering
    Fail("No other render target available");
#endif
    return _renderTargetD;
  }
}

//!
void EngineDD9::SetPPAAQuality(int quality)
{
  if (!_pp._smaa || !_pp._fxaa) return;

  if (quality <= 0) 
  {
    _pp._smaa->Enable(false);
    _pp._fxaa->Enable(false);
  }
  else
  {
    bool smaa = quality > 3;
    
    if (smaa) {

      _pp._smaa->Enable(true);
      _pp._smaa->SetQuality(quality - 3);
      _pp._fxaa->Enable(false);
    } 
    else
    {
      _pp._fxaa->Enable(true);
      _pp._fxaa->SetQuality(quality & 3);
      _pp._smaa->Enable(false);
    }
  }

  _ppaaQuality = quality;
}

//!
int EngineDD9::GetPPAAQuality() const
{
  return _ppaaQuality;
}

#if _HELISIM_RENDER_TO_TEXTURE

void EngineDD9::CreateR2TResources()
{  
  HRESULT err;
  D3DSURFACE_DESC desc;

  const DWORD width = R2TMaxResolution;
  const DWORD height = R2TMaxResolution;

  if (_renderTargetS)
  {
    // non multisampled rt
    err = _renderTargetS->GetLevelDesc(0, &desc);
    if (err != D3D_OK) 
    {
      DDError9("Error: R2T GetLevelDesc", err);
      return;
    }

    err = CreateTextureRT(width, height, 1, desc.Format, desc.Pool, _r2tRT._renderTargetS, true);
    if (err != D3D_OK) 
    {
      DDError9("Error: R2T Render target texture creation failed.", err); 
      return;
    }

    err = _r2tRT._renderTargetS->GetSurfaceLevel(0, _r2tRT._renderTargetSurfaceS.Init());

    if (err != D3D_OK) 
    {
      DDError9("Error: R2T Render target surface creation failed.", err); 
      return;
    }
  }
  else
  {
    if(!_renderTargetSurfaceS) return;
    err = _renderTargetSurfaceS->GetDesc(&desc);
    if (err != D3D_OK) 
    {
      DDError9("Error: R2T GetLevelDesc", err);
      return;
    }

    // multisampled rt
    err = CreateRenderTarget(width, height, desc.Format, 
      desc.MultiSampleType, desc.MultiSampleQuality, FALSE, _r2tRT._renderTargetSurfaceS, false, NULL);

    Log("R2T _renderTargetSurfaceS %dx%d",width, height);
    if (err != D3D_OK)
    {
      DDError9("Error: R2T Render target surface creation failed.", err);
      return;
    }
  }

  // used for RT copy -> CreateR2TRenderTargetCopyAsTexture()
  err = CreateTextureRT(width, height, 1, desc.Format, desc.Pool, _r2tRT._rtCopy, true);
  if (err != D3D_OK) 
  {
    DDError9("Error: R2T Render target texture creation failed.", err); 
    return;
  }

  err = _r2tRT._rtCopy->GetSurfaceLevel(0, _r2tRT._rtCopySurface.Init());

  if (err != D3D_OK) 
  {
    DDError9("Error: R2T render target surface creation failed.", err); 
    return;
  }

  // depth texture used as texture
  err = CreateTextureRT(width, height, 1, D3DFMT_R32F, D3DPOOL_DEFAULT, _r2tRT._renderTargetDepthInfo, true);
  Log("_renderTargetDepthInfo %dx%d", width, height);

  if (err != D3D_OK) 
  {
    DDError9("Error: R2T Render target depth texture creation failed.", err);
    return;
  }

  err = _r2tRT._renderTargetDepthInfo->GetSurfaceLevel(0, _r2tRT._renderTargetDepthInfoSurface.Init());
  if (err != D3D_OK) 
  {
    DDError9("Error: R2T Render target depth surface creation failed.", err);
    return;
  }

  InitializeR2TTargets();
}


void EngineDD9::DestroyR2TResources()
{
  UnRegisterAllR2TSourceTextures();
  DestroyR2TTargets();

  _r2tRT._rtCopy.Free();
  _r2tRT._rtCopySurface.Free();
  _r2tRT._renderTargetS.Free();
  _r2tRT._renderTargetSurfaceS.Free();
  _r2tRT._renderTargetDepthInfo.Free();
  _r2tRT._renderTargetDepthInfoSurface.Free();
}

void EngineDD9::ConvertFullToLite(R2TTargetDesc &full, R2TTargetDescLite &lite)
{
  lite._name = full._desc._name;
  lite._width = full._desc._width;
  lite._height = full._desc._height;
  lite._texture = full._desc._texture;
}

bool EngineDD9::ConvertLiteToFull(R2TTargetDesc &full, R2TTargetDescLite &lite)
{
  full._desc._name = lite._name;
  full._desc._width = lite._width;
  full._desc._height = lite._height;
  full._desc._texture = lite._texture;
  return D3D_OK == lite._texture->GetSurfaceLevel(0, full._surface.Init());
}

void EngineDD9::ClearR2TTarget(R2TTargetDescLite &desc)
{
  if (desc._name.GetLength() > 0 && desc._texture)
  {
    CALL_D3D(R2TClearTarget, desc._texture);
  }
}

void EngineDD9::InvalidateR2TTarget(R2TTargetDesc &tgt, bool registerTexture)
{
  DoAssert(!tgt._active);

  if (tgt._active)
  {
    tgt._releaseWanted = true;
  }
  else
  {
    if (tgt._surface.NotNull())
    {
      if (registerTexture)
      {
        // move texture back to RT list
        R2TTargetDescLite &descLite = _r2tSrcTargets.Append();
        ConvertFullToLite(tgt, descLite);
        ClearR2TTarget(descLite);
      }
    }

    tgt._surface.Free();
    tgt._desc._texture.Free();
    tgt._tiPars.Free();
    tgt._hdrPars.Free();
    tgt._clrPars.Free();
    tgt._id = R2TUninitialized;
  }
}

void EngineDD9::InitializeR2TTargets()
{
  _r2tTargets.Realloc(R2TRenderTargetCount);
  _r2tTargets.Resize(R2TRenderTargetCount);

  for (int i = 0; i < _r2tTargets.Size(); i++)
  {
    R2TTargetDesc &desc = _r2tTargets[i];
    desc._id = R2TUninitialized;
    // create HDR aperture textures
    //desc._hdrPars = new R2THDRPars();
    //desc._hdrPars->Initialize(this);
    //desc._tiPars = new R2TTiPars();
    //desc._tiPars->Initialize(this);
    desc._active = false;
    desc._releaseWanted = false;
  }

  _r2tBegin = false;
  _r2tCamsSyncWanted = true;
}

bool EngineDD9::R2TCamsSyncNeeded()
{
  if (!_r2tCamsSyncWanted) return false;

  _r2tCamsSyncWanted = false;  
  return true;
}

void EngineDD9::DestroyR2TTargets()
{
  for (int i = 0; i < _r2tTargets.Size(); i++)
  {
    R2TTargetDesc &tgt = _r2tTargets[i];

    tgt._desc._texture.Free();
    tgt._surface.Free();
    tgt._hdrPars.Free();
    tgt._tiPars.Free();
    tgt._clrPars.Free();
    tgt._active = false;
  }
}

void EngineDD9::TrackR2TTexUsage(IDirect3DTexture9 *texture)
{
  if (!_r2tBegin)
  {
    for (int i = 0; i < _r2tTargets.Size(); i++)
    {
      if (_r2tTargets[i]._desc._texture == texture)
      {
        _r2tTargets[i]._frameId = FrameId;
        break;
      }
    }
  }
}

void EngineDD9::CreateR2TDepthAsTexture()
{
  DoAssert(_r2tBegin);

  RECT rect = { 0, 0, _viewW, _viewH };
  CALL_D3D(StretchRect,_renderTargetDepthInfoSurface,&rect,_r2tRT._renderTargetDepthInfoSurface, NULL, D3DTEXF_NONE);
}

ComRef<IDirect3DTexture9> EngineDD9::CreateR2TRenderTargetCopyAsTexture(IDirect3DDevice9 *device, const RECT *rect, D3DTEXTUREFILTERTYPE filter)
{
#if !_DEBUG
  DoAssert(!CheckMainThread());
#endif
  
  // create current r2t tgt copy
  HRESULT hr = device->StretchRect(_r2tRT._renderTargetSurfaceS, rect, _r2tRT._rtCopySurface, NULL, filter);
  DoAssert(hr == S_OK);

  return _r2tRT._rtCopy;
}

void EngineDD9::RegisterR2TSourceTexture(IDirect3DTexture9 *texture, int w, int h, RString name)
{
  if (!texture || name.IsEmpty()) return;

#if _ENABLE_REPORT
  LogF("Register r2t texture %s, %x", cc_cast(name), texture);
#endif

  R2TTargetDescLite &src = _r2tSrcTargets.Append();
  src._texture += texture;
  src._width = w;
  src._height = h;
  src._name = name;
}

void EngineDD9::UnRegisterR2TSourceTexture(IDirect3DTexture9 *texture)
{
  if (!texture) return;
  
  int index = -1;
  for (int i = 0; i < _r2tSrcTargets.Size(); i++)
  {
    if (_r2tSrcTargets[i]._texture.GetRef() == texture)
    {
      index = i;
      break;
    }
  }

  // release texture
  if (index != -1)
  {
#if _ENABLE_REPORT
    LogF("Unregister r2t texture %x", texture);
#endif
    _r2tSrcTargets.Delete(index);
  }
  else
  {
    // texture is bind to any render target
    for (int i = 0; i < _r2tTargets.Size(); i++)
    {
      R2TTargetDesc &tgt = _r2tTargets[i];
      if (tgt._desc._texture.GetRef() == texture)
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

void EngineDD9::UnRegisterAllR2TSourceTextures()
{
  _r2tSrcTargets.Clear();
}

RenderTargetID EngineDD9::BindR2T(const RString &name)
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
      dst._hdrPars = new R2THDRPars();
      dst._hdrPars->Initialize(this);
      dst._tiPars = new R2TTiPars();
      dst._tiPars->Initialize(this);

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

bool EngineDD9::UnBindR2T(RenderTargetID rtid)
{
  DoAssert(!_r2tBegin);
  
  if (IsR2TValid(rtid))
  {
    InvalidateR2TTarget(_r2tTargets[rtid], true);
    return true;
  }

  return false;
}

bool EngineDD9::IsBindR2T(RenderTargetID rtid) const
{
  if (IsR2TValid(rtid))
  {
    RenderTargetID id = _r2tTargets[rtid]._id;  
    return (!_r2tTargets[rtid]._releaseWanted && id != R2TUninitialized);
  }

  return false;
}

//! Check whether target was used in main scene
bool EngineDD9::WasR2TUsed(RenderTargetID rtid) const
{
#if _ENABLE_REPORT
  DoAssert(IsR2TValid(rtid));
#endif

  return FrameId < (_r2tTargets[rtid]._frameId + R2TRenderTargetCount);
}

void EngineDD9::InitR2TDraw() 
{
  if (_thermalVision)
  {
    // Fog color is derived from the air temperature in thermal case
    float fR, fG, fB;
    EncodeTemperatureToRGB1(_airTemperature, fR, fG, fB);
    _clearColor = Color(fR, fG, fB, 1.0f);

    // Set texture and sampling parameters
    SetTexture(-1,TEXID_TC, _tableTc, false);
    D3DSetSamplerState(-1, TEXID_SHADOWMAP, D3DSAMP_SRGBTEXTURE, FALSE);
    D3DSetRenderState(-1, D3DRS_SRGBWRITEENABLE,FALSE);
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

void EngineDD9::BeginR2T(RenderTargetID rtid)
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

void EngineDD9::EndR2T(RenderTargetID rtid)
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

void EngineDD9::SaveR2TToFile(IDirect3DSurface9 *surfToSave) const
{
#if _ENABLE_REPORT
  if (surfToSave)
  {
    BString<64> str;
    str.PrintF("%s%x.png", "RenderTarget", (int)surfToSave);
    D3DXSaveSurfaceToFile(str.cstr(), D3DXIFF_PNG, surfToSave, NULL, NULL);
  }

  if (_r2tRT._renderTargetSurfaceS)
  {
    BString<64> str;
    str.PrintF("%s%x.png", "RenderTargetSurf", (int)surfToSave);

    D3DXSaveSurfaceToFile(str.cstr(), D3DXIFF_PNG, 
      const_cast<IDirect3DSurface9 *>(_r2tRT._renderTargetSurfaceS.GetRef()), NULL, NULL);
  }
#endif
}

void EngineDD9::R2TPostProcess(int effect, float deltaT, RenderTargetID rtid)
{
  if (IsR2TValid(rtid))
  {
    R2TTargetDesc &desc = _r2tTargets[rtid];

    if (desc._hdrPars && desc._tiPars)
    {
      // get current hdr settings
      R2T_PARS_TO_ASSIGN(desc._hdrPars->, );
      
      if (IsLLRecording())
        CALL_D3D(R2TPostProcess, effect, deltaT, rtid);
    }
    else
      InvalidateR2TTarget(desc, true);
  }
}

float EngineDD9::GetLastKnownR2TAvgIntensity(RenderTargetID rtid) const
{
  DoAssert(IsR2TValid(rtid));

  return _r2tTargets[rtid]._hdrPars->_lastKnownAvgIntensity;
}

bool EngineDD9::R2THDRPars::Initialize(EngineDD9 *engine) 
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
    HRESULT err = engine->CreateTextureRT(1, 1, 1, engine->_apertureFormat, D3DPOOL_DEFAULT, _rtApertureR2T[i], _rtsApertureR2T[i], true);
    if (FAILED(err))
    {
      engine->DDError9("Post process aperture texture creation failed", err);
      return false;
    }
  }

  DoAssert(_querySinglePassR2T == NULL);
  _querySinglePassR2T = new QueryManagerSinglePass();
  _querySinglePassR2T->ResetQueries();

  return true;
}

void EngineDD9::R2THDRPars::UnInitialize()
{
  for (int i = 0; i < 2; i++)
  {
    _rtApertureR2T[i].Free();
    _rtsApertureR2T[i].Free();
  }

  if (_querySinglePassR2T)
    delete _querySinglePassR2T;
  
  _querySinglePassR2T = NULL;
}

bool EngineDD9::R2TTiPars::Initialize(EngineDD9 *engine)
{
  _tiMode = 0;
  _deltaT = 0.0f;
  _maxTemperatureR2T = TempMin;
  ZeroMemory(_temperaturesR2T, sizeof(int) * lenof(_temperaturesR2T));

  DoAssert(_queryR2T == NULL);
  _queryR2T = new QueryManager();

  return true;
}

void EngineDD9::R2TTiPars::UnInitialize()
{
  if (_queryR2T)
    delete _queryR2T;

  _queryR2T = NULL;
}

EngineDD9::R2TColorsPars::R2TColorsPars()
{
  Initialize(NULL);
}

bool EngineDD9::R2TColorsPars::Initialize(EngineDD9 *engine)
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

void EngineDD9::R2TColorsPars::Update(const float *pars, int count)
{
  DoAssert(count >= 16);

  _enabled = pars[0] > 0.5f;
  _brightness = pars[1];
  _contrast = pars[2];
  _offset = pars[3];
  _lerpColor = Color(pars[4], pars[5], pars[6], pars[7]);
  _colorizeColor = Color(pars[8], pars[9], pars[10], pars[11]);
  _rgbWeights = Color(pars[12], pars[13], pars[14], pars[15]);
}

void EngineDD9::DoR2TPostProcess(int vMode, float deltaT, RenderTargetID rtid)
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
      target._hdrPars->_eyeAdaptMinR2T = GScene->MainLight()->GetEyeAdaptMin(true);
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

void EngineDD9::R2TSetPEColorsPars(RenderTargetID rtid, const float* pars, int count)
{
  DoAssert(IsR2TValid(rtid) && CheckMainThread());

  R2TTargetDesc &target = _r2tTargets[rtid];
  
  if (target._clrPars.IsNull()) target._clrPars = new R2TColorsPars();

  if (target._clrPars)
  {
    target._clrPars->Update(pars, count);
  }
}

void EngineDD9::SetR2TRenderTargets()
{
  DoAssert(_r2tRT._renderTargetSaved.IsNull());
  DoAssert(_r2tRT._renderTargetSurfaceSaved.IsNull());
  DoAssert(_r2tRT._renderTargetDepthBufferSurfaceSaved.IsNull());
  
  _r2tRT._renderTargetSaved = _currRenderTarget;
  _r2tRT._renderTargetSurfaceSaved = _currRenderTargetSurface;
  _r2tRT._renderTargetDepthBufferSurfaceSaved = _currRenderTargetDepthBufferSurface;
  
  _r2tRT._minZRangeSaved = _minZRange;
  _r2tRT._maxZRangeSaved = _maxZRange;
  
  SetRenderTargetAndDepthStencil(
    _r2tRT._renderTargetSurfaceS, 
    _r2tRT._renderTargetS,
    GetFSAA() > 0 ? _depthBufferRT : _depthBuffer);
  
  Clear(false, true, PackedColor(HBlack));

  // note: viewport is reset here, z is set to 0..1 range by the SetRenderTarget call
  _minZRange = 0;
  _maxZRange = 1;

  // Set the depth range to make sure the depth range has not been changed
  SetDepthRange(_minZRangeSaved,_maxZRangeSaved);

  D3DBeginScene();
}

void EngineDD9::RestoreR2TRenderTargets(RenderTargetID id, bool saveEnable)
{
  R2TTargetDesc &r2tTarget = _r2tTargets[id];

  if (r2tTarget._id != R2TUninitialized)
  {
    RECT rect = { 0, 0, r2tTarget._desc._width, r2tTarget._desc._height };

    if (_r2tRT._renderTargetSurfaceS && r2tTarget._surface)
      CALL_D3D(R2TCopyTarget, _r2tRT._renderTargetSurfaceS.GetRef(), &rect, r2tTarget._surface, saveEnable);
  }

  SetRenderTargetAndDepthStencil(
    _r2tRT._renderTargetSurfaceSaved, _r2tRT._renderTargetSaved, _r2tRT._renderTargetDepthBufferSurfaceSaved);
  
  _r2tRT._renderTargetSaved.Free();
  _r2tRT._renderTargetSurfaceSaved.Free();
  _r2tRT._renderTargetDepthBufferSurfaceSaved.Free();

  SetDepthRange(_r2tRT._minZRangeSaved, _r2tRT._maxZRangeSaved);

  _clearColor = _r2tRT._clearColor;

  D3DEndScene();
}

#endif

void EngineDD9::SetTemporaryRenderTarget(IDirect3DTexture9 *rt, IDirect3DSurface9 * rts, IDirect3DSurface9 *dts)
{
  DoAssert(_renderTargetSaved.IsNull());
  DoAssert(_renderTargetSurfaceSaved.IsNull());
  DoAssert(_renderTargetDepthBufferSurfaceSaved.IsNull());
  _renderTargetSaved = _currRenderTarget;
  _renderTargetSurfaceSaved = _currRenderTargetSurface;
  _renderTargetDepthBufferSurfaceSaved = _currRenderTargetDepthBufferSurface;
  _minZRangeSaved = _minZRange;
  _maxZRangeSaved = _maxZRange;
  SetRenderTargetAndDepthStencil(rts, rt, dts);
  // note: viewport is reset here, z is set to 0..1 range by the SetRenderTarget call
  _minZRange = 0;
  _maxZRange = 1;

  // Set the depth range to make sure the depth range has not been changed
  SetDepthRange(_minZRangeSaved,_maxZRangeSaved);
}

void EngineDD9::RestoreOldRenderTarget()
{
  SetRenderTargetAndDepthStencil(_renderTargetSurfaceSaved, _renderTargetSaved, _renderTargetDepthBufferSurfaceSaved);
  _renderTargetSaved.Free();
  _renderTargetSurfaceSaved.Free();
  _renderTargetDepthBufferSurfaceSaved.Free();
  SetDepthRange(_minZRangeSaved,_maxZRangeSaved);
}

/*!
\patch 5163 Date 06/04/2007 by Ondra
- Fixed: Bridges roads had different anisotropic filtering level than usual roads
*/
inline int EngineDD9::AnisotropyLevel(PixelShaderID ps, bool road, TexFilter filter) const
{
  // based on filter and current settings we derive the level
  if (filter<TFAnizotropic) return 1;
  if (_afWanted<=0) return 1;

  int level = 2;
  if (filter==TFAnizotropic4) level = 4;
  else if (filter==TFAnizotropic8) level = 8;
  else if (filter==TFAnizotropic16) level = 16;

  saturate(level, 0, _afWanted);
  if (level > _maxAFLevel)
    level = _maxAFLevel;

  return level;
}

void EngineDD9::SetTextureAndMaterial( int cb, const TextureD3D9 *tex, const TexMaterial *mat, int specFlags, int filterFlags, DefTIMode defTI )
{
  if (!tex && mat)
  {
    tex = static_cast<TextureD3D9*>(mat->_stage[0].GetTextureForDraw());
  }
  
  SetTexture0(cb,tex);

  // TI addressing taken from the spec flags at the beginning (it might be rewritten later by the material)
  // Same code used here: ADDRESSSTAGE0
  bool clampU=false,clampV=false;
  if( specFlags&ClampU ) clampU=true;
  if( specFlags&ClampV ) clampV=true;
  D3DTEXTUREADDRESS tiAddressU=( clampU ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP );
  D3DTEXTUREADDRESS tiAddressV=( clampV ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP );

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
    TextureD3D9 *secTex[UsableSampleStages-1];
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
      secTex[i] = static_cast<TextureD3D9*>(tex);
      if (tex)
      {
        int clamp = tex->GetClamp();
        D3DTEXTUREADDRESS addressU=( clamp&TexClampU ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP );    
        D3DTEXTUREADDRESS addressV=( clamp&TexClampV ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP );

        D3DSetSamplerState(cb, i+1,D3DSAMP_ADDRESSU,addressU);
        D3DSetSamplerState(cb, i+1,D3DSAMP_ADDRESSV,addressV);

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
    D3DSetSamplerState(cb, TEXID_TI, D3DSAMP_ADDRESSU, tiAddressU);
    D3DSetSamplerState(cb, TEXID_TI, D3DSAMP_ADDRESSV, tiAddressV);

    // Use the mate's filtering
    SetFiltering(cb, TEXID_TI, tiFilter, tiMaxAFLevel);

    // Use the SRGB, if possible
    if (_caps._canReadSRGB)
    {
      D3DSetSamplerState(cb, TEXID_TI, D3DSAMP_SRGBTEXTURE, _linearSpace);
    }

    // If TI texture explicitly set then use it, else use the default texture
    TextureD3D9 *textureTI;
    if (mat && mat->_stageTI._tex.NotNull())
    {
      textureTI = static_cast<TextureD3D9*>(mat->_stageTI._tex.GetRef());
    }
    else if (defTI==TIMan)
    {
      textureTI = _textBank->GetDefTIManTexture();
    }
    else /*if (defTI==TIDefault)*/
    {
      textureTI = _textBank->GetDefTITexture();
    }

    // Set the TI texture
    DoAssert(textureTI);
    SetTextureR(cb, TEXID_TI, textureTI);
  }
}

void EngineDD9::D3DSetRenderState(int cb, D3DRENDERSTATETYPE state, DWORD value, bool optimize)
{
  int index = state;

  if (CHECK_ENG_DIAG(EDENoFilterRS)) optimize = false;
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
  if(!optimize || CheckRSNeedsToBeSet(cbState, index, value, cbState._rec._currPredication))
  {
    CALL_CB(cb,SetRenderState,state,value);
  }
}


void EngineDD9::SetStencilBufferTest(int cb, bool test)
{
	if (!FULL_STENCIL)
		D3DSetRenderState(cb, D3DRS_STENCILENABLE, test);
}


void EngineDD9::D3DEnableAlphaToCoverage(int cb, bool value, bool optimize)
{
  #if XBOX_D3D
    D3DSetRenderState(cb, D3DRS_ALPHATOMASKENABLE,value,optimize);
  #else
    if (_aToCSupported==AToC_ATI)
    {
      // ATI
      D3DSetRenderState(cb, D3DRS_POINTSIZE, value ? MAKEFOURCC('A','2','M','1') : MAKEFOURCC('A','2','M','0'),optimize);
    }
    else if (_aToCSupported==AToC_nVidia)
    {
      // nVidia, see http://developer.nvidia.com/object/devnews028.html
      D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE, value);
      D3DSetRenderState(cb, D3DRS_DITHERENABLE, FALSE);
      //D3DSetRenderState(cb, D3DRS_ALPHAREF, 1);
      D3DSetRenderState(cb, D3DRS_ADAPTIVETESS_Y, value ? (D3DFORMAT)MAKEFOURCC('A', 'T', 'O', 'C') : D3DFMT_UNKNOWN, optimize);
    }
    else
    {
      Fail("Unsupported AToC mode");
    }
  #endif
}

void EngineDD9::D3DSetSamplerState(int cb, DWORD stage, D3DSAMPLERSTATETYPE state, DWORD value, bool optimize)
{
  int index = state;

#if DO_COUNTERS
  static OptimizeCounter opt("SamplerState");
#endif
  // assume small values of state (in DX6 max. state was about 25)
  AutoArray<SamplerStateInfo> &samplerState=CBState(cb)._samplerState[stage];
  samplerState.Access(index);
  SamplerStateInfo &info=samplerState[index];
  if( info==value && optimize  && !CHECK_ENG_DIAG(EDENoFilterRS))
  {
#if DO_COUNTERS
    opt.Skip();
#endif
    return;
  }
  info = value;
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
  CALL_CB(cb,SetSamplerState,stage,state,value);
  return;
}

void EngineDD9::SetupMaterialTextures(const TexMaterialLODInfo &mat, int cb, const EngineShapeProperties & prop, int specFlags)
{
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


void EngineDD9::DoPrepareTriangle(
  int cb, TextureD3D9 *tex, const TexMaterialLODInfo &mat, int level, int spec, const EngineShapeProperties &prop
)
{
  PROFILE_DX_SCOPE_DETAIL(prepT);

  bool clampU=false,clampV=false;

  RendState &cbState = CBState(cb);
  //Assert( (spec&(NoClamp|ClampU|ClampV))!=0 );
  // all triangles are marked for clamping
  if( spec&ClampU ) clampU=true;
  if( spec&ClampV ) clampV=true;
  D3DTEXTUREADDRESS addressU=( clampU ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP );
  D3DSetSamplerState(cb, 0, D3DSAMP_ADDRESSU, addressU);
  D3DTEXTUREADDRESS addressV=( clampV ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP );
  D3DSetSamplerState(cb, 0, D3DSAMP_ADDRESSV, addressV);

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
      if (m.GetRenderFlag(RFNoTiWrite) && GetThermalVision()) specFlags |= (IsHidden | NoZWrite);
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
        
        case PSSuperAToC: aToC = true; break;
      }
    }
  }
  
  PredicationMode pass = CurrentPredicationMode();

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
      D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE,0);
      D3DSetRenderState(cb, D3DRS_ZENABLE, TRUE);
      if (specFlags&IsShadowVolume)
      {
        // Shadow volume
        D3DSetRenderState(cb, D3DRS_ZFUNC, D3DCMP_GREATEREQUAL);
        D3DSetRenderState(cb, D3DRS_ZWRITEENABLE, FALSE);
      }
      else
      {
        // Ordinary shadow
        D3DSetRenderState(cb, D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
        if (!_caps._hasStencilBuffer)
        {
          D3DSetRenderState(cb, D3DRS_ZWRITEENABLE,TRUE);
        }
        else
        {
          D3DSetRenderState(cb, D3DRS_ZWRITEENABLE,FALSE);
        }
      }
    }
    else
    {
      Assert(_renderingMode!=RMCrater);
      if (specFlags&IsHidden)
      {
        D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE,0);
      }
      else if (_renderingMode == RMShadowBuffer && _sbTechnique != SBT_Default)
      { /// _Default technique requires us to write into the render target as well
        D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE, 0);
      }
      else
      {
        // local functor (closure) to prevent code duplication in predicated branches
        struct SetColorWriteEnable
        {
          void operator () (EngineDD9 *engine, int cb, int specFlags) const
          {
            if (specFlags&(NoZWrite|NoAlphaWrite))
            {
              if (specFlags&NoColorWrite)
              {
                // sometimes we may want to write alpha only 
                engine->D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE,(specFlags&NoAlphaWrite) ? 0 : D3DCOLORWRITEENABLE_ALPHA);
              }
              else
              {
                engine->D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
              }
            }
            else if (specFlags&NoColorWrite)
            {
              engine->D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_ALPHA);
            }
            else
            {
              engine->D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
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
            D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE,0);
            EndPredication(cb);
          }
          if (BeginPredication<RenderingPass>(cb,pass))
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
        D3DSetRenderState(cb, D3DRS_ZFUNC,D3DCMP_ALWAYS);
        D3DSetRenderState(cb, D3DRS_ZWRITEENABLE,FALSE);
        D3DSetRenderState(cb, D3DRS_ZENABLE,FALSE);
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
        D3DSetRenderState(cb, D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
        D3DSetRenderState(cb, D3DRS_ZWRITEENABLE,FALSE);
        D3DSetRenderState(cb, D3DRS_ZENABLE,TRUE);
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
        
        if (BeginPredication<RenderingPass>(cb,pass))
        {
          if(cbState._zPrimingDone && !(specFlags&IsAlpha) && ZPrimed()) // see LABEL_ZPRIME
          {
            D3DSetRenderState(cb, D3DRS_ZFUNC,D3DCMP_EQUAL);
            if (GetFSAA() > 0 && _useAToC)
            {
              // When we are using ATOC, we have to write to z-buffer (even if the z-value is the same)
              // Reason: CSAA only updates coverage samples when we write to z-buffer
              // Not writing to z-buffer caused artifacts
              // For more info see news:ijjlln$baq$1@new-server.localdomain
              D3DSetRenderState(cb, D3DRS_ZWRITEENABLE,TRUE);              
            }
            else
            {
            D3DSetRenderState(cb, D3DRS_ZWRITEENABLE,FALSE);
          }
          }
          else
          {
            // note: for alpha blended primitives we still need to perform full z buffer compare + update when rendering
            D3DSetRenderState(cb, D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
            D3DSetRenderState(cb, D3DRS_ZWRITEENABLE,TRUE);
          }
          EndPredication(cb);
        }
        
        if (BeginPredication<PrimingPass,DepthPass>(cb,pass))
        {
          // when priming we need to perform full z-test
          D3DSetRenderState(cb, D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
          D3DSetRenderState(cb, D3DRS_ZWRITEENABLE,TRUE);
          EndPredication(cb);
        }
        
        D3DSetRenderState(cb, D3DRS_ZENABLE,TRUE);

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

      D3DSetRenderState(cb, D3DRS_ALPHABLENDENABLE,FALSE);
      if (_renderingMode == RMShadowBuffer)
      {
        ShadowsStatesSettings(cb, SS_ShadowBuffer,noCull);
      }
      else if (specFlags&IsShadowVolume)
      {
        D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE,FALSE);
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
          D3DSetRenderState(cb, D3DRS_ALPHAREF,0xff);
          D3DSetRenderState(cb, D3DRS_ALPHABLENDENABLE,FALSE);
          D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE,FALSE);
          EndPredication(cb);
        }
        if (BeginPredication<RenderingPass>(cb,pass))
        {
          D3DSetRenderState(cb, D3DRS_ALPHAREF,1);
          D3DSetRenderState(cb, D3DRS_ALPHABLENDENABLE,TRUE);
          D3DSetRenderState(cb, D3DRS_SRCBLEND,D3DBLEND_SRCDEF);
          switch (specFlags&(DstBlendOne|DstBlendZero))
          {
          default:
          case 0:
            D3DSetRenderState(cb, D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
            D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE,TRUE);
            break;
          case DstBlendOne:
            // Disabled alpha test - otherwise moon halo causes artifacts
            D3DSetRenderState(cb, D3DRS_DESTBLEND,D3DBLEND_ONE);
            D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE,FALSE);
            break;
          case DstBlendZero:
            D3DSetRenderState(cb, D3DRS_DESTBLEND,D3DBLEND_ZERO);
            D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE,FALSE);
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
              D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE,FALSE);
              EndPredication(cb);
            }
            if (BeginPredication<RenderingPass>(cb,pass))
            {
              // We want to discretize alpha for alpha-tested surfaces.
              // Reason - alpha tested textures are drawn with z-test set to equal, so no alpha blending can occur
              // (because everything behind the surface will not be drawn). That would cause the problem with fog, if
              // the alpha was not discrete (texture filtering causes the alpha to be contiguous even for 1-bit alpha tests)
              D3DXVECTOR4 vector4(0, 0, 0, 1);
              SetPixelShaderConstantF(cb, PSC_DiscretizeAlpha, vector4, 1);

              D3DSetRenderState(cb, D3DRS_ALPHAREF, 128);
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
                D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE,FALSE);
              }
              else
              {
                D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE,TRUE);
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
          D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE,FALSE);
        }
        
        D3DSetRenderState(cb, D3DRS_ALPHABLENDENABLE, FALSE);
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
  SetTextureAndMaterial(cb,tex,mat._mat,spec,specFlags&FilterMask,mat._defTI);
  //HRESULT err=D3D_OK;
  //if( err ) DDError9("Cannot set texture",err);
#endif
}

void EngineDD9::QueuePrepareTriangle( const MipInfo &absMip, const TexMaterial *mat, int specFlags )
{
  TextureD3D9 *tex = static_cast<TextureD3D9 *>(absMip._texture);
  int level = absMip._level;
  AllocateQueue(tex,level,mat,specFlags);
  Assert (_queueNo._triUsed);
}

void EngineDD9::PrepareTriangle(int cb, const MipInfo &absMip, const TexMaterial *mat, int specFlags0)
{
  Assert(cb<0);
  TextureD3D9 *tex = static_cast<TextureD3D9 *>(absMip._texture);
  SwitchRenderMode(RMTris);
  // allocate some queue
  SwitchTL(cb,TLDisabled);
  int level = absMip._level;
  AllocateQueue(tex,level,mat,specFlags0);
  Assert (_queueNo._triUsed);
  CBState(cb)._prepSpec = specFlags0;

}

void EngineDD9::PrepareTriangleTL(
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

  TextureD3D9 *tex = static_cast<TextureD3D9 *>(mip._texture);
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

void EngineDD9::DrawDecal(
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

void EngineDD9::DoDrawDecal(
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

void EngineDD9::DrawPolygon( const VertexIndex *ii, int n )
{
  if (ResetNeeded()) return;
  QueueFan(ii,n);
}

void EngineDD9::DrawSection
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

void EngineDD9::ResetShaderConstantValues(int cb)
{
  for (int i = 0; i < lenof(CBState(cb)._shaderConstValues); i++)
  {
    for (int j = 0; j < 4; j++)
    {
      CBState(cb)._shaderConstValues[i][j] = FLT_MAX;
    }
  }
  for (int i = 0; i < lenof(CBState(cb)._shaderConstValuesI); i++)
  {
    for (int j = 0; j < 4; j++)
    {
      CBState(cb)._shaderConstValuesI[i][j] = INT_MAX;
    }
  }
  for (int i = 0; i < lenof(CBState(cb)._shaderConstValuesB); i++)
  {
    CBState(cb)._shaderConstValuesB[i] = -1;
  }
  for (int i = 0; i < lenof(CBState(cb)._pixelShaderConstValues); i++)
  {
    for (int j = 0; j < 4; j++)
    {
      CBState(cb)._pixelShaderConstValues[i][j] = FLT_MAX;
    }
  }
  for (int i = 0; i < lenof(CBState(cb)._pixelShaderConstValuesI); i++)
  {
    for (int j = 0; j < 4; j++)
    {
      CBState(cb)._pixelShaderConstValuesI[i][j] = INT_MAX;
    }
  }
}

/**
the function is very short and no lazy calling of it has any sense.
Comparing current state is more difficult than finding a shader
*/

void EngineDD9::SetupSectionTL(
  int cb, EMainLight mainLight, EFogMode fogMode,
  VertexShaderID vertexShaderID, const UVSource *uvSource, int uvSourceCount, bool uvSourceOnlyTex0,
  ESkinningType skinningType,
  bool instancing, int instancesCount, int pointLightsCount, int spotLightsCount,
  bool shadowReceiver, RenderingMode renderingMode, bool backCullDisabled, int cratersCount,
  const TexMaterial *mat
)
{
  //ADD_COUNTER(sSecTL,1);
  PROFILE_DX_SCOPE_DETAIL(3dLVS);

  if (vertexShaderID < 0 || vertexShaderID >= NVertexShaderID)
  {
    Fail("Error: Vertex shader ID out of limits");
    //cbState._vertexShader.Free();
    return;
  }

  // Set vertex shader
  PredicationMode pass = CurrentPredicationMode();

  // select vertex shaders for rendering and priming
  ComRefFake<IDirect3DVertexShader9> vs[NPredicationModes];
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
      SetVertexShaderConstantB(cb, 0, fogModeB, 2);

      // ShadowReceiverFlag
      // SBTechniqueDefault
      // EnableAlignNormal
      bool sbTechniqueDefault = (_sbTechnique == SBT_Default);
      bool c456[]={shadowReceiver,sbTechniqueDefault,backCullDisabled};
      SetVertexShaderConstantB(cb, 4, c456, 3);
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
        vs[RenderingPass] = _vertexShaderPool[VSToVSP[vertexShaderID]][uvInputN][mainLightN][rMode][siN];
        if (rMode!=RMCrater && rMode!=RMShadowBuffer)
        {
          vs[PrimingPass] = _vertexShaderPool[VSToVSP[vertexShaderID]][uvInputN][mainLightN][RMDPrime][siN];
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
      IDirect3DVertexShader9 *shader = _vertexShaderShadowVolume[siN];
      for (int i=0; i<NPredicationModes; i++) vs[i] = shader; // pass all the same to indicate no predication
      noPredication = true;
    }
    else if (VSToVSS[vertexShaderID] < NVertexShaderSpecialID)
    {
      // Use the vertex shader
      IDirect3DVertexShader9 *shader = _vertexShaderSpecial[VSToVSS[vertexShaderID]];
      for (int i=0; i<NPredicationModes; i++) vs[i] = shader; // pass all the same to indicate no predication
      noPredication = true;
      // should not be called in priming pass at all
      Assert(pass!=PrimingPass);
    }
    else if (vertexShaderID == VSTerrain)
    {
      // Use the vertex shader
      vs[RenderingPass] = _vertexShaderTerrain[rMode];
      vs[PrimingPass] = _vertexShaderTerrain[RMDPrime];
      vs[DepthPass] = vs[PrimingPass];
    }
    else if (vertexShaderID == VSTerrainGrass)
    {
      // Use the vertex shader
      vs[RenderingPass] = _vertexShaderTerrainGrass[rMode];
      vs[PrimingPass] = _vertexShaderTerrainGrass[RMDPrime];
      vs[DepthPass] = vs[PrimingPass];
    }
  }

  // Set pixel shader and some pixel shader constants
  DoSelectPixelShader(cb,mat,cratersCount);

  #if TRACK_SHADER_PATCH
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
    ComRefFake<IDirect3DVertexShader9> (&vsToSet)[NPredicationModes] = vs;
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


inline void FixLightColorInDay(Color& color, float ambBrightness )
{
	float bright = color.Brightness();

	if (bright <= 0.00001f)
		return;

	float diff = ambBrightness - bright;
	if (diff > 0.0f)
	{
		float ratio = ambBrightness/bright;
		color = color*Color(ratio, ratio, ratio, 1.0f);
	}
}


void EngineDD9::SetupVSConstantsPSLights(int cb, bool setupPSInstead)
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

	if (setupPSInstead)
	{
		PASSMATRIXTYPE ilwsMatrix[2];
		ConvertMatrixTransposed(ilwsMatrix[0], CBState(cb)._world);
		SetPixelShaderConstantF(cb, LIGHTSPACE_LWS_MATRIX_PS, (float*)&ilwsMatrix, 3);
	}

	float nightMul = _night;

	#ifdef ENABLE_DAY_LIGHTS
		nightMul = 1.0f;
	#endif

  {
    SLPoint LPointData[MAX_LIGHTS];
    int pointLightIndex = 0;
    for (int i = 0; i < CBState(cb)._lights.Size(); i++)
    {
			const Light* light = CBState(cb)._lights[i];
      if (light->GetType() == LTPoint)
      {
				LightDescription ldesc;
				light->GetDescription(ldesc, GetAccomodateEye());

        // temporary solution for SkinShader - _matEmmisive and matForcedDiffuse holds skin params
        Color matForcedDiff;
        Color matAmbient;
        if( ( CBState(cb)._vertexShaderID == VSSkin ) || ( CBState(cb)._vertexShaderID == VSTreeAdv ) || ( CBState(cb)._vertexShaderID == VSTreeAdvTrunk ) ) //|| ( CBState(cb)._vertexShaderID == VSTreeAdvSimple ) )
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
        float sa = ldesc.invRadius;
        LPointData[pointLightIndex].Atten = D3DXVECTOR4(sa, sa, sa, sa);

        // LPoint_D
        Color pointD = ldesc.diffuse * nightMul * CBState(cb)._matDiffuse;
        LPointData[pointLightIndex].D = D3DXVECTOR4(pointD.R(), pointD.G(), pointD.B(), CBState(cb)._matDiffuse.A());

        // LPoint_A
        Color pointA = ldesc.ambient * nightMul * matAmbient + ldesc.diffuse * matForcedDiff * nightMul;
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
        SetPixelShaderConstantF(cb,LIGHTSPACE_START_REGISTER_PS, (float*)LPointData, pointLightIndex * vectorsPerLight);
      }
      else
      {
        SetVertexShaderConstantF(cb,LIGHTSPACE_START_REGISTER_VS, (float*)LPointData, pointLightIndex * vectorsPerLight);
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
			const Light* light = CBState(cb)._lights[i];
      if (light->GetType() == LTSpotLight)
      {
				LightDescription ldesc;
				light->GetDescription(ldesc, GetAccomodateEye());

	      // temporary solution for SkinShader - _matEmmisive and matForcedDiffuse holds skin params
        Color matForcedDiff;
        if( ( CBState(cb)._vertexShaderID == VSSkin ) || ( CBState(cb)._vertexShaderID == VSTreeAdv ) || ( CBState(cb)._vertexShaderID == VSTreeAdvTrunk ) ) //|| ( CBState(cb)._vertexShaderID == VSTreeAdvSimple ) )
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
        Color spotD = ldesc.diffuse * nightMul * CBState(cb)._matDiffuse;
        LSpotData[spotLightIndex].D_InvCTHMCPH = D3DXVECTOR4(spotD.R(), spotD.G(), spotD.B(), 0.0f);

        // LSpot_A
        Color spotA = ldesc.ambient * nightMul * CBState(cb)._matAmbient + ldesc.diffuse * matForcedDiff * nightMul;
        LSpotData[spotLightIndex].A_Atten = D3DXVECTOR4(spotA.R(), spotA.G(), spotA.B(), ldesc.invRadius);

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
        SetPixelShaderConstantF(cb,LIGHTSPACE_START_REGISTER_PS + (MAX_LIGHTS - spotLightIndex) * vectorsPerLight, (float*)LSpotData, spotLightIndex * vectorsPerLight);
      }
      else
      {
        SetVertexShaderConstantF(cb,LIGHTSPACE_START_REGISTER_VS + (MAX_LIGHTS - spotLightIndex) * vectorsPerLight, (float*)LSpotData, spotLightIndex * vectorsPerLight);
      }
    }

    // Set the number of spot lights for shader's static looping purposes - see X360_INT_CONSTANT
    int spotLoopCount[4]={spotLightIndex,0,0,0};
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

void EngineDD9::SetupVSConstantsLODPars(int cb)
{
  float lodPars[4];
  _sceneProps->GetTerrainLODPars(lodPars);
  SetVertexShaderConstantF(cb,VSC_TerrainLODPars, lodPars, 1);
}

void EngineDD9::SetupVSConstantsPool(int cb, int minBoneIndex, int bonesCount)
{
  RendState &cbState = CBState(cb);

  // temporary solution for SkinShader - _matAmbient, _matEmmisive and matForcedDiffuse holds specific params
  Color matAmbient;
  Color matEmissiveCoef;
  if( ( CBState(cb)._vertexShaderID == VSSkin ) || ( CBState(cb)._vertexShaderID == VSTreeAdv ) || ( CBState(cb)._vertexShaderID == VSTreeAdvTrunk ) ) //|| ( CBState(cb)._vertexShaderID == VSTreeAdvSimple ) )
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
    else if( (cbState._vertexShaderID == VSTreeAdv) || (cbState._vertexShaderID == VSTreeAdvTrunk) )
    {
      const float k2 = 2.0f;
      float crownBleedMul = cbState._matAmbient.R() * k2 * 10.0f;
      float crownBleedAdd = cbState._matAmbient.G() * k2 * 10.0f - 5.0f;
      Color data = Color( crownBleedMul, crownBleedAdd, 0, 0 );
      SetVertexShaderConstantF( cb, VSC_TreeAdvPars, (float*)&data, 1 );
    }
    // Glass
    if (cbState._vertexShaderID == VSGlass || cbState._vertexShaderID == VSSuper || cbState._vertexShaderID == VSSkin || cbState._vertexShaderID == VSMulti)
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
      if (!_caps._sm2Used)
        SetupVSConstantsPSLights(cb, cbState._vertexShaderID == VSSuper || cbState._vertexShaderID == VSMulti || cbState._vertexShaderID == VSSkin);
      else
      SetupVSConstantsPSLights(cb);
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

void EngineDD9::SetupVSConstantsShadowVolume(int cb, int minBoneIndex, int bonesCount)
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

void EngineDD9::SetupVSConstantsSprite(int cb)
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

void EngineDD9::SetupVSConstantsPoint(int cb)
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

void EngineDD9::SetupVSConstantsWater(int cb)
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

  SetVertexShaderConstantF(cb, VSC_CalmWaterPars, (const float*) &pos, 1);

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

  SetVertexShaderConstantF( cb, VSC_SeaWaterPars, (const float *)GetSeaWaterParsVS(), SEAWATER_VSPARS_SIZE );
*/
}

void EngineDD9::SetupVSConstantsShore(int cb)
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

  SetVertexShaderConstantF(cb, VSC_CalmWaterPars, (const float*) &pos, 1);


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

  SetVertexShaderConstantF( cb, VSC_SeaWaterPars, (const float *)GetSeaWaterParsVS(), SEAWATER_VSPARS_SIZE );*/

  /*SSeaWaterIn &swip = GetSeaWaterInPars();
  Camera *camera = GScene->GetCamera();
  Vector3 cameraPosition = camera->Position();
  Vector3 cameraObjectPosition = TransformPositionToObjectSpace( cb, cameraPosition );
  D3DXVECTOR4 cpos(cameraObjectPosition.X(), cameraObjectPosition.Y(), cameraObjectPosition.Z(), 1.0f);
  SetVertexShaderConstantF( cb, VSC_CameraPosition, (float*)&cpos, 1 );
  swip.cameraPosition[0] = cameraPosition.X();
  swip.cameraPosition[1] = cameraPosition.Z();
  ComputeSeaWaterPars( this, swip, cbState._matAmbient, cbState._matDiffuse, cbState._matForcedDiffuse, cbState._matEmmisive, cbState._matSpecular, cbState._matPower );
  SetVertexShaderConstantF( cb, VSC_SeaWaterPars, (const float *)GetSeaWaterParsVS(), SEAWATER_VSPARS_SIZE );*/

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

  SetVertexShaderConstantF( cb, VSC_SeaWaterPars, (const float *)GetSeaWaterParsVS(), SEAWATER_VSPARS_SIZE );
*/
}

void EngineDD9::SetupVSConstantsTerrain(int cb)
{
  // Terrain LOD
  SetupVSConstantsLODPars(cb);

  // Fog constants
  SetVertexShaderConstantFFAR(cb);

  // InitLights HLSL function uses the following constants
  SetVertexShaderConstantSAFR(cb,CBState(cb)._matPower, CBState(cb)._matAmbient);

  // Point and spot lights
#if 1 // _VBS3_PERPIXELPSLIGHTS
  if (!_caps._sm2Used)
  {
    SetupVSConstantsPSLights(cb,true);
  }
  else
#endif
  {
    SetupVSConstantsPSLights(cb);
  }
}

void EngineDD9::SetupVSConstantsTerrainGrass(int cb)
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
}

/*!
\patch 5126 Date 2/5/2007 by Flyman
- Fixed: Integer light constants as I_POINTLOOPCOUNT were not recorded and there was a problem when using compass during the day
*/
void EngineDD9::SetupVSConstants(int cb, int minBoneIndex, int bonesCount)
{
  RendState &cbState = CBState(cb);
  if (VSToVSP[cbState._vertexShaderID] < NVertexShaderPoolID)
  {
    SetupVSConstantsPool(cb,minBoneIndex, bonesCount);
  }
  else if (cbState._vertexShaderID == VSShadowVolume)
  {
    SetupVSConstantsShadowVolume(cb,minBoneIndex, bonesCount);
  }
  else if (VSToVSS[cbState._vertexShaderID] < NVertexShaderSpecialID)
  {
    if (cbState._vertexShaderID == VSSprite)
    {
      SetupVSConstantsSprite(cb);
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
  else
  {
    Fail("Error: unknown vertex shader ID");
  }
}

void EngineDD9::SetupPSConstants(int cb)
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
      (vs == VSTreeAdv)
    )
    {
      useGroundBidir = false;
    }
    else
    {
      useGroundBidir = true;
    }

    // Determine the new specular calculation should be used
    bool useNewSpecular = vs == VSSuper || vs==VSSkin || vs == VSMulti;

    SetPixelShaderConstantDBS(
      cb,cbState._matDiffuse,cbState._matForcedDiffuse,cbState._matSpecular,cbState._matPower,
      useGroundBidir,
      useNewSpecular);
    SetPixelShaderConstant_A_DFORCED_E_Sun(cb,cbState._matEmmisive, cbState._matAmbient, cbState._matDiffuse, cbState._matForcedDiffuse);
  }
  else
  {
    SetPixelShaderConstantDBSZero(cb, cbState._matPower);
    SetPixelShaderConstant_A_DFORCED_E_NoSun(cb,cbState._matEmmisive);
  }
}

void D3DBoundShadersManager::Bind(EngineDD9 *engine, const D3DBoundShaderName &name)
{
  // create a slot in the hash table  
  D3DBoundShader bound;
  bound._name = name;
  
  // and add into the hashmap
  _bound.Add(bound);
}

void EngineDD9::DrawSectionTL(int cb, const Shape &sMesh, int beg, int end, int bias, int instancesOffset, int instancesCount, const DrawParameters &dp)
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
  
  
  
  if (cbState._skinningType == STNone)
  {
    if (instancing)
    {
      cbState._vertexDecl = _vertexDeclD[sMesh.GetVertexDeclaration()][VSDV_Instancing];
    }
    else
    {
      cbState._vertexDecl = _vertexDeclD[sMesh.GetVertexDeclaration()][VSDV_Basic];
    }
  }
  else
  {
    cbState._vertexDecl = _vertexDeclD[sMesh.GetVertexDeclaration()][VSDV_Skinning];
  }

  // ---------------------------------------------
  // Shader setting ------------------------------

  // Identify the shader to be shadow receiver or not
  bool shadowReceiver = ((cbState._pixelShaderTypeSel == PSTBasicSSSM) || (cbState._pixelShaderTypeSel == PSTShadowReceiver1));

  #if 0
  if (cbState._vertexShaderID==VSSkin || cbState._vertexShaderID==VSSuper)
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
  // Set vertex declaration
  SetVertexDeclaration(cb, cbState._vertexDecl);


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

void EngineDD9::DoDrawSSAO(float fogEnd, float fogStart, float fogCoef)
{
  if (_renderingMode != RMCommon) return;

  if (_pp._ssao.NotNull())
  {
    _pp._ssao->Do(fogEnd, fogStart, fogCoef);
  }
}

void EngineDD9::DrawSSAO()
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
    float minZRange = _minZRange;
    float maxZRange = _maxZRange;

    CALL_CB_DIRECT(this, DrawSSAO, fogEnd, fogStart, fogCoef);

    SetDepthRange(minZRange, maxZRange);
    // SSAO is calling all command directly to device, so we have to invalidate all states in immediate CB
    SetHWToUndefinedState(-1);
  }
}

void EngineDD9::DoDrawEnfDebugShapes()
{
	if (_renderingMode != RMCommon) return;

	if (GScene->GetEnfDebugShapesCount())
	{
		GScene->RenderDebugShapes();
	}
	else
	{
		//static int XXX = 0;
		//LogF("NoShapes %d", XXX++);
	}
}

void EngineDD9::DrawEnfDebugShapes()
{
	Assert (_backgroundD3D!=RecHighLevel); // we must be in the correct recording mode
	Assert (CheckMainThread()); // recording must be done from the main thread

	CALL_CB_DIRECT(this, DrawEnfDebugShapes);	
}


void EngineDD9::DrawStencilShadows(bool interior, int quality)
{
}

void EngineDD9::QueryIssue(int cb, int queryID, int issueCommand)
{
	DoAssert (_backgroundD3D != RecHighLevel);

	IDirect3DQuery9* query = _queryIDs.GetQuery(queryID);

	SetRenderStateTemp  (D3DRS_STENCILENABLE, false);
	CALL_CB(cb, QueryIssue, query, issueCommand);
}

void EngineDD9::DoQueryIssue(IDirect3DQuery9* query, int issueCommand)
{
	Assert(issueCommand == D3DISSUE_BEGIN || issueCommand == D3DISSUE_END);
	query->Issue(issueCommand);
}

void EngineDD9::DoSetFogColor(ColorVal fog)
{
  SetPixelShaderConstantF(-1, PSC_FogColor,fog.GetArray(),1);
}

void EngineDD9::SetFogColor()
{
  // the only thread which should be setting the fog color is the main thread
  Assert(!IsInCBScope());
  if( !_d3DDevice.IsNull() && !ResetNeeded())
  {
    Color fogScaled;

    if (_thermalVision)
    {
      // Fog color is derived from the air temperature in thermal case
      float fR, fG, fB;
      EncodeTemperatureToRGB1(_airTemperature, fR, fG, fB);
      fogScaled = Color(fR, fG, fB, 1.0f);
    }
    else
    {
      fogScaled = _fogColor*GetHDRFactor();
    }

    // when running PS 3 or better, fog saturate no longer needed
    fogScaled.Saturate();
    if (_backgroundD3D==RecHighLevel) RecordHighlevel()->SetFogColor(fogScaled);
    else DoSetFogColor(fogScaled);
  }
}

void EngineDD9::FogColorChanged(ColorVal fogColor )
{
  SetFogColor();
}


#if !defined(_X360SHADERGEN)
HRESULT EngineDD9::HLSLInclude::Open(
                                     D3DXINCLUDE_TYPE includeType, LPCSTR fileName,
                                     LPCVOID parentData,
                                     LPCVOID * data, UINT * bytes
                                     )
#else
HRESULT EngineDD9::HLSLInclude::Open(
                                     D3DXINCLUDE_TYPE includeType, LPCSTR fileName, LPCVOID parentData,
                                     LPCVOID *data, UINT *bytes,
                                     /* OUT */ LPSTR fullPath, DWORD fullPathSize
                                     )
#endif
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
  QIFStreamB in;
  in.AutoOpen(fullName);
  if (in.fail())
  {
    RptF("Cannot open HLSL include %s",cc_cast(fullName));
    data = NULL;
    bytes = 0;
    return D3DXERR_INVALIDDATA;
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
    return D3DXERR_INVALIDDATA;
  }
  *data = buf;
  *bytes = size;
  return S_OK;
}

HRESULT EngineDD9::HLSLInclude::Close(LPCVOID data)
{
  if (data)
  {
    delete[] (char *)data;
  }
  return S_OK;
}

//! Enumerate program definitions for vertex shader compilation
#define PRGDEF_ENUM(type,prefix,XX) \
  XX(type, prefix, PRGDEFName) \
  XX(type, prefix, PRGDEFUVMapTex1) \
  XX(type, prefix, PRGDEFMainLight) \
  XX(type, prefix, PRGDEFRenderingMode) \
  XX(type, prefix, PRGDEFSkinning) \
  XX(type, prefix, PRGDEFInstancing) \

//! Create enum and array of the same names
#ifndef DECL_ENUM_PRGDEF
#define DECL_ENUM_PRGDEF
DECL_ENUM(PRGDef)
#endif
DECLARE_DEFINE_ENUM(PRGDef, , PRGDEF_ENUM)

//! Define array of VertexShaderPoolID names
DEFINE_ENUM(VertexShaderPoolID, VSP, VSP_ENUM)

extern bool EnableSM3;

/**
@param select -1 default, 0..0x2ff = force SM 2.0, 0x300.. = force SM 3.0
*/
void EngineDD9::InitShaderModel(int select)
{
  // determine shader model properties
  int maxFreeConstants = 50;
  // if 1.1 shaders are supported, use them, if not, fall back to SW vertex processing
  //if (_vertexShaders>=0x200 || _vertexShaders<0x101)
  // always used VS 2.0
#if defined(_X360SHADERGEN)
  {
    //    _vsVersion = DebugVS ? "vs_3_0_sw" : "vs_3_0";
    //    _vsVersionLink = DebugVS ? "vs_3_0_sw" : "vs_3_0";
    _caps._vsVersion = "vs_3_0";
    _caps._psVersion = "ps_3_0";
    _caps._vsVersionLink = "vs_3_0";
    // with 20 we have at least 256 registers
    // this could accommodate 50 or even more bones
    maxFreeConstants = 168; // see also maxFreeConstants in FPShaders.hlsl
    InitShaderDirs();
  }
#else
# if 1 // _ENABLE_CHEATS // allow shader model 3.0 for PC
  if (_caps._vertexShaders>=0x300 && _caps._pixelShaders>=0x300 && select<0 && EnableSM3 || select>=0x300)
  {
    //    _vsVersion = DebugVS ? "vs_3_0_sw" : "vs_3_0";
    //    _vsVersionLink = DebugVS ? "vs_3_0_sw" : "vs_3_0";
    _caps._sm2Used = false;
    _caps._vsVersion = "vs_3_0";
    _caps._psVersion = "ps_3_0";
    _caps._vsVersionLink = "vs_3_0";
    // with 20 we have at least 256 registers
    // this could accommodate 50 or even more bones
    maxFreeConstants = 168; // see also maxFreeConstants in FPShaders.hlsl
    InitShaderDirs();
  }
  else
# endif
  {
#if !_VBS3_SM2
    ErrorMessage("Error: Shader model 2 not supported anymore");
#endif
    //    _vsVersion = DebugVS ? "vs_2_0_sw" : "vs_2_0";
    //    _vsVersionLink = DebugVS ? "vs_2_0_sw" : "vs_2_0";
    _caps._sm2Used = true;
    _caps._vsVersion = "vs_2_0";
    _caps._psVersion = "ps_2_0";
    _caps._vsVersionLink = "vs_2_0";
    // with 20 we have at least 256 registers
    // this could accommodate 50 or even more bones
    maxFreeConstants = 168; // see also maxFreeConstants in FPShaders.hlsl
    InitShaderDirs();
  }
#endif
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
  // Initialize the maximums of instances in the night
  // no longer needed - light space is kept separate
  /*
  {
  _caps._maxNightInstances = (maxFreeConstants - MAX_LIGHTS*6)/4;
  _caps._maxNightInstancesSprite = (maxFreeConstants - MAX_LIGHTS*6)/3;
  saturateMin(_caps._maxNightInstances,LimitInstances);
  saturateMin(_caps._maxNightInstancesSprite,LimitInstancesSprite);
  }
  */
}

/// allow setting D3DXMACRO in one function call - a kind of "external constructor"
static inline void SetMacro(D3DXMACRO &ret, const char *name, const char *value)
{
  ret.Name = name;
  ret.Definition = value;
}

/**
@param close should the list be closed (nulll terminated)?
*/

void EngineDD9::CreateBasicDefines(D3DXMacroList &list, bool close)
{
  /* It is not required to define _XBOX - it's being defined by the shader compiler automatically
  SetMacro(list.Append(),"_PC","1");
  */

#if _VBS3_SM2
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



void EngineDD9::InitVertexShaders(VertexShaderID vsID)
{
  EndHighLevelRecording();
  
#if _ENABLE_REPORT
  DWORD start = ::GetTickCount();
#endif
  //vsID = VSTreePRT;
  // Create a compilation flag
  int flags = 0;
#if _VBS3_SM2
  if (_caps._sm2Used)
  {
    flags |= D3DXSHADER_USE_LEGACY_D3DX9_31_DLL;
  }
#endif

  #if THREADED_SHADER_COMPILER
    GShaderCompiler.StartThreads();
  #endif
  
  // defines shared between all shaders
  typedef AutoArray<D3DXMACRO, MemAllocLocal<D3DXMACRO,16> > D3DXMacroList;

  D3DXMacroList definesBasic;
  CreateBasicDefines(definesBasic,false);

  LogF("Creating special purpose shaders");
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
      CreateShader(VSTerrainFile, defines.Data(), "VSTerrain", _caps._vsVersion, flags, _vertexShaderTerrain[rm]);
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
      CreateShader(VSTerrainGrassFile, defines.Data(), "VSTerrainGrass", _caps._vsVersion, flags, _vertexShaderTerrainGrass[rm]);
    }
  }

  { // other special purpose shaders

    // copy and complete defines
    D3DXMacroList defines = definesBasic;
    SetMacro(defines.Append(),NULL,NULL);

    // Sprite
    if (vsID==NVertexShaderID || vsID==VSSprite)
    {
      CreateShader(VSSpriteFile, defines.Data(), "VSSprite", _caps._vsVersion, flags, _vertexShaderSpecial[VSSSprite]);
    }

    // Water
    if (vsID==NVertexShaderID || vsID==VSWater)
    {
      CreateShader(VSWaterFile, defines.Data(), "VSWater", _caps._vsVersion, flags, _vertexShaderSpecial[VSSWater]);
    }

    // WaterSimple
    if (vsID==NVertexShaderID || vsID==VSSWaterSimple)
    {
      CreateShader(VSWaterFile, defines.Data(), "VSWater", _caps._vsVersion, flags, _vertexShaderSpecial[VSSWaterSimple]);
    }

    // Shore
    if (vsID==NVertexShaderID || vsID==VSShore)
    {
      CreateShader(VSShoreFile, defines.Data(), "VSShore", _caps._vsVersion, flags, _vertexShaderSpecial[VSSShore]);
    }

    // Point
    if (vsID==NVertexShaderID || vsID==VSPoint)
    {
      CreateShader(VSPointFile, defines.Data(), "VSPoint", _caps._vsVersion, flags, _vertexShaderSpecial[VSSPoint]);
    }

    // Shadow volume
    if (vsID==NVertexShaderID || vsID==VSShadowVolume)
    {
      CreateShader(VSShadowVolumeFile, defines.Data(), "VSShadowVolume",          _caps._vsVersion, flags, _vertexShaderShadowVolume[0]);
      CreateShader(VSShadowVolumeFile, defines.Data(), "VSShadowVolumeSkinned",   _caps._vsVersion, flags, _vertexShaderShadowVolume[1]);
      CreateShader(VSShadowVolumeFile, defines.Data(), "VSShadowVolumeInstanced", _caps._vsVersion, flags, _vertexShaderShadowVolume[2]);
    }
    // CalmWater
    if( vsID==NVertexShaderID || vsID==VSCalmWater )
    {
      CreateShader(VSCalmWaterFile, defines.Data(), "VSCalmWater", _caps._vsVersion, flags, _vertexShaderSpecial[VSSCalmWater]);
    }

#if _ARMA3_SIMUL_WEATHER
    // Simul Weather
    if (vsID==NVertexShaderID ||  vsID==VSSimulWeatherClouds)
    {
      // setup fade mode to fragment
      D3DXMacroList defines = definesBasic;
      SetMacro(defines.Append(),"FADE_MODE","1");
      SetMacro(defines.Append(),NULL,NULL);
      CreateShader(VSSimulWeatherFile, defines.Data(), "VSSimulWeatherClouds", _caps._vsVersion, flags, _vertexShaderSpecial[VSSSimulWeatherClouds]);
  }
    if (vsID==NVertexShaderID ||  vsID==VSSimulWeatherCloudsCPU)
    {
      // setup fade mode to CPU
      D3DXMacroList defines = definesBasic;
      SetMacro(defines.Append(),"FADE_MODE","0");
      SetMacro(defines.Append(),NULL,NULL);
      CreateShader(VSSimulWeatherFile, defines.Data(), "VSSimulWeatherClouds", _caps._vsVersion, flags, _vertexShaderSpecial[VSSSimulWeatherCloudsCPU]);
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
    defines.Resize(base+NPRGDef);      
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

      #if THREADED_SHADER_COMPILER
      Future< ComRef<ID3DXBuffer> > poolItem[SHP_UVInput][SHP_MainLight][SHP_RenderingMode][SHP_SkinningInstancing];
      InitVal<bool,false> poolItemDone[SHP_UVInput][SHP_MainLight][SHP_RenderingMode][SHP_SkinningInstancing];
      #endif

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
            SetMacro(defines.Append(),FindEnumName(PRGDEFRenderingMode),rmNumber);
            defines[base+PRGDEFRenderingMode].Name = FindEnumName(PRGDEFRenderingMode);
            defines[base+PRGDEFRenderingMode].Definition = rmNumber;

            for (int si = 0; si < SHP_SkinningInstancing; si++)
            {
              static const char *skin[SHP_SkinningInstancing] = {"false", "true",  "false"};
              static const char *inst[SHP_SkinningInstancing] = {"false", "false", "true"};
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
#if _VBS3_SM2
              if (vs == VSPMulti)
              {
                if (si == 1) // Skinned version
                {
                  if (_caps._sm2Used) continue;
                }
              }
#endif
              // Create the shader
              #if THREADED_SHADER_COMPILER
              CreateShader(hlslSource, defines, "VSShaderPool", _caps._vsVersion, flags, poolItem[uvi][ml][rm][si]);
              poolItemDone[uvi][ml][rm][si] = true;
              #else
              CreateShader(hlslSource, defines.Data(), "VSShaderPool", _caps._vsVersion, flags, _vertexShaderPool[vs][uvi][ml][rm][si]);
              #endif
            }
          }
        }
      }

      #if THREADED_SHADER_COMPILER
      for (int uvi = 0; uvi < SHP_UVInput; uvi++)
      {
        for (int ml = 0; ml < SHP_MainLight; ml++)
        {
          for (int rm = 0; rm < SHP_RenderingMode; rm++)
          {
            for (int si = 0; si < SHP_SkinningInstancing; si++)
            {
              if (!poolItemDone[uvi][ml][rm][si]) continue; // no error when item was not compiled in the first place
              ComRef<ID3DXBuffer> shader = poolItem[uvi][ml][rm][si].GetResult();
              if (shader)
              {
                const DWORD *vsData = (const DWORD *)shader->GetBufferPointer();
                ComRef<IDirect3DVertexShader9> &cvs = _vertexShaderPool[vs][uvi][ml][rm][si];
                HRESULT hr = _d3DDevice->CreateVertexShader(vsData, cvs.Init());

                if (FAILED(hr) || cvs.IsNull())
                {
                  ErrF(
                    "Cannot create vertex shader %s:%d,%d,%d,%d, error %x",
                    cc_cast(FindEnumName(VertexShaderID(vs))),uvi,ml,rm,si,hr
                  );
    }
  }
              else
              {
                ErrorMessage("Shader not compiled");
              }
            }
          }
        }
      }
      #endif
    }
  }
  // Create NonTL shader and declaration
  CreateShader(FPShaders, NULL, "VSNonTL", _caps._vsVersion, flags, _vsNonTL);

	CreateShader(FPShaders, NULL, "VSNonTLHeatHaze", _caps._vsVersion, flags, _vsHeatHaze);


  #if THREADED_SHADER_COMPILER
    GShaderCompiler.StopThreads();
  #endif
#if _ENABLE_REPORT
  DWORD time = ::GetTickCount()-start;
  LogF("InitVertexShaders: %d ms",time);
#endif
}

void EngineDD9::DeinitVertexShaders()
{
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

	_vsHeatHaze.Free();

  Assert(!IsCBRecording());
  const int cb = -1;
  RendState &cbState = CBState(cb);
  // make sure no dangling pointers/references exists
  for (int p=0; p<NPredicationModes; p++) cbState._vertexShaderLast[p].Free();
}

void EngineDD9::ReloadPixelShader(PixelShaderID ps)
{
  _psCache.Clear();
  // we want to reload the one pixel shader
  InitPixelShaders(_sbTechnique,ps,ps);
  // we need to flush all pushbuffers to make sure they do not contain the old pixel shader any more
  FlushPushBuffers();
}

void EngineDD9::ReloadVertexShader(VertexShaderID vs)
{
  VSCache.Clear();
  if (vs==NVertexShaderID)
  {
    CreateShaderDeclarations();
    CreatePostProcessStuff();
  }

  Assert(!IsCBRecording());
  //const int cb = -1;
  // we need to flush our cache as well
  //CBState(cb)._vertexShader.Free();

  InitVertexShaders(vs);
  // we need to flush all pushbuffers to make sure they do not contain the old vertex shader any more
  FlushPushBuffers();
}

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
  PS_NOVARIANTS(SimulWeatherCloudsWithLightningCPU), //// SimulWeather clouds with lightning and CPU distance fading
#else
  PS_DUMMY,
  PS_DUMMY,
  PS_DUMMY,
  PS_DUMMY,
#endif
  PS_VARIANTSMOD(SuperExt),
  PS_VARIANTSMODATOC(SuperAToC),
	PS_NOVARIANTS(NonTLHeatHaze)
};

void EngineDD9::InitPixelShaders(SBTechnique sbTechnique, PixelShaderID from, PixelShaderID to)
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
      if (sbTechnique == SBT_nVidia)
      {
        sourceFileName = PSSR_nVidiaFile;
      }
      else
      {
        sourceFileName = PSSR_DefaultFile;
      }
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
        _pixelShaderCacheId[i][t] = index;
      }
      else
      {
        const DWORD *psData = NULL;
        D3DXCompileShader_Result result;
        if (psName.name != NULL)
        {
          const char *name = psName.name;
          int flags = 0; //D3DXSHADER_PARTIALPRECISION;
          BString<256> psFullName;
          switch (t)
          {
          case PSTShadowCaster:
            strcpy(psFullName,name);
            switch (sbTechnique)
            {
            case SBT_nVidia:
              strcat(psFullName,"_nVidia");
              break;
            //case SBT_nVidiaINTZ:
            //case SBT_Default:
            //case SBT_ATIDF16:
            //case SBT_ATIDF24:
            default:
              strcat(psFullName,"_Default");
              break;
            }
            break;
          case PSTShadowReceiver1:
            strcpy(psFullName,name);
            // some shader may be unused / undefined
            // we cannot add suffix to them
            if (strncmp(name,"PSSR",4)==0)
              switch (sbTechnique)
            {
              case SBT_nVidia:
                strcat(psFullName,"_nVidia");
                break;
              //case SBT_nVidiaINTZ:
              //case SBT_Default:
              //case SBT_ATIDF16:
              //case SBT_ATIDF24:
              default:
                // shadow buffer texcoord calculations require full precision
                flags = 0;
                strcat(psFullName,"_Default");
                break;
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

          HRESULT ok = D3DXCompileShader_Cached(
            hlslSource,
            defines.Data(), &_hlslInclude, psFullName, _caps._psVersion, flags, result
            );
          if (FAILED(ok))
          {
            RptF(
              "Error: %x in D3DXCompileShader while compiling the %s:%d pixel shader (%s)",
              ok, cc_cast(psFullName), t, cc_cast(sourceFileName)
            );
            if (result.errorMsgs.NotNull())
            {
              RptF("%s", (const char*)result.errorMsgs->GetBufferPointer());
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
          }
        }
        if (psData)
        {
          ComRef<IDirect3DPixelShader9> ps;
          HRESULT hr = _d3DDevice->CreatePixelShader(psData, ps.Init());
          if (
            // some pixels shader combinations are never used, we do not mind if we fail creating them
            !(t == PSTShadowReceiver1 && (i==PSTerrainX || i==PSTerrainSNX ))
            && (FAILED(hr) || ps.IsNull())
          )
          {
            RStringB psEnumName = FindEnumName(PixelShaderID(i));
            ErrF("Cannot create pixel shader %s in %s, error %x",cc_cast(psEnumName),cc_cast(sourceFileName),hr);
          }
          else
          {
            int cid = _psCache.Add();
            PSCache &item = _psCache[cid];
            item.name = psName;
            item.ps = ps;
            _pixelShader[i][t] = ps;
            _pixelShaderCacheId[i][t] = cid;
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
}


void EngineDD9::DeinitPixelShaders()
{
  Assert(!IsCBRecording());
  const int cb = -1;
  RendState &cbState = CBState(cb);
  SelectPixelShader(cb, PSNone);
  DoSelectPixelShader(cb);
  //DoSelectPixelShader(PSNone,PSMDay,PSSSpecular);
  for (int i=0; i<NPixelShaderID; i++) for (int t=0; t<NPixelShaderType; t++)
  {
    if (_pixelShader[i][t].NotNull()) _pixelShader[i][t].Free();
  }
  // make sure no dangling pointers/references exists
  for (int p=0; p<NPredicationModes; p++) cbState._pixelShaderLast[p].Free();
}

enum {SISingle=0, SISkin=1, SIInst=2};

#define ENUM_NAME_DEF(x) EnumName(x,#x),

#if _ENABLE_CHEATS

static const EnumName VDINames[]=
{
  ENUM_NAME_DEF(VD_Tex0)
  ENUM_NAME_DEF(VD_Position_Tex0_Color)
  ENUM_NAME_DEF(VD_Position_Normal_Tex0)
  ENUM_NAME_DEF(VD_Position_Normal_Tex0_ST)
  ENUM_NAME_DEF(VD_Position_Normal_Tex0_Tex1)
  ENUM_NAME_DEF(VD_Position_Normal_Tex0_Tex1_ST)
  ENUM_NAME_DEF(VD_Position_Normal_Tex0_ST_Float3)
  ENUM_NAME_DEF(VD_Position_Normal_Tex0_WeightsAndIndices)
  ENUM_NAME_DEF(VD_Position_Normal_Tex0_ST_WeightsAndIndices)
  ENUM_NAME_DEF(VD_Position_Normal_Tex0_Tex1_WeightsAndIndices)
  ENUM_NAME_DEF(VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices)
  ENUM_NAME_DEF(VD_ShadowVolume)
  ENUM_NAME_DEF(VD_ShadowVolumeSkinned)
  ENUM_NAME_DEF(VD_SimulWeatherClouds)
  ENUM_NAME_DEF(VD_SimulWeatherCloudsCPU)
  EnumName()
};

template <>
const EnumName *GetEnumNames(EVertexDecl dummy)
{
  COMPILETIME_COMPARE(VDCount+1,lenof(VDINames));
  return VDINames;
}

static const EnumName VDJNames[]=
{
  ENUM_NAME_DEF(VSDV_Basic)
  ENUM_NAME_DEF(VSDV_Skinning)
  ENUM_NAME_DEF(VSDV_Instancing)
  EnumName()
};

template <>
const EnumName *GetEnumNames(VSDVersion dummy)
{
  COMPILETIME_COMPARE(VSDVCount+1,lenof(VDJNames));
  return VDJNames;
}


static const EnumName RMNames[]=
{
  ENUM_NAME_DEF(RMCommon)
  ENUM_NAME_DEF(RMShadowBuffer)
  ENUM_NAME_DEF(RMZPrime)
  ENUM_NAME_DEF(RMDPrime)
  ENUM_NAME_DEF(RMCrater)
  EnumName()
};

template <>
const EnumName *GetEnumNames(RenderingMode dummy)
{
  return RMNames;
}

#if !defined _X360SHADERGEN

#define D3DRENDERSTATETYPE_LIST(XXX) \
  XXX(D3DRS_ZENABLE) \
  XXX(D3DRS_FILLMODE) \
  XXX(D3DRS_SHADEMODE) \
  XXX(D3DRS_ZWRITEENABLE) \
  XXX(D3DRS_ALPHATESTENABLE) \
  XXX(D3DRS_LASTPIXEL) \
  XXX(D3DRS_SRCBLEND) \
  XXX(D3DRS_DESTBLEND) \
  XXX(D3DRS_CULLMODE) \
  XXX(D3DRS_ZFUNC) \
  XXX(D3DRS_ALPHAREF) \
  XXX(D3DRS_ALPHAFUNC) \
  XXX(D3DRS_DITHERENABLE) \
  XXX(D3DRS_ALPHABLENDENABLE) \
  XXX(D3DRS_FOGENABLE) \
  XXX(D3DRS_SPECULARENABLE) \
  XXX(D3DRS_FOGCOLOR) \
  XXX(D3DRS_FOGTABLEMODE) \
  XXX(D3DRS_FOGSTART) \
  XXX(D3DRS_FOGEND) \
  XXX(D3DRS_FOGDENSITY) \
  XXX(D3DRS_RANGEFOGENABLE) \
  XXX(D3DRS_STENCILENABLE) \
  XXX(D3DRS_STENCILFAIL) \
  XXX(D3DRS_STENCILZFAIL) \
  XXX(D3DRS_STENCILPASS) \
  XXX(D3DRS_STENCILFUNC) \
  XXX(D3DRS_STENCILREF) \
  XXX(D3DRS_STENCILMASK) \
  XXX(D3DRS_STENCILWRITEMASK) \
  XXX(D3DRS_TEXTUREFACTOR) \
  XXX(D3DRS_WRAP0) \
  XXX(D3DRS_WRAP1) \
  XXX(D3DRS_WRAP2) \
  XXX(D3DRS_WRAP3) \
  XXX(D3DRS_WRAP4) \
  XXX(D3DRS_WRAP5) \
  XXX(D3DRS_WRAP6) \
  XXX(D3DRS_WRAP7) \
  XXX(D3DRS_CLIPPING) \
  XXX(D3DRS_LIGHTING) \
  XXX(D3DRS_AMBIENT) \
  XXX(D3DRS_FOGVERTEXMODE) \
  XXX(D3DRS_COLORVERTEX) \
  XXX(D3DRS_LOCALVIEWER) \
  XXX(D3DRS_NORMALIZENORMALS) \
  XXX(D3DRS_DIFFUSEMATERIALSOURCE) \
  XXX(D3DRS_SPECULARMATERIALSOURCE) \
  XXX(D3DRS_AMBIENTMATERIALSOURCE) \
  XXX(D3DRS_EMISSIVEMATERIALSOURCE) \
  XXX(D3DRS_VERTEXBLEND) \
  XXX(D3DRS_CLIPPLANEENABLE) \
  XXX(D3DRS_POINTSIZE) \
  XXX(D3DRS_POINTSIZE_MIN) \
  XXX(D3DRS_POINTSPRITEENABLE) \
  XXX(D3DRS_POINTSCALEENABLE) \
  XXX(D3DRS_POINTSCALE_A) \
  XXX(D3DRS_POINTSCALE_B) \
  XXX(D3DRS_POINTSCALE_C) \
  XXX(D3DRS_MULTISAMPLEANTIALIAS) \
  XXX(D3DRS_MULTISAMPLEMASK) \
  XXX(D3DRS_PATCHEDGESTYLE) \
  XXX(D3DRS_DEBUGMONITORTOKEN) \
  XXX(D3DRS_POINTSIZE_MAX) \
  XXX(D3DRS_INDEXEDVERTEXBLENDENABLE) \
  XXX(D3DRS_COLORWRITEENABLE) \
  XXX(D3DRS_TWEENFACTOR) \
  XXX(D3DRS_BLENDOP) \
  XXX(D3DRS_POSITIONDEGREE) \
  XXX(D3DRS_NORMALDEGREE) \
  XXX(D3DRS_SCISSORTESTENABLE) \
  XXX(D3DRS_SLOPESCALEDEPTHBIAS) \
  XXX(D3DRS_ANTIALIASEDLINEENABLE) \
  XXX(D3DRS_MINTESSELLATIONLEVEL) \
  XXX(D3DRS_MAXTESSELLATIONLEVEL) \
  XXX(D3DRS_ADAPTIVETESS_X) \
  XXX(D3DRS_ADAPTIVETESS_Y) \
  XXX(D3DRS_ADAPTIVETESS_Z) \
  XXX(D3DRS_ADAPTIVETESS_W) \
  XXX(D3DRS_ENABLEADAPTIVETESSELLATION) \
  XXX(D3DRS_TWOSIDEDSTENCILMODE) \
  XXX(D3DRS_CCW_STENCILFAIL) \
  XXX(D3DRS_CCW_STENCILZFAIL) \
  XXX(D3DRS_CCW_STENCILPASS) \
  XXX(D3DRS_CCW_STENCILFUNC) \
  XXX(D3DRS_COLORWRITEENABLE1) \
  XXX(D3DRS_COLORWRITEENABLE2) \
  XXX(D3DRS_COLORWRITEENABLE3) \
  XXX(D3DRS_BLENDFACTOR) \
  XXX(D3DRS_SRGBWRITEENABLE) \
  XXX(D3DRS_DEPTHBIAS) \
  XXX(D3DRS_WRAP8) \
  XXX(D3DRS_WRAP9) \
  XXX(D3DRS_WRAP10) \
  XXX(D3DRS_WRAP11) \
  XXX(D3DRS_WRAP12) \
  XXX(D3DRS_WRAP13) \
  XXX(D3DRS_WRAP14) \
  XXX(D3DRS_WRAP15) \
  XXX(D3DRS_SEPARATEALPHABLENDENABLE) \
  XXX(D3DRS_SRCBLENDALPHA) \
  XXX(D3DRS_DESTBLENDALPHA) \
  XXX(D3DRS_BLENDOPALPHA) \


static const EnumNameCStr D3DRSNames[]=
{
  D3DRENDERSTATETYPE_LIST(ENUM_NAME_DEF_CS)
  EnumNameCStr()
};

template <>
const EnumNameCStr *GetEnumNamesCStr(D3DRENDERSTATETYPE dummy) {return D3DRSNames;}


#define D3DSAMPLERSTATETYPE_LIST(XXX) \
    XXX(D3DSAMP_ADDRESSU) \
    XXX(D3DSAMP_ADDRESSV) \
    XXX(D3DSAMP_ADDRESSW) \
    XXX(D3DSAMP_BORDERCOLOR) \
    XXX(D3DSAMP_MAGFILTER) \
    XXX(D3DSAMP_MINFILTER) \
    XXX(D3DSAMP_MIPFILTER) \
    XXX(D3DSAMP_MIPMAPLODBIAS) \
    XXX(D3DSAMP_MAXMIPLEVEL) \
    XXX(D3DSAMP_MAXANISOTROPY) \
    XXX(D3DSAMP_SRGBTEXTURE) \
    XXX(D3DSAMP_ELEMENTINDEX) \
    XXX(D3DSAMP_DMAPOFFSET) \

static const EnumNameCStr D3DSSNames[]=
{
  D3DSAMPLERSTATETYPE_LIST(ENUM_NAME_DEF_CS)
  EnumNameCStr()
};

template <>
const EnumNameCStr *GetEnumNamesCStr(D3DSAMPLERSTATETYPE dummy) {return D3DSSNames;}

#endif

DEFINE_ENUM(VertexShaderSpecialID, VSS, VSS_ENUM)

RString StringNull="NULL";

const char *GetSIName(int si)
{
  static const char *names[]={"SISingle","SISkin","SIInst"};
  return names[si];
}

/** Output us formatted so that it can serve directly as an input into BIND_SHADER macros */

RString EngineDD9::GetVSName(IDirect3DVertexShader9 *vs)
{
  //_vertexShaderPool[SHP_VertexShaderID][SHP_UVInput][SHP_MainLight][SHP_RenderingMode][SHP_SkinningInstancing];
  for (int id=0; id<SHP_VertexShaderID; id++)
  for (int uv=0; uv<SHP_UVInput; uv++)
  for (int ml=0; ml<SHP_MainLight; ml++)
  for (int rm=0; rm<SHP_RenderingMode; rm++)
  for (int si=0; si<SHP_SkinningInstancing; si++)
  {
    if (_vertexShaderPool[id][uv][ml][rm][si]==vs)
    {
      const RString &idName = FindEnumName(VertexShaderPoolID(id));
      const RString &rmName = FindEnumName(RenderingMode(rm));
      return Format("POOL,[VSP%s][%d][%d][%s][%s]",cc_cast(idName),uv,ml,cc_cast(rmName),GetSIName(si));
    }
  }
  //_vertexShaderShadowVolume[SHP_SkinningInstancing];
  for (int si=0; si<SHP_SkinningInstancing; si++)
  {
    if (_vertexShaderShadowVolume[si]==vs)
    {
      return Format("SHADOWVOL,[%s]",GetSIName(si));
    }
  }
  //_vertexShaderSpecial[NVertexShaderSpecialID];
  for (int sp=0; sp<NVertexShaderSpecialID; sp++)
  {
    if (_vertexShaderSpecial[sp]==vs)
    {
      return Format("SPECIAL,[VSS%s]",cc_cast(FindEnumName(VertexShaderSpecialID(sp))));
    }
  }
  //_vertexShaderTerrain[SHP_RenderingMode];
  for (int rm=0; rm<SHP_RenderingMode; rm++)
  {
    if (_vertexShaderTerrain[rm]==vs)
    {
      // TODO: display user friendly
      return Format("TERRAIN,[%s]",cc_cast(FindEnumName(RenderingMode(rm))));
    }
  }
  //_vertexShaderTerrainGrass[SHP_RenderingMode];
  for (int rm=0; rm<SHP_RenderingMode; rm++)
  {
    if (_vertexShaderTerrainGrass[rm]==vs)
    {
      // TODO: display user friendly
      return Format("TERRAINGRASS,[%s]",cc_cast(FindEnumName(RenderingMode(rm))));
    }
  }
  if (vs==_vsNonTL)
  {
    return "NONTL,[VSNonTL]";
  }
	if (vs==_vsHeatHaze)
	{
		return "HeatHaze,[VSNonTLHeatHaze]";
	}

  if (vs==NULL) return StringNull;
  return RString("");
}
RString EngineDD9::GetVDName(IDirect3DVertexDeclaration9 *vd)
{
  for (int i=0; i<VDCount; i++) for (int j=0; j<VSDVCount; j++)
  {
    if (_vertexDeclD[i][j]==vd)
    {
      const RStringB &iName = FindEnumName(EVertexDecl(i));
      const RStringB &jName = FindEnumName(VSDVersion(j));
      return RString("[")+iName+RString("][")+jName+RString("]");
    }
  }
  if (vd==NULL) return StringNull;
  if (vd==_vdNonTL) return "[NonTL]";
	if (vd==_vdHeatHaze) return "[HeatHaze]";
  return RString();
}
RString EngineDD9::GetPSName(IDirect3DPixelShader9 *ps)
{
  for (int pst=0; pst<NPixelShaderType; pst++)
  for (int psi=0; psi<NPixelShaderID; psi++)
  {
    if (_pixelShader[psi][pst]==ps)
    {
      return PSNames[psi][pst].name;
    }
  }
  if (ps==NULL) return StringNull;
  return RString();
}
#endif


static int GetLayerUsageMask(const TexMaterial *mat)
{
  if (!mat) return 0;
  int layerMask = mat->GetCustomInt();
  // TODO: verify stages conform to layer mask, or even better, gather layer mask from stages
  return layerMask;
}

void EngineDD9::DoSelectPixelShader(int cb, const TexMaterial *mat, int cratersCount)
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
      IDirect3DTexture9 * tex = GetDepthAsTexture();
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
    }


    if ((pixelShaderSel == PSWater) || (pixelShaderSel == PSShore) || (pixelShaderSel == PSShoreFoam))
    {
      SetPixelShaderConstantWater(cb);
    }
    else if (pixelShaderSel == PSWaterSimple)
    {
      SetPixelShaderConstantWaterSimple(cb);
    }
    else if (pixelShaderSel == PSGlass)
    {
      // Environmental map color modificator
      LightSun *sun = GScene->MainLight();
      Color lighting = sun->SimulateDiffuse(0.5) * GetAccomodateEye() * _modColor * EnvMapScale;
      SetPixelShaderConstantF(cb,PSC_GlassEnvColor, (float*)&lighting, 1);

      // Material specular color
      SetPixelShaderConstantF(cb,PSC_GlassMatSpecular, (float*)&cbState._matSpecular, 1);
    }
    else if (pixelShaderSel == PSSuper)
    {
      SetPixelShaderConstantSuper(cb, cbState._matSpecular);
			SetPixelShaderNightEmissiveConst(cb, cbState._matEmmisive, false);
    }
#if _HELISIM_FAKE_NIGHT_LIGHTS
    else if (pixelShaderSel == PSSuperExt || pixelShaderSel == PSSuperAToC)
    {
      SetPixelShaderConstantSuper(cb,cbState._matSpecular);
      SetPixelShaderNightEmissiveConst(cb, cbState._matEmmisive, true);
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
      || pixelShaderSel == PSTreeAdvAToC || pixelShaderSel == PSTreeAdvSimpleAToC)
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
  }
  else
  {
    const ComRef<IDirect3DPixelShader9> empty;
    SetPixelShader(cb,empty);
  }
}

void EngineDD9::EnableNightEye(float night)
{
  if (_nightVision) night = 0;
  // select normal or night pixel shader
  if (fabs(_nightEye-night)<0.01f) return;
  FlushQueues();
  _nightEye = night;
}

void EngineDD9::UseShadowMapSSSM(int cb)
{
  // Finish in case of thermal vision (as this function would overwrite the TC table which is bound with TEXID_SHADOWMAP)
  if (_thermalVision) return;

  // Set the SSSM as shadow map
#ifdef SSSMBLUR
  SetTexture(cb,TEXID_SHADOWMAP, _sssmB);
#else
  SetTexture(cb,TEXID_SHADOWMAP, _sssm);
#endif

  // We want to use the point sampling
  D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MINFILTER, D3DTEXF_POINT);
  D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
  D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
  D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_SRGBTEXTURE, FALSE);
}

void EngineDD9::UseShadowMapSB(int cb)
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
  switch (_sbTechnique)
  {
  case SBT_Default:
  case SBT_None:
    {
      // Set texture and sampling parameters
      // we cannot assert - this code is used even for primitives which use no shadowing at all
      //DoAssert(_renderTargetSB.NotNull());
      SetTexture(cb,TEXID_SHADOWMAP, _renderTargetSB);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MINFILTER, D3DTEXF_POINT);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_SRGBTEXTURE, FALSE);
    }
    break;
  case SBT_nVidia:
    {
      // Set texture and sampling parameters
      //DoAssert(_renderTargetDepthBufferSB.NotNull());
      SetTexture(cb,TEXID_SHADOWMAP, _renderTargetDepthBufferSB);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_SRGBTEXTURE, FALSE);
    }
    break;
  case SBT_nVidiaINTZ:
  case SBT_ATIDF16:
  case SBT_ATIDF24:
    {
      // Set texture and sampling parameters
      //DoAssert(_renderTargetDepthBufferSB.NotNull());
      SetTexture(cb,TEXID_SHADOWMAP, _renderTargetDepthBufferSB);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MINFILTER, D3DTEXF_POINT);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
      D3DSetSamplerState(cb, TEXID_SHADOWMAP, D3DSAMP_SRGBTEXTURE, FALSE);
    }
    break;
  default:
    Fail("Error: Unknown SB technique");
  }
}

void EngineDD9::EnableSBShadows(int quality)
{
  _sbQualityWanted = quality;
}

void EngineDD9::EnableSBShadowsCommit()
{
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
    CreateSBResources();
  }
  else
  {
    DestroySBResources();
  }
  _textBank->ResetMaxTextureQuality(false);
}

bool EngineDD9::IsSBEnabled() const
{
  return _renderTargetDepthBufferSurfaceSB.NotNull();
}

bool EngineDD9::SBTR_DefaultCheck(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat) const
{
  if (_direct3D->CheckDeviceFormat(
    adapter, devType, backBufferFormat,D3DUSAGE_DEPTHSTENCIL,D3DRTYPE_SURFACE,D3DFMT_D24S8
    ) == D3D_OK)
  {
    if (_direct3D->CheckDeviceFormat(
      adapter, devType, backBufferFormat,D3DUSAGE_RENDERTARGET,D3DRTYPE_TEXTURE,D3DFMT_G16R16F
      ) == D3D_OK)
    {
      if (_direct3D->CheckDepthStencilMatch(
        adapter, devType, backBufferFormat,D3DFMT_G16R16F,D3DFMT_D24S8
        ) == D3D_OK)
      {
        return true;
      }
    }
  }
  return false;
}

void EngineDD9::SBTR_DefaultCreate(int sbSize)
{
  HRESULT err;
  bool creationFailed = true;

  if ((err = CreateTextureRT(sbSize, sbSize, 1, _smFormat, D3DPOOL_DEFAULT, _renderTargetSB, _renderTargetSurfaceSB, false)) == D3D_OK)
  {
    if ((err = CreateDepthStencilSurface(sbSize, sbSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, _renderTargetDepthBufferSurfaceSB, _renderTargetSurfaceSB, NULL)) == D3D_OK)
    {
      creationFailed = false;
    }
    else
    {
      DDError9("Error: Shadow buffer depth stencil surface creation failed.", err);
    }
  }
  else
  {
    DDError9("Error: Shadow buffer render target texture creation failed - SB disabled", err);
  }

  if (creationFailed)
  {
    DestroySBResources();
    return;
  }
}


#define nVidiaINTZ	((D3DFORMAT)MAKEFOURCC('I','N','T','Z'))
#define D3DFMT_NULL ((D3DFORMAT)MAKEFOURCC('N', 'U', 'L', 'L'))
#define ATIDF16			((D3DFORMAT)MAKEFOURCC('D', 'F', '1', '6'))
#define ATIDF24			((D3DFORMAT)MAKEFOURCC('D', 'F', '2', '4'))


bool EngineDD9::SBTR_nVidiaCheck(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat, D3DFORMAT sbFormat) const
{
  if (_direct3D->CheckDeviceFormat(
    adapter, devType, backBufferFormat,
    D3DUSAGE_DEPTHSTENCIL,
    D3DRTYPE_TEXTURE,
    sbFormat) == D3D_OK)
  {
    if (_direct3D->CheckDeviceFormat(
      adapter, devType, backBufferFormat,
      D3DUSAGE_RENDERTARGET,
      D3DRTYPE_TEXTURE,
      D3DFMT_R5G6B5) == D3D_OK)
    {
      if (_direct3D->CheckDepthStencilMatch(
        adapter, devType, backBufferFormat,
        D3DFMT_R5G6B5,
        sbFormat) == D3D_OK)
      {
        return true;
      }
    }
  }
  return false;
}

/*!
\patch 5089 Date 11/21/2006 by Flyman
- Fixed: NULL RT texture can be created now if possible as a shadow buffer resource - it will save some video RAM when using nVidia SB technique
- Fixed: 1x1 texture is created now as a shadow buffer resource, when using retail version of the DX - it will save some video RAM when using nVidia or ATI SB techniques
*/
void EngineDD9::SBTR_nVidiaCreate(D3DFORMAT sbFormat, int sbSize)
{
  // If debug runtime is detected, we must use the proper primary target size, otherwise the
  // Clear method will fail. In case of retail version we can use size 1x1 and save the video memory
  // See the label SBSVP
  int primaryTextureSize;
  if (StrictAPI || _d3DDevice->CreateQuery(D3DQUERYTYPE_RESOURCEMANAGER, NULL) == D3D_OK)
  {
    primaryTextureSize = sbSize;
  }
  else
  {
    primaryTextureSize = 1;
  }

  // Try to create the NULL texture, if not possible then use the standard one
  HRESULT err;

  bool creationFailed = true;

	if ((err = CreateTextureRT(primaryTextureSize, primaryTextureSize, 1, D3DFMT_NULL, D3DPOOL_DEFAULT, _renderTargetSB, false)) == D3D_OK)
  {
    LogF("Info: nVidia HW supports NULL RT");
  }
  else if ((err = CreateTextureRT(primaryTextureSize, primaryTextureSize, 1, D3DFMT_R5G6B5, D3DPOOL_DEFAULT, _renderTargetSB, false)) == D3D_OK)
  {
    LogF("Info: nVidia HW does not support NULL RT");
  }
  else
  {
    DDError9("Error: Shadow buffer render target texture creation failed - SB disabled", err);
  }

  // If we succeeded creating the texture, continue creating the depth-stencil buffer
  if (_renderTargetSB.NotNull())
  {
    if ((err = _renderTargetSB->GetSurfaceLevel(0, _renderTargetSurfaceSB.Init())) == D3D_OK)
    {
      if ((err = _d3DDevice->CreateTexture(sbSize, sbSize, 1, D3DUSAGE_DEPTHSTENCIL, sbFormat, D3DPOOL_DEFAULT, _renderTargetDepthBufferSB.Init(), NULL)) == D3D_OK)
      {
        if ((err = _renderTargetDepthBufferSB->GetSurfaceLevel(0, _renderTargetDepthBufferSurfaceSB.Init())) == D3D_OK)
        {
          creationFailed = false;
        }
        else
        {
          DDError9("Error: Shadow buffer depth stencil surface creation failed.", err);
        }
      }
      else
      {
        DDError9("Error: Shadow buffer depth stencil texture creation failed.", err);
      }
    }
    else
    {
      DDError9("Error: Shadow buffer render target surface creation failed.", err);
    }
  }

  if (creationFailed)
  {
    DestroySBResources();
    return;
  }
}

bool EngineDD9::SBTR_ATICheck(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat,  D3DFORMAT sbFormat) const
{
  if (_direct3D->CheckDeviceFormat(
    adapter, devType, backBufferFormat,
    D3DUSAGE_DEPTHSTENCIL,
    D3DRTYPE_SURFACE,
    sbFormat) == D3D_OK)
  {
    if (_direct3D->CheckDeviceFormat(
      adapter, devType, backBufferFormat,
      D3DUSAGE_RENDERTARGET,
      D3DRTYPE_TEXTURE,
      D3DFMT_R5G6B5) == D3D_OK)
    {
      if (_direct3D->CheckDepthStencilMatch(
        adapter, devType, backBufferFormat,
        D3DFMT_R5G6B5,
        sbFormat) == D3D_OK)
      {
        return true;
      }
    }
  }
  return false;
}


void EngineDD9::SBTR_ATICreate(D3DFORMAT sbFormat, int sbSize)
{
  // If debug runtime is detected, we must use the proper primary target size, otherwise the
  // Clear method will fail. In case of retail version we can use size 1x1 and save the video memory
  // See the label SBSVP
  int primaryTextureSize;
  if (StrictAPI || _d3DDevice->CreateQuery(D3DQUERYTYPE_RESOURCEMANAGER, NULL) == D3D_OK)
  {
    primaryTextureSize = sbSize;
  }
  else
  {
    primaryTextureSize = 1;
  }

  HRESULT err;

  bool creationFailed = true;

  if ((err = CreateTextureRT(primaryTextureSize, primaryTextureSize, 1, D3DFMT_R5G6B5, D3DPOOL_DEFAULT, _renderTargetSB, false)) == D3D_OK)
  {
    if ((err = _renderTargetSB->GetSurfaceLevel(0, _renderTargetSurfaceSB.Init())) == D3D_OK)
    {
      if ((err = CreateTextureDS(sbSize, sbSize, 1, sbFormat, D3DPOOL_DEFAULT, _renderTargetDepthBufferSB, _renderTargetSurfaceSB)) == D3D_OK)
      {
        if ((err = _renderTargetDepthBufferSB->GetSurfaceLevel(0, _renderTargetDepthBufferSurfaceSB.Init())) == D3D_OK)
        {
          creationFailed = false;
        }
        else
        {
          DDError9("Error: Shadow buffer depth stencil surface creation failed.", err);
        }
      }
      else
      {
        DDError9("Error: Shadow buffer depth stencil texture creation failed.", err);
      }
    }
    else
    {
      DDError9("Error: Shadow buffer render target surface creation failed.", err);
    }
  }
  else
  {
    DDError9("Error: Shadow buffer render target texture creation failed - SB disabled", err);
  }

  if (creationFailed)
  {
    DestroySBResources();
    return;
  }
}


void EngineDD9::SBTR_nVidiaCreate(int sbSize)
{
  SBTR_nVidiaCreate(D3DFMT_D24S8, sbSize);
}


void EngineDD9::SBTR_nVidiaINTZCreate(int sbSize)
{
  SBTR_nVidiaCreate(nVidiaINTZ, sbSize);
}

bool EngineDD9::SBTR_nVidiaCheck(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat) const
{
  return SBTR_nVidiaCheck(adapter,devType,backBufferFormat,D3DFMT_D24S8);
}
bool EngineDD9::SBTR_nVidiaINTZCheck(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat) const
{
  return SBTR_nVidiaCheck(adapter,devType,backBufferFormat,nVidiaINTZ);
}


bool EngineDD9::SBTR_ATID16Check(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat) const
{
  return SBTR_ATICheck(adapter,devType,backBufferFormat,ATIDF16);
}

bool EngineDD9::SBTR_ATID24Check(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat) const
{
  return SBTR_ATICheck(adapter,devType,backBufferFormat,ATIDF24);
}

void EngineDD9::SBTR_ATID16Create(int sbSize)
{
  SBTR_ATICreate(ATIDF16, sbSize);
}
void EngineDD9::SBTR_ATID24Create(int sbSize)
{
  SBTR_ATICreate(ATIDF24, sbSize);
}

//-----------------------------------------------------------------------------------------
#ifdef TEST_RAIN_MAP


#ifdef NEW_SB_INIT

#define D3DFMT_INTZ  ((D3DFORMAT) MAKEFOURCC('I', 'N', 'T', 'Z'))
#define D3DFMT_NULL  ((D3DFORMAT) MAKEFOURCC('N', 'U', 'L', 'L'))
#define D3DFMT_DF24	 ((D3DFORMAT) MAKEFOURCC('D', 'F', '2', '4'))
#define D3DFMT_DF16	 ((D3DFORMAT) MAKEFOURCC('D', 'F', '1', '6'))

SBFormat SBTR_InvalidFormat = {D3DFMT_UNKNOWN,	D3DFMT_UNKNOWN,	SBT_None};
SBFormat SBTR_Formats[] = 
{
	{D3DFMT_INTZ,			D3DFMT_NULL,		SBT_nVidiaINTZ},	//NULL and INTZ format has ATI too
	{D3DFMT_INTZ,			D3DFMT_R5G6B5,	SBT_nVidiaINTZ},
	{D3DFMT_DF24,			D3DFMT_R5G6B5,	SBT_ATIDF24},
	{D3DFMT_DF16,			D3DFMT_R5G6B5,	SBT_ATIDF16},
	{D3DFMT_D24S8,		D3DFMT_R32F,		SBT_Default},
	{D3DFMT_D24S8,		D3DFMT_R16F,		SBT_Default},
	SBTR_InvalidFormat,
};


//----------------------------------------------------------------------------
bool EngineDD9::SBTR_CheckFormat2(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat, D3DFORMAT depthFormat, D3DFORMAT rtFormat) const
{
	if (_direct3D->CheckDeviceFormat(adapter, devType, backBufferFormat, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, depthFormat) != D3D_OK)
		return false;

	if (_direct3D->CheckDeviceFormat(adapter, devType, backBufferFormat, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, rtFormat) != D3D_OK)
		return false;

	if (_direct3D->CheckDepthStencilMatch(adapter, devType, backBufferFormat, rtFormat, depthFormat) != D3D_OK)
		return false;

	return true;
}

//----------------------------------------------------------------------------
SBFormat EngineDD9::SBTR_FindFormat2(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat)
{
	int i = 0;
	for (; i < sizeof(SBTR_Formats)/sizeof(SBFormat) - 1; i++)
	{
		if (SBTR_CheckFormat2(adapter, devType, backBufferFormat, SBTR_Formats[i]._depthFormat, SBTR_Formats[i]._rtFormat))
			break;
	}

	return SBTR_Formats[i];
}


//----------------------------------------------------------------------------
void EngineDD9::SBBuffers::DestroyResources()
{
	_rtColorBuffer.Free();
	_rtColorBufferSurface.Free();
	_rtDepthBuffer.Free();
	_rtDepthBufferSurface.Free();
}


//----------------------------------------------------------------------------
void EngineDD9::SBTR_CreateShadowMap2(const SBFormat& format, int sbSize, SBBuffers& buffers)
{
	//full color buffer size when using defulat SB technique or to avoid fail in Clear method when running debug runtime
	int colSize = (format._sbTechnique == SBT_Default || StrictAPI || _d3DDevice->CreateQuery(D3DQUERYTYPE_RESOURCEMANAGER, NULL) == D3D_OK) ? sbSize : 1;

	HRESULT err;
	bool creationFailed = true;

	if ((err = CreateTextureRT(colSize, colSize, 1, format._rtFormat, D3DPOOL_DEFAULT, buffers._rtColorBuffer, buffers._rtColorBufferSurface, false)) == D3D_OK)
	{
		//CreateTextureDS funguje jinak nez CreateTextureRT, surface musi byt RT surface a uvnitr se oba zinicializujou -> prepsat
		if ((err = CreateTextureDS(sbSize, sbSize, 1, format._depthFormat, D3DPOOL_DEFAULT, buffers._rtDepthBuffer, /*buffers._rtDepthBufferSurface*/)) == D3D_OK)
		{
			creationFailed = false;
		}
		else
		{
			DDError9("Error: Shadow buffer depth stencil surface creation failed.", err);
		}
	}
	else
	{
		DDError9("Error: Shadow buffer render target texture creation failed - SB disabled", err);
	}

	if (creationFailed)
	{
		buffers.DestroyResources();
		_sbFormatDesc = SBTR_InvalidFormat;
		_sbTechnique = _sbFormatDesc._sbTechnique;
		return;
	}	
}


#endif


bool EngineDD9::IsRainMapEnabled() const
{
	return _rtRainMapDepthSurface.NotNull();
}


void EngineDD9::CreateRainMapResources()
{
	//predelat celou inicializaci shadow map na 
	//jednu univerzalni funkci a tohle smazat
	//
	// struct ShadowMapBuffers {} sbBuffers;
	// neco jako SBTR_Create(format[ATIDF24, ATIDF16,nVidiaINTZ,D3DFMT_D24S8, DEFAULT], size, sbBuffers);

	int sbSize = RAIN_MAP_SB_SIZE;
	_oldLRSP[ShadowBufferCascadeLayers] = MIdentity;

	D3DFORMAT depthFormat = D3DFMT_UNKNOWN;
	switch (_sbTechnique)
	{	
		case SBT_Default:			depthFormat = D3DFMT_D24S8;	break; //why S8 not X8 for shadow map?
		case SBT_nVidia:			
		case SBT_nVidiaINTZ:	depthFormat = nVidiaINTZ; break;
		case SBT_ATIDF16:			depthFormat = ATIDF16; break;
		case SBT_ATIDF24:			depthFormat = ATIDF24; break;
	};

	if (depthFormat == D3DFMT_UNKNOWN)
		return;


	//find color format, will not be used for depth render but for height diletation pass
	D3DFORMAT colorFormats[] = 
	{
		D3DFMT_R16F,
		D3DFMT_G16R16F,
		D3DFMT_R32F
	};

	HRESULT err = NOERROR;
	bool creationFailed = true;

	for (int i = 0; i < sizeof(colorFormats)/sizeof(D3DFORMAT); i++)	
	{
		if ((err = CreateTextureRT(sbSize, sbSize, 1, colorFormats[i], D3DPOOL_DEFAULT, _rtRainMap, _rtRainMapSurface, false)) == D3D_OK)
		{
			creationFailed = false;
			break;
		}
	}

	if (creationFailed)
	{
		DDError9("Error: RainMap color buffer creation failed.", err);
		return;
	}

	if ((err = CreateTextureDS(sbSize, sbSize, 1, depthFormat, D3DPOOL_DEFAULT, _rtRainMapDepth, _rtRainMapSurface)) == D3D_OK)
	{
		if ((err = _rtRainMapDepth->GetSurfaceLevel(0, _rtRainMapDepthSurface.Init())) == D3D_OK)
		{
			creationFailed = false;
		}
	}

	if (creationFailed)
	{
		DDError9("Error: RainMap depth buffer creation failed.", err);
		DestroyRainMapResources();
	}
}
#endif

//-----------------------------------------------------------------------------------------
void EngineDD9::CreateSBResources()
{
  for (int i = 0; i < ShadowBufferCascadeLayers; i++)
  {
    _oldLRSP[i] = MIdentity;
  }

	_sbSize = BasicShadowBufferSize;
	if (_sbQuality > 1)
	{
		_sbSize = intMin(_caps._maxTextureSize, _sbSize << (_sbQuality - 1));
	}

	#ifdef NEW_SB_INIT
		SBTR_CreateShadowMap2(_sbFormatDesc, _sbSize);
		//rain map
	#endif
	
  switch (_sbTechnique)
  {
  case SBT_Default:
    SBTR_DefaultCreate(_sbSize);
    break;
  case SBT_nVidia:
    SBTR_nVidiaCreate(_sbSize);
    break;
  case SBT_nVidiaINTZ:
    SBTR_nVidiaINTZCreate(_sbSize);
    break;
  case SBT_ATIDF16:
    SBTR_ATID16Create(_sbSize);
    break;
  case SBT_ATIDF24:
    SBTR_ATID24Create(_sbSize);
    break;
  default:
    break;
  }
}

void EngineDD9::DestroySBResources()
{
  _renderTargetDepthBufferSurfaceSB.Free();
  _renderTargetDepthBufferSB.Free();
  _renderTargetSurfaceSB.Free();
  _renderTargetSB.Free();
}

#ifdef TEST_RAIN_MAP
void EngineDD9::DestroyRainMapResources()
{
	_rtRainMap.Free();
	_rtRainMapSurface.Free();
	_rtRainMapDepth.Free();
	_rtRainMapDepthSurface.Free();
}
#endif


void EngineDD9::CreateResources()
{
  // Create the screen space shadow maps
  HRESULT err;

  D3DFORMAT sssmFormat = D3DFMT_X8R8G8B8;
  {
    // SSSM - never multisampled on PC
    if ((err = CreateTextureRT(_wRT, _hRT, 1, sssmFormat, D3DPOOL_DEFAULT, _sssm, false)) == D3D_OK)
    {
      if ((err = _sssm->GetSurfaceLevel(0, _sssmSurface.Init())) != D3D_OK)
      {
        DDError9("Error: _sssm surface creation failed", err);
      }
    }
    else
    {
      DDError9("Error: _sssm texture creation failed", err);
    }
  }
#ifdef SSSMBLUR
  // SSSM blurred
  if ((err = CreateTextureRT(_wRT, _hRT, 1, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, _sssmB, false)) == D3D_OK)
  {
    if ((err = _sssmB->GetSurfaceLevel(0, _sssmBSurface.Init())) == D3D_OK)
    {
    }
    else
    {
      DDError9("Error: _sssmB surface creation failed", err);
    }
  }
  else
  {
    DDError9("Error: _sssmB texture creation failed", err);
  }
#endif
}

void EngineDD9::DestroyResources()
{
#ifdef SSSMBLUR
  _sssmBSurface.Free();
  _sssmB.Free();
#endif
  _sssmSurface.Free();
  _sssm.Free();

	if (_pp._godRays.NotNull())
	{
		_pp._godRays->UnInit();
		_pp._godRays.Free();
	}

	if (_pp._rain.NotNull())
	{
		_pp._rain->UnInit();
		_pp._rain.Free();
	}

	if (_pp._filmGrain.NotNull())
	{
		_pp._filmGrain->UnInit();
		_pp._filmGrain.Free();
	}

	if (_pp._ssao.NotNull())
	{
		_pp._ssao->UnInit();
		_pp._ssao.Free();
	}
	_ssaoSurface.Free();
}

void EngineDD9::CreateTIResources()
{
  HRESULT err;
  if ((err = _d3DDevice->CreateTexture(16, 16, 1, 0, D3DFMT_R32F, D3DPOOL_DEFAULT, _tableTc.Init(), NULL)) == D3D_OK)
  {    
    if ((err = _d3DDevice->CreateTexture(16, 16, 1, 0, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, _tableTcSystem.Init(), NULL)) == D3D_OK)
  {
    if ((err = _d3DDevice->CreateTexture(TIConversionDimensionW, TIConversionDimensionH, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, _tiConversion.Init(), NULL)) == D3D_OK)
    {
      // In case the conversion texture name was already specified, load it
      if (!_tiConversionName.IsEmpty())
      {
        LoadTIConversionTexture(_tiConversionName);
      }
      return;
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
}

void EngineDD9::DestroyTIResources()
{
  _tiConversion.Free();
  _tableTcSystem.Free();
  _tableTc.Free();
  _temperatureTable.Resize(0);
}

const char *SurfaceFormatNameDD9(int format)
{
# define FN1(x) case D3DFMT_##x: return #x
# define FN(x) case D3DFMT_##x: return #x
  switch (format)
  {
    FN1(UNKNOWN);

#if !defined(_X360SHADERGEN)
    FN(R8G8B8);
    FN(R3G3B2);
    FN(A8P8);
    FN(A4L4);
    FN(A8R3G3B2);
    FN(X8L8V8U8);
    FN(D32);
    FN(D15S1);
    FN(D24X8);
    FN(D24X4S4);
    FN(P8);

    // following formats map to some existing format
    FN(DXT3);
    FN(DXT5);
    FN(D16_LOCKABLE);
#endif
    FN(X4R4G4B4);
    FN(A16B16G16R16);
    FN(A16B16G16R16F);
    FN(A32B32G32R32F);

    FN(R16F);
    FN(R32F);

    FN(G16R16F);
    FN(G32R32F);
    FN(L16);
    FN(G16R16);
    FN(A8R8G8B8);
    FN(A2R10G10B10);
    FN(A2B10G10R10);
    FN(X8R8G8B8);
    FN(R5G6B5);
    FN(X1R5G5B5);
    FN(A1R5G5B5);
    FN(A4R4G4B4);
    FN(A8);


    FN(L8);
    FN(A8L8);

    FN(V8U8);
    FN(L6V5U5);
    FN(Q8W8V8U8);
    FN(V16U16);

    FN(UYVY);
    FN(YUY2);
    FN(DXT1);
    FN(DXT2);
    FN(DXT4);

    FN(D24S8);
    FN(D16);
  }
  return "OTHER";
}

struct FormatNeeded
{
  /// which format
  D3DFORMAT fmt;
  /// which boolean tells if the format is available
  bool EngineDD9::Caps::*available;
};

#if !defined(_X360SHADERGEN)
// format list from d3d9types.h, bpp hand filled. For some exotic formats bpp may be wrong
static SSupportedFormats KnownFormats[]=
{
  {D3DFMT_R8G8B8               , 24},
  {D3DFMT_A8R8G8B8             , 32},
  {D3DFMT_X8R8G8B8             , 32},
  {D3DFMT_R5G6B5               , 16},
  {D3DFMT_X1R5G5B5             , 16},
  {D3DFMT_A1R5G5B5             , 16},
  {D3DFMT_A4R4G4B4             , 16},
  {D3DFMT_R3G3B2               , 8},
  {D3DFMT_A8                   , 8},
  {D3DFMT_A8R3G3B2             , 8},
  {D3DFMT_X4R4G4B4             , 16},
  {D3DFMT_A2B10G10R10          , 16},
  {D3DFMT_A8B8G8R8             , 16},
  {D3DFMT_X8B8G8R8             , 16},
  {D3DFMT_G16R16               , 32},
  {D3DFMT_A2R10G10B10          , 32},
  {D3DFMT_A16B16G16R16         , 64},

  {D3DFMT_A8P8                 , 16},
  {D3DFMT_P8                   , 8},

  {D3DFMT_L8                   , 8},
  {D3DFMT_A8L8                 , 16},
  {D3DFMT_A4L4                 , 8},

  {D3DFMT_V8U8                 , 16},
  {D3DFMT_L6V5U5               , 16},
  {D3DFMT_X8L8V8U8             , 32},
  {D3DFMT_Q8W8V8U8             , 32},
  {D3DFMT_V16U16               , 32},
  {D3DFMT_A2W10V10U10          , 32},

  {D3DFMT_UYVY                 , 32},
  {D3DFMT_R8G8_B8G8            , 32},
  {D3DFMT_YUY2                 , 32},
  {D3DFMT_G8R8_G8B8            , 32},
  {D3DFMT_DXT1                 , 4},
  {D3DFMT_DXT2                 , 8},
  {D3DFMT_DXT3                 , 8},
  {D3DFMT_DXT4                 , 8},
  {D3DFMT_DXT5                 , 8},

  {D3DFMT_D16_LOCKABLE         , 16},
  {D3DFMT_D32                  , 32},
  {D3DFMT_D15S1                , 16},
  {D3DFMT_D24S8                , 32},
  {D3DFMT_D24X8                , 32},
  {D3DFMT_D24X4S4              , 32},
  {D3DFMT_D16                  , 16},

  {D3DFMT_D32F_LOCKABLE        , 32},
  {D3DFMT_D24FS8               , 32},

  {D3DFMT_L16                  , 16},

  {D3DFMT_VERTEXDATA           , 1},
  {D3DFMT_INDEX16              , 16},
  {D3DFMT_INDEX32              , 32},

  {D3DFMT_Q16W16V16U16         , 64},

  {D3DFMT_MULTI2_ARGB8         , 32},

  {D3DFMT_R16F                 , 16},
  {D3DFMT_G16R16F              , 32},
  {D3DFMT_A16B16G16R16F        , 64},

  {D3DFMT_R32F                 , 32},
  {D3DFMT_G32R32F              , 64},
  {D3DFMT_A32B32G32R32F        , 128},

  {D3DFMT_CxV8U8               , 16},
};
#endif

static int FormatToBpp(D3DFORMAT format)
{
#if defined(_X360SHADERGEN)
  return XGBitsPerPixelFromFormat(format);
#else
  for (int i = 0; i < lenof(KnownFormats); i++) {
    if (KnownFormats[i]._format == format) return KnownFormats[i]._bpp;
  }
  return 0;
#endif
}

void EngineDD9::InitDeviceState3D(int cb)
{
  // make sure we are ready to T&L, and do not have to switch it on later
  SwitchTL(cb,TLEnabled);
}

void EngineDD9::InitDeviceState(int cb)
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

  D3DSetRenderState(cb, D3DRS_ZFUNC,D3DCMP_LESSEQUAL,false);
  D3DSetRenderState(cb, D3DRS_ZWRITEENABLE,TRUE,false);

#if !defined(_X360SHADERGEN)
  D3DSetRenderState(cb, D3DRS_SHADEMODE,D3DSHADE_GOURAUD,false);
#endif

  D3DSetRenderState(cb, D3DRS_CULLMODE,D3DCULL_CCW,false);

#if !defined(_X360SHADERGEN)
  D3DSetRenderState(cb, D3DRS_CLIPPING,FALSE,false);
  D3DSetRenderState(cb, D3DRS_LIGHTING,FALSE,false);
  // on 16-bit devices use dithering
  D3DSetRenderState(cb, D3DRS_DITHERENABLE,_pixelSize<=16,false);
#endif

  D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE,FALSE,false);
  D3DSetRenderState(cb, D3DRS_ALPHAFUNC,D3DCMP_GREATEREQUAL,false);
  D3DSetRenderState(cb, D3DRS_ALPHAREF,0x1,false);

  D3DSetRenderState(cb, D3DRS_ALPHABLENDENABLE,FALSE,false);
  D3DSetRenderState(cb, D3DRS_SRCBLEND,D3DBLEND_SRCDEF,false);
  D3DSetRenderState(cb, D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA,false);
  D3DSetRenderState(cb, D3DRS_BLENDOP,D3DBLENDOP_ADD,false);

  SetPixelShaderConstantF(cb,PSC_FogColor,_fogColor.GetArray(),1,false);

  // to do: we should not use bilinear filtering on devices
  // which do not support clamping


  // try to use trilinear interpolation if possible
  // always use bilinear interpolation

  for (int i = 0; i < MaxSamplerStages; i++)
  {
    D3DSetSamplerState(cb, i, D3DSAMP_MAGFILTER,      D3DTEXF_LINEAR,   false);
    D3DSetSamplerState(cb, i, D3DSAMP_MINFILTER,      D3DTEXF_LINEAR,   false);
    D3DSetSamplerState(cb, i, D3DSAMP_MIPFILTER,      D3DTEXF_LINEAR,   false);
    D3DSetSamplerState(cb, i, D3DSAMP_MAXANISOTROPY,  2,                false);
    D3DSetSamplerState(cb, i, D3DSAMP_ADDRESSU,       D3DTADDRESS_WRAP, false);
    D3DSetSamplerState(cb, i, D3DSAMP_ADDRESSV,       D3DTADDRESS_WRAP, false);
    D3DSetSamplerState(cb, i, D3DSAMP_SRGBTEXTURE,    TRUE,             false);
  }

  D3DSetRenderState(cb, D3DRS_SRGBWRITEENABLE, FALSE, false);

  SetTexture0Color(cb,HWhite,HWhite,false);

  // Set the default values
	D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE, 0x0000000F, false);
  D3DSetRenderState(cb, D3DRS_STENCILENABLE, 	FULL_STENCIL, false);
  D3DSetRenderState(cb, D3DRS_STENCILREF, 0, false);
  D3DSetRenderState(cb, D3DRS_STENCILMASK, STENCIL_SHADOW_LEVEL_MASK, false);
  D3DSetRenderState(cb, D3DRS_STENCILWRITEMASK, STENCIL_FULL, false);
  D3DSetRenderState(cb, D3DRS_STENCILFUNC, D3DCMP_ALWAYS, false);

  // Hack in the nVidia driver that makes G80 equivalent to G7x what concerns fog
  //D3DSetRenderStateOpt(D3DRS_POINTSIZE, MAKEFOURCC('H', 'L', '2', 'A'), false);

  // Shadow states settings
  D3DSetRenderState(cb, D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE, false);
  D3DSetRenderState(cb, D3DRS_CULLMODE, D3DCULL_CCW, false);
  DoShadowsStatesSettings(cb,SS_Disabled,false);

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

/// make sure the job is executed for each thread
struct JobForEachThread: public IMicroJob
{
  volatile AtomicInt *_left;
  
  JobForEachThread(AtomicInt *left):_left(left){}
  
  /// make sure each thread runs one task and then waits until all other threads do the same
  void WaitForAllThreads()
  {
    // indicate we are done
    --*_left;
    MemoryBarrier();
    // wait for indication from other threads, prevent scheduling other tasks for this thread meanwhile
    while (*_left>0)
    {
      YieldProcessorNice();
    }
  }
};

static int Clip32B(long long x)
{
  if (x>INT_MAX) return INT_MAX;
  return (int)x;
}

static int Clip32BUnsigned(long long x)
{
  if (x>UINT_MAX) return UINT_MAX;
  return (unsigned)x;
}

void EngineDD9::Init3DState()
{
	#define HANDLE_ERR(str, error)  if (error != D3D_OK) {	DDError9(str, error);		return;	}
	
  long long texMemBeforeInit = _d3DDevice->GetAvailableTextureMem();
  LogF("T. GetAvailableTextureMem %lld",texMemBeforeInit);


  HRESULT err;
  _sbTechnique = SBT_None;
  _dbAvailable = false;

  _caps._dxtFormats = 0;

  _d3DDevice->GetRenderTarget(0, _backBuffer.Init());

  // Remember depth buffer associated with the device
  _d3DDevice->GetDepthStencilSurface(_depthBuffer.Init());
  
  _currRenderTargetSurface = _backBuffer;
  _currRenderTargetDepthBufferSurface = _depthBuffer;
  
  D3DSURFACE_DESC bdesc;
  err=_backBuffer->GetDesc(&bdesc);
  if (err != D3D_OK) {DDError9("GetSurfaceDesc failed.",err);return;}

  Assert(_caps._rtAllocated==0);
  // front-buffer+back-buffer+depth-buffer
  _caps._rtAllocated += (2*FormatToBpp(bdesc.Format)+FormatToBpp(_presentPars.AutoDepthStencilFormat))*(bdesc.Width*bdesc.Height/8);


  // we can verify local/nonlocal numbers now

  // if we are up to 30 MB wrong, we assume the information is probably valid

  const int oneMB = 1024*1024;
#if defined(_X360SHADERGEN)
  _caps._localVramAmount = 64*oneMB;
  _caps._nonlocalVramAmount = 0;
#else
  long long totalByDX7 = (long long)_caps._localVramAmount+_caps._nonlocalVramAmount;  
  long long totalByDX9 = _caps._rtAllocated+texMemBeforeInit;

  LogF("T2. RT %u, GetAvailableTextureMem %lld",_caps._rtAllocated,texMemBeforeInit);
  LogF("1. Total by DX7 %lld, by DX9 %lld:, Local %d, nonLocal %d",totalByDX7,totalByDX9,_caps._localVramAmount,_caps._nonlocalVramAmount);

  // DX9 may report some less memory, as there may be some desktop / additional monitor
  // on the other hand, sometimes it seems DX9 completely screws and reports very little memory
  
  if (_caps._wddm)
  {
  
  }
  else if (totalByDX9>=INT_MAX && _caps._nonlocalVramAmount>=INT_MAX && _caps._localVramAmount>450*oneMB)
  {
    // if below does not work if the numbers reported were above 2 GB and we have clamped them already
    // verify the values are reasonable, if they are assume DX7 numbers to be reliable
  }
  else if (totalByDX9<totalByDX7-8*oneMB || totalByDX9-32*oneMB>totalByDX7)
  {
    // we need to fix our estimates
    // be pessimistic and assume the non-local number is correct
    _caps._localVramAmount = Clip32B(totalByDX9 - _caps._nonlocalVramAmount);
    LogF("2. Total by DX7 %lld, by DX9 %lld:, Local %d, nonLocal %d",totalByDX7,totalByDX9,_caps._localVramAmount,_caps._nonlocalVramAmount);
    // when such result is unexpectedly low, replace it with something else
    if (_caps._localVramAmount<totalByDX9/2) // totalByDX9/2 always fits within 32b limits
    {
      // use heuristics - assume half is VRAM and half AGP
      _caps._localVramAmount = Clip32B(totalByDX9/2);
      _caps._nonlocalVramAmount = Clip32B(totalByDX9-_caps._localVramAmount);
      LogF("3. Total by DX7 %lld, by DX9 %lld:, Local %d, nonLocal %d",totalByDX7,totalByDX9,_caps._localVramAmount,_caps._nonlocalVramAmount);
    }
  }
  
  // never assume less than our minimal requirements
  if (_caps._localVramAmount<256*oneMB) _caps._localVramAmount = 256*oneMB;
#endif

  // Create the render target texture
  {

    // use render to texture if possible on PC - avoids using StretchRect
    const bool forceRenderTarget = false;

    if (_rtMultiSampleType!=D3DMULTISAMPLE_NONE || forceRenderTarget)
    {
      // Create render target
      err = CreateRenderTarget(_wRT, _hRT, _rtFormat, _rtMultiSampleType, _rtMultiSampleQuality, FALSE, _renderTargetSurfaceS, false, NULL);
      Log("_renderTargetSurfaceS %dx%d",_wRT,_hRT);
      HANDLE_ERR("Error: Render target surface creation failed.", err)

      // Create depth buffer
      err = CreateDepthStencilSurface(_wRT, _hRT, D3DFMT_D24S8, _rtMultiSampleType, _rtMultiSampleQuality, TRUE, _depthBufferRT, _renderTargetSurfaceS, NULL);
      Log("_depthBufferRT %dx%d",_wRT,_hRT);
      HANDLE_ERR("Error: Depth buffer creation failed.", err)
     
      //    static const int assumedSamples[4]={2,4,6,8}
      //    int index = _rtMultiSampleQuality-1;
      //    saturate(index,0,lenof(assumedSamples)-1);
      //    // assume 32bpp AA back-buffer + 32bpp AA depth-buffer
      //    int aaSamples = assumedSamples[index];
      //      (FormatToBpp(_rtFormat)+FormatToBpp(D3DFMT_D24S8))*aaSamples // Render target + depth buffer
      //    )*bdesc.Width*bdesc.Height;
    }
    else
    {
      _depthBufferRT.Free();

      // Create render target
      err = CreateTextureRT(_wRT, _hRT, 1, _rtFormat, D3DPOOL_DEFAULT, _renderTargetS, true);
      HANDLE_ERR("Error: Render target texture creation failed.", err);
				
      err = _renderTargetS->GetSurfaceLevel(0, _renderTargetSurfaceS.Init());
      HANDLE_ERR("Error: Render target surface creation failed.", err); 

      if (_wRT>_w || _hRT>_h)
      {
        err = CreateDepthStencilSurface(_wRT, _hRT, D3DFMT_D24S8, _rtMultiSampleType, _rtMultiSampleQuality, TRUE, _depthBufferRT, _renderTargetSurfaceS, NULL);
        HANDLE_ERR("Error: Depth buffer creation failed.", err);
      }
    }

    // Create DepthInfo surfaces - no multisampling used, used later for SSSM rendering
    {
      // Create render target containing depth
      err = CreateTextureRT(_wRT, _hRT, 1, D3DFMT_R32F, D3DPOOL_DEFAULT, _renderTargetDepthInfo, true);
      Log("_renderTargetDepthInfo %dx%d",_wRT,_hRT);
			HANDLE_ERR("Error: Render target depth texture creation failed.", err); 

      err = _renderTargetDepthInfo->GetSurfaceLevel(0, _renderTargetDepthInfoSurface.Init());
      HANDLE_ERR("Error: Render target depth surface creation failed.", err); 

      if (_rtMultiSampleType!=D3DMULTISAMPLE_NONE)
      {
        err = CreateDepthStencilSurface(_wRT, _hRT, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, _renderTargetDepthInfoZ, _renderTargetDepthInfoSurface, NULL);
        Log("_renderTargetDepthInfoZ %dx%d",_wRT,_hRT);
        HANDLE_ERR("Error: Render target depth surface Z creation failed.", err);
      }
      else
      {
        _renderTargetDepthInfoZ.Free();
      }
    }

    // Create render target destination
    err = CreateTextureRT(_wRT, _hRT, 1, _rtFormat, D3DPOOL_DEFAULT, _renderTargetD, true);
    HANDLE_ERR("Error: Render target texture or surface creation failed.", err); 
    
		err = _renderTargetD->GetSurfaceLevel(0, _renderTargetSurfaceD.Init());
		HANDLE_ERR("Error: Render target surface creation failed.", err);
  }

  // Get current adapter 
  UINT adapter;
  {
    D3DDEVICE_CREATION_PARAMETERS cpars;
    _d3DDevice->GetCreationParameters(&cpars);
    adapter = cpars.AdapterOrdinal;
  }

  // Get back buffer format
  D3DFORMAT bbFormat = bdesc.Format;

  err = _direct3D->CheckDeviceFormat(
    adapter,_devType,bbFormat,0,D3DRTYPE_TEXTURE,D3DFMT_DXT1
    );
  if (err==D3D_OK) _caps._dxtFormats |= 1<<1;
  err = _direct3D->CheckDeviceFormat(
    adapter,_devType,bbFormat,0,D3DRTYPE_TEXTURE,D3DFMT_DXT2
    );
  if (err==D3D_OK) _caps._dxtFormats |= 1<<2;
  err = _direct3D->CheckDeviceFormat(
    adapter,_devType,bbFormat,0,D3DRTYPE_TEXTURE,D3DFMT_DXT3
    );
  if (err==D3D_OK) _caps._dxtFormats |= 1<<3;
  err = _direct3D->CheckDeviceFormat(
    adapter,_devType,bbFormat,0,D3DRTYPE_TEXTURE,D3DFMT_DXT4
    );
  if (err==D3D_OK) _caps._dxtFormats |= 1<<4;
  err = _direct3D->CheckDeviceFormat(
    adapter,_devType,bbFormat,0,D3DRTYPE_TEXTURE,D3DFMT_DXT5
    );
  if (err==D3D_OK) _caps._dxtFormats |= 1<<5;
  LogF("DXT support %x",_caps._dxtFormats);
  if (_caps._dxtFormats!=(31<<1))
  {
    ErrorMessage("DXT1..DXT5 support missing.");
  }

  _apertureFormat = D3DFMT_X8R8G8B8;
#if 1
  // we want a high-resolution format for aperture control
  static const D3DFORMAT apertureFormats[]=
  {
    D3DFMT_R32F,
    D3DFMT_R16F,
    D3DFMT_G16R16F,
    D3DFMT_A16B16G16R16F,
    D3DFMT_G32R32F,
    D3DFMT_A32B32G32R32F,
    // using 16b integer might be possible, but we did not test it
    //D3DFMT_L16,
    //D3DFMT_G16R16
  };
  for (int i=0; i<lenof(apertureFormats); i++)
  {
    // we do not require any alpha blending or alpha testing
    D3DRESOURCETYPE type = D3DRTYPE_TEXTURE;
    err = _direct3D->CheckDeviceFormat(adapter,_devType,bbFormat,D3DUSAGE_RENDERTARGET,type,apertureFormats[i]);
    if (err==D3D_OK)
    {
      _apertureFormat = apertureFormats[i];
      break;
    }
  }
  LogF("Aperture surface format %s",SurfaceFormatNameDD9(_apertureFormat));
#endif


  err = _direct3D->CheckDeviceFormat(
    adapter,_devType,bbFormat,0,D3DRTYPE_TEXTURE,D3DFMT_A8R8G8B8
    );
  _caps._can8888 = ( err==D3D_OK );
  if (_caps._can8888) LogF("Can 8888 textures");
  err = _direct3D->CheckDeviceFormat(
    adapter,_devType,bbFormat,0,D3DRTYPE_TEXTURE,D3DFMT_A8L8
    );
  _caps._can88 = ( err==D3D_OK );
  if (_caps._can88) LogF("Can 88 textures");
  // note: 1555 and 4444 support required
  err = _direct3D->CheckDeviceFormat(
    adapter,_devType,bbFormat,0,D3DRTYPE_TEXTURE,tex_A1R5G5B5
    );
  if (err!=D3D_OK)
  {
    ErrorMessage("Texture format ARGB1555 required.");
  }
  err = _direct3D->CheckDeviceFormat(
    adapter,_devType,bbFormat,0,D3DRTYPE_TEXTURE,D3DFMT_A4R4G4B4
    );
  if (err!=D3D_OK)
  {
    ErrorMessage("Texture format ARGB4444 required.");
  }


  D3DCAPS9 caps;
  _d3DDevice->GetDeviceCaps(&caps);

  _caps._maxTextureSize = min(caps.MaxTextureWidth,caps.MaxTextureHeight);

  _modColor = HWhite;

  _caps._canReadSRGB = true;
  _linearSpace = false;
  _caps._rtIsSRGB = true;

  // note: A8L8 has often no SRGB support - we almost never use it, and we therefore do not mind
  static const FormatNeeded srgbFormats[]=
  {
    {D3DFMT_DXT1,NULL},
    {D3DFMT_DXT2,NULL},
    {D3DFMT_DXT3,NULL},
    {D3DFMT_DXT4,NULL},
    {D3DFMT_DXT5,NULL},
    {D3DFMT_A8R8G8B8,&Caps::_can8888},
    //{D3DFMT_A8L8,&EngineDD9::_caps._can88},
    //{D3DFMT_A4R4G4B4,NULL},
  };
  for (int i=0; i<lenof(srgbFormats); i++)
  {
    D3DFORMAT fmt = srgbFormats[i].fmt;
    bool isAvailable = srgbFormats[i].available!=NULL ? _caps.*srgbFormats[i].available : true;
    if (isAvailable)
    {
      err = _direct3D->CheckDeviceFormat(
        adapter,_devType,bbFormat,D3DUSAGE_QUERY_SRGBREAD,D3DRTYPE_TEXTURE,fmt
        );
      if (err!=D3D_OK)
      {
        _caps._canReadSRGB = false;
        LogF("Missing SRGB read support for %s",SurfaceFormatNameDD9(fmt));
      }
    }
  }
  if (_caps._canReadSRGB)
  {
    LogF("SRGB read available for all texture formats");
  }

  _w=bdesc.Width;
  _h=bdesc.Height;

	#ifdef NEW_SB_INIT
		_sbFormatDesc = SBTR_FindFormat2();
	#else
	 // Check what SB technique we can use
  // TODO: check ATI specific formats
  {
    _sbTechnique = SBT_Default;
    //     _sbTechnique = SBT_Default;
    // #else
    if (false) {} // empty if so that we can use else if in all following branches
    //     else if (SBTR_nVidiaCheck(adapter, _devType, _presentPars.BackBufferFormat) || _nvPerfHUDActive)
    //     {
    //       _sbTechnique = SBT_nVidia;
    //     }
    else if (SBTR_nVidiaINTZCheck(adapter, _devType, _presentPars.BackBufferFormat))
    {
      _sbTechnique = SBT_nVidiaINTZ;
    }
    else if (SBTR_ATID24Check(adapter, _devType, _presentPars.BackBufferFormat))
    {
      _sbTechnique = SBT_ATIDF24;
    }
    //     else if (SBTR_DefaultCheck(adapter, _devType, _presentPars.BackBufferFormat))
    //     {
    //       _sbTechnique = SBT_Default;
    //     }
    // #endif
  }

	#endif


  // Check if we can use the depth buffer
  {
    _dbAvailable = (_direct3D->CheckDeviceFormat(
      adapter, _devType, _presentPars.BackBufferFormat,
      D3DUSAGE_RENDERTARGET,
      D3DRTYPE_TEXTURE,
      D3DFMT_R32F) == D3D_OK);
  }

  _minGuardX = 0; // guard band clipping properties
  _maxGuardX = _w;
  _minGuardY = 0;
  _maxGuardY = _h;

  _nightEye = 0;

  // check actual guard band clipping capabilities

  _pixelSize = 32;
  switch (bdesc.Format)
  {
  case D3DFMT_X1R5G5B5:
  case D3DFMT_R5G6B5:
  case D3DFMT_A1R5G5B5:
    _pixelSize = 16;
    break;
  } 

  _depthBpp = 32;

  D3DSURFACE_DESC ddesc;
  ComRef<IDirect3DSurface9> zBuffer;
  _d3DDevice->GetDepthStencilSurface(zBuffer.Init());
  if (zBuffer)
  {
    zBuffer->GetDesc(&ddesc);
    switch (ddesc.Format)
    {
#if !defined(_X360SHADERGEN)
  case D3DFMT_D16_LOCKABLE:
  case D3DFMT_D15S1:
#endif
  case D3DFMT_D16:
    _depthBpp = 16;
    break;
    } 
  }
  LogF("Bpp %d, depth %d",_pixelSize,_depthBpp);

  // fix: enabled guard band clipping
#if 1
  float minGuardX = caps.GuardBandLeft;
  float maxGuardX = caps.GuardBandRight;
  float minGuardY = caps.GuardBandTop;
  float maxGuardY = caps.GuardBandBottom;
  float maxBand = 1024*4;
  saturate(minGuardX,-maxBand,0);
  saturate(minGuardY,-maxBand,0);
  saturate(maxGuardX,_wRT,_wRT+maxBand);
  saturate(maxGuardY,_hRT,_hRT+maxBand);
  _minGuardX = toInt(minGuardX), _maxGuardX = toInt(maxGuardX);
  _minGuardY = toInt(minGuardY), _maxGuardY = toInt(maxGuardY);
  LogF
    (
    "Guard band x %d to %d, y %d to %d",
    _minGuardX,_maxGuardX,_minGuardY,_maxGuardY
    );
#endif

  if( caps.TextureCaps&D3DPTEXTURECAPS_SQUAREONLY )
  {
    ErrorMessage("Only square texture supported by HW.");
  }

  // now make a Viewport
  // Setup the viewport for a reasonable viewing area
  _viewW = _wRT;
  _viewH = _hRT;

  {
    _minZRange = DepthBorderCommonMin;
    _maxZRange = DepthBorderCommonMax;

    D3DVIEWPORT9 viewData;
    memset(&viewData,0,sizeof(viewData));
    viewData.X = 0;
    viewData.Y = 0;
    viewData.MinZ = _minZRange;
    viewData.MaxZ = _maxZRange;
    // we keep the 2D viewport here
    // if anyone sets a 3D viewport, he will reset the settings
    viewData.Width  = _w;
    viewData.Height = _h;
    _d3DDevice->SetViewport(viewData);
  }

  // Set default render state

  _minZRange = 0;
  _maxZRange = 1;

#if defined(_X360SHADERGEN)
  _caps._rtAllocated = 0;
#else
  // note: with shared device, other processes may be allocating/releasing meanwhile
  // following calculation is therefore only estimation
  DWORD texMemAfterInit = _d3DDevice->GetAvailableTextureMem();
  _caps._rtAllocated += Clip32B(texMemBeforeInit-texMemAfterInit);
  if (_caps._rtAllocated<0) _caps._rtAllocated = 0;
  LogF("%d MB used for render targets",_caps._rtAllocated/1024/1024);
#endif

  // always init the immediate device state
  InitDeviceState(-1);

	#undef HANDLE_ERR
}

void EngineDD9::Done3DState()
{
  _backBuffer.Free();
  _depthBuffer.Free();
  _depthBufferRT.Free();
  _renderTargetD.Free();
  _renderTargetSurfaceD.Free();
  _renderTargetDepthInfo.Free();
  _renderTargetDepthInfoSurface.Free();
  _renderTargetDepthInfoZ.Free();
  _renderTargetS.Free();
  _renderTargetSurfaceS.Free();
  _currRenderTarget.Free();
  _currRenderTargetSurface.Free();
  _currRenderTargetDepthBufferSurface.Free();
  DestroyTIResources();
#if _HELISIM_RENDER_TO_TEXTURE
  DestroyR2TResources();
#endif
  DestroyResources();
  DestroySBResources();

	#ifdef TEST_RAIN_MAP
		DestroyRainMapResources();
	#endif

#if _ARMA3_SIMUL_WEATHER
  DeInitSimulWeather();
#endif
}

#define CAN_DXT(i) ( (_caps._dxtFormats&(1<<(i)))!=0 )
void EngineDD9::InitVRAMPixelFormat( D3DFORMAT &pf, PacFormat format, bool isSRGB )
{
  switch( format )
  {
  case PacDXT1:
    if (CAN_DXT(1)) pf = D3DFMT_DXT1;
    else pf = tex_A1R5G5B5;
    break;
  case PacDXT2:
    if( CAN_DXT(2) ) pf = D3DFMT_DXT2;
    else pf = tex_A4R4G4B4;
    break;
  case PacDXT3:
    if( CAN_DXT(3) ) pf = D3DFMT_DXT3;
    else pf = tex_A4R4G4B4;
    break;
  case PacDXT4:
    if( CAN_DXT(4) ) pf = D3DFMT_DXT4;
    else pf = tex_A4R4G4B4;
    break;
  case PacDXT5:
    if( CAN_DXT(5) ) pf = D3DFMT_DXT5;
    else pf = tex_A4R4G4B4;
    break;
  case PacARGB1555:
    pf = tex_A1R5G5B5;
    break;
  case PacRGB565:
    pf = tex_R5G6B5;
    break;
  case PacAI88:
    if (Can88())
    {
      pf = tex_A8L8;
      break;
    }
    // note: previous case may fall through
  case PacARGB8888:
    if (Can8888())
    {
      pf = tex_A8R8G8B8;
      break;
    }
    // note: previous case may fall through
  case PacARGB4444:
    pf = tex_A4R4G4B4;
    break;
#if !defined(_X360SHADERGEN)
  case PacP8:
    pf = D3DFMT_P8;
    Fail("Palette textures obsolete");
    break;
#endif
  default:
    ErrorMessage("Texture has bad pixel format (VRAM).");
    break;
  }
}

#include <El/Debugging/debugTrap.hpp>



typedef IDirect3D9 *WINAPI Direct3DCreate9F(UINT SDKVersion);

RString EngineDD9::GetDebugName() const
{
  RString ret = "D3D9";

  if (_d3DDevice.IsNull())
  {
    ret = ret + ": <null>";
  }
  else
  {
    D3DDEVICE_CREATION_PARAMETERS cp;
    //PROFILE_DX_SCOPE(3dGDN);
    _d3DDevice->GetCreationParameters(&cp);
    D3DADAPTER_IDENTIFIER9 id;
    _direct3D->GetAdapterIdentifier(cp.AdapterOrdinal, 0, &id);
    char driverVersion[256];
    sprintf
      (
      driverVersion,"%d.%d.%d.%d",
      HIWORD(id.DriverVersion.HighPart),LOWORD(id.DriverVersion.HighPart),
      HIWORD(id.DriverVersion.LowPart),LOWORD(id.DriverVersion.LowPart)
      );
    ret = ret + ", Device: " + RString(id.Description);
    ret = ret + ", Driver:" + RString(id.Driver) + RString(" ") + RString(driverVersion);
  }

  return ret;
}

#if !XBOX_D3D

/// attempt to obtain Vista Extended Direct3D
/**
We try to use some advanced Vista only features when available
*/

static HRESULT MyDirect3DCreate9Ex( IDirect3D9Ex **d3dEx )
{
  HRESULT hr = ERROR_NOT_SUPPORTED;

  if (!WinXP)
  {
    // Manually load the d3d9.dll library.
    HMODULE libHandle =  LoadLibrary("d3d9.dll");
    if(libHandle)
    {
      typedef HRESULT (WINAPI *LPDIRECT3DCREATE9EX)( UINT, IDirect3D9Ex **);

      // Obtain the address of the Direct3DCreate9Ex function. 
      LPDIRECT3DCREATE9EX direct3DCreate9ExPtr = (LPDIRECT3DCREATE9EX)GetProcAddress( libHandle, "Direct3DCreate9Ex" );
      if ( direct3DCreate9ExPtr != NULL)
      {
        // Direct3DCreate9Ex is supported.
        hr = direct3DCreate9ExPtr(D3D_SDK_VERSION,d3dEx);
      }
      
      FreeLibrary( libHandle );
    }
  }
  

  return hr;
}
#endif

bool EngineDD9::CreateD3D()
{
  if (_direct3D) return true;
  
  // load Direct3DCreate9Ex dynamically to support WinXP
  
  #if !XBOX_D3D
  HRESULT exOk =  MyDirect3DCreate9Ex(_direct3DEx.Init());
  if (SUCCEEDED(exOk) && _direct3DEx.NotNull())
  {
    _direct3D += _direct3DEx.GetRef();
  }
  else
  #endif
  {
    _direct3D << Direct3DCreate9(D3D_SDK_VERSION);
    if (!_direct3D)
    {
      ErrorMessage("DX9 SDK version %d required.",D3D_SDK_VERSION);
      return false;
    }
  }

  return true;
}

void EngineDD9::DestroyD3D()
{
  _direct3D.Free();
}

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

void EngineDD9::SearchMonitor(int &x, int &y, int &w, int &h)
{
#if !defined(_X360SHADERGEN)
  MONITORINFO mi;
  int adapter = D3DAdapter;
  bool monFound = false;
  if (adapter>=0)
  {
    HMONITOR mon = _direct3D->GetAdapterMonitor(adapter);
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

static SSupportedFormats supportedFormats[] = {
#if !defined(_X360SHADERGEN)
  {D3DFMT_A8R8G8B8, 32},
  {D3DFMT_X8R8G8B8, 32},
  {D3DFMT_R8G8B8, 24},
  {D3DFMT_X4R4G4B4, 16},
  {D3DFMT_R5G6B5, 16},
  {D3DFMT_X1R5G5B5, 16},
  {D3DFMT_A1R5G5B5, 16},
  {D3DFMT_A4R4G4B4, 16},
  {D3DFMT_R3G3B2, 8},
  {D3DFMT_A8R3G3B2, 8},
  {D3DFMT_A8, 8}
#else
  {D3DFMT_A8R8G8B8, 32},
  {D3DFMT_X8R8G8B8, 32},
  {D3DFMT_R5G6B5, 16},
  {D3DFMT_X1R5G5B5, 16},
  {D3DFMT_A1R5G5B5, 16},
  {D3DFMT_A4R4G4B4, 16},
  {D3DFMT_A8, 8}
#endif
};

static const struct {D3DFORMAT format;Engine::HDRPrec prec;} PreferredRTFormat[] = {
  {D3DFMT_A16B16G16R16F,Engine::HDR16},
  {D3DFMT_A32B32G32R32F,Engine::HDR32},
};


Engine::HDRPrec EngineDD9::ValidateHDRPrecision(HDRPrec prec) const
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

bool EngineDD9::SetHDRPrecisionWanted(HDRPrec prec)
{
  prec = ValidateHDRPrecision(prec);
  if (prec==_hdrPrecWanted) return false;

  _hdrPrecWanted=prec;
  return true;
}

void EngineDD9::SetHDRPrecision(HDRPrec prec)
{
  if (SetHDRPrecisionWanted(prec))
  {
    ApplyWanted();
  }
}
int EngineDD9::GetFSAA() const
{
  return _fsAAWanted;
}

bool EngineDD9::SetFSAAWanted(int quality)
{
  if (quality>_maxFSAA) quality = _maxFSAA;
  if (quality==_fsAAWanted) return false;
  _fsAAWanted = quality;
  return true;
}

void EngineDD9::SetFSAA(int level)
{
  if (SetFSAAWanted(level))
  {
    ApplyWanted();
  }
}


void EngineDD9::ApplyWanted()
{
  D3DDEVICE_CREATION_PARAMETERS pars;
  _d3DDevice->GetCreationParameters(&pars);
  int adapter = pars.AdapterOrdinal;
  FindMode(adapter);

  // note: complete reset may be not necessary - deleting all surfaces might do
  Reset();
  CommitSettings();
}

void EngineDD9::CommitSettings()
{
  EndHighLevelRecording();
  EndBackgroundScope(true,"commit");
  
  EnableSBShadowsCommit();
	
	#ifdef TEST_RAIN_MAP
		//rain mus always render into rain map
		if (!IsRainMapEnabled())
			CreateRainMapResources();
	#endif

  if (_textBank) _textBank->InitDetailTextures();
}



int EngineDD9::GetMaxFSAA() const
{
  return _maxFSAA;
}

int EngineDD9::GetAnisotropyQuality() const
{
  return _afWanted;
}

void EngineDD9::FlushPushBuffers()
{
#if USE_PUSHBUFFERS
  if (!TextBankDD()) return;
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());

  for (VertexBufferD3D9LRUItem *vb = _vBufferLRU.First(); vb; vb=_vBufferLRU.Next(vb))
  {
    const Shape *shape = vb->_owner;
    if (!shape) continue;
    VertexBuffer *buf = shape->GetVertexBuffer();
    if (!buf) continue;
    buf->DiscardPushBuffers();
  }
#endif

}

void EngineDD9::SetAnisotropyQuality(int level)
{
  if (level==_afWanted) return;
  _afWanted = level;

  // we need to discard all pushbuffers, as they have the filtering information recorded
  FlushPushBuffers();
}

__forceinline static unsigned char CalculatePostEffectsMask(int quality)
{
  unsigned char postFXLevelMask = (1 << (quality + 1)) - 1;

  return postFXLevelMask;
}

bool EngineDD9::SetPostprocessEffectsWanted(int quality)
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

void EngineDD9::SetAToC(int level)
{
  if (GetFSAA() > 0)
  {
    saturate(level, AToC_Disable, AToC_EnableAll);
    _useAToC = level != AToC_Disable;
    _AToCMask = level;
  }
  else
    _useAToC = false;
}

int EngineDD9::GetAToC() const
{
  return _AToCMask;
}

void EngineDD9::SetPostprocessEffects(int quality)
{
  if (SetPostprocessEffectsWanted(quality))
  {
    ApplyWanted();
  }
}

int EngineDD9::GetPostprocessEffects() const
{
  return _postFxQuality;
}

int EngineDD9::GetDeviceLevel() const
{
  return _deviceLevel;
}

int EngineDD9::GetCurrentDeviceLevel() const
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

void EngineDD9::SetDefaultSettingsLevel(int level)
{
  if (Glob.config.videoOptions._overallSettings.Size() == 0)
  {
    // no overall settings loaded, set default values
//    SetDefaultSettings(); 
    return;
  }

  level = (level>>8);
  saturate(level, 0, Glob.config.videoOptions._overallSettings.Size() - 1);   //very low, low, normal, high, very high
  int iLevel = level-1;
  const VideoOptionsConfig::OverallQualityItem& item = Glob.config.videoOptions._overallSettings[level];

  // we prefer 16bit HDR by default
  Settings newSettings;
  newSettings._hdrPrec = Engine::BitsToHDRPrec(item._hdr._value);

  newSettings._multiSampleQuality = 0;
  newSettings._ppaaQuality = item._ppaaIndex;
  newSettings._fsaaQuality = item._fsaaIndex;
  newSettings._AToCMask = VideoOptionsConfig::ATOCValue::ConvertATOCValueToMask(item._atocQuality);
  newSettings._afWanted = item._anisotropicFiltering._value;
#if _HELISIM_RENDER_TO_TEXTURE
  newSettings._r2tQuality = item._pipIndex;
#endif
#if _ARMA3_SIMUL_WEATHER
  newSettings._simulWeatherQuality = item._cloudQualityIndex-1;
#endif
  newSettings._shadowQuality = item._shadowQualityIndex;
  newSettings._textureQuality = item._textureQualityIndex;

  newSettings._postFxQuality = item._postProcessQuality;

  //int _deviceLevel;
  // scan settings one by one
  // based on level select the most appropriate one, and set it (if supported)
  
  bool changed = SetTextureQualityWanted(newSettings._textureQuality);
  
  // do not set texture memory more than one degree up from Texture Quality
  if (_textureMemory>_textureQuality+1)
  {
    changed |= SetTextureMemoryWanted(_textureQuality+1);
  }

  // use fullscreen size even for a window
  if (!IsWindowed())
  {
    //auto-select resolution
    RECT rect = GetMonitorResolution();
    changed |= SetResolutionWanted((rect.right - rect.left) ,(rect.bottom - rect.top),32);
  }
  changed |= SetFillrateLevelWanted(iLevel);
  // with high-end prefer HDR=High if supported
  // most cards cannot do HDR16 and FSAA at the same time. We prefer FSAA
  changed |= SetHDRPrecisionWanted(newSettings._hdrPrec);

  changed |= (GetPPAAQuality()!=newSettings._ppaaQuality);
  SetPPAAQuality(newSettings._ppaaQuality);

#if _HELISIM_RENDER_TO_TEXTURE
  changed |= (GetPipQuality()!=newSettings._r2tQuality);
  SetPipQuality(newSettings._r2tQuality);
#endif

  // AToC
  SetAToC(newSettings._AToCMask);

  changed |= SetFSAAWanted(iLevel>-1 ? intMax(iLevel,1) : 0); // unless very low, we always want to get at least some FSAA

  SetAnisotropyQuality(newSettings._afWanted);
  changed |= SetPostprocessEffectsWanted(newSettings._postFxQuality);
  // both gamma and brightness do not affect performance - do not set here
  if (changed && _initialized)
  {
    ApplyWanted();
  }
}

D3DFORMAT EngineDD9::SelectRTFormat( int adapter, D3DFORMAT displayModeFormat, int minBpp )
{
  // we prefer a high-resolution format for the render target
  D3DFORMAT bestRTFormat = D3DFMT_X8R8G8B8;
  static const D3DFORMAT rtFormats[]=
  {
    //D3DFMT_X2R10G10B10, // we would like this, but it is not supported on PC
    D3DFMT_A2B10G10R10,
    D3DFMT_A2R10G10B10,
    D3DFMT_A16B16G16R16F,
    D3DFMT_A16B16G16R16,
    D3DFMT_A32B32G32R32F,
    D3DFMT_X8R8G8B8
  };
  for (int i=0; i<lenof(rtFormats); i++)
  {
    if (FormatToBpp(rtFormats[i])<minBpp) continue;
    // for render target we we require alpha blending + filtering
    D3DRESOURCETYPE type = D3DRTYPE_TEXTURE;
    HRESULT err = _direct3D->CheckDeviceFormat(
      adapter,_devType,displayModeFormat,
      D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_FILTER|D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,
      type,rtFormats[i]
    );
    if (err==D3D_OK)
    {
      bestRTFormat = rtFormats[i];
      break;
    }
  }
  return bestRTFormat;
}

void EngineDD9::FindMode(int adapter)
{
#if defined(_X360SHADERGEN)
  D3DDISPLAYMODE bestDisplayMode;
  bestDisplayMode.Format = D3DFMT_X8R8G8B8;
  bestDisplayMode.RefreshRate = D3DPRESENT_RATE_DEFAULT;
  bestDisplayMode.Width = _w;
  bestDisplayMode.Height = _h;
  memset(&_presentPars,0,sizeof(_presentPars));
#else
  // prepare presentation parameters based on _w, _h, _pixelSize
  // first select backbuffer format
  D3DDISPLAYMODE bestDisplayMode;
  // we may want a different render target
  int bestCost = INT_MAX;
  bestDisplayMode.Format = D3DFMT_X8R8G8B8;
  bestDisplayMode.RefreshRate = D3DPRESENT_RATE_DEFAULT;
  bestDisplayMode.Width = _w;
  bestDisplayMode.Height = _h;
  if (_windowed)
  {
    // Retrieve the current display mode
    _direct3D->GetAdapterDisplayMode(adapter,&bestDisplayMode);
  }
  else
  {
    for (int formatIndex = 0; formatIndex < lenof(supportedFormats); formatIndex++) {
      for (int mode = 0; mode < (int)_direct3D->GetAdapterModeCount(adapter, supportedFormats[formatIndex]._format); mode++)
      {
        D3DDISPLAYMODE displayMode;
        _direct3D->EnumAdapterModes(adapter, supportedFormats[formatIndex]._format, mode, &displayMode);

        int bpp = 16;
        int cost = 0;

		  switch (displayMode.Format)
        {
        case D3DFMT_X8R8G8B8: cost += 180; bpp = 32; break;
        case D3DFMT_R5G6B5: cost += 100; bpp = 16; break;
        case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: cost += 110; bpp = 16; break;

        case D3DFMT_A8R8G8B8: cost += 0; bpp = 32; break;
        case D3DFMT_R8G8B8: cost += 200; bpp = 24; break;

        default: cost += 500; break; // unknown format - last resort
        }
        cost +=
          (
          abs(_w*_h-displayMode.Width*displayMode.Height)*10 + 
          abs(_w-displayMode.Width)*10 + 
          abs(_h-displayMode.Height)*10 +
          abs(_pixelSize-bpp)*200
          );
        if (_refreshRate>0)
        {
          cost += abs(_refreshRate-displayMode.RefreshRate);
        }
        else
        {
          cost += abs(75-displayMode.RefreshRate); // higher refresh rate is always better
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
      bestDisplayMode.RefreshRate,
      SurfaceFormatNameDD9(bestDisplayMode.Format)
      );
  }
#endif
  _hdrPrecSupported = HDR8;
  for (int i=0; i<lenof(PreferredRTFormat); i++)
  {
    D3DFORMAT rt = PreferredRTFormat[i].format;

    // check if it can be used as a render target
    HRESULT ok = _direct3D->CheckDeviceFormat(
      adapter,_devType,bestDisplayMode.Format,
      D3DUSAGE_RENDERTARGET,D3DRTYPE_SURFACE,
      rt
      );

    if (ok==D3D_OK)
    {
      ok = _direct3D->CheckDeviceFormat(
        adapter,_devType,bestDisplayMode.Format,
        D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,
        D3DRTYPE_SURFACE,
        rt
        );
      if (ok==D3D_OK)
      {
        _hdrPrecSupported |= PreferredRTFormat[i].prec;
      }
    }
  }

  // select suitable depth-buffer formats
  D3DFORMAT depthFormat = D3DFMT_D24S8;

  D3DCAPS9 caps;
  memset(&caps,0,sizeof(caps));
  HRESULT ok = _direct3D->GetDeviceCaps(adapter,_devType,&caps);
  if (FAILED(ok))
  {
    ErrF("GetDeviceCaps failed, error %x",ok);
  }
  if (caps.StencilCaps)
  {
    LogF("Stencil caps %d",caps.StencilCaps);
  }

  // maxTextureSize used in FindFillrateControl to determine 3D resolution
  _caps._maxTextureSize = min(caps.MaxTextureWidth, caps.MaxTextureHeight);
  _hdrPrecWanted = ValidateHDRPrecision(_hdrPrecWanted);

  //D3DFORMAT bestRTFormat=D3DFMT_A8R8G8B8;
  D3DFORMAT bestRTFormat=D3DFMT_X8R8G8B8;


  //bestRTFormat = D3DFMT_A2B10G10R10;

  // if 10b are supported, use 10b
  bestRTFormat = D3DFMT_X8R8G8B8;
  // try using user's preferred choice
  switch (_hdrPrecWanted)
  {
    case HDR32:
      bestRTFormat = SelectRTFormat(adapter, bestDisplayMode.Format, 128);
      if (bestRTFormat!=D3DFMT_X8R8G8B8) break;
    // may fall-though
    case HDR16:
      bestRTFormat = SelectRTFormat(adapter, bestDisplayMode.Format, 64);
      if (bestRTFormat!=D3DFMT_X8R8G8B8) break;
    // may fall-though
    default:
      bestRTFormat = SelectRTFormat(adapter, bestDisplayMode.Format, 0);
  }

  _caps._canWriteSRGB = false;
#if 1 //!PS_BLEND
  // first of all we need SRGB when doing final conversion into the backbuffer
  ok = _direct3D->CheckDeviceFormat(
    adapter,_devType,bestDisplayMode.Format,
    D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_SRGBWRITE,
    D3DRTYPE_SURFACE,
    bestDisplayMode.Format
    );
  if (ok==D3D_OK)
  {
    // when using 8b RT, we need SRGB when rendering to it as well
    if (bestRTFormat==D3DFMT_X8R8G8B8 || bestRTFormat==D3DFMT_A8R8G8B8)
    {
      ok = _direct3D->CheckDeviceFormat(
        adapter,_devType,bestRTFormat,
        D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_SRGBWRITE,
        D3DRTYPE_SURFACE,
        bestDisplayMode.Format
        );
    }
    if (ok==D3D_OK)
    {
      _caps._canWriteSRGB = true;
      LogF(
        "SRGB write support available for format %s and backbuffer %s",
        SurfaceFormatNameDD9(bestRTFormat),
        SurfaceFormatNameDD9(bestDisplayMode.Format)
        );

    }
  }
#endif

#ifndef _X360SHADERGEN
  ok = _direct3D->CheckDeviceFormat(
    adapter,_devType,bestDisplayMode.Format,
    D3DUSAGE_DEPTHSTENCIL,D3DRTYPE_SURFACE,
    depthFormat
    );
  if (ok!=D3D_OK && _devType!=D3DDEVTYPE_NULLREF)
  {
    ErrorMessage(
      "Display adapter does not support format %s for %s.",
      SurfaceFormatNameDD9(depthFormat),
      SurfaceFormatNameDD9(bestDisplayMode.Format)
      );
  }
  else
  {
    ok = _direct3D->CheckDepthStencilMatch(
      adapter,_devType,
      bestDisplayMode.Format,bestRTFormat,
      depthFormat
      );
    if (ok!=D3D_OK && _devType!=D3DDEVTYPE_NULLREF)
    {
      ErrorMessage(
        "Display adapter does not support format %s with %s/%s.",
        SurfaceFormatNameDD9(depthFormat),
        SurfaceFormatNameDD9(bestDisplayMode.Format),
        SurfaceFormatNameDD9(bestRTFormat)
        );
    }
  }
#endif
  _caps._hasStencilBuffer = true;
  _caps._canTwoSidedStencil = (caps.StencilCaps&D3DSTENCILCAPS_TWOSIDED)!=0;

  if (_windowed)
  {
    _presentPars.BackBufferWidth = _w;
    _presentPars.BackBufferHeight = _h;
    _presentPars.FullScreen_RefreshRateInHz = 0;
    _refreshRate = 0;
  }
  else
  {
    _presentPars.BackBufferWidth = bestDisplayMode.Width;
    _presentPars.BackBufferHeight = bestDisplayMode.Height;
    _presentPars.FullScreen_RefreshRateInHz = bestDisplayMode.RefreshRate;
    _w = bestDisplayMode.Width;
    _h = bestDisplayMode.Height;
    _refreshRate = bestDisplayMode.RefreshRate;
  }

  FindFillrateControl(_wRTPreferred,_hRTPreferred);
  _wRT = _wRTRounded;
  _hRT = _hRTRounded;

  // Get the multisample levels available on the current HW
  DWORD multisampleLevels = 0;
  {
    // Check the RT format
    if (_direct3D->CheckDeviceMultiSampleType(adapter, _devType, bestRTFormat, _windowed, D3DMULTISAMPLE_NONMASKABLE, &multisampleLevels) != D3D_OK)
    {
      multisampleLevels=0;
    }

    // Reduce the multisampleLevels by depth format
    DWORD multisampleLevelsDepth = 0;
    if (_direct3D->CheckDeviceMultiSampleType(adapter, _devType, depthFormat, _windowed, D3DMULTISAMPLE_NONMASKABLE, &multisampleLevelsDepth) != D3D_OK)
    {
      multisampleLevelsDepth = 0;
    }
    if (multisampleLevels > multisampleLevelsDepth) multisampleLevels = multisampleLevelsDepth;

    // select the best format for SSSM

    // 16b fixed point is not bad
    // 32b floating point is better

#if 0
    // we do not require any alpha blending or alpha testing, however filtering is needed, and multi-sampling is plus
    _smFormat = D3DFMT_UNKNOWN;
    static const D3DFORMAT sssmFormats[]={
      D3DFMT_G32R32F,
      D3DFMT_G16R16, // TODO: prefer this format, better performance?
      D3DFMT_G16R16F,
    };
    for (int i=0; i<lenof(sssmFormats); i++)
    {
      if (_direct3D->CheckDeviceFormat(
        adapter,_devType,bestDisplayMode.Format,
        D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_FILTER,D3DRTYPE_TEXTURE,sssmFormats[i]
      )==D3D_OK)
      {
        _smFormat = sssmFormats[i];
        break;
      }
    }
    //_smFormat = D3DFMT_G32R32F;
#else
    // shadow map format
    _smFormat = D3DFMT_R32F;
#endif

    // TODO: try multisampling (and resolving) DF24 / INTZ
//     // multisampling will be used when rendering into the "readable depth buffer format" only
//     // SSSM is always constructed with no FSAA
//     // Reduce the multisampleLevels by readable depth buffer format
//     DWORD multisampleLevelsDOF = 0;
//     if (_direct3D->CheckDeviceMultiSampleType(adapter, _devType, D3DFMT_R32F, _windowed, D3DMULTISAMPLE_NONMASKABLE, &multisampleLevelsDOF) != D3D_OK)
//     {
//       multisampleLevelsDOF = 0;
//     }
//     if (multisampleLevels > multisampleLevelsDOF) multisampleLevels = multisampleLevelsDOF;
  }

  _rtFormat = bestRTFormat;
  _rtMultiSampleType = D3DMULTISAMPLE_NONE;
  _rtMultiSampleQuality = 0;
  _maxFSAA = multisampleLevels;
  
  _useAToC = false;
  _aToCSupported = AToC_None;
  if (multisampleLevels>0 && _fsAAWanted>0)
  {
    _rtMultiSampleType = D3DMULTISAMPLE_NONMASKABLE;
    _rtMultiSampleQuality = _fsAAWanted-1;
    if (_rtMultiSampleQuality>multisampleLevels-1) _rtMultiSampleQuality = multisampleLevels-1;

    // check what AToC do we support
    // vendor ATI: all ATI PS 3.0 cards support AToC
    
    D3DADAPTER_IDENTIFIER9 id;
    _direct3D->GetAdapterIdentifier(adapter,0,&id);
    if (id.VendorId==0x1002) // ATI
    {
      _aToCSupported = AToC_ATI;
    }
    else if (_direct3D->CheckDeviceFormat(
      D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0,D3DRTYPE_SURFACE, (D3DFORMAT)MAKEFOURCC('A', 'T', 'O', 'C')) == S_OK
    )
    {
      _aToCSupported = AToC_nVidia;
    }

    // if supported, use it
    if (_aToCSupported!=AToC_None)
    {
      _useAToC = _AToCMask != 0;
    }
  }
  
  _downsampleFormat = D3DFMT_X8R8G8B8;
  // first try using the same format as the main RT
  HRESULT err = _direct3D->CheckDeviceFormat(
    adapter,_devType,bestDisplayMode.Format,
    D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_FILTER,D3DRTYPE_TEXTURE,_rtFormat
    );
  if (err==D3D_OK)
  {
    _downsampleFormat = _rtFormat;
  }
  else
  {
    // RT format not suitable - try to find something else
    // this most likely means RT format is not 8b, as that would be probably suitable
    static const D3DFORMAT formats8b[]= {
#if !defined(_X360SHADERGEN)
      D3DFMT_R8G8B8,
#endif
      D3DFMT_X8R8G8B8,D3DFMT_A8R8G8B8,
      D3DFMT_R5G6B5,D3DFMT_X1R5G5B5,D3DFMT_A1R5G5B5,
      D3DFMT_A4R4G4B4,D3DFMT_X4R4G4B4,
      D3DFMT_A8B8G8R8,D3DFMT_X8B8G8R8
    };

    bool is8b = false;
    for (int i=0; i<lenof(formats8b); i++)
    {
      if (_rtFormat==formats8b[i]) {is8b=true;break;}
    }
    if (!is8b)
    {
      // we want a high-resolution format for down-sampling
      static const D3DFORMAT downsampleFormats[]=
      {
        // we need to be able to keep at least avg and max
        // for this we need RG
        D3DFMT_G16R16F,
        D3DFMT_A16B16G16R16F,
        D3DFMT_G32R32F,
        D3DFMT_A32B32G32R32F,
      };
      for (int i=0; i<lenof(downsampleFormats); i++)
      {
        // we do not require any alpha blending or alpha testing
        HRESULT err = _direct3D->CheckDeviceFormat(
          adapter,_devType,bestDisplayMode.Format,
          D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_FILTER,D3DRTYPE_TEXTURE,downsampleFormats[i]
        );
        if (err==D3D_OK)
        {
          _downsampleFormat = downsampleFormats[i];
          break;
        }
      }
    }
  }
  LogF("Down-sample surface format %s",SurfaceFormatNameDD9(_downsampleFormat));

  _presentPars.BackBufferFormat = bestDisplayMode.Format;

  // using triple buffering has little sense when using render target
  _presentPars.BackBufferCount = 1;
  _presentPars.MultiSampleType = D3DMULTISAMPLE_NONE;
  _presentPars.MultiSampleQuality = 0;
  _presentPars.SwapEffect = D3DSWAPEFFECT_DISCARD;
  _presentPars.hDeviceWindow = _hwndApp;
  _presentPars.Windowed = _windowed;
  _presentPars.EnableAutoDepthStencil = true;
  _presentPars.AutoDepthStencilFormat = depthFormat;
  _presentPars.Flags = 0;
  if (_presentPars.EnableAutoDepthStencil)
  {
    _presentPars.Flags |= D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL;
  }
  _presentPars.PresentationInterval = _vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
}

struct DeviceLevelInfo
{
  int level;
  int deviceId;
  //int subSysId;
  const char *name1;
  //const char *name2;
  /**
  Adapter levels:
  0 - very bad, not supported
  0x100 - low end (GeForce 5900, ATI 9700)
  0x200 - middle (GeForce 6800, ATI X800)
  0x300 - high (GeForce 7800, ATI X1600)
  */
};
static const DeviceLevelInfo NVDevices[]={

  // Device ID, GPU Name
  { 0x000, 0x00F1, "GeForce 6600 GT" },
  { 0x000, 0x00F2, "GeForce 6600" },
  { 0x000, 0x00F3, "GeForce 6200" },
  { 0x000, 0x00F4, "GeForce 6600 LE" },
  { 0x100, 0x00F5, "GeForce 7800 GS" },
  { 0x100, 0x00F6, "GeForce 6800 GS/XT" },
  { 0x100, 0x00F8, "Quadro FX 3400/4400" },
  { 0x000, 0x00F9, "GeForce 6800 Series GPU" },
  { 0x100, 0x02E0, "GeForce 7600 GT" },
  { 0x100, 0x02E1, "GeForce 7600 GS" },
  { 0x100, 0x02E2, "GeForce 7300 GT" },
  { 0x100, 0x02E3, "GeForce 7900 GS" },
  { 0x100, 0x02E4, "GeForce 7950 GT" },
  { 0x100, 0x0090, "GeForce 7800 GTX" },
  { 0x100, 0x0091, "GeForce 7800 GTX " },
  { 0x100, 0x0092, "GeForce 7800 GT" },
  { 0x100, 0x0093, "GeForce 7800 GS " },
  { 0x100, 0x0095, "GeForce 7800 SLI" },
  { 0x100, 0x009D, "Quadro FX 4500" },
  { 0x100, 0x0290, "GeForce 7900 GTX" },
  { 0x100, 0x0291, "GeForce 7900 GT/GTO" },
  { 0x100, 0x0292, "GeForce 7900 GS " },
  { 0x100, 0x0293, "GeForce 7950 GX2" },
  { 0x100, 0x0294, "GeForce 7950 GX2 " },
  { 0x100, 0x0295, "GeForce 7950 GT " },
  { 0x100, 0x029C, "Quadro FX 5500" },
  { 0x100, 0x029D, "Quadro FX 3500" },
  { 0x100, 0x029E, "Quadro FX 1500" },
  { 0x100, 0x029F, "Quadro FX 4500 X2" },
  { 0x100, 0x01D0, "GeForce 7350 LE" },
  { 0x100, 0x01D1, "GeForce 7300 LE" },
  { 0x100, 0x01D3, "GeForce 7300 SE/7200 GS" },
  { 0x100, 0x01DB, "Quadro NVS 120M" },
  { 0x100, 0x01DD, "GeForce 7500 LE" },
  { 0x100, 0x01DE, "Quadro FX 350" },
  { 0x100, 0x01DF, "GeForce 7300 GS" },
  { 0x100, 0x0390, "GeForce 7650 GS" },
  { 0x100, 0x0391, "GeForce 7600 GT " },
  { 0x100, 0x0392, "GeForce 7600 GS " },
  { 0x100, 0x0393, "GeForce 7300 GT " },
  { 0x100, 0x0394, "GeForce 7600 LE" },
  { 0x100, 0x0395, "GeForce 7300 GT  " },
  { 0x100, 0x039E, "Quadro FX 560" },
  { 0x200, 0x0191, "GeForce 8800 GTX" },
  { 0x200, 0x0193, "GeForce 8800 GTS" },
  { 0x200, 0x0194, "GeForce 8800 Ultra" },
  { 0x200, 0x0197, "Tesla C870" },
  { 0x200, 0x019D, "Quadro FX 5600" },
  { 0x200, 0x019E, "Quadro FX 4600" },
  { 0x100, 0x0400, "GeForce 8600 GTS" },
  { 0x200, 0x0401, "GeForce 8600 GT" },
  { 0x200, 0x0402, "GeForce 8600 GT " },
  { 0x200, 0x0403, "GeForce 8600GS" },
  { 0x100, 0x0404, "GeForce 8400 GS" },
  { 0x100, 0x0406, "GeForce 8300 GS" },
  { 0x200, 0x040A, "Quadro FX 370" },
  { 0x200, 0x040E, "Quadro FX 570" },
  { 0x200, 0x040F, "Quadro FX 1700" },
  { 0x100, 0x0420, "GeForce 8400 SE" },
  { 0x100, 0x0421, "GeForce 8500 GT" },
  { 0x100, 0x0422, "GeForce 8400 GS " },
  { 0x100, 0x0423, "GeForce 8300 GS " },
  { 0x100, 0x0424, "GeForce 8400 GS  " },
  { 0x200, 0x042C, "GeForce 9400 GT" },
  { 0x200, 0x042F, "Quadro NVS 290" },
  { 0x200, 0x0600, "GeForce 8800 GTS 512" },
  { 0x300, 0x0601, "GeForce 9800 GT" },
  { 0x200, 0x0602, "GeForce 8800 GT" },
  { 0x300, 0x0604, "GeForce 9800 GX2" },
  { 0x300, 0x0605, "GeForce 9800 GT " },
  { 0x200, 0x0606, "GeForce 8800 GS" },
  { 0x300, 0x0610, "GeForce 9600 GSO" },
  { 0x200, 0x0611, "GeForce 8800 GT " },
  { 0x300, 0x0612, "GeForce 9800 GTX/9800 GTX+" },
  { 0x300, 0x0613, "GeForce 9800 GTX+" },
  { 0x300, 0x0614, "GeForce 9800 GT  " },
  { 0x300, 0x061A, "Quadro FX 3700" },
  { 0x200, 0x0622, "GeForce 9600 GT" },
  { 0x200, 0x0623, "GeForce 9600 GS" },
  { 0x200, 0x0640, "GeForce 9500 GT" },
  { 0x200, 0x0641, "GeForce 9400 GT " },
  { 0x200, 0x0643, "GeForce 9500 GT " },
  { 0x200, 0x0644, "GeForce 9500 GS" },
  { 0x200, 0x0645, "GeForce 9500 GS " },
  { 0x200, 0x06E0, "GeForce 9300 GE" },
  { 0x200, 0x06E1, "GeForce 9300 GS" },
  { 0x100, 0x06E2, "GeForce 8400" },
  { 0x100, 0x06E3, "GeForce 8400 SE " },
  { 0x100, 0x06E4, "GeForce 8400 GS   " },
  { 0x200, 0x06F9, "Quadro FX 370 LP" },
  { 0x300, 0x05E1, "GeForce GTX 280" },
  { 0x300, 0x05E2, "GeForce GTX 260" },
  { 0x300, 0x05FD, "Quadro FX 5800" },
  { 0x300, 0x05FE, "Quadro FX 4800" },
  { 0x100, 0x0040, "GeForce 6800 Ultra" },
  { 0x000, 0x0041, "GeForce 6800" },
  { 0x000, 0x0042, "GeForce 6800 LE" },
  { 0x000, 0x0043, "GeForce 6800 XE" },
  { 0x100, 0x0044, "GeForce 6800 XT" },
  { 0x100, 0x0045, "GeForce 6800 GT" },
  { 0x100, 0x0047, "GeForce 6800 GS" },
  { 0x100, 0x0048, "GeForce 6800 XT " },
  { 0x100, 0x004E, "Quadro FX 4000" },
  { 0x100, 0x00C0, "GeForce 6800 GS " },
  { 0x100, 0x00C1, "GeForce 6800 " },
  { 0x100, 0x00C2, "GeForce 6800 LE " },
  { 0x100, 0x00C3, "GeForce 6800 XT  " },
  { 0x100, 0x00CD, "Quadro FX 3450/4000 SDI" },
  { 0x100, 0x00CE, "Quadro FX 1400" },
  { 0x000, 0x0140, "GeForce 6600 GT " },
  { 0x000, 0x0141, "GeForce 6600 " },
  { 0x000, 0x0142, "GeForce 6600 LE " },
  { 0x000, 0x0143, "GeForce 6600 VE" },
  { 0x000, 0x0145, "GeForce 6610 XL" },
  { 0x000, 0x0147, "GeForce 6700 XL" },
  { 0x000, 0x014A, "Quadro NVS 440" },
  { 0x000, 0x014C, "Quadro FX 540M" },
  { 0x000, 0x014D, "Quadro FX 550" },
  { 0x000, 0x014E, "Quadro FX 540" },
  { 0x000, 0x014F, "GeForce 6200 " },
  { 0x000, 0x0160, "GeForce 6500" },
  { 0x000, 0x0161, "GeForce 6200 TurboCache(TM)" },
  { 0x000, 0x0162, "GeForce 6200SE TurboCache(TM)" },
  { 0x000, 0x0163, "GeForce 6200 LE" },
  { 0x000, 0x0165, "Quadro NVS 285" },
  { 0x000, 0x0169, "GeForce 6250" },
  { 0x000, 0x016A, "GeForce 7100 GS" },
  { 0x000, 0x0221, "GeForce 6200  " },
  { 0x000, 0x0222, "GeForce 6200 A-LE" },
  { 0x100, 0x0046, "GeForce 6800 GT " },
  { 0x100, 0x0211, "GeForce 6800  " },
  { 0x000, 0x0212, "GeForce 6800 LE  " },
  { 0x100, 0x0215, "GeForce 6800 GT  " },
  { 0x100, 0x0218, "GeForce 6800 XT   " },
  { 0x000, 0x0240, "GeForce 6150" },
  { 0x000, 0x0241, "GeForce 6150 LE" },
  { 0x000, 0x0242, "GeForce 6100" },
  { 0x000, 0x0245, "Quadro NVS 210S / NVIDIA GeForce 6150LE" },
  { 0x000, 0x03D0, "GeForce 6150SE nForce 430" },
  { 0x000, 0x03D1, "GeForce 6100 nForce 405" },
  { 0x000, 0x03D2, "GeForce 6100 nForce 400" },
  { 0x000, 0x03D5, "GeForce 6100 nForce 420" },
  { 0x100, 0x053A, "GeForce 7050 PV / NVIDIA nForce 630a" },
  { 0x100, 0x053B, "GeForce 7050 PV / NVIDIA nForce 630a " },
  { 0x100, 0x053E, "GeForce 7025 / NVIDIA nForce 630a" },
  { 0x100, 0x07E0, "GeForce 7150 / NVIDIA nForce 630i" },
  { 0x100, 0x07E1, "GeForce 7100 / NVIDIA nForce 630i" },
  { 0x100, 0x07E2, "GeForce 7050 / NVIDIA nForce 630i" },
  { 0x100, 0x07E3, "GeForce 7050 / NVIDIA nForce 610i" },
  { 0x100, 0x07E5, "GeForce 7050 / NVIDIA nForce 620i" },
  { 0x100, 0x0848, "GeForce 8300" },
  { 0x100, 0x0849, "GeForce 8200" },
  { 0x100, 0x084A, "nForce 730a" },
  { 0x100, 0x084B, "GeForce 8200 " },
  { 0x200, 0x084C, "nForce 780a SLI" },
  { 0x200, 0x084D, "nForce 750a SLI" },
  { 0x100, 0x084F, "GeForce 8100 / nForce 720a" },
  // end of list
};

#define ATI_DEVICE_ID(level, friendly, code, deviceId) {level,0x##deviceId,friendly},

static const DeviceLevelInfo ATIDevices[]={
  // Retail name, ASIC, Device ID
  ATI_DEVICE_ID( 0x200, " AMD 760G ",RS780, 9616)
  ATI_DEVICE_ID( 0x200, " AMD 780E ",RS780 , 9615)
  ATI_DEVICE_ID( 0x200, " AMD FireStream 9170 ",RV670, 9519)
  ATI_DEVICE_ID( 0x200, " AMD FireStream 9250 ",RV770, 9452)
  ATI_DEVICE_ID( 0x200, " AMD FireStream 9270 ",RV770, 9450)
  ATI_DEVICE_ID( 0x200, " ATI FireStream 2U ",R580  , 724E )
  ATI_DEVICE_ID( 0x200, " ATI FireStream 2U Secondary  ",R580 , 726E )
  ATI_DEVICE_ID( 0x100, " ATI FireGL T2  ",RV360, 4154 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL T2 Secondary ",RV360, 4174 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V3100 ",RV370, 5B64 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V3100 Secondary  ",RV370 ,  5B74 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V3200 ",RV380  ,  3E54 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V3200 Secondary ",RV380 , 3E74 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V3300 ",RV515 , 7152 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V3300 Secondary ",RV515  , 7172 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V3350 ",RV515 , 7153 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V3350 Secondary  ",RV515 , 7173 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V3400 ",RV530 , 71D2 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V3400 Secondary ",RV530 , 71F2 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V3600 ",RV630 , 958D )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V5000 ",RV410 , 5E48 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V5000 Secondary ",RV410 , 5E68 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V5100 ",R423  , 5551 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V5100  Secondary ",R423  , 5571 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V5200 ",RV530,  71DA )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V5200 Secondary ",RV530 , 71FA )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V5300  ",R520,  7105 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V5300 Secondary  ",R520, 7125 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V5600  ",RV630,  958C )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V7100  ",R423 ,5550 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V7100 Secondary  ",R423  ,5570 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V7200  ",R480  ,5D50 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V7200  ",R520  ,7104 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V7200 Secondary  ",R480  ,5D70 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V7200 Secondary  ",R520 ,7124 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V7300  ",R520 ,710E )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V7300 Secondary  ",R520  ,712E )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V7350  ",R520  ,710F )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V7350 Secondary  ",R520  ,712F )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V7600  ",R600  ,940F )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V7700  ",RV670,  9511 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V8600  ",R600  ,940B )
  ATI_DEVICE_ID( 0x100, " ATI FireGL V8650  ",R600 ,940A )
  ATI_DEVICE_ID( 0x100, " ATI FireGL X1  ",R300 ,  4E47 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL X1 Secondary ",R300 ,4E67 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL X2  ",R350 ,4E4B )
  ATI_DEVICE_ID( 0x100, " ATI FireGL X2 Secondary ",R350 ,4E6B )
  ATI_DEVICE_ID( 0x100, " ATI FireGL X3 ",R420 ,4A4D )
  ATI_DEVICE_ID( 0x100, " ATI FireGL X3 Secondary ",R420,  4A6D )
  ATI_DEVICE_ID( 0x100, " ATI FireGL Z1  ",R420 ,4147 )
  ATI_DEVICE_ID( 0x100, " ATI FireGL Z1 Secondary ",R300, 4167 )
  ATI_DEVICE_ID( 0x100, " ATI FireMV 2200 ",RV370  ,5B65 )
  ATI_DEVICE_ID( 0x100, " ATI FireMV 2200 Secondary ",RV370, 5B75 )
  ATI_DEVICE_ID( 0x100, " ATI FireMV 2250 ",RV516 ,719B )
  ATI_DEVICE_ID( 0x100, " ATI FireMV 2250 Secondary ",RV516,  71BB )
  ATI_DEVICE_ID( 0x100, " ATI FireMV 2400 ",M24 ,3151 )
  ATI_DEVICE_ID( 0x100, " ATI FireMV 2400 Secondary ",M24 , 3171 )
  ATI_DEVICE_ID( 0x100, " ATI FirePro 2260  ",RV620  ,95CE )
  ATI_DEVICE_ID( 0x100, " ATI FirePro 2260  ",RV620  ,95CF )
  ATI_DEVICE_ID( 0x100, " ATI FirePro 2450  ",RV620  ,95CD )
  ATI_DEVICE_ID( 0x100, " ATI FirePRO V3700 ",RV620,  95CC )
  ATI_DEVICE_ID( 0x100, " ATI FirePro V3750  ",RV730  ,949F )
  ATI_DEVICE_ID( 0x100, " ATI FirePro V5700 ",RV730  ,949E )
  ATI_DEVICE_ID( 0x100, " ATI FirePro V7750 ",RV730  ,949C )
  ATI_DEVICE_ID( 0x100, " ATI FirePro V7770 ",RV770 ,9446 )
  ATI_DEVICE_ID( 0x100, " ATI FirePro V8700 ",RV770 ,9456 )
  ATI_DEVICE_ID( 0x100, " ATI FirePro V8700 Duo ",R700  ,9447 )
  ATI_DEVICE_ID( 0x100, " ATI FirePro V8750 ",RV770 ,9444 )
  ATI_DEVICE_ID( 0x100, " ATI MOBILITY FIRE GL  ",M10 ,4E54 )
  ATI_DEVICE_ID( 0x100, " ATI MOBILITY FireGL Graphics Processor ",M86 , 959B )
  ATI_DEVICE_ID( 0x100, " ATI MOBILITY FireGL Graphics Processor ",M76 , 958F )
  ATI_DEVICE_ID( 0x100, " ATI MOBILITY FireGL V3100 ",M22  ,5464 )
  ATI_DEVICE_ID( 0x100, " ATI MOBILITY FireGL V3200 ",M24  ,3154 )
  ATI_DEVICE_ID( 0x100, " ATI MOBILITY FireGL V5000 ",M26 ,564A )
  ATI_DEVICE_ID( 0x100, " ATI MOBILITY FireGL V5000 ",M26 ,564B )
  ATI_DEVICE_ID( 0x100, " ATI MOBILITY FireGL V5100 ",M28  ,5D49 )
  ATI_DEVICE_ID( 0x100, " ATI MOBILITY FireGL V5200 ",M56  ,71C4 )
  ATI_DEVICE_ID( 0x100, " ATI MOBILITY FireGL V5250 ",M66  ,71D4 )
  ATI_DEVICE_ID( 0x100, " ATI MOBILITY FireGL V5700 ",M86  ,9595 )
  ATI_DEVICE_ID( 0x100, " ATI MOBILITY FireGL V7100 ",M58  ,7106 )
  ATI_DEVICE_ID( 0x100, " ATI MOBILITY FireGL V7200 ",M58  ,7103 )
  ATI_DEVICE_ID( 0x100, " ATI MOBILITY  RADEON 9500  ",M11, 4E52 )
  ATI_DEVICE_ID( 0x100, " ATI MOBILITY  RADEON 9550  ",M12, 4E56 )
  ATI_DEVICE_ID( 0x100, " ATI MOBILITY  RADEON 9600/9700 Series ",M10, 4E50 )
  ATI_DEVICE_ID( 0x100, " ATI MOBILITY  RADEON 9800  ",M18, 4A4E )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 2300 ",M71, 7210 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 2300 ",M71, 7211 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 2400 ",M72, 94C9 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 2400 XT ",M74, 94C8 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 2600 ",M76, 9581 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 2600 XT ",M76, 9583 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3400 Series  ",M82, 95C4 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3400 Series  ",M82, 95C4 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3400 Series  ",M82, 95C4 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3400 Series  ",M82, 95C4 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3400 Series  ",M82, 95C4 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3400 Series ",M82, 95C4 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3430 ",M82, 95C2 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3450 ",M82, 95C4 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3650 ",M86, 9591 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3650 ",M86, 9591 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3650 ",M86, 9591 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3650 ",M86, 9591 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3650 ",M86 ,9591 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3670 ",M86, 9593 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3850 ",M88 , 9504 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3850 X2 ",M88 ,9506 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3870 ",M88  ,9508 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 3870 X2 ",M88  ,9509 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4300 Series  ",M92  ,9552 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4300 Series  ",M92  ,9552 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4300 Series  ",M92  ,9552 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4300 Series  ",M92  ,9552 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4500 Series  ",M92 ,9553 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4500 Series  ",M92  ,9553 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4500 Series  ",M92 ,9553 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4500 Series  ",M92  ,9553 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4650 ",M96 , 9480 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4650 ",M96  , 9480 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4650 ",M96  , 9480 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4650 ",M96 , 9480 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4670 ",M96, 9488 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4670 ",M96 , 9488 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4670 ",M96 ,9488 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4670 ",M96  ,9488 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4850 ",M98 , 944A )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4850 X2 ",M98 , 944B )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON HD 4870 ",M98 ,945A )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X1300 ",M52 , 7149 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X1300 ",M52 , 714B )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X1300 ",M52 , 714C )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X1300 ",M52 ,714A )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X1350 ",M62 ,718B )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X1350 ",M62 ,718C )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X1350 ",M62 ,7196 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X1400 ",M54 ,7145 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X1450 ",M64 ,7186 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X1450 ",M64 ,718D )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X1600 ",M56 ,71C5 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X1700 ",M66 ,71D5 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X1700 ",M66 ,71DE )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X1700 XT ",M66 ,71D6 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X1800 ",M58  ,7102 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X1800 XT ",M58  ,7101 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X1900 ",M68 ,  7284 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X2300 ",M64, 718A )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X2300 ",M64, 7188 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X300  ",M22, 5461 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X300  ",M22 , 5460 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X300  ",M22 , 3152 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X600  ",M24  , 3150 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X600 SE ",M24 ,5462 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X700  ",M26 ,5652 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X700  ",M26 ,5653 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X700 Secondary  ",M26, 5673 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X700 XL ",M26, 564F )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X800  ",M28, 5D4A )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY  RADEON X800  XT ",M28, 5D48 )
  ATI_DEVICE_ID( 0x200, " ATI MOBILITY/ ATI RADEON X700  ",RV410, 5653 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON 2100 ",RS740, 796E )
  ATI_DEVICE_ID( 0x200, " ATI RADEON 3100 Graphics  ",RS780 ,9611 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON 3100 Graphics  ",RS780 ,9613 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON 3100 Graphics  ",RS780 ,9613 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON 3100 Graphics  ",RS780 ,9613 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON 9550/X1050 Series ",RV350 , 4153 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON 9550/X1050 Series Secondary ",RV350, 4173 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9600 Series ",RV350,  4150 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9600 Series ",RV350 , 4E51 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9600 Series ",RV350 , 4151 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9600 Series ",RV351, 4155 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9600 Series ",RV360 , 4152 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9600 Series Secondary ",RV350, 4E71 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9600 Series Secondary ",RV350, 4171 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9600 Series Secondary ",RV350 , 4170 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9600 Series Secondary ",RV351 , 4175 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9600 Series Secondary ",RV360 , 4172 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON E2400  ",M72 ,94CB )
  ATI_DEVICE_ID( 0x000, " ATI RADEON Graphics Processor  ",RV730 , 9487 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON Graphics Processor  ",RV730 , 948F )
  ATI_DEVICE_ID( 0x100, " ATI RADEON Graphics Processor  ",RV710 , 9541 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON Graphics Processor  ",RV710 , 954E )
  ATI_DEVICE_ID( 0x100, " ATI RADEON Graphics Processor  ",M93 , 9555 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON Graphics Processor  ",RV740 , 94B1 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON Graphics Processor  ",RV740 , 94B3 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON Graphics Processor  ",M97 , 94A0 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON Graphics Processor  ",M97  ,94A1 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 2350 ",RV610, 94C7 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 2400 ",RV610 , 94CC )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 2400 LE  ",RV610 , 94C5 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 2400 PRO ",RV610, 94C3 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 2400 PRO AGP  ",RV610, 94C4 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 2400 XT  ",RV610, 94C1 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 2600 LE  ",RV630 , 958E )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 2600 Pro ",RV630 , 9589 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 2600 Pro AGP  ",RV630,  9587 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 2600 XT  ",RV630,  9588 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 2600 XT AGP ",RV630,  9586 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 2900 GT  ",R600 , 9405 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 2900 PRO ",R600 ,9403 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 2900 XT  ",R600 ,9400 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 2900 XT  ",R600  ,9401 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 2900 XT  ",R600  ,9402 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3200 Graphics ",RS780, 9610 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3200 Graphics ",RS780 ,9612 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3200 Graphics ",RS780 ,9612 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3200 Graphics ",RS780 ,9612 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3300 Graphics ",RS780 ,9614 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3430 ",RV620,  95C7 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3450 ",RV620, 95C5 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3450 ",RV620 , 95C6 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3450 ",RV620, 95C9 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3470 ",RV620 , 95C0 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3600 Series ",RV635  , 9590 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3600 Series ",RV635 , 9597 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3600 Series ",RV635, 9598 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3600 Series ",RV635 , 9599 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3650 AGP ",RV635 , 9596 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3830 ",RV670 , 9507 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3850 ",RV670 , 9505 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3850 AGP ",RV670 , 9515 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3850 X2  ",R680  ,9513 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3870 ",RV670,  9501 )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 3870 X2  ",R680 , 950F )
  ATI_DEVICE_ID( 0x300, " ATI RADEON HD 4350 ",RV710 , 954F )
  ATI_DEVICE_ID( 0x300, " ATI RADEON HD 4550 ",RV710 , 9540 )
  ATI_DEVICE_ID( 0x300, " ATI RADEON HD 4650 ",RV730 , 9498 )
  ATI_DEVICE_ID( 0x300, " ATI RADEON HD 4670 ",RV730 , 9490 )
  ATI_DEVICE_ID( 0x300, " ATI RADEON HD 4800 Series ",RV770, 9440 )
  ATI_DEVICE_ID( 0x300, " ATI RADEON HD 4800 Series ",RV770 ,  9442 )
  ATI_DEVICE_ID( 0x300, " ATI RADEON HD 4800 Series ",RV770 , 944C )
  ATI_DEVICE_ID( 0x300, " ATI RADEON HD 4850 X2  ",R700  ,9443 )
  ATI_DEVICE_ID( 0x300, " ATI RADEON HD 4870 X2  ",R700  ,9441 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1200 Series ",RS690 , 791E )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1200 Series ",RS690 , 791F )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1950 GT ",RV570 , 7288 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1950 GT Secondary  ",RV570,  72A8 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 GT ",R430 ,554E )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 GT Secondary ",R430, 556E )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 XL ",R430 ,554D )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 XL Secondary ",R430, 556D )
  ATI_DEVICE_ID( 0x200, " ATI RADEON HD 2600 XT Gemini ",M76 ,958B )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X850 Consumer  ",R481,  4B48 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X850 Consumer Secondary  ",R481, 4B68 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X850 PRO ",R481  ,4B4B )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X850 PRO Secondary  ",R481, 4B6B )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X850 SE ",R481 , 4B4A )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X850 SE Secondary ",R481,  4B6A )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X850 XT ",R481 , 4B49 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X850 XT Platinum Edition ",R481,  4B4C )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X850 XT Platinum Edition Secondary  ",R481, 4B6C )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X850 XT Secondary ",R481 ,4B69 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON Xpress 1200 Series  ",RS600,  793F )
  ATI_DEVICE_ID( 0x000, " ATI RADEON Xpress 1200 Series  ",RS600 , 7941 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON Xpress 1200 Series  ",RS600 , 7942 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON Xpress Series  ",RC410 , 5A61 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON Xpress Series  ",RC410 , 5A63 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON Xpress Series  ",RC410 , 5A62 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON Xpress Series  ",RS400 , 5A41 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON Xpress Series  ",RS400 , 5A43 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON Xpress Series  ",RS400 , 5A42 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON Xpress Series  ",RS480 , 5954 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON Xpress Series  ",RS480 , 5854 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON Xpress Series  ",RS480 , 5955 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON Xpress Series  ",RS482 , 5974 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON Xpress Series  ",RS482 , 5874 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON Xpress Series  ",RS482 , 5975 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9500  ",R300 ,4144 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9500  ",R300 ,4149 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9500 PRO / 9700 ",R300, 4E45 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9500 PRO / 9700 Secondary ",R300, 4E65 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9500 Secondary ",R300, 4164 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9500 Secondary  ",R300, 4169 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9600 TX ",RV350 ,4E46 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9600 TX Secondary  ",RV350, 4E66 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9600TX  ",RV350 ,4146 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9600TX Secondary ",RV350, 4166 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9700 PRO ",RV350 ,4E44 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9700 PRO Secondary ",RV350, 4E64 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9800  ",R350, 4E49 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9800 PRO ",R350 , 4E48 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9800 PRO Secondary ",R350,  4E68 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9800 SE ",R350, 4148 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9800 SE Secondary  ",R350, 4168 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9800 Secondary  ",R350, 4E69 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9800 XT ",R360 ,4E4A )
  ATI_DEVICE_ID( 0x000, " ATI RADEON 9800 XT Secondary  ",R360 , 4E6A )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300 / X1550 Series ",RV515,  7146 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300 / X1550 Series Secondary ",RV515, 7166 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300 Series ",RV515,  714E )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300 Series ",RV515 , 715E )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300 Series ",RV515 , 714D )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300 Series ",RV535 , 71C3 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300 Series ",RV516, 718F )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300 Series Secondary  ",RV515, 716E )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300 Series Secondary  ",RV515 , 717E )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300 Series Secondary  ",RV515 , 716D )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300 Series Secondary  ",RV535 , 71E3 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300 Series Secondary  ",RV516, 71AF )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300/X1550 Series ",RV515, 7142 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300/X1550 Series ",RV516 , 7180 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300/X1550 Series ",RV516 , 7183 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300/X1550 Series ",RV516 , 7187 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300/X1550 Series Secondary ",RV515,  7162 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300/X1550 Series Secondary ",RV516 , 71A0 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300/X1550 Series Secondary ",RV516 , 71A3 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1300/X1550 Series Secondary ",RV516 , 71A7 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1550 64-bit ",RV505,  7147 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1550 64-bit ",RV505, 715F )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1550 64-bit ",RV516, 719F )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1550 64-bit Secondary  ",RV505 , 7167 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1550 64-bit Secondary  ",RV505 , 717F )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1550 64-bit Secondary  ",RV516 , 71BF )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1550 Series ",RV505 , 7143 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1550 Series ",RV516, 7193 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1550 Series Secondary  ",RV505, 7163 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1550 Series Secondary  ",RV516 , 71B3 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1600 Pro / ATI RADEON X1300 XT  ",RV530,  71CE )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1600 Pro / ATI RADEON X1300 XT Secondary  ",RV530,71EE )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1600 Series ",RV515,  7140 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1600 Series ",RV530 , 71C0 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1600 Series ",RV530 , 71C2 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1600 Series ",RV516 , 7181 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1600 Series ",RV530 , 71CD )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1600 Series Secondary  ",RV515, 7160 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1600 Series Secondary  ",RV530 , 71E2 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1600 Series Secondary  ",RV530 , 71E6 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1600 Series Secondary  ",RV516 , 71A1 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1600 Series Secondary  ",RV530 , 71ED )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1600 Series Secondary  ",RV530 , 71E0 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1650 Series ",RV530 , 71C6 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1650 Series ",RV535 , 71C1 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1650 Series ",RV560 , 7293 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1650 Series ",RV560 , 7291 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1650 Series ",RV535 , 71C7 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1650 Series Secondary  ",RV535 , 71E1 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1650 Series Secondary  ",RV560 , 72B3 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1650 Series Secondary  ",RV560 , 72B1 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1650 Series Secondary  ",RV535 , 71E7 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1800 Series ",R520  , 7100 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1800 Series ",R520  ,7108 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1800 Series ",R520  ,7109 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1800 Series ",R520  ,710A )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1800 Series ",R520  ,710B )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1800 Series ",R520  ,710C )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1800 Series Secondary  ",R520  ,7120 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1800 Series Secondary  ",R520  ,7128 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1800 Series Secondary  ",R520  ,7129 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1800 Series Secondary  ",R520  ,712A )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1800 Series Secondary  ",R520  ,712B )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X1800 Series Secondary  ",R520  ,712C )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series ",R580  ,7243 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series ",R580  ,7245 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series ",R580  ,7246 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series ",R580  ,7247 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series ",R580  ,7248 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series ",R580 ,7249 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series ",R580 ,724A )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series ",R580  ,724B )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series ",R580  ,724C )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series ",R580  ,724D )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series ",R580  ,724F )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series Secondary  ",R580  ,7263 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series Secondary  ",R580  ,7265 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series Secondary  ",R580  ,7266 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series Secondary  ",R580  ,7267 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series Secondary  ",R580  ,7268 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series Secondary  ",R580  ,7269 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series Secondary  ",R580  ,726A )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series Secondary  ",R580  ,726B )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series Secondary  ",R580  ,726C )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series Secondary  ",R580 ,726D )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1900 Series Secondary  ",R580  ,726F )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1950 Series ",R580 ,7240 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1950 Series ",R580 ,7244 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1950 Series ",RV570, 7280 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1950 Series Secondary  ",R580  ,7260 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1950 Series Secondary  ",R580  ,7264 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1950 Series Secondary  ",RV570 ,  72A0 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1950 XTX - Limited Edition  ",R580  ,7248 )
  ATI_DEVICE_ID( 0x100, " ATI RADEON X1950 XTX - Limited Edition Secondary  ",R580, 7268 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X300/X550/X1050 Series  ",RV370 , 5B60 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X300/X550/X1050 Series  ",RV370 , 5B63 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X300/X550/X1050 Series Secondary  ",RV370,  5B73 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X300/X550/X1050 Series Secondary  ",RV370 , 5B70 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X550/X700 Series ",RV410,  5657 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X550/X700 Series Secondary ",RV410,  5677 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X600 Series ",RV380  ,5B62 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X600 Series Secondary ",RV380,  5B72 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X600/X550 Series ",RV380 ,  3E50 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X600/X550 Series Secondary ",RV380, 3E70 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X700  ",RV410  ,5E4D )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X700 PRO ",RV410,  5E4B )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X700 PRO Secondary ",RV410 , 5E6B )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X700 SE ",RV410 , 5E4C )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X700 SE Secondary  ",RV410,  5E6C )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X700 Secondary  ",RV410, 5E6D )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X700 Series Secondary ",RV410 , 566F )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X700 XT ",RV410, 5E4A )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X700 XT Secondary  ",RV410,  5E6A )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X700/X550 Series ",RV410 , 5E4F )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X700/X550 Series Secondary ",RV410,  5E6F )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 CrossFire Edition  ",R430  ,554D )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 CrossFire Edition Secondary  ",R430,  556D )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 GT ",R423,  554B )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 GT Secondary  ",R423,  556B )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 GTO ",R423 , 5549 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 GTO ",R430  ,554F )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 GTO ",R480  ,5D4F )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 GTO Secondary ",R423  ,5569 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 GTO Secondary ",R430  ,556F )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 GTO Secondary ",R480  ,5D6F )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 PRO ",R420 ,4A49 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 PRO Secondary ",R420 ,4A69 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 SE ",R420  ,4A4F )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 SE Secondary  ",R420,  4A6F )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 Series ",R420, 4A48 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 Series ",R420  ,4A4A )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 Series ",R420 , 4A4C )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 Series ",R423 , 5548 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 Series Secondary ",R420,  4A68 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 Series Secondary ",R420 , 4A6A )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 Series Secondary ",R420  ,4A6C )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 Series Secondary ",R423  ,5568 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 VE ",R420  ,4A54 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 VE Secondary  ",R420,  4A74 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 XT ",R420  ,4A4B )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 XT ",R423  ,5D57 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 XT Platinum Edition  ",R420,  4A50 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 XT Platinum Edition  ",R423 , 554A )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 XT Platinum Edition Secondary  ",R420, 4A70 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 XT Platinum Edition Secondary  ",R423 , 556A )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 XT Secondary  ",R420 , 4A6B )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X800 XT Secondary  ",R423 ,5D77 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X850 CrossFire Edition  ",R480  ,5D52 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X850 CrossFire Edition Secondary  ",R480,  5D72 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X850 XT ",R480  ,5D52 )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X850 XT Platinum Edition  ",R480, 5D4D )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X850 XT Platinum Edition Secondary  ",R480, 5D6D )
  ATI_DEVICE_ID( 0x000, " ATI RADEON X850 XT Secondary  ",R480  ,5D72 )

};

static int FindDevice(
  const D3DADAPTER_IDENTIFIER9 &id, const DeviceLevelInfo *devices, int nDevices
)
{
  int deviceLevel = INT_MAX;
  {
    for (int i=0; i<nDevices; i++)
    {
      if (id.DeviceId==devices[i].deviceId)
      {
        LogF("Display adapter identified as %s, level %x",devices[i].name1,devices[i].level);
        saturateMin(deviceLevel,devices[i].level);
        break;
      }
    }
  }
  if (deviceLevel==INT_MAX)
  {
    // if we cannot recognize the device, let features do the work
  }
  return deviceLevel;
}

int EngineDD9::GetAdapterLevel(int adapter) const
{
  // based on device ID
  int deviceLevel = INT_MAX;
  // scan for some capabilities identifying generation by vendor
  int genLevel = 0x300;
  // check feature set (shader model, video memory)
  int featuresLevel = 0x300;

  D3DADAPTER_IDENTIFIER9 id;
  _direct3D->GetAdapterIdentifier(adapter,0,&id);

  D3DCAPS9 caps;
  memset(&caps,0,sizeof(caps));
  _direct3D->GetDeviceCaps(adapter,_devType,&caps);

  {
    // check vertex and pixel shader model
    int vs = caps.VertexShaderVersion&0xffff;
    if (vs<0x300) saturateMin(featuresLevel,0); // no HW 3.0 vertex shaders - very low end
    int ps = caps.PixelShaderVersion&0xffff;
    if (ps<0x300) saturateMin(featuresLevel,0);
    // check amount of video memory (hard to check)
    if (
      _direct3D->CheckDeviceFormat(
      adapter,_devType,D3DFMT_X8R8G8B8,
      D3DUSAGE_RENDERTARGET,D3DRTYPE_TEXTURE,
      D3DFMT_A16B16G16R16
      )!=D3D_OK
      )
    {
      // no D3DFMT_A16B16G16R16 RT Texture on nVidia means 6200 or lower - assume PPLLow End
      // all SM 2.0 ATI support D3DFMT_A16B16G16R16
      // 
      genLevel = 0x0;
    }
  }

  {  
    if (id.VendorId==0x1002) // ATI
    {
      deviceLevel = FindDevice(id,ATIDevices,lenof(ATIDevices));
    }
    else if (id.VendorId==0x10DE) // nVidia
    {
      deviceLevel = FindDevice(id,NVDevices,lenof(NVDevices));
    }
    else if (id.VendorId==0x8086) // Intel
    {
      // Intel is most likely on-board - assume low end
      deviceLevel = 0x100;
    }
    else if (id.VendorId==0x5333) // S3
    {
      deviceLevel = 0x000;
    }
    else if (id.VendorId==0x1039) // SiS
    {
      deviceLevel = 0x000;
    }
  }  
  // select the minimum of those values
  return intMin(deviceLevel,featuresLevel,genLevel);
}

void EngineDD9::CheckFreeTextureMemory(int &local, int &nonlocal) const
{
  local = 0,nonlocal = 0;
  if (_ddraw)
  {
    {
      DDSCAPS2 caps;
      DWORD totalmem = 0,freemem = 0;
      caps.dwCaps = DDSCAPS_LOCALVIDMEM|DDSCAPS_VIDEOMEMORY;
      caps.dwCaps2 = 0;
      caps.dwCaps3 = 0;
      caps.dwCaps4 = 0;
      HRESULT ok = _ddraw->GetAvailableVidMem(&caps,&totalmem,&freemem);
      if (SUCCEEDED(ok))
      {
        // avoid 32b overflow
        if (freemem>INT_MAX) freemem = INT_MAX;
        local = freemem;
      }
    }
    {
      DDSCAPS2 caps;
      DWORD totalmem = 0,freemem = 0;
      caps.dwCaps = DDSCAPS_NONLOCALVIDMEM|DDSCAPS_VIDEOMEMORY;
      caps.dwCaps2 = 0;
      caps.dwCaps3 = 0;
      caps.dwCaps4 = 0;
      HRESULT ok = _ddraw->GetAvailableVidMem(&caps,&totalmem,&freemem);
      if (SUCCEEDED(ok))
      {
        // avoid 32b overflow
        if (freemem>INT_MAX) freemem = INT_MAX;
        nonlocal = freemem;
      }
    }
  }
}

/*!
\patch 5138 Date 3/13/2007 by Ondra
- Fixed: Measures implemented to avoid textures being placed in AGP,
which often caused degraded performance after prolonged playing.
*/

void EngineDD9::CheckLocalNonlocalTextureMemory()
{
  if (_ddraw && _caps._localPrediction)
  {
    int freeLocal = 0,freeNonLocal = 0;
    CheckFreeTextureMemory(freeLocal,freeNonLocal);
    // once there is not enough space to make sure texture creation will be successful,
    // we cannot update our guess any more, because some textures may already be in non-local memory
    if (_freeLocalVramAmount>32*1024*1024)
    {
      _freeLocalVramAmount = freeLocal;
      _freeNonlocalVramAmount = freeNonLocal;
      // record time of the test only when we used the results
      _freeEstCheckTime = ::GlobalTickCount();
    }
    // even if the test was not successful, we want to prevent another test to be executed soon
    _freeEstDirty = 0;
  }
}



void EngineDD9::InitAdapter()
{
  int adapter = D3DAdapter;

  if (adapter<0 || adapter>=(int)_direct3D->GetAdapterCount())
  {
    // auto-detect best adapter

    int maxLevel = 0;
    for (int ad=0; ad<(int)_direct3D->GetAdapterCount(); ad++)
    {
      int level = GetAdapterLevel(ad);
      if (level>maxLevel)
      {
        maxLevel = level;
        adapter = ad;
      }
    }
    if (adapter<0)
    {
      // if no adapter is recognized, select the first one
      adapter = 0;
    }
  }

  _deviceLevel = GetAdapterLevel(adapter);
  D3DAdapter = adapter;
}

/// maximum Command Buffer size
//const int MaxHWCBSize = 128*1024; // TODO:MC: - reduce - experiments have shown CBs up to 20 KB
const int MaxHWCBSize = 160*1024; // TODO:MC: - reduce - experiments have shown CBs up to 20 KB

/// context for DDEnumCallbackEx
struct DDEnumCallbackExContext
{
  /// input - which monitor
  HMONITOR hm;
  /// output - which device
  GUID devGuid;
  /// pointer set to devGuid if device was found
  GUID *guidPtr;

  DDEnumCallbackExContext()
  {
    guidPtr = NULL;
  }
};

/// search for given monitor
static BOOL WINAPI DDEnumCallbackEx(
                                    GUID FAR *lpGUID,  LPSTR lpDriverDescription, LPSTR lpDriverName,        
                                    LPVOID lpContext, HMONITOR  hm        
                                    )
{
  DDEnumCallbackExContext *ctx = (DDEnumCallbackExContext *)lpContext;
  if (hm==ctx->hm)
  {
    ctx->devGuid = *lpGUID;
    ctx->guidPtr = &ctx->devGuid;
    // found - terminate
    return FALSE;
  }
  // not found - continue
  return TRUE;
}

void EngineDD9::InitDXGIInfo(const DXGI_ADAPTER_DESC &adapterDesc)
{
  _lastDeviceId = RString();
  _caps._localVramAmount = Clip32B(adapterDesc.DedicatedVideoMemory);
  _caps._nonlocalVramAmount = Clip32B((long long)adapterDesc.DedicatedSystemMemory+(long long)adapterDesc.SharedSystemMemory);
  _caps._wddm = true;
  _freeLocalVramAmount = 0;
  _freeNonlocalVramAmount = 0;
  _freeEstDirty = 0;
  _freeEstCheckTime = 0;
}

void EngineDD9::InitDirectDraw7(int adapter)
{
  _caps._wddm = false;
#if !defined(_X360SHADERGEN)
  /*
  // following solution adapted from http://msdn.microsoft.com/en-us/library/windows/desktop/bb174526(v=vs.85).aspx does not work
  // it requires DX10 interfaces, does not work with Direct3D9Ex
  {
    ComRef<IDXGIDevice> pDXGIDevice;
    _direct3DEx->QueryInterface(__uuidof(IDXGIDevice), (void **)pDXGIDevice.Init());
    if (pDXGIDevice.NotNull())
    {
      ComRef<IDXGIAdapter> pDXGIAdapter;
      pDXGIDevice->GetAdapter(pDXGIAdapter.Init());
      if (pDXGIAdapter)
      {
        DXGI_ADAPTER_DESC adapterDesc;
        pDXGIAdapter->GetDesc(&adapterDesc);
        InitDXGIInfo(adapterDesc);
        return;
      }
    }
  }
  */

  HMONITOR d3dMonitor = _direct3D->GetAdapterMonitor(adapter);
  if (_direct3DEx)
  {
    // Vista/Win7 - ask DXGI instead
    struct HMODULEAuto: private ::NoCopy
    {
      HMODULE hmod;
      
      HMODULEAuto(HMODULE hmod):hmod(hmod){}
      ~HMODULEAuto(){if (hmod) FreeLibrary(hmod);}
      
      operator HMODULE () const {return hmod;}
    };
    HMODULEAuto dxgiDll(LoadLibrary("dxgi.dll"));

    if(dxgiDll)
    {
      typedef HRESULT WINAPI CreateDXGIFactoryType(REFIID riid, void **ppFactory);

      // Obtain the address of the Direct3DCreate9Ex function. 
      CreateDXGIFactoryType *createFactory = (CreateDXGIFactoryType *)GetProcAddress( dxgiDll, "CreateDXGIFactory" );
      if ( createFactory != NULL)
      {

        ComRef<IDXGIFactory> factory;
        createFactory(__uuidof(IDXGIFactory),(void **)factory.Init());
        if (factory)
        {
          for (int a=0; a<INT_MAX; a++)
          {
            ComRef<IDXGIAdapter> dxgiAdapter;
            if (!SUCCEEDED(factory->EnumAdapters(a,dxgiAdapter.Init())) || dxgiAdapter.IsNull()) break;
            // check outputs to see if our monitor is there
            bool match = false;
            for (int m=0; m<INT_MAX; m++)
            {
              ComRef<IDXGIOutput> output;
              if (!SUCCEEDED(dxgiAdapter->EnumOutputs(m,output.Init())) || output.IsNull() ) break;
              DXGI_OUTPUT_DESC outDesc;
              output->GetDesc(&outDesc);
              if (outDesc.Monitor==d3dMonitor)
              {
                match = true;
                break;
              }
              
            }
            if (match)
            {
              DXGI_ADAPTER_DESC adapterDesc;
              dxgiAdapter->GetDesc(&adapterDesc);
              InitDXGIInfo(adapterDesc);
              return;
            }
          }
        }
      }
    }
  }

  D3DADAPTER_IDENTIFIER9 adid;
  _direct3D->GetAdapterIdentifier(adapter, 0, &adid);

  RString deviceId = Format("%d,%d,%d",adid.VendorId,adid.DeviceId,adid.SubSysId);

  // decide about DDraw device based on monitor handle
  DDEnumCallbackExContext ctx;
  ctx.hm = d3dMonitor;
  DirectDrawEnumerateEx(
    DDEnumCallbackEx,&ctx,
    DDENUM_ATTACHEDSECONDARYDEVICES|DDENUM_DETACHEDSECONDARYDEVICES|DDENUM_NONDISPLAYDEVICES
    );
  //if (strcmp(deviceId,_lastDeviceId))
  {
    // before creating device we want to check its memory properties using DX7
    // we do not know how adapters ID maps to ddraw GUIDS
    // we can handle only the simple case - default adapter is used

    DirectDrawCreateEx(ctx.guidPtr,(void **)_ddraw.Init(),IID_IDirectDraw7,NULL);

    _lastDeviceId = RString();
    _caps._localVramAmount = 0;
    _caps._nonlocalVramAmount = 0;
    _freeLocalVramAmount = 0;
    _freeNonlocalVramAmount = 0;
    _freeEstDirty = 0;
    _freeEstCheckTime = 0;

    // if this is plain wrong, we will use heuristics:
    // assume half of VRAM, other half AGP
    // round to nearest pow2
    _lastDeviceId = deviceId;
  }
#endif
}

/*!
\patch 1.21 Date 8/23/2001 by Ondra
- Improved: Added more information to message "Cannot create 3D device"
- Improved: W-buffer used on nVidia cards to improve z-buffer precision
in 16b modes.
\patch 1.30 Date 11/02/2001 by Ondra
- New: Ground multi-texturing can be turned off in Video options.
This can be used to resolve compatibility issues
with some graphics adapters ("white ground problem").
\patch 1.34 Date 12/06/2001 by Ondra
- Fixed: W-buffer support was broken.
- New: W-buffer support enabled on nVidia cards in both 16b and 32b modes.
\patch 1.44 Date 2/13/2002 by Ondra
- Fixed: When no display adapter is selected in preferences
and no adapter is recognized, select first one.
This solves error message "Cannot create 3D device: Adapter -1"
\patch 5272 Date 16/1/2009 by Ondra
- Fixed: Game did not run on ATI cards in fullscreenmode on Vista x64 with more then 4 GB RAM.
*/

void EngineDD9::Init3D()
{
  memset(&_presentPars,0,sizeof(_presentPars));

  // try to create device (from best to worst)
  HRESULT err = D3D_OK;

  int adapter = D3DAdapter;
  FindMode(adapter);

  // NVPerfHUD not active by default
  _nvPerfHUDActive = false;

#ifdef DEBUG_PERFHUD
  for (UINT currAdapter = 0; currAdapter < _direct3D->GetAdapterCount(); currAdapter++)
  {
    D3DADAPTER_IDENTIFIER9 identifier;
    _direct3D->GetAdapterIdentifier(currAdapter, 0, &identifier);
    if (strstr(identifier.Description,"PerfHUD"))
    {
      _nvPerfHUDActive = true;
      adapter = currAdapter;
#if !defined(_X360SHADERGEN)
      _devType = D3DDEVTYPE_REF;
#else
      _devType = D3DDEVTYPE_COMMAND_BUFFER;
#endif
      break;
    }
  }
#endif

#ifdef _X360SHADERGEN
  _devType = D3DDEVTYPE_COMMAND_BUFFER;
#endif

  #if !XBOX_D3D
    #define RESTORE_AFFINITY 1
    // remember current affinity so that we can restore it later
    DWORD oldAffinity = ~0;
    DWORD sysAffinity = ~0;
    GetProcessAffinityMask(GetCurrentProcess(),&oldAffinity,&sysAffinity);
    #if 1
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
  #endif

  D3DCAPS9 caps;
  err = _direct3D->GetDeviceCaps(adapter,_devType,&caps);
  if (err != D3D_OK)
  {
    RptF("Error: GetDeviceCaps failed");
    // avoid using uninitialized values
    memset(&caps,0,sizeof(caps));
  }

  // Init the behavior flag
  DWORD behavior = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
  if (_devType!=D3DDEVTYPE_NULLREF && (caps.DevCaps&D3DDEVCAPS_HWTRANSFORMANDLIGHT))
  {
    if ((caps.VertexShaderVersion&0xffff)>=0x200)
    {
      if (!DebugVS)
      {
        behavior = D3DCREATE_HARDWARE_VERTEXPROCESSING;
      }
    }
    if (caps.DevCaps&D3DDEVCAPS_PUREDEVICE)
    {
      if (!DebugVS)
      {
        behavior |= D3DCREATE_PUREDEVICE;
      }
      LogF("Adapter %d: T&L HW detected (Pure)",adapter);
    }
    else
    {
      LogF("Adapter %d: T&L HW detected",adapter);
    }
  }

#if !defined(_X360SHADERGEN)
  if (GetCPUCount()>1)
  {
    // when using CBs and background thread, thread safety can make some things easier
    behavior |= D3DCREATE_MULTITHREADED;
  }
  //InitDirectDraw7(adapter);

  if (_ddraw)
  {
    {
      DDSCAPS2 caps;
      DWORD totalmem = 0,freemem = 0;
      caps.dwCaps = DDSCAPS_LOCALVIDMEM|DDSCAPS_VIDEOMEMORY;
      caps.dwCaps2 = 0;
      caps.dwCaps3 = 0;
      caps.dwCaps4 = 0;
      _ddraw->GetAvailableVidMem(&caps,&totalmem,&freemem);
      // avoid 32b overflow
      if (freemem>INT_MAX) freemem = INT_MAX;
      if (totalmem>INT_MAX) totalmem = INT_MAX;
      _caps._localVramAmount = totalmem;
      _freeLocalVramAmount = freemem;
    }
    {
      DDSCAPS2 caps;
      DWORD totalmem = 0,freemem = 0;
      caps.dwCaps = DDSCAPS_NONLOCALVIDMEM|DDSCAPS_VIDEOMEMORY;
      caps.dwCaps2 = 0;
      caps.dwCaps3 = 0;
      caps.dwCaps4 = 0;
      _ddraw->GetAvailableVidMem(&caps,&totalmem,&freemem);
      // avoid 32b overflow
      if (freemem>INT_MAX) freemem = INT_MAX;
      if (totalmem>INT_MAX) totalmem = INT_MAX;
      _caps._nonlocalVramAmount = totalmem;
      _freeNonlocalVramAmount = freemem;
    }
    _freeEstDirty = 0;
    _freeEstCheckTime = ::GlobalTickCount();

    LogF("A. Local %d, nonLocal %d",_caps._localVramAmount,_caps._nonlocalVramAmount);
  }
#else
  _caps._localVramAmount = 64*1024*1024;
  _caps._nonlocalVramAmount = 0;
  _freeLocalVramAmount = _caps._localVramAmount;
  _freeNonlocalVramAmount = 0;
  _freeEstDirty = 0;
  _freeEstCheckTime = ::GlobalTickCount();
  _lastDeviceId = RString();
#endif
  _resetDirty = 0;
  _resetFrame = 0;
  // not creating directly into _d3DDevice, because we may want to create a wrapper
  ComRef<IDirect3DDevice9> dev;
  
  #if !XBOX_D3D
    if (_direct3DEx)
    {
      // note: while technically this flags should work on Vista even when Ex device is not present,
      // detecting Ex device is much easier and more robust then checking Windows version
      behavior |= D3DCREATE_DISABLE_PSGP_THREADING;
    }
  #endif

  int backBufferCount = _presentPars.BackBufferCount;
  // do not retry for more than 1 minute
  for(int maxRetry=60;--maxRetry>=0;)
  {
    err = _direct3D->CreateDevice(adapter,_devType,_hwndApp,behavior,&_presentPars,dev.Init());
    if (err==D3DERR_DEVICELOST)
    {
      // device is lost - wait for a while and try again
      Sleep(1000);
    }
    else
    {
      // on success or other errors break the loop and do not retry
      break;
    }
  }

  if (err==D3DERR_NOTAVAILABLE && _devType==D3DDEVTYPE_REF)
  {
    _devType = D3DDEVTYPE_NULLREF;
    err = _direct3D->CreateDevice(adapter,_devType,_hwndApp,behavior,&_presentPars,dev.Init());
    if (SUCCEEDED(err))
    {
      RptF("Reference device not available, generating shaders using NULL reference device");
    }
  }
  if (err==D3DERR_NOTAVAILABLE && _devType==D3DDEVTYPE_NULLREF)
  {
    _devType = D3DDEVTYPE_HAL;
    err = _direct3D->CreateDevice(adapter,_devType,_hwndApp,behavior,&_presentPars,dev.Init());
    if (SUCCEEDED(err))
    {
      RptF("NULL reference device not available, generating shaders using HAL device");
    }
  }
  // number of back buffer may be adjusted - try again
  if ((err!=D3D_OK || !dev) && backBufferCount>1)
  {
    _presentPars.BackBufferCount = 1;
    err = _direct3D->CreateDevice(adapter,_devType,_hwndApp,behavior,&_presentPars,dev.Init());
  }
  if (err!=D3D_OK || !dev)
  {
    DDError9("Cannot create 3D device",err);
    RptF
      (
      "Resolution failed: %dx%dx (%d Hz)",
      _presentPars.BackBufferWidth,_presentPars.BackBufferHeight,_presentPars.FullScreen_RefreshRateInHz
      );

	 D3DADAPTER_IDENTIFIER9 id;
    _direct3D->GetAdapterIdentifier(adapter, 0, &id);
    ErrorMessage
      (
      "Cannot create 3D device:\n"
      "  Adapter %d (%s) %s\n"
      "  Resolution %dx%d, format %s/%s/%s, refresh %d Hz.\n"
      "  Error %s",
      (int)adapter,(const char *)id.Description,
      _presentPars.Windowed ? "Windowed" : "Fullscreen",
      _presentPars.BackBufferWidth,_presentPars.BackBufferHeight,
      SurfaceFormatNameDD9(_presentPars.BackBufferFormat),
      SurfaceFormatNameDD9(_rtFormat),
      SurfaceFormatNameDD9(_presentPars.AutoDepthStencilFormat),
      _presentPars.FullScreen_RefreshRateInHz,
      DXGetErrorString9(err)
      );
  }

  #if RESTORE_AFFINITY
  SetProcessAffinityMask(GetCurrentProcess(),oldAffinity);
  #endif

  _d3DDevice.AttachDevice(dev);
  //_d3DPlay.AttachDevice(dev);

  LogF("Backbuffers: %d",_presentPars.BackBufferCount);

#if _GAMES_FOR_WINDOWS
  // attach device to Games for Windows - LIVE 
  HRESULT result = XLiveOnCreateDevice(dev, &_presentPars);
  if (FAILED(result)) RptF("XLiveOnCreateDevice failed with 0x%x", result);
#endif

  _caps._canHWMouse = (caps.CursorCaps&D3DCURSORCAPS_COLOR)!=0;

  _caps._vertexShaders = caps.VertexShaderVersion&0xffff;
  _caps._pixelShaders = caps.PixelShaderVersion&0xffff;
  if (_caps._vertexShaders)
  {
    LogF("Vertex shaders version %d.%d",_caps._vertexShaders>>8,_caps._vertexShaders&0xff);
  }
  if (_caps._pixelShaders)
  {
    LogF("Pixel shaders version %d.%d",_caps._pixelShaders>>8,_caps._pixelShaders&0xff);
  }

  if (_caps._pixelShaders<0x300 && _devType!=D3DDEVTYPE_NULLREF)
  {
    ErrorMessage("Pixel Shader 3.0 support required");
  }

  _maxAFLevel = caps.MaxAnisotropy;
  saturate(_maxAFLevel,1,16);

  // test for some known adapters
  // (based on driver name?)

  //_maxLights = 8;
  // patch: detection of known device capabilities

  //InitTexMemEstimations();


  LogF(
    "Format %s/%s/%s, %dx%d/%dx%d, shadows %s, Downsample %s, Aperture %s",
    SurfaceFormatNameDD9(_presentPars.BackBufferFormat),
    SurfaceFormatNameDD9(_rtFormat),
    SurfaceFormatNameDD9(_presentPars.AutoDepthStencilFormat),
    _w,_h,_wRT,_hRT,
    SurfaceFormatNameDD9(_smFormat),
    SurfaceFormatNameDD9(_downsampleFormat),
    SurfaceFormatNameDD9(_apertureFormat)
    );

  _bgRecPredication = false;
  
  // warm up thread testing where necessary

  if (_devType!=D3DDEVTYPE_NULLREF && _devType!=D3DDEVTYPE_REF)
  {

    // CPU #2 - the same as for rendering micro-jobs
	if ( !_bg._thread )
		_bg.Init(64 * 1024, BGThreadCallback, this, 2, THREAD_PRIORITY_BELOW_NORMAL, "Render");
    // CPU #4 - the same as for rendering micro-jobs
    if ( !_vis._threadId )
		_vis.Init( CreateThreadOnCPU(64 * 1024, VisThreadCallback, this, 4, THREAD_PRIORITY_BELOW_NORMAL, "Visualize", &_vis._threadId) );
    Init3DState();
  }

  _ppaaQuality = 0;
  _fxaaSharpQuality = 0;
}

void EngineDD9::Done3D()
{
  // signal background rendering thread should terminate
  _bg.Done();
  _vis.Done();
  
  Done3DState();
#if _GAMES_FOR_WINDOWS
  // dettach device from Games for Windows - LIVE 
  XLiveOnDestroyDevice();
#endif
  _d3DDevice.DetachDevice();
}
AbstractTextBank *EngineDD9::TextBank() {return _textBank;}

void EngineDD9::CreateTextBank(ParamEntryPar cfg, ParamEntryPar fcfg)
{
  if (_textBank.IsNull())
    //if (!_textBank)
  {
    _textBank = new TextBankD3D9(this, cfg, fcfg);
    if (_textBank.IsNull())
      //if (!_textBank)
    {
      ErrorMessage("Cannot create texture bank.");
      return;
    }
    // apply the settings which we only remembered while _textBank did not exists yet
    _textBank->SetTextureQuality(_textureQuality);
    _textBank->SetTextureMemory(_textureMemory);
  }
}

void EngineDD9::DestroyTextBank()
{
  _textBank.Free();
  //if( _textBank ) delete _textBank,_textBank=NULL;
}

void EngineDD9::D3DDrawTexts()
{
}

struct EngineDD9::DrawText3DCenteredAsyncPars: public MainThreadRenderTask
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
    static_cast<EngineDD9 *>(engine)->DoDrawText3DCenteredAsync(*this);
  }
};


void EngineDD9::DoDrawText3DCenteredAsync(const DrawText3DCenteredAsyncPars &pars)
{
  if (IsFontReady3D(pars.font))
  {
    Vector3 width = GetText3DWidth(pars.aside, pars.font, pars.text);
    DrawText3D(
      -1,pars.center + pars.up * 0.5 - width * 0.5, pars.up, pars.aside, pars.clip,
      pars.font, pars.color, 0, pars.text, pars.x1c, pars.y1c, pars.x2c, pars.y2c
    );
  }
}



void EngineDD9::PerformTaskOnMainThread(MainThreadRenderTask * task)
{
  if (!_mainThreadTasks.PutMultipleProducers(task))
  {
    LogF("Out of space for 3D async texts");
    delete task;
  }
}

void EngineDD9::DrawText3DCenteredAsync(
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

//#define SPEC_2D (NoZBuf|IsAlpha|ClampU|ClampV|IsAlphaFog)

inline bool EngineDD9::ClipDraw2D
(
 float &xBeg, float &yBeg, float &xEnd, float &yEnd,
 float &uBeg, float &vBeg, float &uEnd, float &vEnd,
 const Draw2DParsExt &pars, const Rect2DAbs &rect, const Rect2DAbs &clip
 )
{
  // perform simple clipping
  xBeg=rect.x-0.5f;
  xEnd=xBeg+rect.w;
  yBeg=rect.y-0.5f;
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

void EngineDD9::Draw2D(const Draw2DParsExt &pars, const Rect2DAbs &rect, const Rect2DAbs &clip)
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

void EngineDD9::DoDraw2D(const Draw2DParsExt &pars, const Rect2DAbs &rect, const Rect2DAbs &clip)
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

void EngineDD9::Draw2D(const Draw2DPars &pars, const Rect2DAbs &rect, const Rect2DAbs &clip)
{
  // TODO: special case optimization
  Draw2D(Draw2DParsExt(pars),rect,clip);
}
void EngineDD9::Draw2D(const Draw2DParsNoTex &pars, const Rect2DAbs &rect, const Rect2DAbs &clip)
{
  // TODO: special case optimization
  Draw2D(Draw2DParsExt(pars),rect,clip);
}

void EngineDD9::DrawLinePrepare()
{
  // use line texture
  Texture *tex = GPreloadedTextures.New(TextureLine);
  const MipInfo &mip = _textBank->UseMipmap(tex,1,1);
  int specFlags = NoZBuf|IsAlpha|ClampU|ClampV|IsAlphaFog;

  DrawPolyPrepare(mip,specFlags);
}

void EngineDD9::DrawLineDo(const Line2DAbs &line, PackedColor c0, PackedColor c1, const Rect2DAbs &clip)
{
  if (_backgroundD3D!=RecHighLevel) DoDrawLineDo(line,c0,c1,clip);
  else
  {
    RecordHighlevel()->DrawLineDo(line,c0,c1,clip);
  }
}
void EngineDD9::DoDrawLineDo(const Line2DAbs &line, PackedColor c0, PackedColor c1, const Rect2DAbs &clip)
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

void EngineDD9::DrawPolyPrepare(const MipInfo &mip, int specFlags)
{
  if (_backgroundD3D!=RecHighLevel) DoDrawPolyPrepare(mip,specFlags);
  else RecordHighlevel()->DrawPolyPrepare(mip,specFlags);
}

void EngineDD9::DoDrawPolyPrepare(const MipInfo &mip, int specFlags)
{
  if (ResetNeeded()) return;

  SwitchRenderMode(RM2DTris);
  Assert(!IsCBRecording());
  SwitchTL(-1,TLDisabled);
  //FlushAllQueues(_queueNo);
  //AddVertices(gv,n); // note: may flush all queues
  QueuePrepareTriangle(mip,GlobPreloadMaterial(NonTLMaterial),specFlags);
}

void EngineDD9::DrawPolyDo(const Vertex2DPixel *vertices, int n, const Rect2DPixel &clipRect)
{
  if (_backgroundD3D!=RecHighLevel) DoDrawPolyDo(vertices,n,clipRect);
  else
  {
    // no intention to write to vertices, but Array is not const aware
    Array<Vertex2DPixel> temp(unconst_cast(vertices),n);
    RecordHighlevel()->DrawPolyPixelDo(temp,clipRect);
  }
}

void EngineDD9::DrawPolyDo(const Vertex2DAbs *vertices, int n, const Rect2DAbs &clipRect)
{
  if (_backgroundD3D!=RecHighLevel) DoDrawPolyDo(vertices,n,clipRect);
  else
  {
    // no intention to write to vertices, but Array is not const aware
    Array<Vertex2DAbs> temp(unconst_cast(vertices),n);
    RecordHighlevel()->DrawPolyAbsDo(temp,clipRect);
  }
}
void EngineDD9::DoDrawPolyDo(const Vertex2DPixel *vertices, int n, const Rect2DPixel &clipRect)
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
    if( orClip&ClipTop ) n=Clip2D(clipRect,free,used,n,InsideTopPixel,InterpolateVertexPixel),Swap(free,used);
    if( orClip&ClipBottom ) n=Clip2D(clipRect,free,used,n,InsideBottomPixel,InterpolateVertexPixel),Swap(free,used);
    if( orClip&ClipLeft ) n=Clip2D(clipRect,free,used,n,InsideLeftPixel,InterpolateVertexPixel),Swap(free,used);
    if( orClip&ClipRight ) n=Clip2D(clipRect,free,used,n,InsideRightPixel,InterpolateVertexPixel),Swap(free,used);
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

void EngineDD9::DoDrawPolyDo(const Vertex2DAbs *vertices, int n, const Rect2DAbs &clipRect)
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
    if( orClip&ClipTop ) n=Clip2D(clipRect,free,used,n,InsideTopAbs,InterpolateVertexAbs),Swap(free,used);
    if( orClip&ClipBottom ) n=Clip2D(clipRect,free,used,n,InsideBottomAbs,InterpolateVertexAbs),Swap(free,used);
    if( orClip&ClipLeft ) n=Clip2D(clipRect,free,used,n,InsideLeftAbs,InterpolateVertexAbs),Swap(free,used);
    if( orClip&ClipRight ) n=Clip2D(clipRect,free,used,n,InsideRightAbs,InterpolateVertexAbs),Swap(free,used);
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

void EngineDD9::DrawPoly3D(const PositionRender &space, const MipInfo &mip, const Vertex2DFloat *vertices, int nVertices, const Rect2DFloat &clip, int specFlags, const TLMaterial &matType, const Pars3D *pars3D)
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
  n = Clip2D(clip, free, used, n, InsideTopFloat, InterpolateVertexFloat); Swap(free,used);
  n = Clip2D(clip, free, used, n, InsideBottomFloat, InterpolateVertexFloat); Swap(free,used);
  n = Clip2D(clip, free, used, n, InsideLeftFloat, InterpolateVertexFloat); Swap(free,used);
  n = Clip2D(clip, free, used, n, InsideRightFloat, InterpolateVertexFloat); Swap(free,used);
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
  int vOffset = _dynVBuffer_SVertex9.Lock(vertex, nVertices);
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
    _dynVBuffer_SVertex9.Unlock();

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
    PrepareTriangleTL(cb, NULL, mip, TexMaterialLODInfo(NULL), specFlags, mat, specFlags&DisableSun, prop);

    // Draw
    cbState._vertexDecl = _vertexDeclD[VD_Position_Normal_Tex0][VSDV_Basic];
    UVSource uv = UVTex;
    SetupSectionTL(
      cb, cbState._mainLight, FM_None, VSBasic, &uv, 1, true, STNone, false, 0, cbState._pointLightsCount, cbState._spotLightsCount,
      false, RMCommon, false, 0, NULL);
    SetVertexDeclaration(cb, cbState._vertexDecl);
    SetupVSConstants(cb, 0, 0);
    SetupPSConstants(cb);

#ifdef DONT_USE_DYNBUFFERS
    // we will need to make a copy of data
    Array<char> tmp((char *)&_dynVBuffer_SVertex9._geometry[vOffset],nVertices*sizeof(SVertex9));
    CALL_CB(cb,DrawPrimitiveUP,D3DPT_TRIANGLEFAN, nVertices - 2, tmp, sizeof(SVertex9));
    // using DrawPrimitiveUP means we have reset stream 0
    CBState(cb)._vBufferLast = VertexStaticData9::BufferFuture(VertexStaticData9::BufferFuture::Null);
    CBState(cb)._iBufferLast = IndexStaticData9::BufferFuture(IndexStaticData9::BufferFuture::Null);
#else
    CALL_CB(cb,DrawPrimitive,D3DPT_TRIANGLEFAN, cbState._vOffsetLast, nVertices - 2);
#endif
  }
}

void EngineDD9::DrawLine( const TLVertexTable &mesh, int beg, int end, int orLine )
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

//void EngineDD9::DrawLine( int beg, int end, int orLine )
//{
//  if (ResetNeeded()) return;
//
//  const TLVertex &v0 = _mesh->GetVertex(beg);
//  const TLVertex &v1 = _mesh->GetVertex(end);
//
//  float x0 = v0.pos.X();
//  float y0 = v0.pos.Y();
//  float x1 = v1.pos.X();
//  float y1 = v1.pos.Y();
//
//  float z0 = v0.pos.Z();
//  float z1 = v1.pos.Z();
//  float w0 = v0.rhw;
//  float w1 = v1.rhw;
//
//  // use line texture
//  Texture *tex = GPreloadedTextures.New(TextureLine);
//  const MipInfo &mip = _textBank->UseMipmap(tex,1,1);
//
//  // convert line to poly;
//  int specFlags = NoZWrite|IsAlpha|ClampU|ClampV|IsAlphaFog|orLine;
//  float dx = x1-x0;
//  float dy = y1-y0;
//  float dSize2 = dx*dx+dy*dy;
//  float invDSize = dSize2 >0 ? InvSqrt(dSize2) : 1;
//
//  float dSize = dSize2*invDSize;
//
//  // direction perpendicular dx, dy
//  // TODO: use color alpha as width
//  // 2D line drawing
//  float pdx = +dy*invDSize, pdy = -dx*invDSize;
//  float w = 3.0f;
//  x0 -= pdx*(w*0.5);
//  x1 -= pdx*(w*0.5);
//  y0 -= pdy*(w*0.5);
//  y1 -= pdy*(w*0.5);
//  float x0Side = x0+pdx*w, y0Side = y0+pdy*w;
//  float x1Side = x1+pdx*w, y1Side = y1+pdy*w;
//
//  Vertex2DAbs vertices[4];
//  float off = 0.0f;
//  //float off = 0.5f;
//  vertices[0].x = x0-off;
//  vertices[0].y = y0-off;
//  vertices[0].z = z0;
//  vertices[0].w = w0;
//  vertices[0].u = 0;
//  vertices[0].v = 0.25;
//  vertices[0].color = v0.color;
//
//  vertices[1].x = x0Side-off;
//  vertices[1].y = y0Side-off;
//  vertices[1].z = z0;
//  vertices[1].w = w0;
//  vertices[1].u = 0;
//  vertices[1].v = 1;
//  vertices[1].color = v0.color;
//
//  vertices[2].x = x1Side-off;
//  vertices[2].y = y1Side-off;
//  vertices[2].z = z1;
//  vertices[2].w = w1;
//  vertices[2].u = dSize;
//  vertices[2].v = 1;
//  vertices[2].color = v1.color;
//
//  vertices[3].x = x1-off;
//  vertices[3].y = y1-off;
//  vertices[3].z = z1;
//  vertices[3].w = w1;
//  vertices[3].u = dSize;
//  vertices[3].v = 0.25;
//  vertices[3].color = v1.color;
//
//  Rect2DAbs clip(0,0,_w,_h);
//
//  DrawPoly(mip,vertices,4,clip,specFlags);
//}

void EngineDD9::CreateVB()
{
  ReportGRAM9("Before CreateVertexBuffer");
  for (int i=0; i<NumDynamicVB2D; i++)
  {
    ComRef<IDirect3DVertexBuffer9> vBuffer;
    HRESULT err=_d3DDevice->CreateVertexBuffer(
      MeshBufferLength*sizeof(TLVertex),
      NoClipUsageFlag|D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,
      0,
      D3DPOOL_DEFAULT,
      vBuffer.Init(),NULL
      );
    _queueNo._meshBuffer[i].SetResult(vBuffer);
    if( err!=D3D_OK || !vBuffer)
    {
      DDError9("Cannot create vertex buffer.",err);
      return;
    }
    ComRef<IDirect3DIndexBuffer9> iBuffer;
    err = _d3DDevice->CreateIndexBuffer(
      IndexBufferLength*sizeof(VertexIndex),
      D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC|NoClipUsageFlag,
      D3DFMT_INDEX16,
      D3DPOOL_DEFAULT,
      iBuffer.Init(),NULL
      );
    _queueNo._indexBuffer[i].SetResult(iBuffer);
    if (err!=D3D_OK || !iBuffer)
    {
      ReportMemoryStatusWithVRAM();
      DDError9("CreateIndexBuffer",err);
      return;
    }
  }
  ReportGRAM9("After CreateVertexBuffer");
  _queueNo._vertexBufferUsed = 0;
  _queueNo._indexBufferUsed = 0;
  _queueNo._actualIBIndex;
  _queueNo._actualVBIndex;
} 

void EngineDD9::DestroyVB()
{
  for (int i=0; i<NumDynamicVB2D; i++)
  {
    _queueNo._meshBuffer[i].SetResult(ComRef<IDirect3DVertexBuffer9>());
    _queueNo._indexBuffer[i].SetResult(ComRef<IDirect3DIndexBuffer9>());
  }
  //_d3DDevice->SetStreamSource(0,NULL,0);
}

void EngineDD9::CreateVBTL()
{
  _dynVBuffer_SVertex9.Init(MaxShapeVertices,_d3DDevice);
	_dynVBuffer_SVertex9_ST.Init(MaxShapeVertices,_d3DDevice);
  //CreatePostProcessStuff();
} 

void EngineDD9::DestroyVBTL()
{
  //DestroyPostProcessStuff();
  _dynVBuffer_SVertex9.Dealloc();
	_dynVBuffer_SVertex9_ST.Dealloc();
}

void EngineDD9::D3DConstruct()
{
  _d3dFrameOpen = false;
  _d3dFrameOpenRequested = false;

  static StaticStorage<WORD> TriangleQueueStorageNo;
  TriQueue9 &triqNo = _queueNo._tri;
  triqNo._triangleQueue.SetStorage(TriangleQueueStorageNo.Init(TriQueueSize));

  CreateVB();
  CreateVBTL();

  //CreateTextBank();
}

void EngineDD9::D3DDestruct()
{
  Assert(!IsCBRecording());
  const int cb = -1;
  RendState &cbState = CBState(cb);

  for (int p=0; p<NPredicationModes; p++) cbState._vertexShaderLast[p].Free();

  DeinitPixelShaders();
  DeinitVertexShaders();
  DestroyVBTL();
  DestroyVB();
}

void EngineDD9::Restore()
{
  // it should not be necessary to reset the state here, however it should do not harm
  // and it should be more robust in case of driver errors (drivers not maintaining the state correctly)
  Assert(!IsCBRecording());
  const int cb = -1;
  RendState &cbState = CBState(cb);
  for (int p=0; p<NPredicationModes; p++) cbState._renderState[p].Clear();
  //cbState._samplerState
}

bool EngineDD9::CanRestore()
{
  return false;
  //return _frontBuffer->IsLost()==D3D_OK;
}

// implementation of MemoryFreeOnDemandHelper
float EngineDD9::Priority() const
{
  return 0.5;
}

size_t EngineDD9::MemoryControlledRecent() const
{
  return 0;
}

size_t EngineDD9::MemoryControlled() const
{
  return 0;
}

void EngineDD9::CheckLRU() const
{
  #if _ENABLE_REPORT
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  #if _DEBUG
  {
    VertexBufferD3D9LRUItem *last = _vBufferLRU.First();
    Assert((char*)last<((char *)&_vBufferLRU-16) || (char*)last>((char *)&_vBufferLRU+32));
  }
  #endif
  for (VertexBufferD3D9LRUItem *last = _vBufferLRU.First(); last; )
  {
    // free vertex buffer that was not used for longest time
    VertexTable *shape = unconst_cast(last->GetOwner());

    // owner can be set to NULL when shape deleted
    DoAssert(!shape || shape->NVertex() < 100000);
    Assert((char*)last<((char *)&_vBufferLRU-16) || (char*)last>((char *)&_vBufferLRU+32));
    // this one may be destroyed in ReleaseVBuffer - process to a next one
    VertexBufferD3D9LRUItem *prev = last;
    last = _vBufferLRU.Next(prev);
    Assert((char*)last<((char *)&_vBufferLRU-16) || (char*)last>((char *)&_vBufferLRU+32));
  }
  #endif
}
void EngineDD9::FlushVisThread() const
{
  Assert (Glob.config._renderThread==Config::ThreadingEmulated)
  // we cannot check vis thread without destroying it
  unconst_cast(this)->VisThreadProcess();
}



void EngineDD9::WaitForVisThread() const
{
  if (Glob.config._renderThread==Config::ThreadingEmulated)
  {
    FlushVisThread();
  }
  else
  {
    // TODO: push an emptty delegate as a fence and wait for it
  }
}


void EngineDD9::ReleaseAllVertexBuffers()
{
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  EndHighLevelRecording();
  for (VertexBufferD3D9LRUItem *last = _vBufferLRU.First(); last; )
  {
    // free vertex buffer that was not used for longest time
    VertexTable *shape = unconst_cast(last->GetOwner());

    // this one may be destroyed in ReleaseVBuffer - process to a next one
    last = _vBufferLRU.Next(last);
    // release v-buffer
    if (shape) shape->ReleaseVBuffer(); // owner can be set to NULL if ReleaseVBuffer() was called already
  }
}

size_t EngineDD9::FreeOneItem(int minAge)
{
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  for (VertexBufferD3D9LRUItem *last = _vBufferLRU.First(); last; last = _vBufferLRU.Next(last))
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
    DoAssert(static_cast<VertexBufferD3D9 *>(last)==vb);
    if (minAge<=0)
    {
      // do not test lock, release everything
    shape->ReleaseVBuffer();
    }
    if (vb && minAge>0)
    {
      // TODO: verify the buffer cannot be release meanwhile
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

size_t EngineDD9::FreeOneItem()
{
  return FreeOneItem(0);
}

size_t EngineDD9::Free(size_t size, int minAge)
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

size_t EngineDD9::ReleaseVideoMemory(size_t size, int minAge)
{
  return Free(size,minAge);
}

/**
@param limit max. VRAM+AGP allocation allowed for vertex buffers (textures already deduced)
*/
size_t EngineDD9::ThrottleVRAMAllocation(size_t limit, int minAge)
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

size_t EngineDD9::UsedVRAM() const
{
  return _vBuffersAllocated;
}

float EngineDD9::VRAMDiscardMetrics() const
{
  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  VertexBufferD3D9LRUItem *toDiscard = _vBufferLRU.First();
  if (!toDiscard) return 0;
  return _vBufferLRU.FrameID()-toDiscard->GetFrameNum();
}

int EngineDD9::VRAMDiscardCountAge(int minAge, int *totalSize) const
{
  int count = 0;
  int size = 0;

  CHECK_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  for (VertexBufferD3D9LRUItem *last = _vBufferLRU.First(); last; last = _vBufferLRU.Next(last))
  {
    int age = _vBufferLRU.FrameID()-last->GetFrameNum();
    if (age<minAge) break;

    count++;
    size += last->GetStoredMemoryControlled();
  }
  if (totalSize) *totalSize = size;
  return count;
}


void EngineDD9::MemoryControlledFrame()
{
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*TextBankDD());
  _vBufferLRU.Frame();
}

/**
@return description string, pointer into a static local buffer.
*/
const char *EngineDD9::GetMemErrorStatus() const
{
  if (!_textBank) return "";
  static BString<128> errorBuf;
  int local,nonlocal;
  CheckFreeTextureMemory(local,nonlocal);
  #if !XBOX_D3D
    MEMORYSTATUSEX memStat;
    memStat.dwLength = sizeof(memStat);
    GlobalMemoryStatusEx(&memStat);
    long long virtFree = memStat.ullAvailVirtual;
    long long texMemDX9 = _d3DDevice->GetAvailableTextureMem();
  #else
    MEMORYSTATUS memStat;
    memStat.dwLength = sizeof(memStat);
    GlobalMemoryStatus(&memStat);
    long long virtFree = memStat.dwAvailVirtual;
    long long texMemDX9 = 0;
  #endif
  sprintf(
    errorBuf,"VID: alloc %u, limit %d, free: local %d, nonlocal %d, DX9 %lld, virt. %lld",
    _textBank->GetVRamTexAllocation()+UsedVRAM(),
    GetVRAMAllocLimit(),local,nonlocal,texMemDX9,virtFree
    );
  return errorBuf;
}

size_t EngineDD9::FreeOnDemandSystemMemoryLowLevel(size_t toRelease) const
{
  /*
  When any texture / VB allocation or lock failed because of memory allocation this means:
  - on Xbox: there is no more physical memory left
  - on PC: there is no more virtual address space left
  */
  // most likely we are out of virtual address space
  // this is something the game memory manager is currently unable to release
  // we will try to release some VRAM resources as well and hope it will fix it
  size_t releasedSys = ::FreeOnDemandSystemMemoryLowLevel(toRelease);
  size_t releasedVRAM = _textBank->ForcedReserveMemory(toRelease);
  if (releasedSys>releasedVRAM) return releasedSys;
  return releasedVRAM;
}


float EngineDD9::GetResetFactor() const
{
  // assuming 400 allocations per second, we want to run about 60 minutes between resets
  const int dirtyToReset = 400*60*60;
  // assuming 30 fps, we want to run about 4 hours between resets
  const int framesToReset = 30*4*60*60;
  return float(_resetFrame)/framesToReset + float(_resetDirty)/dirtyToReset;
}

void EngineDD9::RequestFlushMemory()
{
  const int minFrames = 30*120;
  if (_resetFrame>minFrames && !_resetRequested)
  {
    // request a reset
    _resetRequested = GetResetFactor()>=1.0f;
    LOG_MEM("RequestFlushMemory %d",_resetRequested);
  }
}

void EngineDD9::RequestFlushMemoryCritical()
{
  // request a reset, until done very recently (with 30 fps at least 120 seconds)
  #if _PROFILE
  const int minFrames = 5*120;
  #else
  const int minFrames = 30*120;
  #endif
  if (_resetFrame>minFrames && !_resetRequested)
  {
    _resetRequested = true;
    LOG_MEM("RequestFlushMemoryCritical");
  }
}

void EngineDD9::FlushMemory(bool deep, bool allowAutoDeep)
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
    Reset();
  }
  else
  {
    _textBank->FlushTextures(deep,allowAutoDeep);
  }
}

#pragma comment(lib,"d3d9.lib")

#if 0 // _DEBUG
#pragma comment(lib,"d3dx9d.lib")
#else
#pragma comment(lib,"d3dx9.lib")
#endif

void EngineDD9::Activate()
{

}
bool EngineDD9::Deactivate()
{
  if (_textBank)
  {
    PROFILE_SCOPE_EX(deact,*);
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

#if _ENABLE_CHEATS

#include <Es/Memory/normalNew.hpp>
#include <d3dx9tex.h>
#include <Es/Memory/debugNew.hpp>

void EngineDD9::Screenshot(RString filename)
{
  HRESULT result;
  result = D3DXSaveSurfaceToFile(filename, D3DXIFF_BMP, _backBuffer, NULL, NULL);
  if (result != D3D_OK) DDError9("Error: Screenshot front buffer saving failed", result);
  result = D3DXSaveSurfaceToFile(filename + RString(".tga"), D3DXIFF_TGA, _renderTargetDepthInfoSurface, NULL, NULL);
  if (result != D3D_OK) DDError9("Error: Screenshot depth buffer saving failed", result);
}

void EngineDD9::DoSaveSurfaceIntoFile(IDirect3DSurface9 *surface)
{
  if (surface)
  {    
    BString<64> str;
    static int TextureID = 0;
    str.PrintF("%s%x.bmp", "ScreenShot", TextureID++);

    HRESULT result = D3DXSaveSurfaceToFile(str.cstr(), D3DXIFF_BMP, surface, NULL, NULL);
    if (result != D3D_OK) DDError9("Error: Screenshot buffer saving failed!!!", result);

    DIAG_MESSAGE(1000, Format("Screenshot captured: %s", str.cstr()));
  }
}

#else

void EngineDD9::Screenshot(RString filename)
{
}

void EngineDD9::DoSaveSurfaceIntoFile(IDirect3DSurface9 *surface)
{

}

#endif

#if _VBS3_SCREENSHOT

// Function returns automatic file name created from the current time
RString AutoFileName()
{
  SYSTEMTIME sysTime;
  GetLocalTime(&sysTime);
  BString<MAX_PATH> tmp;
  sprintf(tmp, "%d_%d_%d_%d_%d_%d_%d", sysTime.wYear, sysTime.wMonth, sysTime.wDay, sysTime.wHour, sysTime.wMinute, sysTime.wSecond, sysTime.wMilliseconds);
  return RString(tmp);
}

D3DXIMAGE_FILEFORMAT FormatAndExtension(RString fileType, RString &extension)
{
  // Lower the case
  fileType.Lower();

  // Create the format structure
  struct
  {
    RString fileType;
    RString fileExtension;
    D3DXIMAGE_FILEFORMAT format;
  } pictureFormats[] =
  {
    {RString("bmp"), RString("bmp"), D3DXIFF_BMP},
    {RString("jpg"), RString("jpg"), D3DXIFF_JPG},
    {RString("tga"), RString("tga"), D3DXIFF_TGA},
    {RString("png"), RString("png"), D3DXIFF_PNG},
    {RString("dds"), RString("dds"), D3DXIFF_DDS},
    {RString("ppm"), RString("ppm"), D3DXIFF_PPM},
    {RString("dib"), RString("dib"), D3DXIFF_DIB},
    {RString("hdr"), RString("hdr"), D3DXIFF_HDR},
    {RString("pfm"), RString("pfm"), D3DXIFF_PFM},
  };

  // Go through the formats and pick the matching one
  for (int i = 0; i < lenof(pictureFormats); i++)
  {
    if (pictureFormats[i].fileType == fileType)
    {
      extension = pictureFormats[i].fileExtension;
      return pictureFormats[i].format;
    }
  }

  // Not found, return jpg
  extension = RString("jpg");
  return D3DXIFF_JPG;
}

void EngineDD9::SaveScreenshot(RString fileName, RString fileType)
{
  // Get the absolute filename to save the screenshot into
  RString outFileNameWithPath;
  if (fileName.IsEmpty())
  {
    // Get destination path and create directory if required
    RString GetUserDirectory();
    outFileNameWithPath = GetUserDirectory() + RString("Screenshots\\");
    CreatePath(outFileNameWithPath);

    // Add the automatic name
    outFileNameWithPath = outFileNameWithPath + AutoFileName();
  }
  else
  {
    if (fileName[fileName.GetLength() - 1] == '\\' || fileName[fileName.GetLength() - 1] == '/')
    {
      // Copy the path
      outFileNameWithPath = fileName;
      CreatePath(outFileNameWithPath);

      // Add the automatic name
      outFileNameWithPath = outFileNameWithPath + AutoFileName();
    }
    else
    {
      // Copy the path and file name
      outFileNameWithPath = fileName;
    }
  }

  // Add extension according to fileType and remember the file type
  D3DXIMAGE_FILEFORMAT outFileFormat;
  {
    RString extension;
    outFileFormat = FormatAndExtension(fileType, extension);
    outFileNameWithPath = outFileNameWithPath + RString(".") + extension;
  }

  // Save the screenshot
  HRESULT result = D3DXSaveSurfaceToFile(outFileNameWithPath, outFileFormat, _backBuffer, NULL, NULL);
  if (result != D3D_OK) DDError9("Screenshot failed", result);
}

#endif //_VBS3_SCREENSHOT

#if _ENABLE_CHEATS

static RString FormatByteSizeNoZero(size_t size)
{
  if (size) return FormatByteSize(size);
  return RString();
}

RString EngineDD9::GetStat(int statId, RString &statVal, RString &statVal2)
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

#include "../engineDll.hpp"

Engine *CreateEngineD3D9( CreateEnginePars &pars )
{
  //DestroyEngine();
  //InitEngine(hInst,hPrev,sw,w,h);

  HideCursor=true;
  if (!pars.UseWindow) 
		EnableDesktopCursor(false);

  EngineDD9 *newEngine = new EngineDD9;
  if (newEngine == NULL)
    return NULL;
 
  if (!newEngine->Init(pars.hInst,pars.hPrev,pars.sw, pars.UseWindow, pars.bpp, pars.GenerateShaders))
  {
    newEngine->Done();
    delete newEngine;
    return NULL;
  }

  // Return created and initialized engine
  return newEngine;
}

//////////////////////////////////////////////////////////////////////////

HWND AppInit(
 HINSTANCE hInst,HINSTANCE hPrev,int sw, bool fullscreen,
 int x, int y, int width, int height
 );

void AppStateChangeBeg(HWND hwndApp, bool fullscreen, bool enableSize, int x, int y, int w, int h);
void AppStateChangeEnd(HWND hwndApp, bool fullscreen, bool enableSize, int x, int y, int w, int h);

BOOL AppDone(HWND hwndApp, HINSTANCE hInstance);


// constructor - attach  a engine to given backBuffer

void EngineDD9::InitSurfaces(ParamEntryPar cfg, ParamEntryPar fcfg)
{
  //LoadConfig();
  Init3D();
  DoSetGamma();

  // direct-color mode required
  D3DConstruct();

  CreateTextBank(cfg, fcfg);

  // Create common resources
  CreateResources();

#if _HELISIM_RENDER_TO_TEXTURE
  CreateR2TResources();
#endif

  // Create TI resources
  CreateTIResources();
}

void EngineDD9::DoneSurfaces(bool destroyTexBank)
{
  // Create TI resources
  DestroyTIResources();

#if _HELISIM_RENDER_TO_TEXTURE
  DestroyR2TResources();
#endif

  DestroyResources();

  _fonts.Clear();
  if (destroyTexBank) DestroyTextBank();

  D3DDestruct();
  Done3D();
}

void EngineDD9::DoSetGamma()
{

  D3DGAMMARAMP ramp;
  float eGamma=1/_gamma;
  for( int i=0; i<256; i++ )
  {
    float x=i*(1/255.0);
    float fx=pow(x,eGamma);
    int ifx=toInt(fx*65535.0); // both PC and X360 gamma ramp is 16b
    saturate(ifx,0,65535.0);
    ramp.red[i]=ramp.green[i]=ramp.blue[i]=ifx;
  }
  _d3DDevice->SetGammaRamp(0, 0,&ramp);

  LogF("Info: Set gamma %.3f", _gamma);
}

void EngineDD9::SetGamma( float gamma )
{
  saturate(gamma,1e-3,1e3);
  _gamma=gamma;
  if (!_d3DDevice.IsNull())
  {
    DoSetGamma();
  }
}

RECT EngineDD9::GetMonitorResolution()
{
  RECT rect;
  // in case detection is not executed, use some default values
  rect.left = 0;
  rect.top = 0;
  rect.right = 800;
  rect.bottom = 600;
#if !defined(_X360SHADERGEN)
  if (D3DAdapter >= 0)
  {
    HMONITOR mon = _direct3D->GetAdapterMonitor(D3DAdapter);

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
#endif
  return rect;
}

static float InterpretUsrBrightness(float usrBrightness)
{
  // Reinhard tonemapping - news:jqvuf7$nen$2@new-server.localdomain
  static const float scale = 0.592f;//0.74f;
  float expAdjust = usrBrightness>1 ? exp(usrBrightness-1) : usrBrightness;
  return expAdjust*scale;
}

// we want at least 20% to be left for HDR rendering
//const float MinHDRRange = 1/1.2f;
const float MinHDRRange = 1.4f;

// we do not want more then 40 % to be left for the HDR
//const float MaxHDRRange = 1/1.4f;
const float MaxHDRRange = 1.7f;

float EngineDD9::GetPreHDRBrightness() const
{
  // convert brightness from log to linear scale
  float expBrightness = InterpretUsrBrightness(_usrBrightness);
  float postHDR = GetPostHDRBrightness();
  return expBrightness/postHDR;
}
float EngineDD9::GetPostHDRBrightness() const
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

template <class Type>
static Type LoadCfgDef(ParamEntryPar cfg, const char *name, Type def)
{
  return cfg.ReadValue(name,def);
}

bool EngineDD9::SetTextureQualityWanted(int value)
{
  if (value==_textureQuality) return false;
  // when reducing textures, we need to "apply" (Reset the device) unless running Vista or newer
  bool needApply = value<_textureQuality && _direct3DEx.NotNull();
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

void EngineDD9::SetTextureQuality(int value)
{
  if (SetTextureQualityWanted(value) && !_d3DDevice.IsNull())
  {
    ApplyWanted();
  }
}

int EngineDD9::GetMaxTextureQuality() const
{
  return _textBank ? _textBank->GetMaxTextureQuality() : 2;
}

bool EngineDD9::SetTextureMemoryWanted(int value)
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

void EngineDD9::SetTextureMemory(int value)
{
  if (SetTextureMemoryWanted(value) && !_d3DDevice.IsNull())
  {
    ApplyWanted();
  }
}

int EngineDD9::GetMaxTextureMemory() const
{
  return _textBank ? _textBank->GetMaxTextureMemory() : 2;
}

int EngineDD9::GetTextureMemoryDefault() const
{
  // check DirectDraw7 stats to tell how much memory would we recommend
  
  int localVRAM = GetLocalVramAmount();
  
  return TextBankD3D9::SelectTextureMemory(localVRAM);
}


float EngineDD9::GetHDRFactor() const
{
  float hdrFactor = 0.5;
  return hdrFactor;
}

static const float FillRateRatio[][2]={{1,2},{2,3},{3,4},{4,5},{5,6},{7,8},{1,1},{8,7},{6,5},{5,4},{4,3},{3,2},{2,1}
#if _HELISIM_EXTENDED_RESOLUTION
,{3,1},{4,1},{5,1},{6,1}
#endif
};
  
// make sure the device can support the render target of given size

bool EngineDD9::FindFillrateControl(int wRTPreferred, int hRTPreferred)
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

#if !_HELISIM_EXTENDED_RESOLUTION
  saturateMin(bestWRT,_w*2);
  saturateMin(bestHRT,_h*2);
#endif
  
  if (_wRTRounded!=bestWRT || _hRTRounded!=bestHRT)
  {
    _wRTRounded =  bestWRT;
    _hRTRounded =  bestHRT;
    return true;
  }
  return false;
}

bool EngineDD9::SetVerticalSynchronization(bool vsync)
{
  if (_vsync != vsync) 
  {
    _vsync = vsync;
    return true;
  }

  return false;
}

void EngineDD9::SetVSync(bool vsync)
{
  if (SetVerticalSynchronization(vsync))
  {
    ApplyWanted();
  }
}

bool EngineDD9::SetFillrateControlWanted(ResolutionInfo info)
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

void EngineDD9::SetFillrateControl(ResolutionInfo info)
{
  if (SetFillrateControlWanted(info))
  {
    Reset();
  }
}

ResolutionInfo EngineDD9::GetFillrateControl() const
{
  ResolutionInfo info;

  info.w = _wRTRounded;
  info.h = _hRTRounded;
  info.bpp = PixelSize(); 
  return info;
}

float EngineDD9::GetDefaultFillrateControl(float &fMin, float &fMax) const
{
  // WIP: RESCALER: make sure the RT size is supported by the device
  fMin = 0.5f;
  fMax = 2.0f;
  return 1.0f; // TODO: add detection based on device class / benchmark
  // ratios in both w and h should be the same
}

void EngineDD9::SetDefaultFillrateControl() 
{
  int level = GetDeviceLevel();
  int iLevel = (level>>8)-1;
  saturate(iLevel,-1,3);
  if(SetFillrateLevelWanted(iLevel))
  {
    FindFillrateControl(_wRTPreferred,_hRTPreferred);
  }
}

void EngineDD9::ListFillrateResolutions(FindArray<ResolutionInfo> &ret)
{
  ret.Clear();
  
  for (int i=0; i<lenof(FillRateRatio); i++)
  {
    int nom = FillRateRatio[i][0], denom = FillRateRatio[i][1];
    
    ResolutionInfo info;
    info.w = _w/denom*nom;
    info.h = _h/denom*nom;
    info.bpp = PixelSize();

    if(info.w > _caps._maxTextureSize || info.h > _caps._maxTextureSize) continue;
    if((info.w * info.h) >= (320* 200) || (i==lenof(FillRateRatio)-1))
      ret.AddUnique(info);
  }
}

void ParseFlashpointCfg(ParamFile &file);
void SaveFlashpointCfg(ParamFile &file);
RString GetConfigSection();

void EngineDD9::LoadConfig(ParamEntryPar cfg,ParamEntryPar fcfg, const char *section)
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
    ConstParamEntryPtr posCfg = fcfg.GetClassInterface();
    if (section && *section)
    {
      ConstParamEntryPtr secCfg = fcfg.FindEntry(section);
      if (secCfg) posCfg = secCfg;
    }
    if (posCfg)
    {
      _winX = posCfg->ReadValue("winX",16);
      _winY = posCfg->ReadValue("winY",32);
      _winW = posCfg->ReadValue("winW",Glob.config.wantW);
      _winH = posCfg->ReadValue("winH",Glob.config.wantH);
      _fullW = posCfg->ReadValue("Resolution_W",_w);
      _fullH = posCfg->ReadValue("Resolution_H",_h);
      // avoid using corrupted values
      if(_winW<64) _winW = 64;
      if(_winH<48) _winH = 48;
      _WindowAspectW = fcfg.ReadValue("winDefW",Glob.config.wantW);
      _WindowAspectH = fcfg.ReadValue("winDefH",Glob.config.wantH);
    }
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


  if (!_initialized && !_caps._wddm)
  {
    // we want to load this only when not initialized yet
    // otherwise it would overwrite the settings we have already made
    // load unique ID to check if video adapter has changed
    // if it did, we may want to reset some default settings?

    // we certainly want to recheck amount of VRAM used
    _lastDeviceId = LoadCfgDef(fcfg,"lastDeviceId",RString());
    _caps._localVramAmount = LoadCfgDef(fcfg,"localVRAM",0);
    _caps._nonlocalVramAmount = LoadCfgDef(fcfg,"nonlocalVRAM",0);
  }

  {
    // user config
    float gamma = LoadCfgDef(cfg,"gamma",GetDefaultGamma());
    saturate(gamma, 0.5f, 2.0f);
    SetGamma(gamma);
  }

  int fsAAWanted = LoadCfgDef(fcfg,"FSAA",_fsAAWanted);
  int afWanted = cfg.ReadValue("anisoFilter",_afWanted);
  int postFxQuality = LoadCfgDef(fcfg,"postFX",_postFxQuality);
  int hdrPrecWanted = LoadCfgDef(fcfg,"HDRPrecision",8);
  int texQuality = cfg.ReadValue("TexQuality",GetTextureQuality());
  int texMemory = cfg.ReadValue("TexMemory",texQuality);
  bool vsync = LoadCfgDef(fcfg, "vsync", true);
  int atoc = LoadCfgDef(fcfg, "AToC",(int)AToC_Disable);
  bool ssaoEnabled = LoadCfgDef(fcfg, "ssaoEnabled", false);
  SetAToC(atoc);
	EnableSSAO(ssaoEnabled);

#if _ARMA3_SIMUL_WEATHER
  int swLayersCount = LoadCfgDef(fcfg, "SWLayersCount", (int)DefaultSWLayers);
  SetSWGeometryLayersCount(swLayersCount);
#endif

#if _HELISIM_RENDER_TO_TEXTURE
  int pipQuality = LoadCfgDef(fcfg, "PipQuality", 0);
  SetPipQuality(pipQuality);
#endif

#if _HELISIM_AMBIENT_TRAFFIC
  Glob.config.ambientTraffic = LoadCfgDef(fcfg, "AmbientTraffic", 1);
#endif

  int fxaaQuality = LoadCfgDef(fcfg, "PPAA", 0);
  SetPPAAQuality(fxaaQuality);

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

  if (changed && _initialized)
  {
    ApplyWanted();
  }

  // Bloom
  SetBloom(LoadCfgDef(cfg,"bloom",DefaultBloomThreshold));
  // RotBlur
  SetRotBlurPower(LoadCfgDef(cfg,"rotblur",DefaultRotBlurPower));
 
  base::LoadConfig(cfg,fcfg, section);
}
void EngineDD9::SaveConfig(ParamFile &cfg, const char *section)
{
  if (!IsOutOfMemory())
  {
#if !XBOX_D3D
    {
      // global config
      ParamFile fcfg;
      ParseFlashpointCfg(fcfg);

      ParamEntryPtr posCfg = fcfg.GetClassInterface();
      if (section && *section)
      {
        ParamEntryPtr secCfg = fcfg.FindEntry(section);
        if (secCfg) posCfg = secCfg;
        else
        {
          posCfg = fcfg.AddClass(section).GetPointer();
        }
      }
      if (posCfg)
      {
        posCfg->Add("Resolution_W",_fullW);
        posCfg->Add("Resolution_H",_fullH);
        posCfg->Add("winX", _winX);
        posCfg->Add("winY", _winY);
        posCfg->Add("winW", _winW);
        posCfg->Add("winH", _winH);

        posCfg->Add("winDefW",_WindowAspectW);
        posCfg->Add("winDefH",_WindowAspectH);
      }
      if (!_windowed)
      {
        fcfg.Add("Resolution_Bpp",_pixelSize);
        fcfg.Add("refresh",_refreshRate);
      }
      fcfg.Add("Render_W",_wRTPreferred);
      fcfg.Add("Render_H",_hRTPreferred);
      fcfg.Add("FSAA",_fsAAWanted);
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
			fcfg.Add("ssaoEnabled", _ssaoEnabled);
      fcfg.Add("AToC", _AToCMask);
#if _ARMA3_SIMUL_WEATHER
      fcfg.Add("SWLayersCount", _swLayersCount);
#endif
#if _HELISIM_RENDER_TO_TEXTURE
      fcfg.Add("PipQuality", _r2tQuality);
#endif
#if _HELISIM_AMBIENT_TRAFFIC
      fcfg.Add("AmbientTraffic", Glob.config.ambientTraffic);
#endif

      if (_pp._fxaa)
        fcfg.Add("PPAA", _ppaaQuality);

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
    if (_pp._bloom)
      cfg.Add("bloom",_bloomThreshold);
    if (_pp._rotBlur)
      cfg.Add("rotblur",_rotBlurPower);

    base::SaveConfig(cfg, section);
  }
}

void EngineDD9::Clear(bool clearZ, bool clear, PackedColor color, bool clearStencil)
{
  PROFILE_DX_SCOPE(3dCLR);
  D3DRECT rect;
  rect.x1=0,rect.y1=0;
  rect.x2=_w,rect.y2=_h;
  int flags=0;
  if (clear) flags|=D3DCLEAR_TARGET;
  if (clearZ)
  {
    // when doing FSAA, depthbuffer is sometimes disabled
    if (!_depthBufferRT || _currRenderTargetDepthBufferSurface)
    {
      flags|=D3DCLEAR_ZBUFFER;
      if (_caps._hasStencilBuffer && clearStencil) flags|=D3DCLEAR_STENCIL;
    }
  }

  if (flags)
  {
    CALL_D3D(Clear,0,NULL,flags,color,1,0);
  }
}

void EngineDD9::WorkToBack()
{
}

#if _ARMA3_SIMUL_WEATHER
void EngineDD9::UpdateSimulWeatherTexture(TextureSysGPUDX9 *tex, unsigned textureIndex)
{
  CALL_CB_DIRECT(this, UpdateSimulWeatherTexture, tex, textureIndex);
}

void EngineDD9::DoUpdateSimulWeatherTexture(TextureSysGPUDX9 *tex, unsigned textureIndex)
{
  if (tex)
    tex->UpdateGPUTexInternal(textureIndex);
}

void EngineDD9::SetSWGeometryLayersCount(int layers) 
{
  saturate(layers, MinSWLayers, MaxSWLayers);
  _swLayersCount = layers; 
}

#endif

void EngineDD9::DoPresentBackbuffer()
{
  
  if (!_cursorSet)
  {
    ShowDeviceCursor(false);
  }

  PROFILE_SCOPE_GRF_EX(3dSwp,*,0);
  HRESULT lost = _d3DDevice->Present(NULL,NULL,NULL,NULL);
  if (lost==D3DERR_DEVICELOST || lost==D3DERR_DEVICENOTRESET || lost==D3DERR_DRIVERINTERNALERROR)
  {
    _resetStatus = lost;
    LogF("*** Device lost - Present:%x",_resetStatus);
  }
  #if !XBOX_D3D
  else if (lost==S_PRESENT_OCCLUDED) // Direct3DEx only
  {
    LogF("*** Present: Window occluded");
    // no need for any action. Once window stop to be occluded, the message will stop appearing.
  }
  else if (lost==S_PRESENT_MODE_CHANGED) // Direct3DEx only
  {
    DoAssert(_direct3DEx);
    _resetStatus = lost;
  } 
  #endif
  else if (lost!=D3D_OK)
  {
    LogF("*** Present failed:%x",lost);
  }
  // update the information about a back-buffer
  if (lost==D3D_OK)
  {
    _d3DDevice->GetRenderTarget(0, _backBuffer.Init());
    
    _currRenderTargetSurface = _backBuffer;
    _currRenderTargetDepthBufferSurface = _depthBuffer;
  }
  ResetLogDevice();
}

void EngineDD9::BackToFront()
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
    if (GInput.GetCheat3ToDo(DIK_A))
    {
      // switching not possible while any data are still pending
      // before switching we need to flush
      if (_aToCSupported!=AToC_None)
      {
        EndHighLevelRecording();
        if (_AToCMask) _useAToC = !_useAToC;
        GlobalShowMessage(1000, "Alpha to Coverage: %s", _useAToC ? "On" : "Off");
      }
    }
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
      VSCache.Clear();
      _psCache.Clear();  
  #if _ENABLE_COMPILED_SHADER_CACHE
      RString cacheName = ShaderCacheName(_sbTechnique,_caps._sm2Used,"PP");
      ShaderCompileCache cacheInit(SourcesPP, cacheName, RStringB(HLSLCacheDir));
  #endif

      CreatePostProcessStuff();
    }
    if (GInput.GetCheat1ToDo(DIK_NUMPADSTAR))
    {
      EndHighLevelRecording();
      VSCache.Clear();
      _psCache.Clear();  
  #if _ENABLE_COMPILED_SHADER_CACHE
      RString cacheName = ShaderCacheName(_sbTechnique,_caps._sm2Used,"PS");
      ShaderCompileCache cacheInit(SourcesPS, cacheName, RStringB(HLSLCacheDir));
  #endif

      InitPixelShaders(_sbTechnique);
    }
    
    if (GInput.GetCheat1ToDo(DIK_NUMPADMINUS))
    {
      EndHighLevelRecording();
      VSCache.Clear();
      _psCache.Clear();  
  #if _ENABLE_COMPILED_SHADER_CACHE
      RString cacheName = ShaderCacheName(_sbTechnique,_caps._sm2Used,"VS");
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

#endif
}


int EngineDD9::FrameTime() const
{
  return GlobalTickCount()-_frameTime;
}


void EngineDD9::ReportGRAM9(const char *name)
{
#if !defined(_X360SHADERGEN)
  MyD3DDevice &device = GetDirect3DDevice();

  if (name) LogF("VRAM report: %s",name);
  UINT dwFree = device->GetAvailableTextureMem();
  if (name) LogF("  VRAM free %u",dwFree);
#endif
}


void EngineDD9::CreateSurfaces(bool windowed, ParamEntryPar cfg, ParamEntryPar fcfg)
{
  _windowed = windowed;
  _caps._rtAllocated = 0;

  _wRT = _wRTRounded;
  _hRT = _hRTRounded;
  
  InitSurfaces(cfg, fcfg);

  // clear all backbuffers
  D3DRECT rect;
  rect.x1=0,rect.y1=0;
  rect.x2=_w,rect.y2=_h;
  int flags = D3DCLEAR_TARGET;

  _d3DDevice->Clear(0,NULL,flags,0,1,0);
  BackToFront();
  _d3DDevice->Clear(0,NULL,flags,0,1,0);
  BackToFront();
  _d3DDevice->Clear(0,NULL,flags,0,1,0);
  BackToFront();

  ReportGRAM9("After creating Frame-Buffer");
}

void EngineDD9::DestroySurfaces(bool destroyTexBank)
{
  Assert(!IsCBRecording());
  // this needs to be done for each CB
  for (int cb=-1; cb<CBCount(); cb++)
  {
    RendState &cbState = CBState(cb);
    ForgetDeviceBuffers(cb);

    // Free device buffers in case of reset - Should the ForgetDeviceBuffers function release them, these lines are not necessary
    cbState._vBufferLast = VertexStaticData9::BufferFuture(VertexStaticData9::BufferFuture::Null); // last SetStreamSource
    cbState._iBufferLast = IndexStaticData9::BufferFuture(IndexStaticData9::BufferFuture::Null); // last SetIndices
    cbState._strideLast = 0;
  }

  DoneSurfaces(destroyTexBank);
}

void EngineDD9::Pause()
{
}

EngineDD9::EngineDD9()
  : DefaultBloomThreshold(0.75f), DefaultRotBlurPower(0.015f)
{
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

  const int memSize = 8*1024*1024;
  _bg.InitMem(memSize);

  _resetStatus = D3D_OK;
  _resetRequested = false;
  _bloomThreshold = DefaultBloomThreshold;
  _rotBlurPower = DefaultRotBlurPower;
}

EngineDD9::~EngineDD9()
{
  Done();
  _bg.ClearMem();
  struct VerifyNoRef
  {
    bool operator () (RefCount *item) const
    {
      Fail("No items should be present in EngineDDBGStoredRefs when destructing");
      return true;
    }
  };
  EngineDDBGStoredRefs.ForEach(VerifyNoRef());
  #if SAFE_RESOURCE_UNSET
  DoAssert(_bgResToRelease.Size()==0);
  DoAssert(_bgTexHandleToRelease.Size()==0);
  #endif
}

PushBuffer *EngineDD9::CreateNewPushBuffer(int key)
{
  return NULL;
  //return new PushBufferD3D9(key);
}

// Special vertex declarations

//D3DVERTEXELEMENT9 vertexDeclTransformed[] =
//{
//  {0,  0, D3DDECLTYPE_FLOAT4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITIONT,    0},
//  D3DDECL_END()
//};
//
//D3DVERTEXELEMENT9 vertexDeclPostProcess[] =
//{
//  {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
//  {0, 12, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
//  D3DDECL_END()
//};

#define D3DDECLTYPESPEC_UV D3DDECLTYPE_SHORT2

# define D3DDECLTYPESPEC_VE D3DDECLTYPE_D3DCOLOR


static D3DVERTEXELEMENT9 vertexDeclNonTL[] =
{
  {0,  0, D3DDECLTYPE_FLOAT4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,   0},
  {0, 16, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,      0},
  {0, 20, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,      1},
  {0, 24, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,   0},
  {0, 32, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,   1},
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclHeatHaze[] =
{
	{0,  0, D3DDECLTYPE_FLOAT4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,   0},
	{0, 16, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,   0},
	D3DDECL_END()
};


// Standard vertex declarations

static D3DVERTEXELEMENT9 vertexDeclPoint[] =
{
  {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
  {0, 12, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
  {0, 16, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0},
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclBasic[] =
{
  {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
  {0, 12, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0},
  {0, 16, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclNormal[] =
{
  {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
  {0, 12, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0},
  {0, 16, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
  {0, 20, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,  0},
  {0, 24, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,  1},
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclBasicUV2[] =
{
  {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
  {0, 12, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0},
  {0, 16, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
  {0, 20, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclNormalUV2[] =
{
  {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
  {0, 12, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0},
  {0, 16, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
  {0, 20, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
  {0, 24, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,  0},
  {0, 28, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,  1},
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclNormalCustom[] =
{
  {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
  {0, 12, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0},
  {0, 16, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
  {0, 20, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,  0},
  {0, 24, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,  1},
  {0, 28, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1},
#if _VBS3_MORETERRAINLODS
  {0, 40, D3DDECLTYPE_FLOAT4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 2},
#endif
#if _VBS3_LAND_SHADOW
  {0, 56, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
#endif
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclShadowVolume[] =
{
  {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
  {0, 12, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclSkinning[] =
{
  {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
  {0, 12, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
  {0, 16, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
  {0, 20, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0},
  {0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclNormalSkinning[] =
{
  {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
  {0, 12, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
  {0, 16, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
  {0, 20, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,      0},
  {0, 24, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,      1},
  {0, 28, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0},
  {0, 32, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclUV2Skinning[] =
{
  {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
  {0, 12, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
  {0, 16, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
  {0, 20, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     1},
  {0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0},
  {0, 28, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclNormalUV2Skinning[] =
{
  {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
  {0, 12, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
  {0, 16, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
  {0, 20, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     1},
  {0, 24, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,      0},
  {0, 28, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,      1},
  {0, 32, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0},
  {0, 36, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclShadowVolumeSkinning[] =
{
  {0,  0,   D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
  {0, 12,   D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0},
  {0, 16,   D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
  {0, 20,   D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     1},
  {0, 32,   D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  1},
  {0, 36,   D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 1},
  {0, 40,   D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     2},
  {0, 52,   D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  2},
  {0, 56,   D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 2},
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclInstancing[] =
{
  {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
  {0, 12, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
  {0, 16, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
  {0, 20, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclNormalInstancing[] =
{
  {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
  {0, 12, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
  {0, 16, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
  {0, 20, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,      0},
  {0, 24, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,      1},
  {0, 28, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclUV2Instancing[] =
{
  {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
  {0, 12, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
  {0, 16, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
  {0, 20, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     1},
  {0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclNormalUV2Instancing[] =
{
  {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
  {0, 12, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
  {0, 16, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
  {0, 20, D3DDECLTYPESPEC_UV,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     1},
  {0, 24, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,      0},
  {0, 28, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,      1},
  {0, 32, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclShadowVolumeInstancing[] =
{
  {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
  {0, 12, D3DDECLTYPESPEC_VE,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
  {0, 16, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
  D3DDECL_END()
};

static D3DVERTEXELEMENT9 vertexDeclSpriteInstancing[] =
{
  {0,  0, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
  D3DDECL_END()
};

// VD for simul weather clouds with fragment distance fading
static D3DVERTEXELEMENT9 vertexDeclSimulWeatherClouds[] =
{
  {0, 0	,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
  {0, 12	,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
  {0,	24	,D3DDECLTYPE_FLOAT1,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},
  {0,	28	,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,2},
  {0,	36	,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,3},
  D3DDECL_END()
};

// VD for simul weather clouds with CPU distance fading
static D3DVERTEXELEMENT9 vertexDeclSimulWeatherCloudsCPU[] =
{
  {0, 0	,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
  {0, 12	,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
  {0,	24	,D3DDECLTYPE_FLOAT1,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},
  {0,	28	,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,2},
  {0,	36	,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,3},
  {0,	48	,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,4},
  {0,	60	,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,5},
  D3DDECL_END()
};

bool EngineDD9::CreateShaderDeclarations()
{
#if _ENABLE_REPORT
  DWORD start = ::GetTickCount();
#endif

  Debugger::PauseCheckingScope scope(GDebugger);

  // Create vertex declaration objects
  ComRef<IDirect3DVertexDeclaration9> vdPoint;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclPoint, vdPoint.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }
  ComRef<IDirect3DVertexDeclaration9> vdBasic;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclBasic, vdBasic.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }
  ComRef<IDirect3DVertexDeclaration9> vdNormal;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclNormal, vdNormal.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }
  ComRef<IDirect3DVertexDeclaration9> vdBasicUV2;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclBasicUV2, vdBasicUV2.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }
  ComRef<IDirect3DVertexDeclaration9> vdNormalUV2;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclNormalUV2, vdNormalUV2.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }
  ComRef<IDirect3DVertexDeclaration9> vdNormalCustom;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclNormalCustom, vdNormalCustom.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }
  ComRef<IDirect3DVertexDeclaration9> vdShadowVolume;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclShadowVolume, vdShadowVolume.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }
  ComRef<IDirect3DVertexDeclaration9> vdSkinning;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclSkinning, vdSkinning.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }
  ComRef<IDirect3DVertexDeclaration9> vdNormalSkinning;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclNormalSkinning, vdNormalSkinning.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }
  ComRef<IDirect3DVertexDeclaration9> vdUV2Skinning;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclUV2Skinning, vdUV2Skinning.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }
  ComRef<IDirect3DVertexDeclaration9> vdNormalUV2Skinning;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclNormalUV2Skinning, vdNormalUV2Skinning.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }
  ComRef<IDirect3DVertexDeclaration9> vdShadowVolumeSkinning;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclShadowVolumeSkinning, vdShadowVolumeSkinning.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }
  ComRef<IDirect3DVertexDeclaration9> vdInstancing;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclInstancing, vdInstancing.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }
  ComRef<IDirect3DVertexDeclaration9> vdNormalInstancing;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclNormalInstancing, vdNormalInstancing.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }
  ComRef<IDirect3DVertexDeclaration9> vdUV2Instancing;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclUV2Instancing, vdUV2Instancing.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }
  ComRef<IDirect3DVertexDeclaration9> vdNormalUV2Instancing;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclNormalUV2Instancing, vdNormalUV2Instancing.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }
  ComRef<IDirect3DVertexDeclaration9> vdShadowVolumeInstancing;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclShadowVolumeInstancing, vdShadowVolumeInstancing.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }
  ComRef<IDirect3DVertexDeclaration9> vdSpriteInstancing;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclSpriteInstancing, vdSpriteInstancing.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }

  ComRef<IDirect3DVertexDeclaration9> vdNonTL;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclNonTL, vdNonTL.Init())))
  {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }

	ComRef<IDirect3DVertexDeclaration9> vdHeatHaze;
	if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclHeatHaze, vdHeatHaze.Init())))
	{
		LogF("Error in CreateVertexDeclaration");
		return false;
	}

  ComRef<IDirect3DVertexDeclaration9> vdSimulWeatherClouds;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclSimulWeatherClouds, vdSimulWeatherClouds.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }

  ComRef<IDirect3DVertexDeclaration9> vdSimulWeatherCloudsCPU;
  if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclSimulWeatherCloudsCPU, vdSimulWeatherCloudsCPU.Init()))) {
    LogF("Error in CreateVertexDeclaration");
    return false;
  }

  //////////////////////////////////////////////////////////////////////////

  // We are going to succeed - save the created objects
  {
    // Basic
    _vertexDeclD[VD_Tex0][VSDV_Basic]                                             << NULL;
    _vertexDeclD[VD_Position_Tex0_Color][VSDV_Basic]                              = vdPoint;
    _vertexDeclD[VD_Position_Normal_Tex0][VSDV_Basic]                             = vdBasic;
    _vertexDeclD[VD_Position_Normal_Tex0_ST][VSDV_Basic]                          = vdNormal;
    _vertexDeclD[VD_Position_Normal_Tex0_Tex1][VSDV_Basic]                        = vdBasicUV2;
    _vertexDeclD[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Basic]                     = vdNormalUV2;
    _vertexDeclD[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic]                   = vdNormalCustom;
    _vertexDeclD[VD_Position_Normal_Tex0_WeightsAndIndices][VSDV_Basic]           << NULL;
    _vertexDeclD[VD_Position_Normal_Tex0_ST_WeightsAndIndices][VSDV_Basic]        << NULL;
    _vertexDeclD[VD_Position_Normal_Tex0_Tex1_WeightsAndIndices][VSDV_Basic]      << NULL;
    _vertexDeclD[VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices][VSDV_Basic]   << NULL;
    _vertexDeclD[VD_ShadowVolume][VSDV_Basic]                                     = vdShadowVolume;
    _vertexDeclD[VD_ShadowVolumeSkinned][VSDV_Basic]                              << NULL;
    _vertexDeclD[VD_SimulWeatherClouds][VSDV_Basic]                               = vdSimulWeatherClouds;
    _vertexDeclD[VD_SimulWeatherCloudsCPU][VSDV_Basic]                            = vdSimulWeatherCloudsCPU;

    // Skinning
    _vertexDeclD[VD_Tex0][VSDV_Skinning]                                            << NULL;
    _vertexDeclD[VD_Position_Tex0_Color][VSDV_Skinning]                             << NULL;
    _vertexDeclD[VD_Position_Normal_Tex0][VSDV_Skinning]                            = vdSkinning;
    _vertexDeclD[VD_Position_Normal_Tex0_ST][VSDV_Skinning]                         = vdNormalSkinning;
    _vertexDeclD[VD_Position_Normal_Tex0_Tex1][VSDV_Skinning]                       = vdUV2Skinning;
    _vertexDeclD[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Skinning]                    = vdNormalUV2Skinning;
    _vertexDeclD[VD_Position_Normal_Tex0_ST_Float3][VSDV_Skinning]                  << NULL;
    _vertexDeclD[VD_Position_Normal_Tex0_WeightsAndIndices][VSDV_Skinning]          << NULL;
    _vertexDeclD[VD_Position_Normal_Tex0_ST_WeightsAndIndices][VSDV_Skinning]       << NULL;
    _vertexDeclD[VD_Position_Normal_Tex0_Tex1_WeightsAndIndices][VSDV_Skinning]     << NULL;
    _vertexDeclD[VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices][VSDV_Skinning]  << NULL;
    _vertexDeclD[VD_ShadowVolume][VSDV_Skinning]                                    = vdShadowVolumeSkinning;
    _vertexDeclD[VD_ShadowVolumeSkinned][VSDV_Skinning]                             << NULL;
    _vertexDeclD[VD_SimulWeatherClouds][VSDV_Skinning]                               << NULL;
    _vertexDeclD[VD_SimulWeatherCloudsCPU][VSDV_Skinning]                            << NULL;

    // Instancing
    _vertexDeclD[VD_Tex0][VSDV_Instancing]                                            = vdSpriteInstancing;
    _vertexDeclD[VD_Position_Tex0_Color][VSDV_Instancing]                             << NULL;
    _vertexDeclD[VD_Position_Normal_Tex0][VSDV_Instancing]                            = vdInstancing;
    _vertexDeclD[VD_Position_Normal_Tex0_ST][VSDV_Instancing]                         = vdNormalInstancing;
    _vertexDeclD[VD_Position_Normal_Tex0_Tex1][VSDV_Instancing]                       = vdUV2Instancing;
    _vertexDeclD[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing]                    = vdNormalUV2Instancing;
    _vertexDeclD[VD_Position_Normal_Tex0_ST_Float3][VSDV_Instancing]                  << NULL;
    _vertexDeclD[VD_Position_Normal_Tex0_WeightsAndIndices][VSDV_Instancing]          << NULL;
    _vertexDeclD[VD_Position_Normal_Tex0_ST_WeightsAndIndices][VSDV_Instancing]       << NULL;
    _vertexDeclD[VD_Position_Normal_Tex0_Tex1_WeightsAndIndices][VSDV_Instancing]     << NULL;
    _vertexDeclD[VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices][VSDV_Instancing]  << NULL;
    _vertexDeclD[VD_ShadowVolume][VSDV_Instancing]                                    = vdShadowVolumeInstancing;
    _vertexDeclD[VD_ShadowVolumeSkinned][VSDV_Instancing]                             << NULL;
    _vertexDeclD[VD_SimulWeatherClouds][VSDV_Instancing]                               << NULL;
    _vertexDeclD[VD_SimulWeatherCloudsCPU][VSDV_Instancing]                            << NULL;
  }

  // Non-TL shader & declaration
  _vdNonTL = vdNonTL;
	_vdHeatHaze = vdHeatHaze;
  Assert(!IsCBRecording());
  const int cb = -1;
  RendState &cbState = CBState(cb);

  // make sure tracking pointers do not show to old objects
  cbState._vertexDecl.Free();
  cbState._vertexDeclLast.Free();

  // Set TL to defined state
#ifndef _X360SHADERGEN
  DoSwitchTL(cb,TLDisabled);
#endif

#if _ENABLE_REPORT
  DWORD time = ::GetTickCount()-start;
  LogF("CreateShaderDeclarations: %d ms",time);
#endif

  return true;
}

void EngineDD9::DestroyShaderDeclarations()
{
  for (int i = 0; i < VDCount; i++)
  {
    for (int j = 0; j < VSDVCount; j++)
    {
      _vertexDeclD[i][j].Free();
    }
  }
  _vsNonTL.Free();
  _vdNonTL.Free();
	_vdHeatHaze.Free();
}

/* Create post process effect */
Ref<EngineDD9::PostProcess> EngineDD9::CreatePostprocess(PostEffectType pEType)
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
          if (clrPP && clrPP->Init(ppRes, true)) { clrPP->SetLevel(EngineDD9::PPLForce); postProcess = clrPP; }
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
        if (rbPP && rbPP->Init(ppRes, true)) { rbPP->SetLevel(EngineDD9::PPLVeryLow); postProcess = rbPP; }
      }
    }
    break;

  case PEDynamicBlur:
    {
      PPDynBlurResources *ppRes = const_cast<PPDynBlurResources *>(_pp._dynamicBlur->Resources());

      if (ppRes)
      {
        Ref<PPDynamicBlur> wdPP = new PPDynamicBlur;
        if (wdPP && wdPP->Init(ppRes, _pp._gaussianBlur, true)) { wdPP->SetLevel(EngineDD9::PPLVeryLow); postProcess = wdPP; }
      }
    }
    break;

  case PEWetDistortion:
    {
      PPWetDistortResources *ppRes = const_cast<PPWetDistortResources *>(_pp._wetDistort->Resources());

      if (ppRes)
      {
        Ref<PPWetDistort> wdPP = new PPWetDistort;
        if (wdPP && wdPP->Init(ppRes, _pp._gaussianBlur, true)) { wdPP->SetLevel(EngineDD9::PPLLow); postProcess = wdPP; }
      }
    }
    break;

  case PEChromAberration:
    {
      PPResourcesExt *ppRes = const_cast<PPResourcesExt *>(_pp._chromAber->Resources());

      if (ppRes)
      {
        Ref<PPChromAber> wdPP = new PPChromAber;
        if (wdPP && wdPP->Init(ppRes, true)) { wdPP->SetLevel(EngineDD9::PPLLow); postProcess = wdPP; }
      }
    }
    break;

  case PEFilmGrain:
    {
      PPFilmGrainResources *ppRes = const_cast<PPFilmGrainResources *>(_pp._filmGrain->Resources());

      if (ppRes)
      {
        Ref<PPFilmGrain> wdPP = new PPFilmGrain;
        if (wdPP && wdPP->Init(ppRes, true)) { wdPP->SetLevel(EngineDD9::PPLVeryLow); postProcess = wdPP; }
      }
    }
    break;

	case PEGodRays:
		{
			PPGodRaysResources *ppRes = const_cast<PPGodRaysResources *>(_pp._godRays->Resources());

			if (ppRes)
			{
				Ref<PPGodRays> wdPP = new PPGodRays;
				if (wdPP && wdPP->Init(ppRes, true)) { wdPP->SetLevel(EngineDD9::PPLVeryLow); postProcess = wdPP; }
			}
		}
		break;

	case PERain:
		{
			PPRainResources *ppRes = const_cast<PPRainResources *>(_pp._rain->Resources());

			if (ppRes)
			{
				Ref<PPRain> wdPP = new PPRain;
				if (wdPP && wdPP->Init(ppRes, true)) { wdPP->SetLevel(EngineDD9::PPLVeryLow); postProcess = wdPP; }
			}
		}
	break;


  case PEColorInv:
    {
      PPResourcesExt *ppRes = const_cast<PPResourcesExt *>(_pp._clrInvers->Resources());

      if (ppRes)
      {
        Ref<PPColorsInversion> cliPP = new PPColorsInversion;
        if (cliPP && cliPP->Init(ppRes, true)) { cliPP->SetLevel(EngineDD9::PPLLow); postProcess = cliPP; }
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

/* Create post process effect */
int EngineDD9::CreatePostprocess(PostEffectType pEType, int priority)
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
	else if(strcmpi(str, "GodRays") == 0)
	{
		return PEGodRays;
	}
	else if(strcmpi(str, "Rain") == 0)
	{
		return PERain;
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

int EngineDD9::CreatePostprocess(RString peTypeName, int priority)
{
  PostEffectType peType = ConvertStrToPE(peTypeName);

  if (peType == PENone) return InvalidPPHndl._hndl;

  return CreatePostprocess(peType, priority);
}

bool EngineDD9::NewPostProcessStuff(bool firstInit)
{
  // Get fragments from specified file
  QOStrStreamDataPrefixedLine psSource(PSFile);
  QOStrStreamDataPrefixedLine vsSource(FPShaders);

	Ref<PPEnfShapeCollectionRes> ppEnfShapeCollectionRes = new PPEnfShapeCollectionRes;
	if (!ppEnfShapeCollectionRes->Init(psSource,vsSource))
	{
		LogF("Error: Enf debug shape creation failed.");
		return false;
	}
	
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
	Ref<PPGodRays> ppGodRays = new PPGodRays;
	if (!ppGodRays->Init(psSource, vsSource, firstInit))
	{
		LogF("Error: Post process effect (GodRays) creation failed.");
		return false;
	}
	Ref<PPRain> ppRain= new PPRain;
	if (!ppRain->Init(psSource, vsSource, firstInit))
	{
		LogF("Error: Post process effect (Rain) creation failed.");
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

  Ref<PPFXAA> ppFxaa = new PPFXAA();
  if (!ppFxaa->Init(psSource,vsSource))
  {
    LogF("Error: Post process effect creation failed.");
    return false;
  }
  
  Ref<PPSMAA> ppSmaa = new PPSMAA();
  if (!ppSmaa->Init(psSource, vsSource, ppFxaa->GetSharedRT0(), ppFxaa->GetSharedRT1()))
  {
    LogF("Error: Post process effect SMAA creation failed.");
    return false;
  }

  ppBloom->SetLevel(EngineDD9::PPLLow);
  ppGlow->SetLevel(EngineDD9::PPLLow);
  ppDOF->SetLevel(EngineDD9::PPLNormal);
  ppSSAO->SetLevel(EngineDD9::PPLLow);
  ppDistanceDOF->SetLevel(EngineDD9::PPLNormal);
  ppRotBlur->SetLevel(EngineDD9::PPLLow);
  ppFilmGrain->SetLevel(EngineDD9::PPLVeryLow);
  ppGodRays->SetLevel(EngineDD9::PPLForce);
	ppRain->SetLevel(EngineDD9::PPLForce);
  ppRadialBlur->SetLevel(EngineDD9::PPLVeryLow);
  ppColors->SetLevel(EngineDD9::PPLForce);
  ppChromAber->SetLevel(EngineDD9::PPLLow);
  ppWetDistort->SetLevel(EngineDD9::PPLLow);
  ppDynamicBlur->SetLevel(EngineDD9::PPLVeryLow);
  ppColorsInvers->SetLevel(EngineDD9::PPLLow);
#if ENABLE_NVG_NOISE
  ppNVGNoiseFilm->SetLevel(EngineDD9::PPLLow);
#endif

	ppGodRays->SetPriority(PPGodRaysPriority);
	ppRain->SetPriority(PPRainPriority);
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
  // We are going to succeed - save the created objects
  _pp._final = ppFinal;
  _pp._bicubic = ppBicubic;
  _pp._gaussianBlur = ppGaussianBlur;
  _pp._bloom = ppBloom;
  _pp._SSSM = ppSSSM;
  _pp._SSSMStencil = ppSSSMStencil;
#ifdef SSSMBLUR
  _pp._SSSMB = ppSSSMB;
#endif
  _pp._DOF = ppDOF;
  _pp._distantDOF = ppDistanceDOF;
  _pp._filmGrain = ppFilmGrain;
	_pp._godRays = ppGodRays;
	_pp._rain = ppRain;
	_pp._enfShapeCollectionRes = ppEnfShapeCollectionRes;

	//should always be (doesn't see now any reason why not)
	_pp._rain->Enable(true);
	_pp._godRays->Enable(true);

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
  _pp._fxaa = ppFxaa;
  _pp._smaa = ppSmaa;

  SetPPAAQuality(_ppaaQuality);

#if ENABLE_NVG_NOISE
  _pp._nvgNoise = ppNVGNoiseFilm;
#endif

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

  return true;
}


bool EngineDD9::CreatePostProcessStuff()
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

// initialize post effect - called after device reset
class InitPPHndlFunc: public IPPHndlFunc
{
private:
  EngineDD9 *_engineDD9;
  Ref<EngineDD9::PPGaussianBlur> _gBlur;

public:
  InitPPHndlFunc(EngineDD9 *engineDD9, Ref<EngineDD9::PPGaussianBlur> gBlur): _engineDD9(engineDD9), _gBlur(gBlur) {};

  bool operator () (PostProcessHndl &hndl)
  {
    if (hndl == InvalidPPHndl || _engineDD9 == NULL) return false;

    switch (hndl._type)
    {
   case PEFilmGrain:
     {
       // original effect 
       EngineDD9::PPFilmGrain *chAPP = dynamic_cast<EngineDD9::PPFilmGrain*> (hndl._pp.GetRef());

	     if (chAPP && _engineDD9->_pp._filmGrain)
	     {
		     // get resources
		     EngineDD9::PPFilmGrainResources *res = const_cast<EngineDD9::PPFilmGrainResources *> (_engineDD9->_pp._filmGrain->Resources());

         if (res && !chAPP->Init(res, false))
         {
           LogF("Error: Post process effect 'PPFilmGrain' initialization failed");
           return false;
         }
         chAPP->SetLevel(EngineDD9::PPLVeryLow);
         return true;
       }
     }
     return false;

	 case PEGodRays:
		 {
			 // original effect 
			 EngineDD9::PPGodRays *chAPP = dynamic_cast<EngineDD9::PPGodRays*> (hndl._pp.GetRef());

			 if (chAPP && _engineDD9->_pp._godRays)
			 {
				 // get resources
				 EngineDD9::PPGodRaysResources *res = const_cast<EngineDD9::PPGodRaysResources *> (_engineDD9->_pp._godRays->Resources());

				 if (res && !chAPP->Init(res, false))
				 {
					 LogF("Error: Post process effect 'PPGodRays' initialization failed");
					 return false;
				 }
				 chAPP->SetLevel(EngineDD9::PPLVeryLow);
				 return true;
			 }
		 }
		 return false;

	 case PERain:
		 {
			 // original effect 
			 EngineDD9::PPRain *chAPP = dynamic_cast<EngineDD9::PPRain*> (hndl._pp.GetRef());

			 if (chAPP && _engineDD9->_pp._rain)
			 {
				 // get resources
				 EngineDD9::PPRainResources *res = const_cast<EngineDD9::PPRainResources *> (_engineDD9->_pp._rain->Resources());

				 if (res && !chAPP->Init(res, false))
				 {
					 LogF("Error: Post process effect 'PPRain' initialization failed");
					 return false;
				 }
				 chAPP->SetLevel(EngineDD9::PPLVeryLow);
				 return true;
			 }
		 }
		 return false;

    case PEChromAberration:
      {
        // original effect 
        EngineDD9::PPChromAber *chAPP = dynamic_cast<EngineDD9::PPChromAber*> (hndl._pp.GetRef());

        if (chAPP && _engineDD9->_pp._chromAber)
        {
          // get resources
          EngineDD9::PPResourcesExt *res = const_cast<EngineDD9::PPResourcesExt *> (_engineDD9->_pp._chromAber->Resources());

          if (res && !chAPP->Init(res, false))
          {
            LogF("Error: Post process effect 'PPChromAber' initialization failed");
            return false;
          }
          chAPP->SetLevel(EngineDD9::PPLLow);
          return true;
        }
      }
      return false;

    case PEColorCorrections:
      {
        EngineDD9::PPColors *cPP = dynamic_cast<EngineDD9::PPColors*> (hndl._pp.GetRef());

        if (cPP && _engineDD9->_pp._colors)
        {
          // get resources
          EngineDD9::PPColorResources *res = const_cast<EngineDD9::PPColorResources *> (_engineDD9->_pp._colors->Resources());
          
          if (res && !cPP->Init(res, false))
          {
            LogF("Error: Post process effect 'PPColors' initialization failed");
            return false;
          }
          cPP->SetLevel(EngineDD9::PPLForce);
          return true;
        }
      }
      return false;

    case PERadialBlur:
      {
        EngineDD9::PPRadialBlur *rbPP = dynamic_cast<EngineDD9::PPRadialBlur*> (hndl._pp.GetRef());

        if (rbPP && _engineDD9->_pp._radialBlur)
        {
          EngineDD9::PPResourcesExt *res = const_cast<EngineDD9::PPResourcesExt *>(_engineDD9->_pp._radialBlur->Resources());
          
          if (res && !rbPP->Init(res, false))
          {
            LogF("Error: Post process effect 'PPRadialBlur' initialization failed");
            return false;
          }
          rbPP->SetLevel(EngineDD9::PPLVeryLow);
          return true;
        }
      }
      return false;

    case PEDynamicBlur:
      {
        EngineDD9::PPDynamicBlur *dbPP = dynamic_cast<EngineDD9::PPDynamicBlur *> (hndl._pp.GetRef());

        if (dbPP && _engineDD9->_pp._dynamicBlur)
        {
          EngineDD9::PPDynBlurResources *res = const_cast<EngineDD9::PPDynBlurResources *> (_engineDD9->_pp._dynamicBlur->Resources());
        
          if (res && !dbPP->Init(res, _gBlur, false))
          {
            LogF("Error: Post process effect 'PPDynamicBlur' initialization failed");
            return false;
          }
          dbPP->SetLevel(EngineDD9::PPLVeryLow);
          return true;
        }
      }
      return false;

    case PEWetDistortion:
      {
        EngineDD9::PPWetDistort *wdPP = dynamic_cast<EngineDD9::PPWetDistort *> (hndl._pp.GetRef());

        if (wdPP && _engineDD9->_pp._wetDistort)
        {
          EngineDD9::PPWetDistortResources *res = const_cast<EngineDD9::PPWetDistortResources *> (_engineDD9->_pp._wetDistort->Resources());

          if (res && !wdPP->Init(res, _gBlur, false))
          {
            LogF("Error: Post process effect 'PPWetDistort' initialization failed");
            return false;
          }
          wdPP->SetLevel(EngineDD9::PPLLow);
          return true;
        }
      }
      return false;

    case PEColorInv:
      {
        // original effect 
        EngineDD9::PPColorsInversion *cliPP = dynamic_cast<EngineDD9::PPColorsInversion*> (hndl._pp.GetRef());

        if (cliPP && _engineDD9->_pp._clrInvers)
        {
          // get resources
          EngineDD9::PPResourcesExt *res = const_cast<EngineDD9::PPResourcesExt *> (_engineDD9->_pp._clrInvers->Resources());

          if (res && !cliPP->Init(res, false))
          {
            LogF("Error: Post process effect 'PPColorInversion' initialization failed");
            return false;
          }
          cliPP->SetLevel(EngineDD9::PPLLow);
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



bool EngineDD9::InitPostProcessStuff(bool firstInit)
{
#if _ENABLE_REPORT
  DWORD start = ::GetTickCount();
#endif
  Log("CreatePostProcessStuff");
  Debugger::PauseCheckingScope scope(GDebugger);
  if (!NewPostProcessStuff(firstInit))
  {
    return false;
  }

  InitPPHndlFunc initFunc(this, _pp._gaussianBlur);
  ForEachPostEffectHndl(initFunc);

  //////////////////////////////////////////////////////////////////////////

#if _ENABLE_REPORT
  DWORD time = ::GetTickCount()-start;
  LogF("InitPostProcessStuff: %d ms",time);
#endif

  ForgetAvgIntensity();

  return true;
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

void EngineDD9::DestroyPostProcessStuff()
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
  _pp._godRays.Free();
	_pp._rain.Free();
	_pp._enfShapeCollectionRes.Free();
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
#if ENABLE_NVG_NOISE
  _pp._nvgNoise.Free();
#endif
  _pp._fxaa.Free();
  _pp._smaa.Free();

  _spp._hit.Free();
  _spp._movement.Free();
  _spp._tired.Free();

  DestroyPPHndlFunc destFunc;
  ForEachPostEffectHndl(destFunc);

  _pp._effects.Clear();
  _pp._effectsThis.Clear();
}

// release D3D resources
class ReleasePPHndlFunc: public IPPHndlFunc
{
  bool operator () (PostProcessHndl &hndl)
  {
    if (hndl._pp) 
    { 
      EngineDD9::PostProcess *postProcess = static_cast<EngineDD9::PostProcess*>(hndl._pp.GetRef());
      if (postProcess) postProcess->UnInit();
    }
    return true;
  }
};

void EngineDD9::ReleasePostProcessStuff()
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
	_pp._godRays.Free();
	_pp._rain.Free();
	_pp._enfShapeCollectionRes.Free();
  _pp._filmGrain.Free();
  _pp._ssao.Free();
  _pp._final.Free();
  _pp._bicubic.Free();
  _pp._rotBlur.Free();
#if ENABLE_NVG_NOISE
  _pp._nvgNoise.Free();
#endif
  _pp._fxaa.Free();
  _pp._smaa.Free();
  
  // release resources allocated by post effects, no classes are destroyed
  if (_pp._radialBlur) _pp._radialBlur->UnInit();
  if (_pp._colors) _pp._colors->UnInit();
  if (_pp._chromAber) _pp._chromAber->UnInit();
  if (_pp._wetDistort) _pp._wetDistort->UnInit();
  if (_pp._dynamicBlur) _pp._dynamicBlur->UnInit();
  if (_pp._clrInvers) _pp._clrInvers->UnInit();

  _spp._hit.Free();
  _spp._movement.Free();
  _spp._tired.Free();

  ReleasePPHndlFunc releaseFunc;
  ForEachPostEffectHndl(releaseFunc);
  _pp._effectsThis.Clear();
}

/*!
\patch 5156 Date 5/15/2007 by Ondra
- Fixed: Better VRAM allocation estimations for nVidia 8800 card, resulting in less frequent missing textures.

\patch 5160 Date 5/23/2007 by Ondra
- Fixed: Improved VRAM managment for ATI cards with Catalyst 7.3 or newer.
*/
void EngineDD9::InitTexMemEstimations()
{
  // make sure the experiment is done only once
  if (_caps._localPredictionSet) return;
  _caps._localPredictionSet = true;

  D3DADAPTER_IDENTIFIER9 id;
  _direct3D->GetAdapterIdentifier(D3DAdapter,0,&id);
  { // set default values based on device id
    _caps._localPrediction = true;
    // by default no throttling
    _caps._vramAllocLimit = INT_MAX;

    // by default assume some reasonable values
    _caps._vbAlignment = 4096;
    _caps._texAlignment = 256;

	 // if we do not know which memory is used, we need to assume worse case, i.e. the local one
    _caps._dynamicVBNonLocal = false;
    _caps._staticVBNonLocal = false;
    _caps._ibNonLocal = false;

    // vendor specific optimization - local/non-local VRAM handling
    if (id.VendorId==0x10DE)
    {
      // on nVidia we assume dynamic vertex buffers go into a non-local memory
      _caps._dynamicVBNonLocal = true;
      _caps._staticVBNonLocal = false;
      // it seems index buffers go into non-local memory
      // however if they do, our runtime should detect it
      // therefore we rather assume worse case
      _caps._ibNonLocal = false;

      _caps._vbAlignment = 4096;
      _caps._texAlignment = 32;
    }
    else if (id.VendorId==0x1002) // ATI
    {
      // tested with 6.7 drivers:
      // dynamic vertex buffers go into a non-local memory
      // with 6.8 drivers then go into a local one
      // runtime test should catch it
      _caps._dynamicVBNonLocal = true;
      _caps._staticVBNonLocal = false;
      // index buffers go into local memory on ATI
      _caps._ibNonLocal = false;

      // experiments have shown 4K alignment for VB and 32B for textures
      _caps._vbAlignment = 4096;
      _caps._texAlignment = 32;

    }

  }


  if (_caps._wddm)
  {
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
    LogF("Vista detected - static Tex/VB VRAM limit %d B",_caps._vramAllocLimit);
  }
  else if (_ddraw && _caps._localPrediction)
  {
    // measure allocation requirements for following object types
    // dynamic vertex buffer
    StatisticsById vbDynCounts;
    // static vertex buffer
    StatisticsById vbStatCounts;
    // static index buffer
    StatisticsById ibStatCounts;
    // texture
    StatisticsById texCounts;
    // fall-back solution, in case small texture texture allocation learned nothing
    StatisticsById bigTexCounts;

    // as  we are not sure we are using the device in exclusive mode,
    // we will perform several experiments and use the most common result

    for (int i=0; i<10; i++)
    {
      {
        int localBeg,nonlocalBeg;
        CheckFreeTextureMemory(localBeg,nonlocalBeg);
        ComRef<IDirect3DTexture9> tex;
        if (_d3DDevice->CreateTexture(4, 4, 1, 0, D3DFMT_A1R5G5B5, D3DPOOL_DEFAULT, tex.Init(), NULL) == D3D_OK)
        {
          int localEnd,nonlocalEnd;
          CheckFreeTextureMemory(localEnd,nonlocalEnd);
          texCounts.Count(StatisticsById::Id(localBeg-localEnd,nonlocalBeg-nonlocalEnd));
        }
      }
      {
        int localBeg,nonlocalBeg;
        CheckFreeTextureMemory(localBeg,nonlocalBeg);
        ComRef<IDirect3DTexture9> tex;
        if (_d3DDevice->CreateTexture(256, 256, 0, 0, D3DFMT_A1R5G5B5, D3DPOOL_DEFAULT, tex.Init(), NULL) == D3D_OK)
        {
          int localEnd,nonlocalEnd;
          CheckFreeTextureMemory(localEnd,nonlocalEnd);
          bigTexCounts.Count(StatisticsById::Id(localBeg-localEnd,nonlocalBeg-nonlocalEnd));
        }
      }
      {
        int localBeg,nonlocalBeg;
        CheckFreeTextureMemory(localBeg,nonlocalBeg);
        ComRef<IDirect3DIndexBuffer9> buf;
        if (_d3DDevice->CreateIndexBuffer(64*1024, WRITEONLYIB,D3DFMT_INDEX16,D3DPOOL_DEFAULT, buf.Init(), NULL) == D3D_OK)
        {
          /*
          // attempt Lock / Unlock to make sure the memory is really allocated - did not work on GeForce 6800 anyway
          void *data = NULL;
          Lock(buf,0,32,&data,0);
          buf->Unlock();
          */

          int localEnd,nonlocalEnd;
          CheckFreeTextureMemory(localEnd,nonlocalEnd);
          ibStatCounts.Count(StatisticsById::Id(localBeg-localEnd,nonlocalBeg-nonlocalEnd));
        }
      }
      {
        int localBeg,nonlocalBeg;
        CheckFreeTextureMemory(localBeg,nonlocalBeg);
        ComRef<IDirect3DVertexBuffer9> buf;
        if (_d3DDevice->CreateVertexBuffer(32, WRITEONLYVB,0,D3DPOOL_DEFAULT, buf.Init(), NULL) == D3D_OK)
        {
          int localEnd,nonlocalEnd;
          CheckFreeTextureMemory(localEnd,nonlocalEnd);
          vbStatCounts.Count(StatisticsById::Id(localBeg-localEnd,nonlocalBeg-nonlocalEnd));
        }
      }
      {
        int localBeg,nonlocalBeg;
        CheckFreeTextureMemory(localBeg,nonlocalBeg);
        ComRef<IDirect3DVertexBuffer9> buf;
        if (_d3DDevice->CreateVertexBuffer(64*1024, WRITEONLYVB|D3DUSAGE_DYNAMIC,0,D3DPOOL_DEFAULT, buf.Init(), NULL) == D3D_OK)
        {
          /*
          // attempt Lock / Unlock to make sure the memory is really allocated - did not work on ATI X800 anyway
          void *data = NULL;
          Lock(buf,0,32,&data,0);
          buf->Unlock();
          */
          int localEnd,nonlocalEnd;
          CheckFreeTextureMemory(localEnd,nonlocalEnd);
          vbDynCounts.Count(StatisticsById::Id(localBeg-localEnd,nonlocalBeg-nonlocalEnd));
        }
      }

    }

    // select most common value
    vbDynCounts.SortData(false);
    vbStatCounts.SortData(false);
    ibStatCounts.SortData(false);
    texCounts.SortData(false);

    bool vista = true;
    if (texCounts.Size()>0)
    {
      const StatisticsById::Item &most = texCounts.Get(0);
      // we expect a significant majority of some value
      // if not, something is wrong and we cannot expect to be able to predict anything
      if (most.count>=7)
      {
        int local = most.id._id1, nonlocal = most.id._id2;
        (void)local,(void)nonlocal;
        // we assume only one of them should be used
        if (local>nonlocal)
        {
          // the value we see should be the alignment
          _caps._texAlignment = local;
          vista = false;
        }
        else if (nonlocal>local)
        {
          // textures not in local memory - do not try to predict anything
          _caps._localPrediction = false;
          vista = false;
        }
        else
        {
          const StatisticsById::Item &most = bigTexCounts.Get(0);
          // we expect a significant majority of some value
          // if not, something is wrong and we cannot expect to be able to predict anything
          if (most.count>=7)
          {
            // when using a big texture, we do not know the alignment, but we should know local/nonlocal
            int local = most.id._id1, nonlocal = most.id._id2;
            (void)local,(void)nonlocal;
            if (local>0)
            {
              LogF("Local alignment not known");
            }
            else if (nonlocal>0)
            {
              _caps._localPrediction = false;
            }
            else if (local==0)
            {
              LogF("Local/non-local detection failed, assuming Vista");
              _caps._texAlignment = 4096;
            }
          }
        }
      }
    }

    if (vbStatCounts.Size()>0)
    {
      const StatisticsById::Item &most = vbStatCounts.Get(0);
      // we expect a significant majority of some value
      // if not, something is wrong and we cannot expect to be able to predict anything
      if (most.count>=7)
      {
        int local = most.id._id1, nonlocal = most.id._id2;
        (void)local,(void)nonlocal;
        // we assume only one of them should be used
        if (local>0 && nonlocal==0)
        {
          // the value we see should be the alignment
          _caps._vbAlignment = local;
          _caps._staticVBNonLocal = false;
          vista = false;
        }
        else
        {
          if (local==0)
          {
            if (nonlocal==0)
            {
              LogF("VB static location detection failed, assuming Vista");
              _caps._vbAlignment = 4096;
            }
            else
            {
              LogF("VB static location now known, using default");
            }
          }

          // static buffers not in local memory - we do not care much about alignment
          _caps._staticVBNonLocal = true;
        }
      }
    }
    if (vbDynCounts.Size()>0)
    {
      const StatisticsById::Item &most = vbDynCounts.Get(0);
      // we expect a significant majority of some value
      // if not, something is wrong and we cannot expect to be able to predict anything
      if (most.count>=7)
      {
        int local = most.id._id1, nonlocal = most.id._id2;
        (void)local,(void)nonlocal;
        // we assume only one of them should be used
        // if buffers not in local memory - we do not care much about alignment, or where they really are
        _caps._dynamicVBNonLocal = local == 0;
        if (local==0 && nonlocal==0)
        {
          LogF("VB dynamic location now known, assuming Vista");
        }
        else
        {
          vista = false;
        }
      }
    }
    if (ibStatCounts.Size()>0)
    {
      const StatisticsById::Item &most = ibStatCounts.Get(0);
      // we expect a significant majority of some value
      // if not, something is wrong and we cannot expect to be able to predict anything
      if (most.count>=7)
      {
        int local = most.id._id1, nonlocal = most.id._id2;
        (void)local,(void)nonlocal;
        // if buffers not in local memory - we do not care much about alignment, or where they really are
        _caps._ibNonLocal = local==0 && nonlocal>0;
        if (local==0 && nonlocal==0)
        {
          LogF("IB static location now known, assuming Vista");
        }
        else
        {
          vista = false;
        }
      }
    }
    LogF("IB: %s",_caps._ibNonLocal ? "nonlocal" : "local");
    LogF("VB static: %d B, %s",_caps._vbAlignment,_caps._staticVBNonLocal ? "nonlocal" : "local");
    LogF("VB dynamic: %s",_caps._dynamicVBNonLocal ? "nonlocal" : "local");
    LogF("Tex: %d B, %s",_caps._texAlignment,_caps._localPrediction ? "local" : "nonlocal");

    if (id.VendorId==0x1002 || vista)
    {
      // ATI Catalyst
      // local prediction is working very well with 6.7 or older drivers
      // we do not care much for drivers between - they have the Process Handle leak anyway
      // Cat. 7.3 seems to be identified as 6.14.10.6677 (ati2dvag.dll)
      // driver is supposed to be very good in resource placement
      // but only when running fullscreen
      const long long cat7_3_version = 0x0006000e000a1a15LL;
      static bool forceStaticVRAM = false;
      if (forceStaticVRAM || vista || id.DriverVersion.QuadPart>=cat7_3_version && !_windowed)
      { // Catalyst 7.3
        _caps._localPrediction = false;
        // still we want only a few textures and buffers to be allocated in AGP
        // avoid 32b overflow while computing with large 32b values
        long long vramAllocLimit = to64bInt(1.2f*_caps._localVramAmount);
        long long maxVram = _caps._localVramAmount+to64bInt(0.8f*_caps._nonlocalVramAmount);
        if (vramAllocLimit>maxVram) vramAllocLimit = maxVram;
        vramAllocLimit -= _caps._rtAllocated;
        if (vramAllocLimit>INT_MAX) vramAllocLimit = INT_MAX;
        _caps._vramAllocLimit = int(vramAllocLimit);
        if (vista)
        {
          LogF("Vista detected - static Tex/VB VRAM limit %d B",_caps._vramAllocLimit);
        }
        else
        {
          LogF("ATI Catalyst 7.3 or newer detected - static Tex/VB VRAM limit %d B",_caps._vramAllocLimit);
        }
      }
    }
  }
}


bool ParseUserParams(ParamFile &cfg, GameDataNamespace *globals);
void SaveUserParams(ParamFile &cfg);

/*!
\patch 5145 Date 3/23/2007 by Ondra
- Fixed: When running windowed, on some systems the image was slightly blurred.
\patch 5153 Date 4/4/2007 by Jirka
- Fixed: Game running in full screen had invisible but clickable window areas (close, minimize)
*/


bool EngineDD9::Init(HINSTANCE hInst, HINSTANCE hPrev, int sw, bool windowed, int bpp, bool generateShaders)
{
  // set cb even here so that we can use it consistently everywhere
  Assert(!IsCBRecording());
  
  // we need immediate CB + one per each CPU (thread)
  _cbState.Resize(GetCPUCount()+1);
  
  Assert(_cbState.Size()>1);
  Assert(_cbState.Size()<=32);
  
  const int cb = -1;
  RendState &cbState = CBState(cb);

  // Instance handle
  _hInst = hInst;

  // Set 
  _AToCMask = AToC_EnableAll;
#if _ARMA3_SIMUL_WEATHER
  _swLayersCount = DefaultSWLayers;
#endif

  _ppaaQuality = 0;
#if _HELISIM_RENDER_TO_TEXTURE
  _r2tQuality = 0;
#endif
  _vsync = true;
  _pixelSize = bpp;
  _refreshRate = 0;
  _fsAAWanted = 0;
  _afWanted = 0;
  _maxFSAA = 15;
  _postFxQuality = 0;
	_ssaoEnabled = 0;
  _postFXLevelMask = CalculatePostEffectsMask(0);
  _hdrPrecWanted = HDR8;
  cbState._bias = 0;
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
  _resetStatus = D3D_OK;
  _resetDone = false;
  _lastKnownAvgIntensity = -1.0f;
  _accomFactorPrev = -1;
  _clearColor = Color(1, 0, 1, 1); // Use the clear color purple to see potential problem

  // Set initial value for pixel shader
  cbState._pixelShaderSel = PSUninitialized;
  cbState._pixelShaderTypeSel = PSTUninitialized;

  _caps._sm2Used = false;

  // using textures and VBs for free on demand handling is somewhat risky, and makes MT safety harder (system allocator used)
  // however, we need time to be recorded for VBs
  RegisterFreeOnDemandSystemMemory(this);

  //_formatSet=SingleTex;
  _renderMode = RMTris;
  cbState._tlActive = TLDisabled;
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

#if _VBS3
  _tunnelVisionCoef = 0.0f;
  _fbGetScreen = false;
  _fbCurrentIntensity = 0.0f;
  _fbInvDuration = 0.0f;
#endif

  // 3D rendering state parameter
  _3DRenderingInProgress = false;
  _end3DRequested = true;

  // Set the device type
  if (generateShaders)
  {
	#if !defined(_X360SHADERGEN)
    _devType = D3DDEVTYPE_NULLREF; // prefer NULLREF to avoid dependence on SDK
	#else
    _devType = D3DDEVTYPE_COMMAND_BUFFER;
	#endif
  }
  else
  {
    _devType = D3DDEVTYPE_HAL;
  }
    
  if (!CreateD3D())
  {
    return false;
  };

  InitAdapter();
  InitDirectDraw7(D3DAdapter);



  // we need to ask using DX7 here
  // in case DX7 fails, provide some reasonable estimation
  long long vramAmount = 256*1024*1024;
  if (_ddraw)
  {
    DDSCAPS2 caps;
    DWORD totalmem = 0,freemem = 0;
    caps.dwCaps = DDSCAPS_LOCALVIDMEM|DDSCAPS_VIDEOMEMORY;
    caps.dwCaps2 = 0;
    caps.dwCaps3 = 0;
    caps.dwCaps4 = 0;
    _ddraw->GetAvailableVidMem(&caps,&totalmem,&freemem);
    vramAmount = totalmem;
    if (vramAmount<128*1024*1024)
    {
      DWORD totalmem = 0,freemem = 0;
      caps.dwCaps = DDSCAPS_NONLOCALVIDMEM|DDSCAPS_VIDEOMEMORY;
      _ddraw->GetAvailableVidMem(&caps,&totalmem,&freemem);
      vramAmount += totalmem;
      _freeNonlocalVramAmount = freemem;
    }
    if (vramAmount<256*1024*1024) vramAmount = 256*1024*1024;
    // avoid 32b overflow
    if (vramAmount>INT_MAX) vramAmount = INT_MAX;
  }
  // set memory before calling SetDefaultSettingsLevel
  
  SetTextureMemory(TextBankD3D9::SelectTextureMemory(int(vramAmount)));
  // make sure values are autodetected once engine is created
  SetDefaultSettingsLevel(GetDeviceLevel());
  

  // Config loading
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  ParseUserParams(cfg, &globals);
  ParamFile fcfg;
  ParseFlashpointCfg(fcfg);
  LoadConfig(cfg,fcfg,GetConfigSection());

  EngineDD9::SetRenderStateTemp::SetEngine(this);
  EngineDD9::SetSamplerStateTemp::SetEngine(this);
  EngineDD9::SetVertexShaderConstantTemp::SetEngine(this);
  EngineDD9::SetPixelShaderConstantTemp::SetEngine(this);
  EngineDD9::PushTextureState::SetEngine(this);
	EngineDD9::PushInternall::SetEngine(this);
  EngineDD9::PostProcess::SetEngine(this);
  EngineDD9::PPGaussianBlur::SetEngine(this);
#ifdef SSSMBLUR
  EngineDD9::PPSSSMB::SetEngine(this);
#endif
  EngineDD9::PPDOF::SetEngine(this);
  EngineDD9::PPDistanceDOF::SetEngine(this);
  EngineDD9::PPFilmGrain::SetEngine(this);
	EngineDD9::PPGodRays::SetEngine(this);
	EngineDD9::PPRain::SetEngine(this);
  EngineDD9::PPSSAO::SetEngine(this);
  EngineDD9::PPGlow::SetEngine(this);
  EngineDD9::PPFlareIntensity::SetEngine(this);
  EngineDD9::ISpecialEffect::SetEngine(this);

  // raster parameters
  if (windowed)
  {
    // search for a location that is handled by selected adapter
    SearchMonitor(_winX, _winY, _winW, _winH);
    _hwndApp=AppInit(hInst, hPrev, sw, false, _winX, _winY, _winW, _winH);
    if( !_hwndApp ) {
      Log("Failed to create a window");
      DestroyD3D();
      return false;
    }

    RECT client;
    // adjust width, height to match client area
    GetClientRect(_hwndApp,&client);
    _w = client.right-client.left;
    _h = client.bottom-client.top;
    CreateSurfaces(windowed, cfg, fcfg);
    //_textBank=new TextBank(_engine);
  }
  else
  {
    _hwndApp=AppInit(hInst,hPrev,sw,true,0,0,160,160);
    if( !_hwndApp ) {
      Log("Failed to create a window");
      DestroyD3D();
      return false;
    }

    //resolution already taken from SetDefault or loadconfig
    Assert(_w!=0);
    Assert(_h!=0);
    _fullW = _w;
    _fullH = _h;

    CreateSurfaces(windowed, cfg, fcfg);
    //_textBank=new TextBank(_engine);

    if (D3DAdapter >= 0)
    {
      HMONITOR mon = _direct3D->GetAdapterMonitor(D3DAdapter);

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

  // check pixel shader support

  // Ablunda2 IDirect3DDevice->GetDeviceCaps() failed -> shaders not generate
  if (!generateShaders && _caps._pixelShaders < 0x200) {
    LogF("Error - pixel shader version 2.0 or better required");
    DestroySurfaces();
    AppDone(_hwndApp, hInst);
    DestroyD3D();
    return false;
  }

  // If shaders should be generated then generate all variations and exit
  if (generateShaders)
  {
    // format should not matter, but we need to set it to something valid so that PPGlow::Init can run
    _apertureFormat = D3DFMT_R32F;
   
		// Generate shaders
		int minShader = 0x300;
    int maxShader = 0x300;
    SBTechnique minSBTechnique = SBT_Default;
    SBTechnique maxSBTechnique = SBT_Default;

		_wRT = _w;
    _hRT = _h;
    for (int select=minShader; select<=maxShader; select+=0x100)
    {
      InitShaderModel(select);
      for (int i = minSBTechnique; i <=maxSBTechnique; i++)
      {
        // for each shader configuration make sure we are starting from the scratch
        VSCache.Clear();
        _psCache.Clear();
        // initialize engine including shaders
        Assert(!strcmp((const char *)_caps._vsVersion+1,(const char *)_caps._psVersion+1));
        {
          RString cacheName = ShaderCacheName(SBTechnique(i),_caps._sm2Used,"PP");
          ShaderCompileCache cacheInit(SourcesPP, cacheName, RStringB(HLSLCacheDir), true);
          CreatePostProcessStuff();
        }
        {
          RString cacheName = ShaderCacheName(SBTechnique(i),_caps._sm2Used,"PS");
          ShaderCompileCache cacheInit(SourcesPS, cacheName, RStringB(HLSLCacheDir), true);
          InitPixelShaders(SBTechnique(i));
        }
        {
          RString cacheName = ShaderCacheName(SBTechnique(i),_caps._sm2Used,"VS");
          ShaderCompileCache cacheInit(SourcesVS, cacheName, RStringB(HLSLCacheDir), true);
          InitVertexShaders();
          CreateShaderDeclarations();
        }
        // deinit again to be prepared for a different shader model / HW variant
        DestroyPostProcessStuff();
        DestroyShaderDeclarations();
        DeinitPixelShaders();
        DeinitVertexShaders();
      }
    }

    // Destroy application and exit
    DestroySurfaces();
    AppDone(_hwndApp, hInst);
    DestroyD3D();
    return false;
  }

  InitShaderModel();
  {
#if _ENABLE_COMPILED_SHADER_CACHE
    // +1 needed - we want to compare vs_2_0 with ps_2_0
    Assert(!strcmp((const char *)_caps._vsVersion+1,(const char *)_caps._psVersion+1));
    RString cacheName = ShaderCacheName(_sbTechnique,_caps._sm2Used,"PS");
    ShaderCompileCache cacheInit(SourcesPS, cacheName, RStringB(HLSLCacheDir));
#endif
    _renderingMode = RMCommon;
    InitPixelShaders(_sbTechnique);
  }
  {
#if _ENABLE_COMPILED_SHADER_CACHE
    // +1 needed - we want to compare vs_2_0 with ps_2_0
    Assert(!strcmp((const char *)_caps._vsVersion+1,(const char *)_caps._psVersion+1));
    RString cacheName = ShaderCacheName(_sbTechnique,_caps._sm2Used,"VS");
    ShaderCompileCache cacheInit(SourcesVS, cacheName, RStringB(HLSLCacheDir));
#endif
    InitVertexShaders();

    // Check vertex shader capabilities
    if (_caps._vertexShaders < 0x200)
    {
      LogF("Info - using SW vertex processing (VS 2.0)");
    }

    if (!CreateShaderDeclarations())
    {
      Fail("Error while creating shader declarations");
      DestroySurfaces();
      AppDone(_hwndApp, hInst);
      DestroyD3D();
      return false;
    }
  }

  {
#if _ENABLE_COMPILED_SHADER_CACHE
    // +1 needed - we want to compare vs_2_0 with ps_2_0
    Assert(!strcmp((const char *)_caps._vsVersion+1,(const char *)_caps._psVersion+1));
    RString cacheName = ShaderCacheName(_sbTechnique,_caps._sm2Used,"PP");
    ShaderCompileCache cacheInit(SourcesPP, cacheName, RStringB(HLSLCacheDir));
#endif
    if (!CreatePostProcessStuff())
    {
      LogF("Error while creating stencil shadows stuff");
      DestroyShaderDeclarations();
      DestroySurfaces();
      AppDone(_hwndApp, hInst);
      DestroyD3D();
      return false;
    }
  }

	if (!_heatHazeVB.IsValid())
		_heatHazeVB.Init(MAXIMUM_HEAT_HAZE_QUADS*6*4, sizeof(HeatHazeVertex));

  #if TRACK_SHADER_PATCH
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
      
    #define SHADOW_SHADER(ps) PSFromCache(#ps)
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

	#ifdef TEST_RAIN_MAP
		_oldLRSP[ShadowBufferCascadeLayers] = MIdentity;
	#endif

  // Single instance parameters
  SetInstanceInfo(-1, HWhite, 1.0f, false);

  // Set Initialized flag
  _initialized = true;

  InitTexMemEstimations();

  // _vramAllocLimit may have changed in InitTexMemEstimations - we want to react
  if (_textBank) _textBank->RecalculateTextureMemoryLimits();

  for (int i=0; i<CBCount(); i++)
  {
    SetHWToUndefinedState(i);
    // note: we may have gathered some instructions in the invocations lists now
    // we do not care about them at all - all we wanted to do was to set the shadow state
    // this is done here for clarity only - StartCBRecording would do the same anyway 
    CBState(i)._rec.Reset();
  }

  // remember undefined state for later use: switching to undefined means only copying the state
  //_undefined = _cbState[0];

  // Success
  return true;
}

void EngineDD9::Init()
{
  base::Init();
  _textBank->ResetMaxTextureQuality(true);
  // no need to do it here, it will be done later. Doing it here causes flush in EnableSBShadowsCommit
  //_textBank->InitDetailTextures();
}

void EngineDD9::Done()
{

  // Done only if initialization succeeded
  if (_initialized)
  {
    //release textures before TextBank is destroyed!
    //TODO: should it be done for all postprocesses?
		if(_pp._godRays)
			_pp._godRays->Cleanup();

		if(_pp._rain)
			_pp._rain->Cleanup();

    _vBufferToRelease.Clear();
    
    UnsetAllDiscardableResources();
    // perform destruction which might still submit more work to a worker thread
    _fonts.Clear();
    DestroyTextBank();
    DeinitPixelShaders();

    // Destroy shader fragments allocations
    DestroyShaderDeclarations();

    // Destroy surfaces
    DestroySurfaces();

    // as we are going to destroy the device, we need to reset the caches as well
    VSCache.Clear();
    _psCache.Clear();  
    
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

    // Destroy D3D object
    DestroyD3D();

    // Test whether there remains some memory allocated for index and vertex buffers
    Assert(_iBuffersAllocated==0);
    Assert(_vBuffersAllocated==0);
    //Assert(_rtAllocated==0);
    _caps._rtAllocated = 0;


    // Set the flag
    _initialized = false;
  }
}

void EngineDD9::DisplayWaitScreen()
{
  LOG_MEM(__FUNCTION__);
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

void EngineDD9::PreReset(bool hard)
{
  LOG_MEM(__FUNCTION__);
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
  // some things need to be reset for each CB to make sure they do no hang around any more
  for (int cb=-1; cb<CBCount(); cb++)
  {
    RendState &cbState = CBState(cb);

    ForgetDeviceBuffers(cb);

    // Free device buffers in case of reset - Should the ForgetDeviceBuffers function release them, these lines are not necessary
    cbState._vBufferLast = VertexStaticData9::BufferFuture(VertexStaticData9::BufferFuture::Null); // last SetStreamSource
    cbState._iBufferLast = IndexStaticData9::BufferFuture(IndexStaticData9::BufferFuture::Null); // last SetIndices
    cbState._strideLast = 0;

    for (int i = MaxSamplerStages - 1; i >= 0; i--) SetTexture(cb, i, NULL);
    SetTexture0Color(cb, HWhite,HWhite,true);
  }

#if _ARMA3_SIMUL_WEATHER
  DeInitSimulWeather();
#endif

	if (_heatHazeVB.IsValid())
		_heatHazeVB.Clear();

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

  _backBuffer.Free();
  _depthBuffer.Free();
  _depthBufferRT.Free();
  _renderTargetD.Free();
  _renderTargetSurfaceD.Free();
  _renderTargetDepthInfo.Free();
  _renderTargetDepthInfoSurface.Free();
  _renderTargetDepthInfoZ.Free();
  _renderTargetS.Free();
  _renderTargetSurfaceS.Free();
  _currRenderTargetDepthBufferSurface.Free();
  _currRenderTargetSurface.Free();
  _currRenderTarget.Free();
  _cursorSurface.Free();
  DestroyTIResources();
#if _HELISIM_RENDER_TO_TEXTURE
  DestroyR2TResources();
#endif
  DestroyResources();
  DestroySBResources();

	#ifdef TEST_RAIN_MAP
		DestroyRainMapResources();
	#endif

  //_frontBuffer.Free();
  //_zBuffer.Free();

  DeinitPixelShaders();
  DeinitVertexShaders();

  ReleasePostProcessStuff();
  //DestroyPostProcessStuff();
  DestroyShaderDeclarations();

  DestroyVB();
  DestroyVBTL();

  Assert(_iBuffersAllocated==0);
  Assert(_vBuffersAllocated==0);
  //Assert(_rtAllocated==0);
  _caps._rtAllocated = 0;
}

void EngineDD9::PostReset(bool hard)
{
  _wRT = _wRTRounded;
  _hRT = _hRTRounded;
  
#if 0 //_ENABLE_CHEATS
  PrintVMMap(1,"PostReset Start");
#endif
  CreateVB();
  CreateVBTL();

  bool fail = false;

  // set cb even here so that we can use it consistently everywhere
  Assert(!IsCBRecording());
  const int cb = -1;
  RendState &cbState = CBState(cb);
  // reset all render/texture states to undefined
  for (int p=0; p<NPredicationModes; p++) cbState._renderState[p].Clear();
  if (!hard)
  {
    Init3DState();
  }
  CheckLocalNonlocalTextureMemory();
  {
#if _ENABLE_COMPILED_SHADER_CACHE
    RString cacheName = ShaderCacheName(_sbTechnique,_caps._sm2Used,"PS");
    ShaderCompileCache cacheInit(SourcesPS, cacheName, RStringB(HLSLCacheDir));
#endif
    InitPixelShaders(_sbTechnique);
  }
  {
#if _ENABLE_COMPILED_SHADER_CACHE
    RString cacheName = ShaderCacheName(_sbTechnique,_caps._sm2Used,"VS");
    ShaderCompileCache cacheInit(SourcesVS, cacheName, RStringB(HLSLCacheDir));
#endif
    InitVertexShaders();
    if (!CreateShaderDeclarations()) fail = true;
  }
  if (_sbQuality>0)
  {
    CreateSBResources();
  }
	
	#ifdef TEST_RAIN_MAP
		CreateRainMapResources();
	#endif

  // Create common resources
  CreateResources();

#if _HELISIM_RENDER_TO_TEXTURE
  CreateR2TResources();
#endif

  // Create TI resources
  CreateTIResources();

  _dbQuality = _postFxQuality>1 ? 1 : 0;

  {
#if _ENABLE_COMPILED_SHADER_CACHE
    RString cacheName = ShaderCacheName(_sbTechnique,_caps._sm2Used,"PP");
    ShaderCompileCache cacheInit(SourcesPP, cacheName, RStringB(HLSLCacheDir));
#endif
	const bool recreatePPEffects = hard;
	InitPostProcessStuff(recreatePPEffects);
  }

  if (fail)
  {
    ErrorMessage("Unable to restore vertex/pixel shaders.");
  }

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

	if (!_heatHazeVB.IsValid())
		_heatHazeVB.Init(MAXIMUM_HEAT_HAZE_QUADS*6*4, sizeof(HeatHazeVertex));

#if _ARMA3_SIMUL_WEATHER
  InitSimulWeather();
#endif

}


bool EngineDD9::Reset()
{
  LOG_MEM(__FUNCTION__);
  Assert(CheckMainThread());
    
  Debugger::PauseCheckingScope scope(GDebugger);

  bool inScene = InitDrawDone();
  if (inScene) FinishDraw(1);

  #if 0 // TODO: implement fast Reset for Vista
  if (_direct3DEx)
  {
    EndHighLevelRecording();
    EndBackgroundScope(true,"reset");
    
    // we need to recreate render targets and everything derived from them (this includes PP effects)
    Done3DState();
    // TODO: recreate (or adjust) PP effects
    // on Direct3D9Ex Reset is fast and simple operation
    LogF("Reset to resolution %dx%d",_presentPars.BackBufferWidth,_presentPars.BackBufferHeight);
    HRESULT resErr = _d3DDevice->Reset(&_presentPars);
    if (SUCCEEDED(resErr))
    {
      Init3DState();
      // reset recreated the swap chain - we need to update
      _d3DDevice->GetDepthStencilSurface(_depthBuffer.Init());
      _d3DDevice->GetRenderTarget(0, _backBuffer.Init());
      _currRenderTargetSurface = _backBuffer;
      _currRenderTargetDepthBufferSurface = _depthBuffer;
      // some effects and resources need to be recreated to react for changed backbuffer size
      return true;
    }
    LogF("Direct3D9Ex light Reset failed, performing a full one");
  }
  else
  #endif
  {
    PreReset(false);

    LogF("Reset to resolution %dx%d",_presentPars.BackBufferWidth,_presentPars.BackBufferHeight);
    LogF("Reset to vsync %d",_presentPars.PresentationInterval);
    for(;;)
    {
      HRESULT ok = _d3DDevice->Reset(&_presentPars);
      if (ok==D3D_OK)
      {
        // we did the reset, hence we can zero the "how much is reset needed" information
        break;
      }
      if (ok==D3DERR_DEVICENOTRESET || ok==D3DERR_DEVICELOST)
      {
        LogF("Reset failed ... waiting to retry.");
        Sleep(1000);
      }
      else
      {
        LogF("*** Reset failed (%x)",ok);
        // we started resetting - we should finish it
        _resetStatus = D3DERR_DEVICENOTRESET;

  #if !defined(_X360SHADERGEN)
        // Report TestCooperativeLevel result
        HRESULT clErr = _d3DDevice->TestCooperativeLevel();
        LogF("*** Cooperative level returns (%x)", clErr);
        if (clErr == D3DERR_DEVICENOTRESET)
        {
          _d3DDevice.DetachDevice();
          ErrorMessage("Error: Device reset failed, error %x",ok);
        }
  #endif

        return false;
      }
    }

  #if _GAMES_FOR_WINDOWS
    XLiveOnResetDevice(&_presentPars);
  #endif

    LogF("*** Reset done");
    // Recreate all surfaces
    PostReset(false);
  }
  
  
  if (inScene) InitDraw();
  return true;
}

bool EngineDD9::ResetHard()
{
  Debugger::PauseCheckingScope scope(GDebugger);

  bool inScene = InitDrawDone();
  if (inScene) FinishDraw(1);
  PreReset(true);

  // destroy surfaces and device
  DestroyPostProcessStuff();
  DestroyShaderDeclarations();
  DestroySurfaces(false);

  // as we are going to destroy the device, we need to reset the caches as well
  VSCache.Clear();
  _psCache.Clear();  
  // Surfaces creation
  {
    // Config loading
    GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
    ParamFile cfg;
    ParseUserParams(cfg, &globals);
    ParamFile fcfg;
    ParseFlashpointCfg(fcfg);
    // do not load config - only pass it to the CreateSurfaces, which will pass it to texture ban
    //LoadConfig(cfg,fcfg);

    // Surfaces creation
    CreateSurfaces(_windowed, cfg, fcfg);
  }

  LogF("*** Reset done (hard)");



  PostReset(true);
  if (inScene) InitDraw();
  return true;
}

bool EngineDD9::SetResolutionWanted(int w, int h, int bpp)
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

bool EngineDD9::SetFillrateLevelWanted(int level)
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

void EngineDD9::AdjustWindowAttributes()
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

void EngineDD9::BeginOptionsVideo()
{
  Assert(!_inVideoOptions);
  _inVideoOptions = true;
  AdjustWindowAttributes();
}
void EngineDD9::EndOptionsVideo()
{
  Assert(_inVideoOptions);
  _inVideoOptions = false;
  AdjustWindowAttributes();
}

void EngineDD9::DoSwitchRes(bool reset3DSize)
{
  D3DDEVICE_CREATION_PARAMETERS pars;
  _d3DDevice->GetCreationParameters(&pars);
  int adapter = pars.AdapterOrdinal;
  FindMode(adapter);
  if (reset3DSize) SetDefaultFillrateControl();
  // some old drivers were buggy - ResetHard was used here as a workaround for fullscreen chain
  Reset();
}

bool EngineDD9::SwitchRes(int x, int y, int w, int h, int bpp)
{
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
  DoSwitchRes(true);

  return true;
}


void EngineDD9::KeepAspect(int &newW, int &newH)
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

void EngineDD9::SetAspect(int newW, int newH)
{
  #if !XBOX_D3D
  _WindowAspectW = newW;
  _WindowAspectH = newH;
  #endif
}

bool EngineDD9::SwitchWindowed(bool windowed, int w, int h, int bpp)
{
#if !XBOX_D3D
  if (windowed == _windowed)
  {
    if (!windowed && w>0 && h>0 && bpp>0) return SwitchRes(_winX,_winY,w,h,bpp);
    return true;
  }

  _windowed = windowed;
  _presentPars.Windowed = windowed;
  D3DDEVICE_CREATION_PARAMETERS pars;
  _d3DDevice->GetCreationParameters(&pars);
  int adapter = pars.AdapterOrdinal;
  FindMode(adapter);

  void SetUseWindow(bool value);
  if (windowed)
  {
    LogF("Switching to window %d,%d: %dx%d",_winX,_winY,_winW,_winH);

    AppStateChangeBeg(_hwndApp,!windowed,!_inVideoOptions,_winX,_winY,_winW,_winH);
    _w = _winW;
    _h = _winH;
    DoSwitchRes(false);
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
    
    AppStateChangeBeg(_hwndApp,!windowed,!_inVideoOptions,0,0,_w,_h);
    DoSwitchRes(false);
    AppStateChangeEnd(_hwndApp,!windowed,!_inVideoOptions,0,0,_w,_h);
  }
#endif
  return true;
}

bool EngineDD9::SwitchRefreshRate(int refresh)
{
  if (_presentPars.Windowed) return false;
  if (refresh==0) return false;
  if (_refreshRate==refresh) return true;
  _refreshRate = refresh;
  D3DDEVICE_CREATION_PARAMETERS pars;
  _d3DDevice->GetCreationParameters(&pars);
  int adapter = pars.AdapterOrdinal;
  FindMode(adapter);
  Reset();
  return true;
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

void EngineDD9::ListResolutions(FindArray<ResolutionInfo> &ret)
{
  ret.Clear();

#if !ENABLE_WINDOWED_SWITCH
  if (_presentPars.Windowed)
  {
    // No resolution change possible
    ResolutionInfo info;
    info.w = info.h = 0;
    info.bpp = 0;
    ret.AddUnique(info);
    return;
  }
#endif

#if !defined(_X360SHADERGEN)
  // Enumerate resolutions available on current device
  D3DDEVICE_CREATION_PARAMETERS pars;
  _d3DDevice->GetCreationParameters(&pars);
  int adapter = pars.AdapterOrdinal;

  // Go through supported formats
  for (int formatIndex = 0; formatIndex < lenof(supportedFormats); formatIndex++) {

    // Go through available modes
    for (int i = 0; i < (int)_direct3D->GetAdapterModeCount(adapter, supportedFormats[formatIndex]._format); i++)
    {
      D3DDISPLAYMODE mode;
      _direct3D->EnumAdapterModes(adapter, supportedFormats[formatIndex]._format, i, &mode);

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

void EngineDD9::ListRefreshRates(FindArray<int> &ret)
{
  ret.Clear();
#if !defined(_X360SHADERGEN)
  if (_presentPars.Windowed)
  {
    ret.Add(0); // Return default
    // No refresh change possible
    return;
  }
  // List refresh rates for current w,h,bpp
  D3DDEVICE_CREATION_PARAMETERS pars;
  _d3DDevice->GetCreationParameters(&pars);
  int adapter = pars.AdapterOrdinal;

  // Go through supported formats
  for (int formatIndex = 0; formatIndex < lenof(supportedFormats); formatIndex++) {

    // Go through available modes
    for (int i = 0; i<(int)_direct3D->GetAdapterModeCount(adapter, supportedFormats[formatIndex]._format); i++)
    {
      D3DDISPLAYMODE mode;
      _direct3D->EnumAdapterModes(adapter, supportedFormats[formatIndex]._format, i, &mode);

      // Mode contains resolution and refresh rate
      if
        (
        mode.Width==Width2D() && mode.Height==Height2D() &&
        FormatToBpp(mode.Format)==PixelSize()
        )
      {
        ret.AddUnique(mode.RefreshRate);
      }
    }
  }
#endif
}

void EngineDD9::SetLimitedViewport(float viewSize)
{
#if _ENABLE_CHEATS
  D3DVIEWPORT9 viewData;
  memset(&viewData,0,sizeof(viewData));
  viewData.X = 0;
  viewData.Y = 0;
  viewData.MinZ = _minZRange;
  viewData.MaxZ = _maxZRange;

  _viewW = toInt(_wRT*viewSize);
  _viewH = toInt(_hRT*viewSize);

  viewData.Width  = _viewW;
  viewData.Height = _viewH;
  _d3DDevice->SetViewport(viewData);
#endif
}

QueryId::QueryId()
{
  for (int i=0; i<MaxQueries; i++) _used[i] = false;
  _curUsed = 0;
}

int QueryId::Open()
{
  // Create the query objects
  for (int i=0; i<MaxQueries-1; i++)
  {
    if (_curUsed>=MaxQueries) _curUsed=0;
    if (!_used[_curUsed])
    {
      if (!_query[_curUsed])
      {
        HRESULT hr = GEngineDD->GetDirect3DDevice()->CreateQuery(D3DQUERYTYPE_OCCLUSION, _query[_curUsed].Init());
        if (!SUCCEEDED(hr)) return -1;
      }
      _used[_curUsed] = true;
      int id = _curUsed++;
      return id;
    }
    _curUsed++;
  }
  // no valid query found
  return -1;
}
void QueryId::Close(int id)
{
  _used[id] = false;
}


#include <Es/Memory/normalNew.hpp>
/// Xbox implementation of GPU Visibility Query
class D3DVisibilityQuery: public IVisibilityQuery
{
  friend class EngineDD9;

  enum QueryStatus
  {
    /// query was not started
    QueryNone,
    /// query was already started
    QueryStarted,
    /// we already have the result
    QueryDone
  };

  enum {NResults=6};

  DWORD _index[NResults];
  int _result[NResults];
  QueryStatus _status[NResults];
  int _toStart;

	mutable int _lastValidValue;
	mutable int _lastLatency;

	void CheckResults();

public:
  D3DVisibilityQuery();
  ~D3DVisibilityQuery();

  virtual bool IsVisible() const;
  int GetVisiblePixels(bool *error = NULL) const;

	int GetLastLatency() const
	{
		return _lastLatency;
	}
	
	void BeginIssue(int cb);
	void EndIssue(int cb);

  USE_FAST_ALLOCATOR
};
#include <Es/Memory/debugNew.hpp>

DEFINE_FAST_ALLOCATOR(D3DVisibilityQuery)

D3DVisibilityQuery::D3DVisibilityQuery()
{
  _toStart = 0;
	_lastValidValue = 0;
	_lastLatency = 0;

  for (int i=0; i<NResults; i++)
  {
    _index[i] = -1;
    _status[i] = QueryNone;
    _result[i] = INT_MIN;
  }
}

D3DVisibilityQuery::~D3DVisibilityQuery()
{
  for (int i=0; i<NResults; i++)
  {
    if (_status[i] == QueryStarted)
    {
      GEngineDD->_queryIDs.Close(_index[i]);
    }
  }
}

bool D3DVisibilityQuery::IsVisible() const
{
  return GetVisiblePixels() > 0;
}

int D3DVisibilityQuery::GetVisiblePixels(bool *error) const
{
  unconst_cast(this)->CheckResults();

  // return most recent result we have
  int recent = _toStart;

  for (int i = 0; i<NResults; i++)
  {
    if (--recent<0) recent = NResults-1;
    if (_status[recent]==QueryDone && _result[recent] != INT_MIN)
    {
			_lastValidValue = _result[recent];
			_lastLatency = i+1;

			if (error)
				*error = false;

      return _lastValidValue;
    }
  }

	if (error)
		*error = true;

  // we do not know - query did not finish  
	_lastLatency = INT_MAX;
	return _lastValidValue;
}

void D3DVisibilityQuery::CheckResults()
{
  for (int i = 0; i < NResults; i++)
  {
    if (_status[i] == QueryStarted)
    {
      // check the result
      UINT pixels = 0;
      int id = _index[i];
      HRESULT ret = GEngineDD->_queryIDs.GetQuery(id)->GetData(&pixels, sizeof(DWORD), D3DGETDATA_FLUSH);

			//have data
      if (ret == S_OK)
      {
        _result[i] = pixels;
        GEngineDD->_queryIDs.Close(id);
        _status[i] = QueryDone;
      }
      else if (ret != S_FALSE)
      {
        _result[i] = INT_MIN;
        GEngineDD->_queryIDs.Close(id);
        _status[i] = QueryDone;
      }
    }
  }
}


void D3DVisibilityQuery::BeginIssue(int cb)
{
	//CheckResults();

	int index = _toStart;
	do 
	{
		index = (index >= NResults - 1) ? 0 : index + 1;

		if (_status[index] != QueryStarted)
			break;

	} while (index != _toStart);

	if (index == _toStart)
	{
			return;
	}

	int newID = GEngineDD->_queryIDs.Open();
	if (newID == -1)
	{
		LogF("Error, no new query available");
		return;
	}
			
	_index[index]  = newID;
	_result[index] = -1;
	_status[index] = QueryStarted;
	
	_toStart = index;

	GEngineDD->QueryIssue(cb, newID, D3DISSUE_BEGIN);
}


void D3DVisibilityQuery::EndIssue(int cb)
{
	Assert(_status[_toStart] == QueryStarted);	

	int newID = _index[_toStart];
	GEngineDD->QueryIssue(cb, newID, D3DISSUE_END);
}


IVisibilityQuery *EngineDD9::BegPixelCounting(int cb, IVisibilityQuery *query)
{
  D3DVisibilityQuery *q = static_cast<D3DVisibilityQuery *>(query);
  if (!q) 
		q = new D3DVisibilityQuery;
 
	if (q)
		q->BeginIssue(cb);

  return q;
}

void EngineDD9::EndPixelCounting(int cb, IVisibilityQuery *query)
{
	if (!query)
		return;

  static_cast<D3DVisibilityQuery *>(query)->EndIssue(cb);
 }


bool EngineDD9::IsCursorSupported() const
{
  return _caps._canHWMouse || _windowed;
  //return false;
}



#define DEBUG_CURSOR 0

void EngineDD9::ShowDeviceCursor(bool showIt)
{
  if (_cursorShown!=showIt)
  {
    _cursorShown = showIt;
  }
#if !defined(_X360SHADERGEN)
  bool show = _cursorShown;
  #if DEBUG_CURSOR
    show = true;
  #endif
  #if _VBS3
    extern bool ForceSimulation;
    if (!ForceSimulation)
  #endif
    {
      //DIAG_MESSAGE(5000, "%s mouse cursor", show ? "Show" : "Hide");
       _d3DDevice->ShowCursor(show);
    }
#endif
}

bool EngineDD9::IsCursorShown() const
{
  // if cursor is currently not shown, no need to clip
  return _cursorShown;
}

/*!
\patch 5127 Date 2/7/2007 by Jirka
- Fixed: Mouse cursor color was not handled correctly
*/

void EngineDD9::SetCursor(Texture *cursor, int x, int y, float hsX, float hsY, PackedColor color, int shadow)
{
#if !defined(_X360SHADERGEN)
  if (cursor!=NULL) _cursorSet = true;
  /*
  if (x!=_cursorX || y!=_cursorY)
  {
  _d3DDevice->SetCursorPosition(x,y,D3DCURSOR_IMMEDIATE_UPDATE);
  _cursorX = x;
  _cursorY = y;
  }
  */
  if (cursor==NULL)
  {
    ShowDeviceCursor(false);

#if DEBUG_CURSOR

    int w = 32;
    int h = 32;
    _d3DDevice->CreateOffscreenPlainSurface(
      w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,_cursorSurface.Init(),NULL
      );

    if (_cursorSurface.NotNull())
    {
      // we have 4444 data now
      // we need to convert them to 8888, but with discrete alpha
      D3DLOCKED_RECT rect;
      _cursorSurface->LockRect(&rect,NULL,0);
      DWORD *tgt = (DWORD *)rect.pBits;
      for (int i=0; i<w*h; i++)
      {
        *tgt = 0xffffffff;
        tgt++;
      }
      _cursorSurface->UnlockRect();
      _d3DDevice->SetCursorProperties(16,16,_cursorSurface);
    }
    _cursorTex = NULL;
#endif

  }
  else
  {
    bool ok = true;
    // when cursor is not owned, we cannot change its shape
    if (_cursorOwned && (cursor!=_cursorTex || color != _cursorColor || shadow != _cursorShadow || _cursorSurface.IsNull()))
    {
      // assume not OK unless proven otherwise
      ok = false;
      TextureD3D9 *newCursorTex = static_cast<TextureD3D9 *>(cursor);
      newCursorTex->LoadHeaders();
      _cursorTex = newCursorTex;
      _cursorColor = color;
      _cursorShadow =  shadow;

      if (newCursorTex->Width(0)!=32 || newCursorTex->Height(0)!=32)
      {
        LogF("Cursor texture %s not 32x32",cc_cast(newCursorTex->GetName()));
      }
      // we may need to transform the cursor from Alpha format to 32x32 ARGB8888 surface
      // A needs to be 0 or 255

      // this is most easily done by reading into system memory
      int level = 0;
      // find closest to 32x32 mipmap
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

      // adjust the data as needed
      // most likely they are DXT5 now
      PacLevelMem mipD = mip;
      switch (mipD._dFormat)
      {
      case PacDXT3:
      case PacDXT5:
        mipD._dFormat = PacARGB4444;
        break;
      case PacARGB1555:
      case PacARGB4444:
      case PacARGB8888:
        break;
      default:
        RptF("Unsupported cursor format for %s",cc_cast(newCursorTex->GetName()));
        break;
      }
      int size = PacLevelMem::MipmapSize(mipD._dFormat,mipD._w,mipD._h);
      mipD._pitch = size/h;

      mem.Realloc(size);
      mem.Resize(size);

      // get data from the texture source
      newCursorTex->_src->GetMipmapData(mem.Data(),mipD,level,0);

      _d3DDevice->CreateOffscreenPlainSurface(
        w,h,D3DFMT_A8R8G8B8,D3DPOOL_SCRATCH,_cursorSurface.Init(),NULL
        );

      if (_cursorSurface.NotNull())
      {
        float colorR = color.R8() / 255.0f;
        float colorG = color.G8() / 255.0f;
        float colorB = color.B8() / 255.0f;
        // we have 4444 data now
        // we need to convert them to 8888, but with discrete alpha
        D3DLOCKED_RECT rect;
        _cursorSurface->LockRect(&rect,NULL,0);
        char *tgt = (char *)rect.pBits;
        const WORD *src = (const WORD *)mem.Data();
        if (mipD._dFormat==PacARGB1555)
        {
          for (int y=0; y<h; y++)
          {
            DWORD *tgtLine = (DWORD *)tgt;
            for (int x=0; x<w; x++)
            {
              int a= ((*src)&0x8000)!=0 ? 255 : 0;
              int r= toInt((((*src)>>10)&0x1f)*(255.0/31)*colorR);
              int g= toInt((((*src)>>5)&0x1f)*(255.0/31)*colorG);
              int b= toInt((((*src)>>0)&0x1f)*(255.0/31)*colorB);
              *tgtLine = (a<<24)|(r<<16)|(g<<8)|b;
              //*tgt = 0x01010101;
              tgtLine++,src++;
            }
            tgt += rect.Pitch;
          }
        }
        else if (mipD._dFormat=PacARGB4444)
        {
          for (int y=0; y<h; y++)
          {
            DWORD *tgtLine = (DWORD *)tgt;
            for (int x=0; x<w; x++)
            {
              int a= (((*src)>>12)&0xf)>2 ? 255 : 0;
              int r= toInt((((*src)>>8)&0xf)*(255.0/15)*colorR);
              int g= toInt((((*src)>>4)&0xf)*(255.0/15)*colorG);
              int b= toInt((((*src)>>0)&0xf)*(255.0/15)*colorB);
              *tgtLine = (a<<24)|(r<<16)|(g<<8)|b;
              //*tgt = 0x01010101;
              tgtLine++,src++;
            }
            tgt += rect.Pitch;
          }
        }
        else // PacARGB8888
        {
          const DWORD *src32 = (const DWORD *)mem.Data();
          for (int y=0; y<h; y++)
          {
            DWORD *tgtLine = (DWORD *)tgt;
            for (int x=0; x<w; x++)
            {
              int a= ((*src32)>>24)>0x7f ? 255 : 0;
              int rgb = (*src32)&0xffffff;
              *tgtLine = (a<<24)|rgb;
              tgtLine++,src32++;
            }
            tgt += rect.Pitch;
          }
        }

        _cursorSurface->UnlockRect();
        if (w==32 && h==32)
        {
          int xs = toInt(w*hsX);
          int ys = toInt(h*hsY);
          _d3DDevice->SetCursorProperties(xs,ys,_cursorSurface);
          //DIAG_MESSAGE(5000,"SetCursorProperties");
          ok = true;
        }
      }
    }
    if (_cursorOwned)
    ShowDeviceCursor(ok);
  }
#endif
}

void EngineDD9::UpdateCursorPos(int x, int y)
{
  PROFILE_SCOPE_EX(updCP, *)
  if (x!=_cursorX || y!=_cursorY)
  {
    // force updates? (Does it matter)
#if !defined(_X360SHADERGEN)
    CALL_D3D(SetCursorPosition,x,y,D3DCURSOR_IMMEDIATE_UPDATE);
#endif
    _cursorX = x;
    _cursorY = y;
  }
}

void EngineDD9::CursorOwned(bool owned)
{
  if (_cursorOwned!=owned)
  {
    // when cursor is not owned, we should let the system cursor to be used
    //ShowDeviceCursor(owned);
    //if (!HideCursor) ShowCursor 
  }
  //DIAG_MESSAGE(5000,Format("Cursor %sowned",owned ? "" : "not "));
}




void EngineDD9::PlayCB(const RenderCBToPlay &cb)
{
  #if PIX_NAMED_CB_SCOPES
    struct PIXScope
    {
      MyD3DDevice &device;
      
      PIXScope(MyD3DDevice &device, const char *name, DWORD color)
      :device(device)
      {
        ::ProfileBeginGraphScope(color,name);
      }
      ~PIXScope()
      {
        ::ProfileEndGraphScope();
      }
    } scope(_d3DDevice,cb._scopeName,0);
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
        _d3DDevice._cb.Execute<true>(tempSerial);
      }
      else
      {
        _d3DDevice._cb.Execute<false>(tempSerial);
      }
    }

    offset = end;
  }
}

inline void EngineDD9::BGThreadProcess()
{
  PROFILE_SCOPE_EX(bgD3D,*)
  
  while (_bg._results.CountPending(ProcessResults)>0)
  {
    // consume one item
    const RenderCBToPlay &toRender = _bg._results.Get(ProcessResults);
    
    if (toRender.Size()>0)
    {
      // subscribe to data - see _backgroundD3D
      MemorySubscribe();
      
      PlayCB(toRender);
      
      // advancing is a way to mark data are consumed. Before doing so we must publish data - see _backgroundD3D
      MemoryPublish();
    }
    
    // report data as consumed
    _bg._results.Advance(ProcessResults);
  }
}

inline void EngineDD9::VisThreadProcess()
{
  PROFILE_SCOPE_EX(visul,*)
  
  // consume all items currently present
  _vis.Update<false>();
}


void EngineDD9::BGThread()
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

void EngineDD9::VisThread()
{
  while (_vis.OneIteration())
  {
    if (Glob.config._renderThread!=Config::ThreadingEmulated) VisThreadProcess();
  }
  // before terminating process any data still pending
  // this is important because some data may be destructed during processing
  if (Glob.config._renderThread!=Config::ThreadingEmulated) VisThreadProcess();
}


#if PIX_NAMED_CB_SCOPES
int EngineDD9::RenderCBToPlay::_currDebugId = 0;
#endif

void EngineDD9::StartBackgroundScope()
{
  const bool doBG = Glob.config._renderThread!=Config::ThreadingNone;
  Assert(_backgroundD3D!=RecHighLevel)
  if (_backgroundD3D==RecNone && doBG)
  {
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


bool EngineDD9::VerifyLowLevelThreadAccess() const
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

void EngineDD9::EndBackgroundScope(bool flushMem, const char *pixScope)
{
  if (_backgroundD3D!=RecNone)
  {
    // we need to flush
    FlushBackground(_bg,flushMem,pixScope);
    _backgroundD3D = RecNone;
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

template <>
typename EngineDD9::BGWorkerThread::BGDataType &EngineDD9::GetBackgroundData<EngineDD9::BGWorkerThread>()
{
  return _cbState[0]._rec;
}

template <class WorkerThreadClass>
bool EngineDD9::SubmitBackground(WorkerThreadClass &bg, bool flushMem, bool setEvent, CleanUpRequest needCleanUp, const char *pixScope)
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
      LOG_MEM("_resultsMem %s - new start at %d, diff %d",pixScope,bg._resultsMemUsedStart,res._memToClear);
    }
    else
    {
      res._memToClear = 0;
    }
    #if PIX_NAMED_CB_SCOPES
    res._copied = false;
    #endif

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



template <class CB_Type, int cbCount, int consumerCount>
bool EngineDD9::WorkerThread<CB_Type,cbCount,consumerCount>::ProcessBackground()
{
  // simulate background thread - this is useful for PIX, as its support of multithreaded rendering is not very good
  if (Glob.config._renderThread==Config::ThreadingEmulated) GEngineDD->ThreadProcess<WorkerThread>();
  
  Assert(GEngineDD->CheckAsync<WorkerThread>());
  // we can destruct while waiting - everything up to _bgRead can be destructed
  // this may improve latency, as after we can destruct most of the command while waiting
  int toDestroy = _results.HowMuchBehind(DestroyResults,ProcessResults);
  if (toDestroy==0) return false;
  // check how many results can be destroyed
  int canDestroy = _results.HowMuchBehind(DestroyResults,CanDestroyResults);
  if (canDestroy<toDestroy)
  {
    if (canDestroy<=0)
    {
      ErrorMessage("Internal error: Rendering command buffer to small");
      return false;
    }
    toDestroy = canDestroy;
  }
  Assert(GEngineDD->CheckThreadMemory(*this));
  while (--toDestroy>=0)
  {
    typename WorkerThread::CBType &res = _results.Get(DestroyResults);

    // if the CB is marked as noDestroy, skip it
    if (res.IsReadyToDestroy())
    {
      //TrapFor0x8000Scope lock("EngineDD9::ProcessBackground if (!res._noDestroy)");
      #if LOG_RESULTS_ALLOC
        res.LogResultsDestroy();
      #endif
      // we need to free each memory - in case it was using the dynamic fall-back
      res.Clear(_resultsMem);
      // if there is any memory to free, do it
      _resultsMem.Clear(res._memToClear);
      // once done, we can advance
      _results.Advance(DestroyResults);
      #if PIX_NAMED_CB_SCOPES
        Assert(!res._copied);
      #endif
      Assert(GEngineDD->CheckThreadMemory(*this));
    }
    else
    {
      //TrapFor0x8000Scope lock("EngineDD9::ProcessBackground * else");
      // once done, we can advance
      #if _DEBUG
      int wasEmpty = _results.EmptySpace(DestroyResults);
      #endif
      _results.Advance(DestroyResults);
      #if _DEBUG
      int isEmpty = _results.EmptySpace(DestroyResults);
      (void)isEmpty,(void)wasEmpty;
      #endif
      Assert(GEngineDD->CheckThreadMemory(*this));
      // mark the data as destroyed for easier debugging
      res._data = NULL;
      res._size = 0;
      res._memToClear = 0;
      #if PIX_NAMED_CB_SCOPES
      res._copied = false;
      #endif
      Assert(GEngineDD->CheckThreadMemory(*this));
    }
  }
  Assert(GEngineDD->CheckThreadMemory(*this));

  #if 0 // _ENABLE_REPORT
    DoAssert(GEngineDD->CheckThreadMemory(*this));
  #endif
  return true;
}

#define YieldProcessorNiceFunc() YieldProcessorNice()

template <class WorkerThreadClass>
void EngineDD9::WaitUntilSpaceAvailable(WorkerThreadClass &bg, int nResults)
{

  // spincount = 1000 corresponds approx. to 0.1 ms on 2.4 GHz Intel Core CPU
  int spinCount = 1000;
	int countToIdle = GetCPUCount()==1 ? -20000 : -200000 ; // cca 1 ms : cca 10 ms
	#if _ENABLE_REPORT
	//DWORD time = GlobalTickCount();
	#endif
  
  #if _ENABLE_CHEATS
    int used = bg._results.TotalSize()-bg._results.EmptySpace(CanDestroyResults);
    if (bg._resultsMaxUsed<used) bg._resultsMaxUsed = used; 
  #endif
  while (bg._results.EmptySpace(DestroyResults)<nResults)
  {
    if (!bg.ProcessBackground())
    {
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
bool EngineDD9::CheckThreadMemory(WorkerThreadClass &bg) const
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
void EngineDD9::FlushWaitUntilDone(WorkerThreadClass &bg, bool flushMem)
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
      LOG_MEM("*** FLUSH");
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
void EngineDD9::FlushBackground(WorkerThreadClass &bg, bool flushMem, const char *pixScope, bool waitUntilDone)
{
  if (CheckAsync<WorkerThreadClass>())
  {
    // in a typical scenario the cleanup was already performed by PreparePostprocess
    // thread termination will perform the cleanup on its own
    //UnsetAllDiscardableResources();
      
    bg.ProcessLazyDataPending();

    SubmitBackground<WorkerThreadClass>(bg,flushMem,true,CleanUpNeeded,pixScope);
    
    Assert(CheckThreadMemory(bg));
    
    #if PIX_NAMED_CB_SCOPES
    // check if all data we skip are already copied
    for (int i=bg._results.CheckPointer(CanDestroyResults); i<bg._results.GetWriteCursor(); i++)
    {
      const RenderCBToPlay &cb = bg._results.Check(i);
      DoAssert(!cb._copied || cb._noDestroy); // basic check: _copied may be used only on _noDestroy
      DoAssert(cb._copied || !cb._noDestroy); // stricter check: each _noDestroy should have _copied now
    }
    #endif
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

bool EngineDD9::CopyCBPredicatedScope(PredicatedCBScope &scope, PredicationMode pred)
{
  LOG_MEM("CopyCBPredicatedScope %d..%d (%d)",scope.beg,scope.end,IsAbleToDrawCheckOnly());
  if (!UsePredication()) return false;
  Assert(scope.beg>=0); // the scope may be empty, but it needs to be recorded
  // if the scope is empty, we do not have to do anything, but we return true to avoid overhead of the default solution
  if (scope.end<=scope.beg)
  {
    scope.finalCopy = !_bgRecPredication; // see MarkNoDestroy below
    return true;
  }
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
    #if PIX_NAMED_CB_SCOPES
    src._copied = true;
    #endif
    // the copied data need to be destroyed unless we are still recording predication
    res.MarkNoDestroy(_bgRecPredication);
    // invalidate the request for memory clearing - safe, processed by main (this) thread
    src._memToClear = 0;
    #if PIX_NAMED_CB_SCOPES
    res._copied = false;
    #endif
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

  #if PIX_NAMED_CB_SCOPES
  for (int i=_bg._results.CheckPointer(CanDestroyResults); i<scope.end; i++)
  {
    const RenderCBToPlay &cb = _bg._results.Check(i);
    DoAssert(!cb._copied || cb._noDestroy); // basic check: _copied may be used only on _noDestroy
    DoAssert(cb._copied || !cb._noDestroy); // stricter check: each _noDestroy should have _copied now
  }
  #endif
  _bg._results.AdvanceTo(CanDestroyResults,scope.end);
  
  // the source data no longer needed - can be released now if desired
  _bg._results.AdvanceTo(CanDestroyResults,scope.end);
  #if _DEBUG
    if (Glob.config._renderThread==Config::ThreadingEmulated)
    {
      // wait until we destroy what we can
      for(;;)
      {
        int toDestroy = _bg._results.HowMuchBehind(DestroyResults,ProcessResults);
        int canDestroy = _bg._results.HowMuchBehind(DestroyResults,CanDestroyResults);
        if (canDestroy<=0 || toDestroy<=0) break;
        _bg.ProcessBackground();
      }
    }
  #endif
  
  // move the source scope to the copied version - needed in case more copies will be done
  scope.beg = copyBeg;
  scope.end = copyEnd;
  LOG_MEM("CopyCBPredicatedScope copied to %d..%d",scope.beg,scope.end);
  scope.finalCopy = !_bgRecPredication; // see MarkNoDestroy above
  Assert(CheckThreadMemory(_bg));
  
  // once we have copied the data, we do not know the exact device state
  SetHWToUndefinedState(-1);

  return true;
}

bool EngineDD9::UsePredication() const
{
  return Glob.config._renderThread!=Config::ThreadingNone && Glob.config._useCB && Glob.config._predication;
}

bool EngineDD9::StartCBPredicatedScope(PredicatedCBScope &scope, PredicationMode pred)
{
  LOG_MEM("StartCBPredicatedScope (%d)",IsAbleToDrawCheckOnly());
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
    Verify( CopyCBPredicatedScope(scope,pred) );
    _bgRecPredication = false;
    return false;
  }
  
  scope.beg = _bg._results.GetWriteCursor();
  scope.end = 0;
  scope.finalCopy = false;

  LOG_MEM("StartCBPredicatedScope %d (%d)",scope.beg,_bgRecPredication);
  SetHWToUndefinedState(-1); /// WIP: 3DUI
  return true;
}
void EngineDD9::EndCBPredicatedScope(PredicatedCBScope &scope)
{
  if (!UsePredication()) return;
  if (_backgroundD3D!=RecNone)
  {
    // if there are already any background tasks waiting, flush them to make sure they are predicated
    SubmitBackground(_bg,true,true,CleanUpNone,"endCB"); // cleanup already recorded

    Assert(CheckThreadMemory(_bg));
  }
  scope.end = _bg._results.GetWriteCursor();
  LOG_MEM("EndCBPredicatedScope %d..%d",scope.beg,scope.end);
  Assert(scope.end>=scope.beg);
  _bgRecPredication = false;

  Assert(_backgroundD3D!=RecHighLevel);
  if (_backgroundD3D!=RecNone)
  {
    Assert(CheckThreadMemory(_bg));
  }
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

void EngineDD9::StartCBScope(int numThreads, int nResults)
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
    PROFILE_SCOPE_DETAIL_EX(hwIni,*)
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




void EngineDD9::EndCBScope()
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
      PROFILE_SCOPE_DETAIL_EX(hwCln,*)
      SetHWToUndefinedState(-1);
    }
  }

  Assert(_backgroundD3D!=RecNone || _cbState[0]._rec.GetDataSize()==0);
  
  _cbCount = -1;
  
  bool forceCleanUp = false;
  { // now replay all CBs
    
    PROFILE_SCOPE_EX(drwCB,*);
    
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

bool EngineDD9::IsInCBScope() const
{
  // we do not care when immediate CB is being recorded, that does not cause any MT safety issues to calls like Create
  // as API is serializing them via a critical section
  return _cbCount>0;
}


void EngineDD9::SimulateUnsetAll(int cb)
{
  #if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("UnsetAll");
    }
  #endif
  RendStateExt &cbState = CBState(cb);
  
  // Sets the textures for all samplers to NULL.
  for (int i = 0; i < MaxSamplerStages; i++) cbState._lastHandle[i] = NULL;
  //cbState._rec.SetTexture(TEXID_SHADOWMAP,cbState._lastHandle[TEXID_SHADOWMAP]);
  //for (int i = 0; i < TEXID_SHADOWMAP; i++) cbState._lastHandle[i] = NULL;
  
  // Sets the vertex buffers for all streams to NULL.
  cbState._vBufferLast = VertexStaticData9::BufferFuture(VertexStaticData9::BufferFuture::Null);
  cbState._strideLast = 0;
  // Sets the index buffer to NULL.
  cbState._iBufferLast = IndexStaticData9::BufferFuture(IndexStaticData9::BufferFuture::Null);
  // Sets the vertex declaration to NULL.
  cbState._vertexDeclLast.Free();
  
  // Sets the vertex and pixel shaders to NULL.
  for (int p=0; p<NPredicationModes; p++) cbState._vertexShaderLast[p].Free();
  for (int p=0; p<NPredicationModes; p++) cbState._pixelShaderLast[p].Free();
}

/**
@return CB ID (see COMMAND_BUFFERS)
*/
int EngineDD9::StartCBRecording(int resultIndex, int thrId, int debugId)
{
  // thread ID is the same as the result CB ID
  DoAssert(thrId<CBCount());
  int cb = thrId;
  RendStateExt &cbState = CBState(cb);
  if (cb>=0)
  {
    Assert(resultIndex>=0 && resultIndex<_bgResultsInThisScope);
    #if _ENABLE_REPORT
    InterlockedIncrement(&_d3DDevice._cbCount);
    #endif
    Assert(cbState._rec._predicationIndex.Size()==0); // no predication outside of the CB scope?
    cbState._rec.Reset();
    // often tasks are being processed sequentially by the thread (this is the default scheduling strategy)
    // in such case there is no need to reset the state
    if (resultIndex!=cbState._lastResult+1)
    {
      PROFILE_SCOPE_DETAIL_EX(hwPrp,*)
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


void EngineDD9::StopCBRecording(int cb, int resultIndex, const char *name)
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
  InterlockedDecrement(&_d3DDevice._cbCount);
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
    res._copied = false;
  #endif
  #if LOG_RESULTS_ALLOC
    if (size>0) {LOG_MEM("%s (%d): Allocate %d",name,res._debugId,size);}
  #endif
  res._memToClear = 0;
  #if PIX_NAMED_CB_SCOPES
  res._copied = false;
  #endif
  cbState._lastResult = resultIndex;
  cbState._rec.Reset();
}

bool NotInRecording()
{
  return !GEngineDD || !GEngineDD->IsCBRecording();
}

bool CheckValidIntegerConstant(const IntConstant *data, int registerCount)
{
  /* X360_INT_CONSTANT
  X360 SDK: SetVertexShaderConstantI
  Each loop constant i (set by this method) is a vector consisting of four integers:

  The first integer specifies the number of times to execute the loop. The legal range is [0, 255]. 
  The second integer specifies the initial value of the loop counter register (aL). The legal range is [0, 255]. 
  The third integer specifies the loop counter step value. The legal range is [-128, 127].
    In every iteration of the loop, the step value is added to the loop counter register.
  The fourth integer is not used and should be zero.
  */
  for (int i=0; i<registerCount; i++)
  {
    if (data[i][0]<0 || data[i][0]>255) return false;
    if (data[i][1]<0 || data[i][1]>255) return false;
    if (data[i][2]<-128 || data[i][2]>127) return false;
    if (data[i][3]!=0) return false;
  }
  return true;
}

/// Set parameters for post process effect
void EngineDD9::SetRadialBlurrParams(float pwrX, float pwrY, float offsX, float offsY)
{
  if (_pp._radialBlur)
  {
    _pp._radialBlur->SetBlurPower(pwrX, pwrY);
    _pp._radialBlur->SetBlurOffset(offsX, offsY);
  }
}

/// Set parameters for post process effect
void EngineDD9::SetWetDistortionParams(float blur,
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
void EngineDD9::SetSSAOParams(const float *params, int nums)
{
  if (_pp._ssao)
  {
     _pp._ssao->SetParams(params, nums, true);
  }
}

/// Set parameters for post process effect
void EngineDD9::SetFilmGrainParams(const float *params, int nums)
{
  if (_pp._filmGrain)
  {
    _pp._filmGrain->SetParams(params, nums);
  }
}

/// Set parameters for post process effect
void EngineDD9::SetGodRaysParams(const float *params, int nums)
{
	if (_pp._godRays)
	{
		_pp._godRays->SetParams(params, nums, true);
	}
}

/// Set parameters for post process effect
void EngineDD9::SetRainParams(const float *params, int nums)
{
	if (_pp._rain)
	{
		_pp._rain->SetParams(params, nums, true);
	}
}

/// Set parameters for post process effect
void EngineDD9::SetChromaticAberrationParams(float aberationPowerX, float aberationPowerY, bool value)
{
  if (_pp._chromAber)
  {
    _pp._chromAber->SetAberration(aberationPowerX, aberationPowerY);
    _pp._chromAber->SetAspectCorrection(value);
  }
}

void EngineDD9::SetClrInversParams(Color invColor)
{
  if (_pp._clrInvers)
  {
    _pp._clrInvers->SetInversion(invColor);
  }
}

void EngineDD9::SetGodraysKernel(Texture *tex)
{
	if (_pp._godRays)
	{
		_pp._godRays->SetTexture(static_cast<TextureD3D9 *>(tex));
	}
}

void EngineDD9::SetNewRainTexture(Texture *tex)
{
	if (_pp._rain)
	{
		_pp._rain->SetTexture(static_cast<TextureD3D9 *>(tex));
	}
}

float EngineDD9::GetRainRadius() const
{
	return _pp._rain ? _pp._rain->getRadius() : -1.0f;
}

void EngineDD9::SetRainDensity(float density)
{
	if (_pp._rain)
		_pp._rain->SetGlobalPower(density);
}

/// Set parameters for post process effect
void EngineDD9::SetColorCorrectionsParameters(float brightness, float contrast, float offset, Color blendClr,
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

/// Set parameters for post process effect
void EngineDD9::SetDynamicBlurParameters(float bluriness)
{
  if (_pp._dynamicBlur)
  {
    _pp._dynamicBlur->SetBlurrines(bluriness);
  }
}

void EngineDD9::CommitRadBlur(float time)
{
  if (_pp._radialBlur)
  {
    _pp._radialBlur->Commit(time);
  }
}

void EngineDD9::CommitSSAO(float time)
{
 if (_pp._ssao)
   {
   _pp._ssao->Commit(time);
   }
}

void EngineDD9::CommitFilmGrain(float time)
{
  if (_pp._filmGrain)
  {
    _pp._filmGrain->Commit(time);
  }
}

void EngineDD9::CommitGodRays(float time)
{
	if (_pp._godRays)
	{
		_pp._godRays->Commit(time);
	}
}

void EngineDD9::CommitRain(float time)
{
	if (_pp._rain)
	{
		_pp._rain->Commit(time);
	}
}

void EngineDD9::CommitChromAberration(float time)
{
  if (_pp._chromAber)
  {
    _pp._chromAber->Commit(time);
  }
}

void EngineDD9::CommitClrInvers(float time)
{
  if (_pp._clrInvers)
  {
    _pp._clrInvers->Commit(time);
  }
}

void EngineDD9::CommitWetDistortion(float time)
{
  if (_pp._wetDistort)
  {
    _pp._wetDistort->Commit(time);
  }
}

void EngineDD9::CommitColorCorrections(float time)
{
  if (_pp._colors)
  {
    _pp._colors->Commit(time);
  }
}

void EngineDD9::CommitDynamicBlur(float time)
{
  if (_pp._dynamicBlur)
  {
    _pp._dynamicBlur->Commit(time);
  }
}

bool EngineDD9::CommittedRadBlur() const
{
  if (_pp._radialBlur) return _pp._radialBlur->Committed();
  return true;
}

bool EngineDD9::CommittedChromAberration() const
{
  if (_pp._chromAber) return _pp._chromAber->Committed();
  return true;
}

bool EngineDD9::CommittedClrInvers() const
{
  if (_pp._clrInvers) return _pp._clrInvers->Committed();
  return true;
}

bool EngineDD9::CommittedSSAO() const
{
  if (_pp._ssao) return _pp._ssao->Committed();
  return true;
}

bool EngineDD9::CommittedFilmGrain() const
{
  if (_pp._filmGrain) return _pp._filmGrain->Committed();
  return true;
}

bool EngineDD9::CommittedGodRays() const
{
	if (_pp._godRays) return _pp._godRays->Committed();
	return true;
}

bool EngineDD9::CommittedRain() const
{
	if (_pp._rain) return _pp._rain->Committed();
	return true;
}

bool EngineDD9::CommittedWetDistortion() const
{
  if (_pp._wetDistort) return _pp._wetDistort->Committed();
  return true;
}

bool EngineDD9::CommittedColorCorrections() const
{
  if (_pp._colors) return _pp._colors->Committed();
  return true;
}

bool EngineDD9::CommittedDynamicBlur() const
{
  if (_pp._dynamicBlur) return _pp._dynamicBlur->Committed();
  return true;
}

/// reload
void EngineDD9::ReloadGodRays() const
{
	if (_pp._godRays) 
	{
		QOStrStreamDataPrefixedLine psSource(PSFile);
		QOStrStreamDataPrefixedLine vsSource(FPShaders);
		_pp._godRays->Reload(psSource, vsSource);
	}
}

void EngineDD9::ReloadRain() const
{
	if (_pp._rain) 
	{
		QOStrStreamDataPrefixedLine psSource(PSFile);
		QOStrStreamDataPrefixedLine vsSource(FPShaders);
		_pp._rain->Reload(psSource, vsSource);
	}
}

void EngineDD9::ReloadSSAO() const
{
	if (_pp._ssao) 
	{
		QOStrStreamDataPrefixedLine psSource(PSFile);
		QOStrStreamDataPrefixedLine vsSource(FPShaders);
		_pp._ssao->Reload(psSource, vsSource);
	}
}

void EngineDD9::ReloadDOF() const
{
	if (_pp._distantDOF || _pp._DOF) 
	{
		QOStrStreamDataPrefixedLine psSource(PSFile);
		QOStrStreamDataPrefixedLine vsSource(FPShaders);

		if (_pp._distantDOF)
			_pp._distantDOF->Reload(psSource, vsSource);

		if (_pp._DOF)
			_pp._DOF->Reload(psSource, vsSource);
	}
}


void EngineDD9::ReloadEnfShapeCollectionRes() const
{
	if (_pp._enfShapeCollectionRes) 
	{
		QOStrStreamDataPrefixedLine psSource(PSFile);
		QOStrStreamDataPrefixedLine vsSource(FPShaders);
		_pp._enfShapeCollectionRes->Reload(psSource, vsSource);
	}
}


void EngineDD9::ReloadRotBlur() const
{
	if (_pp._rotBlur) 
	{
		QOStrStreamDataPrefixedLine psSource(PSFile);
		QOStrStreamDataPrefixedLine vsSource(FPShaders);
		_pp._rotBlur->Reload(psSource, vsSource);
	}
}


/// Enable/disable radial blur post process effect
void EngineDD9::EnableRadialBlur(bool enable)
{
  if (_pp._radialBlur)
  {
    _pp._radialBlur->Enable(enable);
  }
}

/// Enable/disable radial blur post process effect
void EngineDD9::EnableChromAberration(bool enable)
{
  if (_pp._chromAber)
  {
    _pp._chromAber->Enable(enable);
  }
}

void EngineDD9::EnableClrInvers(bool enable)
{
  if (_pp._clrInvers)
  {
    _pp._clrInvers->Enable(enable);
  }
}

/// Enable/disable SSAO post process effect
void EngineDD9::EnableSSAO(bool enable)
{
	_ssaoEnabled = enable;
  if (_pp._ssao)
  {
     _pp._ssao->Enable(_ssaoEnabled && ((_pp._ssao->GetLevel() & _postFXLevelMask) != 0));
  }
}

bool EngineDD9::IsEnabledSSAO() const
{
  if (_pp._ssao)
  {
     return _pp._ssao->Enabled();
  }
  return false;
}


void EngineDD9::SetSSAOQuality(int quality)
{
  if (_pp._ssao)
  {
    _pp._ssao->Enable(_ssaoEnabled && ((_pp._ssao->GetLevel() & _postFXLevelMask) != 0));
    _pp._ssao->SetQuality(quality);
  }
}


/// Enable/disable film grain post process effect
void EngineDD9::EnableFilmGrain(bool enable)
{
  if (_pp._filmGrain)
  {
    _pp._filmGrain->Enable(enable);
  }
}

/// Enable/disable film grain post process effect
void EngineDD9::EnableGodRays(bool enable)
{
	if (_pp._godRays)
	{
		_pp._godRays->Enable(enable);
	}
}

/// Enable/disable film grain post process effect
void EngineDD9::EnableRain(bool enable)
{
	if (_pp._rain)
	{
		_pp._rain->Enable(enable);
	}
}

/// Enable/disable wet distortion post process effect
void EngineDD9::EnableWetDistortion(bool enable)
{
  if (_pp._wetDistort)
  {
    _pp._wetDistort->Enable(enable);
  }
}

/// Enable/disable color correction post process effect
void EngineDD9::EnableColorCorrections(bool enable)
{
  if (_pp._colors)
  {
    _pp._colors->Enable(enable);
  }
}

/// Enable/disable dynamic blur post process effect
void EngineDD9::EnableDynamicBlur(bool enable)
{
  if (_pp._dynamicBlur)
  {
    _pp._dynamicBlur->Enable(enable);
  }
}

void EngineDD9::ResetGameState()
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

	if (_pp._godRays)
	{
		_pp._godRays->SetDefaultValues();
	}

	if (_pp._rain)
	{
		_pp._rain->SetDefaultValues();
	}

  if (_pp._clrInvers)
  {
    _pp._clrInvers->Enable(false);
    _pp._clrInvers->SetDefaultValues();
  }
 
  if (_pp._glow)
    _pp._glow->SetDefaultToneMapping();

  if (_spp._hit)
    _spp._hit->DestroyHndls();
  if (_spp._movement)
    _spp._movement->DestroyHndls();
  if (_spp._tired)
    _spp._tired->DestroyHndls();

  DestroyPPHndlFunc destFunc;
  ForEachPostEffectHndl(destFunc);
       
  _pp._effects.Clear();
  _pp._effectsThis.Clear();

  if (_spp._hit) _spp._hit->Init();
  if (_spp._movement) _spp._movement->Init();
  if (_spp._tired) _spp._tired->Init();
}

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
    EngineDD9::PostProcess *tmpPP = static_cast<EngineDD9::PostProcess*> (_pp.GetRef());
    if (tmpPP) CHECK(ar.Serialize("ppEffect", *tmpPP, 0));
  }
  // load && post process not created yet
  if (ar.IsLoading() && _pp.IsNull())
  {
    Ref<EngineDD9::PostProcess> tmpPP;

    switch(_type)
    {
   case PEFilmGrain:
     tmpPP = new EngineDD9::PPFilmGrain;
     break;

	 case PEGodRays:
		 tmpPP = new EngineDD9::PPGodRays;
		 break;

	 case PERain:
		 tmpPP = new EngineDD9::PPRain;
		 break;

   case PEChromAberration:
        tmpPP = new EngineDD9::PPChromAber;
      break;

    case PEColorCorrections:
        tmpPP = new EngineDD9::PPColors;
      break;

    case PERadialBlur:
        tmpPP = new EngineDD9::PPRadialBlur;
      break;

    case PEDynamicBlur:
        tmpPP = new EngineDD9::PPDynamicBlur;
      break;

    case PEWetDistortion:
        tmpPP = new EngineDD9::PPWetDistort;
      break;

    case PEColorInv:
      tmpPP = new EngineDD9::PPColorsInversion;
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


//-----------------------------------------------------------------------------
void EngineDD9::SetPixelShaderConstantHeatHaze(int cb, Texture *texture)
{
	TextureD3D9* textureD9 = static_cast<TextureD3D9*>(texture);

	SetTexture( cb, 0, textureD9->GetHandle()); 
	SetFiltering(cb, 0, TFTrilinear, 0);

	D3DSetSamplerState(cb, 0, D3DSAMP_SRGBTEXTURE, 0);
	D3DSetSamplerState(cb, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP );
	D3DSetSamplerState(cb, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP );

	//background, in the best case should be captured right before this material selection
	SetTexture( cb, 1, _renderTargetD ); 
	SetFiltering(cb, 1, TFLinear, 0);

	D3DSetSamplerState(cb, 1, D3DSAMP_SRGBTEXTURE, false);
	D3DSetSamplerState(cb, 1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
	D3DSetSamplerState(cb, 1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );

	//depth texture 
	IDirect3DTexture9 * depthTex = GetDepthAsTexture();
	Assert(depthTex);

	SetTexture(cb, 2, depthTex);
	SetFiltering(cb, 2, TFLinear, 0);

	D3DSetSamplerState(cb, 2, D3DSAMP_SRGBTEXTURE, FALSE );
	D3DSetSamplerState(cb, 2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
	D3DSetSamplerState(cb, 2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );

	D3DXVECTOR4 pars [2];

	static float speed = 0.2f;
	float time = Glob.time.toFloat()*speed;

	static float smoothDepth = 0.25;
	pars[0] = D3DXVECTOR4 (time, Glob.config.GetMaxLinearDepth()/smoothDepth, 0, 0);

	SetPixelShaderConstantF(cb, PSC_HeatHazePars, reinterpret_cast<const float*>(&pars), 1);
}


//-----------------------------------------------------------------------------
static inline void HeatHazeQuad(HeatHazeVertex* ENF_RESTRICT v, const HeatHazeRenderInstance& inst, float invW, float invH)
{
	float vx = inst.ProjPos.X();
	float vy = inst.ProjPos.Y();
	float z  = inst.ProjPos.Z();
	float lZ = inst.LinearZ;
	float p  = inst.Power;

	float xBeg =  (vx - inst.SizeX)*invW - 1.0f;
	float xEnd =  (vx + inst.SizeX)*invW - 1.0f;
	float yBeg = -(vy - inst.SizeY)*invH + 1.0f;
	float yEnd = -(vy + inst.SizeY)*invH + 1.0f;

	v[0].pos[0] = xBeg;	v[0].pos[1] = yBeg;	v[0].pos[2] = z;	v[0].pos[3] = lZ;	v[0].t0[0] = 0.0f;	v[0].t0[1] = 0.0f;	 v[0].t0[2] = p;
	v[1].pos[0] = xEnd;	v[1].pos[1] = yBeg;	v[1].pos[2] = z;	v[1].pos[3] = lZ;	v[1].t0[0] = 1.0f;	v[1].t0[1] = 0.0f;	 v[1].t0[2] = p;
	v[2].pos[0] = xEnd;	v[2].pos[1] = yEnd;	v[2].pos[2] = z;	v[2].pos[3] = lZ;	v[2].t0[0] = 1.0f;	v[2].t0[1] = 1.0f;	 v[2].t0[2] = p;
	v[3].pos[0]	=	xBeg;	v[3].pos[1]	=	yEnd;	v[3].pos[2]	=	z;	v[3].pos[3] = lZ;	v[3].t0[0] = 0.0f;	v[3].t0[1] = 1.0f;	 v[3].t0[2] = p;
	v[4].pos[0] = xBeg;	v[4].pos[1] = yBeg;	v[4].pos[2] = z;	v[4].pos[3] = lZ;	v[4].t0[0] = 0.0f;  v[4].t0[1] = 0.0f;	 v[4].t0[2] = p;
	v[5].pos[0] = xEnd;	v[5].pos[1] = yEnd;	v[5].pos[2] = z;	v[5].pos[3] = lZ;	v[5].t0[0] = 1.0f;	v[5].t0[1] = 1.0f;	 v[5].t0[2] = p;
}


//-----------------------------------------------------------------------------
void EngineDD9::DrawLightHeatHaze(HeatHazeRenderArray& instances, Texture *texture)
{
	int vertices = instances.GetCardinality()*6;

	//ScopeLockSection lock(_renderVBCritSection);
	if (!_heatHazeVB.IsValid())
	{
		RptF("DrawLightHeatHaze - buffers invalid");
		return;
	}

	Ref<UserVertexBuffer> buffer = _heatHazeVB.GetVertexBuffer(vertices);
	if (!buffer || !buffer->GetBuffer())
		return; 

	// fill buffer
	int offset = 0;
	{
		VBLockScope lock(*buffer, vertices, offset);
		if (!lock.GetData())
			return;
	
		float invW = 2.0f/_w;
		float invH = 2.0f/_h;
		for (int i = 0; i < instances.GetCardinality(); i++)
			HeatHazeQuad(static_cast<HeatHazeVertex*>(lock.GetData()) + 6*i, instances[i], invW, invH);
	}

	const int cb = -1;

	//states
	SwitchTL(cb, EngineDD9::TLEnabled);
	SetBias(cb, 0);

	SetPixelShaderConstantHeatHaze(cb, texture);

	//render states
	D3DSetRenderState(cb, D3DRS_ZENABLE, TRUE);
	D3DSetRenderState(cb, D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	D3DSetRenderState(cb, D3DRS_ZWRITEENABLE, FALSE);
	D3DSetRenderState(cb, D3DRS_DEPTHBIAS, 0);
	float zero = 0.0f;
	DWORD dwZero = *(const DWORD*)&zero;
	D3DSetRenderState(cb, D3DRS_SLOPESCALEDEPTHBIAS, dwZero);

	D3DSetRenderState(cb, D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
	D3DSetRenderState(cb, D3DRS_ALPHABLENDENABLE, FALSE);
	D3DSetRenderState(cb, D3DRS_ALPHATESTENABLE, FALSE);
	D3DSetRenderState(cb, D3DRS_CULLMODE, D3DCULL_NONE);
	D3DSetRenderState(cb, D3DRS_FILLMODE, D3DFILL_SOLID);

	if (_useAToC)
		D3DEnableAlphaToCoverage(cb, false);	

	SetVertexShader(cb, _vsHeatHaze);
	SetPixelShader(cb, _pixelShader[PSNonTLHeatHaze][PSTBasic]);
	SetVertexDeclaration(cb, _vdHeatHaze);

	SetStreamSource(cb, VertexStaticData9::BufferFuture(buffer->GetBuffer()), sizeof(HeatHazeVertex), buffer->GetMaxCount()); 
	DrawPrimitive(cb, D3DPT_TRIANGLELIST, offset, vertices/3);
}


#if _ARMA3_SIMUL_WEATHER

//-----------------------------------------------------------------------------
void EngineDD9::InitSimulWeather()
{
  if (GLandscape && GLandscape->GetSimulWeather())
    GLandscape->GetSimulWeather()->InitResources();
}

//-----------------------------------------------------------------------------
void EngineDD9::DeInitSimulWeather()
{
  if (GLandscape && GLandscape->GetSimulWeather())
    GLandscape->GetSimulWeather()->DeInitResources();
}

//-----------------------------------------------------------------------------
SimulWeatherBase* EngineDD9::CreateSimulWeather()
{
  return new SimulWeatherDX9(this);
}

#endif

LSError EngineDD9::SerializeGameState(ParamArchive &ar)
{
  if (_pp._filmGrain)   { CHECK(ar.SerializeDef("filmGrain",*_pp._filmGrain,0)); }
	if (_pp._godRays)		  { CHECK(ar.SerializeDef("godRays",*_pp._godRays,0)); }
	if (_pp._rain)				{ CHECK(ar.SerializeDef("rain",*_pp._rain,0)); }
  if (_pp._radialBlur)  { CHECK(ar.SerializeDef("radialBlur",*_pp._radialBlur,0)); }
  if (_pp._chromAber)   { CHECK(ar.SerializeDef("chromAber",*_pp._chromAber, 0)); }
  if (_pp._wetDistort)  { CHECK(ar.SerializeDef("wetDistort",*_pp._wetDistort, 0)); }
  if (_pp._colors)      { CHECK(ar.SerializeDef("colors",*_pp._colors, 0)); }
  if (_pp._dynamicBlur) { CHECK(ar.SerializeDef("dynBlur",*_pp._dynamicBlur, 0)); }
  if (_pp._clrInvers)   { CHECK(ar.SerializeDef("clrInv", *_pp._clrInvers, 0)); }

  if (_spp._hit) { CHECK(ar.Serialize("hitSpp", *_spp._hit, 0)); }
  if (_spp._movement) { CHECK(ar.Serialize("movementSpp", *_spp._movement, 0)); }
  if (_spp._tired) { CHECK(ar.Serialize("tirredSpp", *_spp._tired, 0)); }

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

HRESULT EngineDD9::CheckDeviceFormat(DWORD usage, D3DRESOURCETYPE type, D3DFORMAT format)
{
  DoAssert(_direct3D);
  if (!_direct3D)
    return S_FALSE;

  DoAssert(!_d3DDevice.IsNull());
  if (_d3DDevice.IsNull())
    return S_FALSE;

  DoAssert(_backBuffer.NotNull());
  if (_backBuffer.IsNull())
    return S_FALSE;

  // Get current adapter 
  UINT adapter = 0;
  {
    D3DDEVICE_CREATION_PARAMETERS cpars;
    _d3DDevice->GetCreationParameters(&cpars);
    adapter = cpars.AdapterOrdinal;
  }

  // Get back buffer format
  D3DSURFACE_DESC bdesc;
  HRESULT err=_backBuffer->GetDesc(&bdesc);
  if (FAILED(err)) 
  {
    DDError9("GetSurfaceDesc failed.",err);
    return err;
  }
  D3DFORMAT bbFormat = bdesc.Format;

  return _direct3D->CheckDeviceFormat(adapter, _devType, bbFormat, usage, type, format);
}

#ifdef TRAP_FOR_0x8000
void EngineDD9::TrapFor0x8000(const char *str)
{
  if (CheckMainThread())
    if (_textBank) _textBank->TrapFor0x8000(str);
}

#endif

#endif

#if !_DISABLE_GUI && !defined _SERVER
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
void ComputeCalmWaterPars( EngineDD9 *engine, SCalmWaterIn *cwmp, ColorVal matAmbient, ColorVal matDiffuse,
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
/*
////////////////////////////////////////////////////////////////////////////////////////////////////
// SEA WATER STUFF
////////////////////////////////////////////////////////////////////////////////////////////////////

// sea water VS params structure instance
static SSeaWaterParamsVS swvsp;
// sea water PS params structure instance
static SSeaWaterParamsPS swpsp;

static SSeaWaterIn swip;

// reference to input params structure
SSeaWaterIn& GetSeaWaterInPars()
{
  return swip;
}

// reference to const reg param array for vertex shader
SSeaWaterParamsVS& GetSeaWaterParsVSRef()
{
  return swvsp;
}

// float pointer to const reg param array for vertex shader
const float* GetSeaWaterParsVS()
{
  return (const float*)&swvsp;
}

// reference to const reg param array for pixel shader
SSeaWaterParamsPS& GetSeaWaterParsPS1Ref()
{
  return swpsp;
}

// float pointer to const reg param array for pixel shader
const float* GetSeaWaterParsPS1()
{
  return (const float*)&swpsp;
}

// computes params for calm water shader
void ComputeSeaWaterPars( EngineDD9 *engine, SSeaWaterIn &swip, ColorVal matAmbient, ColorVal matDiffuse,
                          ColorVal matForcedDiffuse, ColorVal matEmmisive, ColorVal matSpecular, const float matPower )
{
  // from material: cbState._matAmbient, cbState._matDiffuse,
  // cbState._matForcedDiffuse, cbState._matEmmisive, cbState._matSpecular, 
  //const float kc = 2.0f; // mul param for material colors (they are halved)
  // weather condition <0;1>
  //float weather = cwp.inPars.weather[3] * 2.87f - 0.25f;
  //float weather = 1; //0.3; // 30%
  //float weather = cwp.inPars.weather[3];

  float t0 = Glob.time.toFloat(); // in seconds
  float waveH0 = swip.waveHeight;
  //t0 = 0;
  SSeaWaterParamsVS &vsp = GetSeaWaterParsVSRef();
  SSeaWaterParamsPS &psp = GetSeaWaterParsPS1Ref();

  /*Camera *camera = GScene->GetCamera();
  Vector3 cameraPosition = camera->Position();
  Vector3 cameraObjectPosition = TransformPositionToObjectSpace( cb, cameraPosition );
  D3DXVECTOR4 cpos(cameraObjectPosition.X(), cameraObjectPosition.Y(), cameraObjectPosition.Z(), 1.0f);
  SetVertexShaderConstantF( cb, VSC_CameraPosition, (float*)&cpos, 1 );
  vsp.cameraPosition[0] = cameraPosition.X();
  vsp.cameraPosition[1] = cameraPosition.Z();*
  //SSeaWaterParamsVS &swvsp = GetSeaWaterParsVSRef();

  // [0]
  vsp.cameraPosition[0] = swip.cameraPosition[0];
  vsp.cameraPosition[1] = swip.cameraPosition[1];
  vsp.cameraPosition[2] = swip.cameraPosition[2];
  vsp.wrapScale0 = 1.0f / 40;
  
  const float deg2rad = 3.1415926f / 180.0f;
  const float kws = 1.0f / 0.385f; // wave size const  (reciprocal of max amp of used function)
  const float kder = kws * 2.0f; //3.1415926f * 2.0f;
  //float rotAngleA = deg2rad * 120.0f;
  //float rotAngleB = deg2rad * 150.0f;
  float rotAngleA = deg2rad * 80.0f; // 110 // lesser angle need to be from A to B
  float rotAngleB = deg2rad * 130.0f; // 150
  //rotAngleA = deg2rad * 70.0f; // 110 // lesser angle need to be from A to B
  //rotAngleB = deg2rad * 120.0f; // 150
  float waveHeightA = waveH0 * 1.4; // 0.7
  float waveHeightB = waveH0 * 1.0; // 0.5
  float waveLenghtA = 49; //49; // m
  float waveLenghtB = 63; //63;
  float waveSpeedA = 2.7; // m / s
  float waveSpeedB = 2.1;
  float normWaveLenghtAx = 145;
  float normWaveLenghtAy = 95;
  float normWaveSpeedA = waveSpeedA * 0.6;
  float normWaveLenghtBx = 171;
  float normWaveLenghtBy = 99;
  float normWaveSpeedB = waveSpeedB * 0.6;
  //
  float sinA = sinf( rotAngleA );
  float cosA = cosf( rotAngleA );
  float sinB = sinf( rotAngleB );
  float cosB = cosf( rotAngleB );
  
  // [1]
  vsp.wrapRotA[0] = sinA;
  vsp.wrapRotA[1] = cosA;
  vsp.wrapRotB[0] = sinB;
  vsp.wrapRotB[1] = cosB;
  // [2]
  vsp.waveScaleA = 1.0f / waveLenghtA;
  vsp.waveScaleB = 1.0f / waveLenghtB;
  vsp.waveShiftA = -waveSpeedA / waveLenghtA * t0;
  vsp.waveShiftB = -waveSpeedB / waveLenghtB * t0;
  // [3]
  vsp.waveHeightA = waveHeightA * kws;
  vsp.waveHeightB = waveHeightB * kws;
  vsp.derivCoefA = kder * waveHeightA / waveLenghtA;
  vsp.derivCoefB = kder * waveHeightB / waveLenghtB;
  //vsp.derivCoefB = waveLenghtB / vsp.waveHeightB * pi2r;

  // [4]
  vsp.wrapA[0] = 1.0f / normWaveLenghtAx; // 1 / length of texture edge (m)
  vsp.wrapA[1] = 1.0f / normWaveLenghtAy;
  vsp.wrapA[2] = -normWaveSpeedA / normWaveLenghtAx * t0 * cosA;
  vsp.wrapA[3] = -normWaveSpeedA / normWaveLenghtAy * t0 * sinA;
  // [5]
  vsp.wrapB[0] = 1.0f / normWaveLenghtBx;
  vsp.wrapB[1] = 1.0f / normWaveLenghtBy;
  vsp.wrapB[2] = -normWaveSpeedB / normWaveLenghtBx * t0 * cosB;
  vsp.wrapB[3] = -normWaveSpeedB / normWaveLenghtBy * t0 * sinB;

  // [6] + [7]
   // lengths for waves C, D, E, F
  float waveLenghtCDEF[4];
  float waveSpeedCDEF[4];
  waveLenghtCDEF[0] = 223; //253;
  waveLenghtCDEF[1] = 159; //187;
  waveLenghtCDEF[2] = 241; //271;
  waveLenghtCDEF[3] = 167; //161;
  waveSpeedCDEF[0] = -1.62 * waveSpeedA;
  waveSpeedCDEF[1] = 2.31 * waveSpeedA;
  waveSpeedCDEF[2] = -1.71 * waveSpeedB;
  waveSpeedCDEF[3] = 2.11 * waveSpeedB;
  for( int i = 0; i < 4; i++ )
  {
    vsp.waveScaleCDEF[i] = 1.0f / waveLenghtCDEF[i];
    vsp.waveShiftCDEF[i] = -waveSpeedCDEF[i] * vsp.waveScaleCDEF[i] * t0;
  }

  // [8] // amplitudes for waves C, D, E, F
  vsp.waveAmpsCDEF[0] = 0.41;
  vsp.waveAmpsCDEF[1] = 0.31; //0.50 - vsp.waveAmpsCDEF[0];
  vsp.waveAmpsCDEF[2] = 0.37;
  vsp.waveAmpsCDEF[3] = 0.29; //0.50 - vsp.waveAmpsCDEF[2];
  // [9]
  vsp.waveComplementCD = 1.0f - ( vsp.waveAmpsCDEF[0] + vsp.waveAmpsCDEF[1] ); // wave C, D amplitudes complement
  vsp.waveComplementEF = 1.0f - ( vsp.waveAmpsCDEF[2] + vsp.waveAmpsCDEF[3] ); // wave E, F amplitudes complement
  //
  vsp.waveAmpsCDEF[0] *= kws;
  vsp.waveAmpsCDEF[1] *= kws;
  vsp.waveAmpsCDEF[2] *= kws;
  vsp.waveAmpsCDEF[3] *= kws;

// PS
  // [0]
  psp.invRtSizes[0] = 1.0f / (float)engine->Width();
  psp.invRtSizes[1] = 1.0f / (float)engine->Height();
  
  // NORMAL MAP BLENDING COEFS A, B
  // coefs variates in interval < 0; sinBlendAmp >
  // input params:
  //float sinBlendAmpA = 0.67f * weather;// + 0.01f; //0.2f;
  //float sinBlendAmpB = 0.47f * weather;// + 0.02f; //0.4f;
  float sinBlendAmpA = 4.2f * waveH0;// + 0.01f; //0.2f;
  float sinBlendAmpB = 3.0f * waveH0;// + 0.02f; //0.4f;
  float sinBlendSpeedA = 2.5f;
  float sinBlendSpeedB = 2.1f;
  //
  float tsbA = t0 * sinBlendSpeedA;
  float tsbB = t0 * sinBlendSpeedB;
  float sbkA = sinBlendAmpA * 0.5f;
  float sbkB = sinBlendAmpB * 0.5f;
  // [1]
  psp.blendAnimA[0] = sinf( tsbA + PI * 0.0f ) * sbkA + sbkA;
  psp.blendAnimA[1] = sinf( tsbA + PI * 0.5f ) * sbkA + sbkA;
  psp.blendAnimA[2] = sinf( tsbA + PI * 1.0f ) * sbkA + sbkA;
  psp.blendAnimA[3] = sinf( tsbA + PI * 1.5f ) * sbkA + sbkA;
  // [2]
  psp.blendAnimB[0] = sinf( tsbB + PI * 1.0f ) * sbkB + sbkB;
  psp.blendAnimB[1] = sinf( tsbB + PI * 0.0f ) * sbkB + sbkB;
  psp.blendAnimB[2] = sinf( tsbB + PI * 1.5f ) * sbkB + sbkB;
  psp.blendAnimB[3] = sinf( tsbB + PI * 0.5f ) * sbkB + sbkB;
  // [3]
  psp.blendAmpKoefs[0] = -sinBlendAmpA;
  psp.blendAmpKoefs[1] = -sinBlendAmpB;
  //psp.blendAmpKoefs[2]
  psp.blendAmpKoefs[3] = -( sinBlendAmpA + sinBlendAmpB ); // final blend offset

  // [4] - same as vsp [3]
  psp.waveHeightA = vsp.waveHeightA;
  psp.waveHeightB = vsp.waveHeightB;
  psp.derivCoefA = vsp.derivCoefA;
  psp.derivCoefB = vsp.derivCoefB;

  // [5]
  psp.mainLightDirO[0] = swip.mainLightDirO[0];
  psp.mainLightDirO[1] = swip.mainLightDirO[1];
  psp.mainLightDirO[2] = swip.mainLightDirO[2];
  psp.mainLightSpecPow = 200;

  // [6]
  psp.maxWaveHeight = waveHeightA + waveHeightB;
  //psp.maxWaveHeightPlus = psp.maxWaveHeight * 

  // [7]
  psp.fogPars[0] = 0.03f;
  psp.fogPars[1] = 0.90f;

  // [9]
  const float lightC1 = 0.08;
  const float lightC2 = 0.08;
  psp.lightSum[0] = swip.skyLightColor[0] * lightC1 + swip.mainLightColor[0] * lightC2;
  psp.lightSum[1] = swip.skyLightColor[1] * lightC1 + swip.mainLightColor[1] * lightC2;
  psp.lightSum[2] = swip.skyLightColor[2] * lightC1 + swip.mainLightColor[2] * lightC2;
  
    
}
*/
#endif

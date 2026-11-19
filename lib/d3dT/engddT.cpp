#include "../wpch.hpp"

#if !_DISABLE_GUI && !defined _RESISTANCE_SERVER && _ENABLE_DX10
#include "engddT.hpp"
#include "postProcessT.hpp"

#include <El/Statistics/statistics.hpp>
#include <El/common/perfLog.hpp>
#include <El/common/randomGen.hpp>
#include <El/QStream/qbstream.hpp>

#include "txtD3DT.hpp"
#include "../Shape/poly.hpp"
#include "../tlVertex.hpp"
#include "../txtPreload.hpp"
#include "../Shape/shape.hpp"
#include "../Shape/material.hpp"
#include "../clip2D.hpp"
#include "../global.hpp"
#include "../keyInput.hpp"
#include "../dikCodes.h"

void ReportMemoryStatus(int level);


#if _ENABLE_COMPILED_SHADER_CACHE
#include "ShaderCompileCacheT.h"
#endif

//! Folder that contains 
static const char HLSLCacheDir[] = "bin\\";
/// include path
static const char HLSLDir[] = "W:\\c\\Poseidon\\lib\\d3dT\\ShaderSources\\";
/// main vertex shader fragment pool
static const char FPShaders[] = "W:\\c\\Poseidon\\lib\\d3dT\\ShaderSources\\FPShaders.hlsl";

/// pixel shader source file
static const char PSFile[] = "W:\\c\\Poseidon\\lib\\d3dT\\ShaderSources\\PS.hlsl";

//!{ Vertex shader source files
static const char VSHeader[]      = "W:\\c\\Poseidon\\lib\\d3dT\\ShaderSources\\VS.h";
static const char VSFile[]        = "W:\\c\\Poseidon\\lib\\d3dT\\ShaderSources\\VS.hlsl";
static const char VSTerrainFile[] = "W:\\c\\Poseidon\\lib\\d3dT\\ShaderSources\\VSTerrain.hlsl";
static const char VSSpriteFile[] = "W:\\c\\Poseidon\\lib\\d3dT\\ShaderSources\\VSSprite.hlsl";
static const char VSWaterFile[] = "W:\\c\\Poseidon\\lib\\d3dT\\ShaderSources\\VSWater.hlsl";
static const char VSShoreFile[] = "W:\\c\\Poseidon\\lib\\d3dT\\ShaderSources\\VSShore.hlsl";
static const char VSTerrainGrassFile[] = "W:\\c\\Poseidon\\lib\\d3dT\\ShaderSources\\VSTerrainGrass.hlsl";
static const char VSShadowVolumeFile[] = "W:\\c\\Poseidon\\lib\\d3dT\\ShaderSources\\VSShadowVolume.hlsl";
//!}

/// informative - dependency information used for the cache
static const char ShaderInclude[] = "W:\\c\\Poseidon\\lib\\d3dT\\ShaderSources\\psvs.h";

//! File common for both the shaders and program
static const char HLSLPRGCommon[] = "W:\\c\\Poseidon\\lib\\d3dT\\ShaderSources\\common.h";

//! List of shader sources
static const char *Sources[] = {FPShaders, PSFile, VSHeader, VSFile, VSTerrainFile, VSSpriteFile, VSWaterFile, VSShoreFile, VSTerrainGrassFile, VSShadowVolumeFile, ShaderInclude, HLSLPRGCommon};


//! Shader file names
static const char SBTFileName[][64] =
{
  "BICompiledShadersT_Default",
  "BICompiledShadersT_nVidia",
  "BICompiledShadersT_Default",
  "BICompiledShadersT_Default",
  "BICompiledShadersT_Default",
};
namespace Unique1
{
  COMPILETIME_COMPARE(lenof(SBTFileName), SBTCount)
}


#include <winuser.h>
#if _DEBUG
//   #define NoSysLockFlag D3DLOCK_NOSYSLOCK
#else
//   // No sys lock is usually slower
//   #define NoSysLockFlag 0
#endif
#define NoClipUsageFlag D3DUSAGE_DONOTCLIP
// #define DiscardLockFlag D3DLOCK_DISCARD

#if _DEBUG
  #define WRITEONLYVB D3DUSAGE_WRITEONLY
  #define WRITEONLYIB D3DUSAGE_WRITEONLY
#else
  #define WRITEONLYVB D3DUSAGE_WRITEONLY
  #define WRITEONLYIB D3DUSAGE_WRITEONLY
#endif

static inline int FAST_D3DRGBA_NORMAL_SAT(float r, float g, float b, float a)
{
  int ia = toLargeInt(a * 255);
  int ir = toLargeInt(r * 255);
  int ig = toLargeInt(g * 255);
  int ib = toLargeInt(b * 255);
  saturateMin(ia,255);
  saturateMin(ir,255);
  saturateMin(ig,255);
  saturateMin(ib,255);
  return (ia<<24)|(ib<<16)|(ig<<8)|ir;
}

#define LOG_LOCKS 0

//const int MaxShapeVertices = 32*1024;
const int MaxShapeVertices = 4*1024;
const int MaxStaticVertexBufferSizeB = 0;
//const int MaxStaticVertexBufferSizeB = 4*1024-64;
//const int MaxStaticVertexBufferSizeB = 16*1024-64;

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

#if _ENABLE_CHEATS

#include <El/Evaluator/express.hpp>
#include <El/Modules/modules.hpp>

#define ENG_DIAG_ENABLE(type,prefix,XX) \
  XX(type, prefix, NoFilterVS) \
  XX(type, prefix, NoSetupPS) \


#ifndef DECL_ENUM_ENG_DIAG_ENABLE
#define DECL_ENUM_ENG_DIAG_ENABLE
  DECL_ENUM(EngDiagEnable)
#endif
DECLARE_DEFINE_ENUM_STATIC(EngDiagEnable,EDE,ENG_DIAG_ENABLE)

static int EngDiagMode;
static int LimitVSCCount=-1;
int LimitTriCountT=-1;
int LimitDIPCountT=-1;
static int LimitSecCount=-1;
static bool ForceStaticVB=false;

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
  LimitTriCountT = toInt(limit);
  return NOTHING;
}

/// limit DIP in the scene
static GameValue SetEngLimitDIP(const GameState *state, GameValuePar oper1)
{
  float limit = oper1;
  LimitDIPCountT = toInt(limit);
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

static const GameNular DDTNular[]=
{
  GameNular(GameNothing,"diag_engStressTexClear",ClearTexStress),
  GameNular(GameNothing,"diag_engFlushTex",FlushTexMem),
  GameNular(GameNothing,"diag_engFlushVB",FlushVBMem),
};

static const GameFunction DDTUnary[]=
{
  GameFunction(GameNothing,"diag_engToggle",SetEngDiagToggle,GameString),
  GameFunction(GameNothing,"diag_engLimitVSC",SetEngLimitVSC,GameScalar),
  GameFunction(GameNothing,"diag_engLimitTri",SetEngLimitTri,GameScalar),
  GameFunction(GameNothing,"diag_engLimitDIP",SetEngLimitDIP,GameScalar),
  GameFunction(GameNothing,"diag_engLimitSec",SetEngLimitSec,GameScalar),
  GameFunction(GameNothing,"diag_engViewport",SetEngViewport,GameScalar),
  GameFunction(GameNothing,"diag_engStressTex",AddTexStress,GameScalar),
  GameFunction(GameNothing,"diag_engStressRT",AddRTStress,GameScalar),
  GameFunction(GameNothing,"diag_engStressTex",AddTexStressAr,GameArray),
  GameFunction(GameNothing,"diag_engStressRT",AddRTStressAr,GameArray),
  GameFunction(GameNothing,"diag_engStressTexMode",TexStressMode,GameScalar),
  GameFunction(GameNothing,"diag_engFlushTexPerf",FlushTexMemPrior,GameScalar),
  GameFunction(GameNothing,"diag_engUseStaticVB",UseStaticVB,GameBool),
  GameFunction(GameNothing,"diag_engReloadVS",ReloadVS,GameString),
};
static const GameOperator DDTBinary[]=
{
  GameOperator(GameNothing,"diag_engEnable",function,SetEngDiagEnable,GameString,GameBool),
};

INIT_MODULE(GameStateDDT, 3)
{
  GGameState.NewOperators(DDTBinary,lenof(DDTBinary));
  GGameState.NewFunctions(DDTUnary,lenof(DDTUnary));
  GGameState.NewNularOps(DDTNular,lenof(DDTNular));
};

#else

  #define CHECK_ENG_DIAG(x) false
  const int LimitVSCCount=-1;
  const int LimitTriCountT=-1;
  const int LimitDIPCountT=-1;
  const int LimitSecCount=-1;
#endif

#if _ENABLE_PERFLOG && _ENABLE_CHEATS
#define DO_TEX_STATS 2
#endif

#if _ENABLE_PERFLOG 
extern bool LogStatesOnce;
static int DPrimIndex;
#endif

#if DO_TEX_STATS
#include "../keyInput.hpp"
#include "../dikCodes.h"
static StatisticsByName TexStats;
static bool EnableTexStats = false;
#endif

extern int D3DAdapter;

EngineDDT *EngineDDT::SetRenderStateTemp::_engine;
EngineDDT *EngineDDT::SetSamplerStateTemp::_engine;
EngineDDT *EngineDDT::PushVertexShaderConstant::_engine;
EngineDDT *EngineDDT::PushPixelShaderConstant::_engine;
EngineDDT *EngineDDT::SetTextureTemp::_engine;

//StatisticsByName vbStats;

struct UVPairCompressed
{
  short u,v;
};

struct VectorCompressed
{
  D3DCOLOR v;
};

//////////////////////////////////////////////////////////////////////////
// Vertex declarations

//!{ Basic

struct SVertexT
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  VectorCompressed s; // Dummy
  VectorCompressed t; // Dummy
};

struct SVertexNormalT
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  UVPairCompressed t1; // Dummy
  VectorCompressed s;
  VectorCompressed t;
};

struct SVertexUV2T
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  UVPairCompressed t1;
  VectorCompressed s; // Dummy
  VectorCompressed t; // Dummy
};

struct SVertexNormalUV2T
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  UVPairCompressed t1;
  VectorCompressed s;
  VectorCompressed t;
};

struct SVertexNormalCustomT
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  VectorCompressed s;
  VectorCompressed t;
  Vector3P custom;
};

struct SVertexShadowVolumeT
{
  Vector3P pos;
  VectorCompressed norm;
};

//!}

//!{ Skinned

struct SVertexSkinnedT
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  UVPairCompressed t1; // Dummy
  VectorCompressed s; // Dummy
  VectorCompressed t; // Dummy
  BYTE weights[4];
  BYTE indices[4];
};

struct SVertexNormalSkinnedT
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  UVPairCompressed t1; // Dummy
  VectorCompressed s;
  VectorCompressed t;
  BYTE weights[4];
  BYTE indices[4];
};

struct SVertexSkinnedUV2T
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  UVPairCompressed t1;
  VectorCompressed s; // Dummy
  VectorCompressed t; // Dummy
  BYTE weights[4];
  BYTE indices[4];
};

struct SVertexNormalSkinnedUV2T
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

struct SVertexShadowVolumeSkinnedT
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

struct SVertexInstancedT
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  UVPairCompressed t1; // Dummy
  VectorCompressed s; // Dummy
  VectorCompressed t; // Dummy
  BYTE index[4]; // Just w is used, the rest is dummy
};

struct SVertexNormalInstancedT
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  UVPairCompressed t1; // Dummy
  VectorCompressed s;
  VectorCompressed t;
  BYTE index[4]; // Just w is used, the rest is dummy
};

struct SVertexInstancedUV2T
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  UVPairCompressed t1;
  VectorCompressed s; // Dummy
  VectorCompressed t; // Dummy
  BYTE index[4]; // Just w is used, the rest is dummy
};

struct SVertexNormalInstancedUV2T
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
  UVPairCompressed t1;
  VectorCompressed s;
  VectorCompressed t;
  BYTE index[4]; // Just w is used, the rest is dummy
};

struct SVertexShadowVolumeInstancedT
{
  Vector3P pos;
  VectorCompressed norm;
  BYTE index[4]; // Just w is used, the rest is dummy
};

struct SVertexSpriteInstancedT
{
  BYTE t0u;
  BYTE t0v;
  BYTE index;
  BYTE dummy;
};

//!}

//////////////////////////////////////////////////////////////////////////


// ------------------------
// Shader constants factory
/*
// The NameArray
#define CONSTANTS_LIST_NAMEARRAY_ITEM(item) #item,
char EngineDDT::_fragmentConstantNames[NEConstantsList][256] =
{
  CONSTANTS_LIST(CONSTANTS_LIST_NAMEARRAY_ITEM)
};
*/

// ------------------------

#include <El/Debugging/debugTrap.hpp>

void EngineDDT::DDError9( const char *text, int err )
{
  LogF("%s:%s",text,DXGetErrorString8(err));
  if (GDebugger.IsDebugger())
  {
    //BREAK();
  }
  int texUsed = _textBank ? _textBank->GetVRamTexAllocation() : 0;
  LogF("Total allocated: VB/IB/Tex: %d/%d/%d",_vBuffersAllocated,_iBuffersAllocated,texUsed);
}



void ConvertMatrixTransposedT(D3DXMATRIX &mat, Matrix4Val src)
{
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
}

static void ConvertMatrixTransposed3(MatrixFloat34 &mat, Matrix4Val src)
{
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
}

static void ConvertMatrixTransposed3Offset(MatrixFloat34 &mat, Matrix4Val src, Vector3Par offset)
{
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
}

void EngineDDT::GetZCoefs(float &zAdd, float &zMult)
{
  int zBias = _bias;
  zMult = 1.0f-zBias*1e-7f;
  zAdd = zBias*-2e-7f;
}

static const float ZBiasEpsilon = 0.1f;

static void ConvertProjectionMatrix(D3DMATRIX &mat, Matrix4Val src, float zBias)
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
    LogF("Set z-bias %d",zBias);

    LogF("  now: z1 = %10g/z0 + %10g",mat._43,mat._33);
    LogF("  def: z1 = %10g/z0 + %10g",src.Position()[2],src.Direction()[2]);
  }
#endif

}

// TODO: avoid scene here
#include "../scene.hpp"
#include "../lights.hpp"
#include "../camera.hpp"


void LogTextureF( const char *t, const TextureD3DT *x )
{
  LogF("Texture %s %s",t,x ? x->Name() : "--");
}

#if 0
  #define LogTexture(t,x) LogTextureF(t,x)
#else
  #define LogTexture(t,x)
#endif

#if DO_TEX_STATS
void LogSetProjT(const D3DMATRIX &mat)
{
  if (LogStatesOnce)
  {
    LogF
    (
      "Set Projection %g,%g,%g,%g",
      mat._11,mat._22,mat._33,mat._43
    );
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

#define LogSetProjT(mat)

#endif

void EngineDDT::SetBias( int bias )
{
  // Some devices support z-bias
  if (bias==_bias) return;
  _bias=bias;
  // Simulate z-bias using perspective matrix
  // Maybe better: simulate it using view-matrix

  if (_tlActive==TLEnabled)
  {
    // Setup projection matrix
    ProjectionChanged();
/*
    D3DMATRIX projMatrix;
    // set projection (based on camera)
    Camera *camera = GScene->GetCamera();
    ConvertProjectionMatrix(projMatrix,camera->ProjectionNormal(),bias);
    SetVertexShaderConstantF(VSC_ProjMatrix, (float*)&projMatrix, 4);
*/
  }
}

void EngineDDT::FlushQueues()
{
  FlushAndFreeAllQueues(_queueNo);
  //FlushAndFreeAllQueues(_queueTL);
}

#define NO_SET_TEX 0
#define NO_SET_TSS 0
#define NO_SET_RS 0

bool EngineDDT::IsAbleToDrawCheckOnly()
{
  return _resetStatus==D3D_OK;
}

void EngineDDT::SetBones(int minBoneIndex, const Matrix4 *bones, int nBones)
{
  // Set the view matrices and matrix offset
  if (nBones > 0)
  {
    // Set the view matrices
    float matrices[EngineDDT::LimitBones][3][4];
    const int vectorsNum = sizeof(matrices[0])/4/4;
    DoAssert(vectorsNum == 3); // Currently we use 3 vectors
    DoAssert(minBoneIndex >= 0);
    DoAssert(nBones <= _maxBones);
    DoAssert(nBones <= LimitBones);
    if (nBones>_maxBones) nBones = _maxBones;
    for (int i = 0; i < nBones; i++)
    {
      ConvertMatrixTransposed3(matrices[i], bones[minBoneIndex + i]);
    }
    SetVertexShaderConstantF(FREESPACE_START_REGISTER, (float*)matrices, nBones * vectorsNum);
  }

  // Set the matrix offset
  const float offset = minBoneIndex / 255.0f;
  D3DXVECTOR4 vector4(offset, offset, offset, offset);
  SetVertexShaderConstantF(VSC_MatrixOffset, vector4, 1);
}

void EngineDDT::SetSkinningType(ESkinningType skinningType, const Matrix4 *bones, int nBones)
{
  _skinningType = skinningType;

  // If some bone exists, remember them
  if (bones)
  {
    // If we have reasonable number of bones then set them at once, else copy them into bone array
    if (nBones <= _maxBones)
    {
      // Send bones to the GPU
      SetBones(0, bones, nBones);

      // Zero bones array, set flag
      _bones.Resize(0);
      _bonesWereSetDirectly = true;
    }
    else
    {
      _bones.Resize(nBones);
      for (int i = 0; i < nBones; i++)
      {
        _bones[i] = bones[i];
      }
      _bonesWereSetDirectly = false;
    }
  }
  else
  {
    // Set bone values somehow
    _bones.Resize(0);
    _bonesWereSetDirectly = true;
  }
}

#if _ENABLE_SKINNEDINSTANCING

void EngineDDT::SetSkinningTypeInstance(ESkinningType skinningType, int instanceIndex, const Matrix4 *bones, int nBones)
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

// avoid SHRT_MIN..SHRT_MAX  to make sure zero is well represented
//@{ UV compression range
const int UVCompressedMin = -SHRT_MAX;
const int UVCompressedMax = +SHRT_MAX;
//@}

bool EngineDDT::CanSeaUVMapping(int waterSegSize, float &mulUV, float &addUV) const
{ 
  // we want to transform from 0..waterSegSize to 
  mulUV = float(UVCompressedMax-UVCompressedMin)/waterSegSize;
  addUV = UVCompressedMin;
  return true;
}

void EngineDDT::SetWaterDepth(
  const WaterDepthQuad *water, int xRange, int zRange, float grid,
  float seaLevel,
  float shoreTop, float shoreBottom, float peakWaveTop, float peakWaveBottom
)
{
  // prepare constants for water "skinning"
  _skinningType = STNone; // this will reset any skinning currently set
  const int waterSegSize = 9;
  DoAssert(xRange==waterSegSize);
  DoAssert(zRange==waterSegSize);
  _waves.Realloc(xRange*zRange);
  _waves.Copy(water,xRange*zRange);
  _waterPar._shoreTop = shoreTop;
  _waterPar._shoreBottom = shoreBottom;
  _waterPar._peakWaveTop = peakWaveTop;
  _waterPar._peakWaveBottom = peakWaveBottom;
  _waterPar._seaLevel = seaLevel;
  _waterPar._grid = grid;
  _waterPar._waterSegSize = waterSegSize;
}

void EngineDDT::SetLandShadow(const float *shadow, int xRange, int zRange, float grid)
{
  // prepare constants for water "skinning"
  _skinningType = STNone; // this will reset any skinning currently set
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
  SetVertexShaderConstantF(VSC_LAND_SHADOW, (float*)landParams, (xRange-1)*(zRange-1));
  // add small eps. because we want to avoid truncating below 0
  // inv. grid size is less then 1/grid, because we want to avoid overflowing above seg. size
  float gridVal[4]={0.9999f/grid,xRange-1,1,(xRange-1)*0.5f+0.0001f};
  SetVertexShaderConstantF(VSC_LAND_SHADOWGRID_GRID__1__GRIDd2, (float*)gridVal, 1);
}

void EngineDDT::SetInstanceInfo(ColorVal color, float shadowIntensity, bool optimize)
{
  // Note: Don't try to implement lazy setting of PS constants here -
  // - potential reset would kill them

  // Remember instance parameters
  _instanceAlpha = alpha;
  _instanceShadowIntensity = shadowIntensity;

  // Set the VS constants with instance parameters
  float si[4]={alpha, 0, 0, shadowIntensity};
  SetVertexShaderConstantF(VSC_Alpha_X_X_LandShadowIntensity, (float*)si, 1);
}

void EngineDDT::SetPostFxEnabled(int value)
{
  // we can set various hints here
  // we could set _linearSpace based on Glow enabled / disable
  // but we rather set it directly based on SRGB write render state
}

void EngineDDT::SetPostFxParam(int fx, const float *param)
{
}

void EngineDDT::Postprocess(float deltaT)
{
// Direct3D 10 NEEDS UPDATE 

//   DoAssert(_linearSpace);
//   // no depth buffer will be required during glow or final
//   if (_depthBufferRT)
//   {
//     _d3DDevice->SetDepthStencilSurface(NULL);
//     _currRenderTargetDepthBufferSurface = NULL;
//   }
// 
//   if (IsDepthOfFieldEnabled() /*|| IsGlowEnabled() */ )
//   {
//     if (_ppGaussianBlur)
//     {
//       _ppGaussianBlur->Do(false);
//     }
//   }
// 
//   // Field of view
//   // We need the glow buffer if we want to perform Glow HDR
//   if (IsDepthOfFieldEnabled())
//   {
//     //FOV
//     if (_farOnly)
//     {
//       if (_ppDistanceDOF)
//       {
//         _ppDistanceDOF->Do(false);
//       }
//     }
//     else
//     {
//       if (_ppDOF)
//       {
//         _ppDOF->Do(false);
//       }
//     }
//   }
// 
//   // target backbuffer
//   if (_linearSpace)
//   {
//     if (_ppGlow)
//     {
//       float pars[3];
//       pars[0] = deltaT;
//       pars[1] = GScene->MainLight()->GetEyeAdaptMin();
//       pars[2] = GScene->MainLight()->GetEyeAdaptMax();
//       
//       _ppGlow->SetParameters(pars,lenof(pars));
//       _ppGlow->Do(true);
//     }
//   }
//   else
//   {
//     if (_ppFinal)
//     {
//       _ppFinal->Do(true);
//     }
//   }
// 
//   DoAssert(!_linearSpace);
// 
//   // SRGB write conversion was on before here - turn it off
//   if (_canWriteSRGB)
//   {
//     D3DSetRenderState(D3DRS_SRGBWRITEENABLE_OLD,FALSE);
//   }
// 
//   // some UI rendering may require depth buffer again
//   /*
//   // TODO: we need a depthbuffer suitable for the backbuffer here
//   if (_depthBufferRT)
//   {
//     _d3DDevice->SetDepthStencilSurface(_depthBufferRT);
//     _currRenderTargetDepthBufferSurface = _depthBufferRT;
//   }
//   */
}

void EngineDDT::NoPostprocess()
{
  // Final post process step (copying data to back buffer)
  // make sure no SRGB conversion is done during the copy
  _linearSpace = false;
  _rtIsSRGB = false;
  if (_ppFinal)
  {
    _ppFinal->Do(true);
  }

  // SRGB write conversion could be on - turn it off
  if (_canWriteSRGB)
  {
    D3DSetRenderState(D3DRS_SRGBWRITEENABLE_OLD,FALSE);
  }
}

/*!
  \patch 5089 Date 11/22/2006 by Flyman
  - Fixed: Tonemapping was not involved in the flare color calculation
*/
void EngineDDT::CalculateFlareIntensity(int x, int y, int sizeX, int sizeY)
{
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   // Calculate flare intensity and set texture on stage 15
//   _ppFlareIntensity->Do(x, y, sizeX, sizeY);
// 
//   // Set aperture texture on stage 14
//   SetTexture(14, _ppGlow->GetApertureTexture());
//   D3DSetSamplerState(14, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
//   D3DSetSamplerState(14, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
//   D3DSetSamplerState(14, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
// 
//   // Set decimation texture on stage 13
//   SetTexture(13, _ppGlow->GetDecimationTexture());
//   const bool decimatedSRGB = _downsampleFormat==D3DFMT_A8B8G8R8;
//   D3DSetSamplerState(13, D3DSAMP_SRGBTEXTURE_OLD, decimatedSRGB);
// 
//   // Set PS Constant 6
//   Color eyeSens = _ppGlow->EyeSens(_ppGlow->Cones());
//   SetPixelShaderConstantF(6, D3DXVECTOR4(eyeSens.R(), eyeSens.G(), eyeSens.B(), 1), 1);
// 
//   // Set PS Constant 7
//   float hdrMultiply = GetPostHDRBrightness() / GetHDRFactor();
//   SetPixelShaderConstantF(7, D3DXVECTOR4(hdrMultiply,hdrMultiply,hdrMultiply,hdrMultiply), 1);
}

bool IsAppPaused();

AbilityToDraw EngineDDT::IsAbleToDraw(bool wantReset)
{
  return ATDAble;
  // Direct3D 10 NEEDS UPDATE 
//   if (!_d3DDevice) return ATDUnable;
//   if (!_pp.Windowed && IsAppPaused()) return ATDUnable;
//   
//   /// loop through the FSM  
//   for(;;)
//   {
//     switch (_resetStatus)
//     {
//       case D3D_OK:
//         // verify the status is still OK
//         if (_resetDone && wantReset)
//         {
//           // report reset only once
//           _resetDone = false;
//           return ATDAbleReset;
//         }
//         return ATDAble;
//       case D3DERR_DEVICELOST:
//         LogF("*** Device lost");
//         _resetStatus = _d3DDevice->TestCooperativeLevel();
//         // wait until status is changed
//         if (_resetStatus==D3DERR_DEVICELOST)
//         {
//           return ATDUnable;
//         }
//         LogF("*** Device lost - status changed to %x",_resetStatus);
//         // status changed - process the change
//         break;
//       case D3DERR_DRIVERINTERNALERROR: // Internal error - perform Reset
//         RptF("*** Device lost - Driver Internal Error");
//         _resetStatus = D3DERR_DEVICENOTRESET;
//         break;
//       default: // Unknown state - perform Reset
//         RptF("*** Device lost - unknown reason %x",_resetStatus);
//         _resetStatus = D3DERR_DEVICENOTRESET;
//         break;
//       case D3DERR_DEVICENOTRESET: // Device reports Reset should be performed
//         if (Reset())
//         {
//           _resetStatus = D3D_OK;
//           if (wantReset)
//           {
//             _resetDone = false;
//             return ATDAbleReset;
//           }
//           // called is not interested about Reset, he wants to render
//           // typical usage is UI screen, which does not perform any scene preloading
//           _resetDone = true;
//           return ATDAble;
//         }
//         else
//         {
//           return ATDUnable;
//         }
//     }
//   }
}


void EngineDDT::InitDraw( bool clear, Color color )
{
  // If frame in progress then finish
  if( _d3dFrameOpen )
  {
    LogF("InitDraw done twice");
    return;
  }

  // if nobody will set the cursor within a frame, we will hide it
  _cursorSet = false;
  
  // Remember the back buffer
  // _d3DDevice->GetRenderTarget(0, _backBuffer.Init()); // Direct3D 10 NEEDS UPDATE 
  #if 0
    // it would be nice keep back buffer as a render target
    // this is not possible when render target alpha is used for shadow intensity
    // as backbuffer never contains alpha
    _currRenderTarget = NULL;
    _currRenderTargetSurface = _backBuffer;
  #else
    // use auxiliary render target
    // sometimes it may be usable as a texture at the same time
    // SetRenderTarget(_renderTargetSurfaceS,_renderTargetS,_depthBufferRT); // Direct3D 10 NEEDS UPDATE 
  #endif
  if (_canWriteSRGB)
  {
    // if using float16, perform SRGB writing only during final visualization
    static bool enableSRGB = true;
    _rtIsSRGB = enableSRGB /* Direct3D 10 NEEDS UPDATE && _rtFormat==D3DFMT_A8R8G8B8*/;
    D3DSetRenderState(D3DRS_SRGBWRITEENABLE_OLD,_rtIsSRGB);
  }
  // most of the scene rendering is done in linear space
  _linearSpace = true;
  
  IsAbleToDrawCheckOnly();

  if (!ResetNeeded())
  {
    PROFILE_DX_SCOPE(3dclr);

    // color needs to be halved - it will be double in HDR glow pass
    color = color * GetHDRFactor();

    // Clear render target and depth stencil
    Clear(true, clear, PackedColor(color));

    // Direct3D 10 NEEDS UPDATE 
    #if _ENABLE_CHEATS
      // set viewport as needed
      // by default viewport is set to contain whole render target?
      SetViewport(_viewW, _viewH);
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

  _textBank->StartFrame();

  D3DSetRenderState(D3DRS_STENCILWRITEMASK_OLD, STENCIL_FULL);
  
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
  _shadowLevel = 0;
  _shadowLevelNeeded = 0;

  // Invalidate the material
  TLMaterial invalidMat;
  invalidMat.diffuse = Color(-1,-1,-1,-1);
  invalidMat.ambient = Color(-1,-1,-1,-1);
  invalidMat.forcedDiffuse = Color(-1,-1,-1,-1);
  invalidMat.emmisive = Color(-1,-1,-1,-1);
  invalidMat.specFlags = 0;
  DoSetMaterial(invalidMat, false);

  // mark material as invalid
  _lastMat = TexMaterialLODInfo((TexMaterial *)-1, 0);
  _lastRenderingMode = RMCommon;

  // Zero the counter
  _dipCount = 0;
}

void EngineDDT::FinishDraw(int wantedDurationVSync, bool allBackBuffers, RString fileName)
{
  if( _d3dFrameOpen )
  {
    // FinishFrame performs texture loading - we want to see this in the stats
    _textBank->PerformMipmapLoading();
    
    //_sMesh =NULL;
    base::FinishDraw(wantedDurationVSync,allBackBuffers);

    base::DrawFinishTexts();
    
    _textBank->FinishFrame();

    CloseAllQueues(_queueNo);
    //CloseAllQueues(_queueTL);
    _d3dFrameOpen=false;
    DiscardVB();

#if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("---- EndScene");
    }
#endif


    // fill shadows (using stencil buffer)

#if LOG_LOCKS
    LogF("[[[ EndScene");
#endif
    D3DEndScene();
#if LOG_LOCKS
    LogF("]]] EndScene");
#endif


    // Do the final post process  (copying data to back buffer)
    // note: it should mostly do nothing, as data should be already there
    if (_ppFinal)
    {
      _ppFinal->Do(true);
    }

    // TODO: check - it might be better to call BackToFront before BeginScene
    // experimental - several BackToFront positions in code possible
    BackToFront();    
  }
}

bool EngineDDT::InitDrawDone()
{
  return _d3dFrameOpen;
}

void EngineDDT::BackBufferToAllBuffers()
{
  /*
  ComRef<IDirect3DSurface9> back;
  ComRef<IDirect3DSurface9> front;
  _d3DDevice->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,back.Init());
  _d3DDevice->GetFrontBuffer(0,front.Init());
  _d3DDevice->CopyRects(back,NULL,0,front,NULL);
  for (int i=1; i<_pp.BackBufferCount; i++)
  {
    ComRef<IDirect3DSurface9> backAdd;
    _d3DDevice->GetBackBuffer(i,D3DBACKBUFFER_TYPE_MONO,backAdd.Init());
    _d3DDevice->CopyRects(back,NULL,0,backAdd,NULL);
  }
  */
}

void EngineDDT::NextFrame()
{
  // swap frames - get ready for next frame
  // experimental - several BackToFront positions in code possible
  base::NextFrame();
}

void EngineDDT::D3DTextureDestroyed(TextureD3DHandle tex)
{
  // Remove the texture from _lastHandle array
  bool textureFound = false;
  for (int i = 0; i < lenof(_lastHandle); i++)
  {
    if (_lastHandle[i] == tex)
    {
      //PROFILE_DX_SCOPE(3dSTD);
      SetTexture(i, NULL);
      if (i == 0)
      {
        SetTexture0Color(HWhite, HWhite,true);
      }
      textureFound = true;
    }
  }

  // Remove texture from _setHandle array as well
  if (textureFound) SetupTextures();
}

void EngineDDT::TextureDestroyed( Texture *tex )
{
  TextureD3DT *texture = static_cast<TextureD3DT *>(tex);
  TextureD3DHandle handle = texture->GetHandle();
  if (handle) D3DTextureDestroyed(handle);
}


void EngineDDT::D3DEndScene()
{
  // Direct3D 10 NEEDS UPDATE 
//   PROFILE_DX_SCOPE(3deSc);
//   HRESULT err=_d3DDevice->EndScene();
//   if( err!=D3D_OK ) DDError9("Cannot end scene",err);
}
void EngineDDT::D3DBeginScene()
{
  // Direct3D 10 NEEDS UPDATE 
//   LogTexture("XXX Start XXX",NULL);
//   // check cooperative level (surfaces lost)
//   PROFILE_DX_SCOPE(3dbSc);
//   HRESULT err=_d3DDevice->BeginScene();
//   if( err!=D3D_OK )
//   {
//     DDError9("Cannot begin scene",err);
//   }
}

void EngineDDT::SetDebugMarker(int id)
{
  //D3DPERF_SetMarker_OLD();
  //_d3DDevice->SetDebugMarker(id);
}

int EngineDDT::ProfileBeginGraphScope(unsigned int color,const char *name) const
{
  // name is limited to 31 chars
  const int maxName = 31;
  int len = strlen(name);
  if (len>maxName) name += len-maxName;
  wchar_t c[256];
  mbstowcs(c, name, 256);
  return D3DPERF_BeginEvent_OLD(color,c);
}
int EngineDDT::ProfileEndGraphScope() const
{
  return D3DPERF_EndEvent_OLD();
}

void EngineDDT::PrepareMesh( int spec )
{
  // prepare internal variables
  SwitchTL(TLDisabled);
  Camera *cam = GScene->GetCamera();
  Matrix4Val proj = cam->ProjectionNormal();
  ChangeWDepth(proj(2,2),proj.Position()[2]);
  ChangeClipPlanes();
}

void EngineDDT::AddIBuffersAllocated( int n )
{
  _iBuffersAllocated += n;
  // be pessimistic: assume index buffer are always local
  if (_ibNonLocal)
  {
    _freeNonlocalVramAmount -= AlignVRAMVBSigned(n);
  }
  else
  {
    _freeLocalVramAmount -= AlignVRAMVBSigned(n);
  }
  _freeEstDirty++;
}

void EngineDDT::AddVBuffersAllocated( int n, bool dynamic )
{
  _vBuffersAllocated += n;
  if (dynamic ? _dynamicVBNonLocal : _staticVBNonLocal)
  {
    // the value is actually not used - we are tracking it only for diagnostics
    _freeNonlocalVramAmount -= AlignVRAMVBSigned(n);
  }
  else
  {
    // local VRAM value is the one we are tracking
    _freeLocalVramAmount -= AlignVRAMVBSigned(n);
  }
  _freeEstDirty++;
  // for each MB of allocated/freed we want the check to be performed a little bit sooner
  _freeEstDirty += abs(n)>>20;
}

void EngineDDT::AddLocalVRAMAllocated(int size)
{
  _freeLocalVramAmount-=size;
  _freeEstDirty++;
  _freeEstDirty += abs(size)>>20;
}

// note: IndexAllocT is actually the same type as IndexAlloc8, variable is shared
DEFINE_FAST_ALLOCATOR(IndexAllocT);
//DEFINE_FAST_ALLOCATOR(VertexAllocT);

VertexStaticDataT::VertexStaticDataT()
{
  //_vBufferSize = 0; // reserved sizes of resources
  //_vBufferUsed = 0; // used sizes
  _nShapes = 0;
}

VertexStaticDataT::~VertexStaticDataT()
{
  Dealloc();
}

/*!
  \param vertices Required number of vertices in the buffer
  \param vertexSize Required size of the buffer in bytes
  \param frequent indicated high performance is preferred (VRAM instead of AGP)
  \param dev 3D device
*/

void VertexStaticDataT::Init(EngineDDT *engine, int vertices, int vertexSize, bool frequent, ID3D10Device *dev)
{
  _engine = engine;
  _vertexSize = vertexSize;
  _usageDynamic = false;
  if (vertices>0)
  {
    _manager.Init(0,vertices,1);
    int byteSize = vertices*vertexSize;

    // make sure there is no extensive VRAM memory overhead
    engine->TextBankDD()->ReserveMemory(byteSize,2);
    
    //bool usageDynamic = !frequent && !_engine->GetVBuffersStatic();
    bool usageDynamic = false;
    #if _ENABLE_CHEATS
    if (ForceStaticVB) usageDynamic = false;
    #endif

    // Create the buffer description
    D3D10_BUFFER_DESC bd;
    bd.ByteWidth = byteSize;
    bd.Usage = D3D10_USAGE_DYNAMIC/*usageDynamic ? D3D10_USAGE_DYNAMIC : D3D10_USAGE_DEFAULT*/;
    bd.BindFlags = D3D10_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;
    bd.MiscFlags = 0;

RetryV:
    
    // note: goto before the constructor causes destruction
    EngineDDT::CheckMemTypeScope scope(_engine,usageDynamic ? "VBdynamic" : "VBstatic",byteSize);

    // Vertex buffer creation
    HRESULT err = dev->CreateBuffer(&bd, NULL, _vBuffer.Init());

    // Handle the error
    if (err != S_OK)
    {
      // If not enough memory, free something and try it again
      if (err = E_OUTOFMEMORY)
      {
        size_t freed = FreeOnDemandSystemMemoryLowLevel(byteSize);
        if (freed != 0) goto RetryV;
      }

      // Buffer creation failed
      // debugging opportunity
      LogF("Error during v-buffer creation: error %x",err);
      size_t freed = FreeOnDemandSystemMemoryLowLevel(byteSize);
      if (freed!=0) goto RetryV;
      engine->DDError9("CreateVertexBuffer",err);
      return;
    }
    engine->AddVBuffersAllocated(byteSize,usageDynamic);
    _usageDynamic = usageDynamic;
  }
}

#define V_OFFSET_CUSTOM 1


void VertexStaticDataT::Dealloc()
{
  if (_nShapes>0)
  {
    LogF("VertexStaticDataT:: %d shapes not removed",_nShapes);
  }
  if (_vBuffer)
  {
    _engine->AddVBuffersAllocated(-_manager.Size()*_vertexSize,_usageDynamic);
    _vBuffer.Free();
  }
}

static __forceinline void CopyNormal(VectorCompressed &tgt, Vector3Par src)
{
  float nX = (-src.X() + 1.0f) * 0.5f;
  float nY = (-src.Y() + 1.0f) * 0.5f;
  float nZ = (-src.Z() + 1.0f) * 0.5f;
  tgt.v = FAST_D3DRGBA_NORMAL_SAT(nX, nY, nZ, 1.0f);
}

static __forceinline UVPair CalcInvScale(const UVPair &srcMaxPos, const UVPair &srcMinPos)
{
  UVPair scale = {srcMaxPos.u-srcMinPos.u,srcMaxPos.v-srcMinPos.v};
  saturateMax(scale.u,1e-6);
  saturateMax(scale.v,1e-6);
  UVPair ret={1/scale.u,1/scale.v};
  return ret;
}

static __forceinline void CopyUV(UVPairCompressed &tgt, const UVPair &src, const UVPair &srcMaxUV, const UVPair &srcMinUV, const UVPair &invUV)
{
  // UV compression equation - opposite to label UVCOMPDECOMP
  Assert(srcMaxUV.u != -1e6);
  Assert(srcMaxUV.v != -1e6);
  Assert(srcMinUV.u != 1e6);
  Assert(srcMinUV.v != 1e6);
  tgt.u = toInt(((src.u - srcMinUV.u) * invUV.u) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
  tgt.v = toInt(((src.v - srcMinUV.v) * invUV.v) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
}

void *VertexStaticDataT::Lock(const VertexHeapT::HeapItem *item, int nVertex, int vertexSize)
{
RetryV:
  void *data = NULL;
  HRESULT err = GEngineDD->Map(_vBuffer, D3D10_MAP_WRITE_NO_OVERWRITE, 0, &data);
  if (err != S_OK || !data)
  {
    if (FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto RetryV;
    ErrF("VB Lock failed, %s",DXGetErrorString8(err));
    return NULL;
  }
  return &((BYTE*)data)[item->Memory() * vertexSize];
}

void VertexStaticDataT::Unlock()
{
  _vBuffer->Unmap();
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
  typedef SVertexT VertexType;
};

template <>
struct VertexDeclTraits<VD_Position_Normal_Tex0_ST>
{
  typedef SVertexNormalT VertexType;
};

template <>
struct VertexDeclTraits<VD_Position_Normal_Tex0_Tex1>
{
  typedef SVertexUV2T VertexType;
};

template <>
struct VertexDeclTraits<VD_Position_Normal_Tex0_Tex1_ST>
{
  typedef SVertexNormalUV2T VertexType;
};

template <>
struct VertexDeclTraits<VD_Position_Normal_Tex0_ST_Float3>
{
  typedef SVertexNormalCustomT VertexType;
};

template <>
struct VertexDeclTraits<VD_ShadowVolume>
{
  typedef SVertexShadowVolumeT VertexType;
};

template <>
struct VertexDeclTraits<VD_Position_Normal_Tex0_WeightsAndIndices>
{
  typedef SVertexSkinnedT VertexType;
};

template <>
struct VertexDeclTraits<VD_Position_Normal_Tex0_ST_WeightsAndIndices>
{
  typedef SVertexNormalSkinnedT VertexType;
};

template <>
struct VertexDeclTraits<VD_Position_Normal_Tex0_Tex1_WeightsAndIndices>
{
  typedef SVertexSkinnedUV2T VertexType;
};

template <>
struct VertexDeclTraits<VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices>
{
  typedef SVertexNormalSkinnedUV2T VertexType;
};

template <>
struct VertexDeclTraits<VD_ShadowVolumeSkinned>
{
  typedef SVertexShadowVolumeSkinnedT VertexType;
};

template <int vertexDecl>
struct VertexDeclInstancedTraits
{
};

template <>
struct VertexDeclInstancedTraits<VD_Position_Normal_Tex0>
{
  typedef SVertexInstancedT VertexType;
};

template <>
struct VertexDeclInstancedTraits<VD_Position_Normal_Tex0_ST>
{
  typedef SVertexNormalInstancedT VertexType;
};

template <>
struct VertexDeclInstancedTraits<VD_Position_Normal_Tex0_Tex1>
{
  typedef SVertexInstancedUV2T VertexType;
};

template <>
struct VertexDeclInstancedTraits<VD_Position_Normal_Tex0_Tex1_ST>
{
  typedef SVertexNormalInstancedUV2T VertexType;
};

template <>
struct VertexDeclInstancedTraits<VD_ShadowVolume>
{
  typedef SVertexShadowVolumeInstancedT VertexType;
};

template <int vertexDecl>
bool VertexStaticDataT::AddShape(const Shape &src, VertexBufferD3DT *vbuf, bool dynamic)
{
  typedef VertexDeclTraits<vertexDecl>::VertexType VertexType;

  // Vertex sizes must match
  if (_vertexSize != sizeof(VertexType)) return false;

  // When adding dynamic data, add only indices
  if (!dynamic)
  {
    // Retrieve the data pointer (Lock)
    VertexHeapT::HeapItem *item = _manager.Alloc(src.NVertex());
    if (!item) return false;
    ShapeGeometryLock<> lock(&src);

    // Calculate the vertex buffer maximum and minimum UV's for first or for both stages
    // Note that even first stage computing is sometimes useless (shadow volume)
    src.CalculateMaxAndMinUV(vbuf->_maxUV[0], vbuf->_minUV[0]);
    if (src.NUVStages() > 1) src.CalculateMaxAndMinUV(1, vbuf->_maxUV[1], vbuf->_minUV[1]);

    PROFILE_DX_SCOPE(3dvbC);
    vbuf->_vAlloc = item;
    void *data = Lock(item, src.NVertex(), sizeof(VertexType));
    if (!data) return false;

    // Fill out the particular data
    AddShapeData<vertexDecl>(data, src, vbuf);

    // Process the data (Unlock)
    Unlock();

    _nShapes++;
    REPORT_ADDSHAPE
  }
  else
  {
    vbuf->_vAlloc = NULL;
  }

  return true;
}

template <>
void VertexStaticDataT::AddShapeData<VD_Position_Normal_Tex0>(void *data, const Shape &src, const VertexBufferD3DT *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0>::VertexType VertexType;

  VertexType *sData = (VertexType *)data;
  const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3> &norm = src.GetNormArray();

  UVPair invUV = CalcInvScale(vbuf->_maxUV[0], vbuf->_minUV[0]);
  int nVertex = src.NVertex();
  for (int i = 0; i < nVertex; i++)
  {
    sData->pos = pos[i];
    CopyNormal(sData->norm, norm[i]);
    CopyUV(sData->t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
    sData++;
  }
}

template <>
void VertexStaticDataT::AddShapeData<VD_Position_Normal_Tex0_ST>(void *data, const Shape &src, const VertexBufferD3DT *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0_ST>::VertexType VertexType;

  const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3> &norm = src.GetNormArray();

  UVPair invUV = CalcInvScale(vbuf->_maxUV[0], vbuf->_minUV[0]);

  // If ST is present in the source, then use it, else generate it
  if (src.IsSTPresent())
  {
    VertexType *sData = (VertexType*)data;
    const STPair *st = &src.ST(0);
    int nVertex = src.NVertex();
    for (int i = 0; i < nVertex; i++)
    {
      sData->pos = pos[i];
      CopyNormal(sData->norm, norm[i]);
      CopyUV(sData->t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
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
      sData->pos = (*pos)[i];
      CopyNormal(sData->norm,(*norm)[i]);
      CopyUV(sData->t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
      CopyNormal(sData->s,(*norm)[i]);
      CopyNormal(sData->t,(*norm)[i]);
      sData++;
    }
  }
}

template <>
void VertexStaticDataT::AddShapeData<VD_Position_Normal_Tex0_Tex1>(void *data, const Shape &src, const VertexBufferD3DT *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0_Tex1>::VertexType VertexType;

  VertexType *sData = (VertexType *)data;
  const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
  const CondensedAutoArray<UVPair> *uv1 = &src.GetUVArray(1);
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3> &norm = src.GetNormArray();
  
  const UVPair *minUV1 = &vbuf->_minUV[1];
  const UVPair *maxUV1 = &vbuf->_maxUV[1];
  if (uv1->Size()!=src.NVertex())
  {
    Fail("2nd UV set needed, but not defined");
    uv1 = uv;
    minUV1 = &vbuf->_minUV[0];
    maxUV1 = &vbuf->_maxUV[0];
  }
  UVPair invUV = CalcInvScale(vbuf->_maxUV[0], vbuf->_minUV[0]);
  UVPair invUV1 = CalcInvScale(*maxUV1, *minUV1);

  int nVertex = src.NVertex();
  for (int i = 0; i < nVertex; i++)
  {
    sData->pos = pos[i];
    CopyNormal(sData->norm, norm[i]);
    CopyUV(sData->t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
    CopyUV(sData->t1, (*uv1)[i], *maxUV1, *minUV1, invUV1);
    sData++;
  }
}

template <>
void VertexStaticDataT::AddShapeData<VD_Position_Normal_Tex0_Tex1_ST>(void *data, const Shape &src, const VertexBufferD3DT *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0_Tex1_ST>::VertexType VertexType;

  const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
  const CondensedAutoArray<UVPair> *uv1 = &src.GetUVArray(1);
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3> &norm = src.GetNormArray();

  UVPair invUV = CalcInvScale(vbuf->_maxUV[0], vbuf->_minUV[0]);
  UVPair invUV1 = CalcInvScale(vbuf->_maxUV[1], vbuf->_minUV[1]);

  // If ST is present in the source, then use it, else generate it
  if (src.IsSTPresent())
  {
    VertexType *sData = (VertexType*)data;
    const STPair *st = &src.ST(0);
    int nVertex = src.NVertex();
    for (int i = 0; i < nVertex; i++)
    {
      sData->pos = pos[i];
      CopyNormal(sData->norm, norm[i]);
      CopyUV(sData->t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
      CopyUV(sData->t1, (*uv1)[i], vbuf->_maxUV[1], vbuf->_minUV[1], invUV1);
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
      sData->pos = pos[i];
      CopyNormal(sData->norm, norm[i]);
      CopyUV(sData->t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
      CopyUV(sData->t1, (*uv1)[i], vbuf->_maxUV[1], vbuf->_minUV[1], invUV1);
      CopyNormal(sData->s, norm[i]);
      CopyNormal(sData->t, norm[i]);
      sData++;
    }
  }
}

template <>
void VertexStaticDataT::AddShapeData<VD_Position_Normal_Tex0_ST_Float3>(void *data, const Shape &src, const VertexBufferD3DT *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0_ST_Float3>::VertexType VertexType;

  VertexType *sData = (VertexType*)data;
  const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3> &norm = src.GetNormArray();

  DoAssert (src.IsSTPresent());
  DoAssert (src.IsCustomPresent());
  sData = (VertexType *)data;
  const Vector3 *custom = src.GetCustomArray().Data();
  const STPair *st = &src.ST(0);
  
  UVPair invUV = CalcInvScale(vbuf->_maxUV[0], vbuf->_minUV[0]);

  int nVertex = src.NVertex();
  for (int i = 0; i < nVertex; i++)
  {
    sData->pos = pos[i];
    CopyNormal(sData->norm, norm[i]);
    CopyUV(sData->t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
    CopyNormal(sData->s, st->s);
    CopyNormal(sData->t, st->t);
    sData->custom = *custom;
    custom++;
    sData++;
    st++;
  }
}

template <>
void VertexStaticDataT::AddShapeData<VD_ShadowVolume>(void *data, const Shape &src, const VertexBufferD3DT *vbuf)
{
  typedef VertexDeclTraits<VD_ShadowVolume>::VertexType VertexType;

  VertexType *sData = (VertexType*)data;
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3> &norm = src.GetNormArray();

  int nVertex = src.NVertex();
  for (int i = 0; i < nVertex; i++)
  {
    sData->pos = pos[i];
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
void VertexStaticDataT::AddShapeData<VD_Position_Normal_Tex0_WeightsAndIndices>(void *data, const Shape &src, const VertexBufferD3DT *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0_WeightsAndIndices>::VertexType VertexType;

  // Copy position, normal and UV
  VertexType *sData = (VertexType *)data;
  const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3> &norm = src.GetNormArray();

  UVPair invUV = CalcInvScale(vbuf->_maxUV[0], vbuf->_minUV[0]);

  // Copy weights and indices
  const AutoArray<AnimationRTWeight> &a = src.GetVertexBoneRef();

  if (a.Size() > 0)
  {
    int nVertex = src.NVertex();
    for (int i = 0; i < nVertex; i++)
    {
      sData[i].pos = pos[i];
      CopyNormal(sData[i].norm, norm[i]);
      CopyUV(sData[i].t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
      FillWeightsAndIndices(sData[i].weights,sData[i].indices,a[i]);
    }
  }
  else
  {
    RptF("Warning: Object is animated, but no skeleton is defined");
    for (int i=src.NVertex(); --i>=0;)
    {
      sData[i].pos = pos[i];
      CopyNormal(sData[i].norm, norm[i]);
      CopyUV(sData[i].t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
      sData[i].indices[0] = 0;
      sData[i].weights[0] = 0;
      sData[i].indices[1] = sData[i].indices[2] = sData[i].indices[3] = 0;
      sData[i].weights[1] = sData[i].weights[2] = sData[i].weights[3] = 0;
    }
  }
}

template <>
void VertexStaticDataT::AddShapeData<VD_Position_Normal_Tex0_ST_WeightsAndIndices>(void *data, const Shape &src, const VertexBufferD3DT *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0_ST_WeightsAndIndices>::VertexType VertexType;

  // Copy position, normal and UV
  VertexType *sData = (VertexType *)data;
  const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3> &norm = src.GetNormArray();

  UVPair invUV = CalcInvScale(vbuf->_maxUV[0], vbuf->_minUV[0]);

  const AutoArray<AnimationRTWeight> &a = src.GetVertexBoneRef();

  // If ST is present in the source, then use it, else generate it
  if (src.IsSTPresent())
  {
    const STPair *st = &src.ST(0);
    if (a.Size() > 0)
    {
      int nVertex = src.NVertex();
      for (int i = 0; i < nVertex; i++)
      {
        sData[i].pos = pos[i];
        CopyNormal(sData[i].norm, norm[i]);
        CopyUV(sData[i].t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
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
        sData[i].pos = pos[i];
        CopyNormal(sData[i].norm, norm[i]);
        CopyUV(sData[i].t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
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
        sData[i].pos = pos[i];
        CopyNormal(sData[i].norm, norm[i]);
        CopyUV(sData[i].t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
        FillWeightsAndIndices(sData[i].weights,sData[i].indices,a[i]);
        CopyNormal(sData[i].s,(*norm)[i]);
        CopyNormal(sData[i].t,(*norm)[i]);
      }
    }
    else
    {
      RptF("Warning: Object is animated, but no skeleton is defined");
      for (int i=src.NVertex(); --i>=0;)
      {
        sData[i].pos = pos[i];
        CopyNormal(sData[i].norm, norm[i]);
        CopyUV(sData[i].t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
        sData[i].indices[0] = 0;
        sData[i].weights[0] = 0;
        sData[i].indices[1] = sData[i].indices[2] = sData[i].indices[3] = 0;
        sData[i].weights[1] = sData[i].weights[2] = sData[i].weights[3] = 0;
        CopyNormal(sData[i].s,(*norm)[i]);
        CopyNormal(sData[i].t,(*norm)[i]);
      }
    }
  }
}

template <>
void VertexStaticDataT::AddShapeData<VD_Position_Normal_Tex0_Tex1_WeightsAndIndices>(void *data, const Shape &src, const VertexBufferD3DT *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0_Tex1_WeightsAndIndices>::VertexType VertexType;

  // Copy position, normal and UV
  VertexType *sData = (VertexType *)data;
  const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
  const CondensedAutoArray<UVPair> *uv1 = &src.GetUVArray(1);
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3> &norm = src.GetNormArray();

  UVPair invUV = CalcInvScale(vbuf->_maxUV[0], vbuf->_minUV[0]);
  UVPair invUV1 = CalcInvScale(vbuf->_maxUV[1], vbuf->_minUV[1]);

  // Copy weights and indices
  const AutoArray<AnimationRTWeight> &a = src.GetVertexBoneRef();

  if (a.Size() > 0)
  {
    int nVertex = src.NVertex();
    for (int i = 0; i < nVertex; i++)
    {
      sData[i].pos = pos[i];
      CopyNormal(sData[i].norm, norm[i]);
      CopyUV(sData[i].t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
      CopyUV(sData[i].t1, (*uv1)[i], vbuf->_maxUV[1], vbuf->_minUV[1], invUV1);

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
      sData[i].pos = pos[i];
      CopyNormal(sData[i].norm, norm[i]);
      CopyUV(sData[i].t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
      CopyUV(sData[i].t1, (*uv1)[i], vbuf->_maxUV[1], vbuf->_minUV[1], invUV1);
      sData[i].indices[0] = 0;
      sData[i].weights[0] = 0;
      sData[i].indices[1] = sData[i].indices[2] = sData[i].indices[3] = 0;
      sData[i].weights[1] = sData[i].weights[2] = sData[i].weights[3] = 0;
    }
  }
}

template <>
void VertexStaticDataT::AddShapeData<VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices>(void *data, const Shape &src, const VertexBufferD3DT *vbuf)
{
  typedef VertexDeclTraits<VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices>::VertexType VertexType;

  // Copy position, normal and UV
  VertexType *sData = (VertexType *)data;
  const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
  const CondensedAutoArray<UVPair> *uv1 = &src.GetUVArray(1);
  const UVPair *minUV1 = &vbuf->_minUV[1];
  const UVPair *maxUV1 = &vbuf->_maxUV[1];
  if (uv1->Size()!=src.NVertex())
  {
    Fail("2nd UV set needed, but not defined");
    uv1 = uv;
    minUV1 = &vbuf->_minUV[0];
    maxUV1 = &vbuf->_maxUV[0];
  }
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3> &norm = src.GetNormArray();

  UVPair invUV = CalcInvScale(vbuf->_maxUV[0], vbuf->_minUV[0]);
  UVPair invUV1 = CalcInvScale(*maxUV1, *minUV1);

  const AutoArray<AnimationRTWeight> &a = src.GetVertexBoneRef();

  // If ST is present in the source, then use it, else generate it
  if (src.IsSTPresent())
  {
    const STPair *st = &src.ST(0);
    if (a.Size() > 0)
    {
      int nVertex = src.NVertex();
      for (int i = 0; i < nVertex; i++)
      {
        sData[i].pos = pos[i];
        CopyNormal(sData[i].norm, norm[i]);
        CopyUV(sData[i].t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
        CopyUV(sData[i].t1, (*uv1)[i], *maxUV1, *minUV1, invUV1);
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
        sData[i].pos = pos[i];
        CopyNormal(sData[i].norm, norm[i]);
        CopyUV(sData[i].t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
        CopyUV(sData[i].t1, (*uv1)[i], *maxUV1, *minUV1, invUV1);
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
        sData[i].pos = pos[i];
        CopyNormal(sData[i].norm, norm[i]);
        CopyUV(sData[i].t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
        CopyUV(sData[i].t1, (*uv1)[i], *maxUV1, *minUV1, invUV1);
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
        sData[i].pos = pos[i];
        CopyNormal(sData[i].norm, norm[i]);
        CopyUV(sData[i].t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
        CopyUV(sData[i].t1, (*uv1)[i], *maxUV1, *minUV1, invUV1);
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

void FillOutShadowVolumeVertexWithNeighbors(int dataIndex, VertexIndex vertex, VertexIndex vertexA, VertexIndex vertexB, SVertexShadowVolumeSkinnedT *sData, const Vector3 *pos, int indices)
{
  // Should we get a degenerated triangle, then skip it
  // (it will be filled another time by a non-degenerated neighbor)
  if ((pos[vertex] == pos[vertexA]) || (pos[vertexA] == pos[vertexB]) || (pos[vertexB] == pos[vertex])) return;

#if _KNI
  sData[dataIndex].pos = Vector3P(pos[vertex].X(),pos[vertex].Y(),pos[vertex].Z());
  sData[dataIndex].weights[0] = sData[dataIndex].weights[1] = sData[dataIndex].weights[2] = sData[dataIndex].weights[3] = 0;
  sData[dataIndex].indices[0] = sData[dataIndex].indices[1] = sData[dataIndex].indices[2] = sData[dataIndex].indices[3] = indices;
  
  sData[dataIndex].posA = Vector3P(pos[vertexA].X(),pos[vertexA].Y(),pos[vertexA].Z());
  sData[dataIndex].weightsA[0] = sData[dataIndex].weightsA[1] = sData[dataIndex].weightsA[2] = sData[dataIndex].weightsA[3] = 0;
  sData[dataIndex].indicesA[0] = sData[dataIndex].indicesA[1] = sData[dataIndex].indicesA[2] = sData[dataIndex].indicesA[3] = indices;
  
  sData[dataIndex].posB = Vector3P(pos[vertexB].X(),pos[vertexB].Y(),pos[vertexB].Z());
  sData[dataIndex].weightsB[0] = sData[dataIndex].weightsB[1] = sData[dataIndex].weightsB[2] = sData[dataIndex].weightsB[3] = 0;
  sData[dataIndex].indicesB[0] = sData[dataIndex].indicesB[1] = sData[dataIndex].indicesB[2] = sData[dataIndex].indicesB[3] = indices;
#else
  sData[dataIndex].pos = pos[vertex];
  sData[dataIndex].weights[0] = sData[dataIndex].weights[1] = sData[dataIndex].weights[2] = sData[dataIndex].weights[3] = 0;
  sData[dataIndex].indices[0] = sData[dataIndex].indices[1] = sData[dataIndex].indices[2] = sData[dataIndex].indices[3] = indices;
  
  sData[dataIndex].posA = pos[vertexA];
  sData[dataIndex].weightsA[0] = sData[dataIndex].weightsA[1] = sData[dataIndex].weightsA[2] = sData[dataIndex].weightsA[3] = 0;
  sData[dataIndex].indicesA[0] = sData[dataIndex].indicesA[1] = sData[dataIndex].indicesA[2] = sData[dataIndex].indicesA[3] = indices;
  
  sData[dataIndex].posB = pos[vertexB];
  sData[dataIndex].weightsB[0] = sData[dataIndex].weightsB[1] = sData[dataIndex].weightsB[2] = sData[dataIndex].weightsB[3] = 0;
  sData[dataIndex].indicesB[0] = sData[dataIndex].indicesB[1] = sData[dataIndex].indicesB[2] = sData[dataIndex].indicesB[3] = indices;
#endif

}


template <>
void VertexStaticDataT::AddShapeData<VD_ShadowVolumeSkinned>(void *data, const Shape &src, const VertexBufferD3DT *vbuf)
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
      sData[i].pos = pos[i];
      FillWeightsAndIndices(sData[i].weights,sData[i].indices,a[i]);
      
      VertexIndex aIndex = neighborBoneRef[i]._posA;
      if ((aIndex >= 0) && (aIndex < nVertex))
      {
        sData[i].posA = pos[aIndex];
      }
      else
      {
        Fail("Error: Neighbour index out of bounds");
      }
      FillWeightsAndIndices(sData[i].weightsA,sData[i].indicesA,neighborBoneRef[i]._rtwA);

      VertexIndex bIndex = neighborBoneRef[i]._posB;
      if ((bIndex >= 0) && (bIndex < nVertex))
      {
        sData[i].posB = pos[bIndex];
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
    RptF("Warning: Object is animated, but no skeleton is defined");
    // Go through all the sections
    for (int i = 0; i < src.NSections(); i++)
    {
      const ShapeSection &sec = src.GetSection(i);

      // Skip sections not designed for drawing
      if (sec.Special() & (IsHidden|IsHiddenProxy)) continue;

      // Go through all the faces in the section
      for (Offset o = sec.beg; o < sec.end; src.NextFace(o))
      {
        const Poly &face = src.Face(o);
        if (face.N() != 3)
        {
          LogF("Error: Shadow volume polygon doesn't have 3 vertices - it's vertices remained uninitialized");
          continue;
        }

        // Fill out three vertices
        FillOutShadowVolumeVertexWithNeighbors(face.GetVertex(0), face.GetVertex(0), face.GetVertex(1), face.GetVertex(2), sData, pos, 0);
        FillOutShadowVolumeVertexWithNeighbors(face.GetVertex(1), face.GetVertex(1), face.GetVertex(2), face.GetVertex(0), sData, pos, 0);
        FillOutShadowVolumeVertexWithNeighbors(face.GetVertex(2), face.GetVertex(2), face.GetVertex(0), face.GetVertex(1), sData, pos, 0);
      }
    }
  }
}

template <int vertexDecl>
bool VertexStaticDataT::AddShapeInstanced(
  const Shape &src, VertexBufferD3DT *vbuf, bool dynamic, int instancesCount)
{
  typedef VertexDeclInstancedTraits<vertexDecl>::VertexType VertexType;

  // Vertex sizes must match
  if (_vertexSize != sizeof(VertexType)) return false;

  // When adding dynamic data, add only indices
  if (!dynamic)
  {
    // Retrieve the data pointer (Lock)
    VertexHeapT::HeapItem *item = _manager.Alloc(src.NVertex() * instancesCount);
    if (!item) return false;
    ShapeGeometryLock<> lock(&src);

    // Calculate the vertex buffer maximum and minimum UV's for first or for both stages
    // Note that even first stage computing is sometimes useless (shadow volume)
    src.CalculateMaxAndMinUV(vbuf->_maxUV[0], vbuf->_minUV[0]);
    if (src.NUVStages() > 1) src.CalculateMaxAndMinUV(1, vbuf->_maxUV[1], vbuf->_minUV[1]);

    PROFILE_DX_SCOPE(3dvbC);
    vbuf->_vAlloc = item;
    void *data = Lock(item, src.NVertex() * instancesCount, sizeof(VertexType));
    if (!data) return false;

    // Fill out the particular data
    AddShapeInstancedData<vertexDecl>(data, src, vbuf, instancesCount);

    // Process the data (Unlock)
    Unlock();

    _nShapes++;
    REPORT_ADDSHAPE
  }
  else
  {
    vbuf->_vAlloc = NULL;
  }

  return true;
}

template <>
void VertexStaticDataT::AddShapeInstancedData<VD_Position_Normal_Tex0>(void *data, const Shape &src, const VertexBufferD3DT *vbuf, int instancesCount)
{
  typedef VertexDeclInstancedTraits<VD_Position_Normal_Tex0>::VertexType VertexType;

  // Copy position, normal and UV
  VertexType *sData = (VertexType *)data;
  const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3> &norm = src.GetNormArray();

  UVPair invUV = CalcInvScale(vbuf->_maxUV[0], vbuf->_minUV[0]);

  int nVertex = src.NVertex();
  for (int instance = 0; instance < instancesCount; instance++)
  {
    for (int i = 0; i < nVertex; i++)
    {
      sData->pos = pos[i];
      CopyNormal(sData->norm, norm[i]);
      CopyUV(sData->t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
      for (int j = 0; j < 4; j++) sData->index[j] = instance;
      sData++;
    }
  }
}

template <>
void VertexStaticDataT::AddShapeInstancedData<VD_Position_Normal_Tex0_ST>(void *data, const Shape &src, const VertexBufferD3DT *vbuf, int instancesCount)
{
  typedef VertexDeclInstancedTraits<VD_Position_Normal_Tex0_ST>::VertexType VertexType;

  // Copy position, normal and UV
  const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3> &norm = src.GetNormArray();

  UVPair invUV = CalcInvScale(vbuf->_maxUV[0], vbuf->_minUV[0]);

  // If ST is present in the source, then use it, else generate it
  if (src.IsSTPresent())
  {
    VertexType *sData = (VertexType*)data;
    int nVertex = src.NVertex();
    for (int instance = 0; instance < instancesCount; instance++)
    {
      const STPair *st = &src.ST(0);
      for (int i = 0; i < nVertex; i++)
      {
        sData->pos = pos[i];
        CopyNormal(sData->norm, norm[i]);
        CopyUV(sData->t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
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
void VertexStaticDataT::AddShapeInstancedData<VD_Position_Normal_Tex0_Tex1>(void *data, const Shape &src, const VertexBufferD3DT *vbuf, int instancesCount)
{
  typedef VertexDeclInstancedTraits<VD_Position_Normal_Tex0_Tex1>::VertexType VertexType;

  // Copy position, normal and UV
  VertexType *sData = (VertexType *)data;
  const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
  const CondensedAutoArray<UVPair> *uv1 = &src.GetUVArray(1);
  const UVPair *minUV1 = &vbuf->_minUV[1];
  const UVPair *maxUV1 = &vbuf->_maxUV[1];
  if (uv1->Size()!=src.NVertex())
  {
    Fail("2nd UV set needed, but not defined");
    uv1 = uv;
    minUV1 = &vbuf->_minUV[0];
    maxUV1 = &vbuf->_maxUV[0];
  }
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3> &norm = src.GetNormArray();

  UVPair invUV = CalcInvScale(vbuf->_maxUV[0], vbuf->_minUV[0]);
  UVPair invUV1 = CalcInvScale(*maxUV1, *minUV1);

  int nVertex = src.NVertex();
  for (int instance = 0; instance < instancesCount; instance++)
  {
    for (int i = 0; i < nVertex; i++)
    {
      sData->pos = pos[i];
      CopyNormal(sData->norm, norm[i]);
      CopyUV(sData->t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
      CopyUV(sData->t1, (*uv1)[i], *maxUV1, *minUV1, invUV1);
      for (int j = 0; j < 4; j++) sData->index[j] = instance;
      sData++;
    }
  }
}

template <>
void VertexStaticDataT::AddShapeInstancedData<VD_Position_Normal_Tex0_Tex1_ST>(void *data, const Shape &src, const VertexBufferD3DT *vbuf, int instancesCount)
{
  typedef VertexDeclInstancedTraits<VD_Position_Normal_Tex0_Tex1_ST>::VertexType VertexType;

  // Copy position, normal and UV
  const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
  const CondensedAutoArray<UVPair> *uv1 = &src.GetUVArray(1);

  const UVPair *minUV1 = &vbuf->_minUV[1];
  const UVPair *maxUV1 = &vbuf->_maxUV[1];
  if (uv1->Size()!=src.NVertex())
  {
    Fail("2nd UV set needed, but not defined");
    uv1 = uv;
    minUV1 = &vbuf->_minUV[0];
    maxUV1 = &vbuf->_maxUV[0];
  }

  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3> &norm = src.GetNormArray();

  UVPair invUV = CalcInvScale(vbuf->_maxUV[0], vbuf->_minUV[0]);
  UVPair invUV1 = CalcInvScale(*maxUV1, *minUV1);

  // If ST is present in the source, then use it, else generate it
  if (src.IsSTPresent())
  {
    VertexType *sData = (VertexType*)data;
    int nVertex = src.NVertex();
    for (int instance = 0; instance < instancesCount; instance++)
    {
      const STPair *st = &src.ST(0);
      for (int i = 0; i < nVertex; i++)
      {
        sData->pos = pos[i];
        CopyNormal(sData->norm, norm[i]);
        CopyUV(sData->t0, (*uv)[i], vbuf->_maxUV[0], vbuf->_minUV[0], invUV);
        CopyUV(sData->t1, (*uv1)[i], *maxUV1, *minUV1, invUV1);
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
void VertexStaticDataT::AddShapeInstancedData<VD_ShadowVolume>(void *data, const Shape &src, const VertexBufferD3DT *vbuf, int instancesCount)
{
  typedef VertexDeclInstancedTraits<VD_ShadowVolume>::VertexType VertexType;

  VertexType *sData = (VertexType*)data;
  const AutoArray<Vector3> &pos = src.GetPosArray();
  const CondensedAutoArray<Vector3> &norm = src.GetNormArray();

  for (int instance = 0; instance < instancesCount; instance++)
  {
    int nVertex = src.NVertex();
    for (int i = 0; i < nVertex; i++)
    {
      sData->pos = pos[i];
      CopyNormal(sData->norm, norm[i]);
      for (int j = 0; j < 4; j++) sData->index[j] = instance;
      sData++;
    }
  }
}

bool VertexStaticDataT::AddShapeSpriteInstanced(const Shape &src, VertexBufferD3DT *vbuf, bool dynamic, int instancesCount)
{

  // when adding dynamic data, add only indices

  if (_vertexSize != sizeof(SVertexSpriteInstancedT)) return false;

  DoAssert(src.NVertex()==4);

  if (!dynamic)
  {
    VertexHeapT::HeapItem *item = _manager.Alloc(4 * instancesCount);
    if (!item)
    {
      return false; 
    }

    PROFILE_DX_SCOPE(3dvbC);

    vbuf->_vAlloc = item;
RetryV:
    void *data = NULL;
    HRESULT err = GEngineDD->Map(_vBuffer, D3D10_MAP_WRITE_NO_OVERWRITE, 0, &data);

    if (err!=D3D_OK || !data)
    {
      if (FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto RetryV;
      ErrF("VB Lock failed, %s",DXGetErrorString8(err));
      return false;
    }

    SVertexSpriteInstancedT *sData = (SVertexSpriteInstancedT *)data;
    for (int instance = 0; instance < instancesCount; instance++)
    {
      sData[instance * 4 + 0].t0u = 0;
      sData[instance * 4 + 0].t0v = 0;
      sData[instance * 4 + 0].index = instance;

      sData[instance * 4 + 1].t0u = 0;
      sData[instance * 4 + 1].t0v = 255;
      sData[instance * 4 + 1].index = instance;

      sData[instance * 4 + 2].t0u = 255;
      sData[instance * 4 + 2].t0v = 0;
      sData[instance * 4 + 2].index = instance;

      sData[instance * 4 + 3].t0u = 255;
      sData[instance * 4 + 3].t0v = 255;
      sData[instance * 4 + 3].index = instance;
    }

    _vBuffer->Unmap();

    _nShapes++;
    REPORT_ADDSHAPE
  }
  else
  {
    vbuf->_vAlloc = NULL;
  }

  return true;
}

#if _ENABLE_SKINNEDINSTANCING

bool VertexStaticDataT::AddShapeSkinnedInstanced(const Shape &src, VertexHeapT::HeapItem *&vIndex, bool dynamic, int instancesCount)
{

  // when adding dynamic data, add only indices

  DWORD flags = NoSysLockFlag;

  if (_vertexSize != sizeof(SVertexSkinnedT)) return false;

  if (!dynamic)
  {
    VertexHeapT::HeapItem *item = _manager.Alloc(src.NVertex() * instancesCount);
    if (!item)
    {
      return false; 
    }

    ShapeGeometryLock<> lock(&src);

    PROFILE_DX_SCOPE(3dvbC);

    vIndex = item;
RetryV:
    void *data = NULL;
    HRESULT err = _vBuffer->Lock(
      item->Memory()*sizeof(SVertexSkinnedT),src.NVertex()* instancesCount * sizeof(SVertexSkinnedT),
      &data,flags
      );

    if (err!=D3D_OK || !data)
    {
      if (FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto RetryV;
      ErrF("VB Lock failed, %s",DXGetErrorString8(err));
      return false;
    }

    // Copy position, normal and UV
    SVertexSkinnedT *sData = (SVertexSkinnedT *)data;
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
          sData->pos = pos[i];
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
          sData->pos = pos[i];
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

    _nShapes++;
    REPORT_ADDSHAPE
  }
  else
  {
    vIndex = NULL;
  }

  return true;
}

bool VertexStaticDataT::AddShapeNormalSkinnedInstanced(const Shape &src, VertexHeapT::HeapItem *&vIndex, bool dynamic, int instancesCount)
{

  // when adding dynamic data, add only indices

  DWORD flags = NoSysLockFlag;

  if (_vertexSize != sizeof(SVertexNormalSkinnedT)) return false;

  if (!dynamic)
  {
    VertexHeapT::HeapItem *item = _manager.Alloc(src.NVertex() * instancesCount);
    if (!item)
    {
      return false; 
    }

    ShapeGeometryLock<> lock(&src);

    PROFILE_DX_SCOPE(3dvbC);

    vIndex = item;
RetryV:
    void *data = NULL;
    HRESULT err = _vBuffer->Lock(
      item->Memory()*sizeof(SVertexNormalSkinnedT),src.NVertex()* instancesCount * sizeof(SVertexNormalSkinnedT),
      &data,flags
      );

    if (err!=D3D_OK || !data)
    {
      if (FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto RetryV;
      ErrF("VB Lock failed, %s",DXGetErrorString8(err));
      return false;
    }

    // Copy position, normal and UV
    SVertexNormalSkinnedT *sData = (SVertexNormalSkinnedT*)data;
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
            sData->pos = pos[i];
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
            sData->pos = pos[i];
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

bool VertexStaticDataT::AddShapeShadowVolumeSkinnedInstanced(const Shape &src, VertexHeapT::HeapItem *&vIndex, bool dynamic, int instancesCount)
{

  // when adding dynamic data, add only indices

  DWORD flags = NoSysLockFlag;

  if (_vertexSize != sizeof(SVertexShadowVolumeSkinnedT)) return false;

  if (!dynamic)
  {
    VertexHeapT::HeapItem *item = _manager.Alloc(src.NVertex() * instancesCount);
    if (!item)
    {
      return false; 
    }

    ShapeGeometryLock<> lock(&src);

    PROFILE_DX_SCOPE(3dvbC);

    vIndex = item;
RetryV:
    void *data = NULL;
    HRESULT err = _vBuffer->Lock(
      item->Memory()*sizeof(SVertexShadowVolumeSkinnedT),src.NVertex()* instancesCount * sizeof(SVertexShadowVolumeSkinnedT),
      &data,flags
      );

    if (err!=D3D_OK || !data)
    {
      if (FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto RetryV;
      ErrF("VB Lock failed, %s",DXGetErrorString8(err));
      return false;
    }

    SVertexShadowVolumeSkinnedT *sData = (SVertexShadowVolumeSkinnedT*)data;
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
        for (int i = 0; i < src.NSections(); i++)
        {
          const ShapeSection &sec = src.GetSection(i);

          // Skip sections not designed for drawing
          if (sec.Special() & (IsHidden|IsHiddenProxy)) continue;

          // Go through all the faces in the section
          for (Offset o = sec.beg; o < sec.end; src.NextFace(o))
          {
            const Poly &face = src.Face(o);
            if (face.N() != 3)
            {
              LogF("Error: Shadow volume polygon doesn't have 3 vertices - it's vertices remained uninitialized");
              continue;
            }

            // Fill out three vertices
            FillOutShadowVolumeVertexWithNeighbors(face.GetVertex(0) + instance * nVertex, face.GetVertex(0), face.GetVertex(1), face.GetVertex(2), sData, pos, instance * bonesStride);
            FillOutShadowVolumeVertexWithNeighbors(face.GetVertex(1) + instance * nVertex, face.GetVertex(1), face.GetVertex(2), face.GetVertex(0), sData, pos, instance * bonesStride);
            FillOutShadowVolumeVertexWithNeighbors(face.GetVertex(2) + instance * nVertex, face.GetVertex(2), face.GetVertex(0), face.GetVertex(1), sData, pos, instance * bonesStride);
          }
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

void VertexStaticDataT::RemoveShape(VertexHeapT::HeapItem *vIndex)
{
  #if 0 //_ENABLE_REPORT
    LogF("Resource %p: shape %d removed: %d",&_busy,_nShapes,Glob.frameID);
  #endif
  if (!vIndex) return;
  _nShapes--;
  _manager.Free(vIndex);
  if (_nShapes==0)
  {
    Assert(_manager.TotalBusy()==0);
  }
}

void EngineDDT::DeleteVB(VertexStaticDataT *vb)
{
  Verify( _statVBuffer.DeleteKey(vb) );
}
void EngineDDT::DeleteIB(IndexStaticDataT *ib)
{
  Verify( _statIBuffer.DeleteKey(ib) );
}


void ResourceBusyT::Used()
{
  _busyUntil = Glob.frameID+2;
}
bool ResourceBusyT::IsBusy() const
{
  return Glob.frameID<=_busyUntil;
}


template <int vertexDecl>
VertexStaticDataT *EngineDDT::AddShapeVertices(
  const Shape &src, VertexBufferD3DT &vbuf, bool dynamic, bool separate, bool frequent
)
{
  vbuf._vAlloc = NULL;
  if (dynamic) return NULL;

  typedef VertexDeclTraits<vertexDecl>::VertexType VertexType;
  
  // Number of items in the new vertex buffer
  int bufferVertexCount;
  int maxVertexCount = MaxVertexCount(VertexType);

  // If separate, then create separated buffer
  if (separate || src.NVertex()>=maxVertexCount)
  {
    bufferVertexCount = src.NVertex();
  }
  // If shared, then try to fit into existing buffer
  else
  {
    // Check if we will fit into some existing buffer
    for (int i=0; i<_statVBuffer.Size(); i++)
    {
      VertexStaticDataT *sd = _statVBuffer[i];
      if (!sd->IsBusy() && sd->AddShape<vertexDecl>(src, &vbuf, dynamic))
      {
        Fail("Buffer sharing not suported - frequent flag not working with it");
        return sd;
      }
    }
    bufferVertexCount = maxVertexCount;
  }

  // Either we didn't fit into existing or we use a separate buffer, so create a new buffer
  VertexStaticDataT *sd = new VertexStaticDataT;
  sd->Init(this, bufferVertexCount, sizeof(VertexType), frequent, _d3DDevice);

  // If we failed to init vertex buffer for the static data, then free static data and return error
  if (!sd->_vBuffer)
  {
    delete sd;
    return NULL;
  }

  // Add shape into the new buffer
  _statVBuffer.Add(sd);
  if (sd->AddShape<vertexDecl>(src, &vbuf, dynamic))
  {
    return sd;
  }
  return NULL;
}

template <int vertexDecl>
VertexStaticDataT *EngineDDT::AddShapeVerticesInstanced(
  const Shape &src, VertexBufferD3DT &vbuf, bool dynamic, bool separate, bool frequent,
  int instancesCount
)
{
  vbuf._vAlloc = NULL;
  if (dynamic) return NULL;

  typedef VertexDeclInstancedTraits<vertexDecl>::VertexType VertexType;

  // Number of items in the new vertex buffer
  int bufferVertexCount;
  int maxVertexCount = MaxVertexCount(VertexType);

  // If separate, then create separated buffer
  if (separate || src.NVertex() * instancesCount>=maxVertexCount)
  {
    bufferVertexCount = src.NVertex() * instancesCount;
  }
  // If shared, then try to fit into existing buffer
  else
  {
    // Check if we will fit into some existing buffer
    for (int i=0; i<_statVBuffer.Size(); i++)
    {
      VertexStaticDataT *sd = _statVBuffer[i];
      if (!sd->IsBusy() && sd->AddShapeInstanced<vertexDecl>(src, &vbuf, dynamic, instancesCount))
      {
        Fail("Buffer sharing not suported - frequent flag not working with it");
        return sd;
      }
    }
    bufferVertexCount = maxVertexCount;
  }

  // Either we didn't fit into existing or we use a separate buffer, so create a new buffer
  VertexStaticDataT *sd = new VertexStaticDataT;
  sd->Init(this, bufferVertexCount, sizeof(VertexType), frequent, _d3DDevice);

  // If we failed to init vertex buffer for the static data, then free static data and return error
  if (!sd->_vBuffer)
  {
    delete sd;
    return NULL;
  }

  // Add shape into the new buffer
  _statVBuffer.Add(sd);
  if (sd->AddShapeInstanced<vertexDecl>(src, &vbuf, dynamic, instancesCount))
  {
    return sd;
  }
  return NULL;
}

VertexStaticDataT *EngineDDT::AddShapeVerticesSpriteInstanced(
  const Shape &src, VertexBufferD3DT &vbuf, bool dynamic, bool separate, bool frequent, int instancesCount
)
{
  vbuf._vAlloc = NULL;
  if (dynamic) return NULL;

  // Number of items in the new vertex buffer
  int bufferVertexCount;
  int maxVertexCount = MaxVertexCount(SVertexSpriteInstancedT);

  // If separate, then create separated buffer
  if (separate || src.NVertex()>=maxVertexCount)
  {
    bufferVertexCount = 4 * instancesCount;
  }
  // If shared, then try to fit into existing buffer
  else
  {
    // Check if we will fit into some existing buffer
    for (int i=0; i<_statVBuffer.Size(); i++)
    {
      VertexStaticDataT *sd = _statVBuffer[i];
      if (!sd->IsBusy() && sd->AddShapeSpriteInstanced(src, &vbuf, dynamic, instancesCount))
      {
        Fail("Buffer sharing not suported - frequent flag not working with it");
        return sd;
      }
    }
    bufferVertexCount = maxVertexCount;
  }

  // Either we didn't fit into existing or we use a separate buffer, so create a new buffer
  VertexStaticDataT *sd = new VertexStaticDataT;
  sd->Init(this, bufferVertexCount, sizeof(SVertexSpriteInstancedT), frequent, _d3DDevice);

  // If we failed to init vertex buffer for the static data, then free static data and return error
  if (!sd->_vBuffer)
  {
    delete sd;
    return NULL;
  }

  // Add shape into the new buffer
  _statVBuffer.Add(sd);
  if (sd->AddShapeSpriteInstanced(src, &vbuf, dynamic, instancesCount))
  {
    return sd;
  }
  return NULL;
}

#if _ENABLE_SKINNEDINSTANCING

VertexStaticDataT *EngineDDT::AddShapeVerticesSkinnedInstanced(
  const Shape &src, VertexHeapT::HeapItem *&vIndex, bool dynamic, bool separate, bool frequent,
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
  int bufferVertexCount;
  int maxVertexCount = MaxVertexCount(SVertexSkinnedT);

  // If separate, then create separated buffer
  if (separate || src.NVertex() * instancesCount >= maxVertexCount)
  {
    bufferVertexCount = src.NVertex() * instancesCount;
  }
  // If shared, then try to fit into existing buffer
  else
  {
    // Check if we will fit into some existing buffer
    for (int i = 0; i < _statVBuffer.Size(); i++)
    {
      VertexStaticDataT *sd = _statVBuffer[i];
      if (!sd->IsBusy() && sd->AddShapeSkinnedInstanced(src, vIndex, dynamic, instancesCount))
      {
        Fail("Buffer sharing not suported - frequent flag not working with it");
        return sd;
      }
    }
    bufferVertexCount = maxVertexCount;
  }

  // Either we didn't fit into existing or we use a separate buffer, so create a new buffer
  VertexStaticDataT *sd = new VertexStaticDataT;
  sd->Init(this, bufferVertexCount, sizeof(SVertexSkinnedT), _d3DDevice);

  // If we failed to init vertex buffer for the static data, then free static data and return error
  if (!sd->_vBuffer)
  {
    delete sd;
    return NULL;
  }

  // Add shape into the new buffer
  _statVBuffer.Add(sd);
  if (sd->AddShapeSkinnedInstanced(src, vIndex, dynamic, instancesCount))
  {
    return sd;
  }
  return NULL;
}

VertexStaticDataT *EngineDDT::AddShapeVerticesNormalSkinnedInstanced(
  const Shape &src, VertexHeapT::HeapItem *&vIndex, bool dynamic, bool separate, bool frequent,
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
  int bufferVertexCount;
  int maxVertexCount = MaxVertexCount(SVertexNormalSkinnedT);

  // If separate, then create separated buffer
  if (separate || src.NVertex() * instancesCount >= maxVertexCount)
  {
    bufferVertexCount = src.NVertex() * instancesCount;
  }
  // If shared, then try to fit into existing buffer
  else
  {
    // Check if we will fit into some existing buffer
    for (int i = 0; i < _statVBuffer.Size(); i++)
    {
      VertexStaticDataT *sd = _statVBuffer[i];
      if (!sd->IsBusy() && sd->AddShapeNormalSkinnedInstanced(src, vIndex, dynamic, instancesCount))
      {
        Fail("Buffer sharing not suported - frequent flag not working with it");
        return sd;
      }
    }
    bufferVertexCount = maxVertexCount;
  }

  // Either we didn't fit into existing or we use a separate buffer, so create a new buffer
  VertexStaticDataT *sd = new VertexStaticDataT;
  sd->Init(this, bufferVertexCount, sizeof(SVertexNormalSkinnedT), _d3DDevice);

  // If we failed to init vertex buffer for the static data, then free static data and return error
  if (!sd->_vBuffer)
  {
    delete sd;
    return NULL;
  }

  // Add shape into the new buffer
  _statVBuffer.Add(sd);
  if (sd->AddShapeNormalSkinnedInstanced(src, vIndex, dynamic, instancesCount))
  {
    return sd;
  }
  return NULL;
}

VertexStaticDataT *EngineDDT::AddShapeVerticesShadowVolumeSkinnedInstanced(
  const Shape &src, VertexHeapT::HeapItem *&vIndex, bool dynamic, bool separate, bool frequent,
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
  int bufferVertexCount;
  int maxVertexCount = MaxVertexCount(SVertexShadowVolumeSkinnedT);

  // If separate, then create separated buffer
  if (separate || src.NVertex() * instancesCount >= maxVertexCount)
  {
    bufferVertexCount = src.NVertex() * instancesCount;
  }
  // If shared, then try to fit into existing buffer
  else
  {
    // Check if we will fit into some existing buffer
    for (int i = 0; i < _statVBuffer.Size(); i++)
    {
      VertexStaticDataT *sd = _statVBuffer[i];
      if (!sd->IsBusy() && sd->AddShapeShadowVolumeSkinnedInstanced(src, vIndex, dynamic, instancesCount))
      {
        Fail("Buffer sharing not suported - frequent flag not working with it");
        return sd;
      }
    }
    bufferVertexCount = maxVertexCount;
  }

  // Either we didn't fit into existing or we use a separate buffer, so create a new buffer
  VertexStaticDataT *sd = new VertexStaticDataT;
  sd->Init(this, bufferVertexCount, sizeof(SVertexShadowVolumeSkinnedT), _d3DDevice);

  // If we failed to init vertex buffer for the static data, then free static data and return error
  if (!sd->_vBuffer)
  {
    delete sd;
    return NULL;
  }

  // Add shape into the new buffer
  _statVBuffer.Add(sd);
  if (sd->AddShapeShadowVolumeSkinnedInstanced(src, vIndex, dynamic, instancesCount))
  {
    return sd;
  }
  return NULL;
}

#endif

//////////////////////{{


IndexStaticDataT::IndexStaticDataT()
{
  _nShapes = 0;

}

IndexStaticDataT::~IndexStaticDataT()
{
  Dealloc();
}

void IndexStaticDataT::Init(EngineDDT *engine, int indices, ID3D10Device *dev)
{
  _engine = engine;
  _manager.Free();
  if (indices > 0)
  {
    int byteSize = indices*sizeof(VertexIndex);

    // Create the buffer description
    D3D10_BUFFER_DESC bd;
    bd.ByteWidth = byteSize;
    bd.Usage = D3D10_USAGE_DYNAMIC/*usageDynamic ? D3D10_USAGE_DYNAMIC : D3D10_USAGE_DEFAULT*/;
    bd.BindFlags = D3D10_BIND_INDEX_BUFFER;
    bd.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;
    bd.MiscFlags = 0;

RetryI:

    EngineDDT::CheckMemTypeScope scope(_engine,"IBstatic",byteSize);

    // Index buffer creation
    HRESULT err = dev->CreateBuffer(&bd, NULL, _iBuffer.Init());

    // Handle the error
    if (err != D3D_OK)
    {
      // If not enough memory, free something and try it again
      if (err = E_OUTOFMEMORY)
      {
        size_t freed = FreeOnDemandSystemMemoryLowLevel(byteSize);
        if (freed != 0) goto RetryI;
      }

      // Buffer creation failed
      // debugging opportunity
      LogF("Error during i-buffer creation: error %x",err);
      size_t freed = FreeOnDemandSystemMemoryLowLevel(byteSize);
      if (freed!=0) goto RetryI;
      engine->DDError9("CreateIndexBuffer",err);
      return;
    }
    engine->AddIBuffersAllocated(byteSize);
    _manager.Init(0,indices,1);
  }
}

#define V_OFFSET_CUSTOM 1


void IndexStaticDataT::Dealloc()
{
  if (_nShapes>0)
  {
    LogF("IndexStaticDataT:: %d shapes not removed",_nShapes);
  }
  if (_iBuffer)
  {
    _engine->AddIBuffersAllocated(-_manager.Size()*sizeof(VertexIndex));
    _iBuffer.Free();
  }
}

int IndexStaticDataT::IndicesNeededTotal(const Shape &src, int nInstances)
{
  int indices = 0;
  for (Offset o=src.BeginFaces(); o<src.EndFaces(); src.NextFace(o))
  {
    const Poly &poly = src.Face(o);
    if (poly.N() < 3) continue;
    if ((poly.N() == 3) &&
      ( (poly.GetVertex(0) == poly.GetVertex(1)) || 
      (poly.GetVertex(1) == poly.GetVertex(2)) ||
      (poly.GetVertex(2) == poly.GetVertex(0)))) continue;
    indices += (poly.N()-2)*3;
  }
  return indices * nInstances;
}

int IndexStaticDataT::IndicesNeededStrippedTotal(const Shape &src, int nInstances)
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

/*!
\patch 1.24 Date 9/21/2001 by Ondra
- Fixed: Lighting of polygons by point lights was reversed on HW T&L.
\patch 1.32 Date 11/26/2001 by Ondra
- Fixed: Destroyed buildings did not collapse with HW T&L.
*/


bool IndexStaticDataT::AddShape(const Shape &src, IndexAllocT *&iIndex, int indicesTotal, bool dynamic, int nInstances)
{
  // let manager find some free space
  IndexHeapT::HeapItem *item = _manager.Alloc(indicesTotal);
  if (!item)
  {
    return false;
  }

  iIndex = item;

  _nShapes++;

  // when adding dynamic data, add only indices

  if (indicesTotal > 0)
  {
    PROFILE_DX_SCOPE(3dibC);

    // Lock the index buffer
RetryI:
    void *data = NULL;
    HRESULT err = GEngineDD->Map(_iBuffer, D3D10_MAP_WRITE_NO_OVERWRITE, 0, &data);
    if (err != S_OK || !data)
    {
      if (FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto RetryI;
      ErrF("IB Lock failed, %s",DXGetErrorString8(err));
      return false;
    }
    data = &((BYTE*)data)[iIndex->Memory() * sizeof(VertexIndex)];

    VertexIndex *iData = (VertexIndex *)data;

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
        // if part of the section is covered, the whole section should be convered as well
        Assert(sec.end<=coveredSoFar);
        continue;
      }
      coveredSoFar = sec.end;
      for (int instance=0; instance<nInstances; instance++)
      {
        int instanceOffset = src.NVertex()*instance;
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

    DoAssert(iData == &((VertexIndex *)data)[indicesTotal]);
    _iBuffer->Unmap();
  }
  return true;
}

bool IndexStaticDataT::AddShapeStripped(const Shape &src, IndexAllocT *&iIndex, int indicesTotal, bool dynamic, int nInstances)
{
  Fail("DX10 TODO");
  return false;
  // Direct3D 10 NEEDS UPDATE 
//   // let manager find some free space
//   IndexHeapT::HeapItem *item = _manager.Alloc(indicesTotal);
//   if (!item)
//   {
//     return false;
//   }
// 
//   iIndex = item;
// 
//   _nShapes++;
// 
//   // when adding dynamic data, add only indices
// 
//   if (indicesTotal>0)
//   {
//     PROFILE_DX_SCOPE(3dibC);
// RetryI:
//     void *data = NULL;
//     HRESULT err = _iBuffer->Lock(
//       iIndex->Memory()*sizeof(VertexIndex), indicesTotal * sizeof(VertexIndex),
//       &data,NoSysLockFlag
//     );
//     if (err!=D3D_OK || !data)
//     {
//       if (FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto RetryI;
//       ErrF("IB Lock failed, %s",DXGetErrorString8(err));
//       return false;
//     }
// 
//     VertexIndex *iData = (VertexIndex *)data;
// 
//     // Go through all the sections
//     for (int i = 0; i < src.NSections(); i++)
//     {
//       const ShapeSection &sec = src.GetSection(i);
//       Assert((OffsetDiff(sec.end, sec.beg) % PolyVerticesSize(3)) == 0);
//       int trianglesCount = OffsetDiff(sec.end, sec.beg) / PolyVerticesSize(3);
//       for (int instance = 0; instance < nInstances; instance++)
//       {
//         int instanceOffset = src.NVertex()*instance;
// 
//         // Add the first triangle
//         Offset o = sec.beg;
//         if (o < sec.end)
//         {
//           const Poly &poly = src.Face(o);
//           Assert(poly.N() == 3);
// 
//           // If we are not in the first instance, then add the concatenation sequence
//           if (instance > 0)
//           {
//             // If the previous instance has got odd number of triangles, then make it even
//             if (trianglesCount & 1)
//             {
//               *iData = iData[-1]; iData++;
//             }
//             *iData = iData[-1]; iData++;
//             *iData++ = poly.GetVertex(0)+instanceOffset;
//           }
//           *iData++ = poly.GetVertex(0)+instanceOffset;
//           *iData++ = poly.GetVertex(1)+instanceOffset;
//           *iData++ = poly.GetVertex(2)+instanceOffset;
//         }
// 
//         // Add the rest of the triangles
//         src.NextFace(o);
//         while (true)
//         {
//           if (o >= sec.end) break;
//           const Poly &polyA = src.Face(o);
//           Assert(polyA.N() == 3);
//           *iData++ = polyA.GetVertex(0)+instanceOffset;
//           src.NextFace(o);
// 
//           if (o >= sec.end) break;
//           const Poly &polyB = src.Face(o);
//           Assert(polyB.N() == 3);
//           *iData++ = polyB.GetVertex(2)+instanceOffset;
//           src.NextFace(o);
//         }
//       }
//     }
//     _iBuffer->Unlock();
//   }
//   return true;
}

bool IndexStaticDataT::AddShapeSprite(const Shape &src, IndexAllocT *&iIndex, int indicesTotal, bool dynamic, int nInstances)
{
  // let manager find some free space
  IndexHeapT::HeapItem *item = _manager.Alloc(indicesTotal);
  if (!item)
  {
    return false;
  }

  iIndex = item;

  _nShapes++;

  // when adding dynamic data, add only indices

  if (indicesTotal > 0)
  {
    PROFILE_DX_SCOPE(3dibC);

RetryI:
    void *data = NULL;
    HRESULT err;
    {
      PROFILE_DX_SCOPE(3dibL);
      err = GEngineDD->Map(_iBuffer, D3D10_MAP_WRITE_NO_OVERWRITE, 0, &data);
    }
    if (err!=D3D_OK || !data)
    {
      if (FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto RetryI;
      ErrF("IB Lock failed, %s",DXGetErrorString8(err));
      return false;
    }

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
    _iBuffer->Unmap();
  }
  return true;
}

void IndexStaticDataT::RemoveShape(IndexAllocT *iIndex)
{
  _manager.Free(iIndex);
  _nShapes--;
  if (_nShapes==0)
  {
    Assert(_manager.TotalBusy()==0);
  }
}

IndexStaticDataT *EngineDDT::AddShapeIndices(const Shape &src, IndexAllocT *&iIndex, bool dynamic, int nInstances)
{
  iIndex = NULL;

  // Maximum number of indices
  const int maxIndices = 0;
  //const int maxIndices = 40*1024;

  // Get the number of indices
  int indicesTotal;
  indicesTotal = IndexStaticDataT::IndicesNeededTotal(src, nInstances);

  // It's a nonsense to have zero indices
  Assert(indicesTotal > 0);

  // Number of items in the new index buffer
  int bufferIndexCount;

  // If separate, then create separated buffer
  if (indicesTotal > maxIndices)
  {
    bufferIndexCount = indicesTotal;
  }
  else
  {
    // Check if we will fit into some existing buffer
    for (int i=0; i<_statIBuffer.Size(); i++)
    {
      IndexStaticDataT *sd = _statIBuffer[i];
      if (sd->IsBusy()) continue;
      if (sd->AddShape(src,iIndex,indicesTotal,dynamic,nInstances))
      {
        return sd;
      }
    }
    bufferIndexCount = maxIndices;
  }

  // Either we didn't fit into existing or we use a separate buffer, so create a new buffer
  IndexStaticDataT *sd = new IndexStaticDataT;
  sd->Init(this, bufferIndexCount, _d3DDevice);

  // If we failed to init index buffer for the static data, then free static data and return error
  if (!sd->_iBuffer)
  {
    delete sd;
    return NULL;
  }

  // Add shape into the new buffer
  _statIBuffer.Add(sd);

  if (sd->AddShape(src,iIndex,indicesTotal,dynamic,nInstances))
  {
    return sd;
  }
  return NULL;
}

IndexStaticDataT *EngineDDT::AddShapeIndicesSprite(const Shape &src, IndexAllocT *&iIndex, bool dynamic, int nInstances)
{
  iIndex = NULL;

  // Maximum number of indices
  const int maxIndices = 40*1024;

  // Get the number of indices
  int indicesTotal;
  indicesTotal = 6 * nInstances;

  // It's a nonsense to have zero indices
  Assert(indicesTotal > 0);

  // Number of items in the new index buffer
  int bufferIndexCount;

  // If separate, then create separated buffer
  if (indicesTotal > maxIndices)
  {
    bufferIndexCount = indicesTotal;
  }
  else
  {
    // Check if we will fit into some existing buffer
    for (int i=0; i<_statIBuffer.Size(); i++)
    {
      IndexStaticDataT *sd = _statIBuffer[i];
      if (sd->IsBusy()) continue;
      if (sd->AddShapeSprite(src,iIndex,indicesTotal,dynamic,nInstances))
      {
        return sd;
      }
    }
    bufferIndexCount = maxIndices;
  }

  // Either we didn't fit into existing or we use a separate buffer, so create a new buffer
  IndexStaticDataT *sd = new IndexStaticDataT;
  sd->Init(this, bufferIndexCount, _d3DDevice);

  // If we failed to init index buffer for the static data, then free static data and return error
  if (!sd->_iBuffer)
  {
    delete sd;
    return NULL;
  }

  // Add shape into the new buffer
  _statIBuffer.Add(sd);

  if (sd->AddShapeSprite(src,iIndex,indicesTotal,dynamic,nInstances))
  {
    return sd;
  }
  return NULL;
}

///////////////////}}

VertexDynamicDataT::VertexDynamicDataT()
{
  _actualVBIndex = 0;
  _vBufferSize = 0;
  _vBufferUsed = 0;
}

void VertexDynamicDataT::Init(int size, ID3D10Device *dev)
{
  // Calculate size of the buffer
  int byteSize = size*sizeof(SVertexT);

  // Get the buffer description
  D3D10_BUFFER_DESC bd;
  bd.Usage = D3D10_USAGE_DYNAMIC;
  bd.ByteWidth = byteSize;
  bd.BindFlags = D3D10_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;
  bd.MiscFlags = 0;

  // Create all the buffers
  for (int i = 0; i < NumDynamicVB3D; i++)
  {
  RetryV:
    HRESULT err = dev->CreateBuffer(&bd, NULL, _vBuffer[i].Init());    
    if (err != D3D_OK)
    {
      if (err == E_OUTOFMEMORY)
      {
        GEngineDD->DDError9("CreateVertexBuffer - dynamic",err);
        if (GEngineDD->TextBankDD()->ForcedReserveMemory(byteSize))
        {
          goto RetryV;
        }
      }
      GEngineDD->DDError9("CreateVertexBuffer",err);
      return;
    }
  }

  _actualVBIndex = 0;
  _vBufferSize = size;
  _vBufferUsed = 0;
}
void VertexDynamicDataT::Dealloc()
{
  for (int i=0; i<NumDynamicVB3D; i++)
  {
    _vBuffer[i].Free();
  }
}

int VertexDynamicDataT::AddVertices(const VertexTableAnimationContext *animContext, const Shape &src)
{
  const int nVertex = src.NVertex();
  SVertexT *sData;
  int vOffset = Lock(sData, nVertex);
  if (vOffset >= 0)
  {
    ShapeGeometryLock<> lock(&src);

    // Calculate the vertex buffer maximum and minimum UV's for first or for both stages
    // Note that even first stage computing is sometimes useless (shadow volume)
    UVPair minUV[MAX_UV_SETS], maxUV[MAX_UV_SETS];
    src.CalculateMaxAndMinUV(maxUV[0], minUV[0]);
    if (src.NUVStages() > 1) src.CalculateMaxAndMinUV(1, maxUV[1], minUV[1]);

    // Calculate the vertex buffer UV's scale and offset and set it to the engine
    UVPair scale[MAX_UV_SETS];
    for (int i = 0; i < MAX_UV_SETS; i++)
    {
      scale[i].u = maxUV[i].u - minUV[i].u;
      scale[i].v = maxUV[i].v - minUV[i].v;
      saturateMax(scale[i].u, 1e-6f);
      saturateMax(scale[i].v, 1e-6f);
      Assert(maxUV[i].u != -1e6);
      Assert(maxUV[i].v != -1e6);
      Assert(minUV[i].u != 1e6);
      Assert(minUV[i].v != 1e6);
    }
    GEngineDD->SetTexGenScaleAndOffset(scale, minUV);

    UVPair invUV = CalcInvScale(maxUV[0], minUV[0]);

    const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
    const AutoArray<Vector3> *pos = &src.GetPosArray();
    const CondensedAutoArray<Vector3> *norm = &src.GetNormArray();
    for (int i = 0; i < nVertex; i++)
    {
      sData->pos = (*pos)[i];
      CopyNormal(sData->norm,(*norm)[i]);
      CopyUV(sData->t0, (*uv)[i], maxUV[0], minUV[0], invUV);
      sData++;
    }

    Unlock();
  }
  return vOffset;
}

int VertexDynamicDataT::Lock(SVertexT *&vertex, int n)
{
  D3D10_MAP flags;
  if (_vBufferUsed + n >= _vBufferSize)
  {
    // discard old - start a new buffer
#if CUSTOM_VB_DISCARD
    if (++_actualVBIndex >= NumDynamicVB3D)
    {
      _actualVBIndex = 0;
    }
#endif
    flags = D3D10_MAP_WRITE_DISCARD;
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
    flags = D3D10_MAP_WRITE_NO_OVERWRITE;
  }

  //int size = sizeof(SVertexT) * n;
  int offset = sizeof(SVertexT) * _vBufferUsed;
Retry:
  void *data = NULL;
  HRESULT err;
  {
    PROFILE_DX_SCOPE(3ddvL);

    // Map the vertex buffer
    err = GEngineDD->Map(_vBuffer[_actualVBIndex], flags, 0, &data);

    // Move the data pointer according to the offset
    data = &((BYTE*)data)[offset];
  }
  if (err!=D3D_OK || !data)
  {
    if (FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto Retry;
    ErrF("VB Lock failed, %s",DXGetErrorString8(err));
    return -1;
  }
  int ret = _vBufferUsed;
  _vBufferUsed += n;

  vertex = (SVertexT *)data;

  return ret;
}

void VertexDynamicDataT::Unlock()
{
  _vBuffer[_actualVBIndex]->Unmap();
}

VertexBufferD3DT::VertexBufferD3DT()
{
  _iOffset = 0;
  _vOffset = 0; // offsetting done by D3D

  for (int i = 0; i < MAX_UV_SETS; i++)
  {
    _maxUV[i].u = _maxUV[i].v = -1e6;
    _minUV[i].u = _minUV[i].v = 1e6;
  }

  _iAlloc = NULL;
  _vAlloc = NULL;
}

VertexBufferD3DT::~VertexBufferD3DT()
{
  if (IsInList())
  {
    GEngineDD->BufferReleased(this);
  }
  if (_sharedV)
  {
    _sharedV->RemoveShape(_vAlloc);
    if (_sharedV->IsEmpty())
    {
      GEngineDD->DeleteVB(_sharedV);
    }
  }
  if (_sharedI)
  {
    _sharedI->RemoveShape(_iAlloc);
    if (_sharedI->IsEmpty())
    {
      GEngineDD->DeleteIB(_sharedI);
    }
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
  /*
  if (_iBuffer)
  {
    //LogF("Destroy IB %x",_iBuffer);
  }
  */
  if (_vBuffer)
  {
    GEngineDD->AddVBuffersAllocated(-_vBufferSize*sizeof(SVertexT),false);
    _vBuffer.Free();
  }

  if (_iBuffer)
  {
    GEngineDD->AddIBuffersAllocated(-_iBufferSize*sizeof(VertexIndex));
    _iBuffer.Free();
  }
}

MemSizeVRAMSys VertexBufferD3DT::GetMemoryControlled() const
{
  size_t vram = 0;
  size_t sys = 0;
#if USE_PUSHBUFFERS
  sys += _pbPool.GetBufferSize();
#endif
  if (_vBuffer)
  {
    vram += _vBufferSize*sizeof(SVertexT);
  }
  if (_iBuffer)
  {
    // we assume index buffers are held in system memory, but we really do not know
    // some HW supports HW index buffers
    sys += _iBufferSize*sizeof(VertexIndex);
  }
  if (_sharedV)
  {
    vram += _vAlloc->Size()*_sharedV->VertexSize();
  }
  if (_sharedI)
  {
    sys += _iAlloc->Size()*sizeof(VertexIndex);
  }
  Assert(vram<10*1024*1024);
  Assert(sys<10*1024*1024);
  return MemSizeVRAMSys(vram,sys);
}

size_t VertexBufferD3DT::GetBufferSize() const
{
  return GetMemoryControlled().GetSysSize();
}

void EngineDDT::BufferReleased(VertexBufferD3DT *buf)
{
  _vBufferLRU.Delete(buf);
}

void EngineDDT::OnSizeChanged(VertexBufferD3DT *buf)
{
  _vBufferLRU.RefreshMemoryControlled(buf);
}

VertexBuffer *EngineDDT::CreateVertexBuffer(const Shape &src, VBType type, bool frequent)
{
  //ThrottleVRAMAllocation(_textBank->GetMaxVRAMAllocation(),_textBank->GetVRamTexAllocation());
  
  VertexBufferD3DT *buffer = new VertexBufferD3DT;
  if (buffer->Init(this, src, type, frequent))
  {
    // insert it into the LRU cache
    // as it is only created, and not used, we might insert it at the very end ???
    Assert(!buffer->IsInList());
    _vBufferLRU.Add(buffer);
    buffer->SetOwner(&src);
    return buffer;
  }
  else
  {
    delete buffer;
    return NULL;
  }
}

int EngineDDT::CompareBuffers(const Shape &s1, const Shape &s2)
{
  // check if there are some vertex buffers
  const VertexBufferD3DT *b1 = static_cast<const VertexBufferD3DT *>(s1.GetVertexBuffer());
  const VertexBufferD3DT *b2 = static_cast<const VertexBufferD3DT *>(s2.GetVertexBuffer());
  int dd;
  if (!b1 && !b2) return 0;
  if (!b1) return -1;
  if (!b2) return +1;
  dd = b1->_separate && b2->_separate;
  if (dd) return dd;
  dd = b1->_dynamic-b2->_dynamic;
  if (dd) return dd;
  dd = (char *)b1->_sharedV.GetTypeRef()-(char *)b2->_sharedV.GetTypeRef();
  if (dd) return dd;
  dd = (char *)b1->_sharedI.GetTypeRef()-(char *)b2->_sharedI.GetTypeRef();
  return dd;
}

void VertexBufferD3DT::AllocateIndices(EngineDDT *engine, int indices)
{
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   _iBufferSize = 0;
//   Assert (!_iBuffer);
//   _iBuffer.Free();
//   ID3D10Device *device = engine->GetDirect3DDevice();
// 
//   if (indices>0)
//   {
// 
//     int byteSize = indices*sizeof(VertexIndex);
//     RetryI:
//     EngineDDT::CheckMemTypeScope scope(engine,"IBstatic",byteSize);
//     HRESULT err = device->CreateIndexBuffer
//     (
//       byteSize,WRITEONLYIB,D3DFMT_INDEX16,D3DPOOL_DEFAULT,
//       _iBuffer.Init(),
//       NULL
//     );
// 
//     if (err!=D3D_OK)
//     {
//       if (err==D3DERR_OUTOFVIDEOMEMORY)
//       {
//         LogF("Out of VRAM during i-buffer creation");
//         if (GEngineDD->TextBankDD()->ForcedReserveMemory(byteSize))
//         {
//           goto RetryI;
//         }
//       }
//       if (err==E_OUTOFMEMORY)
//       {
//         size_t freed = FreeOnDemandSystemMemoryLowLevel(byteSize);
//         if (freed!=0) goto RetryI;
//       }
//       ReportMemoryStatus(1);
//       engine->DDError9("CreateIndexBuffer",err);
//       ErrorMessage("CreateIndexBuffer failed (%x)",err);
//       return;
//     }
//     //LogF("Created IB %x",_iBuffer);
//     engine->AddIBuffersAllocated(byteSize);
// 
//     DoAssert(_iBuffer);
//   }
//   _iBufferSize = indices;
}

void VertexBufferD3DT::AllocateVertices(EngineDDT *engine, int vertices)
{
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   //_vBufferSize = 0;
// 
//   //Assert (!_vBuffer);
// 
//   //_vBuffer.Free();
// 
//   ID3D10Device *device = engine->GetDirect3DDevice();
// 
// 
//   int byteSize = vertices*sizeof(SVertexT);
//   RetryV:
//   // only static data are stored in static buffer
//   HRESULT err=device->CreateVertexBuffer
//   (
//     byteSize,WRITEONLYVB,/*MYSFVF*/0,D3DPOOL_DEFAULT,
//     _vBuffer.Init(),
//     NULL
//   );
// 
//   if (err!=D3D_OK)
//   {
//     if (err==D3DERR_OUTOFVIDEOMEMORY)
//     {
//       engine->DDError9("CreateVertexBuffer - static",err);
//       if (engine->TextBankDD()->ForcedReserveMemory(byteSize))
//       {
//         goto RetryV;
//       }
//     }
//     if (err==E_OUTOFMEMORY)
//     {
//       size_t freed = FreeOnDemandSystemMemoryLowLevel(byteSize);
//       if (freed!=0) goto RetryV;
//     }
//     ReportMemoryStatus(1);
//     engine->DDError9("CreateVertexBuffer",err);
//     return;
//     // 
//   }
// 
//   engine->AddVBuffersAllocated(byteSize,false);
// 
//   #if 0
//   LogF("Created VB %x (%x)",_vBuffer,this);
//   char name[256];
//   sprintf(name,"VB %x (%x)",_vBuffer,this);
//   vbStats.Count(name,+1);
//   #endif
//   _vBufferSize = vertices;
// 
}

bool VertexBufferD3DT::Init(EngineDDT *engine, const Shape &src, VBType type, bool frequent)
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
  ShapeGeometryLock<> lock(&src);
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
      _separate = false;
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
      _separate = false;
#endif
      break;
      //_dynamic = true;
      //_separate = false;
      //break;
    case VBStaticReused:
      // used for water and other buffers which are used multiple times
      frequent = true;
      _dynamic = false;
      _separate = false;
      break;
    case VBBigDiscardable:
      _dynamic = false;
      _separate = false;
      break;
    case VBSmallDiscardable:
      _dynamic = false;
      _separate = false;
      break;
    case VBNone: case VBNotOptimized:
      Fail("Cannot generate vertex buffer of given type");
    case VBDynamic:
      _dynamic = true;
      _separate = false;
      break;
    case VBDynamicDiscardable:
      _dynamic = true;
      _separate = true;
      break;
  }
  _iOffset = 0;
  _vOffset = 0;
  _vBufferSize = 0;

  if (_dynamic && (src.NVertex() > MaxShapeVertices))
  {
    LogF("Error: Number of vertices too large for the dynamic vertex buffer.");
    return false;
  }

  if (!src.CanBeInstanced())
  {
    nInstances = 1;
  }

  int vOffset = 0;
  if (!_separate)
  {
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
      DoAssert(CFragmentTransformSkinnedInstanced::GetMaxInstances(largestInterval) == CFragmentShadowVolumeSkinnedInstancedT::GetMaxInstances(largestInterval));
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
          _sharedV = engine->AddShapeVertices<VD_Position_Normal_Tex0_WeightsAndIndices>(src, _vAlloc, _dynamic, false, frequent);
        }
        else
        {
          _sharedV = engine->AddShapeVerticesSkinnedInstanced(src, _vAlloc, _dynamic, false, nInstances);
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
          _sharedV = engine->AddShapeVerticesNormalSkinnedInstanced(src, _vAlloc, _dynamic, false, nInstances, frequent);
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
//      case VD_Position_Tex0_Color:
//        nInstances = 1;
//        _sharedV = GEngineDD->AddShapeVerticesPoint(src, *this, _dynamic, type == VBBigDiscardable);
//        break;
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
      // mark fall-back
      Fail("Vertex buffer not allocated");
      _separate = true;
    }
    else
    {
      // Finish in case of zero sections
      if (src.NSections() == 0)
      {
        return true;
      }

      vOffset = _vAlloc ? _vAlloc->Memory() : 0;
      if (src.GetVertexDeclaration() == VD_Tex0)
      {
        _sharedI = GEngineDD->AddShapeIndicesSprite(src, _iAlloc, _dynamic, nInstances);
      }
      else
      {
        _sharedI = GEngineDD->AddShapeIndices(src, _iAlloc, _dynamic, nInstances);
      }
      if (!_sharedI || !_iAlloc)
      {
        LogF("Shared index buffer not allocated");
        _separate = true;
        if (_sharedV)
        {
          _sharedV->RemoveShape(_vAlloc);
          _sharedV = NULL;
          _vAlloc = NULL;
        }
      }
      else
      {
        _iOffset = _iAlloc->Memory();
        _vOffset = vOffset;
      }
    }
  }
  if ( (_sharedV || _dynamic) && _sharedI)
  {

    // scan sections
    _sections.Realloc(src.NSections() * nInstances);
    _sections.Resize(src.NSections() * nInstances);

    // Array to remember start index of the section
    AutoArray<int,MemAllocStack<int,64> > sectionStart;
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
        start += size;
      }
    }
  }
  else // !_shared
  {
    // nor vertices not indices shared - sharing not desired or there is no space for sharing
    if (!_dynamic)
    {
      AllocateVertices(engine,src.NVertex());
      if (!_vBuffer)
      {
        // vertex buffer allocation failed - 
        return false;
      }
      CopyData(src);
    }

    DoAssert(nInstances==1); // instancing not supported here
    Fail("DX10 TODO");
    // Direct3D 10 NEEDS UPDATE 
//    // calculate how many triangle we will need to render this buffer
//     int indices = IndexStaticDataT::IndicesNeededTotal(src, 1);
//     if (indices>0)
//     {
//       AllocateIndices(engine,indices);
// 
//       PROFILE_DX_SCOPE(3ddiC);
// Retry:
//       void *data = NULL;
//       HRESULT err;
//       {
//         PROFILE_DX_SCOPE(3ddiL);
//         err = _iBuffer->Lock(0,indices*sizeof(VertexIndex),&data,NoSysLockFlag);
//       }
//       if (err!=D3D_OK || !data)
//       {
//         if (FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto Retry;
//         ErrF("IB Lock failed, %s",DXGetErrorString8(err));
//         return false;
//       }
// 
//       VertexIndex *iData = (VertexIndex *)data;
//       for (Offset o=src.BeginFaces(); o<src.EndFaces(); src.NextFace(o))
//       {
//         const Poly &poly = src.Face(o);
//         if (poly.N() < 3) continue;
//         if ((poly.N() == 3) &&
//           ((poly.GetVertex(0) == poly.GetVertex(1)) || 
//           (poly.GetVertex(1) == poly.GetVertex(2)) ||
//           (poly.GetVertex(2) == poly.GetVertex(0)))) continue;
//         for (int i=2; i<poly.N(); i++)
//         {
//           *iData++ = poly.GetVertex(0);
//           *iData++ = poly.GetVertex(i-1);
//           *iData++ = poly.GetVertex(i);
//         }
//       }
//       DoAssert(iData == &((VertexIndex *)data)[indices]);
//       _iBuffer->Unlock();
// 
//       // scan sections
//       _sections.Realloc(src.NSections());
//       _sections.Resize(src.NSections());
//       Offset covered = Offset(0);
//       int start = 0;
//       for (int i=0; i<src.NSections(); i++)
//       {
//         const ShapeSection &sec = src.GetSection(i);
//         if (sec.beg==Offset(0))
//         {
//           start = 0;
//           covered = Offset(0);
//         }
//         
//         // the rewind above handles only one special case - full rewind
//         // partial going back might exists as well - we will catch them
//         Assert(sec.beg>=covered);
//         covered = sec.end;
// 
//         // calculate how much indices is used for this section
//         int size = 0;
//         int minV = INT_MAX;
//         int maxV = 0;
//         for (Offset o=sec.beg; o<sec.end; src.NextFace(o))
//         {
//           const Poly &face = src.Face(o);
//           if (face.N() < 3) continue;
//           if ((face.N() == 3) &&
//             ((face.GetVertex(0) == face.GetVertex(1)) || 
//             (face.GetVertex(1) == face.GetVertex(2)) ||
//             (face.GetVertex(2) == face.GetVertex(0)))) continue;
//           size += (face.N()-2)*3;
//           for (int vv=0; vv<face.N(); vv++)
//           {
//             int vi = face.GetVertex(vv);
//             saturateMin(minV,vi);
//             saturateMax(maxV,vi);
//           }
//         }
//         // record section info
//         _sections[i].beg = start;
//         _sections[i].end = start+size;
//         _sections[i].begVertex = minV;
//         _sections[i].endVertex = maxV+1;
//         start += size;
//       }
//     }
    _iOffset = 0;
    _vOffset = 0;
    // vertex offset is actually determined dynamically
    // after vertices are placed to dynamic vertex buffer
  }

  // index buffer remains constant - animation is done on vertices
  // or whole sections
  return true;
  
}

void VertexBufferD3DT::CopyData( const Shape &src )
{
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   Assert (_separate);
//   Assert (!_dynamic);
//   if (_vBufferSize<=0) return;
//   // copy source data from the mesh
//   //DWORD size;
// 
//   DWORD flags = NoSysLockFlag;
// 
//   PROFILE_DX_SCOPE(3dvbD);
// 
//   Retry:
//   void *data = NULL;
//   HRESULT err = _vBuffer->Lock(0,_vBufferSize*sizeof(SVertexT),&data,flags);
// 
//   if (err!=D3D_OK || !data)
//   {
//     if (FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto Retry;
//     ErrF("VB Lock failed, %s",DXGetErrorString8(err));
//     return;
//   }
// 
//   // Calculate the vertex buffer maximum and minimum UV's
//   src.CalculateMaxAndMinUV(_maxUV[0], _minUV[0]);
// 
//   SVertexT *sData = (SVertexT *)data;
//   const CondensedAutoArray<UVPair> *uv = &src.GetUVArray();
//   const AutoArray<Vector3> *pos = &src.GetPosArray();
//   const CondensedAutoArray<Vector3> *norm = &src.GetNormArray();
// 
//   UVPair invUV = CalcInvScale(_maxUV[0], _minUV[0]);
// 
//   int nVertex = src.NVertex();
//   for (int i=0; i<nVertex; i++)
//   {
//     sData->pos = (*pos)[i];
//     CopyNormal(sData->norm,(*norm)[i]);
//     CopyUV(sData->t0, (*uv)[i], _maxUV[0], _minUV[0], invUV);
//     sData++;
//   }
//   _vBuffer->Unlock();
// 
}


void VertexBufferD3DT::Update(const VertexTableAnimationContext *animContext, const Shape &src, const EngineShapeProperties &prop, VSDVersion vsdVersion)
{
  if (!_dynamic)
  {
    // use static buffer as source
    GEngineDD->SetVSourceStatic(src, prop, *this, vsdVersion);
  }
  else
  {
    // update dynamic buffer - and use it as source
    GEngineDD->SetVSourceDynamic(animContext, src, prop, *this);
  }

  // note: no need to set indices / vertices when drawing same shape again
  // this can be quite common

}

void VertexBufferD3DT::Detach(const Shape &src)
{
  DoAssert(GetOwner()==NULL || &src==GetOwner());
  SetOwner(NULL);
}

void VertexBufferD3DT::RemoveIndexBuffer()
{
  if (_sharedI)
  {
    _sharedI->RemoveShape(_iAlloc);
    if (_sharedI->IsEmpty())
    {
      GEngineDD->DeleteIB(_sharedI);
    }
    _sharedI = NULL;
  }
  if (_iBuffer)
  {
    GEngineDD->AddIBuffersAllocated(-_iBufferSize*sizeof(VertexIndex));
    _iBuffer.Free();
  }
}

bool VertexBufferD3DT::IsGeometryLoaded() const
{
  // dynamic buffers do not contain any geometry information
  return !_dynamic;
}

void VertexBufferD3DT::OnSizeChanged()
{
  GEngineDD->OnSizeChanged(this);
}

int EngineDDT::ShapePropertiesNeeded(const TexMaterial *mat) const
{
  switch (mat->GetVertexShaderID(0))
  {
    case VSNormalMapThrough:
    case VSNormalMapSpecularThrough:
    case VSNormalMapThroughNoFade:
    case VSNormalMapSpecularThroughNoFade:
      return ESPTreeCrown;
  }
  return 0;
}

void EngineDDT::SetTexGenScaleAndOffset(const UVPair *scale, const UVPair *offset)
{
  for (int i = 0; i < MAX_UV_SETS; i++)
  {
    if (_texGenOffset[i]!=offset[i] || _texGenScale[i]!=scale[i])
    {
      Assert(scale[i].u>0);
      Assert(scale[i].v>0);
      _texGenScale[i] = scale[i];
      _texGenOffset[i] = offset[i];
      _texGenScaleOrOffsetHasChanged = true;
    }
  }
}

void EngineDDT::SetVSourceStatic(const Shape &src, const EngineShapeProperties &prop, const VertexBufferD3DT &buf, VSDVersion vsdVersion)
{
  PROFILE_DX_SCOPE_DETAIL(3dvbS);
  // Separate part

  // if shape requires any special properties, set them
  if (src.GetShapePropertiesNeeded()&ESPTreeCrown)
  {
    prop.GetTreeCrown(_treeCrown);
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
  SetTexGenScaleAndOffset(scale, buf._minUV);

  if (!buf._sharedV || !buf._sharedI)
  {
    SetStreamSource(buf._vBuffer, sizeof(SVertexT), buf._vBufferSize);
    //_vertexDecl = _vertexDeclD[VD_Position_Normal_Tex0][VSDV_Basic];
    int vOffset = 0;
    if (_iBufferLast!=buf._iBuffer || _vOffsetLast!=vOffset)
    {
      ADD_COUNTER(d3xIB,1);
      _d3DDevice->IASetIndexBuffer(buf._iBuffer, DXGI_FORMAT_R16_UINT, 0);
      _iBufferLast = buf._iBuffer;
      _vOffsetLast = vOffset;

      #if DO_TEX_STATS
      if (LogStatesOnce)
      {
        LogF("SetIBSourceStatic separate");
      }
      #endif
    }
    _iOffset = buf._iOffset;
  }
  // Shared part
  else
  {
    VertexStaticDataT *dtaV = buf._sharedV;
    IndexStaticDataT *dtaI = buf._sharedI;
    if (dtaV)
    {
      SetStreamSource(dtaV->_vBuffer, dtaV->VertexSize(), dtaV->Size());
      //_vertexDecl = _vertexDeclD[src.GetVertexDeclaration()][vsdVersion];

      #if DO_TEX_STATS
      if (LogStatesOnce)
      {
        LogF("SetVBSourceStatic shared, vertex size %d",dtaV->VertexSize());
      }
      #endif
      
      #if 0 //_ENABLE_REPORT
      if (dtaV->_busy.WasNeverUsed())
      {
        if (buf._vAlloc && dtaV->_manager.Size()>buf._vAlloc->Size())
        {
          LogF("Shared resource %p marked busy: %d",dtaV,Glob.frameID);
        }
        else
        {
          LogF("Resource %p marked busy: %d",dtaV,Glob.frameID);
        }
      }
      #endif

      dtaV->Used();
    }
    if (dtaI)
    {
      dtaI->Used();
    }
    #if V_OFFSET_CUSTOM
    int vOffset = buf._vOffset;
    #else
    int vOffset = 0;
    #endif

    if (dtaI && (_iBufferLast!=dtaI->_iBuffer || _vOffsetLast!=vOffset))
    {
      ADD_COUNTER(d3xIB,1);
      _d3DDevice->IASetIndexBuffer(dtaI->_iBuffer, DXGI_FORMAT_R16_UINT, 0);
      _iBufferLast = dtaI->_iBuffer;
      _vOffsetLast = vOffset;
      #if DO_TEX_STATS
      if (LogStatesOnce)
      {
        LogF("SetIBSourceStatic shared");
      }
      #endif
    }
    _iOffset = buf._iOffset;
  }
}

void EngineDDT::SetVSourceDynamic(
  const VertexTableAnimationContext *animContext, const Shape &src, const EngineShapeProperties &prop, const VertexBufferD3DT &buf
)
{
  PROFILE_DX_SCOPE_DETAIL(3dvbD);

  // Zero vertex and index buffer in the device, forget _iBufferLast and _vBufferLast.
  ForgetDeviceBuffers();

  int vOffset = _dynVBuffer.AddVertices(animContext, src);
  const ComRef<IDirect3DVertexBuffer9Old> &dbuf = _dynVBuffer.GetVBuffer();
  SetStreamSource(dbuf, sizeof(SVertexT), _dynVBuffer._vBufferSize);

  if (buf._separate)
  {
    
    if (_iBufferLast!=buf._iBuffer || vOffset!=_vOffsetLast)
    {

      #if DO_TEX_STATS
      if (LogStatesOnce)
      {
        LogF("SetIBSourceDynamic separate");
      }
      #endif

      ADD_COUNTER(d3xIB,1);
      _d3DDevice->IASetIndexBuffer(buf._iBuffer, DXGI_FORMAT_R16_UINT, 0);
      _iBufferLast = buf._iBuffer;
      _vOffsetLast = vOffset;
    }
    _iOffset = buf._iOffset;
    //_vOffset = buf._vOffset;
  }
  else
  {
    IndexStaticDataT *dtaI = buf._sharedI;
    if (dtaI && (_iBufferLast!=dtaI->_iBuffer || vOffset!=_vOffsetLast))
    {
      #if DO_TEX_STATS
      if (LogStatesOnce)
      {
        LogF("SetIBSourceDynamic shared");
      }
      #endif

      ADD_COUNTER(d3xIB,1);
      _d3DDevice->IASetIndexBuffer(dtaI->_iBuffer, DXGI_FORMAT_R16_UINT, 0);
      _iBufferLast = dtaI->_iBuffer;
      _vOffsetLast = vOffset;
    }
    _iOffset = buf._iOffset;
    //_vOffset = buf._vOffset;
  }
}
/*
#define SET_COLOR(d,s) d.r = s.R(),d.g = s.G(),d.b = s.B(),d.a = s.A()
#define SET_WHITE(d,av) d.r = d.g = d.b = 1, d.a = av
#define SET_BLACK(d,av) d.r = d.g = d.b = 0, d.a = av
#define SET_POS(d,s) d.x = s.X(),d.y = s.Y(),d.z = s.Z()
*/

float EngineDDT::GetLastKnownAvgIntensity() const
{
  return _lastKnownAvgIntensity;
}
void EngineDDT::ForgetAvgIntensity()
{
  _lastKnownAvgIntensity = -1;
  // force update
  _accomFactorPrev = -1;

  // Reset old queries
  _ppGlow->CancelQueries();
}

const int SmoothFrames = 3;

float EngineDDT::GetSmoothFrameDuration() const
{
  // average for last three frames is a good basis for simulation timing
  // using last frame time can be bad, because:
  //   - D3D uses 3 frames command buffer with periodic flush
  //   - there can be some other one frame spikes
  return GetAvgFrameDuration(SmoothFrames)*0.001f;
}
float EngineDDT::GetSmoothFrameDurationMax() const
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

bool EngineDDT::IsWBuffer() const
{
  return _useWBuffer;
}

bool EngineDDT::CanWBuffer() const
{
  return _canWBuffer && (_depthBpp<=16 || _canWBuffer32);
}

bool EngineDDT::SetWBufferWanted(bool val)
{
  bool change = false;
  if (_userEnabledWBuffer != val) change  = true;
  _userEnabledWBuffer = val;
  if (!CanWBuffer()) return false;
  return change;
}
void EngineDDT::SetWBuffer(bool val)
{
  if (SetWBufferWanted(val))
  {
    Reset();
  }
}

void EngineDDT::SetupLights(const LightList &lights, int spec)
{
  // no scene - no lights
  if (!GScene) return;

  // Main light handling
  {
    // TODO: lights persistent
    // disable all other lights
    // setup sun
    LightSun *sun = GScene->MainLight();

    // Remember main light parameters
    _lightAmbient = sun->GetAmbient() * GetAccomodateEye();
    _lightDiffuse = sun->GetDiffuse() * GetAccomodateEye();
    _lightSpecular = sun->GetDiffuse() * GetAccomodateEye();
    _direction = sun->LightDirection();
    _shadowDirection = sun->ShadowDirection();
  }

  // P&S light handling
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
    saturateMin(nLights, _maxLights-1);

    // Fill out the lights we want to use on the object,
    // and calculate number of point and spot lights
    _lights.Resize(nLights);
    _pointLightsCount = 0;
    _spotLightsCount = 0;
    for (int i=0; i<nLights; i++)
    {
      // Remember the light
      const Light *light = lights[i];
      _night = night;
      _lights[i] = const_cast<Light *>(light);

      // Update number of P&S lights
      LightDescription ldesc;
      light->GetDescription(ldesc, GetAccomodateEye());
      if (ldesc.type == LTPoint) _pointLightsCount++;
      if (ldesc.type == LTSpotLight) _spotLightsCount++;
    }
  }
}

/*!
\patch 1.01 Date 6/18/2001 by Ondra
- Fixed: T&L lights in daytime
*/

void EngineDDT::SetupMaterialLights(const TLMaterial &mat, bool vsIsAS)
{
  // no scene - no lights
  if (!GScene) return;

  // Get the scene main light
  LightSun *sun = GScene->MainLight();

  // Remember the material parameters
  _matAmbient = mat.ambient;
  _matDiffuse = mat.diffuse;
  _matForcedDiffuse = mat.forcedDiffuse;
  _matSpecular = mat.specular;
  _matEmmisive = mat.emmisive;
  _matPower = mat.specularPower;

  // Set the shader constants according to mainlight
  if (_tlActive!=TLDisabled)
  {
    switch(_mainLight)
    {
    case ML_Sun:
      {
        SetVertexShaderConstantMLSunR(_matAmbient, _matDiffuse, _matForcedDiffuse, _matSpecular, _matEmmisive, vsIsAS);
      }
      break;

    case ML_Sky:
      {
        Color accom=GetAccomodateEye();
        Color sky,aroundSun;
        _sceneProps->GetSkyColor(sky,aroundSun);
        Color sunSkyColor = aroundSun * accom * _modColor;
        SetVertexShaderConstantF(VSC_LDirectionSunSkyColor, (float*)&sunSkyColor, 1);

        Color skyColor = sky * accom * _modColor;
        SetVertexShaderConstantF(VSC_LDirectionSkyColor, (float*)&skyColor, 1);
        //skyColor.SaturateMinMax();

        // we always want to consider sun here, never the moon
        Vector3 tDirection = TransformAndNormalizeVectorToObjectSpace(sun->SunDirection());
        D3DXVECTOR4 direction(tDirection.X(), tDirection.Y(), tDirection.Z(), 1.0f);
        SetVertexShaderConstantF(VSC_LDirectionTransformedDirSky, (float*)&direction, 1);

        if (vsIsAS)
        {
          Color zero = Color(0, 0, 0, 0);
          SetVertexShaderConstantF(VSC_AE, (float*)&zero, 1);
          SetVertexShaderConstantF(VSC_DForced, (float*)&zero, 1);
        }
        else
        {
          Color newA_E = _matEmmisive*GetAccomodateEye();
          Color newDForced = HBlack;
          SetVertexShaderConstantF(VSC_AE, (float*)&newA_E, 1);
          SetVertexShaderConstantF(VSC_DForced, (float*)&newDForced, 1);
        }
      }
      break;

    case ML_SunObject:
      {
        Color accom = GetAccomodateEye();
        Color color = sun->SunObjectColor() * accom * _modColor;
        color.SetA(color.A() * mat.diffuse.A());
        SetVertexShaderConstantF(VSC_LDirectionSetColorColor, (float*)&color, 1);

        if (vsIsAS)
        {
          Color zero = Color(0, 0, 0, 0);
          SetVertexShaderConstantF(VSC_AE, (float*)&zero, 1);
          SetVertexShaderConstantF(VSC_DForced, (float*)&zero, 1);
        }
        else
        {
          Color newA_E = _matEmmisive*GetAccomodateEye();
          Color newDForced = HBlack;
          SetVertexShaderConstantF(VSC_AE, (float*)&newA_E, 1);
          SetVertexShaderConstantF(VSC_DForced, (float*)&newDForced, 1);
        }
      }
      break;

    case ML_SunHaloObject:
      {
        Color accom = GetAccomodateEye();
        Color color = sun->SunHaloObjectColor() * accom * _modColor;
        color.SetA(color.A() * mat.diffuse.A());
        SetVertexShaderConstantF(VSC_LDirectionSetColorColor, (float*)&color, 1);

        if (vsIsAS)
        {
          Color zero = Color(0, 0, 0, 0);
          SetVertexShaderConstantF(VSC_AE, (float*)&zero, 1);
          SetVertexShaderConstantF(VSC_DForced, (float*)&zero, 1);
        }
        else
        {
          Color newA_E = _matEmmisive*GetAccomodateEye();
          Color newDForced = HBlack;
          SetVertexShaderConstantF(VSC_AE, (float*)&newA_E, 1);
          SetVertexShaderConstantF(VSC_DForced, (float*)&newDForced, 1);
        }
      }
      break;

    case ML_MoonObject:
      {
        Color accom = GetAccomodateEye();
        Color color = sun->MoonObjectColor() * accom * _modColor;
        color.SetA(color.A() * mat.diffuse.A());
        SetVertexShaderConstantF(VSC_LDirectionSetColorColor, (float*)&color, 1);

        if (vsIsAS)
        {
          Color zero = Color(0, 0, 0, 0);
          SetVertexShaderConstantF(VSC_AE, (float*)&zero, 1);
          SetVertexShaderConstantF(VSC_DForced, (float*)&zero, 1);
        }
        else
        {
          Color newA_E = _matEmmisive*GetAccomodateEye();
          Color newDForced = HBlack;
          SetVertexShaderConstantF(VSC_AE, (float*)&newA_E, 1);
          SetVertexShaderConstantF(VSC_DForced, (float*)&newDForced, 1);
        }
      }
      break;

    case ML_MoonHaloObject:
      {
        Color accom = GetAccomodateEye();
        Color color = sun->MoonHaloObjectColor() * accom * _modColor;
        color.SetA(color.A() * mat.diffuse.A());
        SetVertexShaderConstantF(VSC_LDirectionSetColorColor, (float*)&color, 1);

        if (vsIsAS)
        {
          Color zero = Color(0, 0, 0, 0);
          SetVertexShaderConstantF(VSC_AE, (float*)&zero, 1);
          SetVertexShaderConstantF(VSC_DForced, (float*)&zero, 1);
        }
        else
        {
          Color newA_E = _matEmmisive*GetAccomodateEye();
          Color newDForced = HBlack;
          SetVertexShaderConstantF(VSC_AE, (float*)&newA_E, 1);
          SetVertexShaderConstantF(VSC_DForced, (float*)&newDForced, 1);
        }
      }
      break;

    case ML_None:
      {
        SetVertexShaderConstantMLNoneR(_matEmmisive, vsIsAS);
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

void EngineDDT::ChangeClipPlanes()
{
  // clip planes currently supported only via pixel shaders

  /*
  // 0 is near additional clip plane
  // 1 is far additional clip plane
  Camera *cam = GScene->GetCamera();
  float cANear = cam->GetAdditionalClippingNear();
  float cAFar = cam->GetAdditionalClippingFar();
  float cNear = cam->ClipNear();
  float cFar = cam->ClipFar();

  bool enableNear = cANear>cNear;
  bool enableFar = cAFar<cFar;
  if (enableNear)
  {
    const Plane &p = cam->GetNearClipPlane();
    if (!_clipANearEnabled || PlaneDistance2(p,_clipANear)>1e-6)
    {
      _d3DDevice->SetClipPlane(0,(float *)&p);
      _clipANear = p;
    }
  }
  if (enableFar)
  {
    const Plane &p = cam->GetFarClipPlane();
    if (!_clipAFarEnabled || PlaneDistance2(p,_clipAFar)>1e-6)
    {
      _d3DDevice->SetClipPlane(1,(float *)&p);
      _clipAFar = p;
    }
  }
  if (enableNear!=_clipANearEnabled || enableFar!=_clipAFarEnabled)
  {
    DWORD planesEnabled = int(enableNear)|(int(enableFar)<<1);
    _d3DDevice->SetRenderState(D3DRS_CLIPPLANEENABLE,planesEnabled);
    _clipANearEnabled = enableNear;
    _clipAFarEnabled = enableFar;
  }
  */
}

void EngineDDT::ChangeWDepth(float matC, float matD)
{
  if (_matC==matC && _matD==matD) return;
  _matC = matC, _matD = matD;
  if (!_useWBuffer) return;


  if (_tlActive!=TLDisabled)
  {
    Fail("ChangeWDepth in not compatible with HW T&L");
  }
  else
  {

    D3DMATRIX mat;

    mat._11 = 1;
    mat._12 = 0;
    mat._13 = 0;
    mat._14 = 0;

    mat._21 = 0;
    mat._22 = 1;
    mat._23 = 0;
    mat._24 = 0;

    mat._31 = 0;
    mat._32 = 0;
    mat._33 = matC;
    mat._34 = 1;

    mat._41 = 0;
    mat._42 = 0;
    mat._43 = matD;
    mat._44 = 0;

    // we have to set projection matrix - D3D needs wNear a wFar from it

    //PROFILE_DX_SCOPE(3dSTr);
    Fail("DX10 TODO");
    // Direct3D 10 NEEDS UPDATE 
//     _d3DDevice->SetTransform( D3DTS_PROJECTION , &mat );
     LogSetProjT(mat);
  }

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
    swap(Set(0,1),Set(1,0));swap(Set(0,2),Set(2,0));swap(Set(0,3),Set(3,0));
    swap(Set(1,2),Set(2,1));swap(Set(1,3),Set(3,1));
    swap(Set(2,3),Set(3,2));
  }
};

void EngineDDT::DoSwitchTL( TLMode active )
{
  FlushAndFreeAllQueues(_queueNo);
  //FlushAndFreeAllQueues(_queueTL);
  _tlActive = active;
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
    if (_vertexDecl.NotNull())
    {
      SetVertexDeclarationR(_vertexDecl);
    }
    // -----------------------------------

    D3DSetRenderState(D3DRS_CLIPPING_OLD,TRUE);

    if (active!=TLViewspace)
    {
      // Setup view and projection matrices only in case it wasn't set by shadow buffer rendering process already
      if (_renderingMode != RMShadowBuffer)
      {
        Camera *camera = GScene->GetCamera();
        _invView = camera->Transform();
        _view = camera->InverseScaled();
      }

      // Setup projection matrix
      ProjectionChanged();

      // Set the fog color
      Color fogScaled = _fogColor * GetHDRFactor();
      SetPixelShaderConstantF(PSC_FogColor, (const float*)&fogScaled, 1);
    }
  }
  else
  {
    #if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("SetVertexShader dummy");
    }
    #endif
    PROFILE_DX_SCOPE_DETAIL(3dTL);

    // Disable shadow receiving in non-tl rendering
    SelectPixelShaderType(PSTBasicNA);

    //DoAssert(_vertexDeclNonTL.NotNull());
    SetVertexDeclarationR(_vertexDeclNonTL);
    DoAssert(_vertexShaderNonTL._shader.NotNull());
    SetVertexShaderR(_vertexShaderNonTL);
    D3DSetRenderState(D3DRS_CLIPPING_OLD,FALSE);

    // Zero vertex and index buffer in the device, forget _iBufferLast and _vBufferLast
    ForgetDeviceBuffers();

    // Make the fog purple to be able to identify it (it should not occur everywhere)
    Color fogScaled = Color(1, 0, 1);
    SetPixelShaderConstantF(PSC_FogColor, (const float*)&fogScaled, 1);
  }
}

void EngineDDT::DoSetMaterial(const TLMaterial &mat, bool vsIsAS)
{
  // set corresponding render states
  // combine material with light


  //PROFILE_DX_SCOPE(3dmat);

  if (mat.specularPower>0)
  {
    SelectPixelShaderSpecular(PSSSpecular);
  }
  else
  {
    SelectPixelShaderSpecular(PSSNormal);
  }
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
  SetupMaterialLights(mat, vsIsAS);
}

//TypeIsSimple(Light *);

/*!
\patch 5137 Date 3/2/2007 by Ondra
- Fixed: Haze was too strong with large viewdistance.
*/
void EngineDDT::ProjectionChanged()
{
  if (!_tlActive) return;

  //! If shadow buffer is being rendered, a special projection matrix was set and we don't want to rewrite it
  if (_renderingMode == RMShadowBuffer) return;

  // Set the projection matrix
  D3DMATRIX projMatrix;
  // set projection (based on camera)
  Camera *camera = GScene->GetCamera();
  // empirical, camZTransform should be around 1 for typical scenes
  float camZTransform = camera->ClipNear()*1.25f;
  float bias = _bias * camZTransform;
  _proj = camera->ProjectionNormal();
  ConvertProjectionMatrix(projMatrix,camera->ProjectionNormal(),bias);
  SetVertexShaderConstantF(VSC_ProjMatrix, (float*)&projMatrix, 4);

  
  // Set the fog parameters
  const float defExpFog = 10000.0f;
  float farFog = camera->ClipFarFog();
  //const float startFactor = InterpolativC(farFog,100,200,0,0.2f);
  const float startFactor = InterpolativC(farFog,2000,defExpFog,0.5f,0.9f);
  float nearFog  = ( startFactor*(farFog-camera->ClipNear()) ) + camera->ClipNear();
  // no need for this - exponential fog is handling this in a better way
  // avoid fog starting too far
  //if (nearFog>200) nearFog = 200;
  _fogStart = nearFog;
  _fogEnd = farFog;
  float expFar = defExpFog;
  #if 1 // smart exp. fog handling
  const float denseFogFar = 300.0f;
  const float denseFogNear = 200.0f;
  // if the fog is very dense, prefer exponential (underwater, real fog)
  // in the middle range prefer linear, not starting very soon (hiding far objects)
  // if the fog is very light, prefer exponential (realistic scattering)
  if (farFog>defExpFog) expFar = farFog;
  else if (farFog<denseFogFar)
  {
    if (farFog>denseFogNear) expFar = InterpolativC(farFog,denseFogNear,denseFogFar,denseFogNear,defExpFog);
    else expFar = farFog;
  }
  #endif
  //const float lnInv = -6.238324625f; // ln(1/512)
  static float lnInv = -3.2188758249f; // ln(1/25)
  // ln(1/512)/maxDist  ... maxDist = 10000
  _expFogCoef = lnInv/expFar;
}

void EngineDDT::EnableSunLight(EMainLight mainLight)
{
  if (mainLight == _mainLight) return;
  _mainLight = mainLight;

  if (!_tlActive) return;
  //_d3DDevice->LightEnable(0,_sunEnabled);
}

void EngineDDT::SetFogMode(EFogMode fogMode)
{
  _fogMode = fogMode;
}

bool EngineDDT::BeginMeshTLInstanced(
  const Shape &sMesh, const EngineShapeProperties &prop, int spec, const LightList &lights, int bias, float minDist2, const DrawParameters &dp
)
{
  if (ResetNeeded()) return false;
  
  // We need to use some common "model" space for all instances
  // naive approach is to use identity (common space is world space)
  // We want to avoid world space offsets, as they cause significant numerical error
  // when multiplying with "worldToView" matrix.
  // To avoid large positions we convert all matrices to be relative to camera position
  // See also other CAM_REL_SPACE occurrences
  Matrix4 camTranslate(MTranslation,_invView.Position());
  BeginInstanceTL(camTranslate, minDist2, spec, lights, dp);

  // SwitchTL(TLEnabled); Not needed - performed inside BeginInstanceTL

  ChangeClipPlanes();

  //SetBias(bias);

  //////////////////////////////////////////////////////////////////////////

  // update if necessary
  VertexBufferD3DT *vb = static_cast<VertexBufferD3DT *>(sMesh.GetVertexBuffer());

  if (vb->IsInList())
  {
    _vBufferLRU.Refresh(vb);
  }
  else
  {
    _vBufferLRU.Add(vb);
  }

  vb->SetOwner(&sMesh);
  vb->Lock();

  // Update also sets the _posScale and _posOffset values
  vb->Update(sMesh, prop, VSDV_Instancing);
  
  return true;
}

void EngineDDT::SetupInstances(const ObjectInstanceInfo *instances, int nInstances)
{
  // Do check we set a reasonable number of instances
  DoAssert(nInstances > 0);
  DoAssert(nInstances <= MaxInstances());

  // Prepare structure aligned to VS constants
  struct
  {
    MatrixFloat34 matrix;
    float alpha_X_X_shadow[4];
  } matrices[LimitInstances];
  const int vectorsNum = sizeof(matrices[0])/4/4;
  Assert(vectorsNum == 4); // Currently we use 4 vectors

  // Fill out the structure
  for (int i = 0; i < nInstances; i++)
  {
    /// see CAM_REL_SPACE 
    ConvertMatrixTransposed3Offset(matrices[i].matrix, instances[i]._origin, _invView.Position());
    matrices[i].alpha_X_X_shadow[0] = instances[i]._alpha;
    matrices[i].alpha_X_X_shadow[1] = 0;
    matrices[i].alpha_X_X_shadow[2] = 0;
    matrices[i].alpha_X_X_shadow[3] = instances[i]._shadowCoef;
  }
  SetVertexShaderConstantF(FREESPACE_START_REGISTER, (float*)matrices, nInstances * vectorsNum);
}

void EngineDDT::SetupInstancesSprite(const ObjectInstanceInfo *instances, int nInstances)
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
    /// see CAM_REL_SPACE 
    // Position and scale
    matrices[i][0][0] = instances[i]._object->Position().X()-_invView.Position().X();
    matrices[i][0][1] = instances[i]._object->Position().Y()-_invView.Position().Y();
    matrices[i][0][2] = instances[i]._object->Position().Z()-_invView.Position().Z();
    matrices[i][0][3] = instances[i]._object->Scale();
    if (!instances[i]._limitSpriteSize) matrices[i][0][3] *= -1;

    // Color
    Color color = instances[i]._object->GetConstantColor();
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
  SetVertexShaderConstantF(FREESPACE_START_REGISTER, (float*)matrices, nInstances * vectorsNum);
}

void EngineDDT::SetupInstancesShadowVolume(const ObjectInstanceInfo *instances, int nInstances)
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
    ConvertMatrixTransposed3Offset(matrices[i], instances[i]._origin, _invView.Position());
  }
  SetVertexShaderConstantF(FREESPACE_START_REGISTER, (float*)matrices, nInstances * vectorsNum);
}

void EngineDDT::DrawMeshTLSectionsInstanced(const Shape &sMesh, const SectionMaterialLODs &matLOD, const EngineShapeProperties &prop, int spec, int bias, int instancesOffset, int instancesCount)
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
#if _ENABLE_PERFLOG
    if (LogStatesOnce)
    {
      LogF("Material %d",sec.GetMaterial());
    }
#endif

    // distribute by predefined materials
    TLMaterial mat;
    CreateMaterial(mat, HWhite, sec.GetMaterial());

    // Prepare and draw section
    int materialLevel = (matLOD.Size() > 0) ? matLOD[i] : 0;
    sec.PrepareTL(mat, materialLevel, spec, prop);
    DrawSectionTL(sMesh, i, i+1, maxBias, instancesOffset, instancesCount);
  }
}

extern bool enableInstancing;

extern bool EnablePushBuffers;
extern bool ShowPushBuffers;

void EngineDDT::DrawMeshTLInstanced(
  const Shape &sMesh, const SectionMaterialLODs &matLOD, const EngineShapeProperties &prop, int spec, const LightList &lights, int bias,
  const ObjectInstanceInfo *instances, int nInstances, float minDist2, const DrawParameters &dp
)
{
  DoAssert(nInstances > 0);

  if (!enableInstancing)
  {
    base::DrawMeshTLInstanced(sMesh, matLOD, prop, spec, lights, bias, instances, nInstances, minDist2, dp);
    return;
  }

  // Go through all instances in chunks
  VertexBufferD3DT *vb = static_cast<VertexBufferD3DT *>(sMesh.GetVertexBuffer());
  if (vb == NULL)
  {
    Fail("Error: VB must not be NULL");
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
      saturateMin(nInstancesInOneStep, _maxNightInstancesSprite);
      break;
    case VD_Position_Normal_Tex0:
    case VD_Position_Normal_Tex0_ST:
    case VD_Position_Normal_Tex0_Tex1:
    case VD_Position_Normal_Tex0_Tex1_ST:
      saturateMin(nInstancesInOneStep, _maxNightInstances);
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
    bool ok = BeginMeshTLInstanced(sMesh, prop, spec, lights, bias, minDist2, dp);
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
          instances[drawnInstances]._object->SetupInstances(sMesh, &instances[drawnInstances], newInstances);
        }
        else
        {
          SetupInstances(&instances[drawnInstances], newInstances);
        }

#if USE_PUSHBUFFERS
        // If pushbuffers are to be used then either record them or use them, else draw it the usual way
        if (sMesh.CanUsePushBuffer() && EnablePushBuffers && !LightsPresent() && BonesWereSetDirectly())
        {
          // Decide if pushbuffers are to be drawn
          if (ShowPushBuffers)
          {
            // Get the key to characterize the current dynamic variable state
            int key = GetDynamicVariablesIdentifier(true, matLOD,prop._begSection,prop._endSection);

            // Try to draw the pushbuffer. If failed the PB must be created (and the sections
            // will be drawn during recording)
            PBContext pbContext;
            pbContext._materialLevel = -1;
            pbContext._instancesOffset = drawnInstances;
            pbContext._instancesCount = newInstances;
            pbContext._sMesh = &sMesh;
            if (!vb->DrawPushBuffer(key, pbContext))
            {
              // Set HW to defined state
              SetHWToUndefinedState();

              // Create the empty pushbuffer
              PushBuffer *pb = CreateNewPushBuffer(key);

              // Assign the new pushbuffer to the vertex buffer immediately for vertex buffer
              // to take control over it
              vb->AssignNewPushBuffer(pb);

              // Begin PB creation
              BeginPBCreation(pb);

              // Record the pushbuffer
              DrawMeshTLSectionsInstanced(sMesh, matLOD, prop, spec, bias, drawnInstances, newInstances);

              // End PB creation
              EndPBCreation();
            }
            
            // Finish PB drawing
            PBDrawingCleanup();
          }
        }
        else
#endif
        {
          DrawMeshTLSectionsInstanced(sMesh, matLOD, prop, spec, bias, drawnInstances, newInstances);
        }

        // Update the number of drawn instances
        drawnInstances += newInstances;
      }

      // Finish mesh
      EndMeshTL(sMesh);
    }
  }
  else
  {
    DoAssert(instances);
    Engine::DrawMeshTLInstanced(sMesh, matLOD, prop, spec, lights, bias, instances, nInstances, minDist2);
    return;
  }
}

void EngineDDT::DrawShadowVolumeInstanced(LODShape *shape, int level, const ObjectInstanceInfo *instances, int instanceCount)
{
  DoAssert(instanceCount > 0);

  if (!enableInstancing)
  {
    base::DrawShadowVolumeInstanced(shape, level, instances, instanceCount);
    return;
  }

  ShapeUsed shadow = shape->Level(level);

  // Return immediately in some cases
  if (instanceCount == 0) return;
  if (!GetTL()) return;
  if (shadow->GetVertexBuffer() == NULL) return;

  // Go through all instances in chunks
  VertexBufferD3DT *vb = static_cast<VertexBufferD3DT *>(shadow->GetVertexBuffer());
  int nInstancesInOneStep = vb->GetVBufferSize() / shadow->NVertex();

#if _ENABLE_SKINNEDINSTANCING
  if ((instanceCount < 1) || (nInstancesInOneStep < 1))
#else
  if ((instanceCount <= 1) || (nInstancesInOneStep <= 1))
#endif
  {
    Engine::DrawShadowVolumeInstanced(shape,level, instances, instanceCount);
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
      instances[i]._object->ClippingInfo(minMax);
      Matrix4Par frame = instances[i]._origin;
      
      float shadowSize = GScene->CheckShadowSize(minMax,frame,knownShadowSize);
      
      saturateMax(knownShadowSize,shadowSize);
      // if the shadow is long enough, break - it will not be any longer
      if (knownShadowSize>=maxShadowSize*0.99f) break;
    }
    shadowSize = knownShadowSize;

    
  }

  // Prepare shadow volume drawing
  BeginShadowVolume(shadowSize);

  ADD_COUNTER(oShdV,instanceCount);
  //ADD_COUNTER(bShdV,shape->Level(level)->NSections()*2);
  //DPRIM_STAT_SCOPE_SIMPLE(ShdV);

  //DPRIM_STAT_SCOPE((shape)->GetName(),ShdV);

  DPRIM_STAT_SCOPE((shape)->ShapeName((level)),ShdV);

  // Set shader constants
  int spec=IsShadow|IsAlphaFog;
  const LightList noLights;
  bool ok = BeginMeshTLInstanced(*shadow, *shape, spec, noLights, ShadowBias, -1.0f, DrawParameters::_default);
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
      SetupInstancesShadowVolume(&instances[drawnInstances], newInstances);

      // Go through all the sections
      for (int i = 0; i < shadow->NSections(); i++)
      {
        ShapeSection sec = shadow->GetSection(i);

        // Skip sections not designed for drawing
        if (sec.Special() & (IsHidden|IsHiddenProxy|NoShadow)) continue;

        // Draw section
        sec.OrSpecial(IsShadow | IsShadowVolume);
        sec.SetTexture(NULL);
        sec.PrepareTL(shadowMat, 0, 0, prop);
        DrawSectionTL(*shadow, i, i + 1, ShadowBias, drawnInstances, newInstances);
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
        int newInstances = min(nInstancesInOneStep, instanceCount - drawnInstances);

        // Set the instances
        SetupInstancesShadowVolume(&instances[drawnInstances], newInstances);

        // Go through all the sections
        for (int i = 0; i < shadow->NSections(); i++)
        {
          ShapeSection sec = shadow->GetSection(i);

          // Skip sections not designed for drawing
          if (sec.Special() & (IsHidden|IsHiddenProxy|NoShadow)) continue;

          // Draw section
          sec.OrSpecial(IsShadow | IsShadowVolume | ShadowVolumeFrontFaces);
          sec.SetTexture(NULL);
          sec.PrepareTL(shadowMat, 0, 0, prop);
          DrawSectionTL(*shadow, i, i + 1, ShadowBias, drawnInstances, newInstances);
        }

        // Update the number of drawn instances
        drawnInstances += newInstances;
      }
    }


    // Finish the mesh drawing
    EndMeshTL(*shadow);
  }
}

void EngineDDT::BeginShadowVolume(float shadowVolumeSize)
{
  float constants[4] = {shadowVolumeSize, shadowVolumeSize, shadowVolumeSize , shadowVolumeSize};
  SetVertexShaderConstantF(VSC_ShadowVolumeLength, constants, 1);
}

float EngineDDT::GetClosestZ2(float minDist2)
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

void EngineDDT::BeginInstanceTL(const Matrix4 &modelToWorld, float minDist2, int spec, const LightList &lights, const DrawParameters &dp)
{
  if (ResetNeeded()) return;
  
  SwitchTL(TLEnabled);

  // Setup the camera position
  Camera *camera = GScene->GetCamera();
  
  if (!dp._cameraSpacePos)
  {
    // Setup the view-world matrix
    Matrix4 viewWorld;
    // natural calculation would be
    //viewWorld = _view * modelToWorld;
    // this exhibits serious precision problems for large world space coordinates
    // instead of using inverse, subtract position and use inverse orientation only

    
    // VW = VR * VT * WR * WT
    Matrix4 modelToWorldOffset;
    modelToWorldOffset.SetOrientation(modelToWorld.Orientation());
    modelToWorldOffset.SetPosition(modelToWorld.Position()-_invView.Position());
    Matrix4 viewZero;
    viewZero.SetOrientation(_view.Orientation());
    viewZero.SetPosition(VZero);
    
    viewWorld = viewZero * modelToWorldOffset;
    
    //Matrix4 viewWorldTest = _view * modelToWorld;
    //static bool testOld = false;
    //if (testOld) viewWorld = viewWorldTest;

    D3DXMATRIX viewWorldMatrix;
    ConvertMatrixTransposedT(viewWorldMatrix, viewWorld);
    SetVertexShaderConstantF(VSC_ViewMatrix, (float*)&viewWorldMatrix, 3);
    // Remember the world matrix (to determine the object space)
    _world = modelToWorld;
  }
  else
  {
    // modelToWorld is actually viewWorld matrix (model to camera)
    // Setup the view-world matrix
    D3DXMATRIX viewWorldMatrix;
    ConvertMatrixTransposedT(viewWorldMatrix, modelToWorld);
    SetVertexShaderConstantF(VSC_ViewMatrix, (float*)&viewWorldMatrix, 3);

    // Construct the world matrix from camera and modelToWorld
    // cameraToWorld*modelToCamera
    _world = camera->Transform()*modelToWorld;
  }
  _invWorld = _world.InverseGeneral();


  // Set camera position and direction in model (object) space to VS constants
  {
    // Camera position
    Vector3 cameraPosition = camera->Position();
    Vector3 cameraObjectPosition = TransformPositionToObjectSpace(cameraPosition);
    D3DXVECTOR4 cpos(cameraObjectPosition.X(), cameraObjectPosition.Y(), cameraObjectPosition.Z(), 1.0f);
    SetVertexShaderConstantF(VSC_CameraPosition, (float*)&cpos, 1);

    // Camera direction
    Vector3 cameraDirection = camera->Direction();
    Vector3 cameraObjectDirection = TransformAndNormalizeVectorToObjectSpace(cameraDirection);
    D3DXVECTOR4 cdir(cameraObjectDirection.X(), cameraObjectDirection.Y(), cameraObjectDirection.Z(), 1.0f);
    SetVertexShaderConstantF(VSC_CameraDirection, (float*)&cdir, 1);
  }

  // Selecting of pixel shader type
  {
    // If we render the depth map, then use the appropriate pixel shader
    if (_renderingMode == RMDepthMap)
    {
      SelectPixelShaderType(PSTInvDepthAlphaTest);
    }
    else
    {
      // Decide whether the shadow buffer should be casted on object or not
      // Objects with minDist2 smaller than 0 are not usual (like shadows) and we don't want
      // shadow to be casted on them. We also don't want to cast shadows in case shadow map
      // rendering is in progress. SREC (don't delete this label)
      if (IsSBEnabled())
      {
        if (_renderingMode == RMShadowBuffer)
        {
          SelectPixelShaderType(PSTShadowCaster);
        }
        else
        {
          if (minDist2 >= 0.0f && dp._canReceiveShadows)
          {
            // Get the Z value closest to the camera
            float closestZ2 = GetClosestZ2(minDist2);

            // Select appropriate pixel shader type. Note that the _pixelShaderTypeSel value is also used for VS selection
            if (closestZ2 < Glob.config.shadowsZ * Glob.config.shadowsZ)
            {
              SelectPixelShaderType(PSTShadowReceiver1);

              // Pass the shadowsZ value to the PS. _shadowFactor is here to make the shadow disappear
              // during day/night transition (the similar thing works in SV shadows too)
              float sZ = Glob.config.shadowsZ * _shadowFactor;
              D3DXVECTOR4 vector4(sZ, sZ, sZ, sZ);
              SetPixelShaderConstantF(PSC_Shadow_Factor_ZHalf, vector4, 1);
            }
            else
            {
              SelectPixelShaderType(PSTBasicNA);
            }
          }
          else
          {
            // Not designed as shadow receiver
            SelectPixelShaderType(PSTBasicNA);
          }
        }
      }
      else
      {
        if (GScene->GetObjectShadows())
        {
          if (minDist2 >= 0.0f && dp._canReceiveShadows)
          {
            // Get the Z value closest to the camera
            float closestZ2 = GetClosestZ2(minDist2);

            // Select appropriate pixel shader type. Note that the _pixelShaderTypeSel value is also used for VS selection
            if (closestZ2 < Glob.config.shadowsZ * Glob.config.shadowsZ)
            {
              SelectPixelShaderType(PSTBasic);
            }
            else
            {
              SelectPixelShaderType(PSTBasicNA);
            }
          }
          else
          {
            // Not designed as shadow receiver
            SelectPixelShaderType(PSTBasicNA);
          }
        }
        else
        {
          SelectPixelShaderType(PSTBasicNA);
        }
      }
    }
  }

  // Setup lights
  SetupLights(lights, spec);
}

bool EngineDDT::BeginMeshTL(
  const VertexTableAnimationContext *animContext, const Shape &sMesh, const EngineShapeProperties &prop, int spec, const LightList &lights, int bias
)
{
  if (ResetNeeded()) return false;

  SwitchTL(TLEnabled);
  //EnableSunLight(((spec&(DisableSun|SunPrecalculated))==0)?ML_Sun:ML_None);

  //ChangeWDepth(proj(2,2),proj.Position()[2]);
  ChangeClipPlanes();

  
  //SetBias(bias);

  VertexBufferD3DT *vb = static_cast<VertexBufferD3DT *>(sMesh.GetVertexBuffer());

  if (vb->IsInList())
  {
    _vBufferLRU.Refresh(vb);
  }
  else
  {
    _vBufferLRU.Add(vb);
  }

  vb->SetOwner(&sMesh);
  // update if necessary
  vb->Lock();

  vb->Update(animContext, sMesh, prop, VSDV_Basic);
  
  return true;
}

void EngineDDT::ClearLights()
{
  _lights.Resize(0); // currently active lights
  if (_lights.MaxSize()>64) _lights.Clear();
}

void EngineDDT::EndMeshTL( const Shape &sMesh )
{
  // turn off all lights
  ClearLights();
  VertexBufferD3DT *vb = static_cast<VertexBufferD3DT *>(sMesh.GetVertexBuffer());
  vb->Unlock();
}

static Vector3 RoundVector(Vector3Par point, Vector3Par roundpoint, float smTexelSize)
{
  return Vector3(
    roundpoint.X() + toInt((point.X() - roundpoint.X()) / smTexelSize) * smTexelSize,
    roundpoint.Y() + toInt((point.Y() - roundpoint.Y()) / smTexelSize) * smTexelSize,
    roundpoint.Z() + toInt((point.Z() - roundpoint.Z()) / smTexelSize) * smTexelSize
  );
}


void EngineDDT::BeginShadowMapRendering(float sunClosestDistanceFrom0)
{
  // Calculate position and radius of the frustum bounding sphere
  Camera *cam = GScene->GetCamera();
  Vector3 position;
  float radius;
  {
    // Get the near and far frustum boundaries
    float frustumFront = cam->Near();
    float frustumBack = Glob.config.shadowsZ;
    
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
    if (s > frustumBack) s = frustumBack;

    // Calculate the radius of the frustum bounding sphere
    float backDistance = frustumBack - s;
    radius = sqrt(backDistance*backDistance + c*c);

    // Calculate the position of the frustum bounding sphere (in world coordinates)
    position = cam->Position() + cam->Direction() * s;
  }
  float diameter = radius * 2.0f;

  // Get the shadow direction
  Vector3 shadowDirection = GScene->MainLight()->ShadowDirection();

  // Calculate the shadow plane offset from center of the frustum bounding sphere
  float shadowPlaneOffset;
  {
    // Old estimation
    //shadowPlaneOffset = radius + 20.0f;

    // Calculate the sphere position distance from 0 in the sun direction
    float d = position.DotProduct(shadowDirection);

    // Difference between this distance and closest-object-to-sun distance is the required offset
    shadowPlaneOffset = max(d - sunClosestDistanceFrom0, radius);
  }

  {
    // Move in the local rounded shadow plane space according to current camera position
    Vector3 lrspCameraPos = _oldLRSP.InverseRotation().FastTransform(cam->Position());
    float smTexelSize = diameter / _sbSize;
    Vector3 roundedLRSPCameraPos = RoundVector(lrspCameraPos, VZero, smTexelSize);
    _oldLRSP.SetPosition(_oldLRSP.FastTransform(roundedLRSPCameraPos));

    // Set the origin to fit the current light direction
    _oldLRSP.SetDirectionAndUp(shadowDirection, VForward);

    // Get the shadow plane origin in world coordinates...
    Vector3 worldSPOrigin = position - shadowDirection * shadowPlaneOffset;

    // ...express it in LRSP space...
    Vector3 lrspSPOriginPos = _oldLRSP.InverseRotation().FastTransform(worldSPOrigin);

    // ...round it there and convert it back.
    Vector3 roundedLRSPSPOriginPos = RoundVector(lrspSPOriginPos, VZero, smTexelSize);

    // Save the results
    _shadowPlaneOrigin.SetPosition(_oldLRSP.FastTransform(roundedLRSPSPOriginPos));
    _shadowPlaneOrigin.SetOrientation(_oldLRSP.Orientation());
    _invView = _shadowPlaneOrigin;
    _view = _shadowPlaneOrigin.InverseRotation();
  }

  // Set the shadow plane projection parameters
  {
    float invDiameter = 1.0f / diameter;
    _shadowPlaneProjection = Matrix4(
      invDiameter,  0,            0,                                      0.5,
      0,            -invDiameter, 0,                                      0.5,
      0,            0,            1.0f / (radius + shadowPlaneOffset), 0);
  }

  // Set rendering parameters according to selected technique
  float projectionOffset = 0.4f;
  switch (_sbTechnique)
  {
  case SBT_Default:
    {
      projectionOffset = 0.4f;
    }
    break;
  case SBT_nVidia:
  case SBT_ATIDF16:
  case SBT_ATIDF24:
    {
      // Disable projection offset (offset will be involved in render states)
      projectionOffset = 0.0f;

      // Set the depth biases in render states
      static float fDepthBias = 0.0006f;
      static float fSlopeScaleDepthBias = 1.5f;
#if _ENABLE_CHEATS
      if( GInput.GetCheat3ToDo(DIK_Q))
      {
        fDepthBias += 0.0001f;
        GlobalShowMessage(500,"DepthBias %f", fDepthBias);
      }
      if( GInput.GetCheat3ToDo(DIK_A))
      {
        fDepthBias -= 0.0001f;
        GlobalShowMessage(500,"DepthBias %f", fDepthBias);
      }
      if( GInput.GetCheat3ToDo(DIK_W))
      {
        fSlopeScaleDepthBias += 0.1f;
        GlobalShowMessage(500,"SlopeScaleDepthBias %f", fSlopeScaleDepthBias);
      }
      if( GInput.GetCheat3ToDo(DIK_S))
      {
        fSlopeScaleDepthBias -= 0.1f;
        GlobalShowMessage(500,"SlopeScaleDepthBias %f", fSlopeScaleDepthBias);
      }
#endif
      DWORD dwDepthBias = *(DWORD*)&fDepthBias;
      D3DSetRenderState(D3DRS_DEPTHBIAS_OLD, dwDepthBias);
      DWORD dwSlopeScaleDepthBias = *(DWORD*)&fSlopeScaleDepthBias;
      D3DSetRenderState(D3DRS_SLOPESCALEDEPTHBIAS_OLD, dwSlopeScaleDepthBias);
    }
    break;
  default:
    Fail("Error: Unknown SB technique");
  }

  // Set the SM matrix (matrix from view space to shadow plane space)
  {
    // Add offset to prevent the shadow acne
    _shadowPlaneOrigin.SetPosition(_shadowPlaneOrigin.Position() + _shadowPlaneOrigin.Direction() * projectionOffset);

    // FLY unknown reason for scaling the camera origin - can we use cam->InverseRotation everywhere?
    Matrix4 shadowMapMatrix = _shadowPlaneProjection * (cam->InverseScaled() * _shadowPlaneOrigin).InverseScaled();
    D3DXMATRIX shadowMapMatrixDX;
    ConvertMatrixTransposedT(shadowMapMatrixDX, shadowMapMatrix);
    SetVertexShaderConstantF(VSC_ShadowmapMatrix, (float*)&shadowMapMatrixDX, 3);
  }

  // Set the SB rendering flag
  _renderingMode = RMShadowBuffer;

  // Set own projection matrix parameters - projection matrix for shadow map rendering
  {
    float invRadius = 1.0f / radius;
    Matrix4 smProjection = Matrix4(
      invRadius,    0,          0,                                      0,
      0,            invRadius,  0,                                      0,
      0,            0,          1.0f / (radius + shadowPlaneOffset), 0);
    D3DXMATRIX smProjectionDX;
    ConvertMatrixTransposedT(smProjectionDX, smProjection);
    SetVertexShaderConstantF(VSC_ProjMatrix, (float*)&smProjectionDX, 4);
  }

  // Set the SM texture to NULL
  SetTexture(TEXID_SHADOWMAP, NULL, false);
  
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   // Set shadow buffer as temporary render target
//   SetTemporaryRenderTarget(_renderTargetSB, _renderTargetSurfaceSB, _renderTargetDepthBufferSurfaceSB);
// 
  // Some SB techniques use the _renderTargetSB of size 1x1 (whereas the depth-buffer is of proper size)
  // In such cases the viewport will be set during the rendertarget setting with 1x1 values. We want
  // it to have the back buffer resolution.
  // See the label SBSVP
  SetViewport(_sbSize, _sbSize);

  // Clear the buffer
  Clear(true, true, PackedColor(HWhite));
}

/*!
\patch 5126 Date 2/2/2007 by Ondra
- Fixed: When using shadow detail high with some nVidia cards (Nv3x chip-set)
loading screen could happen sometimes mid-game.
The problem was caused by bad usage of D3DSAMP_SRGBTEXTURE_OLD, which was wrongly handled by the driver.
*/

void EngineDDT::EndShadowMapRendering()
{
  // Restore the old render target
  RestoreOldRenderTarget();

  // Restore original setting of the viewport
  // See the label SBSVP
  SetViewport(_viewW, _viewH);

  // Set the SM texture parameters or restore parameters set in BeginShadowMapRendering
  switch (_sbTechnique)
  {
  case SBT_Default:
    {
      // Set texture and sampling parameters
      Fail("DX10 TODO");
      // SetTexture(TEXID_SHADOWMAP, _renderTargetSB, false); // Direct3D 10 NEEDS UPDATE 
      D3DSetSamplerState(TEXID_SHADOWMAP, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
      D3DSetSamplerState(TEXID_SHADOWMAP, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
      D3DSetSamplerState(TEXID_SHADOWMAP, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);
      D3DSetSamplerState(TEXID_SHADOWMAP, D3DSAMP_SRGBTEXTURE_OLD, FALSE);
    }
    break;
  case SBT_nVidia:
    {
      // Set texture and sampling parameters
      Fail("DX10 TODO");
      // SetTexture(TEXID_SHADOWMAP, _renderTargetDepthBufferSB, false); a// Direct3D 10 NEEDS UPDATE 
      D3DSetSamplerState(TEXID_SHADOWMAP, D3DSAMP_MINFILTER_OLD, D3DTEXF_LINEAR_OLD);
      D3DSetSamplerState(TEXID_SHADOWMAP, D3DSAMP_MAGFILTER_OLD, D3DTEXF_LINEAR_OLD);
      D3DSetSamplerState(TEXID_SHADOWMAP, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);
      D3DSetSamplerState(TEXID_SHADOWMAP, D3DSAMP_SRGBTEXTURE_OLD, FALSE);

      // Restore default bias values
      D3DSetRenderState(D3DRS_DEPTHBIAS_OLD, 0);
      D3DSetRenderState(D3DRS_SLOPESCALEDEPTHBIAS_OLD, 0);
    }
    break;
  case SBT_ATIDF16:
  case SBT_ATIDF24:
    {
      // Set texture and sampling parameters
      Fail("DX10 TODO");
      // SetTexture(TEXID_SHADOWMAP, _renderTargetDepthBufferSB, false); // Direct3D 10 NEEDS UPDATE 
      D3DSetSamplerState(TEXID_SHADOWMAP, D3DSAMP_MINFILTER_OLD, D3DTEXF_POINT_OLD);
      D3DSetSamplerState(TEXID_SHADOWMAP, D3DSAMP_MAGFILTER_OLD, D3DTEXF_POINT_OLD);
      D3DSetSamplerState(TEXID_SHADOWMAP, D3DSAMP_MIPFILTER_OLD, D3DTEXF_NONE_OLD);
      D3DSetSamplerState(TEXID_SHADOWMAP, D3DSAMP_SRGBTEXTURE_OLD, FALSE);

      // Restore default bias values
      D3DSetRenderState(D3DRS_DEPTHBIAS_OLD, 0);
      D3DSetRenderState(D3DRS_SLOPESCALEDEPTHBIAS_OLD, 0);
    }
    break;
  default:
    Fail("Error: Unknown SB technique");
  }

  // Set the SB rendering flag
  _renderingMode = RMCommon;

  // Restore the camera projection setting. Note this must be called after _renderingMode was set
  ProjectionChanged();

  // Set the _view back
  DoSwitchTL(TLEnabled);
}

void EngineDDT::EnableDepthOfField(float focusDist, float blur, bool farOnly)
{
  _focusDist = focusDist;
  _blurCoef = blur;
  _farOnly = farOnly;
}

void EngineDDT::EnableDepthOfField(int quality)
{
  // we currently support one quality only
  saturate(quality,0,1);
  // Quality hasn't changed, return
  if (quality == _dbQuality) return;

  // Engine can't use depth buffer, return
  if (!_dbAvailable) return;

  // Remember the DOF quality
  _dbQuality = quality;

  // if there is no textBank yet, there is no need to flush anything
  if (!_textBank) return;
  
  // If DOF quality is not 0, create DB resources
  if (quality > 0)
  {
    // avoid creating the surface in AGP memory
    // for this we need to make sure device VRAM is free
    if (_textBank->GetVRamTexAllocation()>0 || _vBuffersAllocated>0)
    {
      DisplayWaitScreen();
      if (_textBank) _textBank->ReleaseAllTextures();
      ReleaseAllVertexBuffers();
      // pretend the reset was done - this will make sure preload is done
      _resetDone = true;
    }

    // Create DB resources
    CreateDBResources();
  }
  else
  {
    // Destroy DB resources
    DestroyDBResources();
  }

  _textBank->ResetMaxTextureQuality();
}

bool EngineDDT::IsDepthOfFieldEnabled() const
{
  //Fail("DX10 TODO");
  return false;
  // Direct3D 10 NEEDS UPDATE 
//   if (_blurCoef <= 0.001f) return false;
//   return _renderTargetDB.NotNull();
}

void EngineDDT::BeginDepthMapRendering()
{
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   // Set the SB rendering flag
//   _renderingMode = RMDepthMap;
// 
//   // Set depth buffer as temporary render target
//   SetTemporaryRenderTarget(_renderTargetDB, _renderTargetSurfaceDB, _renderTargetDepthStencilSurfaceDB);
// 
//   // Clear the buffer and Z-buffer (depth map uses own Z-buffer, because the scene Z-buffer might have been created with multisampling)
//   Clear(true, true, PackedColor(HBlack));
}

void EngineDDT::EndDepthMapRendering()
{
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   // Restore the old render target
//   RestoreOldRenderTarget();
// 
//   // Set the SB rendering flag
//   _renderingMode = RMCommon;
}

void EngineDDT::SetDepthClip(float zClip)
{
  SetPixelShaderConstantF(PSC_DepthClip, D3DXVECTOR4(1.0f, 0.0f, 0.0f, zClip), 1);
}

void EngineDDT::UseCraters(const AutoArray<CraterProperties> &craters, int maxCraters, const Color &craterColor)
{
  // Get the actual number of craters to draw
  int cratersCount = min(craters.Size(), maxCraters);

  // Create array of craters (fill out rest with zero)
  DoAssert(cratersCount <= MAX_CRATERS);
  Color cr[MAX_CRATERS];
  for (int i = 0; i < cratersCount; i++)
  {
    // Calculate the disappear coefficient (number between 0 and 1). Note we know it is never zero
    float disappearCoef = min(2.0f * float(i + 1 + maxCraters - cratersCount) / maxCraters, 1.0f);
    float invDisappearCoef2 = 1.0f / (disappearCoef * disappearCoef);

    // Fill out the position and scaled size
    int itemIndex = craters.Size() - cratersCount + i;
    cr[i] = Color(craters[itemIndex]._pos.X(), craters[itemIndex]._pos.Y(), craters[itemIndex]._pos.Z(), craters[itemIndex]._invRadius2 * invDisappearCoef2);
  }

  // Set the craters
  SetPixelShaderConstantF(PSC_Crater, (float *)cr, cratersCount);

  // Pass on light color multiplied by crater color
  LightSun *sun = GScene->MainLight();
  Color lighting = sun->SimulateDiffuse(0.5) * GetAccomodateEye() * _modColor * craterColor;
  lighting.SetA(craterColor.A());
  SetPixelShaderConstantF(PSC_GlassEnvColor, (float*)&lighting, 1);
}

void EngineDDT::SetLowEndQuality()
{
  bool sceneLowEnd = GScene ? (GScene->GetShadingQuality() < 1) : false;
  bool lowEnd = sceneLowEnd || (_textBank->GetTextureQuality() < 0);

  // Quality hasn't changed, return
  if (lowEnd == _lowEndQuality) return;

  // Remember quality
  _lowEndQuality = lowEnd;

  if (_textBank->GetVRamTexAllocation()>0 || _vBuffersAllocated>0)
  {
    // It probably take a while, so display waitscreen
    DisplayWaitScreen();

    // If we're changing to low-end quality, release textures (many normal maps won't be necessary anymore)
    if (_lowEndQuality && _textBank) _textBank->ReleaseAllTextures();

    // Release vertex buffers, that should regenerate the push-buffer that could contain wrong PS reference
    ReleaseAllVertexBuffers();

    // pretend the reset was done - this will make sure preload is done
    _resetDone = true;
  }
}

bool EngineDDT::TextureShouldBeUsed(const Texture *tex) const
{
  if (GetLowEndQuality())
  {
    switch (tex->Type())
    {
    case TT_Normal:
      return false;
    }
  }
  return true;
}

void EngineDDT::DiscardVB()
{
}

static __forceinline PackedColor ColorScaleD2(PackedColor col)
{
  // multiply RGB by 0.5, keep A
  return PackedColor(((col&0xfefefe)>>1)|(col&0xff000000));
}

static __forceinline PackedColor ColorScale(PackedColor col) {return col;}

/*!
  - keep mesh buffer allocated as long as possible
  - fill vertex buffer with vertices
*/
bool EngineDDT::AddVertices(const TLVertex *v, int n)
{
  // No vertices, nothing to add
  if (n <= 0) return true;

  // Check if the new data fit into the present buffer. If yes then calculate the proper offset. If not then flush buffer first
  int lockOffset; // Offset in vertices
  D3D10_MAP lockFlags;
  if (_queueNo._vertexBufferUsed + n <= MeshBufferLength && !_queueNo._firstVertex)
  {
#if LOG_LOCKS
    LogF("add D3DLOCK_NOOVERWRITE %d + %d",_queueNo._vertexBufferUsed,n);
#endif
    lockOffset = _queueNo._vertexBufferUsed;
    lockFlags = D3D10_MAP_WRITE_NO_OVERWRITE;
  }
  else
  {
    // If the whole buffer cannot hold the data, report problem and finish
    if (n > MeshBufferLength)
    {
      RptF("Vertex Buffer too small (%d<%d)", n, MeshBufferLength);
      Fail("Error: Too many vertices in 2D drawing");
      return false;
    }
    _queueNo._firstVertex = false;

    // start a new vertex buffer
#if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("Vertex buffer full");
    }
#endif

    //FlushAndFreeAllQueues(_queueNo);
    FlushAllQueues(_queueNo);
    // check if we will fit into actual vertex buffer
    // if not, flush old vertex buffer
#if LOG_LOCKS
    LogF("add D3DLOCK_DISCARD %d + %d",0,n);
#endif
    lockOffset = 0;
    lockFlags = D3D10_MAP_WRITE_DISCARD;
#if CUSTOM_VB_DISCARD
    if (++_queueNo._actualVBIndex>=NumDynamicVB2D)
    {
      _queueNo._actualVBIndex = 0;
    }
#endif
    _queueNo._vertexBufferUsed = 0;
  }

  const ComRef<IDirect3DVertexBuffer9Old> &buf = _queueNo.GetVBuffer();
  PROFILE_DX_SCOPE_DETAIL(3davC);
  
Retry:

  void *data = NULL;
  HRESULT err = GEngineDD->Map(buf, lockFlags, 0, &data);
  if( err!=D3D_OK )
  {
    if (err!=D3D_OK || !data)
    {
      //if (QFBank::FreeUnusedBanks(1024*1024)) goto Retry;
      if (FreeOnDemandSystemMemoryLowLevel(1024*1024)) goto Retry;
      ErrF("VB Lock failed, %s",DXGetErrorString8(err));
      return false;
    }
  }
  else
  {
    if (data)
    {
      // memcpy not possible if we want to apply factor 0.5
      // if would be more efficient to apply it sooner,
      // but for this we probably need to change Engine interface
      TLVertex *vData = &((TLVertex *)data)[lockOffset];
      for (int i=0; i<n; i++)
      {
        const TLVertex &s = v[i];
        TLVertex &d = vData[i];
        d.pos = Vector3(s.pos.X()/_w*2.0f - 1.0f, -(s.pos.Y()/_h*2.0f - 1.0f), s.pos.Z());
        d.rhw = s.rhw;
        d.color = ColorScale(s.color);
        d.specular = ColorScale(s.specular);
        d.t0 = s.t0;
        d.t1 = s.t1;
      }
      //memcpy(data,v,size);
    }
    buf->Unmap();
  }
  _queueNo._meshBase = _queueNo._vertexBufferUsed;
  _queueNo._meshSize = n;
  _queueNo._vertexBufferUsed += n;
  return true;
}

bool EngineDDT::BeginMesh( TLVertexTable &mesh, int spec )
{
  SwitchTL(TLEnabled);
  _mesh = &mesh;

  // Zero vertex and index buffer in the device, forget _iBufferLast and _vBufferLast.
  ForgetDeviceBuffers();

  return AddVertices(mesh.VertexData(),mesh.NVertex());
}

void EngineDDT::EndMesh( TLVertexTable &mesh )
{
  _mesh = NULL;
}

QueueT::QueueT()
{
  for (int i=0; i<MaxTriQueues; i++) _triUsed[i] = false;
  _usedCounter = 0;
  _vertexBufferUsed = 0;
  _indexBufferUsed = 0;
  _meshBase = 0;
  _meshSize = 0;
  _actTri = -1;
  _firstVertex = true;
  _firstIndex = true;
  _actualVBIndex=0;
  _actualIBIndex=0;

}

int QueueT::Allocate
(
  TextureD3DT *tex, int level, const TexMaterial *mat, int spec,
  int minI, int maxI, int tip
)
{
  int index = -1;
  // slot shown by tip may (but need not) contain the setup we need
  if (tip>=minI && tip<maxI && _triUsed[tip])
  {
    TriQueueT &triq = _tri[tip];
    if (tex==triq._texture && spec==triq._special) index = tip;
  }


  int free = -1;
  if (index<0)
  {
    for (int i=minI; i<maxI; i++)
    {
      if (_triUsed[i])
      {
        TriQueueT &triq = _tri[i];
        if (tex!=triq._texture) continue;
        if (mat!=triq._material) continue;
        if (spec!=triq._special) continue;
        index = i;
      }
      else
      {
        if (free<0) free = i;
      }
    }
  }
  _usedCounter++;
  if (index>=0)
  {
    TriQueueT &triq = _tri[index];
    // append to queue index
    saturateMin(triq._level,level);
    triq._lastUsed = _usedCounter;
    Assert (_triUsed[index]);
    /*
    if (LogStatesOnce)
    {
      LogF("Allocated old queue %d: %s",index,tex ? tex->Name() : "<NULL>");
    }
    */
    return index;
  }
  if (free>=0)
  {
    TriQueueT &triq = _tri[free];
    //triq._queueBase = _meshBase;
    //triq._queueSize = 0;
    triq._special = spec;
    triq._texture = tex;
    triq._level = level;
    triq._material = mat;
    triq._lastUsed = _usedCounter;
    Assert (triq._triangleQueue.Size()==0);
    triq._triangleQueue.Resize(0);
    _triUsed[free] = true;
    #if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("Allocated new queue %d: %s",free,tex ? tex->Name() : "<NULL>");
    }
    #endif
  }
  return free;
}

void QueueT::Free( int i )
{
  Assert (_tri[i]._triangleQueue.Size()==0);
  Assert( _triUsed[i] );
  _triUsed[i] = false;
  #if DO_TEX_STATS
  if (LogStatesOnce)
  {
    LogF("Released queue %d",i);
  }
  #endif
}

void EngineDDT::DoSwitchRenderMode(RenderMode mode)
{
  #if DO_TEX_STATS
  if (LogStatesOnce)
  {
    LogF("DoSwitchRenderMode %d",mode);
  }
  #endif
  FlushAndFreeAllQueues(_queueNo);
  _renderMode = mode;
}

WORD *EngineDDT::QueueAdd( QueueT &queue, int n )
{
  Assert (queue._actTri>=0);
  Assert (queue._triUsed[queue._actTri]);
  TriQueueT &triq = queue._tri[queue._actTri];
  if( triq._triangleQueue.Size()+n>TriQueueSize )
  {
    // flush but keep allocated
    #if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("QueueT full %d",queue._actTri);
    }
    #endif
    FlushQueue(queue,queue._actTri);
  }

  int index = triq._triangleQueue.Size();
  /*
  if (index==0)
  {
    // starting a new queue content
    triq._queueBase = queue._meshBase;
    triq._queueSize = 0;
  }
  */
  triq._triangleQueue.Resize(index+n);
  return triq._triangleQueue.Data()+index;
}

void EngineDDT::QueueFan( const VertexIndex *ii, int n )
{
  DoAssert(!ResetNeeded());
  int addN = (n-2)*3;
  //TriQueueT &triq = _queueNo._tri[_queueNo._actTri];
  WORD *tgt = QueueAdd(_queueNo,addN);

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
    "QueueT fan %d - meshBase %d, queueBase %d, offset %d",
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

int EngineDDT::AllocateQueue(QueueT &queue, TextureD3DT *tex, int level, const TexMaterial *mat, int spec)
{
  // scan if there is some queue with same attributes
  bool alpha = tex && tex->IsAlpha() || !_enableReorder;

  int minI = 0;
  int maxI = MaxTriQueues-1;
  if (alpha)
  {
    minI = MaxTriQueues-1, maxI = MaxTriQueues;
    // flush all other queues
    FlushAllQueues(queue,MaxTriQueues-1);
  }

  int index = queue.Allocate(tex,level,mat,spec,minI,maxI,queue._actTri);
  if (index>=0)
  {
    Assert(queue._triUsed[index]);
    return index;
  }
  // we must free some queue
  // hard to tell which, select random
  //int random = GRandGen.RandomValue()&MaxTriQueues;
  // find LRU
  int minUsed = INT_MAX;
  for (int i=minI; i<maxI; i++)
  {
    int used = queue._tri[i]._lastUsed;
    if (used<minUsed)
    {
      minUsed = used;
      index = i;
    }
  }
  if (index<0)
  { 
    Fail("LRU failed");
    index=0;
  }
  FlushAndFreeQueue(queue,index);

  index = queue.Allocate(tex,level,mat,spec,minI,maxI,index);

  Assert (index>=0); // some queues is free now (after FlushAndFreeQueue)

  Assert(queue._triUsed[index]);

  return index;
}

void EngineDDT::FreeQueue(QueueT &queue, int index)
{
  // queue is already flushed
  Assert(!queue._tri[index]._triangleQueue.Size());
  queue.Free(index);
}

void EngineDDT::FlushAllQueues(QueueT &queue, int skip)
{
  #if DO_TEX_STATS>=2
  if (LogStatesOnce)
  {
    LogF("FlushAllQueues");
  }
  #endif
  for (int i=0; i<MaxTriQueues; i++) if (i!=skip)
  {
    if (queue._triUsed[i])
    {
      FlushQueue(queue,i);
    }
    Assert(queue._tri[i]._triangleQueue.Size()==0);
  }
}

void EngineDDT::FreeAllQueues(QueueT &queue)
{
  #if DO_TEX_STATS>=2
  if (LogStatesOnce)
  {
    LogF("FreeAllQueues");
  }
  #endif
  for (int i=0; i<MaxTriQueues; i++)
  {
    if (queue._triUsed[i])
    {
      // simulate flushing - ignore rest of the queue
      queue._tri[i]._triangleQueue.Clear();
      FreeQueue(queue,i);
    }
    else
    {
      Assert(queue._tri[i]._triangleQueue.Size()==0);
    }
    Assert(!queue._triUsed[i]);
    Assert(queue._tri[i]._triangleQueue.Size()==0);
  }
}

void EngineDDT::FlushAndFreeAllQueues(QueueT &queue, bool nonEmptyOnly)
{
  #if DO_TEX_STATS>=2
  if (LogStatesOnce)
  {
    LogF("FlushAndFreeAllQueues");
  }
  #endif
  for (int i=0; i<MaxTriQueues; i++)
  {
    if
    (
      queue._triUsed[i] &&
      (!nonEmptyOnly || queue._tri[i]._triangleQueue.Size()>0)
    )
    {
      FlushAndFreeQueue(queue,i);
    }
    else
    {
      Assert(queue._tri[i]._triangleQueue.Size()==0);
    }
    Assert(nonEmptyOnly || !queue._triUsed[i]);
    Assert(queue._tri[i]._triangleQueue.Size()==0);
  }
  //queue._actTri = -1; // some queue may be allocated
}

void EngineDDT::CloseAllQueues(QueueT &queue)
{
  FlushAndFreeAllQueues(queue);
  queue._usedCounter = 0; // reset counter to avoid overflow
  queue._firstVertex = true; // reset counter to avoid overflow
}

void EngineDDT::FlushQueue(QueueT &queue, int index)
{
  //SwitchTL(false);
  TriQueueT &triq = queue._tri[index];
  int n = triq._triangleQueue.Size();
  if( n>0 )
  {
    if (index==MaxTriQueues-1)
    {
      FlushAllQueues(queue,index); // avoid recursion
    }
    #if _ENABLE_PERFLOG && DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF
      (
        "Flush queue %d:%s %x",
        index,triq._texture ? triq._texture->Name() : "<NULL>",triq._special
      );
    }
    #endif
    EngineShapeProperties prop;
    DoPrepareTriangle(triq._texture, TexMaterialLODInfo(triq._material, 0), triq._level, triq._special, prop);

    // In case of flares set the special pixel shader
    if (triq._material)
    {
      PixelShaderID ps = triq._material->GetPixelShaderID(0);
      if (ps == PSNonTLFlare) SelectPixelShader(ps);
    }

    DoSetMaterial2D();
    DoSelectPixelShader();

    {
#if DIAG_QUEUE
      LogF("Flushing tri queue: base %d, size %d",_queue._queueBase,_queue._queueSize);
#endif
      // set index stream / vertex buffer
      // queue._meshBuffer
      // triq._triangleQueue.Data(),triq._triangleQueue.Size(),

      // Check if the new data fit into the present buffer. If yes then calculate the proper offset. If not then offset starts with zero
      int indexOffset = 0; // Offset in indices
      void *data;
      D3D10_MAP flags;
      if (queue._indexBufferUsed + n <= IndexBufferLength && !queue._firstIndex)
      {
#if LOG_LOCKS
        LogF("Index add D3DLOCK_NOOVERWRITE %d + %d",queue._indexBufferUsed,n);
#endif
        // append data
        indexOffset = queue._indexBufferUsed;
        flags = D3D10_MAP_WRITE_NO_OVERWRITE;
      }
      else
      {
#if LOG_LOCKS
        LogF("Index add D3DLOCK_DISCARD %d + %d",0,n);
#endif
        queue._firstIndex = false;
        // reset index buffer
        indexOffset = 0;
        flags = D3D10_MAP_WRITE_DISCARD;
#if CUSTOM_VB_DISCARD
          if (++_queueNo._actualIBIndex>=NumDynamicVB2D)
          {
            _queueNo._actualIBIndex = 0;
          }
#endif
      }
      
      // Zero vertex and index buffer in the device, forget _iBufferLast and _vBufferLast.
      // The locked buffers cannot be used by device anyway (at least on XBOX) and we will set the buffers anyway right after the unlock
      ForgetDeviceBuffers();

      PROFILE_DX_SCOPE_DETAIL(3dfiC);

      // create index buffer
      HRESULT ret;
      int size = n*sizeof(VertexIndex);
      const ComRef<IDirect3DIndexBuffer9Old> &ibuf = queue.GetIBuffer();
      ret = GEngineDD->Map(ibuf, flags, 0, &data);
      queue._indexBufferUsed = indexOffset+n;
      if (ret==D3D_OK)
      {
        memcpy(&((VertexIndex*)data)[indexOffset], triq._triangleQueue.Data(), size);
        ibuf->Unmap();
      }
      else
      {
        DDError9("iBuffer->Map",ret);
      }

      // Fill out vertex buffer
      const ComRef<IDirect3DVertexBuffer9Old> &vbuf = queue.GetVBuffer();
      SetStreamSource(vbuf, sizeof(TLVertex), 0);

      //_vertexDecl = NULL;

      // Fill out index buffer
      if (_iBufferLast != ibuf)
      {
        _iBufferLast = ibuf;
        _vOffsetLast = 0;
        ADD_COUNTER(d3xIB,1);
        //PROFILE_DX_SCOPE(3dIND);
        DoAssert(sizeof(VertexIndex) == 2);
        _d3DDevice->IASetIndexBuffer(ibuf, DXGI_FORMAT_R16_UINT, 0);
      }

      PROFILE_DX_SCOPE_DETAIL(3dDI2);
      DoAssert(_d3dFrameOpen);
      DrawIndexedPrimitive(
        D3DPT_TRIANGLELIST_OLD,
        0,
        0,
        //triq._queueSize,
        queue._vertexBufferUsed,
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
  //triq._queueBase = queue._meshBase;
  //triq._queueSize = 0;
  Assert(!triq._triangleQueue.Size());
}

void EngineDDT::FlushAndFreeQueue(QueueT &queue, int index)
{
  FlushQueue(queue,index);
  FreeQueue(queue,index);
}

void EngineDDT::Queue2DPoly( const TLVertex *v0, int n )
{

  int addN = (n-2)*3;

  Assert( _queueNo._actTri>=0 );
  Assert (_queueNo._triUsed[_queueNo._actTri]);
  WORD *tgt = QueueAdd(_queueNo,addN);

  //TriQueueT &triq = _queueNo._tri[_queueNo._actTri];
  int offset = _queueNo._meshBase;
  Assert( offset>=0 );
  //saturateMax(triq._queueSize,0+offset+1);
  //saturateMax(triq._queueSize,1+offset+1);

  // add all triangles
  for( int i=2; i<n; i++ )
  {
    *tgt++ = 0+offset;
    *tgt++ = i-1+offset;
    *tgt++ = i+offset;
    //saturateMax(triq._queueSize,i+offset+1);
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

#if RS_DIAGS
HRESULT EngineDDT::D3DSetTextureStageStateName
(
  DWORD stage, D3DTEXTURESTAGESTATETYPE_OLD state, DWORD value, const char *name, bool optimize
)
#else
HRESULT EngineDDT::D3DSetTextureStageState
(
  DWORD stage, D3DTEXTURESTAGESTATETYPE_OLD state, DWORD value, bool optimize
)
#endif
{
  #if DO_COUNTERS
    static OptimizeCounter opt("TextureStageState");
  #endif
  // assume small values of state (in DX6 max. state was about 25)
  AutoArray<TextureStageStateInfo> &stageState=_textureStageState[stage];
  stageState.Access(state);
  TextureStageStateInfo &info=stageState[state];
  if( info.value==value && optimize )
  {
    #if DO_COUNTERS
      opt.Skip();
    #endif
    return D3D_OK;
  }
  info.value=value;
  #if DO_COUNTERS
    opt.Perform();
  #endif
  #if _ENABLE_PERFLOG && DO_TEX_STATS
  if (LogStatesOnce)
  {
    #if RS_DIAGS
    LogF("SetTextureStageState %d,%s,%d",stage,name,value);
    #else
    LogF("SetTextureStageState %d,%d,%d",stage,state,value);
    #endif
  }
  #endif
  #if !NO_SET_TSS
  PROFILE_DX_SCOPE_DETAIL(3dsTS);

  Fail("DX10 TODO");
  return 0;
  // Direct3D 10 NEEDS UPDATE 
//   HRESULT ret = _d3DDevice->SetTextureStageState(stage,state,value);
//   DX_CHECKN("SetTSS",ret);
//   return ret;
  #else
  return 0;
  #endif
}

#if RS_DIAGS
HRESULT EngineDDT::D3DSetSamplerStateName
(
  DWORD stage, D3DSAMPLERSTATETYPE_OLD state, DWORD value, const char *name, bool optimize
)
#else
HRESULT EngineDDT::D3DSetSamplerState
(
  DWORD stage, D3DSAMPLERSTATETYPE_OLD state, DWORD value, bool optimize
)
#endif
{
  #if DO_COUNTERS
    static OptimizeCounter opt("SamplerState");
  #endif
  // assume small values of state (in DX6 max. state was about 25)
  AutoArray<SamplerStateInfo> &samplerState=_samplerState[stage];
  samplerState.Access(state);
  SamplerStateInfo &info=samplerState[state];
  if( info.value==value && optimize )
  {
    #if DO_COUNTERS
      opt.Skip();
    #endif
    return D3D_OK;
  }
  info.value=value;
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
  #if !NO_SET_TSS
  PROFILE_DX_SCOPE_DETAIL(3dsS);
  //ADD_COUNTER(pbSSS,1);

  if (state == D3DSAMP_MINFILTER_OLD) return D3D_OK;
  if (state == D3DSAMP_MAGFILTER_OLD) return D3D_OK;
  if (state == D3DSAMP_MIPFILTER_OLD) return D3D_OK;
  if (state == D3DSAMP_MAXANISOTROPY_OLD) return D3D_OK;
  if (state == D3DSAMP_ADDRESSU_OLD) return D3D_OK;
  if (state == D3DSAMP_ADDRESSV_OLD) return D3D_OK;
  if (state == D3DSAMP_SRGBTEXTURE_OLD) return D3D_OK;

  Fail("DX10 TODO");
  return 0;
  // Direct3D 10 NEEDS UPDATE 
//   HRESULT ret = _d3DDevice->SetSamplerState(stage,state,value);
//   DX_CHECKN("SetSS",ret);
//   return ret;
  #else
  return 0;
  #endif
}

//! Constants that identify occupied bits in dynamic variable identifier
enum DVI
{
  DVIInstancing = 0,
  DVIPointLightsCount = 1,
  DVISpotLightsCount = 3,
  DVIPixelShaderType = 5,
  DVIAlphaShadowEnabled = 8,
  DVIRenderingMode = 9,
  DVISectionMaterialLODs = 11,
};

int EngineDDT::GetDynamicVariablesIdentifier(
  bool instancing, const SectionMaterialLODs &matLOD, int begSec, int endSec
) const
{
  // Common variables
  int temp;
  int result = 0;

  temp = instancing ? 1 : 0;
  result |= temp << DVIInstancing;

  DoAssert(_pointLightsCount < 4);
  temp = _pointLightsCount;
  result |= temp << DVIPointLightsCount;

  DoAssert(_spotLightsCount < 4);
  temp = _spotLightsCount;
  result |= temp << DVISpotLightsCount;

  DoAssert(_pixelShaderTypeSel < 8);
  temp = _pixelShaderTypeSel;
  result |= temp << DVIPixelShaderType;

  temp = _alphaShadowEnabled ? 1 : 0;
  result |= temp << DVIAlphaShadowEnabled;

  DoAssert(_renderingMode < 4);
  temp = _renderingMode;
  result |= temp << DVIRenderingMode;

  // Material LODs
  if (endSec < 0)
  {
    endSec = matLOD.Size();
    begSec = 0;
  }
  Assert(begSec>=0 && begSec<=matLOD.Size());
  Assert(endSec>=0 && endSec<=matLOD.Size());
  Assert(endSec>=begSec);

  // do not encode more than 16 sections
  saturateMin(endSec,begSec+16);

  temp = 0;
  for (int i = begSec; i < endSec; i++)
  {
    DoAssert(matLOD[i] <= 1);
    if (matLOD[i] == 1) temp |= 1 << i;
  }
  result |= temp << DVISectionMaterialLODs;

  return result;
}

static const D3DXMATRIX D3DMatIdentity
(
    1,0,0, 0,
    0,1,0, 0,
    0,0,1, 0,
    0,0,0, 1
);

static const D3DMATRIX D3DMatZero =
{
    0,0,0, 0,
    0,0,0, 0,
    0,0,0, 0,
    0,0,0, 0,
};

/*!
  \patch 5106 Date 12/20/2006 by Flyman
  - Fixed: Filtering was not working properly when using pushbuffers (square artefact on ground were visible under certain conditions)
*/
void EngineDDT::SetHWToUndefinedState()
{
  // Various HW states
  {
    // Render states
    for (int i = 0; i < _renderState.Size(); i++)
    {
      // Skip some render states (let them keep their value)
      switch (i)
      {
      case D3DRS_SRGBWRITEENABLE_OLD:
      case D3DRS_FOGCOLOR_OLD:
      case D3DRS_STENCILMASK_OLD:
        continue;
      }

      // Set render states to undefined state
      _renderState[i].value = -1;
    }

    // Texture stage states all levels
    for (int j = 0; j < MaxStages; j++)
    {
      for (int i = 0; i < _textureStageState[j].Size(); i++)
      {
//         // Skip some render states (let them keep their value)
//         switch (i)
//         {
//         case D3DTSS_TEXCOORDINDEX_OLD:
//         case D3DTSS_COLOROP_OLD:
//         case D3DTSS_ALPHAOP_OLD:
//           continue;
//         }

        // Set render states to undefined state
        _textureStageState[j][i].value = -1;
      }
    }

    // Sampler stages all levels apart of last one
    for (int j = 0; j < TEXID_FIRST; j++)
    {
      for (int i = 0; i < _samplerState[j].Size(); i++)
      {
        // Skip some render states (let them keep their value)
//         if (j > 0)
//         {
//           switch (i)
//           {
//           case D3DSAMP_MAGFILTER_OLD:
//           case D3DSAMP_MINFILTER_OLD:
//           case D3DSAMP_MIPFILTER_OLD:
//             continue;
//           }
//         }
//         switch (i)
//         {
//         case D3DSAMP_MAXANISOTROPY_OLD:
//           continue;
//         }

        // Set render states to undefined state
        _samplerState[j][i].value = -1;
      }
    }
  }

  // Set tex0 max and avg colors to impossible values
  _texture0MaxColor = Color(-1,-1,-1,-1);
  _texture0AvgColor = Color(-1,-1,-1,-1);

  // Textures to NULL
  for (int i = 0; i < TEXID_FIRST; i++) SetTexture(i, NULL);

  // Vertex declaration
  _vertexDeclLast = NULL;

  // Vertex shader
  _vertexShaderLast._shader = NULL;
  _vertexShaderLast._inputLayout = NULL;

  // Pixel shader
  _pixelShaderLast = NULL;

  // Bias
  _bias = INT_MAX;

  // Last stuff set to undefined
  PBDrawingCleanup();

  // Reset vertex and pixel shader constants
  ResetShaderConstantValues();
}

void EngineDDT::PBDrawingCleanup()
{
  // High-level last stuff set to undefined (the previous PB playing might have
  // changed the low-level and the high-level remained unchanged)
  _lastSpec = -1;
  _lastMat = TexMaterialLODInfo((TexMaterial *)-1, 0);
  _lastRenderingMode = (RenderingMode)-1;
  _texGenScaleOrOffsetHasChanged = true;
  _currentShadowStateSettings = SS_None;
}

void EngineDDT::BeginPBCreation(PushBuffer *pb)
{
  // Inform the engine the PB is being created
  DoAssert(!_pbIsBeingCreated);
  _pbIsBeingCreated = true;
  PushBufferD3DT *pbd3d = static_cast<PushBufferD3DT*>(pb);
  _pushBuffer = pbd3d->GetPBItemList();
}

void EngineDDT::EndPBCreation()
{
  // Something should be in the pushbuffer now
  //DoAssert(_pushBuffer->Size() > 0);

  // Inform the engine the creation of PB was finished
  _pbIsBeingCreated = false;
  _pushBuffer = NULL;
}

void EngineDDT::CreateShader(QOStrStream &hlslSource, CONST D3D10_SHADER_MACRO *pDefines,
                             LPCSTR pFunctionName, LPCSTR pProfile, DWORD flags, const InputElements &ie,
                             VertexShaderInfo &vertexShader)
{
  // Compile shader
  ComRef<ID3D10Blob> shader;
  ComRef<ID3D10Blob> errorMsgs;
  HRESULT hr = D3D10CompileShader(hlslSource.str(), hlslSource.pcount(), NULL, pDefines, &_hlslInclude, pFunctionName, pProfile, flags, shader.Init(), errorMsgs.Init());

  // Create shader and save it
  if (FAILED(hr))
  {
    LogF("Error: %x in D3DXCompileShader while compiling the %s vertex shader", hr, pFunctionName);
    if (errorMsgs.NotNull())
    {
      LogF("%s", (const char*)errorMsgs->GetBufferPointer());
      ErrorMessage("Error compiling vertex shader %s", pFunctionName);
    }
  }
  else
  {
    const DWORD *vsData;
    vsData = (const DWORD *)shader->GetBufferPointer();
    DWORD vsDataSize = shader->GetBufferSize();
    ComRef<ID3D10VertexShader> vs;
    HRESULT hr = _d3DDevice->CreateVertexShader(vsData, vsDataSize, vs.Init());
    if (FAILED(hr) || vs.IsNull())
    {
      ErrF("Cannot create vertex shader %s, error %x", pFunctionName, hr);
    }
    else
    {
      vertexShader._shader = vs;
      if (FAILED(_d3DDevice->CreateInputLayout(ie._ied, ie._size, shader->GetBufferPointer(), shader->GetBufferSize(), vertexShader._inputLayout.Init())))
      {
        RptF("Error: Creating input layout failed");
      }
      //vertexShader._shaderBytecode = shader;

//       ComRef<ID3DXBuffer> disass;
//       D3DXDisassembleShader(vsData, false, NULL, disass.Init());
//       FILE* f = fopen("disassemble.txt", "w");
//       fwrite(disass->GetBufferPointer(), 1, disass->GetBufferSize(), f);
//       fclose(f);
    }
  }
}

void EngineDDT::D3DSetSamplerStateR(DWORD stage, D3DSAMPLERSTATETYPE_OLD state, DWORD value)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    AutoArray<SamplerStateInfo> &samplerState = _samplerState[stage];
    samplerState.Access(state);
    SamplerStateInfo &info = samplerState[state];
    if (info.value != value)
    {
      _pushBuffer->Add(new PBISetSamplerStateT(stage, state, value));
    }
  }
//  else
#endif
  {
    D3DSetSamplerState(stage, state, value);
  }
}

void EngineDDT::D3DSetRenderStateR(D3DRENDERSTATETYPE_OLD state, DWORD value)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    AutoArray<RenderStateInfo> &renderState = _renderState;
    renderState.Access(state);
    RenderStateInfo &info = renderState[state];
    if (info.value != value)
    {
      _pushBuffer->Add(new PBISetRenderStateT(state, value));
    }
  }
//  else
#endif
  {
    D3DSetRenderState(state, value);
  }
}

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

void EngineDDT::SetVertexShaderConstantR(int startRegister, const float *data)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    DoAssert(sizeof(float)==sizeof(int));
    if (MemCmp32(&_shaderConstValues[startRegister][0], data, 1 * 4) != 0)
    {
      _pushBuffer->Add(new PBISetVertexShaderConstantT(startRegister, data));
    }
  }
//  else
#endif
  {
    SetVertexShaderConstantF(startRegister, data, 1);
  }
}

void EngineDDT::SetPixelShaderConstantR(int startRegister, const float *data)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    DoAssert(sizeof(float)==sizeof(int));
    if (MemCmp32(&_pixelShaderConstValues[startRegister][0], data, 1 * 4) != 0)
    {
      _pushBuffer->Add(new PBISetPixelShaderConstantT(startRegister, data));
    }
  }
//  else
#endif
  {
    SetPixelShaderConstantF(startRegister, data, 1);
  }
}

void EngineDDT::SetVertexShaderConstant2R(int startRegister, const float *data)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    DoAssert(sizeof(float)==sizeof(int));
    if (MemCmp32(&_shaderConstValues[startRegister][0], data, 2 * 4) != 0)
    {
      _pushBuffer->Add(new PBISetVertexShaderConstant2T(startRegister, data));
    }
  }
//  else
#endif
  {
    SetVertexShaderConstantF(startRegister, data, 2);
  }
}

void EngineDDT::SetVertexShaderConstantBR(int startRegister, const bool *data)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetVertexShaderConstantBT(startRegister, data));
  }
  //  else
#endif
  {
    SetVertexShaderConstantB(startRegister, data, 1);
  }
}

void EngineDDT::SetVertexShaderConstantB2R(int startRegister, const bool *data)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetVertexShaderConstantB2T(startRegister, data));
  }
  //  else
#endif
  {
    SetVertexShaderConstantB(startRegister, data, 2);
  }
}

void EngineDDT::SetVertexShaderConstantIR(int startRegister, const int *data)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetVertexShaderConstantIT(startRegister, data));
  }
  //  else
#endif
  {
    SetVertexShaderConstantI(startRegister, data, 1);
  }
}

void EngineDDT::SetVertexShaderConstantMaxColorR()
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetVertexShaderConstantMaxColorT(_texture0MaxColorSeparate));
  }
//  else
#endif
  {
    PBISetVertexShaderConstantMaxColorT pbiSetVertexShaderConstantMaxColor(_texture0MaxColorSeparate);
    PBContext pbc;
    pbiSetVertexShaderConstantMaxColor.Do(pbc);
  }
}

void EngineDDT::SetVertexShaderConstantMLSunR(Color matAmbient, Color matDiffuse, Color matForcedDiffuse, Color matSpecular, Color matEmmisive, bool vsIsAS)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetVertexShaderConstantMLSunT(matAmbient, matDiffuse, matForcedDiffuse, matSpecular, matEmmisive, vsIsAS));
  }
  //  else
#endif
  {
    PBISetVertexShaderConstantMLSunT pbiSetVertexShaderConstantMLSun(matAmbient, matDiffuse, matForcedDiffuse, matSpecular, matEmmisive, vsIsAS);
    PBContext pbc;
    pbiSetVertexShaderConstantMLSun.Do(pbc);
  }
}

void EngineDDT::SetVertexShaderConstantMLNoneR(Color matEmmisive, bool vsIsAS)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetVertexShaderConstantMLNoneT(matEmmisive, vsIsAS));
  }
  //  else
#endif
  {
    PBISetVertexShaderConstantMLNoneT pbiSetVertexShaderConstantMLNone(matEmmisive, vsIsAS);
    PBContext pbc;
    pbiSetVertexShaderConstantMLNone.Do(pbc);
  }
}

void EngineDDT::SetVertexShaderConstantSAFRR(float matPower, Color matAmbient)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetVertexShaderConstantSAFRT(matPower, matAmbient));
  }
  //  else
#endif
  {
    PBISetVertexShaderConstantSAFRT pbiSetVertexShaderConstantSAFR(matPower, matAmbient);
    PBContext pbc;
    pbiSetVertexShaderConstantSAFR.Do(pbc);
  }
}

void EngineDDT::SetVertexShaderConstantFFARR()
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetVertexShaderConstantFFART);
  }
  //  else
#endif
  {
    PBISetVertexShaderConstantFFART pbiSetVertexShaderConstantFFAR;
    PBContext pbc;
    pbiSetVertexShaderConstantFFAR.Do(pbc);
  }
}

void EngineDDT::SetVertexShaderConstantLWSMR()
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetVertexShaderConstantLWSMT);
  }
  //  else
#endif
  {
    PBISetVertexShaderConstantLWSMT pbiSetVertexShaderConstantLWSM;
    PBContext pbc;
    pbiSetVertexShaderConstantLWSM.Do(pbc);
  }
}

void EngineDDT::SetTextureR(DWORD stage, const TextureD3DT *texture)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetTextureT(stage, texture));
  }
  //else
#endif
  {
    PBISetTextureT pbiSetTexture(stage, texture);
    PBContext pbc;
    pbiSetTexture.Do(pbc);
  }
}

void EngineDDT::SetTexture0R(const TextureD3DT *texture)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetTexture0T(texture));
  }
  //else
#endif
  {
    PBISetTexture0T pbiSetTexture0(texture);
    PBContext pbc;
    pbiSetTexture0.Do(pbc);
  }
}

void EngineDDT::SetPixelShaderR(const ComRef<ID3D10PixelShader> &ps)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    if (_pixelShaderLast != ps)
    {
      _pushBuffer->Add(new PBISetPixelShaderT(ps));
    }
  }
  //else
#endif
  {
    SetPixelShader(ps);
  }
}

void EngineDDT::SetPixelShaderConstantDBSR(bool texture0MaxColorSeparate, ColorVal matDiffuse, ColorVal matForcedDiffuse, ColorVal matSpecular, float matPower)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetPixelShaderConstantDBST(texture0MaxColorSeparate, matDiffuse, matForcedDiffuse, matSpecular, matPower));
  }
  //else
#endif
  {
    PBISetPixelShaderConstantDBST pbiSetPixelShaderConstantDBS(texture0MaxColorSeparate, matDiffuse, matForcedDiffuse, matSpecular, matPower);
    PBContext pbc;
    pbiSetPixelShaderConstantDBS.Do(pbc);
  }
}

void EngineDDT::SetPixelShaderConstantDBSZeroR()
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetPixelShaderConstantDBSZeroT());
  }
  //else
#endif
  {
    PBISetPixelShaderConstantDBSZeroT pbiSetPixelShaderConstantDBSZero;
    PBContext pbc;
    pbiSetPixelShaderConstantDBSZero.Do(pbc);
  }
}

void EngineDDT::SetPixelShaderConstant_A_DFORCED_E_SunR(bool texture0MaxColorSeparate, ColorVal matEmmisive, ColorVal matAmbient, ColorVal matForcedDiffuse)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetPixelShaderConstant_A_DFORCED_E_SunT(texture0MaxColorSeparate, matEmmisive, matAmbient, matForcedDiffuse));
  }
  //else
#endif
  {
    PBISetPixelShaderConstant_A_DFORCED_E_SunT pbiSetPixelShaderConstant_A_DFORCED_E_Sun(texture0MaxColorSeparate, matEmmisive, matAmbient, matForcedDiffuse);
    PBContext pbc;
    pbiSetPixelShaderConstant_A_DFORCED_E_Sun.Do(pbc);
  }
}

void EngineDDT::SetPixelShaderConstant_A_DFORCED_E_NoSunR(ColorVal matEmmisive)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetPixelShaderConstant_A_DFORCED_E_NoSunT(matEmmisive));
  }
  //else
#endif
  {
    PBISetPixelShaderConstant_A_DFORCED_E_NoSunT pbiSetPixelShaderConstant_A_DFORCED_E_NoSun(matEmmisive);
    PBContext pbc;
    pbiSetPixelShaderConstant_A_DFORCED_E_NoSun.Do(pbc);
  }
}

void EngineDDT::SetPixelShaderConstantWaterR()
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetPixelShaderConstantWaterT());
  }
  //else
#endif
  {
    PBISetPixelShaderConstantWaterT pbiSetPixelShaderConstantWater;
    PBContext pbc;
    pbiSetPixelShaderConstantWater.Do(pbc);
  }
}

void EngineDDT::SetPixelShaderConstantWaterSimpleR()
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetPixelShaderConstantWaterSimpleT());
  }
  //else
#endif
  {
    PBISetPixelShaderConstantWaterSimpleT pbiSetPixelShaderConstantWaterSimple;
    PBContext pbc;
    pbiSetPixelShaderConstantWaterSimple.Do(pbc);
  }
}

void EngineDDT::SetPixelShaderConstantGlassR(ColorVal matSpecular)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBISetPixelShaderConstantGlassT(matSpecular));
  }
  //else
#endif
  {
    PBISetPixelShaderConstantGlassT pbiSetPixelShaderConstantGlass(matSpecular);
    PBContext pbc;
    pbiSetPixelShaderConstantGlass.Do(pbc);
  }
}

void EngineDDT::SetVertexShaderR(const VertexShaderInfo &vs)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    if (_vertexShaderLast._shader != vs._shader)
    {
      _pushBuffer->Add(new PBISetVertexShaderT(vs));
    }
  }
  //else
#endif
  {
    SetVertexShader(vs);
  }
}

void EngineDDT::SetVertexDeclarationR(const ComRef<ID3D10InputLayout> &decl)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    if (_vertexDeclLast != decl)
    {
      _pushBuffer->Add(new PBISetVertexDeclarationT(decl));
    }
  }
  //else
#endif
  {
    SetVertexDeclaration(decl);
  }
}

void EngineDDT::DrawIndexedPrimitiveR(int beg, int end, const Shape &sMesh, int instancesOffset, int instancesCount)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    _pushBuffer->Add(new PBIDrawIndexedPrimitiveT(beg, end));
  }
  //else
#endif
  {
    PBIDrawIndexedPrimitiveT pbiDrawIndexedPrimitive(beg, end);
    PBContext pbc;
    pbc._instancesOffset = instancesOffset;
    pbc._instancesCount = instancesCount;
    pbc._sMesh = &sMesh;
    pbiDrawIndexedPrimitive.Do(pbc);
  }
}

void EngineDDT::SetBiasR(int bias)
{
#if USE_PUSHBUFFERS
  if (_pbIsBeingCreated)
  {
    if (_bias != bias)
    {
      _pushBuffer->Add(new PBISetBiasT(bias));
    }
  }
  //else
#endif
  {
    SetBias(bias);
  }
}

void EngineDDT::ForgetDeviceBuffers()
{
  // Zero index buffer
  if (_iBufferLast.NotNull())
  {
    _d3DDevice->IASetIndexBuffer(buf._iBuffer, DXGI_FORMAT_R16_UINT, 0);
    _iBufferLast = NULL;
  }

  // Zero vertex buffer
  ComRef<IDirect3DVertexBuffer9> empty;
  SetStreamSource(empty, 0, 0);
}

void EngineDDT::EnableCustomTexGenZero(bool optimize)
{
  // Set the _uvSource and _uvSourceCount. Note that this must be done prior
  // EnableCustomTexGen because it uses the result
  _uvSource[0] = UVTex;
  _uvSourceCount = 1;
  _uvSourceOnlyTex0 = true;

  // Set the texgen matrices
  // TODO: why 3? 1 should be enough
  const D3DXMATRIX *matrix[3] = {NULL,NULL,NULL};
  EnableCustomTexGen(matrix, 3, optimize);
}

void EngineDDT::EnableCustomTexGen(
  const D3DXMATRIX **matrix, int nMatrices, bool optimize
)
{
  if (!_tlActive) return;

  // texGenScaleOrOffset is going to be used
  _texGenScaleOrOffsetHasChanged = false;

  // only uv source is currently supported
  #if DO_TEX_STATS
  if (LogStatesOnce)
  {
    //LogF("Set tex gen %.1f,%.1f",matrix0 ? matrix0->_11 : 0.0f,matrix1 ? matrix1->_11 : 0.0f );
  }
  #endif
  static const int constIndex[MaxStages]=
  {
    VSC_TexTransform + 2 * 0,
    VSC_TexTransform + 2 * 1,
    VSC_TexTransform + 2 * 2,
    VSC_TexTransform + 2 * 3,
    VSC_TexTransform + 2 * 4,
    VSC_TexTransform + 2 * 5,
    VSC_TexTransform + 2 * 6,
    VSC_TexTransform + 2 * 7
  };
  for (int i = 0; i < nMatrices; i++)
  {
    // Get either source matrix or identity
    const D3DXMATRIX *pMatrix = matrix[i];
    if (pMatrix == NULL) pMatrix = &D3DMatIdentity;

    // If UV source is specified for given i, then include the UV decompression matrix
    D3DXMATRIX scaledMatrix;
    if (i < _uvSourceCount)
    {
      int uvSetIndex = -1;
      switch (_uvSource[i])
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
        D3DXMATRIX sao = D3DMatIdentity; // Scale And Offset matrix
        sao._11 = _texGenScale[uvSetIndex].u * (1.0f / (UVCompressedMax - UVCompressedMin));
        sao._22 = _texGenScale[uvSetIndex].v * (1.0f / (UVCompressedMax - UVCompressedMin));
        sao._14 = _texGenOffset[uvSetIndex].u - UVCompressedMin / float(UVCompressedMax - UVCompressedMin) * _texGenScale[uvSetIndex].u;
        sao._24 = _texGenOffset[uvSetIndex].v - UVCompressedMin / float(UVCompressedMax - UVCompressedMin) * _texGenScale[uvSetIndex].v;
        scaledMatrix = (*pMatrix) * sao;
        pMatrix = &scaledMatrix;
      }
    }

    // Set the texgen matrix
    SetVertexShaderConstant2R(constIndex[i], (float*)pMatrix);
  }
}

void EngineDDT::PrepareSingleTexDiffuseA()
{
  EnableCustomTexGenZero(true);
  SelectPixelShader(PSNormal);
}

void EngineDDT::PrepareDetailTex(const TexMaterialLODInfo &mat, const EngineShapeProperties &prop)
{
  const TexMaterial *texMat = mat._mat;
  if (texMat)
  {
    texMat->Load();
    D3DXMATRIX matrix[MaxStages];
    const D3DXMATRIX *pMatrix[MaxStages];
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
        ConvertMatrixTransposedT(matrix[0], texMat->_texGen[0]._uvTransform);
        pMatrix[0] = &matrix[0];
        break;
    }
    
    // Handle UV animated texture
    Matrix4 uvMatrix0;
    Matrix4 *puvMatrix0 = texMat->GetUVMatrix0(uvMatrix0);
    if (puvMatrix0)
    {
      ConvertMatrixTransposedT(matrix[0], *puvMatrix0);

      // Move the UV offset from the 3-th place (direction) to the 4-th place (position)
//      matrix[0]._14 = matrix[0]._13;
//      matrix[0]._24 = matrix[0]._23;
#if _ENABLE_REPORT
      if ((matrix[0]._14 != matrix[0]._13) || (matrix[0]._24 != matrix[0]._23))
      {
        RptF("Warning: 3-th row differs from 4-th row in stage 0 of material %s. (position shift is determined by 4-th row now)", texMat->GetName()._name.Data());
      }
#endif
      matrix[0]._13 = 0.0f;
      matrix[0]._23 = 0.0f;

      // if we are overriding, strange things can happen
      pMatrix[0] = &matrix[0];
    }

    for (int stage=1; stage<texMat->_nTexGen; stage++)
    {
      if (stage >= MaxStages) break;
      //D3DXMATRIX &mat = matrix[stage];
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
            ConvertMatrixTransposedT(matrix[0], m0);
            ConvertMatrixTransposedT(matrix[1], m1);
            ConvertMatrixTransposedT(matrix[2], m2);
            ConvertMatrixTransposedT(matrix[3], m3);
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
          ConvertMatrixTransposedT(matrix[stage], texMat->_texGen[stage]._uvTransform);
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
        Matrix4 uvMatrix;
        Matrix4 *puvMatrix = texMat->GetUVMatrix0(uvMatrix);
        if (puvMatrix)
        {
          ConvertMatrixTransposedT(matrix[stage], *puvMatrix);

          // Move the UV offset from the 3-th place (direction) to the 4-th place (position)
//          matrix[stage]._14 = matrix[stage]._13;
//          matrix[stage]._24 = matrix[stage]._23;
#if _ENABLE_REPORT
          if ((matrix[stage]._14 != matrix[stage]._13) || (matrix[stage]._24 != matrix[stage]._23))
          {
            RptF("Warning: 3-th row differs from 4-th row in stage %d of material %s. (position shift is determined by 4-th row now)", stage, texMat->GetName()._name.Data());
          }
#endif
          matrix[stage]._13 = 0.0f;
          matrix[stage]._23 = 0.0f;

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
        _uvSource[0] = UVTex;
      }
      else
      {
        _uvSource[0] = texMat->_texGen[0]._uvSource;
      }

      // Fill out the VU sources, determine the last that is not UVNone (this will be used to calculate the _uvSourceCount)
      int lastNotNone = 0;
      bool onlyTex0 = true;
      for (int i = 1; i < nStages; i++)
      {
        UVSource src = texMat->_texGen[i]._uvSource;
        _uvSource[i] = src;
        if (src != UVNone && src!=UVPos) lastNotNone = i;
        if (src == UVTex1) onlyTex0 = false;
      }
      _uvSourceCount = lastNotNone + 1;
      _uvSourceOnlyTex0 = onlyTex0;
    }

    // Set the texgen matrices
    EnableCustomTexGen(pMatrix, nMatrices, true);

    SelectPixelShader(texMat->GetPixelShaderID(mat._level));

    return;
  }

  PrepareSingleTexDiffuseA();
}

void EngineDDT::SetShadowLevelNeeded(int value)
{
  saturate(value, 0, 3);
  int newValue = value * STENCIL_SHADOW_LEVEL_STEP;
  if (_shadowLevelNeeded != newValue)
  {
    _shadowLevelNeeded = newValue;
    // set impossible value - force shadow state setting
    _currentShadowStateSettings = SS_None;
  }
}

void EngineDDT::SetShadowLevel(int value)
{
  saturate(value, 0, 3);
  int newValue = value * STENCIL_SHADOW_LEVEL_STEP;
  // set impossible value - force shadow state setting
  if (_shadowLevel != newValue)
  {
    _shadowLevel = newValue;
    // set impossible value - force shadow state setting
    _currentShadowStateSettings = SS_None;
  }
}

void EngineDDT::DoShadowsStatesSettings(EShadowState ss, bool noBackCull)
{
  _currentShadowStateSettings = ss;
  _backCullDisabled = noBackCull;
  switch(ss)
  {
  case SS_Transparent:
    D3DSetRenderStateR(D3DRS_STENCILPASS_OLD, D3DSTENCILOP_KEEP_OLD);
    D3DSetRenderStateR(D3DRS_STENCILWRITEMASK_OLD, STENCIL_FULL);
    //if (!_recording)
    {
      D3DSetRenderStateR(D3DRS_STENCILREF_OLD, _shadowLevel/* | _aisValue*/);
    }
    D3DSetRenderStateR(D3DRS_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
    D3DSetRenderStateR(D3DRS_CULLMODE_OLD, noBackCull ? D3DCULL_NONE_OLD : D3DCULL_CCW_OLD) ;
    if (_canTwoSidedStencil)
    {
      D3DSetRenderStateR(D3DRS_TWOSIDEDSTENCILMODE_OLD, FALSE);
    }
    break;
  case SS_Receiver:
    // when drawing object on top of shadow, reset the shadow
    D3DSetRenderStateR(D3DRS_STENCILPASS_OLD, D3DSTENCILOP_REPLACE_OLD);
    D3DSetRenderStateR(D3DRS_STENCILWRITEMASK_OLD, STENCIL_FULL);
    //if (!_recording)
    {
      D3DSetRenderStateR(D3DRS_STENCILREF_OLD, _shadowLevel/* | _aisValue*/);
    }
    D3DSetRenderStateR(D3DRS_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
    D3DSetRenderStateR(D3DRS_CULLMODE_OLD, noBackCull ? D3DCULL_NONE_OLD : D3DCULL_CCW_OLD);
    if (_canTwoSidedStencil)
    {
      D3DSetRenderStateR(D3DRS_TWOSIDEDSTENCILMODE_OLD, FALSE);
    }
    break;
  case SS_Disabled:
    D3DSetRenderStateR(D3DRS_STENCILPASS_OLD, D3DSTENCILOP_REPLACE_OLD);
    D3DSetRenderStateR(D3DRS_STENCILWRITEMASK_OLD, STENCIL_FULL);
    //DoAssert(!_recording);
    D3DSetRenderStateR(D3DRS_STENCILREF_OLD, _shadowLevelNeeded/* | _aisValue*/);
    D3DSetRenderStateR(D3DRS_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
    D3DSetRenderStateR(D3DRS_CULLMODE_OLD, noBackCull ? D3DCULL_NONE_OLD : D3DCULL_CCW_OLD);
    if (_canTwoSidedStencil)
    {
      D3DSetRenderStateR(D3DRS_TWOSIDEDSTENCILMODE_OLD, FALSE);
    }
    break;
  case SS_Ordinary:
    D3DSetRenderStateR(D3DRS_STENCILPASS_OLD, D3DSTENCILOP_REPLACE_OLD);
    //DoAssert(!_recording);
    // no comparison - we use ref value only for replace
    // set STENCIL_COUNTER to make sure shadow is visualized
    // set STENCIL_PROJ_SHADOW so that shadow can be distinguished
    D3DSetRenderStateR(D3DRS_STENCILREF_OLD, STENCIL_PROJ_SHADOW);
    D3DSetRenderStateR(D3DRS_STENCILWRITEMASK_OLD, STENCIL_PROJ_SHADOW|STENCIL_COUNTER);
    D3DSetRenderStateR(D3DRS_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
    D3DSetRenderStateR(D3DRS_CULLMODE_OLD, D3DCULL_CCW_OLD);
    if (_canTwoSidedStencil)
    {
      D3DSetRenderStateR(D3DRS_TWOSIDEDSTENCILMODE_OLD, FALSE);
    }
    break;
  case SS_Volume_Front:
    //DoAssert(!_recording);
    D3DSetRenderStateR(D3DRS_STENCILREF_OLD, _shadowLevelNeeded);
    D3DSetRenderStateR(D3DRS_STENCILWRITEMASK_OLD, STENCIL_COUNTER);
    // if reference<=stencilbuffer value, pass
    D3DSetRenderStateR(D3DRS_STENCILFUNC_OLD, D3DCMP_LESSEQUAL_OLD);
    Assert (!_canTwoSidedStencil);
    D3DSetRenderStateR(D3DRS_CULLMODE_OLD, D3DCULL_CCW_OLD);
    D3DSetRenderStateR(D3DRS_STENCILPASS_OLD, D3DSTENCILOP_DECR_OLD);
    break;
  case SS_Volume_Back:
    // this should never be used with two-sided stencil
    D3DSetRenderStateR(D3DRS_STENCILREF_OLD, _shadowLevelNeeded);
    D3DSetRenderStateR(D3DRS_STENCILWRITEMASK_OLD, STENCIL_COUNTER);
    D3DSetRenderStateR(D3DRS_STENCILFUNC_OLD, D3DCMP_LESSEQUAL_OLD);
    D3DSetRenderStateR(D3DRS_CULLMODE_OLD, D3DCULL_CW_OLD);

    if(_canTwoSidedStencil)
    {
      D3DSetRenderStateR(D3DRS_CULLMODE_OLD, D3DCULL_NONE_OLD);
      D3DSetRenderStateR(D3DRS_TWOSIDEDSTENCILMODE_OLD, TRUE);
      D3DSetRenderStateR(D3DRS_CCW_STENCILFUNC_OLD, D3DCMP_LESSEQUAL_OLD);
      D3DSetRenderStateR(D3DRS_STENCILPASS_OLD, D3DSTENCILOP_INCR_OLD);
      D3DSetRenderStateR(D3DRS_CCW_STENCILPASS_OLD, D3DSTENCILOP_DECR_OLD);
      // 
      // D3DRS_CCW_STENCILFAIL
      // D3DRS_CCW_STENCILZFAIL
    }
    else
    {
      D3DSetRenderStateR(D3DRS_STENCILPASS_OLD, D3DSTENCILOP_INCR_OLD);
      D3DSetRenderStateR(D3DRS_CULLMODE_OLD, D3DCULL_CW_OLD);
    }

    break;
  case SS_ShadowBuffer:
    D3DSetRenderStateR(D3DRS_STENCILPASS_OLD, D3DSTENCILOP_REPLACE_OLD);
    D3DSetRenderStateR(D3DRS_STENCILWRITEMASK_OLD, STENCIL_FULL);
    D3DSetRenderStateR(D3DRS_STENCILREF_OLD, _shadowLevelNeeded);
    D3DSetRenderStateR(D3DRS_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD);
    D3DSetRenderStateR(D3DRS_CULLMODE_OLD, D3DCULL_CCW_OLD);
    if (_canTwoSidedStencil)
    {
      D3DSetRenderStateR(D3DRS_TWOSIDEDSTENCILMODE_OLD, FALSE);
    }
    break;
  default:
    LogF("Error: Unknown shadow state");
  }
}

void EngineDDT::SetMultiTexturing(TextureD3DT **tex, int count, int keepUntouchedLimit)
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
      if (_canReadSRGB)
      {
        D3DSetSamplerStateR(i+1, D3DSAMP_SRGBTEXTURE_OLD, srgb && _linearSpace);
      }
    }

    // Set the specified texture
    SetTextureR(i + 1, tex[i]);
  }

  // Set the rest to NULL
  int maxUntouchedStageCount = min(keepUntouchedLimit, TEXID_FIRST) - 1;
  for (int i = count; i < maxUntouchedStageCount; i++)
  {
    // Perform quick rejection
    if (_lastHandle[i + 1])
    {
      SetTextureR(i + 1, NULL);
    }
  }
}

Vector3 EngineDDT::TransformPositionToObjectSpace(Vector3Par position)
{
  return _invWorld * position;
}

Vector3 EngineDDT::TransformAndNormalizePositionToObjectSpace(Vector3Par position)
{
  // Get the original distance
  float positionToModelDistance = position.Distance(_world.Position());

  // Transform the position
  Vector3 newPosition = _invWorld * position;

  // Normalize the position
  if (newPosition.SquareSize()>0)
  {
    float coef = positionToModelDistance * newPosition.InvSize();
    newPosition = newPosition * coef;
  }

  return newPosition;
}

Vector3 EngineDDT::TransformAndNormalizeVectorToObjectSpace(Vector3Par vector)
{
  Vector3 newVector = _invWorld.Orientation() * vector;
  //Vector3 newVector = _view.Orientation() * vector;
  return newVector.Normalized();
}

void EngineDDT::SetFiltering(int stage, TexFilter filter, int maxAFLevel)
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
        D3DSetSamplerStateR(stage,D3DSAMP_MINFILTER_OLD,D3DTEXF_ANISOTROPIC_OLD);
        D3DSetSamplerStateR(stage,D3DSAMP_MAGFILTER_OLD,D3DTEXF_LINEAR_OLD); 
        D3DSetSamplerStateR(stage,D3DSAMP_MIPFILTER_OLD,D3DTEXF_LINEAR_OLD);
        D3DSetSamplerStateR(stage,D3DSAMP_MAXANISOTROPY_OLD,maxAFLevel);
        break;
      }
    // when AF disabled, fall-through
    case TFTrilinear:
      D3DSetSamplerStateR(stage,D3DSAMP_MINFILTER_OLD,D3DTEXF_LINEAR_OLD);
      D3DSetSamplerStateR(stage,D3DSAMP_MAGFILTER_OLD,D3DTEXF_LINEAR_OLD); 
      D3DSetSamplerStateR(stage,D3DSAMP_MIPFILTER_OLD,D3DTEXF_LINEAR_OLD);
      break;
    case TFLinear:
      D3DSetSamplerStateR(stage,D3DSAMP_MINFILTER_OLD,D3DTEXF_LINEAR_OLD);
      D3DSetSamplerStateR(stage,D3DSAMP_MAGFILTER_OLD,D3DTEXF_LINEAR_OLD); 
      D3DSetSamplerStateR(stage,D3DSAMP_MIPFILTER_OLD,D3DTEXF_POINT_OLD);
      break;
    case TFPoint:
      D3DSetSamplerStateR(stage,D3DSAMP_MINFILTER_OLD,D3DTEXF_POINT_OLD);
      D3DSetSamplerStateR(stage,D3DSAMP_MAGFILTER_OLD,D3DTEXF_POINT_OLD);  
      D3DSetSamplerStateR(stage,D3DSAMP_MIPFILTER_OLD,D3DTEXF_POINT_OLD);
      break;
  }
}

static float ColorDistance2(ColorVal c1, ColorVal c2)
{
  return Square(c1.R()-c2.R())+Square(c1.G()-c2.G())+Square(c1.B()-c2.B())+Square(c1.A()-c2.A());
}

/// conversion table SRGB (float, ~12b precision) to linear space (float)
static struct ConvertFromSRGB
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
static struct ConvertToSRGB
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

void EngineDDT::SetTexture0Color(ColorVal maxColor, ColorVal avgColor, bool optimize)
{
  //PROFILE_DX_SCOPE(3dPsC);
  bool toLinear = _canReadSRGB && _linearSpace;
  Color maxLinear = toLinear ? SRGBToLinear(maxColor) : maxColor;
  if (!optimize || ColorDistance2(_texture0MaxColor,maxLinear)>=Square(0.005))
  {
    //PROFILE_DX_SCOPE(3dPiC);
    _texture0MaxColor=maxLinear;
    // Some shaders (those who use more than 1 diffuse map - f.i. macro maps) needs
    // the value in pixel shader, others use it in vertex shader
    // - vertex shader constant will be set later depending on pixel shader ID
    SetPixelShaderConstantF(PSC_MaxColor,(float *)&maxLinear,1);
  }
  if (!optimize || ColorDistance2(_texture0AvgColor,avgColor)>=Square(0.005))
  {
    //PROFILE_DX_SCOPE(3dPiC);
    _texture0AvgColor=avgColor;
    Color linear = toLinear ? SRGBToLinear(avgColor) : maxColor;
    SetPixelShaderConstantF(PSC_T0AvgColor,(float *)&linear,1);
  }
}

void EngineDDT::SetTexture0(const TextureD3DT *tex)
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

  if (_canReadSRGB)
  {
    D3DSetSamplerStateR(0, D3DSAMP_SRGBTEXTURE_OLD, srgb && _linearSpace);
  }

  SetTexture0R(tex);
}

// Get, possibly create the hash table item
#define GET_HASHTABLE_ITEM(statesType,states,keyData,createStateFnc,comObj) \
Ref<statesType::TableItem> item; \
{ \
  const Ref<statesType::TableItem> &hashItem = states.Get(keyData); \
  if (states.IsNull(hashItem)) \
  { \
    ComRef<comObj> newState; \
    _d3DDevice->createStateFnc(&keyData, newState.Init()); \
    item = new statesType::TableItem(keyData, newState); \
    states.Add(item); \
  } \
  else \
  { \
    item = hashItem; \
  } \
}

void EngineDDT::SetupDepthStencilStates()
{
  D3D10_DEPTH_STENCIL_DESC dsd;

  // DepthEnable
  if (_renderState[D3DRS_ZENABLE_OLD].value == TRUE)
  {
    dsd.DepthEnable = TRUE;
  }
  else
  {
    dsd.DepthEnable = FALSE;
  }

  // DepthWriteMask
  if (_renderState[D3DRS_ZWRITEENABLE_OLD].value == TRUE)
  {
    dsd.DepthWriteMask = D3D10_DEPTH_WRITE_MASK_ALL;
  }
  else
  {
    dsd.DepthWriteMask = D3D10_DEPTH_WRITE_MASK_ZERO;
  }

  // DepthFunc
  switch (_renderState[D3DRS_ZFUNC_OLD].value)
  {
  case D3DCMP_LESSEQUAL_OLD:
    dsd.DepthFunc = D3D10_COMPARISON_LESS_EQUAL;
    break;
  case D3DCMP_GREATEREQUAL_OLD:
    dsd.DepthFunc = D3D10_COMPARISON_GREATER_EQUAL;
    break;
  case D3DCMP_ALWAYS_OLD:
    dsd.DepthFunc = D3D10_COMPARISON_ALWAYS;
    break;
  default:
    dsd.DepthFunc = (D3D10_COMPARISON_FUNC)0;
    Fail("Error: Not handled");
  }

  // StencilEnable
  dsd.StencilEnable = _renderState[D3DRS_STENCILENABLE_OLD].value;
  // StencilReadMask
  dsd.StencilReadMask = _renderState[D3DRS_STENCILMASK_OLD].value;
  // StencilWriteMask
  dsd.StencilWriteMask = _renderState[D3DRS_STENCILWRITEMASK_OLD].value;




  dsd.FrontFace.StencilFailOp = D3D10_STENCIL_OP_KEEP;
  dsd.FrontFace.StencilDepthFailOp = D3D10_STENCIL_OP_KEEP;

  // FrontFace.StencilPassOp
  switch (_renderState[D3DRS_STENCILPASS_OLD].value)
  {
  case D3DSTENCILOP_KEEP_OLD:
    dsd.FrontFace.StencilPassOp = D3D10_STENCIL_OP_KEEP;
    break;
  case D3DSTENCILOP_REPLACE_OLD:
    dsd.FrontFace.StencilPassOp = D3D10_STENCIL_OP_REPLACE;
    break;
  case D3DSTENCILOP_INCR_OLD:
    dsd.FrontFace.StencilPassOp = D3D10_STENCIL_OP_INCR;
    break;
  case D3DSTENCILOP_DECR_OLD:
    dsd.FrontFace.StencilPassOp = D3D10_STENCIL_OP_DECR;
    break;
  default:
    dsd.FrontFace.StencilPassOp = (D3D10_STENCIL_OP)0;
    Fail("Error: Not handled");
  }

  // FrontFace.StencilFunc
  switch (_renderState[D3DRS_STENCILFUNC_OLD].value)
  {
  case D3DCMP_LESSEQUAL_OLD:
    dsd.FrontFace.StencilFunc = D3D10_COMPARISON_LESS_EQUAL;
    break;
  case D3DCMP_GREATEREQUAL_OLD:
    dsd.FrontFace.StencilFunc = D3D10_COMPARISON_GREATER_EQUAL;
    break;
  case D3DCMP_ALWAYS_OLD:
    dsd.FrontFace.StencilFunc = D3D10_COMPARISON_ALWAYS;
    break;
  default:
    dsd.FrontFace.StencilFunc = (D3D10_COMPARISON_FUNC)0;
    Fail("Error: Not handled");
  }

  dsd.BackFace.StencilFailOp = D3D10_STENCIL_OP_KEEP;
  dsd.BackFace.StencilDepthFailOp = D3D10_STENCIL_OP_KEEP;
  dsd.BackFace.StencilPassOp = D3D10_STENCIL_OP_KEEP;
  dsd.BackFace.StencilFunc = D3D10_COMPARISON_ALWAYS;

  // Get the item from the pool (or create it) and return it
  GET_HASHTABLE_ITEM(DepthStencilStates, _depthStencilStates, dsd, CreateDepthStencilState, ID3D10DepthStencilState)
//   Ref<DepthStencilStates::TableItem> item;
//   {
//     const Ref<DepthStencilStates::TableItem> &hashItem = _depthStencilStates.Get(dsd);
//     if (_depthStencilStates.IsNull(hashItem))
//     {
//       ComRef<ID3D10DepthStencilState> newDepthStencilState;
//       _d3DDevice->CreateDepthStencilState(&dsd, newDepthStencilState.Init());
//       item = new DepthStencilStates::TableItem(dsd, newDepthStencilState);
//       _depthStencilStates.Add(item);
//     }
//     else
//     {
//       item = hashItem;
//     }
//   }

  // Set the render states
  OMSetDepthStencilState(item->_object, _renderState[D3DRS_STENCILREF_OLD].value);
}

void EngineDDT::SetupBlendStates()
{
  D3D10_BLEND_DESC bd;

  bd.AlphaToCoverageEnable = FALSE;

  // BlendEnable
  if (_renderState[D3DRS_ALPHABLENDENABLE_OLD].value == TRUE)
  {
    bd.BlendEnable[0] = TRUE;
  }
  else
  {
    bd.BlendEnable[0] = FALSE;
  }

  for (int i = 1; i < 8; i++) bd.BlendEnable[i] = FALSE;

  // SrcBlend
  switch (_renderState[D3DRS_SRCBLEND_OLD].value)
  {
  case D3DBLEND_ZERO_OLD:
    bd.SrcBlend = D3D10_BLEND_ZERO;
    break;
  case D3DBLEND_ONE_OLD:
    bd.SrcBlend = D3D10_BLEND_ONE;
    break;
  case D3DBLEND_SRCALPHA_OLD:
    bd.SrcBlend = D3D10_BLEND_SRC_ALPHA;
    break;
  case D3DBLEND_INVSRCALPHA_OLD:
    bd.SrcBlend = D3D10_BLEND_INV_SRC_ALPHA;
    break;
  default:
    bd.SrcBlend = (D3D10_BLEND)0;
    Fail("Error: Not handled");
  }

  // DestBlend
  switch (_renderState[D3DRS_DESTBLEND_OLD].value)
  {
  case D3DBLEND_ZERO_OLD:
    bd.DestBlend = D3D10_BLEND_ZERO;
    break;
  case D3DBLEND_ONE_OLD:
    bd.DestBlend = D3D10_BLEND_ONE;
    break;
  case D3DBLEND_SRCALPHA_OLD:
    bd.DestBlend = D3D10_BLEND_SRC_ALPHA;
    break;
  case D3DBLEND_INVSRCALPHA_OLD:
    bd.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
    break;
  default:
    bd.DestBlend = (D3D10_BLEND)0;
    Fail("Error: Not handled");
  }

  bd.BlendOp = D3D10_BLEND_OP_ADD;
  bd.SrcBlendAlpha = D3D10_BLEND_ONE;
  bd.DestBlendAlpha = D3D10_BLEND_ZERO;
  bd.BlendOpAlpha = D3D10_BLEND_OP_ADD;
  bd.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;
  for (int i = 1; i < 8; i++) bd.RenderTargetWriteMask[i] = D3D10_COLOR_WRITE_ENABLE_ALL;

  // Get the item from the pool (or create it) and return it
  GET_HASHTABLE_ITEM(BlendStates, _blendStates, bd, CreateBlendState, ID3D10BlendState)

  // Set the render states
  OMSetBlendState(item->_object, _renderState[D3DRS_COLORWRITEENABLE_OLD].value | 0xFFFFFFF0);
}

void EngineDDT::SetupRasterizerStates()
{
  D3D10_RASTERIZER_DESC rd;

  rd.FillMode = D3D10_FILL_SOLID;

  // CullMode
  switch (_renderState[D3DRS_CULLMODE_OLD].value)
  {
  case D3DCULL_NONE_OLD:
    rd.CullMode = D3D10_CULL_NONE;
    break;
  case D3DCULL_CW_OLD:
    rd.CullMode = D3D10_CULL_FRONT;
    break;
  case D3DCULL_CCW_OLD:
    rd.CullMode = D3D10_CULL_BACK;
    break;
  default:
    rd.CullMode = (D3D10_CULL_MODE)0;
    Fail("Error: Not handled");
  }

  rd.FrontCounterClockwise = FALSE;
  rd.DepthBias = 0;
  rd.DepthBiasClamp = 0.0f;
  rd.SlopeScaledDepthBias = 0.0f;
  rd.DepthClipEnable = TRUE;
  rd.ScissorEnable = FALSE;
  rd.MultisampleEnable = FALSE;
  rd.AntialiasedLineEnable = FALSE;

  // Get the item from the pool (or create it) and return it
  GET_HASHTABLE_ITEM(RasterizerStates, _rasterizerStates, rd, CreateRasterizerState, ID3D10RasterizerState)

  // Set the render states
  RSSetState(item->_object);
}

void EngineDDT::SetupSamplerStates()
{
  //ComRef<ID3D10SamplerState> newSamplerStates[MaxSamplerStages];
  ID3D10SamplerState *samplerStates[MaxSamplerStages];
  for (int i = 0; i < MaxSamplerStages; i++)
  {
    D3D10_SAMPLER_DESC sd;

    // Filter
    if (_samplerState[i][D3DSAMP_MINFILTER_OLD].value == D3DTEXF_POINT_OLD &&
        _samplerState[i][D3DSAMP_MAGFILTER_OLD].value == D3DTEXF_POINT_OLD &&
        _samplerState[i][D3DSAMP_MIPFILTER_OLD].value == D3DTEXF_POINT_OLD)
    {
      sd.Filter = D3D10_FILTER_MIN_MAG_MIP_POINT;
    }
    else if (_samplerState[i][D3DSAMP_MINFILTER_OLD].value == D3DTEXF_POINT_OLD &&
             _samplerState[i][D3DSAMP_MAGFILTER_OLD].value == D3DTEXF_POINT_OLD &&
             _samplerState[i][D3DSAMP_MIPFILTER_OLD].value == D3DTEXF_LINEAR_OLD)
    {
      sd.Filter = D3D10_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    }

    else if (_samplerState[i][D3DSAMP_MINFILTER_OLD].value == D3DTEXF_POINT_OLD &&
             _samplerState[i][D3DSAMP_MAGFILTER_OLD].value == D3DTEXF_LINEAR_OLD &&
             _samplerState[i][D3DSAMP_MIPFILTER_OLD].value == D3DTEXF_POINT_OLD)
    {
      sd.Filter = D3D10_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
    }
    else if (_samplerState[i][D3DSAMP_MINFILTER_OLD].value == D3DTEXF_POINT_OLD &&
             _samplerState[i][D3DSAMP_MAGFILTER_OLD].value == D3DTEXF_LINEAR_OLD &&
             _samplerState[i][D3DSAMP_MIPFILTER_OLD].value == D3DTEXF_LINEAR_OLD)
    {
      sd.Filter = D3D10_FILTER_MIN_POINT_MAG_MIP_LINEAR;
    }
    else if (_samplerState[i][D3DSAMP_MINFILTER_OLD].value == D3DTEXF_LINEAR_OLD &&
             _samplerState[i][D3DSAMP_MAGFILTER_OLD].value == D3DTEXF_POINT_OLD &&
             _samplerState[i][D3DSAMP_MIPFILTER_OLD].value == D3DTEXF_POINT_OLD)
    {
      sd.Filter = D3D10_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    }
    else if (_samplerState[i][D3DSAMP_MINFILTER_OLD].value == D3DTEXF_LINEAR_OLD &&
             _samplerState[i][D3DSAMP_MAGFILTER_OLD].value == D3DTEXF_POINT_OLD &&
             _samplerState[i][D3DSAMP_MIPFILTER_OLD].value == D3DTEXF_LINEAR_OLD)
    {
      sd.Filter = D3D10_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
    }
    else if (_samplerState[i][D3DSAMP_MINFILTER_OLD].value == D3DTEXF_LINEAR_OLD &&
             _samplerState[i][D3DSAMP_MAGFILTER_OLD].value == D3DTEXF_LINEAR_OLD &&
             _samplerState[i][D3DSAMP_MIPFILTER_OLD].value == D3DTEXF_POINT_OLD)
    {
      sd.Filter = D3D10_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    }
    else if (_samplerState[i][D3DSAMP_MINFILTER_OLD].value == D3DTEXF_LINEAR_OLD &&
             _samplerState[i][D3DSAMP_MAGFILTER_OLD].value == D3DTEXF_LINEAR_OLD &&
             _samplerState[i][D3DSAMP_MIPFILTER_OLD].value == D3DTEXF_LINEAR_OLD)
    {
      sd.Filter = D3D10_FILTER_MIN_MAG_MIP_LINEAR;
    }

    else if (_samplerState[i][D3DSAMP_MINFILTER_OLD].value == D3DTEXF_ANISOTROPIC_OLD &&
             _samplerState[i][D3DSAMP_MAGFILTER_OLD].value == D3DTEXF_ANISOTROPIC_OLD &&
             _samplerState[i][D3DSAMP_MIPFILTER_OLD].value == D3DTEXF_ANISOTROPIC_OLD)
    {
      sd.Filter = D3D10_FILTER_ANISOTROPIC;
    }
    else
    {
      sd.Filter = (D3D10_FILTER)0;
      Fail("Error: Not handled");
    }

    // AddressU
    switch (_samplerState[i][D3DSAMP_ADDRESSU_OLD].value)
    {
    case D3DTADDRESS_WRAP_OLD:
      sd.AddressU = D3D10_TEXTURE_ADDRESS_WRAP;
      break;
    case D3DTADDRESS_CLAMP_OLD:
      sd.AddressU = D3D10_TEXTURE_ADDRESS_CLAMP;
      break;
    default:
      sd.AddressU = (D3D10_TEXTURE_ADDRESS_MODE)0;
      Fail("Error: Not handled");
    }

    // AddressV
    switch (_samplerState[i][D3DSAMP_ADDRESSV_OLD].value)
    {
    case D3DTADDRESS_WRAP_OLD:
      sd.AddressV = D3D10_TEXTURE_ADDRESS_WRAP;
      break;
    case D3DTADDRESS_CLAMP_OLD:
      sd.AddressV = D3D10_TEXTURE_ADDRESS_CLAMP;
      break;
    default:
      sd.AddressV = (D3D10_TEXTURE_ADDRESS_MODE)0;
      Fail("Error: Not handled");
    }
    



    sd.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
    sd.MipLODBias = 0;

    // MaxAnisotropy
    sd.MaxAnisotropy = _samplerState[i][D3DSAMP_MAXANISOTROPY_OLD].value;

    sd.ComparisonFunc = D3D10_COMPARISON_NEVER;
    sd.BorderColor[0] = 1.0f;
    sd.BorderColor[1] = 1.0f;
    sd.BorderColor[2] = 1.0f;
    sd.BorderColor[3] = 1.0f;
    sd.MinLOD = -FLT_MAX;
    sd.MaxLOD = FLT_MAX;

    // Get the item from the pool (or create it) and return it
    GET_HASHTABLE_ITEM(SamplerStates, _samplerStates, sd, CreateSamplerState, ID3D10SamplerState)

    // Remember the render states
    //newSamplerStates[i] = item->_object;
    //samplerStates[i] = newSamplerStates[i];
    samplerStates[i] = item->_object;
  }

  // Set the render states
  _d3DDevice->PSSetSamplers(0, MaxSamplerStages, samplerStates);
}

void EngineDDT::SetupVSConstantBuffer()
{
  void *data = NULL;
  HRESULT hr = _vsConstants->Map(D3D10_MAP_WRITE_DISCARD, 0, &data);
  if (SUCCEEDED(hr))
  {
    memcpy(data, _shaderConstValues, sizeof(_shaderConstValues));
    _vsConstants->Unmap();
  }
  else
  {
    Fail("Error: Mapping of buffer failed");
  }

  // Start using the vertex constant buffer (not necessary to perform here)
  ID3D10Buffer *buffer[] = {_vsConstants};
  _d3DDevice->VSSetConstantBuffers(0, 1, buffer);
}

void EngineDDT::SetupPSConstantBuffer()
{
  void *data = NULL;
  HRESULT hr = _psConstants->Map(D3D10_MAP_WRITE_DISCARD, 0, &data);
  if (SUCCEEDED(hr))
  {
    memcpy(data, _pixelShaderConstValues, sizeof(_pixelShaderConstValues));
    _psConstants->Unmap();
  }
  else
  {
    Fail("Error: Mapping of buffer failed");
  }

  // Start using the pixel constant buffer (not necessary to perform here)
  ID3D10Buffer *buffer[] = {_psConstants};
  _d3DDevice->PSSetConstantBuffers(0, 1, buffer);
}

void EngineDDT::SetupTextures()
{
  // Find the first difference in texture stages
  int firstDiff;
  for (firstDiff = 0; firstDiff < MaxSamplerStages; firstDiff++)
  {
    if (_lastHandle[firstDiff] != _setHandle[firstDiff]) break;
  }

  // If some differences found, find the upper boundary and set the textures
  if (firstDiff < MaxSamplerStages)
  {
    // Find the last difference in texture stages
    int lastDiff;
    for (lastDiff = MaxSamplerStages - 1; lastDiff >= 0; lastDiff--)
    {
      if (_lastHandle[lastDiff] != _setHandle[lastDiff]) break;
    }
    DoAssert(firstDiff <= lastDiff);

    // Set the shader resources
    _d3DDevice->PSSetShaderResources(firstDiff, lastDiff - firstDiff + 1, &_lastHandle[firstDiff]);

    // Update the _setHandle array
    for (int i = firstDiff; i <= lastDiff; i++)
    {
      _setHandle[i] = _lastHandle[i];
    }
  }
}

void EngineDDT::SetupRenderStates()
{
  SetupDepthStencilStates();
  SetupBlendStates();
  SetupRasterizerStates();
  SetupSamplerStates();
  SetupVSConstantBuffer();
  SetupPSConstantBuffer();
  SetupTextures();
}

HRESULT EngineDDT::Map(const ComRef<IDirect3DVertexBuffer9Old> &buf, D3D10_MAP mapType, UINT mapFlags, void **ppData)
{
#if _ENABLE_REPORT
  if (buf == _vBufferLast)
  {
    Fail("Error: Trying to lock vertex buffer used by the device");
    ComRef<IDirect3DVertexBuffer9> empty;
    SetStreamSource(empty, 0, 0);
  }
#endif
  return buf->Map(mapType, mapFlags, ppData);
}

HRESULT EngineDDT::Map(const ComRef<IDirect3DIndexBuffer9Old> &buf, D3D10_MAP mapType, UINT mapFlags, void **ppData)
{
#if _ENABLE_REPORT
  if (buf == _iBufferLast)
  {
    Fail("Error: Trying to lock index buffer used by the device");
    _d3DDevice->IASetIndexBuffer(NULL, DXGI_FORMAT_R16_UINT, 0);
    _iBufferLast = NULL;
  }
#endif
  return buf->Lock(mapType, mapFlags, ppData);
}

void EngineDDT::SetTexture(int stage, ID3D10ShaderResourceView *tex, bool optimize)
{
#if !NO_SET_TEX
  if (!optimize || _lastHandle[stage] != tex)
  {
    //PROFILE_DX_SCOPE(3dSeT);
    //_d3DDevice->PSSetShaderResources(stage, 1, &tex);
  #if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("Texture stage %d change %s", stage, (const char *) tex ? "<not null>" : "<null>");
    }
  #endif
  }
#endif
  _lastHandle[stage] = tex;
}

void EngineDDT::SetStreamSource(const ComRef<IDirect3DVertexBuffer9Old> &buf, int stride, int bufSize, bool optimize)
{
  ADD_COUNTER(d3xVB,1);
  if (!optimize || _vBufferLast != buf)
  {
    IDirect3DVertexBuffer9Old *vertexBuffers[] = {buf};
    UINT vbStride = stride;
    UINT offset = 0;
    _d3DDevice->IASetVertexBuffers(0, 1, vertexBuffers, &vbStride, &offset);
    _vBufferLast = buf;
    _vBufferSize = bufSize;
#if DO_TEX_STATS
    if (LogStatesOnce)
    {
      LogF("Stream source change");
    }
#endif
  }
}

void EngineDDT::SetVertexDeclaration(const ComRef<ID3D10InputLayout> &decl, bool optimize)
{
//   if (!optimize || _vertexDeclLast != decl)
//   {
//       #if DO_TEX_STATS>=2
//       if (LogStatesOnce)
//       {
//         /*
//         D3DVERTEXELEMENT9 decl[64];
//         UINT numElements = lenof(decl);
//         decl->GetDeclaration()
//         LogF("SetVertexDeclaration %d",dtaV->VertexSize());
//         */
//         
//         for (int i=0; i<VDCount; i++) for (int j=0; j<VSDVCount; j++)
//         {
//           if (_vertexDeclD[i][j]==decl)
//           {
//             LogF("SetVertexDeclaration %d,%d",i,j);
//             goto Break;
//           }
//         }
//         LogF("SetVertexDeclaration ???");
//         Break:;
//       }
//       #endif
//     //PROFILE_DX_SCOPE(3dSVD);
//     _d3DDevice->IASetInputLayout(decl);
//     _vertexDeclLast = decl;
//   }
}

void EngineDDT::SetVertexShader(const VertexShaderInfo &vs, bool optimize)
{
  if (!optimize || _vertexShaderLast._shader != vs._shader)
  {
    //PROFILE_DX_SCOPE(3dSVS);
    _d3DDevice->VSSetShader(vs._shader);
    _d3DDevice->IASetInputLayout(vs._inputLayout);
    _vertexShaderLast = vs;
  }
}

bool EngineDDT::SetVertexShaderConstantF(int startRegister, const float *data, int vector4fCount, bool optimize)
{
  if (CHECK_ENG_DIAG(EDENoFilterVS))
  {
    optimize = false;
  }
  if (LimitVSCCount>=0 && vector4fCount>LimitVSCCount) vector4fCount = LimitVSCCount;

  // If constant is not a light, it should not reach the light space
  if (startRegister < LIGHTSPACE_START_REGISTER)
  {
    DoAssert(startRegister + vector4fCount - 1 < LIGHTSPACE_START_REGISTER);
  }

  //ADD_COUNTER(3dTVC,1);
  //ADD_COUNTER(3dTVS, vector4fCount);
//  PROFILE_DX_SCOPE(3dTVC);
  DoAssert(sizeof(float)==sizeof(int));
  if (!optimize || (MemCmp32(&_shaderConstValues[startRegister][0], data, vector4fCount * 4) != 0))
  {
    if (LimitVSCCount!=0)
    {
      //ADD_COUNTER(3dSVC,1);
      //ADD_COUNTER(3dSVS, vector4fCount);
      PROFILE_DX_SCOPE_DETAIL(3dSVC);
      //ADD_COUNTER(pbSVC,vector4fCount);
      // Direct3D 10 NEEDS UPDATE 
//       _d3DDevice->SetVertexShaderConstantF(startRegister, data, vector4fCount);
    }
    memcpy(&_shaderConstValues[startRegister][0], data, vector4fCount * 4 * sizeof(float));
    return true;
  }
  return false;
}

bool EngineDDT::SetVertexShaderConstantI(int startRegister, const int *data, int vector4fCount, bool optimize)
{
  if (CHECK_ENG_DIAG(EDENoFilterVS))
  {
    optimize = false;
  }

  for (int i = 0; i < vector4fCount; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      _shaderConstValues[INTSTART + startRegister + i][j] = data[i * 4 + j];
    }
  }
  return true;
}

bool EngineDDT::SetVertexShaderConstantB(int startRegister, const bool *data, int boolCount, bool optimize)
{
  for (int i = 0; i < boolCount; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      _shaderConstValues[BOOLSTART + startRegister + i][j] = data[i] ? 1.0f : -1.0f;
    }
  }
  return true;
}

void EngineDDT::SetPixelShader(const ComRef<ID3D10PixelShader> &ps, bool optimize)
{
  if (!CHECK_ENG_DIAG(EDENoSetupPS))
  {
    PROFILE_DX_SCOPE_DETAIL(3dTPC);
    if (!optimize || _pixelShaderLast != ps)
    {
      #if DO_TEX_STATS
        if (LogStatesOnce)
        {
          LogF("SetPixelShader %x",ps.GetRef());
        }
      #endif
      _d3DDevice->PSSetShader(ps);
      _pixelShaderLast = ps;
    }
  }
}

bool EngineDDT::SetPixelShaderConstantF(int startRegister, const float *data, int vector4fCount, bool optimize)
{
  if (CHECK_ENG_DIAG(EDENoSetupPS)) return false;

  DoAssert(sizeof(float)==sizeof(int));
  if (!optimize || (MemCmp32(&_pixelShaderConstValues[startRegister][0], data, vector4fCount * 4) != 0))
  {
    #if DO_TEX_STATS
      if (LogStatesOnce)
      {
        LogF(
          "SetPixelShaderConstantF %d,%d,%g,%g,%g,%g",
          startRegister,vector4fCount,data[0],data[1],data[2],data[3]
        );
      }
    #endif
    memcpy(&_pixelShaderConstValues[startRegister][0], data, vector4fCount * 4 * sizeof(float));
  }
  return true;
}

// Direct3D 10 NEEDS UPDATE 
// void EngineDDT::SetRenderTarget(
//   const ComRef<IDirect3DSurface9> &rts, const ComRef<ID3D10Texture2D> &rt,
//   const ComRef<IDirect3DSurface9> &dt,  
//   bool optimize
// )
// {
//   if (!optimize || _currRenderTargetSurface != rts)
//   {
//     PROFILE_DX_SCOPE_DETAIL(3dSRT);
//     _d3DDevice->SetRenderTarget(0, rts);
//     _currRenderTargetSurface = rts;
// 
// // Direct3D 10 NEEDS UPDATE 
// 
// //     if (_depthBufferRT) // This statement is true only in case multisampling is being used
// //     {
// //       _d3DDevice->SetDepthStencilSurface(dt);
// //       _currRenderTargetDepthBufferSurface = dt;
// //     }
//   }
//   _currRenderTarget = rt;
//   #if _ENABLE_REPORT
//     if (rt) for (int i=0; i<lenof(_lastHandle); i++)
//     {
//       DoAssert(_lastHandle[i]!=rt)
//     }
//   #endif
// }
// 
// void EngineDDT::SetRenderTargetAndDepthStencil(
//   const ComRef<IDirect3DSurface9> &rts, const ComRef<ID3D10Texture2D> &rt,
//   const ComRef<IDirect3DSurface9> &dts,  
//   bool optimize
// )
// {
//   // Lazy set of render target
//   if (!optimize || _currRenderTargetSurface != rts)
//   {
//     PROFILE_DX_SCOPE_DETAIL(3dSRT);
//     _d3DDevice->SetRenderTarget(0, rts);
//     _currRenderTargetSurface = rts;
//   }
//   _currRenderTarget = rt;
// 
//   // Lazy set of depth stencil
//   if (!optimize || _currRenderTargetDepthBufferSurface != dts)
//   {
//     PROFILE_DX_SCOPE_DETAIL(3dSDB);
//     _d3DDevice->SetDepthStencilSurface(dts);
//     _currRenderTargetDepthBufferSurface = dts;
//   }
// 
//   // Check render target is not set as any texture now
// #if _ENABLE_REPORT
//   if (rt) for (int i=0; i<lenof(_lastHandle); i++)
//   {
//     DoAssert(_lastHandle[i]!=rt)
//   }
// #endif
// }



void EngineDDT::DrawPrimitive(D3DPRIMITIVETYPE_OLD primitiveType, UINT startVertex, UINT primitiveCount)
{
  // Setup render and sampler states before the actual drawing
  SetupRenderStates();

  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   _d3DDevice->DrawPrimitive(primitiveType, startVertex, primitiveCount);
}

void EngineDDT::DrawIndexedPrimitive(D3DPRIMITIVETYPE_OLD primitiveType, INT baseVertexIndex, UINT minIndex, UINT numVertices, UINT startIndex, UINT primitiveCount)
{
  // Setup render and sampler states before the actual drawing
  SetupRenderStates();

  // Set the primitive topology
  switch (primitiveType)
  {
  case D3DPT_TRIANGLELIST_OLD:
    _d3DDevice->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    break;
  default:
    Fail("Error: Not handled");
  }

  // Draw the geometry
  _d3DDevice->DrawIndexed(primitiveCount * 3, startIndex, baseVertexIndex);

  // Wait for the flush
  //_d3DDevice->Flush();
}

void EngineDDT::OMSetDepthStencilState(const ComRef<ID3D10DepthStencilState> &dss, UINT stencilRef)
{
  if ((_currDSS != dss) || (_currStencilRef != stencilRef))
  {
    _d3DDevice->OMSetDepthStencilState(dss, stencilRef);
    _currDSS = dss;
    _currStencilRef = stencilRef;
  }
}

void EngineDDT::OMSetBlendState(const ComRef<ID3D10BlendState> &bs, UINT sampleMask)
{
  if ((_currBS != bs) || (_currSampleMask != sampleMask))
  {
    _d3DDevice->OMSetBlendState(bs, NULL, sampleMask);
    _currBS = bs;
    _currSampleMask = sampleMask;
  }
}

void EngineDDT::RSSetState(const ComRef<ID3D10RasterizerState> &rs)
{
  if (_currRS != rs)
  {
    _d3DDevice->RSSetState(rs);
    _currRS = rs;
  }
}

ComRef<ID3D10Texture2D> EngineDDT::GetCurrRenderTargetAsTexture()
{
  Fail("DX10 TODO");
  return NULL;
// Direct3D 10 NEEDS UPDATE 
//   // if current render target cannot be used as a texture, use StretchRect to copy it
//   if (_currRenderTarget)
//   {
//     return _currRenderTarget;
//   }
//   else if (_currRenderTargetSurface!=_renderTargetSurfaceD)
//   {
//     _d3DDevice->StretchRect(_currRenderTargetSurface,NULL,_renderTargetSurfaceD,NULL,D3DTEXF_NONE_OLD);
//     return _renderTargetD;
//   }
//   else
//   {
//     Fail("No other render target available");
//     return _renderTargetD;
//   }
}

void EngineDDT::SwitchToAnotherRenderTarget()
{
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   if (_currRenderTarget!=_renderTargetD)
//   {
//     SetRenderTarget(_renderTargetSurfaceD,_renderTargetD,NULL);
//   }
//   else
//   {
//     Fail("No other render target available");
//   }
}

void EngineDDT::SwitchToBackBufferRenderTarget()
{
  // if there is depth buffer, it is associated with render target, not back buffer
  // Set the render target
  Fail("DX10 TODO");
  // SetRenderTarget(_backBuffer,NULL,_depthBuffer); // Direct3D 10 NEEDS UPDATE 
  
  if (_canWriteSRGB)
  {
    D3DSetRenderState(D3DRS_SRGBWRITEENABLE_OLD,_linearSpace);
  }
  // once we render to the backbuffer with SRGB conversion, we are no longer in linear space
  _linearSpace = false;
}

ComRef<ID3D10Texture2D> EngineDDT::CreateRenderTargetCopyAsTexture()
{
  Fail("DX10 TODO");
  return NULL;
// Direct3D 10 NEEDS UPDATE 
//   if (_currRenderTarget!=_renderTargetD)
//   {
//     _d3DDevice->StretchRect(_currRenderTargetSurface,NULL,_renderTargetSurfaceD,NULL,D3DTEXF_NONE_OLD);
//     return _renderTargetD;
//   }
//   else
//   {
//     Fail("No other render target available");
//     return _renderTargetD;
//   }
}

void EngineDDT::SetTemporaryRenderTarget(const ComRef<ID3D10RenderTargetView> &rt, const ComRef<ID3D10DepthStencilView> &ds)
{
  _savedRenderTargetView = rt;
  _savedDepthStencilView = ds;
}

// Direct3D 10 CHANGED
// void EngineDDT::SetTemporaryRenderTarget(const ComRef<ID3D10Texture2D> &rt, const ComRef<IDirect3DSurface9> &rts, const ComRef<IDirect3DSurface9> &dts)
// {
//   _renderTargetSaved = _currRenderTarget;
//   _renderTargetSurfaceSaved = _currRenderTargetSurface;
//   _renderTargetDepthBufferSurfaceSaved = _currRenderTargetDepthBufferSurface;
//   SetRenderTargetAndDepthStencil(rts, rt, dts);
// }

void EngineDDT::RestoreOldRenderTarget()
{
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   SetRenderTargetAndDepthStencil(_renderTargetSurfaceSaved, _renderTargetSaved, _renderTargetDepthBufferSurfaceSaved);
//   _renderTargetSaved.Free();
//   _renderTargetSurfaceSaved.Free();
//   _renderTargetDepthBufferSurfaceSaved.Free();
}

inline int EngineDDT::AnisotropyLevel(PixelShaderID ps, bool road, TexFilter filter) const
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
    if (ps>=PSTerrain1 && ps<=PSTerrain15)
    {
      offset -= 1;
    }
    else if (ps>=PSTerrainSimple1 && ps<=PSTerrainSimple15)
    {
      offset -= 1;
    }
    else if (ps>=PSTerrainGrass1 && ps<=PSTerrainGrass15)
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

void EngineDDT::SetTexture( const TextureD3DT *tex, const TexMaterial *mat, int specFlags, int filterFlags )
{
  if (!tex && mat)
  {
    tex = static_cast<TextureD3DT*>(mat->_stage[0]._tex.GetRef());
  }
  SetTexture0(tex);

  if (!mat)
  {
    // no material - assume low anisotropy is enough here
    SetFiltering(0,TFAnizotropic,AnisotropyLevel(PSNormal, false, TFAnizotropic4));
    SetMultiTexturing(NULL, 0);
  }
  else
  {
    mat->Load();
    mat->AutodetectAnisotropy();
    Assert(UsableSampleStages<=TexMaterial::NStages);
    Assert(MaxStages<=TexMaterial::NTexGens);
    TextureD3DT *secTex[UsableSampleStages-1];
    //int nStages = UsableSampleStages-1;
    int nStages = mat->_stageCount;
    if (nStages>lenof(secTex))
    {
      nStages = lenof(secTex);
      ErrF("Material %s stage count overflow (%d)",cc_cast(mat->GetName()),nStages);
    }
    for (int i = 0; i < nStages; i++)
    {
      Texture *tex = mat->_stage[i+1]._tex;
      secTex[i] = static_cast<TextureD3DT*>(tex);
      if (tex)
      {
        int clamp = tex->GetClamp();
        D3DTEXTUREADDRESS_OLD addressU=( clamp&TexClampU ? D3DTADDRESS_CLAMP_OLD : D3DTADDRESS_WRAP_OLD );    
        D3DTEXTUREADDRESS_OLD addressV=( clamp&TexClampV ? D3DTADDRESS_CLAMP_OLD : D3DTADDRESS_WRAP_OLD );
        
        D3DSetSamplerStateR(i+1,D3DSAMP_ADDRESSU_OLD,addressU);
        D3DSetSamplerStateR(i+1,D3DSAMP_ADDRESSV_OLD,addressV);
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
      if (filter<TFAnizotropic)
      {
        // anisotropy disabled
        SetFiltering(i,filter,1);
      }
      else
      {
        SetFiltering(i,filter,AnisotropyLevel(mat->GetPixelShaderID(0), mat->GetRenderFlag(RFRoad), filter));
      }
    }

    // Calculate keepUntouchedLimit upon PS
    int keepUntouchedLimit;
    if (mat->GetPixelShaderID(0) == PSNonTLFlare)
    {
      keepUntouchedLimit = 13;
    }
    else
    {
      keepUntouchedLimit = TEXID_FIRST;
    }

    SetMultiTexturing(secTex, nStages, keepUntouchedLimit);
  }
}

#if RS_DIAGS
HRESULT EngineDDT::D3DSetRenderStateName
(
  D3DRENDERSTATETYPE_OLD state, DWORD value, const char *name, bool optimize
)
#else
HRESULT EngineDDT::D3DSetRenderState
(
  D3DRENDERSTATETYPE_OLD state, DWORD value, bool optimize
)
#endif
{
  #if DO_COUNTERS
    static OptimizeCounter opt("RenderState");
  #endif

  // assume small values of state (in DX6 max. state was about 40)
  _renderState.Access(state);
  RenderStateInfo &info=_renderState[state];

  if( info.value==value && optimize )
  {
    #if DO_COUNTERS
      opt.Skip();
    #endif
    return D3D_OK;
  }

  info.value=value;
  #if DO_COUNTERS
    opt.Perform();
  #endif

  #if DO_TEX_STATS
  if (LogStatesOnce)
  {
    #if RS_DIAGS
    LogF("SetRenderState %s,%d",name,value);
    #else
    LogF("SetRenderState %d,%d",state,value);
    #endif
  }
  #endif

  #if !NO_SET_RS
  //PROFILE_DX_SCOPE(3dsRS);

  // If handled case, return success
  if (state == D3DRS_ZFUNC_OLD) return D3D_OK;
  if (state == D3DRS_ZWRITEENABLE_OLD) return D3D_OK;
  if (state == D3DRS_ALPHABLENDENABLE_OLD) return D3D_OK;
  if (state == D3DRS_SRCBLEND_OLD) return D3D_OK;
  if (state == D3DRS_DESTBLEND_OLD) return D3D_OK;
  if (state == D3DRS_ZENABLE_OLD) return D3D_OK;
  if (state == D3DRS_CULLMODE_OLD) return D3D_OK;
  if (state == D3DRS_CLIPPING_OLD) return D3D_OK; // Direct3D 10 NEEDS UPDATE 
  if (state == D3DRS_ALPHATESTENABLE_OLD) return D3D_OK; // Direct3D 10 NEEDS UPDATE 
  if (state == D3DRS_ALPHAFUNC_OLD) return D3D_OK; // Direct3D 10 NEEDS UPDATE 
  if (state == D3DRS_ALPHAREF_OLD) return D3D_OK; // Direct3D 10 NEEDS UPDATE 
  if (state == D3DRS_SRGBWRITEENABLE_OLD) return D3D_OK; // Direct3D 10 NEEDS UPDATE 
  if (state == D3DRS_COLORWRITEENABLE_OLD) return D3D_OK;
  if (state == D3DRS_STENCILENABLE_OLD) return D3D_OK;
  if (state == D3DRS_STENCILREF_OLD) return D3D_OK;
  if (state == D3DRS_STENCILMASK_OLD) return D3D_OK;
  if (state == D3DRS_STENCILWRITEMASK_OLD) return D3D_OK;
  if (state == D3DRS_STENCILFUNC_OLD) return D3D_OK;
  if (state == D3DRS_STENCILPASS_OLD) return D3D_OK;
  if (state == D3DRS_TWOSIDEDSTENCILMODE_OLD) return D3D_OK;

  

  
  




  Fail("DX10 TODO");
  return 0;
  //ADD_COUNTER(pbSRS,1);
  // Direct3D 10 NEEDS UPDATE 
//   HRESULT err=_d3DDevice->SetRenderState(state,value);
//   //HRESULT err=D3D_OK;
//   DX_CHECKN("SetRS",err);
//   return err;
  #else
  return 0;
  #endif
}

/*!
\patch 2.01 Date 12/16/2002 by Ondra
- Fixed: Water detail texture moving again.
*/

void EngineDDT::DoPrepareTriangle(
  TextureD3DT *tex, const TexMaterialLODInfo &mat, int level, int spec, const EngineShapeProperties &prop
)
{
  PROFILE_DX_SCOPE_DETAIL(prepT);

  bool clampU=false,clampV=false;

  //Assert( (spec&(NoClamp|ClampU|ClampV))!=0 );
  // all triangles are marked for clamping
  if( spec&ClampU ) clampU=true;
  if( spec&ClampV ) clampV=true;
  D3DTEXTUREADDRESS_OLD addressU=( clampU ? D3DTADDRESS_CLAMP_OLD : D3DTADDRESS_WRAP_OLD );
  D3DSetSamplerStateR(0, D3DSAMP_ADDRESSU_OLD, addressU);
  D3DTEXTUREADDRESS_OLD addressV=( clampV ? D3DTADDRESS_CLAMP_OLD : D3DTADDRESS_WRAP_OLD );
  D3DSetSamplerStateR(0, D3DSAMP_ADDRESSV_OLD, addressV);
  
  int specFlags=spec&
  (
    NoZBuf|NoZWrite|NoAlphaWrite|NoStencilWrite|
    IsAlphaFog|
    IsShadow|IsShadowVolume|ShadowVolumeFrontFaces|
    IsAlpha|IsTransparent|
    DstBlendOne|DstBlendZero|FilterMask|
    NoColorWrite
  );

  if (
    _lastSpec!=specFlags || _lastMat!=mat || _lastRenderingMode!=_renderingMode ||
    _texGenScaleOrOffsetHasChanged || _currentShadowStateSettings==SS_None ||
    prop.IsDynamic()
  )
  {
    _lastSpec = specFlags;
    _lastMat = mat;
    _lastRenderingMode = _renderingMode;

    int alphaRef = 128;
    if (mat._mat)
    {
      TexMaterial::Loaded m(mat._mat);
      if (m.SomeRenderFlags())
      {
        if (m.GetRenderFlag(RFNoZWrite)) specFlags |= NoZWrite;
        if (m.GetRenderFlag(RFNoColorWrite)) specFlags |= NoColorWrite;
        if (m.GetRenderFlag(RFNoAlphaWrite)) specFlags |= NoAlphaWrite;
        if (m.GetRenderFlag(RFAddBlend)) specFlags |= DstBlendOne;
        // alpha test is used to force alpha testing with continuous alpha texture
        if (m.GetRenderFlag(RFAlphaTest64))
        {
          specFlags = (specFlags&~IsAlpha)|IsTransparent;
          alphaRef = 64;
        }
        else if (m.GetRenderFlag(RFAlphaTest32))
        {
          specFlags = (specFlags&~IsAlpha)|IsTransparent;
          alphaRef = 32;
        }
        else if (m.GetRenderFlag(RFAlphaTest128))
        {
          specFlags = (specFlags&~IsAlpha)|IsTransparent;
          alphaRef = 128;
        }
      }
    }

    if (specFlags&IsShadow)
    {
      D3DSetRenderStateR(D3DRS_COLORWRITEENABLE_OLD, 0);
      D3DSetRenderStateR(D3DRS_ZENABLE_OLD, TRUE);
      if (specFlags&IsShadowVolume)
      {
        // Shadow volume
        D3DSetRenderStateR(D3DRS_ZFUNC_OLD, D3DCMP_GREATEREQUAL_OLD);
        D3DSetRenderStateR(D3DRS_ZWRITEENABLE_OLD, FALSE);
      }
      else
      {
        // Ordinary shadow
        D3DSetRenderStateR(D3DRS_ZFUNC_OLD,D3DCMP_LESSEQUAL_OLD);
        if (!_hasStencilBuffer)
        {
          D3DSetRenderStateR(D3DRS_ZWRITEENABLE_OLD,TRUE);
        }
        else
        {
          D3DSetRenderStateR(D3DRS_ZWRITEENABLE_OLD,FALSE);
        }
      }
    }
    else
    {
      if ((_renderingMode == RMDepthMap) && (specFlags&NoZWrite)) // Added because of the collimator
      {
        D3DSetRenderStateR(D3DRS_COLORWRITEENABLE_OLD, 0);
      }
      else if ((_renderingMode == RMShadowBuffer) && (_sbTechnique == SBT_nVidia))
      {
        D3DSetRenderStateR(D3DRS_COLORWRITEENABLE_OLD, 0);
      }
      else if ((_renderingMode == RMShadowBuffer) && (_sbTechnique == SBT_ATIDF16))
      {
        D3DSetRenderStateR(D3DRS_COLORWRITEENABLE_OLD, 0);
      }
      else if ((_renderingMode == RMShadowBuffer) && (_sbTechnique == SBT_ATIDF24))
      {
        D3DSetRenderStateR(D3DRS_COLORWRITEENABLE_OLD, 0);
      }
      else if (specFlags&IsHidden)
      {
        D3DSetRenderStateR(D3DRS_COLORWRITEENABLE_OLD, 0);
      }
      else if (specFlags&(NoZWrite|NoAlphaWrite))
      {
        if (specFlags&NoColorWrite)
        {
          // sometimes we may want to write alpha only 
          D3DSetRenderStateR(D3DRS_COLORWRITEENABLE_OLD, (specFlags&NoAlphaWrite) ? 0 : D3DCOLORWRITEENABLE_ALPHA);
        }
        else
        {
          D3DSetRenderStateR(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
        }
      }
      else if (specFlags&NoColorWrite)
      {
        D3DSetRenderStateR(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_ALPHA);
      }
      else
      {
        D3DSetRenderStateR(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
      }
      
      if( specFlags&NoZBuf )
      {
        D3DSetRenderStateR(D3DRS_ZFUNC_OLD,D3DCMP_ALWAYS_OLD);
        D3DSetRenderStateR(D3DRS_ZWRITEENABLE_OLD,FALSE);
        D3DSetRenderStateR(D3DRS_ZENABLE_OLD,FALSE);
      }
      else if( specFlags&NoZWrite )
      {
        // road
        D3DSetRenderStateR(D3DRS_ZFUNC_OLD,D3DCMP_LESSEQUAL_OLD);
        D3DSetRenderStateR(D3DRS_ZWRITEENABLE_OLD,FALSE);
        D3DSetRenderStateR(D3DRS_ZENABLE_OLD,TRUE);
      }
      else
      {
        D3DSetRenderStateR(D3DRS_ZFUNC_OLD,D3DCMP_LESSEQUAL_OLD);
        D3DSetRenderStateR(D3DRS_ZWRITEENABLE_OLD,TRUE);
        D3DSetRenderStateR(D3DRS_ZENABLE_OLD,TRUE);
      }
    }
    
    // Detect back face culling is disabled
    bool noCull = (specFlags&NoBackfaceCull)!=0;

    if (specFlags&IsShadow)
    {

      // Select a proper pixel shader
      if (mat._mat && _tlActive)
      {
        // secondary texture used in the material
        // this is possible only with HW T&L
        //DoAssert(_tlActive);
        PrepareDetailTex(mat,prop);
      }
      else
      {
        // single texturing
        //PrepareSingleTexDiffuseA();
        EnableCustomTexGenZero(true);
        SelectPixelShader(PSWhiteAlpha);
      }

      D3DSetRenderStateR(D3DRS_ALPHABLENDENABLE_OLD,FALSE);
      if (_renderingMode == RMShadowBuffer)
      {
        ShadowsStatesSettings(SS_ShadowBuffer,noCull);
      }
      else if (specFlags&IsShadowVolume)
      {
        D3DSetRenderStateR(D3DRS_ALPHATESTENABLE_OLD,FALSE);
        if (specFlags&ShadowVolumeFrontFaces)
        {
          ShadowsStatesSettings(SS_Volume_Front,false);
        }
        else
        {
          ShadowsStatesSettings(SS_Volume_Back,false);
        }
      }
      else
      {
        D3DSetRenderStateR(D3DRS_ALPHAREF_OLD,128);
        D3DSetRenderStateR(D3DRS_ALPHATESTENABLE_OLD,TRUE);
        ShadowsStatesSettings(SS_Ordinary,false);
      }
    }
    else
    {
      if (mat._mat && _tlActive)
      {
        // secondary texture used in the material
        // this is possible only with HW T&L
        PrepareDetailTex(mat,prop);
      }
      else
      {
        // single texturing
        PrepareSingleTexDiffuseA();
      }
      
      if( specFlags&IsAlpha )
      {
        D3DSetRenderStateR(D3DRS_ALPHAREF_OLD,1);
        D3DSetRenderStateR(D3DRS_ALPHABLENDENABLE_OLD,TRUE);
        D3DSetRenderStateR(D3DRS_SRCBLEND_OLD,D3DBLEND_SRCALPHA_OLD);
        switch (specFlags&(DstBlendOne|DstBlendZero))
        {
          default:
          case 0:
            D3DSetRenderStateR(D3DRS_DESTBLEND_OLD,D3DBLEND_INVSRCALPHA_OLD);
            D3DSetRenderStateR(D3DRS_ALPHATESTENABLE_OLD, TRUE);
            break;
          case DstBlendOne:
            // Disabled alpha test - otherwise moon halo causes artifacts
            D3DSetRenderStateR(D3DRS_DESTBLEND_OLD,D3DBLEND_ONE_OLD);
            D3DSetRenderStateR(D3DRS_ALPHATESTENABLE_OLD,FALSE);
            break;
          case DstBlendZero:
            D3DSetRenderStateR(D3DRS_DESTBLEND_OLD,D3DBLEND_ZERO_OLD);
            D3DSetRenderStateR(D3DRS_ALPHATESTENABLE_OLD,FALSE);
            break;
        }
      }
      else
      {
        // check if alpha testing is required
        if (specFlags&IsTransparent)
        {
          D3DSetRenderStateR(D3DRS_ALPHAREF_OLD, alphaRef);
          D3DSetRenderStateR(D3DRS_ALPHATESTENABLE_OLD,TRUE);
        }
        else
        {
          D3DSetRenderStateR(D3DRS_ALPHATESTENABLE_OLD,FALSE);
        }
        D3DSetRenderStateR(D3DRS_ALPHABLENDENABLE_OLD,FALSE);
      }

      if (_renderingMode == RMShadowBuffer)
      {
        ShadowsStatesSettings(SS_ShadowBuffer,noCull);
      }
      else
      {
        ShadowsStatesSettings(
          specFlags&(NoZWrite|NoZBuf|NoAlphaWrite|NoStencilWrite) ? SS_Transparent : SS_Receiver,
          noCull
        );
      }
    }
  }
  
  #if 1
    LogTexture("Tri",tex);
    SetTexture(tex,mat._mat,spec,specFlags&FilterMask);
    //HRESULT err=D3D_OK;
    //if( err ) DDError9("Cannot set texture",err);
  #endif

  // We know pixel shader (set f.i. in PrepareDetailTex()) and we know
  // _texture0MaxColor (set in SetTexture()). We can set some shader constants now
  SetVertexShaderConstantMaxColorR();
}

void EngineDDT::DoPrepareTriangle(
  const MipInfo &absMip, const TexMaterialLODInfo &mat, int specFlags, const EngineShapeProperties &prop
)
{
  TextureD3DT *tex = static_cast<TextureD3DT *>(absMip._texture);
  int level = absMip._level;
  // allocate some queue
  DoPrepareTriangle(tex,mat,level,specFlags,prop);
}

void EngineDDT::QueuePrepareTriangle( const MipInfo &absMip, const TexMaterial *mat, int specFlags )
{
  TextureD3DT *tex = static_cast<TextureD3DT *>(absMip._texture);
  int level = absMip._level;
  _queueNo._actTri = AllocateQueue(_queueNo,tex,level,mat,specFlags);
  Assert (_queueNo._triUsed[_queueNo._actTri]);
}

void EngineDDT::PrepareTriangle
(
  const MipInfo &absMip, const TexMaterial *mat, int specFlags0
)
{
  TextureD3DT *tex = static_cast<TextureD3DT *>(absMip._texture);
  SwitchRenderMode(RMTris);
  // allocate some queue
  SwitchTL(TLDisabled);
  int level = absMip._level;
  _queueNo._actTri = AllocateQueue(_queueNo,tex,level,mat,specFlags0);
  Assert (_queueNo._triUsed[_queueNo._actTri]);
  _prepSpec = specFlags0;

}

void EngineDDT::PrepareTriangleTL(
  const PolyProperties *section,
  const MipInfo &mip, const TexMaterialLODInfo &mat, int specFlags,
  const TLMaterial &tlMat, int spec, const EngineShapeProperties &prop
)
{
  Assert(_tlActive==TLEnabled);

  // Set the new vertex shader
  if (mat._mat)
  {
    // Set the material shader
    TexMaterial::Loaded m(mat._mat);

    // consider material LOD
    _vertexShaderID = m.GetVertexShaderID(mat._level);

    if ((m.GetMainLight() == ML_Sun) || (m.GetMainLight() == ML_None))
    {
      EnableSunLight(((spec&(DisableSun|SunPrecalculated))==0)?ML_Sun:ML_None);
    }
    else
    {
      EnableSunLight(m.GetMainLight());
    }

    // Set the material fog
    SetFogMode(m.GetFogMode());
  }
  else
  {
    // Set the default shader
    _vertexShaderID = VSBasic;

    // Enable sunlight based on flags and the TexMaterial
    EnableSunLight(((spec&(DisableSun|SunPrecalculated))==0)?ML_Sun:ML_None);

    // Fog is usual without material
    SetFogMode(FM_Fog);
  }

  TextureD3DT *tex = static_cast<TextureD3DT *>(mip._texture);
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
    DoPrepareTriangle(tex,TexMaterialLODInfo(),level,specFlagsSB,prop);
  }
  else
  {
    DoPrepareTriangle(tex,mat,level,specFlags,prop);
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
  bool vsIsAS = (_vertexShaderID == VSBasicAS) || (_vertexShaderID == VSNormalMapAS) || (_vertexShaderID == VSNormalMapDiffuseAS);
  DoSetMaterial(matMod, vsIsAS);
}

/*!
\patch 1.33 Date 11/28/2001 by Ondra
- Fixed: D3D rendering queue bug that could cause polygons being dropped.
*/

void EngineDDT::DrawDecal
(
  Vector3Par screen, float rhw, float sizeX, float sizeY, PackedColor color,
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
  ForgetDeviceBuffers();

  SwitchRenderMode(RMTris);
  // allocate some queue
  SwitchTL(TLDisabled);

  AddVertices(v,4);

  QueuePrepareTriangle(mip, mat, specFlags);
  static const VertexIndex indices[4] = {0,1,2,3};
  QueueFan(indices,4);
}

void EngineDDT::DrawPolygon( const VertexIndex *ii, int n )
{
  if (ResetNeeded()) return;
  QueueFan(ii,n);
}

void EngineDDT::DrawSection
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

void EngineDDT::ResetShaderConstantValues()
{
  for (int i = 0; i < lenof(_shaderConstValues); i++)
  {
    for (int j = 0; j < 4; j++)
    {
      _shaderConstValues[i][j] = FLT_MAX;
    }
  }
  for (int i = 0; i < lenof(_shaderConstValuesI); i++)
  {
    for (int j = 0; j < 4; j++)
    {
      _shaderConstValuesI[i][j] = INT_MAX;
    }
  }
  for (int i = 0; i < lenof(_pixelShaderConstValues); i++)
  {
    for (int j = 0; j < 4; j++)
    {
      _pixelShaderConstValues[i][j] = FLT_MAX;
    }
  }
}

void EngineDDT::DoSetupSectionTL(EMainLight mainLight, EFogMode fogMode,
                                 VertexShaderID vertexShaderID, const UVSource *uvSource, int uvSourceCount, bool uvSourceOnlyTex0,
                                 ESkinningType skinningType,
                                 bool instancing, int instancesCount, int pointLightsCount, int spotLightsCount,
                                 bool shadowReceiver,
                                 bool alphaShadowEnabled, RenderingMode renderingMode, bool backCullDisabled, bool craterRendering)
{
  //ADD_COUNTER(sSecTL,1);
  PROFILE_DX_SCOPE_DETAIL(3dLVS);

  // Update the current parameters
  _currMainLight = mainLight;
  _currFogMode = fogMode;
  _currVertexShaderID = vertexShaderID;
  for (int i = 0; i < uvSourceCount; i++) _currUVSource[i] = uvSource[i];
  _currUVSourceCount = uvSourceCount;
  _currUVSourceOnlyTex0 = uvSourceOnlyTex0;
  _currSkinningType = skinningType;
  _currInstancing = instancing;
  if (instancing) _currInstancesCount = instancesCount;
  _currPointLightsCount = pointLightsCount;
  _currSpotLightsCount = spotLightsCount;
  _currShadowReceiver = shadowReceiver;
  _currAlphaShadowEnabled = alphaShadowEnabled;
  _currRenderingMode = renderingMode;
  _currBackCullDisabled = backCullDisabled;
  _currCraterRendering = craterRendering;

  // Rendering mode
  int rm = craterRendering ? 3 : renderingMode;

  // Craters on terrain most likely will not work properly (as we use the non-blended position in the vertex shader) (on the other hand, the blending doesn't
  // occur on close distances). Anyway, terrain use own craters.
  DoAssert((rm != 3) || ((vertexShaderID != VSTerrain) && (vertexShaderID != VSTerrainGrass)));

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
    SetVertexShaderConstantB2R(0, fogModeB);

    // AlphaShadowEnabled
    SetVertexShaderConstantBR(3, &alphaShadowEnabled);

    // ShadowReceiverFlag
    SetVertexShaderConstantBR(4, &shadowReceiver);

    // SBTechniqueDefault
    bool sbTechniqueDefault = (_sbTechnique == SBT_Default);
    SetVertexShaderConstantBR(5, &sbTechniqueDefault);

    // EnableAlignNormal
    SetVertexShaderConstantBR(6, &backCullDisabled);
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
      case ML_None:
        mainLightN = 0;
        break;
      case ML_Sun:
        mainLightN = 1;
        break;
      case ML_Sky:
        mainLightN = 2;
        break;
      default:
        mainLightN = 3;
      }

      // SkinningInstancing
      int siN;
      if ((skinningType == STNone) && (!instancing))
      {
        siN = 0;
      }
      else if ((skinningType != STNone) && (!instancing))
      {
        siN = 1;
      }
      else if ((skinningType == STNone) && (instancing))
      {
        siN = 2;
      }
      else
      {
        Fail("Error: No skinning and instancing supported together");
        siN = 0;
      }

      // Use the vertex shader
      _vertexShader = _vertexShaderPool[VSToVSP[vertexShaderID]][uvInputN][mainLightN][rm][siN];
    }

    // Finish - to not to link in the next paragraphs
    return;
  }
  else if (vertexShaderID == VSShadowVolume)
  {
    // SkinningInstancing
    int siN;
    if ((skinningType == STNone) && (!instancing))
    {
      siN = 0;
    }
    else if ((skinningType != STNone) && (!instancing))
    {
      siN = 1;
    }
    else if ((skinningType == STNone) && (instancing))
    {
      siN = 2;
    }
    else
    {
      Fail("Error: No skinning and instancing supported together");
      siN = 0;
    }

    // Use the vertex shader
    _vertexShader = _vertexShaderShadowVolume[siN];

    // Finish - to not to link in the next paragraphs
    return;
  }
  else if (VSToVSS[vertexShaderID] < NVertexShaderSpecialID)
  {
    // Use the vertex shader
    _vertexShader = _vertexShaderSpecial[VSToVSS[vertexShaderID]];

    // Finish - to not to link in the next paragraphs
    return;
  }
  else if (vertexShaderID == VSTerrain)
  {
    // Use the vertex shader
    _vertexShader = _vertexShaderTerrain[rm];

    // Finish - to not to link in the next paragraphs
    return;
  }
  else
  {
    Fail("Error: Some vertex shader not handled");
  }
}

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

#define VSC_PointLoopCount  0
#define VSC_SpotLoopCount   1

void EngineDDT::SetupVSConstantsPSLights()
{
  // Point lights
  {
    SLPoint LPointData[MAX_LIGHTS];
    int pointLightIndex = 0;
    for (int i = 0; i < _lights.Size(); i++)
    {
      LightDescription ldesc;
      _lights[i]->GetDescription(ldesc, GetAccomodateEye());
      if (ldesc.type == LTPoint)
      {

        // LPoint_TransformedPos
        Vector3 tPos = TransformAndNormalizePositionToObjectSpace(ldesc.pos);
        LPointData[pointLightIndex].TransformedPos = D3DXVECTOR4(tPos.X(), tPos.Y(), tPos.Z(), 1.0f);

        // LPoint_Atten
        float sa = Square(ldesc.startAtten);
        LPointData[pointLightIndex].Atten = D3DXVECTOR4(sa, sa, sa, sa);

        // LPoint_D
        Color pointD = ldesc.diffuse * _night * _matDiffuse;
        LPointData[pointLightIndex].D = D3DXVECTOR4(pointD.R(), pointD.G(), pointD.B(), _matDiffuse.A());

        // LPoint_A
        Color pointA = ldesc.ambient * _night * _matAmbient + ldesc.diffuse * _matForcedDiffuse * _night;
        LPointData[pointLightIndex].A = D3DXVECTOR4(pointA.R(), pointA.G(), pointA.B(), pointA.A());

        // Increment index
        pointLightIndex++;

        // There might be more lights in the array than we require, so break whenever we reach the expected count
        if (pointLightIndex >= _pointLightsCount) break;
      }
    }

    // Set the VS constants
    if (pointLightIndex > 0)
    {
      const int vectorsPerLight = sizeof(SLPoint) / sizeof(D3DXVECTOR4);
      SetVertexShaderConstantF(LIGHTSPACE_START_REGISTER, (float*)LPointData, pointLightIndex * vectorsPerLight);
    }

    // Set the number of point lights for shader's static looping purposes
    int pointLoopCount[4];
    for (int i = 0; i < 4; i++) pointLoopCount[i] = pointLightIndex;
    SetVertexShaderConstantIR(VSC_PointLoopCount, pointLoopCount);
  }

  // Spot lights
  {
    SLSpot LSpotData[MAX_LIGHTS];
    int spotLightIndex = 0;
    for (int i = 0; i < _lights.Size(); i++)
    {
      LightDescription ldesc;
      _lights[i]->GetDescription(ldesc, GetAccomodateEye());
      if (ldesc.type == LTSpotLight)
      {

        // LSpot_TransformedPos
        Vector3 tPos = TransformAndNormalizePositionToObjectSpace(ldesc.pos);
        LSpotData[spotLightIndex].TransformedPos_CosPhiHalf = D3DXVECTOR4(tPos.X(), tPos.Y(), tPos.Z(), cos(ldesc.phi * 0.5f));

        // LSpot_TransformedDir
        Vector3 tDir = TransformAndNormalizeVectorToObjectSpace(ldesc.dir);
        LSpotData[spotLightIndex].TransformedDir_CosThetaHalf = D3DXVECTOR4(tDir.X(), tDir.Y(), tDir.Z(), cos(ldesc.theta * 0.5f));

        // LSpot_D
        Color spotD = ldesc.diffuse * _night * _matDiffuse;
        LSpotData[spotLightIndex].D_InvCTHMCPH = D3DXVECTOR4(spotD.R(), spotD.G(), spotD.B(), 1.0f / (cos(ldesc.theta * 0.5f) - cos(ldesc.phi * 0.5f)));

        // LSpot_A
        Color spotA = ldesc.ambient * _night * _matAmbient * 0.3f + ldesc.diffuse * _matForcedDiffuse * _night;
        LSpotData[spotLightIndex].A_Atten = D3DXVECTOR4(spotA.R(), spotA.G(), spotA.B(), Square(ldesc.startAtten) / 4.0f);

        // Increment index
        spotLightIndex++;

        // There might be more lights in the array than we require, so break whenever we reach the expected count
        if (spotLightIndex >= _spotLightsCount) break;
      }
    }

    // Set the VS constants
    if (spotLightIndex > 0)
    {
      const int vectorsPerLight = sizeof(SLSpot) / sizeof(D3DXVECTOR4);
      SetVertexShaderConstantF(LIGHTSPACE_START_REGISTER + (MAX_LIGHTS - spotLightIndex) * vectorsPerLight, (float*)LSpotData, spotLightIndex * vectorsPerLight);
    }

    // Set the number of spot lights for shader's static looping purposes
    int spotLoopCount[4];
    for (int i = 0; i < 4; i++) spotLoopCount[i] = spotLightIndex;
    SetVertexShaderConstantIR(VSC_SpotLoopCount, spotLoopCount);
  }
}

void EngineDDT::SetupVSConstantsLODPars()
{
  float lodPars[4];
  _sceneProps->GetTerrainLODPars(lodPars);
  SetVertexShaderConstantR(VSC_TerrainLODPars, lodPars);
}

void EngineDDT::SetupVSConstantsPool(int minBoneIndex, int bonesCount)
{
  // Shader specific constants
  {
    // NormalMapThrough
    if ((_vertexShaderID == VSNormalMapThrough) ||
      (_vertexShaderID == VSNormalMapSpecularThrough) ||
      (_vertexShaderID == VSNormalMapThroughNoFade) ||
      (_vertexShaderID == VSNormalMapSpecularThroughNoFade))
    {
      const Vector3 *treeCrown = GetTreeCrown();
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
        SetVertexShaderConstant2R(VSC_TreeCrown, (float*)centerSize);
      }
      else
      {
        D3DXVECTOR4 centerSize[2]={
          D3DXVECTOR4(center.X(),center.Y(),center.Z(),1),
          D3DXVECTOR4(1e9,1e9,1e9,1e9)
        };
        SetVertexShaderConstant2R(VSC_TreeCrown, (float*)centerSize);
      }
    }

    // Glass
    if (_vertexShaderID == VSGlass)
    {
      SetVertexShaderConstantLWSMR();
    }
  }

  // Common constants
  {
    // In case of skinning and many bones (that they weren't set directly) set the section bones
    if (_skinningType != STNone)
    {
      if (!_bonesWereSetDirectly)
      {
        DoAssert(minBoneIndex + bonesCount <= _bones.Size());
        SetBones(minBoneIndex, _bones.Data(), bonesCount);
      }
    }

    // Some fog modes need to have some constants set
    if ((_fogMode == FM_Fog) || (_fogMode == FM_FogAlpha))
    {
      SetVertexShaderConstantFFARR();
    }

    // InitLights HLSL function uses the following constants
    if ((_mainLight != ML_SunObject) && (_mainLight != ML_SunHaloObject) && (_mainLight != ML_MoonObject) && (_mainLight != ML_MoonHaloObject))
    {
      SetVertexShaderConstantSAFRR(_matPower, _matAmbient);
    }

    // Point and spot lights
    if (_mainLight < ML_Sky || _mainLight > ML_MoonHaloObject)
    {
      SetupVSConstantsPSLights();
    }

    if (_uvSourceCount > 0)
    {
      if (!_uvSourceOnlyTex0)
      {
        // Fill out the VSC_TexCoordType
        DoAssert(_uvSourceCount <= MaxStages);
        DoAssert((MaxStages % 4) == 0);
        float texCoordType[MaxStages];
        for (int i = 0; i < _uvSourceCount; i++)
        {
          switch (_uvSource[i])
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
            ErrF("UV source %s not supported",cc_cast(FindEnumName(_uvSource[i])));
            texCoordType[i] = 0;
            break;
          }
        }
        int constCount = toIntCeil((float)_uvSourceCount / 4.0f);
        if (constCount == 1)
        {
          SetVertexShaderConstantR(VSC_TexCoordType, texCoordType);
        }
        else if (constCount == 2)
        {
          SetVertexShaderConstant2R(VSC_TexCoordType, texCoordType);
        }
      }
    }
  }
}

void EngineDDT::SetupVSConstantsShadowVolume(int minBoneIndex, int bonesCount)
{
  // In case of skinning and many bones (that they weren't set directly) set the section bones
  if (_skinningType != STNone)
  {
    if (!_bonesWereSetDirectly)
    {
      DoAssert(minBoneIndex + bonesCount <= _bones.Size());
      SetBones(minBoneIndex, _bones.Data(), bonesCount);
    }
  }

  // Set the light direction
  Vector3 tDirection = TransformAndNormalizeVectorToObjectSpace(_shadowDirection);
  D3DXVECTOR4 direction(tDirection.X(), tDirection.Y(), tDirection.Z(), 1.0f);
  SetVertexShaderConstantF(VSC_LDirectionTransformedDir, (float*)direction, 1);
}

void EngineDDT::SetupVSConstantsSprite()
{
  // Some fog modes need to have some constants set
  if ((_fogMode == FM_Fog) || (_fogMode == FM_FogAlpha))
  {
    SetVertexShaderConstantFFARR();
  }

  // InitLights HLSL function uses the following constants
  SetVertexShaderConstantSAFRR(_matPower, _matAmbient);

  // Point and spot lights
  SetupVSConstantsPSLights();
}

void EngineDDT::SetupVSConstantsWater()
{
  // Fog constants
  SetVertexShaderConstantFFARR();

  // InitLights HLSL function uses the following constants
  SetVertexShaderConstantSAFRR(_matPower, _matAmbient);

  // Set the LWS matrix
  SetVertexShaderConstantLWSMR();

  SetVertexShaderConstantF(VSC_WaterDepth, (const float *)_waves.Data(), _waves.Size());

  float waveXScale,waveZScale,waveHeight;
  _sceneProps->GetSeaWavePars(waveXScale,waveZScale,waveHeight);

  const float waveHeightVal[]=
  {
    waveHeight,waveHeight,
    waveXScale*(2*H_PI)*waveHeight,waveZScale*(2*H_PI)*waveHeight
  };

  const float gridVal[4]={
    1/_waterPar._grid,
    _waterPar._waterSegSize,
    _waterPar._waterSegSize-1,
    (_waterPar._waterSegSize-1)*0.5f
  };
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

  SetVertexShaderConstantF(VSC_WaveHeight,      (const float *)&waveHeightVal, 1);
  SetVertexShaderConstantF(VSC_WaveGrid,        (const float *)&gridVal, 1);
  SetVertexShaderConstantF(VSC_WaterPeakWhite,  (const float *)&peakVal, 1);
  SetVertexShaderConstantF(VSC_WaterSeaLevel,   (const float *)&seaLevelVal, 1);

  // Point and spot lights
  SetupVSConstantsPSLights();
}

void EngineDDT::SetupVSConstantsShore()
{
  // Terrain LOD
  SetupVSConstantsLODPars();

  // Fog constants
  SetVertexShaderConstantFFARR();

  // InitLights HLSL function uses the following constants
  SetVertexShaderConstantSAFRR(_matPower, _matAmbient);

  // Set the LWS matrix
  SetVertexShaderConstantLWSMR();

  float lodPars[4];

  // Wave period
  const float ShoreWaveXDuration = 8000;
  lodPars[0] = Glob.time.ModMs(ShoreWaveXDuration) * (1.0f / ShoreWaveXDuration) * 2.0f * H_PI;

  // Unused
  lodPars[1] = lodPars[2] = 0.0f;

  // Sea level
  lodPars[3] = _sceneProps->GetSeaLevel();

  // Period
  SetVertexShaderConstantR(VSC_Period_X_X_SeaLevel, lodPars);

  // Point and spot lights
  SetupVSConstantsPSLights();
}

void EngineDDT::SetupVSConstantsTerrain()
{
  // Terrain LOD
  SetupVSConstantsLODPars();

  // Fog constants
  SetVertexShaderConstantFFARR();

  // InitLights HLSL function uses the following constants
  SetVertexShaderConstantSAFRR(_matPower, _matAmbient);

  // Point and spot lights
  SetupVSConstantsPSLights();
}

void EngineDDT::SetupVSConstantsTerrainGrass()
{
  // Terrain LOD
  SetupVSConstantsLODPars();

  // Grass alpha
  float grassPars[8];
  _sceneProps->GetGrassPars(grassPars);
  SetVertexShaderConstant2R(VSC_TerrainAlphaAdd, grassPars);

  // InitLights HLSL function uses the following constants
  SetVertexShaderConstantSAFRR(_matPower, _matAmbient);

  // Fog constants
  SetVertexShaderConstantFFARR();

  // Point and spot lights
  SetupVSConstantsPSLights();
}

/*!
  \patch 5126 Date 2/5/2007 by Flyman
  - Fixed: Integer light constants as I_POINTLOOPCOUNT were not recorded and there was a problem when using compass during the day
*/
bool EngineDDT::SetupVSConstants(int minBoneIndex, int bonesCount)
{
  if (VSToVSP[_vertexShaderID] < NVertexShaderPoolID)
  {
    SetupVSConstantsPool(minBoneIndex, bonesCount);
  }
  else if (_vertexShaderID == VSShadowVolume)
  {
    SetupVSConstantsShadowVolume(minBoneIndex, bonesCount);
  }
  else if (VSToVSS[_vertexShaderID] < NVertexShaderSpecialID)
  {
    if (_vertexShaderID == VSSprite)
    {
      SetupVSConstantsSprite();
    }
    else if (_vertexShaderID == VSWater)
    {
      SetupVSConstantsWater();
    }
    else if (_vertexShaderID == VSWaterSimple)
    {
      SetupVSConstantsWater();
    }
    else if (_vertexShaderID == VSShore)
    {
      SetupVSConstantsShore();
    }
    else if (_vertexShaderID == VSTerrainGrass)
    {
      SetupVSConstantsTerrainGrass();
    }
  }
  else if (_vertexShaderID == VSTerrain)
  {
    SetupVSConstantsTerrain();
  }
  else
  {
    Fail("Error: Some vertex shader not handled");
    return false;
  }
  return true;
}

void EngineDDT::SetupPSConstants()
{
  if (_vertexShaderID == VSNormalMap || _vertexShaderID == VSNormalMapAS || _vertexShaderID == VSNormalMapDiffuse || _vertexShaderID == VSNormalMapDiffuseAS ||
      _vertexShaderID == VSTerrain || _vertexShaderID == VSTerrainGrass ||_vertexShaderID == VSShore ||
      _vertexShaderID == VSNormalMapThrough || _vertexShaderID == VSNormalMapSpecularThrough ||
      _vertexShaderID == VSNormalMapThroughNoFade || _vertexShaderID == VSNormalMapSpecularThroughNoFade ||
      _vertexShaderID == VSWater || _vertexShaderID == VSWaterSimple)
  {
    if (_mainLight == ML_Sun)
    {
      SetPixelShaderConstantDBSR(
        _texture0MaxColorSeparate,
        _matDiffuse,
        _matForcedDiffuse,
        _matSpecular,
        _matPower);
    }
    else
    {
      SetPixelShaderConstantDBSZeroR();
    }
  }

  // Ambient shadow shader constants
  if ((_vertexShaderID == VSBasicAS) || (_vertexShaderID == VSNormalMapAS) || (_vertexShaderID == VSNormalMapDiffuseAS))
  {
    if (_mainLight == ML_Sun)
    {
      SetPixelShaderConstant_A_DFORCED_E_SunR(_texture0MaxColorSeparate, _matEmmisive, _matAmbient, _matForcedDiffuse);
    }
    else
    {
      SetPixelShaderConstant_A_DFORCED_E_NoSunR(_matEmmisive);
    }
  }
}

void EngineDDT::DrawSectionTL(const Shape &sMesh, int beg, int end, int bias, int instancesOffset, int instancesCount, const DrawParameters &dp)
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
  
  // Set bias specified for this section
  SetBiasR(bias);

#if _ENABLE_CHEATS
  // Reload shader fragments if necessary
  if (GInput.GetCheat1ToDo(DIK_NUMPADMINUS))
  {
#if _ENABLE_COMPILED_SHADER_CACHE
    Fail("DX10 TODO");
    // Direct3D 10 NEEDS UPDATE 
//     ShaderCompileCacheT cacheInit(Array<const char *>(Sources, lenof(Sources)), SBTFileName[_sbTechnique], RStringB(HLSLCacheDir));
#endif
    
    CreatePostProcessStuff();
    
    // Release current shader
    _vertexShader.Free();

    InitVertexShaders();
  }
  if (GInput.GetCheat1ToDo(DIK_NUMPADSTAR))
  {
#if _ENABLE_COMPILED_SHADER_CACHE
    Fail("DX10 TODO");
    // Direct3D 10 NEEDS UPDATE 
//     ShaderCompileCacheT cacheInit(Array<const char *>(Sources, lenof(Sources)), SBTFileName[_sbTechnique], RStringB(HLSLCacheDir));
#endif

    InitPixelShaders(_sbTechnique);
  }
#endif

  // Determine we use instancing
  const bool instancing = instancesCount>=1;

  if (_skinningType == STNone)
  {
    if (instancing)
    {
      _vertexDecl = _vertexDeclD[sMesh.GetVertexDeclaration()][VSDV_Instancing];
    }
    else
    {
      _vertexDecl = _vertexDeclD[sMesh.GetVertexDeclaration()][VSDV_Basic];
    }
  }
  else
  {
    _vertexDecl = _vertexDeclD[sMesh.GetVertexDeclaration()][VSDV_Skinning];
  }

  // ---------------------------------------------
  // Shader setting ------------------------------

  // Identify the shader to be shadow receiver or not
  bool shadowReceiver = (_pixelShaderTypeSel == PSTShadowReceiver1);

  // Crater rendering flag
  bool craterRendering = dp._cratersCount > 0;

  // Create shader (and therefore a constant table) only if necessary
  SetupSectionTL(_mainLight, _fogMode, _vertexShaderID, _uvSource, _uvSourceCount, _uvSourceOnlyTex0,
    _skinningType, instancing, instancesCount, _pointLightsCount, _spotLightsCount, shadowReceiver,
    _alphaShadowEnabled, _renderingMode, _backCullDisabled, craterRendering);

  // Return in case the shader was not created
  if (_vertexShader._shader.IsNull())
  {
    //LogF("Warning: Failed to create vertex shader");
    return;
  }

  // Set vertex declaration
  SetVertexDeclarationR(_vertexDecl);

  // Set vertex shader
  SetVertexShaderR(_vertexShader);

  // Get minBoneIndex and bonesCount of this section
  // Note that it is OK to use just the "beg" section, because we're sure all other sections have the same parameters
  const ShapeSection &sec = sMesh.GetSection(beg);

  // For VS located in the pool set the VS constants instantly
  SetupVSConstants(sec._minBoneIndex, sec._bonesCount);

  // Setup PS constants
  SetupPSConstants();

  // Set pixel shader and some pixel shader constants
  DoSelectPixelShader(dp);

  // ---------------------------------------------
  // ---------------------------------------------

//   // Drawing (can use more sections, can use more than 65536 vertices)
//   if (
//       (_pixelShaderSel == PSInterpolation) ||
//       ((_pixelShaderSel >= PSTerrain1) && (_pixelShaderSel <= PSTerrainSimple15)) ||
//       (_pixelShaderSel == PSNormalMapThrough) ||
//       (_pixelShaderSel == PSNormalMapSpecularThrough) ||
//       (_pixelShaderSel == PSNormalMapThroughSimple) ||
//       (_pixelShaderSel == PSNormalMapSpecularThroughSimple)
//      )
//   {
//     DrawIndexedPrimitiveR(beg, end, sMesh, instancesOffset, instancesCount);
//   }
//   if (_pixelShaderSel == PSWater)
//   {
//     DrawIndexedPrimitiveR(beg, end, sMesh, instancesOffset, instancesCount);
//   }
//   if (_pixelShaderSel == PSShore)
//   {
//     DrawIndexedPrimitiveR(beg, end, sMesh, instancesOffset, instancesCount);
//   }
//   if (_pixelShaderSel == PSShoreFoam)
//   {
//     DrawIndexedPrimitiveR(beg, end, sMesh, instancesOffset, instancesCount);
//   }
//   if (_pixelShaderSel == PSShoreWet)
//   {
//     DrawIndexedPrimitiveR(beg, end, sMesh, instancesOffset, instancesCount);
//   }
//   if (_pixelShaderSel == PSNormalMapSpecularDIMap)
//   {
//     DrawIndexedPrimitiveR(beg, end, sMesh, instancesOffset, instancesCount);
//   }
//   if (_pixelShaderSel == PSNormalMapMacroASSpecularDIMap)
//   {
//     DrawIndexedPrimitiveR(beg, end, sMesh, instancesOffset, instancesCount);
//   }
//   if (_pixelShaderSel == PSNormalMapMacroASSpecularMap)
//   {
//     DrawIndexedPrimitiveR(beg, end, sMesh, instancesOffset, instancesCount);
//   }

  //if ((_pixelShaderSel >= PSTerrain1) && (_pixelShaderSel <= PSTerrainSimple15))
  {
    DrawIndexedPrimitiveR(beg, end, sMesh, instancesOffset, instancesCount);
  }
}

#if 0
void EngineDDT::RememberAndVerifyHWState()
{
  // Render states
  {
    static AutoArray<RenderStateInfo> renderStateLast;
    Assert(renderStateLast.Size() <= _renderState.Size());
    if (_renderState.Size() > renderStateLast.Size())
    {
      LogF("RS: New state(s) created");
    }
    else
    {
      for (int i = 0; i < _renderState.Size(); i++)
      {
        if (renderStateLast[i].value != _renderState[i].value)
        {
          LogF("RS: Previous (%d) and new (%d) values in the RS %d do not match", renderStateLast[i].value, _renderState[i].value, i);
        }
      }
    }
    // Remember the new states
    renderStateLast.Resize(_renderState.Size());
    for (int i = 0; i < _renderState.Size(); i++)
    {
      renderStateLast[i].value = _renderState[i].value;
    }
  }

  // Sampler states
  {
    static AutoArray<SamplerStateInfo> samplerStateLast[MaxSamplerStages];
    for (int j = 0; j < MaxSamplerStages; j++)
    {
      Assert(samplerStateLast[j].Size() <= _samplerState[j].Size());
      if (_samplerState[j].Size() > samplerStateLast[j].Size())
      {
        LogF("SS: New state(s) created");
      }
      else
      {
        for (int i = 0; i < _samplerState[j].Size(); i++)
        {
          if (samplerStateLast[j][i].value != _samplerState[j][i].value)
          {
            LogF("SS: Previous (%d) and new (%d) values in the SS %d stage %d do not match", samplerStateLast[j][i].value, _samplerState[j][i].value, i, j);
          }
        }
      }
      // Remember the new states
      samplerStateLast[j].Resize(_samplerState[j].Size());
      for (int i = 0; i < _samplerState[j].Size(); i++)
      {
        samplerStateLast[j][i].value = _samplerState[j][i].value;
      }
    }
  }

  // Texture states
  {
    static AutoArray<TextureStageStateInfo> textureStageStateLast[MaxStages];
    for (int j = 0; j < MaxStages; j++)
    {
      Assert(textureStageStateLast[j].Size() <= _textureStageState[j].Size());
      if (_textureStageState[j].Size() > textureStageStateLast[j].Size())
      {
        LogF("TS: New state(s) created");
      }
      else
      {
        for (int i = 0; i < _textureStageState[j].Size(); i++)
        {
          if (textureStageStateLast[j][i].value != _textureStageState[j][i].value)
          {
            LogF("TS: Previous (%d) and new (%d) values in the TS %d stage %d do not match", textureStageStateLast[j][i].value, _textureStageState[j][i].value, i, j);
          }
        }
      }
      // Remember the new states
      textureStageStateLast[j].Resize(_textureStageState[j].Size());
      for (int i = 0; i < _textureStageState[j].Size(); i++)
      {
        textureStageStateLast[j][i].value = _textureStageState[j][i].value;
      }
    }
  }

}
#endif

void EngineDDT::DrawStencilShadows(bool interior, int quality)
{
  // Direct3D 10 NEEDS UPDATE 
//   if (!_hasStencilBuffer) return;
//   if (ResetNeeded()) return;
//   if (interior)
//   {
//     // no attenuation
//     //float pars[2]={Glob.config.shadowsZ*0.5,Glob.config.shadowsZ};
//     float pars[3]={Glob.config.shadowsZ,Glob.config.shadowsZ,quality};
//     if (_ppStencilShadowsPri)
//     {
//       _ppStencilShadowsPri->SetParameters(pars,lenof(pars));
//       _ppStencilShadowsPri->Do(false);
//     }
//   }
//   else
//   {
//     // handle primary (GPU) and secondary (CPU) shadows separately
//     // if we are rendering shadow buffers, there are no projected shadows
//     if (_ppStencilShadowsSec && !IsSBEnabled())
//     {
//       float secShadows = Glob.config.GetProjShadowsZ();
//       float secPars[3]={secShadows * 0.5f, secShadows, quality};
//       _ppStencilShadowsSec->SetParameters(secPars,lenof(secPars));
//       _ppStencilShadowsSec->Do(false);
//     }
//     if (_ppStencilShadowsPri)
//     {
//       float pars[3]={Glob.config.shadowsZ * 0.5f, Glob.config.shadowsZ, quality};
//       _ppStencilShadowsPri->SetParameters(pars,lenof(pars));
//       _ppStencilShadowsPri->Do(false);
//     }
//   }
//   //PROFILE_DX_SCOPE(3dSHA);
}

inline int FracAlpha( float a )
{
  int ia=toInt(a);
  saturate(ia,0,255);
  return ia;
}

void EngineDDT::DrawPoints( int beg, int end )
{
  if (ResetNeeded()) return;
  /*
  D3DPreparePoint();
  // TODO: queue point quads
  */
  //for( int i=beg; i<end; i++ ) QueuePoint(i);
  // now we are drawing quads directly
  for( int i=beg; i<end; i++ )
  {
    if (_mesh->Clip(i)&ClipAll) continue;

    const TLVertex &v = _mesh->GetVertex(i);
    PackedColor color=v.color;
    if( color.A8()<8 ) continue; // do not draw stars that are not visible

    // calculate alpha in TL, TR, BL, BR corners
    // v is used as TL corner
    // we have to simulate TR, BL, BR corners
    int xI = toIntFloor(v.pos[0]);
    int yI = toIntFloor(v.pos[1]);
    float xFrac = v.pos[0]-xI;
    float yFrac = v.pos[1]-yI;
    float ixFrac = 1-xFrac;
    float iyFrac = 1-yFrac;

    float a=color.A8();

    TLVertex vs[4];
    vs[0] = v; // TL
    vs[0].pos[0] = xI+0.5f;
    vs[0].pos[1] = yI+0.5f;
    vs[0].color = PackedColorRGB(color,FracAlpha(ixFrac*iyFrac*a));
    vs[0].specular = PackedColor(0);

    vs[1] = vs[0]; // TR
    vs[1].pos[0] = xI+2.5f;
    vs[1].color = PackedColorRGB(color,FracAlpha(xFrac*iyFrac*a));
    // 
    vs[2] = vs[1]; // BR
    vs[2].pos[1] = yI+2.5f;
    vs[2].color = PackedColorRGB(color,FracAlpha(xFrac*yFrac*a));

    vs[3] = vs[2]; // BL
    vs[3].pos[0] = vs[0].pos[0];
    vs[3].color = PackedColorRGB(color,FracAlpha(ixFrac*yFrac*a));

    static const VertexIndex indices[6]={0,1,2,3};

    AddVertices(vs,4);
    QueueFan(indices,4);
  }
    
}

enum {PointSpec=-2,LineSpec=-3,Line3DSpec=-4};

void EngineDDT::D3DPreparePoint()
{
  // this function is used for star rendering
  // we assume no z-buffering is used
  if( _lastSpec==PointSpec ) return;
  _lastSpec=PointSpec;

  bool someHandlesSet = false;
  for (int i = 0; i < lenof(_lastHandle); i++)
  {
    if (_lastHandle[i] != NULL)
    {
      someHandlesSet = true;
      break;
    }
  }
  if (someHandlesSet)
  {
    LogTexture("Pnt",NULL);
    SetTexture((TextureD3DT*)NULL, (TexMaterial*)NULL,0,0);
    SetTexture0Color(HWhite,HWhite,true);
  }

/*
  if( _lastHandle[0]!=NULL || _lastHandle[1]!=NULL)
  {
    //LogF("Flush: point texture change");
    LogTexture("Pnt",NULL);
    SetTexture(NULL,NULL,0);
    SetTexture0Color(HWhite,HWhite,true);
  }
*/

  PrepareSingleTexDiffuseA();

  D3DSetRenderState(D3DRS_ZFUNC_OLD,D3DCMP_ALWAYS_OLD);
  D3DSetRenderState(D3DRS_ZWRITEENABLE_OLD,FALSE);

  D3DSetRenderState(D3DRS_ALPHABLENDENABLE_OLD,TRUE);
  D3DSetRenderState(D3DRS_SRCBLEND_OLD,D3DBLEND_SRCALPHA_OLD);
  D3DSetRenderState(D3DRS_DESTBLEND_OLD,D3DBLEND_INVSRCALPHA_OLD);
  D3DSetRenderState(D3DRS_ALPHAREF_OLD,0x1);
  D3DSetRenderState(D3DRS_ALPHATESTENABLE_OLD,TRUE);
}

void EngineDDT::FogColorChanged( ColorVal fogColor )
{
  if (_d3DDevice && !ResetNeeded() /*&& _tlActive*/)
  {
    // Set the fog color
    Color fogScaled = fogColor * GetHDRFactor();
    SetPixelShaderConstantF(PSC_FogColor, (const float*)&fogScaled, 1);
  }
}


HRESULT EngineDDT::HLSLInclude::Open(
  D3D10_INCLUDE_TYPE includeType, LPCSTR fileName,
  LPCVOID parentData,
  LPCVOID * data, UINT * bytes
)
{
  // any shader files should be search for in the bin directory
  BString<512> fullName;
  strcpy(fullName,HLSLDir);
  strcat(fullName,fileName);
  
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

HRESULT EngineDDT::HLSLInclude::Close(LPCVOID data)
{
  if (data)
  {
    delete[] (char *)data;
  }
  return S_OK;
}

//! Stream with ready data - used for shader loading
class QOStrStreamData : public QOStrStream
{
public:
  //! Constructor
  QOStrStreamData(const char *fileName)
  {
    QIFStreamB in;
    in.AutoOpen(fileName);
    in.copy(*this);
    in.close();
    GFileServerFunctions->FlushReadHandle(fileName);
  }
};

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
#define D3DDECLTYPESPEC_UVDX10 DXGI_FORMAT_R16G16_SINT
#define D3DDECLTYPESPEC_VE D3DDECLTYPE_D3DCOLOR
#define D3DDECLTYPESPEC_VEDX10 DXGI_FORMAT_R8G8B8A8_UINT

static D3D10_INPUT_ELEMENT_DESC vertexDeclNonTL[] =
{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"COLOR",    0, DXGI_FORMAT_R8G8B8A8_UINT,       0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"COLOR",    1, DXGI_FORMAT_R8G8B8A8_UINT,       0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
};

// Standard vertex declarations

static D3D10_INPUT_ELEMENT_DESC vertexDeclBasic[] =
{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",   0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 1, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
  {"TANGENT",  0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
  {"TANGENT",  1, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
};

static D3D10_INPUT_ELEMENT_DESC vertexDeclNormal[] =
{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",   0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 1, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
  {"TANGENT",  0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",  1, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
};

static D3D10_INPUT_ELEMENT_DESC vertexDeclBasicUV2[] =
{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",   0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 1, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",  0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
  {"TANGENT",  1, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
};

static D3D10_INPUT_ELEMENT_DESC vertexDeclNormalUV2[] =
{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",   0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 1, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",  0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",  1, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
};

static D3D10_INPUT_ELEMENT_DESC vertexDeclNormalCustom[] =
{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",   0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",  0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",  1, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"POSITION", 1, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
};

static D3D10_INPUT_ELEMENT_DESC vertexDeclShadowVolume[] =
{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",   0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
};

static D3D10_INPUT_ELEMENT_DESC vertexDeclSkinning[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      0, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      1, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
  {"TANGENT",       0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
  {"TANGENT",       1, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
  {"BLENDWEIGHT",   0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
};

static D3D10_INPUT_ELEMENT_DESC vertexDeclNormalSkinning[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      0, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      1, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
  {"TANGENT",       0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",       1, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDWEIGHT",   0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
};

static D3D10_INPUT_ELEMENT_DESC vertexDeclUV2Skinning[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      0, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      1, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",       0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
  {"TANGENT",       1, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
  {"BLENDWEIGHT",   0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
};

static D3D10_INPUT_ELEMENT_DESC vertexDeclNormalUV2Skinning[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      0, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      1, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",       0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",       1, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDWEIGHT",   0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
};

static D3D10_INPUT_ELEMENT_DESC vertexDeclShadowVolumeSkinning[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDWEIGHT",   0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"POSITION",      1, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDWEIGHT",   1, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  1, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"POSITION",      2, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDWEIGHT",   2, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  2, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
};

static D3D10_INPUT_ELEMENT_DESC vertexDeclInstancing[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      0, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      1, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
  {"TANGENT",       0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
  {"TANGENT",       1, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
};

static D3D10_INPUT_ELEMENT_DESC vertexDeclNormalInstancing[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      0, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      1, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
  {"TANGENT",       0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",       1, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
};

static D3D10_INPUT_ELEMENT_DESC vertexDeclUV2Instancing[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      0, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      1, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",       0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
  {"TANGENT",       1, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0}, // Dummy
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
};

static D3D10_INPUT_ELEMENT_DESC vertexDeclNormalUV2Instancing[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      0, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD",      1, D3DDECLTYPESPEC_UVDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",       0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"TANGENT",       1, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
};

static D3D10_INPUT_ELEMENT_DESC vertexDeclShadowVolumeInstancing[] =
{
  {"POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,      0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"NORMAL",        0, D3DDECLTYPESPEC_VEDX10,           0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
  {"BLENDINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,        0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
};

static D3D10_INPUT_ELEMENT_DESC vertexDeclSpriteInstancing[] =
{
  {"TEXCOORD", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, D3D10_APPEND_ALIGNED_ELEMENT, D3D10_INPUT_PER_VERTEX_DATA, 0},
};

#define INPUT_ELEMENT(decl) {decl, sizeof(decl)/sizeof(decl[0])}
#define INPUT_ELEMENT_NULL {NULL, 0}

static InputElements IE[][VSDVCount] =
{
  {INPUT_ELEMENT_NULL,                    INPUT_ELEMENT_NULL,                             INPUT_ELEMENT(vertexDeclSpriteInstancing)},
  {INPUT_ELEMENT_NULL,                    INPUT_ELEMENT_NULL,                             INPUT_ELEMENT_NULL},
  {INPUT_ELEMENT(vertexDeclBasic),        INPUT_ELEMENT(vertexDeclSkinning),              INPUT_ELEMENT(vertexDeclInstancing)},
  {INPUT_ELEMENT(vertexDeclNormal),       INPUT_ELEMENT(vertexDeclNormalSkinning),        INPUT_ELEMENT(vertexDeclNormalInstancing)},
  {INPUT_ELEMENT(vertexDeclBasicUV2),     INPUT_ELEMENT(vertexDeclUV2Skinning),           INPUT_ELEMENT(vertexDeclUV2Instancing)},
  {INPUT_ELEMENT(vertexDeclNormalUV2),    INPUT_ELEMENT(vertexDeclNormalUV2Skinning),     INPUT_ELEMENT(vertexDeclNormalUV2Instancing)},
  {INPUT_ELEMENT(vertexDeclNormalCustom), INPUT_ELEMENT_NULL,                             INPUT_ELEMENT_NULL},
  {INPUT_ELEMENT_NULL,                    INPUT_ELEMENT_NULL,                             INPUT_ELEMENT_NULL},
  {INPUT_ELEMENT_NULL,                    INPUT_ELEMENT_NULL,                             INPUT_ELEMENT_NULL},
  {INPUT_ELEMENT_NULL,                    INPUT_ELEMENT_NULL,                             INPUT_ELEMENT_NULL},
  {INPUT_ELEMENT_NULL,                    INPUT_ELEMENT_NULL,                             INPUT_ELEMENT_NULL},
  {INPUT_ELEMENT(vertexDeclShadowVolume), INPUT_ELEMENT(vertexDeclShadowVolumeSkinning),  INPUT_ELEMENT(vertexDeclShadowVolumeInstancing)},
  {INPUT_ELEMENT_NULL,                    INPUT_ELEMENT_NULL,                             INPUT_ELEMENT_NULL},
};
namespace Unique2
{
  COMPILETIME_COMPARE(lenof(IE), VDCount)
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
};

//! Enumerate program definitions for vertex shader compilation
#define PRGDEF_ENUM(type,prefix,XX) \
  XX(type, prefix, PRGDEFName) \
  XX(type, prefix, PRGDEFSTPresent) \
  XX(type, prefix, PRGDEFUVMapTex1) \
  XX(type, prefix, PRGDEFMainLight) \
  XX(type, prefix, PRGDEFRenderingMode) \
  XX(type, prefix, PRGDEFSkinning) \
  XX(type, prefix, PRGDEFInstancing) \

//! Create enum and array of the same names
#ifndef DECL_ENUM_TPRGDEF
#define DECL_ENUM_TPRGDEF
DECL_ENUM(TPRGDef)
#endif
DECLARE_DEFINE_ENUM_STATIC(TPRGDef, , PRGDEF_ENUM)

//! Define array of VertexShaderPoolID names
DEFINE_ENUM_STATIC(VertexShaderPoolID, VSP, VSP_ENUM)

//! Array for conversion from VS Pool to VS
const VertexShaderID VSPToVS[] =
{
  VSBasic,
  VSNormalMap,
  VSNormalMapDiffuse,
  VSNormalMapThrough,
  VSBasicAS,
  VSNormalMapAS,
  VSNormalMapDiffuseAS,
  VSGlass,
  VSNormalMapSpecularThrough,
  VSNormalMapThroughNoFade,
  VSNormalMapSpecularThroughNoFade,
};
namespace Unique3
{
  COMPILETIME_COMPARE(lenof(VSPToVS),NVertexShaderPoolID)
}

//! Array to inform shader wheter ST are required or not
const int STIsPresent[] =
{
  0,
  1,
  1,
  1,
  0,
  1,
  1,
  0,
  1,
  1,
  1,
};
namespace Unique4
{
  COMPILETIME_COMPARE(lenof(STIsPresent),NVertexShaderPoolID)
}

void EngineDDT::InitVertexShaders(VertexShaderID vsID)
{
  // Create a compilation flag
  int flags = D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY | D3D10_SHADER_DEBUG;

  // Create NonTL shader and declaration
  InputElements ie;
  ie._ied = vertexDeclNonTL;
  ie._size = sizeof(vertexDeclNonTL)/sizeof(vertexDeclNonTL[0]);
  CreateShader(QOStrStreamData(FPShaders), NULL, "VSNonTL", "vs_4_0", flags, ie, _vertexShaderNonTL);

  // Terrain
  {
    // Prepare definition array
    D3D10_SHADER_MACRO defines[2];
    defines[1].Name = NULL;
    defines[1].Definition = NULL;

    // Create the shader
    for (int rm = 0; rm < SHP_RenderingMode; rm++)
    {
      const char *rmName[SHP_RenderingMode] = {"0", "1", "2", "3"};
      defines[0].Name = FindEnumName(PRGDEFRenderingMode);
      defines[0].Definition = rmName[rm];
      CreateShader(QOStrStreamData(VSTerrainFile), defines, "VSTerrain", "vs_4_0", flags, IE[ShaderVertexDeclarations[VSTerrain]][VSDV_Basic], _vertexShaderTerrain[rm]);
    }
  }

  // Sprite
  CreateShader(QOStrStreamData(VSSpriteFile), NULL, "VSSprite", "vs_4_0", flags, IE[ShaderVertexDeclarations[VSSprite]][VSDV_Instancing], _vertexShaderSpecial[VSSSprite]);

  // Water
  CreateShader(QOStrStreamData(VSWaterFile), NULL, "VSWater", "vs_4_0", flags, IE[ShaderVertexDeclarations[VSWater]][VSDV_Basic], _vertexShaderSpecial[VSSWater]);

  // WaterSimple
  CreateShader(QOStrStreamData(VSWaterFile), NULL, "VSWater", "vs_4_0", flags, IE[ShaderVertexDeclarations[VSWater]][VSDV_Basic], _vertexShaderSpecial[VSSWaterSimple]);

  // Shore
  CreateShader(QOStrStreamData(VSShoreFile), NULL, "VSShore", "vs_4_0", flags, IE[ShaderVertexDeclarations[VSShore]][VSDV_Basic], _vertexShaderSpecial[VSSShore]);

  // TerrainGrass
  CreateShader(QOStrStreamData(VSTerrainGrassFile), NULL, "VSTerrainGrass", "vs_4_0", flags, IE[ShaderVertexDeclarations[VSTerrainGrass]][VSDV_Basic], _vertexShaderSpecial[VSSTerrainGrass]);

  // Shadow volume
  {
    QOStrStreamData hlslSource(VSShadowVolumeFile);
    CreateShader(hlslSource, NULL, "VSShadowVolume",          "vs_4_0", flags, IE[ShaderVertexDeclarations[VSShadowVolume]][VSDV_Basic],      _vertexShaderShadowVolume[0]);
    CreateShader(hlslSource, NULL, "VSShadowVolumeSkinned",   "vs_4_0", flags, IE[ShaderVertexDeclarations[VSShadowVolume]][VSDV_Skinning],   _vertexShaderShadowVolume[1]);
    CreateShader(hlslSource, NULL, "VSShadowVolumeInstanced", "vs_4_0", flags, IE[ShaderVertexDeclarations[VSShadowVolume]][VSDV_Instancing], _vertexShaderShadowVolume[2]);
  }

  // Common shaders
  {
    // Get the HLSL source stream
    QOStrStreamData hlslSource(VSFile);

    // Prepare array of defines to be passed to the compilation
    D3D10_SHADER_MACRO defines[NTPRGDef + 1];
    defines[NTPRGDef].Name = NULL;
    defines[NTPRGDef].Definition = NULL;

    // Create all possible compilation combinations
    for (int vs = 0; vs < SHP_VertexShaderID; vs++)
    {
      // If the vertex shader is not the one we want to compile, then continue
      if (vsID < NVertexShaderID && vs != vsID) continue;

      const char *stPresentValues[2] = {"0", "1"};
      defines[PRGDEFName].Name = FindEnumName(PRGDEFName);
      defines[PRGDEFName].Definition = FindEnumName((VertexShaderPoolID)vs);
      defines[PRGDEFSTPresent].Name = FindEnumName(PRGDEFSTPresent);
      defines[PRGDEFSTPresent].Definition = stPresentValues[STIsPresent[vs]];

      for (int uvi = 0; uvi < SHP_UVInput; uvi++)
      {
        const char *uviName[SHP_UVInput] = {"0", "1"};
        defines[PRGDEFUVMapTex1].Name = FindEnumName(PRGDEFUVMapTex1);
        defines[PRGDEFUVMapTex1].Definition = uviName[uvi];

        for (int ml = 0; ml < SHP_MainLight; ml++)
        {
          const char *mlName[SHP_MainLight] = {"0", "1", "2", "3"};
          defines[PRGDEFMainLight].Name = FindEnumName(PRGDEFMainLight);
          defines[PRGDEFMainLight].Definition = mlName[ml];

          for (int rm = 0; rm < SHP_RenderingMode; rm++)
          {
            const char *rmName[SHP_RenderingMode] = {"0", "1", "2", "3"};
            defines[PRGDEFRenderingMode].Name = FindEnumName(PRGDEFRenderingMode);
            defines[PRGDEFRenderingMode].Definition = rmName[rm];

            for (int si = 0; si < SHP_SkinningInstancing; si++)
            {
              const char *skin[SHP_SkinningInstancing] = {"0", "1", "0"};
              const char *inst[SHP_SkinningInstancing] = {"0", "0", "1"};
              defines[PRGDEFSkinning].Name = FindEnumName(PRGDEFSkinning);
              defines[PRGDEFSkinning].Definition = skin[si];
              defines[PRGDEFInstancing].Name = FindEnumName(PRGDEFInstancing);
              defines[PRGDEFInstancing].Definition = inst[si];

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

//               // Skip shaders but one
//               if ((VertexShaderPoolID)vs != VSPBasic ||
//                   uvi != 1 ||
//                   ml != 3 ||
//                   rm != 0 ||
//                   si != 0)
//               {
//                 continue;
//               }

              // Create the shader
              CreateShader(hlslSource, defines, "VSShaderPool", "vs_4_0", flags, IE[UV2[ShaderVertexDeclarations[VSPToVS[vs]]][uvi]][si], _vertexShaderPool[vs][uvi][ml][rm][si]);
            }
          }
        }
      }
    }
  }
}

void EngineDDT::DeinitVertexShaders()
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
  _vertexShaderNonTL.Free();
}

void EngineDDT::ReloadVertexShader(VertexShaderID vs)
{
  CreatePostProcessStuff();

  // Release the current shader
  _vertexShader.Free();

  InitVertexShaders(vs);
}

/// pixel shader identification
struct PSName
{
  const char *name;
  const DWORD *data;
};

/// short-term cache - avoid the same shader being compiled twice
struct PSCache
{
  PSName name;
  ComRef<ID3D10PixelShader> ps;
};
TypeIsMovable(PSCache)

template <>
struct FindArrayKeyTraits<PSCache>
{
  typedef const PSName &KeyType;
  /// check if two keys are equal
  static bool IsEqual(KeyType a, KeyType b)
  {
    return !strcmp(a.name ? a.name : "",b.name ? b.name : "") && a.data==b.data;
  }
  /// get a key from an item
  static KeyType GetKey(const PSCache &a) {return a.name;}
};

void EngineDDT::InitPixelShaders(SBTechnique sbTechnique)
{
  #if _ENABLE_REPORT
    DWORD start = ::GetTickCount();
  #endif
  Debugger::PauseCheckingScope scope(GDebugger);
  
  // Get the HLSL source stream (used in PSPairHL macro)
  QOStrStreamData hlslSource(PSFile);

  // simplified adding of whole lines
  #define PS_VARIANTS(name) \
      {PSPairHL(name),PSPairHL(NA##name),PSPairHL(SR##name),PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)}
  // simplified adding of whole lines
  #define PS_NOVARIANTS(name) \
      {PSPairHL(name),PSPairHL(name),PSPairHL(name),PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)}
  /// simplified adding of whole lines
  #define PS_DUMMY {PSPairDummy,PSPairDummy,PSPairDummy,PSPairDummy,PSPairDummy}

  /// adding of a terrain shader family
  #define TERRAIN_LINES(base) \
    PS_VARIANTS(base##1),PS_VARIANTS(base##2),PS_VARIANTS(base##3), \
    PS_VARIANTS(base##4),PS_VARIANTS(base##5),PS_VARIANTS(base##6),PS_VARIANTS(base##7), \
    PS_VARIANTS(base##8),PS_VARIANTS(base##9),PS_VARIANTS(base##10),PS_VARIANTS(base##11), \
    PS_VARIANTS(base##12),PS_VARIANTS(base##13),PS_VARIANTS(base##14),PS_VARIANTS(base##15)

  #define CRATER_LINES \
    PS_NOVARIANTS(Crater1), PS_NOVARIANTS(Crater2), PS_NOVARIANTS(Crater3), PS_NOVARIANTS(Crater4), PS_NOVARIANTS(Crater5), \
    PS_NOVARIANTS(Crater6), PS_NOVARIANTS(Crater7), PS_NOVARIANTS(Crater8), PS_NOVARIANTS(Crater9), PS_NOVARIANTS(Crater10), \
    PS_NOVARIANTS(Crater11),PS_NOVARIANTS(Crater12),PS_NOVARIANTS(Crater13),PS_NOVARIANTS(Crater14)

  #define PSPairHL(name) {"PS" #name,NULL}
  #define PSPairDummy {NULL, NULL}
  static const PSName psNames[NPixelShaderSpecular][NPixelShaderID][NPixelShaderType] =
  {
    { // specular shaders
      PS_VARIANTS(SpecularAlpha),
      PS_VARIANTS(NormalDXTA),
      PS_VARIANTS(NormalMap),
      PS_VARIANTS(SpecularNormalMapThrough),
      PS_VARIANTS(SpecularNormalMapGrass),
      PS_VARIANTS(SpecularNormalMapDiffuse),
      PS_VARIANTS(DetailSpecularAlpha),
      PS_NOVARIANTS(Interpolation), // use for sky, no shadow buffer support
      PS_NOVARIANTS(Water),
      PS_NOVARIANTS(WaterSimple),
      PS_NOVARIANTS(White), // special purpose shader, no shadow buffer support
      PS_NOVARIANTS(WhiteAlpha), // special purpose shader, no shadow buffer support
      PS_NOVARIANTS(Reflect), // 2nd pass reflection - no shadow buffer support
      PS_NOVARIANTS(ReflectNoShadow), // 2nd pass reflection - no shadow buffer support
      PS_DUMMY,
      {PSPairHL(DetailSpecularAlphaMacroAS),           PSPairHL(NADetailSpecularAlphaMacroAS),           PSPairHL(SRDetailSpecularAlphaMacroAS),           PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapMacroAS),                     PSPairHL(NANormalMapMacroAS),                     PSPairHL(SRNormalMapMacroAS),                     PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(SpecularNormalMapDiffuseMacroAS),      PSPairHL(NASpecularNormalMapDiffuseMacroAS),      PSPairHL(SRSpecularNormalMapDiffuseMacroAS),      PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapSpecularMap),                 PSPairHL(NANormalMapSpecularMap),                 PSPairHL(SRNormalMapSpecularMap),                 PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapDetailSpecularMap),           PSPairHL(NANormalMapDetailSpecularMap),           PSPairHL(SRNormalMapDetailSpecularMap),           PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapMacroASSpecularMap),          PSPairHL(NANormalMapMacroASSpecularMap),          PSPairHL(SRNormalMapMacroASSpecularMap),          PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapDetailMacroASSpecularMap),    PSPairHL(NANormalMapDetailMacroASSpecularMap),    PSPairHL(SRNormalMapDetailMacroASSpecularMap),    PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapSpecularDIMap),               PSPairHL(NANormalMapSpecularDIMap),               PSPairHL(SRNormalMapSpecularDIMap),               PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapDetailSpecularDIMap),         PSPairHL(NANormalMapDetailSpecularDIMap),         PSPairHL(SRNormalMapDetailSpecularDIMap),         PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapMacroASSpecularDIMap),        PSPairHL(NANormalMapMacroASSpecularDIMap),        PSPairHL(SRNormalMapMacroASSpecularDIMap),        PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapDetailMacroASSpecularDIMap),  PSPairHL(NANormalMapDetailMacroASSpecularDIMap),  PSPairHL(SRNormalMapDetailMacroASSpecularDIMap),  PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      TERRAIN_LINES(Terrain),
      TERRAIN_LINES(TerrainSimple),
      PS_VARIANTS(Glass),
      PS_NOVARIANTS(NonTL), // special purpose shader, no shadow buffer support
      PS_VARIANTS(SpecularNormalMapSpecularThrough),
      PS_VARIANTS(Grass),
      PS_VARIANTS(SpecularNormalMapThroughSimple),
      PS_VARIANTS(SpecularNormalMapSpecularThroughSimple),
      PS_VARIANTS(Road),
      PS_NOVARIANTS(Shore),
      PS_NOVARIANTS(ShoreWet),
      PS_VARIANTS(Road2Pass),
      PS_NOVARIANTS(ShoreFoam),
      PS_NOVARIANTS(NonTLFlare),
      PS_VARIANTS(NormalMapThroughLowEnd),
      TERRAIN_LINES(TerrainGrass),
      CRATER_LINES,
    },
    { // non-specular shaders
      PS_VARIANTS(Normal),
      PS_VARIANTS(NormalDXTA),
      PS_VARIANTS(NormalMap),
      PS_VARIANTS(SpecularNormalMapThrough),
      PS_VARIANTS(SpecularNormalMapGrass),
      PS_VARIANTS(SpecularNormalMapDiffuse),
      PS_VARIANTS(Detail),
      PS_NOVARIANTS(Interpolation), // use for sky, no shadow buffer support
      PS_NOVARIANTS(Water),
      PS_NOVARIANTS(WaterSimple),
      PS_NOVARIANTS(White), // special purpose shader, no shadow buffer support
      PS_NOVARIANTS(WhiteAlpha), // special purpose shader, no shadow buffer support
      PS_NOVARIANTS(Reflect), // 2nd pass reflection - no shadow buffer support
      PS_NOVARIANTS(ReflectNoShadow), // 2nd pass reflection - no shadow buffer support
      PS_DUMMY,
      {PSPairHL(DetailMacroAS),                        PSPairHL(NADetailMacroAS),                        PSPairHL(SRDetailMacroAS),                        PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapMacroAS),                     PSPairHL(NANormalMapMacroAS),                     PSPairHL(SRNormalMapMacroAS),                     PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(SpecularNormalMapDiffuseMacroAS),      PSPairHL(NASpecularNormalMapDiffuseMacroAS),      PSPairHL(SRSpecularNormalMapDiffuseMacroAS),      PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapSpecularMap),                 PSPairHL(NANormalMapSpecularMap),                 PSPairHL(SRNormalMapSpecularMap),                 PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapDetailSpecularMap),           PSPairHL(NANormalMapDetailSpecularMap),           PSPairHL(SRNormalMapDetailSpecularMap),           PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapMacroASSpecularMap),          PSPairHL(NANormalMapMacroASSpecularMap),          PSPairHL(SRNormalMapMacroASSpecularMap),          PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapDetailMacroASSpecularMap),    PSPairHL(NANormalMapDetailMacroASSpecularMap),    PSPairHL(SRNormalMapDetailMacroASSpecularMap),    PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapSpecularDIMap),               PSPairHL(NANormalMapSpecularDIMap),               PSPairHL(SRNormalMapSpecularDIMap),               PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapDetailSpecularDIMap),         PSPairHL(NANormalMapDetailSpecularDIMap),         PSPairHL(SRNormalMapDetailSpecularDIMap),         PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapMacroASSpecularDIMap),        PSPairHL(NANormalMapMacroASSpecularDIMap),        PSPairHL(SRNormalMapMacroASSpecularDIMap),        PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      {PSPairHL(NormalMapDetailMacroASSpecularDIMap),  PSPairHL(NANormalMapDetailMacroASSpecularDIMap),  PSPairHL(SRNormalMapDetailMacroASSpecularDIMap),  PSPairHL(ShadowBufferAlpha), PSPairHL(InvDepthAlphaTest)},
      TERRAIN_LINES(Terrain),
      TERRAIN_LINES(TerrainSimple),
      PS_VARIANTS(Glass),
      PS_NOVARIANTS(NonTL), // special purpose shader, no shadow buffer support
      PS_VARIANTS(SpecularNormalMapSpecularThrough),
      PS_VARIANTS(Grass),
      PS_VARIANTS(SpecularNormalMapThroughSimple),
      PS_VARIANTS(SpecularNormalMapSpecularThroughSimple),
      PS_VARIANTS(Road),
      PS_NOVARIANTS(Shore),
      PS_NOVARIANTS(ShoreWet),
      PS_VARIANTS(Road2Pass),
      PS_NOVARIANTS(ShoreFoam),
      PS_NOVARIANTS(NonTLFlare),
      PS_VARIANTS(NormalMapThroughLowEnd),
      TERRAIN_LINES(TerrainGrass),
      CRATER_LINES,
    },
  };

  FindArrayKey<PSCache> cache;

  for (int s=0; s<NPixelShaderSpecular; s++)
  for (int i=0; i<NPixelShaderID; i++)
  for (int t=0; t<NPixelShaderType; t++)
  {
    const PSName &psName = psNames[s][i][t];
    int index = cache.FindKey(psName);
    if (index>=0)
    {
      _pixelShader[s][i][t] = cache[index].ps;
    }
    else
    {
      DoAssert(psName.data == NULL); // To be removed later
      const DWORD *psData = NULL;
      DWORD psDataSize = -1;
      ComRef<ID3D10Blob> shader;
      if (!psData && (psName.name != NULL))
      {
        const char *name = psName.name;
        int flags = D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY; //D3DXSHADER_PARTIALPRECISION;
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
              case SBT_Default:
              case SBT_ATIDF16:
              case SBT_ATIDF24:
              default:
                strcat(psFullName,"_Default");
                break;
            }
            break;
          case PSTShadowReceiver1:
            strcpy(psFullName,name);
            // some shader may be unused / undefined
            // we cannot add suffix to them
            if (strncmp(name,"PSSR",4)==0) switch (sbTechnique)
            {
              case SBT_nVidia:
                strcat(psFullName,"_nVidia");
                break;
              case SBT_Default:
              case SBT_ATIDF16:
              case SBT_ATIDF24:
              default:
                // shadow buffer texcoord calculations require full precision
                //flags = 0;
                strcat(psFullName,"_Default");
                break;
            }
            break;
          default:
            strcpy(psFullName,name);
            break;
        }
        if (t==PSTShadowReceiver1 && sbTechnique!=SBT_nVidia)
        {
          // shadow buffer sampling requires full precision
          //flags = 0;
        }
        
        ComRef<ID3D10Blob> errorMsgs;

// #ifdef D3DXSHADER_USE_LEGACY_D3DX9_31_DLL
//         flags |= D3DXSHADER_USE_LEGACY_D3DX9_31_DLL;
// #endif

        HRESULT ok = D3D10CompileShader(
          hlslSource.str(), hlslSource.pcount(),
          NULL, NULL, &_hlslInclude, psFullName, "ps_4_0", flags, shader.Init(), errorMsgs.Init());
        if (FAILED(ok))
        {
          LogF("Error: %x in D3DXCompileShader while compiling the %s pixel shader", ok, cc_cast(psFullName));
          if (errorMsgs.NotNull())
          {
            LogF("%s", (const char*)errorMsgs->GetBufferPointer());
          }
          ErrorMessage("Error compiling pixel shader %s",cc_cast(psFullName));
        }
        else
        {
          psData = (const DWORD *)shader->GetBufferPointer();
          psDataSize = shader->GetBufferSize();
        }
      }
      if (psData)
      {
        ComRef<ID3D10PixelShader> ps;
        HRESULT hr = _d3DDevice->CreatePixelShader(psData, psDataSize, ps.Init());
        if (FAILED(hr) || ps.IsNull())
        {
          RStringB psEnumName = FindEnumName(PixelShaderID(i));
          ErrF("Cannot create pixel shader %d,%s, error %x",s,cc_cast(psEnumName),hr);
        }
        else
        {
          PSCache &item = cache.Append();
          item.name = psName;
          item.ps = ps;
          _pixelShader[s][i][t] = ps;
        }
      }
    }
  }

  // Set a basic pixel shader (for example on XBOX a PS must be set before setting of PS constant)
  SelectPixelShaderSpecular(PSSNormal);
  SelectPixelShader(PSNormal);
  SelectPixelShaderType(PSTBasic);
  DoSelectPixelShader();
  #if _ENABLE_REPORT
    DWORD time = ::GetTickCount()-start;
    LogF("InitPixelShaders: %d ms",time);
  #endif
}


void EngineDDT::DeinitPixelShaders()
{
  SelectPixelShader(PSNone);
  DoSelectPixelShader();
  //DoSelectPixelShader(PSNone,PSMDay,PSSSpecular);
  for (int s=0; s<NPixelShaderSpecular; s++)
  for (int i=0; i<NPixelShaderID; i++)
  for (int t=0; t<NPixelShaderType; t++)
  {
     if (_pixelShader[s][i][t].NotNull()) _pixelShader[s][i][t].Free();
  }
}


void EngineDDT::DoSelectPixelShader(const DrawParameters &dp)
{
  Assert(_pixelShaderSpecularSel != PSSUninitialized);
  Assert(_pixelShaderSel != PSUninitialized);
  Assert(_pixelShaderTypeSel != PSTUninitialized);
  if (_pixelShaderSel < PSNone)
  {
    // Get the pixel shader
    PixelShaderID pixelShaderSel;
    if (dp._cratersCount > 0)
    {
      DoAssert(dp._cratersCount <= 14);
      // Use crater visualization shader
      pixelShaderSel = (PixelShaderID)(PSCrater1 + dp._cratersCount - 1);
    }
    else if (GetLowEndQuality())
    {
      // Replace shader with low-end version here
      switch (_pixelShaderSel)
      {
      case PSNormalMapThrough:
      case PSNormalMapThroughSimple:
      case PSNormalMapSpecularThrough:
      case PSNormalMapSpecularThroughSimple:
        pixelShaderSel = PSNormalMapThroughLowEnd;
        break;
      default:
        pixelShaderSel = _pixelShaderSel;
      }
    }
    else
    {
      // Use original shader
      pixelShaderSel = _pixelShaderSel;
    }

    SetPixelShaderR(_pixelShader[_pixelShaderSpecularSel][pixelShaderSel][_pixelShaderTypeSel]);

    if ((pixelShaderSel == PSWater) || (pixelShaderSel == PSShore))
    {
      SetPixelShaderConstantWaterR();
    }
    else if (pixelShaderSel == PSWaterSimple)
    {
      SetPixelShaderConstantWaterSimpleR();
    }
    else if (pixelShaderSel == PSGlass)
    {
      SetPixelShaderConstantGlassR(_matSpecular);
    }
    /*
    else if (pixelShaderSel>=PSTerrainGrass1 && pixelShaderSel<=PSTerrainGrass15)
    {
      float scale = _sceneProps->GetSatScale();
      float data[4]={scale,0,0,0};
      // constant 
      SetPixelShaderConstantR(PS_SATSCALE,data);
      // TODO: use pushbuffer
    }
    */
//    else if ((pixelShaderSel == PSNormalMapThrough) || (pixelShaderSel == PSNormalMapSpecularThrough))
//    {
//      SetTextureR(4, _textBank->GetRandomTexture());
//    }
  }
  else
  {
    const ComRef<ID3D10PixelShader> empty;
    SetPixelShaderR(empty);
  }
}

void EngineDDT::EnableNightEye(float night)
{
  if (_nightVision) night = 0;
  // select normal or night pixel shader
  if (fabs(_nightEye-night)<0.01f) return;
  FlushQueues();
  _nightEye = night;
}

void EngineDDT::EnableSBShadows(int quality)
{
  return;
  // Direct3D 10 NEEDS UPDATE 
//   if (_sbTechnique==SBT_nVidia)
//   {
//     // only nVidia is able to handle HQ shadow buffers
//   }
//   else
//   {
//     if (quality>1) quality = 1;
//   }
//   if (quality==_sbQuality) return;
//   _sbQuality = quality;
//   if (quality>0)
//   {
//     // avoid creating the surface in AGP memory
//     // for this we need to make sure device VRAM is free
//     if (_textBank->GetVRamTexAllocation()>0 || _vBuffersAllocated>0)
//     {
//       /* we might even perform full or partial Reset() here
//       PreReset(false);
//       // PostReset will perform CreateSBResources();
//       PostReset();
//       */
//       
//       DisplayWaitScreen();
//       
//       _textBank->ReleaseAllTextures();
//       ReleaseAllVertexBuffers();
//       // pretend the reset was done - this will make sure preload is done
//       _resetDone = true;
//     }
//     CreateSBResources();
//   }
//   else
//   {
//     DestroySBResources();
//   }
//   _textBank->ResetMaxTextureQuality();
}

bool EngineDDT::IsSBEnabled() const
{
  return false;
  // Direct3D 10 NEEDS UPDATE 
//  return _renderTargetDepthBufferSurfaceSB.NotNull();
}

void EngineDDT::CreateSBResources()
{
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
// 
//   switch (_sbTechnique)
//   {
//     case SBT_Default:
//       SBTR_DefaultCreate();
//       break;
//     case SBT_nVidia:
//       SBTR_nVidiaCreate();
//       break;
//     case SBT_ATIDF16:
//       SBTR_ATID16Create();
//       break;
//     case SBT_ATIDF24:
//       SBTR_ATID24Create();
//       break;
//     default:
//       break;
//   }
}

void EngineDDT::DestroySBResources()
{
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   _renderTargetDepthBufferSurfaceSB.Free();
//   _renderTargetDepthBufferSB.Free();
//   _renderTargetSurfaceSB.Free();
//   _renderTargetSB.Free();
}

void EngineDDT::CreateDBResources()
{
  //Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   if (_dbAvailable)
//   {
//     HRESULT err;
//     if ((err = _d3DDevice->CreateTexture(_w, _h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, _renderTargetDB.Init(), NULL)) == D3D_OK)
//     {
//       if ((err = _renderTargetDB->GetSurfaceLevel(0, _renderTargetSurfaceDB.Init())) == D3D_OK)
//       {
//         if ((err = _d3DDevice->CreateDepthStencilSurface(_w, _h, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, _renderTargetDepthStencilSurfaceDB.Init(), NULL)) == D3D_OK)
//         {
//           return;
//         }
//         else
//         {
//           DDError9("Error: Depth buffer render target depth-stencil surface creation failed", err);
//         }
//       }
//       else
//       {
//         DDError9("Error: Depth buffer render target surface creation failed", err);
//       }
//     }
//     else
//     {
//       DDError9("Error: Depth buffer render target texture creation failed", err);
//     }
//   }
//   DestroyDBResources();
}

void EngineDDT::DestroyDBResources()
{
  //Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   _renderTargetDepthStencilSurfaceDB.Free();
//   _renderTargetSurfaceDB.Free();
//   _renderTargetDB.Free();
}

static const char *SurfaceFormatName(int format)
{
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   #define FN(x) case D3DFMT_##x: return #x
//   switch (format)
//   {
//     FN(UNKNOWN);
// 
//     FN(R8G8B8);
//     FN(R3G3B2);
//     FN(A8P8);
//     FN(A4L4);
//     FN(X4R4G4B4);
//     FN(A8R3G3B2);
//     FN(X8L8V8U8);
//     FN(D32);
//     FN(D15S1);
//     FN(D24X8);
//     FN(D24X4S4);
// 
//     // following formats map to some existing format
//     FN(DXT3);
//     FN(DXT5);
//     FN(D16_LOCKABLE);
//     FN(A16B16G16R16);
//     FN(A16B16G16R16F);
//     FN(A32B32G32R32F);
//     
//     FN(R16F);
//     FN(R32F);
// 
//     FN(G16R16F);
//     FN(G32R32F);
//     FN(L16);
//     FN(G16R16);
// 
//     FN(A8R8G8B8);
//     FN(X8R8G8B8);
//     FN(R5G6B5);
//     FN(X1R5G5B5);
//     FN(A1R5G5B5);
//     FN(A4R4G4B4);
//     FN(A8);
// 
//     FN(P8);
// 
//     FN(L8);
//     FN(A8L8);
// 
//     FN(V8U8);
//     FN(L6V5U5);
//     FN(Q8W8V8U8);
//     FN(V16U16);
// 
//     FN(UYVY);
//     FN(YUY2);
//     FN(DXT1);
//     FN(DXT2);
//     FN(DXT4);
// 
//     FN(D24S8);
//     FN(D16);
//   }
  return "OTHER";
}

struct FormatNeeded
{
  /// which format
  DXGI_FORMAT fmt;
  /// which boolean tells if the format is available
  bool EngineDDT::*available;
};

struct SSupportedFormats
{
  DXGI_FORMAT _format;
  int _bpp;
};

// Direct3D 10 NEEDS UPDATE 
// format list from d3d9types.h, bpp hand filled. For some exotic formats bpp may be wrong
static SSupportedFormats KnownFormats[]=
{
//   {D3DFMT_R8G8B8               , 24},
//   {D3DFMT_A8R8G8B8             , 32},
//   {D3DFMT_X8R8G8B8             , 32},
//   {D3DFMT_R5G6B5               , 16},
//   {D3DFMT_X1R5G5B5             , 16},
//   {D3DFMT_A1R5G5B5             , 16},
//   {D3DFMT_A4R4G4B4             , 16},
//   {D3DFMT_R3G3B2               , 8},
  {DXGI_FORMAT_A8_UNORM                   , 8},
//   {D3DFMT_A8R3G3B2             , 8},
//   {D3DFMT_X4R4G4B4             , 16},
//   {D3DFMT_A2B10G10R10          , 16},
//   {D3DFMT_A8B8G8R8             , 16},
//   {D3DFMT_X8B8G8R8             , 16},
//   {D3DFMT_G16R16               , 32},
//   {D3DFMT_A2R10G10B10          , 32},
//   {D3DFMT_A16B16G16R16         , 64},
// 
//   {D3DFMT_A8P8                 , 16},
//   {D3DFMT_P8                   , 8},
// 
//   {D3DFMT_L8                   , 8},
//   {D3DFMT_A8L8                 , 16},
//   {D3DFMT_A4L4                 , 8},
// 
//   {D3DFMT_V8U8                 , 16},
//   {D3DFMT_L6V5U5               , 16},
//   {D3DFMT_X8L8V8U8             , 32},
//   {D3DFMT_Q8W8V8U8             , 32},
//   {D3DFMT_V16U16               , 32},
//   {D3DFMT_A2W10V10U10          , 32},
// 
//   {D3DFMT_UYVY                 , 32},
//   {D3DFMT_R8G8_B8G8            , 32},
//   {D3DFMT_YUY2                 , 32},
//   {D3DFMT_G8R8_G8B8            , 32},
//   {D3DFMT_DXT1                 , 4},
//   {D3DFMT_DXT2                 , 8},
//   {D3DFMT_DXT3                 , 8},
//   {D3DFMT_DXT4                 , 8},
//   {D3DFMT_DXT5                 , 8},
// 
//   {D3DFMT_D16_LOCKABLE         , 16},
//   {D3DFMT_D32                  , 32},
//   {D3DFMT_D15S1                , 16},
//   {D3DFMT_D24S8                , 32},
//   {D3DFMT_D24X8                , 32},
//   {D3DFMT_D24X4S4              , 32},
//   {D3DFMT_D16                  , 16},
// 
//   {D3DFMT_D32F_LOCKABLE        , 32},
//   {D3DFMT_D24FS8               , 32},
// 
//   {D3DFMT_L16                  , 16},
// 
//   {D3DFMT_VERTEXDATA           , 1},
//   {D3DFMT_INDEX16              , 16},
//   {D3DFMT_INDEX32              , 32},
// 
//   {D3DFMT_Q16W16V16U16         , 64},
// 
//   {D3DFMT_MULTI2_ARGB8         , 32},
// 
//   {D3DFMT_R16F                 , 16},
//   {D3DFMT_G16R16F              , 32},
//   {D3DFMT_A16B16G16R16F        , 64},
// 
//   {D3DFMT_R32F                 , 32},
//   {D3DFMT_G32R32F              , 64},
//   {D3DFMT_A32B32G32R32F        , 128},
// 
//   {D3DFMT_CxV8U8               , 16},
};

static int FormatToBpp(DXGI_FORMAT format)
{
  for (int i = 0; i < lenof(KnownFormats); i++) {
    if (KnownFormats[i]._format == format) return KnownFormats[i]._bpp;
  }
  return 0;
}

void EngineDDT::Init3DState()
{
  // Direct3D 10 NEEDS UPDATE 
//   DWORD texMemBeforeInit = _d3DDevice->GetAvailableTextureMem();
//   HRESULT err;

  _matC = 0, _matD = 0; // w-buffer limits set
  _bias = 0;
  _clipANearEnabled = false,_clipAFarEnabled = false;
  _sbTechnique = SBT_None;
  _dbAvailable = false;

  // All DXT formats are supported
  _dxtFormats = 0;
  _dxtFormats |= 1<<1;
  _dxtFormats |= 1<<2;
  _dxtFormats |= 1<<3;
  _dxtFormats |= 1<<4;
  _dxtFormats |= 1<<5;

  // _d3DDevice->GetRenderTarget(0, _backBuffer.Init()); // Direct3D 10 NEEDS UPDATE 

  // Direct3D 10 NEEDS UPDATE 
//   // Remember current depth stencil surface of previously created or reseted device
//   _d3DDevice->GetDepthStencilSurface(_currRenderTargetDepthBufferSurface.Init());
//
//   D3DSURFACE_DESC bdesc;
//   err=_backBuffer->GetDesc(&bdesc);
//   if (err != D3D_OK) {DDError9("GetSurfaceDesc failed.",err);return;}
// 
//   Assert(_rtAllocated==0);
//   // front-buffer+back-buffer+depth-buffer
//   _rtAllocated += (2*FormatToBpp(bdesc.Format)+FormatToBpp(_pp.AutoDepthStencilFormat))*(bdesc.Width*bdesc.Height/8);
// 
// 
//   // we can verify local/nonlocal numbers now
// 
//   // if we are up to 30 MB wrong, we assume the information is probably valid
//   
//   const int oneMB = 1024*1024;
//   DWORD totalByDX7 = _localVramAmount+_nonlocalVramAmount;  
//   DWORD totalByDX9 = _rtAllocated+texMemBeforeInit;
//   
//   // DX9 may report some less memory, as there may be some desktop / additional monitor
//   
//   if (totalByDX9<totalByDX7-8*oneMB || totalByDX9>totalByDX7+32*oneMB)
//   {
//     // we need to fix our estimates
//     // be pessimistic and assume the non-local number is correct
//     _localVramAmount = totalByDX9 - _nonlocalVramAmount;
//     // only when such result is not satisfactory, replace it with something else
//     if (_localVramAmount<32*oneMB)
//     {
//       // use heuristics - assume half is VRAM and half AGP
//       _localVramAmount = totalByDX9/2;
//       _nonlocalVramAmount = totalByDX9-_localVramAmount;
//     }
//   }


  // Direct3D 10 NEEDS UPDATE 
//   // Create the render target texture
//   #ifdef DEBUG_SURF
//     int rtUsage = D3DUSAGE_RENDERTARGET;
//   #else
//     int rtUsage = D3DUSAGE_RENDERTARGET;
//   #endif

// Direct3D 10 NEEDS UPDATE 
//   // Remember depth buffer associated with the device
//   _d3DDevice->GetDepthStencilSurface(_depthBuffer.Init());

// Direct3D 10 NEEDS UPDATE 
//   if (_rtMultiSampleType!=D3DMULTISAMPLE_NONE)
//   {
//     err = _d3DDevice->CreateRenderTarget(
//       bdesc.Width, bdesc.Height, _rtFormat,
//       _rtMultiSampleType, _rtMultiSampleQuality, FALSE, _renderTargetSurfaceS.Init(), NULL
//     );
//     if (err != D3D_OK)
//     {
//       DDError9("Error: Render target surface creation failed.", err);
//       return;
//     }

// Direct3D 10 NEEDS UPDATE 
//     err = _d3DDevice->CreateDepthStencilSurface(
//       bdesc.Width, bdesc.Height,D3DFMT_D24S8,
//       _rtMultiSampleType, _rtMultiSampleQuality, TRUE,
//       _depthBufferRT.Init(),NULL
//     );
//     if (err != D3D_OK)
//     {
//       DDError9("Error: Depth buffer creation failed.", err);
//       return;
//     }

//    static const int assumedSamples[4]={2,4,6,8}
//    int index = _rtMultiSampleQuality-1;
//    saturate(index,0,lenof(assumedSamples)-1);
//    // assume 32bpp AA back-buffer + 32bpp AA depth-buffer
//    int aaSamples = assumedSamples[index];
//      (FormatToBpp(_rtFormat)+FormatToBpp(D3DFMT_D24S8))*aaSamples // Render target + depth buffer
//    )*bdesc.Width*bdesc.Height;
//   }
//   else
//   {
//     _depthBufferRT.Free();
//     err = _d3DDevice->CreateTexture(bdesc.Width, bdesc.Height, 1, rtUsage, _rtFormat, D3DPOOL_DEFAULT, _renderTargetS.Init(), NULL);
//     if (err != D3D_OK) {DDError9("Error: Render target texture creation failed.", err); return;}
//     err = _renderTargetS->GetSurfaceLevel(0, _renderTargetSurfaceS.Init());
//     if (err != D3D_OK) {DDError9("Error: Render target surface creation failed.", err); return;}
//   }

// Direct3D 10 NEEDS UPDATE 
//   err = _d3DDevice->CreateTexture(bdesc.Width, bdesc.Height, 1, rtUsage, _rtFormat, D3DPOOL_DEFAULT, _renderTargetD.Init(), NULL);
//   if (err != D3D_OK) {DDError9("Error: Render target texture creation failed.", err); return;}
//   err = _renderTargetD->GetSurfaceLevel(0, _renderTargetSurfaceD.Init());
//   if (err != D3D_OK) {DDError9("Error: Render target surface creation failed.", err); return;}

// Direct3D 10 NEEDS UPDATE 
//   // Get current adapter 
//   UINT adapter;
//   {
//     D3DDEVICE_CREATION_PARAMETERS cpars;
//     _d3DDevice->GetCreationParameters(&cpars);
//     adapter = cpars.AdapterOrdinal;
//   }


  _apertureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  



  _texLoc = TexLocalVidMem;

  // by default assume some resonable values
  _vbAlignment = 4096;
  _texAlignment = 256;
  // if we do not know which memory is used, we need to assume worse case, i.e. the local one
  _dynamicVBNonLocal = false;
  _staticVBNonLocal = false;
  _ibNonLocal = false;

  // Direct3D 10 NEEDS UPDATE 
//   D3DADAPTER_IDENTIFIER9 id;
//   _direct3D->GetAdapterIdentifier(adapter,0,&id);
// 
//   // vendor specific optimization - local/non-local VRAM handling
//   if (id.VendorId==0x10DE)
//   {
//     // on nVidia we assume dynamic vertex buffers go into a non-local memory
//     _dynamicVBNonLocal = true;
//     _staticVBNonLocal = false;
//     // it seems index buffers go into non-local memory
//     // however if they do, our runtime should detect it
//     // therefore we rather assume worse case
//     _ibNonLocal = false;
// 
//     _vbAlignment = 4096;
//     _texAlignment = 32;
//   }
//   else if (id.VendorId==0x1002) // ATI
//   {
//     // tested with 6.7 drivers:
//     // dynamic vertex buffers go into a non-local memory
//     // with 6.8 drivers then go into a local one
//     // runtime test should catch it
//     _dynamicVBNonLocal = true;
//     _staticVBNonLocal = false;
//     // index buffers go into local memory on ATI
//     _ibNonLocal = false;
// 
//     // experiments have shown 4K alignment for VB and 32B for textures
//     _vbAlignment = 4096;
//     _texAlignment = 32;
//   }

  _canReadSRGB = true;
  _linearSpace = false;
  _rtIsSRGB = true;

  // Direct3D 10 NEEDS UPDATE 
//   _w=bdesc.Width;
//   _h=bdesc.Height;

  _minGuardX = 0; // guard band clipping properties
  _maxGuardX = _w;
  _minGuardY = 0;
  _maxGuardY = _h;

  _nightEye = 0;
  _fogStart = 0;
  _fogEnd = 1000;
  _aFogStart = 0;
  _aFogEnd = 100;
  _applyAplhaFog = false;
  _expFogCoef = -0.00062383;

  // check actual guard band clipping capabilities

  _pixelSize = 32;
  // Direct3D 10 NEEDS UPDATE 
//   switch (bdesc.Format)
//   {
//     case D3DFMT_X1R5G5B5:
//     case D3DFMT_R5G6B5:
//     case D3DFMT_A1R5G5B5:
//       _pixelSize = 16;
//       break;
//   } 

  _depthBpp = 32;

  // Direct3D 10 NEEDS UPDATE 
//   D3DSURFACE_DESC ddesc;
//   ComRef<IDirect3DSurface9> zBuffer;
//   _d3DDevice->GetDepthStencilSurface(zBuffer.Init());
//   if (zBuffer)
//   {
//     zBuffer->GetDesc(&ddesc);
//     switch (ddesc.Format)
//     {
//       case D3DFMT_D16_LOCKABLE:
//       case D3DFMT_D15S1:
//       case D3DFMT_D16:
//         _depthBpp = 16;
//         break;
//     } 
//   }
//   LogF("Bpp %d, depth %d",_pixelSize,_depthBpp);

  // Direct3D 10 NEEDS UPDATE 
//   // fix: enabled guard band clipping
//   #if 1
//   float minGuardX = caps.GuardBandLeft;
//   float maxGuardX = caps.GuardBandRight;
//   float minGuardY = caps.GuardBandTop;
//   float maxGuardY = caps.GuardBandBottom;
//   float maxBand = 1024*4;
//   saturate(minGuardX,-maxBand,0);
//   saturate(minGuardY,-maxBand,0);
//   saturate(maxGuardX,_w,_w+maxBand);
//   saturate(maxGuardY,_h,_h+maxBand);
//   _minGuardX = toInt(minGuardX), _maxGuardX = toInt(maxGuardX);
//   _minGuardY = toInt(minGuardY), _maxGuardY = toInt(maxGuardY);
//   LogF
//   (
//     "Guard band x %d to %d, y %d to %d",
//     _minGuardX,_maxGuardX,_minGuardY,_maxGuardY
//   );
//   #endif
// 
//   if( caps.TextureCaps&D3DPTEXTURECAPS_SQUAREONLY )
//   {
//     ErrorMessage("Only square texture supported by HW.");
//   }

  //_vidMemTextures=false; // AGP uses single heap model - n
  // for soft and MMX avoid bilinear/trilinear filtering

  // Set the viewport
  _viewW = _w;
  _viewH = _h;
  SetViewport(_viewW, _viewH);
  
  // Set default render state
  D3DSetRenderStateOpt(D3DRS_ZFUNC_OLD,D3DCMP_LESSEQUAL_OLD,false);
  D3DSetRenderStateOpt(D3DRS_ZWRITEENABLE_OLD,TRUE,false);
  if (_canWBuffer && (_depthBpp<=16 || _canWBuffer32) && _userEnabledWBuffer)
  {
    D3DSetRenderStateOpt(D3DRS_ZENABLE_OLD,D3DZB_USEW_OLD,false);
    LogF("Using w-buffer");
    _useWBuffer = true;
  }
  else
  {
    D3DSetRenderStateOpt(D3DRS_ZENABLE_OLD,D3DZB_TRUE_OLD,false);
    _useWBuffer = false;
  }
  D3DSetRenderStateOpt(D3DRS_CULLMODE_OLD,D3DCULL_CCW_OLD,false);
  D3DSetRenderStateOpt(D3DRS_CLIPPING_OLD,FALSE,false);


  #if NO_SET_RS
    _d3DDevice->SetRenderState(D3DRS_CLIPPING_OLD,FALSE);
  #endif

  D3DSetRenderStateOpt(D3DRS_ALPHATESTENABLE_OLD,FALSE,false);
  D3DSetRenderStateOpt(D3DRS_ALPHAFUNC_OLD,D3DCMP_GREATEREQUAL_OLD,false);
  D3DSetRenderStateOpt(D3DRS_ALPHAREF_OLD,0x1,false);

  D3DSetRenderStateOpt(D3DRS_ALPHABLENDENABLE_OLD,TRUE,false);
  D3DSetRenderStateOpt(D3DRS_SRCBLEND_OLD,D3DBLEND_SRCALPHA_OLD,false);
  D3DSetRenderStateOpt(D3DRS_DESTBLEND_OLD,D3DBLEND_INVSRCALPHA_OLD,false);


  // Set the fog color
  SetPixelShaderConstantF(PSC_FogColor, (const float*)&_fogColor, 1);

  // to do: we should not use bilinear filtering on devices
  // which do not support clamping
  

  // try to use trilinear interpolation if possible
  // always use bilinear interpolation

//   // setup using fixed-function pipeline
//   D3DSetTextureStageStateOpt(0,D3DTSS_ALPHAOP_OLD,D3DTOP_SELECTARG1_OLD,false);
//   D3DSetTextureStageStateOpt(0,D3DTSS_COLOROP_OLD,D3DTOP_MODULATE2X_OLD,false);
//   for (int i = 1; i < MaxStages; i++)
//   {
//     D3DSetTextureStageStateOpt(i, D3DTSS_TEXCOORDINDEX_OLD, i, false);
//     D3DSetTextureStageStateOpt(i, D3DTSS_COLOROP_OLD,D3DTOP_DISABLE_OLD,false);
//     D3DSetTextureStageStateOpt(i, D3DTSS_ALPHAOP_OLD,D3DTOP_DISABLE_OLD,false);
//   }

  for (int i = 0; i < MaxSamplerStages; i++)
  {
    D3DSetSamplerStateOpt     (i, D3DSAMP_MAGFILTER_OLD,      D3DTEXF_LINEAR_OLD,   false);
    D3DSetSamplerStateOpt     (i, D3DSAMP_MINFILTER_OLD,      D3DTEXF_LINEAR_OLD,   false);
    D3DSetSamplerStateOpt     (i, D3DSAMP_MIPFILTER_OLD,      D3DTEXF_LINEAR_OLD,   false);
    D3DSetSamplerStateOpt     (i, D3DSAMP_MAXANISOTROPY_OLD,  2,                false);
    D3DSetSamplerStateOpt     (i, D3DSAMP_ADDRESSU_OLD,       D3DTADDRESS_WRAP_OLD, false);
    D3DSetSamplerStateOpt     (i, D3DSAMP_ADDRESSV_OLD,       D3DTADDRESS_WRAP_OLD, false);
    D3DSetSamplerStateOpt     (i, D3DSAMP_SRGBTEXTURE_OLD,    TRUE,             false);
  }

  D3DSetRenderStateOpt(D3DRS_SRGBWRITEENABLE_OLD, FALSE, false);

  SetTexture0Color(HWhite,HWhite,false);
  
  // Set the default values
  D3DSetRenderStateOpt(D3DRS_COLORWRITEENABLE_OLD, 0x0000000F, false);
  D3DSetRenderStateOpt(D3DRS_STENCILENABLE_OLD, TRUE, false);
  D3DSetRenderStateOpt(D3DRS_STENCILREF_OLD, 0, false);
  D3DSetRenderStateOpt(D3DRS_STENCILMASK_OLD, STENCIL_SHADOW_LEVEL_MASK, false);
  D3DSetRenderStateOpt(D3DRS_STENCILWRITEMASK_OLD, STENCIL_FULL, false);
  D3DSetRenderStateOpt(D3DRS_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD, false);

  // Shadow states settings
  D3DSetRenderStateOpt(D3DRS_STENCILPASS_OLD, D3DSTENCILOP_REPLACE_OLD, false);
  D3DSetRenderStateOpt(D3DRS_STENCILWRITEMASK_OLD, STENCIL_FULL, false);
  D3DSetRenderStateOpt(D3DRS_STENCILREF_OLD, 0, false);
  D3DSetRenderStateOpt(D3DRS_STENCILFUNC_OLD, D3DCMP_ALWAYS_OLD, false);
  D3DSetRenderStateOpt(D3DRS_CULLMODE_OLD, D3DCULL_CCW_OLD, false);
  DoShadowsStatesSettings(SS_Disabled,false);

  for (int i = 0; i < MAX_UV_SETS; i++)
  {
    _texGenScale[i].u = _texGenScale[i].v = 1.0f;
    _texGenOffset[i].u = _texGenOffset[i].v = 0.0f;
  }
  _texGenScaleOrOffsetHasChanged = true;

  _uvSource[0] = UVTex;
  _uvSourceCount = 1;
  _uvSourceOnlyTex0 = true;

  // Zero texture handles
  for (int i = lenof(_lastHandle) - 1; i >= 0; i--)
  {
    SetTexture(i, NULL, false);
    _setHandle[i] = NULL;
  }

  _lastSpec = 0;
  _lastMat = TexMaterialLODInfo();
  _lastRenderingMode = RMCommon;
  _prepSpec = 0;
  _mainLight = ML_Sun;
  _fogMode = FM_Fog;

  _world.SetIdentity();
  _invWorld.SetIdentity();

  ResetShaderConstantValues();

  // Create depth buffer resources if needed
  _dbQuality = _postFxQuality>0 ? 1 : 0;
  if (_dbQuality > 0)
  {
    CreateDBResources();
  }

  _rtAllocated = 0;
  // Direct3D 10 NEEDS UPDATE 
//   // note: with shared device, other processes may be allocating/releasing meanwhile
//   // following calculation is therefore only estimation
//   DWORD texMemAfterInit = _d3DDevice->GetAvailableTextureMem();
//   _rtAllocated += texMemBeforeInit-texMemAfterInit;
//   if (_rtAllocated<0) _rtAllocated = 0;
//   LogF("%d MB used for render targets",_rtAllocated/1024/1024);
}

void EngineDDT::Done3DState()
{
  Fail("DX10 TODO");
  // _backBuffer.Free(); // Direct3D 10 NEEDS UPDATE 
  // _depthBuffer.Free(); // Direct3D 10 NEEDS UPDATE 
  // _depthBufferRT.Free(); // Direct3D 10 NEEDS UPDATE 
  // _renderTargetD.Free(); // Direct3D 10 NEEDS UPDATE 
  // _renderTargetSurfaceD.Free(); // Direct3D 10 NEEDS UPDATE 
  // _renderTargetS.Free(); // Direct3D 10 NEEDS UPDATE 
  // _renderTargetSurfaceS.Free(); // Direct3D 10 NEEDS UPDATE 
  // _currRenderTarget.Free();  // Direct3D 10 NEEDS UPDATE 
  // _currRenderTargetDepthBufferSurface.Free();  // Direct3D 10 NEEDS UPDATE 
  // _currRenderTargetSurface.Free();  // Direct3D 10 NEEDS UPDATE 
  DestroySBResources();
  DestroyDBResources();
}

void EngineDDT::InitVRAMPixelFormat( DXGI_FORMAT &pf, PacFormat format, bool enableDXT )
{
  switch (format)
  {
  case PacAI88:
    pf = DXGI_FORMAT_R8G8_UNORM;
    return;
  case PacARGB8888:
    pf = DXGI_FORMAT_R8G8B8A8_UNORM;
    return;
  case PacARGB1555:
    pf = DXGI_FORMAT_R8G8_UNORM;
    return;
  case PacDXT1:
    pf = DXGI_FORMAT_BC1_UNORM;
    return;
  case PacDXT5:
    pf = DXGI_FORMAT_BC3_UNORM;
    return;
  }

  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   switch( format )
//   {
//     case PacDXT1:
//       pf = DXGI_FORMAT_BC1_UNORM;
//       break;
//     case PacDXT2:
//       pf = DXGI_FORMAT_BC1_UNORM;
//       break;
//     case PacDXT3:
//       pf = DXGI_FORMAT_BC2_UNORM;
//       break;
//     case PacDXT4:
//       pf = DXGI_FORMAT_BC2_UNORM;
//       break;
//     case PacDXT5:
//       pf = DXGI_FORMAT_BC3_UNORM;
//       break;
//     case PacARGB1555:
//       Fail("DX10 TODO");
//       // Direct3D 10 NEEDS UPDATE 
// //       pf = tex_A1R5G5B5;
//       break;
//     case PacRGB565:
//       Fail("DX10 TODO");
//       // Direct3D 10 NEEDS UPDATE 
// //       pf = tex_R5G6B5;
//       break;
//     case PacAI88:
//       if (Can88())
//       {
//         Fail("DX10 TODO");
//         // Direct3D 10 NEEDS UPDATE 
// //         // TODO: correct AI format
// //         pf = tex_A8L8;
//         break;
//       }
//     // note: previous case may fall through
//     case PacARGB8888:
//       if (Can8888())
//       {
//         Fail("DX10 TODO");
//         // Direct3D 10 NEEDS UPDATE 
// //         pf = tex_A8R8G8B8;
//         break;
//       }
//     // note: previous case may fall through
//     case PacARGB4444:
//       Fail("DX10 TODO");
//       // Direct3D 10 NEEDS UPDATE 
// //       pf = tex_A4R4G4B4;
//       break;
//     case PacP8:
//       Fail("DX10 TODO");
//       // Direct3D 10 NEEDS UPDATE 
// //       pf = D3DFMT_P8;
//       Fail("Palette textures obsolete");
//     break;
//     default:
//       ErrorMessage("Texture has bad pixel format (VRAM).");
//     break;
//   }
}

#include <El/Debugging/debugTrap.hpp>

RString EngineDDT::GetDebugName() const
{
  RString ret = "D3DT";

  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   if (!_d3DDevice)
//   {
//     ret = ret + ": <null>";
//   }
//   else
//   {
//     D3DDEVICE_CREATION_PARAMETERS cp;
//     //PROFILE_DX_SCOPE(3dGDN);
//     _d3DDevice->GetCreationParameters(&cp);
//     D3DADAPTER_IDENTIFIER9 id;
//     _direct3D->GetAdapterIdentifier(cp.AdapterOrdinal, 0, &id);
//     char driverVersion[256];
//     sprintf
//     (
//       driverVersion,"%d.%d.%d.%d",
//       HIWORD(id.DriverVersion.HighPart),LOWORD(id.DriverVersion.HighPart),
//       HIWORD(id.DriverVersion.LowPart),LOWORD(id.DriverVersion.LowPart)
//     );
//     ret = ret + ", Device: " + RString(id.Description);
//     ret = ret + ", Driver:" + RString(id.Driver) + RString(" ") + RString(driverVersion);
//   }

  return ret;
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

void EngineDDT::SearchMonitor(int &x, int &y, int &w, int &h)
{
  bool monFound = false;

  // Direct3D 10 NEEDS UPDATE 
//   MONITORINFO mi;
//   int adapter = D3DAdapter;
//   if (adapter>=0)
//   {
//     HMONITOR mon = _direct3D->GetAdapterMonitor(adapter);
//   // if running in windowed mode on multimon window, search all monitors
//     HMODULE lib = LoadLibrary("user32.dll");
//     if (lib)
//     {
//       GetMonitorInfoT *getmon = (GetMonitorInfoT *)GetProcAddress(lib,"GetMonitorInfoA");
//       if (getmon)
//       {
//         memset(&mi,0,sizeof(mi));
//         mi.cbSize = sizeof(mi);
//         getmon(mon,&mi);
//         // we have monitor information, adjust window position
//         int maxW = mi.rcWork.right-mi.rcWork.left;
//         int maxH = mi.rcWork.bottom-mi.rcWork.top;
//         if (w>maxW) w=maxW;
//         if (h>maxH) h=maxH;
//         if (x>mi.rcWork.right-w) x = mi.rcWork.right-w;
//         if (y>mi.rcWork.bottom-h) y = mi.rcWork.bottom-h;
//         if (x<mi.rcWork.left) x = mi.rcWork.left;
//         if (y<mi.rcWork.top) y = mi.rcWork.top;
//         monFound = true;
//       }
//       FreeLibrary(lib);
//     }
//   }

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
}

// Direct3D 10 NEEDS UPDATE 
static SSupportedFormats supportedFormats[] =
{
//   {D3DFMT_A8R8G8B8, 32},
//   {D3DFMT_X8R8G8B8, 32},
//   {D3DFMT_R8G8B8, 24},
//   {D3DFMT_X4R4G4B4, 16},
//   {D3DFMT_R5G6B5, 16},
//   {D3DFMT_X1R5G5B5, 16},
//   {D3DFMT_A1R5G5B5, 16},
//   {D3DFMT_A4R4G4B4, 16},
//   {D3DFMT_R3G3B2, 8},
//   {D3DFMT_A8R3G3B2, 8},
  {DXGI_FORMAT_A8_UNORM, 8}
};

// Direct3D 10 NEEDS UPDATE 
// static const struct {DXGI_FORMAT format;Engine::HDRPrec prec;} PreferredRTFormat[] =
// {
//   {D3DFMT_A16B16G16R16F,Engine::HDR16},
//   {D3DFMT_A32B32G32R32F,Engine::HDR32},
// };

Engine::HDRPrec EngineDDT::ValidateHDRPrecision(HDRPrec prec) const
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

bool EngineDDT::SetHDRPrecisionWanted(HDRPrec prec)
{
  prec = ValidateHDRPrecision(prec);
  if (prec==_hdrPrecWanted) return false;
  
  _hdrPrecWanted=prec;
  return true;
}

void EngineDDT::SetHDRPrecision(HDRPrec prec)
{
  if (SetHDRPrecisionWanted(prec))
  {
    ApplyWanted();
  }
}
int EngineDDT::GetFSAA() const
{
  return _fsAAWanted;
}

bool EngineDDT::SetFSAAWanted(int quality)
{
  if (quality>_maxFSAA) quality = _maxFSAA;
  if (quality==_fsAAWanted) return false;
  _fsAAWanted = quality;
  return true;
}

void EngineDDT::SetFSAA(int level)
{
  if (SetFSAAWanted(level))
  {
    ApplyWanted();
  }
}


void EngineDDT::ApplyWanted()
{
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   D3DDEVICE_CREATION_PARAMETERS pars;
//   _d3DDevice->GetCreationParameters(&pars);
//   int adapter = pars.AdapterOrdinal;
//   FindMode(adapter);
//   // note: complete reset may be not necessary - deleting all surfaces might do
//   Reset();
//   EnableDepthOfField(_postFxQuality>0 ? 1 : 0);
}


int EngineDDT::GetMaxFSAA() const
{
  return _maxFSAA;
}

int EngineDDT::GetAnisotropyQuality() const
{
  return _afWanted;
}

void EngineDDT::SetAnisotropyQuality(int level)
{
  if (level>_maxAF) level = _maxAF;
  if (level==_afWanted) return;
  _afWanted = level;
  
  // we need to discard all pushbuffers, as they have the filtering information recorded
  for (VertexBufferD3DTLRUItem *vb = _vBufferLRU.First(); vb; vb=_vBufferLRU.Next(vb))
  {
    const Shape *shape = vb->_owner;
    if (!shape) continue;
    VertexBuffer *buf = shape->GetVertexBuffer();
    if (!buf) continue;
#if USE_PUSHBUFFERS
    buf->DiscardPushBuffers();
#endif
  }
  /*
    
  D3DDEVICE_CREATION_PARAMETERS pars;
  _d3DDevice->GetCreationParameters(&pars);
  int adapter = pars.AdapterOrdinal;
  FindMode(adapter);
  // note: complete reset may be not necessary - deleting all surfaces might do
  Reset();
  */
}

bool EngineDDT::SetPostprocessEffectsWanted(int quality)
{
  if (_postFxQuality==quality) return false;
  bool changed = ( _postFxQuality!=0 )!= (quality!=0);
  _postFxQuality = quality;
  return changed;
}

void EngineDDT::SetPostprocessEffects(int quality)
{
  if (SetPostprocessEffectsWanted(quality))
  {
    ApplyWanted();
  }
}

int EngineDDT::GetPostprocessEffects() const
{
  return _postFxQuality;
}

int EngineDDT::GetMaxAnisotropyQuality() const
{
  return _maxAF;
}

int EngineDDT::GetDeviceLevel() const
{
  return _deviceLevel;
}

int EngineDDT::GetCurrentDeviceLevel() const
{
  // check settings to learn what quality level we correspond to?
  // some settings are higher than High - we call them Custom (or Very High?)
  int maxILevel = -1;

  if (GetTextureQuality()>-1)
  {
    saturateMax(maxILevel,GetTextureQuality()/2);
  }

  if (HDRPrecision()>HDR8) saturateMax(maxILevel,3);

  if (!IsWindowed())
  {
    if (_w*_h>1024*768*4/3) saturateMax(maxILevel,2);
    else if (_w*_h>800*600*4/3) saturateMax(maxILevel,1);
  }

  if (GetFSAA()>0)
  {
    saturateMax(maxILevel,GetFSAA());
  }
  if (GetAnisotropyQuality()>0)
  {
    saturateMax(maxILevel,GetAnisotropyQuality());
  }
  if (GetPostprocessEffects()>0)
  {
    saturateMax(maxILevel,GetPostprocessEffects()+1);
  }
  return (maxILevel+1)<<8;
}

void EngineDDT::SetDefaultSettingsLevel(int level)
{
  //int _deviceLevel;
  // scan settings one by one
  // based on level select the most appropriate one, and set it (if supported)

  int iLevel = (level>>8)-1;
  saturate(iLevel,-1,3);
  
  // never set texture quality higher than normal by default
  bool changed = SetTextureQualityWanted(intMin(iLevel,1));
  
  // TODO: auto-select resolution
  if (!IsWindowed())
  {
    if (iLevel<=0) changed |= SetResolutionWanted(800,600,32);
    else changed |= SetResolutionWanted(1024,768,32);
  }
  //op(_resolutions);
  //op(_refreshRates);
  // with high-end prefer HDR=High if supported
  // most cards cannot do HDR16 and FSAA at the same time. We prefer FSAA
  changed |= SetHDRPrecisionWanted(HDR8);
  /*
  if (level<=0x200) changed |= SetHDRPrecisionWanted(HDR8);
  else changed |= SetHDRPrecisionWanted(HDR16);
  */

  
  changed |= SetFSAAWanted(iLevel);
  
  SetAnisotropyQuality(iLevel);
  // we want postprocess High with overall High
  changed |= SetPostprocessEffectsWanted(intMax(iLevel-1,0));
  // both gamma and brightness do not affect performance - do not set here
  //SetGamma(GetDefaultGamma());
  //SetBrightness(GetDefaultBrightness());
  
  //changed |= SetWBufferWanted(false);
  
  if (changed && _initialized)
  {
    ApplyWanted();
  }
}


void EngineDDT::FindMode(int adapter)
{
  // Fill out the presentation parameters
  ZeroMemory(&_pp, sizeof(_pp));
  _pp.BufferDesc.Width = _w;
  _pp.BufferDesc.Height = _h;
  _pp.BufferDesc.RefreshRate.Numerator = 60;
  _pp.BufferDesc.RefreshRate.Denominator = 1;
  _pp.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  _pp.SampleDesc.Count = 1;
  _pp.SampleDesc.Quality = 0;
  _pp.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  _pp.BufferCount = 1;
  _pp.OutputWindow = _hwndApp;
  _pp.Windowed = _windowed;
  _pp.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  // Direct3D 10 NEEDS UPDATE 
//   // prepare presentation parameters based on _w, _h, _pixelSize
//   // first select backbuffer format
//   D3DDISPLAYMODE bestDisplayMode;
//   // we may want a different render target
//   int bestCost = INT_MAX;
//   bestDisplayMode.Format = D3DFMT_X8R8G8B8;
//   bestDisplayMode.RefreshRate = D3DPRESENT_RATE_DEFAULT;
//   bestDisplayMode.Width = _w;
//   bestDisplayMode.Height = _h;
//   if (_windowed)
//   {
//     // Retrieve the current display mode
//     _direct3D->GetAdapterDisplayMode(adapter,&bestDisplayMode);
//   }
//   else
//   {
//     for (int formatIndex = 0; formatIndex < lenof(supportedFormats); formatIndex++) {
//       for (int mode = 0; mode < (int)_direct3D->GetAdapterModeCount(adapter, supportedFormats[formatIndex]._format); mode++)
//       {
//         D3DDISPLAYMODE displayMode;
//         _direct3D->EnumAdapterModes(adapter, supportedFormats[formatIndex]._format, mode, &displayMode);
//         
//         int bpp = 16;
//         int cost = 0;
//         #if 0
//         LogF
//         (
//           "Display Format %dx%d (%d) %s supported",
//           displayMode.Width,displayMode.Height,displayMode.RefreshRate,
//           SurfaceFormatName(displayMode.Format)
//         );
//         #endif
//         switch (displayMode.Format)
//         {
//           case D3DFMT_X8R8G8B8: cost += 180; bpp = 32; break;
//           case D3DFMT_R5G6B5: cost += 100; bpp = 16; break;
//           case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: cost += 110; bpp = 16; break;
// 
//           case D3DFMT_A8R8G8B8: cost += 0; bpp = 32; break;
//           case D3DFMT_R8G8B8: cost += 200; bpp = 24; break;
// 
//           default: cost += 500; break; // unknown format - last resort
//         }
//         cost +=
//         (
//           abs(_w*_h-displayMode.Width*displayMode.Height)*10 + 
//           abs(_w-displayMode.Width)*10 + 
//           abs(_h-displayMode.Height)*10 +
//           abs(_pixelSize-bpp)*200
//         );
//         if (_refreshRate>0)
//         {
//           cost += abs(_refreshRate-displayMode.RefreshRate);
//         }
//         else
//         {
//           cost += abs(75-displayMode.RefreshRate); // higher refresh rate is always better
//         }
//         if (cost<bestCost)
//         {
//           bestCost = cost;
//           bestDisplayMode = displayMode;
//         }
//       }
//     }
//     LogF
//     (
//       "mode %dx%d (%d Hz) (%s)",
//       bestDisplayMode.Width,bestDisplayMode.Height,
//       bestDisplayMode.RefreshRate,
//       SurfaceFormatName(bestDisplayMode.Format)
//     );
//   }
// 
//   _hdrPrecSupported = HDR8;
//   for (int i=0; i<lenof(PreferredRTFormat); i++)
//   {
//     DXGI_FORMAT rt = PreferredRTFormat[i].format;
//     
//     // check if it can be used as a render target
//     HRESULT ok = _direct3D->CheckDeviceFormat(
//       adapter,_devType,bestDisplayMode.Format,
//       D3DUSAGE_RENDERTARGET,D3DRTYPE_SURFACE,
//       rt
//     );
//     
//     if (ok==D3D_OK)
//     {
//       ok = _direct3D->CheckDeviceFormat(
//         adapter,_devType,bestDisplayMode.Format,
//         D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,
//         D3DRTYPE_SURFACE,
//         rt
//       );
//       if (ok==D3D_OK)
//       {
//         _hdrPrecSupported |= PreferredRTFormat[i].prec;
//       }
//     }
//   }
//   
//   _canWriteSRGB = false;
//   HRESULT ok = _direct3D->CheckDeviceFormat(
//     adapter,_devType,bestDisplayMode.Format,
//     D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_SRGBWRITE,
//     D3DRTYPE_SURFACE,
//     bestDisplayMode.Format
//   );
//   if (ok==D3D_OK)
//   {
//     _canWriteSRGB = true;
//     LogF(
//       "SRGB write support available for backbuffer format %s",SurfaceFormatName(bestDisplayMode.Format)
//     );
//   }
// 
//   // select suitable depth-buffer formats
//   DXGI_FORMAT depthFormat = D3DFMT_D24S8;
//   
//   D3DCAPS9 caps;
//   memset(&caps,0,sizeof(caps));
//   ok = _direct3D->GetDeviceCaps(adapter,_devType,&caps);
//   if (FAILED(ok))
//   {
//     ErrF("GetDeviceCaps failed, error %x",ok);
//   }
//   if (caps.StencilCaps)
//   {
//     LogF("Stencil caps %d",caps.StencilCaps);
//   }
// 
//   _hdrPrecWanted = ValidateHDRPrecision(_hdrPrecWanted);
// 
//   DXGI_FORMAT bestRTFormat=D3DFMT_A8R8G8B8;
//   switch (_hdrPrecWanted)
//   {
//     case HDR16: bestRTFormat=D3DFMT_A16B16G16R16F; break;
//     case HDR32: bestRTFormat=D3DFMT_A32B32G32R32F; break;
//   }
//   
//   
//   ok = _direct3D->CheckDeviceFormat(
//     adapter,_devType,bestDisplayMode.Format,
//     D3DUSAGE_DEPTHSTENCIL,D3DRTYPE_SURFACE,
//     depthFormat
//   );
//   if (ok!=D3D_OK)
//   {
//     ErrorMessage(
//       "Display adapter does not support format %s for %s.",
//       SurfaceFormatName(depthFormat),
//       SurfaceFormatName(bestDisplayMode.Format)
//     );
//   }
//   else
//   {
//     ok = _direct3D->CheckDepthStencilMatch(
//       adapter,_devType,
//       bestDisplayMode.Format,bestRTFormat,
//       depthFormat
//     );
//     if (ok!=D3D_OK)
//     {
//       ErrorMessage(
//         "Display adapter does not support format %s with %s/%s.",
//         SurfaceFormatName(depthFormat),
//         SurfaceFormatName(bestDisplayMode.Format),
//         SurfaceFormatName(bestRTFormat)
//       );
//     }
//   }
//   _hasStencilBuffer = true;
//   _canTwoSidedStencil = (caps.StencilCaps&D3DSTENCILCAPS_TWOSIDED)!=0;
// 
//   if (_windowed)
//   {
//     _pp.BackBufferWidth = _w;
//     _pp.BackBufferHeight = _h;
//     _pp.FullScreen_RefreshRateInHz = 0;
//     _refreshRate = 0;
//   }
//   else
//   {
//     _pp.BackBufferWidth = bestDisplayMode.Width;
//     _pp.BackBufferHeight = bestDisplayMode.Height;
//     _pp.FullScreen_RefreshRateInHz = bestDisplayMode.RefreshRate;
//     _w = bestDisplayMode.Width;
//     _h = bestDisplayMode.Height;
//     _refreshRate = bestDisplayMode.RefreshRate;
//   }
// 
// 
//   DWORD multisampleLevels = 0;
//   DWORD multisampleLevelsDepth = 0;
//   if (
//     _direct3D->CheckDeviceMultiSampleType(
//       adapter,_devType,bestRTFormat,_windowed,
//       D3DMULTISAMPLE_NONMASKABLE,&multisampleLevels
//     )!=D3D_OK
//   )
//   {
//     multisampleLevels=0;
//   }
//   if (
//     _direct3D->CheckDeviceMultiSampleType(
//       adapter,_devType,depthFormat,_windowed,
//       D3DMULTISAMPLE_NONMASKABLE,&multisampleLevelsDepth
//     )!=D3D_OK
//   )
//   {
//     multisampleLevelsDepth = 0;
//   }
//   if (multisampleLevels>multisampleLevelsDepth) multisampleLevels = multisampleLevelsDepth;
// 
//   _rtFormat = bestRTFormat;
//   _rtMultiSampleType = D3DMULTISAMPLE_NONE;
//   _rtMultiSampleQuality = 0;
//   _maxFSAA = multisampleLevels;
//   
//   if (multisampleLevels>0 && _fsAAWanted>0)
//   {
//     _rtMultiSampleType = D3DMULTISAMPLE_NONMASKABLE;
//     _rtMultiSampleQuality = _fsAAWanted-1;
//     if (_rtMultiSampleQuality>multisampleLevels-1) _rtMultiSampleQuality = multisampleLevels-1;
//   }
// 
//   #if 1
//   _downsampleFormat = D3DFMT_A8R8G8B8;
// 
//   static const DXGI_FORMAT formats8b[]= {
//     D3DFMT_R8G8B8,D3DFMT_A8R8G8B8,D3DFMT_X8R8G8B8,
//     D3DFMT_R5G6B5,D3DFMT_X1R5G5B5,D3DFMT_A1R5G5B5,
//     D3DFMT_A4R4G4B4,D3DFMT_X4R4G4B4,
//     D3DFMT_A8B8G8R8,D3DFMT_X8B8G8R8
//   };
// 
//   bool is8b = false;
//   for (int i=0; i<lenof(formats8b); i++)
//   {
//     if (bestDisplayMode.Format==formats8b[i]) {is8b=true;break;}
//   }
//   if (!is8b)
//   {
//     // we want a high-resolution format for down-sampling
//     static const DXGI_FORMAT downsampleFormats[]=
//     {
//       // we need to be able to keep at least avg and max
//       // for this we need RG
//       D3DFMT_G16R16F,
//       D3DFMT_A16B16G16R16F,
//       D3DFMT_G32R32F,
//       D3DFMT_A32B32G32R32F,
//     };
//     for (int i=0; i<lenof(downsampleFormats); i++)
//     {
//       // we do not require any alpha blending or alpha testing
//       HRESULT err = _direct3D->CheckDeviceFormat(
//         adapter,_devType,bestDisplayMode.Format,
//         D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_FILTER,D3DRTYPE_TEXTURE,downsampleFormats[i]
//       );
//       if (err==D3D_OK)
//       {
//         _downsampleFormat = downsampleFormats[i];
//         break;
//       }
//     }
//     LogF("Down-sample surface format %s",SurfaceFormatName(_downsampleFormat));
//   }
//   #endif
//   
//   _pp.BackBufferFormat = bestDisplayMode.Format;
// 
//   // using triple buffering has little sense when using render target
//   _pp.BackBufferCount = 1;
//   _pp.MultiSampleType = D3DMULTISAMPLE_NONE;
//   _pp.MultiSampleQuality = 0;
//   _pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
//   _pp.hDeviceWindow = _hwndApp;
//   _pp.Windowed = _windowed;
//   _pp.EnableAutoDepthStencil = true;
//   _pp.AutoDepthStencilFormat = depthFormat;
//   _pp.Flags = 0;
//   if (_pp.EnableAutoDepthStencil)
//   {
//     _pp.Flags |= D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL;
//   }
//   _pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
}

struct DeviceLevelInfo
{
  int level;
  int deviceId;
  int subSysId;
  const char *name1;
  const char *name2;
  /**
  Adapter levels:
  0 - very bad, not supported
  0x100 - low end (GeForce 5900, ATI 9700)
  0x200 - middle (GeForce 6800, ATI X800)
  0x300 - high (GeForce 7800, ATI X1600)
  */
};
static const DeviceLevelInfo NVDevices[]={
  // Perf.  Device  GPU/Chip name
  { 0x0000, 0x0020, 0x0000, "RIVA TNT", "RIVA TNT" },
  { 0x0000, 0x0028, 0x0000, "RIVA TNT2/TNT2 Pro", "RIVA TNT2/TNT2 Pro" },
  { 0x0000, 0x00A0, 0x0000, "Aladdin TNT2", "Aladdin TNT2" },
  { 0x0000, 0x002C, 0x0000, "Vanta/Vanta LT", "Vanta/Vanta LT" },
  { 0x0000, 0x0029, 0x0000, "RIVA TNT2 Ultra", "RIVA TNT2 Ultra" },
  { 0x0000, 0x002D, 0x0000, "RIVA TNT2 Model 64/Model 64 Pro", "RIVA TNT2 Model 64/Model 64 Pro" },
  { 0x0000, 0x0100, 0x0000, "GeForce 256", "GeForce 256" },
  { 0x0000, 0x0101, 0x0000, "GeForce DDR", "GeForce DDR" },
  { 0x0000, 0x0103, 0x0000, "Quadro", "Quadro" },
  { 0x0000, 0x0110, 0x0000, "GeForce2 MX/MX 400", "GeForce2 MX/MX 400" },
  { 0x0000, 0x0111, 0x0000, "GeForce2 MX 100/200", "GeForce2 MX 100/200" },
  { 0x0000, 0x0113, 0x0000, "Quadro2 MXR/EX/Go", "Quadro2 MXR/EX/Go" },
  { 0x0000, 0x01A0, 0x0000, "GeForce2 Integrated GPU", "GeForce2 Integrated GPU" },
  { 0x0000, 0x0150, 0x0000, "GeForce2 GTS/GeForce2 Pro", "GeForce2 GTS/GeForce2 Pro" },
  { 0x0000, 0x0151, 0x0000, "GeForce2 Ti", "GeForce2 Ti" },
  { 0x0000, 0x0152, 0x0000, "GeForce2 Ultra", "GeForce2 Ultra" },
  { 0x0000, 0x0153, 0x0000, "Quadro2 Pro", "Quadro2 Pro" },
  { 0x0000, 0x0170, 0x0000, "GeForce4 MX 460", "GeForce4 MX 460" },
  { 0x0000, 0x0171, 0x0000, "GeForce4 MX 440", "GeForce4 MX 440" },
  { 0x0000, 0x0172, 0x0000, "GeForce4 MX 420", "GeForce4 MX 420" },
  { 0x0000, 0x0173, 0x0000, "GeForce4 MX 440-SE", "GeForce4 MX 440-SE" },
  { 0x0000, 0x0178, 0x0000, "Quadro4 550 XGL", "Quadro4 550 XGL" },
  { 0x0000, 0x017A, 0x0000, "Quadro NVS", "Quadro NVS" },
  { 0x0000, 0x0181, 0x0000, "GeForce4 MX 440 with AGP8X", "GeForce4 MX 440 with AGP8X" },
  { 0x0000, 0x0182, 0x0000, "GeForce4 MX 440SE with AGP8X", "GeForce4 MX 440SE with AGP8X" },
  { 0x0000, 0x0183, 0x0000, "GeForce4 MX 420 with AGP8X", "GeForce4 MX 420 with AGP8X" },
  { 0x0000, 0x0185, 0x0000, "GeForce4 MX 4000", "GeForce4 MX 4000" },
  { 0x0000, 0x0188, 0x0000, "Quadro4 580 XGL", "Quadro4 580 XGL" },
  { 0x0000, 0x018A, 0x0000, "Quadro NVS with AGP8X", "Quadro NVS with AGP8X" },
  { 0x0000, 0x018B, 0x0000, "Quadro4 380 XGL", "Quadro4 380 XGL" },
  { 0x0000, 0x018C, 0x0000, "Quadro NVS 50 PCI", "Quadro NVS 50 PCI" },
  { 0x0000, 0x01F0, 0x0000, "GeForce4 MX Integrated GPU", "GeForce4 MX Integrated GPU" },
  { 0x0000, 0x0200, 0x0000, "GeForce3", "GeForce3" },
  { 0x0000, 0x0201, 0x0000, "GeForce3 Ti 200", "GeForce3 Ti 200" },
  { 0x0000, 0x0202, 0x0000, "GeForce3 Ti 500", "GeForce3 Ti 500" },
  { 0x0000, 0x0203, 0x0000, "Quadro DCC", "Quadro DCC" },
  { 0x0000, 0x0250, 0x0000, "GeForce4 Ti 4600", "GeForce4 Ti 4600" },
  { 0x0000, 0x0251, 0x0000, "GeForce4 Ti 4400", "GeForce4 Ti 4400" },
  { 0x0000, 0x0252, 0x0000, "NV25", "NV25" },
  { 0x0000, 0x0253, 0x0000, "GeForce4 Ti 4200", "GeForce4 Ti 4200" },
  { 0x0000, 0x0258, 0x0000, "Quadro4 900 XGL", "Quadro4 900 XGL" },
  { 0x0000, 0x0259, 0x0000, "Quadro4 750 XGL", "Quadro4 750 XGL" },
  { 0x0000, 0x025B, 0x0000, "Quadro4 700 XGL", "Quadro4 700 XGL" },
  { 0x0000, 0x0280, 0x0000, "GeForce4 Ti 4800", "GeForce4 Ti 4800" },
  { 0x0000, 0x0281, 0x0000, "GeForce4 Ti 4200 with AGP8X", "GeForce4 Ti 4200 with AGP8X" },
  { 0x0000, 0x0282, 0x0000, "GeForce4 Ti 4800 SE", "GeForce4 Ti 4800 SE" },
  { 0x0000, 0x0288, 0x0000, "Quadro4 980 XGL", "Quadro4 980 XGL" },
  { 0x0000, 0x0289, 0x0000, "Quadro4 780 XGL", "Quadro4 780 XGL" },
  { 0x0100, 0x0301, 0x0000, "GeForce FX 5800 Ultra", "GeForce FX 5800 Ultra" },
  { 0x0100, 0x0302, 0x0000, "GeForce FX 5800", "GeForce FX 5800" },
  { 0x0100, 0x0308, 0x0000, "Quadro FX 2000", "Quadro FX 2000" },
  { 0x0100, 0x0309, 0x0000, "Quadro FX 1000", "Quadro FX 1000" },
  { 0x0100, 0x0311, 0x0000, "GeForce FX 5600 Ultra", "GeForce FX 5600 Ultra" },
  { 0x0100, 0x0312, 0x0000, "GeForce FX 5600", "GeForce FX 5600" },
  { 0x0100, 0x0313, 0x0000, "NV31", "NV31" },
  { 0x0100, 0x0314, 0x0000, "GeForce FX 5600XT", "GeForce FX 5600XT" },
  { 0x0100, 0x0320, 0x0000, "GeForce FX 5200", "GeForce FX 5200" },
  { 0x0100, 0x0321, 0x0000, "GeForce FX 5200 Ultra", "GeForce FX 5200 Ultra" },
  { 0x0100, 0x0322, 0x0000, "GeForce FX 5200", "GeForce FX 5200" },
  { 0x0100, 0x0323, 0x0000, "GeForce FX 5200LE", "GeForce FX 5200LE" },
  { 0x0100, 0x0326, 0x0000, "GeForce FX 5500", "GeForce FX 5500" },
  { 0x0100, 0x0327, 0x0000, "GeForce FX 5100", "GeForce FX 5100" },
  { 0x0100, 0x032A, 0x0000, "Quadro NVS 280 PCI", "Quadro NVS 280 PCI" },
  { 0x0100, 0x032B, 0x0000, "Quadro FX 500/FX 600", "Quadro FX 500/FX 600" },
  { 0x0100, 0x032F, 0x0000, "NV34GL", "NV34GL" },
  { 0x0100, 0x0330, 0x0000, "GeForce FX 5900 Ultra", "GeForce FX 5900 Ultra" },
  { 0x0100, 0x0331, 0x0000, "GeForce FX 5900", "GeForce FX 5900" },
  { 0x0100, 0x0332, 0x0000, "GeForce FX 5900XT", "GeForce FX 5900XT" },
  { 0x0100, 0x0333, 0x0000, "GeForce FX 5950 Ultra", "GeForce FX 5950 Ultra" },
  { 0x0100, 0x0334, 0x0000, "GeForce FX 5900ZT", "GeForce FX 5900ZT" },
  { 0x0100, 0x0338, 0x0000, "Quadro FX 3000", "Quadro FX 3000" },
  { 0x0100, 0x033F, 0x0000, "Quadro FX 700", "Quadro FX 700" },
  { 0x0100, 0x0341, 0x0000, "GeForce FX 5700 Ultra", "GeForce FX 5700 Ultra" },
  { 0x0100, 0x0342, 0x0000, "GeForce FX 5700", "GeForce FX 5700" },
  { 0x0100, 0x0343, 0x0000, "GeForce FX 5700LE", "GeForce FX 5700LE" },
  { 0x0100, 0x0344, 0x0000, "GeForce FX 5700VE", "GeForce FX 5700VE" },
  { 0x0100, 0x0345, 0x0000, "NV36", "NV36" },
  { 0x0100, 0x034E, 0x0000, "Quadro FX 1100", "Quadro FX 1100" },
  { 0x0100, 0x034F, 0x0000, "NV36GL", "NV36GL" },
  { 0x0200, 0x0040, 0x0000, "GeForce 6800 Ultra", "GeForce 6800 Ultra" },
  { 0x0100, 0x0041, 0x0000, "GeForce 6800", "GeForce 6800" },
  { 0x0100, 0x0042, 0x0000, "GeForce 6800 LE", "GeForce 6800 LE" },
  { 0x0100, 0x0043, 0x0000, "NV40", "NV40" },
  { 0x0200, 0x0045, 0x0000, "GeForce 6800 GT", "GeForce 6800 GT" },
  { 0x0100, 0x0049, 0x0000, "NV40GL", "NV40GL" },
  { 0x0200, 0x004E, 0x0000, "Quadro FX 4000", "Quadro FX 4000" },
  { 0x0200, 0x00C0, 0x0000, "NV41", "NV41" }, // 6800 GS
  { 0x0100, 0x00C1, 0x0000, "GeForce 6800", "GeForce 6800" },
  { 0x0100, 0x00C2, 0x0000, "GeForce 6800 LE", "GeForce 6800 LE" },
  { 0x0100, 0x00CC, 0x0000, "Quadro FX Go1400", "Quadro FX Go1400" },
  { 0x0100, 0x00CD, 0x0000, "Quadro FX 3450/4000 SDI", "Quadro FX 3450/4000 SDI" },
  { 0x0100, 0x00CE, 0x0000, "Quadro FX 1400", "Quadro FX 1400" },
  { 0x0200, 0x0140, 0x0000, "GeForce 6600 GT", "GeForce 6600 GT" },
  { 0x0100, 0x0141, 0x0000, "GeForce 6600", "GeForce 6600" },
  { 0x0100, 0x0142, 0x0000, "NV43", "NV43" },
  { 0x0100, 0x0143, 0x0000, "NV43", "NV43" },
  { 0x0100, 0x0145, 0x0000, "GeForce 6610 XL", "GeForce 6610 XL" },
  { 0x0100, 0x014B, 0x0000, "NV43", "NV43" },
  { 0x0100, 0x014C, 0x0000, "NV43GL", "NV43GL" },
  { 0x0100, 0x014D, 0x0000, "NV43GL", "NV43GL" },
  { 0x0100, 0x014E, 0x0000, "Quadro FX 540", "Quadro FX 540" },
  { 0x0100, 0x014F, 0x0000, "GeForce 6200", "GeForce 6200" },
  { 0x0100, 0x0160, 0x0000, "NV44", "NV44" },
  { 0x0100, 0x0161, 0x0000, "GeForce 6200 TurboCache(TM)", "GeForce 6200 TurboCache(TM)" },
  { 0x0100, 0x0162, 0x0000, "NV44", "NV44" },
  { 0x0100, 0x0163, 0x0000, "NV44", "NV44" },
  { 0x0100, 0x0165, 0x0000, "Quadro NVS 285", "Quadro NVS 285" },
  { 0x0100, 0x016E, 0x0000, "NV44GL", "NV44GL" },
  { 0x0100, 0x0220, 0x0000, "NV44", "NV44" },
  { 0x0100, 0x0221, 0x0000, "NV44", "NV44" },
  { 0x0100, 0x0222, 0x0000, "NV44", "NV44" },
  { 0x0100, 0x0211, 0x0000, "GeForce 6800", "GeForce 6800" },
  { 0x0100, 0x0212, 0x0000, "GeForce 6800 LE", "GeForce 6800 LE" },
  { 0x0200, 0x0215, 0x0000, "GeForce 6800 GT", "GeForce 6800 GT" },
  { 0x0200, 0x00F0, 0x0040, "GeForce 6800 Ultra", "GeForce 6800 Ultra" },
  { 0x0100, 0x00F0, 0x0041, "GeForce 6800", "GeForce 6800" },
  { 0x0200, 0x00F1, 0x0140, "GeForce 6600 GT", "GeForce 6600 GT" },
  { 0x0200, 0x00F1, 0x0000, "GeForce 6600 GT", "GeForce 6600 GT" },
  { 0x0100, 0x00F2, 0x0141, "GeForce 6600", "GeForce 6600" },
  { 0x0100, 0x00F2, 0x0000, "GeForce 6600", "GeForce 6600" },
  { 0x0100, 0x00F3, 0x014F, "GeForce 6200", "GeForce 6200" },
  { 0x0100, 0x00F3, 0x0000, "GeForce 6200", "GeForce 6200" },
  { 0x0100, 0x00F8, 0x004D, "Quadro FX 4400", "Quadro FX 4400" },
  { 0x0100, 0x00F8, 0x004E, "Quadro FX 3400", "Quadro FX 3400" },
  { 0x0100, 0x00F8, 0x0000, "Quadro FX 3400/4400", "Quadro FX 3400/4400" },
  { 0x0200, 0x00F9, 0x0040, "GeForce 6800 Ultra", "GeForce 6800 Ultra" },
  { 0x0200, 0x00F9, 0x0045, "GeForce 6800 GT", "GeForce 6800 GT" },
  { 0x0200, 0x00F9, 0x0000, "GeForce 6800 Series GPU", "GeForce 6800 Series GPU" },
  { 0x0100, 0x00FA, 0x0341, "GeForce PCX 5750", "GeForce PCX 5750" },
  { 0x0100, 0x00FA, 0x0000, "GeForce PCX 5750", "GeForce PCX 5750" },
  { 0x0100, 0x00FB, 0x0331, "GeForce PCX 5900", "GeForce PCX 5900" },
  { 0x0100, 0x00FB, 0x0000, "GeForce PCX 5900", "GeForce PCX 5900" },
  { 0x0100, 0x00FC, 0x0322, "GeForce PCX 5300", "GeForce PCX 5300" },
  { 0x0100, 0x00FC, 0x032B, "Quadro FX 330", "Quadro FX 330" },
  { 0x0100, 0x00FC, 0x0000, "GeForce PCX 5300", "GeForce PCX 5300" },
  { 0x0100, 0x00FD, 0x032A, "Quadro NVS 280 PCI-E", "Quadro NVS 280 PCI-E" },
  { 0x0100, 0x00FD, 0x032B, "Quadro FX 330", "Quadro FX 330" },
  { 0x0100, 0x00FD, 0x0000, "Quadro PCI-E Series", "Quadro PCI-E Series" },
  { 0x0100, 0x00FE, 0x0338, "Quadro FX 1300", "Quadro FX 1300" },
  { 0x0100, 0x00FE, 0x0000, "Quadro FX 1300", "Quadro FX 1300" },
  { 0x0000, 0x00FF, 0x0181, "GeForce PCX 4300", "GeForce PCX 4300" },
  { 0x0000, 0x00FF, 0x0000, "GeForce PCX 4300", "GeForce PCX 4300" },
  { 0x0000, 0x0112, 0x0000, "GeForce2 Go", "GeForce2 Go" },
  { 0x0000, 0x0174, 0x0000, "GeForce4 440 Go", "GeForce4 440 Go" },
  { 0x0000, 0x0175, 0x0000, "GeForce4 420 Go", "GeForce4 420 Go" },
  { 0x0000, 0x0176, 0x0000, "GeForce4 420 Go 32M", "GeForce4 420 Go 32M" },
  { 0x0000, 0x0177, 0x0000, "GeForce4 460 Go", "GeForce4 460 Go" },
  { 0x0000, 0x0179, 0x0000, "GeForce4 440 Go 64M", "GeForce4 440 Go 64M" },
  { 0x0000, 0x017D, 0x0000, "GeForce4 410 Go 16M", "GeForce4 410 Go 16M" },
  { 0x0000, 0x017C, 0x0000, "Quadro4 500 GoGL", "Quadro4 500 GoGL" },
  { 0x0000, 0x0186, 0x0000, "GeForce4 448 Go", "GeForce4 448 Go" },
  { 0x0000, 0x0187, 0x0000, "GeForce4 488 Go", "GeForce4 488 Go" },
  { 0x0000, 0x018D, 0x0000, "GeForce4 448 Go", "GeForce4 448 Go" },
  { 0x0000, 0x0286, 0x0000, "GeForce4 4200 Go", "GeForce4 4200 Go" },
  { 0x0000, 0x028C, 0x0000, "Quadro4 700 GoGL", "Quadro4 700 GoGL" },
  { 0x0000, 0x0316, 0x0000, "NV31M", "NV31M" },
  { 0x0000, 0x0317, 0x0000, "NV31M Pro", "NV31M Pro" },
  { 0x0100, 0x031A, 0x0000, "GeForce FX Go5600", "GeForce FX Go5600" },
  { 0x0100, 0x031B, 0x0000, "GeForce FX Go5650", "GeForce FX Go5650" },
  { 0x0100, 0x031C, 0x0000, "Quadro FX Go700", "Quadro FX Go700" },
  { 0x0100, 0x031D, 0x0000, "NV31GLM", "NV31GLM" },
  { 0x0100, 0x031E, 0x0000, "NV31GLM Pro", "NV31GLM Pro" },
  { 0x0100, 0x031F, 0x0000, "NV31GLM Pro", "NV31GLM Pro" },
  { 0x0100, 0x0324, 0x0000, "GeForce FX Go5200", "GeForce FX Go5200" },
  { 0x0100, 0x0325, 0x0000, "GeForce FX Go5250", "GeForce FX Go5250" },
  { 0x0100, 0x0328, 0x0000, "GeForce FX Go5200 32M/64M", "GeForce FX Go5200 32M/64M" },
  { 0x0100, 0x0329, 0x0000, "NV34MAP", "NV34MAP" },
  { 0x0100, 0x032C, 0x0000, "GeForce FX Go53xx", "GeForce FX Go53xx" },
  { 0x0100, 0x032D, 0x0000, "GeForce FX Go5100", "GeForce FX Go5100" },
  { 0x0100, 0x0347, 0x0000, "GeForce FX Go5700", "GeForce FX Go5700" },
  { 0x0100, 0x0348, 0x0000, "GeForce FX Go5700", "GeForce FX Go5700" },
  { 0x0100, 0x0349, 0x0000, "NV36M Pro", "NV36M Pro" },
  { 0x0100, 0x034B, 0x0000, "NV36MAP", "NV36MAP" },
  { 0x0100, 0x034C, 0x0000, "Quadro FX Go1000", "Quadro FX Go1000" },
  { 0x0100, 0x00C8, 0x0000, "GeForce Go 6800", "GeForce Go 6800" },
  { 0x0100, 0x00C9, 0x0000, "GeForce Go 6800 Ultra", "GeForce Go 6800 Ultra" },
  { 0x0100, 0x00CC, 0x0000, "Quadro FX Go1400", "Quadro FX Go1400" },
  { 0x0100, 0x0140, 0x0000, "NV43", "NV43" },
  { 0x0100, 0x0144, 0x0000, "GeForce Go 6600", "GeForce Go 6600" },
  { 0x0100, 0x0147, 0x0000, "NV43", "NV43" },
  { 0x0100, 0x0148, 0x0000, "GeForce Go 6600", "GeForce Go 6600" },
  { 0x0100, 0x0149, 0x0000, "NV43", "NV43" },
  { 0x0100, 0x014C, 0x0000, "NV43GL", "NV43GL" },
  { 0x0100, 0x0164, 0x0000, "GeForce Go 6200", "GeForce Go 6200" },
  { 0x0100, 0x0167, 0x0000, "GeForce Go 6200", "GeForce Go 6200" },
  { 0x0100, 0x0169, 0x0000, "NV44M", "NV44M" },
  { 0x0100, 0x016B, 0x0000, "NV44GLM", "NV44GLM" },
  { 0x0100, 0x016C, 0x0000, "NV44GLM", "NV44GLM" },
  { 0x0100, 0x016D, 0x0000, "NV44GLM", "NV44GLM" },
  { 0x0100, 0x0228, 0x0000, "NV44M", "NV44M" },
  { 0x0300, 0x0091, 0x0000, "G70", "G70" }, // 7800
  { 0x0300, 0x009D, 0x0000, "Quadro FX 4500", "Quadro FX 4500" },
  /*
  {0x0322,0x100}, // FX5200
  {0x0331,0x100}, // FX5900
  {0x0161,0x100}, // 6200
  {0x0045,0x200}, // 6800
  {0x0091,0x300}, // 7800
  {0x00f8,0x300}, // Quadro FX 4400
  */
};

#define ATI_DEVICE_ID(level, friendly, code, deviceId) {level,deviceId,0,friendly}

static const DeviceLevelInfo ATIDevices[]={
//  {0x100,0x4E54,0,"Mobility FireGL"},
//  {0x100,0x4150,0,"Radeon 9600"},
//  {0x100,0x4E50,0,"Radeon 9700"},
//  {0x100,0x4E48,0,"Radeon 9800"},
//  {0x100,0x5460,0,"Radeon X300"},
//  {0x100,0x5653,0,"Radeon X700"},
//  {0x200,0x5D4A,0,"Radeon X800"},
//  {0x300,0x4B6C,0,"Radeon X1800"},

  ATI_DEVICE_ID( 0x0000, "ATI ES1000", "RN50", 0x515E ),
  ATI_DEVICE_ID( 0x0000, "ATI ES1000", "RN50", 0x515F ),
  ATI_DEVICE_ID( 0x0000, "ATI ES1000", "RN50", 0x5969 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL 8800", "R200GL", 0x5148 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL T2", "RV350GL", 0x4154 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL T2 Secondary", "RV350GL", 0x4174 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V3100", "RV370GL", 0x5B64 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V3100 Secondary", "RV370GL", 0x5B74 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V3200", "RV380GL", 0x3E54 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V3200 Secondary", "RV380GL", 0x3E74 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V3300", "RV410GL", 0x5E49 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V3300 ", "RV515GL", 0x7152 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V3300 Secondary", "RV515GL", 0x7172 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V3400 ", "RV530GL", 0x71D2 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V3400 Secondary", "RV530GL", 0x71F2 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V5000", "RV410GL", 0x5E48 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V5000 Secondary", "RV410GL", 0x5E68 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V5100", "R423GL", 0x5551 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V5100 Secondary", "R423GL", 0x5571 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V5200", "RV530GL", 0x71DA ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V5200 Secondary", "RV530GL", 0x71FA ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V5300", "R520GL", 0x7105 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V5300 Secondary", "R520GL", 0x7125 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V7100", "R423GL", 0x5550 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V7100 Secondary", "R423GL", 0x5570 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V7200", "R480GL", 0x5D50 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V7200 ", "R520GL", 0x7104 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V7200 Secondary", "R520GL", 0x7124 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V7200 Secondary", "R480GL", 0x5D70 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V7300", "R520GL", 0x710E ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V7300 Secondary", "R520GL", 0x712E ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V7350", "R520GL", 0x710F ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL V7350 Secondary", "R520GL", 0x712F ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL X1", "R300GL", 0x4E47 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL X1 Secondary", "R300GL", 0x4E67 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL X2-256/X2-256t", "R350GL", 0x4E4B ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL X2-256/X2-256t Secondary", "R350GL", 0x4E6B ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL X3-256", "R420GL", 0x4A4D ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL X3-256 Secondary", "R420GL", 0x4A6D ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL Z1", "R300GL", 0x4147 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireGL Z1 Secondary", "R300GL", 0x4167 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireMV 2200", "RV370", 0x5B65 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireMV 2200 PCI", "RV280", 0x5965 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireMV 2200 PCI Secondary", "RV280", 0x5D45 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireMV 2200 Secondary", "RV370", 0x5B75 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireMV 2400", "RV380", 0x3151 ),
  ATI_DEVICE_ID( 0x0000, "ATI FireMV 2400 Secondary", "RV380", 0x3171 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility FireGL 7800", "M7", 0x4C58 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility FireGL 9000", "M9", 0x4C64 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility FireGL T2/T2e", "M10GL", 0x4E54 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility FireGL V3100", "M22GL", 0x5464 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility FireGL V3200", "M24GL", 0x3154 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility FireGL V5000", "M26GL", 0x564A ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility FireGL V5000", "M26GL", 0x564B ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility FireGL V5100", "M28GL", 0x5D49 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility FireGL V5200", "M56GL", 0x71C4 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility FireGL V5250", "M56GL", 0x71D4 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility FireGL V7100", "M58GL", 0x7106 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility FireGL V7200", "M58GL", 0x7103 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon", "M6", 0x4C59 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon 7500", "M7", 0x4C57 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon 9500", "M10", 0x4E52 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon 9550", "M12", 0x4E56 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon 9600/9700 Series", "M10", 0x4E50 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon 9800", "M18", 0x4A4E ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X1300", "M52", 0x7149 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X1300", "M52", 0x714B ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X1300", "M52", 0x714A ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X1300", "M52", 0x714C ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X1350", "M52", 0x718C ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X1350", "M52", 0x718B ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X1350", "M52", 0x7196 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X1400", "M54", 0x7145 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X1450", "M54", 0x718D ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X1450 ", "M54", 0x7186 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X1600", "M56", 0x71C5 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X1700", "M56", 0x71DE ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X1700", "M56", 0x71D6 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X1800", "M58", 0x7102 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X1800 XT", "M58", 0x7101 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X300", "M22", 0x5460 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X300", "M22", 0x5461 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X300", "M24", 0x3152 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X600", "M24", 0x3150 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X700", "M26", 0x5652 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X700 ", "M26", 0x5653 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X700 Secondary", "M26", 0x5673 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X700 XL", "M26", 0x564F ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X800", "M28", 0x5D4A ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon X800 XT", "M28", 0x5D48 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility / Radeon 9000 ", "M9", 0x4C66 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility / Radeon 9000 Secondary", "M9", 0x4C6E ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility / Radeon 9200", "M9Plus", 0x5C63 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility / Radeon 9200 ", "M9Plus", 0x5C61 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility / Radeon 9200 Secondary", "M9Plus", 0x5C43 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility / Radeon 9200 Secondary", "M9Plus", 0x5C41 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9000 XT IGP Series", "RS350", 0x7834 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9100 IGP", "RS300", 0x5834 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9550", "RV350", 0x4153 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9550 Secondary", "RV350", 0x4173 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9600 Series", "RV350", 0x4150 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9600 Series", "RV350", 0x4151 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9600 Series", "RV360", 0x4152 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9600 Series", "RV350", 0x4155 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9600 Series", "RV350", 0x4E51 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9600 Series Secondary", "RV350", 0x4170 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9600 Series Secondary", "RV350", 0x4E71 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9600 Series Secondary", "RV350", 0x4171 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9600 Series Secondary", "RV360", 0x4172 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9600 Series Secondary", "RV350", 0x4175 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X800", "R430", 0x554F ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X800 GT", "R430", 0x554E ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X800 SE Secondary", "R430", 0x556E ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X800 XL", "R430", 0x554D ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X800 XL Secondary", "R430", 0x556D ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X850 PRO", "R481", 0x4B4B ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X850 PRO Secondary", "R481", 0x4B6B ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X850 SE", "R481", 0x4B4A ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X850 SE Secondary", "R481", 0x4B6A ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X850 XT", "R481", 0x4B49 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X850 XT Platinum Edition", "R481", 0x4B4C ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X850 XT Platinum Edition Secondary", "R481", 0x4B6C ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X850 XT Secondary", "R481", 0x4B69 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon Xpress 1200 Series", "RS600", 0x793F ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon Xpress 1200 Series", "RS690M", 0x791F ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon Xpress 1200 Series", "RS600M", 0x7942 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon Xpress 1200 Series", "RS600", 0x7941 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon Xpress 1200 Series", "RS690", 0x791E ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon Xpress Series", "RS480", 0x5954 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon Xpress Series", "RS482", 0x5974 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon Xpress Series", "RS400", 0x5A43 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon Xpress Series", "RS480", 0x5854 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon Xpress Series", "RS482", 0x5874 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon Xpress Series", "RC410", 0x5A63 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon Xpress Series", "RS480M", 0x5955 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon Xpress Series", "RS400", 0x5A41 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon Xpress Series", "RC410", 0x5A61 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon Xpress Series", "RC410M", 0x5A62 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon Xpress Series", "RS400M", 0x5A42 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon Xpress Series", "RS482M", 0x5975 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon 7000 IGP", "RS250M", 0x4437 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon 9000 IGP", "RS300M", 0x7835 ),
  ATI_DEVICE_ID( 0x0000, "ATI Mobility Radeon 9000/9100 IGP Series", "RS300M", 0x5835 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 7000 IGP", "RS250", 0x4237 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 7000 Series", "RV100", 0x5159 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 7000 Series", "RV100", 0x515A ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 7200 Series", "default", 0x5144 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 7500 Series", "RV200", 0x5157 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 8500 AIW", "R200", 0x4242 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 8500 Series", "R200", 0x514C ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9000 PRO", "D9", 0x4C67 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9000 PRO Secondary", "D9", 0x4C6F ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9000 Series", "RV250", 0x4967 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9000 Series", "RV250", 0x4966 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9000 Series Secondary", "RV250", 0x496E ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9000 Series Secondary", "RV250", 0x496F ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9000/9100 IGP Series", "RS300", 0x5834 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9000/9100 PRO IGP Series", "RS350", 0x7834 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9100 Series", "R200", 0x514D ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9250", "RV280", 0x5960 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9250 Secondary", "RV280", 0x5940 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9250/9200 Series", "RV280", 0x5961 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9250/9200 Series", "RV280", 0x5964 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9250/9200 Series", "RV280", 0x5962 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9250/9200 Series Secondary", "RV280", 0x5942 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9250/9200 Series Secondary", "RV280", 0x5D44 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon 9250/9200 Series Secondary", "RV280", 0x5941 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9500", "R300", 0x4144 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9500 ", "R350", 0x4149 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9500 PRO / 9700", "R300", 0x4E45 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9500 PRO / 9700 Secondary", "R300", 0x4E65 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9500 Secondary", "R300", 0x4164 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9500 Secondary", "R350", 0x4169 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9600 TX", "R300", 0x4E46 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9600 TX Secondary", "R300", 0x4E66 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9600TX", "R300", 0x4146 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9600TX Secondary", "R300", 0x4166 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9700 PRO", "R300", 0x4E44 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9700 PRO Secondary", "R300", 0x4E64 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9800", "R350", 0x4E49 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9800 PRO", "R350", 0x4E48 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9800 PRO Secondary", "R350", 0x4E68 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9800 SE", "R350", 0x4148 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9800 SE Secondary", "R350", 0x4168 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9800 Secondary", "R350", 0x4E69 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9800 XT", "R360", 0x4E4A ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon 9800 XT Secondary", "R360", 0x4E6A ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon IGP 320", "A3", 0x4136 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon IGP 320M", "U1", 0x4336 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon IGP 340", "RS200", 0x4137 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon IGP 340M", "RS200M", 0x4337 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 PRO", "RV515", 0x7143 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 PRO Secondary", "RV515", 0x7163 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series", "RV515", 0x715F ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series", "RV515", 0x7183 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series", "RV515", 0x7146 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series", "RV530", 0x71CE ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series", "RV515", 0x7142 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series", "RV515", 0x714E ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series", "RV515", 0x715E ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series", "RV515", 0x7147 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series", "RV515", 0x714D ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series", "RV515", 0x7180 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series", "RV515", 0x7187 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series", "RV515", 0x71C3 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series Secondary", "RV515", 0x716D ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series Secondary", "RV515", 0x71A7 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series Secondary", "RV515", 0x7166 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series Secondary", "RV515", 0x717E ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series Secondary", "RV515", 0x717F ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series Secondary", "RV515", 0x71A0 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series Secondary", "RV535", 0x71E3 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series Secondary", "RV530", 0x71EE ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series Secondary", "RV515", 0x7162 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series Secondary", "RV515", 0x716E ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series Secondary", "RV515", 0x7167 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1300 Series Secondary", "RV515", 0x71A3 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1600 Series", "RV530", 0x71C0 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1600 Series", "RV515", 0x7181 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1600 Series", "RV530", 0x71CD ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1600 Series", "RV515", 0x7140 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1600 Series", "RV530", 0x71C2 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1600 Series Secondary", "RV530", 0x71E2 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1600 Series Secondary", "RV515", 0x7160 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1600 Series Secondary", "RV530", 0x71E0 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1600 Series Secondary", "RV515", 0x71A1 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1600 Series Secondary", "RV530", 0x71ED ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1650 Series", "RV530", 0x71C6 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1650 Series", "RV535", 0x71C1 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1650 Series", "R580", 0x7291 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1650 Series Secondary", "R580", 0x72B1 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1650 Series Secondary", "RV53", 0x71E6 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X1650 Series Secondary", "RV535", 0x71E1 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1800 Series", "R520", 0x7100 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1800 Series", "R520", 0x7109 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1800 Series", "R520", 0x710A ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1800 Series", "R520", 0x7108 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1800 Series", "R520", 0x710B ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1800 Series", "R520", 0x710C ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1800 Series Secondary", "R520", 0x7128 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1800 Series Secondary", "R520", 0x712A ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1800 Series Secondary", "R520", 0x7120 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1800 Series Secondary", "R520", 0x7129 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1800 Series Secondary", "R520", 0x712B ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1800 Series Secondary", "R520", 0x712C ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series", "R580", 0x7246 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series", "R580", 0x7243 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series", "R580", 0x7248 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series", "R580", 0x724C ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series", "R580", 0x724F ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series", "R580", 0x7245 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series", "R580", 0x7247 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series", "R580", 0x7249 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series", "R580", 0x724A ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series", "R580", 0x724B ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series", "R580", 0x724D ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series Secondary", "R580", 0x7266 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series Secondary", "R580", 0x7268 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series Secondary", "R580", 0x726C ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series Secondary", "R580", 0x7263 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series Secondary", "R580", 0x7265 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series Secondary", "R580", 0x7267 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series Secondary", "R580", 0x7269 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series Secondary", "R580", 0x726A ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series Secondary", "R580", 0x726B ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series Secondary", "R580", 0x726D ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1900 Series Secondary", "R580", 0x726F ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1950 Series", "R580", 0x7280 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1950 Series", "R580", 0x7240 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1950 Series", "R580", 0x7244 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1950 Series Secondary", "R580", 0x7264 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1950 Series Secondary", "R580", 0x72A0 ),
  ATI_DEVICE_ID( 0x0300, "ATI Radeon X1950 Series Secondary", "R580", 0x7260 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X300/X550 Series", "RV370", 0x5B60 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X300/X550 Series Secondary", "RV370", 0x5B70 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X550", "RV370", 0x5B63 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X550 Secondary", "RV370", 0x5B73 ),
  ATI_DEVICE_ID( 0x0000, "ATI Radeon X600 Series", "RV380x", 0x5B62 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X600 Series Secondary", "RV380x", 0x5B72 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X600/X550 Series", "RV380", 0x3E50 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X600/X550 Series Secondary", "RV380", 0x3E70 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X700", "RV410", 0x5E4D ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X700 PRO", "RV410", 0x5E4B ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X700 PRO Secondary", "RV410", 0x5E6B ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X700 SE", "RV410", 0x5E4C ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X700 SE Secondary", "RV410", 0x5E6C ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X700 SE Secondary", "RV410", 0x5E6C ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X700 Secondary", "RV410", 0x5E6D ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X700 Series", "RV410", 0x5657 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X700 Series Secondary", "RV410", 0x5677 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X700 XT", "RV410", 0x5E4A ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X700 XT Secondary", "RV410", 0x5E6A ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X700/X500 Series", "RV410", 0x5E4F ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X700/X500 Series Secondary", "RV410", 0x5E6F ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 GT", "R423", 0x554B ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 GT Secondary", "R423", 0x556B ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 GTO", "R423", 0x5549 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 GTO", "R480", 0x5D4F ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 GTO Secondary", "R430", 0x556F ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 GTO Secondary", "R423", 0x5569 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X800 PRO", "R420", 0x4A49 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X800 PRO Secondary", "R420", 0x4A69 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 SE ", "R420", 0x4A4F ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 SE Secondary", "R420", 0x4A6F ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 Series", "R420", 0x4A4C ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 Series", "R420", 0x4A48 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 Series", "R423", 0x5548 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 Series", "R420", 0x4A4A ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 Series Secondary", "R420", 0x4A6C ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 Series Secondary", "R420", 0x4A68 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 Series Secondary", "R420", 0x4A6A ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 Series Secondary", "R423", 0x5568 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 VE", "R420", 0x4A54 ),
  ATI_DEVICE_ID( 0x0100, "ATI Radeon X800 VE Secondary", "R420", 0x4A74 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X800 XT", "R423", 0x5D57 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X800 XT ", "R420", 0x4A4B ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X800 XT Platinum Edition", "R420", 0x4A50 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X800 XT Platinum Edition ", "R423", 0x554A ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X800 XT Platinum Edition Secondary", "R423", 0x556A ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X800 XT Platinum Edition Secondary", "R420", 0x4A70 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X800 XT Secondary", "R423", 0x5D77 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X800 XT Secondary", "R420", 0x4A6B ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X850 PRO Secondary", "R480", 0x5D6F ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X850 XT", "R480", 0x5D52 ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X850 XT Platinum Edition", "R480", 0x5D4D ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X850 XT Platinum Edition Secondary", "R480", 0x5D6D ),
  ATI_DEVICE_ID( 0x0200, "ATI Radeon X850 XT Secondary", "R480", 0x5D72 ),
};

// Direct3D 10 NEEDS UPDATE 
// static int FindDevice(
//   const D3DADAPTER_IDENTIFIER9 &id, const DeviceLevelInfo *devices, int nDevices
// )
// {
//   int deviceLevel = INT_MAX;
//   {
//     for (int i=0; i<nDevices; i++)
//     {
//       if (id.DeviceId==devices[i].deviceId)
//       {
//         LogF("Display adapter identified as %s, level %x",devices[i].name1,devices[i].level);
//         saturateMin(deviceLevel,devices[i].level);
//         break;
//       }
//     }
//   }
//   if (deviceLevel==INT_MAX)
//   {
//     // if we cannot recognize the device, let features do the work
//   }
//   return deviceLevel;
// }

int EngineDDT::GetAdapterLevel(int adapter) const
{
  Fail("DX10 TODO");
  return -1;
  // Direct3D 10 NEEDS UPDATE 
//   // based on device ID
//   int deviceLevel = INT_MAX;
//   // scan for some capabilities identifying generation by vendor
//   int genLevel = 0x300;
//   // check feature set (shader model, video memory)
//   int featuresLevel = 0x300;
// 
//   D3DADAPTER_IDENTIFIER9 id;
//   _direct3D->GetAdapterIdentifier(adapter,0,&id);
// 
//   D3DCAPS9 caps;
//   memset(&caps,0,sizeof(caps));
//   _direct3D->GetDeviceCaps(adapter,_devType,&caps);
//   
//   {
//     // check vertex and pixel shader model
//     int vs = caps.VertexShaderVersion&0xffff;
//     if (vs<0x300) saturateMin(featuresLevel,0x200);
//     if (vs<0x200) saturateMin(featuresLevel,0); // no HW vertex shaders - unsupported
//     int ps = caps.PixelShaderVersion&0xffff;
//     if (ps<0x300) saturateMin(featuresLevel,0x200);
//     // check amount of video memory (hard to check)
//     // less then temps 32 on ATI means X300 or lower - assume Low End
//     // less then temps 32 on nVidia means 5900 or lower - assume Low End
//     if (caps.PS20Caps.NumTemps<32) genLevel = 0x100;
//     if (
//       _direct3D->CheckDeviceFormat(
//         adapter,_devType,D3DFMT_X8R8G8B8,
//         D3DUSAGE_RENDERTARGET,D3DRTYPE_TEXTURE,
//         D3DFMT_A16B16G16R16
//       )!=D3D_OK
//     )
//     {
//       // no D3DFMT_A16B16G16R16 RT Texture on nVidia means 6200 or lower - assume Low End
//       // all SM 2.0 ATI support D3DFMT_A16B16G16R16
//       // 
//       genLevel = 0x100;
//     }
//   }
// 
//   {  
//     if (id.VendorId==0x1002) // ATI
//     {
//       deviceLevel = FindDevice(id,ATIDevices,lenof(ATIDevices));
//     }
//     else if (id.VendorId==0x10DE) // nVidia
//     {
//       deviceLevel = FindDevice(id,NVDevices,lenof(NVDevices));
//     }
//     else if (id.VendorId==0x8086) // Intel
//     {
//       // Intel is most likely on-board - assume middle to low end
//       deviceLevel = 0x200;
//     }
//     else if (id.VendorId==0x5333) // S3
//     {
//       deviceLevel = 0x200;
//     }
//     else if (id.VendorId==0x1039) // SiS
//     {
//       deviceLevel = 0x200;
//     }
//   }  
//   // select the minimum of those values
//   return intMin(deviceLevel,featuresLevel,genLevel);
  
}

void EngineDDT::CheckFreeTextureMemory(int &local, int &nonlocal)
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

void EngineDDT::CheckLocalNonlocalTextureMemory()
{
  if (_ddraw && _localPrediction)
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



void EngineDDT::InitAdapter()
{
  // Initialize the adapter somehow
  D3DAdapter = -1;

  // Direct3D 10 NEEDS UPDATE 
//   int adapter = D3DAdapter;
// 
//   if (adapter<0 || adapter>=(int)_direct3D->GetAdapterCount())
//   {
//     // auto-detect best adapter
// 
//     int maxLevel = 0;
//     for (int ad=0; ad<(int)_direct3D->GetAdapterCount(); ad++)
//     {
//       int level = GetAdapterLevel(ad);
//       if (level>maxLevel)
//       {
//         maxLevel = level;
//         adapter = ad;
//       }
//     }
//     if (adapter<0)
//     {
//       // if no adapter is recognized, select the first one
//       adapter = 0;
//     }
//   }
// 
//   _deviceLevel = GetAdapterLevel(adapter);
//   D3DAdapter = adapter;
}

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
*/

void EngineDDT::Init3D()
{
  int adapter = D3DAdapter;

  FindMode(adapter);

  HRESULT hr;

  // Create device and a swap chain
  hr = D3D10CreateDeviceAndSwapChain(NULL, _devType, NULL, 0 /*D3D10_CREATE_DEVICE_DEBUG*/ /*| D3D10_CREATE_DEVICE_SWITCH_TO_REF*/, D3D10_SDK_VERSION, &_pp, _d3DSwapChain.Init(), _d3DDevice.Init());
  if (FAILED(hr))
  {
    ErrorMessage("Cannot create DX10 device");
  }
  RptF("Warning: Debug D3D10 si being used");

//   // Use reference device
//   ComRef<ID3D10SwitchToRef> switchToRef;
//   _d3DDevice->QueryInterface(__uuidof(ID3D10SwitchToRef), (void**)switchToRef.Init());
//   switchToRef->SetUseRef(TRUE);

  // Create a rendertarget view (use data from the swap chain)
  {
    // Get pointer to the swap chain data (f.i. of type ID3D10Texture2D, but could be ID3D10Resource as well)
    ComRef<ID3D10Texture2D> backBuffer;
    hr = _d3DSwapChain->GetBuffer(0, __uuidof(ID3D10Texture2D), (LPVOID*)backBuffer.Init());
    if (FAILED(hr))
    {
      ErrorMessage("Cannot retrieve rendertarget buffer");
    }

    // Create the rendertarget view
    hr = _d3DDevice->CreateRenderTargetView(backBuffer, NULL, _renderTarget.Init());
    if (FAILED(hr))
    {
      ErrorMessage("Cannot create rendertarget view");
    }
  }

  // Create a depth-stencil view (create brand new depth-stencil resource)
  {
    // Create the depth-stencil data
    ComRef<ID3D10Texture2D> depthStencil;
    D3D10_TEXTURE2D_DESC descDepth;
    ZeroMemory(&descDepth, sizeof(descDepth));
    descDepth.Width = _pp.BufferDesc.Width;
    descDepth.Height = _pp.BufferDesc.Height;
    descDepth.MipLevels = 1;
    descDepth.ArraySize = 1;
    descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    descDepth.SampleDesc.Count = 1;
    descDepth.SampleDesc.Quality = 0;
    descDepth.Usage = D3D10_USAGE_DEFAULT;
    descDepth.BindFlags = D3D10_BIND_DEPTH_STENCIL;
    descDepth.CPUAccessFlags = 0;
    descDepth.MiscFlags = 0;
    hr = _d3DDevice->CreateTexture2D(&descDepth, NULL, depthStencil.Init());
    if (FAILED(hr))
    {
      ErrorMessage("Cannot create depth-stencil buffer");
    }

    // Create the depth-stencil view
    _d3DDevice->CreateDepthStencilView(depthStencil, NULL, _depthStencil.Init());
    if (FAILED(hr))
    {
      ErrorMessage("Cannot create depth-stencil view");
    }
  }

  // Set rendertarget and depth-stencil surface
  ID3D10RenderTargetView *renderTargetArray[] = {_renderTarget};
  _d3DDevice->OMSetRenderTargets(1, renderTargetArray, _depthStencil);

  // Direct3D 10 NEEDS UPDATE 
//   // try to create device (from best to worst)
//   HRESULT err = D3D_OK;
// #ifdef DEBUG_PERFHUD
//   for (UINT currAdapter = 0; currAdapter < _direct3D->GetAdapterCount(); currAdapter++)
//   {
//     D3DADAPTER_IDENTIFIER9 identifier;
//     _direct3D->GetAdapterIdentifier(currAdapter, 0, &identifier);
//     if (strstr(identifier.Description,"NVPerfHUD"))
//     {
//       adapter = currAdapter;
//       _devType = D3DDEVTYPE_REF;
//       break;
//     }
//   }
// #endif
// 
//   D3DCAPS9 caps;
//   _direct3D->GetDeviceCaps(adapter,_devType,&caps);
// 
//   DWORD behavior = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
// 
//   if (caps.DevCaps&D3DDEVCAPS_HWTRANSFORMANDLIGHT)
//   {
//     if ((caps.VertexShaderVersion&0xffff)>=0x200)
//     {
//       if (!DebugVS)
//       {
//         behavior = D3DCREATE_HARDWARE_VERTEXPROCESSING;
//       }
//     }
//     if (caps.DevCaps&D3DDEVCAPS_PUREDEVICE)
//     {
//       if (!DebugVS)
//       {
//         behavior |= D3DCREATE_PUREDEVICE;
//       }
//       LogF("Adapter %d: T&L HW detected (Pure)",adapter);
//     }
//     else
//     {
//       LogF("Adapter %d: T&L HW detected",adapter);
//     }
//   }
// 
//   D3DADAPTER_IDENTIFIER9 adid;
//   _direct3D->GetAdapterIdentifier(adapter, 0, &adid);
//   
//   RString deviceId = Format("%d,%d,%d",adid.VendorId,adid.DeviceId,adid.SubSysId);
// 
//   // decide about DDraw device based on monitor handle
//   DDEnumCallbackExContext ctx;
//   ctx.hm = _direct3D->GetAdapterMonitor(adapter);
//   DirectDrawEnumerateEx(
//     DDEnumCallbackEx,&ctx,
//     DDENUM_ATTACHEDSECONDARYDEVICES|DDENUM_DETACHEDSECONDARYDEVICES|DDENUM_NONDISPLAYDEVICES
//   );
//   //if (strcmp(deviceId,_lastDeviceId))
//   {
//     // before creating device we want to check its memory properties using DX7
//     // we do not know how adapters ID maps to ddraw GUIDS
//     // we can handle only the simple case - default adapter is used
//     
//     DirectDrawCreateEx(ctx.guidPtr,(void **)_ddraw.Init(),IID_IDirectDraw7,NULL);
//     
//     _lastDeviceId = 0;
//     _localVramAmount = 0;
//     _nonlocalVramAmount = 0;
//     _freeLocalVramAmount = 0;
//     _freeNonlocalVramAmount = 0;
//     _freeEstDirty = 0;
//     _freeEstCheckTime = 0;
//     
//     if (_ddraw)
//     {
//       {
//         DDSCAPS2 caps;
//         DWORD totalmem = 0,freemem = 0;
//         caps.dwCaps = DDSCAPS_LOCALVIDMEM|DDSCAPS_VIDEOMEMORY;
//         caps.dwCaps2 = 0;
//         caps.dwCaps3 = 0;
//         caps.dwCaps4 = 0;
//         _ddraw->GetAvailableVidMem(&caps,&totalmem,&freemem);
//         _localVramAmount = totalmem;
//         _freeLocalVramAmount = freemem;
//       }
//       {
//         DDSCAPS2 caps;
//         DWORD totalmem = 0,freemem = 0;
//         caps.dwCaps = DDSCAPS_NONLOCALVIDMEM|DDSCAPS_VIDEOMEMORY;
//         caps.dwCaps2 = 0;
//         caps.dwCaps3 = 0;
//         caps.dwCaps4 = 0;
//         _ddraw->GetAvailableVidMem(&caps,&totalmem,&freemem);
//         _nonlocalVramAmount = totalmem;
//         _freeNonlocalVramAmount = freemem;
//       }
//       _freeEstDirty = 0;
//       _freeEstCheckTime = ::GlobalTickCount();
//       
//     }
//     
//     // if this is plain wrong, we will use heuristics:
//     // assume half of VRAM, other half AGP'
//     // round to nearest pow2
//     _lastDeviceId = deviceId;
//   }
// 
//   int backBufferCount = _pp.BackBufferCount;
//   // do not retry for more than 1 minute
//   for(int maxRetry=60;--maxRetry>=0;)
//   {
//     err = _direct3D->CreateDevice(
//       adapter,_devType,_hwndApp,behavior,
//       &_pp,_d3DDevice.Init()
//     );
//     if (err==D3DERR_DEVICELOST)
//     {
//       // device is lost - wait for a while and try again
//       Sleep(1000);
//     }
//     else
//     {
//       // on success or other errors break the loop and do not retry
//       break;
//     }
//   }
//   
//   // number of back buffer may be adjusted - try again
//   if ((err!=D3D_OK || !_d3DDevice) && backBufferCount>1)
//   {
//     _pp.BackBufferCount = 1;
//     err = _direct3D->CreateDevice
//     (
//       adapter,_devType,_hwndApp,behavior,
//       &_pp,_d3DDevice.Init()
//     );
//   }
//   if (err!=D3D_OK || !_d3DDevice)
//   {
//     DDError9("Cannot create 3D device",err);
//     RptF
//     (
//       "Resolution failed: %dx%dx (%d Hz)",
//       _pp.BackBufferWidth,_pp.BackBufferHeight,_pp.FullScreen_RefreshRateInHz
//     );
//     D3DADAPTER_IDENTIFIER9 id;
//     _direct3D->GetAdapterIdentifier(adapter, 0, &id);
//     ErrorMessage
//     (
//       "Cannot create 3D device:\n"
//       "  Adapter %d (%s) %s\n"
//       "  Resolution %dx%d, format %s/%s/%s, refresh %d Hz.\n"
//       "  Error %s",
//       (int)adapter,(const char *)id.Description,
//       _pp.Windowed ? "Windowed" : "Fullscreen",
//       _pp.BackBufferWidth,_pp.BackBufferHeight,
//       SurfaceFormatName(_pp.BackBufferFormat),
//       SurfaceFormatName(_rtFormat),
//       SurfaceFormatName(_pp.AutoDepthStencilFormat),
//       _pp.FullScreen_RefreshRateInHz,
//       DXGetErrorString8(err)
//     );
//   }
//   LogF("Backbuffers: %d",_pp.BackBufferCount);
//   
//   _canWBuffer = false;
//   //_canWBuffer = (caps.RasterCaps&D3DPRASTERCAPS_WBUFFER)!=0;
//   _canWBuffer32 = _canWBuffer;
// 
//   _canHWMouse = (caps.CursorCaps&D3DCURSORCAPS_COLOR)!=0;
// 
//   _vertexShaders = caps.VertexShaderVersion&0xffff;
//   _pixelShaders = caps.PixelShaderVersion&0xffff;
//   if (_vertexShaders)
//   {
//     LogF("Vertex shaders version %d.%d",_vertexShaders>>8,_vertexShaders&0xff);
//   }
//   if (_pixelShaders)
//   {
//     LogF("Pixel shaders version %d.%d",_pixelShaders>>8,_pixelShaders&0xff);
//   }
//   
//   if (_pixelShaders<0x200)
//   {
//     ErrorMessage("Pixel Shader 2.0 support required");
//   }
// 
//   if (_canWBuffer)
//   {
//     LogF("Can w-buffer");
//   }
// 
//   _maxAFLevel = caps.MaxAnisotropy;
//   saturate(_maxAFLevel,1,16);
//   _maxAF = 0;
//   if (_maxAFLevel>=4) _maxAF = 4;
//   else if (_maxAFLevel>=2) _maxAF = 2;
// 
//   // test for some known adapters
//   // (based on driver name?)
// 
//   //_maxLights = 8;
//   // patch: detection of known device capabilities
//   LogF(
//     "Format %s/%s/%s, %dx%d",
//     SurfaceFormatName(_pp.BackBufferFormat),
//     SurfaceFormatName(_rtFormat),
//     SurfaceFormatName(_pp.AutoDepthStencilFormat),
//     _w,_h
//   );

  // check actual device capabilities
  Init3DState();
}

void EngineDDT::Done3D()
{
  Done3DState();
  _d3DDevice.Free();
}
AbstractTextBank *EngineDDT::TextBank() {return _textBank;}

void EngineDDT::CreateTextBank(ParamEntryPar cfg, ParamEntryPar fcfg)
{
  if (_textBank.IsNull())
  //if (!_textBank)
  {
    _textBank = new TextBankD3DT(this, cfg, fcfg);
    if (_textBank.IsNull())
    //if (!_textBank)
    {
      ErrorMessage("Cannot create texture bank.");
      return;
    }
    _textBank->SetTextureQuality(_textureQuality);
  }
}

void EngineDDT::DestroyTextBank()
{
  _textBank.Free();
  //if( _textBank ) delete _textBank,_textBank=NULL;
}

void EngineDDT::D3DDrawTexts()
{
}

//#define SPEC_2D (NoZBuf|IsAlpha|ClampU|ClampV|IsAlphaFog)

inline bool EngineDDT::ClipDraw2D
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

void EngineDDT::Draw2D
(
  const Draw2DParsExt &pars, const Rect2DAbs &rect, const Rect2DAbs &clip
)
{
  Assert( pars.mip.IsOK() );
  if( !pars.mip.IsOK() ) return;

  if (ResetNeeded()) return;

  // cannot render zero area
  if (rect.w<=0 || rect.h<=0) return;
  
  Assert( fabs(pars.vTR+pars.vBL-pars.vTL-pars.vBR)<1e-6);
  Assert( fabs(pars.uTR+pars.uBL-pars.uTL-pars.uBR)<1e-6);

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
  ForgetDeviceBuffers();

  SwitchRenderMode(RM2DTris);
  SwitchTL(TLDisabled);

  AddVertices(pos,4); // note: may flush all queues

  QueuePrepareTriangle(pars.mip,NULL,pars.spec);
  
  Queue2DPoly(pos,4);
}

void EngineDDT::Draw2D
(
  const Draw2DPars &pars, const Rect2DAbs &rect, const Rect2DAbs &clip
)
{
  // TODO: special case optimization
  Draw2D(Draw2DParsExt(pars),rect,clip);
}
void EngineDDT::Draw2D
(
  const Draw2DParsNoTex &pars, const Rect2DAbs &rect, const Rect2DAbs &clip
)
{
  // TODO: special case optimization
  Draw2D(Draw2DParsExt(pars),rect,clip);
}

void EngineDDT::DrawLinePrepare()
{
  // use line texture
  Texture *tex = GPreloadedTextures.New(TextureLine);
  const MipInfo &mip = _textBank->UseMipmap(tex,1,1);
  int specFlags = NoZBuf|IsAlpha|ClampU|ClampV|IsAlphaFog;

  DrawPolyPrepare(mip,specFlags);
}

void EngineDDT::DrawLineDo
(
  const Line2DAbs &line,
  PackedColor c0, PackedColor c1,
  const Rect2DAbs &clip
)
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

  DrawPolyDo(vertices,4,clip);
}

void EngineDDT::DrawPolyPrepare(const MipInfo &mip, int specFlags)
{
  if (ResetNeeded()) return;

  SwitchRenderMode(RM2DTris);
  SwitchTL(TLDisabled);
  //FlushAllQueues(_queueNo);
  //AddVertices(gv,n); // note: may flush all queues
  QueuePrepareTriangle(mip,NULL,specFlags);
}

void EngineDDT::DrawPolyDo
(
  const Vertex2DPixel *vertices, int n, const Rect2DPixel &clipRect
)
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
  ForgetDeviceBuffers();

  AddVertices(gv,n); // note: may flush all queues - but it must not free them - can be dangerous?
  Queue2DPoly(gv,n);
}

void EngineDDT::DrawPolyDo
(
  const Vertex2DAbs *vertices, int n, const Rect2DAbs &clipRect
)
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
  ForgetDeviceBuffers();

  AddVertices(gv,n); // note: may flush all queues, but it cannot free them
  Queue2DPoly(gv,n);
}

void EngineDDT::DrawPoly3D(Matrix4Par space, const MipInfo &mip, const Vertex2DFloat *vertices, int nVertices, const Rect2DFloat &clip, int specFlags, int matType, const Pars3D *pars3D)
{
  if (ResetNeeded()) return;
  
  const LightList noLights;
  const LightList *lights = &noLights;
  if (pars3D)
  {
    lights = pars3D->_lights;
    DrawParameters dp;
    dp._cameraSpacePos = pars3D->_cameraSpacePos;
    BeginInstanceTL(space, -1.0f, specFlags, *lights, dp);
  }
  else
  {
    BeginInstanceTL(space, -1.0f, specFlags, *lights, DrawParameters::_default);
  }
  ChangeClipPlanes();
  SetBias(0);

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
  ForgetDeviceBuffers();

  // Lock the vertex buffer
  SVertexT *vertex;
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
      Vector3 pos(vs.x, 0.0f, vs.y);
      vertex->pos = pos;
      CopyNormal(vertex->norm, Vector3(0, 1, 0));
      UVPair uv; uv.u = vs.u; uv.v = vs.v;
      CopyUV(vertex->t0, uv, uv1[0], uv0[0], uv1[0]);
      vertex++;
    }
    _dynVBuffer.Unlock();

    // Set texgen scale and offset
    SetTexGenScaleAndOffset(uv1, uv0);

    // Set the vertex buffer
    const ComRef<IDirect3DVertexBuffer9Old> &dbuf = _dynVBuffer.GetVBuffer();
    SetStreamSource(dbuf, sizeof(SVertexT), _dynVBuffer._vBufferSize);
    _vOffsetLast = vOffset;

    // Prepare triangle
    TLMaterial mat;
    CreateMaterial(mat, vertices[0].color, matType);
    EngineShapeProperties prop;
    PrepareTriangleTL(
      NULL, mip, TexMaterialLODInfo(NULL, 0), specFlags, mat, specFlags&DisableSun, prop
    );

    // Draw
    _vertexDecl = _vertexDeclD[VD_Position_Normal_Tex0][VSDV_Basic];
    UVSource uv = UVTex;
    SetupSectionTL(
      _mainLight, FM_None, VSBasic, &uv, 1, true, STNone, false, 0, _pointLightsCount, _spotLightsCount,
      false, false, RMCommon, false, false
    );
    SetVertexDeclarationR(_vertexDecl);
    SetVertexShaderR(_vertexShader);
    SetupVSConstants(0, 0);
    SetupPSConstants();
    DoSelectPixelShader();
    DrawPrimitive(D3DPT_TRIANGLEFAN_OLD, _vOffsetLast, nVertices - 2);
  }
}

void EngineDDT::DrawLine( int beg, int end, int orLine )
{
  if (ResetNeeded()) return;
  
  const TLVertex &v0 = _mesh->GetVertex(beg);
  const TLVertex &v1 = _mesh->GetVertex(end);

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

void EngineDDT::CreateVB()
{
  ReportGRAM("Before CreateVertexBuffer");
  for (int i = 0; i < NumDynamicVB2D; i++)
  {
    HRESULT err;

    // Create NonTL vertex buffer
    {
      D3D10_BUFFER_DESC bd;
      bd.Usage = D3D10_USAGE_DYNAMIC;
      bd.ByteWidth = MeshBufferLength * sizeof(TLVertex);
      bd.BindFlags = D3D10_BIND_VERTEX_BUFFER;
      bd.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;
      bd.MiscFlags = 0;
      err = _d3DDevice->CreateBuffer(&bd, NULL, _queueNo._meshBuffer[i].Init());
      if (err!=D3D_OK || !_queueNo._meshBuffer)
      {
        DDError9("Error: Cannot create vertex buffer", err);
        return;
      }
    }

    // Create NonTL index buffer
    {
      D3D10_BUFFER_DESC bd;
      bd.Usage = D3D10_USAGE_DYNAMIC;
      bd.ByteWidth = IndexBufferLength * sizeof(VertexIndex);
      bd.BindFlags = D3D10_BIND_INDEX_BUFFER;
      bd.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;
      bd.MiscFlags = 0;
      err = _d3DDevice->CreateBuffer(&bd, NULL, _queueNo._indexBuffer[i].Init());
      if (err!=D3D_OK || !_queueNo._indexBuffer)
      {
        ReportMemoryStatus(1);
        DDError9("Error: Cannot create index buffer", err);
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

void EngineDDT::DestroyVB()
{
  for (int i=0; i<NumDynamicVB2D; i++)
  {
    _queueNo._meshBuffer[i].Free();
    _queueNo._indexBuffer[i].Free();
  }
  //_d3DDevice->SetStreamSource(0,NULL,0);
}

void EngineDDT::CreateVBTL()
{
  _dynVBuffer.Init(MaxShapeVertices,_d3DDevice);
  //CreatePostProcessStuff();
} 

void EngineDDT::DestroyVBTL()
{
  //DestroyPostProcessStuff();
  _dynVBuffer.Dealloc();
  _statVBuffer.Clear();
  _statIBuffer.Clear();
}

void EngineDDT::D3DConstruct()
{
  _d3dFrameOpen=false;

  static StaticStorage<WORD> TriangleQueueStorageNo[MaxTriQueues];
  for (int i=0; i<MaxTriQueues; i++)
  {
    TriQueueT &triqNo = _queueNo._tri[i];
    triqNo._triangleQueue.SetStorage(TriangleQueueStorageNo[i].Init(TriQueueSize));
  }

  CreateVB();
  CreateVBTL();

  //CreateTextBank();
}

void EngineDDT::D3DDestruct()
{
  //_fonts.Clear();
  //DestroyTextBank();

  //if( _textBank ) delete _textBank,_textBank=NULL;
  //_textBank.Free();

  _vertexShader.Free();
  _vertexShaderLast.Free();

  DeinitPixelShaders();
  DeinitVertexShaders();
  DestroyVBTL();
  DestroyVB();
}

void EngineDDT::Restore()
{
  _renderState.Clear();
  int i;
  for( i=0; i<MaxStages; i++ )
  {
    _textureStageState[i].Clear();
  }
}

bool EngineDDT::CanRestore()
{
  return false;
  //return _frontBuffer->IsLost()==D3D_OK;
}

  // implementation of MemoryFreeOnDemandHelper
float EngineDDT::Priority() const
{
  return 0.5;
}

size_t EngineDDT::MemoryControlledRecent() const
{
  return _vBufferLRU.MemoryControlledRecent().GetSysSize();
}

size_t EngineDDT::MemoryControlled() const
{
  return _vBufferLRU.MemoryControlled().GetSysSize();
}

void EngineDDT::ReleaseAllVertexBuffers()
{
  for (VertexBufferD3DTLRUItem *last = _vBufferLRU.First(); last; )
  {
    // free vertex buffer that was not used for longest time
    Shape *shape = unconst_cast(last->GetOwner());
    
    // this one may be destroyed in ReleaseVBuffer - process to a next one
    last = _vBufferLRU.Next(last);
    // release v-buffer
    shape->ReleaseVBuffer();
  }
}

size_t EngineDDT::FreeOneItem(int minAge)
{
  for (VertexBufferD3DTLRUItem *last = _vBufferLRU.First(); last; last = _vBufferLRU.Next(last))
  {
    int age = FreeOnDemandFrameID()-last->GetFrameNum();
    if (age<minAge) break;
    // free vertex buffer that was not used for longest time
    Shape *shape = unconst_cast(last->GetOwner());
    // if the buffer is used, we cannot discard it
    // buffer may be used and still not be the recent one
    // this is because it is made "recent" quite late, in the BeginMeshTL
    // we may still be before this call
    VertexBuffer *vb = shape->GetVertexBuffer();
    if (vb && vb->IsLocked()) continue;
    // if buffer is not owned, it should not be listed here
    size_t estSize = last->GetStoredMemoryControlled().GetVRAMSize();
    // release v-buffer, but shape knows it should create it again if needed
    shape->ReleaseVBuffer();
    return estSize;
  }
  return 0;
}

size_t EngineDDT::FreeOneItem()
{
  return FreeOneItem(0);
}

size_t EngineDDT::Free(size_t size, int minAge)
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

size_t EngineDDT::ReleaseVideoMemory(size_t size, int minAge)
{
  return Free(size,minAge);
}

/**
@param limit max. VRAM+AGP allocation allowed for vertex buffers (textures already deduced)
*/
size_t EngineDDT::ThrottleVRAMAllocation(size_t limit, int minAge)
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

size_t EngineDDT::UsedVRAM() const
{
  return _vBuffersAllocated;
}

float EngineDDT::VRAMDiscardMetrics() const
{
  VertexBufferD3DTLRUItem *toDiscard = _vBufferLRU.First();
  if (!toDiscard) return 0;
  return FreeOnDemandFrameID()-toDiscard->GetFrameNum();
}

int EngineDDT::VRAMDiscardCountAge(int minAge, int *totalSize) const
{
  int count = 0;
  int size = 0;
  
  for (VertexBufferD3DTLRUItem *last = _vBufferLRU.First(); last; last = _vBufferLRU.Next(last))
  {
    int age = FreeOnDemandFrameID()-last->GetFrameNum();
    if (age<minAge) break;
    
    count++;
    size += last->GetStoredMemoryControlled().GetVRAMSize();
  }
  if (totalSize) *totalSize = size;
  return count;
}


void EngineDDT::MemoryControlledFrame()
{
  _vBufferLRU.Frame();
}

#pragma comment(lib,"d3d10.lib")

#if _DEBUG
#pragma comment(lib,"d3dx10d.lib")
#else
#pragma comment(lib,"d3dx10.lib")
#endif

#if _ENABLE_CHEATS

#include <Es/Memory/normalNew.hpp>
#include <d3dx9tex.h>
#include <Es/Memory/debugNew.hpp>

void EngineDDT::Screenshot(RString filename)
{
//   HRESULT result = D3DXSaveSurfaceToFile(filename, D3DXIFF_BMP, _backBuffer, NULL, NULL);
//   if (result != D3D_OK) DDError9("Screenshot failed", result);
}

#else

void EngineDDT::Screenshot(RString filename)
{
}

#endif

#if _ENABLE_CHEATS

static RString FormatByteSizeNoZero(size_t size)
{
  if (size) return FormatByteSize(size);
  return RString();
}

RString EngineDDT::GetStat(int statId, RString &statVal, RString &statVal2)
{
  int id = 0;
  if (statId==id++)
  {
    statVal = FormatByteSizeNoZero(_iBuffersAllocated+_vBuffersAllocated);
    statVal2 = RString();
    return "Buffers";
  }
  else if (statId==id++)
  {
    statVal = FormatByteSizeNoZero(_vBufferLRU.MemoryControlled().GetVRAMSize());
    statVal2 = FormatByteSizeNoZero(_vBufferLRU.MemoryControlledRecent().GetVRAMSize());
    return "VBuffers";
  }
  else if (statId==id++)
  {
    statVal = FormatByteSizeNoZero(_vBufferLRU.MemoryControlled().GetSysSize());
    statVal2 = FormatByteSizeNoZero(_vBufferLRU.MemoryControlledRecent().GetSysSize());
    return "VB (Sys)";
  }
  else if (statId==id++)
  {
    statVal = FormatByteSizeNoZero(_rtAllocated);
    statVal2 = RString();
    return "RT";
  }
  else if (statId==id++)
  {
    statVal = FormatByteSizeNoZero(_rtAllocated+_textBank->GetVRamTexAllocation()+UsedVRAM());
    statVal2 = RString();
    return "Total";
  }
  else if (statId==id++)
  {
    statVal = Format("%d",_freeEstDirty);
    statVal2 = Format("%.1f",(::GlobalTickCount()-_freeEstCheckTime)*0.001f);
    return "Est recalc";
  }
  return RString();
}
#endif

#include "../engineDll.hpp"

Engine *CreateEngineD3DT( CreateEnginePars &pars )
{
  // Disable desktop cursor if running fullscreen
  HideCursor = true;
  if (!pars.UseWindow) EnableDesktopCursor(false);

  // Create the engine object
  EngineDDT *newEngine = new EngineDDT;
  if (newEngine == NULL)
  {
    return NULL;
  }

  // Initialize engine
  if (pars.UseWindow)
  {
    if (!newEngine->Init(pars.hInst,pars.hPrev,pars.sw,
      pars.winX, pars.winY, pars.winW, pars.winH, true, pars.bpp, pars.GenerateShaders))
    {
      newEngine->Done();
      delete newEngine;
      return NULL;
    }
  }
  else
  {
    if (!newEngine->Init(pars.hInst, pars.hPrev, pars.sw,
      0, 0, pars.w, pars.h, false, pars.bpp, pars.GenerateShaders))
    {
      newEngine->Done();
      delete newEngine;
      return NULL;
    }
  }

  // Return created and initialized engine
  return newEngine;
}

//////////////////////////////////////////////////////////////////////////

HWND AppInit
(
 HINSTANCE hInst,HINSTANCE hPrev,int sw, bool fullscreen,
 int x, int y, int width, int height
 );

BOOL AppDone(HWND hwndApp, HINSTANCE hInstance);


// constructor - attach  a engine to given backBuffer

void EngineDDT::InitSurfaces(ParamEntryPar cfg, ParamEntryPar fcfg)
{
  //LoadConfig();
  Init3D();
  DoSetGamma();

  // direct-color mode required
  D3DConstruct();

  CreateTextBank(cfg, fcfg);

}

void EngineDDT::DoneSurfaces(bool destroyTexBank)
{
  _fonts.Clear();
  if (destroyTexBank) DestroyTextBank();

  D3DDestruct();
  Done3D();
}

void EngineDDT::DoSetGamma()
{
  // Direct3D 10 NEEDS UPDATE 
//   D3DGAMMARAMP ramp;
//   // prepare gamma ramp
//   int table[256];
// 
//   table[0]=0;
//   float eGamma=1/_gamma;
//   for( int i=1; i<256; i++ )
//   {
//     float x=i*(1/255.0);
//     float fx=pow(x,eGamma);
//     int ifx=toInt(fx*65535.0);
//     saturate(ifx,0,65535);
//     table[i]=ifx;
//   }
// 
//   for( int i=0; i<256; i++ )
//   {
//     ramp.red[i]=ramp.green[i]=ramp.blue[i]=table[i];
//   }
//   _d3DDevice->SetGammaRamp(0/*FLY*/, 0,&ramp);
//   LogF("set gamma %.3f",_gamma);
}

void EngineDDT::SetGamma( float gamma )
{
  saturate(gamma,1e-3,1e3);
  _gamma=gamma;
  if (_d3DDevice)
  {
    DoSetGamma();
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

float EngineDDT::GetPreHDRBrightness() const
{
  // convert brightness from log to linear scale
  float expBrightness = InterpretUsrBrightness(_usrBrightness);
  float postHDR = GetPostHDRBrightness();
  return expBrightness/postHDR;
}
float EngineDDT::GetPostHDRBrightness() const
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

bool EngineDDT::SetTextureQualityWanted(int value)
{
  if (value==_textureQuality) return false;
  _textureQuality = value;
  if (_textBank)
  {
    _textBank->SetTextureQuality(value);
  }
  return true;
}

void EngineDDT::SetTextureQuality(int value)
{
  if (SetTextureQualityWanted(value) && _d3DDevice)
  {
    ApplyWanted();
  }
}

int EngineDDT::GetMaxTextureQuality() const
{
  return _textBank ? _textBank->GetMaxTextureQuality() : 2;
}


float EngineDDT::GetHDRFactor() const
{
  #if _ENABLE_CHEATS
    static float hdrFactor = 0.5;
  #else
    static float hdrFactor = 0.5;
  #endif
  return hdrFactor;
}

void ParseFlashpointCfg(ParamFile &file);
void SaveFlashpointCfg(ParamFile &file);

void EngineDDT::LoadConfig(ParamEntryPar cfg,ParamEntryPar fcfg)
{
  // first of all attempt auto-detection
  //SetDefaultSettingsLevel(GetDeviceLevel());
  
  bool changed = false;
  
  if (!_windowed)
  {
    int w = LoadCfgDef(fcfg,"Resolution_W",_w);
    int h = LoadCfgDef(fcfg,"Resolution_H",_h);
    int bpp = LoadCfgDef(fcfg,"Resolution_Bpp",_pixelSize);
    _refreshRate = LoadCfgDef(fcfg,"refresh",_refreshRate);
    
    if (SetResolutionWanted(w,h,bpp)) changed = true;
  }


  if (!_initialized)  
  {
    // we want to load this only when not initialized yet
    // otherwise it would overwrite the settings we have already made
    // load unique ID to check if video adapter has changed
    // if it did, we may want to reset some default settings?
    
    // we certainly want to recheck amount of VRAM used
    _lastDeviceId = LoadCfgDef(fcfg,"lastDeviceId",RString());
    _localVramAmount = LoadCfgDef(fcfg,"localVRAM",0);
    _nonlocalVramAmount = LoadCfgDef(fcfg,"nonlocalVRAM",0);
  }

  {
    // user config
    SetGamma(LoadCfgDef(cfg,"gamma",GetDefaultGamma()));
    _userEnabledWBuffer = LoadCfgDef(cfg, "useWBuffer", false);
  }
  int fsAAWanted = LoadCfgDef(fcfg,"FSAA",_fsAAWanted);
  int afWanted = cfg.ReadValue("anisoFilter",_afWanted);
  int postFxQuality = cfg.ReadValue("postFX",_postFxQuality);
  int hdrPrecWanted = LoadCfgDef(fcfg,"HDRPrecision",8);
  int texQuality = cfg.ReadValue("TexQuality",GetTextureQuality());
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
  changed |= SetPostprocessEffectsWanted(postFxQuality);
  
  if (changed && _initialized)
  {
    ApplyWanted();
  }

  base::LoadConfig(cfg,fcfg);
}
void EngineDDT::SaveConfig(ParamFile &cfg)
{
  if (!IsOutOfMemory())
  {
    {
      // global config
      ParamFile fcfg;
      ParseFlashpointCfg(fcfg);

      if (!_windowed)
      {
        fcfg.Add("Resolution_W",_w);
        fcfg.Add("Resolution_H",_h);
        fcfg.Add("Resolution_Bpp",_pixelSize);
        fcfg.Add("refresh",_refreshRate);
      }
      fcfg.Add("FSAA",_fsAAWanted);
      int prec = 8;
      switch (_hdrPrecWanted)
      {
        case HDR16: prec = 16; break;
        case HDR32: prec = 32; break;
      }
      fcfg.Add("HDRPrecision",prec);

      fcfg.Add("lastDeviceId",_lastDeviceId);
  
      fcfg.Add("localVRAM",_localVramAmount);
      fcfg.Add("nonlocalVRAM",_nonlocalVramAmount);

      SaveFlashpointCfg(fcfg);
    }
    
    cfg.Add("anisoFilter",_afWanted);
    cfg.Add("TexQuality", _textureQuality);
    cfg.Add("postFX",_postFxQuality);
    
    {
      // user config
      cfg.Add("gamma",_gamma);
    }
    base::SaveConfig(cfg);
  }
}

void EngineDDT::Clear(bool clearZ, bool clear, PackedColor color)
{
  PROFILE_DX_SCOPE(3dCLR);

  // Clear render target if required
  if (clear)
  {
    Color fColor = color;
    _d3DDevice->ClearRenderTargetView(_renderTarget, (float*)&fColor);
  }

  // Clear depth-stencil if required
  if (clearZ)
  {
    _d3DDevice->ClearDepthStencilView(_depthStencil, D3D10_CLEAR_DEPTH | D3D10_CLEAR_STENCIL, 1.0f, 0);
  }
}

void EngineDDT::SetViewport(int width, int height)
{
  D3D10_VIEWPORT viewData;
  ZeroMemory(&viewData, sizeof(viewData));
  viewData.TopLeftX = 0;
  viewData.TopLeftY = 0;
  viewData.MinDepth = 0.0f;
  viewData.MaxDepth = 1.0f;
  viewData.Width = width;
  viewData.Height = height;
  _d3DDevice->RSSetViewports(1, &viewData);
}

void EngineDDT::WorkToBack()
{
}

//#include "txtD3D.hpp"
//#include <El/Common/perfProf.hpp>

void EngineDDT::BackToFront()
{
  if (_resetStatus!=D3D_OK) return;

  if (!_cursorSet)
  {
    ShowDeviceCursor(false);
  }

  // Present the scene
  PROFILE_DX_SCOPE(3dSwp);
  _d3DSwapChain->Present(0, 0);
}


int EngineDDT::FrameTime() const
{
  return GlobalTickCount()-_frameTime;
}


void EngineDDT::ReportGRAM(const char *name)
{
  // Direct3D 10 NEEDS UPDATE 
//   ID3D10Device *device = GetDirect3DDevice();
//   if (name) LogF("VRAM report: %s",name);
//   int dwFree = device->GetAvailableTextureMem();
//   if (name) LogF("  VRAM free %d",dwFree);
}


void EngineDDT::CreateSurfaces(bool windowed, ParamEntryPar cfg, ParamEntryPar fcfg)
{
  _windowed = windowed;

  InitSurfaces(cfg, fcfg);

  // clear all backbuffers
  Clear(false);
  BackToFront();
  Clear(false);
  BackToFront();
  Clear(false);
  BackToFront();

  ReportGRAM("After creating Frame-Buffer");
}

void EngineDDT::DestroySurfaces(bool destroyTexBank)
{
  ForgetDeviceBuffers();

  DoneSurfaces(destroyTexBank);

  //D3DDestruct();
  //Done3D();


  //_backBuffer.Free();
  //FLY_frontBuffer.Free();
  //_d3DDevice.Free();

}

void EngineDDT::Pause()
{
}

EngineDDT::EngineDDT()
{
  // Set Initialized flag
  _initialized = false;
  _sbQuality = 0;
  _dbQuality = 0;
  _cursorX = 0;
  _cursorY = 0;
  _cursorSet = false;
  _cursorShown = false;
  // unless proven otherwise, assume worst
  _deviceLevel = 0;
  _vbuffersStatic = false;
}

EngineDDT::~EngineDDT()
{
  Done();
}

PushBuffer *EngineDDT::CreateNewPushBuffer(int key)
{
  return new PushBufferD3DT(key);
}

#define CREATEINPUTLAYOUT(vertexDecl, vertexShader, inputLayout) \
  if (FAILED(_d3DDevice->CreateInputLayout(vertexDecl, sizeof(vertexDecl)/sizeof(vertexDecl[0]), \
  vertexShader._shaderBytecode->GetBufferPointer(), vertexShader._shaderBytecode->GetBufferSize(), inputLayout.Init()))) \
  { \
    LogF("Error in CreateVertexDeclaration"); \
    return false; \
  }

bool EngineDDT::CreateVertexDeclarations()
{
  // Direct3D 10 NEEDS UPDATE 

  #if _ENABLE_REPORT
    DWORD start = ::GetTickCount();
  #endif
  
  Debugger::PauseCheckingScope scope(GDebugger);

//   // Create vertex declaration objects
//   ComRef<ID3D10InputLayout> vdNonTL;
//   CREATEINPUTLAYOUT(vertexDeclNonTL, _vertexShaderNonTL, vdNonTL);

//   ComRef<ID3D10InputLayout> vdBasic;
//   CREATEINPUTLAYOUT(vertexDeclBasic, _vertexShaderPool[VSPBasic], vdBasic);
//   ComRef<ID3D10InputLayout> vdNormal;
//   if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclNormal, vdNormal.Init()))) {
//     LogF("Error in CreateVertexDeclaration");
//     return false;
//   }
//   ComRef<ID3D10InputLayout> vdBasicUV2;
//   if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclBasicUV2, vdBasicUV2.Init()))) {
//     LogF("Error in CreateVertexDeclaration");
//     return false;
//   }
//   ComRef<ID3D10InputLayout> vdNormalUV2;
//   if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclNormalUV2, vdNormalUV2.Init()))) {
//     LogF("Error in CreateVertexDeclaration");
//     return false;
//   }
//   ComRef<ID3D10InputLayout> vdNormalCustom;
//   if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclNormalCustom, vdNormalCustom.Init()))) {
//     LogF("Error in CreateVertexDeclaration");
//     return false;
//   }
//   ComRef<ID3D10InputLayout> vdShadowVolume;
//   if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclShadowVolume, vdShadowVolume.Init()))) {
//     LogF("Error in CreateVertexDeclaration");
//     return false;
//   }
//   ComRef<ID3D10InputLayout> vdSkinning;
//   if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclSkinning, vdSkinning.Init()))) {
//     LogF("Error in CreateVertexDeclaration");
//     return false;
//   }
//   ComRef<ID3D10InputLayout> vdNormalSkinning;
//   if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclNormalSkinning, vdNormalSkinning.Init()))) {
//     LogF("Error in CreateVertexDeclaration");
//     return false;
//   }
//   ComRef<ID3D10InputLayout> vdUV2Skinning;
//   if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclUV2Skinning, vdUV2Skinning.Init()))) {
//     LogF("Error in CreateVertexDeclaration");
//     return false;
//   }
//   ComRef<ID3D10InputLayout> vdNormalUV2Skinning;
//   if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclNormalUV2Skinning, vdNormalUV2Skinning.Init()))) {
//     LogF("Error in CreateVertexDeclaration");
//     return false;
//   }
//   ComRef<ID3D10InputLayout> vdShadowVolumeSkinning;
//   if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclShadowVolumeSkinning, vdShadowVolumeSkinning.Init()))) {
//     LogF("Error in CreateVertexDeclaration");
//     return false;
//   }
//   ComRef<ID3D10InputLayout> vdInstancing;
//   if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclInstancing, vdInstancing.Init()))) {
//     LogF("Error in CreateVertexDeclaration");
//     return false;
//   }
//   ComRef<ID3D10InputLayout> vdNormalInstancing;
//   if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclNormalInstancing, vdNormalInstancing.Init()))) {
//     LogF("Error in CreateVertexDeclaration");
//     return false;
//   }
//   ComRef<ID3D10InputLayout> vdUV2Instancing;
//   if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclUV2Instancing, vdUV2Instancing.Init()))) {
//     LogF("Error in CreateVertexDeclaration");
//     return false;
//   }
//   ComRef<ID3D10InputLayout> vdNormalUV2Instancing;
//   if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclNormalUV2Instancing, vdNormalUV2Instancing.Init()))) {
//     LogF("Error in CreateVertexDeclaration");
//     return false;
//   }
//   ComRef<ID3D10InputLayout> vdShadowVolumeInstancing;
//   if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclShadowVolumeInstancing, vdShadowVolumeInstancing.Init()))) {
//     LogF("Error in CreateVertexDeclaration");
//     return false;
//   }
//   ComRef<ID3D10InputLayout> vdSpriteInstancing;
//   if (FAILED(_d3DDevice->CreateVertexDeclaration(vertexDeclSpriteInstancing, vdSpriteInstancing.Init()))) {
//     LogF("Error in CreateVertexDeclaration");
//     return false;
//   }

  //////////////////////////////////////////////////////////////////////////

  // We are going to succeed - save the created objects
  {
//     // Non-TL declaration
//     _vertexDeclNonTL = vdNonTL;

//     // Basic
//     _vertexDeclD[VD_Tex0][VSDV_Basic]                                             = NULL;
//     _vertexDeclD[VD_Position_Tex0_Color][VSDV_Basic]                              = NULL;
//     _vertexDeclD[VD_Position_Normal_Tex0][VSDV_Basic]                             = vdBasic;
//     _vertexDeclD[VD_Position_Normal_Tex0_ST][VSDV_Basic]                          = vdNormal;
//     _vertexDeclD[VD_Position_Normal_Tex0_Tex1][VSDV_Basic]                        = vdBasicUV2;
//     _vertexDeclD[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Basic]                     = vdNormalUV2;
//     _vertexDeclD[VD_Position_Normal_Tex0_ST_Float3][VSDV_Basic]                   = vdNormalCustom;
//     _vertexDeclD[VD_Position_Normal_Tex0_WeightsAndIndices][VSDV_Basic]           = NULL;
//     _vertexDeclD[VD_Position_Normal_Tex0_ST_WeightsAndIndices][VSDV_Basic]        = NULL;
//     _vertexDeclD[VD_Position_Normal_Tex0_Tex1_WeightsAndIndices][VSDV_Basic]      = NULL;
//     _vertexDeclD[VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices][VSDV_Basic]   = NULL;
//     _vertexDeclD[VD_ShadowVolume][VSDV_Basic]                                     = vdShadowVolume;
//     _vertexDeclD[VD_ShadowVolumeSkinned][VSDV_Basic]                              = NULL;
// 
//     // Skinning
//     _vertexDeclD[VD_Tex0][VSDV_Skinning]                                            = NULL;
//     _vertexDeclD[VD_Position_Tex0_Color][VSDV_Skinning]                             = NULL;
//     _vertexDeclD[VD_Position_Normal_Tex0][VSDV_Skinning]                            = vdSkinning;
//     _vertexDeclD[VD_Position_Normal_Tex0_ST][VSDV_Skinning]                         = vdNormalSkinning;
//     _vertexDeclD[VD_Position_Normal_Tex0_Tex1][VSDV_Skinning]                       = vdUV2Skinning;
//     _vertexDeclD[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Skinning]                    = vdNormalUV2Skinning;
//     _vertexDeclD[VD_Position_Normal_Tex0_ST_Float3][VSDV_Skinning]                  = NULL;
//     _vertexDeclD[VD_Position_Normal_Tex0_WeightsAndIndices][VSDV_Skinning]          = NULL;
//     _vertexDeclD[VD_Position_Normal_Tex0_ST_WeightsAndIndices][VSDV_Skinning]       = NULL;
//     _vertexDeclD[VD_Position_Normal_Tex0_Tex1_WeightsAndIndices][VSDV_Skinning]     = NULL;
//     _vertexDeclD[VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices][VSDV_Skinning]  = NULL;
//     _vertexDeclD[VD_ShadowVolume][VSDV_Skinning]                                    = vdShadowVolumeSkinning;
//     _vertexDeclD[VD_ShadowVolumeSkinned][VSDV_Skinning]                             = NULL;
// 
//     // Instancing
//     _vertexDeclD[VD_Tex0][VSDV_Instancing]                                            = vdSpriteInstancing;
//     _vertexDeclD[VD_Position_Tex0_Color][VSDV_Instancing]                             = NULL;
//     _vertexDeclD[VD_Position_Normal_Tex0][VSDV_Instancing]                            = vdInstancing;
//     _vertexDeclD[VD_Position_Normal_Tex0_ST][VSDV_Instancing]                         = vdNormalInstancing;
//     _vertexDeclD[VD_Position_Normal_Tex0_Tex1][VSDV_Instancing]                       = vdUV2Instancing;
//     _vertexDeclD[VD_Position_Normal_Tex0_Tex1_ST][VSDV_Instancing]                    = vdNormalUV2Instancing;
//     _vertexDeclD[VD_Position_Normal_Tex0_ST_Float3][VSDV_Instancing]                  = NULL;
//     _vertexDeclD[VD_Position_Normal_Tex0_WeightsAndIndices][VSDV_Instancing]          = NULL;
//     _vertexDeclD[VD_Position_Normal_Tex0_ST_WeightsAndIndices][VSDV_Instancing]       = NULL;
//     _vertexDeclD[VD_Position_Normal_Tex0_Tex1_WeightsAndIndices][VSDV_Instancing]     = NULL;
//     _vertexDeclD[VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices][VSDV_Instancing]  = NULL;
//     _vertexDeclD[VD_ShadowVolume][VSDV_Instancing]                                    = vdShadowVolumeInstancing;
//     _vertexDeclD[VD_ShadowVolumeSkinned][VSDV_Instancing]                             = NULL;
  }

  // with 20 we have at least 256 registers
  // this could accommodate 50 or even more bones
  int maxFreeConstants = 168; // see also maxFreeConstants in FPShaders.hlsl

  _maxBones = maxFreeConstants/3;
  _maxInstancesShadowVolume = _maxBones;
  _maxInstances = maxFreeConstants/4;
  _maxInstancesSprite = maxFreeConstants/3;
  saturateMin(_maxBones,LimitBones);
  saturateMin(_maxInstances,LimitInstances);
  saturateMin(_maxInstancesShadowVolume,LimitInstancesShadowVolume);
  saturateMin(_maxInstancesSprite,LimitInstancesSprite);

  _maxNightInstances = _maxInstances;
  _maxNightInstancesSprite = _maxInstancesSprite;
  // Initialize the maximums of instances in the night
  // no longer needed - light space is kept separate
  /*
  {
    _maxNightInstances = (maxFreeConstants - MAX_LIGHTS*6)/4;
    _maxNightInstancesSprite = (maxFreeConstants - MAX_LIGHTS*6)/3;
    saturateMin(_maxNightInstances,LimitInstances);
    saturateMin(_maxNightInstancesSprite,LimitInstancesSprite);
  }
  */

  // Set TL to defined state
  DoSwitchTL(TLDisabled);

#if _ENABLE_REPORT
  DWORD time = ::GetTickCount()-start;
  LogF("CreateVertexDeclarations: %d ms",time);
#endif

  return true;
}

void EngineDDT::DestroyVertexDeclarations()
{
  for (int i = 0; i < VDCount; i++)
  {
    for (int j = 0; j < VSDVCount; j++)
    {
      _vertexDeclD[i][j].Free();
    }
  }
  _vertexDeclNonTL.Free();
}

bool EngineDDT::CreatePostProcessStuff()
{
  return true;
  // Direct3D 10 NEEDS UPDATE 
//   #if _ENABLE_REPORT
//     DWORD start = ::GetTickCount();
//   #endif
//   Log("CreatePostProcessStuff");
//   Debugger::PauseCheckingScope scope(GDebugger);
//   // Get fragments from specified file
//   QOStrStreamData psSource(PSFile);
//   QOStrStreamData vsSource(FPShaders);
//   ComRef<ID3DXBuffer> shader;
//   ComRef<ID3DXBuffer> errorMsgs;
//   // Create post processes
//   Ref<PostProcess> ppFinal = new PPFinal;
//   if (!ppFinal->Init(psSource,vsSource))
//   {
//     LogF("Error: Post process effect creation failed.");
//     return false;
//   }
//   Ref<PPGaussianBlur> ppGaussianBlur = new PPGaussianBlur;
//   if (!ppGaussianBlur->Init(psSource,vsSource))
//   {
//     LogF("Error: Post process effect creation failed.");
//     return false;
//   }
//   Ref<PPDOF> ppDOF = new PPDOF;
//   if (!ppDOF->Init(psSource, vsSource, ppGaussianBlur))
//   {
//     LogF("Error: Post process effect creation failed.");
//     return false;
//   }
//   Ref<PPDistanceDOF> ppDistanceDOF = new PPDistanceDOF;
//   if (!ppDistanceDOF->Init(psSource, vsSource, ppGaussianBlur))
//   {
//     LogF("Error: Post process effect creation failed.");
//     return false;
//   }
//   Ref<PPGlow> ppGlow = new PPGlow;
//   if (!ppGlow->Init(psSource,vsSource, ppGaussianBlur))
//   {
//     LogF("Error: Post process effect creation failed.");
//     return false;
//   }
//   Ref<PostProcess> ppStencilShadowsPri = new PPStencilShadowsPri;
//   if (!ppStencilShadowsPri->Init(psSource,vsSource))
//   {
//     LogF("Error: Post process effect creation failed.");
//     return false;
//   }
//   Ref<PostProcess> ppStencilShadowsSec = new PPStencilShadowsSec;
//   if (!ppStencilShadowsSec->Init(psSource,vsSource))
//   {
//     LogF("Error: Post process effect creation failed.");
//     return false;
//   }
//   Ref<PPFlareIntensity> ppFlareIntensity = new PPFlareIntensity;
//   if (!ppFlareIntensity->Init(psSource,vsSource))
//   {
//     LogF("Error: Post process effect creation failed.");
//     return false;
//   }
// 
//   // We are going to succeed - save the created objects
//   _ppFinal = ppFinal;
//   _ppGaussianBlur = ppGaussianBlur;
//   _ppDOF = ppDOF;
//   _ppDistanceDOF = ppDistanceDOF;
//   _ppGlow = ppGlow;
//   _ppStencilShadowsPri = ppStencilShadowsPri;
//   _ppStencilShadowsSec = ppStencilShadowsSec;
//   _ppFlareIntensity = ppFlareIntensity;
// 
//   #if _ENABLE_REPORT
//     DWORD time = ::GetTickCount()-start;
//     LogF("CreatePostProcessStuff: %d ms",time);
//   #endif
//   
//   ForgetAvgIntensity();
//   
//   return true;
}

void EngineDDT::DestroyPostProcessStuff()
{
  Log("DestroyPostProcessStuff");
  _ppFlareIntensity.Free();
  _ppStencilShadowsSec.Free();
  _ppStencilShadowsPri.Free();
  _ppGlow.Free();
  _ppDistanceDOF.Free();
  _ppDOF.Free();
  _ppGaussianBlur.Free();
  _ppFinal.Free();
}

bool ParseUserParams(ParamFile &cfg);
void SaveUserParams(ParamFile &cfg);

/*!
\patch 5145 Date 3/23/2007 by Ondra
- Fixed: When running windowed, on some systems the image was slightly blurred.
\patch 5153 Date 4/4/2007 by Jirka
- Fixed: Game running in full screen had invisible but clickable window areas (close, minimize)
*/

bool EngineDDT::Init(HINSTANCE hInst, HINSTANCE hPrev,
                     int sw, int x, int y, int width, int height,
                     bool windowed, int bpp, bool generateShaders)
{

  // Instance handle
  _hInst = hInst;

  // Pushbuffer section
  _pbIsBeingCreated = false;
  _pushBuffer = NULL;

  // Set 
  _texLoc = TexLocalVidMem;
  _flippable = false;
  _pixelSize = bpp;
  _refreshRate = 0;
  _fsAAWanted = 0;
  _afWanted = 0;
  _maxFSAA = 15;
  _maxAF = 4;
  _postFxQuality = 0;
  _hdrPrecWanted = HDR8;
  _bias = 0;
  _gamma = 1.0f;
  _mesh = NULL;
  _textBank = NULL;
  _userEnabledWBuffer = true;
  _maxLights = MAX_LIGHTS;
  _maxTextureSize = 4096;
  _hasStencilBuffer = false;
  _iBuffersAllocated = 0;
  _vBuffersAllocated = 0;
  _rtAllocated = 0;
  _resetStatus = D3D_OK;
  _resetDone = false;
  _lastKnownAvgIntensity = -1.0f;
  _accomFactorPrev = -1;

  // Set initial value for pixel shader
  _pixelShaderSpecularSel = PSSUninitialized;
  _pixelShaderSel = PSUninitialized;
  _pixelShaderTypeSel = PSTUninitialized;

  _texture0MaxColorSeparate = true;
  _texture0MaxColor = Color(-1,-1,-1,-1);
  _texture0AvgColor = Color(-1,-1,-1,-1);

  RegisterFreeOnDemandSystemMemory(this);

  //_formatSet=SingleTex;
  _renderMode = RMTris;
  _tlActive = TLDisabled;
  _windowed = windowed;
  _visTestCount = 0;

  // DOF parameters
  _focusDist = 0.0f;
  _blurCoef = 0.0f;
  _farOnly = true;

  // Low-end parameter
  _lowEndQuality = false;

  // Set the device type
  if (generateShaders)
  {
    _devType = D3D10_DRIVER_TYPE_REFERENCE;
  }
  else
  {
    _devType = D3D10_DRIVER_TYPE_HARDWARE;
  }

  InitAdapter();

  // make sure values are autodetected once engine is created
  SetDefaultSettingsLevel(GetDeviceLevel());
  
  // Config loading
  ParamFile cfg;
  ParseUserParams(cfg);
  ParamFile fcfg;
  ParseFlashpointCfg(fcfg);
  LoadConfig(cfg,fcfg);

  EngineDDT::SetRenderStateTemp::SetEngine(this);
  EngineDDT::SetSamplerStateTemp::SetEngine(this);
  EngineDDT::SetVertexShaderConstantTemp::SetEngine(this);
  EngineDDT::SetPixelShaderConstantTemp::SetEngine(this);
  EngineDDT::SetTextureTemp::SetEngine(this);
  EngineDDT::PostProcess::SetEngine(this);
  EngineDDT::PPGaussianBlur::SetEngine(this);
  EngineDDT::PPDOF::SetEngine(this);
  EngineDDT::PPDistanceDOF::SetEngine(this);
  EngineDDT::PPGlow::SetEngine(this);
  EngineDDT::PPFlareIntensity::SetEngine(this);

  // Pushbuffer static initialization
  PBItemT::SetEngine(this);

  // raster parameters
  if (windowed)
  {
    // search for a location that is handled by selected adapter
    //int x=0,y=0;
    SearchMonitor(x, y, width, height);

    // Create the application window
    _hwndApp = AppInit(hInst, hPrev, sw, false, x, y, width, height);
    if (!_hwndApp)
    {
      Log("Failed to create a window");
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
    // Create the application window
    _hwndApp = AppInit(hInst, hPrev, sw, true, 0, 0, 160, 160);
    if (!_hwndApp)
    {
      Log("Failed to create a window");
      return false;
    }

    _w = width;
    _h = height;
    CreateSurfaces(windowed, cfg, fcfg);
    //_textBank=new TextBank(_engine);

    Fail("DX10 TODO");
    // Direct3D 10 NEEDS UPDATE 
//     if (D3DAdapter >= 0)
//     {
//       HMONITOR mon = _direct3D->GetAdapterMonitor(D3DAdapter);
// 
//       MONITORINFO info;
//       memset(&info, 0, sizeof(info));
//       info.cbSize = sizeof(info);
// 
//       if (GetMonitorInfo(mon, &info))
//       {
//         _monitorOffsetX = info.rcMonitor.left;
//         _monitorOffsetY = info.rcMonitor.top;
//       }
//     }
  }

  // If shaders should be generated then generate all variations and exit
  if (generateShaders)
  {
    // Generate shaders
    for (int i = 0; i < SBTCount; i++)
    {
      ShaderCompileCacheT cacheInit(Array<const char *>(Sources, lenof(Sources)), SBTFileName[i], RStringB(HLSLCacheDir), true);
      InitVertexShaders();
      InitPixelShaders(SBTechnique(i));
      CreatePostProcessStuff();
      DestroyPostProcessStuff();
      DeinitPixelShaders();
      DeinitVertexShaders();
    }

    // Destroy application and exit
    DestroySurfaces();
    AppDone(_hwndApp, hInst);
    return false;
  }

#if _ENABLE_COMPILED_SHADER_CACHE
  ShaderCompileCacheT cacheInit(Array<const char *>(Sources, lenof(Sources)), SBTFileName[_sbTechnique], RStringB(HLSLCacheDir));
#endif

  // Load vertex shaders
  InitVertexShaders();
  // Load pixel shaders
  InitPixelShaders(_sbTechnique);


  if (!CreateVertexDeclarations())
  {
    Fail("Error while creating vertex declarations");
    DestroySurfaces();
    AppDone(_hwndApp, hInst);
    return false;
  }

  if (!CreatePostProcessStuff())
  {
    LogF("Error while creating stencil shadows stuff");
    DestroyVertexDeclarations();
    DestroySurfaces();
    AppDone(_hwndApp, hInst);
    return false;
  }

  // Create VS constant buffer
  {
    D3D10_BUFFER_DESC bd;
    bd.ByteWidth = sizeof(_shaderConstValues);
    bd.Usage = D3D10_USAGE_DYNAMIC;
    bd.BindFlags = D3D10_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags   = D3D10_CPU_ACCESS_WRITE;
    bd.MiscFlags        = 0;
    HRESULT hr = _d3DDevice->CreateBuffer(&bd, NULL, _vsConstants.Init());
    DoAssert(SUCCEEDED(hr));
  }

  // Create PS constant buffer
  {
    D3D10_BUFFER_DESC bd;
    bd.ByteWidth = sizeof(_pixelShaderConstValues);
    bd.Usage = D3D10_USAGE_DYNAMIC;
    bd.BindFlags = D3D10_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags   = D3D10_CPU_ACCESS_WRITE;
    bd.MiscFlags        = 0;
    HRESULT hr = _d3DDevice->CreateBuffer(&bd, NULL, _psConstants.Init());
    DoAssert(SUCCEEDED(hr));
  }

  // Back culling
  _backCullDisabled = false;
  _currBackCullDisabled = false;

  // Crater rendering
  _currCraterRendering = false;

  // The main light
  _mainLight = ML_Sun;
  _currMainLight = ML_Sun;

  // The fog mode
  _fogMode = FM_Fog;
  _currFogMode = FM_Fog;

  // Set initial value for vertex shader
  _vertexShaderID = VSBasic;
  _currVertexShaderID = VSBasic;

  // Set initial value for UV sources
  _uvSourceCount = 0;
  _currUVSourceCount = 0;
  _uvSourceOnlyTex0 = true;
  _currUVSourceOnlyTex0 = true;

  // Set initial values for texGen related variables
  _pointLightsCount = -1;
  _spotLightsCount = -1;
  _currPointLightsCount = -1;
  _currSpotLightsCount = -1;

  // Set initial value for shadow receiver flag
  _currShadowReceiver = false;

  // Skinning parameters
  _skinningType = STNone;
  _currSkinningType = STNone;
  _bones.Clear();
  _bonesWereSetDirectly = false;

  // Instancing parameters
  _currInstancing = false;
  _currInstancesCount = -1;

  _alphaShadowEnabled = true;
  _currAlphaShadowEnabled = true;

  // Shadow buffer parameters
  _renderingMode = RMCommon;
  _currRenderingMode = RMCommon;
  _oldLRSP = MIdentity;

  // Various render states
  _currStencilRef = 0;
  _currSampleMask = 0;

  // Single instance parameters
  SetInstanceInfo(HWhite, 1.0f, false);

  // Set Initialized flag
  _initialized = true;

  _localPrediction = false;

  // Direct3D 10 NEEDS UPDATE 
//   if (_ddraw)
//   {
//     _localPrediction = true;
//     // measure allocation requirements for following object types
//     // dynamic vertex buffer
//     StatisticsById vbDynCounts;
//     // static vertex buffer
//     StatisticsById vbStatCounts;
//     // static index buffer
//     StatisticsById ibStatCounts;
//     // texture
//     StatisticsById texCounts;
//     // fall-back solution, in case small texture texture allocation learned nothing
//     StatisticsById bigTexCounts;
// 
//     // as  we are not sure we are using the device in exclusive mode,
//     // we will perform several experiments and use the most common result
// 
//     for (int i=0; i<10; i++)
//     {
//       {
//         int localBeg,nonlocalBeg;
//         CheckFreeTextureMemory(localBeg,nonlocalBeg);
//         ComRef<ID3D10Texture2D> tex;
//         if (_d3DDevice->CreateTexture(4, 4, 1, 0, D3DFMT_A1R5G5B5, D3DPOOL_DEFAULT, tex.Init(), NULL) == D3D_OK)
//         {
//           int localEnd,nonlocalEnd;
//           CheckFreeTextureMemory(localEnd,nonlocalEnd);
//           texCounts.Count(StatisticsById::Id(localBeg-localEnd,nonlocalBeg-nonlocalEnd));
//         }
//       }
//       {
//         int localBeg,nonlocalBeg;
//         CheckFreeTextureMemory(localBeg,nonlocalBeg);
//         ComRef<ID3D10Texture2D> tex;
//         if (_d3DDevice->CreateTexture(256, 256, 0, 0, D3DFMT_A1R5G5B5, D3DPOOL_DEFAULT, tex.Init(), NULL) == D3D_OK)
//         {
//           int localEnd,nonlocalEnd;
//           CheckFreeTextureMemory(localEnd,nonlocalEnd);
//           bigTexCounts.Count(StatisticsById::Id(localBeg-localEnd,nonlocalBeg-nonlocalEnd));
//         }
//       }
//       {
//         int localBeg,nonlocalBeg;
//         CheckFreeTextureMemory(localBeg,nonlocalBeg);
//         ComRef<IDirect3DIndexBuffer9Old> buf;
//         if (_d3DDevice->CreateIndexBuffer(64*1024, WRITEONLYIB,D3DFMT_INDEX16,D3DPOOL_DEFAULT, buf.Init(), NULL) == D3D_OK)
//         {
//           /*
//           // attempt Lock / Unlock to make sure the memory is really allocated - did not work on GeForce 6800 anyway
//           void *data = NULL;
//           buf->Lock(0,32,&data,0);
//           buf->Unlock();
//           */
// 
//           int localEnd,nonlocalEnd;
//           CheckFreeTextureMemory(localEnd,nonlocalEnd);
//           ibStatCounts.Count(StatisticsById::Id(localBeg-localEnd,nonlocalBeg-nonlocalEnd));
//         }
//       }
//       {
//         int localBeg,nonlocalBeg;
//         CheckFreeTextureMemory(localBeg,nonlocalBeg);
//         ComRef<IDirect3DVertexBuffer9Old> buf;
//         if (_d3DDevice->CreateVertexBuffer(32, WRITEONLYVB,0,D3DPOOL_DEFAULT, buf.Init(), NULL) == D3D_OK)
//         {
//           int localEnd,nonlocalEnd;
//           CheckFreeTextureMemory(localEnd,nonlocalEnd);
//           vbStatCounts.Count(StatisticsById::Id(localBeg-localEnd,nonlocalBeg-nonlocalEnd));
//         }
//       }
//       {
//         int localBeg,nonlocalBeg;
//         CheckFreeTextureMemory(localBeg,nonlocalBeg);
//         ComRef<IDirect3DVertexBuffer9Old> buf;
//         if (_d3DDevice->CreateVertexBuffer(64*1024, WRITEONLYVB|D3DUSAGE_DYNAMIC,0,D3DPOOL_DEFAULT, buf.Init(), NULL) == D3D_OK)
//         {
//           /*
//           // attempt Lock / Unlock to make sure the memory is really allocated - did not work on ATI X800 anyway
//           void *data = NULL;
//           buf->Lock(0,32,&data,0);
//           buf->Unlock();
//           */
//           int localEnd,nonlocalEnd;
//           CheckFreeTextureMemory(localEnd,nonlocalEnd);
//           vbDynCounts.Count(StatisticsById::Id(localBeg-localEnd,nonlocalBeg-nonlocalEnd));
//         }
//       }
// 
//     }
//     
//     // select most common value
//     vbDynCounts.SortData(false);
//     vbStatCounts.SortData(false);
//     ibStatCounts.SortData(false);
//     texCounts.SortData(false);
//     if (texCounts.Size()>0)
//     {
//       const StatisticsById::Item &most = texCounts.Get(0);
//       // we expect a significant majority of some value
//       // if not, something is wrong and we cannot expect to be able to predict anything
//       if (most.count>=7)
//       {
//         int local = most.id._id1, nonlocal = most.id._id2;
//         (void)local,(void)nonlocal;
//         // we assume only one of them should be used
//         if (local>nonlocal)
//         {
//           // the value we see should be the alignment
//           _texAlignment = local;
//         }
//         else if (nonlocal>local)
//         {
//           // textures not in local memory - do not try to predict anything
//           _localPrediction = false;
//         }
//         else
//         {
//           const StatisticsById::Item &most = bigTexCounts.Get(0);
//           // we expect a significant majority of some value
//           // if not, something is wrong and we cannot expect to be able to predict anything
//           if (most.count>=7)
//           {
//             // when using a big texture, we do not know the alignment, but we should know local/nonlocal
//             int local = most.id._id1, nonlocal = most.id._id2;
//             (void)local,(void)nonlocal;
//             if (local>0)
//             {
//               LogF("Local alignment not known");
//             }
//             else if (nonlocal>0)
//             {
//               _localPrediction = false;
//             }
//             else if (local==0)
//             {
//               LogF("Local/non-local detection failed");
//             }
//           }
//         }
//       }
//     }
//     
//     if (vbStatCounts.Size()>0)
//     {
//       const StatisticsById::Item &most = vbStatCounts.Get(0);
//       // we expect a significant majority of some value
//       // if not, something is wrong and we cannot expect to be able to predict anything
//       if (most.count>=7)
//       {
//         int local = most.id._id1, nonlocal = most.id._id2;
//         (void)local,(void)nonlocal;
//         // we assume only one of them should be used
//         if (local>0 && nonlocal==0)
//         {
//           // the value we see should be the alignment
//           _vbAlignment = local;
//           _staticVBNonLocal = false;
//         }
//         else
//         {
//           if (local==0)
//           {
//             LogF("VB static location now known, using default");
//           }
// 
//           // static buffers not in local memory - we do not care much about alignment
//           _staticVBNonLocal = true;
//         }
//       }
//     }
//     if (vbDynCounts.Size()>0)
//     {
//       const StatisticsById::Item &most = vbDynCounts.Get(0);
//       // we expect a significant majority of some value
//       // if not, something is wrong and we cannot expect to be able to predict anything
//       if (most.count>=7)
//       {
//         int local = most.id._id1, nonlocal = most.id._id2;
//         (void)local,(void)nonlocal;
//         // we assume only one of them should be used
//         // if buffers not in local memory - we do not care much about alignment, or where they really are
//         _dynamicVBNonLocal = local == 0;
//         if (local==0 && nonlocal==0)
//         {
//           LogF("VB dynamic location now known, using default");
//         }
//       }
//     }
//     if (ibStatCounts.Size()>0)
//     {
//       const StatisticsById::Item &most = ibStatCounts.Get(0);
//       // we expect a significant majority of some value
//       // if not, something is wrong and we cannot expect to be able to predict anything
//       if (most.count>=7)
//       {
//         int local = most.id._id1, nonlocal = most.id._id2;
//         (void)local,(void)nonlocal;
//         // if buffers not in local memory - we do not care much about alignment, or where they really are
//         _ibNonLocal = local==0 && nonlocal>0;
//         if (local==0 && nonlocal==0)
//         {
//           LogF("IB static location now known, using default");
//         }
//       }
//     }
//     LogF("IB: %s",_ibNonLocal ? "nonlocal" : "local");
//     LogF("VB static: %d B, %s",_vbAlignment,_staticVBNonLocal ? "nonlocal" : "local");
//     LogF("VB dynamic: %s",_dynamicVBNonLocal ? "nonlocal" : "local");
//     LogF("Tex: %d B, %s",_texAlignment,_localPrediction ? "local" : "nonlocal");
//   }
  
  // Success
  return true;
}

void EngineDDT::Init()
{
  base::Init();
  _textBank->ResetMaxTextureQuality();
  _textBank->InitDetailTextures();
}

void EngineDDT::Done()
{

  // Done only if initialization succeeded
  if (_initialized)
  {

    // Destroy shader fragments allocations
    DestroyVertexDeclarations();

    // Destroy surfaces
    DestroySurfaces();

    // Deinit application (unregister wnd class f.i.)
    AppDone(_hwndApp, _hInst);

    // Test whether there remains some memory allocated for index and vertex buffers
    Assert(_iBuffersAllocated==0);
    Assert(_vBuffersAllocated==0);
    //Assert(_rtAllocated==0);
    _rtAllocated = 0;

    // Save config
    ParamFile cfg;
    if (ParseUserParams(cfg))
    {
      SaveConfig(cfg);
      SaveUserParams(cfg);
    }

    // Set the flag
    _initialized = false;
  }
}

void EngineDDT::DisplayWaitScreen()
{
  if (!ResetNeeded())
  {
    // we should be outside the frame rendering now
    
    if (!_d3dFrameOpen)
    {
      // perform full frame rendering
      InitDraw(true);
      // TODO: render some text message here
      FinishDraw(1,true);
      NextFrame();
    }
    
  }
}

void EngineDDT::PreReset(bool hard)
{
  // if reset is not really need, we might display some nice wait screen here
  // this may happen with used initiated Reset (resolution switch...)
  
  DisplayWaitScreen();
  
  FreeAllQueues(_queueNo);

  ForgetDeviceBuffers();

  for (int i = MaxSamplerStages - 1; i >= 0; i--) SetTexture(i, NULL);
  SetTexture0Color(HWhite,HWhite,true);

  // destroy all video memory surfaces
  LogF(
    "Releasing vertex buffers: %d, %d, %d",
    _vBuffersAllocated+_iBuffersAllocated,MemoryControlled(),MemoryControlledRecent()
  );
  _textBank->DoneDetailTextures();
  _textBank->ReleaseAllTextures(hard); // release all textures
  // release all vertex buffers
  ReleaseAllVertexBuffers();

  Fail("DX10 TODO");
  // _backBuffer.Free(); // Direct3D 10 NEEDS UPDATE 
  // _depthBuffer.Free(); // Direct3D 10 NEEDS UPDATE 
  // _depthBufferRT.Free(); // Direct3D 10 NEEDS UPDATE 
  // _renderTargetD.Free(); // Direct3D 10 NEEDS UPDATE 
  // _renderTargetSurfaceD.Free(); // Direct3D 10 NEEDS UPDATE 
  // _renderTargetS.Free(); // Direct3D 10 NEEDS UPDATE 
  // _renderTargetSurfaceS.Free(); // Direct3D 10 NEEDS UPDATE 
  // _currRenderTargetDepthBufferSurface.Free(); // Direct3D 10 NEEDS UPDATE 
  // _currRenderTargetSurface.Free(); // Direct3D 10 NEEDS UPDATE 
  // _currRenderTarget.Free(); // Direct3D 10 NEEDS UPDATE 
  // _cursorSurface.Free(); // Direct3D 10 NEEDS UPDATE 
  DestroySBResources();
  //_frontBuffer.Free();
  //_zBuffer.Free();

  // Release depth buffer resources
  DestroyDBResources();

  DeinitPixelShaders();
  DeinitVertexShaders();

  DestroyPostProcessStuff();
  DestroyVertexDeclarations();
  
  DestroyVB();
  DestroyVBTL();

  Assert(_iBuffersAllocated==0);
  Assert(_vBuffersAllocated==0);
  //Assert(_rtAllocated==0);
  _rtAllocated = 0;
}

void EngineDDT::PostReset()
{
  CreateVB();
  CreateVBTL();

#if _ENABLE_COMPILED_SHADER_CACHE
  ShaderCompileCacheT cacheInit(Array<const char *>(Sources, lenof(Sources)), SBTFileName[_sbTechnique], RStringB(HLSLCacheDir));
#endif
  bool fail = false;

  // reset all render/texture states to undefined
  for (int tmu=0; tmu<MaxStages; tmu++)
  {
    _textureStageState[tmu].Clear();
  }
  _renderState.Clear();
  Init3DState();
  InitVertexShaders();
  InitPixelShaders(_sbTechnique);
  if (_sbQuality>0)
  {
    CreateSBResources();
  }

  _dbQuality = _postFxQuality>0 ? 1 : 0;
  // Create depth buffer resources
  if (_dbQuality > 0)
  {
    CreateDBResources();
  }

  if (!CreateVertexDeclarations()) fail = true;
  if (!CreatePostProcessStuff()) fail = true;

  if (fail)
  {
    ErrorMessage("Unable to restore vertex/pixel shaders.");
  }
  
  _textBank->InitDetailTextures();

  // TODO: reset fog
  // frames took very long, but we need to ignore it
  ResetTimingHistory(50);
  _resetDone = true;
  _cursorSet = false;
  _cursorShown = false;

#if _ENABLE_CHEATS
  ReportMemoryStatus(1);
#endif
}

bool EngineDDT::Reset()
{
  Fail("DX10 TODO");
  return false;
  // Direct3D 10 NEEDS UPDATE 
//   Debugger::PauseCheckingScope scope(GDebugger);
//   
//   bool inScene = _d3dFrameOpen;
//   if (inScene) FinishDraw(1);
// 
//   PreReset(false);
// 
//   for(;;)
//   {
//     HRESULT ok = _d3DDevice->Reset(&_pp);
//     if (ok==D3D_OK)
//     {
//       break;
//     }
//     if (ok==D3DERR_DEVICENOTRESET || ok==D3DERR_DEVICELOST)
//     {
//       LogF("Reset failed ... waiting to retry.");
//       Sleep(1000);
//     }
//     else
//     {
//       LogF("*** Reset failed (%x)",ok);
//       // we started resetting - we should finish it
//       _resetStatus = D3DERR_DEVICENOTRESET;
//       return false;
//     }
//   }
// 
//   LogF("*** Reset done");
//   // Recreate all surfaces
//   PostReset();
//   if (inScene) InitDraw();
//   return true;
}

bool EngineDDT::ResetHard()
{
  Debugger::PauseCheckingScope scope(GDebugger);
  
  bool inScene = _d3dFrameOpen;
  if (inScene) FinishDraw(1);
  PreReset(true);

  // destroy surfaces and device
  DestroyPostProcessStuff();
  DestroyVertexDeclarations();
  DestroySurfaces(false);

  // Surfaces creation
  {
    // Config loading
    ParamFile cfg;
    ParseUserParams(cfg);
    ParamFile fcfg;
    ParseFlashpointCfg(fcfg);
    // do not load config - only pass it to the CreateSurfaces, which will pass it to texture ban
    //LoadConfig(cfg,fcfg);

    // Surfaces creation
    CreateSurfaces(_windowed, cfg, fcfg);
  }

  LogF("*** Reset done (hard)");
  
  PostReset();
  if (inScene) InitDraw();
  return true;
}

bool EngineDDT::SetResolutionWanted(int w, int h, int bpp)
{
  if (w==_w && h==_h && _pixelSize==bpp) return false;
  _w = w;
  _h = h;
  _pixelSize = bpp;
  return true;
}

bool EngineDDT::SwitchRes(int w, int h, int bpp)
{
  Fail("DX10 TODO");
  return false;
  // Direct3D 10 NEEDS UPDATE 
//   if (!SetResolutionWanted(w,h,bpp)) return true;
//   D3DDEVICE_CREATION_PARAMETERS pars;
//   _d3DDevice->GetCreationParameters(&pars);
//   int adapter = pars.AdapterOrdinal;
//   FindMode(adapter);
//   // some old drivers were buggy - ResetHard was used here as a workaround for fullscreen chain
//   Reset();
// 
//   return true;
}

bool EngineDDT::SwitchWindowed(bool windowed)
{
  Fail("DX10 TODO");
  return false;
  // Direct3D 10 NEEDS UPDATE 
//   
//   if (windowed == _windowed) return true;
//   
//   _windowed = windowed;
//   _pp.Windowed = windowed;
//   D3DDEVICE_CREATION_PARAMETERS pars;
//   _d3DDevice->GetCreationParameters(&pars);
//   int adapter = pars.AdapterOrdinal;
//   FindMode(adapter);
// 
//   if (windowed)
//   {
//     // resize the window as needed
//     RECT client;
//     // adjust width, height to match client area
//     GetClientRect(_hwndApp,&client);
//     _w = client.right-client.left;
//     _h = client.bottom-client.top;
//   }
//   
//   Reset();
//   
//   extern bool UseWindow;
//   UseWindow = windowed;
// 
//   return true;
}

bool EngineDDT::SwitchRefreshRate(int refresh)
{
  Fail("DX10 TODO");
  return false;
  // Direct3D 10 NEEDS UPDATE 
//   if (_pp.Windowed) return false;
//   if (refresh==0) return false;
//   if (_refreshRate==refresh) return true;
//   _refreshRate = refresh;
//   D3DDEVICE_CREATION_PARAMETERS pars;
//   _d3DDevice->GetCreationParameters(&pars);
//   int adapter = pars.AdapterOrdinal;
//   FindMode(adapter);
//   Reset();
//   return true;
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

void EngineDDT::ListResolutions(FindArray<ResolutionInfo> &ret)
{
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   ret.Clear();
//   #define ENABLE_WINDOWED_SWITCH 0
//   
//   #if !ENABLE_WINDOWED_SWITCH
//   if (_pp.Windowed)
//   {
//     // No resolution change possible
//     ResolutionInfo info;
//     info.w = info.h = 0;
//     info.bpp = 0;
//     ret.AddUnique(info);
//     return;
//   }
//   #endif
// 
//   // Enumerate resolutions available on current device
//   D3DDEVICE_CREATION_PARAMETERS pars;
//   _d3DDevice->GetCreationParameters(&pars);
//   int adapter = pars.AdapterOrdinal;
// 
//   // Go through supported formats
//   for (int formatIndex = 0; formatIndex < lenof(supportedFormats); formatIndex++) {
// 
//     // Go through available modes
//     for (int i = 0; i < (int)_direct3D->GetAdapterModeCount(adapter, supportedFormats[formatIndex]._format); i++)
//     {
//       D3DDISPLAYMODE mode;
//       _direct3D->EnumAdapterModes(adapter, supportedFormats[formatIndex]._format, i, &mode);
// 
//       // Mode contains resolution and refresh rate
//       ResolutionInfo info;
//       info.w = mode.Width;
//       info.h = mode.Height;
//       info.bpp = FormatToBpp(mode.Format);
//       if (info.bpp) ret.AddUnique(info);
//     }
//   }
//   
//   // if there are too many resolutions, drop some of them
//   
//   Optimize(ret);
//   
//   
//   #if ENABLE_WINDOWED_SWITCH
//   ResolutionInfo info;
//   info.w = info.h = 0;
//   info.bpp = 0;
//   ret.AddUnique(info);
//   #endif
}

void EngineDDT::ListRefreshRates(FindArray<int> &ret)
{
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   ret.Clear();
//   if (_pp.Windowed)
//   {
//     ret.Add(0); // Return default
//     // No refresh change possible
//     return;
//   }
//   // List refresh rates for current w,h,bpp
//   D3DDEVICE_CREATION_PARAMETERS pars;
//   _d3DDevice->GetCreationParameters(&pars);
//   int adapter = pars.AdapterOrdinal;
// 
//   // Go through supported formats
//   for (int formatIndex = 0; formatIndex < lenof(supportedFormats); formatIndex++) {
// 
//     // Go through available modes
//     for (int i = 0; i<(int)_direct3D->GetAdapterModeCount(adapter, supportedFormats[formatIndex]._format); i++)
//     {
//       D3DDISPLAYMODE mode;
//       _direct3D->EnumAdapterModes(adapter, supportedFormats[formatIndex]._format, i, &mode);
// 
//       // Mode contains resolution and refresh rate
//       if
//         (
//         mode.Width==Width() && mode.Height==Height() &&
//         FormatToBpp(mode.Format)==PixelSize()
//         )
//       {
//         ret.AddUnique(mode.RefreshRate);
//       }
//     }
//   }
}

void EngineDDT::SetLimitedViewport(float viewSize)
{
  // Limit viewport and use it
  _viewW = toInt(_w*viewSize);
  _viewH = toInt(_h*viewSize);
  SetViewport(_viewW, _viewH);
}

QueryIdT::QueryIdT()
{
  // Direct3D 10 NEEDS UPDATE
// 
//   for (int i=0; i<MaxQueries; i++) _used[i] = false;
//   _curUsed = 0;
}

int QueryIdT::Open()
{
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE
// 
//   // Create the query objects
//   for (int i=0; i<MaxQueries-1; i++)
//   {
//     if (_curUsed>=MaxQueries) _curUsed=0;
//     if (!_used[_curUsed])
//     {
//       if (!_query[_curUsed])
//       {
//         HRESULT hr = GEngineDD->GetDirect3DDevice()->CreateQuery(D3DQUERYTYPE_OCCLUSION, _query[_curUsed].Init());
//         if (!SUCCEEDED(hr)) return -1;
//       }
//       _used[_curUsed] = true;
//       int id = _curUsed++;
//       return id;
//     }
//     _curUsed++;
//   }
  // no valid query found
  return -1;
}
void QueryIdT::Close(int id)
{
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE
// 
//   _used[id] = false;
}


#include <Es/Memory/normalNew.hpp>
/// Xbox implementation of GPU Visibility Query
class D3DVisibilityQueryT: public IVisibilityQuery
{
  friend class EngineDDT;

  enum QueryStatus
  {
    /// query was not started
    QueryNone,
    /// query was already started
    QueryStarted,
    /// we already have the result
    QueryDone
  };
  enum {NResults=4};
  DWORD _index[NResults];
  int _result[NResults];
  QueryStatus _status[NResults];
  int _toStart;

  /// check how many frames it takes between query and result
  int Latency() const;
public:
  D3DVisibilityQueryT();
  ~D3DVisibilityQueryT();
  virtual bool IsVisible() const;
  int VisiblePixels() const;

  void CheckResults(EngineDDT *engine);

  USE_FAST_ALLOCATOR
};
#include <Es/Memory/debugNew.hpp>

DEFINE_FAST_ALLOCATOR(D3DVisibilityQueryT)

D3DVisibilityQueryT::D3DVisibilityQueryT()
{
  _toStart = 0;
  for (int i=0; i<NResults; i++)
  {
    _index[i] = 0;
    _status[i] = QueryNone;
    _result[i] = true;
  }
}

D3DVisibilityQueryT::~D3DVisibilityQueryT()
{
  for (int i=0; i<NResults; i++)
  {
    if (_status[i] == QueryStarted)
    {
      // terminate the query
      //LogF("Pending query destroyed");
      GEngineDD->_ids.Close(_index[i]);
    }
  }
}

bool D3DVisibilityQueryT::IsVisible() const
{
  unconst_cast(this)->CheckResults(GEngineDD);
#if _ENABLE_REPORT
  static bool AlwaysTrue = false;
  if (AlwaysTrue) return true;
#endif
#if 0
  // check how old the query result is
  const int maxAge = 2;
  int latency = Latency();
  if (latency>maxAge)
  {
    // if query result is too old, ignore it
    return true;
  }
#endif
  for (int i=0; i<NResults; i++)
  {
    if (_result[i]>0) return true;
  }
  return false;
}

int D3DVisibilityQueryT::VisiblePixels() const
{
  unconst_cast(this)->CheckResults(GEngineDD);
  // return most recent result we have
  int recent = _toStart;
  for (int i=0; i<NResults; i++)
  {
    if (--recent<0) recent = NResults-1;
    if (_status[recent]==QueryDone)
    {
      return _result[recent];
    }
  }
  // we do not know - query did not finish
  return -1;
}

int D3DVisibilityQueryT::Latency() const
{
  int latency = 0;
  int recent = _toStart;
  for (int i=0; i<NResults; i++)
  {
    if (--recent<0) recent = NResults-1;
    if (_status[recent]!=QueryStarted)
    {
      break;
    }
    latency++;
  }
  return latency;
}

void D3DVisibilityQueryT::CheckResults(EngineDDT *engine)
{
  for (int i=0; i<NResults; i++)
  {
    if (_status[i]==QueryStarted)
    {
      // check the result
      UINT pixels = 0;
      int id = _index[i];
      HRESULT ret = -1;
      //HRESULT ret = engine->GetDirect3DDevice()->GetVisibilityTestResult(id,&pixels,NULL);
      if (ret==D3D_OK)
      {
        _result[i] = pixels;
        engine->_ids.Close(id);
        _status[i] = QueryDone;
        //LogF("Close (OK ) %d for %x:%d - %d",id,q,i,pixels);
      }
      else if (ret!=-1)
      {
        _result[i] = INT_MAX; // we do not know how many pixels completed - assume all
        engine->_ids.Close(id);
        _status[i] = QueryDone;
        //LogF("Close (Err) %d for %x:%d",id,q,i);
      }
    }
  }
}

IVisibilityQuery *EngineDDT::QueryVisibility(
  IVisibilityQuery *query, ClipFlags clipFlags, const Vector3 *minmax, const FrameBase &pos
)
{
  // TODO: implement
  return NULL;
  #if 0
  // if object is very near of the camera, do not check its visibility
  const float minDistTest = 20;
  if (pos.Position().Distance2(_sceneProps->GetCameraPosition())<Square(minDistTest))
  {
    return NULL;
  }
  // check if camera is inside of the minmax box
  Matrix4 invTransform = pos.GetInvTransform();
  Vector3 modelSpaceCam = invTransform.FastTransform(_sceneProps->GetCameraPosition());
  if (
    modelSpaceCam.X()>=minmax[0].X() && modelSpaceCam.X()<=minmax[1].X() &&
    modelSpaceCam.Y()>=minmax[0].Y() && modelSpaceCam.Y()<=minmax[1].Y() &&
    modelSpaceCam.Z()>=minmax[0].Z() && modelSpaceCam.Z()<=minmax[1].Z()
    )
  {
    return NULL;
  }
  D3DVisibilityQueryT *q = static_cast<D3DVisibilityQueryT *>(query);
  if (!q) q = new D3DVisibilityQueryT;
  // check the result of all query which are currently running
  q->CheckResults(this);

//  static int maxLatency = 2;
//  if (q->Latency()<maxLatency)
  {
    // get a new query ID - skip all id's currently used
    int id = _ids.Open();
    if (id>=0)
    {
      SCOPE_GRF("visib",SCOPE_COLOR_GRAY);


      // make sure visibility testing was set up
      Assert(_visTestCount>0);
      BeginVisibilityTesting();

      /*
      SetupModelSpaceRendering(pos);
      SetPosScaleAndOffset(minmax[1]-minmax[0], minmax[0]);

      _d3DDevice->BeginVisibilityTest();
      _d3DDevice->DrawPrimitive(D3DPT_TRIANGLESTRIP,0,12);
      _d3DDevice->EndVisibilityTest(id);
      */

      EndVisibilityTesting();

      //VisibilityQueryStarted(q,id);
    }
  }
//  else
//  {
//    GlobalShowMessage(100,"Query latency high");
//  }
  return q;
  #endif
}

void EngineDDT::BeginVisibilityTesting()
{
  if (_visTestCount++!=0) return;
  
  D3DSetRenderState(D3DRS_COLORWRITEENABLE_OLD, 0);
}
void EngineDDT::EndVisibilityTesting()
{
  if (--_visTestCount!=0) return;

  D3DSetRenderState(D3DRS_COLORWRITEENABLE_OLD, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
}

IVisibilityQuery *EngineDDT::BegPixelCounting(IVisibilityQuery *query)
{
  return NULL;
  /*
  D3DVisibilityQueryT *q = static_cast<D3DVisibilityQueryT *>(query);
  if (!q) q = new D3DVisibilityQueryT;
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
void EngineDDT::EndPixelCounting(IVisibilityQuery *query)
{
  D3DVisibilityQueryT *q = static_cast<D3DVisibilityQueryT *>(query);
  if (!q) return;

}


bool EngineDDT::IsCursorSupported() const
{
  return false;
// 
//   return _canHWMouse || _windowed;
//   //return false;
}



#define DEBUG_CURSOR 0

void EngineDDT::ShowDeviceCursor(bool showIt)
{
  // Direct3D 10 NEEDS UPDATE 
//   if (_cursorShown!=showIt)
//   {
//     _cursorShown = showIt;
//     // we use system cursor
//     //ShowCursor(showIt);
//   }
//   #if DEBUG_CURSOR
//     _d3DDevice->ShowCursor(true);
//   #else
//     _d3DDevice->ShowCursor(_cursorShown);
//   #endif
}

bool EngineDDT::IsCursorShown() const
{
  return true;
  // Direct3D 10 NEEDS UPDATE 
//   // if cursor is currently not shown, no need to clip
//   return _cursorShown;
}

/*!
\patch 5127 Date 2/7/2007 by Jirka
- Fixed: Mouse cursor color was not handled correctly
*/

void EngineDDT::SetCursor(Texture *cursor, int x, int y, float hsX, float hsY, PackedColor color)
{
  // Direct3D 10 NEEDS UPDATE 
//   if (cursor!=NULL) _cursorSet = true;
//   /*
//   if (x!=_cursorX || y!=_cursorY)
//   {
//     _d3DDevice->SetCursorPosition(x,y,D3DCURSOR_IMMEDIATE_UPDATE);
//     _cursorX = x;
//     _cursorY = y;
//   }
//   */
//   if (cursor==NULL)
//   {
//     ShowDeviceCursor(false);
// 
//     #if DEBUG_CURSOR
// 
//     int w = 32;
//     int h = 32;
//     _d3DDevice->CreateOffscreenPlainSurface(
//       w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,_cursorSurface.Init(),NULL
//     );
//     
//     if (_cursorSurface.NotNull())
//     {
//       // we have 4444 data now
//       // we need to convert them to 8888, but with discrete alpha
//       D3DLOCKED_RECT rect;
//       _cursorSurface->LockRect(&rect,NULL,0);
//       DWORD *tgt = (DWORD *)rect.pBits;
//       for (int i=0; i<w*h; i++)
//       {
//         *tgt = 0xffffffff;
//         tgt++;
//       }
//       _cursorSurface->UnlockRect();
//       _d3DDevice->SetCursorProperties(16,16,_cursorSurface);
//     }
//     _cursorTex = NULL;
//     #endif
// 
//   }
//   else
//   {
//     bool ok = true;
//     if (cursor!=_cursorTex || color != _cursorColor || _cursorSurface.IsNull())
//     {
//       // assume not OK unless proven otherwise
//       ok = false;
//       TextureD3DT *newCursorTex = static_cast<TextureD3DT *>(cursor);
//       newCursorTex->LoadHeaders();
//       _cursorTex = newCursorTex;
//       _cursorColor = color;
//       
//       if (newCursorTex->Width(0)!=32 || newCursorTex->Height(0)!=32)
//       {
//         LogF("Cursor texture %s not 32x32",cc_cast(newCursorTex->GetName()));
//       }
//       // we may need to transform the cursor from Alpha format to 32x32 ARGB8888 surface
//       // A needs to be 0 or 255
//       
//       // this is most easily done by reading into system memory
//       int level = 0;
//       // find closest to 32x32 mipmap
//       int areaWanted = 32*32;
//       int nMipmaps = newCursorTex->NMipmaps();
//       for (;level<nMipmaps; level++)
//       {
//         if (level>=newCursorTex->_smallLevel) break;
//         int area = newCursorTex->Width(level)*newCursorTex->Height(level);
//         if (area<=areaWanted) break;
//       }
//       
//       // get mipmap data into system memory
//       AutoArray<char, MemAllocStack<char,64*64*4> > mem;
//       
//       const PacLevelMem &mip = newCursorTex->_mipmaps[level];
//       int w = mip._w;
//       int h = mip._h;
//       
//       // adjust the data as needed
//       // most likely they are DXT5 now
//       PacLevelMem mipD = mip;
//       switch (mipD._dFormat)
//       {
//         case PacDXT3:
//         case PacDXT5:
//           mipD._dFormat = PacARGB4444;
//           break;
//         case PacARGB1555:
//         case PacARGB4444:
//           break;
//         default:
//           RptF("Unsupported cursor format for %s",cc_cast(newCursorTex->GetName()));
//           break;
//       }
//       int size = PacLevelMem::MipmapSize(mipD._dFormat,mipD._w,mipD._h);
//       mipD._pitch = size/h;
// 
//       mem.Realloc(size);
//       mem.Resize(size);
// 
//       // get data from the texture source
//       newCursorTex->_src->GetMipmapData(mem.Data(),mipD,level);
//       
//       _d3DDevice->CreateOffscreenPlainSurface(
//         w,h,D3DFMT_A8R8G8B8,D3DPOOL_SCRATCH,_cursorSurface.Init(),NULL
//       );
//       
//       if (_cursorSurface.NotNull())
//       {
//         float colorR = color.R8() / 255.0f;
//         float colorG = color.G8() / 255.0f;
//         float colorB = color.B8() / 255.0f;
//         // we have 4444 data now
//         // we need to convert them to 8888, but with discrete alpha
//         D3DLOCKED_RECT rect;
//         _cursorSurface->LockRect(&rect,NULL,0);
//         char *tgt = (char *)rect.pBits;
//         const WORD *src = (const WORD *)mem.Data();
//         if (mipD._dFormat==PacARGB1555)
//         {
//           for (int y=0; y<h; y++)
//           {
//             DWORD *tgtLine = (DWORD *)tgt;
//             for (int x=0; x<w; x++)
//             {
//               int a= ((*src)&0x8000)!=0 ? 255 : 0;
//               int r= toInt((((*src)>>10)&0x1f)*(255.0/31)*colorR);
//               int g= toInt((((*src)>>5)&0x1f)*(255.0/31)*colorG);
//               int b= toInt((((*src)>>0)&0x1f)*(255.0/31)*colorB);
//               *tgtLine = (a<<24)|(r<<16)|(g<<8)|b;
//               //*tgt = 0x01010101;
//               tgtLine++,src++;
//             }
//             tgt += rect.Pitch;
//           }
//         }
//         else
//         {
//           for (int y=0; y<h; y++)
//           {
//             DWORD *tgtLine = (DWORD *)tgt;
//             for (int x=0; x<w; x++)
//             {
//               int a= (((*src)>>12)&0xf)>2 ? 255 : 0;
//               int r= toInt((((*src)>>8)&0xf)*(255.0/15)*colorR);
//               int g= toInt((((*src)>>4)&0xf)*(255.0/15)*colorG);
//               int b= toInt((((*src)>>0)&0xf)*(255.0/15)*colorB);
//               *tgtLine = (a<<24)|(r<<16)|(g<<8)|b;
//               //*tgt = 0x01010101;
//               tgtLine++,src++;
//             }
//             tgt += rect.Pitch;
//           }
//         }
//         
//         _cursorSurface->UnlockRect();
//         if (w==32 && h==32)
//         {
//           int xs = toInt(w*hsX);
//           int ys = toInt(h*hsY);
//           _d3DDevice->SetCursorProperties(xs,ys,_cursorSurface);
//           ok = true;
//         }
//       }
//     }
//     ShowDeviceCursor(ok);
//   }
}

void EngineDDT::UpdateCursorPos(int x, int y)
{
  Fail("DX10 TODO");
  // Direct3D 10 NEEDS UPDATE 
//   if (x!=_cursorX || y!=_cursorY)
//   {
//     // force updates? (Does it matter)
//     _d3DDevice->SetCursorPosition(x,y,D3DCURSOR_IMMEDIATE_UPDATE);
//     _cursorX = x;
//     _cursorY = y;
//   }
}

#endif
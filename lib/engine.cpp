#include "wpch.hpp"
#include "engine.hpp"
#include "font.hpp"
#include "mbcs.hpp"
#include "textbank.hpp"
#include "global.hpp"
#include "scene.hpp"
#include "paramFileExt.hpp"
#include <El/Common/perfLog.hpp>

#include <stdarg.h>
Engine *GEngine;

const int TITexSource[] =
{
  0, // Normal
  0, // NormalDXTA
  0, // NormalMap
  0, // NormalMapThrough
  0, // NormalMapGrass
  0, // NormalMapDiffuse
  0, // Detail
  0, // Interpolation
  0, // Water
  0, // WaterSimple
  0, // White
  0, // WhiteAlpha
  0, // AlphaShadow
  0, // AlphaNoShadow
  0, // Dummy0
  2, // DetailMacroAS
  2, // NormalMapMacroAS
  3, // NormalMapDiffuseMacroAS
  0, // NormalMapSpecularMap
  0, // NormalMapDetailSpecularMap
  2, // NormalMapMacroASSpecularMap
  3, // NormalMapDetailMacroASSpecularMap
  0, // NormalMapSpecularDIMap
  0, // NormalMapDetailSpecularDIMap
  2, // NormalMapMacroASSpecularDIMap
  3, // NormalMapDetailMacroASSpecularDIMap
  0, // Terrain1
  0, // Terrain2
  0, // Terrain3
  0, // Terrain4
  0, // Terrain5
  0, // Terrain6
  0, // Terrain7
  0, // Terrain8
  0, // Terrain9
  0, // Terrain10
  0, // Terrain11
  0, // Terrain12
  0, // Terrain13
  0, // Terrain14
  0, // Terrain15
  0, // TerrainSimple1
  0, // TerrainSimple2
  0, // TerrainSimple3
  0, // TerrainSimple4
  0, // TerrainSimple5
  0, // TerrainSimple6
  0, // TerrainSimple7
  0, // TerrainSimple8
  0, // TerrainSimple9
  0, // TerrainSimple10
  0, // TerrainSimple11
  0, // TerrainSimple12
  0, // TerrainSimple13
  0, // TerrainSimple14
  0, // TerrainSimple15
  0, // Glass
  0, // NonTL
  0, // NormalMapSpecularThrough
  0, // Grass
  0, // NormalMapThroughSimple
  0, // NormalMapSpecularThroughSimple
  0, // Road
  0, // Shore
  0, // ShoreWet
  0, // Road2Pass
  0, // ShoreFoam
  0, // NonTLFlare
  0, // NormalMapThroughLowEnd
  0, // TerrainGrass1
  0, // TerrainGrass2
  0, // TerrainGrass3
  0, // TerrainGrass4
  0, // TerrainGrass5
  0, // TerrainGrass6
  0, // TerrainGrass7
  0, // TerrainGrass8
  0, // TerrainGrass9
  0, // TerrainGrass10
  0, // TerrainGrass11
  0, // TerrainGrass12
  0, // TerrainGrass13
  0, // TerrainGrass14
  0, // TerrainGrass15
  0, // Crater1
  0, // Crater2
  0, // Crater3
  0, // Crater4
  0, // Crater5
  0, // Crater6
  0, // Crater7
  0, // Crater8
  0, // Crater9
  0, // Crater10
  0, // Crater11
  0, // Crater12
  0, // Crater13
  0, // Crater14
  0, // Sprite
  0, // SpriteSimple
  0, // Cloud
  0, // Horizon
  0, // Super
  0, // Multi
  0, // TerrainX
  0, // TerrainSimpleX
  0, // TerrainGrassX
  0, // Tree
  0, // TreePRT
  0, // TreeSimple
  0, // Skin
  0, // CalmWater
  0, // TreeAToC 
  0, // GrassAToC
  0, // TreeAdv
  0, // TreeAdvSimple
  0, // TreeAdvTrunk
  0, // TreeAdvTrunkSimple
  0, // TreeAdvAToC
  0, // TreeAdvSimpleAToC
  0, // TreeSN
  0, // SpriteExtTi
  0, // TerrainSNX
  0, // SimulWeather clouds  
  0, // SimulWeather clouds with lightning  
  0, // SimulWeather clouds with CPU distance fading  
  0, // SimulWeather clouds with lightning and CPU distance fading  
  0, // SuperExt
  0, // SuperAToC
	0, // PSNonTLHeatHaze
};
namespace Unique0
{
  COMPILETIME_COMPARE(lenof(TITexSource), NPixelShaderID)
}

const int VertexDeclarations[] =
{
  VDI_TEX0,
  VDI_POSITION | VDI_TEX0 | VDI_COLOR,
  VDI_POSITION | VDI_NORMAL | VDI_TEX0,
  VDI_POSITION | VDI_NORMAL | VDI_TEX0 | VDI_ST,
  VDI_POSITION | VDI_NORMAL | VDI_TEX0 | VDI_TEX1,
  VDI_POSITION | VDI_NORMAL | VDI_TEX0 | VDI_TEX1 | VDI_ST,
  VDI_POSITION | VDI_NORMAL | VDI_TEX0 | VDI_ST | VDI_FLOAT3,
  VDI_POSITION | VDI_NORMAL | VDI_TEX0 | VDI_WEIGHTSANDINDICES,
  VDI_POSITION | VDI_NORMAL | VDI_TEX0 | VDI_ST | VDI_WEIGHTSANDINDICES,
  VDI_POSITION | VDI_NORMAL | VDI_TEX0 | VDI_TEX1 | VDI_WEIGHTSANDINDICES,
  VDI_POSITION | VDI_NORMAL | VDI_TEX0 | VDI_TEX1 | VDI_ST | VDI_WEIGHTSANDINDICES,
  VDI_SHADOWVOLUME,
  VDI_SHADOWVOLUMESKINNED,
  VDI_SIMULWEATHERCLOUDS,
  VDI_SIMULWEATHERCLOUDS_CPU
};
namespace Unique1
{
  COMPILETIME_COMPARE(lenof(VertexDeclarations), VDCount)
}

const EVertexDecl ShaderVertexDeclarations[] =
{
  VD_Position_Normal_Tex0,            // Basic
  VD_Position_Normal_Tex0_ST,         // NormalMap
  VD_Position_Normal_Tex0_ST,         // NormalMapDiffuse
  VD_Position_Normal_Tex0,            // Grass
  VD_None,                            // Dummy
  VD_None,                            // Dummy
  VD_ShadowVolume,                    // ShadowVolume
  VD_Position_Normal_Tex0_ST,         // Water
  VD_Position_Normal_Tex0_ST,         // WaterSimple
  VD_Tex0,                            // Sprite
  VD_Position_Tex0_Color,             // Point
  VD_Position_Normal_Tex0_ST,         // NormalMapThrough
  VD_None,                            // Dummy 
  VD_Position_Normal_Tex0_ST_Float3,  // VSTerrain (float3 used for lod blending)
  VD_Position_Normal_Tex0,            // BasicAS
  VD_Position_Normal_Tex0_ST,         // NormalMapAS
  VD_Position_Normal_Tex0_ST,         // NormalMapDiffuseAS
  VD_Position_Normal_Tex0,            // Glass
  VD_Position_Normal_Tex0_ST,         // NormalMapSpecularThrough
  VD_Position_Normal_Tex0_ST,         // NormalMapThroughNoFade
  VD_Position_Normal_Tex0_ST,         // NormalMapSpecularThroughNoFade
  VD_Position_Normal_Tex0_ST_Float3,  // VSShore (float3 used for lod blending)
  VD_Position_Normal_Tex0_ST_Float3,  // VSTerrainGrass (float3 used for lod blending)
  VD_Position_Normal_Tex0_ST,         // Super
  VD_Position_Normal_Tex0_ST,         // Multi
  VD_Position_Normal_Tex0_ST,         // Tree
  VD_Position_Normal_Tex0_ST,         // TreeNoFade
  VD_Position_Normal_Tex0,            // TreePRT
  VD_Position_Normal_Tex0,            // TreePRTNoFade
  VD_Position_Normal_Tex0_ST,         // Skin
  VD_Position_Normal_Tex0_ST,         // CalmWater ( need only pos but set as Super )
  VD_Position_Normal_Tex0_ST,         // VSTreeAdv
  VD_Position_Normal_Tex0_ST,         // VSTreeAdvTrunk
  VD_SimulWeatherClouds,             // VSSimulWeatherClouds
  VD_SimulWeatherCloudsCPU,           //VSSimulWeatherCloudsCPU
};
namespace Unique2
{
  COMPILETIME_COMPARE(lenof(ShaderVertexDeclarations),NVertexShaderID)
}

const VertexShaderPoolID VSToVSP[] =
{
  VSPBasic,
  VSPNormalMap,
  VSPNormalMapDiffuse,
  VSPGrass,
  NVertexShaderPoolID,
  NVertexShaderPoolID,
  NVertexShaderPoolID,
  NVertexShaderPoolID,
  NVertexShaderPoolID,
  NVertexShaderPoolID,
  NVertexShaderPoolID,
  VSPNormalMapThrough,
  NVertexShaderPoolID,
  NVertexShaderPoolID,
  VSPBasicAS,
  VSPNormalMapAS,
  VSPNormalMapDiffuseAS,
  VSPGlass,
  VSPNormalMapSpecularThrough,
  VSPNormalMapThroughNoFade,
  VSPNormalMapSpecularThroughNoFade,
  NVertexShaderPoolID,
  NVertexShaderPoolID,
  VSPSuper,
  VSPMulti,
  VSPTree,
  VSPTreeNoFade,
  VSPTreePRT,
  VSPTreePRTNoFade,
  VSPSkin,
  NVertexShaderPoolID,
  VSPTreeAdv,
  VSPTreeAdvTrunk,
  NVertexShaderPoolID,
  NVertexShaderPoolID,
};
namespace Unique3
{
  COMPILETIME_COMPARE(lenof(VSToVSP),NVertexShaderID)
}

const VertexShaderSpecialID VSToVSS[] =
{
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  VSSWater,
  VSSWaterSimple,
  VSSSprite,
  VSSPoint,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  VSSShore,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  VSSCalmWater,
  NVertexShaderSpecialID,
  NVertexShaderSpecialID,
  VSSSimulWeatherClouds,
  VSSSimulWeatherCloudsCPU
};
namespace Unique4
{
  COMPILETIME_COMPARE(lenof(VSToVSS),NVertexShaderID)
}

void AspectSettings::GetUISafeArea(float &left, float &top, float &right, float &bottom)
{
  Assert(uiBottomRightX > uiTopLeftX);
  float coefX = 1.0f / (uiBottomRightX - uiTopLeftX);
  left = (0.0f - uiTopLeftX) * coefX;
  right = (1.0f - uiTopLeftX) * coefX;

  Assert(uiBottomRightY > uiTopLeftY);
  float coefY = 1.0f / (uiBottomRightY - uiTopLeftY);
  top = (0.0f - uiTopLeftY) * coefY;
  bottom = (1.0f - uiTopLeftY) * coefY;
}

Engine::Engine()
:_showTextFont(NULL),
_showTextColor(Color(HBlack)),_showTextSize(0),
_showFps(0),
#if _ENABLE_CHEATS
  _statPageStart(0),
#endif
_messageHandle(-1),
#if ENABLE_NV
_nightVision(false),
#endif
#if ENABLE_TI
_thermalVision(false),
#endif
_thermalWanted(false),
_accomodateEye(HWhite),_lightGain(HWhite),
_shadowLA(HWhite),_shadowLD(HWhite),_shadowFactor(1.0f),_accomFactor(1.0),
_usrBrightness(1),
_fogColor(HWhite),_skyTopColor(HWhite),
_frameTime(0),
_sceneProps(&EngineSceneProperties::_default),
_startGame(GlobalTickCount()),
_textHandle(0),
_monitorOffsetX(0),
_monitorOffsetY(0)
#if _ARMA3_HDR_IMPROVEMENTS
,_hdrLuminanceRectCoef(1.0f)
,_desiredLuminanceCoef(1.0f)
#endif
{
	_showTextSize = 0.021;
	_showTextFont = NULL;
	_debugFont = NULL;
	_debugFontSize = _showTextSize;
	ResetFrameDuration();
}

void Engine::Init()
{
  ParamEntryVal fontPars = Pars >> "CfgInGameUI" >> "DebugFont";
	_showTextFont=LoadFont(GetFontID(fontPars >> "font"));
	_debugFont = _showTextFont;
	_debugFontSize = fontPars>>"size";
	/// initialize surface properties 
	TextBank()->Init();
}

void Engine::ResetFrameDuration()
{
	for( int i=0; i<NFrameDurations; i++ ) _frameDurations[i]=70;
	_lastFrameDuration=70;
}

DWORD Engine::GetMaxFrameDuration( int nFrames ) const
{
	DWORD max=0;
	saturateMin(nFrames,NFrameDurations);
	for( int i=NFrameDurations-nFrames; i<NFrameDurations; i++ )
	{
	  if (_frameDurations[i]>max) max = _frameDurations[i];
	}
	return max;
}

DWORD Engine::GetMinFrameDuration( int nFrames ) const
{
	DWORD min=INT_MAX;
	saturateMin(nFrames,NFrameDurations);
	for( int i=NFrameDurations-nFrames; i<NFrameDurations; i++ )
	{
	  if (_frameDurations[i]<min) min = _frameDurations[i];
	}
	return min;
}

float Engine::GetMinFrameDurationSmooth( const DWORD (&frameDurations)[NFrameDurations], int nFrames )
{
  float min=FLT_MAX;
  saturateMin(nFrames,NFrameDurations);
  float smoothOld = 1.00;
  const float smoothingFactor = 1.003f; // ~~ 1.21^(1/64)
  int i;
  for( i=0; i<nFrames/2; i++ )
  {
    float smoothed = frameDurations[NFrameDurations-1-i];
    if (smoothed<min) min = smoothed;
  }
  for( ; i<nFrames; i++ )
  {
    float smoothed = smoothOld*frameDurations[NFrameDurations-1-i];
    if (smoothed<min) min = smoothed;
    smoothOld *= smoothingFactor;
  }
  return min;

}

void Engine::ResetTimingHistory(int ms)
{
  // need to convert to vsync counts
	for( int i=0; i<NFrameDurations; i++)
	{
	  _frameDurations[i] = ms;
	}
	_frameTime = GlobalTickCount();
}

void Engine::ResetTimingVSync()
{
}

bool Engine::CanVSyncTime() const
{
  return false;
}

float Engine::GetAvgFrameDuration( int nFrames ) const
{
	DWORD sum=0;
	saturateMin(nFrames,NFrameDurations);
	for( int i=NFrameDurations-nFrames; i<NFrameDurations; i++ ) sum+=_frameDurations[i];
	return float(sum)/nFrames;
}

float Engine::GetAvgFrameDurationVsync() const
{
	return RefreshRate()*GetAvgFrameDuration()*0.001f;
}

float Engine::GetSmoothFrameDuration() const
{
  return 0;
}
float Engine::GetSmoothFrameDurationMax() const
{
  return GetMaxFrameDuration()*0.001f;
}

int Engine::GetMaxFrameDurationVsync() const
{
	return toInt(RefreshRate()*GetMaxFrameDuration()*0.001);
}

float Engine::GetTimeVSync() const
{
  return Glob.time.toFloat()*1000;
}
int Engine::GetPresentationIntervalVSync() const
{
  return 1;
}
int Engine::GetLastFrameEndVSync() const
{
  return 0;
}

int Engine::GetKnownErrorVsync() const
{
  return 0;
}


#if _ENABLE_CHEATS
RString Engine::GetStat(int statId, RString &statVal, RString &statVal2)
{
	statVal = RString();
	statVal2 = RString();
	return RString();
}
#endif

void Engine::FontDestroyed( Font *font )
{
	#ifndef ACCESS_ONLY
	_fonts.RemoveFont(font);
	#endif
}

Engine::~Engine()
{
	#ifndef ACCESS_ONLY
	// fonts need to be already cleared
	// otherwise pure virtual function would be called now,
	// because this is already partially destructed
	_fonts.Clear();
	#endif
}

void Draw2DPars::Init()
{
	spec=NoZBuf|IsAlpha|ClampU|ClampV|IsAlphaFog;
	SetU(0,1); // u,v range
	SetV(0,1);
}

void Draw2DParsExt::Init()
{
  spec = NoZBuf | IsAlpha | ClampU | ClampV | IsAlphaFog;
  SetU(0, 1); // u,v range
  SetV(0, 1);
}

int Engine::Width2D() const
{
	return toInt((_aspectSettings.uiBottomRightX-_aspectSettings.uiTopLeftX)*WidthBB());
}

int Engine::Height2D() const
{
	return toInt((_aspectSettings.uiBottomRightY-_aspectSettings.uiTopLeftY)*HeightBB());
}
int Engine::Left2D() const
{
	return toInt(_aspectSettings.uiTopLeftX*WidthBB());
}
int Engine::Top2D() const
{
	return toInt(_aspectSettings.uiTopLeftY*HeightBB());
}

Rect2DPixel Rect2DClipPixel(-1e6,-1e6,2e6,2e6);
Rect2DAbs Rect2DClipAbs(0,0,1e6,1e6);
Rect2DFloat Rect2DClipFloat(-1e3,-1e3,2e3,2e3);

void Engine::Convert(Point2DAbs &to, const Point2DPixel &from)
{
	to.x = from.x + Left2D();
	to.y = from.y + Top2D();
}
void Engine::Convert(Point2DAbs &to, const Point2DFloat &from)
{
	to.x = from.x*Width2D() + Left2D();
	to.y = from.y*Height2D() + Top2D();
}

void Engine::Convert(Point2DPixel &to, const Point2DAbs &from)
{
	to.x = from.x - Left2D();
	to.y = from.y - Top2D();
}
void Engine::Convert(Point2DFloat &to, const Point2DAbs &from)
{
	to.x = (from.x - Left2D())/Width2D();
	to.y = (from.y - Top2D())/Height2D();
}

void Engine::Convert(Rect2DAbs &to, const Rect2DPixel &from)
{
	to.x = from.x + Left2D();
	to.y = from.y + Top2D();
	to.w = from.w;
	to.h = from.h;
}
void Engine::Convert(Rect2DAbs &to, const Rect2DFloat &from)
{
	float w2d = Width2D();
	float h2d = Height2D();
	to.x = from.x*w2d + Left2D();
	to.y = from.y*h2d + Top2D();
	to.w = from.w*w2d;
	to.h = from.h*h2d;
}
void Engine::Convert(Rect2DPixel &to, const Rect2DAbs &from)
{
	to.x = from.x - Left2D();
	to.y = from.y - Top2D();
	to.w = from.w;
	to.h = from.h;
}
void Engine::Convert(Rect2DFloat &to, const Rect2DAbs &from)
{
	float w2d = Width2D();
	float h2d = Height2D();
	to.x = (from.x - Left2D())/w2d;
	to.y = (from.y - Top2D())/h2d;
	to.w = from.w/w2d;
	to.h = from.h/h2d;
}

void Engine::Convert(Line2DAbs &to, const Line2DPixel &from)
{
	float l2d = Left2D();
	float t2d = Top2D();
	to.beg.x = from.beg.x + l2d;
	to.beg.y = from.beg.y + t2d;
	to.end.x = from.end.x + l2d;
	to.end.y = from.end.y + t2d;
}
void Engine::Convert(Line2DAbs &to, const Line2DFloat &from)
{
	float w2d = Width2D();
	float h2d = Height2D();
	float l2d = Left2D();
	float t2d = Top2D();
	to.beg.x = from.beg.x*w2d + l2d;
	to.beg.y = from.beg.y*h2d + t2d;
	to.end.x = from.end.x*w2d + l2d;
	to.end.y = from.end.y*h2d + t2d;
}
void Engine::Convert(Line2DPixel &to, const Line2DAbs &from)
{
	float l2d = Left2D();
	float t2d = Top2D();
	to.beg.x = from.beg.x - l2d;
	to.beg.y = from.beg.y - t2d;
	to.end.x = from.end.x - l2d;
	to.end.y = from.end.y - t2d;
}
void Engine::Convert(Line2DFloat &to, const Line2DAbs &from)
{
	float w2d = Width2D();
	float h2d = Height2D();
	float l2d = Left2D();
	float t2d = Top2D();
	to.beg.x = (from.beg.x - l2d)/w2d;
	to.beg.y = (from.beg.y - t2d)/h2d;
	to.end.x = (from.end.x - l2d)/w2d;
	to.end.y = (from.end.y - t2d)/h2d;
}

void Engine::PixelAlignXY(Point2DAbs &pos)
{
	pos.x = toInt(pos.x)+0.5f;
	pos.y = toInt(pos.y)+0.5f;
}
void Engine::PixelAlignX(Point2DAbs &pos)
{
	pos.x = toInt(pos.x)+0.5f;
}
void Engine::PixelAlignY(Point2DAbs &pos)
{
	pos.y = toInt(pos.y)+0.5f;
}
void Engine::PixelAlignXY(Point2DPixel &pos)
{
	pos.x = toInt(pos.x)+0.5f;
	pos.y = toInt(pos.y)+0.5f;
}
void Engine::PixelAlignX(Point2DPixel &pos)
{
	pos.x = toInt(pos.x)+0.5f;
}
void Engine::PixelAlignY(Point2DPixel &pos)
{
	pos.y = toInt(pos.y)+0.5f;
}

float Engine::PixelAlignedX(float x)
{
	return toInt(x)+0.5f;
}
float Engine::PixelAlignedY(float y)
{
	return toInt(y)+0.5f;
}

void Engine::DrawLines
(
	const Line2DAbsInfo *lines, int nLines,
	const Rect2DAbs &clip
)
{
  if (nLines<=0) return;
  DrawLinePrepare();

  for (int i=0; i<nLines; i++)
  {
    const Line2DAbsInfo &line = lines[i];
  	DrawLineDo(line,line.c0,line.c1,clip);
  }
}

void Engine::DrawLines
(
	const Line2DPixelInfo *lines, int nLines,
	const Rect2DPixel &clip
)
{
  if (nLines<=0) return;

  DrawLinePrepare();

	float l2d = Left2D();
	float t2d = Top2D();

  Line2DAbs lineA;
	Rect2DAbs clipA;
	Convert(clipA,clip);
  for (int i=0; i<nLines; i++)
  {
    const Line2DPixelInfo &line = lines[i];
	  lineA.beg.x = line.beg.x + l2d;
	  lineA.beg.y = line.beg.y + t2d;
	  lineA.end.x = line.end.x + l2d;
	  lineA.end.y = line.end.y + t2d;
  	DrawLineDo(lineA,line.c0,line.c1,clipA);
  }
}

void Engine::SaveConfig(ParamFile &cfg, const char *section)
{
  cfg.Add("brightness",_usrBrightness);
	cfg.Add("useWBuffer",IsWBuffer());
	//cfg.Add("fovBottom",_aspectSettings.bottomFOV);
	cfg.Add("fovTop",_aspectSettings.topFOV);
	//cfg.Add("fovRight",_aspectSettings.rightFOV);
	cfg.Add("fovLeft",_aspectSettings.leftFOV);

	cfg.Add("uiTopLeftX",_aspectSettings.uiTopLeftX);
	cfg.Add("uiTopLeftY",_aspectSettings.uiTopLeftY);
	cfg.Add("uiBottomRightX",_aspectSettings.uiBottomRightX);
	cfg.Add("uiBottomRightY",_aspectSettings.uiBottomRightY);

  cfg.Add("IGUIScale",Glob.config.IGUIScale);

  cfg.Add("tripleHead",_aspectSettings.tripleHead);
  cfg.Add("useExtendetInfoType", Glob.config.useExtendetInfoType);
}

void Engine::LoadConfig(ParamEntryPar cfg,ParamEntryPar fcfg, const char *section)
{
	SetBrightness(cfg.ReadValue("brightness", GetDefaultBrightness()));
	//_aspectSettings.bottomFOV = cfg.ReadValue("fovBottom",0.75);

   if(FindDefaultAspect(cfg)) return;

   Glob.config.IGUIScale = cfg.ReadValue("IGUIScale",Glob.config.NormalScale*0.01);
   Glob.config.useExtendetInfoType = cfg.ReadValue("useExtendetInfoType", true);

    _aspectSettings.uiTopLeftX = cfg.ReadValue("uiTopLeftX", 0.075f);
	  _aspectSettings.uiTopLeftY = cfg.ReadValue("uiTopLeftY", 0.075f);
	  _aspectSettings.uiBottomRightX = cfg.ReadValue("uiBottomRightX",0.925f);
	  _aspectSettings.uiBottomRightY = cfg.ReadValue("uiBottomRightY",0.925f);
  	_aspectSettings.topFOV = cfg.ReadValue("fovTop",0.75f);
	  _aspectSettings.leftFOV = cfg.ReadValue("fovLeft",1.0f);
    _aspectSettings.tripleHead = cfg.ReadValue("tripleHead",false);
}

bool Engine::FindDefaultAspect(ParamEntryPar cfg)
{
  float defaultTopFOV = 0.75f;
  float defaultLeftFOV = 1.0f;

  if(!IsWindowed())
  {//default aspect is taken from windows settings
#ifndef _SERVER    
    RECT rect = GetMonitorResolution();
    if(rect.bottom>0)
    {
      if(Glob.config.defaultAspect<=0.1) //has not initialized yet
        Glob.config.defaultAspect = ((float)(rect.right - rect.left )/ (rect.bottom - rect.top));
      defaultLeftFOV = Glob.config.defaultAspect / (4.0f/3);
    }
#endif
  }
  else
  { //if windowed take aspect based on window size
    if(HeightBB()>0)
      defaultLeftFOV = ((float)WidthBB() / HeightBB())/ (4.0f/3) ;;
  }
  //if not yet defined use defaultLeftFOV as default config value
  if(cfg.FindEntry("fovTop") && cfg.FindEntry("fovLeft"))
    return false;
  //if aspect not present in config file..
  Glob.config.IGUIScale = Glob.config.NormalScale*0.01;
  Glob.config.useExtendetInfoType = true;

  int height = HeightBB();

  if(height < 768) Glob.config.IGUIScale = Glob.config.LargeScale*0.01;
  else if(height < 1024) Glob.config.IGUIScale = Glob.config.NormalScale*0.01;
  else if(height < 1050) Glob.config.IGUIScale = Glob.config.SmallScale*0.01;
  else Glob.config.IGUIScale = Glob.config.VerySmallScale*0.01;
  
  float defaultUiTopLeftX = 0.075f;
  float defaultUiTopLeftY = 0.075f;

  float defaultUiBottomRightX = 0.925f;
  float defaultUiBottomRightY = 0.925f;

  float sc = 0.5f * (1 - Glob.config.IGUIScale);
  //compute wide-screen safe zones
  if(defaultLeftFOV/defaultTopFOV >= (4.0/3.0))
  {
    defaultUiTopLeftY = sc;
    defaultUiBottomRightY = 1 - sc;

    defaultUiTopLeftX =   0.5 * (1.0 -((defaultTopFOV * (4.0/3.0) * Glob.config.IGUIScale)/defaultLeftFOV));
    defaultUiBottomRightX = 1.0 - defaultUiTopLeftX;
  }
  else
  {
    defaultUiTopLeftX = sc;
    defaultUiBottomRightX = 1 - sc;

    defaultUiTopLeftY =  0.5f * (1 -((defaultLeftFOV * (3.0f/4.0f) * Glob.config.IGUIScale) / defaultTopFOV));
    defaultUiBottomRightY = 1 - defaultUiTopLeftY;
  }
  //set aspect
  _aspectSettings.uiTopLeftX = defaultUiTopLeftX;
  _aspectSettings.uiTopLeftY = defaultUiTopLeftY;
  _aspectSettings.uiBottomRightX = defaultUiBottomRightX;
  _aspectSettings.uiBottomRightY = defaultUiBottomRightY;
  _aspectSettings.topFOV = defaultTopFOV;
  _aspectSettings.leftFOV = defaultLeftFOV;
  //if extra wide - expect triplehead
  if(GEngine && (float)GEngine->WidthBB() / GEngine->HeightBB() >= (3.5 - 0.0001)) _aspectSettings.tripleHead = true;
  else _aspectSettings.tripleHead = false;

  return true;
}

/**
@param fogColor color of fog and sky near the horizon
@param skyTopColor color of sky top (zenith)
*/
void Engine::SetFogColor( ColorVal fogColor, ColorVal skyTopColor )
{
	_fogColor = fogColor;
	_skyTopColor = skyTopColor;
	FogColorChanged(_fogColor);
}

void Engine::ReinitCounters()
{
	#if _ENABLE_PERFLOG
	GPerfCounters.Reinit();
	GPerfProfilers.Reinit();
	_startGame=GlobalTickCount();
	#endif
  // Make the initial mipbias value a bit worst (the engine should improve it quickly if he is able to...)
	TextBank()->SetTextureMipBias(TextBank()->GetTextureMipBiasMin()*1.3);
}

void Engine::RecalcAccomodation()
{
  float bright = GetPreHDRBrightness();
	_accomodateEye=_lightGain*(bright*_accomFactor);
	_accomodateEye.SetA(1);
}

#include "keyInput.hpp"
#include "dikCodes.h"

  bool thermalCheat = false;

void Engine::SetNightVision( bool state, bool thermalState, float accomFactor, bool isFixed )
{
  // verify we can safely access the device

# if _ENABLE_CHEATS
  if (GInput.GetCheat2ToDo(DIK_N))
  {
    thermalCheat = !thermalCheat;
    DIAG_MESSAGE(500, Format("Thermal: %s", thermalCheat ? "ON" : "OFF"));
  }
# elif (_BULDOZER)
  if (GInput.GetKeyToDo(DIK_N))
  {
    thermalCheat = !thermalCheat;
    GlobalShowMessage(500, Format("Thermal: %s", thermalCheat ? "ON" : "OFF"));
  }
# endif

  EnableThermalVision(thermalState || thermalCheat, accomFactor, isFixed);

  #if ENABLE_NV
  if (state)
  {
    static float gain = 3000.0f;
    _lightGain = Color(gain,gain,gain);
  }
  else
  #endif
  {
    _lightGain = Color(1,1,1);
  }

  _accomFactor = accomFactor;
  _accomFactorFixed = isFixed;
  RecalcAccomodation();

  #if ENABLE_NV
  _nightVision=state;
  #endif
}

bool Engine::GetNightVision() const
{
    return _nightVision;
}

void Engine::EnableThermalVision(bool state, float accomFactor, bool isFixed)
{
  #if ENABLE_NV
  _thermalVision = state;
  if (state)
  {
    static float gain = 3000.0f;
    _lightGain = Color(gain,gain,gain);
    _accomFactor = 1.0f;
    _accomFactorFixed = true;
  }
  else
  #endif
  {
    _lightGain = Color(1,1,1);
    _accomFactor = accomFactor;
    _accomFactorFixed = isFixed;
  }
  RecalcAccomodation();
}

bool Engine::GetThermalVision() const
{
  return _thermalVision;
}

void Engine::InitDraw( bool clear, Color color )
{
  RecalcAccomodation();
}

void Engine::FinishDraw(int wantedDurationVSync, bool allBackBuffers, RString fileName)
{
  // call non-virtual function to prevent possible recursion
  Engine::FinishNoDraw(wantedDurationVSync);
}

void Engine::InitNoDraw()
{
}
void Engine::FinishNoDraw(int wantedDurationVSync)
{
	DWORD time = GlobalTickCount();

	// every second update stats
	// every frame update stats
	if (_frameTime>0)
	{
		COUNTER(tris);
		_lastFrameDuration = time-_frameTime;

    #if 0 // _ENABLE_PERFLOG
      // simulated frame scope    
      DEF_COUNTER_P_EX(frame,*,PROF_COUNT_SCALE);
      //bool profiling = !GPerfProfilers.IsCapturePending() && (GPerfProfilers.IsCapturing() || CheckMainThread());
      //if (profiling || GPerfProfilers.IsCapturing())
      //if (!GPerfProfilers.IsCapturePending() && GPerfProfilers.IsCapturing())
      if (GPerfProfilers.IsCapturing())
      {
        static __int64 oldNow;
        __int64 now = ProfileTime();
        __int64 duration = now-oldNow;
        __int64 start = GPerfProfilers.GetFrameStart();
        //__int64 durationComp = (__int64(_lastFrameDuration)<<PROF_ACQ_SCALE_LOG)*PROF_COUNT_SCALE*100;
        GPerfProfilers.AddCapture(start, start+duration, RString(), &COUNTER_P_NAME(frame));
        oldNow = now;
      }
    #endif

		for( int i=0; i<NFrameDurations-1; i++ )
		{
			_frameDurations[i] = _frameDurations[i+1];
			_frameTriangles[i] = _frameTriangles[i+1];
		}
		_frameDurations[NFrameDurations-1] = _lastFrameDuration;
		_frameTriangles[NFrameDurations-1] = COUNTER_VALUE(tris);
	}
	_frameTime = time;

	// now its time to draw performance logs

}

EngineSceneProperties EngineSceneProperties::_default;

void Engine::SetSceneProperties(EngineSceneProperties *sceneProps)
{
  _sceneProps = sceneProps;
}

void Engine::NextFrame(bool doPresent)
{
	// swap frames - get ready for next frame
	
}

#define INV_SQRT_2 0.70710678119
void NormalizeST(Vector3Par middleST, Vector3Par normal, Vector3 &outS, Vector3 &outT)
{
  // Normalize the middleST
  float t = middleST * normal;
  Vector3 normalizedMiddleST = (middleST - normal * t).Normalized();

  // Get vector orthogonal to the middleST
  Vector3 orthoNormalizedMiddleST = normalizedMiddleST.CrossProduct(normal);

  // Get outS and outT from the middle of the middleST and orthoNormalizedMiddleST
  // Since the source vectors are orthonormal, the normalization of the destination is achieved
  // by dividing by sqrt(2)
  outS = (orthoNormalizedMiddleST + normalizedMiddleST) * INV_SQRT_2;
  outT = (-orthoNormalizedMiddleST + normalizedMiddleST) * INV_SQRT_2;
}

bool GenerateST(Vector3Compressed &anormal, Vector3Par apos, Vector3Par bpos, Vector3Par cpos,
                const UVPair &at, const UVPair &bt, const UVPair &ct,
                Vector3Compressed &outS, Vector3Compressed &outT, int bppS, int bppT)
{
  float k = bt.u - at.u;
  float l = bt.v - at.v;
  float m = ct.u - at.u;
  float n = ct.v - at.v;
  Vector3 v1 = bpos - apos;
  Vector3 v2 = cpos - apos;
  float kn = k * n;
  float lm = l * m;
  float d = lm - kn;

  Vector3 anormalVec = anormal.Get();
  // Test the vectors to be linearly dependent
  if (fabs(d) < 1e-8)
  {
    // First try s to be (1,0,0) vector
    Vector3 s(1, 0, 0);
    float t = s * anormalVec;
    s = s - anormalVec * t;
    if (s.SquareSize() < FLT_MIN)
    {
      s.Normalize();
    }
    else
    {
      // (1,0,0) failed, so (0,1,0) must work
      s = Vector3(0, 1, 0);
      t = s * anormalVec;
      s = (s - anormalVec * t).Normalized();
    }

    // Write out some reasonable values
    outS.Set(s);
    outT.Set(anormalVec.CrossProduct(s));

    // The vectors in the texture are linearly dependent
    return false;
  }

  // U to S
  Vector3 outSVec = (-n * v1 * d + l * v2 * d).Normalized();

  // V to T
  Vector3 outTVec = (m * v1 * d - k * v2 * d).Normalized();

  // Normalize the outS and outT using the anormal
  // The reference vector is the normal - the outS and outT must be orthogonal to it
  {

    // Get the middleST vector and make it orthogonal to normal
    Vector3 middleST = (outSVec + outTVec) * 0.5f;
    float t = middleST * anormalVec;
    middleST = middleST - anormalVec * t;
    if (middleST.SquareSize() < FLT_MIN)
    {
      // The middle ST is lineary dependent on normal
      return false;
    }
    middleST.Normalize();

    // Get vector orthogonal to the middleST
    Vector3 orthoMiddleST = anormalVec.CrossProduct(middleST);

    if (orthoMiddleST.DotProduct(outSVec) > 0.0f)
    {
      // Get outS and outT from the middle of the middleST and orthoModdleST
      outSVec = (orthoMiddleST + middleST).Normalized();
      outTVec = (-orthoMiddleST + middleST).Normalized();
    }
    else
    {
      // Get outS and outT from the middle of the middleST and orthoModdleST
      outSVec = (-orthoMiddleST + middleST).Normalized();
      outTVec = (orthoMiddleST + middleST).Normalized();
    }
  }

  // Move anormal to the ST direction according to the texture precision and recalculate
  // the ST vectors again. This step is here, because information in the texture cannot
  // hold the zero precisely
  {
    //const float moveS = 0.043137254902f;
    //const float moveT = 0.043137254902f;
    float moveS = 1.0f / (float(1 << bppS) - 1.0f);
    float moveT = 1.0f / (float(1 << bppT) - 1.0f);
    float moveN = 1.0f - sqrt(1.0f - moveS * moveS - moveT * moveT);

    // Move anormal in the ST direction
    anormalVec = anormalVec + outSVec * moveS + outTVec * moveT - anormalVec * moveN;
    // Normalization is not necessary
    if (!anormalVec.IsFinite())
    {
      anormalVec = VUp;
    }

    // Orthogonalize ST
    {

      // Get the middle vector
      Vector3 middleST = (outSVec + outTVec) * 0.5f;
      float t = middleST * anormalVec;
      middleST = middleST - anormalVec * t;
      middleST.Normalize();

      // Get vector orthogonal to the middleST
      Vector3 orthoMiddleST = anormalVec.CrossProduct(middleST);

      if (orthoMiddleST.DotProduct(outSVec) > 0.0f)
      {
        // Get outS and outT from the middle of the middleST and orthoModdleST
        outSVec = (orthoMiddleST + middleST).Normalized();
        outTVec = (-orthoMiddleST + middleST).Normalized();
      }
      else
      {
        // Get outS and outT from the middle of the middleST and orthoModdleST
        outSVec = (-orthoMiddleST + middleST).Normalized();
        outTVec = (orthoMiddleST + middleST).Normalized();
      }
    }
  }

  outS = Vector3Compressed(outSVec);
  outT = Vector3Compressed(outTVec);
  anormal = Vector3Compressed(anormal);
  
  return true;
}

bool Engine::IsCursorShown() const
{
  return false;
}

const DrawParameters DrawParameters::_default;

const DrawParameters DrawParameters::_noShadow(false, false, false, false, ZSpaceNormal, 0);

const InstanceParameters InstanceParameters::_default;

const float DepthBorderCommonMin = 0.011f;
const float DepthBorderCommonMax = 0.999f; // avoid 1.0, as we want it to be reserved for sky only
#if _HELISIM_RENDER_TO_TEXTURE

static const EnumName RenderTargetIDNames[] =
{
  R2T_ENUM(RenderTargetID, R2T, ENUM_NAME)
  EnumName()
};

template <>
const EnumName *GetEnumNames(RenderTargetID dummy)
{
  return RenderTargetIDNames;
}

#endif

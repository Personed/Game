#ifdef _MSC_VER
#pragma once
#endif

#ifndef __ENGINE_HPP
#define __ENGINE_HPP

#include "wpch.hpp"
#include "types.hpp"
#include <El/Math/math3d.hpp>
#include <El/Color/colors.hpp>
#include <Es/Containers/staticArray.hpp>
#include <Es/Containers/streamArray.hpp>
#include <El/ParamFile/paramFileDecl.hpp>
#include "font.hpp"
#include "pactext.hpp"
#include "Shape/vertex.hpp"

#if _ARMA3_SIMUL_WEATHER
class SimulWeatherBase;
#endif

#include <Es/Containers/array.hpp>

// abstract engine definition - suitable for software/DirectX/MSI interfaces
// low-level drawing
// current implementations
// Glide 2/3 -> EngineGlide
// DirectX 5/6 -> EngineD3D
// DirectDraw 5 (SW Only) -> EngineDD

/*!
rain map fake by rendering as shadow map
*/
#define TEST_RAIN_MAP

/*!
enable lights even in a day
*/
#define ENABLE_DAY_LIGHTS


class Counter
{
  private:
  int _count;
  
  public:
  Counter(){_count=0;}
  void operator += ( int a ) {_count+=a;}
  operator int () const {return _count;}
  void Reset() {_count=0;}
  int Count() const {return _count;}
};

#define PERF_STATS 1

#if _ENABLE_PERFLOG
extern bool LogStatesOnce;
#else
const bool LogStatesOnce = false;
#endif

/// "sticky" text display item
struct TextInfo
{
  int _handle;
  int _id; // multiple lines allowed for each handle
  DWORD _hideTime;
  RString _text; // remember text

  TextInfo(){}
  TextInfo(int handle,int id, DWORD hideTime, RString text);

};

TypeIsMovableZeroed(TextInfo);

//! simple 2D pars, no texture
struct Draw2DParsNoTex
{
  PackedColor colorTL,colorTR,colorBL,colorBR;
  int spec; // which specflags are used

  void SetColor( PackedColor c ) {colorTL=colorTR=colorBL=colorBR=c;}
  
  void Init();
};

//! simple 2D pars, UV naturally extended from TL to BR
struct Draw2DPars: public Draw2DParsNoTex
{
  MipInfo mip; // which texture
  
  float uTL,vTL; // u,v range
  float uBR,vBR; // u,v range
  void SetU( float u0, float u1 ) {uTL=u0,uBR=u1;}
  void SetV( float v0, float v1 ) {vTL=v0,vBR=v1;}
  void Init();
};


//! simple 2D pars, UV generic
struct Draw2DParsExt: public Draw2DParsNoTex
{
  MipInfo mip; // which texture
  
  float uTL,vTL,uTR,vTR; // u,v range
  float uBL,vBL,uBR,vBR; // u,v range
  Draw2DParsExt() {}
  explicit Draw2DParsExt(const Draw2DPars &src)
  {
    Draw2DParsNoTex::operator = (src);
    mip=src.mip;
    uTL=uBL=src.uTL, uTR=uBR=src.uBR;
    vTL=vTR=src.vTL, vBL=vBR=src.vBR;
  }
  explicit Draw2DParsExt(const Draw2DParsNoTex &src)
  {
    Draw2DParsNoTex::operator = (src);
    uTL=uBL=0, uTR=uBR=1;
    vTL=vTR=0, vBL=vBR=1;
    mip.SetNoTexture();
  }
  void SetU( float u0, float u1 ) {uTL=uBL=u0,uTR=uBR=u1;}
  void SetV( float v0, float v1 ) {vTL=vTR=v0,vBL=vBR=v1;}
  void Init();
};


struct FontID;

/// manager of fonts which are currently being used
class FontCache
{
  RefArray<Font> _fonts;

  public:
  Font *Load(FontID id);
  void RemoveFont( Font *font );
  void Clear();
};


class LightList;

struct ResolutionInfo
{
  int w,h,bpp;
  bool operator == (const ResolutionInfo &info) const
  {
    return w==info.w && h==info.h && bpp==info.bpp;
  }
};
TypeIsSimple(ResolutionInfo);

const int DefSpecFlags2D=NoZBuf|IsAlpha|ClampU|ClampV|IsAlphaFog|FilterLinear;

struct Char3DContext
{
  Vector3 dir;
  Vector3 up;
  Font *font;
  Object *obj;
  float z2;
  float x1c;
  float x2c;
  float y1c;
  float y2c;
  ClipFlags clip;
  int spec;
};

//! logical viewport (viewport containing usefull information) settings
struct AspectSettings
{
  //@{ wide screen settings (ratio world to screen)
  float leftFOV;
  float topFOV;
  float fovCoef;
  //}@
  //@{ 2D UI region settings (0..1 range)
  float uiTopLeftX,uiTopLeftY;
  float uiBottomRightX,uiBottomRightY;
  //@}
  //when tripleHead UI is only on middle screen
  bool tripleHead;

  AspectSettings()
  {
    leftFOV = 1;
    topFOV = 0.75;
    fovCoef = 1.0;
    //medium UI size
    uiTopLeftX = 0.075;
    uiTopLeftY = 0.075;
    uiBottomRightX = 0.925;
    uiBottomRightY = 0.925;
    tripleHead = false;
  }

  /// Return the screen safe area in 2D UI coordinates
  void GetUISafeArea(float &left, float &top, float &right, float &bottom);
  /// Return the screen safe area in 2D screen coordinates
  void GetScreenSafeArea(float &left, float &top, float &right, float &bottom)
  {
    left = 0; top = 0; right = 1; bottom = 1;
  }
};

//@{
/*!\name 2D coordinate system
Various systems of 2D coordinates.
*/

//! position of point on screen in pixels (absolute)
/*!
Onscreen range: x = <0,GEngine-Width()), y = <0,GEngine->Height())
*/
struct Point2DAbs
{
  float x,y;
  Point2DAbs(){}
  Point2DAbs(float xx, float yy):x(xx),y(yy){}
};

//! 2d rectangle
struct Rect2DAbs
{
  float x,y,w,h; // rectangle
  Rect2DAbs(){}
  Rect2DAbs( float xx, float yy, float ww, float hh ){x=xx,y=yy,w=ww,h=hh;}
  Rect2DAbs( const Point2DAbs &pos, float ww, float hh ){x=pos.x,y=pos.y,w=ww,h=hh;}
};
struct Line2DAbs
{
  Point2DAbs beg,end;
  Line2DAbs(){}
  Line2DAbs( float x0, float y0, float x1, float y1){beg.x=x0,beg.y=y0,end.x=x1,end.y=y1;}
};

struct Line2DAbsInfo: public Line2DAbs
{
  PackedColor c0,c1;
};

TypeIsSimple(Line2DAbsInfo)

//! uses same coordinate system as Point2DAbs and Rect2DAbs
struct Vertex2DAbs: Point2DAbs
{
  float z,w; // screen coordinates
  float u,v; // texture coordinates
  PackedColor color; // color

  Vertex2DAbs(){z = 0.5f,w = 1.0f;}
};

TypeIsSimple(Vertex2DAbs)

//! default clipping rectangle
extern Rect2DAbs Rect2DClipAbs;

//! position of point in viewport in pixels
/*!
Insideviewport range: x = <0,GEngine->Width2D()), y = <0,GEngine->Height2D())
*/
struct Point2DPixel
{
  float x,y;
  Point2DPixel(){}
  Point2DPixel(float xx, float yy):x(xx),y(yy){}
};

//! position of rectangle in viewport in pixels
struct Rect2DPixel
{
  float x,y,w,h; // rectangle
  Rect2DPixel(){}
  Rect2DPixel( float xx, float yy, float ww, float hh ){x=xx,y=yy,w=ww,h=hh;}
};

struct Line2DPixel
{
  Point2DPixel beg,end;
  Line2DPixel(){}
  Line2DPixel( float x0, float y0, float x1, float y1){beg.x=x0,beg.y=y0,end.x=x1,end.y=y1;}
};

struct Line2DPixelInfo: public Line2DPixel
{
  PackedColor c0,c1;
};

TypeIsSimple(Line2DPixelInfo)

//! uses same coordinate system as Point2DPixel and Rect2DPixel
struct Vertex2DPixel: Point2DPixel
{
  float z,w; // screen coordinates
  float u,v; // texture coordinates
  PackedColor color; // color

  Vertex2DPixel(){z = 0.5f,w = 1.0f;}
};

TypeIsSimple(Vertex2DPixel)

extern Rect2DPixel Rect2DClipPixel;

class TexMaterial;

//! position of point on screen in 2D viewport coordinates
/*!
Insideviewport range: x = <0,1), y = <0,1)
*/
struct Point2DFloat
{
  float x,y;
  Point2DFloat(){}
  Point2DFloat(float xx, float yy):x(xx),y(yy){}
  
  Point2DFloat operator + (const Point2DFloat &b) const {return Point2DFloat(x+b.x,y+b.y);}
  Point2DFloat operator - (const Point2DFloat &b) const {return Point2DFloat(x-b.x,y-b.y);}
  Point2DFloat operator *(float f) const {return Point2DFloat(x*f,y*f);}
  Point2DFloat Max(const Point2DFloat &b) const
  {
    return Point2DFloat(floatMax(x,b.x),floatMax(y,b.y));
  }
  Point2DFloat Min(const Point2DFloat &b) const
  {
    return Point2DFloat(floatMin(x,b.x),floatMin(y,b.y));
  }
};
//! position of rectangle on screen in 2D viewport coordinates
struct Rect2DFloat
{
  float x,y,w,h; // rectangle
  Rect2DFloat(){}
  Rect2DFloat( float xx, float yy, float ww, float hh ){x=xx,y=yy,w=ww,h=hh;}
};

struct Line2DFloat
{
  Point2DFloat beg,end;
  Line2DFloat(){}
  Line2DFloat( float x0, float y0, float x1, float y1){beg.x=x0,beg.y=y0,end.x=x1,end.y=y1;}
};

struct Line2DFloatInfo: public Line2DFloat
{
  PackedColor c0, c1;
};

TypeIsSimple(Line2DFloatInfo)

//! uses same coordinate system as Point2DPixel and Rect2DPixel
struct Vertex2DFloat: Point2DFloat
{
  float z,w; // screen coordinates
  float u,v; // texture coordinates
  PackedColor color; // color

  Vertex2DFloat()
  {
    z = 0.5f; 
    w = 1.0f;
#if _VBS3
    //!{ Don't leave anything uninitialized, floating point exception traps may occur during interpolation then
    u = 0.0f;
    v = 0.0f;
    color = PackedBlack;
    //!}
#endif
  }
};

TypeIsSimple(Vertex2DFloat)

//! default clipping rectangle
extern Rect2DFloat Rect2DClipFloat;

//@}

//! Shadow quality designed to enable shadow buffers
#define SB_SQ 3

//! Shading quality designed to enable DOF effect
#define DOF_SHQ 50

#if _HELISIM_RENDER_TO_TEXTURE

#define R2T_ENUM(type,prefix,XX) \
  XX(type, prefix, RenderTarget0) \
  XX(type, prefix, RenderTarget1) \
  XX(type, prefix, RenderTarget2) \
  XX(type, prefix, RenderTarget3) \
  XX(type, prefix, RenderTarget4) \
  XX(type, prefix, RenderTarget5) \
  XX(type, prefix, RenderTarget6) \
  XX(type, prefix, RenderTarget7) \

enum RenderTargetID
{
  R2T_ENUM(RenderTargetID, R2T, ENUM_VALUE)
  R2TRenderTargetCount,
  R2TMinResolution = 32,
  R2TMaxResolution = 512,
  R2TUninitialized = -1
};

#endif

//! Pixel shader ID factory
/*!
!!! Don't add new pixel shaders in the middle of the list
(binarization of all data would be required otherwise)
*/
#define PS_ENUM(type,prefix,XX) \
  XX(type, prefix, Normal)                              /*diffuse color modulate, alpha replicate*/\
  XX(type, prefix, NormalDXTA)                          /*diffuse color modulate, alpha replicate, DXT alpha correction*/\
  XX(type, prefix, NormalMap)                           /*normal map shader*/\
  XX(type, prefix, NormalMapThrough)                    /*normal map shader - through lighting*/\
  XX(type, prefix, NormalMapGrass)                      /*normal map shader - through lighting*/\
  XX(type, prefix, NormalMapDiffuse)                    /**/\
  XX(type, prefix, Detail)                              /*detail texturing*/\
  XX(type, prefix, Interpolation)                       /**/\
  XX(type, prefix, Water)                               /*sea water*/\
  XX(type, prefix, WaterSimple)                         /*small water*/\
  XX(type, prefix, White)                               /**/\
  XX(type, prefix, WhiteAlpha)                          /**/\
  XX(type, prefix, AlphaShadow)                         /*shadow alpha write*/\
  XX(type, prefix, AlphaNoShadow)                       /*shadow alpha (no shadow) write*/\
  XX(type, prefix, Dummy0)                              /**/\
  XX(type, prefix, DetailMacroAS)                       /*detail with ambient shadow texture*/\
  XX(type, prefix, NormalMapMacroAS)                    /*normal map with ambient shadow texture*/\
  XX(type, prefix, NormalMapDiffuseMacroAS)             /*diffuse normal map with ambient shadow texture*/\
  XX(type, prefix, NormalMapSpecularMap)                /*normal map with specular map*/\
  XX(type, prefix, NormalMapDetailSpecularMap)          /*normal map with detail and specular map*/\
  XX(type, prefix, NormalMapMacroASSpecularMap)         /*normal map with ambient shadow and specular map*/\
  XX(type, prefix, NormalMapDetailMacroASSpecularMap)   /*normal map with detail and ambient shadow and specular map*/\
  XX(type, prefix, NormalMapSpecularDIMap)              /*normal map with specular map, diffuse is inverse of specular*/\
  XX(type, prefix, NormalMapDetailSpecularDIMap)        /*normal map with detail and specular map, diffuse is inverse of specular*/\
  XX(type, prefix, NormalMapMacroASSpecularDIMap)       /*normal map with ambient shadow and specular map, diffuse is inverse of specular*/\
  XX(type, prefix, NormalMapDetailMacroASSpecularDIMap) /*normal map with detail and ambient shadow and specular map, diffuse is inverse of specular*/\
  XX(type, prefix, Terrain1)                            /*terrain - X layers*/\
  XX(type, prefix, Terrain2)                            /*terrain - X layers*/\
  XX(type, prefix, Terrain3)                            /*terrain - X layers*/\
  XX(type, prefix, Terrain4)                            /*terrain - X layers*/\
  XX(type, prefix, Terrain5)                            /*terrain - X layers*/\
  XX(type, prefix, Terrain6)                            /*terrain - X layers*/\
  XX(type, prefix, Terrain7)                            /*terrain - X layers*/\
  XX(type, prefix, Terrain8)                            /*terrain - X layers*/\
  XX(type, prefix, Terrain9)                            /*terrain - X layers*/\
  XX(type, prefix, Terrain10)                           /*terrain - X layers*/\
  XX(type, prefix, Terrain11)                           /*terrain - X layers*/\
  XX(type, prefix, Terrain12)                           /*terrain - X layers*/\
  XX(type, prefix, Terrain13)                           /*terrain - X layers*/\
  XX(type, prefix, Terrain14)                           /*terrain - X layers*/\
  XX(type, prefix, Terrain15)                           /*terrain - X layers*/\
  XX(type, prefix, TerrainSimple1)                      /*terrainSimple - X layers*/\
  XX(type, prefix, TerrainSimple2)                      /*terrainSimple - X layers*/\
  XX(type, prefix, TerrainSimple3)                      /*terrainSimple - X layers*/\
  XX(type, prefix, TerrainSimple4)                      /*terrainSimple - X layers*/\
  XX(type, prefix, TerrainSimple5)                      /*terrainSimple - X layers*/\
  XX(type, prefix, TerrainSimple6)                      /*terrainSimple - X layers*/\
  XX(type, prefix, TerrainSimple7)                      /*terrainSimple - X layers*/\
  XX(type, prefix, TerrainSimple8)                      /*terrainSimple - X layers*/\
  XX(type, prefix, TerrainSimple9)                      /*terrainSimple - X layers*/\
  XX(type, prefix, TerrainSimple10)                     /*terrainSimple - X layers*/\
  XX(type, prefix, TerrainSimple11)                     /*terrainSimple - X layers*/\
  XX(type, prefix, TerrainSimple12)                     /*terrainSimple - X layers*/\
  XX(type, prefix, TerrainSimple13)                     /*terrainSimple - X layers*/\
  XX(type, prefix, TerrainSimple14)                     /*terrainSimple - X layers*/\
  XX(type, prefix, TerrainSimple15)                     /*terrainSimple - X layers*/\
  XX(type, prefix, Glass)                               /*glass shader with environmental map*/\
  XX(type, prefix, NonTL)                               /*very simple 2D pixel shader*/\
  XX(type, prefix, NormalMapSpecularThrough)            /*normal map shader - through with specular lighting*/\
  XX(type, prefix, Grass)                               /*grass shader - alpha discretized*/\
  XX(type, prefix, NormalMapThroughSimple)              /*simple version of NormalMapThrough shader*/\
  XX(type, prefix, NormalMapSpecularThroughSimple)      /*simple version of NormalMapSpecularThrough shader*/\
  XX(type, prefix, Road)                                /*road shader*/\
  XX(type, prefix, Shore)                               /*shore shader*/\
  XX(type, prefix, ShoreWet)                            /*shore shader for the wet part*/\
  XX(type, prefix, Road2Pass)                           /*road shader - second pass*/\
  XX(type, prefix, ShoreFoam)                           /*shore shader for the foam on the top of the shore*/\
  XX(type, prefix, NonTLFlare)                          /*shader to be used for flares*/\
  XX(type, prefix, NormalMapThroughLowEnd)              /*substitute shader for NormalMapThrough shaders for low-end settings*/\
  XX(type, prefix, TerrainGrass1)                       /*terrain grass - X layers*/\
  XX(type, prefix, TerrainGrass2)                       /*terrain grass - X layers*/\
  XX(type, prefix, TerrainGrass3)                       /*terrain grass - X layers*/\
  XX(type, prefix, TerrainGrass4)                       /*terrain grass - X layers*/\
  XX(type, prefix, TerrainGrass5)                       /*terrain grass - X layers*/\
  XX(type, prefix, TerrainGrass6)                       /*terrain grass - X layers*/\
  XX(type, prefix, TerrainGrass7)                       /*terrain grass - X layers*/\
  XX(type, prefix, TerrainGrass8)                       /*terrain grass - X layers*/\
  XX(type, prefix, TerrainGrass9)                       /*terrain grass - X layers*/\
  XX(type, prefix, TerrainGrass10)                      /*terrain grass - X layers*/\
  XX(type, prefix, TerrainGrass11)                      /*terrain grass - X layers*/\
  XX(type, prefix, TerrainGrass12)                      /*terrain grass - X layers*/\
  XX(type, prefix, TerrainGrass13)                      /*terrain grass - X layers*/\
  XX(type, prefix, TerrainGrass14)                      /*terrain grass - X layers*/\
  XX(type, prefix, TerrainGrass15)                      /*terrain grass - X layers*/\
  XX(type, prefix, Crater1)                             /*Crater rendering - X craters*/\
  XX(type, prefix, Crater2)                             /*Crater rendering - X craters*/\
  XX(type, prefix, Crater3)                             /*Crater rendering - X craters*/\
  XX(type, prefix, Crater4)                             /*Crater rendering - X craters*/\
  XX(type, prefix, Crater5)                             /*Crater rendering - X craters*/\
  XX(type, prefix, Crater6)                             /*Crater rendering - X craters*/\
  XX(type, prefix, Crater7)                             /*Crater rendering - X craters*/\
  XX(type, prefix, Crater8)                             /*Crater rendering - X craters*/\
  XX(type, prefix, Crater9)                             /*Crater rendering - X craters*/\
  XX(type, prefix, Crater10)                            /*Crater rendering - X craters*/\
  XX(type, prefix, Crater11)                            /*Crater rendering - X craters*/\
  XX(type, prefix, Crater12)                            /*Crater rendering - X craters*/\
  XX(type, prefix, Crater13)                            /*Crater rendering - X craters*/\
  XX(type, prefix, Crater14)                            /*Crater rendering - X craters*/\
  XX(type, prefix, Sprite)                              /*Shader used for sprite rendering - it uses SoftParticle approach*/\
  XX(type, prefix, SpriteSimple)                        /*Shader used for sprite rendering - no SoftParticle approach*/\
  XX(type, prefix, Cloud)                               /*Shader used for clouds*/\
  XX(type, prefix, Horizon)                             /*Shader used for the horizon*/\
  XX(type, prefix, Super)                               /*Super shader*/\
  XX(type, prefix, Multi)                               /*Multi shader*/\
  XX(type, prefix, TerrainX)                            /*terrain - general number of layers*/\
  XX(type, prefix, TerrainSimpleX)                      /*terrainSimple - general number of layers*/\
  XX(type, prefix, TerrainGrassX)                       /*terrain grass - general number of layers*/\
  XX(type, prefix, Tree)                                /*Tree shader*/\
  XX(type, prefix, TreePRT)                             /*Tree shader - very cheap shader with PRT*/\
  XX(type, prefix, TreeSimple)                          /*Tree shader - simpler version of Tree*/\
  XX(type, prefix, Skin)                                /*Human skin - derived from Super shader*/\
  XX(type, prefix, CalmWater)                           /*calm water surface*/\
  XX(type, prefix, TreeAToC)                            /*tree with alpha to coverage*/\
  XX(type, prefix, GrassAToC)                           /*grass with alpha to coverage*/\
  XX(type, prefix, TreeAdv)                             /*advanced tree crown shader*/\
  XX(type, prefix, TreeAdvSimple)                       /*advanced tree crown shader*/\
  XX(type, prefix, TreeAdvTrunk)                        /*advanced tree shader*/\
  XX(type, prefix, TreeAdvTrunkSimple)                  /*advanced tree shader*/\
  XX(type, prefix, TreeAdvAToC)                         /*advanced tree crown shader*/\
  XX(type, prefix, TreeAdvSimpleAToC)                   /*advanced tree crown shader*/\
  XX(type, prefix, TreeSN)                              /*Tree shader width simple noise*/\
  XX(type, prefix, SpriteExtTi)                         /*Sprite used for vehicles covering*/\
  XX(type, prefix, TerrainSNX)                          /*terrain - general number of layers + satellite normal map*/\
  XX(type, prefix, SimulWeatherClouds)                  /** SimulWeather clouds */ \
  XX(type, prefix, SimulWeatherCloudsWithLightning)     /** SimulWeather clouds with lightning */ \
  XX(type, prefix, SimulWeatherCloudsCPU)               /** SimulWeather clouds with CPU distance fading */ \
  XX(type, prefix, SimulWeatherCloudsWithLightningCPU)  /** SimulWeather clouds with lightning and CPU distance fading */ \
  XX(type, prefix, SuperExt)                            /* skyscraper & building, intended as super shader light version */\
  XX(type, prefix, SuperAToC)                           /* Super shader AToC variant */\
	XX(type, prefix, NonTLHeatHaze)                       /* Heat haze effect */\

#define CASE_TERRAIN(baseName) \
  case baseName##1: case baseName##2: case baseName##3: \
  case baseName##4: case baseName##5: case baseName##6: case baseName##7: \
  case baseName##8: case baseName##9: case baseName##10: case baseName##11: \
  case baseName##12: case baseName##13: case baseName##14: case baseName##15: \
  case baseName##X

enum PixelShaderID
{
  PS_ENUM(PixelShaderID, PS, ENUM_VALUE)
  NPixelShaderID,
  PSNone=NPixelShaderID,
  PSUninitialized = -1
};
template <>
const EnumName *GetEnumNames(PixelShaderID dummy);

//! Index of texture stage the TI texture will share texture coordinates with
extern const int TITexSource[];

//@{ Items of vertex declaration mask
#define VDI_POSITION               1
#define VDI_NORMAL                 2
#define VDI_TEX0                   4
#define VDI_TEX1                   8
#define VDI_WEIGHTSANDINDICES     16
#define VDI_ST                    32
#define VDI_SHADOWVOLUME          64
#define VDI_SHADOWVOLUMESKINNED  128
#define VDI_COLOR                256
#define VDI_SIMULWEATHERCLOUDS   512
#define VDI_SIMULWEATHERCLOUDS_CPU   1024
//#define VDI_ALPHA                512
/// any 3 float values
#define VDI_FLOAT3               2048
//@}

//! Mask of the vertex declaration
extern const int VertexDeclarations[];

//! Vertex shader ID factory
/*!
!!! Don't add new vertex shaders in the middle of the list
(binarization of all data would be required otherwise)
*/
#define VS_ENUM(type,prefix,XX) \
  XX(type, prefix, Basic)                           /*no extra info*/\
  XX(type, prefix, NormalMap)                       /*normal map*/\
  XX(type, prefix, NormalMapDiffuse)                /*normal map + detail map*/\
  XX(type, prefix, Grass)                           /**/\
  XX(type, prefix, Dummy1)                          /**/\
  XX(type, prefix, Dummy2)                          /**/\
  XX(type, prefix, ShadowVolume)                    /*shadow volumes*/\
  XX(type, prefix, Water)                           /*per-vertex water animation*/\
  XX(type, prefix, WaterSimple)                     /*per-vertex water animation (without foam)*/\
  XX(type, prefix, Sprite)                          /*particle effects*/\
  XX(type, prefix, Point)                           /*anti-aliased points*/\
  XX(type, prefix, NormalMapThrough)                /*normal map - tree shader*/\
  XX(type, prefix, Dummy3)                          /**/\
  XX(type, prefix, Terrain)                         /*one pass terrain, no alpha mask - based on VSNormalMapDiffuse*/\
  XX(type, prefix, BasicAS)                         /*ambient shadow*/\
  XX(type, prefix, NormalMapAS)                     /*normal map with ambient shadow*/\
  XX(type, prefix, NormalMapDiffuseAS)              /*diffuse normal map with ambient shadow*/\
  XX(type, prefix, Glass)                           /*glass shader*/ \
  XX(type, prefix, NormalMapSpecularThrough)        /*normal map with specular - tree shader*/\
  XX(type, prefix, NormalMapThroughNoFade)          /*normal map - tree shader - without face fading*/\
  XX(type, prefix, NormalMapSpecularThroughNoFade)  /*normal map with specular - tree shader - without face fading*/\
  XX(type, prefix, Shore)                           /*sea shore - similar to Terrain*/\
  XX(type, prefix, TerrainGrass)                    /*grass layer - similar to Terrain*/\
  XX(type, prefix, Super)                           /*Super shader - expensive shader containing all common features*/\
  XX(type, prefix, Multi)                           /*Multi shader - shader with multiple layers suitable for huge surfaces like houses*/\
  XX(type, prefix, Tree)                            /*Tree shader - cheap shader designed for trees and bushes*/\
  XX(type, prefix, TreeNoFade)                      /*Tree shader - cheap shader designed for trees and bushes - without face fading*/\
  XX(type, prefix, TreePRT)                         /*Tree shader - very cheap shader designed for trees and bushes*/\
  XX(type, prefix, TreePRTNoFade)                   /*Tree shader - very cheap shader designed for trees and bushes - without face fading*/\
  XX(type, prefix, Skin)                            /*Human skin - derived from Super shader*/\
  XX(type, prefix, CalmWater)                       /*calm water surface - special shader*/\
  XX(type, prefix, TreeAdv)                         /*advanced tree crown shader  VSTreeAdv*/\
  XX(type, prefix, TreeAdvTrunk)                    /*advanced tree crown shader  VSTreeAdvTrunk*/\
  XX(type, prefix, SimulWeatherClouds)              /*simul weather clouds*/\
  XX(type, prefix, SimulWeatherCloudsCPU)           /*simul weather clouds with CPU distance fading*/\

//! VertexShaderID enum
#ifndef DECL_ENUM_VERTEX_SHADER_ID
#define DECL_ENUM_VERTEX_SHADER_ID
DECL_ENUM(VertexShaderID)
#endif
DECLARE_ENUM(VertexShaderID, VS, VS_ENUM)

//! Declarations associated with the vertex shader ID
extern const EVertexDecl ShaderVertexDeclarations[];

//////////////////////////////////////////////////////////////////////////

//! List of shaders placed in the pool of common shaders
#define VSP_ENUM(type,prefix,XX) \
  XX(type, prefix, Basic) \
  XX(type, prefix, NormalMap) \
  XX(type, prefix, NormalMapDiffuse) \
  XX(type, prefix, Grass) \
  XX(type, prefix, NormalMapThrough) \
  XX(type, prefix, BasicAS) \
  XX(type, prefix, NormalMapAS) \
  XX(type, prefix, NormalMapDiffuseAS) \
  XX(type, prefix, Glass) \
  XX(type, prefix, NormalMapSpecularThrough) \
  XX(type, prefix, NormalMapThroughNoFade) \
  XX(type, prefix, NormalMapSpecularThroughNoFade) \
  XX(type, prefix, Super) \
  XX(type, prefix, Multi) \
  XX(type, prefix, Tree) \
  XX(type, prefix, TreeNoFade) \
  XX(type, prefix, TreePRT) \
  XX(type, prefix, TreePRTNoFade) \
  XX(type, prefix, Skin) \
  XX(type, prefix, TreeAdv) \
  XX(type, prefix, TreeAdvTrunk) \

//! Create enum and array of the same names
#ifndef DECL_ENUM_VSP
#define DECL_ENUM_VSP
DECL_ENUM(VertexShaderPoolID)
#endif
DECLARE_ENUM(VertexShaderPoolID, VSP, VSP_ENUM)

//! Array to convert VS into VSP
extern const VertexShaderPoolID VSToVSP[];

//////////////////////////////////////////////////////////////////////////

//! List of special shaders
#define VSS_ENUM(type,prefix,XX) \
  XX(type, prefix, Water) \
  XX(type, prefix, WaterSimple) \
  XX(type, prefix, Sprite) \
  XX(type, prefix, Shore) \
  XX(type, prefix, Point) \
  XX(type, prefix, CalmWater) \
  XX(type, prefix, SimulWeatherClouds) \
  XX(type, prefix, SimulWeatherCloudsCPU) \

//   XX(type, prefix, Point) \

//! Create enum and array of the same names
#ifndef DECL_ENUM_VSS
#define DECL_ENUM_VSS
DECL_ENUM(VertexShaderSpecialID)
#endif
DECLARE_ENUM(VertexShaderSpecialID, VSS, VSS_ENUM)

//! Array to convert VS into VSS
extern const VertexShaderSpecialID VSToVSS[];

//////////////////////////////////////////////////////////////////////////

//! UVSource factory
#define UV_ENUM(type,prefix,XX) \
  XX(type, prefix, None) \
  XX(type, prefix, Tex) \
  XX(type, prefix, TexWaterAnim) \
  XX(type, prefix, Pos) \
  XX(type, prefix, Norm) \
  XX(type, prefix, Tex1) \
  XX(type, prefix, WorldPos) \
  XX(type, prefix, WorldNorm) \
  XX(type, prefix, TexShoreAnim) \

//! UVSource enum
#ifndef DECL_ENUM_UV_SOURCE
#define DECL_ENUM_UV_SOURCE
DECL_ENUM(UVSource)
#endif
DECLARE_ENUM(UVSource,UV,UV_ENUM)

#define UV_STAGES 2

//! Class to store and access AOT coefficients for each UV stage
class AreaOverTex
{
  float _areaOverTex[UV_STAGES];
public:
  AreaOverTex() {Init();}
  void Init() {for (int i = 0; i < UV_STAGES; i++) _areaOverTex[i] = -1000.0f;}
  __forceinline float Get(int uvStage) const {Assert(uvStage >= 0); Assert(uvStage < UV_STAGES); return _areaOverTex[uvStage];}
  __forceinline void Set(int uvStage, float areaOverTex) {Assert(uvStage >= 0); Assert(uvStage < UV_STAGES); _areaOverTex[uvStage] = areaOverTex;}
  
  void Min(const AreaOverTex &aot)
  {
    saturateMax(_areaOverTex[0],aot._areaOverTex[0]);
    saturateMax(_areaOverTex[1],aot._areaOverTex[1]);
  }
};

//! Type of main enlightement
enum EMainLight
{
  ML_None,
  ML_Sun,
  ML_Sky,
  ML_Horizon,
  ML_Stars,
  ML_SunObject,
  ML_SunHaloObject,
  ML_MoonObject,
  ML_MoonHaloObject,
};

/// Type of the fog
enum EFogMode
{
  /// No fog is being used
  FM_None,
  /// Ordinary fog
  FM_Fog,
  /// Fog as alpha
  FM_Alpha,
  /// Fog as both alpha and fog
  FM_FogAlpha
};

//! Type of skinning
enum ESkinningType
{
  STUninitialized = -1,
  STNone = 0,
  STPoint1 = 1,
  STPoint2 = 2,
  STPoint3 = 3,
  STPoint4 = 4
};

class SerializeBinStream;
class VertexBuffer;

//! text drawing attributes
struct TextDrawAttr
{
  float _size;
  Font *_font;
  PackedColor _color;
  bool _fixedWidth;
  /// reference only - the referenced material must keep existing
  const TLMaterial &_mat;
  int _specFlags;
  int _shadow;

  TextDrawAttr(
    float size=0, Font *font=NULL, PackedColor color=PackedBlack,
    int shadow = 0, bool fixedWidth=false,
    const TLMaterial &mat = TLMaterial::_default,
    int specFlags = 0
  ):_mat(mat)
  {
    _size = size;
    _color = color;
    _font = font;
    _fixedWidth = fixedWidth;
    _specFlags = specFlags;
    _shadow = shadow;
  }
};

/// visibility query interface
class IVisibilityQuery: public RefCount
{
  public:
  /// check query result
  virtual bool IsVisible() const = 0;
  /// check how many frames is this information old
  virtual int GetLastLatency() const = 0;
  // if possible, we want to know how many pixels are visible
  virtual int GetVisiblePixels(bool *error = NULL) const = 0;
};

class FrameBase;

struct ObjectInstanceInfo;

typedef StaticArrayAuto<Matrix4> Matrix4Array;

/// handle for video playback
struct VideoPlayback {}; 

class LightList;
class EngineShapeProperties;

struct FogParams
{
  float expCoef;
  float start;
  float end;
};

/// interface to get various global properties from a scene
class EngineSceneProperties
{
public:
  virtual void GetSeaWaveMatrices(Matrix4 &matrix0, Matrix4 &matrix1, Matrix4 &matrix2, Matrix4 &matrix3, const EngineShapeProperties &prop) const {}
  virtual void GetSeaWavePars(float &waveXScale, float &waveZScale, float &waveHeight) const
  {
    waveXScale = 0.02f, waveZScale = 0.02f, waveHeight = 0.50f;
  }
  virtual Vector3Val GetCameraPosition() const {return VZero;}
  virtual Matrix4Val GetCameraProjection() const {return MIdentity;}

  virtual void GetTerrainLODPars(float pars[4]) const
  {
    pars[0] = pars[1] = pars[2] = pars[3] = 0;
  }
  virtual void GetGrassPars(float pars[8]) const
  {
    pars[0] = pars[1] = pars[2] = pars[3] = 0;
    pars[4] = pars[5] = pars[6] = pars[7] = 0;
  }
  //! Sea level f.i. for shore drawing
  virtual float GetSeaLevel() const {return 0.0f;}
  /// get sky parameters for sky rendering  
  virtual void GetSkyColor(Color &sky, Color &aroundSun) const {sky=HWhite;aroundSun=HWhite;}
  // satellite to landGrid scale
  virtual float GetSatScale() const {return 1.0f;}
  virtual FogParams ComputeFogParams( float farFog ) const {FogParams pars;pars.expCoef=-0.00062383;pars.start=0;pars.end=farFog;return pars;};
  /// default instance
  static EngineSceneProperties _default;
};

enum ZSpaceType
{
  ZSpaceNormal,
  ZSpace1stPerson,
  ZSpaceUI,
};

enum DefTIMode
{
  TIDefault,
  TIMan,
};


//! Structure to hold the drawing parameters
struct DrawParameters
{
  //! Flag to determine we render the shadow buffer now
  bool _sbIsBeingRendered;
  //! Flag to determine we do the depth priming now (including depth rendering for SSSM purposes)
  bool _zprimeIsBeingRendered;
  /// shadow receiving possible
  bool _canReceiveShadows;
  /// position is given in camera space, not in world space as usual
  bool _cameraSpacePos;
  //! Flag to determine we're in a pass 3 (cockpit drawing)
  ZSpaceType _zSpace;
  //! Flag to determine the number of on-model craters to be drawn
  int _cratersCount;
#if _VBS3_CRATERS_BIAS
  //! Bias to be used when drawing (non zero usually for crater models)
  int _bias;
#endif

  //!{ TI properties
  float _htMin; // Minimum half time
  float _htMax; // Maximum half time
  float _afMax; // Maximum alive factor
  float _mfMax; // Maximum movement factor
  float _mFact; // Metabolism factor
  float _tBody; // Body temperature
  DefTIMode _defTI; // default TI texture type
  bool _useOwnHeatSource;       // Flag to determine we're going to use the mode's own heat source (false means we're going to use the global heat source - sun direction)
  Vector3 _heatSourceDirection; // Direction of the model own heat source
  //!}
  DrawParameters()
  {
    _sbIsBeingRendered = false;
    _zprimeIsBeingRendered = false;
    _canReceiveShadows = true;
    _cameraSpacePos = false;
    _zSpace = ZSpaceNormal;
    _cratersCount = 0;
#if _VBS3_CRATERS_BIAS
    _bias = 0;
#endif
    InitTI();
  }
  //! Constructor
  DrawParameters(
    bool sbIsBeingRendered, bool zprimeIsBeingRendered, bool canReceiveShadows,
    bool cameraSpacePos, ZSpaceType zSpace, int cratersCount
  )
  {
    _sbIsBeingRendered = sbIsBeingRendered;
    _zprimeIsBeingRendered = zprimeIsBeingRendered;
    _canReceiveShadows = canReceiveShadows;
    _cameraSpacePos = cameraSpacePos;
    _zSpace = zSpace;
    _cratersCount = cratersCount;
#if _VBS3_CRATERS_BIAS
    _bias = 0;
#endif
    InitTI();
  }
  //! Determine the z-space is being used
  ZSpaceType ZSpace() const
  {
    // Use split z-space in case of pass3
    return _zSpace;
  }

  //! Initialize the TI to default values
  void InitTI()
  {
    _htMin = 1.0f;
    _htMax = 60.0f * 60.0f * 24.0f;
    _afMax = 0.0f;
    _mfMax = 0.0f;
    _mFact = 0.0f;
    _tBody = 0.0f;
    _useOwnHeatSource = false;
    _heatSourceDirection = VZero;
  }
  
  /// default static value  
  static const DrawParameters _default;
  /// default value, shadow receiving disabled
  static const DrawParameters _noShadow;
};

//! Structure to hold parameters to be propagated in all virtual Draw functions.
//! In instanced version these parameters are to be propagated per instance.
struct InstanceParameters
{
  //! Number of pixels the instance covers
  float _coveredArea;
  //! Constructor
  InstanceParameters()
  {
    _coveredArea = -1;
  }
  //! Constructor
  InstanceParameters(float coveredArea)
  {
    _coveredArea = coveredArea;
  }
  //! Default static value  
  static const InstanceParameters _default;
};

const int ShadowBias = -28;

//! Number of layers of the SB cascade
const int ShadowBufferCascadeLayers = 4;

#ifdef TEST_RAIN_MAP
	const int RAIN_MAP_FAKE_LAYER = ShadowBufferCascadeLayers;
	const int RAIN_MAP_SB_SIZE		= 256;
#endif


/// Size ratio between two following shadow cascades
/** 2.0 seems like a natural choice, but actually it can be any float number */
const float ShadowBufferCascadeRatio = 3.0f;

enum AbilityToDraw
{
  ATDUnable,
  ATDAble,
  ATDAbleReset
};

//! Structure to encapsulate material and selected LOD
class TexMaterialLODInfo
{
public:
  const TexMaterial *_mat;
  DefTIMode _defTI;

  // store UV offset taken from AnimationContext
  float _offsetU, _offsetV;
  bool _UVchanged;
  TexMaterialLODInfo()
  {
    _mat = NULL;
    _UVchanged = false;
    _defTI = TIDefault;
  }
  TexMaterialLODInfo(const TexMaterial *mat)
  {
    _mat = mat;
    _UVchanged = false;
    _defTI = TIDefault;
  };
  TexMaterialLODInfo(const TexMaterial *mat, bool UVchanged, float offsetU, float offsetV, DefTIMode defTI)
  {
    _mat = mat;
    _offsetU = offsetU;
    _offsetV = offsetV;
    _UVchanged = UVchanged;
    _defTI = defTI;
  };
  //!{ Operators
  __forceinline bool operator== (const TexMaterialLODInfo &sec) const {return _mat == sec._mat && _UVchanged == sec._UVchanged && (!_UVchanged || (_offsetU == sec._offsetU && _offsetV == sec._offsetV)) && _defTI==sec._defTI;}
  __forceinline bool operator!= (const TexMaterialLODInfo &sec) const {return !operator ==(sec);}
  //!}
};

class PolyProperties;

//! Description of a single crater
struct CraterProperties
{
  //! Position of the crater in model space, not skinned
  Vector3 _pos;
  //! Square of the 
  float _invRadius2;
  //! Time when when place of impact is completely cool down
  int _time;
};
TypeIsSimple(CraterProperties);

#if !_ENABLE_CHEATS
  #define SHOW_FPS_ENUM(type,prefix,XX) \
	  XX(type, prefix, None) \
	  XX(type, prefix, Basic) \
	  
#else
  #define SHOW_FPS_ENUM(type,prefix,XX) \
    XX(type, prefix, None) \
    XX(type, prefix, Basic) \
    XX(type, prefix, PerfScope) \
    XX(type, prefix, CPU) \
    XX(type, prefix, PerfCount) \
    XX(type, prefix, DPrim) \
    XX(type, prefix, Tris) \
    XX(type, prefix, Objs) \
    XX(type, prefix, Pixs) \
    XX(type, prefix, Mouse) \
    XX(type, prefix, TexMem) \
    XX(type, prefix, MemStats) \
    XX(type, prefix, DiscardableStats) \

#endif

class ParamArchive;
class SortObject;
struct PositionRender;

//! ShowFps enum
#ifndef DECL_ENUM_SHOWFPS_ID
#define DECL_ENUM_SHOWFPS_ID
DECL_ENUM(ShowFps)
#endif
DECLARE_ENUM(ShowFps,Fps,SHOW_FPS_ENUM)

/** Min for common rendering must not be smaller than DepthBorderPass3Max.
we keep it a little big bigger to make sure there is a gap between the intervals
*/
extern const float DepthBorderCommonMin;
extern const float DepthBorderCommonMax;

/// various postprocess effects controls
struct PostFxSettings 
{
  unsigned rotBlur:1;
};

#include "diagGraph.hpp"

#if _PROFILE
//# define TRAP_FOR_0x8000
#endif
class StatisticsByName;
class HeatHazeRenderArray;

// post effect types managed from scripts
enum PostEffectType { PENone, PERadialBlur, PEChromAberration, PEWetDistortion, PEColorCorrections, PEDynamicBlur, PESSAO, PEFilmGrain, PEGodRays, PERain, PEColorInv };

/// rendering/drawing interface
class Engine
{
public:
  enum {NFrameDurations=64};
protected:
  int _messageHandle;
  int _textHandle;

  Color _fogColor;
  Color _skyTopColor;
  Color _accomodateEye; // color filter including eye accommodation
  float _usrBrightness; // user brightness control
  //@{ amb/dif  values used for stencil shadow visualization
  Color _shadowLA;
  Color _shadowLD; 
  float _shadowFactor;
  //@}

  /// additional light gain, e.g. by night vision goggles
  Color _lightGain;
  
  /// accommodation factor
  float _accomFactor;
  /// sometimes adaptation factor is fixed and we want to avoid GPU updating it
  bool _accomFactorFixed;
  
#if _ENABLE_CHEATS
  #define ENABLE_NV 1
  #define ENABLE_TI 1
#else
  #define ENABLE_NV 0
  #define ENABLE_TI 0
#endif

#if ENABLE_NV
  bool _nightVision;
#else
  static const bool _nightVision = false;
#endif

#if ENABLE_TI
  //! Flag to determine the thermal vision device is being used
  bool _thermalVision;
#else
  static const bool _thermalVision = false;
#endif

  /// fps / stats mode
  int _showFps;
  #if _ENABLE_CHEATS
    /// enable paging up/down through the stats
    int _statPageStart;
  #endif
  
  AspectSettings _aspectSettings;

  //@{ diagnostics text drawing
  Ref<Font> _showTextFont;
  PackedColor _showTextColor;
  float _showTextSize;

  AutoArray<TextInfo> _texts;
  
  /// all diagnostics graphs to display
  FindArrayKey< Ref<IDiagGraph> > _diagGraphs;

  Ref<IDiagGraph> _fpsGraph;
  
  EngineSceneProperties *_sceneProps;
  
  FontCache _fonts;
  Ref<Font> _debugFont; // debug font needs to be present in retail version as well - cheats
  float _debugFontSize;

  DWORD _frameTime; // last frame stats
  DWORD _startTime;
  DWORD _lastFrameDuration; // duration of last frame (in ms)
  DWORD _startGame; // time the game started

  DWORD _frameDurations[NFrameDurations];
  DWORD _frameTriangles[NFrameDurations];

  int _monitorOffsetX;
  int _monitorOffsetY;

#if _ARMA3_HDR_IMPROVEMENTS
  /// coef that is used to multiply the size of the image to determine rectangle, which hdr luminance is computed from
  float _hdrLuminanceRectCoef;
  /// coef used to multiply the desired luminance midpoint
  float _desiredLuminanceCoef;
#endif

public:
  void ToggleFps( int state );
  
  /// scroll stats up/down
  void StatsPage(int pages);

  /// called every time mission is started / restarted
  virtual void FlushMemory(bool deep, bool allowAutoDeep=true);
  virtual void RequestFlushMemory();
  
  DWORD GetLastFrameDuration() const {return _lastFrameDuration;}
  /// get average frame - in ms
  float GetAvgFrameDuration( int nFrames=8 ) const;
  /// get slower frame - in ms
  DWORD GetMaxFrameDuration( int nFrames=8 ) const;
  DWORD GetMinFrameDuration( int nFrames=8 ) const;

  static float GetMinFrameDurationSmooth( const DWORD (&frameDurations)[NFrameDurations], int nFrames=INT_MAX );

  /// get fastest frame, but smooth (older frames less significant)
  float GetMinFrameDurationSmooth( int nFrames=INT_MAX ) const
  {
    return GetMinFrameDurationSmooth(_frameDurations,nFrames);
  }

  DWORD GetTimeStartGame() const {return _startGame;}
  void SetTimeStartGame( DWORD time ) {_startGame=time;}
  void ResetFrameDuration();

  //@{ get timing  suitable for animation
  virtual float GetSmoothFrameDuration() const;
  virtual float GetSmoothFrameDurationMax() const;
  //#}

  /// get average frame duration as a number of vsyncs
  virtual float GetAvgFrameDurationVsync() const;
  /// get slowest frame - as a number of vsyncs
  virtual int GetMaxFrameDurationVsync() const;
  
  // get last known average intensity obtained by image space measurements
  /**
  @param accomFactor return what accomodation was attached with it
  @return value <0 means no valid value is known
  */
  virtual float GetLastKnownAvgIntensity() const {return -1;}
  /// should be called on camera cut to reset GetLastKnownAvgIntensity history
  virtual void ForgetAvgIntensity() {}

  //@( Vsync based measurement interface
  virtual bool CanVSyncTime() const;
  virtual void ResetTimingVSync();
  virtual float GetTimeVSync() const;
  virtual int GetPresentationIntervalVSync() const;
  virtual int GetLastFrameEndVSync() const;
  virtual int GetKnownErrorVsync() const;
  //@}
  virtual void ResetTimingHistory(int ms);

  #if _ENABLE_CHEATS
    virtual RString GetStat(int statId, RString &statVal, RString &statVal2);
  #endif
  
  /// enable night vision device, set eye adaptation level factor
  void SetNightVision( bool state, bool thermalState, float accomFactor, bool isFixed );
  /// check night vision device state
  bool GetNightVision() const;
protected:
  //! Thermal vision
  void EnableThermalVision(bool state, float accomFactor, bool isFixed);
  //! Is thermal vision requested
  bool _thermalWanted;
public:
  //! Check thermal vision is being used
  bool GetThermalVision() const;
  void SetThermalVisionWanted(bool set) {_thermalWanted = set;}
  bool GetThermalVisionWanted() {return _thermalWanted;}
  /// eye adaptation level factor
  float GetAccomFactor() const {return _accomFactor;}

  void SetAspectSettings(const AspectSettings &set){_aspectSettings=set;}
  void GetAspectSettings(AspectSettings &get){get=_aspectSettings;}

#if _VBS3
  //! Setting of the tunnel vision coefficient to be used
  virtual void SetTunnelVisionCoef(float tunnelVisionCoef) {}
  //! Invoking of the FlashBang effect
  virtual void SetFlashBang(float duration) {}
#endif

  virtual bool IsWBuffer() const {return false;}
  virtual bool CanWBuffer() const {return false;}
  virtual void SetWBuffer(bool val) {}
  
  virtual bool CanTwoSidedStencil() const {return false;}

  virtual int MaxTextureSize() const {return 32*1024;}


  /// color filter including eye accomodation
  //ColorVal GetAccomodateEye() {_accomodateEye = Color(0.5, 0.5, 0.5, 1); return _accomodateEye;} // color filter
  ColorVal GetAccomodateEye() {return _accomodateEye;} // color filter

  virtual void EnableNightEye(float night) {}

  /// set if z-priming 1st pass was done
  virtual void SetZPrimingDone(int cb, bool zPrimingDone) {}

  //@{ performance capture
  /// debug marker for shader debugger
  virtual void SetDebugMarker(int id, const char *text){}
  /// begin debug scope (PIX)
  virtual int ProfileBeginGraphScope(unsigned int color,const char *name) const {return 0;}
  /// end debug scope (PIX
  virtual int ProfileEndGraphScope() const {return 0;}
  //@}

  int ShowFpsMode() const {return _showFps;}

  void SetFogColor(ColorVal fogColor, ColorVal skyTopColor);
  ColorVal FogColor(){return _fogColor;}

  /// global shadow intensity setting
  void SetShadowFactor(ColorVal shadowLA, ColorVal shadowLD, float shadowFactor)
  {
    _shadowLA = shadowLA;
    _shadowLD = shadowLD;
    _shadowFactor = shadowFactor;
  }

  //! Setting of per instance information - instance color, land shadow
  virtual void SetInstanceInfo(int cb, ColorVal color, float shadowIntensity, bool optimize = true) {}
  virtual ColorVal GetInstanceColor(int cb) const {return HWhite;}
  virtual float GetInstanceShadowIntensity(int cb) const {return 1.0f;}
  
private:
  Engine( const Engine &src ); // no copy
  void operator = ( const Engine &src );

public:
  Engine();

	/// dummy engine?
	virtual bool IsDummy() = 0;

  virtual void CreateAllPushBuffers() {};
  /// initalization - may access misc. files
  virtual void Init();
  virtual ~Engine();
  //! Function to create an empty pushbuffer
  virtual PushBuffer *CreateNewPushBuffer(int key) {return NULL;};
  //! Inform engine the last material could have been changed
  virtual void MaterialHasChanged(int cb) {};
  //! Maximum number of bones the engine is capable to set at once
  virtual int GetMaxBonesCount() const = 0;
  /// set bones for skinning shader
  virtual void SetSkinningType(int cb, ESkinningType skinningType, const Matrix4 *bones = NULL, int nBones = 0) = 0;
#if _ENABLE_SKINNEDINSTANCING
  virtual void SetSkinningTypeInstance(int cb, ESkinningType skinningType, int instanceIndex, const Matrix4 *bones, int nBones) {}
#endif

  //! Return maximum intensity a star can have
  virtual float GetMaxStarIntensity() const {return 0.0015f;}

  /// check if animated water surface is supported
  virtual bool CanSeaWaves() const {return false;}
  virtual bool CanSeaUVMapping(int waterSegSize, float &mulUV, float &addUV) const {return false;}

  //! Determines whether specified shader can be instanced or not
  virtual bool CanBeInstanced(VertexShaderID vsID) const
  {
    if ((vsID == VSWater) || (vsID == VSWaterSimple) || (vsID == VSPoint) || (vsID == VSCalmWater) || (vsID == VSShore) )
    {
      return false;
    }
    else
    {
      return true;
    }
  }
  
  //! Determines number of possible PS lights
  virtual int GetMaxPSLightsCount() const {return 7;}
  
  /// interface for water depth for water shader 
  /**
  @param seaLevel sealevel not including waves - other parameters are relative to this
  @param shoreTop water depth where water is considered fully off-shore
  @param shoreBottom water depth where water is considered going off-shore
  @param peakWaveTop point where water peaks should be max. visible (rel. to sealevel)
  @param peakWaveBottom point where water peaks should start appearing (rel. to sealevel)
  @param u values in all four corners
  @param v values in all four corners
  */
  typedef float WaterDepthQuad[4];
  virtual void SetWaterWaves(int cb, const WaterDepthQuad *water, int xRange, int zRange){}
  virtual void SetWaterDepth(
    float seaLevel, float shoreTop, float shoreBottom, float peakWaveTop, float peakWaveBottom,
    int xRange, int zRange, float grid
  ){}
  /// cloud/landscape shadow casting
  virtual void SetLandShadow(
    int cb, const float *shadow, int xRange, int zRange, float grid
  ) {}
  
  
  void SetSceneProperties(EngineSceneProperties *sceneProps);
  
  /// check if we can draw (handle lost device)
  virtual AbilityToDraw IsAbleToDraw(bool wantReset) {return ATDAble;}
  /// check if we can draw (handle lost device), only return known status, never call any D3D API
  virtual bool IsAbleToDrawCheckOnly() {return true;}

  //! Sets the modulate color
  virtual void SetModulateColor(const Color &modColor){}
  /// used to flush background thread before postprocessing takes place
  /** optional - may be called as performance optimization, but is not required to be called to perform correctly */
  virtual void PreparePostprocess(){}

  /// some diagnostic rendering is not compatible with background recording
  virtual void FlushBackgroundScope(){}
  /// perform any postprocessing effects as necessary
  virtual void Postprocess(float deltaT, const PostFxSettings &pfx){}
  /// indicate no postprocess effects will be ever performed
  virtual void NoPostprocess(){}
  
  virtual void CaptureRefractionsBackground() {}
	virtual void DrawLightHeatHaze(HeatHazeRenderArray& instances, Texture *tex) {};

  //! Function to calculate flare intensity at specified coordinates (in <-1,1> space) into texture,
  //! and to set the texture at stage 1 (function takes advantage of the post process effect)
  virtual void CalculateFlareIntensity(int x, int y, int sizeX, int sizeY) {}

  virtual void Clear(bool clearZ=true, bool clear=true, PackedColor color=PackedColor(0), bool clearStencil=true) = 0;
  void DrawFinishTexts();

  struct PerfDiagsContext;

  int ShowStats( StatisticsByName &stats, int scale, float &y, PerfDiagsContext &ctx );

  void ShowDiagGraph(IDiagGraph *graph);

  /// draw a diagnostics graph
  void DrawDiagGraph(float x, float y, float w, float h, IDiagGraph *graph);
  

  /// mouse control of perf diags (introduced for Multicore diags)  
  bool CheckCheatMouse(float &mouseX, float &mouseY, bool &cursor);
  /// perfscope diagnostics
  void DrawDiagsPerfScope( PerfDiagsContext &ctx, float &y );
  /// basic diagnostics
  void DrawDiagsCPU( PerfDiagsContext &ctx, float &y );
  /// when not in the CPU mode, we may need to stop CPU counters
  void StopDiagsCPU();
  
  //! Recalculation of accomodateEye and filterEye
  virtual void RecalcAccomodation();
  //! Drawing initialization
  virtual void InitDraw( bool clear=false, Color color = HBlack); // Begin scene
  virtual void FinishDraw(int wantedDurationVSync, bool allBackBuffers = false, RString fileName = RString()); // End scene
  /// alternative to InitDraw when not doing rendering
  virtual void InitNoDraw();
  virtual void FinishNoDraw(int wantedDurationVSync);

  /// if there is any present pending, finish it
  virtual void FinishPresent() {}
  
  virtual void FinishFinishPresent() {}

  /// check if Present or 2D rendering is blocking
  virtual bool PresentIsAsync() {return false;}
  
  // make sure a copy of the current backbuffer copy is present in the whole swap chain
  virtual void BackBufferToAllBuffers() {}
  
  /// check how much time we should wait before starting rendering to avoid a CPU stall
  /** Used to avoid communication buffer (pushbuffer) overflooding */
  virtual float WaitBeforeFrameDrawing() {return 0;}
  
  virtual void NextFrame(bool doPresent); // swap frames - get ready for next frame
  virtual bool InitDrawDone() {return true;} 
  virtual void Pause() = 0; // stop and prepare everything for GDI
  virtual void Restore() = 0; // restore after minimized - before app goes to fullscreen
  virtual void StopAll() {} // stop all background activity - used before termination
  virtual void FogColorChanged( ColorVal fogColor ) = 0;
  
  virtual bool SwitchRes(int x, int y, int w, int h, int bpp) = 0; // switch to resolution nearest to w,h
  virtual bool SwitchRefreshRate(int refresh) = 0; // switch to resolution nearest to w,h
  virtual bool SwitchWindowed(bool windowed, int w=0, int h=0, int bpp=0) = 0;
  virtual RString GetDebugName() const = 0;
  virtual void KeepAspect(int &newW, int &newH) {}
  virtual void SetAspect(int newW, int newH) {}

  virtual void BeginOptionsVideo() {}
  virtual void EndOptionsVideo() {}
  virtual bool IsInOptionsVideo() const {return false;}
  
  virtual void ListResolutions(FindArray<ResolutionInfo> &ret) = 0;
  virtual void ListRefreshRates(FindArray<int> &ret) = 0;
  virtual void SetGamma( float g ) = 0;
  virtual float GetGamma() const = 0;
  virtual float GetDefaultGamma() const = 0;
  
  virtual void SetTextureQuality(int value){}
  virtual int GetTextureQuality() const {return 0;}
  virtual int GetMaxTextureQuality() const {return 2;}

  virtual void SetTextureMemory(int value){}
  virtual int GetTextureMemory() const {return 0;}
  virtual int GetMaxTextureMemory() const {return 2;}
  virtual int GetTextureMemoryDefault() const {return 1;}
  
  /// device quality level - 0, 0x100 = Low, 0x200 = SM2.0 Normal, 0x300 = High
  virtual int GetDeviceLevel() const {return 0x200;}
  /// check what quality level correspond current settings to
  virtual int GetCurrentDeviceLevel() const {return 0x200;}
  
  /// set all settings to default, assuming device level is as given
  virtual void SetDefaultSettingsLevel(int level) {}
  
  //@{ user settings - HDR
  //@{ user settings - HDR
  enum HDRPrec {HDR8=1,HDR16=2,HDR32=4};
  /// converts HDR precision to bits per color channel
  static const int HDRPrecToBits(HDRPrec prec)
  {
    switch (prec)
    {
    case HDR8:
      return 8;
    case HDR16:
      return 16;
    case HDR32:
      return 32;
    default:
      Assert(!"Invalid HDR precision.");
      return 8;
    }
  }
  /// converts bits per color channel to hdr precision
  static const HDRPrec BitsToHDRPrec(int bits)
  {
    if (bits <= 8)
      return HDR8;
    else if (bits <= 16)
      return HDR16;
    else
      return HDR32;
  }
  
  virtual HDRPrec HDRPrecision() const {return HDR8;}
  virtual void SetHDRPrecision(HDRPrec hdrPrec) {}
  virtual int SupportedHDRPrecision() const {return HDR8;}
  //@}

  virtual void SetVSync(bool vsync) {};
  virtual bool GetVSync() {return true;};

  //@{ user settings - FSAA
  virtual void SetFSAA(int quality) {}
  virtual int GetFSAA() const {return 0;}
  /// max. FSAA mode supported (0=none)
  virtual int GetMaxFSAA() const {return 0;}
  //@}

  //@{ user settings - AToC
  virtual void SetAToC(int level) {}
  virtual int GetAToC() const { return 0; }
  //@}

  //@{ user settings - PPAA
  virtual void SetPPAA(int ppaa, int level) {}
  virtual int GetPPAA(int &level) const {return 0;}
  //@}

  //@{ user settings - FXAA
  virtual void SetPPAAQuality(int quality) {}
  virtual int GetPPAAQuality() const {return 0;}
  //@}

  //@{ user settings - anisotropic filtering
  virtual void SetAnisotropyQuality(int quality) {}
  virtual int GetAnisotropyQuality() const {return 0;}
  //@}

  //@{ user settings - postprocess effects
  virtual void SetPostprocessEffects(int quality) {}
  virtual int GetPostprocessEffects() const {return 0;}
  //@}

  virtual void SaveConfig(ParamFile &cfg, const char *section);
  virtual void LoadConfig(ParamEntryPar cfg, ParamEntryPar fcfg, const char *section);
  //find windows aspect ratio and set it as default in not already in config file
  virtual bool FindDefaultAspect(ParamEntryPar cfg);

  /** note: brightness is not virtual - it is done via materials */
  void SetBrightness( float v ) { saturate(v, 0.5f, 1.5f); _usrBrightness=v;}
  float GetBrightness() const {return _usrBrightness;}

  virtual void SetBloom(float val) {}
  virtual float GetBloom() const {return 0;}
  virtual void SetRotBlurPower(float blurPow) {}
  virtual float GetRotBlurPower() const {return 0;}

  virtual void SetFillrateControl( ResolutionInfo info ) {}
  virtual ResolutionInfo GetFillrateControl() const
  {
    ResolutionInfo info;
    info.h = HeightBB();
    info.w = WidthBB();
    info.bpp = PixelSize();
    return info;
  }
  virtual float GetDefaultFillrateControl(float &fMin, float &fMax) const {fMin=fMax=1.0f;return 1.0;}
  virtual void SetDefaultFillrateControl() {}
  //return list of posible BB resolutions for vidio options 
  virtual void ListFillrateResolutions(FindArray<ResolutionInfo> &ret) {};

  /// part of the brightness handled in material (pre-HDR)
  virtual float GetPreHDRBrightness() const {return _usrBrightness;}
  /// default brightness
  virtual float GetDefaultBrightness() const {return 1.0f;}
  /// default HDR factor used for scene rendering
  virtual float GetHDRFactor() const {return 0.5f;}
#if _ARMA3_HDR_IMPROVEMENTS
  /// coef that is used to multiply the size of the image to determine rectangle, which hdr luminance is computed from
  virtual float GetHDRLuminanceRectCoef() const {return _hdrLuminanceRectCoef;}
  virtual void SetHDRLuminanceRectCoef(float coef) {_hdrLuminanceRectCoef = coef; saturate(_hdrLuminanceRectCoef, 0.1f, 1.0f);}

  /// coef used to multiply the desired luminance midpoint
  virtual float GetDesiredLuminanceCoef() const {return _desiredLuminanceCoef;}
  virtual void SetDesiredLuminanceCoef(float coef) {_desiredLuminanceCoef = coef;}
#endif
  virtual void PrepareTriangleTL(
    int cb, const PolyProperties *section,
    const MipInfo &mip, const TexMaterialLODInfo &mat, int specFlags,
    const TLMaterial &tlMat,
    int spec, const EngineShapeProperties &prop
  ) {}

  virtual void PrepareTriangle( int cb, const MipInfo &mip, const TexMaterial *mat, int specFlags )= 0;
  virtual void DrawPolygon( const VertexIndex *i, int n )= 0;
  virtual void DrawSection(const FaceArray &face, Offset beg, Offset end)= 0;

  virtual void FlushQueues() {}

  // integrated transform&lighting
  virtual bool GetTL() const {return false;}
  virtual bool HasWBuffer() const {return false;} // far plane important

  //virtual void SetMaterial(const TLMaterial &mat,const LightList &lights, int spec) {}
  virtual void ProjectionChanged(int cb) {}
  virtual void EnableSunLight(EMainLight mainLight) {}
  virtual void SetFogMode(EFogMode fogMode) {}
  virtual void SetupInstances(int cb, const ObjectInstanceInfo *instances, int nInstances) {};
  virtual void SetupInstancesSprite(int cb, const ObjectInstanceInfo *instances, int nInstances) {};
  virtual void DrawMeshTLInstanced(
    int cb, const Shape &sMesh, const EngineShapeProperties &prop, int spec, const LightList &lights, int bias,
    const ObjectInstanceInfo *instances, int nInstances, float minDist2, const DrawParameters &dp=DrawParameters::_default
  );
  virtual void DrawShadowVolumeInstanced(
    int cb, LODShape *shape, int level, const ObjectInstanceInfo *instances, int nInstances, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
    , int drawListId
#endif
  );
  virtual void BeginShadowVolume(int cb, float shadowVolumeSize) {}
  
  virtual void BeginShadowVolumeRendering() {}
  virtual void EndShadowVolumeRendering() {}
  //! Setting of model properties (called once per model or group of instances)
  /*!
    /param minDist2 Minimum distance of the object from camera. -1.0f in case it was not initialized
  */
  virtual void BeginInstanceTL(int cb, const PositionRender &modelToWorld, float minDist2, int spec, const LightList &lights, const DrawParameters &dp=DrawParameters::_default) {}
  virtual int ShapePropertiesNeeded(const TexMaterial *mat) const {return 0;}
  virtual bool BeginMeshTL(int cb, const VertexTableAnimationContext *animContext, const Shape &sMesh, const EngineShapeProperties &prop, int spec, const LightList &lights, int bias) {return false;} // convert all mesh vertices
  virtual void EndMeshTL(int cb, const Shape &sMesh ) {} // forget mesh

  //!{ Methods to control the ShadowMapRendering
  virtual bool IsSBPossible() const {return false;}
  virtual void EnableSBShadows(int quality) {}
  virtual bool IsSBEnabled() const {return false;}
  virtual void PrepareShadowMapRendering(float sunClosestDistanceFrom0, int layerIndex, float shadowsZ, bool firstLayer) {}
  virtual void BeginShadowMapRendering(float sunClosestDistanceFrom0, int layerIndex, float shadowsZ, bool firstLayer, bool rainMap) {}
  virtual void EndShadowMapRendering(float minShadowsZ, float maxShadowsZ, bool firstLayer, bool lastLayer, bool rainMap) {}
  virtual void RenderStencilShadows() {}

	#ifdef TEST_RAIN_MAP
		virtual bool IsRainMapEnabled() const {return false; }
	#endif

  //!}
  
  virtual void CommitSettings(){}

  //!{ Methods to control the depth of field effect
  /// some engines may be able to implement depth of field
  virtual void EnableDepthOfField(float focusDist, float blur, bool farOnly) {}
  virtual void EnableDepthOfField(int quality) {}
  virtual bool IsDepthOfFieldEnabled() const {return false;}
  virtual void BeginDepthMapRendering(bool zOnly
#if _HELISIM_RENDER_TO_TEXTURE
    , bool useR2T
#endif
    ) {}
  virtual void EndDepthMapRendering(bool zOnly) {}
  virtual void SetDepthClip(float zClip) {}
  /// adjust current z-range used by rendering from 0..1 interval
  virtual void SetDepthRange(float minZ = DepthBorderCommonMin, float maxZ = DepthBorderCommonMax) {}
  //!}

  //! Pass the temperature table into the GPU
  virtual void SetTemperatureTable(const float *table, int gSize, int dotSize, int kSize) {}
  //! Relative setting of TI brightness (black-hot modes are considered)
  virtual void SetTIBrightnessRelative(float brightness) {}
  virtual bool TemperatureTableUpdateNeeded(const float *table, int length) {return true;}
  //!{ TI properties wrappers
  virtual void SetTIBlur(float blur) {}
  virtual float GetTIBlur() const {return -1.0f;}
  virtual void SetTIBrightness(float brightness) {}
  virtual float GetTIBrightness() const {return -1.0f;}
  virtual void SetTIContrast(float contrast) {}
  virtual float GetTIContrast() const {return -1.0f;}
  virtual void SetTIMode(int mode) {}
  virtual int GetTIMode() const {return -1;}
  virtual void SetTIAutoBC(bool autoBC) {}
  virtual bool GetTIAutoBC() const {return false;}
  virtual void SetTIAutoBCContrastCoef(float contrastCoef) {}
  virtual float GetTIAutoBCContrastCoef() const {return -1.0f;}
  //!}
  //! TI conversion texture loading 
  virtual void LoadTIConversionTexture(RString name) {};

  //! Method to fill out the PS constants with craters properties
  virtual void UseCraters(int cb, const AutoArray<CraterProperties> &craters, int maxCraters, const Color &craterColor) {}
  //! Method to return the maximum number of available craters
  virtual int GetMaxCraters() const {return 0;}

  virtual void DrawSSAO() {}
	virtual void DrawEnfDebugShapes() {}

  virtual void DrawSectionTL(int cb, const Shape &sMesh, int beg, int end, int bias, int instancesOffset = 0, int instancesCount = 0, const DrawParameters &dp = DrawParameters::_default) {}
  virtual void RememberAndVerifyHWState() {}
  virtual void DrawStencilShadows(bool interior, int quality) {}
  virtual void DrawDecal
  (
    int cb, Vector3Par pos, float rhw, float sizeX, float sizeY, PackedColor col,
    const MipInfo &mip, const TexMaterial *mat, int specFlags
  )= 0; // 3D rectangle
  virtual void Draw2D
  (
    const Draw2DParsExt &pars, const Rect2DAbs &rect, const Rect2DAbs &clip=Rect2DClipAbs
  )= 0; // 2D rectangle
  virtual void Draw2D
  (
    const Draw2DPars &pars, const Rect2DAbs &rect, const Rect2DAbs &clip=Rect2DClipAbs
  )
  {
    Draw2D(Draw2DParsExt(pars),rect,clip);
  }
  virtual void Draw2D
  (
    const Draw2DParsNoTex &pars, const Rect2DAbs &rect, const Rect2DAbs &clip=Rect2DClipAbs
  )
  {
    Draw2D(Draw2DParsExt(pars),rect,clip);
  }
  virtual void Draw2D
  (
    const Draw2DPars &pars, const Rect2DPixel &rect, const Rect2DPixel &clip=Rect2DClipPixel
  )
  {
    Rect2DAbs rectA,clipA;
    Convert(rectA,rect);
    Convert(clipA,clip);
    Draw2D(pars,rectA,clipA);
  }
  virtual void Draw2D
  (
    const Draw2DParsExt &pars, const Rect2DPixel &rect, const Rect2DPixel &clip=Rect2DClipPixel
  )
  {
    Rect2DAbs rectA,clipA;
    Convert(rectA,rect);
    Convert(clipA,clip);
    Draw2D(pars,rectA,clipA);
  }
  virtual void Draw2D
  (
    const Draw2DParsNoTex &pars, const Rect2DPixel &rect, const Rect2DPixel &clip=Rect2DClipPixel
  )
  {
    Rect2DAbs rectA,clipA;
    Convert(rectA,rect);
    Convert(clipA,clip);
    Draw2D(pars,rectA,clipA);
  }
  virtual void Draw2DWholeScreen(PackedColor color);
  virtual void Draw2DWholeScreen(PackedColor color, float alpha);

  virtual void DrawPolyPrepare(const MipInfo &mip, int specFlags=DefSpecFlags2D) = 0;
  virtual void DrawLinePrepare() = 0;
  // DrawPolyDo may be called several times after DrawPolyPrepare was called
  virtual void DrawPolyDo
  (
    const Vertex2DAbs *vertices, int nVertices,
    const Rect2DAbs &clip=Rect2DClipAbs
  ) = 0;
  virtual void DrawPolyDo
  (
    const Vertex2DPixel *vertices, int nVertices,
    const Rect2DPixel &clip=Rect2DClipPixel
  ) = 0;
  virtual void DrawPoly
  (
    const MipInfo &mip, const Vertex2DAbs *vertices, int nVertices,
    const Rect2DAbs &clip=Rect2DClipAbs, int specFlags=DefSpecFlags2D
  )
  {
    DrawPolyPrepare(mip,specFlags);
    DrawPolyDo(vertices,nVertices,clip);
  }
  virtual void DrawPoly
  (
    const MipInfo &mip, const Vertex2DPixel *vertices, int nVertices,
    const Rect2DPixel &clip=Rect2DClipPixel, int specFlags=DefSpecFlags2D
  )
  {
    DrawPolyPrepare(mip,specFlags);
    DrawPolyDo(vertices,nVertices,clip);
  }

  // DrawLineDo may be called several times after DrawLinePrepare was called
  virtual void DrawLineDo
  (
    const Line2DAbs &line,
    PackedColor c0, PackedColor c1,
    const Rect2DAbs &clip=Rect2DClipAbs
  )= 0; // 2D line

  //! batch line rendering
  virtual void DrawLines
  (
    const Line2DAbsInfo *lines, int nLines, const Rect2DAbs &clip=Rect2DClipAbs
  );

  //! batch line rendering
  virtual void DrawLines
  (
    const Line2DPixelInfo *lines, int nLines, const Rect2DPixel &clip=Rect2DClipPixel
  );

  virtual void DrawLineDo(
    const Line2DPixel &rect,
    PackedColor c0, PackedColor c1,
    const Rect2DPixel &clip=Rect2DClipPixel
  )
  {
    Line2DAbs rectA;
    Rect2DAbs clipA;
    Convert(rectA,rect);
    Convert(clipA,clip);
    DrawLineDo(rectA,c0,c1,clipA);
  }
  virtual void DrawLineDo(
    const Line2DFloat &rect,
    PackedColor c0, PackedColor c1,
    const Rect2DFloat &clip=Rect2DClipFloat
  )
  {
    Line2DAbs rectA;
    Rect2DAbs clipA;
    Convert(rectA,rect);
    Convert(clipA,clip);
    DrawLineDo(rectA,c0,c1,clipA);
  }


  virtual void DrawLine(
    const Line2DAbs &rect,
    PackedColor c0, PackedColor c1,
    const Rect2DAbs &clip=Rect2DClipAbs
  )
  {
    DrawLinePrepare();
    DrawLineDo(rect,c0,c1,clip);
  }

  virtual void DrawLine(
    const Line2DPixel &rect,
    PackedColor c0, PackedColor c1,
    const Rect2DPixel &clip=Rect2DClipPixel
  )
  {
    Line2DAbs rectA;
    Rect2DAbs clipA;
    Convert(rectA,rect);
    Convert(clipA,clip);
    DrawLinePrepare();
    DrawLineDo(rectA,c0,c1,clipA);
  }
  virtual void DrawLine(
    const Line2DFloat &rect,
    PackedColor c0, PackedColor c1,
    const Rect2DFloat &clip=Rect2DClipFloat
  )
  {
    Line2DAbs rectA;
    Rect2DAbs clipA;
    Convert(rectA,rect);
    Convert(clipA,clip);
    DrawLinePrepare();
    DrawLineDo(rectA,c0,c1,clipA);
  }



  virtual void DrawLine(const TLVertexTable &mesh, int beg, int end, int orFlags=0)= 0;  // 3D line - width in m
  void Draw2D(
    const MipInfo &mip, PackedColor color,
    const Rect2DAbs &rect,
    const Rect2DAbs &clip=Rect2DClipAbs,
    int shadow = 0
  ) // wrapper to keep old interface working
  {
    Draw2DPars pars;
    pars.mip=mip;
    pars.SetColor(color);
    pars.Init();
    pars.spec|= (shadow == 2)?UISHADOW : 0;
    // call wrapped function
    Draw2D(pars,rect,clip);
  }
  void Draw2D(
    const MipInfo &mip, PackedColor color,
    const Rect2DPixel &rect,
    const Rect2DPixel &clip=Rect2DClipPixel, 
    int shadow = 0
  ) // wrapper to keep old interface working
  {
    Rect2DAbs rectA,clipA;
    Convert(rectA,rect);
    Convert(clipA,clip);
    Draw2DPars pars;
    pars.mip=mip;
    pars.SetColor(color);
    pars.Init();
    pars.spec|= (shadow == 2)?UISHADOW : 0; 
    // call wrapped function
    Draw2D(pars,rectA,clipA);
  }

  virtual void PrepareMesh( int cb, int spec ) = 0; // prepare internal variables
  virtual bool BeginMesh( int cb, TLVertexTable &mesh, int spec ) = 0; // convert all mesh vertices
  virtual void EndMesh( int cb, TLVertexTable &mesh ) = 0; // forget mesh
  
  /// start counting how many pixels were actually rendered
  virtual IVisibilityQuery *BegPixelCounting(int cb, IVisibilityQuery *query) {return NULL;}
  /// end counting pixels
  virtual void EndPixelCounting(int cb, IVisibilityQuery *query) {}

  virtual AbstractTextBank *TextBank()= 0; // texture management
  
  virtual void RefreshBuffer(const VertexTable &sMesh) {}
  virtual Ref<VertexBuffer> CreateVertexBuffer(const Shape &src, VBType type, bool frequent) {return NULL;}
  virtual void ReleaseAllVertexBuffers() {}
  virtual void DeferVBufferRelease( VertexBuffer *buffer ) {}

  //virtual HWND GetWindowHandle() const = 0;

  // shadow related functions
  virtual float ZShadowEpsilon() const = 0; // bias used for shadows
  virtual float ZRoadEpsilon() const = 0; // bias used for roads
  virtual float ObjMipmapCoef() const = 0; // pixel size multiplier
  //virtual float LandMipmapCoef() const = 0; // pixel size multiplier

  virtual void GetZCoefs(int cb, float &zAdd, float &zMult) = 0;
  virtual int GetBias(int cb) = 0;
  virtual void SetBias( int cb, int value ) = 0;

  /// alpha fogging may be used separately to a normal fog
  virtual void SetAlphaFog(int cb, float start, float end){}

  //@{ gameplay/script driven stuff control (postprocess effects...)
  virtual void ResetGameState(){}
  virtual LSError SerializeGameState(ParamArchive &ar){return LSOK;}
  //@}

  //! Enable SSAO post process effect
  virtual void EnableSSAO(bool enable) {}
  virtual bool IsEnabledSSAO() const { return false; }
  //! Enable film grain post process effect
  virtual void EnableFilmGrain(bool enable) {}
	virtual void EnableGodRays(bool enable) {}
	virtual void EnableRain(bool enable) {}
  //! Enable radial blur post process effect
  virtual void EnableRadialBlur(bool enable) {}
  //! Enable chromatic aberration post process effect
  virtual void EnableChromAberration(bool enable) {}
  //! Enable wet distortion post process effect
  virtual void EnableWetDistortion(bool enable) {}
  //! Enable color corrections post process effect
  virtual void EnableColorCorrections(bool enable) {}
  //! Enable dynamic blur post process effect
  virtual void EnableDynamicBlur(bool enable) {}
  //! Enable color inversion post effect
  virtual void EnableClrInvers(bool enable) {}

  //@{ start commit for post process
  virtual void CommitSSAO(float time) {}
  virtual void CommitRadBlur(float time) {}
  virtual void CommitFilmGrain(float time) {}
	virtual void CommitGodRays(float time) {}
	virtual void CommitRain(float time) {}
  virtual void CommitChromAberration(float time) {}
  virtual void CommitWetDistortion(float time) {}
  virtual void CommitColorCorrections(float time) {}
  virtual void CommitDynamicBlur(float time) {}
  virtual void CommitClrInvers(float time) {}
  //@}

  //@{ check state of post process
  virtual bool CommittedSSAO() const {return true;}
  virtual bool CommittedRadBlur() const {return true;}
  virtual bool CommittedFilmGrain() const {return true;}
	virtual bool CommittedGodRays() const {return true;}
	virtual bool CommittedRain() const {return true;}
  virtual bool CommittedChromAberration() const {return true;}
  virtual bool CommittedWetDistortion() const {return true;}
  virtual bool CommittedColorCorrections() const {return true;}
  virtual bool CommittedDynamicBlur() const {return true;}
  virtual bool CommittedClrInvers() const {return true;}
  //@}

	virtual void ReloadGodRays() const {}
	virtual void ReloadRain() const {}
	virtual void ReloadSSAO() const {}
	virtual void ReloadDOF() const {}
	virtual void ReloadEnfShapeCollectionRes() const {}
	virtual void ReloadRotBlur() const {}

#if _HELISIM_RENDER_TO_TEXTURE

  virtual void InitR2TDraw() {}
  //! Set RTs for render to texture
  virtual void SetR2TRenderTargets() {}
  //! Restore RTs for main scene
  virtual void RestoreR2TRenderTargets(RenderTargetID rtid, bool saveEnable) {}
  //! Set post effect params
  virtual void R2TSetPEColorsPars(RenderTargetID rtid, const float* pars, int count) {}
  //! Run post process on render target
  virtual void R2TPostProcess(int effect, float dT, RenderTargetID rtid) {}  
    //! Bind camera (specified by name) to render target
  virtual RenderTargetID BindR2T(const RString &name) { return R2TUninitialized; }
  //! UnBind camera from render target
  virtual bool UnBindR2T(RenderTargetID rtid) { return false; }
  //! Check whether target is valid
  virtual bool IsBindR2T(RenderTargetID rtid) const { return false; }
  //! Check whether target was used in main scene
  virtual bool WasR2TUsed(RenderTargetID rtid) const { return false; }
  //! Begin rendering to texture
  virtual void BeginR2T(RenderTargetID rtid) {}
  //! End rendering to texture
  virtual void EndR2T(RenderTargetID rtid) {}

  virtual float GetLastKnownR2TAvgIntensity(RenderTargetID rtid) const { return 0.0f; }
  //! Inform about r2t targets invalidation - sync with r2t cams
  virtual bool R2TCamsSyncNeeded() { return false; }

  //!
  virtual void SetPipQuality(int quality) { }
  //!
  virtual int GetPipQuality() const { return 0; }
  //!
  virtual bool IsR2TEnabled() const { return false; }

#endif

  //! Set radial blur post process parameters
  virtual void SetRadialBlurrParams(float pwrX, float pwrY, float offsX, float offsY) {}
  //! Set wet distortion params
  virtual void SetWetDistortionParams(float blur, float pwrX, float pwrY, float sh1, float sh2, float sv1, float sv2,
    float ah1, float ah2, float av1, float av2,float rndX, float rndY, float posX, float posY) {}
  //! Set chromatic aberration parameters
  virtual void SetChromaticAberrationParams(float aberationPowerX, float aberationPowerY, bool value) {}
  //! Set SSAO parameters
  virtual void SetSSAOParams(const float *params, int nums) {}
	//! Set film grain parameters
  virtual void SetFilmGrainParams(const float *params, int nums) {}
	//! Set god rays parameters
	virtual void SetGodRaysParams(const float *params, int nums) {}
	virtual void SetRainParams(const float *params, int nums) {}
	//! Set god rays parameters
	virtual void SetGodRays(const float *params, int nums) {}
	virtual void SetGodraysKernel(Texture *tex) {}
  //! Set color correction parameters
  virtual void SetColorCorrectionsParameters(float brightness, float contrast, float offset, Color blendClr, Color colClr, Color weights) {}
  //! Set dynamic blur parameters
  virtual void SetDynamicBlurParameters(float bluriness) {}
  //! Set inversion parameters
  virtual void SetClrInversParams(Color invColor) {}
  /// set texture used for rain postprocess effect
	virtual void SetNewRainTexture(Texture *tex) {}
  //! Set density of rain postprocess effect
  virtual void SetRainDensity(float density) {}
	virtual float GetRainRadius() const { return -1.0f; }
  //@}

  //@{ Post process functions
  virtual int CreatePostprocess(PostEffectType pFxType, int priority) { return -1; }
  virtual int CreatePostprocess(RString peTypeName, int priority) { return -1; }
  virtual void DestroyPostprocess(int hndl) {}
  virtual void EnablePostprocess(int hndl, bool enable) {}
  virtual int SetPostprocessParams(int hndl, AutoArray<float> &pars) { return 0; }
  virtual int SetPostprocessParams(int hndl, const float* pars, int nPars) { return 0; }
  virtual void CommitPostprocess(int hndl, float time) {}
  virtual bool CommittedPostprocess(int hndl) {return true;}
  //@}

#if _ARMA3_SIMUL_WEATHER
  virtual SimulWeatherBase* CreateSimulWeather() = 0;
  virtual int GetSWGeometryLayersCount() const { return 0; }
  virtual int GetSWGeometryDefault() const { return 0; }
  virtual void SetSWGeometryLayersCount(int layers) {}
#endif

  virtual void SetSpecialEffectPars(int index, const float *pars, int nPars) {}

  virtual bool ZBiasExclusion() const = 0;

  //@{ 2D viewport dimensions
  int Width2D() const;
  int Height2D() const;
  int Top2D() const;
  int Left2D() const;
  //@}

  /// return the offset of the current monitor in the virtual desktop
  void MonitorOffset(int &x, int &y) const
  {
    x = _monitorOffsetX;
    y = _monitorOffsetY;
  }

  //@{ 2D viewport conversions
  void Convert(Point2DAbs &to, const Point2DPixel &from);
  void Convert(Point2DAbs &to, const Point2DFloat &from);
  void Convert(Point2DPixel &to, const Point2DAbs &from);
  void Convert(Point2DFloat &to, const Point2DAbs &from);

  void Convert(Rect2DAbs &to, const Rect2DPixel &from);
  void Convert(Rect2DAbs &to, const Rect2DFloat &from);
  void Convert(Rect2DPixel &to, const Rect2DAbs &from);
  void Convert(Rect2DFloat &to, const Rect2DAbs &from);

  void Convert(Line2DAbs &to, const Line2DPixel &from);
  void Convert(Line2DAbs &to, const Line2DFloat &from);
  void Convert(Line2DPixel &to, const Line2DAbs &from);
  void Convert(Line2DFloat &to, const Line2DAbs &from);
  //@}
  
  void PixelAlignXY(Point2DAbs &pos);
  void PixelAlignX(Point2DAbs &pos);
  void PixelAlignY(Point2DAbs &pos);
  void PixelAlignXY(Point2DPixel &pos);
  void PixelAlignX(Point2DPixel &pos);
  void PixelAlignY(Point2DPixel &pos);

  float PixelAlignedX(float x);
  float PixelAlignedY(float x);

  // general
  //@{ 3D rendering area dimensions
  virtual int Width() const = 0;
  virtual int Height() const = 0;
  //@}

  //@{ backbuffer dimensions  
  virtual int WidthBB() const {return Width();}
  virtual int HeightBB() const {return Height();}
  //@}
  
  virtual int PixelSize() const = 0; // 16 or 32 bit mode?
  virtual int RefreshRate() const = 0;
  virtual bool CanBeWindowed() const = 0;
  virtual bool IsWindowed() const = 0;

  virtual int MinGuardX() const {return 0;} // used for guard band clipping
  virtual int MaxGuardX() const {return Width();}
  virtual int MinGuardY() const {return 0;}
  virtual int MaxGuardY() const {return Height();}

  virtual int MinSatX() const {return 0;} // used for saturation
  virtual int MaxSatX() const {return Width();}
  virtual int MinSatY() const {return 0;}
  virtual int MaxSatY() const {return Height();}

  virtual int AFrameTime() const = 0;

  void FontDestroyed( Font *font );

  #ifndef ACCESS_ONLY

  // 3D texture drawing
  void Draw3D(
    int cb, Vector3Par pos, Vector3Par up, Vector3Par dir, ClipFlags clip,
    PackedColor color, int spec, Texture *tex,
    float x1c = 0, float y1c = 0, float x2c = 1, float y2c = 1
  );

  class MainThreadRenderTask
  {
    public:
    virtual void operator () (Engine *engine) const = 0;
    virtual ~MainThreadRenderTask(){}
  };
  
  /// perform a task which is not thread safe and needs to be executed from the main thread only
  virtual void PerformTaskOnMainThread(MainThreadRenderTask * task) {}
  
  // text drawing
  Font *GetDebugFont() const {return _debugFont;}
  float GetDebugFontSize() const {return _debugFontSize;}
  Font *LoadFont(FontID id);
  bool IsFontReady3D(Font *font) const;
  void DrawText3D(
    int cb, Vector3Par pos, Vector3Par up, Vector3Par dir, ClipFlags clip,
    Font *font, PackedColor color, int spec, const char *text,
    float x1c = 0, float y1c = 0, float x2c = 1e6, float y2c = 1
  );
  virtual void DrawText3DCenteredAsync(
    Vector3Par center, Vector3Par up, Vector3Par dir, ClipFlags clip,
    Font *font, PackedColor color, int spec, const char *text,
    float x1c = 0, float y1c = 0, float x2c = 1e6, float y2c = 1
  );
  void CCALL DrawText3DF(
    int cb, Vector3Par pos, Vector3Par up, Vector3Par dir, ClipFlags clip,
    Font *font, PackedColor color, int spec, const char *text, ...
  );
  Vector3 GetText3DWidth(
    Vector3Par dir, Font *font, const char *text
  );
  Vector3 CCALL GetText3DWidthF(
    Vector3Par dir, Font *font, const char *text, ...
  );
  
  void DrawText(
    const Point2DFloat &pos, const TextDrawAttr &attr, const char *text
  );
  void DrawText(
    const Point2DAbs &pos, const TextDrawAttr &attr, const char *text
  );
  void DrawText(
    const Point2DFloat &pos, const Rect2DFloat &clip,
    const TextDrawAttr &attr, const char *text
  );
  void DrawText(
    const Point2DAbs &pos, const Rect2DAbs &clip,
    const TextDrawAttr &attr, const char *text
  );

  void DrawText(
    const Point2DFloat &pos, float size,
    Font *font, PackedColor color, const char *text, int shadow
  )
  {
    DrawText(pos,TextDrawAttr(size,font,color, shadow),text);
  }
  void DrawText(
    const Point2DAbs &pos, float size,
    Font *font, PackedColor color, const char *text, int shadow
  )
  {
    DrawText(pos,TextDrawAttr(size,font,color,shadow),text);
  }
  void DrawText(
    const Point2DFloat &pos, float size,
    const Rect2DFloat &clip,
    Font *font, PackedColor color, const char *text, int shadow
  )
  {
    DrawText(pos,clip,TextDrawAttr(size,font,color, shadow),text);
  }
  void DrawText(
    const Point2DAbs &pos, float size,
    const Rect2DAbs &clip,
    Font *font, PackedColor color, const char *text, int shadow
  )
  {
    DrawText(pos,clip,TextDrawAttr(size,font,color, shadow),text);
  }
  
  void DrawTextVertical(
    const Point2DFloat &pos, float size,
    Font *font, PackedColor color, const char *text
  );
  void DrawTextVertical
  (
    const Point2DFloat &pos, float size,
    const Rect2DFloat &clip,
    Font *font, PackedColor color, const char *text
  );
  float GetTextWidth(const TextDrawAttr &attr, const char *text);
  float GetTextWidth(float size, Font *font, const char *text)
  {
    return GetTextWidth(TextDrawAttr(size,font),text);
  }
  int GetTextPosition(float x, float size, Font *font, const char *text);

  const FontWithSize *SelectFontSize(
    const TextDrawAttr &attr, const char *text, bool &mapOneToOne, float &sizeH, float &sizeW
  );
  
  void CCALL DrawTextF(
    const Point2DFloat &pos, float size,
    Font *font, PackedColor color, int shadow, const char *text, ...
  );
  void CCALL DrawTextF(
    const Point2DAbs &pos, float size,
    Font *font, PackedColor color, int shadow, const char *text, ...
  );
  void CCALL DrawTextF(
    const Point2DFloat &pos, float size,
    const Rect2DFloat &clip,
    Font *font, PackedColor color, int shadow, const char *text, ...
  );
  
  void CCALL DrawTextF(
    const Point2DFloat &pos, const TextDrawAttr &attr, const char *text, ...
  );
  void CCALL DrawTextF(
    const Point2DAbs &pos, const TextDrawAttr &attr, const char *text, ...
  );
  void CCALL DrawTextF(
    const Point2DFloat &pos, const Rect2DFloat &clip,
    const TextDrawAttr &attr, const char *text, ...
  );
  void CCALL DrawTextVerticalF(
    const Point2DFloat &pos, float size,
    Font *font, PackedColor color, const char *text, ...
  );
  void CCALL DrawTextVerticalF(
    const Point2DFloat &pos, float size,
    const Rect2DFloat &clip,
    Font *font, PackedColor color, const char *text, ...
  );
  float CCALL GetTextWidthF(
    float size, Font *font, const char *text, ...
  );
  #endif

  void ShowFont
  (
    Font *font,
    PackedColor color=PackedColor(0xff000000), float size=1.0
  );
  //! delete floating text
  void RemoveText(int handle);
  //! create a new floating text
  int CreateShowText(int handle, int id, DWORD timeToLive, const char *text );
  //! get text slot number
  int FindTextSlot(int handle, int id) const;
  //! allocate and return text handles
  bool AllocateTextHandles(InitVal<int,-1> *handles, int count);

  //! create a new show-text
  int ShowText( DWORD timeToLive, const char *text );
  //! create a new show-text
  int CCALL ShowTextF( DWORD timeToLive, const char *text, ... );

  void ReinitCounters();
  
  // give opportunity to react to window changes
  virtual void Activate() {}
  virtual bool Deactivate() {return true;}
  virtual void Resize( int x, int y, int w, int h ) {}

  virtual void Screenshot(RString filename) {}

#if _VBS3_SCREENSHOT
  //! Function to save specified screenshot into file
  /*!
  If fileName is present and ends with '\' or '/', then it is used as a folder and automatic name is to be generated
  If fileName is present and doesn't end with '\' or '/', then it is considered as a file name with path
  If fileName is not present, then  folder 'Screenshots' is created in Documents and Settings folder of the VBS project and automatic name is to be used for the screenshot
  fileType can have one of the following values: BMP JPG TGA PNG DDS PPM DIB HDR PFM. If not present or unknown, then jpg is going to be used.
  */
  virtual void SaveScreenshot(RString fileName, RString fileType) {};
#endif

  // some helper drawing functions
  //! draw a frame - textured borders
  void DrawFrame(Texture *corner, PackedColor color, const Rect2DPixel &frame, const Rect2DPixel &clip = Rect2DClipPixel);
  //! draw a frame - textured borders
  void DrawFrame(Texture *corner, PackedColor color, const Rect2DAbs &frame, const Rect2DAbs &clip = Rect2DClipAbs);
  
  //! draw a frame - line borders
  void DrawLineFrame(PackedColor color, const Rect2DPixel &frame);
  //! draw a frame - line borders
  void DrawLineFrame(PackedColor color, const Rect2DAbs &frame);
  
  //@{
  /*!\name video rendering interface */
  /// start playing a video
  virtual VideoPlayback *VideoOpen(const char *path, float volume){return NULL;}
  /// stop playing a video
  virtual void VideoClose(VideoPlayback *video){}
  /// render another video frame
  virtual void VideoFrame(VideoPlayback *video){}
  //@}

  virtual void PrepareScreenshot() {}
  virtual void CleanUpScreenshot() {}
  virtual bool WriteScreenshotToXPR(RString path) {return false;}

  /// store a picture persistant across reset/reboot
  /** PersistDisplay may need a lot of memory - call GC before calling it */
  virtual void PersistDisplay() {}


  /// Command buffer interface
  virtual void StartCBScope(int numThreads, int nResults){}
  virtual void EndCBScope(){}
  virtual int StartCBRecording(int resultIndex, int thrId, int debugId) {return 0;}
  virtual void StopCBRecording(int cb, int resultIndex, const char *name) {}
  /// check if some CB is currently being recorded
  virtual bool IsCBRecording() const {return false;}
  /// check if we are inside of the scope where we are supposed to be mostly recording
  virtual bool IsInCBScope() const {return false;}

  /// can 3D elements be used in the user interface?  
  virtual bool Allow3DUI() const {return true;}
  
  /// mask to identify a predicated pass
  enum PredicationMode
  {
    NoPredication=-1,
    /// depth priming pass
    PrimingPass=0,
    /// dedicated depth-only pass
    DepthPass,
    /// normal rendering pass
    RenderingPass,
    
    NPredicationModes
  };

  struct PredicatedCBScope
  {
    int beg,end;
    /// final copy is the one which is responsible for destruction - was such copy made?
    bool finalCopy;    
    bool IsEmpty() const {return beg>=end;}
    PredicatedCBScope()
    {
      // by default create invalid (empty, negative start) scope
      beg = -1;
      end = -1;
      finalCopy = true;
    }
  };
  
  virtual bool CopyCBPredicatedScope(PredicatedCBScope &scope, PredicationMode pred) {return false;}
  /*
  @return false means scope was copied instead and no further rendering should be done
    (and EndCBPredicatedScope should not be called)
  */
  virtual bool StartCBPredicatedScope(PredicatedCBScope &scope, PredicationMode pred) {return true;}
  virtual void EndCBPredicatedScope(PredicatedCBScope &scope) {}

  /// common parameters for 3D rendering of 2D elements
  struct Pars3D
  {
    /// list of active lights
    const LightList *_lights;
    /// command buffer id - see COMMAND_BUFFERS
    int _cb;
    
    Pars3D(int cb):_cb(cb){}
  };
  //@{
  /// new 3D drawing
  void Draw3D(const PositionRender &space, const Draw2DParsExt &pars, const Rect2DFloat &rect, const Rect2DFloat &clip, const Pars3D *pars3D=NULL);
  void DrawText3D(const PositionRender & space, const Point2DFloat &pos, const TextDrawAttr &attr, const char *text, const Rect2DFloat &clip, const Pars3D *pars3D=NULL);
  void DrawText3D(const PositionRender &space, const Point2DFloat &pos, const Point2DFloat &right, const Point2DFloat &down, const TextDrawAttr &attr, const char *text, const Rect2DFloat &clip, const Pars3D *pars3D=NULL);
  void DrawFrame3D(const PositionRender &space, Texture *corner, PackedColor color, const Rect2DFloat &frame, const Rect2DFloat &clip, const Pars3D *pars3D=NULL);
  void DrawLine3D(const PositionRender &space, const Line2DFloat &rect, PackedColor c0, PackedColor c1, const Rect2DFloat &clip, float width = 1.0f, int specFlags = 0, const TLMaterial &matType = TLMaterial::_default, const Pars3D *pars3D=NULL);
  void DrawLines3D(const PositionRender &space, const Line2DFloatInfo *lines, int nLines, const Rect2DFloat &clip, float width = 1.0f, int specFlags = 0, const TLMaterial &matType = TLMaterial::_default, const Pars3D *pars3D=NULL);
  virtual void DrawPoly3D(const PositionRender &space, const MipInfo &mip, const Vertex2DFloat *vertices, int nVertices, const Rect2DFloat &clip, int specFlags, const TLMaterial &matType = TLMaterial::_default, const Pars3D *pars3D=NULL);
  // draw diagnostics arrow with arrow end oriented horizontally (visible from top)
  void DrawHorizontalArrow(Vector3Par connection, Vector3Par end,PackedColor colorLinkLine, float width, int spec, float arrowSize);
  // draw diagnostics arrow with arrow end oriented vertically (visible from aside)
  void DrawVerticalArrow(Vector3Par connection, Vector3Par end,PackedColor colorLinkLine, float width, int spec, float arrowSize);
  //@}

  //@{ mouse cursor support
  /// check if mouse is supported
  virtual bool IsCursorSupported() const {return false;}
  /// if following function is not called during a frame, the cursor will be hidden
  virtual void SetCursor(Texture *cursor, int x, int y, float hsX, float hsY, PackedColor color, int shaodow) {}
  /// check if the cursor is currently expected to be shown
  virtual bool IsCursorShown() const;
  /// update cursor position - used for WM_MOUSEMOVE
  virtual void UpdateCursorPos(int x, int y) {}
  
#ifndef _SERVER
  //returns monitor resolution
  virtual RECT GetMonitorResolution() {return RECT();}
#endif

  /// notify the engine if we are owning the cursor and the engine should change its shape
  /** this is false when moving is moving in non-client are of the window in windowed mode */
  virtual void CursorOwned(bool owned) {}
  //@}

	//! to enable and disable stencil tests
	virtual void SetStencilBufferTest(int cb, bool test) {}

  //! Flag to determine the asynchronous texture loading is supported
  virtual bool IsAsynchronousTextureLoadingSupported() const {return false;}

#ifdef TRAP_FOR_0x8000
  virtual void TrapFor0x8000(const char *str) { (void *)str; }
#endif
protected:
  const char *DrawComposedChar(
    Point2DAbs &pos, Draw2DPars &pars, const Rect2DAbs &clip, float h, float w, Font *font,  const char *text
  );
  void DrawComposedChar(
    const Point2DAbs &pos, float w, float h,
    Draw2DPars &pars, const Rect2DAbs &clip, float sizeH, float sizeW, Font *font, short c
  );

  const char *DrawComposedChar3D(int cb, float &xPos, Vector3 &lPos, Char3DContext &ctx, const char *text);
  void DrawComposedChar3D(int cb, float xPos, Vector3Par lPos, float w, float h, Char3DContext &ctx, short c);
};

//! Generates S and T vectors upon a specified middleST (need not be normalized) and normal (must be normalized)
void NormalizeST(Vector3Par middleST, Vector3Par normal, Vector3 &outS, Vector3 &outT);

//! Generates S and T vectors upon a specified triangle positions and texture coordinates for A vertex
/*!
  Note that anormal must be already normalized.
*/
bool GenerateST(Vector3Compressed &anormal, Vector3Par apos, Vector3Par bpos, Vector3Par cpos,
                const UVPair &at, const UVPair &bt, const UVPair &ct,
                Vector3Compressed &outS, Vector3Compressed &outT, int bppS, int bppT);

extern Engine *GEngine;

// Note: TRAP_FOR_0x8000 when used should be defined on very global scope (it is used inside many files!)
#ifdef TRAP_FOR_0x8000
struct TrapFor0x8000Scope
{
  const char *_str;
  TrapFor0x8000Scope(const char *str)
  {
    char buf[512];
    _str = str;
    sprintf(buf, "Entering %s", str);
    if (GEngine) GEngine->TrapFor0x8000(buf);
  }
  ~TrapFor0x8000Scope()
  {
    char buf[512];
    sprintf(buf, "Leaving %s", _str);
    if (GEngine) GEngine->TrapFor0x8000(buf);
  }
};
#else
struct TrapFor0x8000Scope //Empty implementation outside ifdef TRAP_FOR_0x8000
{
  TrapFor0x8000Scope(const char *str) { (void *)str; }
}; 
#endif

#define GLOB_ENGINE ( GEngine )

#endif


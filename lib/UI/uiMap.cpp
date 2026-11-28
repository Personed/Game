// Implementation of main map display
#include "../wpch.hpp"
#include "uiMap.hpp"
//#include "win.h"
#include "../dikCodes.h"
#include "../person.hpp"
#include "../soldierOld.hpp"
#include "../diagModes.hpp"
#include <El/Common/perfProf.hpp>

#include "resincl.hpp"
#include "../keyInput.hpp"
#include "../vkCodes.h"

#include "../landscape.hpp"
#include "../roads.hpp"

#include "../camera.hpp"
#include "../detector.hpp"

#include "../Shape/mapTypes.hpp"
#include "../gameStateExt.hpp"
#include "../paramArchiveExt.hpp"
#include "../fileLocator.hpp"
#include "../gameDirs.hpp"

#include <El/QStream/qbStream.hpp>
#include <El/XML/xml.hpp>

#include <El/Common/randomGen.hpp>
#include <El/ParamArchive/paramArchiveDb.hpp>
#include <El/Evaluator/express.hpp>

#include <Es/Algorithms/qsort.hpp>

#include <time.h>

#include <El/Enum/enumNames.hpp>

#include "chat.hpp"

#include "../txtPreload.hpp"
#include "../oggText.h"

#include "../stringtableExt.hpp"
#include "../mbcs.hpp"

#include <Es/Strings/bString.hpp>
#include <ctype.h>

#include "../AI/aiRadio.hpp"

#include "uiActions.hpp"

#include "../keyInput.hpp"

#include "../saveGame.hpp"

#include "../progress.hpp"

#include "displayUI.hpp"
#include "missionDirs.hpp"

#include "../Network/network.hpp"

#include "../timeManager.hpp"

#include "uiViewport.hpp"
#include "menu.hpp"

#include "../drawText.hpp"

#include "mapProperties.hpp"
#include <lib/Inventory/recipe.hpp>

# ifdef NOMINMAX
#  undef min
#  undef max
# endif

#if _PROFILE && USER_Ondra
#pragma optimize( "", off )
#endif

/*!
\file
Implementation file for particular displays (maps, briefing, debriefing, mission editor).
*/

// static const int daysInMonth[]={31,28,31,30,31,30,31,31,30,31,30,31};
int GetDaysInMonth(int year, int month);

extern const float CameraZoom;
extern const float InvCameraZoom;

inline PackedColor ModAlpha( PackedColor color, float alpha )
{
  int a=toIntFloor(alpha*color.A8());
  saturate(a,0,255);
  return PackedColorRGB(color,a);
}

static RString GetMagazinePicture(const MagazineType *type)
{
  RString name = type->GetPictureName();
  if (name[0] == '\\')
    return name;
  else
  {
    RString prefixM = "equip\\m\\m_";
    return prefixM + name + RString(".paa");
  }
}

//LSError AbstractOptionsUI::Serialize(ParamArchive &ar) {return LSOK;}

LSError AbstractOptionsUI::Serialize(ParamArchive &ar)
{
  return LSOK;
}

///////////////////////////////////////////////////////////////////////////////
// Map (generic static control)

#define TO_INT(coef, arg) coef > 0 ? toIntFloor(arg) : toIntCeil(arg)

static void AddChar(BString<32> &buffer, char c)
{
  int n = strlen(buffer);
  if (n < 32 - 1)
  {
    buffer[n] = c;
    buffer[n + 1] = 0;
  }
}

static int Modulo(int &i, int base)
{
  int d = i >= 0 ? i / base : (i - base + 1) / base;
  int m = i - d * base;
  i = d;
  return m;
}

void GridFormat(BString<32> &buffer, RString format, int i)
{
  const char *first = NULL;
  for (const char *p=format; *p!=0; p++)
  {
    if (isalnum(*p))
    {
      first = p; break;
    }
  }
  if (!first) return;
  BString<32> revert;
  int cf = 0;
  const char *p;
  for (p=(const char *)format+format.GetLength()-1; p!=first; p--)
  {
    if (*p >= '0' && *p <= '9')
    {
      int m = Modulo(i, 10) + cf + *p - '0';
      int cf = m;
      m = Modulo(cf, 10);
      AddChar(revert, '0' + m);
    }
    else if (*p >= 'A' && *p <= 'J')
    {
      int m = Modulo(i, 10) + cf + *p - 'A';
      int cf = m;
      m = Modulo(cf, 10);
      AddChar(revert, 'A' + m);
    }
    else if (*p >= 'a' && *p <= 'j')
    {
      int m = Modulo(i, 10) + cf + *p - 'a';
      int cf = m;
      m = Modulo(cf, 10);
      AddChar(revert, 'a' + m);
    }
    else AddChar(revert, *p);
  }
  if (*p >= '0' && *p <= '9')
  {
    int m = Modulo(i, 10) + cf + *p - '0';
    int cf = m;
    m = Modulo(cf, 10);
    AddChar(revert, '0' + m);
  }
  else if (*p >= 'A' && *p <= 'Z')
  {
    int m = Modulo(i, 26) + cf + *p - 'A';
    int cf = m;
    m = Modulo(cf, 26);
    AddChar(revert, 'A' + m);
  }
  else if (*p >= 'a' && *p <= 'z')
  {
    int m = Modulo(i, 26) + cf + *p - 'a';
    int cf = m;
    m = Modulo(cf, 26);
    AddChar(revert, 'a' + m);
  }
  p--;
  for (; p>=(const char *)format; p--) AddChar(revert, *p);

  for (const char *p=revert+strlen(revert)-1; p>=(const char *)revert; p--) AddChar(buffer, *p);
}

static void AddChar(char *buffer, char c)
{
  int n = strlen(buffer);
  buffer[n] = c;
  buffer[n + 1] = 0;
}

void PositionToAA11(Vector3Val pos, char *buffer)
{
  float sizeLand = LandGrid * LandRange;
  *buffer = 0;

  if (!GWorld) return;
  const GridInfo *info = GWorld->GetGridInfo(0);
  if (!info) return;

  float offsetX = GWorld->GetGridOffsetX();
  float offsetY = GWorld->GetGridOffsetY();


  for (const char *p=info->format; *p!=0; p++)
  {
    if (*p == 'X' || *p == 'x')
    {
      float coefX = sizeLand * info->invStepX;
      int x = TO_INT(coefX, (pos.X() - offsetX) * info->invStepX);
      BString<32> buf;
      GridFormat(buf, info->formatX, coefX >= 0 ? x : x - 1);
      strcat(buffer, buf);
    }
    else if (*p == 'Y' || *p == 'y')
    {
      float coefY = sizeLand * info->invStepX;
      int y = TO_INT(coefY, (sizeLand - pos.Z() - offsetY) * info->invStepY);
      BString<32> buf;
      GridFormat(buf, info->formatY, coefY >= 0 ? y : y - 1);
      strcat(buffer, buf);
    }
    else AddChar(buffer, *p);
  }
}

void MapTypeInfo::Load(ParamEntryPar cls)
{
  icon = GlobLoadTexture
    (
    GetPictureName(cls >> "icon")
    );
  color = GetPackedColor(cls >> "color");
  size = cls >> "size";
  coefMin = cls >> "coefMin";
  coefMax = cls >> "coefMax";
  importance = cls >> "importance";
}

void MapTypeInfoTask::Load(ParamEntryPar cls)
{
  MapTypeInfo::Load(cls);

  iconCreated = GlobLoadTexture
    (
    GetPictureName(cls >> "iconCreated")
    );
  iconCanceled = GlobLoadTexture
    (
    GetPictureName(cls >> "iconCanceled")
    );
  iconDone = GlobLoadTexture
    (
    GetPictureName(cls >> "iconDone")
    );
  iconFailed = GlobLoadTexture
    (
    GetPictureName(cls >> "iconFailed")
    );

  colorCreated = GetPackedColor(cls >> "colorCreated");
  colorCanceled = GetPackedColor(cls >> "colorCanceled");
  colorDone = GetPackedColor(cls >> "colorDone");
  colorFailed = GetPackedColor(cls >> "colorFailed");
}

void LegendInfo::Load(ParamEntryPar cls)
{
  _x = cls >> "x";
  _y = cls >> "y";
  _w = cls >> "w";
  _h = cls >> "h";

  _font = GEngine->LoadFont(GetFontID(cls >> "font"));
  _size = cls >> "sizeEx";

  _colorBackground = GetPackedColor(cls >> "colorBackground");
  _color = GetPackedColor(cls >> "color");
}

MapProperties::MapProperties()
{
  _scaleMin = 0.001f;
  _scaleMax = 1.0f;
  _scaleDefault = 0.16f;

  _xMin = _zMin = -FLT_MAX;
  _xMax = _zMax = FLT_MAX;
}

void MapProperties::Load(ParamEntryPar cls)
{
  ParamEntryPtr entry = cls.FindEntry("scaleMin");
  if (entry) _scaleMin = *entry;
  entry = cls.FindEntry("scaleMax");
  if (entry) _scaleMax = *entry;
  entry = cls.FindEntry("scaleDefault");
  if (entry) _scaleDefault = *entry;

  entry = cls.FindEntry("xMin");
  if (entry) _xMin = *entry;
  entry = cls.FindEntry("xMax");
  if (entry) _xMax = *entry;
  entry = cls.FindEntry("zMin");
  if (entry) _zMin = *entry;
  entry = cls.FindEntry("zMax");
  if (entry) _zMax = *entry;

  // visuals

  RString texName = cls >> "text";
  texName = FindPicture(texName);
  if (texName.IsEmpty()) texName = "#(argb,8,8,3)color(1,1,1,1)";
  _texture = GlobLoadTextureUI(texName);
  if (_texture && _texture->getDecoder()  && !_texture->getDecoder()->isReady())
  {
    _texture->getDecoder()->reinit();
  }
  if (_texture) _texture->SetUsageType(Texture::TexUI); // no limits

  _ptsPerSquareSea = cls >> "ptsPerSquareSea";   // seas
  _ptsPerSquareTxt = cls >> "ptsPerSquareTxt";   // textures
  _ptsPerSquareCLn = cls >> "ptsPerSquareCLn";   // count-lines
  _ptsPerSquareFor = cls >> "ptsPerSquareFor";   // forests
  _ptsPerSquareForEdge = cls >> "ptsPerSquareForEdge";   // forest edges
  _ptsPerSquareRoad = cls >> "ptsPerSquareRoad";  // roads
  _ptsPerSquareObj = cls >> "ptsPerSquareObj";    // other objects

  _colorOutside = GetPackedColor(cls >> "colorOutside");

  _colorSeaPacked = GetPackedColor(cls >> "colorSea"); 
  _colorSea = Color((ColorVal)_colorSeaPacked);
  _colorForest = GetPackedColor(cls >> "colorForest");
  _colorRocks = GetPackedColor(cls >> "colorRocks");

  _colorCountlines = GetPackedColor(cls >> "colorCountlines");
  _colorCountlinesWater = GetPackedColor(cls >> "colorCountlinesWater");
  _colorMainCountlines = GetPackedColor(cls >> "colorMainCountlines");
  _colorMainCountlinesWater = GetPackedColor(cls >> "colorMainCountlinesWater");
  _colorForestBorder = GetPackedColor(cls >> "colorForestBorder");
  _colorRocksBorder = GetPackedColor(cls >> "colorRocksBorder");
  _colorPowerLines = GetPackedColor(cls >> "colorPowerLines");
  _colorRailWay = GetPackedColor(cls >> "colorRailWay");
  _colorNames = GetPackedColor(cls >> "colorNames");
  _colorLevels = GetPackedColor(cls >> "colorLevels");

  _colorTracks = GetPackedColor(cls >> "colorTracks");
  _colorRoads = GetPackedColor(cls >> "colorRoads");
  _colorMainRoads = GetPackedColor(cls >> "colorMainRoads");
  _colorTracksFill = GetPackedColor(cls >> "colorTracksFill");
  _colorRoadsFill = GetPackedColor(cls >> "colorRoadsFill");
  _colorMainRoadsFill = GetPackedColor(cls >> "colorMainRoadsFill");

  _colorGrid = GetPackedColor(cls >> "colorGrid");
  _colorGridMap = GetPackedColor(cls >> "colorGridMap");

  _fontNames = GLOB_ENGINE->LoadFont(GetFontID(cls>>"fontNames"));
  _sizeNames = cls >> "sizeExNames";
  _fontLevels = GLOB_ENGINE->LoadFont(GetFontID(cls>>"fontLevel"));
  _sizeLevels = cls >> "sizeExLevel";
  _fontGrid = GLOB_ENGINE->LoadFont(GetFontID(cls>>"fontGrid"));
  _sizeGrid = cls >> "sizeExGrid";

  if (cls.FindEntry("Legend"))
  {
    _legend.Load(cls >> "Legend");
    _showCountourInterval = cls >> "showCountourInterval";
  }
  else _showCountourInterval = false;

  MAP_TYPES_ICONS_1(MAP_TYPE_INFO_LOAD)
  MAP_TYPES_ICONS_2(MAP_TYPE_INFO_LOAD)
}

bool MapProperties::DrawTerrainOutside() const
{
  // when area is limited, do not draw terrain outside
  if (_xMin > 0) return false;
  if (_zMin > 0) return false;
  float mapSize = LandGrid * LandRange;
  if (_xMax < mapSize) return false;
  if (_zMax < mapSize) return false;

  return GLandscape->HasTerrainOutside();
}

CStatic *CreateStaticMap(ControlsContainer *parent, int idc, const ParamEntry &cls)
{
  CStaticMap *map = new CStaticMap(parent, idc, cls, cls >> "scaleMin", cls >> "scaleMax", cls >> "scaleDefault");
  map->SetVisibleRect(map->X(), map->Y(), map->W(), map->H());
  map->Center();
  return map;
}

CStaticMap::CStaticMap
  (
  ControlsContainer *parent, int idc, ParamEntryPar cls,
  float scaleMin, float scaleMax, float scaleDefault
  )
  : CStatic(parent, idc, cls)
{
  // defaults
  _defaultProperties._scaleMin = scaleMin;
  _defaultProperties._scaleMax = scaleMax;
  _defaultProperties._scaleDefault = scaleDefault;

  // load defaults from resource 
  _defaultProperties.Load(cls);

  saturate(_defaultProperties._scaleDefault, _defaultProperties._scaleMin, _defaultProperties._scaleMax);
  _scaleX = _defaultProperties._scaleDefault;

  InitDefaultCenter();

  _legendDisabled = false;

  _ptsPerSquareCost = cls >> "ptsPerSquareCost";  // cost

  _maxSatelliteAlpha = cls >> "maxSatelliteAlpha";
  _alphaFadeStartScale = cls >> "alphaFadeStartScale";
  _alphaFadeEndScale = cls >> "alphaFadeEndScale";

  //_widthRailWay = cls >> "widthRailWay";

  _colorInactive = GetPackedColor(cls >> "colorInactive");

  _fontLabel = GLOB_ENGINE->LoadFont(GetFontID(cls>>"fontLabel"));
  _sizeLabel = cls >> "sizeExLabel";
  _fontInfo = GLOB_ENGINE->LoadFont(GetFontID(cls>>"fontInfo"));
  _sizeInfo = cls >> "sizeExInfo";

  _miniMapMode = false;

  //  _moveOnEdges = cls>>"moveOnEdges";
#if _VBS3 // markers moved to CStaticMap
  _onlyDrawMarkers = cls.ReadValue("onlyDrawMarkers",false);
  _drawAttachedMarkers = cls.ReadValue("drawAttachedMarkers",true);
  _drawConditionalMarkers = cls.ReadValue("drawConditionalMarkers",true);
#endif

  EnableCtrl(true);

  _iconPlayer = GlobLoadTexture
    (
    GetPictureName(cls >> "iconPlayer")
    );
  _iconSelect = GlobLoadTexture
    (
    GetPictureName(cls >> "iconSelect")
    );
  _iconCamera = GlobLoadTexture
    (
    GetPictureName(cls >> "iconCamera")
    );
  _iconSensor = GlobLoadTexture
    (
    GetPictureName(cls >> "iconSensor")
    );
  _sizeUnit = cls >> "size";
  _sizePlayer = cls >> "sizePlayer";
  _colorFriendly = GetPackedColor(cls >> "colorFriendly");
  _colorEnemy = GetPackedColor(cls >> "colorEnemy");
  _colorCivilian = GetPackedColor(cls >> "colorCivilian");
  _colorNeutral = GetPackedColor(cls >> "colorNeutral");
  _colorUnknown = GetPackedColor(cls >> "colorUnknown");
  _colorMe = GetPackedColor(cls >> "colorMe");
  _shadow = cls >> "shadow";
  _blinkingSpeedMe = 0;
  _colorMe2 = _colorMe;
  ConstParamEntryPtr entry = cls.FindEntry("blinkingPeriodMe");
  if (entry)
  {
    float blinkingPeriod = *entry;
    if (blinkingPeriod > 0) _blinkingSpeedMe = 1.0f / blinkingPeriod;
    _colorMe2 = GetPackedColor(cls >> "colorMe2");
  }

  _colorPlayable = GetPackedColor(cls >> "colorPlayable");
  _colorSelect = GetPackedColor(cls >> "colorSelect");
  _colorSensor = GetPackedColor(cls >> "colorSensor");
  _colorDragging = GetPackedColor(cls >> "colorDragging");

  _colorCamera = GetPackedColor(cls >> "colorCamera");
  _colorMissions = GetPackedColor(cls >> "colorMissions");
  _colorActiveMission = GetPackedColor(cls >> "colorActiveMission");

  _colorPath = GetPackedColor(cls >> "colorPath");
  _colorPathDone = GetPackedColor(cls >> "colorPathDone");
  _texturePath = GlobLoadTexture(GetPictureName(cls >> "texturePath"));
  _texturePathDone = GlobLoadTexture(GetPictureName(cls >> "texturePathDone"));
  _sizePath = cls >> "sizePath";
  _sizePathDone = cls >> "sizePathDone";

  _colorInfoMove = GetPackedColor(cls >> "colorInfoMove");
  _colorGroups = GetPackedColor(cls >> "colorGroups");
  _colorActiveGroup = GetPackedColor(cls >> "colorActiveGroup");
  _colorSync = GetPackedColor(cls >> "colorSync");
  _colorLabelBackground = GetPackedColor(cls >> "colorLabelBackground");

  _cursorLineWidth = 0.5;
  entry = cls.FindEntry("cursorLineWidth");
  if (entry) _cursorLineWidth = *entry;


#if _ENABLE_CHEATS
  _show = true;
  _showCost = false;
#endif

  _showIds = false;
  _showScale = true;
  _showScaleAlpha = 1;

  _moveKey = 0;

  Reset();
  Precalculate(_defaultProperties);

  SetVisibleRect(_x, _y, _w, _h);
}

CStaticMap::~CStaticMap()
{
  _mapUsed.Free();
}

const MapProperties &CStaticMap::GetMapProperties() const
{
  if (GWorld)
  {
    const ItemMap *map = GWorld->GetMapItem();
    if (map)
    {
      const ItemMapType *type = map->Type();
      if (type) return type->GetMapProperties();
    }
  }
  return _defaultProperties;
}

#if _VBS3
Rect2DAbs CStaticMap::MapArea()
{
  float sizeLand = LandGrid * LandRange;

  // bottom left point
  float xMap = (_x - _mapX - _x) * _scaleX * sizeLand;
  float yMap = (1.0 - (_y + _h - _mapY - _y) * _scaleY) * sizeLand;

  // top right point
  float wMap = (_x + _w - _mapX - _x) * _scaleX * sizeLand;
  float hMap = (1.0 - (_y - _mapY - _y) * _scaleY) * sizeLand;

  return Rect2DAbs
    (      
    xMap,
    yMap,
    wMap - xMap,
    hMap - yMap
    );
}
#endif

void CStaticMap::SetScales(float scaleMin, float scaleMax, float scaleDefault)
{
  saturate(scaleDefault, scaleMin, scaleMax);
  _defaultProperties._scaleMin = scaleMin;
  _defaultProperties._scaleMax = scaleMax;
  _defaultProperties._scaleDefault = scaleDefault;
  SetScale(scaleDefault);
  Precalculate(_defaultProperties);
}

void CStaticMap::Reset()
{
  _moving = false;
  ClearAnimation();
  _moveKey = 0;
  _dragging = false;
  _selecting = false;
  _infoClick._type = signNone;
  _infoClickCandidate._type = signNone;
  _infoMove._type = signNone;

  //  Center();
}

// colors conversion
// TODO: move to PackedColor class

static PackedColor Mult2p00( PackedColor color )
{
  DWORD val=color;
  val&=0x7f7f7f7f;
  val<<=1;
  return PackedColor(val);
}

static PackedColor Mult1p50( PackedColor color )
{
  DWORD val=color;
  val&=0xfefefefe;
  val+=(val>>1);
  return PackedColor(val);
}

static PackedColor Mult0p75( PackedColor color )
{
  DWORD val=color;
  val&=0xfcfcfcfc;
  val+=(val>>1);
  return PackedColor(val>>1);
}

PackedColor Mult(PackedColor col1, PackedColor col2)
{
  int a = ((col1.A8() + 1) * (col2.A8() + 1) - 1) >> 8;
  int r = ((col1.R8() + 1) * (col2.R8() + 1) - 1) >> 8;
  int g = ((col1.G8() + 1) * (col2.G8() + 1) - 1) >> 8;
  int b = ((col1.B8() + 1) * (col2.B8() + 1) - 1) >> 8;
  return PackedColor(r, g, b, a);
}

PackedColor CStaticMap::InactiveColor(PackedColor color)
{
  return Mult(color, _colorInactive);
}

// coordinates conversion

DrawCoord CStaticMap::WorldToScreen(Vector3Val pt) const
{
  float invSizeLand = 1.0 / (LandGrid * LandRange);
  float xMap = pt.X() * invSizeLand * _invScaleX;
  float yMap = (1.0 - pt.Z() * invSizeLand) * _invScaleY;
  return DrawCoord(xMap + _mapX + _x, yMap + _mapY + _y);
}

DrawCoord CStaticMap::WorldSizeToScreen(Vector3Val pt) const
{
  float invSizeLand = 1.0 / (LandGrid * LandRange);
  float xMapD = pt.X() * invSizeLand * _invScaleX;
  float yMapD = - pt.Z() * invSizeLand * _invScaleY;
  return DrawCoord(xMapD,yMapD);
}

Point3 CStaticMap::ScreenToWorld(DrawCoord ptMap) const
{
  float sizeLand = LandGrid * LandRange;
  float x = (ptMap.x - _mapX - _x) * _scaleX * sizeLand;
  float y = (1.0 - (ptMap.y - _mapY - _y) * _scaleY) * sizeLand;
  // !!! WARNING: result.Y() == 0
  return Point3(x, 0, y);
}

// drawing
bool CStaticMap::DrawSign(const MapTypeInfo &info, Vector3Val pos)
{
  float coef = _invScaleX * 0.05;
  saturate(coef, info.coefMin, info.coefMax);
  float size = coef * info.size;
  return DrawSign(info.icon, info.color, pos, size, size, 0, "", NULL , -1,  info.shadow);
}

#if _VBS2 // conditional markers
bool CStaticMap::DrawSignAutosize
  (
  Texture *texture, PackedColor color,
  Vector3Val pos, float w, float h, float azimut, RString text, bool shadow,
  Font *font, float textSize, bool autosize
  )
{
  if (!autosize)
  {
    // correlates with new editor marker scaling
    w = 1 / (_scaleX / (InvLandSize * 1000)) * w;
    h = 1 / (_scaleX / (InvLandSize * 1000)) * h;
  }
  return DrawSign(texture, color, pos, w, h, azimut, text, shadow, font, textSize);
}
#endif

bool CStaticMap::DrawSign
  (
  Texture *texture, PackedColor color,
  Vector3Val pos, float w, float h, float azimut, RString text,
  Font *font, float textSize, int shadow
  )
{
  DrawCoord posMap = WorldToScreen(pos);
  return DrawSign(texture, color, posMap, w, h, azimut, text, font, textSize,false, shadow);
}

void CStaticMap::DrawLine(Vector3Val pos1, Vector3Val pos2, PackedColor color)
{
  DrawCoord posMap1 = WorldToScreen(pos1);
  DrawCoord posMap2 = WorldToScreen(pos2);
  return DrawLine(posMap1, posMap2, color);
}

void CStaticMap::DrawLine(DrawCoord posMap1, DrawCoord posMap2, PackedColor color)
{
  GEngine->DrawLine(Line2DPixel(posMap1.x * _wScreen, posMap1.y * _hScreen, posMap2.x * _wScreen, posMap2.y * _hScreen), color, color, _clipRect);
}

void CStaticMap::DrawArrow(Vector3Val pos1, Vector3Val pos2, PackedColor color)
{
  DrawCoord posMap1 = WorldToScreen(pos1);
  DrawCoord posMap2 = WorldToScreen(pos2);

  posMap1.x *= _wScreen;
  posMap1.y *= _hScreen;
  posMap2.x *= _wScreen;
  posMap2.y *= _hScreen;

  GEngine->DrawLine(Line2DPixel(posMap1.x, posMap1.y, posMap2.x, posMap2.y), color, color, _clipRect);

  float dx = posMap2.x - posMap1.x;
  float dy = posMap2.y - posMap1.y;
  float size2 = Square(dx) + Square(dy);
  float invSize = InvSqrt(size2);
  dx *= 12.0 * invSize; dy *= 12.0 * invSize;
  float x = posMap2.x - dx + 0.5 * dy;
  float y = posMap2.y - dy - 0.5 * dx;
  GEngine->DrawLine
    (
    Line2DPixel(posMap2.x, posMap2.y, x, y),
    color, color, _clipRect
    );

  x = posMap2.x - dx - 0.5 * dy;
  y = posMap2.y - dy + 0.5 * dx;
  GEngine->DrawLine
    (
    Line2DPixel(posMap2.x, posMap2.y, x, y),
    color, color, _clipRect
    );
}

bool CStaticMap::DrawSign
  (
  Texture *texture, PackedColor color,
  DrawCoord posMap, float ww, float hh, float azimut, RString text,
  Font *font, float textSize, bool textSizeConstant, int shadow
  )
{
  // default arguments
  const MapProperties &props = GetMapProperties();
  if (textSize < 0) textSize = props._sizeNames;
  if (!font) font = props._fontNames;

  // normalize w, h
  ww *= 1.0 / 640;
  hh *= 1.0 / 480;

  float size = textSize;
  if(!textSizeConstant)
  {
    size = textSize * (_invScaleX * 0.05);
    saturate(size, 0.5 * textSize, 1.0 * textSize);
  }

  float offsetX = 0.03 * size;
  float offsetY = 0.04 * size;
  PackedColor shadowColor = PackedBlack;
  shadowColor.SetA8(toIntFloor(0.8 * color.A8()));

  // picture
  if (texture)
  {
    if (azimut == 0)
    {
      // draw via Draw2D

      // center sign
      posMap.x -= 0.5 * ww;
      posMap.y -= 0.5 * hh;

      if (posMap.x + ww < _x) 
        return false;
      if (posMap.y + hh < _y)
        return false;
      if (posMap.x > _x + _w)
        return false;
      if (posMap.y > _y + _h)
        return false;

      float w = _wScreen;
      float h = _hScreen;

      MipInfo mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
      if (shadow == 1 ) GEngine->Draw2D(
        mip, shadowColor,
        Rect2DPixel(CX(posMap.x + offsetX), CY(posMap.y + offsetY),
        CX(posMap.x+ww)-CX(posMap.x), CY(posMap.y+hh)-CY(posMap.y)),
        _clipRect);
      GEngine->Draw2D(
        mip, color,
        Rect2DPixel(CX(posMap.x), CY(posMap.y),
        CX(posMap.x+ww)-CX(posMap.x), CY(posMap.y+hh)-CY(posMap.y)),
        _clipRect,shadow);

      posMap.x += ww;
      posMap.y += 0.5 * hh;
    }
    else
    {
      // draw via DrawPoly
      float a = 0.5 * ww;
      float b = 0.5 * hh;

      // TODO: improve clipping
      float r = sqrt(Square(a) + Square(b));
      if (posMap.x + r < _x) 
        return false;
      if (posMap.y + r < _y)
        return false;
      if (posMap.x - r > _x + _w)
        return false;
      if (posMap.y - r > _y + _h)
        return false;

      float s = sin(azimut);
      float c = cos(azimut);
      float cx = posMap.x * _wScreen;
      float cy = posMap.y * _hScreen;
      a *= _wScreen;
      b *= _hScreen;

      const int n = 4;
      Vertex2DPixel vs[n];
      // 0
      vs[0].x = cx + c * a + s * b;
      vs[0].y = cy + s * a - c * b;
      vs[0].u = 1;
      vs[0].v = 0;
      // 1
      vs[1].x = cx + c * a + s * (-b);
      vs[1].y = cy + s * a - c * (-b);
      vs[1].u = 1;
      vs[1].v = 1;
      // 2
      vs[2].x = cx + c * (-a) + s * (-b);
      vs[2].y = cy + s * (-a) - c * (-b);
      vs[2].u = 0;
      vs[2].v = 1;
      // 3
      vs[3].x = cx + c * (-a) + s * b;
      vs[3].y = cy + s * (-a) - c * b;
      vs[3].u = 0;
      vs[3].v = 0;

      MipInfo mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);

      if (shadow == 1)
      {
        for (int i=0; i<4; i++)
        {
          vs[i].x += offsetX * _wScreen;
          vs[i].y += offsetY * _hScreen;
          vs[i].color = shadowColor;
        }
        GEngine->DrawPoly(mip, vs, n, _clipRect);

        for (int i=0; i<4; i++)
        {
          vs[i].x -= offsetX * _wScreen;
          vs[i].y -= offsetY * _hScreen;
          vs[i].color = color;
        }
        GEngine->DrawPoly(mip, vs, n, _clipRect);
      }
      else
      {
        for (int i=0; i<4; i++)
        {
          vs[i].color = color;
        }
        GEngine->DrawPoly(mip, vs, n, _clipRect);
      }

      posMap.x += 0.5 * (fabs(c * ww) + fabs(s * hh));
    }
  }

  // text
  if (text.GetLength() > 0)
  {
    float hText = size;

    float top = posMap.y - 0.5 * hText;
    //float width = GEngine->GetTextWidth(size, font, text);

    if (shadow == 1) GEngine->DrawText
      (
      Point2DFloat(posMap.x + offsetX, top + offsetY), size,
      Rect2DFloat(_x, _y, _w, _h),
      font, shadowColor, text, 0
      );

    GEngine->DrawText
      (
      Point2DFloat(posMap.x, top), size,
      Rect2DFloat(_x, _y, _w, _h),
      font, color, text, shadow
      );
  }

  return true;
}

void CStaticMap::DrawLabel(Vector3Par pos, RString text, PackedColor color, int shadow)
{
  DrawCoord posMap = WorldToScreen(pos);

  // place label
  float w = GLOB_ENGINE->GetTextWidth(_sizeLabel, _fontLabel, text);
  float h = _sizeLabel;
  posMap.x -= 0.5 * w;
  posMap.y -= 0.01 + h;

  MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(NULL, 0, 0);
  GLOB_ENGINE->Draw2D
    (
    mip, _colorLabelBackground,
    Rect2DPixel(posMap.x * _wScreen, posMap.y * _hScreen,
    w * _wScreen, h * _hScreen),
    _clipRect
    );

  GLOB_ENGINE->DrawText
    (
    posMap, _sizeLabel,
    Rect2DFloat(_x, _y, _w, _h),
    _fontLabel, color, text, shadow
    );
}

struct DetectCircle
{
  int _x;
  int _z;
  int _radius;

  DetectCircle(int x, int z, int radius)
    :_x(x),_z(z),_radius(radius)
  {
  }

  //! check if we overlapp with given rectangle 
  bool operator () (int x, int z, int size) const
  {
    // calculate distance of rectangle center from our center
    int centerX = x+(size>>1);
    int centerZ = z+(size>>1);

    float dist2 = Square(centerX-_x) + Square(centerZ-_z);
    // check if we can quickly reject or quickly accept
    if (dist2<Square(_radius+size))
    {
      // part of the circle is certainly contained in the rectangle
      return true;
    }
    if (dist2>Square(_radius+size)*2)
    {
      // circle is certianly of of the rectangle
      return false;
    }
    // circle may or may not be in the rectangle
    // we can perform stricter check, or we can return true and provide
    // some superfluous items
    return true;
  }
};

#if USE_LANDSCAPE_LOCATIONS

//! enumerate all locations we are interested in
struct LocationsRegion
{
  // in
  float _xMin,_xMax;
  float _zMin,_zMax;

  bool operator () (int x, int y, int size) const
  {
    float xMin = x, xMax = x+size;
    float zMin = y, zMax = y+size;
    bool ret = true;
    if (_xMin>xMax) ret = false;
    if (_xMax<xMin) ret = false;
    if (_zMin>zMax) ret = false;
    if (_zMax<zMin) ret = false;
    //LogF("Testing %d,%d:%d, result %d",x,y,size,ret);
    return ret;
  }
};

struct LocationsListResult
{
  RefArray<Location, MemAllocLocal< Ref<Location>,200 > > _locations;
};

struct LocationsList
{
  // out
  LocationsListResult *_result;

  LocationsList(LocationsListResult *result)
    : _result(result)
  {
  }

  bool operator () (const Ref<LocationsItem> &item) const
  {
    for (int i=0; i<item->_locations.Size(); i++)
    {
      Location *location = item->_locations[i];
      int style = location->_type->_drawStyle;
      _result[style]._locations.Add(location);
    }
    return false;
  }
};

/*
struct SkipNear
{
const Location *_location;
float _minDist2;
LocationDrawStyle _style;

SkipNear(const Location *location, float minDist)
: _location(location),
_minDist2(Square(minDist))
{
_style = _location->_type->_drawStyle;
}

bool operator () (const Ref<LocationsItem> &item) const
{
for (int i=0; i<item->_locations.Size(); i++)
{
const Location *tested = item->_locations[i];
if (tested == _location) continue;
if (tested->_type->_drawStyle != _style) continue;
// check if the location is more important
if (tested->_importance < _location->_importance) continue;
if (tested->_importance == _location->_importance)
{
// select random (but deterministically) as more important
if (
GLandscape->GetRandomValueForCoord(toInt(tested->_position.X()), toInt(tested->_position.Z())) <=
GLandscape->GetRandomValueForCoord(toInt(_location->_position.X()), toInt(_location->_position.Z()))
)
continue;
}
// check if the location is near enough
if (tested->_position.DistanceXZ2(_location->_position) < _minDist2) return true;
}
return false;
}
};
*/

#else

//! container which makes quick space queries possible
#include <Es/Containers/quadtreeCont.hpp>

struct MountainInfo: public Vector3
{
  int _index;
  bool operator == (const MountainInfo &with) const
  {
    return _index==with._index;
  }
  MountainInfo()
  {
    _index = -1;
  }
  MountainInfo(Vector3Par pos, int index)
    :Vector3(pos),_index(index)
  {
  }

  int GetX() const {return toLargeInt(X());}
  int GetY() const {return toLargeInt(Z());}
};

TypeIsMovable(MountainInfo)

struct SkipNear
{
  MountainInfo _pos;
  //bool _skipped;
  float _minDist;
  SkipNear(MountainInfo pos, float minDist)
    :_pos(pos),
    _minDist(minDist)
  {
  }
  bool operator () (const MountainInfo &arg) const
  {
    // check if the mountain is near enough
    // check if the mountain is higher
    if (arg._index>=_pos._index) return false;
    float dist2 = _pos.DistanceXZ2(arg);
    return dist2<Square(_minDist);
  }

};


//! enumerate all mountains we are interested in
struct MountainRegion
{
  // in
  float _xMin,_xMax;
  float _zMin,_zMax;

  bool operator () (int x, int y, int size) const
  {
    float xMin = x, xMax = x+size;
    float zMin = y, zMax = y+size;
    bool ret = true;
    if (_xMin>xMax) ret = false;
    if (_xMax<xMin) ret = false;
    if (_zMin>zMax) ret = false;
    if (_zMax<zMin) ret = false;
    //LogF("Testing %d,%d:%d, result %d",x,y,size,ret);
    return ret;
  }
};

struct MountainList
{
  // out
  AutoArray<int> _indices;

  bool operator () (const MountainInfo &pos)
  {
    _indices.Add(pos._index);
    return false;
  }
};
#endif

struct CStaticMap::DrawForestContext
{
  float xMin,yMin;
  float xStep,yStep;
  int iMin,jMin;

  const MapProperties &props;

  DrawForestContext(const MapProperties &p) : props(p) {}
};

/**
Forest drawing functor
Same code is used for edge and area rendering
*/
struct CStaticMap::DrawForestsNewFunc
{
  const CStaticMap::DrawForestContext &_ctx;
  CStaticMap *_map;
  bool _drawLines;
  bool _rocks;

  DrawForestsNewFunc(const CStaticMap::DrawForestContext &ctx, CStaticMap *map, bool drawLines, bool rocks)
    :_ctx(ctx),_map(map),_drawLines(drawLines), _rocks(rocks)
  {}

  void operator () (const MapObjectList &list, int x, int z, int xSize, int zSize) const
  {
    _map->DrawForestsNew(list, x, z, _ctx, _drawLines, _rocks);
  }
};

struct CStaticMap::DrawRoadContext
{
  mutable LineArray _lines;
  CStaticMap *_map;
  const MapProperties &_props;

  DrawRoadContext(CStaticMap *map, const MapProperties &props) : _map(map), _props(props) {}

  void operator () (const MapObjectList &list, int x, int z, int xSize, int zSize) const
  {
    _map->DrawRoads(_props, _lines, list, x, z);
  }
};


struct CStaticMap::DrawObjectsContext
{
  mutable LineArray _lines;
  CStaticMap *_map;
  const MapProperties &_props;

  DrawObjectsContext(CStaticMap *map, const MapProperties &props) : _map(map), _props(props) {}

  void operator () (const MapObjectList &list, int x, int z, int xSize, int zSize) const
  {
    _map->DrawObjects(_props, list, x, z);
  }
};

template <class Func>
void ForRectangleMapObjects(int xBeg, int zBeg, int xEnd, int zEnd, const Func &f)
{
  if (xBeg<0) xBeg=0; if (xEnd>LandRangeMask) xEnd=LandRangeMask;
  if (zBeg<0) zBeg=0; if (zEnd>LandRangeMask) zEnd=LandRangeMask;
  DetectInRectangle rect(xBeg,zBeg,xEnd,zEnd);
  GLandscape->ForSomeMapObjects(rect,f);
  //  // slow and robust - usefull for ForSomeMapObjects verification
  //  for (int x=xBeg; x<xEnd; x++)
  //  for (int z=zBeg; z<zEnd; z++)
  //  {
  //    const MapObjectList &list = GLandscape->GetMapObjects(x,z);
  //    f(list,x,z,1,1);
  //  }
}

/// draw autopilot taxi guides
void CStaticMap::DrawTaxiGuide(const MapProperties &props, const AutoArray<Vector3> &guide)
{
  for (int i=1; i<guide.Size(); i++)
  {
    Vector3Val beg = guide[i-1];
    Vector3Val end = guide[i];

    DrawCoord begCoord = WorldToScreen(beg);
    DrawCoord endCoord = WorldToScreen(end);

    Line2DPixel line;
    line.beg = Point2DPixel(begCoord.x * _wScreen, begCoord.y * _hScreen);
    line.end = Point2DPixel(endCoord.x * _wScreen, endCoord.y * _hScreen);

    GEngine->DrawLine(line, props._colorRoads, props._colorRoads, _clipRect);
  }
}

void CStaticMap::DrawRunway(const MapProperties &props, const Vector3 *runway)
{
  // if runway is invalid, do not draw it
  if (runway[0].DistanceXZ2(runway[1])<Square(0.1)) return;
  if (runway[0].DistanceXZ2(runway[2])<Square(0.1)) return;
  // convert to screen space
  DrawCoord runwayTL = WorldToScreen(runway[0]);
  DrawCoord runwayTR = WorldToScreen(runway[1]);
  DrawCoord runwayBL = WorldToScreen(runway[2]);
  DrawCoord runwayBR = WorldToScreen(runway[3]);

  Point2DPixel pixelTL = Point2DPixel(runwayTL.x * _wScreen, runwayTL.y * _hScreen);
  Point2DPixel pixelTR = Point2DPixel(runwayTR.x * _wScreen, runwayTR.y * _hScreen);
  Point2DPixel pixelBL = Point2DPixel(runwayBL.x * _wScreen, runwayBL.y * _hScreen);
  Point2DPixel pixelBR = Point2DPixel(runwayBR.x * _wScreen, runwayBR.y * _hScreen);

  Line2DPixel left,right,top,bottom;
  left.beg = pixelTL, left.end = pixelBL;
  right.beg = pixelTR, right.end = pixelBR;

  top.beg = pixelTL, top.end = pixelTR;
  bottom.beg = pixelBL, bottom.end = pixelBR;

  GEngine->DrawLinePrepare();
  GEngine->DrawLineDo(left, props._colorRoads, props._colorRoads, _clipRect);
  GEngine->DrawLineDo(right, props._colorRoads, props._colorRoads, _clipRect);
  GEngine->DrawLineDo(top, props._colorRoads, props._colorRoads, _clipRect);
  GEngine->DrawLineDo(bottom, props._colorRoads, props._colorRoads, _clipRect);
}

void CStaticMap::DrawAirports(const MapProperties &props)
{
  // check ILS and taxiing information and draw runway based on it

  for (int i=0; i<GWorld->GetAirportCount(); i++)
  {
    Vector3 runway[4],taxiway[4];
    GWorld->GetAirportRunwayRect(i,runway,taxiway);
    DrawRunway(props, runway);
    DrawRunway(props, taxiway);

#if _ENABLE_CHEATS
    DrawTaxiGuide(props, GWorld->GetTaxiInPath(TWest,runway[0],VZero));
    DrawTaxiGuide(props, GWorld->GetTaxiOffPath(TWest,runway[0],VZero));
#endif
  }

}


void CStaticMap::DrawBackground(const MapProperties &props)
{
  PROFILE_SCOPE(mapDr);
  float ptsLand = 800.0 * _invScaleX * InvLandRange; // avoid dependece on video resolution
  float invPtsLand = 1.0 / ptsLand;

  float invTerrainRange = 1.0 / TerrainRange;
  float ptsTerrain = 800.0 * _invScaleX * invTerrainRange; // avoid dependece on video resolution
  float invPtsTerrain = 1.0 / ptsTerrain;

  //GEngine->ShowMessage(1000, "Scale = %.4f, PtsPerFld = %.2f", _scaleX, ptsLand);

  // calculate range of coordinates for the landscape
  float xMinBorder = _x + _mapX + _minX;
  float xMaxBorder = _x + _mapX + _maxX;
  float yMinBorder = _y + _mapY + _minY;
  float yMaxBorder = _y + _mapY + _maxY;

#if _ENABLE_CHEATS
  if (CHECK_DIAG(DEMapScale)) DIAG_MESSAGE(100, Format("Map scale: %.4f, ptsLand: %.1f", _scaleX, ptsLand));
#endif

  const bool skipOutside = !props.DrawTerrainOutside();

  if (props._texture)
  {
    // draw paper
    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(props._texture, 0, 0);

    float xStep = (1.0 / 2.0) * _invScaleX;
    float yStep = (1.0 / 2.0) * _invScaleY;
    float xMin = _x - fastFmod(-_mapX, xStep);
    float yMin = _y - fastFmod(-_mapY, yStep);

    for (float y=yMin; y<_y+_h; y+=yStep)
      for (float x=xMin; x<_x+_w; x+=xStep)
      {
        if (skipOutside)
        {
          if (x <= xMinBorder - xStep) continue;
          if (x > xMaxBorder) continue;
          if (y <= yMinBorder - yStep) continue;
          if (y > yMaxBorder) continue;
        }
        GEngine->Draw2D(
          mip, _bgColor,
          Rect2DPixel(x * _wScreen, y * _hScreen, xStep * _wScreen, yStep * _hScreen),
          _clipRect
          );
      }
  }

  //#if _ENABLE_CHEATS
  if ((!_showScale || UserMapDrawing()) && _scaleX < _alphaFadeEndScale)
  {
    // draw fields (textures)
    int iStep = toIntCeil(props._ptsPerSquareTxt * invPtsLand);
    if (iStep>1)
    {
      // once we start stepping, step over whole segments
      int zAlign;
      int satSeg = GLandscape->GetSatSegSize(zAlign);
      if (iStep<satSeg*2) iStep = satSeg;
      // there is some alignment as well
      // however, the alignment is done so that endpoint is aligned at 0
      DoAssert(LandRange%satSeg==zAlign);
    }
    int jStep = iStep;
    float xStep = iStep * InvLandRange * _invScaleX;
    float yStep = jStep * InvLandRange * _invScaleY;

    // Fix: in the implementation using fastFmod, xMin and iMin did not match sometimes
    int steps = toIntFloor(-_mapX / xStep);
    float xMin = _x + _mapX + steps * xStep; // _x - (-_mapX mod xStep)
    int iMin = steps * iStep;

    steps = toIntFloor(-_mapY / yStep);
    float yMin = _y + _mapY + steps * yStep;
    int jMin = LandRange - steps * jStep;

    // calculate mipmap level
    const float invLog4 = 0.72134752044; // 1.0 / log(4.0);
    _mipmapLevel = 7 - toIntFloor(log(xStep * _wScreen * yStep * _hScreen) * invLog4);
    if (_mipmapLevel < 0 ) _mipmapLevel = 0;
    if (_mipmapLevel > 8 ) _mipmapLevel = 8;

    float y = yMin;
    float j = jMin;
    while (y <= _y + _h)
    {
      float x = xMin;
      int i = iMin;
      while (x <= _x + _w)
      {
        if (!skipOutside || (i > -iStep && i < LandRange + iStep && j > -jStep && j < LandRange + jStep))
          DrawField(x, y, xStep, yStep, i, (int)j, iStep, -jStep);
        i += iStep;
        x += xStep;
      }
      j -= jStep;
      y += yStep;
    }
  }
  // #endif

  if (!UserMapIsBeingDrawn())
  {
    // draw sea
    int iStep = toIntCeil(props._ptsPerSquareSea * invPtsLand);
    float xStep = iStep * InvLandRange * _invScaleX;
    int jStep = iStep;
    float yStep = jStep * InvLandRange * _invScaleY;

    // Fix: in the implementation using fastFmod, xMin and iMin did not match sometimes
    int steps = toIntFloor(-_mapX / xStep);
    float xMin = _x + _mapX + steps * xStep; // _x - (-_mapX mod xStep)
    int iMin = steps * iStep;

    steps = toIntFloor(-_mapY / yStep);
    float yMin = _y + _mapY + steps * yStep;
    int jMin = LandRange - steps * jStep;

    float y = yMin;
    int j = jMin;
    while (y <= _y + _h)
    {
      float x = xMin;
      int i = iMin;
      while (x <= _x + _w)
      {
        if (!skipOutside || (i > -iStep && i < LandRange + iStep && j > -jStep && j < LandRange + jStep))
          DrawSea(props, x, y, xStep, yStep, i, j, iStep, -jStep);
        i += iStep;
        x += xStep;
      }
      j -= jStep;
      y += yStep;
    }
  }


#if _ENABLE_CHEATS
  if (_show)
#endif
  {
#if _VBS3
    if(GVBSVisuals.GetShowContours())
#endif
      // draw count-lines
        if (!UserMapIsBeingDrawn())
        {
          LineArray lines;
          //PROFILE_SCOPE(mpCnt);
          int iStep = toIntCeil(props._ptsPerSquareCLn * invPtsTerrain);
          float xStep = iStep * invTerrainRange * _invScaleX;
          int jStep = iStep;
          float yStep = jStep * invTerrainRange * _invScaleY;

          // Fix: in the implementation using fastFmod, xMin and iMin did not match sometimes
          int steps = toIntFloor(-_mapX / xStep);
          float xMin = _x + _mapX + steps * xStep; // _x - (-_mapX mod xStep)
          int iMin = steps * iStep;

          steps = toIntFloor(-_mapY / yStep);
          float yMin = _y + _mapY + steps * yStep;
          int jMin = TerrainRange - steps * jStep;

          float step = 70.0 * _scaleX;
          int stepexp = toIntFloor(log10(step));
          float step10 = pow(10.0, stepexp);
          step /= step10;
          if (step <= 2.0) step = 2.0;
          else if (step <= 5.0) step = 5.0;
          else step = 10.0;
          step *= step10;
          saturateMax(step, 1);

          float maxLevel = step * toIntCeil(10000.0 / step);

          float y = yMin;
          int j = jMin;
          while (y <= _y + _h)
          {
            float x = xMin;
            int i = iMin;
            while (x <= _x + _w)
            {
              if (!skipOutside || (i > -iStep && i < TerrainRange + iStep && j > -jStep && j < TerrainRange + jStep))
                DrawCountlines(props, lines, x, y, xStep, yStep, i, j, iStep, -jStep, step, 5*step, maxLevel);
              i += iStep;
              x += xStep;
            }
            j -= jStep;
            y += yStep;
          }
          GEngine->DrawLines(lines.Data(),lines.Size(),_clipRect);
        }

        {
          //PROFILE_SCOPE(mpObj);
          float xStep = InvLandRange * _invScaleX;
          float yStep = InvLandRange * _invScaleY;

          int iMin = toIntFloor(-_mapX / xStep);
          int jMin = LandRange - toIntFloor(-_mapY / yStep);

          // make sure viewed region is fully loaded
          if (!_mapUsed)
          {
            _mapUsed = new MapObjectListRectUsed;
          }
          // calculate object area
          int xBeg = iMin;
          int xEnd = xBeg + toIntCeil((_w + fastFmod(-_mapX, xStep))/xStep);

          int zEnd = jMin;
          int zBeg = zEnd - toIntCeil((_h + fastFmod(-_mapY, yStep))/yStep);

          // extend because the objects from neighbor fields can interfere with
          xBeg--; xEnd++; zBeg--; zEnd++;

          // saturate borders
          if (xBeg<0) xBeg = 0;
          if (xEnd>LandRangeMask) xEnd = LandRangeMask;
          if (zBeg<0) zBeg = 0;
          if (zEnd>LandRangeMask) zEnd = LandRangeMask;

          float minPtsPerSquare = floatMin(props._ptsPerSquareFor, props._ptsPerSquareRoad, props._ptsPerSquareObj);

          int xBegXXL=xBeg, zBegXXL=zBeg, xEndXXL=xEnd, zEndXXL=zEnd;
          ObjMapRadiusRectangle(xBegXXL, xEndXXL, zBegXXL, zEndXXL);
          if (ptsLand>=minPtsPerSquare)
          {
            // when no objects are needed at all, do not load them
            GLandscape->UseMapObjects(*_mapUsed,xBegXXL,zBegXXL,xEndXXL,zEndXXL);
          }

          if (ptsLand>=minPtsPerSquare*0.8f)
          {
            // when no objects are needed at all even with some zoom, do not preload them
            PROFILE_SCOPE(mapPM)

              // border should be based on the map size
              // 5 % on each side sounds reasonable
              int border=toIntCeil((zEndXXL-zBegXXL)*0.05)+1;
            // preload some border around
            GLandscape->PreloadMapObjects(*_mapUsed,xBegXXL-border,zBegXXL-border,xEndXXL+border,zEndXXL+border);
          }

          // draw objects
          if (ptsLand >= props._ptsPerSquareFor && !UserMapIsBeingDrawn())
          {
            PROFILE_SCOPE(mapFo)
            // forests
            DrawForestContext ctx(props);

            float invPtsLand = (1.0/800.0) * _scaleX * LandRange;  // avoid dependence on video resolution

            int iStep = toIntCeil(props._ptsPerSquareFor * invPtsLand);
            int jStep = iStep;

            ctx.xStep = iStep * InvLandRange * _invScaleX;
            ctx.yStep = jStep * InvLandRange * _invScaleY;

            // Fix: in the implementation using fastFmod, xMin and iMin did not match sometimes
            int steps = toIntFloor(-_mapX / xStep);
            ctx.xMin = _x + _mapX + steps * xStep; // _x - (-_mapX mod xStep)
            ctx.iMin = steps * iStep;

            steps = toIntFloor(-_mapY / yStep);
            ctx.yMin = _y + _mapY + steps * yStep;
            ctx.jMin = LandRangeMask - steps * jStep;

            if (GLandscape->GetMinTreesInForestSquare() > 0)
            {
              ForRectangleMapObjects(xBeg,zBeg,xEnd,zEnd,DrawForestsNewFunc(ctx,this,false,false));
              if (ptsLand >= props._ptsPerSquareForEdge)
              {
                // shared state preparation
                GEngine->DrawLinePrepare();
                ForRectangleMapObjects(xBeg,zBeg,xEnd,zEnd,DrawForestsNewFunc(ctx,this,true,false));
              }
            }
            else
            {
              // old style (OFP) forests no longer supported
            }

            // rocky areas
            if (GLandscape->GetMinRocksInRockSquare() > 0)
            {
              ForRectangleMapObjects(xBeg,zBeg,xEnd,zEnd,DrawForestsNewFunc(ctx,this,false,true));
              if (ptsLand >= props._ptsPerSquareForEdge)
              {
                // shared state preparation
                GEngine->DrawLinePrepare();
                ForRectangleMapObjects(xBeg,zBeg,xEnd,zEnd,DrawForestsNewFunc(ctx,this,true,true));
              }
            }
          }
          if (!UserMapIsBeingDrawn() || UserMapDrawObjects())
          {

            if (ptsLand >= props._ptsPerSquareRoad)
            {
              PROFILE_SCOPE(mapRd)
              DrawRoadContext ctx(this, props);
              ForRectangleMapObjects(xBeg,zBeg,xEnd,zEnd,ctx);
              //GLandscape->_mapObjects->ForSomeItem(ctx);
              //_mapObj
              GEngine->DrawLines(ctx._lines.Data(),ctx._lines.Size(),_clipRect);
            }
            if (ptsLand >= props._ptsPerSquareObj)
            {
              PROFILE_SCOPE(mapOb)
                DrawObjectsContext ctx(this, props);
              ForRectangleMapObjects(xBegXXL,zBegXXL,xEndXXL,zEndXXL,ctx);
            }
          }
#if 0 //_DEBUG
          _mapUsed.Free();
          // make sure no part of map is locked now - it may be cached
          for (int z=0; z<LandRange; z++) for (int x=0; x<LandRange; x++)
          {
            MapObjectListFull *list = GLandscape->GetMapObjects(x,z).GetList();
            if (list)
            {
              int cachedCount = list->IsInList();
              Assert(list->GetUseCount()==cachedCount);
            }
          }
#endif
        }
        if (!UserMapIsBeingDrawn() || UserMapDrawObjects())
        {
          DrawAirports(props);
        }

  }

  if (!UserMapIsBeingDrawn())
  {

#if USE_LANDSCAPE_LOCATIONS
    const Locations &locations = GLandscape->GetLocations();

    LocationsRegion region;
    Vector3 minPos = ScreenToWorld(Point2DFloat(_x, _y));
    Vector3 maxPos = ScreenToWorld(Point2DFloat(_x + _w,_y + _h));
    region._xMin = floatMin(minPos.X(), maxPos.X());
    region._zMin = floatMin(minPos.Z(), maxPos.Z());
    region._xMax = floatMax(minPos.X(), maxPos.X());
    region._zMax = floatMax(minPos.Z(), maxPos.Z());

    LocationsListResult lists[NLocationDrawStyle];
    {
      PROFILE_SCOPE_EX(locL,*);
      locations.ForEachInRegion(region, LocationsList(lists));
    }

    // TODO: define density in CfgLocationTypes
    const float density = 0.08;
    const float elevationOffset = GLandscape->GetLandElevationOffset();
    float sizeLand = LandGrid * LandRange;
    float minDistF = density * sizeLand * _scaleX;
    {
      PROFILE_SCOPE_EX(locD,*);
      for (int style = 0; style<NLocationDrawStyle; style++)
      {
        const LocationsListResult &list = lists[style];
        for (int i=0; i<list._locations.Size(); i++)
        {
          const Location *location = list._locations[i];
          if (location->_nearestMoreImportant >= minDistF)
            location->Draw(this,false,elevationOffset);
          /*
          // make space based query
          int x = toLargeInt(location->GetX());
          int z = toLargeInt(location->GetY());

          if (!locations.ForEachInRegion(DetectCircle(x, z, minDist), SkipNear(location, minDistF)))
          {
          location->Draw(this);
          }
          */
        }
      }
    }

#else
    // names
    ParamEntryVal world = Pars >> "CfgWorlds" >> Glob.header.worldname;
    ConstParamEntryPtr cls = world.FindEntry("Names");
    if (cls)
    {
      //PROFILE_SCOPE(mpPts);
      AUTO_STATIC_ARRAY(Vector3, points, 32);
      float minDist2 = Square(1000 * _scaleX);
      int n = cls->GetEntryCount();
      points.Resize(n);
      for (int i=0; i<n; i++)
      {
        ParamEntryVal item = cls->GetEntry(i);
        Vector3 pos;
        pos.Init();
        pos[0] = (item >> "position")[0];
        pos[2] = (item >> "position")[1];
        pos[1] = 0;
        points[i] = pos;
        bool skip = false;
        for (int j=0; j<i; j++)
        {
          if (pos.DistanceXZ2(points[j]) < minDist2)
          {
            skip = true;
            break;
          }
        }
        if (!skip) DrawName(item);
      }
    }
    else
    {
      Fail("Class <Names> expected");
    }

    // mountains
    {
      //PROFILE_SCOPE(mpMts);
#if 1

      QuadTreeCont<MountainInfo> mountainPos;
      const AutoArray<Vector3> &mountains = GLandscape->GetMountains();

      {
        PROFILE_SCOPE_EX(locC,*);
        for (int i=0; i<mountains.Size(); i++)
        {
          const Vector3& pos = mountains[i];
          // check if given pos is in the visible range
          int x = toLargeInt(pos.X());
          int z = toLargeInt(pos.Z());

          mountainPos.Set(x,z,MountainInfo(pos,i));
        }
      }

      MountainRegion region;
      Vector3 minPos = ScreenToWorld(Point2DFloat(_x,_y));
      Vector3 maxPos = ScreenToWorld(Point2DFloat(_x+_w,_y+_h));
      region._xMin = floatMin(minPos.X(),maxPos.X());
      region._zMin = floatMin(minPos.Z(),maxPos.Z());
      region._xMax = floatMax(minPos.X(),maxPos.X());
      region._zMax = floatMax(minPos.Z(),maxPos.Z());

      MountainList list;

      {
        PROFILE_SCOPE_EX(locL,*);
        mountainPos.ForEachInRegion(region,list);
      }

      float minDistF = 1000 * _scaleX;
      int minDist = toLargeInt( 1000 * _scaleX )+2;
      {
        PROFILE_SCOPE_EX(locD,*);
        for (int ii=0; ii<list._indices.Size(); ii++)
        {
          int i = list._indices[ii];
          // make space based query
          const Vector3 & pos = mountains[i];

          int x = toLargeInt(pos.X());
          int z = toLargeInt(pos.Z());
          SkipNear ctx(MountainInfo(pos,i),minDistF);

          if (!mountainPos.ForEachInRegion(DetectCircle(x,z,minDist),ctx))
          {
            DrawMount(pos);
          }
        }
      }
#endif
    }
#endif
  }

  if (skipOutside)
  {
    // borders around the map
    MipInfo mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
    float xLeft = _x;
    if (xMinBorder > xLeft)
    {
      GEngine->Draw2D(
        mip, props._colorOutside,
        Rect2DPixel(xLeft * _wScreen, _y * _hScreen, (xMinBorder - xLeft) * _wScreen, _h * _hScreen),
        _clipRect
        );
      xLeft = xMinBorder;
    }
    float xRight = _x + _w;
    if (xMaxBorder < xRight)
    {
      GEngine->Draw2D(
        mip, props._colorOutside,
        Rect2DPixel(xMaxBorder * _wScreen, _y * _hScreen, (xRight - xMaxBorder) * _wScreen, _h * _hScreen),
        _clipRect
        );
      xRight = xMaxBorder;
    }
    if (yMinBorder > _y)
    {
      GEngine->Draw2D(
        mip, props._colorOutside,
        Rect2DPixel(xLeft * _wScreen, _y * _hScreen, (xRight - xLeft) * _wScreen, (yMinBorder - _y) * _hScreen),
        _clipRect
        );
    }
    if (yMinBorder < _y + _h)
    {
      GEngine->Draw2D(
        mip, props._colorOutside,
        Rect2DPixel(xLeft * _wScreen, yMaxBorder * _hScreen, (xRight - xLeft) * _wScreen, (_y + _h - yMinBorder) * _hScreen),
        _clipRect
        );
    }
  }
}

void CStaticMap::PreloadData(Vector3Par center)
{
  // calculate a new rectangle
  float xStep = InvLandRange * _invScaleX;
  float yStep = InvLandRange * _invScaleY;
  float invXStep = LandRange * _scaleX;
  float invYStep = LandRange * _scaleY;

  int iMin = toIntFloor(-_mapX * invXStep);
  int jMin = LandRange - toIntFloor(-_mapY * invYStep);

  // make sure viewed region is fully loaded
  if (!_mapUsed)
  {
    _mapUsed = new MapObjectListRectUsed;
  }
  // calculate object area
  int xBeg = iMin;
  int xEnd = xBeg + toIntCeil((_w + fastFmod(-_mapX, xStep))*invXStep);

  int zEnd = jMin;
  int zBeg = zEnd - toIntCeil((_h + fastFmod(-_mapY, yStep))*invYStep);

  // saturate borders
  if (xBeg<0) xBeg = 0;
  if (xEnd>LandRangeMask) xEnd = LandRangeMask;
  if (zBeg<0) zBeg = 0;
  if (zEnd>LandRangeMask) zEnd = LandRangeMask;

  if (_mapUsed)
  {
    GLandscape->PreloadMapObjects(*_mapUsed, xBeg, zBeg, xEnd, zEnd);
  }
}

/*!
\patch_internal 1.45 Date 2/21/2002 by Jirka
- Fixed: Added legend to map
\patch 5126 Date 2/2/2007 by Jirka
- New: Map legend parameters are now read from the resource
*/

void CStaticMap::DrawLegend()
{
  const MapProperties &props = GetMapProperties();
  if (_legendDisabled || !props._showCountourInterval) return;

  float w = _wScreen;
  float h = _hScreen;

  float cx = CX(props._legend._x), cy = CY(props._legend._y), cw = CW(props._legend._w), ch = CH(props._legend._h);

  Draw2DPars pars;
  pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  pars.spec = NoZBuf|IsAlpha|ClampU|ClampV|IsAlphaFog;
  pars.SetColor(props._legend._colorBackground);
  pars.SetU(0,1);
  pars.SetV(0,1);
  Rect2DPixel rect(cx, cy, cw, ch);
  GEngine->Draw2D(pars, rect, _clipRect);

  PackedColor color = props._legend._color;
  GEngine->DrawLine(Line2DPixel(cx, cy, cx + cw, cy), color, color, _clipRect);
  GEngine->DrawLine(Line2DPixel(cx + cw, cy, cx + cw, cy + ch), color, color, _clipRect);
  GEngine->DrawLine(Line2DPixel(cx + cw, cy + ch, cx, cy + ch), color, color, _clipRect);
  GEngine->DrawLine(Line2DPixel(cx, cy + ch, cx, cy), color, color, _clipRect);

  float top = props._legend._y + 0.01;
  float left = props._legend._x + 0.01;

  float step = 70.0 * _scaleX;
  int stepexp = toIntFloor(log10(step));
  float step10 = pow(10.0, stepexp);
  step /= step10;
  if (step <= 2.0) step = 2.0;
  else if (step <= 5.0) step = 5.0;
  else step = 10.0;
  step *= step10;
  saturateMax(step, 1);

  GEngine->DrawTextF
    (
    Point2DFloat(left, top), props._legend._size, 
    Rect2DFloat(_x, _y, _w, _h),
    props._legend._font, color, 0, LocalizeString(IDS_MAP_INTERVAL), step
    );
  top += props._legend._size;
  GEngine->DrawText
    (
    Point2DFloat(left, top), props._legend._size, 
    Rect2DFloat(_x, _y, _w, _h),
    props._legend._font, color, LocalizeString(IDS_MAP_LEGEND_INFO),0
    );
  top += 1.5 * props._legend._size;

  // how much space is for scale picture
  float scaleSpace = props._legend._w - 5.0f * props._legend._size;
  step = LandGrid * LandRange * _scaleX * scaleSpace;
  // round to the 1, 2, 5, 10, ...
  stepexp = toIntFloor(log10(step));
  step10 = pow(10.0, stepexp);
  step /= step10;
  if (step >= 5) step = 5;
  else if (step >= 2) step = 2;
  else step = 1;
  step *= step10;

  float mapStep = InvLandGrid * InvLandRange * _invScaleX * step;

  GEngine->DrawLine(Line2DPixel(CX(left), CY(top), CX(left), CY(top + props._legend._size)), color, color, _clipRect);
  GEngine->DrawLine(Line2DPixel(CX(left + mapStep), CY(top), CX(left + mapStep), CY(top + props._legend._size)), color, color, _clipRect);
  GEngine->DrawLine(Line2DPixel(CX(left), CY(top + 0.5 * props._legend._size), CX(left + mapStep), CY(top + 0.5 * props._legend._size)), color, color, _clipRect);
  GEngine->DrawTextF
    (
    Point2DFloat(left + mapStep + 0.01, top), props._legend._size, 
    Rect2DFloat(_x, _y, _w, _h),
    props._legend._font, color, 0, LocalizeString(IDS_MAP_SCALE_FORMAT), step
    );
}

void CStaticMap::DrawName(Vector3Par pos, RString text, Font *font, float textSize, PackedColor color, int shadow)
{
  DrawCoord pt = WorldToScreen(pos);

  float size = textSize * (_invScaleX * 0.05);
  saturate(size, 0.5 * textSize, 2.0 * textSize);

  float h = size;
  float w = GEngine->GetTextWidth(size, font, text);

  if (pt.x + w < _x) return;
  if (pt.y + 0.5 * h < _y) return;
  if (pt.x > _x + _w) return;
  if (pt.y - 0.5 * h > _y + _h) return;

  GEngine->DrawText
    (
    Point2DFloat(pt.x, pt.y - 0.5 * h),
    size,
    Rect2DFloat(_x, _y, _w, _h),
    font,
    color,
    text,
    shadow
    );
}

void CStaticMap::DrawName(ParamEntryPar cls)
{
  const MapProperties &props = GetMapProperties();

  Vector3 pos;
  pos.Init();
  pos[0] = (cls >> "position")[0];
  pos[2] = (cls >> "position")[1];
  pos[1] = 0;
  RString text = cls >> "name";
  DrawName(pos, text, props._fontNames, props._sizeNames, props._colorNames, 0);
}

void CStaticMap::DrawMount(Vector3Par pos)
{
  const MapProperties &props = GetMapProperties();

  DrawCoord pt = WorldToScreen(pos);

  char buffer[256];
  sprintf(buffer, "%.0f", pos.Y());

  float h = props._sizeLevels;
  float w = GEngine->GetTextWidth(props._sizeLevels, props._fontLevels, buffer);

  if (pt.x + w + 0.005 < _x) return;
  if (pt.y + 0.5 * h < _y) return;
  if (pt.x > _x + _w) return;
  if (pt.y - 0.5 * h > _y + _h) return;

  int x = toIntFloor(pt.x * _wScreen);
  int y = toInt(pt.y * _hScreen);
  GEngine->DrawLine(Line2DPixel(x, y - 1, x + 1, y - 1), props._colorCountlines, props._colorCountlines, _clipRect);
  GEngine->DrawLine(Line2DPixel(x + 1, y - 1, x + 1, y + 1), props._colorCountlines, props._colorCountlines, _clipRect);
  GEngine->DrawLine(Line2DPixel(x + 1, y + 1, x, y + 1), props._colorCountlines, props._colorCountlines, _clipRect);
  GEngine->DrawLine(Line2DPixel(x, y + 1, x, y - 1), props._colorCountlines, props._colorCountlines, _clipRect);
  GEngine->DrawText
    (
    Point2DFloat(pt.x + 0.005, pt.y - 0.5 * h),
    props._sizeLevels,
    Rect2DFloat(_x, _y, _w, _h),
    props._fontLevels,
    props._colorLevels,
    buffer, 0
    );
}

#define HEIGHT_LOG (GLandscape->GetSubdivLog())

#define GetHeightNT(j,i) GLandscape->GetHeight((j)<<HEIGHT_LOG,(i)<<HEIGHT_LOG)

/// draw background terrain texture
void CStaticMap::DrawField(
  float x, float y, float xStep, float yStep,
  int i, int j, int iStep, int jStep
  )
{
  //if( InRange(i,j) || InRange(i+iStep,j+jStep) )
  {
    /*
    // draw only when there is a chance to draw anything

    const float waves = 5.0;
    //const float invWaves = 1.0 / (2.0 * waves);

    float heightTL = GetHeightNT(j, i);
    float heightTR = GetHeightNT(j, i + iStep);
    float heightBL = GetHeightNT(j + jStep, i);
    float heightBR = GetHeightNT(j + jStep, i + iStep);


    if(
    heightTL <= -waves && heightTR <= -waves &&
    heightBL <= -waves && heightBR <= -waves
    )
    {
    // simple case: sea only
    return;
    }
    */

    Draw2DParsExt pars;

    Texture *texture = NULL;

    pars.SetU(0, 1);
    pars.SetV(0, 1);

    Rect2DPixel rect(x * _wScreen, y * _hScreen, xStep * _wScreen, yStep * _hScreen);
    int mipLevel = _mipmapLevel;

    // load sat. map
    // where sat. map is not loaded, use white
    const Scenery::TextureInfo &info = GLandscape->ClippedTextureInfo(j+jStep,i);
    if (
      info._mat &&
      ((info._mat->GetPixelShaderID()>=PSTerrain1 && info._mat->GetPixelShaderID()<=PSTerrain15) ||
      info._mat->GetPixelShaderID()==PSTerrainX || info._mat->GetPixelShaderID()==PSTerrainSNX)
      )
    {
      // sat. map always in stage 0
      Texture *sat = info._mat->_stage[0].GetTextureForDraw();

#if _VBS3_UDEFCHART
      // If non-satellite map is to be drawn, use the user chart
      if (_showScale)
      {
        // If we are in function CStaticMap::DrawField and non-satellite map is to be drawn, it can just mean the user chart was selected
        DoAssert(UserMapDrawing());

        // Get the user chart texture name
        RStringB userChartTextureName;
        {
          RStringB satName = sat->GetName();
          int bckSlashPos = satName.ReverseFind('\\');
          if (bckSlashPos >= 0)
          {
            userChartTextureName = GWorld->GetUserDefinedChartName() + satName.Substring(bckSlashPos, satName.GetLength());
          }
        }

        // We definitely don't want to use the sat texture
        sat = NULL;

        // Replace sat texture by the user defined one
        // Check if source does exist. If it doesn't exist use white texture
        if (!userChartTextureName.IsEmpty())
        {
          // Try to find the texture in the cache of stored textures
          bool userTextureFound = false;
          for (int i = 0; i < _userTextures.Size(); i++)
          {
            if (_userTextures[i]->GetName() == userChartTextureName)
            {
              sat = _userTextures[i];
              userTextureFound = true;
              break;
            }
          }

          // If texture not found, try to search through names that don't have corresponding files
          if (!userTextureFound)
          {
            // Look for names in not-found array
            bool userTextureNotFoundFound = false;
            for (int i = 0; i < _userTexturesNotFound.Size(); i++)
            {
              if (_userTexturesNotFound[i] == userChartTextureName)
              {
                sat = NULL;
                userTextureNotFoundFound = true;
                break;
              }
            }

            // Not found even in not-found names, do the loading
            if (!userTextureNotFoundFound)
            {
              ITextureSourceFactory *factory = SelectTextureSourceFactory(userChartTextureName);
              if (factory && factory->Check(userChartTextureName))
              {
                int newTextureIndex = _userTextures.Add(GlobLoadTexture(userChartTextureName));
                sat = _userTextures[newTextureIndex];
              }
              else
              {
                _userTexturesNotFound.Add(userChartTextureName);
                // Use the white texture
                sat = NULL;
              }
            }
          }
        }
      }
#endif
      // now we want to know the tex-gen for the stage
      const TexGenInfo &tgen = info._mat->_texGen[info._mat->_stage[0]._texGen];
      Assert(tgen._uvSource==UVWorldPos);
      // compute UV from world space coordinates
      Vector3 wPosTL(i*LandGrid,0,j*LandGrid);
      Vector3 wPosTR((i+iStep)*LandGrid,0,j*LandGrid);
      Vector3 wPosBL(i*LandGrid,0,(j+jStep)*LandGrid);
      Vector3 wPosBR((i+iStep)*LandGrid,0,(j+jStep)*LandGrid);
      Vector3 uvTL = tgen._uvTransform.FastTransform(wPosTL);
      Vector3 uvTR = tgen._uvTransform.FastTransform(wPosTR);
      Vector3 uvBL = tgen._uvTransform.FastTransform(wPosBL);
      Vector3 uvBR = tgen._uvTransform.FastTransform(wPosBR);
      pars.uTL = uvTL.X(), pars.vTL = uvTL.Y();
      pars.uTR = uvTR.X(), pars.vTR = uvTR.Y();
      pars.uBL = uvBL.X(), pars.vBL = uvBL.Y();
      pars.uBR = uvBR.X(), pars.vBR = uvBR.Y();

      // Determine the mip level
      if (sat)
      {
        // mipmap is log4(texel2/pixel2)
        // we assume mapping is rectangular - same U on left, same V on top
        float texArea = fabs(pars.uBR-pars.uTL)*fabs(pars.vBR-pars.vTL);
        float pixArea = rect.w*rect.h;

        float bestArea = sat->AWidth()*sat->AHeight();

        const float invLog4 = 0.72134752044; // 1.0 / log(4.0);
        //mipLevel = toIntFloor(log(pixArea/(texArea*bestArea)) * invLog4);
        mipLevel = toIntFloor(log(texArea*bestArea/pixArea) * invLog4);
        saturate(mipLevel,0,sat->ANMipmaps());
      }
      else
      {
        mipLevel = 0;
      }
      //0.05 - 0.15
      float scaleAlpha = (_scaleX > _alphaFadeStartScale)? ((_alphaFadeEndScale - _scaleX)/(_alphaFadeEndScale - _alphaFadeStartScale)) : 1.0f;
      texture = sat;
      pars.SetColor(PackedColor(255,255,255,scaleAlpha * _maxSatelliteAlpha * 255));
    }
    else
    {
      LogF("Only TerrainX shaders supported");
      float scaleAlpha = (_scaleX > _alphaFadeStartScale)? ((_alphaFadeEndScale - _scaleX)/(_alphaFadeEndScale - _alphaFadeStartScale)) : 1.0f;
      texture = NULL;
      pars.SetColor(PackedColor(255,255,255,scaleAlpha * _maxSatelliteAlpha * 255));
    }

    pars.spec = NoZBuf|IsAlpha|IsAlphaFog|NoClamp;
    pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, mipLevel+1, mipLevel);
    GLOB_ENGINE->Draw2D(pars, rect, _clipRect);
  }
}

void CStaticMap::DrawSea(
  const MapProperties &props,
  float x, float y, float xStep, float yStep,
  int i, int j, int iStep, int jStep
  )
{
  const float waves = 5.0;
  const float invWaves = 1.0 / (2.0 * waves);

  float heightTL = GetHeightNT(j, i);
  float heightTR = GetHeightNT(j, i + iStep);
  float heightBL = GetHeightNT(j + jStep, i);
  float heightBR = GetHeightNT(j + jStep, i + iStep);
  if
    (
    heightTL >= waves &&
    heightTR >= waves &&
    heightBL >= waves &&
    heightBR >= waves
    ) return;

  Draw2DPars pars;

  if(
    heightTL <= -waves && heightTR <= -waves &&
    heightBL <= -waves && heightBR <= -waves
    )
  {
    // simple case: sea only
    pars.colorTL = props._colorSeaPacked;
    pars.colorTR = props._colorSeaPacked;
    pars.colorBL = props._colorSeaPacked;
    pars.colorBR = props._colorSeaPacked;
  }
  else
  {

    heightTL += waves; heightTL *= invWaves;
    saturate(heightTL, 0, 1);
    heightTR += waves; heightTR *= invWaves;
    saturate(heightTR, 0, 1);
    heightBL += waves; heightBL *= invWaves;
    saturate(heightBL, 0, 1);
    heightBR += waves; heightBR *= invWaves;
    saturate(heightBR, 0, 1);

    Color color, colorLand;
    colorLand = props._colorSea;
    colorLand.SetA(0);

    // TL
    color = colorLand * heightTL + props._colorSea * (1 - heightTL);
    pars.colorTL = PackedColor(color);
    // TR
    color = colorLand * heightTR + props._colorSea * (1 - heightTR);
    pars.colorTR = PackedColor(color);
    // BL
    color = colorLand * heightBL + props._colorSea * (1 - heightBL);
    pars.colorBL = PackedColor(color);
    // BR
    color = colorLand * heightBR + props._colorSea * (1 - heightBR);
    pars.colorBR = PackedColor(color);
  }

  Rect2DPixel rect(x * _wScreen, y * _hScreen, xStep * _wScreen, yStep * _hScreen);
  pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(NULL, 0, 0);
  pars.spec = NoZBuf|IsAlpha|IsAlphaFog|NoClamp;

  pars.SetU(0, 1);
  pars.SetV(0, 1);

  GLOB_ENGINE->Draw2D(pars, rect, _clipRect);
}

void CStaticMap::DrawScale
  (
  float x, float y, float xStep, float yStep,
  int i, int j, int iStep, int jStep
  )
{
  const float invMaxHeight = 1.0 / 700.0;

  float heightTL = GetHeightNT(j, i) * invMaxHeight;
  saturate(heightTL, 0, 1);
  float heightTR = GetHeightNT(j, i + iStep) * invMaxHeight;
  saturate(heightTR, 0, 1);
  float heightBL = GetHeightNT(j + jStep, i) * invMaxHeight;
  saturate(heightBL, 0, 1);
  float heightBR = GetHeightNT(j + jStep, i + iStep) * invMaxHeight;
  saturate(heightBR, 0, 1);

  Color color, colorL(0, 0, 0, 0), colorH(0, 0, 0, 1);
  Draw2DParsNoTex pars;

  // TL
  color = colorH * heightTL + colorL * (1 - heightTL);
  pars.colorTL = PackedColor(color);
  // TR
  color = colorH * heightTR + colorL * (1 - heightTR);
  pars.colorTR = PackedColor(color);
  // BL
  color = colorH * heightBL + colorL * (1 - heightBL);
  pars.colorBL = PackedColor(color);
  // BR
  color = colorH * heightBR + colorL * (1 - heightBR);
  pars.colorBR = PackedColor(color);

  Rect2DPixel rect(x * _wScreen, y * _hScreen, xStep * _wScreen, yStep * _hScreen);

  pars.spec = NoZBuf|IsAlpha|IsAlphaFog|NoClamp;
  GLOB_ENGINE->Draw2D(pars, rect, _clipRect);
}

inline static void DrawForestPoly3(
  float x1, float y1, float x2, float y2, float x3, float y3,
  PackedColor color, const Rect2DPixel &clipRect
  )
{
  const int n = 3;
  Vertex2DPixel vs[n];
  vs[0].u = 0;
  vs[0].v = 0;
  vs[0].x = x3;
  vs[0].y = y3;
  vs[0].color = color;
  vs[1].u = 0;
  vs[1].v = 0;
  vs[1].x = x2;
  vs[1].y = y2;
  vs[1].color = color;
  vs[2].u = 0;
  vs[2].v = 0;
  vs[2].x = x1;
  vs[2].y = y1;
  vs[2].color = color;
  MipInfo mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  GEngine->DrawPoly(mip, vs, n, clipRect);
}

inline static void DrawForestPoly4(
  float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4,
  PackedColor color, const Rect2DPixel &clipRect
  )
{
  const int n = 4;
  Vertex2DPixel vs[n];
  vs[0].u = 0;
  vs[0].v = 0;
  vs[0].x = x4;
  vs[0].y = y4;
  vs[0].color = color;
  vs[1].u = 0;
  vs[1].v = 0;
  vs[1].x = x3;
  vs[1].y = y3;
  vs[1].color = color;
  vs[2].u = 0;
  vs[2].v = 0;
  vs[2].x = x2;
  vs[2].y = y2;
  vs[2].color = color;
  vs[3].u = 0;
  vs[3].v = 0;
  vs[3].x = x1;
  vs[3].y = y1;
  vs[3].color = color;
  MipInfo mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  GEngine->DrawPoly(mip, vs, n, clipRect);
}

inline static void DrawForestPoly5(
  float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, float x5, float y5,
  PackedColor color, const Rect2DPixel &clipRect
  )
{
  const int n = 5;
  Vertex2DPixel vs[n];
  vs[0].u = 0;
  vs[0].v = 0;
  vs[0].x = x5;
  vs[0].y = y5;
  vs[0].color = color;
  vs[1].u = 0;
  vs[1].v = 0;
  vs[1].x = x4;
  vs[1].y = y4;
  vs[1].color = color;
  vs[2].u = 0;
  vs[2].v = 0;
  vs[2].x = x3;
  vs[2].y = y3;
  vs[2].color = color;
  vs[3].u = 0;
  vs[3].v = 0;
  vs[3].x = x2;
  vs[3].y = y2;
  vs[3].color = color;
  vs[4].u = 0;
  vs[4].v = 0;
  vs[4].x = x1;
  vs[4].y = y1;
  vs[4].color = color;
  MipInfo mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  GEngine->DrawPoly(mip, vs, n, clipRect);
}

inline static void DrawForestLine(float x1, float y1, float x2, float y2, PackedColor color, const Rect2DPixel &clipRect)
{
  GEngine->DrawLineDo(Line2DPixel(x1, y1, x2, y2),color, color, clipRect);
}

void CStaticMap::DrawForestsNew(
  const MapObjectList &list, int i, int j, const DrawForestContext &ctx, bool drawLines, bool rocks)
{
  // new style drawing (using isoarbors)
  float x = (ctx.xMin + (i - ctx.iMin) * ctx.xStep) * _wScreen;
  float y = (ctx.yMin + (ctx.jMin - j) * ctx.yStep) * _hScreen;
  float w = ctx.xStep * _wScreen;
  float h = ctx.yStep * _hScreen;

  PackedColor color = rocks ? ctx.props._colorRocks : ctx.props._colorForest;
  PackedColor colorBorder = rocks ? ctx.props._colorRocksBorder : ctx.props._colorForestBorder;

  for (int o=0; o<list.Size(); o++)
  {
    MapObject *obj = list[o];
    if (obj == NULL) continue;

    if (rocks)
    {
      if (obj->GetType() != MapRocks) continue;
    }
    else
    {
      if (obj->GetType() != MapForest) continue;
    }

    MapObjectForest *forest = static_cast<MapObjectForest *>(obj);

    int row = forest->_row;
    int col = forest->_col;

    float wGrid = w / forest->_cols;
    float hGrid = h / forest->_rows;

    float tl = forest->_tl;
    float tr = forest->_tr;
    float bl = forest->_bl;
    float br = forest->_br;
    float limit = 1.0f;

    int bits = 0;
    if (tl > limit) bits |= 8;
    if (tr > limit) bits |= 4;
    if (bl > limit) bits |= 2;
    if (br > limit) bits |= 1;
    switch (bits)
    {
    case 0:
      // nothing to draw - no forest
      // Fail("No forest");
      break;
    case 1:
      {
        // br
        float coef1 = (limit - bl) / (br - bl); 
        float coef2 = (limit - tr) / (br - tr);
        float x1 = x + (col + coef1) * wGrid;
        float y1 = y + h - (row + 1) * hGrid;
        float x2 = x + (col + 1) * wGrid;
        float y2 = y + h - (row + coef2) * hGrid;

        if (!drawLines) DrawForestPoly3(x1, y1, x2, y2, x2, y1, color, _clipRect);
        else DrawForestLine(x1, y1, x2, y2, colorBorder, _clipRect);
      }
      break;
    case 2:
      {
        // bl
        float coef1 = (limit - tl) / (bl - tl);
        float coef2 = (bl - limit) / (bl - br); 
        float x1 = x + col * wGrid;
        float y1 = y + h - (row + coef1) * hGrid;
        float x2 = x + (col + coef2) * wGrid;
        float y2 = y + h - (row + 1) * hGrid;

        if (!drawLines) DrawForestPoly3(x1, y1, x2, y2, x1, y2, color, _clipRect);
        else DrawForestLine(x1, y1, x2, y2, colorBorder, _clipRect);
      }
      break;
    case 3:
      {
        // bl, br
        float coef1 = (limit - tl) / (bl - tl);
        float coef2 = (limit - tr) / (br - tr); 
        float x1 = x + col * wGrid;
        float y1 = y + h - (row + coef1) * hGrid;
        float x2 = x + (col + 1) * wGrid;
        float y2 = y + h - (row + coef2) * hGrid;
        float y3 = y + h - (row + 1) * hGrid;

        if (!drawLines) DrawForestPoly4(x1, y1, x2, y2, x2, y3, x1, y3, color, _clipRect);
        else DrawForestLine(x1, y1, x2, y2, colorBorder, _clipRect);
      }
      break;
    case 4:
      {
        // tr
        float coef1 = (tr - limit) / (tr - br);
        float coef2 = (limit - tl) / (tr - tl); 
        float x1 = x + (col + 1) * wGrid;
        float y1 = y + h - (row + coef1) * hGrid;
        float x2 = x + (col + coef2) * wGrid;
        float y2 = y + h - row * hGrid;

        if (!drawLines) DrawForestPoly3(x1, y1, x2, y2, x1, y2, color, _clipRect);
        else DrawForestLine(x1, y1, x2, y2, colorBorder, _clipRect);
      }
      break;
    case 5:
      {
        // tr, br
        float coef1 = (limit - bl) / (br - bl);
        float coef2 = (limit - tl) / (tr - tl); 
        float x1 = x + (col + coef1) * wGrid;
        float y1 = y + h - (row + 1) * hGrid;
        float x2 = x + (col + coef2) * wGrid;
        float y2 = y + h - row * hGrid;
        float x3 = x + (col + 1) * wGrid;

        if (!drawLines) DrawForestPoly4(x1, y1, x2, y2, x3, y2, x3, y1, color, _clipRect);
        else DrawForestLine(x1, y1, x2, y2, colorBorder, _clipRect);
      }
      break;
    case 6:
      {
        // tr
        float coef1 = (tr - limit) / (tr - br);
        float coef2 = (limit - tl) / (tr - tl); 
        float x1 = x + (col + 1) * wGrid;
        float y1 = y + h - (row + coef1) * hGrid;
        float x2 = x + (col + coef2) * wGrid;
        float y2 = y + h - row * hGrid;

        if (!drawLines) DrawForestPoly3(x1, y1, x2, y2, x1, y2, color, _clipRect);
        else DrawForestLine(x1, y1, x2, y2, colorBorder, _clipRect);
      }
      {
        // bl
        float coef1 = (limit - tl) / (bl - tl);
        float coef2 = (bl - limit) / (bl - br); 
        float x1 = x + col * wGrid;
        float y1 = y + h - (row + coef1) * hGrid;
        float x2 = x + (col + coef2) * wGrid;
        float y2 = y + h - (row + 1) * hGrid;

        if (!drawLines) DrawForestPoly3(x1, y1, x2, y2, x1, y2, color, _clipRect);
        else DrawForestLine(x1, y1, x2, y2, colorBorder, _clipRect);
      }
      break;
    case 7:
      {
        // tr, bl, br
        float coef1 = (limit - tl) / (bl - tl);
        float coef2 = (limit - tl) / (tr - tl); 
        float x1 = x + col * wGrid;
        float y1 = y + h - (row + coef1) * hGrid;
        float x2 = x + (col + coef2) * wGrid;
        float y2 = y + h - row * hGrid;
        float x3 = x + (col + 1) * wGrid;
        float y3 = y + h - (row + 1) * hGrid;

        if (!drawLines) DrawForestPoly5(x1, y1, x2, y2, x3, y2, x3, y3, x1, y3, color, _clipRect);
        else DrawForestLine(x1, y1, x2, y2, colorBorder, _clipRect);
      }
      break;
    case 8:
      {
        // tl
        float coef1 = (tl - limit) / (tl - tr); 
        float coef2 = (tl - limit) / (tl - bl);
        float x1 = x + (col + coef1) * wGrid;
        float y1 = y + h - row * hGrid;
        float x2 = x + col * wGrid;
        float y2 = y + h - (row + coef2) * hGrid;

        if (!drawLines) DrawForestPoly3(x1, y1, x2, y2, x2, y1, color, _clipRect);
        else DrawForestLine(x1, y1, x2, y2, colorBorder, _clipRect);
      }
      break;
    case 9:
      {
        // tl
        float coef1 = (tl - limit) / (tl - tr); 
        float coef2 = (tl - limit) / (tl - bl);
        float x1 = x + (col + coef1) * wGrid;
        float y1 = y + h - row * hGrid;
        float x2 = x + col * wGrid;
        float y2 = y + h - (row + coef2) * hGrid;

        if (!drawLines) DrawForestPoly3(x1, y1, x2, y2, x2, y1, color, _clipRect);
        else DrawForestLine(x1, y1, x2, y2, colorBorder, _clipRect);
      }
      {
        // br
        float coef1 = (limit - bl) / (br - bl); 
        float coef2 = (limit - tr) / (br - tr);
        float x1 = x + (col + coef1) * wGrid;
        float y1 = y + h - (row + 1) * hGrid;
        float x2 = x + (col + 1) * wGrid;
        float y2 = y + h - (row + coef2) * hGrid;

        if (!drawLines) DrawForestPoly3(x1, y1, x2, y2, x2, y1, color, _clipRect);
        else DrawForestLine(x1, y1, x2, y2, colorBorder, _clipRect);
      }
      break;
    case 10:
      {
        // tl, bl
        float coef1 = (tl - limit) / (tl - tr);
        float coef2 = (bl - limit) / (bl - br); 
        float x1 = x + (col + coef1) * wGrid;
        float y1 = y + h - row * hGrid;
        float x2 = x + (col + coef2) * wGrid;
        float y2 = y + h - (row + 1) * hGrid;
        float x3 = x + col * wGrid;

        if (!drawLines) DrawForestPoly4(x1, y1, x2, y2, x3, y2, x3, y1, color, _clipRect);
        else DrawForestLine(x1, y1, x2, y2, colorBorder, _clipRect);
      }
      break;
    case 11:
      {
        // tl, bl, br
        float coef1 = (tl - limit) / (tl - tr);
        float coef2 = (limit - tr) / (br - tr);
        float x1 = x + (col + coef1) * wGrid;
        float y1 = y + h - row * hGrid;
        float x2 = x + (col + 1) * wGrid;
        float y2 = y + h - (row + coef2) * hGrid;
        float x3 = x + col * wGrid;
        float y3 = y + h - (row + 1) * hGrid;

        if (!drawLines) DrawForestPoly5(x1, y1, x2, y2, x2, y3, x3, y3, x3, y1, color, _clipRect);
        else DrawForestLine(x1, y1, x2, y2, colorBorder, _clipRect);
      }
      break;
    case 12:
      {
        // tl, tr
        float coef1 = (tr - limit) / (tr - br);
        float coef2 = (tl - limit) / (tl - bl); 
        float x1 = x + (col + 1) * wGrid;
        float y1 = y + h - (row + coef1) * hGrid;
        float x2 = x + col * wGrid;
        float y2 = y + h - (row + coef2) * hGrid;
        float y3 = y + h - row * hGrid;

        if (!drawLines) DrawForestPoly4(x1, y1, x2, y2, x2, y3, x1, y3, color, _clipRect);
        else DrawForestLine(x1, y1, x2, y2, colorBorder, _clipRect);
      }
      break;
    case 13:
      {
        // tl, tr, br
        float coef1 = (limit - bl) / (br - bl);
        float coef2 = (tl - limit) / (tl - bl); 
        float x1 = x + (col + coef1) * wGrid;
        float y1 = y + h - (row + 1) * hGrid;
        float x2 = x + col * wGrid;
        float y2 = y + h - (row + coef2) * hGrid;
        float x3 = x + (col + 1) * wGrid;
        float y3 = y + h - row * hGrid;

        if (!drawLines) DrawForestPoly5(x1, y1, x2, y2, x2, y3, x3, y3, x3, y1, color, _clipRect);
        else DrawForestLine(x1, y1, x2, y2, colorBorder, _clipRect);
      }
      break;
    case 14:
      {
        // tl, tr, bl
        float coef1 = (tr - limit) / (tr - br);
        float coef2 = (bl - limit) / (bl - br); 
        float x1 = x + (col + 1) * wGrid;
        float y1 = y + h - (row + coef1) * hGrid;
        float x2 = x + (col + coef2) * wGrid;
        float y2 = y + h - (row + 1) * hGrid;
        float x3 = x + col * wGrid;
        float y3 = y + h - row * hGrid;

        if (!drawLines) DrawForestPoly5(x1, y1, x2, y2, x3, y2, x3, y3, x1, y3, color, _clipRect);
        else DrawForestLine(x1, y1, x2, y2, colorBorder, _clipRect);
      }
      break;
    case 15:
      {
        // full forest
        float x1 = x + col * wGrid;
        float y1 = y + h - row * hGrid;
        float x2 = x + (col + 1) * wGrid;
        float y2 = y + h - (row + 1) * hGrid;

        if (!drawLines) DrawForestPoly4(x1, y1, x2, y1, x2, y2, x1, y2, color, _clipRect);
      }
      break;
    }
    /*
    // grid
    float x1 = x + col * wGrid;
    float y1 = y + h - row * hGrid;
    float x2 = x + (col + 1) * wGrid;
    float y2 = y + h - (row + 1) * hGrid;
    PackedColor color = PackedColor(Color(1, 0, 0, 1));
    GEngine->DrawLine(Line2DPixel(x1, y1, x1, y2), color, color, _clipRect);
    GEngine->DrawLine(Line2DPixel(x1, y2, x2, y2), color, color, _clipRect);
    GEngine->DrawLine(Line2DPixel(x2, y2, x2, y1), color, color, _clipRect);
    GEngine->DrawLine(Line2DPixel(x2, y1, x1, y1), color, color, _clipRect);
    */
  }
}


void CStaticMap::DrawBuilding(MapObject *obj, PackedColor color)
{
  DrawCoord mapTL = WorldToScreen(obj->GetTLPosition());
  DrawCoord mapTR = WorldToScreen(obj->GetTRPosition());
  DrawCoord mapBL = WorldToScreen(obj->GetBLPosition());
  DrawCoord mapBR = WorldToScreen(obj->GetBRPosition());

  const int n = 4;
  Vertex2DPixel vs[n];
  // 0
  vs[0].x = mapTL.x * _wScreen;
  vs[0].y = mapTL.y * _hScreen;
  vs[0].u = 0;
  vs[0].v = 0;
  vs[0].color = color;
  // 1
  vs[1].x = mapBL.x * _wScreen;
  vs[1].y = mapBL.y * _hScreen;
  vs[1].u = 0;
  vs[1].v = 0;
  vs[1].color = color;
  // 2
  vs[2].x = mapBR.x * _wScreen;
  vs[2].y = mapBR.y * _hScreen;
  vs[2].u = 0;
  vs[2].v = 0;
  vs[2].color = color;
  // 3
  vs[3].x = mapTR.x * _wScreen;
  vs[3].y = mapTR.y * _hScreen;
  vs[3].u = 0;
  vs[3].v = 0;
  vs[3].color = color;

  MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(NULL, 0, 0);
  GLOB_ENGINE->DrawPoly(mip, vs, n, _clipRect);
}

void CStaticMap::DrawObjects(const MapProperties &props, const MapObjectList &list, int i, int j)
{
  Vector3 defaultPos((i + 0.5) * LandGrid, 0, (j + 0.5) * LandGrid);
  for (int o=0; o<list.Size(); o++)
  {
    MapObject *obj = list[o];
    if (obj == NULL) continue;

    switch (obj->GetType())
    {
    case MapBuilding:
    case MapHouse:
    case MapFence:
    case MapWall:
      DrawBuilding(obj, obj->GetColor());
      break;
    case MapPowerLines:
      {
        DrawCoord mapTL = WorldToScreen(obj->GetTLPosition());
        DrawCoord mapTR = WorldToScreen(obj->GetTRPosition());
        GEngine->DrawLinePrepare();
        GEngine->DrawLineDo(
          Line2DPixel(mapTL.x * _wScreen, mapTL.y * _hScreen,
          mapTR.x * _wScreen, mapTR.y * _hScreen),
          props._colorPowerLines, props._colorPowerLines, _clipRect);
      }
      break;
    case MapRailWay:
      {        
        DrawCoord map1 = WorldToScreen(obj->GetTLPosition());
        DrawCoord map2 = WorldToScreen(obj->GetTRPosition());
        DrawCoord map3 = WorldToScreen(obj->GetBLPosition());

        GEngine->DrawLinePrepare();
        GEngine->DrawLineDo(
          Line2DPixel(map1.x * _wScreen, map1.y * _hScreen, map2.x * _wScreen, map2.y * _hScreen),
          props._colorRailWay, props._colorRailWay, _clipRect);

        if (!obj->HasMoreParts()) break;

        GEngine->DrawLinePrepare();
        GEngine->DrawLineDo(
          Line2DPixel(map2.x * _wScreen, map2.y * _hScreen,
          map3.x * _wScreen, map3.y * _hScreen),
          props._colorRailWay, props._colorRailWay, _clipRect);

      }
      break;
      // draw icon only
      MAP_TYPES_ICONS_1(MAP_TYPE_DRAW_ICON)
      // draw both icon and polygon
      MAP_TYPES_ICONS_2(MAP_TYPE_DRAW_BUILDING_AND_ICON)
    }

    if (_showIds && obj->GetType() != MapForest && obj->GetType() != MapRocks)
    {
      float ptsLand = 800.0 * _invScaleX * InvLandRange;  // avoid dependece on video resolution
      float invPtsLand = 1.0 / ptsLand;

      int iStep = toIntCeil(_ptsPerSquareCost * invPtsLand);
      float xStep = iStep * InvLandRange * _invScaleX;
      if (xStep>0.15)
      {
        // draw object ID
        DrawCoord pos = WorldToScreen(obj->GetPosition(defaultPos));
        const float size = 0.02;
#if _HELISIM_LARGE_OBJECTID
        float width = GEngine->GetTextWidthF(size, props._fontGrid, "%I64d", obj->GetId().Encode());
#else
        float width = GEngine->GetTextWidthF(size, props._fontGrid, "%d", obj->GetId().Encode());
#endif
        float x = pos.x - 0.5 * width;
        float y = pos.y - 0.5 * size;
        MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(NULL, 0, 0);
        GEngine->Draw2D
          (
          mip, PackedWhite,
          Rect2DPixel(x * _wScreen, y * _hScreen, width * _wScreen, size * _hScreen),
          Rect2DPixel(_x * _wScreen, _y * _hScreen, _w * _wScreen, _h * _hScreen)
          );
#if _HELISIM_LARGE_OBJECTID
        GEngine->DrawTextF
          (
          Point2DFloat(x, y), size, 
          Rect2DFloat(_x, _y, _w, _h),
          props._fontGrid, PackedBlack, 0, "%I64d", obj->GetId().Encode()
          );
#else
        GEngine->DrawTextF
          (
          Point2DFloat(x, y), size, 
          Rect2DFloat(_x, _y, _w, _h),
          props._fontGrid, PackedBlack, 0, "%d", obj->GetId().Encode()
          );
#endif
      }
    }
  }
}

void CStaticMap::DrawRoads(const MapProperties &props, LineArray &lines, const MapObjectList &listU, int i, int j)
{
  const MapObjectList &list = listU;
  for (int o=0; o<list.Size(); o++)
  {
    MapObject *obj = list[o];
    if (obj == NULL) continue;
    PackedColor color;
    PackedColor colorFill;
    switch (obj->GetType())
    {
    case MapTrack:
      color = props._colorTracks;
      colorFill = props._colorTracksFill;
      break;
    case MapRoad:
      color = props._colorRoads;
      colorFill = props._colorRoadsFill;
      break;
    case MapMainRoad:
      color = props._colorMainRoads;
      colorFill = props._colorMainRoadsFill;
      break;
    default:
      // not a road
      continue;
    }

    if (obj->GetType() == MapRoad || obj->GetType() == MapTrack || obj->GetType() == MapMainRoad)
    {
      // if there are many lines, we flush to avoid memory allocation
      if (lines.Size()>AllocLines-2)
      {
        GEngine->DrawLines(lines.Data(),lines.Size(),_clipRect);
        lines.Resize(0);
      }
      DrawCoord mapTL = WorldToScreen(obj->GetTLPosition());
      DrawCoord mapTR = WorldToScreen(obj->GetTRPosition());
      DrawCoord mapBL = WorldToScreen(obj->GetBLPosition());
      DrawCoord mapBR = WorldToScreen(obj->GetBRPosition());

      if (color.A8() > 0)
      {
        Line2DPixelInfo &line1 = lines.Append();
        line1.beg = Point2DPixel(mapTL.x * _wScreen, mapTL.y * _hScreen);
        line1.end = Point2DPixel(mapBL.x * _wScreen, mapBL.y * _hScreen);
        line1.c0 = color;
        line1.c1 = color;

        Line2DPixelInfo &line2 = lines.Append();
        line2.beg = Point2DPixel(mapTR.x * _wScreen, mapTR.y * _hScreen);
        line2.end = Point2DPixel(mapBR.x * _wScreen, mapBR.y * _hScreen);
        line2.c0 = color;
        line2.c1 = color;
      }
      if (colorFill.A8() > 0)
      {
        const int n = 4;
        Vertex2DPixel vs[n];
        // 0
        vs[0].x = mapTL.x * _wScreen;
        vs[0].y = mapTL.y * _hScreen;
        vs[0].u = 0;
        vs[0].v = 0;
        // 1
        vs[1].x = mapBL.x * _wScreen;
        vs[1].y = mapBL.y * _hScreen;
        vs[1].u = 1;
        vs[1].v = 0;
        // 2
        vs[2].x = mapBR.x * _wScreen;
        vs[2].y = mapBR.y * _hScreen;
        vs[2].u = 1;
        vs[2].v = 1;
        // 3
        vs[3].x = mapTR.x * _wScreen;
        vs[3].y = mapTR.y * _hScreen;
        vs[3].u = 0;
        vs[3].v = 1;

        MipInfo mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
        for (int i=0; i<4; i++)
        {
          vs[i].color = colorFill;
        }
        GEngine->DrawPoly(mip, vs, n, _clipRect);
      }
    }
  }
}

/*!
\patch 1.62 Date 5/31/2002 by Ondra
- Fixed: Map contour drawing was not correct in some places
with highly variable gradient.
*/

void CStaticMap::DrawLines
  (
  LineArray &lines, DrawCoord *pt, float *height,
  float step, float stepMain, float minLevel, float maxLevel, PackedColor color, PackedColor colorMain
  )
{
  float invStep = 1.0 / step;

  int n0, n1, n2, nt;
  float xs, ys, xe, ye;
  // original (wrong):
  // draw triangle <0, 1, 2>
  // draw triangle <1, 2, 3>
  // correct:
  // draw triangle <0, 1, 3>
  // draw triangle <0, 2, 3>
  for (int t=0; t<2; t++)
  {
    // t = 0, 1
    // draw triangel <t, t+ 1, t+2>
    n0 = 0; n1 = t + 1; n2 = 3;
    // sort vertices by height
    if (height[n0] > height[n1])
    { // swap n0, n1
      nt = n0; n0 = n1; n1 = nt;
    }
    if (height[n0] > height[n2])
    { // swap n0, n2
      nt = n0; n0 = n2; n2 = nt;
    }
    if (height[n1] > height[n2])
    { // swap n1, n2
      nt = n1; n1 = n2; n2 = nt;
    }

    // fastCeil
    float level = step * toIntCeil(height[n0] * invStep);
    saturateMax(level, minLevel);
    float toLevel = floatMin(height[n2], maxLevel);
    for (; level<=toLevel; level+=step)
    {
      PackedColor col;
      if (fastFmod(level + 0.5 * step, stepMain) < step)
        col = colorMain;
      else
        col = color;
      if (height[n2] == height[n0])
      {
        // all 3 points have the same height - draw the triangle
        {
          Line2DPixelInfo &line = lines.Append();
          line.beg = Point2DPixel(pt[n0].x * _wScreen, pt[n0].y * _hScreen);
          line.end = Point2DPixel(pt[n1].x * _wScreen, pt[n1].y * _hScreen);
          line.c0 = col;
          line.c1 = col;
        }
        {
          Line2DPixelInfo &line = lines.Append();
          line.beg = Point2DPixel(pt[n1].x * _wScreen, pt[n1].y * _hScreen);
          line.end = Point2DPixel(pt[n2].x * _wScreen, pt[n2].y * _hScreen);
          line.c0 = col;
          line.c1 = col;
        }
        {
          Line2DPixelInfo &line = lines.Append();
          line.beg = Point2DPixel(pt[n2].x * _wScreen, pt[n2].y * _hScreen);
          line.end = Point2DPixel(pt[n0].x * _wScreen, pt[n0].y * _hScreen);
          line.c0 = col;
          line.c1 = col;
        }
        continue;
      }
      // draw one line (at level <level>)
      float coef = (level - height[n0]) * (1.0 / (height[n2] - height[n0]));
      xe = pt[n0].x + coef * (pt[n2].x - pt[n0].x);
      ye = pt[n0].y + coef * (pt[n2].y - pt[n0].y);
      if (level == height[n1])
      {
        xs = pt[n1].x;
        ys = pt[n1].y;
      }
      else if (level < height[n1])
      {
        float coef = (level - height[n0]) * (1.0 / (height[n1] - height[n0]));
        xs = pt[n0].x + coef * (pt[n1].x - pt[n0].x);
        ys = pt[n0].y + coef * (pt[n1].y - pt[n0].y);
      }
      else
      {
        float coef = (level - height[n1]) * (1.0 / (height[n2] - height[n1]));
        xs = pt[n1].x + coef * (pt[n2].x - pt[n1].x);
        ys = pt[n1].y + coef * (pt[n2].y - pt[n1].y);
      }
      // draw line from <xs, ys> to <xe, ye>
      Line2DPixelInfo &line = lines.Append();
      line.beg = Point2DPixel(xs * _wScreen, ys * _hScreen);
      line.end = Point2DPixel(xe * _wScreen, ye * _hScreen);
      line.c0 = col;
      line.c1 = col;
    }
  }
}

/** draw map contour lines */

void CStaticMap::DrawCountlines(
  const MapProperties &props, LineArray &lines,
  float x, float y, float xStep, float yStep,
  int i, int j, int iStep, int jStep,
  float step, float stepMain, float maxLevel
  )
{
  // if there are many lines, we flush to avoid memory allocation
  if (lines.Size()>AllocLines-2)
  {
    GEngine->DrawLines(lines.Data(),lines.Size(),_clipRect);
    lines.Resize(0);
  }
  // insert bounds into fields
  DrawCoord pt[4];
  float height[4];
  pt[0].x = x; pt[0].y = y;
  pt[1].x = x + xStep; pt[1].y = y;
  pt[2].x = x; pt[2].y = y + yStep;
  pt[3].x = x + xStep; pt[3].y = y + yStep;
  height[0] = GLOB_LAND->ClippedDataXZ(i, j);
  height[1] = GLOB_LAND->ClippedDataXZ(i + iStep, j);
  height[2] = GLOB_LAND->ClippedDataXZ(i, j + jStep);
  height[3] = GLOB_LAND->ClippedDataXZ(i + iStep, j + jStep);

  DrawLines(lines, pt, height, step, stepMain, -maxLevel, 0.5 * step, props._colorCountlinesWater, props._colorMainCountlinesWater);
  DrawLines(lines, pt, height, step, stepMain, step, maxLevel, props._colorCountlines, props._colorMainCountlines);

}

/*!
\patch_internal 1.45 Date 2/21/2002 by Jirka
- Improved: Map grid format controlled by config
*/

void CStaticMap::DrawGrid(const MapProperties &props)
{
  float sizeLand = LandGrid * LandRange;
  float invSizeLand = 1.0f / sizeLand;

  const GridInfo *info = GWorld->GetGridInfo(_scaleX);
  if (!info) return;

  float offsetX = GWorld->GetGridOffsetX();
  float offsetY = GWorld->GetGridOffsetY();

  float coefX = sizeLand * info->invStepX;
  float invCoefX = invSizeLand * info->stepX;

  float coefY = sizeLand * info->invStepY;
  float invCoefY = invSizeLand * info->stepY;

  float minX = floatMax(-_mapX, _minX);
  float maxX = floatMin(_w - _mapX, _maxX);
  float minY = floatMax(-_mapY, _minY);
  float maxY = floatMin(_h - _mapY, _maxY);
  Rect2DFloat rectFloat(_x + _mapX + minX, _y + _mapY + minY, maxX - minX, maxY - minY);
  Rect2DPixel rect(rectFloat.x * _wScreen, rectFloat.y * _hScreen, rectFloat.w * _wScreen, rectFloat.h * _hScreen);

  int xFrom = TO_INT(coefX, (minX * _scaleX * sizeLand - offsetX) * info->invStepX);
  int zFrom = TO_INT(coefY, (minY * _scaleY * sizeLand - offsetY) * info->invStepY);
  int xTo = TO_INT(coefX, (maxX * _scaleX * sizeLand - offsetX) * info->invStepX);
  int zTo = TO_INT(coefY, (maxY * _scaleY * sizeLand - offsetY) * info->invStepY);

  float xBeg = _x + _mapX + (xFrom * info->stepX + offsetX) * _invScaleX * invSizeLand;
  float zBeg = _y + _mapY + (zFrom * info->stepY + offsetY) * _invScaleY * invSizeLand;
  float xFld = fabs(_invScaleX * invCoefX);
  float zFld = fabs(_invScaleY * invCoefY);

  float h = props._sizeGrid;
  const float textBorder = TEXT_BORDER;

  float zAct = zBeg;
  int zSign = coefY > 0 ? 1 : -1; 
  AspectSettings asp;
  GEngine->GetAspectSettings(asp);

  for (int z = zFrom; zSign * (zTo - z) >= 0; z+=zSign)
  {
    BString<32> buffer;
    GridFormat(buffer, info->formatY, zSign == 1 ? z : z - 1);

    float width = GEngine->GetTextWidth(props._sizeGrid, props._fontGrid, buffer);

    float offset = _gridNumbersOverLines ?  (width + textBorder + 0.001) * _wScreen : 0;

    if (zAct >= rectFloat.y && zAct <= rectFloat.y + rectFloat.h)
    {
      GLOB_ENGINE->DrawLine
        (
        Line2DPixel(rect.x + offset, zAct * _hScreen,
        rect.x + rect.w - offset, zAct * _hScreen),
        props._colorGridMap, props._colorGridMap
        );
      if(!(z % 10))
        GLOB_ENGINE->DrawLine
        (
        Line2DPixel(rect.x + offset, zAct * _hScreen +1,
        rect.x + rect.w - offset, zAct * _hScreen +1),
        props._colorGridMap, props._colorGridMap
        );
    }

    //wide screen map-coordinates position
    float ratio =(float)GEngine->WidthBB()/std::max(GEngine->Width2D(),1);
    float sfX =  (-asp.uiTopLeftX) * ratio;

#if _VBS3_UDEFCHART
    float left = rectFloat.x + textBorder;
    float right = rectFloat.x + rectFloat.w - width - textBorder;
#else
    float left = std::max(rectFloat.x, sfX) + textBorder;
    float right = std::min(rectFloat.x + rectFloat.w, 1-sfX) - width - textBorder;
#endif
    float top = _gridNumbersOverLines ? (zAct - 0.5 * props._sizeGrid) : (zAct + 0.5 * (zFld - h));

    GLOB_ENGINE->DrawText
      (
      Point2DFloat(left, top), props._sizeGrid,
      rectFloat,
      props._fontGrid, props._colorGrid, buffer, 0
      );
    GLOB_ENGINE->DrawText
      (
      Point2DFloat(right, top), props._sizeGrid,
      rectFloat,
      props._fontGrid, props._colorGrid, buffer, 0
      );
    zAct += zFld;
  }

  float xAct = xBeg;
  int xSign = coefX > 0 ? 1 : -1; 

  float offset = _gridNumbersOverLines ?  props._sizeGrid * _hScreen : 0;

  for (int x = xFrom; xSign * (xTo - x) >= 0; x+=xSign)
  {
    if (xAct >= rectFloat.x && xAct <= rectFloat.x + rectFloat.w)
    {
      GLOB_ENGINE->DrawLine
        (
        Line2DPixel(xAct * _wScreen, rect.y + offset,
        xAct * _wScreen, rect.y + rect.h - offset),
        props._colorGridMap, props._colorGridMap
        );
      if(!(x % 10))
        GLOB_ENGINE->DrawLine
        (
        Line2DPixel(xAct * _wScreen+1, rect.y + offset,
        xAct * _wScreen+1, rect.y + rect.h - offset),
        props._colorGridMap, props._colorGridMap
        );
    }

    BString<32> buffer;
    GridFormat(buffer, info->formatX, xSign == 1 ? x : x - 1);

    float left = _gridNumbersOverLines ? 
      xAct - 0.5 * GEngine->GetTextWidth(props._sizeGrid, props._fontGrid, buffer)
      : xAct + 0.5 * (xFld - GEngine->GetTextWidth(props._sizeGrid, props._fontGrid, buffer));

    //wide screen map-coordinates position
    float ratio =(float)GEngine->WidthBB()/std::max(GEngine->Width2D(),1);
    float sfY =  (-asp.uiTopLeftY) * ratio;

#if _VBS3_UDEFCHART
    float top = rectFloat.y;
    float bottom = rectFloat.y + _h - _sizeGrid;
#else
    float top = std::max(rectFloat.y, sfY);
    float bottom = std::min(rectFloat.y + rectFloat.h, 1-sfY) - props._sizeGrid;
#endif
    GLOB_ENGINE->DrawText
      (
      Point2DFloat(left, top), props._sizeGrid,
      rectFloat,
      props._fontGrid, props._colorGrid, buffer, 0
      ); 
    GLOB_ENGINE->DrawText
      (
      Point2DFloat(left, bottom), props._sizeGrid,
      rectFloat,
      props._fontGrid, props._colorGrid, buffer, 0
      );
    xAct += xFld;
  }
}

void CStaticMap::OnHide()
{
  _mapUsed.Free();
}

//static ActionResponseCurve MapScrollX(0.20,1,2.0);
//static ActionResponseCurve MapScrollY(0.20,1,2.0);

void CStaticMap::OnDraw(UIViewport *vp, float alpha)
{
  const MapProperties &props = GetMapProperties();
  SaturateScale(props); // includes Precalculate

  float dt = Glob.uiTime - _moveLast;
  saturateMin(dt, 0.1); // minimal fps 10 - avoid jumps of cursor

  bool canAnimate = true;

  if (static_cast<Display *>(_parent.GetLink()) != GWorld->Map() || GWorld->IsShowMapWanted()) // check only for in-game map
  {
    if (_moveKey != 0)
    {
      _moveLast = Glob.uiTime;
      if (Glob.uiTime - _moveStart < 0.5) dt *= 0.5;

      switch (_moveKey)
      {
      case DIK_ADD:
        SetScale(exp(-dt) * GetScale());
        break;
      case DIK_SUBTRACT:
        SetScale(exp(dt) * GetScale());
        break;
      case DIK_NUMPAD1:
        _mapX += dt; SaturateX(_mapX);
        _mapY -= dt; SaturateY(_mapY);
        break;
      case DIK_NUMPAD2:
        _mapY -= dt; SaturateY(_mapY);
        break;
      case DIK_NUMPAD3:
        _mapX -= dt; SaturateX(_mapX);
        _mapY -= dt; SaturateY(_mapY);
        break;
      case DIK_NUMPAD4:
        _mapX += dt; SaturateX(_mapX);
        break;
      case DIK_NUMPAD6:
        _mapX -= dt; SaturateX(_mapX);
        break;
      case DIK_NUMPAD7:
        _mapX += dt; SaturateX(_mapX);
        _mapY += dt; SaturateY(_mapY);
        break;
      case DIK_NUMPAD8:
        _mapY += dt; SaturateY(_mapY);
        break;
      case DIK_NUMPAD9:
        _mapX -= dt; SaturateX(_mapX);
        _mapY += dt; SaturateY(_mapY);
        break;
      }
      canAnimate = false;
    }
  }

  if (canAnimate && _interpolator)
  {
    float t = Glob.uiTime - _animationStart;
    Vector3 pos = (*_interpolator)(t).Position();
    _scaleX = exp(pos[1]);
    pos[1] = 0;
    saturate(_scaleX, props._scaleMin, props._scaleMax);
    Precalculate(props);
    Center(pos, false);
    _animationLast = Glob.uiTime;
    if (t > _interpolator->MaxArg())
      ClearAnimation();
    else
    {
      float predictTime = 0.2f;
      Vector3 predictPos = (*_interpolator)(t+predictTime).Position();
      PreloadData(predictPos);
    }
  }

  DrawBackground(props);

  if (!UserMapIsBeingDrawn())
  {
#if _ENABLE_CHEATS
    if (_show)
#endif
#if _VBS3
      if(GVBSVisuals.GetShowGrid())
#endif
        DrawGrid(props);
  }
  DrawExt(alpha);
  OnEvent(CEDraw);

  if (_parent && _parent->IsTop() && !GWorld->GetCameraEffect())
  {
    RString cursorName = _parent->GetCursorName();
    if (cursorName.GetLength() > 0 && stricmp(cursorName, "Arrow") != 0)
    {
      int wScreen = GLOB_ENGINE->Width2D();
      int hScreen = GLOB_ENGINE->Height2D();
      const float mouseH = 16.0 / 600;
      const float mouseW = 16.0 / 800;
      float mouseX = 0.5 + GInput.cursorX * 0.5;
      float mouseY = 0.5 + GInput.cursorY * 0.5;
      PackedColor color = _parent->GetCursorColor();
      MipInfo mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
      float pt = mouseX - mouseW;
      if (pt > _x)
        GEngine->Draw2D
        (
        mip, color,
        Rect2DPixel(wScreen * _x, hScreen * mouseY - 0.5 * _cursorLineWidth, wScreen * (pt - _x), _cursorLineWidth)
        );
      pt = mouseX + mouseW;
      if (pt < _x + _w)
        GEngine->Draw2D
        (
        mip, color,
        Rect2DPixel(wScreen * pt, hScreen * mouseY - 0.5 * _cursorLineWidth, wScreen * (_x + _w - pt), _cursorLineWidth)
        );
      pt = mouseY - mouseH;
      if (pt > _y)
        GEngine->Draw2D
        (
        mip, color,
        Rect2DPixel(wScreen * mouseX - 0.5 * _cursorLineWidth, hScreen * _y, _cursorLineWidth, hScreen * (pt - _y))
        );
      pt = mouseY + mouseH;
      if (pt < _y + _h)
        GEngine->Draw2D
        (
        mip, color,
        Rect2DPixel(wScreen * mouseX - 0.5 * _cursorLineWidth, hScreen * pt, _cursorLineWidth, hScreen * (_y + _h - pt))
        );
    }
  }
}

// mouse / keyboard events
bool CStaticMap::OnKeyUp(int dikCode)
{
  switch (dikCode)
  {
  case DIK_ADD:
  case DIK_SUBTRACT:
  case DIK_NUMPAD1:
  case DIK_NUMPAD2:
  case DIK_NUMPAD3:
  case DIK_NUMPAD4:
  case DIK_NUMPAD6:
  case DIK_NUMPAD7:
  case DIK_NUMPAD8:
  case DIK_NUMPAD9:
    // case DIK_NUMPAD5:
    _moveKey = 0;
    return true;
  default:
    return false;
  }
}

bool CStaticMap::OnKeyDown(int dikCode)
{
  const MapProperties &props = GetMapProperties();

  switch (dikCode)
  {
  case DIK_ADD:
  case DIK_SUBTRACT:
  case DIK_NUMPAD1:
  case DIK_NUMPAD2:
  case DIK_NUMPAD3:
  case DIK_NUMPAD4:
  case DIK_NUMPAD6:
  case DIK_NUMPAD7:
  case DIK_NUMPAD8:
  case DIK_NUMPAD9:
    ClearAnimation();
    if (_moveKey != dikCode)
    {
      _moveKey = dikCode;
      _moveStart = Glob.uiTime;
      _moveLast = Glob.uiTime;
    }
    return true;
    /*
    case DIK_NUMPAD5:
    Center();
    return true;
    */
  case DIK_MULTIPLY:
    _moveKey = 0;
    float time;
    if (props._scaleDefault > _scaleX)
      time = log(props._scaleDefault / _scaleX);
    else
      time = log(_scaleX / props._scaleDefault);
    ClearAnimation();
    AddAnimationPhase(time, props._scaleDefault, GetCenter());
    CreateInterpolator();
    return true;
  default:
    return false;
  }
}

void ExportWMF(const char *name, bool grid
#if _VBS3
               ,bool contour = true
#endif
               );

void CStaticMap::ProcessCheats()
{
  if (GInput.CheatActivated() == CheatExportMap)
  {
    char buffer[256];
    sprintf(buffer, "C:\\%s.emf", Glob.header.worldname);
    ExportWMF(buffer, true);
    GInput.CheatServed();
  }
#if _ENABLE_CHEATS
  if (GInput.GetCheat2ToDo(DIK_D))
  {
    char buffer[256];
    sprintf(buffer, "C:\\%s.emf", Glob.header.worldname);
    ExportWMF(buffer, true);
  }
  if (GInput.GetCheat2ToDo(DIK_F))
  {
    char buffer[256];
    sprintf(buffer, "C:\\%s.emf", Glob.header.worldname);
    ExportWMF(buffer, false);
  }
  /*
  if (GInput.GetCheat1ToDo(DIK_V))
  {
  _showCost = !_showCost;
  }
  */
  /*
  if (GInput.GetCheat1ToDo(DIK_B))
  {
  _showScale = !_showScale;
  }
  */
  if (GInput.GetCheat1ToDo(DIK_N))
  {
    _show = !_show;
  }
  if (GInput.GetCheat1ToDo(DIK_W))
  {
    switch (_infoClick._type)
    {
    case signUnit:
      if (_infoClick._unit)
      {
        EntityAI *veh = _infoClick._unit->GetVehicle();
        if (veh)
        {
          GLOB_WORLD->SwitchCameraTo(veh, GLOB_WORLD->GetCameraType(), true);
          GWorld->UI()->ResetHUD();
        }
      }
      break;
    case signVehicle:
    case signStatic:
      if (_infoClick._id!=NULL)
      {
        GLOB_WORLD->SwitchCameraTo(_infoClick._id, GLOB_WORLD->GetCameraType(), true);
        GWorld->UI()->ResetHUD();
      }
      break;
    }
  }
#endif
}

void CStaticMap::OnRButtonDown(float x, float y)
{
  _mouseWorld = ScreenToWorld(DrawCoord(x, y));
  _moving = true;
}

void CStaticMap::OnRButtonUp(float x, float y)
{
  _moving = false;
}

void CStaticMap::OnMouseHold(float x, float y, bool active)
{
  if (!_moving && IsInside(x, y))
    _infoMove = FindSign(x, y);
}

void CStaticMap::OnMouseMove(float x, float y, bool active)
{
  if (_moving)
  {
    DrawCoord mouseMap = WorldToScreen(_mouseWorld);
    _mapX += x - mouseMap.x;
    SaturateX(_mapX);
    _mapY += y - mouseMap.y;
    SaturateY(_mapY);
  }
  else
  {
    OnMouseHold(x, y);
    return;
  }
}

void CStaticMap::OnMouseZChanged(float dz)
{
  if (dz == 0) return;

  _moveKey = 0;
  float scale = exp(0.1 * dz) * _scaleX;
  SetScale(scale);
}

void CStaticMap::ClearAnimation()
{
  _animation.Resize(0);
  _animMatrices.Free();
  _animTimes.Free();
  _interpolator = NULL;
}

void CStaticMap::AddAnimationPhase(float dt, float scale, Vector3Par pos)
{
  int i = _animation.Add();
  MapAnimationPhase &phase = _animation[i];
  if (i == 0)
  {
    _animationStart = Glob.uiTime;
    _animationLast = Glob.uiTime;
    phase.time = Glob.uiTime + dt;
  }
  else
  {
    phase.time = _animation[i - 1].time + dt;
  }
  phase.scale = scale;
  phase.pos = pos;
}

void CStaticMap::CreateInterpolator()
{
  int n = _animation.Size();

  _animTimes.Realloc(n + 1);
  _animMatrices.Realloc(n + 1);
  Vector3 pos;

  pos = GetCenter();
  pos[1] = log(_scaleX);
  _animTimes[0] = 0;
  _animMatrices[0].SetPosition(pos);
  _animMatrices[0].SetOrientation(M3Identity);
  for (int i=0; i<n; i++)
  {
    pos = _animation[i].pos;
    pos[1] = log(_animation[i].scale);
    _animTimes[i + 1] = _animation[i].time - _animationStart;
    _animMatrices[i + 1].SetPosition(pos);
    _animMatrices[i + 1].SetOrientation(M3Identity);
  }
  _interpolator = new InterpolatorLinear(_animMatrices, _animTimes, n + 1);
}

Vector3 CStaticMap::GetCenter()
{
  DrawCoord pt(_center.x + _x, _center.y + _y);
  return ScreenToWorld(pt);
}

// implementation
bool CStaticMap::UserMapDrawing() const
{
#if _VBS3_UDEFCHART
  return !GWorld->GetUserDefinedChartName().IsEmpty();
#else
  return false;
#endif
}

bool CStaticMap::UserMapIsBeingDrawn() const
{
#if _VBS3_UDEFCHART
  return UserMapDrawing() && _showScale;
#else
  return false;
#endif
}

bool CStaticMap::UserMapDrawObjects() const
{
#if _VBS3_UDEFCHART
  return GWorld->GetDrawObjectsOnUserDefinedChart();
#else
  return false;
#endif
}

void CStaticMap::SaturateScale(const MapProperties &props)
{
  if (_scaleX >= props._scaleMin && _scaleX <= props._scaleMax)
  {
    // _scaleX will not change
    Precalculate(props);
    SaturateX(_mapX);
    SaturateY(_mapY);
    return;
  }
  // saturation and Precalculate inside
  SetScale(_scaleX);
}

void CStaticMap::Precalculate(const MapProperties &props)
{
  // precalculates for increasing drawing speed
  _wScreen = GLOB_ENGINE->Width2D();
  _hScreen = GLOB_ENGINE->Height2D();

  float coef = _hScreen / _wScreen;
  _scaleY = coef * _scaleX;
  _invScaleX = 1.0 / _scaleX;
  _invScaleY = 1.0 / _scaleY;

  // map area limits converted to 0 ... _invScale coordinates 
  _minX = 0; _maxX = _invScaleX;
  _minY = 0; _maxY = _invScaleY;
  float mapSize = LandGrid * LandRange;
  float invMapSize = InvLandGrid * InvLandRange;
  if (props._xMin > 0) _minX = props._xMin * invMapSize * _invScaleX;
  if (props._xMax < mapSize) _maxX = props._xMax * invMapSize * _invScaleX;
  if (props._zMin > 0) _maxY = (1.0f - props._zMin * invMapSize) * _invScaleY;
  if (props._zMax < mapSize) _minY = (1.0f - props._zMax * invMapSize) * _invScaleY;

  _clipRect.x = _x * _wScreen;
  _clipRect.y = _y * _hScreen;
  _clipRect.w = _w * _wScreen;
  _clipRect.h = _h * _hScreen;
}

void CStaticMap::SaturateX(float &x)
{
  // try to limit shown area to the map size
  float size = _maxX - _minX;
  if (size < _w) x = -_minX + 0.5 * (_w - size);
  else saturate(x, -(_maxX - _w), -_minX);
}

void CStaticMap::SaturateY(float &y)
{
  // try to limit shown area to the map size
  float size = _maxY - _minY;
  if (size < _h) y = -_minY + 0.5 * (_h - size);
  else saturate(y, -(_maxY -_h), -_minY);
}

void CStaticMap::ScrollX(float dif)
{
  _mapX += dif;
  SaturateX(_mapX);
}

void CStaticMap::ScrollY(float dif)
{
  _mapY += dif;
  SaturateY(_mapY);
}

void CStaticMap::SetVisibleRect(float x, float y, float w, float h)
{
  _center.x = x - _x + 0.5 * w; // offset + 0.5 * width
  _center.y = y - _y + 0.5 * h; // offset + 0.5 * height
  _halfSize.x = 0.5 * w;
  _halfSize.y = 0.5 * h;
}

void CStaticMap::Center(Vector3Val pt, bool soft)
{
  float invSizeLand = 1.0 / (LandGrid * LandRange);
  float xMap = pt.X() * invSizeLand * _invScaleX;
  float yMap = (1.0 - pt.Z() * invSizeLand) * _invScaleY;
  if (soft)
  {
    float mapX = _center.x - xMap;
    float mapY = _center.y - yMap;

    saturate(_mapX, mapX - 0.5 * _halfSize.x, mapX + 0.5 * _halfSize.x);
    saturate(_mapY, mapY - 0.5 * _halfSize.y, mapY + 0.5 * _halfSize.y);
  }
  else
  {
    _mapX = _center.x - xMap;
    _mapY = _center.y - yMap;
  }
  SaturateX(_mapX);
  SaturateY(_mapY);
}

void CStaticMap::SetScale(float scale)
{
  const MapProperties &props = GetMapProperties();
  if (scale < 0) scale = props._scaleDefault;

  float mouseX = 0.5 + GInput.cursorX * 0.5;
  float mouseY = 0.5 + GInput.cursorY * 0.5;

  saturate(mouseX, _x, _x + _w);
  saturate(mouseY, _y, _y + _h);

  DrawCoord ptMap(mouseX, mouseY);
  DrawCoord offset(mouseX - _x, mouseY - _y);
  Vector3 pt = ScreenToWorld(ptMap);

  _scaleX = scale;
  saturate(_scaleX, props._scaleMin, props._scaleMax);
  Precalculate(props);

  float invSizeLand = 1.0 / (LandGrid * LandRange);
  float xMap = pt.X() * invSizeLand * _invScaleX;
  float yMap = (1.0 - pt.Z() * invSizeLand) * _invScaleY;
  _mapX = offset.x - xMap;
  _mapY = offset.y - yMap;
  SaturateX(_mapX);
  SaturateY(_mapY);
}

int GetCursorIndex(RString cursorType)
{//returns cursotType of default textures
  if(strcmp("Arrow",cursorType)==0)
    return CursorArrow;
  if(strcmp("Track",cursorType)==0)
    return CursorTrack;
  if(strcmp("Move",cursorType)==0)
    return CursorMove;
  if(strcmp("Scroll",cursorType)==0)
    return CursorScroll;
  if(strcmp("Watch",cursorType)==0)
    return CursorWatch;
  if(strcmp("Rotate",cursorType)==0)
    return CursorRotate;
#if _VBS3
  if(strcmp"Drag",cursorType)==0)
    return CursorDrag;
  if(strcmp("Raise",cursorType)==0)
    return CursorRaise;
#else
  if(strcmp("Rotate3D",cursorType)==0)
    return CursorRotate3D;
  if(strcmp("Raise3D",cursorType)==0)
    return CursorRaise3D;
  if(strcmp("Move3D",cursorType)==0)
    return CursorMove3D;
  if(strcmp("Track3D",cursorType)==0)
    return CursorTrack3D;
#endif

  return -1;
}

void CStaticMap::SetOverrideCursor(RString cursorName, RString overrideTexture)
{
  //first try to open new texture...
  ParamEntryVal entry = Pars>> "CfgWrapperUI" >> "Cursors";
  //... if new texture does not exist, use default texture name
  if(!entry.FindEntry(overrideTexture))
    overrideTexture = cursorName;

  int index = GetCursorIndex(cursorName);
  if(index>=0)
  {//save new texture to array
    _cursorMapOverride[index] = overrideTexture;
  }

  //if changed cursor is the same we are using at the moment, change used texture 
  if(_parent && strcmp(_cursorName,cursorName)==0)
    _parent->SetCursor(overrideTexture);
}

RString CStaticMap::OverrideCursor(RString cursorName)
{ //save current cursor type
  _cursorName = cursorName;
  RString name;
  int index = GetCursorIndex(cursorName);

  if(index>=0) name = _cursorMapOverride[index];
  //no override texture is prepared, so we use default
  if(name.GetLength()<=0) name = cursorName;

  return name;
}

RString CStaticMap::OverrideCursor(CursorType cursorType)
{
  RString name;
  //if(cursorType>=0)
  //  name = _cursorMapOverride[cursorType];

  //if(name.GetLength()<=0)
  //  name = cursorName;
  return name;
}

void CStaticMap::InitDefaultCenter()
{
  _defaultCenter.Init();
  _defaultCenter[0] = (Pars >> "CfgWorlds" >> Glob.header.worldname >> "centerPosition")[0];
  _defaultCenter[1] = 0;
  _defaultCenter[2] = (Pars >> "CfgWorlds" >> Glob.header.worldname >> "centerPosition")[1];

  _gridNumbersOverLines = Pars >> "CfgWorlds" >> Glob.header.worldname >> "gridNumbersOverLines";

  _mapX = 0;
  _mapY = 0;

  float sizeLand = LandGrid * LandRange;
  saturate(_defaultCenter[0], 0, sizeLand);
  saturate(_defaultCenter[2], 0, sizeLand);
}

///////////////////////////////////////////////////////////////////////////////
// Main (ingame) map

CStatic *CreateStaticMapMain(ControlsContainer *parent, int idc, const ParamEntry &cls)
{
  CStaticMap *map = new CStaticMapMain(parent, idc, cls);
  map->SetVisibleRect(map->X(), map->Y(), map->W(), map->H());
  map->Center();
  return map;
}

#define EXPOSURE_COEF     50.0F

#if _VBS2
CStaticMapMain::CStaticMapMain
  (
  ControlsContainer *parent, int idc, ParamEntryPar cls,
  float scaleMin, float scaleMax, float scaleDefault
  ) : CStaticMap(parent, idc, cls, scaleMin, scaleMax, scaleDefault), _autoCenter(false)
{
}
#endif

CStaticMapMain::CStaticMapMain(ControlsContainer *parent, int idc, ParamEntryPar cls)
#if _ENABLE_CHEATS
  : CStaticMap(parent, idc, cls, 0.001, 1.0, 0.16)
#else
  : CStaticMap(parent, idc, cls, 0.02, 1.0, 0.16)
#endif
  , _autoCenter(false)
{
#if _ENABLE_CHEATS
  _showUnits = false;
  _showTargets = false;
  _showVariables = 0;
#endif
  _activeMarker = -1;

  _infoCommand.Load(cls >> "Command");
  _infoTask.Load(cls >> "Task");
  _infoCustomMark.Load(cls >> "CustomMark");
  _colorActiveMarker = GetPackedColor(cls >> "ActiveMarker" >> "color");
  _sizeActiveMarker = cls >> "ActiveMarker" >> "size";

  _iconPosition = GEngine->TextBank()->Load(GetPictureName(Pars >> "CfgInGameUI" >> "Cursor" >> "mission"));

#if _VBS2 // conditional markers
  _lastEvalMarkerConditions = UITime(0);
#endif

  _mapInfo.Clear();
  ShowScale(false);
  _showScaleAlpha = 0.66f;

  _mouseClickHandled = false;
}

void AddEnemyInfo
  (
  TargetType *id, const VehicleType *type, int x, int z
  )
{
  if (!id) return;
  DisplayMap *display = dynamic_cast<DisplayMainMap *>(GWorld->Map());
  if (!display) return;
  CStaticMapMain *map = display->GetMap();
  if (!map) return;
  map->AddEnemyInfo(id, type, x, z);
}

void CStaticMapMain::AddEnemyInfo
  (
  TargetType *id, const VehicleType *type, int x, int z
  )
{
  Assert(id);
  int index = -1;
  for (int i=0; i<_mapInfo.enemyInfo.Size(); i++)
    if (_mapInfo.enemyInfo[i].id == id)
    {
      index = i; break;
    }
    if (index < 0) index = _mapInfo.enemyInfo.Add();
    MapEnemyInfo &info = _mapInfo.enemyInfo[index];
    info.id = id;
    info.type = type;
    info.time = Glob.clock.GetTimeOfDay();
    if (info.time >= 1.0) info.time--;
    info.time *= 24;
    info.x = x;
    info.z = z;
}

LSError MapEnemyInfo::Serialize(ParamArchive &ar)
{
  CHECK(ar.SerializeRef("Target", id, 1))
    CHECK(::Serialize(ar, "type", type, 1))
    CHECK(ar.Serialize("time", time, 1))
    CHECK(ar.Serialize("x", x, 1))
    CHECK(ar.Serialize("z", z, 1))
    return LSOK;
}

LSError MainMapInfo::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("EnemyInfo", enemyInfo, 1))
    return LSOK;
}

void MainMapInfo::Clear()
{
  enemyInfo.Clear();
}

EntityAI *CStaticMapMain::GetTarget() const
{ 
  switch (_infoMove._type)
  {
  case signVehicle:
  case signVehicleGroup:
  case signStatic:
    return _infoMove._id;
  case signUnit:
    if (_infoMove._unit) return _infoMove._unit->GetVehicle();
    return NULL;
  }
  return NULL;
}

bool CStaticMapMain::GetPosition(Vector3 &pos) const
{
  float mouseX = 0.5 + GInput.cursorX * 0.5;
  float mouseY = 0.5 + GInput.cursorY * 0.5;

  saturate(mouseX, _x, _x + _w);
  saturate(mouseY, _y, _y + _h);

  DrawCoord ptMap(mouseX, mouseY);
  pos = ScreenToWorld(ptMap);
  pos[1] = GLandscape->SurfaceYAboveWater(pos[0], pos[2]);

  return true;
}

LSError CStaticMapMain::Serialize(ParamArchive &ar, RString name, int minVersion)
{
  CHECK(ar.Serialize(name, _mapInfo, minVersion))
    return LSOK;
}

void CStaticMapMain::DrawMarkers()
{
#if _VBS2 // conditional markers
  bool evalCondition = false;
  if (Glob.uiTime - _lastEvalMarkerConditions > 0.5)
  {
    evalCondition = true;
    _lastEvalMarkerConditions = Glob.uiTime;
  }
#endif
  int n = markersMap.Size();
  for (int i=0; i<n; i++)
  {
    ArcadeMarkerInfo &mInfo = markersMap[i];
#if _VBS3 // markers moved to CStaticMap
    if (evalCondition)
    {      
      if (mInfo.condition.GetLength() > 0)
        mInfo.isHidden = !_drawAttachedMarkers || !GWorld->GetGameState()->EvaluateBool(mInfo.condition, GameState::EvalContext::_default, GWorld->GetMissionNamespace());
      if (mInfo.attachCondition.GetLength() > 0)
      {
        if (!_drawConditionalMarkers) continue;
        GameState *state = GWorld->GetGameState();

        GameVarSpace vars(false);
        int gshandle = state->BeginContext(&vars);

        GameValue value = state->CreateGameValue(GameArray);
        GameArrayType &array = value;
        array.Resize(mInfo.attachCtx.Size());
        for (int i=0; i<mInfo.attachCtx.Size(); i++)
          array[i] = GameValueExt(mInfo.attachCtx[i]);
        state->VarSetLocal("_this", value, true);

        GameValue result = state->EvaluateMultiple(mInfo.attachCondition, GameState::EvalContext::_default, GWorld->GetMissionNamespace());
        if (!result.GetNil() && result.GetType() == GameObject)
          mInfo.attachTo = dynamic_cast<GameDataObject *>(result.GetData())->GetObject();

        state->EndContext(gshandle);
      }
    }
    if (mInfo.isHidden) continue;
    if (mInfo.attachTo)
      mInfo.position =  mInfo.attachTo->WorldPosition(mInfo.attachTo->FutureVisualState()) + mInfo.attachOffset;
#endif
    //set color and update alpha according to marker alpha property    
    PackedColor fadedMarkerColor = ModAlpha(mInfo.color,mInfo.alpha);

    switch (mInfo.markerType)
    {
    case MTIcon:
      {
        if (mInfo.size == 0) break;
#if _VBS2 // conditional markers
        if (mInfo.layeredIcons.Size() > 0)
        {
          for (int i=0; i<mInfo.layeredIcons.Size(); i++)
          {
            LayeredMarkerInfo *layeredIcon = mInfo.layeredIcons[i];
            if (!layeredIcon->icon) continue;
            RString text = i == 0 ? Localize(mInfo.text) : ""; 
            Vector3 pos = mInfo.position;
            pos[0] += layeredIcon->x;
            pos[2] += layeredIcon->y;
            DrawSignAutosize
              (
              layeredIcon->icon, mInfo.color,
              mInfo.position,
              mInfo.size * layeredIcon->a, mInfo.size * layeredIcon->b,
              mInfo.angle * (H_PI / 180.0), text, true, NULL, -1, mInfo.autosize
              );
          }
          break;
        }
        if (!mInfo.icon) break;
        DrawSignAutosize
          (
          mInfo.icon, mInfo.color,
          mInfo.position,
          mInfo.size * mInfo.a, mInfo.size * mInfo.b,
          mInfo.angle * (H_PI / 180.0), Localize(mInfo.text), true, NULL, -1, mInfo.autosize
          );
#else     
        if (!mInfo.icon) break;
        DrawSign
          (
          mInfo.icon, fadedMarkerColor,
          mInfo.position,
          mInfo.size * mInfo.a, mInfo.size * mInfo.b,
          mInfo.angle * (H_PI / 180.0), Localize(mInfo.text),NULL,-1, mInfo.shadow
          );
#endif
      }
      break;
    case MTRectangle:
      FillRectangle
        (
        mInfo.position, mInfo.a, mInfo.b, mInfo.angle * (H_PI / 180.0),
        fadedMarkerColor, mInfo.fill
        );
      if(mInfo.drawBorder)
        DrawRectangle(mInfo.position, mInfo.a, mInfo.b, mInfo.angle * (H_PI / 180.0),
        fadedMarkerColor);
      break;
    case MTEllipse:
      FillEllipse
        (
        mInfo.position, mInfo.a, mInfo.b, mInfo.angle * (H_PI / 180.0),
        fadedMarkerColor, mInfo.fill
        );
      if(mInfo.drawBorder)
        DrawEllipse(mInfo.position, mInfo.a, mInfo.b, mInfo.angle * (H_PI / 180.0),
        fadedMarkerColor);
      break;
    }
  }
}

/// helper will extract vehicle from the SignInfo
static EntityAI *GetVehicle(const SignInfo &info)
{
  switch (info._type)
  {
  case signUnit:
    if (info._unit) return info._unit->GetVehicle();
    return NULL;
  case signVehicle:
  case signVehicleGroup:
  case signStatic:
    return info._id;
  default:
    return NULL;
  }
}

#if _ENABLE_CHEATS
Ref<const TargetNormal> diagTarget;
static inline RString GetTargetInfo(const TargetNormal *target)
{
  Vector3 pos = target->LandAimingPosition();
  diagTarget = target;
  return 
    Format(" seen %.1f", Glob.time.Diff(target->lastSeen)) +
    Format(" posAge %.1f",Glob.time.Diff(target->posAccuracyTime)) +
    Format(" prec %.1f", target->posAccuracy) +
    Format(" dstRP %.1f", pos.Distance(target->idExact->AimingPosition(target->idExact->FutureVisualState()))) +
    Format(" dstPos %.1f", pos.Distance(target->position)) +
    Format(" sideAc %.1f", target->FadingSideAccuracy()) +
    Format(" cost %.0f", target->subjectiveCost);
}

static inline RString GetTargetModeInfo(AIBrain *unit)
{
  RString result;

  result = result + Format(" CM Major: %s", cc_cast(FindEnumName(unit->GetCombatModeMajor())));

  result = result + Format(" CM: %s", cc_cast(FindEnumName(unit->GetCombatMode())));

  return result;
}
#endif

//to skip animals and civilians
bool CStaticMapMain::CheckItemWantedDraw(const TargetNormal *target)
{
  if(target->type->IsAnimal() || target->side == TCivilian) return false;
  return true;
};

void CStaticMapMain::DrawLabel(struct SignInfo &info, PackedColor color, int shadow)
{
  // show info from the eyes of observer in Release version, use the player always in Retail
#if _ENABLE_CHEATS
  EntityAI *observer = GetVehicle(_infoClick);
  AIBrain *me = observer ? observer->CommanderUnit() : NULL;
  if (!me) me = GetMyUnit();
#else
  AIBrain *me = GetMyUnit();
#endif

  RString text;
  Vector3 pos;

  EntityAI *vehicle = GetVehicle(info);
  if (vehicle)
  {
#if _ENABLE_CHEATS
    if (_showUnits || CHECK_DIAG(DEFSM))
    {
      AIBrain *unit = vehicle->CommanderUnit();
      text = vehicle->GetType()->GetDisplayName();
      if (unit && unit->GetPerson()) text = text + ": " + unit->GetPerson()->GetPersonName();
      if (CHECK_DIAG(DECombat))
      {
        // display some additional informations about the target
        const TargetList *list = me ? me->AccessTargetList() : NULL;
        if (list)
        {
          const TargetNormal *target = list->FindTarget(vehicle);
          if (target) text = text + GetTargetInfo(target);
        }
      }
    }
    else if (_showTargets)
    {
      const TargetList *list = me ? me->AccessTargetList() : NULL;
      if (list)
      {
        const TargetNormal *target = list->FindTarget(vehicle);
        if (target)
        {
          text = target->GetType()->GetDisplayName();
          pos = target->LandAimingPosition();
          // display some additional informations about the target
          if (CHECK_DIAG(DECombat)) text = text + GetTargetInfo(target);
        }
      }
    }
    else 
#endif
      if (Glob.config.IsEnabled(DTMap) && !_miniMapMode)
      {
        const TargetList *list = me ? me->AccessTargetList() : NULL;
        if (list)
        {
          const TargetNormal *target = list->FindTarget(vehicle);
          if (target)
          {
            //skip animals and civilians
            if(CheckItemWantedDraw(target)) 
            {
              text = target->GetType()->GetDisplayName();
              pos = target->LandAimingPosition();
#if _ENABLE_CHEATS
              // display some additional informations about the target
              if (CHECK_DIAG(DECombat)) text = text + GetTargetInfo(target);
#endif
            }
          }
        }
      }
#if _ENABLE_CHEATS
      if (!CHECK_DIAG(DECombat) && CHECK_DIAG(DECombatMode))
      {
        AIBrain *unit = vehicle->CommanderUnit();
        if (unit) text = text + GetTargetModeInfo(unit);
      }
#endif
  }
  else switch (info._type)
  {
  case signNone:
    break;
#if _ENABLE_IDENTITIES
  case signTask:
    if (info._unit)
    {
      Task *task = info._unit->GetPerson()->GetIdentity().FindTask(info._index);
      if (task && task->GetDestination(pos))
      {
        task->GetDescriptionHUD(text);
      }
    }
    break;
#endif
  }

  if (text.GetLength() > 0) CStaticMap::DrawLabel(pos, text, color, shadow);
}

inline PackedColor CostToColor(float cost)
{
  static const float int1 = 1.0;
  static const float invint1 = (1.0 / int1);
  static const float int2 = 4.0;
  static const float invint2 = (1.0 / int2);
  static const float int12 = int1 + int2;

  static const float alpha = 0.5;
  static const Color colorR(1, 0, 0, alpha);
  static const Color colorG(0, 1, 0, alpha);
  static const Color colorY(1, 1, 0, alpha);

  saturateMin(cost, int12);
  if (cost <= int1)
  {
    return PackedColor
      (
      colorY * (cost * invint1)  + colorG * (1 - cost * invint1)
      );
  }
  else
  {
    cost -= int1;
    return PackedColor
      (
      colorR * (cost * invint2) + colorY * (1 - cost * invint2)
      );
  }
}

static float GetFieldCost(AIBrain *unit, int x, int z, bool road)
{
  GeographyInfo geogr = GLOB_LAND->GetGeography(x,z);
  geogr.u.road = road; // if we are searching on road is now given externally

  EntityAI *uVeh = unit->GetVehicle();

  float cost = 0;
  if (uVeh) cost = uVeh->GetBaseCost(geogr, false, true) * uVeh->GetFieldCost(geogr) * uVeh->GetAirCost(x,z);

  if (cost <= 0) cost = 1;
  cost *= LandGrid; // cost = time for distance == LandGrid
  saturateMin(cost, SET_UNACCESSIBLE); // avoid inf. in cost
  return cost;
}

void CStaticMapMain::DrawCost(const MapProperties &props, AIBrain *unit, float invCost,
                              float x, float y, float xStep, float yStep,
                              int i, int j, int iStep, int jStep)
{
  GeographyInfo geogr = GLOB_LAND->GetGeography(i, j - 1);

  Draw2DParsNoTex pars;

  float cost;
  if (iStep == 1 && jStep == -1)
  {
    // ALL
    cost = GetFieldCost(unit, i, j + jStep, false) * invCost;
    pars.SetColor(CostToColor(cost));
  }
  else
  {
    // TL
    cost = GetFieldCost(unit, i, j, false) * invCost;
    pars.colorTL = CostToColor(cost);
    // TR
    cost = GetFieldCost(unit, i + iStep, j, false) * invCost;
    pars.colorTR = CostToColor(cost);
    // BL
    cost = GetFieldCost(unit, i, j + jStep, false) * invCost;
    pars.colorBL = CostToColor(cost);
    // BR
    cost = GetFieldCost(unit, i + iStep, j + jStep, false) * invCost;
    pars.colorBR = CostToColor(cost);
  }

  pars.spec = NoZBuf|IsAlpha|IsAlphaFog|NoClamp;
  Rect2DPixel rect(x * _wScreen, y * _hScreen, xStep * _wScreen, yStep * _hScreen);
  GEngine->Draw2D(pars, rect, _clipRect);

  // draw text information about the field
  float tsize = xStep * floatMax(_sizeInfo,0.1f);
  if (tsize >= 0.014f)
  {
    float ty = y;

    if (InRange(j - 1, i))
    {
      const ObjectList &ol = GLOB_LAND->GetObjects(j - 1, i);
      if (ol.GetList())
      {
        bool cached = ol.GetList()->IsInList();
        GEngine->DrawTextF
          (
          Point2DFloat(x, ty), tsize,
          Rect2DFloat(x, y, xStep, yStep),
          _fontInfo, props._colorNames, 0,
          "Used %d%s", ol.GetList()->GetUsedCount()-cached, cached ? "+C" : ""
          );
        ty += tsize;
      }

      const MapObjectList &ml = GLOB_LAND->GetMapObjects(i, j - 1);
      if (ml.GetList())
      {
        bool cached = ml.GetList()->IsInList();
        GEngine->DrawTextF
          (
          Point2DFloat(x, ty), tsize,
          Rect2DFloat(x, y, xStep, yStep),
          _fontInfo, props._colorNames, 0,
          "MapUsed %d%s", ml.GetList()->GetUseCount()-cached, cached ? "+C" : ""
          );
        ty += tsize;
      }
    }

    //float tsize = 0.2;
    GEngine->DrawTextF
      (
      Point2DFloat(x, ty), tsize,
      Rect2DFloat(x, y, xStep, yStep),
      _fontInfo, props._colorNames, 0,
      "softObj %d", geogr.u.howManyObjects
      );
    ty += tsize;

    if (geogr.u.howManyHardObjects>0)
    {
      GEngine->DrawTextF
        (
        Point2DFloat(x, ty), tsize,
        Rect2DFloat(x, y, xStep, yStep),
        _fontInfo, props._colorNames, 0,
        "hardObj %d", geogr.u.howManyHardObjects
        );
      ty += tsize;
    }

    GEngine->DrawTextF
      (
      Point2DFloat(x, ty), tsize,
      Rect2DFloat(x, y, xStep, yStep),
      _fontInfo, props._colorNames, 0,
      "grad %d", geogr.u.gradient
      );
    ty += tsize;

    if (geogr.u.forest)
    {
      GEngine->DrawText
        (
        Point2DFloat(x, ty), tsize,
        Rect2DFloat(x, y, xStep, yStep),
        _fontInfo, props._colorNames,
        "forest",0
        );
      ty += tsize;
    }

    if (geogr.u.road)
    {
      GEngine->DrawText
        (
        Point2DFloat(x, ty), tsize,
        Rect2DFloat(x, y, xStep, yStep),
        _fontInfo, props._colorNames,
        "Road",0
        );
      ty += tsize;
    }

    if (geogr.u.minWaterDepth>0 || geogr.u.maxWaterDepth>0)
    {
      GEngine->DrawTextF(
        Point2DFloat(x, ty), tsize,
        Rect2DFloat(x, y, xStep, yStep),
        _fontInfo, props._colorNames, 0,
        "Water %d..%d",geogr.u.minWaterDepth,geogr.u.maxWaterDepth
        );
      ty += tsize;
    }
    if (geogr.u.full)
    {
      GEngine->DrawText
        (
        Point2DFloat(x, ty), tsize,
        Rect2DFloat(x, y, xStep, yStep),
        _fontInfo, props._colorNames,
        "Square full",0
        );
      ty += tsize;
    }
  }

  if (iStep == 1 && jStep == -1)
  {
    float xTL = x * _wScreen, yTL = y * _hScreen;
    float xBR = (x+xStep) * _wScreen, yBR = (y+yStep) * _hScreen;

    const RoadList &roadList = GLandscape->GetRoadNet()->GetRoadList(i, j - 1);
    int n = roadList.Size();
    if (n > 0)
    {
      bool found = false;
      for (int i=0; i<n; i++)
      {
        RoadLink *item = roadList[i];
        for (int j=0; j<item->NConnections(); j++)
        {
          if (!item->Connections()[j])
          {
            found = true;
            break;
          }
        }
        if (found) break;
      }
      PackedColor color = PackedColor(found ? Color(1,.2,.2,1) : Color(1,1,1,1));
      GLOB_ENGINE->DrawLine
        (
        Line2DPixel(xTL, yTL, xBR, yBR),
        color, color, _clipRect
        );
      GLOB_ENGINE->DrawLine
        (
        Line2DPixel(xTL, yBR, xBR, yTL),
        color, color, _clipRect
        );
    }

    if (InRange(j - 1, i))
    {
      const ObjectList &ol = GLOB_LAND->GetObjects(j - 1, i);
      if (ol.GetList())
      {
        bool cached = ol.GetList()->IsInList();
        bool locked = ol.GetList()->GetUsedCount()>int(cached);
        PackedColor color = PackedColor(Color(0.1,0.8,0.1,0.85));
        if (cached && locked)
        {
          color = PackedColor(Color(0.25,0.25,0.7,0.75));
        }
        else if (cached)
        {
          color = PackedColor(Color(0.25,0.25,0.25,0.75));
        }
        else if (locked)
        {
          color = PackedColor(Color(0.7,0.1,0.1,0.85));
        }
        GLOB_ENGINE->DrawLine
          (
          Line2DPixel(xTL, (yTL*1.4+yBR*0.6)*0.5, xBR, (yTL*0.6+yBR*1.4)*0.5),
          color, color, _clipRect
          );
        GLOB_ENGINE->DrawLine
          (
          Line2DPixel((xTL*1.4+xBR*0.6)*0.5, yBR, (xTL*0.6+xBR*1.4)*0.5, yTL),
          color, color, _clipRect
          );
      }

      const MapObjectList &ml = GLOB_LAND->GetMapObjects(i, j - 1);
      if (ml.GetList())
      {
        bool cached = ml.GetList()->IsInList();
        bool locked = ml.GetList()->GetUseCount()>int(cached);
        PackedColor color = PackedColor(Color(0.1,0.8,0.1,0.85));
        if (cached && locked)
        {
          color = PackedColor(Color(0.25,0.25,0.7,0.75));
        }
        else if (cached)
        {
          color = PackedColor(Color(0.25,0.25,0.25,0.75));
        }
        else if (locked)
        {
          color = PackedColor(Color(0.7,0.1,0.1,0.85));
        }
        GLOB_ENGINE->DrawLine
          (
          Line2DPixel(xTL, (yTL+yBR)*0.5, xBR, (yTL+yBR)*0.5),
          color, color, _clipRect
          );
        GLOB_ENGINE->DrawLine
          (
          Line2DPixel((xTL+xBR)*0.5, yBR, (xTL+xBR)*0.5, yTL),
          color, color, _clipRect
          );
      }
    }
  }

}

void CStaticMapMain::DrawInfo()
{
  float coef = 100.0; // width of field in global coord.
  float invCoef = 0.01; // 1.0 / coef;

  float xFld = _invScaleX * invCoef;
  float zFld = _invScaleY * invCoef;

  float tsize = xFld * _sizeInfo;
  if (tsize < 0.022) return;

  int xFrom = toIntFloor(-_mapX * _scaleX * coef);
  int zFrom = toIntFloor((_invScaleY - (_h - _mapY)) * _scaleY * coef);
  int xTo = toIntFloor((_w - _mapX) * _scaleX * coef);
  int zTo = toIntFloor((_invScaleY - (-_mapY)) * _scaleY * coef);

  float xBeg = _x + _mapX + xFrom * _invScaleX * invCoef;
  float zBeg = _y + _mapY + (_invScaleY - zFrom * _invScaleY * invCoef);

  const MapProperties &props = GetMapProperties();

  float zAct = zBeg;
  for (int z = zFrom; z <= zTo; z++)
  {
    float xAct = xBeg;
    for (int x = xFrom; x <= xTo; x++)
    {
      DrawInfo(props, xAct, zAct - zFld, xFld, zFld, x, z);
      xAct += xFld;
    }
    zAct -= zFld;
  }
}

void CStaticMapMain::DrawInfo
  (
  const MapProperties &props, float x, float y, float xStep, float yStep, int i, int j
  )
{
  static const int maxSubgroups = 3;
  static const int maxEnemies = 3;

  float tsize = xStep * _sizeInfo;
  float ty = y + 0.5 * (yStep - (maxSubgroups + 1) * tsize);

  int indices[maxEnemies];
  for (int l=0; l<maxEnemies; l++) indices[l] = -1;
  for (int k=0; k<_mapInfo.enemyInfo.Size(); k++)
  {
    MapEnemyInfo &info = _mapInfo.enemyInfo[k];
    if (info.x != i || info.z != j) continue;
    for (int l=0; l<maxEnemies; l++)
    {
      if (indices[l] < 0)
      {
        indices[l] = k;
        break;
      }
      else
      {
        float kCost = info.type->GetCost();
        float lCost = _mapInfo.enemyInfo[indices[l]].type->GetCost();
        if (kCost >= lCost)
        {
          for (int i=maxEnemies-1; i>l; i--) indices[i] = indices[i - 1];
          indices[l] = k;
          break;
        }
      }
    }
  }
  if (indices[0] >= 0)
  {
    float left = x;
    float maxTime = 0;
    for (int l=0; l<maxEnemies; l++)
    {
      int k = indices[l];
      if (k < 0) break;
      MapEnemyInfo &info = _mapInfo.enemyInfo[k];
      saturateMax(maxTime, info.time);
    }
    int hour = toIntFloor(maxTime);
    int minute = toIntFloor(60.0 * (maxTime - hour));
    if (minute >= 60)
    {
      hour++;
      minute -= 60;
    }
    GEngine->DrawTextF
      (
      Point2DFloat(x, ty), tsize,
      Rect2DFloat(x, y, xStep, yStep),
      _fontInfo, _colorEnemy, 0,
      "% 2d:%02d", hour, minute
      );
    left += GEngine->GetTextWidthF(tsize, GetMapProperties()._fontNames, "% 2d:%02d", hour, minute);
    float height = tsize;
    float width = height * (_hScreen / _wScreen);
    for (int l=0; l<maxEnemies; l++)
    {
      int k = indices[l];
      if (k < 0) break;
      MapEnemyInfo &info = _mapInfo.enemyInfo[k];
      Texture *texture = info.type->GetIcon();
      MipInfo mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
      GEngine->Draw2D
        (
        mip, _colorEnemy,
        Rect2DPixel(left * _wScreen, ty * _hScreen, width * _wScreen, height * _hScreen),
        Rect2DPixel(x * _wScreen, y * _hScreen, xStep * _wScreen, yStep * _hScreen)
        );
      left += width;
    }
    ty += height;
  }
}

AIBrain *CStaticMapMain::GetMyUnit()
{
  return GWorld->FocusOn();
}

void CStaticMapMain::DrawCost()
{
  AIBrain *me = GetMyUnit();
  if (!me) return;

  const MapProperties &props = GetMapProperties();

  GeographyInfo geogr(0);
  float costNormal = me->GetVehicle()->GetBaseCost(geogr, false, true)
    * me->GetVehicle()->GetFieldCost(geogr);
  float invCost = 1.0 / (costNormal * LandGrid);

  float ptsLand = 800.0 * _invScaleX * InvLandRange;  // avoid dependence on video resolution
  float invPtsLand = 1.0 / ptsLand;

  int iStep = toIntCeil(_ptsPerSquareCost * invPtsLand);
  float xStep = iStep * InvLandRange * _invScaleX;
  int jStep = iStep;
  float yStep = jStep * InvLandRange * _invScaleY;
  int iMin = iStep * toIntFloor(-_mapX / xStep);
  float xMin = _x - fastFmod(-_mapX, xStep);
  int jMin = LandRange - jStep * toIntFloor(-_mapY / yStep);
  float yMin = _y - fastFmod(-_mapY, yStep);

  float y = yMin;
  int j = jMin;
  while (y <= _y + _h)
  {
    float x = xMin;
    int i = iMin;
    while (x <= _x + _w)
    {
      // get geography info
      DrawCost(props, me, invCost, x, y, xStep, yStep, i, j, iStep, -jStep);
      i += iStep;
      x += xStep;
    }
    j -= jStep;
    y += yStep;
  }
}

PackedColor TempToColor(float temp, ParamEntryPar entry)
{
  if (entry.GetSize() != 4)
  {
    if (entry.GetSize() == 3)
    {
      int r8 = toInt(entry[0].GetFloat() * 255.0f);
      int g8 = toInt(entry[1].GetFloat() * 255.0f);
      int b8 = toInt(entry[2].GetFloat() * 255.0f);
      saturate(r8, 0.0f, 255.0f);
      saturate(g8, 0.0f, 255.0f);
      saturate(b8, 0.0f, 255.0f);
      return PackedColor(r8, g8, b8, 255.0f);
    }

    // ErrorMessage("Config: '%s' not an ARGB color",(const char *)GetName());
    return PackedWhite;
  }

  int r8 = toInt(entry[0].GetFloat() * 255.0f);
  int g8 = toInt(entry[1].GetFloat() * 255.0f);
  int b8 = toInt(entry[2].GetFloat() * 255.0f);
  int a8 = toInt(entry[3].GetFloat() * 255.0f);

  //set temperature values
  if (temp > 0.0f)
  {
    float ratio = temp / 80.0f;
    saturate(ratio, 0.0f, 1.0f);
    r8 = r8 + toInt(ratio * 255.0f);
    float ratio1 = 1.0f - ratio;
    g8 = toInt(entry[1].GetFloat() * 255.0f * ratio1);
    b8 = toInt(entry[2].GetFloat() * 255.0f * ratio1);
  }
  else if (temp < 0.0f)
  {
    float ratio = -temp / 100.0f;
    saturate(ratio, 0.0f, 1.0f);
    b8 = b8 + toInt(ratio * 255.0f);
    //float ratio1 = 1.0f - ratio1;

    r8 = toInt(entry[0].GetFloat() * 255.0f * ratio);
    g8 = toInt(entry[1].GetFloat() * 255.0f * ratio);
  }

  saturate(r8, 0.0f, 255.0f);
  saturate(g8, 0.0f, 255.0f);
  saturate(b8, 0.0f, 255.0f);
  saturate(a8, 0.0f, 255.0f);

  return PackedColor(r8, g8, b8, a8);
}

#if _ENABLE_CHEATS

inline PackedColor AgeToColor(float age)
{
  static const float alpha = 0.5f;
  static const float maxAge = 60.0f;
  static const float invMaxAge = 1.0f / maxAge;
  static const Color color0(0, 1, 0, alpha);
  static const Color color1(1, 1, 0, alpha);

  if (age < 0) return PackedColor(Color(1, 0, 0, alpha));
  saturateMin(age, maxAge);
  return PackedColor(color0 * (1.0f - age * invMaxAge) + color1 * (age * invMaxAge));
}

void CStaticMapMain::DrawOperCache(float x, float y, float xStep, float yStep, int i, int j, int iStep, int jStep)
{
  Draw2DParsNoTex pars;

  if (iStep == 1 && jStep == -1)
  {
    // ALL
    const OperField *field = GLandscape->OperationalCache()->CheckField(LandIndex(i), LandIndex(j + jStep));
    if (field)
      pars.SetColor(AgeToColor(field->GetAge()));
    else
      pars.SetColor(PackedColor(0));
  }
  else
  {
    // TL
    const OperField *field = GLandscape->OperationalCache()->CheckField(LandIndex(i), LandIndex(j));
    if (field)
      pars.colorTL = AgeToColor(field->GetAge());
    else
      pars.colorTL = PackedColor(0);
    // TR
    field = GLandscape->OperationalCache()->CheckField(LandIndex(i + iStep), LandIndex(j));
    if (field)
      pars.colorTR = AgeToColor(field->GetAge());
    else
      pars.colorTR = PackedColor(0);
    // BL
    field = GLandscape->OperationalCache()->CheckField(LandIndex(i), LandIndex(j + jStep));
    if (field)
      pars.colorBL = AgeToColor(field->GetAge());
    else
      pars.colorBL = PackedColor(0);
    // BR
    field = GLandscape->OperationalCache()->CheckField(LandIndex(i + iStep), LandIndex(j + jStep));
    if (field)
      pars.colorBR = AgeToColor(field->GetAge());
    else
      pars.colorBR = PackedColor(0);
  }

  pars.spec = NoZBuf|IsAlpha|IsAlphaFog|NoClamp;
  Rect2DPixel rect(x * _wScreen, y * _hScreen, xStep * _wScreen, yStep * _hScreen);
  GEngine->Draw2D(pars, rect, _clipRect);
}

void CStaticMapMain::DrawOperCache()
{
  float ptsLand = 800.0 * _invScaleX * InvLandRange;  // avoid dependence on video resolution
  float invPtsLand = 1.0 / ptsLand;

  int iStep = toIntCeil(_ptsPerSquareCost * invPtsLand);
  if (iStep != 1) return; // draw diagnostics only when zoomed enough
  float xStep = iStep * InvLandRange * _invScaleX;
  int jStep = iStep;
  float yStep = jStep * InvLandRange * _invScaleY;
  int iMin = iStep * toIntFloor(-_mapX / xStep);
  float xMin = _x - fastFmod(-_mapX, xStep);
  int jMin = LandRange - jStep * toIntFloor(-_mapY / yStep);
  float yMin = _y - fastFmod(-_mapY, yStep);

  float y = yMin;
  int j = jMin;
  while (y <= _y + _h)
  {
    float x = xMin;
    int i = iMin;
    while (x <= _x + _w)
    {
      DrawOperCache(x, y, xStep, yStep, i, j, iStep, -jStep);
      i += iStep;
      x += xStep;
    }
    j -= jStep;
    y += yStep;
  }
}

void CStaticMapMain::DrawRoadNet(const MapProperties &props, float x, float y, float xStep, float yStep, int i, int j)
{
  AIBrain *me = GetMyUnit();
  Transport *veh = me ? me->GetVehicleIn() : NULL;
  bool soldier = veh == NULL;
  float vehWidth = 0;
  if (veh)
  {
    LODShape *shape = veh->GetShape();
    if (shape) vehWidth = shape->Max().X() - shape->Min().X();
  }

  PackedColor color(Color(0, 1, 1, 1));
  float textSize = xStep * floatMax(_sizeInfo, 0.1f);

  const RoadList &roadList = GLandscape->GetRoadNet()->GetRoadList(i, j - 1);
  for (int i=0; i<roadList.Size(); i++)
  {
    const RoadLink *item = roadList[i];
    if (!item) continue;
    // each RoadLink contain several nodes for A*
    for (int j=0; j<item->NNodes(soldier); j++)
    {
      RoadNode node = item->GetNode(j);
      if (!node.IsValid()) continue;

      Vector3 posB = node.Position(soldier, vehWidth);
      DrawSign(_texturePath, color, posB, 4, 4, 0);

#if 0 // nodes description
      if (0.2f * textSize >= 0.01f)
      {
        DrawCoord pt = WorldToScreen(posB);
        GEngine->DrawText(Point2DFloat(pt.x + 0.01, pt.y), 0.2f * textSize, Rect2DFloat(_x, _y, _w, _h), _fontInfo, _colorNames, cc_cast(node.GetDebugName()));
      }
#endif

#if 1 // draw connections
      for (int k=0; k<node.NConnections(soldier); k++)
      {
        RoadNode to = node.GetConnection(soldier, k);
        if (!to.IsValid()) continue;

        Vector3 posE = to.Position(soldier, vehWidth);
        DrawArrow(posB, posE, color);
      }
#else // draw orientation
      Vector3 dir = node.Direction();
      if (dir.SquareSizeXZ() > 0.01)
      {
        DrawArrow(posB, posB + 5 * dir, color);
      }
#endif
    }

    if (textSize >= 0.01f)
    {
      RString text;
      if (item->IsBridge()) text = "Bridge - ";
      else text = "Road - ";
      switch (item->GetRoadType())
      {
      case RoadLink::RTError:
        text = text + "Error";
        break;
      case RoadLink::RTDisconnected:
        text = text + "Disconnected";
        break;
      case RoadLink::RTBegin:
        text = text + "Begin";
        break;
      case RoadLink::RTEnd:
        text = text + "End";
        break;
      case RoadLink::RTI:
        text = text + "I";
        break;
      case RoadLink::RTTR:
        text = text + "TR";
        break;
      case RoadLink::RTTL:
        text = text + "TL";
        break;
      case RoadLink::RTX:
        text = text + "X";
        break;
      }

      DrawCoord pt = WorldToScreen(item->Transform().Position());
      GEngine->DrawText(Point2DFloat(pt.x + 0.01, pt.y), textSize, Rect2DFloat(_x, _y, _w, _h), _fontInfo, props._colorNames, text,0);
    }
  }
}

void CStaticMapMain::DrawRoadNet()
{
  float ptsLand = 800.0 * _invScaleX * InvLandRange;  // avoid dependence on video resolution
  float invPtsLand = 1.0 / ptsLand;

  int iStep = toIntCeil(_ptsPerSquareCost * invPtsLand);
  float xStep = iStep * InvLandRange * _invScaleX;
  int jStep = iStep;
  float yStep = jStep * InvLandRange * _invScaleY;
  int iMin = iStep * toIntFloor(-_mapX / xStep);
  float xMin = _x - fastFmod(-_mapX, xStep);
  int jMin = LandRange - jStep * toIntFloor(-_mapY / yStep);
  float yMin = _y - fastFmod(-_mapY, yStep);

  const MapProperties &props = GetMapProperties();

  float y = yMin;
  int j = jMin;
  while (y <= _y + _h)
  {
    float x = xMin;
    int i = iMin;
    while (x <= _x + _w)
    {
      DrawRoadNet(props, x, y, xStep, yStep, i, j);
      i += iStep;
      x += xStep;
    }
    j -= jStep;
    y += yStep;
  }
}

#endif

void CStaticMapMain::DrawUnits()
{
#if _ENABLE_INDEPENDENT_AGENTS
  for (int i=0; i<GWorld->NAgents(); i++)
  {
    AIBrain *unit = GWorld->GetAgent(i);
    if (!unit) continue;
    if (!unit->GetVehicle()) continue;
    PackedColor color;
    switch (unit->GetSide())
    {
    case TWest:
      color = _colorFriendly;
      break;
    case TEast:
      color = _colorEnemy;
      break;
    case TGuerrila:
      color = _colorNeutral;
      break;
    default:
      color = _colorCivilian;
      break;
    }
    if (!unit->LSIsAlive()) color.SetA8(color.A8() / 2);
    Vector3 dir = unit->GetVehicle()->RenderVisualState().Direction();
    float azimut = atan2(dir.X(), dir.Z());
    DrawSign(
      unit->GetVehicle()->GetIcon(), color,
      unit->Position(unit->GetRenderVisualState()),
      _sizeUnit, _sizeUnit, azimut, RString(), false, -1,  _shadow);
  }
#endif // _ENABLE_INDEPENDENT_AGENTS
}

void CStaticMapMain::DrawTargets(AIBrain *observerUnit, bool civilians)
{
  const TargetList *list = observerUnit ? observerUnit->AccessTargetList() : NULL;
  if (list)
  {
    int n = list->AnyCount();
    for (int i=0; i<n; i++)
    {
      const TargetNormal *target = list->GetAny(i);
      if (!target->IsKnownBy(observerUnit) || target->vanished || target->destroyed) continue;

      const VehicleType *type = target->type;
      if
        (
        !type->IsKindOf(GWorld->Preloaded(VTypeAllVehicles)) &&
        !type->IsKindOf(GWorld->Preloaded(VTypeStrategic))
        ) continue;

      // visibility
      float visible = target->FadingSpotability();
      if (visible <= 0.01) continue;
      saturateMin(visible, 1);

      if (!civilians && !CheckItemWantedDraw(target)) continue;
      // color
      TargetSide side = target->side;

      PackedColor color;
      /*if (observerUnit->IsFriendly(side))
      color = _colorFriendly;
      else if (observerUnit->IsEnemy(side))
      color = _colorEnemy;
      else if (observerUnit->IsNeutral(side))
      color = _colorNeutral;
      else
      color = _colorUnknown;*/
      switch (side)
      {
      case TWest:
        {
          color = _colorCivilian;
          break;
        }
      case TEast:
        {
          color = _colorEnemy;
          break;
        }
      case TGuerrila:
        {
          color = _colorFriendly;
          break;
        }
      default:
        {
          color = _colorNeutral;
          break;
        }
      }
      color.SetA8(toIntFloor(color.A8() * visible));

      Vector3 pos = target->LandAimingPosition();
      // azimut not used (known) for group targets
      //Vector3 dir = VZero; // ???
      float azimut = atan2(target->dir.X(), target->dir.Z());
      DrawSign(target->type->GetIcon(), color, pos, _sizeUnit, _sizeUnit, azimut, RString(), NULL, -1, _shadow);
    }
  }
}

void CStaticMapMain::DrawPath()
{
#if _ENABLE_CHEATS
  AIBrain *me = GetMyUnit();
  AIBrain *myPilot = me ? me->GetVehicle()->PilotUnit() : NULL;
  if (!myPilot) return;

  Texture *texture = GPreloadedTextures.New(TextureDefault);

  const IAIPathPlanner &planner = myPilot->GetPlanner();
  DrawSign(texture, PackedColor(Color(1, 0, 0, 1)), planner.GetDestination(), 4, 4, 0);

  AutoArray<Vector3> array;
  planner.GetClosedList(array);
  for (int i=0; i<array.Size(); i++)
  {
    DrawSign(texture, PackedColor(Color(0.5, 0.5, 0.5, 0.5)), array[i], 2, 2, 0);
  }
  planner.GetOpenList(array);
  for (int i=0; i<array.Size(); i++)
  {
    DrawSign(texture, PackedColor(Color(1, 0.5, 0.5, 0.5)), array[i], 2, 2, 0);
  }
  DrawSign(texture, PackedColor(Color(1, 1, 0, 0.5)), planner.GetBest(), 4, 4, 0);
#endif

  /*
  DrawCoord ptB = WorldToScreen(pos);
  DrawCoord ptE;
  ptE.x = ptB.x + 0.02 * dir.X();
  ptE.y = ptB.y - 0.02 * dir.Z();
  GLOB_ENGINE->DrawLine
  (
  Line2DPixel(ptB.x * _wScreen, ptB.y * _hScreen,
  ptE.x * _wScreen, ptE.y * _hScreen),
  _colorMe, _colorMe, _clipRect
  );
  */

}

bool CStaticMapMain::CanCenter()
{
  if
    (
#if _ENABLE_CHEATS
    _showUnits || _showTargets ||
#endif
    Glob.config.IsEnabled(DTMap)
    ) return true;

  AIBrain *me = GetMyUnit();
  if (!me) return false;

  const VehicleType *type = me->GetVehicle()->GetType();
  return type->IsKindOf(GWorld->Preloaded(VTypeAir)) || type->IsKindOf(GWorld->Preloaded(VTypeTank));
}

#if _ENABLE_CHEATS
class DrawUpdateState
{
  CStaticMap *_map;

public:
  explicit DrawUpdateState(CStaticMap *map) : _map(map) {}

  bool operator () (Entity *obj) const
  {
    if (!obj->IsLocal() && !obj->GetNetworkId().IsNull())
    {
      float age = Glob.time - obj->GetLastPositionUpdate();
      float size = 10.0 / (1.0 + age);
      _map->DrawEllipse(obj->RenderVisualState().Position(), size, size, 0, PackedColor(Color(1, 0, 0, 1)));

      age = Glob.time - obj->GetLastGenericUpdate();
      size = 10.0 / (1.0 + age);
      _map->DrawEllipse(obj->RenderVisualState().Position(), size, size, 0, PackedColor(Color(0.25, 0, 0.5, 1)));

      age = Glob.time - obj->GetLastDamageUpdate();
      size = 10.0 / (1.0 + age);
      _map->DrawEllipse(obj->RenderVisualState().Position(), size, size, 0, PackedColor(Color(0, 0.5, 0.25, 1)));
    }
    return false;
  }
};
#endif

#if _ENABLE_CHEATS
EntityAI *CStaticMapMain::GetObserver() const
{
  return GetVehicle(_infoClick);
}
#endif

void CStaticMapMain::DrawExt(float alpha)
{
  //for(int i=0;i<N_MOUSE_BUTTONS;i++)
  //  GInput.ProcessMouseButtonLevel(i,255);

  CStaticMap::DrawExt(alpha);

#if _ENABLE_CHEATS
  if (!_show) return;

  if (CHECK_DIAG(DEOperCache)) DrawOperCache();
  if (CHECK_DIAG(DECostMap)) DrawCost();
  if (CHECK_DIAG(DEPath)) DrawPath();
  if (CHECK_DIAG(DERoads)) DrawRoadNet();
#endif

  // draw dynamic locations
  const float elevationOffset = GLandscape->GetLandElevationOffset();
  for (int i=0; i<GWorld->GetLocations().Size(); i++)
    GWorld->GetLocations()[i]->Draw(this,false,elevationOffset);

  AIBrain *me = GetMyUnit();

#if _ENABLE_CHEATS
  EntityAI *observer = GetVehicle(_infoClick);
  AIBrain *observerUnit = observer ? observer->CommanderUnit() : NULL;
  if (!observerUnit) observerUnit = me;
#else
  AIBrain *observerUnit = me;
  //AIGroup *observerGroup = myGroup;
  //AICenter *observerCenter = myCenter;
#endif

  DrawMarkers();
#if _VBS3 // markers moved to CStaticMap
  if (_onlyDrawMarkers) return;
#endif

#if _ENABLE_IDENTITIES
  if (me && me->GetPerson())
  {
    const Identity &identity = me->GetPerson()->GetIdentity();

    // simple tasks
    const RefArray<Task> &tasks = identity.GetSimpleTasks();
    for (int i=0; i<tasks.Size(); i++)
    {
      const Task *task = tasks[i];
      Vector3 pos;
      if (task && task->GetDestination(pos))
      {
        float alpha = 0.5f;
        if (task == identity.GetCurrentTask()) alpha = 1;
        PackedColor color = ModAlpha(_infoTask.color, alpha);
        Ref<Texture> taskTexture = NULL; 
        switch (task->GetState())
        {
        case TSAssigned:
          {
            taskTexture = _infoTask.icon;
            break;
          }
        case TSSucceeded:
          {
            color = ModAlpha(_infoTask.colorDone, alpha);
            taskTexture = _infoTask.iconDone;
            break;
          }
        case TSCanceled:
          {
            color = ModAlpha(_infoTask.colorCanceled, alpha);
            taskTexture = _infoTask.iconCanceled;
            break;
          }
        case TSFailed:
          {
            color = ModAlpha(_infoTask.colorFailed, alpha);
            taskTexture = _infoTask.iconFailed;
            break;
          }
        case TSCreated:
        case TSNone:
          {
            color = ModAlpha(_infoTask.colorCreated, alpha);
            taskTexture = _infoTask.iconCreated;
            break;
          }
        }
        DrawSign(taskTexture, color, pos, _infoTask.size, _infoTask.size, 0);
      }
    }

    // custom mark
    Vector3 pos;
    if (identity.GetCustomMark(pos))
    {
      DrawSign(_infoCustomMark.icon, _infoCustomMark.color, pos, _infoCustomMark.size, _infoCustomMark.size, 0);
    }
  }
#endif

#if _ENABLE_CHEATS
  if (CHECK_DIAG(DEUpdates))
  {
    GWorld->ForEachVehicle(DrawUpdateState(this));
  }
  if (_showUnits)
  {
    // draw camera positions on server
    const AutoArray<PlayerIdentity> *identities = GetNetworkManager().GetIdentities();
    if (identities) for (int i=0; i<identities->Size(); i++)
    {
      int dpnid = identities->Get(i).dpnid;
      Vector3 pos = GetNetworkManager().GetCameraPosition(dpnid);
      if (pos.SquareSize() > 0.01)
      {
        DrawSign(_iconCamera, PackedColor(Color(0.8, 0.4, 0, 1)), pos, 24, 24, 0);
      }
    }
  }
  if (_showUnits || CHECK_DIAG(DEFSM))
  {
    DrawUnits();
  }
  else if (_showTargets)
  {
    DrawTargets(observerUnit, true);
  }
  else 
#endif
    if (Glob.config.IsEnabled(DTMap) && !_miniMapMode)
    {
      DrawTargets(observerUnit, false);
    }
    else DrawInfo();

    // draw clicked unit (helpful for cheats)
#if _ENABLE_CHEATS
    EntityAI *vehClick = GetVehicle(_infoClick);
    if (vehClick)
    {
      PackedColor color(Color(1, 1, 1, 0.5));
      if (_showUnits)
      {
        // draw directly on the vehicle position
        DrawSign(_iconPlayer, color, vehClick->RenderVisualState().Position(), _sizePlayer, _sizePlayer, 0);
      }
      else if (Glob.config.IsEnabled(DTMap) && !_miniMapMode)
      {
        const TargetList *list = observerUnit ? observerUnit->AccessTargetList() : NULL;
        if (list)
        {
          const TargetNormal *target = list->FindTarget(vehClick);
          if (target)
          {
            DrawSign(_iconPlayer, color, target->LandAimingPosition(), _sizePlayer, _sizePlayer, 0);
          }
        }
      }
    }
#endif

    // draw myself
    if
      (
#if _ENABLE_CHEATS
      _showUnits ||
#endif
      Glob.config.IsEnabled(DTMap) || _miniMapMode
      )
    {
      Object *myVehicle = NULL;
      if (me)
      {
        myVehicle = me->GetVehicle();
      }
      else if (GLOB_WORLD->CameraOn())
      {
        myVehicle = GLOB_WORLD->CameraOn();
      }

      if (myVehicle)
      {
        Point3 pos = myVehicle->RenderVisualState().Position();
        PackedColor BlinkColor(PackedColor color1, PackedColor color2, float t);
        PackedColor color = BlinkColor(_colorMe, _colorMe2, _blinkingSpeedMe * Glob.uiTime.toFloat());
        bool draw = DrawSign
          (
          _iconPlayer, color,
          pos,
          _sizePlayer, _sizePlayer, 0
          );
        if (draw)
        {
          // draw orientation
          Vector3 dir = myVehicle->RenderVisualState().Direction();
          dir[1] = 0;
          dir.Normalize();
          DrawCoord ptB = WorldToScreen(pos);
          DrawCoord ptE;
          ptE.x = ptB.x + 0.02 * dir.X();
          ptE.y = ptB.y - 0.02 * dir.Z();
          GLOB_ENGINE->DrawLine
            (
            Line2DPixel(ptB.x * _wScreen, ptB.y * _hScreen,
            ptE.x * _wScreen, ptE.y * _hScreen),
            color, color, _clipRect
            );
        }
      }

    }
    else if (me)
    {
      const VehicleType *type = me->GetVehicle()->GetType();
      if
        (
        type->IsKindOf(GWorld->Preloaded(VTypeAir)) ||
        type->IsKindOf(GWorld->Preloaded(VTypeTank))
        )
      {
        PackedColor color = PackedColor(Color(0, 0, 0, 1));
        const float h1 = 4.0 * (1.0 / 480);
        const float w1 = 4.0 * (1.0 / 640);
        const float h2 = 8.0 * (1.0 / 480);
        const float w2 = 8.0 * (1.0 / 640);
        DrawCoord pt = WorldToScreen(me->Position(me->GetRenderVisualState()));

        float w = _wScreen;
        float h = _hScreen;

        GEngine->DrawLine
          (
          Line2DPixel(_clipRect.x, pt.y * h, (pt.x - w1) * w, pt.y * h),
          color, color, _clipRect
          );
        GEngine->DrawLine
          (
          Line2DPixel((pt.x + w1) * w, pt.y * h, _clipRect.x + _clipRect.w, pt.y * h),
          color, color, _clipRect
          );
        GEngine->DrawLine
          (
          Line2DPixel(pt.x * w, _clipRect.y, pt.x * w, (pt.y - h1) * h),
          color, color, _clipRect
          );
        GEngine->DrawLine
          (
          Line2DPixel(pt.x * w, (pt.y + h1) * h, pt.x * w, _clipRect.y + _clipRect.h),
          color, color, _clipRect
          );
        MipInfo mip = GEngine->TextBank()->UseMipmap(_iconPosition, 0, 0);
        GEngine->Draw2D
          (
          mip, color,
          Rect2DPixel(CX(pt.x - w2), CY(pt.y - h2), CW(2 * w2), CH(2 * h2)), _clipRect
          );
      }
    }

    //  if (_showTargets || _showUnits || Glob.config.easyMode)
    {
      DrawLabel(_infoMove, _colorInfoMove,0);
    }
#if _ENABLE_CHEATS
    if (_showUnits)
    {
      // draw labels
      // draw my path
      AIBrain *myPilot = me ? me->GetVehicle()->PilotUnit() : NULL;
      if (myPilot)
      {
        int n = myPilot->GetPlanner().GetPlanSize();
        int m = myPilot->GetPlanner().FindBestIndex(me->Position(me->GetRenderVisualState()));
        Vector3 oldPos;
        for (int i=0; i<m; i++)
        {
          PlanPosition pos;
          myPilot->GetPlanner().GetPlanPosition(i, pos);
          if (i > 0) DrawLine(oldPos, pos, _colorPathDone);
          oldPos = pos;
          DrawSign(_texturePathDone, _colorPathDone, pos, _sizePathDone, _sizePathDone, 0);
        }
        for (int i=m; i<n; i++)
        {
          PlanPosition pos;
          myPilot->GetPlanner().GetPlanPosition(i, pos);
          if (i > 0) DrawLine(oldPos, pos, _colorPath);
          oldPos = pos;
          DrawSign(_texturePath, _colorPath, pos, _sizePath, _sizePath, 0);
        }
      }

      if (CHECK_DIAG(DEZombie))
      {
        const AutoArray<FSMItem> &fsms = GWorld->GetFSMs();
        for (int i=0; i<fsms.Size(); i++)
        {
          FSM *fsm = fsms[i]._fsm;
          if (!fsm) continue;
          GameVarSpace *vars = fsm->GetVars();
          if (!vars) continue;
          
          Object *GetObject(GameValuePar oper);

          GameValue value;
          if (!vars->VarGet("_agent", value)) continue;
          Object *agent = GetObject(value);
          if (!agent) continue;

          Vector3Val pos = agent->FutureVisualState().Position();
          CStaticMap::DrawLabel(pos, fsm->GetStateName(), PackedWhite, false);

          Object *target = NULL;
          if (vars->VarGet("_target", value)) target = GetObject(value);
          if (target)
          {
            Vector3Val posTarget = target->FutureVisualState().Position();
            DrawArrow(pos, posTarget, PackedBlack);
          }

        }
      }
    }
#endif

    if (_activeMarker >= 0)
    {
      const float rectH2 = 0.5 * _sizeActiveMarker * _hScreen * (1.0 / 480);
      const float rectW2 = 0.5 * _sizeActiveMarker * _wScreen * (1.0 / 640);
      ArcadeMarkerInfo &mInfo = markersMap[_activeMarker];
      DrawCoord pt = WorldToScreen(mInfo.position);
      pt.x *= _wScreen;
      pt.y *= _hScreen;
      GEngine->DrawLine
        (
        Line2DPixel(pt.x - rectW2, pt.y - rectH2, pt.x + rectW2, pt.y - rectH2),
        _colorActiveMarker, _colorActiveMarker, _clipRect
        );
      GEngine->DrawLine
        (
        Line2DPixel(pt.x + rectW2, pt.y - rectH2, pt.x + rectW2, pt.y + rectH2),
        _colorActiveMarker, _colorActiveMarker, _clipRect
        );
      GEngine->DrawLine
        (
        Line2DPixel(pt.x + rectW2, pt.y + rectH2, pt.x - rectW2, pt.y + rectH2),
        _colorActiveMarker, _colorActiveMarker, _clipRect
        );
      GEngine->DrawLine
        (
        Line2DPixel(pt.x - rectW2, pt.y + rectH2, pt.x - rectW2, pt.y - rectH2),
        _colorActiveMarker, _colorActiveMarker, _clipRect
        );
      GEngine->DrawLine
        (
        Line2DPixel(_clipRect.x, pt.y, pt.x - rectW2, pt.y),
        _colorActiveMarker, _colorActiveMarker, _clipRect
        );
      GEngine->DrawLine
        (
        Line2DPixel(pt.x + rectW2, pt.y, _clipRect.x + _clipRect.w, pt.y),
        _colorActiveMarker, _colorActiveMarker, _clipRect
        );
      GEngine->DrawLine
        (
        Line2DPixel(pt.x, _clipRect.y, pt.x, pt.y - rectH2),
        _colorActiveMarker, _colorActiveMarker, _clipRect
        );
      GEngine->DrawLine
        (
        Line2DPixel(pt.x, pt.y + rectH2, pt.x, _clipRect.y + _clipRect.h),
        _colorActiveMarker, _colorActiveMarker, _clipRect
        );
    }

#if _ENABLE_CHEATS
    if (_showVariables > 0)
    {
      const VarBankType *vars = NULL;
      switch (_showVariables)
      {
      case 1:
        vars = &GWorld->GetMissionNamespace()->GetVariables(); // mission namespace
        break;
      case 2:
        {
          Person *body = observerUnit->GetPerson();
          if (body)
          {
            GameVarSpace *varSpace = body->GetVars();
            if (varSpace) vars = &varSpace->GetVariables();
          }
        }
        break;
      case 3:
        {
          Transport *vehicle = observerUnit->GetVehicleIn();
          if (vehicle)
          {
            GameVarSpace *varSpace = vehicle->GetVars();
            if (varSpace) vars = &varSpace->GetVariables();
          }
        }
        break;
      }

      if (vars && vars->NItems() > 0)
      {
        float top = _y + 0.2;
        float left = _x + 0.02;
        // !!! avoid GetTable when NItems == 0
        for (int i=0; i<vars->NTables(); i++)
        {
          for (int j=0; j<vars->GetTable(i).Size(); j++)
          {
            GameVariable &var = vars->GetTable(i)[j];
            if (GWorld->GetGameState()->IsVisible(var)) continue;
            GLOB_ENGINE->DrawTextF
              (
              Point2DFloat(left, top), _sizeLabel, _fontLabel, PackedColor(Color(0, 0, 0, 1)), 0,
              "%s = %s",
              (const char *)var.GetName(),
              (const char *)var.GetValueText()
              );
            top += _sizeLabel;
          }
        }
      }
    }
#endif

    DrawLegend();
}

// simulation
void CStaticMapMain::Center(bool soft)
{
  Point3 pt = _defaultCenter;
  if (GLOB_WORLD->FocusOn())
    pt = GLOB_WORLD->FocusOn()->Position(GLOB_WORLD->FocusOn()->GetRenderVisualState());
  else if (GLOB_WORLD->CameraOn())
    pt = GLOB_WORLD->CameraOn()->RenderVisualState().Position();
  CStaticMap::Center(pt, soft);
}

void CStaticMapMain::CenterMiniMap()
{
  const MapProperties &props = GetMapProperties();

  Point3 pt = _defaultCenter;
  float maxSpeed = 0;
  Vector3 vspeed(VZero);
  if (GLOB_WORLD->FocusOn())
  {
    pt = GLOB_WORLD->FocusOn()->Position(GLOB_WORLD->FocusOn()->GetRenderVisualState());
    EntityAI *entity = GLOB_WORLD->FocusOn()->GetVehicle();
    if (entity) 
    {
      maxSpeed = entity->GetType()->GetMaxSpeedMs();
      vspeed = entity->RenderVisualState().Speed();
    }
  }
  saturateMax(maxSpeed, 0.1f);
  float speed = vspeed.Size();
  const float timeCoef = 30.0f; //we want to see timeCoef * speed map location
  const float minSpeed = 7.0f;
  bool centralizeOnly = false;
  if (speed < minSpeed)
  {
    speed = minSpeed;
    centralizeOnly = true; //no map scaling nor player icon moving
  }

  float invSizeLand = 1 / (LandGrid * LandRange);
  //_scaleX is set so that map width corresponds to speed*timeCoef length in world space
  _scaleX = timeCoef * speed * invSizeLand / W();
  saturate(_scaleX, props._scaleMin, props._scaleMax);
  Precalculate(props);

  float xMap = pt.X() * invSizeLand * _invScaleX;
  float yMap = (1.0 - pt.Z() * invSizeLand) * _invScaleY;
  _mapX = _center.x - xMap;
  _mapY = _center.y - yMap;
  if (!centralizeOnly)
  {
    //shift the map in such a way, player is on elipse around map center (diameter given by speed / maxSpeed)
    float speedXZ = vspeed.SizeXZ();
    if (speedXZ > minSpeed) //for slow movement no effect
    {
      const float maxDiamCoef = 0.75f; //75%
      float speedCoef = (speed-minSpeed) / maxSpeed;
      saturateMin(speedCoef,1.0f);
      float sinus = vspeed.Z() / speedXZ;
      float cosinus = vspeed.X() / speedXZ;
      _mapX -= cosinus * maxDiamCoef * speedCoef * _halfSize.x;
      _mapY += sinus * maxDiamCoef * speedCoef * _halfSize.y;
    }
  }
  SaturateX(_mapX);
  SaturateY(_mapY);
}

void CStaticMapMain::OnDraw(UIViewport *vp, float alpha)
{
  if (_autoCenter) Center(false);
  CStaticMap::OnDraw(vp, alpha);
}

void CStaticMapMain::FindUnit(Vector3Par pt, SignInfo &info, float &minDist)
{
#if _ENABLE_INDEPENDENT_AGENTS
  for (int i=0; i<GWorld->NAgents(); i++)
  {
    AIBrain *unit = GWorld->GetAgent(i);
    if (!unit) continue;
    float dist = (pt - unit->Position(unit->GetRenderVisualState())).SquareSizeXZ();
    if (dist < minDist)
    {
      minDist = dist;
      info._type = signUnit;
      info._unit = unit;
    }
  }
#endif // _ENABLE_INDEPENDENT_AGENTS
}

void CStaticMapMain::FindTarget(Vector3Par pt, SignInfo &info, float &minDist, AIBrain *observerUnit, bool civilians)
{
  const TargetList *list = observerUnit ? observerUnit->AccessTargetList() : NULL;
  if (list)
  {
    int n = list->AnyCount();
    for (int i=0; i<n; i++)
    {
      const TargetNormal *target = list->GetAny(i);
      if (!target->IsKnownBy(observerUnit) || target->vanished || target->destroyed) continue;

      // visibility
      float visible = target->FadingSpotability();
      if (visible <= 0.01) continue;

      if (!civilians && !CheckItemWantedDraw(target)) continue;

      float dist = (pt - target->LandAimingPosition()).SquareSizeXZ();
      if (dist < minDist)
      {
        if (target->type->IsKindOf(GWorld->Preloaded(VTypeStrategic)))
        {
          minDist = dist;
          info._type = signStatic;
          info._id = target->idExact;
        }
        else if (target->type->IsKindOf(GWorld->Preloaded(VTypeAllVehicles)))
        {
          minDist = dist;
          info._type = signVehicleGroup;
          info._id = target->idExact;
        }
      }
    }
  }
}

SignInfo CStaticMapMain::FindSign(float x, float y)
{
  AIBrain *me = GetMyUnit();

  struct SignInfo info;
  info._type = signNone;
  info._unit = NULL;
  info._id = NULL;

#if _ENABLE_CHEATS
  EntityAI *observer = GetVehicle(_infoClick);
  AIBrain *observerUnit = observer ? observer->CommanderUnit() : NULL;
  if (!observerUnit) observerUnit = me;
#else
  AIBrain *observerUnit = me;
#endif

  Point3 pt = ScreenToWorld(DrawCoord(x, y));
  float sizeLand = LandGrid * LandRange;
  float dist, minDist = 0.02 * sizeLand * _scaleX;
  minDist *= minDist;

#if _ENABLE_CHEATS
  if (_showUnits)
  {
    FindUnit(pt, info, minDist);
  }
  else if (_showTargets)
  {
    FindTarget(pt, info, minDist, observerUnit, true);
  }
#endif
  if (Glob.config.IsEnabled(DTMap) && !_miniMapMode)
  {
    FindTarget(pt, info, minDist, observerUnit, false);
  }

  // check tasks
#if _ENABLE_IDENTITIES
  if (me)
  {
    const Identity &identity = me->GetPerson()->GetIdentity();

    // custom mark
    Vector3 pos;
    if (identity.GetCustomMark(pos))
    {
      float dist = (pt - pos).SquareSizeXZ();
      if (dist < minDist)
      {
        minDist = dist;
        info._type = signCustomMark;
        info._unit = me;
      }
    }

    // simple tasks
    const RefArray<Task> &tasks = identity.GetSimpleTasks();
    for (int i=0; i<tasks.Size(); i++)
    {
      const Task *task = tasks[i];
      Vector3 pos;
      if (task && task->GetDestination(pos))
      {
        float dist = (pt - pos).SquareSizeXZ();
        if (dist < minDist)
        {
          minDist = dist;
          info._type = signTask;
          info._unit = me;
          info._index = task->GetId();
        }
      }
    }
  }
#endif

  // check markers
  int n = markersMap.Size();
  for (int i=0; i<n; i++)
  {
    ArcadeMarkerInfo &mInfo = markersMap[i];
    dist = (pt - mInfo.position).SquareSizeXZ();
    if (dist < minDist)
    {
      minDist = dist;
      info._type = signArcadeMarker;
      info._index = i;
    }
  }
  return info;
}

inline bool IsWatchRequired()
{
  return GInput.keys[DIK_LMENU]>0;
}

#if _ENABLE_IDENTITIES
/// in-game navigate to the given diary topic, open a diary if needed

void ProcessDiaryLink(RString link, DisplayMap *map)
{//need to find better solution
  DisplayMainMap *mapIngame = dynamic_cast<DisplayMainMap *>(map);

  if (mapIngame)
  {
    GWorld->ShowMap(true);
    mapIngame->ProcessDiaryLink(link, DCInGame);
  }
  else
  {
    DisplayGetReady *readyMap = dynamic_cast<DisplayGetReady *>(map);
    if (readyMap)
    {
      readyMap->ProcessDiaryLink(link, DCBriefing);
    }
  }
}

void ProcessDiaryLink(RString link)
{
  ProcessDiaryLink(link, dynamic_cast<DisplayMap *>(GWorld->Map()));
}


#endif

bool CStaticMapMain::OnKeyDown(int dikCode)
{
  if (dikCode == 0) return false;

  if (CStaticMap::OnKeyDown(dikCode))
    return true;

  switch (dikCode)
  {
  case INPUT_DEVICE_XINPUT + XBOX_RightTrigger:
    {
      float mouseX = 0.5 + GInput.cursorX * 0.5;
      float mouseY = 0.5 + GInput.cursorY * 0.5;
      saturate(mouseX, 0, 1);
      saturate(mouseY, 0, 1);

      bool alt = IsWatchRequired();
      IssueCommand(mouseX, mouseY, alt, false);
    }
    return true;
  case DIK_DELETE:
    if (_infoMove._type == signArcadeMarker)
    {
      const char *userDefined = "_user_defined";
      RString name = markersMap[_infoMove._index].name;
      if (strnicmp(name, userDefined, strlen(userDefined)) == 0)
      {
#if _VBS3 // marker EH
        void ProcessDeleteMarkerEH(RString name);
        ProcessDeleteMarkerEH(name);
#endif
        GetNetworkManager().MarkerDelete(name);
        markersMap.Delete(_infoMove._index);
        _infoClick._type = signNone;
        _infoClickCandidate._type = signNone;
        _infoMove._type = signNone;
        _dragging = false;
        _selecting = false;
      }
      return true;
    }
    // continue
  case INPUT_DEVICE_XINPUT + XBOX_X:
#if _ENABLE_IDENTITIES
    if (_infoMove._type == signCustomMark)
    {
      Person *person = _infoMove._unit ? _infoMove._unit->GetPerson() : NULL;
      if (person)
      {
        person->GetIdentity().ClearCustomMark();
        _infoClick._type = signNone;
        _infoClickCandidate._type = signNone;
        _infoMove._type = signNone;
        _dragging = false;
        _selecting = false;
      }
      return true;
    }
#endif
    return true;
  case INPUT_DEVICE_XINPUT + XBOX_Y:
#if _ENABLE_IDENTITIES
    {
      AIBrain *me = GetMyUnit();
      Person *person = me ? me->GetPerson() : NULL;
      if (person)
      {
        float mouseX = 0.5 + GInput.cursorX * 0.5;
        float mouseY = 0.5 + GInput.cursorY * 0.5;
        saturate(mouseX, 0, 1);
        saturate(mouseY, 0, 1);
        Vector3 pos = ScreenToWorld(DrawCoord(mouseX, mouseY));
        pos[1] = GLandscape->SurfaceYAboveWater(pos[0],pos[2]);
        person->GetIdentity().SetCustomMark(pos);
      }

    }
#endif
    return true;
  case INPUT_DEVICE_XINPUT + XBOX_A:
#if _ENABLE_IDENTITIES
    if (_infoMove._type == signTask)
    {
      RString link = Format("task:%d", _infoClick._index);
      ProcessDiaryLink(link);
      return true;
    }
#endif
    return true;
  }
  return false;
}

void CStaticMapMain::ProcessCheats()
{
#if _ENABLE_CHEATS
  if (GInput.GetCheat1ToDo(DIK_A) || GInput.GetCheatXToDo(CXTMapShowUnits))
  {
    _showUnits = !_showUnits;
    GlobalShowMessage(500,"Show Units %s",_showUnits ? "On" : "Off");
  }
  if (GInput.GetCheat1ToDo(DIK_D))
  {
    _showTargets = !_showTargets;
    GlobalShowMessage(500,"Show Targets %s",_showTargets ? "On" : "Off");
  }
  if (GInput.GetCheat2ToDo(DIK_V))
  {
    _showVariables++;
    if (_showVariables > 3) _showVariables = 0;
    switch (_showVariables)
    {
    case 0:
      GlobalShowMessage(500,"No variables"); break;
    case 1:
      GlobalShowMessage(500,"Global variables"); break;
    case 2:
      GlobalShowMessage(500,"Person's variables"); break;
    case 3:
      GlobalShowMessage(500,"Vehicle's variables"); break;
    }
  }
#endif
  CStaticMap::ProcessCheats();
}

void CStaticMapMain::OnLButtonDown(float x, float y)
{
  _mouseClickHandled = false;

  _infoClick = FindSign(x, y);
#if _ENABLE_IDENTITIES
  switch (_infoClick._type)
  {
  case signCustomMark:
    {
      _dragging = true;
      _lastPos = ScreenToWorld(DrawCoord(x, y));
    }
  }
#endif
}

void CStaticMapMain::OnRButtonDown(float x, float y)
{
  CStaticMap::OnRButtonDown(x,y);
}


extern GameValue GMapOnSingleClick;
extern GameValue GMapOnSingleClickParams;

void CStaticMapMain::IssueCommand(float x, float y, bool alt, bool shift)
{
  bool issueCommand = true;

  bool eventHandlerValid = !GMapOnSingleClick.GetNil();
  if (eventHandlerValid)
  {
#if USE_PRECOMPILATION
    if (GMapOnSingleClick.GetType() == GameCode)
    {
      GameDataCode *code = static_cast<GameDataCode *>(GMapOnSingleClick.GetData());
      eventHandlerValid = code->IsCompiled() && code->GetCode().Size() > 0;
    }
    else
#endif
    {
      RString code = GMapOnSingleClick;
      eventHandlerValid = code.GetLength() > 0;
    }
  }

  if (eventHandlerValid)
  {
    Vector3 pos = ScreenToWorld(DrawCoord(x, y));
    GameState *state = GWorld->GetGameState();

    GameValue posValue = state->CreateGameValue(GameArray);
    GameArrayType &posArray = posValue;
    posArray.Realloc(3);
    posArray.Add(pos[0]);
    posArray.Add(pos[2]);
    posArray.Add(pos[1]-GLandscape->SurfaceYAboveWater(pos[0],pos[2]));

    GameVarSpace vars(false);
    int gshandle = state->BeginContext(&vars);
    state->VarSetLocal("_pos",posValue,true);
    state->VarSetLocal("_alt",alt,true);
    state->VarSetLocal("_shift",shift,true);
    state->VarSetLocal("_this", GMapOnSingleClickParams, true);
#if USE_PRECOMPILATION
    if (GMapOnSingleClick.GetType() == GameCode)
    {
      GameDataCode *code = static_cast<GameDataCode *>(GMapOnSingleClick.GetData());
      issueCommand = !state->EvaluateBool(code->GetString(), code->GetCode(), GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
    }
    else
#endif
    {
      // make sure string is not destructed while being evaluated
      RString code = GMapOnSingleClick;
      issueCommand = !state->EvaluateMultipleBool(code, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
    }
    state->EndContext(gshandle);
  }

  if (issueCommand)
  {
#if _ENABLE_IDENTITIES
    if (shift)
    {
      AIBrain *me = GetMyUnit();
      Person *person = me ? me->GetPerson() : NULL;
      if (person)
      {
        Vector3 pos = ScreenToWorld(DrawCoord(x, y));
        pos[1] = GLandscape->SurfaceYAboveWater(pos[0],pos[2]);
        person->GetIdentity().SetCustomMark(pos);
      }
    }
#endif
  }
}

void CStaticMapMain::OnLButtonClick(float x, float y)
{
  if(_mouseClickHandled)
  {
    _mouseClickHandled = false;
    return;
  }
  if (!_dragging)
  {
#if _ENABLE_CHEATS && USER_Ondra
	  //not desired for DayZ, but may be useful for debugging
	  bool alt = IsWatchRequired();
    bool shift = GInput.keys[DIK_LSHIFT]>0;
    IssueCommand(x, y, alt, shift);
#endif
  }
}

void CStaticMapMain::OnLButtonDblClick(float x, float y)
{
# if _ENABLE_IDENTITIES
  // double click on the task icon -> find the task in the diary
  if (_infoClick._type == signTask)
  {
    RString link = Format("task:%d", _infoClick._index);
    ProcessDiaryLink(link, dynamic_cast<DisplayMap *>(_parent.GetLink()));
    return;
  }
# endif // _ENABLE_IDENTITIES

  if (dynamic_cast<DisplayMap *>(_parent.GetLink()))
    _parent->CreateChild(new DisplayInsertMarker(_parent, x, y, _x, _y, _w, _h, _parent->SimulationEnabled()));
}

void CStaticMapMain::OnLButtonUp(float x, float y)
{
  if (_dragging)
  {
    _dragging = false;
  }
}


void CStaticMapMain::OnMouseHold(float x, float y, bool active)
{
  if (_dragging)
  {
#if _ENABLE_IDENTITIES
    if (_infoClick._type == signCustomMark)
    {
      Person *person = _infoMove._unit ? _infoMove._unit->GetPerson() : NULL;
      if (person)
      {
        Vector3 pos;
        if (person->GetIdentity().GetCustomMark(pos))
        {
          Vector3 curPos = ScreenToWorld(DrawCoord(x, y));
          Vector3 offset = curPos - _lastPos;

          pos = pos + offset;
          pos[1] = GLOB_LAND->RoadSurfaceYAboveWater(pos[0], pos[2]);
          person->GetIdentity().SetCustomMark(pos);

          _lastPos = curPos;
        }
      }
    }
#endif
  }

  CStaticMap::OnMouseHold(x, y);
}

void CStaticMapMain::OnMouseMove(float x, float y, bool active)
{
  CStaticMap::OnMouseMove(x, y);
}

void CStaticMapMain::OnMouseZChanged(float dz)
{
  CStaticMap::OnMouseZChanged(dz);
}

///////////////////////////////////////////////////////////////////////////////
// Main Map display

WeatherState GetWeatherState(float overcast)
{
  if (overcast < 0.2) return WSClear;
  else if (overcast < 0.4) return WSCloudly;
  else if (overcast < 0.6) return WSOvercast;
  else if (overcast < 0.8) return WSRainy;
  else return WSStormy;
}

static const EnumName ObjectiveStatusNames[]=
{
  EnumName(OSActive, "ACTIVE"),
  EnumName(OSDone, "DONE"),
  EnumName(OSFailed, "FAILED"),
  EnumName(OSHidden, "HIDDEN"),
  EnumName()
};

template<>
const EnumName *GetEnumNames(ObjectiveStatus dummy)
{
  return ObjectiveStatusNames;
}

#include "../saveVersion.hpp"


class TextToHTMLParser : public SAXParser
{
protected:
  CHTMLContainer *_html;
  int _section;

public:
  TextToHTMLParser(CHTMLContainer *html, int section);
  void OnStartElement(RString name, XMLAttributes &attributes);
  void OnEndElement(RString name);
  void OnCharacters(RString chars);
};

TextToHTMLParser::TextToHTMLParser(CHTMLContainer *html, int section)
  : _html(html), _section(section)
{
}

void TextToHTMLParser::OnStartElement(RString name, XMLAttributes &attributes)
{
  if (strcmp(name, "br") == 0)
  {
    _html->AddBreak(_section, false);
  }
  else
  {
    RptF("Unknown text element '%s'", (const char *)name);
  }
}

void TextToHTMLParser::OnEndElement(RString name)
{
}

void TextToHTMLParser::OnCharacters(RString chars)
{
  _html->AddText(_section, chars, HFP, HALeft, false, false, RString());
}

class DisplayInventory : public Display
{
protected:
  OLinkPermNO(AIBrain) _unit;
  Ref<EntityAI> _targetContainer;

  // drag & drop info
  OLinkPermO(InventoryItem) _dragItem;
  bool _dragRotated;
  float _dragOffsetX, _dragOffsetY;
  float _dragGridW, _dragGridH;
  float _dragZ;
  PackedColor _dragColorNone;
  PackedColor _dragColorMove;
  PackedColor _dragColorCraft;
  PackedColor _dragColorSwap;

  OLinkPermO(InventoryItem) _swapItem;

  enum DragAction
  {
    DANone = 0,
    DAMove,
    DACraft,
    DASwap
  };

  // tooltip drawing
  float _tooltipHeight;
  float _tooltipImageWidth;
  float _tooltipTextWidth;
  float _tooltipTextSize;
  PackedColor _tooltipBgColor;
  PackedColor _tooltipLineColor;

  // extension of menu item for an Inventory item action
  class MenuItemAction : public MenuItem
  {
  private:
    OLinkPermO(InventoryItem) _item;
    Ref<Action> _action;
    DisplayInventory *_parent;

  public:
    MenuItemAction(AIBrain *unit, InventoryItem *item, Action *action, DisplayInventory *parent);
    void Invoke(AIBrain *unit);
  };
  class MenuItemRecipe : public MenuItem
  {
  private:
    OLinkPermO(InventoryItem) _item1, _item2;
    const Recipe *_recipe;
  
  public:
    MenuItemRecipe(AIBrain *unit, InventoryItem *item1, InventoryItem *item2, const Recipe *recipe);
    void Invoke(AIBrain *unit);
  };
  class MenuItemAddItem : public MenuItem
  {
  private:
    OLinkPermO(InventoryItem) _item1, _item2;
  
  public:
    MenuItemAddItem(InventoryItem *item1, InventoryItem *item2, bool attachment);
    void Invoke(AIBrain *unit);
  };
  class MenuItemSwapItems : public MenuItem
  {
  private:
    OLinkPermO(InventoryItem) _item1, _item2;
  
  public:
    MenuItemSwapItems(InventoryItem *item1, InventoryItem *item2);
    void Invoke(AIBrain *unit);
  };
  enum MenuCommands
  {
    MCAction,
    MCRecipe,
    MCSwap,
    MCWait, // wait while menu is loaded over network
    MCAdd
  };

  static int _instances;

public:
  DisplayInventory(ControlsContainer *parent, AIBrain *me, EntityAI *source);
  ~DisplayInventory();

  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  ControlObject *OnCreateObject(int type, int idc, ParamEntryPar cls);
  void OnSimulate(EntityAI *vehicle);
  void DestroyHUD(int exit)
  {
    GWorld->DestroyUserDialog();
  }

  void OnButtonDblClick(IControl *ctrl);

  virtual void OnDrag(IControl *ctrl) override;
  virtual unsigned char OnDragging(IControl *draggCtrl,  IControl *dropCtrl, float x, float y) override;
  virtual bool OnDrop(IControl *draggCtrl, IControl *dropCtrl, float x, float y) override;
  virtual bool OnKeyDown(int dikCode) override;
  
  virtual void DrawDragInfo(float x, float y, unsigned char enabled, Ref<Font> dragFont, float dragSize) override;

  void DrawTooltip(UIViewport *vp, float x, float y, const InventoryItem *item);

  virtual bool OnMenuCreated(Menu &menu, float x, float y, int idc, CCMContext &ctx) override;
  virtual void OnMenuSelected(MenuItem *item) override;

  void ShowInventoryMenu(const InventoryMenuMessage &msg);

  static bool IsOpened() {return _instances > 0;}

  const InventoryItem *GetDraggedItem() const {return _dragItem;}
  const InventoryItem *GetSwappedItem() const {return _swapItem;}
};

void CItemBase::SetItemShortcut(int index, const InventoryItem *item)
{
  if (!GWorld) return;
  Person *player = GWorld->PlayerOn();
  if (!player) return;

  const InventoryItemType *type = item ? item->Type() : NULL;
  if (!GetNetworkManager().IsServerOwner())
    // BOT client have to handle it
    GetNetworkManager().AskForSetItemShortcut(player, index, type, const_cast<InventoryItem *>(item));
  else
    player->SetItemShortcutInstance(index, const_cast<InventoryItem *>(item));
}

void CItemBase::DropItem(Person *owner, InventoryItem *item)
{
  if (!GetNetworkManager().IsServerOwner())
    // BOT client have to handle it
    GetNetworkManager().AskForDropItem(owner, item);
  else
  {
    Entity *parent = item->GetHierarchyParent();
    if (parent) parent->RemoveOwnedItem(item);

    // dropped item nor any contained item cannot stay in hands
    if (owner)
    {
      const InventoryItem *inHands = owner->GetItemInHands();
      if (inHands)
      {
        if (inHands == item) owner->RemoveItemInHands();
        else item->ForEachItem([&owner, &inHands](const InventoryItem *child) -> bool
        {
          if (child == inHands)
          {
            owner->RemoveItemInHands();
            return true;
          }
          return false;
        });
      }
    }

    Vector3Val pos = owner->FutureVisualState().Position();
    Vector3Val dir = owner->FutureVisualState().Direction();
    item->ItemDrop(owner, pos, dir);
  }
}

void CItemBase::MoveItem(EntityAI *target, int index, InventoryItem *item)
{
  // CItemBase::CanMoveItem should be called and succeed before

  if (!GetNetworkManager().IsServerOwner())
    // BOT client have to handle it
    GetNetworkManager().AskForMoveItem(target, index, item, item->IsInventoryRotated());
  else
  {
    Entity *parent = item->GetHierarchyParent();
    if (parent) parent->RemoveOwnedItem(item);

    target->MoveItem(index, item);
  }
}

bool CItemBase::CanMoveItem(EntityAI *target, int index, const InventoryItem *item)
{
  return target->CanMoveItem(index, item);
}

void CItemBase::RotateInventoryItem(Person *person, InventoryItem *item)
{
  // Items in the hands slot are manipulated by dragging them out of the slot,
  // never by changing their inventory orientation in place.
  if (!item || (person && person->GetItemInHands() == item)) return;
  if (!GetNetworkManager().IsServerOwner())
    GetNetworkManager().AskForRotateInventoryItem(person, item);
  else if (item->RotateInInventory())
    GetNetworkManager().UpdateObject(item, NMCUpdateGeneric);
}

void CItemBase::TakeItemToHands(Person *person, InventoryItem *item)
{
  if (!GetNetworkManager().IsServerOwner())
    // BOT client have to handle it
    GetNetworkManager().AskForTakeItemToHands(person, item);
  else
    person->TakeItemToHands(item);
}

struct CCMContextInventory : public CCMContext
{
  OLinkPermO(InventoryItem) _item1, _item2;
  CCMContextInventory(InventoryItem *item1, InventoryItem *item2) {_item1 = item1; _item2 = item2;}
};

// highlighting of manipulated items 
static Color ColorItemDragged(1.0f, 1.0f, 1.0f, 0.5f);
static Color ColorItemSwapped(1.0f, 1.0f, 1.0f, 0.5f);

CItemBase::CItemBase(ControlsContainer *parent, int idc, ParamEntryPar cls) : ControlObject(parent, idc, cls)
{
  _dragX = _dragY = 0;

  _dragging = false;

  _x = cls >> "x";
  _y = cls >> "y";
  _w = cls >> "w";
  _h = cls >> "h";
  _z = cls >> "z";

  _bgColor = GetPackedColor(cls >> "colorBackground");
  _borderColor = GetPackedColor(cls >> "colorBorder");
}

bool CItemBase::IsInside(float x, float y) const
{
  return x >= _x && x <= _x + _w && y >= _y && y <= _y + _h;
}

void CItemBase::OnMouseMove(float x, float y, bool active)
{
  // hotfix when OnLButtonUp was not called
  if (!GInput.mouseL) return; // maybe drag & drop cancel reaction would be needed

  if (_moving)
  {
    const float dragLimit = 0.01f;
    if (Square(x - _dragX) + Square(y - _dragY) >= Square(dragLimit))
    {
      if (_parent) _parent->OnDrag(this);
      _dragging = true;
      _moving = false;
    }
  }
  else OnMouseHold(x, y, active);
}


void CItemBase::OnLButtonDblClick(float x, float y)
{
  if (IsInside(x, y))
  {
    if (_parent) _parent->OnButtonDblClick(this);
  }
}

void CItemBase::OnLButtonDown(float x, float y)
{
  _dragX = x;
  _dragY = y;
  _moving = true;
  _dragging = false;

  OnEvent(CEButtonDown);
}

void CItemBase::OnLButtonUp(float x, float y)
{
  _moving = false;
  _dragging = false;
}

void CItemBase::OnRButtonClick(float x, float y)
{
  if (GetShortcutIndex() < 0)
  {
    DragItemInfo info = GetItemInfo(x, y);
    if (info._item)
    {
      if (_parent) _parent->CreateContextMenu(x, y, 
      IDC_INVENTORY_MENU, Pars >> "RscDisplayInventory" >> "Menu",
      CCMContextInventory(info._item, NULL));
    }
  }
}

void CItemBase::OnDrawBackGround(UIViewport *vp, float alpha)
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();
  float xx = CX(_x);
  float yy = CY(_y);
  float ww = CW(_w);
  float hh = CH(_h);

  // draw background
  Draw2DParsExt pars;
  pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  pars.SetColor(_bgColor);
  pars.Init();
  vp->Draw2D(pars, Rect2DPixel(xx, yy, ww, hh));

  // draw border
  vp->DrawLine(Line2DPixel(xx, yy, xx + ww, yy), _borderColor, _borderColor);
  vp->DrawLine(Line2DPixel(xx, yy, xx, yy + hh), _borderColor, _borderColor);
  vp->DrawLine(Line2DPixel(xx + ww, yy, xx + ww, yy + hh), _borderColor, _borderColor);
  vp->DrawLine(Line2DPixel(xx, yy + hh, xx + ww, yy + hh), _borderColor, _borderColor);
}

void CItemBase::DrawItem(InventoryItem *item, float x, float y, float w, float h, float z, float alpha, Color color, Matrix3 *rot)
{
  // enforce the basic (on ground) model
  int oldType = item->SetClothingType(-1);

  LODShape *shape = item->GetShape(); 
  if (!shape || shape->NLevels() == 0)
  {
    item->SetClothingType(oldType);
    return;
  }

  Matrix4 transform(NoInit);
  if (!PlaceItemInRectangle(transform, item, x, y, w, h, z))
  {
    item->SetClothingType(oldType);
    return;
  }

  if (rot) // additional transformation
  {
    Matrix3 orient = (*rot) * transform.Orientation();
    transform.SetOrientation(orient);
  }

  int level = 0;

  // Set UI specific flags
  item->InsideUIDraw(true);

  Color c = color;
  if (color.A() < 0) color = Color(1.0f, 1.0f, 1.0f, 1.0f);
  color.SetA(alpha * color.A());

  GEngine->SetInstanceInfo(-1, color, 1.0f);
  item->Draw(-1, level, ClipAll, DrawParameters::_noShadow, InstanceParameters::_default, 0.1f, transform, NULL
#if _HELISIM_RENDER_TO_TEXTURE
    , GScene->GetDrawObjectIndex()
#endif
    );
  GEngine->SetInstanceInfo(-1, HWhite, 1);

  item->InsideUIDraw(false);

  item->DrawProxies(level, 0.1f, transform, [](Matrix4 &matrix, SkeletonIndex boneIndex) -> bool
  {
    return false;
  });

  item->SetClothingType(oldType);
}


bool PlaceItemInRectangle(Matrix4 &result, const InventoryItem *item, float x, float y, float w, float h, float z)
{
  const InventoryItemType *type = item->Type();
  if (!type) return false;

  // center of the model
  Vector3 pos = ControlObject::Convert2DTo3D(Point2DFloat(x + 0.5f * w, y + 0.5f * h), z);

  // orientation of the model
  Matrix3 orient = type->GetInventoryOrientation();

  // SourceDayZ: when the player rotates an item in the inventory (SPACE), the
  // icon itself must rotate 90 degrees to match the new footprint.
  if (item->IsInventoryRotated())
  {
    Matrix3 rotZ(MRotationZ, 0.5f * H_PI);
    orient = rotZ * orient;
  }

  // calculate scale and offset by the object bounding box
  const Vector3 *bb = type->BoundingBox();
  Vector3 posMin(FLT_MAX, FLT_MAX, 0);
  Vector3 posMax(-FLT_MAX, -FLT_MAX, 0);
  float minZ = FLT_MAX;
  // check all vertices of the cube
#define DIAG_ITEM_PLACEMENT 0
#if DIAG_ITEM_PLACEMENT
LogF("CUBE: %s", cc_cast(type->GetName()));
LogF(" aside %.2f, %.2f, %.2f", orient.DirectionAside().X(), orient.DirectionAside().Y(), orient.DirectionAside().Z());
LogF(" up %.2f, %.2f, %.2f", orient.DirectionUp().X(), orient.DirectionUp().Y(), orient.DirectionUp().Z());
LogF(" dir %.2f, %.2f, %.2f", orient.Direction().X(), orient.Direction().Y(), orient.Direction().Z());
#endif
  for (int x=0; x<2; x++)
    for (int y=0; y<2; y++)
      for (int z=0; z<2; z++)
      {
        Vector3 pos = orient * Vector3(bb[x].X(), bb[y].Y(), bb[z].Z());
#if DIAG_ITEM_PLACEMENT
LogF(" - %.2f, %.2f, %.2f -> %.2f, %.2f, %.2f", bb[x].X(), bb[y].Y(), bb[z].Z(), pos.X(), pos.Y(), pos.Z());
#endif
        saturateMin(posMin[0], pos[0]); saturateMax(posMax[0], pos[0]);
        saturateMin(posMin[1], pos[1]); saturateMax(posMax[1], pos[1]);
        saturateMin(minZ, pos[2]);
      }
#if DIAG_ITEM_PLACEMENT
LogF(" min %.2f, %.2f, %.2f", posMin.X(), posMin.Y(), posMin.Z());
LogF(" max %.2f, %.2f, %.2f", posMax.X(), posMax.Y(), posMax.Z());
#endif
  posMax[2] = posMin[2] = minZ; // nearest z plane to fit into space

  // scale
  Point2DFloat ptMin = ControlObject::Convert3DTo2D(pos + posMin);
  Point2DFloat ptMax = ControlObject::Convert3DTo2D(pos + posMax);
#if DIAG_ITEM_PLACEMENT
LogF(" area %.2f x %.2f", w, h);
LogF(" 2D [%.2f, %.2f] [%.2f, %.2f]", ptMin.x, ptMin.y, ptMax.x, ptMax.y);
#endif
  float scale = std::min(abs(w / (ptMax.x - ptMin.x)), abs(h / (ptMax.y - ptMin.y)));
#if DIAG_ITEM_PLACEMENT
LogF(" scale %.2f", scale);
#endif
  orient.SetScale(scale);

  // offset
  pos[0] -= 0.5f * scale * (posMin.X() + posMax.X());
  pos[1] -= 0.5f * scale * (posMin.Y() + posMax.Y());

  result.SetOrientation(orient);
  result.SetPosition(pos);

#if DIAG_ITEM_PLACEMENT
  Vector3 pos1 = result.FastTransform(Vector3(bb[0].X(), bb[0].Y(), bb[0].Z()));
  Vector3 pos2 = result.FastTransform(Vector3(bb[0].X(), bb[1].Y(), bb[0].Z()));
  Vector3 pos3 = result.FastTransform(Vector3(bb[1].X(), bb[1].Y(), bb[0].Z()));
  Vector3 pos4 = result.FastTransform(Vector3(bb[1].X(), bb[0].Y(), bb[0].Z()));
  Vector3 pos5 = result.FastTransform(Vector3(bb[0].X(), bb[0].Y(), bb[1].Z()));
  Vector3 pos6 = result.FastTransform(Vector3(bb[0].X(), bb[1].Y(), bb[1].Z()));
  Vector3 pos7 = result.FastTransform(Vector3(bb[1].X(), bb[1].Y(), bb[1].Z()));
  Vector3 pos8 = result.FastTransform(Vector3(bb[1].X(), bb[0].Y(), bb[1].Z()));
  GEngine->DrawLine3D(-1, pos1, pos2, PackedWhite, 1, 0);
  GEngine->DrawLine3D(-1, pos2, pos3, PackedWhite, 1, 0);
  GEngine->DrawLine3D(-1, pos3, pos4, PackedWhite, 1, 0);
  GEngine->DrawLine3D(-1, pos4, pos1, PackedWhite, 1, 0);
  GEngine->DrawLine3D(-1, pos5, pos6, PackedWhite, 1, 0);
  GEngine->DrawLine3D(-1, pos6, pos7, PackedWhite, 1, 0);
  GEngine->DrawLine3D(-1, pos7, pos8, PackedWhite, 1, 0);
  GEngine->DrawLine3D(-1, pos8, pos5, PackedWhite, 1, 0);
  GEngine->DrawLine3D(-1, pos1, pos5, PackedWhite, 1, 0);
  GEngine->DrawLine3D(-1, pos2, pos6, PackedWhite, 1, 0);
  GEngine->DrawLine3D(-1, pos3, pos7, PackedWhite, 1, 0);
  GEngine->DrawLine3D(-1, pos4, pos8, PackedWhite, 1, 0);

  Vector3 posMi = pos + scale * posMin;
  Vector3 posMa = pos + scale * posMax;
  GEngine->DrawLine3D(-1, posMi, posMa, PackedColor(Color(1,1,0,10)), 1, 0);
#endif

  return true;
}

static int CountItems(const InventoryItemType *type)
{
  AIBrain *agent = GWorld->FocusOn();
  if (!agent || !agent->LSIsAlive()) return 0;

  Person *player = agent->GetPerson();
  if (!player) return 0;

  return player->CountItems(type);
}

bool IsInventoryOpened();

RString GetKeyName(int dikCode);

UITime CItemShortcut::_lastTime = Glob.uiTime;

void ShowShortcuts()
{
  CItemShortcut::ShowShortcuts();
}

void ToggleShortcuts()
{
  CItemShortcut::ToggleShortcuts();
}

CItemShortcut::CItemShortcut(ControlsContainer *parent, int idc, ParamEntryPar cls, bool igui)
  : base(parent, idc, cls), _igui(igui)
{
  _index = cls >> "index";

  _textColor = GetPackedColor(cls >> "colorText");

  _bgColorInHands = GetPackedColor(cls >> "colorBackgroundInHands");

  _bgColorInactive = GetPackedColor(cls >> "colorBackgroundInactive");
  _borderColorInactive = GetPackedColor(cls >> "colorBorderInactive");
  _textColorInactive = GetPackedColor(cls >> "colorTextInactive");

  _font = GEngine->LoadFont(GetFontID(cls>>"font"));
  _size = cls >> "sizeEx";

  // key mapped to action
  int action = UAItemFirst + _index;
  if (action >= UAItemFirst && action <= UAItemLast)
  {
    KeyListEx list = GInput.GetUserKeys(action);
    if (list.Size() > 0)
    {
      int dik = list[0];
      _shortcut = GetKeyName(dik);
    }
  }
}

InventoryItem *CItemShortcut::GetItem()
{
  if (!GWorld) return NULL;
  Person *ui = GWorld->PlayerOn();
  if (!ui) return NULL;

  return ui->GetItemShortcutObject(_index);
}

DragItemInfo CItemShortcut::GetItemInfo(float x, float y)
{
  DragItemInfo info;
  info._item = GetItem();
  info._offsetX = x - _x;
  info._offsetY = y - _y;
  return info;
}

bool CItemShortcut::IsContained(InventoryItem *item)
{
  return true;
}

bool CItemShortcut::CanAddItem(InventoryItem *item, float x, float y)
{
  return true;
}

void CItemShortcut::AddItem(InventoryItem *item, float x, float y)
{
  SetItemShortcut(_index, item);
}

float CItemShortcut::GetGridW()
{
  return _w;
}

float CItemShortcut::GetGridH()
{
  return _h;
}

void CItemShortcut::DrawTooltip(UIViewport *vp, float x, float y)
{
  /* Do not display tooltip for shortcut items
  const InventoryItem *item = GetItemInfo(x, y)._item;
  if (item)
  {
    DisplayInventory *disp = dynamic_cast<DisplayInventory *>(_parent.GetLink());
    if (disp) disp->DrawTooltip(vp, x, y, item);
  }
  */
}

const float CItemShortcut::_startDim = 45.0f;
const float CItemShortcut::_endDim = 50.0f;

void CItemShortcut::ToggleShortcuts()
{
  float age = Glob.uiTime - _lastTime;
  if (age >= _endDim) _lastTime = Glob.uiTime; // show
  else _lastTime = Glob.uiTime - _endDim; // hide;
}

void CItemShortcut::OnDraw(float alpha)
{
  if (_igui)
  {
    if (IsInventoryOpened())
    {
      ShowShortcuts();
      return;
    }

    // fading
    float age = Glob.uiTime - _lastTime;
    
    if (age >= _endDim) return;
    
    if (age > _startDim)
    {
      // interpolate dim
      float factor = (age - _startDim) * (1.0f / (_endDim - _startDim));
      // update alpha
      alpha *= 1.0 - factor;
    }
  }

  InventoryItem *item = GetItem();
  if (item)
  {
    int count = CountItems(item->Type());
    item->EnableAnimations(count > 0);

    static RStringB whiteTextureName("#(argb,1,1,1)color(1,1,1,1)");

    if (count > 0) item->ResetTexture();
    else item->SetTexture(whiteTextureName);

    DisplayInventory *display = dynamic_cast<DisplayInventory *>(_parent.GetLink());
    Color color(0, 0, 0, -1.0f);
    if (display)
    {
      if (display->GetDraggedItem() == item) color = ColorItemDragged;
      else if (display->GetSwappedItem() == item) color = ColorItemSwapped;
    }

    DrawItem(item, _x, _y, _w, _h, _z, alpha, color);
  }
}

void CItemShortcut::OnDrawBackGround(UIViewport *vp, float alpha)
{
  if (_igui)
  {
    if (IsInventoryOpened()) return;

    // fading
    float age = Glob.uiTime - _lastTime;
    
    if (age >= _endDim) return;
    
    if (age > _startDim)
    {
      // interpolate dim
      float factor = (age - _startDim) * (1.0f / (_endDim - _startDim));
      // update alpha
      alpha *= 1.0 - factor;
    }
  }

  if (!GWorld) return;
  if (!GWorld->UI()->IsHUDShown()) return;

  Person *ui = GWorld->PlayerOn();
  if (!ui) return;
  const InventoryItemType *type = ui->GetItemShortcut(_index);
  if (_igui && !type) return;

  bool active = type && CountItems(type) > 0;

  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();
  float xx = CX(_x);
  float yy = CY(_y);
  float ww = CW(_w);
  float hh = CH(_h);

  // draw background
  Draw2DParsExt pars;
  pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  PackedColor color = active ? _bgColor : _bgColorInactive;
  const InventoryItem *inHands = ui->GetItemInHands();
  if (inHands && inHands->GetType() == type) color = _bgColorInHands;
  pars.SetColor(color);
	color.SetA8(toIntFloor(color.A8() * alpha));
  pars.Init();
  vp->Draw2D(pars, Rect2DPixel(xx, yy, ww, hh));

  // draw border
  color = active ? _borderColor : _borderColorInactive;
	color.SetA8(toIntFloor(color.A8() * alpha));
  vp->DrawLine(Line2DPixel(xx, yy, xx + ww, yy), color, color);
  vp->DrawLine(Line2DPixel(xx, yy, xx, yy + hh), color, color);
  vp->DrawLine(Line2DPixel(xx + ww, yy, xx + ww, yy + hh), color, color);
  vp->DrawLine(Line2DPixel(xx, yy + hh, xx + ww, yy + hh), color, color);
}

void CItemShortcut::OnDrawForeGround(UIViewport *vp, float alpha)
{
  if (_igui)
  {
    if (IsInventoryOpened()) return;

    // fading
    float age = Glob.uiTime - _lastTime;
    
    if (age >= _endDim) return;
    
    if (age > _startDim)
    {
      // interpolate dim
      float factor = (age - _startDim) * (1.0f / (_endDim - _startDim));
      // update alpha
      alpha *= 1.0 - factor;
    }
  }

  if (!GWorld) return;
  if (!GWorld->UI()->IsHUDShown()) return;

  Person *ui = GWorld->PlayerOn();
  if (!ui) return;
  const InventoryItemType *type = ui->GetItemShortcut(_index);
  if (!type) return;

  // Texts
  int count = CountItems(type);
 
  const float offsetX = 0.09 * _size;
  const float offsetY = 0.06 * _size;
  PackedColor color = count > 0 ? _textColor : _textColorInactive;
	color.SetA8(toIntFloor(color.A8() * alpha));
  TextDrawAttr attr(_size, _font, color, true);
  Rect2DFloat clipRect(_x, _y, _w, _h);

  float width = _w - offsetX;
  float top = _y + _h - _size - offsetY;
  if (count > 0)
  {
    vp->DrawText(Point2DFloat(_x + offsetX, _y + offsetY), clipRect, attr, cc_cast(_shortcut));
    BString<16> buffer;
    sprintf(buffer, "%d", count);
    float tw = vp->GetTextWidth(attr, cc_cast(buffer));
    vp->DrawText(Point2DFloat(_x + _w - tw - offsetX, top), clipRect, attr, cc_cast(buffer));
    width -= tw + 4 * offsetY;
  }
  clipRect.w = width;
  vp->DrawText(Point2DFloat(_x + offsetX, top), clipRect, attr, cc_cast(type->GetShortName()));
}

static int AttachmentsCount(InventoryItem *container)
{
  int n = container->NAttachments();
  int count = 0;
  for (int i=0; i<n; i++)
  {
    if (container->GetAttachment(i)) count++;
  }
  return count;
}

// subcontrol handling a singe inventory item
class ContainerUI
{
private:
  float _titleW, _titleH;
  float _attachmentW, _attachmentH;
  float _gridW, _gridH;

  float _sbWidth; // scrollbar width

  Ref<Font> _font;
  float _size;
  PackedColor _textColor;
  // header background color
  PackedColor _bgHeaderColor;
  // items background color
  PackedColor _bgItemColor;
  // items background color
  PackedColor _bgNoItemColor;
  // items background color
  PackedColor _bgItemInHandsColor;

  static const float _textBorder;
  static const bool _drawEmptyItems;

  // icons
  Ref<Texture> _expanded, _collapsed, _inHands;

public:
  ContainerUI(ParamEntryPar cls);

  float GetHeight(CItemBase *ctrl, InventoryItem *container, InventoryStorage *cargo, bool expanded);
  float GetSBWidth() const {return _sbWidth;}
  float GetGridW() const {return _gridW;}
  float GetGridH() const {return _gridH;}
  float GetTextSize() const {return _size;}
  Font *GetFont() const {return _font;}
  PackedColor GetTextColor() const {return _textColor;}
  float GetTextBorder() const {return _textBorder;}
  PackedColor GetHeaderBgColor() const {return _bgHeaderColor;}
  PackedColor GetItemBgColor() const {return _bgItemColor;}
  PackedColor GetNoItemBgColor() const {return _bgNoItemColor;}

  bool DrawObjects(ControlsContainer *parent, CItemBase *ctrl, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded, float alpha);
  bool DrawForeground(CItemBase *ctrl, UIViewport *vp, InventoryItem *container, InventoryStorage *cargo, Person *person, float top, bool expanded, float alpha);
  bool DrawBackground(CItemBase *ctrl, UIViewport *vp, InventoryItem *container, InventoryStorage *cargo, Person *person, float top, bool expanded, float alpha);
  
  void DrawSimpleForeground(CItemBase *ctrl, UIViewport *vp, InventoryItem *item, Person *person, float left, float top, float alpha);
  void DrawSimpleBackground(CItemBase *ctrl, UIViewport *vp, InventoryItem *item, Person *person, float left, float top, float alpha);

  bool FindHeader(CItemBase *ctrl, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded, float x, float y, int index, int &found);
  bool FindSlot(CItemBase *ctrl, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded, float x, float y, InventoryItem *&result, int &resultIndex);
  bool GetItemInfo(CItemBase *ctrl, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded, float x, float y, DragItemInfo &info);
};

const float ContainerUI::_textBorder = 0.002f;

ContainerUI::ContainerUI(ParamEntryPar cls)
{
  _titleW = cls >> "titleW";
  _titleH = cls >> "titleH";
  _attachmentW = cls >> "attachmentW";
  _attachmentH = cls >> "attachmentH";
  _gridW = cls >> "gridW";
  _gridH = cls >> "gridH";

  _sbWidth = 0.018; // TODO: move to config

  _font = GEngine->LoadFont(GetFontID(cls>>"font"));
  _size = cls >> "sizeEx";
  _textColor = GetPackedColor(cls >> "colorText");
  _bgHeaderColor = GetPackedColor(cls >> "colorHeaderBackground");
  _bgNoItemColor = GetPackedColor(cls >> "colorNoItemBackground");
  _bgItemColor = GetPackedColor(cls >> "colorItemBackground");
  _bgItemInHandsColor = GetPackedColor(cls >> "colorItemInHandsBackground");

  RString name = FindPicture(cls >> "expanded");
  name.Lower();
  _expanded = GlobLoadTexture(name);
  if (_expanded) _expanded->SetUsageType(Texture::TexUI); // no limits
  
  name = FindPicture(cls >> "collapsed");
  name.Lower();
  _collapsed = GlobLoadTexture(name);
  if (_collapsed) _collapsed->SetUsageType(Texture::TexUI); // no limits

  name = FindPicture(cls >> "inHands");
  name.Lower();
  _inHands = GlobLoadTexture(name);
  if (_inHands) _inHands->SetUsageType(Texture::TexUI); // no limits
}

float ContainerUI::GetHeight(CItemBase *ctrl, InventoryItem *container, InventoryStorage *cargo, bool expanded)
{
  float h = _titleH; // header
  if (expanded)
  {
    int n = AttachmentsCount(container);
    if (n > 0)
    {
      float width = ctrl->_w - _sbWidth;
      int columns = toIntFloor(width / _attachmentW);
      if (columns < 1) columns = 1;
      int rows = (n + columns - 1) / columns;
      h += rows * _attachmentH;
    }
    if (cargo) h += cargo->GetHeight() * _gridH; // content
  }
  return h;
}

bool ContainerUI::DrawObjects(ControlsContainer *parent, CItemBase *ctrl, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded, float alpha)
{
  float bottom = ctrl->_y + ctrl->_h;

  if (top +  /* better not to draw part of object */_titleH > bottom) return true; // replace by clipping

  DisplayInventory *display = dynamic_cast<DisplayInventory *>(parent);
  Color color(0, 0, 0, -1.0f);
  if (display)
  {
    if (display->GetDraggedItem() == container) color = ColorItemDragged;
    else if (display->GetSwappedItem() == container) color = ColorItemSwapped;
  }

  // container icon in the header
  ctrl->DrawItem(container, ctrl->_x, top, _titleW, _titleH, ctrl->_z, alpha, color);
  top += _titleH;

  if (expanded)
  {
    int n = container->NAttachments();
    if (n > 0)
    {
      float width = ctrl->_w - _sbWidth;
      int columns = toIntFloor(width / _attachmentW);
      if (columns < 1) columns = 1;
      int c = 0;
      for (int i=0; i<n; i++)
      {
        const InventoryItem *item = container->GetAttachment(i);
        if (item && /* better not to draw part of object */ top + _attachmentH <= bottom)
        {
          Color color(0, 0, 0, -1.0f);
          if (display)
          {
            if (display->GetDraggedItem() == item) color = ColorItemDragged;
            else if (display->GetSwappedItem() == item) color = ColorItemSwapped;
          }
          ctrl->DrawItem(unconst_cast(item), ctrl->_x + c * _attachmentW, top, _attachmentW, _attachmentH, ctrl->_z, alpha, color);
          c++;
          if (c >= columns)
          {
            c = 0; top += _attachmentH;
          }
        }
      }
      if (c > 0) top += _attachmentH;
    }
    if (cargo)
    {
      const RefArray<InventoryItem> &items = cargo->GetItems();

      // grid
      int index = 0;
      for (int r=0; r<cargo->GetHeight(); r++)
      {
        if (top >= bottom) return true; // replace by clipping

        for (int c=0; c<cargo->GetWidth(); c++)
        {
          if (index < items.Size())
          {
            const InventoryItem *item = items[index];
            if (item)
            {
              const InventoryItemType *type = item->Type();
              if (type)
              {
                int w = item->GetInventoryWidth();
                int h = item->GetInventoryHeight();
                if (top + h * _gridH <= bottom) /* better not to draw part of object */
                {
                  Color color(0, 0, 0, -1.0f);
                  if (display)
                  {
                    if (display->GetDraggedItem() == item) color = ColorItemDragged;
                    else if (display->GetSwappedItem() == item) color = ColorItemSwapped;
                  }

                  ctrl->DrawItem(unconst_cast(item), ctrl->_x + c * _gridW, top, w * _gridW, h * _gridH, ctrl->_z, alpha, color);
                }
              }
            }
          }
          index++;
        }
        top += _gridH;
      }
    }
  }
  return false;
}

static RString GetSpaceText(const InventoryStorage &cargo)
{
  int count = 0;
  const RefArray<InventoryItem> &items = cargo.GetItems();
  for (int i=0; i<items.Size(); i++)
  {
    const InventoryItem *item = items[i];
    if (item)
    {
      const InventoryItemType *type = item->Type();
      if (type) count += item->GetInventoryWidth() * item->GetInventoryHeight();
    }
  }
  return Format("%d/%d", count, cargo.GetWidth() * cargo.GetHeight());
}

// helper class for scoped existence of the clipped viewport
class ClippedViewport
{
private:
  UIViewport *_parent;
  UIViewport *_clipped;

public:
  ClippedViewport(UIViewport *parent, Rect2DFloat &clipRect) : _parent(parent)
  {
    _clipped = parent->CreateChild(Point2DFloat(0, 0), clipRect);
  }
  ~ClippedViewport()
  {
    _parent->DestroyChild(_clipped);
  }

  operator UIViewport *()
  {
    return _clipped;
  }
};

void ContainerUI::DrawSimpleForeground(CItemBase *ctrl, UIViewport *vp, InventoryItem *item, Person *person, float left, float top, float alpha)
{
  Rect2DFloat clipRectF(ctrl->_x, ctrl->_y, ctrl->_w - _sbWidth, ctrl->_h);
  ClippedViewport vpClipped(vp, clipRectF);

  // quantity
  RefR<INode> text = item->GetQuantity();
  if (text)
  {
    SetTextAttribute(text, "shadow", "1");
    float h = GetTextHeight(text, Rect2DFloat(0, 0, _titleW, _titleH), _size);
    float w = GetTextWidthFloat(text, _size);
    DrawText(vpClipped, text, Rect2DFloat(left + _titleW - w, top + _titleH - h, _titleW, _titleH), _size);
  }
}

void ContainerUI::DrawSimpleBackground(CItemBase *ctrl, UIViewport *vp, InventoryItem *item, Person *person, float left, float top, float alpha)
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  float xx = CX(left), yy = CY(top), ww = CW(_titleW), hh = CH(_titleH);
  Rect2DPixel clipRect(CX(ctrl->_x), CY(ctrl->_y), CW(ctrl->_w - _sbWidth), CH(ctrl->_h));

  // background
  Draw2DParsExt pars;
  pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  /*
  if (item)
  {
	float heat = item->GetTemperature();
	pars.SetColor(TempToColor(heat,Pars >> "RscDisplayInventory" >> "Objects" >> "Content" >> "colorHeaderBackground"));
  }
  {
	pars.SetColor(PackedWhite);
  }
  */
  float heat = item->GetTemperature();
  pars.SetColor(TempToColor(heat,Pars >> "RscDisplayInventory" >> "Objects" >> "Content" >> "colorHeaderBackground"));
  pars.Init();
  vp->Draw2D(pars, Rect2DPixel(xx, yy, ww, hh), clipRect);
  if (item && person && item == person->GetItemInHands())
  {
    Draw2DParsExt hand;
    hand.mip = GEngine->TextBank()->UseMipmap(_inHands, 0, 0);
    hand.SetColor(PackedWhite);
    hand.Init();

    vp->Draw2D(hand, Rect2DPixel(xx + ww - hh, yy, hh, hh), clipRect);
  }
  // border
  vp->DrawLine(Line2DPixel(xx, yy, xx + ww, yy), ctrl->_borderColor, ctrl->_borderColor, clipRect);
  vp->DrawLine(Line2DPixel(xx, yy, xx, yy + hh), ctrl->_borderColor, ctrl->_borderColor, clipRect);
  vp->DrawLine(Line2DPixel(xx + ww, yy, xx + ww, yy + hh), ctrl->_borderColor, ctrl->_borderColor, clipRect);
  vp->DrawLine(Line2DPixel(xx, yy + hh, xx + ww, yy + hh), ctrl->_borderColor, ctrl->_borderColor, clipRect);
}

bool ContainerUI::DrawForeground(CItemBase *ctrl, UIViewport *vp, InventoryItem *container, InventoryStorage *cargo, Person *person, float top, bool expanded, float alpha)
{
  float width = ctrl->_w - _sbWidth;

  Rect2DFloat clipRectF(ctrl->_x, ctrl->_y, width, ctrl->_h);
  ClippedViewport vpClipped(vp, clipRectF);

  // skip the header
  top += _titleH;

  if (expanded)
  {
    int n = container->NAttachments();
    if (n > 0)
    {
      int columns = toIntFloor(width / _attachmentW);
      if (columns < 1) columns = 1;

      float x = ctrl->_x;

      int c = 0;
      for (int i=0; i<n; i++)
      {
        const InventoryItem *item = container->GetAttachment(i);
        if (item)
        {
          // quantity
          RefR<INode> text = unconst_cast(item)->GetQuantity();
          if (text)
          {
            SetTextAttribute(text, "shadow", "1");
            float h = GetTextHeight(text, Rect2DFloat(0, 0, _attachmentW, _attachmentH), _size);
            float w = GetTextWidthFloat(text, _size);
            DrawText(vpClipped, text, Rect2DFloat(x + _attachmentW - w, top + _attachmentH - h, _attachmentW, _attachmentH), _size);
          }
          x += _attachmentW;

          c++;
          if (c >= columns)
          {
            c = 0; top += _attachmentH;
            x = ctrl->_x;
          }
        }
      }
      if (c > 0)
      {
        top += _attachmentH;
      }
    }
    if (cargo)
    {
      float y = top;
      int index = 0;
      for (int i=0; i<cargo->GetHeight(); i++)
      {
        float x = ctrl->_x;
        for (int j=0; j<cargo->GetWidth(); j++)
        {
          InventoryItem *item = index < cargo->GetItems().Size() ? cargo->GetItems()[index] : nullptr;
          const InventoryItemType *type = item ? item->Type() : nullptr;
          if (type)
          {
            // quantity
            RefR<INode> text = unconst_cast(item)->GetQuantity();
            if (text)
            {
              SetTextAttribute(text, "shadow", "1");
              Rect2DFloat rect(0, 0, item->GetInventoryWidth() * _gridW, item->GetInventoryHeight() * _gridH);
              float h = GetTextHeight(text, rect, _size);
              float w = GetTextWidthFloat(text, _size);
              rect.x = x + rect.w - w; rect.y = y + rect.h - h;
              DrawText(vpClipped, text, rect, _size);
            }
          }
          index++;
          x += _gridW;
        }
        y += _gridH;
      }
    }
  }
  return false;
}

bool ContainerUI::DrawBackground(CItemBase *ctrl, UIViewport *vp, InventoryItem *container, InventoryStorage *cargo, Person *person, float top, bool expanded, float alpha)
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();
  float xx = CX(ctrl->_x);
  float width = ctrl->_w - _sbWidth;
  float hdx = xx, hdw = CW(width), hdh = CH(_titleH);

  TextDrawAttr attr(_size, _font, _textColor, true);
  Rect2DFloat clipRectF(ctrl->_x, ctrl->_y, width, ctrl->_h);
  Rect2DPixel clipRect(hdx, CY(ctrl->_y), hdw, CH(ctrl->_h));

  Draw2DParsExt hand;
  hand.mip = GEngine->TextBank()->UseMipmap(_inHands, 0, 0);
  hand.SetColor(PackedWhite);
  hand.Init();

  // header
  float hdy = CY(top);
  // background
  Draw2DParsExt pars;
  pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  //pars.SetColor(_bgHeaderColor);
  float heat = container->GetTemperature();
  pars.SetColor(TempToColor(heat,Pars >> "RscDisplayInventory" >> "Objects" >> "Content" >> "colorHeaderBackground"));

  pars.Init();
  vp->Draw2D(pars, Rect2DPixel(hdx, hdy, hdw, hdh), clipRect);
  if (container && person && container == person->GetItemInHands())
  {
    pars.SetColor(_bgItemInHandsColor);
    vp->Draw2D(pars, Rect2DPixel(hdx, hdy, CW(_titleW), hdh), clipRect);
    vp->Draw2D(hand, Rect2DPixel(CX(ctrl->_x + _titleW) - hdh, hdy, hdh, hdh), clipRect);
  }
  // border
  vp->DrawLine(Line2DPixel(hdx, hdy, hdx + hdw, hdy), ctrl->_borderColor, ctrl->_borderColor, clipRect);
  vp->DrawLine(Line2DPixel(hdx, hdy, hdx, hdy + hdh), ctrl->_borderColor, ctrl->_borderColor, clipRect);
  vp->DrawLine(Line2DPixel(hdx + hdw, hdy, hdx + hdw, hdy + hdh), ctrl->_borderColor, ctrl->_borderColor, clipRect);
  vp->DrawLine(Line2DPixel(hdx, hdy + hdh, hdx + hdw, hdy + hdh), ctrl->_borderColor, ctrl->_borderColor, clipRect);
  // name
  float tt = top + 0.5f * (_titleH - _size);
  vp->DrawText(Point2DFloat(ctrl->_x + _titleW + _textBorder, tt), clipRectF, attr, container->GetDisplayName());
  if (cargo || container->NAttachments() > 0)
  {
    // collapse / expand icon
    float right = ctrl->_x + width - _textBorder;
    Texture *icon = expanded ? _expanded : _collapsed;
    if (icon)
    {
      MipInfo mip = GEngine->TextBank()->UseMipmap(icon, 0, 0);
      GEngine->Draw2D(mip, PackedWhite,
        Rect2DPixel(CX(right) - CW(0.75f * _size), CY(top + _titleH - _size), CW(0.75f * _size), CH(_size)), clipRect);
    }
    if (cargo)
    {
      // space
      RString space = GetSpaceText(*cargo);
      if (!space.IsEmpty())
      {
        float tw = vp->GetTextWidth(attr, space);
        vp->DrawText(Point2DFloat(right - tw, top), clipRectF, attr, space);
      }
    }
  }
  top += _titleH;

  if (expanded)
  {
    int n = container->NAttachments();
    if (n > 0)
    {
      int columns = toIntFloor(width / _attachmentW);
      if (columns < 1) columns = 1;

      float top2 = top; // backup for lines

      float ww = CW(_attachmentW);
      hdh = CH(_attachmentH);

      float y1 = CY(top);
      float y2 = CY(top + _attachmentH);
      float x = ctrl->_x;

      int c = 0, count = 0;
      for (int i=0; i<n; i++)
      {
        float cx = CX(x);
        const InventoryItem *item = container->GetAttachment(i);
        if (!item) continue;
        count++;

        bool inHands = false;
        if (person && item == person->GetItemInHands())
        {
          pars.SetColor(_bgItemInHandsColor);
          inHands = true;
        }
        else
        {
          const float heat = container->GetAttachment(i)->GetTemperature();
          pars.SetColor(TempToColor(heat,Pars >> "RscDisplayInventory" >> "Objects" >> "Content" >> "colorItemBackground"));
          //pars.SetColor(_bgItemColor);
        }
        vp->Draw2D(pars, Rect2DPixel(cx, y1, ww, y2 - y1), clipRect);
        vp->DrawLine(Line2DPixel(cx, y1, cx, y2), ctrl->_borderColor, ctrl->_borderColor, clipRect);
        if (inHands) vp->Draw2D(hand, Rect2DPixel(CX(x + _attachmentW) - hdh, y1, hdh, hdh), clipRect);

        x += _attachmentW;

        c++;
        if (c >= columns)
        {
          c = 0; top += _attachmentH;
          y1 = CY(top);
          y2 = CY(top + _attachmentH);
          x = ctrl->_x;
        }
      }
      if (c > 0)
      {
        top += _attachmentH;
      }
      for (int i=0; i<count; i+=columns)
      {
        int width = intMin(count - i, columns);
        if (i == 0) 
        {
          float cy = CY(top2);
          vp->DrawLine(Line2DPixel(xx, cy, xx + CW(width * _attachmentW), cy), ctrl->_borderColor, ctrl->_borderColor, clipRect);
        }
        top2 += _attachmentH;
        float cy = CY(top2);
        vp->DrawLine(Line2DPixel(xx, cy, xx + CW(width * _attachmentW), cy), ctrl->_borderColor, ctrl->_borderColor, clipRect);
      }
    }
    if (cargo)
    {
      float ww = CW(_gridW);
      float hh = CH(_gridH);

      int n = cargo->GetHeight() * cargo->GetWidth();
      Buffer<bool> handled(n);

      // grid background
      for (int i=0; i<n; i++) handled[i] = false;
      float y = top;
      int index = 0;
      for (int i=0; i<cargo->GetHeight(); i++)
      {
        float cy = CY(y);
        float x = ctrl->_x;
        for (int j=0; j<cargo->GetWidth(); j++)
        {
          if (!handled[index])
          {
            float cx = CX(x);
            InventoryItem *item = index < cargo->GetItems().Size() ? cargo->GetItems()[index] : nullptr;
            const InventoryItemType *type = item ? item->Type() : nullptr;
            if (type)
            {
              const bool inHands = person && item == person->GetItemInHands();
              if (inHands)
              {
                pars.SetColor(_bgItemInHandsColor);
              }
              else
              {
                const float heat = item->GetTemperature();
                pars.SetColor(TempToColor(heat,Pars >> "RscDisplayInventory" >> "Objects" >> "Content" >> "colorItemBackground"));
              }
              vp->Draw2D(pars, Rect2DPixel(cx, cy, CW(item->GetInventoryWidth() * _gridW), CH(item->GetInventoryHeight() * _gridH)), clipRect);
              for (int k=i; k<i+item->GetInventoryHeight(); k++)
                for (int l=j; l<j+item->GetInventoryWidth(); l++)
                {
                  int ii = k * cargo->GetWidth() + l;
                  handled[ii] = true;
                }
              if (inHands) vp->Draw2D(hand, Rect2DPixel(CX(x + item->GetInventoryWidth() * _gridW) - hh, CY(y + item->GetInventoryHeight() * _gridH) - hh, hh, hh), clipRect);
            }
            else
            {
              pars.SetColor(_bgNoItemColor);
              vp->Draw2D(pars, Rect2DPixel(cx, cy, ww, hh), clipRect);
            }
          }
          index++;
          x += _gridW;
        }
        y += _gridH;
      }

      // grid lines
      for (int i=0; i<n; i++) handled[i] = false;
      y = top;
      index = 0;
      for (int i=0; i<cargo->GetHeight(); i++)
      {
        float cy = CY(y);
        float x = ctrl->_x;
        for (int j=0; j<cargo->GetWidth(); j++)
        {
          if (!handled[index])
          {
            float cx = CX(x);
            InventoryItem *item = index < cargo->GetItems().Size() ? cargo->GetItems()[index] : nullptr;
            const InventoryItemType *type = item ? item->Type() : nullptr;
            if (type)
            {
              float ww = CW(item->GetInventoryWidth() * _gridW);
              float hh = CH(item->GetInventoryHeight() * _gridH);
              vp->DrawLine(Line2DPixel(cx, cy, cx + ww, cy), ctrl->_borderColor, ctrl->_borderColor, clipRect);
              vp->DrawLine(Line2DPixel(cx + ww, cy, cx + ww, cy + hh), ctrl->_borderColor, ctrl->_borderColor, clipRect);
              vp->DrawLine(Line2DPixel(cx + ww, cy + hh, cx, cy + hh), ctrl->_borderColor, ctrl->_borderColor, clipRect);
              vp->DrawLine(Line2DPixel(cx, cy + hh, cx, cy), ctrl->_borderColor, ctrl->_borderColor, clipRect);
            
              for (int k=i; k<i+item->GetInventoryHeight(); k++)
                for (int l=j; l<j+item->GetInventoryWidth(); l++)
                {
                  int ii = k * cargo->GetWidth() + l;
                  handled[ii] = true;
                }
            }
            else
            {
              vp->DrawLine(Line2DPixel(cx, cy, cx + ww, cy), ctrl->_borderColor, ctrl->_borderColor, clipRect);
              vp->DrawLine(Line2DPixel(cx + ww, cy, cx + ww, cy + hh), ctrl->_borderColor, ctrl->_borderColor, clipRect);
              vp->DrawLine(Line2DPixel(cx + ww, cy + hh, cx, cy + hh), ctrl->_borderColor, ctrl->_borderColor, clipRect);
              vp->DrawLine(Line2DPixel(cx, cy + hh, cx, cy), ctrl->_borderColor, ctrl->_borderColor, clipRect);
            }
          }
          index++;
          x += _gridW;
        }
        y += _gridH;
      }
    }
  }
  return false;
}

bool ContainerUI::FindHeader(CItemBase *ctrl, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded, float x, float y, int index, int &found)
{
  if (y < top) return true; // finish, not found

  // check the header
  top += _titleH;
  if (y < top)
  {
    // ignore the scrollbar
    float width = ctrl->_w - _sbWidth;
    // if (x > ctrl->_x + width - _size - _textBorder) found = index;
    if (x < ctrl->_x + width) found = index;
    return true; // finish, found
  }

  return false; // continue
}

bool ContainerUI::FindSlot(CItemBase *ctrl, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded, float x, float y, InventoryItem *&result, int &resultIndex)
{
  if (y < top) return true;

  // check the header
  top += _titleH;
  if (y < top)
  {
    result = container;
    resultIndex = -1;
    return true;
  }

  if (expanded)
  {
    int n = AttachmentsCount(container);
    if (n > 0)
    {
      float width = ctrl->_w - _sbWidth;
      int columns = toIntFloor(width / _attachmentW);
      if (columns < 1) columns = 1;
      int rows = (n + columns - 1) / columns;
      if (y < top + rows * _attachmentH)
      {
        result = container;
        resultIndex = -1;
        int row = toIntFloor((y - top) / _attachmentH);
        int col = toIntFloor((x - ctrl->_x) / _attachmentW);
        if (row >= 0 && row < rows && col >= 0 && col < columns)
        {
          int index = row * columns + col;
          if (index < n)
          {
            for (int i=0; i<container->NAttachments(); i++)
            {
              if (!container->GetAttachment(i)) continue;
              if (index-- <= 0)
              {
                resultIndex = i;
                break;
              }
            }
          }
        }
        return true;
      }
      top += rows * _attachmentH;
    }
    if (cargo)
    {
      // grid
      if (y <= top + cargo->GetHeight() * _gridH)
      {
        int row = toIntFloor((y - top) / _gridH);
        int col = toIntFloor((x - ctrl->_x) / _gridW);
        if (row >= 0 && row < cargo->GetHeight() && col >= 0 && col < cargo->GetWidth())
        {
          result = container;
          resultIndex = row * cargo->GetWidth() + col + n;
        }
        return true;
      }
    }
  }

  return false;
}

bool ContainerUI::GetItemInfo(CItemBase *ctrl, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded, float x, float y, DragItemInfo &info)
{
  if (y < top) return true;
  float width = ctrl->_w - _sbWidth;

  // the container
  top += _titleH;
  if (y < top)
  {
    if (x <= ctrl->_x + width)
    {
      info._item = container;
      info._offsetX = x - ctrl->_x;
      info._offsetY = y - (top - _gridH);
    }
    return true;
  }

  if (expanded)
  {
    int n = AttachmentsCount(container);
    if (n > 0)
    {
      int columns = toIntFloor(width / _attachmentW);
      if (columns < 1) columns = 1;
      int rows = (n + columns - 1) / columns;
      if (y < top + rows * _attachmentH)
      {
        int row = toIntFloor((y - top) / _attachmentH);
        int col = toIntFloor((x - ctrl->_x) / _attachmentW);
        if (row >= 0 && row < rows && col >= 0 && col < columns)
        {
          int index = row * columns + col;
          if (index < n)
          {
            for (int i=0; i<container->NAttachments(); i++)
            {
              if (!container->GetAttachment(i)) continue;
              if (index-- <= 0)
              {
                info._item = container->GetAttachment(i);
                info._offsetX = x - (ctrl->_x + col * _attachmentW);
                info._offsetY = y - (top + row * _attachmentH);
                break;
              }
            }
          }
        }
        return true;
      }
      top += rows * _attachmentH;
    }
    if (cargo)
    {
      // grid
      if (y < top + cargo->GetHeight() * _gridH)
      {
        // this container
        int row = toIntFloor((y - top) / _gridH);
        int col = toIntFloor((x - ctrl->_x) / _gridW);
        int startIndex = cargo->ItemOn(row, col);
        if (startIndex >= 0)
        {
          info._item = cargo->GetItems()[startIndex];
          int sy = startIndex / cargo->GetWidth();
          int sx = startIndex - sy * cargo->GetWidth();
          info._offsetX = x - (ctrl->_x + sx * _gridW);
          info._offsetY = y - (top + sy * _gridH);
        }
        return true;
      }
    }
  }

  return false;
}

class CItemContainer : public CItemBase
{
protected:
  typedef CItemBase base;

  float _titleW, _titleH;

  ContainerUI _ui;

  OLink(Person) _person;
  OLink(EntityAI) _target;
  OLinkArray(InventoryItem) _cache;

  // SourceDayZ: vehicle cargo header (name / slots / collapse button)
  bool _targetExpanded;
  float _targetFullH;

  //! scroll bar used by control
  CScrollBar _scrollbar;

public:
  CItemContainer(ControlsContainer *parent, int idc, ParamEntryPar cls, Person *person, EntityAI *target);


  virtual void OnDraw(float alpha) override;
  virtual void OnDrawForeGround(UIViewport *vp, float alpha) override;
  virtual void OnDrawBackGround(UIViewport *vp, float alpha) override;

  virtual void OnMouseHold(float x, float y, bool active) override;
  virtual void OnLButtonDown(float x, float y) override;
  virtual void OnLButtonUp(float x, float y) override;
  virtual void OnLButtonDblClick(float x, float y) override;
  virtual void OnMouseZChanged(float dz) override;

  // CItemBase implementation
  virtual DragItemInfo GetItemInfo(float x, float y) override;
  virtual bool CanAddItem(InventoryItem *item, float x, float y) override;
  virtual void AddItem(InventoryItem *item, float x, float y) override;
  virtual float GetGridW() override;
  virtual float GetGridH() override;
  virtual bool IsContained(InventoryItem *item) override;

private:
  // update the content of the cache
  void Update();
  // cycle through all subcontrols
  bool ForEachSlot(
    const std::function<bool(int index, InventoryItem *item, float left, float top)> &funcSimple,
    const std::function<bool(int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded)> &funcContainer
    );

  // SourceDayZ: grid view for big cargo storages (vehicle trunk with 500 slots)
  InventoryStorage *GetTargetCargo();
  float TargetCellSize(const InventoryStorage *cargo) const;
  float TargetGridTop() const {return _y + _titleH;}
  float TargetGridHeight() const {return _h > _titleH ? _h - _titleH : 0.0f;}
  int TargetRowsVisible(const InventoryStorage *cargo) const;
  int TargetFirstRow(const InventoryStorage *cargo) const;
  int TargetCargoIndex(const InventoryStorage *cargo, float x, float y) const;
  Rect2DFloat TargetToggleRect() const;
  void SetTargetExpanded(bool expanded);
  void DrawTargetCargoHeader(UIViewport *vp, InventoryStorage *cargo, float alpha);
  void DrawTargetCargoGrid(UIViewport *vp, InventoryStorage *cargo, float alpha);
  void DrawTargetCargoItems(InventoryStorage *cargo, float alpha);
  void DrawTargetCargoForeground(UIViewport *vp, InventoryStorage *cargo, float alpha);
  DragItemInfo GetTargetCargoInfo(const InventoryStorage *cargo, float x, float y);
};

CItemContainer::CItemContainer(ControlsContainer *parent, int idc, ParamEntryPar cls, Person *person, EntityAI *target) : base(parent, idc, cls)
  , _ui(cls), _scrollbar(false, false, cls >> "ScrollBar")
{
  _person = person;
  _target = target;

  _titleW = cls >> "titleW";
  _titleH = cls >> "titleH";

  // SourceDayZ: vehicle cargo starts expanded and remembers its full height
  _targetExpanded = true;
  _targetFullH = _h;

  _scrollbar.SetPosition
  (
    _x + _w - _ui.GetSBWidth(), _y,
    _ui.GetSBWidth(), _h
  );
  _scrollbar.SetColor(_borderColor);
  _scrollbar.SetRangeAndPos(0, 0, 1, 0); 
  _scrollbar.SetSpeed(1, 1); 
}

bool CItemContainer::ForEachSlot(
  const std::function<bool(int index, InventoryItem *item, float left, float top)> &funcSimple,
  const std::function<bool(int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded)> &funcContainer
  )
{
  int first = _scrollbar.GetPos();
  float top = _y;
  float vSpace = 0.25f * _titleH;

  // simple items first
  int columns = toIntFloor(_w / _titleW);
  int col = 0;
  float left = _x;
  for (int j=0; j<_cache.Size(); j++)
  {
    InventoryItem *item = _cache[j];
    if (!item) continue;

    ResourceSupply *supply = item->GetSupply();
    InventoryStorage *cargo = supply ? &supply->GetItemsCargo() : NULL;
    if (cargo && (cargo->GetHeight() <= 0 || cargo->GetWidth() <= 0)) cargo = NULL;

    if (!cargo && item->NAttachments() == 0)
    {
      // simple item
      if (first <= 0)
      {
        // visible, invoke the functor
        if (funcSimple(j, item, left, top)) return true;
      }
      col++; left += _titleW;
      if (col >= columns)
      {
        // the next row
        if (first-- <= 0) top += _titleH + vSpace;
        col = 0; left = _x;
      }
    }
  }
  if (col > 0)
  {
    // the next row
    if (first-- <= 0) top += _titleH + vSpace;
  }
  // containers then
  for (int j=0; j<_cache.Size(); j++)
  {
    InventoryItem *container = _cache[j];
    if (!container) continue;

    ResourceSupply *supply = container->GetSupply();
    InventoryStorage *cargo = supply ? &supply->GetItemsCargo() : NULL;
    if (cargo && (cargo->GetHeight() <= 0 || cargo->GetWidth() <= 0)) cargo = NULL;

    if (!cargo && container->NAttachments() == 0) continue;

    if (first-- > 0) continue; // skip scrolled items

    // invoke the functor
    if (funcContainer(j, container, cargo, top, container->IsOpened())) return true;

    // forward top
    top += _ui.GetHeight(this, container, cargo, container->IsOpened());
    top += vSpace; // space
  }
  return false;
}

void CItemContainer::OnDraw(float alpha)
{
  // if needed, Update can be called less often
  Update();

  InventoryStorage *targetCargo = GetTargetCargo();
  if (targetCargo)
  {
    if (_targetExpanded) DrawTargetCargoItems(targetCargo, alpha);
    return;
  }

  DisplayInventory *display = dynamic_cast<DisplayInventory *>(_parent.GetLink());

  ForEachSlot(
    [this, display, alpha](int index, InventoryItem *item, float left, float top) -> bool
    {
      Color color(0, 0, 0, -1.0f);
      if (display)
      {
        if (display->GetDraggedItem() == item) color = ColorItemDragged;
        else if (display->GetSwappedItem() == item) color = ColorItemSwapped;
      }

      DrawItem(item, left, top, _titleW, _titleH, _z, alpha, color);
      return false;
    },
    [this, alpha](int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded) -> bool
    {
      return _ui.DrawObjects(_parent, this, container, cargo, top, expanded, alpha);
    });
}

void CItemContainer::OnDrawForeGround(UIViewport *vp, float alpha)
{
  InventoryStorage *targetCargo = GetTargetCargo();
  if (targetCargo)
  {
    if (_targetExpanded) DrawTargetCargoForeground(vp, targetCargo, alpha);
    return;
  }

  ForEachSlot(
    [this, vp, alpha](int index, InventoryItem *item, float left, float top) -> bool
    {
      _ui.DrawSimpleForeground(this, vp, item, nullptr, left, top, alpha);
      return false;
    },
    [this, vp, alpha](int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded) -> bool
    {
      return _ui.DrawForeground(this, vp, container, cargo, nullptr, top, expanded, alpha);
    });
}

void CItemContainer::OnDrawBackGround(UIViewport *vp, float alpha)
{
  InventoryStorage *targetCargo = GetTargetCargo();
  if (targetCargo)
  {
    if (_targetExpanded) _scrollbar.OnDraw(vp, alpha);

    base::OnDrawBackGround(vp, alpha);

    DrawTargetCargoHeader(vp, targetCargo, alpha);
    if (_targetExpanded) DrawTargetCargoGrid(vp, targetCargo, alpha);
    return;
  }

  _scrollbar.OnDraw(vp, alpha);

  base::OnDrawBackGround(vp, alpha);

  ForEachSlot(
    [this, vp, alpha](int index, InventoryItem *item, float left, float top) -> bool
    {
      _ui.DrawSimpleBackground(this, vp, item, nullptr, left, top, alpha);
      return false;
    },
    [this, vp, alpha](int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded) -> bool
    {
      return _ui.DrawBackground(this, vp, container, cargo, nullptr, top, expanded, alpha);
    });
}

void CItemContainer::OnMouseHold(float x, float y, bool active)
{
  if (GInput.mouseL)
  {
    if (_scrollbar.IsEnabled() && _scrollbar.IsLocked())
      _scrollbar.OnMouseHold(x, y);
  }
}

void CItemContainer::OnLButtonDown(float x, float y)
{
  _moving = false;
  _dragging = false;

  InventoryStorage *targetCargo = GetTargetCargo();
  if (targetCargo)
  {
    Rect2DFloat btn = TargetToggleRect();
    if (x >= btn.x && x <= btn.x + btn.w && y >= btn.y && y <= btn.y + btn.h)
    {
      SetTargetExpanded(!_targetExpanded);
      return;
    }
  }

  if (_scrollbar.IsInside(x, y))
  {
    if (_scrollbar.IsEnabled()) _scrollbar.OnLButtonDown(x, y);
    return;
  }

  _dragX = _dragY = 0;
  base::OnLButtonDown(x, y);
}

void CItemContainer::OnLButtonUp(float x, float y)
{
  if (_scrollbar.IsLocked())
  {
    _scrollbar.OnLButtonUp(x, y);
  }

  if (GetTargetCargo())
  {
    base::OnLButtonUp(x, y);
    return;
  }

  if (!_dragging)
  {
    int found = -1;
    ForEachSlot(
      [this, x, y, &found](int index, InventoryItem *item, float left, float top) -> bool
      {
        return false;
      },
      [this, x, y, &found](int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded) -> bool
      {
        return _ui.FindHeader(this, container, cargo, top, expanded, x, y, index, found);
      });
    if (found >= 0)
    {
      InventoryItem *container = _cache[found];
      if (container) container->Open(!container->IsOpened());
      return;
    }
  }

  base::OnLButtonUp(x, y);
}

void CItemContainer::OnLButtonDblClick(float x, float y)
{
  base::OnLButtonDblClick(x, y);
}

void CItemContainer::OnMouseZChanged(float dz)
{
  if (dz == 0) return;
  if (!_targetExpanded && GetTargetCargo()) return;

  int pos = _scrollbar.GetPos();
  _scrollbar.SetPos(dz > 0 ? pos - 1 : pos + 1);
}

void CItemContainer::Update()
{
  InventoryStorage *targetCargo = GetTargetCargo();
  if (targetCargo)
  {
    const float sbHeight = _targetExpanded ? TargetGridHeight() : 0.0f;
    _scrollbar.SetPosition(_x + _w - _ui.GetSBWidth(), TargetGridTop(), _ui.GetSBWidth(), sbHeight);

    int rows = TargetRowsVisible(targetCargo);
    int pos = toIntFloor(_scrollbar.GetPos() + 0.5f);
    _scrollbar.SetRangeAndPos(0, targetCargo->GetHeight(), rows, pos);
    _scrollbar.Enable(_targetExpanded);
    return;
  }

  OLinkArray(InventoryItem) items;
  // find items in source
  if (_target)
  {
	Object *obj = _target;
	InventoryItem *item = dyn_cast<InventoryItem>(obj);
	if (item)
	{
		items.Add(item);
	}
	else
	{
		/*
		if( _target->GetSupply() )
		{
			InventoryStorage &cargo = _target->GetSupply()->GetItemsCargo();
			for (int i=0; i<cargo.GetItems().Size(); i++)
			{
				InventoryItem *item = cargo.GetItems()[i];
				if (!item) continue;
				items.Add(item);
			}
		}
		*/
		_target->ForEachOwnedItem([&items](InventoryItem *item) -> bool
		{
		  items.Add(item);
		  return false; // continue
		});
	}
  }
  else
  {
    const float radius = 1.0f;
    const EntityVisualState &vs = _person->FutureVisualState();
    Vector3Val center = vs.Position() + 0.5f * vs.Direction();

    int xMin, xMax, zMin, zMax;
    ObjRadiusRectangle(xMin, xMax, zMin, zMax, center, center, radius);
    float minDist2 = Square(radius);
    for (int x=xMin; x<=xMax; x++) for (int z=zMin; z<=zMax; z++)
    {
      const ObjectListUsed &list = GLandscape->UseObjects(x,z);
      int n = list.Size();    
      for (int i=0; i<n; i++)
      {
        Object *obj = list[i];
        // check the distance
        if (obj->FutureVisualState().Position().DistanceXZ2(center) > minDist2) continue;
        // check the type
        InventoryItem *item = dyn_cast<InventoryItem>(obj);
        if (!item) continue;
        // check the visibility
        CollisionBuffer retVal;
        GLandscape->ObjectCollisionLine(
          _person->GetRenderVisualStateAge(), retVal, Landscape::FilterIgnoreOne(_person),
          _person->EyePosition(vs), obj->CameraPosition(), 0,
          ObjIntersectFire, ObjIntersectNone // TODO: update when some problems occurs
        );
        // find the first intersected object
        int found = -1;
        float minT = 1e10;
        for (int i=0; i<retVal.Size(); i++)
        {
          const CollisionInfo &info = retVal[i];
          if (!info.object) continue;
          if (info.under < minT)
          {
            minT = info.under;
            found = i;
          }
        }
        if (found<0 || retVal[found].object==item || retVal[found].parentObject==item) items.Add(item);
      }
    }
  }

  // update cache based on the found items
  // remove items not in the list
  for (int i=_cache.Size()-1; i>=0; i--)
  {
    InventoryItem *item = _cache[i];
    if (item && items.Find(item) < 0) _cache.DeleteAt(i);
  }
  // add items in the list
  for (int i=0; i<items.Size(); i++)
  {
    InventoryItem *item = items[i];
    if (_cache.Find(item) < 0)
    {
      _cache.Add(item);
    }
  }

  // update the scrollbar range
  int countSimple = 0, countContainers = 0;
  for (int j=0; j<_cache.Size(); j++)
  {
    InventoryItem *container = _cache[j];
    if (!container) continue;

    ResourceSupply *supply = container->GetSupply();
    InventoryStorage *cargo = supply ? &supply->GetItemsCargo() : NULL;
    if (cargo && (cargo->GetHeight() <= 0 || cargo->GetWidth() <= 0)) cargo = NULL;

    if (!cargo && container->NAttachments() == 0)
      countSimple++;
    else
      countContainers++;
  }
  int columns = toIntFloor(_w / _titleW);
  int count = countContainers + (countSimple + columns - 1) / columns;

  int pos = _scrollbar.GetPos();
  _scrollbar.SetRangeAndPos(0, count, 1, pos); 
}

// ---------------------------------------------------------------------------
// SourceDayZ: grid view for big cargo storages (vehicle trunk, 5 x 100 slots)
// ---------------------------------------------------------------------------

InventoryStorage *CItemContainer::GetTargetCargo()
{
  if (!_target) return NULL;

  // Item targets (vests, backpacks, ...) keep the original list layout.
  Object *obj = _target;
  if (dyn_cast<InventoryItem>(obj)) return NULL;

  // Bodies use their own slot list, not a cargo grid.
  const EntityAIType *type = _target->GetType();
  if (type && type->IsPerson()) return NULL;

  ResourceSupply *supply = _target->GetSupply();
  if (!supply) return NULL;

  InventoryStorage &cargo = supply->GetItemsCargo();
  if (cargo.GetWidth() <= 0 || cargo.GetHeight() <= 0) return NULL;

  // Small containers fit the old list better; only big grids use cells.
  if (cargo.GetWidth() * cargo.GetHeight() < 24) return NULL;

  static Object *s_lastLogged = NULL;
  if (s_lastLogged != obj)
  {
    s_lastLogged = obj;
    RptF("SDZ cargo grid: %dx%d", cargo.GetWidth(), cargo.GetHeight());
  }
  return &cargo;
}

float CItemContainer::TargetCellSize(const InventoryStorage *cargo) const
{
  if (!cargo) return _ui.GetGridW();
  int cols = cargo->GetWidth();
  if (cols <= 0) return _ui.GetGridW();
  float size = (_w - _ui.GetSBWidth()) / cols;
  if (size < 0.005f) size = 0.005f;
  return size;
}

int CItemContainer::TargetRowsVisible(const InventoryStorage *cargo) const
{
  if (!cargo || !_targetExpanded) return 0;
  float size = TargetCellSize(cargo);
  if (size <= 0) return 0;

  int rows = toIntFloor(TargetGridHeight() / size + 0.001f);
  if (rows < 1) rows = 1;
  if (rows > cargo->GetHeight()) rows = cargo->GetHeight();
  return rows;
}

int CItemContainer::TargetFirstRow(const InventoryStorage *cargo) const
{
  if (!cargo) return 0;

  int rows = TargetRowsVisible(cargo);
  int maxRow = cargo->GetHeight() - rows;
  if (maxRow < 0) maxRow = 0;

  int first = toIntFloor(_scrollbar.GetPos() + 0.5f);
  if (first < 0) first = 0;
  if (first > maxRow) first = maxRow;
  return first;
}

int CItemContainer::TargetCargoIndex(const InventoryStorage *cargo, float x, float y) const
{
  if (!cargo || !_targetExpanded) return -1;
  const float top = TargetGridTop();
  if (y < top || y >= top + TargetGridHeight()) return -1;

  const float cell = TargetCellSize(cargo);
  const int cols = cargo->GetWidth();
  if (cols <= 0 || cell <= 0) return -1;
  if (x < _x || x >= _x + cols * cell) return -1;

  int col = toIntFloor((x - _x) / cell);
  int row = TargetFirstRow(cargo) + toIntFloor((y - top) / cell);
  if (col < 0 || col >= cols) return -1;
  if (row < 0 || row >= cargo->GetHeight()) return -1;
  return row * cols + col;
}

Rect2DFloat CItemContainer::TargetToggleRect() const
{
  const float width = _w - _ui.GetSBWidth();
  const float size = _titleH;
  const float right = _x + width - _ui.GetTextBorder();
  return Rect2DFloat(right - size, _y, size, _titleH);
}

void CItemContainer::SetTargetExpanded(bool expanded)
{
  if (_targetExpanded == expanded) return;
  _targetExpanded = expanded;
  _h = expanded ? _targetFullH : _titleH;
  if (!expanded) _scrollbar.SetPos(0);
}

void CItemContainer::DrawTargetCargoHeader(UIViewport *vp, InventoryStorage *cargo, float alpha)
{
  if (!cargo) return;

  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  const float width = _w - _ui.GetSBWidth();
  const float border = _ui.GetTextBorder();
  const float size = _ui.GetTextSize();

  Rect2DPixel clip(CX(_x), CY(_y), CW(width), CH(_titleH));
  Rect2DFloat clipF(_x, _y, width, _titleH);

  // header background
  Draw2DParsExt pars;
  pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  pars.SetColor(_ui.GetHeaderBgColor());
  pars.Init();
  vp->Draw2D(pars, Rect2DPixel(CX(_x), CY(_y), CW(width), CH(_titleH)), clip);

  // border
  vp->DrawLine(Line2DPixel(CX(_x), CY(_y), CX(_x + width), CY(_y)), _borderColor, _borderColor, clip);
  vp->DrawLine(Line2DPixel(CX(_x), CY(_y), CX(_x), CY(_y + _titleH)), _borderColor, _borderColor, clip);
  vp->DrawLine(Line2DPixel(CX(_x + width), CY(_y), CX(_x + width), CY(_y + _titleH)), _borderColor, _borderColor, clip);
  vp->DrawLine(Line2DPixel(CX(_x), CY(_y + _titleH), CX(_x + width), CY(_y + _titleH)), _borderColor, _borderColor, clip);

  TextDrawAttr attr(size, _ui.GetFont(), _ui.GetTextColor(), true);
  const float textY = _y + 0.5f * (_titleH - size);

  // open / minimize button
  Rect2DFloat btn = TargetToggleRect();
  Rect2DPixel btnClip(CX(btn.x), CY(btn.y), CW(btn.w), CH(btn.h));
  Rect2DFloat btnClipF(btn.x, btn.y, btn.w, btn.h);

  Draw2DParsExt btnPars;
  btnPars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  btnPars.SetColor(_ui.GetItemBgColor());
  btnPars.Init();
  vp->Draw2D(btnPars, Rect2DPixel(CX(btn.x), CY(btn.y), CW(btn.w), CH(btn.h)), btnClip);
  vp->DrawLine(Line2DPixel(CX(btn.x), CY(btn.y), CX(btn.x + btn.w), CY(btn.y)), _borderColor, _borderColor, btnClip);
  vp->DrawLine(Line2DPixel(CX(btn.x), CY(btn.y + btn.h), CX(btn.x + btn.w), CY(btn.y + btn.h)), _borderColor, _borderColor, btnClip);
  vp->DrawLine(Line2DPixel(CX(btn.x), CY(btn.y), CX(btn.x), CY(btn.y + btn.h)), _borderColor, _borderColor, btnClip);
  vp->DrawLine(Line2DPixel(CX(btn.x + btn.w), CY(btn.y), CX(btn.x + btn.w), CY(btn.y + btn.h)), _borderColor, _borderColor, btnClip);

  RString symbol = _targetExpanded ? "-" : "+";
  float symbolW = vp->GetTextWidth(attr, symbol);
  vp->DrawText(Point2DFloat(btn.x + 0.5f * (btn.w - symbolW), btn.y + 0.5f * (btn.h - size)), btnClipF, attr, symbol);

  // slot count
  RString slots = GetSpaceText(*cargo) + " slots";
  float slotsW = vp->GetTextWidth(attr, slots);
  float slotsX = btn.x - slotsW - 2.0f * border;
  vp->DrawText(Point2DFloat(slotsX, textY), clipF, attr, slots);

  // vehicle name
  RString name = _target ? _target->GetDisplayName() : RString();
  if (!name.IsEmpty())
  {
    Rect2DFloat nameClip(_x + border, _y, slotsX - _x - 2.0f * border, _titleH);
    if (nameClip.w > 0.0f)
      vp->DrawText(Point2DFloat(_x + border, textY), nameClip, attr, name);
  }
}

void CItemContainer::DrawTargetCargoGrid(UIViewport *vp, InventoryStorage *cargo, float alpha)
{
  if (!cargo) return;

  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  const float cell = TargetCellSize(cargo);
  const int cols = cargo->GetWidth();
  const int rows = cargo->GetHeight();
  const int first = TargetFirstRow(cargo);
  const int visibleRows = TargetRowsVisible(cargo);
  const float width = _w - _ui.GetSBWidth();
  const float top = TargetGridTop();
  const float bottom = top + TargetGridHeight();

  Rect2DPixel clip(CX(_x), CY(top), CW(width), CH(TargetGridHeight()));

  Draw2DParsExt pars;
  pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  pars.Init();

  // empty cells
  pars.SetColor(_ui.GetNoItemBgColor());
  for (int r=0; r<visibleRows; r++)
  {
    float rowTop = top + r * cell;
    for (int c=0; c<cols; c++)
    {
      float left = _x + c * cell;
      vp->Draw2D(pars, Rect2DPixel(CX(left), CY(rowTop), CW(cell), CH(cell)), clip);
    }
  }

  // occupied cells
  const RefArray<InventoryItem> &items = cargo->GetItems();
  const int total = intMin(items.Size(), cols * rows);
  pars.SetColor(_ui.GetItemBgColor());
  for (int i=0; i<total; i++)
  {
    const InventoryItem *item = items[i];
    if (!item || !item->Type()) continue;

    int r = i / cols;
    int c = i % cols;
    if (r < first || r >= first + visibleRows) continue;

    int iw = item->GetInventoryWidth();
    int ih = item->GetInventoryHeight();
    float left = _x + c * cell;
    float itemTop = top + (r - first) * cell;
    vp->Draw2D(pars, Rect2DPixel(CX(left), CY(itemTop), CW(iw * cell), CH(ih * cell)), clip);
  }

  // grid lines
  for (int c=0; c<=cols; c++)
  {
    float x = _x + c * cell;
    if (x > _x + width) x = _x + width;
    vp->DrawLine(Line2DPixel(CX(x), CY(top), CX(x), CY(bottom)), _borderColor, _borderColor, clip);
  }
  for (int r=0; r<=visibleRows; r++)
  {
    float y = top + r * cell;
    if (y > bottom) y = bottom;
    vp->DrawLine(Line2DPixel(CX(_x), CY(y), CX(_x + width), CY(y)), _borderColor, _borderColor, clip);
  }
}

void CItemContainer::DrawTargetCargoItems(InventoryStorage *cargo, float alpha)
{
  if (!cargo) return;

  DisplayInventory *display = dynamic_cast<DisplayInventory *>(_parent.GetLink());
  const float cell = TargetCellSize(cargo);
  const int cols = cargo->GetWidth();
  const int first = TargetFirstRow(cargo);
  const int visibleRows = TargetRowsVisible(cargo);
  const float gridTop = TargetGridTop();
  const float bottom = gridTop + TargetGridHeight();
  const RefArray<InventoryItem> &items = cargo->GetItems();
  const int total = intMin(items.Size(), cols * cargo->GetHeight());

  for (int i=0; i<total; i++)
  {
    InventoryItem *item = items[i];
    if (!item || !item->Type()) continue;

    int r = i / cols;
    int c = i % cols;
    if (r < first || r >= first + visibleRows) continue;

    int iw = item->GetInventoryWidth();
    int ih = item->GetInventoryHeight();
    float left = _x + c * cell;
    float top = gridTop + (r - first) * cell;
    if (top + ih * cell > bottom + 0.001f) continue;

    Color color(0, 0, 0, -1.0f);
    if (display)
    {
      if (display->GetDraggedItem() == item) color = ColorItemDragged;
      else if (display->GetSwappedItem() == item) color = ColorItemSwapped;
    }
    DrawItem(item, left, top, iw * cell, ih * cell, _z, alpha, color);
  }
}

void CItemContainer::DrawTargetCargoForeground(UIViewport *vp, InventoryStorage *cargo, float alpha)
{
  if (!cargo) return;

  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  const float cell = TargetCellSize(cargo);
  const int cols = cargo->GetWidth();
  const int first = TargetFirstRow(cargo);
  const int visibleRows = TargetRowsVisible(cargo);
  const RefArray<InventoryItem> &items = cargo->GetItems();
  const int total = intMin(items.Size(), cols * cargo->GetHeight());
  const float width = _w - _ui.GetSBWidth();
  const float size = _ui.GetTextSize();
  const float gridTop = TargetGridTop();

  Rect2DPixel clip(CX(_x), CY(gridTop), CW(width), CH(TargetGridHeight()));
  Rect2DFloat clipF(_x, gridTop, width, TargetGridHeight());
  ClippedViewport vpClipped(vp, clipF);

  for (int i=0; i<total; i++)
  {
    InventoryItem *item = items[i];
    if (!item || !item->Type()) continue;

    int r = i / cols;
    int c = i % cols;
    if (r < first || r >= first + visibleRows) continue;

    RefR<INode> text = item->GetQuantity();
    if (!text) continue;

    SetTextAttribute(text, "shadow", "1");
    Rect2DFloat rect(0, 0, item->GetInventoryWidth() * cell, item->GetInventoryHeight() * cell);
    float h = GetTextHeight(text, rect, size);
    float w = GetTextWidthFloat(text, size);
    rect.x = _x + c * cell + rect.w - w;
    rect.y = gridTop + (r - first) * cell + rect.h - h;
    DrawText(vpClipped, text, rect, size);
  }
}

DragItemInfo CItemContainer::GetTargetCargoInfo(const InventoryStorage *cargo, float x, float y)
{
  DragItemInfo info;
  info._item = NULL;
  if (!cargo || !_targetExpanded) return info;

  const float gridTop = TargetGridTop();
  if (y < gridTop || y >= gridTop + TargetGridHeight()) return info;

  const float cell = TargetCellSize(cargo);
  const int cols = cargo->GetWidth();
  if (cols <= 0 || cell <= 0) return info;
  if (x < _x || x >= _x + cols * cell) return info;

  int col = toIntFloor((x - _x) / cell);
  int row = TargetFirstRow(cargo) + toIntFloor((y - gridTop) / cell);
  if (col < 0 || col >= cols) return info;
  if (row < 0 || row >= cargo->GetHeight()) return info;

  const RefArray<InventoryItem> &items = cargo->GetItems();
  const int total = intMin(items.Size(), cols * cargo->GetHeight());
  for (int i=0; i<total; i++)
  {
    const InventoryItem *item = items[i];
    if (!item || !item->Type()) continue;

    int r = i / cols;
    int c = i % cols;
    int iw = item->GetInventoryWidth();
    int ih = item->GetInventoryHeight();
    if (row >= r && row < r + ih && col >= c && col < c + iw)
    {
      info._item = unconst_cast(item);
      info._offsetX = x - (_x + c * cell);
      info._offsetY = y - (gridTop + (r - TargetFirstRow(cargo)) * cell);
      return info;
    }
  }
  return info;
}

DragItemInfo CItemContainer::GetItemInfo(float x, float y)
{
  InventoryStorage *targetCargo = GetTargetCargo();
  if (targetCargo) return GetTargetCargoInfo(targetCargo, x, y);

  DragItemInfo info;
  info._item = NULL;

  ForEachSlot(
    [this, x, y, &info](int index, InventoryItem *item, float left, float top) -> bool
    {
      if (y >= top && y < top + _titleH && x >= left && x < left + _titleW)
      {
        info._item = item;
        info._offsetX = x - left;
        info._offsetY = y - top;
        return true;
      }
      return false;
    },
    [this, x, y, &info](int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded) -> bool
    {
      return _ui.GetItemInfo(this, container, cargo, top, expanded, x, y, info);
    });

  return info;
}

bool CItemContainer::IsContained(InventoryItem *check)
{
  InventoryStorage *targetCargo = GetTargetCargo();
  if (targetCargo && targetCargo->FindItem(check)) return true;

  for (int j=0; j<_cache.Size(); j++)
  {
    InventoryItem *item = _cache[j];
    if (!item) continue;
    if (item == check) return true;

    ResourceSupply *supply = item->GetSupply();
    InventoryStorage *cargo = supply ? &supply->GetItemsCargo() : NULL;
    if (cargo && (cargo->GetHeight() <= 0 || cargo->GetWidth() <= 0)) cargo = NULL;

    if (cargo && cargo->FindItem(check)) return true;
    for (int i=0; i<item->NAttachments(); i++)
    {
      if (item->GetAttachment(i) == check) return true;
    }
  }
  return false;
}

bool CItemContainer::CanAddItem(InventoryItem *item, float x, float y)
{
  InventoryStorage *targetCargo = GetTargetCargo();
  if (targetCargo)
  {
    if (item == _target) return false;
    int index = TargetCargoIndex(targetCargo, x, y);
    if (index < 0) return false;
    return CanMoveItem(_target, index, item);
  }

  InventoryItem *result = NULL;
  int resultIndex = -1;

  ForEachSlot(
    [this, x, y, &result, &resultIndex](int index, InventoryItem *item, float left, float top) -> bool
    {
      return false;
    },
    [this, x, y, &result, &resultIndex](int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded) -> bool
    {
      return _ui.FindSlot(this, container, cargo, top, expanded, x, y, result, resultIndex);
    });

  if (result)
  {
    if (item == result) return false; // cannot contain myself
    // if (resultIndex < 0) return false; // drop on header not supported yet

    return CanMoveItem(result, resultIndex, item);
  }

  // drop on free space
  if (_target)
    return CanMoveItem(_target, -1, item);
  else
    return item->CheckInventoryCondition(nullptr); // drop on ground
}

void CItemContainer::AddItem(InventoryItem *item, float x, float y)
{
  InventoryStorage *targetCargo = GetTargetCargo();
  if (targetCargo)
  {
    if (item == _target) return;
    int index = TargetCargoIndex(targetCargo, x, y);
    if (index >= 0 && CanMoveItem(_target, index, item))
      MoveItem(_target, index, item);
    return;
  }

  InventoryItem *result = NULL;
  int resultIndex = -1;

  ForEachSlot(
    [this, x, y, &result, &resultIndex](int index, InventoryItem *item, float left, float top) -> bool
    {
      return false;
    },
    [this, x, y, &result, &resultIndex](int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded) -> bool
    {
      return _ui.FindSlot(this, container, cargo, top, expanded, x, y, result, resultIndex);
    });

  if (result)
  {
    if (item == result) return; // cannot contain myself
    // if (resultIndex < 0) return; // drop on header not supported yet

    MoveItem(result, resultIndex, item);
    return;
  }

  // drop on free space
  if (_target)
    MoveItem(_target, -1, item);
  else
    DropItem(_person, item);
}

float CItemContainer::GetGridW()
{
  return _titleW;
}

float CItemContainer::GetGridH()
{
  return _titleH;
}

class CItemContent : public CItemBase
{
protected:
  typedef CItemBase base;

  float _titleW, _titleH;

  OLink(Person) _person;

  ContainerUI _ui;

  //! scroll bar used by control
  CScrollBar _scrollbar;

public:
  CItemContent(ControlsContainer *parent, int idc, ParamEntryPar cls, Person *person);

  virtual void OnDraw(float alpha) override;
  virtual void OnDrawForeGround(UIViewport *vp, float alpha) override;
  virtual void OnDrawBackGround(UIViewport *vp, float alpha) override;

  virtual void OnMouseHold(float x, float y, bool active) override;
  virtual void OnLButtonDown(float x, float y) override;
  virtual void OnLButtonUp(float x, float y) override;
  virtual void OnLButtonDblClick(float x, float y) override;
  virtual void OnMouseZChanged(float dz) override;

  // CItemBase implementation
  virtual DragItemInfo GetItemInfo(float x, float y) override;
  virtual bool CanAddItem(InventoryItem *item, float x, float y) override;
  virtual void AddItem(InventoryItem *item, float x, float y) override;
  virtual float GetGridW() override;
  virtual float GetGridH() override;
  virtual bool IsContained(InventoryItem *item) override;

private:
  // cycle through all subcontrols
  bool ForEachSlot(
    const std::function<bool(int index, InventoryItem *item, float left, float top)> &funcSimple,
    const std::function<bool(int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded)> &funcContainer
    );
};

CItemContent::CItemContent(ControlsContainer *parent, int idc, ParamEntryPar cls, Person *person)
  : base(parent, idc, cls), _ui(cls), _scrollbar(false, false, cls >> "ScrollBar")
{
  _person = person;

  _titleW = cls >> "titleW";
  _titleH = cls >> "titleH";

  _scrollbar.SetPosition
  (
    _x + _w - _ui.GetSBWidth(), _y,
    _ui.GetSBWidth(), _h
  );
  _scrollbar.SetColor(_borderColor);
  _scrollbar.SetRangeAndPos(0, 0, 1, 0); 
  _scrollbar.SetSpeed(1, 1); 
}

bool CItemContent::ForEachSlot(
  const std::function<bool(int index, InventoryItem *item, float left, float top)> &funcSimple,
  const std::function<bool(int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded)> &funcContainer
  )
{
  int first = _scrollbar.GetPos();
  float top = _y;
  float vSpace = 0.25f * _titleH;

  // simple items first
  int columns = toIntFloor(_w / _titleW);
  int col = 0;
  float left = _x;
  for (int order=0; order<NInventorySlotId; order++)
  {
    // Keep the serialized slot IDs unchanged, but show both shoulder weapons
    // together in the inventory before clothing containers.
    int j = order == ISShoulder + 1 ? ISShoulder2 :
      (order > ISShoulder + 1 && order <= ISShoulder2 ? order - 1 : order);
    InventoryItem *item = _person->GetInSlot((InventorySlotId)j);
    if (!item) continue;

    ResourceSupply *supply = item->GetSupply();
    InventoryStorage *cargo = supply ? &supply->GetItemsCargo() : NULL;
    if (cargo && (cargo->GetHeight() <= 0 || cargo->GetWidth() <= 0)) cargo = NULL;

    if (!cargo && item->NAttachments() == 0)
    {
      // simple item
      if (first <= 0)
      {
        // visible, invoke the functor
        if (funcSimple(j, item, left, top)) return true;
      }
      col++; left += _titleW;
      if (col >= columns)
      {
        // the next row
        if (first-- <= 0) top += _titleH + vSpace;
        col = 0; left = _x;
      }
    }
  }
  if (col > 0)
  {
    // the next row
    if (first-- <= 0) top += _titleH + vSpace;
  }
  // containers then
  for (int order=0; order<NInventorySlotId; order++)
  {
    int j = order == ISShoulder + 1 ? ISShoulder2 :
      (order > ISShoulder + 1 && order <= ISShoulder2 ? order - 1 : order);
    InventoryItem *container = _person->GetInSlot((InventorySlotId)j);
    if (!container) continue;

    ResourceSupply *supply = container->GetSupply();
    InventoryStorage *cargo = supply ? &supply->GetItemsCargo() : NULL;
    if (cargo && (cargo->GetHeight() <= 0 || cargo->GetWidth() <= 0)) cargo = NULL;

    if (!cargo && container->NAttachments() == 0) continue;

    if (first-- > 0) continue; // skip scrolled items

    // invoke the functor
    if (funcContainer(j, container, cargo, top, container->IsOpened())) return true;

    // forward top
    top += _ui.GetHeight(this, container, cargo, container->IsOpened());
    top += vSpace; // space
  }
  return false;
}

void CItemContent::OnDraw(float alpha)
{
  // update the scrollbar range
  int countSimple = 0, countContainers = 0;
  for (int j=0; j<NInventorySlotId; j++)
  {
    InventoryItem *container = _person->GetInSlot((InventorySlotId)j);
    if (!container) continue;

    ResourceSupply *supply = container->GetSupply();
    InventoryStorage *cargo = supply ? &supply->GetItemsCargo() : NULL;
    if (cargo && (cargo->GetHeight() <= 0 || cargo->GetWidth() <= 0)) cargo = NULL;

    if (!cargo && container->NAttachments() == 0)
      countSimple++;
    else
      countContainers++;
  }
  int columns = toIntFloor(_w / _titleW);
  int count = countContainers + (countSimple + columns - 1) / columns;

  int pos = _scrollbar.GetPos();
  _scrollbar.SetRangeAndPos(0, count, 1, pos); 

  DisplayInventory *display = dynamic_cast<DisplayInventory *>(_parent.GetLink());

  ForEachSlot(
    [this, display, alpha](int index, InventoryItem *item, float left, float top) -> bool
    {
      Color color(0, 0, 0, -1.0f);
      if (display)
      {
        if (display->GetDraggedItem() == item) color = ColorItemDragged;
        else if (display->GetSwappedItem() == item) color = ColorItemSwapped;
      }

      DrawItem(item, left, top, _titleW, _titleH, _z, alpha, color);
      return false;
    },
    [this, alpha](int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded) -> bool
    {
      return _ui.DrawObjects(_parent, this, container, cargo, top, expanded, alpha);
    });
}

void CItemContent::OnDrawForeGround(UIViewport *vp, float alpha)
{
  ForEachSlot(
    [this, vp, alpha](int index, InventoryItem *item, float left, float top) -> bool
    {
      _ui.DrawSimpleForeground(this, vp, item, nullptr, left, top, alpha);
      return false;
    },
    [this, vp, alpha](int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded) -> bool
    {
      return _ui.DrawForeground(this, vp, container, cargo, _person, top, expanded, alpha);
    });
}

void CItemContent::OnDrawBackGround(UIViewport *vp, float alpha)
{
  _scrollbar.OnDraw(vp, alpha);

  base::OnDrawBackGround(vp, alpha);

  ForEachSlot(
    [this, vp, alpha](int index, InventoryItem *item, float left, float top) -> bool
    {
      _ui.DrawSimpleBackground(this, vp, item, _person, left, top, alpha);
      return false;
    },
    [this, vp, alpha](int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded) -> bool
    {
      return _ui.DrawBackground(this, vp, container, cargo, _person, top, expanded, alpha);
    });
}

void CItemContent::OnMouseHold(float x, float y, bool active)
{
  if (GInput.mouseL)
  {
    if (_scrollbar.IsEnabled() && _scrollbar.IsLocked())
      _scrollbar.OnMouseHold(x, y);
  }
}

void CItemContent::OnLButtonDown(float x, float y)
{
  _moving = false;
  _dragging = false;

  if (_scrollbar.IsInside(x, y))
  {
    if (_scrollbar.IsEnabled()) _scrollbar.OnLButtonDown(x, y);
    return;
  }

  _dragX = _dragY = 0;
  base::OnLButtonDown(x, y);
}

void CItemContent::OnLButtonUp(float x, float y)
{
  if (_scrollbar.IsLocked())
  {
    _scrollbar.OnLButtonUp(x, y);
  }

  if (!_dragging)
  {
    int found = -1;
    ForEachSlot(
      [this, x, y, &found](int index, InventoryItem *item, float left, float top) -> bool
      {
        return false;
      },
      [this, x, y, &found](int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded) -> bool
      {
        return _ui.FindHeader(this, container, cargo, top, expanded, x, y, index, found);
      });
    if (found >= 0)
    {
      InventoryItem *container = _person->GetInSlot((InventorySlotId)found);
      if (container) container->Open(!container->IsOpened());
    }
  }

  base::OnLButtonUp(x, y);
}

void CItemContent::OnLButtonDblClick(float x, float y)
{
  base::OnLButtonDblClick(x, y);
}

void CItemContent::OnMouseZChanged(float dz)
{
  if (dz == 0) return;

  int pos = _scrollbar.GetPos();
  _scrollbar.SetPos(dz > 0 ? pos - 1 : pos + 1);
}

DragItemInfo CItemContent::GetItemInfo(float x, float y)
{
  DragItemInfo info;
  info._item = NULL;

  ForEachSlot(
    [this, x, y, &info](int index, InventoryItem *item, float left, float top) -> bool
    {
      if (y >= top && y < top + _titleH && x >= left && x < left + _titleW)
      {
        info._item = item;
        info._offsetX = x - left;
        info._offsetY = y - top;
        return true;
      }
      return false;
    },
    [this, x, y, &info](int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded) -> bool
    {
      return _ui.GetItemInfo(this, container, cargo, top, expanded, x, y, info);
    });

  return info;
}

bool CItemContent::IsContained(InventoryItem *check)
{
  for (int j=0; j<NInventorySlotId; j++)
  {
    InventoryItem *item = _person->GetInSlot((InventorySlotId)j);
    if (!item) continue;
    if (item == check) return true;

    ResourceSupply *supply = item->GetSupply();
    InventoryStorage *cargo = supply ? &supply->GetItemsCargo() : NULL;
    if (cargo && (cargo->GetHeight() <= 0 || cargo->GetWidth() <= 0)) cargo = NULL;

    if (cargo && cargo->FindItem(check)) return true;
    for (int i=0; i<item->NAttachments(); i++)
    {
      if (item->GetAttachment(i) == check) return true;
    }
  }
  return false;
}

bool CItemContent::CanAddItem(InventoryItem *item, float x, float y)
{
  InventoryItem *result = NULL;
  int resultIndex = -1;

  ForEachSlot(
    [this, x, y, &result, &resultIndex](int index, InventoryItem *item, float left, float top) -> bool
    {
      return false;
    },
    [this, x, y, &result, &resultIndex](int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded) -> bool
    {
      return _ui.FindSlot(this, container, cargo, top, expanded, x, y, result, resultIndex);
    });

  if (result)
  {
    if (item == result) return false; // cannot contain myself
    // if (resultIndex < 0) return false; // drop on header not supported

    return CanMoveItem(result, resultIndex, item);
  }
  else
  {
    return CanMoveItem(_person, -1, item); // _person->CanAddItem(item);
  }
}

void CItemContent::AddItem(InventoryItem *item, float x, float y)
{
  InventoryItem *result = NULL;
  int resultIndex = -1;

  ForEachSlot(
    [this, x, y, &result, &resultIndex](int index, InventoryItem *item, float left, float top) -> bool
    {
      return false;
    },
    [this, x, y, &result, &resultIndex](int index, InventoryItem *container, InventoryStorage *cargo, float top, bool expanded) -> bool
    {
      return _ui.FindSlot(this, container, cargo, top, expanded, x, y, result, resultIndex);
    });

  if (result)
  {
    if (item == result) return; // cannot contain myself
    // if (resultIndex < 0) return; // drop on header not supported yet

    MoveItem(result, resultIndex, item);
  }
  else
  {
    if (CanMoveItem(_person, -1, item))
      MoveItem(_person, -1, item);
  }
}

float CItemContent::GetGridW()
{
  return _titleW;
}

float CItemContent::GetGridH()
{
  return _titleH;
}

class CItemInHands : public CItemBase
{
protected:
  typedef CItemBase base;

  float _gridW, _gridH;

  // structured text size
  float _size;

  OLink(Person) _person;

  // items background color
  PackedColor _bgNoItemColor;
  PackedColor _bgItemColor;

public:
  CItemInHands(ControlsContainer *parent, int idc, ParamEntryPar cls, Person *person);

  virtual void OnDraw(float alpha) override;
  virtual void OnDrawForeGround(UIViewport *vp, float alpha) override;
  virtual void OnDrawBackGround(UIViewport *vp, float alpha) override;

  // CItemBase implementation
  virtual DragItemInfo GetItemInfo(float x, float y) override;
  virtual bool CanAddItem(InventoryItem *item, float x, float y) override;
  virtual void AddItem(InventoryItem *item, float x, float y) override;
  virtual float GetGridW() override;
  virtual float GetGridH() override;
  virtual bool IsContained(InventoryItem *item) override;
};

CItemInHands::CItemInHands(ControlsContainer *parent, int idc, ParamEntryPar cls, Person *person)
  : base(parent, idc, cls)
{
  _person = person;

  _gridW = cls >> "gridW";
  _gridH = cls >> "gridH";

  _size = cls >> "sizeEx";

  _bgNoItemColor = GetPackedColor(cls >> "colorNoItemBackground");
  _bgItemColor = GetPackedColor(cls >> "colorItemBackground");
}

void CItemInHands::OnDraw(float alpha)
{
  InventoryItem *item = _person ? _person->GetItemInHands() : NULL;
  if (item)
  {
    float height = _h - _gridH;

    DisplayInventory *display = dynamic_cast<DisplayInventory *>(_parent.GetLink());

    Color color(0, 0, 0, -1.0f);
    if (display)
    {
      if (display->GetDraggedItem() == item) color = ColorItemDragged;
      else if (display->GetSwappedItem() == item) color = ColorItemSwapped;
    }

    DrawItem(item, _x, _y, _w, height, _z, alpha, color);

    float top = _y + height;
    int columns = toIntFloor(_w / _gridW);
    saturateMax(columns, 1);
    int index = 0;

    int n = item->NAttachments();
    if (n > 0)
    {
      for (int i=0; i<n; i++)
      {
        InventoryItem *attachment = item->GetAttachment(i);
        if (attachment)
        {
          Color color(0, 0, 0, -1.0f);
          if (display)
          {
            if (display->GetDraggedItem() == attachment) color = ColorItemDragged;
            else if (display->GetSwappedItem() == attachment) color = ColorItemSwapped;
          }

          DrawItem(attachment, _x + index * _gridW, top, _gridW, _gridH, _z, alpha, color);
          if (++index >= columns) break;
        }
      }
    }
    else if (item->GetSupply())
    {
      InventoryStorage &cargo = item->GetSupply()->GetItemsCargo();
      for (int i=0; i<cargo.GetItems().Size(); i++)
      {
        InventoryItem *cargoItem = cargo.GetItems()[i];
        if (cargoItem)
        {
          Color color(0, 0, 0, -1.0f);
          if (display)
          {
            if (display->GetDraggedItem() == cargoItem) color = ColorItemDragged;
            else if (display->GetSwappedItem() == cargoItem) color = ColorItemSwapped;
          }

          DrawItem(cargoItem, _x + index * _gridW, top, _gridW, _gridH, _z, alpha, color);
          if (++index >= columns) break;
        }
      }
    }
  }
}

void CItemInHands::OnDrawForeGround(UIViewport *vp, float alpha)
{
  InventoryItem *item = _person ? _person->GetItemInHands() : NULL;
  if (item)
  {
    float top = _y + _h - _gridH;
    float x = _x;

    int n = item->NAttachments();
    if (n > 0)
    {
      for (int i=0; i<n; i++)
      {
        InventoryItem *attachment = item->GetAttachment(i);
        if (attachment)
        {
          // quantity
          RefR<INode> text = attachment->GetQuantity();
          if (text)
          {
            SetTextAttribute(text, "shadow", "1");
            float h = GetTextHeight(text, Rect2DFloat(0, 0, _gridW, _gridH), _size);
            float w = GetTextWidthFloat(text, _size);
            DrawText(vp, text, Rect2DFloat(x + _gridW - w, top + _gridH - h, _gridW, _gridH), _size);
          }
          x += _gridW;

          if (x + _gridW > _x + _w) break; // no more items fit
        }
      }
    }
    else if (item->GetSupply())
    {
      InventoryStorage &cargo = item->GetSupply()->GetItemsCargo();
      for (int i=0; i<cargo.GetItems().Size(); i++)
      {
        InventoryItem *cargoItem = cargo.GetItems()[i];
        if (cargoItem)
        {
          // quantity
          RefR<INode> text = cargoItem->GetQuantity();
          if (text)
          {
            SetTextAttribute(text, "shadow", "1");
            float h = GetTextHeight(text, Rect2DFloat(0, 0, _gridW, _gridH), _size);
            float w = GetTextWidthFloat(text, _size);
            DrawText(vp, text, Rect2DFloat(x + _gridW - w, top + _gridH - h, _gridW, _gridH), _size);
          }
          x += _gridW;

          if (x + _gridW > _x + _w) break; // no more items fit
        }
      }
    }
  }
}

void CItemInHands::OnDrawBackGround(UIViewport *vp, float alpha)
{
  base::OnDrawBackGround(vp, alpha);

  InventoryItem *item = _person ? _person->GetItemInHands() : NULL;
  if (item)
  {
    float top = _y + _h - _gridH;

    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();

    float y1 = CY(top);
    float y2 = CY(top + _gridH);
    float ww = CW(_gridW);
    float x = _x;

    Draw2DParsExt pars;
    pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
    pars.Init();

    int n = item->NAttachments();
    if (n > 0)
    {
      for (int i=0; i<n; i++)
      {
        InventoryItem *attachment = item->GetAttachment(i);

        if (attachment)
        {
          float cx = CX(x);
          pars.SetColor(attachment ? _bgItemColor : _bgNoItemColor);
          vp->Draw2D(pars, Rect2DPixel(cx, y1, ww, y2 - y1));

          vp->DrawLine(Line2DPixel(cx, y1, cx, y2), _borderColor, _borderColor);
          x += _gridW;

          if (x + _gridW > _x + _w) break; // no more items fit
        }
      }
      vp->DrawLine(Line2DPixel(CX(_x), y1, CX(x), y1), _borderColor, _borderColor);
      vp->DrawLine(Line2DPixel(CX(_x), y2, CX(x), y2), _borderColor, _borderColor);
      vp->DrawLine(Line2DPixel(CX(x), y1, CX(x), y2), _borderColor, _borderColor);
    }
    else if (item->GetSupply())
    {
      InventoryStorage &cargo = item->GetSupply()->GetItemsCargo();
      for (int i=0; i<cargo.GetItems().Size(); i++)
      {
        InventoryItem *cargoItem = cargo.GetItems()[i];
        if (cargoItem)
        {
          float cx = CX(x);
          pars.SetColor(cargoItem ? _bgItemColor : _bgNoItemColor);
          vp->Draw2D(pars, Rect2DPixel(cx, y1, ww, y2 - y1));

          vp->DrawLine(Line2DPixel(cx, y1, cx, y2), _borderColor, _borderColor);
          x += _gridW;

          if (x + _gridW > _x + _w) break; // no more items fit
        }
      }
      vp->DrawLine(Line2DPixel(CX(_x), y1, CX(x), y1), _borderColor, _borderColor);
      vp->DrawLine(Line2DPixel(CX(_x), y2, CX(x), y2), _borderColor, _borderColor);
      vp->DrawLine(Line2DPixel(CX(x), y1, CX(x), y2), _borderColor, _borderColor);
    }
  }
}

DragItemInfo CItemInHands::GetItemInfo(float x, float y)
{
  DragItemInfo info;
  info._item = NULL;

  InventoryItem *item = _person ? _person->GetItemInHands() : NULL;
  if (!item) return info;

  // The item itself
  float top = _y + _h - _gridH;
  if (y < top)
  {
    info._item = item;
    info._offsetX = x - _x;
    info._offsetY = y - _y;
    return info;
  }

  int c = toIntFloor((x - _x) / _gridW);
  float ox = x - (_x + c * _gridW);

  // The attachments
  int n = item->NAttachments();
  if (n > 0)
  {
    for (int i=0; i<n; i++)
    {
      InventoryItem *attachment = item->GetAttachment(i);
      if (!attachment) continue;
      if (c-- <= 0)
      {
        info._item = attachment;
        info._offsetX = ox;
        info._offsetY = y - top;
        return info;
      }
    }
  }
  else if (item->GetSupply())
  {
    InventoryStorage &cargo = item->GetSupply()->GetItemsCargo();
    for (int i=0; i<cargo.GetItems().Size(); i++)
    {
      InventoryItem *cargoItem = cargo.GetItems()[i];
      if (!cargoItem) continue;
      if (c-- <= 0)
      {
        info._item = cargoItem;
        info._offsetX = ox;
        info._offsetY = y - top;
        return info;
      }
    }
  }

  return info;
}

bool CItemInHands::IsContained(InventoryItem *check)
{
  if (!_person) return false;

  InventoryItem *item = _person ? _person->GetItemInHands() : NULL;
  if (!item) return false;
  if (item == check) return true;

  // The attachments
  int n = item->NAttachments();
  if (n > 0)
  {
    for (int i=0; i<n; i++)
    {
      InventoryItem *attachment = item->GetAttachment(i);
      if (attachment && attachment == check) return true;
    }
  }
  else if (item->GetSupply())
  {
    InventoryStorage &cargo = item->GetSupply()->GetItemsCargo();
    for (int i=0; i<cargo.GetItems().Size(); i++)
    {
      InventoryItem *cargoItem = cargo.GetItems()[i];
      if (cargoItem && cargoItem == check) return true;
    }
  }

  return false;
}

bool CItemInHands::CanAddItem(InventoryItem *newItem, float x, float y)
{
  if (!_person) return false;

  const Person * const p = _person;
  const Man * const m = dynamic_cast<const Man *>(p);
  if (m && m->IsOnAnyLadder())
    return false;

  // Ground items can be dragged directly into empty hands as well.
  if (!_person->FindItem(newItem) && !newItem->IsInLandscape()) return false;

  float top = _y + _h - _gridH;

  // Directly to hands
  InventoryItem *oldItem = _person->GetItemInHands();
  if (!oldItem)
  {
    return true;
  }

  if (y < top)
  {
    if (newItem->Type()) return true; // directly to hands
    return CanMoveItem(oldItem, -1, newItem); // as an attachment
  }

  // As an attachments
  int n = oldItem->NAttachments();
  if (n > 0)
  {
    int c = toIntFloor((x - _x) / _gridW);
    for (int i=0; i<n; i++)
    {
      InventoryItem *attachment = oldItem->GetAttachment(i);
      if (!attachment) continue;
      if (c-- <= 0)
      {
        // check if compatible
        return CanMoveItem(oldItem, i, newItem);
      }
    }
  }

  return false;
}

void CItemInHands::AddItem(InventoryItem *newItem, float x, float y)
{
  if (!_person) return;

  if (!_person->FindItem(newItem) && !newItem->IsInLandscape()) return;

  float top = _y + _h - _gridH;

  // Directly to hands
  InventoryItem *oldItem = _person->GetItemInHands();
  if (!oldItem)
  {
    TakeItemToHands(_person, newItem);
    return;
  }

  if (y < top)
  {
    TakeItemToHands(_person, newItem);
    return;
  }

  // As an attachments
  int n = oldItem->NAttachments();
  if (n > 0)
  {
    int c = toIntFloor((x - _x) / _gridW);
    for (int i=0; i<n; i++)
    {
      InventoryItem *attachment = oldItem->GetAttachment(i);
      if (!attachment) continue;
      if (c-- <= 0)
      {
        MoveItem(oldItem, i, newItem);
        return;
      }
    }
  }
}

float CItemInHands::GetGridW()
{
  return _gridW;
}

float CItemInHands::GetGridH()
{
  return _gridH;
}

class CPlayerPreview : public ControlObject
{
private:
  typedef ControlObject base;
  // info about proxies in the shape
  ProxiesHolder _proxies;

  OLinkPermO(Person) _person;
  //! head type (shape, animations etc.)
  Ref<const HeadType> _headType;
  //! head object (mimics etc.)
  Head _head;

  float _x, _y, _w, _h, _z;

  float _rotateX; // position of mouse when rotation started
  float _rotation; // total rotation

  // background color
  PackedColor _bgColor;
  // border color
  PackedColor _borderColor;

  //@{
  //! animation implementation
  UITime _animStart;
  Ref<AnimationRT> _animation;
  //@}

  //@{
  //! animation speed
  float _animSpeed;
  //@}
  UITime _lastSimulation;

public:
  //! constructor
  /*!
  Used when control is created by resource template.
  \param parent control container by which this control is owned
  \param idc id of control
  \param cls resource template
  */
  CPlayerPreview(ControlsContainer *parent, int idc, ParamEntryPar cls, Person *person);

  virtual void OnDraw(float alpha) override;
  virtual void OnDrawBackGround(UIViewport *vp, float alpha) override;

  virtual bool PrepareShapeDrawMatrices(Matrix4Array &matrices, const ObjectVisualState &vs, LODShape *shape, int level) override;

  virtual bool IsInside(float x, float y) const override;

  virtual void OnLButtonDown(float x, float y) override;
  virtual void OnLButtonUp(float x, float y) override;
  virtual void OnMouseMove(float x, float y, bool active = true) override;
  virtual void OnLButtonDblClick(float x, float y) override;

  void AnimateMatrix(Matrix4 &matrix, SkeletonIndex boneIndex);
};

CPlayerPreview::CPlayerPreview(ControlsContainer *parent, int idc, ParamEntryPar cls, Person *person)
  : base(parent, idc, cls), _person(person)
{
  _x = cls >> "x";
  _y = cls >> "y";
  _w = cls >> "w";
  _h = cls >> "h";
  _z = cls >> "z";

  _bgColor = GetPackedColor(cls >> "colorBackground");
  _borderColor = GetPackedColor(cls >> "colorBorder");

  _animSpeed = cls >> "animSpeed";
  _animStart = Glob.uiTime;

  _lastSimulation = Glob.uiTime;

  _rotation = 0;

  if (person) _shape = person->GetShape();
  else _shape = NULL;

  if (_shape)
  {
    _proxies.Init(_shape);
    _proxies.InitLevel(_shape, 0);

    ConstParamEntryPtr entry = cls.FindEntry("animation");
    if (entry)
    {
      AnimationRTName name;
      name.name = GetAnimationName(*entry);
      name.skeleton = _shape->LoadSkeletonFromSource();
      name.reversed = false;
      name.streamingEnabled = false;
      if (name.skeleton)
      {
        _animation = new AnimationRT(name);
        _animation->SetLooped(true);
      }
    }

    _head._headBone = _shape->GetSkeleton()->FindBone(cls >> "boneHead");
    _head._lEye = _shape->GetSkeleton()->FindBone(cls >> "boneLEye");
    _head._rEye = _shape->GetSkeleton()->FindBone(cls >> "boneREye");
    _head._lEyelidUp = _shape->GetSkeleton()->FindBone(cls >> "boneLEyelidUp");
    _head._rEyelidUp = _shape->GetSkeleton()->FindBone(cls >> "boneREyelidUp");
    _head._lEyelidDown = _shape->GetSkeleton()->FindBone(cls >> "boneLEyelidDown");
    _head._rEyelidDown = _shape->GetSkeleton()->FindBone(cls >> "boneREyelidDown");
    _head._lPupil = _shape->GetSkeleton()->FindBone(cls >> "boneLPupil");
    _head._rPupil = _shape->GetSkeleton()->FindBone(cls >> "boneRPupil");
  }
}

void CPlayerPreview::AnimateMatrix(Matrix4 &matrix, SkeletonIndex boneIndex)
{
  if (SkeletonIndexValid(boneIndex))
  {
    BlendAnims blend;
    float phase = _animSpeed * (Glob.uiTime - _animStart);
    _animation->GetBlend(blend, phase);
    // now perform the blending
    blend.DoBoneAnim(matrix, _animation->GetSkeleton(), boneIndex);
    // this is the only place where we orthogonalize animation matrix
    // we do it because we want to make sure proxies (esp. the gun) live in orthonormal space
    matrix.Orthogonalize();
  }
}

void CPlayerPreview::OnDraw(float alpha)
{
  // center of the model
  Vector3 pos = ControlObject::Convert2DTo3D(Point2DFloat(_x + 0.5f * _w, _y + 0.95 * _h), _z);
  // scale
  Camera *cam = GScene->GetCamera();
  AspectSettings as;
  GEngine->GetAspectSettings(as);
  float scale = _h * (cam->Top() * (as.uiBottomRightY - as.uiTopLeftY)) * _z;
    
  Matrix4 transform(NoInit);
  transform.SetUpAndDirection(VUp, -VForward);
  transform.SetScale(scale);
  transform.SetPosition(pos);

  // rotation
  Matrix3 rot(MRotationY, _rotation);
  Matrix3 orient = rot * transform.Orientation();
  transform.SetOrientation(orient);

  SetTransform(transform);

  base::OnDraw(alpha);

  if (!_person) return;

  // drawing of proxies
  int level = 0; // level of the character
  int pLevel = 0; // level of the proxies
  float dist2 = 1.0f; // distance of the proxies

  int n = _proxies.GetProxyCountForLevel(_shape, level);
  for (int i=0; i<n; i++)
  {
    const ProxyObjectTyped &proxy = _proxies.GetProxyForLevel(_shape, level, i);

    // Retrieve the proxy object
    Object *proxyObj = proxy.obj;
    if (!proxyObj) continue;

    const EntityType *proxyType = proxyObj->GetEntityType();
    if (!proxyType) continue;

    Ref<Object> object;
    InventoryItem *item = NULL;

#if _ENABLE_NEWHEAD
    if (strcmp(proxyType->_simName, "ProxySubpart") == 0)
    {
      // head
      HeadObject *head = _person->CreateHead();
      _headType = head->Type();
      object = head;
    }
    else
#endif // _ENABLE_NEWHEAD
    {
      InventorySlotId slot = proxyType->GetInventorySlot();
      if (slot >= 0 && slot < NInventorySlotId)
      {
        item = _person->ToDrawInSlot(slot);
        object = item;
      }
    }

    LODShape *shape = object ? object->GetShape() : NULL;
    if (shape)
    {
      // we want animate object using this
      Ref<SortObject> oi = new SortObject();
      oi->SetObject(object);
      oi->SetRootObject(this);
      if (GetShape() && shape->GetSkeletonSource() == GetShape()->GetSkeletonSource())
        oi->SetParentObject(this);

      Matrix4 pTransform = proxyObj->RenderVisualState().Transform();
      SkeletonIndex boneIndex = _proxies.GetProxyBoneForLevel(_shape, level, i);
      AnimateMatrix(pTransform, boneIndex);
      // Pose anchor in the paperdoll model space. The second shoulder weapon
      // below uses the very same composition as the world rendering.
      Matrix4 animatedProxy = pTransform;
      pTransform = GetFrameBase() * pTransform;

      if (shape->Remarks() & ShapeReversed)
      {
        Matrix4 swapM(MScale, -1, +1, -1);
        pTransform = pTransform * swapM;
      }

      // Set UI specific flags
      if (item)
      {
        item->InsideUIDraw(true);

        Color color(1.0f, 1.0f, 1.0f, 1.0f);
        DisplayInventory *display = dynamic_cast<DisplayInventory *>(_parent.GetLink());
        if (display)
        {
          if (display->GetDraggedItem() == item) color = ColorItemDragged;
          else if (display->GetSwappedItem() == item) color = ColorItemSwapped;
        }
        color.SetA(alpha * color.A());
        GEngine->SetInstanceInfo(-1, color, 1.0f);
      }
      else object->GetShape()->OrSpecial(DisableSun);

      object->Draw(-1, pLevel, ClipAll, DrawParameters::_noShadow, InstanceParameters::_default, dist2, pTransform, oi
#if _HELISIM_RENDER_TO_TEXTURE
        , GScene->GetDrawObjectIndex()
#endif    
        );

      // Restore flags
      if (item)
      {
        item->InsideUIDraw(false);
        GEngine->SetInstanceInfo(-1, HWhite, 1);
      }
      else object->GetShape()->AndSpecial(~DisableSun);

      // for an inventory item, draw attachments recursively
      Object *parentObject = oi->GetParentObject();
      if (item) item->DrawProxies(pLevel, dist2, pTransform, [&parentObject](Matrix4 &matrix, SkeletonIndex boneIndex) -> bool
      {
        if (!parentObject) return false;

        CPlayerPreview *ctrl = static_cast<CPlayerPreview *>(parentObject);
        ctrl->AnimateMatrix(matrix, boneIndex);
        return true;
      });

      // The character model has a single shoulder proxy, so as in the world
      // rendering the second shoulder weapon must be drawn manually at the
      // same proxy, mirrored to the right half of the back.
      if (proxyType->GetInventorySlot() == ISShoulder)
      {
        InventoryItem *secondItem = _person->GetInSlot(ISShoulder2);
        LODShape *secondShape = secondItem ? secondItem->GetShape() : NULL;
        if (secondShape && secondItem != item)
        {
          // Same composition as Man::PrepareProxiesForDrawing: the offset is
          // applied to the pose anchor in model space, then the paperdoll
          // frame places the result.
          Matrix4 oppositeSide(MTranslation, Vector3(+0.32f, 0.0f, -0.10f));
          Matrix4 secondTransform = GetFrameBase() * oppositeSide * animatedProxy;

          if (secondShape->Remarks() & ShapeReversed)
          {
            Matrix4 swapM(MScale, -1, +1, -1);
            secondTransform = secondTransform * swapM;
          }

          Ref<SortObject> oi2 = new SortObject();
          oi2->SetObject(secondItem);
          oi2->SetRootObject(this);
          if (GetShape() && secondShape->GetSkeletonSource() == GetShape()->GetSkeletonSource())
            oi2->SetParentObject(this);

          secondItem->InsideUIDraw(true);

          Color color(1.0f, 1.0f, 1.0f, 1.0f);
          DisplayInventory *display = dynamic_cast<DisplayInventory *>(_parent.GetLink());
          if (display)
          {
            if (display->GetDraggedItem() == secondItem) color = ColorItemDragged;
            else if (display->GetSwappedItem() == secondItem) color = ColorItemSwapped;
          }
          color.SetA(alpha * color.A());
          GEngine->SetInstanceInfo(-1, color, 1.0f);

          secondItem->Draw(-1, pLevel, ClipAll, DrawParameters::_noShadow, InstanceParameters::_default, dist2, secondTransform, oi2
#if _HELISIM_RENDER_TO_TEXTURE
            , GScene->GetDrawObjectIndex()
#endif
            );

          secondItem->InsideUIDraw(false);
          GEngine->SetInstanceInfo(-1, HWhite, 1);

          Object *parentObject2 = oi2->GetParentObject();
          secondItem->DrawProxies(pLevel, dist2, secondTransform, [&parentObject2](Matrix4 &matrix, SkeletonIndex boneIndex) -> bool
          {
            if (!parentObject2) return false;

            CPlayerPreview *ctrl = static_cast<CPlayerPreview *>(parentObject2);
            ctrl->AnimateMatrix(matrix, boneIndex);
            return true;
          });
        }
      }
    }
  }
}

void CPlayerPreview::OnDrawBackGround(UIViewport *vp, float alpha)
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();
  float xx = CX(_x);
  float yy = CY(_y);
  float ww = CW(_w);
  float hh = CH(_h);

  // draw background
  Draw2DParsExt pars;
  pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  pars.SetColor(_bgColor);
  pars.Init();
  vp->Draw2D(pars, Rect2DPixel(xx, yy, ww, hh));

  // draw border
  vp->DrawLine(Line2DPixel(xx, yy, xx + ww, yy), _borderColor, _borderColor);
  vp->DrawLine(Line2DPixel(xx, yy, xx, yy + hh), _borderColor, _borderColor);
  vp->DrawLine(Line2DPixel(xx + ww, yy, xx + ww, yy + hh), _borderColor, _borderColor);
  vp->DrawLine(Line2DPixel(xx, yy + hh, xx + ww, yy + hh), _borderColor, _borderColor);
}

bool CPlayerPreview::PrepareShapeDrawMatrices(Matrix4Array &matrices, const ObjectVisualState &vs, LODShape *shape, int level)
{
  float phase = _animSpeed * (Glob.uiTime - _animStart);
  if (phase >= 1.0)
  {
    phase = 0;
    _animStart = Glob.uiTime;
  }
  if (_animation)
  {
    const Shape *shapeL = shape->GetLevelLocked(level);
    _animation->PrepareMatrices(matrices, phase, 1.0f, shapeL, shape->GetSkeleton());
  }

  if (_headType)
  {
    // Get the head bones
    if (_head._headBone >= 0)
    {
      const Shape *shapeL = shape->GetLevelLocked(level);
      const SubSkeletonIndexSet &ssis = shapeL->GetSubSkeletonIndexFromSkeletonIndex(_head._headBone);
      if (ssis.Size() > 0)
      {
        // Get the head transformation matrix
        Matrix4Par headOrigin = matrices[GetSubSkeletonIndex(ssis[0])];

        // Go through all the bones with "head" parent and set them the head bone
        // This is necessary, because some bones need not to be set during the grimace process
        // and we need them to be on proper place
        for (int i = 0; i < shapeL->GetSubSkeletonSize(); i++)
        {
          SubSkeletonIndex ssi;
          SetSubSkeletonIndex(ssi, i);
          SkeletonIndex si = shapeL->GetSkeletonIndexFromSubSkeletonIndex(ssi);
          if (shape->GetSkeleton()->GetParent(si) == _head._headBone)
          {
            matrices[i] = headOrigin;
          }
        }

        int grimace = _headType->GetGrimaceIndex("safe");
        _head.SetGrimace(grimace);

        UITime time = Glob.uiTime;
        _head.Simulate(time - _lastSimulation, SimulateVisibleNear, false);
        _lastSimulation = time;

        float eyeAccom = GScene->MainLight()->GetEyeAdaptMin();
        /*
        _headType->PrepareGrimaceMatrices(matrices, _head, _headType, shapeL, headOrigin, VForward, eyeAccom, grimace, grimace, 0);
        */
        if (_head._dstGrimace >= 0 && _head._srcGrimace >= 0)
          _headType->PrepareGrimaceMatrices(matrices, _head, _headType, shapeL, headOrigin, VForward, eyeAccom, _head._dstGrimace, _head._srcGrimace, _head._factorGrimace);
      }
    }
  }

  return true;
}

bool CPlayerPreview::IsInside(float x, float y) const
{
  return x >= _x && x <= _x + _w && y >= _y && y <= _y + _h;
}

void CPlayerPreview::OnLButtonDown(float x, float y)
{
  _moving = true;
  _rotateX = x;
}

void CPlayerPreview::OnLButtonUp(float x, float y)
{
  _moving = false;
}

void CPlayerPreview::OnMouseMove(float x, float y, bool active)
{
  // rotation speed
  static const float speed = 8.0f;

  // hotfix when OnLButtonUp was not called
  if (!GInput.mouseL) return; // maybe drag & drop cancel reaction would be needed

  if (_moving)
  {
    float diff = x - _rotateX;
    _rotation += speed * diff;
    if (_rotation > H_PI) _rotation -= H_2PI;
    else if (_rotation < -H_PI) _rotation += H_2PI;
    _rotateX = x;
  }
}

void CPlayerPreview::OnLButtonDblClick(float x, float y)
{
}

class CItemPreview : public ControlObject
{
private:
  typedef ControlObject base;

  OLinkPermO(InventoryItem) _item;

  float _x, _y, _w, _h, _z;

  float _rotateX; // position of mouse when rotation started
  float _rotation; // total rotation

  // background color
  PackedColor _bgColor;
  // border color
  PackedColor _borderColor;

public:
  CItemPreview(ControlsContainer *parent, int idc, ParamEntryPar cls, InventoryItem *item);

  virtual void OnDraw(float alpha) override;
  virtual void OnDrawBackGround(UIViewport *vp, float alpha) override;

  virtual bool IsInside(float x, float y) const override;

  virtual void OnLButtonDown(float x, float y) override;
  virtual void OnLButtonUp(float x, float y) override;
  virtual void OnMouseMove(float x, float y, bool active = true) override;
};

CItemPreview::CItemPreview(ControlsContainer *parent, int idc, ParamEntryPar cls, InventoryItem *item)
  : base(parent, idc, cls), _item(item)
{
  _x = cls >> "x";
  _y = cls >> "y";
  _w = cls >> "w";
  _h = cls >> "h";
  _z = cls >> "z";

  _bgColor = GetPackedColor(cls >> "colorBackground");
  _borderColor = GetPackedColor(cls >> "colorBorder");

  _rotation = 0;
}

void CItemPreview::OnDraw(float alpha)
{
  Matrix3 rot(MRotationY, _rotation);

  Color color(0, 0, 0, -1.0f);
  CItemBase::DrawItem(_item, _x, _y, _w, _h, _z, alpha, color, &rot);
}

void CItemPreview::OnDrawBackGround(UIViewport *vp, float alpha)
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();
  float xx = CX(_x);
  float yy = CY(_y);
  float ww = CW(_w);
  float hh = CH(_h);

  // draw background
  Draw2DParsExt pars;
  pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  pars.SetColor(_bgColor);
  pars.Init();
  vp->Draw2D(pars, Rect2DPixel(xx, yy, ww, hh));

  // draw border
  vp->DrawLine(Line2DPixel(xx, yy, xx + ww, yy), _borderColor, _borderColor);
  vp->DrawLine(Line2DPixel(xx, yy, xx, yy + hh), _borderColor, _borderColor);
  vp->DrawLine(Line2DPixel(xx + ww, yy, xx + ww, yy + hh), _borderColor, _borderColor);
  vp->DrawLine(Line2DPixel(xx, yy + hh, xx + ww, yy + hh), _borderColor, _borderColor);
}

bool CItemPreview::IsInside(float x, float y) const
{
  return x >= _x && x <= _x + _w && y >= _y && y <= _y + _h;
}

void CItemPreview::OnLButtonDown(float x, float y)
{
  _moving = true;
  _rotateX = x;
}

void CItemPreview::OnLButtonUp(float x, float y)
{
  _moving = false;
}

void CItemPreview::OnMouseMove(float x, float y, bool active)
{
  // rotation speed
  static const float speed = 8.0f;

  // hotfix when OnLButtonUp was not called
  if (!GInput.mouseL) return; // maybe drag & drop cancel reaction would be needed

  if (_moving)
  {
    float diff = x - _rotateX;
    _rotation += speed * diff;
    if (_rotation > H_PI) _rotation -= H_2PI;
    else if (_rotation < -H_PI) _rotation += H_2PI;
    _rotateX = x;
  }
}

// SourceDayZ: find the closest vehicle cargo so pressing G next to a vehicle
// opens its 5x100 grid even when the action system does not provide a Gear
// action (mod vehicles without itemsCargoSize/supply point).
class FindNearestSupplyVehicle
{
public:
  struct Result
  {
    OLink(EntityAI) best;
    float bestDist2;
  };

  Result *_result;
  Vector3 _pos;

  FindNearestSupplyVehicle(Result &result, Vector3Par pos)
    : _result(&result)
  {
    _pos = pos;
  }

  bool operator()(Entity *entity) const
  {
    EntityAI *ai = dyn_cast<EntityAI>(entity);
    if (!ai) return false;
    const EntityAIType *type = ai->GetType();
    if (!type) return false;
    if (!type->IsKindOf("LandVehicle") && !type->IsKindOf("Air") && !type->IsKindOf("Ship")) return false;
    if (!ai->GetSupply()) return false;

    float dist2 = ai->FutureVisualState().Position().DistanceXZ2(_pos);
    if (dist2 < _result->bestDist2)
    {
      _result->bestDist2 = dist2;
      _result->best = ai;
    }
    return false;
  }
};

Display *CreateInventoryDialog(ControlsContainer *parent, AIBrain *me, EntityAI *source)
{
  // SourceDayZ: G while standing next to a vehicle (or sitting inside it) opens
  // the vehicle cargo in the VICINITY panel instead of an empty player gear.
  if (!source && me)
  {
    EntityAI *veh = me->GetVehicleIn();
    if (!veh)
    {
      Person *person = me->GetPerson();
      if (person)
      {
        FindNearestSupplyVehicle::Result result;
        result.bestDist2 = Square(12.0f);
        FindNearestSupplyVehicle find(result, person->FutureVisualState().Position());
        GWorld->ForEachVehicle(find);
        veh = result.best;
      }
    }
    if (veh && veh->GetSupply())
    {
      source = veh;
      RptF("SDZ open nearby vehicle: %s", cc_cast(veh->GetType() ? veh->GetType()->GetName() : "?"));
    }
  }

  return new DisplayInventory(parent, me, source);
}

void CItemBase::DrawTooltip(UIViewport *vp, float x, float y)
{
  const InventoryItem *item = GetItemInfo(x, y)._item;
  if (item)
  {
    DisplayInventory *disp = dynamic_cast<DisplayInventory *>(_parent.GetLink());
    if (disp) disp->DrawTooltip(vp, x, y, item);
  }
}

intptr_t CItemBase::GetTooltipArea(float x, float y)
{
  return reinterpret_cast<intptr_t>(GetItemInfo(x, y)._item);
}

bool IsInventoryOpened()
{
  return DisplayInventory::IsOpened();
}

class DisplayInspect : public Display
{
private:
  Ref<InventoryItem> _item;

public:
  DisplayInspect(ControlsContainer *parent, InventoryItem *item);

  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  ControlObject *OnCreateObject(int type, int idc, ParamEntryPar cls);
};

DisplayInspect::DisplayInspect(ControlsContainer *parent, InventoryItem *item)
  : Display(parent), _item(item)
{
  Load("RscDisplayInspect");
}

Control *DisplayInspect::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_INSPECT_TITLE:
    {
      ITextContainer *text = GetTextContainer(ctrl);
      if (text) text->SetText(_item->GetDisplayName());
    }
    break;
  case IDC_INSPECT_DESCRIPTION:
    {
      CStructuredText *text = GetStructuredText(ctrl);
      if (text)
      {
        Ref<INode> node = _item->GetTooltip();
        if (node) text->SetText(node);
      }
    }
    break;
  }
  return ctrl;
}

ControlObject *DisplayInspect::OnCreateObject(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
  case IDC_INSPECT_PREVIEW:
    return new CItemPreview(this, idc, cls, _item);
  default:
    return Display::OnCreateObject(type, idc, cls);
  }
}

int DisplayInventory::_instances = 0;
static DisplayInventory *GActiveInventoryDisplay = NULL;
static bool GInventorySpaceKeyDown = false;

DisplayInventory::DisplayInventory(ControlsContainer *parent, AIBrain *me, EntityAI *source)
                         : Display(parent),  _unit(me), _targetContainer(source), _dragRotated(false)
{
  Load("RscDisplayInventory");

  ParamEntryVal cls = Pars >> "RscDisplayInventory";
  _dragGridW = cls >> "dragGridW";
  _dragGridH = cls >> "dragGridH";
  _dragZ = cls >> "dragZ";
  _dragColorNone = GetPackedColor(cls >> "dragColorNone");
  _dragColorMove = GetPackedColor(cls >> "dragColorMove");
  _dragColorCraft = GetPackedColor(cls >> "dragColorCraft");
  _dragColorSwap = GetPackedColor(cls >> "dragColorSwap");

  _tooltipHeight = cls >> "tooltipHeight";
  _tooltipImageWidth = cls >> "tooltipImageWidth";
  _tooltipTextWidth = cls >> "tooltipTextWidth";
  _tooltipTextSize = cls >> "tooltipTextSize";
  _tooltipBgColor = GetPackedColor(cls >> "tooltipBgColor");
  _tooltipLineColor = GetPackedColor(cls >> "tooltipLineColor");

  ITextContainer *control = GetTextContainer(GetCtrl(IDC_INVENTORY_UNIT));
  if (control)
  {
    RString name;
    int rank = RankUndefined;
    if (me->GetPerson())
    { 
      name = me->GetPerson()->GetPersonName();
      rank = me->GetPerson()->GetRank();
    }
    if (me->GetSide() != TCivilian) control->SetText(LocalizeString(IDS_SHORT_PRIVATE + rank) + " " +  name);
    else control->SetText(name);
  }

  // OnUnitChanged();
  _instances++;
  GActiveInventoryDisplay = this;
}

DisplayInventory::~DisplayInventory()
{
  _instances--;
  if (GActiveInventoryDisplay == this)
  {
    GActiveInventoryDisplay = NULL;
    GInventorySpaceKeyDown = false;
  }
  _dragItem = nullptr;
  _swapItem = nullptr;
  _dragRotated = false;
}

bool DisplayInventory::OnKeyDown(int dikCode)
{
  if (dikCode != DIK_SPACE) return Display::OnKeyDown(dikCode);

  InventoryItem *item = _dragItem;
  Person *person = _unit ? _unit->GetPerson() : NULL;
  if (!item)
  {
    const float x = 0.5f + GInput.cursorX * 0.5f;
    const float y = 0.5f + GInput.cursorY * 0.5f;
    const int itemControls[] = {IDC_INVENTORY_HANDS, IDC_INVENTORY_CONTAINER, IDC_INVENTORY_CONTENT};
    for (int i = 0; i < lenof(itemControls) && !item; i++)
    {
      CItemBase *control = dynamic_cast<CItemBase *>(GetCtrl(itemControls[i]));
      if (control && control->IsInside(x, y))
      {
        item = control->GetItemInfo(x, y)._item;
      }
    }
  }
  const bool itemInHands = person && item && person->GetItemInHands() == item;
  if (_dragItem)
  {
    if (!itemInHands && _dragItem->Type() && _dragItem->Type()->GetItemWidth() != _dragItem->Type()->GetItemHeight())
      _dragRotated = !_dragRotated;
  }
  else if (!itemInHands && item && item->Type() && item->GetInventoryWidth() != item->GetInventoryHeight())
    CItemBase::RotateInventoryItem(person, item);
  return true;
}

bool HandleInventorySpaceKey()
{
  if (GActiveInventoryDisplay == NULL || !DisplayInventory::IsOpened()) return false;
  if (!GInventorySpaceKeyDown)
  {
    GInventorySpaceKeyDown = true;
    GActiveInventoryDisplay->OnKeyDown(DIK_SPACE);
  }
  return true;
}

void ReleaseInventorySpaceKey()
{
  GInventorySpaceKeyDown = false;
}

Control *DisplayInventory::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{

  // Ignore the legacy hands-drop button and its red background control.
  // Dropping the hands item is handled by inventory drag-and-drop instead.
  if (idc == 198 || stricmp(cls.GetName(), "ButtonBackground2") == 0) return NULL;

  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_INVENTORY_SOURCE_NAME:
    {
	  //LocalizeString(IDS_UI_INVENTORY_VICINITY)
      ITextContainer *text = dynamic_cast<ITextContainer *>(ctrl);
      if(_targetContainer)
	  {
			if (_targetContainer->GetType() && _targetContainer->GetType()->IsPerson())
			{
				text->SetText(LocalizeString(IDS_UI_INVENTORY_BODY));
			}
			else
			{
				text->SetText(LocalizeString(IDS_UI_INVENTORY_CONTAINER));
			}
	  }
      break;
    }
  case IDC_INVENTORY_CONTENT_HEADER:
  case IDC_INVENTORY_SOLDIER_HEADER:
  case IDC_INVENTORY_HANDS_HEADER:
    ctrl->EnableCtrl(true); // handle drop events
    break;
  }
  return ctrl;
}

ControlObject *DisplayInventory::OnCreateObject(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
  case IDC_INVENTORY_SOLDIER:
    return new CPlayerPreview(this, idc, cls, _unit ? _unit->GetPerson() : NULL);
  case IDC_INVENTORY_SHORTCUT:
    return new CItemShortcut(this, idc, cls, false);
  case IDC_INVENTORY_CONTAINER:
    return new CItemContainer(this, idc, cls, _unit ? _unit->GetPerson() : NULL, _targetContainer);
  case IDC_INVENTORY_CONTENT:
    return new CItemContent(this, idc, cls, _unit ? _unit->GetPerson() : NULL);
  case IDC_INVENTORY_HANDS:
    return new CItemInHands(this, idc, cls, _unit ? _unit->GetPerson() : NULL);
    
  }
  return Display::OnCreateObject(type, idc, cls);
}

/*!
\patch 5153 Date 4/12/2007 by Jirka
- Fixed: Gear dialog is closed when player dies
*/

void DisplayInventory::OnSimulate(EntityAI *vehicle)
{
  extern bool GUAGearActive;
  if (GUAGearActive)
  {
    GInput.GetActionToDo(UAGear); GUAGearActive = false;
    Exit(IDC_CANCEL);
    return;
  }

  // close the dialog when supply source is out of range
  EntityAI *source = _targetContainer;
  EntityAI *veh = _unit ? _unit->GetVehicle() : NULL;
  if (!veh)
  {
    Exit(IDC_CANCEL);
    return;
  }
  else if (source)
  {
    // check distance
    const Entity::VisualState &vs = source->FutureVisualState();
    Vector3 supplyPos = source->AimingPosition(vs);
    int index = source->GetType()->GetSupplyPointIndex();
    if (index >= 0)
    {
      const LODShape *shape = source->GetShape();
      if (shape)
      {
        int mem = shape->FindMemoryLevel();
        if (mem >= 0)
        {
          supplyPos = vs.PositionModelToWorld(source->AnimatePoint(vs, mem, index));
        }
      }
    }

    // if item is in scene, then exit when not close
    if (source->IsInLandscape())
    {
      const float supplyRadius = source->GetType()->GetSupplyRadius();
      const float dist2 = veh->FutureVisualState().Position().Distance2(supplyPos);
      const float maxDistance = floatMin(1.5f * (veh->CollisionSize() * 1.2f + 1.0f + supplyRadius), 40.0f);
      if (dist2 > Square(maxDistance))
      {
        Exit(IDC_CANCEL);
        return;
      }
    }
  }

  Display::OnSimulate(vehicle);

  // in MP, check if name of the units did not changed
  if (GWorld->GetMode() == GModeNetware)
  {
    //// patch the names of the units
    //for (int i=0; i<_weaponsInfo._weapons.Size(); i++)
    //{
    //  UnitWeaponsInfo &info = _weaponsInfo._weapons[i];
    //  AIBrain *unit = info.unit;
    //  if (!unit) continue;
    //  Person *person = unit->GetPerson();
    //  if (!person) continue;
    //  info.name = person->GetPersonName();
    //}

    // TODO: update the name
  }
  //mantain weapon info displayed
  GWorld->UI()->ShowWeaponInfo();

  // if in briefing, preload data for a mission
  bool inGame = true;// _pool.IsInGame();
  if (!inGame)
  {
    GWorld->PreloadAroundCamera(0.030f); // keep reasonable framerate
  }
  if (GWorld->GetMode() == GModeNetware)
  {
    if (inGame)
    {
      if (GetNetworkManager().GetServerState() != NSSPlaying) Exit(IDC_CANCEL);
    }
  }
  if (inGame)
  {
    // close when the unit is dead
    if (!_unit || !_unit->LSIsAlive())
    {
      Exit(IDC_CANCEL);
      return;
    }
  }
}

// check if item can be removed from its owner
static bool CanRemoveItem(const InventoryItem *item)
{
  Entity *parent = item->GetHierarchyParent();
  if (!parent) return true;
  return parent->CanRemoveOwnedItem(item);
}

void DisplayInventory::OnButtonDblClick(IControl *ctrl)
{
  if (ctrl && (ctrl->IDC() == IDC_INVENTORY_CONTAINER || ctrl->IDC() == IDC_INVENTORY_CONTENT))
  {
    CItemBase *src = dynamic_cast<CItemBase *>(ctrl);
    if (src)
    {
      float x = 0.5 + GInput.cursorX * 0.5;
      float y = 0.5 + GInput.cursorY * 0.5;
      DragItemInfo info = src->GetItemInfo(x, y);
      if (info._item && CanRemoveItem(info._item))
      {
        Person *person = _unit ? _unit->GetPerson() : NULL;
        if (person && person->GetItemInHands() != info._item && CItemBase::CanMoveItem(person, -1, info._item))
        {
          CItemBase::MoveItem(person, -1, info._item);
        }
      }
    }
  }
}

void DisplayInventory::OnDrag(IControl *ctrl)
{
  Display::OnDrag(ctrl);

  _dragItem = nullptr;
  _swapItem = nullptr;
  _dragRotated = false;

  CItemBase *src = dynamic_cast<CItemBase *>(ctrl);
  if (src)
  {
    // object to be dragged
    DragItemInfo info = src->GetDragItemInfo();
    Person *person = _unit ? _unit->GetPerson() : NULL;
    const bool isHandsItem = src->IDC() == IDC_INVENTORY_HANDS && person &&
      person->GetItemInHands() == info._item;
    if (info._item && (CanRemoveItem(info._item) || isHandsItem))
    {
      _dragItem = info._item;
      _dragRotated = info._item->IsInventoryRotated();
      //_dragOffsetX = info._offsetX;
      //_dragOffsetY = info._offsetY;
      // always drag the top left corner
      _dragOffsetX = 0;
      _dragOffsetY = 0;
      //RptF("### OnDrag: %x %s [%.2f,%.2f]", ctrl, cc_cast(_dragItem->GetName()),_dragOffsetX,_dragOffsetY);
    }
  }
}

bool CanSwapItems(InventoryItem *item1, InventoryItem *item2)
{
  if (item1 == item2) return false;

  Entity *parent1 = item1->GetHierarchyParent();
  Entity *parent2 = item2->GetHierarchyParent();
  if (!parent1 && !parent2) return false;
  // cannot switch container with a contained item
  if (parent1 == item2) return false;
  if (parent2 == item1) return false;

  if (parent1 && parent1->CanReplaceOwnedItem(item1, item2) < 0) return false;
  if (parent2 && parent2->CanReplaceOwnedItem(item2, item1) < 0) return false;

  return true;
}

bool SwapItems(InventoryItem *item1, InventoryItem *item2, Person *owner)
{
  if (item1 == item2) return false;

  EntityAI *parent1 = dyn_cast<EntityAI>(item1->GetHierarchyParent());
  EntityAI *parent2 = dyn_cast<EntityAI>(item2->GetHierarchyParent());
  if (!parent1 && !parent2) return false;

  // cannot switch container with a contained item
  if (parent1 == item2) return false;
  if (parent2 == item1) return false;

  int index1 = -1, index2 = -1;
  if (parent1)
  {
    index1 = parent1->CanReplaceOwnedItem(item1, item2);
    if (index1 < 0) return false;
  }
  if (parent2)
  {
    index2 = parent2->CanReplaceOwnedItem(item2, item1);
    if (index2 < 0) return false;
  }

  if (parent1) parent1->RemoveOwnedItem(item1);
  if (parent2) parent2->RemoveOwnedItem(item2);

  if (parent1) parent1->MoveItem(index1, item2);
  else
  {
    if (owner && owner->GetItemInHands() == item2) owner->RemoveItemInHands();
    Vector3Val pos = owner->FutureVisualState().Position();
    Vector3Val dir = owner->FutureVisualState().Direction();
    item2->ItemDrop(nullptr, pos, dir);
  }

  if (parent2 == parent1 && !parent2->CanMoveItem(index2, item1))
  {
    // hotfix - index2 is already occupied by item2
    // try to move it elsewhere or drop it on ground in worst case
    if (parent2->CanMoveItem(-1, item1)) parent2->MoveItem(-1, item1);
    else parent2 = nullptr;
  }
  else if (parent2)
  {
    parent2->MoveItem(index2, item1);
  }

  if (!parent2)
  {
    if (owner && owner->GetItemInHands() == item1) owner->RemoveItemInHands();
    Vector3Val pos = owner->FutureVisualState().Position();
    Vector3Val dir = owner->FutureVisualState().Direction();
    item1->ItemDrop(nullptr, pos, dir);
  }

  DoAssert(item1->GetHierarchyParent() == parent2);
  DoAssert(item2->GetHierarchyParent() == parent1);

  return true;
}

class InventoryRotationPreviewScope
{
private:
  InventoryItem *_item;
  bool _original;
  Entity *_originalParent;
public:
  InventoryRotationPreviewScope(InventoryItem *item, bool rotated)
    : _item(item), _original(item ? item->IsInventoryRotated() : false),
      _originalParent(item ? item->GetHierarchyParent() : NULL)
  {
    if (_item) _item->SetInventoryRotated(rotated);
  }
  ~InventoryRotationPreviewScope()
  {
    if (_item && _item->GetHierarchyParent() == _originalParent)
      _item->SetInventoryRotated(_original);
  }
};

unsigned char DisplayInventory::OnDragging(IControl *dragCtrl,  IControl *dropCtrl,  float x, float y)
{
  _swapItem = nullptr; // set it when swapping

  InventoryItem *item = _dragItem;
  if (!item) return DANone;

  CItemBase *src = dynamic_cast<CItemBase *>(dragCtrl);
  CItemBase *dst = dynamic_cast<CItemBase *>(dropCtrl);
  if (!src) return DANone;

  InventoryRotationPreviewScope rotationPreview(item, _dragRotated);

  Person *person = _unit ? _unit->GetPerson() : NULL;
  const bool handsToVicinity = src->IDC() == IDC_INVENTORY_HANDS &&
    dropCtrl && dropCtrl->IDC() == IDC_INVENTORY_CONTAINER;
  const bool handsToInventory = src->IDC() == IDC_INVENTORY_HANDS &&
    ((dst && dst->IDC() == IDC_INVENTORY_CONTENT) ||
     (dropCtrl && dropCtrl->IDC() == IDC_INVENTORY_CONTENT_HEADER));
  const bool handsToGround = src->IDC() == IDC_INVENTORY_HANDS && dropCtrl == NULL;
  bool canRemove = !person || person->GetItemInHands() != item || handsToInventory || handsToVicinity || handsToGround;

  int srcShortcut = src->GetShortcutIndex();

  if (dst)
  {
    int dstShortcut = dst->GetShortcutIndex();

    // check if combination is possible
    if (srcShortcut < 0 && dstShortcut < 0)
    {
      if (person)
      {
        DragItemInfo info = dst->GetItemInfo(x, y);
        if (info._item && info._item != item)
        {
          // look for recipes
          // TODO: client-server
          bool found = Recipes.Collect(person, item, info._item,
            [this, item, &info](const Recipe *recipe) -> bool 
            { 
              return true;
            });
          if (found)
          {
            _swapItem = info._item;
            return DACraft;
          }

          bool canSwap = CanSwapItems(item, info._item);
          bool canAddOwnedItem = canRemove && info._item->CanAddOwnedItem(item); 
          if (canSwap && canAddOwnedItem)
          {
            _swapItem = info._item;
            return DACraft;
          }
          if (canSwap)
          {
            _swapItem = info._item;
            return DASwap;
          }
          if (canAddOwnedItem)
          {
            _swapItem = info._item;
            return DAMove;
          }
        }
      }
    }

    if (dst->CanAddItem(item, x, y))
    {
      if (dstShortcut >= 0)
      {
        _swapItem = static_cast<CItemShortcut *>(dst)->GetItem();
        return DAMove;
      }

      // shortcut can be moved to other shortcut only
      if (srcShortcut < 0 && canRemove)
        return DAMove;
    }
    
    return DANone;
  }
  else if (dropCtrl && (dropCtrl->IDC() == IDC_INVENTORY_SOLDIER || dropCtrl->IDC() == IDC_INVENTORY_SOLDIER_HEADER))
  {
    if (src && (src->IDC() == IDC_INVENTORY_CONTAINER || src->IDC() == IDC_INVENTORY_CONTENT))
    {
      if (person && person->GetItemInHands() != item)
      {
        // try to swap with the current item
        const InventoryItemType *type = item->Type();
        if (type)
        {
          InventorySlotId slot = type->GetInventorySlot();
          InventoryItem *oldItem = person->GetInSlot(slot);
          if (oldItem && CanSwapItems(item, oldItem))
          {
            _swapItem = oldItem;
            return DASwap;
          }
        }

        if (person->CanAddItem(item)) return DAMove;
      }
    }
    return DANone;
  }
  else if (dropCtrl && dropCtrl->IDC() == IDC_INVENTORY_CONTENT_HEADER)
  {
    if (srcShortcut < 0 && canRemove) 
    {
      // redirect from the header to the control itself
      CItemBase *dst = dynamic_cast<CItemBase *>(GetCtrl(IDC_INVENTORY_CONTENT));
      if (dst && dst->CanAddItem(item, 0, 0)) return DAMove;
    }
    return DANone;
  }
  else if (dropCtrl && dropCtrl->IDC() == IDC_INVENTORY_HANDS_HEADER)
  {
    if (srcShortcut < 0 && canRemove) 
    {
      // redirect from the header to the control itself
      CItemBase *dst = dynamic_cast<CItemBase *>(GetCtrl(IDC_INVENTORY_HANDS));
      if (dst && dst->CanAddItem(item, 0, 0)) return DAMove;
    }
    return DANone;
  }
  else if (srcShortcut >= 0) // shortcut removed
  {
    return DAMove;
  }
  else if (src->IDC() == IDC_INVENTORY_CONTENT && canRemove)
  {
    return DAMove;
  }
  else if (canRemove) // dst == 0 and can remove item
  {  
    return DAMove;
  }

  return Display::OnDragging(dragCtrl, dropCtrl, x, y);
}

bool DisplayInventory::OnDrop(IControl *dragCtrl, IControl *dropCtrl, float x, float y)
{
  // whenever dropped, drag & drop is finished
  InventoryItem *item = _dragItem;
  _dragItem = nullptr;
  _swapItem = nullptr;
  if (!item) return false;

  CItemBase *src = dynamic_cast<CItemBase *>(dragCtrl);
  CItemBase *dst = dynamic_cast<CItemBase *>(dropCtrl);
  if (!src || !src->IsContained(item)) return false; // check if item still present in the source

  InventoryRotationPreviewScope rotationPreview(item, _dragRotated);

  Person *person = _unit ? _unit->GetPerson() : NULL;
  const bool handsToVicinity = src->IDC() == IDC_INVENTORY_HANDS &&
    dropCtrl && dropCtrl->IDC() == IDC_INVENTORY_CONTAINER;
  const bool handsToInventory = src->IDC() == IDC_INVENTORY_HANDS &&
    ((dst && dst->IDC() == IDC_INVENTORY_CONTENT) ||
     (dropCtrl && dropCtrl->IDC() == IDC_INVENTORY_CONTENT_HEADER));
  const bool handsToGround = src->IDC() == IDC_INVENTORY_HANDS && dropCtrl == NULL;
  bool canRemove = !person || person->GetItemInHands() != item || handsToInventory || handsToVicinity || handsToGround;

  int srcShortcut = src->GetShortcutIndex();

  if (dst)
  {
    int dstShortcut = dst->GetShortcutIndex();

    // offer the combination
    if (srcShortcut < 0 && dstShortcut < 0)
    {
      DragItemInfo info = dst->GetItemInfo(x, y);
      if (info._item && info._item != item)
      {
        Person *person = _unit ? _unit->GetPerson() : NULL;
        if (person)
        {
          bool found = Recipes.Collect(person, item, info._item,
            [this, item, &info](const Recipe *recipe) -> bool 
            { 
              return true;
            });
          if (found && CreateContextMenu(x, y, 
            IDC_INVENTORY_MENU, Pars >> "RscDisplayInventory" >> "Menu",
            CCMContextInventory(item, info._item))) return true;

          bool canSwap = CanSwapItems(item, info._item);
          bool canAddOwnedItem = canRemove && info._item->CanAddOwnedItem(item); 
          if (canSwap && canAddOwnedItem && CreateContextMenu(x, y, 
            IDC_INVENTORY_MENU, Pars >> "RscDisplayInventory" >> "Menu",
            CCMContextInventory(item, info._item))) return true;

          if (canSwap)
          {
            if (!GetNetworkManager().IsServerOwner())
            {
              // BOT client have to handle swapping
              GetNetworkManager().AskForSwapItems(item, info._item, person);
              return true;
            }
            else if (SwapItems(item, info._item, person)) return true;
          }

          if (canAddOwnedItem)
          {
            if (!GetNetworkManager().IsServerOwner())
              GetNetworkManager().AskForMoveItem(info._item, -1, item, item->IsInventoryRotated()); // ask server to execute the action
            else
            {
              // remove from the old owner
              Entity *parent = item->GetHierarchyParent();
              if (parent) DoVerify(parent->RemoveOwnedItem(item));
    
              info._item->AddOwnedItem(item);
            }
            return true;
          }
        }
      }
    }

    if (dst->CanAddItem(item, x, y))
    {
      if (srcShortcut >= 0)
      {
        // shortcut can be moved to other shortcut only
        if (dstShortcut < 0) return false;
        // order is important here, item is destroyed when shortcut is removed
        CItemBase::SetItemShortcut(dstShortcut, item);
        CItemBase::SetItemShortcut(srcShortcut, NULL);
        return true;
      }
      else if (canRemove || dstShortcut >= 0)
      {
        // do not break layout by removing item first
        dst->AddItem(item, x, y);
        return true;
      }
    }

    return false;
  }
  else if (dropCtrl && (dropCtrl->IDC() == IDC_INVENTORY_SOLDIER || dropCtrl->IDC() == IDC_INVENTORY_SOLDIER_HEADER))
  {
    if (src && (src->IDC() == IDC_INVENTORY_CONTAINER || src->IDC() == IDC_INVENTORY_CONTENT))
    {
      Person *person = _unit ? _unit->GetPerson() : NULL;
      if (person && person->GetItemInHands() != item)
      {
        // try to swap with the current item
        const InventoryItemType *type = item->Type();
        if (type)
        {
          InventorySlotId slot = type->GetInventorySlot();
          InventoryItem *oldItem = person->GetInSlot(slot);
          if (oldItem)
          {
            if (!GetNetworkManager().IsServerOwner())
            {
              // BOT client have to handle swapping
              if (CanSwapItems(item, oldItem))
              {
                GetNetworkManager().AskForSwapItems(item, oldItem, person);
                return true;
              }
            }
            else if (SwapItems(item, oldItem, person)) return true;
          }
        }

        if (CItemBase::CanMoveItem(person, -1, item))
        {
          CItemBase::MoveItem(person, -1, item);
          return true;
        }
      }
    }
    return false;
  }
  else if (dropCtrl && dropCtrl->IDC() == IDC_INVENTORY_CONTENT_HEADER)
  {
    if (srcShortcut < 0 && canRemove) 
    {
      // redirect from the header to the control itself
      CItemBase *dst = dynamic_cast<CItemBase *>(GetCtrl(IDC_INVENTORY_CONTENT));
      if (dst && dst->CanAddItem(item, 0, 0))
      {
        dst->AddItem(item, x, y);
        return true;
      }
    }
    return false;
  }
  else if (dropCtrl && dropCtrl->IDC() == IDC_INVENTORY_HANDS_HEADER)
  {
    if (srcShortcut < 0 && canRemove) 
    {
      // redirect from the header to the control itself
      CItemBase *dst = dynamic_cast<CItemBase *>(GetCtrl(IDC_INVENTORY_HANDS));
      if (dst && dst->CanAddItem(item, 0, 0))
      {
        dst->AddItem(item, x, y);
        return true;
      }
    }
    return false;
  }
  else if (srcShortcut >= 0) // shortcut removed
  {
    CItemBase::SetItemShortcut(srcShortcut, NULL);
    return true;
  }
  else if (src->IDC() == IDC_INVENTORY_CONTENT && canRemove)
  {
    Person *person = _unit ? _unit->GetPerson() : NULL;
    if (person) CItemBase::DropItem(person, item);
    return true;
  }
  else if (canRemove) // dst == 0 and canRemove
  {
    Person *person = _unit ? _unit->GetPerson() : NULL;
    if (person) CItemBase::DropItem(person, item);
    return true;
  }

  return Display::OnDrop(dragCtrl, dropCtrl, x, y);
}

void DisplayInventory::DrawDragInfo(float x, float y, unsigned char enabled, Ref<Font> dragFont, float dragSize)
{
  if (!_dragItem) return;
  const InventoryItemType *type = _dragItem->Type();
  if (!type) return;

  float dragX = x - _dragOffsetX;
  float dragY = y - _dragOffsetY;
  float dragW = (_dragRotated ? type->GetItemHeight() : type->GetItemWidth()) * _dragGridW;
  float dragH = (_dragRotated ? type->GetItemWidth() : type->GetItemHeight()) * _dragGridH;

  PackedColor dragColor;
  switch (enabled)
  {
  case DANone:
    dragColor = _dragColorNone;
    break;
  case DAMove:
    dragColor = _dragColorMove;
    break;
  case DACraft:
    dragColor = _dragColorCraft;
    break;
  case DASwap:
    dragColor = _dragColorSwap;
    break;
  }
  PackedColor dragColorLine = PackedBlack;

  int w = GEngine->Width2D();
  int h = GEngine->Height2D();

  MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(NULL, 0, 0);
  GEngine->Draw2D(mip, dragColor, Rect2DPixel(CX(dragX), CY(dragY), CW(dragW), CH(dragH)));

  GEngine->DrawText(
    Point2DFloat(dragX, dragY - dragSize), 
    dragSize, dragFont, 
    dragColor, 
    type->GetDisplayName(), true);

  GEngine->DrawLine(Line2DFloat(dragX, dragY, dragX + dragW, dragY), dragColorLine, dragColorLine);
  GEngine->DrawLine(Line2DFloat(dragX + dragW, dragY, dragX + dragW, dragY + dragH), dragColorLine, dragColorLine);
  GEngine->DrawLine(Line2DFloat(dragX + dragW, dragY + dragH, dragX, dragY + dragH), dragColorLine, dragColorLine);
  GEngine->DrawLine(Line2DFloat(dragX, dragY + dragH, dragX, dragY), dragColorLine, dragColorLine);

  Color color(0, 0, 0, -1.0f);
  // SourceDayZ: the dragged icon must follow the SPACE rotation preview too.
  InventoryRotationPreviewScope rotationPreview(_dragItem, _dragRotated);
  CItemBase::DrawItem(_dragItem, dragX, dragY, dragW, dragH, _dragZ, 1.0f, color);
}

void DisplayInventory::DrawTooltip(UIViewport *vp, float x, float y, const InventoryItem *item)
{
  const InventoryItemType *type = item->Type();
  if (!type) return;

  // offset the position a bit from the cursor's TL
  float cX = GetCursorX();
  float cY = GetCursorY();
  x += (16.0f / 800.0f) * (1.0f - cX);
  y += (16.0f / 600.0f) * (1.0f - cY);

  int w = GEngine->Width2D();
  int h = GEngine->Height2D();

  Rect2DFloat textRect(0, 0, _tooltipTextWidth - 2 * TEXT_BORDER, 0);
  RefR<INode> text = unconst_cast(item)->GetTooltip();

  // calculate the area
  float width = _tooltipImageWidth + _tooltipTextWidth;
  float height = text ? GetTextHeight(text, textRect, _tooltipTextSize) + 2 * TEXT_BORDER : 0;
  saturateMax(height, _tooltipHeight);

  // keep in the safe area
  AspectSettings as;
  GEngine->GetAspectSettings(as);

  float safeLeft, safeTop, safeRight, safeBottom;
  as.GetUISafeArea(safeLeft, safeTop, safeRight, safeBottom);

  if (x < safeLeft)
    x = safeLeft;
  else if (x + width > safeRight)
    x = safeRight - width;
  if (y < safeTop)
    y = safeTop;
  else if (y + height > safeBottom)
    y = safeBottom - height;

  float xx = CX(x), yy = CY(y), ww = CW(width), hh = CH(height);
  MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(NULL, 0, 0);
  GEngine->Draw2D(mip, _tooltipBgColor, Rect2DPixel(xx, yy, ww, hh));
  GEngine->DrawLine(Line2DPixel(xx, yy, xx + ww, yy), _tooltipLineColor, _tooltipLineColor);
  GEngine->DrawLine(Line2DPixel(xx + ww, yy, xx + ww, yy + hh), _tooltipLineColor, _tooltipLineColor);
  GEngine->DrawLine(Line2DPixel(xx + ww, yy + hh, xx, yy + hh), _tooltipLineColor, _tooltipLineColor);
  GEngine->DrawLine(Line2DPixel(xx, yy + hh, xx, yy), _tooltipLineColor, _tooltipLineColor);

  Color color(0, 0, 0, -1.0f);
  CItemBase::DrawItem(unconst_cast(item), x, y, _tooltipImageWidth, height, _dragZ, 1.0f, color);

  if (text) DrawText(vp, text, Rect2DFloat(x +_tooltipImageWidth + TEXT_BORDER, y + TEXT_BORDER, _tooltipTextWidth - 2 * TEXT_BORDER, height - 2 * TEXT_BORDER), _tooltipTextSize);
}

DisplayInventory::MenuItemAction::MenuItemAction(AIBrain *unit, InventoryItem *item, Action *action, DisplayInventory *parent)
  : MenuItem(action->GetText(unit), MCAction), _item(item), _action(action), _parent(parent)
{
}
   
void DisplayInventory::MenuItemAction::Invoke(AIBrain *unit)
{
  if (_item)
  {
    // simple UI action, handle directly
    if (_action->GetType() == ATInspect)
      _parent->CreateChild(new DisplayInspect(_parent, _item));
    // shortcut for ATGear action ... we want to create dialog on client, so avoid sending to server and back
    else if (!GetNetworkManager().IsServerOwner() && _action->GetType() != ATGear)
      GetNetworkManager().ProcessAction(_action, unit); // ask server to execute the action
    else
      unit->GetVehicle()->StartActionProcessing(_action, unit);
  }
}

DisplayInventory::MenuItemRecipe::MenuItemRecipe(AIBrain *unit, InventoryItem *item1, InventoryItem *item2, const Recipe *recipe)
  : MenuItem(recipe->GetDescription(unit->GetPerson(), item1, item2), MCRecipe), _item1(item1), _item2(item2), _recipe(recipe)
{
}

void DisplayInventory::MenuItemRecipe::Invoke(AIBrain *unit)
{
  Person *person = unit->GetPerson();
  if (!person) return;

  if (!GetNetworkManager().IsServerOwner())
    GetNetworkManager().ProcessRecipe(_recipe->_name, person, _item1, _item2); // ask server to execute the action
  else
    _recipe->Process(person, _item1, _item2);
}

DisplayInventory::MenuItemSwapItems::MenuItemSwapItems(InventoryItem *item1, InventoryItem *item2)
  : MenuItem(CreateTextASCII(NULL, Format(LocalizeString(IDS_INVENTORY_SWAP_ITEMS), cc_cast(item1->GetDisplayName()), cc_cast(item2->GetDisplayName()))), MCSwap), _item1(item1), _item2(item2)
{
}

void DisplayInventory::MenuItemSwapItems::Invoke(AIBrain *unit)
{
  Person *person = unit->GetPerson();
  if (!person) return;

  if (!GetNetworkManager().IsServerOwner())
    GetNetworkManager().AskForSwapItems(_item1, _item2, person);
  else
    SwapItems(_item1, _item2, person);
}

DisplayInventory::MenuItemAddItem::MenuItemAddItem(InventoryItem *item1, InventoryItem *item2, bool attachment)
  : MenuItem(CreateTextASCII(NULL, Format(LocalizeString(attachment ? IDS_INVENTORY_ADD_ATTACHMENT : IDS_INVENTORY_ADD_ITEM), cc_cast(item1->GetDisplayName()), cc_cast(item2->GetDisplayName()))), MCAdd),
  _item1(item1), _item2(item2)
{
}

void DisplayInventory::MenuItemAddItem::Invoke(AIBrain *unit)
{
  if (!_item2->CanAddOwnedItem(_item1)) return;

  if (!GetNetworkManager().IsServerOwner())
    GetNetworkManager().AskForMoveItem(_item2, -1, _item1, _item1->IsInventoryRotated()); // ask server to execute the action
  else
  {
    // remove from the old owner
    Entity *parent = _item1->GetHierarchyParent();
    if (parent) DoVerify(parent->RemoveOwnedItem(_item1));
    
    _item2->AddOwnedItem(_item1);
  }
}

bool DisplayInventory::OnMenuCreated(Menu &menu, float x, float y, int idc, CCMContext &ctx)
{
  if (!_unit) return false;

  CCMContextInventory &context = static_cast<CCMContextInventory &>(ctx);
  if (!context._item1) return false;
  Person *person = _unit->GetPerson();
  if (!person) return false;
  
  bool canRemove = person->GetItemInHands() != context._item1; // item in hands cannot be removed

  if (!GetNetworkManager().IsServerOwner())
  {
    // BOT client have to handle it
    GetNetworkManager().AskInventoryMenu(_unit, context._item1, context._item2);
    MenuItem *item = new MenuItem(CreateTextASCII(NULL, LocalizeString(IDS_LOAD_WORLD)), MCWait);
    item->_enable = false;
    menu.AddItem(item);
    return true;
  }

  if (context._item2)
  {
    // recipes
    Recipes.Collect(person, context._item1, context._item2,
      [this, &context, &menu](const Recipe *recipe) -> bool
      { 
        MenuItem *item = new MenuItemRecipe(_unit, context._item1, context._item2, recipe);
        menu.AddItem(item);
        return false; // continue
      });

    if (canRemove && context._item2->CanAddAttachment(context._item1))
    {
      MenuItem *item = new MenuItemAddItem(context._item1, context._item2, true);
      menu.AddItem(item);
    }
    else if (canRemove && context._item2->CanAddOwnedItem(context._item1))
    {
      MenuItem *item = new MenuItemAddItem(context._item1, context._item2, false);
      menu.AddItem(item);
    }

    if (canRemove && CanSwapItems(context._item1, context._item2))
    {
      MenuItem *item = new MenuItemSwapItems(context._item1, context._item2);
      menu.AddItem(item);
    }
  }
  else
  {
    // collect actions
    context._item1->GetActions(
      [this, &context, &menu](const UIAction &action) -> void 
      { 
        MenuItem *menuItem = new MenuItemAction(_unit, context._item1, action.action, this);
        menu.AddItem(menuItem);
      },
      _unit);
    
    Ref<Action> action = new ActionBasic(ATInspect, context._item1); 
    MenuItem *menuItem = new MenuItemAction(_unit, context._item1, action, this);
    menu.AddItem(menuItem);
  }

  return menu._items.Size() > 0;
}

void DisplayInventory::OnMenuSelected(MenuItem *item)
{
  switch (item->_cmd)
  {
  case MCAction:
    if (_unit)
    {
      MenuItemAction *act = static_cast<MenuItemAction *>(item);
      act->Invoke(_unit);
    }
    break;
  case MCRecipe:
    if (_unit)
    {
      MenuItemRecipe *act = static_cast<MenuItemRecipe *>(item);
      act->Invoke(_unit);
    }
    break;
  case MCAdd:
    if (_unit)
    {
      MenuItemAddItem *act = static_cast<MenuItemAddItem *>(item);
      act->Invoke(_unit);
    }
    break;
  case MCSwap:
    {
      MenuItemSwapItems *act = static_cast<MenuItemSwapItems *>(item);
      act->Invoke(_unit);
    }
    break;
  }
}

void ShowInventoryMenu(AbstractOptionsUI *dlg, const InventoryMenuMessage &msg)
{
  // find the Inventory dialog in hierarchy
  ControlsContainer *ptr = dynamic_cast<ControlsContainer *>(dlg);
  while (ptr)
  {
    if (ptr->IDD() == IDD_INVENTORY) break;
    ptr = ptr->Child();
  }
  
  if (ptr)
  {
    DisplayInventory *inventory = dynamic_cast<DisplayInventory *>(ptr);
    if (inventory) inventory->ShowInventoryMenu(msg);
  }
}

void DisplayInventory::ShowInventoryMenu(const InventoryMenuMessage &msg)
{
  if (!_unit) return;

  // check the message
  Person *person = msg._unit;
  if (!person) return;
  if (person->Brain() != _unit) return;
  if (!msg._item1) return;

  // find the opened context menu
  Menu *menu = FindContextMenu();
  if (!menu) return;

  menu->_items.Clear();
  for (int i=0; i<msg._actions.Size(); i++)
  {
    const InventoryMenuActionMessage &src = msg._actions[i];
    Ref<Action> action;
    if (src._actionType >= 0) action = CreateAction((UIActionType)src._actionType);
    if (action)
    {
      action->SetTarget(src._actionTarget);
      QIStrStream in(src._actionParams.Data(), src._actionParams.Size());
      action->LoadParams(in);

      MenuItem *item = new MenuItemAction(_unit, msg._item1, action, this);
      menu->AddItem(item);
    }
  }

  for (int i=0; i<msg._recipes.Size(); i++)
  {
    const Recipe *recipe = Recipes.Find(msg._recipes[i]);
    if (recipe)
    {
      MenuItem *item = new MenuItemRecipe(_unit, msg._item1, msg._item2, recipe);
      menu->AddItem(item);
    }
  }

  for (int i=0; i<msg._attachments.Size(); i++)
  {
    const InventoryMenuAttachmentMessage &src = msg._attachments[i];
    MenuItem *item = new MenuItemAddItem(src._item1, src._item2, src._attachment);
    menu->AddItem(item);
  }

  if (msg._swap)
  {
    MenuItem *item = new MenuItemSwapItems(msg._item1, msg._item2);
    menu->AddItem(item);
  }

  if (!msg._item2)
  {
    Ref<Action> action = new ActionBasic(ATInspect, msg._item1); 
    MenuItem *menuItem = new MenuItemAction(_unit, msg._item1, action, this);
    menu->AddItem(menuItem);
  }

  if (menu->_items.Size() == 0) RemoveContextMenu();
  else ContextMenuUpdated();
}

#if _ENABLE_IDENTITIES

class DisplayEditDiaryRecord : public Display
{
protected:
  int _recordId;
  InitPtr<CEditContainer> _title;
  InitPtr<CEditContainer> _text;

public:
  DisplayEditDiaryRecord(ControlsContainer *parent, int recordId, RString title, RString text);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  bool CanDestroy();
  int GetRecordId() const {return _recordId;}
  RString GetTitle() const;
  RString GetText() const;
};

DisplayEditDiaryRecord::DisplayEditDiaryRecord(ControlsContainer *parent, int recordId, RString title, RString text)
  : Display(parent)
{
  _recordId = recordId;
  Load("RscDisplayEditDiaryRecord");
  if (parent && !parent->SimulationEnabled()) _enableSimulation = false;
  if (_title) _title->SetText(title);
  if (_text) _text->SetText(text);
}

Control *DisplayEditDiaryRecord::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_EDIT_DIARY_TITLE:
    _title = GetEditContainer(ctrl);
    break;
  case IDC_EDIT_DIARY_RECORD:
    _text = GetEditContainer(ctrl);
    break;
  }
  return ctrl;
}

bool DisplayEditDiaryRecord::CanDestroy()
{
  if (!Display::CanDestroy()) return false;

  if (_exit == IDC_OK)
  {
    if (GetTitle().IsEmpty())
    {
      CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_RECORDTITLE_EMPTY));
      return false;
    }
  }

  return true;
}

RString DisplayEditDiaryRecord::GetText() const
{
  if (!_text) return RString();
  return _text->GetText();
}

RString DisplayEditDiaryRecord::GetTitle() const
{
  if (!_title) return RString();
  return _title->GetText();
}

#endif

#if _VBS2
DisplayMap::DisplayMap(ControlsContainer *parent)
  : Display(parent)
{
  _alwaysShow = true;
  _cursor = CursorArrow;
  _selectWeapons = false;
  _showGearOnExit = false;
  _briefingEnabled = true;
  _saveParams = true;
  _xboxStyle = false;
  _updatePlanAsked = false;
  _compassDir = Vector3(0, 0, 1);
}
#endif

/*!
\patch 1.07 Date 7/20/2001 by Jirka
- Fixed: after install or new user create marker centering in map does not work properly
\patch 5126 Date 2/1/2007 by Jirka
- New: Possibility to show satellite textures in the in-game map and briefing
*/

DisplayMap::DisplayMap(ControlsContainer *parent, RString resource)
  : Display(parent)
{
  _alwaysShow = true;

  _cursor = CursorArrow;
  _selectWeapons = false;
  _showGearOnExit = false;

  _briefingEnabled = true;
#if _VBS3
  _compassDir = Vector3(0, 0, 1);
#endif

  ProgressRefresh();
  Load(resource);
  ProgressRefresh();

  ParamEntryVal cls = Pars >> resource;

  _saveParams = true;
  ConstParamEntryPtr entry = cls.FindEntry("saveParams");
  if (entry) _saveParams = *entry;

  _xboxStyle = false;
  entry = cls.FindEntry("xboxStyle");
  if (entry) _xboxStyle = *entry;

  _updatePlanAsked = false;

  // FIX
  AdjustMapVisibleRect();

  Init();
  ResetHUD();

  ShowMap(true);
  ShowWalkieTalkie(true);
  ShowNotepad(true);
  ShowWarrant(false);
  ShowGPS(true);

  ShowBriefing(false);

  UpdateTexturesButton();
}

void ValidateMarker(CHTMLContainer *html, bool inc, bool next)
{
  CHTMLContainer::FieldIndex i = html->GetActiveFieldIndex();
  if (!i.IsValid()) return;
  const HTMLSection &s = html->GetSection(i._section);
  int n = s.fields.Size();
  if (next)
  {
    if (inc)
    {
      i._field++;
      if (i._field >= n) i._field = 0;
    }
    else
    {
      i._field--;
      if (i._field < 0) i._field = n - 1;
    }
  }
  saturate(i._field, 0, n);

  const char *name = "marker:";
  int len = strlen(name);
  if (inc)
  {
    for (int j=i._field; j<n; j++)
      if (strnicmp(s.fields[j].href, name, len) == 0)
      {
        html->SetActiveFieldIndex(CHTMLContainer::FieldIndex(i._section, j));
        return;
      }
      for (int j=0; j<i._field; j++)
        if (strnicmp(s.fields[j].href, name, len) == 0)
        {
          html->SetActiveFieldIndex(CHTMLContainer::FieldIndex(i._section, j));
          return;
        }
  }
  else
  {
    for (int j=i._field; j>=0; j--)
      if (strnicmp(s.fields[j].href, name, len) == 0)
      {
        html->SetActiveFieldIndex(CHTMLContainer::FieldIndex(i._section, j));
        return;
      }
      for (int j=n-1; j>i._field; j--)
        if (strnicmp(s.fields[j].href, name, len) == 0)
        {
          html->SetActiveFieldIndex(CHTMLContainer::FieldIndex(i._section, j));
          return;
        }
  }
}

void DisplayMap::SwitchBriefingSection(RString section)
{
  if (_briefing)
  {
    _briefing->SwitchSection(section);
    ValidateMarker(_briefing, true, false);
  }
}

DisplayMap::~DisplayMap()
{
  SaveParams();
}

bool DisplayMap::IsShownWalkieTalkie() const
{
  return _walkieTalkie ? _walkieTalkie->IsVisible() : false;  
}

bool DisplayMap::IsShownNotepad() const
{
  return _notepad ? _notepad->IsVisible() : false;  
}

bool DisplayMap::IsBriefingUsed() const
{
  return GetCtrl(IDC_NOTEPAD_PICTURE) != NULL;
}

bool DisplayMap::IsShownBriefing() const
{
  const IControl *pad = GetCtrl(IDC_NOTEPAD_PICTURE);
  return pad ? pad->IsVisible() : false;
}

bool DisplayMap::IsShownWarrant() const
{
  return _warrant ? _warrant->IsVisible() : false;  
}

void DisplayMap::ShowWalkieTalkie(bool show)
{
  if (_walkieTalkie) _walkieTalkie->ShowCtrl(show);
}

void DisplayMap::ShowNotepad(bool show)
{
  if (_notepad)
  {
    _notepad->ShowCtrl(show);
    AdjustMapVisibleRect();
  }
  _briefingEnabled = show;
  if (!show) ShowBriefing(false);
}

void DisplayMap::ShowWarrant(bool show)
{
  if (_warrant) _warrant->ShowCtrl(show);
}

int FindSectionForUnit(HTMLContent &html, RString sectionName, TargetSide side, AIBrain *unit)
{
  RString prefix = sectionName + RString(".");
  int section = -1;
  if (unit)
  {
    RString name = unit->GetVehicle()->GetVarName();
    if (name.GetLength() > 0)
    {
      section = html.FindSection(prefix + name);
    }
  }
  if (section < 0)
  {
    RString name;
    switch (side)
    {
    case TWest:
      name = prefix + RString("West"); goto SideFound;
    case TEast:
      name = prefix + RString("East"); goto SideFound;
    case TGuerrila:
      name = prefix + RString("Guerrila"); goto SideFound;
    case TCivilian:
      name = prefix + RString("Civilian"); goto SideFound;
SideFound:
      section = html.FindSection(name);
      break;
    }
  }
  if (section < 0) section = html.FindSection(sectionName);
  return section;
}

int FindSectionForUnit(HTMLContent &html, RString sectionName, AIBrain *unit)
{
  TargetSide side = unit->GetSide();
  return FindSectionForUnit(html, sectionName, side, unit);
}

static void AddObjective(CHTMLContainer *html, int src, int dst, int value)
{
  if (value == OSHidden) return;

  ParamEntryVal cls = Pars >> "RscObjectives";
  RString picture;
  switch (value)
  {
  case OSDone:
    picture = cls >> "done";
    break;
  case OSFailed:
    picture = cls >> "failed";
    break;
  default:
    Fail("Unknown objective type");
  case OSActive:
    picture = cls >> "active";
    break;
  }
  html->AddBreak(dst, false);
  float imgHeight = 480.0f * 1.0f * html->GetPHeight();
  HTMLField *fld = html->AddImage(dst, picture, HALeft, false, -1, imgHeight, "");
  fld->exclude = true;
  float indent = 1.2 * fld->width;
  html->SetIndent(indent);
  html->CopySection(src, dst);
  html->SetIndent(0);
}

static void AddTasks(CHTMLContainer *html, int dst, Person *player)
{
#if _ENABLE_IDENTITIES

  const RefArray<Task> tasks = player->GetIdentity().GetSimpleTasks();
  ParamEntryVal cls = Pars >> "RscObjectives";
  RString picture;

  for (int i=0; i< tasks.Size(); i++)
  {
    switch (tasks[i]->GetState())
    {
    case TSSucceeded:
      picture = cls >> "done";
      break;
    case TSFailed:
      picture = cls >> "failed";
      break;
    case TSCanceled:
      picture = cls >> "cancled";
      break;
    case TSCreated:
    case TSNone:
    case TSAssigned:
      picture = cls >> "active";
      break;
    default:
      Fail("Unknown objective type");
      break;
    }

    html->AddBreak(dst, false);
    float imgHeight = 480.0f * 1.0f * html->GetPHeight();
    html->SetIndent(0);
    HTMLField *fld = html->AddImage(dst, picture, HALeft, false, -1, imgHeight, "");
    fld->exclude = true;
    float indent = 1.2 * fld->width;

    RString taskdesc;
    tasks[i]->GetDescriptionShort(taskdesc);
    html->SetIndent(indent);
    html->AddText(dst,taskdesc,HFP,HALeft,false,false,"",0,0,false);
    html->AddBreak(dst, false);
  }
  html->SetIndent(0);


#endif
}

//! updates mission plan section of briefing
bool UpdatePlan(CHTMLContainer *briefing, TargetSide side, AIBrain *unit)
{
  // init section
  bool actual = false;
  int section = briefing->FindSection("__PLAN");
  if (section < 0)
  {
    section = briefing->AddSection();
  }
  else
  {
    briefing->InitSection(section);
    if (briefing->CurrentSection() == section) actual = true;
    int s;
    while ((s = briefing->FindSection("__PLAN")) >= 0)
    {
      if (briefing->CurrentSection() == s) actual = true;
      briefing->RemoveSection(s);
    }
  }
  briefing->AddName(section, "__PLAN");

  // plan header
  int source = FindSectionForUnit(*briefing, "Plan", side, unit);
  if (source >= 0) briefing->CopySection(source, section);

  // objectives
  for (int s=0; s<briefing->NSections(); s++)
  {
    const HTMLSection &src = briefing->GetSection(s);
    for (int n=0; n<src.names.Size(); n++)
    {
      RString name = src.names[n];
      static const char *prefix = "OBJ_";
      static const char *prefixWest = "OBJ_WEST_";
      static const char *prefixEast = "OBJ_EAST_";
      static const char *prefixGuerrila = "OBJ_GUER_";
      static const char *prefixCivilian = "OBJ_CIVIL_";
      static const char *prefixHidden = "OBJ_HIDDEN_";
      static const char *prefixWestHidden = "OBJ_WEST_HIDDEN_";
      static const char *prefixEastHidden = "OBJ_EAST_HIDDEN_";
      static const char *prefixGuerrilaHidden = "OBJ_GUER_HIDDEN_";
      static const char *prefixCivilianHidden = "OBJ_CIVIL_HIDDEN_";
      if (strnicmp(name, prefix, strlen(prefix)) == 0)
      {
        bool hidden = false;
        if (strnicmp(name, prefixWest, strlen(prefixWest)) == 0)
        {
          if (side != TWest) continue;
        }
        else if (strnicmp(name, prefixEast, strlen(prefixEast)) == 0)
        {
          if (side != TEast) continue;
        }
        else if (strnicmp(name, prefixGuerrila, strlen(prefixGuerrila)) == 0)
        {
          if (side != TGuerrila) continue;
        }
        else if (strnicmp(name, prefixCivilian, strlen(prefixCivilian)) == 0)
        {
          if (side != TCivilian) continue;
        }
        else if (strnicmp(name, prefixHidden, strlen(prefixHidden)) == 0)
        {
          hidden = true;
          name = prefix + name.Substring(strlen(prefixHidden), INT_MAX);
        }
        else if (strnicmp(name, prefixWestHidden, strlen(prefixWestHidden)) == 0)
        {
          if (side != TWest) continue;
          hidden = true;
          name = prefixWest + name.Substring(strlen(prefixWestHidden), INT_MAX);
        }
        else if (strnicmp(name, prefixEastHidden, strlen(prefixEastHidden)) == 0)
        {
          if (side != TEast) continue;
          hidden = true;
          name = prefixEast + name.Substring(strlen(prefixEastHidden), INT_MAX);
        }
        else if (strnicmp(name, prefixGuerrilaHidden, strlen(prefixGuerrilaHidden)) == 0)
        {
          if (side != TGuerrila) continue;
          hidden = true;
          name = prefixGuerrila + name.Substring(strlen(prefixGuerrilaHidden), INT_MAX);
        }
        else if (strnicmp(name, prefixCivilianHidden, strlen(prefixCivilianHidden)) == 0)
        {
          if (side != TCivilian) continue;
          hidden = true;
          name = prefixCivilian + name.Substring(strlen(prefixCivilianHidden), INT_MAX);
        }
        int value;
        if (unit)
        {
          GameState *state = GWorld->GetGameState();
          value = toInt((float)state->Evaluate(name, GameState::EvalContext::_default, GWorld->GetMissionNamespace())); // mission namespace
        }
        else
        {
          value = hidden ? OSHidden : OSActive; 
        }
        AddObjective(briefing, s, section, value);
        break; // next section
      }
    }
  }

  briefing->FormatSection(section);
  return actual;
}

#if _VBS3
#include "../hla/AAR.hpp"
#endif
#if !_VBS3 // support for dynamic briefing
void DisplayMap::Init()
#else
void DisplayMap::Init(const RString &alternateBriefing)
#endif

{
  // setting default values
  if (_map)
  {
    _map->SetScale(-1);
    _map->Center();
    _map->Reset();
  }

  // load values
  LoadParams();

  // load briefing
  if (_briefing)
  {
    RString briefing = GetBriefingFile();
    if (briefing.GetLength() > 0)
      _briefing->Load(briefing);

#if _VBS3 && _EXT_CTRL
    // disabled until parsing is fixed

    if(GAAR.IsReplayMode()) _briefing->Parse(GAAR.GetBriefingData());

    // support for dynamic briefing
    if (alternateBriefing.GetLength() > 0) _briefing->Parse(alternateBriefing);
#endif

    // assign briefing
    AIBrain *unit = GWorld->FocusOn();
    int section = FindSectionForUnit(*_briefing, "Main", unit);
    if (section >= 0)
      _briefing->AddName(section, "__BRIEFING");
    UpdatePlan();

    // find bookmarks
    _briefing->AddBookmark("__PLAN");
    _briefing->AddBookmark("__BRIEFING");
  }
#if _ENABLE_BRIEF_GRP
  if (_briefing)
  {
    _briefing->AddBookmark("Group");
  }
#else
  if (_bookmark3) _bookmark3->SetText("");
  if (_bookmark4) _bookmark4->SetText("");
#endif

  if (_briefing)
  {
    if (_briefing->FindSection("__PLAN") >= 0)
      SwitchBriefingSection("__PLAN");
    else if (_briefing->FindSection("__BRIEFING") >= 0)
      SwitchBriefingSection("__BRIEFING");
  }
  UpdateKeyHints();
  UpdateButtons();

#if 0
  pos = _walkieTalkie->Position();
  posMin = _walkieTalkie->PositionModelToWorld(_walkieTalkie->GetShape()->Min());
  posMax = _walkieTalkie->PositionModelToWorld(_walkieTalkie->GetShape()->Max());
  dmin = posMin.Z() - pos.Z();
  dmax = posMax.Z() - pos.Z();
  LogF("WalkieTalkie %.3f - %.3f", _posWalkieTalkie.Z() + dmin, _posWalkieTalkie.Z() + dmax);
  LogF("WalkieTalkie zoomed %.3f - %.3f", _posWalkieTalkieZoomed.Z() + dmin, _posWalkieTalkieZoomed.Z() + dmax);

  pos = _notepad->Position();
  posMin = _notepad->PositionModelToWorld(_notepad->GetShape()->Min());
  posMax = _notepad->PositionModelToWorld(_notepad->GetShape()->Max());
  dmin = posMin.Z() - pos.Z();
  dmax = posMax.Z() - pos.Z();
  LogF("Notepad %.3f - %.3f", _posNotepad.Z() + dmin, _posNotepad.Z() + dmax);
  LogF("Notepad zoomed %.3f - %.3f", _posNotepadZoomed.Z() + dmin, _posNotepadZoomed.Z() + dmax);

  pos = _warrant->Position();
  posMin = _warrant->PositionModelToWorld(_warrant->GetShape()->Min());
  posMax = _warrant->PositionModelToWorld(_warrant->GetShape()->Max());
  dmin = posMin.Z() - pos.Z();
  dmax = posMax.Z() - pos.Z();
  LogF("Warrant %.3f - %.3f", _posWarrant.Z() + dmin, _posWarrant.Z() + dmax);
  LogF("Warrant zoomed %.3f - %.3f", _posWarrantZoomed.Z() + dmin, _posWarrantZoomed.Z() + dmax);

  pos = _gps->Position();
  posMin = _gps->PositionModelToWorld(_gps->GetShape()->Min());
  posMax = _gps->PositionModelToWorld(_gps->GetShape()->Max());
  dmin = posMin.Z() - pos.Z();
  dmax = posMax.Z() - pos.Z();
  LogF("GPS %.3f - %.3f", _posGPS.Z() + dmin, _posGPS.Z() + dmax);
  LogF("GPS zoomed %.3f - %.3f", _posGPSZoomed.Z() + dmin, _posGPSZoomed.Z() + dmax);

#endif
}

void DisplayMap::InitHUD()
{
  if (_map) _map->Center();
}

void DisplayMap::ResetHUD()
{
  if (_map)
  {
    _map->Reset();
    if (_map->CanCenter()) _map->Center(true);
  }
  if (_briefing)
  {
    if (_updatePlanAsked) UpdatePlan();
  }
  GInput.cursorX = 0;
  GInput.cursorY = 0;
}

ControlObject *DisplayMap::OnCreateObject(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
  case IDC_MAP_WALKIE_TALKIE:
    _walkieTalkie = new ControlObjectContainer(this, idc, cls);
    return _walkieTalkie;
    /*
    case IDC_MAP_NOTEPAD:
    _notepad = new Notepad(this, idc, cls);
    _notepad->SetBriefing(_briefing);
    return _notepad;
    */
  case IDC_MAP_NOTEPAD:
    // Notepad disabled, log is using instead
    return NULL;
  case IDC_MAP_WARRANT:
    _warrant = new ControlObjectContainer(this, idc, cls);
    return _warrant;
  case IDC_MAP_GPS:
    _gps = new ControlObjectContainer(this, idc, cls);
    return _gps;
  default:
    return Display::OnCreateObject(type, idc, cls);
  }
}

void DisplayMap::AskForUpdatePlan()
{
  if (GWorld && GWorld->HasMap()) UpdatePlan();
  else _updatePlanAsked = true;
}

void DisplayMap::UpdatePlan()
{
  TargetSide side = TSideUnknown;
  AIBrain *unit = GWorld->FocusOn();
  if (unit)
  {
    side = unit->GetSide();
  }
  if (_briefing && ::UpdatePlan(_briefing, side, unit))
  {
    SwitchBriefingSection("__PLAN");
  }
  _updatePlanAsked = false;
}

void DisplayMap::ShowBriefing(bool show)
{
  if (show && !_briefingEnabled) return;
  if (GetCtrl(IDC_NOTEPAD_PICTURE))
  {
    GetCtrl(IDC_NOTEPAD_PICTURE)->ShowCtrl(show);
    if (_briefing)
    {
      if (!show && _map) FocusCtrl(_map->IDC());
      _briefingCtrl->ShowCtrl(show);
      if (show)
      {
        FocusCtrl(_briefingCtrl->IDC());
        ValidateMarker(_briefing, true, false);
      }
      else
      {
        if (_briefing->FindSection("__PLAN") >= 0)
          SwitchBriefingSection("__PLAN");
      }
    }
    IControl *ctrl = GetCtrl(IDC_MAP_NAME);
    if (ctrl) ctrl->ShowCtrl(show);
    ctrl = GetCtrl(IDC_MAP_NOTES);
    if (ctrl) ctrl->ShowCtrl(show);
    ctrl = GetCtrl(IDC_MAP_PLAN);
    if (ctrl) ctrl->ShowCtrl(show);
    ctrl = GetCtrl(IDC_MAP_GROUP);
    if (ctrl) ctrl->ShowCtrl(show);
    ctrl = GetCtrl(IDC_MAP_TEAM_SWITCH);
    if (ctrl) ctrl->ShowCtrl(show);
    UpdateButtons();
  }
}

RString GetNextBookmark(CHTMLContainer *html)
{
  static const char *bookmarks[] = {"__PLAN", "__BRIEFING", "Group"};
  static int n = sizeof(bookmarks) / sizeof(*bookmarks);

  RString bookmark = html->ActiveBookmarkName();
  if (bookmark.GetLength() == 0) return RString();

  for (int i=0; i<n; i++)
  {
    if (stricmp(bookmark, bookmarks[i]) == 0)
    {
      for (int j=i+1; j<n; j++)
        if (html->FindSection(bookmarks[j]) >= 0) return bookmarks[j];
      for (int j=0; j<i; j++)
        if (html->FindSection(bookmarks[j]) >= 0) return bookmarks[j];
      return RString();
    }
  }
  return RString();
}

bool DisplayMap::OnKeyDown(int dikCode)
{
  if (dikCode == INPUT_DEVICE_XINPUT + XBOX_A && GetCtrl(IDC_NOTEPAD_PICTURE) && !GetCtrl(IDC_NOTEPAD_PICTURE)->IsVisible() && 

    _briefing)
  {
    _briefingCtrl->OnKeyDown(dikCode);
    return true;
  }
  if (_map && GetCtrl(IDC_MAP) && GetCtrl(IDC_MAP)->IsVisible())
  {
    if (_map->OnKeyDown(dikCode)) return true;
  }
  if (GetCtrl(IDC_NOTEPAD_PICTURE))
  {
    // X - mod
    switch (dikCode)
    {
    case DIK_UP:
      if (GetCtrl(IDC_NOTEPAD_PICTURE) && !GetCtrl(IDC_NOTEPAD_PICTURE)->IsVisible())
      {
        if (_briefing) ValidateMarker(_briefing, false, true);
        UpdateKeyHints();
        return true;
      }
      else break;
    case DIK_DOWN:
      if (GetCtrl(IDC_NOTEPAD_PICTURE) && !GetCtrl(IDC_NOTEPAD_PICTURE)->IsVisible())
      {
        if (_briefing) ValidateMarker(_briefing, true, true);
        UpdateKeyHints();
        return true;
      }
      else break;
    case DIK_SPACE:
      if (GetCtrl(IDC_NOTEPAD_PICTURE) && !GetCtrl(IDC_NOTEPAD_PICTURE)->IsVisible() && _briefing)
      {
        _briefingCtrl->OnKeyDown(dikCode);
        return true;
      }
      else break;
    }
  }
  return Display::OnKeyDown(dikCode);
}

bool DisplayMap::OnKeyUp(int dikCode)
{
  if (dikCode == INPUT_DEVICE_XINPUT + XBOX_A && GetCtrl(IDC_NOTEPAD_PICTURE) && !GetCtrl(IDC_NOTEPAD_PICTURE)->IsVisible() && 

    _briefing)
  {
    _briefingCtrl->OnKeyUp(dikCode);
    return true;
  }
  if (_map && GetCtrl(IDC_MAP) && GetCtrl(IDC_MAP)->IsVisible())
  {
    if (_map->OnKeyUp(dikCode)) return true;
  }
  if (GetCtrl(IDC_NOTEPAD_PICTURE))
  {
    // X - mod
    switch (dikCode)
    {
    case DIK_SPACE:
      if (GetCtrl(IDC_NOTEPAD_PICTURE) && !GetCtrl(IDC_NOTEPAD_PICTURE)->IsVisible() && _briefing)
      {
        _briefingCtrl->OnKeyUp(dikCode);
        return true;
      }
      else break;
    }
  }
  return Display::OnKeyUp(dikCode);
}

void DisplayMap::UpdateButtons()
{
  IControl *bg = GetCtrl(IDC_NOTEPAD_PICTURE);

  IControl *ctrl = GetCtrl(IDC_MAP_BRIEFING_SHOW);
  if (ctrl)
  {
    if (_briefingEnabled && bg)
    {
      ctrl->ShowCtrl(true);
      ITextContainer *text = GetTextContainer(ctrl);
      if (text) text->SetText(LocalizeString(bg->IsVisible() ? IDS_MAP_BRIEFING_HIDE : IDS_MAP_BRIEFING_SHOW));
    }
    else ctrl->ShowCtrl(false);
  }
  ctrl = GetCtrl(IDC_MAP_BRIEFING_PAGE);
  if (ctrl)
  {
    if (_briefingEnabled && bg && bg->IsVisible() && _briefing)
    {
      RString str;
      RString next = GetNextBookmark(_briefing);
      if (stricmp(next, "__PLAN") == 0) str = LocalizeString(IDS_MAP_BRIEFING_PLAN);
      else if (stricmp(next, "__BRIEFING") == 0) str = LocalizeString(IDS_MAP_BRIEFING_NOTES);
      else if (stricmp(next, "Group") == 0) str = LocalizeString(IDS_MAP_BRIEFING_GROUP);

      ctrl->ShowCtrl(str.GetLength() > 0);
      ITextContainer *text = GetTextContainer(ctrl);
      if (text) text->SetText(str);
    }
    else ctrl->ShowCtrl(false);
  }
  ctrl = GetCtrl(IDC_MAP_INVENTORY);
  if (ctrl)
  {
    ctrl->ShowCtrl(!IsInGame() || GWorld->FocusOn() && GWorld->FocusOn()->LSIsAlive());
  }
}

RString DisplayMap::GetKeyHint(int button, bool &structured) const
{
  switch (button)
  {
  case INPUT_DEVICE_XINPUT + XBOX_A:
    if (GetCtrl(IDC_NOTEPAD_PICTURE) && _briefing && (_briefingCtrl->IsFocused() || !_briefingCtrl->IsVisible()))
    {
      const HTMLField *field = _briefing->GetActiveFieldKeyboard();

      if (field && field->href.GetLength() > 0)
      {
        const char *ptr = field->href;
        const char *name = "marker:";
        int n = strlen(name);
        if (strnicmp(ptr, name, n) == 0)
        {
          ptr += n;
          for (int i=0; i<markersMap.Size(); i++)
          {
            ArcadeMarkerInfo &mInfo = markersMap[i];
            if (stricmp(ptr, mInfo.name) == 0)
            {
              RString text = Localize(mInfo.text);
              if (text.GetLength() > 0)
                return Format(LocalizeString(IDS_BRIEFING_LINK_MARKER), (const char *)text);
            }
          }
        }
        if (field->text.GetLength() > 0)
          return Format(LocalizeString(IDS_BRIEFING_LINK_MARKER), (const char *)field->text);
        return LocalizeString(IDS_BRIEFING_LINK_SECTION);
      }
    }
    return RString();
  default:
    return Display::GetKeyHint(button, structured);
  }
}

DisplayGetReady *GetBriefing() {return GBriefing;}


#ifndef min
inline int min(int a, int b)
{
  return a<b ? a : b;
}
#endif

static RString GetSkill(float skill)
{
  RString text(" (");
  if (skill <= 0.25)
    text = text + LocalizeString(IDS_SKILL_NOVICE);
  else if (skill <= 0.45)
    text = text + LocalizeString(IDS_SKILL_ROOKIE);
  else if (skill <= 0.65)
    text = text + LocalizeString(IDS_SKILL_RECRUIT);
  else if (skill <= 0.85)
    text = text + LocalizeString(IDS_SKILL_VETERAN);
  else
    text = text + LocalizeString(IDS_SKILL_EXPERT);
  return text + RString(")");
}

DrawCoord SceneToScreen(Vector3Par pos)
{
  AspectSettings as;
  GEngine->GetAspectSettings(as);
  float invz = CameraZoom / pos.Z();
  float x3d = 1/as.leftFOV * pos.X() * invz + 0.5;
  float y3d = 0.5 - 1/as.topFOV * pos.Y() * invz;
  DrawCoord pt;
  pt.x = (x3d-as.uiTopLeftX)/(as.uiBottomRightX-as.uiTopLeftX);
  pt.y = (y3d-as.uiTopLeftY)/(as.uiBottomRightY-as.uiTopLeftY);
  return pt;
}

void DisplayMap::SetRadioText()
{
  if (_radioAlpha) _radioAlpha->SetText(LocalizeString(IDS_RADIO_UNASSIGNED));
  if (_radioBravo) _radioBravo->SetText(LocalizeString(IDS_RADIO_UNASSIGNED));
  if (_radioCharlie) _radioCharlie->SetText(LocalizeString(IDS_RADIO_UNASSIGNED));
  if (_radioDelta) _radioDelta->SetText(LocalizeString(IDS_RADIO_UNASSIGNED));
  if (_radioEcho) _radioEcho->SetText(LocalizeString(IDS_RADIO_UNASSIGNED));
  if (_radioFoxtrot) _radioFoxtrot->SetText(LocalizeString(IDS_RADIO_UNASSIGNED));
  if (_radioGolf) _radioGolf->SetText(LocalizeString(IDS_RADIO_UNASSIGNED));
  if (_radioHotel) _radioHotel->SetText(LocalizeString(IDS_RADIO_UNASSIGNED));
  if (_radioIndia) _radioIndia->SetText(LocalizeString(IDS_RADIO_UNASSIGNED));
  if (_radioJuliet) _radioJuliet->SetText(LocalizeString(IDS_RADIO_UNASSIGNED));
}

/*!
\patch 5142 Date 3/20/2007 by Jirka
- Fixed: Map info (commands, friendly and enemy units) is now saving into game save
*/

LSError DisplayMap::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("showMap", _showMap, 1, true))  
  CHECK(ar.Serialize("showGPS", _showGPS, 1, false))
  if (_map)
  {
    // ignore the return value, map may not be saved
    _map->Serialize(ar, "Map", 1);
  }
  return LSOK;
}

LSError DisplayMap::SerializeParams(ParamArchive &ar)
{
  if (_walkieTalkie) CHECK(ar.Serialize("WalkieTalkie", *(SerializeClass *)_walkieTalkie, 1));
  if (_notepad) CHECK(ar.Serialize("Notepad", *(SerializeClass *)_notepad, 1));
  if (_warrant) CHECK(ar.Serialize("Warrant", *(SerializeClass *)_warrant, 1));
  if (_gps) CHECK(ar.Serialize("GPS2", *(SerializeClass *)_gps, 1));
  return LSOK;
}

bool ParseUserParamsArchive(ParamArchiveLoad &ar, GameDataNamespace *globals);
bool ParseUserParamsArchive(ParamArchiveSave &ar, GameDataNamespace *globals);
void SaveUserParamsArchive(ParamArchiveSave &ar);

void DisplayMap::LoadParams()
{
  if (!_saveParams) return;

  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamArchiveLoad ar;
  if (!ParseUserParamsArchive(ar, &globals)) return;

  ParamArchive arSubcls;
  if (!ar.OpenSubclass("MainMap", arSubcls))
  {
    // TODO: load failed
    return;
  }
  if (SerializeParams(arSubcls) != LSOK)
  {
    // TODO: load failed
    return;
  }
}

void DisplayMap::SaveParams()
{
  if (!_saveParams) return;
  // when out of memory, saving could fail severely
  if (IsOutOfMemory()) return;

  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamArchiveSave ar(UserInfoVersion);
  if (!ParseUserParamsArchive(ar, &globals)) return;

  ParamArchive arSubcls;
  if (!ar.OpenSubclass("MainMap", arSubcls))
  {
    // TODO: save failed
    return;
  }
  if (SerializeParams(arSubcls) != LSOK)
  {
    // TODO: save failed
    return;
  }

  SaveUserParamsArchive(ar);
}

Control *DisplayMap::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
  case IDC_MAP:
    {
      _map = new CStaticMapMain(this, idc, cls);
      return _map;
    }
  case IDC_NOTEPAD_PICTURE:
    {
      // Notepad disabled, log is using instead
      return NULL;
    }
#if _ENABLE_IDENTITIES
  case IDC_DIARY_TOPIC_LIST:
  case IDC_DIARY_TOPIC_LISTINDEX:
    {
      CMapListBox *mapList = new CMapListBox(this,idc,cls);
      return mapList;
    }
  case IDC_DIARY_TOPIC_HTML_GROUP:
    {
      CMapCGroup *cgroup  = new CMapCGroup(this,idc,cls);
      return cgroup;
    }
#endif
  }

  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_RADIO_ALPHA:
    _radioAlpha = GetTextContainer(ctrl);
    break;
  case IDC_RADIO_BRAVO:
    _radioBravo = GetTextContainer(ctrl);
    break;
  case IDC_RADIO_CHARLIE:
    _radioCharlie = GetTextContainer(ctrl);
    break;
  case IDC_RADIO_DELTA:
    _radioDelta = GetTextContainer(ctrl);
    break;
  case IDC_RADIO_ECHO:
    _radioEcho = GetTextContainer(ctrl);
    break;
  case IDC_RADIO_FOXTROT:
    _radioFoxtrot = GetTextContainer(ctrl);
    break;
  case IDC_RADIO_GOLF:
    _radioGolf = GetTextContainer(ctrl);
    break;
  case IDC_RADIO_HOTEL:
    _radioHotel = GetTextContainer(ctrl);
    break;
  case IDC_RADIO_INDIA:
    _radioIndia = GetTextContainer(ctrl);
    break;
  case IDC_RADIO_JULIET:
    _radioJuliet = GetTextContainer(ctrl);
    break;
  case IDC_MAP_NAME:
    _name = GetTextContainer(ctrl);
    {
      RString name;
      if (GWorld->GetMode() == GModeNetware)
      {
        const MissionHeader *header = GetNetworkManager().GetMissionHeader();
        if (header) name = header->GetLocalizedMissionName();
      }
      if (name.GetLength() == 0)
      {
        name = Localize(CurrentTemplate->Intel().briefingName);
        if (name.GetLength() == 0)
        {
          name = Glob.header.filenameReal;
          // user mission file name is encoded
          if (IsUserMission()) name = DecodeFileName(name);
        }
      }
#if _VBS3
      // Quick fix for AAR mission name
      if(stricmp(name,"__AAR")==0)
        name = RString("After Action Review");
#endif
      _name->SetText(name);
    }
    break;
  case IDC_BRIEFING:
    _briefing = GetHTMLContainer(ctrl);
    _briefingCtrl = ctrl;
    break;
  case IDC_MAP_NOTES:
    _bookmark1 = GetTextContainer(ctrl);
    break;
  case IDC_MAP_PLAN:
    _bookmark2 = GetTextContainer(ctrl);
    break;
  case IDC_MAP_GROUP:
    _bookmark3 = GetTextContainer(ctrl);
    break;
  case IDC_MAP_TEAM_SWITCH:
    _bookmark4 = GetTextContainer(ctrl);
    break;
  case IDC_GPS:
    _gpsCtrl = GetTextContainer(ctrl);
    break;
  case IDC_GPS_ALT:
    _gpsAltCtrl = GetTextContainer(ctrl);
    break;
  case IDC_GPS_HEADING:
    _gpsHeadingCtrl = GetTextContainer(ctrl);
    break;
  case IDC_GETREADY_TITLE:
    {
      ITextContainer *text = GetTextContainer(ctrl);
      AIBrain *agent = GWorld->FocusOn();
      if (agent && agent->GetPerson())
        text->SetText(agent->GetPerson()->GetPersonName());
      else 
        text->SetText("");
    }
    break;

    /*
    case IDC_NOTEPAD_PICTURE:
    {
    RString FindPicture(RString name);

    RString text = FindPicture(cls >> "picturePlan");
    text.Lower();
    _picturePlan = GlobLoadTexture(text);
    if (_picturePlan) _picturePlan->SetUsageType(Texture::TexUI); // no limits

    text = FindPicture(cls >> "pictureNotes");
    text.Lower();
    _pictureNotes = GlobLoadTexture(text);
    if (_pictureNotes) _pictureNotes->SetUsageType(Texture::TexUI); // no limits

    text = FindPicture(cls >> "pictureGroup");
    text.Lower();
    _pictureGroup = GlobLoadTexture(text);
    if (_pictureGroup) _pictureGroup->SetUsageType(Texture::TexUI); // no limits

    text = FindPicture(cls >> "picture");
    text.Lower();
    _picture = GlobLoadTexture(text);
    if (_picture) _picture->SetUsageType(Texture::TexUI); // no limits
    }
    break;
    */
  }
  return ctrl;
}

void DisplayMap::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_RADIO_ALPHA:
  case IDC_RADIO_BRAVO:
  case IDC_RADIO_CHARLIE:
  case IDC_RADIO_DELTA:
  case IDC_RADIO_ECHO:
  case IDC_RADIO_FOXTROT:
  case IDC_RADIO_GOLF:
  case IDC_RADIO_HOTEL:
  case IDC_RADIO_INDIA:
  case IDC_RADIO_JULIET:
    Fail("Radio triggers no longer supported");
    break;
  case IDC_MAP_NOTES:
    SwitchBriefingSection("__BRIEFING");
    break;
  case IDC_MAP_PLAN:
    SwitchBriefingSection("__PLAN");
    break;
#if _ENABLE_BRIEF_GRP
  case IDC_MAP_GROUP:
    SwitchBriefingSection("Group");
    break;
#endif
  case IDC_MAP_TEXTURES:
    if (_map)
    {
      _map->ShowScale(!_map->IsShowingScale());
      UpdateTexturesButton();
    }
    break;
  case IDC_MAP_BRIEFING_SHOW:
    if (_briefingEnabled && GetCtrl(IDC_NOTEPAD_PICTURE))
    {
      bool show = !GetCtrl(IDC_NOTEPAD_PICTURE)->IsVisible();
      ShowBriefing(show);
      UpdateButtons();
    }
    break;
  case IDC_MAP_BRIEFING_PAGE:
    if (_briefingEnabled && GetCtrl(IDC_NOTEPAD_PICTURE) && _briefing && idc == _briefingCtrl->IDC())
    {
      RString next = GetNextBookmark(_briefing);
      if (next.GetLength() > 0) SwitchBriefingSection(next);
      UpdateButtons();
    }
    break;
  case IDC_MAP_PREV_MARKER:
    if (GetCtrl(IDC_NOTEPAD_PICTURE) && !GetCtrl(IDC_NOTEPAD_PICTURE)->IsVisible())
    {
      if (_briefing) ValidateMarker(_briefing, false, true);
      UpdateKeyHints();
    }
    break;
  case IDC_MAP_NEXT_MARKER:
    if (GetCtrl(IDC_NOTEPAD_PICTURE) && !GetCtrl(IDC_NOTEPAD_PICTURE)->IsVisible())
    {
      if (_briefing) ValidateMarker(_briefing, true, true);
      UpdateKeyHints();
    }
    break;
  case IDC_CANCEL:
    break;
  }
}

void DisplayMap::UpdateTexturesButton()
{
  ITextContainer *button = GetTextContainer(GetCtrl(IDC_MAP_TEXTURES));
  if (button)
  {
    if (_map && _map->IsVisible())
    {
      button->GetControl()->ShowCtrl(true);
      if (_map->IsShowingScale())
        button->SetText(LocalizeString(IDS_ARCMAP_SHOW_TEXTURES));
      else
        button->SetText(LocalizeString(IDS_ARCMAP_HIDE_TEXTURES));
    }
    else
      button->GetControl()->ShowCtrl(false);
  }
}

void DisplayMap::AnimateTo(Vector3Par position)
{
  float invSizeLand = 1.0 / (LandGrid * LandRange);
  Vector3 curPos = _map->GetCenter();
  float curScale = _map->GetScale();
  float endScale = curScale;
  float diff = curPos.Distance(position);
  float scale = 2.0 * diff * invSizeLand;

  const MapProperties &props = _map->GetMapProperties();

  saturate(scale, props._scaleMin, props._scaleMax);

  int sizeAnim = _map->_animation.Size();
  if (sizeAnim > 0)
  {
    // animation interrupted, save end scale
    MapAnimationPhase &phase = _map->_animation[sizeAnim - 1];
    endScale = phase.scale;
  }
  _map->ClearAnimation();
  const float maxSpeed = 3000;
  float minTime = diff/maxSpeed;
  if (scale > curScale)
  {
    float time = log(scale / curScale);
    _map->AddAnimationPhase(time, scale, curPos);
    float moveTime = floatMax(minTime,1);
    _map->AddAnimationPhase(moveTime, scale, position);
  }
  else
  {
    float time = floatMax(minTime, diff * invSizeLand / curScale);
    _map->AddAnimationPhase(time, curScale, position);
    scale = curScale;
  }
  if (scale > endScale)
  {
    float time = log(scale / endScale);
    _map->AddAnimationPhase(time, endScale, position);
  }
  _map->CreateInterpolator();

  if (GetCtrl(IDC_NOTEPAD_PICTURE))
  {
    ShowBriefing(false);
    UpdateButtons();
  }
}

/*!
\patch 1.78 Date 7/10/2002 by Jirka
- Fixed: In briefing weapon disappear if trying to assign where doesn't fit
\patch 1.8 Date 9/16/2002 by Ondra
- Fixed: When some units are dead, links in notebook Group section may
point to wrong units.
*/

bool DisplayMap::OnHTMLLink(int idc, RString link)
{
  const char *ptr = link;
  const char *name = "marker:";
  int n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    ptr += n;
    for (int i=0; i<markersMap.Size(); i++)
    {
      ArcadeMarkerInfo &mInfo = markersMap[i];
      if (stricmp(ptr, mInfo.name) == 0)
      {
        AnimateTo(mInfo.position);
        _map->SetActiveMarker(i);
        return true;
      }
    }
    return true;
  }

  return false;
}

void DisplayMap::OnDraw(EntityAI *vehicle, float alpha)
{
  if (_child && _child->IDD() == IDD_INVENTORY)
  {
    // special drawing under Gear dialog
    if (_map && _map->IsVisible())
    {
      UIViewport *vp = Create2DViewport();
      _map->OnDraw(vp, alpha);
      Destroy2DViewport(vp);
    }
    return;
  }

  // show items based on mission and availability
  if (_map) _map->ShowCtrl(_showMap && GWorld->HasPlayerMap() || _showGPS && GWorld->HasPlayerGPS() || _map->IsShowMapForced());
  if (_gps) _gps->ShowCtrl(_showGPS && GWorld->HasPlayerGPS());

  // Radio slots
  SetRadioText();
  // Show / hide mission name
  IControl *ctrl = GetCtrl(IDC_MAP_NAME);
  if (ctrl) ctrl->ShowCtrl(_briefing ? _briefing->ActiveBookmark() >= 0 : false);
  // GPS position
  if (_gpsCtrl)
  {
    Object *cameraOn = GWorld->CameraOn();
    if (cameraOn)
    {
      //VBS supports bigger grids
      char buffer[32];
      PositionToAA11(cameraOn->RenderVisualState().Position(), buffer);
      _gpsCtrl->SetText(buffer);
    }
    else
      _gpsCtrl->SetText("----");
  }

  if (_gpsAltCtrl && vehicle && vehicle->GetShape())
  {
    Vector3Val pos = vehicle->RenderVisualState().Position();
    float y = pos.Y() + vehicle->GetShape()->Min().Y();
    const float elevationOffset = GLandscape->GetLandElevationOffset();
    // float y0 = GLOB_LAND->RoadSurfaceYAboveWater(pos.X(), pos.Z()); 
    y += elevationOffset;
    int h = int(y /* - y0*/);
    saturate(h, 0, 99999);
    char buffer[32];
    sprintf(buffer, "%d", h);
    _gpsAltCtrl->SetText(cc_cast(buffer));
  }
  if (_gpsHeadingCtrl)
  {
    Camera &cam = *GLOB_SCENE->GetCamera();
    Vector3Val dir = cam.Direction();

    // avoid singularity when heading up
    float angle;
    if (fabs(dir.X()) + fabs(dir.Z()) > 1e-3)
      angle = atan2(dir.X(), dir.Z());
    else
      angle = 0;

    int degAngle = toInt(angle * (180.0f / H_PI));
    if (degAngle < 0) degAngle += 360;
    BString<16> buffer;
    sprintf(buffer, "%d", degAngle);

    _gpsHeadingCtrl->SetText(cc_cast(buffer));
  }

  // FIX: do not show notepad for dead units
#if _VBS3 // allow notepad to be shown in new editor
  if (_idd != IDD_MISSION_EDITOR)
  {
#endif
    Person *player = GWorld->GetRealPlayer();
    AIBrain *playerUnit = player ? player->Brain() : NULL;
    if (!playerUnit || playerUnit->GetLifeState() == LifeStateDead) ShowNotepad(false);
#if _VBS3 // allow notepad to be shown in new editor
  }
#endif

  // notepad texture
  ctrl = GetCtrl(IDC_NOTEPAD_PICTURE);
  if (ctrl && ctrl->IsVisible() && _briefing)
  {
    DoAssert(ctrl->GetType() == CT_STATIC);
    CStatic *text = static_cast<CStatic *>(ctrl);
    switch (_briefing->ActiveBookmark())
    {
    case 0:
      text->SetTexture(_picturePlan);
      break;
    case 1:
      text->SetTexture(_pictureNotes);
      break;
#if _ENABLE_BRIEF_GRP
    case 2:
      text->SetTexture(_pictureGroup);
      break;
#endif
    default:
      text->SetTexture(_picture);
      break;
    }
  }

  // if possible, do not allow map gain focus
  if (_map && _map->IsFocused() && _briefing && _briefingCtrl->IsVisible()) FocusCtrl(_briefingCtrl->IDC());

  if (_map && _map->IsVisible())
  {
    float mouseX = 0.5 + GInput.cursorX * 0.5;
    float mouseY = 0.5 + GInput.cursorY * 0.5;

    //  saturate(mouseX,0,1);
    //  saturate(mouseY,0,1);

    IControl *ctrl = GetCtrl(mouseX, mouseY);
    if (ctrl && ctrl == _map)
    {
      if (_map->_dragging || _map->_selecting)
      {
        if (_cursor != CursorMove)
        {
          _cursor = CursorMove;
          SetCursor(_map->OverrideCursor("Move"));
        }
      }
      else if (_map->_moving)
      {
        if (_cursor != CursorScroll)
        {
          _cursor = CursorScroll;
          SetCursor(_map->OverrideCursor("Scroll"));
        }
      }
      else
      {
        if (_cursor != CursorTrack)
        {
          _cursor = CursorTrack;
          SetCursor(_map->OverrideCursor("Track"));
        }
      }
    }
    else
    {
      if (_cursor != CursorArrow)
      {
        _cursor = CursorArrow;
        SetCursor(_map->OverrideCursor("Arrow"));
      }
    }
  }

  Display::OnDraw(vehicle, alpha);
}

void DisplayMap::OnHide()
{
  if (_map) _map->OnHide();
}

/*!
\patch 5129 Date 2/20/2007 by Ondra
- Improved: Delay when returning from map to game is now much shorter.
*/
void DisplayMap::OnSimulate(EntityAI *vehicle)
{
  Display::OnSimulate(vehicle);
  if (!_map) return;

  _map->ProcessCheats();

  if (_map->IsVisible())
  {
    if (!_map->IsMoving())
    {
      float ratioW =(float)GEngine->WidthBB()/GEngine->Width2D();
      float ratioH =(float)GEngine->HeightBB()/GEngine->Height2D();
      AspectSettings asp;
      GEngine->GetAspectSettings(asp);

		float sfX = (-asp.uiTopLeftX) * ratioW;
      float sfY = (-asp.uiTopLeftY) * ratioH;

      float mouseX = 0.5 + GInput.cursorX * 0.5;
      float mouseY = 0.5 + GInput.cursorY * 0.5;

      saturate(mouseX,sfX,1-sfX);
      saturate(mouseY,sfY,1-sfY);

      // automatic map movement on edges
      bool moveEdge = false;
      float dif = 0.01 - (mouseX-sfX);
      if (dif > 0) _map->ScrollX(0.003 * exp(dif * 100.0f)),moveEdge = true;
      else
      {
        dif = (mouseX+sfX) - 0.99;
        if (dif > 0) _map->ScrollX(-0.003 * exp(dif * 100.0f)),moveEdge = true;
      }

      dif = 0.01 - (mouseY-sfY);
      if (dif > 0) _map->ScrollY(0.003 * exp(dif * 100.0f)),moveEdge = true;
      else
      {
        dif = (mouseY+sfY) - 0.99;
        if (dif > 0) _map->ScrollY(-0.003 * exp(dif * 100.0f)),moveEdge = true;
      }

      if (!_map->_interpolator)
      {
        // we are willing to dedicate 50 ms to preload if necessary
        // unless player is moving a lot, it should not be used
        bool done = GWorld->PreloadAroundCamera(0.050f); // keep reasonable framerate
        (void)done;
#if 0 // _ENABLE_CHEATS
        DIAG_MESSAGE(100, done ? "Scene preload done" : "Scene preload not done");
#endif
      }
    }
  }
}

/*!
\patch 1.61 Date 5/29/2002 by Jirka
- Fixed: Map has wrong center when notepad is hidded
*/

void DisplayMap::AdjustMapVisibleRect()
{
  float x = _map->X();
  float y = _map->Y();
  float w = _map->W();
  float h = _map->H();

  if (!_notepad || !_notepad->IsVisible())
  {
    _map->SetVisibleRect(x, y, w, h);
    return;
  }

  // adjust map visible rectangle
  Vector3 min = _notepad->FutureVisualState().PositionModelToWorld
    (
    _notepad->GetShape()->Min()
    );
  Vector3 max = _notepad->FutureVisualState().PositionModelToWorld
    (
    _notepad->GetShape()->Max()
    );
  DrawCoord pt, ptMin, ptMax;
  ptMin.x = 1; ptMin.y = 1;
  ptMax.x = 0; ptMax.y = 0;

  pt = SceneToScreen(min);
  saturateMin(ptMin.x, pt.x);
  saturateMin(ptMin.y, pt.y);
  saturateMax(ptMax.x, pt.x);
  saturateMax(ptMax.y, pt.y);
  pt = SceneToScreen(Vector3(min.X(), min.Y(), max.Z()));
  saturateMin(ptMin.x, pt.x);
  saturateMin(ptMin.y, pt.y);
  saturateMax(ptMax.x, pt.x);
  saturateMax(ptMax.y, pt.y);
  pt = SceneToScreen(Vector3(min.X(), max.Y(), min.Z()));
  saturateMin(ptMin.x, pt.x);
  saturateMin(ptMin.y, pt.y);
  saturateMax(ptMax.x, pt.x);
  saturateMax(ptMax.y, pt.y);
  pt = SceneToScreen(Vector3(min.X(), max.Y(), max.Z()));
  saturateMin(ptMin.x, pt.x);
  saturateMin(ptMin.y, pt.y);
  saturateMax(ptMax.x, pt.x);
  saturateMax(ptMax.y, pt.y);
  pt = SceneToScreen(Vector3(max.X(), min.Y(), min.Z()));
  saturateMin(ptMin.x, pt.x);
  saturateMin(ptMin.y, pt.y);
  saturateMax(ptMax.x, pt.x);
  saturateMax(ptMax.y, pt.y);
  pt = SceneToScreen(Vector3(max.X(), min.Y(), max.Z()));
  saturateMin(ptMin.x, pt.x);
  saturateMin(ptMin.y, pt.y);
  saturateMax(ptMax.x, pt.x);
  saturateMax(ptMax.y, pt.y);
  pt = SceneToScreen(Vector3(max.X(), max.Y(), min.Z()));
  saturateMin(ptMin.x, pt.x);
  saturateMin(ptMin.y, pt.y);
  saturateMax(ptMax.x, pt.x);
  saturateMax(ptMax.y, pt.y);
  pt = SceneToScreen(max);
  saturateMin(ptMin.x, pt.x);
  saturateMin(ptMin.y, pt.y);
  saturateMax(ptMax.x, pt.x);
  saturateMax(ptMax.y, pt.y);

  float aLeft = ptMin.x > x ? (ptMin.x - x) * h : 0;
  float aRight = ptMax.x < x + w ? (x + w - ptMax.x) * h : 0;
  float aTop = ptMin.y > y ? (ptMin.y - y) * w : 0;
  float aBottom = ptMax.y < y + h ? (y + h - ptMax.y) * w : 0;

  if (aLeft > aRight)
  {
    if (aLeft > aTop)
    {
      if (aLeft > aBottom) // left
        _map->SetVisibleRect(x, y, ptMin.x - x, h);
      else // bottom
        _map->SetVisibleRect(x, ptMax.y, w, y + h - ptMax.y);
    }
    else if (aTop > aBottom) // top
      _map->SetVisibleRect(x, y, w, ptMin.y - y);
    else // bottom
      _map->SetVisibleRect(x, ptMax.y, w, y + h - ptMax.y);
  }
  else if (aRight > aTop)
  {
    if (aRight > aBottom) // right
      _map->SetVisibleRect(ptMax.x, y, x + w - ptMax.x, h);
    else // bottom
      _map->SetVisibleRect(x, ptMax.y, w, y + h - ptMax.y);
  }
  else if (aTop > aBottom) // top
    _map->SetVisibleRect(x, y, w, ptMin.y - y);
  else if (aBottom > 0) // bottomf
    _map->SetVisibleRect(x, ptMax.y, w, y + h - ptMax.y);
  else
    // TODO: move notepad
    _map->SetVisibleRect(x, y, w, h);
}

// static variable initialization
int DisplayMap::NextMarkerID = 0;
void ResetMarkerID() { DisplayMap::NextMarkerID=0; }

void DisplayMap::OnChildDestroyed(int idd, int exit)
{
  if (idd == IDD_INSERT_MARKER)
  {
    if (exit == IDC_OK)
    {
      DisplayInsertMarker *display = dynamic_cast<DisplayInsertMarker *>((ControlsContainer *)_child);
      Assert(display);

      int index = markersMap.Add();
      ArcadeMarkerInfo &marker = markersMap[index];
      marker.position = _map->ScreenToWorld(DrawCoord(display->_x, display->_y));
      char buffer[256]; sprintf(buffer, "_USER_DEFINED #%d/%d", GetNetworkManager().GetPlayer(), NextMarkerID++);
      marker.name = buffer;
      marker.text = display->_text;
      marker.markerType = MTIcon;
      ParamEntryVal markers = Pars >> "CfgMarkers";
      if (display->_picture >= 0 && display->_picture < markers.GetEntryCount())
      {
        ParamEntryVal markerCfg = markers.GetEntry(display->_picture);
        marker.type = markerCfg.GetName();
        marker.OnTypeChanged();
      }

      ParamEntryVal colors = Pars >> "CfgMarkerColors";
      if (display->_color >= 0 && display->_color < colors.GetEntryCount())
      {
        ParamEntryVal colorCfg = colors.GetEntry(display->_color);
        marker.colorName = colorCfg.GetName();
        marker.OnColorChanged();
      }

      marker.fillName = "Solid";
      marker.OnFillChanged();
      marker.a = 1;
      marker.b = 1;
#if _VBS3 // marker EH
      void ProcessCreateMarkerEH(RString name, bool updated = true);
      ProcessCreateMarkerEH(buffer,false);
#endif
      ChatChannel channel = ActualChatChannel();
      Person *veh = GWorld->GetRealPlayer();
      AIBrain *unit = veh ? veh->Brain() : NULL;
      if (channel == CCGlobal || unit)
        SendMarker(channel, unit, marker);
    }
  }
  Display::OnChildDestroyed(idd, exit);
}

#if _ENABLE_IDENTITIES

static void InitDiaryTopicList(ControlsContainer *display, AutoArray<RString> &topics, Person *person, DiaryContext context = DCBriefing )
{
  if (!person)
  {
    topics.Clear();

    CListBox *list = dynamic_cast<CListBox *> (display->GetCtrl(IDC_DIARY_TOPIC_LIST));
    list->Clear();
    return;
  }

  // number of topics (restrict to available buttons)
  Identity &identity = person->GetIdentity();
  int n = identity.NDiarySubjects();

  CListBox *list = dynamic_cast<CListBox *> (display->GetCtrl(IDC_DIARY_TOPIC_LIST));
  if(!list) return;

  int currentStr = -1;
  if(list->GetCurSel()>=0) currentStr = list->GetValue(list->GetCurSel());

  topics.Realloc(n);
  topics.Resize(n);
  list->Clear();

  if(context != DCInGame)
  {
    int index = list->AddString(LocalizeString(IDS_USRACT_SHOW_MAP));
    list->SetValue(index,IDC_DIARY_TOPIC_MAP);
  }
  for (int i=0; i<n; i++)
  {
    DiaryPage *page = identity.GetDiarySubject(i);
    topics[i] = page->GetName();

    bool enable = !page->IsReadOnly();
    if (!enable)
    {
      // always enable writable pages (user can add record) 
      if (page->IsChanged()) page->Update(person);
      enable = page->NRecords() > 0;
    }

    if(enable || page->ShowEmpty())
    {
      RString name = page->GetName();
      if(context != DCInGame)
      {
        int index = list->AddString(page->GetDisplayName());
        list->SetValue(index,IDC_DIARY_TOPIC_1 + i);
        list->SetData(index,page->GetName());
      }
      else
      {
        if(stricmp(name,"tasks")==0 || (GWorld->GetMode()  == GModeNetware && stricmp(name,"players")==0))
        {
          int index = list->AddString(page->GetDisplayName());
          list->SetValue(index,IDC_DIARY_TOPIC_1 + i);
          list->SetData(index,page->GetName());
        }
      }
    }
  }
  if(currentStr >=0 && currentStr != list->GetValue(list->GetCurSel()))
  {
    for (int j=0; j< list->Size(); j++)
    {      
      if(currentStr == list->GetValue(j))
      {
        list->SetCurSel(j);
        break;
      }
    }
  }
}

static void UpdateDiaryTopicList(ControlsContainer *display, AutoArray<RString> &topics, Person *person, DiaryContext context = DCBriefing)
{
  InitDiaryTopicList(display, topics, person, context);
}

#endif // _ENABLE_IDENTITIES

DisplayMainMap::DisplayMainMap(ControlsContainer *parent)
  : DisplayMap(parent, "RscDisplayMainMap")
{
  _enableDisplay = false;

  ShowWarrant(false);
  ShowWalkieTalkie(false);
  ShowGPS(true);
#if _ENABLE_IDENTITIES
  _displayDiaryMap = DisplayDiaryInMap(this);
  _displayDiaryMap.SyncTopicList(0);  
  _displayDiaryMap.SyncDiary();
  CListBox *list = dynamic_cast<CListBox *> (this->GetCtrl(IDC_DIARY_TOPIC_LIST));
  if(list && list->Size()>0) 
  {//select map by default
    list->SetCurSel(0);
  }
  _displayDiaryMap.ResetListBox();
#endif
}

void DisplayMainMap::ResetHUD()
{
  DisplayMap::ResetHUD();
#if _ENABLE_IDENTITIES
  // switch diary to the current player
  _displayDiaryMap.UpdateOwner();
#endif
}

void DisplayMainMap::OnButtonClicked(int idc)
{
  // do not ignore Cancel command
  switch (idc)
  {
  case IDC_CANCEL:
    if (IsInGame()) GWorld->ShowMap(false);
    break;
  default:
    DisplayMap::OnButtonClicked(idc);
    break;
  }
#if _ENABLE_IDENTITIES
  _displayDiaryMap.OnButtonClicked(idc);
#endif
}

void DisplayMainMap::OnLBSelChanged(IControl *ctrl, int curSel)
{
#if _ENABLE_IDENTITIES
  _displayDiaryMap.OnLBSelChanged(ctrl, curSel);
#endif
  DisplayMap::OnLBSelChanged(ctrl, curSel);
}

void DisplayMainMap::OnLBDblClick(int idc, int curSel)
{
#if _ENABLE_IDENTITIES
  _displayDiaryMap.OnLBDblClick(idc,  curSel);
#endif
  DisplayMap::OnLBDblClick(idc,  curSel);
}

bool DisplayMainMap::OnKeyDown(int dikCode)
{
  return DisplayMap::OnKeyDown(dikCode);
}

bool DisplayMainMap::OnKeyUp(int dikCode)
{
  return DisplayMap::OnKeyUp(dikCode);
}

void DisplayMainMap::OnSimulate(EntityAI *vehicle)
{
#if _ENABLE_IDENTITIES
  // init buttons based on diary content
  _displayDiaryMap.OnSimulate(vehicle);
  _displayDiaryMap.UpdateTopics();
  _displayDiaryMap.UpdateDiary();
  _displayDiaryMap.ResetListBox();
#endif

  DisplayMap::OnSimulate(vehicle);
}

bool DisplayMainMap::OnHTMLLink(int idc, RString link)
{
#if _ENABLE_IDENTITIES
  return _displayDiaryMap.OnHTMLLink(idc,link, DCInGame);
#else
  return false;
#endif
}

void DisplayMainMap::OnChildDestroyed(int idd, int exit)
{
#if _ENABLE_IDENTITIES
  _displayDiaryMap.OnChildDestroyed(idd,exit, _child);
#endif
  DisplayMap::OnChildDestroyed(idd, exit);
}

void DisplayMainMap::ProcessDiaryLink(RString link,DiaryContext contex)
{
#if _ENABLE_IDENTITIES
  _displayDiaryMap.ProcessLink(link, contex);
#endif
}


DisplayInsertMarker::DisplayInsertMarker(ControlsContainer *parent, float x, float y, float cx, float cy, float cw, float ch, bool 

                                         enableSimulation)
                                         : Display(parent)
{
  GInput.ChangeGameFocus(+1); //call it as early as possible in order not to skip it by some possible return
  _enableSimulation = _enableSimulation && enableSimulation;

  Load("RscDisplayInsertMarker");

  // make sure chat in a screen which disabled display does not enable it
  _enableDisplay = _enableDisplay && (!parent || parent->DisplayEnabled());
  _x = x; _y = y;
  _cx = cx; _cy = cy; _cw = cw; _ch = ch;

  _picture = 0;
  _color = 0;
  UpdatePicture();

  Control *picture = dynamic_cast<Control *>(GetCtrl(IDC_INSERT_MARKER_PICTURE));
  Control *edit = dynamic_cast<Control *>(GetCtrl(IDC_INSERT_MARKER));
  if (picture && edit)
  {
    float wP = picture->W();
    float wE = edit->W();
    float w = wP + wE;
    float hP = picture->H();
    float hE = edit->H();
    float h = floatMax(hP, hE);
    y -= 0.5 * h;
    x -= 0.5 * wP;
    saturate(x, _cx, _cx + _cw - w);
    saturate(y, _cy, _cy + _ch - h);
    picture->SetPos(x, y + 0.5 * (h - hP), wP, hP);
    edit->SetPos(x + wP, y + 0.5 * (h - hE), wE, hE);
  }
}

DisplayInsertMarker::~DisplayInsertMarker()
{
  GInput.ChangeGameFocus(-1);
}

bool DisplayInsertMarker::OnKeyDown(int dikCode)
{
  switch (dikCode)
  {
  case DIK_DOWN:
    if (GInput.keys[DIK_LSHIFT] || GInput.GetKey(DIK_RSHIFT))
      NextColor();
    else
      NextPicture();
    UpdatePicture();
    return true;
  case DIK_UP:
    if (GInput.keys[DIK_LSHIFT] || GInput.GetKey(DIK_RSHIFT))
      PrevColor();
    else
      PrevPicture();
    UpdatePicture();
    return true;
  }
  return Display::OnKeyDown(dikCode);
}

bool DisplayInsertMarker::OnKeyUp(int dikCode)
{
  switch (dikCode)
  {
  case DIK_DOWN:
  case DIK_UP:
    return true;
  }
  return Display::OnKeyUp(dikCode);
}

void DisplayInsertMarker::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_CANCEL:
  case IDC_OK:
    Exit(idc);
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayInsertMarker::Destroy()
{
  Display::Destroy();

  if (_exit != IDC_OK) return;

  CEdit *edit = dynamic_cast<CEdit *>(GetCtrl(IDC_INSERT_MARKER));
  if (edit)
    _text = edit->GetText();
}

void DisplayInsertMarker::UpdatePicture()
{
  CStatic *ctrl = dynamic_cast<CStatic *>(GetCtrl(IDC_INSERT_MARKER_PICTURE));
  if (ctrl)
  {
    ParamEntryVal markers = Pars >> "CfgMarkers";
    if (_picture < 0 || _picture >= markers.GetEntryCount()) return;
    ParamEntryVal colors = Pars >> "CfgMarkerColors";
    if (_color < 0 || _color >= colors.GetEntryCount()) return;

    ParamEntryVal markerCfg = markers.GetEntry(_picture);
    if( (int)(markerCfg >> "scope") <2) 
    {
      NextPicture();
      markerCfg = markers.GetEntry(_picture);
    }
    RString picture = markerCfg >> "icon";
    ParamEntryVal colorCfg = colors.GetEntry(_color);
    PackedColor color;
    if (stricmp(colorCfg.GetName(), "default") == 0)
      color = GetPackedColor(markerCfg >> "color");
    else
      color = GetPackedColor(colorCfg >> "color");
    ctrl->SetText(picture);
    ctrl->SetTextColor(color);
  }
}

void DisplayInsertMarker::PrevPicture()
{
  ParamEntryVal markers = Pars >> "CfgMarkers";
  int n = markers.GetEntryCount();
  int loop = _picture;
  int scope = 0;

  do 
  { 
    _picture--;
    if (_picture < 0) _picture = n - 1;
    ParamEntryVal entry = markers.GetEntry(_picture);
    scope = entry >> "scope";
  } while (scope<2 && loop != _picture);
}

void DisplayInsertMarker::NextPicture()
{
  ParamEntryVal markers = Pars >> "CfgMarkers";
  int n = markers.GetEntryCount();
  int loop = _picture;
  int scope = 0;

  do 
  { 
    _picture++;
    if (_picture >= n) _picture = 0;
    ParamEntryVal entry = markers.GetEntry(_picture);
    scope = entry >> "scope";
  } while (scope<2 && loop != _picture);
}

void DisplayInsertMarker::PrevColor()
{
  ParamEntryVal colors = Pars >> "CfgMarkerColors";
  int n = colors.GetEntryCount();
  int loop = _color;
  int scope = 0;

  do 
  { 
    _color--;
    if (_color < 0) _color = n - 1;
    ParamEntryVal entry = colors.GetEntry(_color);
    scope = entry >> "scope";
  } while (scope==1 && loop != _color);
}

void DisplayInsertMarker::NextColor()
{
  ParamEntryVal colors = Pars >> "CfgMarkerColors";
  int n = colors.GetEntryCount();
  int loop = _color;
  int scope = 0;

  do 
  {
    _color++;
    if (_color >= n) _color = 0;
    ParamEntryVal entry = colors.GetEntry(_color);
    scope = entry >> "scope";
  } while (scope==1 && loop != _color);
}


#if _ENABLE_IDENTITIES

DisplayDiary::DisplayDiary(Display *parent, RString link, DiaryContext context)
  : Display(parent)
{
  //GInput.ChangeGameFocus(+1); //call it as early as possible in order not to skip it by some possible return
  _enableUI = true;

  _alwaysShow = true;
  _context = DCInGame;
  _parent = parent;

  Load("RscDisplayDiary");
  _enableSimulation = true;

  // load list of index textures
  ParamEntryVal list = Pars >> "CfgDiary" >> "Icons";
  for (int i=0; i<list.GetEntryCount(); i++)
  {
    ParamEntryVal entry = list.GetEntry(i);
    Ref<Texture> texture = GlobLoadTexture(GetPictureName(entry));
    if (texture) _indexTextures.Add(TextureByName(entry.GetName(), texture));
  }

  // for backward compatibility, handle content of briefing.html (notes, plan, debriefing)
  RString briefing = GetBriefingFile();
  if (briefing.GetLength() > 0) _briefing.Load(briefing);

  AIBrain *unit = GWorld->FocusOn();
  _diaryPerson = unit ? unit->GetPerson() : NULL;

  if(_diaryPerson) InitDiaryTopicList(this, _topics, _diaryPerson, DCInGame);


  //diary topics list
  _diaryTopics = dynamic_cast<CListBox *> (GetCtrl(IDC_DIARY_TOPIC_LIST));
  if(_diaryTopics) 
  {
    _diaryTopics->EnableQuickSearch(false);
    _maxDiaryTopicsH = _diaryTopics->H();
  }
  //diary sub-topics list
  _diaryIndex = dynamic_cast<CListBox *> (GetCtrl(IDC_DIARY_TOPIC_LISTINDEX));
  if(_diaryIndex) 
  {
    _diaryIndex->EnableQuickSearch(false);
    _maxDiaryIndexH = _diaryIndex->H();
  }
  //diary HTML text
  _diary = dynamic_cast<CHTML *> (GetCtrl(IDC_DIARY_TOPIC_HTML));
  if(_diary) _maxDiaryH = _diary->H();

  IControl* ctrl = GetCtrl(IDC_DIARY_TOPIC_ADD_RECORD);
  if(ctrl) 
  {
    _diaryAdd = GetTextContainer(ctrl);
    ctrl->ShowCtrl(false);
  }
  //HMTML container
  _controlsGroup = dynamic_cast<CControlsGroup *> (GetCtrl(IDC_DIARY_TOPIC_HTML_GROUP));
  if(_controlsGroup)
    _maxControlsGroupH = _controlsGroup->H();
  //mission description
  _playerName = dynamic_cast<ITextContainer *> (GetCtrl(IDC_DIARY_PLAYER_NAME));
  _currentTask = dynamic_cast<ITextContainer *>(GetCtrl(IDC_DIARY_CURRENT_TASK));
  _currentMission = dynamic_cast<ITextContainer *> (GetCtrl(IDC_DIARY_MISSION_NAME));

  if(_currentMission)
  {//get mission name
    RString name;
    if (GWorld->GetMode() == GModeNetware)
    {
      const MissionHeader *header = GetNetworkManager().GetMissionHeader();
      if (header) name = header->GetLocalizedMissionName();
    }
    if (name.GetLength() == 0)
    {
      name = Localize(CurrentTemplate->Intel().briefingName);
      if (name.GetLength() == 0)
      {
        name = Glob.header.filenameReal;
        // user mission file name is encoded
        if (IsUserMission()) name = DecodeFileName(name);
      }
    }
    _currentMission->SetText(name);
  }

  if (_diaryPerson)
  {
    if (link.GetLength() > 0) ProcessLink(link, DCInGame);
  }

  for (int i= 0; i< _diaryTopics->Size();i++)
  {
    if (stricmp(_diaryTopics->GetData(i),link)==0)
    {
      _diaryTopics->SetCurSel(i);
      return;
    }
  }

  SyncDiary();
  UpdateDiary();
  ResetListBox();

  if (_diaryIndex) FocusCtrl(_diaryIndex->GetControl());
}

DisplayDiary::~DisplayDiary()
{
  GInput.ChangeGameFocus(-1);
}

Control *DisplayDiary::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
#if _ENABLE_IDENTITIES
  case IDC_DIARY_TOPIC_LIST:
  case IDC_DIARY_TOPIC_LISTINDEX:
    {
      CMapListBox *mapList = new CMapListBox(this,idc,cls);
      return mapList;
    }
  case IDC_DIARY_TOPIC_HTML_GROUP:
    {
      CMapCGroup *cgroup  = new CMapCGroup(this,idc,cls);
      return cgroup;
    }
#endif
  }
  return Display::OnCreateCtrl(type, idc, cls);
}

void DisplayDiary::ResetListBox()
{ //resize index list and it's background
  //bool showMenu = GWorld->UI()->IsMenuVisible();

  if(_diaryIndex)
  { 
    Control *ctrl = dynamic_cast<Control *> (GetCtrl(IDC_DIARY_SUBTOPIC_BACKGROUND));
    if(_diaryIndex->Size()<=0) 
    {
      _diaryIndex->ShowCtrl(false);
      if(ctrl) ctrl->ShowCtrl(false);
    }
    else 
    {//hide if empty
      if(!_diaryIndex->IsVisible()) _diaryIndex->ShowCtrl(true);
      if(ctrl) ctrl->ShowCtrl(true);
      float size = floatMin(0.002f +_diaryIndex->Size() * _diaryIndex->GetRowHeight(),_maxDiaryIndexH);
      _diaryIndex->SetPos(_diaryIndex->X(),_diaryIndex->Y(),_diaryIndex->W(),size);
      if(ctrl) ctrl->SetPos(ctrl->X(),ctrl->Y(),ctrl->W(),size+0.04);
    }  
  }
  //reset HMTL control and it's background
  if (_diary)
  {
    Control *ctrl = dynamic_cast<Control *> (GetCtrl(IDC_DIARY_CONTENT_BACKGROUND));
    float size = 0;

    if(_diary->NSections()>0)
    {
      HTMLSection &section = _diary->GetSection(_diary->CurrentSection());
      //get HTML control height
      for (int r=0; r<section.rows.Size(); r++)
      {
        HTMLRow &row = section.rows[r];
        size += row.height;
      }
    }
    //if HTML control is visible and !empty
    if(_diary->NSections()>0 && size>0)
    {
      size = floatMin(size,_maxDiaryH);
      _diary->ShowCtrl(true);       
      _diary->SetPos(_diary->X(),_diary->Y(),_diary->W(),size);
      if(_controlsGroup)
      {
        _controlsGroup->ShowCtrl(true);
        size = floatMin(size,_maxControlsGroupH);
        _controlsGroup->SetPos(_controlsGroup->X(),_controlsGroup->Y(),_controlsGroup->W(),size+0.005f);
        if(ctrl)
        {
          ctrl->ShowCtrl(true);
          ctrl->SetPos(ctrl->X(),ctrl->Y(),ctrl->W(),size+0.005f+0.04);
        }
      }
    }
    else  
    {//hide (if not hidden)
      if(_controlsGroup->IsVisible())
      {
        if(_controlsGroup)_controlsGroup->ShowCtrl(false);
        _diary->ShowCtrl(false);
        if(ctrl) ctrl->ShowCtrl(false);

        IControl *ctrl = GetCtrl(IDC_DIARY_TOPIC_LIST);
        if(ctrl) FocusCtrl(ctrl);
      }
    }
  }
  //resize topic listbox and it's background
  if(_diaryTopics)
  {
    Control *ctrl = dynamic_cast<Control *> (GetCtrl(IDC_DIARY_TOPIC_BACKGROUND));
    float size = floatMin(0.002f + _diaryTopics->Size() * _diaryTopics->GetRowHeight(),_maxDiaryTopicsH);
    _diaryTopics->SetPos(_diaryTopics->X(),_diaryTopics->Y(),_diaryTopics->W(),size);
    if(!_diaryTopics->IsVisible())  _diaryTopics->ShowCtrl(true);
    if(ctrl)
    {
      ctrl->SetPos(ctrl->X(), ctrl->Y(), ctrl->W(), size+0.04);
      ctrl->ShowCtrl(true);
    }
  }
  else
  {
    Control *ctrl = dynamic_cast<Control *> (GetCtrl(IDC_DIARY_TOPIC_BACKGROUND));
    if(_diaryTopics)
      _diaryTopics->ShowCtrl(false);
    if(ctrl) ctrl->ShowCtrl(false);
  }
}


void DisplayDiary::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_DIARY_TOPIC_ADD_RECORD:
    {//add diary record
      RString title;
      if (_diaryIndex)
      {
        int sel = _diaryIndex->GetCurSel();
        if (sel >= 0) title = _diaryIndex->GetText(sel);
      }
      CreateChild(new DisplayEditDiaryRecord(this, -1, title, RString()));
      return;
    }
  }
  Display::OnButtonClicked(idc);
}

bool DisplayDiary::OnKeyDown(int dikCode)
{
  switch (dikCode)
  {
  case INPUT_DEVICE_XINPUT + XBOX_A:
    // navigation on Xbox
    if (GetFocused() && _diaryIndex && GetFocused() == _diaryIndex->GetControl())
    {
      return true;
    }
    break;
  case INPUT_DEVICE_XINPUT + XBOX_B:
    // navigation on Xbox
    if (GetFocused() && _diary && GetFocused() == _diary->GetControl())
    {
      return true;
    }
    break;;
  }
  return Display::OnKeyDown(dikCode);
}

bool DisplayDiary::OnKeyUp(int dikCode)
{
  switch (dikCode)
  {
  case INPUT_DEVICE_XINPUT + XBOX_A:
    // navigation on Xbox
    if (GetFocused() && _diaryIndex && GetFocused() == _diaryIndex->GetControl())
    {
      if (_diary) FocusCtrl(_diary->GetControl());
      return true;
    }
    break;;
  case INPUT_DEVICE_XINPUT + XBOX_B:
    // navigation on Xbox
    if (GetFocused() && _diary && GetFocused() == _diary->GetControl())
    {
      if (_diaryIndex) FocusCtrl(_diaryIndex->GetControl());
      return true;
    }
    break;;
  }
  return Display::OnKeyUp(dikCode);
}

void DisplayDiary::OnLBSelChanged(IControl *ctrl, int curSel)
{
  if(ctrl->IDC() == IDC_DIARY_TOPIC_LIST && _diaryTopics)
  {
    int diary_idc = _diaryTopics->GetValue(curSel);
    if(diary_idc>=IDC_DIARY_TOPIC_1)
    {
      RString link = _topics[diary_idc - IDC_DIARY_TOPIC_1];
      ProcessLink(link);
    }
    else if(diary_idc==IDC_DIARY_TOPIC_MAP) ClearLink();
    ResetListBox();
  }
  else if (ctrl->IDC() == IDC_DIARY_TOPIC_LISTINDEX)
  {
    SyncDiary();
    ResetListBox();
    return;
  }
  Display::OnLBSelChanged(ctrl, curSel);
}


void DisplayDiary::ClearLink()
{//hide everything to see only map 
  SelectPage("");
  ProcessLink("");
}

bool DisplayDiary::OnHTMLLink(int idc, RString link)
{
  const char *ptr = link;
  const char *name = "marker:";
  int n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    ptr += n;
    for (int i=0; i<markersMap.Size(); i++)
    {
      ArcadeMarkerInfo &mInfo = markersMap[i];
      if (stricmp(ptr, mInfo.name) == 0)
      {
        if (_parent)
        {
          //_map->AnimateTo(mInfo.position);
          //_map->SetActiveMarker(i);
          //   Exit(IDC_AUTOCANCEL);
        }
        break;
      }
    }
    return true;
  }
  name = "edit:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    ptr += n;
    int recordId = atoi(ptr);
    if (_diaryPerson)
    {
      RString subject = GetCurrentPage();
      if (subject.GetLength() > 0)
      {
        Identity &identity = _diaryPerson->GetIdentity();
        const DiaryPage *page = identity.FindDiarySubject(subject);
        if (page && !page->IsReadOnly())
        {
          DiaryRecord *record = page->FindRecord(recordId);
          if (record && !record->IsReadOnly()) CreateChild(new DisplayEditDiaryRecord(this, recordId, record->GetTitle(),

            record->GetText()));
        }
      }
    }
    return true;
  }
  name = "delete:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    ptr += n;
    int recordId = atoi(ptr);
    if (_diaryPerson)
    {
      RString subject = GetCurrentPage();
      Identity &identity = _diaryPerson->GetIdentity();
      if (subject.GetLength() > 0 && !identity.IsDiaryReadOnly(subject))
      {
        _toDeleteSubject = subject;
        _toDeleteRecord = recordId;
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
        CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_SURE),
          IDD_MSG_DELETE_DIARY_RECORD, false, &buttonOK, &buttonCancel);
        identity.DeleteDiaryRecord(subject, recordId);
      }
    }
    return true;
  }
  name = "currentTask:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    ptr += n;
    int taskId = atoi(ptr);
    if (_diaryPerson)
    {
      Identity &identity = _diaryPerson->GetIdentity();
      Task *task = identity.FindTask(taskId);
      Task *current = identity.GetCurrentTask();
      if (task)
      {
        if(current && current->GetState() == TSAssigned ) current->SetState(TSCreated);
        identity.SetCurrentTask(task);
        task->SetState(TSAssigned);
      }
    }
    return true;
  }
  name = "gear:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    ptr += n;
    ObjectId id; id.Decode(atoi(ptr));
    Object *obj = GWorld->FindObject(id);
    Person *person = dyn_cast<Person>(obj);
    if (person && person->Brain())
    {
      if (_context == DCInGame)
        CreateChild(CreateInventoryDialog(NULL, person->Brain(), NULL));
    }
    return true;
  }
  name = "kick:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    if (_context == DCInGame)
    {
      ptr += n;
      int dpnid = atoi(ptr);
      if (GetNetworkManager().IsServer())
      {
        if (dpnid != GetNetworkManager().GetPlayer())
          GetNetworkManager().KickOff(dpnid, KORKick);
      }
      else if (GetNetworkManager().IsGameMaster())
      {
        if (dpnid != GetNetworkManager().GetPlayer())
          GetNetworkManager().SendKick(dpnid);
      }
    }
    return true;
  }
  name = "ban:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    if (_context == DCInGame)
    {
      ptr += n;
      int dpnid = atoi(ptr);
      if (GetNetworkManager().IsServer())
      {
        if (dpnid != GetNetworkManager().GetPlayer())
          GetNetworkManager().Ban(dpnid);
      }
    }
    return true;
  }
  name = "mute:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    if (_context == DCInGame)
    {
      ptr += n;
      int dpnid = atoi(ptr);
      if (dpnid != GetNetworkManager().GetPlayer())
      {
        const PlayerIdentity *identity = GetNetworkManager().FindIdentity(dpnid);
        if (identity) GetNetworkManager().TogglePlayerMute(identity->xuid);
      }
    }
    return true;
  }
  name = "execute:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    ptr += n;

    GameState *state = GWorld->GetGameState();

    GameVarSpace vars(false);
    int gshandle = state->BeginContext(&vars);
    state->Execute(ptr, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
    state->EndContext(gshandle);

    return true;
  }
  name = "executeClose:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    ptr += n;

    GameState *state = GWorld->GetGameState();

    GameVarSpace vars(false);
    int gshandle = state->BeginContext(&vars);
    state->Execute(ptr, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
    state->EndContext(gshandle);

    GWorld->ShowMap(false);
    // Exit(IDC_AUTOCANCEL);
    return true;
  }

  // handle as an external link
  ProcessLink(link);
  return true;
}

void DisplayDiary::OnChildDestroyed(int idd, int exit)
{
  if (idd == IDD_EDIT_DIARY_RECORD)
  {
    if (exit == IDC_OK)
    {
      if (_diaryPerson)
      {
        ResizeHTML();
        RString page = GetCurrentPage();
        if (page.GetLength() > 0)
        {
          Identity &identity = _diaryPerson->GetIdentity();
          if (!identity.IsDiaryReadOnly(page))
          {
            DisplayEditDiaryRecord *disp = dynamic_cast<DisplayEditDiaryRecord *>((ControlsContainer *)_child);
            Assert(disp);
            int recordId = disp->GetRecordId();
            RString title = disp->GetTitle();
            RString text = disp->GetText();
            if (recordId >= 0) identity.EditDiaryRecord(page, recordId, title, text);
            else identity.AddDiaryRecord(page, title, text, NULL, TSNone, NULL, NULL, false); // not read-only
          }
        }
      }
    }
    UpdateDiary();
  }
  else if (idd == IDD_MSG_DELETE_DIARY_RECORD)
  {
    if (exit == IDC_OK && _diaryPerson)
    {
      Identity &identity = _diaryPerson->GetIdentity();
      identity.DeleteDiaryRecord(_toDeleteSubject, _toDeleteRecord);
    }
    UpdateDiary();
  }
  ResetListBox();
  Display::OnChildDestroyed(idd, exit);
}

void DisplayDiary::OnDraw(EntityAI *vehicle, float alpha)
{
  UpdateDiaryTopicList(this, _topics, _diaryPerson, _context);
  if (_diaryPerson)
  {
    if (_diary) UpdateDiary();
  }
  UpdateButtons();
  Display::OnDraw(vehicle, alpha);
}

void DisplayDiary::OnSimulate(EntityAI *vehicle)
{
  //unit is dead, close diary
  if(!GWorld->FocusOn() || !GWorld->FocusOn()->LSIsAlive())
  {
    Exit(IDC_CANCEL);
    return;
  }

  InitDiaryTopicList(this, _topics, _diaryPerson,DCInGame);
  UpdateDiary();
  ResetListBox();

  ////close diary when 'j' is pressed ('j' is default key for open diary)
  if( GInput.GetActionToDo(UADiary, true, false, false, 2) )
  {
    CListBox *list = dynamic_cast<CListBox *> (GetCtrl(IDC_DIARY_TOPIC_LIST));

    if (stricmp(list->GetData(list->GetCurSel()),"tasks")==0)
      Exit(IDC_AUTOCANCEL);
    else
    {
      for (int i= 0; i< list->Size();i++)
      {
        if (stricmp(list->GetData(i),"tasks")==0)
        {
          list->SetCurSel(i);
          return;
        }
      }
      Exit(IDC_AUTOCANCEL);
    }
  }
  if(GWorld->GetMode()  == GModeNetware)
  {
    //close diary when 'j' is pressed ('j' is default key for open diary)
    if( GInput.GetActionToDo(UANetworkPlayers, true, false, false, 2) )
    {
      CListBox *list = dynamic_cast<CListBox *> (GetCtrl(IDC_DIARY_TOPIC_LIST));

      if (stricmp(list->GetData(list->GetCurSel()),"players")==0)
        Exit(IDC_AUTOCANCEL);
      else
      {
        for (int i= 0; i< list->Size();i++)
        {
          if (stricmp(list->GetData(i),"players")==0)
          {
            list->SetCurSel(i);
            return;
          }
        }
        Exit(IDC_AUTOCANCEL);
      }
    }
  }

  if (GInput.GetActionDo(UAHideMap, false))
    Exit(IDC_AUTOCANCEL);

  //if (GInput.GetActionDo(UAGear, false))
  //  Exit(1003);

  Display::OnSimulate(vehicle);
}

static int FindItemWithData(const CListBoxContainer *list, RString data)
{
  for (int i=0; i<list->GetSize(); i++)
  {
    if (strcmp(list->GetData(i), data) == 0) return i;
  }
  return -1;
}

bool DisplayDiary::SelectPage(RString name)
{
  if (_diaryPerson)
  {
    _diaryPerson->GetIdentity().SetCurrentDiarySubject(name);
    UpdateDiary(true);
    return true;
  }
  return false;
}

void DisplayDiary::ProcessLink(RString link,DiaryContext context)
{
  _context = context;
  AIBrain *unit = GWorld->FocusOn();
  _diaryPerson = unit ? unit->GetPerson() : NULL;

  // load list of index textures
  ParamEntryVal list = Pars >> "CfgDiary" >> "Icons";
  for (int i=0; i<list.GetEntryCount(); i++)
  {
    ParamEntryVal entry = list.GetEntry(i);
    Ref<Texture> texture = GlobLoadTexture(GetPictureName(entry));
    if (texture) _indexTextures.Add(TextureByName(entry.GetName(), texture));
  }

  InitDiaryTopicList(this, _topics, _diaryPerson, context);
  ProcessLink(link);
}

void DisplayDiary::ProcessLink(RString link)
{
  // Unlike OnHTMLLink, global (external) links to diary are processed here
  const char *ptr = link;

  ResizeHTML();

  // link to the task
  const char *name = "task:";
  int n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    ptr += n;
    int taskId = atoi(ptr);
    if (_diaryPerson)
    {
      Identity &identity = _diaryPerson->GetIdentity();
      Task *task = identity.FindTask(taskId);
      if (task)
      {
        // which page contains the list of tasks
        RString pageName;
        for (int i=0; i<identity.NDiarySubjects(); i++)
        {
          const DiaryPage *page = identity.GetDiarySubject(i);
          if (strcmp(page->GetPageType(), "DiaryPageTasks") == 0)
          {
            pageName = page->GetName();
            break;
          }
        }

        if (pageName.GetLength() > 0)
        {
          // switch to the correct page
          SelectPage(pageName);

          // switch to the correct record
          if (_diaryIndex)
          {
            // find the record in the index, select it and synchronize the content
            RString name = Format("Task%d", taskId);
            int i = FindItemWithData(_diaryIndex, name);
            if (i >= 0)
            {
              _diaryIndex->SetCurSel(i);
              SyncDiary();
            }
          }
        }
      }
    }
    return;
  }

  // link to the diary page / diary record
  if (_diaryPerson)
  {
    Identity &identity = _diaryPerson->GetIdentity();
    for (int i=0; i<identity.NDiarySubjects(); i++)
    {
      const DiaryPage *page = identity.GetDiarySubject(i);
      RString name = page->GetName();
      if (stricmp(ptr, name) == 0)
      {
        // switch to the correct page
        SelectPage(name);
      }
      else
      {
        RString prefix = name + RString(":");
        int n = prefix.GetLength();
        if (strnicmp(ptr, prefix, n) == 0)
        {
          ptr += n;
          RString recordId = ptr;

          // TODO: switch sort order by the id type

          // switch to the correct page
          SelectPage(name);

          // switch to the correct record
          if (_diaryIndex)
          {
            // find the record in the index, select it and synchronize the content
            int i = FindItemWithData(_diaryIndex, recordId);
            if (i >= 0)
            {
              _diaryIndex->SetCurSel(i);
              SyncDiary();
            }
          }
        }
      }
    }
  }
}

struct DiaryParserStackItem
{
  RString _name;
  XMLAttributes _attributes;
  //text properties for HTMLContainer
  HTMLFormat format;  //html format (headline, paragraph,..)
  PackedColor color;  //specific color, used only if colored == true
  bool colored;       //used, because i don't know default color yet 
  RString fontName;   //font type
  float fontSize;     //font size (number of pixels / 408)
};
TypeIsMovable(DiaryParserStackItem)

  /// XML parser used to convert XML-based diary text to the HTML control
class DiaryParser : public SAXParser
{
protected:
  CHTMLContainer *_html;
  int _section;

  /// this enable to keep context of the text segment
  AutoArray<DiaryParserStackItem> _stack;

public:
  DiaryParser(CHTMLContainer *html, int section);
  void OnStartElement(RString name, XMLAttributes &attributes);
  void OnEndElement(RString name);
  void OnCharacters(RString chars);

protected:
  RString GetReference(const DiaryParserStackItem &item) const;
};

DiaryParser::DiaryParser(CHTMLContainer *html, int section)
  : _html(html), _section(section)
{//by default is trim=true and then when font props. are changed, spaces beetween words are erased
  _trim = false;
}

void DiaryParser::OnStartElement(RString name, XMLAttributes &attributes)
{
  // add the context to the stack
  DiaryParserStackItem &item = _stack.Append();
  item._name = name;
  item._attributes = attributes;

  // attribute "image" in any tag will force the image instead text
  const XMLAttribute *attr = item._attributes.Find("image");
  if (attr)
  {
    RString image = attr->value;
    float wImage = -1, hImage = -1; 
    attr = item._attributes.Find("width");
    if (attr) wImage = atoi(attr->value);
    attr = item._attributes.Find("height");
    if (attr) hImage = atoi(attr->value);

    _html->AddImage(_section, image, HALeft, false, wImage, hImage, GetReference(item));
  }


  if (_stack.Size() > 1)
  {//if there are at least two items(last one is current), take font properties from previous 
    const DiaryParserStackItem &previousItem = _stack[_stack.Size() - 2]; //_stack.Size() - 1 is current
    item.colored = previousItem.colored;
    item.fontName = previousItem.fontName;
    item.fontSize = previousItem.fontSize;
    item.color = previousItem.color;
    item.format = previousItem.format;
  }
  else
  {//if this is the first item in stack, set default font properties 
    item.colored = false; 
    item.fontName = "";  
    item.fontSize = -1; 
    item.format = HFP;
  }

  //set format type
  if (stricmp(name, "p") == 0)
  {//paragraph
    item.format = HFP;
  }
  if (stricmp(name, "h1") == 0)
  {//headline
    item.format = HFH1;
  }
  else if (stricmp(name, "h2") == 0)
  {
    item.format = HFH2;
  }
  else if (stricmp(name, "h3") == 0)
  {
    item.format = HFH3;
  }
  else if (stricmp(name, "h4") == 0)
  {
    item.format = HFH4;
  }
  else if (stricmp(name, "h5") == 0)
  {
    item.format = HFH5;
  }
  else if (stricmp(name, "h6") == 0)
  {
    item.format = HFH6;
  }
  else if (stricmp(name, "font") == 0)
  {//read specific font properties
    attr = item._attributes.Find("color");
    if (attr) 
    {
      item.color =  HTMLContent::ParseColorHexa(attr->value); //convert #FFFFFF to PackedColor
      item.colored = true;
    }
    attr = item._attributes.Find("size");       
    if (attr) item.fontSize = atoi(attr->value)/408.0f;  //408.0f - see commonDefs.hpp
    attr = item._attributes.Find("face");   
    if (attr) item.fontName = attr->value;               //font name (font is loaded later)
  }
}

void DiaryParser::OnEndElement(RString name)
{
  // check the syntax
  int n = _stack.Size();
  if (n == 0)
  {
    RptF("DiaryParser: <%s/> without corresponding <%s>", cc_cast(name), cc_cast(name));
    return;
  }
  DiaryParserStackItem &item = _stack[n - 1];
  if (strcmp(name, item._name) != 0)
  {
    RptF("DiaryParser: <%s/> does not match to <%s>", cc_cast(name), cc_cast(item._name));
    return;
  }

  // handle some special tags
  if (strcmp(name, "br") == 0)
  {
    _html->AddBreak(_section, false);
  }

  // remove the context from the stack
  _stack.Resize(n - 1);
}

void DiaryParser::OnCharacters(RString chars)
{
  RString href;
  int n = _stack.Size();
  if (n > 0)
  {
    const DiaryParserStackItem &item = _stack[n - 1];

    // when image attribute present, tag was already processed (during OnStartElement)
    const XMLAttribute *attr = item._attributes.Find("image");
    if (attr) return;

    href = GetReference(item);
    //if possible, use font properties from DiaryParserStackItem
    _html->AddText(_section, chars, HFP, HALeft, false, false, href,0,0,item.colored,item.color,item.fontSize,item.fontName);
  }
  else _html->AddText(_section, chars, HFP, HALeft, false, false, href);
}

RString DiaryParser::GetReference(const DiaryParserStackItem &item) const
{
  if (strcmp(item._name, "marker") == 0) // marker in the map
  {
    const XMLAttribute *attr = item._attributes.Find("name");
    if (attr) return RString("marker:") + attr->value;
  }
  else if (strcmp(item._name, "currentTask") == 0) // set as the current task
  {
    const XMLAttribute *attr = item._attributes.Find("id");
    if (attr) return RString("currentTask:") + attr->value;
  }
  else if (strcmp(item._name, "gear") == 0) // access the gear screen
  {
    const XMLAttribute *attr = item._attributes.Find("unit");
    if (attr) return RString("gear:") + attr->value;
  }
  else if (strcmp(item._name, "teamSwitch") == 0) // switch to the unit
  {
    const XMLAttribute *attr = item._attributes.Find("unit");
    if (attr) return RString("teamSwitch:") + attr->value;
  }
  else if (strcmp(item._name, "kick") == 0) // kick off the player
  {
    const XMLAttribute *attr = item._attributes.Find("id");
    if (attr) return RString("kick:") + attr->value;
  }
  else if (strcmp(item._name, "ban") == 0) // ban the player
  {
    const XMLAttribute *attr = item._attributes.Find("id");
    if (attr) return RString("ban:") + attr->value;
  }
  else if (strcmp(item._name, "mute") == 0) // mute the player
  {
    const XMLAttribute *attr = item._attributes.Find("id");
    if (attr) return RString("mute:") + attr->value;
  }
  else if (strcmp(item._name, "log") == 0) // common reference to the log
  {
    const XMLAttribute *attr1 = item._attributes.Find("subject");
    const XMLAttribute *attr2 = item._attributes.Find("record");
    if (attr1 && attr2) return attr1->value + RString(":") + attr2->value;
  }
  else if (strcmp(item._name, "execute") == 0) // execute the expression
  {
    const XMLAttribute *attr = item._attributes.Find("expression");
    if (attr) return RString("execute:") + attr->value;
  }
  else if (strcmp(item._name, "executeClose") == 0) // execute the expression
  {
    const XMLAttribute *attr = item._attributes.Find("expression");
    if (attr) return RString("executeClose:") + attr->value;
  }
  return RString();
}

class WriteToDiary
{
protected:
  mutable CListBoxContainer *_index;
  mutable CHTMLContainer *_text;
  mutable int _section;
  mutable const DiaryRecord *_last;
  const DiaryPage *_page;
  const TexturesByName &_indexTextures;
  const Identity &_identity;
  RString _selected;

public:
  WriteToDiary(CListBoxContainer *index, CHTMLContainer *text, const DiaryPage *page, const TexturesByName &indexTextures, const 

    Identity &identity, RString selected)
    : _indexTextures(indexTextures), _identity(identity)
  {
    _index = index;
    _text = text;
    _section = -1;
    _last = NULL;
    _page = page;
    _selected = selected;
  }
  bool operator ()(int index, const DiaryRecord *record) const;
  void Finish();
};

bool WriteToDiary::operator ()(int index, const DiaryRecord *record) const
{
  // record internal name - used for navigation
  RString name = Format("Record%d", record->GetId());
  // for some dynamic pages, records are better identified by different data than record ID
  DiarySortOrder sortOrder = _page->GetSortOrder();
  switch (sortOrder)
  {
  case DSOTask:
    {
      const Task *task = record->GetTask();
      if (task) name = Format("Task%d", task->GetId());
    }
    break;
  case DSOUnit:
    {
      const AIBrain *unit = record->GetUnit();
      if (unit && unit->GetPerson())
      {
        OIDInt id = unit->GetPerson()->GetObjectId().Encode();
#if _HELISIM_LARGE_OBJECTID
        name = Format("Unit%I64d",id);
#else
        name = Format("Unit%d", id);
#endif
      }
    }
    break;
  }

  // title of the current record in index and text
  RString indexTitle = _page->GetIndexTitle(record);
  RString recordTitle = _page->GetRecordTitle(record);
  bool indexChanged = true;
  if (_last)
  {
    switch (sortOrder)
    {
    case DSONone:
    case DSODate:
      indexChanged = strcmp(indexTitle, _page->GetIndexTitle(_last)) != 0;
      break;
    case DSOTask:
      indexChanged = record->GetTask() != _last->GetTask();
      break;
    case DSOUnit:
      indexChanged = record->GetUnit() != _last->GetUnit();
      break;
    }
  }

  if (indexChanged)
  {
    // index title changed, new HTML section and an index item needed
    if (_index)
    {
      // create new item in index
      int index = _index->AddString(indexTitle);
      _index->SetData(index, name);
      // set icon to the item
      RString indexIcon = _page->GetIndexIcon(record);
      if (indexIcon.GetLength() > 0)
      {
        const TextureByName &texture = _indexTextures[indexIcon];
        _index->SetTexture(index, texture._texture);
      }
    }

    // finish current section
    if (_section >= 0) _text->FormatSection(_section);
    // create a new one
    _section = _text->AddSection();

    // write the record title always on the new page
    _last = NULL;
  }

  // enable navigation in HTML
  _text->AddName(_section, name);

  // record title
  if (recordTitle.GetLength() > 0 && (!_last || strcmp(recordTitle, _page->GetRecordTitle(_last)) != 0))
  {
    _text->AddText(_section, recordTitle, HFH1, HALeft, false, false, RString());
    _text->AddBreak(_section, false);
  }

  // record text
  _text->SetIndent(0.03);
  RString text = _page->GetRecordText(record);
  // interpret the text as a XML description
  DiaryParser parser(_text, _section);
  QIStrStream in(cc_cast(text), text.GetLength());
  parser.Parse(in);
  // actions
  if (!_page->IsReadOnly() && !record->IsReadOnly())
  {
    _text->AddText(_section, " [", HFP, HALeft, false, false, RString());
    _text->AddText(_section, LocalizeString(IDS_LOG_EDIT), HFP, HALeft, false, false, Format("edit:%d", record->GetId()));
    _text->AddText(_section, "]", HFP, HALeft, false, false, RString());

    _text->AddText(_section, "[", HFP, HALeft, false, false, RString());
    _text->AddText(_section, LocalizeString(IDS_LOG_DELETE), HFP, HALeft, false, false, Format("delete:%d", record->GetId()));
    _text->AddText(_section, "]", HFP, HALeft, false, false, RString());
  }
  // finish
  _text->SetIndent(0);
  _text->AddBreak(_section, false);
  if (sortOrder != DSONone) _text->AddBreak(_section, false);

  _last = record;
  return false; // continue with the next record
}

void WriteToDiary::Finish()
{
  if (_section >= 0) _text->FormatSection(_section);

  // select last item in index
  if (_index)
  {
    int sel = -1;
    if (_selected.GetLength()) sel = FindItemWithData(_index, _selected);
    if (sel < 0)
    {
      sel = 0; // by default, select the first item
      if (_page->GetSortOrder() == DSOTask)
      {
        // when sorted by the class, try to select the current task
        Task *currentTask = _identity.GetCurrentTask();
        if (currentTask) sel = FindItemWithData(_index, Format("Task%d", currentTask->GetId()));
      }
    }
    if(_index->GetCurSel() != sel) _index->SetCurSel(sel);
  }
}

void DisplayDiary::HandleReferences(const HTMLSection &section)
{
  Assert(_diary);
  for (int i=0; i<section.fields.Size(); i++)
  {
    const char *href = section.fields[i].href;
    if (*href != '#') continue;
    // link to other section
    RString name = href + 1;
    // check if already in diary
    if (_diary->FindSection(name) >= 0) continue;
    // find in the briefing
    int ss = _briefing.FindSection(name);
    if (ss < 0) continue;

    // need to be copied
    int s = _diary->AddSection();
    HTMLSection &dstSection = _diary->GetSection(s);
    const HTMLSection &srcSection = _briefing.GetSection(ss);
    // create the index item
    if (_diaryIndex)
    {
      int index = _diaryIndex->AddString(section.fields[i].text);
      _diaryIndex->SetData(index, name);
    }
    // copy the content
    dstSection = srcSection;
    // finish
    _diary->FormatSection(s);
    // handle references to other sections
    HandleReferences(srcSection);
  }
}

void DisplayDiary::WriteBriefingNotes()
{
  AIBrain *brain = _diaryPerson ? _diaryPerson->Brain() : NULL;

  int section = FindSectionForUnit(_briefing, "Main", brain);
  if (section >= 0)
  {
    // add the section to the diary
    if (_diary)
    {
      const HTMLSection &srcSection = _briefing.GetSection(section);
      int s = _diary->AddSection();
      HTMLSection &dstSection = _diary->GetSection(s);
      RString name = srcSection.names.Size() > 0 ? srcSection.names[0] : "Notes";
      // create the index item
      if (_diaryIndex)
      {
        int index = _diaryIndex->AddString(LocalizeString(IDS_MAP_NOTES));
        _diaryIndex->SetData(index, name);
      }
      // title
      _diary->AddText(s, LocalizeString(IDS_MAP_NOTES), HFH1, HALeft, false, false, RString());
      _diary->AddBreak(s, false);
      // text
      for (int i=0; i<srcSection.fields.Size(); i++) dstSection.fields.Add(srcSection.fields[i]);
      dstSection.fields.Compact();
      // names
      dstSection.names = srcSection.names;
      if (dstSection.names.Size() == 0) dstSection.names.Add(name);
      // finish
      _diary->FormatSection(s);
      // handle references to other sections
      HandleReferences(srcSection);
    }
  }
}

void DisplayDiary::WriteBriefingPlan()
{
  AIBrain *brain = _diaryPerson ? _diaryPerson->Brain() : NULL;
  TargetSide side = brain ? brain->GetSide() : TSideUnknown;

  // plan header
  int section = FindSectionForUnit(_briefing, "Plan", side, brain);
  if (section >= 0)
  {
    // add the section to the diary
    if (_diary)
    {
      const HTMLSection &srcSection = _briefing.GetSection(section);
      int s = _diary->AddSection();
      HTMLSection &dstSection = _diary->GetSection(s);
      RString name = srcSection.names.Size() > 0 ? srcSection.names[0] : "Plan";
      // create the index item
      if (_diaryIndex)
      {
        int index = _diaryIndex->AddString(LocalizeString(IDS_MAP_PLAN));
        _diaryIndex->SetData(index, name);
      }
      // title
      _diary->AddText(s, LocalizeString(IDS_MAP_PLAN), HFH1, HALeft, false, false, RString());
      _diary->AddBreak(s, false);
      // text
      for (int i=0; i<srcSection.fields.Size(); i++) dstSection.fields.Add(srcSection.fields[i]);
      dstSection.fields.Compact();
      // names
      dstSection.names = srcSection.names;
      if (dstSection.names.Size() == 0) dstSection.names.Add(name);
      // finish
      _diary->FormatSection(s);
      // handle references to other sections
      HandleReferences(srcSection);
    }
  }

  // objectives
  int objective = 1;
  for (int section=0; section<_briefing.NSections(); section++)
  {
    const HTMLSection &src = _briefing.GetSection(section);
    for (int n=0; n<src.names.Size(); n++)
    {
      RString srcName = src.names[n];
      if (strnicmp(srcName, "OBJ_", strlen("OBJ_")) != 0) continue; // optimization

      struct NamePrefix
      {
        const char *prefix;
        const char *prefixReplace;
        TargetSide side;
        bool hidden;
      };

      NamePrefix prefixes[] =
      {
        {"OBJ_WEST_HIDDEN_", "OBJ_WEST_", TWest, true},
        {"OBJ_EAST_HIDDEN_", "OBJ_EAST_", TEast, true},
        {"OBJ_GUER_HIDDEN_", "OBJ_GUER_", TGuerrila, true},
        {"OBJ_CIVIL_HIDDEN_", "OBJ_CIVIL_", TCivilian, true},
        {"OBJ_HIDDEN_", "OBJ_", TSideUnknown, true},
        {"OBJ_WEST_", NULL, TWest, false},
        {"OBJ_EAST_", NULL, TEast, false},
        {"OBJ_GUER_", NULL, TGuerrila, false},
        {"OBJ_CIVIL_", NULL, TCivilian, false},
        {"OBJ_", NULL, TSideUnknown, false}
      };

      // find the longest matching prefix
      int match = -1;
      for (int i=0; i<sizeof(prefixes)/sizeof(prefixes[0]); i++)
      {
        if (strnicmp(srcName, prefixes[i].prefix, strlen(prefixes[i].prefix)) == 0)
        {
          match = i;
          break;
        }
      }

      // valid objective found
      Assert(match >= 0); // because of optimization
      NamePrefix &prefix = prefixes[match];
      if (prefix.side != TSideUnknown && side != prefix.side) continue;
      if (prefix.prefixReplace) srcName = RString(prefix.prefixReplace) + srcName.Substring(strlen(prefix.prefix), INT_MAX); // fix the name

      int value;
      if (brain)
      {
        GameState *state = GWorld->GetGameState();
        value = toInt((float)state->Evaluate(srcName, GameState::EvalContext::_default, GWorld->GetMissionNamespace())); // mission namespace
      }
      else
      {
        value = prefix.hidden ? OSHidden : OSActive; 
      }

      if (value == OSHidden) break;  // next objective

      // add objective to the diary
      ParamEntryVal cls = Pars >> "RscObjectives";
      RString picture;
      switch (value)
      {
      case OSDone:
        picture = cls >> "done";
        break;
      case OSFailed:
        picture = cls >> "failed";
        break;
      default:
        Fail("Unknown objective type");
      case OSActive:
        picture = cls >> "active";
        break;
      }

      RString name = src.names.Size() > 0 ? src.names[0] : Format("Objective_%d", objective);
      RString displayName = Format(LocalizeString(IDS_LOG_TITLE_OBJECTIVE), objective);
      objective++;

      // index
      if (_diaryIndex)
      {
        int index = _diaryIndex->AddString(displayName);
        _diaryIndex->SetData(index, name);
      }
      // content
      if (_diary)
      {
        int s = _diary->AddSection();
        HTMLSection &dstSection = _diary->GetSection(s);
        // title
        _diary->AddText(s, displayName, HFH1, HALeft, false, false, RString());
        _diary->AddBreak(s, false);
        // picture
        float imgHeight = 480.0f * 1.0f * _diary->GetPHeight();
        HTMLField *fld = _diary->AddImage(s, picture, HALeft, false, -1, imgHeight, "");
        fld->exclude = true;
        float indent = 1.2 * fld->width;
        // text
        for (int i=0; i<src.fields.Size(); i++)
        {
          int index = dstSection.fields.Add(src.fields[i]);
          dstSection.fields[index].indent += indent;
        }
        dstSection.fields.Compact();
        // name
        dstSection.names = src.names;
        if (dstSection.names.Size() == 0) dstSection.names.Add(name);
        // finish
        _diary->FormatSection(s);
        // handle references to other sections
        HandleReferences(src);
      }
      break; // next objective
    }
  }
}

void DisplayDiary::ResizeHTML()
{
  //if diary content changes, this ensure that HMTL text will not be broken into smaller pages
  if (_diary) _diary->SetPos(_diary->X(),_diary->Y(),_diary->W(),_maxDiaryH);
  if (_controlsGroup) _controlsGroup->SetPos(_controlsGroup->X(),_controlsGroup->Y(),_controlsGroup->W(),_maxDiaryH);
}

void DisplayDiary::SyncTopicList(int index)
{
  //if external link has been processed, synchronize diary topic 
  if(_diaryTopics)
  {
    if(_diaryTopics->GetValue(_diaryTopics->GetCurSel()) != index + IDC_DIARY_TOPIC_1)

      for (int i = 0; i< _diaryTopics->Size();i++)
      {
        if (_diaryTopics->GetValue(i) == index + IDC_DIARY_TOPIC_1)
        {
          _diaryTopics->SetCurSel(i);
        }
      }
  }
}

void DisplayDiary::UpdateDiary(bool force)
{
  AIBrain *unit = GWorld->FocusOn();
  if(!unit) return;

  if (!_diaryPerson)
    _diaryPerson = unit ? unit->GetPerson() : NULL;

  Assert(_diary);
  Assert(_diaryPerson);

  ResizeHTML();

  Identity &identity = _diaryPerson->GetIdentity();
  RString pageName = GetCurrentPage();

  if (_diaryPerson)
  {
    if (_playerName) _playerName->SetText(_diaryPerson->GetPersonName());
    if (_currentTask)
    {
      RString taskDesc;
      Task *task = identity.GetCurrentTask();
      if (task) task->GetDescriptionShort(taskDesc);
      _currentTask->SetText(taskDesc);
    }
  }
  else
  {
    if (_currentTask) _currentTask->SetText("");
    if (_playerName) _playerName->SetText("");
  }

  if (force || pageName.GetLength() > 0 && identity.IsDiaryChanged(pageName))
  {
    // store selected item in the index
    RString selected;
    if (_diaryIndex)
    {
      int index = _diaryIndex->GetCurSel();
      if (index >= 0) selected = _diaryIndex->GetData(index);
      _diaryIndex->ClearStrings();
    }

    // store selected section in the HTML
    RString selectedSection;
    int currentSection = _diary->CurrentSection();
    if (currentSection >= 0 && currentSection < _diary->NSections())
    {
      const HTMLSection &section = _diary->GetSection(currentSection);
      for (int i=0; i<section.names.Size(); i++)
      {
        if (stricmp(section.names[i], selected) != 0)
        {
          selectedSection = section.names[i]; // first which name is different from the core page name
          break;
        }
      }
    }

    CHTMLContainer::FieldIndex activeField = _diary->GetActiveFieldIndex();

    _diary->Init();

    // create new content of diary
    DiaryPage *page = identity.FindDiarySubject(pageName);
    if (page)
    {
      // for backward compatibility, handle content of briefing.html (notes, plan)
      RString pageType = page->GetPageType();
      if (stricmp(pageType, "DiaryPageDiary") == 0) WriteBriefingNotes();
      //else if (stricmp(pageType, "DiaryPageTasks") == 0) WriteBriefingPlan();

      // now process regular diary records
      WriteToDiary func(_diaryIndex, _diary, page, _indexTextures, identity, selected);
      page->ForEachRecord(func, _diaryPerson);
      func.Finish();
      SyncDiary();
      // try to recover HTML state before the update
      if (selectedSection.GetLength() > 0) _diary->SwitchSection(selectedSection);
      if (activeField.IsValid())
      {
        int currentSection = _diary->CurrentSection();
        if (currentSection == activeField._section && currentSection >= 0 && currentSection < _diary->NSections())
        {
          const HTMLSection &section = _diary->GetSection(currentSection);
          if (activeField._field < section.fields.Size() && section.fields[activeField._field].href.GetLength() > 0)
            _diary->SetActiveFieldIndex(activeField);
        }
      }
    }
  }
  UpdateButtons();
}

void DisplayDiary::SyncDiary()
{
  if (!_diary || !_diaryIndex) return;

  int index = _diaryIndex->GetCurSel();
  if (index >= 0)
    _diary->SwitchSection(_diaryIndex->GetData(index));
  else
  {
    // select the last section in the topic
    int n = intMax(_diary->NSections() - 1, 0);
    _diary->SwitchSectionRaw(n);
  }
}

void DisplayDiary::UpdateButtons()
{
  if (_diaryAdd)
  {
    IControl *ctrl = _diaryAdd->GetControl();
    if (_diaryPerson == NULL)
    {
      ctrl->ShowCtrl(false);
    }
    else
    {
      RString page = GetCurrentPage();
      if (page.GetLength() == 0)
      {
        ctrl->ShowCtrl(false);
      }
      else
      {
        Identity &identity = _diaryPerson->GetIdentity();
        ctrl->ShowCtrl(!identity.IsDiaryReadOnly(page));
      }
    }
  }
}

RString DisplayDiary::GetCurrentPage() const
{
  if (!_diaryPerson) return RString();
  return _diaryPerson->GetIdentity().GetCurrentDiarySubject();
}

/******************************************************************************/
/******************************************************************************/
CMapListBox::CMapListBox(DisplayMap *map, int idc, ParamEntryPar cls) 
  : CListBox(map, idc, cls)
{
  _map = map;
}

void CMapListBox::OnMouseZChanged(float dz)
{
  if(GInput.keyPressed[DIK_LCONTROL] || GInput.keyPressed[DIK_RCONTROL])
  {
    if(_map && _map->GetMap())
      _map->GetMap()->OnMouseZChanged(dz);
  }
  else
  {
    if(dz<0 && GetCurSel()<Size()-1) SetCurSel(GetCurSel()+1);
    else if(dz>0 && GetCurSel()>0) SetCurSel(GetCurSel()-1);
  }
}

CMapCGroup::CMapCGroup(DisplayMap *map, int idc, ParamEntryPar cls) 
  : CControlsGroup(map, idc, cls)
{
  _map = map;
}

// for listbox and combobox
#define SCROLL_SPEED    0.1
#define SCROLL_MAX      0.1

void CMapCGroup::OnMouseZChanged(float dz)
{
  if(GInput.keyPressed[DIK_LCONTROL] || GInput.keyPressed[DIK_RCONTROL])
  {
    if(_map && _map->GetMap())
      _map->GetMap()->OnMouseZChanged(dz);
  }
  else
  {
    float scroll = SCROLL_SPEED * dz;
    saturate(scroll, -SCROLL_MAX, SCROLL_MAX);
    _vScrollbar.SetPos(_vScrollbar.GetPos()-scroll);
  }
}



DisplayDiaryInMap::DisplayDiaryInMap(DisplayMap *parent)
{
  _map = parent;
  _context = DCBriefing;
  // init buttons based on diary content
  AIBrain *unit = GWorld->FocusOn();
  _diaryPerson = unit ? unit->GetPerson() : NULL;

  if(_diaryPerson) InitDiaryTopicList(_map, _topics, _diaryPerson);
  //diary topics list
  _diaryTopics = dynamic_cast<CListBox *> (_map->GetCtrl(IDC_DIARY_TOPIC_LIST));
  if(_diaryTopics) 
  {
    _diaryTopics->EnableQuickSearch(false);
    _maxDiaryTopicsH = _diaryTopics->H();
  }

  //diary sub-topics list
  _diaryIndex = dynamic_cast<CListBox *> (_map->GetCtrl(IDC_DIARY_TOPIC_LISTINDEX));
  if(_diaryIndex) 
  {
    _diaryIndex->EnableQuickSearch(false);
    _maxDiaryIndexH = _diaryIndex->H();
  }
  //diary HTML text
  _diary = dynamic_cast<CHTML *> (_map->GetCtrl(IDC_DIARY_TOPIC_HTML));
  if(_diary)
    _maxDiaryH = _diary->H();

  IControl* ctrl = _map->GetCtrl(IDC_DIARY_TOPIC_ADD_RECORD);
  if(ctrl) 
  {
    _diaryAdd = GetTextContainer(ctrl);
    ctrl->ShowCtrl(false);
  }
  //HMTML container
  _controlsGroup = dynamic_cast<CControlsGroup *> (_map->GetCtrl(IDC_DIARY_TOPIC_HTML_GROUP));
  if(_controlsGroup)
    _maxControlsGroupH = _controlsGroup->H();
  //mission description
  _playerName = dynamic_cast<ITextContainer *> (_map->GetCtrl(IDC_DIARY_PLAYER_NAME));
  _currentTask = dynamic_cast<ITextContainer *>(_map->GetCtrl(IDC_DIARY_CURRENT_TASK));
  _currentMission = dynamic_cast<ITextContainer *> (_map->GetCtrl(IDC_DIARY_MISSION_NAME));

  if(_currentMission)
  {//get mission name
    RString name;
    if (GWorld->GetMode() == GModeNetware)
    {
      const MissionHeader *header = GetNetworkManager().GetMissionHeader();
      if (header) name = header->GetLocalizedMissionName();
    }
    if ( name.IsEmpty() )
    {
      name = GetMissionParametersDisplayName();
      if ( name.IsEmpty() ) 
      {
        name = Localize(CurrentTemplate->Intel().briefingName);
        if ( name.IsEmpty() )
        {
          name = Glob.header.filenameReal;
          // user mission file name is encoded
          if (IsUserMission()) name = DecodeFileName(name);
        }
      }
      else name = Localize(name);
    }
    _currentMission->SetText(name);
  }
}

void DisplayDiaryInMap::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_DIARY_TOPIC_ADD_RECORD:
    {//add diary record
      RString title;
      if (_diaryIndex)
      {
        int sel = _diaryIndex->GetCurSel();
        if (sel >= 0) title = _diaryIndex->GetText(sel);
      }
      _map->CreateChild(new DisplayEditDiaryRecord(_map, -1, title, RString()));
    }
    break;
  }
}


void DisplayDiaryInMap::OnLBSelChanged(IControl *ctrl, int curSel)
{
  if(ctrl->IDC() == IDC_DIARY_TOPIC_LIST && _diaryTopics)
  {
    int diary_idc = _diaryTopics->GetValue(curSel);
    if(diary_idc>=IDC_DIARY_TOPIC_1)
    {
      RString link = _topics[diary_idc - IDC_DIARY_TOPIC_1];
      ProcessLink(link, DCBriefing);
    }
    else if(diary_idc==IDC_DIARY_TOPIC_MAP) ClearLink();
    ResetListBox();
  }
  else if (ctrl->IDC() == IDC_DIARY_TOPIC_LISTINDEX)
  {
    SyncDiary();
    ResetListBox();
    return;
  }
}

void DisplayDiaryInMap::OnLBDblClick(int idc, int curSel)
{
  if (idc == IDC_DIARY_TOPIC_LISTINDEX && _map && stricmp(_diaryTopics->GetData(_diaryTopics->GetCurSel()),"tasks")==0)
  {
    if (_diaryPerson)
    {
      Identity &identity = _diaryPerson->GetIdentity();
      const char *name = "Task";
      const char *ptr = _diaryIndex->GetData(curSel);
      int n = strlen(name);
      if (strnicmp(ptr, name, n) == 0)
      {
        ptr += n;
        int taksID = atoi(ptr);
        Task *tsk = identity.FindTask(taksID);
        if (tsk) 
        { 
          Vector3 dest;
          if(tsk->GetDestination(dest))
            _map->AnimateTo(dest);
        }
      }
    }
  }
}

bool DisplayDiaryInMap::OnHTMLLink(int idc, RString link, DiaryContext context)
{
  _context = context;
  const char *ptr = link;
  const char *name = "marker:";
  int n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    ptr += n;
    for (int i=0; i<markersMap.Size(); i++)
    {
      ArcadeMarkerInfo &mInfo = markersMap[i];
      if (stricmp(ptr, mInfo.name) == 0)
      {
        if (_map)
        {
          _map->AnimateTo(mInfo.position);
          _map->SetActiveMarker(i);
          //   Exit(IDC_AUTOCANCEL);
        }
        break;
      }
    }
    return true;
  }
  name = "edit:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    ptr += n;
    int recordId = atoi(ptr);
    if (_diaryPerson)
    {
      RString subject = GetCurrentPage();
      if (subject.GetLength() > 0)
      {
        Identity &identity = _diaryPerson->GetIdentity();
        const DiaryPage *page = identity.FindDiarySubject(subject);
        if (page && !page->IsReadOnly())
        {
          DiaryRecord *record = page->FindRecord(recordId);
          if (record && !record->IsReadOnly()) _map->CreateChild(new DisplayEditDiaryRecord(_map, recordId, record->GetTitle(), 

            record->GetText()));
        }
      }
    }
    return true;
  }
  name = "delete:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    ptr += n;
    int recordId = atoi(ptr);
    if (_diaryPerson)
    {
      RString subject = GetCurrentPage();
      Identity &identity = _diaryPerson->GetIdentity();
      if (subject.GetLength() > 0 && !identity.IsDiaryReadOnly(subject))
      {
        _toDeleteSubject = subject;
        _toDeleteRecord = recordId;
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
        _map->CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_SURE),
          IDD_MSG_DELETE_DIARY_RECORD, false, &buttonOK, &buttonCancel);
        identity.DeleteDiaryRecord(subject, recordId);
      }
    }
    return true;
  }
  name = "currentTask:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    ptr += n;
    int taskId = atoi(ptr);
    if (_diaryPerson)
    {
      Identity &identity = _diaryPerson->GetIdentity();
      Task *task = identity.FindTask(taskId);
      Task *current = identity.GetCurrentTask();
      if (task) 
      {
        if(current && current->GetState() == TSAssigned ) current->SetState(TSCreated);
        identity.SetCurrentTask(task);
        task->SetState(TSAssigned);
      }
    }
    return true;
  }
  name = "kick:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    if (_context == DCInGame)
    {
      ptr += n;
      int dpnid = atoi(ptr);
      if (GetNetworkManager().IsServer())
      {
        if (dpnid != GetNetworkManager().GetPlayer())
          GetNetworkManager().KickOff(dpnid, KORKick);
      }
      else if (GetNetworkManager().IsGameMaster())
      {
        if (dpnid != GetNetworkManager().GetPlayer())
          GetNetworkManager().SendKick(dpnid);
      }
    }
    return true;
  }
  name = "ban:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    if (_context == DCInGame)
    {
      ptr += n;
      int dpnid = atoi(ptr);
      if (GetNetworkManager().IsServer())
      {
        if (dpnid != GetNetworkManager().GetPlayer())
          GetNetworkManager().Ban(dpnid);
      }
    }
    return true;
  }
  name = "mute:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    if (_context == DCInGame)
    {
      ptr += n;
      int dpnid = atoi(ptr);
      if (dpnid != GetNetworkManager().GetPlayer())
      {
        const PlayerIdentity *identity = GetNetworkManager().FindIdentity(dpnid);
        if (identity) GetNetworkManager().TogglePlayerMute(identity->xuid);
      }
    }
    return true;
  }
  name = "execute:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    ptr += n;

    GameState *state = GWorld->GetGameState();

    GameVarSpace vars(false);
    int gshandle = state->BeginContext(&vars);
    state->Execute(ptr, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
    state->EndContext(gshandle);

    return true;
  }
  name = "executeClose:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    ptr += n;

    GameState *state = GWorld->GetGameState();

    GameVarSpace vars(false);
    int gshandle = state->BeginContext(&vars);
    state->Execute(ptr, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
    state->EndContext(gshandle);

    GWorld->ShowMap(false);
    // Exit(IDC_AUTOCANCEL);
    return true;
  }

  // handle as an external link
  ProcessLink(link);
  return true;
}

void DisplayDiaryInMap::OnChildDestroyed(int idd, int exit, Ref<ControlsContainer> child)
{
  if (idd == IDD_EDIT_DIARY_RECORD)
  {
    if (exit == IDC_OK)
    {
      if (_diaryPerson)
      {
        ResizeHTML();
        RString page = GetCurrentPage();
        if (page.GetLength() > 0)
        {
          Identity &identity = _diaryPerson->GetIdentity();
          if (!identity.IsDiaryReadOnly(page))
          {
            DisplayEditDiaryRecord *disp = dynamic_cast<DisplayEditDiaryRecord *>((ControlsContainer *)child);
            Assert(disp);
            int recordId = disp->GetRecordId();
            RString title = disp->GetTitle();
            RString text = disp->GetText();
            if (recordId >= 0) identity.EditDiaryRecord(page, recordId, title, text);
            else identity.AddDiaryRecord(page, title, text, NULL, TSNone, NULL, NULL, false); // not read-only
          }
        }
      }
    }
    UpdateDiary();

  }
  else if (idd == IDD_MSG_DELETE_DIARY_RECORD)
  {
    if (exit == IDC_OK && _diaryPerson)
    {
      Identity &identity = _diaryPerson->GetIdentity();
      identity.DeleteDiaryRecord(_toDeleteSubject, _toDeleteRecord);
    }
    UpdateDiary();
    ResetListBox();
  }
  else if (idd == 106)
  {
    UpdateTopics();
    UpdateDiary();
    ResetListBox();
  }
}

void DisplayDiaryInMap::OnSimulate(EntityAI *vehicle)
{
  if(_diaryTopics && _diaryTopics->GetCurSel()<0)
  {
    AIBrain *unit = GWorld->FocusOn();
    _diaryPerson = unit ? unit->GetPerson() : NULL;

    if(_diaryPerson) InitDiaryTopicList(_map, _topics, _diaryPerson, DCBriefing);

    if(_diaryTopics->Size()>0) _diaryTopics->SetCurSel(0);
  }
  else if(_diaryPerson && _diaryTopics)
  {
    Identity &identity = _diaryPerson->GetIdentity();
    RString nas = identity.GetCurrentDiarySubject();
    if(_diaryTopics->GetCurSel() == 0 && nas.GetLength()>0) ClearLink(); 
    else if (stricmp(_diaryTopics->GetData(_diaryTopics->GetCurSel()), nas) != 0)
    {
      ProcessLink(_diaryTopics->GetData(_diaryTopics->GetCurSel()));
    }
  }
  ////close diary when 'j' is pressed ('j' is default key for open diary)
  if( GInput.GetActionToDo(UADiary, true, false, false, 2) )
  {
    CListBox *list = dynamic_cast<CListBox *> (_map->GetCtrl(IDC_DIARY_TOPIC_LIST));

    if (stricmp(list->GetData(list->GetCurSel()),"tasks")==0)
      list->SetCurSel(0);
    else
    {
      for (int i= 1; i< list->Size();i++)
      {
        if (stricmp(list->GetData(i),"tasks")==0)
        {
          list->SetCurSel(i);
          return;
        }
      }
    }
  }
  //close diary when 'j' is pressed ('j' is default key for open diary)
  if( GInput.GetActionToDo(UANetworkPlayers, true, false, false, 2) )
  {
    CListBox *list = dynamic_cast<CListBox *> (_map->GetCtrl(IDC_DIARY_TOPIC_LIST));

    if (stricmp(list->GetData(list->GetCurSel()),"players")==0)
      list->SetCurSel(0);
    else
    {
      for (int i= 0; i< list->Size();i++)
      {
        if (stricmp(list->GetData(i),"players")==0)
        {
          list->SetCurSel(i);
          return;
        }
      }
    }
  }
}

bool DisplayDiaryInMap::SelectPage(RString name)
{
  if (_diaryPerson)
  {
    _diaryPerson->GetIdentity().SetCurrentDiarySubject(name);
    UpdateDiary(true);
    return true;
  }
  return false;
}

void DisplayDiaryInMap::UpdateOwner()
{
  AIBrain *unit = GWorld->FocusOn();
  Person *person = unit ? unit->GetPerson() : NULL;
  if (person && person != _diaryPerson)
  {
    _diaryPerson = person;

  }
}


void DisplayDiaryInMap::ProcessLink(RString link,DiaryContext context)
{
  _context = context;
  AIBrain *unit = GWorld->FocusOn();
  _diaryPerson = unit ? unit->GetPerson() : NULL;

  // load list of index textures
  ParamEntryVal list = Pars >> "CfgDiary" >> "Icons";
  for (int i=0; i<list.GetEntryCount(); i++)
  {
    ParamEntryVal entry = list.GetEntry(i);
    Ref<Texture> texture = GlobLoadTexture(GetPictureName(entry));
    if (texture) _indexTextures.Add(TextureByName(entry.GetName(), texture));
  }

  InitDiaryTopicList(_map, _topics, _diaryPerson);
  ProcessLink(link);
}

void DisplayDiaryInMap::ProcessLink(RString link)
{
  // Unlike OnHTMLLink, global (external) links to diary are processed here
  const char *ptr = link;
  ResizeHTML();

  // link to the task
  const char *name = "task:";
  int n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    ptr += n;
    int taskId = atoi(ptr);
    int index = -1;
    if (_diaryPerson)
    {
      Identity &identity = _diaryPerson->GetIdentity();
      Task *task = identity.FindTask(taskId);
      if (task)
      {
        // which page contains the list of tasks
        RString pageName;
        for (int i=0; i<identity.NDiarySubjects(); i++)
        {
          const DiaryPage *page = identity.GetDiarySubject(i);
          if (strcmp(page->GetPageType(), "DiaryPageTasks") == 0)
          {
            index = i;
            pageName = page->GetName();
            break;
          }
        }

        if (pageName.GetLength() > 0)
        {
          // switch to the correct page
          SelectPage(pageName);

          // switch to the correct record
          if (_diaryIndex)
          {
            // find the record in the index, select it and synchronize the content
            RString name = Format("Task%d", taskId);
            int i = FindItemWithData(_diaryIndex, name);
            if (i >= 0)
            {
              _diaryIndex->SetCurSel(i);
              SyncDiary();
            }
          }
        }
      }
      SyncTopicList(index);
    }
    return;
  }

  // link to the diary page / diary record
  if (_diaryPerson)
  {
    Identity &identity = _diaryPerson->GetIdentity();
    int index = -1;
    for (int i=0; i<identity.NDiarySubjects(); i++)
    {
      const DiaryPage *page = identity.GetDiarySubject(i);
      RString name = page->GetName();
      if (stricmp(ptr, name) == 0)
      {
        // switch to the correct page
        SelectPage(name);
        index = i;
      }
      else
      {
        RString prefix = name + RString(":");
        int n = prefix.GetLength();
        if (strnicmp(ptr, prefix, n) == 0)
        {
          ptr += n;
          RString recordId = ptr;

          // TODO: switch sort order by the id type

          // switch to the correct page
          SelectPage(name);
          index = i;

          // switch to the correct record
          if (_diaryIndex)
          {
            // find the record in the index, select it and synchronize the content
            int i = FindItemWithData(_diaryIndex, recordId);
            if (i >= 0)
            {
              _diaryIndex->SetCurSel(i);
              SyncDiary();
            }
          }
        }
      }
    }
    SyncTopicList(index);
  }
}

void DisplayDiaryInMap::SyncTopicList(int index)
{
  //if external link has been processed, synchronize diary topic 
  if(_diaryTopics)
  {
    if(_diaryTopics->GetValue(_diaryTopics->GetCurSel()) != index + IDC_DIARY_TOPIC_1)

      for (int i = 0; i< _diaryTopics->Size();i++)
      {
        if (_diaryTopics->GetValue(i) == index + IDC_DIARY_TOPIC_1)
        {
          _diaryTopics->SetCurSel(i);
        }
      }
  }
}

void DisplayDiaryInMap::ClearLink()
{//hide everything to see only map 
  SelectPage("");
  ProcessLink("");
}

void DisplayDiaryInMap::ResizeHTML()
{
  //if diary content changes, this ensure that HMTL text will not be broken into smaller pages
  if (_diary) _diary->SetPos(_diary->X(),_diary->Y(),_diary->W(),_maxDiaryH);
  if (_controlsGroup) _controlsGroup->SetPos(_controlsGroup->X(),_controlsGroup->Y(),_controlsGroup->W(),_maxDiaryH);
}

void DisplayDiaryInMap::ResetListBox()
{ //resize index list and it's background
  bool showMenu = false;

  if(_diaryIndex)
  { 
    Control *ctrl = dynamic_cast<Control *> (_map->GetCtrl(IDC_DIARY_SUBTOPIC_BACKGROUND));
    if(_diaryIndex->Size()<=0 || showMenu) 
    {
      _diaryIndex->ShowCtrl(false);
      if(ctrl) ctrl->ShowCtrl(false);
    }
    else 
    {//hide if empty
      if(!_diaryIndex->IsVisible()) _diaryIndex->ShowCtrl(true);
      if(ctrl) ctrl->ShowCtrl(true);
      float size = floatMin(0.002f +_diaryIndex->Size() * _diaryIndex->GetRowHeight(),_maxDiaryIndexH);
      _diaryIndex->SetPos(_diaryIndex->X(),_diaryIndex->Y(),_diaryIndex->W(),size);
      if(ctrl) ctrl->SetPos(ctrl->X(),ctrl->Y(),ctrl->W(),size+0.04);
    }  
  }
  //reset HMTL control and it's background
  if (_diary)
  {
    Control *ctrl = dynamic_cast<Control *> (_map->GetCtrl(IDC_DIARY_CONTENT_BACKGROUND));
    float size = 0;

    if(_diary->NSections()>0)
    {
      HTMLSection &section = _diary->GetSection(_diary->CurrentSection());
      //get HTML control height
      for (int r=0; r<section.rows.Size(); r++)
      {
        HTMLRow &row = section.rows[r];
        size += row.height;
      }
    }
    //if HTML control is visible and !empty
    if(_diary->NSections()>0 && size>0 && !showMenu)
    {
      size = floatMin(size,_maxDiaryH);
      _diary->ShowCtrl(true);       
      _diary->SetPos(_diary->X(),_diary->Y(),_diary->W(),size);
      if(_controlsGroup)
      {
        _controlsGroup->ShowCtrl(true);
        size = floatMin(size,_maxControlsGroupH);
        _controlsGroup->SetPos(_controlsGroup->X(),_controlsGroup->Y(),_controlsGroup->W(),size+0.005f);
        if(ctrl)
        {
          ctrl->ShowCtrl(true);
          ctrl->SetPos(ctrl->X(),ctrl->Y(),ctrl->W(),size+0.005f+0.04);
        }
      }
    }
    else  
    {//hide (if not hidden)
      if(_controlsGroup->IsVisible())
      {
        if(_controlsGroup)_controlsGroup->ShowCtrl(false);
        _diary->ShowCtrl(false);
        if(ctrl) ctrl->ShowCtrl(false);

        IControl *ctrl = _map->GetCtrl(IDC_DIARY_TOPIC_LIST);
        if(ctrl) _map->FocusCtrl(ctrl);
      }
    }
  }
  //resize topic listbox and it's background
  if(_diaryTopics && !showMenu)
  {
    Control *ctrl = dynamic_cast<Control *> (_map->GetCtrl(IDC_DIARY_TOPIC_BACKGROUND));
    float size = floatMin(0.002f + _diaryTopics->Size() * _diaryTopics->GetRowHeight(),_maxDiaryTopicsH);
    _diaryTopics->SetPos(_diaryTopics->X(),_diaryTopics->Y(),_diaryTopics->W(),size);
    if(!_diaryTopics->IsVisible())  _diaryTopics->ShowCtrl(true);
    if(ctrl)
    {
      ctrl->SetPos(ctrl->X(), ctrl->Y(), ctrl->W(), size+0.04);
      ctrl->ShowCtrl(true);
    }
  }
  else
  {
    Control *ctrl = dynamic_cast<Control *> (_map->GetCtrl(IDC_DIARY_TOPIC_BACKGROUND));
    if(_diaryTopics)
      _diaryTopics->ShowCtrl(false);
    if(ctrl) ctrl->ShowCtrl(false);
  }
}

void DisplayDiaryInMap::HandleReferences(const HTMLSection &section)
{
  Assert(_diary);
  for (int i=0; i<section.fields.Size(); i++)
  {
    const char *href = section.fields[i].href;
    if (*href != '#') continue;
    // link to other section
    RString name = href + 1;
    // check if already in diary
    if (_diary->FindSection(name) >= 0) continue;
    // find in the briefing
    int ss = _briefing.FindSection(name);
    if (ss < 0) continue;

    // need to be copied
    int s = _diary->AddSection();
    HTMLSection &dstSection = _diary->GetSection(s);
    const HTMLSection &srcSection = _briefing.GetSection(ss);
    // create the index item
    if (_diaryIndex)
    {
      int index = _diaryIndex->AddString(section.fields[i].text);
      _diaryIndex->SetData(index, name);
    }
    // copy the content
    dstSection = srcSection;
    // finish
    _diary->FormatSection(s);
    // handle references to other sections
    HandleReferences(srcSection);
  }
}

void DisplayDiaryInMap::WriteBriefingNotes()
{
  AIBrain *brain = _diaryPerson ? _diaryPerson->Brain() : NULL;

  int section = FindSectionForUnit(_briefing, "Main", brain);
  if (section >= 0)
  {
    // add the section to the diary
    if (_diary)
    {
      const HTMLSection &srcSection = _briefing.GetSection(section);
      int s = _diary->AddSection();
      HTMLSection &dstSection = _diary->GetSection(s);
      RString name = srcSection.names.Size() > 0 ? srcSection.names[0] : "Notes";
      // create the index item
      if (_diaryIndex)
      {
        int index = _diaryIndex->AddString(LocalizeString(IDS_MAP_NOTES));
        _diaryIndex->SetData(index, name);
      }
      // title
      _diary->AddText(s, LocalizeString(IDS_MAP_NOTES), HFH1, HALeft, false, false, RString());
      _diary->AddBreak(s, false);
      // text
      for (int i=0; i<srcSection.fields.Size(); i++) dstSection.fields.Add(srcSection.fields[i]);
      dstSection.fields.Compact();
      // names
      dstSection.names = srcSection.names;
      if (dstSection.names.Size() == 0) dstSection.names.Add(name);
      // finish
      _diary->FormatSection(s);
      // handle references to other sections
      HandleReferences(srcSection);
    }
  }
}

void DisplayDiaryInMap::WriteBriefingPlan()
{
  AIBrain *brain = _diaryPerson ? _diaryPerson->Brain() : NULL;
  TargetSide side = brain ? brain->GetSide() : TSideUnknown;

  // plan header
  int section = FindSectionForUnit(_briefing, "Plan", side, brain);
  if (section >= 0)
  {
    // add the section to the diary
    if (_diary)
    {
      const HTMLSection &srcSection = _briefing.GetSection(section);
      int s = _diary->AddSection();
      HTMLSection &dstSection = _diary->GetSection(s);
      RString name = srcSection.names.Size() > 0 ? srcSection.names[0] : "Plan";
      // create the index item
      if (_diaryIndex)
      {
        int index = _diaryIndex->AddString(LocalizeString(IDS_MAP_PLAN));
        _diaryIndex->SetData(index, name);
      }
      // title
      _diary->AddText(s, LocalizeString(IDS_MAP_PLAN), HFH1, HALeft, false, false, RString());
      _diary->AddBreak(s, false);
      // text
      for (int i=0; i<srcSection.fields.Size(); i++) dstSection.fields.Add(srcSection.fields[i]);
      dstSection.fields.Compact();
      // names
      dstSection.names = srcSection.names;
      if (dstSection.names.Size() == 0) dstSection.names.Add(name);
      // finish
      _diary->FormatSection(s);
      // handle references to other sections
      HandleReferences(srcSection);
    }
  }

  // objectives
  int objective = 1;
  for (int section=0; section<_briefing.NSections(); section++)
  {
    const HTMLSection &src = _briefing.GetSection(section);
    for (int n=0; n<src.names.Size(); n++)
    {
      RString srcName = src.names[n];
      if (strnicmp(srcName, "OBJ_", strlen("OBJ_")) != 0) continue; // optimization

      struct NamePrefix
      {
        const char *prefix;
        const char *prefixReplace;
        TargetSide side;
        bool hidden;
      };

      NamePrefix prefixes[] =
      {
        {"OBJ_WEST_HIDDEN_", "OBJ_WEST_", TWest, true},
        {"OBJ_EAST_HIDDEN_", "OBJ_EAST_", TEast, true},
        {"OBJ_GUER_HIDDEN_", "OBJ_GUER_", TGuerrila, true},
        {"OBJ_CIVIL_HIDDEN_", "OBJ_CIVIL_", TCivilian, true},
        {"OBJ_HIDDEN_", "OBJ_", TSideUnknown, true},
        {"OBJ_WEST_", NULL, TWest, false},
        {"OBJ_EAST_", NULL, TEast, false},
        {"OBJ_GUER_", NULL, TGuerrila, false},
        {"OBJ_CIVIL_", NULL, TCivilian, false},
        {"OBJ_", NULL, TSideUnknown, false}
      };

      // find the longest matching prefix
      int match = -1;
      for (int i=0; i<sizeof(prefixes)/sizeof(prefixes[0]); i++)
      {
        if (strnicmp(srcName, prefixes[i].prefix, strlen(prefixes[i].prefix)) == 0)
        {
          match = i;
          break;
        }
      }

      // valid objective found
      Assert(match >= 0); // because of optimization
      NamePrefix &prefix = prefixes[match];
      if (prefix.side != TSideUnknown && side != prefix.side) continue;
      if (prefix.prefixReplace) srcName = RString(prefix.prefixReplace) + srcName.Substring(strlen(prefix.prefix), INT_MAX); // fix the name

      int value;
      if (brain)
      {
        GameState *state = GWorld->GetGameState();
        value = toInt((float)state->Evaluate(srcName, GameState::EvalContext::_default, GWorld->GetMissionNamespace())); // mission namespace
      }
      else
      {
        value = prefix.hidden ? OSHidden : OSActive; 
      }

      if (value == OSHidden) break;  // next objective

      // add objective to the diary
      ParamEntryVal cls = Pars >> "RscObjectives";
      RString picture;
      switch (value)
      {
      case OSDone:
        picture = cls >> "done";
        break;
      case OSFailed:
        picture = cls >> "failed";
        break;
      default:
        Fail("Unknown objective type");
      case OSActive:
        picture = cls >> "active";
        break;
      }

      RString name = src.names.Size() > 0 ? src.names[0] : Format("Objective_%d", objective);
      RString displayName = Format(LocalizeString(IDS_LOG_TITLE_OBJECTIVE), objective);
      objective++;

      // index
      if (_diaryIndex)
      {
        int index = _diaryIndex->AddString(displayName);
        _diaryIndex->SetData(index, name);
      }
      // content
      if (_diary)
      {
        int s = _diary->AddSection();
        HTMLSection &dstSection = _diary->GetSection(s);
        // title
        _diary->AddText(s, displayName, HFH1, HALeft, false, false, RString());
        _diary->AddBreak(s, false);
        // picture
        float imgHeight = 480.0f * 1.0f * _diary->GetPHeight();
        HTMLField *fld = _diary->AddImage(s, picture, HALeft, false, -1, imgHeight, "");
        fld->exclude = true;
        float indent = 1.2 * fld->width;
        // text
        for (int i=0; i<src.fields.Size(); i++)
        {
          int index = dstSection.fields.Add(src.fields[i]);
          dstSection.fields[index].indent += indent;
        }
        dstSection.fields.Compact();
        // name
        dstSection.names = src.names;
        if (dstSection.names.Size() == 0) dstSection.names.Add(name);
        // finish
        _diary->FormatSection(s);
        // handle references to other sections
        HandleReferences(src);
      }
      break; // next objective
    }
  }
}

void DisplayDiaryInMap::UpdateDiary(bool force)
{
  AIBrain *unit = GWorld->FocusOn();
  if (!unit) return;
  if (!_diaryPerson)
    _diaryPerson = unit ? unit->GetPerson() : NULL;

  Assert(_diary);
  Assert(_diaryPerson);

  ResizeHTML();

  Identity &identity = _diaryPerson->GetIdentity();
  RString pageName = GetCurrentPage();

  if(_diaryPerson)
  {
    if (_playerName) _playerName->SetText(_diaryPerson->GetPersonName());
    if (_currentTask)
    {
      RString taskDesc;
      Task *task = identity.GetCurrentTask();
      if (task) task->GetDescriptionShort(taskDesc);
      _currentTask->SetText(taskDesc);
    }
  }   
  else
  { 
    if(_currentTask) _currentTask->SetText("");
    if(_playerName) _playerName->SetText("");
  }

  if (force || pageName.GetLength() > 0 && identity.IsDiaryChanged(pageName))
  {
    // store selected item in the index
    RString selected;
    if (_diaryIndex)
    {
      int index = _diaryIndex->GetCurSel();
      if (index >= 0) selected = _diaryIndex->GetData(index);
      _diaryIndex->ClearStrings();
    }

    // store selected section in the HTML
    RString selectedSection;
    int currentSection = _diary->CurrentSection();
    if (currentSection >= 0 && currentSection < _diary->NSections())
    {
      const HTMLSection &section = _diary->GetSection(currentSection);
      for (int i=0; i<section.names.Size(); i++)
      {
        if (stricmp(section.names[i], selected) != 0)
        {
          selectedSection = section.names[i]; // first which name is different from the core page name
          break;
        }
      }
    }

    CHTMLContainer::FieldIndex activeField = _diary->GetActiveFieldIndex();

    _diary->Init();

    // create new content of diary
    DiaryPage *page = identity.FindDiarySubject(pageName);
    if (page)
    {
      // for backward compatibility, handle content of briefing.html (notes, plan)
      RString pageType = page->GetPageType();
      if (stricmp(pageType, "DiaryPageDiary") == 0) WriteBriefingNotes();
      else if (stricmp(pageType, "DiaryPageTasks") == 0) WriteBriefingPlan();

      // now process regular diary records
      WriteToDiary func(_diaryIndex, _diary, page, _indexTextures, identity, selected);
      page->ForEachRecord(func, _diaryPerson);
      func.Finish();
      SyncDiary();
      // try to recover HTML state before the update
      if (selectedSection.GetLength() > 0) _diary->SwitchSection(selectedSection);
      if (activeField.IsValid())
      {
        int currentSection = _diary->CurrentSection();
        if (currentSection == activeField._section && currentSection >= 0 && currentSection < _diary->NSections())
        {
          const HTMLSection &section = _diary->GetSection(currentSection);
          if (activeField._field < section.fields.Size() && section.fields[activeField._field].href.GetLength() > 0)
            _diary->SetActiveFieldIndex(activeField);
        }
      }
    }
  }
  UpdateButtons();
}

void DisplayDiaryInMap::SyncDiary()
{

  if (!_diary || !_diaryIndex) return;
  int index = _diaryIndex->GetCurSel();
  if (index >= 0)
  {
    _diary->SwitchSection(_diaryIndex->GetData(index));
  }
  else
  {
    // select the last section in the topic
    int n = intMax(_diary->NSections() - 1, 0);
    _diary->SwitchSectionRaw(n);
  }
}

void DisplayDiaryInMap::UpdateTopics()
{
  if(_diaryPerson) InitDiaryTopicList(_map, _topics, _diaryPerson, DCBriefing);
}

void DisplayDiaryInMap::UpdateButtons()
{
  if (_diaryAdd)
  {
    IControl *ctrl = _diaryAdd->GetControl();
    if (_diaryPerson == NULL)
    {
      ctrl->ShowCtrl(false);
    }
    else
    {
      RString page = GetCurrentPage();
      if (page.GetLength() == 0)
      {
        ctrl->ShowCtrl(false);
      }
      else
      {
        Identity &identity = _diaryPerson->GetIdentity();
        ctrl->ShowCtrl(!identity.IsDiaryReadOnly(page));
      }
    }
  }
}

RString DisplayDiaryInMap::GetCurrentPage() const
{
  if (!_diaryPerson) return RString();
  return _diaryPerson->GetIdentity().GetCurrentDiarySubject();
}

#endif

ChatChannel ActualChatChannel();
void SetChatChannel(ChatChannel channel);

#if _GAMES_FOR_WINDOWS

void DisplayGetReady::OnInvited()
{
  if (_msgBox) return; // confirmation in progress

  if (GSaveSystem.IsInvitationConfirmed() || GetNetworkManager().GetClientState() == NCSNone)
  {
    // no warning is needed in the single player
    Exit(IDC_OK);
  }
  else
  {
    GSaveSystem.ConfirmInvitation();
    // let the user confirm the action because of possible progress lost
    MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
    MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
    RString message;
    if (GetNetworkManager().IsServer())
      message = LocalizeString(IDS_MSG_CONFIRM_TERMINATE_SESSION);
    else
      message = LocalizeString(IDS_MSG_CONFIRM_TERMINATE_SESSION_CLIENT);
    // special type of message box which will not close because of invitation
    _msgBox = new MsgBoxIgnoreInvitation(this, MB_BUTTON_OK | MB_BUTTON_CANCEL, message, IDD_MSG_ACCEPT_INVITATION, false, &buttonOK, 

      &buttonCancel);
  }
}

#endif

/*!
\patch 1.50 Date 4/15/2002 by Jirka
- Improved: side chat channel selected when entering briefing screen
*/
DisplayGetReady::DisplayGetReady(ControlsContainer *parent)
  : DisplayMap(parent, "RscDisplayGetReady")
{
  Assert(!GBriefing);
  GBriefing = this;

  _enableSimulation = false;
  _enableDisplay = false;
  ShowWalkieTalkie(false);
  ShowGPS(false);
  ConstParamEntryPtr cls = ExtParsMission.FindEntry("showMap");
  ShowMap(!cls ? true : (*cls));
  ShowBriefing(true);
  cls = ExtParsMission.FindEntry("showNotepad");
  ShowNotepad(!cls ? true : (*cls));

  // initialization
  _soundPlanPlayed = false;
  _soundNotesPlayed = false;
  _soundGroupPlayed = false;

  AIBrain *unit = GWorld->FocusOn();
  ConstParamEntryPtr clsWeapon = ExtParsMission.FindEntry("allowSubordinatesTakeWeapons");
  _canTakeWeapon = (clsWeapon ? (*clsWeapon) : false) && unit;

  if (GWorld->GetMode() == GModeNetware)
    _selectWeapons = unit != NULL; // everybody can change his weapons in MP
  else
    _selectWeapons = unit && _canTakeWeapon;

  /* RString dir = GetSaveDirectory();
  RString weapons;
  if (dir.GetLength() > 0) weapons = dir + RString("weapons.cfg");
  if (weapons.GetLength() > 0 && QIFileFunctions::FileExists(weapons))
  _weaponsInfo.Load(weapons, _pool);
  */

  // _showGearOnExit = _weaponsInfo._weaponsPool.Size() > 0 || _weaponsInfo._magazinesPool.Size() > 0;
  _showGearOnExit = true;
  ConstParamEntryPtr entry = (Pars >> "RscDisplayGetReady").FindEntry("showGearOnExit");
  if (entry) _showGearOnExit = *entry;

  // FIX - cancel user input
  void RemoveInputMessages();
  RemoveInputMessages();

  GChatList.Load(Pars >> "RscChatListBriefing");

  if (_briefing)
  {
    int index = _briefing->CurrentSection();
    const HTMLSection &section = _briefing->GetSection(index);
    if (section.names.Size() > 0)
      SwitchBriefingSection(section.names[0]);
  }

#if _ENABLE_IDENTITIES
  _displayDiaryMap = DisplayDiaryInMap(this);
  CListBox *list = dynamic_cast<CListBox *> (this->GetCtrl(IDC_DIARY_TOPIC_LIST));
  if(list && list->Size()>0) 
  {
    if(GWorld->GetMode()  == GModeNetware)
      _displayDiaryMap.ProcessLink("players",DCBriefing);
    else 
      _displayDiaryMap.ProcessLink("diary",DCBriefing);
  }
  _displayDiaryMap.ResetListBox();
#endif
}

DisplayGetReady::DisplayGetReady(ControlsContainer *parent, RString resource)
  : DisplayMap(parent, resource)
{
  Assert(!GBriefing);
  GBriefing = this;

  _enableSimulation = false;
  _enableDisplay = false;
  ShowWalkieTalkie(false);
  ShowGPS(false);
  ConstParamEntryPtr cls = ExtParsMission.FindEntry("showMap");
  ShowMap(!cls ? true : (*cls));
  cls = ExtParsMission.FindEntry("showNotepad");
  ShowNotepad(!cls ? true : (*cls));

  // FIX: initialization
  _soundPlanPlayed = false;
  _soundNotesPlayed = false;
  _soundGroupPlayed = false;

  AIBrain *unit = GWorld->FocusOn();
  ConstParamEntryPtr clsWeapon = ExtParsMission.FindEntry("allowSubordinatesTakeWeapons");
  _canTakeWeapon = (clsWeapon ? (*clsWeapon) : false) && unit;

  if (GWorld->GetMode() == GModeNetware)
    _selectWeapons = unit != NULL; // everybody can change his weapons in MP
  else
    _selectWeapons = unit && _canTakeWeapon;

  //RString dir = GetSaveDirectory();
  //RString weapons;
  //if (dir.GetLength() > 0) weapons = dir + RString("weapons.cfg");
  //if (weapons.GetLength() > 0 && QIFileFunctions::FileExists(weapons))
  //  _weaponsInfo.Load(weapons, _pool);

  // _showGearOnExit = _weaponsInfo._weaponsPool.Size() > 0 || _weaponsInfo._magazinesPool.Size() > 0;
  _showGearOnExit = true;
  ConstParamEntryPtr entry = (Pars >> "RscDisplayGetReady").FindEntry("showGearOnExit");
  if (entry) _showGearOnExit = *entry;

  // FIX - cancel user input
  void RemoveInputMessages();
  RemoveInputMessages();

  GChatList.Load(Pars >> "RscChatListBriefing");

#if _ENABLE_IDENTITIES
  _displayDiaryMap = DisplayDiaryInMap(this);
  CListBox *list = dynamic_cast<CListBox *> (this->GetCtrl(IDC_DIARY_TOPIC_LIST));
  if(list && list->Size()>0) 
  {
    if(list && list->Size()>0) 
    {
      if(GWorld->GetMode()  == GModeNetware)
        _displayDiaryMap.ProcessLink("players",DCBriefing);
      else 
        _displayDiaryMap.ProcessLink("diary",DCBriefing);
    }
  }
  _displayDiaryMap.ResetListBox();
#endif
}

DisplayGetReady::~DisplayGetReady()
{
  GChatList.Load(Pars >> "RscChatListDefault");
  GBriefing = NULL;
}

/*!
\patch 1.75 Date 2/7/2002 by Jirka
- Added: Play user defined sound (voice) when section in briefing changes
*/

extern RString GBriefingOnPlan;
extern RString GBriefingOnNotes;
extern RString GBriefingOnGroup;


void DisplayGetReady::SwitchBriefingSection(RString section)
{
  DisplayMap::SwitchBriefingSection(section);
#if _ENABLE_DATADISC
  _sound = NULL;
  if (stricmp(section, "__plan") == 0)
  {
    if (!_soundPlanPlayed)
    {
      PlaySound(GBriefingOnPlan);
      _soundPlanPlayed = true;
    }
  }
  else if (stricmp(section, "__briefing") == 0)
  {
    if (!_soundNotesPlayed)
    {
      PlaySound(GBriefingOnNotes);
      _soundNotesPlayed = true;
    }
  }
  else if (stricmp(section, "group") == 0)
  {
    if (!_soundGroupPlayed)
    {
      PlaySound(GBriefingOnGroup);
      _soundGroupPlayed = true;
    }
  }
#endif
}

ConstParamEntryPtr FindSound(RString name, SoundPars &pars);

void DisplayGetReady::PlaySound(RString name)
{
  if (name.GetLength() == 0) return;

  SoundPars pars;
  if (!FindSound(name, pars)) return;
  _sound = GSoundScene->OpenAndPlayOnce2D
    (
    pars.name, pars.vol, pars.freq, false
    );
  _sound->SetKind(WaveMusic);
  _sound->SetSticky(true);
}

void DisplayGetReady::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_OK:
    Display::OnButtonClicked(idc);
    // before proceeding make sure we have preloading anything necessary
    // time limit can be quite relaxed here
    //GWorld->PreloadAroundCamera(0.5f);
    break;
  case IDC_CANCEL:
    if (GetNetworkManager().IsServer())
    {
      MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(
        MB_BUTTON_OK | MB_BUTTON_CANCEL,
        LocalizeString(IDS_MSG_CONFIRM_RETURN_LOBBY),
        IDD_MSG_TERMINATE_SESSION, false, &buttonOK, &buttonCancel);
    }
    else if (GWorld->GetMode() == GModeNetware)
    {
      MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(
        MB_BUTTON_OK | MB_BUTTON_CANCEL,
        LocalizeString(IDS_MSG_CONFIRM_RETURN_LOBBY_CLIENT),
        IDD_MSG_TERMINATE_SESSION, false, &buttonOK, &buttonCancel);
    }
    else Display::OnButtonClicked(idc);
    break;
  default:
#if _ENABLE_IDENTITIES
    _displayDiaryMap.OnButtonClicked(idc);
#endif
    DisplayMap::OnButtonClicked(idc);
    break;
  }
}


void DisplayGetReady::OnChildDestroyed(int idd, int exit)
{
#if _ENABLE_IDENTITIES
  _displayDiaryMap.OnChildDestroyed(idd,exit, _child);
#endif

  DisplayMap::OnChildDestroyed(idd, exit);

  switch (idd)
  {
  case IDD_INVENTORY:
    if (exit == IDC_OK) Display::OnButtonClicked(IDC_OK);
    break;
  case IDD_MSG_TERMINATE_SESSION:
    if (exit == IDC_OK) Exit(IDC_CANCEL);
    break;
#if _GAMES_FOR_WINDOWS
  case IDD_MSG_ACCEPT_INVITATION:
    if (exit == IDC_OK)
    {
      Exit(IDC_CANCEL);
    }
    else
    {
      // Invitation rejected
      GSaveSystem.InvitationHandled();
    }
    break;    
#endif
  }
}

void DisplayGetReady::OnSimulate(EntityAI *vehicle)
{
  /// we can preload any data as necessary here
  if (!_map->_interpolator && !IsDedicatedClient())
  {
    GWorld->PreloadAroundCamera(0.030f); // keep reasonable framerate
  }

#if _ENABLE_IDENTITIES
  _displayDiaryMap.UpdateTopics();
  _displayDiaryMap.UpdateDiary();
  _displayDiaryMap.ResetListBox();
#endif

  // avoid DisplayMapMain::OnSimulate
  DisplayMap::OnSimulate(vehicle);
}

void DisplayGetReady::OnLBSelChanged(IControl *ctrl, int curSel)
{
#if _ENABLE_IDENTITIES
  _displayDiaryMap.OnLBSelChanged(ctrl,  curSel);
#endif
  DisplayMap::OnLBSelChanged(ctrl,curSel);
}

void DisplayGetReady::OnLBDblClick(int idc,  int curSel)
{ 
#if _ENABLE_IDENTITIES
  _displayDiaryMap.OnLBDblClick(idc,  curSel);
#endif
  DisplayMap::OnLBDblClick(idc,  curSel);
}

void DeleteVehicle(Entity *veh);
void DisplayGetReady::Destroy()
{
  DisplayMap::Destroy();
}

void DisplayGetReady::ProcessDiaryLink(RString link,DiaryContext contex)
{
#if _ENABLE_IDENTITIES
  _displayDiaryMap.ProcessLink(link, contex);
#endif
}

static const RStringB LowCaseName(RStringB name)
{
  if (name.IsLowCase()) return name;
  RString copy = name;
  copy.Lower();
  return copy;
}
Notepad::Notepad(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : ControlObjectContainer(parent, idc, cls)
{
  _paper1 = GlobLoadTexture(LowCaseName(cls >> "paper1"));
  _paper2 = GlobLoadTexture(LowCaseName(cls >> "paper2"));
  _paper3 = GlobLoadTexture(LowCaseName(cls >> "paper3"));
  _paper4 = GlobLoadTexture(LowCaseName(cls >> "paper4"));
  _paper5 = GlobLoadTexture(LowCaseName(cls >> "paper5"));
  _paper6 = GlobLoadTexture(LowCaseName(cls >> "paper6"));
  _paper7 = GlobLoadTexture(LowCaseName(cls >> "paper7"));
  _paperSelection = cls >> "selectionPaper";
  _shape->AddLoadHandler(this);
  _shape->SetCanUsePushBuffer(false);
}

Notepad::~Notepad()
{
  _shape->RemoveLoadHandler(this);
}

void Notepad::LODShapeLoaded(LODShape *shape)
{
  _paper.Init(_shape, _paperSelection, NULL);
}

void Notepad::LODShapeUnloaded(LODShape *shape)
{
}

void Notepad::ShapeLoaded(LODShape *shape, int level)
{
  _paper.InitLevel(_shape, level);
}

void Notepad::ShapeUnloaded(LODShape *shape, int level)
{
  _paper.DeinitLevel(_shape, level);
}

void Notepad::SetPosition(Vector3Par pos)
{
  ControlObjectContainer::SetPosition(pos);

  DisplayMap *parent = dynamic_cast<DisplayMap *>(_parent.GetLink());
  if (parent) parent->AdjustMapVisibleRect();
}

void Notepad::Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2)
{
  if (!_briefing) return;
  switch (_briefing->ActiveBookmark())
  {
  case 0:
    _paper.SetTexture(animContext, _shape, level, _paper1);
    break;
  case 1:
    _paper.SetTexture(animContext, _shape, level, _paper2);
    break;
  case 2:
    _paper.SetTexture(animContext, _shape, level, _paper3);
    break;
  case 3:
    _paper.SetTexture(animContext, _shape, level, _paper4);
    break;
  case 4:
    _paper.SetTexture(animContext, _shape, level, _paper6);
    break;
  case 5:
    _paper.SetTexture(animContext, _shape, level, _paper7);
    break;
  default:
    _paper.SetTexture(animContext, _shape, level, _paper5);
    break;
  }
}

void Notepad::Deanimate(int level, bool setEngineStuff)
{
}

ControlObjectAnimated::ControlObjectAnimated(ControlsContainer *parent, int idc, ParamEntryPar cls, bool ui)
  : base(parent, idc, cls, ui)
{
  _par = cls.GetClassInterface();
  //  _shape->AddLoadHandler(this);
}

ControlObjectAnimated::~ControlObjectAnimated()
{
  //  _shape->RemoveLoadHandler(this);
}

void ControlObjectAnimated::LODShapeLoaded(LODShape *shape)
{
  Assert(shape == _shape);

  _shape->SetAnimationType(AnimTypeHardware);
  _shape->LoadSkeletonFromSource();

  ConstParamEntryPtr array = _par->FindEntry("Animations");
  if (array)
  {
    RptF("Obsolete class Animations defined in %s",cc_cast(_par->GetContext()));
    }
  _animSources.Load(this, GetAnimations());
}

void ControlObjectAnimated::LODShapeUnloaded(LODShape *shape)
{
  _animSources.Clear();
}

void ControlObjectAnimated::ShapeLoaded(LODShape *shape, int level)
{
}

void ControlObjectAnimated::ShapeUnloaded(LODShape *shape, int level)
{
}

bool ControlObjectAnimated::PrepareShapeDrawMatrices(Matrix4Array &matrices, const ObjectVisualState &vs, LODShape *shape, int level)
{
  const Skeleton *sk = _shape->GetSkeleton();
  if (!sk)
  {
    RptF("No skeleton for shape %s",cc_cast(shape->GetName()));
    return false;
  }
  _shape->InitDrawMatrices(matrices,level);

  base::PrepareShapeDrawMatrices(matrices,vs,shape,level);

  return _animSources.PrepareShapeDrawMatrices(matrices, _shape, level, vs, this, GetAnimations());
}

void ControlObjectAnimated::Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2)
{
}

void ControlObjectAnimated::Deanimate(int level, bool setEngineStuff)
{
}

///////////////////////////////////////////////////////////////////////////////
// creation of main display

AbstractOptionsUI *CreateMainMapUI()
{
  return new DisplayMainMap(NULL);
}

void DisplayMainMap::DestroyHUD(int exit)
{
  GLOB_WORLD->DestroyMap(exit);
}

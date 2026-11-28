#ifdef _MSC_VER
#pragma once
#endif

#ifndef _UI_MAP_HPP
#define _UI_MAP_HPP

#include "uiControls.hpp"
#include "../global.hpp"
#include "../engine.hpp"
#include "../world.hpp"
#include "../arcadeTemplate.hpp"
#include "../AI/ai.hpp"
#include "../interpol.hpp"
#include <El/Network/netXboxConfig.hpp>
#include "../progress.hpp"
#include <lib/Inventory/map.hpp>

/*!
\file
Interface file for particular displays (maps, briefing, mission editor).
*/

bool PlaceItemInRectangle(Matrix4 &result, const InventoryItem *item, float x, float y, float w, float h, float z);

//! Structure describes coordinates in map control
struct MapCoord
{
  //! x coordinate
  int x;
  //! y coordinate
  int y;
  //! constructor
  MapCoord(int xx, int yy) {x = xx; y = yy;}
  //! constructor without initialization
  MapCoord() {} // no init
};

//! Type of editable object in map
enum SignType
{
  signNone,
  signUnit,
  /// vehicle as listed in the center target list
  signVehicle,
  /// vehicle as listed in the group target list
  signVehicleGroup,
  /// static object
  signStatic,
  signArcadeUnit,
  signArcadeMarker,
#if _ENABLE_EDITOR2
  signEditorObject,
#endif
#if _ENABLE_IDENTITIES
  signTask,
  signCustomMark
#endif
};

//! Description of editable object in map
struct SignInfo
{
  //! type of object
  enum SignType _type;
  //! concrete unit
  OLinkPerm<AIBrain> _unit;
  //! concrete entity
  TargetId _id;
  //! position
  Point3 _pos;
  //! index of object
  int _index;
#if _ENABLE_EDITOR2
  RString _name;
#endif

  SignInfo();
};
TypeContainsOLink(SignInfo)

//! Briefing notepad control
class Notepad : public ControlObjectContainer, public LODShapeLoadHandler
{
protected:
  //! paper (active area) selection
  AnimationSection _paper;
  //! name of selection
  RString _paperSelection;
  //@{
  //! paper texture
  Ref<Texture> _paper1;
  Ref<Texture> _paper2;
  Ref<Texture> _paper3;
  Ref<Texture> _paper4;
  Ref<Texture> _paper5;
  Ref<Texture> _paper6;
  Ref<Texture> _paper7;
  //@}

  //! briefing HTML control
  InitPtr<CHTMLContainer> _briefing;
public:
  //! constructor
  /*!
  Used when control is created by resource template.
  \param parent control container by which this control is owned
  \param idc id of control
  \param cls resource template
  */
  Notepad(ControlsContainer *parent, int idc, ParamEntryPar cls);
  ~Notepad();
  void SetPosition(Vector3Par pos);
  void Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2);
  void Deanimate(int level, bool setEngineStuff);
  //! attach briefing to notepad
  void SetBriefing(CHTMLContainer *briefing) {_briefing = briefing;}

  //@{ LODShapeLoadHandler implementation
  virtual void LODShapeLoaded(LODShape *shape);
  virtual void LODShapeUnloaded(LODShape *shape);
  virtual void ShapeLoaded(LODShape *shape, int level);
  virtual void ShapeUnloaded(LODShape *shape, int level);
  //@}
};

// Inventory screen support

struct DragItemInfo
{
  InventoryItem *_item;
  float _offsetX;
  float _offsetY;
};

// Base object for inventory items handling, support of drag & drop
class CItemBase : public ControlObject
{
public:
  // 2D position
  float _x, _y, _w, _h;
  // depth
  float _z;

  // background color
  PackedColor _bgColor;
  // border color
  PackedColor _borderColor;

protected:
  // offset where object was dragged
  float _dragX, _dragY;

  // drag & drop operation activated
  bool _dragging;

public:
  CItemBase(ControlsContainer *parent, int idc, ParamEntryPar cls);

  // referenced item on given position
  virtual DragItemInfo GetItemInfo(float x, float y) = 0;
  // size of the single grid item
  virtual float GetGridW() = 0;
  virtual float GetGridH() = 0;
  // handle actions on items
  virtual int GetShortcutIndex() {return -1;}
  virtual bool CanAddItem(InventoryItem *item, float x, float y) = 0;
  virtual void AddItem(InventoryItem *item, float x, float y) = 0;
  virtual bool IsContained(InventoryItem *item) = 0;

  // item to be dragged
  DragItemInfo GetDragItemInfo() {return GetItemInfo(_dragX, _dragY);}

  virtual bool IsInside(float x, float y) const override;

  virtual void OnLButtonDown(float x, float y) override;
  virtual void OnLButtonUp(float x, float y) override;
  virtual void OnMouseMove(float x, float y, bool active = true) override;
  virtual void OnLButtonDblClick(float x, float y) override;
  virtual void OnRButtonClick(float x, float y) override;

  virtual void OnDrawBackGround(UIViewport *vp, float alpha) override;
  virtual void DrawTooltip(UIViewport *vp, float x, float y) override;
  virtual intptr_t GetTooltipArea(float x, float y) override;

  static void DrawItem(InventoryItem *item, float x, float y, float w, float h, float z, float alpha, Color color, Matrix3 *rot = nullptr);

  // implementation of items manipulation
  static void SetItemShortcut(int index, const InventoryItem *item);
  static void DropItem(Person *owner, InventoryItem *item);
  static void MoveItem(EntityAI *target, int index, InventoryItem *item);
  static void RotateInventoryItem(Person *person, InventoryItem *item);
  static void TakeItemToHands(Person *person, InventoryItem *item);

  // test if MoveItem will succeed
  static bool CanMoveItem(EntityAI *target, int index, const InventoryItem *item);
};

// helper class to display inventory item shortcut
class CItemShortcut : public CItemBase
{
protected:
  typedef CItemBase base;

  // shortcut index in InGameUI
  int _index;
  // keyboard shortcut
  RString _shortcut;

  // text color
  PackedColor _textColor;

  // background color of item in hands
  PackedColor _bgColorInHands;
  // inactive background color
  PackedColor _bgColorInactive;
  // inactive border color
  PackedColor _borderColorInactive;
  // inactive text color
  PackedColor _textColorInactive;

  Ref<Font> _font;
  float _size;

  static UITime _lastTime;
  static const float _startDim, _endDim;

  // shown in the HUD
  bool _igui;

public:
  CItemShortcut(ControlsContainer *parent, int idc, ParamEntryPar cls, bool igui);

  virtual void OnDraw(float alpha) override;
  virtual void OnDrawBackGround(UIViewport *vp, float alpha) override;
  virtual void OnDrawForeGround(UIViewport *vp, float alpha) override;

  // CItemBase implementation
  virtual DragItemInfo GetItemInfo(float x, float y) override;
  virtual int GetShortcutIndex() override {return _index;}
  virtual bool CanAddItem(InventoryItem *item, float x, float y) override;
  virtual void AddItem(InventoryItem *item, float x, float y) override;
  virtual float GetGridW() override;
  virtual float GetGridH() override;
  virtual bool IsContained(InventoryItem *item) override;
  virtual void DrawTooltip(UIViewport *vp, float x, float y) override;

  InventoryItem *GetItem();

  static void ShowShortcuts() {_lastTime = Glob.uiTime;}
  static void ToggleShortcuts();
};


class ControlObjectAnimated : public ControlObjectWithZoom, public LODShapeLoadHandler, public AnimatedType, public Animated
{
  typedef ControlObjectWithZoom base;

protected:
  /// source values for _animations
  AnimationSourceHolder _animSources;
  /// config parameters
  ConstParamClassPtr _par;

public:
  //! constructor
  /*!
  Used when control is created by resource template.
  \param parent control container by which this control is owned
  \param idc id of control
  \param cls resource template
  */
  ControlObjectAnimated(ControlsContainer *parent, int idc, ParamEntryPar cls, bool ui);
  ~ControlObjectAnimated();

  //@{ LODShapeLoadHandler implementation
  virtual void LODShapeLoaded(LODShape *shape);
  virtual void LODShapeUnloaded(LODShape *shape);
  virtual void ShapeLoaded(LODShape *shape, int level);
  virtual void ShapeUnloaded(LODShape *shape, int level);
  //@}

  virtual bool PrepareShapeDrawMatrices(Matrix4Array &matrices, const ObjectVisualState &vs, LODShape *shape, int level);
  virtual void Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2);
  virtual void Deanimate(int level, bool setEngineStuff);
  virtual int PassNum(int lod) {return 4; /* UI Pass*/ }

  /// encapsulate access to animations
  const AnimationHolder &GetAnimations() const
  {
    // prefer shape based animations
    if (_shape && _shape->GetAnimations())
    {
      return *_shape->GetAnimations();
    }
    return AnimationHolder::Empty();
  }
};

//! Describes map object type
struct MapTypeInfoTask : MapTypeInfo
{
  //! picture
  Ref<Texture> iconCreated;
  Ref<Texture> iconCanceled;
  Ref<Texture> iconDone;
  Ref<Texture> iconFailed;

  PackedColor colorCreated;
  PackedColor colorCanceled;
  PackedColor colorDone;
  PackedColor colorFailed;
  //! reads description from resource
  virtual void Load(ParamEntryPar cls);
};

// forward declare
class MapObjectList;

//! Describes single animation state of map
struct MapAnimationPhase
{
  //! time of state
  UITime time;
  //! scale of map
  float scale;
  //! map center position in world coordinates
  Vector3 pos;
};
TypeIsSimple(MapAnimationPhase);

class MapObject;
class MapObjectListRectUsed;

#include "../engine.hpp"

//! types of cursor used in map
enum CursorType
{
  CursorArrow,
  CursorTrack,
  CursorMove,
  CursorScroll,
  CursorWatch,
  CursorRotate,
#if _VBS3
  CursorRaise,
  CursorDrag,
#else
  // Mission editor cursors (3D mode)
  CursorTrack3D,
  CursorMove3D,
  CursorRotate3D,
  CursorRaise3D,
#endif
  //dummy cursor, so i can determine size of enum
  NCursorType
};

//! Generic map control
class CStaticMap : public CStatic
{
public:
  static const int AllocLines = 500;
  typedef AutoArray<Line2DPixelInfo, MemAllocLocal<Line2DPixelInfo,AllocLines> > LineArray;
  //@{
  //! position (top left point) of "window" on whole map
  float _mapX;
  float _mapY;
  //@}

  //! mouse position in world coordinates
  Vector3 _mouseWorld;
  //! default map center position in world coordinates
  /*!
  Defined in config for every island.
  Used for map center when no player is displayed.
  */
  Vector3 _defaultCenter;

  //! map object thats may becomes selected
  struct SignInfo _infoClickCandidate;
  //! selected map object
  struct SignInfo _infoClick;
  //! map object map is over
  struct SignInfo _infoMove;

  //@{
  //! current map scale
  float _scaleX;
  float _scaleY;
  float _invScaleX;
  float _invScaleY;
  //@}

  //@{
  //! current map area
  float _minX, _maxX, _minY, _maxY;
  //@}

  // properties used when no item map is in use
  MapProperties _defaultProperties;

  //  bool _moveOnEdges;

  //! position of map control center
  /*!
  May not be in real center of control, for example if notepad hides some area of map.
  */
  Point2DFloat _center;
  Point2DFloat _halfSize;

  //@{
  //! precalculates for increasing drawing speed
  float _wScreen;
  float _hScreen;
  Rect2DPixel _clipRect;
  int _mipmapLevel;
  //@}

  //! locked map region to avoid repeated reloading
  SRef<MapObjectListRectUsed> _mapUsed;

  //@{
  //! animation implementation
  UITime _animationStart;
  UITime _animationLast;
  AutoArray<MapAnimationPhase> _animation;
  SRefArray<Matrix4> _animMatrices;
  SRefArray<float> _animTimes;
  SRef<M4Function> _interpolator;
  //@}

  //@{
  //! moving implementation
  UITime _moveStart;
  UITime _moveLast;
  unsigned _moveKey;
  //@}

  //! map is moving
  bool _moving;
  //! some object is dragging
  bool _dragging;
  //! some area is selecting
  bool _selecting;

#if _ENABLE_CHEATS
  //@{
  //! debug show mode
  bool _show;
  bool _showCost;
  //@}
#endif
  //! show IDs of objects
  bool _showIds;
  //! show Grayscale / Textures
  bool _showScale;
  //! map texture alpha
  float _showScaleAlpha;

  //@{ coefficients which determine rendering density / threshold
  float _ptsPerSquareCost;  // cost
  //@}

  //! player's punctuation picture
  Ref<Texture> _iconPlayer;
  //! selected unit's punctuation picture
  Ref<Texture> _iconSelect;
  //! camera effect picture
  Ref<Texture> _iconCamera;
  //! trigger picture
  Ref<Texture> _iconSensor;
  //! color of friendly units
  PackedColor _colorFriendly;
  //! color of enemy units
  PackedColor _colorEnemy;
  //! color of civilians
  PackedColor _colorCivilian;
  //! color of neutral units
  PackedColor _colorNeutral;
  //! color of unknown units
  PackedColor _colorUnknown;
  //! color of player unit's punctuation
  PackedColor _colorMe;
  //! secondary color of player unit's punctuation
  PackedColor _colorMe2;
  //! color of playable unit's punctuation
  PackedColor _colorPlayable;
  //! color of selected unit's punctuation
  PackedColor _colorSelect;
  //! trigger color
  PackedColor _colorSensor;
  //! color of line for dragged object (in group or synchronization mode)
  PackedColor _colorDragging;

  int _shadow;

  //! color of camera effect picture
  PackedColor _colorCamera;
  //! not used
  PackedColor _colorMissions;
  //! color of waypoint lines
  PackedColor _colorActiveMission;

  //! color of strategic path for camera unit
  PackedColor _colorPath;
  //! color of completed part of strategic path for camera unit
  PackedColor _colorPathDone;
  //! strategic path texture
  Ref<Texture> _texturePath;
  //! completed part of strategic path texture
  Ref<Texture> _texturePathDone;
  //! strategic path texture size
  float _sizePath;
  //! completed part of strategic path texture size
  float _sizePathDone;

  //! color of label for object mouse is over
  PackedColor _colorInfoMove;
  //! color of group lines
  PackedColor _colorGroups;
  //! color of group lines for active group
  PackedColor _colorActiveGroup;
  //! color of synchronization lines
  PackedColor _colorSync;
  //! background color of label
  PackedColor _colorLabelBackground;

  //! width of railway lines
  //float _widthRailWay;

  //! Displays the grid Ids over the lines (VBS)
  bool _gridNumbersOverLines; 

  //! other units size
  float _sizeUnit;
  //! Player indication size
  float _sizePlayer;

  //! coefficient of cursor line color alpha
  float _cursorLineWidth;

  float _blinkingSpeedMe;

  //@{
  //! mouse position in world coordinates in special cases
  Vector3 _special;
  Vector3 _lastPos;
  //@}
  //  DrawCoord _dragOffset;

  ////////////////////////////////////
  //                                //
  // NEW PROPERTIES - from resource //
  //                                //
  ////////////////////////////////////

  //satellite map fading
  float _maxSatelliteAlpha;
  float _alphaFadeStartScale;
  float _alphaFadeEndScale;

  //! color modifier for inactive objects
  PackedColor _colorInactive;

  //! font used for labels
  Ref<Font> _fontLabel;
  //! font size
  float _sizeLabel;
  //! font used for enemy info
  Ref<Font> _fontInfo;
  //! font size
  float _sizeInfo;

  bool _miniMapMode; //when true, player icon is drawn even for Disabled DTMap

  bool _legendDisabled; // legend is disabled temporarily

  RString _cursorMapOverride[NCursorType];
  RString _cursorName;

#if _VBS3 // markers moved to CStaticMap
  bool _onlyDrawMarkers;
  bool _drawAttachedMarkers;
  bool _drawConditionalMarkers;
# if _VBS3_UDEFCHART
  //! List of cached user textures
  RefArray<Texture> _userTextures;
  //! List of texture names whose file has not been found
  AutoArray<RStringB> _userTexturesNotFound;
# endif
#endif //_VBS3

public:
  //! constructor
  /*!
  Used when control is created by resource template.
  \param parent control container by which this control is owned
  \param idc id of control
  \param cls resource template
  \param scaleMin minimal map scale
  \param scaleMax maximal map scale
  \param scaleDefault default map scale
  */
  CStaticMap
    (
    ControlsContainer *parent, int idc, ParamEntryPar cls,
    float scaleMin, float scaleMax, float scaleDefault
    );
  ~CStaticMap();

  const MapProperties &GetMapProperties() const;

  //! initialize the default center based on current global world
  void InitDefaultCenter();

#if _VBS3
  Rect2DAbs MapArea();
#endif

  void OnRButtonDown(float x, float y);
  void OnRButtonUp(float x, float y);
  void OnMouseMove(float x, float y, bool active = true);
  void OnMouseHold(float x, float y, bool active = true);
  virtual void OnMouseZChanged(float dz);
  virtual bool OnKeyDown(int dikCode);
  virtual bool OnKeyUp(int dikCode);

  //! processing of cheats (do nothing in SuperRelease)
  virtual void ProcessCheats();

  virtual void OnDraw(UIViewport *vp, float alpha);
  virtual void OnHide();

  virtual void DrawExt( float alpha ) {}

  //! initialization
  void Reset();

  virtual bool IsShowMapForced() const {return false;}

  //! sets actual scale
  void SetScale(float scale);
  //! returns actual scale
  float GetScale() const {return _scaleX;}

  //! sets visible area of map (calculates _center)
  void SetVisibleRect(float x, float y, float w, float h);  // in screen coordinates
  //! center map on given position
  void Center(Vector3Val pt, bool soft);
  //! center map (by player unit)
  virtual void Center(bool soft = false) {}
  //! returns position of map center in world coordinates
  Vector3 GetCenter();

  //! clear (interrupt) current map animation
  void ClearAnimation();
  //! adds animation phase to animation
  void AddAnimationPhase(float dt, float scale, Vector3Par pos);
  //! finish creation of animation
  void CreateInterpolator();
  //! Check if some animation is running
  bool HasAnimation() const {return _animation.Size() > 0;}

  //! translates world relative coordinates to screen relative coordinates
  Point2DFloat WorldSizeToScreen(Vector3Val pt) const;
  //! translates world coordinates to screen coordinates
  Point2DFloat WorldToScreen(Vector3Val pt) const;
  //! translates screen coordinates to world coordinates
  Point3 ScreenToWorld(Point2DFloat ptMap) const;

  bool IsMoving() const {return _moving;}

  //@{
  //! scroll (move) map window by <dif>
  void ScrollX(float dif);
  void ScrollY(float dif);
  //@}

  //! Is showing of objects' IDs enabled
  bool IsShowingIds() const {return _showIds;}
  //! Show / hide objects' IDs
  void ShowIds(bool show = true) {_showIds = show;}

  //! Is showing greyscale ore textures
  bool IsShowingScale() const {return _showScale;}
  //! Show grayscale / textures
  void ShowScale(bool show = true) {_showScale = show;}

  void SetScales(float scaleMin, float scaleMax, float scaleDefault);

  virtual void SetFont(Font *font) {_defaultProperties._fontNames = font;}
  virtual void SetFontHeight(float h) {_defaultProperties._sizeNames = h;}
#if _VBS3 // getFontHeight
  virtual float GetFontHeight() {return _defaultProperties._sizeNames;}
#endif

#if _VBS2 // conditional markers
  bool DrawSignAutosize
    (
    Texture *texture, PackedColor color,
    Vector3Val pos, float w, float h, float azimut, RString text = RString(), bool shadow = false,
    Font *font = NULL, float textSize = -1 , bool autosize = true
    );
#endif

  //! draw map sign
  /*!
  \param texture texture of picture
  \param color color of picture
  \param pos position in world coordinates
  \param w width of sign (in pixels for resolution 640 x 480)
  \param h height of sign (in pixels for resolution 640 x 480)
  \param azimut rotation of sign
  \param text additional text (drawn on the right side of sign)
  \param font text font
  \param textSize base text size (can be modified in some zoom levels)
  */
  bool DrawSign(Texture *texture, PackedColor color,
    Vector3Val pos, float w, float h, float azimut, RString text = RString(),
    Font *font = NULL, float textSize = -1, int shadow = 0);

  //! draw line in map
  /*!
  \param pos1 start position in world coordinates
  \param pos2 end position in world coordinates
  \param color color of line
  */
  void DrawLine(Vector3Val pos1, Vector3Val pos2, PackedColor color);

  //! draw arrow in map
  /*!
  \param pos1 start position in world coordinates
  \param pos2 end position in world coordinates
  \param color color of arrow
  */
  void DrawArrow(Vector3Val pos1, Vector3Val pos2, PackedColor color);

  //! draw ellipse in map
  /*!
  \param position center of ellipse in world coordinates
  \param a axis a of ellipse (in meters)
  \param b axis b of ellipse (in meters)
  \param angle rotation
  \param color border color
  \param dashFill size of a solid part when rendering dashed
  \param dashGap size of a gap when rendering dashed
  */
  void DrawEllipse(
    Vector3Val position, float a, float b, float angle, PackedColor color,
    float dashSolid=FLT_MAX, float dashGap=0
    );
  //! draw rectangle in map
  /*!
  \param position center of ellipse in world coordinates
  \param a side a of rectangle (in meters)
  \param b side b of rectangle (in meters)
  \param angle rotation
  \param color border color
  */
  void DrawRectangle(Vector3Val position, float a, float b, float angle, PackedColor color);
  //! draw filled ellipse in map
  /*!
  \param position center of ellipse in world coordinates
  \param a axis a of ellipse (in meters)
  \param b axis b of ellipse (in meters)
  \param angle rotation
  \param color fill color
  \param texture fill texture
  */
  void FillEllipse(Vector3Val position, float a, float b, float angle, PackedColor color, Texture *texture);
  //! draw filled rectangle in map
  /*!
  \param position center of ellipse in world coordinates
  \param a side a of rectangle (in meters)
  \param b side b of rectangle (in meters)
  \param angle rotation
  \param color fill color
  \param texture fill texture
  */
  void FillRectangle(Vector3Val position, float a, float b, float angle, PackedColor color, Texture *texture);

  //! draw mount dimension
  void DrawMount(Vector3Par pos);
  //! draw (town or village) name
  void DrawName(Vector3Par pos, RString text, Font *font, float textSize, PackedColor color, int shadow);

  //!set override texture for given cursor texture type
  void SetOverrideCursor(RString cursorName, RString overrideTexture);
  //!get override texture for given texture type
  RString OverrideCursor(RString cursorName);
  //!get override texture for given texture type
  RString OverrideCursor(CursorType cursorType);


  // implementation
protected:
  //VBS:
  //! Flag to determine the user map is selected
  bool UserMapDrawing() const;
  //! Flag to determine the user map is being drawn
  bool UserMapIsBeingDrawn() const;
  //! Flag to determine objects should be drawn on the user map
  bool UserMapDrawObjects() const;

  //! precalculates constans to optimize some calculations
  void Precalculate(const MapProperties &props);
  //! check if x map coordinate is valid (and saturate it)
  void SaturateX(float &x);
  //! check if y map coordinate is valid (and saturate it)
  void SaturateY(float &y);
  /// keep scale in the range, if change is needed do not move 
  void SaturateScale(const MapProperties &props);

  //! draw map sign
  /*!
  \param texture texture of picture
  \param color color of picture
  \param posMap position in map coordinates
  \param w width of sign (in pixels for resolution 640 x 480)
  \param h height of sign (in pixels for resolution 640 x 480)
  \param azimut rotation of sign
  \param text additional text (drawn on the right side of sign)
  \param font text font
  \param textSize base text size (can be modified in some zoom levels)
  */
  bool DrawSign(Texture *texture, PackedColor color,
    DrawCoord posMap, float w, float h, float azimut, RString text = RString(),
    Font *font = NULL, float textSize = -1, bool textSizeConstant = false, int shadow = 0);
  //! draw line in map
  /*!
  \param posMap1 start position in map coordinates
  \param posMap2 end position in map coordinates
  \param color color of line
  */
  void DrawLine(DrawCoord posMap1, DrawCoord posMap2, PackedColor color);
  //! draw map sign
  /*!
  \param info description of map object type
  \param pos position in world coordinates
  */
  bool DrawSign(const MapTypeInfo &info, Vector3Val pos);
  //! draw label
  /*!
  \param info editable map object info
  \param color text color
  */
  virtual void DrawLabel(struct SignInfo &info, PackedColor color, int shadow) {}
  //! draw all airports on the map
  void DrawAirports(const MapProperties &props);
  //! draw single runway/taxiway rectangle
  void DrawRunway(const MapProperties &props, const Vector3 *runway);
  /// draw autopilot taxi guides
  void DrawTaxiGuide(const MapProperties &props, const AutoArray<Vector3> &guide);
  //! draw map background (all informations from Landscape - sea, countlines, forests, roads, objects etc.)
  void DrawBackground(const MapProperties &props);
  /// if we know where is the map moving, we can preload
  void PreloadData(Vector3Par center);
  //! draw map legend
  void DrawLegend();
  //! draw town or village name
  void DrawName(ParamEntryPar cls);
  //! draw real landscape texture in given square
  /*!
  \param x x position on screen
  \param y y position on screen
  \param xStep width of square on screen
  \param yStep height of square on screen
  \param i x position in landscape (in LandGrid coordinates)
  \param j z position in landscape (in LandGrid coordinates)
  \param iStep width in landscape (in LandGrid coordinates)
  \param jStep height in landscape (in LandGrid coordinates)

  */
  void DrawField
    (
    float x, float y, float xStep, float yStep,
    int i, int j, int iStep, int jStep
    );
  //! draw height scale in given square
  /*!
  \param x x position on screen
  \param y y position on screen
  \param xStep width of square on screen
  \param yStep height of square on screen
  \param i x position in landscape (in LandGrid coordinates)
  \param j z position in landscape (in LandGrid coordinates)
  \param iStep width in landscape (in LandGrid coordinates)
  \param jStep height in landscape (in LandGrid coordinates)

  */
  void DrawScale
    (
    float x, float y, float xStep, float yStep,
    int i, int j, int iStep, int jStep
    );
  //! draw sea in given square
  /*!
  \param x x position on screen
  \param y y position on screen
  \param xStep width of square on screen
  \param yStep height of square on screen
  \param i x position in landscape (in LandGrid coordinates)
  \param j z position in landscape (in LandGrid coordinates)
  \param iStep width in landscape (in LandGrid coordinates)
  \param jStep height in landscape (in LandGrid coordinates)

  */
  void DrawSea(
    const MapProperties &props,
    float x, float y, float xStep, float yStep,
    int i, int j, int iStep, int jStep
    );
  //! draw abstract isometric lines
  /*!
  \param pt array of 4 bounding points of square
  \param height array of 4 values in bounding points
  \param step step of value for which lines are drawn
  \param stepMain step of value for which thicker lines are drawn
  \param minLevel minimal value for which line is drawn
  \param maxLevel maximal value for which line is drawn
  \param color lines color
  \param colorMain thicker lines color
  */

  void DrawLines
    (
    LineArray &lines, DrawCoord *pt, float *height,
    float step, float stepMain, float minLevel, float maxLevel, PackedColor color, PackedColor colorMain
    );
  //! draw countlines in given square
  /*!
  \param x x position on screen
  \param y y position on screen
  \param xStep width of square on screen
  \param yStep height of square on screen
  \param i x position in landscape (in LandGrid coordinates)
  \param j z position in landscape (in LandGrid coordinates)
  \param iStep width in landscape (in LandGrid coordinates)
  \param jStep height in landscape (in LandGrid coordinates)
  \param step step of height for which lines are drawn
  \param stepMain step of height for which thicker lines are drawn
  \param maxLevel maximal absolute value for which line is drawn
  */
  void DrawCountlines
    (
    const MapProperties &props, LineArray &lines,
    float x, float y, float xStep, float yStep,
    int i, int j, int iStep, int jStep,
    float step, float stepMain, float maxLevel
    );
  //{@ helper functors DrawForests etc.
  struct DrawForestContext;
  struct DrawForestsNewFunc;
  struct DrawObjectsContext;
  struct DrawRoadContext;
  //@}

  //! draw forests in given square - new version (using iso-arbors)
  /*!
  Drawing is performed by single landscape squares.
  \param i x position in landscape (in LandGrid coordinates)
  \param j z position in landscape (in LandGrid coordinates)
  */
  void DrawForestsNew(
    const MapObjectList &list, int i, int j, const DrawForestContext &ctx, bool drawLines, bool rocks
    );
  //! draw objects in given square
  /*!
  Drawing is performed by single landscape squares.
  \param i x position in landscape (in LandGrid coordinates)
  \param j z position in landscape (in LandGrid coordinates)
  */
  void DrawObjects(const MapProperties &props, const MapObjectList &list, int i, int j);

  //! draw roads in given square
  /*!
  Drawing is performed by single landscape squares.
  \param i x position in landscape (in LandGrid coordinates)
  \param j z position in landscape (in LandGrid coordinates)
  */
  void DrawRoads(const MapProperties &props, LineArray &lines, const MapObjectList &list, int i, int j
    );
  //! draw map grid
  void DrawGrid(const MapProperties &props);

  //! find editable map object on given screen coordinates (<x>, <y>)
  virtual SignInfo FindSign(float x, float y) {return SignInfo();}

  //! draw label implementation
  /*!
  \param pos world position
  \param text label
  \param color text color
  */
  void DrawLabel(Vector3Par pos, RString text, PackedColor color, int shadow);

  /// draw building shape
  void DrawBuilding(MapObject *obj, PackedColor color);

  //! calculate color for inactive item from base color
  PackedColor InactiveColor(PackedColor color);
};

//! weather state (used for selection of icon for weather in mission editor)
enum WeatherState
{
  WSUndefined,
  WSClear,
  WSCloudly,
  WSOvercast,
  WSRainy,
  WSStormy
};

//! translates weather state into icon type
WeatherState GetWeatherState(float overcast);

//! base class for 2D notebook display (currently used only for mission editor)
class DisplayNotebook : public Display
{
protected:
  /*
  Ref<Texture> _glassA;
  Ref<Texture> _glassB;
  Ref<Texture> _glassC;
  Ref<Texture> _glassD;
  Ref<Texture> _glassE;
  */

  //! displayed weather
  WeatherState _lastWeather;

  RString _textureClear;
  RString _textureCloudly;
  RString _textureOvercast;
  RString _textureRainy;
  RString _textureStormy;

public:
  //! constructor
  /*!
  \param parent parent display
  */
  DisplayNotebook(ControlsContainer *parent, ParamEntryVal cls);

  //! returns current weather state
  virtual WeatherState GetWeather() = 0;
  //! returns current game time
  virtual Clock GetTime() = 0;
  //! returns current player position
  virtual bool GetPosition(Point3 &pos) = 0;

  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnDraw(EntityAI *vehicle, float alpha);
};



// Main map (ingame)

class DisplayMap;

/*!
\patch 1.01 Date 06/07/2001 by Ondra
- Fixed: friendly units info in map (veteran mode)
- was MapSubgroupInfo before
- information was stored about subgroups, not units
*/

//! Info about reported enemy
struct MapEnemyInfo
{
  //! enemy id
  TargetId id;
  //! enemy type
  Ref<const VehicleType> type;
  //! when report was done
  float time; // report status time
  //@{
  //! reported position
  int x;
  int z;
  //@}

  //! serialization
  LSError Serialize(ParamArchive &ar);
};
TypeContainsOLink(MapEnemyInfo);

//! Encapsulates all infos in main map (units, enemies, commands)
struct MainMapInfo : public SerializeClass
{
  //! enemies
  AutoArray<MapEnemyInfo> enemyInfo;

  LSError Serialize(ParamArchive &ar);
  //! remove all infos
  void Clear();
};

//! Main (ingame) map (and briefing) control
class CStaticMapMain : public CStaticMap
{
protected:
#if _ENABLE_CHEATS
  //@{
  //! debug show mode
  bool _showUnits;
  bool _showTargets;
  unsigned char _showVariables;
  //@}
#endif

  //! focused marker
  int _activeMarker;
  /// continuously center the map around the camera position
  bool _autoCenter;

  //! map object type properties for command sign
  MapTypeInfo _infoCommand;
  //! default icon for the task (if not overridden by the task itself)
  MapTypeInfoTask _infoTask;
  //! icon for the custom mark
  MapTypeInfo _infoCustomMark;

  //! lines color for active marker enforcement
  PackedColor _colorActiveMarker;
  //! square size for active marker enforcement
  float _sizeActiveMarker;

  //! picture for current position in veteran mode (in tanks and air units)
  Ref<Texture> _iconPosition;

  //mouse down has been handled by some UI, therefore click has to be ignored
  bool _mouseClickHandled;

#if _VBS2 // conditional markers
  UITime _lastEvalMarkerConditions; 
#endif

  MainMapInfo _mapInfo;
public:
  //! constructor
  /*!
  Used when control is created by resource template.
  \param parent control container by which this control is owned
  \param idc id of control
  \param cls resource template
  */
  CStaticMapMain(ControlsContainer *parent, int idc, ParamEntryPar cls);

#if _VBS2
  CStaticMapMain
    (
    ControlsContainer *parent, int idc, ParamEntryPar cls,
    float scaleMin, float scaleMax, float scaleDefault
    );
#endif

  void OnLButtonDown(float x, float y);
  void OnLButtonUp(float x, float y);
  void OnLButtonClick(float x, float y);
  void OnLButtonDblClick(float x, float y);
  void OnMouseHold(float x, float y, bool active = true);
  virtual void OnMouseZChanged(float dz);
  virtual bool OnKeyDown(int dikCode);
  virtual void ProcessCheats();
  void DrawExt(float alpha);
  void OnRButtonDown(float x, float y);
  void OnMouseMove(float x, float y, bool active = true );

  virtual bool IsShowMapForced() const override
  {
#if _ENABLE_CHEATS
    return _showUnits || _showTargets;
#else
    return false;
#endif
  }

  //! returns parent display pointer (casted)
  DisplayMap *GetParent();
  void Center(bool soft = false);
  bool CanCenter();
  //! center and scale map using player's direction and speed
  void CenterMiniMap();
  /// enable / disable continuous centering of the map
  void SetAutocenter(bool set) {_autoCenter = set;}

  void OnDraw(UIViewport *vp, float alpha);

#if _ENABLE_CHEATS
  EntityAI *GetObserver() const;
#endif

  //! sets active (focused) marker
  void SetActiveMarker(int marker) {_activeMarker = marker;}

  //! adds info about reported enemy
  void AddEnemyInfo
    (
    TargetType *id, const VehicleType *type, int x, int z
    );

  /// return the target cursor is pointing at
  EntityAI *GetTarget() const;
  /// retrieve the position cursor is pointing at (returns true when succeeded)
  bool GetPosition(Vector3 &pos) const;

  LSError Serialize(ParamArchive &ar, RString name, int minVersion);

#if _VBS3 // integration of real time editor with main map
  virtual bool IsDragging() {return false;}
  virtual void ClearDrawLinks() {}
  virtual bool IsSelecting() {return false;}
  virtual bool IsDraggingRM() {return false;} 
  virtual bool IsRealTime() {return false;}
  virtual void ClearSelectingBox() {}
  virtual void EditLink(EditorObject *obj, RString name, float key = -1, bool assignLinkArg = false) {}
  virtual Ref<EditorObject> GetLinkObject() {return NULL;}
  virtual void SetLinkObject(Ref<EditorObject> obj) {}
  virtual void RemoveAllDrawLinks(EditorObject *from) {}
  virtual GameVarSpace *GetVariables() {return NULL;}
  virtual EditorObject *FindEditorObject(GameValue value,RString type) {return NULL;}
  virtual RString AddObject
    (
    RString typeName, 
    const AutoArray<RString> &args, 
    bool create = true, 
    GameValue variable = NOTHING, 
    ConstParamEntryPtr subTypeConfig = NULL
    ) {return RString();}  
#endif

protected:
  //! returns player unit
  AIBrain *GetMyUnit();

  SignInfo FindSign(float x, float y);

  //! draws field costs (pathfinding) for player unit
  void DrawCost();
  //! draws strategic pathfinding progress
  void DrawPath();
#if _ENABLE_CHEATS
  //! draws status of the operative cache
  void DrawOperCache();
  /// draw how roads are connected
  void DrawRoadNet();
#endif

  //! draws all agents
  void DrawUnits();
  //! draws all units
  void DrawTargets(AIBrain *observerUnit, bool drawCivilians);

  void IssueCommand(float x, float y, bool alt, bool shift);

  //! draws info about friendly units and enemies in each square
  void DrawInfo();
  //! draws all markers
  void DrawMarkers();
  //skip irellevan items 
  bool CheckItemWantedDraw(const TargetNormal *target);

  void DrawLabel(struct SignInfo &info, PackedColor color, int shadow);

  //! draws field costs (pathfinding) in given square
  void DrawCost
    /*!
    \param unit player's unit
    \param x x position on screen
    \param y y position on screen
    \param xStep width of square on screen
    \param yStep height of square on screen
    \param i x position in landscape (in LandGrid coordinates)
    \param j z position in landscape (in LandGrid coordinates)
    \param iStep width in landscape (in LandGrid coordinates)
    \param jStep height in landscape (in LandGrid coordinates)

    */
    (
    const MapProperties &props,
    AIBrain *unit, float invCost,
    float x, float y, float xStep, float yStep,
    int i, int j, int iStep, int jStep
    );
#if _ENABLE_CHEATS
  //! draws status of the operative cache in given square
  void DrawOperCache(float x, float y, float xStep, float yStep, int i, int j, int iStep, int jStep);
  /// draw how roads are connected
  void DrawRoadNet(const MapProperties &props, float x, float y, float xStep, float yStep, int i, int j);
#endif
  //! draws info about friendly units and enemies in given square
  /*!
  \param x x position on screen
  \param y y position on screen
  \param xStep width of square on screen
  \param yStep height of square on screen
  \param i x position in landscape (in LandGrid coordinates)
  \param j z position in landscape (in LandGrid coordinates)
  */
  void DrawInfo(const MapProperties &props, float x, float y, float xStep, float yStep, int i, int j);

  //! find nearest unit
  /*!
  \param center center of searched unit
  \param pt position
  \param retrieved info nearest units description retrieved 
  \param minDist limit of distance, retrieved distance of nearest unit
  */
  void FindUnit(Vector3Par pt, SignInfo &info, float &minDist);
  //! find nearest target
  void FindTarget(Vector3Par pt, SignInfo &info, float &minDist, AIBrain *observerUnit, bool civilians);
};

//! Display for insertion of user marker in main map (or briefing)
class DisplayInsertMarker : public Display
{
public:
  //! written marker text
  RString _text;
  //! selected marker picture
  int _picture;
  //! selected marker color
  int _color;
  //! position of display
  float _x, _y;
  //! clipping rectangle for display
  float _cx, _cy, _cw, _ch;

public:
  //! constructor
  /*!
  \param parent parent display
  \param x position of display
  \param y position of display
  \param cx clipping rectangle for display
  \param cy clipping rectangle for display
  \param cw clipping rectangle for display
  \param ch clipping rectangle for display
  \param enableSimulation enable game simulation
  - true if called from main map
  - false if called from briefing
  */
  DisplayInsertMarker(ControlsContainer *parent, float x, float y, float cx, float cy, float cw, float ch, bool enableSimulation);
  //! destructor
  ~DisplayInsertMarker();

  // Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  virtual bool OnKeyDown(int dikCode);
  virtual bool OnKeyUp(int dikCode);

  void Destroy();

protected:
  //! reads parameters of selected picture from config
  void UpdatePicture();
  //! select previous picture
  void PrevPicture();
  //! select next picture
  void NextPicture();
  //! select previous color
  void PrevColor();
  //! select next color
  void NextColor();
};


//! number of weapon slots (primary weapon, secondary weapon, handgun, two special items and 12 inventory slots)
#define WEAPON_SLOTS          17
//! the first weapon slot used for inventory
#define FIRST_INVENTORY_SLOT  5
//! maximal number of magazine slots
#define MAGAZINE_SLOTS        20
//! the first magazine slot used for primary weapon
#define FIRST_PRIMARY_SLOT    0
//! the first magazine slot obviously used for secondary weapon
#define FIRST_SECONDARY_SLOT  6
//! the first magazine slot used for hand gun
#define FIRST_HANDGUN_SLOT    12


//! States of mission objective
enum ObjectiveStatus
{
  OSActive,
  OSDone,
  OSFailed,
  OSHidden
};

//! Base (abstract) map display
class DisplayMap : public Display
{
#if _VBS3
  // make aar a friend class here to get briefing page
  friend class AAR;
#endif
protected:
  //! briefing control
  InitPtr<IControl> _briefingCtrl;
  //! briefing content
  InitPtr<CHTMLContainer> _briefing;
  //! mission name control
  InitPtr<ITextContainer> _name;
  //@{
  //! bookmark control
  InitPtr<ITextContainer> _bookmark1;
  InitPtr<ITextContainer> _bookmark2;
  InitPtr<ITextContainer> _bookmark3;
  InitPtr<ITextContainer> _bookmark4;
  //@}
  //@{
  //! radio message control
  InitPtr<ITextContainer> _radioAlpha;
  InitPtr<ITextContainer> _radioBravo;
  InitPtr<ITextContainer> _radioCharlie;
  InitPtr<ITextContainer> _radioDelta;
  InitPtr<ITextContainer> _radioEcho;
  InitPtr<ITextContainer> _radioFoxtrot;
  InitPtr<ITextContainer> _radioGolf;
  InitPtr<ITextContainer> _radioHotel;
  InitPtr<ITextContainer> _radioIndia;
  InitPtr<ITextContainer> _radioJuliet;
  //@}
  //! gps text control
  InitPtr<ITextContainer> _gpsCtrl;
  InitPtr<ITextContainer> _gpsAltCtrl;
  InitPtr<ITextContainer> _gpsHeadingCtrl;
  //! map control
  InitPtr<CStaticMapMain> _map;

  //! walkie talkie (radio) object
  Ref<ControlObjectContainer> _walkieTalkie;
  //! briefing notepad object
  Ref<Notepad> _notepad;
  //! warrant object (not used currently)
  Ref<ControlObjectContainer> _warrant;
  //! GPS object
  Ref<ControlObjectContainer> _gps;
  
  /// tell us if GPS is enabled by the mission or / and scripting
  bool _showGPS;
  /// tell us if Map is enabled by the mission or / and scripting
  bool _showMap;

  //! allow select weapons
  bool _selectWeapons;
  bool _canTakeWeapon;

  //! load / save objects position into user profile
  bool _saveParams;
  //! xbox style groups / gear
  bool _xboxStyle;

  bool _updatePlanAsked;
  bool _showGearOnExit;

  bool _briefingEnabled;

  //! currently displayed mouse cursor
  CursorType _cursor;

  //@{
  //! notepad textures
  Ref<Texture> _picturePlan;
  Ref<Texture> _pictureNotes;
  Ref<Texture> _pictureGroup;
  Ref<Texture> _picture;
  //@}

#if _VBS2
  Vector3 _compassDir;
#endif

public:
  //! constructor
#if _VBS2
  DisplayMap(ControlsContainer *parent);
#endif
  /*!
  \param parent parent display
  \param resource name of dialog resource template
  */
  DisplayMap(ControlsContainer *parent, RString resource);
  //! destructor
  ~DisplayMap();

  //! returns map control
  CStaticMapMain *GetMap() {return _map;}


#if _ENABLE_CHEATS
  Object *GetObserver() const {return _map ? _map->GetObserver() : NULL;}
#endif

  /// tell us if Map is enabled by the mission or / and scripting
  bool IsShownMap() const {return _showMap;}
  /// tell us if GPS is enabled by the mission or / and scripting
  bool IsShownGPS() const {return _showGPS;}

  //! returns if walkie talkie is shown
  bool IsShownWalkieTalkie() const;
  //! returns if briefing notepad is shown
  bool IsShownNotepad() const;
  //! returns if warrant is shown
  bool IsShownWarrant() const;

  //! enables / disables displaying of map
  void ShowMap(bool show = true) {_showMap = show;}
  //! enables / disables displaying of GPS
  void ShowGPS(bool show = true) {_showGPS = show;}
  
  //! enables / disables displaying of walkie talkie
  void ShowWalkieTalkie(bool show = true);
  //! enables / disables displaying of briefing notapad
  void ShowNotepad(bool show = true);
  //! enables / disables displaying of warrant
  void ShowWarrant(bool show = true);

  bool IsBriefingUsed() const;
  //! returns if notepad with briefing is shown
  bool IsShownBriefing() const;
  //! show / hide notepad with briefing
  void ShowBriefing(bool show);

  //! ingame (main) map
  virtual bool IsInGame() const {return false;}

  /// return the target cursor is pointing at
  virtual EntityAI *GetTarget() const {return _map ? _map->GetTarget() : NULL;}
  /// retrieve the position cursor is pointing at (returns true when succeeded)
  virtual bool GetPosition(Vector3 &pos) const {return _map ? _map->GetPosition(pos) : false;}

  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  ControlObject *OnCreateObject(int type, int idc, ParamEntryPar cls);
 
  static int NextMarkerID;

  void OnChildDestroyed(int idd, int exit);

  bool OnKeyDown(int dikCode);
  bool OnKeyUp(int dikCode);
  void OnDraw(EntityAI *vehicle, float alpha);
  void OnSimulate(EntityAI *vehicle);

  void OnHide();

  void OnButtonClicked(int idc);
  bool OnHTMLLink(int idc, RString link);

  RString GetKeyHint(int button, bool &structured) const;

  void ResetHUD();

  //! recalculates rectangle of map not overlapped by notepad
  void AdjustMapVisibleRect();

  //! updates mission plan section of briefing
  void UpdatePlan();

  //! updates mission plan section of briefing
  void AskForUpdatePlan();

  //! switch section in briefing
  virtual void SwitchBriefingSection(RString section);

  //! loads weapons info from file
  //void LoadWeapons(RString filename) {_weaponsInfo.Load(filename, _pool);}
  //! saves weapons info into file
  //void SaveWeapons(RString filename) {_weaponsInfo.Save(filename, _pool);}

  //return gear display
  Display *GetGearDisplay();

  void SetScales(float scaleMin, float scaleMax, float scaleDefault)
  {
    if (_map) _map->SetScales(scaleMin, scaleMax, scaleDefault);
  }

  /// sets active (focused) marker
  void SetActiveMarker(int marker) {if (_map) _map->SetActiveMarker(marker);}
  /// start map animation to the desired position
  void AnimateTo(Vector3Par position);

  //LSError Serialize(ParamArchive &ar);

  //! initialization of display
#if !_VBS3 // support for dynamic briefing
  void Init();
#else
  void Init(const RString& alternateBriefing = RString());
#endif

  //! external initialization of display
  void InitHUD();

  virtual LSError Serialize(ParamArchive &ar);

protected:
  //! loads parameters (f.e. placement of items) from user profile
  void LoadParams();
  //! saves parameters (f.e. placement of items) into user profile
  void SaveParams();
  //! serialization of parameters (f.e. placement of items)
  LSError SerializeParams(ParamArchive &ar);

  //! updates texts on walkie talkie by real situation
  void SetRadioText();

  /// update text in the button show / hide textures
  void UpdateTexturesButton();

  /// Update texts in buttons based on the current content
  void UpdateButtons();
};

/// from which context diary was executed
enum DiaryContext
{
  DCInGame,
  DCBriefing
};

struct TextureByName
{
  RString _name;
  Ref<Texture> _texture;

  TextureByName() {}
  TextureByName(RString name, Ref<Texture> texture) : _name(name), _texture(texture) {}
  const char *GetKey() const {return _name;}
};
TypeIsMovableZeroed(TextureByName);

typedef MapStringToClass< TextureByName, AutoArray<TextureByName> > TexturesByName;

//special listbox for map diary where mouse wheel does not control scrollBar bar selection
class CMapListBox: public CListBox
{
private:
  /// map display which is our parent (to fast access without casting)
  LLink<DisplayMap> _map;
public:
  CMapListBox(DisplayMap *map, int idc, ParamEntryPar cls);
  CMapListBox(Display *parent, int idc, ParamEntryPar cls) : CListBox(parent, idc, cls){};
  virtual void OnMouseZChanged(float dz);
};
//special group for map diary where mouse wheel does not control childs but scrollbar
class CMapCGroup: public CControlsGroup
{
private:
  /// map display which is our parent (to fast access without casting)
  LLink<DisplayMap> _map;
public:
  CMapCGroup(DisplayMap *map, int idc, ParamEntryPar cls);
  CMapCGroup(Display *parent, int idc, ParamEntryPar cls) : CControlsGroup(parent, idc, cls){};
  virtual void OnMouseZChanged(float dz);
};

class DisplayDiaryInMap
{
protected:
  /// list of diary topics
  AutoArray<RString> _topics;
  //! diary topics control
  Ref<CListBox> _diaryTopics;
  //! diary index control
  Ref<CListBox> _diaryIndex;
  //! diary content control
  Ref<CHTML> _diary;
  //! HMTL container
  Ref<CControlsGroup> _controlsGroup;
  /// set of textures used as image icons
  TexturesByName _indexTextures;

  InitPtr<ITextContainer> _playerName;
  InitPtr<ITextContainer> _currentTask;
  InitPtr<ITextContainer> _currentMission;

  //max diary controls height
  float _maxDiaryTopicsH;
  float _maxDiaryIndexH;
  float _maxDiaryH;
  float _maxControlsGroupH;
  //@{
  //! diary buttons
  InitPtr<ITextContainer> _diaryAdd;
  //@}

  /// diary owner
  OLinkPermO(Person) _diaryPerson;

  /// for backward compatibility, handle content of briefing.html
  HTMLContent _briefing;

  /// Info about the record we want to delete (after user confirmation)
  RString _toDeleteSubject;
  /// Info about the record we want to delete (after user confirmation)
  int _toDeleteRecord;

  /// from which context diary was executed
  DiaryContext _context;
  /// map display which is our parent (to fast access without casting)
  LLink<DisplayMap> _map;

public:
  DisplayDiaryInMap(){};
  DisplayDiaryInMap(DisplayMap *parent);

  void OnButtonClicked(int idc);
  void OnLBSelChanged(IControl *ctrl, int curSel);
  void OnLBDblClick(int idc, int curSel);
  bool OnHTMLLink(int idc, RString link, DiaryContext context);
  void OnChildDestroyed(int idd, int exit,Ref<ControlsContainer> child);
  void OnSimulate(EntityAI *vehicle);
  //! update diary if needed
  void UpdateDiary(bool force = false);
  //! synchronize diary with index
  void SyncDiary();
  //! update text in diary buttons
  void UpdateButtons();
  void UpdateTopics();

  /// check if the diary is owned by the current player
  void UpdateOwner();

  //! navigate to the record described by the link
  void ProcessLink(RString link);
  void ProcessLink(RString link,DiaryContext context);
  void SyncTopicList(int index);
  void ClearLink();
  void ResizeHTML();

  void ResetListBox();

protected:
  RString GetCurrentPage() const;
  /// select given diary page
  bool SelectPage(RString name);

  /// write briefing notes section to diary
  void WriteBriefingNotes();
  /// write briefing plan section to diary
  void WriteBriefingPlan();
  /// find references to other sections in srcSection, add these sections to _diary if needed
  void HandleReferences(const HTMLSection &section);
};

//! Main (ingame) map display
class DisplayMainMap : public  DisplayMap
{
protected:
#if _ENABLE_IDENTITIES
  //class for diary processing
  DisplayDiaryInMap _displayDiaryMap;
#endif


public:
  //! constructor
  /*!
  \param parent parent display
  */
  DisplayMainMap(ControlsContainer *parent);
  void ResetHUD();
  void DestroyHUD(int exit);
  virtual void OnButtonClicked(int idc);
  virtual bool OnKeyDown(int dikCode);
  virtual bool OnKeyUp(int dikCode);
  virtual void OnLBSelChanged(IControl *ctrl, int curSel);
  virtual void OnLBDblClick(int idc, int curSel);
  virtual void OnSimulate(EntityAI *vehicle);
  virtual bool OnHTMLLink(int idc, RString link);
  virtual bool IsInGame() const {return true;}
  virtual void OnChildDestroyed(int idd, int exit);
  void ProcessDiaryLink(RString link, DiaryContext contex);
};


//! Briefing display
class DisplayGetReady : public DisplayMap
{
protected:
  bool _soundPlanPlayed;
  bool _soundNotesPlayed;
  bool _soundGroupPlayed;

  RefAbstractWave _sound;

  //class for diary processing
#if _ENABLE_IDENTITIES
  DisplayDiaryInMap _displayDiaryMap;
  //max diary topic listBox height
  float _diaryTopicsHeight;
#endif

public:
  //! constructor
  /*!
  \param parent parent display
  */
  DisplayGetReady(ControlsContainer *parent);
  //! constructor
  /*!
  \param parent parent display
  \param resource name of dialog resource template
  */
  DisplayGetReady(ControlsContainer *parent, RString resource);
  ~DisplayGetReady();
  void OnButtonClicked(int idc);
  void OnChildDestroyed(int idd, int exit);
  void Destroy();
  virtual void OnSimulate(EntityAI *vehicle);
  virtual void SwitchBriefingSection(RString section);
  virtual bool IsInGame() const {return false;}

  virtual void OnLBSelChanged(IControl *ctrl, int curSel);
  virtual void OnLBDblClick(int idc, int curSel);
  virtual bool OnHTMLLink(int idc, RString link)
  {
#if _ENABLE_IDENTITIES
    return _displayDiaryMap.OnHTMLLink(idc,link,DCBriefing);
#else
    return false;
#endif
  }

  void ProcessDiaryLink(RString link,DiaryContext contex);

#if _GAMES_FOR_WINDOWS
  /// handle invitation
  virtual void OnInvited();
#endif

protected:
  //! Play 2D sound (as reaction to switch sections in briefing)
  /*!
  \param var variable with sound name
  */
  void PlaySound(RString name);
};

// shortcut to briefing
static DisplayGetReady *GBriefing = NULL;

class LiveStatsUpdate;

inline DisplayMap *CStaticMapMain::GetParent()
{
  return check_cast<DisplayMap *>(_parent.GetLink());
}

#if _ENABLE_IDENTITIES

class DisplayDiary : public Display
{
protected:
  /// list of diary topics
  AutoArray<RString> _topics;
  //! diary topics control
  Ref<CListBox> _diaryTopics;
  //! diary index control
  Ref<CListBox> _diaryIndex;
  //! diary content control
  Ref<CHTML> _diary;
  //! HMTL container
  Ref<CControlsGroup> _controlsGroup;


  InitPtr<ITextContainer> _playerName;
  InitPtr<ITextContainer> _currentTask;
  InitPtr<ITextContainer> _currentMission;

  //max diary controls height
  float _maxDiaryTopicsH;
  float _maxDiaryIndexH;
  float _maxDiaryH;
  float _maxControlsGroupH;

  /// set of textures used as image icons
  TexturesByName _indexTextures;

  //@{
  //! diary buttons
  InitPtr<ITextContainer> _diaryAdd;
  //@}

  /// diary owner
  OLinkPermO(Person) _diaryPerson;


  /// for backward compatibility, handle content of briefing.html
  HTMLContent _briefing;

  /// Info about the record we want to delete (after user confirmation)
  RString _toDeleteSubject;
  /// Info about the record we want to delete (after user confirmation)
  int _toDeleteRecord;

  /// from which context diary was executed
  DiaryContext _context;
  /// map display which is our parent (to fast access without casting)
  LLink<Display> _parent;

public:
  DisplayDiary(Display *parent, RString link, DiaryContext context = DCInGame);
  ~DisplayDiary();

  void ResetListBox();
  void ClearLink();
  void ResizeHTML();
  void SyncTopicList(int index);

  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  bool OnKeyDown(int dikCode);
  bool OnKeyUp(int dikCode);
  void OnLBSelChanged(IControl *ctrl, int curSel);
  bool OnHTMLLink(int idc, RString link);
  void OnChildDestroyed(int idd, int exit);
  void OnDraw(EntityAI *vehicle, float alpha);
  void OnSimulate(EntityAI *vehicle);
  //! update diary if needed
  void UpdateDiary(bool force = false);
  //! synchronize diary with index
  void SyncDiary();
  //! update text in diary buttons
  void UpdateButtons();

  //! navigate to the record described by the link
  void ProcessLink(RString link);
  void ProcessLink(RString link,DiaryContext context);

protected:
  RString GetCurrentPage() const;
  /// select given diary page
  bool SelectPage(RString name);

  /// write briefing notes section to diary
  void WriteBriefingNotes();
  /// write briefing plan section to diary
  void WriteBriefingPlan();
  /// find references to other sections in srcSection, add these sections to _diary if needed
  void HandleReferences(const HTMLSection &section);
};

#endif

// Mission, intro, outro, campaign intro displays

//! "Fake" display used when mission is running
/*!
No display. Game is running "in background".
Waits for special user input (ESCAPE) or game over.
*/
class DisplayMission : public Display
{
protected:
  //! if mission is called from missin editor (preview)
  bool _editor;

  UserFileErrorInfo _error;

  enum SubdialogType
  {
    STNone,
    STMissionEnd,
    STInterrupt,
    STMissionFail,
  };

  //! child dialog for preload
  SubdialogType _wantedDialog;
  Ref<ControlsContainer> _wantedChild;
  RString _wantedResource;
  Future<QFileTime> _saveTimeHandles[NSaveGameType];

  Ref<ControlObject> _itemShortcuts[Person::NItemShortcuts];

public:
  //! constructor
  /*!
  \param parent parent display
  \param editor if mission is called from missin editor (preview)
  \param erase erase save and autosave file
  \param load mission is created by loading of fps file
  */
  DisplayMission(ControlsContainer *parent, bool editor = false, bool erase = true, bool initMap = false);
  //! destructor
  ~DisplayMission();

  ControlObject *OnCreateObject(int type, int idc, ParamEntryPar cls);
  void OnSimulate(EntityAI *vehicle);
  void OnChildDestroyed(int idd, int exit);
  void OnButtonClicked(int idc) {}
  bool OnKeyDown(int dikCode) {return false;}
  bool OnKeyUp(int dikCode) {return false;}

  //! create hint dialog (HintC function)
  /*!
  \param hint displayed string
  */
  void ShowHint(RString hint);
  //! create extended hint dialog (HintC function)
  /*!
  \param title hint title
  \param hints hint texts
  */
  void ShowHint(RString title, StaticArrayAuto< RefR<INode> > &hints);

  //! restart mission from beginning
  void DoRestartMission();

#if _GAMES_FOR_WINDOWS
  /// handle invitation
  virtual void OnInvited();
#endif

  //! retry mission from autosave or beginning
  bool RetryMission();

protected:
  //! sets InGameUI parameters
  void InitUI();
  //! sets DisplayMap parameters
  void InitMap();
  //! restart mission from beginning
  bool RestartMission();
  //! aborts mission
  void AbortMission();

#if _GAMES_FOR_WINDOWS
  /// handles request
  virtual void HandleRequest(RequestType request);
#endif


};

//! "Fake" display used when cutscene is performed
/*!
No display. Game is running "in background". Waits for user input.
*/
class DisplayCutscene : public Display
{
protected:
  bool _credits;

public:
  //! constructor
  /*!
  \param parent parent display
  */
  DisplayCutscene(ControlsContainer *parent);
  //! destructor
  ~DisplayCutscene();

  void OnSimulate(EntityAI *vehicle);
  void OnButtonClicked(int idc) {}
  bool OnKeyDown(int dikCode) {return false;}
  bool OnKeyUp(int dikCode) {return false;}

  void OnChildDestroyed(int idd, int exit);

  /// no default implementation
  virtual void PlayAgain() = 0;

  //@{ PlayAgain helpers for derived classes
  bool PlayAgainInit();
  void PlayAgainEnd();
  //@}

};

//! "Fake" display used when intro cutscene is performed
/*!
No display. Game is running "in background". Waits for user input.
*/
class DisplayIntro : public DisplayCutscene
{
public:
  enum NoInit {noInit};
  //! constructor
  /*!
  No cutscene is started.
  \param parent parent display
  */
  DisplayIntro(ControlsContainer *parent, NoInit, bool credits);
  //! constructor
  /*!
  Current cutscene is started.
  \param parent parent display
  */
  DisplayIntro(ControlsContainer *parent);
  //! constructor
  /*!
  Given cutscene is started.
  \param parent parent display
  \param cutscene name of mission which intro is started
  */
  DisplayIntro(ControlsContainer *parent, RString cutscene);
  ~DisplayIntro();
  //! Initialize game UI (create map etc.)
  void Init();

  void PlayAgain();
};

bool LaunchIntro(Display *parent, bool credits = false);
bool LaunchIntro(Display *parent, RString cutscene, bool credits = false);

RString GetSaveDirectory();
RString GetBriefingFile();


#endif

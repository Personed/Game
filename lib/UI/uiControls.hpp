// Interface of control hierarchy, displays and dialogs
#ifdef _MSC_VER
#pragma once
#endif

#ifndef _UI_CONTROLS_HPP
#define _UI_CONTROLS_HPP

//#include "wpch.hpp"
#include "optionsUI.hpp"
#include <El/Color/colors.hpp>
#include "../paramFileExt.hpp"
#include <Es/Containers/boolArray.hpp>

#include <El/Time/time.hpp>

#include "../font.hpp"
#include "../global.hpp"

#include "../object.hpp"
#include "../interpol.hpp"
#include "../rtAnimation.hpp"

#include "../keyInput.hpp"

#include <Es/Types/lLinks.hpp>
#include "../progress.hpp"

/*!
\file
Interface file for Controls and Control Containers hierarchy.
*/

template <class To, class From>
To check_cast(From from)
{
  Assert(!from || dynamic_cast<To>(from));
  return static_cast<To>(from);
}

#define TEXT_BORDER 0.008

//! Round screen position to pixel (+0.5 was present here because XBOX1 mapping was working differently)
#define CX(x) (toInt((x) * w) /*+ 0.5*/)
//! Round screen position to pixel
#define CY(y) (toInt((y) * h) /*+ 0.5*/)
#define CW(x) (toInt((x) * w))
#define CH(y) (toInt((y) * h))
//!scale given value, _scaleControl should define scaling value for whole control (text, scrollbar, size, events,..)
#define SCALED(value) (_scaleControl * (value))

class GameDataNamespace;
/// namespace used in displays (event handlers, actions attached to buttons etc.)
extern GameDataNamespace GUINamespace;

/// reference to UI namespace (can be redirected to mission namespace in some implementations)
inline GameDataNamespace *UINamespace();

///////////////////////////////////////////////////////////////////////////////
// class Control

class UIViewport;

class ControlsContainer;
class ControlObject;
struct ControlInObject;
struct MsgBoxButton;

struct KeyHint
{
  int key;
  RString hint;
};
TypeIsMovableZeroed(KeyHint)

#define CONTROL_EVENT_ENUM(type,prefix,XX) \
XX(type, prefix, MouseEnter) /* mouse pointer enter control area */ \
XX(type, prefix, MouseExit) /* mouse pointer exit control area */ \
XX(type, prefix, MouseButtonDown) \
XX(type, prefix, MouseButtonUp) \
XX(type, prefix, MouseButtonClick) \
XX(type, prefix, MouseButtonDblClick) \
XX(type, prefix, MouseMoving) \
XX(type, prefix, MouseHolding) \
XX(type, prefix, MouseZChanged) \
\
XX(type, prefix, KeyDown) \
XX(type, prefix, KeyUp) \
XX(type, prefix, Char) \
XX(type, prefix, IMEChar) \
XX(type, prefix, IMEComposition) \
\
XX(type, prefix, SetFocus) \
XX(type, prefix, KillFocus) \
XX(type, prefix, Timer) \
XX(type, prefix, CanDestroy) \
XX(type, prefix, Destroy) \
\
XX(type, prefix, ButtonDown) /* button is pressed */ \
XX(type, prefix, ButtonUp) /* button is released outside control area */ \
XX(type, prefix, ButtonClick) /* button is activated */ \
XX(type, prefix, ButtonDblClick) /* button is activated */ \
\
XX(type, prefix, LBSelChanged) \
XX(type, prefix, LBListSelChanged) \
XX(type, prefix, LBDblClick) \
XX(type, prefix, LBDrag) \
XX(type, prefix, LBDragging) \
XX(type, prefix, LBDrop) \
\
XX(type, prefix, TreeSelChanged) \
XX(type, prefix, TreeDblClick) \
XX(type, prefix, TreeLButtonDown) \
XX(type, prefix, TreeMouseMove) \
XX(type, prefix, TreeMouseHold) \
XX(type, prefix, TreeMouseExit) \
XX(type, prefix, TreeExpanded) \
XX(type, prefix, TreeCollapsed) \
\
XX(type, prefix, ToolBoxSelChanged) \
XX(type, prefix, CheckBoxesSelChanged) \
XX(type, prefix, HTMLLink) \
XX(type, prefix, SliderPosChanged) \
XX(type, prefix, ObjectMoved) \
XX(type, prefix, MenuSelected) \
\
XX(type, prefix, Draw) \
\
XX(type, prefix, VideoStopped)

#ifndef DECL_ENUM_CONTROL_EVENT
#define DECL_ENUM_CONTROL_EVENT
DECL_ENUM(ControlEvent)
#endif
DECLARE_ENUM(ControlEvent, CE, CONTROL_EVENT_ENUM)

/// encapsulation of a single event handler (for control or display)
class EventHandlerUI
{
protected:
  /// the unique identification
  int _id;
  /// the expression (TODO: precompiled code support)
  RString _handler;

public: 
  EventHandlerUI(int id, RString handler)
    : _id(id), _handler(handler) {}
  int GetId() const {return _id;}
  /// execute the event handler, return true if handled
  bool Process(const GameValue &pars);
  /// execute the event handler, return true if handled, the event handler code is required to return true
  bool ProcessBool(const GameValue &pars);
};
TypeIsMovableZeroed(EventHandlerUI);

/// set of event handlers attached to a single event type
class EventHandlersUI
{
protected:
  /// the first free id
  int _nextId;
  /// list of event handlers
  AutoArray<EventHandlerUI> _handlers;

public:
  EventHandlersUI() {_nextId = 0;}
  /// Check if some handler is registered
  bool IsEmpty() const {return _handlers.Size() == 0;}
  /// Add a new handler, return its id
  int Add(RString expression);
  /// Remove the handler
  void Delete(int id);
  /// Remove all handlers
  void Clear();
  /// execute the event handlers
  bool Process(const GameValue &pars);
  /// execute the event handlers, return true if event was handled
  bool ProcessBool(const GameValue &pars);
  /// execute the event handlers, return false if any event returns false
  bool ProcessFalse(const GameValue &pars);
};

struct CListBoxItems;

enum ControlFocusAction
{
  CFAClick,
  CFAPrev,
  CFANext
};

//! Base abstract class for Controls hierarchy
/*!
Provides basic interface for all types of controls:
  - plain 2D controls
  - objects used as controls (or control containers)
  - 3D controls (controls mapped into selections of object control containers)
*/
class IControl
{
protected:
  //! control container by which this control is owned
  LLink<ControlsContainer> _parent;
  //! id of control
  int _idc;
  //! class name of control (when created from script class)
  RString _className;

  //! control is visible
  bool _visible;
  //! control is enabled (process user input)
  bool _enabled;
  //! control has input focus
  bool _focused;
  //! control is default for _parent container
  bool _default;
  //! mouse is over control
  bool _mouseOver;
  /// when dragging the control, the display will move
  bool _movingDisplay;
  //shadow style
  int _shadow;

  //! time when OnTimer action will be performed
  UITime _timer;

  //! speed of control blinking
  float _blinkingSpeed;

  //! tooltip text
  RString _tooltip;

  //! tooltip text color
  PackedColor _tooltipColorText;
  //! tooltip box color
  PackedColor _tooltipColorBox;
  //! tooltip background color
  PackedColor _tooltipColorShade;

  //! tooltip font
  static Ref<Font> _tooltipFont;
  //! tooltip font size
  static float _tooltipSize;
  //! tooltip font shadow
  static int _tooltipShadow;

  //! joystick button hints
  AutoArray<KeyHint> _keyHints;

  //! event handlers
  EventHandlersUI _eventHandlers[NControlEvent];

  /// list of shortcuts to this control
  FindArray<int> _shortcuts;

public:
  //! constructor
  /*!
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  IControl(ControlsContainer *parent, int idc, ParamEntryPar cls);
  //! constructor
  /*!
    \param parent control container by which this control is owned
    \param idc id of control
  */
  IControl(ControlsContainer *parent, int idc);
  //! destructor
  virtual ~IControl() {}

  // reference counting
  virtual int CtrlAddRef() = 0;
  virtual int CtrlRelease() = 0;

  // create GameControl scripting value
  virtual GameValue CreateGameValue() = 0;

  //! returns id of control
  int IDC() const {return _idc;}
  //! returns type of control
  virtual int GetType() = 0;
  //! returns class name of control (when created from resource)
  const RString GetCtrlClassName() const { return _className; }
  //! returns style of controls
  /*!
    Style is combination of flags used for display and behaviour of control.
  */
  virtual int GetStyle() = 0;
  //! return parent display
  ControlsContainer *GetParent() {return _parent;}

  //! returns if control is visible
  bool IsVisible() const {return _visible;}
  //! sets visible flag of control
  /*!
    \param show new value of visible flag
  */
  void ShowCtrl(bool show = true);
  //! returns if control is enabled (process user input)
  virtual bool IsEnabled() const {return _enabled;}
  /// mouse manipulation is enabled
  virtual bool IsMouseEnabled() const {return _enabled;}
  /// when dragging the control, the display will move
  bool IsMovingDisplay() const {return _movingDisplay;}
  //! sets enabled flag of control
  /*!
    \param enable new value of enabled flag
  */
  void EnableCtrl(bool enable = true);
  //! returns if control has input focus
  virtual bool IsFocused() const {return _focused;}
  //! returns subcontrol with input focus
  /*!
    Used in controls used as containers of other controls.
    In this case return subcontrol with input focus. In the other case returns itself.
  */
  virtual const IControl *GetFocused() const {return this;}
  //! returns subcontrol with input focus
  /*!
    Used in controls used as containers of other controls.
    In this case return subcontrol with input focus. In the other case returns itself.
  */
  virtual IControl *GetFocused() {return this;}
  //! event handler - performed if focus is gained
  /*!
    Called by framework if control (or some subcontrol) gains input focus.
    \param up true if focus is gaines as result of Next Control action (TAB)
    \param def true if only button like controls (defaultable) can gain focus
    \return false if focus cannot be gained
  */
  virtual bool OnSetFocus(ControlFocusAction action, bool def = false);  // return false if focus cannot not be gained
  //! event handler - performed if focus is lost
  /*!
    Called by framework if control (or some subcontrol) loose input focus.
    \return false if focus cannot be lost
  */
  virtual bool OnKillFocus(); // return false if focus cannot be killed
  //! sets focus to given subcontrol
  virtual bool FocusCtrl(int idc) {return false;}
  //! sets focus to given subcontrol
  virtual bool FocusCtrl(IControl *ctrl) {return false;}
  //! returns true if control can be default (true for button like controls)
  virtual bool CanBeDefault() const {return false;}
  //! returns default control in subhierarchy
  virtual const IControl *GetDefault() const;
  //! returns default control in subhierarchy
  virtual IControl *GetDefault();
  //! returns if control is active (mouse cursor is over control)
  bool IsMouseOver() const {return _mouseOver;}
  //! transparence of control (used for blinking)
  virtual float GetAlpha() const;
  //! Return tooltip text
  RString GetTooltip() const {return _tooltip;}
  //! Set tooltip text
  void SetTooltip(RString text) {_tooltip = text;}

  //! Set tooltip text color
  void SetTooltipColorText(PackedColor color) {_tooltipColorText = color;}
  //! Set tooltip box color
  void SetTooltipColorBox(PackedColor color) {_tooltipColorBox = color;}
  //! Set tooltip background color
  void SetTooltipColorShade(PackedColor color) {_tooltipColorShade = color;}

  //! returns time when timer will expire
  UITime GetTimer() const {return _timer;}
  //! set time when timer will expire
  /*!
    \param uiTime new value of time when OnTimer will be performed
  */
  void SetTimer(UITime uiTime) {_timer = uiTime;}
  //! event handler - performed if timer expired
  virtual void OnTimer();

  //! set font
  virtual void SetFont(Font *font) {}
  //! set font size
  virtual void SetFontHeight(float h) {}
#if _VBS3 // getFontHeight
  virtual float GetFontHeight() {return 0.0f;}
#endif
#if _VBS3 // set and getRowHeight
  virtual void SetRowHeight(float height) {}
  virtual float GetRowHeight() {return 0.0f;}
  //! returns index under the current position for Listboxes
  virtual int GetPosIndex(float x, float y){ return -1;}
#endif
  //! set background color
  virtual void SetBgColor(PackedColor color) {}
  //! set foreground color
  virtual void SetFgColor(PackedColor color) {}
  //! set text color
  virtual void SetTextColor(PackedColor color) {}
  //! set selected text color
  virtual void SetActiveColor(PackedColor color) {}

  //! returns if point (<x>, <y>) is inside control's area
  //! x, y are Point2DFloat (2d viewport) coordinates
  virtual bool IsInside(float x, float y) const = 0;
  //! returns if point (<x>, <y>) is inside extended control area
  virtual bool IsInsideExt(float x, float y) const {return false;}
  //! moves control by (<dx>, <dy>)
  //! dx, dy are relative Point2DFloat (2d viewport) coordinates
  virtual void Move(float dx, float dy) = 0;

  //! returns subcontrol with given id
  /*!
    Used in controls used as containers of other controls.
    In this case return subcontrol with given id or itself if id matched.
    Otherwise returns NULL.
    \param idc id of wanted control
  */
  virtual IControl *GetCtrl(int idc);
  //! returns subcontrol with given coordinates
  /*!
    Used in controls used as containers of other controls.
    In this case return subcontrol on position (<x>, <y>).
    Otherwise returns itself.
  */
  virtual IControl *GetCtrl(float x, float y);

  //! event handler - performed if key is pressed
  virtual bool OnKeyDown(int dikCode);
  //! event handler - performed if key is depressed
  virtual bool OnKeyUp(int dikCode);
  //! event handler - performed if char is entered (see Windows SDK)
  virtual bool OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
  //! event handler - performed if IME char is entered (see Windows SDK)
  virtual bool OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
  //! event handler - performed if IME composition is entered (see Windows SDK)
  virtual bool OnIMEComposition(unsigned nChar, unsigned nFlags);
  //! event handler - performed if left mouse button is pressed
  /*!
    Mouse position is (<x>, <y>).
  */
  virtual void OnLButtonDown(float x, float y);
  //! event handler - performed if left mouse button is depressed
  /*!
    Mouse position is (<x>, <y>).
  */
  virtual void OnLButtonUp(float x, float y);
  //! event handler - performed if left mouse button is clicked
  /*!
    Mouse position is (<x>, <y>).
  */
  virtual void OnLButtonClick(float x, float y);
  //! event handler - performed if left mouse button is double clicked
  /*!
    Mouse position is (<x>, <y>).
  */
  virtual void OnLButtonDblClick(float x, float y);
  //! event handler - performed if right mouse button is pressed
  /*!
    Mouse position is (<x>, <y>).
  */
  virtual void OnRButtonDown(float x, float y);
  //! event handler - performed if right mouse button is depressed
  /*!
    Mouse position is (<x>, <y>).
  */
  virtual void OnRButtonUp(float x, float y);
  //! event handler - performed if right mouse button is clicked
  /*!
  Mouse position is (<x>, <y>).
  */
  virtual void OnRButtonClick(float x, float y);
  //! event handler - performed if mouse cursor is moved
  /*!
    Mouse position is (<x>, <y>).
    \param active true if mouse is in control's area
  */
  virtual void OnMouseMove(float x, float y, bool active = true);
  //! event handler - performed if mouse cursor is not moved
  /*!
    Mouse position is (<x>, <y>).
    \param active true if mouse is in control's area
  */
  virtual void OnMouseHold(float x, float y, bool active = true);
  //! event handler - performed if mouse wheel is moved
  /*!
    \param dz amount how much wheel was moved
  */
  virtual void OnMouseZChanged(float dz);
  //! event handler - performed if mouse cursor enter control area
  /*!
    Mouse position is (<x>, <y>).
  */
  virtual void OnMouseEnter(float x, float y);
  //! event handler - performed if mouse cursor exit control area
  /*!
    Mouse position is (<x>, <y>).
  */
  virtual void OnMouseExit(float x, float y);

  /// check if control is ready for drawing
  virtual bool IsReady() const {return true;}

  //! event handler - performed when tooltip is to draw
  /*!
    Mouse position is (<x>, <y>).
  */
  virtual void DrawTooltip(UIViewport *vp, float x, float y);
  /// Check the tooltip area to decide if to hide tooltip
  virtual int GetTooltipArea(float x, float y) {return 1;} // anything different than 0 is good

  //! return hint to joystick button function
  virtual RString GetKeyHint(int button, bool &structured) const;

  //! event handler - performed to ask control if control container can be closed
  /*!
    \param code exit code of controls container
    \return true if control container can be closed
  */
  virtual bool OnCanDestroy(int code);
  //! event handler - performed when control container is destroying
  /*!
    \param code exit code of controls container
  */
  virtual void OnDestroy(int code);

  //! plays 2D sound (usually as reaction to some user action)
  static void PlaySound(SoundPars &pars);

  // set event handler
  void SetEventHandler(RString name, RString value);

  // Add a new event handler
  int AddEventHandler(RString name, RString value);

  // Add a new event handler
  void RemoveEventHandler(RString name, int id);

  // Add a new event handler
  void RemoveAllEventHandlers(RString name);

  //! process event handler - no additional arguments
  void OnEvent(ControlEvent event);

  //! process event handler
  void OnEvent(ControlEvent event, float par);

  //! process event handler
  void OnEvent(ControlEvent event, Vector3Par par);

  //! process event handler
  void OnEvent(ControlEvent event, RString par);

  //! process event handler
  void OnEvent(ControlEvent event, float par1, bool par2, bool par3, bool par4);

  //! process event handler
  void OnEvent(ControlEvent event, float par1, float par2);

  //! process event handler
  void OnEvent(ControlEvent event, float par1, float par2, bool par3);

  //! process event handler
  void OnEvent(ControlEvent event, float par1, float par2, float par3, bool par4, bool par5, bool par6);

  //! process event handler
  void OnEvent(ControlEvent event, const CListBoxItems &items);

  //! process event handler
  bool OnCheckEvent(ControlEvent event);

  //! process event handlers, return false if some event handler returns false (reversed behaviour)
  bool OnCheckEventFalse(ControlEvent event, float par);

  //! process event handler
  bool OnCheckEvent(ControlEvent event, float par1, bool par2, bool par3, bool par4);

  //! process event handler
  bool OnCheckEvent(ControlEvent event, float x, float y, int sourceIDC, const CListBoxItems &items
#if _VBS3 //add the Index the items are dropped onto
    , int destIndex = -1
#endif
    );

  /// Set a single shortcut to the button, value 0 for no shortcuts
  void SetShortcut(int shortcut);

  /// Set multiple shortcuts to the button
  void SetShortcuts(const int *shortcuts, int count);

  // Update the control layout when shortcuts changed
  virtual void UpdateShortcuts() {}

  /// return the control in my hierarchy the key is assigned to as a shortcut
  virtual IControl *GetShortcutControl(int dikCode);

  /// returns the list of devices shortcuts are available to
  bool IsShortcutDevice(int device) const;
  /// true when all shortcuts are Keyboard with some Shift/Control/Alt combination
  bool OnlyComboShortcuts() const;

  /// reaction when shortcut button was pressed
  virtual bool OnShortcutDown();

  /// reaction when shortcut button was released
  virtual bool OnShortcutUp();

  ///Get associated decoder for video texture to control playing 
  virtual OggDecoder* getDecoder() { return NULL; };

  //if tested drop position is based on object, not mouse position
  virtual void OverrideMouseDragPosition(float &x, float &y) {};
};

struct IControlRefTraits
{
  static __forceinline int AddRef(IControl *ptr) {return ptr->CtrlAddRef();}
  static __forceinline int Release(IControl *ptr) {return ptr->CtrlRelease();}
};

typedef RefR<IControl, IControl, IControlRefTraits> IControlRef;

struct ControlPos
{
  float _x;
  float _y;
  float _w;
  float _h;

  ControlPos() {_x = 0; _y = 0; _w = 0; _h = 0;}
  ControlPos(float x, float y, float w, float h) {_x = x; _y = y; _w = w; _h = h;}
};

inline ControlPos operator *(float coef, const ControlPos &value)
{
  return ControlPos(coef * value._x, coef * value._y, coef * value._w, coef * value._h);
}

inline ControlPos operator +(const ControlPos &value1, const ControlPos &value2)
{
  return ControlPos(value1._x + value2._x, value1._y + value2._y, value1._w + value2._w, value1._h + value2._h);
}

template <typename Type>
struct ControlAnimation
{
  Type _valueStart;
  Type _valueEnd;
  Type _valueSet;
  bool _set;
  bool _animated;
  UITime _start;
  UITime _end;
  float _speed;

  ControlAnimation()
  {
    _set = false;
    _animated = false;
    _start = UITIME_MAX;
    _end = UITIME_MAX;
  }
  void Set(const Type &value)
  {
    _valueSet = value;
    _set = true;
  }
  void Commit(float time, const Type &value)
  {
    _valueStart = value;
    _valueEnd = _valueSet;
    _start = Glob.uiTime;
    _end = Glob.uiTime + time;
    if (time > 0) _speed = 1.0f / time;
    _set = false;
    _animated = true;
  }
  void Animate(Type &value)
  {
    if (Glob.uiTime >= _end)
    {
      value = _valueEnd;
      _animated = false;
    }
    else
    {
      float factor = (Glob.uiTime - _start) * _speed;
      value = (1 - factor) * _valueStart + factor * _valueEnd;
    }
  }
};

//! Base class for plain 2D controls
class Control : public RemoveLLinks, public IControl
{
protected:
  //! type of control
  int _type;
  //! style of control (see GetStyle)
  int _style;

  //@{
  //! placement of control
  float _x;
  float _y;
  float _w;
  float _h;
  //@}

  //! size of control 
  float _scale;
  //! control scale (change width and height of every control)
  float _scaleControl;
  //! fade
  float _alpha;

  //@{
  //! control animations
  UITime _timeCommitted;
  ControlAnimation<float> _alphaAnim;
  ControlAnimation<float> _scaleControlAnim;  //animated control size scale, top left remains 0,0 
  ControlAnimation<ControlPos> _posAnim;
  //@}

protected:
  //! constructor
  /*!
    Used only when control is created directly in program.
    (For example in message box.)
    Initial placement is <x>, <y>, <w>, <h>
    \param parent control container by which this control is owned
    \param type type of control
    \param idc id of control
    \param style style of control
  */
  Control(ControlsContainer *parent, int type, int idc,
    int style, float x, float y, float w, float h);

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param type type of control
    \param idc id of control
    \param cls resource template
  */
  Control(ControlsContainer *parent, int type, int idc, ParamEntryPar cls);

  // reference counting
  virtual int CtrlAddRef() {return AddRef();}
  virtual int CtrlRelease() {return Release();}

  virtual GameValue CreateGameValue();

  int GetType() {return _type;}
  int GetStyle() {return _style;}
  virtual bool IsInside(float x, float y) const
    { return x >= _x && x <= _x + SCALED(_w) && y >= _y && y <= _y + SCALED(_h);}
  //@{
  //! returns placement of control
  float X() const {return _x;}
  float Y() const {return _y;}
  float W() const {return _w;}
  float H() const {return _h;}
  //@}

  //! returns size of control
  float GetScale() const {return _scale;}
  //! set size of control
  void SetScale(float scale) {_scale = scale;}
  //! get scale of control
  float virtual GetScaleControl() {return _scaleControl;}
  //! set scale of control
  void virtual SetScaleControl(float scaleControl) {_scaleControl = scaleControl;}
  //! animated alpha (no blinking is calculated)
  float GetAlphaAnimated() const {return _alpha;}
  virtual float GetAlpha() const;

  virtual void Move(float dx, float dy);
  //! set new placement of control
  virtual void SetPos(float x, float y, float w, float h)
  {_x = x; _y = y; _w = w; _h = h;}

  //! event handler - performed when control must be drawn
  /*!
  \param alpha transparency
  */
  virtual void OnDraw(UIViewport *vp, float alpha) = 0;

  //@{
  //! control animations
  void AnimAlpha(float alpha) {_alphaAnim.Set(alpha);}
  void AnimScaleControl(float scaleControl) {_scaleControlAnim.Set(scaleControl);}  //animate contrl. size, font size, scrollbar, top left remains same 
  void AnimPos(float x, float y, float w, float h) {_posAnim.Set(ControlPos(x, y, w, h));}
  void AnimCommit(float time);
  bool IsAnimCommited() const {return Glob.uiTime >= _timeCommitted;}
  void Animate();
  //@}
};

struct Point2DFloat;

//! Base class for object used as control (object control)
class ControlObject : public Object, public IControl
{
protected:
  //! position of object if object is in foreground (zoomed)
  Vector3 _position;
  //! model position of point where mouse cursor drags object
  mutable Vector3 _modelPos;
  //! difference between _modelPos and _position
  Vector3 _offset;

  //! control is moving (by user)
  bool _moving;

  //! show object during first Draw (otherwise preload object and show when loaded)
  bool _waitForLoad;

  ///  is the object for UI, or for the 3D scene?
  bool _ui;
  
public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  ControlObject(ControlsContainer *parent, int idc, ParamEntryPar cls, LODShape *shape);
  ControlObject(ControlsContainer *parent, int idc, ParamEntryPar cls);
  //! destructor
  ~ControlObject();

  // reference counting
  virtual int CtrlAddRef() {return AddRef();}
  virtual int CtrlRelease() {return Release();}

  virtual GameValue CreateGameValue();

  int GetType();
  int GetStyle();
  //! returns position of control if in foreground (zoomed)
  Vector3Val GetBasePosition() const {return _position;}

  //! show object during first Draw
  bool WaitForLoad() const {return _waitForLoad;}

  bool IsInside(float x, float y) const;
  //! convert 2D point to 3D point with given z-coordinate
  static Vector3 Convert2DTo3D(const Point2DFloat &point, float z);

  static Point2DFloat Convert3DTo2D(Vector3 position);


  /// Return position in the form of x, y, z given in the config
  Vector3 GetPosition2D() const;

  void Move(float dx, float dy);
  
  void OnLButtonDown(float x, float y);
  void OnLButtonUp(float x, float y);
  void OnMouseMove(float x, float y, bool active = true);
  //! event handler - performed when control must be drawn
  /*!
  \param alpha transparency
  */
  
  //draw 2D background
  virtual void OnDrawBackGround(UIViewport *vp, float alpha) {};
  virtual void OnDrawForeGround(UIViewport *vp, float alpha) {};
  //draw 3D object
  virtual void OnDraw(float alpha);
  //offset is used for CT_ITEMSLOT_OBJECT
  virtual void OnDraw(float alpha, float offsetX, float offsetY) {OnDraw(alpha);};

  virtual int GetObjSpecial(int shapeSpecial) const;

  bool IsReady() const;

  //! called by framework whenever position of control changed
  virtual void UpdatePosition() {}

protected:
  //! read position from resource
  Vector3 ReadPosition(ParamEntryPar cls, RString name3D, RString nameX, RString nameY, RString nameZ);
};

//! Object control with zoom
/*!
  Added zooming functionality to ControlObject class.
  Zoom is implemented as spline interpolation of positions.
*/
class ControlObjectWithZoom : public ControlObject, public SerializeClass
{
protected:
  //! position of object if object is in background (unzoomed)
  Vector3 _positionBack;
  
  //! control is unzoomed
  bool _inBack;
  //! control is zooming / unzooming
  bool _zooming;
  //! zoom is enabled
  bool _enableZoom;

  //! duration of zoom / unzoom action
  float _zoomDuration;

  //@{
  //! zoom implementation
  UITime _zoomStart;
  Matrix4 _zoomMatrices[3];
  float _zoomTimes[3];
  SRef<M4Function> _interpolator;
  //@}

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  ControlObjectWithZoom(ControlsContainer *parent, int idc, ParamEntryPar cls, bool ui);

  int GetType();

  void OnLButtonDblClick(float x, float y);
  void OnMouseMove(float x, float y, bool active = true);

  void UpdatePosition();
  //! serialization of control's state
  LSError Serialize(ParamArchive &ar);

  //! performs zoom / unzoom action
  /*!
    \param zoom true, if zoom action is performed, false if unzoom
  */
  void Zoom(bool zoom);
};

struct ControlsArea
{
  RString selection;
  Vector3 modelPos;
  Vector3 modelRight;
  Vector3 modelDown;
  RefArray<Control> controls;

  void Transform(Object *obj, Vector3Par b, float &xx, float &yy, float *maxY = NULL);
};
TypeIsMovable(ControlsArea);

//! Unique identifier of control in ControlObjectContainer
struct ControlOnObjectId
{
  //! area identifier
  int area;
  //! index of control in area
  int id;

  //! constructor
  ControlOnObjectId() {area = -1; id = -1;}
  //! constructor
  ControlOnObjectId(int a, int i) {area = a; id = i;}
  //! assignment operator
  void operator =(const ControlOnObjectId &src)
  {
    area = src.area;
    id = src.id;
  }
  //! equality operator
  bool operator ==(const ControlOnObjectId &with) const
  {
    return with.area == area && with.id == id;
  }
  //! inequality operator
  bool operator !=(const ControlOnObjectId &with) const
  {
    return with.area != area || with.id != id;
  }

  //! returns nil id (points to no control)
  static ControlOnObjectId Null() {return ControlOnObjectId();}
  //! returns if id is nil (points to no control)
  bool IsNull() const {return area < 0;}
};

//! Object control used as control container
/*!
  This class allows map 2D controls on object control.
*/
class ControlObjectContainer : public ControlObjectWithZoom
{
protected:
  typedef ControlObjectWithZoom base;

  //! array of control areas
  AutoArray<ControlsArea> _areas;
  //! focused subcontrol
  ControlOnObjectId _indexFocused;
  ControlOnObjectId _indexDefault;
  ControlOnObjectId _indexL;
  ControlOnObjectId _indexR;
  ControlOnObjectId _indexMove;

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  ControlObjectContainer(ControlsContainer *parent, int idc, ParamEntryPar cls);

  int GetType();
  IControl *GetCtrl(int idc);
  IControl *GetCtrl(float x, float y);

  virtual bool IsEnabled() const;

  IControl *GetFocused();
  const IControl *GetFocused() const;
  bool OnSetFocus(ControlFocusAction action, bool def = false);
  virtual bool FocusCtrl(int idc);
  virtual bool FocusCtrl(IControl *ctrl);
  bool CanBeDefault() const;
  const IControl *GetDefault() const;
  IControl *GetDefault();
  /// return the control in my hierarchy the key is assigned to as a shortcut
  virtual IControl *GetShortcutControl(int dikCode);

  void OnLButtonDown(float x, float y);
  void OnLButtonUp(float x, float y);
  void OnLButtonClick(float x, float y);
  void OnLButtonDblClick(float x, float y);
  void OnRButtonDown(float x, float y);
  void OnRButtonUp(float x, float y);
  void OnRButtonClick(float x, float y);
  void OnMouseMove(float x, float y, bool active = true);
  void OnMouseHold(float x, float y, bool active = true);
  void OnMouseZChanged(float dz);
  bool OnKeyDown(int dikCode);
  bool OnKeyUp(int dikCode);
  bool OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
  bool OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
  bool OnIMEComposition(unsigned nChar, unsigned nFlags);
  
  void OnDraw(float alpha);
  virtual void DrawTooltip(UIViewport *vp, float x, float y) override;
  virtual int GetTooltipArea(float x, float y) override;

protected:
  //! creates owned controls
  void LoadControls(ParamEntryPar cls);
  //! creates owned control area
  void LoadArea(ParamEntryPar cls);
  //! find subcontrol on given position (<x>, <y>)
  ControlOnObjectId FindControl(float x, float y, float &xx, float &yy);

  //! sets input focus to subcontrol 
  /*!
    Called by framework if some subcontrol gains input focus.
    \param i index of subcontrol
    \param def true if only button like controls (defaultable) can gain focus
  */
  void SetFocus(const ControlOnObjectId &i, bool def = false);
  //! focus next subcontrol
  bool NextCtrl();
  //! focus previous subcontrol
  bool PrevCtrl();
  //! find subcontrol by its id
  Control *GetCtrl(const ControlOnObjectId &i) const;
};

//! Object control with animation
/*!
  Added possibility attach realtime animation to object control.
*/
class ControlObjectContainerAnim : public ControlObjectContainer, public LODShapeLoadHandler
{
protected:
  //! open animation proceed
  bool _open;
  //! close animation proceed
  bool _close;
  //! zoom is performed automaticly after control is created
  bool _autoZoom;
  //@{
  //! animation implementation
  UITime _animStart;
  Ref<AnimationRT> _animation;
  //WeightInfo _weights;
  //Ref<Skeleton> _skeleton;
  //@}

  //! current animation phase
  float _phase;
  //@{
  //! animation speed
  float _animSpeed;
  float _invAnimSpeed;
  //@}

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  ControlObjectContainerAnim(ControlsContainer *parent, int idc, ParamEntryPar cls);
  ~ControlObjectContainerAnim();

  void Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2);
  void Deanimate(int level, bool setEngineStuff);
  AnimationStyle IsAnimated(int level) const {return AnimatedGeometry;}

  //@{ LODShapeLoadHandler implementation
  virtual void ShapeLoaded(LODShape *shape, int level);
  virtual void ShapeUnloaded(LODShape *shape, int level);
  //@}
  
  //! performs open animation
  void Open();
  //! performs close animation
  void Close();

  //! returns actual animation phase
  float GetPhase() const {return _phase;}
  //! returns if control is open
  bool IsOpen() {return !_open && !_close && _phase >= 0.5;}
  //! returns if control is closed
  bool IsClosed() {return !_open && !_close && _phase <= 0;}
};

//! 2D vector
/*!
  Used especially for screen coordinates.
*/

struct Point2DFloat;

#define DrawCoord Point2DFloat
/*
{
  float x;
  float y;
  DrawCoord(float xx, float yy) {x = xx; y = yy;}
  DrawCoord() {} // no init
};
TypeIsSimple(DrawCoord);
*/

///////////////////////////////////////////////////////////////////////////////
// class CLineBreak

//! Dummy control for lines definition
class CLineBreak : public Control
{
public:
  //! constructor
  CLineBreak(ControlsContainer *parent);
  virtual void OnDraw(UIViewport *vp, float alpha) {}
};

///////////////////////////////////////////////////////////////////////////////
// interface ITextContainer

//! Interface for controls containing text
class ITextContainer
{
public:
  //! constructor
  ITextContainer() {}
  //! virtual destructor
  virtual ~ITextContainer() {}

  //! return control interface
  virtual IControl *GetControl() = 0;

  //! returns currently contained text
  virtual RString GetText() const = 0;
  //@{
  //! sets actual text
  virtual bool SetText(RString text) = 0; // returns true if changed
  bool SetText(const char *text); // returns true if changed
  //@}

  /// return the text color
  virtual PackedColor GetColor() const = 0;

  /// sets the text color
  virtual void SetColor(PackedColor color) = 0;

  /// checks if the text fits into control
  virtual bool TextFits(RString text) const {return true;}
};

///////////////////////////////////////////////////////////////////////////////
// class CTextContainer

//! Base class for controls containing text
class CTextContainer : public ITextContainer
{
private:
  //! text
  RString _text;

public:
  //! constructor
  CTextContainer() {}

  //! returns currently contained text
  virtual RString GetText() const {return _text;}
  //! sets actual text
  virtual bool SetText(RString text); // returns true if changed
};

///////////////////////////////////////////////////////////////////////////////
// class CStatic

//! 2D Static control
/*!
  Used especially as static text control. Can be used also for frames or static pictures.
  Subtype of control is defined by style (see GetStyle()).
*/
class CStatic : public Control, public CTextContainer
{
friend class MsgBox;  // Message Box created static directly
protected:
  //! background color
  PackedColor _bgColor;
  //! foreground (text) color
  PackedColor _ftColor;
  //! used font
  Ref<Font> _font;
  //! font size
  float _size;
  //! picture
  Ref<Texture> _texture;

  //text atribut
  bool _fixedWidth;

  //autoplay
  bool _autoplay;

  int _loops;

  // Shadow color
  PackedColor _shadowColor;

  //@{
  //! frame colors and transparency
  PackedColor _color1;
  PackedColor _color2;
  PackedColor _color3;
  PackedColor _color4;
  PackedColor _color5;
  float _alpha;
  //@}

  //@{
  //! properties of multiline text control
  AutoArray<int> _lines;
  float _firstLine;
  float _lineSpacing;
  //@}

  //picture transformation
  float _offsetX, _offsetY;
  Rect2DFloat _clipOffsetPercent;
  Matrix4 _transform;

protected:
  //! constructor
  /*!
    Used only when control is created directly in program.
    (For example in message box.)
    Initial placement is <x>, <y>, <w>, <h>
    \param parent control container by which this control is owned
    \param text initial text
    \param cls aditional resource template
  */
  CStatic
  (
    ControlsContainer *parent, ParamEntryPar cls,
    float x, float y, float w, float h, RString text
  );

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  CStatic(ControlsContainer *parent, int idc, ParamEntryPar cls);

  virtual IControl *GetControl() {return this;}

  virtual bool SetText(RString text); // returns true if changed

  virtual PackedColor GetColor() const {return _ftColor;}
  virtual void SetColor(PackedColor color) {_ftColor = color;}

  virtual bool TextFits(RString text) const;

  virtual bool IsReady() const;
  virtual void OnDraw(UIViewport *vp, float alpha);
  virtual bool OnKeyDown(int dikCode);
  virtual bool OnKeyUp(int dikCode);

  //! single line of multiline text
  /*!
    \param i index of line
  */
  RString GetLine(int i) const;
  //! Return current texture
  Texture *GetTexture() const {return _texture;}
  //! Set texture
  void SetTexture(Texture *texture) {_texture = texture;}
  //! returns background color
  PackedColor GetBgColor() {return _bgColor;}
  //! returns foreground color
  PackedColor GetTextColor() {return _ftColor;}
  virtual void SetFont(Font *font) {_font = font;}
  virtual void SetFontHeight(float h) {_size = h;}
#if _VBS3 // getFontHeight
  virtual float GetFontHeight() {return _size;}
#endif
  virtual void SetBgColor(PackedColor color) {_bgColor = color;}
  virtual void SetTextColor(PackedColor color) {_ftColor = color;}
  //! returns number of lines of multiline text
  int NLines() const;
  //! calculates total height of contained text
  float GetTextHeight() const;
  //! format multiline text
  /*!
    Split multiline text into individual lines.
  */
  void FormatText();
  float GetTextSize() const;

  OggDecoder* getDecoder() { return ( _texture!=NULL ?  _texture->getDecoder() : NULL    ); };

  void SetPictureUVTransfrom(Matrix4 transform) {_transform = transform;};
  void SetPictureOffset(float offsetX, float offsetY) {_offsetX = offsetX; _offsetY = offsetY;};
  void SetClipPictureOffsetPerct(Rect2DFloat clipRect) {_clipOffsetPercent = clipRect;};
protected:
  //! read frame colors from config
  void InitColors();
  //! draw single line of text
  virtual void DrawText(UIViewport *vp, const char *text, float x, float y, float alpha, const Rect2DFloat clipRect);
  //! returns if text is not empty
  virtual bool IsText() const;
};

//! Static control for display of time.
class CStaticTime : public CStatic
{
protected:
  //! displayed time
  Clock _time;
  //! blink with colon
  bool _blink;
public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
    \param blink blink with colon
  */
  CStaticTime(ControlsContainer *parent, int idc, ParamEntryPar cls, bool blink)
    : CStatic(parent, idc, cls) {_blink = blink;}
  //! returns time
  Clock GetTime() const {return _time;}
  //! sets time
  void SetTime(Clock time) {_time = time;}

protected:
  void DrawText(UIViewport *vp, const char *text, float x, float y, float alpha, const Rect2DFloat clipRect);
  bool IsText() const;
};

///////////////////////////////////////////////////////////////////////////////
// class CSkewStatic

//! NOT USED CURRENTLY
class CSkewStatic : public Control
{
protected:
  PackedColor _color;
  PackedColor _lineColors[5];

  float _xTL;
  float _yTL;
  float _alphaTL;
  float _xTR;
  float _yTR;
  float _alphaTR;
  float _xBL;
  float _yBL;
  float _alphaBL;
  float _xBR;
  float _yBR;
  float _alphaBR;
public:
  CSkewStatic(ControlsContainer *parent, int idc, ParamEntryPar cls);
  
  virtual void Move(float dx, float dy);

  virtual void OnDraw(UIViewport *vp, float alpha);
};

///////////////////////////////////////////////////////////////////////////////
// class CStructuredText

class INode;

//! 2D Structured Text control
class CStructuredText : public Control, public ITextContainer
{
protected:
  //! structured text
  Ref<INode> _text;

  //! height of single row
  float _size;

  //! background color
  PackedColor _bgColor;
public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  CStructuredText(ControlsContainer *parent, int idc, ParamEntryPar cls, bool ignoreText = false);

  virtual IControl *GetControl() {return this;}

  virtual PackedColor GetColor() const {return PackedWhite;} // TODO:
  virtual void SetColor(PackedColor color) {SetTextColor(color);}

  bool IsReady() const;
  virtual void OnDraw(UIViewport *vp, float alpha);
  INode *GetStructuredText() const {return _text;}
  //! Set actual text
  void SetStructuredText(RString content);
  //! Set actual text
  void SetText(INode *text);
  //! Set actual text without conversion
  void SetRawText(RString content);
  //! Get height of actual text
  float GetTextHeight();
  virtual void SetFont(Font *font);
  virtual void SetBgColor(PackedColor color) {_bgColor = color;}
  virtual void SetTextColor(PackedColor color);

  // ITextContainer interface
  virtual RString GetText() const;
  virtual bool SetText(RString text);
  virtual void SetShadow(int shadow);
};

///////////////////////////////////////////////////////////////////////////////
// class CXKeyDesc

//! Xbox joystick button description
class CXKeyDesc : public CStructuredText
{
  typedef CStructuredText base;

private:
  //! joystick button
  int _key;
  
  //! joystick button description
  RefR<INode> _keyDesc;

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  CXKeyDesc(ControlsContainer *parent, int idc, ParamEntryPar cls);

  //! joystick button
  int GetKey() const {return _key;}
  //! Set actual text
  void SetHint(RString hint, bool structured);

  virtual void OnDraw(UIViewport *vp, float alpha);
};

///////////////////////////////////////////////////////////////////////////////
// class CXButton

//! Xbox active text description
class CXButton : public CStructuredText
{
  typedef CStructuredText base;

private:
  //! text color
  PackedColor _color;
  //! selected text color
  PackedColor _colorActive;
  //! selected text color
  PackedColor _colorActive2;
  //! background color of selected item
  PackedColor _bgColorActive;
  //! background color of selected item
  PackedColor _bgColorActive2;
  //! disabled text color
  PackedColor _colorDisabled;

  //@{
  //! sounds
  SoundPars _pushSound;
  SoundPars _clickSound;
  SoundPars _escapeSound;
  //@}

  //! user action (expression) executed when button is pressed
  RString _action;

  UITime _start;

  float _speed;

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  CXButton(ControlsContainer *parent, int idc, ParamEntryPar cls);

  virtual bool OnKeyDown(int dikCode);
  virtual bool OnKeyUp(int dikCode);
  virtual bool OnShortcutDown();
  virtual bool OnShortcutUp();

  virtual void OnLButtonDown(float x, float y);
  virtual void OnLButtonUp(float x, float y);
  virtual void OnDraw(UIViewport *vp, float alpha);

  //! returns user action
  RString GetAction() {return _action;}
  //! sets user action
  void SetAction(RString action) {_action = action;}

  bool CanBeDefault() const {return true;}

  virtual void SetTextColor(PackedColor color) {_color = color;}
  virtual void SetActiveColor(PackedColor color) {_colorActive = color;}

protected:
  //! reaction when button is clicked
  void DoClick();
};

///////////////////////////////////////////////////////////////////////////////
// class CShortcutButton

//! Hybrid button showing the shortcut when available
class CShortcutButton : public CStructuredText
{
  typedef CStructuredText base;

private:
  //! text color
  PackedColor _color;
  //! secondary text color (for color animation)
  PackedColor _color2;
  //! disabled text color
  PackedColor _colorDisabled;
  //! background texture color
  PackedColor _bgColor;
  //! secondary background texture color (for color animation)
  PackedColor _bgColor2;
  //! shortcuts matches XInput present or not
  bool _isWithXInput;

  //@{
  /// background textures
  Ref<Texture> _textureNormal;
  Ref<Texture> _textureDisabled;
  Ref<Texture> _textureOver;
  Ref<Texture> _textureFocused;
  Ref<Texture> _texturePressed;
  Ref<Texture> _textureDefault;
  //@}

  /// texture used when no shortcut is available
  Ref<Texture> _textureNoShortcut;
  /// current shortcut texture
  Ref<Texture> _textureShortcut;

  //@{
  /// hit zone position
  float _hitzoneLeft;
  float _hitzoneTop;
  float _hitzoneRight;
  float _hitzoneBottom;
  //@}

  //@{
  /// shortcut icon position
  float _iconLeft;
  float _iconTop;
  float _iconWidth;
  float _iconHeight;
  //@}

  //@{
  /// text position
  float _textLeft;
  float _textTop;
  float _textRight;
  float _textBottom;
  //@}

  //@{
  //! sounds
  SoundPars _pushSound;
  SoundPars _clickSound;
  SoundPars _escapeSound;
  //@}

  //! user action (expression) executed when button is pressed
  RString _action;

  /// the button is pressed
  bool _pressed;
  /// the keyboard input is enabled
  bool _keyboardEnabled;

  /// start of animation
  UITime _start;
  /// animation speed
  float _speed;

  /// color animation speed when button is focused
  float _speedFocused;
  /// color animation speed when mouse is over
  float _speedOver;

public:
  //! constructor
  /*!
  Used when control is created by resource template.
  \param parent control container by which this control is owned
  \param idc id of control
  \param cls resource template
  */
  CShortcutButton(ControlsContainer *parent, int idc, ParamEntryPar cls);

  virtual bool IsEnabled() const {return _enabled/* && _keyboardEnabled*/;}
  virtual bool IsInside(float x, float y) const;
  virtual bool IsReady() const;

  virtual bool OnKeyDown(int dikCode);
  virtual bool OnKeyUp(int dikCode);
  virtual bool OnShortcutDown();
  virtual bool OnShortcutUp();

  virtual bool OnKillFocus();

  virtual void OnLButtonDown(float x, float y);
  virtual void OnLButtonUp(float x, float y);
  virtual void OnDraw(UIViewport *vp, float alpha);

  virtual void SetBgColor(PackedColor color) {_bgColor = color;}
  virtual void SetTextColor(PackedColor color) {_color = color;}

  //! returns user action
  RString GetAction() {return _action;}
  //! sets user action
  void SetAction(RString action) {_action = action;}

  bool CanBeDefault() const {return true;}

  virtual void UpdateShortcuts();

protected:
  //! reaction when button is clicked
  void DoClick();
};

///////////////////////////////////////////////////////////////////////////////
// class CHTML

//! HTML style of text
enum HTMLFormat
{
  HFError = -1,
  HFP = 0,
  HFH1,
  HFH2,
  HFH3,
  HFH4,
  HFH5,
  HFH6,
  HFImg,
};

//! Alignment of text
enum HTMLAlign
{
  HALeft,
  HACenter,
  HARight
};

//! Single text field
struct HTMLField
{
  bool nextline;
  bool bottom;
  bool bold;
  bool exclude;
  RString text;
  HTMLFormat format;
  HTMLAlign align;
  RString href;
  // image parameters
  RString condition;
  Ref<Texture> texture1;
  Ref<Texture> texture2;
  float width;
  float heightAbs;
  float heightLines;
  float indent;
  float tableWidthAbs;
  float tableWidthRel;
  //text properties  
  //!field text font size
  float fontSize;       
  //! true if filed text shouldn't use default color
  bool colored;        
  //! field text color 
  PackedColor color;    
  //! field font type  
  Ref<Font> font;   

  Texture *GetTexture() const;
};
TypeIsMovableZeroed(HTMLField);

//! Single HTML row
struct HTMLRow
{
  HTMLAlign align;
  bool bottom;
  float width;
  float height;
  int firstField;
  int firstPos;
  int lastField;
  int lastPos;
};
TypeIsMovableZeroed(HTMLRow);

//! HTML section
struct HTMLSection
{
  //! fields in section
  AutoArray <HTMLField> fields;
  //! names of section
  AutoArray <RString> names;
  //! rows info
  AutoArray <HTMLRow> rows;
#if _VBS3 // support for dynamic briefing
  //! html text form of this section
  RString html;
#endif

};
TypeIsMovable(HTMLSection);

class HTMLContent
{
protected:
  //! name of the source file
  RString _filename;
  //! array of sections
  AutoArray<HTMLSection> _sections;
  //! current indent - used only during parsing
  float _indent;
  //! whether the html content was loaded (web or file source both found and loaded)
  bool _loaded;

#if _VBS3
  RString _missionDescContent;
#endif

public:
  HTMLContent() : _loaded(false) {_indent = 0;}

  //! clear whole content of control
  virtual void Init();

  //! loads HTML file
  /*!
  \param filename name of source file
  \param add true if content of file is added, otherwise current content is replaced
  \param parseString reads briefing from String (VBS only)
  */
  void Load(const char *filename, bool add = false, const RString parseString = RString());

  /// helper to read content from the stream
  void Load(QIStream &in);
  void LoadLocalized(QIStream &in);

  bool Loaded() const { return _loaded; }

#if _VBS3
  RString &GetMissionBriefing(){ return _missionDescContent; };
  //! parse RString html contents, used by AAR
  /*
  \param missionDesc string to parse
  */
  void Parse(RString missionDesc, bool add = false);
#endif

  //! returns number of section
  int NSections() const {return _sections.Size();}
  //! returns single section
  const HTMLSection &GetSection(int s) const {return _sections[s];}
  //! returns single section
  HTMLSection &GetSection(int s) {return _sections[s];}
  //! find section by name
  int FindSection(const char *name);

  //! returns current indent
  float GetIndent() const {return _indent;}
  //! sets current indent
  void SetIndent(float indent) {_indent = indent;}

  //! Add a new section
  /*!
  \return index of added section
  */
  int AddSection();
  //! Clear content of section
  void InitSection(int section);
  //! Finish the currently parsed section
  virtual void FormatSection(int s) {}

  //! add end of line to the end of given section
  void AddBreak(int section, bool bottom, float lines = 1.0f);
  //! add text with specific font properties to the end of given section 
  void AddText(
      int section, RString text,
      HTMLFormat format, HTMLAlign align, bool bottom, bool bold,
      RString href, float tableWidthAbs = 0, float tableWidthRel = 0,
      bool colored = false, PackedColor color = PackedColor(),float fontSize=-1, RString fontName = "");
  //! add name to given section
  void AddName(int section, RString name);
  //! add picture to the end of given section
  HTMLField *AddImage(
    int section, RString image, HTMLAlign align, bool bottom,
    float w, float h, RString href, RString text = RString(),
    float tableWidthAbs = 0, float tableWidthRel = 0);
  //!convert HTML color (#XXXXXX) to PackedColor. 
  static PackedColor ParseColorHexa(RString text);

protected:
  /// load the image when incomplete path is given
  Ref<Texture> LoadImage(RString path);
};

//! Data part of HTML control
/*!
  Used both for 2D and 3D HTML controls.
*/
class CHTMLContainer : public HTMLContent
{
  typedef HTMLContent base;

public:
  struct FieldIndex
  {
    int _section;
    int _field;

    FieldIndex() {_section = -1; _field = -1;}
    FieldIndex(int s, int f) {_section = s; _field = f;}
    bool IsValid() const {return _section >= 0;}
  };

protected:
  //! array of bookmarks
  AutoArray <RString> _bookmarks;
  //! currently displayed section
  int _currentSection;
  //! field with mouse over it
  FieldIndex _activeFieldMouse;
  //! field focused by keyboard
  FieldIndex _activeFieldKeyboard;

  /// number of columns (pages) shown
  int _columns;
  /// space between columns
  float _columnsSpace;

  //! background color
  PackedColor _bgColor;
  //! text (foreground) color
  PackedColor _textColor;
  //! color of bold text
  PackedColor _boldColor;
  //! color of link
  PackedColor _linkColor;
  //! color of active link (mouse over it)
  PackedColor _activeLinkColor;

  PackedColor _pictureColor;
  PackedColor _pictureColorLink;
  PackedColor _pictureColorSelected;
  PackedColor _pictureColorBorder;

  //@{
  //! fonts
  Ref<Font> _fontH1;
  Ref<Font> _fontH1Bold;
  float _sizeH1;
  Ref<Font> _fontH2;
  Ref<Font> _fontH2Bold;
  float _sizeH2;
  Ref<Font> _fontH3;
  Ref<Font> _fontH3Bold;
  float _sizeH3;
  Ref<Font> _fontH4;
  Ref<Font> _fontH4Bold;
  float _sizeH4;
  Ref<Font> _fontH5;
  Ref<Font> _fontH5Bold;
  float _sizeH5;
  Ref<Font> _fontH6;
  Ref<Font> _fontH6Bold;
  float _sizeH6;
  Ref<Font> _fontP;
  Ref<Font> _fontPBold;
  float _sizeP;
  //@}

  //! keyboard cycles through links through begin / end of document
  bool _cycleLinks;
  //! left / right events goes through all links (not only through a single row)
  bool _cycleAllLinks;

  //! texture name for previous page icon
  RString _prevPage;
  //! texture name for next page icon
  RString _nextPage;

public:
  //! constructor
  /*!
    \param cls resource template
  */
  CHTMLContainer(ParamEntryPar cls);

  //! return control interface
  virtual IControl *GetControl() = 0;

  int NColumns() const {return _columns;}

  // implementation
  //! remove section
  void RemoveSection(int s);
  //! copy section
  void CopySection(int from, int to);
  //! switch to section identified by name
  virtual void SwitchSection(const char *name);
  //! switch to section identified by index
  void SwitchSectionRaw(int index);
  //! returns index of currently displayed section
  int CurrentSection() const {return _currentSection;}

  //! add bookmark to control
  void AddBookmark(RString link);

  //! clear whole content of control
  virtual void Init();

  //! index of bookmark current section is on
  int ActiveBookmark() const;

  //! name of bookmark current section is on
  RString ActiveBookmarkName() const;
  
  //! returns active field (with mouse over it)
  const HTMLField *GetActiveFieldMouse() const;

  //! returns active field (with mouse over it)
  const HTMLField *GetActiveFieldKeyboard() const;

  //! index of field with keyboard focus
  FieldIndex GetActiveFieldIndex() const {return _activeFieldKeyboard;}

  //! set index of field with keyboard focus
  void SetActiveFieldIndex(const FieldIndex &index) {_activeFieldKeyboard = index;}

  //! returns background color
  PackedColor GetBgColor() const {return _bgColor;}
  //! set background color
  void SetBgColor(PackedColor color) {_bgColor = color;}
  //! returns text (foreground) color
  PackedColor GetTextColor() const {return _textColor;}
  //! set text (foreground) color
  void SetTextColor(PackedColor color) {_textColor = color;}
  //! returns link color
  PackedColor GetLinkColor() const {return _linkColor;}
  //! sets link color
  void SetLinkColor(PackedColor color) {_linkColor = color;}

  //! Split section into individual rows
  void FormatSection(int s);

  //! returns total width of page
  virtual float GetPageWidth() const = 0;
  //! returns total height of page
  virtual float GetPageHeight() const = 0;

  //! Return text width
  virtual float GetTextWidth(float size, Font *font, const char *text) const = 0;

  //! returns height of paragraph text
  float GetPHeight() const {return _sizeP;}

  //@{
  //! fonts / sizes settings
  void SetFontH1(Font *font) {_fontH1 = font;}
  void SetFontH2(Font *font) {_fontH2 = font;}
  void SetFontH3(Font *font) {_fontH3 = font;}
  void SetFontH4(Font *font) {_fontH4 = font;}
  void SetFontH5(Font *font) {_fontH5 = font;}
  void SetFontH6(Font *font) {_fontH6 = font;}
  void SetFontP(Font *font) {_fontP = font;}
  void SetFontH1B(Font *font) {_fontH1Bold = font;}
  void SetFontH2B(Font *font) {_fontH2Bold = font;}
  void SetFontH3B(Font *font) {_fontH3Bold = font;}
  void SetFontH4B(Font *font) {_fontH4Bold = font;}
  void SetFontH5B(Font *font) {_fontH5Bold = font;}
  void SetFontH6B(Font *font) {_fontH6Bold = font;}
  void SetFontPB(Font *font) {_fontPBold = font;}
  void SetFontHeightH1(float h) {_sizeH1 = h;}
  void SetFontHeightH2(float h) {_sizeH2 = h;}
  void SetFontHeightH3(float h) {_sizeH3 = h;}
  void SetFontHeightH4(float h) {_sizeH4 = h;}
  void SetFontHeightH5(float h) {_sizeH5 = h;}
  void SetFontHeightH6(float h) {_sizeH6 = h;}
  void SetFontHeightP(float h) {_sizeP = h;}
  //@}

protected:  
  //! find field with given control coordinates
  virtual FieldIndex FindField(float x, float y) const;

  //! calculate width of (part of) field
  float GetFieldWidth(const HTMLField &field, int from, int to) const;
  
  //! reaction to link activation
  bool ActivateLink(ControlsContainer *parent, int idc, bool mouse);

  //! Split section into pages (each page becomes individual section)
  void SplitSection(int s);

  //! find the first active field in current section
  FieldIndex FirstActiveField() const;
  //! find the last active field in current section
  FieldIndex LastActiveField() const;
  //! find next active field in current section
  bool NextActiveField(FieldIndex &activeField) const;
  //! find previous active field in current section
  bool PrevActiveField(FieldIndex &activeField) const;
  //! find active field in next row of current section
  bool NextRowActiveField(FieldIndex &activeField) const;
  //! find active field in previous row of current section
  bool PrevRowActiveField(FieldIndex &activeField) const;

  //! find offset of field center on given row
  float GetFieldOffset(const HTMLSection &section, const HTMLRow &row, int fld) const;

  //! find active field with closest x offset on given row
  int BestActiveField(int sectionIndex, int rowIndex, float offset) const;

  /// enumarate all rows visible on screen in given range
  bool ForEachRow(int from, int to, const std::function<bool(int sectionIndex, int rowIndex, int totalIndex)> &func) const;
  /// enumarate all fields in given range
  bool ForEachField(const FieldIndex &from, const FieldIndex &to, const std::function<bool(const FieldIndex &index, const HTMLSection &section, const HTMLField &field)> &func) const;
  bool ForEachFieldReverse(const FieldIndex &from, const FieldIndex &to, const std::function<bool(const FieldIndex &index, const HTMLSection &section, const HTMLField &field)> &func) const;

  void PatchActiveField(int s);
};

//! 2D HTML control
class CHTML : public Control, public CHTMLContainer
{
public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  CHTML(ControlsContainer *parent, int idc, ParamEntryPar cls);

  virtual IControl *GetControl() override {return this;}
  
  virtual bool IsEnabled() const override;
  virtual bool IsReady() const override;

  virtual void OnDraw(UIViewport *vp, float alpha) override;
  virtual void OnLButtonDown(float x, float y) override;
  virtual void OnMouseMove(float x, float y, bool active = true) override;
  virtual void OnMouseHold(float x, float y, bool active  = true) override;
  virtual bool OnKeyDown(int dikCode) override;
  virtual bool OnKeyUp(int dikCode) override;
  virtual bool OnSetFocus(ControlFocusAction action, bool def) override;

  virtual float GetPageWidth() const override;
  virtual float GetPageHeight() const override;
  virtual float GetTextWidth(float size, Font *font, const char *text) const override;

  virtual void SwitchSection(const char *name) override;

  virtual void SetFont(Font *font) override {_fontP = font;}
  virtual void SetFontHeight(float h) override {_sizeP = h;}
#if _VBS3 // getFontHeight
  virtual float GetFontHeight() override {return _sizeP;}
#endif
  virtual void SetBgColor(PackedColor color) override {CHTMLContainer::SetBgColor(color);}
  virtual void SetTextColor(PackedColor color) override {CHTMLContainer::SetTextColor(color);}

protected:  
  virtual FieldIndex FindField(float x, float y) const override;
};

class IAutoComplete;

#if __ICL
  #include "autoComplete.hpp"
#endif

///////////////////////////////////////////////////////////////////////////////
// class CEdit

//! Data part of Edit control
/*!
  Used both for 2D and 3D Edit controls.
*/
class CEditContainer : public ITextContainer
{
protected:
  /// current text content in unicode
  RWString _textUnicode;

  Ref<IAutoComplete> _autoComplete;
  /// autocomplete cache (avoid allocation each frame)
  RString _lastTip;
  /// autocomplete cache (avoid allocation each frame)
  Ref<INode> _lastTipFormated;

  //! text (foreground) color
  PackedColor _ftColor;
  //! color of selection
  PackedColor _selColor;
  //! used font
  Ref<Font> _font;
  //! font size
  float _size;

  //! first visible char
  int _firstVisible;
  //! beginning of selection
  int _blockBegin;
  //! end of selection
  int _blockEnd;

  //! limit for chars number
  int _maxChars;
  //! tooltip should be enabled only after editing
  bool _enableToolip;
  
  //! draw caret even when control has no focus or is disabled
  bool _forceDrawCaret;

  //! description of lines for multiline edit
  AutoArray<int> _lines;

#if _VBS3
  //! HTML control, enables disables default return behavior creates
  //! new lines, and on saving converts new lines to <br>
  bool _htmlControl;
#endif

public:
  //! constructor
  /*!
    \param cls resource template
  */
  CEditContainer(ParamEntryPar cls);

  virtual PackedColor GetColor() const {return _ftColor;}
  virtual void SetColor(PackedColor color) {_ftColor = color;}

  //! sets contained text in UTF-8
  virtual bool SetText(RString text);
  //! sets autocomplete type
  virtual void SetAutoComplete(IAutoComplete *ac);
  //! returns contained text in UTF-8
  virtual RString GetText() const;
  //! sets maximal number of contained chars
  void SetMaxChars(int value) {_maxChars = value;}
  //! returns maximal number of contained chars
  int GetMaxChars() const {return _maxChars;}
  //! Return position of caret
  int GetCaretPos() const {return _blockEnd;}
  //! sets position of caret
  void SetCaretPos(int pos);
  //! sets block
  void SetBlock(int begin, int end);
  //! generic reaction on key pressed
  bool DoKeyDown(int dikCode);
  //! generic reaction on key depressed
  bool DoKeyUp(int dikCode);
  //! generic reaction on char entered
  bool DoChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
  //! generic reaction on IME char entered
  bool DoIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
  //! generic reaction on IME composition entered
  bool DoIMEComposition(unsigned nChar, unsigned nFlags);

  //@{
  //! operations with selection
  void BlockDelete();
  void BlockCopy();
  void BlockCut();
  void BlockPaste();
  //@}

protected:
//  virtual int FindPos(float x, float y);
  //! returns if edit is multiline
  virtual bool IsMulti() const = 0;

  //! sets contained text in CP_ACP
  bool SetTextInternal(RWString text);

  //@{
  //! conversion between character end control position
  virtual float PosToX(RWString text, int pos) const = 0;
  virtual int XToPos(RWString text, float x) const = 0;
  //@}

  //! forced position to be visible (scroll content)
  virtual void EnsureVisible(int pos) = 0;

  //! current line of multiline edit
  int CurLine() const;
  //! first visible line of multiline edit
  int FirstLine() const;
  //! returns single line of multiline edit
  RWString GetLine(int i) const;
  //! format multiline text
  /*!
    Split multiline text into individual lines.
  */
  virtual void FormatText() = 0;

  //! find next char position
  int NextPos(int pos);
  //! find previous char position
  int PrevPos(int pos);
};

//! 2D edit control
class CEdit : public Control, public CEditContainer
{
protected:
  //!controls carek blinking
  float _caretBlinkTime;

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  CEdit(ControlsContainer *parent, int idc, ParamEntryPar cls);

  virtual IControl *GetControl() {return this;}

  virtual void OnDraw(UIViewport *vp, float alpha);
  virtual bool OnKeyDown(int dikCode);
  virtual bool OnKeyUp(int dikCode);
  virtual bool OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
  virtual bool OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
  virtual bool OnIMEComposition(unsigned nChar, unsigned nFlags);
  virtual void OnLButtonDown(float x, float y);
  virtual void OnLButtonUp(float x, float y);
  virtual void OnMouseMove(float x, float y, bool active = true);

  virtual void SetFont(Font *font) {_font = font;}
  virtual void SetFontHeight(float h) {_size = h;}
#if _VBS3 // getFontHeight
  virtual float GetFontHeight() {return _size;}
#endif
  virtual void SetTextColor(PackedColor color) {_ftColor = color;}

protected:
  bool IsMulti() const;

  //! find position in text for coordinates (<x>, <y>)
  int FindPos(float x, float y);

  float PosToX(UIViewport *vp, RWString text, int pos) const;
  float PosToX(RWString text, int pos) const;
  int XToPos(RWString text, float x) const;

  void EnsureVisible(int pos);
  //! draw single line of text
  void DrawText(UIViewport *vp, const wchar_t *text, int offset, float x, float y, float alpha, const Rect2DFloat &clipRect);

  void FormatText();
};

///////////////////////////////////////////////////////////////////////////////
// class CButton

//! 2D Button control 
class CButton : public Control, public CTextContainer
{
friend class MsgBox;
protected:
  //! text (foreground) color
  PackedColor _ftColor;
  //! disabled text color
  PackedColor _ftColorDisabled;
  //! used font
  Ref<Font> _font;
  //! font size
  float _size;

  /// Background color (mouse not over)
  PackedColor _bgColor;
  /// Background color (mouse over)
  PackedColor _bgColorActive;
  /// Background color (control disabled)
  PackedColor _bgColorDisabled;
  /// Background color (focused)
  PackedColor _bgColorFocused;
  /// Background color (focused, blinking)
  PackedColor _bgColorFocused2;
  /// Offset in base state
  float _offsetX;
  float _offsetY;
  /// Offset in pressed state
  float _offsetPressedX;
  float _offsetPressedY;

  // Shadow color
  PackedColor _shadowColor;

  // Border color
  PackedColor _borderColor;
  // Border size (negative when border is on the right side)
  float _borderSize;

  //@{
  //! sounds
  SoundPars _enterSound;
  SoundPars _pushSound;
  SoundPars _clickSound;
  SoundPars _escapeSound;
  //@}

  //! current state (pressed / depressed)
  bool _state;

  //! user action (expression) executed when button is pressed
  RString _action;

  /// start of blinking
  UITime _start;
  /// speed of blinking
  float _speed;

protected:
  //! constructor
  //! constructor
  /*!
    Used only when control is created directly in program.
    (For example in message box.)
    Initial placement is <x>, <y>, <w>, <h>
    \param parent control container by which this control is owned
    \param cls aditional resource template
  */
  CButton(ControlsContainer *parent, ParamEntryPar cls, float x, float y, float w, float h);

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  CButton(ControlsContainer *parent, int idc, ParamEntryPar cls);

  virtual IControl *GetControl() {return this;}

  virtual PackedColor GetColor() const {return _ftColor;}
  virtual void SetColor(PackedColor color) {_ftColor = color;}

  virtual bool OnKeyDown(int dikCode);
  virtual bool OnKeyUp(int dikCode);
  virtual bool OnShortcutDown();
  virtual bool OnShortcutUp();

  virtual bool OnKillFocus();

  virtual void OnMouseEnter(float x, float y);
  virtual void OnLButtonDown(float x, float y);
  virtual void OnLButtonUp(float x, float y);
  virtual void OnDraw(UIViewport *vp, float alpha);

  virtual PackedColor BGColor() const { return _bgColor; }

  //! event handler - performed if button is clicked
  virtual void OnClicked();

  //! returns text (foreground) color
  PackedColor GetTextColor() {return _ftColor;}
  
  virtual void SetFont(Font *font) {_font = font;}
  virtual void SetFontHeight(float h) {_size = h;}
#if _VBS3 // getFontHeight
  virtual float GetFontHeight() {return _size;}
#endif
  virtual void SetTextColor(PackedColor color) {_ftColor = color;}

  //! returns user action
  RString GetAction() {return _action;}
  //! sets user action
  void SetAction(RString action) {_action = action;}

  bool CanBeDefault() const {return true;}

protected:
  //! reaction when button is clicked
  void DoClick();
};

///////////////////////////////////////////////////////////////////////////////
// class CTabButton
class CTabButton : public CButton
{
  bool _active;
public:
  CTabButton(ControlsContainer *parent, int idc, ParamEntryPar cls)
    : CButton(parent, idc, cls), _active(false) {}
  PackedColor BGColor() const override;
  bool IsFocused() const override {return false;}
  
  void SetActive(bool val) { _active=val; }
};

///////////////////////////////////////////////////////////////////////////////
// class CActiveText

//! 2D Active text control
class CActiveText : public Control, public CTextContainer
{
protected:
  //! used font
  Ref<Font> _font;
  //! font size
  float _size;
  //! text color
  PackedColor _color;
  //! activated text color (if mouse cursor is over control)
  PackedColor _colorActive;
  //! picture
  Ref<Texture> _texture;

  //@{
  //! sounds
  SoundPars _enterSound;
  SoundPars _pushSound;
  SoundPars _clickSound;
  SoundPars _escapeSound;
  //@}

  //! user action (expression) executed when button is pressed
  RString _action;

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  CActiveText(ControlsContainer *parent, int idc, ParamEntryPar cls);

  virtual IControl *GetControl() {return this;}

  virtual PackedColor GetColor() const {return _color;}
  virtual void SetColor(PackedColor color) {_color = color;}

  virtual bool OnKeyDown(int dikCode);
  virtual bool OnKeyUp(int dikCode);
  virtual bool OnShortcutDown();
  virtual bool OnShortcutUp();

  virtual void OnMouseEnter(float x, float y);
  virtual void OnLButtonDown(float x, float y);
  virtual void OnLButtonUp(float x, float y);
  virtual void OnDraw(UIViewport *vp, float alpha);

  virtual bool IsInside(float x, float y) const;

  virtual bool SetText(RString text); // returns true if changed

  //! Return current texture
  Texture *GetTexture() const {return _texture;}
  //! Set texture
  void SetTexture(Texture *texture) {_texture = texture;}

  virtual void SetFont(Font *font) {_font = font;}
  virtual void SetFontHeight(float h) {_size = h;}
#if _VBS3 // getFontHeight
  virtual float GetFontHeight() {return _size;}
#endif
  virtual void SetTextColor(PackedColor color) {_color = color;}
  virtual void SetActiveColor(PackedColor color) {_colorActive = color;}

  //! returns user action
  RString GetAction() {return _action;}
  //! sets user action
  void SetAction(RString action) {_action = action;}

  bool CanBeDefault() const {return true;}

protected:
  //! reaction when button is clicked
  void DoClick();
};

///////////////////////////////////////////////////////////////////////////////
// class CSlider

//! Data part of Slider control
/*!
  Used both for 2D and 3D Slider controls.
*/
class CSliderContainer
{
protected:
  //! minimal value represented by slider
  float _minPos;
  //! maximal value represented by slider
  float _maxPos;
  //! current value represented by slider
  float _curPos;
  //! value difference performed by moving by one line
  float _lineStep;
  //! value difference performed by moving by one page
  float _pageStep;
  //! minimal step (value will be always multiple of it)
  float _step;

  //! mouse position on thumb
  float _thumbOffset;
  //! thumb is moving
  bool _thumbLocked;

  //! foreground (textures) color
  PackedColor _color;
  //! foreground (textures) color for active control
  PackedColor _colorActive;
  //! foreground (textures) color for disabled control
  PackedColor _colorDisabled;

  //! attached title
  /*!
  - warning: title must be before slider in resource
  */
  InitPtr<CStructuredText> _title;
  //! base title color
  PackedColor _titleColorBase;
  //! active title color
  PackedColor _titleColorActive;

  //! attached value
  /*!
  - warning: value must be before slider in resource
  */
  InitPtr<ITextContainer> _value;
  //! attached value format
  RString _valueFormat;
  //! attached value type
  int _valueType;
  //! base value color
  PackedColor _valueColorBase;
  //! active value color
  PackedColor _valueColorActive;
  
public:
  //! constructor
  /*!
    \param cls resource template
  */
  CSliderContainer(ControlsContainer *parent, ParamEntryPar cls);

  //! returns current value
  float GetThumbPos() {return _curPos;}
  //! retrieves minimal and maximal value
  void GetRange(float &min, float &max) {min = _minPos; max = _maxPos;}
  //! returns difference between maximal and minimal value
  float GetRange() {return _maxPos - _minPos;}
  //! retrieves amount of change processed after user input
  void GetSpeed(float &line, float &page) {line = _lineStep; page = _pageStep;}
  //! sets current value
  void SetThumbPos(float pos);
  //! sets minimal and maximal value
  void SetRange(float min, float max)
  {
    if (min <= max)
    {
      _minPos = min;
      _maxPos = max;
    }
    SaturateThumbPos();
  }
  void SetRangeAndThumbPos(float min, float max, float pos)
  {
    Assert(min<max);
    _minPos = min;
    _maxPos = max;
    SetThumbPos(pos);
  }
  
  //! sets amount of change processed after user input
  void SetSpeed(float line, float page, float step = 0) {_lineStep = line; _pageStep = page; _step = step;}

  bool DoKeyDown(int dikCode);

protected:
  void SaturateThumbPos();
};

#if _VBS3 //TODO: clion, please check if that's a IDC?
  #define S_TIME		8806
#endif

//! 2D slider control
class CSlider : public Control, public CSliderContainer
{
#if _VBS3
  Ref<Texture> _arrowTexture;
  Ref<Texture> _arrowTextureMouseOver;
  Ref<Texture> _deathTexture;
  Ref<Texture> _noteTexture;
  Ref<Texture> _speakerTexture;

  // On mouse over change texture
  bool _mouseOverSlider;

  PackedColor _sideColor[TSideUnknown];

  // Death texture parameters
  float _iconWidth;
  float _iconHeight;
  // Note texture parameters
  float _noteWidth;
  float _noteHeight;
  // Arrow Texture parameters
  float _arrowWidth;
  float _arrowHeight;
  // Speaker texture parameters
  float _speakerWidth;
  float _speakerHeight;
#endif

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  CSlider(ControlsContainer *parent, int idc, ParamEntryPar cls);

  virtual bool OnKeyDown(int dikCode);
  virtual void OnLButtonDown(float x, float y);
  virtual void OnLButtonUp(float x, float y);
  virtual void OnMouseMove(float x, float y, bool active = true);
  virtual void OnDraw(UIViewport *vp, float alpha);

  virtual void SetFgColor(PackedColor color) {_color = color;}
  virtual void SetActiveColor(PackedColor color) {_colorActive = color;}
};

//! Xbox slider control
class CXSlider : public Control, public CSliderContainer
{
private:
  bool _leftSpinDown;
  bool _rightSpinDown;

  Ref<Texture> _arrowEmpty;
  Ref<Texture> _arrowFull;
  Ref<Texture> _border;
  Ref<Texture> _thumb;

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  CXSlider(ControlsContainer *parent, int idc, ParamEntryPar cls);

  virtual bool OnKeyDown(int dikCode);
  virtual bool OnKeyUp(int dikCode);
  virtual void OnLButtonDown(float x, float y);
  virtual void OnLButtonUp(float x, float y);
  virtual void OnMouseMove(float x, float y, bool active = true);
  virtual void OnDraw(UIViewport *vp, float alpha);

  virtual void SetFgColor(PackedColor color) {_color = color;}
  virtual void SetActiveColor(PackedColor color) {_colorActive = color;}
};

///////////////////////////////////////////////////////////////////////////////
// class CScrollBar (used in ListBox etc.)

//! Encapsulates properies of scroll bar in some controls
/*!
  Used in listbox and combo box (selector).
*/
class CScrollBar
{
  //@{
  //! placement
  float _x;
  float _y;
  float _w;
  float _h;
  //@}

  //! minimal represented value
  float _minPos;
  //! maximal represented value
  float _maxPos;

  //! current value
  float _curPos;
  //! thumb size
  float _page;
  //! value difference performed by moving by one line
  float _lineStep;
  //! value difference performed by moving by one page
  float _pageStep;

  //! mouse position on thumb
  float _thumbOffset;
  //! thumb is moving
  bool _thumbLocked;
  
  //! is enabled (false if parent control has no items)
  bool _enabled;

  //! is scrollbar horizontal (default is vertical)
  bool _horizontal;

  //! textures are used for drawing
  bool _textures;

  //! foreground color
  PackedColor _color;

  //@{
  /// textures used for drawing
  Ref<Texture> _thumb;
  Ref<Texture> _arrowEmpty;
  Ref<Texture> _arrowFull;
  Ref<Texture> _border;
  //@}

  //@{
  /// when the arrows was clicked
  bool _leftSpinDown;
  bool _rightSpinDown;
  //@}


  //! how should we wait before auto-scroll is started. (in seconds)
  float _autoScrollDelay;
  //! autoScroll speed (how long does it take to move to next line); -1 to disable (in seconds)
  float _autoScrollSpeed;
  //! timer to recognize how much should scrollBar move
  UITime _autoScrollLastTime;
  //! if control has focus, scrolling should be disabled 
  bool _autoScrollEnabled;
  //!defines if autoscroll should move to start after it reaches end
  bool _autoScrollRewind;
  //! defines auto-scroll fading
  float _autoScrollAlpha;
  
public:
  //! constructor
  CScrollBar(bool textures, bool horizontal, ParamEntryPar cls);
  CScrollBar(bool textures, bool horizontal);
  //@{
  //! access to position
  float X() const {return _x;}
  float Y() const {return _y;}
  float W() const {return _w;}
  float H() const {return _h;}
  //@}

  //@{
  //! event handler - events received from control
  void OnLButtonDown(float x, float y);
  void OnLButtonUp(float x, float y);
  void OnMouseHold(float x, float y);
  void OnDraw(UIViewport *vp, float alpha, bool noClip = false);
  //@}

  //! returns if scrolling is enabled
  bool IsEnabled() const {return _enabled;}
  //! enables / disables scrolling
  void Enable(bool enable = true) {_enabled = enable;}
  //! check if given position (<x>, <y>) is inside scroll bar
  bool IsInside(float x, float y) const
    { return x >= _x && x < _x + _w && y >= _y && y < _y + _h;}
  //! returns current value
  float GetPos() const {return _curPos;}
  //! sets current value
  void SetPos(float pos)
  {
    if (pos <= _minPos)
      _curPos = _minPos;
    else if (pos >= _maxPos - _page)
      _curPos = _maxPos - _page;
    else
      _curPos = pos;
  }
  //! sets minimal and maximal value
  void SetRange(float min, float max, float page)
  {
    _minPos = min;
    _maxPos = max;
    _page = page;
    saturateMax(_maxPos, _minPos + page);
    saturate(_curPos, _minPos, _maxPos - page);
  }
  //! sets minimal and maximal value and position
  void SetRangeAndPos(float min, float max, float page, float pos)
  {
    _minPos = min;
    _maxPos = max;
    _page = page;
    saturateMax(_maxPos, _minPos + page);
    _curPos = pos;
    saturate(_curPos, _minPos, _maxPos - page);
  }
  //! sets amount of change processed after user input
  void SetSpeed(float line, float page) {_lineStep = line; _pageStep = page;}
  //! sets scroll bar placement
  void SetPosition(float x, float y, float w, float h)
  {
    _x = x; _y = y; _w = w; _h = h;
  }
  //! sets foreground (lines) color
  void SetColor(PackedColor color) {_color = color;}
  //! locked - send me the mouse button up event
  bool IsLocked() const {return _thumbLocked || _leftSpinDown || _rightSpinDown;}
  
  //! set auto-scroll properties
  void SetAutoScroll(float scrollSpeed = -1, float scrollDelay = 20, bool autorewind = false)
  {
    _autoScrollSpeed = scrollSpeed; _autoScrollDelay = scrollDelay;   
    _autoScrollLastTime =  Glob.uiTime + _autoScrollDelay;
    _autoScrollEnabled = true;
    _autoScrollRewind = autorewind;
  }
  void SetAutoScrollRewind(bool value){_autoScrollRewind = value;}
  //!Perform auto-scroll
  void AutoScroll();
  //!wait <_autoScrollDelay> until scroll starts again
  void RestartAutoScroll() {_autoScrollLastTime =  Glob.uiTime + _autoScrollDelay;};
  //! return auto-scrolling speed (in seconds)
  float GetAutoScrollSpeed() {return _autoScrollSpeed;}
  //! return time we have to wait, until scrolling starts (in seconds)
  float GetAutoScrollDelay() {return _autoScrollDelay;}
  //! return time we have to wait, until scrolling starts (in seconds)
  bool GetAutoScrollRewind() {return _autoScrollRewind;}
  void EnableAutoScroll(bool value) 
  { if(!_autoScrollEnabled || !value) RestartAutoScroll();
  _autoScrollEnabled = value;}
  float GetAutoScrollAlpha() {return _autoScrollAlpha;}


};

///////////////////////////////////////////////////////////////////////////////
// class CProgressBar

//! 2D Progress Bar control
class CProgressBar : public Control
{
  //! minimal value
  float _minPos;
  //! maximal value
  float _maxPos;
  //! current value
  float _curPos;

  //! color of bar
  PackedColor _barColor;
  //! texture of the bar
  Ref<Texture> _texture; 
  //! texture of the frame
  PackedColor _frameColor;

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  CProgressBar(ControlsContainer *parent, int idc, ParamEntryPar cls);

  virtual void OnDraw(UIViewport *vp, float alpha);

  //! returns current value
  float GetPos() {return _curPos;}
  //! sets current value
  void SetPos(float pos)
  {
    if (pos <= _minPos)
      _curPos = _minPos;
    else if (pos >= _maxPos)
      _curPos = _maxPos;
    else
      _curPos = pos;
  }
  //! retrieves minimal and maximal value
  void GetRange(float &min, float &max) {min = _minPos; max = _maxPos;}
  //! returns difference between maximal and minimal value
  float GetRange() {return _maxPos - _minPos;}
  //! sets minimal and maximal value
  void SetRange(float min, float max)
  {
    if (min <= max)
    {
      _minPos = min;
      _maxPos = max;
    }
    if (_curPos < _minPos)
      _curPos = _minPos;
    else if (_curPos > _maxPos)
      _curPos = _maxPos;
  }
  void SetRangeAndPos(float min, float max, float pos)
  {
    Assert(min<max);
    Assert(pos<=max);
    Assert(min<=pos);
    _minPos = min;
    _maxPos = max;
    _curPos = pos;
  }
  //! returns color of bar
  PackedColor GetBarColor() const {return _barColor;}
  //! sets color of frame
  void SetBarColor(PackedColor color) {_barColor = color;}
};

///////////////////////////////////////////////////////////////////////////////
// class CAnimatedTexture

//! 2D animated texture control
class CAnimatedTexture : public Control
{
  //! current phase <0, 1>
  float _phase;
  //! texture
  Ref<AnimatedTexture> _texture;

public:
  //! constructor
  /*!
  Used when control is created by resource template.
  \param parent control container by which this control is owned
  \param idc id of control
  \param cls resource template
  */
  CAnimatedTexture(ControlsContainer *parent, int idc, ParamEntryPar cls);

  virtual void OnDraw(UIViewport *vp, float alpha);

  //! returns current phase
  float GetPhase() const {return _phase;}
  //! sets current phase
  void SetPhase(float phase)
  {
    saturate(phase, 0, 1);
    _phase = phase;
  }
  //! Return animated texture
  AnimatedTexture *GetTexture() const {return _texture;}
  //! Set animated texture
  void SetTexture(AnimatedTexture *texture) {_texture = texture;}
};

///////////////////////////////////////////////////////////////////////////////
// class CListBox

//! represents one row of List Box or Combo Box control
struct CListBoxItem
{
  RString text;
  RString data;
  int value;
  /// selection status for multi selection list box
  bool selected;
  PackedColor ftColor;
  PackedColor selColor;
  Ref<Texture> texture;

#if _VBS3
  //stores the old index, used by drag drop
  int oldIndex;
#endif

  CListBoxItem()
  {
    value = 0;
    selected = false;
  }
};
TypeIsMovableZeroed(CListBoxItem);

struct CListBoxItems : public AutoArray<CListBoxItem>
{
};

//! Data part of List Box control
/*!
  Used both for 2D and 3D List Box controls.
*/
class CListBoxContainer : public CListBoxItems
{
protected:
  //! base color
  PackedColor _color;
  //! base color of selected row
  PackedColor _selBgColor;
  //! base color of selected row
  PackedColor _selBgColor2;
  //! text (foreground) color
  PackedColor _ftColor;
  //! text (foreground) color of selected row
  PackedColor _selColor;
  //! text (foreground) color of selected row
  PackedColor _selColor2;
  //! index of selected row
  int _selString;
  //! start of selection when multiple selection is enabled
  int _selBegin;
  //! index (and offset) topmost row
  float _topString;
  //! hilite selection
  bool _showSelected;
  //! hilite selection
  bool _readOnly;
  //! if false, up / down on first / last row leave the control
  bool _stickyFocus;
  //! this control can be source for drag & drop
  bool _canDrag;
  //! change selection sound
  SoundPars _selectSound;

  UITime _start;
  float _speed;

  //history of pressed keys, use for search based on keyboard input
  RString _charBuffer;
  //!time when was last key pressed
  float _lastCharTime;
  //!max keyDown history age     
  float _maxHistoryDelay;
  //!select item, which starts with given letter
  bool SelectStartString(unsigned nChar);

public:
  //! constructor
  /*!
    \param cls resource template
  */
  CListBoxContainer(ParamEntryPar cls);

  /// returns Control interface
  virtual Control *GetControl() = 0;

  //! returns number of rows
  virtual int GetSize() const {return Size();}
  //! check if row is selected
  virtual bool IsSelected(int row, int style) const;
  //! returns index of selected row
  int GetCurSel() const {return _selString;};
  //! sets selected row
  virtual void SetCurSel(int sel, bool sendUpdate = true) = 0;
  //! sets selected row
  virtual void SetCurSel(float x, float y, bool sendUpdate = true) = 0;

  /// return the position listbox is scrolled to
  float GetScrolledTo() const {return _topString;}
  /// scroll to the given position
  void ScrollTo(float value) {_topString = value; Check();}

  //! returns if selected row will be hilited
  bool IsReadOnly() const {return _readOnly;}
  //! sets if selected row will be hilited
  void SetReadOnly(bool set = true) {_readOnly = set;}

  //! returns base color
  PackedColor GetFgColor()  const {return _color;}
  //! sets base color
  void SetFgColor(PackedColor color) {_color = color;}
  //! returns text (foreground) color
  PackedColor GetActiveColor()  const {return _selColor;}
  //! sets text (foreground) color
  void SetActiveColor(PackedColor color) {_selColor = color;}
  //! returns text (foreground) color
  PackedColor GetTextColor() const {return _ftColor;}
  //! sets text (foreground) color
  void SetTextColor(PackedColor color) {_ftColor = color;}
  //! sets if selected row will be hilited
  void ShowSelected(bool show = true) {_showSelected = show;}

  //! returns text of given row
  virtual RString GetText(int i) const
  {
    if (i < 0 || i >= Size())
      return RString();
    return Get(i).text;
  }
  //! returns data of given row
  virtual RString GetData(int i) const
  {
    if (i < 0 || i >= Size())
      return RString();
    return Get(i).data;
  }
  //! returns value of given row
  virtual int GetValue(int i) const
  {
    if (i < 0 || i >= Size())
      return 0;
    return Get(i).value;
  }
  //! returns text color of given row
  PackedColor GetFtColor(int i) const
  {
    if (i < 0 || i >= Size())
      return PackedBlack;
    return Get(i).ftColor;
  }
  //! returns selected text color of given row
  PackedColor GetSelColor(int i) const
  {
    if (i < 0 || i >= Size())
      return PackedBlack;
    return Get(i).selColor;
  }
  //! returns picture on given row
  Texture *GetTexture(int i) const
  {
    if (i < 0 || i >= Size())
      return NULL;
    return Get(i).texture;
  }
  //! removes all rows
  virtual void ClearStrings()
  {
    Clear();
  }
  //! removes all rows
  virtual void RemoveAll()
  {
    ClearStrings();
    _selString = -1;
    _topString = 0;
  }
  //! add new row and set text for it
  virtual int AddString(RString text)
  {
    int index = Add();
    Set(index).text = text;
    Set(index).data = "";
    Set(index).value = 0;
    Set(index).ftColor = _ftColor;
    Set(index).selColor = _selColor;
    return index;
  }
  //! sets text of given row
  void SetText(int i, RString text)
  {
    if (i < 0 || i >= GetSize()) return;
    Set(i).text = text;
  }
  //! sets data of given row
  void SetData(int i, RString data)
  {
    if (i < 0 || i >= GetSize()) return;
    Set(i).data = data;
  }
  //! sets value of given row
  void SetValue(int i, int value)
  {
    if (i < 0 || i >= GetSize()) return;
    Set(i).value = value;
  }
  //! sets text color of given row
  void SetFtColor(int i, PackedColor color)
  {
    if (i < 0 || i >= GetSize()) return;
    Set(i).ftColor = color;
  }
  //! sets selected text color of given row
  void SetSelColor(int i, PackedColor color)
  {
    if (i < 0 || i >= GetSize()) return;
    Set(i).selColor = color;
  }
  //! sets picture on given row
  void SetTexture(int i, Texture *texture)
  {
    if (i < 0 || i >= GetSize()) return;
    Set(i).texture = texture;
  }
  //! insert new row and set text for it
  virtual void InsertString(int i, RString text);
  //! delete row
  virtual void DeleteString(int i);
  //! sorts rows by their texts
  void SortItems();
  //! sorts rows by their values
  void SortItemsByValue();

  //! event handler - performed if mouse wheel is moved
  /*!
    \param dz amount how much wheel was moved
  */
  void OnMouseZChanged(float dz);
  //! returns number of rows that fits into control at once
  virtual float NRows() const = 0;
  
  //! checks if listbox is properly scrolled after items changed
  void Check();

  virtual void SetTitle(RString text) {}

  virtual void SetColorPicture(bool set) {}

  virtual void Expand(bool expand) {}

  //! user is now able to list in control
  virtual bool IsWorking() const = 0;

  //! if false, up / down on first / last row leave the control
  void SetStickyFocus(bool set) {_stickyFocus = set;}
};

struct Rect2DFloat;

//! 2D List Box control
class CListBox : public Control, public CListBoxContainer
{
protected:
  //! used font
  Ref<Font> _font;
  //! font size
  float _size;
  //! height of single row
  float _rowHeight;

  float RowHeightUsed() const;

  //! number of rows that fits into control at once
  float _showStrings;
  //! enables quick search
  bool _quickSearch;

  //@{
  //! scrolling implementation
  bool _scrollUp;
  bool _scrollDown;
  UITime _scrollTime;
  //@}

  //! scroll bar used by control
  CScrollBar _scrollbar;

  /// width of scrollbar
  float _sbWidth;

  PackedColor _pictureColor;
  PackedColor _pictureSelectColor;

  PackedColor _focusedColor;

  Ref<Texture> _line;
  Ref<Texture> _thumb;

  int _dragRowWanted;
  float _dragX;
  float _dragY;

  // Shadow color
  PackedColor _shadowColor;


#if _VBS3
  bool _OnLostFocusLoseSelected;
#endif
public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  CListBox(ControlsContainer *parent, int idc, ParamEntryPar cls);

  virtual Control *GetControl() {return this;}

  // virtual bool IsEnabled() const {return _enabled && GetSize() > 0;}

  void SetPos(float x, float y, float w, float h);

  virtual bool OnKeyUp(int dikCode);
  virtual bool OnKeyDown(int dikCode);
  virtual void OnMouseMove(float x, float y, bool active = true);
  virtual void OnMouseHold(float x, float y, bool active = true);
  virtual void OnMouseZChanged(float dz);
  virtual void OnLButtonDown(float x, float y);
  virtual void OnLButtonUp(float x, float y);
  virtual void OnLButtonDblClick(float x, float y);
  virtual void OnDraw(UIViewport *vp, float alpha);
  virtual bool OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
  //! event handler - performed if selection is changed
  virtual void OnSelChanged(int curSel);
  virtual bool OnKillFocus(); // return false if focus cannot be killed
  virtual bool OnSetFocus(ControlFocusAction action, bool def);

  //! sets selected row
  virtual void SetCurSel(int sel, bool sendUpdate = true);
  //! sets selected row
  virtual void SetCurSel(float x, float y, bool sendUpdate = true);
  //! sets height of single row
  void SetRowHeight(float height)
  {
    _rowHeight = floatMax(_size, height);
  }
  float GetRowHeight() {return _rowHeight;}
#if _VBS3 
  // set and getRowHeight
  int GetPosIndex(float x, float y);
#endif
  virtual void SetFont(Font *font) {_font = font;}
  virtual void SetFontHeight(float h) {_size = h;}
#if _VBS3 // getFontHeight
  virtual float GetFontHeight() {return _size;}
#endif
  virtual void SetFgColor(PackedColor color) {CListBoxContainer::SetFgColor(color);}
  virtual void SetTextColor(PackedColor color) {CListBoxContainer::SetTextColor(color);}
  virtual void SetActiveColor(PackedColor color) {CListBoxContainer::SetActiveColor(color);}
  //! draws single row
  virtual void DrawItem
  (
    UIViewport *vp, float alpha, int i, bool selected, float top, const Rect2DFloat &rect, float widthDiff=0
  ); 
#if _VBS3
  //! draws single row anywhere on the screen
  virtual void DrawItem
    (
    UIViewport *vp, float alpha, int i, bool selected, float side, float top, const Rect2DFloat &rect
    ); 
#endif
  float NRows() const {return _showStrings;}

  bool IsWorking() const {return IsFocused();}

  void EnableQuickSearch(bool enable) {_quickSearch = enable;}

  //get auto-scroll properties
  float GetAutoScrollSpeed() {return _scrollbar.GetAutoScrollSpeed();}
  float GetAutoScrollDelay() {return _scrollbar.GetAutoScrollDelay();}
  bool GetAutoScrollRewind() {return _scrollbar.GetAutoScrollRewind();}
  //set auto-scroll properties
  void SetAutoScrollSpeed(float value) { _scrollbar.SetAutoScroll(value,_scrollbar.GetAutoScrollDelay());}
  void SetAutoScrollDelay(float value) { _scrollbar.SetAutoScroll(_scrollbar.GetAutoScrollSpeed(),value);}
  void SetAutoScrollRewind(bool value) { _scrollbar.SetAutoScrollRewind(value);}
};

struct SortLisNItem
{
public:
  RString text;
  int value;
  AutoArray<CListBoxItem> rowItems;

  SortLisNItem(){};
  SortLisNItem(RString inText,int inValue,AutoArray<CListBoxItem> items)
  {
    text = inText; 
    value= inValue;
    rowItems = items;
  }
};
TypeIsMovableZeroed(SortLisNItem)

class CListNBox : public CListBox
{
protected:
  int _nColumns;
  //left/right arrow buttons
  int _idcLeft;
  int _idcRight;
  //show/hide arrow buttons
  bool _drawSideArrows;
  //default column used for row value storage
  int _primaryColumn;

public:
  AutoArray<float> _columnsSize; 

public: 

  CListNBox(ControlsContainer *parent, int idc, ParamEntryPar cls);
  void SetCurSel(int sel, bool sendUpdate = true);
  void OnLButtonDown(float x, float y);
  void OnLButtonUp(float x, float y);
  void OnLButtonDblClick(float x, float y);
  virtual void OnMouseZChanged(float dz);

  int NColumns()
  {
    return _nColumns;
  }

  int RowsCount()
  {//number of rows is number of elements divided by number of columns
    return toInt(Size()/_nColumns);
  }

  int PrimaryColumn()
  {
    return _primaryColumn;
  }

  //! set number and position of columns
  void SetColumns(AutoArray<float> columnSize);

  //! returns text of given row
  virtual RString GetText(int row, int column) const
  {
    if (row < 0 || column < 0 || (row * _nColumns + column) >= Size())
      return RString();
    return Get(row * _nColumns + column).text;
  }
  //! returns data of given row
  virtual RString GetData(int row, int column) const
  {
    if (row < 0 || column < 0 || (row * _nColumns + column) >= Size())
      return RString();
    return Get(row * _nColumns + column).data;
  }
  //! returns value of given row
  virtual int GetValue(int row, int column) const
  {
    if (row < 0 || column < 0 || (row * _nColumns + column) >= Size())
      return 0;
    return Get(row * _nColumns + column).value;
  }
  //! returns text color of given row
  PackedColor GetFtColor(int row, int column) const
  {
    if (row < 0 || column < 0 || (row * _nColumns + column) >= Size())
      return _ftColor;
    return Get(row * _nColumns + column).ftColor;
  }
  //! returns selected text color of given row
  PackedColor GetSelColor(int row, int column) const
  {
    if (row < 0 || column < 0 || (row * _nColumns + column) >= Size())
      return _selColor;
    return Get(row * _nColumns + column).selColor;
  }
  //! returns picture on given row
  Texture *GetTexture(int row, int column) const
  {
    if (row < 0 || column < 0 || (row * _nColumns + column) >= Size())
      return NULL;
    return Get(row * _nColumns + column).texture;
  }


  //! sets text of given row
  void SetText(int row, int column, RString text)
  {
    if (row < 0 || column < 0 || (row * _nColumns + column) >= Size()) return;
    Set(row * _nColumns + column).text = text;
  }
  //! sets data of given row
  void SetData(int row, int column, RString data)
  {
    if (row < 0 || column < 0 || (row * _nColumns + column) >= Size()) return;
    Set(row * _nColumns + column).data = data;
  }
  //! sets value of given row
  void SetValue(int row, int column, int value)
  {
    if (row < 0 || column < 0 || (row * _nColumns + column) >= Size()) return;
    Set(row * _nColumns + column).value = value;
  }
  //! sets text color of given row
  void SetFtColor(int row, int column, PackedColor color)
  {
    if (row < 0 || column < 0 || (row * _nColumns + column) >= Size()) return;
    Set(row * _nColumns + column).ftColor = color;
  }
  //! sets selected text color of given row
  void SetSelColor(int row, int column, PackedColor color)
  {
    if (row < 0 || column < 0 || (row * _nColumns + column) >= Size()) return;
    Set(row * _nColumns + column).selColor = color;
  }
  //! sets picture on given row
  void SetTexture(int row, int column, Texture *texture)
  {
    if (row < 0 || column < 0 || (row * _nColumns + column) >= Size()) return;
    Set(row * _nColumns + column).texture = texture;
  }
  //! insert new row and set text for it
  void InsertString(int row, int column, RString text)
  {
    CListBox::InsertString(row * _nColumns + column,text);
  }
  //! delete row
  void DeleteString(int row, int column);

  void AddRow(AutoArray<RString> inText)
  {
    for(int i=0; i< _nColumns; i++)
    {
      if(i < inText.Size()) CListBox::AddString(inText[i]);
      else CListBox::AddString("");
    }
  }

  void DeleteRow(int row)
  {
    if(row>=RowsCount()) return;

    for(int i=0; i< _nColumns; i++)
      DeleteString(row,0);

    if(Size()<=0) SetCurSel(-1);   
  }

  void AddColumn(float columnPosition);

  void DeleteColumn(int columnIndex);

  void ClearColumn(int columnIndex)
  {
    for(int i =0; i< RowsCount();i++)
    {
      SetText(i,columnIndex,"");
    }
  }

  virtual void OnDraw(UIViewport *vp, float alpha);
  virtual void DrawItem
    (
    UIViewport *vp, float alpha, int row, int column, bool selected, float top, float offsetX, 
    const Rect2DFloat &rect, bool showScrollbar
    );


  static int CompareNItems( const SortLisNItem *str0, const SortLisNItem *str1 )
  {
    return stricmp(str0->text, str1->text);
  }
  virtual void SortItems();

  static int CompareNItemsByValues( const SortLisNItem *str0, const SortLisNItem *str1 )
  {
    int diff = str0->value - str1->value;
    if (diff != 0) return diff;
    return stricmp(str0->text, str1->text);
  }
  virtual void SortItemsByValue();

};

//! 2D Xbox List Box control
class CXListBox : public Control, public CListBoxContainer
{
protected:
  //! used font
  Ref<Font> _font;
  //! font size
  float _size;

  bool _leftSpinDown;
  bool _rightSpinDown;
  
  bool _cycle;

  Ref<Texture> _arrowEmpty;
  Ref<Texture> _arrowFull;
  Ref<Texture> _border;

  /// background color when focused
  PackedColor _focusedColor;
  /// background color when disabled
  PackedColor _disabledColor;

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  CXListBox(ControlsContainer *parent, int idc, ParamEntryPar cls);

  virtual Control *GetControl() {return this;}

  virtual bool OnKeyDown(int dikCode);
  virtual bool OnKeyUp(int dikCode);
  virtual void OnLButtonDown(float x, float y);
  virtual void OnLButtonUp(float x, float y);
  virtual void OnLButtonDblClick(float x, float y);
  virtual void OnDraw(UIViewport *vp, float alpha);

  //! called when item is added to list
  int AddString(RString text);
  //! called when item are removed from list
  void ClearStrings();
  //! insert row
  void InsertString(int i, RString text);
  //! delete row
  void DeleteString(int i);
  //! sets selected row
  void SetCurSel(int sel, bool sendUpdate = true);
  //! sets selected row
  void SetCurSel(float x, float y, bool sendUpdate = true);
  float NRows() const {return 1;}
  virtual void SetFont(Font *font) {_font = font;}
  virtual void SetFontHeight(float h) {_size = h;}
#if _VBS3 // getFontHeight
  virtual float GetFontHeight() {return _size;}
#endif
  virtual void SetFgColor(PackedColor color) {CListBoxContainer::SetFgColor(color);}
  virtual void SetTextColor(PackedColor color) {CListBoxContainer::SetTextColor(color);}
  virtual void SetActiveColor(PackedColor color) {CListBoxContainer::SetActiveColor(color);}

  bool IsWorking() const {return IsFocused();}

protected:
  void Down();
  void Up();
};

//! 2D Xbox List Box control
class CXCombo : public Control, public CListBoxContainer
{
protected:
  //! used font
  Ref<Font> _font;
  //! font size
  float _size;

  // list
  float _xList;
  float _yList;
  float _wList;
  float _hList;
  PackedColor _selColor2;
  PackedColor _bgColorList;
  PackedColor _selBgColorList;
  PackedColor _selBgColorList2;
  PackedColor _borderColorList;
  float _sizeList;
  float _rowHeight;

  float RowHeightUsed() const {return _rowHeight;} // TODO: consider rounding, like with CListBox::RowHeightUsed

  // title
  float _xTitle;
  float _yTitle;
  float _wTitle;
  float _hTitle;
  RString _textTitle;
  Ref<Font> _fontTitle;
  float _sizeTitle;
  PackedColor _ftColorTitle;
  PackedColor _selColorTitle;
  PackedColor _bgColorTitle;
  PackedColor _selBgColorTitle;
  PackedColor _borderColorTitle;
  PackedColor _selBorderColorTitle;
  PackedColor _disabledColorTitle;
  PackedColor _disabledBgColorTitle;
  PackedColor _disabledBorderColorTitle;

  PackedColor _ftColorBase;
  PackedColor _selColorBase;
  PackedColor _disabledColor;
  PackedColor _borderColor;
  PackedColor _selBorderColor;
  PackedColor _disabledBorderColor;

  //! number of rows that fits into control at once
  float _showStrings;

  bool _expanded;
  bool _hasTitle;
  int _listString;

  //! scroll bar used by control
  CScrollBar _scrollbar;

  //! expansion sound
  SoundPars _expandSound;
  //! collapsion sound
  SoundPars _collapseSound;

  UITime _expandTime;

  Ref<Texture> _line;
  Ref<Texture> _thumb;
  Ref<Texture> _arrowEmpty;
  Ref<Texture> _arrowFull;
  Ref<Texture> _boxLeft;
  Ref<Texture> _boxRight;
  Ref<Texture> _boxHorz;

public:
  //! constructor
  /*!
  Used when control is created by resource template.
  \param parent control container by which this control is owned
  \param idc id of control
  \param cls resource template
  */
  CXCombo(ControlsContainer *parent, int idc, ParamEntryPar cls);

  virtual Control *GetControl() {return this;}

  virtual bool OnKillFocus();
  virtual bool OnKeyDown(int dikCode);
  virtual RString GetKeyHint(int button, bool &structured) const;
  virtual void OnDraw(UIViewport *vp, float alpha);
  //! draws single row
  virtual void DrawItem
  (
    UIViewport *vp, float alpha, int i, bool selected, float top, float rowHeight, float size,
    const Rect2DFloat &rect, bool list
  ); 

  //! sets selected row
  void SetCurSel(int sel, bool sendUpdate = true);
  //! sets selected row
  void SetCurSel(float x, float y, bool sendUpdate = true);
  void SetListSel(int sel);
  float NRows() const {return _showStrings;}

  virtual void SetTitle(RString text);
  virtual void SetFont(Font *font) {_font = font;}
  virtual void SetFontHeight(float h) {_size = h;}
#if _VBS3 // getFontHeight
  virtual float GetFontHeight() {return _size;}
#endif
  virtual void SetBgColor(PackedColor color) {_bgColorTitle = color;}
  virtual void SetFgColor(PackedColor color) {CListBoxContainer::SetFgColor(color);}
  virtual void SetTextColor(PackedColor color) {CListBoxContainer::SetTextColor(color);}
  virtual void SetActiveColor(PackedColor color) {CListBoxContainer::SetActiveColor(color);}

  virtual void InsertString(int i, RString text);
  virtual void DeleteString(int i);

  bool IsWorking() const {return _expanded;}

  virtual void Expand(bool expand);

protected:
  void Down();
  void Up();
};

///////////////////////////////////////////////////////////////////////////////
// class CTree

//! represents one item of Tree control
struct CTreeItem : public RemoveLLinks
{
friend class CTree;
protected:
  //! item is expanded
  bool expanded;  // use CTree::Expand() to set

public:
  //! level (generation) of item - 0 for root
  int level;
  //! link to parent item - NULL for root
  LLink<CTreeItem> parent;
  //! list of all child items
  RefArray<CTreeItem> children;

  //! visible text
  RString text;
  //! additional text attached to item
  RString data;
  //! additional value attached to item
  int value;

  //! texture
  Ref<Texture> texture;
  //! draw texture?
  bool drawTexture;

  //! constructor
  /*!
    \param level level (generation) of item
    \param parent parent item
  */
  CTreeItem(int level, CTreeItem *parent);
  //! creates new item and adds it into child list
  CTreeItem *AddChild();
  //! creates new item and inserts it into child list
  CTreeItem *InsertChild(int pos);
  //! sorts child list by contained texts
  /*!
    \param reversed true for descending sort
  */
  void SortChildren(bool reversed = false);

  //! expand / collapse item and all subitems recursivly
  /*!
    \param expand true for expansion, false for collapsion
  */
  void ExpandTree(bool expand = true);
  //! returns if the item is expanded
  bool IsExpanded() const {return expanded;}
  //! set expansion only for this item
  void Expand(bool expand) {expanded = expand;}
  //! returns next accessible (child of expanded) item
  CTreeItem *GetNextVisible();
  //! returns previous accessible (child of expanded) item
  CTreeItem *GetPrevVisible();
  //! returns last accessible (child of expanded) item
  CTreeItem *GetLastVisible();
  //! returns younger brother of item
  CTreeItem *GetYBrother();
  //! returns older brother of item
  CTreeItem *GetOBrother();
  //! returns total size of accessible (child of expanded) items
  int NVisibleItems();

  template <typename Type>
  bool ForEachItem(const Type &functor) const
  {
    if (functor(this)) return true;
    for (int i=0; i<children.Size(); i++)
    {
      if (children[i]->ForEachItem(functor)) return true;
    }
    return false;
  }
};

//! 2D Tree control
class CTree : public Control
{
protected:
  //! root item
  Ref<CTreeItem> _root;
  //! selected item
  LLink<CTreeItem> _selected;
  //! first accessible item
  LLink<CTreeItem> _firstVisible;
  
  //! background color
  PackedColor _bgColor;
  //! foreground (text) color
  PackedColor _ftColor;
  //! foreground (text) color of selected item
  PackedColor _selColor;
  //! color of border
  PackedColor _borderColor;
  //! color of down/up arrow
  PackedColor _arrowColor;

  //! used font
  Ref<Font> _font;
  //! font size
  float _size;

  //@{
  //! scrolling implementation
  bool _scrollUp;
  bool _scrollDown;
  UITime _scrollTime;
  float _offset;
  //@}
  //history of pressed keys, use for search based on keyboard input
  RString _charBuffer;
  //!time when was last key pressed
  float _lastCharTime;
  //!max keyDown history age     
  float _maxHistoryDelay;

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  CTree(ControlsContainer *parent, int idc, ParamEntryPar cls);
  
  //! returns root item
  CTreeItem *GetRoot() const {return _root;}
  //! returns selected item
  CTreeItem *GetSelected() const {return _selected;}
  //! sets selected item
  void SetSelected(CTreeItem *item);
  //! expand / collapse given item
  /*!
    \param expand true for expansion, false for collapsion
  */
  void Expand(CTreeItem *item, bool expand = true);
  //! expand items to ensure given item will be accessible
  void EnsureVisible(CTreeItem *item);

  virtual void SetFont(Font *font) {_font = font;}
  virtual void SetFontHeight(float h) {_size = h;}
#if _VBS3 // getFontHeight
  virtual float GetFontHeight() {return _size;}
#endif
  virtual void SetBgColor(PackedColor color) {_bgColor = color;}
  virtual void SetTextColor(PackedColor color) {_ftColor = color;}
  virtual void SetActiveColor(PackedColor color) {_selColor = color;}

  //! remove all items
  void RemoveAll();

  virtual void OnDraw(UIViewport *vp, float alpha);
  void OnLButtonDown(float x, float y);
  void OnLButtonDblClick(float x, float y);
  void OnMouseExit(float x, float y);
  void OnMouseMove(float x, float y, bool active = true);
  void OnMouseHold(float x, float y, bool active = true);
  void OnMouseZChanged(float dz);
  bool OnKeyDown(int dikCode);
  bool OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
  
  //! draw single item
  virtual void DrawItem
  (
    UIViewport *vp, float alpha, CTreeItem *item,
    float top, float topClip, float bottomClip, bool first
  ); 

  template <typename Type>
  void ForEachItem(const Type &functor) const
  {
    if (_root) _root->ForEachItem(functor);
  }

protected:
  //! find item on given screen coordinates (<x>, <Y>)
  CTreeItem *FindItem(float x, float y);
  //! find first accessible item
  CTreeItem *FindFirstVisible();
  //!select item, which starts with given letter
  bool SelectStartString(unsigned nChar);
};

///////////////////////////////////////////////////////////////////////////////
// class CToolBox

//! 2D Tool Box control
/*!
  Used for selection of one value from choice.
  Behave as group of Radio Buttons in Windows.
*/
class CToolBox : public Control
{
protected:
  //! all texts
  AutoArray<RString> _strings;
  //! number of rows
  int _rows;
  //!number of columns
  int _columns;

  //! foreground (text) color
  PackedColor _ftColor;
  //! background color
  PackedColor _bgColor;
  //! foreground (text) color if control has input focus
  PackedColor _ftSelectColor;
  //! background color if control has input focus
  PackedColor _bgSelectColor;
  //! foreground (text) color if control is disabled
  PackedColor _ftDisabledColor;
  //! background color if control is disabled
  PackedColor _bgDisabledColor;
  //! background color if control is disabled
  PackedColor _selectedBg;
  //! used font
  Ref<Font> _font;
  //! font size
  float _size;

  //! currently selected text
  int _selected;

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  CToolBox(ControlsContainer *parent, int idc, ParamEntryPar cls);

  virtual bool OnKeyDown(int dikCode);
  virtual void OnLButtonDown(float x, float y);
  virtual void OnDraw(UIViewport *vp, float alpha);

  //! returns given text
  RString GetText(int i) const
  {
    if (i < 0 || i >= _strings.Size())
      return RString();
    return _strings[i];
  }
  //! sets given text
  void SetText(int i, RString text)
  {
    if (i < 0 || i >= _strings.Size())
      return;
    _strings[i] = text;
  }
  //! returns index of selected text
  int GetCurSel() const {return _selected;}
  //! sets index of selected text
  void SetCurSel(int sel)
  {
    if (sel < 0) sel = 0;
    if (sel >= _strings.Size()) sel = _strings.Size() - 1;
    _selected = sel;
  }
  
  //! returns foreground (text) color
  PackedColor GetTextColor() const {return _ftColor;}
  virtual void SetFont(Font *font) {_font = font;}
  virtual void SetFontHeight(float h) {_size = h;}
#if _VBS3 // getFontHeight
  virtual float GetFontHeight() {return _size;}
#endif
  virtual void SetBgColor(PackedColor color) {_bgColor = color;}
  virtual void SetTextColor(PackedColor color) {_ftColor = color;}
  virtual void SetActiveColor(PackedColor color) {_ftSelectColor = color;}

  //! returns if given item is selected
  virtual bool IsSelected(int i) const;
  //! change selection of given item
  virtual void ChangeSelection(int i);
};

///////////////////////////////////////////////////////////////////////////////
// class CCheckBoxes

//! 2D Check Boxes control
/*!
  Used for selection of several values from choice.
  Behave as group of Check Boxes in Windows.
*/
class CCheckBoxes : public CToolBox
{
protected:
  //! selection description
  /*!
    \todo usage of PackedBoolArray limits number of texts to 8 * sizeof(int) 
  */
  PackedBoolArray _array;

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  CCheckBoxes(ControlsContainer *parent, int idc, ParamEntryPar cls);

  virtual bool OnKeyDown(int dikCode);

  virtual bool IsSelected(int i) const;
  virtual void ChangeSelection(int i);

  //! returns selection description
  virtual PackedBoolArray GetArray() const {return _array;}
  //! sets selection description
  void SetArray(PackedBoolArray array) {_array = array;}
};

///////////////////////////////////////////////////////////////////////////////
// class CCombo

//! 2D Selector (Combo Box) control
class CCombo : public Control, public CListBoxContainer
{
protected:
  //! height of control if expanded
  float _wholeHeight;
  //! background color
  PackedColor _bgColor;
  //! used font
  Ref<Font> _font;
  //! font size
  float _size;

  //! number of rows that fits into control at once
  float _showStrings;

  //! if control is expanded
  bool _expanded;
  //! combo box expanded up instead of down?
  bool _expandedUp;
  //@{
  //! scrolling implementation
  bool _scrollUp;
  bool _scrollDown;
  UITime _scrollTime;
  //@}

  //! item over which is mouse cursor
  int _mouseSel;
  //! scroll bar used by control
  CScrollBar _scrollbar;

  //! expansion sound
  SoundPars _expandSound;
  //! collapsion sound
  SoundPars _collapseSound;

  Ref<Texture> _arrowEmpty;
  Ref<Texture> _arrowFull;

public:
  //! constructor
  /*!
    Used when control is created by resource template.
    \param parent control container by which this control is owned
    \param idc id of control
    \param cls resource template
  */
  CCombo(ControlsContainer *parent, int idc, ParamEntryPar cls);

  virtual Control *GetControl() {return this;}

  // virtual bool IsEnabled() const {return _enabled && GetSize() > 0;}

  virtual bool IsInside(float x, float y) const;
  virtual bool IsInsideExt(float x, float y) const;
  void SetPos(float x, float y, float w, float h);

  virtual bool OnKeyDown(int dikCode);
  virtual bool OnKeyUp(int dikCode);
  virtual void OnLButtonDown(float x, float y);
  virtual void OnLButtonUp(float x, float y);
  virtual void OnMouseMove(float x, float y, bool active = true);
  virtual void OnMouseHold(float x, float y, bool active = true);
  virtual void OnMouseZChanged(float dz);
  virtual void OnDraw(UIViewport *vp, float alpha);
  virtual bool OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);

  //! event handler - performed if selection is changed
  virtual void OnSelChanged(int curSel);
  virtual bool OnKillFocus(); // return false if focus cannot be killed

  //! returns default text (foreground) color for selected item
  PackedColor GetSelColor() {return _selColor;}
  //! sets default text (foreground) color for selected item
  void SetSelColor(PackedColor color) {_selColor = color;}
  //! returns default text (foreground) color
  PackedColor GetFtColor() {return _ftColor;}
  //! sets default text (foreground) color
  void SetFtColor(PackedColor color) {_ftColor = color;}
  //! returns default background color
  PackedColor GetBgColor() {return _bgColor;}
  //! sets default background color
  virtual void SetFont(Font *font) {_font = font;}
  virtual void SetFontHeight(float h) {_size = h;}
#if _VBS3 // getFontHeight
  virtual float GetFontHeight() {return _size;}
#endif
  virtual void SetBgColor(PackedColor color) {_bgColor = color;}
  virtual void SetFgColor(PackedColor color) {CListBoxContainer::SetFgColor(color);}
  virtual void SetTextColor(PackedColor color) {CListBoxContainer::SetTextColor(color);}
  virtual void SetActiveColor(PackedColor color) {CListBoxContainer::SetActiveColor(color);}

  //! sets selected row
  void SetCurSel(int sel, bool sendUpdate = true);
  //! sets selected row
  void SetCurSel(float x, float y, bool sendUpdate = true);
  void RemoveAll()
  {
    CListBoxContainer::RemoveAll();
    _expanded = false;
  }
  void DeleteString(int i)
  {
    CListBoxContainer::DeleteString(i);
    if (GetSize() == 0) _expanded = false;
  }

  float NRows() const {return _showStrings;}
  bool IsWorking() const {return _expanded;}

  virtual void Expand(bool expand);

protected:
  //! ensures that mouse selection is visible (scroll if needed)
  void SetTopString();
};

///////////////////////////////////////////////////////////////////////////////
class IControlsGroup 
{
protected:
  //! focused subcontrol
  int _indexFocused;
  int _indexDefault;
  int _indexL;
  int _indexR;
  int _indexMove;

public:
  IControlsGroup() {};

  virtual bool IsEnabled() const;

protected:
    //! sets input focus to subcontrol 
  /*!
  Called by framework if some subcontrol gains input focus.
  \param i index of subcontrol
  \param def true if only button like controls (defaultable) can gain focus
  */
  virtual void SetFocus(int i, bool def = false);
  //! focus next subcontrol
  virtual bool NextCtrl();
  //! focus previous subcontrol
  virtual bool PrevCtrl();

  virtual bool FocusCtrl(int idc);
  virtual bool FocusCtrl(IControl *ctrl);

  virtual ControlsContainer *GetControlParent() const =0;
  //! focus next subcontrol
  virtual IControl *GetControl(int index) const = 0;
  //! focus previous subcontrol
  virtual int ControlsCount() const = 0;

  //! find subcontrol on given position (<x>, <y>) in local coordinates
  virtual int FindControl(float x, float y);
};


///////////////////////////////////////////////////////////////////////////////
//! Control used as control container (group of controls)
class CControlsGroup : public Control, public IControlsGroup
{
protected:
  typedef Control base;

  //! array of subcontrols
  RefArray<Control> _controls;

  //! vertical scrollbar
  CScrollBar _vScrollbar;
  //! horizontal scrollbar
  CScrollBar _hScrollbar;

public:
  //! constructor
  /*!
  Used when control is created by resource template.
  \param parent control container by which this control is owned
  \param idc id of control
  \param cls resource template
  */
  CControlsGroup(ControlsContainer *parent, int idc, ParamEntryPar cls);

  //! focus next subcontrol
  virtual IControl *GetControl(int index) const {if(index>=0 && index < _controls.Size()) return _controls[index]; return NULL;};
  //! focus previous subcontrol
  virtual int ControlsCount() const {return _controls.Size();};
  virtual ControlsContainer *GetControlParent() const {return _parent;};

  virtual IControl *GetCtrl(int idc);
  virtual IControl *GetCtrl(float x, float y);
  virtual IControl *GetFocused();
  virtual const IControl *GetFocused() const;
  virtual bool OnSetFocus(ControlFocusAction action, bool def = false);
  virtual bool OnKillFocus();
  virtual bool CanBeDefault() const;
  const IControl *GetDefault() const;
  IControl *GetDefault();
  /// return the control in my hierarchy the key is assigned to as a shortcut
  virtual IControl *GetShortcutControl(int dikCode);

  virtual bool IsInside(float x, float y) const;

  virtual void OnLButtonDown(float x, float y);
  virtual void OnLButtonUp(float x, float y);
  virtual void OnMouseMove(float x, float y, bool active = true);
  virtual void OnMouseHold(float x, float y, bool active = true);

  virtual void OnLButtonClick(float x, float y);
  virtual void OnLButtonDblClick(float x, float y);
  virtual void OnRButtonDown(float x, float y);
  virtual void OnRButtonUp(float x, float y);
  virtual void OnRButtonClick(float x, float y);

  virtual void OnMouseZChanged(float dz);
  virtual bool OnKeyDown(int dikCode);
  virtual bool OnKeyUp(int dikCode);
  virtual bool OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
  virtual bool OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
  virtual bool OnIMEComposition(unsigned nChar, unsigned nFlags);

  virtual void DrawTooltip(UIViewport *vp, float x, float y);
  virtual int GetTooltipArea(float x, float y) override;

  virtual void OnDraw(UIViewport *vp, float alpha);

  void virtual SetScaleControl(float scaleControl);

  /// Add new control to group
  int AddControl(Control *ctrl);
  // Remove a control from the group
  bool RemoveControl(Control *ctrl);
  // Remove all controls from the group
  void RemoveAllControls();
  /// Update position of scrollbars
  void UpdateScrollbars();
  // Move the scroll bars to an absolute position
  void SetViewPos(bool isHorizontal, float pos);

  //get auto-scroll properties
  float GetAutoScrollSpeed() {return _vScrollbar.GetAutoScrollSpeed();}
  float GetAutoScrollDelay() {return _vScrollbar.GetAutoScrollDelay();}
  bool GetAutoScrollRewind() {return _vScrollbar.GetAutoScrollRewind();}
  //set auto-scroll properties
  void SetAutoScrollSpeed(float value) { _vScrollbar.SetAutoScroll(value,_vScrollbar.GetAutoScrollDelay());}
  void SetAutoScrollDelay(float value) { _vScrollbar.SetAutoScroll(_vScrollbar.GetAutoScrollSpeed(),value);}
  void SetAutoScrollRewind(bool value) { _vScrollbar.SetAutoScrollRewind(value);}

protected:
  //! Creates one owned control
  void LoadControl(ParamEntryPar ctrlCls);

};

///////////////////////////////////////////////////////////////////////////////
//! Control used as ControlObject container (group of controls)
class CObjectsGroup : public ControlObject, public IControlsGroup
{
protected:
  //! type of control
  int _type;
  //@{
  //! placement of control
  float _x;
  float _y;
  float _w;
  float _h;
  //@}

  typedef ControlObject base;

  //! array of subcontrols
  RefArray<ControlObject> _objects;

public:
  //! constructor
  /*!
  Used when control is created by resource template.
  \param parent control container by which this control is owned
  \param idc id of control
  \param cls resource template
  */
  CObjectsGroup(ControlsContainer *parent, int idc, ParamEntryPar cls);

  //! focus next subcontrol
  virtual IControl *GetControl(int index) const {if(index>=0 && index < _objects.Size()) return _objects[index]; return NULL;};
  //! focus previous subcontrol
  virtual int ControlsCount() const {return _objects.Size();};
  virtual ControlsContainer *GetControlParent() const {return _parent;};

  virtual IControl *GetCtrl(int idc);
  virtual IControl *GetCtrl(float x, float y);
  virtual IControl *GetFocused();
  virtual const IControl *GetFocused() const;
  virtual bool OnSetFocus(ControlFocusAction action, bool def = false);
  virtual bool OnKillFocus();
  virtual bool CanBeDefault() const;
  const IControl *GetDefault() const;
  IControl *GetDefault();
  /// return the control in my hierarchy the key is assigned to as a shortcut
  virtual IControl *GetShortcutControl(int dikCode);

  virtual bool IsInside(float x, float y) const;

  virtual int GetType() {return _type;}

  virtual void OnLButtonDown(float x, float y);
  virtual void OnLButtonUp(float x, float y);
  virtual void OnMouseMove(float x, float y, bool active = true);
  virtual void OnMouseHold(float x, float y, bool active = true);

  virtual void OnLButtonClick(float x, float y);
  virtual void OnLButtonDblClick(float x, float y);
  virtual void OnRButtonDown(float x, float y);
  virtual void OnRButtonUp(float x, float y);
  virtual void OnRButtonClick(float x, float y);

  virtual void OnMouseZChanged(float dz);
  virtual bool OnKeyDown(int dikCode);
  virtual bool OnKeyUp(int dikCode);
  virtual bool OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
  virtual bool OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
  virtual bool OnIMEComposition(unsigned nChar, unsigned nFlags);

  virtual void DrawTooltip(UIViewport *vp, float x, float y);
  virtual int GetTooltipArea(float x, float y) override;
  virtual void OnDrawBackGround(UIViewport *vp, float alpha);
  virtual void OnDraw(float alpha);

  /// Add new control to group
  int AddObject(ControlObject *obj);
  // Remove a control from the group
  bool RemoveObject(ControlObject *obj);
  // Remove all controls from the group
  void RemoveAllObjects();

protected:
  //! Creates one owned control
  void LoadObject(ParamEntryPar ctrlCls);
};

///////////////////////////////////////////////////////////////////////////////
// class ControlsContainer
class MsgBox;

//! Identifier of control list (layer) in ControlsContainer
/*!
  There are three lists (layers) of controls in ControlsContainer:
  - objetcs - contains control objects
  - foreground - contains 2D controls ahead of objects
  - background - contains 2D controls behind objects
*/
enum ControlList
{
  CLNone,
  CLBackground,
  CLObjects,
  CLForeground
};

//! Unique identifier of control in ControlsContainer
struct ControlId
{
  //! list identifier
  ControlList list;
  //! index of control in list
  int id;

  //! constructor
  ControlId() {list = CLNone; id = -1;}
  //! constructor
  ControlId(ControlList l, int i) {list = l; id = i;}
  //! assignment operator
  void operator =(const ControlId &src)
  {
    list = src.list;
    id = src.id;
  }
  //! equality operator
  bool operator ==(const ControlId &with) const
  {
    return with.list == list && with.id == id;
  }
  //! inequality operator
  bool operator !=(const ControlId &with) const
  {
    return with.list != list || with.id != id;
  }

  //! returns nil id (points to no control)
  static ControlId Null() {return ControlId();}
  //! returns if id is nil (points to no control)
  bool IsNull() const {return list == CLNone;}
};

/// Unique identifier of control in ControlsContainer valid over the time
class ControlIdPersistent
{
protected:
  //! list identifier
  ControlList _list;

  //! control identifier (for CLBackground or CLForeground)
  LLink<Control> _control;
  //! control identifier (for CLObjects)
  OLinkO(ControlObject) _object;

public:
  ControlIdPersistent() {_list = CLNone;}
  //! constructor
  ControlIdPersistent(ControlList list, Control *control) {_list = list; _control = control; _object = NULL;}
  //! constructor
  ControlIdPersistent(ControlList list, ControlObject *object) {_list = list; _control = NULL; _object = object;}
  //! assignment operator
  void operator =(const ControlIdPersistent &src)
  {
    _list = src._list;
    _control = src._control;
    _object = src._object;
  }
  //! return the control we are pointing at
  IControl *GetCtrl() const
  {
    switch (_list)
    {
    case CLBackground:
    case CLForeground:
      return _control;
    case CLObjects:
      return _object;
    case CLNone:
    default:
      return NULL;
    }
  }
  //! return the control we are pointing at
  Control *GetCtrl2D() const
  {
    switch (_list)
    {
    case CLBackground:
    case CLForeground:
      return _control;
    default:
      return NULL;
    }
  }
  //! return the list we are in
  ControlList GetList() const {return _list;}

  //! equality operator
  bool operator ==(const ControlIdPersistent &with) const
  {
    return with.GetCtrl() == GetCtrl();
  }
  //! inequality operator
  bool operator !=(const ControlIdPersistent &with) const
  {
    return with.GetCtrl() != GetCtrl();
  }

  //! returns nil id (points to no control)
  static ControlIdPersistent Null() {return ControlIdPersistent();}
  //! returns if id is nil (points to no control)
  bool IsNull() const {return GetCtrl() == NULL;}
};

struct CCMContext
{
};

class MenuItem;
class Menu;

#define DISPLAY_EVENT_ENUM(type,prefix,XX) \
  XX(type, prefix, Load) \
  XX(type, prefix, Unload) \
  XX(type, prefix, ChildDestroyed) \
  \
  XX(type, prefix, KeyDown) \
  XX(type, prefix, KeyUp) \
  XX(type, prefix, Char) \
  \
  XX(type, prefix, MouseButtonDown) \
  XX(type, prefix, MouseButtonUp) \
  XX(type, prefix, MouseMoving) \
  XX(type, prefix, MouseHolding) \
  XX(type, prefix, MouseZChanged) \

#ifndef DECL_ENUM_DISPLAY_EVENT
#define DECL_ENUM_DISPLAY_EVENT
DECL_ENUM(DisplayEvent)
#endif
DECLARE_ENUM(DisplayEvent, DE, DISPLAY_EVENT_ENUM)

//! Implements basic UI object (single display) that contains controls
class ControlsContainer : public RemoveLLinks
{
protected:
#if _GAMES_FOR_WINDOWS
  /// request for the container
  enum RequestType
  {
    RTResaveNone, //< default value, no resave needed
    RTResaveUserMission,  //< request to resave mission settings created by user via wizard in the main menu
    RTResaveMissionContinue, //< request to resave mission continue
    RTResaveMissionAutosave, //< request to resave mission autosave
    RTResaveCampaign, //< request to resave campaign
    RTResaveUserSave, //< request to resave user mission from in-game menu
    RTResaveUserSave2, //< request to resave user mission from in-game menu
    RTResaveUserSave3, //< request to resave user mission from in-game menu
    RTResaveUserSave4, //< request to resave user mission from in-game menu
    RTResaveUserSave5, //< request to resave user mission from in-game menu
    RTResaveUserSave6, //< request to resave user mission from in-game menu
  };

  /// set function for the static save request flag 
  static void SetSaveRequest(RequestType type) {_saveRequestType = type;}
#endif //#if _GAMES_FOR_WINDOWS

  //! unique id of container
  int _idd;
  //! parent container
  LLink<ControlsContainer> _parent;
  //! class name (when created from resource)
  RString _className;

  //@{
  //! mouse cursor
  Ref<Texture> _cursorTexture;
  float _cursorX;
  float _cursorY;
  float _cursorW;
  float _cursorH;
  RString _cursorName;
  PackedColor _cursorColor;
  int _cursorShadow;
  //@}

  //! background 2D controls layer
  RefArray<Control> _controlsBackground;
  //! object controls layer
  RefArray<ControlObject> _objects;
  //! foreground 2D controls layer
  RefArray<Control> _controlsForeground;
  //! control with input focus
  ControlIdPersistent _controlFocused;
  //! control captured with left mouse button
  ControlIdPersistent _controlLCaptured;
  //! control captured with right mouse button
  ControlIdPersistent _controlRCaptured;
  //! default control
  ControlIdPersistent _controlDefault;
  //! control with mouse cursor over
  ControlIdPersistent _controlMouseOver;

// last mouse states
  //! mouse <x> coordinate in last simulation step
  float _lastX;
  //! mouse <y> coordinate in last simulation step
  float _lastY;
  //@{
  //! double click identification
  float _dblClkX;
  float _dblClkY;
  bool _dblClkDetect;
  //@}
  //! next right mouse button release will be click
  bool _rButtonClick;
  
  //! draw display even if is not on the top of displays hierarchy
  bool _alwaysShow;
  //! display is moving by user
  bool _moving;

  //! game simulation is enabled if display is on top
  bool _enableSimulation;
  //! game display is enabled if display is on top
  bool _enableDisplay;
  //! InGameUI (HUD) is enabled if display is on top
  bool _enableUI;
  //! Function Destroy() was already called
  bool _destroyed;
  //! Hide mouse cursor during drag & drop
  bool _dragHideCursor;
  //! exit code
  int _exit;

  // Tooltip control
  UITime _tooltipTime;
  int _tooltipArea;
  float _tooltipX;
  float _tooltipY;

  //! joystick button hints
  AutoArray<KeyHint> _keyHints;

  //@{
  //! drag & drop
  InitPtr<IControl> _dragCtrl;
  CListBoxItems _dragRows;
  //! font used for drag cursor
  Ref<Font> _dragFont;
  //! font size
  float _dragSize;
  //! font color (drop possible)
  PackedColor _dragColorEnabled;
  //! font color (drop impossible)
  PackedColor _dragColorDisabled;
  //! text shadow
  int _dragShadow;
  //@}

  //@{
  //! sounds
  SoundPars _soundOK;
  SoundPars _soundFail;
  SoundPars _soundCancel;
  SoundPars _soundChangeFocus;
  SoundPars _soundChangeFocusFailed;
  SoundPars _soundWhiteProcessed;
  SoundPars _soundBlackProcessed;
  SoundPars _soundXProcessed;
  SoundPars _soundYProcessed;
  SoundPars _soundStartProcessed;
  SoundPars _soundLeftTriggerProcessed;
  SoundPars _soundRightTriggerProcessed;
  SoundPars _soundLeft;
  SoundPars _soundRight;
  SoundPars _soundUp;
  SoundPars _soundDown;
  //@}

  //! event handlers
  EventHandlersUI _eventHandlers[NDisplayEvent];

public:
  //! constructor
  /*!
    \param parent parent controls container
  */
  ControlsContainer(ControlsContainer *parent);
  //! destructor
  ~ControlsContainer();
  //! initialization
  virtual void Init();

  //! returns unique id of container
  int IDD() const {return _idd;}
  //! returns exit code of container
  int GetExitCode() const {return _exit;}
  //! sets if display is drawn even if is not on the top of displays hierarchy
  void SetAlwaysShow(bool set) {_alwaysShow = set;}
  //! returns if display is drawn even if is not on the top of displays hierarchy
  bool AlwaysShow() const {return _alwaysShow;}
  //! returns if game simulation is enabled if display is on top
  bool SimulationEnabled() const {return _enableSimulation;}
  //! returns if game display is enabled if display is on top
  bool DisplayEnabled() const {return _enableDisplay;}
  //! returns if InGameUI (HUD) is enabled if display is on top
  bool UIEnabled() const {return _enableUI;}
  // Return if keyboard events for dialogs navigation are used
  IControl *GetCtrl(int idc);
  //! returns control with given id
  const IControl *GetCtrl(int idc) const;
  //! returns control on given position
  IControl *GetCtrl(float x, float y);
  //! returns focused control
  const IControl *GetFocused() const;
  //! returns focused control
  IControl *GetFocused();
  //! returns default control
  const IControl *GetDefault() const;
  //! returns default control
  IControl *GetDefault();
  //! returns shortcuted control
  IControl *GetShortcut(int dikCode);
  //! returns parent container
  ControlsContainer *Parent() {return _parent;}
  //@{
  //! returns child container
  virtual ControlsContainer *Child() {return NULL;}
  virtual const ControlsContainer *Child() const {return NULL;}
  //@}
  //! attach child container
  virtual void CreateChild(ControlsContainer *child) {}
  //@{
  //! returns child message box
  virtual MsgBox *GetMsgBox() {return NULL;}
  virtual const MsgBox *GetMsgBox() const {return NULL;}
  //@}
  //! Set child message box
  virtual void SetMsgBox(MsgBox *msgBox) {}
  //! creates message box
  /*!
    \param text displayed text
    \param flags combination of following flags:
    - MB_BUTTON_OK - message box contains OK button
    - MB_BUTTON_CANCEL - message box contains CANCEL button
  */
  virtual void CreateMsgBox(int flags, RString text, int idd = -1, bool structuredText = false, MsgBoxButton *infoOK = NULL, MsgBoxButton *infoCancel = NULL) {}
  //! destroys child message box
  virtual void DestroyMsgBox() {}
  //! returns if display is on top of displays hierarchy
  virtual bool IsTop() const {return true;}

  //! returns name of class of mouse cursor
  RString GetCursorName() const {return _cursorName;}
  //! returns mouse cursor style
  Texture *GetCursorTexture() const {return _cursorTexture;}
  //! returns mouse cursor shadow style
  int GetCursorShadow() const {return  _cursorShadow;}
  //@{
  //! returns mouse cursor placement
  float GetCursorX() const {return _cursorX;}
  float GetCursorY() const {return _cursorY;}
  float GetCursorW() const {return _cursorW;}
  float GetCursorH() const {return _cursorH;}
  //@}
  //! returns mouse cursor color
  PackedColor GetCursorColor() const {return _cursorColor;}
  //! sets mouse cursor
  /*!
    \param name name of class in config.cpp::CfgWrapperUI::Cursors with cursor description
  */
  void SetCursor(const char *name);

  /// access to content of drag & drop
  const CListBoxItems &GetDragRows() {return _dragRows;}

  //! creates one elements of display's content from resource template
  void LoadControl(ParamEntryPar ctrlCls);

  //! creates one elements of display's content from resource template
  void LoadObject(ParamEntryPar ctrlCls);

  //! creates one elements of display's content from resource template
  void LoadControlBackground(ParamEntryPar ctrlCls);

  //! creates display's content from resource template
  void LoadFromEntry(ParamEntryPar clsEntry);
  
  //! creates display's content from resource template
  void Load(ParamEntryPar clsEntry);
  //! create display's content from resource template resource.cpp::<clsName>
  void Load(const char *clsName);
  //! creates display's content from Windows resource - not used nor maintained
  void Load(int idd);

  //! load key hints section
  void LoadHints(ParamEntryPar clsEntry);
  //! load sounds definitions
  void LoadSounds(ParamEntryPar clsEntry);

  //! set key hints directly
  void SetHints(const AutoArray<KeyHint> &hints) {_keyHints = hints;}

  //! moves focus to next control
  bool NextCtrl();
  //! moves focus to previous control
  bool PrevCtrl();
  //! moves focus to control on next line
  bool NextLineCtrl();
  //! moves focus to control on previous line
  bool PrevLineCtrl();
/*
  //! moves focus to next default control
  void NextDefaultCtrl();
  //! moves focus to previous default control
  void PrevDefaultCtrl();
  //! moves focus to default control on next line
  void NextLineDefaultCtrl();
  //! moves focus to default control on previous line
  void PrevLineDefaultCtrl();
*/
  //! returns if display can be destroyed (ask all controls)
  virtual bool CanDestroy();
  //! display destruction
  virtual void Destroy();
  //! ask for destruction of control with given exit code
  void Exit(int code)
  {
    if (_exit < 0)
      _exit = code;
  }
  //! force sets exit code
  void ForceExit(int code)
  {
    _exit = code;
  }

  //! sets focus to given control
  bool FocusCtrl(int idc);

  //! sets focus to given control
  bool FocusCtrl(IControl *ctrl);

  //! move whole display (all controls) by (<dx>, <dy>)
  void Move(float dx, float dy);

  //! event handler - performed by Load when control is to be created
  /*!
    \param type type of control
    \param idc id of control
    \param cls resource template for control
  */
  virtual Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  //! event handler - performed by Load when object control is to be created
  /*!
    \param type type of control
    \param idc id of control
    \param cls resource template for control
  */
  virtual ControlObject *OnCreateObject(int type, int idc, ParamEntryPar cls);
  //! event handler - performed when display must be drawn
  /*!
    \param vehicle with camera on
    \param alpha transparency
  */
  virtual void OnDraw(EntityAI *vehicle, float alpha);
  /// check if container is ready for drawing
  bool IsReady() const;
  //! event handler - performed when display is simulated
  virtual void OnSimulate(EntityAI *vehicle);
  //! may be called by a child to execute simulation of the parent
  virtual void OnSimulateBackground(EntityAI *vehicle);

  //! event handler - performed if key is pressed
  virtual bool OnKeyDown(int dikCode);
  //! event handler - performed if key is depressed
  virtual bool OnKeyUp(int dikCode);
  //! event handler - performed if char is entered (see Windows SDK)
  virtual bool OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
  //! event handler - performed if IME char is entered (see Windows SDK)
  virtual bool OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
  //! event handler - performed if IME composition is entered (see Windows SDK)
  virtual bool OnIMEComposition(unsigned nChar, unsigned nFlags);
  //! called by world simulation when addon is used that has not been registered
  virtual bool OnUnregisteredAddonUsed(RString addon);
  //! event handler - performed if child display is destroyed
  virtual void OnChildDestroyed(int idd, int exit) {}
  //! event handler - controller removed
  virtual void OnControllerRemoved();

  //! event handler - performed if owned button was clicked
  /*!
    \param idc id of control
  */
  virtual void OnButtonClicked(int idc);
  //! event handler - performed if owned button obtains double click event
  /*!
  \param idc id of control
  */
  virtual void OnButtonDblClick(IControl *ctrl) {}
  //! event handler - performed if owned List Box changed selected row
  /*!
  \param ctrl listbox control
  \param curSel index of new selected row
  */
  virtual void OnLBSelChanged(IControl *ctrl, int curSel) {}
  //! event handler - performed if owned List Box (ComboBox) select the same row as before
  /*!
  \param idc id of control
  \param curSel index of new selected row
  */
  virtual void OnLBSelUnchanged(int idc, int curSel) {}
  //! event handler - performed if owned List Box changed listed row
  /*!
  \param idc id of control
  \param curSel index of new list row
  */
  virtual void OnLBListSelChanged(int idc, int curSel) {}
  //! event handler - performed if owned List Box obtains double click event
  /*!
    \param idc id of control
    \param curSel index of selected row
  */
  virtual void OnLBDblClick(int idc, int curSel) {}
  //! event handler - performed if owned List Box starts drag & drop action
  /*!
    \param idc id of control
    \param curSel index of dragging row
  */
  virtual void OnLBDrag(CListBoxContainer *lbox, int sel);   //! event handler - performed control starts drag & drop action
  //! event handler - performed if item is dragging
  virtual bool OnLBDragging(IControl *ctrl, float x, float y) {return false;}
  //! event handler - performed if dragging item is released
  virtual bool OnLBDrop(IControl *ctrl, float x, float y) {return false;}


  /*!
    \param idc id of control
  */
  virtual void OnDrag(IControl *ctrl);
  //! event handler - performed if item is dragging
  virtual unsigned char OnDragging(IControl *dragCtrl,  IControl *dropCtrl, float x, float y) {return false;}
  //! event handler - performed if dragging item is released
  virtual bool OnDrop(IControl *dragCtrl, IControl *ctrl, float x, float y) {return false;}

  virtual void DrawDragInfo(float x, float y, unsigned char enabled, Ref<Font> dragFont, float dragSize) {}

  //! event handler - performed if owned Tree control is expanded or collapsed at any level
  /*!
    \param idc id of control
    \param item expanded/collapsed item
    \param expanded true if expanded false if collapsed
  */
  virtual void OnTreeItemExpanded(IControl *ctrl, CTreeItem *item, bool expanded) {}
  //! event handler - performed if owned Tree control changed selected item
  /*!
    \param idc id of control
  */
  virtual void OnTreeSelChanged(IControl *ctrl) {}
  //! event handler - performed if owned Tree control obtains double click event
  /*!
    \param ctrl tree control
    \param sel selected item
  */
  virtual void OnTreeDblClick(int idc, CTreeItem *sel) {}
  //! event handler - performed if owned Tree control obtains left button down event
  /*!
    \param ctrl tree control
    \param sel selected item
  */
  virtual void OnTreeLButtonDown(int idc, CTreeItem *sel) {}  
  //! event handler - performed if owned Tree control obtains mouse move event
  /*!
    \param ctrl tree control
    \param sel item the mouse is over
  */
  virtual void OnTreeMouseMove(int idc, CTreeItem *sel) {}  
  //! event handler - performed if owned Tree control obtains mouse hold event
  /*!
    \param ctrl tree control
    \param sel item the mouse is over
  */
  virtual void OnTreeMouseHold(int idc, CTreeItem *sel) {} 
  //! event handler - performed if owned Tree control obtains mouse exit event
  /*!
    \param ctrl tree control
    \param sel currently selected item
  */
  virtual void OnTreeMouseExit(int idc, CTreeItem *sel) {} 
  //! event handler - performed if owned Tool Box control changed selected item
  /*!
    \param idc id of control
    \param curSel index of new selected item
  */
  virtual void OnToolBoxSelChanged(int idc, int curSel) {}
  //! event handler - performed if owned Check Boxes changed selected item
  /*!
    \param idc id of control
    \param curSel index of new selected item
  */
  virtual void OnCheckBoxesSelChanged(int idc, int curSel, bool value) {}
  //! event handler - performed if owned HTML control's link is activated
  /*!
    \param idc id of control
    \param link link address (URL)
  */
  virtual bool OnHTMLLink(int idc, RString link) { return false; }
  //! event handler - performed if owned Slider changed actual value
  /*!
    \param idc id of control
    \param pos new value
  */
  virtual void OnSliderPosChanged(IControl *ctrl, float pos) {}
  //! event handler - performed if owned ControlObjectContainerAnim finished close animation
  /*!
    \param ctrl slider control
    \param pos new value
  */
  virtual void OnCtrlClosed(int idc) {}
  //! event handler - performed if owned ControlObject moves
  /*!
    \param idc id of control
  */
  virtual void OnObjectMoved(int idc, Vector3Par offset) {}
  //! event handler - performed if owned Control gained focus
  /*!
  \param idc id of control
  \param button joystick button index
  */
  virtual void OnCtrlFocused(IControl *ctrl) {}

  //! event handler - performed if text in CEdit has been changed
  /*!
  \param idc id of control
  */
  virtual void OnEditChanged(int idc) {}
  //! draws mouse cursor
  /*!
  \patch_internal 1.01 Date 07/09/2001 by Jirka - extracted from Display::DrawHUD
  */
  void DrawCursor();

  //! update hint to joystick buttons
  void UpdateKeyHints();

  //! return hint to joystick button function
  virtual RString GetKeyHint(int button, bool &structured) const;

  virtual bool EnableDefaultKeysProcessing() const {return true;}

  //! check if dialog can be destroyed and call Destroy
  bool CheckDestroy();

  //! remove context menu control
  virtual void RemoveContextMenu();

  //! create new context menu control
  bool CreateContextMenu(float x, float y, int idc, ParamEntryPar cls, CCMContext &ctx);

  /// find context menu control
  Menu *FindContextMenu() const; 

  /// Update placement based on the new content
  void ContextMenuUpdated();

  virtual bool OnMenuCreated(Menu &menu, float x, float y, int idc, CCMContext &ctx) {return false;}
  virtual void OnMenuSelected(MenuItem *item) {}

  /// set event handler
  void SetEventHandler(RString name, RString value);

  // Add a new event handler
  int AddEventHandler(RString name, RString value);

  // Add a new event handler
  void RemoveEventHandler(RString name, int id);

  // Add a new event handler
  void RemoveAllEventHandlers(RString name);

  //! process event handler - no additional arguments
  void OnEvent(DisplayEvent event);

  //! process event handler
  void OnEvent(DisplayEvent event, float par);

  //! process event handler
  void OnEvent(DisplayEvent event, ControlsContainer *par1, float par2);

  //! process event handler
  bool OnEvent(DisplayEvent event, float par1, bool par2, bool par3, bool par4);

  //! process event handler
  void OnEvent(DisplayEvent event, float par1, float par2);

  //! process event handler
  void OnEvent(DisplayEvent event, float par1, float par2, float par3, bool par4, bool par5, bool par6);

// implementation
protected:
#if _GAMES_FOR_WINDOWS
    /// type of the save request
    static RequestType _saveRequestType;

    /// handle return to main menu request
    virtual void OnReturnToMainMenuRequest();
    /// handle active profile change
    virtual void OnCheckInChanged();
    /// handle change in sign in status of any player on local console (active or inactive)
    virtual void OnSomeSignInChanged();
    /// handle invitation
    virtual void OnInvited();
    /// handle storage device change
    virtual void OnStorageDeviceChanged();
    /// handle storage device removal
    virtual void OnStorageDeviceRemoved();
    /// handles when current user is disconnected from Live
    virtual void OnUserDisconnectedFromLive();
    /// handles request
    virtual void HandleRequest(RequestType request);
    /// returns if this or parent of this is an user mission editor wizard
    virtual bool IsInUserMissionWizard() const;
    /// handles saves change
    virtual void OnSavesChanged();
#endif

private:
  //! implements change of input focus
  /*!
    \param id id of control gains input focus
    \param up true if focus is gaines as result of Next Control action (TAB)
    \param def true if only button like controls (defaultable) can gain focus
    \return false if focus cannot be gained
  */
  bool SetFocus(const ControlId &id, ControlFocusAction action, bool def = false);
  //! returns control with given identifier
  const IControl *Ctrl(const ControlId &id) const;
  //! returns control with given identifier
  IControl *Ctrl(const ControlId &id);
  //! find next control (in TAB order)
  ControlId Next(ControlId &id);
  //! find previous control (in TAB order)
  ControlId Prev(ControlId &id);
  //! sorts object controls by z coordinate
  void SortObjects();
  //! update container when control was removed
  void ControlRemoved(const IControl *ctrl);
  /// find the topmost control on given position
  ControlIdPersistent FindCtrl(float x, float y);
  /// convert persistent control id to temporary
  ControlId FindCtrl(const ControlIdPersistent &control);
  /// handle left mouse button pressed
  void OnLButtonDown(IControl *ctrlFound, ControlIdPersistent controlFound, float mouseX, float mouseY, bool shift, bool control, bool alt, bool handleDblClk);
  /// handle left mouse button released
  void OnLButtonUp(IControl *ctrlFound, float mouseX, float mouseY, bool shift, bool control, bool alt);
};

///////////////////////////////////////////////////////////////////////////////
// class MsgBox

/// description of message box button
struct MsgBoxButton
{
  RString _text;
  int _shortcut;

  MsgBoxButton(RString text, int shortcut) : _text(text), _shortcut(shortcut) {}
};

//! message box class
class MsgBox : public ControlsContainer
{
  //! displayed text
  RString _text;
  //! buttons mask
  int _flags;

public:
  //! constructor
  /*!
    \param parent parent container (display)
    \param flags see (ControlsContainer::CreateMsgBox)
    \param text text to be displayed
    \param idd id of created container
  */
  MsgBox(ControlsContainer *parent, int flags, RString text, int idd, bool structuredText = false, MsgBoxButton *infoOK = NULL, MsgBoxButton *infoCancel = NULL, MsgBoxButton *infoUser=NULL);

  void OnButtonClicked(int idc);
  bool OnKeyDown(int dikCode);  
};

#if _USE_BATTL_EYE_SERVER
class BattlEyeLicenceMsgBox : public MsgBox
{
  RString reconnectGUID;
  RString reconnectPassword;
public:
  BattlEyeLicenceMsgBox(ControlsContainer *parent, RString text, int idd, bool structuredText, MsgBoxButton *infoOK, MsgBoxButton *infoCancel, MsgBoxButton *infoLicence, RString reconnectGUID, RString reconnectPassword);
  void OnButtonClicked(int idc);
  RString GetReconnectGUID() const {return reconnectGUID;}
  RString GetReconnectPassword() const {return reconnectPassword;}
};
#endif

#if _GAMES_FOR_WINDOWS

// Special type of message box which will not close because of invitation
class MsgBoxIgnoreInvitation : public MsgBox
{
public:
  MsgBoxIgnoreInvitation(ControlsContainer *parent, int flags, RString text, int idd, bool structuredText = false, MsgBoxButton *infoOK = NULL, MsgBoxButton *infoCancel = NULL)
    : MsgBox(parent, flags, text, idd, structuredText, infoOK, infoCancel) {}

  virtual void OnInvited() {}
};

#endif // defined _XBOX && _XBOX_VER >= 200

///////////////////////////////////////////////////////////////////////////////
// class Display

//! Display class
/*!
  Adds implementation of displays hierarchy to ControlContainer.
  Implements AbstractOptionsUI interface.
*/
class Display : public ControlsContainer, public AbstractOptionsUI
{
protected:
  //! child container (display)
  Ref<ControlsContainer> _child;
  //! child message box
  Ref<MsgBox> _msgBox;
  //! child progress indicator
  IProgressDisplayRef _progressDisplay;
  //! when the progress will be shown (avoid very short progresses)
  DWORD _progressStartTime;

public:
  //! constructor
  /*!
    \param parent parent container (display)
  */
  Display(ControlsContainer *parent);
  //! destructor
  ~Display();

  ControlsContainer *Child() {return _child;}
  const ControlsContainer *Child() const {return _child;}
  void CreateChild(ControlsContainer *child) {_child = child;}
  virtual void OnChildDestroyed(int idd, int exit);
  //! returns if display is on top of displays hierarchy
  bool IsTopmost() const {return _child == NULL && _msgBox == NULL;}
  bool IsTop() const {return _child == NULL && _msgBox == NULL;}
  int GetTopID() const;
  virtual int GetIDD() const {return _idd;}

  //! called by framework when displays hierarchy must be drawn
  /*!
    \param vehicle with camera on
    \param alpha transparency
  */
  void DrawHUD(EntityAI *vehicle, float alpha);
  //! called by framework when displays hierarchy is simulated
  void SimulateHUD(EntityAI *vehicle);
  //! called by framework if key is pressed
  bool DoKeyDown(int dikCode);
  //! called by framework if key is depressed
  bool DoKeyUp(int dikCode);
  //! called by framework if char is entered (see Windows SDK)
  bool DoChar( unsigned nChar, unsigned nRepCnt, unsigned nFlags );
  //! called by framework if IME char is entered (see Windows SDK)
  bool DoIMEChar( unsigned nChar, unsigned nRepCnt, unsigned nFlags );
  //! called by framework if IME composition is entered (see Windows SDK)
  bool DoIMEComposition( unsigned nChar, unsigned nFlags );
  //! called by world simulation when addon is used that has not been registered
  bool DoUnregisteredAddonUsed( RString addon );

  MsgBox *GetMsgBox() {return _msgBox;}
  const MsgBox *GetMsgBox() const {return _msgBox;}
  void SetMsgBox(MsgBox *msgBox) {_msgBox = msgBox;}
  void CreateMsgBox(int flags, RString text, int idd = -1, bool structuredText = false, MsgBoxButton *infoOK = NULL, MsgBoxButton *infoCancel = NULL)
  {_msgBox = new MsgBox(this, flags, text, idd, structuredText, infoOK, infoCancel);}
  void DestroyMsgBox()
  {_msgBox = NULL;}
  //! returns if game simulation is enabled
  bool IsSimulationEnabled() const;
  //! returns if game display is enabled
  bool IsDisplayEnabled() const;
  //! returns if InGameUI (HUD) is enabled
  bool IsUIEnabled() const;

  bool IsDisplayReady() const;

  void OnSimulate(EntityAI *vehicle);
  void OnDraw(EntityAI *vehicle, float alpha);

  void StartProgress(RString msg, DWORD delay = PROGRESS_DEFAULT_DELAY);
  void FinishProgress();

protected:
#if _GAMES_FOR_WINDOWS
  /// handle storage device removal
  virtual void OnStorageDeviceRemoved();
#endif

};

ITextContainer *GetTextContainer(IControl *ctrl);
CEditContainer *GetEditContainer(IControl *ctrl);
CSliderContainer *GetSliderContainer(IControl *ctrl);
CListBoxContainer *GetListBoxContainer(IControl *ctrl);
CHTMLContainer *GetHTMLContainer(IControl *ctrl);
CStructuredText *GetStructuredText(IControl *ctrl);

#endif

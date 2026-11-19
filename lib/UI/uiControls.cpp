#pragma warning( disable : 4530 )

// Implementation of control hierarchy
#include "../wpch.hpp"
#include "uiControls.hpp"
#include "../gameDirs.hpp"
#include "../keyInput.hpp"
#include "../engine.hpp"
#include "../world.hpp"
#include "../dikCodes.h"
#include "../vkCodes.h"
#include "../autoComplete.hpp"
#include "../fileLocator.hpp"

#include <Es/Common/win.h>

#include "resincl.hpp"
#include "../paramArchiveExt.hpp"

#include "../camera.hpp"
#include "../txtPreload.hpp"

#include <Es/Algorithms/qsort.hpp>

#include "../mbcs.hpp"

#include <El/Clipboard/clipboard.hpp>
#include <El/Evaluator/express.hpp>

#include "../drawText.hpp"
#include "../stringtableExt.hpp"

#include "../diagModes.hpp"

#include "../gameStateExt.hpp"

#include "../joystick.hpp"

#include "uiViewport.hpp"

#include "../oggtext.h"

///////////////////////////////////////////////////////////////////////////////
//
//   Implementation
//
///////////////////////////////////////////////////////////////////////////////


#if _ENABLE_GSPCONTROLLED_SERVER == 0
	#define ENABLE_CLIPBOARD
#endif


/*!
\file
Implementation file for basic controls.
*/

RString FindPicture(RString name);

static inline bool ButtonPressed(int dikCode)
{
  return dikCode == DIK_SPACE || dikCode == DIK_RETURN || dikCode == DIK_NUMPADENTER ||
    dikCode == INPUT_DEVICE_XINPUT + XBOX_A || dikCode == INPUT_DEVICE_XINPUT + XBOX_Start;
}

// for listbox and combobox
#define SCROLL_SPEED    100.0
#define SCROLL_MIN      2.0
#define SCROLL_MAX      10.0

#define ISSPACE(c) ((c) >= 0 && (c) <= 32)

#define DrawBottom(i, color) vp->DrawLine(Line2DPixel(xx + i, yy + hh - 1 - i, xx + ww - i, yy + hh - 1 - i), color, color);
#define DrawRight(i, color) vp->DrawLine(Line2DPixel(xx + ww - 1 - i, yy + hh - 1 - i, xx + ww - 1 - i, yy + 0 + i), color, color);
#define DrawLeft(i, color) vp->DrawLine(Line2DPixel(xx + i, yy + hh - 1 - i, xx + i, yy + i), color, color);
#define DrawTop(i, color) vp->DrawLine(Line2DPixel(xx + i, yy + i, xx + ww - 1 - i, yy + i), color, color);

#define DrawBottomNoClip(i, color) vp->DrawLineNoClip(Line2DPixel(xx + i, yy + hh - 1 - i, xx + ww - i, yy + hh - 1 - i), color, color);
#define DrawRightNoClip(i, color) vp->DrawLineNoClip(Line2DPixel(xx + ww - 1 - i, yy + hh - 1 - i, xx + ww - 1 - i, yy + 0 + i), color, color);
#define DrawLeftNoClip(i, color) vp->DrawLineNoClip(Line2DPixel(xx + i, yy + hh - 1 - i, xx + i, yy + i), color, color);
#define DrawTopNoClip(i, color) vp->DrawLineNoClip(Line2DPixel(xx + i, yy + i, xx + ww - 1 - i, yy + i), color, color);

extern const float CameraZoom = 2.0;
extern const float InvCameraZoom = 1.0 / CameraZoom;

ITextContainer *GetTextContainer(IControl *ctrl)
{
  if (!ctrl) return NULL;
  switch (ctrl->GetType())
  {
  case CT_STATIC:
    return static_cast<CStatic *>(ctrl);
  case CT_BUTTON:
    return static_cast<CButton *>(ctrl);
  case CT_ACTIVETEXT:
    return static_cast<CActiveText *>(ctrl);
  case CT_STRUCTURED_TEXT:
  case CT_XKEYDESC:
  case CT_XBUTTON:
  case CT_SHORTCUTBUTTON:
    return static_cast<CStructuredText *>(ctrl);
  case CT_EDIT:
    return static_cast<CEdit *>(ctrl);
  }
  Fail("Unexpected control type");
  return NULL;
}

CEditContainer *GetEditContainer(IControl *ctrl)
{
  if (!ctrl) return NULL;
  switch (ctrl->GetType())
  {
  case CT_EDIT:
    return static_cast<CEdit *>(ctrl);
  }
  Fail("Unexpected control type");
  return NULL;
}

CSliderContainer *GetSliderContainer(IControl *ctrl)
{
  if (!ctrl) return NULL;
  switch (ctrl->GetType())
  {
  case CT_SLIDER:
    return static_cast<CSlider *>(ctrl);
  case CT_XSLIDER:
    return static_cast<CXSlider *>(ctrl);
  }
  Fail("Unexpected control type");
  return NULL;
}

CListBoxContainer *GetListBoxContainer(IControl *ctrl)
{
  if (!ctrl) return NULL;
  switch (ctrl->GetType())
  {
  case CT_LISTBOX:
    return static_cast<CListBox *>(ctrl);
  case CT_XLISTBOX:
    return static_cast<CXListBox *>(ctrl);
  case CT_COMBO:
    return static_cast<CCombo *>(ctrl);
  case CT_XCOMBO:
    return static_cast<CXCombo *>(ctrl);
  case CT_LISTNBOX:
    return static_cast<CListNBox *>(ctrl);
  }
  Fail("Unexpected control type");
  return NULL;
}

CHTMLContainer *GetHTMLContainer(IControl *ctrl)
{
  if (!ctrl) return NULL;
  switch (ctrl->GetType())
  {
  case CT_HTML:
    return static_cast<CHTML *>(ctrl);
  }
  Fail("Unexpected control type");
  return NULL;
}

CStructuredText *GetStructuredText(IControl *ctrl)
{
  if (!ctrl) return NULL;
  switch (ctrl->GetType())
  {
  case CT_STRUCTURED_TEXT:
  case CT_XKEYDESC:
  case CT_XBUTTON:
  case CT_SHORTCUTBUTTON:
    return static_cast<CStructuredText *>(ctrl);
  }
  return NULL;
}

inline PackedColor ModAlpha( PackedColor color, float alpha )
{
  int a=toInt(alpha*color.A8());
  saturate(a,0,255);
  return PackedColorRGB(color,a);
}

bool EventHandlerUI::Process(const GameValue &pars)
{
  GameVarSpace local(false);
  if (!GWorld) return false;
  GameState *gstate = GWorld->GetGameState();
  int gshandle = gstate->BeginContext(&local, true); // it is run as a result of some UI Control action
  gstate->VarSetLocal("_this", pars, true);
  GameValue result = gstate->EvaluateMultiple(_handler, GameState::EvalContext::_default, UINamespace()); // UI namespace
  gstate->EndContext(gshandle);
  
  return (result.GetType() & GameBool) && (bool)result;
}

bool EventHandlerUI::ProcessBool(const GameValue &pars)
{
  GameVarSpace local(false);
  if (!GWorld) return false;
  GameState *gstate = GWorld->GetGameState();
  int gshandle = gstate->BeginContext(&local, true); // it is run as a result of some UI Control action
  gstate->VarSetLocal("_this", pars, true);
  bool result = gstate->EvaluateMultipleBool(_handler, GameState::EvalContext::_default, UINamespace()); // UI namespace
  gstate->EndContext(gshandle);
  return result;
}

int EventHandlersUI::Add(RString expression)
{
  int id = _nextId++;
  _handlers.Add(EventHandlerUI(id, expression));
  return id;
}

void EventHandlersUI::Delete(int id)
{
  for (int i=0; i<_handlers.Size(); i++)
  {
    if (_handlers[i].GetId() == id)
    {
      _handlers.Delete(i);
      return;
    }
  }
}

void EventHandlersUI::Clear()
{
  _handlers.Clear();
}

bool EventHandlersUI::Process(const GameValue &pars)
{
  for (int i=_handlers.Size()-1; i>=0; i--)
  {
    if (_handlers[i].Process(pars)) return true; // handled
  }
  return false;
}

bool EventHandlersUI::ProcessBool(const GameValue &pars)
{
  for (int i=_handlers.Size()-1; i>=0; i--)
  {
    if (_handlers[i].ProcessBool(pars)) return true; // handled
  }
  return false;
}

bool EventHandlersUI::ProcessFalse(const GameValue &pars)
{
  for (int i=_handlers.Size()-1; i>=0; i--)
  {
    if (!_handlers[i].ProcessBool(pars)) return false; // failed
  }
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// classes IControl, Control

DEFINE_ENUM(ControlEvent, CE, CONTROL_EVENT_ENUM)

#define ENUM_LOAD(type, prefix, name) \
  entry = cls.FindEntry("on"#name); \
  if (entry) \
  { \
    RString value = *entry; \
    _eventHandlers[prefix##name].Clear(); \
    if (value.GetLength() > 0) _eventHandlers[prefix##name].Add(value); \
  }

IControl::IControl(ControlsContainer *parent, int idc)
{
  _parent = parent;
  _idc = idc;

  _visible = true;
  _enabled = true;
  _focused = false;
  _default = false;
  _mouseOver = false;
  _movingDisplay = false;
  _className = ""; //no class

  _blinkingSpeed = 0;
  _shadow = 0;
}

Ref<Font> IControl::_tooltipFont;
float IControl::_tooltipSize;
int IControl::_tooltipShadow;

IControl::IControl(ControlsContainer *parent, int idc, ParamEntryPar cls)
{
  _parent = parent;
  _idc = idc;

  _visible = true;
  _enabled = true;
  _focused = false;
  _default = false;
  _mouseOver = false;
  _movingDisplay = false;
  _className = cls.GetName();

  _blinkingSpeed = 0;
  ConstParamEntryPtr entry = cls.FindEntry("blinkingPeriod");
  if (entry)
  {
    float blinkingPeriod = *entry;
    if (blinkingPeriod > 0) _blinkingSpeed = 1.0f / blinkingPeriod;
  }

  if(cls.FindEntry("shadow"))
    _shadow = cls>>"shadow";
  else _shadow = 0;

  entry = cls.FindEntry("default");
  if (entry) _default = *entry;

  entry = cls.FindEntry("tooltip");
  if (entry) _tooltip = *entry;

  if (!_tooltipFont)
  {
    ParamEntryVal fontPars = Pars >> "CfgInGameUI" >> "TooltipFont";
    _tooltipFont = GEngine->LoadFont(GetFontID(fontPars >> "font"));
    _tooltipSize = fontPars >> "size";
    _tooltipShadow = fontPars >> "shadow";
  }

  _tooltipColorText = PackedWhite;
  entry = cls.FindEntry("tooltipColorText");
  if (entry) GetValue(_tooltipColorText, *entry);

  _tooltipColorBox = PackedColor(Color(1, 1, 1, 0.3));
  entry = cls.FindEntry("tooltipColorBox");
  if (entry) GetValue(_tooltipColorBox, *entry);
  
  _tooltipColorShade = PackedColor(Color(0, 0, 0, 0.3));
  entry = cls.FindEntry("tooltipColorShade");
  if (entry) GetValue(_tooltipColorShade, *entry);

  entry = cls.FindEntry("shortcuts");
  if (entry)
  {
    for (int i=0; i<entry->GetSize(); i++)
    {
      _shortcuts.AddUnique((*entry)[i]);
    }
  }

  ConstParamEntryPtr hints = cls.FindEntry("KeyHints");
  if (hints)
  {
    int n = hints->GetEntryCount();
    _keyHints.Realloc(n);
    _keyHints.Resize(n);
    for (int i=0; i<n; i++)
    {
      ParamEntryVal entry = hints->GetEntry(i);
      _keyHints[i].key = entry >> "key";
      _keyHints[i].hint = entry >> "hint";
    }
  }

  CONTROL_EVENT_ENUM(ControlEvent, CE, ENUM_LOAD)
}

void IControl::ShowCtrl(bool show)
{
  _visible = show;
#if !_VBS2  // disable in VBS2 for now (causes control draw order to change in complex dialogs?) TODO: investigate
  if (!show && _focused && _parent) _parent->NextCtrl();
#endif
}

void IControl::EnableCtrl(bool enable)
{
  _enabled = enable;
  if (enable)
  {
    if (_parent && !_parent->GetFocused())
      _parent->NextCtrl(); // try to focus somethink
  }
  else
  {
    if (_focused && _parent)
      _parent->NextCtrl();
  }
}

const IControl *IControl::GetDefault() const
{
  if (CanBeDefault() && _default)
    return this;
  return NULL;
}

IControl *IControl::GetDefault()
{
  if (CanBeDefault() && _default)
    return this;
  return NULL;
}

float IControl::GetAlpha() const
{
  if (_blinkingSpeed == 0) return 1.0f;
  float time = Glob.uiTime.toFloat() * _blinkingSpeed;
  float phase = 2 * H_PI * (time - toIntFloor(time));
  return 0.5 * (1 + sin(phase));
}

Control::Control(ControlsContainer *parent, int type, int idc, int style, float x, float y, float w, float h)
  : IControl(parent, idc)
{
  _parent = parent;
  _type = type;
  _style = style;
  _x = x;
  _y = y;
  _w = w;
  _h = h;
  _timer = UITIME_MAX;
  _scale = 1.0f;
  _alpha = 1.0f;
  _scaleControl = 1.0;  //default scale is 1.0 - original size

  // all committed
  _timeCommitted = UITIME_MIN;

  _movingDisplay = _type == CT_STATIC && (_style & ST_TYPE) == ST_TITLE_BAR;
}

Control::Control(ControlsContainer *parent, int type, int idc, ParamEntryPar cls)
  : IControl(parent, idc, cls)
{
  _parent = parent;
  _type = type;
  _timer = UITIME_MAX;
  _scale = 1.0f;
  _alpha = 1.0f;
  _scaleControl = 1.0;   //default scale is 1.0 - original size

  _style = cls>>"style";
  _x = cls>>"x";
  _y = cls>>"y";
  _w = cls>>"w";
  _h = cls>>"h";

  _movingDisplay = _type == CT_STATIC && (_style & ST_TYPE) == ST_TITLE_BAR;
  ConstParamEntryPtr entry = cls.FindEntry("moving");
  if (entry) _movingDisplay = *entry;

  // all commited
  _timeCommitted = UITIME_MIN;
}

GameValue Control::CreateGameValue()
{
  return GameValueExt(this);
}

float Control::GetAlpha() const
{
  return _alpha * IControl::GetAlpha();
}

void Control::Move(float dx, float dy)
{
  _x += dx;
  _y += dy;
}

void Control::AnimCommit(float time)
{
  if (_alphaAnim._set) _alphaAnim.Commit(time, _alpha);
  if (_scaleControlAnim._set) _scaleControlAnim.Commit(time, _scaleControl);
  if (_posAnim._set) _posAnim.Commit(time, ControlPos(_x, _y, _w, _h));

  UITime end = Glob.uiTime + time;
  if (end > _timeCommitted ) _timeCommitted = end;

  if (time <= 0) Animate();
}

void Control::Animate()
{
  if (_alphaAnim._animated) _alphaAnim.Animate(_alpha);
  if (_scaleControlAnim._animated ) _scaleControlAnim.Animate(_scaleControl);
  if (_posAnim._animated)
  {
    ControlPos pos;
    _posAnim.Animate(pos);
#if !_VBS3
    _x = pos._x; _y = pos._y; _w = pos._w; _h = pos._h;
#else
    SetPos(pos._x,pos._y,pos._w,pos._h);
#endif
  }
}


DrawCoord SceneToScreen(Vector3Par pos);

void IControl::PlaySound(SoundPars &pars)
{
  if( pars.name.GetLength()>0 )
  {
    //float volume = GSoundsys->GetSpeechVolCoef();
    AbstractWave *wave = GSoundScene->OpenAndPlayOnce2D(pars.name, pars.vol, pars.freq, false);
    if( wave )
    {
      wave->SetKind(WaveMusic); // UI sounds considered music???
      wave->SetSticky(true);
      GSoundScene->AddSound(wave);
    }
  }
  bool IsAppPaused();
  GSoundScene->AdvanceAll(0, !GWorld->IsSimulationEnabled(), IsAppPaused());
  GSoundsys->Commit();
}

void IControl::SetEventHandler(RString name, RString value)
{
  ControlEvent event = GetEnumValue<ControlEvent>((const char *)name);
  if (event != INT_MIN)
  {
    _eventHandlers[event].Clear();
    if (value.GetLength() > 0) _eventHandlers[event].Add(value);
  }
}

int IControl::AddEventHandler(RString name, RString value)
{
  ControlEvent event = GetEnumValue<ControlEvent>((const char *)name);
  if (event == INT_MIN) return -1;
  return _eventHandlers[event].Add(value);
}

void IControl::RemoveEventHandler(RString name, int id)
{
  ControlEvent event = GetEnumValue<ControlEvent>((const char *)name);
  if (event != INT_MIN) _eventHandlers[event].Delete(id);
}

void IControl::RemoveAllEventHandlers(RString name)
{
  ControlEvent event = GetEnumValue<ControlEvent>((const char *)name);
  if (event != INT_MIN) _eventHandlers[event].Clear();
}

void IControl::OnEvent(ControlEvent event)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(CreateGameValue());
  handlers.Process(value);
}

/*!
\patch 5148 Date 3/26/2007 by Jirka
- New: UI - control event handler ButtonClick now can return true to avoid default reaction
*/

bool IControl::OnCheckEvent(ControlEvent event)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return false;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(CreateGameValue());

  // keep the backward compatibility - do not require bool, but return it when given
  return handlers.Process(value);
}

void IControl::OnEvent(ControlEvent event, float par)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(CreateGameValue());
  arguments.Add(GameValue(par));
  handlers.Process(value);
}

bool IControl::OnCheckEventFalse(ControlEvent event, float par)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return true;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(CreateGameValue());
  arguments.Add(GameValue(par));
  return handlers.ProcessFalse(value);
}

void IControl::OnEvent(ControlEvent event, RString par)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(CreateGameValue());
  arguments.Add(GameValue(par));
  handlers.Process(value);
}

void IControl::OnEvent(ControlEvent event, Vector3Par par)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(CreateGameValue());
  arguments.Add(GameValue(par.X()));
  arguments.Add(GameValue(par.Y()));
  arguments.Add(GameValue(par.Z()));
  handlers.Process(value);
}

void IControl::OnEvent(ControlEvent event, float par1, bool par2, bool par3, bool par4)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(CreateGameValue());
  arguments.Add(GameValue(par1));
  arguments.Add(GameValue(par2));
  arguments.Add(GameValue(par3));
  arguments.Add(GameValue(par4));
  handlers.Process(value);
}

bool IControl::OnCheckEvent(ControlEvent event, float par1, bool par2, bool par3, bool par4)
{//new event handler, so key down can be 
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return false;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(CreateGameValue());
  arguments.Add(GameValue(par1));
  arguments.Add(GameValue(par2));
  arguments.Add(GameValue(par3));
  arguments.Add(GameValue(par4));

  // keep the backward compatibility - do not require bool, but return it when given
  return handlers.Process(value);
}


void IControl::OnEvent(ControlEvent event, float par1, float par2)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(CreateGameValue());
  arguments.Add(GameValue(par1));
  arguments.Add(GameValue(par2));
  handlers.Process(value);
}

void IControl::OnEvent(ControlEvent event, float par1, float par2, bool par3)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(CreateGameValue());
  arguments.Add(GameValue(par1));
  arguments.Add(GameValue(par2));
  arguments.Add(GameValue(par3));
  handlers.Process(value);
}

void IControl::OnEvent(ControlEvent event, float par1, float par2, float par3, bool par4, bool par5, bool par6)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(CreateGameValue());
  arguments.Add(GameValue(par1));
  arguments.Add(GameValue(par2));
  arguments.Add(GameValue(par3));
  arguments.Add(GameValue(par4));
  arguments.Add(GameValue(par5));
  arguments.Add(GameValue(par6));
  handlers.Process(value);
}

void IControl::OnEvent(ControlEvent event, const CListBoxItems &items)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return;

  GameValue value0 = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value0;
  arguments.Add(CreateGameValue());
  GameValue value1 = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &array = value1;
  arguments.Add(GameValue(value1));
  for (int i=0; i<items.Size(); i++)
  {
    const CListBoxItem &item = items[i];
    GameValue value2 = GWorld->GetGameState()->CreateGameValue(GameArray);
    GameArrayType &entry = value2;
    array.Add(GameValue(value2));

    entry.Add(GameValue(item.text));
    entry.Add(GameValue((float)item.value));
    entry.Add(GameValue(item.data));
    entry.Compact();
  }
  array.Compact();
  arguments.Compact();

  handlers.Process(value0);
}

bool IControl::OnCheckEvent(ControlEvent event, float x, float y, int sourceIDC, const CListBoxItems &items
#if _VBS3 //add the index the items are dropped onto
                            , int destIndex
#endif
                            )
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return false;

  GameValue value0 = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value0;
  arguments.Add(CreateGameValue());
  arguments.Add(GameValue(x));
  arguments.Add(GameValue(y));
  arguments.Add(GameValue((float)sourceIDC));
  GameValue value1 = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &array = value1;
  arguments.Add(GameValue(value1));
  for (int i=0; i<items.Size(); i++)
  {
    const CListBoxItem &item = items[i];
    GameValue value2 = GWorld->GetGameState()->CreateGameValue(GameArray);
    GameArrayType &entry = value2;
    array.Add(GameValue(value2));

    entry.Add(GameValue(item.text));
    entry.Add(GameValue((float)item.value));
    entry.Add(GameValue(item.data));
#if _VBS3 //add the old LB index
    entry.Add(GameValue((float)item.oldIndex));
#endif
    entry.Compact();
  }
  array.Compact();
#if _VBS3 //add the index the items are dropped onto
  arguments.Add((float)destIndex);
#endif
  arguments.Compact();

  return handlers.ProcessBool(value0);
}

void IControl::SetShortcut(int shortcut)
{
  if (shortcut == 0)
  {
    _shortcuts.Clear();
  }
  else
  {
    _shortcuts.Realloc(1);
    _shortcuts.Resize(1);
    _shortcuts[0] = shortcut;
  }
  UpdateShortcuts();
}

void IControl::SetShortcuts(const int *shortcuts, int count)
{
  _shortcuts.Copy(shortcuts, count);
  UpdateShortcuts();
}

/// return the control in my hierarchy the key is assigned to as a shortcut
IControl *IControl::GetShortcutControl(int dikCode)
{
  return _visible && _enabled && _shortcuts.Find(dikCode) >= 0 ? this : NULL;
}

bool IControl::IsShortcutDevice(int device) const
{
  for (int i=0; i<_shortcuts.Size(); i++)
  {
    if ((_shortcuts[i] & INPUT_DEVICE_MASK) == device) return true;
  }
  return false;
}

bool IControl::OnlyComboShortcuts() const
{ // true when all shortcuts are Keyboard with some Shift/Control/Alt combination
  for (int i=0; i<_shortcuts.Size(); i++)
  {
    if (  (_shortcuts[i] & INPUT_DEVICE_MASK) != INPUT_DEVICE_KEYBOARD ||
          (_shortcuts[i]&0x700)==0 ) return false; 
  }
  return _shortcuts.Size()>0;
}

bool IControl::OnShortcutDown()
{
  // not handled by default
  return false;
}

bool IControl::OnShortcutUp()
{
  // not handled by default
  return false;
}

bool IControl::OnSetFocus(ControlFocusAction action, bool def)
{
  _focused = true;
  return true;
}

bool IControl::OnKillFocus()
{
  _focused = false;
  OnEvent(CEKillFocus);
  return true;
}

void IControl::OnTimer()
{
  _timer = UITIME_MAX;
}

IControl *IControl::GetCtrl(int idc)
{
  if (idc == _idc) return this;
  return NULL;
}

IControl *IControl::GetCtrl(float x, float y)
{
  return this;
}

bool IControl::OnKeyDown(int dikCode)
{
  return false;
}

bool IControl::OnKeyUp(int dikCode)
{
  return false;
}

bool IControl::OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  return false;
}

bool IControl::OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  return false;
}

bool IControl::OnIMEComposition(unsigned nChar, unsigned nFlags)
{
  return false;
}

void IControl::OnLButtonDown(float x, float y)
{
}

void IControl::OnLButtonUp(float x, float y)
{
}

void IControl::OnLButtonClick(float x, float y)
{
}

void IControl::OnLButtonDblClick(float x, float y)
{
}

void IControl::OnRButtonDown(float x, float y)
{
}

void IControl::OnRButtonUp(float x, float y)
{
}

void IControl::OnRButtonClick(float x, float y)
{
}

void IControl::OnMouseMove(float x, float y, bool active)
{
  bool mouseOver = active && IsInside(x, y);
  if (mouseOver && !_mouseOver)
  {
    OnEvent(CEMouseEnter);
    OnMouseEnter(x, y);
  }
  else if (_mouseOver && !mouseOver)
  {
    OnEvent(CEMouseExit);
    OnMouseExit(x, y);
  }
  _mouseOver = mouseOver;
}

void IControl::OnMouseHold(float x, float y, bool active)
{
  bool mouseOver = active && IsInside(x, y);
  if (mouseOver && !_mouseOver)
  {
    OnEvent(CEMouseEnter);
    OnMouseEnter(x, y);
  }
  else if (_mouseOver && !mouseOver)
  {
    OnEvent(CEMouseExit);
    OnMouseExit(x, y);
  }
  _mouseOver = mouseOver;
}

void IControl::OnMouseEnter(float x, float y)
{
}

void IControl::OnMouseExit(float x, float y)
{
}

void IControl::OnMouseZChanged(float dz)
{
}

bool IControl::OnCanDestroy(int code)
{
  return true;
}

void IControl::OnDestroy(int code)
{
}

/*!
\patch 1.50 Date 4/11/2002 by Jirka
- Added: tooltips for UI controls 
\patch 5136 Date 2/28/2007 by Jirka
- Fixed: UI - Tooltips are kept is screen safe area, not in the UI area
*/

void IControl::DrawTooltip(UIViewport *vp, float x, float y)
{
  if (_tooltip.GetLength() > 0)
  {
    int w = GEngine->Width2D();
    int h = GEngine->Height2D();

    const float border = TEXT_BORDER;
    float width = GEngine->GetTextWidth(_tooltipSize, _tooltipFont, _tooltip);

    float bx = x + 0.02;
//    float by = y - 0.5 * height - border;
    float by = y + 0.02;
    float ex = bx + width + 2.0 * border;
    float ey = by + _tooltipSize + 2.0 * border;

    // keep in the safe area
    AspectSettings as;
    GEngine->GetAspectSettings(as);

    float safeLeft, safeTop, safeRight, safeBottom;
    as.GetUISafeArea(safeLeft, safeTop, safeRight, safeBottom);

    if (bx < safeLeft)
    {
      float offset = safeLeft - bx;
      bx = safeLeft;
      ex += offset; 
    }
    else if (ex > safeRight)
    {
      float offset = ex - safeRight;
      ex = safeRight;
      bx -= offset; 
    }
    if (by < safeTop)
    {
      float offset = safeTop - by;
      by = safeTop;
      ey += offset; 
    }
    else if (ey > safeBottom)
    {
      float offset = ey - safeBottom;
      ey = safeBottom;
      by -= offset; 
    }

    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(NULL, 0, 0);
    GEngine->Draw2D(mip, _tooltipColorShade, Rect2DPixel(bx * w, by * h, (ex - bx) * w, (ey - by) * h));
    GEngine->DrawText(Point2DFloat(bx + border, by + border), _tooltipSize, _tooltipFont, _tooltipColorText, _tooltip, _tooltipShadow);
    GEngine->DrawLine(Line2DPixel(bx * w, by * h, ex * w, by * h), _tooltipColorBox, _tooltipColorBox);
    GEngine->DrawLine(Line2DPixel(ex * w, by * h, ex * w, ey * h), _tooltipColorBox, _tooltipColorBox);
    GEngine->DrawLine(Line2DPixel(ex * w, ey * h, bx * w, ey * h), _tooltipColorBox, _tooltipColorBox);
    GEngine->DrawLine(Line2DPixel(bx * w, ey * h, bx * w, by * h), _tooltipColorBox, _tooltipColorBox);
  }
}

RString IControl::GetKeyHint(int button, bool &structured) const
{
  for (int i=0; i<_keyHints.Size(); i++)
    if (_keyHints[i].key == button) return _keyHints[i].hint;
  return RString();
}

RString FindShape(RString name);

Vector3 ControlObject::ReadPosition(ParamEntryPar cls, RString name3D, RString nameX, RString nameY, RString nameZ)
{
  ConstParamEntryPtr entry = cls.FindEntry(name3D);
  if (entry)
  {
    Vector3 pos;
    pos.Init();
    pos[0] = (*entry)[0];
    pos[1] = (*entry)[1];
    pos[2] = (*entry)[2];
    pos[2] *= CameraZoom;
    return pos;
  }
  else
  {
    float x = cls >> nameX;
    float y = cls >> nameY;
    float z = cls >> nameZ;
    return Convert2DTo3D(Point2DFloat(x, y), z * CameraZoom);
  }
}

ControlObject::ControlObject(ControlsContainer *parent, int idc, ParamEntryPar cls, LODShape *shape)
  : Object
  (
  shape, VISITOR_NO_ID
  ),
  IControl(parent, idc, cls)
{
  _ui = true;

  if (_shape && _shape->NLevels()>0)
  {
    _shape->Level(0)->MakeCockpit();
    //_shape->OrSpecial(BestMipmap | DisableSun);
    // WIP: 3DUI: clean here
    _shape->OrSpecial(BestMipmap);
    _shape->SetCanUsePushBuffer(false);
  }

  _position = ReadPosition(cls, "position", "x", "y", "z");
  SetPosition(_position);

  Vector3 dir, up;
  dir.Init();
  up.Init();
  dir[0] = (cls >> "direction")[0];
  dir[1] = (cls >> "direction")[1];
  dir[2] = (cls >> "direction")[2];
  up[0] = (cls >> "up")[0];
  up[1] = (cls >> "up")[1];
  up[2] = (cls >> "up")[2];
  SetOrient(dir, up);

  float scale = cls >> "scale";
  AspectSettings as;
  GEngine->GetAspectSettings(as);
  // adjust scale by aspect settings
  SetScale(scale * (as.uiBottomRightY - as.uiTopLeftY) /* * as.topFOV * (1.0 / 0.75) */);

  _offset = VZero;
  _moving = false;

  _waitForLoad = true;
  ConstParamEntryPtr entry = cls.FindEntry("waitForLoad");
  if (entry) _waitForLoad = *entry;
}

ControlObject::ControlObject(ControlsContainer *parent, int idc, ParamEntryPar cls)
: Object
  (
    Shapes.New(FindShape(cls >> "model"), false, false),
    VISITOR_NO_ID
  ),
  IControl(parent, idc, cls)
{
  _ui = true;

  if (_shape && _shape->NLevels()>0)
  {
    _shape->Level(0)->MakeCockpit();
    //_shape->OrSpecial(BestMipmap | DisableSun);
    // WIP: 3DUI: clean here
    _shape->OrSpecial(BestMipmap);
    _shape->SetCanUsePushBuffer(false);
  }

  _position = ReadPosition(cls, "position", "x", "y", "z");
  SetPosition(_position);

  Vector3 dir, up;
  dir.Init();
  up.Init();
  dir[0] = (cls >> "direction")[0];
  dir[1] = (cls >> "direction")[1];
  dir[2] = (cls >> "direction")[2];
  up[0] = (cls >> "up")[0];
  up[1] = (cls >> "up")[1];
  up[2] = (cls >> "up")[2];
  SetOrient(dir, up);

  float scale = cls >> "scale";
  AspectSettings as;
  GEngine->GetAspectSettings(as);
  // adjust scale by aspect settings
  SetScale(scale * (as.uiBottomRightY - as.uiTopLeftY) /* * as.topFOV * (1.0 / 0.75) */);

  _offset = VZero;
  _moving = false;

  _waitForLoad = true;
  ConstParamEntryPtr entry = cls.FindEntry("waitForLoad");
  if (entry) _waitForLoad = *entry;
}

ControlObject::~ControlObject()
{
}

GameValue ControlObject::CreateGameValue()
{
  return GameValueExt(this);
}

int ControlObject::GetType()
{
  return CT_OBJECT;
}

int ControlObject::GetStyle()
{
  return 0;
}

int ControlObject::GetObjSpecial(int shapeSpecial) const
{
  return _ui ? shapeSpecial|DisableSun : shapeSpecial;
}

Vector3 ControlObject::Convert2DTo3D(const Point2DFloat &point, float z)
{
  AspectSettings as;
  GEngine->GetAspectSettings(as);
  float scrX = point.x*(as.uiBottomRightX-as.uiTopLeftX)+as.uiTopLeftX;
  float scrY = point.y*(as.uiBottomRightY-as.uiTopLeftY)+as.uiTopLeftY;

  Camera *cam = GScene->GetCamera();
  float left = 2.0f * cam->Left();
  float top = 2.0f * cam->Top();
#if 0
  float leftUI = InvCameraZoom * as.leftFOV;
  float topUI = InvCameraZoom * as.topFOV;
  LogF("Left %.3f:%.3f", left, leftUI);
  LogF("Top %.3f:%.3f", top, topUI);
#endif

  return Vector3
  (
    (scrX - 0.5) * left * z, (0.5 - scrY) * top * z, z
  );
}

Point2DFloat ControlObject::Convert3DTo2D(Vector3 position)
{
  AspectSettings as;
  GEngine->GetAspectSettings(as);

  Camera *cam = GScene->GetCamera();
  float left = 2.0f * cam->Left();
  float top = 2.0f * cam->Top();
/*
  float left = InvCameraZoom * as.leftFOV;
  float top = InvCameraZoom * as.topFOV;
*/

  float scrX = position.X() / (left * position.Z()) + 0.5;  
  float scrY = position.Y() / (top * position.Z()) + 0.5;  

  float x = (scrX - as.uiTopLeftX) / (as.uiBottomRightX - as.uiTopLeftX); 
  float y = (scrY - as.uiTopLeftY) / (as.uiBottomRightY - as.uiTopLeftY); 

  return Point2DFloat(x,y);
}

Vector3 ControlObject::GetPosition2D() const
{
  Vector3Val pos = FutureVisualState().Position();
  
  float z = pos.Z();
  if (z <= 0) return VZero;
  float invZ = 1.0f / z;

  AspectSettings as;
  GEngine->GetAspectSettings(as);

  // convert to screen space
  float x = pos.X() * CameraZoom * invZ / as.leftFOV + 0.5f;
  float y = 0.5 - pos.Y() * CameraZoom * invZ / as.topFOV;

  // convert to UI space
  x = (x - as.uiTopLeftX) / (as.uiBottomRightX - as.uiTopLeftX);
  y = (y - as.uiTopLeftY) / (as.uiBottomRightY - as.uiTopLeftY);
  
  z *= InvCameraZoom;
  return Vector3(x, y, z);
}

bool ControlObject::IsInside(float x, float y) const
{
  Vector3 begin = VZero;
  Vector3 end = Convert2DTo3D(Point2DFloat(x,y),2.0 * CameraZoom);
  CollisionBuffer buffer;
  IntersectLine(GetFutureVisualStateAge(),buffer, begin, end, 0, ObjIntersectGeom);
  if (buffer.Size() > 0)
  {
    _modelPos = buffer[0].object->FutureVisualState().Transform().InverseScaled() * buffer[0].pos;
    return true;
  }
  return false;
}

void ControlObject::Move(float dx, float dy)
{
  // nothing to do
}

void ControlObject::OnLButtonDown(float x, float y)
{
  _offset = FutureVisualState().PositionModelToWorld(_modelPos) - FutureVisualState().Position();
  _moving = true;
  LogF("Start moving %s (0x%x)", cc_cast(GetDebugName()), this);
}

void ControlObject::OnLButtonUp(float x, float y)
{
  _moving = false;
  LogF("Stop moving %s (0x%x)", cc_cast(GetDebugName()), this);
}

void ControlObject::OnMouseMove(float x, float y, bool active)
{
  if (_moving)
  {
    Vector3 pos = Convert2DTo3D(Point2DFloat(x,y),FutureVisualState().Position().Z() + _offset.Z());

    pos -= _offset;
    OnEvent(CEObjectMoved, pos - _position);
    if (_parent) _parent->OnObjectMoved(_idc, pos - _position);
    _position = pos;
    SetPosition(pos);
  }
}
  
void ControlObject::OnDraw(float alpha)
{
  if (!_shape) return;
  if (_shape->NLevels() <= 0) return;
  if (!GEngine->Allow3DUI()) return;
  int level = 0;
#if _ENABLE_CHEATS || _VBS3_CHEAT_DIAG
  if (DiagDrawModeState == DDMGeometry) level = _shape->FindGeometryLevel();
#endif
  GEngine->SetInstanceInfo(-1,Color(1,1,1,alpha), 1.0f);

#if _HELISIM_RENDER_TO_TEXTURE
  DoAssert(CheckMainThread());
#endif

  Draw(-1,level, ClipAll, DrawParameters::_noShadow, InstanceParameters::_default, 0.1f, GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
    , GScene->GetDrawObjectIndex()
#endif
    );
  GEngine->SetInstanceInfo(-1,HWhite, 1);
}

bool ControlObject::IsReady() const
{
  if (!_shape || _shape->NLevels()<=0) return true;
  if (!_shape->CheckLevelLoaded(0, true)) return false;
  ShapeUsed shape = _shape->Level(0);
  return shape->PreloadTextures(0, NULL);
}

ControlObjectWithZoom::ControlObjectWithZoom(ControlsContainer *parent, int idc, ParamEntryPar cls, bool ui)
: ControlObject(parent, idc, cls)
{
  _positionBack = ReadPosition(cls, "positionBack", "xBack", "yBack", "zBack");

  for (int i=0; i<3; i++)
    _zoomMatrices[i].SetIdentity();

  _inBack = cls >> "inBack";
  _enableZoom = cls >> "enableZoom";

  _zoomDuration = cls >> "zoomDuration";

  _ui = ui;
  _zooming = false;
    
  if (_inBack)
    SetPosition(_positionBack);
}

int ControlObjectWithZoom::GetType()
{
  return CT_OBJECT_ZOOM;
}

void ControlObjectWithZoom::OnLButtonDblClick(float x, float y)
{
  if (_enableZoom) Zoom(_inBack);
}

void ControlObjectWithZoom::OnMouseMove(float x, float y, bool active)
{
  if (_zooming) return;
  if (_moving)
  {
    Vector3 pos = Convert2DTo3D(Point2DFloat(x,y),FutureVisualState().Position().Z() + _offset.Z());

    pos -= _offset;
    if (_inBack)
    {
      OnEvent(CEObjectMoved, pos - _position);
      if (_parent) _parent->OnObjectMoved(_idc, pos - _positionBack);
      _positionBack = pos;
    }
    else
    {
      OnEvent(CEObjectMoved, pos - _position);
      if (_parent) _parent->OnObjectMoved(_idc, pos - _position);
      _position = pos;
    }
    SetPosition(pos);
  }
}

void ControlObjectWithZoom::UpdatePosition()
{
  if (_zooming)
  {
    float time = Glob.uiTime - _zoomStart;
    if (time >= _zoomTimes[2])
    {
      time = _zoomTimes[2];
      _zooming = false;
      _inBack = !_inBack;
    }
    Vector3 pos = (*_interpolator)(time).Position();
    SetPosition(pos);
    if (!_zooming) _interpolator = NULL;
  }
}

void ControlObjectWithZoom::Zoom(bool zoom)
{
  if (zoom != _inBack) return;

  _zooming = true;
  _zoomStart = Glob.uiTime;
  _zoomTimes[0] = 0;
  _zoomTimes[1] = 0.5 * _zoomDuration;
  _zoomTimes[2] = 1.0 * _zoomDuration;
  if (zoom)
  {
    _zoomMatrices[0].SetPosition(_positionBack);
    _zoomMatrices[2].SetPosition(_position);
  }
  else
  {
    _zoomMatrices[0].SetPosition(_position);
    _zoomMatrices[2].SetPosition(_positionBack);
  }
  Vector3 pos;
  pos.Init();
  pos[0] = 0.33 * _position[0] + 0.67 * _positionBack[0];
  pos[1] = 0.33 * _position[1] + 0.67 * _positionBack[1];
  pos[2] = 0.67 * _position[2] + 0.33 * _positionBack[2];
  _zoomMatrices[1].SetPosition(pos);

  _interpolator = new InterpolatorSpline(_zoomMatrices, _zoomTimes, 3);
}

LSError ControlObjectWithZoom::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("inBack", _inBack, 1)) 
  _position[2] *= InvCameraZoom;
  CHECK(::Serialize(ar, "position", _position, 1))
  _position[2] *= CameraZoom;
  _positionBack[2] *= InvCameraZoom;
  CHECK(::Serialize(ar, "positionBack", _positionBack, 1))
  _positionBack[2] *= CameraZoom;

  if (ar.IsLoading())
  {
    if (_inBack)
      SetPosition(_positionBack);
    else
      SetPosition(_position);
  }
  return LSOK;
}

ControlObjectContainer::ControlObjectContainer(ControlsContainer *parent, int idc, ParamEntryPar cls)
: base(parent, idc, cls, true)
{
  _enabled = true;

  LoadControls(cls);
  SetPosition(FutureVisualState().Position());  // update controls
}

int ControlObjectContainer::GetType()
{
  return CT_OBJECT_CONTAINER;
}

Control *ControlObjectContainer::GetCtrl(const ControlOnObjectId &i) const
{
  if (i.area < 0 || i.area >= _areas.Size()) return NULL;
  const ControlsArea &area = _areas[i.area];
  if (i.id < 0 || i.id >= area.controls.Size()) return NULL;
  return area.controls[i.id];
}

IControl *ControlObjectContainer::GetCtrl(int idc)
{
  if (idc == _idc) return this;
  for (int i=0; i<_areas.Size(); i++)
  {
    ControlsArea &area = _areas[i];
    for (int j=0; j<area.controls.Size(); j++)
    {
      IControl *control = area.controls[j];
      if (control)
      {
        IControl *ctrl = control->GetCtrl(idc);
        if (ctrl) return ctrl;
      }
    }
  }
  return NULL;
}

IControl *ControlObjectContainer::GetCtrl(float x, float y)
{
  float xx, yy;
  ControlOnObjectId index = FindControl(x, y, xx, yy);
  IControl *ctrl = GetCtrl(index);
  if (ctrl) return ctrl;
  return this;
}

bool ControlObjectContainer::IsEnabled() const
{
  for (int i=0; i<_areas.Size(); i++)
  {
    const ControlsArea &area = _areas[i];
    for (int j=0; j<area.controls.Size(); j++)
    {
      const IControl *control = area.controls[j];
      if (control && control->IsVisible() && control->IsEnabled()) return true;
    }
  }
  return false;
}

IControl *ControlObjectContainer::GetFocused()
{
  IControl *ctrl = GetCtrl(_indexFocused);
  if (ctrl) return ctrl->GetFocused();
  _indexFocused = ControlOnObjectId::Null();
  return this;
}

const IControl *ControlObjectContainer::GetFocused() const
{
  IControl *ctrl = GetCtrl(_indexFocused);
  if (ctrl) return ctrl->GetFocused();
  return this;
}

bool ControlObjectContainer::CanBeDefault() const
{
  for (int i=0; i<_areas.Size(); i++)
  {
    const ControlsArea &area = _areas[i];
    for (int j=0; j<area.controls.Size(); j++)
    {
      const IControl *control = area.controls[j];
      if (control && control->IsVisible() && control->IsEnabled() && control->CanBeDefault()) return true;
    }
  }
  return false;
}

const IControl *ControlObjectContainer::GetDefault() const
{
  IControl *ctrl = GetCtrl(_indexDefault);
  if (ctrl) return ctrl->GetDefault();
  return NULL;
}

IControl *ControlObjectContainer::GetDefault()
{
  IControl *ctrl = GetCtrl(_indexDefault);
  if (ctrl) return ctrl->GetDefault();
  _indexDefault = ControlOnObjectId::Null();
  return NULL;
}

IControl *ControlObjectContainer::GetShortcutControl(int dikCode)
{
  // myself
  IControl *ctrl = base::GetShortcutControl(dikCode);
  if (ctrl) return ctrl;

  // controls under me
  for (int i=0; i<_areas.Size(); i++)
  {
    ControlsArea &area = _areas[i];
    for (int j=0; j<area.controls.Size(); j++)
    {
      IControl *ctrl = area.controls[j]->GetShortcutControl(dikCode);
      if (ctrl) return ctrl;
    }
  }
  return NULL;
}

bool ControlObjectContainer::OnSetFocus(ControlFocusAction action, bool def)
{
  if (!base::OnSetFocus(action)) return false;

  int n = _areas.Size();
  switch (action)
  {
  case CFAPrev:
    // last control
    for (int i=n-1; i>=0; i--)
    {
      ControlsArea &area = _areas[i];
      int m = area.controls.Size();
      for (int j=m-1; j>=0; j--)
      {
        IControl *control = area.controls[j];
        if (control->IsVisible() && control->IsEnabled() && (!def || control->CanBeDefault()))
        {
          SetFocus(ControlOnObjectId(i, j), def);
          return true;
        }
      }
    }
    break;
  case CFAClick:
    if (!_indexFocused.IsNull()) return true;
    // continue
  case CFANext:
    // first control
    for (int i=0; i<n; i++)
    {
      ControlsArea &area = _areas[i];
      int m = area.controls.Size();
      for (int j=0; j<m; j++)
      {
        IControl *control = area.controls[j];
        if (control->IsVisible() && control->IsEnabled() && (!def || control->CanBeDefault()))
        {
          SetFocus(ControlOnObjectId(i, j), def);
          return true;
        }
      }
    }
    break;
  }
  _indexFocused = ControlOnObjectId::Null();
  return false;
}

bool ControlObjectContainer::FocusCtrl(int idc)
{
  for (int i=0; i<_areas.Size(); i++)
  {
    ControlsArea &area = _areas[i];
    for (int j=0; j<area.controls.Size(); j++)
    {
      IControl *c = area.controls[j];
      if (c->IDC() == idc || c->FocusCtrl(idc))
      {
        if (c->IsVisible() && c->IsEnabled())
        {
          SetFocus(ControlOnObjectId(i, j));
          return true;
        }
        return false;
      }
    }
  }
  return false;
}

bool ControlObjectContainer::FocusCtrl(IControl *ctrl)
{
  for (int i=0; i<_areas.Size(); i++)
  {
    ControlsArea &area = _areas[i];
    for (int j=0; j<area.controls.Size(); j++)
    {
      IControl *c = area.controls[j];
      if (c == ctrl || c->FocusCtrl(ctrl))
      {
        if (c->IsVisible() && c->IsEnabled())
        {
          SetFocus(ControlOnObjectId(i, j));
          return true;
        }
        return false;
      }
    }
  }
  return false;
}

void ControlObjectContainer::LoadArea(ParamEntryPar cls)
{
  int index = _areas.Add();
  ControlsArea &area = _areas[index];

  area.selection = cls >> "selection";
  area.modelPos = _shape->MemoryPoint(area.selection + RString(" TL"));
  area.modelRight = _shape->MemoryPoint(area.selection + RString(" TR")) - area.modelPos;
  area.modelDown = _shape->MemoryPoint(area.selection + RString(" BL")) - area.modelPos;

#if 0
LogF("<Viewport3D>");
LogF("  Model %s", (const char *)GetShape()->GetName());
LogF("  Selection %s", (const char *)area.selection);
AspectSettings as;
GEngine->GetAspectSettings(as);
float coef = as.topFOV / as.leftFOV;
LogF("  MaxY %.3f", area.modelDown.Size() / (coef * area.modelRight.Size()));
LogF("</Viewport3D>");
#endif

  ConstParamEntryPtr cfg = cls.FindEntry("Controls");
  if (cfg)
  {
    for (int i=0; i<cfg->GetEntryCount(); i++)
    {
      ParamEntryVal ctrlCls = cfg->GetEntry(i);
      if (!ctrlCls.IsClass()) continue;
      int type = ctrlCls >> "type";
      int idc = ctrlCls >> "idc";
      Control *ctrl = _parent->OnCreateCtrl(type, idc, ctrlCls);
      if (ctrl)
      {
        int id = area.controls.Add(ctrl);
        if (ctrl->GetDefault())
          _indexDefault = ControlOnObjectId(index, id);
      }
    }
  }
}

void ControlObjectContainer::LoadControls(ParamEntryPar cls)
{
  ConstParamEntryPtr cfg = cls.FindEntry("Areas");
  if (cfg)
  {
    for (int i=0; i<cfg->GetEntryCount(); i++)
    {
      ParamEntryVal areaCls = cfg->GetEntry(i);
      if (!areaCls.IsClass()) continue;
      if (!areaCls.FindEntry("selection")) continue;
      LoadArea(areaCls);
    }

    if (!_indexDefault.IsNull())
    {
      IControl *focused = GetFocused();
      if (focused && focused->CanBeDefault()) SetFocus(_indexDefault, true); 
    }
  }
}

/*!
\patch 5128 Date 2/8/2007 by Jirka
- Fixed: Controls mapped on 3D object did not work correctly in different screen ratios than 4:3
*/

void ControlsArea::Transform(Object *obj, Vector3Par b, float &xx, float &yy, float *maxY)
{
  Vector3 position = obj->FutureVisualState().PositionModelToWorld(modelPos);
  Vector3 right = obj->FutureVisualState().DirectionModelToWorld(modelRight);
  float coef = 0.75f;
  Vector3 worldDown = obj->FutureVisualState().DirectionModelToWorld(modelDown);
  Vector3 down = coef * right.Size() * worldDown.Normalized();
  if (maxY) *maxY = worldDown.Size() / down.Size();

  float pxy = position.Y() * b.X() - position.X() * b.Y();
  float rxy = right.Y() * b.X() - right.X() * b.Y();
  float dxy = down.Y() * b.X() - down.X() * b.Y();
  float pxz = position.Z() * b.X() - position.X() * b.Z();
  float rxz = right.Z() * b.X() - right.X() * b.Z();
  float dxz = down.Z() * b.X() - down.X() * b.Z();

  float dr = dxy * rxz - dxz * rxy;
  float pr = pxy * rxz - pxz * rxy;

  // avoid the zero division
  yy = dr == 0 ? 0 : - pr / dr;
  xx = rxy == 0 ? 0 : - (pxy + dxy * yy) / rxy;
}

ControlOnObjectId ControlObjectContainer::FindControl(float x, float y, float &xx, float &yy)
{
  Vector3 b = ControlObject::Convert2DTo3D(Point2DFloat(x, y), 1);

  for (int i=0; i<_areas.Size(); i++)
  {
    ControlsArea &area = _areas[i];
    float maxY;
    area.Transform(this, b, xx, yy, &maxY);
    if (xx < 0 || xx > 1 || yy < 0 || yy > maxY) continue;

    for (int j=0; j<area.controls.Size(); j++)
    {
      IControl *control = area.controls[j];
      Assert(control);
      if (control && control->IsVisible() && control->IsMouseEnabled() && control->IsInside(xx, yy))
      {
        return ControlOnObjectId(i, j);
      }
    }
  }
  return ControlOnObjectId::Null();
}

void ControlObjectContainer::OnLButtonDown(float x, float y)
{
  float xx, yy;
  _indexL = FindControl(x, y, xx, yy);
  Control *control = GetCtrl(_indexL);
  if (control)
  {
    SetFocus(_indexL);
    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    control->OnEvent(CEMouseButtonDown, 0, xx, yy, shift, ctrl, alt);
    control->OnLButtonDown(xx, yy);
  }
  else
  {
    base::OnLButtonDown(x, y);
  }
}

void ControlObjectContainer::OnLButtonDblClick(float x, float y)
{
  float xx, yy;
  ControlOnObjectId iFound = FindControl(x, y, xx, yy);
  Control *control = GetCtrl(iFound);
  if (iFound == _indexL)
  {
    if (control)
    {
      bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
      bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
      bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
      control->OnEvent(CEMouseButtonDblClick, 0, xx, yy, shift, ctrl, alt);
      control->OnLButtonDblClick(xx, yy);
      return;
    }
  }
  else
  {
    _indexL = iFound;
    if (control)
    {
      bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
      bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
      bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
      control->OnEvent(CEMouseButtonDown, 0, xx, yy, shift, ctrl, alt);
      control->OnLButtonDown(xx, yy);
      return;
    }
  }
  base::OnLButtonDblClick(x, y);
}

void ControlObjectContainer::OnLButtonUp(float x, float y)
{
  Control *control = GetCtrl(_indexL);
  if (control)
  {
    Vector3 b = ControlObject::Convert2DTo3D(Point2DFloat(x, y), 1);
    ControlsArea &area = _areas[_indexL.area];
    float xx, yy;
    area.Transform(this, b, xx, yy);

    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    control->OnEvent(CEMouseButtonUp, 0, xx, yy, shift, ctrl, alt);
    control->OnLButtonUp(xx, yy);
  }
  else
    base::OnLButtonUp(x, y);
}

void ControlObjectContainer::OnLButtonClick(float x, float y)
{
  Control *control = GetCtrl(_indexL);
  if (control)
  {
    Vector3 b = ControlObject::Convert2DTo3D(Point2DFloat(x, y), 1);
    ControlsArea &area = _areas[_indexL.area];
    float xx, yy;
    area.Transform(this, b, xx, yy);

    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    control->OnEvent(CEMouseButtonClick, 0, xx, yy, shift, ctrl, alt);
    control->OnLButtonClick(xx, yy);
  }
  else
    base::OnLButtonClick(x, y);
}

void ControlObjectContainer::OnRButtonDown(float x, float y)
{
  float xx, yy;
  _indexR = FindControl(x, y, xx, yy);
  Control *control = GetCtrl(_indexL);
  if (control)
  {
    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    control->OnEvent(CEMouseButtonDown, 1, xx, yy, shift, ctrl, alt);
    control->OnRButtonDown(xx, yy);
  }
  else
    base::OnRButtonDown(x, y);
}

void ControlObjectContainer::OnRButtonUp(float x, float y)
{
  Control *control = GetCtrl(_indexR);
  if (control)
  {
    Vector3 b = ControlObject::Convert2DTo3D(Point2DFloat(x, y), 1);
    ControlsArea &area = _areas[_indexR.area];
    float xx, yy;
    area.Transform(this, b, xx, yy);

    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    control->OnEvent(CEMouseButtonUp, 1, xx, yy, shift, ctrl, alt);
    control->OnRButtonUp(xx, yy);
  }
  else
    base::OnRButtonUp(x, y);
  _indexR = ControlOnObjectId::Null();
}

void ControlObjectContainer::OnRButtonClick(float x, float y)
{
  Control *control = GetCtrl(_indexR);
  if (control)
  {
    Vector3 b = ControlObject::Convert2DTo3D(Point2DFloat(x, y), 1);
    ControlsArea &area = _areas[_indexR.area];
    float xx, yy;
    area.Transform(this, b, xx, yy);

    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    control->OnEvent(CEMouseButtonClick, 1, xx, yy, shift, ctrl, alt);
    control->OnRButtonClick(xx, yy);
  }
  else
    base::OnRButtonClick(x, y);
}

void ControlObjectContainer::OnMouseMove(float x, float y, bool active)
{
  if (active)
  {
    ControlOnObjectId index;
    if (GInput.mouseL)
    {
      index = _indexL;
    }
    else if (GInput.mouseR)
    {
      index = _indexR;
    }
    else
    {
      float xx, yy;
      index = FindControl(x, y, xx, yy);
    }
    if (!_indexMove.IsNull() && _indexMove != index)
    {
      Control *control = GetCtrl(_indexMove);
      if (control)
      {
        Vector3 b = ControlObject::Convert2DTo3D(Point2DFloat(x, y), 1);
        ControlsArea &area = _areas[_indexMove.area];
        float xx, yy;
        area.Transform(this, b, xx, yy);

        control->OnEvent(CEMouseMoving, xx, yy, false);
        control->OnMouseMove(xx, yy, false);
      }
    }
    if (!index.IsNull())
    {
      Control *control = GetCtrl(index);
      if (control)
      {
        Vector3 b = ControlObject::Convert2DTo3D(Point2DFloat(x, y), 1);
        ControlsArea &area = _areas[index.area];
        float xx, yy;
        area.Transform(this, b, xx, yy);

        control->OnEvent(CEMouseMoving, xx, yy, true);
        control->OnMouseMove(xx, yy);
      }
    }
    else
      base::OnMouseMove(x, y);
    _indexMove = index;
  }
  else
  {
    if (!_indexMove.IsNull())
    {
      Control *control = GetCtrl(_indexMove);
      if (control)
      {
        Vector3 b = ControlObject::Convert2DTo3D(Point2DFloat(x, y), 1);
        ControlsArea &area = _areas[_indexMove.area];
        float xx, yy;
        area.Transform(this, b, xx, yy);

        control->OnEvent(CEMouseMoving, x, y, false);
        control->OnMouseMove(x, y, false);
      }
    }
    else
      base::OnMouseMove(x, y, false);
    _indexMove = ControlOnObjectId::Null();
  }
}

void ControlObjectContainer::OnMouseHold(float x, float y, bool active)
{
  if (active)
  {
    ControlOnObjectId index;
    if (GInput.mouseL)
    {
      index = _indexL;
    }
    else if (GInput.mouseR)
    {
      index = _indexR;
    }
    else
    {
      float xx, yy;
      index = FindControl(x, y, xx, yy);
    }
    if (!_indexMove.IsNull() && _indexMove != index)
    {
      Control *control = GetCtrl(_indexMove);
      if (control)
      {
        Vector3 b = ControlObject::Convert2DTo3D(Point2DFloat(x, y), 1);
        ControlsArea &area = _areas[_indexMove.area];
        float xx, yy;
        area.Transform(this, b, xx, yy);

        control->OnEvent(CEMouseMoving, xx, yy, false);
        control->OnMouseMove(xx, yy, false);
      }
    }
    if (!index.IsNull())
    {
      Control *control = GetCtrl(index);
      if (control)
      {
        Vector3 b = ControlObject::Convert2DTo3D(Point2DFloat(x, y), 1);
        ControlsArea &area = _areas[index.area];
        float xx, yy;
        area.Transform(this, b, xx, yy);

        control->OnEvent(CEMouseHolding, xx, yy, true);
        control->OnMouseHold(xx, yy);
      }
    }
    else
      base::OnMouseMove(x, y);
    _indexMove = index;
  }
  else
  {
    if (!_indexMove.IsNull())
    {
      Control *control = GetCtrl(_indexMove);
      if (control)
      {
        Vector3 b = ControlObject::Convert2DTo3D(Point2DFloat(x, y), 1);
        ControlsArea &area = _areas[_indexMove.area];
        float xx, yy;
        area.Transform(this, b, xx, yy);

        control->OnEvent(CEMouseMoving, x, y, false);
        control->OnMouseMove(x, y, false);
      }
    }
    else
      base::OnMouseMove(x, y, false);
    _indexMove = ControlOnObjectId::Null();
  }
}

void ControlObjectContainer::OnMouseZChanged(float dz)
{
  if (!_indexMove.IsNull())
  {
    IControl *ctrl = GetCtrl(_indexMove);
    if (ctrl) ctrl->OnEvent(CEMouseZChanged, dz);
    if (ctrl) ctrl->OnMouseZChanged(dz);
  }
  else
    base::OnMouseZChanged(dz);
}

void ControlObjectContainer::SetFocus(const ControlOnObjectId &i, bool def)
{
  if (i == _indexFocused) return;

  IControl *ctrl = GetCtrl(_indexFocused);
  if (ctrl)
  {
    if (!ctrl->OnKillFocus())
      return;
  }

  ctrl = GetCtrl(i);
  if (ctrl)
  {
    ctrl->OnEvent(CESetFocus);
    ctrl->OnSetFocus(CFAClick, def);
    if (_parent) _parent->OnCtrlFocused(ctrl);
  }
  _indexFocused = i;
}

bool ControlObjectContainer::NextCtrl()
{
  int n = _areas.Size();
  int i = 0;
  if (_indexFocused.area >= 0 && _indexFocused.area < n)
  {
    ControlsArea &area = _areas[_indexFocused.area];
    int m = area.controls.Size();
    int j = 0;
    if (_indexFocused.id >= 0 && _indexFocused.id < m) j = _indexFocused.id + 1;
    for (; j<m; j++)
    {
      Control *ctrl = area.controls[j];
      if (ctrl->IsVisible() && ctrl->IsEnabled())
      {
        SetFocus(ControlOnObjectId(_indexFocused.area, j));
        return true;
      }
    }
    i = _indexFocused.area + 1;
  }
  for (; i<n; i++)
  {
    ControlsArea &area = _areas[i];
    int m = area.controls.Size();
    for (int j=0; j<m; j++)
    {
      Control *ctrl = area.controls[j];
      if (ctrl->IsVisible() && ctrl->IsEnabled())
      {
        SetFocus(ControlOnObjectId(i, j));
        return true;
      }
    }
  }
//  SetFocus(ControlOnObjectId::Null());
  return false;
}

bool ControlObjectContainer::PrevCtrl()
{
  int n = _areas.Size();
  int i = n - 1;
  if (_indexFocused.area >= 0 && _indexFocused.area < n)
  {
    ControlsArea &area = _areas[_indexFocused.area];
    int m = area.controls.Size();
    int j = m - 1;
    if (_indexFocused.id >= 0 && _indexFocused.id < m) j = _indexFocused.id - 1;
    for (; j>=0; j--)
    {
      Control *ctrl = area.controls[j];
      if (ctrl->IsVisible() && ctrl->IsEnabled())
      {
        SetFocus(ControlOnObjectId(_indexFocused.area, j));
        return true;
      }
    }
    i = _indexFocused.area - 1;
  }
  for (; i>=0; i--)
  {
    ControlsArea &area = _areas[i];
    int m = area.controls.Size();
    for (int j=m-1; j>=0; j--)
    {
      Control *ctrl = area.controls[j];
      if (ctrl->IsVisible() && ctrl->IsEnabled())
      {
        SetFocus(ControlOnObjectId(i, j));
        return true;
      }
    }
  }
  //  SetFocus(ControlOnObjectId::Null());
  return false;
}

bool ControlObjectContainer::OnKeyDown(int dikCode)
{
  IControl *focused = GetCtrl(_indexFocused);
  bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
  bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
  if (focused)
  {
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL]; (void)ctrl;
    focused->OnEvent(CEKeyDown, dikCode, shift, ctrl, alt);
  }
  if (focused && focused->OnKeyDown(dikCode))
  {
    return true;
  }
  else switch (dikCode)
  {
    case DIK_TAB:
      if (alt) return false;
      if (shift)
        return PrevCtrl();
      else
        return NextCtrl();
    case DIK_LEFT:
    case DIK_UP:
    case DIK_PRIOR:
    case INPUT_DEVICE_XINPUT + XBOX_Up:
    case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYUp:
    case INPUT_DEVICE_XINPUT + XBOX_Left:
    case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXLeft:
      return PrevCtrl();
    case DIK_RIGHT:
    case DIK_DOWN:
    case DIK_NEXT:
    case INPUT_DEVICE_XINPUT + XBOX_Down:
    case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYDown:
    case INPUT_DEVICE_XINPUT + XBOX_Right:
    case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXRight:
    case INPUT_DEVICE_XINPUT + XBOX_RightTrigger:
      return NextCtrl();
    default:
      return false;
  }
}

bool ControlObjectContainer::OnKeyUp(int dikCode)
{
  IControl *focused = GetCtrl(_indexFocused);
  if (focused)
  {
    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT]; (void)shift;
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL]; (void)ctrl;
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU]; (void)alt;
    focused->OnEvent(CEKeyUp, dikCode, shift, ctrl, alt);
  }
  if (focused && focused->OnKeyUp(dikCode))
  {
    return true;
  }
  return false;
}

bool ControlObjectContainer::OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  IControl *focused = GetCtrl(_indexFocused);
  if (focused) focused->OnEvent(CEChar, nChar);
  if (focused && focused->OnChar(nChar, nRepCnt, nFlags))
  {
    return true;
  }
  return false;
}

bool ControlObjectContainer::OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  IControl *focused = GetCtrl(_indexFocused);
  if (focused) focused->OnEvent(CEIMEChar, nChar);
  if (focused && focused->OnIMEChar(nChar, nRepCnt, nFlags))
  {
    return true;
  }
  return false;
}

bool ControlObjectContainer::OnIMEComposition(unsigned nChar, unsigned nFlags)
{
  IControl *focused = GetCtrl(_indexFocused);
  if (focused) focused->OnEvent(CEIMEComposition, nChar);
  if (focused && focused->OnIMEComposition(nChar, nFlags))
  {
    return true;
  }
  return false;
}


void ControlObjectContainer::OnDraw(float alpha)
{
  ControlObjectWithZoom::OnDraw(alpha);

  bool isAnimated = true;
  if (_shape->FindMemoryLevel() < 0)
  {
    LogF("Error: Memory level in object %s doesn't exist", _shape->Name());
    return;
  }

  AnimationContextStorageGeom storage;
  AnimationContext animContext(RenderVisualState(),storage);
  const Shape *memory = _shape->MemoryLevel();
  memory->InitAnimationContext(animContext, NULL, true); // no convex components for the memory level

  if (isAnimated) Animate(animContext, _shape->FindMemoryLevel(), false, this, 0);

  // draw areas
  for (int i=0; i<_areas.Size(); i++)
  {
    ControlsArea &area = _areas[i];
    if (isAnimated)
    {
      // update position of area
      int index = _shape->PointIndex(memory, area.selection + RString(" TL"));
      area.modelPos = index >= 0 ? animContext.GetPos(memory, index) : VZero;
      index = _shape->PointIndex(memory, area.selection + RString(" TR"));
      area.modelRight = (index >= 0 ? animContext.GetPos(memory, index) : VZero) - area.modelPos;
      index = _shape->PointIndex(memory, area.selection + RString(" BL"));
      area.modelDown = (index >= 0 ? animContext.GetPos(memory, index) : VZero) - area.modelPos;
    }
    // create viewport
    UIViewport *vp = Create3DViewport(
      -1, FutureVisualState().PositionModelToWorld(area.modelPos),
      FutureVisualState().DirectionModelToWorld(area.modelRight),
      FutureVisualState().DirectionModelToWorld(area.modelDown),
      &GScene->ActiveAggLights(),false,false
    );
    for (int j=0; j<area.controls.Size(); j++)
    {
      Control *ctrl = area.controls[j];
      if (ctrl)
      {
        ctrl->Animate();
        if (ctrl->IsVisible())
        {
          ctrl->OnDraw(vp, alpha * ctrl->GetAlpha());
        }
      }
    }
	  Destroy3DViewport(vp);
  }

  if (isAnimated) Deanimate(_shape->FindMemoryLevel(),false);
}

void ControlObjectContainer::DrawTooltip(UIViewport *vp, float x, float y)
{
  float xx, yy;
  ControlOnObjectId iFound = FindControl(x, y, xx, yy);
  IControl *ctrl = GetCtrl(iFound);
  if (ctrl)
    ctrl->DrawTooltip(vp, x, y);
  else
    base::DrawTooltip(vp, x, y);
}

int ControlObjectContainer::GetTooltipArea(float x, float y)
{
  float xx, yy;
  ControlOnObjectId iFound = FindControl(x, y, xx, yy);
  IControl *ctrl = GetCtrl(iFound);
  if (ctrl)
    return ctrl->GetTooltipArea(x, y);
  else
    return base::GetTooltipArea(x, y);
}

///////////////////////////////////////////////////////////////////////////////
// class ControlObjectContainerAnim

ControlObjectContainerAnim::ControlObjectContainerAnim(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : ControlObjectContainer(parent, idc, cls)
{
  _shape->SetAutoCenter(false);
  _shape->CalculateBoundingSphere();
  _shape->SetAnimationType(AnimTypeSoftware);

  _open = false;
  _close = false;
  _animSpeed = cls >> "animSpeed";
  _invAnimSpeed = 1.0 / _animSpeed;
  //_skeleton = new Skeleton();
  AnimationRTName name;
  name.name = GetAnimationName(cls >> "animation");
  name.skeleton = _shape->GetSkeleton();
  name.reversed = false;
  name.streamingEnabled = false;
  _animation = new AnimationRT(name);
  _animation->Prepare(false);

  int autoZoom = cls >> "autoZoom";
  _autoZoom = autoZoom != 0;
  int autoOpen = cls >> "autoOpen";
  if (autoOpen != 0)
  {
    _phase = 0;
    Open();
  }
  else
    _phase = cls >> "animPhase";
  _shape->AddLoadHandler(this);
}

ControlObjectContainerAnim::~ControlObjectContainerAnim()
{
  _shape->RemoveLoadHandler(this);
}

void ControlObjectContainerAnim::ShapeLoaded(LODShape *shape, int level)
{
}
void ControlObjectContainerAnim::ShapeUnloaded(LODShape *shape, int level)
{
}

void ControlObjectContainerAnim::Open()
{
  if (_open) return;
  _open = true;
  _close = false;
  _animStart = Glob.uiTime - _phase * _invAnimSpeed;
/*
  if (_close)
  {
    _close = false;
    _animStart = Glob.uiTime - (1 - _phase) * _invAnimSpeed;
  }
  else
  {
    _animStart = Glob.uiTime;
  }
*/
  if (_autoZoom) Zoom(true);
}

void ControlObjectContainerAnim::Close()
{
  if (_close) return;
  _close = true;
  _open = false;
  _animStart = Glob.uiTime - (1 - _phase) * _invAnimSpeed;
/*
  if (_open)
  {
    _open = false;
    _animStart = Glob.uiTime - (1 - _phase) * _invAnimSpeed;
  }
  else
  {
    _animStart = Glob.uiTime;
  }
*/
  if (_autoZoom) Zoom(false);
}

void ControlObjectContainerAnim::Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2)
{
  if (_open)
  {
    _phase = _animSpeed * (Glob.uiTime - _animStart);
    if (_phase >= 1.0)
    {
      _phase = 1.0;
      _open = false;
    }
  }
  else if (_close)
  {
    _phase = 1.0 - _animSpeed * (Glob.uiTime - _animStart);
    if (_phase <= 0)
    {
      _phase = 0;
      _close = false;
      if (_parent) _parent->OnCtrlClosed(IDC());
    }
  }
  _animation->Apply(animContext, _shape, level, _phase);
}

void ControlObjectContainerAnim::Deanimate(int level, bool setEngineStuff)
{
  // _animation->Apply(_shape, level, 0);
}

///////////////////////////////////////////////////////////////////////////////
// class CLineBreak

CLineBreak::CLineBreak(ControlsContainer *parent)
  : Control(parent, CT_LINEBREAK, -1, 0, 0, 0, 0, 0)
{
  _visible = false;
  _enabled = false;
}

///////////////////////////////////////////////////////////////////////////////
// class CTextContainer

bool ITextContainer::SetText(const char *text)
{
  // avoid reallocation if possible
  if (!strcmp(GetText(),text)) return false;
  return SetText(RString(text));
}

bool CTextContainer::SetText(RString text)
{
  if (_text && !strcmp(_text, text)) return false;
  _text = text;
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// class CStatic

const float textBorder = TEXT_BORDER;

CStatic::CStatic
(
  ControlsContainer *parent, ParamEntryPar cls,
  float x, float y, float w, float h, RString text
)
  : Control(parent, CT_STATIC, cls >> "idc", cls >> "style", x, y, w, h)
{
  _enabled = ((_style & ST_TYPE) == ST_MULTI);
  _texture = NULL;
  _bgColor = GetPackedColor(cls>>"colorBackground");
  _ftColor = GetPackedColor(cls>>"colorText");
  _font = GLOB_ENGINE->LoadFont(GetFontID(cls>>"font"));
  _size = cls >> "sizeEx";
  _firstLine = 0;
  _lineSpacing = 1;
  _autoplay = false;
  _loops  = 0;
  _transform = M4Identity;
  _offsetX = _offsetY = 0;
  _clipOffsetPercent = Rect2DFloat(0,0,0,0);
  SetText(text);

  InitColors();
}

CStatic::CStatic(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : Control(parent, CT_STATIC, idc, cls)
{
  _texture = NULL;
  _bgColor = GetPackedColor(cls>>"colorBackground");
  _ftColor = GetPackedColor(cls>>"colorText");
  _font = GLOB_ENGINE->LoadFont(GetFontID(cls>>"font"));
  _size = cls >> "sizeEx";
  _firstLine = 0;
  _enabled = false;
  _lineSpacing = 1;
  _autoplay = false;
  _loops  = 0;
  _transform = M4Identity;
  _offsetX = _offsetY = 0;
  _clipOffsetPercent = Rect2DFloat(0,0,0,0);
  
  if ((_style & ST_TYPE) == ST_MULTI)
  {
    _enabled = true;
    _lineSpacing = cls >> "lineSpacing";
  }

  ConstParamEntryPtr par =  cls.FindEntry("autoplay");
  if (	par 	)
  {
    _autoplay = cls>>"autoplay";
  }
  
  par =  cls.FindEntry("loops");
  if (	par 	)
  {
    _loops = cls>>"loops";
  }

  RString text = cls>>"text";
  SetText(text);

  if(cls.FindEntry("fixedWidth")) _fixedWidth = cls>>"fixedWidth";
  else _fixedWidth = false;

  if(cls.FindEntry("colorShadow")) _shadowColor = GetPackedColor((cls>>"colorShadow"));
  else _shadowColor = PackedBlack;

  InitColors();
}

void CStatic::InitColors()
{
  ParamEntryVal clsColors = Pars >> "CfgWrapperUI" >> "Colors";
  _color1 = GetPackedColor(clsColors >> "color1");
  _color2 = GetPackedColor(clsColors >> "color2");
  _color3 = GetPackedColor(clsColors >> "color3");
  _color4 = GetPackedColor(clsColors >> "color4");
  _color5 = GetPackedColor(clsColors >> "color5");
  switch (_style & ST_TYPE)
  {
    case ST_BACKGROUND:
      _alpha = Pars >> "CfgWrapperUI" >> "Background" >> "alpha";
      break;
    case ST_GROUP_BOX:
      _alpha = Pars >> "CfgWrapperUI" >> "GroupBox" >> "alpha";
      break;
    case ST_GROUP_BOX2:
      _alpha = Pars >> "CfgWrapperUI" >> "GroupBox2" >> "alpha";
      break;
    case ST_TITLE_BAR:
      _alpha = Pars >> "CfgWrapperUI" >> "TitleBar" >> "alpha";
      break;
    default:
      _alpha = 1;
      break;
  }
}

bool CStatic::SetText(RString text)
{
  if (!CTextContainer::SetText(text)) return false;

  if ((_style & ST_TYPE) == ST_PICTURE || (_style & ST_TYPE) == ST_TILE_PICTURE)
  {
    text = FindPicture(text);
    text.Lower();
    _texture = GlobLoadTextureUI(text);

    if (_texture && _texture->getDecoder()  && !_texture->getDecoder()->isReady()  )
    {
      _texture->getDecoder()->reinit();
    }

    if (_texture) _texture->SetUsageType(Texture::TexUI); // no limits
  }
  else if ((_style & ST_TYPE) == ST_MULTI)
  {
    FormatText();
    float height = _scale * _size;
    float maxLine = NLines() - _h / height;
    saturateMax(maxLine, 0);
    saturate(_firstLine, 0, toIntCeil(maxLine));
  }
  return true;
}

int CStatic::NLines() const
{
  return (_style & ST_TYPE) == ST_MULTI ? _lines.Size() : 1;
}

float CStatic::GetTextHeight() const
{
  return NLines() * _scale * _size;
}

bool CStatic::OnKeyDown(int dikCode)
{
  switch (dikCode)
  {
  case DIK_UP:
    if (NLines() <= 1) return false;
    {
      _firstLine--;
      saturateMax(_firstLine, 0);
    }
    return true;
  case DIK_DOWN:
    if (NLines() <= 1) return false;
    {
      _firstLine++;
      float height = _scale * _size;
      float maxLine = NLines() - _h / height;
      saturateMin(_firstLine, toIntCeil(maxLine));
    }
    return true;
  case DIK_INSERT:
  case DIK_C:
    if (GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL])
    {
      RString text = GetText();
      ExportToClipboard(text, text.GetLength());
    }
    return true;
  }
  return false;
}

bool CStatic::OnKeyUp(int dikCode)
{
  switch (dikCode)
  {
  case DIK_UP:
  case DIK_DOWN:
    if (NLines() <= 1) return false;
    // continue
  case DIK_INSERT:
  case DIK_C:
    return true;
  }
  return false;
}

void CStatic::FormatText()
{
  _lines.Clear();

  float lineWidth = _w - 2 * _scale * textBorder;
  float size = _scale * _size;

  const char *p = GetText();
  while (*p != 0)
  {
    // begin of the line
    _lines.Add(p - (const char *)GetText());
    const char *word = p;
    int n = 0;
    float width = 0;
    while (true)
    {
      const char *pLast = p;
      char c = *p++;
      if (c == 0) return;
      if (c == '\\' && *p == 'n')
      {
        p++;
        break;
      }
      if (ISSPACE(c))
      {
        n++;
        word = p;
      }

      // read the single character in UTF-8 encoding
      char temp[7];
      int j = 0;
      temp[j++] = c;
      if (c & 0x80)
      {
        DoAssert((c & 0xc0) == 0xc0); // the first character
        while (((c = *p) & 0xc0) == 0x80)
        {
          p++;
          if (j < 5) temp[j++] = c;
          else
          {
            Fail("Character too long");
          }
        };
      }
      temp[j] = 0;
      width += GEngine->GetTextWidth(size, _font, temp);

      if (width > lineWidth)
      {
        if (n > 0)
        {
          p = word;
          break;
        }
        else
        {
          p = pLast;
          break;
        }
      }
    }
  }
}

RString CStatic::GetLine(int i) const
{

  if ((_style & ST_TYPE) != ST_MULTI) return "";
  if (i < 0) return "";
  if (i >= NLines()) return "";

  int from = _lines[i];
  int to = i + 1 < NLines() ? _lines[i + 1] : GetText().GetLength();
  return GetText().Substring(from,to);
}

bool IsTextureReady(Texture *texture)
{
  if (!texture) return true;
  if (!texture->HeadersReady()) return false;
  if (texture->IgnoreLoadedLevels()) return true; // assume video is always ready
  MipInfo mip = GEngine->TextBank()->UseMipmap(texture, 1, 0, TexPriorityUILow);
  return mip._level <= texture->BestMipmap();
}

static bool IsAnimatedTextureReady(Texture *texture)
{
  if (!texture) return true;
  if (!texture->HeadersReady()) return false;
  if (texture->IsAnimated())
  {
    bool ok = true;
    for (int i=0; i<texture->AnimationLength(); i++)
    {
      if (!texture->RequestAnimation(i, 0)) ok = false;
    }
    return ok;
  }
  else
  {
    MipInfo mip = GEngine->TextBank()->UseMipmap(texture, 1, 0, TexPriorityUILow);
    return mip._level <= texture->BestMipmap();
  }
}

bool IsFontReady(Font *font, float size)
{
  float h = GEngine->Height2D();
  const FontWithSize *fontWithSize = font->Select(h * size);
  return !fontWithSize || fontWithSize->IsReady();
}

static void Intersect(Rect2DFloat &result, const Rect2DFloat &rect, float x, float y, float w, float h)
{
  result.x = floatMax(rect.x, x);
  result.y = floatMax(rect.y, y);
  result.w = floatMin(rect.x + rect.w, x + w) - result.x;
  result.h = floatMin(rect.y + rect.h, y + h) - result.y;
}

bool CStatic::IsReady() const
{
  bool ready = true;

  int type = _style & ST_TYPE;
  switch (type)
  {
  case ST_HUD_BACKGROUND:
    {
      Texture *corner = GLOB_SCENE->Preloaded(Corner);
      if (corner && !IsTextureReady(corner)) ready = false;
      break;
    }
  case ST_BACKGROUND:
    {
      Texture *background = GLOB_SCENE->Preloaded(DialogBackground);
      if (background && !IsTextureReady(background)) ready = false;
      break;
    }
  case ST_FRAME:
    {
      if (GetText().GetLength() > 0)
      {
        if (!IsFontReady(_font, _scale * _size)) ready = false;
      }
      return ready;
    }
  case ST_GROUP_BOX2:
    {
      Texture *background = GLOB_SCENE->Preloaded(DialogGroup);
      if (background && !IsTextureReady(background)) ready = false;
      break;
    }
  case ST_PICTURE:
  case ST_TILE_PICTURE:
    {
      if (_texture && !IsTextureReady(_texture)) ready = false;
      return ready;
    }
  }

  if (!IsText()) return ready;

  if (!IsFontReady(_font, _scale * _size)) ready = false;
  return ready;
}

bool CStatic::TextFits(RString text) const
{
  if (!IsText()) return true;
  if ((_style & ST_TYPE) == ST_MULTI) return true;

  float size = 2 * _scale * textBorder + GEngine->GetTextWidth(_scale * _size, _font, text);
  return size <= _w;
}

#define v000 VZero
#define v001 VForward
#define v100 VAside
#define v101 Vector3(1, 0, 1)

void CStatic::OnDraw(UIViewport *vp, float alpha)
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  float xx = CX(_x);
  float yy = CY(_y);
  float ww = CW(SCALED(_w));
  float hh = CH(SCALED(_h));

  int type = _style & ST_TYPE;
  
  PackedColor ftColor = ModAlpha(_ftColor, alpha);
  PackedColor bgColor = ModAlpha(_bgColor, alpha);

  float size = _scale * SCALED(_size);

  Rect2DFloat clipRect(_x, _y, SCALED(_w), SCALED(_h));

  if (_texture && _texture->getDecoder() )
  {
    if (_loops>0)
    {
      _texture->getDecoder()->setLoops(_loops);
      _loops = 0;
    }
    
    if (_autoplay)
    {
      _texture->getDecoder()->Start();
      _autoplay=false;
    }
    
    if (!_texture->getDecoder()->IsPlaybackDone())
    {
      OnEvent(CEVideoStopped);
  }
  
  }
  
  switch (type)
  {
    case ST_HUD_BACKGROUND:
    {
      Texture *corner = GLOB_SCENE->Preloaded(Corner);
      vp->DrawFrame
      (
        corner, bgColor,
        Rect2DPixel(xx, yy, ww, hh)
      );
      break;
    }
    case ST_BACKGROUND:
    {
      // background
      Texture *background = GLOB_SCENE->Preloaded(DialogBackground);
      MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap
      (
        background, 0, 0
      );
      float invH = 1, invW = 1;
      if (background)
      {
        invH = 1.0 / background->AHeight();
        invW = 1.0 / background->AWidth();
      }
      Rect2DPixel rect;
      rect.w = ww;
      rect.h = hh;
      rect.x = xx;
      rect.y = yy;
      Draw2DPars pars;
      pars.mip = mip;
      pars.SetColor(bgColor);
      pars.spec = NoZBuf|IsAlpha|NoClamp|IsAlphaFog;
      pars.SetU(0, ww * invW );
      pars.SetV(0, hh * invH );
      pars.spec|= (_shadow == 2)?UISHADOW : 0; 
      vp->Draw2D(Draw2DParsExt(pars), rect);

      // frame
      PackedColor bgColor1 = ModAlpha(_color1, alpha * _alpha);
      PackedColor bgColor2 = ModAlpha(_color2, alpha * _alpha);
      PackedColor bgColor3 = ModAlpha(_color3, alpha * _alpha);
      PackedColor bgColor4 = ModAlpha(_color4, alpha * _alpha);
      PackedColor bgColor5 = ModAlpha(_color5, alpha * _alpha);

      DrawLeft(0, bgColor5);
      DrawTop(0, bgColor5);
      DrawLeft(1, bgColor4);
      DrawTop(1, bgColor4);
      DrawLeft(2, bgColor3);
      DrawTop(2, bgColor3);
      DrawLeft(3, bgColor3);
      DrawTop(3, bgColor3);
      DrawLeft(4, bgColor2);
      DrawTop(4, bgColor2);
      DrawLeft(5, bgColor1);
      DrawTop(5, bgColor1);

      DrawBottom(0, bgColor1);
      DrawRight(0, bgColor1);
      DrawBottom(1, bgColor2);
      DrawRight(1, bgColor2);
      DrawBottom(2, bgColor3);
      DrawRight(2, bgColor3);
      DrawBottom(3, bgColor3);
      DrawRight(3, bgColor3);
      DrawBottom(4, bgColor4);
      DrawRight(4, bgColor4);
      DrawBottom(5, bgColor5);
      DrawRight(5, bgColor5);
      break;
    }
    case ST_FRAME:
    {
      if (GetText().GetLength() >0 )
      {
        TextDrawAttr attr(size, _font, ftColor, _shadow);
        float width = vp->GetTextWidth(attr, GetText());
        float heigth = size;
        const float border = _scale * SCALED(0.01);
        if (border + width + border <= SCALED(_w))
        {
          vp->DrawText
          (
            Point2DFloat(_x + border, _y), clipRect,
            attr, GetText()
          );
          float top = yy + 0.5 * heigth * h;
          vp->DrawLine
          (
            Line2DPixel(xx, top, xx + border * w, top), ftColor, ftColor
          );
          vp->DrawLine
          (
            Line2DPixel(xx + (border + width) * w, top, xx + ww - 1, top), ftColor, ftColor
          );
          vp->DrawLine
          (
            Line2DPixel(xx, top, xx, yy + hh - 1), ftColor, ftColor
          );
          vp->DrawLine
          (
            Line2DPixel(xx + ww - 1, top, xx + ww - 1, yy + hh - 1), ftColor, ftColor
          );
          DrawBottom(0, ftColor);
          return;
        }
      }

      DrawLeft(0, ftColor);
      DrawTop(0, ftColor);
      DrawBottom(0, ftColor);
      DrawRight(0, ftColor);
      return;
    }
    case ST_WITH_RECT:
    {
      DrawLeft(0, ftColor);
      DrawTop(0, ftColor);
      DrawBottom(0, ftColor);
      DrawRight(0, ftColor);
      break;
    }
    case ST_LINE:
    {
      vp->DrawLine(Line2DPixel(xx, yy, xx + ww, yy + hh), ftColor, ftColor);
      break;
    }
    case ST_GROUP_BOX:
    {
      PackedColor grpColor2 = ModAlpha(_color2, alpha * _alpha);
      PackedColor grpColor3 = ModAlpha(_color3, alpha * _alpha);
      PackedColor grpColor4 = ModAlpha(_color4, alpha * _alpha);

      Draw2DParsExt pars;
      pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
      pars.SetColor(grpColor3);
      pars.Init();
      pars.spec|= (_shadow == 2)?UISHADOW : 0; 

      vp->Draw2D(pars, Rect2DPixel(xx + 1, yy + 1, ww - 2, hh - 2));

      DrawLeft(0, grpColor2);
      DrawTop(0, grpColor2);
      DrawBottom(0, grpColor4);
      DrawRight(0, grpColor4);
      break;
    }
    case ST_GROUP_BOX2:
    {
      PackedColor grpColor2 = ModAlpha(_color2, alpha * _alpha);
      PackedColor grpColor3 = ModAlpha(_color3, alpha * _alpha);
      PackedColor grpColor4 = ModAlpha(_color4, alpha * _alpha);

      Texture *background = GLOB_SCENE->Preloaded(DialogGroup);
      Draw2DParsExt pars;
      pars.mip = GEngine->TextBank()->UseMipmap(background, 0, 0);
      pars.SetColor(bgColor);
      pars.Init();
      pars.spec|= (_shadow == 2)?UISHADOW : 0; 
      vp->Draw2D(pars, Rect2DPixel(xx + 1, yy + 1, ww - 2, hh - 2));

      DrawLeft(0, grpColor2);
      DrawTop(0, grpColor2);
      DrawBottom(0, grpColor4);
      DrawRight(0, grpColor4);
      break;
    }
    case ST_PICTURE:
    case ST_TILE_PICTURE:
    {
      if (_texture)
      {
        float x = _x + _offsetX, y = _y + _offsetY, width = SCALED(_w), height = SCALED(_h);
        if (_style & ST_KEEP_ASPECT_RATIO)
        {
          float xCoef = (SCALED(_w) * w) / _texture->AWidth();
          float yCoef = (SCALED(_h) * h) / _texture->AHeight();
          if (xCoef > yCoef)
          {
            width *= yCoef / xCoef;
            x += 0.5 * (SCALED(_w) - width);
          }
          else
          {
            height *= xCoef / yCoef;
            y += 0.5 * (SCALED(_h) - height);
          }
        }
        float xx = CX(x);
        float yy = CY(y);
        float ww = CX(x + width) - xx;
        float hh = CY(y + height) - yy;

        if (_texture && _texture->getDecoder() && !_texture->getDecoder()->isReady() )
        {
          return;
        }
        Draw2DParsExt pars;
        pars.mip = GEngine->TextBank()->UseMipmap(_texture, 0, 0);
        pars.SetColor(ftColor);
        pars.Init();
        pars.spec|= (_shadow == 2)?UISHADOW : 0; 

        Vector3 uv = _transform.FastTransform(Vector3(1, 0, 0));
        pars.uTR = uv[0];
        pars.vTR = uv[2];
        uv = _transform.FastTransform(Vector3(0, 0, 0));
        pars.uTL = uv[0];
        pars.vTL = uv[2];
        uv = _transform.FastTransform(Vector3(1, 0, 1));
        pars.uBR = uv[0]; 
        pars.vBR = uv[2];
        uv = _transform.FastTransform(Vector3(0, 0, 1));
        pars.uBL = uv[0];
        pars.vBL = uv[2];

        vp->Draw2D(pars, 
          Rect2DPixel(xx, yy, ww, hh),
          Rect2DPixel(xx + _clipOffsetPercent.x * ww , yy + _clipOffsetPercent.y * hh, ww + _clipOffsetPercent.w * ww, hh + _clipOffsetPercent.h * hh));
      }
      return; // do not draw any text (text field content texture name)
    }
    case ST_TITLE_BAR:
    {
      PackedColor barColor2 = ModAlpha(_color2, alpha * _alpha);
      PackedColor barColor3 = ModAlpha(_color3, alpha * _alpha);
      PackedColor barColor4 = ModAlpha(_color4, alpha * _alpha);

      Draw2DParsExt pars;
      pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
      pars.SetColor(barColor3);
      pars.Init();
      pars.spec|= (_shadow == 2)?UISHADOW : 0; 
      vp->Draw2D(pars, Rect2DPixel(xx + 5, yy + 1, ww - 10, hh - 2));
      vp->DrawLine
      (
        Line2DPixel(xx + 5, yy, xx + ww - 5, yy),
        barColor4, barColor4
      );
      vp->DrawLine
      (
        Line2DPixel(xx + ww - 8, yy + 5, xx + 7, yy + 5),
        barColor2, barColor2
      );
      vp->DrawLine
      (
        Line2DPixel(xx + 7, yy + 5, xx + 7, yy + hh - 6),
        barColor2, barColor2
      );
      vp->DrawLine
      (
        Line2DPixel(xx + 7, yy + hh - 6, xx + ww - 7, yy + hh - 6),
        barColor4, barColor4
      );
      vp->DrawLine
      (
        Line2DPixel(xx + ww - 8, yy + hh - 6, xx + ww - 8, yy + 5),
        barColor4, barColor4
      );
      vp->DrawLine
      (
        Line2DPixel(xx + 5, yy + hh - 1, xx + ww - 5, yy + hh - 1),
        barColor2, barColor2
      );
      break;
    }
    default:
    {
      if (bgColor.A8()>0)
      {
        Draw2DParsExt pars;
        pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
        pars.SetColor(bgColor);
        pars.Init();
        pars.spec|= (_shadow == 2)?UISHADOW : 0; 
        vp->Draw2D(pars, Rect2DPixel(xx, yy, ww, hh));
      }
      break;
    }
  }
  
  if (!IsText())
    return;
  
  if (type == ST_MULTI)
  {
    float height = size * _lineSpacing;
    if (IsFocused() && (_style & ST_NO_RECT) == 0)
    {
      DrawLeft(0, ftColor);
      DrawTop(0, ftColor);
      DrawBottom(0, ftColor);
      DrawRight(0, ftColor);
    }

    int fl = toIntFloor(_firstLine);
    float top = _y - (_firstLine - fl) * height;
    for (int i=fl; i<_lines.Size(); i++)
    {
      RString text = GetLine(i);
      char *p = text.MutableData();
      if (!p) continue;
      while ((p = strchr(p, '\\'))!=0)
      {
        if (*(++p) == 'n')
        {
          *(p-1) = 0;
          break;
        }
      }
      DrawText(vp, text, _x, top, alpha, clipRect);
      top += height;
      if (top >= _y + SCALED(_h)) break;
    }
  }
  else
  {
    int pos = _style & ST_POS;
    if (pos == ST_UP || pos == ST_VCENTER || pos == ST_DOWN)
    {
      float left = _x + 0.5 * (_w - size);
      DrawText(vp, GetText(), left, _y, alpha, clipRect);
    }
    else
    {
      float top = _y + 0.5 * (SCALED(_h) - size);
      DrawText(vp, GetText(), _x, top, alpha, clipRect);
    }
  }
}

bool CStatic::IsText() const
{
  return GetText().GetLength() >0;
}

float CStatic::GetTextSize() const
{
  return 2 * _scale * textBorder + GEngine->GetTextWidth(_scale * _size, _font, GetText());
}

void CStatic::DrawText(UIViewport *vp, const char *text, float x, float y, float alpha, const Rect2DFloat clipRect)
{
  int pos = _style & ST_POS;
  float size = _scale * SCALED(_size);
  bool vertical = false;
  const float height = size;
  const float offsetX = 0.045 * height;
  const float offsetY = 0.06 * height;

  TextDrawAttr attr(size, _font, PackedBlack, _shadow);
  attr._fixedWidth = _fixedWidth;
  attr._shadow = _shadow;

  float left = x;
  float top = y;
  switch (pos)
  {
    case ST_UP:
      top = y + _scale * SCALED(textBorder);
      vertical = true;
      break;
    case ST_VCENTER:
      top = y + 0.5 * (SCALED(_h) - vp->GetTextWidth(attr, text));
      vertical = true;
      break;
    case ST_DOWN:
      top = y + SCALED(_h) - vp->GetTextWidth(attr, text) - _scale * SCALED(textBorder);
      vertical = true;
      break;
    case ST_RIGHT:
      left = x + SCALED(_w) - vp->GetTextWidth(attr, text) - _scale * SCALED(textBorder);
      break;
    case ST_CENTER:
      left = x + 0.5 * (SCALED(_w) - vp->GetTextWidth(attr, text));
      break;
    default:
      Assert(pos == ST_LEFT)
      left = x + _scale * SCALED(textBorder);
      break;
  }

  PackedColor ftColor = ModAlpha(_ftColor, alpha);

  if (vertical)
  {
    if (_shadow== 1)
    {
      attr._shadow = 0;
      attr._color = ModAlpha(_shadowColor, alpha);
      vp->DrawTextVertical
      (
        Point2DFloat(left + offsetX, top + offsetY), clipRect, attr, text
      );
    }
    attr._color = ftColor;
    vp->DrawTextVertical
    (
      Point2DFloat(left, top), clipRect, attr, text
    );
  }
  else
  {
    if (_shadow == 1)
    {
      attr._color = ModAlpha(_shadowColor, alpha);
      vp->DrawText
      (
        Point2DFloat(left + offsetX, top + offsetY), clipRect, attr, text
      );
    }
    attr._color = ftColor;
    vp->DrawText
    (
      Point2DFloat(left, top), clipRect, attr, text
    );
  }
}

bool CStaticTime::IsText() const
{
  return (_style & ST_TYPE) != ST_MULTI;
}

void CStaticTime::DrawText(UIViewport *vp, const char *text, float x, float y, float alpha, const Rect2DFloat clipRect)
{
  float time = _time.GetTimeOfDay();
  if (time > 1.0) time--;
  time *= 24;
  int hour = toIntFloor(time);
  time -= hour;
  time *= 60;
  int min = toIntFloor(time);
  time -= min;
  time *= 60;
  int sec = toIntFloor(time);
  if (sec >= 60)
  {
    min++;
    sec -= 60;
  }
  if (min >= 60)
  {
    hour++;
    min -= 60;
  }
  bool colon = !_blink || (sec % 2 == 0);
  
  char bufHour[4], bufMin[4], bufSec[4];
  sprintf(bufHour, "% 2d", hour);
  sprintf(bufMin, "%02d", min);
  sprintf(bufSec, "%02d", sec);

#if !_VBS3
  const float coefH = 0.8;
#endif
  const float coefW = 0.5;
  float size = _scale * _size;
  float size2 = size * 0.75;

  TextDrawAttr attr(size, _font, PackedBlack, _shadow);
  TextDrawAttr attr2(size2, _font, PackedBlack, _shadow);

#if !_VBS3
  float diffH = coefH * (size - size2);
#endif
  float diffW = coefW * vp->GetTextWidth(attr2, " ");

  float width = 
    vp->GetTextWidth(attr, bufHour) +
    vp->GetTextWidth(attr, ":") +
    vp->GetTextWidth(attr, bufMin) +
    diffW + vp->GetTextWidth(attr2, bufSec);

  int pos = _style & ST_HPOS;
  float left = 0;
  float top = y;
  switch (pos)
  {
    case ST_RIGHT:
      left = x + _w - width - _scale * textBorder;
      break;
    case ST_CENTER:
      left = x + 0.5 * (_w - width);
      break;
    default:
      Assert(pos == ST_LEFT)
      left = x + _scale * textBorder;
      break;
  }
  
  const float offsetX = 0.045 * size;
  const float offsetY = 0.06 * size;

  PackedColor ftColor = ModAlpha(_ftColor, alpha);
  PackedColor shadowColor = PackedColor(Color(0, 0, 0, alpha));
  
  if (_shadow == 1)
  {
    float left2 = left;
    attr._color = shadowColor;
    attr2._color = shadowColor;
    vp->DrawText
    (
      Point2DFloat(left2 + offsetX, top + offsetY), clipRect, attr, bufHour
    );
    left2 += vp->GetTextWidth(attr, bufHour);
    if (colon)
    {
      vp->DrawText
      (
        Point2DFloat(left2 + offsetX, top + offsetY), clipRect, attr, ":"
      );
    }
    left2 += vp->GetTextWidth(attr, ":");
    vp->DrawText
    (
      Point2DFloat(left2 + offsetX, top + offsetY), clipRect, attr, bufMin
    );
#if _VBS3 //draw seconds normal
    left2 += vp->GetTextWidth(attr, bufMin);
    if (colon)
    {
      vp->DrawText
        (
        Point2DFloat(left2 + offsetX, top + offsetY), clipRect, attr, ":"
        );
    }
    left2 += vp->GetTextWidth(attr, ":");
    vp->DrawText
    (
      Point2DFloat(left2 + offsetX, top + offsetY), clipRect, attr, bufSec
    );
#else
    left2 += vp->GetTextWidth(attr, bufMin) + diffW;
    vp->DrawText
    (
      Point2DFloat(left2 + offsetX, top + diffH + offsetY), clipRect, attr2, bufSec
    );
#endif
  }

  attr._color = ftColor;
  attr2._color = ftColor;
  vp->DrawText
  (
    Point2DFloat(left, top), clipRect, attr, bufHour
  );
  left += vp->GetTextWidth(attr, bufHour);
  if (colon)
  {
    vp->DrawText
    (
      Point2DFloat(left, top), clipRect, attr, ":"
    );
  }
  left += vp->GetTextWidth(attr, ":");
  vp->DrawText
  (
    Point2DFloat(left, top), clipRect, attr, bufMin
  );
#if _VBS3 //draw seconds normal
  left += vp->GetTextWidth(attr, bufMin);
  if (colon)
  {
    vp->DrawText
      (
      Point2DFloat(left, top), clipRect, attr, ":"
      );
  }
  left += vp->GetTextWidth(attr, ":");
  vp->DrawText
    (
    Point2DFloat(left, top), clipRect, attr, bufSec
    );
#else
  left += vp->GetTextWidth(attr, bufMin) + diffW;
  vp->DrawText
  (
    Point2DFloat(left, top + diffH), clipRect, attr2, bufSec
  );
#endif
}

///////////////////////////////////////////////////////////////////////////////
// class CSkewStatic

CSkewStatic::CSkewStatic(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : Control(parent, CT_STATIC_SKEW, idc, cls)
{
  _enabled = false;
  _color = GetPackedColor(cls >> "color");
  _xTL = cls >> "TL" >> "x";
  _yTL = cls >> "TL" >> "y";
  _alphaTL = cls >> "TL" >> "alpha";
  _xTR = cls >> "TR" >> "x";
  _yTR = cls >> "TR" >> "y";
  _alphaTR = cls >> "TR" >> "alpha";
  _xBL = cls >> "BL" >> "x";
  _yBL = cls >> "BL" >> "y";
  _alphaBL = cls >> "BL" >> "alpha";
  _xBR = cls >> "BR" >> "x";
  _yBR = cls >> "BR" >> "y";
  _alphaBR = cls >> "BR" >> "alpha";
  
  ParamEntryVal clsColors = Pars >> "CfgWrapperUI" >> "Colors";
  _lineColors[0] = GetPackedColor(clsColors >> "color1");
  _lineColors[1] = GetPackedColor(clsColors >> "color2");
  _lineColors[2] = GetPackedColor(clsColors >> "color3");
  _lineColors[3] = GetPackedColor(clsColors >> "color4");
  _lineColors[4] = GetPackedColor(clsColors >> "color5");

  _x = floatMin(_xTL, _xBL);
  _w = floatMax(_xTR, _xBR) - _x;
  _y = floatMin(_yTL, _yTR);
  _h = floatMax(_yBL, _yBR) - _y;
}

void CSkewStatic::Move(float dx, float dy)
{
  Control::Move(dx, dy);
  _xTL += dx;
  _xTR += dx;
  _xBL += dx;
  _xBR += dx;
  _yTL += dy;
  _yTR += dy;
  _yBL += dy;
  _yBR += dy;
}
  
void CSkewStatic::OnDraw(UIViewport *vp, float alpha)
{
  Fail("Obsolete");
}

///////////////////////////////////////////////////////////////////////////////
// class CStructuredText

CStructuredText::CStructuredText(ControlsContainer *parent, int idc, ParamEntryPar cls, bool ignoreText)
  : Control(parent, CT_STRUCTURED_TEXT, idc, cls)
{
  _enabled = false;
  if (ignoreText) _text = CreateTextStructured(NULL);
  else _text = CreateTextStructured(NULL, cls >> "text");
  ConstParamEntryPtr entry = cls.FindEntry("Attributes");
  if (entry)
  {
    for (int i=0; i<entry->GetEntryCount(); i++)
    {
      ParamEntryVal attr = entry->GetEntry(i);
      RString name = attr.GetName();
      RString value = attr;
      SetTextAttribute(_text, name, value);
    }
  }
  _size = cls >> "size";
  entry = cls.FindEntry("colorBackground");
  if (entry) _bgColor = GetPackedColor(*entry);
  else _bgColor = PackedColor(0, 0, 0, 0);
}

bool CStructuredText::IsReady() const
{
  if (!_text) return true;
  return IsTextReadyToDraw(_text, _h);
}

void CStructuredText::OnDraw(UIViewport *vp, float alpha)
{
  PackedColor bgColor = ModAlpha(_bgColor, alpha);
  if (bgColor.A8()>0)
  {
    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();
    float xx = CX(_x);
    float yy = CY(_y);
    float ww = CW(SCALED(_w));
    float hh = CH(SCALED(_h));

    Draw2DParsExt pars;
    pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0); 
    pars.SetColor(bgColor);
    pars.Init();
    pars.spec|= (_shadow == 2)?UISHADOW : 0; 
    vp->Draw2D(pars, Rect2DPixel(xx, yy, ww, hh));
  }
  SetShadow(_shadow);
  ::DrawText(vp, _text, Rect2DFloat(_x + SCALED(TEXT_BORDER), _y, SCALED(_w) - 2 * SCALED(TEXT_BORDER), _h), SCALED(_size), alpha);
}

void CStructuredText::SetStructuredText(RString content)
{
  // store attributes and use them for new text
  AttributeList attributes = _text->GetAttributes();
  _text = CreateTextStructured(NULL, content);
  _text->SetAttributes(attributes);
}

void CStructuredText::SetRawText(RString content)
{
  // store attributes and use them for new text
  AttributeList attributes = _text->GetAttributes();
  _text = CreateTextPlain(NULL, content);
  _text->SetAttributes(attributes);
}

void CStructuredText::SetText(INode *text)
{
  // store attributes and use them for new text
  AttributeList attributes = _text->GetAttributes();
  _text = text;
  _text->SetAttributes(attributes);
}

float CStructuredText::GetTextHeight()
{
  return ::GetTextHeight(_text, Rect2DFloat(_x + TEXT_BORDER, _y, _w - 2 * TEXT_BORDER, _h), _size);
}

void CStructuredText::SetTextColor(PackedColor color)
{
  ::SetTextColor(_text, color);
}

void CStructuredText::SetFont(Font *font)
{
  ::SetTextFont(_text, font);
}

void CStructuredText::SetShadow(int shadow)
{
  ::SetShadow(_text, shadow);
}


// ITextContainer interface implementation

RString CStructuredText::GetText() const
{
  return GetTextPlain(_text);
}

bool CStructuredText::SetText(RString text)
{
  SetRawText(text);
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// class CXKeyDesc

CXKeyDesc::CXKeyDesc(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : CStructuredText(parent, idc, cls, true)
{
  _type = CT_XKEYDESC;

  _key = cls >> "key";
  
  INode *GetKeyImageOrName(INode *parent, int dikKey);
  _keyDesc = GetKeyImageOrName(NULL, _key);

  ConstParamEntryPtr entry = cls.FindEntry("AttributesImage");
  if (entry)
  {
    for (int i=0; i<entry->GetEntryCount(); i++)
    {
      ParamEntryVal attr = entry->GetEntry(i);
      RString name = attr.GetName();
      RString value = attr;
      SetTextAttribute(_keyDesc, name, value);
    }
  }
}

void CXKeyDesc::SetHint(RString hint, bool structured)
{
  INode *FormatText(const char *format, RefR<INode> *args, int nArgs, bool structured);
  SetText(FormatText(hint, &_keyDesc, 1, structured));
}

void CXKeyDesc::OnDraw(UIViewport *vp, float alpha)
{
  // Avoid multiple hints confusing 
  if (_parent && (_parent->Child() || _parent->GetMsgBox())) return;
  if (GWorld && GWorld->GetWarningMessage() && GWorld->GetWarningMessage() != _parent) return;

  // Do not draw hints which shortcut is not accessible
  bool presentKeyboard = true;
  bool presentXInput = GInput.IsXInputPresent();
  bool shorcutAvailable = 
    presentKeyboard && (_key & INPUT_DEVICE_MASK) == INPUT_DEVICE_KEYBOARD ||
    presentXInput && (_key & INPUT_DEVICE_MASK) == INPUT_DEVICE_XINPUT;
  if (shorcutAvailable)
  {
    base::OnDraw(vp, alpha);
  }
}

///////////////////////////////////////////////////////////////////////////////
// class CXButton

CXButton::CXButton(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : base(parent, idc, cls)
{
  _type = CT_XBUTTON;
  _enabled = true;

  _color = GetPackedColor(cls >> "color");
  _colorActive = GetPackedColor(cls >> "colorActive");
  _bgColorActive = GetPackedColor(cls >> "colorActiveBackground");
  _colorDisabled = GetPackedColor(cls >> "colorDisabled");
  ConstParamEntryPtr entry = cls.FindEntry("colorActive2");
  if (entry) _colorActive2 = GetPackedColor(*entry);
  else _colorActive2 = _colorActive;
  entry = cls.FindEntry("colorActiveBackground2");
  if (entry) _bgColorActive2 = GetPackedColor(*entry);
  else _bgColorActive2 = _bgColorActive;

  float period = 2.0f;
  entry = cls.FindEntry("period");
  if (entry) period = *entry;
  _speed = period > 0 ? H_PI / period : 0;

  GetValue(_pushSound, cls >> "soundPush");
  GetValue(_clickSound, cls >> "soundClick");
  GetValue(_escapeSound, cls >> "soundEscape");

  entry = cls.FindEntry("action");
  if (entry) _action = *entry;

  _start = Glob.uiTime;
}

bool CXButton::OnKeyDown(int dikCode)
{
  if (ButtonPressed(dikCode))
  {
    OnEvent(CEButtonDown);
    PlaySound(_pushSound);
    return true;
  }
  return false;
}

bool CXButton::OnKeyUp(int dikCode)
{
  if (ButtonPressed(dikCode))
  {
    DoClick();
    return true;
  }
  return false;
}

bool CXButton::OnShortcutDown()
{
  if (!IsEnabled()) return false;

  OnEvent(CEButtonDown);
  PlaySound(_pushSound);
  return true;
}

bool CXButton::OnShortcutUp()
{
  if (!IsEnabled()) return false;

  DoClick();
  return true;
}

void CXButton::OnLButtonDown(float x, float y)
{
  OnEvent(CEButtonDown);
  PlaySound(_pushSound);
}

void CXButton::OnLButtonUp(float x, float y)
{
  if (IsInside(x, y))
  {
    DoClick();
  }
  else
  {
    OnEvent(CEButtonUp);
    PlaySound(_escapeSound);
  }
}

void CXButton::OnDraw(UIViewport *vp, float alpha)
{
  PackedColor color;
  if (!IsEnabled()) color = _colorDisabled;
  else if (IsFocused())
  {
    float factor = -0.5 * cos(_speed * (Glob.uiTime - _start)) + 0.5;

    Color col0=_colorActive, col1=_colorActive2;
    color = PackedColor(col0 * factor + col1 * (1 - factor));

    Color bgcol0=_bgColorActive, bgcol1=_bgColorActive2;
    PackedColor bgColor = PackedColor(bgcol0 * factor + bgcol1 * (1 - factor));
    bgColor = ModAlpha(bgColor, alpha);

    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();

    Rect2DPixel rect(CX(_x), CY(_y), CW(SCALED(_w)), CH(SCALED(_h)));
    Draw2DParsExt pars;
    pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
    pars.SetColor(bgColor);
    pars.Init();
    pars.spec|= (_shadow == 2)?UISHADOW : 0; 
    GEngine->Draw2D(pars, rect);
  }
  else color = _color;

  base::SetTextColor(color);
  base::OnDraw(vp, alpha);
}

void CXButton::DoClick()
{
  PlaySound(_clickSound);
  if (_action.GetLength() > 0)
  {
    GameState *gstate = GWorld->GetGameState();
    GameVarSpace local(false);
    int gshandle = gstate->BeginContext(&local);
    gstate->Execute(_action, GameState::EvalContext::_default, UINamespace()); // UI namespace
    gstate->EndContext(gshandle);
    float GetTimeForScripts();
    GWorld->SimulateScripts(0, GetTimeForScripts());
  }
  if (OnCheckEvent(CEButtonClick)) return;
  if (_parent) _parent->OnButtonClicked(IDC());
}

///////////////////////////////////////////////////////////////////////////////
// class CShortcutButton

CShortcutButton::CShortcutButton(ControlsContainer *parent, int idc, ParamEntryPar cls)
: base(parent, idc, cls)
{
  _type = CT_SHORTCUTBUTTON;
  _enabled = true;

  _textureNormal = GlobLoadTextureUI(FindPicture(cls >> "animTextureNormal"));
  _textureDisabled = GlobLoadTextureUI(FindPicture(cls >> "animTextureDisabled"));
  _textureOver = GlobLoadTextureUI(FindPicture(cls >> "animTextureOver"));
  _textureFocused = GlobLoadTextureUI(FindPicture(cls >> "animTextureFocused"));
  _texturePressed = GlobLoadTextureUI(FindPicture(cls >> "animTexturePressed"));
  _textureDefault = GlobLoadTextureUI(FindPicture(cls >> "animTextureDefault"));

  _textureNoShortcut = GlobLoadTextureUI(FindPicture(cls >> "textureNoShortcut"));

  _color = GetPackedColor(cls >> "color");
  _color2 = GetPackedColor(cls >> "color2");
  _colorDisabled = GetPackedColor(cls >> "colorDisabled");
  _bgColor = GetPackedColor(cls >> "colorBackground");
  _bgColor2 = GetPackedColor(cls >> "colorBackground2");

  float period = 0.1f;
  ConstParamEntryPtr entry = cls.FindEntry("period");
  if (entry) period = *entry;
  _speed = period > 0 ? H_PI / period : 0;

  period = 0.1f;
  entry = cls.FindEntry("periodFocus");
  if (entry) period = *entry;
  _speedFocused = period > 0 ? H_PI / period : 0;

  period = 0.1f;
  entry = cls.FindEntry("periodOver");
  if (entry) period = *entry;
  _speedOver = period > 0 ? H_PI / period : 0;

  GetValue(_pushSound, cls >> "soundPush");
  GetValue(_clickSound, cls >> "soundClick");
  GetValue(_escapeSound, cls >> "soundEscape");

  entry = cls.FindEntry("action");
  if (entry) _action = *entry;

  // hit zone position
  _hitzoneLeft = cls >> "HitZone" >> "left";
  _hitzoneTop = cls >> "HitZone" >> "top";
  _hitzoneRight = cls >> "HitZone" >> "right";
  _hitzoneBottom = cls >> "HitZone" >> "bottom";

  // shortcut icon position
  _iconLeft = cls >> "ShortcutPos" >> "left";
  _iconTop = cls >> "ShortcutPos" >> "top";
  _iconWidth = cls >> "ShortcutPos" >> "w";
  _iconHeight = cls >> "ShortcutPos" >> "h";

  // text position
  _textLeft = cls >> "TextPos" >> "left";
  _textTop = cls >> "TextPos" >> "top";
  _textRight = cls >> "TextPos" >> "right";
  _textBottom = cls >> "TextPos" >> "bottom";

  _start = Glob.uiTime;
  _pressed = false;

  // set the control properties based on the list of shortcuts
  UpdateShortcuts();
}

void CShortcutButton::UpdateShortcuts()
{
  // disable button when some shortcut is available
  bool presentKeyboard = true;
  bool presentXInput = GInput.IsXInputPresent();
  _isWithXInput=GInput.IsXInputPresent();
  bool shorcutAvailable = 
    presentKeyboard && IsShortcutDevice(INPUT_DEVICE_KEYBOARD) && !OnlyComboShortcuts() ||
    presentXInput && IsShortcutDevice(INPUT_DEVICE_XINPUT);
  _keyboardEnabled = !shorcutAvailable;

  // find the first available shortcut
  for (int i=0; i<_shortcuts.Size(); i++)
  {
    int shortcut = _shortcuts[i];
    int device = shortcut & INPUT_DEVICE_MASK;

    // check if shortcut is available
    bool valid = 
      presentKeyboard && device == INPUT_DEVICE_KEYBOARD ||
      presentXInput && device == INPUT_DEVICE_XINPUT;
    if (!valid) continue;

    // add the shortcut
    KeyImage keyImage = GInput.GetKeyImage(shortcut);
    _textureShortcut = GlobLoadTextureUI(FindPicture(keyImage.image));
    if (_textureShortcut) return;
  }

  // when no image found, add the default one
  _textureShortcut = _textureNoShortcut;
}

bool CShortcutButton::IsInside(float x, float y) const
{
  return x >= _x + SCALED(_hitzoneLeft) && x <= _x + SCALED(_w - _hitzoneRight) &&
    y >= _y + SCALED(_hitzoneTop) && y <= _y + SCALED(_h - _hitzoneBottom);
}

bool CShortcutButton::IsReady() const
{
  bool ok = base::IsReady();

  if (!IsTextureReady(_textureNoShortcut)) ok = false;

  if (!IsAnimatedTextureReady(_textureNormal)) ok = false;
  if (!IsAnimatedTextureReady(_textureDisabled)) ok = false;
  if (!IsAnimatedTextureReady(_textureOver)) ok = false;
  if (!IsAnimatedTextureReady(_textureFocused)) ok = false;
  if (!IsAnimatedTextureReady(_texturePressed)) ok = false;
  if (!IsAnimatedTextureReady(_textureDefault)) ok = false;

  return ok;
}

bool CShortcutButton::OnKeyDown(int dikCode)
{
  if (!IsEnabled()) return false;

  if (ButtonPressed(dikCode))
  {
    if (!_pressed)
    {
      OnEvent(CEButtonDown);
      PlaySound(_pushSound);
      _pressed = true;
    }
    return true;
  }
  return false;
}

bool CShortcutButton::OnKeyUp(int dikCode)
{
  if (!IsEnabled()) return false;

  if (ButtonPressed(dikCode))
  {
    if (_pressed)
    {
      DoClick();
      _pressed = false;
    }
    return true;
  }
  return false;
}

bool CShortcutButton::OnShortcutDown()
{
  if (!_enabled) return false;

  if (!_pressed)
  {
    OnEvent(CEButtonDown);
    PlaySound(_pushSound);
    _pressed = true;
  }
  return true;
}

bool CShortcutButton::OnShortcutUp()
{
  if (!_enabled) return false;

  if (_pressed)
  {
    DoClick();
    _pressed = false;
  }
  return true;
}

bool CShortcutButton::OnKillFocus()
{
  _pressed = false;
  return Control::OnKillFocus();
}

void CShortcutButton::OnLButtonDown(float x, float y)
{
  OnEvent(CEButtonDown);
  PlaySound(_pushSound);
  _pressed = true;
}

void CShortcutButton::OnLButtonUp(float x, float y)
{
  if (IsInside(x, y))
  {
    DoClick();
  }
  else
  {
    OnEvent(CEButtonUp);
    PlaySound(_escapeSound);
  }
  _pressed = false;
}

void CShortcutButton::OnDraw(UIViewport *vp, float alpha)
{
  if (_isWithXInput!=GInput.IsXInputPresent())
  {
    UpdateShortcuts();
  }

  // select the colors and the background texture
  PackedColor color = _color;
  PackedColor bgColor = _bgColor;
  Texture *background = NULL;

  if (!_enabled)
  {
    color = _colorDisabled;
    background = _textureDisabled;
  }
  else if (_pressed)
  {
    background = _texturePressed;
  }
  else if (IsMouseOver())
  {
    background = _textureOver;
    if (_speedOver > 0) 
    {
      float factor = -0.5 * cos(_speedOver * (Glob.uiTime - _start)) + 0.5;
      Color col0 = _color, col1 = _color2;
      color = PackedColor(col0 * factor + col1 * (1 - factor));
      col0 = _bgColor; col1 = _bgColor2;
      bgColor = PackedColor(col0 * factor + col1 * (1 - factor));
    }
  }
  else if (IsFocused())
  {
    background = _textureFocused;
    if (_speedFocused > 0) 
    {
      float factor = -0.5 * cos(_speedFocused * (Glob.uiTime - _start)) + 0.5;
      Color col0 = _color, col1 = _color2;
      color = PackedColor(col0 * factor + col1 * (1 - factor));
      col0 = _bgColor; col1 = _bgColor2;
      bgColor = PackedColor(col0 * factor + col1 * (1 - factor));
    }
  }
  else if (GetDefault() == this)
  {
    background = _textureDefault;
  }
  else
  {
    background = _textureNormal;
  }

  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  // draw the background
  if (background)
  {
    Texture *texture = background;
    if (background->IsAnimated())
    {
      // animate
      int n = background->AnimationLength();
      int frame = toInt((Glob.uiTime - _start) * _speed) % n;
      texture = background->GetAnimation(frame);
    }

    Rect2DPixel clip(CX(_x), CY(_y), CW(SCALED(_w)), CH(SCALED(_h)));

    Draw2DParsExt pars;
    pars.mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
    pars.SetColor(ModAlpha(bgColor, alpha));
    pars.Init();
   // pars.spec|= (_shadow == 2)?UISHADOW : 0; 
    if (clip.w > 2 * clip.h)
    {
      Rect2DPixel rect(clip.x, clip.y, clip.h, clip.h); // 1:1 aspect ratio of the image
      pars.SetU(0, 0.25);
      vp->Draw2D(pars, rect, clip);
      rect.x += rect.w;
      rect.w = clip.w - 2 * clip.h;
      pars.SetU(0.25, 0.75);
      vp->Draw2D(pars, rect, clip);
      rect.x += rect.w;
      rect.w = clip.h;
      pars.SetU(0.75, 1);
      vp->Draw2D(pars, rect, clip);
    }
    else
    {
      Rect2DPixel rect(clip.x, clip.y, clip.h, clip.h); // 1:1 aspect ratio of the image
      pars.SetU(0, 0.25);
      vp->Draw2D(pars, rect, clip);
      rect.x += rect.w;
      //clip.x += rect.w;
      pars.SetU(0.75, 1);
      vp->Draw2D(pars, rect, clip);
    }
  }

  // draw the shortcut icon
  if (_textureShortcut)
  {
    Rect2DPixel rect(CX(_x + _iconLeft), CY(_y + _iconTop), CW(SCALED(_iconWidth)), CH(SCALED(_iconHeight)));
    Draw2DParsExt pars;
    pars.mip = GEngine->TextBank()->UseMipmap(_textureShortcut, 0, 0);
    pars.SetColor(ModAlpha(PackedWhite, alpha));
    pars.Init();
  //  pars.spec|= (_shadow == 2)?UISHADOW : 0; 
    vp->Draw2D(pars, rect);
  }

  // draw the structured text
  base::SetTextColor(color);
  base::SetShadow(_shadow);
  Rect2DFloat rect(_x + SCALED(_textLeft), _y + SCALED(_textTop),
    SCALED(_w - _textLeft - _textRight), SCALED(_h - _textTop - _textBottom));
  ::DrawText(vp, _text, rect, SCALED(_size), alpha);
}

void CShortcutButton::DoClick()
{
  PlaySound(_clickSound);
  if (_action.GetLength() > 0)
  {
    GameState *gstate = GWorld->GetGameState();
    GameVarSpace local(false);
    int gshandle = gstate->BeginContext(&local);
    gstate->Execute(_action, GameState::EvalContext::_default, UINamespace()); // UI namespace
    gstate->EndContext(gshandle);
    float GetTimeForScripts();
    GWorld->SimulateScripts(0, GetTimeForScripts());
  }
  if (OnCheckEvent(CEButtonClick)) return;
  if (_parent) _parent->OnButtonClicked(IDC());
}

///////////////////////////////////////////////////////////////////////////////
// class CEditContainer

CEditContainer::CEditContainer(ParamEntryPar cls)
{
  _ftColor = GetPackedColor(cls >> "colorText");
  _selColor = GetPackedColor(cls >> "colorSelection");
  _font = GLOB_ENGINE->LoadFont(GetFontID(cls>>"font"));

  _enableToolip = false;
  _firstVisible = 0;
  _blockBegin = 0;
  _blockEnd = 0;

  _maxChars = INT_MAX;

  _size = 0;

  _forceDrawCaret = false;
  ConstParamEntryPtr entry = cls.FindEntry("forceDrawCaret");
  if (entry) _forceDrawCaret = *entry;
  
  RString autoComplete = cls>>"autocomplete";
  SetAutoComplete(CreateAutoComplete(autoComplete));

#if _VBS3
  _htmlControl = cls.ReadValue("htmlControl",false);
#endif
}

void CEditContainer::SetAutoComplete(IAutoComplete *ac)
{
  _autoComplete = ac;
}

bool CEditContainer::SetTextInternal(RWString text)
{
  if (text.GetLength() <= _maxChars) _textUnicode = text;
  else _textUnicode = text.Substring(0, _maxChars);
  
  FormatText();

  int n = _textUnicode.GetLength();
  saturateMin(_blockBegin, n);
  saturateMin(_blockEnd, n);
  saturateMin(_firstVisible, n);
  EnsureVisible(_blockEnd);

  return true;
}


#if _VBS3
void SearchAndReplace(RString &text,const RString remove,const RString insert)
{
  int pos = 0; 
  while(pos != -1)
  {
    pos = text.Find(remove.Data());
    if(pos != -1)
      text = text.Substring(0,pos) + insert + text.Substring(pos+remove.GetLength(),text.GetLength());
  }
}
#endif

RString CEditContainer::GetText() const
{
#if _VBS3
  // on get text convert all the \n into <BR>
  RString conversion = UnicodeToUTF(_textUnicode);
  if( _htmlControl ) SearchAndReplace(conversion,"\n","<br>");
  return conversion;
#else
  return UnicodeToUTF(_textUnicode);
#endif
}

bool CEditContainer::SetText(RString text)
{
#if _VBS3
  RString newText = text;
  if(_htmlControl) SearchAndReplace(newText,"<br>","\n");
  SetTextInternal(UTFToUnicode(newText));
  FormatText();
#else
  SetTextInternal(UTFToUnicode(text));
#endif
  return true;
}

void CEditContainer::SetCaretPos(int pos)
{
  int n = wcslen(_textUnicode);
  saturate(pos, 0, n);
  _blockBegin = pos;
  _blockEnd = pos;
  EnsureVisible(pos);
}

void CEditContainer::SetBlock(int begin, int end)
{
  int n = wcslen(_textUnicode);
  saturate(begin, 0, n);
  saturate(end, 0, n);
  _blockBegin = begin;
  _blockEnd = end;
  EnsureVisible(end);
}

int CEditContainer::NextPos(int pos)
{
  return pos + 1;
}

int CEditContainer::PrevPos(int pos)
{
  return pos - 1;
}

/*!
\patch 1.28 Date 10/31/2001 by Jirka
- Fixed: Clipboard operations enabled in chat line
\patch 1.30 Date 11/01/2001 by Ondra
- Fixed: Clipboard operations not working in Win9x (since 1.28)
*/
bool CEditContainer::DoKeyDown(int dikCode)
{
  int n = wcslen(_textUnicode);
  Assert(_blockBegin >= 0);
  Assert(_blockBegin <= n);
  Assert(_blockEnd >= 0);
  Assert(_blockEnd <= n);
  switch (dikCode)
  {
  case DIK_LEFT:
    if (_blockEnd > 0)
    {
      _blockEnd = PrevPos(_blockEnd);
      if
      (
        _blockEnd > 0 &&
        (GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL])
      )
      {
        const wchar_t *text = _textUnicode;
        while (true)
        {
          if
          (
            ISSPACE(text[_blockEnd - 1]) &&
            !ISSPACE(text[_blockEnd])
          ) break;
          _blockEnd = PrevPos(_blockEnd);
          if (_blockEnd <= 0) break;
        }
      }
      EnsureVisible(_blockEnd);
      _enableToolip = false;
    }
    if (!GInput.keys[DIK_LSHIFT] && !GInput.keys[DIK_RSHIFT])
      _blockBegin = _blockEnd;
    return true;
  case DIK_RIGHT:
    if (_blockEnd < n)
    {
      _blockEnd = NextPos(_blockEnd);
      if
      (
        _blockEnd < n &&
        (GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL])
      )
      {
        const wchar_t *text = _textUnicode;
        while (true)
        {
          if
          (
            ISSPACE(text[_blockEnd - 1]) &&
            !ISSPACE(text[_blockEnd])
          ) break;
          _blockEnd = NextPos(_blockEnd);
          if (_blockEnd >= n) break;
        }
      }
      EnsureVisible(_blockEnd);
      _enableToolip = false;
    }
    if (!GInput.keys[DIK_LSHIFT] && !GInput.keys[DIK_RSHIFT])
      _blockBegin = _blockEnd;
    return true;
  case DIK_UP:
    if (IsMulti())
    {
      int line = CurLine();
      if (line > 0)
      {
        RWString text = GetLine(line);
        int pos = _blockEnd - _lines[line];
        float x = PosToX(text, pos);
        line--;
        text = GetLine(line);
        pos = XToPos(text, x);
        _blockEnd = _lines[line] + pos;
        if(GetLine(line).GetLength() < GetLine(line+1).GetLength())
          _blockEnd = _lines[line+1]-1;
        EnsureVisible(_blockEnd);
        _enableToolip = false;
        if (!GInput.keys[DIK_LSHIFT] && !GInput.keys[DIK_RSHIFT])
          _blockBegin = _blockEnd;
      }
      return true;
    }
    else
      return false;
  case DIK_DOWN:
    if (IsMulti())
    {
      int line = CurLine();
      if (line < _lines.Size() - 1)
      {
        RWString text = GetLine(line);
        int pos = _blockEnd - _lines[line];
        float x = PosToX(text, pos);
        line++;
        text = GetLine(line);
        pos = XToPos(text, x);
        _blockEnd = _lines[line] + pos;
        if((line+1) < _lines.Size())
          if(_blockEnd > (_lines[line+1]-1))
            _blockEnd = _lines[line+1] - 1;
        EnsureVisible(_blockEnd);
        _enableToolip = false;
        if (!GInput.keys[DIK_LSHIFT] && !GInput.keys[DIK_RSHIFT])
          _blockBegin = _blockEnd;
      }
      return true;
    }
    else
      return false;
  case DIK_HOME:
    {
      int cLine    = CurLine();
      if(IsMulti() && (cLine < _lines.Size()))
        _blockEnd = _lines[cLine];
      else
        _blockEnd = 0;
      _enableToolip = false;
      EnsureVisible(_blockEnd);
      if (!GInput.keys[DIK_LSHIFT] && !GInput.keys[DIK_RSHIFT])
        _blockBegin = _blockEnd;
      return true;
    }
  case DIK_END:
    {
      int cLine    = CurLine() + 1;

      if(IsMulti() && (cLine < _lines.Size()))
        _blockEnd = (_lines[cLine] - 1);// move back one char
      else
        _blockEnd = n;
      _enableToolip = true;
      EnsureVisible(_blockEnd);
      if (!GInput.keys[DIK_LSHIFT] && !GInput.keys[DIK_RSHIFT])
        _blockBegin = _blockEnd;
      return true;
    }
  case DIK_BACK:
    if (_blockBegin != _blockEnd)
    {
      BlockDelete();
      return true;
    }
    if (_blockEnd <= 0) return true;
    _blockEnd = PrevPos(_blockEnd);
    _blockBegin = _blockEnd;
    _enableToolip = true;
    goto DeleteChar;
  case DIK_DELETE:
    if (_blockBegin != _blockEnd)
    {
      if (GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT])
        BlockCut();
      else
        BlockDelete();
      return true;
    }
    if (_blockEnd >= n) return true;
    _enableToolip = true;
DeleteChar:
    {
      SetTextInternal
      (
        _textUnicode.Substring(0, _blockEnd) +
        _textUnicode.Substring(NextPos(_blockEnd), _textUnicode.GetLength())
      );
      // EnsureVisible is called from SetText
    }
    return true;
  case DIK_INSERT:
    if (GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT])
    {
      if (_blockBegin != _blockEnd) BlockDelete();
      BlockPaste();
    }
    else if (GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL])
      BlockCopy();
    return true;
  case DIK_TAB:
  {
    if (_blockBegin!=_blockEnd || !_autoComplete || !_enableToolip) return false;
    bool certain;
    RString beg;
    RString tip = _autoComplete->Guess(UnicodeToUTF(_textUnicode),_blockEnd,certain,beg);
    if (tip.GetLength() <= 0) return false;

    RWString wTip = UTFToUnicode(tip);
    if (wTip.GetLength() + _textUnicode.GetLength() <= _maxChars)
    {
      RWString wBeg = UTFToUnicode(beg);

      int start = _blockEnd - wBeg.GetLength();
      saturateMax(start, 0);
      RWString text =
        _textUnicode.Substring(0, start) +
        wTip + 
        _textUnicode.Substring(_blockEnd, _textUnicode.GetLength());
      SetTextInternal(text);
      _blockBegin = _blockEnd = start + wTip.GetLength();
      EnsureVisible(_blockEnd);
    }

    return true;
  }
    
  case DIK_X:
    if (GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL])
    {
      BlockCut();
    }
    return true;
  case DIK_C:
    if (GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL])
    {
      BlockCopy();
    }
    return true;
  case DIK_V:
    if (GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL])
    {
      if (_blockBegin != _blockEnd) BlockDelete();
      BlockPaste();
    }
    return true;
  case DIK_RETURN:
  case DIK_NUMPADENTER:
      if (IsMulti())
      {
        int line = CurLine();
        if (line < _lines.Size()) DoChar('\n',0,0);
        return true;
      }
  case DIK_ESCAPE:
  case INPUT_DEVICE_XINPUT + XBOX_Up:
  case INPUT_DEVICE_XINPUT + XBOX_Down:
  case INPUT_DEVICE_XINPUT + XBOX_Left:
  case INPUT_DEVICE_XINPUT + XBOX_Right:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYUp:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYDown:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXLeft:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXRight:
    return false;
  }

  // all other keys are caught by control
  return true;
}

bool CEditContainer::DoKeyUp(int dikCode)
{
  switch (dikCode)
  {
  case DIK_UP:
  case DIK_DOWN:
  case DIK_RETURN:
  case DIK_NUMPADENTER:
    return IsMulti();
  case DIK_ESCAPE:
    return false;
  }

  // all other keys are caught by control
  return true;
}

bool CEditContainer::DoChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  int n = wcslen(_textUnicode);
  if (n >= _maxChars) return true; // TODO: sound

  Assert(_blockBegin >= 0);
  Assert(_blockBegin <= n);
  Assert(_blockEnd >= 0);
  Assert(_blockEnd <= n);
  wchar_t newChar[2];
  newChar[0] = nChar;
  newChar[1] = 0;

  if (_blockBegin != _blockEnd) BlockDelete();

  RWString data =
  (
    _textUnicode.Substring(0, _blockEnd) +
    RWString(newChar) +
    _textUnicode.Substring(_blockEnd, n)
  );

  SetTextInternal(data);
  _blockEnd++;
  _blockBegin = _blockEnd;
  if (_autoComplete) _autoComplete->AfterChar(UnicodeToUTF(_textUnicode), _blockEnd);
  _enableToolip = true;
  EnsureVisible(_blockEnd);

  return true;
}

bool CEditContainer::DoIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  // TODO: correct implementation whenever needed
/*
  int n = strlen(_textInternal);
  if (n + 2 > _maxChars) return true; // TODO: sound

  Assert(_blockBegin >= 0);
  Assert(_blockBegin <= n);
  Assert(_blockEnd >= 0);
  Assert(_blockEnd <= n);
  char newChar[3];
  newChar[0] = (nChar >> 8) & 0xff;
  newChar[1] = nChar & 0xff;
  newChar[2] = 0;

  if (_blockBegin != _blockEnd) BlockDelete();

  RString data =
  (
    _textInternal.Substring(0, _blockEnd) +
    RString(newChar) +
    _textInternal.Substring(_blockEnd, n)
  );

  SetTextInternal(data);
  _blockEnd += 2;
  _blockBegin = _blockEnd;
  if (_autoComplete) _autoComplete->AfterChar(_textInternal,_blockEnd);
  _enableToolip = true;
  EnsureVisible(_blockEnd);
*/
  return true;
}

bool CEditContainer::DoIMEComposition(unsigned nChar, unsigned nFlags)
{
  // TODO: correct implementation whenever needed
/*
  int n = strlen(_textInternal);
  if (nChar != 0 && n + 2 > _maxChars) return true; // TODO: sound

  Assert(_blockBegin >= 0);
  Assert(_blockBegin <= n);
  Assert(_blockEnd >= 0);
  Assert(_blockEnd <= n);

  if (_blockBegin != _blockEnd) BlockDelete();
  if (nChar == 0)
  {

    return true;
  }

  char newChar[3];
  newChar[0] = (nChar >> 8) & 0xff;
  newChar[1] = nChar & 0xff;
  newChar[2] = 0;

  RString data =
  (
    _textInternal.Substring(0, _blockEnd) +
    RString(newChar) +
    _textInternal.Substring(_blockEnd, n)
  );

  SetTextInternal(data);
  _blockBegin = _blockEnd;
  _blockEnd += 2;
  if (_autoComplete) _autoComplete->AfterChar(_textInternal,_blockEnd);
  _enableToolip = true;
  EnsureVisible(_blockEnd);
*/
  return true;
}

int CEditContainer::CurLine() const
{
  int n = _lines.Size();
  for (int i=1; i<n; i++)
    if (_blockEnd < _lines[i])
    {
      return i - 1;
    }
  return n - 1;
}

int CEditContainer::FirstLine() const
{
  int n = _lines.Size();
  for (int i=1; i<n; i++)
    if (_firstVisible < _lines[i])
    {
      return i - 1;
    }
  return n - 1;
}

RWString CEditContainer::GetLine(int i) const
{
  if (i < 0) return RWString();
  //Assert((_style & ST_TYPE) == ST_MULTI);
  Assert(i < _lines.Size());

  int from = _lines[i];
  int to = i + 1 < _lines.Size() ? _lines[i + 1] : _textUnicode.GetLength();
  return _textUnicode.Substring(from, to);
}

void CEditContainer::BlockDelete()
{
#ifdef ENABLE_CLIPBOARD
  if (_blockBegin == _blockEnd) return;

  int from, to;
  if (_blockBegin < _blockEnd)
  {
    from = _blockBegin;
    to = _blockEnd;
  }
  else
  {
    from = _blockEnd;
    to = _blockBegin;
  }
  RWString text = _textUnicode.Substring(0, from) + _textUnicode.Substring(to, _textUnicode.GetLength());
  _blockBegin = _blockEnd = from;
  SetTextInternal(text);
#endif
}

/*!
\patch_internal 1.44 Date 2/11/2002 by Ondra
- Fixed: Windows clipboard copy sometimes did not work correctly
when non-text data were already stored in cliboard.
*/

void CEditContainer::BlockCopy()
{
#ifdef ENABLE_CLIPBOARD
  if (_blockBegin == _blockEnd) return;

  int from, to;
  if (_blockBegin < _blockEnd)
  {
    from = _blockBegin;
    to = _blockEnd;
  }
  else
  {
    from = _blockEnd;
    to = _blockBegin;
  }
  RWString text = _textUnicode.Substring(from, to);
  ExportUnicodeToClipboard(text, text.GetLength());
#endif
}

void CEditContainer::BlockCut()
{
#ifdef ENABLE_CLIPBOARD
  BlockCopy();
  BlockDelete();
#endif
}

/*!
\patch 1.23 Date 9/17/2001 by Jirka
- Fixed: Do not allow paste End of Line character in edit control.
*/
void CEditContainer::BlockPaste()
{
#ifdef ENABLE_CLIPBOARD
  RWString result = ImportUnicodeFromClipboard();
  int n = result.GetLength();
  if (n > 0)
  {
    wchar_t *ptr = result.MutableData();
    for (int i=0; i<n; i++)
    {
      switch (ptr[i])
      {
      case '\n':
        if(IsMulti()) break;
      case '\r':
      case '\t':
        ptr[i] = ' ';
        break;
      }
    }
    if (n + _textUnicode.GetLength() <= _maxChars)
    {
      RWString text =
        _textUnicode.Substring(0, _blockEnd) +
        result + 
        _textUnicode.Substring(_blockEnd, _textUnicode.GetLength());
      SetTextInternal(text);
      _blockBegin = _blockEnd = _blockEnd + n;
      EnsureVisible(_blockEnd);
    }
  }
#endif
}

///////////////////////////////////////////////////////////////////////////////
// class DisplayScriptingHelp

/// Dialog displaying the help for the given scripting command
class DisplayScriptingHelp : public Display
{
protected:
  InitPtr<CHTMLContainer> _content;
  bool _parentShown;
  /// history of shown pages
  AutoArray<RString> _history;
  /// current position in the _history
  int _historyIndex;

public:
  DisplayScriptingHelp(ControlsContainer *parent, RString text);
  ~DisplayScriptingHelp();
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);

  void OnButtonClicked(int idc);
  bool OnHTMLLink(int idc, RString link);

protected:
  /// Open the referenced link in _content
  bool ProcessLink(RString link);
  /// load the help for the given keyword
  void LoadContent(RString text);
  /// add item to the history
  void StoreHistory(RString href);
  /// update Back / Forward buttons
  void UpdateButtons();
  /// if example is listed, return it as a text
  RString GetExample() const;
};

DisplayScriptingHelp::DisplayScriptingHelp(ControlsContainer *parent, RString text)
: Display(parent)
{
  Load("RscDisplayScriptingHelp");

  // force invoking display to be visible
  if (_parent)
  {
    // keep the previous state to restore it
    _parentShown = _parent->AlwaysShow();
    _parent->SetAlwaysShow(true);

    _enableSimulation = _parent->SimulationEnabled();
  }

  _historyIndex = -1;
  if (ProcessLink(text))
    StoreHistory(text);
}

DisplayScriptingHelp::~DisplayScriptingHelp()
{
  // restore the original visibility of invoking display
  if (_parent)
  {
    _parent->SetAlwaysShow(_parentShown);
  }
}

Control *DisplayScriptingHelp::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_SCRITING_HELP:
    _content = GetHTMLContainer(ctrl);
    break;
  }
  return ctrl;
}

void DisplayScriptingHelp::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_SCRITING_HELP_BACK:
    if (_historyIndex > 0)
    {
      _historyIndex--;
      ProcessLink(_history[_historyIndex]);
      UpdateButtons();
    }
    break;
  case IDC_SCRITING_HELP_FORWARD:
    if (_historyIndex < _history.Size() - 1)
    {
      _historyIndex++;
      ProcessLink(_history[_historyIndex]);
      UpdateButtons();
    }
    break;
  case IDC_SCRITING_HELP_EXAMPLE:
    {
      RString example = GetExample();
      if (example.GetLength() > 0) ExportToClipboard(example, example.GetLength());
    }
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

bool DisplayScriptingHelp::OnHTMLLink(int idc, RString link)
{
  if (idc != IDC_SCRITING_HELP) return false;
  
  if (ProcessLink(link))
  {
    StoreHistory(link);
    return true;
}

  return false;
}

bool DisplayScriptingHelp::ProcessLink(RString link)
{
  const char *ptr = link;
  const char *name = "type:";
  int n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
#if _ENABLE_COMREF
    RString href = ptr + n;
    href.Lower();
    const GameTypeType *type = GGameState.FindType(href);
    if (type)
    {
      LoadContent(type->FormatHelp());
      return true;
    }
#endif
    return false;
  }
  name = "nular:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
#if _ENABLE_COMREF
    RString href = ptr + n;
    href.Lower();
    const GameNular *nular = GGameState.FindNular(href);
    if (nular)
    {
      LoadContent(nular->FormatHelp());
      return true;
    }
#endif
    return false;
  }
  name = "function:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
#if _ENABLE_COMREF
    ptr += n;
    const char *ext = strchr(ptr, ':');
    if (ext)
    {
      RString href(ptr, ext - ptr);
      href.Lower();
      int index = atoi(ext + 1);
      const GameFunctions *functions = GGameState.FindFunctions(href);
      if (functions && index >= 0 && index < functions->Size())
      {
        LoadContent((*functions)[index].FormatHelp());
        return true;
      }
    }
#endif
    return false;
  }
  name = "operator:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
#if _ENABLE_COMREF
    ptr += n;
    const char *ext = strchr(ptr, ':');
    if (ext)
    {
      RString href(ptr, ext - ptr);
      href.Lower();
      int index = atoi(ext + 1);
      const GameOperators *operators = GGameState.FindOperators(href);
      if (operators && index >= 0 && index < operators->Size())
      {
        LoadContent((*operators)[index].FormatHelp());
        return true;
      }
    }
#endif
    return false;
  }
  return false;
}

void DisplayScriptingHelp::LoadContent(RString text)
{
  if (_content)
  {
    _content->Init();
    // parse the content
    QIStrStream in(cc_cast(text), text.GetLength());
    _content->Load(in);
  }
}

void DisplayScriptingHelp::StoreHistory(RString href)
{
  _historyIndex++;
  saturateMax(_historyIndex, 0);
  _history.Resize(_historyIndex + 1);
  _history[_historyIndex] = href;

  UpdateButtons();
}

void DisplayScriptingHelp::UpdateButtons()
{
  IControl *ctrl = GetCtrl(IDC_SCRITING_HELP_BACK);
  if (ctrl) ctrl->EnableCtrl(_historyIndex > 0);
  ctrl = GetCtrl(IDC_SCRITING_HELP_FORWARD);
  if (ctrl) ctrl->EnableCtrl(_historyIndex < _history.Size() - 1);
  ctrl = GetCtrl(IDC_SCRITING_HELP_EXAMPLE);
  if (ctrl)
  {
    RString example = GetExample();
    ctrl->EnableCtrl(example.GetLength() > 0);
  }
}

RString DisplayScriptingHelp::GetExample() const
{
  if (_historyIndex < 0 || _historyIndex >= _history.Size()) return RString();
  RString link = _history[_historyIndex];

  const char *ptr = link;
  const char *name = "type:";
  int n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
    // no example for type
    return RString();
  }
  name = "nular:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
#if _ENABLE_COMREF
    RString href = ptr + n;
    href.Lower();
    const GameNular *nular = GGameState.FindNular(href);
    if (nular) return nular->_example;
#endif
    return RString();
  }
  name = "function:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
#if _ENABLE_COMREF
    ptr += n;
    const char *ext = strchr(ptr, ':');
    if (ext)
    {
      RString href(ptr, ext - ptr);
      href.Lower();
      int index = atoi(ext + 1);
      const GameFunctions *functions = GGameState.FindFunctions(href);
      if (functions && index >= 0 && index < functions->Size())
      {
        return (*functions)[index]._example;
      }
    }
#endif
    return RString();
  }
  name = "operator:";
  n = strlen(name);
  if (strnicmp(ptr, name, n) == 0)
  {
#if _ENABLE_COMREF
    ptr += n;
    const char *ext = strchr(ptr, ':');
    if (ext)
    {
      RString href(ptr, ext - ptr);
      href.Lower();
      int index = atoi(ext + 1);
      const GameOperators *operators = GGameState.FindOperators(href);
      if (operators && index >= 0 && index < operators->Size())
      {
        return (*operators)[index]._example;
      }
    }
#endif
    return RString();
  }
  return RString();
}

///////////////////////////////////////////////////////////////////////////////
// class CEdit

CEdit::CEdit(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : Control(parent, CT_EDIT, idc, cls), CEditContainer(cls)
{
  _size = cls >> "sizeEx";

  RString text = cls>>"text";
  SetText(text);
  _caretBlinkTime = Glob.uiTime.toFloat();
  // note: SetText does EnsureVisible
}

bool CEdit::IsMulti() const
{
  return (_style & ST_TYPE) == ST_MULTI;
}

float CEdit::PosToX(UIViewport *vp, RWString text, int pos) const
{
  float left;
  int style = ST_LEFT;
  if ((_style & ST_TYPE) == ST_MULTI || _firstVisible == 0)
    style = _style & ST_HPOS;
  float size = _scale * SCALED(_size);
  TextDrawAttr attr(size, _font, PackedBlack, _shadow);
  switch (style)
  {
  case ST_RIGHT:
    left = _x + SCALED(_w) - vp->GetTextWidth(attr, UnicodeToUTF(text)) - _scale * SCALED(textBorder);
    break;
  case ST_CENTER:
    left = _x + 0.5 * (SCALED(_w) - vp->GetTextWidth(attr, UnicodeToUTF(text)));
    break;
  default:
    Assert((_style & ST_HPOS) == ST_LEFT)
      left = _x + _scale * SCALED(textBorder);
    break;
  }

  RWString str = text.Substring(0, pos);
  return left + vp->GetTextWidth(attr, UnicodeToUTF(str));
}

float CEdit::PosToX(RWString text, int pos) const
{
  float left;
  int style = ST_LEFT;
  if ((_style & ST_TYPE) == ST_MULTI || _firstVisible == 0)
    style = _style & ST_HPOS;
  float size = _scale * SCALED(_size);
  switch (style)
  {
    case ST_RIGHT:
      left = _x + SCALED(_w) - GLOB_ENGINE->GetTextWidth(size, _font, UnicodeToUTF(text)) - _scale * SCALED(textBorder);
      break;
    case ST_CENTER:
      left = _x + 0.5 * (SCALED(_w) - GLOB_ENGINE->GetTextWidth(size, _font, UnicodeToUTF(text)));
      break;
    default:
      Assert((_style & ST_HPOS) == ST_LEFT)
      left = _x + _scale * SCALED(textBorder);
      break;
  }

  RWString str = text.Substring(0, pos);
  return left + GLOB_ENGINE->GetTextWidth(size, _font, UnicodeToUTF(str));
}

int CEdit::XToPos(RWString text, float x) const
{
  float left;
  int style = ST_LEFT;
  if ((_style & ST_TYPE) == ST_MULTI || _firstVisible == 0)
    style = _style & ST_HPOS;
  float size = _scale * SCALED(_size);
  switch (style)
  {
    case ST_RIGHT:
      left = _x + SCALED(_w) - GLOB_ENGINE->GetTextWidth(size, _font, UnicodeToUTF(text)) - _scale * SCALED(textBorder);
      break;
    case ST_CENTER:
      left = _x + 0.5 * (SCALED(_w) - GLOB_ENGINE->GetTextWidth(size, _font, UnicodeToUTF(text)));
      break;
    default:
      Assert((_style & ST_HPOS) == ST_LEFT)
      left = _x + _scale * SCALED(textBorder);
      break;
  }

  if (x < left) return 0;
  
  int n = wcslen(text);
  for (int i=0; i<n; i++)
  {
    int j = i;
    wchar_t c = text[i];
    wchar_t temp[2]; temp[0] = c; temp[1] = 0;
    float cw = GLOB_ENGINE->GetTextWidth(size, _font, UnicodeToUTF(temp));
    if (left + cw > x)
    {
      if (x - left < left + cw - x)
        return j;
      else
        return i + 1;
    }
    left += cw;
  }
  return n;
}

/*!
\patch 5149 Date 3/27/2007 by Jirka
- Fixed: UI - text cursor in edit control was on wrong position
*/

void CEdit::DrawText(UIViewport *vp, const wchar_t *text, int offset, float x, float y, float alpha, const Rect2DFloat &clipRect)
{
  int pos = ST_LEFT;
  if ((_style & ST_TYPE) == ST_MULTI || _firstVisible == 0)
    pos = _style & ST_HPOS;
  float size = _scale * SCALED(_size);

  PackedColor ftColor = ModAlpha(_ftColor, alpha);
  TextDrawAttr attr(size, _font, ftColor, _shadow);
  
  float left;
  switch (pos)
  {
    case ST_RIGHT:
      left = x + SCALED(_w) - vp->GetTextWidth(attr, UnicodeToUTF(text)) - _scale * SCALED(textBorder);
      break;
    case ST_CENTER:
      left = x + 0.5 * (SCALED(_w) - vp->GetTextWidth(attr, UnicodeToUTF(text)));
      break;
    default:
      Assert(pos == ST_LEFT)
      left = x + _scale * SCALED(textBorder);
      break;
  }
  
  if (_blockBegin != _blockEnd)
  {
    int from, to;
    if (_blockBegin < _blockEnd)
    {
      from = _blockBegin;
      to = _blockEnd;
    }
    else
    {
      from = _blockEnd;
      to = _blockBegin;
    }
    if (from < offset + (int)wcslen(text) && to > offset)
    {
      PackedColor selColor = ModAlpha(_selColor, alpha);
      float x1 = 0;
//      char buffer[1024];
      if (from > offset)
      {
        RWString prefix(text, from - offset);
        x1 = vp->GetTextWidth(attr, UnicodeToUTF(prefix));
      }
      float x2 = 0;
      if (to < offset + (int)wcslen(text))
      {
        RWString prefix(text, to - offset);
        x2 = vp->GetTextWidth(attr, UnicodeToUTF(prefix));
      }
      else
        x2 = vp->GetTextWidth(attr, UnicodeToUTF(text));

      const int w = GLOB_ENGINE->Width2D();
      const int h = GLOB_ENGINE->Height2D();

      Draw2DParsExt pars;
      pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
      pars.SetColor(selColor);
      pars.Init();
      pars.spec|= (_shadow == 2)?UISHADOW : 0; 

      Rect2DFloat clip;
      Intersect(clip, clipRect, x + _scale * SCALED(textBorder), y, SCALED(_w) - 2.0 * _scale * SCALED(textBorder), SCALED(_h));

      vp->Draw2D
      (
        pars,
        Rect2DPixel((left + x1) * w, y * h, (x2 - x1) * w, size * h),
        Rect2DPixel(CX(clip.x), CY(clip.y), CW(clip.w), CH(clip.h))
      );
    }
  }

  vp->DrawText
  (
    Point2DFloat(left, y), clipRect, attr, UnicodeToUTF(text)
  );
}

void CEdit::OnDraw(UIViewport *vp, float alpha)
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  float xx = CX(_x);
  float yy = CY(_y);
  float ww = CW(SCALED(_w));
  float hh = CH(SCALED(_h));

  PackedColor ftColor = ModAlpha(_ftColor, alpha);

  Rect2DFloat clipRect(_x, _y, SCALED(_w), SCALED(_h));

  if ((_style & ST_NO_RECT) == 0)
  {
    DrawLeft(0, ftColor);
    DrawTop(0, ftColor)
    DrawBottom(0, ftColor);
    DrawRight(0, ftColor);
  }
  
  float size = _scale * SCALED(_size);
  float height = size;

  if (_textUnicode.GetLength() > 0)
  {
    if ((_style & ST_TYPE) == ST_MULTI)
    {
      float top = _y;
      for (int i=FirstLine(); i<_lines.Size(); i++)
      {
        DrawText(vp, GetLine(i), _lines[i], _x, top, alpha, clipRect);
        top += height;
        if (top >= _y + SCALED(_h)) break;
      }
    }
    else
    {
      float top = _y + 0.5 * (SCALED(_h) - height);
      DrawText
      (
        vp, _textUnicode.Substring(_firstVisible, _textUnicode.GetLength()),
        _firstVisible, _x, top, alpha, clipRect
      );
    }
  }

  // draw caret
  float top = 0, left = 0;
  if (IsFocused() || _forceDrawCaret)
  {
    if ((_style & ST_TYPE) == ST_MULTI)
    {
      int cur = CurLine();
      int first = FirstLine();
      top = _y + (cur - first) * height;
      left = CX(PosToX
      (
        vp, GetLine(cur),
        _blockEnd - (cur < 0 ? 0 : _lines[cur])
      ));
    }
    else
    {
      top = _y + 0.5 * (SCALED(_h) - height);
      left = CX(PosToX
      (
        vp, _textUnicode.Substring(_firstVisible, _textUnicode.GetLength()),
        _blockEnd - _firstVisible
      ));
    }
    float state = (Glob.uiTime.toFloat() - _caretBlinkTime);
    if (state < 0.5)
    {
      // fix: draw caret prior character, not on the beginning
      vp->DrawLine
      (
        Line2DPixel(left - 1, CY(top), left - 1, CY(top + height)),
        ftColor, ftColor
      );
    }
    else if(state > 1) _caretBlinkTime = Glob.uiTime.toFloat();
  }

  // draw auto-complete
  if (!IsFocused()) return;
  if (_blockBegin==_blockEnd && _autoComplete && _enableToolip)
  {
    bool certain = false;
    RString beg;
    RString tip = _autoComplete->Guess(UnicodeToUTF(_textUnicode),_blockEnd, certain, beg);

    if (tip.GetLength() == 0)
    {
      // clear the autocomplete
      _lastTip = tip;
      _lastTipFormated = NULL;
    }
    else
    {
      // avoid allocation in each frame
      if (tip != _lastTip)
      {
        _lastTip = tip;
        _lastTipFormated = _autoComplete->ShowGuess(tip);
      }
      
      if (_lastTipFormated)
      {
        PackedColor colorBackground(Color(0, 0, 0, 0.3));
        PackedColor colorText = PackedWhite;
        PackedColor colorLines(Color(1, 1, 1, 0.3));
        if (!certain)
        {
          colorText = PackedBlack;
          colorBackground = PackedColor(Color(0.95,0.95,0.95, 0.3));
        }

        // set text attributes
        ::SetTextColor(_lastTipFormated, colorText);
        ::SetTextFont(_lastTipFormated, _tooltipFont);

        const float border = SCALED(TEXT_BORDER);

        // decide the size of the structured text
        float width = GetTextWidthFloat(_lastTipFormated, SCALED(_tooltipSize))+ 0.002; //Hladas: fix rounding problems (this is not mine solution)
        saturateMin(width, 1.0f - 2.0f * border); // fit on the screen
        Rect2DFloat rect(0, 0, width, 1.0f);
        float height = GetTextHeight(_lastTipFormated, rect, SCALED(_tooltipSize));
        rect.h = height;
        // position of the structured text
        rect.x = (left / w) - 0.5f * width;
        rect.y = top - SCALED(border) - height;
        saturate(rect.x, border, 1.0f - border - width);
        if (rect.y < border && top + size + border + height + border < 1.0f)
        {
          // better fit below the text
          rect.y = top + size + border;
        }
        else
        {
          saturate(rect.y, border, 1.0f - border - height);
        }

        // size and position of the background box
        float bx = rect.x - border;
        float by = rect.y - border;
        float ex = rect.x + rect.w + border;
        float ey = rect.y + rect.h + border;

        // draw the background
        Draw2DParsExt pars;
        pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
        pars.SetColor(colorBackground);
        pars.Init();
        pars.spec|= (_shadow == 2)?UISHADOW : 0; 
        vp->Draw2DNoClip(pars, Rect2DPixel(bx * w, by * h, (ex - bx) * w, (ey - by) * h));
        vp->DrawLineNoClip(Line2DPixel(bx * w, by * h, ex * w, by * h), colorLines, colorLines);
        vp->DrawLineNoClip(Line2DPixel(ex * w, by * h, ex * w, ey * h), colorLines, colorLines);
        vp->DrawLineNoClip(Line2DPixel(ex * w, ey * h, bx * w, ey * h), colorLines, colorLines);
        vp->DrawLineNoClip(Line2DPixel(bx * w, ey * h, bx * w, by * h), colorLines, colorLines);
        // draw the text
        ::DrawText(vp, _lastTipFormated, rect, SCALED(_tooltipSize), 1.0f, NULL, true);

/*
        TextDrawAttr attr(_tooltipSize, _tooltipFont, colorText);

        float x = left / w;
        float y = top;
        const float border = TEXT_BORDER;
        float width = vp->GetTextWidth(attr, tip);
        float begW = vp->GetTextWidth(attr, beg);

        float bx = x - begW;
        //    float by = y - 0.5 * height - border;
        float by = y - _tooltipSize - 2.0 * border;
        float ex = bx + width + 2.0 * border;
        float ey = by + _tooltipSize + 2.0 * border;

        Draw2DParsExt pars;
        pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
        pars.SetColor(colorBackground);
        pars.Init();
        vp->Draw2DNoClip(pars, Rect2DPixel(bx * w, by * h, (ex - bx) * w, (ey - by) * h));
        vp->DrawTextNoClip(Point2DFloat(bx + border, by + border), attr, tip);
        vp->DrawLineNoClip(Line2DPixel(bx * w, by * h, ex * w, by * h), colorLines, colorLines);
        vp->DrawLineNoClip(Line2DPixel(ex * w, by * h, ex * w, ey * h), colorLines, colorLines);
        vp->DrawLineNoClip(Line2DPixel(ex * w, ey * h, bx * w, ey * h), colorLines, colorLines);
        vp->DrawLineNoClip(Line2DPixel(bx * w, ey * h, bx * w, by * h), colorLines, colorLines);
*/
      }
    }
  }
  else
  {
    // clear the autocomplete
    _lastTip = RString();
    _lastTipFormated = NULL;
  }
}

#ifdef _LINUX
#include <wctype.h>
#endif

bool CEdit::OnKeyDown(int dikCode)
{
  if (dikCode == DIK_F1)
  {
    if (_autoComplete)
    {
      RString wordOver;
      if (wordOver.GetLength() == 0)
      {
        // check the word we are over
        const wchar_t *beg = _textUnicode;
        const wchar_t *endWord = beg + _blockEnd;
        const wchar_t *begWord = endWord;
        while (begWord > beg)
        {
          wchar_t c = *(begWord - 1);
          if (!iswalnum(c) && c != '_' && c != '"') break;
          begWord--;
        }
        while (*endWord)
        {
          wchar_t c = *(endWord);
          if (!iswalnum(c) && c != '_' && c != '"') break;
          endWord++;
        }
        wordOver = UnicodeToUTF(RWString(begWord, endWord - begWord));
      }
      // first check the edited text, then hint
      RString help;
      if (wordOver.GetLength() > 0) help = _autoComplete->GetHelp(wordOver);
      if (help.GetLength() == 0 && _lastTip.GetLength() > 0) help = _autoComplete->GetHelp(_lastTip);
      if (help.GetLength() > 0)
      {
        Assert(_parent);
        _parent->CreateChild(new DisplayScriptingHelp(_parent, help));
      }
      return true;
    }
  }
  _caretBlinkTime = Glob.uiTime.toFloat();

  return CEditContainer::DoKeyDown(dikCode);
}

bool CEdit::OnKeyUp(int dikCode)
{
  _parent->OnEditChanged(this->IDC());
  return CEditContainer::DoKeyUp(dikCode);
}

bool CEdit::OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  return CEditContainer::DoChar(nChar, nRepCnt, nFlags);
}

bool CEdit::OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  return CEditContainer::DoIMEChar(nChar, nRepCnt, nFlags);
}

bool CEdit::OnIMEComposition(unsigned nChar, unsigned nFlags)
{
  return CEditContainer::DoIMEComposition(nChar, nFlags);
}

int CEdit::FindPos(float x, float y)
{
  if ((_style & ST_TYPE) == ST_MULTI)
  {
    if (_textUnicode.GetLength() == 0)
    {
      return 0;
    }
    else
    {
      float size = _scale * SCALED(_size);
      float height = size;
      int line = FirstLine() + toIntFloor((y - _y) / height);
      saturate(line, 0, _lines.Size() - 1);
      return _lines[line] + XToPos
      (
        GetLine(line), x
      );
    }
  }
  else
  {
    return _firstVisible + XToPos
    (
      _textUnicode.Substring(_firstVisible, _textUnicode.GetLength()), x
    );
  }
}

void CEdit::OnLButtonDown(float x, float y)
{
  int pos = FindPos(x, y);
  if (!GInput.keys[DIK_LSHIFT] && !GInput.keys[DIK_RSHIFT])
  {
    _blockBegin = pos;
  }
  _blockEnd = pos;
  EnsureVisible(_blockEnd);
}

void CEdit::OnLButtonUp(float x, float y)
{
/*
  _blockEnd = FindPos(x, y);
  EnsureVisible(_blockEnd);
*/
}

void CEdit::OnMouseMove(float x, float y, bool active)
{
  if (GInput.mouseL)
  {
    _blockEnd = FindPos(x, y);
    EnsureVisible(_blockEnd);
  }
}

void CEdit::EnsureVisible(int pos)
{
  float size = _scale * SCALED(_size);

  if ((_style & ST_TYPE) == ST_MULTI)
  {
    if (_textUnicode.GetLength() == 0)
    {
      _firstVisible = 0;
      return;
    }

    int cur = CurLine();
    int first = FirstLine();
    saturateMin(first, cur);

    float height = size;
    int maxlines = toIntFloor(SCALED(_h) / height);
    
    int lines = _lines.Size() - first;
    if (lines <= maxlines)
    {
      first += lines - maxlines;
      saturateMax(first, 0);
    }
    else if (first + maxlines <= cur)
    {
      first = cur - maxlines + 1;
    }
    _firstVisible = _lines[first];
  }
  else
  {
    saturateMin(_firstVisible, _blockEnd);
    float wlimit = SCALED(_w) - 2.0 * _scale * textBorder; 
    float w = GLOB_ENGINE->GetTextWidth
    (
      size, _font,
      UnicodeToUTF(_textUnicode.Substring(_firstVisible, _textUnicode.GetLength()))
    );
    if (w <= wlimit)
    {
      while (_firstVisible > 0)
      {
        int prev = PrevPos(_firstVisible);
        w += GLOB_ENGINE->GetTextWidth
        (
          size, _font,
          UnicodeToUTF(_textUnicode.Substring(prev, _firstVisible))
        );
        if (w > wlimit) break;
        _firstVisible = prev;
      }
    }
    else
    {
      w = GLOB_ENGINE->GetTextWidth
      (
        size, _font,
        UnicodeToUTF(_textUnicode.Substring(_firstVisible, _blockEnd))
      );
      while (w > wlimit)
      {
        int prev = _firstVisible;
        _firstVisible = NextPos(_firstVisible);
        w -= GLOB_ENGINE->GetTextWidth
        (
          size, _font,
          UnicodeToUTF(_textUnicode.Substring(prev, _firstVisible))
        );
      }
    }
  }
}

void CEdit::FormatText()
{
  if ((_style & ST_TYPE) != ST_MULTI) return;

  _lines.Clear();

  float lineWidth = SCALED(_w) - 2 * _scale * textBorder;
  float size = _scale * SCALED(_size);

  _lines.Add(0);

  const wchar_t *p = _textUnicode;
  while (*p != 0)
  {
    // begin of the line
    const wchar_t *word = p;
    int n = 0;
    float width = 0;
    while (true)
    {
      const wchar_t *q = p;
      wchar_t c = *p++;
      if (c == 0) return;
      if (ISSPACE(c))
      {
        n++;
        word = p;
      }

      // TODO: simplify
      wchar_t temp[2]; temp[0] = c; temp[1] = 0;
      width += GLOB_ENGINE->GetTextWidth(size, _font, UnicodeToUTF(temp));

      if ((width > lineWidth) || (c == '\n')) //VBS supports \n in multiline as well
      {
        if (n > 0)
        {
          p = word;
        }
        else
        {
          p = q;
        }
        _lines.Add(p - (const wchar_t *)_textUnicode);
        break;
      }
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// class CButton

/*!
\patch 5164 Date 6/8/2007 by Jirka
- Improved: UI - better detection of focused buttons
*/

CButton::CButton(ControlsContainer *parent, ParamEntryPar cls, float x, float y, float w, float h)
  : Control(parent, CT_BUTTON, cls >> "idc", cls >> "style", x, y, w, h)
{
  SetText(cls>>"text");
  _ftColor = GetPackedColor(cls >> "colorText");
  _ftColorDisabled = GetPackedColor(cls >> "colorDisabled");
  _font = GLOB_ENGINE->LoadFont(GetFontID(cls>>"font"));
  _size = cls >> "sizeEx";

  _bgColor = GetPackedColor(cls >> "colorBackground");
  _bgColorActive = GetPackedColor(cls >> "colorBackgroundActive");
  _bgColorDisabled = GetPackedColor(cls >> "colorBackgroundDisabled");
  _bgColorFocused = GetPackedColor(cls >> "colorFocused");
  ConstParamEntryPtr entry = cls.FindEntry("colorFocused2");
  if (entry) _bgColorFocused2 = GetPackedColor(*entry);
  else _bgColorFocused2 = _bgColor;
  _offsetX = cls >> "offsetX";
  _offsetY = cls >> "offsetY";
  _offsetPressedX = cls >> "offsetPressedX";
  _offsetPressedY = cls >> "offsetPressedY";

  _shadowColor = GetPackedColor(cls >> "colorShadow");

  _borderColor = GetPackedColor(cls >> "colorBorder");
  _borderSize = cls >> "borderSize";

  GetValue(_enterSound, cls >> "soundEnter");
  GetValue(_pushSound, cls >> "soundPush");
  GetValue(_clickSound, cls >> "soundClick");
  GetValue(_escapeSound, cls >> "soundEscape");

  entry = cls.FindEntry("action");
  if (entry) _action = *entry;

  float period = 0.5f;
  entry = cls.FindEntry("period");
  if (entry) period = *entry;
  _speed = period > 0 ? H_PI / period : 0;

  _state = false;
  _start = Glob.uiTime;

}

CButton::CButton(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : Control(parent, CT_BUTTON, idc, cls)
{
  SetText(cls >> "text");
  _ftColor = GetPackedColor(cls >> "colorText");
  _ftColorDisabled = GetPackedColor(cls >> "colorDisabled");
  _font = GEngine->LoadFont(GetFontID(cls >> "font"));
  _size = cls >> "sizeEx";

  _bgColor = GetPackedColor(cls >> "colorBackground");
  _bgColorActive = GetPackedColor(cls >> "colorBackgroundActive");
  _bgColorDisabled = GetPackedColor(cls >> "colorBackgroundDisabled");
  _bgColorFocused = GetPackedColor(cls >> "colorFocused");
  ConstParamEntryPtr entry = cls.FindEntry("colorFocused2");
  if (entry) _bgColorFocused2 = GetPackedColor(*entry);
  else _bgColorFocused2 = _bgColor;
  _offsetX = cls >> "offsetX";
  _offsetY = cls >> "offsetY";
  _offsetPressedX = cls >> "offsetPressedX";
  _offsetPressedY = cls >> "offsetPressedY";

  _shadowColor = GetPackedColor(cls >> "colorShadow");

  _borderColor = GetPackedColor(cls >> "colorBorder");
  _borderSize = cls >> "borderSize";

  GetValue(_enterSound, cls >> "soundEnter");
  GetValue(_pushSound, cls >> "soundPush");
  GetValue(_clickSound, cls >> "soundClick");
  GetValue(_escapeSound, cls >> "soundEscape");

  entry = cls.FindEntry("action");
  if (entry) _action = *entry;

  float period = 0.5f;
  entry = cls.FindEntry("period");
  if (entry) period = *entry;
  _speed = period > 0 ? H_PI / period : 0;

  _state = false;
  _start = Glob.uiTime;
}

void CButton::DoClick()
{
  PlaySound(_clickSound);
  if (_action.GetLength() > 0)
  {
    GameState *gstate = GWorld->GetGameState();
    GameVarSpace local(false);
    int gshandle = gstate->BeginContext(&local);
    gstate->Execute(_action, GameState::EvalContext::_default, UINamespace()); // UI namespace
    gstate->EndContext(gshandle);
    float GetTimeForScripts();
    GWorld->SimulateScripts(0, GetTimeForScripts());
  }
  OnClicked();
  if (OnCheckEvent(CEButtonClick)) return;
  if (_parent) _parent->OnButtonClicked(IDC());
  
}

bool CButton::OnKeyDown(int dikCode)
{
  if (ButtonPressed(dikCode))
  {
    if (!_state)
    {
      _state = true;
      OnEvent(CEButtonDown);
      PlaySound(_pushSound);
    }
    return true;
  }
  return false;
}

bool CButton::OnKeyUp(int dikCode)
{
  if (ButtonPressed(dikCode))
  {
    if (_state)
    {
      _state = false;
      DoClick();
    }
    return true;
  }
  return false;
}

bool CButton::OnShortcutDown()
{
  if (!IsEnabled()) return false;

  if (!_state)
  {
    _state = true;
    OnEvent(CEButtonDown);
    PlaySound(_pushSound);
  }
  return true;
}

bool CButton::OnShortcutUp()
{
  if (!IsEnabled()) return false;

  if (_state)
  {
    _state = false;
    DoClick();
  }
  return true;
}

bool CButton::OnKillFocus()
{
  _state = false;
  return Control::OnKillFocus();
}

void CButton::OnMouseEnter(float x, float y)
{
  PlaySound(_enterSound);
}

void CButton::OnLButtonDown(float x, float y)
{
  _state = true;
#if _VBS2 // CButton::OnLButtonDown returns mouse x,y
  bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
  bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
  bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
  OnEvent(CEButtonDown, 0, x, y, shift, ctrl, alt);
#else
  OnEvent(CEButtonDown);
#endif
  PlaySound(_pushSound);
}

void CButton::OnLButtonUp(float x, float y)
{
  _state = false;
  if (IsInside(x, y))
  {
    DoClick();
  }
  else
  {
    OnEvent(CEButtonUp);
    PlaySound(_escapeSound);
  }
}

/*!
\patch 5128 Date 2/9/2007 by Jirka
- Fixed: UI - button text position calculation improved
*/

void CButton::OnDraw(UIViewport *vp, float alpha)
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  // calculation of basic elements
  float shadowX = _x + _offsetX, shadowY = _y + _offsetY;
  float borderX = 0, borderY = _y + 1.0f / 480; // 1 pixel in 640 x 480
  float borderWidth = 0, borderHeight = SCALED(_h) - 2.0f / 480; // 2 pixels in 640 x 480
  if (_borderSize < 0)
  {
    borderX = _x + SCALED(_w);
    borderWidth = -SCALED(_borderSize);
  }
  else if (_borderSize > 0)
  {
    borderX = _x - SCALED(_borderSize);
    borderWidth =  SCALED(_borderSize) + _offsetX;
  }

  float bgX = _x, bgY = _y;
  bool pushed = IsEnabled() && _state;
  if (pushed)
  {
    // pushed
    bgX += _offsetPressedX;
    bgY += _offsetPressedY;
  }

  // decide what colors to use
  PackedColor color, bgColor;

  if (!IsEnabled())
  {
    color = ModAlpha(_ftColorDisabled, alpha);
    bgColor = ModAlpha(_bgColorDisabled, alpha);
  }
  else
  {
    color = ModAlpha(_ftColor, alpha);
    if (IsMouseOver())
      bgColor = ModAlpha(_bgColorActive, alpha);
    else if (IsFocused())
    {
      float factor = -0.5 * cos(_speed * (Glob.uiTime - _start)) + 0.5;
      Color bgcol0 = _bgColorFocused, bgcol1 = _bgColorFocused2;
      bgColor = PackedColor(bgcol0 * factor + bgcol1 * (1 - factor));
      bgColor = ModAlpha(bgColor, alpha);
    }
    else
      bgColor = ModAlpha(BGColor(), alpha);
  }

  // draw elements
  Draw2DParsExt pars;
  pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  pars.Init();
  //pars.spec|= (_shadow == 2)?UISHADOW : 0; 
  
  // shadow
  if (IsEnabled())
  {
    pars.SetColor(ModAlpha(_shadowColor, alpha));
    vp->Draw2D(pars, Rect2DPixel(CX(shadowX), CY(shadowY), CW(SCALED(_w)), CH(SCALED(_h))));
  }

  // border
  if (borderWidth > 0)
  {
    pars.SetColor(ModAlpha(_borderColor, alpha));
    vp->Draw2D(pars, Rect2DPixel(CX(borderX), CY(borderY), CW(borderWidth), CH(borderHeight)));
  }

  // background
  pars.SetColor(bgColor);
  vp->Draw2D(pars, Rect2DPixel(CX(bgX), CY(bgY), CW(SCALED(_w)), CH(SCALED(_h))));

  // text
  TextDrawAttr attr(SCALED(_size), _font, color, _shadow);
  float top = bgY + 0.5 * (SCALED(_h-_size));
  float left = bgX;
  switch (_style & ST_HPOS)
  {
  case ST_RIGHT:
    left += SCALED(_w) - vp->GetTextWidth(attr, GetText()) - SCALED(textBorder);
    break;
  case ST_CENTER:
    left += 0.5 * (SCALED(_w) - vp->GetTextWidth(attr, GetText()));
    break;
  default:
    Assert((_style & ST_HPOS) == ST_LEFT)
    left += SCALED(textBorder);
    break;
  }
  
  vp->DrawText(Point2DFloat(left, top), Rect2DFloat(bgX, bgY, SCALED(_w), SCALED(_h)), attr, GetText());
}

void CButton::OnClicked()
{
}

PackedColor CTabButton::BGColor() const
{
  return (_active ? _bgColorFocused2 : _bgColor); 
}

///////////////////////////////////////////////////////////////////////////////
// class CActiveText

CActiveText::CActiveText(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : Control(parent, CT_ACTIVETEXT, idc, cls)
{
  SetText(cls>>"text");
  _font = GLOB_ENGINE->LoadFont(GetFontID(cls>>"font"));
  _size = cls >> "sizeEx";
  _color = GetPackedColor(cls >> "color");
  _colorActive = GetPackedColor(cls >> "colorActive");

  GetValue(_enterSound, cls >> "soundEnter");
  GetValue(_pushSound, cls >> "soundPush");
  GetValue(_clickSound, cls >> "soundClick");
  GetValue(_escapeSound, cls >> "soundEscape");

  ConstParamEntryPtr entry = cls.FindEntry("action");
  if (entry) _action = *entry;
}

bool CActiveText::SetText(RString text)
{
  if (!CTextContainer::SetText(text)) return false;

  if ((_style & ST_TYPE) == ST_PICTURE)
  {
    text = FindPicture(text);
    text.Lower();
    _texture = GlobLoadTextureUI(text);
  }
  return true;
}

void CActiveText::DoClick()
{
  PlaySound(_clickSound);
  if (_action.GetLength() > 0)
  {
    GameState *gstate = GWorld->GetGameState();
    GameVarSpace local(false);
    int gshandle = gstate->BeginContext(&local);
    gstate->Execute(_action, GameState::EvalContext::_default, UINamespace()); // UI namespace
    gstate->EndContext(gshandle);
    float GetTimeForScripts();
    GWorld->SimulateScripts(0, GetTimeForScripts());
  }
  if (OnCheckEvent(CEButtonClick)) return;
  if (_parent) _parent->OnButtonClicked(IDC());
}

bool CActiveText::OnKeyDown(int dikCode)
{
  if (ButtonPressed(dikCode))
  {
    if (IsEnabled())
    {
      OnEvent(CEButtonDown);
      PlaySound(_pushSound);
      return true;
    }
  }
  return false;
}

bool CActiveText::OnKeyUp(int dikCode)
{
  if (ButtonPressed(dikCode))
  {
    if (IsEnabled())
    {
      DoClick();
      return true;
    }
  }
  return false;
}

bool CActiveText::OnShortcutDown()
{
  if (!IsEnabled()) return false;

  OnEvent(CEButtonDown);
  PlaySound(_pushSound);
  return true;
}

bool CActiveText::OnShortcutUp()
{
  if (!IsEnabled()) return false;

  DoClick();
  return true;
}

void CActiveText::OnMouseEnter(float x, float y)
{
  PlaySound(_enterSound);
}

void CActiveText::OnLButtonDown(float x, float y)
{
  OnEvent(CEButtonDown);
  PlaySound(_pushSound);
}

void CActiveText::OnLButtonUp(float x, float y)
{
  if (IsInside(x, y))
  {
    DoClick();
  }
  else
  {
    OnEvent(CEButtonUp);
    PlaySound(_escapeSound);
  }
}

void CActiveText::OnDraw(UIViewport *vp, float alpha)
{
  PackedColor color = ModAlpha(IsMouseOver() ? _colorActive : _color, alpha);

  if ((_style & ST_TYPE) == ST_PICTURE)
  {
    if (_texture)
    {
      const int w = GLOB_ENGINE->Width2D();
      const int h = GLOB_ENGINE->Height2D();

      float x = _x, y = _y, width = SCALED(_w), height = SCALED(_h);
      if (_style & ST_KEEP_ASPECT_RATIO)
      {
        float xCoef = (SCALED(_w) * w) / _texture->AWidth();
        float yCoef = (SCALED(_h) * h) / _texture->AHeight();
        if (xCoef > yCoef)
        {
          width *= yCoef / xCoef;
          x += 0.5 * (SCALED(_w) - width);
        }
        else
        {
          height *= xCoef / yCoef;
          y += 0.5 * (SCALED(_h) - height);
        }
      }
      float xx = CX(x);
      float yy = CY(y);
      float ww = CX(x + width) - xx;
      float hh = CY(y + height) - yy;

      Draw2DParsExt pars;
      pars.mip = GEngine->TextBank()->UseMipmap(_texture, 0, 0);
      pars.SetColor(color);
      pars.Init();
      pars.spec|= (_shadow == 2)?UISHADOW : 0; 
      vp->Draw2D(pars, Rect2DPixel(xx, yy, ww, hh));
    }
    return;
  }

  float size = _scale * SCALED(_size);
  bool vertical = false;
  float height = size;
  const float offsetX = 0.045 * height;
  const float offsetY = 0.06 * height;
  float left = _x + 0.5 * (SCALED(_w) - height);
  float top = _y + 0.5 * (SCALED(_h) - height);

  TextDrawAttr attr(size, _font, PackedBlack ,_shadow);

  float wText = vp->GetTextWidth(attr, GetText());
  switch (_style & ST_POS)
  {
    case ST_UP:
      top = _y;
      vertical = true;
      break;
    case ST_VCENTER:
      top = _y + 0.5 * (SCALED(_h) - wText);
      vertical = true;
      break;
    case ST_DOWN:
      top = _y + SCALED(_h) - wText;
      vertical = true;
      break;
    case ST_RIGHT:
      left = _x + SCALED(_w)  - wText - SCALED(textBorder);
      break;
    case ST_CENTER:
      left = _x + 0.5 * (SCALED(_w) - wText);
      break;
    default:
      Assert((_style & ST_HPOS) == ST_LEFT)
      left = _x + SCALED(textBorder);
      break;
  }

  PackedColor shadowColor = PackedColor(Color(0, 0, 0, alpha));

  if (!IsEnabled()) color = ModAlpha(color, 0.25);
  bool focused = IsFocused();
  bool selected = GetDefault() && !(_parent->GetFocused() && _parent->GetFocused()->CanBeDefault());
  const float w = GLOB_ENGINE->Width2D();
  const float h = GLOB_ENGINE->Height2D();
  if (GetText().GetLength() > 0)
  {
    if (vertical)
    {
      if (focused || selected)
      {
        PackedColor col;
        if (focused) col = color;
        else col = ModAlpha(color, 0.5);
        vp->DrawLine
        (
          Line2DPixel(_x * w, top * h, _x * w, top * h + wText * w),
          col, col
        );
      }
      if (_shadow == 1)
      {
        attr._color = shadowColor;
        vp->DrawTextVertical
        (
          Point2DFloat(left + offsetX, top + offsetY),
          Rect2DFloat(_x, _y, SCALED(_w), SCALED(_h)),
          attr, GetText()
        );
      }
      attr._color = color;
      vp->DrawTextVertical
      (
        Point2DFloat(left, top),
        Rect2DFloat(_x, _y, SCALED(_w), SCALED(_h)),
        attr, GetText()
      );
    }
    else
    {
      if (focused || selected)
      {
        PackedColor col;
        if (focused) col = color;
        else col = ModAlpha(color, 0.5);
        vp->DrawLine
        (
          Line2DPixel(left * w, (_y + SCALED(_h)) * h, (left + wText) * w, (_y + SCALED(_h)) * h),
          col, col
        );
      }
      if (_shadow == 1)
      {
        attr._color = shadowColor;
        vp->DrawText
        (
          Point2DFloat(left + offsetX, top + offsetY),
          Rect2DFloat(_x, _y, SCALED(_w), SCALED(_h)),
          attr, GetText()
        );
      }
      attr._color = color;
      vp->DrawText
      (
        Point2DFloat(left, top),
        Rect2DFloat(_x, _y, SCALED(_w), SCALED(_h)),
        attr, GetText()
      );
    }
  }
  else
  {
    if (selected)
    {
      vp->DrawLine
      (
        Line2DPixel(_x * w, _y * h, _x * w, (_y + SCALED(_h)) * h),
        color, color
      );
      vp->DrawLine
      (
        Line2DPixel(_x * w, (_y + SCALED(_h)) * h, (_x + SCALED(_w)) * w, (_y + SCALED(_h)) * h),
        color, color
      );
      vp->DrawLine
      (
        Line2DPixel((_x + SCALED(_w)) * w, (_y + SCALED(_h)) * h, (_x + SCALED(_w)) * w, _y * h),
        color, color
      );
      vp->DrawLine
      (
        Line2DPixel((_x + SCALED(_w)) * w, _y * h, _x * w, _y * h),
        color, color
      );
    }
  }
}

bool CActiveText::IsInside(float x, float y) const
{
  if (!Control::IsInside(x, y)) return false;

  if ((_style & ST_TYPE) == ST_FRAME) return true;
  if ((_style & ST_TYPE) == ST_PICTURE) return _texture != NULL;
  if (GetText().GetLength() == 0) return false;

  float size = _scale * SCALED(_size);
  bool vertical = false;
  float left = _x + 0.5 * (SCALED(_w) - size);
  float top = _y + 0.5 * (SCALED(_h) - size);
  float wText = GLOB_ENGINE->GetTextWidth(size, _font, GetText());
  switch (_style & ST_POS)
  {
    case ST_UP:
      top = _y;
      vertical = true;
      break;
    case ST_VCENTER:
      top = _y + 0.5 * (SCALED(_h) - wText);
      vertical = true;
      break;
    case ST_DOWN:
      top = _y + SCALED(_h) - wText;
      vertical = true;
      break;
    case ST_RIGHT:
      left = _x + SCALED(_w)  - wText - SCALED(textBorder);
      break;
    case ST_CENTER:
      left = _x + 0.5 * (SCALED(_w) - wText);
      break;
    default:
      Assert((_style & ST_HPOS) == ST_LEFT)
      left = _x + SCALED(textBorder);
      break;
  }

  if (vertical)
    return x >= left && x <= left + size && y >= top && y <= top + wText;
  else
    return x >= left && x <= left + wText && y >= top && y <= top + size;
}

///////////////////////////////////////////////////////////////////////////////
// class CToolBox

CToolBox::CToolBox(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : Control(parent, CT_TOOLBOX, idc, cls)
{
  int i, n = (cls>>"strings").GetSize();
  _strings.Resize(n);
  for (i=0; i<n; i++) _strings[i] = (cls>>"strings")[i];

  _rows = cls>>"rows";
  _columns = cls>>"columns";

  _ftColor = GetPackedColor(cls >> "colorText");
  _bgColor = GetPackedColor(cls >> "color");
  _ftSelectColor = GetPackedColor(cls >> "colorTextSelect");
  _bgSelectColor = GetPackedColor(cls >> "colorSelect");
  _ftDisabledColor = GetPackedColor(cls >> "colorTextDisable");
  _bgDisabledColor = GetPackedColor(cls >> "colorDisable");
  _font = GLOB_ENGINE->LoadFont(GetFontID(cls>>"font"));
  _size = cls >> "sizeEx";
  _selectedBg = GetPackedColor(cls >> "coloSelectedBg"); 

  SetCurSel(0);
}

bool CToolBox::IsSelected(int i) const
{
  return i == _selected;
}

void CToolBox::ChangeSelection(int i)
{
  SetCurSel(i);
  OnEvent(CEToolBoxSelChanged, _selected);
  if (_parent) _parent->OnToolBoxSelChanged(_idc, _selected);
}

bool CToolBox::OnKeyDown(int dikCode)
{
  switch (dikCode)
  {
  case DIK_LEFT:
  case DIK_UP:
    ChangeSelection(_selected - 1);
    return true;
  case DIK_RIGHT:
  case DIK_DOWN:
    ChangeSelection(_selected + 1);
    return true;
  }
  return false;
}

void CToolBox::OnLButtonDown(float x, float y)
{
  float w = SCALED(_w) / _columns;
  float h = SCALED(_h) / _rows;

  int c = toIntFloor((x - _x) / w);
  int r = toIntFloor((y - _y) / h);
  if (c < 0 || c >= _columns) return;
  if (r < 0 || r >= _rows) return;

  int i = r * _columns + c;
  if (i >= 0 && i < _strings.Size())
  {
    ChangeSelection(i);
  }
}


void CToolBox::OnDraw(UIViewport *vp, float alpha)
{
  PackedColor colorBg;
  if (!IsEnabled())
  {
    colorBg = ModAlpha(_bgDisabledColor, alpha);
  }
  else if (IsFocused())
  {
    colorBg = ModAlpha(_bgSelectColor, alpha);
  }
  else
  {
    colorBg = ModAlpha(_bgColor, alpha);
  }
  float wScr = GLOB_ENGINE->Width2D();
  float hScr = GLOB_ENGINE->Height2D();

  float w = SCALED(_w) / _columns;
  float h = SCALED(_h) / _rows;

 // const float border = SCALED(TEXT_BORDER);
  int sel = GetCurSel();
  int row = sel / _columns;
  int col = sel % _columns;
  float yy = _y + row * h /*+ border*/;
  float xx = _x + col * w /*+ border*/;
  float ww = w /*- 2 * border*/;
  float hh = h /*- 2 * border*/;
  //vp->DrawLine
  //(
  //  Line2DPixel(wScr * xx, hScr * yy, wScr * (xx + ww), hScr * yy),
  //  colorBg, colorBg
  //);
  //vp->DrawLine
  //(
  //  Line2DPixel(wScr * (xx + ww), hScr * yy, wScr * (xx + ww), hScr * (yy + hh)),
  //  colorBg, colorBg
  //);
  //vp->DrawLine
  //(
  //  Line2DPixel(wScr * (xx + ww), hScr * (yy + hh), wScr * xx, hScr * (yy + hh)),
  //  colorBg, colorBg
  //);
  //vp->DrawLine
  //(
  //  Line2DPixel(wScr * xx, hScr * (yy + hh), wScr * xx, hScr * yy),
  //  colorBg, colorBg
  //);
  
  Draw2DParsExt pars;
  pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  pars.SetColor(_selectedBg);
  pars.Init();
  pars.spec|= (_shadow == 2)?UISHADOW : 0; 

  vp->Draw2D
    (
    pars, Rect2DPixel(wScr * xx,  hScr * yy,wScr * ww,  hScr * hh),
    Rect2DPixel(wScr * xx, hScr * yy,wScr * ww, hScr * hh)
    );

  TextDrawAttr attr(SCALED(_size), _font, PackedBlack, _shadow);

  float hText = SCALED(_size);

  int i = 0, n = _strings.Size();
  for (int y=0; y<_rows; y++)
    for (int x=0; x<_columns; x++)
    {
      if (i >= n) return;

      float leftBound = _x + x * w;
      float left = leftBound;
      switch (_style & ST_HPOS)
      {
        case ST_RIGHT:
          left += w - vp->GetTextWidth(attr, _strings[i]) - textBorder;
          break;
        case ST_CENTER:
          left += 0.5 * (w - vp->GetTextWidth(attr, _strings[i]));
          break;
        default:
          Assert((_style & ST_HPOS) == ST_LEFT)
          left += textBorder;
          break;
      }
      float topBound = _y + y * h;
      float top = topBound + 0.5 * (h - hText);

      if (IsSelected(i))
        attr._color = ModAlpha(_ftSelectColor, alpha);
      else
        attr._color = ModAlpha(_ftColor, alpha);
      vp->DrawText
      (
        Point2DFloat(left, top),
        Rect2DFloat(leftBound, topBound, w, h),
        attr, _strings[i]
      );
      i++;
    }
}

///////////////////////////////////////////////////////////////////////////////
// class CCheckBoxes

CCheckBoxes::CCheckBoxes(ControlsContainer *parent, int idc, ParamEntryPar cls)
: CToolBox(parent, idc, cls)
{
  _type = CT_CHECKBOXES;
}

bool CCheckBoxes::OnKeyDown(int dikCode)
{
  switch (dikCode)
  {
  case DIK_LEFT:
  case DIK_UP:
    SetCurSel(_selected - 1);
    return true;
  case DIK_RIGHT:
  case DIK_DOWN:
    SetCurSel(_selected + 1);
    return true;
  case DIK_SPACE:
    ChangeSelection(_selected);
    return true;
  }
  return false;
}

bool CCheckBoxes::IsSelected(int i) const
{
  return GetArray().Get(i);
}

void CCheckBoxes::ChangeSelection(int i)
{
  _array.Toggle(i);
  SetCurSel(i);
  bool status = _array.Get(i);
  OnEvent(CECheckBoxesSelChanged, _selected, status);
  if (_parent) _parent->OnCheckBoxesSelChanged(_idc, _selected, status);
}

///////////////////////////////////////////////////////////////////////////////
// class CSlider

CSliderContainer::CSliderContainer(ControlsContainer *parent, ParamEntryPar cls)
{
  _color = GetPackedColor(cls >> "color");
  ConstParamEntryPtr entry = cls.FindEntry("colorActive");
  if (entry)
    _colorActive = GetPackedColor(*entry);
  else
    _colorActive = _color;
  entry = cls.FindEntry("colorDisabled");
  if (entry)
    _colorDisabled = GetPackedColor(*entry);
  else
    _colorDisabled = _color;

  entry = cls.FindEntry("Title");
  if (entry)
  {
    int idc = *entry >> "idc";
    if (idc >= 0)
    {
      IControl *ctrl = parent->GetCtrl(idc);
      _title = GetStructuredText(ctrl);
      _titleColorBase = GetPackedColor(*entry >> "colorBase");
      _titleColorActive = GetPackedColor(*entry >> "colorActive");
    }
  }
  entry = cls.FindEntry("Value");
  _valueFormat = "%g";
  _valueType = SPTPlain;
  if (entry)
  {
    int idc = *entry >> "idc";
    if (idc >= 0)
    {
      IControl *ctrl = parent->GetCtrl(idc);
      _value = GetTextContainer(ctrl);
      ConstParamEntryPtr format = entry->FindEntry("format");
      if (format) _valueFormat = *format;
      ConstParamEntryPtr type = entry->FindEntry("type");
      if (type) _valueType = type->GetInt();
      _valueColorBase = GetPackedColor(*entry >> "colorBase");
      _valueColorActive = GetPackedColor(*entry >> "colorActive");
    }
  }

  SetSpeed(1, 3);
  SetRangeAndThumbPos(0, 10, 0);
  _thumbLocked = false;
}

void CSliderContainer::SaturateThumbPos()
{
  if (_curPos < _minPos)
    _curPos = _minPos;
  else if (_step > 0)
  {
    _curPos = _minPos + _step * toInt((_curPos - _minPos) / _step);
    if (_curPos > _maxPos) _curPos = _minPos + _step * toIntFloor((_maxPos - _minPos) / _step);
  }
  else
  {
    if (_curPos > _maxPos) _curPos = _maxPos;
  }
}

void CSliderContainer::SetThumbPos(float pos)
{
  _curPos = pos;
  SaturateThumbPos();

  if (_value)
  {
    float value = _curPos;
    switch (_valueType)
    {
    case SPTPercents:
      value *= 100.0;
      break;
    }
    _value->SetText(Format(_valueFormat, value));
  }
}

bool CSliderContainer::DoKeyDown(int dikCode)
{
  switch (dikCode)
  {
  case DIK_LEFT:
  case INPUT_DEVICE_XINPUT + XBOX_Left:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXLeft:
    SetThumbPos(_curPos - _lineStep);
    return true;
  case DIK_RIGHT:
  case INPUT_DEVICE_XINPUT + XBOX_Right:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXRight:
    SetThumbPos(_curPos + _lineStep);
    return true;
  }
  return false;
}

CSlider::CSlider(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : Control(parent, CT_SLIDER, idc, cls), CSliderContainer(parent, cls)
{
#if _VBS3
  if(idc == S_TIME)
  {
    _arrowTexture  = GlobLoadTextureUI(FindPicture(Pars >> "RscDisplayMissionEditor" >> "Controls" >> "AAR_Slider" >> "arrowTexture"));
    _arrowTextureMouseOver = GlobLoadTextureUI(FindPicture(Pars >> "RscDisplayMissionEditor" >> "Controls" >> "AAR_Slider" >> "arrowTextureMouseOver"));
    _deathTexture  = GlobLoadTextureUI(FindPicture(Pars >> "RscDisplayMissionEditor" >> "Controls" >> "AAR_Slider" >> "deathTexture"));
    _noteTexture   = GlobLoadTextureUI(FindPicture(Pars >> "RscDisplayMissionEditor" >> "Controls" >> "AAR_Slider" >> "noteTexture"));
    _speakerTexture= GlobLoadTextureUI(FindPicture(Pars >> "RscDisplayMissionEditor" >> "Controls" >> "AAR_Slider" >> "speakerTexture"));

    static char* side[] = {"ColorEast", "ColorWest", "ColorGuerrila", "ColorCivilian"};
    for(int i=0; i < TSideUnknown; ++i)
      _sideColor[i] = GetPackedColor(Pars >> "RscDisplayMissionEditor" >> "Controls" >> "AAR_Slider" >> side[i]);

    _iconWidth  = 0.01;
    _iconHeight = 0.01;
    _noteWidth  = 0.020;
    _noteHeight = 0.026;
    _arrowWidth  = 0.020;
    _arrowHeight = 0.026;
    _speakerWidth  = 0.025;
    _speakerHeight = 0.025;
    _mouseOverSlider = false;
  }
#endif
}

bool CSlider::OnKeyDown(int dikCode)
{
  int oldPos = _curPos;
  bool ret = CSliderContainer::DoKeyDown(dikCode);
  if (_curPos != oldPos)
  {
    OnEvent(CESliderPosChanged, _curPos);
    if (_parent) _parent->OnSliderPosChanged(this, _curPos);
  }
  return ret;
}

void CSlider::OnLButtonDown(float x, float y)
{
  if ((_style & SL_DIR) == SL_VERT)
  {
    float spinHeight = (1.33 * 0.6) * SCALED(_w);
    const float thumbHeight = 0.02;
    float top = _y + spinHeight + 0.5 * thumbHeight;
    float fieldHeight = SCALED(_h) - 2 * spinHeight - thumbHeight;

    float coef = 0;
    if (_maxPos > _minPos)
      coef = 1.0 / (_maxPos - _minPos);
    float thumbPos = top + fieldHeight * (_curPos - _minPos) * coef;

    if (y - _y < spinHeight)
    {
      SetThumbPos(_curPos - _lineStep);
      OnEvent(CESliderPosChanged, _curPos);
      if (_parent) _parent->OnSliderPosChanged(this, _curPos);
    }
    else if (y - _y > SCALED(_h) - spinHeight)
    {
      SetThumbPos(_curPos + _lineStep);
      OnEvent(CESliderPosChanged, _curPos);
      if (_parent) _parent->OnSliderPosChanged(this, _curPos);
    }
    else if (y < thumbPos - 0.5 * thumbHeight)
    {
      SetThumbPos(_curPos - _pageStep);
      OnEvent(CESliderPosChanged, _curPos);
      if (_parent) _parent->OnSliderPosChanged(this, _curPos);
    }
    else if (y > thumbPos + 0.5 * thumbHeight)
    {
      SetThumbPos(_curPos + _pageStep);
      OnEvent(CESliderPosChanged, _curPos);
      if (_parent) _parent->OnSliderPosChanged(this, _curPos);
    }
    else
    {
      _thumbLocked = true;
      _thumbOffset = y - thumbPos;
    }
  }
  else
  {
    float spinWidth = (0.75 * 0.6) * SCALED(_h);
    const float thumbWidth = SCALED(0.015);
    float left = _x + spinWidth + 0.5 * thumbWidth;
    float fieldWidth = SCALED(_w) - 2 * spinWidth - thumbWidth;

    float coef = 0;
    if (_maxPos > _minPos)
      coef = 1.0 / (_maxPos - _minPos);
    float thumbPos = left + fieldWidth * (_curPos - _minPos) * coef;

    if (x - _x < spinWidth)
    {
      SetThumbPos(_curPos - _lineStep);
      OnEvent(CESliderPosChanged, _curPos);
      if (_parent) _parent->OnSliderPosChanged(this, _curPos);
    }
    else if (x - _x > SCALED(_w) - spinWidth)
    {
      SetThumbPos(_curPos + _lineStep);
      OnEvent(CESliderPosChanged, _curPos);
      if (_parent) _parent->OnSliderPosChanged(this, _curPos);
    }
    else if (x < thumbPos - 0.5 * thumbWidth)
    {
      SetThumbPos(_curPos - _pageStep);
      OnEvent(CESliderPosChanged, _curPos);
      if (_parent) _parent->OnSliderPosChanged(this, _curPos);
    }
    else if (x > thumbPos + 0.5 * thumbWidth)
    {
      SetThumbPos(_curPos + _pageStep);
      OnEvent(CESliderPosChanged, _curPos);
      if (_parent) _parent->OnSliderPosChanged(this, _curPos);
    }
    else
    {
      _thumbLocked = true;
      _thumbOffset = x - thumbPos;
    }
  }
}

void CSlider::OnLButtonUp(float x, float y)
{
  _thumbLocked = false;
}

void CSlider::OnMouseMove(float x, float y, bool active)
{
#if _VBS3 //special AAR slider
  if(_idc == S_TIME)
  {
    if ((_style & SL_DIR) != SL_VERT)
    {
      float bigLine = 0.6 * SCALED(_h);

      float spinWidth = 0.75 * bigLine;
      const float thumbWidth = SCALED(0.015);
      float fieldWidth = SCALED(_w) - 2 * spinWidth - thumbWidth;

      float left = _x + spinWidth + 0.5 * thumbWidth;

      float coef = 0;
      if (_maxPos > _minPos)
        coef = 1.0 / (_maxPos - _minPos);
      float thumbPos = left + fieldWidth * (_curPos - _minPos) * coef;

      if
        ( 
        x >= (thumbPos-(_arrowWidth * 0.5)) &&
        x <= (thumbPos+(_arrowWidth * 0.5)) && 
        y >= _y &&
        y <= (_y + _arrowHeight)
        )
        _mouseOverSlider = true;
      else
        _mouseOverSlider = false;
    }
  }
#endif

  if (_thumbLocked)
  {
    if ((_style & SL_DIR) == SL_VERT)
    {
      float spinHeight = (1.33 * 0.6) * SCALED(_w);
      const float thumbHeight = 0.02;
      float top = _y + spinHeight + 0.5 * thumbHeight;
      float fieldHeight = SCALED(_h) - 2 * spinHeight - thumbHeight;

      float coef = 0;
      if (_maxPos > _minPos)
        coef = _maxPos - _minPos;

      if (y - _thumbOffset < top)
      {
        SetThumbPos(_minPos);
        OnEvent(CESliderPosChanged, _curPos);
        if (_parent) _parent->OnSliderPosChanged(this, _curPos);
      }
      else if (y - _thumbOffset > top + fieldHeight)
      {
        SetThumbPos(_maxPos);
        OnEvent(CESliderPosChanged, _curPos);
        if (_parent) _parent->OnSliderPosChanged(this, _curPos);
      }
      else
      {
        SetThumbPos(_minPos + (y - _thumbOffset - top) * coef / fieldHeight);
        OnEvent(CESliderPosChanged, _curPos);
        if (_parent) _parent->OnSliderPosChanged(this, _curPos);
      }
    }
    else
    {
      float spinWidth = (0.75 * 0.6) * SCALED(_h);
      const float thumbWidth = SCALED(0.015);
      float left = _x + spinWidth + 0.5 * thumbWidth;
      float fieldWidth = SCALED(_w) - 2 * spinWidth - thumbWidth;

      float coef = 0;
      if (_maxPos > _minPos)
        coef = _maxPos - _minPos;

      if (x - _thumbOffset < left)
      {
        SetThumbPos(_minPos);
        OnEvent(CESliderPosChanged, _curPos);
        if (_parent) _parent->OnSliderPosChanged(this, _curPos);
      }
      else if (x - _thumbOffset > left + fieldWidth)
      {
        SetThumbPos(_maxPos);
        OnEvent(CESliderPosChanged, _curPos);
        if (_parent) _parent->OnSliderPosChanged(this, _curPos);
      }
      else
      {
        SetThumbPos(_minPos + (x - _thumbOffset - left) * coef / fieldWidth);
        OnEvent(CESliderPosChanged, _curPos);
        if (_parent) _parent->OnSliderPosChanged(this, _curPos);
      }
    }
  }
}

/**
VBS: AAR Drawing code, for the line + little triangles + timeline triangle
*/

void CSlider::OnDraw(UIViewport *vp, float alpha)
{
#if _VBS3 && _EXT_CTRL
  // Use a custome AAR drawing for this IDC
  if( _idc == S_TIME)
  {
    OnAARDraw(vp,alpha);
    return;
  }
#endif

  if (_title)
  {
    if (IsFocused()) _title->SetTextColor(_titleColorActive);
    else _title->SetTextColor(_titleColorBase);
  }
  if (_value)
  {
    IControl *ctrl = _value->GetControl();
    if (ctrl)
    {
      if (IsFocused()) ctrl->SetTextColor(_valueColorActive);
      else ctrl->SetTextColor(_valueColorBase);
    }
  }

  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  PackedColor color = ModAlpha(_color, alpha);
  PackedColor colorActive = IsFocused() ? ModAlpha(_colorActive, alpha) : color;

  if ((_style & SL_DIR) == SL_VERT)
  {
    float bigLine = 0.6 * SCALED(_w);
    float smallLine = 0.3 * SCALED(_w);

    float spinHeight = 1.33 * bigLine;
    const float thumbHeight = 0.02;
    float fieldHeight = SCALED(_h) - 2 * spinHeight - thumbHeight;
    
    float top = _y + spinHeight + 0.5 * thumbHeight;
    float bottom = top + fieldHeight;
    float right = _x + bigLine;

    float coef = 0;
    if (_maxPos > _minPos)
      coef = 1.0 / (_maxPos - _minPos);
    float thumbPos = top + fieldHeight * (_curPos - _minPos) * coef;

    // draw top spin
    float center = _x + 0.5 * bigLine;
    vp->DrawLine
    (
      Line2DPixel(center * w, _y * h, _x * w, (_y + spinHeight) * h),
      colorActive, colorActive
    );
    vp->DrawLine
    (
      Line2DPixel(_x * w, (_y + spinHeight) * h, (_x + bigLine) * w, (_y + spinHeight) * h),
      colorActive, colorActive
    );
    vp->DrawLine
    (
      Line2DPixel((_x + bigLine) * w, (_y + spinHeight) * h, center * w, _y * h),
      colorActive, colorActive
    );

    // draw right spin
    vp->DrawLine
    (
      Line2DPixel(center * w, (_y + SCALED(_h)) * h, _x * w, (_y + SCALED(_h) - spinHeight) * h),
      colorActive, colorActive
    );
    vp->DrawLine
    (
      Line2DPixel(_x * w, (_y + SCALED(_h) - spinHeight) * h, (_x + bigLine) * w, (_y + SCALED(_h) - spinHeight) * h),
      colorActive, colorActive
    );
    vp->DrawLine
    (
      Line2DPixel((_x + bigLine) * w, (_y + SCALED(_h) - spinHeight) * h, center * w, (_y + SCALED(_h)) * h),
      colorActive, colorActive
    );

    // draw background
    vp->DrawLine
    (
      Line2DPixel(CX(right), CY(top), CX(right), CY(bottom) + 1.0),
      color, color
    );
    int lines = 1;
    float lineDist = fieldHeight;
    while (lineDist > 0.02)
    {
      lines *= 2;
      lineDist *= 0.5;
    }

    for (int i=0; i<=lines; i++)
    {
      float pos = CY(top + i * lineDist);
      float left = right - ((i % 2 == 0 || lines == 1) ? bigLine : smallLine);
      vp->DrawLine
      (
        Line2DPixel(CX(left), pos, CX(right), pos),
        color, color
      );
    }

    // draw thumb
    vp->DrawLine
    (
      Line2DPixel((_x + bigLine) * w, thumbPos * h, (_x + SCALED(_w)) * w, (thumbPos - 0.5 * thumbHeight) * h),
      colorActive, colorActive
    );
    vp->DrawLine
    (
      Line2DPixel((_x + SCALED(_w)) * w, (thumbPos - 0.5 * thumbHeight) * h, (_x + SCALED(_w)) * w, (thumbPos + 0.5 * thumbHeight) * h),
      colorActive, colorActive
    );
    vp->DrawLine
    (
      Line2DPixel((_x + SCALED(_w)) * w, (thumbPos + 0.5 * thumbHeight) * h, (_x + bigLine) * w, thumbPos * h), 
      colorActive, colorActive
    );
  }
  else
  {
    float bigLine = 0.6 * SCALED(_h);
    float smallLine = 0.3 * SCALED(_h);
    float space = 0.15 * SCALED(_h);

    float spinWidth = 0.75 * bigLine;
    const float thumbWidth = SCALED(0.015);
    float fieldWidth = SCALED(_w) - 2 * spinWidth - thumbWidth;
    
    float left = _x + spinWidth + 0.5 * thumbWidth;
    float right = left + fieldWidth;
    float bottom = _y + bigLine;

    float coef = 0;
    if (_maxPos > _minPos)
      coef = 1.0 / (_maxPos - _minPos);
    float thumbPos = left + fieldWidth * (_curPos - _minPos) * coef;

    // draw left spin
    float center = _y + 0.5 * bigLine;
    vp->DrawLine
    (
      Line2DPixel(_x * w, center * h, (_x + spinWidth) * w, _y * h),
      colorActive, colorActive
    );
    vp->DrawLine
    (
      Line2DPixel((_x + spinWidth) * w, _y * h,(_x + spinWidth) * w, (_y + bigLine) * h),
      colorActive, colorActive
    );
    vp->DrawLine
    (
      Line2DPixel((_x + spinWidth) * w, (_y + bigLine) * h,_x * w, center * h),
      colorActive, colorActive
    );

    // draw right spin
    vp->DrawLine
    (
      Line2DPixel((_x + SCALED(_w)) * w, center * h, (_x + SCALED(_w) - spinWidth) * w, _y * h),
      colorActive, colorActive
    );
    vp->DrawLine
    (
      Line2DPixel((_x + SCALED(_w) - spinWidth) * w, _y * h, (_x + SCALED(_w) - spinWidth) * w, (_y + bigLine) * h),
      colorActive, colorActive
    );
    vp->DrawLine
    (
      Line2DPixel((_x + SCALED(_w) - spinWidth) * w, (_y + bigLine) * h, (_x + SCALED(_w)) * w, center * h),
      colorActive, colorActive
    );

    // draw background
    vp->DrawLine
    (
      Line2DPixel(CX(left), CY(bottom), CX(right) + 1.0, CY(bottom)),
      color, color
    );
    int lines = 1;
    float lineDist = fieldWidth;
    while (lineDist > 0.015)
    {
      lines *= 2;
      lineDist *= 0.5;
    }

    for (int i=0; i<=lines; i++)
    {
      float pos = CX(left + i * lineDist);
      float top = bottom - ((i % 2 == 0 || lines == 1) ? bigLine : smallLine);
      vp->DrawLine
      (
        Line2DPixel(pos, CY(top), pos, CY(bottom)),
        color, color
      );
    }

    // draw thumb
    vp->DrawLine
    (
      Line2DPixel(thumbPos * w, (_y + bigLine + space) * h, (thumbPos - 0.5 * thumbWidth) * w, (_y + SCALED(_h)) * h),
      colorActive, colorActive
    );
    vp->DrawLine
    (
      Line2DPixel((thumbPos - 0.5 * thumbWidth) * w, (_y + SCALED(_h)) * h, (thumbPos + 0.5 * thumbWidth) * w, (_y + SCALED(_h)) * h),
      colorActive, colorActive
    );
    vp->DrawLine
    (
      Line2DPixel((thumbPos + 0.5 * thumbWidth) * w, (_y + SCALED(_h)) * h, thumbPos * w, (_y + bigLine + space) * h), 
      colorActive, colorActive
    );
  }
}

static const float ArrowTextureWidth = 0.75;
static const float BorderSpaceWidth = 0.005;

static const float ArrowTextureHeight = 0.75 / Square(0.75); // 0.75 == UI aspect ratio
static const float BorderSpaceHeight = 0.005 / 0.75; // 0.75 == UI aspect ratio

CXSlider::CXSlider(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : Control(parent, CT_SLIDER, idc, cls), CSliderContainer(parent, cls)
{
  _leftSpinDown = false;
  _rightSpinDown = false;

  _arrowEmpty = GlobLoadTextureUI(FindPicture(cls >> "arrowEmpty"));
  _arrowFull = GlobLoadTextureUI(FindPicture(cls >> "arrowFull"));
  _border = GlobLoadTextureUI(FindPicture(cls >> "border"));
  _thumb = GlobLoadTextureUI(FindPicture(cls >> "thumb"));
}

bool CXSlider::OnKeyDown(int dikCode)
{
  int oldPos = _curPos;
  bool ret = CSliderContainer::DoKeyDown(dikCode);
  if (_curPos != oldPos)
  {
    OnEvent(CESliderPosChanged, _curPos);
    if (_parent) _parent->OnSliderPosChanged(this, _curPos);
  }
  switch (dikCode)
  {
  case DIK_LEFT:
  case INPUT_DEVICE_XINPUT + XBOX_Left:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXLeft:
    _leftSpinDown = true;
    break;
  case DIK_RIGHT:
  case INPUT_DEVICE_XINPUT + XBOX_Right:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXRight:
    _rightSpinDown = true;
    break;
  }
  return ret;
}

bool CXSlider::OnKeyUp(int dikCode)
{
  switch (dikCode)
  {
  case DIK_LEFT:
  case INPUT_DEVICE_XINPUT + XBOX_Left:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXLeft:
    _leftSpinDown = false;
    return true;
  case DIK_RIGHT:
  case INPUT_DEVICE_XINPUT + XBOX_Right:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXRight:
    _rightSpinDown = false;
    return true;
  }
  return false;
}


void CXSlider::OnLButtonDown(float x, float y)
{
  if ((_style & SL_DIR) == SL_VERT)
  {
    // Not implemented
  }
  else
  {
    float wArrow = ArrowTextureWidth * SCALED(_h);
    float wBorder = SCALED(BorderSpaceWidth);

    float xC1 = _x + wArrow + wBorder;
    float xC2 = _x + SCALED(_w) - wArrow - wBorder;

    if (x < _x + wArrow)
    {
      SetThumbPos(_curPos - _lineStep);
      OnEvent(CESliderPosChanged, _curPos);
      if (_parent) _parent->OnSliderPosChanged(this, _curPos);
      _leftSpinDown = true;
    }
    else if (x > _x + SCALED(_w) - wArrow)
    {
      SetThumbPos(_curPos + _lineStep);
      OnEvent(CESliderPosChanged, _curPos);
      if (_parent) _parent->OnSliderPosChanged(this, _curPos);
      _rightSpinDown = true;
    }
    else if (x >= xC1 && x <= xC2)
    {
      _thumbLocked = true;
      float coef = (x - xC1) / (xC2 - xC1);
      float pos = _minPos + coef * (_maxPos - _minPos);
      SetThumbPos(pos);
      OnEvent(CESliderPosChanged, _curPos);
      if (_parent) _parent->OnSliderPosChanged(this, _curPos);
    }
  }
}

void CXSlider::OnLButtonUp(float x, float y)
{
  _thumbLocked = false;
  _leftSpinDown = false;
  _rightSpinDown = false;
}

void CXSlider::OnMouseMove(float x, float y, bool active)
{
  if (_thumbLocked)
  {
    if ((_style & SL_DIR) == SL_VERT)
    {
      // Not implemented
    }
    else
    {
      float wArrow = ArrowTextureWidth * SCALED(_h);
      float wBorder = SCALED(BorderSpaceWidth);

      float xC1 = _x + wArrow + wBorder;
      float xC2 = _x + SCALED(_w) - wArrow - wBorder;

      saturate(x, xC1, xC2);
      float coef = (x - xC1) / (xC2 - xC1);
      float pos = _minPos + coef * (_maxPos - _minPos);
      SetThumbPos(pos);
      OnEvent(CESliderPosChanged, _curPos);
      if (_parent) _parent->OnSliderPosChanged(this, _curPos);
    }
  }
}

void CXSlider::OnDraw(UIViewport *vp, float alpha)
{
  if (_title)
  {
    if (IsFocused()) _title->SetTextColor(_titleColorActive);
    else _title->SetTextColor(_titleColorBase);
  }
  if (_value)
  {
    IControl *ctrl = _value->GetControl();
    if (ctrl)
    {
      if (IsFocused()) ctrl->SetTextColor(_valueColorActive);
      else ctrl->SetTextColor(_valueColorBase);
    }
  }

  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  // select color
  PackedColor color;
  if (!IsEnabled()) color = ModAlpha(_colorDisabled, alpha);
  else if (IsFocused()) color = ModAlpha(_colorActive, alpha);
  else color = ModAlpha(_color, alpha);

  // now, always draw using textures
  if ((_style & SL_DIR) == SL_VERT)
  {
    // Not implemented
  }
  else
  {
    float wArrow = ArrowTextureWidth * SCALED(_h);
    float wBorder = SCALED(BorderSpaceWidth);

    float xC1 = _x + wArrow + wBorder;
    float xC2 = _x + SCALED(_w) - wArrow - wBorder;

    // left arrow
    Draw2DParsExt pars;
    pars.Init();
    pars.spec|= (_shadow == 2)?UISHADOW : 0; 
    pars.SetColor(color);
    if (_leftSpinDown)
      pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(_arrowFull, 0, 0);
    else
      pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(_arrowEmpty, 0, 0);
    vp->Draw2D(pars, Rect2DPixel(CX(_x), CY(_y), CW(wArrow), CH(SCALED(_h))));

    // right arrow
    pars.SetU(1, 0);
    if (_rightSpinDown)
      pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(_arrowFull, 0, 0);
    else
      pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(_arrowEmpty, 0, 0);
    vp->Draw2D(pars, Rect2DPixel(CX(_x + SCALED(_w) - wArrow), CY(_y), CW(wArrow), CH(SCALED(_h))));

    // border
    float xx = CX(xC1);
    float ww = CW((xC2 - xC1));
    pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(_border, 0, 0);
    pars.SetU(0, 1);
    vp->Draw2D(pars, Rect2DPixel(xx, CY(_y), ww, CH(SCALED(_h))));

    // thumb
    float coef = 0;
    if (_maxPos > _minPos) coef = 1.0 / (_maxPos - _minPos);
    ww = CW((xC2 - xC1) * (_curPos - _minPos) * coef);
    pars.mip = GEngine->TextBank()->UseMipmap(_thumb, 0, 0);
    pars.SetU(0, 1);
    vp->Draw2D(pars, Rect2DPixel(xx, CY(_y), ww, CH(SCALED(_h))));
  }
}

///////////////////////////////////////////////////////////////////////////////
// class CScrollBar (used in ListBox etc.)

/*!
\patch 5104 Date 12/18/2006 by Jirka
- Fixed: UI - listbox scrollbars did not handle well thumb position for mouse events
*/

const float sbWidth = 0.02;
const float spinCoefV = 0.8f;
const float spinCoefH = 0.8f * Square(0.75f);

CScrollBar::CScrollBar(bool textures, bool horizontal, ParamEntryPar cls)
{
  _thumbLocked = false;
  _textures = textures;
  _horizontal = horizontal;

  if (textures)
  {
    _thumb = GlobLoadTextureUI(FindPicture(cls >> "thumb"));
    _arrowEmpty = GlobLoadTextureUI(FindPicture(cls >> "arrowEmpty"));
    _arrowFull = GlobLoadTextureUI(FindPicture(cls >> "arrowFull"));
    _border = GlobLoadTextureUI(FindPicture(cls >> "border"));
  }

  _leftSpinDown = false;
  _rightSpinDown = false;
  //set auto-scroll properties - disabled by default
  SetAutoScroll(-1,5);
  _autoScrollEnabled = false;
  _autoScrollRewind = false;
  _autoScrollAlpha = 1;
}

CScrollBar::CScrollBar(bool textures, bool horizontal)
{
  _thumbLocked = false;
  _textures = textures;
  _horizontal = horizontal;

  _leftSpinDown = false;
  _rightSpinDown = false;
  //set auto-scroll properties - disabled by default
  SetAutoScroll(-1,5);
  _autoScrollEnabled = false;
  _autoScrollRewind = false;
  _autoScrollAlpha = 1;
}

void CScrollBar::OnLButtonDown(float x, float y)
{
  const float ratio = GLOB_ENGINE->Width2D()/GLOB_ENGINE->Height2D();

  if (_horizontal)
  {
    float spinSize = _textures ? ArrowTextureWidth * _h : spinCoefH * _h;
    float borderSize = _textures ? spinSize + BorderSpaceWidth : spinSize;
    if (2.0 * borderSize >= _w) return; // too width scrollbar
    if (_maxPos <= _minPos) return;

    x -= _x;
    float invRange = (_w - 2.0f * borderSize) / (_maxPos - _minPos);
    //what should size of thumb be
    float thumbSizeReal = _page * invRange;
    //size of thumb which was drawn (height of thumb is at least _h)
    float thumbSize = floatMax(_page * invRange,_h/ratio); 
    //if thumbSize > thumbSizeReal we have to decrease invRange, so we have enough space to draw bigger thumb
    //scrollbar is to small
    if(thumbSize>(_w - 2.0f * borderSize)) 
      thumbSize = thumbSizeReal;
    invRange = (_w - 2.0f * borderSize - (thumbSize - thumbSizeReal)) / (_maxPos - _minPos);
    //where was thumb drawn (used to compare with mouse position)
    float thumbPos = borderSize + _curPos * invRange;

    if (x <= spinSize)
    {
      // lineUp
      _curPos -= _lineStep;
      _leftSpinDown = true;
    }
    else if (x <= borderSize)
    {
      // free space
      return;
    }
    else if (x <= thumbPos)
    {
      // pageUp
      _curPos -= _pageStep;
      _leftSpinDown = true;
    }
    else if (x <= thumbPos + thumbSize)
    {
      // thumb
      _thumbLocked = true;
      _thumbOffset = x - thumbPos;
    }
    else if (x <= _w - borderSize)
    {
      // pageDown
      _curPos += _pageStep;
      _rightSpinDown = true;
    }
    else if (x <= _w - spinSize)
    {
      // free space
      return;
    }
    else
    {
      // lineDown
      _curPos += _lineStep;
      _rightSpinDown = true;
    }
    saturate(_curPos, _minPos, _maxPos - _page);
  }
  else
  {
    float spinSize = _textures ? ArrowTextureHeight * _w : spinCoefV * _w;
    float borderSize = _textures ? spinSize + BorderSpaceHeight : spinSize;
    if (2.0 * borderSize >= _h) return; // too width scrollbar
    if (_maxPos <= _minPos) return;

    y -= _y;
    //space required for one item
    float invRange = (_h - 2.0f * borderSize) / (_maxPos - _minPos);
    //what should size of thumb be
    float thumbSizeReal = _page * invRange;   
    //size of thumb which was drawn (height of thumb is at least _w) - /0.75 is because of 4/3 screen drawing
    float thumbSize = floatMax(_page * invRange,_w*ratio);   
    if(thumbSize>(_h - 2.0f * borderSize)) 
      thumbSize = thumbSizeReal;
    //if thumbSize > thumbSizeReal we have to decrease invRange, so we have enough space to draw bigger thumb
    invRange = (_h - 2.0f * borderSize - (thumbSize - thumbSizeReal)) / (_maxPos - _minPos);
    //where was thumb drawn (used to compare with mouse position)
    float thumbPos = borderSize + _curPos * invRange;

    if (y <= spinSize)
    {
      // lineUp
      _curPos -= _lineStep;
      _leftSpinDown = true;
    }
    else if (y <= borderSize)
    {
      // free space
      return;
    }
    else if (y <= thumbPos)
    {
      // pageUp
      _curPos -= _pageStep;
      _leftSpinDown = true;
    }
    else if (y <= thumbPos + thumbSize)
    {
      // thumb
      _thumbLocked = true;
      //_thumbOffset is based on real position, not on position, where thumb is drawn
      _thumbOffset = y - thumbPos ;
    }
    else if (y <= _h - borderSize)
    {
      // pageDown
      _curPos += _pageStep;
      _rightSpinDown = true;
    }
    else if (y <= _h - spinSize)
    {
      // free space
      return;
    }
    else
    {
      // lineDown
      _curPos += _lineStep;
      _rightSpinDown = true;
    }
    saturate(_curPos, _minPos, _maxPos - _page);
  }
}

void CScrollBar::OnLButtonUp(float x, float y)
{
  _thumbLocked = false;
  _leftSpinDown = false;
  _rightSpinDown = false;
}

void CScrollBar::OnMouseHold(float x, float y)
{
  if (_thumbLocked)
  {
    const float ratio = GLOB_ENGINE->Width2D()/GLOB_ENGINE->Height2D();

    if (_horizontal)
    {
      float borderSize = _textures ? ArrowTextureWidth * _h + BorderSpaceWidth : spinCoefH * _h;
      if (2.0 * borderSize >= _w) return; // too width scrollbar
      if (_maxPos <= _minPos) return;

      x -= _x;
      float range = (_maxPos - _minPos) / (_w - 2.0f * borderSize);

      //recalibrate range, if _h/ratio  thumbSize is used
      float thumbSizeReal =  _page / range;
      float thumbSize =  floatMax(_page / range,_h/ratio);
      if(thumbSize>(_w - 2.0f * borderSize)) 
        thumbSize = thumbSizeReal;
      //if thumbSize > thumbSizeReal we have to decrease invRange, so we have enough space to draw bigger thumb
      range = (_maxPos - _minPos) / (_w - 2.0f * borderSize - (thumbSize - thumbSizeReal));

      float thumbPos = x - _thumbOffset;
      _curPos = (thumbPos - borderSize) * range;
      saturate(_curPos, _minPos, _maxPos - _page);
    }
    else
    {
      float borderSize = _textures ? ArrowTextureHeight * _w + BorderSpaceHeight : spinCoefV * _w;
      if (2.0 * borderSize >= _h) return; // too width scrollbar
      if (_maxPos <= _minPos) return;

      y -= _y;
      float range = (_maxPos - _minPos) / (_h - 2.0f * borderSize);
      
      //recalibrate range, if _h/ratio  thumbSize is used
      float thumbSizeReal =  _page / range;
      float thumbSize =  floatMax(_page / range,_w/ratio);
      if(thumbSize>(_h - 2.0f * borderSize)) 
        thumbSize = thumbSizeReal;
      //if thumbSize > thumbSizeReal we have to decrease invRange, so we have enough space to draw bigger thumb
      range = (_maxPos - _minPos) / (_h - 2.0f * borderSize - (thumbSize - thumbSizeReal));

      float thumbPos = y - _thumbOffset;
      _curPos = (thumbPos - borderSize) * range;
      saturate(_curPos, _minPos, _maxPos - _page);
    }
  }
}

static void DrawRect(UIViewport *vp, float x1, float y1, float x2, float y2, PackedColor color)
{
  vp->DrawLine(Line2DPixel(x1, y1, x2, y1), color, color);
  vp->DrawLine(Line2DPixel(x2, y1, x2, y2), color, color);
  vp->DrawLine(Line2DPixel(x2, y2, x1, y2), color, color);
  vp->DrawLine(Line2DPixel(x1, y2, x1, y1), color, color);
}

static void DrawRectNoClip(UIViewport *vp, float x1, float y1, float x2, float y2, PackedColor color)
{
  vp->DrawLineNoClip(Line2DPixel(x1, y1, x2, y1), color, color);
  vp->DrawLineNoClip(Line2DPixel(x2, y1, x2, y2), color, color);
  vp->DrawLineNoClip(Line2DPixel(x2, y2, x1, y2), color, color);
  vp->DrawLineNoClip(Line2DPixel(x1, y2, x1, y1), color, color);
}

void CScrollBar::AutoScroll()
{
  //_autoScrollSpeed<0, auto scroll is disabled
  if(_autoScrollSpeed<=0 || !_autoScrollEnabled) return;
  float timeDelta = (Glob.uiTime - _autoScrollLastTime);
  //if timeDelta < 0 it is not yet time to start scrolling...
  //..but it is time to restore faded items
  if(timeDelta < 0) 
  {
    _autoScrollAlpha =  floatMax(_autoScrollAlpha,timeDelta + _autoScrollDelay);
    saturate(_autoScrollAlpha,0.0f,1.0f);
    return;
  }

  if(_curPos< _maxPos - _page) 
  { //to avoid big jumps
    if(timeDelta > 0.5)timeDelta = 0.5;
    float positionUpdate = ((timeDelta/_autoScrollSpeed) * _lineStep);
    _curPos += positionUpdate;
    _autoScrollLastTime = Glob.uiTime;
  }
  else if(_autoScrollRewind && timeDelta>_autoScrollDelay)
  {//scrollbar is at the end
    _autoScrollAlpha = (_autoScrollDelay + 1 - timeDelta ); // 1 is one sec. until items disappear
    saturate(_autoScrollAlpha,0.0f,1.0f);
    if(_autoScrollRewind && timeDelta>_autoScrollDelay + 1.1f) // 1.1 is previous one sec. plus something little extra so items get fully faded
    {//restart
      _curPos =0;
      RestartAutoScroll();
    }
  }
  saturate(_curPos, _minPos, _maxPos - _page);
}

void CScrollBar::OnDraw(UIViewport *vp, float alpha, bool noClip)
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();
  //ratio is used for dividing a sometime happens, that w,h == 0 
  const float ratio = floatMax(w, 1) / floatMax(h, 1);

  float xx = CX(_x);
  float yy = CY(_y);
  float ww = CW(_w);
  float hh = CH(_h);

  PackedColor color = ModAlpha(_color, _autoScrollAlpha * alpha);
  AutoScroll();

  if (_textures)
  {
    if (_horizontal)
    {
      float wArrow = ArrowTextureWidth * _h;
      float wBorder = BorderSpaceWidth;

      float xC1 = _x + wArrow + wBorder;
      float xC2 = _x + _w - wArrow - wBorder;

      // left arrow
      Draw2DParsExt pars;
      pars.Init();
      pars.SetColor(color);
      // rotate the texture
      pars.uTL = 1; pars.vTL = 0;
      pars.uTR = 1; pars.vTR = 1;
      pars.uBR = 0; pars.vBR = 1;
      pars.uBL = 0; pars.vBL = 0;
      if (_leftSpinDown)
        pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(_arrowFull, 0, 0);
      else
        pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(_arrowEmpty, 0, 0);
      vp->Draw2D(pars, Rect2DPixel(CX(_x), CY(_y), CW(wArrow), CH(_h)));

      // right arrow
      pars.uTL = 0; pars.vTL = 1;
      pars.uTR = 0; pars.vTR = 0;
      pars.uBR = 1; pars.vBR = 0;
      pars.uBL = 1; pars.vBL = 1;
      if (_rightSpinDown)
        pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(_arrowFull, 0, 0);
      else
        pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(_arrowEmpty, 0, 0);
      vp->Draw2D(pars, Rect2DPixel(CX(_x + _w - wArrow), CY(_y), CW(wArrow), CH(_h)));

      // border
      float xx = CX(xC1);
      float ww = CW((xC2 - xC1));
      pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(_border, 0, 0);
      pars.uTL = 1; pars.vTL = 0;
      pars.uTR = 1; pars.vTR = 1;
      pars.uBR = 0; pars.vBR = 1;
      pars.uBL = 0; pars.vBL = 0;
      vp->Draw2D(pars, Rect2DPixel(xx, CY(_y), ww, CH(_h)));

      // thumb
      if (_maxPos > _minPos)
      {
        //space required for one item
        float invRange = (xC2 - xC1) / (_maxPos - _minPos);
        //what should size of thumb be
        float thumbSizeReal = _page * invRange; 
        //size of thumb do draw (height of thumb is at least _w)
        float thumbSize =  floatMax(_page * invRange,_h/ratio);
        //scrollbar is to small
        if(thumbSize>(xC2 - xC1)) 
          thumbSize = thumbSizeReal;
        //if thumbSize > thumbSizeReal we have to decrease invRange, so we have enough space to draw bigger thumb
        invRange = (xC2 - xC1 - (thumbSize - thumbSizeReal)) / (_maxPos - _minPos);
        //where thumb should be drawn
        float thumbPos = xC1 + _curPos * invRange;

        // calculate all sizes in pixels to avoid rounding problems
        float yy = CY(_y);
        float hh = CH(_h);
        float xx = CX(thumbPos);
        float ww = CW(thumbSize);

        pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(_thumb, 0, 0);

        /// keep the left and right 25 % in 1:1 mapping, resize the 50 % in the center
        if (ww > 2 * hh)
        {
          // resize the central part
          pars.uTL = 1; pars.vTL = 0;
          pars.uTR = 1; pars.vTR = 0.25;
          pars.uBR = 0; pars.vBR = 0.25;
          pars.uBL = 0; pars.vBL = 0;
          vp->Draw2D(pars, Rect2DPixel(xx, yy, hh, hh));

          pars.uTL = 1; pars.vTL = 0.25;
          pars.uTR = 1; pars.vTR = 0.75;
          pars.uBR = 0; pars.vBR = 0.75;
          pars.uBL = 0; pars.vBL = 0.25;
          vp->Draw2D(pars, Rect2DPixel(xx + hh, yy, ww - 2 * hh, hh));

          pars.uTL = 1; pars.vTL = 0.75;
          pars.uTR = 1; pars.vTR = 1;
          pars.uBR = 0; pars.vBR = 1;
          pars.uBL = 0; pars.vBL = 0.75;
          vp->Draw2D(pars, Rect2DPixel(xx + ww - hh, yy, hh, hh));
        }
        else
        {
          // resize border parts
          pars.uTL = 1; pars.vTL = 0;
          pars.uTR = 1; pars.vTR = 0.25;
          pars.uBR = 0; pars.vBR = 0.25;
          pars.uBL = 0; pars.vBL = 0;
          vp->Draw2D(pars, Rect2DPixel(xx, yy, 0.5 * ww, hh));

          pars.uTL = 1; pars.vTL = 0.75;
          pars.uTR = 1; pars.vTR = 1;
          pars.uBR = 0; pars.vBR = 1;
          pars.uBL = 0; pars.vBL = 0.75;
          vp->Draw2D(pars, Rect2DPixel(xx + 0.5 * hh, yy, 0.5 * ww, hh));
        }
      }
    }
    else
    {
      float hArrow = ArrowTextureHeight * _w;
      float hBorder = BorderSpaceHeight;

      float yC1 = _y + hArrow + hBorder;
      float yC2 = _y + _h - hArrow - hBorder;

      // top arrow
      Draw2DParsExt pars;
      pars.Init();
      pars.SetColor(color);
      if (_leftSpinDown)
        pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(_arrowFull, 0, 0);
      else
        pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(_arrowEmpty, 0, 0);
      vp->Draw2D(pars, Rect2DPixel(CX(_x), CY(_y), CW(_w), CH(hArrow)));

      // bottom arrow
      pars.SetV(1, 0);
      if (_rightSpinDown)
        pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(_arrowFull, 0, 0);
      else
        pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(_arrowEmpty, 0, 0);
      vp->Draw2D(pars, Rect2DPixel(CX(_x), CY(_y + _h - hArrow), CW(_w), CH(hArrow)));

      // border
      float yy = CY(yC1);
      float hh = CH((yC2 - yC1));
      pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(_border, 0, 0);
      pars.SetV(0, 1);
      vp->Draw2D(pars, Rect2DPixel(CX(_x), yy, CW(_w), hh));

      // thumb (with minimal height==_w)
      if (_maxPos > _minPos)
      {
        //space required for one item
        float invRange = (yC2 - yC1) / (_maxPos - _minPos);
        //what should size of thumb be
        float thumbSizeReal = _page * invRange;         
        //size of thumb do draw (height of thumb is at least _w) - ratio ensure rectangle is rectangle
        float thumbSize = floatMax(_page * invRange,_w*ratio); 
        //scrollbar is to small
        if(thumbSize>(yC2 - yC1)) 
          thumbSize = thumbSizeReal;
        //if thumbSize > thumbSizeReal we have to decrease invRange, so we have enough space to draw bigger thumb
        invRange = (yC2 - yC1 - (thumbSize - thumbSizeReal)) / (_maxPos - _minPos);
        //where thumb should be drawn
        float thumbPos = yC1 + _curPos * invRange;
    
        

        // calculate all sizes in pixels to avoid rounding problems
        float xx = CX(_x);
        float ww = CW(_w);
        float yy = CY(thumbPos);
        float hh = CH(thumbSize);

        pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(_thumb, 0, 0);

        // keep the top and bottom 25 % in 1:1 mapping, resize the 50 % in the center
        if (hh > 2 * ww)
        {
          // resize the central part
          pars.SetV(0, 0.25);
          vp->Draw2D(pars, Rect2DPixel(xx, yy, ww, ww));

          pars.SetV(0.25, 0.75);
          vp->Draw2D(pars, Rect2DPixel(xx, yy + ww, ww, hh - 2 * ww));

          pars.SetV(0.75, 1);
          vp->Draw2D(pars, Rect2DPixel(xx, yy + hh - ww, ww, ww));
        }
        else
        {
          // resize border parts
          pars.SetV(0, 0.25);
          vp->Draw2D(pars, Rect2DPixel(xx, yy, ww, 0.5 * hh));

          pars.SetV(0.75, 1);
          vp->Draw2D(pars, Rect2DPixel(xx, yy + 0.5 * hh, ww, 0.5 * hh));
        }
      }
    }
  }
  else
  {
    // frame
    if (noClip)
      DrawRectNoClip(vp, xx, yy, xx + ww, yy + hh, color);
    else
      DrawRect(vp, xx, yy, xx + ww, yy + hh, color);

    if (_horizontal)
    {
      // spins
      float spinSize = spinCoefH * _h;
      if (2.0 * spinSize >= _w) return; // too width scrollbar
      float borderY = CH(0.25 * _h);
      float centerY = CH(0.5 * _h);
      float borderX = CW(0.25 * spinSize);
      float left = _x + spinSize;
      float ll = CX(left);
      float right = _x + _w - spinSize;
      float rr = CX(right);
      if (noClip)
      {
        vp->DrawLineNoClip(
          Line2DPixel(ll, yy, ll, yy + hh),
          color, color
          );
        vp->DrawLineNoClip(
          Line2DPixel(ll - borderX, yy + borderY, xx + borderX, yy + centerY),
          color, color
          );
        vp->DrawLineNoClip(
          Line2DPixel(xx + borderX, yy + centerY, ll - borderX, yy + hh - borderY),
          color, color
          );
        vp->DrawLineNoClip(
          Line2DPixel(rr, yy, rr, yy + hh),
          color, color
          );
        vp->DrawLineNoClip(
          Line2DPixel(rr + borderX, yy + borderY, xx + ww - borderX, yy + centerY),
          color, color
          );
        vp->DrawLineNoClip(
          Line2DPixel(xx + ww - borderX, yy + centerY, rr + borderX, yy + hh - borderY),
          color, color
          );
      }
      else
      {
        vp->DrawLine(
          Line2DPixel(ll, yy, ll, yy + hh),
          color, color
          );
        vp->DrawLine(
          Line2DPixel(ll - borderX, yy + borderY, xx + borderX, yy + centerY),
          color, color
          );
        vp->DrawLine(
          Line2DPixel(xx + borderX, yy + centerY, ll - borderX, yy + hh - borderY),
          color, color
          );
        vp->DrawLine(
          Line2DPixel(rr, yy, rr, yy + hh),
          color, color
          );
        vp->DrawLine(
          Line2DPixel(rr + borderX, yy + borderY, xx + ww - borderX, yy + centerY),
          color, color
          );
        vp->DrawLine(
          Line2DPixel(xx + ww - borderX, yy + centerY, rr + borderX, yy + hh - borderY),
          color, color
          );
      }

      // thumb
      if (_maxPos <= _minPos) return;
      //space required for one item
      float invRange = (right - left) / (_maxPos - _minPos);
      //what should size of thumb be
      float thumbSizeReal = _page * invRange; 
      //size of thumb do draw (height of thumb is at least _w)
      float thumbSize =  floatMax(_page * invRange,_h/ratio);
      //if thumbSize > thumbSizeReal we have to decrease invRange, so we have enough space to draw bigger thumb
      //scrollbar is to small
      if(thumbSize>(right - left)) 
        thumbSize = thumbSizeReal;
      invRange = (right - left - (thumbSize - thumbSizeReal)) / (_maxPos - _minPos);
      //where thumb should be drawn
      float thumbPos =  left + _curPos * invRange;

      if (noClip)
        DrawRectNoClip(vp, thumbPos * w + 2, yy + 2, (thumbPos + thumbSize) * w - 2, yy + hh - 2, color);
      else
        DrawRect(vp, thumbPos * w + 2, yy + 2, (thumbPos + thumbSize) * w - 2, yy + hh - 2, color);
    }
    else
    {
      // spins
      float spinSize = spinCoefV * _w;
      if (2.0 * spinSize >= _h) return; // too width scrollbar
      float borderX = CW(0.25 * _w);
      float centerX = CW(0.5 * _w);
      float borderY = CH(0.25 * spinSize);
      float top = _y + spinSize;
      float tt = CY(top);
      float bottom = _y + _h - spinSize;
      float bb = CY(bottom);
      if (noClip)
      {
        vp->DrawLineNoClip(
          Line2DPixel(xx, tt, xx + ww, tt),
          color, color
          );
        vp->DrawLineNoClip(
          Line2DPixel(xx + borderX, tt - borderY, xx + centerX, yy + borderY),
          color, color
          );
        vp->DrawLineNoClip(
          Line2DPixel(xx + centerX, yy + borderY, xx + ww - borderX, tt - borderY),
          color, color
          );
        vp->DrawLineNoClip(
          Line2DPixel(xx, bb, xx + ww, bb),
          color, color
          );
        vp->DrawLineNoClip(
          Line2DPixel(xx + borderX, bb + borderY, xx + centerX, yy + hh - borderY),
          color, color
          );
        vp->DrawLineNoClip(
          Line2DPixel(xx + centerX, yy + hh - borderY, xx + ww - borderX, bb + borderY),
          color, color
          );
      }
      else
      {
        vp->DrawLine(
          Line2DPixel(xx, tt, xx + ww, tt),
          color, color
          );
        vp->DrawLine(
          Line2DPixel(xx + borderX, tt - borderY, xx + centerX, yy + borderY),
          color, color
          );
        vp->DrawLine(
          Line2DPixel(xx + centerX, yy + borderY, xx + ww - borderX, tt - borderY),
          color, color
          );
        vp->DrawLine(
          Line2DPixel(xx, bb, xx + ww, bb),
          color, color
          );
        vp->DrawLine(
          Line2DPixel(xx + borderX, bb + borderY, xx + centerX, yy + hh - borderY),
          color, color
          );
        vp->DrawLine(
          Line2DPixel(xx + centerX, yy + hh - borderY, xx + ww - borderX, bb + borderY),
          color, color
          );
      }

      // thumb
      if (_maxPos <= _minPos) return;

      //space required for one item
      float invRange = (bottom - top) / (_maxPos - _minPos);
      //what should size of thumb be
      float thumbSizeReal = _page * invRange;         
      //size of thumb do draw (height of thumb is at least _w)
      float thumbSize = floatMax(_page * invRange,_w*ratio); 
      //scrollbar is to small
      if(thumbSize>(bottom - top)) 
        thumbSize = thumbSizeReal;
      //if thumbSize > thumbSizeReal we have to decrease invRange, so we have enough space to draw bigger thumb
      invRange = (bottom - top - (thumbSize - thumbSizeReal)) / (_maxPos - _minPos);
      //where thumb should be drawn
      float thumbPos = top + _curPos * invRange;

      if (noClip)
        DrawRectNoClip(vp, xx + 2, thumbPos * h + 2, xx + ww - 2, (thumbPos + thumbSize) * h - 2, color);
      else
        DrawRect(vp, xx + 2, thumbPos * h + 2, xx + ww - 2, (thumbPos + thumbSize) * h - 2, color);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// class CProgressBar

CProgressBar::CProgressBar(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : Control(parent, CT_PROGRESS, idc, cls)
{
  SetRangeAndPos(0, 1, 0);

  _barColor = GetPackedColor(cls >> "colorBar");
  _texture = GlobLoadTextureUI(FindPicture(cls >> "texture"));
  _frameColor = GetPackedColor(cls >> "colorFrame");
  _style = cls>>"style";

  _enabled = false;
}

void CProgressBar::OnDraw(UIViewport *vp, float alpha)
{
  // draw bar
  if (_minPos <= _curPos && _curPos <= _maxPos)
  {
    PackedColor barColor = ModAlpha(_barColor, alpha);
    float coef = (_curPos - _minPos) / (_maxPos - _minPos);

    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();

    float xx = CX(_x);
    float yy = CY(_y);
    float ww = CW(SCALED(_w));
    float hh = CH(SCALED(_h));

    Draw2DParsExt pars;
    pars.mip = GEngine->TextBank()->UseMipmap(_texture, 0, 0);
    pars.SetColor(barColor);
    pars.Init();
    pars.spec|= (_shadow == 2)?UISHADOW : 0; 

    if((_style & ST_VERTICAL) != 0)
    {
      float hhFull = hh;
      hh = CH(coef * SCALED(_h));      
      pars.SetV(0, hh / CH(SCALED(_h)));
      vp->Draw2D(pars, Rect2DPixel(xx, yy + (hhFull - hh) , ww, hh));
      hh = CH(SCALED(_h));
    }
    else
    {
      ww = CW(coef * SCALED(_w));
      pars.SetU(0, ww / CW(SCALED(_w)));
      vp->Draw2D(pars, Rect2DPixel(xx, yy, ww, hh));
      ww = CW(SCALED(_w));
    }
    PackedColor frameColor = ModAlpha(_frameColor, alpha);

    // draw frame
    if(frameColor.A8()!=0)
    {
      DrawLeft(0, frameColor);
      DrawTop(0, frameColor);
      DrawBottom(0, frameColor);
      DrawRight(0, frameColor);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// class CAnimatedTexture

CAnimatedTexture::CAnimatedTexture(ControlsContainer *parent, int idc, ParamEntryPar cls)
: Control(parent, CT_ANIMATED_TEXTURE, idc, cls)
{
  _phase = 0;

  RString texture = cls >> "texture";
  if (texture.GetLength() > 0) _texture = GlobLoadTextureAnimated(FindPicture(texture));

  _enabled = false;
}

void CAnimatedTexture::OnDraw(UIViewport *vp, float alpha)
{
  if (!_texture) return;

  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  float xx = CX(_x);
  float yy = CY(_y);
  float ww = CW(SCALED(_w));
  float hh = CH(SCALED(_h));

  int n = _texture->Size();
  int i = toInt(_phase * n);
  if (i == n) i = 0; // cyclic

  Draw2DParsExt pars;
  pars.mip = GEngine->TextBank()->UseMipmap(_texture->Get(i), 0, 0);
  pars.SetColor(PackedWhite);
  pars.Init();
  pars.spec|= (_shadow == 2)?UISHADOW : 0; 
  vp->Draw2D(pars, Rect2DPixel(xx, yy, ww, hh));
}

///////////////////////////////////////////////////////////////////////////////
// class CListBox

CListBoxContainer::CListBoxContainer(ParamEntryPar cls)
{
  _ftColor = GetPackedColor(cls >> "colorText");
  _selColor = GetPackedColor(cls >> "colorSelect");
  ConstParamEntryPtr entry = cls.FindEntry("color");
  if (entry) _color = GetPackedColor(*entry);
  else _color = _ftColor;

  entry = cls.FindEntry("colorSelect2");
  if (entry) _selColor2 = GetPackedColor(*entry);
  else _selColor2 = _selColor;

  float period = 2.0f;
  entry = cls.FindEntry("period");
  if (entry) period = *entry;
  _speed = period > 0 ? H_PI / period : 0;

  ::GetValue(_selectSound, cls >> "soundSelect");

  entry = cls.FindEntry("canDrag");
  if (entry) _canDrag = *entry;
  else _canDrag = false;

  _selString = -1;
  _selBegin = _selString;
  _topString = 0;

  _showSelected = true;
  _readOnly = false;

  _start = Glob.uiTime;

  _stickyFocus = true;
}

bool CListBoxContainer::IsSelected(int row, int style) const
{
  // reimplementation is needed when GetSize() != Size()
  if (style & LB_MULTI)
    return row >= 0 && row < Size() && Get(row).selected;
  else
    return row == GetCurSel();
}

static int CompareItems( const CListBoxItem *str0, const CListBoxItem *str1 )
{
#if _VBS2 // categorize game vehicle classes seperately from VBS2 classes
  bool hasGame1 = strstr(str0->text,LocalizeString("STR_EDITOR_GAME_CLASS")) != NULL;
  bool hasGame2 = strstr(str1->text,LocalizeString("STR_EDITOR_GAME_CLASS")) != NULL;
  if (hasGame1 && hasGame2)
    return stricmp(str0->text, str1->text);
  if (!hasGame1 && hasGame2)
    return -1;
  if (hasGame1 && !hasGame2)
    return +1;
#endif
  return stricmp(str0->text, str1->text);
}

void CListBoxContainer::SortItems()
{
  QSort(Data(), Size(), CompareItems);
}

static int CompareItemsByValues( const CListBoxItem *str0, const CListBoxItem *str1 )
{
  int diff = str0->value - str1->value;
  if (diff != 0) return diff;
  return stricmp(str0->text, str1->text);
}

void CListBoxContainer::SortItemsByValue()
{
  QSort(Data(), Size(), CompareItemsByValues);
}

void CListBoxContainer::InsertString(int i, RString text)
{
  if (i < 0 || i > GetSize())
    return;
  Insert(i);
  Set(i).text = text;
  Set(i).data = "";
  Set(i).value = 0;
  Set(i).ftColor = _ftColor;
  Set(i).selColor = _selColor;
  if (i <= _selString)
  {
    _selString++;
  }
  if (i <= _selBegin)
  {
    _selBegin++;
  }
}

void CListBoxContainer::DeleteString(int i)
{
  if (i < 0 || i >= GetSize())
    return;
  Delete(i);
  if (i == GetSize())
  {
    _selString = GetSize() - 1;
    _selBegin = _selString;
    if (i == 0)
    {
      _topString = 0;
    }
  }
}

void CListBoxContainer::OnMouseZChanged(float dz)
{
  if (dz == 0) return;
  if (_topString > 0 && dz > 0)
  {
    // scroll up
    float scroll = -SCROLL_SPEED * dz;
    saturate(scroll, -SCROLL_MAX, -SCROLL_MIN);
    _topString += 0.1 * scroll;
    if (_topString <= 1.0)
      _topString = 0;
  }
  else if (_topString + NRows() <= GetSize() && dz < 0)
  {
    // scroll down
    float scroll = -SCROLL_SPEED * dz;
    saturate(scroll, SCROLL_MIN, SCROLL_MAX);
    _topString += 0.1 * scroll;
    if (_topString <= 1.0)
      _topString += 1.0;
    if (_topString + NRows() > GetSize())
      _topString = GetSize() - NRows();
  }
}

void CListBoxContainer::Check()
{
  saturateMin(_topString, GetSize() - NRows());
  saturateMax(_topString, 0);
}

bool CListBoxContainer::SelectStartString(unsigned nChar)
{
  //check if new string should be started
  if(Glob.uiTime.toFloat() - _lastCharTime > _maxHistoryDelay) _charBuffer = "";
  _lastCharTime = Glob.uiTime.toFloat();
  //create new string
  char c[2]; 
  c[0] = nChar;   c[1] = '\0';
  //add new char to current string
  _charBuffer=_charBuffer + c;
  //is not case sensitive
  _charBuffer.Lower();

  //first try currently selected item (to avoid some unnecessary selecting)
  RString item = this->GetText(_selString);
  //is not case sensitive
  item.Lower(); 
  if(strncmp(item,_charBuffer,_charBuffer.GetLength())==0)
  {//current one is OK, so we don't have to change anything
    return true;
  }

  for(int i = 0; i< GetSize(); i++)
  {//find first match between all items
    RString item = this->GetText(i);
    item.Lower(); 

    if(strncmp(item,_charBuffer,_charBuffer.GetLength())==0)
    {//select first match and go away
      SetCurSel(i,true);
      return true;
    }
  }
  return false;
}

/*
\VBS_patch 1.00 Date [2/1/2008] by clion
- Added: New parameter for ListBox's OnLostFocusLoseSelected, will cause
listbox to lose the currently select item when another control takes focus
*/

CListBox::CListBox(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : Control(parent, CT_LISTBOX, idc, cls), CListBoxContainer(cls),
  _scrollbar((_style & LB_TEXTURES) != 0, false, cls >> "ScrollBar")
{
  _font = GLOB_ENGINE->LoadFont(GetFontID(cls>>"font"));
  _size = cls >> "sizeEx";
  float height = cls >> "rowHeight";
  SetRowHeight(height);

  _selBgColor = _ftColor;
  ConstParamEntryPtr entry = cls.FindEntry("colorSelectBackground");
  if (entry) _selBgColor = GetPackedColor(*entry);

  entry = cls.FindEntry("colorSelectBackground2");
  if (entry) _selBgColor2 = GetPackedColor(*entry);
  else _selBgColor2 = _selBgColor;
  
  entry = cls.FindEntry("colorPicture");
  if (entry) _pictureColor = GetPackedColor(*entry);
  else _pictureColor = PackedWhite;

  entry = cls.FindEntry("colorPictureSelect");
  if (entry) _pictureSelectColor = GetPackedColor(*entry);
  else _pictureSelectColor = PackedWhite;

  entry = cls.FindEntry("colorFocused");
  if (entry) _focusedColor = GetPackedColor(*entry);
  else _focusedColor = _selColor;

  entry = cls.FindEntry("colorShadow");
  if (entry) _shadowColor = GetPackedColor(*entry);
  else _shadowColor = PackedBlack;

#if _VBS3
  entry = cls.FindEntry("OnLostFocusLoseSelected");
  if( entry ) _OnLostFocusLoseSelected = cls >> "OnLostFocusLoseSelected";
  else _OnLostFocusLoseSelected = false;
#endif

  _scrollUp = false;
  _scrollDown = false;

  _showStrings = (_rowHeight > 0)? _h / _rowHeight : 0;
  saturateMax(_showStrings, 0);

  _sbWidth = sbWidth;
  entry = cls.FindEntry("widthScrollBar");
  if (entry) _sbWidth = *entry;

  _scrollbar.SetPosition
  (
    (_x + SCALED(_w - _sbWidth)) * _scale, _y * _scale,
    SCALED(_sbWidth) * _scale, SCALED(_h) * _scale
  );
  _scrollbar.SetColor(_color);
  _scrollbar.SetRangeAndPos(0, 0, _showStrings, 0); 
  _scrollbar.SetSpeed(1, floatMax(_showStrings - 1, 1)); 
  _scrollbar.SetAutoScroll(cls >> "autoScrollSpeed",cls >> "autoScrollDelay",cls >> "autoScrollRewind");

  _line = GlobLoadTextureUI(FindPicture(Pars >> "CfgWrapperUI" >> "ListBox" >> "line"));
  _thumb = GlobLoadTextureUI(FindPicture(Pars >> "CfgWrapperUI" >> "ListBox" >> "thumb"));

  _dragRowWanted = -1;
  _dragX = 0;
  _dragY = 0;

  //_charBuffer contains what has user written. if _lastCharTime>_maxHistoryDelay, buffer is cleared
  _charBuffer = "";
  _maxHistoryDelay = cls >> "maxHistoryDelay";
  _lastCharTime = Glob.uiTime.toFloat();

  _quickSearch = true;
}

void CListBox::SetPos(float x, float y, float w, float h)
{
#if _VBS3
  // relative change in size of scrollbar
  _sbWidth *= w / _w;
#endif

  Control::SetPos(x, y, w, h);
  // update _showStrings
  _showStrings = _h / _rowHeight;
  saturateMax(_showStrings, 0);
  _scrollbar.SetSpeed(1, floatMax(_showStrings - 1, 1));  
}

#ifndef max
inline int max( int a, int b)
{
  return a>b ? a : b;
}
#endif
#if _VBS3
//! returns index under the current position
int CListBox::GetPosIndex(float x, float y)
{
  if (GetSize() == 0)
  {
    return -1;
  }
  float rowHeight = RowHeightUsed();
  float index = (y - _y) / rowHeight;
  if (index >= 0 && index < _showStrings && index < GetSize())
    return toIntFloor(_topString + index);
  else
  {
    return -1;
  }
}
#endif

void CListBox::SetCurSel(int sel, bool sendUpdate)
{
  if (GetSize() == 0) sel = -1;
  if (!sendUpdate && sel == _selString)
  {
    saturateMin(_topString, GetSize() - _showStrings);
    saturateMax(_topString, 0);
    return;
  }

  if (GetSize() == 0)
    _selString = -1;
  else if (sel < 0)
    _selString = -1;
  else if (sel >= GetSize())
    _selString = GetSize() - 1;
  else
    _selString = sel;

  if (_selString < _topString)
  {
    _topString = _selString;
    saturateMin(_topString, GetSize() - _showStrings);
    saturateMax(_topString, 0);
  }
  else if (_selString > _topString + _showStrings - 1)
    _topString = _selString - _showStrings + 1;

  if (sendUpdate)
  {
    OnEvent(CELBSelChanged, _selString);
    OnSelChanged(_selString);
    if (_parent) _parent->OnLBSelChanged(this, _selString);
  }
  Check();

  bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
  if (!shift) _selBegin = _selString;
  //if listBox change, restart auto-scroll (f.e. in armory, where one LB influence other)
  _scrollbar.RestartAutoScroll();
}

void CListBox::SetCurSel(float x, float y, bool sendUpdate)
{
  float rowHeight = RowHeightUsed();
  float index = (y - _y) / rowHeight;
  {
    _scrollUp = _scrollDown = false;
    if (index >= 0 && index < _showStrings)
    {
      SetCurSel(toIntFloor(_topString + index), sendUpdate);
      PlaySound(_selectSound);
      //if listBox change, restart auto-scroll (f.e. in armory, where one LB influence other)
      _scrollbar.RestartAutoScroll();
    }
  }
}

bool CListBox::OnKillFocus()
{ //_scrollbar with focus has always disabled auto-scroll
  _scrollbar.EnableAutoScroll(true);
#if _VBS3
  if(_OnLostFocusLoseSelected) _selString = -1;
#endif
  return Control::OnKillFocus();
}

bool CListBox::OnSetFocus(ControlFocusAction action, bool def)
{ //_scrollbar with focus has always disabled auto-scroll
  _scrollbar.EnableAutoScroll(false); 
  return Control::OnSetFocus(action, def);
}

bool CListBox::OnKeyUp(int dikCode)
{
  if (IsReadOnly()) return false;

  switch (dikCode)
  {
  case DIK_SPACE:
    if ((_style & LB_MULTI) && Size() == GetSize() && GetSize() > 0)
    {
      return true;
    }
    return false;
  }
  return false;
}

bool CListBox::OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  if(_quickSearch)
    return SelectStartString(nChar);
  else return false;
};

bool CListBox::OnKeyDown(int dikCode)
{
  if (IsReadOnly()) return false;

  switch (dikCode)
  {
  case DIK_UP:
  case INPUT_DEVICE_XINPUT + XBOX_Up:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYUp:
    if (!_stickyFocus && GetCurSel() <= 0) return false;
    {
      int sel = GetCurSel();
      if (sel>0) SetCurSel(sel - 1);
      if ((_style & LB_MULTI) && Size() == GetSize())
      {
        bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
        bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];

        if (!shift && !ctrl)
        {
          for (int i=0; i<GetSize(); i++)
            Set(i).selected = false;
        }
        if (!ctrl)
          Set(sel - 1).selected = true;
      }
    }
    if (GetCurSel() < _topString)
      _topString = GetCurSel();
    PlaySound(_selectSound);
    return true;
  case DIK_DOWN:
  case INPUT_DEVICE_XINPUT + XBOX_Down:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYDown:
    if (!_stickyFocus && GetCurSel() == GetSize() - 1) return false;
    {
      int sel = GetCurSel();
      SetCurSel(sel + 1);
      if ((_style & LB_MULTI) && Size() == GetSize())
      {
        bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
        bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];

        if (!shift && !ctrl)
        {
          for (int i=0; i<GetSize(); i++)
            Set(i).selected = false;
        }
        if (!ctrl)
          Set(sel + 1).selected = true;
      }
    }
    if (GetCurSel() > _topString + _showStrings - 1)
      _topString = GetCurSel() - _showStrings + 1;
    PlaySound(_selectSound);
    return true;
  case DIK_HOME:
    if (!_stickyFocus && GetCurSel() <= 0) return false;
    SetCurSel(0);
    // TODO: _style & LB_MULTI
    if (GetCurSel() < _topString)
      _topString = GetCurSel();
    PlaySound(_selectSound);
    return true;
  case DIK_END:
    if (!_stickyFocus && GetCurSel() == GetSize() - 1) return false;
    SetCurSel(GetSize() - 1);
    // TODO: _style & LB_MULTI
    if (GetCurSel() > _topString + _showStrings - 1)
      _topString = GetCurSel() - _showStrings + 1;
    PlaySound(_selectSound);
    return true;
  case DIK_PGUP:
    if (!_stickyFocus && GetCurSel() <= 0) return false;
    {
      int newSel = GetCurSel()-_showStrings;
      if (newSel<0) newSel=0;
      SetCurSel(newSel);
      // TODO: _style & LB_MULTI
    }
    if (GetCurSel() < _topString)
      _topString = GetCurSel();
    PlaySound(_selectSound);
    return true;
  case DIK_PGDN:
    if (!_stickyFocus && GetCurSel() == GetSize() - 1) return false;
    {
      int sel = GetCurSel();
      SetCurSel(sel + _showStrings);
      // TODO: _style & LB_MULTI
    }
    if (GetCurSel() > _topString + _showStrings - 1)
      _topString = GetCurSel() - _showStrings + 1;
    PlaySound(_selectSound);
    return true;
  case DIK_SPACE:
    if ((_style & LB_MULTI) && Size() == GetSize() && GetSize() > 0)
    {
      int sel = GetCurSel();
      bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
      if (ctrl)
        Set(sel).selected = !Get(sel).selected;
      return true;
    }
    return false;
  }
  return false;
}

void CListBox::OnMouseMove(float x, float y, bool active)
{
//  if (IsReadOnly()) return;

  if (!GInput.mouseL) return;

  if (_dragRowWanted >= 0)
  {
    const float dragLimit = 0.01f;
    if (Square(x - _dragX) + Square(y - _dragY) >= Square(dragLimit))
    {
      if (_parent)
      {
        _parent->OnLBDrag(this, _dragRowWanted);
        OnEvent(CELBDrag, _parent->GetDragRows());
      }
      _dragRowWanted = -1;
    }
  }

  OnMouseHold(x, y, active);
}

void CListBox::OnMouseHold(float x, float y, bool active)
{
//  if (IsReadOnly()) return;

  if (!GInput.mouseL) return;

  if (_scrollbar.IsEnabled() && _scrollbar.IsLocked())
  {
    _scrollbar.SetPos(_topString);
    _scrollbar.OnMouseHold(x, y);
    _topString = _scrollbar.GetPos();
  }
  else if (_scrollbar.IsEnabled() && _scrollbar.IsInside(x, y))
  {
  }
}

void CListBox::OnMouseZChanged(float dz)
{
//  if (IsReadOnly()) return;

  CListBoxContainer::OnMouseZChanged(dz);
}

void CListBox::OnLButtonDown(float x, float y)
{
//  if (IsReadOnly()) return;

  if (_scrollbar.IsEnabled() && _scrollbar.IsInside(x, y))
  {
    _scrollbar.SetPos(_topString);
    _scrollbar.OnLButtonDown(x, y);
    _topString = _scrollbar.GetPos();
  }
  else if (_idc >= 0 && _canDrag)
  {
    if (_parent)
    {
      float rowHeight = RowHeightUsed();
      float index = (y - _y) / rowHeight;
      if (index >= 0 && index < _showStrings)
      {
        int sel = toIntFloor(_topString + index);
        if (sel >= 0 && sel < GetSize())
        {
          _dragRowWanted = sel;
          _dragX = x;
          _dragY = y;
        }
      }
    }
  }
}

void CListBox::OnLButtonUp(float x, float y)
{
//  if (IsReadOnly()) return;
  _dragRowWanted = -1;
  float rowHeight = toInt(SCALED(_rowHeight) * GEngine->Height2D()) * (1.0f / GEngine->Height2D());

  if (_scrollbar.IsLocked())
  {
    _scrollbar.OnLButtonUp(x, y);
  }
  else if ( _scrollbar.IsInside(x,y) || !IsInside(x,y) ) 
  {
    return;
  }
  else if (!IsReadOnly() && GetSize() > 0)
  {
    float index = (y - _y) / SCALED(rowHeight);
    {
      _scrollUp = _scrollDown = false;
      if (index >= 0 && index < _showStrings)
      {
        int newSel = toIntFloor(_topString + index);
        saturate(newSel, 0, GetSize() - 1);
        SetCurSel(newSel);
        if ((_style & LB_MULTI) && Size() == GetSize())
        {
          bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
          bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
          if (!ctrl)
          {
            for (int i=0; i<GetSize(); i++)
              Set(i).selected = false;
          }
          if (shift)
          {
            if (newSel < _selBegin)
            {
              for (int i=newSel; i<=_selBegin; i++)
                Set(i).selected = true;
            }
            else if (_selBegin < newSel)
            {
              for (int i=_selBegin; i<=newSel; i++)
                Set(i).selected = true;
            }
            else
            {
              Set(newSel).selected = true;
            }
          }
          else Set(newSel).selected = !Get(newSel).selected;
        }
        PlaySound(_selectSound);
      }
    }
  }
}

/*!
\patch 5153 Date 4/10/2007 by Jirka
- Fixed: UI - Double click on listbox scrollbar does not launch the listbox action now
*/

void CListBox::OnLButtonDblClick(float x, float y)
{
  if (IsReadOnly()) return;

  if (_scrollbar.IsInside(x, y) || !IsInside(x, y)) return;

  if (_selString < 0) return;
  OnEvent(CELBDblClick, _selString);
  if (_parent) _parent->OnLBDblClick(IDC(), _selString);
}

float CListBox::RowHeightUsed() const
{
  float h2d = GEngine->Height2D();
  return toInt(SCALED(_rowHeight) * h2d) * (1.0f / h2d);
}

void CListBox::OnDraw(UIViewport *vp, float alpha)
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  float xx = CX(_x);
  float yy = CY(_y);
  float ww = CW(SCALED(_w));
  float hh = CH(SCALED(_h));

  PackedColor color = ModAlpha(_color, alpha);

  bool textures = (_style & LB_TEXTURES) != 0;
  bool showScrollbar = GetSize() > _showStrings && _sbWidth > 0;

  // enable scrollbar
  Rect2DFloat rect(_x, _y, SCALED(_w), SCALED(_h));
  float rectWidthDiff = 0;
  if (showScrollbar)
  {
    _scrollbar.Enable(true);
    _scrollbar.SetPosition
    (
      (_x + SCALED(_w) - SCALED(_sbWidth)) * _scale, _y * _scale,
      SCALED(_sbWidth) * _scale, SCALED(_h) * _scale
    );
    _scrollbar.SetRange(0, GetSize(), _showStrings);
    _scrollbar.SetPos(_topString);
    rectWidthDiff = SCALED(_sbWidth);
    rect.w -= rectWidthDiff; 
  }
  else
  {
    _scrollbar.Enable(false);
  }

  // draw items
  const float eps = 1e-6;
  float rowHeight = RowHeightUsed();

  int i = toIntFloor(_topString + eps);
  saturateMax(i, 0);
  float top = _y - (_topString - i) * rowHeight;
  bool canSelect = IsEnabled();
  while (top < _y + SCALED(_h) && i < GetSize())
  {
    bool selected = canSelect && IsSelected(i, GetStyle());
    DrawItem(vp, _scrollbar.GetAutoScrollAlpha() * alpha, i, selected, top, rect, rectWidthDiff);
    top += rowHeight;
    i++;
  }

  // draw focused
  if ((_style & LB_MULTI) && GetSize() > 0)
  {
    int sel = GetCurSel();
    float top = _y - (_topString - sel) * rowHeight;
    if (top + rowHeight > _y && top < _y + SCALED(_h))
    {
      Line2DPixelInfo lines[4];
      lines[0].c0 = _focusedColor;
      lines[0].c1 = _focusedColor;
      lines[0].beg = Point2DPixel(CX(rect.x), CY(top));
      lines[0].end = Point2DPixel(CX(rect.x + rect.w), CY(top));
      lines[1].c0 = _focusedColor;
      lines[1].c1 = _focusedColor;
      lines[1].beg = Point2DPixel(CX(rect.x + rect.w), CY(top));
      lines[1].end = Point2DPixel(CX(rect.x + rect.w), CY(top + rowHeight));
      lines[2].c0 = _focusedColor;
      lines[2].c1 = _focusedColor;
      lines[2].beg = Point2DPixel(CX(rect.x + rect.w), CY(top + rowHeight));
      lines[2].end = Point2DPixel(CX(rect.x), CY(top + rowHeight));
      lines[3].c0 = _focusedColor;
      lines[3].c1 = _focusedColor;
      lines[3].beg = Point2DPixel(CX(rect.x), CY(top + rowHeight));
      lines[3].end = Point2DPixel(CX(rect.x), CY(top));

      vp->DrawLines(lines, lenof(lines), Rect2DPixel(rect.x * w, rect.y * h, rect.w * w, rect.h * h));
    }
  }

  // draw scrollbar
  if (showScrollbar)
  {
    _scrollbar.OnDraw(vp, alpha);
    _topString = _scrollbar.GetPos();
  }
  else _topString = 0;

  // draw borders
  if (textures)
  {
    if (_line)
    {
      const int lineWidth = 4;
      Draw2DParsExt pars;
      pars.mip = GEngine->TextBank()->UseMipmap(_line, 0, 0);
      pars.SetColor(color);
      pars.Init();
      pars.spec|= (_shadow == 2)?UISHADOW : 0; 
      vp->Draw2D
      (
        pars, Rect2DPixel(xx, yy, lineWidth, hh),
        Rect2DPixel(xx, yy, ww, hh)
      );
      vp->Draw2D
      (
        pars, Rect2DPixel(xx + ww - lineWidth, yy, lineWidth, hh),
        Rect2DPixel(xx, yy, ww, hh)
      );
      if (showScrollbar)
        vp->Draw2D
        (
          pars, Rect2DPixel(xx + ww - CW(_sbWidth) - lineWidth, yy, lineWidth, hh),
          Rect2DPixel(xx, yy, ww, hh)
        );
    }
  }
  else DrawRect(vp, xx, yy, xx + ww, yy + hh, color);
}

#if _VBS3
/*
\VBS_patch 1.00 Date [2/1/2008] by clion
- Added: When draging listbox's items now will use listbox to draw the row thats being draged
*/
void CListBox::DrawItem
(
 UIViewport *vp, float alpha, int i, bool selected, float side, float top, const Rect2DFloat &rect, float widthDiff
)
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();
  const float border = TEXT_BORDER;
  bool textures = (_style & LB_TEXTURES) != 0;

  float rowHeight = RowHeightUsed();
  PackedColor color;
  bool focused = IsFocused();
  if (selected && _showSelected)
  {
    float factor = -0.5 * cos(_speed * (Glob.uiTime - _start)) + 0.5;

    PackedColor bgColor = _selBgColor;
    if (focused)
    {
      Color bgcol0=bgColor, bgcol1=_selBgColor2;
      bgColor = PackedColor(bgcol0 * factor + bgcol1 * (1 - factor));
    }
    bgColor = ModAlpha(bgColor, alpha);

    float xx = rect.x * w;
    float ww = rect.w * w;
    if (textures)
    {
      const int borderWidth = 2;
      xx += borderWidth;
      ww -= 2 * borderWidth;
    }
    Draw2DParsExt pars;
    pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
    pars.SetColor(bgColor);
    pars.Init();
    vp->Draw2D
      (
      pars, Rect2DPixel(xx, top * h, ww, rowHeight * h),
      Rect2DPixel(rect.x * w, rect.y * h, rect.w * w, rect.h * h)
      );

    color = i < Size() ? GetSelColor(i) : _selColor;
    if (focused)
    {
      Color col0=color, col1=_selColor2;
      color = PackedColor(col0 * (1 - factor) + col1 * factor);
    }
    color = ModAlpha(color, alpha);
  }
  else
  {
    color = i < Size() ? GetFtColor(i) : _ftColor;
    color = ModAlpha(color, alpha);
  }

  float left = side + border;

  Texture *texture = i < Size() ? GetTexture(i) : NULL;
  if (texture)
  {
    float height = texture->AHeight();
    if (height > 0)
    {
      float width = rowHeight * (texture->AWidth() * h) / (height * w);

      PackedColor pictureColor;
      if (selected && _showSelected)
        pictureColor = ModAlpha(_pictureSelectColor, alpha);
      else
        pictureColor = ModAlpha(_pictureColor, alpha);

      Draw2DParsExt pars;
      pars.mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
      pars.SetColor(pictureColor);
      pars.Init();

      vp->Draw2D
        (
        pars,
        Rect2DPixel(CX(rect.x + border), CY(top), CW(width), CH(rowHeight)),
        Rect2DPixel(rect.x * w, rect.y * h, rect.w * w, rect.h * h)
        );
      left += width + border;
    }
  }

  // update rect
  Rect2DFloat textRect(left, rect.y, rect.x + rect.w - left, rect.h);
  TextDrawAttr attr(SCALED(_size), _font, color);

  const char *text = GetText(i);
  if (text)
  {
    float t = top + 0.5 * (rowHeight - SCALED(_size));
    switch (_style & ST_POS)
    {
    case ST_RIGHT:
      left += textRect.w - vp->GetTextWidth(attr, text) - textBorder;
      break;
    case ST_CENTER:
      left += 0.5 * (textRect.w - vp->GetTextWidth(attr, text));
      break;
    default:
      Assert((_style & ST_POS) == ST_LEFT)
        left += textBorder;
      break;
    }
    vp->DrawText(Point2DFloat(left, t), textRect, attr, text);
  }
}
#endif //_VBS3

/*!
\patch 5153 Date 4/4/2007 by Jirka
- Fixed: UI - listboxes with texts of different color - color of selected text
*/

void CListBox::DrawItem
(
  UIViewport *vp, float alpha, int i, bool selected, float top, const Rect2DFloat &rect, float widthDiff
)
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();
  const float border = TEXT_BORDER;
  bool textures = (_style & LB_TEXTURES) != 0;
  float rowHeight = RowHeightUsed();

  const float height = _scale * SCALED(_size);
  const float offsetX = 0.045 * height;
  const float offsetY = 0.06 * height;

  PackedColor color;
  bool focused = IsFocused();
  if (selected && _showSelected)
  {
    float factor = -0.5 * cos(_speed * (Glob.uiTime - _start)) + 0.5;

    PackedColor bgColor = _selBgColor;
    if (focused)
    {
      Color bgcol0=bgColor, bgcol1=_selBgColor2;
      bgColor = PackedColor(bgcol0 * factor + bgcol1 * (1 - factor));
    }
    bgColor = ModAlpha(bgColor, alpha);

    float xx = rect.x * w;
    float ww = rect.w * w;
    if (textures)
    {
      const int borderWidth = 2;
      xx += borderWidth;
      ww -= 2 * borderWidth;
    }
    Draw2DParsExt pars;
    pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
    pars.SetColor(bgColor);
    pars.Init();
    pars.spec|= (_shadow == 2)?UISHADOW : 0; 
    vp->Draw2D
    (
      pars, Rect2DPixel(xx, top * h, ww, rowHeight * h),
      Rect2DPixel(rect.x * w, rect.y * h, rect.w * w, rect.h * h)
    );

    color = i < Size() ? GetSelColor(i) : _selColor;
    if (focused)
    {
      Color col0=color, col1=_selColor2;
      color = PackedColor(col0 * (1 - factor) + col1 * factor);
    }
    color = ModAlpha(color, alpha);
  }
  else
  {
    color = i < Size() ? GetFtColor(i) : _ftColor;
    color = ModAlpha(color, alpha);
  }

  float left = _x + border;
  
  Texture *texture = i < Size() ? GetTexture(i) : NULL;
  if (texture)
  {
    float height = texture->AHeight();
    if (height > 0)
    {
      float width = rowHeight * (texture->AWidth() * h) / (height * w);

      PackedColor pictureColor;
      if (selected && _showSelected)
        pictureColor = ModAlpha(_pictureSelectColor, alpha);
      else
        pictureColor = ModAlpha(_pictureColor, alpha);


      Draw2DParsExt pars;
      pars.mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
      pars.SetColor(pictureColor);
      pars.Init();
      pars.spec|= (_shadow == 2)?UISHADOW : 0; 

      if(_shadow == 1)
      {
        pars.SetColor(ModAlpha(_shadowColor, alpha));
        vp->Draw2D
          (
          pars,
          Rect2DPixel(CX(rect.x + border + offsetX), CY(top + offsetY), CW(width), CH(SCALED(_rowHeight))),
          Rect2DPixel((rect.x + offsetX) * w, (rect.y + offsetY) * h, rect.w * w, rect.h * h)
          );

        pars.SetColor(pictureColor);
      }

      vp->Draw2D
      (
        pars,
        Rect2DPixel(CX(rect.x + border), CY(top), CW(width), CH(rowHeight)),
        Rect2DPixel(rect.x * w, rect.y * h, rect.w * w, rect.h * h)
      );
      left += width + border;
    }
  }

  // update rect
  Rect2DFloat textRect(left, rect.y, rect.x + rect.w - left, rect.h);
  TextDrawAttr attr(SCALED(_size), _font, color, _shadow);

  const char *text = GetText(i);
  if (text)
  {
    float t = top + 0.5 * (rowHeight - SCALED(_size));
    switch (_style & ST_POS)
    {
    case ST_RIGHT:
      left += textRect.w - vp->GetTextWidth(attr, text) - textBorder;
      break;
    case ST_CENTER:
      left += 0.5 * (textRect.w - vp->GetTextWidth(attr, text));
      break;
    default:
      Assert((_style & ST_POS) == ST_LEFT)
      left += textBorder;
      break;
    }
    
    if(_shadow == 1)
    {
      attr._color = ModAlpha(_shadowColor, alpha);
      vp->DrawText
        (
        Point2DFloat(left + offsetX, top + offsetY), textRect, attr, text
        );
      attr._color = color;
    }
    vp->DrawText(Point2DFloat(left, t), textRect, attr, text);
  }
}

void CListBox::OnSelChanged(int curSel)
{
}
///////////////////////////////////////////////////////////////////////////////
// class CListNBox


CListNBox::CListNBox(ControlsContainer *parent, int idc, ParamEntryPar cls)
: CListBox(parent, idc,cls)
{
  _type = CT_LISTNBOX;
  _nColumns = 1;
  _columnsSize.Clear();
  _primaryColumn =0;
  _drawSideArrows = false;

  _drawSideArrows = cls >> "drawSideArrows";
  _idcLeft = cls >> "idcLeft"; 
  _idcRight = cls >> "idcRight"; 

  ConstParamEntryPtr entry = cls.FindEntry("columns");
  if (entry)
  {
    ParamEntryPar par = *entry;
    for(int i=0; i<par.GetSize();i++)
      _columnsSize.Add(par[i].GetFloat());
  }
  else _columnsSize.Add(0.0);
  _nColumns = _columnsSize.Size();

  _drawSideArrows = true;
  if(_idcLeft==-1 || _idcRight ==-1) _drawSideArrows = false;

  CShortcutButton *button = static_cast<CShortcutButton *>(_parent->GetCtrl(_idcLeft));
  if(button) 
  {
    button->ShowCtrl(false);
  }
  button = static_cast<CShortcutButton *>(_parent->GetCtrl(_idcRight));
  if(button)        
  { 
    button->ShowCtrl(false);
  }
}

void CListNBox::SetColumns(AutoArray<float> columnSize)
{
  _columnsSize.Clear();
  //original values
  int ncol = _nColumns;
  int cols = columnSize.Size();

  if(cols>=ncol)
  {
    for(int i=0; i<ncol; i++)
      _columnsSize.Add(columnSize[i]);
    for(int i= ncol; i < cols;i++)
      AddColumn(columnSize[i]);
  }
  else
  {
    for(int i=0; i<cols; i++)
      _columnsSize.Add(columnSize[i]);
    for(int i= cols; i < ncol;i++)
      _columnsSize.Add(0.0);
    for(int i= cols; i < ncol;i++)
      DeleteColumn(cols);
  }
  _nColumns = _columnsSize.Size();
}

void CListNBox::DeleteString(int row, int column)
{
  int i = row * _nColumns + column;
  if (i < 0 || i >= GetSize())
    return;
  Delete(i);
  if (row >= RowsCount()-1)
  {
    _selString = row - 1;
    _selBegin = _selString;
    if (i == 0)
    {
      _topString = 0;
    }
  }
}

void CListNBox::AddColumn(float columnPosition)
{
  int rows = RowsCount();
  _columnsSize.Add(columnPosition);
  _nColumns = _columnsSize.Size();
  int insertAt = _nColumns-1;
  for(int i =0; i< rows;i++)
  {
    if(insertAt < Size())CListBox::InsertString(insertAt,"");
    else CListBox::AddString("");
    insertAt+=_nColumns;
  }
}

void CListNBox::DeleteColumn(int columnIndex)
{
  if(columnIndex>= _nColumns) return;
  int removeIndex = (RowsCount()-1) * _nColumns + columnIndex;
  int rows = RowsCount();
  for(int i =0; i< rows; i++)
  {
    Delete(removeIndex);
    removeIndex -= _nColumns;
  }
  _columnsSize.Delete(columnIndex);
  _nColumns = _columnsSize.Size();
}

void CListNBox::SetCurSel(int sel, bool sendUpdate)
{
  if (GetSize() == 0) sel = -1;
  if (!sendUpdate && sel == _selString)
  {
    saturateMin(_topString, GetSize() - _showStrings);
    saturateMax(_topString, 0);
    return;
  }

  if (GetSize() == 0)
    _selString = -1;
  else if (sel < 0)
    _selString = -1;
  else if (sel >= RowsCount())
    _selString = RowsCount() - 1;
  else
    _selString = sel;

  if (_selString < _topString)
  {
    _topString = _selString;
    saturateMin(_topString, GetSize() - _showStrings);
    saturateMax(_topString, 0);
  }
  else if (_selString > _topString + _showStrings - 1)
    _topString = _selString - _showStrings + 1;

  if (sendUpdate)
  {
    OnEvent(CELBSelChanged, _selString);
    OnSelChanged(_selString);
    if (_parent) _parent->OnLBSelChanged(this, _selString);
  }
  Check();

  bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
  if (!shift) _selBegin = _selString;
  //if listBox change, restart auto-scroll (f.e. in armory, where one LB influence other)
  _scrollbar.RestartAutoScroll();
}

void CListNBox::OnLButtonDown(float x, float y)
{
  //  if (IsReadOnly()) return;
  float rowHeight = toInt(SCALED(_rowHeight) * GEngine->Height2D()) * (1.0f / GEngine->Height2D());

  if (_scrollbar.IsEnabled() && _scrollbar.IsInside(x, y))
  {
    _scrollbar.SetPos(_topString);
    _scrollbar.OnLButtonDown(x, y);
    _topString = _scrollbar.GetPos();
  }
  else if (_idc >= 0 && _canDrag)
  {
    if (_parent)
    {
      float index = (y - _y) / SCALED(rowHeight);
      if (index >= 0 && index < _showStrings)
      {
        int sel = toIntFloor(_topString + index);
        if (sel >= 0 && sel < GetSize())
        { 
          int index = sel * NColumns() +  PrimaryColumn();
          if(index < GetSize())
          {
            _dragRowWanted = index;
            _dragX = x;
            _dragY = y;
          }
        }
      }
    }
  }
}



void CListNBox::OnLButtonUp(float x, float y)
{//send OnLButtonUp event from LB to buttons, which are in the same area
  if(_drawSideArrows)
  { 
    CButton *buttonAdd = static_cast<CButton *>(_parent->GetCtrl(_idcLeft));
    if(buttonAdd) 
      buttonAdd->OnLButtonUp(x,y);

    CButton *buttonRemove = static_cast<CButton *>(_parent->GetCtrl(_idcRight));
    if(buttonRemove) 
      buttonRemove->OnLButtonUp(x,y);
  }
  CListBox::OnLButtonUp(x,y);
}

void CListNBox::OnLButtonDblClick(float x, float y)
{//to avoid LB double-click, when button is pressed
  if(_drawSideArrows)
  { 
    CButton *buttonAdd = static_cast<CButton *>(_parent->GetCtrl(_idcLeft));
    CButton *buttonRemove = static_cast<CButton *>(_parent->GetCtrl(_idcRight));

    if((buttonAdd && buttonRemove) && (buttonAdd->IsInside(x,y) || buttonRemove->IsInside(x,y))) return;
    else CListBox::OnLButtonDblClick(x,y);
  }
  else CListBox::OnLButtonDblClick(x,y);
}

void CListNBox::OnMouseZChanged(float dz)
{
  if (dz == 0) return;
  if (_topString > 0 && dz > 0)
  {
    // scroll up
    float scroll = -SCROLL_SPEED * dz;
    saturate(scroll, -SCROLL_MAX, -SCROLL_MIN);
    _topString += 0.1 * scroll;
    if (_topString <= 1.0)
      _topString = 0;
  }
  else if (_topString + NRows() <= RowsCount() && dz < 0)
  {
    // scroll down
    float scroll = -SCROLL_SPEED * dz;
    saturate(scroll, SCROLL_MIN, SCROLL_MAX);
    _topString += 0.1 * scroll;
    if (_topString <= 1.0)
      _topString += 1.0;
    if (_topString + NRows() > RowsCount())
      _topString = RowsCount() - NRows();
  }
}

void CListNBox::OnDraw(UIViewport *vp, float alpha)
{
 
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  float xx = CX(_x);
  float yy = CY(_y);
  float ww = CW(SCALED(_w));
  float hh = CH(SCALED(_h));

  PackedColor color = ModAlpha(_color, alpha);

  bool textures = (_style & LB_TEXTURES) != 0;
  bool showScrollbar = RowsCount() > _showStrings && _sbWidth > 0;

  // enable scrollbar
  Rect2DFloat rect(_x, _y, SCALED(_w), SCALED(_h));
  if (showScrollbar)
  {
    _scrollbar.Enable(true);
    _scrollbar.SetPosition
      (
      (_x + SCALED(_w) - SCALED(_sbWidth)) * _scale, _y * _scale,
      SCALED(_sbWidth) * _scale, SCALED(_h) * _scale
      );
    _scrollbar.SetRange(0, RowsCount(), _showStrings);
    _scrollbar.SetPos(_topString);
  }
  else
  {
    _scrollbar.Enable(false);
  }      


  // draw items
  const float eps = 1e-6;
  float rowHeight = RowHeightUsed();
  float topSelected = 0;
  int i = toIntFloor(_topString + eps);
  saturateMax(i, 0);
  float top = _y - (_topString - i) * rowHeight;
  bool canSelect = IsEnabled();
  int rows = Size()/_nColumns;
  while (top < _y + SCALED(_h) && i < rows)
  {
    for(int j=0; j<_nColumns; j++)
    {
      bool selected = canSelect && IsSelected(i, GetStyle());
      // if(selected) DrawItem(vp, _scrollbar.GetAutoScrollAlpha() * alpha, i,selected,top,rect);
      CListNBox::DrawItem(vp, _scrollbar.GetAutoScrollAlpha() * alpha, i,j, selected, top,_columnsSize[j], rect, showScrollbar);
      if(selected)
      {
        topSelected = top;
      }
    }
    i++;
    top += rowHeight;
  }

  // draw focused
  if ((_style & LB_MULTI) && GetSize() > 0)
  {
    int sel = GetCurSel();
    float rowHeight = RowHeightUsed();
    float top = _y - (_topString - sel) * rowHeight;
    if (top + rowHeight > _y && top < _y + SCALED(_h))
    {
      Line2DPixelInfo lines[4];
      lines[0].c0 = _focusedColor;
      lines[0].c1 = _focusedColor;
      lines[0].beg = Point2DPixel(CX(rect.x), CY(top));
      lines[0].end = Point2DPixel(CX(rect.x + rect.w), CY(top));
      lines[1].c0 = _focusedColor;
      lines[1].c1 = _focusedColor;
      lines[1].beg = Point2DPixel(CX(rect.x + rect.w), CY(top));
      lines[1].end = Point2DPixel(CX(rect.x + rect.w), CY(top + rowHeight));
      lines[2].c0 = _focusedColor;
      lines[2].c1 = _focusedColor;
      lines[2].beg = Point2DPixel(CX(rect.x + rect.w), CY(top + rowHeight));
      lines[2].end = Point2DPixel(CX(rect.x), CY(top + rowHeight));
      lines[3].c0 = _focusedColor;
      lines[3].c1 = _focusedColor;
      lines[3].beg = Point2DPixel(CX(rect.x), CY(top + rowHeight));
      lines[3].end = Point2DPixel(CX(rect.x), CY(top));

      vp->DrawLines(lines, lenof(lines), Rect2DPixel(rect.x * w, rect.y * h, rect.w * w, rect.h * h));
    }
  }
  if(showScrollbar)
  {
    _scrollbar.OnDraw(vp, alpha);
    _topString = _scrollbar.GetPos();
  }
  else _topString = 0;

  // draw borders
  if (textures)
  {
    if (_line)
    {
      const int lineWidth = 4;
      Draw2DParsExt pars;
      pars.mip = GEngine->TextBank()->UseMipmap(_line, 0, 0);
      pars.SetColor(color);
      pars.Init();
      pars.spec|= (_shadow == 2)?UISHADOW : 0; 
      vp->Draw2D
        (
        pars, Rect2DPixel(xx, yy, lineWidth, hh),
        Rect2DPixel(xx, yy, ww, hh)
        );
      vp->Draw2D
        (
        pars, Rect2DPixel(xx + ww - lineWidth, yy, lineWidth, hh),
        Rect2DPixel(xx, yy, ww, hh)
        );
      if (showScrollbar)
        vp->Draw2D
        (
        pars, Rect2DPixel(xx + ww - CW(_sbWidth) - lineWidth, yy, lineWidth, hh),
        Rect2DPixel(xx, yy, ww, hh)
        );
    }
  }
}

void CListNBox::DrawItem
(
 UIViewport *vp, float alpha, int row, int column, bool selected, float top,
 float offsetX, const Rect2DFloat &rect, bool showScrollbar
 )
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();
  const float border = TEXT_BORDER;
 // bool textures = (_style & LB_TEXTURES) != 0;

  PackedColor color;
  bool focused = IsFocused();
  float rowHeight = RowHeightUsed();
  if (selected && _showSelected)
  {
    float factor = -0.5 * cos(_speed * (Glob.uiTime - _start)) + 0.5;

    PackedColor bgColor = _selBgColor;
    if (focused)
    {
      Color bgcol0=bgColor, bgcol1=_selBgColor2;
      bgColor = PackedColor(bgcol0 * factor + bgcol1 * (1 - factor));
    }
    bgColor = ModAlpha(bgColor, alpha);
    //compute pixel precise position to avoid gaps
    float xi = toIntFloor(rect.x * w) * (1.0f/w);
    float yi = toIntFloor(rect.y * h) * (1.0f/h);
    float wi;
    if(!showScrollbar) wi = toIntFloor(rect.w * w) * (1.0f/w);
      else wi = toIntFloor((rect.w - SCALED(_sbWidth)) * w) * (1.0f/w);
    float hi = toIntFloor(rect.h * h) * (1.0f/h);

    Rect2DFloat rectangle =  Rect2DFloat(xi,yi,wi,hi);
    float xx = rectangle.x * w;
    float ww = rectangle.w * w;

    if(column== 0) //ensure that button arrows and rows selection are drawn only once
    {
      if(_drawSideArrows)
      {
        float buttonsOfset = toIntFloor(_rowHeight*h)*(1.0/h);  //space for add/remove item to/from pool 
        top = toIntFloor(top*h)*(1.0/h);
        float rowheight = toIntFloor(SCALED(_rowHeight * h))*(1.0/h);

        Draw2DParsExt pars;
        pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
        pars.SetColor(bgColor);
        pars.Init();
        pars.spec|= (_shadow == 2)?UISHADOW : 0; 
        vp->Draw2D
          (
          pars, Rect2DPixel(xx + buttonsOfset*h, top * h, ww - 2 * buttonsOfset*h+1, rowheight*h),
          Rect2DPixel(rectangle.x * w ,  top * h, rectangle.w * w+5, rectangle.h * h+5)
          );

        //screen ration so we can compute button width from item height
        float ratio = floatMax(h,1)/floatMax(w,1);
        //button size
        buttonsOfset = ratio*buttonsOfset;  //space for add/remove item to/from pool 


        CShortcutButton *button = static_cast<CShortcutButton *>(_parent->GetCtrl(_idcLeft));
        if(button) 
        {
          button->SetPos(rectangle.x ,top, buttonsOfset,rowheight);
          //show and hide - to ensure buttons have updated position when drawn
          button->ShowCtrl(true);
          button->OnDraw(vp,alpha);
          button->ShowCtrl(false);
        }
        button = static_cast<CShortcutButton *>(_parent->GetCtrl(_idcRight));
        if(button)        
        { 
          button->SetPos(rectangle.x + rectangle.w  - buttonsOfset ,top,buttonsOfset,rowheight); 
          //show and hide - to ensure buttons have updated position when drawn
          button->ShowCtrl(true);
          button->OnDraw(vp,alpha);
          button->ShowCtrl(false);
        }
      }
      else
      {//without buttons use whole width for line selection
        Draw2DParsExt pars;
        pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
        pars.SetColor(bgColor);
        pars.Init();
        pars.spec|= (_shadow == 2)?UISHADOW : 0; 
        float wi;
        if(!showScrollbar) wi =  rect.w * w;
        else wi = (rect.w - SCALED(_sbWidth)) * w;

        vp->Draw2D
          (
          pars, Rect2DPixel(xx, top * h, ww, rowHeight * h),
          Rect2DPixel(rect.x * w, rect.y * h, wi, rect.h * h)
          );
      }
    }

    color = GetSelColor(row, column);
    if (focused)
    {
      Color col0=color, col1=_selColor2;
      color = PackedColor(col0 * (1 - factor) + col1 * factor);
    }
    color = ModAlpha(color, alpha);
  }
  else
  {
    color = GetFtColor(row,column) ;
    color = ModAlpha(color, alpha);
  }

  float left = _x + border;

  Texture *texture =GetTexture(row,column);
  if (texture)
  {
    float height = texture->AHeight();
    if (height > 0)
    {
      float width = rowHeight * (texture->AWidth() * h) / (height * w);

      PackedColor pictureColor;
      if (selected && _showSelected)
        pictureColor = ModAlpha(_pictureSelectColor, alpha);
      else
        pictureColor = ModAlpha(_pictureColor, alpha);

      Draw2DParsExt pars;
      pars.mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
      pars.SetColor(pictureColor);
      pars.Init();
      pars.spec|= (_shadow == 2)?UISHADOW : 0; 

      vp->Draw2D
        (
        pars,
        Rect2DPixel(CX(rect.x+ (offsetX * rect.w) + border), CY(top), CW(width), CH(rowHeight)),
        Rect2DPixel((rect.x + offsetX * rect.w)* w, rect.y * h, rect.w * w, rect.h * h)
        );
      left += width + border;
    }
  }

  // update rect
  Rect2DFloat textRect(left, rect.y, rect.x + rect.w - left, rect.h);
  TextDrawAttr attr(SCALED(_size), _font, color, _shadow);

  const char *text = GetText(row, column);

  if (text)
  {
    float t = top + 0.5 * (rowHeight - SCALED(_size));
    switch (_style & ST_POS)
    {
    case ST_RIGHT:
      left += textRect.w - vp->GetTextWidth(attr, text) - textBorder;
      break;
    case ST_CENTER:
      left += 0.5 * (textRect.w - vp->GetTextWidth(attr, text));
      break;
    default:
      Assert((_style & ST_POS) == ST_LEFT)
        left += textBorder;
      break;
    }
    vp->DrawText(Point2DFloat(left + offsetX * rect.w, t), textRect, attr, text);
  }
}

void CListNBox::SortItems()
{
  AutoArray<SortLisNItem> primaryItems;
  int index = 0;
  for(int i = 0; i<RowsCount();i++)
  {    
    AutoArray<CListBoxItem> rowItems;
    for(int j=0;j<NColumns();j++)
      rowItems.Add(this->Get(index++));
    primaryItems.Add(SortLisNItem(GetText(i,PrimaryColumn()),GetValue(i,PrimaryColumn()),rowItems));
  }

  QSort(primaryItems.Data(), primaryItems.Size(), CompareNItems);

  int rows = RowsCount();
  int columns = NColumns();
  this->Clear();

  for(int i = 0; i<rows;i++)
    for(int j=0;j<columns;j++)
      this->Add(primaryItems[i].rowItems[j]);
}

void CListNBox::SortItemsByValue()
{
  AutoArray<SortLisNItem> primaryItems;
  int index = 0;
  for(int i = 0; i<RowsCount();i++)
  {    
    AutoArray<CListBoxItem> rowItems;
    for(int j=0;j<NColumns();j++)
      rowItems.Add(this->Get(index++));
    primaryItems.Add(SortLisNItem(GetText(i,PrimaryColumn()),GetValue(i,PrimaryColumn()),rowItems));
  }

  QSort(primaryItems.Data(), primaryItems.Size(), CompareNItemsByValues);

  int rows = RowsCount();
  int columns = NColumns();
  this->Clear();

  for(int i = 0; i<rows;i++)
    for(int j=0;j<columns;j++)
      this->Add(primaryItems[i].rowItems[j]);
}


///////////////////////////////////////////////////////////////////////////////
// class CXListBox

CXListBox::CXListBox(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : Control(parent, CT_XLISTBOX, idc, cls), CListBoxContainer(cls)
{
  _font = GLOB_ENGINE->LoadFont(GetFontID(cls>>"font"));
  _size = cls >> "sizeEx";
  
  _cycle = true;
  ConstParamEntryPtr entry = cls.FindEntry("cycle");
  if (entry) _cycle = *entry;

  _arrowEmpty = GlobLoadTextureUI(FindPicture(cls >> "arrowEmpty"));
  _arrowFull = GlobLoadTextureUI(FindPicture(cls >> "arrowFull"));
  _border = GlobLoadTextureUI(FindPicture(cls >> "border"));

  _focusedColor = GetPackedColor(cls >> "colorActive");
  _disabledColor = GetPackedColor(cls >> "colorDisabled");

  _leftSpinDown = false;
  _rightSpinDown = false; 
  //while LB don't have at least 2 items, it will be disabled  
  EnableCtrl(false);
}

void CXListBox::Down()
{
  int sel = GetCurSel();
  if (sel > 0)
    SetCurSel(sel - 1);
  else if (_cycle)
    SetCurSel(GetSize() - 1);
  _leftSpinDown = true;
  PlaySound(_selectSound);
}

void CXListBox::Up()
{
  int sel = GetCurSel();
  if (sel < GetSize() - 1)
    SetCurSel(sel + 1);
  else if (_cycle)
    SetCurSel(0);
  _rightSpinDown = true;
  PlaySound(_selectSound);
}

bool CXListBox::OnKeyDown(int dikCode)
{
  if (IsReadOnly()) return false;

  switch (dikCode)
  {
  case DIK_LEFT:
  case INPUT_DEVICE_XINPUT + XBOX_Left:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXLeft:
    if ((_style & SL_DIR) == SL_HORZ)
    {
      Down();
      return true;
    }
    else return false;
  case DIK_RIGHT:
  case INPUT_DEVICE_XINPUT + XBOX_Right:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXRight:
    if ((_style & SL_DIR) == SL_HORZ)
    {
      Up();
      return true;
    }
    else return false;
  case DIK_UP:
  case INPUT_DEVICE_XINPUT + XBOX_Up:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYUp:
    if ((_style & SL_DIR) == SL_VERT)
    {
      Down();
      return true;
    }
    else return false;
  case DIK_DOWN:
  case INPUT_DEVICE_XINPUT + XBOX_Down:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYDown:
    if ((_style & SL_DIR) == SL_VERT)
    {
      Up();
      return true;
    }
    else return false;
  }
  return false;
}

bool CXListBox::OnKeyUp(int dikCode)
{
  if (IsReadOnly()) return false;

  switch (dikCode)
  {
  case DIK_LEFT:
  case INPUT_DEVICE_XINPUT + XBOX_Left:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXLeft:
    if ((_style & SL_DIR) == SL_HORZ)
    {
      _leftSpinDown = false;
      return true;
    }
    else return false;
  case DIK_RIGHT:
  case INPUT_DEVICE_XINPUT + XBOX_Right:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXRight:
    if ((_style & SL_DIR) == SL_HORZ)
    {
      _rightSpinDown = false;
      return true;
    }
    else return false;
  case DIK_UP:
  case INPUT_DEVICE_XINPUT + XBOX_Up:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYUp:
    if ((_style & SL_DIR) == SL_VERT)
    {
      _leftSpinDown = false;
      return true;
    }
    else return false;
  case DIK_DOWN:
  case INPUT_DEVICE_XINPUT + XBOX_Down:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYDown:
    if ((_style & SL_DIR) == SL_VERT)
    {
      _rightSpinDown = false;
      return true;
    }
    else return false;
  }
  return false;
}

void CXListBox::OnLButtonDown(float x, float y)
{
  if (IsReadOnly()) return;

  // check zone
  if ((_style & SL_DIR) == SL_VERT)
  {
    float hZone = ArrowTextureHeight * SCALED(_h) + BorderSpaceHeight;

    if (y < _y + hZone)
    {
      Down();
    }
    else if (y > _y + SCALED(_h) - hZone)
    {
      Up();
    }
  }
  else
  {
    float wZone = ArrowTextureWidth * SCALED(_h) + BorderSpaceWidth;

    if (x < _x + wZone)
    {
      Down();
    }
    else 
    { //if (x > _x + _w - wZone) if you to disable "go to next item" if LB item is clicked 
      Up();
    }
  }
}

int CXListBox::AddString(RString text)
{//until second item is added, control remains disabled (player dosn't have items to choose from)
    int index = CListBoxContainer::AddString(text);
    if(GetSize()>1) EnableCtrl(true);
    return index;
}
 
void CXListBox::ClearStrings()
{//items are removed and control will be disabled, until 2 items are added again
    CListBoxContainer::Clear();
    EnableCtrl(false);
}

//! insert row
void CXListBox::InsertString(int i, RString text)
{//until second item is added, control remains disabled (player dosn't have items to choose from)
    CListBoxContainer::InsertString(i,text);
    if(GetSize()>1) EnableCtrl(true);
}

void CXListBox::DeleteString(int i)
{//items are removed and control will be disabled, if at least 2 items are present
    CListBoxContainer::DeleteString(i);   
    if(GetSize()<=1) EnableCtrl(false);
}

void CXListBox::OnLButtonUp(float x, float y)
{
  _leftSpinDown = false;
  _rightSpinDown = false;
}

void CXListBox::OnLButtonDblClick(float x, float y)
{
  if (IsReadOnly()) return;

  if (_selString < 0) return;
  OnEvent(CELBDblClick, _selString);
  if (_parent) _parent->OnLBDblClick(IDC(), _selString);
}

void CXListBox::OnDraw(UIViewport *vp, float alpha)
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  PackedColor color;
  if (!IsEnabled()) color = ModAlpha(_disabledColor, alpha);
  else if (IsFocused()) color = ModAlpha(_focusedColor, alpha);
  else color = ModAlpha(_color, alpha);

  float xC1, xC2, yC1, yC2;

  if (_style & LB_TEXTURES)
  {
    if ((_style & SL_DIR) == SL_VERT)
    {
      // Not implemented
      xC1 = _x;
      xC2 = _x + SCALED(_w);
      yC1 = _y;
      yC2 = _y + SCALED(_h);
    }
    else
    {
      float wArrow = ArrowTextureWidth * SCALED(_h);
      float wBorder = SCALED(BorderSpaceWidth);

      xC1 = _x + wArrow + wBorder;
      xC2 = (_x + SCALED(_w)) - 2 * (wArrow + wBorder);
      yC1 = _y;
      yC2 = _y + SCALED(_h);

      // left arrow
      if (GetSize() > 1 && (_selString > 0 || _cycle || _leftSpinDown))
      {
        Draw2DParsExt pars;
        pars.Init();
        pars.spec|= (_shadow == 2)?UISHADOW : 0; 
        pars.SetColor(color);
        if (_leftSpinDown)
          pars.mip = GEngine->TextBank()->UseMipmap(_arrowFull, 0, 0);
        else
          pars.mip = GEngine->TextBank()->UseMipmap(_arrowEmpty, 0, 0);
        vp->Draw2D(pars, Rect2DPixel(CX(_x), CY(_y), CW(wArrow), CH(SCALED(_h))));
      }
      // right arrow
      if (GetSize() > 1 && (_selString < GetSize() - 1 || _cycle || _rightSpinDown))
      {
        Draw2DParsExt pars;
        pars.Init();
        pars.spec|= (_shadow == 2)?UISHADOW : 0; 
        pars.SetColor(color);
        pars.SetU(1, 0);
        if (_rightSpinDown)
          pars.mip = GEngine->TextBank()->UseMipmap(_arrowFull, 0, 0);
        else
          pars.mip = GEngine->TextBank()->UseMipmap(_arrowEmpty, 0, 0);
        vp->Draw2D(pars, Rect2DPixel(CX(_x + SCALED(_w) - wArrow), CY(_y), CW(wArrow), CH(SCALED(_h))));
      }
      // border
      Draw2DParsExt pars;
      pars.Init();
      pars.spec|= (_shadow == 2)?UISHADOW : 0; 
      pars.SetColor(color);
      pars.mip = GEngine->TextBank()->UseMipmap(_border, 0, 0);
      vp->Draw2D(pars, Rect2DPixel(CX(_x + wArrow + wBorder), CY(_y), CW(SCALED(_w) - 2 * (wArrow + wBorder)), CH(SCALED(_h))));
    }
  }
  else
  {
    AUTO_STATIC_ARRAY(Line2DPixelInfo, lines, 10)
    Line2DPixelInfo line;
    line.c0 = color;
    line.c1 = color;

    if ((_style & SL_DIR) == SL_VERT)
    {
      float hArrow = ArrowTextureHeight * SCALED(_w);
      float hBorder = SCALED(BorderSpaceHeight);

      float yT1 = _y;
      float yT2 = _y + hArrow;
      yC1 = _y + hArrow + hBorder;
      yC2 = _y + SCALED(_h) - 2 * (hArrow - hBorder);
      float yB1 = _y + SCALED(_h) - hArrow;
      float yB2 = _y + SCALED(_h);

      xC1 = _x;
      float xC = _x + 0.5 * SCALED(_w);
      xC2 = _x + SCALED(_w);

      // top arrow
      if (_selString > 0 || (GetSize() > 1 &&  _cycle) || _leftSpinDown)
      {
        line.beg = Point2DPixel(CX(xC), CY(yT1));
        line.end = Point2DPixel(CX(xC1), CY(yT2));
        lines.Add(line);
        line.beg = Point2DPixel(CX(xC1), CY(yT2));
        line.end = Point2DPixel(CX(xC2), CY(yT2));
        lines.Add(line);
        line.beg = Point2DPixel(CX(xC2), CY(yT2));
        line.end = Point2DPixel(CX(xC), CY(yT1));
        lines.Add(line);

        // top arrow fill
        if (_leftSpinDown)
        {
          const int n = 3;
          Vertex2DPixel vs[n];
          // 0
          vs[0].x = CX(xC);
          vs[0].y = CY(yT1);
          vs[0].u = 0;
          vs[0].v = 0;
          vs[0].color = color;
          // 1
          vs[1].x = CX(xC2);
          vs[1].y = CY(yT2);
          vs[1].u = 0;
          vs[1].v = 0;
          vs[1].color = color;
          // 2
          vs[2].x = CX(xC1);
          vs[2].y = CY(yT2);
          vs[2].u = 0;
          vs[2].v = 0;
          vs[2].color = color;

          MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(NULL, 0, 0);
          vp->DrawPoly(mip, vs, n);
        }
      }
      // bottom arrow
      if (_selString < GetSize() - 1 || (GetSize() > 1 &&  _cycle) || _rightSpinDown)
      {
        line.beg = Point2DPixel(CX(xC), CY(yB2));
        line.end = Point2DPixel(CX(xC1), CY(yB1));
        lines.Add(line);
        line.beg = Point2DPixel(CX(xC1), CY(yB1));
        line.end = Point2DPixel(CX(xC2), CY(yB1));
        lines.Add(line);
        line.beg = Point2DPixel(CX(xC2), CY(yB1));
        line.end = Point2DPixel(CX(xC), CY(yB2));
        lines.Add(line);

        // bottom arrow fill
        if (_rightSpinDown)
        {
          const int n = 3;
          Vertex2DPixel vs[n];
          // 0
          vs[0].x = CX(xC1);
          vs[0].y = CY(yB1);
          vs[0].u = 0;
          vs[0].v = 0;
          vs[0].color = color;
          // 1
          vs[1].x = CX(xC2);
          vs[1].y = CY(yB1);
          vs[1].u = 0;
          vs[1].v = 0;
          vs[1].color = color;
          // 2
          vs[2].x = CX(xC);
          vs[2].y = CY(yB2);
          vs[2].u = 0;
          vs[2].v = 0;
          vs[2].color = color;

          MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(NULL, 0, 0);
          vp->DrawPoly(mip, vs, n);
        }
      }
    }
    else
    {
      float wArrow = ArrowTextureWidth * SCALED(_h);
      float wBorder = SCALED(BorderSpaceWidth);

      float xL1 = _x;
      float xL2 = _x + wArrow;
      xC1 = _x + wArrow + wBorder;
      xC2 = _x + SCALED(_w) - 2 * (wArrow + wBorder);
      float xR1 = _x + SCALED(_w) - wArrow;
      float xR2 = _x + SCALED(_w);

      yC1 = _y;
      float yC = _y + 0.5 * SCALED(_h);
      yC2 = _y + SCALED(_h);

      // left arrow
      if (IsEnabled() && (_selString > 0 || (GetSize() > 1 &&  _cycle) || _leftSpinDown))
      {
        line.beg = Point2DPixel(CX(xL1), CY(yC));
        line.end = Point2DPixel(CX(xL2), CY(yC1));
        lines.Add(line);
        line.beg = Point2DPixel(CX(xL2), CY(yC1));
        line.end = Point2DPixel(CX(xL2), CY(yC2));
        lines.Add(line);
        line.beg = Point2DPixel(CX(xL2), CY(yC2));
        line.end = Point2DPixel(CX(xL1), CY(yC));
        lines.Add(line);

        // left arrow fill
        if (_leftSpinDown)
        {
          const int n = 3;
          Vertex2DPixel vs[n];
          // 0
          vs[0].x = CX(xL1);
          vs[0].y = CY(yC);
          vs[0].u = 0;
          vs[0].v = 0;
          vs[0].color = color;
          // 1
          vs[1].x = CX(xL2);
          vs[1].y = CY(yC1);
          vs[1].u = 0;
          vs[1].v = 0;
          vs[1].color = color;
          // 2
          vs[2].x = CX(xL2);
          vs[2].y = CY(yC2);
          vs[2].u = 0;
          vs[2].v = 0;
          vs[2].color = color;

          MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(NULL, 0, 0);
          vp->DrawPoly(mip, vs, n);
        }
      }
      // right arrow
      if (IsEnabled() && (_selString < GetSize() - 1 || (GetSize() > 1 &&  _cycle) || _rightSpinDown))
      {
        line.beg = Point2DPixel(CX(xR2), CY(yC));
        line.end = Point2DPixel(CX(xR1), CY(yC1));
        lines.Add(line);
        line.beg = Point2DPixel(CX(xR1), CY(yC1));
        line.end = Point2DPixel(CX(xR1), CY(yC2));
        lines.Add(line);
        line.beg = Point2DPixel(CX(xR1), CY(yC2));
        line.end = Point2DPixel(CX(xR2), CY(yC));
        lines.Add(line);

        // right arrow fill
        if (_rightSpinDown)
        {
          const int n = 3;
          Vertex2DPixel vs[n];
          // 0
          vs[0].x = CX(xR1);
          vs[0].y = CY(yC1);
          vs[0].u = 0;
          vs[0].v = 0;
          vs[0].color = color;
          // 1
          vs[1].x = CX(xR2);
          vs[1].y = CY(yC);
          vs[1].u = 0;
          vs[1].v = 0;
          vs[1].color = color;
          // 2
          vs[2].x = CX(xR1);
          vs[2].y = CY(yC2);
          vs[2].u = 0;
          vs[2].v = 0;
          vs[2].color = color;

          MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(NULL, 0, 0);
          vp->DrawPoly(mip, vs, n);
        }
      }
    }

    // box
    line.beg = Point2DPixel(CX(xC1), CY(yC1));
    line.end = Point2DPixel(CX(xC2), CY(yC1));
    lines.Add(line);
    line.beg = Point2DPixel(CX(xC2), CY(yC1));
    line.end = Point2DPixel(CX(xC2), CY(yC2));
    lines.Add(line);
    line.beg = Point2DPixel(CX(xC2), CY(yC2));
    line.end = Point2DPixel(CX(xC1), CY(yC2));
    lines.Add(line);
    line.beg = Point2DPixel(CX(xC1), CY(yC2));
    line.end = Point2DPixel(CX(xC1), CY(yC1));
    lines.Add(line);

    vp->DrawLines(lines.Data(), lines.Size());
  }

  if (_selString < 0) return;

  PackedColor textColor = IsFocused() ? ModAlpha(GetSelColor(_selString), alpha) : ModAlpha(GetFtColor(_selString), alpha);

  const float border = TEXT_BORDER;
  xC1 += border;
  xC2 -= border;

  // picture
  Texture *texture = GetTexture(_selString);
  if (texture)
  {
    Draw2DParsExt pars;
    pars.mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
    pars.SetColor(textColor);
    pars.Init();
    pars.spec|= (_shadow == 2)?UISHADOW : 0; 

    float height = yC2 - yC1;
    float width = height * (texture->AWidth() * h) / (texture->AHeight() * w);
    vp->Draw2D
    (
      pars,
      Rect2DPixel(CX(xC1), CY(yC1), CW(width), CH(height)),
      Rect2DPixel(xC1 * w, yC1 * h, (xC2 - xC1) * w, height * h)
    );
    xC1 += width;
  }

  // text
  const char *text = GetText(_selString);
  if (text)
  {
    TextDrawAttr attr(SCALED(_size), _font, textColor,_shadow);

    float top = 0.5 * (yC1 + yC2 - SCALED(_size));
    float left = xC1;
	/*
    switch (_style & ST_POS)
    {
    case ST_RIGHT:
      left = xC2 - vp->GetTextWidth(attr, text) - textBorder;
      break;
    case ST_CENTER:
      left = 0.5 * ((xC2 + xC1) - vp->GetTextWidth(attr, text));
      break;
    default:
      Assert((_style & ST_POS) == ST_LEFT)
      left += textBorder;
      break;
    }*/
	left = _x + 0.5 * (SCALED(_w) - vp->GetTextWidth(attr, text));
	//xC2 = (_x + SCALED(_w)) - 2 * (wArrow + wBorder);
    vp->DrawText
    (
      Point2DFloat(left, top),
      Rect2DFloat(xC1, yC1,  (xC2 - _x - SCALED(BorderSpaceWidth)), yC2 - yC1),
      attr, text
    );
  }
}

void CXListBox::SetCurSel(int sel, bool sendUpdate)
{
  if (IsReadOnly()) return;

  if (GetSize() == 0)
    sel = -1;
  else if (sel < 0)
    sel = 0;
  else if (sel >= GetSize())
    sel = GetSize() - 1;

  if (sel == _selString) return;
  
  _selString = sel;
  if (sendUpdate)
  {
    OnEvent(CELBSelChanged, _selString);
    if (_parent) _parent->OnLBSelChanged(this, _selString);
  }
}

void CXListBox::SetCurSel(float x, float y, bool sendUpdate)
{
  // nothing to do
}

///////////////////////////////////////////////////////////////////////////////
// class CXCombo

CXCombo::CXCombo(ControlsContainer *parent, int idc, ParamEntryPar cls)
: Control(parent, CT_XCOMBO, idc, cls), CListBoxContainer(cls),
_scrollbar((_style & LB_TEXTURES) != 0, false, cls >> "ScrollBar")
{
  _expanded = false;
  _hasTitle = false;
  _listString = 0;

  _font = GLOB_ENGINE->LoadFont(GetFontID(cls>>"font"));
  _size = cls >> "sizeEx";
  _ftColorBase = _ftColor;
  _selColorBase = _selColor;
  
  ConstParamEntryPtr entry = cls.FindEntry("colorDisabled");
  if (entry) _disabledColor = GetPackedColor(*entry);
  else _disabledColor = _ftColorBase;
  
  entry = cls.FindEntry("colorBorder");
  if (entry) _borderColor = GetPackedColor(*entry);
  else _borderColor = _ftColorBase;

  entry = cls.FindEntry("colorSelectBorder");
  if (entry) _selBorderColor = GetPackedColor(*entry);
  else _selBorderColor = _selColorBase;

  entry = cls.FindEntry("colorDisabledBorder");
  if (entry) _disabledBorderColor = GetPackedColor(*entry);
  else _disabledBorderColor = _disabledColor;

  ParamEntryPar clsList = cls >> "List";
  _xList = clsList >> "x";
  _yList = clsList >> "y";
  _wList = clsList >> "w";
  _hList = clsList >> "h";

  _ftColor = GetPackedColor(clsList >> "colorText");
  _selColor = GetPackedColor(clsList >> "colorSelect");
  entry = cls.FindEntry("colorSelect2");
  if (entry) _selColor2 = GetPackedColor(*entry);
  else _selColor2 = _selColor;
  _bgColorList = GetPackedColor(clsList >> "colorBackground");
  _selBgColorList = GetPackedColor(clsList >> "colorSelectBackground");
  entry = cls.FindEntry("colorSelectBackground2");
  if (entry) _selBgColorList2 = GetPackedColor(*entry);
  else _selBgColorList2 = _selBgColorList;
  _borderColorList = GetPackedColor(clsList >> "colorBorder");
  _sizeList = clsList >> "sizeEx";
  _rowHeight = clsList >> "rowHeight";

  ConstParamEntryPtr clsTitle = cls.FindEntry("Title");
  if (clsTitle)
  {
    _hasTitle = true;
    _textTitle = (*clsTitle) >> "text";
    _xTitle = (*clsTitle) >> "x";
    _yTitle = (*clsTitle) >> "y";
    _wTitle = (*clsTitle) >> "w";
    _hTitle = (*clsTitle) >> "h";
    _fontTitle = GEngine->LoadFont(GetFontID((*clsTitle) >> "font"));
    _sizeTitle = (*clsTitle) >> "size";
    _ftColorTitle = GetPackedColor((*clsTitle) >> "colorText");
    _selColorTitle = GetPackedColor((*clsTitle) >> "colorSelect");
    _bgColorTitle = GetPackedColor((*clsTitle) >> "colorBackground");
    _selBgColorTitle = GetPackedColor((*clsTitle) >> "colorSelectBackground");
    _borderColorTitle = GetPackedColor((*clsTitle) >> "colorBorder");
    _selBorderColorTitle = GetPackedColor((*clsTitle) >> "colorSelectBorder");
    entry = clsTitle->FindEntry("colorDisabled");
    if (entry) _disabledColorTitle = GetPackedColor(*entry);
    else _disabledColorTitle = _ftColorTitle;
    entry = clsTitle->FindEntry("colorDisabledBackground");
    if (entry) _disabledBgColorTitle = GetPackedColor(*entry);
    else _disabledBgColorTitle = _bgColorTitle;
    entry = clsTitle->FindEntry("colorDisabledBorder");
    if (entry) _disabledBorderColorTitle = GetPackedColor(*entry);
    else _disabledBorderColorTitle = _borderColorTitle;
  }

  _showStrings = _hList / _rowHeight;
  saturateMax(_showStrings, 0);

  _scrollbar.SetPosition
  (
    (_xList + _wList - sbWidth) * _scale, _yList * _scale,
    sbWidth * _scale, _hList * _scale
  );
  _scrollbar.SetColor(_borderColorList);
  _scrollbar.SetRangeAndPos(0, 0, _showStrings, 0); 
  _scrollbar.SetSpeed(1, floatMax(_showStrings - 1, 1));  

  ::GetValue(_expandSound, cls >> "soundExpand");
  ::GetValue(_collapseSound, cls >> "soundCollapse");

  _line = GlobLoadTextureUI(FindPicture(Pars >> "CfgWrapperUI" >> "ListBox" >> "line"));
  _thumb = GlobLoadTextureUI(FindPicture(Pars >> "CfgWrapperUI" >> "ListBox" >> "thumb"));
  _arrowEmpty = GlobLoadTextureUI(FindPicture(Pars >> "CfgWrapperUI" >> "ListBox" >> "arrowEmpty"));
  _arrowFull = GlobLoadTextureUI(FindPicture(Pars >> "CfgWrapperUI" >> "ListBox" >> "arrowFull"));
  _boxLeft = GlobLoadTextureUI(FindPicture(Pars >> "CfgWrapperUI" >> "ListBox" >> "boxLeft"));
  _boxRight = GlobLoadTextureUI(FindPicture(Pars >> "CfgWrapperUI" >> "ListBox" >> "boxRight"));
  _boxHorz = GlobLoadTextureUI(FindPicture(Pars >> "CfgWrapperUI" >> "ListBox" >> "boxHorz"));

  _expandTime = Glob.uiTime - 60;
}

void CXCombo::SetTitle(RString text)
{
  if (_hasTitle) _textTitle = text;
}

bool CXCombo::OnKillFocus()
{
  _expanded = false;
  return Control::OnKillFocus();
}

void CXCombo::Expand(bool expand)
{
  if (expand)
  {
    if (!_expanded)
    {
      _expanded = true;
      SetListSel(_selString);
    }
  }
  else
  {
    _expanded = false;
  }
}

bool CXCombo::OnKeyDown(int dikCode)
{
  if (IsReadOnly()) return false;

  switch (dikCode)
  {
  case DIK_LEFT:
  case INPUT_DEVICE_XINPUT + XBOX_Left:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXLeft:
    if (_expanded)
    {
      _expanded = false;
      PlaySound(_collapseSound);
    }
    return true;
  case DIK_RIGHT:
  case INPUT_DEVICE_XINPUT + XBOX_Right:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXRight:
    _expanded = !_expanded;
    if (_expanded)
    {
      SetListSel(_selString);
      PlaySound(_expandSound);
      _expandTime = Glob.uiTime + 0.5;
    }
    else
    {
      SetCurSel(_listString);
      PlaySound(_collapseSound);
    }
    if (_parent) _parent->UpdateKeyHints();
    return true;
  case DIK_UP:
  case INPUT_DEVICE_XINPUT + XBOX_Up:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYUp:
    if (_expanded)
    {
      Down();
      return true;
    }
    return false;
  case DIK_DOWN:
  case INPUT_DEVICE_XINPUT + XBOX_Down:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYDown:
    if (_expanded)
    {
      Up();
      return true;
    }
    return false;
  case XBOX_A:
    if (_expanded)
    {
      SetCurSel(_listString);
      _expanded = false;
      if (_parent) _parent->UpdateKeyHints();
      PlaySound(_collapseSound);
      return true;
    }
    return false;
  }
  return false;
}

RString CXCombo::GetKeyHint(int button, bool &structured) const
{
  switch (button)
  {
  case INPUT_DEVICE_XINPUT + XBOX_A:
    if (_expanded) return LocalizeString(IDS_DISP_XBOX_HINT_SELECT);
    break;
  }
  return Control::GetKeyHint(button, structured);
}

void CXCombo::OnDraw(UIViewport *vp, float alpha)
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  bool focused = IsFocused();
  bool enabled = IsEnabled();
  bool textures = (_style & LB_TEXTURES) != 0;
  float rowHeight = RowHeightUsed();

  AUTO_STATIC_ARRAY(Line2DPixelInfo, lines, 32)
  Line2DPixelInfo line;

  // draw title
  if (_hasTitle)
  {
    PackedColor bgColor = enabled ?
      (focused ? ModAlpha(_selBgColorTitle, alpha) : ModAlpha(_bgColorTitle, alpha)) :
      ModAlpha(_disabledBgColorTitle, alpha);
    PackedColor lineColor = enabled ?
      (focused ? ModAlpha(_selBorderColorTitle, alpha) : ModAlpha(_borderColorTitle, alpha)) :
      ModAlpha(_disabledBorderColorTitle, alpha);
    PackedColor color = enabled ?
      (focused ? ModAlpha(_selColorTitle, alpha) : ModAlpha(_ftColorTitle, alpha)) :
      ModAlpha(_disabledColorTitle, alpha);
    Rect2DPixel rect(CX(_xTitle), CY(_yTitle), CW(SCALED(_wTitle)), CH(SCALED(_hTitle)));

    // background
    Draw2DParsExt pars;
    pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
    pars.SetColor(bgColor);
    pars.Init();
    pars.spec|= (_shadow == 2)?UISHADOW : 0; 
    vp->Draw2D(pars, rect, rect);

    // text
    float top = _yTitle + 0.5 * (SCALED(_hTitle - _sizeTitle));
    const float border = SCALED(TEXT_BORDER);
    TextDrawAttr attr(SCALED(_sizeTitle), _fontTitle, color,_shadow);
    vp->DrawText
    (
      Point2DFloat(_xTitle + border, top),
      Rect2DFloat(_xTitle, _yTitle, SCALED(_wTitle), SCALED(_hTitle)),
      attr, _textTitle
    );

    // border
    line.c0 = lineColor;
    line.c1 = lineColor;
    line.beg = Point2DPixel(rect.x, rect.y);
    line.end = Point2DPixel(rect.x + rect.w, rect.y);
    lines.Add(line);
    line.beg = Point2DPixel(rect.x + rect.w, rect.y);
    line.end = Point2DPixel(rect.x + rect.w, rect.y + rect.h);
    lines.Add(line);
    line.beg = Point2DPixel(rect.x + rect.w, rect.y + rect.h);
    line.end = Point2DPixel(rect.x, rect.y + rect.h);
    lines.Add(line);
    line.beg = Point2DPixel(rect.x, rect.y + rect.h);
    line.end = Point2DPixel(rect.x, rect.y);
    lines.Add(line);
  }

  PackedColor lineColor =
    enabled ?
    (focused ? ModAlpha(_selBorderColor, alpha) : ModAlpha(_borderColor, alpha)) :
    ModAlpha(_disabledBorderColor, alpha);
  PackedColor lineColorList = ModAlpha(_borderColorList, alpha);

  line.c0 = lineColor;
  line.c1 = lineColor;

  float wArrow = ArrowTextureWidth * SCALED(_h);
  float wBorder = SCALED(BorderSpaceWidth);

  float xBL = _x;
  float xBR = _x + SCALED(_w) - wArrow - wBorder;
  float xAL = _x + SCALED(_w) - wArrow;
  float xAR = _x + SCALED(_w);
  float yT = _y;
  float yC = _y + 0.5 * SCALED(_h);
  float yB = _y + SCALED(_h);

  // draw selected item

  DrawItem(vp, alpha, _selString, focused, _y, SCALED(_h), SCALED(_size), Rect2DFloat(xBL, _y, xBR - xBL, SCALED(_h)), false);

  // border
  if (textures)
  {
    Rect2DPixel clipRect(CX(_x), CY(_y), CW(SCALED(_w)), CH(SCALED(_h)));
    const int lineVert = 4;
    
    Draw2DParsExt pars;
    pars.mip = GEngine->TextBank()->UseMipmap(_boxLeft, 0, 0);
    pars.SetColor(lineColor);
    pars.Init();
    pars.spec|= (_shadow == 2)?UISHADOW : 0; 
    vp->Draw2D
    (
      pars, Rect2DPixel(CX(xBL), CY(yT), lineVert, CY(yB) - CY(yT)), clipRect
    );
    pars.mip = GEngine->TextBank()->UseMipmap(_boxRight, 0, 0);
    vp->Draw2D
    (
      pars, Rect2DPixel(CX(xBR) - lineVert, CY(yT), lineVert, CY(yB) - CY(yT)), clipRect
    );
    pars.mip = GEngine->TextBank()->UseMipmap(_boxHorz, 0, 0);
    vp->Draw2D
    (
      pars, Rect2DPixel(CX(xBL) + lineVert, CY(yT), CX(xBR) - CX(xBL) - lineVert - lineVert, CY(yB) - CY(yT)),
      clipRect
    );
  }
  else
  {
    line.beg = Point2DPixel(CX(xBL), CY(yT));
    line.end = Point2DPixel(CX(xBR), CY(yT));
    lines.Add(line);
    line.beg = Point2DPixel(CX(xBR), CY(yT));
    line.end = Point2DPixel(CX(xBR), CY(yB));
    lines.Add(line);
    line.beg = Point2DPixel(CX(xBR), CY(yB));
    line.end = Point2DPixel(CX(xBL), CY(yB));
    lines.Add(line);
    line.beg = Point2DPixel(CX(xBL), CY(yB));
    line.end = Point2DPixel(CX(xBL), CY(yT));
    lines.Add(line);
  }

  // arrow
  if (focused)
  {
    if (textures)
    {
      Draw2DParsExt pars;
      pars.Init();
      pars.spec|= (_shadow == 2)?UISHADOW : 0; 
      pars.SetColor(lineColor);
      pars.SetU(1, 0);
      if (Glob.uiTime < _expandTime)
        pars.mip = GEngine->TextBank()->UseMipmap(_arrowFull, 0, 0);
      else
        pars.mip = GEngine->TextBank()->UseMipmap(_arrowEmpty, 0, 0);
      vp->Draw2D(pars, Rect2DPixel(CX(xAL), CY(yT), CX(xAR) - CX(xAL), CY(yB) - CY(yT)));
    }
    else
    {
      line.beg = Point2DPixel(CX(xAL), CY(yT));
      line.end = Point2DPixel(CX(xAR), CY(yC));
      lines.Add(line);
      line.beg = Point2DPixel(CX(xAR), CY(yC));
      line.end = Point2DPixel(CX(xAL), CY(yB));
      lines.Add(line);
      line.beg = Point2DPixel(CX(xAL), CY(yB));
      line.end = Point2DPixel(CX(xAL), CY(yT));
      lines.Add(line);
    }
  }

  // draw list
  _scrollbar.Enable(false);
  if (_expanded)
  {
    // enable scrollbar
    float wList = _wList;
    bool showScrollbar = GetSize() > _showStrings;
    if (showScrollbar)
    {
      _scrollbar.Enable(true);
      _scrollbar.SetRange(0, GetSize(), _showStrings);
      _scrollbar.SetPos(_topString);
      wList -= sbWidth; 
    }

    float xLL = _xList;
    float xLR = _xList + SCALED(wList);
    float yLT = _yList;
    float yLB = _yList + SCALED(_hList);

    // background
    Rect2DFloat rect(_xList, _yList, SCALED(wList), SCALED(_hList));
    Rect2DPixel clipRect(CX(_xList), CY(_yList), CW(SCALED(_wList)), CH(SCALED(_hList)));
    Draw2DParsExt pars;
    pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
    pars.SetColor(_bgColorList);
    pars.Init();
    pars.spec|= (_shadow == 2)?UISHADOW : 0; 
    vp->Draw2D(pars, clipRect, clipRect);

    // items
    const float eps = 1e-6;
    int i = toIntFloor(_topString + eps);
    saturateMax(i, 0);
    float top = _yList - (_topString - i) * rowHeight;
    while (top < _yList + SCALED(_hList) && i < GetSize())
    {
      bool selected = i == _listString;
      DrawItem
      (
        vp, alpha, i, selected,
        top, rowHeight, _sizeList, rect, true
      );
      top += rowHeight;
      i++;
    }

    // scrollbar
    if (showScrollbar)
    { 
      _scrollbar.SetPosition
        (
        (_xList + SCALED(_wList - sbWidth)) * _scale, _yList * _scale,
        SCALED(sbWidth) * _scale, SCALED(_hList) * _scale
        );
      _scrollbar.OnDraw(vp, alpha);
    }

    // border
    line.c0 = lineColorList;
    line.c1 = lineColorList;
    if (textures)
    {
      if (_line)
      {
        const int lineWidth = 4;

        Draw2DParsExt pars;
        pars.mip = GEngine->TextBank()->UseMipmap(_line, 0, 0);
        pars.SetColor(lineColorList);
        pars.Init();
        pars.spec|= (_shadow == 2)?UISHADOW : 0; 

        vp->Draw2D(pars, Rect2DPixel(CX(xLL), CY(yLT), lineWidth, CY(yLB) - CY(yLT)), clipRect);
        float xLS = _xList + _wList;
        vp->Draw2D(pars, Rect2DPixel(CX(xLS) - lineWidth, CY(yLT), lineWidth, CY(yLB) - CY(yLT)), clipRect);
        if (showScrollbar)
          vp->Draw2D(pars, Rect2DPixel(CX(xLR) - lineWidth, CY(yLT), lineWidth, CY(yLB) - CY(yLT)), clipRect);
      }
    }
    else
    {
      line.beg = Point2DPixel(CX(xLL), CY(yLT));
      line.end = Point2DPixel(CX(xLR), CY(yLT));
      lines.Add(line);
      line.beg = Point2DPixel(CX(xLR), CY(yLT));
      line.end = Point2DPixel(CX(xLR), CY(yLB));
      lines.Add(line);
      line.beg = Point2DPixel(CX(xLR), CY(yLB));
      line.end = Point2DPixel(CX(xLL), CY(yLB));
      lines.Add(line);
      line.beg = Point2DPixel(CX(xLL), CY(yLB));
      line.end = Point2DPixel(CX(xLL), CY(yLT));
      lines.Add(line);
    }
  }

  vp->DrawLines(lines.Data(), lines.Size());
}

void CXCombo::DrawItem
(
  UIViewport *vp, float alpha, int i, bool selected, float top, float rowHeight, float size,
  const Rect2DFloat &rect, bool list
)
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  bool textures = (_style & LB_TEXTURES) != 0;
  float factor = -0.5 * cos(_speed * (Glob.uiTime - _start)) + 0.5;

  PackedColor textColor;
  if (list)
  {
    if (selected)
    {
      Color col0=GetSelColor(i), col1=_selColor2;
      textColor = PackedColor(col0 * factor + col1 * (1 - factor));
      textColor = ModAlpha(textColor, alpha);
    }
    else textColor = ModAlpha(GetFtColor(i), alpha);
  }
  else
  {
    if (!IsEnabled()) textColor = ModAlpha(_disabledColor, alpha);
    else if (selected)
    {
      Color col0=_selColorBase, col1=_selColor2;
      textColor = PackedColor(col0 * factor + col1 * (1 - factor));
      textColor = ModAlpha(textColor, alpha);
    }
    else textColor = ModAlpha(_ftColorBase, alpha);
  }
  Rect2DPixel clipRect(rect.x * w, rect.y * h, rect.w * w, rect.h * h);

  const float border = SCALED(TEXT_BORDER);
  float xL = rect.x + border;
  float xR = rect.x + rect.w - border;

  // background
  if (selected && (list || textures))
  {
    Color bgcol0=_selBgColorList, bgcol1=_selBgColorList2;
    PackedColor bgColor = PackedColor(bgcol0 * factor + bgcol1 * (1 - factor));
    bgColor = ModAlpha(bgColor, alpha);

    float xx = CX(rect.x);
    float yy = CY(top);
    float ww = CW(rect.w);
    float hh = CH(rowHeight);
    if (textures)
    {
      const int borderWidth = 2;
      xx += borderWidth;
      ww -= 2 * borderWidth;
      if (!list)
      {
        yy += borderWidth;
        hh -= 2 * borderWidth;
      }
    }

    Draw2DParsExt pars;
    pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
    pars.SetColor(bgColor);
    pars.Init();
    pars.spec|= (_shadow == 2)?UISHADOW : 0; 

    vp->Draw2D(pars, Rect2DPixel(xx, yy, ww, hh), clipRect);
  }

  // picture
  Texture *texture = GetTexture(i);
  if (texture)
  {
    float width = rowHeight * (texture->AWidth() * h) / (texture->AHeight() * w);
    PackedColor pictureColor = ModAlpha(PackedWhite, alpha);

    Draw2DParsExt pars;
    pars.mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
    pars.SetColor(pictureColor);
    pars.Init();
    pars.spec|= (_shadow == 2)?UISHADOW : 0; 
    vp->Draw2D(pars, Rect2DPixel(CX(xL), CY(top), CW(width), CH(rowHeight)), clipRect);
    xL += width;
  }

  // text
  const char *text = GetText(i);
  if (text)
  {
    TextDrawAttr attr(SCALED(size), _font, textColor,_shadow);

    float t = top + 0.5 * (rowHeight - size);
    float left = xL;
    switch (_style & ST_POS)
    {
    case ST_RIGHT:
      left = xR - vp->GetTextWidth(attr, text) - textBorder;
      break;
    case ST_CENTER:
      left = 0.5 * (xL + xR - vp->GetTextWidth(attr, text));
      break;
    default:
      Assert((_style & ST_POS) == ST_LEFT)
      left += textBorder;
      break;
    }
    vp->DrawText(Point2DFloat(left, t), rect, attr, text);
  }
}

void CXCombo::SetCurSel(int sel, bool sendUpdate)
{
  if (IsReadOnly()) return;

  if (GetSize() == 0)
    sel = -1;
  else if (sel < 0)
    sel = 0;
  else if (sel >= GetSize())
    sel = GetSize() - 1;

  if (sel == _selString)
  {
    if (_parent) _parent->OnLBSelUnchanged(IDC(), _selString);
    return;
  }

  _selString = sel;
  
  if (sendUpdate)
  {
    OnEvent(CELBSelChanged, _selString);
    if (_parent) _parent->OnLBSelChanged(this, _selString);
  }
}

void CXCombo::SetCurSel(float x, float y, bool sendUpdate)
{
  // nothing to do
}

void CXCombo::SetListSel(int sel)
{
  if (!_expanded) return;

  if (GetSize() == 0)
    sel = -1;
  else if (sel < 0)
    sel = 0;
  else if (sel >= GetSize())
    sel = GetSize() - 1;

  if (sel == _listString) return;

  _listString = sel;

  if (_listString < _topString)
  {
    _topString = _listString;
    saturateMin(_topString, GetSize() - _showStrings);
    saturateMax(_topString, 0);
  }
  else if (_listString > _topString + _showStrings - 1)
    _topString = _listString - _showStrings + 1;

  OnEvent(CELBListSelChanged, _listString);
  if (_parent) _parent->OnLBListSelChanged(IDC(), _listString);
}

void CXCombo::Down()
{
  if (_listString > 0) SetListSel(_listString - 1);
  PlaySound(_selectSound);
}

void CXCombo::Up()
{
  if (_listString < GetSize() - 1) SetListSel(_listString + 1);
  PlaySound(_selectSound);
}

void CXCombo::InsertString(int i, RString text)
{
  if (i < 0 || i > GetSize())
    return;
  CListBoxContainer::InsertString(i, text);
  if (i <= _listString)
  {
    _listString++;
  }
}

void CXCombo::DeleteString(int i)
{
  if (i < 0 || i >= GetSize())
    return;
  CListBoxContainer::DeleteString(i);
  if (i == GetSize())
  {
    _listString = GetSize() - 1;
  }
}

///////////////////////////////////////////////////////////////////////////////
// class CCombo

/*
\patch 5118 Date 1/19/2007 by Pete
- Fixed: combo boxes auto-expand upwards if there is not enough screen space to expand downwards
*/

CCombo::CCombo(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : Control(parent, CT_COMBO, idc, cls), CListBoxContainer(cls),
  _scrollbar((_style & LB_TEXTURES) != 0, false, cls >> "ScrollBar")
{
  _mouseSel = _selString;
  _wholeHeight = cls>>"wholeHeight";
  _bgColor = GetPackedColor(cls >> "colorBackground");
  _selBgColor = _ftColor;
  ConstParamEntryPtr entry = cls.FindEntry("colorSelectBackground");
  if (entry) _selBgColor = GetPackedColor(*entry);
  _font = GLOB_ENGINE->LoadFont(GetFontID(cls>>"font"));
  _size = cls >> "sizeEx";

  _expanded = false;
  _expandedUp = false;
  _scrollUp = false;
  _scrollDown = false;

  _showStrings = (_wholeHeight - _h) / (_size);
  if (_showStrings < 0)
    _showStrings = 0;

  _scrollbar.SetPosition
  (
    (_x + SCALED(_w) - SCALED(sbWidth)) * _scale, (_y + SCALED(_h)) * _scale,
    SCALED(sbWidth) * _scale, (SCALED(_wholeHeight) - SCALED(_h)) * _scale
  );
  _scrollbar.SetColor(_color);
  _scrollbar.SetRangeAndPos(0, 0, _showStrings, 0); 
  _scrollbar.SetSpeed(1, floatMax(_showStrings - 1, 1)); 

  ::GetValue(_expandSound, cls >> "soundExpand");
  ::GetValue(_collapseSound, cls >> "soundCollapse");

  //_charBuffer contains what has user written. if _lastCharTime>_maxHistoryDelay, buffer is cleared
  _charBuffer = "";
  _maxHistoryDelay = cls >> "maxHistoryDelay";
  _lastCharTime = Glob.uiTime.toFloat();

  _arrowEmpty = GlobLoadTextureUI(FindPicture(cls>>"arrowEmpty"));
  _arrowFull = GlobLoadTextureUI(FindPicture(cls>>"arrowFull"));
}

void CCombo::SetPos(float x, float y, float w, float h)
{
  Control::SetPos(x, y, w, h);
  // update _showStrings
  _showStrings = (_wholeHeight - _h) / (_size);
  saturateMax(_showStrings, 0);
  _scrollbar.SetSpeed(1, floatMax(_showStrings - 1, 1));  
}

const float border = TEXT_BORDER;

bool CCombo::IsInsideExt(float x, float y) const
{
  if (!_expanded) return false;

  float height = floatMin
  (
    SCALED(_wholeHeight - _h),
    GetSize() * SCALED(_size)
  ) + SCALED(_h);

  float width = 0;
  for (int i=0; i<GetSize(); i++)
  {
    const char *text = GetText(i);
    if (text)
      saturateMax(width, GLOB_ENGINE->GetTextWidth(SCALED(_size), _font, text));
  }
  width += 2 * border;
  if (GetSize() > _showStrings)
    width += SCALED(sbWidth); 
  saturateMax(width, SCALED(_w));
  if (_expandedUp)
    return x >= _x && x < _x + width && y >= _y - height + SCALED(_h) && y < _y;
  else
    return x >= _x && x < _x + width && y >= _y + SCALED(_h) && y < _y + height;
}

/*!
\patch 5139 Date 3/14/2007 by Jirka
- Fixed: UI - wrong focus switching from expanded combo box (through click on the other control)
*/

bool CCombo::IsInside(float x, float y) const
{
  if (Control::IsInside(x, y)) return true;
  return IsInsideExt(x, y);
}

void CCombo::SetTopString()
{
  if (_mouseSel < _topString)
    _topString = max(_mouseSel, 0);
  else if (_mouseSel > _topString + _showStrings - 1)
    _topString = _mouseSel - _showStrings + 1;
}

void CCombo::SetCurSel(int sel, bool sendUpdate)
{
  if (GetSize() == 0)
    _selString = -1;
  else if (sel < 0)
    _selString = 0;
  else if (sel >= GetSize())
    _selString = GetSize() - 1;
  else
    _selString = sel;

  _mouseSel = _selString;

  SetTopString();

  if (sendUpdate)
  {
    OnEvent(CELBSelChanged, _selString);
    OnSelChanged(_selString);
    if (_parent) _parent->OnLBSelChanged(this, _selString);
  }
  Check();
}

void CCombo::SetCurSel(float x, float y, bool sendUpdate)
{
  if (!_expanded || !IsInsideExt(x, y)) return;

  float index = (y - (_y + SCALED(_h))) / (SCALED(_size));
  if (index >= 0 && index < _showStrings)
  {
    SetCurSel(toIntFloor(_topString + index));
    PlaySound(_selectSound);
  }
}

bool CCombo::OnKillFocus()
{
  _expanded = false;
  return Control::OnKillFocus();
}

void CCombo::Expand(bool expand)
{
  if (expand)
  {
    if (!_expanded && _showStrings > 0)
    {
      _expanded = true;
      SetTopString();
    }
  }
  else
  {
    _expanded = false;
  }
}

bool CCombo::OnKeyDown(int dikCode)
{
  if (IsReadOnly()) return false;

  switch (dikCode)
  {
  case DIK_SPACE:
    if (_expanded)
    {
      if (!GInput.mouseL)
      {
        _expanded = false;
        PlaySound(_collapseSound);
      }
    }
    else
    {
      if (_showStrings > 0)
      {
        _expanded = true;
        SetTopString();
        PlaySound(_expandSound);
      }
    }
    return true;
  case DIK_UP:
    if (_expanded && !GInput.mouseL)
    {
      SetCurSel(GetCurSel() - 1);
      PlaySound(_selectSound);
    }
    return true;
  case DIK_DOWN:
    if (_expanded && !GInput.mouseL)
    {
      SetCurSel(GetCurSel() + 1);
      PlaySound(_selectSound);
    }
    return true;
  case INPUT_DEVICE_XINPUT + XBOX_Up:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYUp:
    if (!_expanded && _showStrings > 0)
    {
      _expanded = true;
      SetTopString();
      PlaySound(_expandSound);
    }
    else if (!GInput.mouseL)
    {
      SetCurSel(GetCurSel() - 1);
      PlaySound(_selectSound);
    }
    return true;
  case INPUT_DEVICE_XINPUT + XBOX_Down:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYDown:
    if (!_expanded && _showStrings > 0)
    {
      _expanded = true;
      PlaySound(_expandSound);
      SetTopString();
    }
    else if (!GInput.mouseL)
    {
      SetCurSel(GetCurSel() + 1);
      PlaySound(_selectSound);
    }
    return true;
  }
  return false;
}

bool CCombo::OnKeyUp(int dikCode)
{
  if (IsReadOnly()) return false;

  switch (dikCode)
  {
  case DIK_SPACE:
  case DIK_UP:
  case DIK_DOWN:
  case XBOX_Up:
  case XBOX_LeftThumbYUp:
  case XBOX_Down:
  case XBOX_LeftThumbYDown:
    return true;
  }
  return false;
}

bool CCombo::OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  return SelectStartString(nChar);
};


void CCombo::OnLButtonDown(float x, float y)
{
  if (_expanded && _scrollbar.IsEnabled() && _scrollbar.IsInside(x, y))
  {
    _scrollbar.SetPos(_topString);
    _scrollbar.OnLButtonDown(x, y);
    _topString = _scrollbar.GetPos();
  }
  else if (_showStrings > 0 && !IsInsideExt(x, y))
  {
    _expanded = !_expanded;
    if (_expanded)
    {
      const int h = GLOB_ENGINE->Height2D();
      float curHeight = floatMin(SCALED(_wholeHeight - _h), GetSize() * SCALED(_size));
      float top = _y + SCALED(_h);
      _expandedUp = CY(top) + CH(curHeight) > h && !(CY(_y) - CH(curHeight) < 0);
      PlaySound(_expandSound);
    }
    else
      PlaySound(_collapseSound);
    SetTopString();
  }
}

void CCombo::OnLButtonUp(float x, float y)
{
  if (_scrollbar.IsLocked())
  {
    _scrollbar.OnLButtonUp(x, y);
  }
  else if (_scrollbar.IsEnabled() && _scrollbar.IsInside(x, y))
  {
  }
  else
  {
    if (_expanded && IsInsideExt(x, y))
    {
      if (_expandedUp)
      {
        if (_mouseSel >= 0 && _mouseSel < GetSize())
        {
          SetCurSel(_mouseSel);
          PlaySound(_selectSound);
        }
      }
      else 
      {
        float index = (y - (_y + SCALED(_h))) / (SCALED(_size));
        if (index >= 0 && index < _showStrings)
        {
          SetCurSel(toIntFloor(_topString + index));
          PlaySound(_selectSound);
        }
      }
    }
    if (!Control::IsInside(x, y))
    {
      if (_expanded)
      {
        _expanded = false;
        PlaySound(_collapseSound);
      }
    }
  }
}

void CCombo::OnMouseMove(float x, float y, bool active)
{
  OnMouseHold(x, y, active);
}

void CCombo::OnMouseHold(float x, float y, bool active)
{
  if (_expanded)
  {
    if (_scrollbar.IsEnabled() && _scrollbar.IsLocked())
    {
      _scrollUp = _scrollDown = false;
      _scrollbar.SetPos(_topString);
      _scrollbar.OnMouseHold(x, y);
      _topString = _scrollbar.GetPos();
    }
    else if (_scrollbar.IsEnabled() && _scrollbar.IsInside(x, y))
    {
      _scrollUp = _scrollDown = false;
    }
    else
    {
      float dy;
      if 
      (
        GInput.mouseL && _topString > 0 && 
        (dy = _y + SCALED(_h) - y) > 0
      )
      {
        _scrollDown = false;
        // scroll up
        if (_scrollUp)
        {
          float scroll = SCROLL_SPEED * dy;
          saturate(scroll, SCROLL_MIN, SCROLL_MAX);
          _topString -= scroll * (Glob.uiTime - _scrollTime);
          saturateMax(_topString, 0);
          _scrollTime = Glob.uiTime;
        }
        else
        {
          _scrollUp = true;
          _scrollTime = Glob.uiTime;
        }
      }
      else if
      (
        GInput.mouseL && _topString + _showStrings < GetSize() &&
        (dy = y - (_y + SCALED(_wholeHeight))) > 0
      )
      {
        _scrollUp = false;
        // scroll down
        if (_scrollDown)
        {
          float scroll = SCROLL_SPEED * dy;
          saturate(scroll, SCROLL_MIN, SCROLL_MAX);
          _topString += scroll * (Glob.uiTime - _scrollTime);
          saturateMin(_topString, GetSize() - _showStrings);
          _scrollTime = Glob.uiTime;
        }
        else
        {
          _scrollDown = true;
          _scrollTime = Glob.uiTime;
        }
      }
      else
      {
        _scrollUp = _scrollDown = false;
        if (IsInsideExt(x, y))
        {         
          if (_expandedUp)
          {
            float nStrings = _scrollbar.IsEnabled() ? _showStrings : GetSize();
            float index = GetSize() - nStrings + ((_y - y) / SCALED(_size)) - _topString;
            if (index >= 0 && index < GetSize())
              _mouseSel = toIntFloor(GetSize() - index);              
          }
          else 
          {
            float index = (y - (_y + SCALED(_h))) / (SCALED(_size));
            if (index >= 0 && index < _showStrings)
              _mouseSel = toIntFloor(_topString + index);
          }
          if (GetSize() == 0) _mouseSel = -1;
          else saturate(_mouseSel, 0, GetSize() - 1);
        }
      }
    }
  }
  else
    _scrollUp = _scrollDown = false;
}

void CCombo::OnMouseZChanged(float dz)
{
  if (dz == 0 || !_expanded) return;
  
  if (_topString > 0 && dz > 0)
  {
    // scroll up
    float scroll = -SCROLL_SPEED * dz;
    saturate(scroll, -SCROLL_MAX, -SCROLL_MIN);
    _topString += 0.1 * scroll;
    saturateMax(_topString, 0);
  }
  else if (_topString + _showStrings < GetSize() && dz < 0)
  {
    // scroll down
    float scroll = -SCROLL_SPEED * dz;
    saturate(scroll, SCROLL_MIN, SCROLL_MAX);
    _topString += 0.1 * scroll;
    saturateMin(_topString, GetSize() - _showStrings);
  }
}

/*!
\patch 5139 Date 3/14/2007 by Jirka
- Fixed: UI - selector active row highlighting was clipped to controls group
*/

void CCombo::OnDraw(UIViewport *vp, float alpha)
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  Rect2DFloat clipRect(_x, _y, SCALED(_w), SCALED(_h));

  float xx = CX(_x);
  float yy = CY(_y);
  float ww = CW(SCALED(_w));
  float hh = CH(SCALED(_h));

  PackedColor color = ModAlpha(_color, alpha);
  PackedColor bgColor = ModAlpha(_bgColor, alpha);
  PackedColor ftColor = ModAlpha(_ftColor, alpha);
  PackedColor selColor = ModAlpha(_selColor, alpha);
  PackedColor selBgColor = ModAlpha(_selBgColor, alpha);

  // draw bar
  DrawLeft(0, color);
  DrawTop(0, color);
  DrawBottom(0, color);
  DrawRight(0, color);

  Draw2DParsExt pars;
  pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  pars.Init();
  pars.spec|= (_shadow == 2)?UISHADOW : 0; 

  TextDrawAttr attr(SCALED(_size), _font, PackedBlack, _shadow);
  if (IsFocused() && !_expanded)
  {
    pars.SetColor(selBgColor);
    vp->Draw2D(pars, Rect2DPixel(xx, yy, ww, hh));
    attr._color = selColor;
  }
  else
  {
    pars.SetColor(bgColor);
    vp->Draw2D(pars, Rect2DPixel(xx, yy, ww, hh));
    attr._color = ftColor;
  }
  Texture *texture = GetTexture(GetCurSel());
  float textureWidth = 0.0f;
  if (texture)
  {
    if (texture->AHeight() > 0)
    {
      PackedColor pictureColor = ModAlpha(PackedWhite, alpha);
      Draw2DParsExt pars;
      pars.mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
      pars.SetColor(pictureColor);
      pars.Init();
      pars.spec|= (_shadow == 2)?UISHADOW : 0; 
      textureWidth = SCALED(border / 2 + _h);
      vp->Draw2D
      (
        pars, 
        Rect2DPixel(xx + (border * w), yy, hh, hh),
        Rect2DPixel(xx, yy, ww, hh)
      );
    }
  }
  const char *text = GetText(GetCurSel());
  if (text)
  {
    float top = _y + 0.5 * (SCALED(_h - _size));
    vp->DrawText
    (
      Point2DFloat(_x + border + textureWidth, top), clipRect,
      attr, text
    );
  }

  if(IsEnabled())
  {
    // draw combo box arrow
    Draw2DParsExt parsArrow;
    parsArrow.Init();
    pars.spec|= (_shadow == 2)?UISHADOW : 0; 
    parsArrow.SetColor(ModAlpha(PackedWhite, alpha));
    if (_expanded)
      parsArrow.mip = GLOB_ENGINE->TextBank()->UseMipmap(_arrowFull, 0, 0);
    else
      parsArrow.mip = GLOB_ENGINE->TextBank()->UseMipmap(_arrowEmpty, 0, 0);
    vp->Draw2D(parsArrow, Rect2DPixel(xx+ ww - hh, yy , hh, hh),Rect2DPixel(xx, yy, ww, hh));
  }

  // draw list
  if (_expanded)
  {
    float height = SCALED(_size);    
    float curHeight = floatMin(SCALED(_wholeHeight - _h), GetSize() * height);
    float top = _expandedUp ? _y - curHeight : _y + SCALED(_h);

    yy = CY(top);
    hh = CH(curHeight);

    float w2 = 0;
    for (int i=0; i<GetSize(); i++)
    {
      const char *text = GetText(i);
      if (text)
        saturateMax(w2, vp->GetTextWidth(attr, text));
    }
    w2 += 2 * border;
    float w2SB = w2;
    if (GetSize() > _showStrings)
    {
      w2 += SCALED(sbWidth);
      saturateMax(w2, SCALED(_w));
      w2SB = w2 - SCALED(sbWidth);
      _scrollbar.Enable(true);
      _scrollbar.SetPosition
      (
        (_x + w2SB) * _scale, top * _scale,
        SCALED(sbWidth) * _scale, curHeight * _scale
      );
      _scrollbar.SetRange(0, GetSize(), _showStrings);
      _scrollbar.SetPos(_topString);
    }
    else
    {
      saturateMax(w2, SCALED(_w));
      w2SB = w2;
      _scrollbar.Enable(false);
    }
    ww = CW(w2);
    float wwSB = CW(w2SB);

    pars.SetColor(bgColor);
    vp->Draw2DNoClip(pars, Rect2DPixel(xx, yy, ww, hh));
    
    float topClip = top;
    float bottomClip = _expandedUp ? _y : _y + SCALED(_h) + curHeight;

    const float eps = 1e-6;
    int i = toIntFloor(_topString + eps);
    saturateMax(i, 0);

    top = topClip - (_topString - i) * height;
    while (top < bottomClip && i < GetSize())
    {
      float realTop = floatMax(top, topClip);
      if (i == _mouseSel)
      {
        float realHeight = floatMin(top + height, bottomClip) - realTop;
        saturateMin(realHeight, height);
        pars.SetColor(selBgColor);
        vp->Draw2DNoClip(pars, Rect2DPixel(xx, realTop * h, wwSB, realHeight * h));
        attr._color = selColor;
      }
      else
      {
        attr._color = ftColor;
      }
      Texture *texture = GetTexture(i);
      float textureWidth = 0.0f;
      if (texture)
      {
        if (texture->AHeight() > 0)
        {
          PackedColor pictureColor = ModAlpha(PackedWhite, alpha);
          Draw2DParsExt pars;
          pars.mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
          pars.SetColor(pictureColor);
          pars.Init();
          pars.spec|= (_shadow == 2)?UISHADOW : 0; 
          textureWidth = border / 2 + height;
          vp->Draw2DNoClip(pars, Rect2DPixel(xx + (border * w), realTop * h, height * h, height * h));
        }
      }
      const char *text = GetText(i);
      if (text)
      {
        Rect2DFloat clipRect(_x, topClip, w2, bottomClip - topClip);
        vp->DrawTextNoClip(Point2DFloat(_x + border + textureWidth, top), clipRect, attr, text);
      }
      top += height;
      i++;
    }

    DrawLeftNoClip(0, color);
    DrawTopNoClip(0, color);
    DrawBottomNoClip(0, color);
    DrawRightNoClip(0, color);

    if (GetSize() > _showStrings)
    { 
      _scrollbar.OnDraw(vp, alpha, true);
    }
  }
  else
    _scrollbar.Enable(false);
}

void CCombo::OnSelChanged(int curSel)
{
}

///////////////////////////////////////////////////////////////////////////////
// class IControlsGroup

bool IControlsGroup::IsEnabled() const
{
  int n = ControlsCount();
  for (int i=0; i<n; i++)
  {
    IControl *ctrl = GetControl(i);
    if (ctrl->IsVisible() && ctrl->IsEnabled()) return true;
  }
  return false;
}

bool IControlsGroup::FocusCtrl(int idc)
{
  for (int i=0; i<ControlsCount(); i++)
  {
    IControl *c = GetControl(i);
    if (!c) continue;
    if (c->IDC() == idc || c->FocusCtrl(idc))
    {
      if (c->IsVisible() && c->IsEnabled())
      {
        SetFocus(i);
        return true;
      }
      return false;
    }
  }
  return false;
}

bool IControlsGroup::FocusCtrl(IControl *ctrl)
{
  for (int i=0; i<ControlsCount(); i++)
  {
    IControl *c = GetControl(i);
    if (!c) continue;
    if (c == ctrl || c->FocusCtrl(ctrl))
    {
      if (c->IsVisible() && c->IsEnabled())
      {
        SetFocus(i);
        return true;
      }
      return false;
    }
  }
  return false;
}

void IControlsGroup::SetFocus(int i, bool def)
{
  if (i == _indexFocused)
  {
    if (i >= 0 && !GetControl(i)->IsFocused())
    {
      GetControl(i)->OnEvent(CESetFocus);
      GetControl(i)->OnSetFocus(CFAClick, def);
      if (GetControlParent()) GetControlParent()->OnCtrlFocused(GetControl(i));
    }
    return;
  }

  if (_indexFocused >= 0 && _indexFocused < ControlsCount())
  {
    if (!GetControl(_indexFocused)->OnKillFocus())
      return;
  }

  if (i >= 0)
  {
    GetControl(i)->OnEvent(CESetFocus);
    GetControl(i)->OnSetFocus(CFAClick, def);
    if (GetControlParent()) GetControlParent()->OnCtrlFocused(GetControl(i));
  }
  _indexFocused = i;
}

bool IControlsGroup::NextCtrl()
{
  int n = ControlsCount();
  if (n == 0) return false;
  for (int i=_indexFocused+1; i<n; i++)
  {
    IControl *ctrl = GetControl(i);
    if (ctrl->IsVisible() && ctrl->IsEnabled())
    {
      SetFocus(i);
      return true;
    }
  }
  return false;
}

bool IControlsGroup::PrevCtrl()
{
  int n = ControlsCount();
  if (n == 0) return false;
  for (int i=_indexFocused-1; i>=0; i--)
  {
    IControl *ctrl = GetControl(i);
    if (ctrl->IsVisible() && ctrl->IsEnabled())
    {
      SetFocus(i);
      return true;
    }
  }
  return false;
}

int IControlsGroup::FindControl(float x, float y)
{
  // check focused item first
  if (_indexFocused >= 0 && _indexFocused < ControlsCount())
  {
    IControl *ctrl = GetControl(_indexFocused);
    Assert(ctrl);
    if (ctrl->IsVisible() && ctrl->IsMouseEnabled() && ctrl->IsInside(x, y)) return _indexFocused;
  }

  // other items need to be checked in reversed order (items drawn last will receive events first)
  for (int i=ControlsCount()-1; i>=0; i--)
  {
    if (i == _indexFocused) continue;
    IControl *ctrl = GetControl(i);
    Assert(ctrl);
    if (ctrl->IsVisible() && ctrl->IsMouseEnabled() && ctrl->IsInside(x, y))
      return i;
  }
  return -1;
}

///////////////////////////////////////////////////////////////////////////////
// class CControlsGroup

CControlsGroup::CControlsGroup(ControlsContainer *parent, int idc, ParamEntryPar cls)
: Control(parent, CT_CONTROLS_GROUP, idc, cls),
_vScrollbar((_style & LB_TEXTURES) != 0, false, cls >> "ScrollBar"),
_hScrollbar((_style & LB_TEXTURES) != 0, true, cls >> "ScrollBar")
{
  _indexFocused = -1;
  _indexDefault = -1;
  _indexL = -1;
  _indexR = -1;
  _indexMove = -1;
  ParamEntryPtr list = cls.FindEntry("Controls");
  if (list && list->IsClass())
  {
    for (int i=0; i<list->GetEntryCount(); i++)
    {
      ParamEntryVal ctrlCls = list->GetEntry(i);
      if (!ctrlCls.IsClass()) continue;
      LoadControl(ctrlCls);
    }
    if (_indexDefault >= 0)
    {
      IControl *focused = GetFocused();
      if (focused && focused->CanBeDefault()) SetFocus(_indexDefault, true); 
    }
  }

  ParamEntryVal entry = cls >> "VScrollbar";
  _vScrollbar.SetColor(GetPackedColor(entry >> "color"));
  _vScrollbar.SetPosition(0, 0, entry >> "width", 0);
  _vScrollbar.SetAutoScroll( entry >> "autoScrollSpeed", entry >> "autoScrollDelay", entry >> "autoScrollRewind");

  entry = cls >> "HScrollbar";
  _hScrollbar.SetColor(GetPackedColor(entry >> "color"));
  _hScrollbar.SetPosition(0, 0, 0, entry >> "height");
  _hScrollbar.SetAutoScroll(-1,0);
  _hScrollbar.EnableAutoScroll(false);

  _vScrollbar.SetRangeAndPos(0, 0, 0, 0);
  _hScrollbar.SetRangeAndPos(0, 0, 0, 0);
  UpdateScrollbars();
}

int CControlsGroup::AddControl(Control *ctrl)
{
  return _controls.Add(ctrl);
}

bool CControlsGroup::RemoveControl(Control *ctrl)
{
  for (int i=0; i<_controls.Size(); i++)
    if (_controls[i] == ctrl)
    {
      _controls.Delete(i);
      // update the indices
      if (_indexFocused == i)
      {
        // control was focused - try to focus other
        _indexFocused--;
        NextCtrl();
      }
      else if (_indexFocused > i) _indexFocused--;
      if (_indexDefault == i) _indexDefault = -1; // pointed control invalid
      else if (_indexDefault > i) _indexDefault--; // update the pointed control
      if (_indexL == i) _indexL = -1;
      else if (_indexL > i) _indexL--;
      if (_indexR == i) _indexR = -1;
      else if (_indexR > i) _indexR--;
      if (_indexMove == i) _indexMove = -1;
      else if (_indexMove > i) _indexMove--;
      return true;
    }
  return false;
}

void CControlsGroup::RemoveAllControls()
{
  _controls.Clear();
  SetFocus(-1);
}

IControl *CControlsGroup::GetCtrl(int idc)
{
  if (idc == _idc) return this;
  for (int i=0; i<_controls.Size(); i++)
  {
    IControl *control = _controls[i];
    if (control)
    {
      IControl *ctrl = control->GetCtrl(idc);
      if (ctrl) return ctrl;
    }
  }
  return NULL;
}

IControl *CControlsGroup::GetCtrl(float x, float y)
{
  x -= _x; y -= _y;
  if (_vScrollbar.IsEnabled()) y += _vScrollbar.GetPos();
  if (_hScrollbar.IsEnabled()) x += _hScrollbar.GetPos();
  int index = FindControl(x, y);
  if (index >= 0) return _controls[index];
  else return this;
}

IControl *CControlsGroup::GetFocused()
{
  if (_indexFocused < 0) return this;
  if (_indexFocused >= _controls.Size())
  {
    _indexFocused = -1;
    return this;
  }
  return _controls[_indexFocused]->GetFocused();
}

const IControl *CControlsGroup::GetFocused() const
{
  if (_indexFocused < 0) return this;
  if (_indexFocused >= _controls.Size())
  {
    // _indexFocused = -1;
    return this;
  }
  return _controls[_indexFocused]->GetFocused();
}

bool CControlsGroup::CanBeDefault() const
{
  int n = _controls.Size();
  for (int i=0; i<n; i++)
  {
    IControl *ctrl = _controls[i];
    if (ctrl->IsVisible() && ctrl->IsEnabled() && ctrl->CanBeDefault())
      return true;
  }
  return false;
}

const IControl *CControlsGroup::GetDefault() const
{
  if (_indexDefault < 0) return NULL;
  if (_indexDefault >= _controls.Size())
  {
    return NULL;
  }
  return _controls[_indexDefault]->GetDefault();
}

IControl *CControlsGroup::GetDefault()
{
  if (_indexDefault < 0) return NULL;
  if (_indexDefault >= _controls.Size())
  {
    _indexDefault = -1;
    return NULL;
  }
  return _controls[_indexDefault]->GetDefault();
}

IControl *CControlsGroup::GetShortcutControl(int dikCode)
{
  // myself
  IControl *ctrl = base::GetShortcutControl(dikCode);
  if (ctrl) return ctrl;

  // controls under me
  for (int i=0; i<_controls.Size(); i++)
  {
    IControl *ctrl = _controls[i]->GetShortcutControl(dikCode);
    if (ctrl) return ctrl;
  }
  return NULL;
}

bool CControlsGroup::OnSetFocus(ControlFocusAction action, bool def)
{
  if (!base::OnSetFocus(action)) return false;
  int n = _controls.Size();
  if (n <= 0) return false;
  _vScrollbar.EnableAutoScroll(false);
  switch (action)
  {
  case CFAPrev:
    for (int i=n-1; i>=0; i--)
    {
      IControl *ctrl = _controls[i];
      if (ctrl->IsVisible() && ctrl->IsEnabled() && (!def || ctrl->CanBeDefault()))
      {
        SetFocus(i, def);
        return true;
      }
    }
    SetFocus(-1, def);
    return false;
  case CFAClick:
    if (_indexFocused >= 0) return true;
    // continue
  case CFANext:
    for (int i=0; i<n; i++)
    {
      IControl *ctrl = _controls[i];
      if (ctrl->IsVisible() && ctrl->IsEnabled() && (!def || ctrl->CanBeDefault()))
      {
        SetFocus(i, def);
        return true;
      }
    }
    SetFocus(-1, def);
    return false;
  default:
    Fail("Focus Action");
    return false;
  }
}

bool CControlsGroup::OnKillFocus()
{
  _vScrollbar.EnableAutoScroll(true);
  return Control::OnKillFocus();
}

bool CControlsGroup::IsInside(float x, float y) const
{
  if (base::IsInside(x, y)) return true;
  if (_indexFocused < 0) return false;
  IControl *ctrl = _controls[_indexFocused];
  if (!ctrl) return false;
  x -= _x; y -= _y;
  if (_vScrollbar.IsEnabled()) y += _vScrollbar.GetPos();
  if (_hScrollbar.IsEnabled()) x += _hScrollbar.GetPos();
  return ctrl->IsInsideExt(x, y);
}

void CControlsGroup::OnLButtonDown(float x, float y)
{
  _vScrollbar.RestartAutoScroll();
  if (_vScrollbar.IsEnabled() && x >= _x + _w - _vScrollbar.W())
  {
     if (_vScrollbar.IsInside(x, y))
       _vScrollbar.OnLButtonDown(x, y);
     return;
  }
  if (_hScrollbar.IsEnabled() && y >= _y + _h - _hScrollbar.H())
  {
    if (_hScrollbar.IsInside(x, y))
      _hScrollbar.OnLButtonDown(x, y);
    return;
  }

  // search subcontrol
  x -= _x; y -= _y;
  if (_vScrollbar.IsEnabled()) y += _vScrollbar.GetPos();
  if (_hScrollbar.IsEnabled()) x += _hScrollbar.GetPos();
  int iFound = FindControl(x, y);
  _indexL = iFound;
  if (iFound >= 0)
  {
    SetFocus(iFound);
    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    _controls[iFound]->OnEvent(CEMouseButtonDown, 0, x, y, shift, ctrl, alt);
    _controls[iFound]->OnLButtonDown(x, y);
  }
}

void CControlsGroup::OnLButtonUp(float x, float y)
{
  if (_vScrollbar.IsLocked())
  {
    _vScrollbar.OnLButtonUp(x, y);
    return;
  }
  if (_hScrollbar.IsLocked())
  {
    _hScrollbar.OnLButtonUp(x, y);
    return;
  }

  Rect2DFloat rect(_x, _y, _w, _h);

  if (_vScrollbar.IsEnabled() && x >= _x + _w - _vScrollbar.W())
    return;
  if (_hScrollbar.IsEnabled() && y >= _y + _h - _hScrollbar.H())
    return;

  // search subcontrol
  x -= _x; y -= _y;
  if (_vScrollbar.IsEnabled()) y += _vScrollbar.GetPos();
  if (_hScrollbar.IsEnabled()) x += _hScrollbar.GetPos();
  if (_indexL >= 0)
  {
    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    _controls[_indexL]->OnEvent(CEMouseButtonUp, 0, x, y, shift, ctrl, alt);
    _controls[_indexL]->OnLButtonUp(x, y);
  }
}

void CControlsGroup::OnMouseMove(float x, float y, bool active)
{
  if (_vScrollbar.IsEnabled())
  {
    if (_vScrollbar.IsLocked())
    {
      _vScrollbar.OnMouseHold(x, y);
      return;
    }
    if (x >= _x + _w - _vScrollbar.W())
      return;
  }
  if (_hScrollbar.IsEnabled())
  {
    if (_hScrollbar.IsLocked())
    {
      _hScrollbar.OnMouseHold(x, y);
      return;
    }
    if (y >= _y + _h - _hScrollbar.H())
      return;
  }

  // search subcontrol
  x -= _x; y -= _y;
  if (_vScrollbar.IsEnabled()) y += _vScrollbar.GetPos();
  if (_hScrollbar.IsEnabled()) x += _hScrollbar.GetPos();
  if (active)
  {
    int index;
    if (GInput.mouseL)
      index = _indexL;
    else if (GInput.mouseR)
      index = _indexR;
    else
      index = FindControl(x, y);
    if (_indexMove >= 0 && _indexMove != index)
    {
      _controls[_indexMove]->OnEvent(CEMouseMoving, x, y, false);
      _controls[_indexMove]->OnMouseMove(x, y, false);
    }
    if (index >= 0)
    {
      _controls[index]->OnEvent(CEMouseMoving, x, y, true);
      _controls[index]->OnMouseMove(x, y);
    }
    _indexMove = index;
  }
  else
  {
    if (_indexMove >= 0)
    {
      _controls[_indexMove]->OnEvent(CEMouseMoving, x, y, false);
      _controls[_indexMove]->OnMouseMove(x, y, false);
    }
    _indexMove = -1;
  }
}

void CControlsGroup::OnMouseHold(float x, float y, bool active)
{
  if (_vScrollbar.IsEnabled())
  {
    if (_vScrollbar.IsLocked())
    {
      _vScrollbar.OnMouseHold(x, y);
      return;
    }
    if (x >= _x + _w - _vScrollbar.W())
      return;
  }
  if (_hScrollbar.IsEnabled())
  {
    if (_hScrollbar.IsLocked())
    {
      _hScrollbar.OnMouseHold(x, y);
      return;
    }
    if (y >= _y + _h - _hScrollbar.H())
      return;
  }

  // search subcontrol
  x -= _x; y -= _y;
  if (_vScrollbar.IsEnabled()) y += _vScrollbar.GetPos();
  if (_hScrollbar.IsEnabled()) x += _hScrollbar.GetPos();
  if (active)
  {
    int index;
    if (GInput.mouseL)
      index = _indexL;
    else if (GInput.mouseR)
      index = _indexR;
    else
      index = FindControl(x, y);
    if (_indexMove >= 0 && _indexMove != index)
    {
      _controls[_indexMove]->OnEvent(CEMouseMoving, x, y, false);
      _controls[_indexMove]->OnMouseMove(x, y, false);
    }
    if (index >= 0)
    {
      _controls[index]->OnEvent(CEMouseHolding, x, y, true);
      _controls[index]->OnMouseHold(x, y);
    }
    _indexMove = index;
  }
  else
  {
    if (_indexMove >= 0)
    {
      _controls[_indexMove]->OnEvent(CEMouseHolding, x, y, false);
      _controls[_indexMove]->OnMouseHold(x, y, false);
    }
    _indexMove = -1;
  }
}

void CControlsGroup::OnLButtonClick(float x, float y)
{
  x -= _x; y -= _y;
  if (_vScrollbar.IsEnabled()) y += _vScrollbar.GetPos();
  if (_hScrollbar.IsEnabled()) x += _hScrollbar.GetPos();
  if (_indexL >= 0)
  {
    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    _controls[_indexL]->OnEvent(CEMouseButtonClick, 0, x, y, shift, ctrl, alt);
    _controls[_indexL]->OnLButtonClick(x, y);
  }
}

void CControlsGroup::OnLButtonDblClick(float x, float y)
{
  x -= _x; y -= _y;
  if (_vScrollbar.IsEnabled()) y += _vScrollbar.GetPos();
  if (_hScrollbar.IsEnabled()) x += _hScrollbar.GetPos();
  int iFound = FindControl(x, y);
  if (iFound == _indexL)
  {
    if (iFound >= 0)
    {
      bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
      bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
      bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
      _controls[iFound]->OnEvent(CEMouseButtonDblClick, 0, x, y, shift, ctrl, alt);
      _controls[iFound]->OnLButtonDblClick(x, y);
    }
  }
  else
  {
    _indexL = iFound;
    if (iFound >= 0)
    {
      bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
      bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
      bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
      _controls[iFound]->OnEvent(CEMouseButtonDown, 0, x, y, shift, ctrl, alt);
      _controls[iFound]->OnLButtonDown(x, y);
    }
  }
}

void CControlsGroup::OnRButtonDown(float x, float y)
{
  x -= _x; y -= _y;
  if (_vScrollbar.IsEnabled()) y += _vScrollbar.GetPos();
  if (_hScrollbar.IsEnabled()) x += _hScrollbar.GetPos();
  int iFound = FindControl(x, y);
  _indexR = iFound;
  if (iFound >= 0)
  {
    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    _controls[iFound]->OnEvent(CEMouseButtonDown, 1, x, y, shift, ctrl, alt);
    _controls[iFound]->OnRButtonDown(x, y);
  }
}

void CControlsGroup::OnRButtonUp(float x, float y)
{
  x -= _x; y -= _y;
  if (_vScrollbar.IsEnabled()) y += _vScrollbar.GetPos();
  if (_hScrollbar.IsEnabled()) x += _hScrollbar.GetPos();
  if (_indexR >= 0)
  {
    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    _controls[_indexR]->OnEvent(CEMouseButtonUp, 1, x, y, shift, ctrl, alt);
    _controls[_indexR]->OnRButtonUp(x, y);
  }
  _indexR = -1;
}

void CControlsGroup::OnRButtonClick(float x, float y)
{
  x -= _x; y -= _y;
  if (_vScrollbar.IsEnabled()) y += _vScrollbar.GetPos();
  if (_hScrollbar.IsEnabled()) x += _hScrollbar.GetPos();
  if (_indexR >= 0)
  {
    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    _controls[_indexR]->OnEvent(CEMouseButtonClick, 1, x, y, shift, ctrl, alt);
    _controls[_indexR]->OnRButtonClick(x, y);
  }
}

void CControlsGroup::OnMouseZChanged(float dz)
{
  if (_indexMove >= 0)
  {
    _controls[_indexMove]->OnEvent(CEMouseZChanged, dz);
    _controls[_indexMove]->OnMouseZChanged(dz);
  }
}

bool CControlsGroup::OnKeyDown(int dikCode)
{
  int n = _controls.Size();
  if (n == 0) return false;

  bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
  bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
  bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];

  IControl *focused = NULL;
  if (_indexFocused >= 0 && _indexFocused < n) focused = _controls[_indexFocused];
  if (focused) focused->OnEvent(CEKeyDown, dikCode, shift, ctrl, alt);
  if (focused && focused->OnKeyDown(dikCode))
  {
    return true;
  }
  else switch (dikCode)
  {
    case DIK_TAB:
      if (alt) return false;
      if (shift)
        return PrevCtrl();
      else
        return NextCtrl();
    case DIK_LEFT:
    case DIK_UP:
    case DIK_PRIOR:
    case INPUT_DEVICE_XINPUT + XBOX_Up:
    case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYUp:
    case INPUT_DEVICE_XINPUT + XBOX_Left:
    case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXLeft:
      return PrevCtrl();
    case DIK_RIGHT:
    case DIK_DOWN:
    case DIK_NEXT:
    case INPUT_DEVICE_XINPUT + XBOX_Down:
    case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYDown:
    case INPUT_DEVICE_XINPUT + XBOX_Right:
    case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXRight:
    case INPUT_DEVICE_XINPUT + XBOX_RightTrigger:
      return NextCtrl();
    default:
      return false;
  }
}

bool CControlsGroup::OnKeyUp(int dikCode)
{
  int n = _controls.Size();
  if (n == 0) return false;

  bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
  bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
  bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];

  IControl *focused = NULL;
  if (_indexFocused >= 0 && _indexFocused < n) focused = _controls[_indexFocused];
  if (focused) focused->OnEvent(CEKeyUp, dikCode, shift, ctrl, alt);
  if (focused && focused->OnKeyUp(dikCode))
  {
    return true;
  }
  return false;
}

bool CControlsGroup::OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  int n = _controls.Size();
  if (n == 0) return false;

  IControl *focused = NULL;
  if (_indexFocused >= 0 && _indexFocused < n) focused = _controls[_indexFocused];
  if (focused) focused->OnEvent(CEChar, nChar);
  if (focused && focused->OnChar(nChar, nRepCnt, nFlags))
  {
    return true;
  }
  return false;
}

bool CControlsGroup::OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  int n = _controls.Size();
  if (n == 0) return false;

  IControl *focused = NULL;
  if (_indexFocused >= 0 && _indexFocused < n) focused = _controls[_indexFocused];
  if (focused) focused->OnEvent(CEIMEChar, nChar);
  if (focused && focused->OnIMEChar(nChar, nRepCnt, nFlags))
  {
    return true;
  }
  return false;
}

bool CControlsGroup::OnIMEComposition(unsigned nChar, unsigned nFlags)
{
  int n = _controls.Size();
  if (n == 0) return false;

  IControl *focused = NULL;
  if (_indexFocused >= 0 && _indexFocused < n) focused = _controls[_indexFocused];
  if (focused) focused->OnEvent(CEIMEComposition, nChar);
  if (focused && focused->OnIMEComposition(nChar, nFlags))
  {
    return true;
  }
  return false;
}

void CControlsGroup::DrawTooltip(UIViewport *vp, float x, float y)
{
  float xDraw = x, yDraw = y;
  x -= _x; y -= _y;
  if (_vScrollbar.IsEnabled()) y += _vScrollbar.GetPos();
  if (_hScrollbar.IsEnabled()) x += _hScrollbar.GetPos();

  int iFound = FindControl(x, y);
  if (iFound >= 0)
    _controls[iFound]->DrawTooltip(vp, xDraw, yDraw);
}

int CControlsGroup::GetTooltipArea(float x, float y)
{
  float xDraw = x, yDraw = y;
  x -= _x; y -= _y;
  if (_vScrollbar.IsEnabled()) y += _vScrollbar.GetPos();
  if (_hScrollbar.IsEnabled()) x += _hScrollbar.GetPos();

  int iFound = FindControl(x, y);
  if (iFound >= 0) return _controls[iFound]->GetTooltipArea(xDraw, yDraw);
  return 0; // not found
}


/*!
\patch 5104 Date 12/18/2006 by Jirka
- Fixed: UI fade animations (ctrlSetFade) did not work for controls in control groups 
\patch 5104 Date 12/18/2006 by Jirka
- Fixed: UI - combo in control group did not handle mouse events well
*/

void CControlsGroup::SetScaleControl(float scaleControl) 
{
  _scaleControl = scaleControl;
  for (int i=0; i<_controls.Size(); i++)
  {
    Control *ctrl = _controls[i];
    ctrl->SetScaleControl(scaleControl);
  }
}

void CControlsGroup::OnDraw(UIViewport *vp, float alpha)
{
  // animate controls
  for (int i=0; i<_controls.Size(); i++)
  {
    Control *ctrl = _controls[i];
    if (ctrl)
      ctrl->Animate();
  }

  UpdateScrollbars();

  Point2DFloat offset(_x, _y);
  Rect2DFloat clip(_x, _y, _w, _h);

  // draw scrollbars
  if (_vScrollbar.IsEnabled())
  {
    _vScrollbar.OnDraw(vp, alpha);
    clip.w -= _vScrollbar.W();
    offset.y -= _vScrollbar.GetPos();
  }
  if (_hScrollbar.IsEnabled())
  {
    _hScrollbar.OnDraw(vp, alpha);
    clip.h -= _hScrollbar.H();
    offset.x -= _hScrollbar.GetPos();
  }
  
  
  UIViewport *vpChild = vp->CreateChild(offset, clip);

  // draw controls
  for (int i=0; i<_controls.Size(); i++)
  {
    if (i == _indexFocused) continue;
    Control *ctrl = _controls[i];
    if (ctrl && ctrl->IsVisible())
      ctrl->OnDraw(vpChild, _vScrollbar.GetAutoScrollAlpha() * alpha * ctrl->GetAlpha());
  }
  // draw focused control at last
  if (_indexFocused >= 0 && _indexFocused < _controls.Size())
  {
    Control *ctrl = _controls[_indexFocused];
    if (ctrl->IsVisible())
      ctrl->OnDraw(vpChild, _vScrollbar.GetAutoScrollAlpha() * alpha * ctrl->GetAlpha());
  }
  
  vp->DestroyChild(vpChild);
}


void CControlsGroup::SetViewPos(bool isHorizontal, float pos)
{
  if (isHorizontal && _hScrollbar.IsEnabled())
    _hScrollbar.SetPos(pos);
  
  if (!isHorizontal && _vScrollbar.IsEnabled())
    _vScrollbar.SetPos(pos);
}

void CControlsGroup::UpdateScrollbars()
{
  // calculate controls area size
  float xMin = FLT_MAX, xMax = -FLT_MAX;
  float yMin = FLT_MAX, yMax = -FLT_MAX;
  for (int i=0; i<_controls.Size(); i++)
  {
    Control *ctrl = _controls[i];
    if (!ctrl) continue;
    saturateMin(xMin, ctrl->X());
    saturateMax(xMax, ctrl->X() + ctrl->W());
    saturateMin(yMin, ctrl->Y());
    saturateMax(yMax, ctrl->Y() + ctrl->H());
  }
  float wArea = xMax + xMin; // border on left is used on right as well
  float hArea = yMax + yMin; // border on top is used on bottom as well

  float width = _vScrollbar.W();
  float height = _hScrollbar.H();

  bool showVScrollbar = hArea > _h;
  bool showHScrollbar = wArea > _w;

  if (showVScrollbar)
  {
    float x = _x + _w - width;
    float h = _h;
    if (showHScrollbar) h -= height;
    _vScrollbar.SetPosition(x, _y, width, h);
    _vScrollbar.SetRange(0, hArea, h);
    _vScrollbar.SetSpeed(0.1f * h, 0.8f * h); 
    _vScrollbar.Enable(true);
  }
  else
  {
    _vScrollbar.Enable(false);
  }

  if (showHScrollbar)
  {
    float y = _y + _h - height;
    float w = _w;
    if (showVScrollbar) w -= width;
    _hScrollbar.SetPosition(_x, y, w, height);
    _hScrollbar.SetRange(0, wArea, w);  
    _hScrollbar.SetSpeed(0.1f * w, 0.8f * w); 
    _hScrollbar.Enable(true);
  }
  else
  {
    _hScrollbar.Enable(false);
  }
}

void CControlsGroup::LoadControl(ParamEntryPar ctrlCls)
{
  int type = ctrlCls >> "type";
  int idc = ctrlCls >> "idc";
  Control *ctrl = _parent->OnCreateCtrl(type, idc, ctrlCls);
  if (ctrl)
  {
    int index = _controls.Add(ctrl);
    if (ctrl->GetDefault()) _indexDefault = index;
  }
}

///////////////////////////////////////////////////////////////////////////////
// class CControlsGroup

CObjectsGroup::CObjectsGroup(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : ControlObject (parent, idc, cls, NULL)
{
  //! type of control
  _type = CT_OBJECTS_GROUP;
  //control position
  _x = cls>>"x";
  _y = cls>>"y";
  _w = cls>>"w";
  _h = cls>>"h";

  _indexFocused = -1;
  _indexDefault = -1;
  _indexL = -1;
  _indexR = -1;
  _indexMove = -1;
  ParamEntryPtr list = cls.FindEntry("Objects");
  if (list && list->IsClass())
  {
    for (int i=0; i<list->GetEntryCount(); i++)
    {
      ParamEntryVal ctrlCls = list->GetEntry(i);
      if (!ctrlCls.IsClass()) continue;
      LoadObject(ctrlCls);
    }
    if (_indexDefault >= 0)
    {
      IControl *focused = GetFocused();
      if (focused && focused->CanBeDefault()) SetFocus(_indexDefault, true); 
    }
  }

  if (_shape && _shape->NLevels()>0)
  {
    _shape->Level(0)->MakeCockpit();
    //_shape->OrSpecial(BestMipmap | DisableSun);
    // WIP: 3DUI: clean here
    _shape->OrSpecial(BestMipmap);
    _shape->SetCanUsePushBuffer(false);
  }

  _position = ReadPosition(cls, "position", "x", "y", "z");
  _position = Convert2DTo3D(Point2DFloat(_x + _w * 0.5,_y + _h * 0.5), _position.Z());
  SetPosition(_position);

  Vector3 dir, up;
  dir.Init();
  up.Init();
  dir[0] = (cls >> "direction")[0];
  dir[1] = (cls >> "direction")[1];
  dir[2] = (cls >> "direction")[2];
  up[0] = (cls >> "up")[0];
  up[1] = (cls >> "up")[1];
  up[2] = (cls >> "up")[2];
  SetOrient(dir, up);

  float scale = cls >> "scale";
  AspectSettings as;
  GEngine->GetAspectSettings(as);
  // adjust scale by aspect settings
  SetScale(scale * (as.uiBottomRightY - as.uiTopLeftY) /* * as.topFOV * (1.0 / 0.75) */);

  _offset = VZero;
  _moving = false;

  _waitForLoad = true;
  ConstParamEntryPtr entry = cls.FindEntry("waitForLoad");
  if (entry) _waitForLoad = *entry;

}

int CObjectsGroup::AddObject(ControlObject *obj)
{
  return _objects.Add(obj);
}

bool CObjectsGroup::RemoveObject(ControlObject *obj)
{
  for (int i=0; i<_objects.Size(); i++)
    if (_objects[i] == obj)
    {
      _objects.Delete(i);
      // update the indices
      if (_indexFocused == i)
      {
        // control was focused - try to focus other
        _indexFocused--;
        NextCtrl();
      }
      else if (_indexFocused > i) _indexFocused--;
      if (_indexDefault == i) _indexDefault = -1; // pointed control invalid
      else if (_indexDefault > i) _indexDefault--; // update the pointed control
      if (_indexL == i) _indexL = -1;
      else if (_indexL > i) _indexL--;
      if (_indexR == i) _indexR = -1;
      else if (_indexR > i) _indexR--;
      if (_indexMove == i) _indexMove = -1;
      else if (_indexMove > i) _indexMove--;
      return true;
    }
    return false;
}

void CObjectsGroup::RemoveAllObjects()
{
  _objects.Clear();
  SetFocus(-1);
}

IControl *CObjectsGroup::GetCtrl(int idc)
{
  if (idc == _idc) return this;
  for (int i=0; i<_objects.Size(); i++)
  {
    IControl *control = _objects[i];
    if (control)
    {
      IControl *ctrl = control->GetCtrl(idc);
      if (ctrl) return ctrl;
    }
  }
  return NULL;
}

IControl *CObjectsGroup::GetCtrl(float x, float y)
{
  x -= _x; y -= _y;
  int index = FindControl(x, y);
  if (index >= 0) return _objects[index];
  else return this;
}

IControl *CObjectsGroup::GetFocused()
{
  if (_indexFocused < 0) return this;
  if (_indexFocused >= _objects.Size())
  {
    _indexFocused = -1;
    return this;
  }
  return _objects[_indexFocused]->GetFocused();
}

const IControl *CObjectsGroup::GetFocused() const
{
  if (_indexFocused < 0) return this;
  if (_indexFocused >= _objects.Size())
  {
    // _indexFocused = -1;
    return this;
  }
  return _objects[_indexFocused]->GetFocused();
}

bool CObjectsGroup::CanBeDefault() const
{
  int n = _objects.Size();
  for (int i=0; i<n; i++)
  {
    IControl *ctrl = _objects[i];
    if (ctrl->IsVisible() && ctrl->IsEnabled() && ctrl->CanBeDefault())
      return true;
  }
  return false;
}

const IControl *CObjectsGroup::GetDefault() const
{
  if (_indexDefault < 0) return NULL;
  if (_indexDefault >= _objects.Size())
  {
    return NULL;
  }
  return _objects[_indexDefault]->GetDefault();
}

IControl *CObjectsGroup::GetDefault()
{
  if (_indexDefault < 0) return NULL;
  if (_indexDefault >= _objects.Size())
  {
    _indexDefault = -1;
    return NULL;
  }
  return _objects[_indexDefault]->GetDefault();
}

IControl *CObjectsGroup::GetShortcutControl(int dikCode)
{
  // myself
  IControl *ctrl = base::GetShortcutControl(dikCode);
  if (ctrl) return ctrl;

  // controls under me
  for (int i=0; i<_objects.Size(); i++)
  {
    IControl *ctrl = _objects[i]->GetShortcutControl(dikCode);
    if (ctrl) return ctrl;
  }
  return NULL;
}

bool CObjectsGroup::OnSetFocus(ControlFocusAction action, bool def)
{
  if (!base::OnSetFocus(action)) return false;
  int n = _objects.Size();
  if (n <= 0) return false;
  switch (action)
  {
  case CFAPrev:
    for (int i=n-1; i>=0; i--)
    {
      IControl *ctrl = _objects[i];
      if (ctrl->IsVisible() && ctrl->IsEnabled() && (!def || ctrl->CanBeDefault()))
      {
        SetFocus(i, def);
        return true;
      }
    }
    SetFocus(-1, def);
    return false;
  case CFAClick:
    if (_indexFocused >= 0) return true;
    // continue
  case CFANext:
    for (int i=0; i<n; i++)
    {
      IControl *ctrl = _objects[i];
      if (ctrl->IsVisible() && ctrl->IsEnabled() && (!def || ctrl->CanBeDefault()))
      {
        SetFocus(i, def);
        return true;
      }
    }
    SetFocus(-1, def);
    return false;
  default:
    Fail("Focus Action");
    return false;
  }
}

bool CObjectsGroup::OnKillFocus()
{
  return ControlObject::OnKillFocus();
}

bool CObjectsGroup::IsInside(float x, float y) const
{
  if(
      x >= _x && x <= _x + (_w) && y >= _y && y <= _y + (_h)
  ) return true;

  if (_indexFocused < 0) return false;
  IControl *obj = _objects[_indexFocused];
  if (!obj) return false;
  x -= _x; y -= _y;
  return obj->IsInsideExt(x, y);
}

void CObjectsGroup::OnLButtonDown(float x, float y)
{
  // search subcontrol
  x -= _x; y -= _y;
  int iFound = FindControl(x, y);
  _indexL = iFound;
  if (iFound >= 0)
  {
    SetFocus(iFound);
    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    _objects[iFound]->OnEvent(CEMouseButtonDown, 0, x, y, shift, ctrl, alt);
    _objects[iFound]->OnLButtonDown(x, y);
  }
}

void CObjectsGroup::OnLButtonUp(float x, float y)
{

  Rect2DFloat rect(_x, _y, _w, _h);

  // search subcontrol
  x -= _x; y -= _y;
  if (_indexL >= 0)
  {
    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    _objects[_indexL]->OnEvent(CEMouseButtonUp, 0, x, y, shift, ctrl, alt);
    _objects[_indexL]->OnLButtonUp(x, y);
  }
}

void CObjectsGroup::OnMouseMove(float x, float y, bool active)
{
  // search subcontrol
  x -= _x; y -= _y;

  if (active)
  {
    int index;
    if (GInput.mouseL)
      index = _indexL;
    else if (GInput.mouseR)
      index = _indexR;
    else
      index = FindControl(x, y);

    if (_indexMove >= 0 && _indexMove != index)
    {
      _objects[_indexMove]->OnEvent(CEMouseMoving, x, y, false);
      _objects[_indexMove]->OnMouseMove(x, y, false);
    }
    if (index >= 0)
    {
      _objects[index]->OnEvent(CEMouseMoving, x, y, true);
      _objects[index]->OnMouseMove(x, y);
    }
    _indexMove = index;
  }
  else
  {
    if (_indexMove >= 0)
    {
      _objects[_indexMove]->OnEvent(CEMouseMoving, x, y, false);
      _objects[_indexMove]->OnMouseMove(x, y, false);
    }
    _indexMove = -1;
  }
}

void CObjectsGroup::OnMouseHold(float x, float y, bool active)
{

  // search subcontrol
  x -= _x; y -= _y;
  if (active)
  {
    int index;
    if (GInput.mouseL)
      index = _indexL;
    else if (GInput.mouseR)
      index = _indexR;
    else
      index = FindControl(x, y);
    if (_indexMove >= 0 && _indexMove != index)
    {
      _objects[_indexMove]->OnEvent(CEMouseMoving, x, y, false);
      _objects[_indexMove]->OnMouseMove(x, y, false);
    }
    if (index >= 0)
    {
      _objects[index]->OnEvent(CEMouseHolding, x, y, true);
      _objects[index]->OnMouseHold(x, y);
    }
    _indexMove = index;
  }
  else
  {
    if (_indexMove >= 0)
    {
      _objects[_indexMove]->OnEvent(CEMouseHolding, x, y, false);
      _objects[_indexMove]->OnMouseHold(x, y, false);
    }
    _indexMove = -1;
  }
}

void CObjectsGroup::OnLButtonClick(float x, float y)
{
  x -= _x; y -= _y;
  if (_indexL >= 0)
  {
    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    _objects[_indexL]->OnEvent(CEMouseButtonClick, 0, x, y, shift, ctrl, alt);
    _objects[_indexL]->OnLButtonClick(x, y);
  }
}

void CObjectsGroup::OnLButtonDblClick(float x, float y)
{
  x -= _x; y -= _y;
  int iFound = FindControl(x, y);
  if (iFound == _indexL)
  {
    if (iFound >= 0)
    {
      bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
      bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
      bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
      _objects[iFound]->OnEvent(CEMouseButtonDblClick, 0, x, y, shift, ctrl, alt);
      _objects[iFound]->OnLButtonDblClick(x, y);
    }
  }
  else
  {
    _indexL = iFound;
    if (iFound >= 0)
    {
      bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
      bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
      bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
      _objects[iFound]->OnEvent(CEMouseButtonDown, 0, x, y, shift, ctrl, alt);
      _objects[iFound]->OnLButtonDown(x, y);
    }
  }
}

void CObjectsGroup::OnRButtonDown(float x, float y)
{
  x -= _x; y -= _y;
  int iFound = FindControl(x, y);
  _indexR = iFound;
  if (iFound >= 0)
  {
    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    _objects[iFound]->OnEvent(CEMouseButtonDown, 1, x, y, shift, ctrl, alt);
    _objects[iFound]->OnRButtonDown(x, y);
  }
}

void CObjectsGroup::OnRButtonUp(float x, float y)
{
  x -= _x; y -= _y;
  if (_indexR >= 0)
  {
    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    _objects[_indexR]->OnEvent(CEMouseButtonUp, 1, x, y, shift, ctrl, alt);
    _objects[_indexR]->OnRButtonUp(x, y);
  }
  _indexR = -1;
}

void CObjectsGroup::OnRButtonClick(float x, float y)
{
  x -= _x; y -= _y;
  if (_indexR >= 0)
  {
    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
    bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];
    _objects[_indexR]->OnEvent(CEMouseButtonClick, 1, x, y, shift, ctrl, alt);
    _objects[_indexR]->OnRButtonClick(x, y);
  }
}

void CObjectsGroup::OnMouseZChanged(float dz)
{
  if (_indexMove >= 0)
  {
    _objects[_indexMove]->OnEvent(CEMouseZChanged, dz);
    _objects[_indexMove]->OnMouseZChanged(dz);
  }
}

bool CObjectsGroup::OnKeyDown(int dikCode)
{
  int n = _objects.Size();
  if (n == 0) return false;

  bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
  bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
  bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];

  IControl *focused = NULL;
  if (_indexFocused >= 0 && _indexFocused < n) focused = _objects[_indexFocused];
  if (focused) focused->OnEvent(CEKeyDown, dikCode, shift, ctrl, alt);
  if (focused && focused->OnKeyDown(dikCode))
  {
    return true;
  }
  else switch (dikCode)
  {
  case DIK_TAB:
    if (alt) return false;
    if (shift)
      return PrevCtrl();
    else
      return NextCtrl();
  case DIK_LEFT:
  case DIK_UP:
  case DIK_PRIOR:
  case INPUT_DEVICE_XINPUT + XBOX_Up:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYUp:
  case INPUT_DEVICE_XINPUT + XBOX_Left:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXLeft:
    return PrevCtrl();
  case DIK_RIGHT:
  case DIK_DOWN:
  case DIK_NEXT:
  case INPUT_DEVICE_XINPUT + XBOX_Down:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYDown:
  case INPUT_DEVICE_XINPUT + XBOX_Right:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXRight:
  case INPUT_DEVICE_XINPUT + XBOX_RightTrigger:
    return NextCtrl();
  default:
    return false;
  }
}

bool CObjectsGroup::OnKeyUp(int dikCode)
{
  int n = _objects.Size();
  if (n == 0) return false;

  bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
  bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
  bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];

  IControl *focused = NULL;
  if (_indexFocused >= 0 && _indexFocused < n) focused = _objects[_indexFocused];
  if (focused) focused->OnEvent(CEKeyUp, dikCode, shift, ctrl, alt);
  if (focused && focused->OnKeyUp(dikCode))
  {
    return true;
  }
  return false;
}

bool CObjectsGroup::OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  int n = _objects.Size();
  if (n == 0) return false;

  IControl *focused = NULL;
  if (_indexFocused >= 0 && _indexFocused < n) focused = _objects[_indexFocused];
  if (focused) focused->OnEvent(CEChar, nChar);
  if (focused && focused->OnChar(nChar, nRepCnt, nFlags))
  {
    return true;
  }
  return false;
}

bool CObjectsGroup::OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  int n = _objects.Size();
  if (n == 0) return false;

  IControl *focused = NULL;
  if (_indexFocused >= 0 && _indexFocused < n) focused = _objects[_indexFocused];
  if (focused) focused->OnEvent(CEIMEChar, nChar);
  if (focused && focused->OnIMEChar(nChar, nRepCnt, nFlags))
  {
    return true;
  }
  return false;
}

bool CObjectsGroup::OnIMEComposition(unsigned nChar, unsigned nFlags)
{
  int n = _objects.Size();
  if (n == 0) return false;

  IControl *focused = NULL;
  if (_indexFocused >= 0 && _indexFocused < n) focused = _objects[_indexFocused];
  if (focused) focused->OnEvent(CEIMEComposition, nChar);
  if (focused && focused->OnIMEComposition(nChar, nFlags))
  {
    return true;
  }
  return false;
}

void CObjectsGroup::DrawTooltip(UIViewport *vp, float x, float y)
{
  float xDraw = x, yDraw = y;
  x -= _x; y -= _y;

  int iFound = FindControl(x, y);
  if (iFound >= 0)
    _objects[iFound]->DrawTooltip(vp, xDraw, yDraw);
}

int CObjectsGroup::GetTooltipArea(float x, float y)
{
  float xDraw = x, yDraw = y;
  x -= _x; y -= _y;

  int iFound = FindControl(x, y);
  if (iFound >= 0)
    return _objects[iFound]->GetTooltipArea(xDraw, yDraw);
  return 0; // not found
}

void CObjectsGroup::OnDraw(float alpha)
{
  // draw controls
  for (int i=0; i<_objects.Size(); i++)
  {
    ControlObject *ctrl = _objects[i];
    if (ctrl && ctrl->IsVisible())
      ctrl->OnDraw(alpha, _x, _y);
  }
}

void CObjectsGroup::OnDrawBackGround(UIViewport *vp, float alpha)
{
  //// animate controls
  //for (int i=0; i<_objects.Size(); i++)
  //{
  //  Control *ctrl = _controls[i];
  //  if (ctrl)
  //    ctrl->Animate();
  //}


  Point2DFloat offset(_x, _y);
  Rect2DFloat clip(_x, _y, _w, _h);


  UIViewport *vpChild = vp->CreateChild(offset, clip);

  // draw controls
  for (int i=0; i<_objects.Size(); i++)
  {
    if (i == _indexFocused) continue;
    ControlObject *ctrl = _objects[i];
    if (ctrl && ctrl->IsVisible())
      ctrl->OnDrawBackGround(vpChild, alpha);
  }
  // draw focused control at last
  if (_indexFocused >= 0 && _indexFocused < _objects.Size())
  {
    ControlObject *ctrl = _objects[_indexFocused];
    if (ctrl->IsVisible())
      ctrl->OnDrawBackGround(vpChild, alpha);
  }

  vp->DestroyChild(vpChild);
}

void CObjectsGroup::LoadObject(ParamEntryPar ctrlCls)
{
  int type = ctrlCls >> "type";
  int idc = ctrlCls >> "idc";
  ControlObject *ctrl = _parent->OnCreateObject(type, idc, ctrlCls);
  if (ctrl)
  {
    int index = _objects.Add(ctrl);
    if (ctrl->GetDefault()) _indexDefault = index;
  }
}


/// namespace used in displays (event handlers, actions attached to buttons etc.)
GameDataNamespace GUINamespace(NULL, "ui", false); // this namespace will not be serialized

#define UI_USING_MISSION_NAMESPACE  1

inline GameDataNamespace *UINamespace()
{
#if UI_USING_MISSION_NAMESPACE
  return GWorld->GetMissionNamespace();
#else
  return &GUINamespace;
#endif
}

#include <El/Modules/modules.hpp>

INIT_MODULE(UIControls, 2)
{
  // namespace need to be registered manually because of order of static variables initialization
  GGameState.RegisterStaticNamespace(&GUINamespace);
}

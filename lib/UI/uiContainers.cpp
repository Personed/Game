// Implementation of displays and dialogs

#include "../wpch.hpp"
#include "uiControls.hpp"
#include "../keyInput.hpp"
#include "../engine.hpp"
#include "../world.hpp"
#include "../dikCodes.h"
#include "../vkCodes.h"
#include <Es/ErrorProp/errorProp.hpp>

#include "resincl.hpp"

#include "../camera.hpp"

#include <Es/Algorithms/qsort.hpp>
#include <El/Common/perfProf.hpp>

#include "../mbcs.hpp"
#include "../fileLocator.hpp"
#include "../gameStateExt.hpp"

#include "uiViewport.hpp"
#include "../joystick.hpp"

#include "../diagModes.hpp"
#include "../saveGame.hpp"

#include "../stringtableExt.hpp"

#include <El/Clipboard/clipboard.hpp>
#include <El/Evaluator/express.hpp>

#include "uiMap.hpp"

// #pragma optimize("", off)

/*!
\file
Implementation file for controls containers.
*/

extern const float CameraZoom;
extern const float InvCameraZoom;

DEFINE_ENUM(DisplayEvent, DE, DISPLAY_EVENT_ENUM)

#define ENUM_LOAD(type, prefix, name) \
  entry = clsEntry.FindEntry("on"#name); \
  if (entry) \
  { \
    RString value = *entry; \
    _eventHandlers[prefix##name].Clear(); \
    if (value.GetLength() > 0) _eventHandlers[prefix##name].Add(value); \
  }

///////////////////////////////////////////////////////////////////////////////
// class ControlsContainer

static const float tooltipDelay = 0.5f;

#if _GAMES_FOR_WINDOWS
ControlsContainer::RequestType ControlsContainer::_saveRequestType = ControlsContainer::RTResaveNone;
#endif

ControlsContainer::ControlsContainer(ControlsContainer *parent)
{
  _parent = parent;
  _enableSimulation = true;
  _enableDisplay = true;
  _enableUI = true;
  _destroyed = false;
  Init();
  SetCursor("Arrow");
  
  _tooltipTime = Glob.uiTime + tooltipDelay;
  _tooltipArea = 0;
  _tooltipX = 0.0f;
  _tooltipY = 0.0f;
}

ControlsContainer::~ControlsContainer()
{
  // virtual function cannot be called from destructor
  // Destroy();
}

void ControlsContainer::Init()
{
  _alwaysShow = false;
  _moving = false;
  _idd = -1;
  _exit = -1;
  _controlsBackground.Clear();
  _objects.Clear();
  _controlsForeground.Clear();
  _lastX = GInput.cursorX;
  _lastY = GInput.cursorY; 
  _dblClkDetect = false;

  _controlFocused = ControlIdPersistent::Null();
  _controlLCaptured = ControlIdPersistent::Null();
  _controlRCaptured = ControlIdPersistent::Null();
  _controlDefault = ControlIdPersistent::Null();
  _controlMouseOver = ControlIdPersistent::Null();

  _dragCtrl = NULL;
  // TODO: move to display resource
  ParamEntryVal fontPars = Pars >> "CfgInGameUI" >> "DragAndDropFont";
  _dragFont = GEngine->LoadFont(GetFontID(fontPars >> "font"));
  _dragSize = fontPars >> "size";
  _dragColorEnabled = GetPackedColor(fontPars >> "colorEnabled");
  _dragColorDisabled = GetPackedColor(fontPars >> "colorDisabled");
  _dragHideCursor = fontPars >> "hideCursor";
  _dragShadow = fontPars >> "shadow";
}

IControl *ControlsContainer::GetCtrl(int idc)
{
  for (int i=0; i<_controlsBackground.Size(); i++)
  {
    IControl *ctrl = _controlsBackground[i]->GetCtrl(idc);
    if (ctrl) return ctrl;
  }
  for (int i=0; i<_objects.Size(); i++)
  {
    IControl *ctrl = _objects[i]->GetCtrl(idc);
    if (ctrl) return ctrl;
  }
  for (int i=0; i<_controlsForeground.Size(); i++)
  {
    IControl *ctrl = _controlsForeground[i]->GetCtrl(idc);
    if (ctrl) return ctrl;
  }
  return NULL;
}

const IControl *ControlsContainer::GetCtrl(int idc) const
{
  for (int i=0; i<_controlsBackground.Size(); i++)
  {
    const IControl *ctrl = _controlsBackground[i]->GetCtrl(idc);
    if (ctrl) return ctrl;
  }
  for (int i=0; i<_objects.Size(); i++)
  {
    const IControl *ctrl = _objects[i]->GetCtrl(idc);
    if (ctrl) return ctrl;
  }
  for (int i=0; i<_controlsForeground.Size(); i++)
  {
    const IControl *ctrl = _controlsForeground[i]->GetCtrl(idc);
    if (ctrl) return ctrl;
  }
  return NULL;
}

IControl *ControlsContainer::GetFocused()
{
  IControl *ctrl = _controlFocused.GetCtrl();
  return ctrl ? ctrl->GetFocused() : NULL;
}

const IControl *ControlsContainer::GetFocused() const
{
  const IControl *ctrl = _controlFocused.GetCtrl();
  return ctrl ? ctrl->GetFocused() : NULL;
}

IControl *ControlsContainer::GetDefault()
{
  IControl *ctrl = _controlDefault.GetCtrl();
  return ctrl ? ctrl->GetDefault() : NULL;
}

const IControl *ControlsContainer::GetDefault() const
{
  const IControl *ctrl = _controlDefault.GetCtrl();
  return ctrl ? ctrl->GetDefault() : NULL;
}

IControl *ControlsContainer::GetShortcut(int dikCode)
{
  // find the first control shortcut is handled by
  for (int i=0; i<_controlsBackground.Size(); i++)
  {
    IControl *ctrl = _controlsBackground[i]->GetShortcutControl(dikCode);
    if (ctrl) return ctrl;
  }
  for (int i=0; i<_objects.Size(); i++)
  {
    IControl *ctrl = _objects[i]->GetShortcutControl(dikCode);
    if (ctrl) return ctrl;
  }
  for (int i=0; i<_controlsForeground.Size(); i++)
  {
    IControl *ctrl = _controlsForeground[i]->GetShortcutControl(dikCode);
    if (ctrl) return ctrl;
  }
  return NULL;
}

bool ControlsContainer::FocusCtrl(int idc)
{
  for (int i=0; i<_controlsBackground.Size(); i++)
  {
    IControl *c = _controlsBackground[i];
    if (!c) continue;
    if (c->IDC() == idc || c->FocusCtrl(idc))
    {
      ControlId ci = ControlId(CLBackground, i);
      SetFocus(ci, CFAClick);
      return true;
    }
  }
  for (int i=0; i<_objects.Size(); i++)
  {
    IControl *c = _objects[i];
    if (!c) continue;
    if (c->IDC() == idc || c->FocusCtrl(idc))
    {
      ControlId ci = ControlId(CLObjects, i);
      SetFocus(ci, CFAClick);
      return true;
    }
  }
  for (int i=0; i<_controlsForeground.Size(); i++)
  {
    IControl *c = _controlsForeground[i];
    if (!c) continue;
    if (c->IDC() == idc || c->FocusCtrl(idc))
    {
      ControlId ci = ControlId(CLForeground, i);
      SetFocus(ci, CFAClick);
      return true;
    }
  }
  return false;
}

bool ControlsContainer::FocusCtrl(IControl *ctrl)
{
  for (int i=0; i<_controlsBackground.Size(); i++)
  {
    IControl *c = _controlsBackground[i];
    if (!c) continue;
    if (c == ctrl || c->FocusCtrl(ctrl))
    {
      ControlId ci = ControlId(CLBackground, i);
      SetFocus(ci, CFAClick);
      return true;
    }
  }
  for (int i=0; i<_objects.Size(); i++)
  {
    IControl *c = _objects[i];
    if (!c) continue;
    if (c == ctrl || c->FocusCtrl(ctrl))
    {
      ControlId ci = ControlId(CLObjects, i);
      SetFocus(ci, CFAClick);
      return true;
    }
  }
  for (int i=0; i<_controlsForeground.Size(); i++)
  {
    IControl *c = _controlsForeground[i];
    if (!c) continue;
    if (c == ctrl || c->FocusCtrl(ctrl))
    {
      ControlId ci = ControlId(CLForeground, i);
      SetFocus(ci, CFAClick);
      return true;
    }
  }
  return false;
}

IControl *ControlsContainer::GetCtrl(float x, float y)
{
  int i;
  for (i=0; i<_controlsForeground.Size(); i++)
  {
    IControl *ctrl = _controlsForeground[i];
    if
    (
      ctrl->IsVisible() &&
      ctrl->IsMouseEnabled() &&
      ctrl->IsInside(x, y)
    ) return ctrl->GetCtrl(x, y);
  }
  SortObjects();
  for (i=0; i<_objects.Size(); i++)
  {
    IControl *ctrl = _objects[i];
    if
    (
      ctrl->IsVisible() &&
      ctrl->IsMouseEnabled() &&
      ctrl->IsInside(x, y)
    ) return ctrl->GetCtrl(x, y);
  }
  for (i=0; i<_controlsBackground.Size(); i++)
  {
    IControl *ctrl = _controlsBackground[i];
    if
    (
      ctrl->IsVisible() &&
      ctrl->IsMouseEnabled() &&
      ctrl->IsInside(x, y)
    ) return ctrl->GetCtrl(x, y);
  }
  return NULL;
}

void ControlsContainer::SetCursor(const char *name)
{
  if (name)
  {   
    if (_cursorName && stricmp(name, _cursorName) == 0) return;
    _cursorName = name;
    ParamEntryVal cls = Pars >> "CfgWrapperUI" >> "Cursors" >> _cursorName;
    _cursorTexture = GlobPreloadTexture(GetPictureName(cls >> "texture"));
    _cursorX = cls >> "hotspotX";
    _cursorY = cls >> "hotspotY";
    _cursorW = cls >> "width";
    _cursorH = cls >> "height";
    _cursorColor = GetPackedColor(cls >> "color");
    _cursorShadow = cls >> "shadow";
  }
  else
  {
    if (!_cursorName || _cursorName.GetLength() == 0) return;
    _cursorName = "";
    _cursorTexture = NULL;
    _cursorX = 0;
    _cursorY = 0;
    _cursorColor = PackedColor(0,0,0,0);
    _cursorShadow = 0;
  }
}

void ControlsContainer::LoadControl(ParamEntryPar ctrlCls)
{
  int type = ctrlCls>>"type";
  int idc = ctrlCls>>"idc";
  Control *ctrl = OnCreateCtrl(type, idc, ctrlCls);
  if (ctrl != NULL)
  {
    _controlsForeground.Add(ctrl);
    if (ctrl->GetDefault())
    {
      Assert(_controlDefault.IsNull()); // Only one default button
      _controlDefault = ControlIdPersistent(CLForeground, ctrl);
    }
  }
}

void ControlsContainer::LoadObject(ParamEntryPar ctrlCls)
{
  int type = ctrlCls>>"type";
  int idc = ctrlCls>>"idc";
  ControlObject *ctrl = OnCreateObject(type, idc, ctrlCls);
  if (ctrl != NULL)
  {
    _objects.Add(ctrl);
    if (ctrl->GetDefault())
    {
      Assert(_controlDefault.IsNull()); // Only one default button
      _controlDefault = ControlIdPersistent(CLObjects, ctrl);
    }
  }
}

void ControlsContainer::LoadControlBackground(ParamEntryPar ctrlCls)
{
  int type = ctrlCls>>"type";
  int idc = ctrlCls>>"idc";
  Control *ctrl = OnCreateCtrl(type, idc, ctrlCls);
  if (ctrl != NULL)
  {
    _controlsBackground.Add(ctrl);
    if (ctrl->GetDefault())
    {
      Assert(_controlDefault.IsNull()); // Only one default button
      _controlDefault = ControlIdPersistent(CLBackground, ctrl);
    }
  }
}

void ControlsContainer::LoadHints(ParamEntryPar clsEntry)
{
  ConstParamEntryPtr hints = clsEntry.FindEntry("KeyHints");
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
}

#define LOAD_SOUND(name) \
{ \
  ConstParamEntryPtr entry = clsEntry.FindEntry(#name); \
  if (entry) GetValue(_##name, *entry); \
  else if (defaults) \
  { \
    entry = defaults->FindEntry(#name); \
    if (entry) GetValue(_##name, *entry); \
  } \
}

void ControlsContainer::LoadSounds(ParamEntryPar clsEntry)
{
  ConstParamEntryPtr defaults = Pars.FindEntry("DefaultSounds");
  LOAD_SOUND(soundOK)
  LOAD_SOUND(soundFail)
  LOAD_SOUND(soundCancel)
  LOAD_SOUND(soundChangeFocus)
  _soundChangeFocusFailed = _soundFail;
  LOAD_SOUND(soundChangeFocusFailed)

  LOAD_SOUND(soundWhiteProcessed)
  LOAD_SOUND(soundBlackProcessed)
  LOAD_SOUND(soundXProcessed)
  LOAD_SOUND(soundYProcessed)
  LOAD_SOUND(soundStartProcessed)
  LOAD_SOUND(soundLeftTriggerProcessed)
  LOAD_SOUND(soundRightTriggerProcessed)
  LOAD_SOUND(soundLeft)
  LOAD_SOUND(soundRight)
  LOAD_SOUND(soundUp)
  LOAD_SOUND(soundDown)

  if (GWorld) // world is needed for sound scene
  {
    SoundPars soundOpen;
    ConstParamEntryPtr entry = clsEntry.FindEntry("soundOpen");
    if (entry) GetValue(soundOpen, *entry);
    else if (defaults)
    {
      entry = defaults->FindEntry("soundOpen");
      if (entry) GetValue(soundOpen, *entry);
    }
    if (soundOpen.name.GetLength() > 0) IControl::PlaySound(soundOpen);
  }
}

/*!
\patch 1.62 Date 5/30/2002 by Ondra
- Added: Instead of arrays controls[], objects[] and controlsBackground[] classes
may be used. This way it is not necessary to write class names twice.
*/

void ControlsContainer::LoadFromEntry(ParamEntryPar clsEntry)
{
  _idd = clsEntry>>"idd";
  _className = clsEntry.GetName();
  LoadHints(clsEntry);
  LoadSounds(clsEntry);

  ConstParamEntryPtr entry = clsEntry.FindEntry("enableSimulation");
  if (entry) _enableSimulation = *entry;
  entry = clsEntry.FindEntry("enableDisplay");
  if (entry) _enableDisplay = *entry;

  DISPLAY_EVENT_ENUM(DisplayEvent, DE, ENUM_LOAD)

  ConstParamEntryPtr cfg = clsEntry.FindEntry("controls");
  if (cfg)
  {
    if (cfg->IsClass())
    {
      for (int i=0; i<cfg->GetEntryCount(); i++)
      {
        ParamEntryVal ctrlCls = cfg->GetEntry(i);
#ifdef DEBUG_UI_RSC
        RString clname = Format("%s/controls/%s",cc_cast(clsEntry.GetName()), cc_cast(ctrlCls.GetName()));
        if (stricmp(cc_cast(clname),"RscDisplayNotFreeze/controls/Text")==0)
        {
          RptF(" I am here!");
        }
#endif
        if (!ctrlCls.IsClass()) continue;
        if (!ctrlCls.FindEntry("type")) { RptF("Warning: no type entry inside class %s/controls/%s ", cc_cast(clsEntry.GetName()), cc_cast(ctrlCls.GetName())); continue; }
        if (!ctrlCls.FindEntry("idc")) { RptF("Warning: no idc entry inside class %s/controls/%s ", cc_cast(clsEntry.GetName()), cc_cast(ctrlCls.GetName())); continue; }
        bool errorOnce = false;
        ERROR_TRY()
          LoadControl(ctrlCls);
        ERROR_CATCH_ANY(exc)
          if (!errorOnce)
          {
            RptF("Error loading control %s",cc_cast(ctrlCls.GetContext()));
            errorOnce = true;
          }
        ERROR_END()
      }
    }
    else
    {
      for (int i=0; i<cfg->GetSize(); i++)
      {
        RString ctrlName = (*cfg)[i];
        ParamEntryVal ctrlCls = clsEntry>>ctrlName;
        bool errorOnce = false;
        ERROR_TRY()
          LoadControl(ctrlCls);
        ERROR_CATCH_ANY(exc)
          if (!errorOnce)
          {
            RptF("Error loading control %s",cc_cast(ctrlCls.GetContext()));
            errorOnce = true;
          }
        ERROR_END()
      }
    }
  }
  cfg = clsEntry.FindEntry("objects");
  if (cfg)
  {
    if (cfg->IsClass())
    {
      for (int i=0; i<cfg->GetEntryCount(); i++)
      {
        ParamEntryVal ctrlCls = cfg->GetEntry(i);
        if (!ctrlCls.IsClass()) continue;
        if (!ctrlCls.FindEntry("type")) { RptF("Warning: no type entry inside class %s/objects/%s ", cc_cast(clsEntry.GetName()), cc_cast(ctrlCls.GetName())); continue; }
        if (!ctrlCls.FindEntry("idc")) { RptF("Warning: no idc entry inside class %s/objects/%s ", cc_cast(clsEntry.GetName()), cc_cast(ctrlCls.GetName())); continue; }
        LoadObject(ctrlCls);
      }
    }
    else
    {
      for (int i=0; i<cfg->GetSize(); i++)
      {
        RString ctrlName = (*cfg)[i];
        ParamEntryVal ctrlCls = clsEntry>>ctrlName;
        LoadObject(ctrlCls);
      }
    }
  }
  cfg = clsEntry.FindEntry("controlsBackground");
  if (cfg)
  {
    if (cfg->IsClass())
    {
      for (int i=0; i<cfg->GetEntryCount(); i++)
      {
        ParamEntryVal ctrlCls = cfg->GetEntry(i);
        if (!ctrlCls.IsClass()) continue;
        if (!ctrlCls.FindEntry("type")) { RptF("Warning: no type entry inside class %s/controlsBackground/%s ", cc_cast(clsEntry.GetName()), cc_cast(ctrlCls.GetName())); continue; }
        if (!ctrlCls.FindEntry("idc")) { RptF("Warning: no idc entry inside class %s/controlsBackground/%s ", cc_cast(clsEntry.GetName()), cc_cast(ctrlCls.GetName())); continue; }
        LoadControlBackground(ctrlCls);
      }
    }
    else
    {
      for (int i=0; i<cfg->GetSize(); i++)
      {
        RString ctrlName = (*cfg)[i];
        ParamEntryVal ctrlCls = clsEntry>>ctrlName;
        LoadControlBackground(ctrlCls);
      }
    }
  }
  NextCtrl();

  if (!_controlDefault.IsNull())
  {
    IControl *focused = GetFocused();
    if (focused && focused->CanBeDefault())
    {
      ControlId index = FindCtrl(_controlDefault);
      SetFocus(index, CFANext); 
    }
  }

  if (_controlFocused.IsNull()) UpdateKeyHints();

  OnEvent(DELoad);
}

#if _ENABLE_CHEATS
#include "El/DebugConsole/debugConsole.hpp"
#endif
void ControlsContainer::Load(const char *clsName)
{
  if (CHECK_DIAG(DEResource))
  {
    RString text = Format("Loading resource: %s", cc_cast(clsName));
    DIAG_MESSAGE(10000, text);
#if _ENABLE_CHEATS
    extern AutoArray<RString> DebugConsoleLog;
    DebugConsoleLog.Add(text);
    GDebugConsole->Printf(text);
    LogF("<load-resource>");
    LogF(text);
    LogF("</load-resource>");
#endif
  }

  ParamEntryVal mainCfg = Pars >> clsName;
  LoadFromEntry(mainCfg);
}

void ControlsContainer::Load(int idd)
{
  #if 0
  HRSRC hRsrc = FindResource(NULL, (LPCSTR)idd, RT_DIALOG);
  HGLOBAL hGlobal = LoadResource(NULL, hRsrc);
  WORD *ptr = (WORD *)LockResource(hGlobal);

  if (*ptr == 0xffff)
  {
    ptr++;
    // extended template
//  see DLGTEMPLATEEX, DLGITEMTEMPLATEEX;

  }
  else
  {
    // standard template
    DLGTEMPLATE *dlg = (DLGTEMPLATE *)ptr;
    WORD *ptr2 = ptr;
    int nItems = dlg->cdit;
    float invw = 1.0 / dlg->cx;
    float invh = 1.0 / dlg->cy;
    bool hasFont = (dlg->style & DS_SETFONT) != 0;
    ptr += (sizeof(DLGTEMPLATE) + 1) / 2;
    WORD menu = *ptr;
    if (menu == 0xffff)
    {
      ptr++;
      WORD menuID = *ptr++;
    }
    else
    {
      PWCHAR menuID = ptr;
      ptr += wcslen(menuID) + 1;
    }
    WORD wndClass = *ptr;
    if (wndClass == 0xffff)
    {
      ptr++;
      WORD wndClassID = *ptr++;
    }
    else
    {
      PWCHAR wndClassID = ptr;
      ptr += wcslen(wndClassID) + 1;
    }
    PWCHAR title = ptr;
    ptr += wcslen(title) + 1;
    if (hasFont)
    {
      WORD fontSize = *ptr++;
      PWCHAR fontName = ptr;
      ptr += wcslen(fontName) + 1;
    }
    // controls
    for (int i=0; i<nItems; i++)
    {
      ptr = ptr2 + ((ptr - ptr2 + 1) / 2) * 2;
      ptr2 = ptr;
      DLGITEMTEMPLATE *item = (DLGITEMTEMPLATE *)ptr;
      WORD style = item->style;
      float x = invw * item->x;
      float y = invh * item->y;
      float w = invw * item->cx;
      float h = invh * item->cy;
      short idc = item->id;
      ptr += (sizeof(DLGTEMPLATE) + 1) / 2;
      WORD wndClass = *ptr;
      WORD wndClassID = 0;
      if (wndClass == 0xffff)
      {
        ptr++;
        wndClassID = *ptr++;
      }
      else
      {
        PWCHAR wndClassID = ptr;
        ptr += wcslen(wndClassID) + 1;
      }
      PWCHAR title = ptr;
      ptr += wcslen(title) + 1;
      char buffer[1024];
      WideCharToMultiByte(CP_ACP, 0, title, -1, buffer, 1024, NULL, NULL);

      if (idc != -1)
      {
        ptr = ptr2 + ((ptr - ptr2 + 1) / 2) * 2;
        ptr2 = ptr;
        WORD size = *ptr;
        if (size == 0)
        {
          ptr++;
        }
        else
        {
          ptr += (size + 1) / 2;
        }
      }
    }
  }
  #endif
}

bool ControlsContainer::CanDestroy()
{
  for (int i=0; i<_controlsBackground.Size(); i++)
  {
    if (!_controlsBackground[i]->OnCheckEventFalse(CECanDestroy, _exit))
      return false;
    if (!_controlsBackground[i]->OnCanDestroy(_exit))
      return false;
  }
  for (int i=0; i<_objects.Size(); i++)
  {
    if (!_objects[i]->OnCheckEventFalse(CECanDestroy, _exit))
      return false;
    if (!_objects[i]->OnCanDestroy(_exit))
      return false;
  }
  for (int i=0; i<_controlsForeground.Size(); i++)
  {
    if (!_controlsForeground[i]->OnCheckEventFalse(CECanDestroy, _exit))
      return false;
    if (!_controlsForeground[i]->OnCanDestroy(_exit))
      return false;
  }
  return true;
}

/*!
\patch 1.85 Date 9/17/2002 by Jirka
- Fixed: When retrying mission, sometimes my group has other weapons than previously selected in briefing
\patch_internal 1.85 Date 9/17/2002 by Jirka
- Fixed: ControlsContainer::Destroy() was called twice when dialog was closed via keyboard
- Note: Always call base::Destroy from inherited methods
*/

void ControlsContainer::Destroy()
{
  OnEvent(DEUnload, _exit);

  for (int i=0; i<_controlsBackground.Size(); i++)
  {
    _controlsBackground[i]->OnEvent(CEDestroy, _exit);
    _controlsBackground[i]->OnDestroy(_exit);
  }
  for (int i=0; i<_objects.Size(); i++)
  {
    _objects[i]->OnEvent(CEDestroy, _exit);
    _objects[i]->OnDestroy(_exit);
  }
  for (int i=0; i<_controlsForeground.Size(); i++)
  {
    _controlsForeground[i]->OnEvent(CEDestroy, _exit);
    _controlsForeground[i]->OnDestroy(_exit);
  }
  _destroyed = true;
}

bool ControlsContainer::CheckDestroy()
{
#if _GAMES_FOR_WINDOWS
  // handle return to main menu request
  if (GSaveSystem.GetReturnToMainMenuRequest() != SaveSystem::RRNone)
    OnReturnToMainMenuRequest();
  // handle profile change
  else if (GSaveSystem.IsCheckInChanged()) 
    OnCheckInChanged();
  // handle invite 
  else if (GSaveSystem.IsInvited()) 
    OnInvited(); 
else 
  {
    if (GSaveSystem.IsStorageChanged())
      OnStorageDeviceChanged();
    if (GSaveSystem.IsStorageRemoved())
      OnStorageDeviceRemoved();
    if (GSaveSystem.IsSomeSignInChanged())
    {
      OnSomeSignInChanged();
    }
    if (GSaveSystem.IsLiveConnectionLost())
    {
      OnUserDisconnectedFromLive();
    }
    if (GSaveSystem.IsSavesChanged())
      OnSavesChanged();
    if (_saveRequestType != RTResaveNone)
    {
      HandleRequest(_saveRequestType);
      _saveRequestType = RTResaveNone;
    }
  }
#endif

  if (_exit < 0) return false;
  if (_destroyed) return true;
  if (CanDestroy())
  {
    Destroy();
    return true;
  }
  _exit = -1;
  return false;
}

#if _GAMES_FOR_WINDOWS

void ControlsContainer::OnReturnToMainMenuRequest()
{
  DoAssert(GSaveSystem.GetReturnToMainMenuRequest() != SaveSystem::RRNone);
  if (GSaveSystem.GetReturnToMainMenuRequest() == SaveSystem::RRNone)
    return;

  // return to the main menu (by default, exit the current display)
  Exit(IDC_CANCEL);
}

void ControlsContainer::OnCheckInChanged()
{
  // set change as handled
  GSaveSystem.CheckInHandled();

  // set request to return to main menu
  GSaveSystem.SetReturnToMainMenuRequest(SaveSystem::RRSignInChanged);
  // and handle the request (must be called! otherwise it would be handled in the next call to CheckDestroy(), but it would be too late)
  OnReturnToMainMenuRequest();
}


void ControlsContainer::OnSomeSignInChanged()
{
  // handle sign in change
  GSaveSystem.SignInChangeHandled();
}

void ControlsContainer::OnSavesChanged()
{
  // handle saves change
  GSaveSystem.SavesChangeHandled();
}

void ControlsContainer::OnUserDisconnectedFromLive()
{
  // current user was disconnected from Live

  // set change as handled
  GSaveSystem.LiveConnectionLostHandled();

  // if we need connection to Live or if we are in multiplayer game that needs Live (session exists and we are not playing system link)
  if (GSaveSystem.IsLiveConnectionNeeded() || (GetNetworkManager().SessionExists() && !GetNetworkManager().IsSystemLink()))
  {
    // set request to return to main menu
    GSaveSystem.SetReturnToMainMenuRequest(SaveSystem::RRLiveDisconnected);
    // and handle the request (must be called! otherwise it would be handled in the next call to CheckDestroy(), but it would be too late)
    OnReturnToMainMenuRequest();
  }
  else if (!GetNetworkManager().IsSystemLink())
  {
    // just show the warning for user
    // localization: "You are not signed to Xbox LIVE. Statistics will not be posted on leaderboards."
    CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_LIVE_STATS_WARN));
  }

}

void ControlsContainer::OnInvited()
{
  // navigate to multiplayer menu (by default, exit the current display)
  Exit(IDC_CANCEL);
}

void ControlsContainer::OnStorageDeviceRemoved()
{
}

void ControlsContainer::OnStorageDeviceChanged()
{
}

void ControlsContainer::HandleRequest(ControlsContainer::RequestType request)
{
  if (RTResaveMissionAutosave == request)
    GWorld->AutoSaveRequest();
  else
  // by default just propagate the request to the parent
  if (_parent)
    _parent->HandleRequest(request);
}

bool ControlsContainer::IsInUserMissionWizard() const
{
  if (_parent)
    return _parent->IsInUserMissionWizard();
  return false;
}

#endif

void ControlsContainer::Move(float dx, float dy)
{
  for (int i=0; i<_controlsBackground.Size(); i++)
    _controlsBackground[i]->Move(dx, dy);
  for (int i=0; i<_objects.Size(); i++)
    _objects[i]->Move(dx, dy);
  for (int i=0; i<_controlsForeground.Size(); i++)
    _controlsForeground[i]->Move(dx, dy);
}

Control *ControlsContainer::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
#if _VBS2 // version number is available for any display
  switch (idc)
  {
    case IDC_BUILD_VERSION:
      {
        CStatic *text = new CStatic(this, idc, cls);
        text->SetText(Glob.buildVersion);
        return text;
      }
    case IDC_BUILD_DATE:
      {
        CStatic *text = new CStatic(this, idc, cls);
        text->SetText(Glob.buildDate);
#if _VBS3
        text->SetText(Glob.expiryDate);
#endif
        return text;
      }
  }
#endif
  switch (type)
  {
    case CT_STATIC:
      return new CStatic(this, idc, cls);
    case CT_EDIT:
      return new CEdit(this, idc, cls);
    case CT_BUTTON:
      return new CButton(this, idc, cls);
    case CT_TAB_BUTTON:
      return new CTabButton(this, idc, cls);
    case CT_SLIDER:
      return new CSlider(this, idc, cls);
    case CT_COMBO:
      return new CCombo(this, idc, cls);
    case CT_LISTBOX:
      return new CListBox(this, idc, cls);
    case CT_LISTNBOX:
      return new CListNBox(this, idc, cls);
    case CT_TOOLBOX:
      return new CToolBox(this, idc, cls);
    case CT_CHECKBOXES:
      return new CCheckBoxes(this, idc, cls);
    case CT_PROGRESS:
      return new CProgressBar(this, idc, cls);
    case CT_HTML:
      return new CHTML(this, idc, cls);
    case CT_STATIC_SKEW:
      return new CSkewStatic(this, idc, cls);
    case CT_ACTIVETEXT:
      return new CActiveText(this, idc, cls);
    case CT_TREE:
      return new CTree(this, idc, cls);
    case CT_STRUCTURED_TEXT:
      return new CStructuredText(this, idc, cls);
    case CT_CONTEXT_MENU:
      Fail("Do not create it this way");
      return NULL;
    case CT_CONTROLS_GROUP:
      return new CControlsGroup(this, idc, cls);
    case CT_SHORTCUTBUTTON:
      return new CShortcutButton(this, idc, cls);
    case CT_XKEYDESC:
      return new CXKeyDesc(this, idc, cls);
    case CT_XBUTTON:
      return new CXButton(this, idc, cls);
    case CT_XLISTBOX:
      return new CXListBox(this, idc, cls);
    case CT_XSLIDER:
      return new CXSlider(this, idc, cls);
    case CT_XCOMBO:
        return new CXCombo(this, idc, cls);
    case CT_LINEBREAK:
      return new CLineBreak(this);
    case CT_ANIMATED_TEXTURE:
      return new CAnimatedTexture(this, idc, cls);
    case CT_MAP:
      {
        CStatic *CreateStaticMap(ControlsContainer *parent, int idc, const ParamEntry &cls);
        return CreateStaticMap(this, idc, cls);
      }
    case CT_MAP_MAIN:
      {
        CStatic *CreateStaticMapMain(ControlsContainer *parent, int idc, const ParamEntry &cls);
        return CreateStaticMapMain(this, idc, cls);
      }
  }
  return NULL;
}

void ControlsContainer::OnLBDrag(CListBoxContainer *lbox, int sel)
{
  Assert(lbox);
  Assert(sel >= 0 && sel < lbox->GetSize());

  _dragCtrl = lbox->GetControl();
  if ((lbox->GetControl()->GetStyle() & LB_MULTI) && lbox->Get(sel).selected)
  {
    _dragRows.Resize(0);
    for (int i=0; i<lbox->GetSize(); i++)
    {
#if _VBS3
      if (lbox->Get(i).selected)
      {
        CListBoxItem item = lbox->Get(i);
        item.oldIndex = i;
        _dragRows.Add(item);
      }
#else
      if (lbox->Get(i).selected) _dragRows.Add(lbox->Get(i));
#endif
    }
    _dragRows.Compact();
  }
  else
  {
    _dragRows.Realloc(1);
    _dragRows.Resize(1);
#if _VBS3
    CListBoxItem item = lbox->Get(sel);
    item.oldIndex = sel;
   _dragRows[0] = item;
#else
    _dragRows[0] = lbox->Get(sel);
#endif
  }
}

void ControlsContainer::OnDrag(IControl *ctrl)
{
  _dragCtrl = ctrl;
  _dragRows.Resize(0);
}


Camera OriginalCamera;


Ref<LightPoint> CreateUILight(const Color& diff, Vector3Par pos)
{
	LightPoint* light = new LightPoint(diff, HBlack);

	// Since introducing per-pixel lights the saturation inside shader is not so tough (0.3 changed to 3) and UI objects were to bright since
	float distanceFactor = 1;
	if (!GEngine->GetTL())
	{
		distanceFactor *= 0.2;
	}

	light->SetPosition(Vector3(pos.x*distanceFactor, pos.y*distanceFactor, pos.z));
	return light;
}



void ControlsContainer::OnDraw(EntityAI *vehicle, float alpha)
{
  if (_exit >= 0)
    return;

// draw controls
  for (int i=0; i<_objects.Size(); i++)
    _objects[i]->UpdatePosition();
  SortObjects();

  UIViewport *vp = NULL;
  if (_controlsBackground.Size()!=0 || _controlsForeground.Size()!=0 || _objects.Size()!=0)
  {
    vp = Create2DViewport();
  }

  Control *ctrlFocused = _controlFocused.GetCtrl2D();

  // drag & drop simulation
  float mouseX = 0, mouseY = 0;
  unsigned char dropEnabled = 0;
  if (_dragCtrl)
  {
    mouseX = 0.5 + GInput.cursorX * 0.5;
    mouseY = 0.5 + GInput.cursorY * 0.5;
    IControl *mouseOver = GetCtrl(mouseX, mouseY);

    if (_dragRows.Size() > 0)
    {
      // listbox rows

      // TODO: Event handler
      dropEnabled = OnLBDragging(mouseOver, mouseX, mouseY); 
      if (mouseOver && !dropEnabled) dropEnabled = mouseOver->OnCheckEvent(CELBDragging, mouseX, mouseY, _dragCtrl->IDC(), _dragRows);
    }
    else
    {
      // whole control

      // TODO: Event handler
      dropEnabled = OnDragging(_dragCtrl, mouseOver, mouseX, mouseY); 
    }
  }

  for (int i=0; i<_controlsBackground.Size(); i++)
  {
    Control *ctrl = _controlsBackground[i];
    if (ctrl == ctrlFocused) continue;
    ctrl->Animate();
    if (ctrl->IsVisible())
    {
      ctrl->OnDraw(vp, alpha * ctrl->GetAlpha());
    }
  }
  if (ctrlFocused && _controlFocused.GetList() == CLBackground)
  {
    ctrlFocused->Animate();
    if (ctrlFocused->IsVisible())
    {
      ctrlFocused->OnDraw(vp, alpha * ctrlFocused->GetAlpha());
    }
  }
  
  LightSun *oldLight = nullptr;
  SRef<LightSun> mainLight;

  // temporary override camera
  if (_objects.Size() > 0 && GEngine->Allow3DUI())
  {
    for (int i=0; i<_objects.Size(); i++)
    {
      ControlObject *ctrlObject = _objects[i];
      if (ctrlObject->IsVisible() && vp)
      {
        ctrlObject->OnDrawBackGround(vp, alpha * ctrlObject->GetAlpha());
      }
    }

    // clear Z-buffer
    GEngine->Clear(true, false);

//		{
#if 0 //_ENABLE_CHEATS
/*
      static float diffuse = 1.00;
      static float ambient = 0.50;
      static float brightness = 0.092;
      static float distance = 10;
*/
      static float diffuse = 2.65f;
      static float ambient = 0.60f;
      static float brightness = 0.092f;
      static float distance = 2.15f;

      if( GInput.GetCheat2ToDo(DIK_LEFT) )
      {
        diffuse *= 0.95;
        GlobalShowMessage(1000,"diffuse %.3f",diffuse);
      }
      if( GInput.GetCheat2ToDo(DIK_RIGHT) )
      {
        diffuse *= 1/0.95;
        GlobalShowMessage(1000,"diffuse %.3f",diffuse);
      }
      if( GInput.GetCheat1ToDo(DIK_LEFT) )
      {
        ambient *= 0.95;
        GlobalShowMessage(1000,"ambient %.3f",ambient);
      }
      if( GInput.GetCheat1ToDo(DIK_RIGHT) )
      {
        ambient *= 1/0.95;
        GlobalShowMessage(1000,"ambient %.3f",ambient);
      }
      if( GInput.GetCheat3ToDo(DIK_LEFT) )
      {
        distance *= 0.95;
        GlobalShowMessage(1000,"distance %.3f",distance);
      }
      if( GInput.GetCheat3ToDo(DIK_RIGHT) )
      {
        distance *= 1/0.95;
        GlobalShowMessage(1000,"distance %.3f",distance);
      }
#else
      static float brightness = 0.092f;
#endif
		// create lights
		static float xDist = 3.0f;
		static float yDist = 1.0f;
		static float zDist = -3.0f;
		static Color lightColor = Color(1.4f, 1.4f, 1.4f, 1);

		LightList work;
		work.Resize(2);
    work[0] = CreateUILight(lightColor, Vector3( xDist, yDist, zDist));
		work[1] = CreateUILight(lightColor, Vector3(-xDist, yDist, zDist));

    GScene->SetActiveLights(work);
  
    // create camera
    Assert(GScene->GetCamera());
    OriginalCamera = *GScene->GetCamera();
    // set object drawing parameters
    Camera *cam = GScene->GetCamera();
    *cam = Camera();
    AspectSettings as;
    GEngine->GetAspectSettings(as);
    float fov = 0.1875;
/*
    cam->SetPerspective(
      0.1, 100.0, as.leftFOV * fov, as.topFOV * fov,
      Glob.config.shadowsZ, Glob.config.GetProjShadowsZ(), 100.0
    );
*/
    // FIX: fixed perspective parameters to keep the same scene for different FOVs
    cam->SetPerspective(0.1, 100.0, fov * as.leftFOV / as.topFOV, fov, Glob.config.GetShadowZ(), Glob.config.GetProjShadowsZ(), 100.0);

    // HOTFIX: avoid problems with lighting from Sun
    oldLight = GScene->MainLight();
    mainLight = new LightSun();
    mainLight->Recalculate();
    mainLight->SetLighting(HBlack, HBlack, HBlack, 1.0f);
    GScene->SetMainLight(mainLight);
    GScene->MainLightChanged();
		
		GEngine->SetFogColor(HBlack, HBlack);

    //GScene->SetCamera(cam);
    cam->Adjust(GEngine);
    for (int i=_objects.Size()-1; i>=0; i--)
    {
      ControlObject *ctrl = _objects[i];
      if (ctrl->IsVisible() && (ctrl->WaitForLoad() || ctrl->IsReady()))
      {
        ctrl->OnDraw(alpha * ctrl->GetAlpha());
      }
    }
    // drag & drop drawing
    if (_dragCtrl) DrawDragInfo(mouseX, mouseY, dropEnabled, _dragFont, _dragSize);

    for (int i=0; i<_objects.Size(); i++)
    {
      ControlObject *ctrlObject = _objects[i];
      if (ctrlObject->IsVisible() && vp)
      {
        ctrlObject->OnDrawForeGround(vp, alpha * ctrlObject->GetAlpha());
      }
    }
  }

  for (int i=0; i<_controlsForeground.Size(); i++)
  {
    Control *ctrl = _controlsForeground[i];
    if (ctrl == ctrlFocused) continue;
    ctrl->Animate();
    if (ctrl->IsVisible())
    {
      ctrl->OnDraw(vp, alpha * ctrl->GetAlpha());
    }
  }
  if (ctrlFocused && _controlFocused.GetList() == CLForeground)
  {
    ctrlFocused->Animate();
    if (ctrlFocused->IsVisible())
    {
      ctrlFocused->OnDraw(vp, alpha * ctrlFocused->GetAlpha());
    }
  }

  // drag & drop drawing
  if (_dragCtrl)
  {
    if (_dragRows.Size() > 0)
    {
      // listbox rows
      PackedColor color = dropEnabled ? _dragColorEnabled : _dragColorDisabled;
      RString text = _dragRows[0].text;
      if (_dragRows.Size() > 1) text = text + RString(", ...");
      mouseY -= 0.5 * _dragSize;
      GEngine->DrawText(Point2DFloat(mouseX, mouseY), _dragSize, _dragFont, color, text, _dragShadow);
    }
  }

  // show tooltip only for the top display
  if (!Child() && !GetMsgBox() && _tooltipArea != 0)
  {
    IControl *ctrl = _controlMouseOver.GetCtrl();
    if (ctrl) ctrl->DrawTooltip(vp, _tooltipX, _tooltipY);
  }
  if (_objects.Size() > 0 && GEngine->Allow3DUI())
  {
    // restore camera
    GScene->SetCamera(OriginalCamera);

    // restore the original Sun
    GScene->SetMainLight(oldLight);
    GScene->MainLightChanged();
  }
  
  if (vp) Destroy2DViewport(vp);
}

bool ControlsContainer::OnUnregisteredAddonUsed(RString addon)
{
  return false;
}

bool ControlsContainer::IsReady() const
{
  bool ready = true;

  for (int i=0; i<_controlsBackground.Size(); i++)
    if (!_controlsBackground[i]->IsReady()) ready = false;
  for (int i=0; i<_objects.Size(); i++)
    if (!_objects[i]->IsReady()) ready = false;
  for (int i=0; i<_controlsForeground.Size(); i++)
    if (!_controlsForeground[i]->IsReady()) ready = false;

  return ready;
}

/*!
\patch 5145 Date 3/23/2007 by Jirka
- Fixed: UI - when two controls are drawn over, mouse events are now received by the one in the front
*/

ControlIdPersistent ControlsContainer::FindCtrl(float x, float y)
{
  // check focused item first
  Control *ctrlFocused = _controlFocused.GetCtrl2D();

  if (ctrlFocused && _controlFocused.GetList() == CLForeground)
  {
    if (ctrlFocused->IsVisible() && ctrlFocused->IsMouseEnabled() && ctrlFocused->IsInside(x, y))
    {
      return _controlFocused;
    }
  }
  // other items need to be checked in reversed order (items drawn last will receive events first)
  for (int i=_controlsForeground.Size()-1; i>=0; i--)
  {
    Control *ctrl = _controlsForeground[i];
    Assert(ctrl);
    if (ctrl == ctrlFocused) continue;
    if (ctrl->IsVisible() && ctrl->IsMouseEnabled() && ctrl->IsInside(x, y))
    {
      return ControlIdPersistent(CLForeground, ctrl);
    }
  }
  for (int i=_controlsForeground.Size()-1; i>=0; i--)
  {
    Control *ctrl = _controlsForeground[i];
    Assert(ctrl);
    if (ctrl->IsVisible() && ctrl->IsMovingDisplay() && ctrl->IsInside(x, y))
    {
      return ControlIdPersistent(CLForeground, ctrl);
    }
  }

  SortObjects();
  for (int i=_objects.Size()-1; i>=0; i--)
  {
    ControlObject *ctrl = _objects[i];
    Assert(ctrl);
    if (ctrl->IsVisible() && ctrl->IsMouseEnabled() && ctrl->IsInside(x, y))
    {
      return ControlIdPersistent(CLObjects, ctrl);
    }
  }

  // check focused item first
  if (ctrlFocused && _controlFocused.GetList() == CLBackground)
  {
    if (ctrlFocused->IsVisible() && ctrlFocused->IsMouseEnabled() && ctrlFocused->IsInside(x, y))
    {
      return _controlFocused;
    }
  }
  // other items need to be checked in reversed order (items drawn last will receive events first)
  for (int i=_controlsBackground.Size()-1; i>=0; i--)
  {
    Control *ctrl = _controlsBackground[i];
    Assert(ctrl);
    if (ctrl == ctrlFocused) continue;
    if (ctrl->IsVisible() && ctrl->IsMouseEnabled() && ctrl->IsInside(x, y))
    {
      return ControlIdPersistent(CLBackground, ctrl);
    }
  }
  for (int i=_controlsBackground.Size()-1; i>=0; i--)
  {
    Control *ctrl = _controlsBackground[i];
    Assert(ctrl);
    if (ctrl->IsVisible() && ctrl->IsMovingDisplay() && ctrl->IsInside(x, y))
    {
      return ControlIdPersistent(CLBackground, ctrl);
    }
  }

  // not found
  return ControlIdPersistent::Null();
}

ControlId ControlsContainer::FindCtrl(const ControlIdPersistent &control)
{
  IControl *ctrl = control.GetCtrl();
  if (!ctrl) return ControlId::Null();

  switch (control.GetList())
  {
  case CLBackground:
    for (int i=0; i<_controlsBackground.Size(); i++)
    {
      if (_controlsBackground[i] == ctrl) return ControlId(CLBackground, i);
    }
    break;
  case CLForeground:
    for (int i=0; i<_controlsForeground.Size(); i++)
    {
      if (_controlsForeground[i] == ctrl) return ControlId(CLForeground, i);
    }
    break;
  case CLObjects:
    for (int i=0; i<_objects.Size(); i++)
    {
      if (_objects[i] == ctrl) return ControlId(CLObjects, i);
    }
    break;
  }
  return ControlId::Null();
}

void ControlsContainer::OnLButtonDown(IControl *ctrlFound, ControlIdPersistent controlFound, float mouseX, float mouseY, bool shift, bool control, bool alt, bool handleDblClk)
{
  // dialog level event handler
  OnEvent(DEMouseButtonDown, 0, mouseX, mouseY, shift, control, alt);

  // LogF("MouseButtonsToDo[0] was TRUE inside ControlsContainer::OnSimulate");
  if (ctrlFound)
  {
    if (ctrlFound->IsMouseEnabled())
    {
      ControlId index = FindCtrl(controlFound);
      SetFocus(index, CFAClick);

      ctrlFound->OnEvent(CEMouseButtonDown, 0, mouseX, mouseY, shift, control, alt);
      ctrlFound->OnLButtonDown(mouseX, mouseY);
      _dblClkDetect = true;
      _dblClkX = mouseX; 
      _dblClkY = mouseY; 

      if (_dblClkDetect && GInput.mouseButtonsDoubleToDo[0] && handleDblClk && ctrlFound == _controlLCaptured.GetCtrl())
      {
/*
        LogF("%.3f - Double click handled", 0.001f * ::GlobalTickCount());
*/
        ctrlFound->OnEvent(CEMouseButtonDblClick, 0, mouseX, mouseY, shift, control, alt);
        ctrlFound->OnLButtonDblClick(mouseX, mouseY);
        _dblClkDetect = false;
      }
    }
    else if (ctrlFound->IsMovingDisplay())
    {
      _moving = true;
    }
  }
  _controlLCaptured = controlFound;
}

/*!
\patch 5148 Date 3/26/2007 by Jirka
- Fixed: UI - double clicks detected better during low frame rate
*/

void ControlsContainer::OnLButtonUp(IControl *ctrlFound, float mouseX, float mouseY, bool shift, bool control, bool alt)
{
  // dialog level event handler
  OnEvent(DEMouseButtonUp, 0, mouseX, mouseY, shift, control, alt);

  IControl *ctrl = _controlLCaptured.GetCtrl();
  if (ctrl)
  {
    if (ctrl->IsMouseEnabled())
    {
      if (_dragCtrl)
      {
        if (!ctrlFound || !ctrlFound->OnCheckEvent(CELBDrop, mouseX, mouseY, _dragCtrl->IDC(), _dragRows
#if _VBS3 //add the Index the items are dropped onto
          , ctrlFound->GetPosIndex(mouseX, mouseY)
#endif
          ))
        {
          OnLBDrop(ctrlFound, mouseX, mouseY); 
          OnDrop( _dragCtrl ,ctrlFound, mouseX, mouseY); 
        }

        _dragCtrl = NULL;
        _dragRows.Resize(0);
        _controlLCaptured = ControlIdPersistent::Null();
      }
      else
      {
        ctrl->OnEvent(CEMouseButtonUp, 0, mouseX, mouseY, shift, control, alt);
        ctrl->OnLButtonUp(mouseX, mouseY);
      }
    }
  }
  if (GInput.mouseButtonsReleased[0] >= GInput.mouseButtonsPressed[0] + GInput.mouseDoubleClickTime)
  {
    // double click time out (mouse button hold too long)
    _controlLCaptured = ControlIdPersistent::Null();
  }
  // FIX: after double click, _controlLCaptured was still pressed and prevent sending of mouse move to the right button pressed control
  if (!_dblClkDetect)
  {
    _controlLCaptured = ControlIdPersistent::Null();
  }
  _moving = false;
}

#if _VBS3 
// Ability to position any UI control
// requires UIControls cheat active + Left shift while hovering over control
LLink<Control> lastControl;
LLink<Control> lockedControl;
bool  lockControl = false;
DWORD lastPress = GlobalTickCount();
float incAmount = 0.01;

void UpdateControlCheat()
{
  if(GInput.keys[DIK_LSHIFT]>0.0)
  {
    // timeout of 1/2 a second
    if(GlobalTickCount()-lastPress > 500)
    {
      lastPress = GlobalTickCount();
      if(lockControl)
      {
        lockControl = false;
        lockedControl = NULL;
      }
      else
      {
        lockControl = true;
        lockedControl = lastControl;
      }
    }
  }

  if(lockedControl)
  {
    float x,y,h,w;
    x = 0.0f; y = 0.0f;
    h = 0.0f; w = 0.0f;

    // increase incAmount
    if(GInput.keys[DIK_ADD]>0.0)      incAmount *= 10.0f;
    if(GInput.keys[DIK_SUBTRACT]>0.0) incAmount /= 10.0f;
    if(GInput.keys[DIK_NUMPAD8]>0.0)  y -= incAmount;
    if(GInput.keys[DIK_NUMPAD2]>0.0)  y += incAmount;
    if(GInput.keys[DIK_NUMPAD4]>0.0)  x -= incAmount;
    if(GInput.keys[DIK_NUMPAD6]>0.0)  x += incAmount;
    if(GInput.keys[DIK_NUMPAD7]>0.0)  h -= incAmount;
    if(GInput.keys[DIK_NUMPAD9]>0.0)  h += incAmount;
    if(GInput.keys[DIK_NUMPAD1]>0.0)  w -= incAmount;
    if(GInput.keys[DIK_NUMPAD3]>0.0)  w += incAmount;

    lockedControl->SetPos (lockedControl->X()+x, lockedControl->Y()+y, lockedControl->W()+w, lockedControl->H()+h);

    DIAG_MESSAGE(500,Format("ControlIDC Locked:%d",lockedControl->IDC()));
    DIAG_MESSAGE(500,Format("IncAmount:%f",incAmount));
    DIAG_MESSAGE(500,Format("X:%f",lockedControl->X()));
    DIAG_MESSAGE(500,Format("Y:%f",lockedControl->Y()));
    DIAG_MESSAGE(500,Format("W:%f",lockedControl->W()));
    DIAG_MESSAGE(500,Format("H:%f",lockedControl->H()));
  }
}
#endif

void ControlsContainer::OnSimulateBackground(EntityAI *vehicle)
{

}
/*!
\patch 5145 Date 3/23/2007 by Jirka
- Fixed: UI - during low frame rate, double click on some item in listbox was sometimes handled as double click on different item
*/

void ControlsContainer::OnSimulate(EntityAI *vehicle)
{
  //disable some input for game simulation
  (void) GInput.GetActionWithCurveExclusive(InputInterface::Curve1Mouse, UAAimRight, UAAimLeft);
  (void) GInput.GetActionWithCurveExclusive(InputInterface::Curve1Mouse, UAAimUp, UAAimDown);
#if 0 // _ENABLE_PERFLOG
  BString<10> idd;
  sprintf(idd,"D%d",IDD());
  PerfCounter counter;
  counter.Init(idd,"ccSimulate",&GPerfProfilers,PROF_COUNT_SCALE);
  ScopeProfiler scopeProfL(counter, counter.TimingEnabled());
#endif

  bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT]; (void)shift;
  bool control = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL]; (void)control;
  bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU]; (void)alt;

#if _VBS3 && _ENABLE_CHEATS
  if(CHECK_DIAG(DEUIControls)) UpdateControlCheat();
#endif

  if (_exit < 0)
  {
    SortObjects();

    // Timer
    for (int i=0; i<_controlsBackground.Size(); i++)
      if (_controlsBackground[i]->GetTimer() <= Glob.uiTime)
      {
        _controlsBackground[i]->OnEvent(CETimer);
        _controlsBackground[i]->OnTimer();
      }
    for (int i=0; i<_objects.Size(); i++)
      if (_objects[i]->GetTimer() <= Glob.uiTime)
      {
        _objects[i]->OnEvent(CETimer);
        _objects[i]->OnTimer();
      }
    for (int i=0; i<_controlsForeground.Size(); i++)
      if (_controlsForeground[i]->GetTimer() <= Glob.uiTime)
      {
        _controlsForeground[i]->OnEvent(CETimer);
        _controlsForeground[i]->OnTimer();
      }


# if _ENABLE_CHEATS
    // when cheat mouse is used, ignore it for UI
    if (!GInput.keys[DIK_RWIN])
# endif
    {
      
      // mouse buttons
      // found control with mouse over
      float mouseX = 0.5 + GInput.cursorX * 0.5;
      float mouseY = 0.5 + GInput.cursorY * 0.5;
      ControlIdPersistent controlFound = FindCtrl(mouseX, mouseY);
      IControlRef ctrlFound = controlFound.GetCtrl(); // control may be destroyed during simulation

      // left mouse button - go through the whole history
      unsigned int downToDo = GInput.mouseButtonsToDo[0];
      unsigned int upToDo = GInput.mouseButtonsReleased[0];
      unsigned int cycles;
      if (GInput.GetMouseButton(0,true,2))
        cycles = downToDo - 1 < upToDo ? downToDo - 1 : upToDo; // downToDo need to be handled at last
      else
        cycles = downToDo < upToDo ? downToDo : upToDo;
      if (cycles < 0) cycles = 0 ;

      if (upToDo > cycles)
      {
        OnLButtonUp(ctrlFound, mouseX, mouseY, shift, control, alt);
        upToDo--;
      }
      for (unsigned int i=0; i<cycles; i++)
      {
        OnLButtonDown(ctrlFound, controlFound, mouseX, mouseY, shift, control, alt, downToDo == 1);
        downToDo--;
        OnLButtonUp(ctrlFound, mouseX, mouseY, shift, control, alt);
        upToDo--;
      }
      if (downToDo > 0)
      {
        Assert(GInput.mouseButtons[0]);
        OnLButtonDown(ctrlFound, controlFound, mouseX, mouseY, shift, control, alt, true);
        downToDo--;
      }
      Assert(downToDo == 0);
      Assert(upToDo == 0);

      if (_dblClkDetect && !_controlLCaptured.IsNull())
      {
        if
        (
          GInput._lastProcessMouse - GInput.mouseButtonsPressed[0] >= GInput.mouseDoubleClickTime ||
  #if _VBS2 // allow more mouse movement during double click
          fabs(mouseX - _dblClkX) > 0.02 ||
          fabs(mouseY - _dblClkY) > 0.02
  #else
          fabs(mouseX - _dblClkX) > 0.01 ||
          fabs(mouseY - _dblClkY) > 0.01
  #endif
        )
        {
          IControl *ctrl = _controlLCaptured.GetCtrl();
          if (ctrl->IsMouseEnabled())
          {
            ctrl->OnEvent(CEMouseButtonClick, 0, mouseX, mouseY, shift, control, alt);
            ctrl->OnLButtonClick(mouseX, mouseY);
          }
          _dblClkDetect = false;
          if (!GInput.mouseL) _controlLCaptured = ControlIdPersistent::Null();
        }
      }
      
      // right mouse button
      GInput.GetMouseButton(1,true,2);
      if (GInput.mouseButtonsToDo[1])
      {
        // dialog level event handler
        OnEvent(DEMouseButtonDown, 1, mouseX, mouseY, shift, control, alt);

        if (_controlRCaptured.IsNull() && ctrlFound)
        {
          if (ctrlFound->IsMouseEnabled())
          {
            ctrlFound->OnEvent(CEMouseButtonDown, 1, mouseX, mouseY, shift, control, alt);
            ctrlFound->OnRButtonDown(mouseX, mouseY);
            _controlRCaptured = controlFound;
            _rButtonClick = true;
          }
        }
      }

      if (GInput.mouseButtonsReleased[1])
      {
        // dialog level event handler
        OnEvent(DEMouseButtonUp, 1, mouseX, mouseY, shift, control, alt);

        IControl *ctrl = _controlRCaptured.GetCtrl();
        if (ctrl)
        {
          if (_rButtonClick)
          {
            ctrl->OnEvent(CEMouseButtonClick, 1, mouseX, mouseY, shift, control, alt);
            ctrl->OnRButtonClick(mouseX, mouseY);
          }
          ctrl->OnEvent(CEMouseButtonUp, 1, mouseX, mouseY, shift, control, alt);
          ctrl->OnRButtonUp(mouseX, mouseY);
          _controlRCaptured = ControlIdPersistent::Null();
        }
      }
      //Disable RMB input (it is used here)
      if (!_controlRCaptured.IsNull()) GInput.DisableKey(INPUT_DEVICE_MOUSE + 1);

      // mouse moving
      ControlIdPersistent control;
      if (!_controlLCaptured.IsNull())
      {
        control = _controlLCaptured;
      }
      else if (!_controlRCaptured.IsNull())
      {
        control = _controlRCaptured;
      }
      else if (ctrlFound && ctrlFound->IsMouseEnabled())
      {
        control = controlFound;
      }
      IControl *ctrl = control.GetCtrl();
      // ?? maybe after some distance
      float mouseSpeedX = GInput.mouseAxis[1] - GInput.mouseAxis[0];
      float mouseSpeedY = GInput.mouseAxis[3] - GInput.mouseAxis[2];
      if (_lastX != GInput.cursorX || _lastY != GInput.cursorY)
      {
        if (_moving)
        {
          Move(0.5 * (GInput.cursorX - _lastX), 0.5 * (GInput.cursorY - _lastY));
        }
        
        // dialog level event handler
        OnEvent(DEMouseMoving, mouseSpeedX, mouseSpeedY);

        IControl *ctrlMouseOver = _controlMouseOver.GetCtrl();
        if (ctrlMouseOver && ctrlMouseOver != ctrl)
        {
          ctrlMouseOver->OnEvent(CEMouseMoving, mouseX, mouseY, false);
          ctrlMouseOver->OnMouseMove(mouseX, mouseY, false);
        }
        if (ctrl)
        {
          ctrl->OnEvent(CEMouseMoving, mouseX, mouseY, true);
          ctrl->OnMouseMove(mouseX, mouseY);
        }
        if (CHECK_DIAG(DEUIControls))
        {
          char diagmsg[1024]; *diagmsg = 0;
          char ctrlPos[64]; *ctrlPos = 0;
          Control *theCtrl = NULL;
          if (ctrl && ctrl->IsInside(mouseX, mouseY))
          {
            sprintf(diagmsg, "x=%.2f y=%.2f Dlg=\"%s\" IDC=%d cls=\"%s\"", mouseX, mouseY, cc_cast(_className), ctrl->IDC(), cc_cast(ctrl->GetCtrlClassName()));
            theCtrl = dynamic_cast<Control *>(ctrl);
            if (theCtrl)
            {
              sprintf(ctrlPos, "Control position: x=%.2f y=%.2f w=%.2f h=%.2f", theCtrl->X(), theCtrl->Y(), theCtrl->W(), theCtrl->H());
            }
          }
          else
          {
            bool ctrlFound = false;
            for (int i=0; i<_controlsForeground.Size(); i++)
            {
              if (_controlsForeground[i] && _controlsForeground[i]->IsInside(mouseX,mouseY))
              {
                theCtrl = _controlsForeground[i];
                sprintf(ctrlPos, "Control position: x=%.2f y=%.2f w=%.2f h=%.2f", theCtrl->X(), theCtrl->Y(), theCtrl->W(), theCtrl->H());
                sprintf(diagmsg, "x=%.2f y=%.2f Dlg=\"%s\" IDC=%d cls=\"%s\"", mouseX, mouseY, cc_cast(_className), theCtrl->IDC(), cc_cast(theCtrl->GetCtrlClassName()));
                ctrlFound = true;
                break;
              }
            }
            if (!ctrlFound)
            {
              for (int i=0; i<_controlsBackground.Size(); i++)
              {
                if (_controlsBackground[i] && _controlsBackground[i]->IsInside(mouseX,mouseY))
                {
                  theCtrl = _controlsBackground[i];
                  sprintf(ctrlPos, "Control position: x=%.2f y=%.2f w=%.2f h=%.2f", theCtrl->X(), theCtrl->Y(), theCtrl->W(), theCtrl->H());
                  sprintf(diagmsg, "x=%.2f y=%.2f Dlg=\"%s\" IDC=%d cls=\"%s\"", mouseX, mouseY, cc_cast(_className), theCtrl->IDC(), cc_cast(theCtrl->GetCtrlClassName()));
                  ctrlFound = true;
                  break;
                }
              }
            }

            if (!ctrlFound)
              sprintf(diagmsg, "x=%.2f y=%.2f Dlg=\"%s\"", mouseX, mouseY, cc_cast(_className));
          }
#if _VBS3 //for control cheat
          // remember the last control highlighted
          lastControl = theCtrl;
#endif
          char texture[128]; *texture = 0;
          if (theCtrl)
          {
            CStatic *theStatic = dynamic_cast<CStatic *>(theCtrl);
            if (theStatic)
            {
              if (theStatic->GetTexture())
              {
                sprintf(texture, "Texture: \"%s\"", theStatic->GetTexture()->Name());
              }
            }
          }
          DIAG_MESSAGE(5000, diagmsg);
          DIAG_MESSAGE(5000, ctrlPos);
          DIAG_MESSAGE(5000, texture);
        }

        _rButtonClick = false; // right button click canceled

        if (Glob.uiTime < _tooltipTime) 
        {
          // tooltip not shown - show tooltip when mouse cursor is still
          _tooltipTime = Glob.uiTime + tooltipDelay;
          _tooltipArea = 0;
        }
      }
      else
      {
        // dialog level event handler
        OnEvent(DEMouseHolding, mouseSpeedX, mouseSpeedY);

        IControl *ctrlMouseOver = _controlMouseOver.GetCtrl();
        if (ctrlMouseOver && ctrlMouseOver != ctrl)
        {
          ctrlMouseOver->OnEvent(CEMouseMoving, mouseX, mouseY, false);
          ctrlMouseOver->OnMouseMove(mouseX, mouseY, false);
        }
        if (ctrl)
        {
          ctrl->OnEvent(CEMouseHolding, mouseX, mouseY, true);
          ctrl->OnMouseHold(mouseX, mouseY);
        }
      }

      if (GInput.mouseButtons[0] > 0 || !ctrl || control != _controlMouseOver)
      {
        // hide tooltip
        _tooltipTime = Glob.uiTime + tooltipDelay;
        _tooltipArea = 0;
      }

      if (Glob.uiTime >= _tooltipTime) // mouse cursor was still long enough
      {
        // check if area in the control is still the same
        int area = ctrl->GetTooltipArea(mouseX, mouseY);
        if (_tooltipArea == 0 && area != 0)
        {
          // show the tooltip now
          _tooltipArea = area;
          _tooltipX = mouseX;
          _tooltipY = mouseY;
        }
        else if (area == 0 || area != _tooltipArea)
        {
          // hide cursor when current area is left
          _tooltipTime = Glob.uiTime + tooltipDelay;
          _tooltipArea = 0;
        }
      }

      _controlMouseOver = control;

      if (GInput.cursorMovedZ != 0)
      {
        // dialog level event handler
        OnEvent(DEMouseZChanged, GInput.cursorMovedZ);

        IControl *ctrlMouseOver = _controlMouseOver.GetCtrl();
        if (ctrlMouseOver)
        {
          ctrlMouseOver->OnEvent(CEMouseZChanged, GInput.cursorMovedZ);
          ctrlMouseOver->OnMouseZChanged(GInput.cursorMovedZ);
          GInput.cursorMovedZ = 0;
        }
      }

      _lastX = GInput.cursorX;
      _lastY = GInput.cursorY; 
    }
  }
}

void ControlsContainer::OnControllerRemoved()
{
  GWorld->OnControllerRemoved();
}

static int dikCodeShortcutFirst[] = {INPUT_DEVICE_XINPUT + XBOX_Start};

/*!
\patch 5129 Date 2/19/2007 by Jirka
- Fixed: UI - disabled default button responded to Enter
*/

bool ControlsContainer::OnKeyDown(int dikCode)
{
  bool ok = false;
  bool enableCtrlProcessing = EnableDefaultKeysProcessing();
  bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
  bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL]; (void)ctrl;
  bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU]; (void)alt;

  // check whether shortcut or regular reaction will be handled first
  bool shortcutFirst = false;
  for (int i=0; i<lenof(dikCodeShortcutFirst); i++)
  {
    if (dikCode == dikCodeShortcutFirst[i])
    {
      shortcutFirst = true;
      break;
    }
  }

  IControl *focused = _controlFocused.GetCtrl();

  if (!shortcutFirst && enableCtrlProcessing)
  {
    // offer to focused control
    if (focused)
    {
      // offer the event to the focused control
      ok = focused->OnCheckEvent(CEKeyDown, dikCode, shift, ctrl, alt);
      if(!ok) ok = focused->OnKeyDown(dikCode);
    }
  }

  // handle as shortcut
  if (!ok)
  {
    int offset = (ctrl ? 512 : 0 ) + (shift ? 1024 : 0) + (alt ? 2048 : 0);
    IControl *ictrl = GetShortcut(dikCode+offset);
    if (ictrl)
    {
      // TODO: event handler?
      ok = ictrl->OnShortcutDown();
    }
  }

  if (!ok && shortcutFirst && enableCtrlProcessing)
  {
    // offer to focused control
    if (focused)
    {
      // offer the event to the focused control
      focused->OnEvent(CEKeyDown, dikCode, shift, ctrl, alt);

      ok = focused->OnKeyDown(dikCode);
    }
  }

  if (ok)
  {
    // button sounds
    switch (dikCode)
    {
    case INPUT_DEVICE_XINPUT + XBOX_RightBumper:
      IControl::PlaySound(_soundWhiteProcessed);
      break;
    case INPUT_DEVICE_XINPUT + XBOX_LeftBumper:
      IControl::PlaySound(_soundBlackProcessed);
      break;
    case INPUT_DEVICE_XINPUT + XBOX_X:
      IControl::PlaySound(_soundXProcessed);
      break;
    case INPUT_DEVICE_XINPUT + XBOX_Y:
      IControl::PlaySound(_soundYProcessed);
      break;
    case INPUT_DEVICE_XINPUT + XBOX_Start:
      IControl::PlaySound(_soundStartProcessed);
      break;
    case INPUT_DEVICE_XINPUT + XBOX_LeftTrigger:
      IControl::PlaySound(_soundLeftTriggerProcessed);
      break;
    case INPUT_DEVICE_XINPUT + XBOX_RightTrigger:
      IControl::PlaySound(_soundRightTriggerProcessed);
      break;
    case INPUT_DEVICE_XINPUT + XBOX_Left:
    case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXLeft:
      IControl::PlaySound(_soundLeft);
      break;
    case INPUT_DEVICE_XINPUT + XBOX_Right:
    case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXRight:
      IControl::PlaySound(_soundRight);
      break;
    case INPUT_DEVICE_XINPUT + XBOX_Up:
    case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYUp:
      IControl::PlaySound(_soundUp);
      break;
    case INPUT_DEVICE_XINPUT + XBOX_Down:
    case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYDown:
      IControl::PlaySound(_soundDown);
      break;
    }
  }
  else if (enableCtrlProcessing)
  {
    switch (dikCode)
    {
    case DIK_TAB:
      if (!alt)
      {
        IControl::PlaySound(_soundChangeFocus);
        if (shift)
          PrevCtrl();
        else
          NextCtrl();
        ok = true;
      }
      break;
    case DIK_UP:
    case INPUT_DEVICE_XINPUT + XBOX_Up:
    case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYUp:
      if (PrevLineCtrl())
      {
        IControl::PlaySound(_soundChangeFocus);
        ok = true;
      }
      else
      {
        IControl::PlaySound(_soundChangeFocusFailed);
      }
      break;
    case DIK_LEFT:
    case DIK_PRIOR:
    case INPUT_DEVICE_XINPUT + XBOX_Left:
    case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXLeft:
      if (PrevCtrl())
      {
        IControl::PlaySound(_soundChangeFocus);
        ok = true;
      }
      else
      {
        IControl::PlaySound(_soundChangeFocusFailed);
      }
      break;
    case DIK_DOWN:
    case INPUT_DEVICE_XINPUT + XBOX_Down:
    case INPUT_DEVICE_XINPUT + XBOX_LeftThumbYDown:
      if (NextLineCtrl())
      {
        IControl::PlaySound(_soundChangeFocus);
        ok = true;
      }
      else
      {
        IControl::PlaySound(_soundChangeFocusFailed);
      }
      break;
    case DIK_RIGHT:
    case DIK_NEXT:
    case INPUT_DEVICE_XINPUT + XBOX_Right:
    case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXRight:
      if (NextCtrl())
      {
        IControl::PlaySound(_soundChangeFocus);
        ok = true;
      }
      else
      {
        IControl::PlaySound(_soundChangeFocusFailed);
      }
      break;
    case DIK_SPACE:
    case DIK_RETURN:
    case DIK_NUMPADENTER:
    case INPUT_DEVICE_XINPUT + XBOX_Start:
    case INPUT_DEVICE_XINPUT + XBOX_A:
      {
        IControl *control = GetDefault();
        if (control && control->IsVisible() && control->IsEnabled())
        {
          control->OnEvent(CEKeyDown, dikCode, shift, ctrl, alt);
          ok = control->OnKeyDown(dikCode);
        }
      }
      break;
    case DIK_ESCAPE:
    case INPUT_DEVICE_XINPUT + XBOX_Back:
    case INPUT_DEVICE_XINPUT + XBOX_B:
      IControl::PlaySound(_soundCancel);
      OnButtonClicked(IDC_CANCEL);
      ok = true;
      break;
    }
  }
  return ok;
}

/*!
\patch 5115 Date 1/5/2007 by Jirka
- Fixed: UI - Enter no longer generate OK event when OK button not present
*/

bool ControlsContainer::OnKeyUp(int dikCode)
{
  bool ok = false;
  bool enableCtrlProcessing = EnableDefaultKeysProcessing();
  bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT]; (void)shift;
  bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL]; (void)ctrl;
  bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU]; (void)alt;

  // check whether shortcut or regular reaction will be handled first
  bool shortcutFirst = false;
  for (int i=0; i<lenof(dikCodeShortcutFirst); i++)
  {
    if (dikCode == dikCodeShortcutFirst[i])
    {
      shortcutFirst = true;
      break;
    }
  }

  IControl *focused = _controlFocused.GetCtrl();

  if (!shortcutFirst && enableCtrlProcessing)
  {
    // offer to focused control
    if (focused)
    {
      // offer the event to the focused control
      focused->OnEvent(CEKeyUp, dikCode, shift, ctrl, alt);

      ok = focused->OnKeyUp(dikCode);
    }
  }

  // handle as shortcut
  if (!ok)
  {
    int offset = (ctrl ? 512 : 0 ) + (shift ? 1024 : 0) + (alt ? 2048 : 0);
    IControl *ictrl = GetShortcut(dikCode+offset);
    if (ictrl)
    {
      // TODO: event handler?
      ok = ictrl->OnShortcutUp();
    }
  }

  if (!ok && shortcutFirst && enableCtrlProcessing)
  {
    // offer to focused control
    if (focused)
    {
      // offer the event to the focused control
      focused->OnEvent(CEKeyUp, dikCode, shift, ctrl, alt);

      ok = focused->OnKeyUp(dikCode);
    }
  }

  if (!ok && enableCtrlProcessing)
  {
    switch (dikCode)
    {
    case DIK_SPACE:
    case DIK_RETURN:
    case DIK_NUMPADENTER:
    case INPUT_DEVICE_XINPUT + XBOX_Start:
    case INPUT_DEVICE_XINPUT + XBOX_A:
      {
        IControl *control = GetDefault();
        if (control && control->IsVisible() && control->IsEnabled())
        {
          control->OnEvent(CEKeyUp, dikCode, shift, ctrl, alt);
          ok = control->OnKeyUp(dikCode);
        }
      }
      break;
    }
  }

  return ok;
}

bool ControlsContainer::OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  IControl *focused = _controlFocused.GetCtrl();
  if (focused)
  {
    focused->OnEvent(CEChar, nChar);
    return focused->OnChar(nChar, nRepCnt, nFlags);
  }
  return false;
}

bool ControlsContainer::OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  IControl *focused = _controlFocused.GetCtrl();
  if (focused)
  {
    focused->OnEvent(CEIMEChar, nChar);
    return focused->OnIMEChar(nChar, nRepCnt, nFlags);
  }
  return false;
}

bool ControlsContainer::OnIMEComposition(unsigned nChar, unsigned nFlags)
{
  IControl *focused = _controlFocused.GetCtrl();
  if (focused)
  {
    focused->OnEvent(CEIMEComposition, nChar);
    return focused->OnIMEComposition(nChar, nFlags);
  }
  return false;
}

void ControlsContainer::OnButtonClicked(int idc)
{
  switch (idc)
  {
    case IDC_OK:
      Exit(IDC_OK);
      break;
    case IDC_CANCEL:
      Exit(IDC_CANCEL);
      break;
  }
}

bool ControlsContainer::NextCtrl()
{
  int n = _controlsBackground.Size() + _objects.Size() + _controlsForeground.Size();
  if (n == 0) return false;

  ControlId id = FindCtrl(_controlFocused);
  for (int i=0; i<n; i++)
  {
    id = Next(id);
    IControl *ctrl = Ctrl(id);
    if (ctrl->IsVisible() && ctrl->IsEnabled())
    {
      return SetFocus(id, CFANext);
    }
  }
  ControlId ci = ControlId::Null();
  SetFocus(ci, CFANext);
  return false;
}

bool ControlsContainer::PrevCtrl()
{
  int n = _controlsBackground.Size() + _objects.Size() + _controlsForeground.Size();
  if (n == 0) return false;

  ControlId id = FindCtrl(_controlFocused);
  for (int i=0; i<n; i++)
  {
    id = Prev(id);
    IControl *ctrl = Ctrl(id);
    if (ctrl->IsVisible() && ctrl->IsEnabled())
    {
      return SetFocus(id, CFAPrev);
    }
  }
  ControlId ci = ControlId::Null();
  SetFocus(ci, CFAPrev);
  return false;
}

bool ControlsContainer::NextLineCtrl()
{
  RefArray<Control> *list = NULL;

  ControlId index = FindCtrl(_controlFocused);
  if (index.list == CLBackground) list = &_controlsBackground;
  else if (index.list == CLForeground) list = &_controlsForeground;
  else if (index.list == CLObjects)
  {
    // TODO: let object process NextLineCtrl
  }

  if (list && index.id >= 0 && index.id < list->Size())
  {
    int n = list->Size();
    int iCurrent = index.id;
    Control *current = (*list)[iCurrent];
    Assert(current);
    for (int i=iCurrent+1; true; i++)
    {
      if (i >= n) i = 0; // cyclic list
      if (i == iCurrent) break;
      Control *ctrl = (*list)[i];
      if (ctrl->GetType() == CT_LINEBREAK)
      {
        // next line found
        int found = -1;
        float error = FLT_MAX;
        float xBase = current->X() + 0.5 * current->W();
        for (int j=i+1; j!=iCurrent; j++)
        {
          if (j >= n) j = 0; // cyclic list
          Control *ctrl = (*list)[j];
          if (ctrl->GetType() == CT_LINEBREAK) break;
          if (ctrl->IsVisible() && ctrl->IsEnabled())
          {
            // candidate found
            // float e = fabs(ctrl->X() + 0.5 * ctrl->W() - xBase);
            float e = 0;
            if (xBase < ctrl->X()) e = ctrl->X() - xBase;
            else if (xBase > ctrl->X() + ctrl->W()) e = xBase - ctrl->X() - ctrl->W();
            if (e < error)
            {
              found = j;
              error = e;
            }
          }
        }
        if (found >= 0)
        {
          return SetFocus(ControlId(index.list, found), CFANext, true);
        }
        break;
      }
    }
  }
  // not found - select next
  return NextCtrl(); 
}

bool ControlsContainer::PrevLineCtrl()
{
  RefArray<Control> *list = NULL;

  ControlId index = FindCtrl(_controlFocused);
  if (index.list == CLBackground) list = &_controlsBackground;
  else if (index.list == CLForeground) list = &_controlsForeground;
  else if (index.list == CLObjects)
  {
    // TODO: let object process NextLineDefaultCtrl
  }

  if (list && index.id >= 0 && index.id < list->Size())
  {
    int n = list->Size();
    int iCurrent = index.id;
    Control *current = (*list)[iCurrent];
    Assert(current);
    for (int i=iCurrent-1; true; i--)
    {
      if (i < 0) i = n - 1; // cyclic list
      if (i == iCurrent) break;
      Control *ctrl = (*list)[i];
      if (ctrl->GetType() == CT_LINEBREAK)
      {
        // next line found
        int found = -1;
        float error = FLT_MAX;
        float xBase = current->X() + 0.5 * current->W();
        for (int j=i-1; j!=iCurrent; j--)
        {
          if (j < 0) j = n - 1; // cyclic list
          Control *ctrl = (*list)[j];
          if (ctrl->GetType() == CT_LINEBREAK) break;
          if (ctrl->IsVisible() && ctrl->IsEnabled())
          {
            // candidate found
            // float e = fabs(ctrl->X() + 0.5 * ctrl->W() - xBase);
            float e = 0;
            if (xBase < ctrl->X()) e = ctrl->X() - xBase;
            else if (xBase > ctrl->X() + ctrl->W()) e = xBase - ctrl->X() - ctrl->W();
            if (e < error)
            {
              found = j;
              error = e;
            }
          }
        }
        if (found >= 0)
        {
          return SetFocus(ControlId(index.list, found), CFAPrev, true);
        }
        break;
      }
    }
  }
  // not found - select next
  return PrevCtrl(); 
}

ControlObject *ControlsContainer::OnCreateObject(int type, int idc, ParamEntryPar cls)
{
  switch (type)
  {
  case CT_OBJECT:
    return new ControlObject(this, idc, cls);
  case CT_OBJECT_ZOOM:
    return new ControlObjectWithZoom(this, idc, cls, true);
  case CT_OBJECT_CONTAINER:
    return new ControlObjectContainer(this, idc, cls);
  case CT_OBJECT_CONT_ANIM:
    return new ControlObjectContainerAnim(this, idc, cls);
  case CT_OBJECTS_GROUP:
    return new CObjectsGroup(this, idc, cls);
  }
  return NULL;
}

int CmpControlObjects
(
  const Ref<ControlObject> *pObj1,
  const Ref<ControlObject> *pObj2
)
{
  ControlObject *obj1 = *pObj1;
  ControlObject *obj2 = *pObj2;
  float diff = obj2->GetBasePosition().Z() - obj1->GetBasePosition().Z();
  if (diff != 0) return sign(diff);
  return (int)obj1 - (int)obj2;
}

void ControlsContainer::SortObjects()
{
  QSort(_objects.Data(), _objects.Size(), CmpControlObjects);
}

void ControlsContainer::ControlRemoved(const IControl *ctrl)
{
  if (_controlFocused.GetCtrl() == ctrl)
  {
    _controlFocused = ControlIdPersistent::Null();
    NextCtrl();
  }
  if (_controlLCaptured.GetCtrl() == ctrl) _controlLCaptured = ControlIdPersistent::Null();
  if (_controlRCaptured.GetCtrl() == ctrl) _controlRCaptured = ControlIdPersistent::Null();
  if (_controlDefault.GetCtrl() == ctrl) _controlDefault = ControlIdPersistent::Null();
  if (_controlMouseOver.GetCtrl() == ctrl) _controlMouseOver = ControlIdPersistent::Null();
}

#include "contextMenu.hpp"

Menu *ControlsContainer::FindContextMenu() const
{
  for (int i=0; i<_controlsForeground.Size(); i++)
  {
    Control *ctrl = _controlsForeground[i];
    if (ctrl->GetType() == CT_CONTEXT_MENU)
    {
      return &static_cast<CContextMenu *>(ctrl)->GetMenu();
    }
  }
  return nullptr;
}

void ControlsContainer::ContextMenuUpdated()
{
  for (int i=0; i<_controlsForeground.Size(); i++)
  {
    Control *ctrl = _controlsForeground[i];
    if (ctrl->GetType() == CT_CONTEXT_MENU)
    {
      static_cast<CContextMenu *>(ctrl)->SelectPlacement(ctrl->X(), ctrl->Y());
      return;
    }
  }
}

void ControlsContainer::RemoveContextMenu()
{
  int index = -1;
  for (int i=0; i<_controlsForeground.Size(); i++)
  {
    if (_controlsForeground[i]->GetType() == CT_CONTEXT_MENU)
    {
      index = i;
      break;
    }
  }
  if (index < 0) return;
  ControlRemoved(_controlsForeground[index]);
  _controlsForeground.Delete(index);
}

bool ControlsContainer::CreateContextMenu(float x, float y, int idc, ParamEntryPar cls, CCMContext &ctx)
{
  RemoveContextMenu();

  Ref<CContextMenu> ctrl = new CContextMenu(this, idc, cls);
  if (OnMenuCreated(ctrl->GetMenu(), x, y, idc, ctx))
  {
    ctrl->SelectPlacement(x, y);
    _controlsForeground.Insert(0, ctrl.GetRef());
    ControlId id(ControlId(CLForeground, 0));
    SetFocus(id, CFANext);
    return true;
  }
  return false;
}

void ControlsContainer::SetEventHandler(RString name, RString value)
{
  DisplayEvent event = GetEnumValue<DisplayEvent>((const char *)name);
  if (event != INT_MIN)
  {
    _eventHandlers[event].Clear();
    if (value.GetLength() > 0) _eventHandlers[event].Add(value);
  }
}

int ControlsContainer::AddEventHandler(RString name, RString value)
{
  DisplayEvent event = GetEnumValue<DisplayEvent>((const char *)name);
  if (event == INT_MIN) return -1;
  return _eventHandlers[event].Add(value);
}

void ControlsContainer::RemoveEventHandler(RString name, int id)
{
  DisplayEvent event = GetEnumValue<DisplayEvent>((const char *)name);
  if (event != INT_MIN) _eventHandlers[event].Delete(id);
}

void ControlsContainer::RemoveAllEventHandlers(RString name)
{
  DisplayEvent event = GetEnumValue<DisplayEvent>((const char *)name);
  if (event != INT_MIN) _eventHandlers[event].Clear();
}


void ControlsContainer::OnEvent(DisplayEvent event)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  handlers.Process(value);
}

void ControlsContainer::OnEvent(DisplayEvent event, float par)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  arguments.Add(GameValue(par));
  handlers.Process(value);
}

void ControlsContainer::OnEvent(DisplayEvent event, ControlsContainer *par1, float par2)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  arguments.Add(GameValueExt(par1));
  arguments.Add(GameValue(par2));
  handlers.Process(value);
}

bool ControlsContainer::OnEvent(DisplayEvent event, float par1, bool par2, bool par3, bool par4)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return false;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  arguments.Add(GameValue(par1));
  arguments.Add(GameValue(par2));
  arguments.Add(GameValue(par3));
  arguments.Add(GameValue(par4));
  return handlers.ProcessBool(value);
}

void ControlsContainer::OnEvent(DisplayEvent event, float par1, float par2)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  arguments.Add(GameValue(par1));
  arguments.Add(GameValue(par2));
  handlers.Process(value);
}

void ControlsContainer::OnEvent(DisplayEvent event, float par1, float par2, float par3, bool par4, bool par5, bool par6)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  arguments.Add(GameValue(par1));
  arguments.Add(GameValue(par2));
  arguments.Add(GameValue(par3));
  arguments.Add(GameValue(par4));
  arguments.Add(GameValue(par5));
  arguments.Add(GameValue(par6));
  handlers.Process(value);
}

ControlId ControlsContainer::Next(ControlId &id)
{
  switch (id.list)
  {
  case CLNone:
    if (_controlsBackground.Size() > 0)
      return ControlId(CLBackground, 0);
    if (_objects.Size() > 0)
      return ControlId(CLObjects, 0);
    if (_controlsForeground.Size() > 0)
      return ControlId(CLForeground, 0);
    return ControlId::Null();
  case CLBackground:
    if (id.id + 1 < _controlsBackground.Size())
      return ControlId(CLBackground, id.id + 1);
    if (_objects.Size() > 0)
      return ControlId(CLObjects, 0);
    if (_controlsForeground.Size() > 0)
      return ControlId(CLForeground, 0);
    if (_controlsBackground.Size() > 0)
      return ControlId(CLBackground, 0);
    return ControlId::Null();
  case CLObjects:
    if (id.id + 1 < _objects.Size())
      return ControlId(CLObjects, id.id + 1);
    if (_controlsForeground.Size() > 0)
      return ControlId(CLForeground, 0);
    if (_controlsBackground.Size() > 0)
      return ControlId(CLBackground, 0);
    if (_objects.Size() > 0)
      return ControlId(CLObjects, 0);
    return ControlId::Null();
  case CLForeground:
    if (id.id + 1 < _controlsForeground.Size())
      return ControlId(CLForeground, id.id + 1);
    if (_controlsBackground.Size() > 0)
      return ControlId(CLBackground, 0);
    if (_objects.Size() > 0)
      return ControlId(CLObjects, 0);
    if (_controlsForeground.Size() > 0)
      return ControlId(CLForeground, 0);
    return ControlId::Null();
  }
  return ControlId::Null();
}

ControlId ControlsContainer::Prev(ControlId &id)
{
  switch (id.list)
  {
  case CLNone:
    if (_controlsForeground.Size() > 0)
      return ControlId(CLForeground, _controlsForeground.Size() - 1);
    if (_objects.Size() > 0)
      return ControlId(CLObjects, _objects.Size() - 1);
    if (_controlsBackground.Size() > 0)
      return ControlId(CLBackground, _controlsBackground.Size() - 1);
    return ControlId::Null();
  case CLBackground:
    if (id.id > 0)
      return ControlId(CLBackground, id.id - 1);
    if (_controlsForeground.Size() > 0)
      return ControlId(CLForeground, _controlsForeground.Size() - 1);
    if (_objects.Size() > 0)
      return ControlId(CLObjects, _objects.Size() - 1);
    if (_controlsBackground.Size() > 0)
      return ControlId(CLBackground, _controlsBackground.Size() - 1);
    return ControlId::Null();
  case CLObjects:
    if (id.id > 0)
      return ControlId(CLObjects, id.id - 1);
    if (_controlsBackground.Size() > 0)
      return ControlId(CLBackground, _controlsBackground.Size() - 1);
    if (_controlsForeground.Size() > 0)
      return ControlId(CLForeground, _controlsForeground.Size() - 1);
    if (_objects.Size() > 0)
      return ControlId(CLObjects, _objects.Size() - 1);
    return ControlId::Null();
  case CLForeground:
    if (id.id > 0)
      return ControlId(CLForeground, id.id - 1);
    if (_objects.Size() > 0)
      return ControlId(CLObjects, _objects.Size() - 1);
    if (_controlsBackground.Size() > 0)
      return ControlId(CLBackground, _controlsBackground.Size() - 1);
    if (_controlsForeground.Size() > 0)
      return ControlId(CLForeground, _controlsForeground.Size() - 1);
    return ControlId::Null();
  }
  return ControlId::Null();
}

const IControl *ControlsContainer::Ctrl(const ControlId &id) const
{
  switch (id.list)
  {
  case CLBackground:
    return _controlsBackground[id.id];
  case CLObjects:
    return _objects[id.id];
  case CLForeground:
    return _controlsForeground[id.id];
  default:
    return NULL;
  }
}

IControl *ControlsContainer::Ctrl(const ControlId &id)
{
  switch (id.list)
  {
  case CLBackground:
    return _controlsBackground[id.id];
  case CLObjects:
    return _objects[id.id];
  case CLForeground:
    return _controlsForeground[id.id];
  default:
    return NULL;
  }
}

bool ControlsContainer::SetFocus(const ControlId &id, ControlFocusAction action, bool def)
{
  IControl *focused = GetFocused();
  IControl *ctrl = Ctrl(id);
  if (ctrl && ctrl->GetFocused() == focused) return true;

  if (ctrl && (!ctrl->IsVisible() || !ctrl->IsEnabled()))
    return false;

  if (focused)
  {
    if (!focused->OnKillFocus())
      return false;
  }

  switch (id.list)
  {
  case CLBackground:
    _controlFocused = ControlIdPersistent(id.list, _controlsBackground[id.id]);
    break;
  case CLObjects:
    _controlFocused = ControlIdPersistent(id.list, _objects[id.id]);
    break;
  case CLForeground:
    _controlFocused = ControlIdPersistent(id.list, _controlsForeground[id.id]);
    break;
  default:
    _controlFocused = ControlIdPersistent::Null();
    break;
  }

  if (ctrl)
  {
    ctrl->OnEvent(CESetFocus);
    ctrl->OnSetFocus(action, def);
    OnCtrlFocused(ctrl);
  }

  UpdateKeyHints();
  // need not check _objects
  return true;
}

void ControlsContainer::UpdateKeyHints()
{
  // change joystick hints
  IControl *ctrl = GetFocused();
  for (int i=0; i<_controlsBackground.Size(); i++)
  {
    IControl *c = _controlsBackground[i];
    if (!c || c->GetType() != CT_XKEYDESC) continue;
    CXKeyDesc *desc = static_cast<CXKeyDesc *>(c);
    int key = desc->GetKey();
    RString hint;
    bool structured = false;
    if (ctrl) hint = ctrl->GetKeyHint(key, structured);
    if (hint.GetLength() == 0)
    {
      structured = false;
      hint = GetKeyHint(key, structured);
    }
    desc->SetHint(hint, structured);
  }
  for (int i=0; i<_controlsForeground.Size(); i++)
  {
    IControl *c = _controlsForeground[i];
    if (!c || c->GetType() != CT_XKEYDESC) continue;
    CXKeyDesc *desc = static_cast<CXKeyDesc *>(c);
    int key = desc->GetKey();
    RString hint;
    bool structured = false;
    if (ctrl) hint = ctrl->GetKeyHint(key, structured);
    if (hint.GetLength() == 0)
    {
      structured = false;
      hint = GetKeyHint(key, structured);
    }
    desc->SetHint(hint, structured);
  }
}

bool IsAppPaused();

void ControlsContainer::DrawCursor()
{
  if (IsAppPaused()) return;
  
  #if _ENABLE_CHEATS
    // when cheats are using the cursor, do not set it here
    if (GInput.keys[DIK_RWIN]) return;
  #endif

  if (_dragHideCursor && _dragCtrl && _dragRows.Size() > 0) return;

  Texture *texture = GetCursorTexture();
  int shadow = GetCursorShadow();
  if (!texture) return;

  PackedColor color = GetCursorColor();
  float hsX = GetCursorX();
  float hsY = GetCursorY();

  const float mouseScrH = GetCursorH() / 600;
  const float mouseScrW = GetCursorW() / 800;
  float mouseScrX = GInput.cursorX * 0.5 + 0.5;
  float mouseScrY = GInput.cursorY * 0.5 + 0.5;
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  if (GEngine->IsCursorSupported())
  {
    int mx = toInt(mouseScrX*w);
    int my = toInt(mouseScrY*h);
    GEngine->SetCursor(texture, mx, my, hsX, hsY, color, shadow);
  }
  else // comment this else out if you want to see both SW and HW cursor
  {
    int mx = toInt((mouseScrX - hsX * mouseScrW) * w);
    int my = toInt((mouseScrY - hsY * mouseScrH) * h);
    int mw = toInt(mouseScrW * w);
    int mh = toInt(mouseScrH * h);
    MipInfo mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
    GEngine->Draw2D(mip, color, Rect2DPixel(mx, my, mw, mh), Rect2DClipPixel,shadow);
  }
}

RString ControlsContainer::GetKeyHint(int button, bool &structured) const
{
  for (int i=0; i<_keyHints.Size(); i++)
    if (_keyHints[i].key == button) return _keyHints[i].hint;
  return RString();
}

///////////////////////////////////////////////////////////////////////////////
// class Display

Display::Display( ControlsContainer *parent )
:ControlsContainer(parent)
{}

Display::~Display()
{
}

void Display::DrawHUD(EntityAI *vehicle, float alpha)
{
  ControlsContainer *ptr = this;
  while (ptr->Child())
  {
    if (ptr->AlwaysShow()) ptr->OnDraw(vehicle,alpha);
    ptr = ptr->Child();
  }
  
  ptr->OnDraw(vehicle,alpha);
  if (ptr->GetMsgBox())
  {
    ptr = ptr->GetMsgBox();
//    ptr->GetMsgBox()->OnDraw(vehicle,alpha);
    ptr->OnDraw(vehicle,alpha);
  }

  // draw cursor
  ptr->DrawCursor();
}

void Display::SimulateHUD(EntityAI *vehicle)
{
  //GetName()
  ControlsContainer *ptr = this;
  while (ptr->Child())
    ptr = ptr->Child();
  
  if (ptr->GetMsgBox())
  {
    ptr->GetMsgBox()->OnSimulate(vehicle);
    if (ptr->GetMsgBox()->CheckDestroy())
    {
      // FIX: allow create new message box inside OnChildDestroyed
      int idd = ptr->GetMsgBox()->IDD();
      int exit = ptr->GetMsgBox()->GetExitCode();
      ptr->DestroyMsgBox();
      ptr->OnEvent(DEChildDestroyed, ptr->GetMsgBox(), exit);
      ptr->OnChildDestroyed(idd, exit);
    }
  }
  else
  {
    ptr->OnSimulate(vehicle);
  }

  while (ptr && !ptr->GetMsgBox() && !ptr->Child() && ptr->CheckDestroy())
  {
    // FIX: allow destruction of multiple dialogs in single simulation step
    ControlsContainer *parent = ptr->Parent();
    if (parent)
    {
      int idd = ptr->IDD();
      int exit = ptr->GetExitCode();
      parent->OnEvent(DEChildDestroyed, ptr, exit);
      parent->OnChildDestroyed(idd, exit);
    }
    else
    {
      DestroyHUD(ptr->GetExitCode());
    }
    ptr = parent;
  }
}

bool Display::DoKeyDown(int dikCode)
{
  ControlsContainer *ptr = this;
  while (ptr->Child())
    ptr = ptr->Child();
  
  bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
  bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
  bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];

  if (ptr->GetMsgBox())
  {
    if (dikCode != VK_ESCAPE && ptr->GetMsgBox()->OnEvent(DEKeyDown, dikCode, shift, ctrl, alt)) return true;
    return ptr->GetMsgBox()->OnKeyDown(dikCode);
  }
  else
  {
    if (dikCode != VK_ESCAPE && ptr->OnEvent(DEKeyDown, dikCode, shift, ctrl, alt)) return true;
    return ptr->OnKeyDown(dikCode);
  }
}

bool Display::DoKeyUp(int dikCode)
{
  ControlsContainer *ptr = this;
  while (ptr->Child())
    ptr = ptr->Child();

  bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT];
  bool ctrl = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL];
  bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU];

  if (ptr->GetMsgBox())
  {
    if (dikCode != DIK_ESCAPE && ptr->GetMsgBox()->OnEvent(DEKeyUp, dikCode, shift, ctrl, alt)) return true;
    return ptr->GetMsgBox()->OnKeyUp(dikCode);
  }
  else
  {
    if (dikCode != DIK_ESCAPE && ptr->OnEvent(DEKeyUp, dikCode, shift, ctrl, alt)) return true;
    return ptr->OnKeyUp(dikCode);
  }
}

bool Display::DoChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  if (nChar < 32) return false;

  ControlsContainer *ptr = this;
  while (ptr->Child())
    ptr = ptr->Child();
  
  if (ptr->GetMsgBox())
  {
    ptr->GetMsgBox()->OnEvent(DEChar, nChar);
    return ptr->GetMsgBox()->OnChar(nChar, nRepCnt, nFlags);
  }
  else
  {
    ptr->OnEvent(DEChar, nChar);
    return ptr->OnChar(nChar, nRepCnt, nFlags);
  }
}

bool Display::DoIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  ControlsContainer *ptr = this;
  while (ptr->Child())
    ptr = ptr->Child();

  if (ptr->GetMsgBox())
    return ptr->GetMsgBox()->OnIMEChar(nChar, nRepCnt, nFlags);
  else
    return ptr->OnIMEChar(nChar, nRepCnt, nFlags);
}

bool Display::DoIMEComposition(unsigned nChar, unsigned nFlags)
{
  ControlsContainer *ptr = this;
  while (ptr->Child())
    ptr = ptr->Child();

  if (ptr->GetMsgBox())
    return ptr->GetMsgBox()->OnIMEComposition(nChar, nFlags);
  else
    return ptr->OnIMEComposition(nChar, nFlags);
}

bool Display::DoUnregisteredAddonUsed( RString addon )
{
  ControlsContainer *ptr = this;
  while (ptr)
  {
    bool done = ptr->OnUnregisteredAddonUsed(addon);
    if (done) return true;
    ptr = ptr->Child();
  }
  return false;
}

void Display::OnChildDestroyed(int idd, int exit)
{
  _child = NULL ;
}

int Display::GetTopID() const
{
  const ControlsContainer *ptr = this;
  while (ptr->Child() != NULL)
    ptr = ptr->Child();

  return ptr->IDD();
}

bool Display::IsSimulationEnabled() const
{
  const ControlsContainer *ptr = this;
  while (ptr->Child() != NULL)
    ptr = ptr->Child();

  if (ptr->GetMsgBox()) return false;
  return ptr->SimulationEnabled();
}

bool Display::IsDisplayEnabled() const
{
  const ControlsContainer *ptr = this;
  while (ptr->Child() != NULL)
    ptr = ptr->Child();

  return ptr->DisplayEnabled();
}

bool Display::IsUIEnabled() const
{
  const ControlsContainer *ptr = this;
  while (ptr->Child() != NULL)
    ptr = ptr->Child();

  return ptr->UIEnabled();
}

bool Display::IsDisplayReady() const
{
  return IsReady();
}

void Display::OnSimulate(EntityAI *vehicle)
{
  if (_progressDisplay) _progressDisplay->SetProgress(GlobalTickCount(), 0, 0);
  ControlsContainer::OnSimulate(vehicle);
}

void Display::OnDraw(EntityAI *vehicle, float alpha)
{
  ControlsContainer::OnDraw(vehicle, alpha);
  if (_progressDisplay && GlobalTickCount() >= _progressStartTime) _progressDisplay->DrawProgress(alpha);
}

void Display::StartProgress(RString msg, DWORD delay)
{
  IProgressDisplay *CreateDisplayProgress(RString text);
  _progressDisplay = CreateDisplayProgress(msg);
  _progressStartTime = GlobalTickCount() + delay;
}

void Display::FinishProgress()
{
  _progressDisplay = NULL;
}

#if _GAMES_FOR_WINDOWS
void Display::OnStorageDeviceRemoved()
{
  if (!GSaveSystem.IsStorageRemovalDialogShown())
  {
    if (IsInUserMissionWizard())
    {
      // we are somewhere in user mission editor
      if (GSaveSystem.IsStorageRemovalDialogDeclined())
      {
        // user declined storage device change - we exit
        Exit(IDC_CANCEL);
        return;
      }
    }
    else
    {
      GSaveSystem.StorageRemovedHandled();
    }

    if (GSaveSystem.IsStorageRemovalDialogDeclined())
    {
      // user declined storage device change
      GSaveSystem.StorageRemovedHandled();
      GSaveSystem.StorageRemovalDialogResetState();
    }
    else
    {
      // we show the dialog
      GSaveSystem.StorageRemovalDialogShown();
      MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(
        MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_MSG_STORAGE_REMOVED),
        IDD_MSG_XBOX_STORAGE_CHANGED, false, &buttonOK, &buttonCancel
        );
    }
  }
}

#endif //#if _GAMES_FOR_WINDOWS


TypeIsSimple(Control *)

///////////////////////////////////////////////////////////////////////////////
// class MsgBox

MsgBox::MsgBox(ControlsContainer *parent, int flags, RString text, int idd, bool structuredText, MsgBoxButton *infoOK, MsgBoxButton *infoCancel, MsgBoxButton *infoUser)
  : ControlsContainer(parent)
{
  _idd = idd;
  _flags = flags;
  _text = text;
  
  ParamEntryVal clsEntry = (flags & MB_BUTTON_USER) ?  Pars >> "RscMsgBox3" : Pars >> "RscMsgBox";

  LoadSounds(clsEntry);
  
  ConstParamEntryPtr entry;
  DISPLAY_EVENT_ENUM(DisplayEvent, DE, ENUM_LOAD)

  AUTO_STATIC_ARRAY(Control *, top, 32);
  AUTO_STATIC_ARRAY(Control *, middle, 32);
  AUTO_STATIC_ARRAY(Control *, bottom, 32);

  // Create controls, sort them by the position on screen
  ParamEntryVal array =  clsEntry >> "Middle";
  for (int i=0; i<array.GetEntryCount(); i++)
  {
    ParamEntryVal entry = array.GetEntry(i);
    if (!entry.IsClass()) continue;

    int type = entry >> "type";
    int idc = entry >> "idc";
    Control *ctrl = OnCreateCtrl(type, idc, entry);
    if (ctrl)
    {
      _controlsForeground.Add(ctrl);
      middle.Add(ctrl);
    }
  }
  array =  clsEntry >> "Top";
  for (int i=0; i<array.GetEntryCount(); i++)
  {
    ParamEntryVal entry = array.GetEntry(i);
    if (!entry.IsClass()) continue;

    int type = entry >> "type";
    int idc = entry >> "idc";
    Control *ctrl = OnCreateCtrl(type, idc, entry);
    if (ctrl)
    {
      _controlsForeground.Add(ctrl);
      top.Add(ctrl);
    }
  }
  array =  clsEntry >> "Bottom";
  for (int i=0; i<array.GetEntryCount(); i++)
  {
    ParamEntryVal entry = array.GetEntry(i);
    if (!entry.IsClass()) continue;

    int type = entry >> "type";
    int idc = entry >> "idc";
    Control *ctrl = OnCreateCtrl(type, idc, entry);
    if (ctrl)
    {
      _controlsForeground.Add(ctrl);
      bottom.Add(ctrl);
    }
  }

  // Height of the text
  float height = 0;
  CStructuredText *message = GetStructuredText(GetCtrl(IDC_MSG_BOX_MESSAGE));    
  if (message) 
  {
    if (structuredText)
      message->SetStructuredText(text);
    else
      message->SetRawText(text);
    height = message->GetTextHeight();
  }

  // Move / resize controls based on their position
  for (int i=0; i<top.Size(); i++)
  {
    Control *ctrl = top[i];
    ctrl->SetPos(ctrl->X(), ctrl->Y() - 0.5 * height, ctrl->W(), ctrl->H());
  }
  for (int i=0; i<middle.Size(); i++)
  {
    Control *ctrl = middle[i];
    ctrl->SetPos(ctrl->X(), ctrl->Y() - 0.5 * height, ctrl->W(), ctrl->H() + height);
  }
  for (int i=0; i<bottom.Size(); i++)
  {
    Control *ctrl = bottom[i];
    ctrl->SetPos(ctrl->X(), ctrl->Y() + 0.5 * height, ctrl->W(), ctrl->H());
  }

  // Buttons configuration
  IControl *button1 = GetCtrl(IDC_OK);
  IControl *button2 = GetCtrl(IDC_CANCEL);
  IControl *button3 = GetCtrl(IDC_USER_BUTTON);
  if (button1)
  {
    button1->ShowCtrl((flags & MB_BUTTON_OK) != 0);
    if (infoOK)
    {
      button1->SetShortcut(infoOK->_shortcut);
      ITextContainer *text = GetTextContainer(button1);
      if (text) text->SetText(infoOK->_text);
    }
  }
  if (button2)
  {
    button2->ShowCtrl((flags & MB_BUTTON_CANCEL) != 0);
    if (infoCancel)
    {
      button2->SetShortcut(infoCancel->_shortcut);
      ITextContainer *text = GetTextContainer(button2);
      if (text) text->SetText(infoCancel->_text);
    }
  }
  if (button3)
  {
    button3->ShowCtrl((flags & MB_BUTTON_USER) != 0);
    if (infoUser)
    {
      button3->SetShortcut(infoUser->_shortcut);
      ITextContainer *text = GetTextContainer(button3);
      if (text) text->SetText(infoUser->_text);
    }
  }
  // all prepared, call the event handler
  OnEvent(DELoad);

  NextCtrl();
}

void MsgBox::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_OK:
    if (_flags & MB_BUTTON_OK) Exit(IDC_OK);
    break;
  case IDC_CANCEL:
    if (_flags & MB_BUTTON_CANCEL) Exit(IDC_CANCEL);
    break;
  case IDC_USER_BUTTON:
    if (_flags & MB_BUTTON_USER) Exit(IDC_USER_BUTTON);
    break;
  }
}

bool MsgBox::OnKeyDown(int dikCode)
{
  // button sounds
  switch (dikCode)
  {
  case DIK_C:
  case DIK_INSERT:
    {
      if(GInput.keys[DIK_LCONTROL])
      {
        ExportToClipboard(_text,_text.GetLength());
        return true;
      }
    }
  }
  return ControlsContainer::OnKeyDown(dikCode);
}

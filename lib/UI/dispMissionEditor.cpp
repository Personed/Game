#include "../wpch.hpp"

#if _PROFILE && (USER_Ondra || USER_ondra)
#pragma optimize("",off)
#endif

#if _ENABLE_EDITOR2 && !_VBS2

#define USE_EDITOR_CAMERA 1

/*!
\patch 2.92 Date 03/08/2006 by Pete
- Added: functions addEditorObject, setObjectArguments, getObjectProxy
*/

#include "missionEditor.hpp"
#include "missionEditorCursor.hpp"
#include "dispMissionEditor.hpp"
#include "uiControls.hpp"
#include "contextMenu.hpp"
#include "missionDirs.hpp"
#include "resincl.hpp"
#include "uiMap.hpp"
#include "chat.hpp"
#include "../gameDirs.hpp"
#include "../world.hpp"
#include "../landscape.hpp"
#include "../camera.hpp"
#include "../cameraHold.hpp"
#include "../camEffects.hpp"
#include "../gameStateExt.hpp"
#include "../AI/ai.hpp"
#include "../stringtableExt.hpp"
#include "../paramArchiveExt.hpp"
#include "../dikCodes.h"
#include "../fileLocator.hpp"
#include <El/QStream/packFiles.hpp>

#include <El/ParamArchive/paramArchiveDb.hpp>
#include "../saveVersion.hpp"

#include <Es/Files/fileContainer.hpp>

#include <El/PreprocC/preprocC.hpp>
#include <El/Evaluator/express.hpp>

#include "../autoComplete.hpp"

#if _VBS2
#include "../hla/AAR.hpp"
#include "../detector.hpp"
#endif

#if _ENABLE_GAMESPY
  #include "../Network/winSockImpl.hpp"
  #include "../Network/network.hpp"
  #include <Es/Containers/bitmask.hpp>
  #include <Es/Common/win.h>
#endif
#include "displayUI.hpp"

#include "../AI/aiRadio.hpp"

RString GetVehicleIcon(RString name);

// mission editor events
#define EDITOR_EVENT_ENUM(type,prefix,XX) \
  XX(type, prefix, SelectObject) \
  XX(type, prefix, EditOverlay) \
  XX(type, prefix, CloseOverlay) \
  XX(type, prefix, RestartCamera) \
  XX(type, prefix, MouseOver)

#ifndef DECL_ENUM_EDITOR_EVENT
#define DECL_ENUM_EDITOR_EVENT
DECL_ENUM(EditorEvent)
#endif
DECLARE_ENUM(EditorEvent, ME, EDITOR_EVENT_ENUM)

DEFINE_ENUM(EditorEvent, ME, EDITOR_EVENT_ENUM)

//-!

static const EnumName EditorObjectScopeNames[]=
{
  EnumName(EOSHidden, "Hide"),
  EnumName(EOSView, "View"),
  EnumName(EOSSelect, "Select"),
  EnumName(EOSLinkTo, "LinkTo"),
  EnumName(EOSLinkFrom, "LinkFrom"),
  EnumName(EOSDrag, "AllNoDrag"),
  EnumName(EOSAllNoTree, "AllNoTree"),
  EnumName(EOSAllNoSelect, "AllNoSelect"),
  EnumName(EOSAllNoCopy, "AllNoCopy"),
  EnumName(EOSAll, "All"),
  EnumName()
};
template<>
const EnumName *GetEnumNames(EditorObjectScope dummy)
{
	return EditorObjectScopeNames;
}

enum EditorMode
{
  EMMap,
  EM3D,
  NEditorModes
};

static const EnumName EditorModeNames[]=
{
	EnumName(EMMap, "Map"),
  EnumName(EM3D, "3D"),
	EnumName()
};
template<>
const EnumName *GetEnumNames(EditorMode dummy)
{
	return EditorModeNames;
}

// dialog coords (0..1) to mouse coords (-1..1)
#define TO_MOUSE(pt) (pt - 0.5f) / 0.5f

RString GetUserDirectory();

GameValue CreateGameControl(Control *ctrl);
GameValue CreateGameControl(ControlObject *ctrl);

extern GameValue GMapOnSingleClick;
extern GameValue GMapOnSingleClickParams;

enum EditorCommands
{
  ECSetCameraPosition,
  ECLockCamera,
  ECSwitchToVehicle,
  ECEditObject,
  ECEditLink,
  ECDeleteObject,
  ECCenterMap,
  ECNewObject,
  ECExecuteScript,
  ECMenuFileLoad,
  ECMenuFileSave,
  ECMenuFileClear,
  ECMenuFileRestart,
  ECMenuFilePreview,
  ECMenuFileExit,
  ECMenuView2D,
  ECMenuView3D,
  ECMenuAdditionalItem
};

enum EditorMenuType
{
  EMNone = -1,
  EMPopup,
  EMFile,
  EMView,
  EMUser  // must always be last
};

// used with help text
enum EditorSelectedStatus
{
  ESNone,
  ESUnSelected,
  ESSelected
};

struct CCMContextEditor : public CCMContext
{
  int type;
  RString name;
  bool map;
  Vector3 position;

  CCMContextEditor(RString n, bool m, Vector3Par pos, int t = EMPopup) {name = n; map = m; position = pos; type = t;}
};

// String attribute
class AttrVector : public IAttribute
{
protected:
  Vector3 _value;

public:
  void Load(RString value);
  Vector3 GetValue() const {return _value;}
  void SetValue(Vector3Par value) {_value = value;}

  LSError Serialize(ParamArchive &ar);
  bool IsEqualTo(const IAttribute *with) const;
};

void AttrVector::Load(RString value)
{
  sscanf(value, "%g, %g, %g", &_value[0], &_value[1], &_value[2]);
}

LSError AttrVector::Serialize(ParamArchive &ar)
{
  CHECK(::Serialize(ar, "value", _value, 1));
  return LSOK;
}

bool AttrVector::IsEqualTo(const IAttribute *with) const
{
  const AttrVector *w = static_cast<const AttrVector *>(with);
  return GetValue() == w->GetValue();
}

IAttribute *CreateAttrVector() {return new AttrVector();}

AttributeTypes EditorMenuAttributeTypes;

#define ATTRIBUTES_LIST(XX) \
  XX("object", attrObject, CreateAttrString) \
  XX("argument", attrArgument, CreateAttrString) \
  XX("position", attrPosition, CreateAttrVector)

#define DEFINE_ATTR_VARIABLE(name, variable, func) static int variable;

ATTRIBUTES_LIST(DEFINE_ATTR_VARIABLE);

class DisplayMissionEditor;

enum LineType
{
  LTLine,
  LTArrow,
  NLineTypes
};

static const EnumName LineTypeNames[]=
{
	EnumName(LTLine, "Line"),
  EnumName(LTArrow, "Arrow"),
	EnumName()
};
template<>
const EnumName *GetEnumNames(LineType dummy)
{
	return LineTypeNames;
}

struct ObjectLink
{
  LLink<EditorObject> from;
  LLink<EditorObject> to;
  RString linkName;
  LineType lineType;
  PackedColor color;
  ObjectLink() {from = NULL; to = NULL; lineType = LTLine;}
};
TypeIsMovableZeroed(ObjectLink)

#if _VBS2
class CStaticMapEditor : public CStaticMapMain
#else
class CStaticMapEditor : public CStaticMap
#endif
{
protected:
  InitPtr<EditorWorkspace> _ws;
  InitPtr<GameVarSpace> _vars;    

  // dragging an editor object
  bool _dragging;

  // selecting multiple editor objects
  bool _selecting;

  // moving the map or camera by holding right mouse button
  bool _draggingRM;

  // current position
  Vector3 _dragPos;

  // starting position for multiple select
  Vector3 _startPos;

  // record time that mouse was first moved (for clearing labels)
  UITime _firstMove;

  UITime _lastObjectVisibilityUpdate;

  Ref<EditorObject> _linkObject;
  RString _linkArgument;
  int _linkShortcutKey;

  // speeds up drawing of links between objects
  AutoArray<ObjectLink> _objectLinks;

  /// icon of the 3D camera in the map
  Ref<Texture> _iconCamera;
  /// color of the 3D camera in the map
  PackedColor _iconCameraColor;
  /// size of the 3D camera in the map
  float _iconCameraSize;

public:
  CStaticMapEditor(
    ControlsContainer *parent, int idc, ParamEntryPar cls,
    float scaleMin, float scaleMax, float scaleDefault,
    EditorWorkspace *ws, GameVarSpace *vars);

  EditorWorkspace *GetWorkspace() {return _ws;}
  GameVarSpace *GetVariables() {return _vars;}

  virtual void OnLButtonDown(float x, float y);
  virtual void OnLButtonUp(float x, float y);
  virtual void OnRButtonDown(float x, float y);
  virtual void OnRButtonUp(float x, float y);
  virtual void OnRButtonClick(float x, float y);
  virtual void OnMouseMove(float x, float y, bool active);
  virtual void OnMouseHold(float x, float y, bool active);
  virtual void OnLButtonClick(float x, float y);
  virtual void OnLButtonDblClick(float x, float y);

  bool OnKeyDown(int dikCode) {return CStaticMap::OnKeyDown(dikCode);}

  virtual void OnDraw(UIViewport *vp, float alpha);
  virtual void DrawExt(float alpha);  

  virtual void Center(bool soft = false);
  virtual void DrawLabel(struct SignInfo &info, PackedColor color, int shadow);
  virtual SignInfo FindSign(float x, float y);

  GameValue GetObjectArgument(RString id, RString name);
  EditorObject *FindEditorObject(RString type, AutoArray<EditorArgument> &args);
  EditorObject *FindEditorObject(GameValue value,RString type = "");

  void EditLink(EditorObject *obj, RString name, float key = -1);
  
  // show hide legend
  void ShowLegend(bool showLegend) {_defaultProperties._showCountourInterval = showLegend;}

  // add an object to the mission editor
  RString AddObject(
    RString typeName, 
    const AutoArray<RString> &args, 
    bool create = true, 
    GameValue variable = NOTHING, 
    ConstParamEntryPtr subTypeConfig = NULL
  );  

  // show add new object dialog
  void ShowNewObject(RString type, RString className, TargetSide side, Vector3Par posn);
  void ShowEditObject(RString id);

  // return an object proxy corresponding to id
  Object *FindObject(RString id);

  // delete an object from the mission editor
  void DeleteObjectFromEditor(RString id);

  // return true if running in real time mode
  bool IsRealTime();

  // camera related
  Object *CameraGet();
  void CameraRestart();

  // update the proxy object for an editor object
  void UpdateProxy(Object *object, EditorObject *edObj);

  // delete the proxy object for an editor object
  void DeleteProxy(EditorObject *edObj);

  // set the onDoubleClick eventhandler for the parent
  void SetOnDoubleClick(RString eh);

  // set the onShowNewObject eventhandler for the parent
  void SetOnShowNewObject(RString eh);

  // select a single object
  void SelectObject(EditorObject *obj);

  void UpdateObjectBrowser();

  // position map and camera
  void LookAtPosition(Vector3Par pos);

  Ref<EditorObject> GetLinkObject() {return _linkObject;}

  void ProcessMouseOverEvent();

  // add menus and menu items to the editor
  int AddMenu(RString text, float priority);
  void CreateMenu(int index);
  int AddMenuItem(RString type, RString text, RString command, int index, float priority, bool assignPriority);
  void UpdateMenuItem(int index, RString text, RString command);  
  void RemoveMenuItem(int index, RString text = RString());
  int NMenuItems(RString menuName, int index);
  int GetNextMenuItemIndex();

  // overlays
  void NewOverlay(ConstParamEntryPtr entry);
  void LoadOverlay(ConstParamEntryPtr entry);
  void SaveOverlay();
  void ClearOverlay(bool commit = false, bool close = false);

  // set editor mode
  void SetMode(EditorMode mode);
  EditorMode GetMode();

  void Show3DIcons(bool show);
  bool IsShow3DIcons();

  void Allow3DMode(bool allow);

  void AllowFileOperations(bool allow);

  // set editor eventhandler
  void SetEditorEventHandler(RString name, RString value);

  // validate parent (ie is DisplayMissionEditor valid?)
  bool HasParent();

  bool IsDragging() {return _dragging;} 
  bool IsDraggingRM() {return _draggingRM;} 
  bool IsSelecting() {return _selecting;}

  void SetDrawLink(EditorObject *from, EditorObject *to, RString linkName, LineType lineType, PackedColor color);
  void RemoveDrawLinks(EditorObject *from, RString linkName = "");
  void RemoveAllDrawLinks(EditorObject *from);
  void ClearDrawLinks() {_objectLinks.Clear();}

#if _VBS2
  void ImportAllGroups();
#endif

protected:
  DisplayMissionEditor *GetParent();
};

class CEditorMenuButton : public CButton
{
public:
  CEditorMenuButton(ControlsContainer *parent, int idc, ParamEntryPar cls)
    : CButton(parent, idc, cls) {}
  virtual void OnLButtonDblClick(float x, float y) {return;}
};

class CObjectBrowser : public CTree
{
public:
  CObjectBrowser(ControlsContainer *parent, int idc, ParamEntryPar cls)
    : CTree(parent, idc, cls) {}
  virtual void OnRButtonClick(float x, float y);
};

// this class serves only to 'catch' key and mouse events in 3D mode
class CBackground3D : public CListBox
{
public:
  CBackground3D(ControlsContainer *parent, int idc, ParamEntryPar cls)
    : CListBox(parent, idc, cls) {}
  virtual void OnLButtonDown(float x, float y);
  virtual void OnLButtonUp(float x, float y);
  virtual void OnLButtonClick(float x, float y);
  virtual void OnLButtonDblClick(float x, float y);
  virtual void OnMouseMove(float x, float y, bool active);
  virtual void OnMouseHold(float x, float y, bool active);
  virtual void OnRButtonDown(float x, float y);
  virtual void OnRButtonUp(float x, float y);
  virtual void OnRButtonClick(float x, float y);

protected:
  DisplayMissionEditor *GetParent();
};

#if _VBS2
class CompassMissionEditor : public Compass
{
  bool _isRotating;

public:
  CompassMissionEditor(ControlsContainer *parent, int idc, ParamEntryPar cls);
  virtual void OnRButtonDown(float x, float y);
  void OnSimulate(float deltaT, Vector3 &compassDir);

protected:
  bool IsRotating() 
  {
    if (GInput.keys[DIK_LSHIFT] > 0 && GInput.mouseR)
      return _isRotating;
    return _isRotating = false;
  }
};
#endif

struct AdditionalMenuItem
{
  int type;
  RString text;
  RString command;
  int index;
  float priority;
};
TypeIsMovableZeroed(AdditionalMenuItem)

struct UserMenu
{
  Control *ctrl;
  Control *bg;
  float priority;
  int index;
  UserMenu(Control *c, Control *b, float p, int i) {ctrl = c; bg = b; priority = p; index = i;}
};
TypeIsMovableZeroed(UserMenu)

#if _VBS2
class DisplayMissionEditor : public DisplayMap, public MissionEditCursorContainer
#else
class DisplayMissionEditor : public Display, public MissionEditCursorContainer  
#endif
{
protected:  
  EditorWorkspace _ws;
  bool _loadedExisting;
  GameVarSpace _vars;

  RefArray<EditorObject> _clipboard;

  // proxy objects that have been moved in a single simulation step
  RefArray<Object> _movedProxies;

  // this is a copy of the last created editor object
  LLink<EditorObject> _lastObjectCreated;  
  
  // additional menu items
  AutoArray<UserMenu> _userMenus;
  AutoArray<AdditionalMenuItem> _menuItems;

  Ref<MissionEditorCamera> _camera;

  UITime _lastSimulate;

  Ref<CTree> _objectBrowser;
  Ref<CListBox> _attributeBrowser;
#if !_VBS2
  Ref<CStaticMapEditor> _map;
#endif
  Ref<CListBox> _objectTypeList;
  Ref<CBackground3D> _background3D;

  Ref<Person> _originalPlayer;
  Ref<AIBrain> _playerUnit;

  CursorType _mouseCursor;

  EditorMode _mode;

  EditorSelectedStatus _selectedObject;

  // the editor object the mouse is currently over in 2D or 3D
  LLink<EditorObject> _objMouseOver;

  // running in real time mode?
  bool _realTime;

  bool _allow3DMode;
  bool _allowFileOperations;

  // started by server in a non-dedicated multiplayer session
  bool _multiplayer;
  bool _multiplayerPreview;

  // last saved mission name
  RString _mission;

  // used to auto-popup main menus in a similar manner to windows
  int _menuTypeActive;

  // onDoubleClick eventhandler
    // received from map and background controls
  RString _onDoubleClick;

  // onShowNewObject eventhandler
  RString _onShowNewObject;

  // dragging an editor object
  bool _dragging;
  Vector3 _dragPos;

  // selecting multiple editor objects
  bool _selecting;

  // moving with right mouse held down
  bool _draggingRM;

  // rotating an editor object
  bool _rotating;

  // raising or lowering an editor object
  bool _raising;

  // used with multiple select to record screen coords
  DrawCoord _pt1, _pt2;

  // last selected object in the tree
  RString _lastSelectedInTree;

  // timer used to re-enable simulation for selected objects in real time
  UITime _stoppedSimulate;
  bool _simulationDisabled;

  // point to overlay types config entry
  ConstParamEntryPtr _overlayType;

  // undo
  bool _hasChanged;

  /// Attribute browser text colors
  PackedColor _colorAttrDialog, _colorAttrPosition, _colorAttrDirection, _colorAttrLink, _colorAttrParent, _colorAttrCounter;

  // pointer to editor version of compass
#if _VBS2
  CompassMissionEditor *_compassEditor;
#endif

  // hack to enable scripted missions to work in MP
  // template used for saving player and playable units to a mission.sqm file
  ArcadeTemplate _templateMission;

  //! event handlers
  EventHandlersUI _eventHandlers[NEditorEvent];

  // draw 3D icons?
  bool _show3DIcons;

public:
#if _VBS2
  DisplayMissionEditor(ControlsContainer *parent, RString resource, bool multiplayer = false);
#else
  DisplayMissionEditor(ControlsContainer *parent, bool multiplayer = false);
#endif
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
#if _VBS2
  ControlObject *OnCreateObject(int type, int idc, ParamEntryPar cls);
#endif

  void ProcessMouseOverEvent();
  void SetObjMouseOver(EditorObject *over) {_objMouseOver = over;}
  EditorObject *GetObjMouseOver() {return _objMouseOver;}

  // undo
  void RecordAction();

  void SetLastSelectedInTree(RString id) {_lastSelectedInTree = id;}

  void OnButtonClicked(int idc);
  void OnTreeSelChanged(IControl *ctrl);
  void OnTreeItemExpanded(IControl *ctrl, CTreeItem *item, bool expanded);
  void OnTreeDblClick(int idc, CTreeItem *sel);
  void OnTreeLButtonDown(int idc, CTreeItem *sel);
  void OnTreeMouseMove(int idc, CTreeItem *sel);
  void OnTreeMouseHold(int idc, CTreeItem *sel);
  void OnTreeMouseExit(int idc, CTreeItem *sel);
  bool OnKeyDown(int dikCode);
  bool OnKeyUp(int dikCode);

  EditorObject *LastObjectCreated() {return _lastObjectCreated;}
  void ClearLastObjectCreated() {_lastObjectCreated = NULL;}

  void OnSimulate(EntityAI *vehicle);
  void OnDraw(EntityAI *vehicle, float alpha);

  // received from CBackground3D (the control that catches these events)
  void OnLButtonDown(float x, float y);
  void OnLButtonUp(float x, float y);
  void OnLButtonClick(float x, float y);  
  void OnLButtonDblClick(float x, float y);
  void OnMouseMove(float x, float y, bool active);
  void OnMouseHold(float x, float y, bool active);
  void OnRButtonDown(float x, float y);
  void OnRButtonUp(float x, float y);
  void OnRButtonClick(float x, float y);

  // copy selected editable objects to clipboard  
  void ClipboardCopy(bool isCut);
  void ClipboardPaste();

  bool IsRotating() const {return _rotating;}
  bool IsRaising() const {return _raising;}

  void SetRotating(bool rotating) {_rotating = rotating;}
  void SetRaising(bool raising) {_raising = raising;}

  // return true if dragging
  bool IsDragging() {if (!_map) return _dragging || _rotating || _raising; else return _dragging || _rotating || _raising || _map->IsDragging();}

  virtual void OnMenuSelected(MenuItem *item);
  virtual bool OnMenuCreated(Menu &menu, float x, float y, int idc, CCMContext &ctx);
  void InsertAdditionalMenuItems(Menu &menu, int menuType);
  void RemoveContextMenu();

  void OnObjectSelected(RString id);  
  void OnLinkEditted(EditorObject *obj, RString name, RString value, float key);
  
  virtual void OnChildDestroyed(int idd, int exit);

  virtual bool OnUnregisteredAddonUsed(RString addon);

  // MissionEditCursorContainer interface
  void OnObjectMoved(EditorObject *obj, Vector3Par offset);
  void RotateObject(EditorObject *obj, Vector3Par center, float angle);

  Object *FindObject(RString id);

  void UpdateObjectBrowser();
  
  // position map and camera
  void LookAtPosition(Vector3Par pos);

  void CreateObject(EditorObject *obj,bool isNewObject = false, bool isPaste = false, bool isLoading = false);

  // call the delete object script for the object
  void DeleteObject(RString id);  
  void DeleteObject(EditorObject *obj);

  // delete an object from the editor (does not call delete script)
  void DeleteObjectFromEditor(RString id);  

  // display new object resource
  void ShowNewObject(Vector3Par posPar, EditorObject *linkObj = NULL, RString linkArg = RString(), RString className = RString(), TargetSide side = TSideUnknown);

  // select a specific editor object type
  bool SelectEditorObject(RString select);

  // update position of remote objects (after a drag, for example)
  void UpdateRemoteSelObjects();

  // select a specific editor object
  void SelectObject(RString name);
  void UnSelectObject(RString name);
  void ClearSelected();

  // display edit object resource
  virtual void ShowEditObject(RString id, bool disregardProxy = false);

  // find the editor object that owns the given proxy
  EditorObject *FindProxyOwner(Object *proxyObject);

  // return true if running in real time mode
  bool IsRealTime() {return _realTime;}

  // set/get double click event handler
  void SetOnDoubleClick(RString eh) {_onDoubleClick = eh;}
  RString GetOnDoubleClick() {return _onDoubleClick;}

  // set show new object event handler
  void SetOnShowNewObject(RString eh) {_onShowNewObject = eh;}

  // update the controls help text
  void UpdateControlsHelp();
  void SetSelectedStatus(EditorSelectedStatus selectedObject);

  // enable simulation for selected units - used with raising in real time
  void EnableSimulationForSelected(bool enable);    

  // set the active overlay
  void SetActiveOverlay(EditorOverlay *newOverlay);

  // show message box asking to save before closing overlay
  void AskForOverlaySave(int idd, RString text = LocalizeString("STR_EDITOR_PROMPT_SAVE"));

  // commit the active overlay
  void CommitActiveOverlay();  

  // add a new scripted menu or menu item
  int AddMenu(RString text, float priority);
  void CreateMenu(int index);
  int AddMenuItem(RString type, RString text, RString command, int index, float priority, bool assignPriority);
  void UpdateMenuItem(int index, RString text, RString command);
  void RemoveMenuItem(int index, RString text = RString());
  int NMenuItems(RString menuName, int index);
  int GetNextMenuItemIndex();

  // overlays
  void NewOverlay(ConstParamEntryPtr entry);
  void LoadOverlay(ConstParamEntryPtr entry);
  void SaveOverlay();
  void ClearOverlay(bool commit = false, bool close = false);

  // set editor mode (2D, 3D etc)
  void SetMode(EditorMode mode);
  EditorMode GetMode() {return _mode;}

  void Show3DIcons(bool show) {_show3DIcons = show;}
  bool IsShow3DIcons() {return _show3DIcons;}

  void Allow3DMode(bool allow) {_allow3DMode = allow;}
  void AllowFileOperations(bool allow) {_allowFileOperations = allow;}

  // execute 'select' script for an editor object
  void DoSelectObjectScript(EditorObject *obj, bool selected);

  /// set event handler
  void SetEditorEventHandler(RString name, RString value);

  // process event handlers
  void OnEditorEvent(EditorEvent event);
  void OnEditorEvent(EditorEvent event, RString par1, const Object *par2 = NULL);
  void OnEditorEvent(EditorEvent event, RString par1, ConstParamEntryPtr par2 = NULL);

  // camera related
  Object *CameraGet();
  void CameraRestart();

#if _VBS2
  virtual void ImportAllGroups() {}
#endif

  /// when dragging, check if rotations / raising is required
  bool UpdateMode();

  void UpdateObject(EditorObject *obj, bool isRotate = false, bool isRaise = false, float raiseBy = 0.0);

protected:  
  void UpdatePositionObject(EditorObject *obj,Vector3Par offset);  

  bool IsObjectBrowser() const {return _objectBrowser && _objectBrowser->IsVisible();}
  void ShowObjectBrowser(bool show);  
  void UpdateAttributeBrowser();

  void ShowBackground3D(bool show);

  bool IsMap() const {return _map && _map->IsVisible();}
  //CStaticMapEditor *GetMap() {return _map;}
  void ShowMap(bool show);

  void SetCameraPosition(RString id, bool lockCamera);

  // is the editor camera currently active?
  bool EditorCameraActive() 
  {
    CameraEffect *effect = GWorld->GetCameraEffect();
    return effect && effect->GetObject() == _camera;
  }

  bool IsCameraOnVehicle() const {return _playerUnit.NotNull();}
  virtual bool SwitchToVehicle(RString id);
  void SwitchToCamera();

  void LoadMission();
  void RestartMission();
  void ClearMission();

  void StartPreview();
  void FinishPreview();

  // read and process init.sqf
  void DoInit(RString initSQF = "");

  /// retrieve objects currently in the 3D selection box
  typedef RefArray< EditorObject, MemAllocDataStack<Ref<EditorObject>, 64> > SelectingList;
  void GetSelecting(SelectingList &selecting);
};

class DisplayMissionEditorNormal : public DisplayMissionEditor
{
public:
  DisplayMissionEditorNormal(ControlsContainer *parent, bool multiplayer = false);
};

DisplayMissionEditorNormal::DisplayMissionEditorNormal(ControlsContainer *parent, bool multiplayer)
#if _VBS2
: DisplayMissionEditor(parent,"RscDisplayMissionEditor",multiplayer)
#else
: DisplayMissionEditor(parent,multiplayer)
#endif
{
#if _VBS2 // hide main map objects (they are only used in Real Time)
  ShowCompass(false);
  ShowWatch(false);
  ShowWalkieTalkie(false);
  ShowNotepad(false);
  ShowWarrant(false);
  ShowGPS(false);
  ShowBriefing(false); 
#endif

#if !_VBS2
  Load("RscDisplayMissionEditor");
#endif

  if (*Glob.header.filename)
    _mission = Glob.header.filename;

  SetMode(EMMap);
  LoadMission();
  DoInit();
}

inline DisplayMissionEditor *CStaticMapEditor::GetParent()
{
  return static_cast<DisplayMissionEditor *>(_parent.GetLink());
}

Display *CreateMissionEditor(ControlsContainer *parent, bool multiplayer)
{
  return new DisplayMissionEditorNormal(parent, multiplayer);
}

#include <Es/Memory/normalNew.hpp>

struct SubTypeContainer : public RefCount
{
  Ref<EditorObjectType> subType; 
  // main parameter name from editor definitions for the current type of selected object
  RString paramName;  
  // name of subparameter - ie what is displayed in main parameter combo box
  RString paramSubType;

  USE_FAST_ALLOCATOR
};

struct EditArgument : public RefCount
{
  RString name;
  Ref<Control> control;
  Ref<CStatic> title;
  Ref<CStatic> preview;

  // subparam related
  // a pointer to the parent argument if this control is for a subtype
  const EditArgument *parentArgument;
  
  // a pointer to the SubTypeContainer that matches the currently selected option
    // valid only for combo boxes that control subtypes
  const SubTypeContainer *subTypeSelected;
  //-!

  EditArgument() : parentArgument(NULL), subTypeSelected(NULL) {}

  bool IsValid(Display *parent, const EditorParam *param) const; // control level validation
  RString GetValue() const;

  USE_FAST_ALLOCATOR
};

#include <Es/Memory/debugNew.hpp>

DEFINE_FAST_ALLOCATOR(SubTypeContainer)
DEFINE_FAST_ALLOCATOR(EditArgument)

bool EditArgument::IsValid(Display *parent, const EditorParam *param) const
{
  switch (control->GetType())
  {
  case CT_TREE:
    {
      CTree *tree = static_cast<CTree *>(control.GetRef());
      Assert(tree);
      CTreeItem *selected = tree->GetSelected();
      if (!selected || selected->value == 0)
      {
        parent->CreateMsgBox(MB_BUTTON_OK, Format(LocalizeString("STR_EDITOR_ERROR_TREE_SEL"), cc_cast(param->displayName)));
        return false;
      }
    }
    break;
  case CT_LISTBOX:
  case CT_COMBO:
  case CT_XLISTBOX:
  case CT_XCOMBO:
    {
      CListBoxContainer *list = GetListBoxContainer(control);
      Assert(list);
      int sel = list->GetCurSel();
      if (sel < 0)
      {
        parent->CreateMsgBox(MB_BUTTON_OK, Format(LocalizeString("STR_EDITOR_ERROR_TREE_ITEM"), cc_cast(param->displayName)));
        return false;
      }
    }
    break;
  }
  return true;
}

RString EditArgument::GetValue() const
{
  switch (control->GetType())
  {
  case CT_STATIC:
    {
      CStatic *staticCtrl = static_cast<CStatic *>(control.GetRef());
      Assert(staticCtrl);
      return staticCtrl->GetText();
    }
  case CT_EDIT:
    {
      CEditContainer *edit = GetEditContainer(control);
      Assert(edit);
      return edit->GetText();
    }
  case CT_LISTBOX:
  case CT_COMBO:
  case CT_XLISTBOX:
  case CT_XCOMBO:
    {
      CListBoxContainer *list = GetListBoxContainer(control);
      Assert(list);
      int sel = list->GetCurSel();
      if (sel < 0) return RString();
      return list->GetData(sel);
    }
  case CT_SLIDER:
  case CT_XSLIDER:
    {
      CSliderContainer *slider = GetSliderContainer(control);
      Assert(slider);
      float val = slider->GetThumbPos();
      return Format("%.8g", val);
    }
  case CT_TREE:
    {
      CTree *tree = static_cast<CTree *>(control.GetRef());
      Assert(tree);
      CTreeItem *selected = tree->GetSelected();
      if (!selected || selected->value == 0) return RString();
      return selected->data;
    }
  }
  return RString();
}

class DisplayEditObject : public Display
{
protected:
  Ref<CStaticMapEditor> _map; // used for scripting access to editor workspace
  Ref<EditorObject> _object;
  
  RefArray<SubTypeContainer> _subTypes;

  RefArray<EditArgument> _arguments;
  bool _update;

  // used to link the new object once it is created (when control is destroyed)
  Ref<EditorObject> _linkObject;
  RString _linkArgument;

  Control *_toggleButton;
  bool _togglePressed;

public:
  DisplayEditObject(
    ControlsContainer *parent, 
    CStaticMapEditor *map, 
    EditorObject *obj, 
    bool update, 
    EditorObject *linkObj = NULL, 
    RString linkArg = "", 
    RString className = ""
  );

  Control *AddControl(CControlsGroup *grp, const EditorParam &param, float &top);

  EditorObject *GetObject() {return _object;}
  bool IsUpdate() const {return _update;}
  
  EditorObject *LinkToObject(RString &linkType) {linkType = _linkArgument; return _linkObject;}

  virtual void OnLBSelChanged(IControl *ctrl, int curSel) {OnChanged(ctrl);}
  virtual void OnTreeSelChanged(IControl *ctrl) {OnChanged(ctrl);}
  virtual void OnTreeMouseMove(int idc, CTreeItem *sel);
  virtual void OnTreeMouseHold(int idc, CTreeItem *sel);
  virtual void OnTreeMouseExit(int idc, CTreeItem *sel);
  virtual void OnSliderPosChanged(IControl *ctrl, float pos) {OnChanged(ctrl);}

  void OnDraw(EntityAI *vehicle, float alpha);
  
  void OnButtonClicked(int idc);
  virtual bool CanDestroy();
  virtual void Destroy();

protected:
  void AddArgument(RString name, Control *control, CStatic *title, CStatic *preview);
  /// return the index of the  argument with the given name in _arguments
  int FindArgument(RString name) const;
  void OnChanged(IControl *ctrl);
};

void DisplayEditObject::OnDraw(EntityAI *vehicle, float alpha)
{
  Display::OnDraw(vehicle, alpha);
}

static RString Trim(const char *ptr, int len)
{
  while (*ptr && isspace(*ptr))
  {
    ptr++; len--;
  }
  while (isspace(ptr[len - 1]))
  {
    len--;
  }
  return RString(ptr, len);
}

static bool ProcessPair(const char *ptr, RString &s1, RString &s2, char ch)
{
  if (*ptr != '(') return false;
  if (*(ptr + strlen(ptr) - 1) != ')') return false;
  ptr++;
  const char *ext = strchr(ptr, ch);
  if (!ext) return false;
  s1 = Trim(ptr, ext - ptr);
  ptr = ext + 1;
  s2 = Trim(ptr, strlen(ptr) - 1);
  return true;
}

static void ProcessConfigSubtype(const char *ptr, RString &config, RString &entryName, CCombo *combo)
{
  // config name
  const char *ext = strchr(ptr, ',');
  if (!ext)
  {
    config = Trim(ptr, strlen(ptr));
    entryName = "displayName";
    return;
  }
  config = Trim(ptr, ext - ptr);
  
  // entry name
  ptr = ext + 1;
  ext = strchr(ptr, ',');
  if (!ext)
  {
    entryName = Trim(ptr, strlen(ptr));
    return;
  }
  entryName = Trim(ptr, ext - ptr);

  // additional parameters
  while (ext)
  {
    ptr = ext + 1;
    ext = strchr(ptr, ',');
    RString param;
    if (!ext) param = Trim(ptr, strlen(ptr));
    else param = Trim(ptr, ext - ptr);
    const char *p = param;
    if (*p == '+')
    {
      p++;
      RString data;
      RString str;
      if (ProcessPair(p, data, str, '|') && combo)
      {
        int index = combo->AddString(LocalizeString(str));
        combo->SetData(index, data);
      }
    }
  }
}

static ConstParamEntryPtr FindConfigEntry(const ParamClass &cls, const char *path)
{
  ConstParamEntryPtr entry(&cls);
  while (entry)
  {
    const char *ext = strchr(path, '.');
    if (!ext) return entry->FindEntry(path);
    entry = entry->FindEntry(RString(path, ext - path));
    path = ext + 1;
  }
  return NULL;
}

// TODO: add hierarchy based on... side?
static void FillMarkerTree(CTree *tree, RString value)
{
  CTreeItem *root = tree->GetRoot();

  ParamEntryVal cls = Pars >> "CfgMarkers";
  for (int i=0; i<cls.GetEntryCount(); i++)
  {
    ParamEntryVal entry = cls.GetEntry(i);
    if (!entry.IsClass()) continue;

    CTreeItem *item = root->AddChild();
    item->texture = GlobPreloadTexture(GetPictureName(entry >> "icon"));
    item->text = entry >> "name";
    item->data = entry.GetName();
    item->value = 1;  // valid
  }

  for (int i=0; i<root->children.Size(); i++)
  {
    CTreeItem *group = root->children[i];
    group->SortChildren();

    if (group->children.Size() == 0)
      if (stricmp(group->data, value) == 0)
      {
        tree->SetSelected(group);      
        tree->EnsureVisible(group);
      }

    for (int j=0; j<group->children.Size(); j++)
    {
      CTreeItem *item = group->children[j];
      if (stricmp(item->data, value) == 0)
      {
        tree->SetSelected(item);
        CTreeItem *ptr = item;
        while (ptr)
        {
          tree->Expand(ptr);
          ptr = ptr->parent;
        }
        tree->EnsureVisible(item);
      }
    }
  }
}

static bool IsMan(const char *sim)
{
  return stricmp(sim, "soldier") == 0 || stricmp(sim, "invisible") == 0 || stricmp(sim, "zombie") == 0;
}

static const char *SimItems[]={
"inventoryItem",
"clothing",
"itemwatch",
"itemradio",
"itemtransmitter",
"itemcompass",
"itemgps",
"itemmap",
"itembook",
"itemoptics",
NULL
};

static void FillVehicleTree(CTree *tree, RString value, bool menOnly = false, const char**simList=NULL)
{
  CTreeItem *root = tree->GetRoot();
  ParamEntryVal cls = Pars >> "CfgVehicles";
  auto processType = [=](ParamEntryPar entry)
  {
    if (!entry.IsClass()) return;
    int scope = entry >> "scope";
    if (scope != 2) return;
    RString vehicleClass = entry >> "vehicleClass";
    if (vehicleClass.GetLength() == 0 || stricmp(vehicleClass, "sounds") == 0 || stricmp(vehicleClass, "mines") == 0) return;
    RString sim = entry >> "simulation";

    if (simList)
    {
      bool found = false;
      for (const char **s = simList; *s; s++)
      {
        if (!stricmp(*s,sim)) {found=true;break;}
      }
      if (!found) return;
    }
    else
    {
      if (menOnly!=IsMan(sim)) return; // diff
    }

    CTreeItem *found = NULL; 
    for (int i=0; i<root->children.Size(); i++)
    {
      CTreeItem *item = root->children[i];
      if (stricmp(item->data, vehicleClass) == 0)
      {
        found = item;
        break;
      }
    }
    if (!found)
    {
      found = root->AddChild();
      found->data = vehicleClass;
      found->value = 0; // invalid selection
      ConstParamEntryPtr ptr = (Pars >> "CfgVehicleClasses").FindEntry(vehicleClass);
      if (ptr) found->text = (*ptr) >> "displayName";
      else found->text = vehicleClass;
#if _VBS2 // categorize game vehicle classes seperately from VBS2 classes
      bool isGame = true;
      if (ConstParamEntryPtr isVBS = entry.FindEntry("vbs_entity"))
        if (isVBS)
        {
          bool isVBS = entry >> "vbs_entity";          
          isGame = !isVBS;
        }
      if (isGame) found->text = LocalizeString("STR_EDITOR_GAME_CLASS") + " " + found->text;
#endif
    }

    CTreeItem *item = found->AddChild();
    item->data = entry.GetName();
    item->value = 1; // valid selection
    item->text = entry >> "displayName";

    // preview picture
      // expected to be model path + \data\ico\preview_<model name>_ca.paa
    if (entry.FindEntry("preview") || entry.FindEntry("model"))
    {
      RString path = entry.ReadValue("preview",RString());
      if(path.GetLength()) //no config entry found, look for model
      {
        const char *ptr = strchr(path, '.');
        if (!ptr)
          path = path +".paa";
      }
      else
      {
        RString model = entry >> "model";
        RString modelNoExt = model;

        const char *ptr = strchr(model, '.');
        if (ptr)
          modelNoExt = RString(model, ptr - model);

        ptr = strrchr(modelNoExt, '\\');
        if (ptr)
        {
          RString textName(ptr + 1); 
          path = RString(modelNoExt, ptr - modelNoExt);

          // full path to texture
          path = path + "\\data\\ico\\preview_" + textName + "_ca.paa";
        }
      }

      if (path.GetLength() > 0 && QFBankQueryFunctions::FileExists((const char *)path + 1))
      {
        item->texture = GlobPreloadTexture(GetPictureName(path));
        item->drawTexture = false;  // don't draw icon in the tree, it will be drawn in preview pic
      }      
    }
  };

  for (int i=0; i<cls.GetEntryCount(); i++) processType(cls.GetEntry(i));

  root->SortChildren();
  for (int i=0; i<root->children.Size(); i++)
  {
    CTreeItem *group = root->children[i];
    group->SortChildren();

    for (int j=0; j<group->children.Size(); j++)
    {
      CTreeItem *item = group->children[j];
      if (stricmp(item->data, value) == 0)
      {
        tree->SetSelected(item);
        CTreeItem *ptr = item;
        while (ptr)
        {
          tree->Expand(ptr);
          ptr = ptr->parent;
        }
        tree->EnsureVisible(item);
      }
    }
  }  
}

static CStatic *CreatePreview(const EditorParam &param, Display *disp)
{
  if (param.source != EPSDialog) return NULL;

  if (stricmp(param.type, "special") == 0)
  {
    if (stricmp(param.subtype, "vehicle") == 0 || stricmp(param.subtype, "agent") == 0)
    {
      CStatic *ctrl = new CStatic(disp, -1, Pars >> "RscDisplayEditObject" >> "Preview");
      return ctrl;
    }
  }
  return NULL;
}

static Control *CreateCtrl(const EditorParam &param, RString value, Display *disp, EditorWorkspace *ws, EditorObject *obj, RefArray<SubTypeContainer> &subTypes)
{
  if (param.source == EPSParent)
  {
    // list of possible parents
    CCombo *combo = new CCombo(disp, param.idc, Pars >> "RscDisplayEditObject" >> "Combo");

    int sel = 0;
    if (param.type.GetLength() > 0)
    {
      // no parent
      if (stricmp(param.subtype, "noneOrSingle") == 0)
      {
        // TODO: string to stringtable
        int index = combo->AddString("<NULL>");
        combo->SetData(index, RString());
        if (value.GetLength() == 0) sel = index;
      }
      // search for objects of required type
      for (int i=0; i<ws->NObjects(); i++)
      {
        EditorObject *obj = ws->GetObject(i);
        if (stricmp(param.type, obj->GetType()->GetName()) == 0)
        {
          // object found, add it
          RString name = obj->GetArgument("VARIABLE_NAME");
          RString displayName = obj->GetDisplayName();
          if (displayName.GetLength() == 0) displayName = Format("%s \"%s\"", cc_cast(param.type), cc_cast(name));

          int index = combo->AddString(displayName);
          combo->SetData(index, name);
          if (stricmp(name, value) == 0) sel = index;
        }
      }
    }

    combo->SetCurSel(sel, false);
    return combo;
    
  }

  Assert(param.source == EPSDialog);
  if (stricmp(param.type, "enum") == 0)
  {
    const EnumName *names = NULL;
    if (stricmp(param.subtype, "ArcadeUnitAge") == 0)
    {
      names = GetEnumNames(ArcadeUnitAge());
    }
    else if (stricmp(param.subtype, "CamEffectPosition") == 0)
    {
      names = GetEnumNames(CamEffectPosition());
    }
    else if (stricmp(param.subtype, "TitleType") == 0)
    {
      names = GetEnumNames(TitleType());
    }
    else if (stricmp(param.subtype, "TitEffectName") == 0)
    {
      names = GetEnumNames(TitEffectName());
    }
    else if (stricmp(param.subtype, "MarkerType") == 0)
    {
      names = GetEnumNames(MarkerType());
    }
    else if (stricmp(param.subtype, "ArcadeUnitSpecial") == 0)
    {
      names = GetEnumNames(ArcadeUnitSpecial());
    }
    else if (stricmp(param.subtype, "LockState") == 0)
    {
      names = GetEnumNames(LockState());
    }
    else if (stricmp(param.subtype, "Rank") == 0)
    {
      names = GetEnumNames(Rank());
    }
    else if (stricmp(param.subtype, "Semaphore") == 0)
    {
      names = GetEnumNames(AI::Semaphore());
    }
    else if (stricmp(param.subtype, "CombatMode") == 0)
    {
      names = GetEnumNames(CombatMode());
    }
    else
    {
      LogF("### ENUM: %s", (const char *)param.subtype);
      return NULL;
    }

    Assert(names);
    CCombo *combo = new CCombo(disp, param.idc, Pars >> "RscDisplayEditObject" >> "Combo");
    int sel = 0;
    while (names->IsValid())
    {
      int ids = names->GetIDS();
      if (ids != DUPLICATE_IDS)
      {
        RString text = ids >= 0 ? LocalizeString(ids) : names->name;
        int index = combo->AddString(text);
        combo->SetData(index, names->name);
        combo->SetValue(index, names->value);
        if (stricmp(names->name, value) == 0) sel = index;
      }
      names++;
    }
    combo->SetCurSel(sel, false);
    return combo;
  }
  else if (stricmp(param.type, "number") == 0)
  {
    {
      float from, to;
      if (sscanf(param.subtype, "range(%f,%f)", &from, &to) == 2)
      {
        CSlider *slider = new CSlider(disp, param.idc, Pars >> "RscDisplayEditObject" >> "Slider");
        slider->SetRange(from, to);
        slider->SetSpeed(0.01 * (to - from), 0.1 * (to - from));
        float pos = strtod(value, NULL);
        slider->SetThumbPos(pos);
        return slider;
      }
    }
    {
      int from, to, step;
      if (sscanf(param.subtype, "list(%d,%d,%d)", &from, &to, &step) == 3)
      {
        CCombo *combo = new CCombo(disp, param.idc, Pars >> "RscDisplayEditObject" >> "Combo");
        int val = atoi(value);
        int sel = 0;
        for (int i=from; i<=to; i+=step)
        {
          RString value = Format("%d", i);
          int index = combo->AddString(value);
          combo->SetData(index, value);
          combo->SetValue(index, i);
          if (i == val) sel = index;
        }
        combo->SetCurSel(sel, false);
        return combo;
      }
    }
  }
  else if (stricmp(param.type, "bool") == 0)
  {
    CCombo *combo = new CCombo(disp, param.idc, Pars >> "RscDisplayEditObject" >> "Combo");
    RString t = "true";
    RString f = "false";
    if (ProcessPair(param.subtype, t, f, ','))
    {
      t = LocalizeString(t);
      f = LocalizeString(f);
    }
    int index = combo->AddString(f);
    combo->SetData(index, "false");
    index = combo->AddString(t);
    combo->SetData(index, "true");
    if (stricmp(value, "true") == 0) combo->SetCurSel(1, false);
    else combo->SetCurSel(0, false);
    return combo;
  }
  else if (stricmp(param.type, "button") == 0 || stricmp(param.type, "buttonToggle") == 0)
  {
     CButton *button = new CButton(disp, param.idc, Pars >> "RscDisplayEditObject" >> "Button");
     return button;
  }
  // preview picture (linked to a tree or list with icons)
  else if (stricmp(param.type, "picture") == 0)
  {
     CStatic *picture = new CStatic(disp, param.idc, Pars >> "RscDisplayEditObject" >> "Picture");
     return picture;
  }
  else if (stricmp(param.type, "config") == 0)
  {
    RString config;
    RString entryName;

    CCombo *combo = new CCombo(disp, param.idc, Pars >> "RscDisplayEditObject" >> "Combo");
    ProcessConfigSubtype(param.subtype, config, entryName, combo);

    ConstParamEntryPtr array = FindConfigEntry(Pars, config);
    if (array && array->IsClass())
    {
      int n = array->GetEntryCount();
      for (int i=0; i<n; i++)
      {
        ParamEntryVal entry = array->GetEntry(i);
        if (!entry.IsClass()) continue;
        if (!entry.FindEntry(entryName)) continue;

        // hidden?
        if (entry.CheckIfEntryExists("hidden"))
            if (entry >> "hidden")
              continue;

        int index = combo->AddString(entry >> entryName);
        combo->SetData(index, entry.GetName());
      }
    }

    int sel = 0;
    for (int i=0; i<combo->GetSize(); i++)
    {
      if (stricmp(value, combo->GetData(i)) == 0)
      {
        sel = i;
        break;
      }
    }
    combo->SetCurSel(sel, false);
    return combo;
  }
  else if (stricmp(param.type, "configEx") == 0)
  {
    RString config;
    RString entryName;

    CCombo *combo = new CCombo(disp, param.idc, Pars >> "RscDisplayEditObject" >> "Combo");
    ProcessConfigSubtype(param.subtype, config, entryName, combo);

    for (int j=0; j<2; j++)
    {
      ParamFile *pFile = &Pars;
      if (j == 1) 
        pFile = &ExtParsMission;
      ConstParamEntryPtr array = FindConfigEntry(*pFile, config);
      if (array && array->IsClass())
      {
        int n = array->GetEntryCount();
        for (int i=0; i<n; i++)
        {
          ParamEntryVal entry = array->GetEntry(i);
          if (!entry.IsClass()) continue;
          if (!entry.FindEntry(entryName)) continue;

          // hidden?
          if (entry.CheckIfEntryExists("hidden"))
             if (entry >> "hidden")
               continue;

          int index = combo->AddString(entry >> entryName);
          combo->SetData(index, entry.GetName());

          // find subparameters and create a new type if required
          if (entry.CheckIfEntryExists("Params"))
          {
            Ref<SubTypeContainer> container = new SubTypeContainer();
            container->subType = new EditorObjectType(RString(),entry);
            container->paramName = param.name;
            container->paramSubType = entry.GetName();
            subTypes.Add(container);
          }
          //-!
        }
      }
    }

    int sel = 0;
    for (int i=0; i<combo->GetSize(); i++)
    {
      if (stricmp(value, combo->GetData(i)) == 0)
      {
        sel = i;
        break;
      }
    }
    combo->SetCurSel(sel, false);
    return combo;
  }
  else if (stricmp(param.type, "special") == 0)
  {
    if (stricmp(param.subtype, "marker") == 0)
    {
      CTree *tree = new CTree(disp, param.idc, Pars >> "RscDisplayEditObject" >> "TreeWithIcons");
      FillMarkerTree(tree, value);
      return tree;
    }
    else if (stricmp(param.subtype, "agent") == 0)
    {
      CTree *tree = new CTree(disp, param.idc, Pars >> "RscDisplayEditObject" >> "Tree");
      FillVehicleTree(tree, value, true);
      return tree;
    }
    else if (stricmp(param.subtype, "vehicle") == 0)
    {
      CTree *tree = new CTree(disp, param.idc, Pars >> "RscDisplayEditObject" >> "Tree");
      FillVehicleTree(tree, value);
      return tree;
    }
    else if (stricmp(param.subtype, "item") == 0)
    {
      CTree *tree = new CTree(disp, param.idc, Pars >> "RscDisplayEditObject" >> "Tree");
      FillVehicleTree(tree, value, false, SimItems);
      return tree;
    }
    else if (stricmp(param.subtype, "collection") == 0)
    {
      CCombo *combo = new CCombo(disp, param.idc, Pars >> "RscDisplayEditObject" >> "Combo");
      ParamEntryVal cls = Pars >> "CfgVehicles";
      for (int i=0; i<cls.GetEntryCount(); i++)
      {
        ParamEntryVal entry = cls.GetEntry(i);
        if (!entry.IsClass()) continue;
        int scope = entry >> "scope";
        if (scope != 2) continue;
        RString vehicleClass = entry >> "vehicleClass";
        if (vehicleClass.GetLength() == 0 || stricmp(vehicleClass, "sounds") == 0 || stricmp(vehicleClass, "mines") == 0) continue;
        RString sim = entry >> "simulation";
        if (stricmp(sim, "collection") != 0 && stricmp(sim, "thing") != 0 ) continue;

        RString displayName = entry >> "displayName";
        int index = combo->AddString(displayName);
        combo->SetData(index, entry.GetName());
      }
      combo->SortItems();

      int sel = 0;
      for (int i=0; i<combo->GetSize(); i++)
      {
        if (stricmp(value, combo->GetData(i)) == 0)
        {
          sel = i;
          break;
        }
      }
      combo->SetCurSel(sel, false);
      return combo;
    }
    else if (stricmp(param.subtype, "soundSource") == 0)
    {
      CCombo *combo = new CCombo(disp, param.idc, Pars >> "RscDisplayEditObject" >> "Combo");

      ParamEntryVal cls = Pars >> "CfgVehicles";
      for (int i=0; i<cls.GetEntryCount(); i++)
      {
        ParamEntryVal entry = cls.GetEntry(i);
        if (!entry.IsClass()) continue;
        int scope = entry >> "scope";
        if (scope != 2) continue;
        RString vehicleClass = entry >> "vehicleClass";
        if (stricmp(vehicleClass, "sounds") != 0) continue;
        RString displayName = entry >> "displayName";
        int index = combo->AddString(displayName);
        combo->SetData(index, entry.GetName());
      }
      combo->SortItems();

      int sel = 0;
      for (int i=0; i<combo->GetSize(); i++)
      {
        if (stricmp(value, combo->GetData(i)) == 0)
        {
          sel = i;
          break;
        }
      }
      combo->SetCurSel(sel, false);
      return combo;
    }
    return NULL;
  }

  bool multiline = stricmp(param.subtype,"long")==0;
  CEdit *edit = new CEdit(disp, param.idc, Pars >> "RscDisplayEditObject" >> (multiline ? "EditLong" : "Edit"));
  edit->SetText(value);
  if (stricmp(param.type, "code") == 0 || stricmp(param.type, "expression") == 0 || stricmp(param.type, "evalBool") == 0 || stricmp(param.type, "condition") == 0)
  {
    edit->SetAutoComplete(CreateAutoComplete("scripting"));
  }
  return edit;
}

class FindVehicleFunc
{
protected:
  RString _id;
  mutable const CTreeItem *_found;

public:
  FindVehicleFunc(RString id) {_id = id; _found = NULL;}
  const CTreeItem *GetFound() const {return _found;}
  bool operator () (const CTreeItem *item) const
  {
    if (stricmp(item->data, _id) == 0)
    {
      _found = item;
      return true; // stop searching now
    }
    return false;
  }
};

DisplayEditObject::DisplayEditObject(
  ControlsContainer *parent, 
  CStaticMapEditor *map, 
  EditorObject *obj, 
  bool update, 
  EditorObject *linkObj, 
  RString linkArg, 
  RString className
)
: Display(parent)
{  
  _enableDisplay = true;
  Load("RscDisplayEditObject");

  SetCursor("Wait");
  DrawCursor();

  _enableSimulation = map->IsRealTime();

  _linkObject = linkObj;
  _linkArgument = linkArg;

  _map = map;
  _object = obj;
  _update = update;

  _toggleButton = NULL;
  _togglePressed = false;

  // add controls
  IControl *ctrl = GetCtrl(IDC_EDIT_OBJECT_CONTROLS);
  if (ctrl && ctrl->GetType() == CT_CONTROLS_GROUP)
  {
    CControlsGroup *grp = static_cast<CControlsGroup *>(ctrl);

    float top = 0.02;
    const EditorParams &params = obj->GetType()->GetParams();   

    int n = params.Size();
    for (int i=0; i<n; i++)
    {
      const EditorParam &param = params[i];

      int nParent = _arguments.Size();
      
      Control *ctrl = NULL;
      ctrl = AddControl(grp,param,top); 

      // did we add a toggle button?
      if (ctrl && ctrl->GetType() == CT_BUTTON)
      {
        if (!_toggleButton && stricmp(param.type,"buttonToggle") == 0)
        {
          _toggleButton = ctrl;
          
          // toggle button defaults to OFF?
          if (_update)
          {
            RString isAdvanced = obj->GetArgument("IS_ADVANCED");
            if (stricmp(isAdvanced,"true") == 0)
              _togglePressed = true;     
          }
          else  // look at default setting for advanced
          {
            for (int j=0; j<n; j++)
            {
              const EditorParam &param = params[j];
              if (stricmp(param.name,"IS_ADVANCED") == 0)
                if (stricmp(param.defValue,"true") == 0)
                {
                  _togglePressed = true;
                  break;
                }
            }
          }
        }
      }        

      // add controls for subparameters
      //
      // parameters of type "configEx" can define their own subparameters that can be changed in the dialog
      // subparameters are added to object parameters when dialog is closed
      // example (main editor object definition):
      //
      // class Params
      // {
      //   class TYPE
      //   {
      //     type = configEx;
      //     subtype = CfgTasks, name;
      //     ...
      //   };
      //   ...
      // };
      //
      // CfgTasks:
      //
      // class CfgTasks
      // {
      //   class Move
      //   {
      //     name = "Move";   
      //     ...
      //     class Params
      //     {
      //       class SUBPARAM1
      //       {
      //         type = text;
      //         canChange = false;
      //         default = "a subparameter";
      //       };
      //     };
      //     ...
      //   };
      //   ...
      // };
      // in this example a SUBPARAM1 text box would be displayed under the TYPE combo on the dialog
      // limitations - subparams must be named differently and subparam subparams won't be processed

      if (stricmp(param.type, "configEx") == 0)
      {
        CCombo *combo = static_cast<CCombo *>(ctrl);
        if (combo)
        {
          int nSubTypes = _subTypes.Size();
          for (int j=0; j<nSubTypes; j++)
          {
            if (
              param.name == _subTypes[j]->paramName &&
              combo->GetData(combo->GetCurSel()) == _subTypes[j]->paramSubType
            )
            {
              const EditorParams &subParams = _subTypes[j]->subType->GetParams();   

              // add controls for the subparameters
              for (int k=0; k<subParams.Size(); k++)
              {
                const EditorParam *subParam = &subParams[k];

                // search object params for subparameter value (in case this subparameter was previously set)
                const EditorObjectType *existingSubType = obj->GetSubType();
                if (existingSubType)
                {                  
                  const EditorParams &params = existingSubType->GetParams();   
                  const EditorParam *existingSubParam = params.Find(subParam->name);                
                  if (existingSubParam)
                    subParam = existingSubParam;
                }

                // add control for subparameter
                int n = _arguments.Size();
                AddControl(grp,*subParam,top);         

                // store the parent argument for this control
                if (_arguments.Size() > n) _arguments[n]->parentArgument = _arguments[nParent];
              }

              // update parent editargument to point to the active subparameters 
                // (will change every time parent combo box is changed) - see OnChanged()
              for (int k=0; k<_arguments.Size(); k++)
                if (_arguments[k]->control == ctrl)
                {
                  _arguments[k]->subTypeSelected = _subTypes[j];                 
                  break;
                }
              break;
            }
          }
        }
      }
    }

    grp->UpdateScrollbars();

    // search for tree control and process preview pic
    for (int i=0; i<_arguments.Size(); i++)
    {
      Control *ctrl = _arguments[i]->control;
      if (ctrl->GetType() == CT_TREE)
      {
        CTree *tree = static_cast<CTree *>(ctrl);
        Assert(tree);
        OnTreeMouseMove(-1, tree->GetSelected());
      }
    }

    // handling of onInit events
    GameState *gstate = GWorld->GetGameState();
    int edhandle = gstate->BeginContext(_map->GetVariables()); // editor space
    GameVarSpace local(_map->GetVariables(), false);
    int lochandle = gstate->BeginContext(&local); // local space

    // initialization of variables
    gstate->VarSetLocal("_map", CreateGameControl(_map), true);
    for (int i=0; i<_arguments.Size(); i++)
    {
      RString name = RString("_control_") + _arguments[i]->name;
      gstate->VarSetLocal(name, CreateGameControl(_arguments[i]->control), true);
      name = RString("_control_title_") + _arguments[i]->name;
      gstate->VarSetLocal(name, CreateGameControl(_arguments[i]->title), true);
    }
    if (_update && _object) // _this == proxy
      gstate->VarSetLocal("_this", GameValueExt(unconst_cast(_object->GetProxyObject())), true);
    else
      gstate->VarSetLocal("_this", NOTHING, true);

    // executing of handlers - subparameters
    for (int i=0; i<_arguments.Size(); i++)
    {
      const SubTypeContainer *subParamsContainer = _arguments[i]->subTypeSelected;
      if (subParamsContainer) // contains subparams
      {
        const EditorParams &subParams = subParamsContainer->subType->GetParams();
        for (int j=0; j<subParams.Size(); j++)
        {
          const EditorParam &param = subParams[j];
          if (param.source == EPSDialog && param.onInit.GetLength() > 0)
            gstate->Execute(param.onInit, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
        }
      }
    }

    // executing of handlers - normal parameters
    for (int i=0; i<n; i++)
    {
      const EditorParam &param = params[i];
      if (param.source == EPSDialog && param.onInit.GetLength() > 0)
        gstate->Execute(param.onInit, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
    }

    gstate->EndContext(lochandle); // local space
    gstate->EndContext(edhandle); // editor space

    if (!update)
    {
      //-- automatically select entry
      if (className.GetLength() > 0)
      {
        IControl *control = NULL;

        // find control for type
        for (int i=0; i<_arguments.Size(); i++)
          if (stricmp(_arguments[i]->name,RString("type")) == 0)
             control = _arguments[i]->control;
        
        // find matching class
        if (control)
        {
          CCombo *combo = dynamic_cast<CCombo *>(control);
          if (combo)
          {
            for (int i=0; i<combo->GetSize(); i++)
            {
              RString data = combo->GetData(i);
              if (stricmp(className, data) == 0)
              {
                combo->SetCurSel(i);
                break;
              }
            }
          }
          else
          {
            CTree *tree = dynamic_cast<CTree *>(control);
            if (tree)
            {
              FindVehicleFunc func(className);
              tree->ForEachItem(func);              
              CTreeItem *found = unconst_cast(func.GetFound());
              if (found)
              {
                tree->SetSelected(found);
                CTreeItem *ptr = found;
                while (ptr)
                {
                  tree->Expand(ptr);
                  ptr = ptr->parent;
                }
                tree->EnsureVisible(found);
              }
            }
          }
        }
      }
      //-!
    }
  }
  if (_toggleButton && _togglePressed)
  {
    _togglePressed = false;
    OnButtonClicked(IDC_EDIT_OBJECT_TOGGLE_BUTTON);
  }
  SetCursor("Arrow");
}

Control *DisplayEditObject::AddControl(CControlsGroup *grp, const EditorParam &param, float &top)
{
  if (param.source != EPSDialog && param.source != EPSParent) return NULL;

  RString name = param.name;
  RString value;
  if (stricmp(param.subtype, "multiple") == 0)
  {
    for (int j=0; j<_object->NArguments(); j++)
    {
      const EditorArgument &arg = _object->GetArgument(j);
      if (stricmp(arg.name, name) == 0)
      {
        if (value.GetLength() > 0) value = value + ", ";
        value = value + arg.value;
      }
    }
  }
  else value = _object->GetArgument(name);

  // no argument exists? might be a subparameter, assign default value
  if (value.GetLength() == 0 && param.hasDefValue)
    value = param.defValue;

  // position of all resources relative to 0, top

  CStatic *title = new CStatic(this, -1, Pars >> "RscDisplayEditObject" >> "Title");
  title->SetText(param.displayName);
  title->SetPos(title->X(), top + title->Y(), title->W(), title->H());
  grp->AddControl(title);
  
  float bottom = title->Y() + title->H();

  bool positionAbs = false;

  // select control by param description
  Control *ctrl = CreateCtrl(param, value, this, _map->GetWorkspace(), _object, _subTypes);
  if (ctrl)
  {
    // absolute position specified?
    if (param.x != -1 || param.y != -1 || param.w != -1 || param.h != -1)
    {
      positionAbs = true;
      float x = param.x == -1 ? ctrl->X() : param.x;
      float y = param.y == -1 ? top + ctrl->Y(): param.y;
      if (param.y == -1) positionAbs = false;
      float w = param.w == -1 ? ctrl->W() : param.w;
      float h = param.h == -1 ? ctrl->H() : param.h;
      ctrl->SetPos(x, y, w, h);      
    }
    else
      ctrl->SetPos(ctrl->X(), top + ctrl->Y(), ctrl->W(), ctrl->H());
    grp->AddControl(ctrl);
    saturateMax(bottom, ctrl->Y() + ctrl->H());
    ctrl->EnableCtrl(!_update || param.canChange);

    // preview control
    CStatic *preview = CreatePreview(param, this);
    if (preview)
    {
      preview->SetPos(preview->X(), top + preview->Y(), preview->W(), preview->H());
      grp->AddControl(preview);
      saturateMax(bottom, preview->Y() + preview->H());
    }

    AddArgument(name, ctrl, title, preview);
  }

  // hidden?
  if (param.hidden || positionAbs || _toggleButton)
  {
    title->ShowCtrl(false);
    if (param.hidden || _toggleButton) ctrl->ShowCtrl(false);
  }
  else
    top = bottom + 0.005; // keep space between controls

  return ctrl;
}

void DisplayEditObject::OnButtonClicked(int idc)
{
  switch (idc)
  {
    case IDC_EDIT_OBJECT_TOGGLE_BUTTON:
      {
        // toggle button pressed, so show/hide hidden controls below it
        IControl *ctrl = GetCtrl(idc);
        if (ctrl && ctrl->GetType() == CT_BUTTON)
        {
          // find button in controls list
          const EditArgument *buttonArgument = NULL;          
          int i; for (i=0; i<_arguments.Size(); i++)
            if (_arguments[i]->control == ctrl)
            {
              buttonArgument = _arguments[i];
              break;
            }
          if (!buttonArgument) return;

          float top = 0.02;  
          if (i > 0)  // button is not the first argument
            top = _arguments[i-1]->title->Y() + _arguments[i-1]->title->H() + 0.005;

          const EditorParams &params = _object->GetAllParams();

          // find parameter of type buttonToggle 
          const EditorParam *buttonParam = NULL;
          for (int i=0; i<params.Size(); i++) // find parameter matching control
            if (stricmp(buttonArgument->name,params[i].name) == 0)
            {
              buttonParam = &params[i];
              break;
            }
          if (!buttonParam) return;

          // button expliticly positioned?
          if (buttonParam->y > -1)  
            top = buttonArgument->control->Y() + buttonArgument->control->H() + (0.005 * 3); // make top below the button

          // show all hidden controls
          for (int j=i+1; j<_arguments.Size(); j++)
          {     
            for (int i=0; i<params.Size(); i++) // find parameter matching control
            {
              if (params[i].name == _arguments[j]->name)
              {
                Control *ctrl = _arguments[j]->control;                
                Control *title = _arguments[j]->title;

                if (_togglePressed) // hide all controls
                {
                  ctrl->SetPos(ctrl->X(), top, ctrl->W(), ctrl->H());
                  ctrl->ShowCtrl(false);
                  title->SetPos(title->X(), top, title->W(), title->H());
                  title->ShowCtrl(false);                    
                }
                else  // show all controls
                {                  
                  const EditorParam &param = params[i];
                  
                  bool positionAbs = false;
                  float h = title->H();

                  // absolute position specified?
                  if (param.x != -1 || param.y != -1 || param.w != -1 || param.h != -1)
                  {
                    positionAbs = true;
                    float x = param.x == -1 ? ctrl->X() : param.x;
                    float y = param.y == -1 ? top : param.y; if (param.y == -1) positionAbs = false;
                    float w = param.w == -1 ? ctrl->W() : param.w;
                    float h = param.h == -1 ? ctrl->H() : param.h;
                    ctrl->SetPos(x, y, w, h);      
                  }
                  else
                  {
                    ctrl->SetPos(ctrl->X(), top, ctrl->W(), ctrl->H());
                    title->SetPos(title->X(), top, title->W(), title->H());
                  }
                  
                  // hidden?
                  if (param.hidden || positionAbs)
                  {
                    title->ShowCtrl(false);
                    ctrl->ShowCtrl(!param.hidden);
                  }
                  else
                  {
                    title->ShowCtrl(true);
                    ctrl->ShowCtrl(true);
                    top += h + 0.005;
                  }
                }
                break;
              }
            }
          }
          // move button to start or end, unless button position is explicitly defined
          if (buttonParam->y == -1)
          {
            Control *ctrl = buttonArgument->control;
            Control *title = buttonArgument->title;
            ctrl->SetPos(ctrl->X(), top, ctrl->W(), ctrl->H());
            title->SetPos(title->X(), top, title->W(), title->H());              
          }

          // move view (within controls group) to top or bottom depending on state of toggle button
          IControl *ctrl = GetCtrl(IDC_EDIT_OBJECT_CONTROLS);
          if (ctrl && ctrl->GetType() == CT_CONTROLS_GROUP)
          {
            CControlsGroup *grp = static_cast<CControlsGroup *>(ctrl);
            if (_togglePressed) // view to top
              grp->SetViewPos(false,0);
            else  // view to bottom
              grp->SetViewPos(false,1);
          }

          _togglePressed = !_togglePressed;
        }
      }
      return;
  }
  Display::OnButtonClicked(idc);
}

bool DisplayEditObject::CanDestroy()
{
  if (!Display::CanDestroy()) return false;

  const EditorParams *params = &_object->GetType()->GetParams();

  if (_exit == IDC_OK)
  {
    // arguments level validation
    for (int i=0; i<_arguments.Size(); i++)
    {
      const EditArgument *arg = _arguments[i];
      const EditorParam *param = params->Find(arg->name);
      Assert(param);
      if (!arg->IsValid(this, param)) return false;
    }

    // object level validation
    bool valid = true;

    // handling of onChanged event
    GameState *gstate = GWorld->GetGameState();
    int edhandle = gstate->BeginContext(_map->GetVariables()); // editor space
    GameVarSpace local(_map->GetVariables(), false);
    int lochandle = gstate->BeginContext(&local); // local space

    // initialization of variables
    gstate->VarSetLocal("_map", CreateGameControl(_map), true);
    gstate->VarSetLocal("_edit", GameValue(_update), true);
    gstate->VarSetLocal("_object", GameValue(_object->GetArgument("VARIABLE_NAME")), true);

    // executing of handlers    
    for (int i=0; i<_arguments.Size(); i++)
    {
      const EditArgument *arg = _arguments[i];
      
      // is this a control for a subparameter?
      const EditArgument *parentArg = arg->parentArgument;
      if (parentArg)
        params = &parentArg->subTypeSelected->subType->GetParams();

      const EditorParam *param = params->Find(arg->name);
      Assert(param && (param->source == EPSDialog || param->source == EPSParent));      

      RString value = arg->GetValue();
      RString message;

      if (stricmp(param->type, "code") == 0 || stricmp(param->type, "expression") == 0 || stricmp(param->type, "evalBool") == 0 || stricmp(param->type, "condition") == 0)
      {
        if (stricmp(param->type, "code") == 0)
          valid = gstate->CheckExecute(value, GWorld->GetMissionNamespace()); // mission namespace
        else if (stricmp(param->type, "evalBool") == 0 || stricmp(param->type, "condition") == 0)
          valid = gstate->CheckEvaluateBool(value, GWorld->GetMissionNamespace()); // mission namespace
        else
          valid = gstate->CheckEvaluate(value, GWorld->GetMissionNamespace()); // mission namespace
        if (!valid)
        {
          CEdit *edit = dynamic_cast<CEdit *>((IControl *)arg->control);
          if (edit)
          {
            FocusCtrl(edit);
            message = gstate->GetLastErrorText();
            edit->SetCaretPos(gstate->GetLastErrorPos(value));
          }
        }
      }
      else if (stricmp(param->type, "number") == 0)
      {
        if (value.GetLength() == 0) valid = false;
        else
        {
          char *end = NULL;
          strtod(value, &end);
          valid = end == (const char *)value + value.GetLength();
        }
        if (!valid)
        {
          message = LocalizeString(IDS_MSG_NUMBER_EXPECTED);
        }
      }

      if (param->valid.GetLength() > 0)
      {
        gstate->VarSetLocal("_value", GameValue(value), true);

        GameValue result = gstate->Evaluate(param->valid, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
        if (result.GetNil())
        {
          message = LocalizeString("STR_EDITOR_ERROR_RTN_VAL_NIL"); 
          valid = false;
        }
        else if (result.GetType() == GameBool)
        {
          bool ok = result;
          if (!ok)
          {
            message = LocalizeString("STR_EDITOR_ERROR_RTN_VAL_FALSE");
            valid = false;
          }
        }
        else if (result.GetType() == GameString)
        {
          message = result;
          valid = false;
        }
        else
        {
          message = Format(LocalizeString("STR_EDITOR_ERROR_RTN_VAL_STR"), cc_cast(result.GetText()));
          valid = false;
        }
      }
      if (!valid)
      {
        FocusCtrl(arg->control);
        CreateMsgBox(MB_BUTTON_OK, message);
        break;
      }
    }
    gstate->EndContext(lochandle); // local space
    gstate->EndContext(edhandle); // editor space

    return valid;
  }

  return true;
}

void DisplayEditObject::Destroy()
{
  Display::Destroy();

  // update object
  if (_exit == IDC_OK)
  {
    // remove old arguments
    const EditorParams &params = _object->GetAllParams();
    for (int i=0; i<params.Size(); i++)
    {
      const EditorParam &param = params[i];
      if (param.source == EPSDialog || param.source == EPSParent) _object->RemoveArgument(param.name);
    }

    // add new arguments
    for (int i=0; i<_arguments.Size(); i++)
    {
      const EditorParams *params = &_object->GetType()->GetParams();
      const EditArgument *arg = _arguments[i];

      // does this control handle subparamaters?
      const SubTypeContainer *subTypeContainer = arg->subTypeSelected;
      if (subTypeContainer)
      {
        // process subparameters
        const EditorObjectType *subType = subTypeContainer->subType;
        if (subType)
        {
          // assign subtype to object
          bool assign = true;

          // does exiting subtype need to be changed?          
          if (_object->GetSubType())
            if (_object->GetSubType()->GetName() == arg->GetValue())
              assign = false;

          if (assign) _object->AssignSubType(subType,arg->GetValue());
        }
      }

      // is this a control for a subparameter?
      const EditArgument *parentArg = arg->parentArgument;
      if (parentArg)
        params = &parentArg->subTypeSelected->subType->GetParams();

      const EditorParam *param = params->Find(arg->name);
      Assert(param && (param->source == EPSDialog || param->source == EPSParent));

      RString value = arg->GetValue();
      if (stricmp(param->subtype, "multiple") == 0)
      {
        const char *ptr = value;
        while (true)
        {
          const char *ext = strchr(ptr, ',');
          if (!ext)
          {
            _object->SetArgument(arg->name, Trim(ptr, strlen(ptr)));
            break;
          }
          _object->SetArgument(arg->name, Trim(ptr, ext - ptr));
          ptr = ext + 1;
        }
      }
      else
      {
        _object->SetArgument(arg->name, value);
      }
    } 
  }
}

void DisplayEditObject::AddArgument(RString name, Control *control, CStatic *title, CStatic *preview)
{
  Ref<EditArgument> arg = new EditArgument();
  arg->name = name;
  arg->control = control;
  arg->title = title;
  arg->preview = preview;
  _arguments.Add(arg);
}

int DisplayEditObject::FindArgument(RString name) const
{
  for (int i=0; i<_arguments.Size(); i++)
  {
    if (stricmp(_arguments[i]->name, name) == 0) return i;
  }
  return -1;
}

// TODO: reset to selected icon if mouse exits tree area
void DisplayEditObject::OnTreeMouseMove(int idc, CTreeItem *sel)
{
  CStatic *ctrl = NULL;

  // attempt to find a preview (picture) control
  for (int i=0; i<_arguments.Size(); i++)
  {
    if (_arguments[i]->control->GetStyle() & ST_PICTURE)
    {
      Control *control = static_cast<Control *>(_arguments[i]->control);
      ctrl = dynamic_cast<CStatic *>(control);
      break;
    };
  };
  if (!ctrl) return;

  if (!sel)
  {
    ctrl->SetTexture(NULL);
    return;
  }

  // show same picture as shown in currently selected item
  ctrl->SetTexture(sel->texture);
}

void DisplayEditObject::OnTreeMouseHold(int idc, CTreeItem *sel)
{
  OnTreeMouseMove(idc, sel);
}

void DisplayEditObject::OnTreeMouseExit(int idc, CTreeItem *sel)
{
  OnTreeMouseMove(idc, sel);
}

static TargetSide GetSideFromName(RString sideStr)
{
  TargetSide side = TSideUnknown;
  const EnumName *names = GetEnumNames(side);
  for (int i=0; names[i].IsValid(); i++)
    if (stricmp(names[i].name, sideStr) == 0)
    {
      side = (TargetSide)names[i].value;
      break;
    }
  return side;
}

void DisplayEditObject::OnChanged(IControl *ctrl)
{
  // search for the argument matching the control
  const EditArgument *arg = NULL;
  for (int i=0; i<_arguments.Size(); i++)
    if (_arguments[i]->control == ctrl)
      arg = _arguments[i];
  if (!arg) return;

  // update the preview
  if (arg->preview)
  {
    arg->preview->ShowCtrl(false);

    const EditorParam *param = _object->GetType()->GetParams().Find(arg->name);
    if (param)
    {
      if (stricmp(param->type, "special") == 0)
      {
        if (stricmp(param->subtype, "agent") == 0 || stricmp(param->subtype, "vehicle") == 0)
        {
          CTree *tree = dynamic_cast<CTree *>(ctrl);
          if (tree)
          {
            CTreeItem *item = tree->GetSelected();
            if (item && item->value != 0 && item->data.GetLength() > 0)
            {
              RString picture = Pars >> "CfgVehicles" >> item->data >> "picture";
              if (picture.GetLength() > 0)
              {
                arg->preview->SetTexture(GlobLoadTexture(GetVehicleIcon(picture)));
                arg->preview->ShowCtrl(true);
              }
            }
          }
        }
      }
    }
  }

  // change subparameter controls
  Control *control = static_cast<Control *>(ctrl);
  IControl *grpCtrl = GetCtrl(IDC_EDIT_OBJECT_CONTROLS);
  if (control && grpCtrl && grpCtrl->GetType() == CT_CONTROLS_GROUP)
  {      
    CControlsGroup *grp = static_cast<CControlsGroup *>(grpCtrl);
    CCombo *combo = dynamic_cast<CCombo *>(control);

    const SubTypeContainer *subParamsContainer = arg->subTypeSelected;

    if (!subParamsContainer && combo)
    {
      // attempt to find subType that matches the new selection
      for (int i=0; i<_subTypes.Size(); i++)
        if (_subTypes[i]->paramSubType == combo->GetData(combo->GetCurSel()))
        {
          subParamsContainer = _subTypes[i];
          break;
        }
    }

    if (subParamsContainer && combo)
    {
      const EditorParams &subParams = subParamsContainer->subType->GetParams();

      // for changing position of other controls
      float heightChange = 0;

      // loop through subparams removing existing controls
      for (int i=0; i<subParams.Size(); i++)
      {
        RString subParamName = subParams[i].name;

        // remove old subparameter arguments and controls
        RefArray<EditArgument> arguments = _arguments;
        for (int j=0; j<_arguments.Size(); j++)
        {
          if (subParamName == _arguments[j]->name)
          {
            Control *control = _arguments[j]->control;
            
            // remove control + argument
            if (control)
            {
              for (int k=0; k<arguments.Size(); k++)
                if (arguments[k]->control == control)
                {
                  arguments.Delete(k);
                  break;
                }

              if (subParams[i].y < 0 && !subParams[i].hidden) // don't take into account controls that set their own position
                heightChange -= control->H() + 0.005;

              grp->RemoveControl(control);      
              grp->RemoveControl(_arguments[j]->title);      
            }               
            break;
          }     
        }
        _arguments = arguments;
        //-!
      }
      int argSize = _arguments.Size();
      //-!

      // height of first subparameter control...
      float top = control->Y() + control->H() + 0.005;

      // process init EH
      GameState *gstate = GWorld->GetGameState();
      int edhandle = gstate->BeginContext(_map->GetVariables()); // editor space
      GameVarSpace local(_map->GetVariables(), false);
      int lochandle = gstate->BeginContext(&local); // local space

      // initialization of variables
      gstate->VarSetLocal("_map", CreateGameControl(_map), true);
      for (int i=0; i<_arguments.Size(); i++)
      {
        RString name = RString("_control_") + _arguments[i]->name;
        gstate->VarSetLocal(name, CreateGameControl(_arguments[i]->control), true);
        name = RString("_control_title_") + _arguments[i]->name;
        gstate->VarSetLocal(name, CreateGameControl(_arguments[i]->title), true);
      }
      //-!

      // add new controls based on combo box selection
      int nSubTypes = _subTypes.Size();
      for (int i=0; i<nSubTypes; i++)
      {
        if (
          arg->name == _subTypes[i]->paramName &&
          combo->GetData(combo->GetCurSel()) == _subTypes[i]->paramSubType
        )
        {
          const EditorParams &subParams = _subTypes[i]->subType->GetParams();   
          unconst_cast(arg)->subTypeSelected = _subTypes[i];
          for (int j=0; j<subParams.Size(); j++)
          {
            const EditorParam *subParam = &subParams[j];

            // add control for subparameter
            float topBefore = top;
            int n = _arguments.Size();
            AddControl(grp,*subParam,top);                 
            if (_arguments.Size() > n)  // has control been created?
            {
              _arguments[n]->parentArgument = arg;

              // process init EH
              RString name = RString("_control_") + _arguments[n]->name;
              gstate->VarSetLocal(name, CreateGameControl(_arguments[n]->control), true);
              name = RString("_control_title_") + _arguments[i]->name;
              gstate->VarSetLocal(name, CreateGameControl(_arguments[i]->title), true);
            }
            heightChange += top - topBefore;
          }
          // process init EH
          for (int j=0; j<subParams.Size(); j++)
          {
            const EditorParam &param = subParams[j];
            if (param.source == EPSDialog && param.onInit.GetLength() > 0)
            {
              for (int i=0; i<_arguments.Size(); i++)
                if (_arguments[i]->name == param.name)
                {                    
                    gstate->VarSetLocal("_this", CreateGameControl(_arguments[i]->control), true);   
                    break;
                }
              gstate->Execute(param.onInit, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
            }
            gstate->VarSetLocal("_this", NOTHING, true);
          }
          break;
        }
      }
          
      gstate->EndContext(lochandle); // local space
      gstate->EndContext(edhandle); // editor space

      // reposition all combos below control      
      for (int i=0; i<argSize; i++)
      {
        Control *paramControl = _arguments[i]->control;
        if (paramControl->Y() > control->Y())
        {
          paramControl->Move(0, heightChange);
          _arguments[i]->title->Move(0, heightChange);
        }
      }
    }
    //-!
  }
  
  //-!

  RString handler;  
  const EditorParam *param = _object->GetType()->GetParams().Find(arg->name);
      
  // is this a control for a subparameter?
  const EditArgument *parentArg = arg->parentArgument;
  if (parentArg)
    param = parentArg->subTypeSelected->subType->GetParams().Find(arg->name);

  if (param) 
    handler = param->onChanged;
  if (handler.GetLength() == 0) return;

  // handling of onChanged event
  GameState *gstate = GWorld->GetGameState();
  int edhandle = gstate->BeginContext(_map->GetVariables()); // editor space
  GameVarSpace local(_map->GetVariables(), false);
  int lochandle = gstate->BeginContext(&local); // local space

  // initialization of variables
  gstate->VarSetLocal("_map", CreateGameControl(_map), true);
  gstate->VarSetLocal("_this", CreateGameControl(arg->control), true);
  for (int i=0; i<_arguments.Size(); i++)
  {
    RString name = RString("_control_") + _arguments[i]->name;
    gstate->VarSetLocal(name, CreateGameControl(_arguments[i]->control), true);
    name = RString("_control_title_") + _arguments[i]->name;
    gstate->VarSetLocal(name, CreateGameControl(_arguments[i]->title), true);
  }

  // executing of handler
  gstate->Execute(handler, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace

  gstate->EndContext(lochandle); // local space
  gstate->EndContext(edhandle); // editor space
}


class DisplayMissionLoad : public Display
{
protected:
  InitPtr<CListBoxContainer> _island;
  InitPtr<CListBoxContainer> _mission;
  bool _multiplayer;
  bool _keepDirectory;

public:
  DisplayMissionLoad(ControlsContainer *parent, bool multiplayer, bool keepDirectory);

  RString GetIsland() const;
  RString GetMission() const;

  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnLBSelChanged(IControl *ctrl, int curSel);

protected:
  void OnIslandChanged(RString name);
};

DisplayMissionLoad::DisplayMissionLoad(ControlsContainer *parent, bool multiplayer, bool keepDirectory)
: Display(parent)
{
  _enableSimulation = false;
  _enableDisplay = parent->DisplayEnabled();
  _multiplayer = multiplayer;
  _keepDirectory = keepDirectory;

  Load("RscDisplayMissionLoad");

  if (_island)
  {
    int sel = 0;
    int n = (Pars >> "CfgWorldList").GetEntryCount();
    for (int i=0; i<n; i++)
    {
      ParamEntryVal entry = (Pars >> "CfgWorldList").GetEntry(i);
      if (!entry.IsClass()) continue;
      RString name = entry.GetName();

#if _FORCE_DEMO_ISLAND
      RString demo = Pars >> "CfgWorlds" >> "demoWorld";
      if (stricmp(name, demo) != 0) continue;
#endif

      // Check if wrp file exists
      RString fullname = GetWorldName(name);
      if (!QFBankQueryFunctions::FileExists(fullname)) continue;

      int index = _island->AddString
      (
        Pars >> "CfgWorlds" >> name >> "description"
      );
      _island->SetData(index, name);

      if (stricmp(name, Glob.header.worldname) == 0) sel = index;
    }
    _island->SetCurSel(sel, false);
    OnIslandChanged(Glob.header.worldname);
  }

  if (_mission)
  {
    int sel = 0;
    for (int i=0; i<_mission->GetSize(); i++)
    {
      RString name = _mission->GetData(i);
      if (stricmp(name, Glob.header.filename) == 0) sel = i;
    }
    _mission->SetCurSel(sel);
  }
}

RString DisplayMissionLoad::GetIsland() const
{
  if (!_island) return RString();
  int sel = _island->GetCurSel();
  if (sel < 0) return RString();
  return _island->GetData(sel);
}

RString DisplayMissionLoad::GetMission() const
{
  if (!_mission) return RString();
  int sel = _mission->GetCurSel();
  if (sel < 0) return RString();
  return _mission->GetData(sel);
}

Control *DisplayMissionLoad::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_MISSION_LOAD_ISLAND:
    _island = GetListBoxContainer(ctrl);
    break;
  case IDC_MISSION_LOAD_MISSION:
    _mission = GetListBoxContainer(ctrl);
    break;
  }
  return ctrl;
}

void DisplayMissionLoad::OnLBSelChanged(IControl *ctrl, int curSel)
{
  if (ctrl->IDC() == IDC_MISSION_LOAD_ISLAND && curSel >= 0)
    OnIslandChanged(_island->GetData(curSel));
}

struct AddMissionContext
{
  CListBoxContainer *list;
  RString island;
};

static bool AddMission(const FileItem &file, AddMissionContext &ctx)
{
  // remove trailing '\'
  int n = file.path.GetLength();
  DoAssert(n > 0 && file.path[n - 1] == '\\');
  RString path(file.path, n - 1);

  // world and mission name
  const char *ptr = strrchr(path, '\\');
  if (!ptr) return false;  // avoid missions on root
  RString name(ptr + 1);
  // RString dir(path, ptr + 1 - path);
  ptr = strrchr(name, '.');
  if (!ptr) return false; // bad directory name
  RString world(ptr + 1);
  RString mission(name, ptr - name);

  if (stricmp(world, ctx.island) == 0)
  {
    int index = ctx.list->AddString(mission);
    ctx.list->SetData(index, mission);
  }

  return false; // continue with enumeration
}

void DisplayMissionLoad::OnIslandChanged(RString name)
{
  if (!_mission) return;
  _mission->ClearStrings();

  AddMissionContext ctx;
  ctx.list = _mission;
  ctx.island = name;
  RString missionsDir;
  if (_keepDirectory) missionsDir = GetMissionsDirectory();
  else missionsDir = GetUserDirectory() + (_multiplayer ? "MPMissions\\" : "Missions\\");
  ForMaskedFileR(missionsDir, "mission.biedi", AddMission, ctx);
  _mission->SetCurSel(0);
}

enum MissionEdPlacement
{
  MPNone,
  MPSingleplayer,
  MPMultiplayer
};

class DisplayMissionSave : public Display
{
protected:
  InitPtr<CEditContainer> _mission;
  InitPtr<CEditContainer> _title;
  InitPtr<CEditContainer> _description;
  InitPtr<CListBoxContainer> _placement;

public:
  DisplayMissionSave(ControlsContainer *parent, RString title, RString description);
  RString GetMission() const;
  RString GetTitle() const;
  RString GetDescription() const;
  MissionEdPlacement GetPlacement() const;
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
};

DisplayMissionSave::DisplayMissionSave(ControlsContainer *parent, RString title, RString description)
: Display(parent)
{
  _enableSimulation = false;
  _enableDisplay = parent->DisplayEnabled();

  Load("RscDisplayMissionSave");

  if (_mission)
  {
    _mission->SetText(Glob.header.filename);
  }
  if (_title)
  {
    _title->SetText(title);
  }
  if (_description)
  {
    _description->SetText(description);
  }
  if (_placement)
  {
    int index = _placement->AddString(LocalizeString(IDS_EXPORT_NONE));
    _placement->SetValue(index, MPNone);
    index = _placement->AddString(LocalizeString(IDS_EXPORT_SINGLE));
    _placement->SetValue(index, MPSingleplayer);
    index = _placement->AddString(LocalizeString(IDS_EXPORT_MULTI));
    _placement->SetValue(index, MPMultiplayer);
    _placement->SetCurSel(0);
  }
}

RString DisplayMissionSave::GetMission() const
{
  if (!_mission) return RString();
  return _mission->GetText();
}

RString DisplayMissionSave::GetTitle() const
{
  if (!_title) return RString();
  return _title->GetText();
}

RString DisplayMissionSave::GetDescription() const
{
  if (!_description) return RString();
  return _description->GetText();
}

MissionEdPlacement DisplayMissionSave::GetPlacement() const
{
  if (!_placement) return MPNone;
  int sel = _placement->GetCurSel();
  if (sel < 0) return MPNone;
  return (MissionEdPlacement)_placement->GetValue(sel);
}

Control *DisplayMissionSave::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_MISSION_SAVE_MISSION:
    _mission = GetEditContainer(ctrl);
    break;
  case IDC_MISSION_SAVE_TITLE:
    _title = GetEditContainer(ctrl);
    break;
  case IDC_MISSION_SAVE_DESCRIPTION:
    _description = GetEditContainer(ctrl);
    break;
  case IDC_MISSION_SAVE_PLACEMENT:
    _placement = GetListBoxContainer(ctrl);
    break;
  }
  return ctrl;
}

class DisplayOverlayCreate : public Display
{
protected:
  InitPtr<CEditContainer> _name;
  ConstParamEntryPtr _overlayType;
  RString _overlayTypeDesc, _overlayTypeName;

public:
  DisplayOverlayCreate(ControlsContainer *parent, ConstParamEntryPtr overlayType = NULL);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  RString GetName() const;
};

DisplayOverlayCreate::DisplayOverlayCreate(ControlsContainer *parent, ConstParamEntryPtr overlayType)
:Display(parent)
{
  _enableSimulation = false;
  _enableDisplay = false;

  _overlayType = overlayType;
  if (_overlayType)
  {
    _overlayTypeDesc = *_overlayType >> "name";
    _overlayTypeName = overlayType->GetName();
  }
  _overlayTypeName.Lower();

  Load("RscDisplayOverlayCreate");
}

static bool OverlayExists(const FileItem &file, RString &ctx)
{
  RString name = file.filename;
  const char *ptr = strrchr(name, '.');
  if (!ptr) return false;
  RString overlay(name, ptr - name);
  return stricmp(overlay,ctx) == 0;
}

void DisplayOverlayCreate::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_OK:
    {
      // check if an overlay of the same name already exists
      if (ForMaskedFile(GetUserDirectory() + RString("Overlays\\"), "*." + _overlayTypeName + ".biedi", OverlayExists, GetName()))
      {
        GWorld->CreateWarningMessage(LocalizeString("STR_EDITOR_ERROR_OVERLAY_EXISTS"));
        return;
      }
      else if (_name->GetText().GetLength() == 0)
      {
        GWorld->CreateWarningMessage(LocalizeString("STR_EDITOR_ERROR_INVALID_OVERLAY_NAME"));
        return;
      }
    }
    break;
  }
  Display::OnButtonClicked(idc);
}

Control *DisplayOverlayCreate::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_OVERLAY_CREATE_NAME:
    _name = GetEditContainer(ctrl);
    if (_name)
    {
      RString filename;
      RString initialName("Overlay_"); 
      int i = 0;
      do
      {
        filename = GetUserDirectory() + RString("Overlays\\") + initialName + Format("%d",++i) + "." + _overlayTypeName + ".biedi";
      } while (QIFileFunctions::FileExists(filename));
      filename = initialName + Format("%d",i);
      _name->SetText(filename);
      _name->SetBlock(0,filename.GetLength());
    }
    break;
  }
  return ctrl;
}

RString DisplayOverlayCreate::GetName() const
{
  if (!_name) return RString();
  return EncodeFileName(_name->GetText()) + "." + _overlayTypeName;
}

class DisplayOverlayLoad : public Display
{
protected:
  InitPtr<CListBoxContainer> _overlay;
  ConstParamEntryPtr _overlayType;
  RString _overlayTypeDesc, _overlayTypeName;

public:
  DisplayOverlayLoad(ControlsContainer *parent, ConstParamEntryPtr overlayType = NULL);
  RString GetOverlay() const;
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
};

struct AddOverlayContext
{
  CListBoxContainer *list;
};

static bool AddOverlay(const FileItem &file, AddOverlayContext &ctx)
{
  RString name = file.filename;
  const char *ptr = strrchr(name, '.');
  if (!ptr) return false;
  RString overlay(name, ptr - name);

  ptr = strrchr(overlay, '.');
  RString overlayName = RString(overlay, ptr - overlay);

  int index = ctx.list->AddString(overlayName);
  ctx.list->SetData(index, overlay);

  return false; // continue with enumeration
}

DisplayOverlayLoad::DisplayOverlayLoad(ControlsContainer *parent, ConstParamEntryPtr overlayType)
: Display(parent)
{
  _enableSimulation = false;
  _enableDisplay = false;

  _overlayType = overlayType;
  if (_overlayType)
  {
    _overlayTypeDesc = *_overlayType >> "name";
    _overlayTypeName = overlayType->GetName();
  }
  _overlayTypeName.Lower();

  Load("RscDisplayOverlayLoad");

  if (_overlay)
  {
    AddOverlayContext ctx;
    ctx.list = _overlay;
    ForMaskedFile(GetUserDirectory() + RString("Overlays\\"), "*." + _overlayTypeName + ".biedi", AddOverlay, ctx);
    _overlay->SetCurSel(0);
  }
}

RString DisplayOverlayLoad::GetOverlay() const
{
  if (!_overlay) return RString();
  int sel = _overlay->GetCurSel();
  if (sel < 0) return RString();
  return _overlay->GetData(sel);
}

Control *DisplayOverlayLoad::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_OVERLAY_LOAD_NAME:
    _overlay = GetListBoxContainer(ctrl);
    break;
  }
  return ctrl;
}

// for processing click eventhandlers
#if _VBS2
static void ProcessOnClick(RString command, Vector3Par pos, InitPtr<CStaticMapMain> map)
#else
static void ProcessOnClick(RString command, Vector3Par pos, Ref<CStaticMapEditor> map)
#endif
{
  bool alt = GInput.keys[DIK_LALT]>0;
  bool shift = GInput.keys[DIK_LSHIFT]>0;
 
  GameState *state = GWorld->GetGameState();

  GameValue posValue = state->CreateGameValue(GameArray);
  GameArrayType &posArray = posValue;
  posArray.Realloc(3);
  posArray.Add(pos[0]);
  posArray.Add(pos[2]);
  posArray.Add(pos[1]-GLandscape->SurfaceYAboveWater(pos[0],pos[2]));

  GameVarSpace vars(false);
  int gshandle = state->BeginContext(&vars);
  state->VarSetLocal("_map", CreateGameControl(map), true);
  state->VarSetLocal("_pos",posArray,true);
  state->VarSetLocal("_alt",alt,true);
  state->VarSetLocal("_shift",shift,true);
  state->EvaluateMultiple(command, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
  state->EndContext(gshandle);
}

CStaticMapEditor::CStaticMapEditor(
  ControlsContainer *parent, int idc, ParamEntryPar cls,
  float scaleMin, float scaleMax, float scaleDefault,
  EditorWorkspace *ws, GameVarSpace *vars)
#if _VBS2
  : CStaticMapMain(parent, idc, cls, scaleMin, scaleMax, scaleDefault), _ws(ws), _vars(vars)
#else
  : CStaticMap(parent, idc, cls, scaleMin, scaleMax, scaleDefault), _ws(ws), _vars(vars)
#endif
{
  _dragging = false;
  _draggingRM = false;
  _selecting = false;  
  _dragPos = VZero;
  _linkShortcutKey = -1;

  _iconCamera = GlobLoadTexture(GetPictureName(cls >> "iconCamera"));
  _iconCameraColor = GetPackedColor(cls >> "iconCameraColor");
  _iconCameraSize = cls >> "iconCameraSize";
}

bool CStaticMapEditor::HasParent()
{
  return GetParent() ? true : false;
}

void CStaticMapEditor::Center(bool soft)
{
  Object *obj = GWorld->CameraOn();
  if (obj)
    CStaticMap::Center(obj->FutureVisualState().Position(), soft);
  else
    CStaticMap::Center(_defaultCenter, soft);
}

void CStaticMapEditor::DrawLabel(struct SignInfo &info, PackedColor color, int shadow)
{
  if (info._type == signEditorObject)
  {
    const EditorObject *obj = _ws->FindObject(info._name);
    if (obj)
    {      
      RString name = obj->GetDisplayName();
      if (name.GetLength() == 0)
      {
        RString varName = obj->GetArgument("VARIABLE_NAME");
        RString typeName = obj->GetType()->GetName();
        name = Format("%s \"%s\"", (const char *)typeName, (const char *)varName);
      }
      CStaticMap::DrawLabel(info._pos, name, color, shadow);
    }
  }
}

static void FindSignRecurseChildren(const EditorObject *obj, const EditorWorkspace *ws, SignInfo &info, float &minDist2, Vector3Par pos, GameState *gstate, Object *parentProxy = NULL)
{
  RString varName = obj->GetArgument("VARIABLE_NAME");
  Object *proxy = obj->GetProxyObject().GetRef();
  if (
    obj->IsVisible() && 
    obj->GetScope() != EOSAllNoSelect && 
    obj->IsObjInCurrentOverlay() &&
    (
      !proxy ||
      !parentProxy ||
      proxy != parentProxy // don't recurse if the child shares the same proxy as the parent
    )
  )
  {
    GameValue result = obj->GetArgumentValue("POSITION");
    if (!result.GetNil() && result.GetType() == GameArray)
    {
      const GameArrayType &array = result;
      if (array.Size() >= 2)
      {
        if (array[0].GetType() == GameScalar && array[1].GetType() == GameScalar)
        {
          float x = array[0], y = array[1];
          float dist2 = Square(pos.X() - x) + Square(pos.Z() - y);
          if (dist2 < minDist2)
          {
            minDist2 = dist2;
            info._type = signEditorObject;
            info._name = obj->GetArgument("VARIABLE_NAME");
            info._pos = Vector3(x, 0, y);
          }
        }
      }
    }
  }

  // child objects
  for (int i=0; i<ws->NObjects(); i++)
  {
    const EditorObject *child = ws->GetObject(i);    
    if (obj->GetScope() == EOSAllNoSelect) continue;
    if (stricmp(child->GetParent(), varName) == 0) FindSignRecurseChildren(child, ws, info, minDist2, pos, gstate, obj->GetProxyObject().GetRef());
  }
}

SignInfo CStaticMapEditor::FindSign(float x, float y)
{
  struct SignInfo info;
  info._type = signNone;
  if (!_ws) return info;

  Vector3 pos = ScreenToWorld(DrawCoord(x, y));
  const float sizeLand = LandGrid * LandRange;
  float minDist2 = Square(0.02 * sizeLand * _scaleX);

  GameState *gstate = GWorld->GetGameState(); 
  int gshandle = gstate->BeginContext(_vars);

  // find sign to assign higher priority to objects higher in the tree
    // ensures teams are selected before agents or vehicles
  for (int i=0; i<_ws->NObjects(); i++)
  {
    const EditorObject *obj = _ws->GetObject(i);
    if (obj->GetParent().GetLength() == 0)
      FindSignRecurseChildren(obj, _ws, info, minDist2, pos, gstate);
  }

  gstate->EndContext(gshandle);

  return info;
}

void CStaticMapEditor::OnDraw(UIViewport *vp, float alpha)
{
  CStaticMap::OnDraw(vp, alpha);

  // CStaticMap::OnDraw prevents keyboard for working outside of the main map
    // this enables zoom by keyboard in realtime
  if (GetParent()->IsRealTime())
  {
    float dt = Glob.uiTime - _moveLast;
    saturateMin(dt, 0.1); // minimal fps 10 - avoid jumps of cursor
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
    }
  }

  if (GetParent() && GetParent()->IsTop() && !GetParent()->IsRealTime())  // can't use CStaticMap version because camera effect is active
  {
    // draw lines around tracking cursor
    RString cursorName = GetParent()->GetCursorName();
    if (cursorName.GetLength() > 0 && stricmp(cursorName, "Arrow") != 0)
    {
      int wScreen = GLOB_ENGINE->Width2D();
      int hScreen = GLOB_ENGINE->Height2D();
      const float mouseH = 16.0 / 600;
      const float mouseW = 16.0 / 800;
      float mouseX = 0.5 + GInput.cursorX * 0.5;
      float mouseY = 0.5 + GInput.cursorY * 0.5;
      PackedColor color = GetParent()->GetCursorColor();
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
  DrawLegend();
}

void CStaticMapEditor::DrawExt(float alpha)
{
  if (!_ws || !_vars) return;

  GameState *gstate = GWorld->GetGameState();
  int gshandle = gstate->BeginContext(_vars);
  gstate->VarSetLocal("_map", CreateGameControl(this), true);

  // pass scale so script may do icon autosize if it wishes
  float scale = _scaleX / (InvLandSize * 1000);
  gstate->VarSetLocal("_scale", GameValue(scale), false);  

  bool updateVisibility = Glob.uiTime - _lastObjectVisibilityUpdate > 0.1;
  if (updateVisibility)
    _lastObjectVisibilityUpdate = Glob.uiTime;

  // draw camera position
  if (_iconCamera)
  {
    Object *camera = GetParent()->CameraGet();
    if (camera)
    {
      Vector3 pos = camera->FutureVisualState().Position();
      Vector3 dir = camera->FutureVisualState().Direction();
      float azimut = atan2(dir.X(), dir.Z());
      DrawSign(_iconCamera, _iconCameraColor, pos, _iconCameraSize, _iconCameraSize, azimut);
    }
  }

  // execute draw scripts
  for (int i=0; i<_ws->NObjects(); i++)
  {
    EditorObject *obj = _ws->GetObject(i);    

    if (updateVisibility)
    {
      obj->UpdateVisibleOnMap(gstate);
      obj->UpdateExecDrawMap(gstate);
    }

    obj->UpdateEvaluatedPos();

    if (!obj->IsVisible()) continue;
    
    // clipping
    Vector3 pos = obj->GetEvaluatedPos();
    DrawCoord drawPos = WorldToScreen(pos);

    #define SCREEN_BUFFER 0.25 
    if (
      drawPos.x < _x - SCREEN_BUFFER ||
      drawPos.x > _x + _w + SCREEN_BUFFER ||
      drawPos.y < _y - SCREEN_BUFFER ||
      drawPos.y > _y + _h + SCREEN_BUFFER
      ) continue;    

    float alpha = 1.0f;
    if (!obj->IsObjInCurrentOverlay()) alpha = 0.1f;

    if (obj->IsSelected())
    {
      const float period = 0.5f;
      const float speed = H_PI / period;
      alpha = 0.33 * sin(speed * Glob.uiTime.toFloat()) + 0.67;
    }
    
    // draw object icons
    int n = obj->NIcons();
    for (int j=0; j<n; j++)
    {
      EditorObjectIcon &icon = obj->GetIcon(j);
      if (!icon.is3D)
      {
        PackedColor color = icon.color;
        color.SetA8(toIntFloor(color.A8()*alpha));
        float iconScale = scale < icon.minScale ? icon.minScale : scale;
        float w = icon.maintainSize ? icon.width : icon.width * 1 / iconScale;
        float h = icon.maintainSize ? icon.height : icon.height * 1 / iconScale;
        w = !icon.maintainSize && w < 5 ? 5 : w;
        h = !icon.maintainSize && h < 5 ? 5 : h;
        DrawSign(icon.icon,color,Vector3(pos.X() + icon.offset.X(),0,pos.Z() + icon.offset.Z()),w,h,icon.angle,icon.text,NULL,-1,icon.shadow);
      }
    }

    if (!obj->ExecDrawMap()) continue; 

    // draw object
    gstate->VarSetLocal("_alpha", GameValue(alpha), true);
    obj->ExecuteDrawMapScript(gstate);
  }
  gstate->EndContext(gshandle);

  // draw links
  for (int i=0; i<_objectLinks.Size(); i++)
  {    
    ObjectLink &objectLink = _objectLinks[i];
    if (objectLink.to && objectLink.from)
    {
      if (objectLink.from->IsVisible() && objectLink.to->IsVisible())
      {
        Vector3 pos1 = objectLink.to->GetEvaluatedPos();
        Vector3 pos2 = objectLink.from->GetEvaluatedPos();
        if (_objectLinks[i].lineType == LTLine)
          DrawLine(pos2, pos1, objectLink.color);
        else
          DrawArrow(pos2, pos1, objectLink.color);
      }
    }
    else
      _objectLinks.Delete(i--);
  }

  // draw arrow if linking objects
  if (_linkObject)
  {
    GameValue result = _linkObject->GetArgumentValue("POSITION");
    if (!result.GetNil() && result.GetType() == GameArray)
    {
      const GameArrayType &array = result;
      if (array.Size() >= 2 && array[0].GetType() == GameScalar && array[1].GetType() == GameScalar)
      {
        Vector3 objPos(array[0], 0, array[1]);
        DrawArrow(objPos, _dragPos, PackedBlack);
      }
    }
  }
  // draw multiple select box
  else if (_selecting)
  {
    // PackedColor color = PackedColor(Color(0,1,0,1));
    PackedColor color = _parent->GetCursorColor();

    DrawCoord pt1 = WorldToScreen(_startPos);
    DrawCoord pt2 = WorldToScreen(_dragPos);
    GEngine->DrawLine
    (
      Line2DPixel(pt1.x * _wScreen, pt1.y * _hScreen,
      pt2.x * _wScreen, pt1.y * _hScreen),
      color, color, _clipRect
    );
    GEngine->DrawLine
    (
      Line2DPixel(pt2.x * _wScreen, pt1.y * _hScreen,
      pt2.x * _wScreen, pt2.y * _hScreen),
      color, color, _clipRect
    );
    GEngine->DrawLine
    (
      Line2DPixel(pt2.x * _wScreen, pt2.y * _hScreen,
      pt1.x * _wScreen, pt2.y * _hScreen),
      color, color, _clipRect
    );
    GEngine->DrawLine
    (
      Line2DPixel(pt1.x * _wScreen, pt2.y * _hScreen,
      pt1.x * _wScreen, pt1.y * _hScreen),
      color, color, _clipRect
    );
  }
  
  // update help text
  if (GetParent() && _ws)
  {
    if (_infoMove._type == signEditorObject)
    {
      EditorObject *obj = _ws->FindObject(_infoMove._name);
      if (obj)
        if (obj->IsSelected())
          GetParent()->SetSelectedStatus(ESSelected);
        else
          GetParent()->SetSelectedStatus(ESUnSelected);
    }
    else
      GetParent()->SetSelectedStatus(ESNone);
  }
  //-!

  PackedColor colorLabel(Color(1, 1, 1, 1));
  DrawLabel(_infoMove, colorLabel,0);
}

Object *CStaticMapEditor::FindObject(RString id)
{
  if (GetParent())
    return GetParent()->FindObject(id);
  return NULL;
}

void CStaticMapEditor::DeleteObjectFromEditor(RString id)
{
  if (GetParent())
    GetParent()->DeleteObjectFromEditor(id);
};

bool CStaticMapEditor::IsRealTime() 
{
  if (GetParent())
    return GetParent()->IsRealTime();
  return false;
}

Object *CStaticMapEditor::CameraGet()
{
  if (GetParent())
    return GetParent()->CameraGet();
  return NULL;
}

void CStaticMapEditor::CameraRestart()
{
  if (GetParent())
    GetParent()->CameraRestart();
}

void CStaticMapEditor::UpdateProxy(Object *object, EditorObject *edObj) 
{
  if (GetParent())
    GetParent()->UpdateProxy(object,edObj);
}

void CStaticMapEditor::DeleteProxy(EditorObject *edObj)
{
  if (GetParent())
    GetParent()->DeleteProxy(edObj);
}

void CStaticMapEditor::SetOnDoubleClick(RString eh)
{
  if (GetParent())
    GetParent()->SetOnDoubleClick(eh);  
}

void CStaticMapEditor::SetOnShowNewObject(RString eh)
{
  if (GetParent())
    GetParent()->SetOnShowNewObject(eh);  
}

void CStaticMapEditor::UpdateObjectBrowser() 
{
  if (GetParent()) 
    GetParent()->UpdateObjectBrowser();
}

void CStaticMapEditor::LookAtPosition(Vector3Par pos) 
{
  if (GetParent()) 
    GetParent()->LookAtPosition(pos);
}

#if _VBS2
void CStaticMapEditor::ImportAllGroups() 
{
  if (GetParent()) 
    GetParent()->ImportAllGroups();
}
#endif

int CStaticMapEditor::AddMenu(RString text, float priority)
{
  if (GetParent()) 
    return GetParent()->AddMenu(text, priority);
  return -1;
}

void CStaticMapEditor::RemoveDrawLinks(EditorObject *from, RString linkName)
{
  for (int i=0; i<_objectLinks.Size(); i++)
    if (_objectLinks[i].from == from && (linkName.GetLength() == 0 || stricmp(_objectLinks[i].linkName,linkName) == 0))
      _objectLinks.Delete(i--);
}

void CStaticMapEditor::RemoveAllDrawLinks(EditorObject *from)
{
  for (int i=0; i<_objectLinks.Size(); i++)
    if (_objectLinks[i].from == from || _objectLinks[i].to == from)
      _objectLinks.Delete(i--);
}

void CStaticMapEditor::SetDrawLink(EditorObject *from, EditorObject *to, RString linkName, LineType lineType, PackedColor color)
{
  // disallow duplicate links
  for (int i=0; i<_objectLinks.Size(); i++)
    if (_objectLinks[i].from == from && _objectLinks[i].to == to && stricmp(_objectLinks[i].linkName,linkName) == 0)
      return;

  int i = _objectLinks.Add();
  ObjectLink &objectLink = _objectLinks[i];
  objectLink.from = from;
  objectLink.to = to;
  objectLink.linkName = linkName;
  objectLink.lineType = lineType;
  objectLink.color = color;  
}

void CStaticMapEditor::NewOverlay(ConstParamEntryPtr entry)
{
  if (GetParent()) 
    GetParent()->NewOverlay(entry);
}

void CStaticMapEditor::LoadOverlay(ConstParamEntryPtr entry)
{
  if (GetParent()) 
    GetParent()->LoadOverlay(entry);
}

void CStaticMapEditor::SaveOverlay()
{
  if (GetParent()) 
    GetParent()->SaveOverlay();
}

void CStaticMapEditor::ClearOverlay(bool commit, bool close)
{
  if (GetParent()) 
    GetParent()->ClearOverlay(commit, close);
}

void CStaticMapEditor::CreateMenu(int index)
{
  if (GetParent()) 
    GetParent()->CreateMenu(index);
}

int CStaticMapEditor::AddMenuItem(RString type, RString text, RString command, int index, float priority, bool assignPriority)
{
  if (GetParent()) 
    return GetParent()->AddMenuItem(type, text, command, index, priority, assignPriority);
  return -1;
}

void CStaticMapEditor::UpdateMenuItem(int index, RString text, RString command) 
{
  if (GetParent()) 
    GetParent()->UpdateMenuItem(index, text, command);
}

void CStaticMapEditor::RemoveMenuItem(int index, RString text)
{
  if (GetParent()) 
    return GetParent()->RemoveMenuItem(index,text);
}

int CStaticMapEditor::NMenuItems(RString text, int index)
{
  if (GetParent()) 
    return GetParent()->NMenuItems(text,index);
  return 0;
}

int CStaticMapEditor::GetNextMenuItemIndex()
{
  if (GetParent()) 
    return GetParent()->GetNextMenuItemIndex();
  return -1;
}

void CStaticMapEditor::AllowFileOperations(bool allow)
{
  if (GetParent()) 
    GetParent()->AllowFileOperations(allow);
}

void CStaticMapEditor::Allow3DMode(bool allow)
{
  if (GetParent()) 
    GetParent()->Allow3DMode(allow);
}

bool CStaticMapEditor::IsShow3DIcons()
{
  if (GetParent()) 
    return GetParent()->IsShow3DIcons();
  return false;
}

void CStaticMapEditor::Show3DIcons(bool show)
{
  if (GetParent()) 
    GetParent()->Show3DIcons(show);
}

EditorMode CStaticMapEditor::GetMode()
{
  if (GetParent()) 
    return GetParent()->GetMode();
  return EMMap;
}

void CStaticMapEditor::SetMode(EditorMode mode)
{
  if (GetParent()) 
    GetParent()->SetMode(mode);
}

void CStaticMapEditor::SetEditorEventHandler(RString name, RString value)
{
  if (GetParent()) 
    GetParent()->SetEditorEventHandler(name,value);
}

void CStaticMapEditor::SelectObject(EditorObject *obj) 
{
  if (_ws)
    _ws->SetSelected(obj,true); // note - will not fire SelectObject EH
}

void CStaticMapEditor::ShowEditObject(RString id)
{
  if (GetParent())
    GetParent()->ShowEditObject(id);
}

void CStaticMapEditor::ShowNewObject(RString type, RString className, TargetSide side, Vector3Par posn)
{
  if (GetParent())
  {
    if (className.GetLength() > 0)
      GetParent()->ClearLastObjectCreated();

    GetParent()->SelectEditorObject(type);
    GetParent()->ShowNewObject(posn,NULL,RString(),className,side);
  }
};

RString CStaticMapEditor::AddObject(RString typeName, const AutoArray<RString> &args, bool create, GameValue variable, ConstParamEntryPtr subTypeConfig)
{
  ProgressRefresh();
  EditorObjectType *type = _ws->FindObjectType(typeName);
  if (type && GetParent())
  { 
    EditorObject *obj = _ws->AddObject(type);

    // assign subtype
    if (subTypeConfig)
    {
      Ref<EditorObjectType> subType = new EditorObjectType(RString(),subTypeConfig);
      obj->AssignSubType(subType,subTypeConfig->GetName());
    }

    obj->SetArguments(args);

    // set default arguments
    const EditorParams &params = obj->GetAllParams();
    for (int i=0; i<params.Size(); i++)
    {
      const EditorParam *param = &params.Get(i);
      if (param->hasDefValue)
      {
        // check if already added      
        if (obj->GetArgument(param->name).GetLength() == 0)
          obj->SetArgument(param->name,param->defValue);
      }
    }

    if (create)
      GetParent()->CreateObject(obj,true);    
    else
    {
      GameState *gstate = GWorld->GetGameState();

      // so variable name (eg _team_1) is visible as %VARIABLE_NAME in editor definitions
      _vars->VarSet(obj->GetArgument("VARIABLE_NAME"), variable, true);

      // set initial visibility (hide all but the parent)
      if (obj->GetArgument("PARENT").GetLength() > 0)
        obj->SetVisibleInTree(false);

      RString statement = obj->GetProxy();
      GameValue result;

      // add object    
      int gshandle = gstate->BeginContext(_vars);
      if (statement.GetLength() > 0) result = gstate->Evaluate(statement, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
      gstate->EndContext(gshandle);

      // register proxy
      if (!result.GetNil() && result.GetType() == GameObject)
      {
        Object *object = static_cast<GameDataObject *>(result.GetData())->GetObject();
        if (object)
        {
          GetParent()->AddProxy(object,obj);
          obj->SetProxyObject(object);
        }
      }
    }
    //GetParent()->UpdateObjectBrowser();
    return obj->GetArgument("VARIABLE_NAME");
  }
  return RString();
}

void CStaticMapEditor::EditLink(EditorObject *obj, RString name, float key)
{
  if (obj)
    if (obj->GetScope() < EOSLinkFrom) return;
  _linkObject = obj;
  _linkArgument = name;
  _linkShortcutKey = key;
  float x = 0.5 + GInput.cursorX * 0.5;
  float y = 0.5 + GInput.cursorY * 0.5;
  _dragPos = ScreenToWorld(Point2DFloat(x, y));  
}

/// translate position to the object argument
static RString FormatPosition(Vector3Par pos)
{
  // in scripting commands, positions are relative to surface above water
  float y = pos.Y() - GLandscape->SurfaceYAboveWater(pos.X(), pos.Z());
  return Format("[%.8g, %.8g, %.8g]", pos.X(), pos.Z(), y);
}

void CStaticMapEditor::OnLButtonDown(float x, float y)
{
  _infoClick = FindSign(x, y);

  // linking one object to another?
  if (_linkObject)
  {    
    if (GetParent())
    {
      const EditorParam *param = _linkObject->GetAllParams().Find(_linkArgument);

      // holding down shortcut key and linking to nothing?
        // search only for context link type with the matching shortcut key
      if (!param && _infoClick._type != signEditorObject)
      {
        const EditorParams &params = _linkObject->GetAllParams();
        int nParams = params.Size();
        for (int i=0; i<nParams; i++)
        {
          if (params[i].source == EPSContext && params[i].shortcutKey == _linkShortcutKey)
          {
            param = &params[i];
          }
        }    
      }
      
      // link to new object?
      if (param && param->source == EPSContext)
      {
        const char *ptr = param->subtype;
        const char *ext = strchr(ptr, ',');

        // editor object type
        RString objectType = Trim(ptr, ext - ptr);
               
        // link type
        ptr = ext + 1;
        RString linkType = Trim(ptr, strlen(ptr));

        // is there a type in memory that matches the one we are trying to add?
        bool showNewObj = true;
        if (GetParent()->LastObjectCreated())
        {
          EditorObjectType *type = GetParent()->LastObjectCreated()->GetType();
          if (type)
          {
            if (stricmp(objectType,type->GetName()) == 0)
            {
              // try and find a suitable link parameter that we can use to link the new (copied) object with the linkObject
                // look for the parameter with the same shortcut key TODO: is there a better way?
              const EditorParams &params = _linkObject->GetAllParams();
              int nParams = params.Size();
              for (int i=0; i<nParams; i++)
              {
                if (
                  params[i].source == EPSLink && 
                  params[i].shortcutKey == _linkShortcutKey &&
                  stricmp(params[i].type,type->GetName()) == 0
                )
                {                  
                  _linkArgument = params[i].name;
                  showNewObj = false;
                }
              }                  
            }
          }
        }

        // set correct object type (matching name)
        if (showNewObj)
        {
          if (GetParent()->SelectEditorObject(objectType))
          GetParent()->ShowNewObject(ScreenToWorld(DrawCoord(x, y)),_linkObject,linkType);          
        }
        else
        {
          // create the new object based on what is in memory, and link to it
          EditorObject *obj = _ws->CopyObject(GetParent()->LastObjectCreated());
          if (obj)
          {
            // assign new name (because old name has been copied over)
            EditorObjectType *type = obj->GetType();
            if (type)
            {        
              obj->RemoveArgument("VARIABLE_NAME");        
              obj->SetArgument("VARIABLE_NAME", type->NextVarName());
            }

            GetParent()->CreateObject(obj,false);

            // convert position back to string
            Vector3 clickPos = ScreenToWorld(Point2DFloat(x, y));
            clickPos[1] = GLandscape->RoadSurfaceYAboveWater(clickPos[0], clickPos[2]);
            RString value = FormatPosition(clickPos);

            // update argument - position
            obj->RemoveArgument("POSITION");
            obj->SetArgument("POSITION", value);

            GetParent()->UpdateObjectBrowser();
            
            RString id = obj->GetArgument("VARIABLE_NAME");
            GetParent()->SelectObject(id);

            GetParent()->OnLinkEditted(_linkObject, _linkArgument, id, -1);       
          }
        }
      }
      else
      {
        RString name;

        // link to position?
        if (param && param->source == EPSLink && stricmp(param->type, "position") == 0)
        {
          Vector3 clickPos = ScreenToWorld(Point2DFloat(x, y));
          clickPos[1] = GLandscape->RoadSurfaceYAboveWater(clickPos[0], clickPos[2]);
          name = FormatPosition(clickPos);
        }
        // link to existing object?
        else if (_infoClick._type == signEditorObject) 
          name = _infoClick._name;        

        // do linkage
        GetParent()->OnLinkEditted(_linkObject, _linkArgument, name, _linkShortcutKey);       
      }
    }
    
    _linkObject = NULL;
    _linkShortcutKey = -1;
    if (GetParent()) GetParent()->UpdateControlsHelp();
  }
  // left mouse down on an editor object?
  else if (_infoClick._type == signEditorObject)
  {
    EditorObject *obj = NULL;

    // select object
    if (GetParent() && _ws)
    {
      obj = _ws->FindObject(_infoClick._name);      
      if (obj)
      {
        // select object in object browser
        GetParent()->OnObjectSelected(_infoClick._name);

        // select object if not selected
        if (!obj->IsSelected())
          GetParent()->SelectObject(_infoClick._name);          
        // ... unless control is held down, in which case we unselect the object
        else
          if (GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL])
            GetParent()->UnSelectObject(_infoClick._name);        
      }
    }

    // was the link shortcut key pressed (held down)?
    if (obj)
    {
      // search for shortcut keys
      const EditorParams &params = obj->GetAllParams();
      int nParams = params.Size();
      for (int i=0; i<nParams; i++)
      {
        if ((params[i].source != EPSLink && params[i].source != EPSContext && params[i].source !=EPSParent) || params[i].shortcutKey < 0) continue;
        if (GInput.keys[params[i].shortcutKey] > 0)
        {
          EditLink(obj, RString(),params[i].shortcutKey);
          return;
        }
      }      
    }
    //-!

    _dragging = true;
    _selecting = false;
    _dragPos = ScreenToWorld(Point2DFloat(x, y));
  }
  _startPos = ScreenToWorld(Point2DFloat(x, y)); 
}

void CStaticMapEditor::OnLButtonUp(float x, float y)
{
  if (_selecting && _ws && GetParent())
  {
    _selecting = false;

    if (!GInput.keys[DIK_LCONTROL] && !GInput.keys[DIK_RCONTROL])
      GetParent()->ClearSelected();

    float xMin = floatMin(_startPos.X(), _dragPos.X());
    float xMax = floatMax(_startPos.X(), _dragPos.X());
    float zMin = floatMin(_startPos.Z(), _dragPos.Z());
    float zMax = floatMax(_startPos.Z(), _dragPos.Z());

    for (int i=0; i<_ws->NObjects(); i++)
    {
      EditorObject *obj = _ws->GetObject(i);
      if (!obj->IsVisibleInTree()) continue;

      Vector3 objPos;
      if (obj->GetEvaluatedPosition(objPos))
      {
        if
        (
          objPos.X() >= xMin && objPos.X() < xMax &&
          objPos.Z() >= zMin && objPos.Z() < zMax
        ) 
        {
          _ws->SetSelected(obj, !obj->IsSelected(), true, true, true);
          if (GetParent()) GetParent()->UpdateControlsHelp();
        }
      }
    }
  }
  else if (!_dragging)
  {
    _infoClick = _infoMove = FindSign(x, y);
    if (GetParent() && _infoClick._type != signEditorObject)
      GetParent()->ClearSelected();
  }
  else if (_dragging && GetParent())
    GetParent()->UpdateRemoteSelObjects();

  if (GetParent()) 
  {
    GetParent()->SetRotating(false);
    GetParent()->SetRaising(false);
  }

  _dragging = false;
}

static RString GetMapOnSingleClick()
{
  if (GMapOnSingleClick.GetNil()) return RString();
#if USE_PRECOMPILATION
  if (GMapOnSingleClick.GetType() == GameCode)
  {
    GameDataCode *code = static_cast<GameDataCode *>(GMapOnSingleClick.GetData());
    return code->GetString();
  }
  else
#endif
  {
    return GMapOnSingleClick;
  }
}

void CStaticMapEditor::OnLButtonClick(float x, float y)
{
  // process onMapSingleClick    
  if (!_dragging)
  {
    RString eventHandler = GetMapOnSingleClick();
    if (eventHandler.GetLength()>0)
      ProcessOnClick(eventHandler, ScreenToWorld(DrawCoord(x, y)), this);
  
    // start drawing multiple select box?
    if (GInput.mouseL)
    {
      _dragging = false;
      _selecting = true;
      // startpos is defined when the mouse is first depressed in LButtonDown
    }
  }
}

// TODO: remove new object resource & code
void CStaticMapEditor::OnLButtonDblClick(float x, float y)
{
  _infoClick = _infoMove = FindSign(x, y);
  if (GetParent())
  {    
    if (GInput.keys[DIK_LSHIFT]>0) return;
    if (!_infoMove._type)
    {
      Vector3 pos = ScreenToWorld(DrawCoord(x, y));
      pos[1] = GLandscape->RoadSurfaceYAboveWater(pos[0], pos[2]);
      if (GetParent()->GetOnDoubleClick().GetLength()>0)
        ProcessOnClick(GetParent()->GetOnDoubleClick(), pos, this);
      else
        GetParent()->ShowNewObject(pos);
    }
    else
      GetParent()->ShowEditObject(_infoMove._name);
  }
}

void CStaticMapEditor::OnRButtonDown(float x, float y)
{
  if (_parent) _parent->RemoveContextMenu();
  _draggingRM = true;
  _dragPos = ScreenToWorld(Point2DFloat(x, y));

  // for rotation
  _infoClick = FindSign(x, y);

  if (_infoClick._type == signEditorObject)
  {
    EditorObject *obj = NULL;

    // select object
    if (GetParent() && _ws)
    {
      obj = _ws->FindObject(_infoClick._name);      
      if (obj)
      {
        // select object in object browser
        GetParent()->OnObjectSelected(_infoClick._name);

        // select object if not selected
        if (!obj->IsSelected())
          GetParent()->SelectObject(_infoClick._name);          
        // ... unless control is held down, in which case we unselect the object
        else
          if (GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL])
            GetParent()->UnSelectObject(_infoClick._name);        
      }
    }
  }
}

void CStaticMapEditor::OnRButtonUp(float x, float y)
{
  _draggingRM = false;
}

void DisplayMissionEditor::DoInit(RString initSQF)
{
  // read and preprocess init files
  GameState *gstate = GWorld->GetGameState();
  if (initSQF.GetLength() > 0)
  {
    RString filename = initSQF;
    FilePreprocessor preproc;
    QOStrStream processed;
    if (preproc.Process(&processed, filename))
    {
      RString script(processed.str(),processed.pcount());      
      int gshandle = gstate->BeginContext(&_vars);
      gstate->VarSetLocal("_map", CreateGameControl(_map), true);
      gstate->VarSetLocal("_this", gstate->CreateGameValue(GameArray), true);
      gstate->Execute(script, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
      gstate->EndContext(gshandle);
    }
  }
  else
  {
    ConstParamEntryPtr clsImport = Pars.FindEntry("CfgEditorInit");
    if (clsImport)
    {
      int n = clsImport->GetEntryCount();
      for (int i=0; i<n; i++)
      {
        ParamEntryVal entry = clsImport->GetEntry(i);
        if (entry.IsClass()) continue;
        RString filename = entry;
        FilePreprocessor preproc;
        QOStrStream processed;
        if (preproc.Process(&processed, filename))
        {
          RString script(processed.str(),processed.pcount());
          int gshandle = gstate->BeginContext(&_vars);
          gstate->VarSetLocal("_map", CreateGameControl(_map), true);
          gstate->VarSetLocal("_this", gstate->CreateGameValue(GameArray), true);
          gstate->Execute(script, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
          gstate->EndContext(gshandle);
        }
      }
    }
  }

  // set up help text
  _selectedObject = ESNone;
  UpdateControlsHelp();

  // center map
  _map->Center();
}

void CStaticMapEditor::ProcessMouseOverEvent()
{
  if (GetParent() && _ws)
  { 
    EditorObject *oldOver = GetParent()->GetObjMouseOver();

    if (oldOver && _infoMove._type != signEditorObject)
      return GetParent()->SetObjMouseOver(NULL);

    RString oldName = oldOver ? oldOver->GetArgument("VARIABLE_NAME") : RString();
    
    if (stricmp(oldName,_infoMove._name))
      GetParent()->SetObjMouseOver(_ws->FindObject(_infoMove._name));
  }
}

void CStaticMapEditor::OnMouseHold(float x, float y, bool active)
{
  if (_dragging)
  {
    // check rotate / raising
    GetParent()->UpdateMode();
  }
  else if (IsInside(x, y))
  {
    _infoMove = FindSign(x, y);
    ProcessMouseOverEvent();
  }  
};

void CStaticMapEditor::OnMouseMove(float x, float y, bool active)
{
  Vector3 curPos = ScreenToWorld(Point2DFloat(x, y));
  
  // this will clear text labels as soon as mouse is moved off an object
  if (Glob.uiTime - _firstMove > 0.5)
  {
    _firstMove = Glob.uiTime;
    _infoMove = FindSign(x, y);
    ProcessMouseOverEvent();
  }

  if (_draggingRM)
  {
    DrawCoord mouseMap = WorldToScreen(_dragPos);
    _mapX += x - mouseMap.x;
    SaturateX(_mapX);
    _mapY += y - mouseMap.y;
    SaturateY(_mapY);

    // _dragPos must remain the original value when dragging commenced
    return;
  }
  else if (_dragging)
  {    
    // Rotation / Raising / Moving
    DisplayMissionEditor *display = GetParent();

    if (display)
    {
      if (_ws && _ws->GetSelected().Size() > 0)
      {
        const RefArray<EditorObject> &selected = _ws->GetSelected();
        if (display->IsRotating())
        {
          // find center
          Vector3 sum = VZero;
          for (int i=0; i<selected.Size(); i++)
          {       
            Vector3 pos;
            if (selected[i]->GetEvaluatedPosition(pos))
              sum += pos;
          }
          Vector3 center = (1.0f / selected.Size()) * sum; center[1] = 0;
          // rotate selected objects
          Vector3 oldDir = _dragPos - center;
          Vector3 dir = curPos - center;
          float angle = AngleDifference(atan2(dir.X(), dir.Z()), atan2(oldDir.X(), oldDir.Z()));
          for (int i=0; i<selected.Size(); i++)
            display->RotateObject(selected[i], center, angle);
        }
        else if (display->IsRaising())
        {
          // raise selected object
          float offset = curPos.Z() - _dragPos.Z(); // TODO: some coef, maybe depending on map scale
          for (int i=0; i<selected.Size(); i++)
          {        
            EditorObject *obj = selected[i];
            if (obj->GetScope() >= EOSDrag)
              display->UpdateObject(obj, false, true, offset);
          }      

        }
        else
        {
          // offset selected objects
          Vector3 offset = curPos - _dragPos;
          for (int i=0; i<selected.Size(); i++)
          {        
            EditorObject *obj = selected[i];
            display->OnObjectMoved(obj, offset);
          }      
        }
      }

      // check if rotate / raising
      display->UpdateMode();
    }
  }

  _dragPos = curPos; 
}

void CStaticMapEditor::OnRButtonClick(float x, float y)
{
  SignInfo info = FindSign(x, y);

  RString id;
  if (info._type == signEditorObject) id = info._name;
  Vector3 pos = ScreenToWorld(DrawCoord(x, y));
  if (_parent) _parent->CreateContextMenu(
    x, y, IDC_EDITOR_MENU,
    Pars >> "RscDisplayMissionEditor" >> "Menu",
    CCMContextEditor(id, true, pos));
}

void CObjectBrowser::OnRButtonClick(float x, float y)
{
  if (!_parent) return;

  // select
  OnLButtonDown(x, y);

  CTreeItem *item = FindItem(x, y);

  // create menu
  if (item) _parent->CreateContextMenu(
    x, y, IDC_EDITOR_MENU,
    Pars >> "RscDisplayMissionEditor" >> "Menu",
    CCMContextEditor(item->data, false, VZero));
}

inline DisplayMissionEditor *CBackground3D::GetParent()
{
  return static_cast<DisplayMissionEditor *>(_parent.GetLink());
}

void CBackground3D::OnLButtonDown(float x, float y)
{
#if USE_EDITOR_CAMERA
  if (GetParent()) GetParent()->OnLButtonDown(x, y);
#endif
}

void CBackground3D::OnLButtonUp(float x, float y)
{
#if USE_EDITOR_CAMERA
  if (GetParent()) GetParent()->OnLButtonUp(x, y);
#endif
}

void CBackground3D::OnLButtonClick(float x, float y)
{
#if USE_EDITOR_CAMERA
  if (GetParent()) GetParent()->OnLButtonClick(x, y);
#endif
}

void CBackground3D::OnLButtonDblClick(float x, float y)
{
#if USE_EDITOR_CAMERA
  if (GetParent()) GetParent()->OnLButtonDblClick(x, y);
#endif
}

void CBackground3D::OnMouseMove(float x, float y, bool active)
{
#if USE_EDITOR_CAMERA
  if (GetParent()) GetParent()->OnMouseMove(x, y, active);
#endif
}

void CBackground3D::OnMouseHold(float x, float y, bool active)
{
#if USE_EDITOR_CAMERA
  if (GetParent()) GetParent()->OnMouseHold(x, y, active);
#endif
}

void CBackground3D::OnRButtonDown(float x, float y)
{
#if USE_EDITOR_CAMERA
  if (GetParent()) GetParent()->OnRButtonDown(x, y);
#endif
}

void CBackground3D::OnRButtonUp(float x, float y)
{
#if USE_EDITOR_CAMERA
  if (GetParent()) GetParent()->OnRButtonUp(x, y);
#endif
}

void CBackground3D::OnRButtonClick(float x, float y)
{
#if USE_EDITOR_CAMERA
  if (GetParent()) GetParent()->OnRButtonClick(x, y);
#endif
}

#if _VBS2
CompassMissionEditor::CompassMissionEditor(ControlsContainer *parent, int idc, ParamEntryPar cls)
  : Compass(parent, idc, cls)
{
  _isRotating = false;
}

void CompassMissionEditor::OnRButtonDown(float x, float y)
{
  _isRotating = true;
}

void CompassMissionEditor::OnSimulate(float deltaT, Vector3 &compassDir)
{
  if (IsRotating())      
  {
    float speedX = (GInput.GetAction(UABuldMoveRight) - GInput.GetAction(UABuldMoveLeft)) * deltaT*Input::MouseRangeFactor;
    float azimut = atan2(compassDir.X(), compassDir.Z());
    azimut += speedX;
    Matrix3 rotY(MRotationY, -azimut);
    compassDir = rotY.Direction();
  }
}
#endif

#if _VBS2
DisplayMissionEditor::DisplayMissionEditor(ControlsContainer *parent, RString resource, bool multiplayer)
: DisplayMap(parent), _vars(false)
#else
DisplayMissionEditor::DisplayMissionEditor(ControlsContainer *parent, bool multiplayer)
: Display(parent), _vars(false)
#endif
{
  _enableDisplay = false;
  _enableSimulation = false;
  _loadedExisting = false;
  _alwaysShow = true;
  _menuTypeActive = EMNone;
  _mouseCursor = CursorArrow;
  _dragging = false;
  _draggingRM = false;
  _selecting = false;
  _rotating = false;
  _raising = false;
  _dragPos = VZero;
  _pt1.x = 0; _pt1.y = 0;
  _pt2.x = 0; _pt2.y = 0;
  _lastObjectCreated = NULL;
  _simulationDisabled = false;
  _overlayType = NULL;
  _hasChanged = false;
  _multiplayer = multiplayer;
  _multiplayerPreview = false;
  _mission = RString();
  _realTime = false;
  _allow3DMode = true;
  _allowFileOperations = true;
  _show3DIcons = true;
  _objMouseOver = NULL;

#if _VBS2 // remainder of DisplayMap constructor
  _compassEditor = NULL;

  Load(resource);
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
#else
  ParamEntryVal cls = Pars >> "RscDisplayMissionEditor";
  _colorAttrDialog = GetPackedColor(cls >> "colorAttrDialog");
  _colorAttrPosition = GetPackedColor(cls >> "colorAttrPosition");
  _colorAttrDirection = GetPackedColor(cls >> "colorAttrDirection");
  _colorAttrLink = GetPackedColor(cls >> "colorAttrLink");
  _colorAttrParent = GetPackedColor(cls >> "colorAttrParent");
  _colorAttrCounter = GetPackedColor(cls >> "colorAttrCounter");
#endif

}

void DisplayMissionEditor::SetEditorEventHandler(RString name, RString value)
{
  EditorEvent event = GetEnumValue<EditorEvent>((const char *)name);
  if (event != INT_MIN)
  {
    _eventHandlers[event].Clear();
    if (value.GetLength() > 0) _eventHandlers[event].Add(value);
  }
}

void DisplayMissionEditor::OnEditorEvent(EditorEvent event)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(CreateGameControl(_map));
  handlers.Process(value);
}

void DisplayMissionEditor::OnEditorEvent(EditorEvent event, RString par1, const Object *par2)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(CreateGameControl(_map));
  arguments.Add(par1);
  if (par2)
    arguments.Add(GameValueExt(unconst_cast(par2),GameValExtObject));        
  else
    arguments.Add(GameValueExt((Object *)NULL));  
  handlers.Process(value);
}

void DisplayMissionEditor::OnEditorEvent(EditorEvent event, RString par1, ConstParamEntryPtr par2)
{
  EventHandlersUI &handlers = _eventHandlers[event];
  if (handlers.IsEmpty()) return;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(CreateGameControl(_map));
  arguments.Add(par1);
  if (par2)
  {
    GameConfigType result;
    result.entry = par2;
    result.path.Realloc(1);
    result.path.Resize(1);
    result.path[0] = par2->GetName();
    arguments.Add(GameValueExt(result));        
    handlers.Process(value);
  }
}

void DisplayMissionEditor::StartPreview()
{
  // preview in MP exits the editor
  if (_multiplayer)
  {
    _multiplayerPreview = true;
    if (_mission.GetLength() == 0)
    {
      // mission has not been saved, so show save mission dialog
      OnButtonClicked(IDC_EDITOR_SAVE);
      return;
    }
    // save latest version of mission and exit editor
    OnChildDestroyed(IDD_MISSION_SAVE, IDC_OK);
    return;
  }

  // save and clear overlay  - TODO: ask to save first?
  _ws.SaveOverlay(_ws.GetActiveOverlay());
  SetActiveOverlay(NULL);    
  // order objects by link dependancy
  _ws.OrderAllObjects();
#if USE_EDITOR_CAMERA
  GWorld->SetCameraEffect
  (
    CreateCameraEffect(_camera, "terminate", CamEffectBack, true)
  );
#endif
  if (IsCameraOnVehicle())
  {
    SwitchToCamera();
  }
  _enableSimulation = true;
  _enableDisplay = true;
#if !USE_EDITOR_CAMERA
  ShowObjectBrowser(false);
#else
  SetCursor(NULL);
#endif
  ShowMap(false);
  // enable simulation for all vehicles
  for (int i=0; i<_proxies.Size(); i++)
  {
    Entity *veh = dyn_cast<Entity>(_proxies[i].object.GetRef());
    if (!veh) continue;
    veh->DisableSimulation(false);
  }
  // switch camera to player
  {
    Person *person = GWorld->PlayerOn();
    if (person)
    {
      EntityAIFull *vehicle = person->Brain()->GetVehicle();
      DoAssert(vehicle);
      GWorld->SwitchCameraTo(vehicle, CamInternal, true);
      GWorld->SetPlayerManual(true);
      if (GWorld->UI())
      {
        GWorld->UI()->ResetHUD();
        GWorld->UI()->ResetVehicle(vehicle);
      }
    }
  }
  // prepare the UI for preview
  GWorld->CreateMainMap();
  AbstractUI *ui = GWorld->UI();
  if (ui)
  {
    ui->ShowAll(true);
    ui->ScriptShowHUD(true);
  }
  GWorld->DestroyUserDialog();
  GChatList.Clear();
  GChatList.ScriptEnable(true);
  GChatList.Load(Pars >> "RscChatListMission");
  GetNetworkManager().SetPendingInvitationPos("RscPendingInvitationInGame");
  GCustomChannels.ClearCustomChannels();
  // need to manually process init messages
  GWorld->EvalPostponedEvents();
  GWorld->ProcessInitMessages();
  RunInitScript();
  GWorld->FinishMissionInit(false);
  GWorld->SetEndMode(CAST_TO_ENUM(EEndMode, EMContinue));
  CreateChild(new DisplayMission(this, true));
  _alwaysShow = false;
}

void DisplayMissionEditor::FinishPreview()
{
  // cancel preview mode
  GWorld->DestroyMap(IDC_OK);
  GWorld->DestroyUserDialog();
  GChatList.Clear();
  GChatList.ScriptEnable(true);
  GChatList.Load(Pars >> "RscChatListDefault");
  GetNetworkManager().SetPendingInvitationPos("RscPendingInvitation");
  GCustomChannels.ClearCustomChannels();
#if USE_EDITOR_CAMERA
  SetCursor("Arrow");
#endif
  CameraRestart();

  SetMode(_mode);
  _alwaysShow = true;
}

void DisplayMissionEditor::SetMode(EditorMode mode)
{
  if (_multiplayerPreview)
    return;

  _mode = mode;

  // update button label and tooltip
  IControl *ctrl = GetCtrl(IDC_EDITOR_MAP);
  if (ctrl)
  {
    RString str = LocalizeString(mode == EMMap ? IDS_UI_EDITOR_INT_EDIT3D : IDS_UI_EDITOR_INT_EDIT2D);
    ctrl->SetTooltip(str);
    ITextContainer *text = GetTextContainer(ctrl);
    if (text) text->SetText(str);
  }

  // update controls help
  UpdateControlsHelp();

  switch (mode)
  {
  case EMMap:
#if _VBS2
    if (_compassEditor) _compassEditor->TrackNorth(true);
#endif
    _mouseCursor = CursorArrow;
    if (IsCameraOnVehicle() || _enableSimulation)
    {
      //SwitchToCamera();
      if (!_realTime) _enableSimulation = false;
    }
    // enable display in realtime mode
    _enableDisplay = _realTime;
#if !USE_EDITOR_CAMERA
    ShowObjectBrowser(true);
#endif
    ShowBackground3D(false);
    ShowMap(true);
    break;
  case EM3D:
#if _VBS2
    if (_compassEditor) _compassEditor->TrackNorth(false);
#endif
    _enableDisplay = true;
    if (IsCameraOnVehicle() || _enableSimulation)
    {
      //SwitchToCamera();
      if (!_realTime) _enableSimulation = false;
    }    
#if !USE_EDITOR_CAMERA
    ShowObjectBrowser(false);
#endif
    ShowBackground3D(true);
    ShowMap(false);
    break;
  }
}

void DisplayMissionEditor::LoadMission()
{
  // clean up editor objects
  _ws.Clear();

  SetCursor("Wait");
  DrawCursor();     

  // load mission
  RString filename = GetMissionDirectory() + RString("mission.biedi");
  if (QIFileFunctions::FileExists(filename))
  {
    _ws.Load(filename);
    _loadedExisting = true;
  }
  else
  {
    _loadedExisting = false;
  }

  Vector3 pos = VZero;
  bool lookAt = false;

  // collapse tree
  for (int i=0; i<_ws.NObjects(); i++)
  {
    EditorObject *obj = _ws.GetObject(i);
    if (!obj) continue;
    if (obj->GetParent().GetLength() > 0)
      obj->SetVisibleInTree(false);
    else
      if (!lookAt)
        if (obj->GetEvaluatedPosition(pos))
          lookAt = true;
  }
  UpdateObjectBrowser();

  RestartMission();

  // look at first unit automatically
  if (!EditorCameraActive())
    CameraRestart();
  if (lookAt)
  {
    _camera->LookAt(pos);
    if (_map)    
      _map->CStaticMap::Center(pos, false);
  }

  SetCursor("Arrow");

  // clear postprocess stuff
  GEngine->ResetGameState();
}

void DisplayMissionEditor::ClearMission()
{
  // save and clear overlay  - TODO: ask to save first?
  _ws.SaveOverlay(_ws.GetActiveOverlay());
  SetActiveOverlay(NULL);

  // clean up editor objects
  _ws.Clear();

  _mission = RString();
  _loadedExisting = false;


  // clear postprocess stuff
  GEngine->ResetGameState();

  RestartMission();
}

void DisplayMissionEditor::RestartMission()
{
  // save and clear overlay  - TODO: ask to save first?
  _ws.SaveOverlay(_ws.GetActiveOverlay());
  SetActiveOverlay(NULL);

  // clean up landscape
  _vars._vars.Clear();
  _originalPlayer = NULL;
  _playerUnit = NULL;
  _proxies.Clear();
  _hasChanged = false;
  _map->ClearDrawLinks();
  GWorld->SwitchLandscape(Glob.header.worldname);
  GWorld->AdjustSubdivision(GWorld->GetMode());

  // fill object type list
  _objectTypeList->RemoveAll();
  for (int i=0; i<_ws.NObjectTypes(); i++)
  {    
    const EditorObjectType *type = _ws.GetObjectType(i);
    int index = _objectTypeList->AddString(type->GetTitle() + type->GetShortcutKeyDesc());
    _objectTypeList->SetData(index, type->GetName());
  }
  if (_objectTypeList->Size() > 0)
    _objectTypeList->SetCurSel(0);

  // set up some local variables automatically
  // TODO: remove other _map variable definitions?
  _vars.VarLocal("_map");
  _vars.VarSet("_map", CreateGameControl(_map), true);

  // create camera (cursor)
#if USE_EDITOR_CAMERA
  _camera = new MissionEditorCamera(this);
#else
  _camera = new MissionEditorCursor(this);
#endif

  // create mission editor objects
  CreateObject(_ws.GetPrefixObject(),false,false,true);
  for (int i=0; i<_ws.NObjects(); i++)
    CreateObject(_ws.GetObject(i),false,false,true);
  CreateObject(_ws.GetPostfixObject(),false,false,true);

  // switch camera to cursor
  _originalPlayer = GWorld->PlayerOn();
  SwitchToCamera();

  UpdateObjectBrowser();

  // select editor mode
  // SetMode(EMMap);

  // FIX: remove mission fade in effect
  GWorld->ClearCutEffects();
}

void DisplayMissionEditor::RecordAction()
{
  _hasChanged = true;
}

#if _VBS2
ControlObject *DisplayMissionEditor::OnCreateObject(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
  case IDC_MAP_COMPASS:
    _compass = new CompassMissionEditor(this, idc, cls);
    _compassEditor = static_cast<CompassMissionEditor *>(_compass.GetRef());
    return _compass;
  }
  return DisplayMap::OnCreateObject(type, idc, cls);
}
#endif

Control *DisplayMissionEditor::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
  case IDC_EDITOR_FILE:
  case IDC_EDITOR_VIEW:
  case IDC_EDITOR_USER:
    {
      CEditorMenuButton *button = new CEditorMenuButton(this, idc, cls);
      return button;
    }
  case IDC_MAP:
    _map = new CStaticMapEditor(this, idc, cls, 0.001, 1.0, 0.1, &_ws, &_vars);
    if (_map)
    {
      _map->SetScale(-1);
      _map->Reset();
    }
    return _map;
  case IDC_EDITOR_OBJECTS:
    _objectBrowser = new CObjectBrowser(this, idc, cls);
    return _objectBrowser;
  case IDC_EDITOR_BACKGROUND:
    _background3D = new CBackground3D(this, idc, cls);
    return _background3D;
  }
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_EDITOR_ATTRIBUTES:
    _attributeBrowser = dynamic_cast<CListBox *>(ctrl);
    break;
  case IDC_EDITOR_TYPE_LIST:
    _objectTypeList = dynamic_cast<CListBox *>(ctrl);
    break;
  }
#if _VBS2
  return DisplayMap::OnCreateCtrl(type, idc, cls);
#else
  return ctrl;
#endif
}

class DisplayInterruptEditor : public DisplayInterrupt
{
public:
  DisplayInterruptEditor(ControlsContainer *parent) : DisplayInterrupt(parent) {}
  void OnButtonClicked(int idc);
};

void DisplayInterruptEditor::OnButtonClicked(int idc)
{
  switch (idc)
  {
    case IDC_INT_ABORT:
      DisplayInterrupt::OnChildDestroyed(IDD_MSG_TERMINATE_SESSION, IDC_OK);
      return;
    case IDC_INT_RETRY:
      DisplayInterrupt::OnChildDestroyed(IDD_MSG_RESTART_MISSION, IDC_OK);
      return;
  }
  DisplayInterrupt::OnButtonClicked(idc);
}

void DisplayMissionEditor::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_CANCEL:
/*
    if (_mode == EM3D)
    {
      // interrupt display
      //CreateChild(new DisplayEditorInterrupt(this, "RscDisplayInterruptEditor3D"));
      SetMode(EMMap);
    }
    else 
*/
    if (_ws.GetActiveOverlay() && _hasChanged)
      AskForOverlaySave(IDD_MSG_EXIT_OVERLAY);
    OnChildDestroyed(IDD_MSG_EXIT_OVERLAY, IDC_CANCEL); // skip straight to exit dialog
    // do not call base function - this would close the editor
    return;
  case IDC_EDITOR_LOAD:
    // save and clear overlay  - TODO: ask to save first?
    _ws.SaveOverlay(_ws.GetActiveOverlay());
    SetActiveOverlay(NULL);
    // SetMode(EMMap);
    CreateChild(new DisplayMissionLoad(this,_multiplayer,_loadedExisting));
    break;
  case IDC_EDITOR_SAVE:
    {
      // save and clear overlay  - TODO: ask to save first?
      _ws.SaveOverlay(_ws.GetActiveOverlay());
      SetActiveOverlay(NULL);
      // SetMode(EMMap);

      // find mission title and description
      RString title, description;
      EditorObject *prefix = _ws.GetPrefixObject();
      if (prefix)
        for (int i=0; i<prefix->NArguments(); i++)
        {
          const EditorArgument &arg = prefix->GetArgument(i);
          if (stricmp(arg.name, "TITLE") == 0)
            title = arg.value;
          if (stricmp(arg.name, "DESCRIPTION") == 0)
            description = arg.value;
        }

      CreateChild(new DisplayMissionSave(this, title, description));
    }
    break;
  case IDC_EDITOR_RESTART:
    RestartMission();
    break;
  case IDC_EDITOR_CLEAR:
    ClearMission();
    break;
  case IDC_EDITOR_PREVIEW:
    StartPreview();
    break;
  case IDC_EDITOR_MAP:
    if (_mode == EMMap)
      SetMode(EM3D);
    else
      SetMode(EMMap);
    break;
  case IDC_EDITOR_FILE:
    {
      Control *ctrl = dynamic_cast<Control *>(GetCtrl(IDC_EDITOR_FILE));
      if (!ctrl) break;
      ParamEntryPar cls = Pars >> "RscDisplayMissionEditor" >> "Menu_File";
      CreateContextMenu(
        ctrl->X(), ctrl->Y() + ctrl->H(), IDC_EDITOR_MENU_FILE,
        cls, CCMContextEditor(RString(), true, VZero, EMFile));
    }
    break;
  case IDC_EDITOR_VIEW:
    {
      Control *ctrl = dynamic_cast<Control *>(GetCtrl(IDC_EDITOR_VIEW));
      if (!ctrl) break;
      ParamEntryPar cls = Pars >> "RscDisplayMissionEditor" >> "Menu_View";
      CreateContextMenu(
        ctrl->X(), ctrl->Y() + ctrl->H(), IDC_EDITOR_MENU_VIEW,
        cls, CCMContextEditor(RString(), true, VZero, EMView));
    }
    break;
  }
#if _VBS2
  DisplayMap::OnButtonClicked(idc);
#else
  Display::OnButtonClicked(idc);
#endif
}

void DisplayMissionEditor::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
  case IDD_EDIT_OBJECT:
    if (exit == IDC_OK)
    {
      _mouseCursor = CursorArrow;
      DisplayEditObject *child = static_cast<DisplayEditObject *>(_child.GetRef());
      EditorObject *obj = child->GetObject();
      if (obj)
      {
        if (child->IsUpdate())
        {
          UpdateObject(obj);
          UpdateAttributeBrowser();

          // take a copy of the new object
          _lastObjectCreated = new EditorObject(*obj);
        }
        else
        {  
          // take a copy of the new object
          _lastObjectCreated = new EditorObject(*obj);

          _ws.AddObject(obj);

          SetCursor("Wait");
          DrawCursor();

          CreateObject(obj,true);

          SetCursor("Arrow");

          UpdateObjectBrowser();

          RString id = obj->GetArgument("VARIABLE_NAME");
          SelectObject(id);

          // link to this new object from some other object?
          RString linkArg;
          EditorObject *linkObj = child->LinkToObject(linkArg);        
          if (linkObj)
            OnLinkEditted(linkObj, linkArg, id, -1);
        }
        RecordAction();
      }
    }
    Display::OnChildDestroyed(idd, exit);
    return;
  case IDD_MISSION_LOAD:
    if (exit == IDC_OK)
    {
      // clean up references to objects
      _vars._vars.Clear();
      _originalPlayer = NULL;
      _playerUnit = NULL;
      _proxies.Clear();
      _map->ClearDrawLinks();

      DisplayMissionLoad *child = static_cast<DisplayMissionLoad *>(_child.GetRef());
      RString island = child->GetIsland();
      _mission = child->GetMission();
      SetBaseDirectory(true, RString());
#if _VBS2 // all missions to be saved in MPMissions
      ::SetMission(island, _mission, MPMissionsDir);
#else
      if (_multiplayer)
        ::SetMission(island, _mission, MPMissionsDir);
      else
        ::SetMission(island, _mission, MissionsDir);
#endif
      LoadMission();
    }
    break;
  case IDD_MISSION_SAVE:
    if (exit == IDC_OK)
    {      
      DisplayMissionSave *child = dynamic_cast<DisplayMissionSave *>(_child.GetRef());
      if (child) 
      {
        _mission = child->GetMission();

        // save mission title and description to prefix
        EditorObject *prefix = _ws.GetPrefixObject();
        if (prefix)
        {     
          prefix->RemoveArgument("TITLE");
          prefix->SetArgument("TITLE",child->GetTitle());

          prefix->RemoveArgument("DESCRIPTION");
          prefix->SetArgument("DESCRIPTION",child->GetDescription());
        }
      }
      
      if (_mission.GetLength() == 0)
      {
        GWorld->CreateWarningMessage(LocalizeString(IDS_MSG_WRONG_MISSION_NAME));
        break;
      }

      RString island = Glob.header.worldname;
     
      // save and clear overlay - TODO: ask to save first?
      _ws.SaveOverlay(_ws.GetActiveOverlay());
      SetActiveOverlay(NULL);

      // clean up all possible references to config before SetMission is called
      _vars._vars.Clear();
      _originalPlayer = NULL;
      _playerUnit = NULL;
      _proxies.Clear();
      _map->ClearDrawLinks();
      GWorld->SwitchLandscape(island);
      GWorld->AdjustSubdivision(GWorld->GetMode());

      if (!_loadedExisting) // if we are editing an existing mission, do not touch the file path
      {
        SetBaseDirectory(true, RString());
        if (_multiplayer)
          ::SetMission(island, _mission, MPMissionsDir);
        else
          ::SetMission(island, _mission, MissionsDir);
      }
      else
      {
        // use the directory, change only the filename
        SetMission(Glob.header.worldname, _mission, GetBaseSubdirectory());
      }
      // order objects by link dependancy
      _ws.OrderAllObjects();

      // save mission  
      RString directory = GetMissionDirectory();
      RString path = directory + RString("mission.biedi");
      void CreatePath(RString path);
      CreatePath(path);
      _ws.Save(path);
      _ws.WriteCreateScript(directory + RString("mission.sqf"));
      if (child)
      {
        switch (child->GetPlacement())
        {
        case MPSingleplayer:
          {
            RString bankName =
              RString("Missions\\") +
              _mission + RString(".") +
              RString(Glob.header.worldname) + RString(".pbo");
            DoVerify(QIFileFunctions::CleanUpFile(bankName));
            FileBankManager mgr;
            mgr.Create(bankName, directory);
          }
          break;
        case MPMultiplayer:
          {
            RString bankName =
              RString("MPMissions\\") +
              _mission + RString(".") +
              RString(Glob.header.worldname) + RString(".pbo");
            DoVerify(QIFileFunctions::CleanUpFile(bankName));
            FileBankManager mgr;
            mgr.Create(bankName, directory);
          }
          break;
        }
      }
      // restart is needed (world was cleaned up)
      EditorMode oldMode = _mode;
      RestartMission();
      SetMode(oldMode);

      if (_multiplayer && _multiplayerPreview)
      {
        _multiplayerPreview = false;
        Exit(IDC_OK);
        return;
      }      
    }
    else
    {
      _multiplayerPreview = false;
    }
    break;
  case IDD_OVERLAY_CREATE:
    if (exit == IDC_OK)
    {
      DisplayOverlayCreate *child = static_cast<DisplayOverlayCreate *>(_child.GetRef());
      RString name = child->GetName();
      EditorOverlay *overlay = _ws.CreateOverlay(name);
      SetActiveOverlay(overlay);      
    }
    break;
  case IDD_OVERLAY_LOAD:
    if (exit == IDC_OK)
    {
      DisplayOverlayLoad *child = static_cast<DisplayOverlayLoad *>(_child.GetRef());
      RString name = child->GetOverlay();
      // TODO: allowed types
      EditorOverlay *overlay = _ws.LoadOverlay(name);
      SetActiveOverlay(overlay);    
    }
    break;
  case IDD_MSG_EXITTEMPLATE:
    if (exit == IDC_OK)
    {
      // save and clear overlay - TODO: ask to save first?
      _ws.SaveOverlay(_ws.GetActiveOverlay());
      SetActiveOverlay(NULL);

      Exit(IDC_CANCEL);
    }
    break;
  case IDD_MSG_EXIT_OVERLAY:
    {
      if (exit == IDC_OK)
      {
        _ws.SaveOverlay(_ws.GetActiveOverlay());
      }
      
      MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(
        MB_BUTTON_OK | MB_BUTTON_CANCEL,
        LocalizeString(IDS_SURE),
        IDD_MSG_EXITTEMPLATE, false, &buttonOK, &buttonCancel);
    }
    break;  
  case IDD_MSG_CREATE_OVERLAY:
    {      
      if (exit == IDC_OK)
        _ws.SaveOverlay(_ws.GetActiveOverlay());
      SetActiveOverlay(NULL);
      SetMode(EMMap);
      if (_overlayType) CreateChild(new DisplayOverlayCreate(this,_overlayType));  
      return;
    }
    break;
  case IDD_MSG_LOAD_OVERLAY:
    {      
      if (exit == IDC_OK)
        _ws.SaveOverlay(_ws.GetActiveOverlay());
      SetActiveOverlay(NULL);
      SetMode(EMMap);
      if (_overlayType) CreateChild(new DisplayOverlayLoad(this,_overlayType)); 
      return;
    }
    break;
  case IDD_MSG_COMMIT_OVERLAY:
    {
      if (exit == IDC_OK)
      {
        _ws.SaveOverlay(_ws.GetActiveOverlay());  // save automatically
        CommitActiveOverlay();
        GWorld->CreateWarningMessage(LocalizeString("STR_EDITOR_PROMPT_OVERLAY_COMMITTED")); 
      }
    }
    break;
  case IDD_MSG_CLOSE_OVERLAY:
    {
      if (exit == IDC_OK)
        _ws.SaveOverlay(_ws.GetActiveOverlay());
      SetActiveOverlay(NULL);
    }
    break;
  case IDD_MSG_CLEAR_OVERLAY:
    {
      if (exit == IDC_OK)
      {
        ClearSelected();
        EditorOverlay *currentOverlay = _ws.GetActiveOverlay();

        // delete objects that are in currentOverlay
        for (int i=0; i<_ws.NObjects(); i++)
        {
          EditorObject *obj = _ws.GetObject(i);
          if (obj->GetParentOverlay() && obj->GetParentOverlay() == currentOverlay)
          {
            obj->SetParentOverlay(NULL);
            DeleteObject(obj);
            i--;
          }    
        }
        GWorld->CreateWarningMessage(LocalizeString("STR_EDITOR_PROMPT_OVERLAY_CLEARED")); 
      }
    }
    break;
  case IDD_MISSION:
    FinishPreview();
    break;
  case IDD_INTERRUPT:
    switch (exit)
    {
    case IDC_INT_ABORT:
#if _VBS2
      OnChildDestroyed(idd, IDC_INT_EDIT_MAP);
      return;
#else
      Display::OnChildDestroyed(idd, exit);
      {
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
        CreateMsgBox(
          MB_BUTTON_OK | MB_BUTTON_CANCEL,
          LocalizeString(IDS_SURE),
          IDD_MSG_EXITTEMPLATE, false, &buttonOK, &buttonCancel);
      }
      return;
#endif
    case IDC_INT_RETRY:
      {
        EditorMode oldMode = _mode;
        RestartMission();
        SetMode(oldMode);
      }
      break;
    case IDC_INT_EDIT_MAP:
      // edit and continue in ArmA 2
#if _VBS2
      RestartMission();
#endif
      SetMode(EMMap);
      break;
    case IDC_INT_EDIT_3D:
      // edit and continue in ArmA 2
#if _VBS2
      RestartMission();
#endif
      SetMode(EM3D);
      break;
    case IDC_INT_EDIT_PREVIEW:
      StartPreview();
      break;
    }
    break;
  }
  Display::OnChildDestroyed(idd, exit);
}


bool DisplayMissionEditor::OnUnregisteredAddonUsed(RString addon)
{
  EditorObject *prefix = _ws.GetPrefixObject();
  Assert(prefix);
  for (int i=0; i<prefix->NArguments(); i++)
  {
    const EditorArgument &arg = prefix->GetArgument(i);
    if (stricmp(arg.value,addon) == 0) return false;
  }
  prefix->SetArgument("ADDON", addon);
  UpdateAttributeBrowser();
  return true;
}

class SetAllObjectVisibility
{
protected:
  EditorWorkspace *_ws;
public:
  SetAllObjectVisibility(EditorWorkspace *ws){_ws = ws;}
  bool operator () (const CTreeItem *item) const
  {
    EditorObject *obj = _ws->FindObject(item->data);
    if (obj && item->level > 1)
      obj->SetVisibleInTree(item->IsExpanded());
    if (item->parent)
      if (item->parent->IsExpanded())
        obj->SetVisibleInTree(true);  
    return false;
  }
};

void DisplayMissionEditor::OnTreeMouseMove(int idc, CTreeItem *sel)
{
  EditorObject *oldOver = _objMouseOver;
  if (sel)
    _objMouseOver = _ws.FindObject(sel->data);
  if (oldOver != _objMouseOver)
    ProcessMouseOverEvent();
}

void DisplayMissionEditor::OnTreeMouseHold(int idc, CTreeItem *sel)
{
  OnTreeMouseMove(idc, sel);
}

void DisplayMissionEditor::OnTreeMouseExit(int idc, CTreeItem *sel)
{
  OnTreeMouseMove(idc, sel);
}

// update visibility of icons when tree is expanded or collapsed
void DisplayMissionEditor::OnTreeItemExpanded(IControl *ctrl, CTreeItem *item, bool expanded)
{
  if (ctrl->IDC() == IDC_EDITOR_OBJECTS)
  {
    SetAllObjectVisibility func(&_ws);
    _objectBrowser->ForEachItem(func);
  }
}

void DisplayMissionEditor::OnTreeDblClick(int idc, CTreeItem *sel)
{
  // on double click center the item in the map and camera
  if (sel)
  {
    EditorObject *obj = _ws.FindObject(sel->data);
    if (obj)
    {
      Vector3 pos;
      if (obj->GetEvaluatedPosition(pos))
        _map->CStaticMap::Center(pos, false);
      if (EditorCameraActive())
        SetCameraPosition(sel->data,_camera->IsLocked());
    }
  }

  /*
  if (sel)
    ShowEditObject(sel->data);
  */
}

void DisplayMissionEditor::OnTreeLButtonDown(int idc, CTreeItem *sel)
{
  if (sel)
  {
    EditorObject *selected = _ws.FindObject(sel->data);
    if (selected)
    {
      if (!GInput.keys[DIK_LCONTROL] && !GInput.keys[DIK_RCONTROL])
        ClearSelected();
      _ws.SetSelected(selected,true);
      OnEditorEvent(MESelectObject, sel->data, selected->GetProxyObject());
      DoSelectObjectScript(selected,true);
    }
    //fix: original sel->Expand(true) did not raise Expand event
    CTree *ctree = dynamic_cast<CTree *>(GetCtrl(IDC_EDITOR_OBJECTS));
    if(ctree) 
    {
      ctree->Expand(sel,true);
    }
  }
}

void DisplayMissionEditor::OnTreeSelChanged(IControl *ctrl)
{
  if (ctrl->IDC() == IDC_EDITOR_OBJECTS)
    UpdateAttributeBrowser();
  if (_objectBrowser->GetSelected())
    SetLastSelectedInTree(_objectBrowser->GetSelected()->data);
}

bool DisplayMissionEditor::OnKeyDown(int dikCode)
{
  switch (dikCode)
  {
  case DIK_TAB:
    return true; // disable tab switching (causes problems with drop-down menus)
  case DIK_L: // lock camera
    if (EditorCameraActive())
    {
      if (_camera->IsLocked())
      {
        _camera->UnLock();
        return true;
      }
      Object *lockTo = _objMouseOver ? _objMouseOver->GetProxyObject() : NULL;
      if (!lockTo)
      {
        // find nearest selected object to camera
        float minDist2 = FLT_MAX;
        for (int i=0; i<_ws.GetSelected().Size(); i++)
        {
          EditorObject *obj = _ws.GetSelected()[i];
          const Object *proxy = obj->GetProxyObject();
          if (proxy)
          {
            float dist2 = _camera->FutureVisualState().Position().Distance2(proxy->FutureVisualState().Position());
            if (dist2 < minDist2)
            {
              minDist2 = dist2;
              lockTo = unconst_cast(proxy);
            }
          }
        }     
      }
      _camera->LockTo(lockTo);
    }
    return true;
  case DIK_H:
    OnButtonClicked(IDC_EDITOR_PREVIEW);
    return true;
  case DIK_M:
    OnButtonClicked(IDC_EDITOR_MAP);
    return true;
  case DIK_ESCAPE:
    if (_ws.GetActiveOverlay())
    {
      if (_hasChanged)
        AskForOverlaySave(IDD_MSG_CLOSE_OVERLAY,LocalizeString("STR_EDITOR_PROMPT_OVERLAY_SAVE_CLOSE"));
      else
        OnChildDestroyed(IDD_MSG_CLOSE_OVERLAY, IDC_CANCEL);    // skip straight to create dialog 
      return true;
    }
    else
      return Display::OnKeyDown(dikCode);
  case DIK_DELETE:
    {
      if (_objMouseOver)
        if (_objMouseOver->GetScope() > EOSLinkFrom)
          DeleteObject(_objMouseOver);
    }
    return true;
  case DIK_C:
    if (GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL])
    {
      ClipboardCopy(false);
      return true;
    }
  case DIK_X:
    if (GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL])
    {
      ClipboardCopy(true);
      return true;
    }
  case DIK_V:
    if (GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL])
    {
      ClipboardPaste();
      UpdateObjectBrowser();
      return true;
    }
  default:
    // check shortcut keys
    for (int i=0; i<_ws.NObjectTypes(); i++)
      if (dikCode == _ws.GetObjectType(i)->GetShortcutKey())
      {
        _objectTypeList->SetCurSel(i);
        break;
      }
    return Display::OnKeyDown(dikCode);
  }
}

bool DisplayMissionEditor::OnKeyUp(int dikCode)
{
  return Display::OnKeyUp(dikCode);
}

void DisplayMissionEditor::OnSimulate(EntityAI *vehicle)
{  
  bool IsAppPaused();
  if (IsAppPaused()) return;

  UITime now = Glob.uiTime;
  float deltaT = now - _lastSimulate;
  _lastSimulate = now;

  if ( GWorld->GetTitleEffect() )
  {
    GWorld->GetTitleEffect()->Simulate(deltaT);
  }

  // main menu switching on mouse over
  if (_menuTypeActive > EMPopup)
  {
    IControl *ctrlFile = GetCtrl(IDC_EDITOR_FILE);
    IControl *ctrlView = GetCtrl(IDC_EDITOR_VIEW);

    if (_menuTypeActive != EMFile && ctrlFile && ctrlFile->IsMouseOver())
      OnButtonClicked(IDC_EDITOR_FILE);
    else if (_menuTypeActive != EMView && ctrlView && ctrlView->IsMouseOver())
      OnButtonClicked(IDC_EDITOR_VIEW);

    // user menus
    for (int i=0; i<_userMenus.Size(); i++)
    {
      Control *ctrl = _userMenus[i].ctrl;     
      if (ctrl && ctrl->IsMouseOver() && _menuTypeActive != EMUser + i)
      {
        CreateMenu(_userMenus[i].index);
        break;
      }
    }
  }

  if (IsDragging())
    _movedProxies.Clear();

  // enable/disable simulation if moving objects in real time
  if (_simulationDisabled && now - _stoppedSimulate > 5)
    EnableSimulationForSelected(true);

  if (_dragging || _rotating || _raising)
  {
    _stoppedSimulate = Glob.uiTime;
    if (!_simulationDisabled)
      EnableSimulationForSelected(false);    
  }
  //-!

  // Simulation of cursor in 3D
  if (_mode == EM3D && EditorCameraActive())
  {
    Vector3 curPos = _camera->GetGroundIntercept(GInput.cursorX, GInput.cursorY);

    RefArray<EditorObject> selected = _ws.GetSelected();
    if (selected.Size() > 0)
    {
      if (_rotating)
      {
        // rotate selected objects
        // find center
        Vector3 sum = VZero;
        for (int i=0; i<selected.Size(); i++)
        {       
          Vector3 pos;
          if (selected[i]->GetEvaluatedPosition(pos))
            sum += pos;
        }
        Vector3 center = (1.0f / selected.Size()) * sum; center[1] = 0;

        // rotate objects around center
        float speedX = (GInput.GetAction(UABuldMoveRight) - GInput.GetAction(UABuldMoveLeft)) * deltaT * Input::MouseRangeFactor;
        for (int i=0; i<selected.Size(); i++)        
          RotateObject(selected[i], center, speedX);
      }
      else if (_raising)
      {
        float speedY = (GInput.GetAction(UABuldMoveForward) - GInput.GetAction(UABuldMoveBack)) * deltaT * Input::MouseRangeFactor;
        Vector3 offset(0, 10.0f * speedY, 0);
        for (int i=0; i<selected.Size(); i++)        
          OnObjectMoved(selected[i], offset);
      }
      else if (_dragging)
      {
        // drag selected objects
        Vector3 offset = curPos - _dragPos; offset[1] = 0;

        // offset selected objects
        for (int i=0; i<selected.Size(); i++)
        {        
          EditorObject *obj = selected[i];
          OnObjectMoved(obj, offset);
        }      
      }
    }

    _dragPos = curPos;
  }

  //-!

  EditorObject *oldMouseOver = _objMouseOver;

  switch (_mode)
  {
  case EMMap:
    {
      float mouseX = 0.5 + GInput.cursorX * 0.5;
      float mouseY = 0.5 + GInput.cursorY * 0.5;
      IControl *ctrl = GetCtrl(mouseX, mouseY);
      if (ctrl == _map && _map)
      {
        if (_map->IsDragging())
        {
          if (_raising)
          {
            if (_mouseCursor != CursorTrack)
            {
              _mouseCursor = CursorTrack;
              SetCursor("Track");
            }
          }
          else if (_rotating)
          {
            if (_mouseCursor != CursorRotate)
            {
              _mouseCursor = CursorRotate;
              SetCursor("Rotate");
            }
          }
          else
          {
            if (_mouseCursor != CursorMove)
            {
              _mouseCursor = CursorMove;
              SetCursor("Move");
            }
          }
        }
        else if (_map->IsDraggingRM())
        {
          if (_mouseCursor != CursorScroll)
          {
            _mouseCursor = CursorScroll;
            SetCursor("Scroll");
          }
        }
        else
        {
          if (_mouseCursor != CursorTrack)
          {
            _mouseCursor = CursorTrack;
            SetCursor("Track");
          }
        }
      }
      else
      {
        if (_mouseCursor != CursorArrow)
        {
          _mouseCursor = CursorArrow;
          SetCursor("Arrow");
        }
      }
      // rotate compass   
#if _VBS2
      if (_compassEditor) _compassEditor->OnSimulate(deltaT, _compassDir);
#endif
      Display::OnSimulate(vehicle);
    }
    break;
  case EM3D:
    {
      float mouseX = 0.5 + GInput.cursorX * 0.5;
      float mouseY = 0.5 + GInput.cursorY * 0.5;
      IControl *ctrl = GetCtrl(mouseX, mouseY);
      if (ctrl && ctrl == _background3D)
      {
        // mouse over 3D environment
        if (_dragging)
        {
          if (_raising)
          {
            if (_mouseCursor != CursorRaise3D)
            {
              _mouseCursor = CursorRaise3D;
              SetCursor("Raise3D");
            }
          }
          else if (_rotating)
          {
            if (_mouseCursor != CursorRotate3D)
            {
              _mouseCursor = CursorRotate3D;
              SetCursor("Rotate3D");
            }
          }
          else
          {
            if (_mouseCursor != CursorMove3D)
            {
              _mouseCursor = CursorMove3D;
              SetCursor("Move3D");
            }
          }
        }
        else
        {
          if (_mouseCursor != CursorTrack3D)
          {
            _mouseCursor = CursorTrack3D;
            SetCursor("Track3D");
          }
        }
      }
      else
      {
        // mouse over other control
        if (_mouseCursor != CursorArrow)
        {
          _mouseCursor = CursorArrow;
          SetCursor("Arrow");
        }
      }

      if (EditorCameraActive())
      {      
        if (_camera) 
        {
          // is the cursor over a control or over the camera?
          float mouseX = 0.5 + GInput.cursorX * 0.5;
          float mouseY = 0.5 + GInput.cursorY * 0.5;

          bool cursorOverCamera = false;
          if (GetCtrl(mouseX, mouseY) == _background3D)
            cursorOverCamera = true;

          // determine currently selected proxy (only proxy position matters in 3D)
          if (cursorOverCamera && !_dragging && !_selecting)
          {
            _objMouseOver = NULL;

            // are we aiming directly at an object?
            Vector3 groundIntercept = _camera->GetGroundIntercept();
            CollisionBuffer collision;   
	          GLandscape->ObjectCollisionLine(_camera->GetRenderVisualStateAge(),collision,Landscape::FilterIgnoreOne(_camera),_camera->FutureVisualState().Position(),groundIntercept,0);

            for (int i=0; i<collision.Size(); i++)
            {
              Object *obj = collision[i].object;
              const EditorProxy *proxy = FindProxy(obj);          
              if (proxy)
              {
                EditorObject *edObj = proxy->edObj;
                if (edObj && (!edObj->IsVisibleIn3D() || !edObj->IsObjInCurrentOverlay())) continue;
                _objMouseOver = edObj;
                break;
              }
            }
            
            // find object closest to cursor
            /*
            if (!_objMouseOver)
            {
              Camera& camera = *GScene->GetCamera();
              Vector3 cDir = _camera->GetCursorDir().Normalized();                  

              int n = _ws.NObjects();
              float minDis2 = FLT_MAX;
              for (int i=0; i<n; i++)
              {
                EditorObject *obj = _ws.GetObject(i);
                if (!obj->IsVisibleIn3D() || !obj->IsObjInCurrentOverlay()) continue;
                
                const Object *proxy = obj->GetProxyObject();
                if (!proxy) continue;

                Vector3 pos = proxy->AimingPosition();
                Vector3Val dir = pos - camera.Position();

                // check nearest point on line
                float nearestT = cDir * dir;
                saturateMax(nearestT,0);
                Vector3 nearP = camera.Position() + cDir * nearestT;
               
                float dis = nearP.Distance(pos);

                if (dis < minDis2 && dis < 2)
                {
                  _objMouseOver = obj;
                  minDis2 = dis;
                }
              }
            }
            */
          }

#if _VBS2
          // need to manually move attached objects if simulation is disabled
          for (int i=0; i<_ws.GetSelected().Size(); i++)
          {        
            EditorObject *obj = _ws.GetSelected()[i];
            Entity *veh = dyn_cast<Entity>(obj->GetProxyObject().GetRef());
            if (veh && (veh->IsSimulationDisabled() || !_enableSimulation))
              veh->MoveAttached();
          }    
#endif

          GameState *gstate = GWorld->GetGameState(); 
          int gshandle = gstate->BeginContext(&_vars);
          _camera->Simulate(deltaT, SimulateInvisibleNear, gstate, cursorOverCamera);
          gstate->EndContext(gshandle);
        }
      }
#if USE_EDITOR_CAMERA
      Display::OnSimulate(vehicle);
#endif
    }
    break;
  }
  if (oldMouseOver != _objMouseOver)
    ProcessMouseOverEvent();
}

void DisplayMissionEditor::ProcessMouseOverEvent()
{
  if (_objMouseOver)
    OnEditorEvent(MEMouseOver, _objMouseOver->GetArgument("VARIABLE_NAME"), _objMouseOver->GetProxyObject());
  else
    OnEditorEvent(MEMouseOver, RString(), NULL);
}

void DisplayMissionEditor::DoSelectObjectScript(EditorObject *obj, bool selected)
{
  QOStrStream out;
  obj->WriteSelectScript(out);
  GameState *gstate = GWorld->GetGameState();
  int gshandle = gstate->BeginContext(&_vars);
  gstate->VarSetLocal("_selected", selected, true);
  gstate->Execute(RString(out.str(), out.pcount()), GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
  gstate->EndContext(gshandle);
}

void DisplayMissionEditor::SelectObject(RString name)
{
  EditorObject *selected = _ws.FindObject(name);
  if (selected)
  {    
    if (!GInput.keys[DIK_LCONTROL] && !GInput.keys[DIK_RCONTROL])
      ClearSelected();
    _ws.SetSelected(selected,true);
    OnEditorEvent(MESelectObject, name, selected->GetProxyObject());
    DoSelectObjectScript(selected,true);
    OnObjectSelected(name);
    UpdateControlsHelp();
  }
}

void DisplayMissionEditor::UnSelectObject(RString name)
{
  EditorObject *selected = _ws.FindObject(name);

  if (_objMouseOver == selected)
    _objMouseOver = NULL;

  // TODO: re-enable simulation for unselected object in realtime?
  if (selected)
    _ws.SetSelected(selected,false);
  UpdateControlsHelp();
}

void DisplayMissionEditor::ClearSelected()
{
  EnableSimulationForSelected(true);
  _ws.ClearSelected();
  UpdateControlsHelp();
}

void DisplayMissionEditor::EnableSimulationForSelected(bool enable) 
{
  if (_realTime)
  {
    _simulationDisabled = !enable;

    RefArray<EditorObject> selected = _ws.GetSelected();
    for (int i=0; i<selected.Size(); i++) 
    {
      EditorObject *obj = selected[i];
      if (obj && obj->GetScope() >= EOSDrag)
      {
        Object *proxy = unconst_cast(selected[i]->GetProxyObject());
        if (proxy)
        {
          Entity *veh = dyn_cast<Entity>(proxy);
          if (veh)
            veh->DisableSimulation(!enable);
        }
      }
    }
  }
}

void DisplayMissionEditor::AskForOverlaySave(int idd, RString text)
{
  CreateMsgBox
  (
    MB_BUTTON_OK | MB_BUTTON_CANCEL,
    text,
    idd
  );  
  MsgBox *msgBox = GetMsgBox();
  if (msgBox)
  {
    CButton *ok = dynamic_cast<CButton *>(msgBox->GetCtrl(IDC_OK));
    if (ok) ok->SetText(LocalizeString("STR_LIB_INFO_YES"));
    CButton *cancel = dynamic_cast<CButton *>(msgBox->GetCtrl(IDC_CANCEL));
    if (cancel) cancel->SetText(LocalizeString("STR_LIB_INFO_NO"));
  }
}

void DisplayMissionEditor::CommitActiveOverlay() 
{
  EditorOverlay *currentOverlay = _ws.GetActiveOverlay();
  if (!currentOverlay) return;

  ClearSelected();

  // recreate objects
  RefArray<EditorObject> newObjects;

  // delete objects that are in currentOverlay
  for (int i=0; i<_ws.NObjects(); i++)
  {
    EditorObject *obj = _ws.GetObject(i);
    if (obj->GetParentOverlay() && obj->GetParentOverlay() == currentOverlay)
    {
      EditorObject *copiedObj = new EditorObject(obj->GetType());
      newObjects.Add(copiedObj);
      *copiedObj = *obj;
      DeleteObject(obj);
      i--;
    }    
  }

  _ws.SetActiveOverlay(NULL);

  // create objects again
  for (int i=0; i<newObjects.Size(); i++)
  {
    EditorObject *obj = _ws.CopyObject(newObjects[i]);    
    obj->SetParentOverlay(NULL,true);
    CreateObject(obj,false);  // _new = false in create scripts
  }

  UpdateObjectBrowser();
  UpdateControlsHelp();

  OnEditorEvent(MECloseOverlay, *_overlayType >> "name", _overlayType);
}

// only allows one overlay at a time for now, in future allow more than one?
void DisplayMissionEditor::SetActiveOverlay(EditorOverlay *newOverlay) 
{
  EditorOverlay *currentOverlay = _ws.GetActiveOverlay();
  if (currentOverlay == newOverlay) return;

  ClearSelected();

  // delete objects that are in currentOverlay
  for (int i=0; i<_ws.NObjects(); i++)
  {
    EditorObject *obj = _ws.GetObject(i);
    if (obj->GetParentOverlay() && obj->GetParentOverlay() == currentOverlay)
    {
      obj->SetParentOverlay(NULL);
      DeleteObject(obj);
      i--;
    }    
  }

  _ws.SetActiveOverlay(newOverlay);

  // create objects in the active overlay
  if (newOverlay)
  {
    int nObj = _ws.NObjects();

    // add objects
    RString filename = GetUserDirectory() + RString("Overlays\\") + newOverlay->GetName() + RString(".biedi");
    _ws.Load(filename);

    // fix object names
    RefArray<EditorObject> newObjects;
    for (int i=nObj; i<_ws.NObjects(); i++)
      newObjects.Add(_ws.GetObject(i));
    _ws.RenameObjects(newObjects);

    // create objects
    for (int i=0; i<newObjects.Size(); i++)
      CreateObject(newObjects[i],false);  // _new = false in create scripts

    OnEditorEvent(MEEditOverlay, *_overlayType >> "name", _overlayType);
  }
  else
  {
    OnEditorEvent(MECloseOverlay, *_overlayType >> "name", _overlayType);
  }
  _hasChanged = false;
  UpdateObjectBrowser();
  UpdateControlsHelp();
}  

void DisplayMissionEditor::OnLButtonDown(float x, float y)
{
  if (!EditorCameraActive()) return;

  // ensure we process right mouse up before left button down
  OnRButtonUp(x, y);

  // mouse over object?
  if (_objMouseOver)
  {
    // select object in object browser
    OnObjectSelected(_objMouseOver->GetArgument("VARIABLE_NAME"));

    if (!_objMouseOver->IsSelected())
      SelectObject(_objMouseOver->GetArgument("VARIABLE_NAME"));
    else
      if (GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL])
        UnSelectObject(_objMouseOver->GetArgument("VARIABLE_NAME")); 

    // update help text
    if (_objMouseOver && _objMouseOver->IsSelected())
      SetSelectedStatus(ESSelected);
    else
      SetSelectedStatus(ESUnSelected);

    _dragging = true;
    _dragPos = _camera->GetGroundIntercept(GInput.cursorX, GInput.cursorY);

    _selecting = false;
  }
  else
  {
    _pt1 = Point2DFloat(x, y); 
    SetSelectedStatus(ESNone);
  }
}

void DisplayMissionEditor::GetSelecting(SelectingList &selecting)
{
  DoAssert(_selecting);

  Camera &camera = *GScene->GetCamera();

  float posX = TO_MOUSE(_pt1.x) * camera.Left();
  float posY = -TO_MOUSE(_pt1.y) * camera.Top();
  Vector3 startSelection = camera.DirectionModelToWorld(Vector3(posX, posY, 1));

  posX = TO_MOUSE(_pt2.x) * camera.Left();
  posY = -TO_MOUSE(_pt2.y) * camera.Top();
  Vector3 endSelection = camera.DirectionModelToWorld(Vector3(posX, posY, 1));

  Matrix4Val camInvTransform = camera.GetInvTransform();
  Vector3 posStart = camInvTransform.Rotate(startSelection);
  Vector3 posEnd = camInvTransform.Rotate(endSelection);
  if (posStart.Z() > 0 && posEnd.Z() > 0)
  {
    float invZ = 1.0 / posStart.Z();
    float xs = 0.5 * (1.0 + posStart.X() * invZ * camera.InvLeft());
    float ys = 0.5 * (1.0 - posStart.Y() * invZ * camera.InvTop());
    invZ = 1.0 / posEnd.Z();
    float xe = 0.5 * (1.0 + posEnd.X() * invZ * camera.InvLeft());
    float ye = 0.5 * (1.0 - posEnd.Y() * invZ * camera.InvTop());
    if (xs > xe) Swap(xs, xe);
    if (ys > ye) Swap(ys, ye);
    int n = _ws.NObjects();
    for (int i=0; i<n; i++)
    {
      EditorObject *obj = _ws.GetObject(i);

      const Object *proxy = obj->GetProxyObject();
      if (!proxy) continue;

      // check position
      Vector3 dir = proxy->AimingPosition(proxy->FutureVisualState()) - camera.Position();
      Vector3 pos = camInvTransform.Rotate(dir);
      if (pos.Z() <= 0) continue;
      invZ = 1.0 / pos.Z();
      float x = 0.5 * (1.0 + pos.X() * invZ * camera.InvLeft());
      float y = 0.5 * (1.0 - pos.Y() * invZ * camera.InvTop());
      if (x < xs) continue;
      if (x > xe) continue;
      if (y < ys) continue;
      if (y > ye) continue;

      // all tests passed
      selecting.Add(obj);
    }
  }
}

void DisplayMissionEditor::OnLButtonUp(float x, float y)
{
  if (!EditorCameraActive()) return;
  if (_selecting)
  {
    // do multiple selection
    if (!GInput.keys[DIK_LCONTROL] && !GInput.keys[DIK_RCONTROL])
      ClearSelected();

    SelectingList selecting;
    GetSelecting(selecting);

    for (int i=0; i<selecting.Size(); i++)
    {
      EditorObject *obj = selecting[i];
      // unlike 2D select, *don't* select hidden subordinates also
      _ws.SetSelected(obj, !obj->IsSelected(), false, false, true);
    }
    UpdateControlsHelp();

    _selecting = false;
  }
  else if (!_dragging)
  {
    if (!_objMouseOver)
      ClearSelected();
  }
  else if (_dragging)
    UpdateRemoteSelObjects();

  _dragging = false;
  _rotating = false;  
  _raising = false;
}

void DisplayMissionEditor::OnLButtonClick(float x, float y)
{
  if (!EditorCameraActive()) return;
  if (!_dragging)
  {
    // start drawing multiple select box?
    if (GInput.mouseL)
    {
      _dragging = false;
      _selecting = true;
      // _pt1 is set when mouse is first depressed
    }
  }
}

void DisplayMissionEditor::OnLButtonDblClick(float x, float y)
{
  if (!EditorCameraActive()) return;
  if (GInput.keys[DIK_LSHIFT]>0) return;
  if (_objMouseOver)
  {
    ShowEditObject(_objMouseOver->GetArgument("VARIABLE_NAME"));
  }
  else    
  {
    Vector3 pos = _camera->GetGroundIntercept();
    if (GetOnDoubleClick().GetLength()>0)      
      ProcessOnClick(GetOnDoubleClick(),pos,_map);
    else
      ShowNewObject(pos);
  }
}

bool DisplayMissionEditor::UpdateMode()
{
  if (GInput.keys[DIK_LSHIFT])
  {
    _rotating = true;
    return true; // rotation set
  }
  else
    _rotating = false;

  if (GInput.keys[DIK_LALT])
  {
    _raising = true;
    return true; // raising set
  }
  else
    _raising = false;

  return false; // no rotation nor raising set
}

void DisplayMissionEditor::OnMouseHold(float x, float y, bool active)
{
  if (!EditorCameraActive()) return;

  // currently raising or rotating selected objects?
  if (_dragging && !_camera->IsMoving())
  {
    UpdateMode();
  }
  else
  { 
    _rotating = false;
    _raising = false;
  }
}

void DisplayMissionEditor::OnMouseMove(float x, float y, bool active)
{
  if (!EditorCameraActive()) return;

  if (_objMouseOver)
  {
    if (_objMouseOver->IsSelected())
      SetSelectedStatus(ESSelected);
    else
      SetSelectedStatus(ESUnSelected);
  }
  else
    SetSelectedStatus(ESNone);
  
  // currently raising or rotating selected objects?
  if (_dragging && !_camera->IsMoving())
  {
    UpdateMode();
  }
  else
  { 
    _rotating = false;
    _raising = false;
  }

  if (_draggingRM)
  {
    _camera->SetRotating(true);
  }

  // used with drawing select box in 3D
  _pt2 = Point2DFloat(x, y); 
}

void DisplayMissionEditor::OnRButtonDown(float x, float y)
{
  RemoveContextMenu();
  if (!EditorCameraActive()) return;

  _draggingRM = true;

  // mouse over object?
  if (_objMouseOver)
  {
    // select object in object browser
    OnObjectSelected(_objMouseOver->GetArgument("VARIABLE_NAME"));

    if (!_objMouseOver->IsSelected())
      SelectObject(_objMouseOver->GetArgument("VARIABLE_NAME"));
    else
      if (GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL])
        UnSelectObject(_objMouseOver->GetArgument("VARIABLE_NAME")); 
  }
}

void DisplayMissionEditor::OnRButtonUp(float x, float y)
{
  if (!EditorCameraActive()) return;

  if (_draggingRM)
  {
    _camera->SetRotating(false);
    UpdateRemoteSelObjects();
    _draggingRM = false;  
  }
}

void DisplayMissionEditor::OnRButtonClick(float x, float y)
{
  if (!EditorCameraActive()) return;

  RString id;
  if (_objMouseOver)
    id = _objMouseOver->GetArgument("VARIABLE_NAME");
  
  Vector3 pos = _camera->GetGroundIntercept();
  CreateContextMenu(
      x, y, IDC_EDITOR_MENU,
      Pars >> "RscDisplayMissionEditor" >> "Menu",
      CCMContextEditor(id, true, pos)
  );
}

void DisplayMissionEditor::OnDraw(EntityAI *vehicle, float alpha)
{
  switch (_mode)
  {
  case EMMap:
#if _VBS2
    DisplayMap::OnDraw(vehicle, alpha);
#else
    Display::OnDraw(vehicle, alpha);
#endif
#if !_VBS2
    DIAG_MESSAGE(500, LocalizeString("STR_EDITOR_MODE_2D"));
#endif
    break;
  case EM3D:
#if !USE_EDITOR_CAMERA
    if (!IsCameraOnVehicle())
    {
      if (_camera && !_child) _camera->Draw(-1, 0, ClipAll, DrawParameters::_default, *_camera, _camera);
    }
#endif   
    if ( GWorld->GetTitleEffect() )
    {
      GWorld->GetTitleEffect()->Draw();
    }
    // draw multiple select box
    if (_selecting)
    {
      float wScreen = GLOB_ENGINE->Width2D();
      float hScreen = GLOB_ENGINE->Height2D();
      Rect2DPixel clipRect(0,0,wScreen,hScreen);
      // PackedColor color = PackedColor(Color(0,1,0,1));
      PackedColor color = GetCursorColor();

      GEngine->DrawLine
      (
        Line2DPixel(_pt1.x * wScreen, _pt1.y * hScreen,
        _pt2.x * wScreen, _pt1.y * hScreen),
        color, color, clipRect
      );
      GEngine->DrawLine
      (
        Line2DPixel(_pt2.x * wScreen, _pt1.y * hScreen,
        _pt2.x * wScreen, _pt2.y * hScreen),
        color, color, clipRect
      );
      GEngine->DrawLine
      (
        Line2DPixel(_pt2.x * wScreen, _pt2.y * hScreen,
        _pt1.x * wScreen, _pt2.y * hScreen),
        color, color, clipRect
      );
      GEngine->DrawLine
      (
        Line2DPixel(_pt1.x * wScreen, _pt2.y * hScreen,
        _pt1.x * wScreen, _pt1.y * hScreen),
        color, color, clipRect
      );
    }
#if !_VBS2
    DIAG_MESSAGE(500, LocalizeString("STR_EDITOR_MODE_3D"));
#endif
#if USE_EDITOR_CAMERA
    if (!_child && EditorCameraActive())
    {            
      const Object *selected = NULL;
      if (_objMouseOver)
      {
        selected = _objMouseOver->GetProxyObject();
        if (selected)
        {
          Vector3 pos = selected->GetShape() ? selected->COMPosition(selected->FutureVisualState()) : selected->FutureVisualState().Position();
          _camera->DrawSelectedIcon(pos, GetCursorColor());
        }
      }

      SelectingList selecting;
      if (_selecting) GetSelecting(selecting);

      // TODO: optimize & draw in order of distance from camera (closest first)
      for (int i=0; i<_ws.NObjects(); i++)
      {
        // have to use proxy object as position has no Y value!
        EditorObject *obj = _ws.GetObject(i);
        if (!obj->IsVisible()) continue;
        const Object *proxy = obj->GetProxyObject();
        if (proxy)
        {   
          // draw normal selected icon
          if (selected != proxy)
          {
            if (_selecting)
            {
              // when selecting, show the objects currently in the box
              if (selecting.Find(obj) >= 0)
              {
                Vector3 pos = proxy->GetShape() ? proxy->COMPosition(proxy->FutureVisualState()) : proxy->FutureVisualState().Position();
                PackedColor color = GetCursorColor();
                _camera->DrawSelectedIcon(pos, color);
              }
            }
            else if (obj->IsSelected())
            {
              Vector3 pos = proxy->GetShape() ? proxy->COMPosition(proxy->FutureVisualState()) : proxy->FutureVisualState().Position();
              PackedColor color = _dragging ? GetCursorColor() : _camera->SelectedColor();
              _camera->DrawSelectedIcon(pos,color);
            }
          }

          int n = obj->NIcons();
          bool lineDrawn = false;
          for (int j=0; j<n; j++)
          {
            EditorObjectIcon &icon = obj->GetIcon(j);
            if (icon.is3D)
            {
              Vector3 pos = proxy->FutureVisualState().Position() + icon.offset;      

              // selected? match color
              PackedColor colorIcon = icon.color, colorLine = PackedWhite;
              if (proxy == selected && !_rotating && !_raising)
                colorIcon = _camera->SelectedColor(), colorLine = _camera->SelectedColor();
              
              if (
                _show3DIcons &&
                _camera->DrawIcon(pos, icon.icon, icon.width, icon.height, colorIcon, icon.shadow) &&
                !lineDrawn && 
                obj->Draw3DLine()
              )
              {
                _camera->DrawLine(pos, proxy->CameraPosition(), colorLine, icon.shadow);
                lineDrawn = true;
              }
            }
          }
        }
      }
    }
    // draw all controls in 3D also
#if _VBS2
    _compassDir = Vector3(0, 0, 1);
    DisplayMap::OnDraw(vehicle, alpha);
#else
    Display::OnDraw(vehicle, alpha);
#endif
#endif
    break;
  }
}

EditorObject *DisplayMissionEditor::FindProxyOwner(Object *proxyObject)
{
  EditorObject *obj = NULL;
  GameValue proxy = GameValueExt(proxyObject);   
  GameState *gstate = GWorld->GetGameState();
  int gshandle = gstate->BeginContext(&_vars);
  for (int i=0; i<_ws.NObjects(); i++)
  {
    EditorObject *candidate = _ws.GetObject(i);
    GameValue result;
    result = gstate->Evaluate(candidate->GetArgument("VARIABLE_NAME"), GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
    if (result.IsEqualTo(proxy))
    {
      obj = candidate;
      break;
    }
  }
  gstate->EndContext(gshandle);
  return obj;
};

void DisplayMissionEditor::ShowEditObject(RString id, bool disregardProxy)
{
  EditorObject *obj = _ws.FindObject(id);
  if (!obj) return;

  if (obj->GetType()->ContextMenuFollowsProxy() && !disregardProxy)
  {
    EditorObject *proxyOwner = FindProxyOwner(obj->GetProxyObject().GetRef());
    if (proxyOwner)
    {
      obj = proxyOwner;
      id = obj->GetArgument("VARIABLE_NAME");
    }
  }

  if (obj->GetScope() < EOSDrag) return;

  OnObjectSelected(id);
  
  CreateChild(new DisplayEditObject(this, (CStaticMapEditor *)_map.GetRef(), obj, true));
};

void DisplayMissionEditor::SetSelectedStatus(EditorSelectedStatus selectedObject) 
{
  bool updateHelpText = _selectedObject != selectedObject;
  _selectedObject = selectedObject;
  if (updateHelpText)
    UpdateControlsHelp();
}

void DisplayMissionEditor::UpdateControlsHelp()
{
  CStatic *help = dynamic_cast<CStatic *>(GetCtrl(IDC_EDITOR_CONTROLS_HELP));
  if (!help) return;

  RString text;

  if (_ws.GetActiveOverlay())
  {
    text = text + LocalizeString("STR_CONTROLS_OVERLAY");
    text = text + "\\n";
  }
  if (_map->GetLinkObject())
  {
    text = text + LocalizeString("STR_CONTROLS_MOUSE_LINK_OBJECT");
  }
  else
  {
    switch (_selectedObject)
    {
    case ESUnSelected:
      if (_mode == EMMap)
        text = text + LocalizeString("STR_CONTROLS_MOUSE_UNSELECTED_OBJECT_MAP");
      else
        text = text + LocalizeString("STR_CONTROLS_MOUSE_UNSELECTED_OBJECT_3D");
      break;
    case ESSelected:
      if (_mode == EMMap)
        text = text + LocalizeString("STR_CONTROLS_MOUSE_SELECTED_OBJECT_MAP");
      else
        text = text + LocalizeString("STR_CONTROLS_MOUSE_SELECTED_OBJECT_3D");
      break;
    default:
      text = text + LocalizeString("STR_CONTROLS_MOUSE_NO_OBJECT");
      break;
    }
  }
  text = text + "\\n";
  if (_ws.GetSelected().Size() > 0)
  {
    if (_mode == EMMap)
      text = text + LocalizeString("STR_CONTROLS_MOUSE_SELECTED_MAP");
    else
      text = text + LocalizeString("STR_CONTROLS_MOUSE_SELECTED_3D");
    text = text + "\\n";
  }
  if (_mode == EMMap)
    text = text + LocalizeString("STR_CONTROLS_MAP");
  else
    text = text + LocalizeString("STR_CONTROLS_3D");
  text = text + "\\n";
  if (_realTime)
    text = text + LocalizeString("STR_CONTROLS_KEYS_REAL_TIME");
  else
    text = text + LocalizeString("STR_CONTROLS_KEYS_EDITOR");

  help->SetText(text);
};

bool DisplayMissionEditor::SelectEditorObject(RString select)
{
  for (int i=0; i<_ws.NObjectTypes(); i++)
  {    
    const EditorObjectType *type = _ws.GetObjectType(i);
    if (stricmp(type->GetName(),select) == 0)
    {
      _objectTypeList->SetCurSel(i);
      return true;
    }
  }
  return false;
}

void DisplayMissionEditor::UpdateRemoteSelObjects()
{
  if (GWorld->GetMode() == GModeNetware)
  {
    // move remote units
    for (int i=0; i<_ws.GetSelected().Size(); i++)
    {
      EditorObject *obj = _ws.GetSelected()[i];
      Object *proxy = unconst_cast(obj->GetProxyObject());
      if (proxy && !proxy->IsLocal())
        GetNetworkManager().AskForMove(proxy,proxy->FutureVisualState().Position());
    }
  }
}

void DisplayMissionEditor::ShowNewObject(Vector3Par pos, EditorObject *linkObj, RString linkArg, RString className, TargetSide side)
{
  // no item selected?
  if (_objectTypeList->GetCurSel() == -1)
      return;
  RString name = _objectTypeList->GetData(_objectTypeList->GetCurSel());
  EditorObjectType *type = _ws.FindObjectType(name);
  EditorObject *obj = new EditorObject(type);
  // fill object values
  const EditorParams &params = obj->GetType()->GetParams();
  for (int i=0; i<params.Size(); i++)
  {
    const EditorParam &param = params[i];
    if (param.source == EPSPosition)
    {
      // position
      RString value = FormatPosition(pos);
      obj->SetArgument(param.name, value);
    }
    else if (param.source == EPSParent)
    {
      // parent
      // try to use the object selected in the object browser as an parent
      if (_objectBrowser)
      {
        CTreeItem *sel = _objectBrowser->GetSelected();
        while (sel)
        {
          EditorObject *parent = _ws.FindObject(sel->data);
          if (parent && stricmp(parent->GetType()->GetName(), param.type) == 0)
          {
            obj->SetArgument(param.name, sel->data);
            break;
          }
          sel = sel->parent;
        }
      }
    }
    else if (param.source == EPSId)
    {
      // do not change
    }
    else if (param.hasDefValue)
    {
      obj->SetArgument(param.name, param.defValue);
    }
  }
  if (!linkObj && _lastObjectCreated)
    className = _lastObjectCreated->GetArgument("TYPE");
  CreateChild(new DisplayEditObject(this, (CStaticMapEditor *)_map.GetRef(), obj, false, linkObj, linkArg, className));
}

void DisplayMissionEditor::CreateObject(EditorObject *obj, bool isNewObject, bool isPaste, bool isLoading)
{
  bool cameraOnCursor = GWorld->CameraOn() == _camera;

  QOStrStream out;
  obj->WriteCreateScript(out);

// LogF("*** Creating object: %s", cc_cast(obj->GetArgument("VARIABLE_NAME")));
// LogF(RString(out.str(), out.pcount()));

  RString statement = obj->GetProxy();
  GameValue result;

  // create object

  GameState *gstate = GWorld->GetGameState();
  int gshandle = gstate->BeginContext(&_vars);
  gstate->VarSetLocal("_new", GameValue(isNewObject), true);
  gstate->VarSetLocal("_loading", GameValue(isLoading), true);
  gstate->VarSetLocal("_paste", GameValue(isPaste), true);

  // has the object been added in an overlay?
  bool isOverlay = obj->GetParentOverlay() ? true : false;
  gstate->VarSetLocal("_overlay", GameValue(isOverlay), true);

  gstate->Execute(RString(out.str(), out.pcount()), GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
  if (statement.GetLength() > 0) result = gstate->Evaluate(statement, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
  gstate->EndContext(gshandle);

  // proxy object
  Object *object = NULL;

  // does a proxy for the object already exist?
    // this might occur through the use of setObjectProxy command in create script
  bool proxyExists = false;
  for (int i=0; i<_proxies.Size(); i++)
    if (_proxies[i].edObj == obj)
    {
      proxyExists = true;
      object = _proxies[i].object;
      break;
    }

  // register proxy
  if (!proxyExists && !result.GetNil() && result.GetType() == GameObject)
  {    
    object = static_cast<GameDataObject *>(result.GetData())->GetObject();
    if (object)
      AddProxy(object,obj);
  }

  obj->SetProxyObject(object);

  obj->UpdateExecDrawMap(gstate);

  if (cameraOnCursor && GWorld->CameraOn() != _camera)
  {
    // keep camera on cursor
    _originalPlayer = GWorld->PlayerOn();
    SwitchToCamera();
  }

  RecordAction();
}

void DisplayMissionEditor::UpdateObject(EditorObject *obj, bool isRotate, bool isRaise, float raiseBy)
{
  bool cameraOnCursor = GWorld->CameraOn() == _camera;

  QOStrStream out;
  obj->WriteUpdateScript(out);

  // update object
  GameState *gstate = GWorld->GetGameState();
  int gshandle = gstate->BeginContext(&_vars);

  // has the object been updated in an overlay?
  bool isOverlay = obj->GetParentOverlay() ? true : false;
  gstate->VarSetLocal("_overlay", GameValue(isOverlay), true);
  gstate->VarSetLocal("_rotate", GameValue(isRotate), true);
  gstate->VarSetLocal("_raising", GameValue(isRaise), true);
  if (isRaise)
  {
    gstate->VarSetLocal("_raiseBy", GameValue(raiseBy), true);
  }

  gstate->Execute(RString(out.str(), out.pcount()), GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
  gstate->EndContext(gshandle);

  if (cameraOnCursor && GWorld->CameraOn() != _camera)
  {
    // keep camera on cursor
    _originalPlayer = GWorld->PlayerOn();
    SwitchToCamera();
  }
}

void DisplayMissionEditor::UpdatePositionObject(EditorObject *obj,Vector3Par offset)
{
  bool cameraOnCursor = GWorld->CameraOn() == _camera;

  QOStrStream out;
  obj->WriteUpdatePositionScript(out);

  // update object
  GameState *gstate = GWorld->GetGameState();

  // convert offset
  GameValue value = gstate->CreateGameValue(GameArray);
  GameArrayType &array = value;
  array.Resize(3);
  array[0] = offset.X();
  array[1] = offset.Z();
  array[2] = offset.Y();

  int gshandle = gstate->BeginContext(&_vars);
  gstate->VarSetLocal("_offset", array, true);
  gstate->Execute(RString(out.str(), out.pcount()), GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
  gstate->EndContext(gshandle);

  if (cameraOnCursor && GWorld->CameraOn() != _camera)
  {
    // keep camera on cursor
    _originalPlayer = GWorld->PlayerOn();
    SwitchToCamera();
  }
}

void DisplayMissionEditor::DeleteObject(RString id)
{
  EditorObject *obj = _ws.FindObject(id);
  DeleteObject(obj);  
}

void DisplayMissionEditor::DeleteObject(EditorObject *obj)
{
  if (obj)
  {
    QOStrStream out;
    obj->WriteDeleteScript(out);

#if !_VBS2
    // we can delete the object from editor immediately
    DeleteObjectFromEditor(obj->GetArgument("VARIABLE_NAME"));
#endif

    // call delete object script (is expected to use deleteEditorObject command)
    GameState *gstate = GWorld->GetGameState();
    int gshandle = gstate->BeginContext(&_vars);
    gstate->Execute(RString(out.str(), out.pcount()), GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
    gstate->EndContext(gshandle);
  }
};

void DisplayMissionEditor::DeleteObjectFromEditor(RString id)
{
  if (id.GetLength() == 0) return;

  bool cameraOnCursor = GWorld->CameraOn() == _camera;
  
  // check if object can be deleted
  bool link = false;
  bool parent = false;
  for (int i=0; i<_ws.NObjects(); i++)
  {
    const EditorObject *obj = _ws.GetObject(i);
    const EditorObjectType *type = obj->GetType();
    for (int j=0; j<type->GetParams().Size(); j++)
    {
      const EditorParam &param = type->GetParams()[j];
      if (param.source == EPSParent)
      {
        RString value = obj->GetArgument(param.name);
        if (stricmp(value, id) == 0) {parent = true; LogF("Unable to delete, %s is a child of %s",value.Data(),obj->GetArgument("VARIABLE_NAME").Data());}
      }
      else if (param.source == EPSLink)
      {
        RString value = obj->GetArgument(param.name);
        if (stricmp(value, id) == 0) {link = true; LogF("Unable to delete, link to %s remains in %s (%s)",value.Data(),obj->GetArgument("VARIABLE_NAME").Data(),param.name.Data());}
      }
      if (parent && link) break; // nothing more can be found
    }
  }
  if (parent)
  {
    CreateMsgBox(MB_BUTTON_OK, LocalizeString("STR_EDITOR_ERROR_CHILD_OBJS"));
    return;
  }
  else if (link)
  {
    CreateMsgBox(MB_BUTTON_OK, LocalizeString("STR_EDITOR_ERROR_HAS_LINKS"));
    return;
  }
  //-!

  EditorObject *obj = _ws.FindObject(id);
  if (obj)
  {
    UnSelectObject(id);

    // delete proxy
    for (int i=0; i<_proxies.Size(); i++)
    {
      if (_proxies[i].id == id)
      {
        Entity *veh = dyn_cast<Entity>(_proxies[i].object.GetRef());
        if (veh) veh->SetDelete();
        _proxies.Delete(i--);
      }
    }

    // delete links that are drawn both from and to this object
    _map->RemoveAllDrawLinks(obj);

    _ws.DeleteObject(obj);

    if (cameraOnCursor && GWorld->CameraOn() != _camera)
    {
      // keep camera on cursor
      _originalPlayer = GWorld->PlayerOn();
      SwitchToCamera();
    }

    UpdateObjectBrowser();
  }
}

Object *DisplayMissionEditor::FindObject(RString id)
{
  for (int i=0; i<_proxies.Size(); i++)
  {
    if (stricmp(_proxies[i].id, id) == 0) return _proxies[i].object;
  }
  return NULL;
}

void DisplayMissionEditor::ShowObjectBrowser(bool show)
{
  if (_objectBrowser) _objectBrowser->ShowCtrl(show);
  if (_attributeBrowser) _attributeBrowser->ShowCtrl(show);
  if (_objectTypeList) _objectTypeList->ShowCtrl(show);
  SetCursor(show ? "Arrow" : NULL);
}

void DisplayMissionEditor::ShowBackground3D(bool show)
{
  if (_background3D) _background3D->ShowCtrl(show);
}

void DisplayMissionEditor::ShowMap(bool show)
{
  if (_map)
  {
    _map->ShowCtrl(show);
    //if (show) _map->Center();
  }
}

static void AddBrowserObject(CTree *tree, CTreeItem *parent, EditorWorkspace &ws, const EditorObject *obj, RString &selectedId)
{
  if (!obj->IsObjInCurrentOverlay()) return;
  if (obj->GetScope() == EOSHidden || obj->GetScope() == EOSAllNoTree) return;

  RString varName = obj->GetArgument("VARIABLE_NAME");
  RString typeName = obj->GetType()->GetName();
  RString displayName = obj->GetDisplayNameTree();
  if (displayName.GetLength() == 0) displayName = obj->GetDisplayName();
  if (displayName.GetLength() == 0) displayName = Format("%s \"%s\"", (const char *)typeName, (const char *)varName);

  CTreeItem *item = parent->AddChild();
  item->text = displayName;
  item->data = varName;

  if (selectedId.GetLength() > 0 && stricmp(selectedId,varName) == 0)
  {
    tree->SetSelected(item);
    selectedId = "";
  }

  // reset tree expansion
  if (item->parent && obj->GetParent().GetLength() > 0)
      item->parent->Expand(obj->IsVisibleInTree());
  
  // child objects
  for (int i=0; i<ws.NObjects(); i++)
  {
    EditorObject *obj = ws.GetObject(i);
    if (stricmp(obj->GetParent(), varName) == 0) 
      AddBrowserObject(tree, item, ws, obj, selectedId);
  }
}

void DisplayMissionEditor::UpdateObjectBrowser()
{
  if (!_objectBrowser) return;

  RString lastSelected = _lastSelectedInTree;

  _objectBrowser->RemoveAll();
  CTreeItem *root = _objectBrowser->GetRoot();

  // add root objects
  EditorObject *obj = _ws.GetPrefixObject();
#if !_VBS2  // no prefix object visible in VBS2
  Assert(obj->GetParent().GetLength() == 0);
  AddBrowserObject(_objectBrowser, root, _ws, obj, lastSelected);
#endif
  for (int i=0; i<_ws.NObjects(); i++)
  {
    obj = _ws.GetObject(i);
    if (!obj->IsObjInCurrentOverlay()) continue;
    if (obj->GetScope() == EOSHidden || obj->GetScope() == EOSAllNoTree) continue;
    if (obj->GetParent().GetLength() == 0) AddBrowserObject(_objectBrowser, root, _ws, obj, lastSelected);
  }  
#if !_VBS2  // no postfix object visible in VBS2
  obj = _ws.GetPostfixObject();
  Assert(obj->GetParent().GetLength() == 0);
  AddBrowserObject(_objectBrowser, root, _ws, obj, lastSelected);
#endif
  /*
  if (root->children.Size() > 0)
    _objectBrowser->SetSelected(root->children[0]);
  */

  // update object visibility
  SetAllObjectVisibility func(&_ws);
  _objectBrowser->ForEachItem(func);
}

void DisplayMissionEditor::UpdateAttributeBrowser()
{
  if (!_attributeBrowser) return;
  _attributeBrowser->RemoveAll();

  if (!_objectBrowser) return;
  CTreeItem *item = _objectBrowser->GetSelected();
  if (!item) return;
  RString varName = item->data;

  EditorObject *obj = _ws.FindObject(varName);
  if (obj)
  {
    for (int i=0; i<obj->NArguments(); i++)
    {
      const EditorArgument &arg = obj->GetArgument(i);
      if (stricmp(arg.name, "VARIABLE_NAME") != 0)
      {
        const EditorParam *param = obj->GetAllParams().Find(arg.name);
        if (param && param->hidden) continue;

        // don't show empty arguments
        if (arg.value.GetLength() == 0) continue;

        RString name;
        if (param) name = param->displayName;
        if (name.GetLength() == 0) name = arg.name;
        int index = _attributeBrowser->AddString(Format("%s: %s", cc_cast(name), cc_cast(arg.value)));
        _attributeBrowser->SetData(index, arg.name);

        EditorParamSource source = EPSDialog;
        if (param) source = param->source;
        _attributeBrowser->SetValue(index, source);

        PackedColor color;
        switch (source)
        {
        case EPSPosition:
          color = _colorAttrPosition;
          break;
        case EPSDirection:
          color = _colorAttrDirection;
          break;
        case EPSLink:
          color = _colorAttrLink;
          break;
        case EPSParent:
          color = _colorAttrParent;
          break;
        case EPSCounter:
          color = _colorAttrCounter;
          break;
        case EPSDialog:
        default:
          color = _colorAttrDialog;
          break;
        }
        _attributeBrowser->SetSelColor(index, color);
        _attributeBrowser->SetFtColor(index, PackedColorRGB(color, toInt(0.75f * color.A8())));
      }
    }

    //if (_map) _map->SelectObject(obj);
  }
  _attributeBrowser->SetCurSel(0);
}

void DisplayMissionEditor::SetCameraPosition(RString id, bool lockCamera)
{
  // prefer external camera position on object
  Object *object = FindObject(id);
  if (object)
  {
    if (lockCamera && _mode == EMMap) _camera->ResetTargets();  // clear _resetTargets variable, reqd if in map mode (camera sim won't be running)
#if USE_EDITOR_CAMERA
    _camera->LookAt(object, lockCamera); 
#else    
    _camera->SetPosition(object->Position());
#endif
    return;
  }

  EditorObject *obj = _ws.FindObject(id);
  if (!obj) return;

  RString statement = obj->GetPosition();
  GameState *gstate = GWorld->GetGameState();
  int gshandle = gstate->BeginContext(&_vars);
  GameValue result = gstate->Evaluate(statement, GameState::EvalContext::_default, GWorld->GetMissionNamespace());
  gstate->EndContext(gshandle);

  if (result.GetType() != GameArray) return;
  const GameArrayType &array = result;
  if (array.Size() < 2 || array.Size() > 3) return;
  if (array[0].GetType() != GameScalar) return;
  if (array[1].GetType() != GameScalar) return;

  Vector3 pos(array[0], 0, array[1]);

#if USE_EDITOR_CAMERA
  _camera->LookAt(pos); 
#else  
  _camera->SetPosition(pos);
#endif
}

bool DisplayMissionEditor::SwitchToVehicle(RString id)
{
#if USE_EDITOR_CAMERA
  GWorld->SetCameraEffect
  (
    CreateCameraEffect(_camera, "terminate", CamEffectBack, true)
  );
#endif

  Object *obj = FindObject(id);
  EntityAI *veh = dyn_cast<EntityAI>(obj);
  if (veh) _playerUnit = veh->PilotUnit();
  if (!_playerUnit) return false;

  _originalPlayer = GWorld->PlayerOn();
  GWorld->SwitchCameraTo(_playerUnit->GetVehicle(), CamInternal, true);

  GWorld->SetPlayerManual(true);
  GWorld->SwitchPlayerTo(_playerUnit->GetPerson());
  GWorld->SetRealPlayer(_playerUnit->GetPerson());    

  if (GWorld->GetEEndMode() == EMKilled) GWorld->SetEndMode(EMContinue);

  GWorld->UI()->ResetHUD();

  _enableSimulation = true;

  // disable simulation for all vehicles (except camera vehicle)
  if (!_realTime)
  {
    for (int i=0; i<_proxies.Size(); i++)
    {
      Entity *veh = dyn_cast<Entity>(_proxies[i].object.GetRef());
      if (!veh) continue;
      veh->DisableSimulation(veh != _playerUnit->GetPerson() && veh != _playerUnit->GetVehicleIn());
    }
  }

  return true;
}

Object *DisplayMissionEditor::CameraGet()
{
  return _camera;
}

void DisplayMissionEditor::CameraRestart()
{
  OnEditorEvent(MERestartCamera);
  SwitchToCamera();
}

void DisplayMissionEditor::SwitchToCamera()
{
  Object *obj = GWorld->CameraOn();
  if (obj)
  {
#if USE_EDITOR_CAMERA
    _camera->LookAt(obj); 
#else
    _camera->SetTransform(*obj);
#endif
  }
  else
  {
    Matrix4 transform;
    // orientation
    transform.SetOrientation(M3Identity);
    // position
    Vector3 pos;
    pos[0] = (Pars >> "CfgWorlds" >> Glob.header.worldname >> "centerPosition")[0];
    pos[2] = (Pars >> "CfgWorlds" >> Glob.header.worldname >> "centerPosition")[1];
#if !USE_EDITOR_CAMERA
    pos[1] = 0; // will be fixed in simulation
#else
    pos[1] = GLandscape->SurfaceYAboveWater(pos.X(),pos.Z()) + 10;
#endif
    transform.SetPosition(pos);
#if USE_EDITOR_CAMERA
    _camera->LookAt(pos); 
#else
    _camera->SetTransform(transform);
#endif
  }

#if USE_EDITOR_CAMERA
  GWorld->SetCameraEffect
  (
    CreateCameraEffect(_camera, "internal", CamEffectBack, true)
  );
  void ShowCinemaBorder(bool show);
  ShowCinemaBorder(false);
#else
  GWorld->SwitchCameraTo(_camera, CamExternal, true);
#endif
  
  _playerUnit = NULL;
  if (_originalPlayer)
  {
    GWorld->SwitchPlayerTo(_originalPlayer);
    _originalPlayer = NULL;
  }
  //GWorld->SetPlayerManual(false);
  GWorld->UI()->ResetHUD();

  if (!_realTime)
  {
    _enableSimulation = false;
    // disable simulation for all vehicles
    for (int i=0; i<_proxies.Size(); i++)
    {
      Entity *veh = dyn_cast<Entity>(_proxies[i].object.GetRef());
      if (!veh) continue;
      veh->DisableSimulation(false);
    }
  }
  //-!
}

void DisplayMissionEditor::ClipboardCopy(bool isCut)
{
  // copy selected and all subordinates to clipboard
  _clipboard.Clear();
  _ws.CopyObjects(_clipboard); 
  
  // remove original objects if this is a cut
    // TODO: look at improving this, sometimes objects are missed?
  if (isCut)
  {
    // delete selected objects
    RefArray<EditorObject> selected = _ws.GetSelected();
    for (int i=0; i<selected.Size(); i++) 
    {
      if (selected[i])
        DeleteObject(selected[i]->GetArgument("VARIABLE_NAME"));
    }
  }   
}

void DisplayMissionEditor::ClipboardPaste()
{
  // paste clipboard at mouse position
  if (_clipboard.Size() > 0)
  {
    ClearSelected();

    Vector3 pastePos;
    if (_mode == EMMap)
    {
      float x = 0.5 + GInput.cursorX * 0.5;
      float y = 0.5 + GInput.cursorY * 0.5;
      pastePos = _map->ScreenToWorld(Point2DFloat(x, y));     
      pastePos[1] = GLandscape->RoadSurfaceYAboveWater(pastePos[0], pastePos[2]);
    }
    else
    {
      pastePos = _camera->GetGroundIntercept();
    }

    // work out positional offset
    GameValue result = _clipboard[0]->GetArgumentValue("POSITION");
    if (result.GetNil() || result.GetType() != GameArray) return;
    
    const GameArrayType &array = result;
    if (array.Size() < 2 || array[0].GetType() != GameScalar || array[1].GetType() != GameScalar) return;
    
    Vector3 objPos;
    if (!_clipboard[0]->GetEvaluatedPosition(objPos))
      return;

    Vector3 offset = pastePos - objPos;

    // this assigns new names for all objects and fixes parent and link parameters
    _ws.RenameObjects(_clipboard);

    for (int i=0; i<_clipboard.Size(); i++)
    {         
      EditorObject *obj = _ws.CopyObject(_clipboard[i]);

      // set to correct position
      Vector3 objPos, newPos;
      if (obj->GetEvaluatedPosition(objPos))
      {
        // convert back to string (plus offset)
        newPos = objPos + offset;
        RString value = FormatPosition(newPos);

        // update argument - position
        obj->RemoveArgument("POSITION");
        obj->SetArgument("POSITION", value);
      }

      // create the object in the world (calls create script, _new == false)
      CreateObject(obj,false,true);
      obj->SetVisibleInTree(_clipboard[i]->IsVisibleInTree());

      // set as selected (not subordinates though)
      _ws.SetSelected(obj,true,false,false,true);
    }
  }
}

class FindSelectedObjectFunc
{
protected:
  RString _id;
  mutable const CTreeItem *_found;

public:
  FindSelectedObjectFunc(RString id) {_id = id; _found = NULL;}
  const CTreeItem *GetFound() const {return _found;}
  bool operator () (const CTreeItem *item) const
  {
    if (stricmp(item->data, _id) == 0)
    {
      _found = item;
      return true; // stop searching now
    }
    return false;
  }
};

void DisplayMissionEditor::OnObjectSelected(RString id)
{
  if (_objectBrowser)
  {
    FindSelectedObjectFunc func(id);
    _objectBrowser->ForEachItem(func);
    const CTreeItem *found = func.GetFound();
    if (found)
    {
      CTreeItem *ptr = unconst_cast(found);
      _objectBrowser->SetSelected(ptr);
      while (ptr)
      {
        _objectBrowser->Expand(ptr);
        ptr = ptr->parent;
      }
    }
  }
  if (_mode == EMMap)
    SetCameraPosition(id,_camera->IsLocked());
}

void DisplayMissionEditor::RotateObject(EditorObject *obj, Vector3Par center, float angle)
{
  if (obj)
  {
    if (obj->GetScope() < EOSAllNoTree) return;

    bool _isChanged = false;

    // update the direction first
    const EditorParams &params = obj->GetType()->GetParams();
    for (int i=0; i<params.Size(); i++)
    {
      const EditorParam &param = params[i];
      if (param.source == EPSDirection)
      {        
        float azimut = atof(obj->GetArgument(param.name));
        azimut += (180.0 / H_PI) * angle;
        RString value = Format("%.8g", azimut);
               
        // update argument
        obj->RemoveArgument(param.name);
        obj->SetArgument(param.name, value);

        UpdateObject(obj,true);
        _isChanged = true;
        break;
      }
    }
    // then update the position
    {  
      Vector3 curPos;
      if (obj->GetEvaluatedPosition(curPos))
      {  
        Vector3 dir = curPos - center;
        Matrix3 rot(MRotationY, -angle);
        dir = rot * dir;

        Vector3 position = center + dir;
        Vector3 offset = position - curPos;          
        offset[1] = 0;  // no movement in vertical plane when rotating
        OnObjectMoved(obj, offset);
        _isChanged = true;
      }
    }

    if (_isChanged) RecordAction();
  }

  UpdateAttributeBrowser();
}

// executes the user-defined updateposition code for the dragged item
void DisplayMissionEditor::OnObjectMoved(EditorObject *obj, Vector3Par offset)
{
  if (obj)
  {
    if (obj->GetScope() < EOSAllNoTree) return;
    
    bool _isChanged = false;

    // offset
    const EditorParams &params = obj->GetType()->GetParams();
    for (int i=0; i<params.Size(); i++)
    {
      const EditorParam &param = params[i];
      if (param.source == EPSPosition)
      {
        // get position
        GameValue result = obj->GetArgumentValue(param.name);
        if (result.GetNil() || result.GetType() != GameArray) continue;

        const GameArrayType &array = result;
        int n = array.Size();
        if (n != 2 && n != 3) continue;
        if (array[0].GetType() != GameScalar) continue;
        if (array[1].GetType() != GameScalar) continue;
        float x = array[0];
        float y = 0;
        float z = array[1];
        if (n == 3)
        {
          if (array[2].GetType() != GameScalar) continue;
          y = array[2];
        }
        
        // calculate new position (all positions relative to surface above water)
        Vector3 newPos = Vector3(x, y, z) + offset;
        RString value;
        if (newPos.Y() == 0)
          value = Format("[%.8g, %.8g]", newPos.X(), newPos.Z());
        else
          value = Format("[%.8g, %.8g, %.8g]", newPos.X(), newPos.Z(), newPos.Y());

        // update argument - position
        obj->RemoveArgument(param.name);
        obj->SetArgument(param.name, value);

        // update object position
        UpdatePositionObject(obj,offset);

        // update position of proxy
        // look for proxy for this object
        Object *proxy = unconst_cast(obj->GetProxyObject());
        if (proxy)
        {
          // already moved?
          bool alreadyMoved = false;
          for (int i=0; i<_movedProxies.Size(); i++)
            if (_movedProxies[i] == proxy) {alreadyMoved = true; break;}
          if (alreadyMoved) break;
          _movedProxies.Add(proxy);

          Person *soldier = dyn_cast<Person>(proxy);
          if (soldier) soldier->DisableDamageFromObj(5.0f);
          
          // ensure the unit doesn't go below ground
          // current height over ground
          float diff = proxy->FutureVisualState().Position().Y() - GLandscape->SurfaceYAboveWater(proxy->FutureVisualState().Position().X(), proxy->FutureVisualState().Position().Z());            
          // new height over ground = diff + offset.Y()
          Vector3 newPos = proxy->FutureVisualState().Position() + offset;
          newPos[1] = GLandscape->SurfaceYAboveWater(newPos.X(), newPos.Z()) + diff + offset.Y();

          // vehicles need to be placed on the surface (because of copy surface flags etc.)
          Matrix4 trans = proxy->FutureVisualState().Transform();
          Entity *veh = dyn_cast<Entity>(proxy);
          if (veh) veh->PlaceOnSurfaceUnderWater(trans);
          trans.SetPosition(newPos);

          // move local unit
          proxy->Move(trans);  // proxy may in a different position to the editor object itself - eg if multiple objects share the same proxy
          proxy->OnPositionChanged();
          GScene->GetShadowCache().ShadowChanged(proxy);
        }
        _isChanged = true;
        break;        
      }
    }
    if (_isChanged) RecordAction();
  }
  UpdateAttributeBrowser();
}

void DisplayMissionEditor::OnLinkEditted(EditorObject *obj, RString name, RString value, float key)
{
  if (!obj) return;

  EditorObject *linked = _ws.FindObject(value);
  if ((linked && linked->GetScope() < EOSLinkTo) || obj == linked) return;

  const EditorParam *param = NULL;

  // if name is empty, search for first avilable link parameter beloing to obj 
  // that matches the type of the linked object
  if (name.GetLength() == 0 && key >= 0)
  {
    if (!linked)
    {
      // don't know which link to cancel! so do nothing
        // TODO: come up with a better solution (maybe a right click - clear links option?)
      return;
    }
    else
    {
      RString typeWanted = linked->GetType()->GetName();
      const EditorParams &params = obj->GetAllParams();
      int nParams = params.Size();

      // search for link or parent parameter that matches typeWanted
      for (int i=0; i<nParams; i++)
      {
        if (params[i].shortcutKey != key) continue;
        if (stricmp(typeWanted,params[i].type) == 0)
        {
          name = params[i].name;
          param = &params[i];
          break;
        }
      }    
      if (!param) return;
    }
  }
  else
  {
    param = obj->GetAllParams().Find(name);

    // check type of linked object
    if (linked && stricmp(linked->GetType()->GetName(), param->type) != 0)
    {
      // wrong type
      linked = NULL;
    }
  }

  Assert(param->source == EPSLink);

  // link to a position? allow processing to continue
  if (param->source == EPSLink && stricmp(param->type, "position") == 0)
    linked = obj;
  
  if (stricmp(param->subtype, "single") == 0)
  {
    if (linked)
    {
      obj->RemoveArgument(name);
      obj->SetArgument(name, value);
    }
  }
  else if (stricmp(param->subtype, "noneOrSingle") == 0)
  {
    obj->RemoveArgument(name);
    if (linked)
    {
      obj->SetArgument(name, value);
    }
  }
  else
  {
    if (linked)
    {
      bool found = false;
      for (int i=0; i<obj->NArguments(); i++)
      {
        const EditorArgument &arg = obj->GetArgument(i);
        if (stricmp(arg.name, name) == 0 && stricmp(arg.value, value) == 0)
        {
          obj->RemoveArgument(i);
          found = true;
          break;
        }
      }
      if (!found) obj->SetArgument(name, value);
    }
    else
    {
      obj->RemoveArgument(name);
    }
  }

  RecordAction();

  UpdateObject(obj);
  UpdateAttributeBrowser();
  UpdateObjectBrowser();
}

void DisplayMissionEditor::LookAtPosition(Vector3Par pos)
{
  _camera->LookAt(pos);
  if (_map)    
    _map->CStaticMap::Center(pos, false);
}

void DisplayMissionEditor::OnMenuSelected(MenuItem *item)
{
  switch (item->_cmd)
  {
  case ECSetCameraPosition:
    {
      const IAttribute *attr = item->_attributes[attrObject];
      Assert(attr);
      RString id = GetAttrString(attr);
      if (id.GetLength() > 0)
      {
        if (!EditorCameraActive())
          CameraRestart();
        SetCameraPosition(id,false);

        const EditorObject *obj = _ws.FindObject(id);
        if (!obj) break;
        
        Vector3 pos;
        if (obj->GetEvaluatedPosition(pos))
          _map->CStaticMap::Center(pos, false);
      }
      else
      {
        const IAttribute *iattr = item->_attributes[attrPosition];
        const AttrVector *attr = static_cast<const AttrVector *>(iattr);
        Assert(attr);
        Vector3 pos = attr->GetValue();
        _camera->LookAt(pos);
        if (_map) _map->CStaticMap::Center(pos, false);
      }
      SetMode(EM3D);
    }
    break;
  case ECLockCamera:
    {
      const IAttribute *attr = item->_attributes[attrObject];
      Assert(attr);
      RString id = GetAttrString(attr);
      if (id.GetLength() > 0)
      {
        if (!EditorCameraActive())
          CameraRestart();
        SetCameraPosition(id, true);

        const EditorObject *obj = _ws.FindObject(id);
        if (!obj) break;        

        Vector3 pos;
        if (obj->GetEvaluatedPosition(pos))
          _map->CStaticMap::Center(pos, false);
      }
      SetMode(EM3D);
    }
    break;    
  case ECSwitchToVehicle:
    {
      const IAttribute *attr = item->_attributes[attrObject];
      Assert(attr);
      RString id = GetAttrString(attr);
      EditorMode oldMode = _mode;
      StartPreview();
      if (!SwitchToVehicle(id))
      {
        SetMode(oldMode);
      }
    }
    break;
  case ECEditObject:
    {
      const IAttribute *attr = item->_attributes[attrObject];
      Assert(attr);
      RString id = GetAttrString(attr);
      ShowEditObject(id, true);
    }
    break;
  case ECDeleteObject:
    {
      const IAttribute *attr = item->_attributes[attrObject];
      Assert(attr);
      RString id = GetAttrString(attr);
      DeleteObject(id);
    }
    break;
  case ECNewObject:
    {
      const IAttribute *iattr = item->_attributes[attrPosition];
      const AttrVector *attr = static_cast<const AttrVector *>(iattr);
      Assert(attr);
      Vector3 pos = attr->GetValue();
      if (_onShowNewObject.GetLength()>0)
      {
        GameState *state = GWorld->GetGameState();

        GameValue posValue = state->CreateGameValue(GameArray);
        GameArrayType &posArray = posValue;
        posArray.Realloc(3);
        posArray.Add(pos[0]);
        posArray.Add(pos[2]);
        posArray.Add(pos[1]-GLandscape->SurfaceYAboveWater(pos[0],pos[2]));

        GameVarSpace vars(false);
        int gshandle = state->BeginContext(&vars);
        state->VarSetLocal("_map", CreateGameControl(_map), true);
        state->VarSetLocal("_pos",posArray,true);
        state->EvaluateMultiple(_onShowNewObject, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
        state->EndContext(gshandle);
      }
      else
        ShowNewObject(pos);
    }
    break;
  case ECCenterMap:
    if (_map)
    {
      SetMode(EMMap);
      const IAttribute *attr = item->_attributes[attrObject];
      Assert(attr);
      RString id = GetAttrString(attr);
      if (id.GetLength() > 0)
      {
        const EditorObject *obj = _ws.FindObject(id);
        if (!obj) break;
        
        Vector3 pos;
        if (obj->GetEvaluatedPosition(pos))
        {
          _map->CStaticMap::Center(pos, false);
          _camera->LookAt(pos);
        }
      }
      else
      {
        const IAttribute *iattr = item->_attributes[attrPosition];
        const AttrVector *attr = static_cast<const AttrVector *>(iattr);
        Assert(attr);
        Vector3 pos = attr->GetValue();
        _map->CStaticMap::Center(pos, false);
        _camera->LookAt(pos);
      }
    }
    break;
  case ECEditLink:
    {
      const IAttribute *attr = item->_attributes[attrObject];
      Assert(attr);
      RString id = GetAttrString(attr);
      EditorObject *obj = _ws.FindObject(id);
      if (!obj) break;
      
      attr = item->_attributes[attrArgument];
      Assert(attr);
      RString name = GetAttrString(attr);      

      _map->EditLink(obj, name);     
    }
    break;
  case ECExecuteScript:
    {
      const IAttribute *attr = item->_attributes[attrObject];
      Assert(attr);
      RString id = GetAttrString(attr);
      EditorObject *obj = _ws.FindObject(id);
      if (!obj) break;
      
      attr = item->_attributes[attrArgument];
      Assert(attr);
      RString name = GetAttrString(attr);      

      // retrieve code
      const EditorParam *param = obj->GetAllParams().Find(name);
      if (param && param->subtype.GetLength() > 0)
      {
        RString code = param->subtype;

        // insert parameters into a line of code
        code = obj->WriteScript(code);

        // execute code
        GameState *gstate = GWorld->GetGameState();
        int edhandle = gstate->BeginContext(_map->GetVariables()); // editor space
        GameVarSpace local(_map->GetVariables(), false);
        int lochandle = gstate->BeginContext(&local); // local space

        // initialization of variables
        gstate->VarSetLocal("_map", CreateGameControl(_map), true);

        // _this = proxy
        RString statement = obj->GetProxy();
        GameValue result;
        if (statement.GetLength() > 0) result = gstate->Evaluate(statement, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
        if (!result.GetNil() && result.GetType() == GameObject)
        {
          gstate->VarSetLocal("_this", result, true);
          gstate->Execute(code, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
        }

        gstate->EndContext(lochandle); // local space
        gstate->EndContext(edhandle); // editor space
      }
    }
    break;
  case ECMenuAdditionalItem:
    {
      const IAttribute *attr = item->_attributes[attrObject];
      Assert(attr);
      RString code = GetAttrString(attr);

      GameState *gstate = GWorld->GetGameState();
      int edhandle = gstate->BeginContext(_map->GetVariables()); // editor space
      GameVarSpace local(_map->GetVariables(), false);
      int lochandle = gstate->BeginContext(&local); // local space

      gstate->VarSetLocal("_map", CreateGameControl(_map), true);
      RefArray<EditorObject> selectedObjs = _ws.GetSelected(); // selected objects - TODO: make an array     
      if (selectedObjs.Size() > 0)
        gstate->VarSetLocal("_selected", GameValueExt(unconst_cast(selectedObjs[0]->GetProxyObject()),GameValExtObject), false);        
      else
        gstate->VarSetLocal("_selected", GameValueExt((Object *)NULL), false);        
#if USE_EDITOR_CAMERA
      gstate->VarSetLocal("_camera", GameValueExt(_camera,GameValExtObject), true);        
#endif
      gstate->Execute(code, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
      gstate->EndContext(lochandle); // local space
      gstate->EndContext(edhandle); // editor space
    }
    break;
  case ECMenuFileLoad:
    OnButtonClicked(IDC_EDITOR_LOAD);
    break;
  case ECMenuFileSave:
    OnButtonClicked(IDC_EDITOR_SAVE);
    break;
  case ECMenuFileClear:
    OnButtonClicked(IDC_EDITOR_CLEAR);
    break;
  case ECMenuFileRestart:
    OnButtonClicked(IDC_EDITOR_RESTART);
    break;
  case ECMenuFilePreview:
    OnButtonClicked(IDC_EDITOR_PREVIEW);
    break;
  case ECMenuFileExit:
    OnButtonClicked(IDC_CANCEL);
    break;
  case ECMenuView2D:
    SetMode(EMMap);
    break;
  case ECMenuView3D:
    SetMode(EM3D);
    break;
  }  
  _menuTypeActive = EMNone;
}

void DisplayMissionEditor::NewOverlay(ConstParamEntryPtr entry)
{
  if (!entry) return;
  _overlayType = entry;
  if (_ws.GetActiveOverlay() && _hasChanged)
    AskForOverlaySave(IDD_MSG_CREATE_OVERLAY);       
  else
    OnChildDestroyed(IDD_MSG_CREATE_OVERLAY, IDC_CANCEL);    // skip straight to create dialog      
}

void DisplayMissionEditor::LoadOverlay(ConstParamEntryPtr entry)
{
  if (!entry) return;
  _overlayType = entry;
  if (_ws.GetActiveOverlay() && _hasChanged)
    AskForOverlaySave(IDD_MSG_LOAD_OVERLAY);       
  else
    OnChildDestroyed(IDD_MSG_LOAD_OVERLAY, IDC_CANCEL);    // skip straight to create dialog 
}

void DisplayMissionEditor::SaveOverlay()
{
  const EditorOverlay *overlay = _ws.GetActiveOverlay();
  if (!overlay)
  {
    GWorld->CreateWarningMessage(LocalizeString("STR_EDITOR_ERROR_NO_OVERLAY"));  
    return;
  }      
  _ws.SaveOverlay(overlay);
  GWorld->CreateWarningMessage(LocalizeString("STR_EDITOR_PROMPT_OVERLAY_SAVED"));  
}

void DisplayMissionEditor::ClearOverlay(bool commit, bool close)
{
  if (!_ws.GetActiveOverlay())
  {
    GWorld->CreateWarningMessage(LocalizeString("STR_EDITOR_ERROR_NO_OVERLAY"));  
    return;
  }   
  if (commit)
    AskForOverlaySave(IDD_MSG_COMMIT_OVERLAY,LocalizeString("STR_EDITOR_PROMPT_OVERLAY_SAVE_COMMIT"));
  else if (close)
    if (_hasChanged)
      AskForOverlaySave(IDD_MSG_CLOSE_OVERLAY,LocalizeString("STR_EDITOR_PROMPT_OVERLAY_SAVE_CLOSE"));
    else
      OnChildDestroyed(IDD_MSG_CLOSE_OVERLAY, IDC_CANCEL);    // skip straight to create dialog 
  else
    AskForOverlaySave(IDD_MSG_CLEAR_OVERLAY,LocalizeString("STR_EDITOR_PROMPT_OVERLAY_SAVE_CLEAR"));
}

void DisplayMissionEditor::RemoveContextMenu()
{
  _menuTypeActive = EMNone;
  ControlsContainer::RemoveContextMenu();
  if (_map) _map->_legendDisabled = false;
}

static int CmpMenus(const UserMenu *a, const UserMenu *b)
{
  return sign(b->priority - a->priority);
}

int DisplayMissionEditor::AddMenu(RString text, float priority)
{
  // last hard-coded button
  Control *lastCtrl = dynamic_cast<Control *>(GetCtrl(IDC_EDITOR_VIEW));
  if (!lastCtrl) return -1;

  // dynamically create new button background based upon Menu_Button_BG template
  ConstParamEntryPtr ptr = (Pars >> "RscDisplayMissionEditor").FindEntry("Menu_Button_BG");
  if (!ptr) return -1;

  int type = *ptr >> "type";

  Control *background = OnCreateCtrl(type, -1, *ptr);
  if (!background) return -1;

  // dynamically create new button based upon Menu_Button template
  ptr = (Pars >> "RscDisplayMissionEditor").FindEntry("Menu_Button");
  if (!ptr) return -1;

  type = *ptr >> "type";

  Control *btn = OnCreateCtrl(type, IDC_EDITOR_USER, *ptr);
  if (!btn) return -1;

  // set action
  char str[256];
  sprintf(str,"((findDisplay %d) displayCtrl %d) createMenu %d",_idd,IDC_MAP,_userMenus.Size());
  switch (type)
  {
    case CT_BUTTON:
      {
        CButton *s = static_cast<CButton *>(btn);
        s->SetAction(str);
        s->SetText(text);
        break;
      }
    case CT_ACTIVETEXT:
      {
        CActiveText *s = static_cast<CActiveText *>(btn);
        s->SetAction(str);
        s->SetText(text);
      }
    default:
      return -1;
  }

  _controlsForeground.Add(background);
  _controlsForeground.Add(btn);

  int index = _userMenus.Size();

  _userMenus.Add(UserMenu(btn,background,priority,index));

  // sort menu buttons by priority
  QSort(_userMenus.Data(),_userMenus.Size(),CmpMenus);

  // position new button
  float x = lastCtrl->X() + lastCtrl->W();
  float y = lastCtrl->Y();
  float w = lastCtrl->W();
  float h = lastCtrl->H();

  for (int i=0; i<_userMenus.Size(); i++, x += w)
  {
    _userMenus[i].ctrl->SetPos(x,y,w,h);
    _userMenus[i].bg->SetPos(x,y,_userMenus[i].bg->W(),_userMenus[i].bg->H());
  }

  return index;
}

void DisplayMissionEditor::CreateMenu(int index)
{
  if (index < 0 || index >= _userMenus.Size())
    return;
  ParamEntryPar cls = Pars >> "RscDisplayMissionEditor" >> "Menu_User";

  // find menu matching index
  int i; for (i=0; i<_userMenus.Size(); i++)
    if (index == _userMenus[i].index)
      break;

  Control *ctrl = _userMenus[i].ctrl;
  if (!ctrl) return;

  CreateContextMenu(
    ctrl->X(), ctrl->Y() + ctrl->H(), IDC_EDITOR_MENU_USER,
    cls, CCMContextEditor(RString(), true, VZero, EMUser + i)); // pass position in _userMenus array
}

void DisplayMissionEditor::RemoveMenuItem(int index, RString text)
{
  if (index < 0 && text.GetLength() == 0) return;

  for (int i=0; i<_menuItems.Size(); i++)
    if (_menuItems[i].index == index || stricmp(_menuItems[i].text,text) == 0)
    {
      _menuItems.Delete(i);
      break;
    }
}

static int CmpMenuItems(const AdditionalMenuItem *a, const AdditionalMenuItem *b)
{
  return sign(b->priority - a->priority);
}

void DisplayMissionEditor::UpdateMenuItem(int index, RString text, RString command) 
{
  for (int i=0; i<_menuItems.Size(); i++)
  {
    if (_menuItems[i].index == index)
    {
      if (text.GetLength() > 0) _menuItems[i].text = text;
      if (command.GetLength() > 0) _menuItems[i].command = command;  
      break;
    }
  }
}

int DisplayMissionEditor::NMenuItems(RString text, int index)
{
  int n = 0;
  for (int i=0; i<_menuItems.Size(); i++)
  {
    if (
      (stricmp(text,"file") == 0 && _menuItems[i].type == EMFile) ||
      (stricmp(text,"view") == 0 && _menuItems[i].type == EMView) ||
      _menuItems[i].type - EMUser == index
    )
      n++;
  }  
  return n;
}

int DisplayMissionEditor::GetNextMenuItemIndex()
{
  int id = 0;
  for (int i=0; i<_menuItems.Size(); i++)
    if (_menuItems[i].index >= id) id = _menuItems[i].index + 1;
  return id;
}

int DisplayMissionEditor::AddMenuItem(RString type, RString text, RString command, int index, float priority, bool assignPriority)
{
  AdditionalMenuItem menuItem;

  if (type.GetLength() > 0)
  {
    if (stricmp(type,"file") == 0)
      menuItem.type = EMFile;
    else if (stricmp(type,"view") == 0)
      menuItem.type = EMView;
    else
      return -1;
  }
  else
  {
    if (index < 0 || index >= _userMenus.Size())
      return -1;
    menuItem.type = EMUser + index;
  }
  menuItem.text = text;
  menuItem.command = command;  
  menuItem.priority = priority;
  
  int id = 0; float lowestPri = priority;
  for (int i=0; i<_menuItems.Size(); i++)
  {
    if (_menuItems[i].index >= id) id = _menuItems[i].index + 1;
    if (_menuItems[i].priority <= lowestPri) lowestPri = _menuItems[i].priority - 1;
  }
  menuItem.index = id;

  if (assignPriority) menuItem.priority = lowestPri;

  _menuItems.Add(menuItem);

  // sort menu buttons by priority
  QSort(_menuItems.Data(),_menuItems.Size(),CmpMenuItems);

  return id;
}

void DisplayMissionEditor::InsertAdditionalMenuItems(Menu &menu, int menuType)
{
  // menuType is the position in _userMenus that we are creating a menu for, not index
    // so find index (we need to do this because _userMenus may be ordered non-sequentially)
  if (menuType >= EMUser)
    menuType = EMUser + _userMenus[menuType - EMUser].index;

  for (int i=0; i<_menuItems.Size(); i++)
  {
    AdditionalMenuItem &menuItem = _menuItems[i];
    if (menuItem.type == menuType)
    {
      MenuItem *item;
      if (menuItem.text.GetLength() == 0)
        item = new MenuItem("", 0, CMD_SEPARATOR);
      else
      {
        item = new MenuItem(menuItem.text, 0, ECMenuAdditionalItem);
        item->SetAttribute("object", menuItem.command, EditorMenuAttributeTypes);        
      }
      menu.AddItem(item);
    }
  }
}

static bool IsMenuItemActive(RString condition, EditorObject *obj, Ref<CStaticMapEditor> map, bool realTime)
{
  if (condition.GetLength() == 0) return true;

  GameState *gstate = GWorld->GetGameState();
  int edhandle = gstate->BeginContext(map->GetVariables()); // editor space
  GameVarSpace local(map->GetVariables(), false);
  int lochandle = gstate->BeginContext(&local); // local space

  gstate->VarSetLocal("_map", CreateGameControl(map), true);
  gstate->VarSetLocal("_realtime", realTime, true);

  RString statement = obj->GetProxy();
  GameValue result;
  if (statement.GetLength() > 0) result = gstate->Evaluate(statement, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
  if (!result.GetNil() && result.GetType() == GameObject)
  {
    gstate->VarSetLocal("_this", result, true);              
  }
  bool isValid = gstate->EvaluateBool(condition, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace

  gstate->EndContext(lochandle); // local space
  gstate->EndContext(edhandle); // editor space

  return isValid;
}

bool DisplayMissionEditor::OnMenuCreated(Menu &menu, float x, float y, int idc, CCMContext &ctx)
{
  CCMContextEditor &ctxEd = static_cast<CCMContextEditor &>(ctx);
  _menuTypeActive = ctxEd.type;
  switch (ctxEd.type)
  {
  case EMPopup:
    {
      RString id = ctxEd.name;
      bool map = ctxEd.map;

      EditorObject *obj = _ws.FindObject(id);
      if (!obj)
      {
        // right click on empty map or 3D space
        if (map)
        {
          MenuItem *item = new MenuItem(LocalizeString("STR_EDITOR_MENU_CTX_NEW_OBJ"), 0, ECNewObject);
          IAttribute *iattr = item->AccessAttribute("position", EditorMenuAttributeTypes);
          AttrVector *attr = static_cast<AttrVector *>(iattr);
          attr->SetValue(ctxEd.position);
          menu.AddItem(item);

          if (_allow3DMode)
          {
            item = new MenuItem("", 0, CMD_SEPARATOR);
            menu.AddItem(item);

            if (_map && _map->IsVisible()) // right click in 2D
            {
              MenuItem *item = new MenuItem(LocalizeString("STR_EDITOR_MENU_CTX_SET_CAM_POS"), 0, ECSetCameraPosition);
              item->SetAttribute("object", RString(), EditorMenuAttributeTypes);
              IAttribute *iattr = item->AccessAttribute("position", EditorMenuAttributeTypes);
              AttrVector *attr = static_cast<AttrVector *>(iattr);
              Vector3 pos = ctxEd.position;
              pos[1] = GLandscape->SurfaceYAboveWater(pos[0],pos[2]);
              attr->SetValue(pos);
              menu.AddItem(item);
            }

            if (_map && !_map->IsVisible()) // right click in 3D
            {
              MenuItem *item = new MenuItem(LocalizeString("STR_EDITOR_MENU_CTX_CENTER_MAP"), 0, ECCenterMap);
              item->SetAttribute("object", RString(), EditorMenuAttributeTypes);
              IAttribute *iattr = item->AccessAttribute("position", EditorMenuAttributeTypes);
              AttrVector *attr = static_cast<AttrVector *>(iattr);
              attr->SetValue(_camera->GetGroundIntercept());
              menu.AddItem(item);
            }
          }
          return true;
        }
        return false;
      }

      MenuItem *item;

      if (obj->GetType()->ContextMenuFollowsProxy() && map)
      {        
        EditorObject *proxyOwner = FindProxyOwner(obj->GetProxyObject().GetRef());
        if (proxyOwner)
        {
          obj = proxyOwner;
          id = obj->GetArgument("VARIABLE_NAME");
        }
      };

      if (obj->GetScope() >= EOSDrag)
      {
        item = new MenuItem(LocalizeString("STR_EDITOR_MENU_CTX_EDIT_OBJ"), 0, ECEditObject);
        item->SetAttribute("object", id, EditorMenuAttributeTypes);
        menu.AddItem(item);
      }

      if (obj->GetScope() >= EOSDrag)
      {
        item = new MenuItem(LocalizeString("STR_EDITOR_MENU_CTX_DEL_OBJ"), 0, ECDeleteObject);
        item->SetAttribute("object", id, EditorMenuAttributeTypes);
        menu.AddItem(item);
      }

      const EditorParams &params = obj->GetAllParams();
      int n = params.Size();       
      for (int i=0; i<n; i++)
      {
        const EditorParam &param = params[i];
        if (param.source != EPSLink && param.source != EPSContext) continue;
        if (param.hidden) continue;

        // link?
        if (param.source == EPSLink && obj->GetScope() >= EOSLinkFrom && _mode == EMMap)
        {
          if (!IsMenuItemActive(param.activeMode, obj, (CStaticMapEditor *)_map.GetRef(),_realTime))
            continue;
          RString name = param.name;
          if (param.description.GetLength() == 0)  
            item = new MenuItem(Format(LocalizeString("STR_EDITOR_MENU_CTX_EDIT_LINK"), (const char *)name), 0, ECEditLink);
          else
            item = new MenuItem(param.description, 0, ECEditLink);
          item->SetAttribute("object", id, EditorMenuAttributeTypes);
          item->SetAttribute("argument", name, EditorMenuAttributeTypes);
          menu.AddItem(item);
        }

        // context menu?
        if (param.source == EPSContext && obj->GetScope() >= EOSDrag)
        {
          if (
            (stricmp(param.activeMode, "editor") == 0 && _realTime) ||
            (stricmp(param.activeMode, "realtime") == 0 && !_realTime)
          ) continue;

          // evaluate activeMode
          if (stricmp(param.activeMode, "editor") && stricmp(param.activeMode, "realtime"))
          {
            if (!IsMenuItemActive(param.activeMode, obj, (CStaticMapEditor *)_map.GetRef(), _realTime))
              continue;
          }

          // seperator?
          if (param.subtype.GetLength() == 0) 
          {
            item = new MenuItem("", 0, CMD_SEPARATOR);
            menu.AddItem(item);    
            continue;
          }

          // context items can be "editorObject" or "code"
          EditorCommands ec = ECEditLink;
          if (stricmp(param.type, "code") == 0 || stricmp(param.type, "expression") == 0)
            ec = ECExecuteScript;

          if (_mode != EMMap && ec == ECEditLink) continue;

          RString name = param.name;
          if (param.description.GetLength() == 0)  
            item = new MenuItem(Format("%s", (const char *)name), 0, ec);
          else
            item = new MenuItem(param.description, 0, ec);
          item->SetAttribute("object", id, EditorMenuAttributeTypes);
          item->SetAttribute("argument", name, EditorMenuAttributeTypes);
          menu.AddItem(item);
        }
      }

      // Fill menu
      Object *object = FindObject(id);
      if (object && _allow3DMode)
      {
        item = new MenuItem(LocalizeString("STR_EDITOR_MENU_CTX_SET_CAM_POS"), 0, ECSetCameraPosition);
        item->SetAttribute("object", id, EditorMenuAttributeTypes);
        menu.AddItem(item);

        item = new MenuItem(LocalizeString("STR_EDITOR_MENU_CTX_LOCK_CAM"), 0, ECLockCamera);
        item->SetAttribute("object", id, EditorMenuAttributeTypes);
        menu.AddItem(item);

        if (obj->GetScope() >= EOSDrag)
        {
          EntityAI *veh = dyn_cast<EntityAI>(object);
          if (veh && veh->PilotUnit())
          {
            item = new MenuItem(LocalizeString("STR_EDITOR_MENU_CTX_SWITCH_VEH"), 0, ECSwitchToVehicle);
            item->SetAttribute("object", id, EditorMenuAttributeTypes);
            menu.AddItem(item);
          }
        }

        if (_map && !_map->IsVisible())
        {
          if (menu._items.Size() > 0)
          {
            item = new MenuItem("", 0, CMD_SEPARATOR);
            menu.AddItem(item);
          }

          MenuItem *item = new MenuItem(LocalizeString("STR_EDITOR_MENU_CTX_CENTER_MAP"), 0, ECCenterMap);
          item->SetAttribute("object", id, EditorMenuAttributeTypes);
          menu.AddItem(item);
        }
      }
    }
    break;
  case EMFile:
    {
      if (_map) {_map->_legendDisabled = true;}
      if (_allowFileOperations)
      {
        MenuItem *item = new MenuItem(LocalizeString("STR_EDITOR_MENU_FILE_LOAD"), 0, ECMenuFileLoad);
        menu.AddItem(item);

        item = new MenuItem(LocalizeString("STR_EDITOR_MENU_FILE_SAVE"), 0, ECMenuFileSave);
        menu.AddItem(item);

        item = new MenuItem(LocalizeString("STR_EDITOR_MENU_FILE_CLEAR"), 0, ECMenuFileClear);
        menu.AddItem(item);

        item = new MenuItem("", 0, CMD_SEPARATOR);
        menu.AddItem(item);    

        item = new MenuItem(LocalizeString("STR_EDITOR_MENU_FILE_RESTART"), 0, ECMenuFileRestart);
        menu.AddItem(item);

        item = new MenuItem(LocalizeString("STR_EDITOR_MENU_FILE_PREVIEW"), 0, ECMenuFilePreview);
        menu.AddItem(item);

        item = new MenuItem("", 0, CMD_SEPARATOR);
        menu.AddItem(item);    
      }

      InsertAdditionalMenuItems(menu,EMFile);

      MenuItem *item = new MenuItem(LocalizeString("STR_EDITOR_MENU_FILE_EXIT"), 0, ECMenuFileExit);
      menu.AddItem(item);      
    }
    break;
  case EMView:
    {
      if (_map) {_map->_legendDisabled = true;}
      if (_allow3DMode)
      {
          MenuItem *item = new MenuItem(LocalizeString("STR_EDITOR_MENU_VIEW_2D"), 0, ECMenuView2D);
          menu.AddItem(item);

          item = new MenuItem(LocalizeString("STR_EDITOR_MENU_VIEW_3D"), 0, ECMenuView3D);
          menu.AddItem(item);
       }

       InsertAdditionalMenuItems(menu,EMView);
    }
    break;
  default:
    {
      if (_map) {_map->_legendDisabled = true;}
      // user menu - index passed as idc
      int index = ctxEd.type - EMUser;  // index here equals position in _userMenus array
      if (index < 0 || index >= _userMenus.Size())
        break;
      InsertAdditionalMenuItems(menu,ctxEd.type);      
    }
    break;
  }

  return menu._items.Size() != 0;
}

// drawing commands

#define NOTHING GameValue()

IControl *GetControl(GameValuePar oper);

GameValue CStaticMapEditor::GetObjectArgument(RString id, RString name)
{
  if (!_ws) return RString();
  
  EditorObject *obj = _ws->FindObject(id);
  if (!obj) return RString();

  // return an array if type is multiple
  const EditorParams &params = obj->GetAllParams(); 
  for (int i=0; i<params.Size(); i++)
  {
    const EditorParam &param = params[i];
    if (stricmp(param.name, name) == 0 && stricmp(param.subtype, "multiple") == 0) 
    {
      // construct return array
      GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
      GameArrayType &array = value;

      for (int i=0; i<obj->NArguments(); i++)
        if (obj->GetArgument(i).name == name)
          array.Add(obj->GetArgument(i).value);

      return value;
    }
  }

  return obj->GetArgument(name);
}

GameValue MapEvalObjectArgument(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  const GameArrayType &array = oper2;
  int n = array.Size();
  if (n != 2)
  {
    state->SetError(EvalDim, array.Size(), 2);
    return NOTHING;
  }
  if (array[0].GetType() != GameString)
  {
    state->TypeError(GameString, array[0].GetType());
    return NOTHING;
  }
  if (array[1].GetType() != GameString)
  {
    state->TypeError(GameString, array[1].GetType());
    return NOTHING;
  }
  RString value = map->GetObjectArgument(array[0], array[1]);
  if (value.GetLength() == 0) return NOTHING;

  // allows access to editor variables (for use in returning VARIABLE_NAME)
  int gshandle = state->BeginContext(map->GetVariables());
  GameValue result = state->Evaluate(value, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
  state->EndContext(gshandle);

  return result;
}

GameValue MapGetVariable(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameString)
  {
    state->TypeError(GameString, oper2.GetType());
    return NOTHING;
  }

  RString name = oper2;
  GameVarSpace *vars = map->GetVariables();
  if (vars)
  {
    GameValue var;
    if (vars->VarGet(name, var)) return var;
  }
  return NOTHING;
}

GameValue MapSetVariable(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  const GameArrayType &array = oper2;
  int n = array.Size();
  if (n != 2)
  {
    state->SetError(EvalDim, array.Size(), 2);
    return NOTHING;
  }
  if (array[0].GetType() != GameString)
  {
    state->TypeError(GameString, array[0].GetType());
    return NOTHING;
  }

  // set the argument in the variable space
  GameVarSpace *vars = map->GetVariables();
  RString varName = array[0];
  vars->VarLocal(varName);
  vars->VarSet(varName, array[1], false);

  return NOTHING;
}

GameValue MapIsShow3DIcons(const GameState *state, GameValuePar oper1)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return false;
  return map->IsShow3DIcons();
}

GameValue MapShowLegend(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameBool)
  {
    state->TypeError(GameBool, oper2.GetType());
    return NOTHING;
  }

  map->ShowLegend(oper2);

  return NOTHING;
}

GameValue MapAllowFileOperations(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameBool)
  {
    state->TypeError(GameBool, oper2.GetType());
    return NOTHING;
  }

  map->AllowFileOperations(oper2);

  return NOTHING;
}

GameValue MapAllow3DMode(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameBool)
  {
    state->TypeError(GameBool, oper2.GetType());
    return NOTHING;
  }

  map->Allow3DMode(oper2);

  return NOTHING;
}

GameValue MapShow3DIcons(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameBool)
  {
    state->TypeError(GameBool, oper2.GetType());
    return NOTHING;
  }

  map->Show3DIcons(oper2);

  return NOTHING;
}

GameValue MapMoveObjectToEnd(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameString)
  {
    state->TypeError(GameString, oper2.GetType());
    return NOTHING;
  }

  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return NOTHING;

  EditorObject *obj = ws->FindObject(oper2);
  if (obj)
    ws->MoveObjectToEnd(obj);

  return NOTHING;
}

GameValue MapDeleteEditorObject(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameString)
  {
    state->TypeError(GameString, oper2.GetType());
    return NOTHING;
  }

  map->DeleteObjectFromEditor(oper2);
  return NOTHING;
}

GameValue MapGetObjectArgument(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return RString();

  const GameArrayType &array = oper2;
  int n = array.Size();
  if (n != 2)
  {
    state->SetError(EvalDim, array.Size(), 2);
    return RString();
  }
  if (array[0].GetType() != GameString)
  {
    state->TypeError(GameString, array[0].GetType());
    return RString();
  }
  if (array[1].GetType() != GameString)
  {
    state->TypeError(GameString, array[1].GetType());
    return RString();
  }

  return map->GetObjectArgument(array[0], array[1]);
}


GameValue MapGetMode(const GameState *state, GameValuePar oper1)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  const EnumName *eMode = GetEnumNames((EditorMode)map->GetMode());
  return eMode[map->GetMode()].name;
}

GameValue MapSetMode(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameString)
  {
    state->TypeError(GameString, oper2.GetType());
    return NOTHING;
  }

  GameStringType str = oper2;
  EditorMode editorMode = NEditorModes;
  const EnumName *names = GetEnumNames(editorMode);
  for (int i=0; names[i].IsValid(); i++)
  {
    if (stricmp(names[i].name, str) == 0)
    {
      editorMode = (EditorMode)names[i].value;
      break;
    }    
  }
  if (editorMode == NEditorModes) return NOTHING;

  map->SetMode(editorMode);
  return NOTHING;
}

GameValue MapSetEventHandler(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameArray)
  {
    state->TypeError(GameArray, oper2.GetType());
    return NOTHING;
  }
  const GameArrayType &array = oper2;
  int n = array.Size();
  if (n != 2)
  {
    state->SetError(EvalDim, array.Size(), 2);
    return NOTHING;
  }
  for (int i=0; i<n; i++)
    if (array[i].GetType() != GameString)
    {
      state->TypeError(GameString, array[i].GetType());
      return NOTHING;
    }

  map->SetEditorEventHandler(array[0], array[1]);
  return NOTHING;
}

GameValue MapAddMenu(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameArray)
  {
    state->TypeError(GameArray, oper2.GetType());
    return NOTHING;
  }
  const GameArrayType &array = oper2;
  int n = array.Size();
  if (n != 1 && n != 2)
  {
    state->SetError(EvalDim, array.Size(), 1);
    return NOTHING;
  }
  
  // text
  if (array[0].GetType() != GameString)
  {
    state->TypeError(GameString, array[0].GetType());
    return NOTHING;
  }
  RString text = array[0];

  // priority
  float priority = 0.0f;
  if (n > 1)
  {
    if (array[1].GetType() != GameScalar)
    {
      state->TypeError(GameScalar, array[1].GetType());
      return NOTHING;
    }
    priority = array[1];
  }

  return (float)map->AddMenu(text,priority);
}

inline const GameConfigType * const GetConfig(GameValuePar oper)
{
  if (oper.GetType() != GameConfig) return NULL;
  return & (static_cast<GameDataConfig *>(oper.GetData())->GetConfig());
}

GameValue MapOverlayNew(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() == GameConfig)
  {
    GameConfigType const *config = GetConfig(oper2);
    if (!config || !config->entry) return NOTHING;
    
    ConstParamEntryPtr name = config->entry->FindEntry("name");
    if (!name) return NOTHING;

    ConstParamEntryPtr types = config->entry->FindEntry("editorObjects");
    if (!types) return NOTHING;

    map->NewOverlay(config->entry);
  }
  return NOTHING;
}

GameValue MapOverlayLoad(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() == GameConfig)
  {
    GameConfigType const *config = GetConfig(oper2);
    if (!config || !config->entry) return NOTHING;
    
    ConstParamEntryPtr name = config->entry->FindEntry("name");
    if (!name) return NOTHING;

    ConstParamEntryPtr types = config->entry->FindEntry("editorObjects");
    if (!types) return NOTHING;

    map->LoadOverlay(config->entry);
  }
  return NOTHING;
}

GameValue MapOverlaySave(const GameState *state, GameValuePar oper1)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  map->SaveOverlay();

  return NOTHING;
}

GameValue MapOverlayClear(const GameState *state, GameValuePar oper1)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  map->ClearOverlay(false);

  return NOTHING;
}

GameValue MapOverlayClose(const GameState *state, GameValuePar oper1)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  map->ClearOverlay(false,true);

  return NOTHING;
}

GameValue MapOverlayCommit(const GameState *state, GameValuePar oper1)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  map->ClearOverlay(true);

  return NOTHING;
}

GameValue MapUpdateObjectTree(const GameState *state, GameValuePar oper1)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  map->UpdateObjectBrowser();

  return NOTHING;
}

GameValue MapImportAllGroups(const GameState *state, GameValuePar oper1)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

#if _VBS2
  map->ImportAllGroups();
#endif

  return NOTHING;
}

GameValue MapGetSelectedObjects(const GameState *state, GameValuePar oper1)
{
  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &array = value;

  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return value;

  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return value;

  for (int i=0; i<ws->NObjects(); i++)
  {
    EditorObject *obj = ws->GetObject(i);
    if (obj && obj->IsSelected())
      array.Add(obj->GetArgument("VARIABLE_NAME"));
  }

  return value;
}

bool GetPos( const GameState *state, Vector3 &ret, GameValuePar oper );

GameValue MapLookAtPosition(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameArray)
  {
    state->TypeError(GameArray, oper2.GetType());
    return NOTHING;
  }
  Vector3 pos;
  if (!GetPos(state, pos, oper2))
    return NOTHING;

  map->LookAtPosition(pos);

  return NOTHING;
}

GameValue MapCreateMenu(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameScalar)
  {
    state->TypeError(GameScalar, oper2.GetType());
    return NOTHING;
  }
  float index = oper2;
  map->CreateMenu((int)index);
  return NOTHING;
}

GameValue MapRemoveMenuItem(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameScalar)
  {
    state->TypeError(GameScalar, oper2.GetType());
    return NOTHING;
  }

  float findex = oper2;
  int index = (int)findex;

  map->RemoveMenuItem(index);

  return NOTHING;
}

GameValue MapRemoveMenuItemString(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameString)
  {
    state->TypeError(GameString, oper2.GetType());
    return NOTHING;
  }

  map->RemoveMenuItem(-1,oper2);

  return NOTHING;
}

GameValue MapRemoveDrawLinks(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameArray)
  {
    state->TypeError(GameArray, oper2.GetType());
    return NOTHING;
  }
  const GameArrayType &array = oper2;
  int n = array.Size();
  if (n != 2)
  {
    state->SetError(EvalDim, array.Size(), 2);
    return NOTHING;
  }
  for (int i=0; i<n; i++)
    if (array[i].GetType() != GameString)
    {
      state->TypeError(GameString, array[i].GetType());
      return NOTHING;
    }

  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return NOTHING;

  RString strFrom = array[0];
  EditorObject *from = NULL;

  for (int i=0; i<ws->NObjects(); i++)
  {
    EditorObject *obj = ws->GetObject(i);
    if (obj)
      if (stricmp(obj->GetArgument("VARIABLE_NAME"),strFrom) == 0)
      {
        from = obj;
        break;
      }
  }
  if (!from) return NOTHING;
  map->RemoveDrawLinks(from,array[1]);
  return NOTHING;
}

bool GetColor(const GameState *state, PackedColor &color, GameValuePar value);

GameValue MapDrawLink(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameArray)
  {
    state->TypeError(GameArray, oper2.GetType());
    return NOTHING;
  }
  const GameArrayType &array = oper2;
  int n = array.Size();
  if (n != 5)
  {
    state->SetError(EvalDim, array.Size(), 5);
    return NOTHING;
  }
  for (int i=0; i<4; i++)
    if (array[i].GetType() != GameString)
    {
      state->TypeError(GameString, array[i].GetType());
      return NOTHING;
    }

  //[from,to,param type,line type]

  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return NOTHING;

  RString strFrom = array[0];
  EditorObject *from = NULL;

  RString strTo = array[1];
  EditorObject *to = NULL;

  for (int i=0; i<ws->NObjects(); i++)
  {
    EditorObject *obj = ws->GetObject(i);
    if (obj)
      if (stricmp(obj->GetArgument("VARIABLE_NAME"),strFrom) == 0)
      {
        from = obj;
        if (to) break;
      }
      else if (stricmp(obj->GetArgument("VARIABLE_NAME"),strTo) == 0)
      {
        to = obj;
        if (from) break;
      }
  }
  if (!to || !from || to == from) return NOTHING;

  RString lineTypeStr = array[3];
  LineType lineType = LTLine;
  const EnumName *names = GetEnumNames(lineType);
  for (int i=0; names[i].IsValid(); i++)
    if (stricmp(names[i].name, lineTypeStr) == 0)
    {
      lineType = (LineType)names[i].value;
      break;
    }

  PackedColor color;
  if (!GetColor(state, color, array[4])) return NOTHING;

  map->SetDrawLink(from,to,array[2],lineType,color);

  return NOTHING;
}

GameValue MapNextMenuItemIndex(const GameState *state, GameValuePar oper1)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return 0.0f;
  
  return (float)map->GetNextMenuItemIndex();
}

GameValue MapGetNMenuItems(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return 0.0f;

  if (oper2.GetType() != GameString && oper2.GetType() != GameScalar)
  {
    state->TypeError(GameScalar, oper2.GetType());
    return 0.0f;
  }

  float findex = -1;
  RString menuName;
 
  if (oper2.GetType() == GameString)
    menuName = oper2;

  if (oper2.GetType() == GameScalar)
    findex = oper2;

  return (float)map->NMenuItems(menuName,(int)findex);
}

GameValue MapUpdateMenuItem(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameArray)
  {
    state->TypeError(GameArray, oper2.GetType());
    return NOTHING;
  }
  const GameArrayType &array = oper2;
  int n = array.Size();
  if (n != 2 && n != 3)
  {
    state->SetError(EvalDim, array.Size(), 3);
    return NOTHING;
  }
  // index and text
  if (array[0].GetType() != GameScalar)
  {
    state->TypeError(GameScalar, array[0].GetType());
    return NOTHING;
  }
  if (array[1].GetType() != GameString)
  {
    state->TypeError(GameString, array[1].GetType());
    return NOTHING;
  }
  float fIndex = array[0];  
  int index = (int)fIndex;
  RString text = array[1];
  RString command;

  // command
  if (array.Size() > 2)
  {
    if (array[2].GetType() != GameString)
    {
      state->TypeError(GameString, array[2].GetType());
      return NOTHING;
    }
    command = array[2];
  }

  map->UpdateMenuItem(index, text, command);

  return NOTHING;
}

GameValue MapAddMenuItem(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  float value = -1;

  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return value;

  if (oper2.GetType() != GameArray)
  {
    state->TypeError(GameArray, oper2.GetType());
    return value;
  }
  const GameArrayType &array = oper2;
  int n = array.Size();
  if (n != 3 && n != 4)
  {
    state->SetError(EvalDim, array.Size(), 3);
    return value;
  }
  // text and command
  if (array[1].GetType() != GameString)
  {
    state->TypeError(GameString, array[1].GetType());
    return value;
  }
  if (array[2].GetType() != GameString)
  {
    state->TypeError(GameString, array[2].GetType());
    return value;
  }
  RString text = array[1];  
  RString command = array[2];

  float priority = 0.0f;
  bool assignPriority = true;
  if (n == 4)
  {
    priority = array[3];
    assignPriority = false;
  }  

  // index can be text or index  
  if (array[0].GetType() == GameString)
  {
    RString type = array[0];
    value = (float)map->AddMenuItem(type,text,command,-1,priority,assignPriority);
  }
  else if (array[0].GetType() == GameScalar)
  {
    float index = array[0];
    value = (float)map->AddMenuItem(RString(),text,command,(int)index,priority,assignPriority);
  }
  else 
    state->TypeError(GameString, array[0].GetType());

  return value;
}

static void AssignScope(EditorWorkspace *ws, const EditorObject *parent, EditorObjectScope scope)
{
  for (int i=0; i<ws->NObjects(); i++)
  {
    EditorObject *obj = ws->GetObject(i);
    if (stricmp(obj->GetParent(),parent->GetArgument("VARIABLE_NAME")) == 0)
    {
      obj->SetScope(scope);
      AssignScope(ws, obj, scope);
    }
  }
}

GameValue MapGetEditorObjectScope(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return RString();

  if (oper2.GetType() != GameString) // condition
  {
    state->TypeError(GameString, oper2.GetType());
    return RString();
  }  

  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return RString();

  // find object
  RString id = oper2;
  EditorObject *obj = ws->FindObject(id);
  if (!obj) return RString();

  const EnumName *eScope = GetEnumNames((EditorObjectScope)obj->GetScope());
  return eScope[obj->GetScope()].name;  
}

GameValue MapSetEditorObjectScope(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameArray)
  {
    state->TypeError(GameArray, oper2.GetType());
    return NOTHING;
  }
  const GameArrayType &array = oper2;
  int n = array.Size();
  if (n != 5 && n != 6)
  {
    state->SetError(EvalDim, array.Size(), 5);
    return NOTHING;
  }
  if (array[0].GetType() != GameArray)  // array of objects
  {
    state->TypeError(GameArray, array[0].GetType());
    return NOTHING;
  }
  if (array[1].GetType() != GameString) // editor object type
  {
    state->TypeError(GameString, array[1].GetType());
    return NOTHING;
  }  
  if (array[2].GetType() != GameString) // condition
  {
    state->TypeError(GameString, array[2].GetType());
    return NOTHING;
  }  
  if (array[3].GetType() != GameString) // editor object scope
  {
    state->TypeError(GameString, array[3].GetType());
    return NOTHING;
  }
  if (array[4].GetType() != GameBool) // assign same scope to subordinates?
  {
    state->TypeError(GameString, array[4].GetType());
    return NOTHING;
  }
  // check objects
  const GameArrayType &objs = array[0];
  for (int i=0; i<objs.Size(); i++)
  {
    if (objs[i].GetType() != GameObject && objs[i].GetType() != GameString)
    {
      state->TypeError(GameObject, objs[i].GetType());
      return NOTHING;
    }
  }
  bool updateTree = true;
  if (n > 5)
  {
    if (array[5].GetType() != GameBool) // assign same scope to subordinates?
    {
      state->TypeError(GameString, array[5].GetType());
      return NOTHING;
    }
    updateTree = array[5];
  }

  // determine scope
  GameStringType str = array[3];
  EditorObjectScope scope = NEditorObjectScopes;
  const EnumName *names = GetEnumNames(scope);
  for (int i=0; names[i].IsValid(); i++)
  {
    if (stricmp(names[i].name, str) == 0)
    {
      scope = (EditorObjectScope)names[i].value;
      break;
    }    
  }
  if (scope == NEditorObjectScopes) return NOTHING;

  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return NOTHING;

  RString type = array[1];
  RString condition = array[2];
  bool includeSubordinates = array[4];

  const GameArrayType *allObjs = &objs;
  bool updateBrowser = false;

  // process all editor objects? compile an array of strings
  GameArrayType array2;
  if (objs.Size() == 0)
  {
    for (int i=0; i<ws->NObjects(); i++)
    {
      EditorObject *obj = ws->GetObject(i);
      if (obj)
        array2.Add(obj->GetArgument("VARIABLE_NAME"));
    }
    allObjs = &array2;
  }

  for (int i=0; i<allObjs->Size(); i++)
  {
    EditorObject *obj = NULL;
    if (allObjs->Get(i).GetType() == GameObject)
    {
      // condition
      GameVarSpace local(state->GetContext(), false);        
      int gshandle = state->BeginContext(&local);
      state->VarSetLocal("_x", allObjs->Get(i), true);
      if (condition.GetLength() == 0 || state->EvaluateBool(condition, GameState::EvalContext::_default, GWorld->GetMissionNamespace())) // mission namespace
        obj = map->FindEditorObject(allObjs->Get(i),type);
      state->EndContext(gshandle);
    }
    else if (allObjs->Get(i).GetType() == GameString)
    {
      obj = ws->FindObject(allObjs->Get(i)); 
      if (obj && stricmp(obj->GetType()->GetName(),type) == 0 || type.GetLength() == 0)
      {
        if (condition.GetLength() > 0 && stricmp(condition,"true") != 0)
        {
          // condition
          GameVarSpace local(state->GetContext(), false);        
          int edhandle = state->BeginContext(&local);
          int lochandle = state->BeginContext(map->GetVariables());
          GameValue result = state->Evaluate(RString(allObjs->Get(i)), GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
          state->EndContext(lochandle);
          state->VarSetLocal("_x", result, true);
          if (!state->EvaluateBool(condition, GameState::EvalContext::_default, GWorld->GetMissionNamespace())) // mission namespace
            obj = NULL;
          state->EndContext(edhandle);   
        }
      }
    }
    if (obj)
    {
      if (stricmp(obj->GetType()->GetName(),type) == 0 || type.GetLength() == 0)
      {          
        obj->SetScope(scope);
        updateBrowser = true;
        if (includeSubordinates)
          AssignScope(ws,obj,scope);
      }
    }
  }
  if (updateBrowser && updateTree) map->UpdateObjectBrowser();
  return NOTHING;
}

GameValue MapSelectEditorObject(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameString)
  {
    state->TypeError(GameString, oper2.GetType());
    return NOTHING;
  }

  // find the editor object
  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return NOTHING;

  // find object
  RString id = oper2;
  EditorObject *obj = ws->FindObject(id);
  if (!obj) return NOTHING;

  map->SelectObject(obj);

  return NOTHING;
}

// return the proxy object for given editor object
GameValue MapGetObjectProxy(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameString)
  {
    state->TypeError(GameString, oper2.GetType());
    return NOTHING;
  }

  // find the editor object
  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return NOTHING;

  // find object
  RString id = oper2;
  EditorObject *obj = ws->FindObject(id);
  if (!obj) return NOTHING;

  // return proxy
  Object *proxy = map->FindObject(id);
  if (proxy) 
    return GameValueExt(proxy);

  return NOTHING;
}

GameValue MapSetVisibleIfTreeCollapsed(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  const GameArrayType &array = oper2;
  int n = array.Size();
  if (n != 2)
  {
    state->SetError(EvalDim, array.Size(), 2);
    return NOTHING;
  }
  if (array[0].GetType() != GameString)
  {
    state->TypeError(GameString, array[0].GetType());
    return NOTHING;
  }
  if (array[1].GetType() != GameBool)
  {
    state->TypeError(GameBool, array[1].GetType());
    return NOTHING;
  }

  // find the editor object
  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return NOTHING;

  // find object
  RString id = array[0];
  EditorObject *obj = ws->FindObject(id);
  if (!obj) return NOTHING;

  bool visible = array[1];
  obj->SetVisibleIfTreeCollapsed(visible);

  return NOTHING;
}

// set the proxy object for a given editor object
GameValue MapSetObjectProxy(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  const GameArrayType &array = oper2;
  int n = array.Size();
  if (n != 2)
  {
    state->SetError(EvalDim, array.Size(), 2);
    return NOTHING;
  }
  if (array[0].GetType() != GameString)
  {
    state->TypeError(GameString, array[0].GetType());
    return NOTHING;
  }
  if (array[1].GetType() != GameObject)
  {
    state->TypeError(GameObject, array[1].GetType());
    return NOTHING;
  }

  // find the editor object
  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return NOTHING;

  // find object
  RString id = array[0];
  EditorObject *obj = ws->FindObject(id);
  if (!obj) return NOTHING;

  // set proxy
  Object *object = static_cast<const GameDataObject *>(array[1].GetData())->GetObject();
  if (object)
  {
    map->UpdateProxy(object,obj);
    obj->SetProxyObject(object);
  }
  else
  {
    map->DeleteProxy(obj);
    obj->SetProxyObject(NULL);
  }

  return NOTHING;
}

GameValue MapDrawIconUpdate(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  const GameArrayType &array = oper2;
  int n = array.Size();
  if (n != 9)
  {
    state->SetError(EvalDim, array.Size(), 9);
    return NOTHING;
  }
  if (array[0].GetType() != GameString)
  {
    state->TypeError(GameString, array[0].GetType());
    return NOTHING;
  }
  // find the editor object
  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return NOTHING;

  // find object
  RString id = array[0];
  EditorObject *obj = ws->FindObject(id);
  if (!obj) return NOTHING;

  // identifier
  if (array[1].GetType() != GameString)
  {
    state->TypeError(GameString, array[1].GetType());
    return NOTHING;
  }
  RString name = array[1];

  // color
  if (array[2].GetType() != GameArray)
  {
    state->TypeError(GameArray, array[2].GetType());
    return NOTHING;
  }  
  PackedColor color;
  bool GetColor(const GameState *state, PackedColor &color, GameValuePar value);
  if (!GetColor(state, color, array[2])) return NOTHING;

  // offset
  if (array[3].GetType() != GameArray)
  {
    state->TypeError(GameArray, array[3].GetType());
    return NOTHING;
  }
  const GameArrayType &posArray = array[3];
  if (posArray.Size() != 3)
  {
    state->SetError(EvalDim, posArray.Size(), 3);
    return NOTHING;
  }
  for (int i=0; i<3; i++)
  {
    if (posArray[i].GetType() != GameScalar)
    {
      state->TypeError(GameScalar,posArray[i].GetType());
      return NOTHING;
    }
  }
  Vector3 offset;
  offset[0] = posArray[0];
  offset[1] = posArray[2];  
  offset[2] = posArray[1];
  //-!

  // width
  if (array[4].GetType() != GameScalar)
  {
    state->TypeError(GameScalar, array[4].GetType());
    return NOTHING;
  }
  float width = array[4];

  // height
  if (array[5].GetType() != GameScalar)
  {
    state->TypeError(GameScalar, array[5].GetType());
    return NOTHING;
  }
  float height = array[5];

  // maintain size? (don't grow/shrink as map scale changes?)
  if (array[6].GetType() != GameBool && array[6].GetType() != GameScalar)
  {
    state->TypeError(GameBool, array[6].GetType());
    return NOTHING;
  }
  bool maintainSize = false;
  float minScale = -1;
  if (array[6].GetType() == GameBool)
    maintainSize = array[6];
  else
    minScale = array[6];

  // direction
  if (array[7].GetType() != GameScalar)
  {
    state->TypeError(GameScalar, array[7].GetType());
    return NOTHING;
  }
  float angle = array[7];

  // shadow?
  if (array[8].GetType() != GameBool)
  {
    state->TypeError(GameBool, array[8].GetType());
    return NOTHING;
  }
  bool shadow = array[8];

  int nIcons = obj->NIcons();
  for (int i=0; i<nIcons; i++)
  {
    EditorObjectIcon &icon = obj->GetIcon(i);
    if (stricmp(icon.id,name) == 0) 
    {
      icon.color = color;
      icon.offset = offset;
      icon.angle = (H_PI / 180.0f) * angle;
      icon.width = width;
      icon.height = height;
      icon.maintainSize = maintainSize;
      icon.minScale = minScale;
      icon.shadow = shadow;
    }
  }

  return NOTHING;
}

GameValue MapRemoveDrawIcon(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  const GameArrayType &array = oper2;
  int n = array.Size();
  if (n != 2)
  {
    state->SetError(EvalDim, array.Size(), 2);
    return NOTHING;
  }
  if (array[0].GetType() != GameString)
  {
    state->TypeError(GameString, array[0].GetType());
    return NOTHING;
  }
  // find the editor object
  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return NOTHING;

  // find object
  RString id = array[0];
  EditorObject *obj = ws->FindObject(id);
  if (!obj) return NOTHING;

  // identifier
  if (array[1].GetType() != GameString)
  {
    state->TypeError(GameString, array[1].GetType());
    return NOTHING;
  }
  RString name = array[1];

  obj->RemoveIcon(name);

  return NOTHING;
}

GameValue MapSetDrawIcon(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  const GameArrayType &array = oper2;
  int n = array.Size();
  if (n != 11 && n != 13 && n != 14)
  {
    state->SetError(EvalDim, array.Size(), 11);
    return NOTHING;
  }
  if (array[0].GetType() != GameString)
  {
    state->TypeError(GameString, array[0].GetType());
    return NOTHING;
  }
  // find the editor object
  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return NOTHING;

  // find object
  RString id = array[0];
  EditorObject *obj = ws->FindObject(id);
  if (!obj) return NOTHING;

  // icon name
  if (array[1].GetType() != GameString)
  {
    state->TypeError(GameString, array[1].GetType());
    return NOTHING;
  }
  RString iconName = array[1];

  // color
  if (array[2].GetType() != GameArray)
  {
    state->TypeError(GameArray, array[2].GetType());
    return NOTHING;
  }  
  PackedColor color;
  bool GetColor(const GameState *state, PackedColor &color, GameValuePar value);
  if (!GetColor(state, color, array[2])) return NOTHING;

  // offset
  if (array[3].GetType() != GameArray)
  {
    state->TypeError(GameArray, array[3].GetType());
    return NOTHING;
  }
  const GameArrayType &posArray = array[3];
  if (posArray.Size() != 3)
  {
    state->SetError(EvalDim, posArray.Size(), 3);
    return NOTHING;
  }
  for (int i=0; i<3; i++)
  {
    if (posArray[i].GetType() != GameScalar)
    {
      state->TypeError(GameScalar,posArray[i].GetType());
      return NOTHING;
    }
  }
  Vector3 offset;
  offset[0] = posArray[0];
  offset[1] = posArray[2];  
  offset[2] = posArray[1];
  //-!

  // width
  if (array[4].GetType() != GameScalar)
  {
    state->TypeError(GameScalar, array[4].GetType());
    return NOTHING;
  }
  float width = array[4];

  // height
  if (array[5].GetType() != GameScalar)
  {
    state->TypeError(GameScalar, array[5].GetType());
    return NOTHING;
  }
  float height = array[5];

  // maintain size? (don't grow/shrink as map scale changes?)
  if (array[6].GetType() != GameBool && array[6].GetType() != GameScalar)
  {
    state->TypeError(GameBool, array[6].GetType());
    return NOTHING;
  }
  bool maintainSize = false;
  float minScale = -1;
  if (array[6].GetType() == GameBool)
    maintainSize = array[6];
  else
    minScale = array[6];

  // direction
  if (array[7].GetType() != GameScalar)
  {
    state->TypeError(GameScalar, array[7].GetType());
    return NOTHING;
  }
  float angle = array[7];

  // string identifier
  if (array[8].GetType() != GameString)
  {
    state->TypeError(GameString, array[8].GetType());
    return NOTHING;
  }
  RString name = array[8];

  // shadow?
  if (array[9].GetType() != GameBool)
  {
    state->TypeError(GameBool, array[9].GetType());
    return NOTHING;
  }
  bool shadow = array[9];

  // text
  if (array[10].GetType() != GameString)
  {
    state->TypeError(GameString, array[10].GetType());
    return NOTHING;
  }
  RString text = array[10];

  bool is3D = false;
  bool line = false;
  float priority = 0;
  if (n > 11)
  {
    // is3D?
    if (array[11].GetType() != GameBool)
    {
      state->TypeError(GameBool, array[11].GetType());
      return NOTHING;
    }
    is3D = array[11];

    // show line?
    if (array[12].GetType() != GameBool)
    {
      state->TypeError(GameBool, array[12].GetType());
      return NOTHING;
    }
    line = array[12];

    if (n > 13)
    {
      // priority
      if (array[13].GetType() != GameScalar)
      {
        state->TypeError(GameScalar, array[13].GetType());
        return NOTHING;
      }
      priority = array[13];
    }
  }

  // set icon 2D
  Ref<Texture> texture = GlobLoadTexture(GetVehicleIcon(iconName));
  if (texture)
  {
    // only one icon of each ID is allowed
    obj->RemoveIcon(name);

    EditorObjectIcon icon;

    icon.icon = texture;
    icon.color = color;
    icon.offset = offset;
    icon.width = width;
    icon.height = height;
    icon.angle = (H_PI / 180.0f) * angle;
    icon.shadow = shadow;
    icon.line = line;
    icon.is3D = is3D;
    icon.id = name;
    icon.maintainSize = maintainSize;
    icon.minScale = minScale;
    icon.priority = priority;
    icon.text = text;

    obj->AddIcon(icon);

    //LogF("Set icon for %s to %s (id: %s)",obj->GetArgument("VARIABLE_NAME").Data(),iconName.Data(),name.Data());
  }

  return NOTHING;
}

// convert arguments from a GameArray to an array of RString
static bool ConvertArguments(const GameState *state,const GameArrayType &argArray,AutoArray<RString> &args)
{
  for (int i=0; i<argArray.Size()-1; i+=2)
  {
    // check type of argument
    if (argArray[i].GetType() != GameString)
    {
      state->TypeError(GameString, argArray[i].GetType());
      return false;
    }

    // convert value to string
    RString value;

    // is it an array? assume subtype multiple
    if (argArray[i+1].GetType() == GameArray)
    {
      RString name = argArray[i];
      const GameArrayType &array = argArray[i+1];
      for (int j=0; j<array.Size(); j++) 
      {
        if (array[j].GetType() == GameString)
          value = array[j];
        else
          value = array[j].GetText();
        args.Add(argArray[i]); args.Add(value);
      }
      continue;
    }

    if (argArray[i+1].GetType() == GameString)
      value = argArray[i+1];
    else
      value = argArray[i+1].GetText();
    args.Add(argArray[i]); args.Add(value);
  }
  args.Compact();

  return true;
}

// TODO: enumerate possible editor scripts
GameValue MapExecEditorScript(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  const GameArrayType &array = oper2;
  if (array.Size() != 2)
  {
    state->SetError(EvalDim, array.Size(), 2);
    return NOTHING;
  }
  if (array[0].GetType() != GameString)
  {
    state->TypeError(GameString, array[0].GetType());
    return NOTHING;
  }
  if (array[1].GetType() != GameString)
  {
    state->TypeError(GameArray, array[1].GetType());
    return NOTHING;
  }

  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return NOTHING;

  // find object
  RString id = array[0];
  EditorObject *obj = ws->FindObject(id);
  if (!obj) return NOTHING;

  RString script = array[1];

  QOStrStream out;

  if (stricmp(script,"create") == 0)
    obj->WriteCreateScript(out);
  else if (stricmp(script,"update") == 0)
    obj->WriteUpdateScript(out);
  else if (stricmp(script,"updateposition") == 0)
    obj->WriteUpdatePositionScript(out);
  else if (stricmp(script,"select") == 0)
    obj->WriteSelectScript(out);
  else if (stricmp(script,"destroy") == 0)
    obj->WriteDeleteScript(out);
  else
    return NOTHING;

  GameState *gstate = GWorld->GetGameState();
  int gshandle = gstate->BeginContext(map->GetVariables());
  /*
  // TODO: allow custom variables (ie _new?)
  gstate->VarLocal("_new");
  gstate->VarSet("_new", GameValue(isNewObject), true, false, GWorld->GetMissionNamespace());
  */
  gstate->Execute(RString(out.str(), out.pcount()), GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
  gstate->EndContext(gshandle);

  return NOTHING;
}

GameValue MapSetObjectArguments(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  const GameArrayType &array = oper2;
  if (array.Size() != 2)
  {
    state->SetError(EvalDim, array.Size(), 2);
    return NOTHING;
  }
  if (array[0].GetType() != GameString)
  {
    state->TypeError(GameString, array[0].GetType());
    return NOTHING;
  }
  if (array[1].GetType() != GameArray)
  {
    state->TypeError(GameArray, array[1].GetType());
    return NOTHING;
  }

  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return NOTHING;

  // find object
  RString id = array[0];
  EditorObject *obj = ws->FindObject(id);
  if (!obj) return NOTHING;

  // any empty arrays passed in? if so, clear corresponding arguments of subtype multiple
  const GameArrayType &argArray = array[1];
  for (int i=0; i<argArray.Size()-1; i+=2)
  {  
    if (argArray[i].GetType() == GameString && argArray[i+1].GetType() == GameArray)
    {
      const GameArrayType &value = argArray[i+1];
      if (value.Size() == 0)
      {
        RString name = argArray[i];
        obj->RemoveArgument(name);
      }
    }
  }

  AutoArray<RString> args;
  if (!ConvertArguments(state,array[1],args)) return NOTHING;

  // assign arguments
  obj->SetArguments(args);

  return NOTHING;
}

EditorObject *CStaticMapEditor::FindEditorObject(RString type, AutoArray<EditorArgument> &args)
{
  for (int i=0; i<_ws->NObjects(); i++)
  {
    EditorObject *obj = _ws->GetObject(i);
    if (stricmp(obj->GetType()->GetName(), type) != 0) continue;
    bool match = true;
    for (int i=0; i<args.Size(); i++)
    {
      match = obj->HasArgument(args[i]);
      if (!match)
        break;
    }
    if (match) return obj;
  }
  return NULL;
}

EditorObject *CStaticMapEditor::FindEditorObject(GameValue value,RString type)
{
  GameState *gstate = GWorld->GetGameState();
  int gshandle = gstate->BeginContext(_vars);
  EditorObject *rtn = NULL;
  for (int i=0; i<_ws->NObjects(); i++)
  {
    EditorObject *obj = _ws->GetObject(i);
    if (!obj) continue;
    if (type.GetLength() > 0 && stricmp(obj->GetType()->GetName(),type) != 0) continue; 
    GameValue result;
    result = gstate->Evaluate(obj->GetArgument("VARIABLE_NAME"), GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
    if (result.GetText() == value.GetText())  // IsEqualTo doesn't work for game arrays
    {
      rtn = obj;
      break;
    }
  }
  gstate->EndContext(gshandle);
  return rtn;
}

GameValue MapFindEditorObjectFromValue(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return RString();

  // is a gamevalue passed in? find corresponding editor object
  EditorObject *obj = map->FindEditorObject(oper2);
  if (obj) return obj->GetArgument("VARIABLE_NAME");
  return RString();
}

GameValue MapFindEditorObject(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return RString();

  const GameArrayType &array = oper2;
  int n = array.Size();

  // find editor object from value?
  if (n == 2)
  {
    if (array[0].GetType() != GameString)
    {
      state->TypeError(GameString, array[0].GetType());
      return RString();
    }
    EditorObject *obj = map->FindEditorObject(array[1],array[0]);
    if (obj) return obj->GetArgument("VARIABLE_NAME");
    return RString();
  }

  if ((n % 2) != 1)
  {
    state->SetError(EvalDim, array.Size(), 3);
    return RString();
  }
  for (int i=0; i<n; i++)
  {
    if (array[i].GetType() != GameString)
    {
      state->TypeError(GameString, array[i].GetType());
      return RString();
    }
  }

  RString type = array[0];
  int count = (n - 1) / 2;
  
  AutoArray<EditorArgument> args;
  args.Realloc(count);
  args.Resize(count);

  int j = 1;
  for (int i=0; i<count; i++)
  {
    args[i].name = array[j++];
    args[i].value = array[j++];
  }

  EditorObject *obj = map->FindEditorObject(type, args);
  if (obj) return obj->GetArgument("VARIABLE_NAME");
  return RString();
}

GameValue MapGetObjectChildren(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  GameValue value = state->CreateGameValue(GameArray);
  GameArrayType &array = value;

  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return value;
  
  if (oper2.GetType() != GameString)
  {
    state->TypeError(GameString, oper2.GetType());
    return value;
  }

  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return value;
  
  RString id = oper2;

  // find children
  for (int i=0; i<ws->NObjects(); i++)
  {
    EditorObject *obj = ws->GetObject(i);
    if (!obj) continue;
    if (stricmp(obj->GetArgument("PARENT"), id) == 0) array.Add(obj->GetArgument("VARIABLE_NAME"));
  }

  array.Compact();
  return value;
}

GameValue MapListObjects(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  GameValue value = state->CreateGameValue(GameArray);
  GameArrayType &array = value;

  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return value;

  RString type = oper2;

  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return value;
  for (int i=0; i<ws->NObjects(); i++)
  {
    EditorObject *obj = ws->GetObject(i);
    if (!obj) continue;
    if (stricmp(obj->GetType()->GetName(), type) != 0) continue;
    if (obj->GetParentOverlay() != ws->GetActiveOverlay()) continue;
    array.Add(obj->GetArgument("VARIABLE_NAME"));
  }
  array.Compact();
  return value;
}

GameValue MapInsertObject(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  const GameArrayType &array = oper2;
  if (array.Size() < 3 || array.Size() > 4)
  {
    state->SetError(EvalDim, array.Size(), 3);
    return NOTHING;
  }
  if (array[0].GetType() != GameString)
  {
    state->TypeError(GameString, array[0].GetType());
    return NOTHING;
  }
  if (array[2].GetType() != GameArray)
  {
    state->TypeError(GameArray, array[2].GetType());
    return NOTHING;
  }

  // get arguments from array
  const GameArrayType &argArray = array[2];
  AutoArray<RString> args;
  if (!ConvertArguments(state,argArray,args)) return NOTHING;

  // subtype specified?
  ConstParamEntryPtr subTypeConfig = NULL;
  if (array.Size() == 4)
  {
    if (array[3].GetType() != GameConfig)
    {
      state->TypeError(GameConfig, array[3].GetType());
      return NOTHING;
    }
    const GameConfigType &configEntry = static_cast<GameDataConfig *>(array[3].GetData())->GetConfig();
    subTypeConfig = configEntry.entry;
  }

  // new object of given type
  RString typeName = array[0];
  return map->AddObject(typeName, args, false, array[1], subTypeConfig);

}

GameValue MapEditObject(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameString)
  {
    state->TypeError(GameString, oper2.GetType());
    return NOTHING;
  }

  map->ShowEditObject(oper2);

  return NOTHING;
}

GameValue MapShowNewObject(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  const GameArrayType &array = oper2;
  if (array.Size() != 4)
  {
    state->SetError(EvalDim, array.Size(), 4);
    return NOTHING;
  }
  if (array[0].GetType() != GameString)
  {
    state->TypeError(GameString, array[0].GetType());
    return NOTHING;
  }
  if (array[1].GetType() != GameString)
  {
    state->TypeError(GameString, array[1].GetType());
    return NOTHING;
  }

  TargetSide side = GetSide(array[2]);
  if (side > TSideUnknown) side = TSideUnknown;

  Vector3 pos;
  if (!GetPos(state, pos, array[3]))
  {
    return NOTHING;
  }

  RString type = array[0];
  RString className = array[1];

  map->ShowNewObject(type,className,side,pos);
  return NOTHING;
}

GameValue MapAddObject(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  const GameArrayType &array = oper2;
  if (array.Size() < 2 || array.Size() > 3)
  {
    state->SetError(EvalDim, array.Size(), 2);
    return NOTHING;
  }
  if (array[0].GetType() != GameString)
  {
    state->TypeError(GameString, array[0].GetType());
    return NOTHING;
  }

  if (array[1].GetType() != GameArray)
  {
    state->TypeError(GameArray, array[1].GetType());
    return NOTHING;
  }

  // get arguments from array
  const GameArrayType &argArray = array[1];
  AutoArray<RString> args;
  if (!ConvertArguments(state,argArray,args)) return NOTHING;

  // subtype specified?
  ConstParamEntryPtr subTypeConfig = NULL;
  if (array.Size() == 3)
  {
    if (array[2].GetType() != GameConfig)
    {
      state->TypeError(GameConfig, array[2].GetType());
      return NOTHING;
    }
    const GameConfigType &configEntry = static_cast<GameDataConfig *>(array[2].GetData())->GetConfig();
    subTypeConfig = configEntry.entry;
  }

  // new object of given type
  RString typeName = array[0];
  return map->AddObject(typeName, args, true, NOTHING, subTypeConfig);
}

GameValue MapIsRealTime( const GameState *state, GameValuePar oper1 )
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  return map->IsRealTime();
}

#define OBJECT_NULL GameValueExt((Object *)NULL)

GameValue CameraGet( const GameState *state, GameValuePar oper1 )
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return OBJECT_NULL;

  Object *obj;
  obj = map->CameraGet();
  if (obj)
    return GameValueExt(map->CameraGet());
  return OBJECT_NULL;
}

GameValue CameraRestart( const GameState *state, GameValuePar oper1 )
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  map->CameraRestart();

  return NOTHING;
}

GameValue MapCollapseTree( const GameState *state, GameValuePar oper1 )
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;
  
  EditorWorkspace *ws = map->GetWorkspace();
  if (!ws) return NOTHING;

  // hide all children
  for (int i=0; i<ws->NObjects(); i++)
  {
    EditorObject *obj = ws->GetObject(i);
    if (!obj) continue;
    if (obj->GetParent().GetLength() > 0)
      obj->SetVisibleInTree(false);
  }

  map->UpdateObjectBrowser();

  return NOTHING;
}


GameValue MapOnDoubleClick( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameString)
  {
    state->TypeError(GameArray, oper1.GetType());
    return NOTHING;
  }

  map->SetOnDoubleClick(oper2);
  return NOTHING;
}

GameValue MapOnShowNewObject( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  CStaticMapEditor *map = dynamic_cast<CStaticMapEditor *>(GetControl(oper1));
  if (!map || !map->HasParent()) return NOTHING;

  if (oper2.GetType() != GameString)
  {
    state->TypeError(GameArray, oper1.GetType());
    return NOTHING;
  }

  map->SetOnShowNewObject(oper2);
  return NOTHING;
}

// combined types
const GameType GameStringOrScalar(GameString | GameScalar);

// helpers only for template instantiation
extern GameTypeType GTypeGameStringOrScalar;

CachedJavaClass ConvertTypes<&GTypeGameStringOrScalar>::_cache("com/bistudio/JNIScripting/NativeObject");

#define FUNCTIONS_EDITOR(XX, Category) \
  XX(GameBool, "isRealTime", MapIsRealTime, GameControl, "map", "Returns true if the mission editor is operating in real time mode.", "_isRealTime = isRealTime _map", "", "2.92", "", Category) \
  XX(GameNothing, "collapseObjectTree", MapCollapseTree, GameControl, "map", "Collapse the object tree.", "collapseObjectTree _map", "", "2.92", "", Category) \
  XX(GameNothing, "saveOverlay", MapOverlaySave, GameControl, "map", "Save the current overlay.", "saveOverlay _map", "", "2.92", "", Category) \
  XX(GameNothing, "clearOverlay", MapOverlayClear, GameControl, "map", "Clear the current overlay.", "clearOverlay _map", "", "2.92", "", Category) \
  XX(GameNothing, "commitOverlay", MapOverlayCommit, GameControl, "map", "Commit the current overlay.", "commitOverlay _map", "", "2.92", "", Category) \
  XX(GameNothing, "closeOverlay", MapOverlayClose, GameControl, "map", "Closes the current overlay without committing.", "", "", "2.92", "", Category) \
  XX(GameNothing, "updateObjectTree", MapUpdateObjectTree, GameControl, "map", "Update the editor object tree.", "", "", "2.92", "", Category) \
  XX(GameNothing, "selectedEditorObjects", MapGetSelectedObjects, GameControl, "map", "Returns a list of currently selected editor objects.", "", "", "2.92", "", Category) \
  XX(GameObject, "getEditorCamera", CameraGet, GameControl, "map", "Fetches a reference to the mission editor camera.", "", "", "2.92", "", Category) \
  XX(GameNothing, "restartEditorCamera", CameraRestart, GameControl, "map", "Restarts the mission editor camera (if it was deleted by a script, for example).", "", "", "2.92", "", Category) \
  XX(GameNothing, "importAllGroups", MapImportAllGroups, GameControl, "map", "Imports all groups into the RTE.", "", "", "2.92", "", Category) \
  XX(GameString, "getEditorMode", MapGetMode, GameControl, "map", "Returns the current mode of the editor.", "", "", "2.92", "", Category) \
  XX(GameBool, "isShowing3DIcons", MapIsShow3DIcons, GameControl, "map", "Returns true if the editor is set to draw 3D icons.", "", "", "2.92", "", Category) \
  XX(GameScalar, "nextMenuItemIndex", MapNextMenuItemIndex, GameControl, "map", "Returns the next available menu item index.", "", "", "2.92", "", Category) \

FUNCTIONS_EDITOR(JNI_FUNCTION, "Editor")

static const GameFunction EditorUnary[]=
{
  FUNCTIONS_EDITOR(REGISTER_FUNCTION, "Editor")
};

#if DOCUMENT_COMREF
static ComRefFunc ExtComRefFunc1[] =
{
  FUNCTIONS_EDITOR(COMREF_FUNCTION, "Editor")
};
#endif

#define OPERATORS_EDITOR(XX, Category) \
  XX(GameAny, "evalObjectArgument", function, MapEvalObjectArgument, GameControl, GameArray, "map", "[object, argument]", "Return object argument in mission editor.", "", "", "2.35", "", Category) \
  XX(GameNothing, "setVariable", function, MapSetVariable, GameControl, GameArray, "map", "[name, value]", "Set variable to given value in the variable space of given map.", "", "", "2.92", "", Category) \
  XX(GameNothing, "getVariable", function, MapGetVariable, GameControl, GameString, "map", "name", "Get variable from the variable space of given map.", "", "", "2.92", "", Category) \
  XX(GameString, "getObjectArgument", function, MapGetObjectArgument, GameControl, GameArray, "map", "[object, argument]", "Return name of object argument in mission editor.", "", "", "2.35", "", Category) \
  XX(GameString, "findEditorObject", function, MapFindEditorObject, GameControl, GameArray, "map", "[type, name1, value1, ...]", "Return object of given type with given arguments. Use [type, game value] to search by object reference of a specific editor object type.", "", "", "2.35", "", Category) \
  XX(GameString, "findEditorObject", function, MapFindEditorObjectFromValue, GameControl, GameAny, "map", "value", "Return object that matches the provided reference.", "", "", "2.92", "", Category) \
  XX(GameArray, "listObjects", function, MapListObjects, GameControl, GameString, "map", "type", "Return the list of all objects of given type.", "", "", "2.92", "", Category) \
  XX(GameString, "addEditorObject", function, MapAddObject, GameControl, GameArray, "map", "[type, [name1, value1, ...], subtype class]", "Add an object to the editor and assign arguments. Create script is called with _new equal to true. Returns the ID of the new EditorObject. Subtype class is optional.", "", "", "2.92", "", Category) \
  XX(GameString, "insertEditorObject", function, MapInsertObject, GameControl, GameArray, "map", "[type, value, [name1, value1, ...], subtype class]", "Insert an object to the editor and assign arguments. Create script is not called. Returns the ID of the new EditorObject. Subtype class is optional.", "", "", "2.92", "", Category) \
  XX(GameAny, "setObjectArguments", function, MapSetObjectArguments, GameControl, GameArray, "map", "[object, [name1, value1, ...]]", "Set object arguments in mission editor.", "", "", "2.92", "", Category) \
  XX(GameObject, "getObjectProxy", function, MapGetObjectProxy, GameControl, GameString, "map", "object", "Return the proxy object associated with the given editor object.", "", "", "2.92", "", Category) \
  XX(GameAny, "setObjectProxy", function, MapSetObjectProxy, GameControl, GameArray, "map", "[object, proxy object]", "Set the proxy object associated with the given editor object.", "", "", "2.92", "", Category) \
  XX(GameAny, "deleteEditorObject", function, MapDeleteEditorObject, GameControl, GameString, "map", "object", "Delete the editor object. Requires all editor object links to be removed prior.", "", "", "2.92", "", Category) \
  XX(GameArray, "getObjectChildren", function, MapGetObjectChildren, GameControl, GameString, "map", "object", "Return a list of all the children of the specified object.", "", "", "2.92", "", Category) \
  XX(GameAny, "execEditorScript", function, MapExecEditorScript, GameControl, GameArray, "map", "[object, script]", "Execute an editor script for the specified object.", "_map execEditorScript [\"_team_1\", \"create\"]", "", "2.92", "", Category) \
  XX(GameAny, "showNewEditorObject", function, MapShowNewObject, GameControl, GameArray, "map", "[type, class, side, position]", "Show the add editor object dialog, type is editor object type, class is class definition to automatically select, side filters by a certain side, pos is position to create the object.", "", "", "2.92", "", Category) \
  XX(GameAny, "editObject", function, MapEditObject, GameControl, GameString, "map", "object", "Show the edit object dialog for the given object.", "", "", "2.92", "", Category) \
  XX(GameAny, "onDoubleClick", function, MapOnDoubleClick, GameControl, GameString, "map", "command", "Defines an action performed when the user double clicks on the map. Command receives:<br/>\r\n<br/>\r\n_pos <t>array</t> position<br/>\r\n_units <t>array</t> selected units<br/>\r\n_shift,_alt <t>bool</t> key state", "", "", "2.92", "", Category) \
  XX(GameAny, "onShowNewObject", function, MapOnShowNewObject, GameControl, GameString, "map", "command", "Defines an action performed when the user right clicks on the map and selects New Object. Set to empty for default behavior. Command receives:<br/>\r\n<br/>\r\n_pos <t>array</t> position", "", "", "2.92", "", Category) \
  XX(GameAny, "selectEditorObject", function, MapSelectEditorObject, GameControl, GameString, "map", "object", "Select an editor object. Does not un-select previously selected objects.", "", "", "2.92", "", Category) \
  XX(GameNothing, "setEditorObjectScope", function, MapSetEditorObjectScope, GameControl, GameArray, "map", "[objects,editor type,condition,scope,subordinates also]", "This command defines the level of access a user has to editor objects. objects is an array of either Editor Objects (eg [""_unit_0""]) or actual Game Objects (eg [player]). If the array is empty then the command will automatically parse all editor objects. editor type is the editor type to effect (eg ""unit""), or """" for none. condition is an executable string that must evaluate to true or false. If true, the scope of the evaluated editor object will be modified. scope is one of ""HIDE"", ""VIEW"", ""SELECT"", ""LINKTO"", ""LINKFROM"", ""ALLNODRAG"", ""ALLNOTREE"" or ""ALL"". subordinates also is a boolean value, if true then subordinates in the editor will be assigned the same scope as the parent.", "_map setEditorObjectScope [[],""vehicle"",""side effectiveCommander _x != side player"",""HIDE"",false];", "", "2.92", "", Category) \
  XX(GameString, "getEditorObjectScope", function, MapGetEditorObjectScope, GameControl, GameString, "map", "object", "Returns the editor object scope of the specified editor object.", "", "", "2.92", "", Category) \
  XX(GameScalar, "addMenuItem", function, MapAddMenuItem, GameControl, GameArray, "map", "[menu or index,text,command,priority]", "Creates a new menu item. Menu can be \"file\" or \"view\", index is index as returned from addMenu command. priority is optional and determines where in the menu the item will reside (higher priority items first).", "", "", "2.92", "", Category) \
  XX(GameNothing, "updateMenuItem", function, MapUpdateMenuItem, GameControl, GameArray, "map", "[menu item index,text,command]", "Sets the text and command for the menu item. index is index as returned from addMenuItem command. command is optional.", "", "", "2.92", "", Category) \
  XX(GameNothing, "removeMenuItem", function, MapRemoveMenuItem, GameControl, GameScalar, "map", "index of menu item to delete", "Removes a previously added menu item.", "", "", "2.92", "", Category) \
  XX(GameNothing, "removeMenuItem", function, MapRemoveMenuItemString, GameControl, GameString, "map", "text of menu item to delete", "Removes a previously added menu item.", "", "", "2.92", "", Category) \
  XX(GameScalar, "nMenuItems", function, MapGetNMenuItems, GameControl, GameStringOrScalar, "map", "menu index", "Returns the total number of user-added menu items belonging to the given menu.", "", "", "2.92", "", Category) \
  XX(GameNothing, "setEditorMode", function, MapSetMode, GameControl, GameString, "map", "mode", "Sets map mode to ""MAP"", ""3D"" or ""PREVIEW"".", "", "", "2.92", "", Category) \
  XX(GameNothing, "editorSetEventHandler", function, MapSetEventHandler, GameControl, GameArray, "map", "[handler name, function]", "Sets given event handler of given editor.", "_map editorSetEventHandler [\"SelectObject\", \"\"]", "", "2.92", "", Category) \
  XX(GameNothing, "setVisibleIfTreeCollapsed", function, MapSetVisibleIfTreeCollapsed, GameControl, GameArray, "map", "[object, visible if tree collapsed]", "Sets whether or not the object is visible even if the tree is collapsed.", "", "", "2.92", "", Category) \
  XX(GameScalar, "addMenu", function, MapAddMenu, GameControl, GameArray, "map", "[text,priority]", "Adds a new menu button. Priority is optional.", "", "", "2.92", "", Category) \
  XX(GameNothing, "createMenu", function, MapCreateMenu, GameControl, GameScalar, "map", "index", "Creates a previously added menu.", "", "", "2.92", "", Category) \
  XX(GameNothing, "newOverlay", function, MapOverlayNew, GameControl, GameConfig, "map", "config", "Creates the new overlay dialog for the specified type of overlay.", "", "", "2.92", "", Category) \
  XX(GameNothing, "loadOverlay", function, MapOverlayLoad, GameControl, GameConfig, "map", "config", "Creates the load overlay dialog for the specified type of overlay.", "", "", "2.92", "", Category) \
  XX(GameNothing, "lookAtPos", function, MapLookAtPosition, GameControl, GameArray, "map", "position", "Center the map on, and point the camera at, the position.", "", "", "2.92", "", Category) \
  XX(GameNothing, "drawLink", function, MapDrawLink, GameControl, GameArray, "map", "[from, to, param type, line type, color]", "The editor will draw a line between the two specified editor objects. Line type can be ""LINE"" or ""ARROW"".", "", "", "2.92", "", Category) \
  XX(GameNothing, "removeDrawLinks", function, MapRemoveDrawLinks, GameControl, GameArray, "map", "[from, param type]", "Remove all drawn links for the given editor object for the given editor object type. Pass an empty string as param type to remove all draw links for an object.", "", "", "2.92", "", Category) \
  XX(GameNothing, "setDrawIcon", function, MapSetDrawIcon, GameControl, GameArray, "map", "[object, texture, color, offset, width, height, maintain size?, angle, string identifier, shadow, is3D, draw line?, priority]", "Set the icon to be shown in 2D editor for the specified editor object. If maintain size is false, icon will not scale depending on the scale of the map. If maintain size is a number, the icon will maintain size if map scale is below that number. is3D, show line and priority are optional.", "", "", "2.92", "", Category) \
  XX(GameNothing, "updateDrawIcon", function, MapDrawIconUpdate, GameControl, GameArray, "map", "[object, string identifier, color, offset, width, height, maintain size?, angle, shadow]", "Updates the icon to be shown in 2D editor for the specified editor object. If maintain size is false, icon will not scale depending on the scale of the map. If maintain size is a number, the icon will maintain size if map scale is below that number.", "", "", "2.92", "", Category) \
  XX(GameNothing, "removeDrawIcon", function, MapRemoveDrawIcon, GameControl, GameArray, "map", "[object, string identifier]", "Removes an icon for an editor object.", "", "", "2.92", "", Category) \
  XX(GameNothing, "moveObjectToEnd", function, MapMoveObjectToEnd, GameControl, GameString, "map", "object", "Shifts an editor object to the end of the objects array. This means that the object will be drawn last (after all other objects).", "", "", "2.92", "", Category) \
  XX(GameNothing, "show3DIcons", function, MapShow3DIcons, GameControl, GameBool, "map", "bool", "Toggle the drawing of 3D icons.", "", "", "2.92", "", Category) \
  XX(GameNothing, "allow3DMode", function, MapAllow3DMode, GameControl, GameBool, "map", "bool", "Allow/dissallow 3D mode.", "", "", "2.92", "", Category) \
  XX(GameNothing, "allowFileOperations", function, MapAllowFileOperations, GameControl, GameBool, "map", "bool", "Allow/dissallow file ops (load/save etc).", "", "", "2.92", "", Category) \
  XX(GameNothing, "showLegend", function, MapShowLegend, GameControl, GameBool, "map", "bool", "Show/hide map legend.", "", "", "2.92", "", Category)

OPERATORS_EDITOR(JNI_OPERATOR, "Editor")

static const GameOperator EditorBinary[]=
{
  OPERATORS_EDITOR(REGISTER_OPERATOR, "Editor")
};

#if DOCUMENT_COMREF
static ComRefFunc ExtComRefFunc2[] =
{
  OPERATORS_EDITOR(COMREF_OPERATOR, "Editor")
};
#endif

#define REGISTER_ATTR(name, variable, func) \
  variable = EditorMenuAttributeTypes.attributes.AddValue(name); \
  EditorMenuAttributeTypes.createFunctions.Access(variable); \
  EditorMenuAttributeTypes.createFunctions[variable] = func;

#include <El/Modules/modules.hpp>

INIT_MODULE(MissionEditor, 2)
{
  for( int i=0; i<sizeof(EditorUnary)/sizeof(*EditorUnary); i++ )
  {
    GGameState.NewFunction(EditorUnary[i]);
  }
  for (int i=0; i<sizeof(EditorBinary)/sizeof(*EditorBinary); i++)
  {
    GGameState.NewOperator(EditorBinary[i]);
  }

#if DOCUMENT_COMREF
  for (int i=0; i<lenof(ExtComRefFunc1); i++)
  {
    GGameState.AddComRefFunction(ExtComRefFunc1[i]);
  }
  for (int i=0; i<lenof(ExtComRefFunc2); i++)
  {
    GGameState.AddComRefFunction(ExtComRefFunc2[i]);
  }
#endif

  // Register menu attributes
  ATTRIBUTES_LIST(REGISTER_ATTR);
  EditorMenuAttributeTypes.attributes.Close();
};


#endif // _ENABLE_EDITOR2

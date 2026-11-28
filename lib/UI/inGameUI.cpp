/*!
\file
HUD UI - In-game UIl layer
\patch_internal 1.01 Date 6/29/2001 by Ondra.
Win32 call replaced by corresponding C counterparts.
Win32 are DLL linked and make protection impossible,
as they are changed by macromedia protection.
*/

#include "../wpch.hpp"
#include "../world.hpp"
#include "../AI/ai.hpp"
#include "../AI/aiRadio.hpp"
#include "../person.hpp"
#include "../keyInput.hpp"
#include "../joystick.hpp"
#include "InGameUIImpl.hpp"
#include "../engine.hpp"
#include "../camera.hpp"
#include "../landscape.hpp"
#include "../detector.hpp"
#include "../house.hpp"
#include <El/QStream/qbStream.hpp>
#include "../Network/network.hpp"
#include <El/Common/randomGen.hpp>
#include "../cameraHold.hpp"
#include "../diagModes.hpp"

#include "../move_actions.hpp"
#include "../manActs.hpp"
#include "../fileLocator.hpp"

#include <El/ParamFile/paramFile.hpp>
#include <El/Evaluator/express.hpp>
#include <El/Common/perfProf.hpp>

#include "resincl.hpp"
#include <ctype.h>
#ifdef _WIN32
  #include <io.h>
#endif
#include "../dikCodes.h"

#include <Es/Algorithms/qsort.hpp>

#include "chat.hpp"

#include "../stringtableExt.hpp"
#include "../mbcs.hpp"
#include <stdint.h>

#include "../drawText.hpp"
#include "../progress.hpp"

static int CompareUIObjectAddress(const void *left, const void *right)
{
  const uintptr_t l = reinterpret_cast<uintptr_t>(left);
  const uintptr_t r = reinterpret_cast<uintptr_t>(right);
  return l < r ? -1 : (l > r ? 1 : 0);
}

#include "../gameStateExt.hpp"

/*
#define FontS "tahomaB24"
#define FontM "tahomaB36"
*/

#if SPEEREO_SPEECH
#include <Es/Common/win.h>
#include <SpeereoSpeech/hdlcut.h>
#pragma comment(lib, "hdlcutmt")
#include <El/Interfaces/iAppInfo.hpp>
#endif

///////////////////////////////////////////////////////////////////////////////
// class UIActions - action list

#define ACTIONS_CREATE(name, type) \
  case AT##name: \
    return new type(AT##name);

Action *CreateAction(UIActionType type)
{
  switch (type)
  {
    ACTIONS_DEFAULT(ACTIONS_CREATE)
#if _ENABLE_CONVERSATION
    ACTIONS_CONVERSATION(ACTIONS_CREATE)
#endif    
#if _HELISIM_ROTORLIB
    ACTIONS_HELISIM(ACTIONS_CREATE)
#endif
#if _VBS3 && _ENABLE_CARRY_BODY //BattlefieldClearance
    ACTIONS_CARRY_BODY(ACTIONS_CREATE)
#endif
#if _VBS2
    ACTIONS_VBS2(ACTIONS_CREATE)
#endif
  }
  RptF("Unknown action type: %d", safe_cast<int>(type));
  return NULL;
}

bool Action::HasEqualType(const Action *to) const
{
  return to && _type == to->GetType();
}

int Action::Compare(const Action *with) const
{
  int s = CompareUIObjectAddress(GetTarget(), with->GetTarget());
  if (s != 0) return s;
  s = (int)GetType() - (int)with->GetType();
  return s;
}

INode *FormatText(const char *format, RefR<INode> *args, int nArgs, bool structured);

RefR<INode> Action::GetText(AIBrain *unit) const
{
  UIActionParams params;
  if (!GetParams(params, unit)) return NULL;
  return FormatText(UIActions::actionTypes[GetType()].text, params.Data(), params.Size(), true);
}

RefR<INode> Action::GetTextDefault(AIBrain *unit) const
{
  UIActionParams params;
  if (!GetParams(params, unit)) return NULL;
  return FormatText(UIActions::actionTypes[GetType()].textDefault, params.Data(), params.Size(), true);
}

RString Action::GetTextSimple(AIBrain *unit) const
{
  UIActionParams params;
  if (!GetParams(params, unit)) return RString();
  RefR<INode> text = FormatText(UIActions::actionTypes[GetType()].textSimple, params.Data(), params.Size(), true);
  return GetTextPlain(text);
}

#if _HELISIM_PRECISE_INTERACTION
RefR<INode> Action::GetTextToolTip(AIBrain *unit) const
{
  UIActionParams params;
  if (!GetParams(params, unit)) return NULL;
  return FormatText(UIActions::actionTypes[GetType()].textToolTip, params.Data(), params.Size(), true);
}
#endif

bool Action::GetParams(UIActionParams &params, AIBrain *unit) const
{
  if (!_target)
  {
    // regular option for commanding menu
    // RptF("Action %s - missing target", cc_cast(FindEnumName(GetType())));
    return false;
  }
  return _target->GetActionParams(params, this, unit);
}

bool Action::Process(AIBrain *unit) const
{
  if (_target)
  {
    _target->PerformAction(this, unit);

#if _HELISIM_PRECISE_INTERACTION
#if _HELISIM_ROTORLIB
    AIBrain *agent = GWorld->FocusOn();
    if(agent && agent->GetPerson())
    {
      //helisim action - we need to bu sure action index does not change by adding default action
      if(_type >= ATEnginesOnAuto && _type <= ATWheelsBrakeOff)  
      {
        agent->GetPerson()->OnEvent(EESelectedRotorLibActionPerformed, _target, (float)_type  - ATEnginesOnAuto, _userActionID);
        GWorld->OnMissionEvent(MESelectedRotorLibActionPerformed, agent->GetPerson(), _target, (float)_type  - ATEnginesOnAuto, _userActionID);
      }
      //default action
      else 
      {
        agent->GetPerson()->OnEvent(EESelectedActionPerformed, _target, (float)_type, _userActionID);
        GWorld->OnMissionEvent(MESelectedActionPerformed, agent->GetPerson(), _target, (float)_type, _userActionID);
      }
    }
#endif
#endif
    return true;
  }
  else
  {
    RptF("Action::Process - No target [action: %s]", cc_cast( FindEnumName(GetType()) ) );
    return false;
  }
}

#if _VBS3
bool Action::PerformScripted(AIBrain *unit) const
{
  UIActionTypeDesc desc = UIActions::actionTypes[_type];

  EntityAI *tgt = GetTarget();

  if(desc.script.GetLength() > 0)
  {
    GameVarSpace local(false);
    GameState *gstate = GWorld->GetGameState();
    int gshandle = gstate->BeginContext(&local);

    GameValue value = gstate->CreateGameValue(GameArray);
    GameArrayType &array = value;
    array.Add(GameValueExt(tgt));
    array.Add(GameValueExt(unit->GetPerson()));
    gstate->VarSetLocal("_this",value,true);
    gstate->Execute(desc.script, GameState::EvalContext::_default, GWorld->GetMissionNamespace());
    gstate->EndContext(gshandle);
    return true;
  }
  return false;
}
#endif

bool Action::SetParams(const GameState *state, GameValuePar oper)
{
  // no additional parameters are expected
  const GameArrayType &array = oper;
  Assert(array.Size() >= 3);
  state->SetError(EvalDim, array.Size(), 2);
  return false;
}

Action *Action::CreateObject(ParamArchive &ar)
{
  UIActionType type;
  if (ar.SerializeEnum("type", type, 1, ATNone) != LSOK) return NULL;
  return CreateAction(type);
}

LSError Action::Serialize(ParamArchive &ar)
{
  if (ar.IsSaving())
  {
    CHECK(ar.SerializeEnum("type", _type, 1, ATNone))
  }
  CHECK(ar.SerializeRef("target", _target, 1))
  CHECK(ar.Serialize("fromGUI", _fromGUI, 1, false))
  CHECK(ar.Serialize("processed", _processed, 1, false))

#if _HELISIM_PRECISE_INTERACTION
  CHECK(ar.Serialize("userActionID", _userActionID, 1, -1))
#endif
  return LSOK;
}

Ref<Action> ActionBasic::Copy() const
{
  Action *action = new ActionBasic(_type, _target);
  action->SetFromGUI(_fromGUI);
  return action;
}

bool ActionBasic::GetParams(UIActionParams &params, AIBrain *unit) const
{
  switch (GetType())
  {
  case ATGetInDriver:
  case ATGetInPilot:
  case ATHeal:
  case ATHealSoldier:
  case ATFirstAid:
  case ATRepairVehicle:
  case ATRepair:
  case ATRefuel:
  case ATTakeItem:
  case ATTakeItemIntoHands:
  case ATDropItem:
#if _VBS2 // interact with vehs
  case ATInteractWithVehicle:
  case ATQuickEnterVehicle:
#endif
    // argument %1 - action target
    if (!GetTarget())
    {
      // regular option for commanding menu
      // RptF("Action %s - missing target", cc_cast(FindEnumName(GetType())));
      return false;
    }
    params.Add(CreateTextASCII(NULL, GetTarget()->GetShortName()));
    return true;
#if _VBS3 && _ENABLE_CARRY_BODY
  case ATCaptureUnit:
  case ATReleaseUnit:
  case ATHandcuff:
  case ATReleaseHandcuff:
  case ATCarryBody:
    {
      // argument %1 - action target
      if (!GetTarget())
      {
        // regular option for commanding menu
        // RptF("Action %s - missing target", cc_cast(FindEnumName(GetType())));
        return false;
      }
      RString name = GetTarget()->GetType(1)->GetDisplayName();
      Person* person = dyn_cast<Person>(GetTarget());
      if(person) name = name + RString(" (") + person->GetPersonName() + RString(")");

      params.Add(CreateTextASCII(NULL, name));
      return true;
    }
#endif
  case ATRadioNextChannel:
  case ATRadioPrevChannel:
  case ATRadioOn:
  case ATRadioOff:
    return base::GetParams(params, unit);
  case ATGetOut:
  case ATLightOn:
  case ATLightOff:
  case ATSearchLightOn:
  case ATSearchLightOff:
  case ATCollisionLightOn:
  case ATCollisionLightOff:
  case ATEngineOn:
  case ATEngineOff:
  case ATEject:
  case ATTakeFlag:
  case ATCancelAction:
  case ATReturnFlag:
  case ATTurnIn:
  case ATTurnOut:
  case ATSitDown:
  case ATSalute:
  case ATTaunt:
  case ATClap:
  case ATPoint:
  case ATThumbsUp:
  case ATHideBody:
  case ATNVGoggles:
  case ATNVGogglesOff:
  case ATGear:
  case ATInspect:
  case ATGunLightOn:
  case ATGunLightOff:
  case ATIRLaserOn:
  case ATIRLaserOff:
  case ATTakeVehicleControl:
  case ATSuspendVehicleControl:
  case ATLockVehicleControl:
  case ATUnlockVehicleControl:
  case ATLandGear:
  case ATLandGearUp:
  case ATRadioVolumeUp:
  case ATRadioVolumeDown:
#if _HELISIM_ROTORLIB
  case ATEnginesOnAuto:
  case ATEnginesOffAuto:
  case ATHelicopterAutoTrimOn:
  case ATHelicopterAutoTrimOff:
  case ATHelicopterTrimOn:
  case ATHelicopterTrimOff:
  case ATBatteriesOn:
  case ATBatteriesOff:
  case ATAPUOn:
  case ATAPUOff:
  case ATRotorBrakeOn:
  case ATRotorBrakeOff:
  case ATStarterOn1:
  case ATStarterOff1:
  case ATStarterOn2:
  case ATStarterOff2:
  case ATStarterOn3:
  case ATStarterOff3:
  case ATThrottleOff1:
  case ATThrottleIdle1:
  case ATThrottleFull1:
  case ATThrottleOff2:
  case ATThrottleIdle2:
  case ATThrottleFull2:
  case ATThrottleOff3:
  case ATThrottleIdle3:
  case ATThrottleFull3:
  case ATWheelsBrakeOn:
  case ATWheelsBrakeOff:
#endif
#if _VBS3_WEAPON_ON_BACK
  case ATPutWeaponOnBack:
#endif
    // no parameters
    return true;
  }
  return base::GetParams(params, unit);
}

/// Functor checking if crew member is enemy to given unit
class IsCrewEnemyInside : public ICrewFunc
{
protected:
  AIBrain *_unit;

public:
  IsCrewEnemyInside(AIBrain *unit) : _unit(unit) {}
  virtual bool operator () (Person *entity)
  {
    return entity && _unit->IsEnemy(entity->GetTargetSide()); // true -> break enumeration and return true
  }
};

class IsCrewEmpty : public ICrewFunc
{
protected:
  int  *_count;

public:
  IsCrewEmpty(int &count) {_count = &count;}

  virtual bool operator () (Person *person)
  {
    if(person) return true; // continue
    return false;
  }
};

void MoveToGetInPos(AIBrain *unit, Transport &veh, const Vector3& shift, const GetInPoint &pt);

bool ActionBasic::Process(AIBrain *unit) const
{
  Object *veh = GetTarget();
  switch (GetType())
  {
  case ATGetInDriver:
  case ATGetInPilot:
    {
      if (!unit->IsFreeSoldier()) return false;
      Transport *trans = dyn_cast<Transport>(veh);
      if (!trans) return false;
      if (unit->IsPlayer() && trans->GetLock() == LSLocked) return false;
      
      // avoid get in enemy vehicle
      IsCrewEnemyInside func(unit);
      if (trans->ForEachCrew(func)) return false;
      
      RString getInAction;
      if (trans->Type()->PreciseGetInOut()==GETIN_OLD)
        getInAction = trans->Type()->GetDriverGetInAction();
      Vector3 shift = unit->GetPerson()->GetActionMoveShift(getInAction);
      GetInPoint pt = trans->GetDriverGetInPos(unit->GetPerson(), unit->GetPerson()->RenderVisualState().Position());
      MoveToGetInPos(unit, *trans, shift, pt);
      // Disable dammage see Man.Simulate
#define GET_IN_TIME 5.0f
      unit->GetPerson()->DisableDamageFromObj(GET_IN_TIME); 

      unit->GetPerson()->CreateActivityGetIn(trans, GetType());
    }
    return true;
  case ATTakeFlag:
    // only owner can process actions
    DoAssert(GetNetworkManager().IsServerOwner());
    if (veh && unit->IsFreeSoldier())
    {
      veh->LowerFlag(1);
      return true;
    }
    return false;
  case ATReturnFlag:
    // only owner can process actions
    DoAssert(GetNetworkManager().IsServerOwner());
    if (veh && unit->IsFreeSoldier())
    {
      veh->LowerFlag(0);
      return true;
    }
    return false;
  case ATNVGoggles:
    {
      Person *person = unit->GetPerson();
      person->SetNVWanted(true);
    }
    return true;
  case ATNVGogglesOff:
    {
      Person *person = unit->GetPerson();
      person->SetNVWanted(false);
    }
    return true;
  }

  return base::Process(unit);
}

DEFINE_FAST_ALLOCATOR(ActionBasic)

Ref<Action> ActionTurret::Copy() const
{
  Action *action = new ActionTurret(_type, _target, _turret);
  action->SetFromGUI(_fromGUI);
  return action;
}

bool ActionTurret::HasEqualType(const Action *to) const
{
  if (!base::HasEqualType(to)) return false;
  const ActionTurret *actionTyped = static_cast<const ActionTurret *>(to);
  return GetTurret() == actionTyped->GetTurret();
}

int ActionTurret::Compare(const Action *with) const
{
  int s = base::Compare(with);
  if (s != 0) return s;
  const ActionTurret *actionTyped = static_cast<const ActionTurret *>(with);
  s = CompareUIObjectAddress(GetTurret(), actionTyped->GetTurret());
  return s;
}

bool ActionTurret::GetParams(UIActionParams &params, AIBrain *unit) const
{
  switch (GetType())
  {
  case ATGetInTurret:
  case ATMoveToTurret:
    if (!GetTarget())
    {
      // regular option for commanding menu
      // RptF("Action %s - missing target", cc_cast(FindEnumName(GetType())));
      return false;
    }
    if (!GetTurret())
    {
      RptF("Action %s - missing turret", cc_cast(FindEnumName(GetType())));
      return false;
    }
    else
    {
      Transport *vehicle = dyn_cast<Transport>(GetTarget());
      TurretContext context; // need access to turret type
      if (!vehicle->FindTurret(GetTurret(), context))
      {
        RptF("Action %s - wrong turret", cc_cast(FindEnumName(GetType())));
        return false;
      }
      params.Add(CreateTextASCII(NULL, vehicle->GetShortName()));
      params.Add(CreateTextASCII(NULL, context._turretType->_gunnerName));
      return true;
    }
  }

  return base::GetParams(params, unit);
}

bool ActionTurret::Process(AIBrain *unit) const
{
  Object *veh = GetTarget();
  switch (GetType())
  {
  case ATGetInTurret:
    {
      if (!unit->IsFreeSoldier()) return false;
      Transport *trans = dyn_cast<Transport>(veh);
      if (!trans) return false;
      Turret *turret = GetTurret();
      if (!turret) return false;
      if (unit->IsPlayer() && trans->GetLock() == LSLocked) return false;

      // avoid get in enemy vehicle
      IsCrewEnemyInside func(unit);
      if (trans->ForEachCrew(func)) return false;

      TurretContext context;
      if (trans->FindTurret(unit->GetPerson(), context) && context._turretType)
      {
        Vector3 shift = unit->GetPerson()->GetActionMoveShift(context._turretType->GetGunnerGetInAction());
        GetInPoint pt = trans->GetTurretGetInPos(context._turret, unit->GetPerson(), unit->GetPerson()->RenderVisualState().Position());
        MoveToGetInPos(unit, *trans, shift, pt);
      }
      // Disable damage see Man.Simulate
#define GET_IN_TIME 5.0f
      unit->GetPerson()->DisableDamageFromObj(GET_IN_TIME); 

      unit->GetPerson()->CreateActivityGetIn(trans, GetType(), turret);
    }
    return true;
  }

  return base::Process(unit);
}

bool ActionTurret::SetParams(const GameState *state, GameValuePar oper)
{
  const GameArrayType &array = oper;
  int n = array.Size();
  if (n == 3)
  {
    // path to turret
    if (array[2].GetType() != GameArray)
    {
      state->TypeError(GameArray, array[2].GetType());
      return false;
    }
    const GameArrayType &subarray = array[2];
    int n = subarray.Size();
    for (int i=0; i<n; i++)
    {
      if (subarray[i].GetType() != GameScalar)
      {
        state->TypeError(GameScalar, subarray[i].GetType());
        return false;
      }
    }

    Transport *vehicle = dyn_cast<Transport>(GetTarget());
    if (!vehicle) return false;

    AutoArray<int, MemAllocLocal<int, 16> > path;
    path.Resize(n);
    for (int i=0; i<n; i++) path[i] = toInt((float)subarray[i]);
    _turret = vehicle->GetTurret(path.Data(), path.Size());
    return _turret != NULL;
  }

  state->SetError(EvalDim, n, 3);
  return false;
}

LSError ActionTurret::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))
  CHECK(ar.SerializeRef("Turret", _turret, 1))
  return LSOK;
}

static void SaveRString(QOStream &out, const RString &str)
{
  int len = str.GetLength();
  out.write(&len, sizeof(len));
  if (len > 0) out.write(str.Data(), len);
}

static RString LoadRString(QIStream &in)
{
  int len;
  in.read(&len, sizeof(len));
  RString result;
  if (len > 0)
  {
    char *buffer = result.CreateBuffer(len);
    in.read(buffer, len);
  }
  return result;
}

static void SaveNetworkObject(QOStream &out, const NetworkObject *obj)
{
  NetworkId nid = obj ? obj->GetNetworkId() : NetworkId::Null();

  int creator = nid.creator.CreatorInt();
  OIDInt id = nid.id;
  out.write(&creator, sizeof(creator));
  out.write(&id, sizeof(id));
}

static NetworkObject *LoadNetworkObject(QIStream &in)
{
  int creator;
  OIDInt id;
  in.read(&creator, sizeof(creator));
  in.read(&id, sizeof(id));
  NetworkId nid(CreatorId(creator), id);

  return nid.IsNull() ? nullptr : GetNetworkManager().GetObject(nid);
}

void ActionTurret::SaveParams(QOStream &out) const
{
  SaveNetworkObject(out, _turret);
}

void ActionTurret::LoadParams(QIStream &in)
{
  _turret = dyn_cast<Turret>(LoadNetworkObject(in));
}

DEFINE_FAST_ALLOCATOR(ActionTurret)

Ref<Action> ActionWeaponIndex::Copy() const
{
  Action *action = new ActionWeaponIndex(_type, _target, _gunner, _index);
  action->SetFromGUI(_fromGUI);
  return action;
}

bool ActionWeaponIndex::HasEqualType(const Action *to) const
{
  if (!base::HasEqualType(to)) return false;
  const ActionWeaponIndex *actionTyped = static_cast<const ActionWeaponIndex *>(to);
  return GetGunner() == actionTyped->GetGunner() && GetWeaponIndex() == actionTyped->GetWeaponIndex();
}

int ActionWeaponIndex::Compare(const Action *with) const
{
  int s = base::Compare(with);
  if (s != 0) return s;
  const ActionWeaponIndex *actionTyped = static_cast<const ActionWeaponIndex *>(with);
  s = CompareUIObjectAddress(GetGunner(), actionTyped->GetGunner());
  if (s != 0) return s;
  Weapon *w1 = GetWeaponIndex()._weapon;
  Weapon *w2 = actionTyped->GetWeaponIndex()._weapon;
  s = CompareUIObjectAddress(w1, w2);
  if (s != 0) return s;
  s = GetWeaponIndex()._muzzle - actionTyped->GetWeaponIndex()._muzzle;
  if (s != 0) return s;
  s = GetWeaponIndex()._mode - actionTyped->GetWeaponIndex()._mode;
  return s;
}

bool ActionWeaponIndex::GetParams(UIActionParams &params, AIBrain *unit) const
{
  return base::GetParams(params, unit);
}

bool ActionWeaponIndex::Process(AIBrain *unit) const
{
  return base::Process(unit);
}

bool ActionWeaponIndex::SetParams(const GameState *state, GameValuePar oper)
{
  const GameArrayType &array = oper;
  int n = array.Size();
  if (n == 6)
  {
    if (array[2].GetType() != GameObject)
    {
      state->TypeError(GameObject, array[2].GetType());
      return false;
    }
    Object *obj = static_cast<GameDataObject *>(array[2].GetData())->GetObject();
    _gunner = dyn_cast<Person>(obj);

    if (array[3].GetType() != GameObject)
    {
      state->TypeError(GameObject, array[3].GetType());
      return false;
    }
    obj = static_cast<GameDataObject *>(array[3].GetData())->GetObject();
    _index._weapon = dyn_cast<Weapon>(obj);
    
    if (array[4].GetType() != GameScalar)
    {
      state->TypeError(GameObject, array[4].GetType());
      return false;
    }
    _index._muzzle = toInt(safe_cast<float>(array[4]));

    if (array[5].GetType() != GameScalar)
    {
      state->TypeError(GameObject, array[5].GetType());
      return false;
    }
    _index._mode = toInt(safe_cast<float>(array[5]));

    return true;
  }

  state->SetError(EvalDim, n, 6);
  return false;
}

LSError ActionWeaponIndex::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))
  CHECK(ar.SerializeRef("gunner", _gunner, 1))
  CHECK(ar.SerializeDef("index", _index, 1))
  return LSOK;
}

void ActionWeaponIndex::SaveParams(QOStream &out) const
{
  SaveNetworkObject(out, _gunner);
  SaveNetworkObject(out, _index._weapon);
  out.write(&_index._muzzle, sizeof(_index._muzzle));
  out.write(&_index._mode, sizeof(_index._mode));
}

void ActionWeaponIndex::LoadParams(QIStream &in)
{
  _gunner = dyn_cast<Person>(LoadNetworkObject(in));
  _index._weapon = dyn_cast<Weapon>(LoadNetworkObject(in));
  in.read(&_index._muzzle, sizeof(_index._muzzle));
  in.read(&_index._mode, sizeof(_index._mode));
}

DEFINE_FAST_ALLOCATOR(ActionWeaponIndex)

Ref<Action> ActionIndex::Copy() const
{
#if _HELISIM_PRECISE_INTERACTION
  Action *action = new ActionIndex(_type, _target, _index, _userActionID);
#else
  Action *action = new ActionIndex(_type, _target, _index);
#endif
  action->SetFromGUI(_fromGUI);
  return action;
}

bool ActionIndex::HasEqualType(const Action *to) const
{
  if (!base::HasEqualType(to)) return false;
  const ActionIndex *actionTyped = static_cast<const ActionIndex *>(to);
  return GetIndex() == actionTyped->GetIndex();
}

int ActionIndex::Compare(const Action *with) const
{
  int s = base::Compare(with);
  if (s != 0) return s;
  const ActionIndex *actionTyped = static_cast<const ActionIndex *>(with);
  s = GetIndex() - actionTyped->GetIndex();
  return s;
}

bool ActionIndex::GetParams(UIActionParams &params, AIBrain *unit) const
{
  switch (GetType())
  {
  case ATGetInCargo:
    // argument %1 - action target
    if (!GetTarget())
    {
      // regular option for commanding menu
      // RptF("Action %s - missing target", cc_cast(FindEnumName(GetType())));
      return false;
    }
    params.Add(CreateTextASCII(NULL, GetTarget()->GetShortName()));
    return true;
  }

  return base::GetParams(params, unit);
}

bool ActionIndex::Process(AIBrain *unit) const
{
  switch (GetType())
  {
  case ATGetInCargo:
    {
      if (!unit->IsFreeSoldier()) return false;
      Transport *trans = dyn_cast<Transport>(GetTarget());
      if (!trans) return false;
      if (unit->IsPlayer() && trans->GetLock() == LSLocked) return false;

      // avoid get in enemy vehicle
      IsCrewEnemyInside func(unit);
      if (trans->ForEachCrew(func)) return false;

      Vector3 shift = unit->GetPerson()->GetActionMoveShift(trans->Type()->GetCargoGetInAction(_index));
      GetInPoint pt = trans->GetCargoGetInPos(_index, unit->GetPerson(), unit->GetPerson()->RenderVisualState().Position());
      MoveToGetInPos(unit, *trans, shift, pt);
      // Disable dammage see Man.Simulate
#define GET_IN_TIME 5.0f
      unit->GetPerson()->DisableDamageFromObj(GET_IN_TIME); 

      unit->GetPerson()->CreateActivityGetIn(trans, GetType(), NULL, _index);
    }
    return true;
  }
  return base::Process(unit);
}

bool ActionIndex::SetParams(const GameState *state, GameValuePar oper)
{
  const GameArrayType &array = oper;
  int n = array.Size();
  if (n == 3)
  {
    if (array[2].GetType() != GameScalar)
    {
      state->TypeError(GameScalar, array[2].GetType());
      return false;
    }
    _index = toInt(safe_cast<float>(array[2]));
    return true;
  }

  state->SetError(EvalDim, n, 3);
  return false;
}

LSError ActionIndex::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))
  CHECK(ar.Serialize("index", _index, 1, -1))
  return LSOK;
}

void ActionIndex::SaveParams(QOStream &out) const
{
  out.write(&_index, sizeof(_index));
}

void ActionIndex::LoadParams(QIStream &in)
{
  in.read(&_index, sizeof(_index));
}

DEFINE_FAST_ALLOCATOR(ActionIndex)

 Ref<Action> ActionSurface::Copy() const
{
  Action *action = new ActionSurface(_type, _surface, _index, _pos);
  action->SetFromGUI(_fromGUI);
  return action;
}

bool ActionSurface::HasEqualType(const Action *to) const
{
  if (!base::HasEqualType(to)) return false;
  const ActionSurface *actionTyped = static_cast<const ActionSurface *>(to);
  return GetSurface() == actionTyped->GetSurface() && GetIndex() == actionTyped->GetIndex();
}

int ActionSurface::Compare(const Action *with) const
{
  int s = base::Compare(with);
  if (s != 0) return s;
  const ActionSurface *actionTyped = static_cast<const ActionSurface *>(with);
  s = strcmp(GetSurface(), actionTyped->GetSurface());
  if (s != 0) return s;
  s = GetIndex() - actionTyped->GetIndex();
  return s;
}

bool ActionSurface::GetParams(UIActionParams &params, AIBrain *unit) const
{
  if (GetType() == ATSurfaceAction)
  {
    ObjectActionsMapItem *item = SurfaceActions.get(_surface);
    if (item)
    {
      if (_index < (*item)->Size())
      {
        ObjectActionsMapAction &act = (**item)[_index];
        params.Add(CreateTextStructured(NULL, act._displayName));
        params.Add(CreateTextStructured(NULL, act._displayName));
        return true;
      }
    }
    return false;
  }

  return base::GetParams(params, unit);
}

bool ActionSurface::Process(AIBrain *unit) const
{
  if (GetType() == ATSurfaceAction)
  {
    ObjectActionsMapItem *item = SurfaceActions.get(_surface);
    if (item)
    {
      if (_index < (*item)->Size())
      {
        ObjectActionsMapAction &act = (**item)[_index];
        Person *person = unit->GetPerson();

        GameState *state = GWorld->GetGameState();
        GameVarSpace local(state->GetContext(), false);
        int gshandle = state->BeginContext(&local);

        state->VarSetLocal("this", _surface, true, true);
        state->VarSetLocal("_name", act._name, true, true);
        state->VarSetLocal("_owner", GameValueExt(person), true, true);
        InventoryItem *inHands = person ? person->GetItemInHands() : NULL;
        state->VarSetLocal("_inHands", GameValueExt(inHands), true, true);

        GameValue value = state->CreateGameValue(GameArray);
        GameArrayType &array = value;
        array.Add(_pos[0]);
        array.Add(_pos[2]);
        state->VarSetLocal("_pos", value, true, true);

        state->Execute(act._statement, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
        state->EndContext(gshandle);
        return true;
      }
    }
    return false;
  }
  return base::Process(unit);
}

bool ActionSurface::SetParams(const GameState *state, GameValuePar oper)
{
  const GameArrayType &array = oper;
  int n = array.Size();
  if (n == 3)
  {
    if (array[2].GetType() != GameScalar)
    {
      state->TypeError(GameScalar, array[2].GetType());
      return false;
    }
    _index = toInt(safe_cast<float>(array[2]));
    return true;
  }

  state->SetError(EvalDim, n, 3);
  return false;
}

LSError ActionSurface::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))
  CHECK(ar.Serialize("surface", _surface, 1, RString()))
  CHECK(ar.Serialize("index", _index, 1, -1))
  CHECK(::Serialize(ar, "pos", _pos, 1, VZero))
  return LSOK;
}

void ActionSurface::SaveParams(QOStream &out) const
{
  SaveRString(out, _surface);
  out.write(&_index, sizeof(_index));
  Coord x = _pos[0], y = _pos[1], z = _pos[2];
  out.write(&x, sizeof(x));
  out.write(&y, sizeof(y));
  out.write(&z, sizeof(z));
}

void ActionSurface::LoadParams(QIStream &in)
{
  _surface = LoadRString(in);
  in.read(&_index, sizeof(_index));
  Coord x, y, z;
  in.read(&x, sizeof(x));
  in.read(&y, sizeof(y));
  in.read(&z, sizeof(z));
  _pos = Vector3(x, y, z);
}

DEFINE_FAST_ALLOCATOR(ActionSurface)

Ref<Action> ActionCrafting::Copy() const
{
  Action *action = new ActionCrafting(_type, _target, _item, _recipe);
  action->SetFromGUI(_fromGUI);
  return action;
}

bool ActionCrafting::HasEqualType(const Action *to) const
{
  if (!base::HasEqualType(to)) return false;
  const ActionCrafting *actionTyped = static_cast<const ActionCrafting *>(to);
  return GetItem() == actionTyped->GetItem() && GetRecipe() == actionTyped->GetRecipe();
}

int ActionCrafting::Compare(const Action *with) const
{
  int s = base::Compare(with);
  if (s != 0) return s;
  const ActionCrafting *actionTyped = static_cast<const ActionCrafting *>(with);
  s = CompareUIObjectAddress(GetItem(), actionTyped->GetItem());
  if (s != 0) return s;
  s = strcmp(GetRecipe(), actionTyped->GetRecipe());
  return s;
}

bool ActionCrafting::GetParams(UIActionParams &params, AIBrain *unit) const
{
  return base::GetParams(params, unit);
}

bool ActionCrafting::Process(AIBrain *unit) const
{
  return base::Process(unit);
}

bool ActionCrafting::SetParams(const GameState *state, GameValuePar oper)
{
  const GameArrayType &array = oper;
  int n = array.Size();
  if (n == 4)
  {
    if (array[2].GetType() != GameObject)
    {
      state->TypeError(GameObject, array[2].GetType());
      return false;
    }
    if (array[3].GetType() != GameString)
    {
      state->TypeError(GameString, array[3].GetType());
      return false;
    }
    Object *obj = obj = static_cast<GameDataObject *>(array[2].GetData())->GetObject();;
    _item = dyn_cast<InventoryItem>(obj);
    _recipe = array[2];
    return true;
  }

  state->SetError(EvalDim, n, 4);
  return false;
}

LSError ActionCrafting::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))
  CHECK(ar.SerializeRef("item", _item, 1))
  CHECK(ar.Serialize("recipe", _recipe, 1, RString()))
  return LSOK;
}

void ActionCrafting::SaveParams(QOStream &out) const
{
  SaveNetworkObject(out, _item);
  SaveRString(out, _recipe);
}

void ActionCrafting::LoadParams(QIStream &in)
{
  _item = dyn_cast<InventoryItem>(LoadNetworkObject(in));
  _recipe = LoadRString(in);
}

DEFINE_FAST_ALLOCATOR(ActionCrafting)

Ref<Action> ActionMagazine::Copy() const
{
  Action *action = new ActionMagazine(_type, _target, _gunner, _magazine);
  action->SetFromGUI(_fromGUI);
  return action;
}

bool ActionMagazine::HasEqualType(const Action *to) const
{
  if (!base::HasEqualType(to)) return false;
  const ActionMagazine *actionTyped = static_cast<const ActionMagazine *>(to);
  return GetGunner() == actionTyped->GetGunner() && GetMagazine() == actionTyped->GetMagazine();
}

int ActionMagazine::Compare(const Action *with) const
{
  int s = base::Compare(with);
  if (s != 0) return s;
  const ActionMagazine *actionTyped = static_cast<const ActionMagazine *>(with);
  s = CompareUIObjectAddress(GetGunner(), actionTyped->GetGunner());
  if (s != 0) return s;
  s = CompareUIObjectAddress(GetMagazine(), actionTyped->GetMagazine());
  return s;
}

bool ActionMagazine::GetParams(UIActionParams &params, AIBrain *unit) const
{
  return base::GetParams(params, unit);
}

bool ActionMagazine::Process(AIBrain *unit) const
{
  return base::Process(unit);
}

bool ActionMagazine::SetParams(const GameState *state, GameValuePar oper)
{
  const GameArrayType &array = oper;
  int n = array.Size();
  if (n == 4)
  {
    if (array[2].GetType() != GameObject)
    {
      state->TypeError(GameObject, array[2].GetType());
      return false;
    }
    Object *obj = static_cast<GameDataObject *>(array[2].GetData())->GetObject();
    _gunner = dyn_cast<Person>(obj);
    
    if (array[3].GetType() != GameObject)
    {
      state->TypeError(GameObject, array[3].GetType());
      return false;
    }
    obj = static_cast<GameDataObject *>(array[3].GetData())->GetObject();
    _magazine = dyn_cast<Magazine>(obj);
    return true;
  }

  state->SetError(EvalDim, n, 4);
  return false;
}

LSError ActionMagazine::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))
  CHECK(ar.SerializeRef("gunner", _gunner, 1))
  CHECK(ar.SerializeRef("magazine", _magazine, 1))
  return LSOK;
}

void ActionMagazine::SaveParams(QOStream &out) const
{
  SaveNetworkObject(out, _gunner);
  SaveNetworkObject(out, _magazine);
}

void ActionMagazine::LoadParams(QIStream &in)
{
  _gunner = dyn_cast<Person>(LoadNetworkObject(in));
  _magazine = dyn_cast<Magazine>(LoadNetworkObject(in));
}

DEFINE_FAST_ALLOCATOR(ActionMagazine)

static MuzzleType *FindMuzzle(const WeaponType *weapon, RString muzzle)
{
  if (weapon)
  {
    for (int i=0; i<weapon->_muzzles.Size(); i++)
    {
      if (stricmp(weapon->_muzzles[i]->GetName(), muzzle) == 0)
        return weapon->_muzzles[i];
    }
  }
  return NULL;
}

Ref<Action> ActionMagazineAndMuzzle::Copy() const
{
  Action *action = new ActionMagazineAndMuzzle(_type, _target, _gunner, _magazine, _weapon, _muzzle);
  action->SetFromGUI(_fromGUI);
  return action;
}

bool ActionMagazineAndMuzzle::HasEqualType(const Action *to) const
{
  if (!base::HasEqualType(to)) return false;
  const ActionMagazineAndMuzzle *actionTyped = static_cast<const ActionMagazineAndMuzzle *>(to);
  return GetWeapon() == actionTyped->GetWeapon() && GetMuzzle() == actionTyped->GetMuzzle();
}

int ActionMagazineAndMuzzle::Compare(const Action *with) const
{
  int s = base::Compare(with);
  if (s != 0) return s;
  const ActionMagazineAndMuzzle *actionTyped = static_cast<const ActionMagazineAndMuzzle *>(with);
  if (GetWeapon()->GetName() != actionTyped->GetWeapon()->GetName())
    return stricmp(GetWeapon()->GetName(), actionTyped->GetWeapon()->GetName());
  if (GetMuzzle()->GetName() != actionTyped->GetMuzzle()->GetName())
    return stricmp(GetMuzzle()->GetName(), actionTyped->GetMuzzle()->GetName());
  return 0;
}

bool ActionMagazineAndMuzzle::GetParams(UIActionParams &params, AIBrain *unit) const
{
  return base::GetParams(params, unit);
}

bool ActionMagazineAndMuzzle::Process(AIBrain *unit) const
{
  return base::Process(unit);
}

bool ActionMagazineAndMuzzle::SetParams(const GameState *state, GameValuePar oper)
{
  const GameArrayType &array = oper;
  int n = array.Size();
  if (n == 6)
  {
    if (array[2].GetType() != GameObject)
    {
      state->TypeError(GameObject, array[2].GetType());
      return false;
    }
    Object *obj = static_cast<GameDataObject *>(array[2].GetData())->GetObject();
    _gunner = dyn_cast<Person>(obj);
    
    if (array[3].GetType() != GameObject)
    {
      state->TypeError(GameObject, array[3].GetType());
      return false;
    }
    obj = static_cast<GameDataObject *>(array[3].GetData())->GetObject();
    _magazine = dyn_cast<Magazine>(obj);
    
    if (array[4].GetType() != GameString)
    {
      state->TypeError(GameString, array[5].GetType());
      return false;
    }
    RString weapon = array[4];
    _weapon = weapon.GetLength() > 0 ? static_cast<const WeaponType *>(VehicleTypes.New(weapon)) : NULL;

    if (array[5].GetType() != GameString)
    {
      state->TypeError(GameString, array[6].GetType());
      return false;
    }
    RString muzzle = array[5];
    _muzzle = FindMuzzle(_weapon, muzzle);

    return true;
  }

  state->SetError(EvalDim, n, 6);
  return false;
}

LSError ActionMagazineAndMuzzle::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))
  if (ar.IsSaving())
  {
    RString weapon = _weapon ? cc_cast(_weapon->GetName()) : "";
    RString muzzle = _muzzle ? cc_cast(_muzzle->GetName()) : "";
    CHECK(ar.Serialize("weapon", weapon, 1, RString()))
    CHECK(ar.Serialize("muzzle", muzzle, 1, RString()))
  }
  else if (ar.GetPass() == ParamArchive::PassFirst)
  {
    RString weapon;
    RString muzzle;
    CHECK(ar.Serialize("weapon", weapon, 1, RString()))
    CHECK(ar.Serialize("muzzle", muzzle, 1, RString()))
    _weapon = weapon.GetLength() > 0 ? dynamic_cast<WeaponType *>(VehicleTypes.New(weapon)) : NULL;
    _muzzle = FindMuzzle(_weapon, muzzle);
  }
  return LSOK;
}

void ActionMagazineAndMuzzle::SaveParams(QOStream &out) const
{
  base::SaveParams(out);

  RString weapon = _weapon ? cc_cast(_weapon->GetName()) : "";
  RString muzzle = _muzzle ? cc_cast(_muzzle->GetName()) : "";
  SaveRString(out, weapon);
  SaveRString(out, muzzle);
}

void ActionMagazineAndMuzzle::LoadParams(QIStream &in)
{
  base::LoadParams(in);

  RString weapon = LoadRString(in);
  RString muzzle = LoadRString(in);
  _weapon = weapon.GetLength() > 0 ? dynamic_cast<WeaponType *>(VehicleTypes.New(weapon)) : NULL;
  _muzzle = FindMuzzle(_weapon, muzzle);
}

DEFINE_FAST_ALLOCATOR(ActionMagazineAndMuzzle)

Ref<Action> ActionLadder::Copy() const
{
  Action *action = new ActionLadder(_type, _target, _index);
  action->SetFromGUI(_fromGUI);
  return action;
}

bool ActionLadder::HasEqualType(const Action *to) const
{
  if (!base::HasEqualType(to)) return false;
  const ActionLadder *actionTyped = static_cast<const ActionLadder *>(to);
  return GetLadder() == actionTyped->GetLadder();
}

int ActionLadder::Compare(const Action *with) const
{
  int s = base::Compare(with);
  if (s != 0) return s;
  const ActionLadder *actionTyped = static_cast<const ActionLadder *>(with);
  s = GetLadder() - actionTyped->GetLadder();
  return s;
}

bool ActionLadder::GetParams(UIActionParams &params, AIBrain *unit) const
{
  return base::GetParams(params, unit);
}

bool ActionLadder::Process(AIBrain *unit) const
{
  return base::Process(unit);
}

bool ActionLadder::SetParams(const GameState *state, GameValuePar oper)
{
  const GameArrayType &array = oper;
  int n = array.Size();
  if (n == 3)
  {
    if (array[2].GetType() != GameScalar)
    {
      state->TypeError(GameScalar, array[2].GetType());
      return false;
    }
    _index = toInt(safe_cast<float>(array[2]));
    return true;
  }

  state->SetError(EvalDim, n, 3);
  return false;
}

LSError ActionLadder::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))
  CHECK(ar.Serialize("index", _index, 1, -1))
  return LSOK;
}

void ActionLadder::SaveParams(QOStream &out) const
{
  out.write(&_index, sizeof(_index));
}

void ActionLadder::LoadParams(QIStream &in)
{
  in.read(&_index, sizeof(_index));
}

DEFINE_FAST_ALLOCATOR(ActionLadder)

Ref<Action> ActionLadderPos::Copy() const
{
  Action *action = new ActionLadderPos(_type, _target, _index, _pos);
  action->SetFromGUI(_fromGUI);
  return action;
}

bool ActionLadderPos::HasEqualType(const Action *to) const
{
  if (!base::HasEqualType(to)) return false;
  const ActionLadderPos *actionTyped = static_cast<const ActionLadderPos *>(to);
  return GetLadderPos() == actionTyped->GetLadderPos();
}

int ActionLadderPos::Compare(const Action *with) const
{
  int s = base::Compare(with);
  if (s != 0) return s;
  const ActionLadderPos *actionTyped = static_cast<const ActionLadderPos *>(with);
  s = GetLadderPos() - actionTyped->GetLadderPos();
  return s;
}

bool ActionLadderPos::GetParams(UIActionParams &params, AIBrain *unit) const
{
  return base::GetParams(params, unit);
}

bool ActionLadderPos::Process(AIBrain *unit) const
{
  return base::Process(unit);
}

bool ActionLadderPos::SetParams(const GameState *state, GameValuePar oper)
{
  const GameArrayType &array = oper;
  int n = array.Size();
  if (n == 4)
  {
    if (array[2].GetType() != GameScalar)
    {
      state->TypeError(GameScalar, array[2].GetType());
      return false;
    }
    if (array[3].GetType() != GameScalar)
    {
      state->TypeError(GameScalar, array[3].GetType());
      return false;
    }
    _index = toInt(safe_cast<float>(array[2]));
    _pos = toInt(safe_cast<float>(array[3]));
    return true;
  }

  state->SetError(EvalDim, n, 4);
  return false;
}

LSError ActionLadderPos::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))
  CHECK(ar.Serialize("pos", _pos, 1, -1))
  return LSOK;
}

void ActionLadderPos::SaveParams(QOStream &out) const
{
  base::SaveParams(out);

  out.write(&_pos, sizeof(_pos));
}

void ActionLadderPos::LoadParams(QIStream &in)
{
  base::LoadParams(in);

  in.read(&_pos, sizeof(_pos));
}

DEFINE_FAST_ALLOCATOR(ActionLadderPos)

#if _ENABLE_CONVERSATION

Ref<Action> ActionTell::Copy() const
{
  Action *action = new ActionTell(_type, _target, _text, _message);
  action->SetFromGUI(_fromGUI);
  return action;
}

bool ActionTell::HasEqualType(const Action *to) const
{
  if (!base::HasEqualType(to)) return false;
  const ActionTell *actionTyped = static_cast<const ActionTell *>(to);
  return strcmp(GetText(), actionTyped->GetText()) == 0;
}

int ActionTell::Compare(const Action *with) const
{
  int s = base::Compare(with);
  if (s != 0) return s;
  const ActionTell *actionTyped = static_cast<const ActionTell *>(with);
  return strcmp(GetText(), actionTyped->GetText());
}

bool ActionTell::GetParams(UIActionParams &params, AIBrain *unit) const
{
  return base::GetParams(params, unit);
}

bool ActionTell::Process(AIBrain *unit) const
{
  return base::Process(unit);
}

bool ActionTell::SetParams(const GameState *state, GameValuePar oper)
{
  const GameArrayType &array = oper;
  int n = array.Size();
  if (n == 6)
  {
    // _text
    if (array[2].GetType() == GameString)
    {
      _text = array[2];
    }
    else if (array[2].GetType() == GameArray)
    {
      const GameArrayType &textDesc = array[2];
      if (textDesc.Size() == 0)
      {
        state->SetError(EvalDim, textDesc.Size(), 2);
        return false;
      }
      if (textDesc[0].GetType() != GameString)
      {
        state->TypeError(GameString, textDesc[0].GetType());
        return false;
      }
      _text = textDesc[0];
    }
    else
    {
      state->TypeError(GameString | GameArray, array[2].GetType());
      return false;
    }
    // _message
    if (array[3].GetType() != GameString)
    {
      state->TypeError(GameString, array[3].GetType());
      return false;
    }
    if (array[4].GetType() != GameString)
    {
      state->TypeError(GameString, array[4].GetType());
      return false;
    }
    if (array[5].GetType() != GameArray)
    {
      state->TypeError(GameArray, array[5].GetType());
      return false;
    }
    // TODO: access to askingUnit
    /*
    _message = askingUnit->CreateKBMessage(array[3], array[4]);
    // attributes
    const GameArrayType &attrArray = array[5];
    for (int i=0; i<attrArray.Size(); i++)
    {
      // TODO: check the format of arguments
      askingUnit->SetKBMessageArgument(_message, attrArray[i]);
    }
    return true;
    */
    return false;
  }

  state->SetError(EvalDim, n, 6);
  return false;
}

LSError ActionTell::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))
  CHECK(ar.Serialize("text", _text, 1, RString()))
  CHECK(ar.Serialize("message", _message, 1))
  return LSOK;
}

void ActionTell::SaveParams(QOStream &out) const
{
  SaveRString(out, _text);
  Fail("Serialization of message not implemented");
}

void ActionTell::LoadParams(QIStream &in)
{
  _text = LoadRString(in);
  Fail("Serialization of message not implemented");
}

DEFINE_FAST_ALLOCATOR(ActionTell)

#endif

UIActionTypeDesc::UIActionTypeDesc()
{
  priority = 0;
  show = true;
  showWindow = false;
  hideOnUse = true;
  shortcut = (UserAction)-1;
#if _HELISIM_PRECISE_INTERACTION
  userActionID = -1;
#endif
}

void UIActionTypeDesc::Load(ParamEntryPar cls)
{
  priority = cls >> "priority";
  show = cls >> "show";
  showWindow = cls >> "showWindow";
  hideOnUse = cls >> "hideOnUse";
  shortcut = (UserAction)-1;
  RString name = cls >> "shortcut";
  if (name.GetLength() > 0) 
  {
    for (int i=0; i<UAN; i++)
    {
      if (stricmp(name, GInput.userActionDesc[i].name) == 0)
      {
        shortcut = (UserAction)i;
        break;
      }
    }
  }
  text = cls >> "text";
  textDefault = cls >> "textDefault";
  if (textDefault.GetLength() == 0) textDefault = text;
  textSimple = cls >> "textSimple";
  if (textSimple.GetLength() == 0) textSimple = text;

#if _HELISIM_PRECISE_INTERACTION
  //we need array of points because of copilot 
  if(cls.FindEntry("modelPositions"))
  {
    ParamEntryVal entry = cls >> "modelPositions";
    if (entry.IsArray())
    {
      for(int i =0; i < entry.GetSize(); i++)
      {
        modelPositions.Add(entry[i]);
      }
    }
    else modelPositions.Add(entry.GetValue());

    modelPositions.Compact();
    showIn3D = cls >> "showIn3D";
  }
  if(cls.FindEntry("radius")) radius = cls >> "radius";
  if(cls.FindEntry("radiusView")) radiusView = cls >> "radiusView";

  if(cls.FindEntry("available")) available = cls >> "available";
  if(cls.FindEntry("textToolTip")) textToolTip = cls >> "textToolTip";
#endif

#if _VBS3 //support for scripted actions
  if(cls.FindEntry("statement"))
    script = cls >> "statement";
#endif
}

UIActionTypeDesc UIActions::actionTypes[NUIActionType];

void UIActions::Load(ParamEntryPar cls)
{
  for (int i=0; i<NUIActionType; i++)
  {
    RString name = FindEnumName((UIActionType)i);
    actionTypes[i].Load(cls >> name);
  }
}

UIAction::UIAction()
{
  priority = 0;
  show = true;
  showWindow = false;
  hideOnUse = true;
  highlight = false;
  shortcut = (UserAction)-1;
  created = Glob.uiTime;

#if _HELISIM_PRECISE_INTERACTION
  radius = -1;
  radiusView = -1;
  showIn3D = 0;
  available = 0;
  userActionID = -1;
#endif
}

UIAction::UIAction(Action *act)
{
  action = act;
  highlight = false;
  created = Glob.uiTime;

  // read defaults from type description
  UIActionType type = act->GetType();
  const UIActionTypeDesc &desc = UIActions::actionTypes[type];
  
  priority = desc.priority;
  show = desc.show;
  showWindow = desc.showWindow;
  hideOnUse = desc.hideOnUse;
  shortcut = desc.shortcut;

#if _HELISIM_PRECISE_INTERACTION
  modelPositions = desc.modelPositions;
  radius = desc.radius;
  radiusView = desc.radiusView;
  showIn3D = desc.showIn3D;
  available = desc.available;
  textToolTip = desc.textToolTip;
  userActionID  = desc.userActionID;
#endif
}

/// check if we want to process actions for given unit
static bool CanProcessActions(AIBrain *unit)
{
  // check the unit existence and life state
  if (!unit) return false;
  if (!unit->LSCanProcessActions()) return false;

  // check if person is not attached
  Person *person = unit->GetPerson();
  if (!person) return false;
  if (person->IsAttached()) return false;

  return true;
}

/*!
\patch 1.21 Date 08/20/2001 by Jirka
- Fixed: better processing of conflict in "Take Flag" and "Return Flag" actions
\patch 1.78 Date 7/22/2002 by Jirka
- Fixed: Behaviour of AI in Capture the flag or Flag Fight missions
(sometimes flag was placed on wrong unit)
*/

void UIAction::Process(AIBrain *unit) const
{
  if (!CanProcessActions(unit)) return;

  if (action) action->Process(unit);
}

#define UPDATE_UI_ACTION_MSG(MessageName, XX) \
  XX(MessageName, AutoArray<int>, memoryPoints, NDTIntArray, AutoArray<int>, NCTSmallUnsigned, DEFVALUEINTARRAY, DOC_MSG("Memory point indices"), TRANSF) \
  XX(MessageName, float, priority, NDTFloat, float, NCTNone, DEFVALUE(float, 0.0f), DOC_MSG("Action priority"), TRANSF) \
  XX(MessageName, bool, show, NDTBool, bool, NCTNone, DEFVALUE(bool, true), DOC_MSG("Action shown in menu"), TRANSF) \
  XX(MessageName, bool, showWindow, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Action enforces menu"), TRANSF) \
  XX(MessageName, bool, hideOnUse, NDTBool, bool, NCTNone, DEFVALUE(bool, true), DOC_MSG("Action hides when used"), TRANSF) \
  XX(MessageName, bool, highlight, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Action blinking when available"), TRANSF) \
  XX(MessageName, int, shortcut, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("Shortcut input action"), TRANSF) \
  XX(MessageName, int, actionType, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("Action type"), TRANSF) \
  XX(MessageName, OLink(Object), actionTarget, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Action target"), TRANSF_REF) \
  XX(MessageName, AutoArray<char>, actionParams, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Action parameters based on type"), TRANSF) \


DECLARE_NET_INDICES(UpdateUIAction, UPDATE_UI_ACTION_MSG)
DEFINE_NET_INDICES(UpdateUIAction, UPDATE_UI_ACTION_MSG)

bool UIAction::operator == (const UIAction &with) const
{
  int n = _memoryPoints.Size();
  if (with._memoryPoints.Size() != n) return false;
  for (int i=0; i<n; i++)
  {
    if (_memoryPoints[i] != with._memoryPoints[i]) return false;
  }

  if (priority != with.priority) return false;

  if (action)
  {
    if (!action->IsEqual(with.action)) return false;
  }
  else if (with.action) return false;

  return true;
}

NetworkMessageType UIAction::GetNMType(NetworkMessageClass cls) const
{
  return NMTUpdateUIAction;
}

NetworkMessageFormat &UIAction::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat &format)
{
  switch (cls)
  {
  case NMCUpdateGeneric:
    UPDATE_UI_ACTION_MSG(UpdateUIAction, MSG_FORMAT)
    break;
  default:
    break;
  }
  return format;
}

TMError UIAction::TransferMsg(NetworkMessageContext &ctx)
{
  switch (ctx.GetClass())
  {
  case NMCUpdateGeneric:
    {
      PREPARE_TRANSFER(UpdateUIAction)
      TRANSF(memoryPoints)
      TRANSF_EX(priority, priority)
      TRANSF_EX(show, show)
      TRANSF_EX(showWindow, showWindow)
      TRANSF_EX(hideOnUse, hideOnUse)
      TRANSF_EX(highlight, highlight)
      TRANSF_EX(shortcut, (int &)shortcut)
      if (ctx.IsSending())
      {
        if (action)
        {
          message->_actionType = action->GetType();
          message->_actionTarget = action->GetTarget() ? action->GetTarget()->GetNetworkId() : NetworkId::Null();

          QOStrStream out;
          action->SaveParams(out);
          int size = out.pcount();
          message->_actionParams.Resize(size);
          memcpy(message->_actionParams.Data(), out.str(), size);
        }
        else
        {
          message->_actionType = -1;
          message->_actionTarget = NetworkId::Null();
        }
      }
      else
      {
        created = Glob.uiTime;
        if (message->_actionType < 0) action = nullptr;
        else action = CreateAction((UIActionType)message->_actionType);
        if (action)
        {
          Object *obj = NULL;
          TRANSF_PTR_REF_EX(actionTarget, obj)
          action->SetTarget(obj);

          QIStrStream in(message->_actionParams.Data(), message->_actionParams.Size());
          action->LoadParams(in);
        }
      }
    }
    break;
  default:
    break;
  }
  return TMOK;
}


static const float dimTime = 10.0f;
static const float fadeTime = dimTime + 2.0f;
static const float protectionTime = 0.5f;
static const float fadeCoef = 1.0f / (fadeTime - dimTime);

UIActionsDrawable::UIActionsDrawable()
{
  // action menu parameters
  ParamEntryVal cls = Pars >> "CfgInGameUI" >> "Actions";
  _font = GEngine->LoadFont(GetFontID(cls >> "font"));
  _size = cls >> "size";

  // actions menu position
  ConstParamEntryPtr entry = cls.FindEntry("align");
  if (entry)
  {
    _align = *entry;
    _x = cls >> "x";
    _y = cls >> "y";
  }
  else
  {
    _align = ST_RIGHT | ST_DOWN;
    _x = cls >> "right";
    _y = cls >> "bottom";
  }
  
  _rows = cls >> "rows";

  _bgColor = GetPackedColor(cls >> "colorBackground");
  _selBgColor = GetPackedColor(cls >> "colorBackgroundSelected");
  _textColor = GetPackedColor(cls >> "colorText");
  _selColor = GetPackedColor(cls >> "colorSelect");

  _underlineSelected = cls >> "underlineSelected";

  _background = GlobLoadTextureUI(GetPictureName(cls >> "background"));

  _shadow = cls >> "shadow";

  _arrowWidth = cls >> "arrowWidth";
  _arrowHeight = cls >> "arrowHeight";
  _iconArrowUp = GlobLoadTextureUI(GetPictureName(cls >> "iconArrowUp"));
  _iconArrowDown = GlobLoadTextureUI(GetPictureName(cls >> "iconArrowDown"));

  // default action parameters
  cls = Pars >> "CfgInGameUI" >> "DefaultAction";
  _showHint = cls >> "showHint";
  _showNext = cls >> "showNext";
  _showLine = cls >> "showLine";
  _defaultFont = GEngine->LoadFont(GetFontID(cls >> "font"));
  _defaultSize = cls >> "size";
  _defaultColor = GetPackedColor(cls >> "colorText");
  _nextFont = GEngine->LoadFont(GetFontID(cls >> "fontNext"));
  _nextSize = cls >> "sizeNext";
  _xOffset = cls >> "offsetX";
  _yOffset = cls >> "offsetY";
  _xHotspot = cls >> "hotspotX";
  _yHotspot = cls >> "hotspotY";
  _relativeToCursor = cls >> "relativeToCursor";
  _defaultBackground = GlobLoadTextureUI(GetPictureName(cls >> "background"));
  _defaultShadow = cls >> "shadow";

#if _HELISIM_PRECISE_INTERACTION
  _textTipFade = cls >> "textTipFade";
  _textTipDelay = cls >> "textTipDelay";
#endif

  // others
  _default = false;
  _selectedPriority = -FLT_MAX;
  Hide();
}

void UIActionsDrawable::Refresh(bool user)
{
  _default = !user;

  if (user || Glob.uiTime < _dimStart + dimTime)
    _dimStart = Glob.uiTime - protectionTime; // avoid protection
  else
    _dimStart = Glob.uiTime;                    // set protection
}

bool UIActionsDrawable::IsListVisible() const
{
  if (_default) return false;

  float age = GetAge();
  return age >= protectionTime && age <= dimTime;
}

void UIActionsDrawable::Hide()
{
  // hide immediatelly
  _dimStart = Glob.uiTime - fadeTime - 1.0;
  _default = false;
  _selected = NULL;
  _selectedPriority = -FLT_MAX;
}

float UIActionsDrawable::GetAge() const
{
  return Glob.uiTime - _dimStart;
}


float UIActionsDrawable::GetAlpha() const
{
  float age = GetAge();
  if (age <= dimTime) return 1.0f;
  if (age >= fadeTime) return 0.0f;
  return fadeCoef * (fadeTime - age);
}

int UIActionsDrawable::FindSelected()
{
  int n = Size();

  int first = -1;
  for (int i=0; i<n; i++)
  {
    const UIAction &item = Get(i);
    if (item.show)
    {
      first = i;
      break;
    }
  }
  if (first < 0) return -1;

  if (!_selected) return first; // default
  for (int i=0; i<n; i++)
  {
    const UIAction &item = Get(i);
    DoAssert(item.action);
    if (item.show && item.action->IsEqual(_selected)) return i;
  }
  _selected = NULL;
  _selectedPriority = -FLT_MAX;

#if _HELISIM_PRECISE_INTERACTION
//find new default action in old actions
  for (int i=0; i<n; i++)
  {
    const UIAction &item = Get(i);
    DoAssert(item.action);
    if (item.show && item.showWindow) 
    {
      _selected = item.action;
      _selectedPriority = item.priority;
      return i;
    }
  }
#endif

  if (_default) Hide();
  return first; // default
}

void UIActionsDrawable::SelectPrev(bool cycle)
{
  if (Size() == 0) return;

  int selected = FindSelected();
  for (int i=0; i<Size(); i++) // avoid infinite loop
  {
    if (selected >= 0)
    {
      selected--;
      if (selected < 0)
      {
        if (cycle) selected = Size() - 1;
        else selected = 0;
      }
    }
    else selected = 0;

    const UIAction &action = Get(selected);
    if (action.show && !action.action->IsEqual(_selected))
    {
      _selected = Get(selected).action;
      _selectedPriority = Get(selected).priority;
      return;
    }
  }
}

void UIActionsDrawable::SelectNext(bool cycle)
{
  if (Size() == 0) return;

  int selected = FindSelected();
  for (int i=0; i<Size(); i++) // avoid infinite loop
  {
    if (selected >= 0)
    {
      selected++;
      if (selected >= Size())
      {
        if (cycle) selected = 0;
        else selected = Size() - 1;
      }
    }
    else selected = 0;

    const UIAction &action = Get(selected);
    if (action.show && !action.action->IsEqual(_selected))
    {
      _selected = Get(selected).action;
      _selectedPriority = Get(selected).priority;
      return;
    }
  }
}

// some actions are processed on the client owner (UI actions, position updates)
static bool ActionHandledByServer(UIActionType type)
{
  switch (type)
  {
    case ATGear:
      // shortcut for ATGear action ... we want to create dialog on client, so avoid sending to server and back
    case ATInspect:
      return false;
    case ATLadderUp:
    case ATLadderDown:
    case ATLadderOnUp:
    case ATLadderOnDown:
    case ATLadderOff:
      // ladder simulation is handled by the client (owner)
      return false;
  }
  return true;
}

void UIActionsDrawable::ProcessAction(int index, AIBrain *unit, bool fromGUI, bool byShortcut)
{
  Assert(unit);
  Assert(index >= 0);
  UIAction &action = Set(index);
  DoAssert(action.action);
  action.action->SetFromGUI(fromGUI);

  bool canHide = true;
  if (!_default)
  {
    for (int i=0; i<Size(); i++)
    {
      if (i != index && Get(i).showWindow)
      {
        // other action enforces action menu
        canHide = false;
        break;
      }
    }
  }

  if (action.hideOnUse )//&& canHide)
  {
    Hide();
    _selected = NULL;
    _selectedPriority = -FLT_MAX;
  }
  else if (fromGUI && !byShortcut)
  {
    _dimStart = Glob.uiTime;
  }
  if (!GetNetworkManager().IsServerOwner() && ActionHandledByServer(action.action->GetType()))
    GetNetworkManager().ProcessAction(action.action, unit); // ask server to execute the action
  else
    unit->GetVehicle()->StartActionProcessing(action.action, unit);
}

static int CmpActions(const UIAction *action1, const UIAction *action2)
{
  int s = sign(action2->priority - action1->priority);
  if (s != 0) return s;
  DoAssert(action1->action);
  DoAssert(action2->action);
  return action1->action->Compare(action2->action);
}

void UIActions::Sort()
{
  QSort(Data(), Size(), CmpActions);
}

void UIActionsDrawable::Update(UIActions &src)
{
  int n = src.Size();

  // update creation time of actions
  for (int i=0; i<n; i++)
  {
    UIAction &action = src[i];
    bool found = false;
    for (int j=0; j<Size(); j++)
    {
      const UIAction &compare = Get(j);
      if (compare.IsEqual(action))
      {
        if(action.show == compare.show) action.created = compare.created;
        break;
      }
    }
    if (!found) for (int j=0; j<_newActions.Size(); j++)
    {
      const UIAction &compare = _newActions[j];
      if (compare.IsEqual(action))
      {
        action.created = compare.created;
        break;
      }
    }
  }

  // move too new actions to separate array
  const float minAge = 0.1f;
  _newActions.Resize(0);
  for (int i=0; i<src.Size();)
  {
    const UIAction &action = src[i];
    float age = Glob.uiTime - action.created;
    if (age < minAge)
    {
      _newActions.Add(action);
      src.Delete(i);
    }
    else i++;
  }
  n = src.Size();

  // check for new action
  int newAction = -1;
  if (n == 0)
  {
    if (Size() > 0) Hide();
  }
  else if (_default || GetAge() > dimTime)
  {
    for (int i=0; i<n; i++)
    {
      const UIAction &action = src[i];
      if (!action.show) continue;
      if (!action.showWindow) continue;

      // update selected action
      DoAssert(action.action);
      if (action.action->IsEqual(_selected))
      {
        _selected = action.action;
        _selectedPriority = action.priority;
      }

      bool found = false;
      if (!_default)
      {
        // if default action is shown, action with higher priority always replace it
        for (int j=0; j<Size(); j++)
        {
          const UIAction &compare = Get(j);
          if (compare.IsEqual(action))
          {
            found = compare.showWindow;
            break;
          }
        }
      }
      if (!found)
      {
        newAction = i;
        break;
      }
    }
  }
  if (newAction >= 0)
  {
    // do not show action with less priority
    if (_default && src[newAction].priority <= _selectedPriority)
      newAction = -1;
    else
    {
      _selected = src[newAction].action;
      _selectedPriority = src[newAction].priority;
    }
  }

#if _HELISIM_PRECISE_INTERACTION
  if(_default)
  {
    //if menu is not opened, select closer action even if it has slighly smaller priority 
    Camera *camera = GScene->GetCamera();
    Vector3 selPosition;
    if(camera)
    {
      Vector3 camdir = GInput.ActionDriverFreeLook() ? camera->Direction().Normalized() : GWorld->UI()->GetCursorDirection().Normalized();
      Vector3 camPos = camera->Position();

      for (int i=0; i<n; i++)
      {
        const UIAction &action = src[i];
        if (action.action->IsEqual(_selected)) 
        {
          selPosition = action.position;
          break;
        }
      }

      for (int i=0; i<n; i++)
      {
        const UIAction &action = src[i];
        if (!action.show) continue;
        if (!action.showWindow) continue;

        if (!action.action->IsEqual(_selected) && action.radiusView >0)
        {
          //note: action.priority = act.priority -0.001 * i;
          //therefore test for same priority cannot be used
          if((_selectedPriority - action.priority) < 0.25)
          {
            Vector3 targetDir = action.position - camPos;
            targetDir.Normalize();
            Vector3 selectedDir = selPosition - camPos;
            selectedDir.Normalize();

            if(selectedDir * camdir < targetDir * camdir)
            {
              _selected = action.action;
              _selectedPriority = action.priority;
              selPosition = action.position;
            }
          }
          else break;
        }
      }
    }
  }
#endif

  Resize(n);
  for (int i=0; i<n; i++) Set(i) = src[i];

  if (newAction >= 0)  Refresh(false);
  if(FindSelected()<0) Hide();
 
  
  _newActions.CompactIfNeeded();

#if _HELISIM_PRECISE_INTERACTION && _HELISIM_ROTORLIB
  if((_selected != _lastSelected) && ((_selected && !_lastSelected) || !_lastSelected->IsEqual(_selected)))
  {
    AIBrain *agent = GWorld->FocusOn();
    int type = _selected?  (int)_selected->GetType():-1;
    RString text = _selected ? UIActions::actionTypes[_selected->GetType()].text : "";
    if(agent && agent->GetPerson())
    {
      if(type >= ATEnginesOnAuto && type <= ATWheelsBrakeOff) 
      {
        //rotor lib action - we need to be suer action index does not change by adding default action
        agent->GetPerson()->OnEvent(EESelectedRotorLibActionChanged, _selected ? _selected->GetTarget(): NULL, type - ATEnginesOnAuto, _selected ? _selected->GetUserActionID(): -1);
        GWorld->OnMissionEvent(MESelectedRotorLibActionChanged, _selected ? _selected->GetTarget(): NULL, type  - ATEnginesOnAuto, _selected ? _selected->GetUserActionID(): -1);
      }
      else 
      {
        //default actions
        agent->GetPerson()->OnEvent(EESelectedActionChanged, _selected ? _selected->GetTarget(): NULL, type, _selected ? _selected->GetUserActionID(): -1);
        GWorld->OnMissionEvent(MESelectedActionChanged, _selected ? _selected->GetTarget(): NULL, type, _selected ? _selected->GetUserActionID(): -1);
      }
    }
    _lastSelected = _selected;

    GWorld->UI()->SetTextTipDelayTime(Glob.uiTime + _textTipDelay);
  }
#endif
}

static float ActionSourceCost(EntityAI *veh, Target *tgt)
{
  // check target focus
  Assert(tgt->idExact);
  Vector3 relPos = veh->PositionWorldToModel(tgt->idExact->AimingPosition(tgt->idExact->FutureVisualState()));
  float dist2 = relPos.SquareSize();
  if (relPos.Z()<0)
  {
    dist2 *= 8;
  }
  if (fabs(relPos.X())>relPos.Z())
  {
    dist2 *= 4;
  }
  return dist2;
}

/// virtual target list - provide access to all static entities in the given area
class TargetStaticList
{
  Vector3 _pos;
  float _radius;
  
  public:
  
  TargetStaticList(Vector3Par pos, float radius)
  :_pos(pos),_radius(radius)
  {
  }
  
  template <class Functor>
  bool ForEach(const Functor &f) const;
};

template <class Functor>
bool TargetStaticList::ForEach(const Functor &f) const
{
  int xMin,xMax,zMin,zMax;
  ObjRadiusRectangle(xMin,xMax,zMin,zMax,_pos,_pos,_radius);
  for (int x=xMin; x<=xMax; x++) for (int z=zMin; z<=zMax; z++)
  {
    const ObjectListUsed &list = GLandscape->UseObjects(x,z,Landscape::UONoWait);
    int n = list.Size();
    for (int i=0; i<n; i++)
    {
      Object *obj = list[i];
      // check if it is a target entity
      EntityAI *ai = dyn_cast<EntityAI>(obj);
      if (!ai) continue;
      float dist2 = ai->FutureVisualState().Position().Distance2Inline(_pos);
      if (dist2>Square(_radius+ai->GetRadius())) continue;
      if (f(ai)) return true;
    }
  }
  
  return false;
}

#if _ENABLE_CONVERSATION
static void InterruptConversation(ConversationContext *conversationContext)
{
  if (!conversationContext || conversationContext->_topic.GetLength() == 0) return;

  // player do not want to answer, we may want to react somehow
  AIBrain *sender = conversationContext->_askingUnit;
  AIBrain *receiver = conversationContext->_askedUnit;
  if (!sender || !receiver) return;

  Ref<KBMessageInfo> message = receiver->CreateKBMessage(conversationContext->_topic, "interrupted");
  if (message)
  {
    receiver->KBReact(sender, message);
  }
}
#endif

static void CheckActionsRadiusView(UIActions *actions, AIBrain *unit)
{
#if _HELISIM_PRECISE_INTERACTION
  //check only for player
  if(!unit || !unit->IsPlayer()) return;

  PROFILE_SCOPE_EX(CamPo, proAc);

  Camera *camera = GScene->GetCamera();
  if(!camera) return;

  Vector3 cameraPosition = camera->Position();

  //cursor direction
  Vector3 camDir = GInput.ActionDriverFreeLook() ? camera->Direction() : GWorld->UI()->GetCursorDirection().Normalized();

  //check radius view, update position if modelPosition is present
  for (int i = actions->Size() -1 ; i >= 0; i--)
  {
    UIAction &action = (*actions)[i];
    //skip precise action testing for action without radiusView (important for UsePreciseGetInAction)
    if(action.radiusView < 0 || action.radius < 0) continue;

    if( action.modelPositions.Size() > 0)
    {
      // some position is valid
      bool validPresence = false;
      // if no valid mem. point is defined, use default behaviour 
      bool atLeastOneMemPoint = false;

      EntityAI *target = action.action->GetTarget();
      if(target && target->GetType()->GetShape())
      {
        //find memory point
        LODShape *shape = target->GetType()->GetShape(); 
        const Shape* mem = shape->MemoryLevel();

        //check all memory points, if they are valid and close to player
        for (int j = 0; j< action.modelPositions.Size(); j++)
        {
          int positionIndex = (shape && mem) ? shape->PointIndex(mem,(*actions)[i].modelPositions[j]) : -1;
          //if memory point not found - do not show in 3D, skip radius and radiusView testing
          if(positionIndex<0) continue;
          //by default hidden (available for shortcuts)
          action.show = false;

          atLeastOneMemPoint = true;

          //special case rules
          if(
            (action.available & 1 && unit != target->PilotUnit() && unit != target->CopilotUnit()) ||
            (action.available & 2 && unit->GetVehicleIn() != target) ||
            (action.available & 4 && unit->GetVehicleIn() == target)) goto removeAction;

          //since in vehicle you don't have proper cursor, skip action 
          if(unit->GetVehicleIn() && GWorld->GetCameraType() == CamExternal) 
          {
            //do not remove, so it can be used for shorcuts
            validPresence = true;
            goto removeAction;
          }

          Vector3 modelPosition;
          int memLevel = shape->FindMemoryLevel();
          if(memLevel > -1) modelPosition = target->AnimatePoint(target->RenderVisualState(), memLevel, positionIndex);

          //update position in 3D (AI doesn't care about button position)
          action.position = target->RenderVisualState().PositionModelToWorld(modelPosition);

          //action position direction
          Vector3 targetDir = action.position - cameraPosition; 
          if(action.radiusView > 0 && camDir * targetDir < 0) continue;

          //check radius
          if (action.radius > 0)
          {
            if(GWorld->GetCameraType() == CamExternal)
            {
              if(unit->GetVehicle()->AimingPosition(unit->GetVehicle()->RenderVisualState()).Distance2((*actions)[i].position) > Square((*actions)[i].radius)) continue;
            }
            else if(cameraPosition.Distance2(action.position) > Square(action.radius)) continue;
          }

          //check if we ar looking at modelPos
          if(action.radiusView > 0 && GScene->GetCamera())
          {
            //minimal distance
            Vector3 distnceVect = ( cameraPosition +  camDir * (targetDir * camDir)) - action.position;
            //only hide action, so shortcut can be still used 
            if( distnceVect.Size() <= action.radiusView)
            {
              action.show = true;
              //everything is ok, no need to test all memory points
              validPresence = true;
              break;
            }
          }

          //do not remove (precise action is valid and available)
          validPresence = true;
        }
      }
removeAction:
      //none of memory points is valid or is not in radiusradiusView - remove action
      if(validPresence == false && atLeastOneMemPoint) (*actions).Delete(i);
    }
  }
#endif
}

static bool IsAccessible(EntityAI *entity, const EntityVisualState &vs, Vector3Par pos)
{
  Vector3 modelPos = vs.PositionWorldToModel(pos);

  // limit angle
  if (fabs(modelPos.X()) > modelPos.Z()) return false;

  // limit distance
  float maxDistance = entity->CollisionSize() + 2.0f;
  if (modelPos.SquareSize() > Square(maxDistance)) return false;

  return true;
}

void InGameUI::UpdateActions(const AutoArray<UIAction> &myActions, const AutoArray<UIAction> &targetActions)
{
  _myActions = myActions;
  _targetActions = targetActions;
}

/*!
\patch_internal 1.27 Date 10/17/2001 by Jirka
- Fixed: ProcessActions assumed unit->GetGroup() != NULL - crash occured when this condition where not satisfied
\patch 1.79 Date 7/26/2002 by Ondra
- New: MP: Player who is disconnected from server cannot activate actions.
\patch 5117 Date 1/15/2007 by Bebul
- Fixed: When the command menu is up, many keys/functions do not work.
\patch 5160 Date 5/17/2007 by Bebul
- Fixed: Some Joystick buttons have fixed function in UI and game.
*/
void InGameUI::ProcessActions(AIBrain *unit)
{
  PROFILE_SCOPE_EX(proAc,ui);
  if (!CanProcessActions(unit))
  {
    _actions.Clear();
    return;
  }

  if (IsCommandingForced() /*|| unit->IsSpeaking() || unit->IsListening()*/ ||
    (_actions.IsListVisible() && _actions.FindSelected() >= 0 && GInput.GetActionToDo(UAMenuBack,true,true,true,2,/*BLOCK*/true)))
  {
    _actions.Hide();
  }

  // first process user input, than collect new actions (actions may changed)

  //////////////////////////////////////////////////////////////////////////
  // user input

  float age = _actions.GetAge();
  bool def = _actions.IsDefault();
  bool visible = def || age >= protectionTime && age <= dimTime;

  {
    PROFILE_SCOPE_EX(usrIn, proAc);

    if (!IsCommandingForced() && _showHUD)
    {
      if(!GInput.keyPressed[DIK_LCONTROL] && !GInput.keyPressed[DIK_RCONTROL])
      {
        if (GInput.GetActionToDo(UANextAction))
        {
          if (visible && !def)
            _actions.SelectNext(false);
          if(_actions.FindSelected()>=0) _actions.Refresh(true);
        }
        if (GInput.GetActionToDo(UAPrevAction))
        {
          if (visible && !def)
            _actions.SelectPrev(false);
          if(_actions.FindSelected()>=0) _actions.Refresh(true);
        }
      }
      bool confirmMenu = false;

#if _HELISIM_PRECISE_INTERACTION
      bool unarmedSoldier = false;

      //confirm action, if soldier has no weapon, shoot person inf front of you otherwise
      if(unit->IsFreeSoldier())
      {
        if (unit->GetPerson() && unit->GetPerson()->GetWeapons()._currentWeapon < 0) unarmedSoldier = true;
      }

      if(
        GWorld->GetCameraType() != CamGunner &&
        !_actions.IsListVisible() &&
        ((GInput.ActionDriverFreeLook() || unarmedSoldier) && GInput.GetActionToDo(UAActionFeeLook,true,true,false,2, true))
        )
      {
        if(_actions.GetSelected()) confirmMenu = true;
      }
#endif

      if (confirmMenu ||( GInput.GetActionToDo(UAAction)))
      {
        if (visible)
        {
          if (_actions.Size() > 0)
          {
            if (!GetNetworkManager().IsControlsPaused())
            {
              int index = _actions.FindSelected();          
              if (index >= 0 && _actions[index].shortcut != UACancelAction) _actions.ProcessAction(index, unit, true);
            }
          }
          else
            _actions.Hide();
        }
        else
          _actions.Refresh(true);
      }
    }
    // SourceDayZ: the Gear key (TAB by default) must also open the vehicle
    // cargo while seated inside a vehicle. In MP the server does not send
    // the vehicle's Gear action when the vehicle is owned by the client
    // (ResourceSupply::GetActions skips non-local vehicles), so the action
    // list has no ATGear and the shortcut loop below would do nothing.
    {
      Transport *vehIn = unit->GetVehicleIn();
      if (vehIn && !GWorld->UserDialog() && vehIn->GetSupply() &&
          !vehIn->IsDamageDestroyed() && GInput.GetActionToDo(UAGear))
      {
        Display *CreateInventoryDialog(ControlsContainer *parent, AIBrain *me, EntityAI *source);
        Display *dlg = CreateInventoryDialog(NULL, unit, vehIn);
        if (dlg && GWorld->SetUserDialog(dlg))
        {
          extern bool GUAGearActive;
          GUAGearActive = false;
          RptF("SDZ gear key in vehicle: %s", cc_cast(vehIn->GetType() ? vehIn->GetType()->GetName() : "?"));
        }
      }
    }

    // shortcuts
    for (int i=0; i<_actions.Size(); i++)
    {
      UIAction &action = _actions[i];
      if (action.shortcut >= 0 && GInput.GetActionToDo(action.shortcut))
        _actions.ProcessAction(i, unit, true, true);
    }
  }

  //////////////////////////////////////////////////////////////////////////
  // collect all actions

  UIActions actions;
  static StaticStorage<UIAction> actionStorage;
  actions.SetStorage(actionStorage.Init(64));

#if _HELISIM_ROTORLIB
  if(GWorld->IsUserActionEnabled())
#endif
  {
    PROFILE_SCOPE_EX(colAc, proAc);

    Person *person = unit->GetPerson();
    if (person->CanCancelAction())
    {
      UIAction action(new ActionBasic(ATCancelAction, person));
      actions.Add(action);
    }
    else
    {
      // my vehicle
      EntityAI *veh = unit->GetVehicle();
      Object::ProtectedVisualState<const EntityVisualState> vsVeh = veh->FutureVisualStateScope();
      if (!GetNetworkManager().IsServerOwner())
      {
        // _myActions, _targetActions are filled by the network messages
      }
      else
      {
        // fill _myActions, _targetActions now
        _myActions.Clear();
        _targetActions.Clear();

        {
          PROFILE_SCOPE_EX(vehAc,colAc);
          veh->GetActions(
            [this](const UIAction &action) -> void { _myActions.Add(action); },
            unit);
        }

        // FIX: enable TouchOff action in vehicles
        if (person != veh) 
        {
          PROFILE_SCOPE_EX(perAc, colAc);
          Object::ProtectedVisualState<const ObjectVisualState> vsPerson = person->FutureVisualStateScope(); 
          person->GetActions(
            [this](const UIAction &action) -> void { _myActions.Add(action); },
            unit);
        }

        InventoryItem *inHands = person->GetItemInHands();
        if (inHands)
        {
          // collect user actions for item in hands
          inHands->GetUserActions(
            [this](const UIAction &action) -> void { _myActions.Add(action); },
            unit, true);
        }

        // target
        Object *entity = _cursorObject;
        if (entity)
        {
          if (entity != veh && entity != person)
          {
            PROFILE_SCOPE_EX(enAc, colAc);
            Object::ProtectedVisualState<const ObjectVisualState> vsEntity = entity->FutureVisualStateScope();
            entity->GetActions(
              [this](const UIAction &action) -> void { _targetActions.Add(action); },
              unit);
          }
        }
        else if (!_cursorSurface.IsEmpty())
        {
          // add the actions available on this surface
          ObjectActionsMapItem *item = SurfaceActions.get(_cursorSurface);
          if (item)
          {
            GameState *state = GWorld->GetGameState();
            GameVarSpace local(state->GetContext(), false);
            int gshandle = state->BeginContext(&local);

            state->VarSetLocal("this", _cursorSurface, true, true);
            state->VarSetLocal("_name", "SurfaceAction", true, true);
            state->VarSetLocal("_owner", GameValueExt(person), true, true);
            InventoryItem *inHands = person ? person->GetItemInHands() : NULL;
            state->VarSetLocal("_inHands", GameValueExt(inHands), true, true);
            GameValue value = state->CreateGameValue(GameArray);
            GameArrayType &array = value;
            array.Add(_cursorObjectPos[0]);
            array.Add(_cursorObjectPos[2]);
            state->VarSetLocal("_pos", value, true, true);

            for (int i=0; i<(*item)->Size(); i++)
            {
              ObjectActionsMapAction &src = (**item)[i];
              if (!src._condition.IsEmpty() && !state->EvaluateBool(src._condition, GameState::EvalContext::_default, GWorld->GetMissionNamespace())) continue; // mission namespace

              UIAction action(new ActionSurface(ATSurfaceAction, _cursorSurface, i, _cursorObjectPos));
              action.priority = src._priority;
              action.showWindow = src._showWindow;
              action.hideOnUse = src._hideOnUse;

              _targetActions.Add(action);
            }

            state->EndContext(gshandle);
          }
        }
      }

      // actions are in _myActions, _targetActions
      // filter them to actions
      for (int i=0; i<_myActions.Size(); i++) actions.Add(_myActions[i]);
      // for _targetActions, position need to be checked
      for (int i=0; i<_targetActions.Size(); i++)
      {
        const UIAction &action = _targetActions.Get(i);
        if (!action.action) continue;
        Object *target = action.action->GetTarget();

        // SourceDayZ: allow Gear on the focused vehicle even when the addon
        // config has no supply memory point (the accessibility check would use
        // the hull center and reject the action).
        if (action.action->GetType() == ATGear && target == _cursorObject &&
            person && person->FutureVisualState().Position().DistanceXZ2(_cursorObjectPos) < Square(5.0f))
        {
          actions.Add(action);
          continue;
        }

        if (target)
        {
          Object::ProtectedVisualState<const ObjectVisualState> vsTarget = target->FutureVisualStateScope();

          if (action._memoryPoints.Size() > 0)
          {
            const LODShape *shape = target->GetShape();
            if (shape)
            {
              int mem = shape->FindMemoryLevel();
              if (mem >= 0)
              {
                for (int j=0; j<action._memoryPoints.Size(); j++)
                {
                  Vector3 pos = vsTarget->PositionModelToWorld(target->AnimatePoint(vsTarget, mem, action._memoryPoints[j]));
                  if (IsAccessible(veh, vsVeh, pos))
                  {
                    actions.Add(action);
                    break;
                  }
                }
              }
            }
          }
          else
          {
            if (IsAccessible(veh, vsVeh, target->AimingPosition(vsTarget)))
              actions.Add(action);
          }
        }
        else if (action.action->GetType() == ATSurfaceAction)
        {
          // no target required
          actions.Add(action);
        }
      }

      {
        PROFILE_SCOPE_EX(addAc, colAc);

        // Gear action (if not already present)
        bool found = false;
        for (int i=0; i<actions.Size(); i++)
        {
          if (actions[i].action->GetType() == ATGear)
          {
            found = true;
            break;
          }
        }
        if (!found)
        {
          UIAction action(new ActionBasic(ATGear, NULL));
          action.show = false;
          action.showWindow = false;
          actions.Add(action);
        }
      }
    }
  }

#if _HELISIM_PRECISE_INTERACTION
  {
    PROFILE_SCOPE_EX(chAcR, proAc);
    CheckActionsRadiusView(&actions, unit);
  }
#endif
  

  actions.Sort();
  _actions.Update(actions);
}

///////////////////////////////////////////////////////////////////////////////
// Global selected units - valid for IngameUI and map

DisplayUnitInfo::DisplayUnitInfo(ControlsContainer *parent)
  : Display(parent)
{
  SetCursor(NULL);
  backgroundHeight = 0;
  backgroundWidth = 0;
  //InitControls();
#if _HELISIM_ROTORLIB
  _HUDColor = GWorld->UI()->GetHelicopterHUDColor();
#endif
}

void DisplayUnitInfo::InitControls()
{
  // InitPtr used, no NULL assignment is required
  time = NULL;
  date = NULL;
  name = NULL;
  valueExp = NULL;
  combatMode = NULL;
  valueHealth = NULL;
  weapon = NULL;
  ammo = NULL;
  gunnerWeapon = NULL;
  ammoMode = NULL;
  background = NULL;
  vehicle = NULL;
  speed = NULL;
  alt = NULL;
  heading = NULL;
  alt_wanted = NULL;
  speed_wanted = NULL;
  position = NULL;
  opticZoom = NULL;
  radarRange = NULL;
#if _VBS3
  magazine = NULL;
  override_status = NULL;
  //TI
  ti_background = NULL;
  ti_mode= NULL;
  ti_brightness_txt = NULL;
  ti_brightness = NULL;
  ti_contrast_txt = NULL;
  ti_contrast = NULL;
  ti_autocontrast = NULL;
#endif
  valueArmor = NULL;
  valueFuel = NULL;
  valueReload = NULL;
  cargoMan = NULL;
  cargoFuel = NULL;
  cargoRepair = NULL;
  cargoAmmo = NULL;
  commander = NULL;
  driver = NULL;
  gunner = NULL;
  gunnerWeapon = NULL;
  ammoMode = NULL;
  counterMeasuresAmmo = NULL;
  counterMeasuresMode = NULL;

#if _HELISIM_ROTORLIB
  airSpeedIndicator = NULL;
  airSpeedIndicatorAxis = NULL;
  airSpeedIndicatorValue = NULL;
  altimeter = NULL;
  altimeterAxis = NULL;
  altimeterValue = NULL;
  attitudeIndicator = NULL;
  attitudeIndicatorLite = NULL;
  gMeter = NULL;
  radarAltimeter = NULL;
  radarAltimeterBar = NULL;
  radarAltimeterValue = NULL;
  torque = NULL;
  winIndicator = NULL;
  stabilityIndicator = NULL;
  collective = NULL;
  trimIndicator = NULL;
  autohover = NULL;
  heliHUDmisc = NULL;

  speedAnalogue = NULL;
  speedAnalogueNeedle = NULL;
  speedAnalogueMin = NULL;
  speedAnalogueMax = NULL;
  speedAnalogueValue = NULL;
  speedAnalogueImperial = NULL;
  speedAnalogueMetric  = NULL;

  altitudeAnalogue = NULL;
  altitudeAnalogueNeedle = NULL;
  altitudeAnalogueMin = NULL;
  altitudeAnalogueMax = NULL;
  altitudeAnalogueValue = NULL;
  altitudeAnalogueImperial = NULL;
  altitudeAnalogueMetric  = NULL;

  compassAnalogue = NULL;
  compassAnalogueNeedle = NULL;
  compassAnalogueWind = NULL;
  compassAnalogueMin = NULL;
  compassAnalogueMax = NULL;
  compassAnalogueValue = NULL;
  compassAnalogueWindValue = NULL;

  stabilityAnalogue = NULL;
  stabilityAnalogueSpeed = NULL;
  stabilityAnalogueValue = NULL;
  stabilityAnalogueVSpeedNeedle = NULL;
  stabilityAnalogueCollective = NULL;
  stabilityAnalogueVSpeed = NULL;
  stabilityAnalogueImperial = NULL;
  stabilityAnalogueMetric = NULL;

  horizonAnalogue = NULL;
  horizonAnalogueAttitude = NULL;
  horizonAnalogueValue = NULL;

  statusDamage = NULL;
  statusEnginesRPM = NULL;
  statusEnginesDamage = NULL;
  statusTorque = NULL;
  statusElectricity = NULL;
  statusFuel = NULL;
  temperature = NULL;
  mainRotor = NULL;
  tailRotor = NULL;
  statusAutohover = NULL;
#endif
}

void DisplayUnitInfo::Reload(ParamEntryVal clsEntry)
{
  Init();
  InitControls();
  LoadFromEntry(clsEntry);

  _updateHeightByCrew = true;
  ConstParamEntryPtr entry = clsEntry.FindEntry("updateHeightByCrew");
  if (entry) _updateHeightByCrew = *entry;
  
  _updateWidthByCrew = true;
  entry = clsEntry.FindEntry("updateWidthByCrew");
  if (entry) _updateWidthByCrew = *entry;

  _updateWidthByWeapon = true;
  entry = clsEntry.FindEntry("updateWidthByWeapon");
  if (entry) _updateWidthByWeapon = *entry;
}

#define STATIC_CTRL(CTRL_IDC,NAME) \
    case CTRL_IDC: \
    NAME = new CStatic(this, idc, cls); \
    return NAME;

#define PROGRESS_CTRL(CTRL_IDC,NAME) \
    case CTRL_IDC: \
    NAME = new CProgressBar(this, idc, cls); \
    return NAME;

#if _VBS3_LASE_LEAD
DisplayLaserRange::DisplayLaserRange(ControlsContainer *parent)
: Display(parent)
{
  SetCursor(NULL);
  InitControls();
  LoadFromEntry(Pars >> "RscInGameUI" >> "RscLaserRange");
}

void DisplayLaserRange::InitControls()
{
  range = NULL;
  rangeNum = NULL;
}

Control *DisplayLaserRange::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
    STATIC_CTRL(IDC_IGLR_BG,background)
      STATIC_CTRL(IDC_IGLR_RANGE,range)
      STATIC_CTRL(IDC_IGLR_RANGE_NUM,rangeNum)
  }
  return Display::OnCreateCtrl(type, idc, cls);
}
#endif
Control *DisplayUnitInfo::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
    case IDC_IGUI_BG:
      background = new CStatic(this, idc, cls);
      backgroundHeight = background->H();
      backgroundWidth = background->W();
      return background;
    case IDC_IGUI_TIME:
#if _VBS3
      time = new CStaticTime(this, idc, cls, false); //no blinking
#else
      time = new CStaticTime(this, idc, cls, true);
#endif
      return time;
    case IDC_IGUI_DATE:
      date = new CStatic(this, idc, cls);
      return date;
    case IDC_IGUI_NAME:
      name = new CStatic(this, idc, cls);
      return name;
    case IDC_IGUI_VALUE_EXP:
      valueExp = new CProgressBar(this, idc, cls);
      return valueExp;
    case IDC_IGUI_COMBAT_MODE:
      combatMode = new CStatic(this, idc, cls);
      return combatMode;
    case IDC_IGUI_VALUE_HEALTH:
      valueHealth = new CProgressBar(this, idc, cls);
      return valueHealth;
    case IDC_IGUI_WEAPON:
      weapon = new CStatic(this, idc, cls);
      return weapon;
    case IDC_IGUI_AMMO:
      ammo = new CStatic(this, idc, cls);
      return ammo;
    case IDC_IGUI_COUNTER_MEASURES_AMMO:
      counterMeasuresAmmo = new CStatic(this, idc, cls);
      return counterMeasuresAmmo;
    case IDC_IGUI_COUNTER_MEASURES_MODE:
      counterMeasuresMode = new CStatic(this, idc, cls);
      return counterMeasuresMode;
    case IDC_IGUI_WEAPON_GUNNER:
      gunnerWeapon = new CStatic(this, idc, cls);
      return gunnerWeapon;
    case IDC_IGUI_WEAPON_MODE:
      ammoMode = new CStatic(this, idc, cls);
      return ammoMode;
    case IDC_IGUI_VEHICLE:
      vehicle = new CStatic(this, idc, cls);
      return vehicle;
    case IDC_IGUI_SPEED:
      speed = new CStatic(this, idc, cls);
      return speed;
    case IDC_IGUI_ALT:
      alt = new CStatic(this, idc, cls);
      return alt;
    case IDC_IGUI_RADARRANGE:
      radarRange = new CStatic(this, idc, cls);
      return radarRange;
    case IDC_IGUI_HEADING:
      heading = new CStatic(this, idc, cls);
      return heading;
    STATIC_CTRL(IDC_IGUI_ALT_WANTED,alt_wanted)
    STATIC_CTRL(IDC_IGUI_SPEED_WANTED,speed_wanted)
    STATIC_CTRL(IDC_IGUI_POSITION,position)
    STATIC_CTRL(IDC_IGUI_OPTIC,opticZoom)
#if _VBS3
      STATIC_CTRL(IDC_IGUI_MAGAZINE,magazine)
      STATIC_CTRL(IDC_IGUI_OVR_STATUS,override_status)
      //TI
      STATIC_CTRL(IDC_IGUI_TI_BACK,ti_background)
      STATIC_CTRL(IDC_IGUI_TI_MODE,ti_mode)
      STATIC_CTRL(IDC_IGUI_TI_BRIGHTNESS_TXT,ti_brightness_txt)
      STATIC_CTRL(IDC_IGUI_TI_CONTRAST_TXT,ti_contrast_txt)
      STATIC_CTRL(IDC_IGUI_TI_AUTO_CONTRAST,ti_autocontrast)
      PROGRESS_CTRL(IDC_IGUI_TI_BRIGHTNESS,ti_brightness)
      PROGRESS_CTRL(IDC_IGUI_TI_CONTRAST,ti_contrast)
#endif
    case IDC_IGUI_VALUE_ARMOR:
      valueArmor = new DisplayHitZoneInfo(this, idc, cls);
      return valueArmor;
    case IDC_IGUI_VALUE_FUEL:
      valueFuel = new CProgressBar(this, idc, cls);
      return valueFuel;
    case IDC_IGUI_VALUE_RELOAD:
      valueReload = new CProgressBar(this, idc, cls);
      return valueReload;
    case IDC_IGUI_CARGO_MAN:
      cargoMan = new CStatic(this, idc, cls);
      return cargoMan;
    case IDC_IGUI_CARGO_FUEL:
      cargoFuel = new CStatic(this, idc, cls);
      return cargoFuel;
    case IDC_IGUI_CARGO_REPAIR:
      cargoRepair = new CStatic(this, idc, cls);
      return cargoRepair;
    case IDC_IGUI_CARGO_AMMO:
      cargoAmmo = new CStatic(this, idc, cls);
      return cargoAmmo;
    case IDC_IGUI_COMMANDER:
      commander = new CStatic(this, idc, cls);
      return commander;
    case IDC_IGUI_DRIVER:
      driver = new CStatic(this, idc, cls);
      return driver;
    case IDC_IGUI_GUNNER:
      gunner = new CStatic(this, idc, cls);
      return gunner;
#if _HELISIM_ROTORLIB
    case IDC_IGUI_AIRSPEEDINDICATOR:
      airSpeedIndicator = new CControlsGroup(this, idc, cls);
      if (airSpeedIndicator)
      {
        airSpeedIndicatorAxis = static_cast<CControlsGroup *>(airSpeedIndicator->GetCtrl(IDC_IGUI_AIRSPEEDINDICATOR_AXIS));
        airSpeedIndicatorValue = static_cast<CStatic *>(airSpeedIndicator->GetCtrl(IDC_IGUI_AIRSPEEDINDICATOR_VALUE));
        airSpeedIndicatorValue->SetTextColor(_HUDColor);
      }
      return airSpeedIndicator;
    case IDC_IGUI_ALTIMETER:
      altimeter = new CControlsGroup(this, idc, cls);
      if (altimeter)
      {
        altimeterAxis = static_cast<CControlsGroup *>(altimeter->GetCtrl(IDC_IGUI_ALTIMETER_AXIS));
        altimeterValue = static_cast<CStatic *>(altimeter->GetCtrl(IDC_IGUI_ALTIMETER_VALUE));
        altimeterValue->SetTextColor(_HUDColor);
      }
      return altimeter;
    case IDC_IGUI_ATTITUDEINDICATOR:
      attitudeIndicator = new CControlsGroup(this, idc, cls);
      return attitudeIndicator;
    case IDC_IGUI_ATTITUDEINDICATOR_LITE:
      attitudeIndicatorLite = new CControlsGroup(this, idc, cls);
      return attitudeIndicatorLite;
    case IDC_IGUI_GMETER:
      gMeter = new CStatic(this, idc, cls);
      gMeter->SetTextColor(_HUDColor);
      return gMeter;
    case IDC_IGUI_RADARALTIMETER:
      radarAltimeter = new CControlsGroup(this, idc, cls);
      if (radarAltimeter)
      {
        radarAltimeterBar = static_cast<CProgressBar *>(radarAltimeter->GetCtrl(IDC_IGUI_RADARALTIMETER_BAR));
        radarAltimeterBar->SetBarColor(_HUDColor);
        radarAltimeterValue = static_cast<CStatic *>(radarAltimeter->GetCtrl(IDC_IGUI_RADARALTIMETER_VALUE));
        radarAltimeterValue->SetTextColor(_HUDColor);
      }
      return radarAltimeter;
    case IDC_IGUI_TORQUE:
      torque = new CStatic(this, idc, cls);
      return torque;
    case IDC_IGUI_WINDINDICATOR:
      winIndicator = new CControlsGroup(this, idc, cls);
      return winIndicator;
    case IDC_IGUI_STABILITYINDICATOR:
      stabilityIndicator = new CControlsGroup(this, idc, cls);
      return stabilityIndicator;
    case IDC_IGUI_COLLECTIVE:
      collective = new CProgressBar(this, idc, cls);
      collective->SetBarColor(_HUDColor);
      return collective;
    case IDC_IGUI_TRIMINDICATOR:
      trimIndicator = new CStatic(this, idc, cls);
      trimIndicator->ShowCtrl(false);
      trimIndicator->SetTextColor(_HUDColor);
      return trimIndicator;
    case IDC_IGUI_AUTOHOVERNDICATOR:
      autohover = new CStatic(this, idc, cls);
      autohover->ShowCtrl(false);
      autohover->SetTextColor(_HUDColor);
      return autohover;
    case IDC_IGUI_HELIHUD_MISCELLANEOUS:
      heliHUDmisc = new CControlsGroup(this, idc, cls);
      return heliHUDmisc;
    case IDC_IGUI_ANALOGUE_SPEED:
      speedAnalogue = new CControlsGroup(this, idc, cls);
      if (speedAnalogue)
      {
        speedAnalogueNeedle = static_cast<CStatic *>(speedAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_SPEED_NEEDLE));
        speedAnalogueMin    = static_cast<CStatic *>(speedAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_SPEED_MIN));
        speedAnalogueMax    = static_cast<CStatic *>(speedAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_SPEED_MAX));
        speedAnalogueValue  = static_cast<CStatic *>(speedAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_SPEED_TEXT));

        speedAnalogueImperial  = static_cast<CStatic *>(speedAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_SPEED_IMPERIAL));
        speedAnalogueMetric  = static_cast<CStatic *>(speedAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_SPEED_METRIC));
      }
      return speedAnalogue;
    case  IDC_IGUI_ANALOGUE_ALTITUDE:
      altitudeAnalogue = new CControlsGroup(this, idc, cls);
      if (altitudeAnalogue)
      {
        altitudeAnalogueNeedle  = static_cast<CStatic *>(altitudeAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_ALTITUDE_NEEDLE));;
        altitudeAnalogueMin     = static_cast<CStatic *>(altitudeAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_ALTITUDE_MIN));
        altitudeAnalogueMax     = static_cast<CStatic *>(altitudeAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_ALTITUDE_MAX));
        altitudeAnalogueValue   = static_cast<CStatic *>(altitudeAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_ALTITUDE_TEXT));
        
        altitudeAnalogueImperial = static_cast<CStatic *>(altitudeAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_ALTITUDE_IMPERIAL));
        altitudeAnalogueMetric = static_cast<CStatic *>(altitudeAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_ALTITUDE_METRIC));
      }
      return altitudeAnalogue;
    case IDC_IGUI_ANALOGUE_COMPASS:
      compassAnalogue = new CControlsGroup(this, idc, cls);
      if (compassAnalogue)
      {
        compassAnalogueNeedle   = static_cast<CStatic *>(compassAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_COMPASS_DIR));
        compassAnalogueWind     = static_cast<CStatic *>(compassAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_COMPASS_WIND));
        compassAnalogueMin      = static_cast<CStatic *>(compassAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_COMPASS_MIN));
        compassAnalogueMax      = static_cast<CStatic *>(compassAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_COMPASS_MAX));
        compassAnalogueValue    = static_cast<CStatic *>(compassAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_COMPASS_TEXT));
        compassAnalogueWindValue = static_cast<CStatic *>(compassAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_COMPASS_WIND_TEXT));
      }
      return compassAnalogue;
    case  IDC_IGUI_ANALOGUE_STABILITY:
      stabilityAnalogue = new CControlsGroup(this, idc, cls);
      if (stabilityAnalogue)
      {
        stabilityAnalogueSpeed   =      static_cast<CStatic *>(stabilityAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_STABILITY_SPEED));
        stabilityAnalogueValue   =      static_cast<CStatic *>(stabilityAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_STABILITY_TEXT));
        stabilityAnalogueVSpeedNeedle = static_cast<CStatic *>(stabilityAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_STABILITY_VSPEED_NEEDLE));
        stabilityAnalogueCollective =   static_cast<CStatic *>(stabilityAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_STABILITY_COLLECTIVE));
        stabilityAnalogueVSpeed =       static_cast<CStatic *>(stabilityAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_STABILITY_VSPEED));

        stabilityAnalogueImperial =       static_cast<CStatic *>(stabilityAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_STABILITY_IMPERIAL));
        stabilityAnalogueMetric =       static_cast<CStatic *>(stabilityAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_STABILITY_METRIC));
      }
      return stabilityAnalogue;
    case  IDC_IGUI_ANALOGUE_ATTITUDE:
      horizonAnalogue = new CControlsGroup(this, idc, cls);
      if (horizonAnalogue)
      {
        horizonAnalogueAttitude   = static_cast<CStatic *>(horizonAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_ATTITUDE_HORIZON));
        horizonAnalogueValue      = static_cast<CStatic *>(horizonAnalogue->GetCtrl(IDC_IGUI_ANALOGUE_ATTITUDE_VALUE));
      }
      return horizonAnalogue;
    case  IDC_IGUI_DAMAGE_GROUP:
      statusDamage = new CControlsGroup(this, idc, cls);
      if (statusDamage)
      {
        statusEnginesRPM =        static_cast<CStatic *>(statusDamage->GetCtrl(IDC_IGUI_DAMAGE_ENGINESRPM));
        statusEnginesDamage =     static_cast<CStatic *>(statusDamage->GetCtrl(IDC_IGUI_DAMAGE_ENGINESDAMAGE));
        statusTorque =            static_cast<CStatic *>(statusDamage->GetCtrl(IDC_IGUI_DAMAGE_TORQUE));
        statusElectricity =       static_cast<CStatic *>(statusDamage->GetCtrl(IDC_IGUI_DAMAGE_ELECTRICITY ));
        statusFuel =              static_cast<CStatic *>(statusDamage->GetCtrl(IDC_IGUI_DAMAGE_FUEL));
        temperature =             static_cast<CStatic *>(statusDamage->GetCtrl(IDC_IGUI_DAMAGE_HYDRAULICS));
        mainRotor =               static_cast<CStatic *>(statusDamage->GetCtrl(IDC_IGUI_DAMAGE_MAINROTOR));
        tailRotor =               static_cast<CStatic *>(statusDamage->GetCtrl(IDC_IGUI_DAMAGE_TAILROTOR));
        statusAutohover =         static_cast<CStatic *>(statusDamage->GetCtrl(IDC_IGUI_DAMAGE_AUTOHOVER));
      }
      return statusDamage;
#endif
  }
  return Display::OnCreateCtrl(type, idc, cls);
}

DisplayHitZoneInfo::DisplayHitZoneInfo(ControlsContainer *parent, int idc, ParamEntryPar cls)
:CListBox(parent, idc, cls)
{

}

void DisplayHitZoneInfo::DrawItem(
 UIViewport *vp, float alpha, int i, bool selected, float top, const Rect2DFloat &rect, float widthDiff)
{
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();
  const float border = TEXT_BORDER;

  PackedColor color;
  float left = _x + border;
  int shadow = GWorld->UI()->GetHitZonesShadow();

  Texture *texture = GWorld->UI()->GetHitZonesTexture();
  if (texture)
  {
    float height = texture->AHeight();
    if (height > 0)
    {
      float width = SCALED(_rowHeight) * (texture->AWidth() * h) / (height * w);

      PackedColor pictureColor = GetFtColor(i);

      float textureU = ((GetValue(i)-1)%8)/8.0f;
      float textureV = ((GetValue(i)-1)/8)/4.0f;;

      Draw2DParsExt pars;
      pars.mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
      pars.SetColor(pictureColor);

      pars.Init();
      pars.SetU(textureU,textureU + 1/8.0f);
      pars.SetV(textureV,textureV + 1/4.0f);
      pars.spec |= (shadow==2)? UISHADOW : 0;

      vp->Draw2D
        (
        pars,
        Rect2DPixel(CX(rect.x), CY(top), CW(rect.w), CH(SCALED(_rowHeight))),
        Rect2DPixel(CX(rect.x), CY(top), CW(rect.w), CH(SCALED(_rowHeight)))
        );
      left += width + border;
    }
  }
}

DisplayWeaponInfo::DisplayWeaponInfo(ControlsContainer *parent)
: Display(parent)
{
  showCompass = false;
  SetCursor(NULL);
}

void DisplayWeaponInfo::InitControls()
{
  // InitPtr used, no NULL assignment is required
  distance = NULL;
  visionMode = NULL;
  FLIRMode = NULL;
  FOVMode = NULL;
  compass = NULL;
  heading = NULL;
  autohover = NULL;
  elevation = NULL;
  elevationText = NULL;
  GPSPlayer = NULL;
  GPSTarget = NULL;

  staticItems = NULL;
}

void DisplayWeaponInfo::Reload(ParamEntryVal clsEntry)
{
  Init();
  InitControls();
  LoadFromEntry(clsEntry);
  if(clsEntry.FindEntry("showCompass")) showCompass = clsEntry >> "showCompass";
}

Control *DisplayWeaponInfo::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
  case IDC_IGUI_WEAPON_DISTANCE:
    distance = new CStatic(this, idc, cls);
    return distance;
  case IDC_IGUI_WEAPON_VISION_MODE:
    visionMode = new CStatic(this, idc, cls);
    return visionMode;
  case IDC_IGUI_WEAPON_FLIR_MODE:
    FLIRMode = new CStatic(this, idc, cls);
    return FLIRMode;
  case IDC_IGUI_WEAPON_FOV_MODE:
    FOVMode = new CStatic(this, idc, cls);
    return FOVMode;
  case IDC_IGUI_WEAPON_COMPASS:
    compass = new CStatic(this, idc, cls);
    return compass;
  case IDC_IGUI_WEAPON_HEADING:
    heading = new CStatic(this, idc, cls);
    return heading;
  case IDC_IGUI_WEAPON_AUTOHOVER:
    autohover = new CStatic(this, idc, cls);
    return autohover;
  case IDC_IGUI_WEAPON_ELEVATION:
    elevation = new CStatic(this, idc, cls);
    return elevation;
  case IDC_IGUI_WEAPON_ELEVATION_TEXT:
    elevationText = new CStatic(this, idc, cls);
    return elevationText;
  case IDC_IGUI_WEAPON_STATIC_ITEMS:
    staticItems = new CControlsGroup(this, idc, cls);
    return staticItems;
  case IDC_IGUI_WEAPON_GPS_PLAYER:
    GPSPlayer = new CStatic(this, idc, cls);
    return GPSPlayer;
  case IDC_IGUI_WEAPON_GPS_TARGET:
    GPSTarget = new CStatic(this, idc, cls);
    return GPSTarget;
  }
  return Display::OnCreateCtrl(type, idc, cls);
}

// Hints

DisplayHint::DisplayHint(ControlsContainer *parent)
: Display(parent)
{
  SetCursor(NULL);
  LoadFromEntry(Pars >> "RscInGameUI" >> "RscHint");
}

Control *DisplayHint::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_IGHINT_BG:
    _background = ctrl;
    break;
  case IDC_IGHINT_HINT:
    ctrl->EnableCtrl(false);
    _hint = ctrl;
    break;
  }
  return ctrl;
}

RString DisplayHint::GetHint() const
{
  ITextContainer *ctrl = GetTextContainer(_hint);
  return ctrl ? ctrl->GetText() : RString();
}

void DisplayHint::SetHint(RString hint)
{
  ITextContainer *ctrl = GetTextContainer(_hint);
  if (!ctrl) return;

  ctrl->SetText(hint);
  float h = 0;
  switch (_hint->GetType())
  {
  case CT_STATIC:
    h = static_cast<CStatic *>(_hint.GetRef())->GetTextHeight();
    break;
  case CT_STRUCTURED_TEXT:
    h = static_cast<CStructuredText *>(_hint.GetRef())->GetTextHeight();
    break;
  }
  
  float dh = _background->H() - _hint->H();
  _hint->SetPos(_hint->X(), _hint->Y(), _hint->W(), h );
  _background->SetPos(_background->X(), _background->Y(), _background->W(), h + dh);
}

void DisplayHint::SetHint(INode *hint)
{
  CStructuredText *ctrl = GetStructuredText(_hint);

  ctrl->SetText(hint);
  float h = ctrl->GetTextHeight();

  float dh = _background->H() - _hint->H();
  _hint->SetPos(_hint->X(), _hint->Y(), _hint->W(), h );
  _background->SetPos(_background->X(), _background->Y(), _background->W(), h + dh);
}


void DisplayHint::SetPosition(float top)
{
  if (!_hint) return;
  float dy = _hint->Y() - _background->Y();
  _background->SetPos
  (
    _background->X(), top, _background->W(), _background->H()
  );
  _hint->SetPos
  (
    _hint->X(), top + dy, _hint->W(), _hint->H()
  );
}

//TASK hint

DisplayTaskHint::DisplayTaskHint(ControlsContainer *parent)
: Display(parent)
{
  _timeDown = Glob.uiTime; 
  _timeUp =  Glob.uiTime;
  SetCursor(NULL);
  _textureRect = Rect2DPixel(
    GEngine->Width2D() * (float)(Pars >> "RscInGameUI" >> "RscTaskHint" >> "TaskIcon" >> "x"),
    GEngine->Height2D() * (float)(Pars >> "RscInGameUI" >> "RscTaskHint" >> "TaskIcon" >> "y"),
    GEngine->Width2D() *  (float)(Pars >> "RscInGameUI" >> "RscTaskHint" >> "TaskIcon" >> "w"),
    GEngine->Height2D() * (float)(Pars >> "RscInGameUI" >> "RscTaskHint" >> "TaskIcon" >> "h")
    );
  LoadFromEntry(Pars >> "RscInGameUI" >> "RscTaskHint");

}

Control *DisplayTaskHint::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_IGTASKHINT_BG:
    _background = ctrl;
    break;
  case IDC_IGTASKHINT_HINT:
    ctrl->EnableCtrl(false);
    _taskHint = ctrl;
    break;
  }
  return ctrl;
}

RString DisplayTaskHint::GetTaskHint() const
{
  ITextContainer *ctrl = GetTextContainer(_taskHint);
  return ctrl ? ctrl->GetText() : RString();
}

void DisplayTaskHint::AddTaskHint(RString hint, PackedColor color,Ref<Texture> texture)
{
  _colorQue.Add(color);
  _textureQue.Add(texture);
  _textQue.Add(hint);

  if(_textQue.Size() == 1) SetTaskHint();
}

void DisplayTaskHint::SetTaskHint()
{
  ITextContainer *ctrl = GetTextContainer(_taskHint);
  if (!ctrl) return;
  if(_textQue.Size()==0 || _colorQue.Size()==0) return;

  _timeUp = Glob.uiTime;
  _timeDown = Glob.uiTime;

  ctrl->SetText(_textQue[0]);
  float textH = 0;
  switch (_taskHint->GetType())
  {
  case CT_STATIC:
    textH = static_cast<CStatic *>(_taskHint.GetRef())->GetTextHeight();
    break;
  case CT_STRUCTURED_TEXT:
    textH = static_cast<CStructuredText *>(_taskHint.GetRef())->GetTextHeight();
    break;
  }

  textH = floatMax(textH,_textureRect.h/(float)GEngine->Width2D()+0.02f);
  float dh = _background->H() -_taskHint->H();
  _taskHint->SetPos(_taskHint->X(), _taskHint->Y(), _taskHint->W(), textH );
  _background->SetPos(_background->X(), _background->Y(), _background->W(),textH + dh);

  ctrl->SetColor(_colorQue[0]);
}


void DisplayTaskHint::SetPosition(float top)
{
  if (!_taskHint) return;
  float dy = _taskHint->Y() - _background->Y();
  _background->SetPos
    (
    _background->X(), top, _background->W(), _background->H()
    );
  _taskHint->SetPos
    (
    _taskHint->X(), top + dy, _taskHint->W(), _taskHint->H()
    );
}

// menu attributes

// EntityAI attribute
class AttrEntityAI : public IAttribute
{
protected:
  OLink(EntityAI) _value;

public:
  void Load(RString value);
  EntityAI *GetValue() const {return _value;}
  void SetValue(EntityAI *value) {_value = value;}

  LSError Serialize(ParamArchive &ar);
  bool IsEqualTo(const IAttribute *with) const;
};

void AttrEntityAI::Load(RString value)
{
  Fail("Not implemented");
}

LSError AttrEntityAI::Serialize(ParamArchive &ar)
{
  CHECK(ar.SerializeRef("value", _value, 1));
  return LSOK;
}

bool AttrEntityAI::IsEqualTo(const IAttribute *with) const
{
  const AttrEntityAI *w = static_cast<const AttrEntityAI *>(with);
  return GetValue() == w->GetValue();
}

IAttribute *CreateAttrEntityAI() {return new AttrEntityAI();}
EntityAI *GetAttrEntityAI(const IAttribute *attr) {return static_cast<const AttrEntityAI *>(attr)->GetValue();}
void SetAttrEntityAI(IAttribute *attr, EntityAI *value) {static_cast<AttrEntityAI *>(attr)->SetValue(value);}

// Action attribute
class AttrAction : public IAttribute
{
protected:
  Ref<Action> _value;

public:
  void Load(RString value);
  Action *GetValue() const {return _value;}
  void SetValue(Action *value) {_value = value;}

  LSError Serialize(ParamArchive &ar);
  bool IsEqualTo(const IAttribute *with) const;
};

void AttrAction::Load(RString value)
{
  Fail("Not implemented");
}

LSError AttrAction::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("value", _value, 1));
  return LSOK;
}

bool AttrAction::IsEqualTo(const IAttribute *with) const
{
  const AttrAction *w = static_cast<const AttrAction *>(with);
  return GetValue() == w->GetValue();
}

IAttribute *CreateAttrAction() {return new AttrAction();}
Action *GetAttrAction(const IAttribute *attr) {return static_cast<const AttrAction *>(attr)->GetValue();}
void SetAttrAction(IAttribute *attr, Action *value) {static_cast<AttrAction *>(attr)->SetValue(value);}

// Target attribute
class AttrTarget : public IAttribute
{
protected:
  LinkTarget _value;

public:
  void Load(RString value);
  Target *GetValue() const {return _value;}
  void SetValue(Target *value) {_value = value;}

  LSError Serialize(ParamArchive &ar);
  bool IsEqualTo(const IAttribute *with) const;
};

void AttrTarget::Load(RString value)
{
  Fail("Not implemented");
}

LSError AttrTarget::Serialize(ParamArchive &ar)
{
  CHECK(ar.SerializeRef("value", _value, 1));
  return LSOK;
}

bool AttrTarget::IsEqualTo(const IAttribute *with) const
{
  const AttrTarget *w = static_cast<const AttrTarget *>(with);
  return GetValue() == w->GetValue();
}

IAttribute *CreateAttrTarget() {return new AttrTarget();}
Target *GetAttrTarget(const IAttribute *attr) {return static_cast<const AttrTarget *>(attr)->GetValue();}
void SetAttrTarget(IAttribute *attr, Target *value) {static_cast<AttrTarget *>(attr)->SetValue(value);}

#if _ENABLE_CONVERSATION
// Message attribute
class AttrMessage : public IAttribute
{
protected:
  Ref<const KBUINodeInfo> _value;

public:
  void Load(RString value);
  const KBUINodeInfo *GetValue() const {return _value;}
  void SetValue(const KBUINodeInfo *value) {_value = value;}

  LSError Serialize(ParamArchive &ar);
  bool IsEqualTo(const IAttribute *with) const;
};

void AttrMessage::Load(RString value)
{
  Fail("Not implemented");
}

LSError AttrMessage::Serialize(ParamArchive &ar)
{
  Fail("Not implemented");
  return LSOK;
}

bool AttrMessage::IsEqualTo(const IAttribute *with) const
{
  const AttrMessage *w = static_cast<const AttrMessage *>(with);
  return GetValue() == w->GetValue();
}

IAttribute *CreateAttrMessage() {return new AttrMessage();}
const KBUINodeInfo *GetAttrMessage(const IAttribute *attr) {return static_cast<const AttrMessage *>(attr)->GetValue();}
void SetAttrMessage(IAttribute *attr, const KBUINodeInfo *value) {static_cast<AttrMessage *>(attr)->SetValue(value);}

// GameValue attribute
class AttrGameValue : public IAttribute
{
protected:
  GameValue _value;

public:
  void Load(RString value);
  const GameValue &GetValue() const {return _value;}
  void SetValue(const GameValue &value) {_value = value;}

  LSError Serialize(ParamArchive &ar);
  bool IsEqualTo(const IAttribute *with) const;
};

void AttrGameValue::Load(RString value)
{
  Fail("Not implemented");
}

LSError AttrGameValue::Serialize(ParamArchive &ar)
{
  Fail("Not implemented");
  return LSOK;
}

bool AttrGameValue::IsEqualTo(const IAttribute *with) const
{
  const AttrGameValue *w = static_cast<const AttrGameValue *>(with);
  return GetValue().IsEqualTo(w->GetValue());
}

IAttribute *CreateAttrGameValue() {return new AttrGameValue();}
const GameValue &GetAttrGameValue(const IAttribute *attr) {return static_cast<const AttrGameValue *>(attr)->GetValue();}
void SetAttrGameValue(IAttribute *attr, const GameValue &value) {static_cast<AttrGameValue *>(attr)->SetValue(value);}

#endif

// menu attributes registration

AttributeTypes MenuAttributeTypes;

#if _ENABLE_DIRECT_MESSAGES && _ENABLE_CONVERSATION

#define ATTRIBUTES_LIST(XX) \
  XX("dir", attrDir, CreateAttrFloat) \
  XX("dist", attrDist, CreateAttrFloat) \
  XX("vehicle", attrVehicle, CreateAttrEntityAI) \
  XX("target", attrTarget, CreateAttrTarget) \
  XX("action", attrAction, CreateAttrAction) \
  XX("getinpos", attrGetInPos, CreateAttrInt) \
  XX("direct", attrDirect, CreateAttrBool) \
  XX("gesture", attrGesture, CreateAttrString) \
  XX("message", attrMessage, CreateAttrMessage) \
  XX("gamevalue", attrGameValue, CreateAttrGameValue) \
  XX("expression", attrExpression, CreateAttrString) \
  // name, variable, create function

#elif _ENABLE_CONVERSATION

#define ATTRIBUTES_LIST(XX) \
  XX("dir", attrDir, CreateAttrFloat) \
  XX("dist", attrDist, CreateAttrFloat) \
  XX("vehicle", attrVehicle, CreateAttrEntityAI) \
  XX("target", attrTarget, CreateAttrTarget) \
  XX("action", attrAction, CreateAttrAction) \
  XX("getinpos", attrGetInPos, CreateAttrInt) \
  XX("message", attrMessage, CreateAttrMessage) \
  XX("gamevalue", attrGameValue, CreateAttrGameValue) \
  XX("expression", attrExpression, CreateAttrString) \
  // name, variable, create function

#else

#define ATTRIBUTES_LIST(XX) \
  XX("dir", attrDir, CreateAttrFloat) \
  XX("dist", attrDist, CreateAttrFloat) \
  XX("vehicle", attrVehicle, CreateAttrEntityAI) \
  XX("target", attrTarget, CreateAttrTarget) \
  XX("action", attrAction, CreateAttrAction) \
  XX("getinpos", attrGetInPos, CreateAttrInt) \
  XX("expression", attrExpression, CreateAttrString) \
  // name, variable, create function

#endif


#define DEFINE_ATTR_VARIABLE(name, variable, func) static int variable;
ATTRIBUTES_LIST(DEFINE_ATTR_VARIABLE);

#define REGISTER_ATTR(name, variable, func) \
  variable = MenuAttributeTypes.attributes.AddValue(name); \
  MenuAttributeTypes.createFunctions.Access(variable); \
  MenuAttributeTypes.createFunctions[variable] = func;
  
#include <El/Modules/modules.hpp>

INIT_MODULE(InGameUI, 4)
{
  // Register attributes
  ATTRIBUTES_LIST(REGISTER_ATTR);
  MenuAttributeTypes.attributes.Close();
};

void SetMenuAttribute(MenuItem *item, RString name, RString value)
{
  item->SetAttribute(name, value, MenuAttributeTypes);
}

void SetMenuAttrBool(MenuItem *item, int index, bool value)
{
  IAttribute *attr = item->AccessAttribute(index, MenuAttributeTypes);
  ::SetAttrBool(attr, value);
}

void SetMenuAttrInt(MenuItem *item, int index, int value)
{
  IAttribute *attr = item->AccessAttribute(index, MenuAttributeTypes);
  ::SetAttrInt(attr, value);
}

void SetMenuAttrFloat(MenuItem *item, int index, float value)
{
  IAttribute *attr = item->AccessAttribute(index, MenuAttributeTypes);
  ::SetAttrFloat(attr, value);
}

void SetMenuAttrString(MenuItem *item, int index, RString value)
{
  IAttribute *attr = item->AccessAttribute(index, MenuAttributeTypes);
  ::SetAttrString(attr, value);
}

void SetMenuAttrEntityAI(MenuItem *item, int index, EntityAI *value)
{
  IAttribute *attr = item->AccessAttribute(index, MenuAttributeTypes);
  ::SetAttrEntityAI(attr, value);
}

void SetMenuAttrAction(MenuItem *item, int index, Action *value)
{
  IAttribute *attr = item->AccessAttribute(index, MenuAttributeTypes);
  ::SetAttrAction(attr, value);
}

void SetMenuAttrTarget(MenuItem *item, int index, Target *value)
{
  IAttribute *attr = item->AccessAttribute(index, MenuAttributeTypes);
  ::SetAttrTarget(attr, value);
}

#if _ENABLE_CONVERSATION

void SetMenuAttrMessage(MenuItem *item, int index, const KBUINodeInfo *value)
{
  IAttribute *attr = item->AccessAttribute(index, MenuAttributeTypes);
  ::SetAttrMessage(attr, value);
}

void SetMenuAttrGameValue(MenuItem *item, int index, const GameValue &value)
{
  IAttribute *attr = item->AccessAttribute(index, MenuAttributeTypes);
  ::SetAttrGameValue(attr, value);
}
#endif

/// list of all menu conditions (simple expression variables)
#define MENU_CONDITION_VARIABLES(XXX) \
  XXX(HasRadio) \
  XXX(IsAloneInVehicle) \
  XXX(IsCommander) \
  XXX(VehicleCommander) \
  XXX(CommandsToGunner) \
  XXX(CommandsToPilot) \
  XXX(Multiplayer) \
  XXX(IsWatchCommanded) \
  XXX(IsSelectedToAdd) \
  XXX(PlayerVehicleCanGetIn) \
  XXX(IsXbox) \
  XXX(PlayerOwnRadio) \

/// Variable names
#define MENU_CONDITION_VAR_NAME(name) #name,

static const RString MenuConditionVariables[]=
{
  MENU_CONDITION_VARIABLES(MENU_CONDITION_VAR_NAME)
};

/// Variable values
#define MENU_CONDITION_VAR_VALUE(name) var##name ? 1.0f : 0.0f,

InGameUI::InGameUI()
:_weaponCursorScaleCoef(1.0f), _throwCursorMinScale(1.0f), _throwCursorMaxScale(2.0f), 
_throwCursorFadeSpeed(2.0f)
{
#if _ENABLE_CHEATS
  _showAll=false;
#endif

#if SPEEREO_SPEECH
  _idSpeechEngine = AddRegisterApplication((HWND)CurrentAppInfoFunctions->GetAppHWnd());
#endif

  _modelCursor=VForward;
  //_camGroupPos=VForward;
  _camGroupHeading = 0;
  _camGroupDive = _camGroupDiveOld = 0;
  _camGroupZoom = 1;
  _camGroupCursor = Point2DFloat(0,0);

  #if PROTECTION_ENABLED && !CHECK_FADE && !ALWAYS_FAIL_CRC
    // random time before message is shown
    _timeToPlay = Glob.uiTime+10*60+GRandGen.RandomValue()*(50*60);
  #else
    _timeToPlay = Glob.uiTime+GRandGen.PlusMinus(60,30);
  #endif

  _target = NULL;
//  _curWeapon = 0;
  
  _blinkState = false;
  _blinkStateChange = UITime(0);

  _weaponInfoAlpha = -1.0f;

#if _HELISIM_PRECISE_INTERACTION
  _textTipDelayTime = UITIME_MAX;
#endif
  Init();

#if _VBS3_LASE_LEAD
  _laserRange = new DisplayLaserRange(NULL);
#endif

  _hint = new DisplayHint(NULL);
  _hint->SetHint("");
  _hintTime = Glob.uiTime;

  _taskHint = new DisplayTaskHint(NULL);
  _taskHint->ClearHints();

/*
  _font24=GLOB_ENGINE->LoadFont(GetFontID(FontS));
  _font36=GLOB_ENGINE->LoadFont(GetFontID(FontM));
*/

  ProgressRefresh();

  ParamEntryVal mainCfg = Pars >> "CfgInGameUI";

  _xboxStyle = false;
  ConstParamEntryPtr entry = mainCfg.FindEntry("xboxStyle");
  if (entry) _xboxStyle = *entry;
  
  bgColor = GetPackedColor(mainCfg >> "colorBackground");
  bgColorCmd = GetPackedColor(mainCfg >> "colorBackgroundCommand");
  bgColorHelp = GetPackedColor(mainCfg >> "colorBackgroundHelp");
  ftColor = GetPackedColor(mainCfg >> "colorText");

  {
    ParamEntryVal cfg = mainCfg >> "Capture";
    capBgColor = GetPackedColor(cfg >> "colorBackground");
    capFtColor = GetPackedColor(cfg >> "colorText");
    capLnColor = GetPackedColor(cfg >> "colorLine");
    capShadow = cfg >> "shadow";
  }
  ProgressRefresh();


  {
    ParamEntryVal cfg = mainCfg >> "TankDirection";

    tankX = cfg >> "left";
    tankY = cfg >> "top";
    tankW = cfg >> "width";
    tankH = cfg >> "height";

    tankColor = GetPackedColor(cfg >> "color");
    tankColorFullDammage = GetPackedColor(cfg >> "colorFullDammage");
    tankColorHalfDammage = GetPackedColor(cfg >> "colorHalfDammage");
    tankShadow = cfg >> "shadow";

    _imageTurret = GlobLoadTextureUI(GetPictureName(cfg >> "imageTurret"));
    _imageGun = GlobLoadTextureUI(GetPictureName(cfg >> "imageGun"));
    _imageObsTurret = GlobLoadTextureUI(GetPictureName(cfg >> "imageObsTurret"));

    _imageHull = GlobLoadTextureUI(GetPictureName(cfg >> "imageHull"));
    _imageEngine = GlobLoadTextureUI(GetPictureName(cfg >> "imageEngine"));

    _imageLTrack = GlobLoadTextureUI(GetPictureName(cfg >> "imageLTrack"));
    _imageRTrack = GlobLoadTextureUI(GetPictureName(cfg >> "imageRTrack"));
  }

#if _HELISIM_ROTORLIB
  {
    ParamEntryVal cfg = mainCfg >> "HelicopterHUD";
    _helicopterHUDShadow = cfg >> "shadow";

    _helicopterHUDfontSizeNormal = cfg >> "SizeExNormal";
    _helicopterHUDfontSizeSmall = cfg >> "SizeExSmall";
    _helicopterHUDtextFont = GEngine->LoadFont(GetFontID(cfg >> "font"));

    _helicopterHUDColor = GetPackedColor(cfg >> "color");
    _windDirection = GlobLoadTexture(GetPictureName(cfg >> "windDirection"));
    _stabilityGrid = GlobLoadTexture(GetPictureName(cfg >> "stabilityGrid"));
    _stabilityVector = GlobLoadTexture(GetPictureName(cfg >> "stabilityVector"));
    _indicator = GlobLoadTexture(GetPictureName(cfg >> "indicator"));
    _helicopterHUDAxis = GlobLoadTexture(GetPictureName(cfg >> "axis"));

    _horizonAxes = GlobLoadTexture(GetPictureName(cfg >> "horizonAxes"));
    _horizonVector = GlobLoadTexture(GetPictureName(cfg >> "horizonVector"));
    _horizonAircraft = GlobLoadTexture(GetPictureName(cfg >> "horizonAircraft"));
    _attitudeElemmentsSize = (cfg >> "attitudeElemmentsSize");
    _attitudeFOV = (cfg >> "attitudeFOV");

    _statusOff = GetPackedColor(cfg >> "statusOff");
    _statusOn = GetPackedColor(cfg >> "statusOn");
    _statusDamaged = GetPackedColor(cfg >> "statusDamaged");
    _statusDestroyed = GetPackedColor(cfg >> "statusDestroyed");
    _HUDLimitsColor = GetPackedColor(cfg >> "HUDLimitsColor");

    _analogueSpeedAngleOffset = (cfg >> "analogueSpeedAngleOffset");
    _analogueAltitudeAngleOffset = (cfg >> "analogueAltitudeAngleOffset");
    _analogueSpeedAngleRange = (cfg >> "analogueSpeedAngleRange");
    _analogueAltitudeAngleRange = (cfg >> "analogueAltitudeAngleRange");
  }
#endif

  {
    ParamEntryVal cfg = mainCfg >> "FLIRModeNames";
    
      int n = std::min((cfg >> "FLIRModeName").GetSize(), (int)sizeof(_FLIRModeNames));

      for (int i=0; i<n; i++)
        _FLIRModeNames[i] = (cfg >> "FLIRModeName")[i];   
  }

  ProgressRefresh();

  {
    ParamEntryVal cfg = mainCfg >> "Compass";
    coX = cfg >> "left";
    coY = cfg >> "top";
    coW = cfg >> "width";
    coH = cfg >> "height";
    coShadow = cfg >> "shadow";
    compassColor = GetPackedColor(cfg >> "color");
    
    compassDirColor = GetPackedColor(cfg >> "dirColor");
    compassTurretDirColor = GetPackedColor(cfg >> "turretDirColor");
  }
  
  {
    ParamEntryVal cfg = mainCfg >> "PlayerInfo";
    piY = cfg >> "top";
    piDimStartTime = cfg >> "dimmStartTime";
    piDimEndTime = cfg >> "dimmEndTime";
    stallWarning = cfg >> "HealthBar" >> "stallWarning";
  }

  {
    ParamEntryVal cfg = mainCfg >> "Cursor";
    actW = cfg >> "activeWidth";
    actH = cfg >> "activeHeight";
    actMin = cfg >> "activeMinimum";
    actMax = cfg >> "activeMaximum";
    cursorColor = GetPackedColor(cfg >> "color");
    cursorBgColor = GetPackedColor(cfg >> "colorBackground");
    cursorDim = cfg >> "dimm";
    cursorLockColor = GetPackedColor(cfg >> "colorLocked");
    cursorShadow = cfg >> "shadow";
    cursorInfoShadow= cfg >> "infoTextShadow";
    _iconMission = GLOB_ENGINE->TextBank()->Load
    (
      GetPictureName(cfg >> "mission")
    );
    _iconMissionMaxFade = cfg >> "missionFade";

    _iconCustomMark = GEngine->TextBank()->Load
    (
      GetPictureName(cfg >> "customMark")
    );

    ProgressRefresh();

    missionColor1 = GetPackedColor(cfg >> "missionColor1");
    missionColor2 = GetPackedColor(cfg >> "missionColor2");
    customMarkColor = GetPackedColor(cfg >> "customMarkColor");
    float blinkingPeriod = cfg >> "blinkingPeriod";
    blinkingSpeed = blinkingPeriod > 0 ? 1.0f / blinkingPeriod : 0;

    freeLookActionColor = GetPackedColor(cfg >> "freeLookActionColor");
    freeLookActionColorSelected = GetPackedColor(cfg >> "freeLookActionColorSelected");
    freeLookActionColorAvailable = GetPackedColor(cfg >> "freeLookActionColorAvailable");

    cmdDimStartTime = cfg >> "dimmCmdStartTime";
    cmdDimEndTime = cfg >> "dimmCmdEndTime";

    // TODO: read from the right place
    targetDimStartTime = cfg >> "dimmCmdStartTime";
    targetDimEndTime = cfg >> "dimmCmdEndTime";

    _cursorFont = GEngine->LoadFont(GetFontID(cfg >> "font"));
    _cursorSize = cfg >> "size";

    _throwCursorMinScale = cfg >> "throwCursorMinScale";
    _throwCursorMaxScale = cfg >> "throwCursorMaxScale";
    _throwCursorFadeSpeed = cfg >> "throwCursorFadeSpeed";
  }

  {
    ParamEntryVal cfg = mainCfg >> "Bar";
    barBgColor = GetPackedColor(cfg >> "colorBackground");
    barGreenColor = GetPackedColor(cfg >> "colorGreen");
    barYellowColor = GetPackedColor(cfg >> "colorYellow");
    barRedColor = GetPackedColor(cfg >> "colorRed");
    barBlinkOnColor = GetPackedColor(cfg >> "colorBlinkOn");
    barBlinkOffColor = GetPackedColor(cfg >> "colorBlinkOff");
  }

  {
    ParamEntryVal cfg = mainCfg >> "Hint";
    hintDimStartTime = cfg >> "dimmStartTime";
    hintDimEndTime = cfg >> "dimmEndTime";
    GetValue(_hintSound, cfg >> "sound");
  }

  {
    ParamEntryVal cfg = mainCfg >> "TaskHint";
    taskHintDimStartTime = cfg >> "dimmStartTime";
    taskHintDimEndTime = cfg >> "dimmEndTime";
    taskHintDimShowTime = cfg >> "dimShowTime";
  }

  {
    ParamEntryVal cfg = mainCfg >> "ConnectionLost";

    _clX = cfg >> "left";
    _clY = cfg >> "top";
    _clW = cfg >> "width";
    _clH = cfg >> "height";
    _clFont = GEngine->LoadFont(GetFontID(cfg >> "font"));
    _clSize = cfg >> "size";
    _clColor = GetPackedColor(cfg >> "color");
    _clShadow = cfg >> "shadow";
  }

  {
    ParamEntryVal perVision = mainCfg >> "PeripheralVision";
    
    _cueTexture = GlobLoadTextureUI(GetPictureName(perVision>>"cueTexture"));
    _cueColor = GetPackedColor(perVision>>"cueColor");
    _cueEnemyColor = GetPackedColor(perVision>>"cueEnemyColor");
    _cueFriendlyColor = GetPackedColor(perVision>>"cueFriendlyColor");
    _cueShadow = perVision>>"shadow";

    _bloodTexture = GlobLoadTextureUI(GetPictureName(perVision>>"bloodTexture"));
    _bloodColor = GetPackedColor(perVision>>"bloodColor");
  }

  if(mainCfg.FindEntry("HitZones"))
  {
    ParamEntryVal hitZones = mainCfg >> "HitZones";
    _hitZones = GlobLoadTextureUI(GetPictureName(hitZones >> "hitZonesTexture"));
    _hitShadow =  hitZones >> "Shadow";
  }

  ProgressRefresh();

  {
    ParamEntryVal cfg = Pars >> "RscInGameUI";
    _weaponReadyColor = GetPackedColor(cfg >> "colorReady");
    _weaponPrepareColor = GetPackedColor(cfg >> "colorPrepare");
    _weaponUnloadColor = GetPackedColor(cfg >> "colorUnload");
  }

#if PROTECTION_ENABLED
  ParamEntryVal fontPars = Pars >> "CfgInGameUI" >> "FadeFont";
  _fadeFont = GEngine->LoadFont(GetFontID(fontPars >> "font"));
  _fadeSize = fontPars >> "size";
  _fadeShadow = fontPars >> "shadow";
#endif

  UIActions::Load(Pars >> "CfgActions");

#if _VBS3_RANKS
  GVBSVisuals.LoadRanks(Pars >> "CfgRanks");
#endif
}

InGameUI::~InGameUI()
{
#if SPEEREO_SPEECH
  UnregisterApplication(_idSpeechEngine);
#endif
}

#define SPEERREO_WORD_ENUM(XX) \
  XX("White", SWWhite) \
  XX("Red", SWRed) \
  XX("Green", SWGreen) \
  XX("Blue", SWBlue) \
  XX("Yellow", SWYellow)
/*
  XX("Forward", SWForward) \
  XX("Reverse", SWReverse) \
  XX("Fast", SWFast) \
  XX("Slow", SWSlow) \
  XX("Left", SWLeft) \
  XX("Right", SWRight) \
  XX("Fire", SWFire)
*/
/*
  XX("Column", SWColumn) \
  XX("Stagered Column", SWStageredColumn) \
  XX("Wedge", SWWedge) \
  XX("Echelon Left", SWEchelonLeft) \
  XX("Echelon Right", SWEchelonRight) \
  XX("Vee", SWVee) \
  XX("Line", SWLine)
*/
  /*
  XX("All", SWAll) \
  XX("One", SWOne) \
  XX("Two", SWTwo) \
  XX("Three", SWThree) \
  XX("Four", SWFour) \
  XX("Five", SWFive)
  XX("Six", SWSix) \
  XX("Seven", SWSeven) \
  XX("Eight", SWEight) \
  XX("Nine", SWNine) \
  XX("Ten", SWTen) \
  XX("Eleven", SWEleven) \
  XX("Twelve", SWTwelve) \
  */
  /*
  XX("Switch To", SWSwitchTo) \
  XX("Form", SWForm) \
  XX("At Ease", SWAtEase) \
  XX("Stay Alert", SWStayAlert) \
  XX("Danger", SWDanger) \
  XX("Fire At Will", SWFireAtWill) \
  XX("Go Go Go", SWGoGoGo) \
  XX("Hold Fire", SWHoldFire) \
  XX("Engage At Will", SWEngageAtWill) \
  XX("Get Down", SWGetDown) \
  XX("Stand Up", SWStandUp) \
  XX("Keep Low", SWKeepLow) \
  XX("Advance", SWAdvance) \
  XX("Stay Back", SWStayBack) \
  XX("Flank Left", SWFlankLeft) \
  XX("Flank Right", SWFlankRight) \
  XX("Keep Formation", SWKeepFormation) \
  XX("Report Status", SWReportStatus) \
  XX("Return To Formation", SWReturnToFormation) \
  XX("Get In", SWGetIn) \
  XX("Disembark", SWDisembark) \
  XX("Stop", SWStop) \
  XX("Take Cover", SWTakeCover) \
  */

#define SW_VALUE(name,value) \
  value,

#define SW_NAME(name,value) \
  EnumName(value,name),

enum SpeerreoWord
{
  SPEERREO_WORD_ENUM(SW_VALUE)
  NSpeerreoWord
};

static const EnumName SpeerreoWordNames[] =
{
  SPEERREO_WORD_ENUM(SW_NAME)
  EnumName()
};

template <>
const EnumName *GetEnumNames(SpeerreoWord dummy) {return SpeerreoWordNames;}

LONG InGameUI::ProcessUserMessage(UINT msg, WORD wParam, DWORD lParam)
{
#if SPEEREO_SPEECH
  switch (msg)
  {
  case WM_SRT_CHECKKEY:
    return GenStartKey(wParam, lParam);
  case WM_SRT_MAKEHYPO:
    for (int i=0; i<NSpeerreoWord; i++)
      AddPhrase(SpeerreoWordNames[i].name, SpeerreoWordNames[i].value);
    return NSpeerreoWord;
  case WM_SRT_ACCEPTHYPO:
    DIAG_MESSAGE(500, SpeerreoWordNames[wParam].name);
    return TRUE;
  }

  HWND hwnd = CurrentAppInfoFunctions->GetAppHWnd();
  if (IsWindowUnicode(hwnd))
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  else
    return DefWindowProc(hwnd, msg, wParam, lParam);
#endif
  return 0;
}

AbstractUI *CreateInGameUI() {return new InGameUI;}

/*
const char *InGameUI::GetMissionEnd()
{
  if (_missionEnd && _missionEnd->_title)
  {
    RString text = _missionEnd->_title->GetText();
    if ( text.GetLength() > 0)
      return text;
  }
  return NULL;
}

void InGameUI::SetMissionEnd(const char *text, int colorIndex)
{
  if (_missionEnd && _missionEnd->_title)
  {
    _missionEnd->_title->SetText(text);
    switch (colorIndex)
    {
    case 0:
      _missionEnd->_title->SetFtColor(unknownColor);
      break;
    case 1:
      _missionEnd->_title->SetFtColor(friendlyColor);
      break;
    case 2:
      _missionEnd->_title->SetFtColor(enemyColor);
      break;
    }
  }
}
*/

void AbstractUI::ShowAll(bool show)
{
  _showUnitInfo = show;
  _showCompass = show;
  _showTankDirection = show;
}

bool AbstractUI::IsHUDShown() const
{
  return _showCursors;
}

bool IsHUDShown()
{
  if (!GWorld) return true;
  AbstractUI *ui = GWorld->UI();
  if (!ui) return true;
  return ui->IsHUDShown();
}

void InGameUI::Init()
{
  ShowAll();
  _showHUD = true;
  ShowCursors();

  void ShowShortcuts();
  ShowShortcuts();

  _actions.Clear();

  _iguiHMD = false; 
}


void InGameUI::ResetVehicle( EntityAIFull *vehicle )
{
  AIBrain *unit = GWorld->FocusOn();
  Person *player = unit ? unit->GetPerson() : NULL;

  TurretContext context;
  bool valid = vehicle->FindTurret(player, context);

  if (valid)
  {
    WeaponModePtr ptr = context._weapons->FirstWeapon(vehicle);
    ptr = context._weapons->ValidateWeapon(ptr);
    // if the weapon is not weapon, but rather special item, do not select it
    if (ptr.IsValid())
    {
      const WeaponType *type = ptr.GetWeaponType();
      if (
        !(type->_weaponType & MaskSlotPrimary) &&
        (type->_weaponType & (MaskSlotBinocular | MaskSlotSecondary | MaskSlotInventory)))
      {
        // do not autoselect binocular, secondary weapon or inventory
        ptr = WeaponModePtr();
      }
    }
    if (GLOB_WORLD->PlayerManual())
    {
      context._weapons->SelectWeapon(vehicle, ptr, true);
    }
    context._weapons->FirstCM(vehicle);
  }

//LogF("Select weapon %d (ResetVehicle)", weapon);

  if( vehicle==_target.IdExact() ) _target=NULL;

  _modelCursor=VForward;

/*
  // commander should always use eye direction
  Vector3 dir;
  if (!valid)
    dir = vehicle->Direction();
  else if (unit && unit == vehicle->ObserverUnit())
    dir = vehicle->GetEyeDirection(context);
  else if (unit && unit == vehicle->GunnerUnit())
    dir = vehicle->GetWeaponDirection(context, 0);
  else
    dir = vehicle->Direction();
  SetCursorDirection(dir);
*/

//  _timeSendLoad = UITIME_MAX;
}

void InGameUI::ResetHUD()
{
  _target = NULL;
  _iguiHMD = false;

  _hint->SetHint("");
  _taskHint->ClearHints();

  _actions.Clear();
}

void InGameUI::ResetUnitInfo()
{
  _lastUnitInfos.Clear();
}

/*!
\patch 5129 Date 2/20/2007 by Jirka
- Fixed: Display all (fading) HUD elements when mission is restarted
*/

void InGameUI::InitMission()
{
  _lastUnitInfos.Clear();
  _lastWeaponInfos.Clear();
}

Target *InGameUI::GetCursorTarget() const
{
  return _target;
}


Object *InGameUI::GetCursorObject() const
{
  return _cursorObject;
}

RString InGameUI::GetCursorSurface() const
{
  return _cursorSurface;
}


/// comparison for radar target sorting
/*!
\patch 5149 Date 3/26/2007 by Ondra
- Fixed: Improved target Tab locking prioritization for aircraft.
*/

class CmpRadarTarget
{
  EntityAIFull *_me;
  AIBrain *_agent;
  Target *_target;
  /// how much do we prefer some direction
  Vector3 _dirPrefer;
  
  public:
  CmpRadarTarget(EntityAIFull *me, AIBrain *agent, Target *target, Vector3Par dirPrefer)
  :_me(me), _agent(agent), _target(target), _dirPrefer(dirPrefer)
  {
  }
  int operator () (const Ref<Target> *t1, const Ref<Target> *t2) const
  {
    const Target *tgt1 = *t1;
    const Target *tgt2 = *t2;
    // preferred target is first
    if (tgt1 == _target) return -1;
    if (tgt2 == _target) return +1;
    // sort by cost
    Vector3 tgt1Dir = tgt1->GetPosition() - _me->FutureVisualState().Position();
    Vector3 tgt2Dir = tgt2->GetPosition() - _me->FutureVisualState().Position();
    tgt1Dir.Normalize();
    tgt2Dir.Normalize();
    // common situation is when there is no preference - in such situation the dot is always zero
    float dir1Factor = _dirPrefer*tgt1Dir+1.0f;
    float dir2Factor = _dirPrefer*tgt2Dir+1.0f;
    float cost1 = floatMax(tgt1->GetType()->GetCost(),tgt1->GetSubjectiveCost())*dir1Factor;
    float cost2 = floatMax(tgt2->GetType()->GetCost(),tgt2->GetSubjectiveCost())*dir2Factor;
    if (cost1>cost2) return -1;
    if (cost1<cost2) return +1;
    // keep sort order stable
    return ComparePointerAddresses(t1,t2);
  }
};


#define CameraFrame() GScene->GetCamera()

/*!
\patch 5260 Date 7/30/2008 by Bebul
- Fixed: Dedicated server crashed when script made soldier fire with Binocular.
*/

Vector3 InGameUI::GetCursorDirection() const
{
  AIBrain *unit = GWorld->FocusOn();
  if(unit && GWorld->GetCameraType() != CamGroup)
  {
    const Camera *camera = GScene->GetCamera();
    if(camera)
    {
      if(!unit->IsFreeSoldier()) return camera->Direction(); 
      if(GWorld->GetCameraType() == CamGunner) return camera->Direction(); 
    }
  }

  Vector3 dir;
  const FrameBase *cam = CameraFrame();
  if (cam)
  {
    dir = cam->DirectionModelToWorld(_modelCursor);
  }
  else
  {
    Fail("No camera");
    dir = _modelCursor;
  }
  if (GWorld && GWorld->GetCameraType() != CamGroup)
  {
    Object *camObject = GWorld->CameraOn();
    if (camObject)
    { 
      camObject->OverrideCursor(dir);
    }
    else dir = _modelCursor; // there was fail on dedicated server with camObject==NULL
  }
  return dir;
}

/**
@param dir world space
*/
void InGameUI::SetCursorDirection( Vector3Par dir )
{
  const FrameBase *cam=CameraFrame();
  if( cam )
  {
    Matrix4Val invTransform=cam->GetInvTransform();
    _modelCursor=invTransform.Rotate(dir);
  }
}

/// Functor checking for free gunner positions
class CheckFreeTurrets : public ITurretFunc
{
protected:
  bool &_freeCommander;
  bool &_freeGunner;

public:
  CheckFreeTurrets(bool &freeCommander, bool &freeGunner):
    _freeCommander(freeCommander), _freeGunner(freeGunner) {}

  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turret) return false; // continue
    Transport *veh = static_cast<Transport *>(entity); // safe here, we know how we called it (+ turret exist)
    if (veh->QCanIGetInTurret(context._turret))
    {
      if (context._turretType->_primaryObserver)
        _freeCommander = true;
      else if (context._turretType->_primaryGunner)
        _freeGunner = true;
      else if (!context._weapons->HasWeapons())
        _freeCommander = true;
      else
        _freeGunner = true;
    }
    return _freeCommander && _freeGunner; // end when both observer and gunner position found
  }
};


void InGameUI::ShowWeaponInfo()
{
  for (int i=0; i<_lastUnitInfos.Size(); i++)
  { //if Menu with weapon is hidden, first show menu
    UnitInfoDesc &desc = _lastUnitInfos[i];
    if (!desc._display) continue;
    DisplayUnitInfo *disp = desc._display;

    if(disp->ammo)
    {
      desc._changed = Glob.uiTime;
    }
  }
}

#if _ENABLE_CONVERSATION

/// 
static RadioChannel *GetRadioChannel(AIBrain *askingUnit, AIBrain *askedUnit, ChatChannel chan)
{
  Person *askingPerson = askingUnit->GetPerson();
  Person *askedPerson = askedUnit->GetPerson();
  Transport *askingVehicle = askingUnit->GetVehicleIn();
  Transport *askedVehicle = askedUnit->GetVehicleIn();

  switch (chan)
  {
  case CCNone:
    return NULL;
  case CCGlobal:
    return &GWorld->GetRadio();    
  case CCDirect:
    return askingPerson->GetRadio();
  case CCVehicle:
    if (askingVehicle && askingVehicle == askedVehicle)
    {
      // both persons are in the same vehicle, use the vehicle channel
      return &askingVehicle->GetRadio();
    }
    break;
  }
  if (chan>=CCCustom1 && chan<=CCCustomLast)
  {
    if (askingPerson!=NULL && askedPerson!=NULL)
    {
      CustomChannelInfo &info = GCustomChannels.GetCustomChannelInfo(chan);
      // both players participate in this custom channel, use it
      if (info.CheckUnit(askingPerson) && info.CheckUnit(askedPerson))
      {
        return &GWorld->GetCustomRadio(chan-CCCustom1);
      }
    }
  }
 
  return NULL;
}

/// more advanced channel selection (based on the units in the conversation)
static RadioChannel *SelectRadioChannel(AIBrain *askingUnit, AIBrain *askedUnit, bool forceRadio, ChatChannel chan)
{
  if (forceRadio && chan!=CCNone)
  {
    RadioChannel *rchan = GetRadioChannel(askingUnit, askedUnit, chan);
    if (rchan) return rchan;
  }

  const float MaxDirectDistance = 20.0f;

  Person *askingPerson = askingUnit->GetPerson();
  Person *askedPerson = askedUnit->GetPerson();
  Transport *askingVehicle = askingUnit->GetVehicleIn();
  Transport *askedVehicle = askedUnit->GetVehicleIn();

  if (askingVehicle && askingVehicle == askedVehicle)
  {
    // 1. if both persons are in the same vehicle and the same compartment and the radio is not forced, use the direct channel
    if (!forceRadio && (askingVehicle->GetPersonCompartments(askingPerson) & askingVehicle->GetPersonCompartments(askedPerson)) != 0)
    {
      return askingPerson->GetRadio();
    }
    // 2. if both persons are in the same vehicle, use the vehicle channel
    return &askingVehicle->GetRadio();
  }

  // 3. if both persons are outside vehicles, radio is not forced and the distance is lower than 20 m, use the direct channel
  if (!forceRadio && !askingVehicle && !askedVehicle &&
    askingPerson->WorldPosition(askingPerson->FutureVisualState()).Distance2(askedPerson->WorldPosition(askedPerson->FutureVisualState())) < Square(MaxDirectDistance))
  {
    return askingPerson->GetRadio();
  }

  // 7. otherwise, use the global channel
  return &GWorld->GetRadio();
}

/// tell something to someone (verbal communication)
void KBTell(AIBrain *askingUnit, AIBrain *askedUnit, const KBMessageInfo *info, bool forceRadio = false, ChatChannel chan=CCNone)
{
  if (!askingUnit || !askingUnit->LSCanSpeak() || !askingUnit->GetKBCenter()) return;
  if (!askedUnit || !askedUnit->LSIsAlive()) return;

  Ref<RadioMessage> msg = new RadioMessageTalk(info, askingUnit, askedUnit);
  RadioChannel *radio = SelectRadioChannel(askingUnit, askedUnit, forceRadio, chan);
  if (radio) radio->Transmit(msg);
}
#endif

/// helper function which will find the candidate for the next waypoint
static bool FindNextWaypoint(Vector3 &result, AIBrain *agent)
{
#if _ENABLE_IDENTITIES
  // find criteria when the position is good enough
  EntityAIFull *veh = agent->GetVehicle();
  if (!veh) return false;
  Vector3Val myPos = veh->FutureVisualState().Position();
  float prec2 = Square(veh->GetPrecision());

  Person *person = agent->GetPerson();
  if (!person) return false;
  const Identity &identity = agent->GetPerson()->GetIdentity();

  // second, the custom mark position
  if (identity.GetCustomMark(result))
  {
    if (myPos.DistanceXZ2(result) > prec2) return true; // far enough
  }

  // third, the current task position
  const Task *task = identity.GetCurrentTask();
  if (task && !task->IsCompleted() && task->GetDestination(result))
  {
    if (myPos.DistanceXZ2(result) > prec2) return true; // far enough
  }
#endif

  // not found
  return false;
}

/// some actions only can be performed when player have control over camera vehicle
static bool IsManual( EntityAI *veh, AIBrain *unit )
{
  if( !GWorld->PlayerManual() ) return false;
  if( GWorld->GetPlayerSuspended() ) return false;
  return true;
}

void InGameUI::RevealTarget( Target *target, float spot )
{
  AIBrain *agent = GWorld->FocusOn();
  if (!agent) return;
  EntityAI *vehicle = agent->GetVehicle();
  if (!vehicle) return;

  if (target->IsMinimal()) return;
  
  TargetNormal *tgt = static_cast<TargetNormal *>(target);
  if
  (
    !tgt->IsKnownNoDelay() ||
    tgt->delay>Glob.time+1 ||
    tgt->FadingSpotability()<spot
  )
  {
    // report only targets that are not known

    const SideInfo *side = agent->GetSideInfo();
    if (side)
    {
      TargetList *list = agent->AccessTargetList();
      tgt->Reveal(*side,list,1.5f);

      if (tgt->delay>Glob.time+1) tgt->delay=Glob.time+1;
      tgt->delaySensor=Glob.time-5;
      tgt->idSensor = agent;
      tgt->lastSeen = Glob.time;
      // use current properties
      EntityAI *exact=tgt->idExact;
      tgt->position=exact->AimingPosition(exact->FutureVisualState());
      tgt->speed=exact->FutureVisualState().Speed();
      tgt->LimitError(0.1f,0.1f);
      if( tgt->FadingSpotability()<spot )
      {
        tgt->spotability=spot;
        tgt->spotabilityTime=Glob.time;
      }
    }
  }
}

/**
@param itPos position of the collision with the target (world coordinates)
 or the nearest point on the line camera-cursor to the target
*/

Target *InGameUI::CheckCursorTarget(
  Vector3 &itPos, Vector3Par cursorDir,
  const Camera &camera, CameraType cam, bool knownOnly, bool includeBuildings
)
{
  _cursorObject = NULL;
  
  AIBrain *agent = GWorld->FocusOn();
  if (!agent) return NULL;
  EntityAI *vehicle = agent->GetVehicle();

  float maxVisDist=floatMax(vehicle->GetType()->GetIRScanRange(),TACTICAL_VISIBILITY);

  CollisionBuffer retVal;
  // view test is not available for soldiers - use fire as a secondary test
  GLandscape->ObjectCollisionLine(
    vehicle->GetRenderVisualStateAge(),retVal,Landscape::FilterIgnoreOne(vehicle),
    camera.Position(),camera.Position()+cursorDir*maxVisDist,0,
    ObjIntersectView,ObjIntersectFire
  );
  // select first intersected object
  int minI = -1;
  float minT = 1e10;
  for (int i=0; i<retVal.Size(); i++)
  {
    const CollisionInfo &info = retVal[i];
    if (!info.object) continue;
    if (info.under<minT)
    {
      minT = info.under;
      minI = i;
    }
  }
  Target *iTarget = NULL;

  itPos = camera.Position();

  #if 1
  if (minI>=0)
  { 
    // check intersection with land
    const CollisionInfo &info = retVal[minI];

    if (CHECK_DIAG(DEModel))
    {
      Assert(info.object);
      LODShape *shape = info.object->GetShape();
      if (shape)
      {
        RString modelName = Format(
          "%d: %s",
          safe_cast<int>(info.object->ID()), cc_cast(shape->GetName())
        );
        const EntityType *type = info.object->GetEntityType();
        if (type)
        {
          modelName = modelName + Format(" (type=%s)",cc_cast(type->GetName()));
        }
        if (shape->GetPropertyClass().GetLength()>0)
        {
          modelName = modelName + Format(" (class=%s)",cc_cast(shape->GetPropertyClass()));
        }
        DIAG_MESSAGE(100,modelName);
#if _HELISIM_RENDER_TO_TEXTURE
        DoAssert(GScene->GetDrawObjectIndex() == 0);
        const SortObject *so = info.object->GetInList(GScene->GetDrawObjectIndex());
#else
        const SortObject *so = info.object->GetInList();
#endif
        if (so)
        {
          RString addInfo = Format(
            "   dist %.2f, cArea %g, lod %d (%d,%d), shadow %d",
            sqrt(so->distance2),so->coveredArea,so->drawLOD,so->srcLOD,so->dstLOD,so->shadowLOD
          );
          DIAG_MESSAGE(100,addInfo);
        }
      }
    }
   
    // if we are on the proxy, it would be nice to have the underlying object information
    Object *obj = info.parentObject;
    if (!obj) obj = info.object;
    
    _cursorObject = obj;
    _cursorObjectPos = info.pos;
    
    EntityAI *ai = dyn_cast<EntityAI,Object>(obj);
    if (ai)
    {
      Vector3 iPos = info.pos;
      Vector3 iDir = iPos-camera.Position();
      if (iDir.SquareSize() > Square(0.001f))
      {
        Vector3 iDirN = iDir.Normalized();
        float iDirSize = iDir*iDirN;
        Vector3 lPos;
        float isect = GLandscape->IntersectWithGround(
          &lPos,camera.Position(),iDirN,0,iDirSize*1.1);
        // we know what is 
        if (isect>iDirSize)
        {
          // no ground obstacle
          // check if not too deep in sea
          isect = GLandscape->IntersectWithGroundOrSea(
            &lPos,camera.Position(),iDirN,0,iDirSize*1.1);
          if (iDirSize - isect < 1.0f)
          {
            // check if we know the target
            if (knownOnly)
            {
              iTarget = agent->FindTarget(ai,includeBuildings);
            }
            else
            {
              iTarget = agent->FindTargetAll(ai,includeBuildings);
            }
            if (iTarget) itPos = iPos;
          }
          //GScene->DrawCollisionStar(iPos,1,PackedColor(Color(0,1,0)));
        }
        else
        {
          // ground occluding
          //GScene->DrawCollisionStar(lPos,0.5,PackedColor(Color(1,0,0)));
        }
      }
    }
  }

  // if we are aiming at something directly, return it
  if (iTarget) return iTarget;
  #endif

  // if not, we should use cursor range look-up
  // if there is no object detected directly, use a target if possible
  if (!_cursorObject && iTarget)
  {
    _cursorObject = iTarget->idExact.GetLink();
    _cursorObjectPos = iTarget->AimingPosition(); // ????
  }
  return iTarget;
}

void InGameUI::SimulateHUDNonAI
(
  const Camera &camera, Entity *vehicle, CameraType cam, float deltaT
)
{
}

bool InGameUI::IsCompassShown() const
{
  AIBrain *unit = GWorld->FocusOn();
  if (!unit) return false;
  Transport *vehicle = unit->GetVehicleIn();
  if (!vehicle) return false;

  int canSee = 0;
  if (unit == vehicle->ObserverUnit()) canSee = vehicle->Type()->_commanderCanSee;
  else if (unit == vehicle->PilotUnit()) canSee = vehicle->Type()->_driverCanSee;
  else if (unit == vehicle->GunnerUnit()) canSee = vehicle->Type()->_gunnerCanSee;

  return (canSee & CanSeeCompass) != 0;
}

void InGameUI::RevealCursorTarget()
{
  Vector3 pos;
  Vector3 cursorDir = GetCursorDirection();
  const Camera &camera = *GScene->GetCamera();
  CameraType cam = GWorld->GetCameraType();
  Target *target = CheckCursorTarget(
    pos, cursorDir, camera, cam, false, false
  );

  if (target) RevealTarget(target, 0.3);
}

Vector3 InGameUI::GetCamGroupCameraPos() const
{
  Vector3 pos;
  Matrix3 camOrient = Matrix3(MRotationY,_camGroupHeading)*Matrix3(MRotationX,_camGroupDive);
  pos = camOrient*VForward*_camGroupZoom;
  return pos;
}

void InGameUI::InitCamGroupCameraPos()
{
  //position camera behind the vehicle
  _camGroupDive=-0.5f; _camGroupZoom=1;
  Object *camOn = GWorld->CameraOn();
  if (camOn)
  {
    Vector3 direct = camOn->FutureVisualState().Transform().Orientation().Direction();
    float angle = atan2(direct.Z(), direct.X());
    _camGroupHeading=angle+H_PI/2;
  }
  else _camGroupHeading=H_PI;
}

void InGameUI::CamGroupActions(float deltaT)
{
  if (GWorld->GetCameraType()==CamGroup)
  {
    //Update camera position
    //Get UALookXXX actions (Keyboard)
    _camGroupDiveOld = _camGroupDive;
    const float diag = 0.5 * H_SQRT2;
    float headSpeed = (
      GInput.GetActionExclusive(UALookLeft)
      + GInput.GetActionExclusive(UALookLeftUp)*diag
      + GInput.GetActionExclusive(UALookLeftDown)*diag
      - GInput.GetActionExclusive(UALookRight)
      - GInput.GetActionExclusive(UALookRightUp)*diag
      - GInput.GetActionExclusive(UALookRightDown)*diag
      );
    float heading = deltaT*headSpeed;
    float diveSpeed = (
      GInput.GetActionExclusive(UALookDown)
      + GInput.GetActionExclusive(UALookLeftDown)*diag
      + GInput.GetActionExclusive(UALookRightDown)*diag
      - GInput.GetActionExclusive(UALookUp)
      - GInput.GetActionExclusive(UALookRightUp)*diag
      - GInput.GetActionExclusive(UALookLeftUp)*diag
      );
    float dive = -deltaT*diveSpeed;
    _camGroupHeading += heading;
    _camGroupDive += dive;
    //Get zoom actions
    float expChange=GInput.GetActionExclusive(UAZoomOut)-GInput.GetActionExclusive(UAZoomIn);
    if( expChange )
    {
      const float ZoomSpeed=8;
      float change=pow(ZoomSpeed,expChange*deltaT);
      _camGroupZoom*=change;
      saturate(_camGroupZoom,0.25,4);
    }
    //Get cursor movement actions (Mouse)
    float aimXVal = GInput.GetActionWithCurveExclusive(InputInterface::Curve1Mouse,UAAimRight, UAAimLeft);
    float aimYVal = GInput.GetActionWithCurveExclusive(InputInterface::Curve1Mouse,UAAimUp, UAAimDown);
    float moveX = (aimXVal/GInput.GetMouseSensitivityMultX())*deltaT*(1000/200.0f);
    float moveY = (aimYVal/GInput.GetMouseSensitivityMultY())*deltaT*(1000/150.0f);
    static float sensitivityX=1.0f;
    static float sensitivityY=1.0f;
    _camGroupCursor.x += moveX*sensitivityX;
    _camGroupCursor.y += moveY*sensitivityY;
  }
}

// TODO: make a member of EntityAIFull, similar to EntityAIFull::CanFireUsing
static bool CanTargetUsing(EntityAIFull *vehicle,const TurretContext &context)
{
  // if the weapon has no locking capabilities, we cannot target
  // prototype: assume commander has no lockable weapons
  WeaponModePtr ptr = context._weapons->ValidatedCurrentWeapon();
  if (!ptr.IsValid()) return false;

  const MuzzleType *muzzle = ptr.GetMuzzleType();
  if (!muzzle) return false;

  return (
    muzzle->_canBeLocked == 2 ||
    muzzle->_canBeLocked == 1 && Glob.config.IsEnabled(DTAutoGuideAT)
  );
}


/*!
\patch 1.32 Date 11/26/2001 by Jirka
- Fixed: Disable action menu in cutscenes
\patch 5117 Date 1/15/2007 by Bebul
- Changed: Command Fire in vehicles works also when commanding mode is not active
\patch 5120 Date 1/18/2007 by Bebul
- Fixed: Locking target by RMB in tank does not activate optics action
\patch 5120 Date 1/19/2007 by Ondra
- Fixed: Commander can use Tab to issue target orders when sitting in the weapon station
  as long as his own weapons are not lockable.
\patch 5126 Date 1/31/2007 by Jirka
- Fixed: MP - manual fire rate was too high sometimes
\patch 5137 Date 3/5/2007 by Jirka
- Fixed: In-game UI: RMB now sends target also to the units selected in the menu (when SPACE is holding)
\patch 5161 Date 5/29/2007 by Jirka
- Fixed: Team switch shortcuts (previous / next unit) accessible also when map is shown
*/

void InGameUI::SimulateHUD
(
  const Camera &camera, CameraType cam, float deltaT
)
{
  UpdateWeaponCursorScaleCoef(deltaT);

  // Cheats
#if _ENABLE_CHEATS
  if( GInput.GetCheat1ToDo(DIK_A))
  {
    _showAll=!_showAll;
    GlobalShowMessage(500,"ShowAll %s",_showAll ? "On":"Off");
  }

  // show / hide status displays
  if (GInput.GetCheat1ToDo(DIK_SEMICOLON))
  {
    bool show = !_showUnitInfo;
    ShowAll(show);
    GChatList.Enable(show);
  }

  if (GInput.GetCheat1ToDo(DIK_APOSTROPHE))
  {
    ShowCursors(!_showCursors);
  }
#endif

  if (GInput.GetActionToDo(UAHelp))
  {
    ShowLastHint();
  }

  AIBrain *agent = GWorld->FocusOn();
  if (!agent || !agent->LSIsAlive()) return;
  Person *person = agent->GetPerson();
  // some actions (commanding or actions menu, group info) can be performed only by a player himself
  bool playerControlled = (person && person == GWorld->PlayerOn());

  EntityAIFull *vehicle = agent->GetVehicle();

  Assert(vehicle);
  if (!vehicle) return;

  //Commanding camera
  if (GWorld->GetCameraType()==CamGroup)
  {
    //CamGroupActions called before SimulateAllVehicles
    //Update cursor position
    //Get UAAimXXX actions (Mouse)
    float cgCurRange = 0.75f;
    Point2DFloat cgCurPos = _camGroupCursor;
    saturate(_camGroupCursor.x, -cgCurRange, cgCurRange);
    saturate(_camGroupCursor.y, -cgCurRange, cgCurRange);
    Vector3 newPos(_camGroupCursor.x * camera.Left(), _camGroupCursor.y * camera.Top(), 1.0f);
    Vector3 newDir = camera.Transform().Rotate(newPos);
    newDir.Normalize();
    SetCursorDirection(newDir);
    //rotate camera view, if the cursor wanted to leave the camGroup range
    static float rotSensitivityX=0.5f;
    static float rotSensitivityY=0.5f;
    Point2DFloat cgRotCam = cgCurPos-_camGroupCursor;
    _camGroupHeading -= cgRotCam.x*rotSensitivityX;
    _camGroupDive += cgRotCam.y*rotSensitivityY;
    if (_camGroupDiveOld != _camGroupDive )
    { //there was some dive camera movement, ground saturation needed
      OLinkObject cameraVehicle = GWorld->CameraOn();
      if (cameraVehicle)
      {
        float dist = cameraVehicle->OutsideCameraDistance(CamGroup);
        Vector3 cgPos = GetCamGroupCameraPos();
        cgPos = cgPos * dist;
        Matrix3 vehOrient;
        Vector3 focPos = cameraVehicle->CameraPosition();
        Vector3 cgPosWorld = focPos+cgPos;
        //test whether cgPosWorld is under the ground
        float minCamY = GScene->GetLandscape()->SurfaceYAboveWater(cgPosWorld.X(),cgPosWorld.Z());
        minCamY += 0.1f;
        if( cgPosWorld.Y()<minCamY ) 
        {
          //atan of triangle a,b,c
          float a = minCamY - cgPosWorld.Y();
          cgPosWorld[1]=minCamY;
          float b = cgPosWorld.Distance(focPos);
          float diveChange = atan2f(a,b);
          _camGroupDive -= diveChange;
        }
      }
    }
    saturateMax(_camGroupDive, -0.8*H_PI/2); //saturation looking from the top down to the group (cannot be at the north pole)
  }

  TurretContextV myContext;
#if _VBS3 //commander override
  bool myContextValid = vehicle->FindControlledTurret(unconst_cast(vehicle->RenderVisualState()), agent->GetPerson(), myContext);
#else
  bool myContextValid = vehicle->FindTurret(unconst_cast(vehicle->RenderVisualState()), agent->GetPerson(), myContext);
#endif

  // show / hide tank direction
  CameraType type = GWorld->GetCameraType();
  const VehicleType *vehType = vehicle->GetType();
  _tankPos = (vehType->HasObserver() && vehicle->IsTurret(type)) ? 0 : 1;

  if (GWorld->HasMap())
  {
    return;
  }
  
  //////////////////////////////////////////////////////////////////////////
  // Handle actions and item shortcuts

  if (playerControlled && !GWorld->Chat() && !GWorld->GetPlayerSuspended())
  {
    // actions
    ProcessActions(GWorld->FocusOn());
  }

  // focus may changed !!!
  agent = GWorld->FocusOn();
  if (!agent) return;
  person = agent->GetPerson();

  playerControlled = person && person == GWorld->PlayerOn();

  vehicle = agent->GetVehicle();

  Assert(vehicle);
  if (!vehicle) return;

  bool isCommander = agent == vehicle->CommanderUnit();

  Transport *transport = agent->GetVehicleIn();

  // bool isGunnerFire = agent == vehicle->GunnerUnit();
  // can player fire as a gunner? if not, we can use UAFire for commanding
  bool isGunnerFire = myContextValid && vehicle->CanFireUsing(myContext);

  if (transport && transport->IsManualFire() && isCommander)
  {
    // when manual fire is on, commander is controlling primary turret (even when has own weapons)
    isGunnerFire = false;
  }

  // can player target as a gunner? if not, we can use cursor and UALockTarget(s) for commanding
  // note: if player cannot fire, he cannot target as well
  // isGunnerFire implies myContextValid

  TurretContextV fireContext = myContext;
  TurretContextV targetContext = myContext;
  
  /*
  {
    int weapon = vehicle->SelectedWeapon();
    if (weapon < 0)
    {
      weapon = vehicle->FirstWeapon();
      // select weapon only if it is primary weapon
      SelectWeapon(unit, weapon);
    }
  }
  */
  
  // find visible target with cursor on
  _target = NULL;
  Vector3 housePosition = VZero;

  {
    //use camera.Direction() for vehicles bacause GetCursorDirection is locked with weapon
    Vector3 cursorDir = GetCursorDirection();

    if (GInput.GetActionToDo(UARevealTarget))
    { // no reset - some processing needs to be done yet
      Vector3 itPos;
      Target *manualTarget= CheckCursorTarget(itPos, cursorDir, camera, cam, false, false);

      if (manualTarget)
      {
        RevealTarget(manualTarget,0.3);
        _target = manualTarget;
      }
    }

    if (!_target)
    {
      // check which object is showed by cursor
      Vector3 itPos;
      Target *iTarget = CheckCursorTarget(itPos, cursorDir, camera, cam, true, true);
      if (iTarget)
      {
        _target = iTarget;
      }
    }
  }

  // check what surface is under cursor
  _cursorSurface = nullptr;
  if (!_cursorObject) 
  {
    // check the intersection with ground
    Vector3 cursorDir = GetCursorDirection();
    const Camera &camera = *GScene->GetCamera();
          
    bool sea;
    const float maxDist = 3.0f;
    float dist = GLandscape->IntersectWithGroundOrSea(&_cursorObjectPos, sea, camera.Position(), cursorDir, 0.0f, maxDist);
    if (dist < maxDist)
    {
      if (sea)
        _cursorSurface = "sea";
      else
      {
        Texture *texture = nullptr;
        GLandscape->RoadSurfaceY(_cursorObjectPos.X(), _cursorObjectPos.Z(), nullptr, nullptr, &texture);
        if (texture)
        {
          _cursorSurface = texture->GetSurfaceInfo()._entryName;
          _cursorSurface.Lower();
        }
      }
      if (CHECK_DIAG(DESurface))
      {
        DIAG_MESSAGE(10, RString("Surface: ") + _cursorSurface);
      }
    }
  }

  {
    float throwScale = vehicle->GetWeaponCursorScale();
    if (throwScale>0)
    {
      _weaponCursorScaleCoef = _throwCursorMinScale + (_throwCursorMaxScale - _throwCursorMinScale)* throwScale;
    }

  }

  if (GInput.GetActionToDo(UANightVision))
  {
    person->NextVisionMode();
  }

  /*
  const float brightnessF = 4.0f;
  const float autoBCF = 2.0f;
  const float contrastF = 4.0f;

  GEngine->SetTIBrightness(GEngine->GetTIBrightness() + 
    (GInput.GetActionExclusive(UATI_Brightness_Inc) - GInput.GetActionExclusive(UATI_Brightness_Dec))
    * deltaT * brightnessF
    );
  if(GEngine->GetTIAutoBC())
  {
    GEngine->SetTIAutoBCContrastCoef(GEngine->GetTIAutoBCContrastCoef() - 
      (GInput.GetActionExclusive(UATI_Contrast_Inc) - GInput.GetActionExclusive(UATI_Contrast_Dec))
      * deltaT * autoBCF
      );
  }
  else
  {
    GEngine->SetTIContrast(GEngine->GetTIContrast() - 
      (GInput.GetActionExclusive(UATI_Contrast_Inc) - GInput.GetActionExclusive(UATI_Contrast_Dec))
      * deltaT * contrastF
      );
  }
  if(GInput.GetActionToDo(UATI_AutoContrast_Toggle))
    GEngine->SetTIAutoBC(!GEngine->GetTIAutoBC());
  */
}

void InGameUI::ShowHint(RString hint, bool sound)
{
  _hint->SetHint(hint);
  _hintTime = Glob.uiTime;
  if (sound && hint.GetLength() > 0 && _hintSound.name.GetLength() > 0)
  {
    AbstractWave *wave = GSoundScene->OpenAndPlayOnce2D
    (
      _hintSound.name, _hintSound.vol, _hintSound.freq, false
    );
    if (wave)
    {
      wave->SetKind(WaveMusic); // UI sounds considered music???
      GSoundScene->AddSound(wave);
    }
  }
}

void InGameUI::ShowHint(INode *hint, bool sound)
{
  _hint->SetHint(hint);
  _hintTime = Glob.uiTime;
  if (sound && hint != 0 && _hintSound.name.GetLength() > 0)
  {
    AbstractWave *wave = GSoundScene->OpenAndPlayOnce2D(
      _hintSound.name, _hintSound.vol, _hintSound.freq, false);
    if (wave)
    {
      wave->SetKind(WaveMusic); // UI sounds considered music???
      GSoundScene->AddSound(wave);
    }
  }
}

void InGameUI::ShowTaskHint(RString taskHint, PackedColor color, Texture *texture)
{
  _taskHint->AddTaskHint(taskHint, color, texture);
}

void InGameUI::UpdateWeaponCursorScaleCoef(float deltaT)
{
  if (_weaponCursorScaleCoef != 1.0f)
  {
    if (_weaponCursorScaleCoef > 1.0f)
    {
      _weaponCursorScaleCoef -= _throwCursorFadeSpeed * deltaT;
      if (_weaponCursorScaleCoef <= 1.0f)
        _weaponCursorScaleCoef = 1.0f;
    }
    else
    {
      _weaponCursorScaleCoef += _throwCursorFadeSpeed * deltaT;
      if (_weaponCursorScaleCoef >= 1.0f)
        _weaponCursorScaleCoef = 1.0f;
    }
  }
}

#if _ENABLE_CONVERSATION

void InGameUI::ProcessConversation(AIBrain *askingUnit, AIBrain *askedUnit, const KBMessageInfo *question)
{
}

void ProcessConversation(AIBrain *askingUnit, AIBrain *askedUnit, const KBMessageInfo *question)
{
}

void ClearConversationHistory()
{
}

#endif


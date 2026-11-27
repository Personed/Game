#include "wpch.hpp"

/*!
\file
Implementation file for Man and Soldier classes
*/

#if _PROFILE
# pragma optimize( "", off )
#endif

#include "soldierOld.hpp"
#include "transport.hpp"
#include "shots.hpp"
#include "scene.hpp"
#include "world.hpp"
#include "landscape.hpp"
#include "keyInput.hpp"
#include <El/Common/randomGen.hpp>
#include <Es/Framework/potime.hpp>
#include "camera.hpp"
#include "tracks.hpp"
#include <El/Common/perfLog.hpp>
#include <El/Common/perfProf.hpp>
#include "proxyWeapon.hpp"
#include "Shape/specLods.hpp"
#include "house.hpp"
#include "detector.hpp"
#ifdef _WIN32
#include "joystick.hpp"
#endif
#include "txtPreload.hpp"
#include "diagModes.hpp"
#include <Es/Strings/bString.hpp>
#include <Es/Types/scopeLock.hpp>
#include <Es/Common/langExt.hpp>
#include <string.h>

#include "move_actions.hpp"
#include "integrity.hpp"
#include "fileLocator.hpp"
#include <El/FileServer/fileServer.hpp>

// Case-insensitive substring test for move names. The names returned by the
// animation config are lower-case ("...wrfl...", "...pknl..."), so the old
// case-sensitive strstr() never matched and the jump/vault always fell back to
// the unarmed variant even when a weapon was in the hands.
static bool MoveNameHas(const char *name, const char *part)
{
  if (!name || !part || !*part) return false;
  const size_t len = strlen(part);
  for (const char *p = name; *p; ++p)
    if (_strnicmp(p, part, len)==0) return true;
  return false;
}
#include "gameDirs.hpp"

// Match animation-state fragments without depending on their letter casing.
static bool MoveNameHas(const char *name, const char *part)
{
  if (!name || !part || !*part) return false;
  const size_t len = strlen(part);
  for (const char *p = name; *p; ++p)
    if (_strnicmp(p, part, len) == 0) return true;
  return false;
}

#include "dikCodes.h"

#include "Network/network.hpp"

#include <El/QStream/qbStream.hpp>
#include <El/PreprocC/preprocC.hpp>
#include <El/Evaluator/express.hpp>

#include "AI/operMap.hpp"

#include "UI/uiActions.hpp"

#include "gameStateExt.hpp"
#include "paramArchiveExt.hpp"
#include "scripts.hpp"
#if _ENABLE_CONVERSATION
# include "KBExt/kbExt.hpp"
#endif
#include "seaGull.hpp"

#include <El/Enum/enumNames.hpp>

#include "UI/chat.hpp"

#include "stringtableExt.hpp"

#include "progress.hpp"
#include "drawText.hpp"

#include "Protect/selectProtection.h"

#include "AI/aiRadio.hpp"
#include "ai/ai.hpp"

#include "Physics\enf_physicsbullet.h"

#include "Es/Common/langExt.hpp"
#include <lib/Inventory/itemoptics.hpp>
#include "baseWorld/enf_shapes.h"

#if _ENABLE_PHYSICS
#include "BulletCollision/CollisionShapes/btCapsuleShape.h"
#endif

#define ENABLED_NOOPT_Bebul 0
#if ENABLED_FEATURE(NOOPT)
#if _ENABLE_CHEATS
#pragma optimize ("", off)
#endif
#endif

#if !_RELEASE
  #define ARROWS 1
#endif


#if _ENABLE_CHEATS
// For DEBUG: Store info about last actions
void SDebugActionsInfo::AddInfo(RString actionName, RString moveName, RString type)
{
  if (lastActions.Size() > 0 
    && lastActions[lastActions.Size()-1].actionName == actionName
    && lastActions[lastActions.Size()-1].moveName == moveName)
  {
    lastActions[lastActions.Size()-1].time = GetTickCount();
    return;
  }
  if (lastActions.Size() >= 10)
    lastActions.Delete(0);
  SInfo info;
  info.actionName = actionName;
  info.moveName = moveName;
  info.type = type;
  info.time = GetTickCount();
  lastActions.Add(info);
}

RString SDebugActionsInfo::GetDiagInfo(RString prefix)
{
  if (lastActions.Size() == 0) 
    return RString();

  // Check for timeout
  unsigned int time = GetTickCount();
  while (lastActions.Size() > 0 && (time - lastActions[0].time > 5000))
     lastActions.Delete(0);

//  RString buf = "Last actions:";
  RString buf;
  for (int i = 0 ; i < lastActions.Size(); ++i)
  {
    const SInfo &info = lastActions[i];
    buf = buf + Format("\n%s %d %s %s %s", prefix.Data(), info.time, info.type.Data(), info.actionName.Data(), info.moveName.Data());
  }
  return buf;
}
TypeIsGeneric(SDebugActionsInfo::SInfo)
#endif


static const EnumName ManActionNames[]=
{
#define ACTION(x) EnumName(ManAct##x,#x),
#include "../cfg/manActions.hpp"
#undef ACTION
  EnumName()
};


#define DNAME() (cc_cast(GetDebugName())
#define NAME(i) (cc_cast(GetMovesType()->GetMoveName(i)))
#define NAME_T(t,i) (cc_cast((t)->_moveType->GetMoveName(i)))

#include <Es/Memory/normalNew.hpp>
struct ActionContextDefault: public ActionContextBase
{
  int param;
  int param2;
  RString param3;

  ActionContextDefault();
  ~ActionContextDefault();

  virtual LSError Serialize(ParamArchive &ar)
  {
    RString type("default");
    if (ar.IsSaving()) 
      CHECK(ar.Serialize("type", type, 1))
    CHECK(ar.Serialize("function", (int &)function, 1, MFNull))
    CHECK(ar.Serialize("param", param, 1, 0))
    CHECK(ar.Serialize("param2", param2, 1, 0))
    CHECK(ar.Serialize("param3", param3, 1, ""))
    return LSOK;
  }

  USE_FAST_ALLOCATOR;
};

struct ActionContextThrow: public ActionContextBase
{
  OLinkPermO(Magazine) _item;
  OLinkO(Man) _man;
  MoveIdExt _move;

  ActionContextThrow(){}
  ~ActionContextThrow(){};

  /// called once given context is finished
  virtual void Do(Man *ai)
  {
    DoAssert(_man==ai);
  }

  virtual LSError Serialize(ParamArchive &ar)
  {
    RString type("Throw");
    if (ar.IsSaving()) CHECK(ar.Serialize("type", type, 1))
    CHECK(ar.Serialize("function", (int &)function, 1, MFNull))
    CHECK(ar.SerializeRef("item", _item, 1))
    return LSOK;
  }

  virtual MoveId GetPlayMove() const override {return _move.id;}

  USE_FAST_ALLOCATOR;
};

DEFINE_FAST_ALLOCATOR(ActionContextThrow)

#if _ENABLE_PHYSICS
struct ActionContextThrowItem : public ActionContextBase
{
  OLinkPermO(InventoryItem) _item;
  OLinkO(Man) _man;
  MoveId _moveid;
  unsigned int _power;
  bool _loaded;

	MoveId GetPlayMove() const
	{
		return _moveid;
	}

  ActionContextThrowItem():_loaded(false)
  {
    _power = (unsigned int)(getSystemTime() / 1000);
  }

  ~ActionContextThrowItem(){};

  virtual LSError Serialize(ParamArchive &ar) override
  {
    RString type("ThrowItem");
    if (ar.IsSaving()) CHECK(ar.Serialize("type", type, 1))
    CHECK(ar.Serialize("function", (int &)function, 1, MFNull))
    CHECK(ar.SerializeRef("item", _item, 1))
    CHECK(ar.Serialize("power", _power, 1))
    return LSOK;
  }
  
  virtual void Do(Man *man) override;
  virtual void Event(int userType, const RStringB& param) override;
  void UserEvent(int userType);
  
  USE_FAST_ALLOCATOR;
};

void ActionContextThrowItem::UserEvent(int userType)
{
  //powerLoading end (key up)
  if(userType == 1 && !_loaded)
  {
    _power = (unsigned int)(getSystemTime() / 1000) - _power;
    _man->ContinueMove();
    _loaded = true;
  }
}

void ActionContextThrowItem::Event(int userType, const RStringB& param)
{
  if(!_man.IsNull())
  {
    Man* man = _man;
    
    switch(userType)
    {
    case 4:
      if(!_loaded)
      {
        man->PauseMove(_moveid);
      }
      break;
    case 5:
      {
        //can happen if the continue is invoked by different action
        if(!_loaded)
        {
          _power = (unsigned int)(getSystemTime() / 1000) - _power;
        }
        float power = enf::enf_min((float)_power/10.f, man->GetMass());
        man->ThrowItemAction(_item, power);
      }
      break;
    default:
      break;
    };

    man->OnEvent(EEAnimHook, userType, param);
  }
}

void ActionContextThrowItem::Do(Man *man)
{
  //man->ThrowItemAction(_item);
}

DEFINE_FAST_ALLOCATOR(ActionContextThrowItem)
#endif

struct ActionContextTakeInHands : public ActionContextBase
{
  OLinkPermO(InventoryItem) _item;

  ActionContextTakeInHands(){}
  ~ActionContextTakeInHands(){};

  virtual LSError Serialize(ParamArchive &ar) override
  {
    RString type("TakeInHands");
    if (ar.IsSaving()) CHECK(ar.Serialize("type", type, 1))
    CHECK(ar.Serialize("function", (int &)function, 1, MFNull))
    CHECK(ar.SerializeRef("item", _item, 1))
    return LSOK;
  }
  virtual void Do(Man *man) override;

  USE_FAST_ALLOCATOR;
};

void ActionContextTakeInHands::Do(Man *man)
{
  man->TakeInHandsAction(_item);
}

DEFINE_FAST_ALLOCATOR(ActionContextTakeInHands)

struct ActionContextReload: public ActionContextBase
{
  // where to reload
  WeaponModePtr _ptr;

  ActionContextReload(){}
  ~ActionContextReload(){};

  virtual void Do(Man *ai) override;
  virtual void Progress(float progress) override;

  virtual LSError Serialize(ParamArchive &ar)
  {
    RString type("Reload");
    if (ar.IsSaving())
      CHECK(ar.Serialize("type", type, 1))
    CHECK(ar.Serialize("function", (int &)function, 1, MFNull))
    CHECK(ar.SerializeDef("weapon", _ptr, 1))
    return LSOK;
  }

  USE_FAST_ALLOCATOR;
};

DEFINE_FAST_ALLOCATOR(ActionContextReload)

void ActionContextReload::Progress(float progress)
{
  _ptr.SetReloadMagazineProgress(progress >= 1 ? 0 : progress);
}

void ActionContextReload::Do(Man *ai)
{
  if (!_ptr.IsValid()) return;
  ai->ReloadMagazineTimed(*ai->GetWeaponsState(), _ptr, true);
}

struct ActionContextDrawBow : public ActionContextBase
{
  OLinkO(Man) _man;
  bool _started;
  WeaponModePtr _weapon;

  ActionContextDrawBow()
    :_started(false)
  {
  }

  ~ActionContextDrawBow() {};

  virtual LSError Serialize(ParamArchive &ar) override
  {
    RString type("DrawBow");
    if (ar.IsSaving()) 
      CHECK(ar.Serialize("type", type, 1))
    CHECK(ar.Serialize("function", (int &)function, 1, MFNull))
    CHECK(ar.Serialize("started", _started, 0, false));
    CHECK(ar.SerializeDef("weapon", _weapon, 1))
    return LSOK;
  }

  virtual void Started() override;
  virtual void Do(Man *man) override;
  virtual void Event(int userType, const RStringB& param) override;

  USE_FAST_ALLOCATOR;
};

void ActionContextDrawBow::Started()
{
  if (_started)
  {
    return;
  }

  if (function == MFBowDraw && !_man.IsNull())
  {
    _man->SetDrawingBow(true);
    _man->OnBowStateChange(BSDrawn);
    _started = true;
  }

  if (function == MFBowRelease && !_started && !_man.IsNull() && _man->CanFire() && _weapon.IsValid())
  {
    const WeaponModeType *mode = _weapon.GetWeaponModeType();
    const Magazine *mag = _weapon.GetMagazine();

    bool fire = true;
    if (mode && mode->_autoFire && mag && mag->GetAmmo() > 0)
    {
      fire = !GWorld->HasMap();
    }

    if (fire)
    {
      TurretContextV myContext;
      myContext._turret = NULL;
      myContext._turretType = NULL;
      myContext._gunner = _man.GetLink();
      myContext._weapons = _man->GetWeaponsState();
      myContext._turretVisualState = NULL;

      _man->FireWeapon(myContext, _weapon, NULL, false);
      _started = true;

      _man->OnBowStateChange(BSReleased);
    }
  }

  if (function == MFBowCancel)
  {
    _started = true;
    _man->OnBowStateChange(BSCanceled);
  }
}

void ActionContextDrawBow::Do(Man *man)
{
  if((MFBowRelease == function || MFBowCancel == function) && !_man.IsNull())
  {
    _man->SetDrawingBow(false);
    _man->OnBowStateChange(BSIdle);
  }
}

void ActionContextDrawBow::Event(int userType, const RStringB& param)
{
  if(!_man.IsNull())
  {
    _man->OnEvent(EEAnimHook, userType, param);
  }
}

DEFINE_FAST_ALLOCATOR(ActionContextDrawBow)

struct ActionContextCloseDoor: public ActionContextBase
{
  int _doorIndex;
  RString _actionAfter;

  ActionContextCloseDoor(){_doorIndex = -1;}
  ~ActionContextCloseDoor(){};

  virtual LSError Serialize(ParamArchive &ar)
  {
    RString type("CloseDoor");
    if (ar.IsSaving())
      CHECK(ar.Serialize("type", type, 1))
      CHECK(ar.Serialize("function", (int &)function, 1, MFNull))
      CHECK(ar.Serialize("doorIndex", _doorIndex, 1, 0))
      return LSOK;
  }

  USE_FAST_ALLOCATOR;
};

DEFINE_FAST_ALLOCATOR(ActionContextCloseDoor)


struct ActionContextAllowGetOut: public ActionContextBase
{
  ActionContextAllowGetOut(){}
  ~ActionContextAllowGetOut(){};

  virtual LSError Serialize(ParamArchive &ar)
  {
    RString type("AllowGetOut");
    if (ar.IsSaving())
      CHECK(ar.Serialize("type", type, 1))
      CHECK(ar.Serialize("function", (int &)function, 1, MFNull))
      return LSOK;
  }

  USE_FAST_ALLOCATOR;
};

DEFINE_FAST_ALLOCATOR(ActionContextCloseDoor)


/// mediator class for increased type safety
/** this call is used to communicate with ActionContextAnimation
*/
class ActivityAnimation: public EntityActivity
{
  public:
  //{@ empty state implementation - helper for any derived class
  void DrawNothing(Entity *entity){}
  void SimulateNothing(Entity *entity, float deltaT){}
  //@}
};


/// report when animation started or finished
class ActivityEventAnimation: public ActivityEvent, public SerializeClass
{
  typedef ActivityEvent base;

public:
  /// event reports animation has started, canceled or finished
  enum State
  {
    SStarted,
    SCanceled,
    SFinished
  };

protected:
  State _state;
  
public:
  State GetState() const {return _state;}
  ActivityEventAnimation() {};
  ActivityEventAnimation(ActivityAnimation *act, State state) : base(act), _state(state){}

  virtual LSError Serialize(ParamArchive &ar)
  {
    base::Serialize(ar);
    CHECK(ar.Serialize("State", (int &)_state, 1, SStarted))
    return LSOK;
  }
};


/// implement ClassTypeInfo for ActivityGetIn
class ActivityClassTypeInfoGetIn: public ActivityClassTypeInfo
{
  typedef ActivityClassTypeInfo base;
  
  public:
  ActivityClassTypeInfoGetIn():base("GetIn"){}
  //@{ implement ActivityClassTypeInfo
  virtual Activity *CreateObject(ParamArchive &ar) const;
  virtual Activity *LoadRef(ParamArchive &ar) const;
  //@}
  
};

/// handle getting in vehicle process
class ActivityGetIn: public ActivityAnimation
{
  typedef EntityActivity base;
  
  static ActivityClassTypeInfoGetIn _typeInfo;
  
  OLinkPerm<Transport> _vehicle;
  
  UIActionType _position;
  OLinkPerm<Turret> _turret;
  int _cargoIndex;

  typedef ActivityState<ActivityGetIn> State;
  
  /// what state is active now
  State _state;
  
  /// global state information
  float _fadeOut;
  
  //@{
  /** @name processing of individual states, all with StateSimulation or StateDraw signature */
  /// start the animation
  void StartAnimation(Entity *entity, float deltaT);
  /// animation is running
  void SimulateAnimation(Entity *entity, float deltaT);
  /// wait before fading out
  void SimulateWaitForFadeOut(Entity *entity, float deltaT);
  /// fade out before getting in
  void SimulateFadeOut(Entity *entity, float deltaT);
  /// fade out before getting in
  void SimulateFinish(Entity *entity, float deltaT);
  /// draw nothing
  ///void DrawNothing(Entity *entity);
  /// draw fade out rectangle
  void DrawFadeOut(Entity *entity);
  //@}
  
  //@{ support for serialization
  static State::StateSimulation _sim[];
  static State::StateDraw _draw[];
  public:
  static int NStateSimulation();
  static State::StateSimulation GetStateSimulation(int i);
  static int NStateDraw();
  static State::StateDraw GetStateDraw(int i);
  //@}

  private:
  /// helper - do any preloading as necessary
  void DoPreload(Entity *entity);
  public:
  ActivityGetIn(Transport *veh, UIActionType pos, Turret *turret, int cargoIndex, Man *man);
  //@{ implement Activity
  virtual void OnEvent(ActivityEvent *event);
  virtual LSError Serialize(ParamArchive &ar);
  virtual const ActivityClassTypeInfo &GetTypeInfo() const {return _typeInfo;}
  //@}
  
  //@{ implement EntityActivity
  virtual bool Simulate(Entity *entity, float deltaT, SimulationImportance prec);
  virtual void Draw(Entity *entity);
  /// man should be suspended while performing this activity
  virtual bool SuspendEntity(const Entity *entity) const {return true;}
  //@}

  static void RegisterType() {Activity::RegisterType(&_typeInfo);}
  
  USE_FAST_ALLOCATOR
};



/// implement ClassTypeInfo for ActivityGetOut
class ActivityClassTypeInfoGetOut: public ActivityClassTypeInfo
{
  typedef ActivityClassTypeInfo base;

public:
  ActivityClassTypeInfoGetOut():base("GetOut"){}
  //@{ implement ActivityClassTypeInfo
  virtual Activity *CreateObject(ParamArchive &ar) const;
  virtual Activity *LoadRef(ParamArchive &ar) const;
  //@}
};

#include <Es/Memory/normalNew.hpp>

/// get out of the vehicle
class ActivityGetOut: public EntityActivity
{
  typedef EntityActivity base;

  static ActivityClassTypeInfoGetOut _typeInfo;

  OLinkPermNO(AIBrain) _unit;
  float _fadeOut;
  /// when ejecting, we do not switch the engine off
  bool _eject;

public:
  ActivityGetOut(AIBrain *unit, bool eject);

  //@{ implement Activity
  virtual void OnEvent(ActivityEvent *event){}
  virtual const ActivityClassTypeInfo &GetTypeInfo() const {return _typeInfo;}
  virtual LSError Serialize(ParamArchive &ar)
  {
    CHECK(base::Serialize(ar))
    CHECK(ar.SerializeRef("unit", _unit, 1));
    CHECK(ar.Serialize("fadeOut", _fadeOut, 1));
    CHECK(ar.Serialize("eject", _eject, 0, false));
    return LSOK;
  }
  //@}

  //@{ implementation of EntityActivity
  virtual bool Simulate(Entity *entity, float deltaT, SimulationImportance prec);
  virtual void Draw(Entity *entity);
  //@}

  //@{ When activity is to be added, try to find the same prior activity and update its eject
  const AIBrain *GetUnit() const { return _unit; }
  void SetEject(bool eject) { _eject = eject; }
  static const ActivityClassTypeInfo &GetOutTypeInfo() {return _typeInfo;}
  //@}

  static void RegisterType() {Activity::RegisterType(&_typeInfo);}

  USE_FAST_ALLOCATOR
};

DEFINE_FAST_ALLOCATOR(ActivityGetOut)


ActivityClassTypeInfoGetOut ActivityGetOut::_typeInfo;

Activity* ActivityClassTypeInfoGetOut::CreateObject(ParamArchive &ar) const
{
  return new ActivityGetOut(NULL, false); 
}

Activity* ActivityClassTypeInfoGetOut::LoadRef(ParamArchive &ar) const
{
  return NULL;
}

ActivityGetOut::ActivityGetOut(AIBrain *unit, bool eject)
:_unit(unit),_fadeOut(0),_eject(eject)
{
  if (_unit) _unit->GetPerson()->SetOutReady(false);
}

CameraType ValidateCamera(CameraType cam);

bool ActivityGetOut::Simulate(Entity *entity, float deltaT, SimulationImportance prec)
{
  // before we can start getting out, we want the vehicle to open doors for us
  if (_unit)
  {
    Transport *trans = dyn_cast<Transport>(entity);
    if (trans && !trans->PrepareGetOut(_unit))
    {
      // if door opening in progress, wait
      return true;
    }
    // play getout animation
    if (!_unit->GetPerson()->GetOutReady() )
    {
      RString action; 
      if (_unit->GetPerson()==trans->Driver())
      {
        if (trans->Type()->PreciseGetInOut()!=GETIN_OLD)
          action = trans->Type()->GetDriverGetOutAction();
      }
      else
      {
        TurretContext context;
        if (trans->FindTurret(_unit->GetPerson(), context))
        {
          if (context._turretType->GunnerPreciseGetInOut()!=GETIN_OLD)
            action = context._turretType->GetGunnerGetOutAction();
        }
        else
        {
          for (int i=0; i<trans->GetManCargo().Size(); i++)
          {
            if (_unit->GetPerson() == trans->GetManCargo()[i])
            {
              if (trans->Type()->CargoPreciseGetInOut(i)!=GETIN_OLD)
                action = trans->Type()->GetCargoGetOutAction(i);
              break;
            }
          }
        }
      }

      if (!action.IsEmpty())
      {
        _unit->GetPerson()->PreciseGetOutAction(action);
        return true;
      }
    }
  }

  if (_unit && GWorld->FocusOn() == _unit)
  {
    _fadeOut += deltaT*3;
    Person *person = _unit->GetPerson();

    person->PreloadView(person,ValidateCamera(GWorld->GetCameraType()));
    // preload LOD and texture for external viewing
    LODShape *shape = entity->GetShape();
    if (shape)
    {
      if (shape->CheckLevelLoaded(0,true))
      {
        ShapeUsed lock = shape->Level(0);
        lock->PreloadTextures(0, NULL);
      }
    }
  }
  else
  {
    _fadeOut = 1;
  }
  if (_fadeOut>=1)
  {
    if (_unit)
    {
      Transport *trans = dyn_cast<Transport>(entity);
      if (trans)
        trans->CloseDoor(_unit);

      _unit->ProcessGetOut(_eject,false);

      Person *person = _unit->GetPerson();
      Man *man = dyn_cast<Man>(person);
      DoAssert(man);
      if (man)
      {
        ManVisualState &mvs = man->FutureVisualState();
        mvs._gettingInOut = false;
      }
      if (trans)
        trans->FinishGetOut(_unit);
    }
#if 0 // _ENABLE_CHEATS
    if (_unit && GWorld->FocusOn() == _unit)
    {
      extern bool StopLoadingTextures;
      StopLoadingTextures = true;
    }
#endif
    return false; // terminate immediately
  }
  return true;
}

void ActivityGetOut::Draw(Entity *entity)
{
  if (_unit && GWorld->FocusOn() == _unit)
  {
    GEngine->Draw2DWholeScreen(PackedBlack,_fadeOut);
  }
}

void Transport::StartGetOutActivity(AIBrain *unit, bool eject)
{
  // Check whether other GetOutActivity is pending. If so, change only its parameters (eject).
  for (int i=0; i<_activities.Size(); i++)
  {
    if ( &_activities[i]->GetTypeInfo() == &ActivityGetOut::GetOutTypeInfo() )
    {
      ActivityGetOut *getOut = static_cast<ActivityGetOut *>(_activities[i].GetRef());
      if (getOut->GetUnit()==unit)
      {
        getOut->SetEject(eject);
        return; // found: no new GetOut activity needed
      }
    }
  }
  ActivityGetOut *act = new ActivityGetOut(unit,eject);
  _activities.Add(act);
}


class ActionContextAnimation: public ActionContextBase
{
  ActivityEventAnimation _anim;
  ActivityEventAnimation _getIn;
  ActivityEventAnimation _cancel;
  
  USE_FAST_ALLOCATOR;
  
  public:
  ActionContextAnimation() {};
  ActionContextAnimation(ActivityAnimation *activity, MoveFinishF func);
  //@{ implementation of ActionContextBase
  virtual void Started() override {_anim.Do();}
  virtual void Cancel() override {_cancel.Do();}
  virtual void Do(Man *man) override {_getIn.Do();}
  //@}

  virtual LSError Serialize(ParamArchive &ar) override
  {
    RString type("animation");
    if (ar.IsSaving()) 
      CHECK(ar.Serialize("type", type, 1))
    CHECK(ar.Serialize("function", (int &)function, 1, MFNull))
    CHECK(ar.Serialize("anim", _anim, 1))
    CHECK(ar.Serialize("getIn", _getIn, 1))
    CHECK(ar.Serialize("cancel", _cancel, 1))
    return LSOK;
  }
};

#include <Es/Memory/debugNew.hpp>

Activity *ActivityClassTypeInfoGetIn::CreateObject(ParamArchive &ar) const
{
  return new ActivityGetIn(NULL, ATNone, NULL, -1, NULL);
}

Activity *ActivityClassTypeInfoGetIn::LoadRef(ParamArchive &ar) const
{
  return NULL;
}



DEFINE_FAST_ALLOCATOR(ActivityGetIn)

ActivityGetIn::State::StateSimulation ActivityGetIn::_sim[] =
{
  &StartAnimation,
  &SimulateAnimation,
  &SimulateWaitForFadeOut,
  &SimulateFadeOut,
  &SimulateFinish
};

ActivityGetIn::State::StateDraw ActivityGetIn::_draw[] =
{
  &DrawNothing,
  &DrawFadeOut
};

int ActivityGetIn::NStateSimulation() {return lenof(_sim);}
ActivityGetIn::State::StateSimulation ActivityGetIn::GetStateSimulation(int i) {return _sim[i];}
int ActivityGetIn::NStateDraw() {return lenof(_draw);}
ActivityGetIn::State::StateDraw ActivityGetIn::GetStateDraw(int i) {return _draw[i];}

/**
@param man never used, passed here only to make sure activity is registered for Man only
*/

ActivityGetIn::ActivityGetIn(Transport *veh, UIActionType pos, Turret *turret, int cargoIndex, Man *man)
:_vehicle(veh),_position(pos),_turret(turret),_cargoIndex(cargoIndex),
_state(&ActivityGetIn::StartAnimation,&ActivityGetIn::DrawNothing),
_fadeOut(0)
{
  if (_vehicle)
  {
    _vehicle->PrepareGetIn(_position, _turret, _cargoIndex);
  }
}

bool ActivityGetIn::Simulate(Entity *entity, float deltaT, SimulationImportance prec)
{
  if (_state._sim)
    (this->*_state._sim)(entity,deltaT);
  // we may preload required data of the target now
  return _state._sim;
}

void ActivityGetIn::Draw(Entity *entity)
{
  if (_state._draw)
    (this->*_state._draw)(entity);
}

void ActivityGetIn::StartAnimation(Entity *entity, float deltaT)
{
  if (!_vehicle)
  {
    _state = State();
    return;
  }
  
  // we may take this as a preloading opportunity
  if ( entity==GWorld->CameraOn())
  {
    _vehicle->PreloadGetIn(_position);
  }

  if (!_vehicle->PrepareGetIn(_position, _turret, _cargoIndex))
  {
    Person *person = dyn_cast<Person>(entity);
    if (person)
    {
      // make sure we stop (we may also want to prepare the animation?)
      //person->PlayAction(CAST_TO_ENUM(ManAction, ManActStop));
      person->SwitchAction(CAST_TO_ENUM(ManAction, ManActStop));
    }
    return;
  }
  // set the state to wait for the animation ... and let us wait for it
  _state = State(&ActivityGetIn::SimulateAnimation,&ActivityGetIn::DrawNothing);

  // start the animation
  Ref<ActionContextAnimation> ctx = new ActionContextAnimation(this,MFGetIn);
  Person *person = dyn_cast<Person>(entity);
  DoAssert(person);
  if (person)
  {
    RString getInAction;
    switch (_position)
    {
    case ATGetInPilot:
    case ATGetInDriver:
      {
        if (_vehicle->Type()->PreciseGetInOut()==GETIN_OLD)
          getInAction = _vehicle->Type()->GetDriverGetInAction();
      }
      break;
    case ATGetInTurret:
      {
        TurretContext context;
        if (_vehicle->FindTurret(_turret, context) && context._turretType)
        {
          if (context._turretType->GunnerPreciseGetInOut()==GETIN_OLD)
          {
            getInAction = context._turretType->GetGunnerGetInAction();
          }
        }
      }
      break;
    case ATGetInCargo:
      if (_vehicle->Type()->CargoPreciseGetInOut(_cargoIndex)==GETIN_OLD)
        getInAction = _vehicle->Type()->GetCargoGetInAction(_cargoIndex);
      break;
    }    


    person->SwitchAction(getInAction, ctx);
    // note: SwitchAction may already have performed a state change, 
    // as if it returned false, it has already called ActivityGetIn::OnEvent with ActivityEventAnimation::SFinished
  }
}


void ActivityGetIn::SimulateAnimation(Entity *entity, float deltaT)
{
  // no simulation, we are only waiting for an event
}

const float FadeOutTime = 0.250f;

/**
Fading out should be timed so that is is finished by the time animation is finished
This state starts executing when the animation starts playing.
*/

void ActivityGetIn::SimulateWaitForFadeOut(Entity *entity, float deltaT)
{
  // check how much time is left in the currently running primary animation
  Man *man = static_cast<Man *>(entity);
  float primTime = man->GetPrimaryMoveTimeLeft();
  if (primTime<=FadeOutTime)
  {
    // time to start fading out
    _state = State(&ActivityGetIn::SimulateFadeOut,&ActivityGetIn::DrawFadeOut);
  }
  DoPreload(entity);
}

void ActivityGetIn::DoPreload(Entity *entity)
{
  if (entity==GWorld->CameraOn() && _vehicle)
  {
    _vehicle->PreloadGetIn(_position);
  }
}

void ActivityGetIn::SimulateFadeOut(Entity *entity, float deltaT)
{
  if (entity==GWorld->CameraOn() && _vehicle)
  {
    _fadeOut += deltaT*(1.0f/FadeOutTime);
    _vehicle->PreloadGetIn(_position);
  }
  else
  {
    // no fade out when there is no focus on the activity
    _fadeOut = 1;
  }
  if (_fadeOut>=1)
  {
    _fadeOut = 1;
    _state = State(&ActivityGetIn::SimulateFinish,&ActivityGetIn::DrawFadeOut);
  }
}

void ActivityGetIn::SimulateFinish(Entity *entity, float deltaT)
{
  Man *man = static_cast<Man *>(entity);
  AIBrain *brain = man->Brain();
  if (brain && _vehicle)
  {
    brain->ProcessGetIn2(_vehicle, _position, _turret, _cargoIndex);
    _vehicle->FinishGetIn(_position, _turret, _cargoIndex);
#if 0 //_ENABLE_CHEATS
    if (entity==GWorld->CameraOn() && _vehicle)
    {
      extern bool StopLoadingTextures;
      StopLoadingTextures = true;
    }
#endif
  }
  // indicate activity has terminated
  _state = State();
}

//void ActivityAnimation::DrawNothing(Entity *entity)
//{
//}
void ActivityGetIn::DrawFadeOut(Entity *entity)
{
  GEngine->Draw2DWholeScreen(PackedBlack,_fadeOut);
}

void ActivityGetIn::OnEvent(ActivityEvent *event)
{
  ActivityEventAnimation *ev = static_cast<ActivityEventAnimation *>(event);
  switch (ev->GetState())
  {
  case ActivityEventAnimation::SStarted:
    _state = State(&ActivityGetIn::SimulateWaitForFadeOut,&ActivityGetIn::DrawNothing);
    break;
  case ActivityEventAnimation::SCanceled:
    // indicate activity has terminated
    _state = State();
    break;
  case ActivityEventAnimation::SFinished:
    _state = State(&ActivityGetIn::SimulateFadeOut,&ActivityGetIn::DrawFadeOut);
    break;
  }
}
  
LSError ActivityGetIn::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))
  
  CHECK(ar.SerializeRef("vehicle", _vehicle, 1))
  CHECK(ar.SerializeEnum("position", _position, 1))
  CHECK(ar.SerializeRef("turret", _turret, 1))
  CHECK(ar.Serialize("cargoIndex", _cargoIndex, 1, -1))
  CHECK(ar.Serialize("State", _state, 1))
  CHECK(ar.Serialize("fadeOut", _fadeOut, 1))
  return LSOK;
}

ActivityClassTypeInfoGetIn ActivityGetIn::_typeInfo;


/// implement ClassTypeInfo for ActivityUIAction
class ActivityClassTypeInfoUIAction: public ActivityClassTypeInfo
{
  typedef ActivityClassTypeInfo base;
  
  public:
  ActivityClassTypeInfoUIAction():base("UIAction"){}
  //@{ implement ActivityClassTypeInfo
  virtual Activity *CreateObject(ParamArchive &ar) const;
  virtual Activity *LoadRef(ParamArchive &ar) const;
  //@}
  
};

#include <Es/Memory/normalNew.hpp>

/// handle UIAction process
class ActivityUIAction: public ActivityAnimation
{
  typedef EntityActivity base;
  
  static ActivityClassTypeInfoUIAction _typeInfo;
  
  Ref<Action> _action;
  
  typedef ActivityState<ActivityUIAction> State;
  
  /// what state is active now
  State _state;
  
  void SimulatePreload(Entity *entity, float deltaT);
  void SimulateUIAction(Entity *entity, float deltaT);
  
  //@{ support for serialization
  static State::StateSimulation _sim[];
  static State::StateDraw _draw[];
  
public:
  static int NStateSimulation();
  static State::StateSimulation GetStateSimulation(int i);
  static int NStateDraw();
  static State::StateDraw GetStateDraw(int i);
  //@}

  ActivityUIAction(const Action *action, Man *man);
  //@{ implement Activity
  virtual void OnEvent(ActivityEvent *event);
  virtual LSError Serialize(ParamArchive &ar);
  virtual const ActivityClassTypeInfo &GetTypeInfo() const {return _typeInfo;}
  //@}
  
  //@{ implement EntityActivity
  virtual bool Simulate(Entity *entity, float deltaT, SimulationImportance prec);
  virtual void Draw(Entity *entity){}
  /// man should be suspended while performing this activity
  //virtual bool SuspendEntity(const Entity *entity) const {return true;}
  //@}

  static void RegisterType() {Activity::RegisterType(&_typeInfo);}

  USE_FAST_ALLOCATOR
};

Activity *ActivityClassTypeInfoUIAction::CreateObject(ParamArchive &ar) const
{
  return new ActivityUIAction(NULL, NULL);
}

Activity *ActivityClassTypeInfoUIAction::LoadRef(ParamArchive &ar) const
{
  return NULL;
}

#include <Es/Memory/debugNew.hpp>

DEFINE_FAST_ALLOCATOR(ActivityUIAction)

ActivityUIAction::State::StateSimulation ActivityUIAction::_sim[] =
{
  &SimulateNothing,
  &SimulatePreload,
  &SimulateUIAction
};

ActivityUIAction::State::StateDraw ActivityUIAction::_draw[] =
{
  &DrawNothing
};

int ActivityUIAction::NStateSimulation() {return lenof(_sim);}
ActivityUIAction::State::StateSimulation ActivityUIAction::GetStateSimulation(int i) {return _sim[i];}
int ActivityUIAction::NStateDraw() {return lenof(_draw);}
ActivityUIAction::State::StateDraw ActivityUIAction::GetStateDraw(int i) {return _draw[i];}

/**
@param man never used, passed here only to make sure activity is registered for Man only
*/

ActivityUIAction::ActivityUIAction(const Action *action, Man *man)
:_action(unconst_cast(action)),_state(&ActivityUIAction::SimulatePreload,&ActivityUIAction::DrawNothing)
{
}

void ActivityUIAction::SimulateUIAction(Entity *entity, float deltaT)
{
  // actually fire weapon
  Man *man = static_cast<Man *>(entity);
  man->ProcessUIAction(_action);
  _state=State();
}

void ActivityUIAction::SimulatePreload(Entity *entity, float deltaT)
{
}

bool ActivityUIAction::Simulate(Entity *entity, float deltaT, SimulationImportance prec)
{
  if (_state._sim) (this->*_state._sim)(entity,deltaT);
  // we may preload required data of the target now
  return _state._sim;
}

void ActivityUIAction::OnEvent(ActivityEvent *event)
{
  ActivityEventAnimation *ev = static_cast<ActivityEventAnimation *>(event);
  switch (ev->GetState())
  {
  case ActivityEventAnimation::SStarted:
    break;
  case ActivityEventAnimation::SCanceled:
    // indicate activity has terminated
    _state = State();
    break;
  case ActivityEventAnimation::SFinished:
    _state = State(&ActivityUIAction::SimulateUIAction,&ActivityUIAction::DrawNothing);
    break;
  }
}
  
LSError ActivityUIAction::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))
  
  CHECK(ar.Serialize("action", _action, 1))
  CHECK(ar.Serialize("state", _state, 1))

  return LSOK;
}

ActivityClassTypeInfoUIAction ActivityUIAction::_typeInfo;

//////////////////////////////////////////////////////////////////////////
//
// ActivityGear and support classes
//

/// implement ClassTypeInfo for ActivityGear
class ActivityClassTypeInfoGear : public ActivityClassTypeInfo
{
  typedef ActivityClassTypeInfo base;

public:
  ActivityClassTypeInfoGear():base("Gear"){}
  //@{ implement ActivityClassTypeInfo
  virtual Activity *CreateObject(ParamArchive &ar) const;
  virtual Activity *LoadRef(ParamArchive &ar) const;
  //@}

};

#include <Es/Memory/normalNew.hpp>

/// handle Gear process
class ActivityGear : public ActivityAnimation
{
  typedef EntityActivity base;

  static ActivityClassTypeInfoGear _typeInfo;

  /// unit processing the action
  OLinkPermNO(AIBrain) _unit;
  /// container for the Gear action
  TargetId _target;
  /// dialog which should be displayed
  SRef<AbstractOptionsUI> _display;

  typedef ActivityState<ActivityGear> State;

  /// what state is active now
  State _state;

  void SimulatePreload(Entity *entity, float deltaT);
  void SimulateAction(Entity *entity, float deltaT);

  /// create the display based on the target type
  void CreateDisplay(UIActionType actionType = ATGear);

  //@{ support for serialization
  static State::StateSimulation _sim[];
  static State::StateDraw _draw[];

public:
  static int NStateSimulation();
  static State::StateSimulation GetStateSimulation(int i);
  static int NStateDraw();
  static State::StateDraw GetStateDraw(int i);
  //@}

  ActivityGear(TargetId target, Man *man, UIActionType actionType = ATGear);
  //@{ implement Activity
  virtual void OnEvent(ActivityEvent *event);
  virtual LSError Serialize(ParamArchive &ar);
  virtual const ActivityClassTypeInfo &GetTypeInfo() const {return _typeInfo;}
  //@}

  //@{ implement EntityActivity
  virtual bool Simulate(Entity *entity, float deltaT, SimulationImportance prec);
  virtual void Draw(Entity *entity){}
  /// man should be suspended while performing this activity
  //virtual bool SuspendEntity(const Entity *entity) const {return true;}
  //@}

  static void RegisterType() {Activity::RegisterType(&_typeInfo);}

  USE_FAST_ALLOCATOR
};

#include <Es/Memory/debugNew.hpp>

Activity *ActivityClassTypeInfoGear::CreateObject(ParamArchive &ar) const
{
  return new ActivityGear(NULL, NULL);
}

Activity *ActivityClassTypeInfoGear::LoadRef(ParamArchive &ar) const
{
  return NULL;
}


DEFINE_FAST_ALLOCATOR(ActivityGear)

ActivityGear::State::StateSimulation ActivityGear::_sim[] =
{
  &SimulateNothing,
  &SimulatePreload,
  &SimulateAction
};

ActivityGear::State::StateDraw ActivityGear::_draw[] =
{
  &DrawNothing
};

int ActivityGear::NStateSimulation() {return lenof(_sim);}
ActivityGear::State::StateSimulation ActivityGear::GetStateSimulation(int i) {return _sim[i];}
int ActivityGear::NStateDraw() {return lenof(_draw);}
ActivityGear::State::StateDraw ActivityGear::GetStateDraw(int i) {return _draw[i];}

/**
@param man never used, passed here only to make sure activity is registered for Man only
*/

ActivityGear::ActivityGear(TargetId target, Man *man, UIActionType actionType)
: _target(unconst_cast(target)), _unit(man ? man->Brain() : NULL),
  _state(&ActivityGear::SimulatePreload, &ActivityGear::DrawNothing)
{
  // create the display based on the target type
  CreateDisplay(actionType);
}

void ActivityGear::CreateDisplay(UIActionType actionType)
{
  if (!_unit->IsLocal()) return;

  EntityAI *target = _target;

  Display *CreateInventoryDialog(ControlsContainer *parent, AIBrain *me, EntityAI *source);
  _display = CreateInventoryDialog(NULL, _unit, target);
}

void ActivityGear::SimulateAction(Entity *entity, float deltaT)
{
  // actually process the action (show the dialog)
  if (_display)
  {
    GWorld->DestroyUserDialog();
    GWorld->SetUserDialog(_display);
  }
  // the activity is finished
  _state = State();
}

void ActivityGear::SimulatePreload(Entity *entity, float deltaT)
{
  // preload the dialog
  if (_display) _display->IsDisplayReady();
}

bool ActivityGear::Simulate(Entity *entity, float deltaT, SimulationImportance prec)
{
  if (_state._sim) (this->*_state._sim)(entity,deltaT);
  // we may preload required data of the target now
  return _state._sim;
}

void ActivityGear::OnEvent(ActivityEvent *event)
{
  ActivityEventAnimation *ev = static_cast<ActivityEventAnimation *>(event);
  switch (ev->GetState())
  {
  case ActivityEventAnimation::SStarted:
    break;
  case ActivityEventAnimation::SCanceled:
    // indicate activity has terminated
    _state = State();
    break;
  case ActivityEventAnimation::SFinished:
    _state = State(&ActivityGear::SimulateAction, &ActivityGear::DrawNothing);
    break;
  }
}

LSError ActivityGear::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))

  CHECK(ar.SerializeRef("Unit", _unit, 1))
  CHECK(ar.SerializeRef("Target", _target, 1))
  CHECK(ar.Serialize("state", _state, 1))

  if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassSecond)
    CreateDisplay();

  return LSOK;
}

ActivityClassTypeInfoGear ActivityGear::_typeInfo;

#include <Es/Memory/normalNew.hpp>
class ActionContextUIAction: public ActionContextAnimation
{
  typedef ActionContextAnimation base;

  UIActionType _actionType; // needed for action cancelling

  OLinkO(Man) _man;
  MoveIdExt _move;

  USE_FAST_ALLOCATOR;

public:
  ActionContextUIAction(ActivityAnimation *activity, MoveFinishF func, UIActionType actionType, Man *man, MoveIdExt move);
  UIActionType GetActionType() const {return _actionType;}

  virtual MoveId GetPlayMove() const override {return _move.id;}
};

#include <Es/Memory/debugNew.hpp>

DEFINE_FAST_ALLOCATOR(ActionContextAnimation)

ActionContextAnimation::ActionContextAnimation(ActivityAnimation *activity, MoveFinishF func)
: _getIn(activity,ActivityEventAnimation::SFinished),
  _anim(activity,ActivityEventAnimation::SStarted),
  _cancel(activity,ActivityEventAnimation::SCanceled)
{
  function = func;
}

DEFINE_FAST_ALLOCATOR(ActionContextUIAction)

ActionContextUIAction::ActionContextUIAction(ActivityAnimation *activity, MoveFinishF func, UIActionType actionType, Man *man, MoveIdExt move)
: ActionContextAnimation(activity, func), _actionType(actionType), _man(man), _move(move)
{
}

ActionContextBase *Man::CreateActionContextUIAction(const Action *action, ManAction moveAction)
{
  ActivityUIAction *act = new ActivityUIAction(action, this);
  _activities.Add(act);

  return new ActionContextUIAction(act, MFUIAction, action->GetType(), this, GetMove(moveAction));
}

ActionContextBase *Man::CreateActionContextGear(const Action *action)
{
  ActivityGear *act = new ActivityGear(dyn_cast<EntityAI>(action->GetTarget()), this, action->GetType());
  _activities.Add(act);

  return new ActionContextUIAction(act, MFUIAction, action->GetType(), this, GetMove(ManActGear));
}


void Man::CreateActivityGetIn(Transport *veh, UIActionType pos, Turret *turret, int cargoIndex)
{
  if (!IsActionInProgress(MFGetIn))
  {
    if (pos == ATGetInCargo && cargoIndex<0) // cargoIndex needn't be valid, then detect it 
    {
      int n = veh->GetManCargo().Size();
      for (int i=0; i<n; i++)
      {
        if (veh->GetManCargo()[i]==nullptr)
        {
          cargoIndex=i;
          break;
        }
      }
      Assert(cargoIndex>=0);
    }
    ActivityGetIn *act = new ActivityGetIn(veh,pos,turret,cargoIndex,this);
    _activities.Add(act);
  }
}


/*!
\patch 1.17 Date 8/14/2001 by Ondra.
- Fixed: Crew leg position after mounting vehicle on the hillside.
*/

void Man::OnGetInFinished()
{
  FutureVisualState()._legTrans = MIdentity;
  IRLaserWanted(false);
  DisableFlashLight();
  UpdateJoinedObject();

  if(_brain && _brain->GetVehicleIn())
    _brain->GetVehicleIn()->AddCrewWeight();

}

void Man::OnGetOutFinished()
{
  _afterGetOut = true;

  if(_brain && _brain->GetVehicleIn())
    _brain->GetVehicleIn()->RemoveCrewWeight();
}


DEFINE_FAST_ALLOCATOR(ActionContextDefault)

ActionContextDefault::ActionContextDefault()
{
  static const RStringB empty = "";
  param = 0;
  param2 = 0;
  param3 = empty;
}

ActionContextDefault::~ActionContextDefault()
{
}


static int GetUpDegree(CombatMode cm, bool handGun, bool hasWeapon, bool hasRifle)
{
  if (!hasWeapon) return ManPosNoWeapon;
  if (cm==CMSafe || cm==CMCareless)
  {
    if (!hasRifle) 
    {
      // we know he has some weapon, but no rifle - he needs to have handgun
      // we do not care - we want him to move as civilian anyway
      return ManPosNoWeapon;
    }
    return ManPosStand;
  }
  if (handGun) return ManPosHandGunStand;
  return ManPosCombat;
}

MovesTypeMan::MovesTypeMan()
{
}

MovesTypeMan::~MovesTypeMan()
{
}

void MovesTypeMan::Load(const MovesTypeName &name)
{
  ParamEntryVal moves = Pars>>name.skeletonName;
  if (moves.FindEntry("gestures"))
  {
    RStringB gestures = moves>>"gestures";
    
    _gestures = MovesTypesGestureMan.New(gestures);
  }

  base::Load(name);

  // register actions used by program
#define ACTION(x) #x,
  static const char *actionNames[] =
  {
#include "../cfg/manActions.hpp"
  };
#undef ACTION
  DoAssert(lenof(actionNames) == ManActN);
  for (int i=0; i<ManActN; i++) _actionIndices[i] = (ActionMap::Index)_actions.GetValue(actionNames[i]);
}

MoveId MovesTypeMan::GetMoveId(MoveLayer layer, RStringB name ) const
{
  switch (layer)
  {
    case MovePrimary:
      return GetMoveId(name);
    default:
      Assert(layer==MoveGesture);
      if (!_gestures) return MoveIdNone;
      return _gestures->GetMoveId(name);
  }
}

const MoveIdExt &MovesTypeMan::GetMove(const ActionMap *map, ManAction action) const
{
  ActionMap::Index index = _actionIndices[action];
  if (index == ActionMap::IndexNone) return MoveIdExt::none;

  return map->GetAction(index);
}

MovesTypeGestureMan::MovesTypeGestureMan(){}

MovesTypeGestureMan::~MovesTypeGestureMan(){}

const float TimeToRun=6;
const float TimeToCrawl=8;
const float WaitBeforeStandUp=TimeToRun+TimeToCrawl;

DEFINE_CASTING(Man)

const float MinMoveSpeed = 0.05f;

Man::Man(const EntityAIType *name, bool fullCreate)
:Person(name, fullCreate),
_moves(ManPosStand),
_gesture(-1), // up degree not used for gestures
_walkToggle(false), _walkRunTempLocked(false), _walkForced(false), _customRecoilCoefficient(1.0f),
#if _VBS2
_isWalking(true),
#else
_isWalking(false),
#endif
_leanLeftLocked(false), _leanLeftToggle(false), _leanRightLocked(false), _leanRightToggle(false),
_inBuilding(false),
_pipeBombsAuto(false),
_doSoundStep(false), _doMark(0),
_slideSoundRunning(false),
_freeFall(false), _freeFallUntil(Glob.time), _freeFallStartTime(TIME_MAX),
_swimming(false),
_stillMoveQueueEnd(MoveIdNone), _variantTime(Glob.time),

_targetMoveWanted(MoveIdNone),
_targetMove(MoveIdNone),

_useAudioTimeForMoves(false),
//_launchDelay(3),

_aimInaccuracyX(0), _aimInaccuracyY(0),
#if _VBS2 // fatigue model
_aimSwayAngle(0),  // For the figure-8 sway of the weapon
_breathsPerMinute(MIN_BREATH_RATE),
#endif
_aimInaccuracyDist(0), _lastInaccuracyTime(Glob.time), _lastInaccuracyDistTime(Glob.time),

_lastObjectContactTime(Glob.time), _lastMovementTime(Glob.time),

_waterDepth(0),
_unitPosCommanded(UPAuto), _unitPosScripted(UPAuto), _unitPosFSM(UPAuto),
_landGradFwd(0), _landGradAside(0),
_forceStand(false), _slideSpeed(0),

_hideBody(0), _hideBodyWanted(0), // hide body state

_primaryMoveBackUp(ENUM_CAST(MoveId, MoveIdNone)), _secondaryMoveBackUp(ENUM_CAST(MoveId, MoveIdNone)),

_hasNVG(false),
_lightWanted(false),
_lightForced(false),
_irWanted(false),
_irEnabled(false),

_upDegreeStable(ManPosStand),

_posWanted(ManPosStand), _posWantedTime(TIME_MAX), // no change waiting
_forceBinocUntil(TIME_MIN),

_coverState(CoverNo), _inOpenCover(false), _coverEntered(0),

_walkSpeedWantedX(0), _walkSpeedWantedZ(0), _walkSpeedFineControl(false), _walkUntil(TIME_MIN),

_whenScreamed(TIME_MAX), _whenHit(TIME_MIN), _howMuchHit(0), _hitDirection(VZero),

_tired(0), _canMoveFast(true), _breathAir(1.0), _holdBreath(0),

_vegCollisionTime(TIME_MIN),


_metabolismFactor(1.0f),

_showPrimaryWeapon(true), _showSecondaryWeapon(true), _showHead(true),
  _secondShoulderOffsetLocal(VZero), _secondShoulderOffsetValid(false),
_manScale(1),

_coverGrassHeight(0.0f),
_coverGrassHeightWanted(0.0f),
_nvg(false),
_flir(false),
_wasNVGStateSet(true),
_wasNVGActive(false),
_useVisionModes(false),
_sumaShakingVector(VZero),

// turret controls
_gunYRot(0), _gunXRot(0),
_gunYRotWanted(0), _gunXRotWanted(0), _gunXRotWantedNoTrembleHands(0), _gunYRotWantedNoTrembleHands(0),
_gunYRotIronSightBackup(0),
_gunXRotDelay(0), _gunYRotDelay(0),
_gunXSpeed(0), _gunYSpeed(0),
_gunXRecoil(0), _gunYRecoil(0), _gunZRecoil(0),
_renderDepthOfFieldDistance(-1),

_headYRot(0), _headYRotWanted(0), _headYRotWantedCont(0), _headYRotWantedPan(0), _headYRotWantedAuto(0), //head
_headXRot(0), _headXRotWanted(0), _headXRotWantedCont(0), _headXRotWantedPan(0), _headXRotWantedAuto(0),
_headSmoothMove(false),
_lookYRot(0), _lookYRotWanted(0), //look
_lookXRot(0), _lookXRotWanted(0),
_zoom(),
_zoomInToggled(false),
_zoomOutToggled(false),
_lookAroundViewPars(NULL),

_leanZRot(0),
_leanZRotWanted(0),
_hitSkeletonRot(0),
_hitSkeletonRotWanted(0),

_lookForwardTimeLeft(0),_lookTargetTimeLeft(0),
_leaveCoverSemaphore(0),

// initialize ladder state
_ladderBuilding(NULL),
_ladderIndex(-1),
_ladderPosition(0), // 0..1 - position on ladder, 0 = bottom
_ladderPositionLast(0),

_isAttached(false),
_fireEnabled(false),
_turnWanted(0),
#if !_ENABLE_NEWHEAD
_head(Type()->_head, _shape, GetFaceType()),
#endif
_staticCollisionUntil(Glob.time),
_landContactPos(0,-1000000.0f,0), // initialization - everything is over landcontact
_objectLandContact(false),
_collVolume(INFINITY_VOLUME),
_objectContactNonStatic(false),
_staticCollision(true),
_walkingOnObject(false),
_bodyPosition(0),
_safeSystem(true),
_useSafeCapsule(false),
#if _ENABLE_DIRECT_MESSAGES
_directSpeaking(CCDirectSpeaking, Pars >> "RadioChannels" >> "DirectSpeakingChannel"),
#else
_directSpeaking(CCDirect, Pars >> "RadioChannels" >> "DirectSpeakingChannel"),
#endif
_landDX(0),
_landDZ(0),
_afterGetOut(false),
_ignoreCollisionIfStuck(false),
_stuckStarted(TIME_MAX),
_allowBowDraw(true),
_drawingBow(false),
_pullingBowString(false),
_bowStringOffset(0.f),
_damageTimeToLive(0),
_corpseTime(0),
_damageHitSelect(0),
_breathSelect(0)
#if _ENABLE_PHYSICS
,_ragdollCtrl(0)
,_ragdollMode(0)
#endif
{
  SetSimulationPrecision(AI_MAX_SIM_TIME,AI_MAX_SIM_TIME); 
  RandomizeSimulationTime();
  SetTimeOffset(AI_MAX_SIM_TIME);

  RecalcGunTransform();
  RecalcLeaningTransform();
  RecalcAffectSkeletonTransform();
  RecalcShakeTransform(VZero,0.0f);

  _breathSelect = toIntCeil(GRandGen.RandomValue() * Type()->_breathSound.Size());
  saturate(_breathSelect, 0, Type()->_breathSound.Size() - 1);

  _damageHitSelect = toIntCeil(GRandGen.RandomValue() * Type()->_damageHitSound.Size());
  saturate(_damageHitSelect, 0, Type()->_damageHitSound.Size() - 1);

  _destrType=DestructMan;

  //_manScale = GRandGen.PlusMinus(1,0.05f);
  _manScale = GRandGen.PlusMinus(1,0.9f);

  //SetScale(_manScale);
#if _ENABLE_NEWHEAD
  SetFace("Default");
  //SetHead("Default");
#endif
#if _ENABLE_CONVERSATION
  { // prototype implementation: set variables needed by the conversation system
    _vars.VarSet("randomValue", 5.0f);
    _vars.VarSet("morale", 5.0f);
  }
#endif
#if _ENABLE_NEWHEAD
  if (_headType.NotNull()) _head.SetGrimace(_headType->_grimaceIndexNeutral);

  // make indices easily accessible
  const ManType *type = Type();
  _head._headBone = type->_headBone;
  _head._lEye = type->_lEye;
  _head._rEye = type->_rEye;
  _head._lEyelidUp = type->_lEyelidUp;
  _head._rEyelidUp = type->_rEyelidUp;
  _head._lEyelidDown = type->_lEyelidDown;
  _head._rEyelidDown = type->_rEyelidDown;
  _head._lPupil = type->_lPupil;
  _head._rPupil = type->_rPupil;
#else
  _head.SetMimicMode("neutral");
#endif
  ScanEquipment();

#if !_ENABLE_NEWHEAD
  if (!_head.GetFace())
  {
    RptF("White head warning: missing face texture on %s (after construction)", cc_cast(GetDebugName()));
  }
#endif
  _zoom.Init(this);

  // Backup's initialization to default values
  Moves moves(true);
  _primaryMoveBackUp = moves.GetPrimaryMove(FutureVisualState()._moves);
  _secondaryMoveBackUp = moves.GetSecondaryMove(FutureVisualState()._moves);
  _primaryFactorBackUp = moves.GetPrimaryFactor(FutureVisualState()._moves); 
  _primaryTimeBackUp = moves.GetPrimaryTime(FutureVisualState()._moves);
  _primaryTimeDBackUp = moves.GetPrimaryTimeDelta(FutureVisualState()._moves);
  _secondaryTimeBackUp = moves.GetSecondaryTime(FutureVisualState()._moves);
  _secondaryTimeDBackUp = moves.GetSecondaryTimeDelta(FutureVisualState()._moves);
  _finishedBackUp = _moves.GetFinished(FutureVisualState()._moves);
  _legTransBackUp = FutureVisualState()._legTrans;
  _hideBodyBackUp = _hideBody;
  _waterDepthBackUp = _waterDepth;
  _landContactBackUp = _landContact;
  _waterContactBackUp = _waterContact;
  _surfaceSoundBackUp =  _surfaceSound;
  _speedBackUp = FutureVisualState()._speed;
  _angMomentumBackUp = _angMomentum;
  _lastRemotePos = FutureVisualState().Position();
  _staticCollisionBackUp = _staticCollision;
  _useSafeCapsuleBackUp = _useSafeCapsule;

  for (int i=0; i<NInventorySlotId; i++)
  {
    const InventoryItemType *itemType = type->_emptySlots[i];
    if (itemType)
    {
      _emptySlots[i] = dyn_cast<InventoryItem>(GWorld->NewVehicleWithID(itemType));
      //we need to know that we are someone's proxy
      if(_emptySlots[i])
        _emptySlots[i]->SetMoveOutDone(this);

    }
  }

  _raiseWeaponActions.Init();
}

Man::~Man()
{
  if (_isAttached)
  {
    GWorld->RemoveAttachment(this);
  }
#if _ENABLE_NEWHEAD
  RemoveGlasses();
  RemoveHead();
#endif
}

#pragma warning(disable:4065)

int Man::InsideLOD(CameraType camType) const
{
  return Type()->_insideView;
}

void Man::BlendMatrix(Matrix4 &mat,const Matrix4 &trans,float factor) const
{
  if (factor >= Moves::_primaryFactor1)
  {
    mat = trans * mat;
  }
  else if (factor > Moves::_primaryFactor0)
  {
    // interpolation necessary
    mat = (trans*factor + MIdentity*(1-factor))*mat;
  }

}

#define BLEND_ANIM(name) \
  BlendAnimSelectionsStorage name##Storage; \
  BlendAnimSelections name; \
  AUTO_STORAGE_ALIGNED(BlendAnimInfo,name##Memory,32,int); \
  name##Storage.Init(name##Memory,sizeof(name##Memory)); \
  name.SetStorage(name##Storage);

#if _ENABLE_PHYSICS1
enf::BulletBoneController* GetRagdollBone(enf::BulletRagdollController* ctrl, SkeletonIndex bone)
{
	for(int i = 0; i < ctrl->GetNumBones(); ++i)
	{
		if(ctrl->m_Bones[i]->m_iOriginalIndex == bone)
			return ctrl->m_Bones[i];
	}
	return 0;
}

enf::BulletBoneController* GetRagdollParent(Skeleton* sk, enf::BulletRagdollController* ctrl, SkeletonIndex bone)
{
	//fskutecnosti je to prvni s vetsim indexem
	int pIdx = 0;	//root
	for(int i = 0; i < ctrl->GetNumBones(); ++i)
	{
		int idx = ctrl->m_Bones[i]->m_iOriginalIndex;
		if(idx > pIdx && idx <= bone)
			pIdx = idx;
	}

	return ctrl->FindBoneByNodeIndex(pIdx);
}
#endif

/*!
\patch_internal 1.01 Date 6/26/2001 by Ondra. Fixed: animate matrix connected with head.
- this was required because of drawing Night Vision.
*/

// ANIMS-FREELOOK: devolve uma fracao da rotacao contida em "trans"
// (mesma convencao de eixos/pivo, so com o angulo reduzido).
static Matrix4 PartialHeadTrans(const Matrix4 &trans, float factor)
{
  if (factor<=0.001f) return MIdentity;
  if (factor>=0.999f) return trans;
  Matrix3 o = trans.Orientation();
  Quaternion<float> q0(o), qId(M3Identity);
  Matrix3 partial;
  qId.Slerp(q0,factor).UnitToMatrix(partial);
  Matrix4 res = trans;
  res.SetOrientation(partial);
  res.SetPosition(trans.Position()*factor);
  return res;
}

// ANIMS-FREELOOK: descobre os ossos da coluna (cache por skeleton).
static void GetFreeLookSpineBones(const LODShape *shape, SkeletonIndex &spine2, SkeletonIndex &spine3)
{
  spine2 = spine3 = SkeletonIndexNone;
  if (!shape) return;
  spine2 = shape->FindBone("spine2");
  spine3 = shape->FindBone("spine3");
}

void Man::AnimateBoneMatrix(Matrix4 &mat, const ObjectVisualState &vs, int level, SkeletonIndex boneIndex) const
{
#if _ENABLE_PHYSICS1
	if(_ragdollCtrl)
	{
		//enf::BulletBoneController* parent = GetRagdollParent(GetShape()->GetSkeleton(), _ragdollCtrl, boneIndex);
		//mat = parent->m_BoneMx; 
		//return;
	}
#endif

	const ManVisualState &mvs = vs.cast<Man>();
  base::AnimateBoneMatrix(mat,vs,level,boneIndex);
  if (SkeletonIndexValid(boneIndex))
  {
    BlendAnims blend;
    GetBlendAnims(mvs,blend);
    // now perform the blending
    _moves.AnimateBoneMatrix(blend,GetMovesType(),mat,boneIndex);
  }

  if (boneIndex != SkeletonIndexNone)
  {
    if (!mvs._headTransIdent)
    { 
      BLEND_ANIM(head);
      const BlendAnimSelections &headRes = GetHead(mvs,head);

      float headFactor = 0;
      for (int bi = 0; bi<headRes.Size(); bi++)
      {
        const BlendAnimInfo &blend = headRes[bi];
        if (boneIndex==blend.matrixIndex) {headFactor=blend.factor;break;}
      }

      Matrix4 headTrans = mvs._headTrans;

      // ANIMS-FREELOOK: no free look o tronco acompanha ~35% da rotacao da
      // cabeca (divisao tronco/cabeca), em vez de girar apenas pescoco/cabeca.
      AIBrain *brainLook = Brain();
      if (brainLook && brainLook->LookAroundEnabled())
      {
        SkeletonIndex spine2 = SkeletonIndexNone;
        SkeletonIndex spine3 = SkeletonIndexNone;
        GetFreeLookSpineBones(GetShape(),spine2,spine3);

        bool isSpine2 = (spine2!=SkeletonIndexNone && boneIndex==spine2);
        bool isSpine3 = (spine3!=SkeletonIndexNone && boneIndex==spine3);
        if (isSpine2 || isSpine3)
        {
          // spine2 + spine3 somam 35% (a cabeca recebe os 65% restantes)
          Matrix4 torsoTrans = PartialHeadTrans(mvs._headTrans, isSpine2 ? 0.15f : 0.20f);
          BlendMatrix(mat,torsoTrans,1.0f);
        }
        headTrans = PartialHeadTrans(mvs._headTrans,0.65f);
      }

      // check matrix aiming factor
      BlendMatrix(mat,headTrans,headFactor);
    }
    AIBrain *unit = Brain();
    if (!mvs._leaningTransIdent || (unit && !mvs._shakingTransIdent && !unit->IsFreeSoldier()))
    {
      BLEND_ANIM(leaning);
      const BlendAnimSelections &leaningRes = GetLeaning(mvs,leaning);

      float leaningFactor = 0;
      for (int bi = 0; bi<leaningRes.Size(); bi++)
      {
        const BlendAnimInfo &blend = leaningRes[bi];
        if (boneIndex==blend.matrixIndex) {leaningFactor=blend.factor;break;}
      }

      // check matrix aiming factor
      if (!mvs._leaningTransIdent)
        BlendMatrix(mat,mvs._leaningTrans,leaningFactor);
      if (unit && !mvs._shakingTransIdent && !unit->IsFreeSoldier())
        BlendMatrix(mat,mvs._shakingTrans,leaningFactor);
    }

    if (!mvs._gunTransIdent)
    {
      {
        BLEND_ANIM(aiming);
        const BlendAnimSelections &aimingRes = GetAiming(mvs,aiming);

        float aimingFactor = 0;
        for (int bi = 0; bi<aimingRes.Size(); bi++)
        {
          const BlendAnimInfo &blend = aimingRes[bi];
          if (boneIndex==blend.matrixIndex)
          {
            aimingFactor=blend.factor;
            break;
          }
        }

        // check matrix aiming factor
        BlendMatrix(mat,mvs._gunTrans,aimingFactor);
      }
      {
        BLEND_ANIM(aimingBody);
        const BlendAnimSelections &aimingBodyRes = GetAimingBody(mvs,aimingBody);

        float aimingFactor = 0;
        for (int bi = 0; bi<aimingBodyRes.Size(); bi++)
        {
          const BlendAnimInfo &blend = aimingBodyRes[bi];
          if (boneIndex==blend.matrixIndex) {aimingFactor=blend.factor;break;}
        }

        // check matrix aiming factor
        BlendMatrix(mat,mvs._gunBodyTrans,aimingFactor);
      }
    }

    // during crawling gun matrix may be influences by "legs"
    {
      BLEND_ANIM(legs);
      const BlendAnimSelections &legsRes = GetLegs(mvs,legs);

      float legsFactor = 0;
      for (int bi = 0; bi<legsRes.Size(); bi++)
      {
        const BlendAnimInfo &blend = legsRes[bi];
        if (boneIndex==blend.matrixIndex)
        {
          legsFactor=blend.factor;
          break;
        }
      }

      BlendMatrix(mat,mvs._legTrans,legsFactor);
    }

    //mat.Orthogonalize();
  }
}


Vector3 Man::COMPosition(ObjectVisualState const& vs) const
{
#if _ENABLE_ATTACHED_OBJECTS
  if (IsAttached()) return base::COMPosition(vs);
#endif
  // COM is animated, it is somewhere near aiming position
  return AimingPosition(vs);
}

Vector3 Man::ApplyNotRTMAnimations(const ManVisualState &vs, Vector3Par src, int level, int index) const
{
  ShapeUsed shape = _shape->Level(level);

  Vector3 res = src;
  if (Type()->_headAxisPoint>=0 && !vs._headTransIdent)
  {
    BLEND_ANIM(head);
    const BlendAnimSelections &headRes = GetHead(vs,head);
    AnimationRT::TransformPoint
      (
      res,shape.GetShape(),vs._headTrans,
      headRes.Data(),headRes.Size(),index
    );
  }
  if (Type()->_leaningAxisPoint>=0 && !vs._leaningTransIdent)
  {
    BLEND_ANIM(leaning);
    const BlendAnimSelections &leaningRes = GetLeaning(vs,leaning);
    AnimationRT::TransformPoint(
      res,shape.GetShape(),vs._leaningTrans,
      leaningRes.Data(),leaningRes.Size(),index
    );
  }
  AIBrain *unit = Brain();
  if (unit && Type()->_leaningAxisPoint>=0 && !vs._shakingTransIdent && !unit->IsFreeSoldier())
  {
    BLEND_ANIM(leaning);
    const BlendAnimSelections &leaningRes = GetLeaning(vs,leaning);
    AnimationRT::TransformPoint(
      res,shape.GetShape(),vs._shakingTrans,
      leaningRes.Data(),leaningRes.Size(),index
      );
  }
  if (Type()->_aimingAxisPoint>=0 && !vs._gunTransIdent)
  {
    {
      BLEND_ANIM(aimingBody);
      const BlendAnimSelections &aimingBodyRes = GetAimingBody(vs,aimingBody);
      AnimationRT::TransformPoint(
        res,shape.GetShape(),vs._gunBodyTrans,
        aimingBodyRes.Data(),aimingBodyRes.Size(),index
      );
    }
    {
      BLEND_ANIM(aiming);
      const BlendAnimSelections &aimingRes = GetAiming(vs,aiming);
      AnimationRT::TransformPoint(
        res,shape.GetShape(),vs._gunTrans,
        aimingRes.Data(),aimingRes.Size(),index
      );
    }
  }

  // combine with transformation of res by legs
  BLEND_ANIM(legs);
  const BlendAnimSelections &legsRes = GetLegs(vs,legs);
  AnimationRT::TransformPoint(
    res,shape.GetShape(),vs._legTrans,
    legsRes.Data(),legsRes.Size(),index
  );
  return res;
}

Vector3 Man::AnimatePoint(const ObjectVisualState &vs, int level, int index) const
{
  const ManVisualState &mvs = vs.cast<Man>();
  
  BlendAnims blend;
  GetBlendAnims(mvs,blend);
  Vector3 res = _moves.AnimatePoint(blend,GetMovesType(), _shape, level, index);

  return ApplyNotRTMAnimations(mvs,res,level,index);
}

Vector3 Man::AnimatePointNeutral(const ObjectVisualState &vs, int level, int index) const
{
  const ManVisualState &mvs = vs.cast<Man>();
  
  BlendAnims blend;
  GetBlendAnimsNeutral(vs.cast<Man>(),blend);
  
  Vector3 res = _moves.AnimatePoint(blend,GetMovesType(), _shape, level, index);

  return ApplyNotRTMAnimations(mvs,res,level,index);
}

bool Man::HasFlares(CameraType camType) const
{
  // check current weapon
  if (camType==CamGunner)
  {
    // check current weapon
    WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
    if (ptr.IsValid())
    {
      const OpticsInfo *opticsInfo = ptr.GetOpticsInfo();
      if (opticsInfo) return opticsInfo->_opticsFlare;
    }
  }
  return base::HasFlares(camType);
}


// WIP:HEADBOB
// this is handled by the HeadBob cheat (RALT+RWIN+H), see World::Simulate() in world.cpp
int HeadBobCheatMode = 7;

/*!
\patch 5133 Date 2/25/2007 by Ondra
- Fixed: Iron sight view no longer strictly tied to a weapon.
*/

Matrix4 Man::InsideCamera(CameraType camType) const
{
  const VisualState &vs = RenderVisualStateScope();
  
  if (camType == CamGunner)
  {
    WeaponModePtr selected = _weaponsState.ValidatedCurrentWeapon();
    Matrix4 weapon = GetWeaponRelTransform(vs, selected);
    
    // apply a little bit of sideways movement
    // the question is what should be the source of the movement
    
    // as a 1st attempt we use X/Y/Z rotations as a basis for the movement
    
    // compare animTrans and _gunTrans
    // _gunTrans is rotation only
    Matrix4 animChange = weapon * vs._leaningTrans.InverseRotation() * vs._gunTrans.InverseRotation() * vs._gunBodyTrans.InverseRotation();

    static Vector3 angleMod(0.05f,0.10f,0.10f);
    static Vector3 recoilMod(0.002f,0.005f,2.0f);
    static Vector3 speedMod(0.004f,0.004f,0);
    float xRot = animChange.Direction().Y();
    float yRot = animChange.Direction().X();
    float zRot = animChange.DirectionAside().Y();
    
    Vector3 offset = Vector3(yRot,xRot,zRot).Modulate(angleMod);
    offset += Vector3(0,0,_gunZRecoil).Modulate(recoilMod);
    float turnY = _gunYSpeed+_turnWanted;
    static float maxTurn = 0.5f;
    saturate(turnY,-maxTurn,+maxTurn);
    offset += Vector3(turnY,_gunXSpeed,0).Modulate(speedMod);
    
   if (!GetOpticsModel(this))
    {
      // 3D iron sights may choose camera transformation more freely
      int index = Type()->_pilotPoint;
      if (index >= 0)
      {
        // link to head movement
        // we cannot use the real eye position
        // we need somehow to what the neutral position is and then offset it
        int level = _shape->FindMemoryLevel();
        Vector3Val pilotPos = AnimatePoint(vs,level, index);
        Vector3Val pilotPosBase = AnimatePointNeutral(vs,level, index);

        // 0 means animation drives fully, 1 means animation has no effect
        static Vector3 animMod(0.07f,0.07f,1);
        offset -= (pilotPos-pilotPosBase).Modulate(animMod);
      }
      weapon.SetOrientation(vs._lookTrans.Orientation()*vs._gunBodyTrans.Orientation());
    }
    else
    {
      int index = Type()->_pilotPoint;
      if (index >= 0)
      {
        // limit Z offset to prevent optics bringing us too forward, as that makes z-fighting fix impossible
        int level = _shape->FindMemoryLevel();
        Vector3Val pilotPos = AnimatePoint(vs,level, index);
        // if weapon is too much forward, limit it
        static float maxOpticsForward = 0.1f;
        // limiting must be done in the weapon direction, so that there is no offset to aiming
        // compute the offset in weapon direction
        float weaponToEye = weapon.Direction()*(weapon.Position()-pilotPos);
        if (weaponToEye>maxOpticsForward)
        {
          weapon.SetPosition(weapon.Position()-(weaponToEye-maxOpticsForward)*weapon.Direction());
          Assert(weapon.Direction()*(weapon.Position()-pilotPos)<maxOpticsForward+0.001f); // verify we are in limit now
        }
      }
      
      // for 2D optics we need to link strictly to the weapon
      weapon.SetDirectionAndUp(weapon.Direction(), VUp);
    }
    
    weapon.SetPosition(weapon.Position() + offset);

    // add camera shake
    GWorld->GetCameraShakeManager().UpdateCameraTransform(weapon);

    return weapon;
  }

  int index = -1;
  if (camType==CamExternal || camType==CamGroup)
  {
    index = Type()->_camTgt;
  }
  if (index < 0) index = Type()->_pilotPoint;
  if (index < 0) return MIdentity;

  // final camera transformation
  Matrix4 transform;
  int level = _shape->FindMemoryLevel();
  Vector3Val pilotPos = AnimatePoint(vs,level, index);
  transform.SetPosition(pilotPos);
  transform.SetOrientation(HeadBobOrientation(GetVisualStateAge(vs),camType,true));

  // add camera shake
  if (CamInternal == camType)
  {
    GWorld->GetCameraShakeManager().UpdateCameraTransform(transform);
  }

#if _VBS3 && _VBS_TRACKING
  // Use configuration file, if the view port is centered for
  // this object.
  const ManType *type = static_cast<const ManType*>(GetType());
  if(type->_viewPortCentered)
  {   
    // set the orientation and position to zero.
    transform.SetOrientation(M3Identity);
    transform.SetPosition(VZero);
  }
#endif
  
  return transform;
}

Matrix3 Man::HeadBobOrientation(const VisualStateAge vsa, CameraType camType, bool setEyes) const
{
  int index = -1;
  if (camType==CamExternal || camType==CamGroup)
    index = Type()->_camTgt;
  if (index < 0)
    index = Type()->_pilotPoint;
  if (index < 0)
    return M3Identity;

  // head bob
  Matrix4 headBobMatrix = MZero;
  // WIP:HEADBOB: remove global head bob cheat (ALT-WIN-H) completely?
  const ManType *type = Type();
  const Skeleton *skeleton = GetMovesType()->GetSkeleton();

  ObjectVisualState const& vs = GetVisualStateByAge(vsa);
  const ManVisualState &mvs = vs.cast<Man>();

  BlendAnims headBlend;
  GetBlendAnims(mvs,headBlend);
  int animCount = headBlend._list.Size();
  int gestCount = headBlend._masked.Size();
  bool cutSceneMode = false;
  float headBobWeight = 0.0f;
  for (int i = 0; i < animCount + gestCount; i++)
  {
    const AnimBlendInfo &animInfo = (i < animCount) ? headBlend._list.Get(i) : headBlend._masked.Get(i - animCount);

    float animTime = animInfo._timeInAnimation;
    float animFactor = animInfo._b_factor;
    const AnimationRTLock &animation = animInfo._lock;
    const MoveInfoManCommon *moveInfo = static_cast<const MoveInfoManCommon*>(animInfo._moveInfo);
    float headBobStrength = moveInfo->HeadBobStrength();
    HeadBobModeEnum headBobMode = moveInfo->HeadBobMode();

    bool animIsCutScene = (headBobMode == HeadBobRaw) || (headBobMode == HeadBobCutScene);
    if (animIsCutScene)
      cutSceneMode = true;

    bool animIsStatic = (moveInfo->GetSpeed() > 1e9f);

    if (animIsStatic && !animIsCutScene && ((HeadBobCheatMode == 6) || (HeadBobCheatMode == 7)))
      // static in-game animation (no cut-scene) -> head bob must be ignored
      headBobStrength = 0.0f;
    else
    {
      if (headBobStrength < 0.0f)
        // strength given fully by the animation
        headBobStrength = -headBobStrength;
      else
        // strength modifiable by the user-defined "head bob effect reduction" factor
        headBobStrength *= Glob.config.headBob;
      saturate(headBobStrength, 0.0f, 1.0f);

      if ((HeadBobCheatMode != 7) && (headBobStrength > Moves::_primaryFactor0))
        headBobStrength = 1.0f;
      if ((HeadBobCheatMode != 6) && (HeadBobCheatMode != 7))
      {
        headBobMode = (HeadBobModeEnum)HeadBobCheatMode;
        headBobStrength = (HeadBobCheatMode == 0) ? 0.0f : 1.0f;
      }
    }

    // head bob with non-zero strength and undef mode is invalid
    Assert((headBobStrength < Moves::_primaryFactor0) || (headBobMode != HeadBobUndef));

    Matrix4 partMatrix = MIdentity;
    if (headBobStrength > Moves::_primaryFactor0)
    {
      // get the head bob matrix for the given animation (using the right bone)
      SkeletonIndex partBoneIndex = type->GetHeadBone(headBobMode == HeadBobCutScene);
      BlendAnims partBlend;
      partBlend._list.Add(animInfo);
      partBlend.DoBoneAnim(partMatrix, skeleton, partBoneIndex);
      partMatrix.Orthogonalize();

      // "straight" view (which is modified in straight horizon mode)
      Matrix4 zeroStrengthMatrix = MIdentity;

      // eventual head bob correction
      if ((headBobMode == HeadBobBeginEnd) || (headBobMode == HeadBobAverageFrame))
        // apply begin/end or average correction
        partMatrix = partMatrix * animation->GetHeadBobCorrection(headBobMode, animTime, animation, skeleton, partBoneIndex);
      else if (headBobMode == HeadBobStraightHorizon)
      { // straight horizon
        // we want to interpolate between fully aligned and unaligned horizon
        zeroStrengthMatrix = partMatrix;
        Vector3Val dir = partMatrix.Direction();
        // this is a vector product of dir and the Y axis
        Vector3 aside = Vector3(dir.Z(), 0.0, -dir.X());
        partMatrix.SetDirectionAndAside(dir, aside);
      }

      // blend between "full head bob" and "straight" views according to the corresponding head bob strength
      partMatrix = partMatrix * headBobStrength + zeroStrengthMatrix * (1.0 - headBobStrength);
    }
    headBobMatrix += partMatrix * animFactor;
    headBobWeight += fabs(animFactor);
  }
  // Animation transitions can briefly have no weighted pose. Orthogonalizing
  // a zero matrix produces an unstable view orientation in those frames.
  if (headBobWeight > 1e-6f)
    headBobMatrix.Orthogonalize();
  else
    headBobMatrix = MIdentity;

  // final camera transformation
  Matrix3 pilotOrient;
  // we really want a different look-around: around the neck in cut-scenes x around the vertical axis in the game

  Matrix3 lookDir = M3Identity;
  if (setEyes)
    lookDir = mvs._lookTrans.Orientation();

  if (cutSceneMode)
    pilotOrient = headBobMatrix.Orientation() * mvs._gunBodyTrans.Orientation() * lookDir;
  else
    pilotOrient = lookDir * mvs._gunBodyTrans.Orientation() * headBobMatrix.Orientation();

  return pilotOrient;
}

Vector3 Man::GetCameraDirection(CameraType cam) const
{
  const VisualState &vs = RenderVisualStateScope();
  return vs.DirectionModelToWorld(vs._lookTrans.Orientation()*vs._gunBodyTrans.Direction());
}

Vector3 Man::ExternalCameraPosition(CameraType camType) const
{
  return (camType == CamGroup) ? Type()->_groupCameraPosition : Type()->_extCameraPosition;
}

Vector3 Man::GetSpeakerPosition() const
{
  return EyePosition(FutureVisualState());
}

bool Man::IsVirtual(CameraType camType) const
{
  return true;
}
bool Man::IsVirtualX(CameraType camType) const
{
  if (IsLookingAround()) return true;
  return camType!=CamInternal && camType!=CamExternal;
}
bool Man::IsGunner(CameraType camType) const
{
  return camType==CamGunner || camType==CamInternal || camType==CamExternal;
}

/*!
\patch 5088 Date 11/15/2006 by Bebul
- Fixed: Command interface, with ALT held should move cursor
*/

void Man::OverrideCursor(Vector3 &dir) const
{
  if (!QIsManual()) return;
  // get cursor direction from eye wanted direction
  TurretContextV context;
  context._turret = NULL;
  context._turretType = NULL;
  context._gunner = unconst_cast(this);
  context._weapons = unconst_cast(&_weaponsState);
//  context._parentTrans = Orientation();
  if (!WeaponsDisabled())
  {
    Vector3 gunAxis=GetWeaponCenterMoves(RenderVisualState());        
    Matrix4Val gunTransWanted= (
      Matrix4(MTranslation,gunAxis)*
      Matrix4(MRotationY,_gunYRotWanted)*
      Matrix4(MRotationX,-_gunXRotWanted)*
      Matrix4(MTranslation,Vector3(0,0,-_gunZRecoil)-gunAxis)
      );

    dir = gunTransWanted.Direction();
    RenderVisualState().DirectionModelToWorld(dir,dir);
  }
  else
    dir = GetEyeDirectionWantedNoAddCont(RenderVisualState(), context);
}

bool Man::IsCommander(CameraType camType) const {return true;}
bool Man::ShowAim(const TurretContextV &context, const WeaponModePtr &ptr, CameraType camType, Person *person) const
{
  //return !QIsManual();
  if (WeaponsDisabled()) return false;
  if (!ShowWeaponAim()) return false;
  return base::ShowAim(context, ptr, camType, person);
}
bool Man::ShowCursor(const TurretContextV &context, const WeaponModePtr &ptr, CameraType camType, Person *person) const
{
  //return !QIsManual();
#if _ENABLE_CHEATS
  if (CHECK_DIAG(DECombat)) return true;
#endif
  return camType!=CamGunner;
}

void Man::InitVirtual(CameraType camType, float &heading, float &dive, float &fov) const
{
  if (camType==CamExternal) camType=CamInternal;
  base::InitVirtual(camType,heading,dive,fov);
  switch(camType)
  {
    case CamGunner:
      dive=0;
      //fov
      WeaponModePtr curWeapon = _weaponsState.ValidatedCurrentWeapon();
      if (curWeapon.IsValid() && !IsDamageDestroyed())
      {
        const OpticsInfo *opticsInfo = curWeapon.GetOpticsInfo();
        DoAssert(opticsInfo);
        fov = opticsInfo->_opticsZoomInit;
        saturate(fov, opticsInfo->_opticsZoomMin, opticsInfo->_opticsZoomMax); //in order opticsZoomInit wrong
      }
      else
      {
        fov = 0.21f;
        float dummy=0;
        LimitVirtual(camType, dummy, dummy, fov);
      }
    break;
  }
}

void Man::LimitVirtual(CameraType camType, float &heading, float &dive, float &fov) const
{
  if (camType==CamExternal) camType=CamInternal;
  base::LimitVirtual(camType,heading,dive,fov);
  switch(camType)
  {
    case CamGunner:
    {
      // check current weapon
      WeaponModePtr curWeapon = _weaponsState.ValidatedCurrentWeapon();
      if (curWeapon.IsValid() && !IsDamageDestroyed())
      {
        const OpticsInfo *opticsInfo = curWeapon.GetOpticsInfo();
        DoAssert(opticsInfo);
        saturate(fov, opticsInfo->_opticsZoomMin, opticsInfo->_opticsZoomMax);
      }
      else
      {
        fov = 0.21f;
      }
      heading = 0;
      dive = 0;
      //saturate(heading,0,0);
      //saturate(dive,-0.7f,+0.8f);

    }
    break;
    case CamInternal:
      if (IsDamageDestroyed())
      {
        heading = 0;
        dive = 0;
        fov = GetType()->_viewPilot._initFov;
      }
      else if (QIsManual())
      {
        heading = 0;
        dive = 0;
      }
      // force camera direction to be equal to the eye direction
    break;
  }
}

const float WalkSpeed=1.5f;
const float WalkBackSpeed=1.5f;
const float RunSpeed=4.9f;
const float FastRunSpeed=7.2f;
const float TiredRunSpeed=4.9f;
const float InvFastRunSpeed=1/FastRunSpeed;
// based on current tired level we limit sideroll speed as well
const float SideRollSpeed = 1.7f;
const float SideRollTired = 0.6f;

//const float InvWalkSpeed=1/WalkSpeed;
//const float InvWalkBackSpeed=1/WalkBackSpeed;
//const float InvRunSpeed=1/RunSpeed;

const float TurnSpeed=1.0f;
const float TurnBackSpeed=0.7f;

#define DIAG_QUEUE 0

void Man::RefreshMoveQueue(bool enableVariants)
{ // select random variants
  if (Glob.time>=_variantTime)
  {
    const MoveInfoMan *stillInfo = Type()->GetMoveInfo(_stillMoveQueueEnd, AnimSubIx());
    // we may select another move instead of this one
    // playing move long enough - select variant
    if (stillInfo)
    {
      MoveId id = stillInfo->GetEquivalentTo();

      if (enableVariants)
      {
        id = IsNetworkPlayer() ? stillInfo->RandomVariantPlayer() : stillInfo->RandomVariantAI();
      }
      const MoveInfoMan *newInfo = Type()->GetMoveInfo(id, AnimSubIx());
      if (newInfo)
      {
        float wait = newInfo->GetVariantAfter();
        _variantTime = Glob.time+wait;
        MotionPathItem item(id);
        // TODO: keep same termination function
        // now we assume there is no termination function
        
        //LogF("%s: REFRESH refresh variant, wait %.2f",cc_cast(GetDebugName()),wait);
        #if DIAG_QUEUE
        LogF
        (
          "%s: Switch to variant %s of %s for %.2f",
          DNAME(),NAME_T(Type(),id),NAME_T(Type(),_stillMoveQueueEnd),
          _variantTime-Glob.time
        );
        #endif
        ChangeMoveQueue(item);
        return;
      }
    }
    // no random variant - you wait inifitelly
    _variantTime = Glob.time+600;
  }
}

RString Man::GetCurrentMove() const
{
  return _moves.GetCurrentMove(FutureVisualState()._moves,GetMovesType());
}

RString Man::GetCurrentMoveEq() const
{
  return _moves.GetCurrentMoveEq(FutureVisualState()._moves,GetMovesType());
}

bool Man::ChangeMoveQueue(MotionPathItem item)
{
#if DIAG_QUEUE
  LogF("%s: ChangeMoveQueue %s", DNAME(), NAME_T(Type(), item.id));
#endif

  bool animChanged = false;
  bool ok = _moves.ChangeMoveQueue(FutureVisualState()._moves,GetMovesType(), animChanged, item);
  if (animChanged) OnEvent(EEAnimChanged, GetCurrentMove());
  return ok;
}

bool Man::ChangeMoveQueueSetStillEnd(MotionPathItem item)
{
  bool ret = ChangeMoveQueue(item);
  if (ret)
  {
    _stillMoveQueueEnd = item.id;
  }
  return ret;
}


bool Man::SetMoveQueue(MotionPathItem item, bool enableVariants)
{
  #if 0 //_ENABLE_REPORT
    if (ToMoveOut() && GWorld->GetMode()==GModeNetware)
    {
      LogF
      (
        "%s: SetMoveQueue %s",
        cc_cast(GetDebugName()),
        NAME_T(Type(),item.id)
      );
    }
  #endif

  #if DIAG_QUEUE>=2
  LogF("SetMoveQueue %s",NAME_T(Type(),item.id));
  #endif
  
  if (item.id==MoveIdNone)
  {
    // invalid move - movement not possible
    if (item.context)
      ProcessMoveFunction(item.context);
#if DIAG_QUEUE
    LogF("Invalid move - no action");
#endif
    _moves.ResetMoveQueue();
    return false;
  }
  MoveId equivItemId = Type()->_moveType->GetEquivalent(item.id);
  MoveId equivStillId = Type()->_moveType->GetEquivalent(_stillMoveQueueEnd);
  //if (_queueMove.Size()<=0) equivStillId = MoveIdNone;
  if (equivItemId!=equivStillId)
  {
    // equivalent primary move changed - mark it
    _stillMoveQueueEnd = item.id;
    const MoveInfoMan *info = Type()->GetMoveInfo(item.id, AnimSubIx());
    float wait = info->GetVariantAfter();
    _variantTime = Glob.time + wait;

    //LogF("%s: CHANGE refresh variant, wait %.2f",cc_cast(GetDebugName()),wait);

#if DIAG_QUEUE
    LogF("%s: Eq move changed - %s (eq %s)", DNAME(),NAME_T(Type(),item.id),NAME_T(Type(),equivItemId));
#endif
  }
  else
  {
#if DIAG_QUEUE>=2
    LogF("%s: Same eq move changed - %s (eq %s)", DNAME(),NAME_T(Type(),item.id),NAME_T(Type(),_stillMoveQueueEnd));
#endif
  }
  // check time for which current equivalent animation is being played

  if (Glob.time>=_variantTime)
  {
    const MoveInfoMan *stillInfo = Type()->GetMoveInfo(_stillMoveQueueEnd, AnimSubIx());
    // we may select another move instead of this one
    // playing move long enough - select variant
    MoveId id = stillInfo->GetEquivalentTo();
    if (enableVariants)
      id = IsNetworkPlayer() ? stillInfo->RandomVariantPlayer() : stillInfo->RandomVariantAI();
    if (id!=MoveIdNone)
    {
      const MoveInfoMan *newInfo = Type()->GetMoveInfo(id, AnimSubIx());

      float wait = newInfo->GetVariantAfter();

      _variantTime = Glob.time+wait;

      //LogF("%s: STILL refresh variant, wait %.2f",(const char *)GetDebugName(),wait);

      item.id = id;
#if DIAG_QUEUE
      LogF("%s: Switch to variant %s of %s for %.2f", DNAME(),NAME_T(Type(),id),NAME_T(Type(),equivItemId), _variantTime-Glob.time);
#endif
    }
    else
      // no random variant - you wait inifitelly
      _variantTime = Glob.time+600;
  }
  else
  {
    if (equivItemId==equivStillId)
    {
#if DIAG_QUEUE>=2
      LogF("  equivalent state %s and %s", NAME_T(Type(),item.id), NAME_T(Type(),_stillMoveQueueEnd));
#endif
      // our target state is equivalent to wanted state - be happy with it
      // change target state function if necessary
      if (item.context)
      {
        if (_moves.GetMoveQueueSize() > 0)
          _moves.SetMoveQueueContext(item.context);
        else
        {
          if (!ChangeMoveQueue(item))
          {
            //_stillMoveQueueEnd = MoveIdNone;
          }
          return true;
        }
      }
      return true;
    }
  }
  bool ret = ChangeMoveQueue(item);
  // if (!ret) _stillMoveQueueEnd = MoveIdNone;
  return ret;
}

static Object *DirectHitCheck(Man *man, Vector3Par beg, Vector3 &hit, const AmmoType *ammo)
{
  /*
  LODShape *CollisionCapsule=Shapes.New("data3d\\colision.p3d",false,false);
  {
    Ref<Object> star=new ObjectColored(CollisionCapsule,-1);
    star->SetTransform(M4Identity);
    star->SetPosition(beg);
    star->SetScale(0.4);
    star->SetConstantColor(PackedColor(Color(0,1,0)));
    GLandscape->AddObject(star);
  }

  {
    Ref<Object> star=new ObjectColored(CollisionCapsule,-1);
    star->SetTransform(M4Identity);
    star->SetPosition(hit);
    star->SetScale(0.4);
    star->SetConstantColor(PackedColor(Color(1,1,0)));
    GLandscape->AddObject(star);
  }
  */

  // check if there is some direct hit
  CollisionBuffer retVal;
  GLandscape->ObjectCollisionLine(man->GetFutureVisualStateAge(),retVal,Landscape::FilterIgnoreOne(man),beg,hit,0);
  // seek first hit
  if (retVal.Size()<=0) return NULL;
  int minI = -1;
  float minT = 1e10;
  for (int i=0; i<retVal.Size(); i++)
  {
    if (minT>retVal[i].under)
    {
      minI = i;
      minT = retVal[i].under;
    }
  }
  const CollisionInfo &info = retVal[minI];
  // change hit position if necessary
  hit = info.object->WorldTransform(info.object->RenderVisualState()).FastTransform(info.pos);
  /*
  LogF("Direct hit %s",(const char *)info.object->GetDebugName());

  Ref<Object> star=new ObjectColored(CollisionCapsule,-1);
  star->SetTransform(M4Identity);
  star->SetPosition(hit);
  star->SetScale(0.4);
  star->SetConstantColor(PackedColor(Color(1,0,0)));
  GLandscape->AddObject(star);
  */

  return info.object;
}

void Man::TakeItemToHands(InventoryItem *item)
{
  _itemInHandsWanted = item;
}



void Man::PlayForcedMove(const MoveIdExt &item, ActionContextBase *context)
{
  if (item.id != MoveIdNone)
  {
    if (item.layer==MovePrimary)
    {
      _moves.AddForceMove(MotionPathItem(item.id,context), FutureVisualState()._moves);
    }
    else
    {
      // TODO: use _gestures.AddForceMove
      PlayMove(item, context);
    }
  }
}

void Man::ProcessItemToHands()
{
  if (IsOnAnyLadder()) return;
  if (_itemInHandsWanted == _itemInHands) return;
  // A hands item restored from a save or an authoritative update has no
  // pending request. Starting the Disarm transition for it would restart the
  // TakeInHands action forever ("Cancel Action" loop when joining with an
  // item already in hands).
  if (!_itemInHandsWanted) return;
  if (IsActionInProgress(MFTakeInHands))
  {
    // ANIMS-FIX: se a animacao de pegar item foi interrompida/cancelada pelo
    // movimento, a acao ficaria travada mostrando "Cancel Action" para sempre.
    // Depois do prazo, encerra a acao antiga e refaz a troca imediatamente.
    if (Glob.time < _takeInHandsDeadline) return;
    CancelAction();
    if (_itemInHandsWanted == _itemInHands) return;
    // conclui a troca imediatamente, sem depender da animacao
    TakeInHandsAction(_itemInHandsWanted);
    return;
  }
  const MoveIdExt &moveWanted = GetMove("Disarm");
  if (moveWanted.id != MoveIdNone)
  {
    Ref<ActionContextTakeInHands> context = new ActionContextTakeInHands;
    context->function = MFTakeInHands;
    context->_item = _itemInHandsWanted;
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DEAnimation))
    {
      RString moveName = GetMovesType()->GetMoveName(moveWanted.id);
      _debugLastActionsInfo.AddInfo("TakeInHands", moveName, "ProcessItemInHands");
    }
#endif
    PlayForcedMove(moveWanted,context);
    _takeInHandsDeadline = Glob.time + 3.0f;
  }
  else
  {
    // no animation found, expect we are without weapons already
    TakeInHandsAction(_itemInHandsWanted);
  }
}

#if _ENABLE_PHYSICS
void Man::RemoveItemInHandsThrow()
{
  if (IsActionInProgress(MFThrowItem))
    return;
  
	const MoveIdExt &moveWanted = GetMove("Throw");
  if (moveWanted.id != MoveIdNone)
  {
    Ref<ActionContextThrowItem> context = new ActionContextThrowItem();
    context->function = MFThrowItem;
    context->_item = GetItemInHands();
    context->_man = this;
    context->_moveid = moveWanted.id;
		PlayForcedMove(moveWanted,context);
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DEAnimation))
    {
      RString moveName = GetMovesType()->GetMoveName(moveWanted.id);
      _debugLastActionsInfo.AddInfo("Throw", moveName, "RemoveItemInHandsThrow");
    }
#endif
	}
  
}
#endif

void Man::RemoveItemInHands()
{
  HandleItemInHands(nullptr);

  // item need to be removed immediatelly
  _itemInHandsWanted = NULL;
  _itemInHands = NULL;

  // switch movement
  const MoveIdExt &moveWanted = GetMove("Disarm");
  if (moveWanted.id != MoveIdNone)
  {
    Ref<ActionContextTakeInHands> context = new ActionContextTakeInHands;
    context->function = MFTakeInHands;
    context->_item = NULL;
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DEAnimation))
    {
      RStringB moveName = GetMovesType()->GetMoveName(moveWanted.id);
      _debugLastActionsInfo.AddInfo("Disarm", moveName, "RemoveItemInHands");
    }
#endif
    PlayForcedMove(moveWanted,context);
  }
}

void Man::TakeInHandsAction(InventoryItem *item)
{
  // Use the common ownership transfer: remove the selected item from its
  // clothing/cargo slot and put it in ISHands before making it active.
  Person::TakeItemToHands(item);

  // Hands are an inventory location, not a second reference to a slot item.
  // Transfer the item only after the previous hands item has been put away.
  if (item && GetInSlot(ISHands) != item)
  {
    Entity *previousOwner = item->GetHierarchyParent();
    if (previousOwner) previousOwner->RemoveOwnedItem(item);
    if (!GetInSlot(ISHands)) PutInSlot(ISHands, item);
  }

  _itemInHands = item;

  // TODO: weapon will tell us what action to play
  if (item)
  {
    const InventoryItemType *type = item->Type();
    if (type)
    {
      RString action = type->GetArmAction();
      if (!action.IsEmpty())
      {
        const MoveIdExt &moveWanted = GetMove(action);
        if (moveWanted.id != MoveIdNone)
        {
#if _ENABLE_CHEATS
          if (CHECK_DIAG(DEAnimation))
          {
            RStringB moveName = GetMovesType()->GetMoveName(moveWanted.id);
            _debugLastActionsInfo.AddInfo(action, moveName, "TakeInHands");
          }
#endif
          PlayForcedMove(moveWanted,nullptr);
        }
      }
    }
  }
}

void Man::PauseMove(MoveId moveId)
{
  _moves.Pause(FutureVisualState()._moves, moveId);
}

void Man::ContinueMove()
{
  _moves.Continue(FutureVisualState()._moves);
}

void Man::ResetItemInHands()
{
  _itemInHandsWanted = NULL;
  _itemInHands = NULL;       
}

void Man::ThrowItemAction(InventoryItem *item, float power)
{
#if _ENABLE_PHYSICS
	if(!GetNetworkManager().IsPhysicsEnabled())
		return;

  ///smrt prebytecnejm parametrum!
  const TurretContext tc;
  const WeaponModePtr wmode = _weaponsState.ValidatedCurrentWeapon();
        
  Vector3 dir = GetAimCursorDirection(tc, wmode);
  Matrix4P mx;
  GetPalmMatrix(FutureVisualState(), mx);
  Vector3Val pos = FutureVisualState().PositionModelToWorld(mx.Position()); 

	//push into the world & throw it
	if (!GetNetworkManager().IsServerOwner())
  {
		ResetItemInHands();
		/*
		Matrix4 transform;
		transform.SetUpAndDirection(VUp, dir);
		transform.SetPosition(pos);
		item->SetTransform(transform);
		*/
		GetNetworkManager().AskForThrowItem(this, item, pos, dir * power);
  }
  else
  {
		//move in world
		Matrix4 transform;
		transform.SetUpAndDirection(VUp, dir);
		transform.SetPosition(pos);
		item->SetTransform(transform);
  
    ///////remove
    // item need to be removed immediatelly
    _itemInHandsWanted = NULL;
    _itemInHands = NULL;

    Entity *parent = item->GetHierarchyParent();
    if(parent)
      parent->RemoveOwnedItem(item);

    // move weapon to the world
    item->ItemMoveIn();

    //create physics - avoid another creation during moving into the scene - wont do any problem just take time
    item->CreateDynamicPhysics(EPL_BUILDING);
    item->ItemDropPhysicaly(dir, power);
  }
#endif
}


/// the direction in which the grenade is thrown
const static Vector3 GrenadeDir = Vector3(0,0.4f,1).Normalized();
/// the throw direction displayed to the user
/**
As grenades fall down substantially, it is confusing to show the real direction
*/
const static Vector3 GrenadeShowDir = Vector3(0,0.3f,1).Normalized();

const float GrenadeAtan2 = atan2(GrenadeDir.Y(),GrenadeDir.SizeXZ());

void Man::ThrowAction(Magazine *item)
{
  const MagazineType *type = item->Type();
  if (!type) return;
  const AmmoType *ammo = type->_ammo;
  if (!ammo) return;

  RemoteFireWeaponInfo localInfo;

  localInfo._position = FutureVisualState().Position();
  localInfo._visible=floatMax(ammo->visibleFire, ammo->audibleFire);

  VisualState &vs = FutureVisualState();
  Vector3 dir = vs._gunBodyTrans.Rotate(vs._gunTrans.Rotate(GrenadeDir));
  FutureVisualState().DirectionModelToWorld(localInfo._direction, dir);

  Vector3 offset = VZero;
  if (_shape->FindMemoryLevel() >= 0 && Type()->_handGrenadePoint >= 0)
    offset = AnimatePoint(vs, _shape->FindMemoryLevel(), Type()->_handGrenadePoint);

  Shot *shot = NewShot(this, ammo, NULL, NULL, true, false);
  if (shot)
  {
    const Shot::VisualState &svs = shot->FutureVisualState();
    shot->SetOrient(localInfo._direction, VUp);

    float ammoSpeed = type->_initSpeed;
    Vector3 initSpeedWorld = svs.Direction() * ammoSpeed;

    shot->SetSpeed(vs._speed+ initSpeedWorld);
    shot->SetPosition(vs.PositionModelToWorld(offset));

    GWorld->AddFastVehicle(shot);

    // shot created
    GetNetworkManager().CreateObject(shot);

    // destroy the "magazine"
    RemoveItem(item);
    item->ItemDestroy();
  }
}

void Man::PutAction(Magazine *item)
{
  const MagazineType *type = item->Type();
  if (!type) return;
  const AmmoType *ammo = type->_ammo;
  if (!ammo) return;

  RemoteFireWeaponInfo localInfo;

  VisualState &vs = FutureVisualState();
  Vector3 pos = vs.Position() + 0.5f * vs.Direction() + 0.5f * VUp;
  pos[1] = GLandscape->RoadSurfaceY(pos);
  localInfo._direction = vs.Direction();

  Shot *shot = NewShot(this, ammo, NULL, NULL, true, false);
  if (shot)
  {
    shot->SetOrient(localInfo._direction, VUp);
    shot->SetSpeed(VZero);
    shot->SetPosition(pos);

    GWorld->AddAnimal(shot);

    // shot created
    GetNetworkManager().CreateObject(shot);

    // destroy the "magazine"
    RemoveItem(item);
    item->ItemDestroy();

    if (ammo->_simulation == AmmoShotPipeBomb)
    {
      _pipeBombs.Add(shot);
      // allow autonomous touching off
      _pipeBombsAuto = true;
    }
  }
}

void Man::PreciseGetInAction(const RString action, const RString actionAfter, int doorIndex)
{
  if (IsActionInProgress(MFPreciseGetInEnd)) return;

  Ref<ActionContextCloseDoor> context = new ActionContextCloseDoor;
  context->function = MFPreciseGetInEnd;
  context->_doorIndex = doorIndex;
  context->_actionAfter = actionAfter;
  SwitchAction(action,context,false);
  FutureVisualState()._gettingInOut = true;
}

void Man::PreciseGetOutAction(const RString action)
{
  if (IsActionInProgress(MFPreciseGetOutEnd)) return;

  Ref<ActionContextAllowGetOut> context = new ActionContextAllowGetOut;
  context->function = MFPreciseGetOutEnd;
  SwitchAction(action,context,false);
  FutureVisualState()._gettingInOut = true;
}


float Man::GetWeaponCursorScale() const
{
  return 0;
}
void Man::ProcessFire(InputInterface *input)
{
  if (ProcessDrawBow(input)) return;

  bool fire = input->GetActionToDo(UADefaultAction);

  if (!QIsManual()) return;

  if (fire)
  {
    _fireEnabled = true;
    _fireStartTime = Glob.time;
  }

  if (_fireEnabled)
  {
    TurretContextV myContext;
    myContext._turret = NULL;
    myContext._turretType = NULL;
    myContext._gunner = this;
    myContext._weapons = &_weaponsState;
    myContext._turretVisualState = NULL;

    if (CanFire())
    {
      // Gunner is firing using mouse
      WeaponModePtr ptr = myContext._weapons->ValidatedCurrentWeapon();
      if (ptr.IsValid())
      {
        const WeaponModeType *mode = ptr.GetWeaponModeType();
        const Magazine *mag = ptr.GetMagazine();

        if (mode && mode->_autoFire && mag && mag->GetAmmo() > 0)
        {
          fire = input->GetActionDo(UADefaultAction) && !GWorld->HasMap();
        }

        if (fire)
        {
          FireWeapon(myContext, ptr, NULL, false);
        }
        else 
        {
          _fireStartTime = Time(0);
          _fireEnabled = false;
        }
      }
    }
  }
}

bool Man::ProcessDrawBow(InputInterface *input)
{
  bool active = IsDrawingBow();
  const float pull = input->GetAction(UADefaultAction);
  const bool drawsBow = pull > 0.001f;
  _allowBowDraw |= !drawsBow;

  WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
  const WeaponType *weapon = ptr.IsValid() ? ptr.GetWeaponType() : nullptr;
  const bool usesBow = (weapon && weapon->_bow);

  if (!_allowBowDraw || !QIsManual()) return usesBow;

  if (usesBow)
  {
    active = true;
    const bool inProgress = IsActionInProgress(MFBowDraw) || IsActionInProgress(MFBowRelease) || IsActionInProgress(MFBowCancel);
    const bool starting = drawsBow && !inProgress && !IsDrawingBow();
    if(starting && WeaponsDisabled() && input->GetActionToDo(UADefaultAction))
    {
      RaiseWeapon();
    }
    if(starting && CheckAndUpdateRaiseWeapon(input))
    {
      const MoveIdExt &moveWanted = GetMove("BowWindUp");
      if (moveWanted.id != MoveIdNone)
      {
        Ref<ActionContextDrawBow> context = new ActionContextDrawBow();
        context->function = MFBowDraw;
        context->_man = this;
        context->_weapon = ptr;
        PlayForcedMove(moveWanted,context);

#if _ENABLE_CHEATS
        if (CHECK_DIAG(DEAnimation))
        {
          RString moveName = GetMovesType()->GetMoveName(moveWanted.id);
          _debugLastActionsInfo.AddInfo("DrawBow", moveName, "ProcessDrawBow");
        }
#endif
      } 
    }
  }

  if (IsDrawingBow() && !IsActionInProgress(MFBowRelease) && !IsActionInProgress(MFBowCancel))
  {
    const bool cancelWanted = !usesBow || input->GetActionToDo(UAReloadMagazine);
    if (!drawsBow || cancelWanted)
    {
      RString actionName(!drawsBow ? "BowRelease" : "BowCancel");
      const MoveIdExt &moveWanted = GetMove(actionName);
      if (moveWanted.id != MoveIdNone)
      {
        Ref<ActionContextDrawBow> context = new ActionContextDrawBow();
        context->function = !drawsBow ? MFBowRelease : MFBowCancel;
        context->_man = this;
        context->_weapon = ptr;
        PlayForcedMove(moveWanted, context);

        _allowBowDraw = !drawsBow;

#if _ENABLE_CHEATS
        if (CHECK_DIAG(DEAnimation))
        {
          RString moveName = GetMovesType()->GetMoveName(moveWanted.id);
          _debugLastActionsInfo.AddInfo(actionName, moveName, "ProcessDrawBow");
        }
#endif
      }
      else
      {
        active = false;
      }
    }
    else if(!usesBow)
    {
      active = false;
    }

    if (!active)
    {
      SetDrawingBow(false);
      OnBowStateChange(BSIdle);
    }
  }

  return active;
}

void Man::ProcessMoveFunction(ActionContextBase *context)
{
  // animation backup / restoration might cause the same function processing to be called twice
  if (!context->DoOnce()) 
    return;
  context->Do(this);
  switch (context->function)
  {
    // old way of handling based on function ID
  case MFThrowItem:
    break;
  case MFThrow:
    {
      // actually fire weapon
      ActionContextThrow *action = static_cast<ActionContextThrow *>(context);
      ThrowAction(action->_item);
    }
    break;
  case MFPreciseGetInEnd:
    {
      ActionContextCloseDoor *cd = static_cast<ActionContextCloseDoor *>(context);
      Transport *veh = _brain?_brain->GetVehicleIn() : NULL;
      if (veh)
      {
        if (cd->_doorIndex >=0)
          veh->SetDoorOpen(cd->_doorIndex,false);
        if (!IsActionInProgress(MFPreciseGetInEnd))
        {
          SwitchAction(cd->_actionAfter);
          FutureVisualState()._gettingInOut = false;
        }
      }
    }
    break;
  case MFPreciseGetOutEnd:
    {
      //doors are open, we can really get out now
      SetOutReady(true);
    }
    break;
  case MFDead:
#if _ENABLE_DATADISC
    bool NoBlood();
#ifdef TEST_JANA
    //do podminky pridat, ze nesmi byt zrovna simulovano...proc?
#endif
    if (!NoBlood() && Glob.config.bloodLevel>1 && GRandGen.RandomValue() <= 0.3f)
    {
      // create slop of blood
      LODShape *shape = GLOB_SCENE->Preloaded(SlopBlood);
      float azimut = GRandGen.RandomValue() * H_PI * 2;

      Matrix4 transform(MIdentity);
      transform.SetOrientation(Matrix3(MRotationY, azimut));

      Vector3Val pos = FutureVisualState().Position();
      float surfY = GLandscape->RoadSurfaceY(pos+VUp*0.5f);
      Vector3 offset = transform.Rotate(shape->BoundingCenter());
      transform.SetPosition(Vector3(pos[0], surfY, pos[2]) + offset);

      // create vehicle
      float ttl = 180.0f + 420.0f * GRandGen.RandomValue();
      Slop *slop = new Slop(shape, VehicleTypes.New("#slop"), transform, ttl);
      slop->SetTransform(transform);
      slop->SetAlpha(0.7f);
      GWorld->AddAnimal(slop);
    }
#endif
    break;
  }
}

void Man::ProcessMoveFunctionProgress(ActionContextBase *context, float progress)
{
  context->Progress(progress);
}

bool Man::PlayMove(const MoveIdExt &id, ActionContextBase *context, bool callFuncNow, const char *action)
{
#if _ENABLE_CHEATS
  if (CHECK_DIAG(DEAnimation))
  {
    RStringB moveName = GetMovesType()->GetMoveName(id.id);
    _debugLastActionsInfo.AddInfo(action, moveName, "PlayMove");
  }
#endif

  if (id.layer==MovePrimary)
  {
    if (id.id == MoveIdNone)
    { 
      if (context && callFuncNow)
      {
        ProcessMoveFunction(context);
      }
      return false;
    }
    else
    {
      _moves.AddForceMove(MotionPathItem(id.id, context), FutureVisualState()._moves);
      return true;
    }
  }
  else
  {
    Assert(id.layer==MoveGesture);

    if (!GetGesturesType())
    {
      Fail("No gestures available");
      return false;
    }
    // how to map action to a gesture?
    // we need id in GetGesturesType(), not in GetMovesType()
    if (id.id!=MoveIdNone)
    {
      bool gestChanged;
      // we need to find a gesture movement as close to the primary one as possible
      // prototype: start all gestures in the state corresponding to a stop state of current action map
      // TODO: add start/end points or a general queue
      // DoAssert(_gesture.CheckValidState(GetGesturesType()));
      _gesture.SwitchMove(FutureVisualState()._gestures,id.id,context);
      // DoAssert(_gesture.CheckValidState(GetGesturesType()));
      // start interpolation
      FutureVisualState()._gestureFactor = 0;
      _gesture.ChangeMoveQueue(FutureVisualState()._gestures,GetGesturesType(),gestChanged,id.id);
      return true;
    }
    else
    {
      if (context && callFuncNow)
      {
        ProcessMoveFunction(context);
      }
      return false;
    }
  }
}

bool Man::PlayMoveNow(const MoveIdExt &id, ActionContextBase *context, bool callFuncNow, const char *action)
{
#if _ENABLE_CHEATS
  if (CHECK_DIAG(DEAnimation))
  {
    RStringB moveName = GetMovesType()->GetMoveName(id.id);
    _debugLastActionsInfo.AddInfo(action, moveName, "PlayMoveNow");
  }
#endif
  if (id.layer == MovePrimary && id.id != MoveIdNone)
  {
    // TODO: cancel any waiting forced moves? Can be dangerous, some of them may have a context
    return PlayMove(id, context, callFuncNow);
  }
  else return PlayMove(id, context, callFuncNow);
}

bool Man::PlayMove(RStringB move, ActionContextBase *context)
{
  // get move id
  // id may be MoveIdNone -> in that case movement control will be returned
  return PlayMove(MoveIdExt(GetMovesType()->GetMoveId(move),MovePrimary),context);
}

bool Man::PlayMoveNow(RStringB move, ActionContextBase *context)
{
  // get move id
  // id may be MoveIdNone -> in that case movement control will be returned
  return PlayMoveNow(MoveIdExt(GetMovesType()->GetMoveId(move),MovePrimary),context);
}

void Man::SwitchMove(const MoveIdExt &actId, ActionContextBase *context, bool callFuncNow, const char *action)
{
#if _ENABLE_CHEATS
  if (CHECK_DIAG(DEAnimation))
  {
    RStringB moveName = GetMovesType()->GetMoveName(actId.id);
    _debugLastActionsInfo.AddInfo(action, moveName, "SwitchMove");
  }
#endif
  if (actId.layer==MovePrimary)
  {
    MoveId id = actId.id;
    // finish the current primary move (if action attached)
    ActionContextBase *ctx = _moves.GetPrimaryMove(FutureVisualState()._moves).context;
    if (ctx)
    {
      ProcessMoveFunctionProgress(ctx, 1);
      ProcessMoveFunction(ctx);
    }

    //Assert(context == NULL);
    if (id==MoveIdNone)
    {
      // process context if needed (may be used for getin when there is no animation)
      if (context && callFuncNow)
      {
        ProcessMoveFunction(context);
        context = NULL;
      }
      id = GetDefaultMove();
      //Fail("SwitchMove to MoveIdNone");
    }
    // DoAssert(_moves.CheckValidState(GetMovesType()));
    _moves.SwitchMove(FutureVisualState()._moves,id, context);
    // DoAssert(_moves.CheckValidState(GetMovesType()));
    _stillMoveQueueEnd = id; // be ready to select any variant
    _variantTime = Glob.time;

    _gunXRotWanted = _gunXRot = 0;
    _gunYRotWanted = _gunYRot = 0;
    _gunYRotIronSightBackup = 0;
    _gunXRotDelay = 0;
    _gunYRotDelay = 0;
    _gunXSpeed = 0;
    _gunYSpeed = 0;
    _gunXRecoil = 0;
    _gunYRecoil = 0;
    _headXRotWantedAuto = _headXRotWantedCont = _headXRotWantedPan = _headXRotWanted = _headXRot = 0;
    _headYRotWantedAuto = _headYRotWantedCont = _headYRotWantedPan = _headYRotWanted = _headYRot = 0;
    _headSmoothMove = false;
    _lookXRotWanted = _lookXRot = 0;
    _lookYRotWanted = _lookYRot = 0;

    FutureVisualState()._headXShiftWantedCont = FutureVisualState()._headYShiftWantedCont = FutureVisualState()._headZShiftWantedCont = FutureVisualState()._headZRotWantedCont = 0;
    FutureVisualState()._headXShiftWanted = FutureVisualState()._headYShiftWanted = FutureVisualState()._headZShiftWanted = FutureVisualState()._headZRotWanted = 0;
    _leanZRot = 0;
    _leanZRotWanted = 0;
    //LogF("%s: SwitchMove %s",(const char *)GetDebugName(),NAME_T(Type(),_primaryMove.id));
    RecalcGunTransform();
    RecalcLeaningTransform();
    RecalcAffectSkeletonTransform();
    VisualCut();
#ifdef TEST_JANA
    /// we have to check ragdoll actions here (we cannot wait for next Simulate, because animations can change here)
    //CheckRagdollActions();
    //VisualCut(); //znovu?
#endif
  }
  else
  {
    Assert(actId.layer==MoveGesture);
    Fail("Not implemented");
  }
}

void Man::SwitchMove(RStringB move, ActionContextBase *context)
{
  // get move id
  // id may be MoveIdNone -> in that case movement control will be returned
  MoveId id = GetMovesType()->GetMoveId(move);
  // DoAssert(_moves.CheckValidState(GetMovesType()));
  SwitchMove(MoveIdExt(id,MovePrimary),context);
  // DoAssert(_moves.CheckValidState(GetMovesType()));
}

void Man::FreeFall(float seconds)
{
  if (!_swimming) //freeFall is disabled while swimming
  {
    Time until = Glob.time+seconds;
    if (_freeFallUntil<until) _freeFallUntil = until;
  }
}

bool Man::IsAction(RStringB action) const
{
  return GetMove(action).operator MoveId() != CAST_TO_ENUM(MoveId, MoveIdNone);
}

void Man::UseAudioTimeForMoves(bool toggle)
{
  _useAudioTimeForMoves = toggle;
  _audioTimeProcessed = GSoundScene->GetAudioTime();
}

float Man::GetMajorAnimationTime() const
{
  return _moves.GetMajorAnimationTime(FutureVisualState()._moves,GetMovesType());
}

bool Man::PlayAction(ManAction action, ActionContextBase *context, bool callFuncNow)
{
#if _ENABLE_CHEATS
  RString actionName;
  if (action < ManActN)
    actionName = ManActionNames[action].name;
  return PlayMove(GetMove(ENUM_CAST(ManAction,action)),context,callFuncNow, actionName.Data());
#else
  return PlayMove(GetMove(ENUM_CAST(ManAction,action)),context,callFuncNow);
#endif
}

bool Man::PlayAction(RString action, ActionContextBase *context, bool callFuncNow)
{
#if _ENABLE_CHEATS
  return PlayMove(GetMove(action),context,callFuncNow, action.Data());
#else
  return PlayMove(GetMove(action),context,callFuncNow);
#endif
}

bool Man::PlayActionNow(ManAction action, ActionContextBase *context, bool callFuncNow)
{
#if _ENABLE_CHEATS
  RString actionName;
  if (action < ManActN)
    actionName = ManActionNames[action].name;
  return PlayMoveNow(GetMove(ENUM_CAST(ManAction,action)),context,callFuncNow, actionName.Data());
#else
  return PlayMoveNow(GetMove(ENUM_CAST(ManAction,action)),context,callFuncNow);
#endif
}

bool Man::PlayActionNow(RString action, ActionContextBase *context, bool callFuncNow)
{
#if _ENABLE_CHEATS
  return PlayMoveNow(GetMove(action),context,callFuncNow, action.Data());
#else
  return PlayMoveNow(GetMove(action),context,callFuncNow);
#endif
}

void Man::SwitchAction(ManAction action, ActionContextBase *context)
{
#if _ENABLE_CHEATS
  RString actionName;
  if (action < ManActN)
    actionName = ManActionNames[action].name;
  SwitchMove(GetMove(ENUM_CAST(ManAction,action)),context, true, actionName.Data());
#else
  SwitchMove(GetMove(ENUM_CAST(ManAction,action)),context);
#endif
}

void Man::SwitchAction(RString action, ActionContextBase *context, bool callFuncNow)
{
#if _ENABLE_CHEATS
  SwitchMove(GetMove(action),context,callFuncNow, action.Data());
#else
  SwitchMove(GetMove(action),context,callFuncNow);
#endif
}

void Man::PutDownEnd()
{
  PlayAction(CAST_TO_ENUM(ManAction, ManActPutDownEnd));
}

void PlayActionHandlerContext::Event(int userType, const RStringB& param)
{
  _man->OnEvent(EEAnimHook, userType, param);
}

void PlayActionHandlerContext::Do(Man *ai)
{
  DoAssert(_man==ai);
  if (_handler.GetCode().Size()!=0)
  {

    GameState *state = GWorld->GetGameState();
    if (state)
    {
      GameVarSpace vars(false);
      int gshandle = state->BeginContext(&vars);
      state->VarSetLocal("_this", GameValueExt(ai,GameValExtObject), true);

      state->Evaluate(_handler.GetString(), _handler.GetCode(), GameState::EvalContext::_default, GWorld->GetMissionNamespace());
      state->EndContext(gshandle);
    }
  }
}

LSError PlayActionHandlerContext::Serialize(ParamArchive &ar)
{
  if (ar.IsSaving()) 
  {
    RString type("actionHandler");
    CHECK(ar.Serialize("type", type, 1))
  }
  CHECK(ar.SerializeValue("handler", _handler, 1))
  return LSOK;
}

ActionContextBase *Man::CreatePlayActionContext(RString action)
{
  if (!GetNetworkManager().IsServer()) return nullptr;
  return new PlayActionContext(this,GetMove(action));
}

ActionContextBase *Man::CreatePlayMoveContext(RString move)
{
  if (!GetNetworkManager().IsServer()) return nullptr;
  MoveIdExt moveId(GetMovesType()->GetMoveId(move),MovePrimary);
  return new PlayActionContext(this,moveId);
}
ActionContextBase *Man::CreatePlayMoveContext(RString move, const GameDataCode &handler)
{
  if (!GetNetworkManager().IsServer())
  {
    return new PlayActionHandlerContext(this,handler);
  }
  MoveIdExt moveId(GetMovesType()->GetMoveId(move),MovePrimary);
  return new PlayActionContext(this,moveId,handler);
}
ActionContextBase *Man::CreatePlayActionContext(RString action, const GameDataCode &handler)
{
  if (!GetNetworkManager().IsServer())
  {
    return new PlayActionHandlerContext(this,handler);
  }
  return new PlayActionContext(this,GetMove(action),handler);
}

bool Man::PlayActionGlobal(RString action)
{
  return PlayAction(action,CreatePlayActionContext(action));
}
bool Man::PlayActionGlobal(RString action, const GameDataCode &handler)
{
  return PlayAction(action,CreatePlayActionContext(action,handler));
}

bool Man::PlayActionNowGlobal(RString action)
{
  return PlayActionNow(action,CreatePlayActionContext(action));
}
void Man::SwitchActionGlobal(RString action)
{
  return SwitchAction(action,CreatePlayActionContext(action));
}

bool Man::PlayMoveGlobal(RStringB move)
{
  return PlayMove(move,CreatePlayMoveContext(move));
}
bool Man::PlayMoveNowGlobal(RStringB move)
{
  return PlayMoveNow(move,CreatePlayMoveContext(move));
}
bool Man::PlayMoveGlobal(RStringB move, const GameDataCode &handler)
{
  return PlayMove(move,CreatePlayMoveContext(move,handler));
}

void Man::SwitchMoveGlobal(RStringB move)
{
  return SwitchMove(move,CreatePlayMoveContext(move));
}


void Man::PlayerAction(RString action)
{
  PlayerAction(action,GameDataCode());
}

void Man::PlayerAction(RString action, const GameDataCode &handler)
{
  PlayMove(GetMove(action),CreatePlayActionContext(action,handler));
}

bool Man::CheckActionProcessing(UIActionType action, AIBrain *unit) const
{
  // check if this action is being processed
  // TODO: check action context
  return IsActionInProgress(MFUIAction);
}

static void __cdecl CDPHeal(Person *me, const Action *action)
{
  SECUROM_MARKER_SECURITY_ON(9)
  // force target to perform some action
  Person *person = dyn_cast<Person, Object>(action->GetTarget());
  if (person && person != me)
  {
    Vector3 dir = (me->FutureVisualState().Position() - person->FutureVisualState().Position()).Normalized();

    // move me (change orientation only)
    Matrix4 trans;
    trans.SetUpAndDirection(VUp, -dir);
    trans.SetPosition(me->FutureVisualState().Position());
    me->Move(trans);
#define DISABLE_MAN_DAMMAGE 2.0f
    me->DisableDamageFromObj(DISABLE_MAN_DAMMAGE);
    
    // move medic
    Vector3 pos = me->FutureVisualState().Position() - dir;
    pos[1] = GLandscape->RoadSurfaceYAboveWater(pos+VUp*0.5f);

    trans.SetUpAndDirection(VUp, dir);
    trans.SetPosition(pos);
    person->Move(trans);
    person->DisableDamageFromObj(DISABLE_MAN_DAMMAGE);

    person->PlayAction(CAST_TO_ENUM(ManAction, ManActMedic));
  }

  if (me)
  {
    // some actions are postponed
    Ref<ActionContextBase> context = me->CreateActionContextUIAction(action, ManActTreated);
    me->PlayAction(CAST_TO_ENUM(ManAction, ManActTreated),context);
  }

  SECUROM_MARKER_SECURITY_OFF(9)
}

static void __cdecl CDPHealSoldier(Person *me, Action *action)
{
  // force target to perform some action
  Person *person = dyn_cast<Person, Object>(action->GetTarget());
  bool handlerCalled = false;
  if (person->HandleHeal(me,true, handlerCalled)) 
  {
    action->SetProcessed(true);
    return;
  }
  if (person && person != me)
  {
    Vector3 dir = (me->FutureVisualState().Position() - person->FutureVisualState().Position()).Normalized();

    // move me (change orientation only)
    Matrix4 trans;
    trans.SetUpAndDirection(VUp, -dir);
    trans.SetPosition(me->FutureVisualState().Position());
    me->Move(trans);
#define DISABLE_MAN_DAMMAGE 2.0f
    me->DisableDamageFromObj(DISABLE_MAN_DAMMAGE);

    // move target
    Vector3 pos = me->FutureVisualState().Position() - dir;
    pos[1] = GLandscape->RoadSurfaceYAboveWater(pos+VUp*0.5f);

    trans.SetUpAndDirection(VUp, dir);
    trans.SetPosition(pos);
    person->Move(trans);
    person->DisableDamageFromObj(DISABLE_MAN_DAMMAGE);

    person->PlayAction(CAST_TO_ENUM(ManAction, ManActTreated));
  }

  if (me)
  {
    // some actions are postponed
    Ref<ActionContextBase> context = me->CreateActionContextUIAction(action, ManActMedic);
    me->PlayAction(CAST_TO_ENUM(ManAction, ManActMedic),context);
  }
}

class EventRetBoolCallHP: public IPostponedEvent
{
  OLinkO(Person) _person;
  Ref<Action> _action;

public:
  EventRetBoolCallHP(Person *person, const Action *action)
    : _person(person), _action(action->Copy()) {}

  void DoEvent() const
  {
    if (_person)
      CDPHealSoldier(_person, _action); // CDPHealSoldier call OnEventRetBool(EEHandleHeal, ...)
  }

  USE_FAST_ALLOCATOR
};

static void __cdecl CDPFirstAid(Person *me, const Action *action)
{
  Person *person = dyn_cast<Person, Object>(action->GetTarget());
  if (person && person != me)
  {
    bool handlerCalled = false;
    person->HandleHeal(me,false, handlerCalled);
  }
}

class EventRetBoolCallFA: public IPostponedEvent
{
  OLinkO(Person) _person;
  Ref<Action> _action;

public:
  EventRetBoolCallFA(Person *person, const Action *action)
    : _person(person), _action(action->Copy()) {}

  void DoEvent() const
  {
    if (_person)
      CDPFirstAid(_person, _action); // CDPFirstAid call OnEventRetBool(EEHandleHeal, ...)
  }

  USE_FAST_ALLOCATOR
};

static void __cdecl CDPRepairVehicle(Person *me, const Action *action)
{
  // force target to perform some action
  Person *person = dyn_cast<Person, Object>(action->GetTarget());

  if (person && person != me)
  {
    Vector3 dir = (me->FutureVisualState().Position() - person->FutureVisualState().Position()).Normalized();

    // move me (change orientation only)
    Matrix4 trans;
    trans.SetUpAndDirection(VUp, -dir);
    trans.SetPosition(me->FutureVisualState().Position());
    me->Move(trans);
#define DISABLE_MAN_DAMMAGE 2.0f
    me->DisableDamageFromObj(DISABLE_MAN_DAMMAGE);

  }

  if (me)
  {
    // some actions are postponed
    Ref<ActionContextBase> context = me->CreateActionContextUIAction(action, ManActMedic);
    me->PlayAction(CAST_TO_ENUM(ManAction, ManActMedic),context);
  }
}

/*!
\patch 1.21 Date 8/22/2001 by Ondra.
- Fixed: Soldier now cannot run away while taking down the a flag.
*/

void Man::StartActionProcessing(Action *action, AIBrain *unit)
{
  switch (action->GetType())
  {
    case ATHeal:
    {
      CDPHeal(this, action);
      break;
    }
    case ATHealSoldier:
    {
      // EH used
      GWorld->AppendPostponedEvent(new EventRetBoolCallHP(this, action));
      break;
    }
    case ATFirstAid:
    {
      // EH used
      GWorld->AppendPostponedEvent(new EventRetBoolCallFA(this, action));
      break;
    }
    case ATRepairVehicle:
    {
      CDPRepairVehicle(this, action);
      break;
    }
    case ATRepair:
    case ATRefuel:
    case ATStartTimer:
    case ATSetTimer:
    case ATDeactivate:
    case ATUseWeapon:
    case ATHideBody:
    case ATFireInflame:
    case ATFirePutDown:
    case ATDeactivateMine:
    case ATTakeMine:
    case ATPutItem:
    {
      // some actions are postponed
      Ref<ActionContextBase> context = CreateActionContextUIAction(action, ManActPutDown);
      PlayAction(CAST_TO_ENUM(ManAction, ManActPutDown),context);
      break;
    }
    case ATGear:
      {
#if _VBS3_INVENTORY
        if(!action->PerformScripted(unit))
#endif
        {
          // some actions are postponed
		  // only allow agents that can process actions to use inventory
		  if (unit->LSCanProcessActions()) 
		  {
			Ref<ActionContextBase> context = CreateActionContextGear(action);
			PlayAction(CAST_TO_ENUM(ManAction, ManActGear),context);
		  }
        }
        break;
      }
    case ATTakeFlag:
    case ATReturnFlag:
    {                  
      // manipulation with pole flag requires different animation
      if (!dyn_cast<FlagCarrier, Object>(action->GetTarget()))
      {
        Ref<ActionContextBase> context = CreateActionContextUIAction(action, ManActPutDown);
        PlayAction(CAST_TO_ENUM(ManAction, ManActPutDown),context);
      }
      else
      {
        // If man is lying check space for stand up.
        if (!action->IsFromGUI() || !QIsManual() || !IsDown() || IsCollisionFreeMove(GetMove( CAST_TO_ENUM(ManAction, ManActTakeFlag) )))
        {          
          Ref<ActionContextBase> context = CreateActionContextUIAction(action, ManActTakeFlag);
          PlayAction(CAST_TO_ENUM(ManAction, ManActTakeFlag),context);
        }
        else
        {
          Ref<ActionContextBase> context = CreateActionContextUIAction(action, ManActCanNotMove);
          PlayAction(CAST_TO_ENUM(ManAction, ManActCanNotMove));
        }
      }
      break;
    }
    case ATLadderOnDown:
    case ATLadderDown:
    {
      Ref<ActionContextBase> context = CreateActionContextUIAction(action, ManActLadderOnDown);
      PlayAction(CAST_TO_ENUM(ManAction, ManActLadderOnDown),context);
      break;
    }
    case ATLadderOff:
    {
      action->Process(unit);
      //Ref<ActionContextBase> context = CreateActionContextUIAction(action);
      PlayAction(CAST_TO_ENUM(ManAction, ManActLadderOff));
      break;
    }
    case ATLadderOnUp:
    case ATLadderUp:
    {
      Ref<ActionContextBase> context = CreateActionContextUIAction(action, ManActLadderOnUp);
      PlayAction(CAST_TO_ENUM(ManAction, ManActLadderOnUp),context);
      break;
    }
    default:  
    // some are processed directly
      action->Process(unit);
      break;
  }
}

bool Man::IsActionInProgress(MoveFinishF action) const
{
  // check _primary move
  if (_moves.IsActionInProgress(FutureVisualState()._moves,action)) return true;

  // we need to check _gestures as well
  if (!GetGesturesType()) return false;

  return _gesture.IsActionInProgress(FutureVisualState()._gestures,action);
}

const ActionContextBase *Man::GetActionInProgress() const
{
  // check _primary move
  // check moves waiting in queues
  return _moves.GetActionInProgress(FutureVisualState()._moves);
}

bool Man::DoReloadMagazine(WeaponsState &weapons, const WeaponModePtr &ptr)
{
  bool reloadOk = false;
  // reload action
  if (!IsActionInProgress(MFReload))
  {
    AIBrain *unit = CommanderUnit();
    const MuzzleType *muzzle = ptr.GetMuzzleType();
    // only weapon in hand is reloading using animation
    if (unit && muzzle && !muzzle->_autoReload && ptr._weapon == _itemInHands)
    {
      // load reload action in this order
      // 1. from magazine
      // 2. from muzzle
      // 3. from defaultActions <-- NOT_IMPLEMENTED
      const Magazine *magazine = ptr.GetMuzzle()._magazineWanted;
      RString action = magazine->Type()->_reloadAction;

      if (action.IsEmpty())
      {
        // from muzzle
        action = muzzle->_reloadAction;

        // from defaultActions NOT_IMPLEMENTED
//      if (action.IsEmpty()) {}
      }

      if (action.GetLength() > 0)
      {
        Ref<ActionContextReload> context = new ActionContextReload;
        context->function = MFReload;
        context->_ptr = ptr;

        reloadOk = PlayAction(action, context, false);
        if (reloadOk)
        {
          // while we are reloading the magazine, we can set its parameters
          // we have just started, progress will reach 1 eventually
          ptr.SetReloadMagazineProgress(0);
        }
      }
    }
    if (!reloadOk) reloadOk = ReloadMagazineTimed(weapons, ptr, false);
    if (reloadOk)
    {
      TurretContext context;
      context._turret = NULL;
      context._turretType = NULL;
      context._gunner = this;
      context._weapons = &_weaponsState;
      PlayReloadMagazineSound(context, ptr, muzzle);
    }
  }
  return reloadOk;
}

bool Man::EnableWeaponManipulation() const
{
  if (IsActionInProgress(MFReload)) return false;
  // during some operations weapon manipulation is not possible
  // like during burst or during reload animation
  //if (DisableWeaponsLong()) return false;
  return base::EnableWeaponManipulation();
}

/*!
\patch 1.01 Date 6/25/2001 by Ondra.
- Fixed: optics during crouch.
- in 1.00 it was impossible to move with optics view when crouching
- optics now work like mode, once set, they appear when you stop moving
\patch_internal 1.01 Date 6/25/2001 by Ondra. new function EnableOptions
*/

bool Man::EnableViewThroughOptics() const
{
  // check if unit is able to use optics
  if (BinocularSelected() && !EnableBinocular(FutureVisualState())) return false;
  // FIX
  return EnableOptics(FutureVisualState());
  //return true;
}

void Man::UpdateAnimSub()
{
  int animSubIx = MotionType::NoSubclassIx;

  do {
    // ASInjuredLight, ASInjuredHeavy
    float damage = GetTotalDamage();
    if (damage > 0.5f) 
      if (damage < 0.8f) animSubIx = GetMovesType()->_animEnumToSubIx[MotionType::ASInjuredLight];
      else animSubIx = GetMovesType()->_animEnumToSubIx[MotionType::ASInjuredHeavy];

    if (animSubIx != MotionType::NoSubclassIx) break;

    // Item in hands (driven by item/weapon animClass entry)
    if (_itemInHands)
    {
      RStringB ihAnimClass = _itemInHands->Type()->AnimClass();
      if (_itemInHandsACInfo.animClass != ihAnimClass )
      { // find _animClassSubIx
        _itemInHandsACInfo.animClass = ihAnimClass; 
        _itemInHandsACInfo.animSubIx = GetMovesType()->GetSubClassIx(ihAnimClass); //-1 when not found
        COMPILE_ASSERT(MotionType::NoSubclassIx == -1);
      }
      animSubIx = _itemInHandsACInfo.animSubIx;
    }
    else _itemInHandsACInfo.Reset();
  } while(false);
  // and set it at last  
  _moves.SetAnimSubIx(animSubIx);
}

/*!
\patch 1.01 Date 06/11/2001 by Ondra.
- Fixed: freeze after getout (in code see FIX)
- soldier sometimes stayed frozen in the air
after getting out of vehicle, until moved.
\patch 5117 Date 1/16/2007 by Bebul
- Fixed: units picked up while swimming and dropped on land still swim
*/

void Man::ResetMovement(float speed, ActionMap::Index actionIndex, bool init)
{
  VisualCut();
  
  int deg = -1;
  // try to receive upDegree from current move
  if (!init)
  {
    const ActionMap *map = GetMovesType()->GetActionMap(_moves.GetPrimaryMove(FutureVisualState()._moves).id, AnimSubIx());
    if (map) deg = map->GetUpDegree();
  }
  // if failed, select by combat mode
  if (deg < 0) deg = GetAutoUpDegree();

  // check if current move is default move
  MoveId move = GetMovesType()->GetMove(deg, actionIndex);

  AIBrain *unit = Brain();
  int upDegreeWanted = ManPosStand;

  if (_moves.GetPrimaryMove(FutureVisualState()._moves).id != MoveIdNone)
  {
    const ActionMap *map = Type()->GetActionMap(_moves.GetPrimaryMove(FutureVisualState()._moves).id, AnimSubIx());
    if (map->GetUpDegree() == upDegreeWanted)
    {
      MoveId moveWanted = map->GetAction(actionIndex);
      if (moveWanted!=MoveIdNone) move = moveWanted;
    }
  }

  SetTargetMove(move);
  _moves.ResetMovement(FutureVisualState()._moves, GetMovesType(), MotionPathItem(move));

  if (_itemInHands) TakeInHandsAction(_itemInHands); // select animation based on the item in hands

  _turnWanted=0;
  _landContact=false;
  // FIX freeze after getting out
  _objectContact=false;
  _objectContactNonStatic=false;
  _tired = 0;
  FutureVisualState()._speed=VZero;
  _angMomentum=VZero;
  _impulseForce=VZero;
  _impulseTorque=VZero;

  if (unit) unit->SetSearchTime(Glob.time-60);
  FutureVisualState()._modelSpeed=VZero;
  //SetScale(_manScale);

  _gunXRotWanted = _gunXRot = 0;
  _gunYRotWanted = _gunYRot = 0;
  _gunYRotIronSightBackup = 0;
  _gunXRotDelay = 0;
  _gunYRotDelay = 0;
  _gunXSpeed = 0;
  _gunYSpeed = 0;
  _gunXRecoil = 0;
  _gunYRecoil = 0;
  _headXRotWantedAuto = _headXRotWantedCont = _headXRotWantedPan = _headXRotWanted = _headXRot = 0;
  _headYRotWantedAuto = _headYRotWantedCont = _headYRotWantedPan = _headYRotWanted = _headYRot = 0;
  _headSmoothMove = false;
  _lookXRotWanted = _lookXRot = 0;
  _lookYRotWanted = _lookYRot = 0;
  FutureVisualState()._headXShiftWantedCont = FutureVisualState()._headYShiftWantedCont = FutureVisualState()._headZShiftWantedCont = FutureVisualState()._headZRotWantedCont = 0;
  FutureVisualState()._headXShiftWanted = FutureVisualState()._headYShiftWanted = FutureVisualState()._headZShiftWanted = FutureVisualState()._headZRotWanted = 0;
  _leanZRot = 0;
  _leanZRotWanted = 0;
  _swimming = false;

  RecalcGunTransform();
  RecalcLeaningTransform();
  RecalcAffectSkeletonTransform();
  if (IsInLandscape())
  {
    RecalcPositions(GetFrameBase());
  }

#ifdef TEST_JANA
  /// we have to check ragdoll actions here (we cannot wait for next Simulate, because animations can change here)
  //CheckRagdollActions();
#endif
  // DoAssert(_moves.CheckValidState(GetMovesType()));
  // DoAssert(_gesture.CheckValidState(GetGesturesType()));
}

void Man::ResetMovement(float speed, int action)
{
  if (_isDead) action = ManActDie;
  else if (action < 0) action = ManActDefault;

  ActionMap::Index actionIndex = GetMovesType()->FindActionIndex((ManAction)action);
  ResetMovement(speed, actionIndex, action == ManActDefault);
}

void Man::ResetMovement(float speed, RString action)
{
  ActionMap::Index actionIndex;
  bool init = false;

  if (_isDead) actionIndex = GetMovesType()->FindActionIndex(CAST_TO_ENUM(ManAction, ManActDie));
  else if (action.GetLength() == 0)
  {
    actionIndex = GetMovesType()->FindActionIndex(CAST_TO_ENUM(ManAction, ManActDefault));
    init = true;
  }
  else actionIndex = GetMovesType()->FindActionIndex(action);

  ResetMovement(speed, actionIndex, init);
}

/*!
\patch 1.78 Date 7/22/2002 by Jirka
- Added: MP respawn in base - enable several respawn markers for each side / vehicle
*/

/*
ArcadeMarkerInfo *FindMarker(RString name)
{
  AUTO_STATIC_ARRAY(int, indices, 16);
  int len = name.GetLength();
  for (int i=0; i<markersMap.Size(); i++)
  {
    ArcadeMarkerInfo &mInfo = markersMap[i];
    if (strnicmp(mInfo.name, name, len) == 0) indices.Add(i);
  }
  if (indices.Size() == 0) return NULL;
  int index = toIntFloor(indices.Size() * GRandGen.RandomValue());
  return &markersMap[indices[index]];
}

*/


float MoveInfoMan::GetStepSpeed() const
{
	return _move->GetStepLength()*GetOriginalSpeed();
}

float MoveInfoMan::GetStepSpeedX() const
{
	return _move->GetStepLengthX()*GetOriginalSpeed();
}

/*!
\patch 5142 Date 3/19/2007 by Ondra
- Fixed: Config entry predictSpeedCoef no longer needed - speed prediction now based on looped/nonlooped.
  This should help AI predicting solider movement in transitions like when seeing someone going prone.
*/

Vector3 Man::PredictVelocity() const
{
  if (_freeFall) return FutureVisualState().Speed();
  //if (_isDead) return VZero;
  // check for some animation, which change position only very slightly (like going down)
  // if an animation does not repeat itself, we can disregard it
  float speedCoef = ValueTest(FutureVisualState(),&MoveInfoMan::GetLooped);
  return FutureVisualState().Speed()*speedCoef;
}

/*!
\patch 2.01 Date 1/8/2003 by Ondra
- New: Short delay before soldier is identified as being dead.
*/

/*
Time Man::GetDestroyedTime() const
{
  if (!_isDead) return TIME_MAX;
  // if kill time is not known, assume it was killed recently
  if(_whenKilled==Time(0)) return Glob.time;
  return _whenKilled; // helpers for suspending dead body after a while
}
*/

/*!
\patch 1.31 Date 11/08/2001 by Jirka
- Fixed: when player was killed in vehicle in MP (respawn = "BIRD")
seagull appears at wrong place
\patch 1.32 Date 11/26/2001 by Jirka
- Fixed: respawn in group did not properly switch camera if unit respawn into was in vehicle
\patch 1.33 Date 12/03/2001 by Jirka
- Improved: friendly kills are distinguished in MP messages
\patch 1.34 Date 12/7/2001 by Ondra
- Fixed: Player was considered alive by AI leader after respawned to sea-gull.
\patch 1.36 Date 12/13/2001 by Jirka
- Fixed: Group respawn enabled also for nonleader units.
\patch 1.58 Date 5/20/2002 by Jirka
- Fixed: Group respawn - sometimes two nonleader units respawns into single body
\patch 5124 Date 1/29/2007 by Jirka
- New: playerKilledScript entry in the description.ext of the mission can override this entry in the config
\patch 5196 Date 12/11/2007 by Ondra
- New: Added difficulty option DeathMessages (KILL MSG), allowing to disable XXX killed by YYY messages.
*/

void Man::KilledBy(EntityAI *owner)
{
  if (!GetNetworkManager().IsServerOwner()) return;
  if (!_brain) return;
  if (!_brain->LSIsAlive()) return;

  // clean up all activities
  _activities.Clear();

  bool player = this == GWorld->GetRealPlayer();
  if (GWorld->GetMode() == GModeNetware)
  {
    if (player)
    {
      LocalizedFormatedString message;
      AIBrain *killerUnit = owner ? owner->CommanderUnit() : NULL;

      bool friendly = killerUnit && killerUnit->IsFriendly(_brain->GetSide());

      if (killerUnit && killerUnit->GetPerson()->IsRemotePlayer())
      {
        message.SetFormat(
          LocalizedString::Stringtable,
          friendly ? "STR_KILLED_BY_FRIENDLY" : "STR_KILLED_BY"
        );
        message.AddArg(LocalizedString::PlainText,_brain->GetPerson()->GetPersonName());
        message.AddArg(LocalizedString::PlainText,killerUnit->GetPerson()->GetPersonName());
#if _ENABLE_REPORT
int dpnid = killerUnit->GetPerson()->GetRemotePlayer();
const PlayerIdentity *identity = GetNetworkManager().FindIdentity(dpnid);
RptF
(
  "*** Player kill: %s (%d:%" OID_FORMAT") killed by %s (%d:%" OID_FORMAT") - player %d, identity %s",
  (const char *)_brain->GetPerson()->GetPersonName(), _brain->GetPerson()->GetNetworkId().creator.CreatorInt(), _brain->GetPerson()->GetNetworkId().id,
  (const char *)killerUnit->GetPerson()->GetPersonName(), killerUnit->GetPerson()->GetNetworkId().creator.CreatorInt(), killerUnit->GetPerson()->GetNetworkId().id,
  dpnid, identity ? (const char *)identity->name : "NOT FOUND"
);
#endif
      }
      else
      {
        message.SetFormat(
          LocalizedString::Stringtable,
          friendly ? "STR_KILLED_FRIENDLY" : "STR_KILLED"
        );
        message.AddArg(LocalizedString::PlainText,_brain->GetPerson()->GetPersonName());
      }
      if (
        Glob.config.IsEnabled(DTDeathMessages)
#if _VBS2 // death message difficulty option
        || (killerCenter == myCenter && GWorld->GetFriendlyFireMsgSideOnly()) ||
        (friendly && killerCenter->IsFriendly(myCenter->GetSide()) && !GWorld->GetFriendlyFireMsgSideOnly())
#endif
      )
      {
        GChatList.Add(CCGlobal, NULL, message.GetLocalizedValue(), false, true);
        RefArray<NetworkObject> units;
        void WhatUnits(RefArray<NetworkObject> &units, ChatChannel channel, NetworkObject *object);
        WhatUnits(units, CCGlobal, NULL);
        //RadioSentence rs = RadioSentence();
        // radio chat was used before, perhaps to keep message ordering?
        GetNetworkManager().LocalizedChat(CCGlobal, RString(), units, message);
      }
    }
  }
  if (player)
  {
    if (GWorld->GetMode() != GModeNetware) { GChatList.Clear(); GCustomChannels.ClearCustomChannels(); }
    RString name;
    ConstParamEntryPtr entry = ExtParsMission.FindEntry("playerKilledScript");
    if (entry) name = *entry;
    else name = Pars >> "playerKilledScript";
    RString FindScript(RString name);
    if (FindScript(name).GetLength() > 0)
    {
      GWorld->EnableEndDialog(false);
      GameArrayType arguments;
      arguments.Add(GameValueExt(this));
      arguments.Add(GameValueExt(owner));
      Script *script = new Script(name, GameValue(arguments), GWorld->GetMissionNamespace()); // mission namespace
      // HOTFIX: script can destroy the person / brain
      Ref<Man> lock = this;          
      GWorld->StartCameraScript(script);
      if (RefCounter() <= 1 || !_brain) return; // something destroyed
    }
  }

  GLOB_WORLD->RemoveSensor(this);

  _brain->SetLifeState(LifeStateDead);
  GetNetworkManager().UpdateObject(this);

  if (IsLocal()) GetNetworkManager().DisposeBody(this);
}

/*!
\patch 1.85 Date 9/9/2002 by Jirka
- Fixed: Healing of dead unit sometimes ends in loop
*/
float Man::NeedsAmbulance() const
{
  // FIX: no ambulance can help already
  if (_isDead) return 0;

  float maxPartDmamage = 0;
  maxPartDmamage = floatMax(GetHitCont(Type()->_handsHit),GetHitCont(Type()->_bodyHit));
  maxPartDmamage = floatMax(GetHitCont(Type()->_headHit),maxPartDmamage);
  maxPartDmamage = floatMax(GetHitCont(Type()->_legsHit),maxPartDmamage);
  if(maxPartDmamage >=0.3) return maxPartDmamage;

  if (GetTotalDamage()<=0.05f) return 0;

  return GetTotalDamage();
}

float Man::NeedsRepair() const {return 0;}
float Man::NeedsRefuel() const {return 0;}

void Man::SetTotalDamageHandleDead(float damage, bool setHitpoints)
{
  bool wasDead = _isDead;
  base::SetTotalDamageHandleDead(damage,setHitpoints);
  if (wasDead && !_isDead)
  {
    AIBrain *agent = Brain();
    if (agent)
    {
      agent->SetLifeState(LifeStateAlive);
      ResetMovement(0);
    }
    else
    {
      RptF("Cannot resurrect %s",cc_cast(GetDebugName()));
      _isDead = true;
    }
  }
}

void Man::ReactToDamage()
{
  if (!_isDead)
  {
    if (IsDamageDestroyed() || GetHit(Type()->_headHit)>0.9f || GetHit(Type()->_bodyHit)>0.9f)
      _isDead=true;
  }
  if (_isDead)
  {
    _turnWanted=0;
  }
}

bool Man::BinocularSelected() const
{
  if (!_itemInHands) return false;
  const Weapon *weapon = _itemInHands->GetWeapon();
  if (!weapon) return false;
  const WeaponType *type = weapon->Type();
  return type && type->_simulation == WSBinocular;
}

bool Man::BinocularAnimSelected() const
{
  WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
  if (!ptr.IsValid()) return false;
  const MuzzleType *muzzle = ptr.GetMuzzleType();
  return muzzle && muzzle->_useAsBinocular;
}

/*
bool Man::HandGunSelected() const
{
  int weapon = _weaponsState.ValidatedCurrentWeapon();
  if (weapon < 0) return false;
  const MagazineSlot &slot = GetMagazineSlot(weapon);
  const WeaponType *type = slot._weapon;
  if (!(type->_weaponType & MaskSlotHandGun)) return false;
  return true;
}
*/

bool Man::IsHandGunInMove() const
{
  int actPos = GetActUpDegree(FutureVisualState());
  return
  (
    actPos==ManPosHandGunLying || actPos==ManPosHandGunCrouch || actPos==ManPosHandGunStand
  );
}

bool Man::IsPrimaryWeaponInMove() const
{
  int actPos = GetActUpDegree(FutureVisualState());
  return
  (
    actPos==ManPosLying || actPos==ManPosCrouch || actPos==ManPosCombat
  );
}

bool Man::IsSwimmingInMove() const
{
  int actPos = GetActUpDegree(FutureVisualState());
  return
  (
    actPos==ManPosSwimming
  );
}

/*!
\patch 1.02 Date 07/11/2001 by Ondra
- Fixed: empty LAW optics looking to sky.
*/

const AmmoType *Man::GetCurrentAmmoType() const
{
  WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
  if (!ptr.IsValid()) return NULL;

  const AmmoType *ammo = ptr.GetAmmoType();
  if (ammo) return ammo;
  
  // FIX
  // if there is no mode, use some other way to check if it is LAW
  // check weapon typical magazine  
  const MuzzleType *muzzle = ptr.GetMuzzleType();
  if (!muzzle) return NULL;
  if (muzzle->_magazines.Size() <= 0) return NULL;
  MagazineType *magazine = muzzle->_magazines[0];
  if (!magazine)
  {
    magazine = muzzle->_typicalMagazine;
  }
  if (!magazine) return NULL;
  // check first magazine mode
  return magazine->_ammo;
}

bool Man::LauncherSelected() const
{
  const AmmoType *ammo = GetCurrentAmmoType();
  if (!ammo) return false;
  if (ammo->_simulation == AmmoShotMissile) return true;
  return false;
}

bool Man::LauncherAnimSelected() const
{
  WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
  if (!ptr.IsValid()) return false;
  const AmmoType *ammo = GetCurrentAmmoType();
  if (!ammo) return false;
  if (ammo->_simulation == AmmoShotMissile) return true;
  return false;
}

/*!
\patch 1.37 Date 12/17/2001 by Ondra
- Fixed: AI: Missile launcher is not prepared
if there is no reason to prepare it.
\patch_internal 1.37 Date 12/17/2001 by Ondra
- Fixed: AI laucher is not selected when:
1) there is no current target or current target armor is less than 10 
and 
2) there is no nearby enemy or unknown target with armor higher than 50
*/

bool Man::LauncherWanted() const
{
  // check if there is some reason to have missile ready
  // reason can be: some armored target is selected to be fired at
  AIBrain *unit = _brain;
  if (!unit) return false;
  if (unit->GetSemaphore()<=AI::SemaphoreGreen)
  {
    Target *tgt = unit->GetTargetAssigned();
    if (tgt)
    {
      if (tgt->GetType()->_armor>10) return true;
    }
    return false;
  }
  // check if some known enemy or unidentified target
  // is worth attacking by launcher
  Target *tgt = unit->GetTargetAssigned();
  if (tgt)
  {
    const EntityAIType *type = tgt->GetType();
    if (type->_armor>10) return true;
    if (type->IsKindOf(GWorld->Preloaded(VTypeMan))) return false;
  }
  // check all targets
  const TargetList *list = unit->AccessTargetList();
  if (!list) return false;
  // TODO: use Unknown as well
  for (int i=0; i<list->EnemyCount(); i++)
  {
    TargetNormal *tgt = list->GetEnemy(i);
    if (tgt->type->_armor<50) continue;
    if (!tgt->IsKnownBy(unit)) continue;
    if (!unit->IsEnemy(tgt->side) && tgt->side != TSideUnknown)
    {
      continue;
    }
    return true;
  }
  return false;
}

bool Man::LauncherReady() const
{
  if (!LauncherSelected()) return false;
  WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
  if (!ptr.IsValid()) return false;
  const Magazine *magazine = ptr.GetMagazine();
  if (!magazine || magazine->GetAmmo() == 0) return false;
  return true;
}


void Man::ResetLauncher()
{
  /*
  for(int w=0; w<NWeapons(); w++)
  {
    const WeaponInfo &info=GetWeapon(w);
    if (info._ammo && info._ammo->_simulation==AmmoShotMissile)
    {
      AmmoState &state=GetAmmoState(w);
      MuzzleState &mState=GetMuzzleState(w);
      mState._reload=info._reloadTime;
    }
  }
  */
}

void Man::GetRelSpeedRange(float &speedX, float &speedZ, float &minSpd, float &maxSpd)
{
  _moves.GetRelSpeedRange(FutureVisualState()._moves, GetMovesType(), speedX, speedZ, minSpd, maxSpd);
}

void Man::ProcessMovesProgress(Moves &moves, const MovesVisualState &mvs)
{
  if (moves.GetPrimaryMove(mvs).context && moves.GetPrimaryFactor(mvs) > Moves::_primaryFactor0)
    ProcessMoveFunctionProgress(moves.GetPrimaryMove(mvs).context,moves.GetPrimaryTime(mvs));
  if (moves.GetSecondaryMove(mvs).context && moves.GetPrimaryFactor(mvs) < Moves::_primaryFactor1)
    ProcessMoveFunctionProgress(moves.GetSecondaryMove(mvs).context,moves.GetSecondaryTime(mvs));
}

class CombineMinMax
{
public:
  MoveInfoMan::SpeedRange operator () (const MoveInfoMan::SpeedRange &a, const MoveInfoMan::SpeedRange &b, float factorA) const
  {
    return MoveInfoMan::SpeedRange(floatMin(a.minSpeed,b.minSpeed),floatMax(a.maxSpeed,b.maxSpeed));
  }
};

void Man::CalcAttachedPosition(float deltaT)
{
  if (!_attachedTo) return;
  base::CalcAttachedPosition(deltaT);
  RecalcPositions(GetFrameBase());
}

bool Man::SimulateAnimations(float &turn, float &moveX, float &moveZ, float deltaT, SimulationImportance prec)
{
  // DoAssert(_moves.CheckValidState(GetMovesType()));
  // DoAssert(_gesture.CheckValidState(GetGesturesType()));
  UpdateAnimSub();

  bool change = false;

  // doing nothing: switch to action stand depending on current UpDegree
  _moves.PatchNoMove(FutureVisualState()._moves,MotionPathItem(GetDefaultMove()));

  // DoAssert(_moves.CheckValidState(GetMovesType()));
  
  // limit movement by tired in range <TiredRunSpeed, FastRunSpeed>
  float maxRunTired = _tired * TiredRunSpeed + (1 - _tired) * FastRunSpeed;
  float speedWanted2 = _walkSpeedWantedX*_walkSpeedWantedX+_walkSpeedWantedZ*_walkSpeedWantedZ;
  saturateMin(speedWanted2, maxRunTired*maxRunTired);

  if (!IsLocal())
  {
    static bool doIt = false;
    if (doIt)
    {
      float speedWantedRemote2 = FutureVisualState()._speed.SquareSize();
      if (speedWanted2<speedWantedRemote2) speedWanted2 = speedWantedRemote2;
    }
  }
  // set move speed coefficient
  // to move exactly at given speed
  float speedX,speedZ, minSpd, maxSpd;
  GetRelSpeedRange(speedX,speedZ,minSpd,maxSpd);

  // now we can adjust the animation speed to match the desired movement speed
  float aSpeed2 = speedX*speedX+speedZ*speedZ;
  // if the animation has no noting of speed, keep it playing on normal speed
  float adjustTimeCoef = 1.0f;
  if (aSpeed2 > 1e-6f)
    adjustTimeCoef = sqrt(speedWanted2/aSpeed2);
  saturate(adjustTimeCoef, minSpd, maxSpd);
  // ANIMS-TEMPO: clamp removido em 20/11/2026. Ele existia para compensar as
  // animacoes 1.30 convertidas; com os RTMs nativos 0.45 (anims.pbo original)
  // o coeficiente nativo do motor ja esta correto. O clamp fazia a corrida em
  // diagonal/ lateral ficar lenta (>1.15) e a mira andando ficar rapida (<0.80).

#if 0
  if (this == GWorld->CameraOn())
  {
    LogF
    (
      "adjustTimeCoef %.2f, min %.2f, max %.2f",
      adjustTimeCoef,minSpd,maxSpd
    );
  }
#endif

  turn=0;
  // apply current animation
  AdvanceQueueInfo info;
  //float priPhaseChange,secPhaseChange;
  // note: adjustTimeCoef also determines speed
  bool done = false;
  RStringB primaryMove = GetMovesType()->GetMoveName(_moves.GetPrimaryMove(FutureVisualState()._moves).id);

  //VisualState &vs = FutureVisualState();

  if (CHECK_DIAG(DEAnimation) && Brain()==GWorld->FocusOn())
  {
    DIAG_MESSAGE(2000,"%s: adv. time %.3f*%.3f",cc_cast(GetDebugName()),deltaT,adjustTimeCoef);
    DIAG_MESSAGE(2000,"%s: _stillMoveQueueEnd %s",cc_cast(GetDebugName()),cc_cast(GetMovesType()->GetMoveName(_stillMoveQueueEnd)));
  }

  change = _moves.AdvanceMoveQueue(FutureVisualState()._moves,GetMovesType(), deltaT, adjustTimeCoef, info, done, QIsManual(), this);
  if (done) 
    OnEvent(EEAnimDone, primaryMove);

  moveX = info.moveX;
  moveZ = info.moveZ;

  bool directControl = EnableDirectControl(FutureVisualState());
  // check if the movement allows a speed control
  // if not, we need to respect its speed
  if (directControl)
  {
    // in case of small wanted speeds make sure the speed matches the request
    // this allows for a fine control
    if (_walkSpeedFineControl || !IsLocal())
    {
      // AI prevent moving faster than the animation allows
      // we need to check current animation speed
      MoveInfoMan::SpeedRange moveRngZ = ValueTestEx(FutureVisualState(),&MoveInfoMan::GetStepSpeedRange,MoveInfoMan::SpeedRange(0),CombineMinMax());
      MoveInfoMan::SpeedRange moveRngX = ValueTestEx(FutureVisualState(),&MoveInfoMan::GetStepSpeedRangeX,MoveInfoMan::SpeedRange(0),CombineMinMax());

      if (fabs(moveRngZ.minSpeed)>100 || fabs(moveRngZ.maxSpeed)>100 || fabs(moveRngX.minSpeed)>100 || fabs(moveRngX.maxSpeed)>100)
        RptF("Speed range bad: %g..%g,%g..%g",moveRngZ.minSpeed,moveRngZ.maxSpeed,moveRngX.minSpeed,moveRngX.maxSpeed);
      else
      {
        // we want to allow a slight speed difference to prevent oscillation
        const float allowedDiff = 0.05f;

        moveX = floatMinMax(_walkSpeedWantedX,moveRngX.minSpeed-allowedDiff,moveRngX.maxSpeed+allowedDiff)*deltaT;
        moveZ = floatMinMax(_walkSpeedWantedZ,moveRngZ.minSpeed-allowedDiff,moveRngZ.maxSpeed+allowedDiff)*deltaT;
      }
    }
  }

  DoAssert(fabs(moveX)<100);
  DoAssert(fabs(moveZ)<100);

  if (EnableVisualEffects(prec) && prec<=SimulateVisibleNear)
  {
    ProcessMovesProgress(_moves,FutureVisualState()._moves);
    ProcessMovesProgress(_gesture,FutureVisualState()._gestures);
  }
  if (info.context)
  {
    ProcessMoveFunctionProgress(info.context,1);
    ProcessMoveFunction(info.context);
  }

  VisualState &vs = FutureVisualState();

  if (info.step)
  {
    _doSoundStep = true;
    _soundStepOverride = info.soundOverride;
    _doMark = info.stepLeft+1;
    //GlobalShowMessage
    //(
    //  100,"time %.3f, edges %.3f,%.3f",
    //  oldPrimTime,edge1Time,edge2Time
    //);
  }

  // simulate recoil effects
  if (_recoil)
    change = true;

  // note: suspended soldiers need not recalculate Gun transform
  // unless they are aiming at something
  // RecalcGunTransform is done in MoveWeapons 
  //RecalcGunTransform();

  ActionMap *map = Type()->GetActionMap(_moves.GetPrimaryMove(vs._moves).id, AnimSubIx());
  float turnSpeed = map ? map->GetTurnSpeed() : 0;
  saturate(turnSpeed, 0, 1);
  turn = -_turnWanted*turnSpeed*deltaT;

  if (fabs(turn)>1e-5f)
    change = true;

#if _ENABLE_CHEATS
  if (Brain()==GWorld->FocusOn() && CHECK_DIAG(DEAnimation))
  {
    {
      RString buf = "---- Player -----------------------------------\n";
      buf = buf + _moves.GetDiagInfo(vs._moves,GetMovesType());
      const int nMsg = 10;
      static InitVal<int,-1> handlesMsg[nMsg];
      static bool init = GEngine->AllocateTextHandles(handlesMsg, nMsg);
      GlobalShowMessage(200,buf,handlesMsg,nMsg);
    }
    if (GetGesturesType() && vs._gestureFactor>0)
    {
      RString buf = _gesture.GetDiagInfo(vs._gestures,GetGesturesType());
      const int nMsg = 10;
      static InitVal<int,-1> handlesMsg[nMsg];
      static bool init = GEngine->AllocateTextHandles(handlesMsg, nMsg);
      GlobalShowMessage(200,buf,handlesMsg,nMsg);
    }
    if (_debugLastActionsInfo.lastActions.Size() > 0)
    {
      RString buf = "Last actions:";
      buf = buf + _debugLastActionsInfo.GetDiagInfo(RString());
      const int nMsg = 20;
      static InitVal<int,-1> handlesMsg[nMsg];
      static bool init = GEngine->AllocateTextHandles(handlesMsg, nMsg);
      GlobalShowMessage(200,buf,handlesMsg,nMsg);
    }
  }

  if (CHECK_DIAG(DEAnimation))
  {
    Object *obj = GWorld->GetCameraFocusObject();
    if (obj != NULL)
    {
      Man *man = dyn_cast<Man>(obj);
      if (man == this && _debugLastActionsInfo.lastActions.Size() > 0)
      {
        {
          RString buf = "---- Target -----------------------------------\n";
          buf = buf + _moves.GetDiagInfo(vs._moves,GetMovesType());
          const int nMsg = 10;
          static InitVal<int,-1> handlesMsg[nMsg];
          static bool init = GEngine->AllocateTextHandles(handlesMsg, nMsg);
          GlobalShowMessage(200,buf,handlesMsg,nMsg);
        }
    if (GetGesturesType() && vs._gestureFactor>0)
    {
      RString buf = _gesture.GetDiagInfo(vs._gestures,GetGesturesType());
          const int nMsg = 10;
          static InitVal<int,-1> handlesMsg[nMsg];
          static bool init = GEngine->AllocateTextHandles(handlesMsg, nMsg);
          GlobalShowMessage(200,buf,handlesMsg,nMsg);
        }
        if (_debugLastActionsInfo.lastActions.Size() > 0)
        {
          RString buf = "Last actions:";
          buf = buf + _debugLastActionsInfo.GetDiagInfo(RString());
      const int nMsg = 20;
      static InitVal<int,-1> handlesMsg[nMsg];
          static bool init = GEngine->AllocateTextHandles(handlesMsg, nMsg);
          GlobalShowMessage(200,buf,handlesMsg,nMsg);
    }
  }
    }
  }
#endif

  
  if (GetGesturesType())
  {
    if (vs._gestureFactor<1 && _gesture.GetPrimaryTime(vs._gestures)==0 && _gesture.GetPrimaryMove(vs._gestures).id!=MoveIdNone)
      vs._gestureFactor = floatMin(1.0f, vs._gestureFactor + deltaT*_gesture.GetInterpolSpeed(vs._gestures,GetGesturesType()));
    else
    {
      AdvanceQueueInfo gInfo;
      bool gDone = false;
      
      PlayActionHandlerContext ctx(this); //temporary context just for animhook events
      ctx.AddRef(); //need to prevent calling delete on unref
      gInfo.context = &ctx;

      _gesture.AdvanceMoveQueue(vs._gestures,GetGesturesType(),deltaT,1,gInfo,gDone, false, this);

      if (gInfo.context)
      {
        ProcessMoveFunctionProgress(gInfo.context,1);
        ProcessMoveFunction(gInfo.context);
      }

      const MoveInfoBase *_movesInfo = GetMovesType()->GetMoveInfoBase(_moves.GetPrimaryMove(vs._moves).id, AnimSubIx());
      if ((_gesture.GetMoveQueueSize()==0 && _gesture.GetPrimaryTime(vs._gestures)>=1) || (_movesInfo && _movesInfo->GetTerminal()) )
      {
        // primary movement is done and there are no more moves in the queue
        // we want to terminate the gesture
        // TODO: terminate using interpolation
        vs._gestureFactor -= deltaT*_gesture.GetInterpolSpeed(vs._gestures,GetGesturesType());
        if (vs._gestureFactor<=0)
        {
          vs._gestureFactor = 0;
          // DoAssert(_gesture.CheckValidState(GetGesturesType()));
          _gesture.SwitchMove(vs._gestures,MoveIdNone,NULL);
          // DoAssert(_gesture.CheckValidState(GetGesturesType()));
        }
      }
    }
  }

  // DoAssert(_moves.CheckValidState(GetMovesType()));
  // DoAssert(_gesture.CheckValidState(GetGesturesType()));
  
  const MoveInfoMan * inf = GetMovesType()->GetMoveInfo(_moves.GetPrimaryMove(vs._moves).id, AnimSubIx());
  if(inf)
  {
    if(inf->GetBodyPosition() != _bodyPosition) //to see it's actually working
    {
      _bodyPosition = inf->GetBodyPosition();
      _poseAudibility = Type()->_poseAudibility[_bodyPosition];
      _poseVisibility = Type()->_poseVisibility[_bodyPosition];
    }
  }
  return change;
}

void Man::BasicSimulationCore(float deltaT, SimulationImportance prec)
{
  VisualState& vs = FutureVisualState();

  // Fatigue model
  {

    // how much work is current activity
    // consider speed
    // when laying, speed is more important

    //float speedUz = FutureVisualState()._speed.Y();

    // when holding breath, we will get tired quickly

    float duty = ValueTest(vs,&MoveInfoMan::GetDuty);
    // when carrying a big weight, we will get tired as well
    const float encumbranceCoef = 1.0f;
    duty += encumbranceCoef * GetEncumbrance();
    // personality duration
    float personality = GetInvAbility(AKEndurance);
    if (duty<0) personality = 1; // refreshing is same speed for all soldiers
    // "bad" soldier are tired much faster
    _tired += duty *(1.0f/20)*personality*deltaT;
    saturate(_tired,0,1);
  }

  // Update the metabolism factor
  {
    if (_isDead)
    {
      static float hoursToCoolDown = 0.5f; 
      const float invSecToCoolDown = 1.0f / (hoursToCoolDown * 60.0f * 60.0f);
      _metabolismFactor -= deltaT * invSecToCoolDown;
      if (_metabolismFactor < 0.0f) _metabolismFactor = 0.0f;
    }
    else
    {
      _metabolismFactor = 1.0f;
    }
  }

  bool randomLip = false;
  const float maxRadius2 = Square(50.0f);
  bool audible = false;
  if (GScene->GetCamera())
  {
    Vector3Val pos = GScene->GetCamera()->Position();
    audible = pos.Distance2(WorldPosition(vs)) <= maxRadius2;
  }
  _directSpeaking.SetAudible(audible);
  _directSpeaking.Simulate(deltaT, this);

  if (_directSpeakingSound)
    randomLip = true;

  if (_speakingSound3D)
  {
    // TODO: use real lip-sync data
    randomLip = true;
    if (_isDead || _speakingSound3D->IsTerminated())
      _speakingSound3D.Free();
  }
  else if (this==GWorld->GetRealPlayer())
  {
    if (GetNetworkManager().IsVoiceRecording())
      randomLip = true;
  }
  else if (IsRemotePlayer())
  {
    if (GetNetworkManager().IsVoicePlaying(GetRemotePlayer()))
      randomLip = true;
  }
#if _ENABLE_CHEATS
  else
  { // RandomLip for AI speaking by VoNSay
    if (GetNetworkManager().IsVoicePlaying(GetVoNSayDpnid()))
      randomLip = true;
  }
#endif

  // if _lipInfo loaded and non-empty do not use randomLip
  if (_head._lipInfo && !_head._lipInfo->IsEmpty())
    SetRandomLip(false);
  else
  SetRandomLip(randomLip);

#if _ENABLE_NEWHEAD
  if (prec <= SimulateVisibleNear)
    _head.Simulate(deltaT, prec, _isDead);
#else
  if (prec <= SimulateVisibleNear)
    _head.Simulate(Type()->_head,deltaT, prec, _isDead);
#endif

  if (_isDead)
  {
    _leanZRotWanted = 0;
    _turnWanted = 0;
    //_walkSpeedWanted=0;
    //_sideSpeedWanted=0;

    // for terminal animation, do not process even Die action
    // (ArmA2 fix - news:h0nmcq$jvn$1@new-server.localdomain)
    MoveId primary = _moves.GetPrimaryMove(vs._moves).id;
    const MoveInfoBase *info = GetMovesType()->GetMoveInfoBase(primary, AnimSubIx());
    if (!info || !info->GetTerminal())
    {
      ActionMap *map = Type()->GetActionMap(primary, AnimSubIx());
      // get which action is it (based on action map)
      if (map)
      {
        MoveId moveWanted = Type()->GetMove(map, ManActDie);
        if (moveWanted!=MoveIdNone)
        {
          Ref<ActionContextDefault> context = new ActionContextDefault;
          context->function = MFDead;
          SetMoveQueue(MotionPathItem(moveWanted, context), prec<=SimulateVisibleFar);
        }
        _moves.ResetForcedQueue();
      }    
    }

    if (prec <= SimulateVisibleNear)
    {
#if _ENABLE_NEWHEAD
    if (_headType.NotNull())
    {
      _head.SetGrimace(_headType->_grimaceIndexDead);
      // discard any forced grimace, so that dead can be used
      _head.SetForcedGrimace(-1);
    }
#else
    _head.SetForceMimic("");
    _head.SetMimicMode("dead");
#endif
    }
  }
  else
  {
    if (prec <= SimulateVisibleNear)
    {
      // 
      AIBrain *unit = _brain;
      if (unit)
      {
        CombatMode mode = unit->GetCombatModeLowLevel();
        if (unit->IsDanger())
        {
#if _ENABLE_NEWHEAD
          if (_headType.NotNull())
            _head.SetGrimace(_headType->_grimaceIndexDanger);
#else
          _head.SetMimicMode("danger");
#endif
        }
        else if (_whenScreamed<Glob.time && _whenScreamed>Glob.time-1)
        {
#if _ENABLE_NEWHEAD
          if (_headType.NotNull())
            _head.SetGrimace(_headType->_grimaceIndexHurt);
#else
          _head.SetMimicMode("hurt");
#endif
        }
        else switch (mode)
        {
        case CMAware:
          if (_itemInHands && _itemInHands->GetWeapon())
          {
#if _ENABLE_NEWHEAD
            if (_headType.NotNull())
              _head.SetGrimace(_headType->_grimaceIndexAware);
#else
            _head.SetMimicMode("aware");
#endif
          }
          else
          {
#if _ENABLE_NEWHEAD
            if (_headType.NotNull())
              _head.SetGrimace(_headType->_grimaceIndexSafe);
#else
            _head.SetMimicMode("safe");
#endif
          }
          break;
        case CMCombat:
        case CMStealth:
#if _ENABLE_NEWHEAD
          if (_headType.NotNull())
            _head.SetGrimace(_headType->_grimaceIndexCombat);
#else
          _head.SetMimicMode("combat");
#endif
          break;
        default:
#if _ENABLE_NEWHEAD
          if (_headType.NotNull())
            _head.SetGrimace(_headType->_grimaceIndexSafe);
#else
          _head.SetMimicMode("safe");
#endif
          break;
        }
      }
    }
  }

  // animations streaming - load needed frames
  _moves.LoadAnimationPhases(vs._moves,GetMovesType(),_timeOffset);
  if (vs._gestureFactor > 0 && GetGesturesType())
    _gesture.LoadAnimationPhases(vs._gestures,GetGesturesType(),_timeOffset);
}

float Man::GetLegPhase() const
{
  return _moves.GetPrimaryTime(FutureVisualState()._moves);
}

float Man::GetPrimaryMoveTimeLeft() const
{
  return _moves.GetPrimaryTimeLeft(FutureVisualState()._moves,GetMovesType());
}

RStringB Man::GetPrimaryMoveName() const
{
  return GetMovesType()->GetMoveName(_moves.GetPrimaryMove(FutureVisualState()._moves).id);
}

void Man::BasicSimulation(float deltaT, SimulationImportance prec, float speedFactor)
{
  AgeVisualStates(deltaT);

  //  animation should also run inside of vehicle
  float turn,moveX,moveZ;

  RefreshMoveQueue(prec<=SimulateVisibleFar);

  bool forceRecalcMatrix = MoveHeadCrew(deltaT);
  if (forceRecalcMatrix)
    RecalcGunTransform();

  // keep doors open if getting in, because it can be closed by somebody else // todo find better place to keep them open
  if (Brain() && Brain()->GetVehicleIn() && IsActionInProgress(MFPreciseGetInEnd))
    Brain()->GetVehicleIn()->OpenDoor(Brain());

  SimulateForcedMove(prec);

  SimulateAnimations(turn,moveX,moveZ,deltaT*speedFactor,prec);
  // note: results are ignored

  BasicSimulationCore(deltaT,prec);

  _simulationSkipped = 0;
}

Vector3 Man::PlacingPoint() const
{
  // assume landcontact point is always at y=0
  return VZero;
}

void Man::PlaceOnSurface(Matrix4 &trans)
{
  // assume landcontact point is always at y=0
  // calculate animated point position
  // place in steady position
  Vector3 pos=trans.Position();
  Matrix3 orient=trans.Orientation();
  
  pos[1] = GLOB_LAND->RoadSurfaceYAboveWater(pos+VUp*0.5f);
  trans.SetPosition(pos);
}

void Man::RecalcPositions(const Frame &moveTrans)
{
  VisualState &vs = FutureVisualState();
  vs._aimingPositionWorld = CalculateAimingPosition(moveTrans);
  vs._cameraPositionWorld = CalculateCameraPosition(moveTrans);
  //_headPositionWorld = CalculateHeadPosition(moveTrans);
#if _ENABLE_REPORT
  if (vs._aimingPositionWorld.SquareSize()<=Square(10))
  {
    // Get designers to know more: Warning: Object B 1-1-B:10 (emtydoor.p3d) at position [1,1,0.356] too close to [0,0,0].
    LogF("Warning: %s at position [%.2f,%.2f,%.2f] too close to [0,0,0].", cc_cast(GetDebugName()), vs._aimingPositionWorld.X(), vs._aimingPositionWorld.Y(), vs._aimingPositionWorld.Z());
  }
#endif
  
  //Assert (_headPositionWorld.SquareSize()>Square(10));
  LandSlope(_forceStand, _landGradFwd, _landGradAside);
}

void Man::OnPositionChanged()
{
  _landContact = false;
  _objectContact = false;
  _objectContactNonStatic=false;
  RecalcPositions(GetFrameBase());
  base::OnPositionChanged();
}

/** Function simulates free fall mode. 
 @param moveTrans - (in/out) new man position and orientation.
 @param deltaT - (in) simulation time step. 
 @param prec - (in) simulation precision.
 @return true if transformation changed.
 */
const float shortFreeFallThreshold = 0.6f;
bool Man::SimulateFreeFall(ManVisualState & moveTrans, float deltaT, SimulationImportance prec)
{   
  bool shortFall = false;
  if (_freeFallStartTime==TIME_MAX)
  {
    _freeFallStartTime = Glob.time;
  }
  else if (_landContact) //prevent freeFall to continue after short falls
  {
    if (Glob.time - _freeFallStartTime < shortFreeFallThreshold)
    {
      _freeFallUntil = Glob.time;
	  shortFall = true;
    }
  }
  PROFILE_SCOPE(manFF);

  //float fVoll = ValidatePotentialState(moveTrans, Color(0,1,0));

  // simulate interaction with land and objects
  Vector3 friction(VZero),torqueFriction(VZero);
  Vector3 force(VZero),torque(VZero);
  Vector3 pForce(VZero),pCenter(VZero);
  Vector3 repairMove(VZero);

  
  //bool objectContactOld = _objectContact;
  // apply friction and create moveTrans

  CreatePotentialStateFreeFall(moveTrans, friction, torqueFriction, repairMove, deltaT, prec);
    
  if (_primaryMoveBackUp.id != _moves.GetPrimaryMove(FutureVisualState()._moves).id ||
    _primaryTimeBackUp != _moves.GetPrimaryTime(FutureVisualState()._moves) ||
    _primaryFactorBackUp != _moves.GetPrimaryFactor(FutureVisualState()._moves)) //I do not have control of the move so let disable dammage system.
  {
#if _DEBUG
    RStringB str1 = GetMovesType()->GetMoveName(_primaryMoveBackUp.id);
    RStringB str2 = GetMovesType()->GetMoveName(_moves.GetPrimaryMove(FutureVisualState()._moves).id);      
#endif
    _collVolume = INFINITY_VOLUME;
  }

  // measure new state 
  
  ObjectArray collObjects;
  float fVoll = 0;

  Vector3 repairMove2(VZero);
  float fCollVol = CheckAndRepairPotencialStateFreeFall(moveTrans, repairMove2, collObjects, deltaT, fVoll, true);
  repairMove += repairMove2;

  _walkingOnObject = _landContact || (_objectContact && (repairMove[1] * repairMove[1]) > 0.09f * repairMove.SquareSize());

  if (fCollVol > 0)
    fVoll = ValidatePotentialStateThis();

  if (fVoll > _collVolume && fCollVol > 0 && _disableDamageFromObjUntil < Glob.time && !_purePersonContact)
    DammageOnCollVol(fCollVol);                           

  // apply gravity
  // We apply full force only if man was falling without contact. 
  // If man was shifted by repairMove, apply only proportional part.
  if (fVoll >= fCollVol * ALLOWED_VOL_GROW)
  {  
    float diffMove = FutureVisualState().Position()[1] - moveTrans.Position()[1];
    float coef = 1.0f;
    if (repairMove[1] != 0)
    {
      float unrepairedMove = diffMove + repairMove[1];
      if (fabs(unrepairedMove) > 0.00001f) // numerical barrier preventing zero division
      {      
        coef = diffMove / unrepairedMove;
        saturateMin(coef,1.0f);
        saturateMax(coef,0);
      }
      else
        coef = 0; 
    }

    force[1]=-G_CONST*GetMass() * coef;
  }  

  // do impulses and dammage
  if (_waterContact)
  {
    const SurfaceInfo &info = GLandscape->GetWaterSurface();
    _surfaceSound = info._soundEnv;

    // check water hit speed
    if (GetNetworkManager().IsServerOwner() && Glob.time>_disableDamageUntil)
    {
      float maxCrashSpeed=20;
      if (FutureVisualState()._speed.SquareSize()>=Square(maxCrashSpeed))
      {
        float dammage=(FutureVisualState()._speed.Size()-maxCrashSpeed)*0.1f;
        float armorCoef = GetInvArmor()*3;
        dammage *= armorCoef;
        LocalDamageMyself(VZero,dammage,2.0f);
      }
    }            
  }

  // check land hit speed
  //GlobalShowMessage(50, "%f ,%f, %f, %s, %f, %f", FutureVisualState()._speed[0], FutureVisualState()._speed[1], FutureVisualState()._speed[2], Glob.time>_disableDamageUntil ? "A" : "N", _landDX, _landDX);
  if (_landContact)
  {
    Vector3 landNormal(-_landDX, 1, -_landDZ);
    landNormal.Normalize();


    float maxCrashSpeed = 7;
    float normalSpeed = - FutureVisualState()._speed * landNormal;
    
    if(GetNetworkManager().IsServerOwner() &&  normalSpeed >=maxCrashSpeed && Glob.time>_disableDamageUntil)
    {
      float dammage=(normalSpeed-maxCrashSpeed)*0.3f;
      float armorCoef = GetInvArmor()*3;
      dammage *= armorCoef;
      LocalDamageMyself(VZero,dammage,2.0f); 
    }

    maxCrashSpeed = 10;
    Vector3 tangetialSpeed = FutureVisualState()._speed + landNormal * normalSpeed;
    float tangetialSpeedSize = tangetialSpeed.Size();
    if (GetNetworkManager().IsServerOwner() && tangetialSpeedSize >= maxCrashSpeed && Glob.time>_disableDamageUntil)
    {
      float dammage=(tangetialSpeedSize-maxCrashSpeed)*0.15f;
      float armorCoef = GetInvArmor()*3;
      dammage *= armorCoef;
      LocalDamageMyself(VZero,dammage,2.0f); 
    }
    // Reduce speed in dirout direction to zero.                                
    if (normalSpeed > 0)
    {
      FutureVisualState()._speed = tangetialSpeed;
      // Reduce speed in other directions.                                    
#define IMPULSE_FRICTION 0.4
      if (tangetialSpeedSize > 0.001f)
        FutureVisualState()._speed -= floatMin(IMPULSE_FRICTION * normalSpeed, tangetialSpeedSize) * FutureVisualState()._speed / tangetialSpeedSize;
    }                                       
  }

  if (_objectContact && collObjects.Size() > 0) 
  {
    //The following part is not very physically correct but somehow it works
    Vector3 avrObjectSpeed(VZero);
    float fInvMassGlobal = 0;
    float fMassGlobal = 0;

    for(int i = 0; i < collObjects.Size(); i++)
    {
      if (collObjects[i]->Static())
        continue;

      Entity * ent = dyn_cast<Entity,Object>(collObjects[i]);
      if (ent != NULL)
      {
        fInvMassGlobal += ent->GetInvMass();
        fMassGlobal += ent->Mass();
        avrObjectSpeed += ent->FutureVisualState().Speed();
      }
    }

    avrObjectSpeed /= collObjects.Size();

    Vector3 diffSpeed = FutureVisualState()._speed - avrObjectSpeed;
    if (repairMove.SquareSize() < 0.0001f)
    {
      if (diffSpeed.SquareSize() > 0.0001f)
      {
        repairMove = diffSpeed;
        repairMove.Normalize();
      }
      else
        repairMove = VZero;
    }
    else
      repairMove.Normalize();

    float fNormalSpeed = - repairMove * diffSpeed;

    if (fNormalSpeed > 0)
    {
      float fNormalImpulse = fNormalSpeed / (fInvMassGlobal + GetInvMass());
      Vector3 impulse = fNormalImpulse * repairMove;

      Vector3 tangentialSpeed = diffSpeed + fNormalSpeed * repairMove;
      float tangentialSpeedSize = tangentialSpeed.Size();

      if (tangentialSpeedSize > 0.0001f)
      {
        float fTangentialImpulse = floatMin(fNormalImpulse * IMPULSE_FRICTION, tangentialSpeedSize * Mass());
        impulse -= tangentialSpeed / tangentialSpeedSize * fTangentialImpulse;
      }

      FutureVisualState()._speed += impulse * GetInvMass();
      
      if (IsLocal())
      {
        float impulseSize = impulse.Size() - 5 * GetMass();
        if (impulseSize * GetInvMass() > 5 && Glob.time>_disableDamageUntil)
          DamageOnImpulse(this, impulseSize, VZero);

        AddImpulseNetAwareAccumulator accum;
        for(int i = 0; i < collObjects.Size(); i++)
        {
          Entity * ent = dyn_cast<Entity,Object>(collObjects[i]);          
          if (ent != NULL && (NULL ==  dyn_cast<Man,Entity>(ent) || impulseSize * GetInvMass() > 5.0f))
          {
            Vector3 applyImpulse;
            if (ent->Static())
              applyImpulse = -impulse / collObjects.Size();
            else
              applyImpulse = -impulse * ent->Mass() / fMassGlobal;

            accum.Add(ent, applyImpulse,VZero);

            if (!ent->IsLocal())
              ent->AddImpulse(applyImpulse,VZero);
          }
        }
      }
    }
  }

  bool bChange = true;
  if (fVoll < fCollVol * ALLOWED_VOL_GROW)
  {
    moveTrans.SetPosition(FutureVisualState().Position()); // brutal force use old position but new rotation
    // rotation must be always allowed

    //bChange = false;
    //UseOldState();
    /// save the system allow turning and always take result
    //CreatePotentialStateFreeFall(moveTrans,friction,torqueFriction,deltaT, prec, false);

    _collVolume = CheckAndRepairPotencialStateFreeFall(moveTrans, repairMove, collObjects, deltaT);
    moveTrans.SetPosition(moveTrans.Position() + repairMove);

    if (_afterGetOut && Glob.time < _stuckStarted )
      // is stuck (cannot move because of collision)
      _stuckStarted = Glob.time;      
  }
  else
  {
    // use new state
    _collVolume = fCollVol;

    if (_afterGetOut )
    {
      const float MIN_SPEED_FREEFALL_SQ = 1; 
      if ((FutureVisualState().Position() - moveTrans.Position()).SquareSize() > MIN_SPEED_FREEFALL_SQ * deltaT * deltaT)
        _stuckStarted = TIME_MAX;
      else if (Glob.time < _stuckStarted) 
        _stuckStarted = Glob.time;     // is stuck (cannot move because of collision)
    }
  }


  const float STUCK_CRIT_TIME = 5;
  if (Glob.time - STUCK_CRIT_TIME > _stuckStarted) // stuck too long ignore collisions
    _ignoreCollisionIfStuck = !IsNetworkPlayer(); // do not apply that for players... 

  ApplyForces(deltaT,force,torque,friction,torqueFriction);

  // Force negative Y-speed to stay in contact.
  //if (_objectContact && !_landContact && FutureVisualState()._speed[1] > -1 && FutureVisualState()._speed[1] < 1)
  //     FutureVisualState()._speed[1] = -1;

  DirectionWorldToModel(FutureVisualState()._modelSpeed, FutureVisualState()._speed);

  _lastMovementTime=Glob.time;  

  // Do static collisions a while after freefall
  if (Glob.time + 1.0f > _staticCollisionUntil)
    _staticCollisionUntil = Glob.time + 1.0f;

  //Do freefall if speed is high
  if (!shortFall && FutureVisualState()._modelSpeed.SquareSize() > 10.0f)
    FreeFall();

  _primaryMoveBackUp = _moves.GetPrimaryMove(FutureVisualState()._moves);   
  _primaryTimeBackUp = _moves.GetPrimaryTime(FutureVisualState()._moves);
  _primaryTimeDBackUp = _moves.GetPrimaryTimeDelta(FutureVisualState()._moves);
  _primaryFactorBackUp = _moves.GetPrimaryFactor(FutureVisualState()._moves);
  _finishedBackUp = _moves.GetFinished(FutureVisualState()._moves);

  return bChange;
}

// define ammuniton types (it is taken from core config file cfg\Bin\cfgWeapons.hpp)
#define WeaponNoSlot		0	// dummy weapons
#define WeaponSlotHandGun	2	// HandGun
#define WeaponSlotHandGunItem	16 // HandGun magazines
#define WeaponSlotInventory 131072 // inventory items

int Man::HeavyWeaponsCount()
{
  AIBrain *unit = Brain();
  if (!unit) return 0;

  int count = 0;
  GetWeapons().ForEachWeapon([&count](Weapon *weapon) -> bool
  {
    const WeaponType *type = weapon->Type();
    if (!type) return false; // continue
    switch (type->_weaponType)
    {
    case WeaponNoSlot:
    case WeaponSlotHandGun:
    case WeaponSlotInventory:
      break; // do not drop light weapons or inventory items (HandGun, map, GPS, watches, compass, ...)
    default: count++;
      break;
    }
    return false; // continue
  });
  GetWeapons().ForEachMagazine([&count](Magazine *mag) -> bool
  {
    const MagazineType *type = mag->Type();
    if (type && type->_magazineType != WeaponSlotHandGunItem)
      count++;
    return false; // continue
  });
  return count;
}

void Man::DropHeavyWeaponsWhileSwimming()
{
  if(!GWorld->IsEnabledItemsDropping()) return;

  AIBrain *unit = Brain();
  if (!unit) return;

  ForEachItemReverse([this](InventoryItem *item) -> bool
  {
    Weapon *weapon = item->GetWeapon();
    if (weapon)
    {
    const WeaponType *type = weapon->Type();
    if (!type) return false; // continue
    switch (type->_weaponType)
    {
    case WeaponNoSlot:
    case WeaponSlotHandGun:
    case WeaponSlotInventory:
      break; // do not drop light weapons or inventory items (HandGun, map, GPS, watches, compass, ...)
    default: 
        DropItem(weapon);
      break;
    }
    return false; // continue
    }

    Magazine *magazine = item->GetMagazine();
    if (magazine)
    {
      const MagazineType *type = magazine->Type();
      if (type && type->_magazineType != WeaponSlotHandGunItem)
          DropItem(magazine);
        return false; // continue
    }
    return false; // continue
  });
  if (GetInSlot(ISBack)) DropFromSlot(ISBack);
}

#define MAX_STEP_UP 0.5f  // maximal height of person vertical step
#define MAX_STEP_DOWN 0.5f  // in future will be better to define max speed for up and down change. Now max steps are not depending on sim time...


void Man::ApplyTurn(VisualState &vs, float turn)
{
  if (IsDead()) return;
  // check current up and turn
  if (fabs(turn)>1e-6)
  {
    Matrix3 orient;
    // some orientation change - set orientation in matrix
    // note: we want to rotate around point in landcontact

    Matrix3 rotate(MRotationY,turn);
#if _VBS3
    orient = rotate*vs.Orientation();
#else
    orient.SetUpAndDirection(VUp,rotate*vs.Direction());
#endif

    vs.SetOrientation(orient);        
  }

}

/** Function in fact provides collision detection with ground (or roadway). First it adjust rotation of moveTrans according
 to the ground normal and the requested turn. Then it moves a land contact on the ground. And at the end it calculates legs orientation
 etc..
 @param moveTrans - investigated man position and orientation.
 @param turn - requested turn.
 @param deltaT - simulation time step.
 @param changeTrans - if true moveTrans can be changed, i false moveTrans must be unchanged.
 @see #_legTrans
 */
void Man::SetleInPosition(ManVisualState& moveTrans, float turn, float deltaT,  bool changeTrans /* = true*/)
{
  if (changeTrans)
  {    
    if (IsDead())
    {
      Matrix3 orient;
      float dx,dz;
      GLOB_LAND->RoadSurfaceY(moveTrans.Position()+VUp*0.5f,&dx,&dz);
      Vector3Val landUp=Vector3(-dx,1,-dz).Normalized();
      Vector3 toUp=landUp-moveTrans.DirectionUp();
      float maxD=1*deltaT;
      saturate(toUp[0],-maxD,+maxD);
      saturate(toUp[1],-maxD,+maxD);
      saturate(toUp[2],-maxD,+maxD);
      orient.SetUpAndDirection(moveTrans.DirectionUp()+toUp,moveTrans.Direction());      
      moveTrans.SetOrientation(orient);        
    }
  }

  float underBias=MAX_STEP_UP;
  //    bool steadyBody = false;
  //    float landDX=0,landDZ=0;

  // check collision with ground
  _landContact = false;
  _objectLandContact = false;

  _waterContact = false;
  float maxUnder= -MAX_STEP_DOWN - deltaT * fabs(FutureVisualState()._modelSpeed[2]); // At low FPS if man is moving to fast check for roadway lower, because it can fly over the stairs... 
  float waterDepthForSwimming = 0;

  if (!_isDead)
  {
    // calculate landcontact point position

#if !_RELEASE
    int level=GetShape()->FindLandContactLevel();
    DoAssert(level>=0);
    // verify this level has just one point
    //Shape *lc=GetShape()->Level(level);
    //DoAssert(lc->NPos()==1);
#endif
    _waterDepth=0;
    GroundCollisionBuffer retVal;
    float under = underBias-_hideBody;
    GLOB_LAND->GroundCollision(retVal,this,moveTrans,under,0,true,true);
    //ADD_COUNTER(sGndC,retVal.Size());       

    if (retVal.Size()>0)
    {
      for (int i=0; i<retVal.Size(); i++)
      {
        const UndergroundInfo &info=retVal[i];
        float under = info.under-underBias;   
        //LogF("under %lf, maxUnder %lf",under,MAX_STEP_DOWN + deltaT * fabs(_modelSpeed[2]));

        if (info.type==GroundSolid)
        {          
          if (maxUnder<under)
          { 
            maxUnder = under;

            _landContact=true;
            _objectLandContact = const_cast<Object*>(info.obj);
            if (info.texture) 
            {
              _surfaceAudibility = info.texture->GetSurfaceInfo()._audibility;
              _surfaceVisibility = info.texture->GetSurfaceInfo()._visibility[_bodyPosition];
              _surfaceSound = info.texture->GetSoundEnv();
            }
            else
            {
              _surfaceVisibility = 1;
              _surfaceAudibility = 1;
            }

            _landContactPos = info.pos;

            _landDX=info.dX,_landDZ=info.dZ;

            //LogF("ground %.2f,%.2f",landDX,landDZ);
          }
          if (under < 0) 
            _swimming = false;
        }
        else if(info.type==GroundWater)
        {
          saturateMax(_waterDepth,info.under-underBias);
          waterDepthForSwimming = info.under-underBias;
          _waterContact = true;
        }
      }
    }
    if (_waterContact)
    {
      const SurfaceInfo &info = GLandscape->GetWaterSurface();
      _surfaceSound = info._soundEnv;
    }        
  }
  else
  {
    float under = underBias-_hideBody;

    GroundCollisionBuffer retVal;
    GLOB_LAND->GroundCollision(retVal,this,moveTrans,under,0,true,true);
    for (int i=0; i<retVal.Size(); i++)
    {
      const UndergroundInfo &info=retVal[i];
      if (info.under<0)
        continue;
      if (info.type==GroundSolid)
      {
        saturateMax(maxUnder,info.under-underBias);

        _landContact=true;
        _landContactPos = info.pos;

        _landDX=info.dX,_landDZ=info.dZ;
        //LogF("dead ground %.2f,%.2f",landDX,landDZ);
      }
      else if (info.type==GroundWater)
      {                    
        saturateMax(_waterDepth,info.under-underBias);
        _waterContact = true;               
      }
    }
  }
  // avoid underland situation
  if (_landContact && changeTrans)
  {    
    Vector3 position=moveTrans.Position();
    position[1]+=maxUnder;
    moveTrans.SetPosition(position);  
    _landContactPos[1] += maxUnder;
  }
  // avoid drowning (update moveTrans position when completely under water)
  if (_waterDepth>1.4f && IsAbleToStand()) 
  {
    //if (!_swimming) DropHeavyWeaponsWhileSwimming();
    _swimming = true;
  }
  if (IsSwimmingInMove() && _swimming)
  {
    Vector3 position=moveTrans.Position();
    position[1] += waterDepthForSwimming;
    moveTrans.SetPosition(position);
  }

  // Legs transform and company

  float onLand = ValueTest(FutureVisualState(),&MoveInfoMan::OnLand);
  if (onLand<=1e-5)
  { // common case - not on the land. Skew only
    float legDX = _landDX;
    float legDZ = _landDZ;
    // calculate normal
    // avoid extreme skew
    saturate(legDX,-0.5f,+0.5f);
    saturate(legDZ,-0.5f,+0.5f);

    Vector3 dxdz(legDX,0,legDZ);

    Matrix4 invMoveTrans = moveTrans.CalcInvTransform(
      #ifdef _DEBUG
      GetShape()->Name()
      #endif
    );
    dxdz = invMoveTrans.Rotate(dxdz);

    FutureVisualState()._legTrans = M4Identity;
    // keep legs on the ground using skew
    FutureVisualState()._legTrans(1,0) = dxdz.X();
    FutureVisualState()._legTrans(1,2) = dxdz.Z();  
  
  }
  else
  { // combine skew and rotation together
    float legDX = _landDX;
    float legDZ = _landDZ;
    // calculate normal
    Vector3 dxdzNormal(-legDX,1,-legDZ);
    // avoid extreme skew
    saturate(legDX,-0.5f,+0.5f);
    saturate(legDZ,-0.5f,+0.5f);

    Vector3 dxdz(legDX,0,legDZ);

    Matrix4 invMoveTrans = moveTrans.CalcInvTransform(
      #ifdef _DEBUG
      GetShape()->Name()
      #endif
    );
    dxdz = invMoveTrans.Rotate(dxdz);
    
    dxdzNormal = invMoveTrans.Rotate(dxdzNormal);

    Matrix4 skew(MIdentity);
    // keep legs on the ground using skew
    skew(1,0) = dxdz.X();
    skew(1,2) = dxdz.Z();  
    
    // keep legs on the ground using rotation
    Matrix4 rot(MUpAndDirection,dxdzNormal,VForward);
    // blend between skew and rotation
    // keep legs on the ground using skew
    FutureVisualState()._legTrans = skew*(1-onLand)+rot*onLand;
  }
}


/** Function in fact provides collision detection with ground (or roadway). If land contact is under the ground it pushes moveTrans up.
@param moveTrans - investigated man position and orientation.
*/
void Man::SetleInPositionFreeFall(ManVisualState& moveTrans)
{
  // Resolve collisions
  // Collision's with ground.
  const float underBias = 0.05f; 
  GroundCollisionBuffer retVal;     

  GLOB_LAND->GroundCollision(retVal,this,moveTrans,underBias,0,true,true);

  _landContact = false;
  _waterContact = false;
  _waterDepth=0;

  _landDX = 0;  
  _landDZ = 0;

  float waterDepthForSwimming = 0;
  if( retVal.Size()>0 )
  {
    float maxUnder = -underBias;

    for( int i=0; i<retVal.Size(); i++ )
    {
      const UndergroundInfo &info=retVal[i];
      float under = info.under - underBias;
      if( under<0 ) continue;
      if( info.type==GroundSolid )
      {
        //float under = info.under - underBias;
        //if (under > -0.01f)     // If landcontact is exactly happens under is 0 +- numerical error.  
        _landContact = true;

        if (maxUnder<=under)
        {
          maxUnder = under;                    
          _landDX=info.dX,_landDZ=info.dZ;
          _landContactPos = info.pos;
          if (info.texture) 
            _surfaceSound = info.texture->GetSoundEnv();                                       
        }
        if (under < 0) 
        {
          _swimming = false;
        }      
      }
      else if( info.type==GroundWater )
      {
        saturateMax(_waterDepth,under/*-underBias*/);
        waterDepthForSwimming = info.under/*-underBias*/;
        _waterContact = true;               
      }
    }

    if (_landContact)
    {
      // push up man from land                       
      Vector3 position=moveTrans.Position();
      position[1]+= maxUnder;
      _landContactPos[1] += maxUnder;
      moveTrans.SetPosition(position);

      // Legs transform and company
      float onLand = ValueTest(FutureVisualState(),&MoveInfoMan::OnLand);
      if (onLand<=1e-5)
      { // common case - not on the land. Skew only
        float legDX = _landDX;
        float legDZ = _landDZ;
        // calculate normal
        // avoid extreme skew
        saturate(legDX,-0.5f,+0.5f);
        saturate(legDZ,-0.5f,+0.5f);

        Vector3 dxdz(legDX,0,legDZ);

        Matrix4 invMoveTrans = moveTrans.CalcInvTransform(
          #ifdef _DEBUG
          GetShape()->Name()
          #endif
        );
        dxdz = invMoveTrans.Rotate(dxdz);

        // keep legs on the ground using skew
        FutureVisualState()._legTrans(1,0) = dxdz.X();
        FutureVisualState()._legTrans(1,2) = dxdz.Z();  

      }
      else
      { // combine skew and rotation together
        float legDX = _landDX;
        float legDZ = _landDZ;
        // calculate normal
        Vector3 dxdzNormal(-legDX,1,-legDZ);
        // avoid extreme skew
        saturate(legDX,-0.5f,+0.5f);
        saturate(legDZ,-0.5f,+0.5f);

        Vector3 dxdz(legDX,0,legDZ);

        Matrix4 invMoveTrans = moveTrans.CalcInvTransform(
          #ifdef _DEBUG
          GetShape()->Name()
          #endif
        );
        dxdz = invMoveTrans.Rotate(dxdz);

        dxdzNormal = invMoveTrans.Rotate(dxdzNormal);

        Matrix4 skew(MIdentity);
        // keep legs on the ground using skew
        skew(1,0) = dxdz.X();
        skew(1,2) = dxdz.Z();  

        // keep legs on the ground using rotation
        Matrix4 rot(MUpAndDirection,dxdzNormal,VForward);
        // blend between skew and rotation
        // keep legs on the ground using skew
        FutureVisualState()._legTrans = skew*(1-onLand)+rot*onLand;
      }
    }
    else
    {
      FutureVisualState()._legTrans = MIdentity;
    }
    // avoid drowning (update moveTrans position when completely under water)
    if (_waterDepth>1.4f && IsAbleToStand()) 
    {
      //if (!_swimming) DropHeavyWeaponsWhileSwimming();
      _swimming = true;
    }
    if (IsSwimmingInMove() && _swimming)
    {
      Vector3 position=moveTrans.Position();
      position[1] += waterDepthForSwimming;
      moveTrans.SetPosition(position);
    }
  }
  else
  {
    FutureVisualState()._legTrans = MIdentity;
  }
}
/** Function creates new state. The first it backups values, needed later for initial state restoration, 
then it advances animation and calculates forces acting on man. The result is new moveTrans.
@param moveTrans - (out) new man position and orientation.
@param friction - (out) friction force acting on man.
@param torqueFriction - (out) torque made by friction forces.
#param repairMove - (out) move done expect free fall 
@param deltaT - (in) simulation time step.
@param prec - (in) simulation precision.
@return true if anything changed.
*/
bool Man::CreatePotentialStateFreeFall(ManVisualState & moveTrans, Vector3& friction, Vector3& torqueFriction,Vector3& repairMove, float deltaT, SimulationImportance prec)
{
  //ADD_COUNTER(mancre,1);
  // BackUp current animation state
  _primaryMoveBackUp = _moves.GetPrimaryMove(FutureVisualState()._moves); // Is used in ValidatePotentialStateThis
  _secondaryMoveBackUp = _moves.GetSecondaryMove(FutureVisualState()._moves);
  _primaryFactorBackUp = _moves.GetPrimaryFactor(FutureVisualState()._moves); 
  _primaryTimeBackUp = _moves.GetPrimaryTime(FutureVisualState()._moves);
  _primaryTimeDBackUp = _moves.GetPrimaryTimeDelta(FutureVisualState()._moves);
  _secondaryTimeBackUp = _moves.GetSecondaryTime(FutureVisualState()._moves);
  _secondaryTimeDBackUp = _moves.GetSecondaryTimeDelta(FutureVisualState()._moves);
  _finishedBackUp = _moves.GetFinished(FutureVisualState()._moves);
  
  _legTransBackUp = FutureVisualState()._legTrans;
  _hideBodyBackUp = _hideBody;
  _waterDepthBackUp = _waterDepth;
  _landContactBackUp = _landContact;  
  _waterContactBackUp = _waterContact;
  _surfaceSoundBackUp =  _surfaceSound;
  _speedBackUp = FutureVisualState()._speed;
  _angMomentumBackUp = _angMomentum;
  _staticCollisionBackUp = _staticCollision;
  _useSafeCapsuleBackUp = _useSafeCapsule;

  // Create new animation state
  float moveX,moveZ,turn;
 
  SimulateAnimations(turn,moveX,moveZ,deltaT,prec);

#define MARK_FREE_FALL2 0
#if MARK_FREE_FALL2
  AddForce(Position(), Vector3(0,5,0), Color(0,0,0));
#endif

  // actual movement
  ApplyImpulses(); 
  moveTrans.SetTransform(ApplySpeed(deltaT));
  ApplyRemoteStateAdjustSpeed(deltaT,moveTrans);
  if (_walkingOnObject)
  {
     Vector3 diffPos = moveTrans.Position() - FutureVisualState().Position();

     // If man is standing on object move it a bit down to reach the collision.
    if (diffPos[1] < 0.002 && diffPos[1] > -0.011 )
      diffPos[1] = -0.011f;   

    // do move
    Vector3 xzSpeed;
    
    FutureVisualState().DirectionModelToWorld(xzSpeed,Vector3(moveX,0,moveZ));

    for( int j = 0; j < 3; j += 2)
    {
      if (xzSpeed[j] > 0)
      {
        if (diffPos[j] < xzSpeed[j])
        {
          diffPos[j] += xzSpeed[j];
          saturateMin(diffPos[j], xzSpeed[j]);
        }
      }
      else
      {
        if (diffPos[j] > xzSpeed[j])
        {
          diffPos[j] += xzSpeed[j];
          saturateMax(diffPos[j], xzSpeed[j]);
        }
      }
    }

    moveTrans.SetPosition(FutureVisualState().Position() + diffPos);
  }
  else if (QIsManual() && !_swimming && (!Brain() || !Brain()->GetVehicleIn()))
  {
    // Free-fall normally preserves momentum but discards locomotion root steps.
    // Keep limited horizontal control for the local player so a missing ground
    // contact cannot permanently lock movement and yaw after the fall timeout.
    Vector3 airStep;
    FutureVisualState().DirectionModelToWorld(airStep, Vector3(moveX, 0, moveZ));
    airStep[1] = 0;
    airStep *= 0.25f;
    moveTrans.SetPosition(moveTrans.Position() + airStep);
  }

  // Friction with air.
  Vector3Val speed=FutureVisualState().ModelSpeed();
  friction[0]=(speed[0]*fabs(speed[0])*0.002f+speed[0]*0.01f)*GetMass();
  friction[1]=(speed[1]*fabs(speed[1])*0.002f+speed[1]*0.01f)*GetMass();
  friction[2]=(speed[2]*fabs(speed[2])*0.002f+speed[2]*0.01f)*GetMass();

  //Torque friction.
  saturate(_angMomentum[0],-10,+10);
  saturate(_angMomentum[1],-10,+10);
  saturate(_angMomentum[2],-10,+10);

  if( _landContact )
  {
    //Friction with ground
    torqueFriction=_angMomentum * 1;
    friction[0]+=(fSign(speed[0])*10+speed[0]*0.5f)*GetMass() / 2;
    friction[1]+=(fSign(speed[1])*10+speed[1]*0.5f)*GetMass() / 2;
    friction[2]+=(fSign(speed[2])*10+speed[2]*0.5f)*GetMass() / 2;
  }
  else
  {
    if (_waterDepth > 0.5f)
    {
      torqueFriction=_angMomentum*0.6;
      friction[0]+=(fSign(speed[0])*10+speed[0]*0.3f)*GetMass();
      friction[1]+=(fSign(speed[1])*10+speed[1]*0.3f)*GetMass();
      friction[2]+=(fSign(speed[2])*10+speed[2]*0.3f)*GetMass();            
    }
    else
    {            
      torqueFriction=_angMomentum*0.3f;

      /* float surfaceY=GLOB_LAND->RoadSurfaceY(Position()+VUp*0.5f);
      if( Position().Y()-surfaceY>5 )
      {
      pCenter=Vector3(-0.1f,0.4f,0.2f);
      torque+=10 * pCenter.CrossProduct(friction);
      }
      */
    }
  }

  friction=FutureVisualState().DirectionModelToWorld(friction);

#if ARROWS
  Vector3 wCenter;
  FutureVisualState().PositionModelToWorld(wCenter,GetCenterOfMass());
  AddForce(wCenter,friction*InvMass());
#endif
  bool walkingOnObjectOld = _walkingOnObject;
  _walkingOnObject = false;
  _objectContact = false;
  _objectContactNonStatic = false;

  // is simulation step too long
  if (deltaT>0 && prec<=SimulateVisibleNear)
  {
    // check collision on new position        
    Vector3 oldPos = AimingPosition(FutureVisualState());
    Vector3 relAim = PositionWorldToModel(oldPos);
    Vector3 newPos = moveTrans.FastTransform(relAim);
    // check collision on line oldPos / newPos
    //GScene->DrawCollisionStar(oldPos,0.1,PackedColor(Color(0,1,0)));
    //GScene->DrawCollisionStar(newPos,0.1,PackedColor(Color(1,1,0)));

    CollisionBuffer collision;
    // check if we have some substantial distance and we are not ignoring collisions after get out
    if (newPos.Distance2(oldPos)>Square(0.1f) && !_ignoreCollisionIfStuck )
    {
      // check if there is some collision in the line of our movement
      GLandscape->ObjectCollisionLine(
        GetFutureVisualStateAge(),collision,Landscape::FilterIgnoreOne(this),oldPos,newPos,0/*0.5f*/,ObjIntersectGeom
        );
    }
    if (collision.Size()>0)
    {
      // check at what distance will be the first collision
      float minT = 1e10;
      for (int i=0; i<collision.Size(); i++)
      {
        const CollisionInfo &info = collision[i];
        Object *obj = info.object;
        if (!obj) continue;
        if (obj->IsPassable()) continue;
        _objectContact = true;
        _objectContactNonStatic = _objectContactNonStatic || (!obj->Static());

        saturateMin(minT,info.under);
      }
      if (minT<1)
      {
        // t is from 0 to 1, 0 is beg, 1 is end
        // alow some minimal movement (to avoid being stuck in)
        saturateMax(minT,0.01f);
        //LogF("Col %g",minT);
        Vector3 col=FutureVisualState().Position()*(1-minT)+moveTrans.Position()*minT;

        repairMove += col - moveTrans.Position();
        moveTrans.SetPosition(col);
      }
    }
  }

  // Synchronize DirectionUp with animation DirectionUp   
#define MAX_DIRECTIONUP_SYNC_HEIGHT 10.0f
  float dx,dz;
  float fHeight = FutureVisualState().Position()[1] - GLOB_LAND->RoadSurfaceY(FutureVisualState().Position()+VUp*0.5f, &dx,&dz);


  if (fHeight < MAX_DIRECTIONUP_SYNC_HEIGHT)
  {

    // check current up 
    if (VUp.Distance2(FutureVisualState().DirectionUp())>1e-6)
    {
      if (FutureVisualState()._speed[1] < 0.0f) 
        _angMomentum = VZero;

      Matrix3 orient;
      // some orientation change - set orientation in matrix            
#define MAX_DIRECTIONUP_FULLSYNC_HEIGHT 2.0f
      if (fHeight < MAX_DIRECTIONUP_FULLSYNC_HEIGHT)
      {
        // Full synchronization]
        orient.SetUpAndDirection(VUp,FutureVisualState().Direction()); 
        _walkingOnObject = true;
      }
      else
      {
        // Partial sync
        Vector3 toUp=VUp-FutureVisualState().DirectionUp();
#define MAX_DIRECTIONUP_SYNC_SPEED 1.0f
        float maxD=MAX_DIRECTIONUP_SYNC_SPEED*deltaT;
        saturate(toUp[0],-maxD,+maxD);
        saturate(toUp[1],-maxD,+maxD);
        saturate(toUp[2],-maxD,+maxD);
        orient.SetUpAndDirection(FutureVisualState().DirectionUp()+toUp,FutureVisualState().Direction());        
      }
      moveTrans.SetOrientation(orient);
    }
    else
      _walkingOnObject = true;
  }

  if (walkingOnObjectOld) // if allowMove == false always allow turning
  {   
    if (fabs(turn) > 0.0001)
    {        
      Matrix3 rotate(MRotationY,turn);
      Matrix3 orient; 
      orient.SetUpAndDirection(moveTrans.DirectionUp(),rotate*moveTrans.Direction());

      moveTrans.SetOrientation(orient);
    }
  }

  // now movetrans is ready.     
  SetleInPositionFreeFall(moveTrans);

  return true;
}

const float SlideSlopeThreshold = 2.0f; //atan(sqrt(2))=cca=55�
inline float SlideSpeedWanted(float gradFwd, float gradAside, float forceStand)
{
  if (forceStand) return 0;
  float slope = gradFwd*gradFwd+gradAside*gradAside;
#if 0
  DIAG_MESSAGE(500, Format("slope = %.2f", atan(sqrt(slope))*180.0/PI));
#endif
  if (slope > SlideSlopeThreshold)
    return slope - SlideSlopeThreshold;
  else
    return slope - 2*SlideSlopeThreshold; //brake is more efficient
}
const float StandSlopeThreshold = 0.6f; //atan(sqrt(2))=cca=38�
inline bool ShouldStand(float gradFwd, float gradAside, bool forceStand)
{
  //if (forceStand) return true; //NO! unable to prone on the road after such setting!
  float slope = gradFwd*gradFwd+gradAside*gradAside;
  if (slope > StandSlopeThreshold) 
  {
    return true;
  }
  return false;
}
/** Function creates new state. The first it backups values, needed later for initial state restoration, 
  then it advances animation and calculates new moveTrans.
  @param moveTrans - (out) new man position and orientation.
  @param turn - (out) new rotation.
  @param deltaT - (in) simulation time step.
  @param prec - (in) simulation precision.
  @return true if anything changed.
  */
bool Man::CreatePotentialState(ManVisualState & moveTrans, float& turn, float deltaT, SimulationImportance prec)
{
  ADD_COUNTER(mancre,1);
  // BackUp current animation state
  _primaryMoveBackUp.Done();
  _secondaryMoveBackUp.Done();
  _primaryMoveBackUp = _moves.GetPrimaryMove(FutureVisualState()._moves);
  _primaryTimeBackUp = _moves.GetPrimaryTime(FutureVisualState()._moves);
  _primaryTimeDBackUp = _moves.GetPrimaryTimeDelta(FutureVisualState()._moves);
  _secondaryMoveBackUp = _moves.GetSecondaryMove(FutureVisualState()._moves);
  _secondaryTimeBackUp = _moves.GetSecondaryTime(FutureVisualState()._moves);
  _secondaryTimeDBackUp = _moves.GetSecondaryTimeDelta(FutureVisualState()._moves);
  _primaryFactorBackUp = _moves.GetPrimaryFactor(FutureVisualState()._moves);
  _finishedBackUp = _moves.GetFinished(FutureVisualState()._moves);

  _legTransBackUp = FutureVisualState()._legTrans;
  _hideBodyBackUp = _hideBody;
  _waterDepthBackUp = _waterDepth;  
  _landContactBackUp = _landContact;
  _waterContactBackUp = _waterContact;
  _surfaceSoundBackUp =  _surfaceSound;
  _speedBackUp = FutureVisualState()._speed;
  _angMomentumBackUp = _angMomentum;
  _staticCollisionBackUp = _staticCollision;
  _useSafeCapsuleBackUp = _useSafeCapsule;

  // Create new animation state
  float moveX,moveZ;
  
  bool change = SimulateAnimations(turn,moveX,moveZ,deltaT,prec);    

  
  if (_objectContact) 
    change = true;


  if (
    turn != 0 || moveX != 0 || moveZ != 0 ||
    _moves.GetPrimaryMove(FutureVisualState()._moves).id != _primaryMoveBackUp.id 
    /*|| _gunYRot != _gunYRotWanted || _gunXRot != _gunXRotWanted*/
  ) 
  {
    change = true;
  }

  FutureVisualState()._speed = VZero;
  _angMomentum=VZero;  

  moveTrans = FutureVisualState();

  if( _isDead && _whenKilled==Time(0) )
  {
    _whenKilled=Glob.time;
    OnMoved();
    _lastMovementTime=Glob.time;
    _staticCollision = true;
    _staticCollisionUntil = Glob.time + 5; // AI can have disabled static collisions.
  }

  if (QIsManual())
  {
    //if (IsDead()) LandSlope(_forceStand, _landGradFwd, _landGradAside);
    // The player's animation already supplies the requested displacement.
    // Adding slope slide on top of a running step makes ground contact and the
    // attached camera bounce as the terrain correction changes each frame.
    // Keep automatic sliding only when there is no locomotion input.
    bool playerMoving = fabs(_walkSpeedWantedX) > 0.1f || fabs(_walkSpeedWantedZ) > 0.1f;
    if (playerMoving)
      _slideSpeed = 0;
    float slideSpeedWanted = playerMoving ? 0 : SlideSpeedWanted(_landGradFwd, _landGradAside, _forceStand);
    if (slideSpeedWanted>0 || _slideSpeed>0) //slide or brake slide
    {
      const float timeToReachTheSpeed = 2.0f; //from zero speed 2 seconds needed to reach the speed
      float acceleration = slideSpeedWanted / timeToReachTheSpeed;
      float speed = _slideSpeed + acceleration*deltaT;
      saturateMax(speed,0);
      _slideSpeed = speed;
      if (_slideSpeed!=0)
      {
#if 0
        DIAG_MESSAGE(100,Format("slideSpeed = %.1f",_slideSpeed))
#endif
        moveX -= _landGradAside*_slideSpeed*deltaT;
        moveZ -= _landGradFwd*_slideSpeed*deltaT;
        change = true;
        // do some dammage when sliding speed is fast
        float slideDamageThreshold = 8.0f;
        float actSpeed = FutureVisualState()._modelSpeed.Size();
        if (GetNetworkManager().IsServerOwner() && actSpeed>slideDamageThreshold)
        {
          LocalDamageMyself(VZero,0.05f*deltaT*actSpeed,1.0f);
        }
      }
    }
  }

  if( IsDead() )
  {                           
    if (
      _whenKilled<Glob.time-5 && _lastMovementTime<Glob.time-5 &&
      _landContact && _hideBody>=_hideBodyWanted
      )
    {
      // simulation suspended - dead body is in steady position
      _landContact=true;
      if (_hideBody >= 1 && !_brain) 
      {
        // destroy also all items
        DeleteItems([](const InventoryItem *item) -> bool
        {
          return true;
        });

        SetDelete();
      }
      return false;
    }
    else
    {               
      float delta = _hideBodyWanted-_hideBody;
      Limit(delta,-0.1f*deltaT,+0.1f*deltaT);

      _hideBody += delta;
/*
      // WORK IN PROGRESS: sliding of dead bodies 
      if (change)
      {
        // correct speedX
        Vector3 xzSpeed;
        //float invDeltaT=1/deltaT;
        DirectionModelToWorld(xzSpeed,Vector3(moveX,0,moveZ));
        xzSpeed[1]=0;

        FutureVisualState()._speed = xzSpeed / deltaT;      

        Vector3 position=Position();
        position += xzSpeed;
        moveTrans.SetPosition(position);        
      }
      else 
*/
      if (delta != 0)
      {
        change = true;
      }                        
    }
  }              
  else
  {
    if (change)
    {

      // correct speedX
      Vector3 xzSpeed;
      //float invDeltaT=1/deltaT;
      FutureVisualState().DirectionModelToWorld(xzSpeed,Vector3(moveX,0,moveZ));
      xzSpeed[1]=0;

      FutureVisualState()._speed = deltaT>0 ? xzSpeed / deltaT : VZero;

      Vector3 position=FutureVisualState().Position();
      position += xzSpeed;
      moveTrans.SetPosition(position);        
    }
  } 

  return change;
}
/** Function recursively searches for a subset of directions. In subset must be one dir from every set. Any two dirs from
  subset must have positive dot product. And the result repairMove build from directions must be the shortest.
  @param collisions - (in) array with collision points
  @param repairMoveSets - (in) index of the sets last points.
  @param actRepairMove - (in) actual repair move (build from directions in actualSubSet)
  @param iSet - (in) set in which function must search for new dir. 
  @param actualSubSet - (in/out) directions chosen till now.
  @param minRepairMove - (in/out) the smallest repair move found till now.
  @param minRepairMoveSize - (in/out) size of the smallest repair move found till now.
  @see #ValidatePotentialState( const Frame& , bool, float, Vector3&, bool, ObjectArray& )  
 */

void Man::FindConvexSubSet(const CollisionBuffer& collisions, const MoveSets& repairMoveSets, 
                           Vector3Par actRepairMove, const int iSet, Subset& actualSubSet, 
                           Vector3& minRepairMove, float& minRepairMoveSize)
{
  for(int iPosInSet = repairMoveSets[iSet - 1]; iPosInSet <  repairMoveSets[iSet]; iPosInSet++)
  {
    const CollisionInfo& info = collisions[iPosInSet];

    if (info.under == 0)
      continue;

    // check if current info is convex with already chosen.
    int i = 0;
    for(; i < iSet - 1; i++) 
    {
      if (info.dirOut * actualSubSet[i] < -0.01)
      {
        break;
      }
    }

    if (i == iSet - 1)
    {
      // convex
      float under = info.under - info.dirOut * actRepairMove;
      Vector3 cNewActRepairMove = actRepairMove;
      if (under > 0)
      {
        cNewActRepairMove += info.dirOut * under;                
      }

      if (cNewActRepairMove.Size() < minRepairMoveSize)
      {
        // still interesting
        if (iSet == repairMoveSets.Size() - 1)
        {
          //end
          minRepairMove = cNewActRepairMove;
          minRepairMoveSize = cNewActRepairMove.Size();
        }
        else
        {
          // continue with next set
          actualSubSet[iSet - 1] = info.dirOut;

          FindConvexSubSet( collisions,  repairMoveSets, 
            cNewActRepairMove, iSet + 1, actualSubSet, 
            minRepairMove, minRepairMoveSize);
        }
      }
    }
  }
}

#define MAX_REPAIR_SPEED 15.0f
#define MAX_REPAIR_STEP(repairMove, speed, deltaT) \
           (((speed).SquareSize() > 100.0f) ? \
           (MAX_REPAIR_SPEED - 10 * (repairMove).CosAngle(speed)) * (deltaT) \
           : (MAX_REPAIR_SPEED - (repairMove).CosAngle(speed)) * (deltaT))

typedef struct 
{
    int iComp;
    Ref<Object> obj;
} CollComponentID;

TypeIsMovableZeroed(CollComponentID);

/** Function detects collisions with objects. Futher, if requested, it searches for "repair move". 
 Repair move is difference to transformation. In ideal case repaired transformation will be collision free.
 @param moveTrans - investigated man position and orientation. 
 @param repairMove - result repair move
 @param reportContact - if true function calls OnNoCollide, OnCollide. 
 @param objArray - array of colliding objects
 @return collision volume
 */
DEF_RSB(treehard);
DEF_RSB(treesoft);
DEF_RSB(bushhard);
DEF_RSB(bushsoft);

float Man::ValidatePotentialState(const ManVisualState& moveTrans,  
                                  bool setUpContactInfo /*= false*/,                                
                                  Vector3 * repairMove /*=NULL*/,
                                  bool reportContact /*= false*/,                                  
                                  ObjectArray * objArray /*= NULL*/)                                  
{

  //PROFILE_SCOPE(manval);

#if _ENABLE_CHEATS
  if (CHECK_DIAG(DEManCollision) && _landContact)
  {
    Vector3 cLandNormal(-_landDX,1,-_landDZ);
    cLandNormal.Normalize(); 

    AddForce(_landContactPos, cLandNormal * 2, Color(0,0,0));
    AddForce(moveTrans.Position(), cLandNormal * 2, Color(1,1,1));
    if (_freeFall)
      AddForce(moveTrans.Position(), cLandNormal * 10, Color(1,1,1));
  }
#endif

#define SHOW_STATIC_COL 0
#if SHOW_STATIC_COL
  if (_staticCollision)
  {
    AddForce(moveTrans.Position(), Vector3(0,2,0), Color(0,0,1));
  }
#endif

  if (setUpContactInfo)
  {
    _purePersonContact = false;
    _lowPriorityContact = false;    
  }
  _highPriorityContact = false;

  // do it just for local ones and with geometry. 
  if ((!IsLocal() && !IsRemotePlayer()) || !HasGeometry())
    return 0;

  //ADD_COUNTER(manval2,1);

  //if (prec<=SimulateVisibleFar /*&& !steadyBody*/)

  CollisionBuffer collision;
  // AI soldiers have to choose collision free path.  But sometimes (f.e. in freefall mode etc) they can collide with static objects.

  bool onlyVehicles = !_freeFall && !_staticCollision;

  #if 0 // _DEBUG || _PROFILE
    onlyVehicles = true;
  #endif
  
  #if 1
  {
    PROFILE_SCOPE(manCl);
    GLandscape->ObjectCollision( collision, this, moveTrans, onlyVehicles, true);
  }
  #endif

  if (collision.Size() == 0)
  {
    return 0;
  }

  // check collision with vegetation
  for (int i = 0; i < collision.Size(); i++)
  {
    Object *obj = collision[i].object;
    if (obj)
    {
      const LODShape *lod = obj->GetShape();
      if (lod->GetPropertyClass() == RSB(bushhard) || lod->GetPropertyClass() == RSB(bushsoft) || lod->GetPropertyClass() == RSB(treesoft))
      {
        _vegCollisionTime = Glob.time;
        break;
      }
    }
  }

  Vector3 cLandNormal(-_landDX,1,-_landDZ);
  cLandNormal.Normalize();

  //const Mapping& bottomComp = Type()->GetBottomGeomComp();

  // The following code is searching for collisions that will be ignored. 
  // Ignored will be collisions with objects, where is reachable roadway.
  // It allows as to reach these roadways with landcontact and continue on them.

  // Ignored collisions are marked as forbidden. Forbidden is combination of two convex components
  // where their intersection is lying between landcontact and roadway. Roadway must maximally MAX_STEP_UP over landcontact.

  bool bFirst = true;
  CollComponentID actComp;
  AutoArray<CollComponentID, MemAllocLocal<CollComponentID,32> > cForbiddenComponents;

  if (!_freeFall)
  {
    bool bRoadWay= false;
    bool bForbidden = true; 
    bool bSureForbidden = false;

    for( int i=0; i<collision.Size(); i++ )
    {
      // info.pos is relative to object
      CollisionInfo &info=collision[i];                 

      if (!info.object) continue;
      if (info.object->IsPassable()) continue;

      info.object->SimulationNeeded(GetVisualStateAge(FutureVisualState()));
      // In collision array are collision points ordered. The first come points from
      // collision between man and the first object, then from the man and the second object etc...
      // Further they are ordered according to convex components.

      if (bFirst)
      {
        actComp.obj = info.object;
        actComp.iComp = info.component;                       
        bFirst = false;

        if (info.object->GetShape()->FindRoadwayLevel()>=0 )  
        {
          bRoadWay = true;                        
        }              

        bSureForbidden = (_ignoreLowPriorituContact && (!info.object->Static() || info.hierLevel > 0));          
      }
      else
      {
        if (actComp.obj != info.object || actComp.iComp != info.component)
        {
          // new component
          if ((bRoadWay && bForbidden) || bSureForbidden)                   
            cForbiddenComponents.Add(actComp);

          bRoadWay = false;
          bForbidden = true; 
          bSureForbidden = (_ignoreLowPriorituContact && (!info.object->Static() || info.hierLevel > 0));     

          actComp.obj = info.object;
          actComp.iComp = info.component;  

          if (info.object->GetShape()->FindRoadwayLevel()>=0 ) 
          {
            bRoadWay = true;
          }
        }
      }


      Vector3 worldPos = info.pos;      

      // Check iF collision point is under landcontact and land contact is maximally MAX_STEP_UP, over landcontact.
      // Here is a bit of simplification, we do not have all collision points in array collision.  
      if (bRoadWay && bForbidden && !bSureForbidden)
      {
        const Shape *roadway = info.object->GetShape()->RoadwayLevel();
        AnimationContextStorageGeom storage;
        AnimationContext animContext(info.object->FutureVisualState(),storage);
        roadway->InitAnimationContext(animContext, NULL, true); // no convex components for roadway level
        info.object->Animate(animContext, info.object->GetShape()->FindRoadwayLevel(), false, info.object, -FLT_MAX);
        animContext.RecalculateNormalsAsNeeded(roadway);

        // get point in local coord
        Vector3 modelPos;
        if (info.hierLevel == 0) 
        {
          Entity * ent = dyn_cast<Entity,Object>(info.object);
          if (ent)
            modelPos = ent->PositionWorldToModel(worldPos);
          else
            modelPos = info.object->FutureVisualState().GetInvTransform() * worldPos;
        }
        else
        {
          Entity * ent = dyn_cast<Entity,Object>(info.parentObject);
          if (ent)
            modelPos = ent->PositionWorldToModel(worldPos);
          else
            modelPos = info.parentObject->FutureVisualState().GetInvTransform() * worldPos;
        }

        static float minUnder = -0.30f; // numerical barrier
        float under = GLandscape->UnderRoadSurface(info.object, animContext, modelPos, modelPos, 0, Landscape::ObjDebuggingContext());
        if (under > minUnder ) 
        {          
          bForbidden = (
            _landContact ? (worldPos + Vector3(0,under,0) - _landContactPos).DotProduct(cLandNormal) < MAX_STEP_UP  * cLandNormal[1]:
            under < MAX_STEP_UP
          );
        }
        else 
          bForbidden = false;

        info.object->Deanimate(info.object->GetShape()->FindRoadwayLevel(), false);
      }                           

      info.pos = worldPos;                                  
    }

    if ((bRoadWay && bForbidden) || bSureForbidden)                  
      cForbiddenComponents.Add(actComp);
  }

  // Now collision points will be moved into wipedCollision array, but forbidden points will be omited.  
  CollisionBuffer wipedCollision;

  // Find repair move sets.
  // repairMoveSets array will contain 1-based indexes into array wipedCollision.
  // It will be indexes of the last points from a collision between two convex components.
  MoveSets repairMoveSets;
  repairMoveSets.Add(0); // Terminator   

  bFirst = true;
  if (setUpContactInfo)
    _purePersonContact = true;

  for( int i=0; i<collision.Size(); i++ )
  {
    // info.pos is relative to object
    CollisionInfo &info=collision[i];

    if (!info.object) continue;
    if (info.object->IsPassable()) continue;

    // If object contact is under land contact, iqnore it. It must be solved by land contact
    int j = 0;
    for(; j < cForbiddenComponents.Size(); j++)
    {
      if (info.component == cForbiddenComponents[j].iComp && info.object == cForbiddenComponents[j].obj)
      {
        break;
      }
    }

    if (j != cForbiddenComponents.Size())
    {
      if (setUpContactInfo)
        _objectContactNonStatic = _objectContactNonStatic || (!info.object->Static());

      continue;
    }         


    if (bFirst)
    {
      actComp.obj = info.object;
      actComp.iComp = info.component;  
      if (objArray)
        objArray->Add(info.object);

      if (setUpContactInfo)
      {       
        _purePersonContact = _purePersonContact && (dyn_cast<Person,Object>(info.object) != NULL);
        _objectContactNonStatic = _objectContactNonStatic || (!info.object->Static());

        if (!info.object->Static() || info.hierLevel > 0)
          _lowPriorityContact = true;
        //else
        //  _highPriorityContact = true;
      }

      if (info.object->Static() && info.hierLevel == 0)
        _highPriorityContact = true;


      bFirst = false;
    }
    else
    {
      if (actComp.obj != info.object || actComp.iComp != info.component)
      {
        actComp.obj = info.object;
        actComp.iComp = info.component; 
        if (objArray)
          objArray->Add(info.object);

        if (dyn_cast<Person,Object>(info.object) == NULL)
          _purePersonContact = false;

        if (setUpContactInfo)
        {       
          _purePersonContact = _purePersonContact && (dyn_cast<Person,Object>(info.object) != NULL);
          _objectContactNonStatic = _objectContactNonStatic || (!info.object->Static());

          if (!info.object->Static() || info.hierLevel > 0)
            _lowPriorityContact = true;
          //else
          //  _highPriorityContact = true;

          if (info.object->Static() && info.hierLevel == 0)
            _highPriorityContact = true;
        }

        repairMoveSets.Add(wipedCollision.Size());
      }
    }           

    wipedCollision.Add(info);
#if _ENABLE_CHEATS
    if(CHECK_DIAG(DEManCollision))
      AddForce(info.pos, info.dirOut * info.under * 10, Color(0,0,1));
#endif
  }

  if (bFirst)
  {
    // there are not a valid collision in the result;
    if (reportContact)
      OnNoCollide();

    return 0;
  }

  _objectContact = true;

  repairMoveSets.Add(wipedCollision.Size()); // Terminator      

  // Now find the shortes penetration depth in each repairMoveSets. The collision volume is the largest value from those values.
  float maxCollVol = 0;
  int maxCollVolSet = 1; 

  for( int iSet = 1; iSet < repairMoveSets.Size(); iSet++ )
  {
    float minCollVol = 1000000.0f; // Maximum        

    for(int i = repairMoveSets[iSet - 1]; i < repairMoveSets[iSet]; i ++)
    {

      // info.pos is relative to object
      const CollisionInfo &info=wipedCollision[i];

      if(minCollVol > info.under)
      {        
        minCollVol = info.under;                 
      }            
    }

    if (minCollVol > maxCollVol)
    {
      maxCollVolSet = iSet;
      maxCollVol = minCollVol;
    }        
  }

  if (reportContact)  
  {
    OnCollide(wipedCollision,repairMoveSets, maxCollVolSet);
  }


  if (!repairMove)
  {
    return maxCollVol; // State value
  }    

  bool foundRepair = false;

  // Now, repairMove must be from the plane perpendicular to LandNomrmal. Recalculate all direction to this plane. 
  if (_landContact)
  {                     
    for(int iSet = 1; iSet < repairMoveSets.Size(); iSet++)
    {
      bool bValid = false;
      for(int i = repairMoveSets[iSet - 1]; i < repairMoveSets[iSet]; i ++)
      {
        CollisionInfo& info = wipedCollision[i];

        if ( cLandNormal * info.dirOut != 0) 
        {                
          Vector3 cOld = info.dirOut;

          info.dirOut -= cLandNormal * (cLandNormal * info.dirOut);
          float fSize = info.dirOut.Size();                                                          

          if (fSize <= 0.1f) // numerical zero
          {
            // this move cannot be used
            info.dirOut = VZero;
            info.under = 0;
          }
          else
          {
            info.dirOut /= fSize;
            info.under /= fSize;
            bValid = true;
          }
        }
        else
          bValid = true;
      }

      if (!bValid)
      {
        // we cannot solve this set
        *repairMove = VZero;
        return maxCollVol;
      }                  
    }
  }

  *repairMove = VZero;  

  // Find smallest convex path. Find set of directions (one from each repairMoveSet) that have positive dot product and 
  // forms the smallest repair move.
  if (!foundRepair)
  {
    // if there is too many repairMoveSets algorithm is very ineffective
    if (repairMoveSets.Size() < 8)
    {    
      Subset actualSubSet;

      //Init array       
      for(int i = 0; i < repairMoveSets.Size(); i++)
        actualSubSet.Add(VZero);

      float minRepairMoveSize = 100000.0f;    

      FindConvexSubSet( wipedCollision, repairMoveSets, 
        VZero, 1, actualSubSet, 
        *repairMove, minRepairMoveSize);

      foundRepair = (*repairMove != VZero);      
    }   
  }

  // If convex path does not exist, take the smallest from each set. There is no garanty, that result repairMove, 
  // will be right correction.
  if (!foundRepair)
  {
    MoveSets cMinMoves;

    // Find Min Moves in each set
    for( int iSet = 1; iSet < repairMoveSets.Size(); iSet++ )
    {
      float minCollVol = 1000000.0f; // Maximum  
      int iMinIndex = 0;

      for(int i = repairMoveSets[iSet - 1]; i < repairMoveSets[iSet]; i ++)
      {                
        // info.pos is relative to object
        const CollisionInfo &info=wipedCollision[i];

        if (info.under != 0)
        {                    
          if(minCollVol > info.under)
          {        
            minCollVol = info.under;  
            iMinIndex = i;
          }            
        }
      }

      cMinMoves.Add(iMinIndex);
    }

    for(int i = 0; i < cMinMoves.Size(); i++)
    {        
      const CollisionInfo &info = wipedCollision[cMinMoves[i]];
      float under = info.under - (*repairMove) * info.dirOut;

      if (under > 0)
      {                        
        if (*repairMove * info.dirOut < 0)
        {
          // not complex part.
          // take perpendicular dir to dirOut
          Vector3 repairMoveNorm = repairMove->Normalized();
          Vector3 dir = info.dirOut - repairMoveNorm * (repairMoveNorm * info.dirOut);

          float fDot = dir * info.dirOut;
          if (fDot < 0.1)
          {
            // numerical zero (we can't find any dir)
            *repairMove = VZero;
            return maxCollVol;
          }

          *repairMove += dir * (under / (dir * info.dirOut));                                  
        }
        else
        {
          // complex part
          *repairMove += info.dirOut * under;                    
        }
      }
    }
    foundRepair = true;                        
  }

  //Enlarge repairMove a bit.
  if (repairMove->SquareSize() != 0)            
    *repairMove += repairMove->Normalized() * 0.01;

  return maxCollVol;
}

/** Function rollbacks state to the initial values. It is uses values backuped in CreatePotentialState.
 */
void Man::UseOldState(/*float deltaT, SimulationImportance prec */)
{
  // Revert state.
  FutureVisualState()._legTrans = _legTransBackUp;
  _hideBody = _hideBodyBackUp;
  _waterDepth = _waterDepthBackUp;  
  _landContact = _landContactBackUp;  
  _waterContact = _waterContactBackUp;
  _surfaceSound =  _surfaceSoundBackUp;
  FutureVisualState()._speed = _speedBackUp;
  _angMomentum = _angMomentumBackUp;
  _useSafeCapsule = _useSafeCapsuleBackUp;

  FutureVisualState()._acceleration = VZero;    

  // If move have changed, something have changed...
  if (_primaryMoveBackUp.id != _moves.GetPrimaryMove(FutureVisualState()._moves).id || _primaryTimeBackUp != _moves.GetPrimaryTime(FutureVisualState()._moves) ||
    _primaryFactorBackUp != _moves.GetPrimaryFactor(FutureVisualState()._moves))
  {
    _collVolume = INFINITY_VOLUME; // it is optimalization in case of problems uncomment the following part
    /*Frame trans = GetFrameBase();
    SetleInPosition(trans, 0, 0, false);           

    const MoveInfo * info = Type()->GetMoveInfo(_primaryMove.id);
    if (info && info->GetFrozeRotation())
    {
    Matrix3 tmpRotation;
    tmpRotation.SetUpAndDirection(trans.DirectionUp(), Vector3(0,0,1));
    trans.SetOrientation(tmpRotation);
    }

    _collVolume = ValidatePotentialState(trans);         
    */
  }
}


/** Function applies dammage according to collision volume.
 @param fCollVol - collision volume
 */
void Man::DammageOnCollVol(float fCollVol)
{
	const float DamageTreshold = 2.0f; //was 0.15 but discrepancy between server/client makes this very fragile...

  if (!GetNetworkManager().IsServerOwner() || fCollVol < DamageTreshold)
    return;

  float dammage= (fCollVol - DamageTreshold) * 2;                   
  dammage *= GetInvArmor() * 3;   
  LocalDamageMyself(VZero,dammage,2.0f); 
}

/** Function detects collsion with objects. If any collision was detected and any repair move was suggested it tries 
repaired position. The result transformation is one with the lower collision volume.
@param moveTrans - investigated man position and orientation.
@param trun - requested turn (used in SetleInPosition for repaired transformation)
@param deltaT - simulation time step
@param voll - initial state collsion volume. Just for debbuging purposes. 
@param reportContact - if true functions will call OnCollide or OnNoCollide... 
@return collision volume of returned transformation
 */
float Man::CheckAndRepairPotencialState(ManVisualState &moveTrans, float turn, float deltaT, float voll /*= 0*/, bool reportContact /*= false*/)
{
  Vector3 repairMove(VZero);
  float collVoll = ValidatePotentialState( moveTrans, false,&repairMove, reportContact);

  _highPriorityContactEnd = _highPriorityContact;

  if (collVoll > 0 && repairMove != VZero)
  {       
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DEManCollision))
      AddForce(moveTrans.Position() + Vector3(0,2.2,0), repairMove * 10, Color(1,0,0));
#endif

    // Check if repair step is not to big
    float maxStep = MAX_REPAIR_STEP(repairMove, FutureVisualState()._speed, deltaT);
    if (maxStep < repairMove.Size())
    {                    
      repairMove.Normalize();
      repairMove *= maxStep;

#if _ENABLE_CHEATS
      if (CHECK_DIAG(DEManCollision))
      {
        GlobalShowMessage(50, "fVoll: %f, collVoll: %f, TO BIG REPAIR STEP", voll, collVoll);
      }
#endif
    }

    // Try step according to repairMove
    ManVisualState moveTransRepaired = moveTrans;

    Vector3 position=moveTransRepaired.Position();
    position+=repairMove;
    moveTransRepaired.SetPosition(position);    

    //Vector3 landContactPosBackUp2 = _landContactPos;                

    ApplyTurn(moveTransRepaired,turn);
    SetleInPosition(moveTransRepaired, turn, deltaT);
    
    float collVollRep = ValidatePotentialState( moveTransRepaired);
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DEManCollision))
    {
      GlobalShowMessage(50, "fVoll: %f, collVoll: %f, collVollRep: %f", voll, collVoll, collVollRep);
    }
#endif
    if (collVollRep <= collVoll)
    {
      moveTrans = moveTransRepaired;      
      collVoll = collVollRep;
      _highPriorityContactEnd = _highPriorityContact;
    }
    //else
    //{
    //    _landContactPos = landContactPosBackUp2;
    //}
  }
  else
  {
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DEManCollision) && collVoll > 0)
    {
      GlobalShowMessage(50, "fVoll: %f, collVoll: %f, NO REPAIR STEP FOUND", voll, collVoll);
    }
#endif
  }

  return collVoll;
}

/** Function detects collsion with objects. If any collision was detected and any repair move was suggested it tries 
repaired position. The result transformation is one with the lower collision volume. Futher function returns array of objects,
colliding with man (just for the unrepaired trans). 
@param moveTrans - investigated man position and orientation.
@param trun - requested turn (used in SetleInPosition for repaired transformation)
@param collObjects - array of objects colliding with man
@param deltaT - simulation time step
@param fVoll - initial state collsion volume. Just for debbuging purposes. 
@param reportContact - function calls OnNoCollide & OnCollide
@return collision volume of returned transformation
*/
float Man::CheckAndRepairPotencialStateFreeFall(ManVisualState &moveTrans, Vector3& repairMove, ObjectArray& collObjects, float deltaT, float fVoll /*= 0*/, bool reportContact /*= false*/)
{    
  float fCollVol = ValidatePotentialState( moveTrans, false, &repairMove, reportContact, &collObjects);

  if (fCollVol > 0 && repairMove != VZero)
  {        
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DEManCollision))
      AddForce(moveTrans.Position() + Vector3(0,2.2,0), repairMove * 10, Color(1,0,0));
#endif
    
    // Because in freefall mode really fast vehicles can be meat, do not test repair step length.

  /*  float fMaxStep = MAX_REPAIR_STEP(repairMove, FutureVisualState()._speed, deltaT);
    if (fMaxStep < repairMove.Size())
    {

      repairMove.Normalize();
      repairMove *= fMaxStep;
#if _ENABLE_CHEATS
      if (CHECK_DIAG(DEManCollision))
        GlobalShowMessage(50, "fVoll: %f, fCollVol: %f, TO BIG REPAIR STEP", fVoll, fCollVol);
#endif
    }
    */

    // Try step according to repairMove
    ManVisualState moveTransRepaired = moveTrans;

    Vector3 position=moveTransRepaired.Position();
    position+=repairMove;
    moveTransRepaired.SetPosition(position);

    SetleInPositionFreeFall(moveTransRepaired);

    float fCollVolRep = ValidatePotentialState( moveTransRepaired);
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DEManCollision))
      GlobalShowMessage(50, "fVoll: %f, fCollVol: %f, fCollVolRep: %f", fVoll, fCollVol, fCollVolRep);
#endif
    if (fCollVolRep <= fCollVol)
    {
      moveTrans = moveTransRepaired;
      fCollVol = fCollVolRep;
    }
  }
  else
  {
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DEManCollision) && (fCollVol > 0))        
      GlobalShowMessage(50, "fVoll: %f, fCollVol: %f, NO REPAIR STEP FOUND", fVoll, fCollVol);
#endif
  }

  return fCollVol;
}

/** Function measures a collision volume for initail state. The first it rollbacks to the initial state according 
 to backuped values. 
 @return collsion volume
*/

float Man::ValidatePotentialStateThis()
{
  // DoAssert(_moves.CheckValidState(GetMovesType()));
  // By CreatePotencialState values was changed, use original ones.
  // Do it just with values influencing ValidatePotentialState. 
  bool staticCollisionBackUp2 = _staticCollision;
  _staticCollision = _staticCollisionBackUp;
  MotionPathItem primaryMoveBackUp2 = _moves.GetPrimaryMove(FutureVisualState()._moves);
  float primaryTimeBackUp2 = _moves.GetPrimaryTime(FutureVisualState()._moves);
  float primaryTimeDBackUp2 = _moves.GetPrimaryTimeDelta(FutureVisualState()._moves);
  MotionPathItem secondaryMoveBackUp2 = _moves.GetSecondaryMove(FutureVisualState()._moves);
  float secondaryTimeBackUp2 = _moves.GetSecondaryTime(FutureVisualState()._moves);
  float secondaryTimeDBackUp2 = _moves.GetSecondaryTimeDelta(FutureVisualState()._moves);
  float primaryFactorBackUp2 = _moves.GetPrimaryFactor(FutureVisualState()._moves);
  bool finishedBackUp2 = _moves.GetFinished(FutureVisualState()._moves);
  _moves.SetState(
    FutureVisualState()._moves,
    _primaryMoveBackUp, _primaryTimeBackUp, _primaryTimeDBackUp,
    _secondaryMoveBackUp, _secondaryTimeBackUp, _secondaryTimeDBackUp,
    _primaryFactorBackUp, _finishedBackUp);
  bool useSafeCapsuleBackUp2 = _useSafeCapsule;
  _useSafeCapsule = _useSafeCapsuleBackUp;

  // DoAssert(_moves.CheckValidState(GetMovesType()));
  
  ManVisualState trans = FutureVisualState();
  // Recalculate _ledTrans, _landContactPos etc
  SetleInPosition(trans, 0, 0, false);  
  float fVoll = ValidatePotentialState(trans, true);
  
  _staticCollision = staticCollisionBackUp2;  
  _moves.SetState(
    FutureVisualState()._moves,
    primaryMoveBackUp2, primaryTimeBackUp2, primaryTimeDBackUp2,
    secondaryMoveBackUp2, secondaryTimeBackUp2, secondaryTimeDBackUp2,
    primaryFactorBackUp2, finishedBackUp2);
  _useSafeCapsule = useSafeCapsuleBackUp2;
  // DoAssert(_moves.CheckValidState(GetMovesType()));

  return fVoll;
}

void Man::Move(Matrix4Par transform)
{
  base::Move(transform);
  // any movement may require land-slope recalculation
}
void Man::Move(Vector3Par position)
{
  base::Move(position);
  // any movement may require land-slope recalculation
}

static void DestroyPPEHndls(AutoArray<int> &hndls)
{
  for (int i = 0; i < hndls.Size(); i++)
  {
    if (hndls[i] != -1) GEngine->DestroyPostprocess(hndls[i]);
  }

  hndls.Clear();
}

static void CreatePPEHndls(const AutoArray<PPEffectType> &ppeTypes, AutoArray<int> &hndls)
{
  int size = ppeTypes.Size();
  Assert(hndls.Size() == 0);

  hndls.Realloc(size);
  hndls.Resize(size);

  for (int i = 0; i < size; i++)
  {
    const PPEffectType &oet = ppeTypes[i];
    int tmpHndl = GEngine->CreatePostprocess(oet._effectType, oet._priority);
    // initial set
    hndls[i] = -1;

    if (tmpHndl != -1)
    {
      int retVal = GEngine->SetPostprocessParams(tmpHndl, const_cast< AutoArray<float> &> (oet._pars));

      if (retVal != 0)
      {
        GEngine->DestroyPostprocess(tmpHndl); 
      }
      else
      {
        GEngine->EnablePostprocess(tmpHndl, true);
        GEngine->CommitPostprocess(tmpHndl, 0);

        // handle successfully created than we can set pars
        hndls[i] = tmpHndl;
      }
    }
  }
}

static Vector3 ClipToPath(Vector3Par newPos, Vector3Par oldPos, Vector3Par nearestNewPos, Vector3Par nearestOldPos)
{
  // we can stick to the planned path only when it does not differ from our Y too much. If it does, it is a broken path
  if (fabs(nearestNewPos.Y()-newPos.Y())<0.5f && fabs(nearestOldPos.Y()-oldPos.Y())<0.5f)
  {
    // only prevent getting worse - if the situation already was bad, leave it bad
    // this is done to make sure units placed without AI (cutscenes, mission start) are not jumping
    float distNew2 = nearestNewPos.DistanceXZ2(newPos);
    float distOld2 = nearestOldPos.DistanceXZ2(oldPos);
    // move toward the nearestPos to stay close enough the path
    float maxDist = OperItemGrid*0.5f;
    if (distNew2>Square(maxDist) && distNew2>distOld2)
    {
      // keep elevation
      float aboveSurface = newPos.Y()-GLandscape->RoadSurfaceYAboveWater(newPos+VUp*0.5f);
      Vector3 limitedPos = (newPos - nearestNewPos)*InvSqrt(distNew2)*maxDist + nearestNewPos;
      limitedPos[1] = GLandscape->RoadSurfaceYAboveWater(limitedPos+VUp*0.5f)+aboveSurface;
      return limitedPos-newPos;
    }
  }
  return VZero;
}

void PathAroundObstacles(Vector3Par from, Vector3 to, Entity* pAgent);

/*!
  \patch 5117 Date 1/11/2007 by Ondra
  - Fixed: Player could sometimes get stuck after falling onto some object or vehicle.
*/
void Man::Simulate( float deltaT, SimulationImportance prec )
{  
  float deltaT0 = deltaT;
  // we may be forced to use different timer
  if (_useAudioTimeForMoves)
  {
    UITime now = GSoundScene->GetAudioTime();
    if (CHECK_DIAG(DEAnimation) && Brain()==GWorld->FocusOn())
    {
      DIAG_MESSAGE(2000,"%s: deltaT %.3f, audio %.3f",cc_cast(GetDebugName()),deltaT,now-_audioTimeProcessed);
    }
    deltaT = now-_audioTimeProcessed;
    _audioTimeProcessed = now;
    // note: KeyboardPilot (called from Man::SimulateAI) still uses deltaT.
    // When audio time is a lot different from game time, controls may feel very sluggish (e.g. with setAccTime 0.01)
  }

  ProcessItemToHands();

  // DoAssert(_moves.CheckValidState(GetMovesType()));
  // DoAssert(_gesture.CheckValidState(GetGesturesType()));
  if (_ladderBuilding && _ladderIndex>=0)
  {    // check keyboard pilot / AI pilot
    if (IsDamageDestroyed())
    {
      DropLadder(_ladderBuilding,_ladderIndex);
      //Move(Position()-Direction());
      //SetSpeed(Direction()*-1);
      //_landContact=false;
    }
    else
    {
      SimLadderMovement(deltaT,prec);
    }

    //ApplyRemoteState(deltaT);

    _collVolume = INFINITY_VOLUME;
    base::Simulate(deltaT,prec);
    SimulatePost(deltaT,prec);
    return;
  }

  // ANIMS-JUMP/VAULT: tratamento direto no jogador local (client-side).
  // ESPACO = pulo (usa a mesma animacao de pulo 1.30 que aparecia no V).
  // V      = passar muro/cerca (animacao de vault + impulso fisico).
  if (this==GWorld->GetRealPlayer() && !_swimming && !IsDead() && Brain() && !Brain()->GetVehicleIn())
  {
    RStringB curName = GetMovesType()->GetMoveName(_moves.GetPrimaryMove(FutureVisualState()._moves).id);
    const char *n = cc_cast(curName);
    const bool onGround = _landContact && !_freeFall;

    // ---------- ESPACO: pulo ----------
    if (GInput.GetKeyToDo(DIK_SPACE) && onGround)
    {
      FutureVisualState()._speed[1] = 4.6f;
      _landContact = false;
      _freeFallStartTime = TIME_MAX;
      FreeFall(1.2f);

      // Pulo: usa os estados "Mjmp" do config (apontam para os RTM
      // CdzpAmovPercMjmp*.rtm, a animacao de pulo de verdade). Se o config nao
      // tiver esses estados, cai para a variante Ovr equivalente.
      RStringB jumpMove = "AmovPercMjmpSnonWnonDf";
      if (MoveNameHas(n,"Wrfl"))
        jumpMove = "AmovPercMjmpSlowWrflDf";
      else if (MoveNameHas(n,"Wmel"))
        jumpMove = "AmovPercMjmpSlowWmelDf";
      bool played = PlayMoveNow(jumpMove);
      if (!played)
      {
        RStringB ovrMove = "AovrPercMrunSnonWnonDf";
        if (MoveNameHas(n,"Wrfl")) ovrMove = "AovrPercMrunSlowWrflDf";
        else if (MoveNameHas(n,"Wmel")) ovrMove = "AovrPercMrunSlowWmelDf";
        played = PlayMoveNow(ovrMove);
        jumpMove = ovrMove;
      }
      RptF("ANIMS-JUMP: espaco estado=%s mov=%d -> %s", n, played?1:0, cc_cast(jumpMove));
    }

    // ---------- V: passar muro ----------
    if (GInput.GetKeyDo(DIK_V) && onGround)
    {
      bool able = IsAbleToGetOver();
      RptF("ANIMS-VAULT: V pressionado estado=%s apto=%d", n, able?1:0);
      if (able)
      {
        RStringB vault = "AovrPercMstpSnonWnonDf";
        if (MoveNameHas(n,"Pknl")!=NULL)
        {
          if (MoveNameHas(n,"Wrfl")!=NULL) vault = "AovrPknlMstpSlowWrflDf";
          else if (MoveNameHas(n,"Wmel")!=NULL) vault = "AovrPknlMstpSlowWmelDf";
          else vault = "AovrPknlMstpSnonWnonDf";
        }
        else if (MoveNameHas(n,"Wrfl")!=NULL)
          vault = (MoveNameHas(n,"Sras")!=NULL) ? "AovrPercMstpSrasWrflDf" : "AovrPercMstpSlowWrflDf";
        else if (MoveNameHas(n,"Wmel")!=NULL)
          vault = (MoveNameHas(n,"Sras")!=NULL) ? "AovrPercMstpSrasWmelDf" : "AovrPercMstpSlowWmelDf";

        Vector3Val d = FutureVisualState().Direction();
        Vector3 fwd(d.X(),0,d.Z());
        if (fwd.SquareSize()>0.0001f) fwd.Normalize(); else fwd = VForward;
        FutureVisualState()._speed = fwd * 2.3f;
        FutureVisualState()._speed[1] = 6.0f;
        _landContact = false;
        _freeFallStartTime = TIME_MAX;
        FreeFall(1.4f);
        bool byAction = PlayActionNowGlobal("GetOver");
        if (!byAction) PlayMoveNow(vault);
        RptF("ANIMS-VAULT: estado=%s acao=%d", cc_cast(vault), byAction?1:0);
      }
    }
  }

  if (!IsControlled() && CheckPredictionFrozen())
  {
    float timeOffset = AI_MAX_SIM_TIME;
    SetTimeOffset(timeOffset);

    // Man is frozen. Just do the animation, but don't move him. 
    float turn,moveX,moveZ;
    SimulateAnimations(turn,moveX,moveZ,deltaT,prec);
    base::Simulate(deltaT,prec);    
    BasicSimulationCore(deltaT, prec);
    SimulatePost(deltaT, prec);
    _impulseForce = VZero;
    _impulseTorque = VZero;

    _collVolume = INFINITY_VOLUME;
    return;
  }
 
  // Apply Impulses 
  float impulse2=_impulseForce.SquareSize();
  if (impulse2 > 0.0001f)
    DamageOnImpulse(this, sqrt(impulse2), VZero);

  if (impulse2>Square(GetMass() * 5))
  {        
    _lastMovementTime=Glob.time;        
    FreeFall(1.0f);        

    if( impulse2>Square(GetMass()*60) )
    {
      _impulseForce=_impulseForce.Normalized()*(GetMass()*60);
    }
  }

  if (_primaryMoveBackUp.id != _moves.GetPrimaryMove(FutureVisualState()._moves).id || _primaryTimeBackUp != _moves.GetPrimaryTime(FutureVisualState()._moves) ||
    _primaryFactorBackUp != _moves.GetPrimaryFactor(FutureVisualState()._moves)) //I do not have control of the move so let disable dammage system.
  {
#if _DEBUG
    RStringB str1 = GetMovesType()->GetMoveName(_primaryMoveBackUp.id);
    RStringB str2 = GetMovesType()->GetMoveName(_moves.GetPrimaryMove(FutureVisualState()._moves).id);      
#endif

    _collVolume = INFINITY_VOLUME;
  }

#if _ENABLE_ATTACHED_OBJECTS
  if (IsAttached()) _landContact = true; // if man is attached, assume land contact
#endif

#if _VBS2
  AIBrain *brain = Brain();
  Assert(brain);
  bool inVehicle = (brain && brain->GetVehicleIn()); //Convoy Trainer

  if (inVehicle)   //if man is attached, assume land contact
    _landContact = true;
#endif
  // some objects contacts may prevent us from falling?
  // when in contact with an object and speed is stabilized, do not assume free fall any more
  // x/z speed is not as important as the y one
  if (!_landContact && !(_objectContact && FutureVisualState()._speed.Modulate(Vector3(0.05f,1,0.05f)).SquareSize()<Square(0.3f)))
  {
    if (!_useAudioTimeForMoves) // audio time is used for precise cutscene moves - no free fall here
    {
      FreeFall(0.3f);
    }
  }

  _freeFall = (Glob.time < _freeFallUntil);    

  float timeOffset = AI_MAX_SIM_TIME;
  if (QIsManual() && !CHECK_DIAG(DEVisualStates))
  {
#if 0
    //FIDOs collision/pathfinding tests. Do not remove ;)
    static Vector3 fromPos, toPos;

    const float radius = 0.35f;

    if(GetAsyncKeyState(VK_NUMPAD1) & 0x8000)
    {
      fromPos = FutureVisualState().Position() + Vector3(0,radius+0.1f, 0);
      //fromPos = Vector3(2547.48, 16.0835, 2846.88);
      //toPos = Vector3(2555.41, 16.1528, 2889.53);
    }
    
    /*if(GetAsyncKeyState(VK_NUMPAD2) & 0x8000)
    {
      toPos = FutureVisualState().Position() + Vector3(0,radius+0.1f, 0);
    }*/
    toPos = FutureVisualState().Position() + Vector3(0,radius+0.1f, 0);

    if(!fromPos.AllZeroes() && !toPos.AllZeroes())
    {
      CollisionBuffer col;
      
      GLandscape->ObjectCollisionLine(GetVisualStateAge(FutureVisualState()),col,Landscape::FilterIgnoreOne(this),fromPos, toPos,radius,ObjIntersectGeom, ObjIntersectNone, CF_NEAREST_CONTACT|CF_ONLY_STATIC);
      GScene->DrawDiagSphere(fromPos, radius, HWhite);
      GScene->DrawDiagSphere(toPos, radius, Color(0,1,0));
      if(col.Size())
      {
        /*
        float bestDist = FLT_MAX;
        int bestIndex = -1;
        for(int n = 0; n < col.Size(); n++)
        {
          float dist = col[n].pos.Distance2(fromPos);
          if(dist < bestDist)
          {
            bestDist = dist;
            bestIndex = n;
          }
        }
        */
        int bestIndex = 0;
        GScene->DrawDiagSphere(col[bestIndex].pos, radius, ColorP(1,0,0));

      }
      PathAroundObstacles(fromPos, toPos, this);
    }
 #endif
    SetSimulationPrecision(PLAYER_MAX_SIM_TIME,AI_MAX_SIM_TIME); /// player "AI" needs to be called in each frame
    timeOffset = 0;
  }
  else if (Glob.time < _staticCollisionUntil || _freeFall && !CHECK_DIAG(DEVisualStates))
  {
    SetSimulationPrecision(PLAYER_MAX_SIM_TIME,AI_MAX_SIM_TIME);
    timeOffset = AI_MAX_SIM_TIME_NEAR;
  }
  else
  {
    float step = prec<=SimulateVisibleNear ? AI_MAX_SIM_TIME_NEAR : AI_MAX_SIM_TIME;
    SetSimulationPrecision(step,AI_MAX_SIM_TIME);
    timeOffset = step;
  }

  SetTimeOffset(timeOffset);

  bool bChange = false;


  bool leanChanged = false;
  float delta;
  {
    // Calculate the leaning position
    float d = deltaT * 6.0f;
    delta = _leanZRotWanted - _leanZRot;
    Limit(delta, -d, d);
    float oldLeanZRot = _leanZRot;
    _leanZRot = _leanZRot + delta;
    // if there is leaning, we may need to limit leaning to avoid peeking through the wall
    // done only to prevent visual artifacts, no need to do for AI
    if (fabs(_leanZRot)>0.01f && QIsManual())
    {
      static float zOffset = 0.230f;
      static float xOffset = 0.200f;
      static float zOffset2 = 0.250f;

      // note: CheckMaxLean works directly with leaning transform and _leanZRot
      _leanZRot = CheckMaxLean(_leanZRot,xOffset, zOffset);
      _leanZRot = CheckMaxLean(_leanZRot,0, zOffset2);
      _leanZRot = CheckMaxLean(_leanZRot,-xOffset, zOffset);
      RecalcLeaningTransform();
    }
    leanChanged = oldLeanZRot!=_leanZRot;
    if (leanChanged)
    {
      RecalcLeaningTransform();
    }
  }

  ManVisualState moveTrans = FutureVisualState(); // store old position
  Vector3 prevPos = moveTrans.Position();
  if (!_remoteState.IsNull())
  {
    _lastRemotePos = _remoteState->pos;
  }

  // Freefall state must be handled separately
  if( _freeFall && !_useAudioTimeForMoves) // audio time is used for precise cutscene moves - no free fall here
  { 
    bChange = SimulateFreeFall(moveTrans, deltaT, prec);           
  }
  else
  {           
    _freeFallStartTime = TIME_MAX; //no freeFall now
    // not freefall
    //Assert(g_TestPos == VZero || g_TestPos == Position());      
    Vector3 repairMove;

    float fVoll = 0;      
    bool bCalculatedVoll = false;                                   
    bool bTakeAllCost = false;
    _ignoreLowPriorituContact = false;
    //Assert(g_fVoll == -1 || g_fVoll == fVoll);
    float fCollVol = 0;

                  
    float turn;
    bChange = CreatePotentialState(moveTrans, turn, deltaT, prec);

    if (bChange || _swimming)
    {
      ApplyTurn(moveTrans,turn);
    }

    ApplyRemoteStateAdjustSpeed(deltaT,moveTrans);

    //   if (_remoteState)
    //   {
    //     LogF(
    //       "localX,remoteX,localSpeedX,remoteSpeedX,localSpeedSize,remoteSpeedSize ; %.2f,%.2f,%.3f,%.3f,%.3f,%.3f",
    //       FutureVisualState().Position().X(),_remoteState->pos.X()+_remoteState->speed.X()*(Glob.time-_remoteState->time),
    //       FutureVisualState().Speed().X(),_remoteState->speed.X(),
    //       FutureVisualState().Speed().Size(),_remoteState->speed.Size()
    //     );
    //   }

    _objectContact = false; // cannot be done before ApplyRemoteState, it needs to react on collisions

    if (bChange || _swimming)            
    {
      // check current up and turn
      SetleInPosition(moveTrans,turn,deltaT);
    }


    bool ignoreCollisions = _useAudioTimeForMoves;

#if _ENABLE_ATTACHED_OBJECTS
    if (IsAttached()) ignoreCollisions = true;
#endif
#if _VBS2
    if (inVehicle) ignoreCollisions = true;
#endif

    if (ignoreCollisions) // ignore all collisions
    {
      // attached or invehicle units
      _collVolume = INFINITY_VOLUME;
    }
    else
    {
      // Change static collision status 
      if (_staticCollision)
      {
        if (!QIsManual() && !IsRemotePlayer() && _staticCollisionUntil < Glob.time)      
          _staticCollision = false;
      }
      else
      {
        if (QIsManual() || IsRemotePlayer())      
          _staticCollision = true;

        if (_objectContactNonStatic)

        {        
#define STATIC_COLL_TIME 5.0f   
          _staticCollision = true;
          _staticCollisionUntil = Glob.time + STATIC_COLL_TIME;
          SetSimulationPrecision(PLAYER_MAX_SIM_TIME);
          // SetTimeOffset(PLAYER_MAX_SIM_TIME);
        }
      }

      _objectContactNonStatic = false;

      const bool farCollision = false;
      if (prec<=SimulateVisibleFar) 
      {
        if (prec<=(farCollision ? SimulateVisibleFar : SimulateVisibleNear)) 
        {
          auto doCollision = [&moveTrans,this,turn, &fVoll,&fCollVol,&bTakeAllCost,&bChange,&bCalculatedVoll](float deltaT)->void 
          {
          ManVisualState moveTransOrig = moveTrans;
          if (!bChange)
          {
            fVoll = ValidatePotentialStateThis();          

            bCalculatedVoll = true;
            if (fVoll > 0)         
              bChange = true;                      
          }

          if (bChange)
          {
            bool switchedState = false;
            bool reportContact = true;
            do 
            {
              if (switchedState)
              {
                // neutralize situation to original state
                moveTrans = moveTransOrig;
                SetleInPosition(moveTrans, 0,0, false);
                bCalculatedVoll = false;
                fVoll = 0;
                switchedState = false;
                reportContact = false; // do not report contacts during second pass
              }

              const MoveInfoMan * info = Type()->GetMoveInfo(_moves.GetPrimaryMove(FutureVisualState()._moves).id, AnimSubIx());
              bool hasCapsuleSafe = (info != NULL ? info->HasCollCapsuleSafe() : false);
              if (!_safeSystem || !hasCapsuleSafe)
              {
                _useSafeCapsule = false;
              }            

#if _ENABLE_CHEATS
              if (CHECK_DIAG(DEManCollision))
              {
                fVoll = ValidatePotentialStateThis();
                bCalculatedVoll = true;                 
              }
#endif
              fCollVol = CheckAndRepairPotencialState(moveTrans, turn, deltaT, fVoll, reportContact);

              if (fCollVol > 0 && !bCalculatedVoll)    
              {
                fVoll = ValidatePotentialStateThis();              
              }

              if (fCollVol * ALLOWED_VOL_GROW > fVoll  && fabsf(turn) > 1e-6)
              { 
                if (_safeSystem && info != NULL && info->HasCollCapsuleSafe())
                {           
                  if (_useSafeCapsule)
                  {
                    // Try it without move
                    // Take it on all cost.
                    moveTrans = FutureVisualState();
                    ApplyTurn(moveTrans,turn);
                    SetleInPosition(moveTrans,turn, deltaT);

                    fCollVol = CheckAndRepairPotencialState(moveTrans, turn, deltaT, fVoll);  
                    _highPriorityContactEnd = _highPriorityContact;

                    bTakeAllCost = true;
                  }
                  else
                  {
                    // Try it without move
                    ManVisualState moveTransNew = FutureVisualState();
                    ApplyTurn(moveTransNew,turn);
                    SetleInPosition(moveTransNew,turn, deltaT);                 

                    float fNewCollVol = CheckAndRepairPotencialState(moveTransNew, turn, deltaT, fVoll);
                    if (fNewCollVol <= fVoll)
                    {
                      moveTrans = moveTransNew;                   
                      fCollVol = fNewCollVol;  
                      _highPriorityContactEnd = _highPriorityContact;
                    }                     
                  } //if (_useSafeCapsule)
                } //if (_safeSystem)
#define TRY_TURN_MOVE_SEPARATELY 1
#if TRY_TURN_MOVE_SEPARATELY
                else
                {
                  //Try it without rotation
                  ManVisualState moveTransNew = moveTransOrig;
                  SetleInPosition(moveTransNew,0, deltaT);

                  float fNewCollVol = CheckAndRepairPotencialState(moveTransNew, 0, deltaT, fVoll);
                  if (fNewCollVol <= fVoll)
                  {
                    moveTrans = moveTransNew;                  
                    fCollVol = fNewCollVol;  
                    _highPriorityContactEnd = _highPriorityContact;
                  }
                  else
                  {
                    // Try it without move
                    moveTransNew = FutureVisualState();
                    ApplyTurn(moveTransNew,turn);
                    SetleInPosition(moveTransNew,turn, deltaT);

                    fNewCollVol = CheckAndRepairPotencialState(moveTransNew, turn, deltaT, fVoll);
                    if (fNewCollVol <= fVoll)
                    {
                      moveTrans = moveTransNew;                  
                      fCollVol = fNewCollVol; 
                      _highPriorityContactEnd = _highPriorityContact;
                    }                                
                  }
                }
#endif           
              } //if (fCollVol * ALLOWED_VOL_GROW > fVoll && fabsf(turn) > 1e-6)

              if (hasCapsuleSafe && fVoll < fCollVol * ALLOWED_VOL_GROW && _safeSystem && !_useSafeCapsule &&  fabsf(turn) > 1e-6 )
              {
                switchedState = true; 
                _useSafeCapsule = true; 
                //_collVolume = INFINITY_VOLUME;
              }
              else
              {
                if (QIsManual() && _lowPriorityContact && _highPriorityContactEnd && !_ignoreLowPriorituContact && 
                  fCollVol > 0 && fVoll > 0 && !hasCapsuleSafe)
                {
                  switchedState = true; 
                  _ignoreLowPriorituContact = true;

                  if (fVoll * 0.999 /* num. barier */ > _collVolume && fCollVol > 0 &&
                    _disableDamageFromObjUntil < Glob.time && !_purePersonContact)
                  {
                    DammageOnCollVol(fCollVol);                           
                  }                   
                }
              }
            } while(switchedState);    

            if (!_ignoreLowPriorituContact && fVoll * 0.999 /* num. barier */ > _collVolume && fCollVol > 0 &&
              _disableDamageFromObjUntil < Glob.time && !_purePersonContact)
            {
              DammageOnCollVol(fCollVol);                           
            } 
          } //if (bChange) 
          else
            OnNoCollide();

          //if (GetNetworkManager().IsClient())
          //{
          //  //PURPLE - final repaired position
          //  enf::Shape::CreateSphere(moveTrans.Position() + 2.05f*VUp, 0.05f, 0xffff00cc, enf::SS_ONCE|enf::SS_NOOUTLINE);
          //}
          };

  static bool doSubdiv = true;
  static float subdiv = 0.2f;
          float moveDist = prevPos.Distance(moveTrans.Position());
          if(doSubdiv && moveDist > (subdiv + 0.01f))
          {
            const Time backupVCT = _vegCollisionTime;
            const bool backupHPC = _highPriorityContact;
            const bool backupPPC = _purePersonContact;
            const bool backupOC = _objectContact;

            ManVisualState collTest(moveTrans);
            int nextIter = 0;
            int numIter = intMax((int)ceilf(moveDist / subdiv), 2);
            Vector3 endPos = moveTrans.Position();
            for(int n = 0; n < numIter; n++)
            {
              collTest.SetPosition(prevPos.Lerp(endPos, (float)n / (numIter - 1)));

              const float collVol = ValidatePotentialState(collTest);
              
              // revert any side-effects on member variables
              _vegCollisionTime = backupVCT;
              _highPriorityContact = backupHPC;
              _purePersonContact = backupPPC;
              _objectContact = backupOC;

              if (collVol > 0.f)
              {
                break;
              }

              nextIter = n;

              //if (GetNetworkManager().IsClient())
              //{
              //  //CYAN - collision validation on each subdivision
              //  enf::Shape::CreateSphere(collTest.Position() + 2.f*VUp, 0.03f, 0xff00ccff, enf::SS_ONCE|enf::SS_NOOUTLINE);
              //}
            }

            const int NUM_CLOSE_SUBDIVS = 2;  //number of first subdivs considered close
            const bool isClientOnly(!GetNetworkManager().IsServerOwner());
            bool canTeleport(isClientOnly && (nextIter < NUM_CLOSE_SUBDIVS) && (numIter > NUM_CLOSE_SUBDIVS + 1));
            if (canTeleport && !_local)
            {
              const Vector3 localMoveDir = endPos - FutureVisualState().Position();
              const Vector3 remotePosDir = _lastRemotePos - FutureVisualState().Position();
              const float projDist = localMoveDir.DotProduct(remotePosDir);
              const float sqDistToLocalPos = localMoveDir.SquareSize();
              const float sqDistToRemotePos = remotePosDir.SquareSize();
              canTeleport = (projDist > 0.f) && (sqDistToLocalPos <= sqDistToRemotePos);
            }

            if (canTeleport)
            {
              //if collision place is very close, jump straight to the last position
              nextIter = numIter - 1;
            }
            else if (nextIter < NUM_CLOSE_SUBDIVS)
            {
              //advance to the next subdivision so that movement can be repaired
              nextIter = intMin(nextIter + 1, numIter - 1);
            }

            float const iterRatio = (float)nextIter / (numIter - 1);
            DoAssert(iterRatio > 0.f);  // always advance on next iteration
            moveTrans.SetPosition(prevPos.Lerp(endPos, iterRatio));
            doCollision(deltaT * iterRatio);
          }
          else
            doCollision(deltaT);
          
/*          moveDist = prevPos.Distance(moveTrans.Position());
          if(moveDist)
          {
            float speed = moveDist / deltaT;
            LogF("cspeed %f speed %f mspeed %f", speed, moveTrans._speed, moveTrans._modelSpeed);
          }*/
        } //if (prec<=SimulateVisibleFar) 
        else 
          OnNoCollide();

        if (bChange)
        {
          if ((fVoll >= fCollVol * ALLOWED_VOL_GROW || bTakeAllCost) /*&& prec <= SimulateVisibleNear && fCollVol == 0*/)
          {
            // take move
            if (_landContact)
            {
              // calculate y speed from x,z to follow surface
              FutureVisualState()._speed[1]=FutureVisualState()._speed[0]*_landDX+FutureVisualState()._speed[2]*_landDZ;
            }

            // calculate acceleration
            if( FutureVisualState()._speed.SquareSize()>=Square(0.1f) ) 
              _lastMovementTime=Glob.time;
             
            FutureVisualState()._acceleration = VZero;
            _angMomentum = VZero;
            _angVelocity = VZero;          

            if (!_ignoreLowPriorituContact)
            {
              _collVolume = fCollVol;
            }
            else
            {
              _ignoreLowPriorituContact = false;
              _collVolume = ValidatePotentialState(moveTrans);            
            }

            // Switch capsule back to normal one, if there is enough space. 
            if (_useSafeCapsule)
            {
              _useSafeCapsule = false;
              if (ValidatePotentialState(moveTrans)>0)
              {
                // safe capsule needs to be used again - normal capsule in collision
                _useSafeCapsule = true;
              }
              else if (!_useSafeCapsule)
              {
                // detection confirmed we can safely use normal capsule
                _collVolume = INFINITY_VOLUME;
              }
            }
          }
          else
          {
            // use old state
            UseOldState();      

            moveTrans = FutureVisualState();
            bChange = false;
          }
        }
      }
      else
      {
        OnNoCollide(); //if (prec<=SimulateVisibleFar) 
      }


      if (_landContact && _afterGetOut)
      {
        _afterGetOut = false; // after get out is only till landcontact
      }  
      // if all collisions was ignored because of "stuck" and man is not in collision anymore do not ignore ... 
      if (_ignoreCollisionIfStuck)
      {
        _stuckStarted = TIME_MAX;
        _ignoreCollisionIfStuck = false;
        if (ValidatePotentialState(moveTrans) > 0)
          _ignoreCollisionIfStuck = true;              
      }

    }
  } // if( _freeFall )

  MoveWeapons( deltaT, bChange);

  // Calculate body position after hit
  if(fabs(_hitSkeletonRotWanted - _hitSkeletonRot) > 0.01f)
  {
    //delta = _hitSkeletonRotWanted - _hitSkeletonRot;
    float d = deltaT * 6.0f;
    delta = _hitSkeletonRotWanted - _hitSkeletonRot;
    Limit(delta, -d, d);
    
    if(fabs(delta) < 0.04f)
    {
      _hitSkeletonRot = _hitSkeletonRotWanted;
    }
    else
    {
      //delta *= 0.3f;
      _hitSkeletonRot = _hitSkeletonRot + delta;
    }
    
    RecalcAffectSkeletonTransform();
  }
  else  //vrat se zpatky do vzprimeny pozice
  {
    _hitSkeletonRotWanted = 0.f;
  }

  _impulseForce = VZero; 
  _impulseTorque = VZero; 

  if (_doMark>0)
  {
    // it makes no sense to create stepmarks which are far from the camera
    VisualState &vs = FutureVisualState();
    if (EnableVisualEffects(prec, 20) && ValueTest(vs,&MoveInfoMan::OnLand) < 0.5f)
    {
      int index=(_doMark>1 ? Type()->_stepLIndex : Type()->_stepRIndex );
      // check foot position
      if (index>=0)
      {
        Vector3 mPos=AnimatePoint(vs,_shape->FindMemoryLevel(),index);
        Vector3 pos=vs.PositionModelToWorld(mPos);
        // leave stepmark

        float timeToLive= 10.0f * (GEngine->GetThermalVision() ? 5.0f : 1.0f);

        //timeToLive*=0.3;
        //Ref<Mark> mark=new Mark(shape,0.15,timeToLive);
        float ys = GLandscape->SurfaceY(pos.X(),pos.Z());
        bool isWater = false;
        Texture *texture = NULL;
        float yr = GLandscape->RoadSurfaceYAboveWater(pos, NULL, NULL, &texture);

#if _HELISIM_TERRAIN_RIVERS
        if (texture)
          isWater = texture->IsWater();
#endif

        if (!isWater && yr<=ys+0.2f)
        {
          LODShape *shape=GScene->Preloaded(_doMark>1 ? FootStepL : FootStepR);
          // TODO: VSMP: place mark on the finally confirmed position, not here
          Ref<Mark> mark=new Mark(shape,(GEngine->GetThermalVision() ? 1.0f : 0.5f),timeToLive);
          mark->SetDirectionAndUp(vs.Direction(),VUp);
          mark->SetPosition(pos);
          GWorld->AddAnimal(mark);
        }
      }
    }
    _doMark = 0;
  }

  //move
  {

    if (bChange)
    {     
      if (!IsNetworkPlayer() && IsLocal() && Brain() && (Brain()->GetAIDisabled() & (AIBrain::DAAnim|AIBrain::DAMove))==0 && _landContact && _hideBodyWanted == 0)
      {
         // we cannot use camera position against path - path is planned for 0,0,0
        #if 1
        Vector3 oldPos = FutureVisualState().Position();
        Vector3 newPos = moveTrans.Position();
        #else
        Vector3Val oldPos = CameraPosition();
        // for AI make sure the movement does not bring us too far from planned path
        // recompute what would the new CameraPosition be, assuming all other things equal
        // at this point we ignore changes caused by both RTM animation and any aiming/looking around...
        Vector3Val camModel = FutureVisualState().Transform().InverseRotation().FastTransform(camOldPos);
        Vector3Val newPos = moveTrans.FastTransform(camModel);
        #endif
        if (_coverState!=CoverNo)
        {
          // while in cover different handling is needed
        }
        else if (_brain->GetPath().Size()>=2)
        { // stick to the planned path
          const Path &path = _brain->GetPath();
          Vector3Val nearestNewPos = path.NearestPosXZ(newPos);
          Vector3Val nearestOldPos = path.NearestPosXZ(oldPos);
          Vector3 offset = ClipToPath(newPos,oldPos,nearestNewPos,nearestOldPos);
          Vector3 newTransPos = moveTrans.Position()+offset;
          moveTrans.SetPosition(newTransPos);
        }
        else if (_brain->GetPlanningMode()==AIBrain::DoNotPlanFormation && IsSimplePathValid(Glob.time))
        {
          Vector3Val nearestNewPos = NearestPoint(_simplePathFrom,_simplePathDst,newPos);
          Vector3Val nearestOldPos = NearestPoint(_simplePathFrom,_simplePathDst,oldPos);
          Vector3 offset = ClipToPath(newPos,oldPos,nearestNewPos,nearestOldPos);
          Vector3 newTransPos = moveTrans.Position()+offset;
          moveTrans.SetPosition(newTransPos);
        }
        else
        {
          // when there is no planned path, check against OperMap fields
          Vector3 allowedTo = _brain->NearestIntersection(oldPos,newPos);
          Vector3 offset = allowedTo - newPos;
          Vector3 newTransPos = moveTrans.Position()+offset;
          moveTrans.SetPosition(newTransPos);
        }

      }
      
    }

    Move(moveTrans);
    DirectionWorldToModel(FutureVisualState()._modelSpeed, FutureVisualState()._speed);
  }

  // tell the animation change to the scripts
  if (_moves.GetPrimaryMove(FutureVisualState()._moves).id != _primaryMoveBackUp.id)
  {
    OnEvent(EEAnimStateChanged, GetMovesType()->GetMoveName(_moves.GetPrimaryMove(FutureVisualState()._moves).id));
  }

  {
    AIBrain *unit = GWorld->FocusOn();
    Person *person = unit ? unit->GetPerson() : NULL;

    if (person == this && !GWorld->GetCameraEffect())
    {
      float pePar = FutureVisualState()._speed.Size();
      GEngine->SetSpecialEffectPars(1, &pePar, 1);

      pePar = _tired;
      GEngine->SetSpecialEffectPars(2, &pePar, 1);

      if (GWorld->GetCameraType() == CamGunner)
      {
        if (_ppeHndls.Size() == 0)
        {
          WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
          if (ptr.IsValid())
          {
            const OpticsInfo *opticsInfo = ptr.GetOpticsInfo();
            if (opticsInfo && opticsInfo->_ppEffectType.Size() > 0) { CreatePPEHndls(opticsInfo->_ppEffectType, _ppeHndls); }
          }
        }
      }
      else
      {
        if (_ppeHndls.Size() > 0) { DestroyPPEHndls(_ppeHndls); }
      }
    }
    else
    {
      if (_ppeHndls.Size() > 0) { DestroyPPEHndls(_ppeHndls); }
    }
  }

  if (bChange || leanChanged)
  {
    // if position or animation changed, we need to recalculate some world space / model space properties     
    RecalcPositions(moveTrans); 
  }


  _primaryMoveBackUp = _moves.GetPrimaryMove(FutureVisualState()._moves);   
  _primaryTimeBackUp = _moves.GetPrimaryTime(FutureVisualState()._moves);
  _primaryTimeDBackUp = _moves.GetPrimaryTimeDelta(FutureVisualState()._moves);
  _primaryFactorBackUp = _moves.GetPrimaryFactor(FutureVisualState()._moves);   
  _finishedBackUp = _moves.GetFinished(FutureVisualState()._moves);

  if (IsLocal() && _swimming && _tired>0.95 && HeavyWeaponsCount()>0)
  {
    DropHeavyWeaponsWhileSwimming();
  }

  // simulate holding the breath
  float maxHold = _breathAir;
  // we cannot hold breath fully when there is no air reserve
  if (_holdBreath>maxHold) _holdBreath = maxHold;
  const float maxHoldBreathTime = 10;
  _breathAir -= deltaT*(2/maxHoldBreathTime)*(_holdBreath-0.5);
  if (_breathAir<=0) _breathAir = 0;
  float maxAir = 1-_tired;
  if (_breathAir>maxAir) _breathAir = maxAir;

  // Simulation of head movement, it set up action, mimic
  // Can be called at the end;
  BasicSimulationCore(deltaT, prec); 

  int hAxisPt = Type()->_headAxisPoint;
  if (hAxisPt>=0)
  {
    if (!IsSwimmingInMove())
    {
      if (_waterDepth>0) _waterContact = true;
      else _waterContact = false;
      int level=_shape->FindMemoryLevel();
      Vector3 headPos = FutureVisualState().PositionModelToWorld(AnimatePoint(FutureVisualState(),level,hAxisPt));
      float seaLevel = GLandscape->GetSeaLevel(FutureVisualState().Position());
      if (headPos[1]<seaLevel) //head is under water
      {
        float drown = seaLevel-headPos[1];
        saturateMin(drown,2);
        //DWORD systime = ::GetTickCount();
        //LogF("%ud: Head is under water, drown = %.2f", systime, drown);
        // do some dammage
        if (IsLocal())
        {
          LocalDamageMyself(VZero,0.05f*deltaT*drown,1.0f);
        }
      }
    }
  }
  else if (_waterDepth>0) //old solution kept when there is no _headAxisPoint
  {
    _waterContact = true;
    //GlobalShowMessage(200,"Water %.2f",_waterDepth);
    float maxSafeDepth = 1.2f;
    if (IsDown()) maxSafeDepth = 0.6f;
    if( _waterDepth>maxSafeDepth )
    {
      float drown = (_waterDepth-maxSafeDepth)*(1.0f/0.5f);
      saturateMin(drown,2);
      // do some dammage
      if (IsLocal())
      {
        LocalDamageMyself(VZero,0.05f*deltaT*drown,1.0f);
      }
    }
  }
  else
  {
    _waterContact = false;
  }  

  TurretContext context;
  context._turret = NULL;
  context._turretType = NULL;
  context._gunner = this;
  context._weapons = &_weaponsState;
  if (context._weapons->_mGunClouds.Active() || context._weapons->_mGunFire.Active() || context._weapons->_gunClouds.Active())
  {
    WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
    if (ptr.IsValid())
    {
      Vector3 gunPos=FutureVisualState().PositionModelToWorld(GetWeaponPoint(FutureVisualState(),context, ptr));
      Vector3 dir = GetWeaponRelDirection(FutureVisualState(), ptr, true);
      FutureVisualState().DirectionModelToWorld(dir, dir);
      context._weapons->_mGunFire.Simulate(gunPos,deltaT);
      context._weapons->_mGunClouds.Simulate(gunPos,FutureVisualState().Speed()*0.7f+dir*5.0f,0.35f,deltaT);
      context._weapons->_gunClouds.Simulate(gunPos,FutureVisualState().Speed()*0.7f+dir*2.0f,0.35f,deltaT);
      //CancelStop();
    }
  }

  if (IsLocal())
  {
    if (QIsManual())
    {
      // advanced missile control
      // check if last shot is missile
      if (LauncherSelected())
      {
        Missile *missile = dyn_cast<Missile,Vehicle>(context._weapons->_lastShot);
        if (missile)
        {
        TurretContext context;
        context._turret = NULL;
        context._turretType = NULL;
        context._gunner = this;
        context._weapons = &_weaponsState;

          // check cursor position
          WeaponModePtr selected = _weaponsState.ValidatedCurrentWeapon();
          if (selected.IsValid())
          {
            Vector3 tmpEnd;
            GLandscape->IntersectWithGroundOrSea(&tmpEnd, FutureVisualState().Position(), GetWeaponDirection(FutureVisualState(), context, selected).Normalized(), 0, 3500);
            missile->SetControlDirection(tmpEnd);
          }
        }
      }

    }
  }

  // fake distance hiding simulation
  const float MaxCoverGrassSpeed = 0.07f;
  delta = (_coverGrassHeightWanted - _coverGrassHeight) * deltaT;
  saturate(delta, -MaxCoverGrassSpeed, MaxCoverGrassSpeed);
  _coverGrassHeight += delta;

  base::Simulate(deltaT,prec);
  SimulatePost(deltaT0,prec);
  // DoAssert(_moves.CheckValidState(GetMovesType()));
  // DoAssert(_gesture.CheckValidState(GetGesturesType()));
  UpdateJoinedObject(); // we call it in each frame to stay safe (we do not know if we are handling all possible changes)

  UpdateBow(deltaT, prec);
#if _ENABLE_PHYSICS1
	if(GetPhysics())
	{
		Vector3 center;
		Vector3 minMax[2];
		BoundingInfoUI(center, minMax);
		GetPhysics()->SetMatrix(Matrix4(MTranslation, BCenterPosition() + center));//FutureVisualState().Transform());
	}
#endif
}

void Man::SimulateSimple(float deltaT, SimulationImportance prec)
{
  // Man is frozen. Just do the animation, but don't move him. 
  float turn,moveX,moveZ;
  SimulateAnimations(turn,moveX,moveZ,deltaT,prec);
  base::Simulate(deltaT,prec);    
  BasicSimulationCore(deltaT, prec);
  SimulatePost(deltaT, prec);
  _impulseForce = VZero;
  _impulseTorque = VZero;

  _collVolume = INFINITY_VOLUME;
}

void Man::CreateFlashLight(FlashLigthInfo *flInfo)
{
  if (_light.IsNull() && flInfo)
  {
    // light shape
    Ref<LODShape> shape = GScene->Preloaded(GunLightModel);
    _light = new LightReflector(shape, flInfo->_color, flInfo->_colorAmbient, flInfo->_angle, flInfo->_innerAngleRatio, flInfo->_scale, true);
   	_light->SetRadius(flInfo->_radius);

    // add light to scene
    GScene->AddLight(_light);
    _lUpdateTime = Time(0);
    _lSimTime = Time(0);
  }
}

void Man::DisableFlashLight()
{
  _light.Free();
  _lUpdateTime = Time(0);
  _lSimTime = Time(0);
  _attenuation = 50.0f;
  _lightWanted = false;
  _lightForced = false;
}

void Man::SimulateFlashLight(float deltaT, Vector3Val begPos, Vector3Val dir)
{
  const float attenuation = 50.0f;

  if (!_lightWanted) return;

  if (GWorld->FocusOn() == Brain() && GWorld->GetCameraType() != CamGroup)
  {
    _attenuation = attenuation;
  }
  else
  {
    if (_lUpdateTime + 0.2f < Glob.time)
    {
      Vector3 endPos(begPos + dir * attenuation);

      CollisionBuffer result;
      GLandscape->ObjectCollisionLine(GetFutureVisualStateAge(),result, Landscape::FilterIgnoreOne(this), begPos, endPos, 0/*0.5f*/, ObjIntersectView, ObjIntersectFire, CF_NEAREST_CONTACT);

      // select the nearest
      if (result.Size() > 0)
      {
        endPos = result[0].pos;
        _attenuationWanted = 0.75 * begPos.Distance(endPos);
      }
      else
      {
        _attenuationWanted = attenuation;
      }

      _lUpdateTime = Glob.time;
    }

    float delta = deltaT * (_attenuationWanted - _attenuation);
    saturate(delta, -0.4f, 0.4f);
    _attenuation += delta;
  }

  // apply attenuation factor
  _light->SetRadius(_attenuation);
  // update direction
  _light->SetDirection(dir);
  // update position
  _light->SetPosition(begPos);
}

void Man::IRLaserWanted(bool enable)
{
  if (enable)
  {
    _irWanted = true;
    GScene->AddIRRay(this, true);
  }
  else
  {
     _irWanted = false;
     GScene->AddIRRay(this, false);
  }
}


bool Man::GetWeaponAim(TurretContext context, Vector3 &position, Vector3 &direction)
{
  WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
  if (ptr.IsValid())
  {
    const MuzzleType *muzzle = ptr.GetMuzzleType();
    const ManVisualState &vs = FutureVisualState();

    Matrix4 animTrans;
    if (muzzle && GetGunMatrix(vs, context, ptr, animTrans))
    {
      position = animTrans.FastTransform(muzzle->_muzzlePos);
      position = vs.Position() + vs.DirectionModelToWorld(position);

      direction = animTrans.Rotate(muzzle->_muzzleDir);
      direction = vs.DirectionModelToWorld(direction);
      direction.Normalize();

      return true;
    }
  }
  return false;
}

/** Function tests if capsule for move will collide.
  @param move - (in) move identification.
  */
bool Man::IsCollisionFreeMove(MoveId move)
{
  // DoAssert(_moves.CheckValidState(GetMovesType()));
  
  MoveId moveBackUp = _moves.SetTempPrimaryMove(FutureVisualState()._moves,move);

  Vector3 repairMove;
  float fValue = ValidatePotentialState(FutureVisualState(), false, &repairMove);
  
  if (fValue > 0 && repairMove.SquareSize() > 0 && repairMove.SquareSize() < 0.25f)
  {
    ManVisualState moveTrans = FutureVisualState();
    moveTrans.SetPosition(moveTrans.Position() + repairMove);

    fValue = ValidatePotentialState(moveTrans);
  }

  _moves.SetTempPrimaryMove(FutureVisualState()._moves,moveBackUp);

  // DoAssert(_moves.CheckValidState(GetMovesType()));
  
  return fValue == 0;
}

const float AutoActionLimit = 0.75f;

static bool AutoActionEnabled(UIActionType type)
{
  switch (type)
  {
  case ATGetInDriver:
  case ATGetInPilot:
  case ATGetInTurret:
  case ATGetInCargo:
  case ATLadderOnDown:
  case ATLadderOnUp:
    return true;
  }
  return false;
}

static bool AutoActionHighlightOnly(UIActionType type)
{
  switch (type)
  {
  case ATGetInDriver:
  case ATGetInPilot:
  case ATGetInTurret:
  case ATGetInCargo:
    return true;
  }
  return false;
}

void Man::OnCollide(CollisionBuffer& collisions,  MoveSets& sets, int maxSet)
{
  Object *with = collisions[sets[maxSet -1]].object;
  if (_collideWith != with)
  {
    _collideSince = Glob.time;
    _collideWith = with;
    _autoActionProcessed = false;
    _autoActionTime = Glob.time + AutoActionLimit;
    _autoActionType = ATNone;
  }

  //AddForce(with->Position(), Vector3(0,10,1));

  // check conditions
  Assert(with);
  if (_autoActionProcessed) return;

  // for player unit, try to perform auto action
  AIBrain *agent = Brain();
  if (!agent || !agent->IsPlayer())
  {
    _autoActionProcessed = true; // check only once
    return;
  }

  // - target object is actions source
  EntityAI *veh = dyn_cast<EntityAI>(with);
  if (!veh)
  {
    _autoActionProcessed = true; // check only once
    return;
  }
  
  // - do not perform action during free fall or action processing
  if (_freeFall /*|| IsActionInProgress(MFUIAction)*/)
  {
    _autoActionType = ATNone;
    _autoActionTime = Glob.time + AutoActionLimit; // reset timer
    if (CHECK_DIAG(DEAutoAction))
    {
      DIAG_MESSAGE(1000, "Auto action canceled - free fall");
    }
    return;
  }

  // - check if enabled for actual animation
  if (!EnableAutoActions(FutureVisualState()))
  {
    _autoActionType = ATNone;
    _autoActionTime = Glob.time + AutoActionLimit; // reset timer
    if (CHECK_DIAG(DEAutoAction))
    {
      DIAG_MESSAGE(1000, "Auto action canceled - animation");
    }
    return;
  }

  // select the best action
  Action *bestAction = nullptr;
  float maxPriority = -FLT_MAX;
  veh->GetActions(
      [this, &bestAction, &maxPriority](const UIAction &action) -> void 
      {
        DoAssert(action.action);
        if (!AutoActionEnabled(action.action->GetType())) return;
        if (action.priority > maxPriority)
        {
          maxPriority = action.priority;
          bestAction = action.action;
        }
      },
      agent);

  if (!bestAction)
  {
    _autoActionType = ATNone;
    _autoActionTime = Glob.time + AutoActionLimit; // reset timer
    if (CHECK_DIAG(DEAutoAction))
    {
      DIAG_MESSAGE(1000, "Auto action canceled - no action");
    }
    return;
  }

  if (bestAction->GetType() != _autoActionType)
  {
    _autoActionType = bestAction->GetType();
    _autoActionTime = Glob.time + AutoActionLimit; // reset timer
    if (CHECK_DIAG(DEAutoAction))
    {
      DIAG_MESSAGE(1000, "Auto action canceled - action changed");
    }
    return;
  }

  if (CHECK_DIAG(DEAutoAction))
  {
    DIAG_MESSAGE(100, Format("Auto action: %.1f s", _autoActionTime - Glob.time));
  }

  if (AutoActionHighlightOnly(bestAction->GetType())) return;

  // check if time for action reached
  if (Glob.time < _autoActionTime) return;

  // perform action
  StartActionProcessing(bestAction, agent);
  _autoActionProcessed = true;
}

bool Man::IsActionHighlighted(UIActionType type, EntityAI *target) const
{
  return type == _autoActionType && target == _collideWith && !_autoActionProcessed;
}

void Man::OnNoCollide()
{
  _collideWith = NULL;
}


void Man::SetFace(RString name, RString player)
{
  if (name.GetLength() <= 0)
  {
    ErrF("No face for %s",cc_cast(GetDebugName()));
    name = "Default";
  }
#if _ENABLE_NEWHEAD
  SetHead(GetFaceType(), name);

  // Read the face and face-wound textures
  {
    // Get the config entry of the particular face
    RString faceType = GetFaceType();
    ParamEntryVal cfg = Pars >> "CfgFaces" >> faceType;
    ConstParamEntryPtr cls = cfg.FindEntry(name);
    if (!cls)
    {
      RptF("Error: Error during SetFace - class CfgFaces.%s.%s not found", cc_cast(faceType), cc_cast(name));
      cls = cfg.FindEntry("Default");
      if (!cls)
      {
        RptF("Error: Error during SetFace - class CfgFaces.%s.Default not found", cc_cast(faceType));
        return;
      }
    }

    // Read material of the face
    RString matName = GetPictureName(*cls >> "material");
    if (matName.GetLength() == 0)
    {
      // do not change the material
      _faceMaterial = NULL;
    }
    else
    {
      Ref<TexMaterial> mat = GTexMaterialBank.New(TexMaterialName(matName));
      if (mat)
      {
        _faceMaterial = mat;
      }
      else
      {
        RptF("SetFace error: material %s not found", cc_cast(matName));
      }
    }

    // Read texture of the face
    RString textName = GetPictureName(*cls >> "texture");
    Ref<Texture> text = GlobLoadTexture(textName);
    if (text)
    {
      _faceTexture = text;
    }
    else
    {
      RptF("SetFace error: texture %s not found", cc_cast(textName));
    }

    if (stricmp(cls->GetName(), "custom") == 0)
    {
      RString GetUserDirectory();
      RString GetClientCustomFilesDir();

      RString dir;
      if (player.GetLength() > 0)
        dir = GetClientCustomFilesDir() + RString("\\players\\") + EncodeFileName(player) + RString("\\");
      else
        dir = GetUserDirectory();
      dir.Lower();
      RString name = dir + RString("face.paa");
      if (QIFileFunctions::FileExists(name))
      {
        Ref<Texture> text = GlobLoadTexture(name);
        if (text)
          _faceTexture = text;
        else
        {
          RptF("SetFace error: cannot load custom face texture %s", cc_cast(name));
        }
      }
      else
      {
        name = dir + RString("face.jpg");
        if (QIFileFunctions::FileExists(name))
        {
          Ref<Texture> text = GlobLoadTexture(name);
          if (text)
            _faceTexture = text;
          else
          {
            RptF("SetFace error: cannot load custom face texture %s", cc_cast(name));
          }
        }
        else
        {
          RptF("SetFace error: custom face texture not found in %s", cc_cast(dir));
        }
      }
    }

    // Read wounded texture of the face associated with the texture
    if (_faceTexture)
    {
      ParamEntryVal cfg = Pars>>"CfgFaceWounds">>"wounds";
      for (int i=0; i<cfg.GetSize()-1; i+=2)
      {
        RStringB origName = cfg[i];
        RStringB woundName = cfg[i+1];
        // check if origName is name of currently set texture

        RStringB origTexName = GetDefaultName(origName, ".pac");
        if (strcmp(origTexName,_faceTexture->GetName())) continue;
        RStringB woundTexName = GetDefaultName(woundName, ".pac");
        // TODO: 3-step wounds
        _faceTextureWounded[0] = GlobLoadTexture(woundTexName);
        _faceTextureWounded[1] = GlobLoadTexture(woundTexName);
      }
    }
  }
#else
  _head.SetFace(Type()->_head, GetFaceType(), _shape, name, player);
  if (!_head.GetFace())
  {
    RptF("SetFace warning: missing face texture on %s (after SetFace)", cc_cast(GetDebugName()));
  }
#endif
}

#if _ENABLE_NEWHEAD
void Man::RemoveGlasses()
{
  if (_glassesType)
  {
    _glassesType->ShapeRelease();
    _glassesType.Free();
  }
}
#endif

void Man::SetGlasses(RString name)
{
  if (name.GetLength() == 0)
  {
    ErrF("No glasses for %s",cc_cast(GetDebugName()));
    name = "None";
  }
#if _ENABLE_NEWHEAD
  if (_glassesType) RemoveGlasses();
  _glassesType = GlassesTypes.New(name);
  _glassesType->ShapeAddRef();
#else
  _head.SetGlasses(Type()->_head, _shape, name);
#endif
}

void Man::SetMimic(RStringB name)
{
  #if _ENABLE_NEWHEAD
  if (_headType.NotNull())
  {
    _head.SetForcedGrimace(_headType->GetGrimaceIndex(name));
  }
  #else
  _head.SetForceMimic(name);
  #endif
}

bool Man::IsIdentityEnabled(RString identityType) const
{
  const FindArrayRStringCI &enabled = Type()->_identityTypes;
  if (enabled.Size() == 0) return true; // when non is given, all is enabled
  return enabled.FindKey(identityType) >= 0;
}

void Man::MinimalWeapons()
{
  RemoveAllWeapons(false); // do not remove items
  RemoveAllMagazines();
  if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicle() == this)
    GWorld->UI()->ResetVehicle(this);
}

void Man::ScanEquipment()
{
  _hasNVG = GetSpecialItem(WSNVGoggles) != NULL;
  if (!_hasNVG)
  {
    _nvg = false;
  }
}

float Man::GetEncumbrance() const
{
  // TODO: other calculation, which takes also strength, own weight etc.
  return 0;
}

void Man::OnWeaponAdded()
{
  base::OnWeaponAdded();
  ScanEquipment();
}

bool Man::HasWeaponIR(const WeaponModePtr &ptr) const
{
  if (ptr.IsValid())
  {
    const MuzzleType *muzzle = ptr.GetMuzzleType();
    return _irWanted && muzzle && muzzle->_irDistance > 0;
  }
  return false;
}

bool Man::HasWeaponFlashLight(const WeaponModePtr &ptr) const
{
  if (ptr.IsValid())
  {
    const MuzzleType *muzzle = ptr.GetMuzzleType();
    return (_lightWanted && muzzle && muzzle->_gunLightInfo.NotNull());
  }
  return false;
}

bool Man::HasReflectorAttached() const
{
  auto func = [](const InventoryItem *item) -> bool
  {
    const InventoryItemType *type = item->Type();
    return type && type->_lights.SomeLights(); // break when some light found
  };

  for (int i=0; i<NInventorySlotId; i++)
  {
    const InventoryItem *item = _inventorySlots[i];
    if (!item) continue;
    if (func(item) || item->ForEachAttachment(func)) return true;
  }
  if (_itemInHands && (func(_itemInHands) || _itemInHands->ForEachAttachment(func))) return true;
  return false;
}

void Man::UpdateJoinedObject()
{
  WeaponModePtr ptr = GetCurrentHandWeapon(); 
  if (!_isDead && (HasWeaponFlashLight(ptr) || HasWeaponIR(ptr) || HasReflectorAttached()))
  {
    if (!_isAttached)
    {
      GWorld->AddAttachment(this);
      _isAttached = true;
    }
  }
  else
  {
    if (_isAttached)
    {
      GWorld->RemoveAttachment(this);
      _isAttached = false;
      // also make sure to simulate effect of the function
      DisableFlashLight();
      IRLaserWanted(false);
    }
  }
}

void Man::OnWeaponRemoved()
{
  ScanEquipment();
  DisableFlashLight();
  IRLaserWanted(false);
  UpdateJoinedObject();

  base::OnWeaponRemoved();
}

void Man::OnWeaponChanged()
{
  base::OnWeaponChanged();
  ScanEquipment();
  WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
  if (!HasWeaponFlashLight(ptr)) DisableFlashLight();
  if (!HasWeaponIR(ptr)) IRLaserWanted(false);
  UpdateJoinedObject();
}

void Man::OnWeaponSelected()
{
  //we have to detect Launcher in each step in Soldier::KeyboardPilot() to make it able to abort launcher swaping action
  /*
  if (LauncherAnimSelected() && GetActUpDegree(FutureVisualState()) != ManPosWeapon)
  {
    ActionMap *map = Type()->GetActionMap(_moves.GetPrimaryMove(FutureVisualState()._moves).id);
    MoveId moveWanted = Type()->GetMove(map, ManActWeaponOn);
    _moves.SetForceMove(MotionPathItem(moveWanted, NULL));
  }
  */
 
  // flashlight off when weapon changed
  DisableFlashLight();
  IRLaserWanted(false);
  UpdateJoinedObject();
  base::OnWeaponSelected();
}

void Man::OnDanger()
{
  float reaction = 0.3f;
  if (_lookForwardTimeLeft>reaction)
  {
    _lookForwardTimeLeft = reaction;
  }
}

bool Man::CanReloadCurrentWeapon()
{
  // disable reloading when launcher is selected, but not in hands
  return !LauncherSelected() || GetActUpDegree(FutureVisualState()) == ManPosWeapon;
}

void Man::OnBowStateChange(BowState newState)
{
  const bool holdsString = (BSDrawn == newState) || (BSCanceled == newState);
  SetPullingBowString(holdsString);

  if (IsLocal())
  {
    GetNetworkManager().UpdateBowState(this, newState);
  }
}

void Man::UpdateBow(float deltaT, SimulationImportance prec)
{
  WeaponModePtr selectedWeapon = _weaponsState.ValidatedCurrentWeapon();

  if (_pullingBowString && selectedWeapon.IsValid() && selectedWeapon._weapon->Type()->_bow)
  {
    LODShape *shape = GetShape();
    if (shape)
    {
      int leftPoint = Type()->_leftHandPoint;
      int rightPoint = Type()->_rightHandPoint;
      if (leftPoint >= 0 && rightPoint >= 0)
      {
        const int level = shape->FindMemoryLevel();
        const ObjectVisualState &vs = FutureVisualState();
        const Vector3 leftPos = AnimatePoint(vs, level, leftPoint);
        const Vector3 rightPos = AnimatePoint(vs, level, rightPoint);
        float const handsDist = rightPos.Distance(leftPos);
        float const IDLE_HANDS_DIST = 0.19408f;
        float const offset = floatMax((handsDist - IDLE_HANDS_DIST), 0.f);
        SetBowStringOffset(offset);
      }
    }
  }

  float stringOffset = GetBowStringOffset();
  if (selectedWeapon.IsValid())
  {
    if (!_pullingBowString && (stringOffset > 0.f))
    {
      static float RELEASE_SPEED = 13.f;
      stringOffset -= RELEASE_SPEED * deltaT;
      saturateMax(stringOffset, 0.f);
      SetBowStringOffset(stringOffset);
    }
    selectedWeapon.SetDrawBowProgress(stringOffset);
  }
  else if (stringOffset > 0.f)
  {
    selectedWeapon.SetDrawBowProgress(0.f);
    SetBowStringOffset(0.f);
  }
}

void Man::Init( Matrix4Par pos, bool init )
{
  // if soldier has only handgun and not rifle, mark it as using handgun
  // assume position is already set
  ReactToDamage();
  if (init) ResetMovement(0); // do not reset for get out, will be reset later
  FutureVisualState()._aimingPositionWorld = CalculateAimingPosition(pos);
  FutureVisualState()._cameraPositionWorld = CalculateCameraPosition(pos);
  //_headPositionWorld = CalculateHeadPosition(pos);
  base::Init(pos, init);
  ScanEquipment();
}

void GetHeadLookAroundActions(InputInterface *input, float deltaT, float &headingChangeWanted, float &diveChangeWanted)
{
  const float diag = 0.5 * H_SQRT2;
  /*
  float headSpeed = (
  input->GetAction(UALookLeft)
  + input->GetAction(UALookLeftUp)*diag
  + input->GetAction(UALookLeftDown)*diag
  - input->GetAction(UALookRight)
  - input->GetAction(UALookRightUp)*diag
  - input->GetAction(UALookRightDown)*diag
  );
  headingChangeWanted = deltaT*headSpeed;
  float headChange=deltaT*headSpeed;
  */
  float diveSpeed = (
    input->GetAction(UALookDown)
    + input->GetAction(UALookLeftDown)*diag
    + input->GetAction(UALookRightDown)*diag
    - input->GetAction(UALookUp)
    - input->GetAction(UALookRightUp)*diag
    - input->GetAction(UALookLeftUp)*diag
    );
  diveChangeWanted = -deltaT*diveSpeed;
  float initHead = 0.0f; //TODO
  if( input->GetAction(UALookLeftUp) )
  {
    headingChangeWanted = initHead+H_PI/4;
    diveChangeWanted = 0.0f;
  }
  else if( input->GetAction(UALookLeft) )
  {
    headingChangeWanted = initHead+H_PI/2;
    diveChangeWanted = 0.0f;
  }
  else if( input->GetAction(UALookLeftDown) )
  {
    headingChangeWanted = initHead+H_PI*0.99;
    diveChangeWanted = 0.0f;
  }
  else if( input->GetAction(UALookRightUp) )
  {
    headingChangeWanted = initHead-H_PI/4;
    diveChangeWanted = 0.0f;
  }
  else if( input->GetAction(UALookRight) )
  {
    headingChangeWanted = initHead-H_PI/2;
    diveChangeWanted = 0.0f;
  }
  else if( input->GetAction(UALookRightDown) )
  {
    headingChangeWanted = initHead-H_PI*0.99;
    diveChangeWanted = 0.0f;
  }
  else headingChangeWanted = initHead;
  if( input->GetActionToDo(UALookCenter,false) )
  {
    headingChangeWanted = 0;
    diveChangeWanted = 0;
  }
}

bool Man::IsOnLadder(Building *obj, int ladder) const
{
  return _ladderBuilding==obj && ladder==_ladderIndex;
}

bool Man::IsOnAnyLadder() const
{
  return _ladderBuilding && _ladderIndex >= 0;
}

/*!
\patch 1.55 Date 5/7/2002 by Ondra
- Fixed: Soliders now climb ladders fasters.
*/

void Man::SimLadderMovement(float deltaT, SimulationImportance prec)
{
  float turn,moveX,moveZ;

  bool change = SimulateAnimations(turn, moveX, moveZ, deltaT, prec);

  BasicSimulationCore(deltaT, prec);

  // might drop ladder during SimulateAnimations

  if (!_ladderBuilding) return;

  // check ladder positions
  Building *obj = _ladderBuilding;
  const BuildingType *type = obj->Type();
  const Ladder &ladder = type->GetLadder(_ladderIndex);
  // get top/bottom positions
  int mem = obj->GetShape()->FindMemoryLevel();
  Vector3 top = obj->FutureVisualState().PositionModelToWorld(obj->AnimatePoint(obj->FutureVisualState(),mem,ladder._top));
  Vector3 bottom = obj->FutureVisualState().PositionModelToWorld(obj->AnimatePoint(obj->FutureVisualState(),mem,ladder._bottom));

  // check AI / keyboard speed
  float speed = 0;
  if (IsLocal())
  {
    _headYRotWantedAuto = 0;
    _headXRotWantedAuto = 0;
    if (GetNetworkManager().IsClient() ? IsRemotePlayer() : QIsManual())
    {
      InputInterface *input = GetNetworkManager().GetInputInterface(GetRemotePlayer());
      _headYRotWantedCont = H_PI*(input->GetAction(UALookLeftCont,false) - input->GetAction(UALookRightCont,false));
      _headXRotWantedCont = H_PI*(input->GetAction(UALookUpCont,false) - input->GetAction(UALookDownCont,false));
      float headingChangeWanted=0.0f, diveChangeWanted=0.0f;
      GetHeadLookAroundActions(input, deltaT, headingChangeWanted, diveChangeWanted);
      _headXRotWanted += diveChangeWanted;
      _headYRotWantedPan = headingChangeWanted;
      speed =
      (
        input->GetAction(UAMoveForward) + 
        input->GetActionMoveFastForward() -
        input->GetAction(UAMoveBack)
      );
      speed += input->GetAction(UAMoveUp) - input->GetAction(UAMoveDown);
      saturate(speed,-1,+1);

      // use freelook
      float aimX = input->GetActionWithCurve(InputInterface::CurveFovMouse,UAAimHeadRight,UAAimHeadLeft);
      float aimY = input->GetActionWithCurve(InputInterface::CurveFovMouse,UAAimHeadUp,UAAimHeadDown);

      // calculate head and weapon direction
      float rotXWanted = _headXRotWanted + aimY*deltaT;
      float rotYWanted = _headYRotWanted - aimX*deltaT;

      // calculate world space aiming direction
      Matrix3 rotXMat(MRotationX,-rotXWanted);
      Matrix3 rotYMat(MRotationY,+rotYWanted);
      Vector3 aimDir = FutureVisualState().DirectionModelToWorld(rotYMat * rotXMat.Direction());

      // AimHeadAndWeapon will set both _headXRotWanted and _gunXRotWanted
      AimHeadAndWeapon(aimDir);
      AimHead(aimDir);
    }
    else
    {
      speed = _ladderAIDir;
      _headYRotWantedCont = _headXRotWantedCont = _headXRotWantedPan = _headYRotWantedPan = 0.0f;
      FutureVisualState()._headXShiftWantedCont = FutureVisualState()._headYShiftWantedCont = FutureVisualState()._headZShiftWantedCont = FutureVisualState()._headZRotWantedCont = 0;
      FutureVisualState()._headXShiftWanted = FutureVisualState()._headYShiftWanted = FutureVisualState()._headZShiftWanted = FutureVisualState()._headZRotWanted = 0;
    }
  }
  MoveWeapons(deltaT, change);

  // check distance between top/bottom
  float invDist = (top-bottom).InvSize();
  float relChange = speed*deltaT*invDist;
  _ladderPosition += relChange;
  bool topEnd = _ladderPosition>=1 && relChange>0;
  bool bottomEnd = _ladderPosition<=0 && relChange<0;
  // TODO: corresponding animation
  saturate(_ladderPosition,0,1);
  
  if( !IsLocal() )
  {
	  if( _ladderPosition != _ladderPositionLast )
		speed = (_ladderPosition - _ladderPositionLast > 0) ? 1 : -1;
	  _ladderPositionLast = _ladderPosition;
  }


  MoveId id = MoveId(MoveIdNone);
  ActionMap *map = Type()->GetActionMap(_moves.GetPrimaryMove(FutureVisualState()._moves).id, AnimSubIx());
  // check if we are still on ladder

  if (!EnableTest(FutureVisualState(),&MoveInfoMan::OnLadder))
  {
    //LogF("Off ladder");
    DropLadder(_ladderBuilding,_ladderIndex);
    MoveId id = Type()->GetMove(map, ManActLadderOffTop);
    if (id!=MoveIdNone)
    {
      SetMoveQueue(id,false);
    }
    return;
  }
  if (map)
  {
    if (speed<-0.1f)
    {
      id = Type()->GetMove(map, ManActDown);
    }
    else if (speed>0.1f)
    {
      id = Type()->GetMove(map, ManActUp);
    }
    else
    {
      id = Type()->GetMove(map, ManActStop);
    }
    if (topEnd)
    {
      id = Type()->GetMove(map, ManActLadderOffTop);
    }
    if (bottomEnd)
    {
      id = Type()->GetMove(map, ManActLadderOffBottom);
    }
  }
  if (id!=MoveIdNone)
  {
    SetMoveQueue(id,false);
  }

  //LogF("speed %.3f, %s",speed,NAME_T(Type(),id));

  // current positon is given by top..bottom
  Vector3 aPos = (top-bottom)*_ladderPosition+bottom;

  Frame moveTrans = GetFrameBase(); // store old position
  moveTrans.SetPosition(aPos);
  Move(moveTrans);

  RecalcGunTransform();
  RecalcPositions(moveTrans);
}

void Man::CatchLadder(Building *obj, int ladderIndex, bool up)
{
  if (obj)
  {
    Vector3 ladderForward = obj->GetLadderForward(ladderIndex);
    
    Matrix4 trans;
    trans.SetPosition(FutureVisualState().Position());
    trans.SetUpAndDirection(VUp, obj->FutureVisualState().DirectionModelToWorld(ladderForward));
    Move(trans);
  }
  
  // perform actual catch/drop ladder
  _ladderBuilding = obj;
  _ladderIndex = ladderIndex;
  _ladderPosition = up;
  _ladderAIDir = up ? -1 : +1;
  //LogF("catch %d",_ladderAIDir);
}

void Man::DropLadder(Building *obj, int ladder)
{
  // perform actual catch/drop ladder
  _ladderBuilding = NULL;
  _ladderIndex = -1;
}

/// check the obstrucion/occlusion only once
struct ObstructionCached
{
  bool cached;
  
  float obstruction, occlusion;
  
  ObstructionCached()
  :obstruction(1),occlusion(1),cached(false)
  {
  }
  
  void CheckSoundObstruction(EntityAI *veh, bool isInside, float &obs, float &occ)
  {
    if (!cached)
    {
      GWorld->CheckSoundObstruction(veh,isInside,obstruction,occlusion);
      cached = true;
    }
    obs = obstruction, occ = occlusion;
  }
};

const SoundPars* SoundHitDamage::SelectHitSound(float probab) const
{
  const AutoArray<SoundProbab> &hitArr = _hitSounds._pars;

  if (hitArr.Size() > 0)
  {
    for (int i = 0; i < hitArr.Size(); i++)
    {
      probab -= hitArr[i]._probability;
      if (probab <= 0) return &hitArr[i];
    }

    return &hitArr[hitArr.Size() - 1];
  }

  return NULL;
}

const SoundDamage* SoundHitDamage::SelectDamageSound(float probab, const RString &name) const
{
  for (int i = 0; i < _damageSounds.Size(); i++)
  {
    // names match
    if (strcmpi(cc_cast(_damageSounds[i]._name), cc_cast(name)) == 0)
    {
      const AutoArray<SoundDamage> &damArr = _damageSounds[i]._pars;

      if (damArr.Size() > 0)
      {
        for (int j = 0; j < damArr.Size() - 1; j++)
        {
          probab -= damArr[j]._probability;
          if (probab <= 0) return &damArr[j];
        }

        return &damArr[damArr.Size() - 1];
      }
    }
  }

  return NULL;
}

const SoundPars& Man::SelectBulletSoundPars(const TurretContext &context, const WeaponModePtr &ptr) const
{
  // select bullet sound based on surface
  if (_surfaceSound.GetLength() > 0)
  {
    // fix bug when weapon was removed during no last bullet fall down
    if (context._weapons && ptr.IsValid())
    {
      // samples are defined: [0 - 1/3] metallic surfaces, [1/3 - 2/3] hard surfaces and [2/3 - 1] soft surfaces
      float rand = GRandGen.RandomValue() * .33333f;
      const float nextGroup = 1.0f/3.0f;

      if (strcmpi(cc_cast(_surfaceSound), "water") == 0)
      {
        return base::SelectBulletSoundPars(context, ptr);
      }

      const MuzzleType *muzzle = ptr.GetMuzzleType();

      if (strcmpi(cc_cast(_surfaceSound), "metal") == 0)
      {
        const SoundPars &pars = muzzle ? muzzle->_bullets.SelectSound(rand) : base::SelectBulletSoundPars(context, ptr);
        return pars;
      }

      rand += nextGroup;
      // hard sounds
      if ((strcmpi(cc_cast(_surfaceSound), "road") == 0) || (strcmpi(cc_cast(_surfaceSound), "gravel") == 0) || 
        (strcmpi(cc_cast(_surfaceSound), "rock") == 0))
      {
        const SoundPars &pars = muzzle ? muzzle->_bullets.SelectSound(rand) : base::SelectBulletSoundPars(context, ptr);
        return pars;
      }

      // soft sounds
      rand += nextGroup;
      const SoundPars &pars = muzzle ? muzzle->_bullets.SelectSound(rand) : base::SelectBulletSoundPars(context, ptr);
      return pars;
    }

    return base::SelectBulletSoundPars(context, ptr);
  }
  else
  {
    return base::SelectBulletSoundPars(context, ptr);
  }
}

void Man::Sound( bool inside, float deltaT )
{
#if defined SERVER_CORPSE_PERFORMANCE_PATCH
  if (IsDamageDestroyed())
    return;
#endif

  PROFILE_SCOPE_EX(snMan,sound);
#if 1
  const float slideSpeedThresholdForSound = 0.1f;
  bool loopSound = false;
#define SLIDING_SOUND_ENABLED 1

#if SLIDING_SOUND_ENABLED
  if ( _slideSpeed > slideSpeedThresholdForSound )
  {
    _soundStepOverride = "slide";
    if (!_slideSoundRunning)
    {
      _doSoundStep = true; //start sliding
      loopSound = true;
      _slideSoundRunning = true;
    }
    else _doSoundStep = false; //slide sound is playing already
  }
  else
  {
    if (_slideSoundRunning)
    { //stop sliding sound
      _slideSoundRunning = false;
      _soundStep = NULL;
    }
  }
#endif //SLIDING_SOUND_ENABLED

  const VisualState &vs = RenderVisualState();
  Vector3 pos = vs._cameraPositionWorld;
  Vector3 speed = vs.Speed();
  // some sounds should be placed on the ground (footsteps)
  // other sounds should come from the soldier body
  Vector3 centerPos = AimingPosition(vs);


  float obstruction = 1, occlusion = 1;
  ObstructionCached obs;
  if (!inside) { obs.CheckSoundObstruction(this, false, obstruction, occlusion); }

  const ManType* type = Type();

  // damage
  if (!_isDead && (_damageTimeToLive < Glob.time) && (type->_damageHitSound.Size() != 0))
  {
    float hit = GetTotalDamage();

    if (hit > 0.1f && (GetHit(type->_headHit) <= 0.9f))
    {
      // hit points name
      static const RString hitBodyParts[] = { "head", "body", "hands", "legs" };
      // default body hit = general damage sounds
      int hitIndex = 1;

      // 10% chance to select specific damage body
      if (GRandGen.RandomValue() < 0.1f)
      {      
        // select the most injured part
        float bodyHit = GetHitCont(type->_bodyHit);
        if (GetHitCont(type->_handsHit) > bodyHit) hitIndex = 2;
        if (GetHitCont(type->_legsHit) > bodyHit) hitIndex = 3;
      }

      if (hitIndex >= 0 && lenof(hitBodyParts))
      {
        Assert((_damageHitSelect >= 0) && (_damageHitSelect < type->_damageHitSound.Size()));

        // select damage sound pars
        const SoundDamage* pars = type->_damageHitSound[_damageHitSelect].SelectDamageSound(GRandGen.RandomValue(), hitBodyParts[hitIndex]);

        if (pars && GSoundScene->CanBeAudible(centerPos, pars->distance))
        {
          float timeToLive = GRandGen.Gauss(pars->_min, pars->_mid, pars->_max);
          _damageTimeToLive = Glob.time + timeToLive;

          if (_soundDamage) _soundDamage.Free();

          float volume = GRandGen.RandomValue() * 0.4f + 0.8f;
          float frequency = GRandGen.RandomValue() * 0.1f + 0.95f;

          _soundDamage = GSoundScene->OpenAndPlayOnce(pars->name, this, false, centerPos, VZero, volume, frequency, pars->distance);

          if (_soundDamage)
          {
            if (inside) volume *= 0.5;
            _soundDamage->SetPosition(centerPos, VZero);
            _soundDamage->SetVolume(pars->vol * volume, pars->freq * frequency);
            _soundDamage->SetObstruction(obstruction, occlusion);
          }
        }
      }
    }
  }

  // equipment sounds
  float maxVol = type->GetEnvSoundMaxVol();

  if (_doSoundStep)
  {
    // equipment sound
    AIBrain *unit = Brain();
    TargetSide side = unit ? unit->GetSide() : TSideUnknown;

    if ((side != TSideUnknown) && inside || GSoundScene->CanBeAudible(WaveEffect,WorldPosition(vs),maxVol))
    {
      const SoundPars *pars = NULL;
      pars = type->GetEquipSoundExtRandom((side == TCivilian) ? "civilian" : "soldier", _soundStepOverride);

      if (pars && GSoundScene->CanBeAudible(centerPos, pars->distance))
      {
        if (!_soundEquip || stricmp(pars->name, _soundEquip->Name()) != 0)
        {
          if (pars->name.GetLength() > 0) _soundEquip = GSoundScene->Open(pars->name, true, true, pars->distance);
        }

        if (_soundEquip)
        {
          float volume = GRandGen.RandomValue() * 0.4f + 0.8f;
          float frequency = GRandGen.RandomValue() * 0.2f + 0.9f;

          if (!inside) volume *= 0.5;
          _soundEquip->SetVolume(pars->vol * volume, pars->freq * frequency);
          _soundEquip->SetObstruction(obstruction, occlusion);
          _soundEquip->SetPosition(centerPos, VZero);
          _soundEquip->Repeat(1);
          _soundEquip->Restart();
        }
      }
    }

    // primary gear
    // play sound when some weapon is on shoulder (this is primary weapon) 
    bool hasPrimary = GetInSlot(ISShoulder) != NULL;
    
    if (hasPrimary)
    {
      const SoundPars *pars = NULL;
      pars = type->GetMunitionSoundRandom("primary", _soundStepOverride);

      if (pars && GSoundScene->CanBeAudible(centerPos, pars->distance))
      {
        if (!_soundMunitPrimary || stricmp(pars->name, _soundMunitPrimary->Name()) != 0)
        {
          if (pars->name.GetLength() > 0) _soundMunitPrimary = GSoundScene->Open(pars->name, true, true, pars->distance);
        }

        if (_soundMunitPrimary)
        {
          float volume = GRandGen.RandomValue() * 0.4f + 0.8f;
          float frequency = GRandGen.RandomValue() * 0.2f + 0.9f;
          
          if (!inside) volume *= 0.5;
          _soundMunitPrimary->SetVolume(pars->vol * volume, pars->freq * frequency);
          _soundMunitPrimary->SetObstruction(obstruction, occlusion);
          _soundMunitPrimary->SetPosition(centerPos, VZero);
          _soundMunitPrimary->Repeat(1);
          _soundMunitPrimary->Restart();
        }
      }
    }
  }

  // collision with vegetation
  if (_vegCollisionTime + 0.5 > Glob.time)
  {
    if (_vegSound.IsNull())
    {
      const SoundPars &pars = type->_vegSound.SelectSound(GRandGen.RandomValue());

      if (pars.name.GetLength() > 0 && GSoundScene->CanBeAudible(pos, pars.distance))
      {
        _vegSound = GSoundScene->OpenAndPlay(pars.name, this, false, pos, speed, pars.vol, pars.freq, pars.distance);
      }
    }
    if (_vegSound)
    {
      // fade out volume
      _vegSound->SetPosition(pos, speed);
    }
  }
  else
  {
    _vegSound.Free();
  }
  
  // the test is done because that way we do not have to query ground height, we can use _landContactPos
  if (_doSoundStep)
  {
    // _landContactPos corresponds to Future, we need to compensate for the VS used here
    const Vector3 &stepPos = _landContact ? _landContactPos-FutureVisualStateRaw().Position()+vs.Position() : WorldPosition(vs);

    if (inside || GSoundScene->CanBeAudible(WaveEffect, stepPos, maxVol))
    {
      InventoryItem const * const shoes = _inventorySlots[ISFeet];
      const char * feetItem = shoes ? "someshoes" : "";
      const SoundPars &pars = type->GetEnvSoundExtRandom(_surfaceSound, _soundStepOverride, feetItem);

      if (!_soundStep || stricmp(pars.name, _soundStep->Name()) != 0)
      { 
        // load sound if necessary
        if ((pars.name.GetLength() > 0) && GSoundScene->CanBeAudible(stepPos, pars.distance))
        {         
          _soundStep = GSoundScene->Open(pars.name, true, true, pars.distance);
        }
        else
          _soundStep.Free();
      }
      if (_soundStep)
      {
        float volume = GRandGen.RandomValue() * 0.4f + 0.8f;
        float frequency = GRandGen.RandomValue() * 0.2f + 0.9f;

        if (!inside) volume *= 0.5;
        _soundStep->SetVolume(pars.vol * volume, pars.freq * frequency);
        _soundStep->SetObstruction(obstruction,occlusion);
        _soundStep->SetPosition(stepPos, VZero);
        _soundStep->Repeat(1);
        _soundStep->Restart();
      }
    }

    _doSoundStep=false;
    _soundStepOverride = RStringBEmpty;
  }

  Vector3 absSoundPos = pos;
  if (inside)
  {
    Vector3 relSoundPos(0,0.2f,0.3f);
    absSoundPos = EyePosition(vs);
    absSoundPos += GScene->GetCamera()->DirectionModelToWorld(relSoundPos);
  }

  // no breathing if person is death, if swimming or damage screaming
  if (_isDead || _waterContact || (_soundDamage && !_soundDamage->IsStopped()))
  {
    _soundBreath.Free();
  }
  else
  {
    float tmp = floatMax(_tired, (1-_breathAir)*(1-_holdBreath));
    tmp = InterpolativC(tmp, 0.2, 1, 0, 1);
    const SoundPars &pars = type->GetBreathSound(_breathSelect, tmp);
    float vol = pars.vol * tmp;
    if (!inside) vol *= 0.5;
    if (tmp > 0.001 && (pars.name.GetLength() > 0) && (inside || GSoundScene->CanBeAudible(absSoundPos, pars.distance)))
    {
      // wait until sound is played
      if (_soundBreath && _soundBreath->IsStopped())
      {
        // change breath sound
        if (strcmpi(cc_cast(pars.name), cc_cast(_soundBreath->Name())) != 0) 
        {
          _soundBreath.Unload(), _soundBreath.Free();
        }
        else
          _soundBreath->Restart();
      }

      if (!_soundBreath)
        _soundBreath = GSoundScene->OpenAndPlayOnce(pars.name, this, false, absSoundPos, speed, vol, pars.freq, pars.distance);

      if (_soundBreath)
      {
        _soundBreath->SetVolume(vol, 1.0f); // volume, frequency
        _soundBreath->SetObstruction(obstruction, occlusion, deltaT);
        _soundBreath->SetPosition(absSoundPos, speed);
      }
    }
    else
    {
      _soundBreath.Free();
    }
  }

#endif

  UpdateSpeechPosition(inside,pos,speed,deltaT);
}

/**
@param we are currently processing the crew of the vehicle the camera is in

\patch 5127 Date 2/7/2007 by Ondra
- Fixed: Better sound occlusion calculation for vehicle crews.
*/

void Man::UpdateSpeechPosition(bool inside, Vector3Par pos, Vector3Par speed, float deltaT)
{
  ObstructionCached obs;
  if (_directSpeakingSound)
  {
    float obstruction = 1, occlusion = 1;
    if (!inside)
    {
      obs.CheckSoundObstruction(this,false,obstruction,occlusion);
    }
    _directSpeakingSound->SetPosition(pos, speed);
    _directSpeakingSound->SetObstruction(obstruction,occlusion,deltaT);
  }

  if (_speakingSound3D)
  {
    float obstruction = 1, occlusion = 1;
    if (!inside)
    {
      obs.CheckSoundObstruction(this,false,obstruction,occlusion);
    }
    _speakingSound3D->SetPosition(pos, speed);
    _speakingSound3D->SetObstruction(obstruction,occlusion,deltaT);
  }
  if (IsRemotePlayer())
  {
    //VoNSystem needs to know it in order to sort and select voices (relative volume estimation)
    int dpnid = GetRemotePlayer();
    if (dpnid == GetNetworkManager().GetPlayer())
    {
      if (QIsManual() && GWorld->CameraOn()==this && !GWorld->GetCameraEffect()) 
      { //when camera is not on player, do not set VoN to his position! (this also eliminates _dead players pos mischmasch)
        Camera *cam = GScene->GetCamera();
        if (cam) GetNetworkManager().SetVoNPlayerPosition(cam->Position(), speed);
      }
    }
    else
    {
      bool isAlive = (Brain() && (Brain()->GetLifeState() != LifeStateDead) );
      if ( (dpnid != AI_PLAYER) && isAlive )
      {
        float obstruction = 1, occlusion = 1;
        if (GWorld->GetRealPlayer() && GWorld->GetRealPlayer()->Brain() && 
            (GWorld->GetRealPlayer()->Brain()->GetLifeState() != LifeStateDead) ) // no obstruction or occlusion is set when listener is dead
          obs.CheckSoundObstruction(this,inside,obstruction,occlusion);
        Vector3 camPos = pos;
        Vector3 camSpeed = speed;
        GWorld->UpdateCameraPosition(camPos, camSpeed, dpnid); // take the camera position instead of pos when defined
        GetNetworkManager().SetVoNPosition(dpnid, camPos, camSpeed);
        GetNetworkManager().SetVoNObstruction(dpnid, obstruction,occlusion,deltaT);
        float accomodation = GSoundScene->GetEarAccomodation();
        //float volume = GSoundsys->GetLoudness(WaveEffect, pos, /*volume*/1.0f, accomodation);
        float volume = 1.0f;
        GetNetworkManager().UpdateVoNVolumeAndAccomodation(dpnid, volume, accomodation);
        //LogF("Position [%.2, %.2, %.2], acc=%f", pos.X(), pos.Y(), pos.Z(), accomodation);
      }
    }
  }
  else if (QIsManual() && GWorld->CameraOn()==this && !GWorld->GetCameraEffect()) 
  { //when camera is not on player, do not set VoN to his position! (this also eliminates _dead players pos mischmasch)
    Camera *cam = GScene->GetCamera();
    if (cam) GetNetworkManager().SetVoNPlayerPosition(cam->Position(), speed);
  }
#if _ENABLE_CHEATS
  else
  { // update speech position of AI for VoNSay purposes
    if (GetVoNSayDpnid())
      GetNetworkManager().SetVoNPosition(GetVoNSayDpnid(), pos, speed);
  }
#endif

  base::Sound(inside,deltaT);
}

bool Man::SoundInTransportNeeded() const
{
  if (_speakingSound3D) return true;
  if (
    IsRemotePlayer() && !QIsManual() && GetRemotePlayer()!=AI_PLAYER 
  ) return true;
  // local player should always update her speech position at least
  if (!IsRemotePlayer() && QIsManual())
    return true;

  if (_directSpeakingSound) return true;
  return false;
}

void Man::SoundInTransport(Transport *parent, Matrix4Par worldPos, float deltaT)
{
  // TODO: real mouth position
  Vector3Val pos = worldPos.Position();
  // TODO: include relative soldier speed
  const TransportVisualState &vs = parent->RenderVisualState();
  Vector3Val speed = vs.Speed();
  // we do not need inside here
  // camera is inside of the Transport, but this does not mean it is inside of the Man
  UpdateSpeechPosition(false,pos,speed,deltaT);
}

void Man::UnloadSound()
{
  _soundEnv.Free();
  _soundBreath.Free();
  _soundStep.Free();
  _soundEquip.Free();
  _soundMunitPrimary.Free();
  _soundMunitSecondary.Free();
  _soundDamage.Free();
  _vegSound.Free();
  base::UnloadSound();
}

/*!
\patch 1.75 Date 2/13/2002 by Jirka
- Added: Blood slops under dead bodies
*/

void Man::Destroy( EntityAI *owner, float overkill, float minExp, float maxExp )
{
  // check: man can never explode
  base::Destroy(owner,overkill,minExp,maxExp);
  
  if( _brain ) KilledBy(owner);
}

void Man::Scream(EntityAI *killer)
{
  const ManType* type = Type();

  if ((type->_damageHitSound.Size() > 0) && (GetHit(type->_headHit) <= 0.9f) && (_whenScreamed<Glob.time-3 || _whenScreamed>Glob.time))
  {
    Assert((_damageHitSelect >= 0) && (_damageHitSelect < type->_damageHitSound.Size()));

    // select sound
    const SoundPars* pars = type->_damageHitSound[_damageHitSelect].SelectHitSound(GRandGen.RandomValue());

    if (pars)
    {
      _whenScreamed = Glob.time;
      Vector3Val pos=WorldPosition(FutureVisualState());
      float rndFreq = GRandGen.RandomValue() * 0.1f + 0.95f;
      float vol = pars->vol;
      bool isOut = IsInLandscape();
      if (!isOut) vol *= 0.2f;

      AbstractWave *sound = GSoundScene->OpenAndPlayOnce(
        pars->name, this, false, pos, WorldSpeed(), vol, pars->freq * rndFreq, pars->distance);

      if (sound)
      {
        sound->Set3D(true);
        GSoundScene->SimulateSpeedOfSound(sound);
        GSoundScene->AddSound(sound);
      }
      // if we scream, somebody may hear it
      if (isOut && killer)
      {
        // disclose only if not in any vehicle
        GLandscape->Disclose(DCScream,killer,pos,80,false,false);
      }
    }
  }
}

#define SHOW_DAMAGE_MIN 0.55f
#define SHOW_DAMAGE_MAX 2.50f

void Man::DoDamage(
  DoDamageResult &result, Vector3Par pos, float val, float valRange, RString ammo, Vector3Par originDir, bool ignoreSmall
)
{

  // only one hit a time can be shown - select the most important one
  if (!IsDamageDestroyed() && val>SHOW_DAMAGE_MIN)
  {

    AIBrain *unit = GWorld->FocusOn();
    if (unit != NULL && unit == Brain())
    {
      // camera shake
      if (GWorld) 
        GWorld->GetCameraShakeManager().AddSource(CameraShakeParams(CameraShakeSource::CSPlayerHit, pos, ammo));
    }

    if (_whenHit<Glob.time-1.0f || val>_howMuchHit)
    {
      _whenHit = Glob.time;
      _howMuchHit = val;
      Assert(val>=0);

      if (unit != NULL && unit == Brain())
      {
        AutoArray< float, MemAllocLocal<float, 16> > pars;
        pars.Realloc(2);
        pars.Resize(2);

        pars[0] = 1; // time in sec
        pars[1] = _howMuchHit; 

        if (GEngine) GEngine->SetSpecialEffectPars(0, pars.Data(), 2);
      }

      if (GScene && GScene->GetCamera() && this==GWorld->CameraOn())
      {
        _hitDirection = GScene->GetCamera()->GetInvTransform().Rotate(originDir);
      }
      else
      {
        _hitDirection = VZero;
      }
    }
  }
  base::DoDamage(result,pos,val,valRange,ammo,originDir,ignoreSmall);
#if _VBS3
  // Morale system - inform the squad mates of the injured character.
  InformSubgroupCasualty();
#endif
}

void Man::DirectHit(const HitInfo& hitInfo, const SkinnedPosition& skinning)
{
  float power = hitInfo._ammoType->_affectSkeleton;
  if(power > 0.01f)
  {
    ProtectedVisualState<VisualState> vs = FutureVisualStateScope();
    Vector3 lc = GetLeaningCenterMoves(vs);
    Vector3 hc = GetHeadCenterMoves(vs);
    Vector3 up = (hc - lc);
    
    _hitSkeletonRotAxis = (FutureVisualState().WorldToModel() * hitInfo._pos);
    if(up.DotProduct(_hitSkeletonRotAxis - lc) < 0.01f) //pod pasem
    {
      return;
    }
    else if(up.DotProduct(_hitSkeletonRotAxis - hc) < 0.01f)  //pod hlavou
    {
      _headHitOnly = false;
    }
    else
    {
      _headHitOnly = true;
    }

    //laying
    up.Normalize();
    if(up.Y() < 0.2f)
    {
      _hitSkeletonRotAxis = FutureVisualState().WorldToModel() * Vector3(0.f, 1.f, 0.f);
      _hitSkeletonRotWanted = 0.2f * power;
      return;
    }
    
    //osa rotace
    _hitSkeletonRotAxis = up.CrossProduct(_hitSkeletonRotAxis);
    _hitSkeletonRotAxis.Normalize();

    //vyklon
    if(fabs(_hitSkeletonRotAxis.X()) + fabs(_hitSkeletonRotAxis.Z()) > 0.01f)
      _hitSkeletonRotWanted = power;
  }
}

/*!
  \patch 5092 Date 11/29/2006 by Ondra
  - Fixed: Improved AI reaction to soldier being hit nearby.
*/

void Man::ShowDamage(int part, EntityAI *killer)
{
  AIBrain *brain = Brain();
  bool wasAlive = brain && brain->LSIsAlive();
  if (wasAlive)
  {
    Scream(killer);
  }
  base::ShowDamage(part,killer);
}

void Man::HitBy(EntityAI *owner, float howMuch, RString ammo, bool wasDestroyed)
{
  base::HitBy(owner,howMuch, ammo, wasDestroyed);

  ReactToDamage();
}

RString Man::DiagText() const
{
  char buffer[256];
  sprintf(buffer,"spdw %.1f ",_walkSpeedWantedZ*3.6f);
  return RString(buffer)+base::DiagText();
}

#define DRAW_OBJ(obj) GScene->DrawObject(obj)

void Man::DiagCover(CoverVars *cover, RString text, ColorVal coverColor, ColorVal textColor)
{
  {
    Ref<Object> obj = new ObjectColored(GScene->Preloaded(SphereModel), VISITOR_NO_ID);
    Color color = HBlack;
    switch (cover->_type)
    {
    case CoverRightCornerHigh:
    case CoverRightCornerMiddle:
    case CoverRightCornerLow: 
      color = Color(0,1,0,0.5);
      break;
    case CoverLeftCornerHigh:
    case CoverLeftCornerMiddle:
    case CoverLeftCornerLow:
      color = Color(1,1,0,0.5);
      break;
    case CoverEdgeLow:
    case CoverEdgeMiddle:
      color = Color(0,0,1,0.5);
      break;
    case CoverWall:
      color = Color(0.5,0.5,0,0.5);
      break;
    }
    obj->SetPosition(cover->_pos);
    obj->SetScale(0.3f);
    obj->SetConstantColor(color);
    DRAW_OBJ(obj);
  }
  
  Vector3 minmax[2];
  minmax[0] = Vector3(-cover->_areaFreeLeft,0.0f,-cover->_areaFreeBack);
  minmax[1] = Vector3(+cover->_areaFreeRight,2.0f,0);

  GScene->DiagBBox(minmax,cover->_coverPos,coverColor,true);


  RString fullText = GetDebugName() + " " + FindEnumName(cover->_type)+" "+text;
  GScene->DrawDiagText(fullText,cover->_pos+VUp*0.5f,20,1,textColor);

  {
    Ref<Object> obj = new ObjectColored(GScene->Preloaded(SphereModel), VISITOR_NO_ID);
    obj->SetPosition(cover->_entry);
    obj->SetScale(0.2f);
    obj->SetConstantColor(coverColor*0.5f);
    DRAW_OBJ(obj);
  }

}

////////////////
#ifdef TEST_JANA

const int gRagdollBoneCnt = 17;
const SkeletonIndex gRagdollBones[gRagdollBoneCnt] = {0, 4, 65, 71, 7, 13, 39, 17, 21, 43, 47, 67, 69, 70, 73, 75, 76};
const SkeletonIndex gRagdollBonesParent[gRagdollBoneCnt] = {-1, 0, 0, 0, 4, 4, 4, 13, 17, 39, 43, 65, 67, 69, 71, 73, 75};

#endif
/////////////////
#include "Physics\enf_physicsbullet.h"
#include "LinearMath\btTransform.h"
#include "Physics\enf_btHeightfieldTerrainShape.h"
#include "BulletCollision/CollisionDispatch/btCollisionObject.h"
#include "BulletDynamics/Dynamics/btRigidBody.h"
#include "BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h"
#include "BulletCollision/CollisionDispatch/btCollisionWorld.h"
#include "baseWorld/enf_shapes.h"

#if _ENABLE_PHYSICS
void Soldier::PhysDiagDraw()
{
  const VisualState &vs = RenderVisualState();
  Matrix4 trans = vs.ModelToWorld();
      
  if (this == GLOB_WORLD->GetRealPlayer())
  {
		enf::BulletWorld* bw = (enf::BulletWorld*)GLandscape->GetPhysicsWorld();

#if USER_jana
		//kostricka?
		//////////////////////
		//cela originalni kostra
		Skeleton* skeleton = GetShape()->GetSkeleton();
		Array<Vector3> pivArr = skeleton->GetPivots();
    
    const VisualState &vs = RenderVisualState();  //ktery je lepsi a proc?
    Matrix4 trans = vs.ModelToWorld();
    
    static Vector3 shift = Vector3(1.0f, 2.f, 0.f);//1.5f * trans.DirectionAside();

		/*
		if(_ragdollCtrl && (vs._ragdollMx.Size() == _ragdollCtrl->GetNumBones()))
		{
			for(int i = 0; i < vs._ragdollMx.Size(); ++i)
			{
				Matrix4 mx = trans * vs._ragdollMx.Get(i);
				Vector3 pos = mx.Position() + shift;

				enf::Shape::CreateSphere(pos, 0.02f, 0xffff0000, enf::SS_ONCE|enf::SS_VISIBLE);
			}
		}
		*/	

		Matrix4Array mat;
		PrepareShapeDrawMatrices(mat, RenderVisualState(), GetShape(), 0);//PrepareRagdollMatrices();

		//dlan
		Matrix4 palm;
		GetPalmMatrix(FutureVisualState(), palm);
		Matrix4 palm2 = trans * palm;
		enf::Shape::CreateSphere(palm2.Position(), 0.02f, 0xff0000ff, enf::SS_ONCE|enf::SS_VISIBLE);

		/*
		//Vector3 rp[17];
		for(SkeletonIndex boneIdx = 0; boneIdx < mat.Size(); ++boneIdx)
    {
			//ragdoll bones only
			if(boneIdx != 0 && boneIdx != 36 && boneIdx != 41 && boneIdx != 86 && boneIdx != 90 && boneIdx != 94 && boneIdx != 113 && boneIdx != 117 && boneIdx != 121 && boneIdx != 139 && boneIdx != 143 && boneIdx != 145 && boneIdx != 146 && boneIdx != 147 && boneIdx != 151 && boneIdx != 153 && boneIdx != 154)
				continue;

      SkeletonIndex parentIdx = GetShape()->GetSkeletonSource()->GetParent(boneIdx);

      SkeletonIndex si = GetSkeletonIndex(boneIdx); //je to opravdu nutny?
      SkeletonIndex pi = GetSkeletonIndex(parentIdx);

			if(si >= 0 && si < skeleton->NBones())//mat.Size())
      {
				//pro vizualizaci kostry odstranim posun ve smeru
				mat[si].Orthogonalize();
				Vector3 norm = mat[si].Direction().Normalized();
				mat[si].SetDirection(mat[si].Direction() - norm);
				Matrix4 mx = trans * mat[si];
				Vector3 pos = mx * pivArr.Get(si);
				//Vector3 pos = trans * mx.Position();// * pivArr.Get(si);
        pos = pos + shift;
        
				unsigned int color = 0xffff00ff;
				if(_ragdollCtrl && _ragdollCtrl->FindBoneByNodeIndex(si))
					color = 0xffff0000;
				else if(_ragdollCtrl)
					;//continue;
				enf::Shape::CreateSphere(pos, 0.02f, color, enf::SS_ONCE|enf::SS_VISIBLE);

        if(pi >= 0)
        {
					Matrix4 mxp = trans * mat[pi];
          mxp.SetDirection(Vector3(0.f, 0.f, 0.f));
					Vector3 ppos = mxp * pivArr.Get(pi);
					//Vector3 ppos = trans * mxp.Position();// * pivArr.Get(pi);
          ppos = ppos + shift;
					
          enf::Shape::CreateLine(ppos, pos, 0xffff77ff, enf::SS_ONCE|enf::SS_VISIBLE);
        }
			}
		}
		*/
		//////////////////////
#endif

    ////paprsek ze zbrane - protni trojuhelnik zeme
    /*
		const TurretContext tc;
    const WeaponModePtr wmode = _weaponsState.ValidatedCurrentWeapon();
        
    Vector3Val dir = GetAimCursorDirection(tc, wmode);

    WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
    Vector3 lPos = FutureVisualState().PositionModelToWorld(GetWeaponPoint(FutureVisualState(), tc, wmode));//vs.Position() + vs.DirectionModelToWorld(mx.Position());
    Vector3 lDir = GetAimCursorDirection(tc, wmode);//Vector3(0.f, 0.f, 0.f) - vs.DirectionModelToWorld(mx.DirectionAside());  //ne neptejte se proc, ale je to spravnej smer..
    Vector3 lEnd = lPos + lDir*50.f;
    lPos = lPos + lDir * 1.5f;
      
    btVector3 start = enf::BulletWorld::Point2btVector(lPos);
    btVector3 end = enf::BulletWorld::Point2btVector(lEnd);
    btCollisionWorld::ClosestRayResultCallback rayCallback(start, end);

    // Perform raycast
    bw->GetBulletWorld()->rayTest(start, end, rayCallback);
    if(rayCallback.hasHit()) 
    {
      end = rayCallback.m_hitPointWorld;
      lEnd = enf::BulletWorld::btVector2Point(end);
          
      enf::Shape::CreateLine(lPos, lEnd, 0xffff0000, enf::SS_ONCE);
      enf::Shape::CreateSphere(lEnd, 0.1f, 0xffff0000, enf::SS_ONCE);
    }
		*/
	}
}
#endif

void Soldier::DrawDiags()
{
  if (CHECK_DIAG(DECombat) && _brain)
  {
    CoverVars *pathCover =_brain->GetPath().GetCover()._payload;
    if (_cover)
    {
      RString state = FindEnumName(_coverState);
      if (pathCover==_cover) state = state + " Path";
      DiagCover(_cover,state,Color(0,0,1),Color(1,1,0));
    }
    if (pathCover && pathCover!=_cover)
    {
      DiagCover(pathCover,"Path",Color(0,1,0),Color(1,1,0));
    }
  }

#if _ENABLE_PHYSICS
  if (CHECK_DIAG(DEPhys))
    PhysDiagDraw();
#endif

  base::DrawDiags();
}

int Man::GetProxyComplexity(int level, const Matrix4 &pos, float dist2) const
{
  int nFaces = base::GetProxyComplexity(level,pos,dist2);
  // TODO: calculate weapons
  return nFaces;
}

/// Info needed for drawing, preloading and other manipulation with the proxy
struct Man::ProxyInfo
{
  Object *object;
  Object *parentObject;
  
  /// during drawing, we need a temporary unique object to store a proxy state - when set, object == uniqueObject
  Ref<Object> uniqueObject;
};

/*!
\patch 5123 Date 1/26/2007 by Ondra
- Fixed: Binocular style laser designators were using wrong models and animations.
*/

const float ManHitLimit = 0.7f;
const float ManHitLimitHead = 0.9f;
const float ManHitLimitBody = 0.9f;

static inline int WoundLevel(float cont, float limit)
{
  if (Glob.config.bloodLevel>1 && cont>=limit) return 2;
  if (cont>=limit*0.5) return 1;
  return 0;
}

#include "helicopter.hpp"

static inline bool IsInSlot(Weapon *weapon, int maskInclude, int maskExclude = 0)
{
  const WeaponType *type = weapon->Type();
  return type && (type->_weaponType & maskInclude) != 0 && (type->_weaponType & maskExclude) == 0;
}

void Man::GetProxyInfo(ProxyInfo &info, const ProxyObjectTyped &proxy, bool createUniqueObject) const
{
  info.object = NULL;
  info.parentObject = NULL;
  info.uniqueObject = NULL;

  // Retrieve the proxy object
  Object *proxyObj = proxy.obj;
  if (!proxyObj) return;
  // Retrieve the proxy type
  const EntityType *proxyType = proxyObj->GetEntityType();
  // skip proxies which are handled by a parent
  if (!proxyType || !strcmp(proxyType->_simName,"alwaysshow"))
  {
    return;
  }

#if _ENABLE_NEWHEAD

  // Get head (in case it is a subpart)
  {
    // If subpart then chose the shape and return
    if (proxyType && strcmp(proxyType->_simName, "ProxySubpart") == 0)
    {
      HeadObject *model = (_headType.NotNull()) ? _headType->_model : NULL;
      if (!createUniqueObject)
        info.object = model;
      else if (model)
      {
        // unique copy need to be created
        LODShape *lodShape = model->GetShape();
        const HeadType *type = model->Type();
        
        HeadObject *head = new HeadObject(type, lodShape, VISITOR_NO_ID);
        info.uniqueObject = head;        

        info.object = head;
        // Set correct face to the head
        head->_faceMaterial = _faceMaterial;
        head->_faceTexture = _faceTexture;
        head->_faceTextureWounded[0] = _faceTextureWounded[0];
        head->_faceTextureWounded[1] = _faceTextureWounded[1];
        head->_faceTextureOrig = Type()->_faceTextureOrig;
        head->_headWound = WoundLevel(GetHitContRaw(Type()->_headHit), ManHitLimitHead);
      }

      info.parentObject = const_cast<Man*> (this);
      return;
    }
  }

#endif

  Weapon *weaponInHands = NULL;
  if (_itemInHands) weaponInHands = _itemInHands->GetWeapon();

  Weapon *weapon = NULL;

  // inventory proxies
  InventorySlotId slot = proxyType->GetInventorySlot();
  if (slot >= 0)
  {
    InventoryItem *item = NULL;
    if (slot == ProxyInventoryType::_hands)
      item = _itemInHands;
    else if (slot < NInventorySlotId)
    {
      item = _inventorySlots[slot];
      if (item == _itemInHands) item = NULL; // avoid drawing item in hands twice
      
      if (item == NULL) item = _emptySlots[slot]; // default for empty slot
    }

    if (item)
    {
      info.object = item;
      weapon = item->GetWeapon();
    }
  }

  if (!info.object) return;

  // if the item is using the same skeleton, use my pose to animate it  
  LODShape *shape = info.object->GetShape();
  if (shape && GetShape() && shape->GetSkeletonSource() == GetShape()->GetSkeletonSource())
    info.parentObject = unconst_cast(this);

  // weapon animation
  if (weapon)
  {
    weapon->UpdateShape();

    int phaseFire = -1;
    const WeaponType *type = weapon->Type();
    if (type && type == _mGunFireWeapon && _weaponsState.ShowFire())
    {
      phaseFire = _weaponsState._mGunFirePhase;
    }
    weapon->SetAnimPhase(phaseFire);
  }
}

InventoryItem *Man::ToDrawInSlot(InventorySlotId slot)
{
  InventoryItem *item = base::ToDrawInSlot(slot);
  return item ? item : _emptySlots[slot]; // pass correct model for empty slot
}

HeadObject *Man::CreateHead() const
{
  if (!_headType) return NULL;

  const HeadObject *model = _headType->_model;
  if (!model) return NULL;

  // unique copy need to be created
  LODShape *lodShape = model->GetShape();
  const HeadType *type = model->Type();
        
  HeadObject *head = new HeadObject(type, lodShape, VISITOR_NO_ID);

  // Set correct face to the head
  head->_faceMaterial = _faceMaterial;
  head->_faceTexture = _faceTexture;
  head->_faceTextureWounded[0] = _faceTextureWounded[0];
  head->_faceTextureWounded[1] = _faceTextureWounded[1];
  head->_faceTextureOrig = Type()->_faceTextureOrig;
  head->_headWound = WoundLevel(GetHitContRaw(Type()->_headHit), ManHitLimitHead);

  return head;
}

void Man::ReleaseProxyInfo(ProxyInfo &info, bool rejected) const
{
  if (info.uniqueObject)
  {
    // unless rejected, it must be stored in GScene->_proxyObjects
    Assert(info.uniqueObject->RefCounter() > (rejected ? 0 : 1));
    info.uniqueObject = NULL;
  }
}

/*!
\patch 1.05 Date 7/17/2001 by Ondra.
- Fixed: Weapon shadows now reflect actual weapon held.
\patch_internal 1.05 Date 7/17/2001 by Ondra.
- New: GetProxy is now returning shape
*/

Object *Man::GetProxy
(
  VisualStateAge age, 
  LODShape *&shape, Object *&parentObject,
  int level, Matrix4 &transform, const Matrix4 &parentPos, int i
) const
{
  const ManType *type = Type();
  const ProxyObjectTyped &proxy = type->GetProxyForLevel(level, i);

  ProxyInfo info;
  GetProxyInfo(info, level, i);

  if (!info.object) return NULL;

  if (info.parentObject)
  {
    shape = info.object->GetShape();
    parentObject = info.parentObject;
    transform = parentPos * proxy.GetTransform();
    return info.object;
  }

  SkeletonIndex matIndex = Type()->GetProxyBoneForLevel(level,i);
  if (matIndex != SkeletonIndexNone)
  {
    Matrix4 animTransform = AnimateProxyMatrix(GetVisualStateByAge(age), level,proxy.GetTransform(),matIndex);

    animTransform.Orthogonalize();
    // TODO: smart clipping
    transform=parentPos*animTransform;
  }
  else
  {
    transform = parentPos * proxy.GetTransform();
  }

  shape = info.object->GetShape();
  parentObject = info.object;

  // if shape is reversed, reverse orientation
  if( shape && (shape->Remarks()&ShapeReversed) )
  {
    Matrix4 swapM(MScale,-1,+1,-1);
    transform = transform*swapM;
  }
  DoAssert(transform.IsFinite());
  return info.object;
}

float Man::ForceVerticalFlag() const
{
  if (!_isDead || _whenKilled==Time(0)) return 0;
  float deadTime = Glob.time-_whenKilled;
  const float interpolTime = 2.0f;
  if (deadTime>=interpolTime) return 1;
  if (deadTime<=0) return 0;
  return deadTime/interpolTime;
}

bool Man::NeedsPrepareProxiesForDrawing() const
{
  return true;
}

struct VONDiagsHelper
{
  bool _init;
  VONDiagsHelper()
  {
    _init = false;
  }
  Ref<ObjectColored> _colored[2][2][2];
  void Init()
  {
    if (_init) return;
    _init = true;
    for (int r=0; r<2; r++) for (int g=0; g<2; g++) for (int b=0; b<2; b++)
    {
      _colored[r][g][b] = new ObjectColored(GScene->Preloaded(SphereModel),CreateObjectId());
      _colored[r][g][b]->SetConstantColor(Color(r,g,b));
    }
  }
  void Clear()
  {
    for (int r=0; r<2; r++) for (int g=0; g<2; g++) for (int b=0; b<2; b++)
    {
      _colored[r][g][b].Free();
    }
    
  }
};

static VONDiagsHelper VONDiags;

void Man::PrepareProxiesForDrawing(
  Object *rootObj, int rootLevel, const AnimationContext &animContext,
  const PositionRender &transform, int level, ClipFlags clipFlags, SortObject *so
)
{
//((TO_BE_USED_IN_FINAL //REMOVE COMMENT!!
#if 0
//))TO_BE_USED_IN_FINAL
  // Toggle VoN Bubbles rendering (cheat}
  static bool showVoNBubbles = false;
  if (GInput.CheatActivated() == CheatShowVoNBubbles)
  {
    GInput.CheatServed();
    showVoNBubbles = !showVoNBubbles; //toggle
  }
  if (showVoNBubbles)
  {
    int vonR = 0;
    int vonG = 0;
    int vonB = 0;
    if (this==GWorld->GetRealPlayer())
    {
    }
    else if (IsRemotePlayer())
    {
      if (GetNetworkManager().IsVoicePlaying(GetRemotePlayer()))
      {
        vonB = 1;
      }
    }
#if _ENABLE_CHEATS
    else
    { // Bubble for AI speaking by VoNSay
      if (GetNetworkManager().IsVoicePlaying(GetVoNSayDpnid()))
      {
        vonB = 1;
      }
    }
#endif

    bool audible2D = false,audible3D = false;
    GetNetworkManager().IsVoice2D3DPlaying(GetRemotePlayer(), audible2D, audible3D);
    if (audible2D) vonG = 1;
    if (audible3D) vonR = 1;

  
    // if any indication is required, indicate
    if (vonR+vonG+vonB>0)
    {
      Matrix4 pTransform=transform;
      pTransform.SetPosition(pTransform.Position()+Vector3(0,3,0));
      pTransform.SetScale(0.3);
      VONDiags.Init();
      Ref<Object> obj = VONDiags._colored[vonR][vonG][vonB];
      //GScene->ProxyForDrawing(obj,0,clipFlags,so->distance2,pTransform);
      GScene->ProxyForDrawing(obj,rootObj,0,rootLevel,clipFlags,so->distance2,pTransform);
    }
  }
//((TO_BE_USED_IN_FINAL //REMOVE COMMENT!!
#endif
//))TO_BE_USED_IN_FINAL

  // no proxy drawing from optics
  float transition;
  if (!CanDrawModel(transition)) return;

  // inside vehicles, some proxies are hidden
  Object *parent = GetHierarchyParent();
  if (parent)
  {
    // check if head need to be hidden
    bool insidePlayer = false;
    CameraType camType = GWorld->GetCameraType();
    if (camType != CamExternal && camType != CamGroup)
    {
      if (!GWorld->GetCameraEffect())
      {
        AIBrain *unit = GWorld->FocusOn();
        Person *player = unit ? unit->GetPerson() : NULL;
        insidePlayer = this == player;
      }
    }

    // check if weapons need to be hidden
    bool hideWeapons = true;
    bool showSecondaryWeapon = false;
    {
      Transport *vehicle = dyn_cast<Transport>(parent);
      if (vehicle)
      {
        const TransportType *type = vehicle->Type();
        if (this == vehicle->Driver()) hideWeapons = type->_hideWeaponsDriver;
        else
        {
          TurretContext context;
          if (vehicle->FindTurret(this, context) && context._turretType) hideWeapons = context._turretType->_hideWeaponsGunner;
          else hideWeapons = type->_hideWeaponsCargo;
        }
      }
    }

    // hide what needed
    ShowWeapons(!hideWeapons, showSecondaryWeapon);
    ShowHead(!insidePlayer);
    ShowFlag(false);
  }

  float dist2 = so->distance2;
  // prepare muzzle flashes (and other weapon parts?) for later drawing
  const ManType *type = Type();
  ShapeUsed lock = _shape->Level(level);
  for( int i=0; i<type->GetProxyCountForLevel(level); i++ )
  {
    const ProxyObjectTyped &proxy = type->GetProxyForLevel(level, i);
    bool insideView = level == type->_insideView;
    
    // createObjectUnique need to be passed to GetProxyInfo to be sure the object is unique for each proxy instance
    ProxyInfo info;
    GetProxyInfo(info, proxy, true);

    Object *object = info.object;
    if (!object)
    {
      ReleaseProxyInfo(info,true);
      continue;
    }
    LODShape *pshape = object->GetShape();
    if (!pshape)
    {
      ReleaseProxyInfo(info,true);
      continue;
    }

    // LOD detection
    // if soldier cockpit is used, use also weapon cockpit
    int pLevel = -1;
    if (insideView)
    {
      // find inside view
      pLevel = pshape->FindSpecLevel(VIEW_PILOT);
      // if there is no inside view, use the best LOD (if there is any)
      if (pLevel<0 && pshape->FindSimplestLevel()>=0) pLevel = 0;
    }
    if (pLevel<0)
    {
      // TODO: propagate Scale2 instead of Scale() all way down from here
      pLevel=GScene->LevelFromDistance2(pshape,dist2,transform.position.Scale());
    }
    // disappering head hotfix (news:i7v7sa$329$1@new-server.localdomain)
    if (pLevel==LOD_INVISIBLE && info.object->GetFeatureSize()>0)
    {
      pLevel = pshape->FindSimplestLevel();
    }

    if (pLevel == LOD_INVISIBLE)
    {
      ReleaseProxyInfo(info,true);
      continue;
    }

    PositionRender pTransform;
    if (info.parentObject)
    {
      pTransform = transform;

      // registration for drawing
      // transform cannot be used for clipping, so avoid it at all (pass clipFlags 0)
      if (info.uniqueObject)
        GScene->TempProxyForDrawing(info.object, rootObj, pLevel, rootLevel, 0, dist2, pTransform, info.parentObject);
      else
        GScene->ProxyForDrawing(info.object, rootObj, pLevel, rootLevel, 0, dist2, pTransform, info.parentObject);
    }
    else
    {
      SkeletonIndex boneIndex = type->GetProxyBoneForLevel(level,i);
      Matrix4 animTransform = AnimateProxyMatrix(RenderVisualState(), level,proxy.GetTransform(),boneIndex);
      pTransform = transform * animTransform;

      // if shape is reversed, reverse orientation
      if (pshape->Remarks() & ShapeReversed)
      {
        Matrix4 swapM(MScale, -1, +1, -1);
        pTransform = pTransform * swapM;
      }

      // note: the itself may be hidden, if it is, skip it

      // check if the corresponding proxy face is hidden
      int section = type->GetProxySectionForLevel(level,i);
      if (section>=0)
      {
        int spec = animContext.GetSection(lock, section).Special();
        if (spec & IsHidden)
        {
          ReleaseProxyInfo(info,true);
          continue;
        }
      }

      // registration for drawing
      if (info.uniqueObject)
        GScene->TempProxyForDrawing(info.object, rootObj, pLevel, rootLevel, clipFlags, dist2, pTransform);
      else
        GScene->ProxyForDrawing(info.object, rootObj, pLevel, rootLevel, clipFlags, dist2, pTransform);
    }

    // scan if there are some proxies inside of this proxy
    if (object->NeedsPrepareProxiesForDrawing())
    {
      const EntityType *proxyType = object->GetEntityType();
      if (proxyType && pshape->NLevels()>0)
      {
        // LOD selection for the proxy of the proxy
        int ppLevel = -1;
        if (insideView)
        {
          // find inside view
          ppLevel = pshape->FindSpecLevel(VIEW_PILOT);
          // if there is no inside view, use the best LOD (if there is any)
          if (ppLevel<0 && pshape->FindSimplestLevel()>=0) ppLevel = 0;
        }
        if (ppLevel<0)
        {
          // TODO: propagate Scale2 instead of Scale() all way down from here
          ppLevel=GScene->LevelFromDistance2(pshape,dist2,pTransform.position.Scale());
        }
        // most likely the Animate is empty, but we call it for consistency
        if( ppLevel!=LOD_INVISIBLE )
        {
          ShapeUsed pLock = object->GetShape()->Level(ppLevel);
          AnimationContextStorageGeom storage;
          AnimationContext animContext(object->RenderVisualState(),storage);
          pLock->InitAnimationContext(animContext, pshape->GetConvexComponents(ppLevel), false);
          object->Animate(animContext, ppLevel, true, object, dist2);
          SortObject inner;
          inner.SetObject(object);
          inner.SetParentObject(info.parentObject);
          inner.SetRootObject(rootObj);
					inner.distance2 = dist2;
          object->PrepareProxiesForDrawing(rootObj, rootLevel, animContext, pTransform, ppLevel, ClipAll, &inner);
					object->Deanimate(ppLevel,true);
        }
      }
    }
    
    ReleaseProxyInfo(info,false);
  }

  // The second long weapon uses the existing shoulder proxy as a pose anchor.
  // Offset it across the back so it remains visible without requiring a new
  // character-model proxy in every survivor P3D.
  InventoryItem *secondShoulderWeapon = GetInSlot(ISShoulder2);
  if (_showPrimaryWeapon && secondShoulderWeapon && secondShoulderWeapon != _itemInHands && secondShoulderWeapon->GetShape())
  {
    int shoulderProxyIndex = -1;
    for (int i=0; i<type->GetProxyCountForLevel(level); i++)
    {
      const ProxyObjectTyped &proxy = type->GetProxyForLevel(level, i);
      Object *proxyObj = proxy.obj;
      const EntityType *proxyType = proxyObj ? proxyObj->GetEntityType() : NULL;
      if (proxyType && proxyType->GetInventorySlot() == ISShoulder)
      {
        shoulderProxyIndex = i;
        break;
      }
    }

    if (shoulderProxyIndex >= 0)
    {
      Weapon *secondWeapon = secondShoulderWeapon->GetWeapon();
      if (secondWeapon) secondWeapon->UpdateShape();

      LODShape *secondShape = secondShoulderWeapon->GetShape();
      int secondLevel = GScene->LevelFromDistance2(secondShape, dist2, transform.position.Scale());
      if (secondLevel == LOD_INVISIBLE && secondShoulderWeapon->GetFeatureSize() > 0)
        secondLevel = secondShape->FindSimplestLevel();

      if (secondLevel != LOD_INVISIBLE && secondLevel >= 0)
      {
        const ProxyObjectTyped &shoulderProxy = type->GetProxyForLevel(level, shoulderProxyIndex);
        SkeletonIndex boneIndex = type->GetProxyBoneForLevel(level, shoulderProxyIndex);
        Matrix4 anchor = AnimateProxyMatrix(RenderVisualState(), level, shoulderProxy.GetTransform(), boneIndex);

        const ManVisualState &mvs = RenderVisualState().cast<Man>();
        const Vector3 approvedShift(+0.32f, 0.0f, -0.10f);
        // While standing still (and not leaning) the offset is recalibrated so
        // the placement stays exactly the tuned one in character space. While
        // moving the last calibrated vector is kept, which makes the weapon
        // follow the shoulder bone rigidly (no sliding off the back).
        if ((mvs._modelSpeed.SquareSize() < 0.25f && mvs._leaningTransIdent) || !_secondShoulderOffsetValid)
        {
          _secondShoulderOffsetLocal = anchor.Orientation().InverseGeneral() * approvedShift;
          _secondShoulderOffsetValid = true;
        }
        Matrix4 oppositeSide(MTranslation, _secondShoulderOffsetLocal);
        PositionRender secondTransform = transform * anchor * oppositeSide;

        if (secondShape->Remarks() & ShapeReversed)
        {
          Matrix4 swapM(MScale, -1, +1, -1);
          secondTransform = secondTransform * swapM;
        }

        GScene->ProxyForDrawing(secondShoulderWeapon, rootObj, secondLevel, rootLevel, clipFlags, dist2, secondTransform);
      }
    }
  }

  base::PrepareProxiesForDrawing(rootObj, rootLevel, animContext, transform, level, clipFlags, so);

  if (parent)
  {
    // restore hidden elements
    ShowFlag(true);
    ShowHead(true);
    ShowWeapons(true, true);
  }
}

static float ManDensity = 1.0;


float Man::DensityRatio() const
{
  return ManDensity;
}

#if _ENABLE_CHEATS

// advances debugging - via scripting
#include <El/evaluator/expressImpl.hpp>

static GameValue SetManDensity(const GameState *state, GameValuePar oper1)
{
  ManDensity = oper1;
  return GameValue();
}

#include <El/Modules/modules.hpp>

static const GameFunction DbgUnary[]=
{
  GameFunction(GameNothing,"diag_densMan",SetManDensity,GameScalar TODO_FUNCTION_DOCUMENTATION),
};

INIT_MODULE(GameStateDbgMan, 3)
{
  GGameState.NewFunctions(DbgUnary,lenof(DbgUnary));
};

#endif

bool Man::PreloadInsideView() const
{
  int level = InsideLOD(CamInternal);
  if (level<0) return true;
  bool ret = true;

  const RefShapeRef &shapeRef = GetShape()->LevelRef(level);
  if (!shapeRef.RequestLoading()) return false;
  
  ShapeUsed shape = GetShape()->Level(level);
  // preload all proxy objects
  const ManType *type = Type();
  for( int i=0; i<type->GetProxyCountForLevel(level); i++ )
  {
    ProxyInfo info;
    GetProxyInfo(info,level,i);

    if (!info.object)
    {
      ReleaseProxyInfo(info,false);
      continue;
    }

    LODShape *pshape = info.object->GetShape();
    if (pshape->NLevels() == 0)
    {
      ReleaseProxyInfo(info,false);
      continue;
    }

    int inside = pshape->FindSpecLevel(VIEW_PILOT);
    if (inside<0) inside = 0;
    
    if (!pshape->CheckLevelLoaded(inside,true))
    {
      ret = false;
      ReleaseProxyInfo(info,false);
      continue;
    }
    // preload textures
    ShapeUsed shape = pshape->Level(inside);
    if (!shape->PreloadTextures(0, NULL))
    {
      ret = false;
    }

    // Preload proxies of the proxy
    if (!info.object->PreloadProxies(inside, 0.0f, true))
    {
      ret = false;
    }

    ReleaseProxyInfo(info,false);
  }
  
  return ret;
}

bool Man::PreloadProxies(int level, float dist2, bool preloadTextures) const
{
  bool ret = base::PreloadProxies(level, dist2, preloadTextures);

  // preload all proxy objects
  const ManType *type = Type();
  for( int i=0; i<type->GetProxyCountForLevel(level); i++ )
  {
    ProxyInfo info;
    GetProxyInfo(info,level,i);

    if (!info.object)
    {
      ReleaseProxyInfo(info,false);
      continue;
    }

    LODShape *pshape = info.object->GetShape();
	if( !pshape )
	{
		ReleaseProxyInfo(info,false);
		break;
	}

    const ProxyObjectTyped &proxy = type->GetProxyForLevel(level,i);
    
    int pLevel = -1;
    if (level==type->_insideView)
    {
      // find inside view
      pLevel = pshape->FindSpecLevel(VIEW_PILOT);
      if (pLevel>=0 && !pshape->CheckLevelLoaded(pLevel,true))
      {
        ret = false;
        ReleaseProxyInfo(info,false);
        continue;
      }
    }
    if (pLevel<0)
      pLevel = GScene->PreloadLevelFromDistance2(pshape, dist2, FutureVisualState().Scale()*proxy.obj->FutureVisualState().Scale());
    if( pLevel==LOD_INVISIBLE )
    {
      ReleaseProxyInfo(info,false);
      continue;
    }
    if (pLevel<0)
    {
      ret = false;
      ReleaseProxyInfo(info,false);
      continue;
    }
    // preload textures
    ShapeUsed shape = pshape->Level(pLevel);
    if (!shape->PreloadTextures(dist2, NULL))
      ret = false;
    ReleaseProxyInfo(info,false);
  }
  return ret;  
}


void Man::DrawNVOptics()
{
  if (IsNVEnabled() && !_useVisionModes)
  { 
    const WeaponType *goggles = GetSpecialItem(WSNVGoggles);
    if (/*Brain() && (!Brain()->GetVehicleIn() || GWorld->GetCameraType() == CamExternal) &&*/ goggles && goggles->_muzzles.Size()>0)
    {
      const MuzzleType *muzzle = goggles->_muzzles[0];
      LODShape *oShape = muzzle->_opticsModel;
      if (oShape)
      {
        ShapeUsed shape = oShape->Level(0);
        // 2D optics may have a few vertices and faces, but little sections and no components
        AnimationContextStorage<512,100,50,4> storage;
        AnimationContext animContext(RenderVisualState(),storage);
        shape->InitAnimationContext(animContext, NULL, false);

        // assume 20 frames per second (50 ms)
        const int frameMs = 50;
        const int nPhases = 5;
        const int repeat = frameMs*nPhases*10;
        int seedPrev = Glob.time.ModMs(repeat)/frameMs;
        int thisPhase = seedPrev%nPhases;
        muzzle->_animFire.RequestPhase(oShape, 0, thisPhase);
        muzzle->_animFire.SetPhaseLoadedOnly(animContext, oShape, 0, thisPhase);
        Draw2D(&animContext, oShape, 0, PackedWhite);
      }
    }
  }
}

void Man::DrawPeripheralVision(Person *person, CameraType camType)
{
  base::DrawPeripheralVision(person,camType);

  // when hit, draw red direction indication
  if (camType==CamInternal || camType==CamGunner)
  {
    SCOPE_GRF("drawHit",0);
    const float timeDelay = 0.1f;
    const float timePlus = 0.2f;
    const float timeMinus = 0.5f;
    if (_whenHit<Glob.time-timeDelay && _whenHit>=Glob.time-(timeDelay+timePlus+timeMinus))
    {
      float redTime = Glob.time-_whenHit-timeDelay;
      float redFactor;
      if (redTime>timePlus)
      {
        redFactor = 1-(redTime-timePlus)*(1.0f/timeMinus);
      }
      else
      {
        redFactor = redTime*(1.0f/timePlus);
      }
      // apply computed red factor
      float alpha = redFactor*0.4f;
      if (alpha>0.03f)
      {
        // calculate position based on direction
        const Camera &camera = *GScene->GetCamera();
        // conversion from model space to camera space??
        //Vector3 modelDir = camera.GetInvTransform().Rotate(_hitDirection);
        Vector3Val modelDir = _hitDirection;
        // check if it is from visible range or not
        // for this we can project on-screen
        // default - screen center
        float scrX = 0, scrY = +1;
        if (modelDir.Z()>1e-6)
        {
          // front half - show in upper half of the screen
          float invZ = 1/modelDir.Z();
          scrX = modelDir.X()*invZ*camera.InvLeft();
          scrY = -modelDir.Y()*invZ*camera.InvTop();
          // if we are out of the screen, normalize to get there
          if (fabs(scrX)>1)
          {
            float coef = 1/fabs(scrX);
            scrX *= coef; // result should be very close to +1 or -1
            scrY *= coef;
          }
          if (fabs(scrY)>1)
          {
            float coef = 1/fabs(scrY);
            scrX *= coef;
            scrY *= coef; // result should be very close to +1 or -1
          }
          // saturate to handle any "bad" values
          saturate(scrX,-1,+1);
          saturate(scrY,-1,0);
        }
        else if (modelDir.SquareSize()<Square(0.01f))
        {
          // very near - show it in the middle of the screen
          scrX = 0, scrY = 0;
        }
        else
        {
          // it is somewhere from the back
          // modelDir.X(),modelDir.Z() determines the direction
          // the direction should always be renormalized to the screen edge?
          float dir = atan2(modelDir.X(),modelDir.Z());
          // dir should be somewhere between (-PI/2, -PI) (+ PI/2, + PI)
          if (dir>0)
          {
            if (dir>H_PI*+0.75f)
            {
              // +PI*3/4..+PI should translate to bottom positive
              scrX = (H_PI-dir)/(H_PI*0.25f);
              scrY = +1;
            }
            else
            {
              // PI/2..PI*3/4 should translate to positive edge, y
              scrX = +1;
              scrY = (dir-H_PI*0.5f)/(H_PI*0.25f);
            }
          }
          else
          {
            if (dir<H_PI*-0.75f)
            {
              // -PI*3/4..-PI should translate to bottom negative
              scrX = (-H_PI-dir)/(H_PI*0.25f);
              scrY = +1;
            }
            else
            {
              // -PI/2..-PI*3/4 should translate to negative edge, y
              scrX = -1;
              scrY = (-H_PI*0.5f-dir)/(H_PI*0.25f);
            }
          }
          
          saturate(scrX,-1,+1);
          saturate(scrY,0,+1);
        }
        
        const float w = GEngine->Width();
        const float h = GEngine->Height();

        AspectSettings as;
        GEngine->GetAspectSettings(as);

        // const float size = 0.25;
        float size = 0.40f + 0.15f * (_howMuchHit - SHOW_DAMAGE_MIN) / (SHOW_DAMAGE_MAX - SHOW_DAMAGE_MIN);
        saturateMin(size, 1.0f);
        const float width = size * w * as.topFOV / as.leftFOV;
        const float height = size * h;
        // screne aspect
        const float x = (w-width)*(0.5f+scrX*0.5f);
        const float y = (h-height)*(0.5f+scrY*0.5f);
        PackedColor color = PackedColorRGB(
          GWorld->UI()->GetBloodColor(), PackColorComponent(alpha)
        );
        Texture *tex = GWorld->UI()->GetBloodTexture();
        MipInfo mip = GEngine->TextBank()->UseMipmap(tex, 0, 0);
        GEngine->Draw2D(mip, color, Rect2DAbs(x, y, width, height));
      }
    }
  }
}

bool Man::PreloadMuzzleFlash(const WeaponsState &weapons, const WeaponModePtr &ptr) const
{
  if (!ptr.IsValid()) return true;

  const AmmoType *ammo = ptr.GetAmmoType();
  if (!ammo) return true;
  
  const WeaponType *weapon = ptr.GetWeaponType();
  if (!weapon) return true;

  LODShape *pshape = weapon->GetShape();
  AnimationAnimatedTexture *animFire = &weapon->_animFire;
  
  const Magazine *magazine = ptr.GetMagazine();

  if (magazine && magazine->Type() && magazine->GetAmmo() > 0)
  {
    LODShape *special = magazine->Type()->_modelSpecial;
    if (special)
    {
      pshape = special;
      animFire = &magazine->Type()->_animFire;
    }
  }
      
  float z2 = GScene->GetCamera()->Position().Distance2(FutureVisualState().Position());
  
  if (EnableVisualEffects(SimulateVisibleNear)) switch (ammo->_simulation)
  {
    case AmmoShotBullet:
    case AmmoShotSpread:
      // preload muzzle flash texture - level not known
      if (!pshape) return true;
      return animFire->PreloadTexture(pshape,z2);
  }
  return true;
}

void Man::DrawCameraCockpit()
{
  if (GWorld->IsCameraActive(CamGunner))
  {
    LODShape *oShape = GetOpticsModel(this);
    if (oShape)
    {
      ShapeUsed shape = oShape->Level(0);
      AnimationContextStorage<512,100,50,4> storage;
      AnimationContext animContext(RenderVisualState(),storage);
      shape->InitAnimationContext(animContext, NULL, false);

      float transition = 1;
      CameraType camOld = GWorld->GetCameraTypeOld();
      CameraType camNew = GWorld->GetCameraTypeNew();
      bool internal = GWorld->IsCameraActive(CamInternal);
      if (camOld!=CamGunner)
      {
        transition = GWorld->GetCameraTypeTransition();
      }
      else if (camNew!=CamGunner)
      {
        transition = 1-GWorld->GetCameraTypeTransition();
      }
      if (!EnableOptics(RenderVisualState())) transition = 0;
      // if we are in transition, draw the 3D model as well as 2D

      const float transitionArea = internal ? 0.4f : 0.15f;
      // check transitionAlpha>0
      WeaponModePtr selected = _weaponsState.ValidatedCurrentWeapon();
      if (selected.IsValid() && transition > 1 - transitionArea)
      {
        const WeaponType *weapon = selected.GetWeaponType();
        const MuzzleType *muzzle = selected.GetMuzzleType();
        if (weapon && muzzle)
        {
          bool showFire = false;
          if (_weaponsState.ShowFire())
          {
            if ((weapon->_weaponType & MaskSlotPrimary) != 0)
              showFire = true;
          }
          
          // Check for possible animFire from attached optics
          const AnimationAnimatedTexture *animFire = &muzzle->_animFire;

          WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
        	if (ptr.IsValid()) 
          {
            const OpticsInfo *opticsInfo = ptr.GetOpticsInfo();
            if (opticsInfo && opticsInfo->_useModelOptics)
            {
              if (ptr._muzzle >=0 && ptr._weapon)
              {
                ItemOptics *optics = ptr._weapon->GetAttachedOptics();
                if (optics) animFire = &optics->Type()->_animFire;
              }
            }
          }
          
          if (showFire)
          {
            animFire->Unhide(animContext, oShape, 0);
            animFire->SetPhaseLoadedOnly(animContext, oShape, 0, _weaponsState._mGunFirePhase);
          }
          else
          {
            animFire->Hide(animContext, oShape, 0);
          }
          // during transition apply a blend
          PackedColor color = GetOpticsColor(this);

          float transitionAlpha = (transition-(1-transitionArea))/transitionArea;
          int a = toInt(transitionAlpha*255);
          saturate(a,0,255);
          color.SetA8(a);
#if _VBS3 //zoomable inner optics via LOD1
          if (oShape && oShape->NLevels()>1)
          {
            float zoom = 1.0f;
            zoom = muzzle->_opticsZoomInit / GetCameraFOV();
            Draw2D(&animContext,oShape, 1, color, zoom);
          }
#endif
          Draw2D(&animContext, oShape, 0, color);
        }
      }
    }
  }
  // while screaming, draw red overlay
  {
    const float timeDelay = 0.3f;
    const float timePlus = 0.3f;
    const float timeMinus = 2;
    if (_whenScreamed<Glob.time-timeDelay && _whenScreamed>=Glob.time-(timeDelay+timePlus+timeMinus))
    {
      float redTime = Glob.time-timeDelay-_whenScreamed;
      float redFactor;
      if (redTime>timePlus)
      {
        redFactor = 1-(redTime-timePlus)*(1.0f/timeMinus);
      }
      else
      {
        redFactor = redTime*(1.0f/timePlus);
      }
      // apply computed red factor
      if (redFactor>0)
      {
        float alpha = redFactor*0.2f;
        PackedColor color = PackedColor(Color(0.5f, 0, 0, alpha));
        const float w = GEngine->WidthBB();
        const float h = GEngine->HeightBB();
        MipInfo mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
        GEngine->Draw2D(mip, color, Rect2DAbs(0, 0, w, h));
      }
    }
  }

}

int Man::PassNum( int lod )
{
  if (DrawInPass3()) return 3;
  if (_shape->IsCockpit(lod)) return 3;  // cockpit contains alpha - muzzle flash
  // for people assume no alpha
  return -3;
}

#define DOF_ALWAYS 1

void Man::DrawBase(
  int cb, int level, ClipFlags clipFlags, const DrawParameters &dp,
  const InstanceParameters &ip, float dist2, const PositionRender &pos, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
  , int drawListId
#endif
)
{
#if _HELISIM_RENDER_TO_TEXTURE
  base::Draw(cb,level, clipFlags, dp, ip, dist2, pos, oi, drawListId);
#else
  base::Draw(cb,level, clipFlags, dp, ip, dist2, pos, oi);
#endif
}

bool Man::CastPass3VolShadow(int level, bool zSpace, float &castShadowDiff, float &castShadowAmb) const
{
  castShadowDiff = 1.0f;
  castShadowAmb = 1.0f;
  return _shape->FindShadowVolume() >= 0;
}

void Man::Draw(
  int cb, int level, ClipFlags clipFlags, const DrawParameters &dp,
  const InstanceParameters &ip, float dist2, const PositionRender &pos, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
  , int drawListId
#endif  
  )
{
  if (level == LOD_INVISIBLE) return; // invisible LOD
  ADD_COUNTER(manDr, 1);

  Object *parent = GetHierarchyParent();
  if (parent)
    DrawAsProxy(cb, level, clipFlags, dp, ip, dist2, pos, oi
#if _HELISIM_RENDER_TO_TEXTURE
    , drawListId
#endif
    ); // inside vehicle
  else
    DoDraw(cb, level, clipFlags, dp, ip, dist2, pos, oi
#if _HELISIM_RENDER_TO_TEXTURE
    , drawListId
#endif
    ); // outside vehicle
}

bool Man::CanDrawModel(float &transition) const
{
  transition = 0;
  if (!GWorld->IsCameraActive(CamGunner) || GWorld->CameraOn() != this || !EnableOptics(RenderVisualState())) return true; // not drawing own optics

  if (GWorld->GetCameraTypeOld() != CamGunner)
  {
    transition = GWorld->GetCameraTypeTransition();
  }
  else if (GWorld->GetCameraTypeNew() != CamGunner)
  {
    transition = 1 - GWorld->GetCameraTypeTransition();
  }
  else transition = 1;

  // if we are in transition, draw the 3D model as well as 2D
  bool internal = GWorld->IsCameraActive(CamInternal);
  float transitionThold = internal ? 0.85f : 0.95f;
  return (transition < transitionThold || !GetOpticsModel(this));
}

bool Man::DrawBlur(float transition) const
{
  return transition>0.4f;
}

float Man::GetFocusDistance(float transition)
{
  float ret = -1;
  if (DrawBlur(transition))
  {
    TurretContextEx context;
    context._turret = NULL;
    context._turretType = NULL;
    context._gunner = this;
    context._weapons = &_weaponsState;
    context._parentTrans = RenderVisualState().Orientation();

    WeaponModePtr ptr = context._weapons->ValidatedCurrentWeapon();
    Vector3 weaponPos = RenderVisualState().PositionModelToWorld(GetWeaponPoint(RenderVisualState(),context, ptr));
    Vector3 cursorDir = GetWeaponDirectionWanted(context, ptr);
    Target *autoAimTarget = _autoAimLocked.NotNull() ? safe_cast<Target *>(_autoAimLocked) : SelectAutoAimTarget(context, ptr, cursorDir, 4, false);

    float distance;
    if (!autoAimTarget)
    {
      // we want distance to have marginal effect - most important is what are we aiming at
      distance = GWorld->AutoFocus(GetRenderVisualStateAge(),this,weaponPos,cursorDir,0.3,0,Glob.header.MinDOFFocusDistance);
			saturate(distance, Glob.header.MinDOFFocusDistance, Glob.header.MaxDOFFocusDistance);
    }
    else
    {
      Vector3 tgtPos = autoAimTarget->idExact->AimingPosition(autoAimTarget->idExact->RenderVisualState());
      distance = tgtPos.Distance(GScene->GetCamera()->Position());
    }

    ret = Interpolativ(transition, 0, 1, Glob.header.MinDOFFocusDistance, distance);
  }

  return ret;
}

void Man::PrepareInsideRendering()
{
  float transition;
  //  check the transition factor
  CanDrawModel(transition);
  _renderDepthOfFieldDistance = GetFocusDistance(transition);
}

void Man::DoDraw(
  int cb, int level, ClipFlags clipFlags, const DrawParameters &dp,
  const InstanceParameters &ip, float dist2, const PositionRender &pos, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
  , int drawListId
#endif
)
{
#if _ENABLE_CHEATS
  if (GWorld->CameraOn() == this)
    {
      if (CHECK_DIAG(DETransparent))
      {
        return;
      }
    }
#endif

  float transition;
  if (CanDrawModel(transition))
  {
    // draw the 3D model
    DrawBase(cb,level,clipFlags, dp, ip, dist2, pos, oi
#if _HELISIM_RENDER_TO_TEXTURE
      , drawListId
#endif
      );
  }
  // when we want to draw blurred, check what the blur should be
  // this was precalculated, because we need to be thread safe here
  if (DrawBlur(transition) && _renderDepthOfFieldDistance>0)
  {
    GEngine->EnableDepthOfField(_renderDepthOfFieldDistance,1,false);
  }
}

void Man::DrawAsProxy(
  int cb, int level, ClipFlags clipFlags, const DrawParameters &dp,
  const InstanceParameters &ip, float dist2, const PositionRender &pos, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
  , int drawListId
#endif
  )
{
  // check if head need to be hidden
  bool insidePlayer = false;
  CameraType camType = GWorld->GetCameraType();
  if (camType != CamExternal && camType != CamGroup)
  {
    if (!GWorld->GetCameraEffect())
    {
      AIBrain *unit = GWorld->FocusOn();
      Person *player = unit ? unit->GetPerson() : NULL;
      insidePlayer = this == player;
    }
  }

  // check if weapons need to be hidden
  bool hideWeapons = true;
  bool showSecondaryWeapon = false;
  {
    Object *parent = GetHierarchyParent();
    if (parent)
    {
      Transport *vehicle = dyn_cast<Transport>(parent);
      if (vehicle)
      {
        const TransportType *type = vehicle->Type();
        if (this == vehicle->Driver()) hideWeapons = type->_hideWeaponsDriver;
        else
        {
          TurretContext context;
          if (FindTurret(this, context) && context._turretType) hideWeapons = context._turretType->_hideWeaponsGunner;
          else hideWeapons = type->_hideWeaponsCargo;
        }
      }
    }
  }

  // hide what needed
  SCOPE_GRF(GetShape()->GetName(), 0);
  ShowWeapons(!hideWeapons, showSecondaryWeapon);
  if (insidePlayer) ShowHead(false);
  ShowFlag(false);

  // common drawing implementation
  DoDraw(cb, level, clipFlags, dp, ip, dist2, pos, oi
#if _HELISIM_RENDER_TO_TEXTURE
    , drawListId
#endif    
    );

  // restore hidden elements
  ShowFlag(true);
  if (insidePlayer) ShowHead(true);
  ShowWeapons(true, true);
}

AnimationStyle Man::IsAnimated( int level ) const {return AnimatedGeometry;}

#if _ENABLE_NEWHEAD

void Man::SetHead(RString name)
{
  if (_headType.NotNull()) RemoveHead();
  _headType = HeadTypes.New(name);
  _headType->ShapeAddRef();
  if (_headType->_model.NotNull())
  {
    if (GetShape()->GetSkeleton() != _headType->_model->GetShape()->GetSkeleton())
    {
      RptF("Error: Skeleton of the head %s doesn't match the skeleton of the corresponding body %s", _headType->_model->GetShape()->Name(), GetShape()->Name());
      Fail("Error: Skeleton of some head doesn't match the skeleton of the corresponding body");
    }
  }
  else
  {
    RptF("Error: Head model named '%s' not present", cc_cast(name));
  }
}

void Man::SetHead(RString faceType, RString name)
{
  ParamEntryVal cfg = Pars >> "CfgFaces" >> faceType;
  ConstParamEntryPtr cls = cfg.FindEntry(name);
  if (!cls)
  {
    RptF("SetFace error: class CfgFaces.%s.%s not found", cc_cast(faceType), cc_cast(name));
    cls = cfg.FindEntry("Default");
    if (!cls)
    {
      RptF("SetFace error: class CfgFaces.%s.Default not found", cc_cast(faceType));
      return;
    }
  }
  if (cls->FindEntry("head"))
  {
    RString name = *cls >> "head";
    if (!name.IsEmpty()) SetHead(name);
  }
  else
  {
    RptF("SetFace error: 'head' entry was not found (class CfgFaces.%s.%s)", cc_cast(faceType), cc_cast(name));
  }
}

void Man::RemoveHead()
{
  if (_headType.NotNull())
  {
    _headType->ShapeRelease();
    _headType.Free();
  }
}

#endif

Vector3 Man::GetHUDOffset() const
{
  int index = Type()->_pilotPoint;
  if (index >= 0)
  {
    const VisualState &vs = RenderVisualStateScope();
    int level = _shape->FindMemoryLevel();
    BlendAnims blend;
    GetBlendAnims(vs,blend);
    Vector3 pilotPosBase = _moves.AnimatePoint(blend,GetMovesType(), _shape, level, index);
    Vector3Val pilotPos =  ApplyNotRTMAnimations(vs,pilotPosBase,level,index);

    return pilotPos-pilotPosBase;
  }
  return VZero;
}

bool Man::AttachWaveReady(const char *wave)
{
  return _head.AttachWaveReady(wave);
}

void Man::AttachWave(AbstractWave *wave, float freq)
{
  _head.AttachWave(wave, freq);
  if (GetCameraInterest()<50)
  {
    SetCameraInterest(50);
  }
}

float Man::GetSpeaking() const
{
  if (_head._randomLip || _head._lipInfo)
  {
    return 1;
  }
  return 0;
}

void Man::SetRandomLip(bool set)
{
  _head.SetRandomLip(set);
}

bool Man::UseInternalLODInVehicles()  const
{
  return Type()->_useInternalLODInVehicles;
}

void Man::ShowHead(bool show)
{
  _showHead = show;
}

void Man::ShowWeapons(bool showPrimary, bool showSecondary)
{
  _showPrimaryWeapon = showPrimary;
  _showSecondaryWeapon = showSecondary;
}

// SUBIDA: limites originais (0.4 subida no run / 0.2 no sprint) faziam o
// personagem cair para "andar lento" em qualquer ladeira. Agora ele mantem
// corrida/sprint em praticamente qualquer declive caminhavel.
const float MaxStandSlope = 1.0f;
const float MinStandSlope = -1.0f;

const float MaxRunSlope = 1.0f;
const float MinRunSlope = -1.0f;

const float MaxSprintSlope = 0.9f;
const float MinSprintSlope = -0.9f;

inline bool CanSprint(float gradFwd, float gradAside, float dirFwd, float dirAside)
{
  float slope = gradFwd*dirFwd + gradAside*dirAside;
  return slope>=MinSprintSlope && slope<=MaxSprintSlope;
}
inline bool CanRun(float gradFwd, float gradAside, float dirFwd, float dirAside)
{
  float slope = gradFwd*dirFwd + gradAside*dirAside;
  return slope>=MinRunSlope && slope<=MaxRunSlope;
}
inline bool CanStand(float gradFwd, float gradAside, float dirFwd, float dirAside)
{
  /*
  if (fabs(dirFwd)+fabs(dirAside)<0.1) return true; //soldier can stand until he do not wants to move
  float slope = gradFwd*dirFwd + gradAside*dirAside;
  return slope>=MinStandSlope && slope<=MaxStandSlope;
  */
  // crawling at steep slopes looks ugly - we rather stand and move slow
  return true;
}

void Man::LandSlope(bool &forceStand, float &gradFwd, float &gradAside) const
{
  float dx,dz;
  Texture *tex = NULL;
  Object *obj = NULL;
  forceStand = false;
  GLandscape->RoadSurfaceY(FutureVisualState().Position()+VUp*0.5f,&dx,&dz,&tex,Landscape::UOWait,&obj);
  if (obj) forceStand = true;
  // calculate change along direction forward
  gradFwd = dx*FutureVisualState().Direction().X()+dz*FutureVisualState().Direction().Z();
  gradAside = dx*FutureVisualState().DirectionAside().X()+dz*FutureVisualState().DirectionAside().Z();
  //GlobalShowMessage
  //(
  //  100, "Slope %.2f,%.2f, slope %.2f, dir %.2f,%.2f",
  //  dx,dz, slope, Direction().X(),Direction().Z()
  //);
}

bool Man::IsAbleToProne() const
{
  Vector3Val pos = FutureVisualState().Position() + VUp*0.5f;
  Vector3Val dir = FutureVisualState().Direction();
  float dist = 1.5f;

  Vector3 testDirs[2] =
  {
    dir, -dir
  };

  for (int i = 0; i < 2; ++i)
  {
    CollisionBuffer col;
    GLandscape->ObjectCollisionLine(GetFutureVisualStateAge(), col, Landscape::FilterIgnoreOne(this), pos, pos + testDirs[i] * dist, 0.25f, ObjIntersectGeom, ObjIntersectView, CF_NEAREST_CONTACT);
    if (col.Size() == 0)
      return true;
  }
  return false;
}

bool Man::IsAbleToGetOver() const
{
  if (!IsAbleToStand())
    return false;

  CollisionBuffer col;
  Vector3Val playerPos = FutureVisualState().Position();
  Vector3Val playerDir = FutureVisualState().Direction();
  int testAge = GetFutureVisualStateAge();

  // ANIMS-VAULT:
  // O teste original exigia o vao totalmente livre a 1,2 m de altura e chao do
  // outro lado dentro de +-0,3 m, o que impedia saltar muros reais.
  // Agora: aceita obstaculo com topo ate 1,35 m e chegada entre -0,9 m e +0,35 m.

  // 1) espaco livre acima do obstaculo imediato
  {
    Vector3Val headPos = playerPos + VUp * 2.0f + playerDir * 0.6f;
    GLandscape->ObjectCollisionLine(testAge, col, Landscape::FilterIgnoreOne(this), headPos, headPos + playerDir * 0.8f, 0.25f, ObjIntersectGeom, ObjIntersectFire, CF_NEAREST_CONTACT);
    if (col.Size() > 0)
      return false;
  }

  // 2) topo do obstaculo a frente (o chao no mesmo nivel nao conta)
  {
    Vector3Val probeTop = playerPos + VUp * 2.0f + playerDir * 0.85f;
    GLandscape->ObjectCollisionLine(testAge, col, Landscape::FilterIgnoreOne(this), probeTop, probeTop - VUp * 1.9f, 0.25f, ObjIntersectGeom, ObjIntersectFire, CF_NEAREST_CONTACT);
    if (col.Size() > 0)
    {
      float top = col[0].pos.y - playerPos.y;
      if (top > 1.6f) return false; // alto demais para saltar
    }
  }

  // 3) ponto de chegada (1,5 m a frente) precisa estar em nivel parecido
  {
    Vector3Val testPos2 = playerPos + VUp * 2.0f + playerDir * 1.5f;
    GLandscape->ObjectCollisionLine(testAge, col, Landscape::FilterIgnoreOne(this), testPos2, testPos2 - VUp * 3.0f, 0.25f, ObjIntersectGeom, ObjIntersectFire, CF_NEAREST_CONTACT);
    if (col.Size() > 0)
    {
      float dy = col[0].pos.y - playerPos.y;
      if (dy > 0.35f || dy < -0.9f)
        return false;
    }
  }
  return true;
}

bool Man::IsAbleToStand() const
{
  if( GetHit(Type()->_legsHit)>=0.9f ) return false;
  // check landscape slope
  return true;
}

void Man::BreakLegs()
{
	this->SetHitContRaw(Type()->_legsHit, 0.91f);
}

bool Man::IsAbleToFire(const TurretContext &context) const
{
  //if( GetHit(Type()->_handsHit)>=0.9f ) return false;
  return true;
}

/*!
\patch 1.01 Date 6/25/2001 by Ondra.
- Fixed: Movement possible while using optics during crouch.
*/

LODShape *Man::GetOpticsModel(const Person *person) const
{
  LODShape *ret = base::GetOpticsModel(person);
  if (!ret) return ret;
  if (BinocularSelected() && !EnableBinocular(RenderVisualState())) return NULL;
  if (LauncherSelected() && !EnableMissile(RenderVisualState())) return NULL;
  if (!EnableOptics(RenderVisualState())) return NULL;
  return ret;
}

bool Man::GetEnableOptics(ObjectVisualState const& vs, const Person *person) const
{
  bool ret = base::GetEnableOptics(vs, person);
  if (!ret) return false;
  if (BinocularSelected() && !EnableBinocular(vs.cast<Man>())) return false;
  if (LauncherSelected() && !EnableMissile(vs.cast<Man>())) return false;
  if (!EnableOptics(vs)) return false;
  return true;
}


bool Man::GetForceOptics(const Person *person, CameraType camType) const
{
  bool ret = base::GetForceOptics(person, camType);
  if (!ret) return false;
  // check if we are already in binoc mode
  if (!EnableBinocular(RenderVisualState())) return false;
  return true;
}

bool Man::HasOpticsTransitionFx(Person *person, CameraType camTypeNew, CameraType camTypeOld) const
{
  if (WeaponsDisabled()) return false;
  if (!GetOpticsModel(person) && !GetEnableOptics(RenderVisualState(), person)) return false;
  if (camTypeNew==CamGunner && (camTypeOld==CamInternal || camTypeOld==CamExternal)) return true;
  if (camTypeOld==CamGunner && (camTypeNew==CamInternal || camTypeNew==CamExternal)) return true;
  return false;
}

bool Man::HasCameraTransitionFx(Person *person, CameraType camTypeNew, CameraType camTypeOld) const
{
  // optics transition are handled by HasOpticsTransitionFx
  // if they are disabled, there is some reason for it
  if (camTypeNew==CamGunner || camTypeOld==CamGunner) return false;
  return true;
}

/// check if both camera are a kind of internal one
static inline bool AllInternal(CameraType camTypeNew, CameraType camTypeOld)
{
  return(
    (camTypeNew==CamInternal || camTypeNew==CamGunner ) &&
    (camTypeOld==CamInternal || camTypeOld==CamGunner )
  );
};

float Man::CameraTransitionFxSpeed(Person *person, CameraType camTypeNew, CameraType camTypeOld) const
{
  if (AllInternal(camTypeNew,camTypeOld) && !GetOpticsModel(person))
  {
    // if the weapon uses 3D sights, there is no need for any significant time
    // model is already loaded
    return 4.5f;
  }
  else
  {
    // transition slower due to preloading needed
    return 3.0f;
  }
}

void Man::OnCameraTransition(Person *person, CameraType camTypeNew, CameraType camTypeOld, float newFactor) const
{
  // we might update zoom during transition between 1st person view and sights here
  // we rather do it directly in Zoom::Update
}

bool Man::PreloadView(Person *person, CameraType camType) const
{
  bool ret = base::PreloadView(person,camType);
  // preload weapon proxies
  return ret;
}


/*!
\patch 1.05 Date 7/18/2001 by Ondra.
- Improved: blood appears before the injury is critical.
Soldiers also reports sooner for medical assistance.
*/

void Man::WoundsAnimation(AnimationContext &animContext, int level, float dist2)
{
  bool NoBlood();
  if (NoBlood()) return;
  if (Glob.config.bloodLevel==0) return;

  const ManType * type = Type();
  int bodyWound = WoundLevel(GetHitContRaw(type->_bodyHit),ManHitLimitBody);
  int headWound = WoundLevel(GetHitContRaw(type->_headHit),ManHitLimitHead);
  int handsWound = WoundLevel(GetHitContRaw(type->_handsHit),ManHitLimit);
  int legsWound = WoundLevel(GetHitContRaw(type->_legsHit),ManHitLimit);
  // scan corresponding wound
  if (bodyWound>0)
  {
    type->_bodyWound.Apply(animContext, _shape, level, bodyWound, dist2);
  }
  if (/*_showHead && */headWound>0)
  {
    type->_headWound.ApplyModified(animContext, _shape, level, headWound, type->_faceTextureOrig, _faceTextureWounded[headWound-1], dist2);
  }
  if (handsWound>0)
  {
    type->_lArmWound.ApplyModified(animContext, _shape, level, handsWound, type->_faceTextureOrig, _faceTextureWounded[handsWound-1], dist2);
    type->_rArmWound.ApplyModified(animContext, _shape, level, handsWound, type->_faceTextureOrig, _faceTextureWounded[handsWound-1], dist2);
  }
  if (legsWound>0)
  {
    type->_lLegWound.Apply(animContext, _shape, level, legsWound, dist2);
    type->_rLegWound.Apply(animContext, _shape, level, legsWound, dist2);
  }
}

void Man::WoundsDeanimation( int level )
{
}

void Man::BasicAnimation(AnimationContext &animContext, int level, bool setEngineStuff, float dist2)
{
  
  Assert(_shape->IsLevelLocked(level));
  const Shape *shape = _shape->GetLevelLocked(level);

  // Animate the personality textures (face in case we use LOD that have face in geometry, hands)
  const ManType *type = Type();
  if (_faceMaterial) type->_personality.SetMaterial(animContext, _shape, level, _faceMaterial);
  type->_personality.SetTexture(animContext, _shape, level, _faceTexture);

  if (_showHead)
    Type()->_headHide.Unhide(animContext, _shape, level);
  else
    Type()->_headHide.Hide(animContext, _shape, level);

  {
#if !_ENABLE_NEWHEAD
    const VisualState &vs = RenderVisualStateScope();
    Matrix3 headTrans;
    // in typical HW TL case headTrans is not used at all
    if (!setEngineStuff)
    {
      Matrix4 headOrient(MDirection, -VAside, VUp);
      // Matrix4 headOrient(M4HeadBase);
      if (GetSkeletonIndex(Type()->_headBone) >= 0)
      {
        AnimateBoneMatrix(headOrient,vs,level,Type()->_headBone);
        if (_headTransIdent)
        {
          headTrans = headOrient.Orientation();
        }
        else
        {
          headTrans = headOrient.Orientation() * _headTrans.Orientation();
        }
      }
    }
    if (!_head.GetFace())
    {
      RptF("White head error: missing face texture on %s", (const char *)GetDebugName());
    }
    const ManType *type=Type();
    _head.Animate(type->_head, animContext, _shape, level, _isDead, headTrans, !_showHead);
#endif
  }

  // is wounded animate wounds
  WoundsAnimation(animContext, level, dist2);

  // set minmax box and sphere
  Vector3 min = shape->Min();
  Vector3 max = shape->Max();
  Vector3 bCenter = shape->BSphereCenter();
  float bRadius = shape->BSphereRadius();
  // enlarge twice to be sure soldier will fit in
  Vector3 factor(2,1.2f,3);
  float sFactor = 3;
  min = bCenter+factor.Modulate(min-bCenter);
  max = bCenter+factor.Modulate(max-bCenter);
  bRadius *= sFactor;
  animContext.SetMinMax(min,max,bCenter,bRadius);
  base::Animate(animContext, level, setEngineStuff, this, dist2);
}

float Man::GetCollisionRadius(const ObjectVisualState &vs) const 
{ 
	float bSphereEnlarge = ValueTest(vs.cast<Man>(),&MoveInfoMan::_boundingSphere); 

	if (bSphereEnlarge>1) 
	{ 
		return 2.0f*bSphereEnlarge; 
	}  

	return 2.0f; 
} 

float Man::ClippingInfo(const ObjectVisualState &vs, Vector3 *minMax, ClippingType clip) const
{
  const ManVisualState &mvs = vs.cast<Man>();
  float bSphereEnlarge = ValueTest(mvs,&MoveInfoMan::_boundingSphere);
  float bSphere;
  if (!IsDown(mvs) && !IsDead(mvs) || _moves.GetMoveQueueSize()>0 || _gesture.GetMoveQueueSize()>0)
  {
    minMax[0] = Vector3(-0.8f,-0.1f,-1.15f);
    minMax[1] = Vector3(+0.8f,+1.9f,+1.05f);
    bSphere = 2.0f;
  }
  else
  {
    minMax[0] = Vector3(-0.8f,-0.1f,-1.15f);
    minMax[1] = Vector3(+0.8f,+0.8f,+1.05f);
    bSphere = 2.0f;
  }
  if (bSphereEnlarge>1)
  {
    Vector3 minMaxEnlarge = (minMax[1]-minMax[0])*((bSphereEnlarge-1)*0.5f);
    minMax[0] -= minMaxEnlarge;
    minMax[1] += minMaxEnlarge;
    bSphere *= bSphereEnlarge;
  }
  return bSphere;
}

float Man::BoundingInfoUI (Vector3 &bCenter, Vector3 *minMax) const
{
  const float scaleMinMax = 0.6f;
  // derived from Man::ClippingInfo
  const ManVisualState &vs = RenderVisualState();
  float bSphereEnlarge = ValueTest(vs,&MoveInfoMan::_boundingSphere);
  float bSphere;
  if (!IsDown() && !IsDead() || _moves.GetMoveQueueSize()>0)
  {
    minMax[0] = Vector3(-0.8f,-0.1f,-1.15f)*scaleMinMax;
    minMax[1] = Vector3(+0.8f,+1.9f,+1.05f)*scaleMinMax;
    bCenter = Vector3(0,0.9f,0);
    bSphere = 1.0f;
  }
  else
  {
    minMax[0] = Vector3(-0.8f,-0.1f,-1.15f)*scaleMinMax;
    minMax[1] = Vector3(+0.8f,+0.8f,+1.05f)*scaleMinMax;
    bCenter = Vector3(0,0.35f,0);
    bSphere = 1.0f;
  }
  if (bSphereEnlarge>1)
  {
    Vector3 minMaxEnlarge = (minMax[1]-minMax[0])*((bSphereEnlarge-1)*0.5f);
    minMax[0] -= minMaxEnlarge;
    minMax[1] += minMaxEnlarge;
    bSphere *= bSphereEnlarge;
  }
  return bSphere;
}

void Man::AnimatedBSphere(
  int level, Vector3 &bCenter, float &bRadius, Matrix4Par pos, float posMaxScale
) const
{
  bRadius = 2.0f*posMaxScale;
  bCenter = pos.Position();
}


void Man::BasicDeanimation(int level, bool setEngineStuff)
{
  WoundsDeanimation(level);

  {
#if !_ENABLE_NEWHEAD
    Matrix3 headTrans;
    if (!setEngineStuff)
    {
      Matrix4 headOrient(MDirection, -VAside, VUp);
      // Matrix4 headOrient(M4HeadBase);
      if (GetSkeletonIndex(Type()->_headBone) >= 0)
      {
        AnimateBoneMatrix(headOrient, vs, level, Type()->_headBone);

        if (_headTransIdent)
        {
          headTrans = headOrient.Orientation();
        }
        else
        {
          headTrans = headOrient.Orientation() * _headTrans.Orientation();
        }
      }
    }
    const ManType *type = Type();

    _head.Deanimate(type->_head, _shape, level, _isDead, headTrans, !_showHead);
#endif
  }

  base::Deanimate(level,setEngineStuff);
}

UnitPosition Man::GetUnitPosition() const
{
  if (_unitPosCommanded != UPAuto) return _unitPosCommanded; // highest priority
  if (_unitPosScripted != UPAuto) return _unitPosScripted;
  return _unitPosFSM;
}

void Man::SetUnitPositionCommanded(UnitPosition status)
{
  _unitPosCommanded = status;
}

void Man::SetUnitPositionScripted(UnitPosition status)
{
  _unitPosScripted = status;
}

void Man::SetUnitPositionFSM(UnitPosition status)
{
  _unitPosFSM = status;
}

float Man::GetAnimSpeed(RStringB move)
{
  const MovesTypeMan *type = GetMovesType();
  MoveId moveId = type->GetMoveId(move);
  const MoveInfoMan *info = Type()->GetMoveInfo(moveId, AnimSubIx());
  if (info) return info->GetSpeed();
  return 1;
}

Vector3 Man::GetPilotPosition(CameraType camType) const
{
  const VisualState &vs = RenderVisualStateScope();
  const ManType *type = Type();
  int index = type->_pilotPoint;
  int level = _shape->FindMemoryLevel();
  return AnimatePoint(vs, level, index);
}

void Man::Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2)
{
  const VisualState &vs = animContext.GetVisualState().cast<Man>();
  
  PROFILE_SCOPE(manAn);
  Assert(_shape->IsLevelLocked(level));
  const Shape *shape=_shape->GetLevelLocked(level);
  const ManType *type=Type();

  if (level == _shape->FindGeometryLevel())
  {
    if (setEngineStuff) return; // no need to animate (and probably geometry in animContext is not valid)

    /* Collision geometry are animated by different system than other geometries. 
     It uses prefabricated capsules for each move. This capsules are loaded into man's geometry level.
     Because now is impossible to replace a shape in LODShape with another one following algorithm is used:

     In man's geometry level are prepared convex components, where each vertex has name. The same convex 
     components are in capsules (in fact LODShapes). Vertexes in man's geometry level are just moved to the position 
     in used in capsules. 

     Not all convex components from man's geometry level are in capsules. Those that are not present, should not be used 
     in collision detection algorithm. 

     Some moves have two capsules normal one and safe one. Safe one is used in cases where normal capsule too much 
     restricts move.
     */
    const MoveInfoMan *info = Type()->GetMoveInfo(_moves.GetPrimaryMove(vs._moves).id, AnimSubIx());
    const ConvexComponents &componentsInfo = _shape->GetGeomComponents();

    if (info == NULL)
    {
      // disable all components
      for (int i=0; i<componentsInfo.Size(); i++) animContext.CCDisable(i);
      return;
    }

    if (_safeSystem && _useSafeCapsule && info->HasCollCapsuleSafe())
      info->AnimateCollGeom(animContext, shape, type->GetVertexCollMapping(), type->GetGeomCompCollMapping(), true);
    else
      _useSafeCapsule = info->AnimateCollGeom(animContext, shape, type->GetVertexCollMapping(), type->GetGeomCompCollMapping());

    // Legs transform. Transform bottom components according to legs
    const Mapping &bottomComp = Type()->GetBottomGeomComp();

    // Enable / disable components
    for (int k=0; k<bottomComp.Size(); k++)
    {
      const ConvexComponent *componentInfo = componentsInfo[bottomComp[k]];

      if (animContext.IsCCEnabled(bottomComp[k]))
      {        
        for (int j=0; j<componentInfo->Size(); j++ )
        {
          Vector3 &cVertex = animContext.SetPos(shape, (*componentInfo)[j]);
          cVertex = vs._legTrans.Rotate(cVertex);            
        }
      }
    }

#if _ENABLE_CHEATS || _VBS3_CHEAT_DIAG
    if (DiagDrawModeState == DDMGeometry)
    {
      for (int k=0; k<componentsInfo.Size(); k++)
      {
        const ConvexComponent *componentInfo = componentsInfo[k];

        if (!animContext.IsCCEnabled(k))
        {
          for (int j=0; j<componentInfo->Size(); j++ )
          {
            Vector3 &cVertex = animContext.SetPos(shape, (*componentInfo)[j]);
            cVertex = VZero;           
          }
        }
      }
    }
#endif

    animContext.InvalidateNormals(shape);
    animContext.InvalidateMinMax();
    animContext.InvalidateConvexComponents();

    //BasicAnimation(level);
    return;       
  }


  // Software animation
  if (!setEngineStuff)
  {
    // check for special case: animating one-point level on CPU
    if (shape->NVertex()==1)
    {
      // singular case
      animContext.SetPos(shape, 0) = AnimatePoint(vs, level, 0);
      return;
    }
  }

  BasicAnimation(animContext, level,setEngineStuff,dist2);
}

void Man::Deanimate(int level, bool setEngineStuff)
{
  if (level == _shape->FindGeometryLevel())
  {
    return; 
  }

  // Man is always animated - no need to deanimate
  Assert(_shape->IsLevelLocked(level));
  const Shape *shape=_shape->GetLevelLocked(level);
  if (!setEngineStuff && shape->NVertex()==1) return; // singular case
  
  BasicDeanimation(level,setEngineStuff);
}

#if _ENABLE_HAND_IK

void Man::HandIK(const VisualState &vs, LODShape * lshape, int level, StaticArrayAuto<Matrix4>& matrix)
{
  //const VisualState &vs = animContext.GetVisualState();
  float leftBlend = ValueTest(vs,&MoveInfoMan::LeftHandIK);
  float rightBlend = ValueTest(vs,&MoveInfoMan::RightHandIK);

  const MoveInfoGestureMan *gestureInfo = Type()->GetMoveInfoGesture(_gesture.GetPrimaryMove(vs._gestures).id);
  if (gestureInfo)
  {
    float gestureTime = _gesture.GetPrimaryTime(vs._gestures);
    float leftGestureBlend = gestureInfo->LeftHandIK(gestureTime);
    float rightGestureBlend = gestureInfo->RightHandIK(gestureTime);
    leftBlend = floatMin(leftBlend, leftGestureBlend);
    rightBlend = floatMin(rightBlend, rightGestureBlend); 
  };

  if (leftBlend <= 0 && rightBlend <= 0)
    return;

  // find item in hands
  if (!_itemInHands) return;
  const InventoryItemType *type = _itemInHands->Type();
  if (!type) return;

  // check if animation wants to apply IK
  const MoveInfoMan *info = Type()->GetMoveInfo(_moves.GetPrimaryMove(vs._moves).id, AnimSubIx());
  if (!info || info->_weaponIK <= 0) return;

  // find the bone attached to this weapon
  SkeletonIndex boneIndex = Type()->_weaponBoneIndx;

  if (!SkeletonIndexValid(boneIndex)) return;

  Ref<AnimationRT> anim = type->GetHandAnim(lshape->GetSkeleton());
  if (!anim) return;

  AnimationRTIKParam par(anim, boneIndex);

  if (leftBlend > 0  )
    Type()->_leftArmIK.CombineTransform(matrix, lshape, level, leftBlend, par);

  if (rightBlend > 0)
    Type()->_rightArmIK.CombineTransform(matrix, lshape, level, rightBlend, par);  
}

void Man::HandIKPilot(const ManVisualState &vs, LODShape * lshape, int level, StaticArrayAuto<Matrix4>& matrix, 
                      SkeletonIndex leftHandBone, SkeletonIndex rightHandBone, SkeletonIndex leftLegBone,  SkeletonIndex rightLegBone,
                      const Matrix4 &leftHandIK, const Matrix4 &rightHandIK, const Matrix4 &leftLegIK, const Matrix4 &rightLegIK)
{
  PROFILE_SCOPE(pltIK);

  float leftBlendArm  = ValueTest(vs,&MoveInfoMan::LeftHandIK);
  float rightBlendArm = ValueTest(vs,&MoveInfoMan::RightHandIK);
  float leftBlendLeg  = ValueTest(vs,&MoveInfoMan::LeftLegIK);
  float rightBlendLeg = ValueTest(vs,&MoveInfoMan::RightLegIK);

  const ManType *manType=Type();

  const MoveInfoGestureMan *gestureInfo = manType->GetMoveInfoGesture(_gesture.GetPrimaryMove(vs._gestures).id);
  if (gestureInfo)
  {
    float gestureTime = _gesture.GetPrimaryTime(vs._gestures);
    float leftGestureBlend  = gestureInfo->LeftHandIK(gestureTime);
    float rightGestureBlend = gestureInfo->RightHandIK(gestureTime);
    leftBlendArm  = floatMin(leftBlendArm , leftGestureBlend);
    rightBlendArm = floatMin(rightBlendArm, rightGestureBlend); 
  };

  if (!SkeletonIndexValid(leftHandBone))  leftBlendArm  = 0.0f;
  if (!SkeletonIndexValid(rightHandBone)) rightBlendArm = 0.0f;
  if (!SkeletonIndexValid(leftLegBone))   leftBlendLeg  = 0.0f;
  if (!SkeletonIndexValid(rightLegBone))  rightBlendLeg = 0.0f;

  if (leftBlendArm <= 0 && rightBlendArm <= 0 && leftBlendLeg <= 0 && rightBlendLeg <= 0)
    return;

  // Arms transformation
  if ((leftBlendArm > 0  || rightBlendArm > 0 ))
  {
    if ( rightHandBone==leftHandBone)
    {
      AnimationRTIKParamPilot par(leftHandIK);

      if (leftBlendArm > 0 )
        manType->_leftArmIK.CombineTransform(matrix, lshape, level, leftBlendArm,  par);

      if (rightBlendArm > 0)
        manType->_rightArmIK.CombineTransform(matrix, lshape, level, rightBlendArm,  par );
    }
    else
    {
      if (leftBlendArm > 0)
      {
        AnimationRTIKParamPilot par(leftHandIK);
        manType->_leftArmIK.CombineTransform(matrix, lshape, level, leftBlendArm, par);
      }
      if (rightBlendArm > 0)
      {
        AnimationRTIKParamPilot par(rightHandIK);
        manType->_rightArmIK.CombineTransform(matrix, lshape, level, rightBlendArm, par ); 
      }
    }
  }

  // Legs transformation
  if (leftBlendLeg > 0  || rightBlendLeg > 0)
  {
    if ( rightLegBone==leftLegBone)
    {
      AnimationRTIKParamPilot par(leftLegIK);

      if (leftBlendLeg > 0 )
        manType->_leftLegIK.CombineTransform(matrix, lshape, level, leftBlendLeg, par ); 
      if (rightBlendLeg > 0 )
        manType->_rightLegIK.CombineTransform(matrix, lshape, level, rightBlendLeg, par ); 
    }
    else
    {
      if (leftBlendLeg > 0)
      {
        AnimationRTIKParamPilot par(leftLegIK);
        manType->_leftLegIK.CombineTransform(matrix, lshape, level, leftBlendLeg, par ); 
      }
      if (rightBlendLeg > 0)
      {
        AnimationRTIKParamPilot par(rightLegIK);
        manType->_rightLegIK.CombineTransform(matrix, lshape, level, rightBlendLeg, par ); 
      }
    }
  }
}

#endif

void Man::GetBlendAnims(const ManVisualState &vs, BlendAnims &blend) const
{
  _moves.GetBlendAnims(vs._moves,blend,GetMovesType());

  if (vs._gestureFactor>0)
  {
    // we need to provide a mask for the gesture
    BLEND_ANIM(maskTgt);
    const BlendAnimSelections *mask = GetGestureMask(vs,maskTgt);
    if (mask && mask->Size()>0)
    {
      _gesture.GetBlendAnims(vs._gestures,blend,GetGesturesType(),vs._gestureFactor,mask);
    }
  }
}

void Man::GetBlendAnimsNeutral(const ManVisualState &vs, BlendAnims &blend) const
{
  _moves.GetBlendAnimsNeutral(vs._moves,blend,GetMovesType());

  #if 0
  // gestures do not affect neutral position
  BLEND_ANIM(aiming);
  if (_gesture)
  {
    const BlendAnimSelections &aimingRes = GetAiming(aiming);
    _gesture->GetBlend(vs._gestures,blend,_gestureTime,1,&aimingRes);
  }
  #endif

}

#if _ENABLE_PHYSICS
void Man::PushRagdollMx(Matrix4Array& matrices, const Shape *shapeL, LODShape *shape)
{
	/*
	Skeleton *skeleton = GetShape()->GetSkeleton();
  Assert(skeleton);

	if(!_ragdollCtrl)
		return;
	
	int nBones = skeleton->NBones();
  //matrices.Resize(nBones);

	// get blend anims
	BlendAnims blendAnims;
  GetBlendAnimsNeutral(RenderVisualState(), blendAnims);
  
	for (int i = 0; i<matrices.Size(); ++i)
  {
    SkeletonIndex si = SetSkeletonIndex(i);
    
    const SubSkeletonIndexSet& subSet = shapeL->GetSubSkeletonIndexFromSkeletonIndex(si);
    bool takenFromSubSkeleton = false;
    
		for(int i = 0; i < matrices.Size(); ++i)
		{
			enf::BulletBoneController* rb = GetRagdollBone(_ragdollCtrl, i);
			if(rb)
			{
				matrices[i] = rb->m_BoneMx;
				LogF("bone %i set", i);
				takenFromSubSkeleton = true;
			}
		}
		if (!takenFromSubSkeleton)
    {
      // matrix not contained in subskeleton -> take it from blendanims
      matrices[i].SetIdentity();
			
      Soldier* solObj = dynamic_cast<Soldier *>(this);
			blendAnims.DoBoneAnim(matrices[i], skeleton, si);
		}
		matrices[i].Orthogonalize();
  }
*/
}
#endif

bool Man::PrepareShapeDrawMatrices(Matrix4Array &matrices, const ObjectVisualState &vs, LODShape *shape, int level)
{
  const ManVisualState &mvs = vs.cast<Man>();
  
  BlendAnims blend;
  GetBlendAnims(mvs,blend);
  _moves.PrepareMatrices(GetMovesType(),matrices,shape,level,blend);

  if (matrices.Size()>0)
  {
    const ManType *type=Type();

    // Get the shape and make sure it already exists
    DoAssert(shape->IsLevelLocked(level));
    const Shape *shapeL = shape->GetLevelLocked(level);

#if _ENABLE_PHYSICS1
		if(_ragdollCtrl)
    {
			PushRagdollMx(matrices, shapeL, shape);
			return true;
		}

#endif
#if _ENABLE_HAND_IK 
    Object *parent = GetHierarchyParent();
    if (!parent)
    {
      // for free soldier use weapon IK
      HandIK(mvs, shape, level, matrices);
    }
    else
    {
      // in vehicle use pilot stick/driving wheel
      const Transport *vehicle = dyn_cast<Transport>(parent);
      if (vehicle)
      {
        if (this == vehicle->Driver()) 
        { 
          const TransportType *vehType = vehicle->Type();
          const TransportVisualState &tvs = vehicle->RenderVisualState();
          if (!tvs._IKIdent)
            HandIKPilot(mvs, shape, level, matrices, vehType->_driverLeftHandIndex, vehType->_driverRightHandIndex,vehType->_driverLeftLegIndex, vehType->_driverRightLegIndex,
              tvs._leftHandIK, tvs._rightHandIK, tvs._leftLegIK, tvs._rightLegIK);
        }
        else
        {
          TurretContextV context;
          if (vehicle->FindTurret(unconst_cast(vehicle->RenderVisualState()), this, context) && context._turretType)
          {
            const TurretType *trtType = context._turretType;
            const TurretVisualState * tvs = context._turretVisualState;
          
            if (!tvs->_IKIdent)
              HandIKPilot(mvs, shape, level, matrices, trtType->_gunnerLeftHandIndex, trtType->_gunnerRightHandIndex, trtType->_gunnerLeftLegIndex, trtType->_gunnerRightLegIndex,
                tvs->_leftHandIK, tvs->_rightHandIK, tvs->_leftLegIK, tvs->_rightLegIK);
          }
        }
      }
    }
#endif

    // Animate finger on trigger
    if (type->_pullTrigger.NotNull())
    {
      if (EnableTest(mvs,&MoveInfoMan::CanPullTrigger) && (!EnableTest(mvs,&MoveInfoMan::WeaponsDisabled)))
      {
        const MoveInfoGestureMan *gestureInfo = Type()->GetMoveInfoGesture(_gesture.GetPrimaryMove(mvs._gestures).id);
        if ((!gestureInfo) || (gestureInfo->CanPullTrigger()))
        {
          float state = GetTriggerPullingState();
          //if (state>0)
          {
            for (int i = 0; i < matrices.Size(); i++)
            {
              SubSkeletonIndex ssi;
              SetSubSkeletonIndex(ssi, i);
              SkeletonIndex si = shapeL->GetSkeletonIndexFromSubSkeletonIndex(ssi);
              Matrix4 transform = type->_pullTrigger->GetMatrix((1.0f - state) * 0.1f, si);
              #if 0 // never failed since added init, _DEBUG || _PROFILE
              DoAssert(transform.IsFinite());
              #endif
              matrices[i] = matrices[i] * transform;
            }
          }
        }
      }
    }

    if (type->_headAxisPoint>=0 && !mvs._headTransIdent)
    {
      BLEND_ANIM(head);
      const BlendAnimSelections &headRes = GetHead(mvs,head);
      AnimationRT::CombineTransform(
        shape,level,
        matrices,mvs._headTrans,
        headRes.Data(),headRes.Size()
        );
    }
    if (type->_leaningAxisPoint>=0 && !mvs._leaningTransIdent)
    {
      BLEND_ANIM(leaning);
      const BlendAnimSelections &leaningRes = GetLeaning(mvs,leaning);
      AnimationRT::CombineTransform(
        shape,level,
        matrices,mvs._leaningTrans,
        leaningRes.Data(),leaningRes.Size()
      );
    }
    
    AIBrain *unit = Brain();
    if (unit && type->_leaningAxisPoint>=0 && !mvs._shakingTransIdent && !unit->IsFreeSoldier())
    {
      BLEND_ANIM(leaning);
      const BlendAnimSelections &leaningRes = GetLeaning(mvs,leaning);
      AnimationRT::CombineTransform(
        shape,level,
        matrices,mvs._shakingTrans,
        leaningRes.Data(),leaningRes.Size()
        );
    }
    if (type->_aimingAxisPoint>=0 && !mvs._gunTransIdent)
    {
      {
        BLEND_ANIM(aiming);
        const BlendAnimSelections &aimingRes = GetAiming(mvs,aiming);
        AnimationRT::CombineTransform(
          shape,level,
          matrices,mvs._gunTrans,
          aimingRes.Data(),aimingRes.Size()
        );
      }
      {
        BLEND_ANIM(aimingBody);
        const BlendAnimSelections &aimingBodyRes = GetAimingBody(mvs,aimingBody);
        AnimationRT::CombineTransform(
          shape,level,
          matrices,mvs._gunBodyTrans,
          aimingBodyRes.Data(),aimingBodyRes.Size()
        );
      }
    }
 
    // ortho-normalization must be done before legs animation, as skew denormalizes
    #if 0 //!_FP2
      // note: ortho-normalization may do very bad artifacts
      // on bones which contain a lot of rotation - this is common for OFP2
      if (this==GWorld->CameraOn())
      {
        // visual artifacts on our soldier are quite important
        for (int i=0; i<matrices.Size(); i++)
        {
          matrices[i].Orthogonalize();
        }
      }
    #endif



    BLEND_ANIM(legs);
    const BlendAnimSelections &legsRes = GetLegs(mvs,legs);

    // apply legs selections
    AnimationRT::CombineTransform(
      shape,level,
       matrices,mvs._legTrans,
      legsRes.Data(),legsRes.Size()
    );

    // Get the head bones
    if (GetSkeletonIndex(type->_headBone) >= 0)
    {
      const SubSkeletonIndexSet &ssis = shapeL->GetSubSkeletonIndexFromSkeletonIndex(type->_headBone);
      if (ssis.Size() > 0)
      {
#if !_ENABLE_NEWHEAD
        _head.PrepareShapeDrawMatrices(type->_head, matrices, shape, level, _isDead, matrices[GetSubSkeletonIndex(ssis[0])]);
#endif
        // No matrix replication here, no matrix setting here - we only use already set matrix

#if _ENABLE_NEWHEAD

        // Get the head transformation matrix
        Matrix4Par headOrigin = matrices[GetSubSkeletonIndex(ssis[0])];

        // Go through all the bones with "head" parent and set them the head bone
        // This is necessary, because some bones need not to be set during the grimace process
        // and we need them to be on proper place
        {
          for (int i = 0; i < shapeL->GetSubSkeletonSize(); i++)
          {
            SubSkeletonIndex ssi;
            SetSubSkeletonIndex(ssi, i);
            SkeletonIndex si = shapeL->GetSkeletonIndexFromSubSkeletonIndex(ssi);
            if (shape->GetSkeleton()->GetParent(si) == type->_headBone)
            {
              matrices[i] = headOrigin;
            }
          }
        }

        // Set grimace
        {
          if ((_head._dstGrimace >= 0) && (_head._srcGrimace >= 0))
          {
            // Get the eye direction in world space
            TurretContextEx context;
            bool valid = GetPrimaryGunnerTurret(unconst_cast(mvs), context);
            (void)valid;DoAssert(valid);
            Vector3 eyeDirectionWorld = GetEyeDirectionWanted(mvs, context);
            // Convert the eye direction into the model space
            Vector3 eyeDirModel = mvs.DirectionWorldToModel(eyeDirectionWorld);
            // Get the adaptation factor
            float eyeAccom = GScene->MainLight()->EyeAdaptation(GetRenderVisualStateAge(), eyeDirectionWorld, 0.3, 0.5, true);

            if (_headType.NotNull()) _headType->PrepareGrimaceMatrices(matrices, _head, _headType, shapeL, headOrigin, eyeDirModel, eyeAccom, _head._dstGrimace, _head._srcGrimace, _head._factorGrimace);
          }
        }

#endif

      }
    }

    return true;
  }
  else
  {
    return false;
  }
}

template<>
const EnumName *GetEnumNames(ManAction dummy) {return ManActionNames;}

static const EnumName ManPosNames[]=
{
  EnumName(ManPosDead,"Dead"),
  EnumName(ManPosWeapon,"Weapon"),
  EnumName(ManPosBinocLying,"BinocLying"),
  EnumName(ManPosLyingNoWeapon,"LyingNoWeapon"),
  EnumName(ManPosLying,"Lying"),
  EnumName(ManPosHandGunLying,"HandGunLying"),
  EnumName(ManPosCrouch,"Crouch"),
  EnumName(ManPosHandGunCrouch,"HandGunCrouch"),
  EnumName(ManPosCombat,"Combat"),
  EnumName(ManPosHandGunStand,"HandGunStand"),
  EnumName(ManPosStand,"Stand"),
  EnumName(ManPosSwimming,"Swimming"),
  EnumName(ManPosNoWeapon,"NoWeapon"),
  EnumName(ManPosBinoc,"Binoc"),
  EnumName(ManPosBinocStand,"BinocStand"),
  EnumName()
};

template<>
const EnumName *GetEnumNames(ManPos dummy) {return ManPosNames;}


// Load and check curve for IK blending
static void CreateIKCurve(AutoArray<float> &handIKCurve,ParamEntryPar entry ,const RString name)
{
  ParamEntryVal handIKCurveVal = entry >> name;
  if (!handIKCurveVal.IsArray()) return;

  int size = handIKCurveVal.GetSize();
  handIKCurve.Realloc(size);
  handIKCurve.Resize(size);

  if (size>=2 && size<4)
    RptF("Warning: %s, wrong size (size<2||size>=4) in %s",(const char*)name, (const char*)(entry>>"file").operator RStringB());
  if (size>=4 && (size%2!=0))
    RptF("Warning: %s, size is not n*2 in %s",(const char*)name, (const char*)(entry>>"file").operator RStringB());

  for (int i = 0; i < size; i++)
  {
    handIKCurve[i] = handIKCurveVal[i];
  }
}

DEFINE_FAST_ALLOCATOR(MoveInfoManCommon)

MoveInfoManCommon::MoveInfoManCommon(MotionType *motion, ParamEntryPar entry)
: base(motion, entry)
{
  _disableWeapons = entry>>"disableWeapons";
  _enableOptics = entry>>"enableOptics";
  _showWeaponAim = entry>>"showWeaponAim";
  _forceAim = entry>>"forceAim";
  _enableMissile = entry>>"enableMissile";
  _enableBinocular = entry>>"enableBinocular";

  _showItemInHand = entry>>"showItemInHand";
  _showItemInRightHand = entry>>"showItemInRightHand";
#if _ENABLE_DATADISC
  _showHandGun = entry>>"showHandGun";
#else
  _showHandGun = false;
#endif

  _canPullTrigger = entry>>"canPullTrigger";

  _headBobStrength = entry>>"headBobStrength";
  _headBobMode = (HeadBobModeEnum)(int)(entry>>"headBobMode");
  if ((_headBobStrength > Moves::_primaryFactor0) && (_headBobMode == HeadBobUndef))
  {
    RptF("Warning: unset head bob mode in animation %s with strength %g, changed to begin/end alignment", (const char*)(entry>>"file").operator RStringB(), _headBobStrength);
    _headBobMode = HeadBobBeginEnd;
  }

#if _ENABLE_HAND_IK
  CreateIKCurve(_rightHandIKCurve,entry, "rightHandIKCurve");
  CreateIKCurve(_leftHandIKCurve,entry,"leftHandIKCurve");

  ParamEntryPtr test = entry.FindEntry("rightLegIKCurve");
  if (test) CreateIKCurve(_rightLegIKCurve,entry, "rightLegIKCurve");

  test = entry.FindEntry("leftLegIKCurve");
  if (test) CreateIKCurve(_leftLegIKCurve,entry,"leftLegIKCurve");

#endif

  ParamEntryPtr const forceUnarmedE = entry.FindEntry("forceUnarmed");
  _forceUnarmed = forceUnarmedE ? (*forceUnarmedE) : false;

  //TODO: move here _limitGunMovement = entry>>"limitGunMovement";
  VONDiags.Clear();
}

void MoveInfoManCommon::LoadAsSubclass(MotionType *motion, ParamEntryPar entry, ParamEntryPar actionMaps)
{
  base::LoadAsSubclass(motion, entry, actionMaps);

#if _ENABLE_HAND_IK
  ConstParamEntryPtr ptr = entry.FindEntry("rightHandIKCurve");
  if (ptr) CreateIKCurve(_rightHandIKCurve,entry, "rightHandIKCurve");
  ptr = entry.FindEntry("leftHandIKCurve");
  if (ptr) CreateIKCurve(_leftHandIKCurve,entry,"leftHandIKCurve");
  ptr = entry.FindEntry("rightLegIKCurve");
  if (ptr) CreateIKCurve(_rightLegIKCurve,entry, "rightLegIKCurve");
  ptr = entry.FindEntry("leftLegIKCurve");
  if (ptr) CreateIKCurve(_leftLegIKCurve,entry,"leftLegIKCurve");
#endif
  //RptF("### MoveInfoManCommon::LoadAsSubclass");
}

struct ActionContextForceUnarmed : public ActionContextBase
{
  typedef ActionContextBase base;

  ActionContextForceUnarmed() {}

  virtual LSError Serialize(ParamArchive &ar) override
  {
    RString type("ForceUnarmed");
    if (ar.IsSaving()) 
      CHECK(ar.Serialize("type", type, 1))
    CHECK(ar.Serialize("function", (int &)function, 1, MFNull))
    return LSOK;
  }

  virtual void Do(Man *man) override {man->ForceUnArmed();base::Do(man);}

  USE_FAST_ALLOCATOR;
};

ActionContextBase *MoveInfoManCommon::GetAutoContext() const
{
  if (_forceUnarmed) 
    return new ActionContextForceUnarmed();
  else return base::GetAutoContext();
}

DEFINE_FAST_ALLOCATOR(MoveInfoMan)

#if _ENABLE_HAND_IK
// find and calculate position in curve and return blend factor
float HandIKFromArray(const AutoArray<float> &iKCurve,  float time )  
{
  int size = iKCurve.Size();
  if (size==0) return 0; // most of calling this function ends here or on next line.
  if (size==1) return iKCurve[0];
  Assert(size>=4);
  Assert(size%2==0);
  float lastX = iKCurve[0];
  float lastY = iKCurve[1];
  if (time<lastX) return lastY;
  for (int c = 2;c < size; c+=2)
  {
    float x = iKCurve[c];
    float y = iKCurve[c+1];
    Assert(lastX<x);

    if (time<x && time>=lastX) 
    {
      float invDelta = 1/(x-lastX);
      return y*((time-lastX)*invDelta)+lastY*((x-time)*invDelta);
    }
    lastX = x;
    lastY = y;
  }
  return lastY;    
};

float MoveInfoManCommon::LeftHandIK( float time ) const  
{
  return HandIKFromArray(_leftHandIKCurve,time);
};
float MoveInfoManCommon::RightHandIK( float time ) const 
{
  return HandIKFromArray(_rightHandIKCurve,time);
};

float MoveInfoManCommon::LeftLegIK( float time ) const  
{
  return HandIKFromArray(_leftLegIKCurve,time);
};
float MoveInfoManCommon::RightLegIK( float time ) const 
{
  return HandIKFromArray(_rightLegIKCurve,time);
};

#endif

MoveInfoMan::MoveInfoMan(MotionType *motion, ParamEntryPar entry)
: base(motion, entry), _camShakeFire(1.0f), _subclassInherit(false)
{
  RStringB file = entry>>"file";
  if (file.GetLength()>0)
  {
    Ref<Skeleton> skeleton = motion->GetSkeleton();

    RStringB collCapsuleFile = entry>>"collisionShape";    
    
    _collShape = CollisionCapsules.New(collCapsuleFile);
    _collShape->FindMapping(skeleton->GetCollVertexPattern(), skeleton->GetCollGeomCompPattern());
    bool hasSafeCapsule = (bool)(entry>>"hasCollShapeSafe");

    if (hasSafeCapsule)
    {
      RStringB collCapsuleSafeFile = entry>>"collisionShapeSafe";
      _collShapeSafe = CollisionCapsules.New(collCapsuleSafeFile);
      _collShapeSafe->FindMapping(skeleton->GetCollVertexPattern(), skeleton->GetCollGeomCompPattern());
    }
  }

  _disableWeaponsLong = entry>>"disableWeaponsLong";
  _enableAutoActions = entry>>"enableAutoActions";
  _enableDirectControl = entry>>"enableDirectControl";
  _onLandBeg = entry>>"onLandBeg";
  _onLandEnd = entry>>"onLandEnd";
  _onLadder = entry>>"onLadder";
  _duty = entry>>"duty";

  //_aimingRigid = entry>>"aimingRigid";

  // load aiming blending information
  ParamEntryVal blendAnimTypes = motion->GetEntry()>>"BlendAnims";
  RStringB aimingName = entry>>"aiming";
  RStringB aimingBodyName = entry>>"aimingBody";
  RStringB leaningName = entry>>"leaning";
  RStringB legsName = entry>>"legs";
  RStringB headName = entry>>"head";
  _aiming = motion->NewBlendAnimType(blendAnimTypes>>aimingName);
  _aimingBody = motion->NewBlendAnimType(blendAnimTypes>>aimingBodyName);
  _leaning = motion->NewBlendAnimType(blendAnimTypes>>leaningName);
  _legs = motion->NewBlendAnimType(blendAnimTypes>>legsName);
  _head = motion->NewBlendAnimType(blendAnimTypes>>headName);

  _leaningFactorBeg = entry>>"leaningFactorBeg";
  _leaningFactorEnd = entry>>"leaningFactorEnd";
  _leaningFactorZeroPoint = entry>>"leaningFactorZeroPoint";

  _variantAfterMin = (entry>>"variantAfter")[0];
  _variantAfterMid = (entry>>"variantAfter")[1];
  _variantAfterMax = (entry>>"variantAfter")[2];
  _boundingSphere = entry>>"boundingSphere";

  //_neutralHeadXRot = float(entry>>"neutralHeadXRot")*(H_PI/180.0f);
  _limitGunMovement = entry>>"limitGunMovement";

  _aimPrecision = entry>>"aimPrecision";
  _camShakeFire = entry.ReadValue("camShakeFire", 1.0f);

  _visibleSize = entry>>"visibleSize";

#if _ENABLE_HAND_IK
  _weaponIK = entry >> "weaponIK";
#endif

  _canBlendStep = entry >> "canBlendStep";
  RStringB bodyPosition = entry.ReadValue("bodyPosition", RStringBEmpty);
  if(strcmpi(bodyPosition, "down") == 0)
    _bodyPosition = VPDown;
  else if(strcmpi(bodyPosition, "prone") == 0)
    _bodyPosition = VPProne;
  else if(strcmpi(bodyPosition, "kneel") == 0)
    _bodyPosition = VPKneel;
  else if(strcmpi(bodyPosition, "stand") == 0)
    _bodyPosition = VPStand;
  else
    _bodyPosition = 0;

  LoadVariants(_variantsPlayer,NULL,motion,entry,"variantsPlayer");
  LoadVariants(_variantsAI,&_variantsPlayer,motion,entry,"variantsAI");

  ParamEntryVal actionMaps = motion->GetEntry()>>"Actions";
  InitAnimSubclasses(motion, entry, actionMaps);
}

bool MoveInfoMan::Preload(ParamEntryPar entry)
{
  bool done = MoveInfoBase::Preload(entry);
  RStringB file = entry>>"file";
  if( file.GetLength()>0 )
  {
    RStringB collCapsuleFile = entry >> "collisionShape";
    if (!LODShape::Preload(FileRequestPriority(1), collCapsuleFile)) done = false;

    bool hasSafeCapsule = (bool) (entry >> "hasCollShapeSafe");
    if (hasSafeCapsule)
    {
      RStringB collCapsuleSafeFile = entry >> "collisionShapeSafe";
      if (!LODShape::Preload(FileRequestPriority(1), collCapsuleSafeFile)) done = false;
    }
  }

  return done;
}

float MoveInfoMan::LeaningFactor(float time) const
{
  if ((_leaningFactorZeroPoint >= 0) && (_leaningFactorZeroPoint <= 1))
  {
    if (time < _leaningFactorZeroPoint)
    {
      float t = time  / _leaningFactorZeroPoint;
      return (1 - t) * _leaningFactorBeg;
    }
    else
    {
      float t = (time - _leaningFactorZeroPoint) / (1 - _leaningFactorZeroPoint);
      return t * _leaningFactorEnd;
    }
  }
  else
  {
    return _leaningFactorBeg*(1-time)+_leaningFactorEnd*time;
  }
}

MoveInfoMan::SpeedRange MoveInfoMan::GetStepSpeedRange() const
{
  // handle static animations to make sure we do not return out-of-range values
  if (GetOriginalSpeed()>1e9f) return MoveInfoMan::SpeedRange(0);
  float base = _move.NotNull() ? -_move->GetStepLength()*GetOriginalSpeed() : 1;
  if (base>=0) return SpeedRange(base*_relSpeedMin,base*_relSpeedMax);
  else return SpeedRange(base*_relSpeedMax,base*_relSpeedMin);
}

MoveInfoMan::SpeedRange MoveInfoMan::GetStepSpeedRangeX() const
{
  // handle static animations to make sure we do not return out-of-range values
  if (GetOriginalSpeed()>1e9f) return MoveInfoMan::SpeedRange(0);
  float base = _move.NotNull() ? -_move->GetStepLengthX()*GetOriginalSpeed() : 1;
  if (base>=0) return SpeedRange(base*_relSpeedMin,base*_relSpeedMax);
  else return SpeedRange(base*_relSpeedMax,base*_relSpeedMin);
}

void MoveInfoMan::LoadVariants(
  AutoArray<MoveVariant> &vars, AutoArray<MoveVariant> *defaultVars,
  MotionType *motion, ParamEntryPar entry, RString member
) const
{
  ParamEntryVal cfg = entry >> member;

  // check first name
  // if the array is {""}, use default variants (if any)
  if (cfg.GetSize()==1)
  {
    RStringB single = cfg[0];
    if (single.GetLength()==0)
    {
      if (defaultVars)
      {
        vars = *defaultVars;
        return;
      }
      else
      {
        RptF("No default vars in %s",(const char *)entry.GetContext(member));
        return;
      }
    }
  }


  float totalP = 0;
  for (int i=0; i<cfg.GetSize(); i+=2)
  {
    RStringB name = cfg[i];
    float prop = i + 1 < cfg.GetSize() ? cfg[i + 1] : floatMax(1 - totalP, 0);
    MoveVariant &var = vars[vars.Add()];
    var._move = motion->GetMoveId(name);
    var._probab = prop;
    totalP += prop;
  }

  // check if given probabilities are valid (ignore rounding problems)
  if (totalP > 1.0005f)
  {
    RptF("Animation config %s - wrong sum of probabilities (%.3f)", cc_cast(entry.GetContext(member)), totalP);
  }
}

MoveId MoveInfoMan::RandomVariant( const AutoArray<MoveVariant> &vars, float rnd ) const
{
  int i=0;
  for (;i<vars.Size(); i++)
  {
    float prop = vars[i]._probab;
    rnd -= prop;
    if (rnd<=0) break;
  }
  if (i>=vars.Size()) return MoveIdNone;
  return vars[i]._move;
}

float MoveInfoMan::GetVariantAfter() const
{
  return GRandGen.Gauss(_variantAfterMin,_variantAfterMid,_variantAfterMax);
}

MoveId MoveInfoMan::RandomVariantPlayer() const
{
  float rnd = GRandGen.RandomValue();
  return RandomVariant(_variantsPlayer,rnd);
}

MoveId MoveInfoMan::RandomVariantAI() const
{
  float rnd = GRandGen.RandomValue();
  return RandomVariant(_variantsAI,rnd);
}

/** Function replaces GeomtryLevel in pLODShapeTo by _collShape or _collShapeSafe.
 @param pLodShapeTo (in/out) the shape where will be geometry level changed.
 @param vertexMapping (in) the mapping between vertexes in pLodShapeTo and pattern CollisionVertexPattern.
 @param geomCompMapping (in) the mapping between convex comp in pLodShapeTo and CollisionGeomCompPattern.
 @param bUseSafe (in) if true replace by _collShapeSafe otherwise by _collShape.
 @return true when safe capsule was used
 */
bool MoveInfoMan::AnimateCollGeom(
  AnimationContext &animContext, const Shape *shape,
  const Mapping& vertexMapping, const Mapping& geomCompMapping, bool bUseSafe
) const
{
  // no need to lock - capsules are persistent in the memory

  // Replace
  if (!bUseSafe)
  {  
    if (_collShape.IsNull())
    {
      RptF("No collision shape found in %s", _move ? cc_cast(_move->GetName().name) : "noname") ;
      return false;
    }

    _collShape->ReplaceGeometryLevel(animContext, shape, vertexMapping, geomCompMapping);  
    return false;
  }
  else
  {
    if (_collShapeSafe.IsNull())
    {
      RptF("No safe collision shape found in %s", _move ? cc_cast(_move->GetName().name) : "noname");
      return false;
    }

    _collShapeSafe->ReplaceGeometryLevel(animContext, shape, vertexMapping, geomCompMapping);    
    return true;
  }
}

void MoveInfoMan::LoadAsSubclass(MotionType *motion, ParamEntryPar entry, ParamEntryPar actionMaps)
{
  base::LoadAsSubclass(motion, entry, actionMaps);

  // redefine: WeaponIK
  ConstParamEntryPtr ptr;
#if _ENABLE_HAND_IK
  ptr = entry.FindEntry("WeaponIK");
  if (ptr) _weaponIK = *ptr;
#endif

  // redefine: limitGunMovement
  ptr = entry.FindEntry("limitGunMovement");
  if (ptr) _limitGunMovement = *ptr;

  //RptF("### MoveInfoMan::LoadAsSubclass, %s - %s looped", _move->Name(), _move->GetLooped() ? "" : "NOT");
}

void MoveInfoMan::InitAnimSubclasses(MotionType *motion, ParamEntryPar entry, ParamEntryPar actionMaps)
{
  ConstParamEntryPtr ptr = entry.FindEntryNoInheritance("subclassInherit");
  if (ptr) _subclassInherit = *ptr;

  _subAnim.Realloc(motion->NSubClasses());
  _subAnim.Resize(motion->NSubClasses());

  for (int ix=0; ix<_subAnim.Size(); ix++)
  {
    if (_subclassInherit) ptr = entry.FindEntry(motion->GetSubClassName(ix));
    else ptr = entry.FindEntryNoInheritance(motion->GetSubClassName(ix));
    if (ptr && ptr->IsClass())
    {
      _subAnim[ix] = new MoveInfoMan(*this); //copy constructor
      // note: _subAnim members inside _subAnim are have no usage and should be NULLs
      if (_subAnim[ix]) _subAnim[ix]->LoadAsSubclass(motion, *ptr, actionMaps);
    }
  }
  // the absence of InjuredHeavy should make the system fall back to InjuredLight, and only if that's missing, to the default state
  int ixHeavy = motion->_animEnumToSubIx[MotionType::ASInjuredHeavy];
  if (ixHeavy>=0 && ixHeavy<_subAnim.Size() && !_subAnim[ixHeavy])
  {
    int ixLight = motion->_animEnumToSubIx[MotionType::ASInjuredLight];
    if (ixLight>=0 && ixLight<_subAnim.Size())
      _subAnim[ixHeavy] = _subAnim[ixLight];
  }
}

DEFINE_FAST_ALLOCATOR(MoveInfoGestureMan)

MoveInfoGestureMan::MoveInfoGestureMan(MotionType *motion, ParamEntryPar entry)
: base(motion, entry)
{
  // load aiming blending information
  ParamEntryVal blendAnimTypes = motion->GetEntry()>>"BlendAnims";
  RStringB maskName = entry>>"mask";
  _mask = motion->NewBlendAnimType(blendAnimTypes>>maskName);
}


#if _ENABLE_CONVERSATION

struct KBTopicName
{
  RStringB _topicId;
  RStringB _className;

  KBTopicName(const RStringB &topicId, const RStringB &className)
    : _topicId(topicId), _className(className) {}
};

template <>
struct BankTraits<KBTopicExt>: public DefBankTraits<KBTopicExt>
{
  // default name is character
  typedef KBTopicName NameType;
  // default name comparison
  static int CompareNames(NameType n1, NameType n2)
  {
    // both parts of name need to match
    int val = strcmpi(n1._topicId, n2._topicId);
    if (val != 0) return val;
    return strcmpi(n1._className, n2._className);
  }
};

template <>
struct BankTraitsExt< BankTraits<KBTopicExt> > // : public DefBankTraitsExt< BankTraits<KBTopicExt> >
{
  typedef BankTraits<KBTopicExt> Traits;
  typedef  Traits::Type Type;
  typedef Traits::NameType NameType;
  typedef Traits::ContainerType ContainerType;
  /// perform a search in the container
  static Type *Find(const ContainerType &container, NameType name)
  {
    for( int i=0; i<container.Size(); i++ )
    {
      Type *ii = container.Get(i);
      if( ii && !Traits::CompareNames(GetName(ii),name) ) return ii;
    }
    return NULL;
  }

  /// add an item into the container
  static void AddItem(ContainerType &container, Type *item)
  {
    container.AddReusingNull(item);
  }
  /// delete an item with given name
  /** @return false if not found */
  static bool DeleteItem(ContainerType &container, NameType name)
  {
    for( int i=0; i<container.Size(); i++ )
    {
      Type *ii = container.Get(i);
      if( ii && !Traits::CompareNames(GetName(ii),name) )
      {
        container.Delete(i);
        return true;
      }
    }
    return false;
  }

	/// create an object based on the name
  static Type *Create(NameType name)
  {
    ParamEntryVal topicCfg = Pars >> "CfgTalkTopics" >> name._className;
    RStringB fsmName = topicCfg>>"react";
    GameValue eventHandler;
    if (topicCfg.FindEntry("reactPlayer"))
    {
      // reactPlayer contain the script name, precompile it and pass to the topic
      RString handler = topicCfg >> "reactPlayer";
      RString FindScript(RString name);
      RString filename = FindScript(handler);
      if (filename.GetLength() > 0)
      {
        FilePreprocessor preproc(true);
        QOStrStream processed;
        if (preproc.Process(&processed, filename))
        {
          RString expression(processed.str(), processed.pcount());
#if USE_PRECOMPILATION
          eventHandler = GameValue(new GameDataCode(&GGameState, expression, GWorld->GetMissionNamespace())); // mission namespace
#else
          eventHandler = GameValue(expression);
#endif
        }
      }
    }
    KBTopicExt *topic = new KBTopicExt();
    topic->Init(name._topicId, RString("#") + name._className, fsmName, eventHandler); // mark topic to read content from the config
    return topic;
  }
  /// get a name
	static NameType GetName(const Type *item)
  {
    RString topicId = item->GetName();
    RString filename = item->GetFilename();
    DoAssert(filename.GetLength() > 0);
    DoAssert(filename[0] == '#');
    return NameType(topicId, filename.Substring(1, INT_MAX));
  }
};

static BankArray<KBTopicExt> TalkTopics;
#endif

static inline Matrix4 Matrix4FromMatrix3(Matrix3Par orient)
{
  Matrix4 m;
  m.SetOrientation(orient);
  m.SetPosition(VZero);
  return m;
}


ManVisualState::ManVisualState(const ManType & type)
:base(type),
_gestureFactor(0),
_cameraPositionWorld(VZero),_aimingPositionWorld(VZero),
_gunTrans(MIdentity),_gunBodyTrans(MIdentity),_leaningTrans(MIdentity),_headTrans(MIdentity),_lookTrans(MIdentity),
_gunTransIdent(true),_headTransIdent(true),_leaningTransIdent(true),_gettingInOut(false),
_legTrans(MIdentity),
_shakingTransIdent(true),_shakingTrans(MIdentity),
_headXShiftWantedCont(0),_headYShiftWantedCont(0),_headZShiftWantedCont(0),_headZRotWantedCont(0),
_headXShiftWanted(0), _headYShiftWanted(0), _headZShiftWanted(0), _headZRotWanted(0)

{

}

void ManVisualState::Interpolate(Object *obj, const ObjectVisualState & t1state, float t, ObjectVisualState& interpolatedResult) const
{
  Interpolate(static_cast<Man *>(obj),static_cast_checked<const ManVisualState &>(t1state),t,static_cast_checked<ManVisualState &>(interpolatedResult));
}

static void RecomputeAxis(Matrix4 &trans, Vector3Par axis)
{
  Matrix4 headOrient(MTranslation,axis);
  Matrix4 headOrientInv(MTranslation,-axis);

  trans = headOrient*Matrix4FromMatrix3(trans.Orientation())*headOrientInv;
}

static Matrix3 Slerp(Matrix3 const& t0, Matrix3 const& t1, float t)
{
  Matrix3 interpolated; // always used to that NRVO can be applied
  if (t0 != t1)
  {
    Quaternion<float> q0(t0), q1(t1);  // convert to quaternions, lerp, normalize, convert back to matrices
    q0.Slerp(q1,t).UnitToMatrix(interpolated);
    return interpolated;
  }
  else
  {  // copy closer state
    interpolated = t1;
    return interpolated;
  }

}

static Matrix4 Slerp(Matrix4 const& t0, Matrix4 const& t1, float t)
{
  Matrix4 interpolated; // always used to that NRVO can be applied
  interpolated.SetPosition(t0.Position()*t+ t1.Position() *(1-t));  // no equality test - it's slower
  if (t0.Orientation() != t1.Orientation())
  {
    Quaternion<float> q0(t0.Orientation()), q1(t1.Orientation());
    Matrix3 interpolated3;
    q0.Slerp(q1,t).UnitToMatrix(interpolated3);
    interpolated.SetOrientation(interpolated3);
    return interpolated;
  }
  else
  {  // copy closer state
    interpolated.SetOrientation(t1.Orientation());
    return interpolated;
  }
}

void ManVisualState::Interpolate(Man *man, const ManVisualState & t1state, float t, ManVisualState& interpolatedResult) const
{
  base::Interpolate(man, t1state,t,interpolatedResult);
  _moves.Interpolate(t1state._moves,t,interpolatedResult._moves);
  _gestures.Interpolate(t1state._gestures,t,interpolatedResult._gestures);
#define INTERPOLATE(x) interpolatedResult.x = x + t*(t1state.x - x)
  INTERPOLATE(_gestureFactor);

  if (CHECK_DIAG(DEAnimation) && CHECK_DIAG(DEVisualStates) && GWorld->CameraOn()==man)
  {
    DIAG_MESSAGE_ID(2000,0,"T0: " + _moves.DiagText());
    DIAG_MESSAGE_ID(2000,1,Format ("+ %.2f x T1:",t) + t1state._moves.DiagText());
    DIAG_MESSAGE_ID(2000,2," = I:" + interpolatedResult._moves.DiagText());
  }

  interpolatedResult._cameraPositionWorld = _cameraPositionWorld*(1-t)+t1state._cameraPositionWorld*t;
  interpolatedResult._aimingPositionWorld = _aimingPositionWorld*(1-t)+t1state._aimingPositionWorld*t;
  
  // note: instead of interpolating matrices we might interpolate source values instead and recompute matrices from them
  // this way also axis recomputation would not be necessary
  interpolatedResult._gunTrans = Slerp(_gunTrans,t1state._gunTrans,t);
  interpolatedResult._gunBodyTrans = Slerp(_gunBodyTrans,t1state._gunBodyTrans,t);
  interpolatedResult._gunTransIdent = _gunTransIdent && t1state._gunTransIdent;

  interpolatedResult._headTrans = Slerp(_headTrans,t1state._headTrans,t);
  interpolatedResult._headTransIdent = _headTransIdent && t1state._headTransIdent;

  interpolatedResult._lookTrans = Slerp(_lookTrans,t1state._lookTrans,t);
  interpolatedResult._legTrans = _legTrans*(1-t)+t1state._legTrans*t;

  interpolatedResult._leaningTrans = Slerp(_leaningTrans,t1state._leaningTrans,t);
  interpolatedResult._leaningTransIdent = _leaningTransIdent && t1state._leaningTransIdent;

  interpolatedResult._shakingTrans = _shakingTrans*(1-t)+t1state._shakingTrans*t;
  interpolatedResult._shakingTransIdent = _shakingTransIdent && t1state._shakingTransIdent;

  RecomputeAxis(interpolatedResult._gunTrans,man->GetWeaponCenterMoves(interpolatedResult));
  RecomputeAxis(interpolatedResult._gunBodyTrans,man->GetLeaningCenterMoves(interpolatedResult));

  Vector3 headAxis = man->GetHeadCenterMoves(interpolatedResult);
  RecomputeAxis(interpolatedResult._headTrans,headAxis);
  RecomputeAxis(interpolatedResult._lookTrans,headAxis);
  RecomputeAxis(interpolatedResult._leaningTrans,man->GetLeaningCenterMoves(interpolatedResult));

  INTERPOLATE(_headXShiftWantedCont);
  INTERPOLATE(_headYShiftWantedCont);
  INTERPOLATE(_headZShiftWantedCont);
  INTERPOLATE(_headZRotWanted);
  INTERPOLATE(_headZRotWantedCont);
  INTERPOLATE(_headXShiftWanted);
  INTERPOLATE(_headYShiftWanted);
  INTERPOLATE(_headZShiftWanted);

  #undef INTERPOLATE

  interpolatedResult._gettingInOut = _gettingInOut && t1state._gettingInOut;
}


ManType::ManType( ParamEntryPar param )
:VehicleType(param)
{
  _headHit = -1;
  _bodyHit = -1;
  _handsHit = -1;
  _legsHit = -1;
#if _VBS3
  _viewPortCentered = false;
#endif 
}

void ManType::Load(ParamEntryPar par, const char *shape)
{
  base::Load(par,shape);

  _isMan = par>>"isMan";
  _minGunElev=(float)(par>>"minGunElev")*(H_PI/180);
  _maxGunElev=(float)(par>>"maxGunElev")*(H_PI/180);
  _minGunTurn=(float)(par>>"minGunTurn")*(H_PI/180);
  _maxGunTurn=(float)(par>>"maxGunTurn")*(H_PI/180);
  _minGunTurnAI=(float)(par>>"minGunTurnAI")*(H_PI/180);
  _maxGunTurnAI=(float)(par>>"maxGunTurnAI")*(H_PI/180);

  _minTriedrElev=-30*(H_PI/180);
  _maxTriedrElev=+60*(H_PI/180);
  _minTriedrTurn=-30*(H_PI/180);
  _maxTriedrTurn=+30*(H_PI/180);

  _minHeadTurnAI = (float)(par>>"minHeadTurnAI")*(H_PI/180);
  _maxHeadTurnAI = (float)(par>>"maxHeadTurnAI")*(H_PI/180);

  _canHideBodies = par>>"canHideBodies";
  _canDeactivateMines = par>>"canDeactivateMines";

  _useInternalLODInVehicles = par >> "useInternalLODInVehicles";

  _lyingLimitSpeedHiding = par >> "lyingLimitSpeedHiding";
  _crouchProbabilityHiding = par >> "crouchProbabilityHiding";
  _lyingLimitSpeedCombat = par >> "lyingLimitSpeedCombat";
  _crouchProbabilityCombat = par >> "crouchProbabilityCombat";
  _lyingLimitSpeedStealth = par >> "lyingLimitSpeedStealth";

  _maxTurnAngularVelocity = par >> "maxTurnAngularVelocity";
  _costTurnCoef = par >> "costTurnCoef";

#if _VBS3
  _viewPortCentered = par.ReadValue("viewPortCentered",false);
#endif

#if !_ENABLE_NEWHEAD
  _head.Load(par);
#endif

  // _moves will be initialized during InitShape

  _hitSound.Load(par,"hitSounds");
  _vegSound.Load(par, "vegetationSounds");

  GetValue(_addSound, par >> "additionalSound");

  _woman = par >> "woman";
  _genericNames = par >> "genericNames";
  _faceType = par >> "faceType";
  _glassesEnabled = par >> "glassesEnabled";
  _canCrawl = !_woman; // this is only a default, better detection is done once animations are loaded

  ParamEntryVal array = par >> "identityTypes";
  int n = array.GetSize();
  _identityTypes.Resize(0);
  _identityTypes.Realloc(n);
  for (int i=0; i<n; i++) _identityTypes.AddUnique(array[i]);

  // set HW animation type
  // check if the skinning engine is powerfull enough
  if (GEngine->GetMaxBonesCount()>=30)
  {
    _shapeAnimated = AnimTypeHardware;
  }

#if _ENABLE_NEWHEAD
  _boneHead = par >> "boneHead";
  _boneHeadCutScene = par >> "boneHeadCutScene";
  _boneLEye = par >> "boneLEye";
  _boneREye = par >> "boneREye";
  _boneLEyelidUp = par >> "boneLEyelidUp";
  _boneREyelidUp = par >> "boneREyelidUp";
  _boneLEyelidDown = par >> "boneLEyelidDown";
  _boneREyelidDown = par >> "boneREyelidDown";
  _boneLPupil = par >> "boneLPupil";
  _boneRPupil = par >> "boneRPupil";
#endif

#if _ENABLE_CONVERSATION
  ParamEntryVal talk = par >> "TalkTopics";
  if (talk.GetClassInterface())
  {
    Ref<KBCenter> kbCenter;
    for (ParamClass::Iterator<> it = ConstParamEntryPtr(talk.GetClassInterface()); it; ++it)
    {
      ParamEntryVal entry = *it;
      // name ... topic id
      // value ... name of class in CfgTalkTopics where topic is defined
      RString topicId = entry.GetName(); 
      if (kbCenter)
      {
        // if topic is already defined, do not change it by one defined in the base class
        if (kbCenter->GetTopic(topicId, KBCenter::ParentList())) continue;
      }
      KBTopicName name(topicId, entry);
      // load from CfgTalkTopics
      Ref<KBTopicExt> topic = TalkTopics.New(name);
      if (!kbCenter) kbCenter = new KBCenter;
      kbCenter->AddTopic(topic);
    }
    if (kbCenter) _kbCenter = kbCenter.GetRef();
  }
#endif

  LoadBreathSounds(par, "SoundBreath");
  LoadDamageHitSounds(par, "HitDamage");

  for (int i=0; i<NInventorySlotId; i++)
  {
    if (i == ISHands) continue; // temporary invisible slot
    RString emptySlotName = RString("empty") + InventorySlotTypes.GetName(i);
    // Older config.bin files do not define the new optional Shoulder2 slot.
    // Do not ask ParamEntry for a missing key: that emits a fatal config
    // warning during SurvivorMale initialization and can abort startup.
    RString typeName;
    if (par.CheckIfEntryExists(emptySlotName)) typeName = par >> emptySlotName;
    if (!typeName.IsEmpty()) _emptySlots[i] = dynamic_cast<InventoryItemType *>(VehicleTypes.New(typeName));
  }

  for(int n = 0; n < VPCount; n++)
  {
    _poseAudibility[n] = 1;
    _poseVisibility[n] = 1;
  }
  if(par.CheckIfEntryExists("audibility"))
  {
    ParamEntryVal aud = par >> "audibility";
    if(aud.IsClass())
    {
      for(int n = 0; n < aud.GetEntryCount(); n++)
      {
        RStringB name = aud.GetEntry(n).GetName();
        if(strcmpi(name, "down") == 0)
          _poseAudibility[VPDown] = aud.GetEntry(n);
        else if(strcmpi(name, "prone") == 0)
          _poseAudibility[VPProne] = aud.GetEntry(n);
        else if(strcmpi(name, "kneel") == 0)
          _poseAudibility[VPKneel] = aud.GetEntry(n);
        else if(strcmpi(name, "stand") == 0)
          _poseAudibility[VPStand] = aud.GetEntry(n);
      }
    }
  }
  if(par.CheckIfEntryExists("visibility"))
  {
    ParamEntryVal vid = par >> "visibility";
    if(vid.IsClass())
    {
      for(int n = 0; n < vid.GetEntryCount(); n++)
      {
        RStringB name = vid.GetEntry(n).GetName();
        if(strcmpi(name, "down") == 0)
          _poseVisibility[VPDown] = vid.GetEntry(n);
        else if(strcmpi(name, "prone") == 0)
          _poseVisibility[VPProne] = vid.GetEntry(n);
        else if(strcmpi(name, "kneel") == 0)
          _poseVisibility[VPKneel] = vid.GetEntry(n);
        else if(strcmpi(name, "stand") == 0)
          _poseVisibility[VPStand] = vid.GetEntry(n);
      }
    }
  }
}

void ManType::LoadDamageHitSounds(ParamEntryPar base, const char *name)
{
  ParamEntryVal entry = base >> name;

  if (entry.IsClass())
  {
    // number of damage/hit groups
    int n = entry.GetEntryCount();

    // no entry in class
    if (n == 0) return;

    _damageHitSound.Realloc(n);
    _damageHitSound.Resize(n);
    
    for (int i = 0; i < n; i++)
    {
      ParamEntryVal subClass = entry.GetEntry(i);

      if (subClass.IsClass())
      {
        // load single group
        _damageHitSound[i].Load(subClass);
      }
    }
  }
}

void ManType::LoadBreathSounds(ParamEntryPar base, const char *name)
{
  ParamEntryVal breathEntry = base >> name;

  if (!breathEntry.IsClass()) return;

  int n = breathEntry.GetEntryCount();

  // breath groups - each group of breathing is created by different person
  _breathSound.Realloc(n);
  _breathSound.Resize(n);

  for (int i = 0; i < n; i++)
  {
    ParamEntryVal groupEntry = breathEntry.GetEntry(i);

    if (groupEntry.IsArray() && groupEntry.GetSize() != 0)
    {
      LoadBreathVarinat(groupEntry, _breathSound[i]);
    }
  }
}

void ManType::LoadBreathVarinat(ParamEntryPar groupEntry, BreathSound &bSound)
{
  // for each group of samples
  AutoArray<ProbSound> &groupSound = bSound._group;

  int n = groupEntry.GetSize();

  // variants of breath, usually 3 variants which differ by speed
  groupSound.Realloc(n);
  groupSound.Resize(n);

  for (int i = 0; i< n; i++)
  {
    const IParamArrayValue &item = groupEntry[i];
    int m = item.GetItemCount();

    // breathing samples for one variant of speed
    AutoArray<SoundProbab> &breathPars = groupSound[i]._pars;
    breathPars.Realloc(m);
    breathPars.Resize(m);

    // samples in sub array
    for (int j = 0; j < m; j++)
    {
      const IParamArrayValue &subItem = item[j];

      // sample + probability
      if (subItem.GetItemCount() == 2) 
      {
        GetValue(breathPars[j], subItem[0]);
        breathPars[j]._probability = subItem[1];
      }
    }
  }
}

void SoundHitDamage::Load(ParamEntryPar entry)
{
  // class contain two arrays: hitSounds[] & damageSounds[]
  if (entry.GetEntryCount() >= 1)
  {
    // hit sound samples
    ParamEntryPar hitArr = entry.GetEntry(0);
    
    if (hitArr.IsArray())
    {
      // items in array
      int hitArrSize = hitArr.GetSize();

      AutoArray<SoundProbab> &aar = _hitSounds._pars;
      aar.Realloc(hitArrSize);
      aar.Resize(hitArrSize);

      // foreach subarray
      for (int i = 0; i < hitArrSize; i++)
      {
        // {{soundPars}, float}
        const IParamArrayValue &subItem = hitArr[i];

        if (subItem.GetItemCount() == 2)
        {
          GetValue(aar[i], subItem[0]);
          aar[i]._probability = subItem[1];
        }
      }
    }
  }

  if (entry.GetEntryCount() >= 2)
  {
    // damage sounds
    ParamEntryPar damageArr = entry.GetEntry(1);

    if (damageArr.IsArray())
    {
      // items in array
      int damageArrSize = damageArr.GetSize();

      for (int i = 0; i < damageArrSize; i++)
      {
        const IParamArrayValue &subItem = damageArr[i];

        // {"name", {"sample_name", volume, freq, prob, min, mid, max}}
        if (subItem.GetItemCount() == 2)
        {
          RString name = subItem[0];
          int index = -1;

          // try find existing group
          for (int j = 0; j < _damageSounds.Size(); j++)
          {
            // name == SoundDamageEx.name
            if (strcmpi(cc_cast(_damageSounds[j]._name), cc_cast(name)) == 0)
            {
              index = j;
              break;
            }
          }

          // group not found - create new
          if (index == -1)
          {
            SoundDamageEx &sdex = _damageSounds.Append();
            sdex._name = name;
            index = _damageSounds.Size() - 1;
          }

          const IParamArrayValue &secondPart = subItem[1];
          if (secondPart.GetItemCount() >= 8)
          {
            // {"sample_name", volume, freq, listDistance, prob, min, mid, max} 
            Assert(index >= 0 && index < _damageSounds.Size());

            AutoArray<SoundDamage> &pars = _damageSounds[index]._pars;

            SoundDamage &sd = pars.Append();
            GetValue(sd, subItem[1]);
            sd._probability = secondPart[4];
            sd._min = secondPart[5];
            sd._mid = secondPart[6];
            sd._max = secondPart[7];
          }
        }
      }

      for (int i = 0; i < _damageSounds.Size(); i++)
        _damageSounds[i]._pars.Compact();

      _damageSounds.Compact();
    }
  }
}

const SoundPars &ManType::GetBreathSound(int breathSelector, float selector) const
{
  if (_breathSound.Size() > 0)
  {
    Assert((breathSelector >= 0) && (breathSelector < _breathSound.Size()));

    if ((breathSelector >= 0) && (breathSelector < _breathSound.Size()))
    {
      // select breath group
      const AutoArray<ProbSound> &groupArr = _breathSound[breathSelector]._group;
      int n = groupArr .Size();

      if (n > 0)
      {
        // select variant of breath
        int index = selector * (n - 1);
        saturate(index, 0, n - 1);

        const AutoArray<SoundProbab> &parsArr = groupArr[index]._pars;
        int m = parsArr.Size();
        if (m > 0)
        {
          float val = GRandGen.RandomValue();

          // assume sort of probabilities
          for (int i = 0; i < (m - 1); i++)
          {
            val -= parsArr[i]._probability;
            if (val < 0) { return parsArr[i]; }
          }
          return parsArr[m - 1];
        }
      }
    }
  }

  return _mainSound;
}

static const char *GetSelectionName(ParamEntryPar entry)
{
  if (entry.IsArray())
  {
    RString name = entry[0];
    return name;
  }

  RString name = entry.GetValue();
  return name;
}

static const char *GetSelectionAltName(ParamEntryPar entry)
{
  if (entry.IsArray())
  {
    RString name = entry[1];
    return name;
  }

  return NULL;
}

#define SELECTION_NAMES(entry) GetSelectionName(entry), GetSelectionAltName(entry)

static inline RString StrToLower(RString str)
{
  str.Lower();
  return str;
}

/*!
\patch_internal 1.01 Date 6/26/2001 by Ondra. Fixed: getting NVG proxy index.
*/

void ManType::InitShape()
{
  base::InitShape();

  ParamEntryVal par=*_par;

  const Shape *mem=_shape->MemoryLevel();
  Assert( mem );
  
  _pilotPoint = mem ? _shape->PointIndex(mem,(par >> "memoryPointPilot").operator RString()) : -1;

  // find matrix index of gun proxy

  // search in any animation RT
  Ref<WoundInfo> woundInfo = new WoundInfo;
  woundInfo->Load(_shape,par>>"wounds");
  // TODO: share texture list between wounds
  _headWound.Init(_shape,woundInfo,SELECTION_NAMES(par >> "selectionHeadWound"));
  _bodyWound.Init(_shape,woundInfo,SELECTION_NAMES(par >> "selectionBodyWound"));
  _lArmWound.Init(_shape,woundInfo,SELECTION_NAMES(par >> "selectionLArmWound"));
  _rArmWound.Init(_shape,woundInfo,SELECTION_NAMES(par >> "selectionRArmWound"));
  _lLegWound.Init(_shape,woundInfo,SELECTION_NAMES(par >> "selectionLLegWound"));
  _rLegWound.Init(_shape,woundInfo,SELECTION_NAMES(par >> "selectionRLegWound"));

  _personality.Init(_shape,SELECTION_NAMES(par >> "selectionPersonality"));
  _headHide.Init(_shape,SELECTION_NAMES(par >> "selectionHeadHide"));

  _insideView=-1;
  //_layDownView=0;
  //_crawlView=0;
  //_launcherView=0;

  _stepLIndex=mem ? _shape->PointIndex(mem,(par >> "memoryPointLStep").operator RString()) : -1;
  _stepRIndex=mem ? _shape->PointIndex(mem,(par >> "memoryPointRStep").operator RString()) : -1;

  _aimPoint=-1;
  _camTgt = -1;
  _aimingAxisPoint=-1;
  _leaningAxisPoint=-1;
  _headAxisPoint=-1;
  _handGrenadePoint=-1;
  _leftHandPoint = -1;
  _rightHandPoint = -1;
  if( mem )
  {
    _aimPoint = _shape->PointIndex(mem,(par >> "memoryPointAim").operator RString());
    if (_aimPoint < 0)
    {
      LogF("No aim point in %s",(const char *)GetName());
    }
    _camTgt = _shape->PointIndex(mem,(par >> "memoryPointCameraTarget").operator RString());
    if (_camTgt < 0)
    {
      LogF("Warning: No memoryPointCameraTarget specified for model %s, using memoryPointAim instead", GetName().Data());
      _camTgt = _aimPoint;
    }

    _aimingAxisPoint=_shape->PointIndex(mem,(par >> "memoryPointAimingAxis").operator RString());
    _leaningAxisPoint=_shape->PointIndex(mem,(par >> "memoryPointLeaningAxis").operator RString());
    _headAxisPoint=_shape->PointIndex(mem,(par >> "memoryPointHeadAxis").operator RString());
    _handGrenadePoint=_shape->PointIndex(mem,(par>>"memoryPointHandGrenade").operator RString());
    
    if (_headAxisPoint<0) _headAxisPoint = _aimingAxisPoint;

    _leftHandPoint = _shape->PointIndex(mem, "lefthand");
    _rightHandPoint = _shape->PointIndex(mem, "righthand");
  }

  _insideView = _shape->FindSpecLevel(VIEW_PILOT);
  if (_insideView<0 && _shape->FindSimplestLevel()>=0) _insideView = _shape->FindSimplestLevel();

  RStringB moveName = par>>"moves";

  _moveType = MovesTypesMan.New(MovesTypeName(moveName));
  _gestureType = _moveType->GetGestures();

  _shape->SkeletonAssigned(_moveType->GetSkeleton());
  
  WeightInfoName wName;
  wName.shape = _shape.GetRef();
  wName.skeleton = _moveType->GetSkeleton();

  // prepare skeleton for all lods
  //_moveType->PrepareSkeleton(_shape);
  
#if _ENABLE_NEWHEAD
  // Remember eye bone indices
  _lEye = _moveType->GetSkeleton()->FindBone(_boneLEye);
  _rEye = _moveType->GetSkeleton()->FindBone(_boneREye);
  _lEyelidUp = _moveType->GetSkeleton()->FindBone(_boneLEyelidUp);
  _rEyelidUp = _moveType->GetSkeleton()->FindBone(_boneREyelidUp);
  _lEyelidDown = _moveType->GetSkeleton()->FindBone(_boneLEyelidDown);
  _rEyelidDown = _moveType->GetSkeleton()->FindBone(_boneREyelidDown);
  _lPupil = _moveType->GetSkeleton()->FindBone(_boneLPupil);
  _rPupil = _moveType->GetSkeleton()->FindBone(_boneRPupil);
#endif

  _handsIndex = -1; // index of corresponding proxy

#if !_ENABLE_NEWHEAD
  _headBone = _moveType->GetSkeleton()->FindBone(_head._bone);
  if (GetSkeletonIndex(_headBone) < 0)
  {
    if (_isMan)
    {
      ErrF("Bone '%s' not found in %s",(const char *)_head._bone,(const char *)_shape->GetName());
    }
  }
  _head.InitShape(par, _shape);
#else
  _headBone = _moveType->GetSkeleton()->FindBone(_boneHead);
  _headBoneCutScene = _moveType->GetSkeleton()->FindBone(_boneHeadCutScene);
#endif
 
  _headHit = FindHitPoint("HitHead");
  _bodyHit = FindHitPoint("HitBody");
  _handsHit = FindHitPoint("HitHands");
  _legsHit = FindHitPoint("HitLegs");

  _commonDamagePoint = -1;
  if (mem)
  {
    _commonDamagePoint = _shape->PointIndex(mem, (par >> "memoryPointCommonDamage").operator RString());
  }
/*
  const Shape *hitShape = _shape->HitpointsLevel();
  if (hitShape)
  {
    RString name = par >> "hitPointCommonDamage";
    int selIndex = _shape->FindNamedSel(hitShape,name);
    if (selIndex >= 0)
    {
      const NamedSelection &sel = hitShape->NamedSel(selIndex);
      if (sel.Size() > 0) _commonDamagePoint = sel[0];
    }
  }
*/

  // Find mapping to patterns
  FindCollisionMapping();
  
#if _ENABLE_HAND_IK
  _weaponBoneIndx = _shape->FindBone(StrToLower(par >> "weaponBone"));
  // Prepare data for IK
  _leftArmIK.Init(_shape, false, par);
  _rightArmIK.Init(_shape, true, par);
  _leftLegIK.Init(_shape, false, par);
  _rightLegIK.Init(_shape, true, par);
  if (!_leftArmIK.Valid() || !_rightArmIK.Valid())
  {
    _weaponBoneIndx = SkeletonIndexNone;
  }
#endif

  // Load the _pullTrigger RTM
  DoAssert(_shape->GetSkeleton() == _moveType->GetSkeleton());
  AnimationRTName rtmName;
  //rtmName.name = "ca\\anims\\characters\\data\\anim\\sdr\\trigger.rtm";
  rtmName.name = par >> "triggerAnim";
  if (!rtmName.name.IsEmpty())
  { // avoiding: "Animation  not found or empty"
    rtmName.skeleton = _moveType->GetSkeleton();
    rtmName.reversed = true;
    rtmName.streamingEnabled = false;
    _pullTrigger = new AnimationRT(rtmName);
  }

  MoveId lyingNoWeapon = _moveType->GetDefaultMove(ManPosLyingNoWeapon);
  MoveId standNoWeapon = _moveType->GetDefaultMove(ManPosNoWeapon);

  _canCrawl = lyingNoWeapon!=standNoWeapon && lyingNoWeapon!=MoveIdNone;
}

void ManType::DeinitShape()
{
  // Destroy the _pullTrigger RTM
  _pullTrigger.Free();

  _headWound.Unload();
  _bodyWound.Unload();
  _lArmWound.Unload();
  _rArmWound.Unload();
  _lLegWound.Unload();
  _rLegWound.Unload();

  base::DeinitShape();

  _shape->SkeletonUnassigned(_moveType->GetSkeleton());
  _moveType = NULL; 
}

Object* ManType::CreateObject(bool fullCreate) const
{ 
  return new Soldier(this, fullCreate);
}

#if _DEBUG
void ManType::OnUsed()
{
  base::OnUsed();
}

void ManType::OnUnused()
{
  base::OnUnused();
}
#endif


void ManType::InitShapeLevel(int level)
{
  base::InitShapeLevel(level);

  if (level == _shape->FindMemoryLevel())
  {
    // find the proxy for an item in hands
    for (int i=0; i<GetProxyCountForLevel(level); i++)
    {
      const ProxyObjectTyped &proxy = GetProxyForLevel(level,i);
      Object *proxyObj = proxy.obj;
      if (!proxyObj) continue;
      const EntityType *proxyType = proxyObj->GetEntityType();
      if (!proxyType) continue;
      if (proxyType->GetInventorySlot() == ProxyInventoryType::_hands)
      {
        // found
        _handsIndex = i;
        break;
      }
    }
  }

#if !_ENABLE_NEWHEAD
  _head.InitShapeLevel(_shape,level);
#endif
  
  _personality.InitLevel(_shape,level);

  // get some texture from personality
  // note: there should be only one personality texture
  if (!_faceTextureOrig) _faceTextureOrig = _personality.GetTexture(_shape);

  _headHide.InitLevel(_shape,level);

  _headWound.InitLevel(_shape,level);
  _bodyWound.InitLevel(_shape,level);
  _lArmWound.InitLevel(_shape,level);
  _rArmWound.InitLevel(_shape,level);
  _lLegWound.InitLevel(_shape,level);
  _rLegWound.InitLevel(_shape,level);
  
  if (level==_shape->FindSpecLevel(VIEW_PILOT))
  {
    ShapeUsed shape=_shape->Level(level);
    shape->MakeCockpit();
  }
  if (level==_shape->FindSpecLevel(VIEW_GUNNER))
  {
    ShapeUsed shape=_shape->Level(level);
    shape->MakeCockpit();
  }

}

void ManType::DeinitShapeLevel(int level)
{
#if !_ENABLE_NEWHEAD
  _head.DeinitShapeLevel(_shape,level);
#endif
  _personality.DeinitLevel(_shape,level);
  _headHide.DeinitLevel(_shape,level);

  _headWound.DeinitLevel(_shape,level);
  _bodyWound.DeinitLevel(_shape,level);
  _lArmWound.DeinitLevel(_shape,level);
  _rArmWound.DeinitLevel(_shape,level);
  _lLegWound.DeinitLevel(_shape,level);
  _rLegWound.DeinitLevel(_shape,level);
  
  base::DeinitShapeLevel(level);
}

ActionMap *ManType::GetActionMap( MoveId move, int subIx) const
{
  ActionMap *map = _moveType->GetActionMap(move, subIx);
  if (!map) map = _moveType->GetNoActions();
  return map;
}

void ManType::FindCollisionMapping()
{
  const Shape * shape = _shape->GeometryLevel();
  if (shape == NULL)
  {
    return;
  }

  if (_moveType.IsNull())
    return;

  Skeleton * skelet = _moveType->GetSkeleton();
  if (skelet == NULL)
    return;

  const FindArray<RStringB>& vertexPattern = skelet->GetCollVertexPattern();

  int n = vertexPattern.Size();
  _vertexCollMapping.Realloc(n);
  _vertexCollMapping.Resize(n);
  for(int i = 0; i < n;i++)
  {
    int iSel = _shape->FindNamedSel(shape,vertexPattern[i]);

    if (iSel < 0)
    {
      RptF(
        "Wrong vertex mapping for person collision geometry found in %s. Selection %s not found",
        _shape->Name(), cc_cast(vertexPattern[i])
      );
    }

    const NamedSelection::Access sel(shape->NamedSel(iSel),_shape,shape);
    _vertexCollMapping[i] = (iSel == -1) ? -1 : sel[0];
  }

  const FindArray<int>& geomCompPattern = skelet->GetCollGeomCompPattern();

  n = geomCompPattern.Size();
  _geomCompCollMapping.Realloc(n);
  _geomCompCollMapping.Resize(n);

  const ConvexComponents& comp = _shape->GetGeomComponents();
  for(int i = 0; i < n; i++)
  {
    int compName = geomCompPattern[i];
    int compIndex = comp.FindNameIndex(compName);
    if (compIndex < 0)
    {
      RptF("Wrong geometry convex component mapping  for person collision geometry found in %s. Convex component number %d. not found", _shape->Name(), compName);
    }
    _geomCompCollMapping[i] = compIndex;
  }

  // Find bottom ConvexComponents
  // by definition that are components 1 and 2
  const static int bottomComponents[] = {1};
  int numberOfBottoms = lenof(bottomComponents);

  _bottomGeomComp.Clear();
  _bottomGeomComp.Realloc(numberOfBottoms);  
  for(int j = 0; j < numberOfBottoms; j++)
  {
    int ind = comp.FindNameIndex(bottomComponents[j]);
    if (ind >= 0 )
    {
      _bottomGeomComp.Add(ind);
    }
  }
  _bottomGeomComp.Compact();
}

#if _ENABLE_PHYSICS
enf::BulletRagdollController* Man::CreateRagdoll( void )
{

//  // do not control by player anymore
//	SetControllable(false);
//	// stop fire
//	DisableFire();

//	bool notFirstTime = CheckSpawnFlags(ESPAWN_RAGDOLL);
//	if( notFirstTime )
//		SetAnimator();

	// create ragdoll
  enf::BulletWorld* mworld = (enf::BulletWorld*)GetWorldPhysics();

  enf::BulletRagdollController* ragdoll = mworld->CreateRagdoll( this,
		                               "humanoidtest",
												            110.f,
												            EPL_RAGDOLL );


//  // enable dynamic bbox
//	enf::MeshObjectAnimatedInstance* inst = enf::MeshObjectAnimatedInstance::SafeCast(GetInstance());
//	inst->SetFlags(enf::VIF_DYNAMICBBOX);
//	inst->ClearFlags(VIF_TRACE_MODEL|VIF_TRACE_BONES);
//
//	// Set only frame and visible
//	ClearEventMask( EV_ALL );
	SetEventMask( EV_FRAME|EV_VISIBLE );
//
//	// expire flags
//	SetSpawnFlags(ESPAWN_RAGDOLL);
//
//	// reset shield if character is dead
//	SetShieldHealth(0.0f);

	// result
	return ragdoll;
}

void Man::RemovePhysics( void )
{
	if( GetPhysics())// && GetNetworkManager().IsPhysicsMaster() )
	{
		enf::PhysicsWorld* pworld = GetWorld()->GetPhysicsWorld(); 
		pworld->DestroyBody(this);
	};
}

void Man::CreatePhysics(EPhysicsLayers layer)
{
	return;
	if(GetNetworkManager().IsPhysicsMaster())
	{
		if(_pPhysics)
			RemovePhysics();

		enf::PhysicsWorld* pworld = GLandscape->GetPhysicsWorld();
		if(!pworld)
			return;

		Vector3 center;
		Vector3 minMax[2];
		BoundingInfoUI(center, minMax);

		const float radius = enf::enf_max(minMax[1].x - minMax[0].x, minMax[1].z - minMax[0].z) * 0.5;
		const Matrix4 mat = Matrix4(MTranslation, _shape->BoundingCenter());//BCenterPosition());

		const float height = minMax[1].y - minMax[0].y;
		enf::PhysicsGeometry* capsule = (enf::PhysicsGeometry*)new btCapsuleShape(radius, height);
		enf::PhysicsGeomDef* comp = new enf::PhysicsGeomDef(GetName(), (enf::PhysicsGeometry*)capsule, mat, 0, GWorld->GetPhysicsManager()->FindMaterial("material/default"), 8);
		pworld->CreateDynamicBody(this, Vector3(0.f), comp, 1, 0, 8);	//propadne to - tahle moznost to zlepsi || btCollisionObject::CF_NO_GRAVITY
  }

  return;

//  // remove previous
//	RemovePhysics();
//
//  if(GWorld->GetRealPlayer() == this)
//    return;
//
//#pragma message("odstran - panak a zombik bez fyziky")
//  static int eg = 0;
//  if(eg < 2)
//  {
//    eg++;
//    return;
//  }
//
//  // should be ragdoll ?
//  if(0)//CheckSpawnFlags(ESPAWN_RAGDOLL))
//	{
//		_ragdollCtrl = CreateRagdoll();
//    _ragdollMode = _ragdollCtrl ? 1 : 0;
//    GetPhysics()->SetActive(enf::AS_ACTIVE);
//    return;
//	};
//
//	// load object
//	//GetInstance()->SetFlags(VIF_TRACE_MODEL|VIF_TRACE_BONES);
//  LODShape *shape = GetShape();
//  if(shape)
//	{
//    //if( !IsClient() )
//		{
//			//float capsuleHeight = bbox.GetSize().z * .5f;
//      float boxSize = shape->BoundingSphere() * 0.70710678f * 0.5; //sqrt(0.5) * nezajima me podzemi none a sirce to jen prospeje
//
//			enf::PhysicsWorld* pworld = GetWorld()->GetPhysicsWorld();
//			enf::PhysicsGeomDef def;
//      def.m_Frame.SetTranslation(shape->BoundingCenter() + Vector3(0.f, boxSize, 0.f)); // need to subtract margin
//			// set interaction layer
//			def.m_iInteractionLayer = EPL_CHARACTER;
//			// physical material
//      //def.m_pMaterial = pworld->GetManager()->FindMaterial(GameConstants::GetMaterialInfo( m_pStyle->GetCharacterTemplate()->GetMaterialTemplate().GetEnum() )->GetMaterialName());
//			def.m_pMaterial = pworld->GetManager()->FindMaterial("material/ragdollflesh");
//			// capsule
//			//def.m_pGeom  = pworld->CreateCapsuleGeom( ct->GetCapsuleRadius(), enf::Vector3( 0, 0, capsuleHeight - ct->GetCapsuleRadius() ));
//      def.m_pGeom  = pworld->CreateBoxGeom(Vector3(boxSize));
//      
//      enf::PhysicsController* body = pworld->CreateDynamicBody( this, Vector3( 0.f ), &def, 1, GetMass());
//			body->SetInertiaTensor( Vector3(0.0f) ); // create infinite inertia tensor		
//			body->SetActive( enf::AS_INACTIVE );
//    };
//
//    GetPhysics()->SetActive(enf::AS_ACTIVE);
//    
//    // event mask
//	  if( !IsDead() )
//	  {
//		  SetEventMask( EV_FRAME|EV_TICK|EV_VISIBLE );
//
//		  //if( !IsClient() )
//			  SetEventMask( EV_SIMULATE );
//	  };
//
//   
//    // update position upon terrain
//		//if( !this->GetWorld()->IsEditMode() && !GetGame()->IsLoadingSave() && GetAlignToTerrain())
//		if(1)//!_swimming)
//    {
//      SnapToGround();
//    }
//
//  }
}
#endif

void Soldier::FakePilot( float deltaT, SimulationImportance prec )
{
  _turnWanted = 0;
  _walkSpeedFineControl = false;
  _holdBreath = 0;

  if (!SimulateForcedMove(prec))
  {
    if (_targetMoveWanted!=MoveIdNone)
    {
      //const ActionMap *map = GetMovesType()->GetActionMap(_moves.GetPrimaryMove(FutureVisualState()._moves).id);
      //if (map && map->GetName())
      MoveId oldQueue = _moves.GetDesiredMove();
      if (!ChangeMoveQueueSetStillEnd(MotionPathItem(_targetMoveWanted)) && oldQueue!=MoveIdNone)
      {
        // change move may fail when dying from variant animations
        // different variant animation may be chosen on remote and local versions
        // note: remote units select dying animation in the remote simulation
        const MoveInfoMan *info = GetMovesType()->GetMoveInfo(_targetMoveWanted, AnimSubIx());
        if (info && info->GetTerminal())
        { // the move received is a terminal animation
          MoveId primary = _moves.GetPrimaryMove(FutureVisualState()._moves).id;
          const MoveInfoBase *primInfo = GetMovesType()->GetMoveInfoBase(primary, AnimSubIx());
          if (!primInfo || !primInfo->GetTerminal())
          { // the current move is not a terminal animation
            ChangeMoveQueueSetStillEnd(MotionPathItem(oldQueue)); // restore the animation selected by us
          }
        }
      }
    }
  }
}

bool Man::SimulateForcedMove(SimulationImportance prec)
{
  // forced queue is present only where the script simulation is local (on the server), _movesPlayMove only remotely
  _moves.AdvanceForcedQueue();
  _gesture.AdvanceForcedQueue();
  if (_gesture.GetForceMove().id!=MoveIdNone)
  {
    if (_gesture.GetPrimaryMove(FutureVisualState()._gestures).id==MoveIdNone)
    {
      _gesture.SwitchMove(FutureVisualState()._gestures,_gesture.GetForceMove().id,nullptr);
    }
    else
    {
      bool animChanged;
      _gesture.ChangeMoveQueue(FutureVisualState()._gestures,GetGesturesType(), animChanged, _gesture.GetForceMove().id);
    }
  }

  if (_moves.GetForceMove().id!=MoveIdNone)
  {
    SetMoveQueue(_moves.GetForceMove(),prec<=SimulateVisibleFar);
    return true;
  }
  else
  {
    return false;
  }
}

void Soldier::SuspendedPilot(float deltaT, SimulationImportance prec)
{
  _holdBreath = 0;

  _turnWanted=0;
  _walkSpeedWantedX = 0;
  _walkSpeedWantedZ = 0;
  _walkSpeedFineControl = false;
  _leanZRotWanted = 0;
  _headXRotWantedCont = _headYRotWantedCont = _headXRotWantedPan = _headYRotWantedPan = 0;
  FutureVisualState()._headXShiftWantedCont = FutureVisualState()._headYShiftWantedCont = FutureVisualState()._headZShiftWantedCont = FutureVisualState()._headZRotWantedCont = 0;
  FutureVisualState()._headXShiftWanted = FutureVisualState()._headYShiftWanted = FutureVisualState()._headZShiftWanted = FutureVisualState()._headZRotWanted = 0;

  _headXRotWantedAuto = _headYRotWantedAuto = 0;

  // set primary move to stand

  if (!SimulateForcedMove(prec))
  {
    // stand still
    ActionMap *map = Type()->GetActionMap(_moves.GetPrimaryMove(FutureVisualState()._moves).id, AnimSubIx());
    MoveId moveWanted = map ? Type()->GetMove(map, ManActStop) : GetDefaultMove();
    SetTargetMove(moveWanted);
    SetMoveQueue(MotionPathItem(moveWanted),prec<=SimulateVisibleFar);
  }
}

bool Man::IsWeaponInMove() const
{
  int actPos = GetActUpDegree(FutureVisualState());
  return
  (
    actPos!=ManPosNoWeapon && actPos!=ManPosLyingNoWeapon && actPos!=ManPosSwimming
  );
}

bool Man::IsBinocularInMove() const
{
  int actPos = GetActUpDegree(FutureVisualState());
  return actPos==ManPosBinoc || actPos==ManPosBinocLying || actPos==ManPosBinocStand;
}

void Man::UnselectLauncher()
{
  if (!LauncherAnimSelected()) return;
  WeaponModePtr ptr = _weaponsState.FirstWeapon(this);
  if (ptr == _weaponsState.ValidatedCurrentWeapon()) ptr = WeaponModePtr();
  _weaponsState.SelectWeapon(this, ptr);
}

bool Man::IsLookingAround() const
{
  if (!_brain) return false;
  bool forceAim = ForceAim(RenderVisualState()); // looking around enforced
  return _brain->LookAroundEnabled() || forceAim || _ladderBuilding;
}

/*!
\patch 1.01 Date 6/25/2001 by Ondra.
- Fixed: reloading weapon on back disabled.
\patch 1.01 Date 6/25/2001 by Ondra.
- Fixed: turning enabled while reloading.
\patch_internal 1.04 Date 6/25/2001 by Ondra
- Fixed: wanted camera checked instead of actual when determining
if optics moves should be used.
\patch 1.05 Date 7/17/2001 by Ondra.
- New: Toggle walking mode with 'F'.
\patch 1.22 Date 8/30/2001 by Ondra.
- Fixed: Player character was sprinting when running up very steep hill.
- Improved: Walking is used when running up or down steep hill to slow down.
\patch 5139 Date 3/13/2007 by Bebul
- Fixed: Releasing Alt in a vehicle should center view vertically
*/
void Man::UpdateHeadLookAround(InputInterface *input, float deltaT, bool forceLookAround)
{
  //suppose continuous looking around is enabled always
  _headYRotWantedCont = H_PI*(input->GetAction(UALookLeftCont,false) - input->GetAction(UALookRightCont,false));
  _headXRotWantedCont = H_PI*(input->GetAction(UALookUpCont,false) - input->GetAction(UALookDownCont,false));
  bool isFreeHeadMove = (input->GetAction(UAFreeHeadMove) > 0.5f);
  if (!isFreeHeadMove)
  {
    if ( _brain->LookAroundEnabled() || forceLookAround )
    {
      if (_headXRotWantedPan == 0)
      {
        _headXRotWantedPan = _lookXRotWanted;
      }
      const float headSpeedFactor = 2;
      const float diveSpeedFactor = 2;
      const float diag = 0.5 * H_SQRT2;
      float headSpeed = (
        input->GetAction(UALookLeft)
        + input->GetAction(UALookLeftUp)*diag
        + input->GetAction(UALookLeftDown)*diag
        - input->GetAction(UALookRight)
        - input->GetAction(UALookRightUp)*diag
        - input->GetAction(UALookRightDown)*diag
        );
      _lookYRotWanted += deltaT*headSpeed*headSpeedFactor;
      //  if (_headYRotWanted!=0.0f) 
      //    LogF("_headYRotWanted=%.2f _headYRot=%.2f",_headYRotWanted,_headYRotWanted);
      float diveSpeed = (
        input->GetAction(UALookDown)
        + input->GetAction(UALookLeftDown)*diag
        + input->GetAction(UALookRightDown)*diag
        - input->GetAction(UALookUp)
        - input->GetAction(UALookRightUp)*diag
        - input->GetAction(UALookLeftUp)*diag
        );
      _lookXRotWanted -= deltaT*diveSpeed*diveSpeedFactor;
    }
    else
    {
      if (_headXRotWantedPan!=0)
      {
        //LookAroundEnabled just switched OFF, so we must recover to remembered PANNED value
        _lookXRotWanted = _headXRotWantedPan;
        _headXRotWantedPan = 0;
      }
      float heading=0, dive=0;
      GetHeadLookAroundActions(input, deltaT, heading, dive);
      _lookXRotWanted += dive;
      _headYRotWantedPan = heading;
      _lookYRotWanted = 0.0f;
    }
  }
  bool lookAround = (input->GetAction(UALookAround) > 0.5f);
  if (!lookAround && _zoom._lookAroundLocked>0) _zoom._lookAroundLocked--;
  else if (_zoom._lookAroundLocked) lookAround=false; //no lookAround while not released after freeLook activation
  if( input->GetActionToDo(UALookCenter,false) || (lookAround && _zoom._lookAroundToggleEnabled) )
    _lookXRotWanted = _lookYRotWanted = 0; //UALookAround when FreeLook toggled centralizes camera

  //@{ Update Head Movement Actions
  // Update only when CameraInternal is active (preserve selected values otherwise)
  if ( GWorld->GetCameraType()==CamInternal )
  {
    FutureVisualState()._headXShiftWantedCont = (input->GetAction(UALookShiftRightCont,false) - input->GetAction(UALookShiftLeftCont,false));
    saturate(FutureVisualState()._headXShiftWantedCont,-1.0f,1.0f);
    FutureVisualState()._headYShiftWantedCont = (input->GetAction(UALookShiftUpCont,false) - input->GetAction(UALookShiftDownCont,false));
    saturate(FutureVisualState()._headYShiftWantedCont,-1.0f,1.0f);
    FutureVisualState()._headZShiftWantedCont = (input->GetAction(UALookShiftForwardCont,false) - input->GetAction(UALookShiftBackCont,false));
    saturate(FutureVisualState()._headZShiftWantedCont,-1.0f,1.0f);
    FutureVisualState()._headZRotWantedCont = (input->GetAction(UALookRollRightCont,false) - input->GetAction(UALookRollLeftCont,false));
    saturate(FutureVisualState()._headZRotWantedCont ,-1.0f,1.0f);
    // UALookShiftLeft/Right/Forward/Backward/Up/Down works are independent on UAFreeHeadMove activation
    float headSpeedUpDown = input->GetAction(UALookShiftUp) - input->GetAction(UALookShiftDown);
    float headSpeedLR = input->GetAction(UALookShiftRight) - input->GetAction(UALookShiftLeft);
    float headSpeedFwdBack = input->GetAction(UALookShiftForward) - input->GetAction(UALookShiftBack);
    float headSpeedRoll = input->GetAction(UALookRollRight) - input->GetAction(UALookRollLeft);
    // When UAFreeHeadMove is activated, the UALookLeft/Right/Up/Down actions are mapped to MoveHead instead of looking around
    if ( isFreeHeadMove )
    {
      headSpeedLR += input->GetAction(UALookRight) - input->GetAction(UALookLeft);
      headSpeedFwdBack += input->GetAction(UALookUp) - input->GetAction(UALookDown);
      // mouse Head Movement actions
      AIBrain *brain = Brain();
      if (brain)
      {
        float aimX = input->GetActionWithCurveExclusive(InputInterface::CurveFov0p8,UAAimRight, UAAimLeft);  //Exclusive read not to collide with other mouse actions (namely mouse pilot in Heli)
        float aimY = input->GetActionWithCurveExclusive(InputInterface::CurveFov0p8,UAAimUp, UAAimDown);
        FutureVisualState()._headXShiftWanted += aimX;
        FutureVisualState()._headZShiftWanted += aimY;
      }
    }

    static float SpeedFactor = 0.6f;
    FutureVisualState()._headXShiftWanted += deltaT*headSpeedLR*SpeedFactor; saturate(FutureVisualState()._headXShiftWanted,-1.0f,1.0f);
    FutureVisualState()._headYShiftWanted += deltaT*headSpeedUpDown*SpeedFactor; saturate(FutureVisualState()._headYShiftWanted,-1.0f,1.0f);
    FutureVisualState()._headZShiftWanted += deltaT*headSpeedFwdBack*SpeedFactor; saturate(FutureVisualState()._headZShiftWanted,-1.0f,1.0f);
    saturateMax(FutureVisualState()._headYShiftWanted, 0);  //negative values not allowed atm
    saturateMax(FutureVisualState()._headZShiftWanted, 0);  //negative values not allowed atm
    FutureVisualState()._headZRotWanted += deltaT*headSpeedRoll*SpeedFactor; saturate(FutureVisualState()._headZRotWanted,-1.0f,1.0f);
    if( input->GetActionToDo(UALookShiftCenter,false) )
    {
      FutureVisualState()._headXShiftWanted = FutureVisualState()._headYShiftWanted = FutureVisualState()._headZShiftWanted = FutureVisualState()._headZRotWanted = 0;
    }
  }
  //@} Update Head Movement Actions
}

Vector3 ManVisualState::HeadShift() const 
{ 
  static float shiftCoefCont = 1.0f;
  // combine headShift and headShiftCont actions
  float x = _headXShiftWanted + shiftCoefCont*_headXShiftWantedCont; saturate(x,-1.0f,+1.0f);
  float y = _headYShiftWanted + shiftCoefCont*_headYShiftWantedCont; saturate(x,-1.0f,+1.0f);
  float z = _headZShiftWanted + shiftCoefCont*_headZShiftWantedCont; saturate(x,-1.0f,+1.0f);
  return Vector3(x,y,z);
}

float ManVisualState::HeadRoll() const 
{ 
  // combine _headZRotWantedCont and _headZRotWanted actions
  float rot = _headZRotWanted + _headZRotWantedCont;
  saturate(rot,-1.0f,1.0f);
  return rot;
}

void Man::ProcessLookAroundAuto(Vector3Par dir)
{
  // based on AimHead, TODO: DRY
  // calculate current gun direction

  float yRotWanted=-atan2(dir.X(),dir.Z());
  float sizeXZ=dir.SizeXZ();
  float xRotWanted=atan2(dir.Y(),sizeXZ);

//   //limit look
//   LimitLookRot(xRotWanted, yRotWanted);
//   _lookYRotWantedAuto = yRotWanted;
//   _lookXRotWantedAuto = xRotWanted;
// 
//   // limit with natural head limits
//   // the more y rot, the less x rot enabled
//   LimitHeadRot(xRotWanted, yRotWanted);
  _headYRotWantedAuto = yRotWanted;
  _headXRotWantedAuto = xRotWanted;
}

/*!
\patch 5131 Date 2/21/2007 by Bebul
- Fixed: Prone rolling no longer activates lean toggle
*/
void Man::UpdateLeaning(InputInterface *input)
{
  // When soldier is down, no leaning is possible. 
  // Moreover, evasiveLeft collides with leanLeft and is used for rolling in prone stance.
  if (IsDown()) return;
  bool toggleLL = input->GetActionToDo(UALeanLeftToggle);
  float tempLL = input->GetAction(UALeanLeft);
  saturate(tempLL, 0.0f, 1.0f);
  bool toggleLR = input->GetActionToDo(UALeanRightToggle);
  float tempLR = input->GetAction(UALeanRight);
  saturate(tempLR, 0.0f, 1.0f);

  if (toggleLL) //we should lock the UALeanLeft action until released
    _leanLeftLocked = true;
  else if (_leanLeftLocked && (tempLL<0.5f)) 
    _leanLeftLocked = false; //unlock
  if (toggleLL) 
  { 
    _leanLeftToggle = !_leanLeftToggle;
    _leanRightToggle = false;
  }
  if (toggleLR) //we should lock the UALeanRight action until released
    _leanRightLocked = true;
  else if (_leanRightLocked && (tempLR<0.5f)) 
    _leanRightLocked = false; //unlock
  if (toggleLR) 
  {
    _leanRightToggle = !_leanRightToggle;
    _leanLeftToggle = false;
  }

  if (fabs(tempLR-tempLL)>0.5f) 
  {
    if (!_leanLeftLocked) 
    {
      if (_leanLeftToggle)
      {
        _leanLeftToggle = false;
        _leanLeftLocked = true;
      }
    }
    else tempLL = 0;
    if (!_leanRightLocked) 
    {
      if (_leanRightToggle)
      {
        _leanRightToggle = false;
        _leanRightLocked = true;
      }
    }
    else tempLR = 0;
  }
  _leanZRotWanted = (_leanRightToggle ? 1.0f : tempLR ) - (_leanLeftToggle ? 1.0f : tempLL);
}

const float ZoomSpeed=8;
extern bool LandEditor;

static float LimitZoom(float zoom, Object* object, const ViewPars *viewPars)
{
  
  CameraType camOld = GWorld->GetCameraTypeOld();
  CameraType camNew = GWorld->GetCameraTypeNew();
  float camNewFactor = GWorld->GetCameraTypeTransition();

  if (camOld!=camNew)
  {
    // some objects may allow smooth fov transitions, while other not
    // currently we hack it and to transition only for Person,
    // and only when transitioning between 1st person and 3D optics (sights)
    Person *person = dyn_cast<Person>(object);
    if(
      !person || !person->HasOpticsTransitionFx(person,camNew,camOld) ||
      !AllInternal(camOld,camNew) || person->GetOpticsModel(person)
    )
    {
      camOld = camNew = GWorld->GetCameraType();
    }
  }
  
  // we might update zoom during transition between 1st person view and sights here

  float zoomOld = zoom;
  float zoomNew = zoom;
  
  float dummyHead=0, dummyDive=0;
  if (viewPars)
  {
    viewPars->LimitVirtual(camOld, dummyHead, dummyDive, zoomOld);
    viewPars->LimitVirtual(camNew, dummyHead, dummyDive, zoomNew);
  }
  else 
  {
    object->LimitVirtual(camOld, dummyHead, dummyDive, zoomOld);
    object->LimitVirtual(camNew, dummyHead, dummyDive, zoomNew);
  }
    
  return zoomOld*(1-camNewFactor)+zoomNew*camNewFactor;
  
}

static void InitZoom(CameraType camTypeAct, float &zoom, Object* object, const ViewPars *viewPars)
{
  float dummyHead=0, dummyDive=0;
  if (viewPars)
    viewPars->InitVirtual(camTypeAct, dummyHead, dummyDive, zoom);
  else 
    object->InitVirtual(camTypeAct, dummyHead, dummyDive, zoom);
}

void Object::Zoom::Init(const ViewPars *init)
{
  _camFOV = _camFOVNoCont = _camFOVWanted = init->_initFov;
}

void Object::Zoom::Init(float fov)
{
  _camFOV = _camFOVNoCont = _camFOVWanted = fov;
}

void Object::Zoom::Init(const Object *object)
{
  float dummyH, dummyD;
  object->InitVirtual(GWorld->GetCameraType(), dummyH, dummyD, _camFOV);
  _camFOVWanted = _camFOVNoCont = _camFOV;
}

/*!
\patch 5139 Date 3/13/2007 by Bebul
- Fixed: Zoom out and initial zoom settings for infantry 3D optics and 1st person view
\patch 5142 Date 3/19/2007 by Bebul
- New: User action ZoomOutToggle. Mapped to 2xNumpadMinus as default.
*/
void Object::Zoom::Update(InputInterface *input, float deltaT, Object *object, const ViewPars *viewPars, bool discrete)
{
  {
#if _ENABLE_CHEATS
    if (!GInput.cheat1 && !GInput.cheat2)
#endif
    {
      if (input->GetActionToDo(UALookAroundToggle))
      {
        _lookAroundToggleEnabled = !_lookAroundToggleEnabled;
        if (_lookAroundToggleEnabled) _lookAroundLocked=2;
      }

      bool oldVal = _lookAroundEnabled;
      if (input->GetAction(UALookAround))
      {
        _lookAroundEnabled = true;
      }
      else
      {
        _lookAroundEnabled = _lookAroundToggleEnabled;
      }
      if (oldVal!=_lookAroundEnabled)
      {
        // free-look changed - some reaction possible
        if (GWorld && object==GWorld->CameraOn()) GWorld->FreelookChange(_lookAroundEnabled);
      }

    }          
  }




  float initFOV;

  CameraType camTypeAct = GWorld->GetCameraType();
  InitZoom(camTypeAct, initFOV, object, viewPars);

  float camFOVWantedCont = initFOV;
  float minFOV = 0;
  float maxFOV = FLT_MAX; 
  bool needLimit = true;
  { // continuous zoom control
    float initFOVCont = initFOV;
    // note: GetAction should not return negative values!
    float zoomC = input->GetAction(UAZoomContIn,false) - input->GetAction(UAZoomContOut,false);

    if (_zoomInsteadOfOptics)
    {
      if (!LandEditor) initFOVCont = 0; //LockTarget forces zoom in buldozer
    }
    else if (camTypeAct == CamGunner || camTypeAct == CamInternal || camTypeAct == CamExternal)
    {
      // zoom is controlled continuously
      if (zoomC < 0)
      { // using zoomC: -1 .. maxFOV, 0 .. initFOVCont
        maxFOV = LimitZoom(maxFOV, object, viewPars);
        initFOVCont = initFOVCont + zoomC*(initFOVCont - maxFOV);
      }
      else
      { // using zoomC: 0 .. initFOVCont, 1 .. minFOV
        minFOV = LimitZoom(minFOV, object, viewPars);
        initFOVCont = initFOVCont + zoomC*(minFOV - initFOVCont);
      }
    }
    else if (camTypeAct == CamGroup && zoomC >= 0.75f)
      initFOVCont = FLT_MAX;

    initFOVCont = LimitZoom(initFOVCont, object, viewPars);
    camFOVWantedCont = initFOVCont;
  }
  if( !object->IsExternal(camTypeAct) )
  {
    bool resetZoomOutToggle = false;
    bool resetZoomInToggle = false;
    if (object->IsContinuous(camTypeAct) || (viewPars && viewPars->_continuous>1))
    {
      if(!discrete)
      {//if discrete, we cycle optics modes instead of zooming
        float expChange=input->GetAction(UAZoomOut)-input->GetAction(UAZoomIn);
#if _VBS3 //discrete optics zoom level
        if (!viewPars || !viewPars->DiscreteValues())
#endif
          if (expChange)
          {
            float contZoomSpeed = (viewPars && viewPars->_continuous>1) ? viewPars->_continuous : ZoomSpeed;
            float change=pow(contZoomSpeed,expChange*deltaT);
            _camFOVNoCont*=change;
            if (expChange<0) resetZoomOutToggle = true;
            if (expChange>0) resetZoomInToggle = true;
          }
          _camFOVNoCont = LimitZoom(_camFOVNoCont, object, viewPars);
          _camFOVWanted = _camFOVNoCont;
          if (_zoomInsteadOfOptics)
          { //force max zoom (but only as continuous)
            camFOVWantedCont=0.001f; //maximal zoom (will be saturated)
          }
      }
      // for standalone gunner (not inside vehicle) we want continuous zoom or PAN based on interval
      // PAN for _camFOVWanted in range (muzzle->opticsZoomMin, muzzle->opticsZoomInit)
      if (camTypeAct == CamGunner)
      {
        Person *person= dyn_cast<Person>(object);
        if (person)
        {
          WeaponModePtr curPtr = person->GetWeapons().ValidatedCurrentWeapon();
          if (curPtr.IsValid() && !person->IsDamageDestroyed())
          {
            const OpticsInfo *opticsInfo = curPtr.GetOpticsInfo();
            DoAssert(opticsInfo);
#if !_VBS3
            if (!person->GetOpticsModel(person))
#endif
            {
              AIBrain *brain = person->Brain();
              if (brain && !brain->GetVehicleIn()) //gunner not inside vehicle
              {

#if _VBS3
                if (!person->GetOpticsModel(person))
                {
#endif
                  if (_camFOVWanted > opticsInfo->_opticsZoomInit) //Continuous
                  {
                    _camFOVNoCont = opticsInfo->_opticsZoomInit;
                    _camFOVWanted = opticsInfo->_opticsZoomMax; //Snap
                  }
                  maxFOV = opticsInfo->_opticsZoomMax;
#if _VBS3
                }
                if(muzzle->_viewOptics.Initialized())
                {
                  viewPars = &muzzle->_viewOptics;
                }
#endif/
              }
            }

            // discrete zoom
            {
              if(opticsInfo->_discreteFov.Size() > 0)
              {
                Muzzle &muzzle = curPtr.GetMuzzle();
                if(input->GetActionToDo(UAZoomIn))
                {
                  muzzle._discreteIndex++;
                  saturate(muzzle._discreteIndex,0, opticsInfo->_discreteFov.Size()-1);
                }
                if(input->GetActionToDo(UAZoomOut))
                {                
                  muzzle._discreteIndex--;
                  saturate(muzzle._discreteIndex,0, opticsInfo->_discreteFov.Size()-1);
                }

                _camFOV = _camFOVWanted = _camFOVNoCont = opticsInfo->_discreteFov[muzzle._discreteIndex];
                needLimit = false;
                _zoomInsteadOfOptics = false;
              }
            }
          }
        }
#if _VBS3 //discrete optics zoom level
        if (viewPars && viewPars->DiscreteValues())
        {
          float prev, curr, next;
          viewPars->DiscretizeValue(_camFOVNoCont, prev, curr, next);

          if(input->GetActionToDo(UAZoomOut)) _camFOV = _camFOVWanted = _camFOVNoCont = next;
          if(input->GetActionToDo(UAZoomIn))  _camFOV = _camFOVWanted = _camFOVNoCont = prev;
        }
#endif
      }
    }
    else
    {
      float newFOV=initFOV;
      if( input->GetAction(UAZoomIn)) 
      {
        newFOV*=0.25;
        resetZoomOutToggle = true;        
      }            
      else if ( _zoomInsteadOfOptics ) newFOV*=0.25; // but do not reset ZoomOutToggle (for RMB zooming)
      else if( input->GetAction(UAZoomOut) ) 
      {
        newFOV*=4;
        resetZoomInToggle = true;        
      }
      maxFOV = initFOV*4;
      maxFOV = LimitZoom(maxFOV, object, viewPars);
      _camFOVWanted = LimitZoom(newFOV, object, viewPars);
    }

    // UAZoomOutToggle implementation
    Person *person= dyn_cast<Person>(object);
    // zoom toggling in vehicles
    if (!person) 
      person = GWorld->PlayerOn();

    if (person && (camTypeAct == CamGunner || camTypeAct == CamInternal || camTypeAct == CamExternal) )
    {
      if (resetZoomOutToggle) person->ResetZoomOutToggle();
      if (!person->GetOpticsModel(person))
      {
        AIBrain *brain = person->Brain();
        //if (brain && !brain->GetVehicleIn()) //gunner not inside vehicle
        if (brain) //zoom out toggling in vehicles
        {
          if (input->GetActionToDo(UAZoomOutToggle)) person->ToggleZoomOut();
          if (person->IsZoomOutToggled()) _camFOVWanted = _camFOVNoCont = maxFOV;
        }
      }
    } 

    // UAZoomInToggle implementation
    if (person && (camTypeAct == CamGunner || camTypeAct == CamInternal || camTypeAct == CamExternal) )
    {
      if (resetZoomInToggle) person->ResetZoomInToggle();      
      if (!person->GetOpticsModel(person))
      {
        AIBrain *brain = person->Brain();
        
        //if (brain && !brain->GetVehicleIn()) //gunner not inside vehicle
        if (brain) //zoom in toggling in vehicles
        {
          if (input->GetActionToDo(UAZoomInToggle)) person->ToggleZoomIn();
          
          //if (person->IsZoomInToggled()) _camFOVWanted = _camFOVNoCont = 0;
          if (person->IsZoomInToggled()) _camFOVWanted = _camFOVNoCont = minFOV;
        }
      }
    }
  }
  if( input->GetActionToDo(UALookCenter,false) ) 
  {
    float dummyH, dummyD;
    if (viewPars) viewPars->InitVirtual(GWorld->GetCameraType(), dummyH, dummyD, _camFOVNoCont);
    else object->InitVirtual(GWorld->GetCameraType(), dummyH, dummyD, _camFOVNoCont);;
  }

  if(needLimit)
  {
    //process read actions
    //_camFOVWanted *= camFOVWantedCont/initFOV;
    float camFOVWantedWithCont = _camFOVWanted*camFOVWantedCont/initFOV;
    //saturate(camFOVWantedWithCont, initFOV*0.25, initFOV*4);
    float delta = camFOVWantedWithCont/_camFOV;
    //LogF("Zoom delta: delta=%.2f cont/init=%.2f", delta, camFOVWantedCont/initFOV);

    float changeMax=pow(ZoomSpeed,deltaT);
    saturate(delta,1/changeMax,changeMax);
    _camFOV*=delta;

    _camFOV = LimitZoom(_camFOV, object, viewPars);
    _camFOVWanted = LimitZoom(_camFOVWanted, object, viewPars);
    _camFOVNoCont = LimitZoom(_camFOVNoCont, object, viewPars);
  }
}


LSError Object::Zoom::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("fovWanted",_camFOVWanted,1,0.5f));
  if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
  {
    _camFOV = _camFOVNoCont = _camFOVWanted;
  }

  CHECK(ar.Serialize("_lookAroundToggleEnabled",_lookAroundToggleEnabled,1,false));
  CHECK(ar.Serialize("_lookAroundEnabled",_lookAroundEnabled,1,false));
  CHECK(ar.Serialize("_forceLookAround",_forceLookAround,1,false));
  CHECK(ar.Serialize("_lookAroundLocked",_lookAroundLocked,1,0));

  return LSOK;
}

float Man::GetCameraFOV() const
{
  return _zoom._camFOV * GInput.fov;
}

void Object::Zoom::ZoomIn(Object *object)
{
  _camFOVWanted = 0;
  float dummyH=0, dummyD=0;
  object->LimitVirtual(GWorld->GetCameraType(), dummyH, dummyD, _camFOVWanted);
}

void Object::Zoom::ZoomOut(Object *object)
{
  _camFOVWanted = FLT_MAX;
  float dummyH=0, dummyD=0;
  object->LimitVirtual(GWorld->GetCameraType(), dummyH, dummyD, _camFOVWanted);
}

void Man::UpdateHeadLookWanted(float heading, float dive)
{
  _headYRotWanted += heading;
  _headXRotWanted += dive;
}

#ifdef __GNUC__
static const int SelectManAction[3][3][3]=
#else
static const ManAction SelectManAction[3][3][3]=
#endif
{
  { // walk action
    {ManActWalkLB,ManActWalkB,ManActWalkRB},// back L|R 
    {ManActWalkL,ManActStop,ManActWalkR},// L|R 
    {ManActWalkLF,ManActWalkF,ManActWalkRF},// front L|R 
  },
  { // slow action
    {ManActSlowLB,ManActSlowB,ManActSlowRB},// back L|R 
    {ManActSlowL,ManActStop,ManActSlowR},// L|R 
    {ManActSlowLF,ManActSlowF,ManActSlowRF},// front L|R 
  },
  { // fast action
    {ManActFastLB,ManActFastB,ManActFastRB},// back L|R 
    {ManActFastL,ManActStop,ManActFastR},// L|R 
    {ManActFastLF,ManActFastF,ManActFastRF},// front L|R 
  },
};

#ifdef __GNUC__
static const int SelectManActionRaised[3][3][3]=
#else
static const ManAction SelectManActionRaised[3][3][3]=
#endif
{
  { // walk action
    {ManActWalkRaisedLB,ManActWalkRaisedB,ManActWalkRaisedRB},// back L|R 
    {ManActWalkRaisedL,ManActStopRaised,ManActWalkRaisedR},// L|R 
    {ManActWalkRaisedLF,ManActWalkRaisedF,ManActWalkRaisedRF},// front L|R 
  },
  { // slow action
    {ManActSlowRaisedLB,ManActSlowRaisedB,ManActSlowRaisedRB},// back L|R 
    {ManActSlowRaisedL,ManActStopRaised,ManActSlowRaisedR},// L|R 
    {ManActSlowRaisedLF,ManActSlowRaisedF,ManActSlowRaisedRF},// front L|R 
  },
  { // fast action
    {ManActFastLB,ManActFastB,ManActFastRB},// back L|R 
    {ManActFastL,ManActStopRaised,ManActFastR},// L|R 
    {ManActFastLF,ManActFastF,ManActFastRF},// front L|R 
  },
};


/*!
\patch 5100 Date 12/8/2006 by Bebul
- Fixed: Player with no legs gets no longer stuck in up/down limbo
\patch 5101 Date 12/12/2006 by Bebul
- Fixed: Injured soldier is forced to prone only when he wants to move (done to fix RPG soldier ability to run even when injured)
*/

void Soldier::KeyboardPilot(float deltaT, SimulationImportance prec)
{
  if (KeyboardUsedInFrame())
  {
    return;
  }
  
  TurretContextEx context;
  context._turret = NULL;
  context._turretType = NULL;
  context._gunner = this;
  context._weapons = &_weaponsState;
  context._parentTrans = FutureVisualState().Orientation();

  bool fullSimulation = GetNetworkManager().IsServerOwner();
  InputInterface *input = GetNetworkManager().GetInputInterface(GetRemotePlayer());
  UpdateRaiseWeapon(input);

  // item shortcuts
  void ToggleShortcuts();
  if (input->GetActionToDo(UAItemsShow)) ToggleShortcuts();

  if (fullSimulation)
  {
    SimulateWeaponInputs(input,_brain,context);
  }
  else
  {
    // on client, show items bar when shortcut is activated
    for (int i=UAItemFirst; i<=UAItemLast; i++)
    {
      if (input->GetActionToDo((UserAction)i) && QIsManual())
      {
        void ShowShortcuts();
        ShowShortcuts();
      }
    }
  }
  ProcessFire(input);

#if 0 //_ENABLE_REPORT
  if (ToMoveOut() && GWorld->GetMode()==GModeNetware)
  {
    LogF("No keyboard pilot should be called now");
  //  return;
  }
#endif

  if (IsSuspendedByActivity())
  {
    // In get in action soldier must not move or turn, just play animation with get in.
    _turnWanted = 0;
    _walkSpeedWantedX = 0;
    _walkSpeedWantedZ = 0;
    _walkSpeedFineControl = false;
    _leanZRotWanted = 0;
    _headXRotWantedCont = _headYRotWantedCont = _headXRotWantedPan = _headYRotWantedPan = 0;
    _headXRotWantedAuto = _headYRotWantedAuto = 0;
    //SuspendedPilot(deltaT, prec);
    return;
  }
  //load from backup _gunRotWanted values
  _gunXRotWanted = _gunXRotWantedNoTrembleHands;
  _gunYRotWanted = _gunYRotWantedNoTrembleHands;

  _headYRotWantedCont = H_PI*(input->GetAction(UALookLeftCont,false) - input->GetAction(UALookRightCont,false));
  _headXRotWantedCont = H_PI*(input->GetAction(UALookUpCont,false) - input->GetAction(UALookDownCont,false));
  _headXRotWantedAuto = _headYRotWantedAuto = 0;
    
  Object::Zoom *zoom = GetZoom();
  if (zoom) 
  {
    CameraType camType = GWorld->GetCameraType();
    // Right mouse button (UALockTarget) = hold-to-zoom. This must be
    // re-evaluated every frame, otherwise releasing the button leaves the
    // camera stuck at the maximum zoom level.
    zoom->_zoomInsteadOfOptics = input->GetAction(UALockTarget) &&
      (camType == CamInternal || camType == CamExternal || camType == CamGunner);
    RenderVisualStateScope(true),zoom->Update(input,deltaT,this); // Zoom update may call RenderVisualState to check if optics are enabled

    WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
    if (ptr.IsValid())
    {
      if (input->GetActionToDo(UAZeroingUp))
      {
        const OpticsInfo *opticsInfo = ptr.GetOpticsInfo();
        DoAssert(opticsInfo);

        Muzzle &muzzle = ptr.GetMuzzle();
        if (muzzle._discreteDistanceIndex < opticsInfo->_discreteDistance.Size()-1) muzzle._discreteDistanceIndex++;
      }

      if (input->GetActionToDo(UAZeroingDown))
      {
        Muzzle &muzzle = ptr.GetMuzzle();
        if (muzzle._discreteDistanceIndex > 0) muzzle._discreteDistanceIndex--;
      }
    }
  }

  float headingChangeWanted = 0.0f, diveChangeWanted = 0.0f;
  GetHeadLookAroundActions(input, deltaT, headingChangeWanted, diveChangeWanted);

  if( (headingChangeWanted != 0.0f || diveChangeWanted != 0.0f) || (_headYRotWantedPan != 0.0f && headingChangeWanted == 0.0f) )
    _headSmoothMove = true;

  _lookXRotWanted += diveChangeWanted;
  _headYRotWantedPan = headingChangeWanted;

  // Movement modes (requested):
  //   W alone       -> jog (light run)
  //   Shift (hold)  -> sprint
  //   Control (hold) -> slow walk
  const bool shiftHeld = GInput.GetKey(DIK_LSHIFT) > 0.5f || GInput.GetKey(DIK_RSHIFT) > 0.5f;
  const bool altHeld = GInput.GetKey(DIK_LCONTROL) > 0.5f || GInput.GetKey(DIK_RCONTROL) > 0.5f;

  bool isWalking = altHeld;

  if(_walkForced)
  {// walk forced by script function
    isWalking = true;
  }
  _isWalking = isWalking;
  _walkRunTempLocked = altHeld;

  float runWalkFactor = isWalking ? 0.5f : 1.0f;
  // Double-tap W sprint disabled: the character only runs while Shift is held.
  float fastForwardAct = 0;

  /// when weaponRaised action is active, the weapon is "raised" and SelectManActionRaised array is used to select the ManAction
  bool weaponRaised = CheckAndUpdateRaiseWeapon(input) || (GetEnableOptics(FutureVisualState(), this) && GWorld->GetCameraTypeWanted()==CamGunner);

  float fbWanted =
  (
    fastForwardAct*2.0f+
    input->GetAction(UAMoveForward)*runWalkFactor+
    input->GetAction(UAMoveSlowForward)*0.5f+
    input->GetAction(UAMoveBack)*-1.0f
  );
  float rlWanted = input->GetAction(UATurnRight)-input->GetAction(UATurnLeft);
  bool suspendMovement = false;
  if ((!_landContact || _freeFall) && _freeFallStartTime<Glob.time-0.6f && !_swimming)
  {
    suspendMovement = true;
    if (QIsManual())
    {
      // Do not freeze the controlled player when ground contact is missing.
      // Keep only a small air-control input while the physics state recovers.
      const float airControl = 0.25f;
      rlWanted *= airControl;
      fbWanted *= airControl;
    }
    else
    {
      rlWanted = 0;
      fbWanted = 0;
    }
  }


  float turnWanted=0;
  _leanZRotWanted = 0;
  if (!_isDead)
  {
    // turn with arrows
    turnWanted=(input->GetAction(UAMoveRight)-input->GetAction(UAMoveLeft))*1.5f;
    UpdateLeaning(input);

    bool forceAim = ForceAim(FutureVisualState());
    bool ironSight = GetEnableOptics(FutureVisualState(), this) && GWorld->GetCameraType() == CamGunner;
    bool freelook = (_brain->LookAroundEnabled() || forceAim) && (GWorld->GetCameraType() != CamGunner || ironSight);

    // force turning body instead of aiming
    //bool turnBody = fabs(fbWanted) < 0.01; // && fabs(rlWanted) < 0.01;

    if (freelook && !_freelook)
    {
      // freelook mode entered, reset turn in progress
      _turnWanted = 0;
      _headSmoothMove = false;
      _gunYRotIronSightBackup = _gunYRot;
    }
    else if (!freelook && _freelook)
    {
      //after changing from freelook to turn mode, turn head to the default fwd direction
      _headYRotWanted = 0;
      _headSmoothMove = true;
      if (ironSight)
        _gunYRotWanted = _gunYRotWantedNoTrembleHands = _gunYRotIronSightBackup;
    }
    _freelook = freelook;

    static float recoilFactor = 0.2f;

    if( freelook )
    {
      float lookX = input->GetActionWithCurveExclusive(InputInterface::CurveFovMouse, UAAimHeadRight, UAAimHeadLeft);
      float lookY = input->GetActionWithCurveExclusive(InputInterface::CurveFovMouse, UAAimHeadUp, UAAimHeadDown);
      // Older profiles may bind horizontal mouse movement only to body aim.
      // Select that fallback from the bindings, never from a zero input sample:
      // zero samples also occur between mouse events and while sprinting, and
      // switching input sources on those frames makes the camera stutter.
      if (QIsManual() &&
          GInput.GetActionKeys("AimHeadRight").Size() == 0 &&
          GInput.GetActionKeys("AimHeadLeft").Size() == 0)
        lookX = input->GetActionWithCurveExclusive(InputInterface::CurveFovMouse, UAAimRight, UAAimLeft);

      // Mouse look must follow the current input. The head smoothing above is
      // for automatic recentering and discrete look actions; leaving it active
      // while walking makes horizontal camera motion trail and oscillate.
      if (QIsManual() && (fabs(lookX) > 1e-6f || fabs(lookY) > 1e-6f))
        _headSmoothMove = false;

      float rotXWanted = (ironSight ? _gunXRotWanted : _lookXRotWanted) + lookY*deltaT;
      float rotYWanted = (ironSight ? _gunYRotWanted : _lookYRotWanted) - lookX*deltaT;

      if( ironSight )
      {
        float f = GetLimitGunMovement(FutureVisualState());
        Limit(rotXWanted, Type()->_minGunElev*f, Type()->_maxGunElev*f);
        Limit(rotYWanted, Type()->_minGunTurnAI*f, Type()->_maxGunTurnAI*f);
      }
      else
      {
        // TODO: handle lookAroundLocked in InputInterface
        bool lookAround = (input->GetAction(UALookAround) > 0.5f);
        if( !lookAround && _zoom._lookAroundLocked > 0 )
          _zoom._lookAroundLocked--;
        else if( _zoom._lookAroundLocked )
          lookAround = false; //no lookAround while not released after freeLook activation

        if( input->GetActionToDo(UALookCenter, false) || (lookAround && _zoom._lookAroundToggleEnabled) )
        {
          _lookXRotWanted = _lookYRotWanted = rotXWanted = rotYWanted = 0; //UALookAround when FreeLook toggled centralizes camera
          _headSmoothMove = true;
        }

        LimitLookRot(rotXWanted, rotYWanted);
      }

      // calculate world space aiming direction
      Matrix3 rotXMat(MRotationX, -rotXWanted);
      Matrix3 rotYMat(MRotationY, +rotYWanted);
      Vector3 aimDir = FutureVisualState().DirectionModelToWorld(rotYMat * rotXMat.Direction());
      aimDir = ValidateAimDir(aimDir);
      AimHead(aimDir);

      if( !ironSight )
      {
        Matrix3 rotXMat(MRotationX, -_gunXRotWantedNoTrembleHands);
        Matrix3 rotYMat(MRotationY, _gunYRotWantedNoTrembleHands);
        aimDir = FutureVisualState().DirectionModelToWorld(rotYMat * rotXMat.Direction());
        aimDir = ValidateAimDir(aimDir);
      }
      AimHeadAndWeapon(aimDir); //currently it aim only weapon
    }
    else
    {
      _holdBreath = input->GetAction(UAHoldBreath);

      float aimX = input->GetActionWithCurve(InputInterface::CurveFovMouse,UAAimRight,UAAimLeft);
      float aimY = input->GetActionWithCurve(InputInterface::CurveFovMouse,UAAimUp,UAAimDown);
      if (QIsManual() && (fabs(aimX) > 1e-6f || fabs(aimY) > 1e-6f))
        _headSmoothMove = false;

      bool useAimingDeadzone = IsAimingDeadzoneEnabled();

      //no floating zone when WeaponsDisabled
      if( !useAimingDeadzone )
      {
        _gunYRotWanted = _headYRotWanted;
        _gunXRotWanted = _headXRotWanted;
      }

      // aiming up-down is done using lookXRotWanted and gunXRotWanted
      float rotXWanted = _gunXRotWanted + aimY*deltaT;
      float rotYWanted = _headYRotWanted;
      saturate(rotXWanted, Type()->_minGunElev, Type()->_maxGunElev);
      float weaponRotXWanted = rotXWanted; //weapon dir

      //Cursor Floating Zone
      if( useAimingDeadzone )
      {
        const Camera* camera = GScene->GetCamera();
        float maxHeading = GInput.floatingZoneArea * camera->Left();
        float maxDive = GInput.floatingZoneArea * camera->Top();

        //horizontal floating zone
        float rotYWanted2 = _gunYRotWanted -aimX * deltaT;
        rotYWanted = rotYWanted2;
        saturate(rotYWanted, -maxHeading, maxHeading);
        float turnValue = (rotYWanted2 - rotYWanted) / deltaT;
        aimX = -turnValue;

        //vertical floating zone
        float lookWeaponXDiff = weaponRotXWanted - _lookXRotWanted;
        float satLookWeaponXDiff = lookWeaponXDiff;
        saturate(satLookWeaponXDiff, -maxDive, maxDive);
        rotXWanted = _lookXRotWanted + lookWeaponXDiff - satLookWeaponXDiff; //_lookXRotWanted inside floating zone

        _gunXRotDelay = -(aimY * deltaT);
        _gunYRotDelay = aimX * deltaT;
      }

      // calculate world space aiming direction
      Matrix3 rotXMat(MRotationX, -rotXWanted);
      Matrix3 rotYMat(MRotationY, +rotYWanted);
      Vector3 headDir = FutureVisualState().DirectionModelToWorld(rotYMat * rotXMat.Direction());
      //head is turning only up down here
      //in order weapon and head being synchronized, sizeXZ should be kept
      Vector3 relDir(VMultiply, DirWorldToModel(), headDir);
      float sizeXZ = relDir.SizeXZ();
      relDir[0] = 0;
      relDir[2] = sizeXZ;
      AimHead(FutureVisualState().DirectionModelToWorld(relDir));

      // AimHeadAndWeapon will set both _headXRotWanted and _gunXRotWanted
      Matrix3 weaponRotXMat(MRotationX, -weaponRotXWanted);
      Matrix3 weaponRotYMat(MRotationY, rotYWanted);
      Vector3 aimDir = FutureVisualState().DirectionModelToWorld(weaponRotYMat * weaponRotXMat.Direction());
      aimDir = ValidateAimDir(aimDir);
      AimHeadAndWeapon(aimDir); //currently it aim only weapon

      turnWanted += aimX;
    }
  }

  if ((!_landContact || _freeFall) && _freeFallStartTime<Glob.time-0.6f && !_swimming && !QIsManual())
    turnWanted = 0;
  _turnWanted = turnWanted;

#if _ENABLE_PHYSICS
  //position in code has its reason above is rotation code which is allways allowed, bellow are actions which may invoke animation and we dont want to process them while throw is paused
  float power = input->GetAction(UAThrowitem);
  if(!IsActionInProgress(MFThrowItem) && power > 0.001f)
  {
    InventoryItem* item = GetItemInHands();
    if(item)
    {
      RemoveItemInHandsThrow();
			return;
    }
  }
  else if(IsActionInProgress(MFThrowItem) && power < 0.001f)
  {
    ActionContextThrowItem* ctx = (ActionContextThrowItem*)GetActionInProgress();
    ctx->UserEvent(1);
  }

  if(IsActionInProgress(MFThrowItem) && _moves.GetPaused(CurrentVisualState()._moves))
  {
    return;
  }
#endif

  AIBrain *unit = Brain();
  if (!unit)
    return;

  if (unit->GetVehicleIn() == 0 && !unit->IsInCargo() && !GWorld->GetCameraEffect())
  {
    if (input->GetActionToDo(UAHeadlights))
    {
      _lightWanted = !_lightWanted;
      _irWanted = !_irWanted;
    }
  }

  // Shift is now the sprint key (see movement modes above), Alt is the walk key.
  bool turbo = shiftHeld;

  //DIAG_MESSAGE(
  //  500,Format("turnWanted %+.2f -> %+.2f",_turnWanted,turnWanted)
  //);

  ManAction selAction = ManActStop;
  bool forceAction = false;
  
  ActionMap *map = Type()->GetActionMap(_moves.GetPrimaryMove(FutureVisualState()._moves).id, AnimSubIx());

  _walkSpeedWantedX = 0;
  _walkSpeedWantedZ = 0;
  _walkSpeedFineControl = false;

  bool shouldStand = ShouldStand(_landGradFwd, _landGradAside, _forceStand);
  bool wantSlow = isWalking;
  if (GWorld->FocusOn() == unit)
    // change
    wantSlow = wantSlow || GWorld->GetCameraTypeWanted()==CamGunner;
  float dirFwd = fbWanted>0.2 ? 1 : (fbWanted<-0.2 ? -1 : 0);
  float dirAside = rlWanted>0.2 ? 1 : (rlWanted<-0.2 ? -1 : 0);
  float dirSiz = sqrtf(dirFwd*dirFwd + dirAside*dirAside);
  if (dirSiz>0) { dirFwd /= dirSiz; dirAside /= dirSiz; }
  // check if we are using binocular
  bool bCheckIfMoveIsAllowed = false;
  if( input->GetActionToDo(UAMoveUp) )
  {
    bCheckIfMoveIsAllowed = true;
    selAction = ManActUp;
    forceAction = true;
  }
  else if (input->GetActionToDo(UAMoveDown))
  {
    DecFadeSemaphore(FadeProne);
    bCheckIfMoveIsAllowed = true;
    selAction = ManActDown;
    forceAction = true;
    // if missile is selected, de-select it
  }
  else if (input->GetActionToDo(UAStand) && IsAbleToStand() )
  {
    bCheckIfMoveIsAllowed = true;
    forceAction = true;
    selAction = ManActPlayerStand;
  }
  else if (input->GetActionToDo(UACrouch))
  {
    bCheckIfMoveIsAllowed = true;
    forceAction = true;
    selAction = ManActPlayerCrouch;
  }
  else if (input->GetActionToDo(UAProne) && (!QIsManual() || !ShouldStand(_landGradFwd, _landGradAside, _forceStand)))
  {
    DecFadeSemaphore(FadeProne);
    bCheckIfMoveIsAllowed = true;
    forceAction = true;
    selAction = ManActPlayerProne;
  }
  else if (IsAbleToStand() && CanSprint(_landGradFwd,_landGradAside, fbWanted, rlWanted) && input->GetActionToDo(UAEvasiveForward))
  {
    bCheckIfMoveIsAllowed = true;
    forceAction = true;
    selAction = ManActEvasiveForward;
  }
/*
  else if ( CanSprint(_landGradFwd,_landGradAside, fbWanted, rlWanted) && input->GetActionToDo(UAEvasiveLeft) )
  {
    bCheckIfMoveIsAllowed = true;
    forceAction = true;
    selAction = ManActEvasiveLeft;
  }
  else if ( CanSprint(_landGradFwd,_landGradAside, fbWanted, rlWanted) && input->GetActionToDo(UAEvasiveRight) )
  {
    bCheckIfMoveIsAllowed = true;
    forceAction = true;
    selAction = ManActEvasiveRight;
  }
*/
  else if (CanSprint(_landGradFwd,_landGradAside, fbWanted, rlWanted) && input->GetActionToDo(UAEvasiveBack))
  {
    bCheckIfMoveIsAllowed = true;
    forceAction = true;
    selAction = ManActEvasiveBack;
  }
  else if (IsSwimmingInMove() && !_swimming)
    selAction = ManActStopSwim;
  else if (!IsSwimmingInMove() && _swimming) //start swim
    selAction = ManActStartSwim;
  else if(!_forceStand && (!CanStand(_landGradFwd, _landGradAside, dirFwd, dirAside) || GetEncumbrance() >= 1) &&
    !IsDown() && !LauncherAnimSelected() && !_swimming)
  {
    selAction = ManActLying;
  }
  else if (IsDown() &&  input->GetAction(UAEvasiveLeft))
  { // Leaning in Prone should use evasive side moves
    if (!suspendMovement)
      selAction = ManActEvasiveLeft,_walkSpeedWantedX = -Lerp(SideRollSpeed,SideRollTired,_tired);
  }
  else if (IsDown() && input->GetAction(UAEvasiveRight))
  { // Leaning in Prone should use evasive side moves
    if (!suspendMovement)
      selAction = ManActEvasiveRight,_walkSpeedWantedX = +Lerp(SideRollSpeed,SideRollTired,_tired);
  }
  else if (IsDown() && QIsManual() && shouldStand )
  {
    bCheckIfMoveIsAllowed = true;
    forceAction = true;
	  if( IsAbleToStand() )
		  selAction = ManActPlayerStand;
	  else
		  selAction = ManActPlayerCrouch;
  }
  else if (!forceAction)
  {
    // check which action we should take - based on keys
    int fastFlag = turbo;
    // FIX: when some input mapped to GetActionMoveFastForward() is present, do not keep FastMove active
    //      The problem was, when sprint was achieved by fastForward action mapped to XBOXThumb, it was preserved
    //      even when thumb was moving back (no translation through RUN and WALK).
    if (fastForwardAct<+0.1f && map && map->GetUseFastMove())
      fastFlag = true; 
    int rlFlag = 0, fbFlag = 0;
    if (fabs(rlWanted)>+1.5f)
      fastFlag = 1;
    if (fabs(fbWanted)>+1.9f)
      fastFlag = 1;
    if (isWalking || (fabs(fbWanted)<0.6f && fabs(rlWanted)<0.6f))
      wantSlow = true;

    if (fbWanted>+0.1f)
      fbFlag = +1;
    else if (fbWanted<-0.1f)
      fbFlag = -1;

    // when there is no front back movement, small input causes side movement
    // avoid diagonal movement with small input values
    const float rlThold = fbFlag==0 ? 0.1f : 0.5f;
    if (rlWanted>+rlThold)
      rlFlag = +1;
    else if (rlWanted<-rlThold)
      rlFlag = -1;

    // slow down if carrying heavy weight
    // update fastFlag and calculate _walkSpeedWanted
    float maxSpeed = Type()->GetMaxSpeedMs();
    float limitSpeed = floatMin(1.2f - GetEncumbrance(), 1.0f) * maxSpeed;
    
    // Lean is an additive pose overlay; it does not force the character back
    // to walking. The normal run, sprint, weight, slope and water limits apply.
    if (_waterDepth>0.6)
    {
      if (_waterDepth>1.0)
        // in deep water only walking is allowed
        saturateMin(limitSpeed,1.5f);
      else
        // in shallow water only slow running is allowed
        saturateMin(limitSpeed,2.5f);
    }

    float limitFast = map ? map->GetLimitFast() : 5.0f;
    float limitSlow = map ? map->GetLimitFast() * 0.6f : 1.0f;

    //@{ stop->run delay disabled, see https://projects.bistudio.com/dayz/Immediate_response_while_maneuvering.txt
    // always start walking for a short while, run later
    // whenever we are moving slow, reset to walking mode
    // static float walkDuration = 0.33;
    // float const fbrlWanted = floatMax(fabs(fbWanted), fabs(rlWanted));
    // if (fbrlWanted < 0.9f)
    //   _walkUntil = Glob.time+Glob.config.animParams._moveTransitionTime; //+walkDuration;
    _walkSpeedFineControl = false; //Glob.time<_walkUntil;
    wantSlow |= _walkSpeedFineControl;
    //@}

    float controlSpeed = wantSlow ? limitSpeed*0.5f : limitSpeed;
    
    _walkSpeedWantedZ = fbWanted * controlSpeed;
    _walkSpeedWantedX = rlWanted * controlSpeed;
    
    if (IsDown())
    {
      if (!_canMoveFast)
        fastFlag = 0;
      fastFlag++;
      if (wantSlow && fastFlag==1)
        fastFlag=0;
    }
    else
    {
      //GlobalShowMessage(100, "Slope %.2f",slope);
      // Terrain gradients can jump between adjacent samples on a steep hill.
      // For the local player, changing the gait every frame makes the running
      // animation (and its camera transform) oscillate. Preserve the chosen
      // gait while the regular stamina and movement limits still apply.
      if (!_canMoveFast || (!QIsManual() && !CanSprint(_landGradFwd, _landGradAside, dirFwd, dirAside)))
        fastFlag = 0;
      fastFlag++;
      if (wantSlow && fastFlag==1 || (!QIsManual() && !CanRun(_landGradFwd, _landGradAside, dirFwd, dirAside)))
        fastFlag = 0;
    }
    switch (fastFlag)
    {
    case 2: // fast
      if (limitSpeed >= limitFast)
        break;
      fastFlag--;
      // continue
    case 1: // slow
      if (limitSpeed >= limitSlow)
      {
        saturate(_walkSpeedWantedZ,-limitFast,+limitFast);
        saturate(_walkSpeedWantedX,-limitFast,+limitFast);
        break;
      }
      fastFlag--;
    case 0: // walk
      saturate(_walkSpeedWantedZ,-limitSlow,+limitSlow);
      saturate(_walkSpeedWantedX,-limitSlow,+limitSlow);
      break;
    }

    // ANIMS-MOVE-DIAG: numeros reais do movimento (para calibrar velocidade).
    {
      static int diagTick = 0;
      if ((fabs(fbWanted)>0.01f || fabs(rlWanted)>0.01f) && (++diagTick % 90)==0)
        RptF("ANIMS-MOVE: fb=%.2f fast=%d max=%.2f limF=%.2f limS=%.2f wantZ=%.2f walk=%d raised=%d move=%s",
          fbWanted, fastFlag, maxSpeed, limitFast, limitSlow, _walkSpeedWantedZ, isWalking?1:0, weaponRaised?1:0,
          cc_cast(GetMovesType()->GetMoveName(_moves.GetPrimaryMove(FutureVisualState()._moves).id)));
    }

    // map flags to action
    // check which action map should be used now
    // suppose we are doing _primaryMove
    // map to eight directions/two levels of stress based on values
    // get action ID
    if (IsAbleToStand() || IsDown() || shouldStand || IsSwimming() || (fbFlag==0 && rlFlag==0))
    {
      if (!weaponRaised)
        selAction = ENUM_CAST(ManAction,SelectManAction[fastFlag][fbFlag+1][rlFlag+1]);
      else
      {
        selAction = ENUM_CAST(ManAction,SelectManActionRaised[fastFlag][fbFlag+1][rlFlag+1]);
        if ( fbFlag && (fastFlag<2) ) //not stopped and not too fast
        {
          ResetRaiseWeapon(); //already raisen, just reset timer
        }
      }
    }
    else
    { // player wants to move, but he is injured so movement is possible only while lying or crouching
      selAction = ManActLying;
      forceAction = true;
    }
  }

  if (selAction == ManActPlayerProne && !IsAbleToProne())
  {
    selAction = ManActPlayerCrouch;
  }
  /*
  if( selAction!=ManActWeaponOn && GetActUpDegree(FutureVisualState())==ManPosWeapon )
  {
    selAction = ManActWeaponOff;
    forceAction = true;
  }
  */

  // get which action is it (based on action map)

  CameraType camType = GWorld->GetCameraType();
  if (!QIsManual() || camType==CamExternal || camType==CamGroup)
  {
    if (selAction==ManActStop || selAction==ManActStopRaised)
    {
      if (fabs(_turnWanted)>0.5f)
        if (!weaponRaised) selAction = _turnWanted<0 ? ManActTurnL : ManActTurnR;
        else selAction = _turnWanted<0 ? ManActTurnRaisedL : ManActTurnRaisedR;
    }
  }


  MoveId moveWanted = map ? Type()->GetMove(map, selAction) : MoveIdNone;
  // Resolve the requested action through the active ActionMap first. If the
  // current state has no map or does not define this action, use the map for
  // the decoded posture before falling back to an idle/default move.
  if (moveWanted == MoveIdNone)
  {
    const MoveIdExt &postureMove = GetMovesType()->GetMove(GetActUpDegree(FutureVisualState()), selAction);
    if (postureMove.id != MoveIdNone)
    {
      moveWanted = postureMove.id;
    }
  }
  if (moveWanted == MoveIdNone)
    moveWanted = GetDefaultMove();
  if (!IsLocal() && _targetMoveWanted!=MoveIdNone)
  {
    ActionMap *targetMap = Type()->GetActionMap(_targetMoveWanted, AnimSubIx());
    MoveId currentStop = map ? Type()->GetMove(map, ManActStop) : GetDefaultMove();
    MoveId targetStop = targetMap ? Type()->GetMove(targetMap, ManActStop) : GetDefaultMove();
    if (map!=targetMap && currentStop!=targetStop)
    {
      moveWanted = _targetMoveWanted;
    }
  }

  // Add info to debug buffer of last actions
#if _ENABLE_CHEATS
  if (CHECK_DIAG(DEAnimation))
  {
    RString actionName;
    RStringB moveName = GetMovesType()->GetMoveName(moveWanted);
    if (selAction < ManActN)
      actionName = ManActionNames[selAction].name;
    _debugLastActionsInfo.AddInfo(actionName, moveName, "KeyboardPilot");
  }
#endif

  
  // some actions are edge sensitive - must be executed
  if (!SimulateForcedMove(prec))
  {
    bool notForbbidenMove = true;
    if (IsDown()) 
    {
      // Check if move changes updegree and if yes do mot SetMoveQueue
      
      ActionMap *map = Type()->GetActionMap(moveWanted, AnimSubIx());
      int newPos = map ? map->GetUpDegree() : ManPosLying;

      notForbbidenMove = newPos == ManPosLying  || newPos == ManPosBinocLying || newPos == ManPosLyingNoWeapon ||
        newPos == ManPosHandGunLying || newPos == ManPosDead;

      if (!notForbbidenMove && IsCollisionFreeMove(moveWanted))
        notForbbidenMove = true;

      if (!notForbbidenMove && _lastCurrentWeapon != _weaponsState.ValidatedCurrentWeapon())
        SelectWeaponCommander(unit, context, _lastCurrentWeapon);
    }

    if( notForbbidenMove )
    {
      MotionPathItem item = MotionPathItem(moveWanted);
      if (SetMoveQueue(item, prec <= SimulateVisibleFar) && forceAction)
      {
        _moves.AddForceMove(item, FutureVisualState()._moves);
      }
    }
    else
    {
      MoveId moveWanted = map ? Type()->GetMove(map, ManActCanNotMove) : GetDefaultMove();
      MotionPathItem item = MotionPathItem(moveWanted);
      if (SetMoveQueue(item, prec <= SimulateVisibleFar))
      {
        _moves.AddForceMove(item, FutureVisualState()._moves);
      }
    }
  }
  else
  {
    moveWanted = MoveIdNone;
  }

  SetTargetMove(moveWanted);
  //LogF("%s: Move selected %s",cc_cast(GetDebugName()),cc_cast(GetMovesType()->GetMoveName(moveWanted)));

  _lastCurrentWeapon = _weaponsState.ValidatedCurrentWeapon();
  
  if (QIsManual())
  {
    // advanced missile control
    // check if last shot is missile
    if (LauncherSelected())
    {

      TurretContext context;
      context._turret = NULL;
      context._turretType = NULL;
      context._gunner = this;
      context._weapons = &_weaponsState;

      ManualControlMissile(context);
    }
  }

}

int Man::GetAutoUpDegree() const
{
  AIBrain *unit = Brain();
  if (!unit) return ManPosStand;

  bool hasWeapon = _itemInHands != NULL;
  return GetUpDegree(unit->GetCombatModeLowLevel(), false, hasWeapon, true);
}

MoveId Man::GetDefaultMove() const
{
  int deg = GetAutoUpDegree();
  MoveId move = GetMovesType()->GetDefaultMove(deg);
  if (move != MoveIdNone)
    return move;

  // Some imported survivor animation configs leave the posture's Default
  // action unmapped. MotionPath::PatchNoMove then receives MoveIdNone and the
  // character stays in bind pose with no movement actions. Recover a living,
  // upright character to the known base idle state so the normal action map
  // can drive walking and running again.
  if (!_isDead && !IsDown())
  {
    MoveId fallbackMove = GetMovesType()->GetMoveId("AidlPercMstpSnonWnonDnon");
    if (fallbackMove == MoveIdNone)
      fallbackMove = GetMovesType()->GetMoveId("AmovPercMstpSnonWnonDnon");
    if (fallbackMove != MoveIdNone)
      return fallbackMove;
  }
  return move;
}

MoveId Man::GetDefaultMove(ManAction action) const
{
  if (_isDead)
  {
    action = ManActDie;
  }
  int deg = GetAutoUpDegree();
  return GetMovesType()->GetMove(deg, action);
}

const MoveIdExt &Man::GetMove(ManAction action) const
{
  // get action map corresponding to current move
  const ActionMap *map = GetMovesType()->GetActionMap(_moves.GetPrimaryMove(FutureVisualState()._moves).id, AnimSubIx());
  if (map)
  {
    return GetMovesType()->GetMove(map, action);
  }

  int deg = GetActUpDegree(FutureVisualState());
  //if (action<ManActNormN) return Type()->GetMove(deg, action);
  // TODO: use some other action list
  return GetMovesType()->GetMove(deg, action);
}

const MoveIdExt &Man::GetMove(RString action) const
{
  // get action map corresponding to current move
  const ActionMap *map = GetMovesType()->GetActionMap(_moves.GetPrimaryMove(FutureVisualState()._moves).id, AnimSubIx());
  if (map)
  {
    return GetMovesType()->GetMove(map, action);
  }

  int deg = GetActUpDegree(FutureVisualState());
  //if (action<ManActNormN) return Type()->GetMove(deg, action);
  // TODO: use some other action list
  return GetMovesType()->GetMove(deg, action);
}

// based on actual move

Vector3 Man::GetActionMoveShift(RString action) const
{
  MoveId move = GetMove(action);
  const MoveInfoMan *prim = Type()->GetMoveInfo(move, AnimSubIx());
  if (!prim)
    return VZero;

  const AnimationRT *primA = *prim;
  if (!primA)
    return VZero;

  return primA->GetStep();
}

float Man::GetCursorInaccuracy() const
{
  float inacc=RifleInaccuracy()*GetInvAbility(AKAimingShake);

  // when holding the breath, rifle is shaking much less
  // as we are loosing air, rifle starts shaking again
  //inacc *= 1.0f-0.75f*_holdBreath*_breathAir;
  inacc *= 1.0f-0.75f*_holdBreath+(1-_breathAir)*0.75f/0.5f;
  
  return inacc-0.005f;
}

float Man::GetCursorSize(Person *person) const
{
  if (WeaponsDisabled()) return 1.0f;
  return base::GetCursorSize(person);
}

const CursorTextureInfo *Man::GetCursorTexture(Person *person)
{
  if (WeaponsDisabled()) return NULL;
  //if (IsWeaponLowered()) return NULL;
  // check if 
  return base::GetCursorTexture(person);
}

const CursorTextureInfo *Man::GetCursorAimTexture(Person *person)
{
  // check if target designator is active
  if (WeaponsDisabled()) return NULL;
  // check if 
  return base::GetCursorAimTexture(person);
}

bool CheckSupply(EntityAI *vehicle, EntityAI *parent, SupportCheckF check, float limit, bool now);

static bool FormatWeaponSystem(UIActionParams &params, UIActionType action, const EntityAIFull *veh, RString weaponName)
{
  if (veh)
  {
    if (weaponName.GetLength() > 0)
    {
      Ref<WeaponType> weapon = dynamic_cast<WeaponType *>(VehicleTypes.New(weaponName));
      if (weapon)
      {
        params.Add(CreateTextASCII(NULL, weapon->_displayName));
        return true;
      }
      else
      {
        RptF("Action %s - wrong weapon name", cc_cast(FindEnumName(action)));
        return false;
      }
    }
    else
    {
      RptF("Action %s - empty weapon name", cc_cast(FindEnumName(action)));
      return false;
    }
  }
  else
  {
    // regular option for commanding menu
    // RptF("Action %s - missing target", cc_cast(FindEnumName(action)));
    return false;
  }
}

bool Man::GetActionParams(UIActionParams &params, const Action *action, AIBrain *unit) const
{
  switch (action->GetType())
  {
  case ATStrokeFist:
  case ATStrokeGun:
    // No parameters
    return true;
#if _ENABLE_CONVERSATION
  case ATTalk:
    params.Add(CreateTextASCII(NULL, GetPersonName()));
    return true;
  case ATTell:
    {
      const ActionTell *actionTyped = static_cast<const ActionTell *>(action);
      params.Add(CreateTextASCII(NULL, actionTyped->GetText()));
    }
    return true;
#endif
  case ATTouchOff:
    {
      int n = 0;
      for (int i=0; i<_pipeBombs.Size();)
      {
        if (_pipeBombs[i])
        {
          if (_pipeBombs[i]->FutureVisualState().Position().Distance2(FutureVisualState().Position()) <= Square(300)) n++;
          i++;
        }
        else unconst_cast(_pipeBombs).Delete(i);
      }
      params.Add(CreateTextASCII(NULL, Format("%d", n)));
      return true;
    }
  }
  return base::GetActionParams(params, action, unit);
}

void Man::ProcessUIAction(const Action *action)
{
  // this function is called when MFUIAction is processed
  // (actions peformed after some animation is played)
  
  AIBrain *agent = Brain();
  if (!agent) return;
  action->Process(agent);
}

bool Man::HasPendingBomb() const
{
  return _pipeBombs.Size()>0 && _pipeBombsAuto;

}

void Man::TouchOffBombs()
{
  for (int i=0; i<_pipeBombs.Size();)
  {
    Entity *veh = _pipeBombs[i];
    PipeBomb *bomb = dyn_cast<PipeBomb>(veh);
    if (!bomb)
    {
      _pipeBombs.Delete(i);
      continue;
    }
    Assert(bomb->GetOwner() == this);
    if (FutureVisualState().Position().Distance2(bomb->FutureVisualState().Position()) > Square(300.0f))
    {
      i++;
      continue;
    }
    bomb->Explode();
    _pipeBombs.Delete(i);
  }
}

/*!
\patch 1.05 Date 7/18/2001 by Jirka
- Fixed: hide body sometimes did not work in MP
*/
void Man::PerformAction(const Action *action, AIBrain *unit)
{
  if (!unit || !unit->LSCanProcessActions()) return;
  // note: action is performed by target of the action
  // unit is who activated the action
  switch (action->GetType())
  {
    case ATHideBody:
    {
      Object *tgt = action->GetTarget();
      if (tgt)
      {
        Man *man = dyn_cast<Man>(tgt);
        if (man)
        {
          // only owner can process actions
          DoAssert(GetNetworkManager().IsServerOwner());
          man->_hideBodyWanted = 1;
        }
      }
      break;
    }
    case ATTouchOff:
    {
      TouchOffBombs();
      break;
    }
    case ATPutItem:
      {
        const ActionMagazine *actionTyped = static_cast<const ActionMagazine *>(action);
        Magazine *item = actionTyped->GetMagazine();
        if (!item) return;
        PutAction(item);
      }
      break;
    case ATSitDown:
      Assert(action->GetTarget() == this);
      PlayActionNowGlobal("SitDown");
      break;
    case ATSalute:
      Assert(action->GetTarget() == this);
      PlayActionNowGlobal("Salute");
      break;
    case ATTaunt:
      Assert(action->GetTarget() == this);
      PlayActionNowGlobal("Taunt");
      break;
    case ATClap:
      Assert(action->GetTarget() == this);
      PlayActionNowGlobal("Clap");
      break;
    case ATPoint:
      Assert(action->GetTarget() == this);
      PlayActionNowGlobal("Point");
      break;
    case ATThumbsUp:
      Assert(action->GetTarget() == this);
      PlayActionNowGlobal("ThumbsUp");
      break;
    case ATSurrender:
      Assert(action->GetTarget() == this);
      PlayActionNowGlobal("Surrender");
      break;
    case ATGetOver:
      Assert(action->GetTarget() == this);
	    if( IsAbleToGetOver() )
      {
        // ANIMS-VAULT: usa sempre a animacao de subir muro (Ovr), nunca a de pulo (Mjmp),
        // independente de estar andando/correndo.
        RStringB curName = GetMovesType()->GetMoveName(_moves.GetPrimaryMove(FutureVisualState()._moves).id);
        const char *n = cc_cast(curName);
        RStringB vault = "AovrPercMstpSnonWnonDf";
        if (MoveNameHas(n,"Pknl")!=NULL)
        {
          if (MoveNameHas(n,"Wrfl")!=NULL) vault = "AovrPknlMstpSlowWrflDf";
          else if (MoveNameHas(n,"Wmel")!=NULL) vault = "AovrPknlMstpSlowWmelDf";
          else vault = "AovrPknlMstpSnonWnonDf";
        }
        else if (MoveNameHas(n,"Wrfl")!=NULL)
        {
          vault = (MoveNameHas(n,"Sras")!=NULL) ? "AovrPercMstpSrasWrflDf" : "AovrPercMstpSlowWrflDf";
        }
        else if (MoveNameHas(n,"Wmel")!=NULL)
        {
          vault = (MoveNameHas(n,"Sras")!=NULL) ? "AovrPercMstpSrasWmelDf" : "AovrPercMstpSlowWmelDf";
        }
        if (!PlayActionNowGlobal("GetOver")) PlayMoveNow(vault);

        // ANIMS-VAULT: alem da animacao, aplica um impulso que garante a
        // transposicao fisica do obstaculo (a animacao sozinha nao cruza muros).
        if (this==GWorld->GetRealPlayer())
        {
          Vector3Val d = FutureVisualState().Direction();
          Vector3 fwd(d.X(),0,d.Z());
          if (fwd.SquareSize()>0.0001f) fwd.Normalize(); else fwd = VForward;
          FutureVisualState()._speed = fwd * 2.3f;
          FutureVisualState()._speed[1] = 6.0f;
          _landContact = false;
          _freeFallStartTime = TIME_MAX;
          FreeFall(1.4f);
        }
      }
      break;
    case ATStrokeFist:
      Assert(action->GetTarget() == this);
      PlayActionGlobal("StrokeFist");
      break;      
    case ATStrokeGun:
      Assert(action->GetTarget() == this);
      PlayActionGlobal("StrokeGun");
      break;      
    case ATCancelAction:
      CancelAction();
      break;
#if _ENABLE_CONVERSATION
    case ATTalk:
      {
        Assert(action->GetTarget() == this);
        Person *askedPerson = dyn_cast<Person>(action->GetTarget());
        AIBrain *askedUnit = askedPerson ? askedPerson->Brain() : NULL;
        if (askedUnit && askedUnit->LSIsAlive() && askedUnit->GetKBCenter())
        {
          void ProcessConversation(AIBrain *askingUnit, AIBrain *askedUnit, const KBMessageInfo *question);
          ProcessConversation(unit, askedUnit, NULL);
        }
      }
      break;
    case ATTell:
      {
        // target unit
        Assert(action->GetTarget() == this);
        Person *askedPerson = dyn_cast<Person>(action->GetTarget());
        AIBrain *askedUnit = askedPerson ? askedPerson->Brain() : NULL;

        if (askedUnit && askedUnit->LSIsAlive())
        {
          const ActionTell *actionTyped = static_cast<const ActionTell *>(action);

          void KBTell(AIBrain *askingUnit, AIBrain *askedUnit, const KBMessageInfo *info, bool forceRadio = false, ChatChannel chan=CCNone);
          KBTell(unit, askedUnit, actionTyped->GetMessage());
        }
      }
      break;

    case ATIRLaserOn:
      {
        _irEnabled = true;
      }
      break;

    case ATIRLaserOff:
      {
        _irEnabled = false;
      }
      break;

    case ATGunLightOn:
      {
        _lightWanted = true;
      }
      break;

    case ATGunLightOff:
      {
        _lightWanted = false;
      }
      break;
#endif    
# if _ENABLE_CARRY_BODY //BattlefieldClearance & POW
    case ATCaptureUnit:
    case ATReleaseUnit:
    case ATHandcuff:
    case ATReleaseHandcuff:
    case ATCarryBody:
      action->PerformScripted(unit);
      break;
#endif //_VBS3

    default:
      base::PerformAction(action,unit);
      break;
  }
}

void Man::CancelAction()
{
  //to not stuck if visual state is paused
  ContinueMove();
  if (_moves.GetPrimaryMove(FutureVisualState()._moves).context)
  {
    _moves.CancelPrimaryAction(FutureVisualState()._moves);

    // stand still
    ActionMap *map = Type()->GetActionMap(_moves.GetPrimaryMove(FutureVisualState()._moves).id, AnimSubIx());
    MoveId moveWanted = map ? Type()->GetMove(map, ManActStop) : GetDefaultMove();
    SetTargetMove(moveWanted);
    SetMoveQueue(MotionPathItem(moveWanted), true);
    return;
  }
  _moves.CancelForcedActions();
}

static bool CheckEnemySoldier(AIBrain *unit,Vector3Par pos, Vector3Par dir)
{
  float hitDistance = 0.7f;
  float hitRadius = 0.8f;

  // check circular area (pos+dir*hitDistance,hitRadius)
  // scan targets? or vehicles? or objects?

  Vector3 center = pos+dir*hitDistance;
  int xMin,xMax,zMin,zMax;
  ObjRadiusRectangle(xMin,xMax,zMin,zMax,center,center,hitRadius);

  for (int x=xMin; x<=xMax; x++) for (int z=zMin; z<=zMax; z++)
  {
    const ObjectList &list = GLandscape->GetObjects(z, x);
    for (int i=0; i<list.Size(); i++)
    {
      Object *obj = list[i];
      if (obj->FutureVisualState().Position().DistanceXZ2(center)>hitRadius) continue;
      Man *man = dyn_cast<Man>(obj);
      if (!man) continue;
      // TODO: consider checking vehicle armor
      // enable hitting any soft object
      if (man->IsDown()) continue;
      if (!unit->IsEnemy(man->GetTargetSide())) continue;
      return true;
      
    }
  }
  return false;
}

TypeIsSimple(const MagazineType *)

bool Man::CanCancelAction() const
{
  if (_isDead) return false;
  if (!QIsManual()) return false;
  AIBrain *unit = Brain();
  if (!unit) return false;
  if (!unit->IsFreeSoldier()) return false;

  const ActionContextBase *ctx = GetActionInProgress();
  if (ctx)
  {
    switch (ctx->function)
    {
    case MFGetIn:
    case MFThrow:
    case MFTakeInHands:
      return true;
    case MFUIAction:
      {
        const ActionContextUIAction *ctxUI = static_cast<const ActionContextUIAction *>(ctx);
        switch (ctxUI->GetActionType())
        {
        case ATGear:
          return true;
        };
      }
      return false;          
    }
  }
  return false;          
}

/*!
\patch_internal 1.01 Date 6/25/2001 by Ondra.
Changes necessary because EnableWeaponManipulation function has been changed.
\patch 2.01 Date 1/3/2003 by Jirka
- Fixed: Bug in testing of distance unit and satchel charges (mines), if player was in vehicle
\patch 5153 Date 4/4/2007 by Jirka
- Fixed: Touch off action available also in vehicles now
*/

void Man::GetActions(const std::function<void(const UIAction &action)> &add, AIBrain *unit)
{
  if (_hideBody>0) return;


#if !_VBS2
  if (unit && unit->GetPerson() == this && unit->GetVehicleIn() != NULL)
  {
    // when in vehicle, only some actions on myself will be collected
  }
  else
#endif
  {
    base::GetActions(add, unit);
  }
  
  if (!unit) return;
  bool free = unit->IsFreeSoldier();

  Person *veh = unit->GetPerson();

  if (GetAllocSupply() && GetAllocSupply() != veh && !unit->IsPlayer()) return; // wait

  // dead or unconscious characters can be looted
  if (_isDead || _brain && _brain->LSCanBeLooted())
  {
    if (free)
    {
      bool reloading = unit->GetPerson()->IsActionInProgress(MFReload);
      if (!reloading)
      {
        UIAction action(new ActionBasic(ATGear, this));
        int index = GetType()->GetSupplyPointIndex();
        if (index >= 0) action._memoryPoints.Add(index);
        add(action);
      }
    }
  }

  if (_isDead)
  {
    // corpse
    if (free)
    {
      bool reloading = unit->GetPerson()->IsActionInProgress(MFReload);
      if (!reloading)
      {
        // check if Person is able to hide bodies
        if (_hideBodyWanted==0) // body not hidden yet
        {
          Man *vehMan = dyn_cast<Man>(veh);
          if (vehMan && vehMan->Type()->_canHideBodies)
          {
            UIAction action(new ActionBasic(ATHideBody, this));
            int index = GetType()->GetSupplyPointIndex();
            if (index >= 0) action._memoryPoints.Add(index);
            add(action);
          }
        }

#if _VBS3 && _ENABLE_CARRY_BODY //BattlefieldClearance
        if (_hideBodyWanted==0 && !IsAttached()) // body not hidden or already attached
        {
          Man *vehMan = dyn_cast<Man>(veh);
          if(vehMan && vehMan->RenderVisualState().Position().Distance2(Position()) < Square(3.0f))
          {
            UIAction action(new ActionBasic(ATCarryBody, this));
            int index = GetType()->GetSupplyPointIndex();
            if (index >= 0) action._memoryPoints.Add(index);
            add(action);
          }
        }
#endif
      } // if (!reloading)
    }
  } // dead body
  else
  {
    // living
    if (unit->GetPerson() == this)
    {
      // some actions to himself
      
      if (free) // is free soldier
      {
        const ActionMap *map = Type()->GetActionMap(_moves.GetPrimaryMove(FutureVisualState()._moves).id, AnimSubIx());

        // add sit down (and some other actions) only when there is such action available
        if (map)
        {
          if (Type()->GetMove(map, CAST_TO_ENUM(ManAction, ManActSitDown)).operator MoveId() != CAST_TO_ENUM(MoveId, MoveIdNone))
          {
            UIAction action(new ActionBasic(ATSitDown, this));
            add(action);
          }
          if (Type()->GetMove(map, CAST_TO_ENUM(ManAction, ManActSalute)).operator MoveId() != CAST_TO_ENUM(MoveId, MoveIdNone))
          {
            UIAction action(new ActionBasic(ATSalute, this));
            add(action);
          }
          if (Type()->GetMove(map, CAST_TO_ENUM(ManAction, ManActSurrender)).operator MoveId() != CAST_TO_ENUM(MoveId, MoveIdNone))
          {
            UIAction action(new ActionBasic(ATSurrender, this));
            add(action);
          }
          if (Type()->GetMove(map, CAST_TO_ENUM(ManAction, ManActTaunt)).operator MoveId() != CAST_TO_ENUM(MoveId, MoveIdNone))
          {
            UIAction action(new ActionBasic(ATTaunt, this));
            add(action);
          }
          if (Type()->GetMove(map, CAST_TO_ENUM(ManAction, ManActClap)).operator MoveId() != CAST_TO_ENUM(MoveId, MoveIdNone))
          {
            UIAction action(new ActionBasic(ATClap, this));
            add(action);
          }
          if (Type()->GetMove(map, CAST_TO_ENUM(ManAction, ManActPoint)).operator MoveId() != CAST_TO_ENUM(MoveId, MoveIdNone))
          {
            UIAction action(new ActionBasic(ATPoint, this));
            add(action);
          }
          if (Type()->GetMove(map, CAST_TO_ENUM(ManAction, ManActThumbsUp)).operator MoveId() != CAST_TO_ENUM(MoveId, MoveIdNone))
          {
            UIAction action(new ActionBasic(ATThumbsUp, this));
            add(action);
          }
          if (Type()->GetMove(map, CAST_TO_ENUM(ManAction, ManActGetOver)).operator MoveId() != CAST_TO_ENUM(MoveId, MoveIdNone))
          {
            UIAction action(new ActionBasic(ATGetOver, this));
            add(action);
          }
        }
      } // soldier

      // pipe bombs
      bool isNear = false;
      for (int i=0; i<_pipeBombs.Size();)
      {
        Vehicle *veh = _pipeBombs[i];
        PipeBomb *bomb = dyn_cast<PipeBomb>(veh);
        if (!bomb)
        {
          _pipeBombs.Delete(i);
          continue;
        }
        Assert(bomb->GetOwner() == this);
        float dist2 = WorldPosition(RenderVisualState()).Distance2(bomb->RenderVisualStateScope(true)->Position());
        if (!isNear) isNear = dist2 <= Square(300);
        i++;
      }
      if (isNear)
      {
        UIAction action(new ActionBasic(ATTouchOff, this));
        add(action);
      }

      // night vision
      if (IsNVEnabled())
      {
        UIActionType type = IsNVWanted() ? ATNVGogglesOff : ATNVGoggles;
        UIAction action(new ActionBasic(type, this));
        add(action);
      }
    } // if (himself)
#if _ENABLE_CONVERSATION
    else if (unit->IsPlayer() && free)
    {
      AIBrain *askedUnit = Brain();
      if
      (
        unit->GetKBCenter() && !unit->IsSpeaking() && !unit->IsListening() &&
        askedUnit && askedUnit->GetKBCenter() && !askedUnit->IsSpeaking() && !askedUnit->IsListening() // ask only unit which support it
      )
      {
        // conversation enabled, check if something to tell
        ConversationContext context(unit, askedUnit, NULL);
        if (context._handlerResult.GetNil())
        {
          // old reaction (based on class UI)
          UIAction action(new ActionBasic(ATTalk, this));
          add(action);
        }
        else
        {
          // new reaction (based on event handler)
          const GameArrayType &array = context._handlerResult;
          if (array.Size() == 1)
          {
            // single menu item
            const GameArrayType &subarray = array[0];
            if (subarray.Size() == 4 && subarray[1].GetType() == GameString && subarray[2].GetType() == GameString && subarray[3].GetType() == GameArray)
            {
              // Sentence
              // text
              RString text;
              if (subarray[0].GetType() == GameString)
              {
                text = subarray[0];
              }
              else if (subarray[0].GetType() == GameArray)
              {
                const GameArrayType &textDesc = subarray[0];
                if (textDesc.Size() > 0 && textDesc[0].GetType() == GameString)
                  text = textDesc[0];
              }
              if (text.GetLength() > 0)
              {
                // message
                Ref<KBMessageInfo> message = context._askingUnit->CreateKBMessage(subarray[1], subarray[2]);
                if (!message)
                  return; // FIX: crash, see news:h7o49h$uqv$1@new-server.localdomain
                // attributes
                const GameArrayType &attrArray = subarray[3];
                for (int i=0; i<attrArray.Size(); i++)
                {
                  // TODO: check the format of arguments
                  context._askingUnit->SetKBMessageArgument(message, attrArray[i]);
                }
                // create the action
                UIAction action(new ActionTell(ATTell, this, text, message));
                add(action);
              }
            }
            else if (subarray.Size() == 2 && subarray[1].GetType() == GameArray)
            {
              // submenu
              UIAction action(new ActionBasic(ATTalk, this));
              add(action);
            }
          }
          else if (array.Size() > 1)
          {
            UIAction action(new ActionBasic(ATTalk, this));
            add(action);
          }
        }
      }
    }
#endif 
  } // if (dead) else
}


#if _VBS3
inline bool GetOpticsTurret(const Man* man, TurretContext &context)
{
  //TODO: only for player?
  context._turret = NULL;
  if(man->Brain() && GWorld->IsCameraActive(CamGunner))
  {
    Transport* transport = man->Brain()->GetVehicleIn();
    if(transport) 
      return (transport->FindOpticsTurret(man, context) && context._turret);
  }
  return false;
}
#endif


const WeaponType *Man::GetSpecialItem(WeaponSimulation simulation) const
{
  const WeaponType *result = NULL;
  unconst_cast(_weaponsState).ForEachWeapon([&result, simulation](Weapon *weapon) -> bool
  {
    const WeaponType *type = weapon->Type();
    if (type && type->GetSimulation() == simulation)
    {
      result = type;
      return true; // found
    }
    return false; // continue
  });
  return result;
}

bool Man::HasTiOptics() const
{
  return _weaponsState.HasCurrentWeaponTi();
}

/*!
\patch 1.01 Date 6/26/2001 by Ondra.
- Fixed: display NV from external camera for dead soldier.
*/

bool Man::IsNVEnabled() const
{
  if (QIsManual())
  {
    if (IsDamageDestroyed()) return false;
    // if (GWorld->GetCameraType() == CamGroup) return false;
    if (GWorld->HasMap()) return false;
#if _VBS3
  //check current turret optic, gun optic
  const ViewPars* viewPars = GetViewPars();
  if(viewPars)
    return viewPars->HasTI() || viewPars->HasNV();
#endif

  }
  return _hasNVG;
}

bool Man::IsNVReady() const
{
  const WeaponType *goggles = GetSpecialItem(WSNVGoggles);
  if (goggles && goggles->_muzzles.Size()>0)
  {
    const MuzzleType *muzzle = goggles->_muzzles[0];
    LODShape *oShape = muzzle->_opticsModel;
    if (!oShape || oShape->FindSimplestLevel()<0) return true;
    // we do not need the vertex buffer, only shape and geometry
    // we do not care about time management - this is quite important
    if (!oShape->LevelRef(0).IsLoaded())
    {
      oShape->LevelRef(0).RequestLoading();
      return false;
    }
    // request first frame to be ready
    // preload textures
    ShapeUsed shape = oShape->Level(0);
    if (!shape->IsGeometryLoaded())
    {
      shape->RequestGeometry();
      return false;
    }
    return shape->PreloadTextures(0, NULL);
  }

  return true;
}

bool Man::IsFlirWanted() const
{
  return _flir;
}

bool Man::IsNVWanted() const
{
#if _VBS3
  VisionMode vm = GetVisionMode();
  return (vm > VMDv && vm < VMInvalid);
#else
  return _nvg;
#endif
}

void Man::SetNVWanted(bool set)
{
  _nvg = set;
}

#if _VBS3

const ViewPars* Man::GetViewPars(bool includeTurrets) const
{
  if(includeTurrets)
  {
    TurretContext context;
    if(GetOpticsTurret(this, context))
    {
      return context._turret->GetCurrentViewPars();
    }
  }

  if (GWorld && GWorld->IsCameraActive(CamGunner))
  {
    int curWeapon = _weaponsState.ValidatedCurrentWeapon();
    if (curWeapon >= 0 && !IsDamageDestroyed())
    {
      const MagazineSlot &slot = _weaponsState._magazineSlots[curWeapon];
      if(slot._muzzle)
      {
        Assert(slot._tiModeIndex >= slot._muzzle->_viewOptics._thermalModes.Size())
          const ViewPars* viewPars = &slot._muzzle->_viewOptics;
        if(viewPars->Initialized())
          return viewPars;
      }
    }
  }
  return NULL;
  //  return _lookAroundViewPars;
}

VisionMode Man::GetVisionMode() const
{
  TurretContext context;
  if(GetOpticsTurret(this, context))
  {
    return context._turret->GetVisionMode();
  }

  if (GWorld && GWorld->IsCameraActive(CamGunner))
  {
    int curWeapon = _weaponsState.ValidatedCurrentWeapon();
    if (curWeapon >= 0 && !IsDamageDestroyed())
    {
      return _weaponsState._magazineSlots[curWeapon]._visionMode;
    }
  }
  return VisionMode(_nvg);
}

int Man::GetTiModus() const
{
  TurretContext context;
  if(GetOpticsTurret(this, context))
  {
    return context._turret->GetTiModus();
  }

  if (GWorld && GWorld->IsCameraActive(CamGunner))
  {
    int curWeapon = _weaponsState.ValidatedCurrentWeapon();
    if (curWeapon >= 0 && !IsDamageDestroyed())
    {
      const MagazineSlot &slot = _weaponsState._magazineSlots[curWeapon];
      if(slot._muzzle && slot._muzzle->_viewOptics.Initialized())
      {
        return slot._muzzle->_viewOptics._thermalModes[slot._tiModeIndex];
      }
    }
  }

  LogF("[m] GetTIModus without Gunner view?");
  return 0;
}

void Man::NextVisionMode()
{
  TurretContext context;
  if(GetOpticsTurret(this, context))
  {
    context._turret->NextVisionMode();
    return;
  }

  if (GWorld && GWorld->IsCameraActive(CamGunner))
  {
    int curWeapon = _weaponsState.ValidatedCurrentWeapon();
    if (curWeapon >= 0 && !IsDamageDestroyed())
    {
      MagazineSlot &slot = _weaponsState._magazineSlots[curWeapon];
      if(slot._muzzle && slot._muzzle->_viewOptics.Initialized())
      {
        slot._muzzle->_viewOptics.NextVisionMode(slot._visionMode, slot._tiModeIndex);
        return;
      }
    }
  }
#if _VBS3_TI
  GEngine->SetThermalVisionWanted(false);
#endif
  _nvg = !_nvg; 
}

void Man::ApplyVisionMode()
{
  TurretContext context;
  if(GetOpticsTurret(this, context))
  {
    context._turret->ApplyVisionMode();
    return;
  }

  if (GWorld && GWorld->IsCameraActive(CamGunner))
  {
    int curWeapon = _weaponsState.ValidatedCurrentWeapon();
    if (curWeapon >= 0 && !IsDamageDestroyed())
    {
      MagazineSlot &slot = _weaponsState._magazineSlots[curWeapon];
      if(slot._muzzle && slot._muzzle->_viewOptics.Initialized())
      {
        slot._muzzle->_viewOptics.ApplyVisionMode(slot._visionMode, slot._tiModeIndex);
        return;
      }
    }
  }
#if _VBS3_TI
  //no weapon optics nv modes yet!
  GEngine->SetThermalVisionWanted(false);
#endif
}

#endif //_VBS3

class FindTurretSpec : public ITurretFunc
{
protected:
  const Person *unit;
public:
  const VehPosRelatives *turretSpec; //result

  FindTurretSpec(const Person *u) : unit(u),turretSpec(NULL) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (unit==context._gunner) 
    {
      turretSpec = &context._turretType->_turretSpec;
      return true; //stop search
    }
    return false; // all turrets
  }
};

const VehPosRelatives* Man::GetVehPosRelatives() const
{
  AIBrain *brain = Brain();
  if (!brain) return NULL;
  Transport *vehIn = brain->GetVehicleIn();
  if (vehIn && !RenderVisualState()._gettingInOut)
  { //inside vehicle
    if (brain->IsDriver()) return &vehIn->Type()->_pilotSpec;
    if (brain->IsInCargo()) 
    { // find the cargo position
      for (int i=0; i<vehIn->GetManCargo().Size(); i++)
      {
        if (vehIn->GetManCargo()[i] == this)
        {
          int n = vehIn->Type()->_cargoSpec.Size();
          if (n == 0) return NULL;
          saturate(i, 0, n - 1);
          return &vehIn->Type()->_cargoSpec[i];
        }
      }
      return NULL; //cargo position not found
    }
    // Is gunner in turret
    FindTurretSpec func(this);
    vehIn->ForEachTurret(func);
    return func.turretSpec;
  }
  else
  { //get my _pilotSpec otherwise
    return &Type()->_pilotSpec;
  }
}

inline bool FindGunnerTurret(const AIBrain* brain, TurretContext &context)
{
  if (GWorld->FocusOn() == brain)
  {
    Transport *transport = brain->GetVehicleIn();

    if (transport && transport->VisionModesEnabled() && brain->GetPerson() && transport->FindTurret(brain->GetPerson(), context))
    {
      return (context._turret && context._turret->HasVisionModes(context._turretType));
    }
  }

  return false;
}

void Man::NextVisionMode()
{
  if (Brain() && GWorld->GetCameraType() == CamGunner)
  {
    // turrets
    TurretContext context;
    if (FindGunnerTurret(Brain(), context))
    {
      // select next turret vision mode
      if (context._turret->NextVisionMode(context._turretType))
      {
       if(context._turret->VisionModesCount(context._turretType)>1) GWorld->SetTitleEffect(CreateTitleEffect(TitBlackIn, ""));
      }
      return;
    }

    // driver
    if (Brain()->IsDriver())
    {
      Transport *transport = Brain()->GetVehicleIn();
      if (transport && transport->HasVisionModes() && transport->NextVisionMode())
      {
        if(transport->VisionModesCount()>1) GWorld->SetTitleEffect(CreateTitleEffect(TitBlackIn, ""));
        return;
      }
    }

    // soldier
    if (!Brain()->GetVehicleIn())
    {
      if (_weaponsState.HasVisionModes() && _weaponsState.NextVisionMode() && !IsDamageDestroyed())
      {
        if(_weaponsState.VisionModesCount()>1) GWorld->SetTitleEffect(CreateTitleEffect(TitBlackIn, ""));
        return;
      }
    }
  }

  // flir only in turret or optics
  _flir = false;

  // switch NVG if owned by soldier
  if (IsNVEnabled()) _nvg = !_nvg;
}

inline void SelectVisionMode(VisionMode vMode, int flirIndex, bool &nvg, bool &flir)
{
  // vision modes are exclusive
  nvg = flir = false;
  GEngine->SetThermalVisionWanted(false);
  if (vMode == VMNvg) nvg = true;
  if (vMode == VMTi)
  {
    flir = true;
    GEngine->SetTIMode(flirIndex);
    GEngine->SetThermalVisionWanted(true);
  }
}

void Man::ApplyVisionMode()
{
  if (Brain() && GWorld->GetCameraType() == CamGunner)
  {
    TurretContext context;
    // turret gunner
    if (FindGunnerTurret(Brain(), context))
    {
      if (_wasNVGStateSet) _wasNVGActive = _nvg, _wasNVGStateSet = false;

      int flirIndex;
      VisionMode vMode = context._turret->GetVisionModePars(flirIndex);
      SelectVisionMode(vMode, flirIndex, _nvg, _flir);
        
      // no NVG 2D optics drawing, Man::DrawNVOptics()
      _useVisionModes = true;
      return;
    }

    // driver
    if (Brain()->IsDriver())
    {
      Transport *transport = Brain()->GetVehicleIn();
      if (transport && transport->VisionModesEnabled() && transport->HasVisionModes())
      {
        if (_wasNVGStateSet) _wasNVGActive = _nvg, _wasNVGStateSet = false;

        int flirIndex;
        VisionMode vMode = transport->GetVisionModePars(flirIndex);
        SelectVisionMode(vMode, flirIndex, _nvg, _flir);

        // no NVG 2D optics drawing, Man::DrawNVOptics()
        _useVisionModes = true;
        return;
      }
    }

    float cTrans = GWorld->GetCameraTypeTransition();  
    bool enableFlir = (cTrans > 0.75 || cTrans < 0.3);

    // soldier
    if (enableFlir && !Brain()->GetVehicleIn())
    {
      if (_weaponsState.HasVisionModes() && !IsDamageDestroyed())
      {
        VisionMode vMode;
        int flirIndex;
        _weaponsState.GetVisionModePars(vMode, flirIndex);

        if (_wasNVGStateSet) _wasNVGActive = _nvg, _wasNVGStateSet = false;
        // change immediate
        SelectVisionMode(vMode, flirIndex, _nvg, _flir);

        // no NVG 2D optics drawing, Man::DrawNVOptics()
        _useVisionModes = true;      
        return;
      }
    }
  }

  _useVisionModes = false;
  // switch on NVG if was on before applying vision mode
  if (!_wasNVGStateSet) _nvg = _wasNVGActive, _wasNVGStateSet = true;
  _flir = false;
  GEngine->SetThermalVisionWanted(_flir);
}

void CutScene(const char *name);

struct MagazineInWeapon
{
  Ref<MuzzleType> muzzle;
  Ref<Magazine> magazine;
};
TypeIsMovableZeroed(MagazineInWeapon)


/*!
\patch 1.34 Date 12/03/2001 by Ondra
- Fixed: AI was not able to reload after taking magazine from corpse
when it was completely out of ammo before.
*/

bool Man::Supply(EntityAIFull *vehicle, const Action *action)
{
  return base::Supply(vehicle, action);
}

float Man::Rigid() const
{
 // how much energy is transfered in collision
  return 0.6f;
}

float Man::MaxDeflectionAngleCos() const
{
  // no deflection possible for human body
  // what about grenades?
  return 0;
}

bool Man::HasGeometry() const
{
  //return !_isDead;

  if (_ignoreCollisionIfStuck) // ignore all collisions after get out... 
    return false;

  const float MIN_DEAD_SIM_TIME = 5.0f;
  return !_isDead || _whenKilled + MIN_DEAD_SIM_TIME > Glob.time || _freeFall;
}

void LogFVector( Vector3Val val )
{
  LogF("  %10.5f,%10.5f,%10.5f",val.X(),val.Y(),val.Z());
}

void LogFMatrix( Matrix4Val val )
{
  LogFVector(val.DirectionAside());
  LogFVector(val.DirectionUp());
  LogFVector(val.Direction());
  LogFVector(val.Position());
}

void Man::RecalcGunTransform()
{
  ProtectedVisualState<VisualState> vs = FutureVisualStateScope();
  float headXRotDone = 0;
  float headYRotDone = 0;
  bool moveHead = false;
  if (fabs(_gunXRot)+fabs(_gunYRot)+fabs(_gunZRecoil)>1e-6f)
  {
    Vector3 gunAxis=GetWeaponCenterMoves(vs);
    const float xViaAiming = HDegree(15);
    const float xViaAimingAiming = HDegree(10);
    const float yViaAiming = HDegree(15);
    const float yViaAimingAiming = HDegree(10);
    
    // aim using upper torso pars (shoulders, arms, head)
    float xRot = fSign(_gunXRot)*InterpolativC(fabs(_gunXRot),0,xViaAiming,0,xViaAimingAiming);
    float yRot = fSign(_gunYRot)*InterpolativC(fabs(_gunYRot),0,yViaAiming,0,yViaAimingAiming);
    // the rest needs to be done using the whole body
    
    vs->_gunTrans= (
      Matrix4(MTranslation,gunAxis)*
      Matrix4(MRotationY,yRot)*
      Matrix4(MRotationX,-xRot)*
      Matrix4(MTranslation,Vector3(0,0,-_gunZRecoil)-gunAxis)
    );
    
    
    Matrix3 totalGunTrans = Matrix3(MRotationY,_gunYRot)*Matrix3(MRotationX,-_gunXRot);

    // _gunBodyTrans.Orientation() * _gunTrans.Orientation() == totalGunTrans
    // _gunBodyTrans.Orientation() == totalGunTrans * _gunTrans.Orientation().InverseRotation()
    Matrix3 leftForBody = totalGunTrans * vs->_gunTrans.Orientation().InverseRotation();
    
    Vector3 leaningAxis=GetLeaningCenterMoves(vs);
    vs->_gunBodyTrans= (
      Matrix4(MTranslation,leaningAxis)*
      Matrix4FromMatrix3(leftForBody)*
      Matrix4(MTranslation,-leaningAxis)
    );
    moveHead = true;
    vs->_gunTransIdent = false;
  }
  else
  {
    vs->_gunTrans = MIdentity;
    vs->_gunBodyTrans = MIdentity; 
    vs->_gunTransIdent = true; // no aim - avoid matrix multiply
  }

  // note: rotation must be around head up direction axis
  Vector3 headAxis = GetHeadCenterMoves(vs);
  Matrix4 headOrient(MTranslation,headAxis);
  Matrix4 headOrientInv(MTranslation,-headAxis);

  if (moveHead || fabs(_headYRot-headYRotDone)+fabs(_headXRot-headXRotDone)>1e-6f)
  {
    vs->_headTrans=(
      headOrient*
      Matrix4(MRotationY,_headYRot-headYRotDone)*
      Matrix4(MRotationX,-(_headXRot-headXRotDone))*
      Matrix4FromMatrix3(vs->_gunBodyTrans.Orientation().InverseRotation())*
      headOrientInv
    );
    vs->_headTransIdent = false;
  }
  else
  {
    vs->_headTrans = MIdentity;
    vs->_headTransIdent = true;
  }
  if (moveHead || fabs(_lookYRot-headYRotDone)+fabs(_lookXRot-headXRotDone)>1e-6f)
  {
    vs->_lookTrans=(
      headOrient*
      Matrix4(MRotationY,_lookYRot-headYRotDone)*
      Matrix4(MRotationX,-(_lookXRot-headXRotDone))*
      Matrix4FromMatrix3(vs->_gunBodyTrans.Orientation().InverseRotation())*
      headOrientInv
    );
  }
  else
  {
    vs->_lookTrans = MIdentity;
  }
}

void Man::RecalcAffectSkeletonTransform()
{
  ProtectedVisualState<VisualState> vs = FutureVisualStateScope();
  
  const float leanRotAngle = 0.6f * 0.75f;
  const float leanShift = 0.1f * 0.75f;
  
  float leaningAngle = -_hitSkeletonRot * leanRotAngle;
  float leaningShift = _hitSkeletonRot * leanShift;

  if (fabs(leaningAngle) > 1e-6f || fabs(leaningShift) > 1e-6f)
  {
    ///rotacni matice kolem osy v xz
    Matrix4 roxz(MRotationAxis, _hitSkeletonRotAxis, leaningAngle);

    // Leaning
    if(!_headHitOnly)
    {
      Vector3 leaningAxis = GetLeaningCenterMoves(vs);
      vs->_leaningTrans = (
        Matrix4(MTranslation,leaningAxis)*
        roxz *
        Matrix4(MTranslation, Vector3(leaningShift, 0, 0))*
        Matrix4(MTranslation,-leaningAxis)
      );
      vs->_leaningTransIdent = false;

      //prepare head move in reverse direction
      roxz = roxz.InverseRotation();
    }

    Vector3 headAxis = GetHeadCenterMoves(vs);
    Matrix4 headOrient(MTranslation,headAxis);
    Matrix4 headOrientInv(MTranslation,-headAxis);

    vs->_headTrans=(
        headOrient*
        roxz*
        headOrientInv
      );
    vs->_headTransIdent = false;
  }
  else
  {
    vs->_leaningTrans = MIdentity;
    vs->_leaningTransIdent = true;

    vs->_headTrans = MIdentity;
    vs->_headTransIdent = true;
  }
  return;
}

void Man::RecalcLeaningTransform()
{
  ProtectedVisualState<VisualState> vs = FutureVisualStateScope();
  const ManType *type = Type();

  float leanRotAngle;
  float leanShift;
  ActionMap *map = type->GetActionMap(_moves.GetPrimaryMove(vs->_moves).id, AnimSubIx());
  ActionMap *leanMap = map;
  if (map)
  {
    if (_leanZRot >= 0)
    {
      leanRotAngle = leanMap->GetLeanRRot();
      leanShift = leanMap->GetLeanRShift();
    }
    else
    {
      leanRotAngle = leanMap->GetLeanLRot();
      leanShift = leanMap->GetLeanLShift();
    }

    // Some fast and strafe action maps omit lean offsets. Reuse the stop
    // animation map from the same movement family while the player runs.
    if (fabs(leanRotAngle) < 1e-6f && fabs(leanShift) < 1e-6f)
    {
      const MoveIdExt &stopMove = type->GetMove(map, ManActStop);
      if (stopMove.id != MoveIdNone)
      {
        ActionMap *stopMap = type->GetActionMap(stopMove.id, AnimSubIx());
        if (stopMap && stopMap != map)
        {
          leanMap = stopMap;
          if (_leanZRot >= 0)
          {
            leanRotAngle = leanMap->GetLeanRRot();
            leanShift = leanMap->GetLeanRShift();
          }
          else
          {
            leanRotAngle = leanMap->GetLeanLRot();
            leanShift = leanMap->GetLeanLShift();
          }
        }
      }
    }
  }
  else
  {
    leanRotAngle = 0;
    leanShift = 0;
  }

  float leaningFactor = ValueTest(vs,&MoveInfoMan::LeaningFactor);
  // Fast locomotion clips may set their leaning factor to zero to suppress
  // the overlay. If the player is actively holding Q/E, preserve the lean
  // request; the movement animation still supplies its normal limb motion.
  if (fabs(_leanZRot) > 0.01f && leaningFactor < 1e-4f)
    leaningFactor = 1.0f;
  float leaningAngle = -_leanZRot * leanRotAngle * leaningFactor;
  float leaningShift = _leanZRot * leanShift * leaningFactor;
  if (fabs(leaningAngle) > 1e-6f || fabs(leaningShift) > 1e-6f)
  {
    // Leaning
    Vector3 leaningAxis=GetLeaningCenterMoves(vs);
    vs->_leaningTrans = (
      Matrix4(MTranslation,leaningAxis)*
      Matrix4(MRotationZ, leaningAngle)*
      Matrix4(MTranslation, Vector3(leaningShift, 0, 0))*
      Matrix4(MTranslation,-leaningAxis)
    );
    vs->_leaningTransIdent = false;
  }
  else
  {
    vs->_leaningTrans = MIdentity;
    vs->_leaningTransIdent = true;
  }
}

void Man::RecalcShakeTransform(Vector3Par v,float deltaT)
{
  if (!(deltaT > 0.0f)) return;
  float x= -0.01f*v.X()*deltaT;
  float y= -0.002f*v.Y()*deltaT;
  float z= -0.01f*v.Z()*deltaT;

  static float ShakeForceLimit = 0.05f;
  saturate(x,-ShakeForceLimit,ShakeForceLimit);
  saturate(y,-ShakeForceLimit,ShakeForceLimit);
  saturate(z,-ShakeForceLimit,ShakeForceLimit);

  Vector3 force = Vector3(x,y,z);

  // degrade old values exponentially, add new ones
  _sumaShakingVector = _sumaShakingVector*floatMax(1-deltaT*2,0)+force;

  static float ShakeMoveLimit = 0.15f;
  saturate(_sumaShakingVector[0],-ShakeMoveLimit,ShakeMoveLimit);
  saturate(_sumaShakingVector[1],-ShakeMoveLimit,ShakeMoveLimit);
  saturate(_sumaShakingVector[2],-ShakeMoveLimit,ShakeMoveLimit);
  
  FutureVisualState()._shakingTrans = Matrix4(MTranslation, _sumaShakingVector);
  FutureVisualState()._shakingTransIdent = _sumaShakingVector.SquareSize()<1e-9f;

}

bool Man::VerifyStructure() const
{
  if (FutureVisualState().DirectionAside().SquareSize()<0.01f) {Fail("DirectionAside zero");return false;}
  if (FutureVisualState().DirectionUp().SquareSize()<0.01f) {Fail("DirectionUp zero");return false;}
  if (FutureVisualState().Direction().SquareSize()<0.01f) {Fail("Direction zero");return false;}

  // check for indefinite numbers
  if (!FutureVisualState().DirectionAside().IsFinite()) {Fail("DirectionAside inf");return false;}
  if (!FutureVisualState().DirectionUp().IsFinite()) {Fail("DirectionUp inf");return false;}
  if (!FutureVisualState().Direction().IsFinite()) {Fail("Direction inf");return false;}
  if (!FutureVisualState().Position().IsFinite()) {Fail("Position inf");return false;}

  Matrix4Val it = FutureVisualState().GetInvTransform();

  if (!it.DirectionAside().IsFinite()) {Fail("inv DirectionAside inf");return false;}
  if (!it.DirectionUp().IsFinite()) {Fail("inv DirectionUp inf");return false;}
  if (!it.Direction().IsFinite()) {Fail("inv Direction inf");return false;}
  if (!it.Position().IsFinite()) {Fail("inv Position inf");return false;}

  return true;
}

/**
head movement is used for crew turning head while in vehicle
*/
bool Man::MoveHeadCrew(float deltaT)
{
  if (_brain && _brain->IsAnyPlayer()) return MoveHead(deltaT,1.0f);
  // periodically look forward/to the watch direction
  // TODO: if we are driver, we need to look forward more
  if (_lookTargetTimeLeft>0)
  {
    _lookTargetTimeLeft -= deltaT;
  }
  else if (_lookForwardTimeLeft>0)
  {
    _headXRotWanted = _headYRotWanted = 0;
    _lookXRotWanted = _lookYRotWanted = 0;
    _lookForwardTimeLeft -= deltaT;
  }
  else
  {
    _headXRotWanted = _headYRotWanted = 0;
    _lookXRotWanted = _lookYRotWanted = 0;
    // start the cycle anew -
    AIBrain *unit = Brain();
    // if we are not moving, we can look around a lot
    if (FutureVisualState().Speed().SquareSize()<Square(1))
    {
      _lookTargetTimeLeft = 1+GRandGen.RandomValue()*4;
      _lookForwardTimeLeft = 2+GRandGen.RandomValue()*3;
    }
    else if (unit && unit->GetCombatModeLowLevel()<=CMAware)
    {
      // safe - look around from time to time
      _lookTargetTimeLeft = 1+GRandGen.RandomValue()*2;
      _lookForwardTimeLeft = 10+GRandGen.RandomValue()*5;
    }
    else
    {
      // when in combat, we are extra cautious and look for targets more often
      _lookTargetTimeLeft = 1+GRandGen.RandomValue()*2;
      _lookForwardTimeLeft = 5+GRandGen.RandomValue()*5;
    }
  }
    
  return MoveHead(deltaT,0.33f);
}


/*!
\patch 1.52 Date 4/20/2002 by Ondra
- Fixed: Soldiers sometimes turned their head around completely.

\patch 5164 Date 7/9/2007 by Ondra
- Fixed: Recoil was working bad (moving in uneexpected directions) when fps was low.
*/
void Man::MoveWeapons( float deltaT0, bool forceRecalcMatrix )
{
  float f = GetLimitGunMovement(FutureVisualState());
  float ability = GetAbility(AKAimingSpeed);

  // scan for the maximum recoil
  float maxMag = 0;
  
  if (f<0.01f)
  {
    // no gun movement - reset all gun properties
    _gunXSpeed = 0;
    _gunYSpeed = 0;
    _gunXRecoil = 0;
    _gunYRecoil = 0;

    if (fabs(_gunXRot)>1e-5) forceRecalcMatrix = true;
    if (fabs(_gunYRot)>1e-5) forceRecalcMatrix = true;

    _gunXRot = 0;
    _gunYRot = 0;
    _gunXRotDelay = 0;
    _gunYRotDelay = 0;
  }
  else
  {           

    static float maxStepRecoil = 1.0f/30; // min. 30 fps for good accuracy
    static float maxStepNoRecoil = 1.0f/10; // min. 10 fps gives enough accuracy with no recoil
    float maxStep = _recoil ? maxStepRecoil : maxStepNoRecoil;
    float deltaTT = deltaT0;
    while (deltaTT>0)
    {
      float deltaT;
      if (deltaTT>maxStep)
      {
        deltaT = maxStep;
        deltaTT -= maxStep;
      }
      else
      {
        deltaT = deltaTT;
        deltaTT = 0;
      }

      float gunXRotWanted=_gunXRotWanted;
      float gunYRotWanted=_gunYRotWanted;

      Limit(gunXRotWanted,Type()->_minGunElev*f,Type()->_maxGunElev*f);
      Limit(gunYRotWanted,Type()->_minGunTurnAI*f,Type()->_maxGunTurnAI*f);

      if( gunXRotWanted != _gunXRot || gunYRotWanted != _gunYRot || _recoil || fabs(_gunXSpeed)+fabs(_gunYSpeed)!= 0 )
      {
        float weaponDexterity = 5;

        WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
        if (ptr.IsValid())
        {
          weaponDexterity = ptr.SumDexterityModifiers(this);
        }

        // limited acceleration model
        float delta;
        float speed;
        float deltaPos;

        const float maxV = 5*ability*weaponDexterity;
        const float maxA = 20*weaponDexterity;

        deltaPos = gunXRotWanted-_gunXRot;
        speed=deltaPos*8*weaponDexterity;
        if (fabs(speed)*deltaT>fabs(deltaPos))
        {
          speed = deltaPos/deltaT;
        }
        delta=speed-_gunXSpeed;
        Limit(delta,-maxA*deltaT,+maxA*deltaT);
        _gunXSpeed+=delta;

        deltaPos = gunYRotWanted - _gunYRot;
        speed=deltaPos*6*weaponDexterity;
        if (fabs(speed)*deltaT>fabs(deltaPos))
        {
          speed = deltaPos/deltaT;
        }
        delta=speed-_gunYSpeed;
        Limit(delta,-maxA*deltaT,+maxA*deltaT);
        _gunYSpeed+=delta;

        Limit(_gunXSpeed,-maxV,+maxV);
        Limit(_gunYSpeed,-maxV,+maxV);

        float gunXRotChangeRecoil = 0;
        float gunYRotChangeRecoil = 0;

        // the recoil function may be quite wild - make sure the integration is accurate
        const float maxRecoilDeltaT = 0.02f;
        float recoilT = deltaT;
        while (_recoil && recoilT>0)
        { // recoil simulation
          // check if we should pass to another item
          // apply the item we have just passed
          float dt = floatMin(maxRecoilDeltaT,recoilT);

          float impulseAngX,impulseAngY,impulseZ;
          _recoil->GetRecoil(_recoilIndex,_recoilTime,impulseAngX,impulseAngY,impulseZ);

          // for LIB44 
          if (_brain && _brain->IsPlayer())
          {
            float skillCoef =  Glob.config.GetRecoilCoef() * _customRecoilCoefficient;
            impulseAngX *= skillCoef;
            impulseAngY *= skillCoef;
            impulseZ *= skillCoef;
          }

          ApplyRecoil(impulseAngX*_recoilRandomX,impulseAngY*_recoilRandomY,impulseZ*_recoilRandomZ);

          // update the maximum
          RecoilFFRamp ramp;
          if (_recoil->GetFFRamp(_recoilTime,ramp))
          {
            maxMag = floatMax(maxMag,ramp._begAmplitude,ramp._endAmplitude);
          }

          gunXRotChangeRecoil += _gunXRecoil*deltaT;
          gunYRotChangeRecoil += _gunYRecoil*deltaT;

          _recoilTime += dt;
          if (_recoil->CheckItemDone(_recoilIndex,_recoilTime))
          {
            _recoilIndex++;
          }

          if (_recoil->GetTerminated(_recoilIndex,_recoilTime))
          {
            _recoil.Free();
            _gunXRecoil = _gunYRecoil = _gunZRecoil = 0;
          }

          recoilT -= dt;
        }

        float gunXRotChange = gunXRotChangeRecoil + _gunXSpeed*deltaT;
        float gunYRotChange = gunYRotChangeRecoil + _gunYSpeed*deltaT;
        if (fabs(gunXRotChange)>=1e-7f) forceRecalcMatrix = true;
        else gunXRotChange = 0;
        if (fabs(gunYRotChange)>=1e-7f) forceRecalcMatrix = true;
        else gunYRotChange = 0;

        const float dynamicFactorX = (15 - 2 * weaponDexterity) / 5;
        const float dynamicFactorY = (5 - weaponDexterity) / 5;
        const float dampingFactorX = 10 + 10 * weaponDexterity;
        const float dampingFactorY = 2 * weaponDexterity;
        const float minDampingSpeedX = (H_PI / 32)*dampingFactorX;
        const float minDampingSpeedY = (H_PI / 32)*dampingFactorY;

        float gunRotXNew = ComputeWeaponDynamics(_gunXRotWanted, deltaT, _gunXRot, _gunXRotDelay, dynamicFactorX, dampingFactorX, minDampingSpeedX);
        float gunRotYNew = ComputeWeaponDynamics(_gunYRotWanted, deltaT, _gunYRot, _gunYRotDelay, dynamicFactorY, dampingFactorY, minDampingSpeedY);

        _gunXRot = gunRotXNew + gunXRotChangeRecoil;
        _gunYRot = gunRotYNew + gunYRotChangeRecoil;

        // caution: when there is no recoil, avoid applying any limits - this would cause aiming without weapon impossible
        // see news:i11e8q$845$2@new-server.localdomain
        if (QIsManual() && fabs(gunXRotChangeRecoil)+fabs(gunYRotChangeRecoil)>0)
        {
          static float misalignFactor = 0.25f;
          _gunXRotWantedNoTrembleHands += gunXRotChangeRecoil*misalignFactor;
          _gunYRotWantedNoTrembleHands += gunYRotChangeRecoil*misalignFactor;

          Limit(_gunXRotWantedNoTrembleHands,Type()->_minGunElev*f,Type()->_maxGunElev*f);
          Limit(_gunYRotWantedNoTrembleHands,Type()->_minGunTurnAI*f,Type()->_maxGunTurnAI*f);
        }
      }

    }

    Limit(_gunXRot,Type()->_minGunElev*f,Type()->_maxGunElev*f);
    Limit(_gunYRot,Type()->_minGunTurnAI*f,Type()->_maxGunTurnAI*f);

    if (fabs(_gunXRot)>0 && QIsManual()) // only to prevent visual artifacts, no need to do for AI
    {
      static float zOffset = 0.230f;
      static float xOffset = 0.200f;
      static float zOffset2 = 0.250f;

      _gunXRot = CheckMaxLean(_gunXRot,xOffset, zOffset,true);
      _gunXRot = CheckMaxLean(_gunXRot,0, zOffset2,true);
      _gunXRot = CheckMaxLean(_gunXRot,-xOffset, zOffset,true);
    }
    RecalcGunTransform();
  }

#ifdef _WIN32
  if (this==GWorld->CameraOn())
  {
    // force feedback simulation is necessary
    float ffCoef = 10;
    float mag = maxMag>0 ? maxMag * _recoilFactor*ffCoef : 0;
    if (GJoystickDevices) GJoystickDevices->PlayRamp(mag,mag,deltaT0);
    if (GInput.IsXInputPresent()) XInputDev->PlayRamp(mag,mag,deltaT0);
  }
#endif
  if (_recoil && _recoil->GetTerminated(_recoilIndex,_recoilTime))
  {
    _recoil.Free();
    _gunXRecoil = _gunYRecoil = _gunZRecoil = 0;
    ApplyRecoil(0,0,0);
  }
  
  bool headMoved = MoveHead(deltaT0,1);
  if (headMoved) forceRecalcMatrix = true;

  //static StatEventRatio eventGun("RecalcGun");
  //eventGun.Count(forceRecalcMatrix);
  if (forceRecalcMatrix)
  {
    RecalcGunTransform();
  }
}

float Man::ComputeWeaponDynamics(float origin, float deltaT, float actValue, float moveDelta, float dynamicFactor, float dampingFactor, float minDampingSpeed)
{
  float res = actValue + dynamicFactor * moveDelta;
  float dampingSpeed = (res - origin)*dampingFactor;
  if( fabs(dampingSpeed) < minDampingSpeed )
    dampingSpeed = sign(dampingSpeed)*minDampingSpeed;

  float damping = dampingSpeed*deltaT;
  if( res < origin && (damping + origin) < res || res > origin && (damping + origin) > res || fabs(res - origin) <= 1e-6f )
    res = origin;
  else
    res -= damping;

  return res;
}

bool Man::MoveHead(float deltaT, float speedFactor)
{
  bool forceRecalcMatrix = false;

  float lookXRotWanted = _lookXRotWanted + _headXRotWantedCont;
  float lookYRotWanted = _lookYRotWanted + _headYRotWantedCont + _headYRotWantedPan;
  if( _isDead ) 
    lookXRotWanted = lookYRotWanted = 0;

  float lookXRotDelta = lookXRotWanted - _lookXRot;
  if( fabs(lookXRotDelta)>1e-6 ) 
    forceRecalcMatrix = true;

  float lookYRotDelta = lookYRotWanted - _lookYRot;
  if( fabs(lookYRotDelta)>1e-6 )
    forceRecalcMatrix = true;

  float f1 = 1, f2 = 1;
  if( _headSmoothMove )
  {
    f1 = ((4*H_PI) * deltaT) / fabs(lookXRotDelta);
    f2 = ((4*H_PI) * deltaT) / fabs(lookYRotDelta);
    Limit(f1, 0, 1);
    Limit(f2, 0, 1);
    if( f1 >= 1 && f2 >= 1 )
      _headSmoothMove = false;
  }
  
  _lookXRot = Lerp(_lookXRot, lookXRotWanted, f1);
  _lookYRot = Lerp(_lookYRot, lookYRotWanted, f2);
  _headXRot = _lookXRot; _headYRot = _lookYRot;
  LimitLookRot(_lookXRot, _lookYRot);
  LimitHeadRot(_headXRot, _headYRot);

  return forceRecalcMatrix;
}

void Man::ProcessLookAround(float headingWanted, float diveWanted, const ViewPars *viewPars, float deltaT, bool forceLookAround, bool forceDiveAround)
{
  if (_brain->LookAroundEnabled() || forceLookAround)
  {
    _lookYRotWanted += headingWanted;
    _lookXRotWanted -= diveWanted;
  }
  else if (forceDiveAround) _lookXRotWanted -= diveWanted;
  LimitLookRot(_lookXRotWanted, _lookYRotWanted);
  float dummyfov;
  viewPars->LimitVirtual(GWorld->GetCameraType(),_lookYRotWanted,_lookXRotWanted,dummyfov);
  _lookAroundViewPars = viewPars;
  //update head too
  _headXRotWanted = _lookXRotWanted;
  _headYRotWanted = _lookYRotWanted;
  LimitHeadRot(_headXRotWanted, _headYRotWanted);
}

inline bool IsDown(int pos)
{
  return pos==ManPosLying || pos==ManPosBinocLying ||
    pos==ManPosLyingNoWeapon || pos==ManPosHandGunLying;
}
inline bool IsCrouch(int pos)
{
  return pos==ManPosCrouch || pos==ManPosHandGunCrouch;
}
inline bool IsLaunchDown(int pos)
{
  return pos==ManPosWeapon;
}

bool Man::IsDown(const VisualState &vs) const
{
  return ::IsDown(GetActUpDegree(vs));
}

inline bool Man::IsCrouch(const VisualState &vs) const
{
  return ::IsCrouch(GetActUpDegree(vs));
}

inline bool Man::IsDead(const VisualState &vs) const
{
  return GetActUpDegree(vs)==ManPosDead;
}


inline bool Man::IsLaunchDown() const
{
  return ::IsLaunchDown(GetActUpDegree(FutureVisualState()));
}

float Man::CorpseAge()
{
  if(!_isDead)
    return 0;

  if(_corpseTime == Time(0))
  {
    _corpseTime = Glob.time;
    return 0;
  }

  return Glob.time - _corpseTime;
}

bool Man::EnableTest(const VisualState &vs, TestEnable func) const
{
  // FIX: return the value by the most appropriate move
  if (_moves.GetPrimaryFactor(vs._moves) < 0.5f)
  {
    // secondary move
    const MoveInfoMan *info = Type()->GetMoveInfo(_moves.GetSecondaryMove(vs._moves).id, AnimSubIx());
    return !info || (info->*func)();
  }
  else
  {
    // primary move
    const MoveInfoMan *info = Type()->GetMoveInfo(_moves.GetPrimaryMove(vs._moves).id, AnimSubIx());
    return !info || (info->*func)();
  }
}

bool Man::EnableTestMovesCommon( TestGEnable func ) const
{
  // FIX: return the value by the most appropriate move
  if (_moves.GetPrimaryFactor(FutureVisualState()._moves) < 0.5f)
  {
    // secondary move
    const MoveInfoManCommon *info = Type()->GetMoveInfo(_moves.GetSecondaryMove(FutureVisualState()._moves).id, AnimSubIx());
    return !info || (info->*func)();
  }
  else
  {
    // primary move
    const MoveInfoManCommon *info = Type()->GetMoveInfo(_moves.GetPrimaryMove(FutureVisualState()._moves).id, AnimSubIx());
    return !info || (info->*func)();
  }
}

bool Man::EnableTestGesturesCommon( TestGEnable func ) const
{
  Assert(FutureVisualState()._gestureFactor>0 && Type()->_gestureType);
  // FIX: return the value by the most appropriate move
  if (_gesture.GetPrimaryFactor(FutureVisualState()._gestures) < 0.5f)
  {
    // secondary move
    const MoveInfoManCommon *info = Type()->GetMoveInfoGesture(_gesture.GetSecondaryMove(FutureVisualState()._gestures).id);
    return !info || (info->*func)();
  }
  else
  {
    // primary move
    const MoveInfoManCommon *info = Type()->GetMoveInfoGesture(_gesture.GetPrimaryMove(FutureVisualState()._gestures).id);
    return !info || (info->*func)();
  }
}

bool Man::EnableTestWithGestureOr(TestGEnable func) const
{
  bool ret = EnableTestMovesCommon(func);
  if (ret) return ret;
  if (FutureVisualState()._gestureFactor<=0 || !Type()->_gestureType) return ret;
  ret = EnableTestGesturesCommon(func);
  return ret;
}

bool Man::EnableTestWithGestureAnd(TestGEnable func) const
{
  bool ret = EnableTestMovesCommon(func);
  if (!ret) return ret;
  if (FutureVisualState()._gestureFactor<=0 || !Type()->_gestureType) return ret;
  ret = EnableTestGesturesCommon(func);
  return ret;
}

struct CombineAnd
{
  int operator () (int primValue, int secValue, int primFactor) const
  {
    return primValue & secValue;
  }
};

bool Man::EnableOptics(ObjectVisualState const& vs) const
{
  // find the current weapon
  WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
  if (!ptr.IsValid()) return false;
  
  const WeaponType *type = ptr.GetWeaponType();
  if (!type) return false;

  // find for what weapons is the optics enabled
  int mask = ValueTestEx(vs.cast<Man>(), &MoveInfoMan::EnableOptics, 0xffffffff, CombineAnd());
  if (FutureVisualState()._gestureFactor > 0 && Type()->_gestureType) mask |= ValueTestGesturesEx(&MoveInfoMan::EnableOptics, 0xffffffff, CombineAnd());

  // combine it
  return (type->_weaponType & mask) != 0;
}


struct CombineLerp
{
  float operator () (float primValue, float secValue, float primFactor) const
  {
    return Lerp(primValue,secValue,1-primFactor);
  }
};

float Man::ValueTest(const ManVisualState &vs, TestValue func, float defValue) const
{
  return ValueTestEx(vs,func,defValue,CombineLerp());
}

float Man::ValueTest(const ManVisualState &vs, TestValueTimed func, float defValue) const
{
  const MoveInfoMan *info=Type()->GetMoveInfo(_moves.GetPrimaryMove(vs._moves).id, AnimSubIx());
  float primValue = info ? (info->*func)(_moves.GetPrimaryTime(vs._moves)) : defValue;
  float factor = _moves.GetPrimaryFactor(vs._moves);
  if (factor < Moves::_primaryFactor1)
  {
    const MoveInfoMan *sec=Type()->GetMoveInfo(_moves.GetSecondaryMove(vs._moves).id, AnimSubIx());
    float secValue = sec ? (sec->*func)(_moves.GetSecondaryTime(vs._moves)) : defValue;
    primValue = factor*primValue + (1-factor)*secValue;
  }
  return primValue;
}

float Man::ValueTest(const ManVisualState &vs, TestVar var, float defValue) const
{
  const MoveInfoMan *info=Type()->GetMoveInfo(_moves.GetPrimaryMove(vs._moves).id, AnimSubIx());
  float primValue = info ? info->*var : defValue;
  float factor = _moves.GetPrimaryFactor(vs._moves);
  if (factor < Moves::_primaryFactor1)
  {
    const MoveInfoMan *sec=Type()->GetMoveInfo(_moves.GetSecondaryMove(vs._moves).id, AnimSubIx());
    float secValue = sec ? sec->*var : defValue;
    primValue = factor*primValue + (1-factor)*secValue;
  }
  return primValue;
}

bool Man::DisableWeapons() const
{
  return WeaponsDisabled();
}

bool Man::EnableMissile(VisualState const& vs) const
{
  const MoveInfoMan *info=Type()->GetMoveInfo(_moves.GetPrimaryMove(vs._moves).id, AnimSubIx());
  if (info && !info->MissileEnabled()) return false;
  if (_moves.GetPrimaryFactor(vs._moves) < Moves::_primaryFactor1)
  {
    const MoveInfoMan *sec=Type()->GetMoveInfo(_moves.GetSecondaryMove(vs._moves).id, AnimSubIx());
    if (sec && !sec->MissileEnabled()) return false;
  }
  return true;
}

const BlendAnimSelections &Man::GetBlendAnim(const ManVisualState &vs, BlendAnimSelections &tgt, BlendAnimFunc func) const
{
  const ManType *type = Type();
  // blend aiming to tgt buffer
  // check primary and secondary factors
  if (_moves.GetPrimaryFactor(vs._moves) >= Moves::_primaryFactor1)
  {
    const MoveInfoMan *move = type->GetMoveInfo(_moves.GetPrimaryMove(vs._moves).id, AnimSubIx());
    if (move) return (move->*func)();
    return tgt;
  }

  const BlendAnimSelections *anim1 = NULL;
  const BlendAnimSelections *anim2 = NULL;

  const MoveInfoMan *move1 = type->GetMoveInfo(_moves.GetPrimaryMove(vs._moves).id, AnimSubIx());
  //if (move) tgt.AddOther((move->*func)(),_primaryFactor);
  const MoveInfoMan *move2 = type->GetMoveInfo(_moves.GetSecondaryMove(vs._moves).id, AnimSubIx());
  //if (move) tgt.AddOther((move->*func)(),1-_primaryFactor);
  if (move1) anim1 = &(move1->*func)();
  if (move2) anim2 = &(move2->*func)();
  if (anim1 && anim1==anim2)
  {
    // if both are the same, blending is trivial
    return *anim1;
  }
  if (anim1)
    tgt.AddOther(*anim1,_moves.GetPrimaryFactor(vs._moves));
  if (anim2)
    tgt.AddOther(*anim2,1-_moves.GetPrimaryFactor(vs._moves));
  return tgt;
}

const BlendAnimSelections *Man::GetGestureMask(const ManVisualState &vs, BlendAnimSelections &tgt) const
{
  if (!GetGesturesType()) return NULL;
  const ManType *type = Type();
  // blend aiming to tgt buffer
  // check primary and secondary factors
  if (_gesture.GetPrimaryFactor(vs._gestures) >= Moves::_primaryFactor1)
  {
    const MoveInfoGestureMan *move = type->GetMoveInfoGesture(_gesture.GetPrimaryMove(vs._gestures).id);
    if (move) return &move->GetMask();
    return &tgt;
  }

  const BlendAnimSelections *anim1 = NULL;
  const BlendAnimSelections *anim2 = NULL;

  const MoveInfoGestureMan *move1 = type->GetMoveInfoGesture(_gesture.GetPrimaryMove(vs._gestures).id);
  const MoveInfoGestureMan *move2 = type->GetMoveInfoGesture(_gesture.GetSecondaryMove(vs._gestures).id);
  if (move1) anim1 = &move1->GetMask();
  if (move2) anim2 = &move2->GetMask();
  if (anim1 && anim1==anim2)
  {
    // if both are the same, blending is trivial
    return anim1;
  }
  if (anim1)
    tgt.AddOther(*anim1,_gesture.GetPrimaryFactor(vs._gestures));
  if (anim2)
    tgt.AddOther(*anim2,1-_gesture.GetPrimaryFactor(vs._gestures));
  return &tgt;
}

const BlendAnimSelections &Man::GetAiming( const ManVisualState &vs, BlendAnimSelections &tgt ) const
{
  return GetBlendAnim(vs,tgt,&MoveInfoMan::GetAiming);
}
const BlendAnimSelections &Man::GetLeaning( const ManVisualState &vs, BlendAnimSelections &tgt ) const
{
  return GetBlendAnim(vs,tgt,&MoveInfoMan::GetLeaning);
}
const BlendAnimSelections &Man::GetAimingBody( const ManVisualState &vs, BlendAnimSelections &tgt ) const
{
  return GetBlendAnim(vs,tgt,&MoveInfoMan::GetAimingBody);
}
const BlendAnimSelections &Man::GetLegs( const ManVisualState &vs, BlendAnimSelections &tgt ) const
{
  return GetBlendAnim(vs,tgt,&MoveInfoMan::GetLegs);
}
const BlendAnimSelections &Man::GetHead( const ManVisualState &vs, BlendAnimSelections &tgt ) const
{
  return GetBlendAnim(vs,tgt,&MoveInfoMan::GetHead);
}

float Man::VisibleMovementNoSize() const
{
  return (_shootVisible > 1.0f) ? _shootVisible : base::VisibleMovementNoSize();
}

float Man::VisibleMovement() const
{
  if( _shootVisible>1 ) return _shootVisible;
  float vis=base::VisibleMovement();

  vis *= GetVisibleSize(FutureVisualState());
  
  float bodyNotHidden = floatMax(1-_hideBody,0);
  vis *= bodyNotHidden;

  return vis;
}

/*!
  \patch 1.01 Date 06/11/2001 by Ondra
  - Fixed: too low audibility of near soldiers
  - AI would not hear soldier even when moving very near.
*/

float Man::Audible() const
{
  // modified EntityAI::Audible
  // when not moving, man is really very quiet
  float vis=0.05f;
  // screaming target is better ::AIPilot
  // currently it is 30 sec after scream
  if (_whenScreamed<Glob.time && _whenScreamed>Glob.time-30)
  {
    saturateMax(vis,1.2f);
  }

  // FIX
  float rSpeed=FutureVisualState().Speed().SizeXZ()*0.5f;
  // moving target is easier to hear
  saturateMin(rSpeed,4);
  saturateMax(vis,rSpeed);
  // TODO: if light is on at night, vehicle is much more visible
  //if( IsDown() ) vis*=0.25f;
  vis *= GetType()->GetAudible();
  saturateMax(vis,_shootAudible);
  return vis;
}

float Man::GetHidden(float dist) const
{
  // track hiding in trees/bushes etc.
  return 1-_surround.Track(this,FutureVisualState().Position(),dist);
  //return 1;
}

float Man::GetLit() const
{
  return _lit.Track(this,FutureVisualState().Position());
}

float Man::VisibleSize(ObjectVisualState const& vs) const
{
  float bodyNotHidden = floatMax(1-_hideBody,0);
  float vis=GetShape()->GeometrySphere()*bodyNotHidden;
  //if( IsDown() ) return size*0.3f;

  vis *= GetVisibleSize(vs.cast<Man>());

  return vis;
}

float Man::AimingSize() const
{
  // when aiming at a soldier, we need quite a good accuracy
  // aiming point is somewhere in upper part of the torso, and error of 30 cm may mean we miss
  
  // we want to avoid using absolute value here, to make sure it works for non-humans implemented via the class
  // (used for animals now, might be used for dwarfs or giants by some mod or derived product)
  // for human in ArmA/ArmA 2 GeometrySpheres returns cca 1.09 m
  return GetShape()->GeometrySphere()*(0.25f/1.09f);
}

float Man::CollisionSize() const
{
  if( IsDown() ) return GetShape()->GeometrySphere();
  return base::CollisionSize();
}

Vector3 Man::VisiblePosition(ObjectVisualState const& vs) const
{
  return AimingPosition(vs);
}

Vector3 Man::CalculateAimingPosition(Matrix4Par pos) const
{
  if( Type()->_aimPoint>=0 )
  {
    int level=_shape->FindMemoryLevel();
    return pos.FastTransform(AnimatePoint(FutureVisualState(),level,Type()->_aimPoint));
  }
  else
  {
    LogF("Computing aiming position with no aimPoint - %s,%s",cc_cast(GetShape()->GetName()),cc_cast(Type()->GetName()));
    //  if laid down, position is different
    // TODO: correct calculation of position
    Vector3 basePos=base::AimingPosition(FutureVisualState());
    if( IsDown() ) return basePos-Vector3(0,1.25f,0);
    return basePos;
  }
}

Vector3 Man::CalculateCameraPosition(Matrix4Par pos) const
{
  int index = Type()->_pilotPoint;
  if (index < 0)
  {
    // fall-back - no explicit camera point
    return CalculateAimingPosition(pos);
  }
  int level = _shape->FindMemoryLevel();
  return pos.FastTransform(AnimatePoint(FutureVisualState(), level, index));
}

Vector3 Man::CalculateHeadPosition(Matrix4Par pos)const
{
  int index = Type()->_headAxisPoint;
  if (index < 0)
  {
    // fall-back - no explicit head point
    return CalculateAimingPosition(pos);
  }
  int level = _shape->FindMemoryLevel();
  return pos.FastTransform(AnimatePoint(FutureVisualState(),level, index));
}

Vector3 Man::GetLocalAimingPosition() const
{
  if( Type()->_aimPoint>=0 )
  {
    int level=_shape->FindMemoryLevel();
    return AnimatePoint(FutureVisualState(),level,Type()->_aimPoint);
  }
  return VZero;
}

Vector3 Man::AimingPosition(ObjectVisualState const& vs, const AmmoType *ammo, Vector3Par dirToAim) const
{
  if (GetHierarchyParent())
  {
    return CalculateAimingPosition(WorldTransform(vs));
  }
  return vs.cast<Man>()._aimingPositionWorld;
}

Vector3 Man::CameraPosition() const
{
  if (GetHierarchyParent())
  {
    return CalculateCameraPosition(WorldTransform(FutureVisualState()));
  }
  return RenderVisualState()._cameraPositionWorld;
}

Vector3 Man::EyePosition(ObjectVisualState const& vs) const
{
  if (GetHierarchyParent())
  {
    return CalculateCameraPosition(WorldTransform(FutureVisualState()));
  }
  return vs.cast<Man>()._cameraPositionWorld;
}

Vector3 Man::AimingPositionHeadShot(ObjectVisualState const& vs, const AmmoType *ammo, Vector3Par dirToAim) const
{
  return vs.cast<Man>()._cameraPositionWorld;
}

float Man::DistanceFrom(const SensorPos &pos) const
{
  // we want to estimate how much changing given property can change the position of the aiming point
  // note: leaning in cover or changing stance there can change a lot
  // we may consider this by increasing its importance in the calculation
  float dist = pos.pos.Distance(FutureVisualState().Position());
  float lean = (_hitSkeletonRot) ? fabs(_hitSkeletonRot-pos.optional[0]) : fabs(_leanZRot-pos.optional[0]);
  float stance = fabs(FutureVisualState()._aimingPositionWorld.Y()-_landContactPos.Y()-pos.optional[1]);
  // note: here we assume leaning 1 means moving about 1 m
  // this means we are giving leaning higher weight than would be appropriate for the movement
  // however, as leaning is mostly done near object edges, we assume leaning really changes object visibility a lot
  return dist + lean + stance;
}

void Man::GetSensorInfo(SensorPos &pos) const
{
  pos.pos = FutureVisualState().Position();
  pos.optional[0] = (_hitSkeletonRot) ? _hitSkeletonRot :_leanZRot;
  // height of aiming position above surface tells a lot about the stance
  pos.optional[1] = FutureVisualState()._aimingPositionWorld.Y()-_landContactPos.Y();
}

float Man::GetIndirectHitFactor() const
{
  // indirect hit depends on unit posture for men (http://dev-heaven.net/issues/20441#note-5)
  int upDegree = GetActUpDegree(FutureVisualState());
  const float proneFactor = 0.3333f;
  const float crouchFactor = 0.66667f;
  if (::IsDown(upDegree))
  {
    return proneFactor;
  }
  switch(upDegree)
  {
    case ManPosSwimming:
      return proneFactor;
    case ManPosCrouch:
    case ManPosHandGunCrouch:
    case ManPosWeapon: //special case for AT
      return crouchFactor;
  }
  return 1.0f;
}

/**
@return check if aiming in given direction is possible, and how difficult it is
*/

/*
\patch Date 6/04/2010 by Filip.
- Fixed: This allows soldier to shoot, if he wants, but cant because his target is on higher ground and too close.
*/
float Man::FireAngleInRange(const TurretContext &context, const WeaponModePtr &ptr, Vector3Par rel) const
{
  const ManType *type=Type();
  float dist2=rel.SquareSizeXZ();
  if (dist2 == 0.0f) return 0;
  float y2=Square(rel.Y());
  // x>0: atan(x)<x
  // atan(y/sqrt(dist2)) < type->_maxGunElev
  // y/sqrt(dist2) < type->_maxGunElev
  // y < type->_maxGunElev * sqrt(dist2)
  // y2 < type->_maxGunElev^2 * dist2
  // we need to have correct signs
  Assert( type->_maxGunElev>=0 );
  Assert( type->_minGunElev<=0 );
  float ret=0;
  if( rel.Y()>=0 )
  {
    // fire up
    if(y2>dist2*Square(tan(type->_maxGunElev))) 
      return 0;
    else
      ret=rel.Y()*InvSqrt(dist2)*(1/tan(type->_maxGunElev));	
  }
  else
  {
    // fire down
    if( y2>dist2*Square(tan(type->_minGunElev)) ) return 0;
    ret=rel.Y()*InvSqrt(dist2)*(1/tan(type->_minGunElev));
  }
  return floatMin(1,2-ret*2);
}

void Man::FireAimingLimits(float &minAngle, float &maxAngle, const TurretContext &context, const WeaponModePtr &ptr) const
{
  const ManType *type=Type();
  minAngle = type->_minGunTurnAI;
  maxAngle = type->_maxGunTurnAI;
}

/*!
\patch 1.05 Date 7/18/2001 by Ondra.
- New: even small hand injury causes some weapon trembling.
- Improved: Increased weapon trembling caused by heavy breath.
*/

float Man::RifleInaccuracy() const
{
  // weapon cursor movement when moving
  // riffle more accurate when prone
  // if man is wounded, riffle is much less accurate
  float woundFactor = GetHitCont(Type()->_handsHit)*10 + 1;
  woundFactor += floatMin(_tired*2,1)*6;
  float speedXZ = FutureVisualState()._speed.SizeXZ();
  float ret = floatMin(speedXZ*0.5f,1)*0.05f+0.005f;
  // get current move
  float moveInacc = ValueTest(FutureVisualState(),&MoveInfoMan::GetAimPrecision);
  
  ret *= moveInacc;
  // some weapons (weapon modes) are more accurate
  //const AmmoType *ammo = GetCurrentAmmoType();
  /*
  if (this==(const Object *)GWorld->CameraOn())
  {
    GlobalShowMessage(100,"Inacc %.3f, %.3f",ret,woundFactor);
  }
  */
  return ret*woundFactor;
}

static void LimitLead( Vector3 &spd, float maxLead )
{
  float size2=spd.SquareSize();
  if( size2>Square(maxLead) )
  {
    spd.Normalize();
    spd*=maxLead;
  }
}


void Man::AimHeadForward()
{
  // head should follow gun
  _headYRotWanted = _headXRotWanted = 0;
  _lookXRotWanted = _lookYRotWanted = 0;
}

void Man::AimHeadCrew(Vector3Par dir)
{
  // TODO: switch between watching forward and in the given direction
  float yRotWanted=-atan2(dir.X(),dir.Z());
  float sizeXZ=sqrt(Square(dir.X())+Square(dir.Z()));
  float xRotWanted=atan2(dir.Y(),sizeXZ);

  // limit look limits
  LimitLookRot(xRotWanted,yRotWanted);
  _lookYRotWanted = yRotWanted;
  _lookXRotWanted = xRotWanted;

  // limit with natural head limits
  LimitHeadRot(xRotWanted,yRotWanted);
  _headYRotWanted = yRotWanted;
  _headXRotWanted = xRotWanted;
}

void Man::GetLookWanted(float &x, float &y)
{
  x= _lookXRotWanted;
  y= _lookYRotWanted;
}

Vector3 Man::GetCrewHeadDirection() const
{
  Vector3 dir = (
    Matrix3(MRotationY,_headYRot)*
    Matrix3(MRotationX,-_headXRot).Direction()
  );
  return dir;
}
Vector3 Man::GetCrewLookDirection(ObjectVisualState const &vs) const
{
  /*
  vs->_lookTrans=(
    headOrient*
    Matrix4(MRotationY,_lookYRot-headYRotDone)*
    Matrix4(MRotationX,-(_lookXRot-headXRotDone))*
    Matrix4FromMatrix3(vs->_gunBodyTrans.Orientation().InverseRotation())*
    headOrientInv
    );
  */
  const VisualState &mvs = vs.cast<Man>();
  Matrix3 dirMat = mvs._lookTrans.Orientation() * mvs._gunBodyTrans.Orientation();
  Vector3 dir = dirMat.Direction();
  return dir;
}
Vector3 Man::GetCrewHeadDirectionWanted() const
{
  Vector3 dir = (
    Matrix3(MRotationY,_headYRotWanted)*
    Matrix3(MRotationX,-_headXRotWanted).Direction()
  );
  return dir;
}

void Man::LimitRot(float &xRotWanted, float &yRotWanted, bool head) const
{
  // limit with natural head limits
  // the more y rot, the less x rot enabled
/*
  const float maxYRot = H_PI/2;
  if (!QIsManual())
  {
    Limit(yRotWanted,Type()->_minHeadTurnAI,Type()->_maxHeadTurnAI);
  }
  Limit(yRotWanted,-maxYRot,+maxYRot);
  float yRotF = fabs(yRotWanted)*(1.0f/maxYRot);
  float maxXRot = (1-yRotF)*H_PI/4;
  Limit(xRotWanted,-maxXRot,+maxXRot);
*/
  float len2=0.0f;
  if (!head)
  {
    const ViewPars &pars = GetType()->_viewPilot;
    float initYRot = pars._initAngleY*(H_PI/180);
    float maxYRot = (yRotWanted<initYRot) ? pars._minAngleY*(H_PI/180) : pars._maxAngleY*(H_PI/180);
    float initXRot = pars._initAngleX*(H_PI/180);
    float maxXRot = (xRotWanted<initXRot) ? pars._minAngleX*(H_PI/180) : pars._maxAngleX*(H_PI/180);
    float invMaxXRot2 = maxXRot!=0 ? 1/Square(maxXRot) : 1;
    float invMaxYRot2 = maxYRot!=0 ? 1/Square(maxYRot) : 1;
    len2 = (xRotWanted*xRotWanted)*invMaxXRot2+(yRotWanted*yRotWanted)*invMaxYRot2;
  }
  else
  {
    const HeadPars &pars = GetType()->_headLimits;
    float initYRot = pars._initAngleY*(H_PI/180);
    float maxYRot = (yRotWanted<initYRot) ? pars._minAngleY*(H_PI/180) : pars._maxAngleY*(H_PI/180);
    float initXRot = pars._initAngleX*(H_PI/180);
    float maxXRot = (xRotWanted<initXRot) ? pars._minAngleX*(H_PI/180) : pars._maxAngleX*(H_PI/180);
    float invMaxXRot2 = maxXRot!=0 ? 1/Square(maxXRot) : 1;
    float invMaxYRot2 = maxYRot!=0 ? 1/Square(maxYRot) : 1;
    len2 = (xRotWanted*xRotWanted)*invMaxXRot2+(yRotWanted*yRotWanted)*invMaxYRot2;
  }
  //eliptical saturation
  if (len2<=1.0f) 
  {
    if (!QIsManual() && head) Limit(yRotWanted,Type()->_minHeadTurnAI,Type()->_maxHeadTurnAI);
    return;
  }
#define GONIOMETRIC_SAT 0
#if GONIOMETRIC_SAT
  float phi = atan2(yRotWanted*maxXRot,xRotWanted*maxYRot);
  xRotWanted = maxXRot * cos(phi);
  yRotWanted = maxYRot * sin(phi);
#else
  float c = sqrt(1/len2);
  xRotWanted *= c;
  yRotWanted *= c;
#endif
  if (!QIsManual() && head) Limit(yRotWanted,Type()->_minHeadTurnAI,Type()->_maxHeadTurnAI);
}


/*!
\patch 5126 Date 2/1/2007 by Bebul
- Fixed: View limits in vehicle applied on TrackIR and NumPad looking around
*/
void Man::LimitHeadRot(float &xRotWanted, float &yRotWanted) const
{
  AIBrain *brain = Brain();
  if (brain && brain->GetVehicleIn()!=NULL) 
  { // we must consider also vehicle limits when inside
    float dummy;
    if (_lookAroundViewPars) _lookAroundViewPars->LimitVirtual(GWorld->GetCameraType(), yRotWanted, xRotWanted, dummy);
  }
  LimitRot(xRotWanted, yRotWanted, true);
}

void Man::LimitLookRot(float &xRotWanted, float &yRotWanted) const
{
  AIBrain *brain = Brain();
  if (brain && brain->GetVehicleIn()!=NULL) 
  { // we must consider also vehicle limits when inside
    float dummy;
    if (_lookAroundViewPars) 
    {
      _lookAroundViewPars->LimitVirtual(GWorld->GetCameraType(), yRotWanted, xRotWanted, dummy);
      return; //inside vehicles the viewPilot ranges could be able to exceed the Man head limits
    }
  }
  LimitRot(xRotWanted, yRotWanted, false);
}

void Man::AimHead(Vector3Par direction)
{
  // move head accordingly to direction
  Vector3 relDir(VMultiply,DirWorldToModel(),direction);
  // calculate current gun direction

  float yRotWanted=-atan2(relDir.X(),relDir.Z());
  float sizeXZ=sqrt(Square(relDir.X())+Square(relDir.Z()));
  float xRotWanted=atan2(relDir.Y(),sizeXZ);

  //limit look
  LimitLookRot(xRotWanted, yRotWanted);
  _lookYRotWanted = yRotWanted;
  _lookXRotWanted = xRotWanted;

  // limit with natural head limits
  // the more y rot, the less x rot enabled
  LimitHeadRot(xRotWanted, yRotWanted);
  _headYRotWanted = yRotWanted;
  _headXRotWanted = xRotWanted;
}

void Man::AimHeadAI(Vector3Par direction, Target *target, float deltaT)
{
  //static StatEventRatio ratio("AimHeadF");
  if (_lookTargetTimeLeft>0)
  {
    // target may require scanning an area
    Vector3 headDir = direction;
    if (target)
    {
      Vector3 tgtPos = Brain()->ScanTarget(target);
      // convert position back to direction
      headDir = tgtPos - EyePosition(FutureVisualState());
    }
    AimHead(headDir);

    //ratio.Count(false);
  }
  else
  {
    const float cosMaxForwardAngle = 0.96592582629f; // cos 15deg
    if (direction.CosAngle(FutureVisualState().Direction())>cosMaxForwardAngle)
    {
      // when direction is almost forward, make forward looking phase much shorter
      _lookForwardTimeLeft -= 2*deltaT;
    }
    AimHeadForward();
    //ratio.Count(true);
  }
}

void Man::AimWeaponAI(const TurretContext &context, const WeaponModePtr &ptr, Vector3Par direction, Target *target, float deltaT )
{
  // TODO: move deltaT funct. to other place
  // merge with AimWeapon


  context._weapons->SelectWeapon(this, ptr);
  // move turret/gun accordingly to direction
  Vector3 relDir(VMultiply,DirWorldToModel(),direction);

  //if (magazine)
  {
    // calculate current gun direction
    // compensate for neutral gun position

    float inacc=RifleInaccuracy()*GetInvAbility(AKAimingShake);
    inacc *= 1.0f-0.75f*_holdBreath;

    const OpticsCameraInfo *opticsInfo = ptr.GetOpticsCameraInfo();
    DoAssert(opticsInfo);
    // calculate actual neutral rotation
    // for this: animate gun points as necessary
    float yRotWanted=-atan2(relDir.X(),relDir.Z()) - opticsInfo->_neutralYRot;
    float sizeXZ=sqrt(Square(relDir.X())+Square(relDir.Z()));
    float xRotWanted=atan2(relDir.Y(),sizeXZ) - opticsInfo->_neutralXRot;

    const Magazine *magazine = ptr.GetMagazine();
    const MagazineType *mag = magazine ? magazine->Type() : NULL;
    const AmmoType *ammo = mag ? mag->_ammo : NULL;

    if (ammo && ammo->_simulation==AmmoShotShell && mag->_initSpeed<30)
    {
      xRotWanted -= GrenadeAtan2;
    }

    //float size2 = direction.SquareSize();

    // aim slowly - depending on ability
    //float maxSpeed=GetAbility()*0.2f*deltaT;
    // when target is near, ability is always bettter
    // on 100 m 
    float ability = GetAbility(AKAimingSpeed);
    //float minAbility 
    float weaponDexterity = 1;
    if (ptr.IsValid())
    {
      weaponDexterity = ptr.SumDexterityModifiers(this);
    }

    float maxSpeed=ability*0.2f*deltaT*weaponDexterity;
    float delta;

    float coef = 0.5f;
    xRotWanted+=_aimInaccuracyX*coef;
    yRotWanted+=_aimInaccuracyY*coef;

    Limit(xRotWanted,Type()->_minGunElev*1.5f,Type()->_maxGunElev*1.5f);
    Limit(yRotWanted,Type()->_minGunTurnAI*1.5f,Type()->_maxGunTurnAI*1.5f);


    delta=yRotWanted-_gunYRotWanted;
    saturate(delta,-maxSpeed,+maxSpeed);
    _gunYRotWanted+=delta;

    delta=xRotWanted-_gunXRotWanted;
    saturate(delta,-maxSpeed,+maxSpeed);
    _gunXRotWanted+=delta;
    
    // when not aiming for fire, set maximal error
    // this avoids recalculating _gunTrans, which is quite expensive
    // at the same time it prevents aiming accurately in prepare mode
    // which could make problems when switching from prepare to fire mode
    float maxAimInacc=floatMin(inacc,0.2f);
    float maxDistIA=(GetInvAbility(AKAimingShake)-1)*0.1f;
    saturateMax(maxDistIA,0.00001);

    _aimInaccuracyX = maxAimInacc;
    _aimInaccuracyY = maxAimInacc;
    _aimInaccuracyDist = maxDistIA;
  }
  AimHeadAI(direction,target,deltaT);
}

void Man::AimWeaponAI(const TurretContext &context, const WeaponModePtr &ptr, Target *target, float deltaT )
{
  WeaponModePtr selected = ptr;
  if (!selected.IsValid())
  {
    if (!context._weapons->HasWeapons()) return;
    selected = context._weapons->FirstWeapon(this);
  }

  Vector3 dir;
  float timeToLead;
  if (!CalculateAimWeapon(context, ptr,dir,timeToLead,target,false))
    return;
  // while in corner cover or a wall cover, we need to prevent aiming into the cover
  // however, we do not obey this when aiming to kill
  if (
    _cover && _coverState==CoverInCover && (_cover->_type<CoverEdgeMiddle || _cover->_type>=CoverWall)
    // note: checking only _firePrepareOnly may prevent visibility ever established - we check target state as well
    // && context._weapons->_fire.GetTargetFinished(_brain) && context._weapons->_fire._firePrepareOnly
  ) 
  {
    // cover limits depend on current position and cover type
    
    Matrix4 coverPos(MTranslation,_cover->_pos);
    coverPos.SetUpAndAside(VUp,_cover->_coverPos.DirectionAside());
    Matrix4 toCover(MInverseRotation,coverPos);

    // we assume we are aligned in the cover so that pos - _cover->_pos

    // note: using GetWeaponPoint causes circular calculation dependency here, as it changes with aiming
    // however, no instability was observer and it handles leaning fine, which GetWeaponCenter would not
    Vector3 weaponPosRel = GetWeaponPoint(FutureVisualState(),context,ptr);
    Vector3 weaponCenterRel = GetWeaponCenter(FutureVisualState(),context,ptr);
    Vector3 weaponDirRel = GetWeaponRelDirection(FutureVisualState(),ptr,true);
    // problem with GetWeaponPoint is the point is too far away and it cannot be used as a basis for weapon direction
    // however when saturating aim we need to assume leaning as well
    // we solve this by projecting from weaponPos back to the point nearest to GetWeaponCenter
    Vector3 weaponPosBackRel = NearestPointInfinite(weaponPosRel,weaponPosRel-weaponDirRel,weaponCenterRel);
    Vector3 weaponPos = FutureVisualState().PositionModelToWorld(weaponPosBackRel);
    //Vector3 weaponPos = PositionModelToWorld(GetWeaponCenter(FutureVisualState(),context,weapon));
    Vector3 dirToCover = _cover->_pos - weaponPos;
    if (_cover->_type==CoverWall)
      dirToCover = -_cover->_coverPos.Direction();
    // coordinate space where dirToCover is forward
    // we may simply clamp the vector left/right in such space
    Matrix3 dirSpace(MDirection,dirToCover,VUp);
    Matrix3 toDirSpace(MInverseRotation,dirSpace);
    
    Vector3 aimInDirSpace = toDirSpace*dir;
    switch (_cover->_type)
    {
      case CoverRightCornerHigh: case CoverRightCornerMiddle: case CoverRightCornerLow:
        // when on the right corner, we cannot aim too left
        // however, we may be using the cover in an "opposite" direction (right as left)
        if (aimInDirSpace[0]<0)
        {
          // make sure the vertical angle stays the same, at the same time handle aim heading back
          aimInDirSpace[2] = aimInDirSpace.SizeXZ();
          aimInDirSpace[0] = 0;
        }
        break;
      case CoverLeftCornerHigh: case CoverLeftCornerMiddle: case CoverLeftCornerLow:
        if (aimInDirSpace[0]>0)
        {
          aimInDirSpace[2] = aimInDirSpace.SizeXZ();
          aimInDirSpace[0] = 0;
        }
        break;
      case CoverWall:
        // never aim into the wall
        // to avoid singularity, instead of aiming into the wall just mirror the aiming
        aimInDirSpace[2] = -fabs(aimInDirSpace[2]);
        break;
    }
    if (CHECK_DIAG(DECombat))
      GScene->DrawDiagArrow("Aim",weaponPos,dir,1.0f,Color(0,1,1));
    // transform the clamped result back
    dir = dirSpace * aimInDirSpace;
    if (CHECK_DIAG(DECombat))
      GScene->DrawDiagArrow("Aim sat",weaponPos+VUp*0.2,dir,0.9f,Color(0,0.5,1));
  }
  AimWeaponAI(context, ptr,dir,target,deltaT);
}

bool Man::IsAutoAimEnabled(const TurretContext &context, const WeaponModePtr &ptr) const
{
  return !DisableWeapons() && base::IsAutoAimEnabled(context, ptr);
}

Vector3 Man::ValidateAimDir(Vector3Par dir) const
{
  // when we are not lying, the gun is always high enough
  if (!IsDown())
    return dir;
  // avoid aiming under the ground
  // check current surface properties
  Vector3 pos = FutureVisualState().Position();
  float dX,dZ;

  GLandscape->RoadSurfaceYAboveWater(pos+VUp*0.5f,&dX,&dZ);
  Vector3 normal(-dX,1,-dZ);
  //normal.Normalize();
  
  const float cosMaxAngle = -0.34202014333; // cos 110 deg
  //const float cosMaxAngle = -0.5; // cos 120 deg
  float cosAlpha = normal.CosAngle(dir);
  if (cosAlpha>cosMaxAngle) return dir;

  Vector3 adir = dir;
  // max. allowed direction is normal*dir  = cosMaxAngle*(|normal|*|dir|)
  adir[1] = cosMaxAngle*normal.Size()*dir.Size()-normal[0]*dir[0]-normal[2]*dir[2];
  
  return adir;
}


/*!
\patch 2.01 Date 1/8/2003 by Ondra
- New: Autoaiming option.
*/

void Man::AimWeaponManDir(const TurretContextEx &context, const WeaponModePtr &ptr, Vector3Par direction)
{
}

void Man::AimHeadAndWeapon(Vector3Par direction)
{
  WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
  TurretContextEx context;
  context._turret = NULL;
  context._turretType = NULL;
  context._gunner = this;
  context._weapons = &_weaponsState;
  context._parentTrans = FutureVisualState().Orientation();
  base::AimWeaponManDir(context, ptr, direction);
}

void Man::SimulateHUD(CameraType camType, float deltaT)
{
}

bool Man::AimWeaponAdjusted(const TurretContextEx &context, const WeaponModePtr &ptr, Vector3Par direction )
{
  _weaponsState.SelectWeapon(this, ptr);
  // move turret/gun accordingly to direction
  // we need to transfer into leaning space - 
  Matrix3 invLean = FutureVisualState()._leaningTrans.Orientation().InverseRotation();
  //Vector3 relDir = DirWorldToModel()*(invLean*direction);
  //Vector3 relDir = DirWorldToModel()*invLean.FastTransform(direction);
  Vector3 relDir = DirWorldToModel()*direction;
  // calculate current gun direction
  // compensate for neutral gun position
  const AmmoType *aType = ptr.GetAmmoType();

  float inacc=RifleInaccuracy()*GetInvAbility(AKAimingShake);

  // when holding the breath, rifle is shaking much less
  // as we are loosing air, rifle starts shaking again
  //inacc *= 1.0f-0.75f*_holdBreath*_breathAir;
  inacc *= 1.0f-0.75f*_holdBreath+(1-_breathAir)*0.75f/0.5f;
  // calculate actual neutral rotation
  // for this: animate gun points as necessary
  float yRotWanted=-atan2(relDir.X(),relDir.Z());
  float sizeXZ=sqrt(Square(relDir.X())+Square(relDir.Z()));
  float xRotWanted=atan2(relDir.Y(),sizeXZ);

  // note: this function should be used only for manual units
  // but it may be used for AI when forceFire function is used

  const float coef = 0.25f;
  const float invCoef = 1/coef;

  float minInaccY = floatMin(0,(Type()->_minGunTurnAI-yRotWanted)*invCoef);
  float maxInaccY = floatMax(0,(Type()->_maxGunTurnAI-yRotWanted)*invCoef);

  saturate(_aimInaccuracyY,minInaccY,maxInaccY);

  float inaccX = _aimInaccuracyX*coef;
  float inaccY = _aimInaccuracyY*coef;

  _gunXRotWantedNoTrembleHands = xRotWanted;
  _gunYRotWantedNoTrembleHands = yRotWanted;
  _gunXRotWanted = xRotWanted + inaccX;
  _gunYRotWanted = yRotWanted + inaccY;

#if _VBS2 // fatigue model
  if (Glob.config.IsEnabled(DTRealisticFatigue) && !BinocularSelected())
  {
  // Weapon sway and steadiness based on breathing of soldier - whether holding breath,
  // and what breath rate is.
  // Fundamentally 3 states:
  //  BreathNormal - weapon sways with inhalation and exhalation of soldier.
  //  BreathSteady - weapon is rock steady while soldier holds breath.
  //  BreathShakey - weapon is kind of steady as soldier is running out of held breath.
  float iTime = Glob.time-_lastInaccuracyTime;

  if (_breathState==BreathNormal) 
  {
    if (_holdBreath==0) 
    {
      _timeSinceLastBreathHold += iTime;
    }
    else if (_timeSinceLastBreathHold>=_interBreathHoldInterval) 
    {
      _breathState = BreathSteady;
      _timeSinceLastBreathHold = 0.0;
      _timeHoldingBreath = iTime;
      // TODO: Turn off breathing sound
    }
    else 
    { // Trying to hold breath when haven't "caught breath" yet
      // TODO: Give gasp as try to hold breath unsuccessfully
    }
  }
  else if (_breathState==BreathSteady) 
  {
    if (_holdBreath==0) 
    {  // Have released breath
      _timeSinceLastBreathHold = 0;
      _interBreathHoldInterval = 0.1*MAX_TIME_TO_NEXT_HOLD*(MAX_BREATH_HOLD_DURATION-_canHoldBreathFor + _timeHoldingBreath);
      _cumulativeAnaerobicEnergy += BREATH_HOLD_ENERGY_COST*_timeHoldingBreath/_canHoldBreathFor;
      _cumulativeAerobicEnergy += BREATH_HOLD_ENERGY_COST*_timeHoldingBreath/_canHoldBreathFor;
      _timeHoldingBreath = 0;
      _breathState = BreathNormal;
      // TODO: Turn on breathing sound
    }
    else 
    {
      _timeHoldingBreath += iTime;
      if (_timeHoldingBreath>_canHoldSteadyFor) 
      { // Getting shakey
        _breathState = BreathShakey;
      }
    }
  }
  else 
  { // In Shakey breath period
    if (_holdBreath==0) 
    {  // Have released breath
      _timeSinceLastBreathHold = 0;
      _interBreathHoldInterval = 0.1*MAX_TIME_TO_NEXT_HOLD*(MAX_BREATH_HOLD_DURATION-_canHoldBreathFor + _timeHoldingBreath);
      _cumulativeAnaerobicEnergy += BREATH_HOLD_ENERGY_COST*_timeHoldingBreath/_canHoldBreathFor;
      _cumulativeAerobicEnergy += BREATH_HOLD_ENERGY_COST*_timeHoldingBreath/_canHoldBreathFor;
      _timeHoldingBreath = 0;
      _breathState = BreathNormal;
      // TODO: Turn on breathing sound
    }
    else 
    {
      _timeHoldingBreath += iTime;
      if (_timeHoldingBreath>_canHoldBreathFor) 
      {    // Can no longer hold breath!
        // TODO: Turn on breathing. Make a sudden gasp of indrawn breath
        _aimSwayAngle = 1.5*H_PI;  // Held breath is released - gun drops down to bottom of curve
        _timeSinceLastBreathHold = 0;
        _interBreathHoldInterval = 0.1*MAX_TIME_TO_NEXT_HOLD*(MAX_BREATH_HOLD_DURATION-_canHoldBreathFor + _timeHoldingBreath);
        _timeHoldingBreath = 0;
        _cumulativeAnaerobicEnergy += BREATH_HOLD_ENERGY_COST;
        _cumulativeAerobicEnergy += BREATH_HOLD_ENERGY_COST;
        _breathState = BreathGasp;
      }
    }
  }

  // Control the weapon facing variance in response to the breath state 
  if (_suppressedWince)
  {
    _gunXRotWanted = _gunXRotWantedNoTrembleHands -= GRandGen.Gauss(-SUPPRESSION_X_TWITCH/2,0,SUPPRESSION_X_TWITCH/2)*_suppressedWince;
    _gunYRotWanted = _gunYRotWantedNoTrembleHands -= GRandGen.Gauss(-SUPPRESSION_Y_TWITCH/2,0,SUPPRESSION_Y_TWITCH/2)*_suppressedWince;
    _suppressedWince = 0;
  }
  // Soldier is breathing normally (not holding breath) - hence put a weave (figure-8) in weapon sights.
  else if (_breathState==BreathNormal) 
  {
    _aimRadiansPerSecond = 2.0*H_PI*_breathsPerMinute/60.0;
    _aimSwayAngle+= _aimRadiansPerSecond*iTime; 
    if (_aimSwayAngle>2*H_PI)
      _aimSwayAngle -= 2*H_PI;
    float fractionMaxRate = (_breathsPerMinute-MIN_BREATH_RATE)/(MAX_BREATH_RATE-MIN_BREATH_RATE);
    if (fractionMaxRate<0)
      fractionMaxRate = 0.0;
    if (fractionMaxRate>1.0)
      fractionMaxRate = 1.0;
    // Scale according to the posture - less variance for kneeling and lying
    _aimPostureFactor = STANDING_AIM_SCALE;  // defaults to standing
    if (GetActUpDegree()<=ManPosHandGunLying)
      _aimPostureFactor = LYING_AIM_SCALE;
    else if (GetActUpDegree()<ManPosHandGunCrouch)
      _aimPostureFactor = KNEELING_AIM_SCALE;

    // The "random walk" factor in the sway due to a heavy weapon. This is a noise (offset) that gets added
    // into the inaccuracy values. The net result is the sights wander off the straight figure-8.
    if (GRandGen.RandomValue()<AIM_NOISE_RESET_CHANCE*iTime) 
    {
      _aimXOffset *= AIM_NOISE_RESET_MULTIPLIER;
      _aimYOffset *= AIM_NOISE_RESET_MULTIPLIER;
      //GlobalShowMessage(1000,"Reset wander");
    }
    else 
    {
      _aimXOffset += (GRandGen.RandomValue()-0.5)*2.0*_aimOffsetVariability*iTime*_aimMaxXOffset;
      if (_aimXOffset>_aimMaxXOffset)
        _aimXOffset = _aimMaxXOffset;
      if (_aimXOffset<-_aimMaxXOffset)
        _aimXOffset = -_aimMaxXOffset;
      _aimYOffset += (GRandGen.RandomValue()-0.5)*2.0*_aimOffsetVariability*iTime*_aimMaxYOffset;
      if (_aimYOffset>_aimMaxYOffset)
        _aimYOffset = _aimMaxYOffset;
      if (_aimYOffset<-_aimMaxYOffset)
        _aimYOffset = -_aimMaxYOffset;
    }

    // Inaccuracy is primarily a function of a figure-8 function (the trig) that sways in time with the breathing rate.
    _aimInaccuracyY =_aimPostureFactor*((1.0+fractionMaxRate*AIM_RANGE_SCALE_FACTOR)*VERTICAL_AIM_RANGE*sin(_aimSwayAngle)*cos(_aimSwayAngle)+_aimXOffset);    
    _aimInaccuracyX = _aimPostureFactor*((1.0+fractionMaxRate*AIM_RANGE_SCALE_FACTOR)*HORIZONTAL_AIM_RANGE*sin(_aimSwayAngle)+_aimYOffset);
    _canHoldBreathFor = MAX_BREATH_HOLD_DURATION*(1.0-fractionMaxRate) + 0.2;
    _canHoldSteadyFor = 0.5*_canHoldBreathFor;
  }
  // Soldier has held breath till the last - gasping exhale so weapon drops
  // This should really be held across several frames so the user sees it.
  else if (_breathState==BreathGasp) 
  {
    _breathState = BreathNormal;
    _aimInaccuracyY = -3*VERTICAL_AIM_RANGE;
    _aimInaccuracyX = (GRandGen.RandomValue()-0.5)*HORIZONTAL_AIM_RANGE;
  }
  // All holding breath below here.
  // Soldier is holding breath, but weapon is heavy - hence bad shakes
  else if (_weaponMassType==WeaponMassHeavy && GetActUpDegree()>ManPosHandGunLying) 
  {
    float fractionUnsteady = _timeHoldingBreath/_canHoldSteadyFor;
    _aimInaccuracyY = fractionUnsteady*(GRandGen.RandomValue()-0.5)*VERTICAL_AIM_RANGE;
    _aimInaccuracyX= fractionUnsteady*(GRandGen.RandomValue()-0.5)*HORIZONTAL_AIM_RANGE;
  } // About to run out of breath - add some shake proportional to how close to gasping for a new breath
  else if (_breathState==BreathShakey && GetActUpDegree()>ManPosHandGunLying) 
  {
    float fractionUnsteady = (_timeHoldingBreath-_canHoldSteadyFor)/_canHoldSteadyFor;
    _aimInaccuracyY = fractionUnsteady*(GRandGen.RandomValue()-0.5)*VERTICAL_AIM_RANGE;
    _aimInaccuracyX= fractionUnsteady*(GRandGen.RandomValue()-0.5)*HORIZONTAL_AIM_RANGE;
  }
  _lastInaccuracyTime = Glob.time;
  } // DTRealisticFatigue
  else
  {
#endif
  // tremble hands
  float iTime=Glob.time-_lastInaccuracyTime;
  while( (iTime-=0.2f)>=0 )
  {
    float randX=GRandGen.RandomValue()-0.5f;
    float randY=GRandGen.RandomValue()-0.5f;
    _aimInaccuracyX+=randX*inacc;
    _aimInaccuracyY+=randY*inacc;
    saturate(_aimInaccuracyX,-inacc,+inacc);
    saturate(_aimInaccuracyY,-inacc,+inacc);
    _lastInaccuracyTime=Glob.time;
  }
#if _VBS2 // fatigue model
  }
#endif
  _aimInaccuracyDist=0;

  /*
  GlobalShowMessage
  (
    100,"Aim XY %+.4f,%+.4f, max %+.4f",
    _aimInaccuracyX,_aimInaccuracyY,inacc
  );
  */
  if (_weaponsState._forceFireWeapon.IsValid())
  {
    // note: this check is only required when performing _forceFire
    // TODO: create different function for this situation

    if (aType)
    {
      switch (aType->_simulation)
      {
        case AmmoShotPipeBomb:
        case AmmoShotTimeBomb:
        case AmmoShotMine:
          return true;
      }
    }
    // check if we have already aimed
    float f = GetLimitGunMovement(FutureVisualState());
    Limit(_gunXRotWanted,Type()->_minGunElev*f,Type()->_maxGunElev*f);
    Limit(_gunYRotWanted,Type()->_minGunTurnAI*f,Type()->_maxGunTurnAI*f);
    return
    (
      fabs(_gunXRotWanted-_gunXRot)<0.01f &&
      fabs(_gunYRotWanted-_gunYRot)<0.01f
    );
  }
  return true;
}

bool Man::AimWeapon(const TurretContextEx &context, const WeaponModePtr &ptr, Vector3Par direction )
{
  return AimWeaponAdjusted(context, ptr, direction);
}

bool Man::AimWeaponForceFire(const TurretContextEx &context, const WeaponModePtr &ptr)
{
  Vector3 dir = FutureVisualState().Direction();
  dir[1] = 10;
  // different direction depending on weapon type
  if (!ptr.IsValid()) return false;
  const Magazine *magazine = ptr.GetMagazine();
  if (!magazine) return false;
  const MagazineType *aInfo = magazine->Type();
  const AmmoType *aType = aInfo ? aInfo->_ammo : NULL;
  if (aType)
  {
    switch (aType->_simulation)
    {
      case AmmoShotPipeBomb:
      case AmmoShotTimeBomb:
      case AmmoShotMine:
        return true;
      case AmmoShotShell:
        if (aInfo->_initSpeed<30) dir[1]=0.5f;
        else dir[1]=1;
        break;
    }

  }
#if _VBS3
  if(context._weapons->_forceFirePosition != VZero)
  {
    float timeToLead = 10.0f;
    CalculateAimWeapon(context,weapon,dir,timeToLead,context._weapons->_forceFirePosition);
  }
#endif
  dir.Normalize();
  return AimWeapon(context, context._weapons->ValidatedCurrentWeapon(), dir);
}

/*!
\patch 1.52 Date 4/21/2002 by Ondra
- Fixed: Units sometimes aimed into air when searching for unknown target.
*/

bool Man::CalculateAimWeaponPos(const TurretContext &context, const WeaponModePtr &ptr, Vector3 &pos, float &timeToLead, Target *target, bool exact) const
{
  if (!ptr.IsValid())
    return false;

  if (!CheckTargetKnown(context,target))
    return false;

  const Magazine *magazine = ptr.GetMagazine();
  if (!magazine)
  { // even with binoculars we need the AI to aim at the target, however the aim is simple
    Vector3 tgtSpd;
    Vector3 tgtPos;
    float seenBefore;
    GetAimingPosition(tgtPos, tgtSpd, seenBefore, exact, context, target, NULL);

    pos = tgtPos;
    return true;
  }
  const MagazineType *aInfo = magazine->Type();
  Assert(aInfo);
  const AmmoType *ammo = aInfo ? aInfo->_ammo : NULL;

  Vector3 tgtSpd;
  Vector3 tgtPos;
  float seenBefore;
  GetAimingPosition(tgtPos, tgtSpd, seenBefore, exact, context, target, ammo);

  float dist=tgtPos.Distance(FutureVisualState().Position());
  // distance estimation is quite bad for soldiers
  // introduce some distance estimation error

  // verbose only for sniper

  // scale factor according to distance
  float distFactor = dist<40 ? 0 : dist-40;

  dist+=_aimInaccuracyDist*distFactor;

  // never shoot grenades to very short range
  saturateMax(dist,10);
  float time=dist*aInfo->_invInitSpeed;
  float balFactor = 0.5f*G_CONST;
  float leadFactor = 1;
  bool rockets = false;
  //weaponPos=GunTransform()*weaponPos;
  if (ammo) switch (ammo->_simulation)
  {
    case AmmoShotBullet:
    case AmmoShotSpread:
    break;
    case AmmoShotShell:
      // grenade launcher ballistics
      // adjusted to work at 100 m
      if (aInfo->_initSpeed<30) time*=1.3f;
      else time*=1.1f;
    break;
    case AmmoShotMissile:
      time = ammo->maxSpeed > 0 ? dist / ammo->maxSpeed + 0.1f : 0.1f;
      balFactor *= 0.37f;
      leadFactor = 1.4f;
    break;
    default:
      time=0;
    break;
  }
  else time=0;
  saturateMin(time,4);

  float fall = Square(time)*balFactor;
  if (ammo && (ammo->_simulation==AmmoShotMissile || ammo->_simulation==AmmoShotRocket))
  {
    if(ammo->maxControlRange >= 10) fall = time = 0; //correction is not needed for guided missiles
    else time = PreviewRocketBallistics(tgtPos,FutureVisualState().Position(),aInfo,ammo,fall);
    if (ammo->maxControlRange<10) rockets = true;
  }

  // calculate ballistics
  tgtPos[1]+=fall; // consider ballistics

  // lead target
  // for rockets this speed correction aprox. is not working properly
  Vector3 speedEst = rockets? tgtSpd : tgtSpd - FutureVisualState().Speed();
  if (aInfo)
  {
    float maxLead = aInfo->_maxLeadSpeed*GetAbility(AKAimingAccuracy);
    //LogF("Speed %.2f,max %.2f, time %.3f",speedEst.Size(),maxLead,time);
    LimitLead(speedEst,maxLead);
    tgtPos+=speedEst*time*leadFactor;
  }

  pos = tgtPos;
  return true;
}

bool Man::AimWeaponTgt(const TurretContextEx &context, const WeaponModePtr &ptr, Target *target)
{
  Vector3 dir;
  float timeToLead;
  if (!CalculateAimWeapon(context, ptr, dir, timeToLead, target, false)) return false;
  return AimWeapon(context, ptr, dir);
}

#if _VBS3
void Man::DrawLaserPointer(VisionMode vm, int lightMode)
{
  TurretContext context;
  if(GetPrimaryGunnerTurret(context))
  {
    int weapon = _weaponsState.ValidatedCurrentWeapon();
    if (weapon<0) return;
    const MagazineSlot &mag = _weaponsState._magazineSlots[weapon];

    if(lightMode == -1) //no scripting mode set, take weapon mode
    {
      const WeaponType *weaponType = mag._weapon;
      //weapon is not configured for lightmode
      if(!weaponType) return;
      lightMode = weaponType->_lightMode;
    }

    if(!(lightMode & 1<<vm)) return;

    const MuzzleType *muzzle = mag._muzzle;

    if(muzzle->_laserPos == VZero) return;

    Vector3 dir = GetWeaponDirection(context,weapon);
    Vector3 startPos = PositionModelToWorld(GetLaserPoint(context, weapon));
    dir.Normalize();
    const float maxLength = 100.0f;

    Vector3 endPos;
    bool collide = false;
    float tGround = GLandscape->IntersectWithGroundOrSea(&endPos,startPos,dir,0,maxLength);
    // we've hit the ground, check if there is an object collision
    if( tGround >= maxLength)
      endPos = startPos + dir * maxLength;
    else
      collide = true;
    CollisionBuffer result;
    //TODO: objectcollision is wrong here! 
    GLandscape->ObjectCollision(result,NULL,this,startPos,endPos,0.5f,ObjIntersectView,ObjIntersectFire);
    if(result.Size() > 0)
    {
      endPos = result[0].pos;
      float dist2 = startPos.Distance2(endPos);
      for(int i=0; i< result.Size();++i) //sort results in regards to startpos
      {
        if(startPos.Distance2(result[i].pos) < dist2)
          endPos = result[i].pos;
      }
      collide = true;
    }

    float length = Vector3(endPos - startPos).Size();

    //TODO: optimize drawing! animated texture?
    if(vm == VMDv)
    {
      PackedColor color(50,0,0,30);
      while(length > 0)
      {
        float l = GRandGen.RandomValue() * 3.0f+1.0f;
        length -= l;
        if(length <0) l += length;
        endPos = startPos + l* dir;
        color.SetA8(l*10);
        //          GVBSVisuals.Add3dLine(startPos,endPos, color, -0.003f);
        GVBSVisuals.Add3dLine(startPos,endPos, color, 3.0f);
        startPos = endPos;
      }

      if(collide)
        GScene->DrawDiagSphere(endPos, 0.02f, PackedColor(255,0,0,230));
    }
    else //NV
    {
      //      PackedColor color(200,200,200,100);
      /*
      while(length > 0)
      {
      float l = GRandGen.RandomValue() * 3.0f+1.0f;
      length -= l;
      if(length <0) l += length;
      endPos = startPos + l* dir;
      int col = l*40;
      PackedColor color(col, col, col, 100);
      */
      PackedColor color(50, 50, 50, 100);
      GVBSVisuals.Add3dLine(startPos,endPos, color, -0.02f);
      startPos = endPos;
      //      }
      if(collide)

      {
        GScene->DrawDiagSphere(endPos, 0.1f, PackedColor(255,255,255,230));
        GScene->DrawDiagSphere(endPos, 0.3f, PackedColor(255,255,255,100));
      }
    }
  }
}

void Man::ShowTrajectory()
{
  TurretContext context;
  if(GetPrimaryGunnerTurret(context))
  {
    int weapon = _weaponsState.ValidatedCurrentWeapon();
    if (weapon < 0) return;

    const MagazineSlot &mag = _weaponsState._magazineSlots[weapon];

    const MuzzleType *muzzle = mag._muzzle;

    if(!muzzle->_showTrajectory)
      return;

    const Magazine *magazine = mag._magazine;
    if (!magazine) return;
    const MagazineType *aInfo = magazine->Type();
    if (!aInfo) return;

    const AmmoType * aType = aInfo->_ammo;
    if(!aType) return;

    Vector3 dir    = GetWeaponDirection(context,weapon);
    Vector3 wPos   = PositionModelToWorld(GetWeaponPoint(context, weapon));
    if(aInfo->_initSpeed < 40) //grenades and thrown objects, move point to shoulder
      wPos = COMPosition() + Orientation() * Vector3(0.2,0.3,0.2);

    dir.Normalize();
    Vector3 speed = dir * aInfo->_initSpeed * GetAmmoInitSpeedFactor(aInfo);

    Vector3 startPos = wPos;

    float deltaT = 0.05f;
    PackedColor colorA(200,200,200,100);
    PackedColor colorB(200,200,200,100);

    SYSTEMTIME sysTime;
    GetLocalTime(&sysTime);
    bool skip = sysTime.wMilliseconds > 500;

    float dist = 0.0f;
    bool collide = false;

    Vector3 newPos = startPos;
    const float alpha = 1.0f;
    // after 10 second's fade out the trajectory
    /*    User feedback: fading out is irritating

    DWORD currentTime = ::GlobalTickCount();
    DWORD lastChange  = _lastWeaponSwitchTime;


    float alpha = 1.0f-((float)(currentTime-lastChange)/10000.0f);
    alpha = floatMinMax(alpha,0.0,0.4f);

    colorA.SetA8(toInt(alpha * 255));
    colorB.SetA8(toInt(alpha * 255));
    */
    while(dist < 300.0f && !collide) 
    {
      Vector3 segment = speed * deltaT;
      newPos = startPos + segment; 
      float segmentLength = segment.Size();
      dist += segmentLength;

      CollisionBuffer result;
      GLandscape->ObjectCollision(result,NULL,this,startPos,newPos,0.5f,ObjIntersectFire);
      if(result.Size() > 0)
      {
        newPos = result[0].pos;
        collide = true;
      }
      else //check ground intersection
      {
        Vector3 isect;
        float tGround = GLandscape->IntersectWithGroundOrSea(&isect,startPos,speed.Normalized(),0,segmentLength);

        // we've hit the ground, check if there is an object collision
        if( tGround < segmentLength)
        {
          newPos = isect; //move it out of the ground again
          collide = true;
        }
      }

      GVBSVisuals.Add3dLine(startPos,newPos, skip ? colorA : colorB, -0.02f);

      skip = !skip;

      Vector3 accel = speed*(speed.Size() * aType->_airFriction);
      accel[1] -= aType->_coefGravity * G_CONST;
      speed += accel*deltaT;

      startPos = newPos;
    }
    if(collide)
    {
      GScene->DrawDiagSphere(newPos, 0.5f, PackedColor(100,100,100,toInt(alpha * 255)));
    }
  }
};
#endif



// adjust weapon for distance fov
void Man::AdjustWeapon(const WeaponModePtr &ptr, float fov, Vector3 &camDir)
{
  if (!ptr.IsValid()) return;
  const Magazine *magazine = ptr.GetMagazine();
  if (!magazine) return;
  const MagazineType *aInfo = magazine->Type();
  if (!aInfo) return;
  const OpticsInfo *opticsInfo = ptr.GetOpticsInfo();
  if (!opticsInfo) return;


  // calculate adjustment distance
  // interpolate between adjustment distances depending on fov
  float distance = 0;
  WeaponModePtr current = _weaponsState.ValidatedCurrentWeapon();
  if (opticsInfo->_discreteDistance.Size() == 0)
  {
    distance = Interpolativ(fov, opticsInfo->_opticsZoomMin, opticsInfo->_opticsZoomMax,
      opticsInfo->_distanceZoomMin, opticsInfo->_distanceZoomMax);
  }
  else if (current.IsValid())
  {
    distance =  opticsInfo->_discreteDistance[std::min(current.GetDiscreteDistanceIndex(), opticsInfo->_discreteDistance.Size()-1)];
  }

  // calculate adjustment angle from distance
  // imagine we will be shooting to given distance
  // it depends on ammo fired
  // Hladas - added friction because of sniper rifles with long range
  float friction = (aInfo->_ammo)? aInfo->_ammo->_airFriction : 0;
  Vector3 speed = aInfo->_initSpeed *  Vector3(0.0f,0.0f,1.0f);
  float time = 0;
  Vector3 grav = VUp * -G_CONST ;
  //two steps  - gives me best results
  float distPart = distance * 0.5f;
  for (int i=0; i< 2; i++)
  {
    float dtime = distPart / speed.Size();
    Vector3 accel = speed * speed.Size() * friction;   
    speed += (accel + grav) * dtime;
    time +=dtime;
  }
  float fallPerM = (0.5f * G_CONST * time * time) / distance;
  
  // this correction should be applied to weapon aiming
  Vector3 relDir = DirectionWorldToModel(camDir);
  relDir[1] += fallPerM;

  camDir = FutureVisualState().DirectionModelToWorld(relDir);
}

int Man::GetWeaponIndex(const WeaponModePtr &ptr) const
{
  if (ptr._weapon != NULL) return Type()->_handsIndex;
  return -1;
}

Vector3 Man::GetWeaponRelDirectionDetectGrenades( const ManVisualState &vs, const WeaponModePtr &ptr, Vector3Par grenadeDir, bool muzzleDir ) const
{
  if (ptr.IsValid())
  {
    int weaponIndex = GetWeaponIndex(ptr);
    if (weaponIndex >= 0)
    {
      int proxyContainer = _shape->FindMemoryLevel();
      if (proxyContainer>=0)
      {
        const OpticsCameraInfo *opticsInfo = ptr.GetOpticsCameraInfo();
        if (opticsInfo)
        {
          const ProxyObjectTyped &proxy = Type()->GetProxyForLevel(proxyContainer,weaponIndex);

          const MuzzleType *muzzle = ptr.GetMuzzleType();
          Vector3Val dir = muzzleDir ? muzzle->_muzzleDir : opticsInfo->_cameraDir;

          // apply proxy transformation
          SkeletonIndex boneIndex = GetType()->GetProxyBoneForLevel(proxyContainer,weaponIndex);
          Matrix4Val animTrans = AnimateProxyMatrix(vs, proxyContainer,proxy.GetTransform(),boneIndex);
          // TODO: normalize proxy transformations
          return animTrans.Rotate(dir);
        }
      }
      return vs._gunBodyTrans.Rotate(vs._gunTrans.Direction());
    }
    // check for hand grenades

    const AmmoType *ammo = ptr.GetAmmoType();
    if (ammo && ammo->_simulation==AmmoShotShell)
    {
      return vs._gunBodyTrans.Rotate(vs._gunTrans.Rotate(grenadeDir));
    }
  }

  return vs._gunBodyTrans.Rotate(vs._gunTrans.Direction());
}

Vector3 Man::GetWeaponRelDirection( const ManVisualState &vs, const WeaponModePtr &ptr, bool muzzleDir ) const
{
  return GetWeaponRelDirectionDetectGrenades(vs, ptr, GrenadeDir, muzzleDir);
}

/**
@animTrans return how is the wanted position/direction modified by the animation
*/
Matrix4 Man::GetWeaponRelTransform(const ManVisualState &vs, const WeaponModePtr &ptr) const
{
  if (ptr.IsValid())
  {
    int weaponIndex = GetWeaponIndex(ptr);
    if (weaponIndex >= 0)
    {
      int proxyContainer = _shape->FindMemoryLevel();
      const ProxyObjectTyped &proxy = Type()->GetProxyForLevel(proxyContainer, weaponIndex);

      const OpticsCameraInfo *opticsInfo = ptr.GetOpticsCameraInfo();
      
      if (opticsInfo)
      {
        Vector3Val dir = opticsInfo->_cameraDir;
        Vector3Val pos = opticsInfo->_cameraPos;

        SkeletonIndex boneIndex = GetType()->GetProxyBoneForLevel(proxyContainer, weaponIndex);
    
        // apply proxy transformation
        Matrix4 animTrans = AnimateProxyMatrix(vs, _shape->FindMemoryLevel(),proxy.GetTransform(),boneIndex);
        Matrix4 trans;
        trans.SetDirectionAndUp(animTrans.Rotate(dir),animTrans.DirectionUp());
        trans.SetPosition(animTrans.FastTransform(pos));
        return trans;
      }
    }
  }

  // if we have nothing else, use eye position (binoc. etc)
  int level = _shape->FindMemoryLevel();
  int index = Type()->_pilotPoint;
  Vector3 pos = (index >= 0) ? AnimatePoint(vs, level, index) : VZero;
  Matrix4 trans;
  trans.SetPosition(pos);
  trans.SetOrientation(vs._gunBodyTrans.Orientation() * vs._gunTrans.Orientation());
  return trans;
}

Vector3 Man::GetEyeDirectionWanted(ObjectVisualState const& vs, const TurretContextEx &context) const
{
  float headXRotWanted=_headXRotWanted;
  float headYRotWanted=_headYRotWanted;
  //combine movement with Continuous wanted values
  headXRotWanted += _headXRotWantedCont;
  headYRotWanted += _headYRotWantedCont + _headYRotWantedPan;
  LimitHeadRot(headXRotWanted, headYRotWanted);
  Vector3 dir = (
    Matrix3(MRotationY,headYRotWanted)*
    Matrix3(MRotationX,-headXRotWanted).Direction()
  );
  return WorldTransform(vs).Rotate(dir);
}

Vector3 Man::GetLookAroundDirection(ObjectVisualState const& vs, const TurretContext &context) const
{
  float lookXRotWanted=_lookXRotWanted;
  float lookYRotWanted=_lookYRotWanted;
  //combine movement with Continuous wanted values
  lookXRotWanted += _headXRotWantedCont;
  lookYRotWanted += _headYRotWantedCont + _headYRotWantedPan;
  LimitLookRot(lookXRotWanted, lookYRotWanted);
  Vector3 dir = (
    Matrix3(MRotationY,lookYRotWanted)*
    Matrix3(MRotationX,-lookXRotWanted).Direction()
    );
  return WorldTransform(vs).Rotate(dir);
}

Vector3 Man::GetEyeDirectionWantedNoAddCont(ObjectVisualState const& vs, const TurretContextV &context) const
{
  float headXRotWanted=_headXRotWanted;
  float headYRotWanted=_headYRotWanted;
  Vector3 dir = (
    Matrix3(MRotationY,headYRotWanted)*
    Matrix3(MRotationX,-headXRotWanted).Direction()
    );
  return WorldTransform(vs).Rotate(dir);
}

Vector3 Man::GetEyeDirection(ObjectVisualState const& vs, const TurretContext &context) const
{
  // get ideal weapon direction - do not consider animation
  Vector3 dir = vs.cast<Man>()._headTrans.Direction();
  return WorldTransform(vs).Rotate(dir);
}

Vector3 Man::GetWeaponDirection(ObjectVisualState const& vs, const TurretContext &context, const WeaponModePtr &ptr) const
{
  Vector3 dir = GetWeaponRelDirection(vs.cast<Man>(), ptr, false);
  return WorldTransform(vs).Rotate(dir);
}

Vector3 Man::GetWeaponDirectionWanted(const TurretContextEx &context, const WeaponModePtr &ptr) const
{
  Matrix3 rotXMat(MRotationX,-_gunXRotWantedNoTrembleHands);
  Matrix3 rotYMat(MRotationY, _gunYRotWantedNoTrembleHands);
  Vector3 weaponDir = rotYMat * rotXMat.Direction();
  return WorldTransform(FutureVisualState()).Rotate(weaponDir);
}

/*!
\patch 5117 Date 1/12/2007 by Ondra
- Fixed: Grenade throwing direction indication more intuitive
*/
Vector3 Man::GetAimCursorDirection(const TurretContext &context, const WeaponModePtr &ptr) const
{
  Vector3 dir = GetWeaponRelDirectionDetectGrenades(RenderVisualState(), ptr, GrenadeShowDir, false);
  RenderVisualState().DirectionModelToWorld(dir,dir);
  return dir;
}



Vector3 Man::GetHeadCenterMoves(const VisualState &vs) const
{
  int sel = Type()->_headAxisPoint;
  int level=_shape->FindMemoryLevel();
  if (sel<0) return VZero;
  if (level<0) return VZero;
  BlendAnims blend;
  GetBlendAnims(vs,blend);
  return _moves.AnimatePoint(blend,GetMovesType(), _shape, level, sel);
}

Vector3 Man::GetLeaningCenterMoves(const VisualState &vs) const
{
  int sel = Type()->_leaningAxisPoint;
  if (sel<0) return VZero;
  int level=_shape->FindMemoryLevel();
  if (level<0) return VZero;
  BlendAnims blend;
  GetBlendAnims(vs,blend);
  return _moves.AnimatePoint(blend,GetMovesType(), _shape, level, sel);
}

Vector3 Man::GetWeaponCenterMoves(VisualState const& vs) const
{
  int sel = Type()->_aimingAxisPoint;
  if (sel<0) return VZero;
  int level=_shape->FindMemoryLevel();
  if (level<0) return VZero;
  BlendAnims blend;
  GetBlendAnims(vs.cast<Man>(), blend);
  return _moves.AnimatePoint(blend,GetMovesType(), _shape, level, sel);
}

Vector3 Man::GetWeaponCenter(ObjectVisualState const& vs, const TurretContext &context, const WeaponModePtr &ptr) const
{
  int sel = Type()->_aimingAxisPoint;
  if (sel<0) return VZero;
  int level=_shape->FindMemoryLevel();
  if (level<0) return VZero;
  return AnimatePoint(vs, level,sel);
}

Vector3 Man::AnimateProxyPoint(const Object *item, Vector3Par modelPos)
{
  const ManType *type = Type();

  int level = _shape->FindMemoryLevel();
  if (level > 0)
  {
    // find the proxy
    int proxyIndex = -1;
    for (int i=0; i<type->GetProxyCountForLevel(level); i++)
    {
      const ProxyObjectTyped &proxy = type->GetProxyForLevel(level, i);
      Object *proxyObj = proxy.obj;
      if (!proxyObj) continue;
      const EntityType *proxyType = proxyObj->GetEntityType();
      if (!proxyType) continue;
      // the item can be in hands or in an inventory slot
      InventorySlotId slot = proxyType->GetInventorySlot();
      if (slot == ProxyInventoryType::_hands)
      {
        if (item == _itemInHands)
        {
          proxyIndex = i;
          break;
        }
      }
      else if (slot >= 0 && slot < NInventorySlotId)
      {
        if (item == _inventorySlots[slot])
        {
          proxyIndex = i;
          break;
        }
      }
    }
    if (proxyIndex >= 0)
    {
      // proxy found
      SkeletonIndex boneIndex = GetType()->GetProxyBoneForLevel(level, proxyIndex);
      const ProxyObjectTyped &proxy = Type()->GetProxyForLevel(level, proxyIndex);
      const ObjectVisualState &vs = RenderVisualState();
      Matrix4 animTrans = AnimateProxyMatrix(vs, level, proxy.GetTransform(), boneIndex);

      // apply transformation
      return animTrans.FastTransform(modelPos);
    }
  }

  // proxy not found
  return base::AnimateProxyPoint(item, modelPos);
}

Vector3 Man::GetWeaponPoint(ObjectVisualState const& vs, const TurretContext &context, const WeaponModePtr &ptr) const
{
  if (ptr.IsValid())
  {
    int weaponIndex = GetWeaponIndex(ptr);
    if (weaponIndex >= 0)
    {
      int proxyContainer = _shape->FindMemoryLevel();
      if (proxyContainer>=0)
      {
        SkeletonIndex boneIndex = GetType()->GetProxyBoneForLevel(proxyContainer,weaponIndex);
        const ProxyObjectTyped &proxy = Type()->GetProxyForLevel(proxyContainer,weaponIndex);
        Matrix4 animTrans = AnimateProxyMatrix(vs, proxyContainer,proxy.GetTransform(),boneIndex);

        const MuzzleType *muzzle = ptr.GetMuzzleType();
        Vector3 pos = muzzle->_muzzlePos;
        const AmmoType *ammo = ptr.GetAmmoType();
        if (ammo && (ammo->_simulation == AmmoShotBullet || ammo->_simulation == AmmoShotSpread))
          pos = muzzle->_muzzleEnd;

        // apply proxy transformation
        // apply gun transformation
        return animTrans.FastTransform(pos);
      }
    }
    // check for hand grenades
    if (Type()->_handGrenadePoint>=0 && _shape->FindMemoryLevel()>=0)
    {
      const AmmoType *ammo = ptr.GetAmmoType();
      if (ammo && (ammo->_simulation == AmmoShotShell || ammo->_simulation == AmmoShotSmoke))
        return AnimatePoint(vs,_shape->FindMemoryLevel(),Type()->_handGrenadePoint);
    }
  }

  // not fired from any known weapon, assume camera position
  if (Type()->_pilotPoint >= 0 && _shape->FindMemoryLevel()>=0)
  {
    return AnimatePoint(vs, _shape->FindMemoryLevel(), Type()->_pilotPoint);
  }
  // as a last resort use the "zero" position
  return vs.cast<Man>().PositionWorldToModel(vs.Position());
}

#if _VBS3
Vector3 Man::GetLaserPoint(const TurretContext &context, int weapon) const
{
  if (weapon >= 0 && weapon < context._weapons->_magazineSlots.Size())
  {
    int weaponIndex = GetWeaponIndex(weapon);
    if (weaponIndex >= 0)
    {
      int proxyContainer = _shape->FindMemoryLevel();
      if (proxyContainer>=0)
      {
        SkeletonIndex boneIndex = GetType()->GetProxyBoneForLevel(proxyContainer,weaponIndex);
        const ProxyObjectTyped &proxy = Type()->GetProxyForLevel(proxyContainer,weaponIndex);
        Matrix4 animTrans = AnimateProxyMatrix(proxyContainer,proxy.GetTransform(),boneIndex);

        const MuzzleType *muzzle = context._weapons->_magazineSlots[weapon]._muzzle;
        Vector3Val pos = muzzle->_laserPos;

        // apply proxy transformation
        // apply gun transformation
        return animTrans.FastTransform(pos);
      }
    }
  }
  // missile is not fired from any known weapon
  // convert neutral gun position
  return PositionWorldToModel(AimingPosition());
}
#endif

bool Man::GetPalmMatrix(const ManVisualState &vs, Matrix4 &matrix) const
{
  int idx = Type()->_handsIndex;
  if(idx >= 0)
  {
    int proxyContainer = _shape->FindMemoryLevel();

      if (proxyContainer >= 0)
      {
        SkeletonIndex boneIndex = Type()->GetProxyBoneForLevel(proxyContainer, idx);
        const ProxyObjectTyped &proxy = Type()->GetProxyForLevel(proxyContainer, idx);
    
        matrix = AnimateProxyMatrix(vs, proxyContainer, proxy.GetTransform(), boneIndex);

        return true;
      }
  }
  return false;
}
bool Man::GetGunMatrix(const ManVisualState &vs, const TurretContext &context, const WeaponModePtr &ptr, Matrix4 &matrix) const
{
  if (ptr.IsValid())
  {
    int weaponIndex = GetWeaponIndex(ptr);
    if (weaponIndex >= 0)
    {
      int proxyContainer = _shape->FindMemoryLevel();

      if (proxyContainer >= 0)
      {
        SkeletonIndex boneIndex = Type()->GetProxyBoneForLevel(proxyContainer, weaponIndex);
        const ProxyObjectTyped &proxy = Type()->GetProxyForLevel(proxyContainer, weaponIndex);
    
        matrix = AnimateProxyMatrix(vs, proxyContainer, proxy.GetTransform(), boneIndex);

        return true;
      }
    }
  }

  return false;
}

bool Man::GetWeaponCartridgePos(const TurretContext &context, const WeaponModePtr &ptr, Matrix4 &pos, Vector3 &vel) const
{
  Assert(context._gunner == this);
  if (ptr.IsValid())
  {
    int weaponIndex = GetWeaponIndex(ptr);
    if (weaponIndex >= 0)
    {
      int proxyContainer = _shape->FindMemoryLevel();
      const ProxyObjectTyped &proxy = Type()->GetProxyForLevel(proxyContainer,weaponIndex);
      SkeletonIndex boneIndex = GetType()->GetProxyBoneForLevel(proxyContainer,weaponIndex);

      // apply proxy transformation
      Matrix4 animTrans = AnimateProxyMatrix(FutureVisualState(), _shape->FindMemoryLevel(),proxy.GetTransform(),boneIndex);

      const MuzzleType *muzzle = ptr.GetMuzzleType();

      if (muzzle->_cartridgeOutPosIndex<0 || muzzle->_cartridgeOutEndIndex<0)
        return false;

      Vector3Val cDir = muzzle->_muzzleDir;
      Vector3Val cPos = muzzle->_cartridgeOutPos;
      Vector3Val cVel = muzzle->_cartridgeOutVel;

      float randomSpd = GRandGen.RandomValue()*0.5f+0.75f;

      // create matrix for cartridge position
      Matrix4 trans;
      trans.SetDirectionAndUp(cDir,VUp);
      trans.SetPosition(cPos);
      // TODO: consider precalculation trans in MuzzleType

      pos = animTrans * trans;
      vel = animTrans.Rotate(cVel*randomSpd)+FutureVisualState().ModelSpeed();
      return true;
    }
    else
    {
      const WeaponType *type = ptr.GetWeaponType();
      RptF("Cannot find cartridge pos for %s, wrong weaponType", type ? cc_cast(type->GetName()) : "<NULL>");
    }
  }
  else
  {
    RptF("Cannot find cartridge pos, weapon index out of bounds");
  }
  // missile is not fired from any known weapon
  // do not process effects
  return false;
}

WeaponModePtr Man::GetCurrentHandWeapon() const
{
  // start with current weapon
  WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();

  // no laser when binocular or laser selected
  if (BinocularSelected()) return ptr;

  if (!ptr.IsValid()) return WeaponModePtr();

  const WeaponType *type = ptr.GetWeaponType();
  if (!type) return ptr;
  
  // check if current weapon is weapon in hands (primary, secondary or hand gun)
  if ( (type->_weaponType & MaskSlotPrimary) || 
       (type->_weaponType & MaskSlotSecondary) ||
       (type->_weaponType & MaskSlotHandGun) )
       return ptr;

  // if current weapon is none of those types, then look for primary weapon or hand gun
  int slot = MaskSlotHandGun | MaskSlotPrimary;

  _weaponsState.ForEachMuzzle([&ptr, slot](const WeaponModePtr &p) -> bool
  {
    const WeaponType *type = p.GetWeaponType();
    if (!type) return false; // continue
    if (type->_weaponType & slot)
    {
      ptr = p;
      return true; // done
    }
    return false; // continue
  });
  return ptr;
}

void Man::FireAttemptWhenNotPossible()
{
  InputInterface *input = GetNetworkManager().GetInputInterface(GetRemotePlayer());
  input->GetActionToDo(UADefaultAction); //this "fire" was already used, so mark it
  ActionMap *map = Type()->GetActionMap(_moves.GetPrimaryMove(FutureVisualState()._moves).id, AnimSubIx());
  MoveId moveWanted = Type()->GetMove(map, ManActFireNotPossible);
  if (moveWanted != MoveIdNone)
  {
    RaiseWeapon(); //14 seconds
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DEAnimation))
    {
      RStringB moveName = GetMovesType()->GetMoveName(moveWanted);
      _debugLastActionsInfo.AddInfo("FireNotPossible", moveName, "Fire");
    }
#endif
    _moves.AddForceMove(MotionPathItem(moveWanted,NULL), FutureVisualState()._moves);
  }
}

bool Man::Throw(Magazine *item)
{
  if (!IsActionInProgress(MFThrow))
  {
    ActionMap *map = Type()->GetActionMap(_moves.GetPrimaryMove(FutureVisualState()._moves).id, AnimSubIx());
    // get which action is it (based on action map)
    ManAction selAction = ManActThrowGrenade;
    MoveIdExt moveWanted = Type()->GetMove(map, selAction);
    if (moveWanted != MoveIdNone)
    {
      Ref<ActionContextThrow> context = new ActionContextThrow;
      context->function = MFThrow;
      context->_man = this;
      context->_move = moveWanted;
      context->_item = item;
#if _ENABLE_CHEATS
      if (CHECK_DIAG(DEAnimation))
      {
        RStringB moveName = GetMovesType()->GetMoveName(moveWanted);
        _debugLastActionsInfo.AddInfo("ThrowGrenade", moveName, "Throw");
      }
#endif
      _moves.AddForceMove(MotionPathItem(moveWanted, context), FutureVisualState()._moves);
      return true;
    }
  }
  return false;
}

bool Soldier::ProcessFireWeapon(const TurretContext &context, const WeaponModePtr &ptr, const Magazine *magazine, EntityAI *target,
  const RemoteFireWeaponInfo *remoteInfo, RemoteFireWeaponInfo *localInfo, bool forceLock)
{
  // if must be either local fire or remote effect processing
  Assert(localInfo || remoteInfo);
  const AmmoType *ammo = ptr.GetAmmoType();
  if (!ammo) return false;

  const MagazineType *aInfo = magazine ? magazine->Type() : NULL;
  Assert(aInfo);

  ProtectedVisualState<const VisualState> vs = RenderVisualStateScope(true);
  if (localInfo)
  {
    localInfo->_direction = VForward;
    localInfo->_position = vs->Position();
    localInfo->_visible=floatMax(ammo->visibleFire,ammo->audibleFire);
  }
  
  switch (ammo->_simulation)
  {
  case AmmoShotMissile:
    if (remoteInfo) return false;
    {
      if (GetActUpDegree(FutureVisualState()) != ManPosWeapon)
      {
        ActionMap *map = Type()->GetActionMap(_moves.GetPrimaryMove(FutureVisualState()._moves).id, AnimSubIx());
        MoveId moveWanted = Type()->GetMove(map, ManActWeaponOn);
#if _ENABLE_CHEATS
        if (CHECK_DIAG(DEAnimation))
        {
          RStringB moveName = GetMovesType()->GetMoveName(moveWanted);
          _debugLastActionsInfo.AddInfo("WeaponOn", moveName, "FireWeapon");
        }
#endif
        _moves.AddForceMove(MotionPathItem(moveWanted, NULL), FutureVisualState()._moves);
        return false;
      }
      if( !EnableMissile(FutureVisualState()) ) return false;
      // check relative missile direction
      // should be forward animated with GunTransform

      Vector3 dir = GetWeaponRelDirection(*vs, ptr, true);
      vs->DirectionModelToWorld(localInfo->_direction, dir);
      bool fired = FireMissile(context, ptr,
        GetWeaponPoint(*vs, context, ptr),
        localInfo->_direction,dir*aInfo->_initSpeed,
        target, true, false, forceLock);
#if PROTECTION_ENABLED
      CheckGenuine();
#if CHECK_FADE
      FILE *f = fopen("bin\\fade.txt","a");
      if (f)
      {
        fprintf(f,"Fade crash check %.2f\n",Glob.uiTime.toFloat());
        fclose(f);
      }
#endif
#endif
      return fired;
    }
  case AmmoShotStroke:
    return false;
  case AmmoShotShell:
  case AmmoShotSmoke:
  case AmmoShotIlluminating:
    if (remoteInfo) return false;
    {
      if( WeaponsDisabled() ) return false;

      if (DisableWeaponsLong(RenderVisualState()))
      {
        FireAttemptWhenNotPossible();
        return false;
      }
      RaiseWeapon(); //14 seconds
      // launched with GrenadeLauncher or Mortar
      //_gunClouds.Start(0.1f);
      Vector3Val pos = GetWeaponPoint(*vs,context, ptr);
      Vector3 dir = GetWeaponRelDirection(*vs, ptr, true);

      vs->DirectionModelToWorld(localInfo->_direction, dir);
      return FireShell(
        context,ptr,pos,localInfo->_direction,target, true, false
      );
    }
  case AmmoShotTimeBomb:
  case AmmoShotPipeBomb:
  case AmmoShotMine:
    if (remoteInfo) return false;
    {
      //if( WeaponsDisabled() ) return false;
      Vector3 pos = vs->Position() + 0.5f * vs->Direction() + VUp*0.5f;
      pos[1] = GLandscape->RoadSurfaceY(pos);
      localInfo->_direction = vs->Direction();
      bool fired = FireShell(context, ptr, PositionWorldToModel(pos), localInfo->_direction, target, true, false);
      if (fired && ammo->_simulation == AmmoShotPipeBomb)
      {
        _pipeBombs.Add(context._weapons->_lastShot);
        if (context._weapons->_forceFireWeapon != ptr)
          // if it is not a force fire, allow autonomous touching off
          _pipeBombsAuto = true;
      }
      return fired;
    }
  case AmmoShotBullet:
  case AmmoShotSpread:
    {
      if (remoteInfo)
      {
        Vector3Val pos = GetWeaponPoint(*vs, context, ptr);
        Vector3 dir = remoteInfo->_direction;
        return FireMGun(context, ptr, pos, dir, target, false, true);
      }
      if (DisableWeaponsLong(*FutureVisualStateScope(true)))
      {
        FireAttemptWhenNotPossible();
        return false;
      }
      RaiseWeapon(); //14 seconds
      if( WeaponsDisabled() ) return false;
      Vector3Val pos = GetWeaponPoint(*vs,context, ptr);
      Vector3 dir = GetWeaponRelDirection(*vs, ptr, true);

      if (QIsManual())
      {
        // In iron sights the camera and the muzzle have different origins.
        // Aim the projectile from the muzzle at the surface under the sight,
        // otherwise the two rays remain parallel and miss sideways nearby.
        if (GWorld->GetCameraType() == CamGunner && GWorld->CameraOn() == this &&
            GScene && GScene->GetCamera())
        {
          const Camera *camera = GScene->GetCamera();
          Vector3 cameraPos = camera->Position();
          Vector3 cameraDir = camera->Direction().Normalized();
          const float maxAimDistance = 500.0f;
          float aimDistance = GLandscape->IntersectWithGroundOrSea(
            NULL, cameraPos, cameraDir, 0, maxAimDistance);
          if (aimDistance <= 0 || aimDistance > maxAimDistance)
            aimDistance = maxAimDistance;

          CollisionBuffer hits;
          GLandscape->ObjectCollisionLine(GetRenderVisualStateAge(), hits,
            Landscape::FilterIgnoreOne(this), cameraPos,
            cameraPos + cameraDir * aimDistance, 0,
            ObjIntersectView, ObjIntersectFire, CF_NEAREST_CONTACT);
          Vector3 sightTarget = hits.Size() > 0 ? hits[0].pos :
            cameraPos + cameraDir * aimDistance;
          Vector3 muzzleWorld = vs->PositionModelToWorld(pos);
          Vector3 muzzleToTarget = sightTarget - muzzleWorld;
          if (muzzleToTarget.SquareSize() > 1.0f)
            dir = vs->DirectionWorldToModel(muzzleToTarget.Normalized());
        }
        // get fov from the current camera settings
        GWorld->GetCameraType();
        //float fov = GScene->GetCamera()->Left();
        float fov = GetCameraFOV();
        // let vehicle adjust the weapon depending on fov
        AdjustWeapon(ptr,fov,dir);
      }

      vs->DirectionModelToWorld(localInfo->_direction, dir);
      return FireMGun(context, ptr,pos,localInfo->_direction,target,false, false);
    }
  case AmmoNone:
    break;
  default:
    Fail("Unknown ammo used.");
    break;
  }
  return false;
}

bool Soldier::IsAimingDeadzoneEnabled()
{
  const InventoryItem* itemInHands = GetItemInHands();
  if( !itemInHands )
    return false;
  const InventoryItemType* itemType = itemInHands->Type();
  if( !itemType )
    return false;

  return !WeaponsDisabled() && !itemType->IsMelee();
}

bool Soldier::PrepareThrow(const TurretContext &context, const WeaponModePtr &ptr)
{
  // first find out if we are able to fire
  if( WeaponsDisabled() ) 
    return false;

  if (GetNetworkManager().IsControlsPaused())
    return false;

  if (!ptr.IsValid())
    return false;

  if (!IsAbleToFire(context))
    return false;

  if (_waterContact)
  {
    float maxFireDepth = IsDown() ? 0.2f : 0.9f;
    if (_waterDepth > maxFireDepth)
      return false;
  }

  const AmmoType *ammo = ptr.GetAmmoType();
  if (!ammo)
    return false;
  if( !GetWeaponLoaded(*context._weapons, ptr) )
  {
    return false;
  }

  // get action map for current move
  ActionMap *map = Type()->GetActionMap(_moves.GetPrimaryMove(FutureVisualState()._moves).id, AnimSubIx());
  if (!map)
    return false;

  // get move for prepare to throw
  ManAction selAction = ManActThrowPrepare;
  MoveId moveWanted = Type()->GetMove(map, selAction);

  if (MoveIdNone == moveWanted)
  {
    // move not found, lets try to find the move another way

    // first find the move for grenade throwing
    ManAction grenadeAction = ManActThrowGrenade;
    MoveId moveTarget = Type()->GetMove(map, grenadeAction);
    if (MoveIdNone == moveTarget)
      return false; //no throw grenade move

    // try to find move that connects to throwing the grenade (that should be our prepare move)
    moveWanted = GetMovesType()->FindConnectedSource(moveTarget);
  }

  if (MoveIdNone == moveWanted)
    return false; //move not found

  _moves.AddForceMove(MotionPathItem(moveWanted,0), FutureVisualState()._moves);
  return true;
}

/*!
\patch 1.27 Date 10/12/2001 by Ondra.
- Fixed: soldiers can no fire under water.
\patch 1.53 Date 4/26/2002 by Ondra
- Fixed: When satchel charge was placed under some walkable surface,
it appeared on top of it.
\patch 1.79 Date 7/26/2002 by Ondra
- New: MP: Player who is disconnected from server cannot fire.
\patch_internal 5101 Date 12/11/2006 by Ondra
- New: Ammo cheat anti-measures.
\patch 5115 Date 1/8/2007 by Ondra
- Fixed: Soldier movement stopped when firing in burst or auto mode or with an empty magazine.
*/
bool Soldier::FireWeapon(const TurretContextV &context, const WeaponModePtr &ptr, TargetType *target, bool forceLock)
{
  if (GetNetworkManager().IsControlsPaused())
    return false;
  if (!ptr.IsValid())
    return false;
  if (!IsAbleToFire(context))
    return false;

  if (ptr._weapon && ptr._weapon->IsDamageDestroyed())
  {
    PlayEmptyMagazineSound(context, ptr);
    RaiseWeapon(); //14 seconds
    return false;
  }

  if (_waterContact)
  {
    float maxFireDepth = IsDown() ? 0.2f : 0.9f;
    if (_waterDepth > maxFireDepth)
      return false;
  }
  if (BinocularSelected())
  {
    if (GWorld->CameraOn() == this)
      // "fire" with binocular caused reveal of observed target
      GWorld->UI()->RevealCursorTarget();
    return true;
  }

  const Magazine *magazine = ptr.GetMagazine();
  if (!magazine)
  {
    if (WeaponsDisabled())
    {
      RaiseWeapon(); //14 seconds
      return false;
    }
    //if (slot._weapon)
    //  LogF("Fire weapon %s",(const char *)slot._weapon->GetName());
    //if (slot._muzzle)
    //  LogF("Fire muzzle %s",(const char *)slot._muzzle->GetName());
    //if (slot._weaponMode)
    //  LogF("Fire mode %s",(const char *)slot._weaponMode->GetName());
    PlayEmptyMagazineSound(context, ptr);
    RaiseWeapon(); //14 seconds
    return false;
  }
  const AmmoType *ammo = ptr.GetAmmoType();
  if( !GetWeaponLoaded(*context._weapons, ptr) )
  {
    if (WeaponsDisabled())
    {
      RaiseWeapon(); //14 seconds
      return false;
    }
    if (DisableWeaponsLong(RenderVisualState()))
      FireAttemptWhenNotPossible();
    else
      RaiseWeapon(); //14 seconds

    bool playSound = false;
    if (ammo && ammo->_simulation != AmmoNone)
      playSound = magazine->GetAmmo() <= 0 || magazine->_reloadMagazine > 0 || IsActionInProgress(MFReload);
    if (playSound)
      PlayEmptyMagazineSound(context, ptr);
    return false;
  }
  RaiseWeapon(); //14 seconds

  RemoteFireWeaponInfo localInfo;
  bool fired = ProcessFireWeapon(context, ptr, magazine, target, NULL, &localInfo, forceLock);
  if (fired)
  {
    // validation is done inside of PostFireWeapon, but we need it here as well
    if (ptr.IsValid())
    {
      // check if ammo count has changed during PostFireWeapon
      // if not, activate some fade effects
      EncryptedInt ammoCountEnc;
      EncryptedIntSupport ammoCountSup; // not a real count, only an encrypted value
      SET_ENCRYPTED(ammoCountEnc,ammoCountSup,0);
      const Magazine *magazine = ptr.GetMagazine();
      if (magazine) ammoCountEnc = magazine->_ammo, ammoCountSup = magazine->_ammoSupport;

      bool ok = base::PostFireWeapon(context, ptr, target, localInfo);
      // when there is a magazine, we expect the function to return true
      // we want to avoid checking the return value, so that hackers cannot disable it
      (void)ok;
      Assert(ok == (magazine!=NULL));
      if (magazine)
      {
        // the magazine might have been changed meanwhile
        // if it was, the old one is now empty
        const Magazine *newMagazine = ptr.GetMagazine();
        // magazine has changed, no need to test ammo count - it has surely changed
        if (newMagazine==magazine && newMagazine->_ammo==ammoCountEnc && newMagazine->_ammoSupport==ammoCountSup)
          // we expected ammo count should change - we need to activate the countermeasures
          context._weapons->_lastShot->AmmoChangeExpectedFailed();
      }
    }
    return true;
  }
  return false;
}

float Soldier::GetRecoilFactor() const
{
  return GetAimPrecision(FutureVisualState());
}

float Soldier::GetCamShakeFireCoef() const 
{
  return GetCamShakeFire(FutureVisualState());
}

RStringB Soldier::GetRecoilName(const WeaponModeType *mode) const
{
  // check recoil factor; if low, we can assume supported stance
  if (GetAimPrecision(FutureVisualState()) < 0.4f)
  { // low recoil factor - assume supported stance
    // check onLand 
    float onLand = ValueTest(FutureVisualState(),&MoveInfoMan::OnLand);
    if (onLand>0.5f)
    {
      return mode->_recoilProneName;
    }
  }
  return mode->_recoilName;
}

void Soldier::ApplyRecoil(float impulseAngX, float impulseAngY, float offsetZ)
{
  static float factor = 20.0f;
  _gunXRecoil = impulseAngX*factor;
  _gunYRecoil = impulseAngY*factor;
  _gunZRecoil = offsetZ;
  // TODO: implement z-axis movement as well
}

void Soldier::OnAddImpulse(Vector3Par force, Vector3Par torque)
{
  // TODO: construct artificial recoil effect based on impulse
  // offset is based on force?
  // angle on torque?
  // currently we use effect as defined in CfgRecoils::impulse
  float factor = fabs(force.Z()*GetInvMass())*0.1f;
  if (factor>0.01f)
  {
    saturateMin(factor,1);

    static RStringB impulse = "impulse";
    //LogF("Impulse %.2f",factor);
    StartRecoil(impulse,factor*GetRecoilFactor());
  }
}

void Soldier::OnStress(Vector3Par pos, float hit)
{
  // stress is causing us to breathe heavily
  float dist2 = CameraPosition().Distance2(pos);
  
  // stress is proportional to distance
  float invDist = dist2>1 ? InvSqrt(dist2) : 1;

  static float hitScale = 0.05f;
  float personality = GetInvAbility(AKEndurance);
  float duty = invDist*hit*hitScale;
  _tired += duty*personality;
  saturate(_tired,0,1);
  
  // TODO: perform out-of-focus, or some black/white overlay effect as well
}

/*!
\patch 1.05 Date 7/18/2001 by Ondra.
- New: Weaker recoil when crouching.
\patch_internal 1.05 Date 7/18/2001 by Ondra.
- Changed: Recoil handling no longer supports separate recoil definition.
As recoil factor aimPrecision is used instead.
*/
bool Soldier::FireWeaponEffects(const TurretContext &context, const WeaponModePtr &ptr, const Magazine *magazine,EntityAI *target, const RemoteFireWeaponInfo *remoteInfo)
{
  const AmmoType *ammo = ValidateFireWeaponWithAmmo(ptr, magazine);
  if (!ammo) return false;

  const MagazineType *aInfo = magazine ? magazine->Type() : NULL;
  
  switch (ammo->_simulation)
  {
    case AmmoShotMissile:
      _triggerPullingImpulse = true;
    break;
    case AmmoShotShell:
    case AmmoShotSmoke:
    case AmmoShotIlluminating:
      if (aInfo->_initSpeed<30) break;
      _triggerPullingImpulse = true;
      context._weapons->_gunClouds.Start(0.1f);
    break;
    case AmmoShotBullet:
    case AmmoShotSpread:
    {
      _triggerPullingImpulse = true;
      const WeaponType *weapon = ptr.GetWeaponType();
      _mGunFireWeapon = unconst_cast(weapon);
      context._weapons->_mGunClouds.Start(0.1f);
      {
        float duration = 0.05f;
        float intensity = 0.012f;
        if (weapon)
        {
          duration = weapon->_fireLightDuration;
          intensity = weapon->_fireLightIntensity;
        }
        if (duration > 0 && intensity > 0) context._weapons->_mGunFire.Start(duration, intensity, true);
      }
      context._weapons->_mGunFireUntilFrame = Glob.frameID+1;
      context._weapons->_mGunFireTime = Glob.time;
      int newPhase;
      while ((newPhase = toIntFloor(GRandGen.RandomValue() * 3)) == context._weapons->_mGunFirePhase);
      context._weapons->_mGunFirePhase = newPhase;
      // TODO: recoil from weapon config
      // check FF params
    }
    break;
  }

  // camera shake effect
  if (Brain() && Brain() == GWorld->FocusOn())
  {
    GWorld->GetCameraShakeManager().AddSource(CameraShakeParams(CameraShakeSource::CSPlayerWeaponFire, FutureVisualState().Position(), ammo));
  }

  return base::FireWeaponEffects(context, ptr, magazine,target,remoteInfo);
}


/////////////////////////////////////////////////////
//
// Soldier implementation
//
/////////////////////////////////////////////////////

DEFINE_CASTING(Soldier)

Soldier::Soldier(const EntityAIType *name, bool fullCreate)
: base(name,fullCreate)
{
//  _pPhysics = NULL;
  _lastCurrentWeapon = _weaponsState.ValidatedCurrentWeapon();
  _freelook = false;
  _weaponRaisedState = _weaponTempRaisedState = _weaponGlobRaisedState = WeaponLower;
  _weaponTempRaisedActive = false;
  _triggerPullingCoef = 0.0f;
  _triggerPullingImpulse = false;
}

Soldier::~Soldier()
{
#if _ENABLE_INDEPENDENT_AGENTS
  if (_brain)
  {
    AITeamMember *teamMember = _brain->GetTeamMember();
    if (teamMember) GWorld->RemoveAgent(teamMember);
  }
#endif
}

static const float ObjPenalty[4][4]=
{
  {1.0f,1.05f,1.2f,1.5f}, // safe
  {1.0f, 0.7f,0.8f,1.2f}, // aware
  {1.2f, 0.8f,0.5f,1.2f}, // combat
  {1.2f, 0.8f,0.5f,1.2f} // stealth
};

#if _VBS3 //units "on foot" used roads too much in safe mode
  static const float RoadPenalty[4]={1.0f/1.2f,1.0f/1.2f,1.5f,1.5f};
#else
  static const float RoadPenalty[4]={1.0f/1.5f,1.0f/1.2f,1.5f,1.5f};
#endif
static const float ForestPenalty[4]={1.5f,1.2f,0.7f,0.5f};
static const float FullPenalty = 10.0f;

float ManType::GetMaxRemoteAngleCorrection() const
{
  static float maxAngleSpeedMan = 2.0f;
  return maxAngleSpeedMan;
}

float ManType::GetFieldCost( const GeographyInfo &info, CombatMode mode ) const
{
  int modeIndex = mode-CMSafe;
  saturate(modeIndex, 0, CMStealth-CMSafe);

  float cost=1;
  // road fields are expected to be slightly faster
  if (info.u.road)
  {
    cost*=RoadPenalty[modeIndex];
  }
  else
  {
    if (info.u.minWaterDepth>=2) return 1e6f;
    if (info.u.full) cost *= FullPenalty;
    static const float waterPenalty[4]={0,0.5f,10.0f,100.0f};
    cost += waterPenalty[info.u.maxWaterDepth]; // 1 m/s in deep water
  }

  if (info.u.forest)
  {
    cost *= ForestPenalty[modeIndex];
  }
  else
  {
    // fields with objects will be passed through slower
    int nObj=info.u.howManyObjects;
    Assert( nObj<=3 );
    cost *= ObjPenalty[modeIndex][nObj];
  }
  return cost;
}


/// bonus for moving in a cover
static const float ModeCoverBonus[CMStealth-CMCareless+1] = {1.0f,1.0f,0.75f,0.5f,0.4f};

float ManType::GetCostModeFactor(CombatMode mode) const
{
  int modeIndex = mode-CMCareless;
  saturate(modeIndex, 0, CMStealth-CMCareless);
  return ModeCoverBonus[modeIndex];
}

#define GRAD_0_SPEED 1.0f
#define GRAD_1_SPEED 1.0f
#define GRAD_2_SPEED 1.1f
#define GRAD_3_SPEED 1.2f
#define GRAD_4_SPEED 1.5f
#define GRAD_5_SPEED 2.0f
#define GRAD_6_SPEED 3.0f

static const float GradPenalty[7]=
{
  GRAD_0_SPEED,GRAD_1_SPEED,GRAD_2_SPEED,
  GRAD_3_SPEED,GRAD_4_SPEED,GRAD_5_SPEED,GRAD_6_SPEED
};
static const float InvGradPenalty[7]=
{
  1/GRAD_0_SPEED,1/GRAD_1_SPEED,1/GRAD_2_SPEED,
  1/GRAD_3_SPEED,1/GRAD_4_SPEED,1/GRAD_5_SPEED,1/GRAD_6_SPEED
};

inline float GradientPenalty( int grad )
{
  if (grad>=7) return 1e30f;
  return GradPenalty[grad];
}
inline float GradientMaxSpeed( int grad )
{
  if (grad>=7) return 0.1f;
  return InvGradPenalty[grad];
}

float ManType::GetBaseCost(const GeographyInfo &geogr, bool operative, bool includeGradient) const
{
  // avoid any deep water
  float cost=InvFastRunSpeed; // basic running speed
  if (geogr.u.road && operative)
  {
    // detect deep water everywhere
    // soldier can swim now, but with an extremely high cost
    if (geogr.u.minWaterDepth>=2) return 10.0f;
  }

  if (geogr.u.full) cost *= FullPenalty;

  // penalty for objects
  Assert( geogr.u.howManyObjects<=3 );
  //cost *= ObjPenalty[nObj];
  if (includeGradient)
  {
    // avoid steep hills
    // penalty for hills
    cost *= GradientPenalty(geogr.u.gradient);
  }
  // when there is some deep water, it is possible the water is blocking the field
  // we should better avoid it if possible
  if (geogr.u.road && operative)
  {
    static const float waterPenalty[4]={0,0.5f,10.0f,10.0f};
    cost += waterPenalty[geogr.u.maxWaterDepth]; // 1 m/s in deep water
  }
  return cost;
}

float ManType::GetGradientPenalty(float gradient) const
{
  // TODO: continuous function, sign of gradient
  gradient = fabs(gradient);

  if (gradient >= 0.95f) return 1e30f; // level 7
  else if (gradient >= 0.60f) return GRAD_6_SPEED; // level 6
  else if (gradient >= 0.40f) return GRAD_5_SPEED; // level 5
  else if (gradient >= 0.25f) return GRAD_4_SPEED; // level 4
  else if (gradient >= 0.15f) return GRAD_3_SPEED; // level 3
  else if (gradient >= 0.10f) return GRAD_2_SPEED; // level 2
  else if (gradient >= 0.05f) return GRAD_1_SPEED; // level 1
  return GRAD_0_SPEED; // level 0
}

float ManType::GetCostTurn(int difDir, int &debet, bool forceFullTurn) const
{
  // in sec
  if( difDir==0 ) return 0;
  // convert argument into -8..7 range (old direction representation)
  float aDir=fabs(difDir)*(float(15)/255);
  float cost=aDir*_costTurnCoef;
  if( difDir<0 ) return cost*0.8f;
  return cost;
}

FieldCost ManType::GetTypeCost(OperItemType type, CombatMode mode, Clearance clearance) const
{
  float clearanceFactor = clearance<ClearanceNormal ? _canCrawl ? 3.0f : SET_UNACCESSIBLE : 1.0f; // crawling is slower
  // consider using tables
  switch (type)
  {
  case OITRoad:
    // we prefer roads unless in combat , but we do not move any faster on them
    if (mode<CMCombat)
    {
      return FieldCost(0.9f,1.0f);
    }
    else
    {
      return FieldCost(10.0f,1.0f);
    }
  case OITRoadForced:
    if (mode<CMCombat)
    {
      return FieldCost(0.5f,1.0f);
    }
    else
    {
      return FieldCost(10.0f,1.0f);
    }
  case OITNormal:
  default:
    return FieldCost(clearanceFactor);
  case OITSpaceBush:
    // we avoid bush, but when moving through it, we do not need to slow down
    return FieldCost(2,1);
  case OITWater:
    // soldier can swim now, but with a high cost
    // we do not want to get wet, but when we swim, we are not that slow
    return FieldCost(1e6f,4.0f);
  case OITSpaceHardBush:
  case OITSpaceTree:
  case OITSpace:
  case OITSpaceRoad:
    return FieldCost(SET_UNACCESSIBLE);
  case OITAvoidBush:
  case OITAvoidTree:
  case OITAvoid:
    {
      Assert( mode>=CMCareless && mode<=CMStealth );
      // CMCareless, CMSafe, CMAware, CMCombat, CMStealth
      // we get a bonus, but that does not mean we move any faster
      return FieldCost(ModeCoverBonus[mode-CMCareless]*clearanceFactor,clearanceFactor);
    }
  }
  //return 1;
}

bool ManType::CheckEject(Vector3Par pos, float radius)
{
  int xMin = toIntFloor((pos.X()-radius) * InvLandGrid);
  int xMax = toIntFloor((pos.X()+radius) * InvLandGrid);
  int zMin = toIntFloor((pos.Z()-radius) * InvLandGrid);
  int zMax = toIntFloor((pos.Z()+radius) * InvLandGrid);
  
  for (int zz=zMin; zz<=zMax; zz++) for (int xx=xMin; xx<=xMax; xx++)
  {
    GeographyInfo geogr = GLandscape->GetGeography(xx,zz);
    if (geogr.u.minWaterDepth>1 || geogr.u.full && !geogr.u.forest) return false;
  }
  return true;
}


static float FlattenGrassDist = 1.00f;

/*!
  \patch 5104 Date 12/15/2006 by Ondra
  - New: Grass flattened around soldiers, improving visibility.
*/
float Man::IsGrassFlatenner(Vector3Par pos, float radius, Vector3 &flattenPos) const
{
  // we are interested only when we are withing the reach
  // add some reserve as a max. clutter radius
  const float maxClutterRadius = 2.0f;
  float maxDist = FlattenGrassDist + radius + maxClutterRadius;
  float dist2 = pos.Distance2(RenderVisualState()._aimingPositionWorld);
  if (dist2>Square(maxDist)) return 0;
  // TODO: each soldier is flattening some grass around him

  float down = ValueTest(RenderVisualState(),&MoveInfoMan::OnLand);
  if (down<=0) return 0;

  flattenPos = RenderVisualState()._aimingPositionWorld;
  return FlattenGrassDist+maxClutterRadius;
}

float Man::FlattenGrass(float &skewX, float &skewZ, Vector3Par pos, float radius) const
{
  skewX = skewZ = 0;

  const float maxDist = FlattenGrassDist;
  // distance where flattening is maximal
  const float minDist = FlattenGrassDist*0.5f;

  // distance where flattening is maximal
  const float centerDist = FlattenGrassDist*0.25f;
  
  // we know we are lying down
  // flatten some grass in a circle around us

  // calculate elliptic distance
  // consider point nearest to the player
  Vector3 offset = pos-RenderVisualState()._aimingPositionWorld;

  float dist2Round = offset.SquareSize();
  if (dist2Round>Square(radius))
  {
    if (dist2Round>Square(radius+maxDist)) return 0;
		//using  fast normalization, do not need exact solution here
    offset = offset - offset.NormalizedFast()*radius;
  }
  else
  {
    offset = VZero;
  }

  static float elliptic = 2.0f;
  float xOffset = FutureVisualState().DirectionAside()*offset*elliptic;
  float zOffset = FutureVisualState().Direction()*offset;

  float dist2 = xOffset*xOffset+zOffset*zOffset;
  // distance where flattening starts

  if (dist2>Square(maxDist)) return 0;

  float down = ValueTest(RenderVisualState(),&MoveInfoMan::OnLand);

  // TODO: once onLandBeg/onLandEnd is fixed, remove the hack
  //if (IsDown()) down = 1.0f;

  //static float maxFlattenSize = 0.2f;
  //const float maxFlatten = maxFlattenSize*down+(1-down);
  //const float maxFlatten = 1.0f;

  // calculate skew direction
  float skewXDir = offset.X();
  float skewZDir = offset.Z();

  float skewDirSize2 = skewXDir*skewXDir+skewZDir*skewZDir;
  float skewInvSize = skewDirSize2>0 ? InvSqrt(skewDirSize2) : 0;

  static float skewFactor = 5.0f;
  skewX = skewXDir*skewInvSize*down*skewFactor;
  skewZ = skewZDir*skewInvSize*down*skewFactor;

  float dist = dist2>0 ? dist2*InvSqrt(dist2) : 0;
  if (dist<centerDist)
  {
    // as center is closing, reduce skew to prevent singularities
    float toCenter = InterpolativC(dist,0,centerDist,0,1.0f);
    skewX *= toCenter;
    skewZ *= toCenter;
    return 1.0f;
  }

  float factor = InterpolativC(dist,minDist,maxDist,1.0f,0.0f);

  skewX *= factor;
  skewZ *= factor;

  return factor;
}


FSMEntityConditionFunc *ManType::CreateEntityConditionFunc(RString name)
{
  return base::CreateEntityConditionFunc(name);
}

FSMEntityActionFunc *ManType::CreateEntityActionInFunc(RString name)
{
  return base::CreateEntityActionInFunc(name);
}

FSMEntityActionFunc *ManType::CreateEntityActionOutFunc(RString name)
{
  return base::CreateEntityActionOutFunc(name);
}

/**
@param timeToAim Estimate how long will aiming in given direction take

\patch 5158 Date 5/16/2007 by Ondra
- Fixed: AI is now less likely to use RPG against infantry when having other weapons.
*/
float Soldier::FireInRange(const TurretContext &context, const WeaponModePtr &ptr, float &timeToAim, const Target &target) const
{
  timeToAim=0;
  if (ptr != context._weapons->ValidatedCurrentWeapon())
  {
    const WeaponType *weapon = ptr.GetWeaponType();
    int newType = weapon->_weaponType;
    int oldType = 0;
    WeaponModePtr oldPtr = context._weapons->ValidatedCurrentWeapon();
    if (oldPtr.IsValid())
    {
      oldType = oldPtr.GetWeaponType()->_weaponType;
    }
    else
    {
      // no weapon selected - assume based on current animation
      // this is needed because sometimes (e.g. on mission start) no weapon is actively selected,
      // still animation dictates using some weapon (most often rifle, but not necessarily)
      if (IsPrimaryWeaponInMove()) oldType = MaskSlotPrimary;
      else if (IsHandGunInMove()) oldType = MaskSlotHandGun;
      else if (GetActUpDegree(FutureVisualState())==ManPosWeapon) oldType = MaskSlotSecondary;
    }
    
    // switching between weapons can be expensive
    // assume switching mode takes some time
    timeToAim = 0.3f;
    // switching weapon which require different stance should be considered very expensive
    // TODO: handle other weapons as well
    
    int oldSpecial = newType&(MaskSlotHandGun|MaskSlotSecondary);
    int newSpecial = oldType&(MaskSlotHandGun|MaskSlotSecondary);
    // assume changing some weapon types (primary/secondary/handgun) is very slow
    if (oldSpecial!=newSpecial)
    {
      timeToAim = 1.5f;
    }
    else if (newType!=oldType)
    {
      // switching to a different weapon slot takes more time than switching mode
      timeToAim = 0.7f;
    }
  }

  // even using some weapons may be expensive
  // TODO: move this to weapon config
  const Magazine *magazine = ptr.GetMagazine();
  const MagazineType *magazineType = magazine ? magazine->Type() : NULL;
  const AmmoType *ammo = magazineType ? magazineType->_ammo : NULL;
  if (ammo)
  {
    switch (ammo->_simulation)
    {
      case AmmoShotRocket: case AmmoShotMissile:
        // using rockets is generally quite slow
        timeToAim += 2.5f;
        break;
      case AmmoShotShell:
      case AmmoShotSmoke:
        if (magazineType->_initSpeed<30)
        {
          // throwing hand grenade takes some time
          timeToAim += 1.0f;
        }
        break;
      case AmmoShotPipeBomb: case AmmoShotTimeBomb: case AmmoShotMine:
        // planting a bomb takes a long time
        timeToAim += 5.0f;
        break;
    }
  }
  
  Vector3 relDir=PositionWorldToModel(target.GetPosition());
  return FireAngleInRange(context, ptr, relDir);
}

void Soldier::UpdateRaiseWeapon(InputInterface *input)
{
#define OLD_RAISE_WEAPON 0
#if OLD_RAISE_WEAPON
  WeaponRaisedState manPosRS = (GetActUpDegree(FutureVisualState())<ManPosStand) ? WeaponRaised : WeaponLower;
  if (manPosRS != _weaponRaisedState) //for instance fire when weapon lowered can do that (raise playing animation)
  {
    _weaponRaisedState = manPosRS;
    _weaponTempRaisedState = _weaponGlobRaisedState = manPosRS;
  }
  if (GWorld->IsCameraActive(CamGunner)) 
  {
    _weaponTempRaisedState = _weaponGlobRaisedState = WeaponRaised;
  }
  else
  {
    //temporary action
    if (input->GetAction(UATempRaiseWeapon)) //key still pressed
    {
      if (!_weaponTempRaisedActive) _weaponTempRaisedState = ((_weaponGlobRaisedState==WeaponRaised) ? WeaponLower : WeaponRaised);
      _weaponTempRaisedActive = true;
    }
    else
    {
      _weaponTempRaisedActive = false; //active/inactive edge detected
      //restore the global state (which could have changed)
      _weaponTempRaisedState = _weaponGlobRaisedState;
    }
    //toggle action
    if (input->GetActionToDo(UAToggleRaiseWeapon)) //switch wanted
    {
      if (_weaponTempRaisedState == _weaponGlobRaisedState)
        _weaponTempRaisedState = ((_weaponTempRaisedState==WeaponRaised) ? WeaponLower : WeaponRaised);
      _weaponGlobRaisedState = _weaponTempRaisedState; //glob == temp
    }
  }
  //the active weapon state should always be the TEMP one
  if (_weaponTempRaisedState != _weaponRaisedState)
  {
    _weaponRaisedState = _weaponTempRaisedState;
    //do animations etc.
    // check current position
    if (GetActUpDegree(FutureVisualState())<ManPosStand) //raised
    {
      if (EnableWeaponManipulation())
      {
        if (!IsDown() || IsCollisionFreeMove(GetMove( CAST_TO_ENUM(ManAction, ManActStand)) ))
        {
          // deselect missile launcher if necessary
          if (LauncherAnimSelected())
          {
            UnselectLauncher();
          }
          //if (IsHandGunSelected()) SelectHandGun(false);
          PlayAction(CAST_TO_ENUM(ManAction, ManActStand));
        }
        else
        {
          PlayAction(CAST_TO_ENUM(ManAction, ManActCanNotMove));
        }
      }
    }
    else 
    {
      int upDeg = GetActUpDegree(FutureVisualState());
      if (upDeg==ManPosStand || upDeg==ManPosHandGunStand) //lower
      {
        PlayAction(CAST_TO_ENUM(ManAction, ManActCombat));
      }
    }
  }
#endif
}


void Soldier::SimulateAI( float deltaT, SimulationImportance prec )
{
  PROFILE_SCOPE_EX(siUSo,sim);
  if( !_isDead )
  {
    if (!_ladderBuilding)
    {
      if( IsControlled())
      {
        if (this==GWorld->GetRealPlayer() && GWorld->GetPlayerSuspended()) 
        {
          SuspendedPilot(deltaT,prec);
        }
        else 
        {
          KeyboardPilot(deltaT,prec);
        }
      }
      else if (!IsLocal()) FakePilot(deltaT,prec);
#if _ENABLE_AI
      else if ( Brain() )
      {
        if (Brain()->GetAIDisabled() & AIBrain::DAAnim)
          DisabledPilot(deltaT,prec);
        else
          AIPilot(deltaT,prec);
      }
#endif
      // we want to trace fire sectors for both AI and players
      TurretContextEx context;
      context._turret = NULL;
      context._turretType = NULL;
      context._gunner = this;
      context._weapons = &_weaponsState;
      context._parentTrans = FutureVisualState().Orientation();
      TraceFireSector(context);
    }
  }
  else
  {
    //ActionMap *map = Type()->GetActionMap(_primaryMove);
    // get which action is it (based on action map)
    //ManAction selAction = ManActDie;
    //MoveId moveWanted = map->GetAction(selAction);

    // some actions are edge sensitive - must be executed
    if( _moves.GetForceMove().id!=MoveIdNone )
    {
      SetMoveQueue(MotionPathItem(_moves.GetForceMove()),prec<=SimulateVisibleFar);
    }   
  }

  CombatMode cMode = GetCombatModeAutoDetected(Brain());

  if (cMode == CMCombat)
  {
    //
    // __asm nop;
  }

  base::SimulateAI(deltaT,prec);
}

void Soldier::UpdatePosition()
{
  TurretContext context;
  context._turret = NULL;
  context._turretType = NULL;
  context._gunner = this;
  context._weapons = &_weaponsState;

  WeaponModePtr ptr = GetCurrentHandWeapon(); 
  const ManVisualState &vs = RenderVisualState();

  if (!_isDead && HasWeaponFlashLight(ptr))
  {

    Matrix4 animTrans;
    if (GetGunMatrix(vs, context, ptr, animTrans))
    {
      const MuzzleType *muzzle = ptr.GetMuzzleType();

      // flash light position
      Vector3 lPos = animTrans.FastTransform(muzzle->_gunLightInfo->_position);
      lPos = vs.Position() + vs.DirectionModelToWorld(lPos);

      // flash light direction
      Vector3 lDir = animTrans.Rotate(muzzle->_gunLightInfo->_direction);
      lDir = vs.DirectionModelToWorld(lDir);

      CreateFlashLight(muzzle->_gunLightInfo);

      float deltaT = Glob.time.Diff(_lSimTime);
      _lSimTime = Glob.time;

      SimulateFlashLight(deltaT, lPos, lDir);
    }
  }
  else
  {
    DisableFlashLight();
  }

  if (!_isDead && HasWeaponIR(ptr))
  {
    Matrix4 animTrans;
    if (GetGunMatrix(vs, context, ptr, animTrans))
    {
      const MuzzleType *muzzle = ptr.GetMuzzleType();

      Vector3 lPos = animTrans.FastTransform(muzzle->_irLaserPos);

      // add fake distance hiding
      lPos = vs.Position() + Vector3(0.0f, (GWorld->FocusOn() != Brain()) ? -_coverGrassHeight : 0.0f, 0.0f) + vs.DirectionModelToWorld(lPos);

      Vector3 lDir = animTrans.Rotate(muzzle->_irLaserDir);
      lDir = vs.DirectionModelToWorld(lDir);
      lDir.Normalize();

      GScene->AddIRRay(this, true, lPos, lDir);
    }
  }
  else
  {
    IRLaserWanted(false);
  }

  // check all items recursively if some some reflector is attached
  if (HasReflectorAttached())
  {
    int level = _shape->FindMemoryLevel();
    if (level >= 0)
    {
      const ManType *type = Type();
      for (int i=0; i<type->GetProxyCountForLevel(level); i++ )
      {
        const ProxyObjectTyped &proxy = type->GetProxyForLevel(level, i);
        // check if proxy is attached to some inventory slot
        Object *proxyObj = proxy.obj;
        if (!proxyObj) continue;
        const EntityType *proxyType = proxyObj->GetEntityType();
        if (!proxyType) continue;
        InventorySlotId slot = proxyType->GetInventorySlot();
        if (slot < 0) continue;

        InventoryItem *item = NULL;
        if (slot == ProxyInventoryType::_hands)
          item = _itemInHands;
        else if (slot < NInventorySlotId)
        {
          item = _inventorySlots[slot];
          if (item == _itemInHands) item = NULL; // avoid drawing item in hands twice
      
          if (item == NULL) item = _emptySlots[slot]; // default for empty slot
        }
        
        if (item)
        {
          SkeletonIndex boneIndex = type->GetProxyBoneForLevel(level,i);
          const VisualState &vs = RenderVisualState();
          Matrix4 animTransform = AnimateProxyMatrix(vs, level, proxy.GetTransform(), boneIndex);
          // if shape is reversed, reverse orientation
          if (_shape->Remarks() & ShapeReversed)
          {
            Matrix4 swapM(MScale, -1, +1, -1);
            animTransform = animTransform * swapM;
          }

          PositionRender transform = vs.Transform() * animTransform;
          item->UpdateLights(transform);
        }
      }
    }
  }

  base::UpdatePosition();
}

void Soldier::Simulate( float deltaT, SimulationImportance prec )
{
  PROFILE_SCOPE_EX(simSo,sim);

  base::Simulate(deltaT,prec);

  // Simulate the trigger pulling
  {
    const float maxPullingState = 2.0f;
    const float pullingSpeed = 50.0f; // maxPullingState will be reached within 1.0/pullingSpeed of the second
    const float pullingOffSpeed = 10.0f; 
    if (_triggerPullingImpulse)
    {
      _triggerPullingCoef += pullingSpeed * deltaT;
      if (_triggerPullingCoef >= maxPullingState)
      {
        _triggerPullingImpulse = false;
        _triggerPullingCoef = maxPullingState;
      }
    }
    else
    {
      _triggerPullingCoef -= pullingOffSpeed * deltaT;
      saturateMax(_triggerPullingCoef, 0.0f);
    }
  }
}

LSError CoverVars::Serialize(ParamArchive &ar)
{
  CHECK(::Serialize(ar,"pos",_pos,1,VZero));
  CHECK(::Serialize(ar,"coverPos",_coverPos,1,MZero));
  CHECK(ar.Serialize("areaFreeLeft",_areaFreeLeft,1,0));
  CHECK(ar.Serialize("areaFreeRight",_areaFreeRight,1,0));
  CHECK(ar.Serialize("areaFreeBack",_areaFreeBack,1,0));
  CHECK(ar.SerializeEnum("type",_type,1,ENUM_CAST(CoverType,CoverNone)));
  CHECK(::Serialize(ar,"entry",_entry,1,VZero));
  return LSOK;
}

LSError Soldier::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))

  // TODO: default value for SerializeEnumValue
  // CHECK(ar.SerializeEnumValue("primaryMove", _primaryMove.id, 1, Type()->GetMoveIdNames() ))
  // CHECK(ar.SerializeEnumValue("secondaryMove", _secondaryMove.id, 1, Type()->GetMoveIdNames() ))

  if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
  {
  }
  return LSOK;
}

/*!
\patch 1.31 Date 11/23/2001 by Ondra
- Fixed: AI: Grenade launcher could be fired even when not aimed properly.
*/

float Man::GetAimed(const TurretContext &context, const WeaponModePtr &ptr, Target *target, bool exact, bool checkLockDelay) const
{
  // check if weapon is aimed
  if (!ptr.IsValid()) return 0;
  if( !target ) return 0;
  if( !target->idExact ) return 0;
  if( WeaponsDisabled() ) 
  {
    return 0;
  }
  Assert(context._gunner==this);
  
  AIBrain *unit = context._gunner->Brain();
  // check if we know exact target position

  const WeaponModeType *mode = ptr.GetWeaponModeType();

  const Magazine *magazine = ptr.GetMagazine();
  const MagazineType *aInfo = magazine ? magazine->Type() : NULL;
  const AmmoType *ammo = aInfo ? aInfo->_ammo : NULL;

  if (!exact && CheckSuppressiveFire()<SuppressYes)
  {
    if (!target->AccurateEnoughForHit(ammo,Brain()))
      return 0;
    // when not suppressing, wait until a recoil is ended
  }

  Vector3 speed;
  Vector3 ap;
  float seenBefore;
  float visible = GetAimingPosition(ap, speed, seenBefore, exact, context, target, ammo);

  if (!exact)
  {
    // even when suppressive fire is allowed, we require a visibility
    // in such case the visibility returned from GetAimingPosition is against the last seen position
    if( visible<0.3f ) return 0;
    if (CheckSuppressiveFire()<SuppressYes)
    {
      if (!target->SeenRecently(unit)) return 0;
    }
    else
    {
      float pauseWanted = SuppressiveFireInterval(seenBefore, context, GetSuppressiveFireBoost());
      // if pause was not long enough, wait some more
      if (pauseWanted>=FLT_MAX || context._weapons->_lastShotTime>Glob.time-pauseWanted)
        return 0;
    }
  }

  if (CheckSuppressiveFire()>=SuppressYes)
    // pretend at least partial visibility for purposes of aimed calculations
    visible = floatMax(visible,0.6);
  if (mode && ammo && (ammo->_simulation != AmmoShotMissile || ammo->maxControlRange<10))
  {
    switch (ammo->_simulation)
    {
      case AmmoShotTimeBomb:
      case AmmoShotMine:
      case AmmoShotPipeBomb:
      {
        // if we have already planted some bomb, do not plant any other until that one goes off
        // TODO: allow multiple bombs for multiple targets
        if (_pipeBombs.Size()>0) return false;
        // check if we are in good range
        float dist = ap.Distance(FutureVisualState().Position());
        float rad = target->idExact ? target->idExact->GetRadius()*0.5f : 5;
        float range = floatMin(mode->midRange*0.7f+mode->maxRange*0.3f+rad,rad*3);
        return (dist<range) * visible;
      }
    }
    
    // 0.6 visibility means 0.8 unaimed
    visible=1-(1-visible)*0.5f;

    #if 0 // would be more robust, but it presents quite a significant change, would require extensive testing
    if (ammo->_simulation == AmmoShotBullet)
    {
      float ret = CheckAimed(ap, speed, context, weapon, target, exact, ammo)*visible;
      return ret; // -ret;
    }
    #endif
    
    // predict shot result
    Vector3 wPos=FutureVisualState().PositionModelToWorld(GetWeaponPoint(FutureVisualState(),context, ptr));

    float time = 0;
    float balFactor = 1;
    float leadFactor = 1;
    float fall = 0;
    float dist=ap.Distance(wPos);
    if (ammo->_simulation==AmmoShotRocket || ammo->_simulation==AmmoShotMissile)
    {
      time = PreviewRocketBallistics(ap,wPos,aInfo,ammo,fall);
      if (time>=FLT_MAX) time = 0, fall = 0;
    }
    else
    {
      // TODO: consider using real ballistics preview
      float distFactor = dist<40 ? 0 : dist-40;
      float aimDist = dist + _aimInaccuracyDist*distFactor;
      saturateMax(aimDist,20);

      time = aimDist*aInfo->_invInitSpeed;
      
      fall = 0.5f*G_CONST*balFactor*time*time;
    }

    Vector3 leadSpeed = speed;
    LimitLead(leadSpeed,aInfo->_maxLeadSpeed*GetAbility(AKAimingAccuracy));
    Vector3 estPos=ap+leadSpeed*time*leadFactor;

    Vector3 wDir=GetWeaponDirection(FutureVisualState(), context, ptr);

    float eDist=estPos.Distance(wPos);

    Vector3 hit = wPos+wDir*eDist;
    hit[1] -= fall;
    // adapt hit to be in correct distance

    // distance should be eDist
    Vector3 norm = (hit-wPos);
    hit = norm.Normalized()*eDist+wPos;

#if _ENABLE_CHEATS
      float distHit = hit.Distance(wPos);
#endif

    Vector3 hError = hit-estPos;

    if (aInfo && (ammo->_simulation == AmmoShotShell) && (aInfo->_initSpeed >=30))
      // grenade launcher - mistake in y-axis may have fatal results
      hError[1] *= 4;
    float error = hError.Size();
    float precisionFactor = _aimRandom;
    float invAimingAccuracy = GetInvAbility(AKAimingAccuracy);
    float aimPrecision=(invAimingAccuracy-1)*1.5f*precisionFactor+1;
    saturateMax(aimPrecision,1);

    if (eDist<70)
    {
      // when target is very near, increase ability
      float distAcc = 1+floatMax(eDist*1.0f/70,0.2f);
      //LogF("aimPrecision %.3f, distAcc %.3f",aimPrecision,distAcc);
      saturateMin(aimPrecision,distAcc);
    }

    if (aInfo && ammo->_simulation == AmmoShotShell)
    {
      if(aInfo->_initSpeed < 30)
      {
        // hand grenade
        error *= 0.5f;
        // we are not able to throw grenade very precisely
        saturateMax(aimPrecision,5);
      }
      // grenade launcher
    }
    else if (ammo->_simulation==AmmoShotMissile)
      error *= 2;

    float tgtSizeRaw=target->idExact->AimingSize()*aimPrecision;
    const MuzzleType *muzzle = ptr.GetMuzzleType();
    float aiDispersionCoefX,aiDispersionCoefY;
    // for max. skill we want the aiDispersionCoef values to be always 1
    muzzle->GetDispersionCoefs(aiDispersionCoefX,aiDispersionCoefY,floatMax(invAimingAccuracy,1));
    
    float dCoef = floatMax(aiDispersionCoefX,aiDispersionCoefY);
    float tgtSize = tgtSizeRaw + dist*ptr.SumDispersionModifiers(this)*dCoef*aimPrecision*0.5f;

    // when aiming into the given area, we assume to be 100 % aimed
    // when outside of the area, we continuously reduce the factor
    const float outerAimed = tgtSize*1.2f;

#if _ENABLE_CHEATS
    if ((Object *)this==GWorld->CameraOn() && CHECK_DIAG(DECombat))
    {
      GlobalShowMessage(
        2000,
        "Error %.1f, tgtSize %.1f (%.1f), time %.2f, "
        "ePos %.1f,%.1f,%.1f, distErr %.1f",
        error,tgtSize,tgtSizeRaw,time,
        hError[0],hError[1],hError[2],
        distHit-eDist
      );
    }
#endif

    if (error>=outerAimed)
      return 0;
    if (!exact && !CheckFriendlyFire(context, ptr, target))
      return 0;
    float ret = error<=tgtSize ? visible : (1-(error-tgtSize)/(outerAimed-tgtSize))*visible;
    return ret;
    }
  else 
  {
    if (visible<0.5f)
    {
      return 0;
    }
    // 0.6 visibility means 0.8 unaimed
    visible=1-(1-visible)*0.5f;
    Vector3 relPos=ap-FutureVisualState().Position();
    // check if target is in front of us
    if( relPos.SquareSize()<=Square(30) ) 
    {
      return 0;
    }// missile fire impossible
    
    // check if target position is withing missile lock cone
    Vector3Val wDir=GetWeaponDirection(FutureVisualState(), context, ptr);

    float wepCos=relPos.CosAngle(wDir);
    float misAlignCoef  = 30;
    float wepAimed=1-(1-wepCos)*misAlignCoef;

    // unguided missile must be better aligned

    saturateMax(wepAimed,0);

#if _ENABLE_CHEATS
    if ((Object *)this==GWorld->CameraOn() && CHECK_DIAG(DECombat))
      GlobalShowMessage(2000, "AT visible %.3f, aimed %.3f",visible,wepAimed);
#endif


    if (!exact && !CheckFriendlyFire(context, ptr, target))
      return 0;
    return visible * wepAimed;
  }
}

WeaponModePtr Soldier::MissileIndex() const
{
  WeaponModePtr result;
  _weaponsState.ForEachMuzzle([&result](const WeaponModePtr &ptr) -> bool
  {
    const AmmoType *ammo = ptr.GetAmmoType();
    if (ammo && ammo->_simulation == AmmoShotMissile)
    {
      result = ptr;
      return true; // done
    }
    return false; // continue
  });
  return result;
}

void Soldier::OnNewTarget(Target * target, Time knownSince)
{
  // when we have discovered a new target, check it visually 
  // TODO: this is esp. important when target is not precise enough
  float delay = floatMin(knownSince-Glob.time,0.2f);
  if (_lookForwardTimeLeft>delay) _lookForwardTimeLeft = delay;
  base::OnNewTarget(target,knownSince);
}

#if _ENABLE_AI

void Soldier::AIFire( float deltaT )
{
  PROFILE_SCOPE_EX(manAF,sim);
#if _ENABLE_CHEATS
  extern bool disableUnitAI;
  if( disableUnitAI ) return;
#endif
  AIBrain *unit = Brain();
  if( !unit ) return;

  TurretContextV context;
  context._turret = NULL;
  context._turretType = NULL;
  context._turretVisualState = NULL;
  context._gunner = this;
  context._weapons = &_weaponsState;

  // periodically change between looking forward and on target
  if (_lookTargetTimeLeft>0)
  {
    _lookTargetTimeLeft -= deltaT;
  }
  else
  {
    _lookForwardTimeLeft -= deltaT;
    if (_lookForwardTimeLeft<=0)
    {
      if (BinocularAnimSelected())
      {
        // with binocular we want to look at the target quite long
        _lookTargetTimeLeft = 4+GRandGen.RandomValue()*4;
        _lookForwardTimeLeft = 30+GRandGen.RandomValue()*5;
      }
      else if (unit->GetCombatModeLowLevel()<=CMSafe || !(_itemInHands && _itemInHands->GetWeapon()))
      {
        if (fabs(_walkSpeedWantedZ)<0.1f)
        {
          // safe and static - almost no need to look forward
          _lookTargetTimeLeft = 1+GRandGen.RandomValue()*4;
          _lookForwardTimeLeft = 2+GRandGen.RandomValue()*3;
        }
        else
        {
          // safe, but walking - look forward often
          _lookTargetTimeLeft = 4+GRandGen.RandomValue()*8;
          _lookForwardTimeLeft = 3+GRandGen.RandomValue()*5;
        }
      }
      else if (fabs(_walkSpeedWantedZ)<0.1f)
      { 
        // combat, but static - look around often to keep situational awareness
        _lookTargetTimeLeft = 1.5f+GRandGen.RandomValue()*2;
        _lookForwardTimeLeft = 2.0f+GRandGen.RandomValue()*4;
      }
      else
      { 
        // combat and moving - look mostly forward, but watch the target often, but briefly
        _lookTargetTimeLeft = 0.5f+GRandGen.RandomValue()*0.5f;
        _lookForwardTimeLeft = 3.0f+GRandGen.RandomValue()*3.0f;
      }
    }

  }

  // keep gun in neutral position
  if (!_weaponsState._forceFireWeapon.IsValid())
  {
    _gunXRotWanted = 0;
    _gunYRotWanted = 0;
    // follow watch direction
    Vector3Val dir = unit->GetWatchHeadDirection();

    if (dir.SquareSize()>0.1f)
    {
      // check if we have any
      AimHeadAI(dir,NULL,deltaT);
    }
    else
    {
      _headXRotWanted = _headYRotWanted = 0;
      _lookXRotWanted = _lookYRotWanted = 0;
    }
  }
}

#endif //_ENABLE_AI

#define DIAG_FIRE 0

#if _ENABLE_AI

inline ManPos SaturatePos(int wantedPos, int minPos, int maxPos)
{
  saturate(wantedPos, minPos, maxPos);
  return (ManPos)wantedPos;
}

#define SATURATE_POS(wantedPos) SaturatePos(wantedPos, minPos, maxPos)

struct IsLadderAction
{
  bool found;
  IsLadderAction() {found = false;}
  bool operator () (const ActionContextBase *context);
};

bool IsLadderAction::operator () (const ActionContextBase *context)
{
  Assert(context);
  if (context->function != MFUIAction) return false; // continue
  const ActionContextUIAction *ctx = static_cast<const ActionContextUIAction *>(context);
  found = ctx->GetActionType() == ATLadderUp || ctx->GetActionType() == ATLadderDown;
  return found; // if found, return immediately
}

bool Man::IsLadderActionInProgress() const
{
  if (_ladderBuilding && _ladderIndex >= 0) return true;
  IsLadderAction func;
  ForEachAction(func);
  return func.found;
}

void Man::SetPosWanted(ManPos pos)
{
  _posWanted = pos;
  if (_posWantedTime>Glob.time) _posWantedTime=Glob.time;
}

DEFINE_ENUM(CoverType,Cover,COVER_TYPE_ENUM)

DEFINE_ENUM(CoverState,Cover,COVER_STATE_ENUM)

DEFINE_ENUM(Clearance, Clearance, CLEARANCE_TYPE_ENUM)

const float CoverHeightCorner[]={0.3f,1.0f,1.6f};
const float CoverHeightEdge[]={0.6,1.2f};

bool Man::StartUsingCover(CoverVars *cover, CoverState state)
{
  // if there is already some cover in use, we need to get out of it first
  if (_cover) return false;;
  _cover = cover;
  _coverState = state;
  return true;
}

bool Man::CheckMovementNeeded() const
{
  // when we are performing some cover manoeuvring, formation or forced speed must not stop us
  return _brain->GetPath().IsIntoCover();
}

bool Man::PlanChangePossible() const
{
  Path &path = _brain->GetPath();
  // if cover was not consumed, we cannot replan
  
  // until the path into a cover is completed and cover entry processed by FSM, we cannot allow replanning it
  if (!path.CheckCompleted() && path.IsIntoCover())
  {
    return false;
  }
  // when in cover, do not start planning until we are told to leave the cover
  if ((_cover || _inOpenCover) && _leaveCoverSemaphore<=0)
  {
    return false;
  }
  return true;
}

Vector3 Man::PositionToPlanFrom() const
{
  // when in cover, return cover entry
  if (_cover) return _cover->_entry;
  return FutureVisualState().Position();
}

static int FindCoverCollision(CollisionBuffer &col, float &minT)
{
  int minI = -1;
  minT = FLT_MAX;
  for (int i=0; i<col.Size(); i++)
  {
    const CollisionInfo &info = col[i];
    // we are currently unable to hide behind proxy objects
    // TODO: implement proxy hiding
    if (info.hierLevel>0) continue;
    if (info.geomLevel<0) continue;
    if (minT>info.under) minT = info.under, minI = i;
  }
  return minI;
}

// keep some distance to be able to stand or lie with a weapon there
const float DistanceToCoverStand = 1.0f;
const float DistanceToCoverProne = 1.3f;
// we need some side spacing to be able to peek easily but still be hidden
const float AsideToCoverLeft = 0.25f; // leaning left is very limited
const float AsideToCoverRight = 0.25f;
const float AsideToCoverProne = 0.30f;

const float AsideToCoverLeftOut = 0.05f; // leaning left is very limited
const float AsideToCoverRightOut = 0.20f;
const float AsideToCoverProneRightOut = -0.10f;
const float AsideToCoverProneLeftOut = -0.10f;

const float SpaceNeededProne = 0.8f;
const float SpaceNeededStand = 0.3f;

struct PosInCover
{
  float asideHidden;
  float asideOut;
  float distanceToCover;
  float spaceNeeded;

};
/**
Array elements:
0: 
*/
static const PosInCover DesiredPosInCover[]={
  {0,0,0,0},
  {+AsideToCoverRight,+AsideToCoverRightOut,DistanceToCoverStand,SpaceNeededStand}, // RightCornerHigh
  {+AsideToCoverRight,+AsideToCoverRightOut,DistanceToCoverStand,SpaceNeededStand}, // RightCornerMiddle
  {+AsideToCoverProne,+AsideToCoverProneRightOut,DistanceToCoverProne,SpaceNeededProne}, // RightCornerLow
  {-AsideToCoverLeft,-AsideToCoverLeftOut,DistanceToCoverStand,SpaceNeededStand}, // LeftCornerHigh
  {-AsideToCoverLeft,-AsideToCoverLeftOut,DistanceToCoverStand,SpaceNeededStand}, // LeftCornerMiddle
  {-AsideToCoverProne,-AsideToCoverProneLeftOut,DistanceToCoverProne,SpaceNeededProne}, // LeftCornerLow
  {0,0,DistanceToCoverStand,SpaceNeededStand}, // EdgeMiddle
  {0,0,DistanceToCoverProne,SpaceNeededProne}, // EdgeLow
  {0,0,DistanceToCoverProne,SpaceNeededProne}, // Wall
};
COMPILETIME_COMPARE(lenof(DesiredPosInCover),NCoverType);

bool Man::FindCover(CoverVars &coverVars, const OperCover &cover, Vector3Par tgtPos)
{
#if 0 // _ENABLE_CHEATS
  static Vector3 debugPos = Vector3(2883.37,0,2900.77);
  if (cover._pos.DistanceXZ2(debugPos)<Square(3))
  {
    __asm nop;
  }
#endif

  //Vector3 directionWanted = tgtPos-cover._pos;
  Matrix3 orient(MRotationY,-cover._heading); // orient.Direction() is out

  // based on prefered direction, choose a direction suitable for given cover
  // cover orientation was already determined while gathering candidates in AStarCover.GetBestCover (see ci._type = CoverRightCornerHigh)
  // in case of wall cover this is always out
  Vector3 direction;
  const float cosA = 0.7660444431189780352f; // cos 40 deg
  const float sinA = 0.64278760968653932632f; // sin 40 deg
  switch (cover._type.GetEnumValue())
  {
    case CoverLeftCornerHigh: case CoverLeftCornerMiddle: case CoverLeftCornerLow:
      direction = orient * Vector3(+cosA,0,-sinA);
      break;
    case CoverRightCornerHigh: case CoverRightCornerMiddle: case CoverRightCornerLow:
      direction = orient * Vector3(-cosA,0,-sinA);
      break;
    default:
      direction = -orient.Direction();
      break;
  }

  Vector3 posX = cover._pos;
  
  Vector3 coverAlong;
  
  float landY = GLandscape->RoadSurfaceY(posX);
  
  
  // pull slightly in to make sure first test find an intersection
  
  //float coverHeight = posX.Y()-landY;
  posX[1] = landY;

  direction.Normalize();
  // we are unable to cover against targets which are too low or too high
  if (fabs(direction.Y())>0.8f)
  {
    return false;
  }
  direction[1] = 0;
  direction.Normalize();

  coverVars._type = CoverNone;
  
  // find a suitable cover near us
  if (cover._type.operator CoverType() < CAST_TO_ENUM(CoverType, CoverEdgeMiddle))
  {
    // corner type cover
    // do not attempt to find another cover, this is already handled by cover candidate selection
    // if current cover is not suitable, just reject it
    Vector3 pos = posX - orient.Direction()*0.1f;
    pos[1] = landY+0.2f;

    CollisionBuffer col;
    // TODO: detect the cover very low above the ground
    const float firstRayLenght = 2.0f;
    // other rays should not collide - meaning there is open firing line
    const float otherRayLenght = 5.0f;

    GLandscape->ObjectCollisionLine(GetFutureVisualStateAge(),col,Landscape::FilterIgnoreOne(this),pos,pos+direction*firstRayLenght,0,ObjIntersectFire,ObjIntersectView);

      float minT;
    if (FindCoverCollision(col,minT)>=0)
    {
      Vector3 posO = cover._pos + orient.Direction()*0.1f;

      col.Resize(0);
      GLandscape->ObjectCollisionLine(GetFutureVisualStateAge(),col,Landscape::FilterIgnoreOne(this),posO,posO+direction*otherRayLenght,0,ObjIntersectFire,ObjIntersectView);

      if (FindCoverCollision(col,minT)<0)
        {
        if (CHECK_DIAG(DECombat) || CHECK_DIAG(DECostMap))
        {
          static int handle;
          GDiagsShown.ShowArrow(handle,0,100,Format("Col"),posO,direction,0.6f,Color(0.5,0.4,0));
        }
        
        coverVars._type = cover._type;
        coverVars._pos = cover._pos;
          
        // cover angle may be up to 120 degree, most often it is around 90 degree
        // i.e. free space can be up to 30 degree (most often 45 degree) turned left or right
        const float coverOpenAngle = H_PI*0.25f*0.95f; // almost 45 deg
        //const float coverOpenAngle = H_PI*(1.0f/6); // 30 deg
        bool right = cover._type.GetEnumValue()>=CoverRightCornerHigh && cover._type.GetEnumValue()<=CoverRightCornerLow;
        const float coverAlongAngle = cover._heading+(right ? +coverOpenAngle : -coverOpenAngle);

        coverAlong = -Matrix3(MRotationY,-coverAlongAngle).DirectionAside();

        // check how high above the soldier landcontact (not necessarily the ground - may be in the building) the cover pos is
        // check if it is high enough to allow for leaning
        // the cover is already scanned quite well for its height and general suitability, no need to repeat this here
        float coverHeightFireG = coverVars._pos.Y()-landY;
        float coverHeightCCand = cover._pos.Y()+OperCover::UpperGap()-landY;
        float coverHeight = floatMax(coverHeightCCand,coverHeightFireG);
        if (coverHeight>1.8f)
        {
          coverVars._pos[1] = landY + CoverHeightCorner[2];
            }
        else if (coverHeight>1.2f)
          {
          coverVars._pos[1] = landY + CoverHeightCorner[1];
          coverVars._type = (CoverType)(coverVars._type+1);
          }
        else if (coverHeight>0.4f)
        {
          coverVars._pos[1] = landY + CoverHeightCorner[0];
          coverVars._type = (CoverType)(coverVars._type+2);
            }
          else
          {
            // cover is too low even to lie behind it
            // TODO: detect and use horizontal edges
            coverVars._type = CoverNone;
          }
          }
      else
      {
        if (CHECK_DIAG(DECombat) || CHECK_DIAG(DECostMap))
        {
          Vector3 isect = col[0].pos;
          static int handle;
          GDiagsShown.ShowArrow(handle,0,100,Format("Col"),posO,(isect-posO).Normalized(),(isect-posO).Size(),Color(0.5,0,0));
          }

  }
    }

  }
  else if (cover._type.operator CoverType() <= CAST_TO_ENUM(CoverType, CoverEdgeLow))
  {
    // horizontal edge cover
    // we do not need to find any edges, we should only verify the cover provides a cover and allows firing
    
    // first check if there is any cover under the edge
    // we are checking for a cover slightly above the ground level
    // if there is no such cover, there is hardly anything we can do
    float expectedHeight = CoverHeightEdge[CAST_TO_ENUM(CoverType, CoverEdgeLow) - cover._type.operator CoverType()];
    
    Vector3 pos = posX;

    pos[1] = landY+expectedHeight-0.15f;
    
    CollisionBuffer col;
    GLandscape->ObjectCollisionLine(GetFutureVisualStateAge(),col,Landscape::FilterIgnoreOne(this),pos,pos+direction*3.0f,0,ObjIntersectFire,ObjIntersectView);
    float minT;
    int minI = FindCoverCollision(col,minT);
    // if there is a collision, verify edge top provide firing opportunity
    if (minI>=0)
    {
      col.Resize(0);
      pos[1] = landY+expectedHeight+0.25f;
      GLandscape->ObjectCollisionLine(GetFutureVisualStateAge(),col,Landscape::FilterIgnoreOne(this),pos,pos+direction*5.0f,0,ObjIntersectFire,ObjIntersectView);
      float minT;
      int minI = FindCoverCollision(col,minT);
      if (minI<0)
      {
        // we have confirmed the cover
        coverVars._type = cover._type;
        
        coverVars._pos = cover._pos;
        
        // we have a direction, but we need along
        // this is not hard to construct
        coverAlong = -Matrix3(MRotationY,-cover._heading).DirectionAside();
      }
    }    
  }
  else
  {
    // wall cover - no verification required?
    coverVars._type = cover._type;
    coverVars._pos = cover._pos;
    coverAlong = -Matrix3(MRotationY,-cover._heading).DirectionAside();
  }
  
  if (coverVars._type!=CoverNone)
  {
    // find a point on the ground level
    Vector3 coverPosDown = coverVars._pos;
    coverPosDown[1] = landY;
    // create a coordinate space
    Matrix4 coverPos(MTranslation,coverPosDown);
    coverPos.SetUpAndAside(VUp,coverAlong);
    FrameBase frame(coverPos);
    // assume we could use around 6x3 m around the cover if free
    Vector3 minmax[2]={Vector3(-3.0f,-0.5f,-3.0f),Vector3(+3.0f,2.0f,0)};

    // for horizontal edges we do not mind if we cannot be located a little bit further from the obstacle
    // but for corners we require precise positioning
    const float maxPreciseOffset = -0.20f;
    float allowedMinZ = coverVars._type>=CoverEdgeMiddle ? -0.50f : maxPreciseOffset;
    // now let us verify there is a free area around the cover and check how large it is
    GLandscape->CheckBoxFreeOfObjects(minmax,this,frame,true,allowedMinZ);
    
    if (
      // the minmax box must be nonempty
      minmax[0][0]>=minmax[1][0] || minmax[0][2]>=minmax[1][2] ||
      // the minmax box must contain the cover
      minmax[0][0]>=0 || minmax[1][0]<0
    )
    {
      return false;
    }
    // the minmax box must contain enough space required for manoeuvring in such cover type
    if (minmax[1][2]<maxPreciseOffset)
    {
      // when there is a significant offset, we move the assumed cover position
      float zOffset = maxPreciseOffset-minmax[1][2];
      
      // we need to move the position into the negative part of the minmax
      // this means moving it in the negative direction
      Vector3 posOffset = zOffset*coverPos.Direction();
      coverVars._pos -= posOffset;
      coverPos.SetPosition(coverPos.Position()-posOffset);
      
      minmax[0][2] += zOffset;
      minmax[1][2] += zOffset;
    }

    coverVars._areaFreeLeft = -minmax[0].X();
    coverVars._areaFreeRight = +minmax[1].X();
    coverVars._areaFreeBack = -minmax[0].Z();

    const float spaceNeeded = DesiredPosInCover[coverVars._type].spaceNeeded;

    const float xBorder = floatMax(0.3f,spaceNeeded);
    const float zBorderFront = floatMax(0.5f,spaceNeeded);
    const float zBorderBack = floatMax(0.3f,spaceNeeded);
    if(
      -coverVars._areaFreeLeft+xBorder>+coverVars._areaFreeRight-xBorder ||
      -coverVars._areaFreeBack+zBorderBack>+0-zBorderFront
    )
    {
      return false;
    }

    coverVars._coverPos = coverPos;

    return true;

  }
  return false;
}

bool Man::IsOccupyingCover(const CoverInfo &info) const
{
  if (_brain)
  {
    
    // if the unit has a path planned into the cover, consider the cover occupied
    // TODO: multiple units sharing the same cover
    if (_brain->GetPath().Size()>=2 && _brain->GetPath().GetCover())
    {
      float dist2 = _brain->GetPath().GetCover()._oper._pos.Distance2(info._oper._pos);
      if (dist2<Square(1.5f)) return true;
    }
  }
  if (_cover)
  {
    float dist2 = _cover->_pos.Distance2(info._oper._pos);
    if (dist2<Square(1.5f)) return true;
  }
  return false;
}


void Man::ReportMoving()
{
}

void Man::StopUsingCover()
{
  // initiate move out of the cover
  switch (_coverState)
  {
  case CoverMovingInDirect: // if we are just moving in, simply go back to entry
    _coverState = CoverMovingOut;
    break;
  case CoverInCover:// if we are in, start moving out
    // TODO: find a suitable out position
    _coverState = CoverMovingOut;
    
    break;
  case CoverMovingOut: // if we are moving out, do nothing
    break;
  }
}
bool Man::OnPathFoundToCover()
{
  Path &path = _brain->GetPath();
  // if the path is empty, no reaction required
  //if (path.Size()<2) return false;
  /// path which is already completed is no longer interesting
  if (path.CheckCompleted()) return false;
  // we need to leave the cover we are currently in
  if (_cover)
  {
    // while we are moving into some cover, path into another cover must not be found
    // while in cover, we wait for a FSM instructing us to leave it
    if (_leaveCoverSemaphore<=0) return false;
    StopUsingCover();
    ReportMoving();
    return false;
  }
  else if (_inOpenCover)
  {
    // as long as FSM will not allow us from the cover, we need to stay stopped
    if (_leaveCoverSemaphore<=0) return false;
    // the cover is open - all we need to do is to allow the movement
    ReportCoverLeft();
    ReportMoving();
    _inOpenCover = false;
    // suspend searching until search parameters are set
    _brain->SetPlanParametersSet(false);
  }
  
  return true;
}

void Man::ReportCoverLeft()
{
  Assert(_leaveCoverSemaphore>0);
  _leaveCoverSemaphore--;
}

void Man::LeaveCover()
{
  _leaveCoverSemaphore++;
//   if (CHECK_DIAG(DEFSM))
//   {
//     static int handle;
//     static int unique; // make unique so that each arrow stays displayed until timeout
//     GDiagsShown.ShowArrow(handle,unique++,60,"Cover Left",Position(),VUp,3,Color(0,1,1));
//   }
}

bool Man::CheckCoverEntered()
{
  // return true only once, resets itself
  bool val=_coverEntered>0;
  _coverEntered=-1;
  return val;
}

bool Man::CheckCoverLeft()
{
  // if we have already left the cover, the semaphore is back at zero
  return _leaveCoverSemaphore<=0;
}

Man::CoverStyle Man::IsInCover() const
{
  if (_cover) return InRealCover;
  if (_inOpenCover) return InOpenCover;
  return NotInCover;
}

float Man::GoingToCover() const
{
  if (IsInCover()>NotInCover) return 1.0f;
  // should not be called for a player - we cannot detect for him
  Assert(!QIsManual());
  Path &path = _brain->GetPath();
  if (path.CheckCompleted()) return 1.0f;
  
  float currCost = path.CostAtPos(FutureVisualState().Position());
  float totalCost = path.EndCost();
  if (totalCost<=0) return 1.0f;
  float going = currCost/totalCost;
  // never report as completed (1.0f) unless really completed
  return floatMin(going,0.99f);
}

CombatMode Man::GetCombatModeAutoDetected(const AIBrain*brain) const
{
  CombatMode autoCM = CMSafe;
  if (_upDegreeStable < ManPosNormalMin)
    autoCM = CMCombat;
  else if (_upDegreeStable <= ManPosLying)
    autoCM = CMCombat;
  else if (_upDegreeStable <= ManPosCombat)
    autoCM = CMAware;
  else if (_upDegreeStable < ManPosNormalMax)
    autoCM = CMSafe;
  return autoCM;
}

bool Man::CheckSupressiveFireRequired() const
{
  if (QIsManual())
  {
    // heuristics - does player need to be covered?
    // assume he needs to be covered whenever he is moving

    // check based on movement history
    return !IsStandingStill();
  }
  else
  {
    // whenever AI is not settled in a cover, cover it by suppressing enemies
    return !_cover || _coverState!=CoverInCover;
  }
}

void Man::StopPilot(AIBrain *unit, SteerInfo &info)
{
  Path &path = _brain->GetPath();
  if (_cover.IsNull() && !path.CheckCompleted())
  {
    if (CoverVars *cover = path.GetCover()._payload)
    {
      TrySwitchingToCover(cover);
    }
    else if (path.GetOpenCover())
    {
      // even when path is not into a cover, once we have completed it, we will not be moving any more
      // report we have completed the path and are in the cover spot
      path.SetCompleted();
      // report we have entered the cover
      SetCoverEntered();
      _inOpenCover = true;
      if (CHECK_DIAG(DEFSM))
      {
        static int handle;
        static int unique; // make unique so that each arrow stays displayed until timeout
        GDiagsShown.ShowArrow(handle,unique++,60,"Open Cover Enter",RenderVisualState().Position(),VUp,3,Color(0,0,1));
    }
  }
  }
  base::StopPilot(unit,info);
}

#if 0 //_ENABLE_CHEATS
#pragma optimize("",off)
#endif

/**
@param aligned out is soldier aligned with the cover orientation? (Useful to decide if leaning should be used)
  negative means we are using the cover in an opposite direction (left as right)
*/
float Man::AlignInCover(float &posHeadingWanted, CoverVars *cover, Vector3Par aimDir, float &aligned ) const
{
  float headingWanted = atan2(cover->_coverPos.Direction().X(),cover->_coverPos.Direction().Z());

  if (aimDir.SquareSize()>0.1f)
  {
    // because of aiming we may have to turn the body
    // max. aiming angle is Type()->_maxGunTurnAI
    float aimAngle = atan2(aimDir.X(),aimDir.Z());
    
    if (cover->_type<CoverEdgeMiddle)
    {
        // we need to prevent aiming "into" the cover
      //const float maxOffCover = H_PI*0.25f;
      //float minHeadingDiffL = -maxOffCover;
      //float maxHeadingDiffR = +maxOffCover;
      
      float maxDiffAllowed = Type()->_maxGunTurnAI*0.9f;
      float maxAimDiffL = -maxDiffAllowed;
      float maxAimDiffR = +maxDiffAllowed;
      float maxIntoCoverL = -H_PI*0.4f;
      float maxIntoCoverR = +H_PI*0.4f;
      // what is the "opposite" direction of the cover
      float directionOpposite = 0;
      // we need to avoid attempting maneuvering into the covering obstacle
      
      switch (cover->_type)
      {
        case CoverRightCornerHigh: case CoverRightCornerMiddle: case CoverRightCornerLow:
          //minHeadingDiffL = 0; // do not allow moving out of cover
          maxAimDiffL = 0; // do not allow aiming left, we need to move in such case
          maxIntoCoverR = -H_PI*0.1f; // avoid looking into the corner
          directionOpposite = -H_PI*0.5f;
          break;
        case CoverLeftCornerHigh: case CoverLeftCornerMiddle: case CoverLeftCornerLow:
          //maxHeadingDiffR = 0;
          maxAimDiffR = 0;
          maxIntoCoverL = +H_PI*0.1f; // avoid looking into the corner
          directionOpposite = +H_PI*0.5f;
          break;
      }

      if (CHECK_DIAG(DECombat))
      {
        Vector3 dirR = Matrix3(MRotationY,-maxIntoCoverR-headingWanted).Direction();
        Vector3 dirL = Matrix3(MRotationY,-maxIntoCoverL-headingWanted).Direction();
        GScene->DrawDiagArrow("HeadWnt",RenderVisualState().Position()+VUp*1.3,aimDir,0.9f,Color(0.7,0.7,0.7));
        GScene->DrawDiagArrow("L",RenderVisualState().Position()+VUp,dirR,0.9f,Color(1,1,0));
        GScene->DrawDiagArrow("R",RenderVisualState().Position()+VUp,dirL,0.9f,Color(1,0.5,0));
      }
      
      // aim by body only as much as necessary
      float aimDiff = AngleDifference(aimAngle,headingWanted);
      // the far the aiming direction is from the cover direction, the less we try to be aligned with it
      if (aimDiff<maxAimDiffL) aimDiff -= Interpolativ(aimDiff,-H_PI*0.5,maxAimDiffL,0,maxAimDiffL);
      else if (aimDiff>maxAimDiffR) aimDiff -= Interpolativ(aimDiff,maxAimDiffR,H_PI*0.5,maxAimDiffR,0);
      else aimDiff = 0;
      
      // adjust the heading to prevent turning body where aiming would be enough
      // maxIntoCoverR and maxIntoCoverL is relative to headingWanted
      // aimDiff is now also relative to it
      
      // adjust the headingWanted to be sure it is not aiming into the cover
      float inCoverCenter = (maxIntoCoverR+maxIntoCoverL)*0.5f;
      float inCoverDiff = AngleDifference(aimDiff,inCoverCenter);
      // opposite of saturate: make sure the value is out of given range
      // note: following code is very unstable around 0
      if (inCoverDiff>maxIntoCoverL-inCoverCenter && inCoverDiff<maxIntoCoverR-inCoverCenter)
      {
        // when around a switch-point (straight into the cover), we create an artificial hysteresis
        if (inCoverDiff>(maxIntoCoverL-inCoverCenter)*0.2f && inCoverDiff<(maxIntoCoverR-inCoverCenter)*0.2f)
        {
          // to make it stable we prefer to keep current heading
          float curHead = atan2(FutureVisualState().Direction().X(),FutureVisualState().Direction().Z());
          float curHeadDiff = AngleDifference(curHead,inCoverCenter+headingWanted);
          // based on current heading decide which side to turn to
          if (curHeadDiff<0) inCoverDiff = maxIntoCoverL-inCoverCenter;
          else inCoverDiff = maxIntoCoverR-inCoverCenter;
        }
        else
        {
          // we need to prevent aiming into the cover, but we are not in the hysteresis zone
          if (inCoverDiff<0) inCoverDiff = maxIntoCoverL-inCoverCenter;
          else inCoverDiff = maxIntoCoverR-inCoverCenter;
        }
        // adjust aimDiff
        aimDiff = inCoverCenter + inCoverDiff;
      }
      // modify headingWanted based on aimDiff

      headingWanted += aimDiff;
      

      // check how much is has sense to perform leaning
      aligned = InterpolativC(fabs(aimDiff),H_PI*0.25f,H_PI*0.4f,1,0);
      if (aligned<=0)
      {
        // when aimDiff points into the "opposite" direction of the cover, we want to perform inverted lean
        aligned = -InterpolativC(fabs(aimDiff-directionOpposite),H_PI*0.25f,H_PI*0.4f,1,0);
      }

      if (CHECK_DIAG(DECombat))
      {
        Vector3 dir = Matrix3(MRotationY,-headingWanted).Direction();
        Vector3 aimR = Matrix3(MRotationY,-maxAimDiffR-headingWanted).Direction();
        Vector3 aimL = Matrix3(MRotationY,-maxAimDiffL-headingWanted).Direction();
        GScene->DrawDiagArrow("HeadSat",RenderVisualState().Position()+VUp*1.4,dir,0.9f,Color(0.7,0.7,0.7));
        GScene->DrawDiagArrow("AimR",RenderVisualState().Position()+VUp*1.2,aimR,0.9f,Color(0.7,0.7,0.7));
        GScene->DrawDiagArrow("AimL",RenderVisualState().Position()+VUp*1.2,aimL,0.9f,Color(0.7,0.7,0.7));
      }
      posHeadingWanted = headingWanted;
    }
    else if (cover->_type<CoverWall)
    {
      // edge cover - any aiming is valid
      posHeadingWanted = headingWanted;
      headingWanted = aimAngle;
    }
    else
    {
      // wall - always stand at the fixed position, back to the wall
      posHeadingWanted = headingWanted;
      // aimDiff = aimAngle - headingWanted
      float aimDiff = AngleDifference(aimAngle,headingWanted);
      if (fabs(aimDiff)<H_PI*0.5)
      {
        // aimAngle = aimDiff + headingWanted
        aimAngle = fSign(aimDiff)*(H_PI-fabs(aimDiff)) + headingWanted;
      }
      headingWanted = aimAngle;
    }
  }
  return headingWanted;
}

void Man::ManeuverInCover(CoverVars *cover, Vector3Par pos, Vector3Par aimDir, SteerInfo &info) const
{
  float curHeading = atan2(FutureVisualState().Direction().X(),FutureVisualState().Direction().Z());
  float aligned = 0;
  float posHeadingWanted;
  float headingWanted = AlignInCover(posHeadingWanted,cover,aimDir,aligned);
  info.headChange = AngleDifference(headingWanted,curHeading);

  // in cover the body direction is dictated by the cover
  info.allowFreeTurn = false;

  // once we are aligned quite well, we will start moving
  if (fabs(info.headChange)<H_PI*0.5f)
  {
    /// world space desired position
    Vector3 posDif = PositionWorldToModel(pos);

    info.speedWanted = floatMinMax(posDif.Z()*1.5f,-1.2f,+3.5f);
    info.sideSpeedWanted = floatMinMax(posDif.X()*1.0f,-1.2f,+1.2f);
    if (fabs(info.speedWanted)<MinMoveSpeed) info.speedWanted = 0;
    if (fabs(info.sideSpeedWanted)<MinMoveSpeed) info.sideSpeedWanted = 0;

#if _ENABLE_REPORT
    // make sure we are not manoeuvring too far from the cover space
    const Matrix4 &coverPos =  cover->_coverPos;
    Matrix4 toCover(MInverseRotation,coverPos);
    Vector3 inCover = toCover.FastTransform(pos);
    const float border = OperItemGrid;
    if (
      -inCover.X()>cover->_areaFreeLeft+border || inCover.X()>cover->_areaFreeRight+border ||
      inCover.Z()>border || -inCover.Z()>cover->_areaFreeBack+border
    )
    {
      LogF(
        "%s: Out of cover area (%.2f,%.2f), %s",
        cc_cast(GetDebugName()),inCover.X(),inCover.Z(),
        cc_cast(FindEnumName(_coverState))
      );
    }
#endif


  }
  else
  {
    info.speedWanted = 0;
    info.sideSpeedWanted = 0;
  }
}

void Man::TrySwitchingToCover( CoverVars *cover )
{
  Path &path = _brain->GetPath();
  // we can complete the path only once
  // check what is our current position relative to selected cover
  Matrix4Val coverPos = cover->_coverPos;
  Matrix4 toCover(MInverseRotation,coverPos);

  // once we are inside of the maneuvering region, use cover maneuvering
  Vector3 relToCover = toCover.FastTransform(_landContactPos);

  // check if transition to cover is possible
  if (_coverState<CoverInCover)
  {
    if (
      relToCover.X()>-cover->_areaFreeLeft && relToCover.X()<cover->_areaFreeRight &&
      relToCover.Z()<0 && relToCover.Z()>-cover->_areaFreeBack && relToCover.Y()>-1.0f && relToCover.Y()<1.0f
    )
    {
      _cover = cover;
      _coverState = CoverInCover;
    }
    else if(
      _cover!=cover && !path.CheckCompleted() &&
       path.Size()>0 && path.End().Distance2(_landContactPos)<Square(OperItemGrid)
    )
    {
      // verify we are really close
      float distEntry2 = cover->_entry.Distance2(_landContactPos);
      if (distEntry2>Square(OperItemGrid*H_SQRT2)*2.0f)
      {
        // something is wrong - path tells us we are close, but entry is still far
        ErrF("%s: moving in direct condition failed, dist %g",cc_cast(GetDebugName()),sqrt(distEntry2));
        // however, to prevent deadlock, we resume based on what path is telling us
      }
      // we are not in the manoeuvring zone, however we are close to the end of the path
      // we want to align with the cover, and we want to reach the destination
      _cover = cover;
      _coverState = CoverMovingInDirect;
    }

    if (_cover == cover && !path.CheckCompleted())
    {
      // mark the path as completed
      path.SetCompleted();
      // report we have entered the cover
      SetCoverEntered();
      if (CHECK_DIAG(DEFSM))
      {
        static int handle;
        static int unique; // make unique so that each arrow stays displayed until timeout
        GDiagsShown.ShowArrow(handle,unique++,60,"Cover Enter",RenderVisualState().Position(),VUp,3,Color(0,0,1));
    }
  }
}
}

void Man::MoveInCover(CoverVars *cover, Vector3Par aimDir, SteerInfo &info )
{
  // use micro movement (strafing+walking) to move into the cover, peek as needed based on the cover type
  // micro movement is done by moving rather slow
  // make sure you are oriented as need
  float curHeading = atan2(FutureVisualState().Direction().X(),FutureVisualState().Direction().Z());
  float aligned = 0;
  float posHeadingWanted;
  float headingWanted = AlignInCover(posHeadingWanted,cover,aimDir,aligned);

  info.headChange = AngleDifference(headingWanted,curHeading);

  // in cover the body direction is dictated by the cover
  info.allowFreeTurn = false;

  // once we are aligned quite well, we will start moving
  if (fabs(info.headChange)<H_PI*0.5f)
  {
    // we adjust the position of the assumed coordinate space slightly depending on the direction adjusted by aiming
    // we need to respect cover limits while doing this, though
    Matrix3 coverOrient(MRotationY,-posHeadingWanted);
    Vector3Val coverDir = coverOrient.Direction();
    Vector3Val coverAside = coverOrient.DirectionAside();

    // when providing cover, we may need to move out of the cover slightly
    
    /// world space desired position
    const PosInCover &posC = DesiredPosInCover[cover->_type];
    
    // depending on tired (reflects stress level as well) randomize the covering out
    float stress = _tired*1.3f;
    // we are afraid of close covering fire lines as well
    float supCost = GLandscape->SuppressCache()->GetSuppressionLevel(AimingPosition(FutureVisualState()),3.0,_brain);
    // ignore small suppression values - most likely caused by friendlies
    stress += InterpolativC(supCost,300,1000,0,1);
    saturate(stress,0,1);
    
    bool needOut = false;
    // TODO: better randomization here, or some FSM control
    switch (CheckSuppressiveFire())
    {
      case SuppressIntense:
        needOut = _brain->RandomDecision(15.0f,1-stress*0.33f);
        break;
      case SuppressYes:
        needOut = _brain->RandomDecision(15.0f,1-stress);
        break;
      case SuppressAuto:
        needOut = _brain->RandomDecision(15.0f,0.5f*(1-stress));
        break;
    }
    
    float aside = needOut ? posC.asideOut : posC.asideHidden;
    Vector3 posDesired = cover->_pos - posC.distanceToCover*coverDir - aside*coverAside;
    // clamp to stay inside allowed maneuvering space
    Matrix4Val coverPos = cover->_coverPos;
    Matrix4 toCover(MInverseRotation,coverPos);
    Vector3 inCover = toCover.FastTransform(posDesired);

    if (CHECK_DIAG(DECombat))
    {
      if (stress>0 && this==GWorld->CameraOn())
      {
        DIAG_MESSAGE(500,Format("%s: stress %.2f",cc_cast(GetDebugName()),stress));
      }
      Vector3 posDesiredRaw = coverPos.FastTransform(inCover);
      GScene->DrawDiagArrow("Desired pos raw",posDesiredRaw,VUp,1.0f,Color(0,0,0.5));
    }
    
    // when prone we need more space then when standing
    const float spaceNeeded = posC.spaceNeeded;

    const float xBorder = floatMax(0.3f,spaceNeeded);
    const float zBorderFront = floatMax(0.5f,spaceNeeded);
    const float zBorderBack = floatMax(0.3f,spaceNeeded);
    // we assume some space is always left
    DoAssert(-cover->_areaFreeLeft+xBorder<=+cover->_areaFreeRight-xBorder);
    DoAssert(-cover->_areaFreeBack+zBorderBack<=+0-zBorderFront);
    // what if not?
    saturate(inCover[0],-cover->_areaFreeLeft+xBorder,+cover->_areaFreeRight-xBorder);
    saturate(inCover[2],-cover->_areaFreeBack+zBorderBack,+0-zBorderFront);
    
    posDesired = coverPos.FastTransform(inCover);
    // make sure the position is well withing the maneuvering space we have
    // when prone, we need more space then when standing
    
    if (CHECK_DIAG(DECombat))
    {
      GScene->DrawDiagArrow("Desired pos",posDesired,VUp,1.5f,Color(0,0,1));
    }

    Vector3 posDif = PositionWorldToModel(posDesired);

    info.speedWanted = floatMinMax(posDif.Z()*1.5f,-1.2f,+3.5f);
    info.sideSpeedWanted = floatMinMax(posDif.X()*1.0f,-1.2f,+1.2f);
    switch (cover->_type)
    {
    case CoverLeftCornerMiddle: case CoverRightCornerMiddle: case CoverEdgeMiddle:
      // unless the cover is very far, if we are behind a low cover,
      // avoid forcing the soldier to stand up because of running
      if (posDif.SquareSizeXZ()<Square(5))
      {
        //saturate(info.speedWanted,-0.6f,+0.6f);
        saturate(info.sideSpeedWanted,-0.6f,+0.6f);
      }
    }

    // avoid attempting too precise maneuvers - simulation does not handle them well, oscillation may happen
    if (fabs(info.speedWanted)<MinMoveSpeed) info.speedWanted = 0;
    if (fabs(info.sideSpeedWanted)<MinMoveSpeed) info.sideSpeedWanted = 0;
    
    if (info.speedWanted==0 && info.sideSpeedWanted==0)
    {
      // report we have reached the desired position
      _inFormation = true;
    }
    // as we are approaching the destination, start leaning, unless you are currently reloading
    if (fabs(aligned)>0 && needOut && !IsActionInProgress(MFReload))
    {
      // alignment may be negative - meaning we are using the cover in an opposite direction (left as right)
      // in such case we also want the leaning to be inverted
      float lean = floatMinMax(1-(posDif.SquareSizeXZ()-Square(0.7f))*0.5f,0,1)*aligned;
      switch (cover->_type)
      {
      case CoverRightCornerLow: case CoverRightCornerMiddle: case CoverRightCornerHigh:
        _leanZRotWanted = +lean;
        break;
      case CoverLeftCornerLow: case CoverLeftCornerMiddle: case CoverLeftCornerHigh:
        _leanZRotWanted = -lean;
        break;
      }
    }
  }
  else
  {
    info.speedWanted = 0;
    info.sideSpeedWanted = 0;
  }
}


#if _VBS3
float Man::GetAmmoInitSpeedFactor(const MagazineType *aInfo)
const
{
  float factor = 1.0f;
  if(aInfo->_initSpeed < 30.0f) //detect throw objects based on speed
  {
    if (GetActUp() <= ManPosHandGunLying)
      factor = 0.66f;
    else
      if (GetActUp() <= ManPosHandGunCrouch)
        factor =0.83f;
  }
  return factor;
}
#endif

bool Soldier::ValidatePoseChange(ActionMap *map, ManAction action) const
{
  if (!map) return true;
  MoveId currentStop = Type()->GetMove(map, ManActStop);
  MoveId  newMove = Type()->GetMove(map, action);
  ActionMap *newMap = Type()->GetActionMap(newMove, AnimSubIx());
  if (!newMap) return true; // no map, stay safe, assume change has sense
  // if (newMap!=map) return true; // too strong - leads to a "froggy" movement for a crouched civilian
  if (Type()->GetMove(newMap,ManActStop).operator MoveId()!=currentStop) // a different standing animation - change has sense
  {
    return true;
  }
  if (map->GetUpDegree()!=newMap->GetUpDegree())
  {
    return true;
    // a different up degree - change has sense
  }
  return false;
}

/*!
\patch 1.01 Date 6/26/2001 by Ondra.
- Fixed: Weapon (AT missile) activation when ordered to stand-up or lay down.
\patch 1.05 Date 7/17/2001 by Ondra.
- New: AI uses walking with riffle animation when appropriate.
\patch 1.50 Date 4/11/2002 by Ondra
- Fixed: Minor visual bug in AI soldier turning animation interpolation.
\patch 1.50 Date 4/13/2002 by Ondra
- New: AI uses crouch position in some situations.
\patch 5123 Date 1/29/2007 by Ondra
- Fixed: AI usage of laser designators much improved.
*/

void Soldier::AIPilot(float deltaT, SimulationImportance prec)
{
  //PROFILE_SCOPE_EX(manAI,sim);
  _holdBreath = 0; // AI is currently never holding breath
  _leanZRotWanted = 0;
  _headXRotWantedCont = _headYRotWantedCont = _headXRotWantedPan = _headYRotWantedPan = 0;
  _irWanted = _irEnabled;
  _flir = false;

  if (GWorld->CameraOn()==this)
  { // if camera is attached, adjust zoom settings depending on current weapon / binoculars
    Object::Zoom *zoom = GetZoom();
    if (zoom) 
    {
      //CameraType camType = GWorld->GetCameraType();
      zoom->_zoomInsteadOfOptics = false;
      RenderVisualStateScope(true),zoom->Update(&GInput,deltaT,this); // Zoom update may call RenderVisualState to check if optics are enabled
    }
  }


  if (IsSuspendedByActivity())
  {
    // In get in action soldier must not move or turn, just play animation
    _turnWanted = 0;
    return;
  }

#if _ENABLE_CHEATS
  if (this==GWorld->CameraOn())
  {
    // enable placing breakpoint at camera vehicle AI
    __asm nop;
  }
  extern bool disableUnitAI;
  if( disableUnitAI ) return;
#endif

  SteerInfo info;
  info.headChange=0;
  info.speedWanted=0;
  info.turnPredict=0;

  // if we are near the target we have to operate more precisely
  // aim to current target
  if( !_isDead ) AIFire(deltaT);

  // TODO: limit AIPilot simulation rate (10 ps)
  AIBrain *unit = Brain();
  if (!unit) return;

  bool inPosition = true;
  
  if( unit->GetState()==AIBrain::Stopping )
  {
    unit->OnStepCompleted();
    // unit can be destroyed in unit->SendAnswer
    // unit = Brain();
    info.speedWanted=0;
    return;
  }
  else if( unit->GetState()==AIBrain::Stopped )
  {
    info.speedWanted=0;
  }
  else if (_cover && _coverState==CoverMovingOut)
  {
    // before moving normally we have to move out of the cover
    // maneuver back to _entry
    // note: once at _entry, we want to start following the path
    // therefore while moving, we probably want to aling with it
    // for this is would be good to know starting direction of the path
    ManeuverInCover(_cover,_cover->_entry,VZero,info);
    // once there, start moving normally
    if (
      info.speedWanted==0 && info.sideSpeedWanted==0
      // all we need is to be close enough, stopping is too strict condition
      || _landContactPos.Distance2(_cover->_entry)<Square(OperItemGrid*0.5f)
    )
    {
      _cover.Free();
      _coverState = CoverNo;
      ReportCoverLeft();
    }
    _inFormation = false;
  }
  else
  {
    _fireState=FireDone;
    if (!_cover)
    {
      const Path &path = unit->GetPath();
      CoverVars *cover = path.GetCover()._payload;
      if (cover)
      {
        TrySwitchingToCover(cover);
      }
    }
    LeaderPilot(info);
    // leader is always assumed to be in position
    inPosition = true;
    const Path &path = unit->GetPath();
    if (_cover)
    {
      if (_coverState>=CoverInCover)
      {
        // FormationPilot makes sure wanted position is set as needed
        // MoveInCover performs in-cover maneuvering
        // GetWatchDirection respects fire sector dictated by a formation
        MoveInCover(_cover,GetTargetAimDir(_brain->GetWatchDirection(),true),info);
      }
      else if (_coverState==CoverMovingInDirect)
      {
        ManeuverInCover(_cover,_cover->_entry,GetTargetAimDir(_brain->GetWatchDirection(),true), info);
        // once we are well in cover, we need to switch to it
        TrySwitchingToCover(_cover);
      }
    }
    // if there is some path, check if it uses building
    // if yes, mark it
    // TODO: check current position
    if (path.Size()>0)
    {
      bool building = false;
      for (int i=0; i<path.Size(); i++)
      {
        if (path[i]._house) {building = true;break;}
      }
      _inBuilding = building;
    }
  }

  if ((!_landContact || _freeFall) && _freeFallStartTime<Glob.time-0.6f && !_swimming)
  {
    info.speedWanted = 0;
    info.headChange = 0;
  }
  if (info.action)
  {
    switch (info.action->GetType())
    {
    case PATLadderTop:
      {
        if (!IsLadderActionInProgress())
        {
          Ref<ActionContextBase> context = CreateActionContextUIAction(new ActionLadderPos(
            ATLadderDown, dyn_cast<EntityAI, Object>(info.building),
            info.action->GetLadderIndex(), 1), ManActLadderOnDown);
          PlayAction(CAST_TO_ENUM(ManAction, ManActLadderOnDown), context);
          unit->RemoveAction(info.action); // activate action once
        }
      }
      return;
    case PATLadderBottom:
      {
        if (!IsLadderActionInProgress())
        {
          Ref<ActionContextBase> context = CreateActionContextUIAction(new ActionLadderPos(
            ATLadderUp, dyn_cast<EntityAI, Object>(info.building),
            info.action->GetLadderIndex(), 0), ManActLadderOnUp);
          PlayAction(CAST_TO_ENUM(ManAction, ManActLadderOnUp), context);
          unit->RemoveAction(info.action); // activate action once
        }
      }
      return;
    case PATUserActionBegin:
    case PATUserActionEnd:
      {
        EntityAI *building = dyn_cast<EntityAI, Object>(info.building);
        RString actionName = info.action->GetActionName();
        if (building && actionName.GetLength() > 0)
        {
          building->ProcessUserAction(actionName);
          if (!building->DoneUserAction(actionName))
          {
            // wait until action is processed
            info.headChange = 0;
            info.speedWanted = 0;
            info.turnPredict = 0;
          }
        }
      }
      break;
    }
  }

  // if we are getting close to a cover, we need to avoid sprinting
  // this is because stopping from a sprint can take very long
  // the same is true when end of the path is closing
  if (_brain->GetPath().Size()>=2 && _brain->GetPath().IsIntoCover())
  {
    float coverDist2 = FutureVisualState().Position().Distance2(_brain->GetPath().End());
    if (coverDist2<Square(10))
    {
      saturate(info.speedWanted,-5,+5);
      saturate(info.sideSpeedWanted,-5,+5);
      // if the cover is an open one, we want to stop in it
      // slow down to avoid overrunning it
      if (_brain->GetPath().GetOpenCover() && coverDist2<Square(2))
      {
        saturate(info.speedWanted,-1,+1);
        saturate(info.sideSpeedWanted,-1,+1);
        if (_brain->GetPath().GetOpenCover() && coverDist2<Square(0.5f))
        {
          info.speedWanted = info.sideSpeedWanted = 0;
        }
      }
      
    }
  }

  //if (goBack) speedWanted=-speedWanted;
  AvoidCollision(deltaT,info.speedWanted,info.headChange);

  {
    // slow down when turning
    float maxSpeed=Type()->GetMaxSpeedMs();
    float limitSpeed=InterpolativC(fabs(info.headChange),0,H_PI*0.5f,maxSpeed,0);
    saturate(info.speedWanted,-limitSpeed,+limitSpeed);

    // when in buildings, slow down even more soon enough
    if (info.building)
    {
      float limitSpeed=InterpolativC(fabs(info.turnPredict), 0, H_PI*0.15f, maxSpeed, 0.1f*maxSpeed);
      saturate(info.speedWanted,-limitSpeed,+limitSpeed);

      if (CHECK_DIAG(DEPath) && this==GWorld->CameraOn())
        DIAG_MESSAGE(500, Format("Speed in building limited by %.2f to %.2f", limitSpeed, info.speedWanted));
    }

    // slow down if carrying heavy weight
    limitSpeed = (1.2f - GetEncumbrance()) * maxSpeed;
    saturate(info.speedWanted,-limitSpeed,+limitSpeed);
  }

  // make appropriate reaction based on speedWanted, headChange and wantedPosition

  const ManType *type = Type();
  _turnWanted = info.headChange*3;
  float maxTurnAngVelocity = type->GetMaxTurnAngularVelocity(); 
  saturate(_turnWanted,-maxTurnAngVelocity,+maxTurnAngVelocity);

  // if movement is necessary, stand up

  ManPos minPos = ManPosLying; // default is: we can be in any position
  ManPos maxPos = ManPosStand;

  // convert from minPos,maxPos to old scheme
  CombatMode unitMode = unit->GetCombatModeLowLevel();
  // when player is a group leader, follow his lead
  CombatMode autoCM = CMSafe;
  
  // show laser target in combat mode
  if (_irEnabled)
  {
    _irWanted = GScene->MainLight()->NightEffect()>0 && unitMode == CMCombat;
  }

  // use gun light in CM or if forced
  _lightWanted = GScene->MainLight()->NightEffect()>0 && unitMode == CMCombat && !_hasNVG || _lightForced;

  // select the stricter mode (more combat, meaning more prone)
  CombatMode stanceMode = CombatMode(autoCM>unitMode ? autoCM : unitMode);
  
  float delay = 0.5f; 

  if (IsLaunchDown())
  {
    // we have missile ready - wait with it for a while
    delay = 3;
  }

  if (info.clearance<ClearanceNormal && Type()->_canCrawl)
  {
    // we need to go prone - low clearance ahead
    maxPos = SATURATE_POS(ManPosLying);
    delay = 0;
  }
  // FIX weapon activation before _unitPos check
  else if (unit->GetPlanningMode() == AIBrain::LeaderDirect
#if _VBS3  // disableAI PATHPLAN
    && (unit->GetAIDisabled() & AIBrain::DAPathPlan) == 0
#endif
  )
  {
    minPos = ManPosCombat; // disable lying - force run
  }
  else if
  (
    LauncherAnimSelected() && LauncherReady() /*&& fabs(info.speedWanted)<0.5f*/ &&
    stanceMode>=CMCombat && LauncherWanted()
  )
  {
    // check if we should have launcher prepared
    delay = 2;
    // then delay before giving it up
    minPos = ManPosWeapon;
    maxPos = ManPosWeapon;
  }
  else
  {
    // restriction given by _unitPos
    UnitPosition unitPos = GetUnitPosition();
    switch (unitPos)
    {
    case UPUp:
      minPos = ManPosCombat;
      break;
    case UPMiddle:
      minPos = ManPosCrouch;
      maxPos = ManPosCrouch;
      break;
    case UPDown:
      maxPos = ManPosLying;
      break;
    }

    
    if (_cover && _coverState>=CoverMovingInDirect && _coverState<=CoverInCover)
    {
      // we should not force prone/crouched until we are really close
      if (fabs(info.speedWanted)<2.0f && fabs(info.sideSpeedWanted)<1.0f)
      {
        // cover type may dictate a position - should we ignore unitPos here?
        switch (_cover->_type)
        {
        case CoverNone:
          break;
        case CoverRightCornerHigh: case CoverLeftCornerHigh:
          minPos = ManPosCrouch; maxPos = ManPosCombat;
          break;
        case CoverRightCornerMiddle: case CoverLeftCornerMiddle:
          minPos = maxPos = ManPosCrouch;
          break;
        case CoverRightCornerLow: case CoverLeftCornerLow:
          minPos = maxPos = ManPosLying;
          break;
        case CoverEdgeLow:
          if (CheckSuppressiveFire()>SuppressNo) minPos = maxPos = ManPosCrouch;
          else minPos = maxPos = ManPosLying;
          break;
        case CoverEdgeMiddle:
          if (CheckSuppressiveFire()>SuppressNo) minPos = maxPos = ManPosCombat;
          else minPos = maxPos = ManPosCrouch;
          break;
        case CoverWall:
          // no suppressive fire possible here
          minPos = ManPosCrouch; maxPos = ManPosCombat;
          break;
        }
      }
      else
      {
        minPos = ManPosCrouch; maxPos = ManPosCombat;
      }
    }
    else if (unit->IsDanger() && !_inBuilding)
    {
      maxPos = SATURATE_POS(ManPosLying); // force lying
    }
    else if( unit->GetPath().IsIntoCover())
    {
      // while moving into the cover we always want to move as fast as possible
      // do not allow going prone
      // TODO: consider a special case when cover is very near, esp. when the cover is low (allow prone)
      minPos = SATURATE_POS(ManPosCombat);
      maxPos = SATURATE_POS(ManPosCombat);
    }
    else if( stanceMode == CMAware )
    {
      minPos = maxPos = SATURATE_POS(ManPosCombat);
    }
    else if( stanceMode <= CMSafe )
    {
      minPos = maxPos = SATURATE_POS(ManPosStand);
    }
    else if( stanceMode == CMCombat )
    {
      // check speed wanted
      float limitSpeed = type->_lyingLimitSpeedCombat;
      // check if our position corresponds to the position we should have in the formation
      // if not, never lie down
      if (info.speedWanted<limitSpeed && !_inBuilding)
      {
        // if we are not lying yet, consider crouching
        if (!IsDown() && (!inPosition || unit->RandomDecision(30,type->_crouchProbabilityCombat)))
        {
          maxPos = SATURATE_POS(ManPosCrouch);
        }
        else
        {
          #if 0 //_ENABLE_REPORT
            if (!IsDown())
            {
              LogF("%s: lie down, speed %.2f",cc_cast(GetDebugName()),info.speedWanted);
            }
          #endif
          maxPos = SATURATE_POS(ManPosLying);
        }
      }
      else
      {
        minPos = maxPos = SATURATE_POS(ManPosCombat);
      }
    }
    else if( stanceMode == CMStealth )
    {
      // if there is no enemy known or expected, do not lay down
      float exposure = 0;
      minPos = maxPos = SATURATE_POS((exposure<400 && info.speedWanted>type->_lyingLimitSpeedStealth) ? ManPosCombat : ManPosLying);
    }
  }

  bool forceStand = false;
  float dirFwd = 1, dirAside = 0;
  if (!IsAbleToStand() || (!forceStand && !CanStand(_landGradFwd, _landGradAside, dirFwd, dirAside)) || GetEncumbrance() >= 1)
  {
    minPos = LauncherAnimSelected() ? ManPosWeapon : ManPosLying;
    maxPos = ManPosLying;
  }

  /*
  // we may force lying also when the slope is too steep

  // consider terrain when selecting movement
  float dx,dz;
  GLandscape->SurfaceY(Position().X(),Position().Z(),&dx,&dz);
  // if surface is 
  */

  if (_swimming)
  {
    minPos = maxPos = ManPosSwimming;
  }

  // prepare for new animation
  // check which action map is used now
  ActionMap *map = Type()->GetActionMap(_moves.GetPrimaryMove(FutureVisualState()._moves).id, AnimSubIx());

  ManAction selAction = ManActN;

  int upDegree = map ? map->GetUpDegree() : ManPosStand;
  if (_posWanted<minPos || _posWantedTime>Glob.time+30)
  {
    _posWanted = minPos;
    _posWantedTime = Glob.time+(GetInvAbility(AKSpotTime)*delay+delay*0.5f)*GRandGen.RandomValue();
  }
  else if( _posWanted>maxPos )
  {
    _posWanted = maxPos;
    _posWantedTime = Glob.time+(GetInvAbility(AKSpotTime)*delay+delay*0.5f)*GRandGen.RandomValue();
  }

  if (!_itemInHands)
  {
    if (_posWanted==ManPosCombat || _posWanted==ManPosStand)
    {
      SetPosWanted(ManPosNoWeapon);
    }
    else if (_posWanted==ManPosLying)
    {
      SetPosWanted(ManPosLyingNoWeapon);
    }
  }

  //LogF("_posWanted %d",_posWanted);

  if (IsSwimmingInMove() && !_swimming)
  {
    selAction = ManActStopSwim;
  }
  else if (!IsSwimmingInMove() && _swimming) //start swim
  {
    selAction = ManActStartSwim;
  }
  else if (_posWanted==ManPosWeapon && _posWantedTime<=Glob.time && GetActUpDegree(FutureVisualState())!=ManPosWeapon)
  {
    selAction = ManActWeaponOn;
    //LogF("%s: action weapon",(const char *)GetDebugName());
  }
  else if ((BinocularAnimSelected() && _lookTargetTimeLeft>0) || _forceBinocUntil>Glob.time)
  {
    selAction = ManActBinocOn;
  }
  else if (upDegree!=_posWanted && _posWantedTime<=Glob.time)
  {
    //_posWantedTime = TIME_MAX; // cancel request
    if (upDegree==ManPosWeapon)
    {
      if (_posWanted!=ManPosWeapon)
      {
        selAction = ManActWeaponOff;
      }
      else
      {
        selAction = ManActWeaponOn;
      }
    }
    else if (_posWanted<ManPosNormalMin)
    {
    }
    else
    {
      if (_posWanted == ManPosLying || _posWanted==ManPosHandGunLying)
      {
        if (upDegree != ManPosHandGunLying)
        {
          selAction = ManActLying;
        }
      }
      else if (_posWanted == ManPosCrouch)
      {
        if (upDegree != ManPosWeapon && upDegree != ManPosHandGunCrouch)
        {
          if (ValidatePoseChange(map, ManActCrouch))
          { // if the action will not result in the pose (upDegree) change, ignore it (fix http://dev-heaven.net/issues/16229 - Crouched AI with no weapon stuck)
            selAction = ManActCrouch;
          }
        }
      }
      else if (_posWanted == ManPosCombat || _posWanted==ManPosHandGunStand)
      {
        // note: some upDegrees may be valid
        if (upDegree != ManPosWeapon && upDegree != ManPosHandGunStand)
        {
          selAction = ManActCombat;
        }
      }
      else if( _posWanted==ManPosStand ) 
      {
        if (upDegree != ManPosHandGunStand)
        {
          selAction = ManActStand;
        }
      }
      else if( _posWanted==ManPosNoWeapon ) selAction = ManActCivil;
      else if( _posWanted==ManPosLyingNoWeapon ) selAction = ManActCivilLying;
      else if( _posWanted==ManPosDead ) {/*will be handled elsewhere*/}
      else
      {
        LogF("Transition ManPos %d to %d not handled",upDegree,_posWanted);
      }
    }
  }
  if (selAction==ManActN)
  {
    selAction=ManActStop;
    // normal movement - check speedWanted (maybe also speedAside?)
    float limitFast = map ? map->GetLimitFast() : 5;
    float limitSlow = limitFast*0.6f;

    // water limits
    if (_waterDepth>0.6)
    {
      if (_waterDepth>1.0)
      {
        // in deep water only walking is allowed
        saturateMin(info.speedWanted,1.5f);
      }
      else
      {
        // in shallow water only slow running is allowed
        saturateMin(info.speedWanted,2.5f);
      }
    }

    // under certain speed we rather want to "slide" than to switch animations forth and back
    // switching would be likely, as small speeds are usually result of micro manoeuvring
    float limitAnim = floatMin(0.1f,limitFast*0.2f);
    
    if (!_canMoveFast || !CanSprint(_landGradFwd, _landGradAside, dirFwd, dirAside) && !IsDown()) limitFast = 100; // never true
    // when slow limit is enforced high, we need to relax the fast limit as well
    if (!CanRun(_landGradFwd, _landGradAside, dirFwd, dirAside) && !IsDown()) limitFast=limitSlow = 100; // never true

    // when fine control is needed and target is near (desired speed is low), always use walking
    if (_walkSpeedFineControl && Square(info.speedWanted)+Square(info.sideSpeedWanted)<Square(1.5f)) limitFast=limitSlow = 100;
    
    //int fast = 2;
    // selAction = ENUM_CAST(ManAction,SelectManAction[fastFlag][fbFlag+1][rlFlag+1]);
    if (_posWanted == ManPosCrouch || _posWanted == ManPosHandGunCrouch) limitFast = 100;
    if (!Type()->IsAnimal())
    {
      if (_posWanted == ManPosNoWeapon)
      {
        if (map)
        { // check fast/slow/walk actions
          MoveId moveS = Type()->GetMove(map, ManActSlowF); 
          MoveId moveW = Type()->GetMove(map, ManActWalkF); 
          if (moveS!=moveW) // do not disable sprint when there is no run (run==walk - woman, see http://dev-heaven.net/issues/26444)
          {
            limitFast = 100; /* TODO: implement a better fix, sprint/run walkcycle do not match: http://dev-heaven.net/issues/26280*/
          }
        }
      }
    }
    if (info.clearancePredict<ClearanceNormal) limitFast = 100, limitSlow = 100; // slow down before entering a hole to be prone in time

    if( info.speedWanted>limitFast ) selAction = ManActFastF;
    else if( info.speedWanted>limitSlow ) selAction = ManActSlowF;
    else if( info.speedWanted>+limitAnim ) selAction = ManActWalkF;
    else if( info.speedWanted<-limitAnim ) selAction = ManActWalkB;
    else if( info.speedWanted<-limitFast ) selAction = ManActFastB;
    else if( info.speedWanted<-limitSlow ) selAction = ManActSlowB;
    // sometimes make strafing movement
    if (info.sideSpeedWanted<=-limitAnim)
    {
      if (info.sideSpeedWanted<-limitFast)
      {
        if (selAction==ManActStop) selAction = ManActSlowL;
        else if (selAction==ManActWalkF) selAction = ManActWalkLF;
        else if (selAction==ManActWalkB) selAction = ManActWalkLB;
        else if (selAction==ManActSlowF) selAction = ManActSlowLF;
        else if (selAction==ManActSlowB) selAction = ManActSlowLB;
        else if (selAction==ManActFastF) selAction = ManActFastLF;
        else if (selAction==ManActFastB) selAction = ManActFastLB;
      }
      else
      {
        if (selAction==ManActStop) selAction = ManActWalkL;
        else if (selAction==ManActWalkF) selAction = ManActWalkLF;
        else if (selAction==ManActWalkB) selAction = ManActWalkLB;
        else if (selAction==ManActSlowF) selAction = ManActSlowLF;
        else if (selAction==ManActSlowB) selAction = ManActSlowLB;
        else if (selAction==ManActFastF) selAction = ManActSlowLF;
        else if (selAction==ManActFastB) selAction = ManActSlowLB;
      }

    }
    if (info.sideSpeedWanted>=+limitAnim)
    {
      if (info.sideSpeedWanted>limitFast)
      {
        if (selAction==ManActStop) selAction = ManActSlowR;
        else if (selAction==ManActWalkF) selAction = ManActWalkRF;
        else if (selAction==ManActWalkB) selAction = ManActWalkRB;
        else if (selAction==ManActSlowF) selAction = ManActSlowRF;
        else if (selAction==ManActSlowB) selAction = ManActSlowRB;
        else if (selAction==ManActFastF) selAction = ManActFastRF;
        else if (selAction==ManActFastB) selAction = ManActFastRB;
      }
      else
      {
        if (selAction==ManActStop) selAction = ManActWalkR;
        else if (selAction==ManActWalkF) selAction = ManActWalkRF;
        else if (selAction==ManActWalkB) selAction = ManActWalkRB;
        else if (selAction==ManActSlowF) selAction = ManActSlowRF;
        else if (selAction==ManActSlowB) selAction = ManActSlowRB;
        else if (selAction==ManActFastF) selAction = ManActSlowRF;
        else if (selAction==ManActFastB) selAction = ManActSlowRB;
      }
    }
  }

  if(selAction==ManActBinocOn)
  {
    if (_forceBinocUntil<=Glob.time)
    {
      _forceBinocUntil = Glob.time+4;
    }
  }

  if (selAction==ManActStop)
  {
    if( fabs(_turnWanted)>0.5f)
    {
      selAction = _turnWanted<0 ? ManActTurnL : ManActTurnR;
      if (unit->GetCombatModeLowLevel()<CMCombat)
      {
        // first choice - relaxed
        ManAction relAction = _turnWanted<0 ? ManActTurnLRelaxed : ManActTurnRRelaxed;
        if (Type()->GetMove(map, relAction).id!=MoveIdNone)
        {
          selAction = relAction;
        }
      }
    }
    // never use Relaxed animations once you have some target
    // relaxed animations can prevent aligning the target properly
    else if (unit->GetCombatModeLowLevel()<CMCombat)
    {
      if (Type()->GetMove(map, ManActStopRelaxed).id!=MoveIdNone)
      {
        selAction = ManActStopRelaxed;
      }
    }
  }

  // get which action is it (based on action map)
  MoveId moveWanted = map ? Type()->GetMove(map, selAction) : GetDefaultMove(); 
  // LogF("%s: moveWanted %s (action %s)",DNAME(),NAME_T(Type(), moveWanted), (const char *)ManActionNames[selAction].name);

  if (!IsLocal() && _targetMoveWanted!=MoveIdNone)
  {
    ActionMap *targetMap = Type()->GetActionMap(_targetMoveWanted, AnimSubIx());
    if (map!=targetMap && Type()->GetMove(map, ManActStop)!=Type()->GetMove(targetMap, ManActStop))
    {
      moveWanted = _targetMoveWanted;
    }
  }

  SetTargetMove(moveWanted);

  MotionPathItem motionPathItem;
  if( _moves.GetForceMove().id!=MoveIdNone )
  {
    motionPathItem = _moves.GetForceMove();
  }
  else
  {
    motionPathItem = MotionPathItem(moveWanted);
  }
  SetMoveQueue(motionPathItem,prec<=SimulateVisibleFar);    

#if _ENABLE_CHEATS
  if (CHECK_DIAG(DEAnimation))
  {
    RString actionName;
    RStringB moveName = GetMovesType()->GetMoveName(motionPathItem.id);
    if (selAction < ManActN)
      actionName = ManActionNames[selAction].name;
    _debugLastActionsInfo.AddInfo(actionName, moveName, "AIPilot");
  }
#endif

  float limitFast = map ? map->GetLimitFast() : 5;
  _walkSpeedWantedZ=info.speedWanted;
  _walkSpeedWantedX=info.sideSpeedWanted;
  // avoid moving faster than given movement allows to move
  saturate(_walkSpeedWantedZ,-limitFast,+limitFast);
  saturate(_walkSpeedWantedX,-limitFast,+limitFast);
  _walkSpeedFineControl = !info.allowFreeTurn;

  #if _ENABLE_REPORT
    if (CHECK_DIAG(DEPath))
    {
      if (this==GWorld->CameraOn())
      {
        DIAG_MESSAGE(500,Format(
          "%s: speed Z/X %.1f/%.1f, %s",
          cc_cast(GetDebugName()),
          _walkSpeedWantedZ,_walkSpeedWantedX,_walkSpeedFineControl ? "fine" : "fast"
        ));
      }
    }
  #endif
}

bool Soldier::UseBackwardMovement(Vector3Par toMove, EntityAI *leaderVehicle) const
{
  // if it is more than 1 seconds movement full speed, we prefer to turn around
  float toMoveSize2 = toMove.SquareSize();
  if (toMoveSize2>Square(Type()->GetMaxSpeedMs()*1.0f))
  {
    // when leader is moving backward, apply more relaxed limit
    if (leaderVehicle->FutureVisualState().ModelSpeed().Z()>-Type()->GetMaxSpeedMs()*0.1f && toMoveSize2>Square(Type()->GetMaxSpeedMs()*5))
    {
      return false;
    }
  }
  // use backward only if the leader is facing backward
  // note: we need to consider the unit we are keeping with in formation as well
  return FutureVisualState().Direction() * leaderVehicle->FutureVisualState().Direction() > 0;
}

PositionRender Man::GetPositionRender()
{
  PositionRender basePR = base::GetPositionRender();
  assert(basePR.camSpace == false);
  
  Matrix4 transf = basePR.position;

  static float CoverMaxHeight = 0.35f;

  if (GScene->GetCamera())
  {
    Vector3Val cPos = transf.Position();
    Vector3Val camPos = GScene->GetCamera()->Position();

    float distToCamera = (camPos - cPos).Size();

    if (distToCamera > GLandscape->StartDisappearDistGlobMin())
    {
      int x = toIntFloor(cPos.X() * GLandscape->GetInvLandGrid());
      int z = toIntFloor(cPos.Z() * GLandscape->GetInvLandGrid());

      AUTO_STATIC_ARRAY(InitPtr<Object>,roadways,32);
      GLandscape->GetRoadList(x, z, roadways);
      float dY = GLandscape->GetGrassHeight(cPos.X(), cPos.Z(), roadways);

      static float MaxGrassY = 0.5f;
      _coverGrassHeightWanted = InterpolativC(dY, 0.0f, MaxGrassY, 0.0f, CoverMaxHeight);

      // GLandscape->EndDisappearDistGMax() - no clutter is drawn behind this boundary
      float distMax = floatMin(GLandscape->EndDisappearDistGMax(), GLandscape->EffDissapearDist());      
      // update minimum when necessary
      float distMin = GLandscape->StartDisappearDistGlobMin() > distMax ? distMax * 0.7 : GLandscape->StartDisappearDistGlobMin();

      // distance interpolation
      float distMult = InterpolativC(distToCamera, distMin, distMax, 0.0f, 1.0f);

      _coverGrassHeightWanted *= distMult;
    }
    else
      _coverGrassHeight = 0;
  }
  else
    _coverGrassHeight = 0;

  if (_coverGrassHeight > 0.001f)
  {
    Vector3 newPos = transf.Position() + Vector3(0.0f, -_coverGrassHeight , 0.0f);
    transf.SetPosition(newPos);
  }

  return PositionRender(transf, false);
}

/*!
\patch 1.97 Date 6/4/2004 by Jirka
- Added: disableAI option for disable automatic animation selection
*/
void Soldier::DisabledPilot(float deltaT, SimulationImportance prec)
{
  _turnWanted=0;
  _walkSpeedWantedX = 0;
  _walkSpeedWantedZ = 0;

  // set primary move to stand

  SimulateForcedMove(prec);
}

#endif //_ENABLE_AI

Vector3 Man::GetCommonDamagePoint()
{
//  int level = GetShape()->FindHitpoints();
  int level = GetShape()->FindMemoryLevel();
  if (level < 0) return VZero;

  const ManType *type = Type();
  const VisualState &vs = FutureVisualState();
  return AnimatePoint(vs, level, type->_commonDamagePoint);
}

void Man::CleanUp()
{
  DestroyPPEHndls(_ppeHndls);
  IRLaserWanted(false);
  UpdateJoinedObject();
  base::CleanUp();
}

void Man::AddProjectileDecal(ProjectileDecalOnVehicle *decal)
{
  //RemoveProjectileDecals();

  decal->AttachTo(this);
  GLOB_LAND->AddObject(decal);
  _attachedDecals.Add(decal);
}

void Man::RemoveProjectileDecal(ProjectileDecalOnVehicle *decal)
{
  int index = _attachedDecals.Find(decal);
  Assert(index >= 0);
  if (index >= 0)
  {
    GLOB_LAND->RemoveObject(decal);
    decal->Detach();
    _attachedDecals.Delete(index);
  }
}

int Man::RemoveProjectileDecals(int startIdx /*= 0*/, int count /*= 999*/)
{
  const int remCount = startIdx < _attachedDecals.Size() ? intMin(count, _attachedDecals.Size() - startIdx) : 0;
  if (remCount > 0)
  {
    for(int i=startIdx; i<remCount; i++)
    {
      GLOB_LAND->RemoveObject(_attachedDecals[i]);
    }
    _attachedDecals.DeleteAt(startIdx, remCount);

    return remCount;
  }

  return 0;
}

LSError Man::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))

  // TODO: default value for SerializeEnumValue
  // CHECK(ar.SerializeEnumValue("primaryMove", _primaryMove.id, 1, Type()->GetMoveIdNames() ))
  // CHECK(ar.SerializeEnumValue("secondaryMove", _secondaryMove.id, 1, Type()->GetMoveIdNames() ))

  if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
  {
    CHECK(_moves.Serialize(FutureVisualState()._moves,"move",GetMovesType(), ar))
    CHECK(_gesture.Serialize(FutureVisualState()._gestures,"gesture",GetGesturesType(), ar))

    CHECK(ar.Serialize("walkSpeedWanted", _walkSpeedWantedZ, 1, 0))
    CHECK(ar.Serialize("sideSpeedWanted", _walkSpeedWantedX, 1, 0))

    CHECK(::Serialize(ar, "whenScreamed", _whenScreamed, 1, TIME_MAX))

    CHECK(ar.Serialize("tired", _tired, 1, 0))
    CHECK(ar.Serialize("metabolismFactor", _metabolismFactor, 1, 1.0f))
    
    CHECK(ar.Serialize("hideBody", _hideBody, 1, 0))
    CHECK(ar.Serialize("hideBodyWanted", _hideBodyWanted, 1, 0))
    CHECK(ar.SerializeEnum("unitPosCommanded", _unitPosCommanded, 1, ENUM_CAST(UnitPosition,UPAuto)))
    CHECK(ar.SerializeEnum("unitPosScripted", _unitPosScripted, 1, ENUM_CAST(UnitPosition,UPAuto)))
    CHECK(ar.SerializeEnum("unitPosFSM", _unitPosFSM, 1, ENUM_CAST(UnitPosition,UPAuto)))

    CHECK(ar.SerializeRef("ladderBuilding", _ladderBuilding, 1))
    CHECK(ar.Serialize("ladderIndex", _ladderIndex, 1, -1))
    CHECK(ar.Serialize("ladderPosition", _ladderPosition, 1, -1))

    CHECK(ar.SerializeRefs("pipeBombs",_pipeBombs,1 ))

    CHECK(ar.Serialize("nvg", _nvg, 1, false))

    CHECK(ar.Serialize("headXRotWanted", _headXRotWanted, 1, 0))
    CHECK(ar.Serialize("headYRotWanted", _headYRotWanted, 1, 0))
    CHECK(ar.Serialize("headXRotWantedPan", _headXRotWantedPan, 1, 0))
    CHECK(ar.Serialize("headYRotWantedPan", _headYRotWantedPan, 1, 0))
    CHECK(ar.Serialize("lookXRotWanted", _lookXRotWanted, 1, 0))
    CHECK(ar.Serialize("lookYRotWanted", _lookYRotWanted, 1, 0))
    CHECK(ar.Serialize("gunXRotWanted", _gunXRotWanted, 1, 0))
    CHECK(ar.Serialize("gunYRotWanted", _gunYRotWanted, 1, 0))
    CHECK(ar.Serialize("zoom", _zoom, 1));
    CHECK(ar.Serialize("zoomInToggle", _zoomInToggled, 1, false))
    CHECK(ar.Serialize("zoomOutToggle", _zoomOutToggled, 1, false))

    CHECK(ar.Serialize("directSpeaking", _directSpeaking,1) )
    CHECK(ar.Serialize("lightWanted", _lightWanted, 1, false))
    CHECK(ar.Serialize("lightForced", _lightForced, 1, false))
    CHECK(ar.Serialize("ir", _irWanted, 1, false))
    CHECK(ar.Serialize("irEnable", _irEnabled, 1, false))
    CHECK(ar.Serialize("flir", _flir, 1, false))
    CHECK(ar.Serialize("wasNVGActive", _wasNVGActive, 1, false))
    CHECK(ar.Serialize("wasNVGStateSet", _wasNVGStateSet, 1, true))
    CHECK(ar.Serialize("useVisionModes", _useVisionModes, 1, false))
    CHECK(::Serialize(ar,"forceBinocUntil", _forceBinocUntil, 1, TIME_MIN))

    CHECK(ar.Serialize("customRecoilCoefficient", _customRecoilCoefficient, 1, 1.0f))
    CHECK(ar.Serialize("walkForced", _walkForced, 1, false))

    CHECK(ar.Serialize("coverGrassHeight", _coverGrassHeight, 1, 0))
    CHECK(ar.Serialize("coverGrassHeightWanted", _coverGrassHeightWanted, 1, 0))

    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst) DestroyPPEHndls(_ppeHndls);
    CHECK(ar.Serialize("ppeHndls", _ppeHndls, 1))

    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassSecond)
    {
      // we could serialize _hasNVG, but then we would need to care
      // about older version files
      ScanEquipment();

      _lUpdateTime = Time(0);
      _lSimTime = Time(0);

      _headXRot = _headXRotWanted;
      _headYRot = _headYRotWanted;
      _lookXRot = _lookXRotWanted;
      _lookYRot = _lookYRotWanted;
      _gunXRot = _gunXRotWanted;
      _gunYRot = _gunYRotWanted;
      _gunXRotWantedNoTrembleHands = _gunXRotWanted;
      _gunYRotWantedNoTrembleHands = _gunYRotWanted;
      _gunYRotIronSightBackup = _gunYRotWanted;

      RecalcGunTransform();
      if (IsInLandscape())
      {
        RecalcPositions(GetFrameBase());
      }
      else
      {
        // TODO: check for parent

        FutureVisualState()._aimingPositionWorld = VZero;
        FutureVisualState()._cameraPositionWorld = VZero;
        //_headPositionWorld = VZero;
      }
    }

    CHECK(ar.Serialize("cover",_cover,1));
    CHECK(ar.Serialize("inOpenCover",_inOpenCover,1));
    CHECK(ar.Serialize("coverEntered",_coverEntered,1,0));

    CHECK(ar.Serialize("leaveCoverSemaphore",_leaveCoverSemaphore,1,0));

    // we may need to keep this
    CHECK(ar.SerializeEnum("coverState",_coverState,1,ENUM_CAST(CoverState,CoverNo)));
    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassSecond)
    {
      // we want to check which of the covers denote the same object
      if (_brain)
      {
        const CoverInfo &pathCover = _brain->GetPath().GetCover();
        CoverVars *vars = pathCover._payload;
        if (vars && _cover && *vars==*_cover)
        {
          // we are in fact not changing it - we are replacing with an equivalent
          unconst_cast(pathCover._payload) = _cover;
        }
      }
      
    }
  }

  ProgressRefresh();
  return LSOK;

/* TODO: ?? Serialize
  float _gunYRot,_gunYRotWanted;
  float _gunXRot,_gunXRotWanted;
  float _gunXSpeed,_gunYSpeed;

  Ref<AbstractWave> _sound;

  bool _doSoundStep:1;

  bool _pilotLayDown:1,_pilotStandUp:1,_pilotLaunchDown:1;
  bool _endSpecMode:1; // transition from spec mode

  float _timeToCrawl; // crawl/run time left
  float _timeToRun; // crawl/run time left

  Time _freeFallUntil;

  Time _layDownTime;
  Time _standUpTime;
  float _launchDelay;
  float _standUpDelay; // (group leader is standing - stand too)

  float _aimInaccuracyX,_aimInaccuracyY;
  Time _lastInaccuracyTime;
  float _aimInaccuracyDist;
  Time _lastInaccuracyDistTime;

  float _waterDepth;

  Time _whenKilled; // helpers for suspending dead body after a while
  Time _lastMovementTime;

  float _turnWanted;
*/
}

#define MAN_MSG_LIST(XX) \
  XX(UpdateGeneric, UpdateMan) \
  XX(UpdatePosition, UpdatePositionMan)

DEFINE_NETWORK_OBJECT(Man, base, MAN_MSG_LIST)

#define UPDATE_MAN_MSG(MessageName, XX) \
  XX(MessageName, float, hideBodyWanted, NDTFloat, float, NCTFloat0To1, DEFVALUE(float, 0), DOC_MSG("Wanted state of body (1 .. fully hidden)"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
  XX(MessageName, float, tired, NDTFloat, float, NCTFloat0To1, DEFVALUE(float, 0), DOC_MSG("How man is tired"), TRANSF, ET_NONE, 0) \
  XX(MessageName, float, walkSpeedWanted, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG("Wanted speed"), TRANSF, ET_NONE, 0) \
  XX(MessageName, float, sideSpeedWanted, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG("Wanted side speed"), TRANSF, ET_NONE, 0) \
  XX(MessageName, int, unitPosCommanded, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, UPAuto), DOC_MSG("Up / down state"), TRANSF, ET_NONE, 0) \
  XX(MessageName, int, unitPosScripted, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, UPAuto), DOC_MSG("Up / down state"), TRANSF, ET_NONE, 0) \
  XX(MessageName, OLink(EntityAI), flagCarrier, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Carried flag"), TRANSF_REF, ET_NOT_EQUAL, ERR_COEF_MODE) \
  XX(MessageName, bool, nvg, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Night vision is active"), TRANSF, ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR) \
  XX(MessageName, bool, irWanted, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Infra red vision is active"), TRANSF, ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR) \
  XX(MessageName, bool, lightWanted, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Gun flash lightis active"), TRANSF, ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR) \
  XX(MessageName, int, movesPlayMove, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, MoveIdNone), DOC_MSG("Current action animation on the main layer"), TRANSF, ET_NOT_EQUAL, ERR_COEF_MODE) \
  XX(MessageName, int, gesturePlayMove, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, MoveIdNone), DOC_MSG("Current action animation on the gesture layer"), TRANSF, ET_NOT_EQUAL, ERR_COEF_MODE)
//  XX(MessageName, RStringB, movesPlayMove, NDTString, RStringB, NCTStringMove, DEFVALUE(RString, ""), DOC_MSG("Current action animation on the main layer"), TRANSF, ET_NOT_EQUAL, ERR_COEF_MODE) \
//  XX(MessageName, RStringB, gesturePlayMove, NDTString, RStringB, NCTStringMove, DEFVALUE(RString, ""), DOC_MSG("Current action animation on the gesture layer"), TRANSF, ET_NOT_EQUAL, ERR_COEF_MODE)

DECLARE_NET_INDICES_EX_ERR(UpdateMan, UpdateVehicleBrain, UPDATE_MAN_MSG)
DEFINE_NET_INDICES_EX_ERR(UpdateMan, UpdateVehicleBrain, UPDATE_MAN_MSG, NoErrorInitialFunc)

#define UPDATE_POSITION_MAN_MSG(MessageName, XX) \
  XX(MessageName, float, gunXRotWanted, NDTFloat, float, NCTFloatMPIToPPI, DEFVALUE(float, 0), DOC_MSG("Wanted gun rotation"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
  XX(MessageName, float, gunYRotWanted, NDTFloat, float, NCTFloatMPIToPPI, DEFVALUE(float, 0), DOC_MSG("Wanted gun rotation"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
  XX(MessageName, float, headXRotWanted, NDTFloat, float, NCTFloatMPIToPPI, DEFVALUE(float, 0), DOC_MSG("Wanted head rotation"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
  XX(MessageName, float, headYRotWanted, NDTFloat, float, NCTFloatMPIToPPI, DEFVALUE(float, 0), DOC_MSG("Wanted head rotation"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
  XX(MessageName, float, lookXRotWanted, NDTFloat, float, NCTFloatMPIToPPI, DEFVALUE(float, 0), DOC_MSG("Wanted look rotation"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
  XX(MessageName, float, lookYRotWanted, NDTFloat, float, NCTFloatMPIToPPI, DEFVALUE(float, 0), DOC_MSG("Wanted look rotation"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
  XX(MessageName, float, headXRotWantedCont, NDTFloat, float, NCTFloatMPIToPPI, DEFVALUE(float, 0), DOC_MSG("Wanted trackIR head rotation"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
  XX(MessageName, float, headYRotWantedCont, NDTFloat, float, NCTFloatMPIToPPI, DEFVALUE(float, 0), DOC_MSG("Wanted trackIR head rotation"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
  XX(MessageName, float, headXRotWantedPan, NDTFloat, float, NCTFloatMPIToPPI, DEFVALUE(float, 0), DOC_MSG("Wanted pan (keyboard) head rotation"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
  XX(MessageName, float, headYRotWantedPan, NDTFloat, float, NCTFloatMPIToPPI, DEFVALUE(float, 0), DOC_MSG("Wanted pan (keyboard) head rotation"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
  XX(MessageName, float, leanZRotWanted, NDTFloat, float, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Wanted lean aside"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
  XX(MessageName, int, move, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, MoveIdNone), DOC_MSG("Current animation"), TRANSF, ET_NOT_EQUAL, ERR_COEF_MODE) \
  XX(MessageName, OLinkO(Building), ladderBuilding, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Ladder ID"), TRANSF_REF, ET_NOT_EQUAL, ERR_COEF_MODE) \
  XX(MessageName, int, ladderIndex, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("Ladder ID"), TRANSF, ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR) \
  XX(MessageName, float, ladderPosition, NDTFloat, float, NCTFloat0To1, DEFVALUE(float, 0), DOC_MSG("Position on ladder"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MAJOR) \

  //XX(MessageName, RStringB, move, NDTString, RStringB, NCTStringMove, DEFVALUE(RString, "Stand"), DOC_MSG("Current animation"), TRANSF, ET_NOT_EQUAL, ERR_COEF_MODE) \

DECLARE_NET_INDICES_EX_ERR(UpdatePositionMan, UpdatePositionVehicle, UPDATE_POSITION_MAN_MSG)
DEFINE_NET_INDICES_EX_ERR(UpdatePositionMan, UpdatePositionVehicle, UPDATE_POSITION_MAN_MSG, NoErrorInitialFunc)

/*!
\patch 1.27 Date 10/11/2001 by Ondra.
- Optimized: Soldier update more compressed.
*/

NetworkMessageFormat &Man::CreateFormat
(
  NetworkMessageClass cls,
  NetworkMessageFormat &format
)
{
  switch (cls)
  {
  case NMCUpdateGeneric:
    base::CreateFormat(cls, format);
    UPDATE_MAN_MSG(UpdateMan, MSG_FORMAT_ERR)
    break;
  case NMCUpdatePosition:
    base::CreateFormat(cls, format);
    UPDATE_POSITION_MAN_MSG(UpdatePositionMan, MSG_FORMAT_ERR)
    break;
  default:
    base::CreateFormat(cls, format);
    break;
  }
  return format;
}

bool Man::RejectMsg(NetworkMessageClass cls, NetworkMessage *msg)
{
  if (!GetNetworkManager().IsServer()) return false;
  if (GetNetworkManager().GetClientState() != NCSPlaying) return false;
  bool rejectedByBase = base::RejectMsg(cls,msg);
  if (rejectedByBase) return true;
  switch (cls)
  {
    case NMCUpdatePosition:
    {
      PREPARE_TRANSFER_MSG(UpdatePositionMan,msg)
      if (IsInLandscape())
      {
/*
        RStringB mName = message->_move;
        if (!mName.IsEmpty())
        {
          MoveId moveId = GetMovesType()->GetMoveId(mName);
*/
          MoveId moveId = (MoveId)message->_move;
          if (moveId!=MoveIdNone)
          {
            bool reject = false;
            //const auto &vs = FutureVisualState();
            //if (_moves.GetPrimaryMove(vs._moves).context) reject = true;
            //if (_moves.GetForceMove().context) reject = true;
            /*
            const ActionMap *curMap = GetMovesType()->GetActionMap(_moves.GetPrimaryMove(vs._moves).id);
            const ActionMap *newMap = GetMovesType()->GetActionMap(moveId);
            if (curMap!=newMap)
            {
              __asm nop;
              //reject = true;
            }
            */
            return reject;
          }
        //}
      }
      break;
    }

  }
  return false;
}

TMError Man::TransferMsg(NetworkMessageContext &ctx)
{
  switch (ctx.GetClass())
  {
  case NMCUpdateGeneric:
    TMCHECK(base::TransferMsg(ctx))
    {
      PREPARE_TRANSFER(UpdateMan)

      TRANSF(hideBodyWanted)
      TRANSF(tired)

      //ITRANSF(rndSpeed)
      if (ctx.IsSending()) // TODO: remove fields completely
      {
        TRANSF_EX(walkSpeedWanted,_walkSpeedWantedZ)
        TRANSF_EX(sideSpeedWanted,_walkSpeedWantedX)
      }
      TRANSF_ENUM(unitPosCommanded)
      TRANSF_ENUM(unitPosScripted)
      TRANSF(nvg)
      TRANSF(irWanted)
      TRANSF(lightWanted)
      if (ctx.IsSending())
      {
//        TRANSF_EX(movesPlayMove, GetMovesType()->GetMoveNameEx(_moves.GetCurrentPlayMove(FutureVisualState()._moves)))
//        TRANSF_EX(gesturePlayMove, GetGesturesType()->GetMoveNameEx(_gesture.GetCurrentPlayMove(FutureVisualState()._gestures)))
          MoveId moveM = _moves.GetCurrentPlayMove(FutureVisualState()._moves);
          MoveId moveG = _gesture.GetCurrentPlayMove(FutureVisualState()._gestures);
          TRANSF_ENUM_EX(movesPlayMove, moveM);
          TRANSF_ENUM_EX(gesturePlayMove, moveG);
      }
      else
      {
        if (IsInLandscape()) // avoid updates of animations if inside vehicle, old message (from time when soldier is outside) can be processed
        {
          // transfer current action played on top of the AI/player control
          //RStringB playM,playG;
          //TRANSF_EX(movesPlayMove, playM);
          //TRANSF_EX(gesturePlayMove, playG);
          //MoveId moveM = GetMovesType()->GetMoveId(playM);
          //MoveId moveG = GetGesturesType()->GetMoveId(playG);
          MoveId moveM,moveG;
          TRANSF_ENUM_EX(movesPlayMove, moveM);
          TRANSF_ENUM_EX(gesturePlayMove, moveG);
          _moves.ChangePlayMove(moveM);
          _gesture.ChangePlayMove(moveG);
        }
      }
    }
    break;
  case NMCUpdatePosition:
    TMCHECK(base::TransferMsg(ctx))
    {
	  int lastLadderIndex = _ladderIndex;
      PREPARE_TRANSFER(UpdatePositionMan)

      // Position snapshots are authoritative for the body, but the local
      // player's view angles are predicted from this frame's mouse input.
      // Keep them before reading the server snapshot so an older look value
      // cannot visibly pull the camera back after a local mouse movement.
      const bool preserveLocalView = !ctx.IsSending() && GWorld && this == GWorld->GetRealPlayer();
      const float localGunXRotWanted = _gunXRotWanted;
      const float localGunYRotWanted = _gunYRotWanted;
      const float localHeadXRotWanted = _headXRotWanted;
      const float localHeadYRotWanted = _headYRotWanted;
      const float localHeadXRotWantedCont = _headXRotWantedCont;
      const float localHeadYRotWantedCont = _headYRotWantedCont;
      const float localHeadXRotWantedPan = _headXRotWantedPan;
      const float localHeadYRotWantedPan = _headYRotWantedPan;
      const float localLookXRotWanted = _lookXRotWanted;
      const float localLookYRotWanted = _lookYRotWanted;

      TRANSF(gunXRotWanted)
      TRANSF(gunYRotWanted)
      TRANSF(headXRotWanted)
      TRANSF(headYRotWanted)
      TRANSF(lookXRotWanted)
      TRANSF(lookYRotWanted)
      TRANSF(headXRotWantedCont)
      TRANSF(headYRotWantedCont)
      TRANSF(headXRotWantedPan)
      TRANSF(headYRotWantedPan)
      TRANSF(leanZRotWanted)

      if (preserveLocalView)
      {
        _gunXRotWanted = localGunXRotWanted;
        _gunYRotWanted = localGunYRotWanted;
        _headXRotWanted = localHeadXRotWanted;
        _headYRotWanted = localHeadYRotWanted;
        _headXRotWantedCont = localHeadXRotWantedCont;
        _headYRotWantedCont = localHeadYRotWantedCont;
        _headXRotWantedPan = localHeadXRotWantedPan;
        _headYRotWantedPan = localHeadYRotWantedPan;
        _lookXRotWanted = localLookXRotWanted;
        _lookYRotWanted = localLookYRotWanted;
      }

      TRANSF_REF(ladderBuilding)
      TRANSF(ladderIndex)
      TRANSF(ladderPosition)

      if (ctx.IsSending())
      {
//        RStringB mName = GetMovesType()->GetMoveNameEx(_targetMove);
//        TRANSF_EX(move, mName)
        TRANSF_ENUM_EX(move, _targetMove);
      }
      else
      {

        if (IsInLandscape()) // avoid updates of animations if inside vehicle, old message (from time when soldier is outside) can be processed
        {
          if (_remoteState)
          {
            Vector3 modelSpeed = FutureVisualState().DirectionWorldToModel(_remoteState->speed);
            _walkSpeedWantedX = modelSpeed.X();
            _walkSpeedWantedZ = modelSpeed.Z();
          }
          {
            MoveId moveID;
            TRANSF_ENUM_EX(move, moveID);
            if (GetNetworkManager().GetClientState() != NCSPlaying)
            {
              SwitchMove(MoveIdExt(moveID,MovePrimary),NULL);
            }
            else
            {
              _targetMoveWanted = moveID;
            }
/*
            RStringB mName;
            TRANSF_EX(move, mName)
            if (mName.GetLength() > 0)
            {
              if (GetNetworkManager().GetClientState() != NCSPlaying)
              {
                SwitchMove(mName);
              }
              else
              {
                _targetMoveWanted = GetMovesType()->GetMoveId(mName);
              }
            }
*/
          }
          if( !GetNetworkManager().IsServerOwner() && _ladderBuilding && (lastLadderIndex != _ladderIndex) && (_ladderIndex != -1) )
          {
            Vector3 ladderForward = _ladderBuilding->GetLadderForward(_ladderIndex);
            FutureVisualState().SetDirectionAndUp(_ladderBuilding->FutureVisualState().DirectionModelToWorld(ladderForward),  VUp);
          }

          RecalcPositions(GetFrameBase());
        }
        else
        {
          // TODO: check for parent

          FutureVisualState()._aimingPositionWorld = VZero;
          FutureVisualState()._cameraPositionWorld = VZero;
        }
      }
    }
    break;
  default:
    return base::TransferMsg(ctx);
  }
  #if 0 //_ENABLE_CHEATS
    if (this==GWorld->CameraOn() && !QIsManual())
    {
      PREPARE_TRANSFER(NetworkObject)
      Time simTime = message->_simulationTime;

      char buffer[1024];
      sprintf
      (
        buffer,
        "%s: %s update class %d, time sent %.3f",
        ctx.IsSending() ? "Sent" : "Received",
        (const char *)GetDebugName(),
        ctx.GetClass(),simTime.toFloat()
      );
      GlobalShowMessage(500,buffer);
      LogF(buffer);
    }
  #endif
  return TMOK;
}

bool Man::ChildUpdateByParent(const Entity *obj) const
{
  for (int i=0; i<NInventorySlotId; i++)
  {
    if (_inventorySlots[i]==obj) return true;
  }
  return false;
}

float Man::CalculateError(NetworkMessageContextWithError &ctx)
{
  float error = 0;
  switch (ctx.GetClass())
  {
  case NMCUpdateGeneric:
    {
      error += base::CalculateError(ctx);

      PREPARE_TRANSFER(UpdateMan)

      ICALCERRE_NEQ(bool, irWanted, _irWanted, ERR_COEF_VALUE_NORMAL);
      ICALCERRE_NEQ(bool, lightWanted, _lightWanted, ERR_COEF_VALUE_NORMAL);
      ICALCERRE_NEQ(int, movesPlayMove, _moves.GetCurrentPlayMove(FutureVisualState()._moves), ERR_COEF_STRUCTURE)
      ICALCERRE_NEQ(int, gesturePlayMove, _gesture.GetCurrentPlayMove(FutureVisualState()._gestures), ERR_COEF_STRUCTURE)
//      ICALCERRE_NEQ(RStringB, movesPlayMove, GetMovesType()->GetMoveNameEx(_moves.GetCurrentPlayMove(FutureVisualState()._moves)), ERR_COEF_STRUCTURE)
//      ICALCERRE_NEQ(RStringB, gesturePlayMove, GetGesturesType()->GetMoveNameEx(_gesture.GetCurrentPlayMove(FutureVisualState()._gestures)), ERR_COEF_STRUCTURE)
      // TODO: implementation
    }
    break;
  case NMCUpdatePosition:
    {
      error += base::CalculateError(ctx);

      PREPARE_TRANSFER(UpdatePositionMan)

//        ICALCERRE_NEQ(RStringB, move, GetMovesType()->GetMoveNameEx(_targetMove), ERR_COEF_STRUCTURE)
      ICALCERRE_NEQ(int, move, _targetMove, ERR_COEF_STRUCTURE)

      ICALCERRE_ABSDIF(float,headXRotWanted, _headXRotWanted,ERR_COEF_VALUE_MINOR);
      ICALCERRE_ABSDIF(float,headYRotWanted, _headYRotWanted,ERR_COEF_VALUE_MINOR);
      ICALCERRE_ABSDIF(float,lookXRotWanted, _lookXRotWanted,ERR_COEF_VALUE_MINOR);
      ICALCERRE_ABSDIF(float,lookYRotWanted, _lookYRotWanted,ERR_COEF_VALUE_MINOR);
      ICALCERRE_ABSDIF(float,headXRotWantedCont, _headXRotWantedCont,ERR_COEF_VALUE_MINOR);
      ICALCERRE_ABSDIF(float,headYRotWantedCont, _headYRotWantedCont,ERR_COEF_VALUE_MINOR);
      ICALCERRE_ABSDIF(float,headXRotWantedPan, _headXRotWantedPan,ERR_COEF_VALUE_MINOR);
      ICALCERRE_ABSDIF(float,headYRotWantedPan, _headYRotWantedPan,ERR_COEF_VALUE_MINOR);
      ICALCERRE_ABSDIF(float,gunXRotWanted, _gunXRotWanted,ERR_COEF_VALUE_MINOR);
      ICALCERRE_ABSDIF(float,gunYRotWanted, _gunYRotWanted,ERR_COEF_VALUE_MINOR);
      ICALCERRE_ABSDIF(float,leanZRotWanted, _leanZRotWanted,ERR_COEF_VALUE_MINOR);

      ICALCERR_NEQREF_SOFT(EntityAI, ladderBuilding, ERR_COEF_MODE)
      ICALCERRE_NEQ(int,ladderIndex, _ladderIndex,ERR_COEF_VALUE_MAJOR);
      ICALCERRE_NEQ(float,ladderPosition, _ladderPosition,ERR_COEF_VALUE_MAJOR);
    }
    break;
    default:
    error += base::CalculateError(ctx);
    break;
  }
  return error;
}

/*!
  \patch 5101 Date 12/13/2006 by Ondra
  - Fixed: Peeking through walls using leaning is no longer possible.
*/
float Man::CheckMaxLean(float leanZRot, float xOffset,float zOffset, bool isAimbody )
{
  // Checking intersection disabled  of new collision geometry and weapon collision behaviour (camPosBeg, camPosEnd, offset and check intersection)
#define DISABLE_WEAPON_COLLISION 1
#if DISABLE_WEAPON_COLLISION
  return leanZRot;
#else
  ProtectedVisualState<const VisualState> vs = FutureVisualStateScope();
  // pretend there is no leaning
  // TODO: avoid setting state variables, use temporaries instead?

  Vector3 camPosBeg;
  // check where the camera would be with no leaning/aimbody
  if (isAimbody) // aimbody branch
  {
    _gunXRot = 0;
    RecalcGunTransform();
    camPosBeg = CalculateCameraPosition(vs->Transform());
    _gunXRot = fSign(leanZRot);
    RecalcGunTransform();
  }
  else  // leaning branch
  {
    _leanZRot = 0;
    RecalcLeaningTransform();
    camPosBeg = CalculateCameraPosition(vs->Transform());
    _leanZRot = fSign(leanZRot);
    RecalcLeaningTransform();
  }
  Vector3 camPosEnd;
  camPosEnd = CalculateCameraPosition(vs->Transform());
  // extend a line into the max. possible leaning position

  // we need to keep some free space (5-10 cm?)
  // space needs to be in front of the camera
  // camera direction is not necessarily the same as soldier direction
  // we also need some side offset to make sure screen does not clip in

  Vector3 offset = vs->Orientation()*(vs->_lookTrans.Orientation()*(vs->_gunBodyTrans.Orientation()*Vector3(xOffset*fSign(leanZRot),0,zOffset)));

  camPosEnd += offset;
  camPosBeg += offset;

#if 0 // _ENABLE_CHEATS
  //if (CHECK_DIAG(DECollision))
  {
    GScene->DrawCollisionStar(camPosBeg,0.05,PackedColor(Color(0,0.5,0)));
    GScene->DrawCollisionStar(camPosEnd,0.035,PackedColor(Color(0,0.5,0.5)));
  }
#endif

  // check intersection

  CollisionBuffer retVal;
  GLandscape->ObjectCollisionLine(GetFutureVisualStateAge(),retVal,Landscape::FilterIgnoreOne(this),camPosBeg,camPosEnd,0,ObjIntersectGeom);
  if (retVal.Size()>0)
  {
    // seek first hit
    float minT = 1e10;
    for (int i=0; i<retVal.Size(); i++)
    {
      if (minT>retVal[i].under)
      {
        minT = retVal[i].under;
      }
    }
    // limit leaning as needed
    if (minT<0) minT = 0;
    if (fabs(leanZRot)>minT)
    {
      leanZRot = minT*fSign(leanZRot);
    }
  }
  // return the value (may have been limited meanwhile)
  return leanZRot;
#endif
}

#if _VBS3
// Return the fatigue level of the individual. 
// A value between 0 [unfatigued] and 1 [totally fatigued]
float Man::GetFatigue() const
{
  return (_anaerobicExertion+_aerobicExertion)/2.0;
}

// Add on an amount of energy expenditure to the
// anaerobic total. Values should be between
// 0 and 1 (totally expended).
void Man::AddAnaerobicExertion(float addon) 
{
  if (addon < -1)
    addon = -1;
  if (addon > 1)
    addon = 1;
  float scaled = addon*_maxAnaerobicExertion;
  if (scaled+_anaerobicEnergy<0)
    scaled = -_anaerobicEnergy;
  _anaerobicEnergy += scaled;
  _anaerobicValues[_anaerobicIndex] += scaled;
  _anaerobicExertion = _anaerobicEnergy / _maxAnaerobicExertion;
  if (_anaerobicExertion>1.0)
    _anaerobicExertion = 1.0;
}


// Add on an amount of energy expenditure to the
// anaerobic total for the current second only. Values should be between
// 0 and 1 (totally expended for the second).
void Man::AddAnaerobicExertionBySecond(float addon) 
{
  AddAnaerobicExertion(addon/NUM_ANAEROBIC_VALUES);
}


// Add on an amount of energy expenditure to the
// aerobic total. Values should be between
// 0 and 1 (totally expended).
void Man::AddAerobicExertion(float addon) 
{
  if (addon < -1)
    addon = -1;
  if (addon > 1)
    addon = 1;
  float scaled = addon*_maxAerobicExertion;
  if (scaled+_aerobicEnergy<0)
    scaled = -_aerobicEnergy;
  _aerobicEnergy += scaled;
  _aerobicValues[_aerobicIndex] += scaled;
  _aerobicExertion = _aerobicEnergy / _maxAerobicExertion;
  if (_aerobicExertion>1.0)
    _aerobicExertion = 1.0;
}

// Adds to both aerobic and anaerobic exertion.
void Man::AddExertion(float addon) 
{
  AddAerobicExertion(addon);
  AddAnaerobicExertion(addon);
}


// Get the level of aerobic exertion.
// Between 0 and 1.
float Man::GetAerobicExertion() 
{
  return _aerobicExertion;
}

// Get the level of anaerobic exertion.
// Between 0- and 1.
float Man::GetAnaerobicExertion() 
{
  return _anaerobicExertion;
}

Morale* Man::GetMorale() { return &_morale; }


// Carry out the secondary effects of suppression. That is increase fatigue 
// and inform the morale system.
void Man::SuppressSecondaryEffects() 
{
  float stress = SUPPRESSION_STRESS_COST / (float)NUM_ANAEROBIC_VALUES;
  AddAnaerobicExertion(stress);
  _morale.HaveBeenSuppressed();
}

// Intermediary function so that vehicleAI can call this method,
// which then passes onto morale (where we want that info).
void Man::HaveFired() {
  _morale.HaveFired();
}

// Man has been injured or killed. This affects morale of other units in
// subgroup - inform them.
void Man::InformSubgroupCasualty() 
{
  AIBrain *brain = Brain();
  bool injured = brain && brain->LSIsAlive();
  if (!brain) return;
  AIUnit *unit = brain->GetUnit();
  if (!unit) return;
  AISubgroup *subgrp = unit->GetSubgroup();
  if (!subgrp) return;

  for( int i = 0; i < subgrp->NUnits(); ++i) 
  {
    AIBrain *unitBrain = subgrp->GetUnit(i);
    if(unitBrain) 
    {
      Person *myPerson = unitBrain->GetPerson();
      Man	*man = dyn_cast<Man>(myPerson);
      if (man) 
      {
        Morale *morale = man->GetMorale();
        if (injured)
          morale->SquadMateInjured();
        else
          morale->SquadMateKilled();
      } // man
    } // unitBrain
  } // for
}
#endif //_VBS3

BankArray<MovesTypeMan> MovesTypesMan;
BankArray<MovesTypeGestureMan> MovesTypesGestureMan;

FindArray<RStringB> CollisionVertexPattern; ///< Pattern order of vertexes in man's geometry level.
FindArray<RStringB> CollisionGeomCompPattern;  ///<Pattern order of convex component in  man's geometry level.

#include <El/Modules/modules.hpp>

INIT_MODULE(Soldier, 10)
{
  // Register activities
  ActivityGetIn::RegisterType();
  ActivityGetOut::RegisterType();
  ActivityUIAction::RegisterType();
};

void ManCompact()
{
  MovesTypesMan.RemoveNulls();
  MovesTypesMan.Compact();
  MovesTypesGestureMan.RemoveNulls();
  MovesTypesGestureMan.Compact();
}

void Man::ClassCleanUp()
{
  void PerformAnimationRTCleanUp();
  PerformAnimationRTCleanUp();
  AnimationRTBank.Clear();
  MovesTypesMan.Clear();
  MovesTypesGestureMan.Clear();
#if _ENABLE_CONVERSATION
  TalkTopics.Clear();
#endif
}


ActionContextBase * ActionContextBase::CreateObject(ParamArchive &ar)
{
  RString type;
  if (ar.Serialize("type", type, 1, "default") != LSOK) return NULL;
  if (stricmp(type, "default") == 0) return new ActionContextDefault();
  if (stricmp(type, "actionHandler") == 0) return new PlayActionHandlerContext();
  if (stricmp(type, "animation") == 0) return new ActionContextAnimation();
  if (stricmp(type, "throw") == 0) return new ActionContextThrow();
  if (stricmp(type, "takeinhands") == 0) return new ActionContextTakeInHands();
  if (stricmp(type, "reload") == 0) return new ActionContextReload();
  if (stricmp(type, "closedoor") == 0) return new ActionContextCloseDoor();
  if (stricmp(type, "allowgetout") == 0) return new ActionContextAllowGetOut();
  if (stricmp(type, "drawbow") == 0) return new ActionContextDrawBow();

  ErrF("Unknown action context type: %s", cc_cast(type));
  return NULL;
}


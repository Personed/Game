#if _PROFILE
# pragma optimize( "", off )
#endif

/*!
\file
implementation for Transport class
*/
#include "wpch.hpp"
#include "transport.hpp"
#include "helicopter.hpp"
#include "AI/ai.hpp"
#include "proxyWeapon.hpp"
#include "person.hpp"
#include "global.hpp"
#include "world.hpp"
#include "landscape.hpp"
#include <El/Common/randomGen.hpp>
#include "keyInput.hpp"
#include "diagModes.hpp"
#include "soldierOld.hpp"
#include "shots.hpp"

#include "Marina/carCollisions.hpp" // new physics

#include "paramArchiveExt.hpp"
#include "stringtableExt.hpp"
#include "sentences.hpp"
#include "camera.hpp"
#include "fileLocator.hpp"

#include "Network/network.hpp"
#include "UI/chat.hpp"
#include "UI/uiActions.hpp"
#include "integrity.hpp"

#include "Shape/specLods.hpp"

#include <El/Enum/enumNames.hpp>
#include <El/Common/perfProf.hpp>

#include "txtPreload.hpp"

#include "mbcs.hpp"
#include "progress.hpp"

#if _VBS2 // interact with vehs
#include "gameStateExt.hpp"

enum ExtCtrl {EC_NONE, EC_HLA, EC_AAR};
#endif

#if _VBS3
#include "Hla/AAR.hpp"
#endif

//static const float CmdPlanDist = 100.0f;
//static const float CmdMinDist = 40.0f;
static const float CmdPlanDist = 30.0f;
static const float CmdMinDist = 10.0f;

const float DriverReactionTime=0.3;

// common implementation of transport vehicles

DEFINE_CASTING(Transport)

/*
static float TransportGetFieldCost(int x, int z, void *param)
{
  if (!param) return SET_UNACCESSIBLE;
  Transport *vehicle = (Transport *)param;
  //return subgrp->GetFieldCost(x, z);

  if( !InRange(x,z) ) return SET_UNACCESSIBLE;
  GeographyInfo geogr = GLOB_LAND->GetGeography(x,z);

  float cost = vehicle->GetCost(geogr)*vehicle->GetFieldCost(geogr);
  cost *= LandGrid; // cost = time for distance == LandGrid
  return cost;
}
*/

#pragma warning(disable:4355)

#define DIAG_TRANSPORT 0
#define DIAG_SOUND_SAMPLE 0

bool HasPrimaryGunnerTurret(ParamEntryPar cls)
{
  ParamEntryVal turrets = cls >> "Turrets";
  for (int i=0; i<turrets.GetEntryCount(); i++)
  {
    ParamEntryVal entry = turrets.GetEntry(i);
    if (!entry.IsClass())
      continue;
    bool hasGunner = entry >> "primaryGunner";
    if (hasGunner)
      return true;
    if (HasPrimaryGunnerTurret(entry))
      return true;
  }
  return false;
}

bool HasPrimaryObserverTurret(ParamEntryPar cls)
{
  ParamEntryVal turrets = cls >> "Turrets";
  for (int i=0; i<turrets.GetEntryCount(); i++)
  {
    ParamEntryVal entry = turrets.GetEntry(i);
    if (!entry.IsClass())
      continue;
    bool hasObserver = entry >> "primaryObserver";
    if (hasObserver)
      return true;
    if (HasPrimaryObserverTurret(entry))
      return true;
  }
  return false;
}

TransportType::TransportType( ParamEntryPar param )
:base(param), _camShakeCoef(CameraShakeManager::DefaultVehicleAttenuationCoef)
#if _ENABLE_HAND_IK
  ,_driverLeftHandIndex(SkeletonIndexNone),_driverRightHandIndex(SkeletonIndexNone)
  ,_driverLeftLegIndex(SkeletonIndexNone),_driverRightLegIndex(SkeletonIndexNone)
#endif
{
}

/// Functor checks if some gunner is better commander than driver
class GunnerIsCommander : public ITurretTypeFunc
{
public:
  virtual bool operator () (const EntityAIType *entityType, const TurretType &turretType, TurretPath &path) const
  {
    return turretType._commanding > 0;
  };
};

/// Functor checks if some turret has primary gunner flag set
class HasPrimaryGunner : public ITurretTypeFunc
{
public:
  virtual bool operator () (const EntityAIType *entityType, const TurretType &turretType, TurretPath &path) const
  {
    return turretType._primaryGunner;
  };
};

/// Functor checks if some turret has primary observer flag set
class HasPrimaryObserver : public ITurretTypeFunc
{
public:
  virtual bool operator () (const EntityAIType *entityType, const TurretType &turretType, TurretPath &path) const
  {
    return turretType._primaryObserver;
  };
};


/* used as key for submix groups */
struct SubmixKey
{
  float _vol;
  float _low;
  float _up;
  bool _useCone;
  SoundCone _sCone;

  SubmixKey():_vol(0), _low(0), _up(0), _useCone(false) {}
  SubmixKey(float vol, SoundCone *sCone = NULL)
    : _vol(floatMax(vol, 1e-5)), _useCone(sCone ? true : false)
  {
    if (sCone)
      _sCone = *sCone;

    static float Log10 = logf(10.0f);
    static float dBRange = 2.5f;

    float db = 20.0f * logf(vol) / Log10;
    _low = powf(10.0f, (db - dBRange) * (1.0f/20));
    _up = powf(10.0f, (db + dBRange) * (1.0f/20));
  }

  SubmixKey& operator= (const SubmixKey &key)
  {
    _vol = key._vol;
    _low = key._low;
    _up = key._up;
    _useCone = key._useCone;
    _sCone = key._sCone;
    
    return *this;
  }

  bool operator== (const SubmixKey &key) const
  {    
    if (_useCone && key._useCone)
      return (_sCone == key._sCone && ((_vol >= key._low && _vol <= key._up) || (key._vol >= _low && key._vol <= _up)));
    else
    {
      if (!_useCone && !key._useCone)
        return ((_vol >= key._low && _vol <= key._up) || (key._vol >= _low && key._vol <= _up));
      else
        return false;
    }
  }
};

TypeIsSimple(SubmixKey)

template<class Type>
struct BaseSubmixGroup
{
  SubmixKey _key;
  AutoArray<Type> _sounds;

  BaseSubmixGroup(): _sounds(), _key() {}
  virtual ~BaseSubmixGroup() { _sounds.Clear(); }
};

/* struct used for temporary holding audio controllers groups */
struct SubmixGroup: public BaseSubmixGroup<SoundParsEx>
{
  typedef BaseSubmixGroup<SoundParsEx> base;
  SubmixGroup(): base() {}
};

TypeIsMovable(SubmixGroup)

/* struct used for temporary audio controllers grouping */
struct SubmixEventGroup: public BaseSubmixGroup<SoundEventPars>
{
  typedef BaseSubmixGroup<SoundEventPars> base;
  SubmixEventGroup(): base() {}
};

TypeIsMovable(SubmixEventGroup)

// used for sorting waves into submix groups
template<>
struct FindArrayKeyTraits<SubmixGroup>
{
  typedef const SubmixKey &KeyType;
  static bool IsEqual(KeyType a, KeyType  b) { return (a == b); }
  static KeyType GetKey(const SubmixGroup &sg) { return sg._key; }
};

// used for sorting event waves into submix groups
template<>
struct FindArrayKeyTraits<SubmixEventGroup>
{
  typedef const SubmixKey &KeyType;
  static bool IsEqual(KeyType a, KeyType  b) { return (a == b); }
  static KeyType GetKey(const SubmixEventGroup &esg) { return esg._key; }
};

inline static bool LoadSoundConePars(const ParamEntryVal &ie, SoundCone &cone)
{
  ConstParamEntryPtr coneEntry = ie.FindEntry("cone");
  if (coneEntry.NotNull())
  {
    if (coneEntry->IsArray() && coneEntry->GetSize() == 4)
    {
      cone._angleIn = (*coneEntry)[0];
      cone._angleOut = (*coneEntry)[1];
      cone._volIn = (*coneEntry)[2];
      cone._volOut = (*coneEntry)[3];
      return true;
    }
  }
  else
  {
    memset(&cone, 0, sizeof(SoundCone));
  }

  return false;
}

/// controllers list
static const RString SoundCtrlNames[]=
{
  "rpm", "randomizer", "speed", "thrust", "camPos", "engineOn", "rotorSpeed", "rotorThrust",  "angVelocity",
  "gmeterZ", "roughness", "dustness", "damper0", "rock", "sand", "grass", "mud", "gravel", "asphalt",
  "gear", "flaps", "rotPos", "water"
#if _HELISIM_SOUNDS    
  , "apu", "batteries", "rpmStarter", "rpm1", "rpmStarter1", "motorDamage", "transmissionDamage", "rotorBrake"
#endif
};

/*!
\patch 5500 Date 10/8/2007 by Jirka
- New: Different get in / out animations for different positions
*/

void TransportType::Load(ParamEntryPar par, const char *shape)
{
  base::Load(par,shape);

#define GET_PAR(x) _##x=par>>#x

  GET_PAR(getInRadius);

  GET_PAR(hasDriver);
  GET_PAR(driverForceOptics);

  GET_PAR(crewVulnerable);
  GET_PAR(enableManualFire);

  _crew = par >> "crew";
  _driverAction = ExtractActionName(par >> "driverAction");
  _driverInAction = ExtractActionName(par >> "driverInAction");
  _driverGetInAction = ExtractActionName(par >> "getInAction");
  _driverGetOutAction = ExtractActionName(par >> "getOutAction");

  _preciseGetInOut =(GetInOutMode)(int)(par >> "preciseGetInOut");

  _wheelCircumference=par>>"wheelCircumference";

  ParamEntryVal cargoOnProxy=par>>"cargoPreciseGetInOut";
  _cargoPreciseGetInOut.Resize(cargoOnProxy.GetSize());
  for (int i=0; i< cargoOnProxy.GetSize(); i++)
  {
    _cargoPreciseGetInOut[i] = (GetInOutMode)(int) cargoOnProxy[i];
  }

  _maxManCargo=par>>"transportSoldier";
  //_maxVehicleCargo=par>>"transportVehiclesCount";
  //_maxVehicleCargoMass=par>>"transportVehiclesMass";
  _typicalCargo.Resize(0);
  ParamEntryVal typCargo=par>>"typicalCargo";
  for (int i=0; i<typCargo.GetSize(); i++)
  {
    RString name=typCargo[i];
    Ref<const EntityType> vType=VehicleTypes.New(name);
    const EntityAIType *type=dynamic_cast<const EntityAIType *>(vType.GetRef());
    if (!type)
      continue;
    _typicalCargo.Add(const_cast<EntityAIType *>(type));
  }
  _typicalCargo.Compact();


  _cargoProxyIndexes.Resize(0);
  ParamEntryVal proxyIndexes=par>>"cargoProxyIndexes";
  int size = proxyIndexes.GetSize();
  _cargoProxyIndexes.Realloc(size);
  _cargoProxyIndexes.Resize(size);

  for (int i=0; i<size; ++i)
  {
    _cargoProxyIndexes[i] = proxyIndexes[i];
  }
  _cargoProxyIndexes.Compact();

#if _ENABLE_REPORT
  int max = 0;
  for (int i=0; i<size; ++i)
  {
    if (_cargoProxyIndexes[i]<1)
      RptF("Error: %s\\cargoProxyIndexes[%d] value is %d, need to be > 0 ",cc_cast(par.GetName()),i,_cargoProxyIndexes[i]);
    if (_cargoProxyIndexes[i]> max)
      max = _cargoProxyIndexes[i];
  }
  if (size>0 && _cargoProxyIndexes[size-1]!=max && _maxManCargo > size)
    RptF("Error: last value in %s\\cargoProxyIndexes is not the biggest in array ",cc_cast(par.GetName()),size-1,_cargoProxyIndexes[size-1]);
#endif

  ParamEntryVal cargo = par >> "cargoAction";
  _cargoAction.Realloc(cargo.GetSize());
  _cargoAction.Resize(cargo.GetSize());
  for (int i=0; i<cargo.GetSize(); i++)
    _cargoAction[i] = ExtractActionName(cargo[i]);
  cargo = par >> "cargoGetInAction";
  _cargoGetInAction.Realloc(cargo.GetSize());
  _cargoGetInAction.Resize(cargo.GetSize());
  for (int i=0; i<cargo.GetSize(); i++)
    _cargoGetInAction[i] = ExtractActionName(cargo[i]);
  cargo = par >> "cargoGetOutAction";
  _cargoGetOutAction.Realloc(cargo.GetSize());
  _cargoGetOutAction.Resize(cargo.GetSize());
  for (int i=0; i<cargo.GetSize(); i++)
    _cargoGetOutAction[i] = ExtractActionName(cargo[i]);

  GET_PAR(driverCompartments);
  ParamEntryVal cargoCompartments = par >> "cargoCompartments";
  _cargoCompartments.Realloc(cargoCompartments.GetSize());
  _cargoCompartments.Resize(cargoCompartments.GetSize());
  for (int i=0; i<cargoCompartments.GetSize(); i++)
    _cargoCompartments[i] = cargoCompartments[i];

  GET_PAR(hideProxyInCombat);
  GET_PAR(forceHideDriver);

#if _VBS3 //added optional parameter _viewDriverInExternal
  _viewDriverInExternal = par.ReadValue("viewDriverInExternal", false);
#endif

  GET_PAR(unloadInCombat);
  GET_PAR(insideSoundCoef);
  GET_PAR(outsideSoundFilter);
  GET_PAR(occludeSoundsWhenIn);
  GET_PAR(obstructSoundsWhenIn);

  _driverOpticsColor = GetPackedColor(par >> "driverOpticsColor");

  _viewCargo.Load(par>>"ViewCargo");
  _viewOptics.Load(par>>"ViewOptics");

  ParamEntryPtr cargoSpecs = par.FindEntry("CargoSpec");
  if (cargoSpecs && cargoSpecs->IsClass())
  {
    int n = cargoSpecs->GetEntryCount();
    _cargoSpec.Realloc(n);
    _cargoSpec.Resize(n);
    int index = 0;
    for (int i=0; i<n; i++)
    {
      RString cargoLabel = Format("Cargo%d", i+1); //start from 1
      ParamEntryPtr cargo = cargoSpecs->FindEntry(cargoLabel);
      if (!cargo || !cargo->IsClass()) continue;
      _cargoSpec[index++].Load(*cargo);
    }
    _cargoSpec.Resize(index);
  }

  GET_PAR(viewDriverShadow);
  GET_PAR(viewDriverShadowDiff);
  GET_PAR(viewDriverShadowAmb);
  GET_PAR(viewCargoShadow);
  GET_PAR(viewCargoShadowDiff);
  GET_PAR(viewCargoShadowAmb);
  GET_PAR(ejectDeadDriver);
  GET_PAR(ejectDeadCargo);
  GET_PAR(hideWeaponsDriver);
  GET_PAR(hideWeaponsCargo);

  _lockTargetAction = UAVehLockTargets;
  _lockEmptyTargetAction = UAVehLockEmptyTargets;

#if _ENABLE_HAND_IK // hand IK 
  GET_PAR(driverLeftHandAnimName);
  _driverLeftHandAnimName.Lower();
  GET_PAR(driverRightHandAnimName);
  _driverRightHandAnimName.Lower();

  GET_PAR(driverLeftLegAnimName);
  _driverLeftLegAnimName.Lower();
  GET_PAR(driverRightLegAnimName);
  _driverRightLegAnimName.Lower();
#endif

#undef GET_PAR

  // load MFD description
  ParamEntryVal mfdList = par>>"MFD";
  _mfd.Realloc(mfdList.GetEntryCount());
  for (int i=0; i<mfdList.GetEntryCount(); i++)
  {
    ParamEntryVal entry = mfdList.GetEntry(i);
    if (!entry.IsClass())
      continue;
    MFDDesc &mfd = _mfd.Append();
    mfd.Load(entry);
  }
  _mfd.Compact();

  // Turrets
  _turrets.Resize(0);
  ParamEntryVal turrets = par >> "Turrets";
  for (int i=0; i<turrets.GetEntryCount(); i++)
  {
    ParamEntryVal entry = turrets.GetEntry(i);
    if (!entry.IsClass())
      continue;
    _turrets.Add(new TurretType(entry));
  }
  _turrets.Compact();

  {
    HasPrimaryGunner func;
    _hasGunner = ForEachTurret(func);
  }
  {
    HasPrimaryObserver func;
    _hasObserver = ForEachTurret(func);
  }
  {
    // if there are no gunners with higher commanding level, driver is commander
    GunnerIsCommander func;
    _driverIsCommander = !ForEachTurret(func);
  }

  /// audio controllers
  LoadSubmixGroups(par, "Sounds", _submixPars);

  // used for dividing samples into submix groups
  FindArrayKey< SubmixEventGroup, FindArrayKeyTraits< SubmixEventGroup > > tmpEventGroups;

  /// audio events
  ParamEntryVal eventEntry = par >> "SoundEvents";

  if (eventEntry.IsClass())
  {
    for (ParamClass::Iterator<> i(eventEntry.GetClass()); i; ++i)
    {
      const ParamEntryVal &ie = *i;

      // each sound event component is sub class of sound event class
      if (!ie.IsClass())
        continue;

      SoundEventPars eventPars;

      // sound[]={"path",base_volume,base_frequency};
      GetValue(eventPars._pars, ie >> "sound");

      eventPars._started = false;
      eventPars._limit = ie >> "limit";
      RString expText = ie >> "expression";

      // expression compilation
      bool result = eventPars._expression.Compile(expText, Transport::SoundControllers(), Transport::SoundControllersCount());
      Assert(result);
      if (!result)
      {
        RptF("Error: Sound event expression: %s", cc_cast(expText));
        continue;
      }

      eventPars._coneUsed = LoadSoundConePars(ie, eventPars._cone);

      // create key
      SubmixKey key(eventPars._pars.vol, eventPars._coneUsed ? &eventPars._cone : NULL);

      // exist
      int index = tmpEventGroups.FindKey(key);

      if (index < 0)
      {
        // create group
        SubmixEventGroup &sgTmp = tmpEventGroups.Append();
        sgTmp._key = key;
        sgTmp._sounds.Append(eventPars);
      }
      else
      {
        // append into existing group
        tmpEventGroups[index]._sounds.Append(eventPars);
      }
    }
  }

  // copy groups
  if (tmpEventGroups.Size() != 0)
  {
    _submixEventPars.Realloc(tmpEventGroups.Size());
    _submixEventPars.Resize(tmpEventGroups.Size());

    for (int i = 0; i < tmpEventGroups.Size(); i++)
    {
      SubmixEventPars &sep = _submixEventPars[i];
      // copy ref
      sep._sEventPars.Append(tmpEventGroups[i]._sounds);
    }
  }

  // read camera shake params
  _camShakeCoef = CameraShakeManager::DefaultVehicleAttenuationCoef;
  ParamEntryPtr camShakePar = par.FindEntry("camShakeCoef");
  if (camShakePar)
  {
    _camShakeCoef = *camShakePar;
  }
  camShakePar = par.FindEntry("CamShake");
  if (camShakePar)
  {
    _camShake.Load(*camShakePar);
  }
}

void TransportType::LoadSubmixGroups(ParamEntryPar par, RString className, AutoArray<SubmixPars> &submixPars)
{
  ParamEntryVal entry = par >> className;

  if (!entry.IsClass()) return;

  FindArrayKey< SubmixGroup, FindArrayKeyTraits< SubmixGroup > > tmpGroups;

  if (entry.IsClass() )
  {
    for (ParamClass::Iterator<> i(entry.GetClass()); i; ++i)
    {
      const ParamEntryVal &ie = *i;

      // each sound component is sub class of sound class
      if (!ie.IsClass())
        continue;

      SoundParsEx sParsEx;

      ConstParamEntryPtr sItem = ie.FindEntry("sound");
      if (sItem)
      {
        if (sItem->IsArray())
        {
          // load sound base parameters: sound[]={"path",base_volume,base_frequency};
          GetValue(sParsEx._pars, ie >> "sound");
        }
        else
        {
          // ensure compatibility, sound source and values are set in var. soundEngine and soundEnviron
          RString text = ie >> "sound";
          GetValue(sParsEx._pars, par >> text);
        }
      }

      if (sParsEx._pars.name.IsEmpty()) continue;

      // load frequency expression: frequency = "rpm*A";
      RString freqText = ie >> "frequency";
      // load volume expression: volume = "rpm*B+speed*C";
      RString volText = ie >> "volume";

      bool result = sParsEx._freqExpr.Compile(freqText, Transport::SoundControllers(), Transport::SoundControllersCount());
      Assert(result);
      if (!result)
      {
        RptF("Error: Sound frequency expression: %s", cc_cast(freqText));
        continue;
      }

      result = sParsEx._volExpr.Compile(volText, Transport::SoundControllers(), Transport::SoundControllersCount());
      Assert(result);
      if (!result)
      {
        RptF("Error: Sound volume expression: %s", cc_cast(volText));
        continue;
      }

      sParsEx._coneUsed = LoadSoundConePars(ie, sParsEx._cone);

      // create key
      SubmixKey key(sParsEx._pars.vol, sParsEx._coneUsed ? &sParsEx._cone : NULL);

      // exist
      int index = tmpGroups.FindKey(key);

      if (index < 0)
      {
        // create group
        SubmixGroup &sgTmp = tmpGroups.Append();
        sgTmp._key = key;
        sgTmp._sounds.Append(sParsEx);
      }
      else
      {
        // insert into existing group
        tmpGroups[index]._sounds.Append(sParsEx);
      }
    }
  }

  if (tmpGroups.Size() != 0)
  {
    // copy groups
    submixPars.Realloc(tmpGroups.Size());
    submixPars.Resize(tmpGroups.Size());

    for (int i = 0; i < tmpGroups.Size(); i++)
    {
      SubmixPars &spa = submixPars[i];
      // copy ref
      spa.Append(tmpGroups[i]._sounds);
    }
  }
}

#include <Es/Common/delegate.hpp>

/*!
\patch 5161 Date 5/31/2007 by Ondra
- New: Animation controllers gmeterx, gmetery, gmeterz
*/
AnimationSource *TransportType::CreateAnimationSource(const AnimationType *type, RStringB source)
{
  static const struct AnimSourceDesc
  {
    const char *name;
    float (Transport::*func)(ObjectVisualState const&) const;
  } desc[]=
  {
    {"altRadar",&Transport::GetCtrlAltRadar},
    {"altBaro",&Transport::GetCtrlAltBaro},
    {"speed",&Transport::GetCtrlSpeedHorizontal},
    {"vertSpeed",&Transport::GetCtrlSpeedVertical},
    {"rpm",&Transport::GetCtrlRPM},
    {"direction",&Transport::GetCtrlDirection},
    {"waypointDirection",&Transport::GetCtrlWaypointDirection},
    {"turretDir",&Transport::GetCtrlTurretDirection},  // TODO
    {"hatchDriver",&Transport::GetCtrlDriverHatchPos},
    {"drivingWheel",&Transport::GetCtrlDrivingWheelPos},
    {"horizonBank",&Transport::GetCtrlHorizonBank},
    {"horizonDive",&Transport::GetCtrlHorizonDive},
    {"fuel",&Transport::GetCtrlFuel},
    {"gmeter",&Transport::GetCtrlGMeter},
    {"gmeterx",&Transport::GetCtrlGMeterX},
    {"gmetery",&Transport::GetCtrlGMeterY},
    {"gmeterz",&Transport::GetCtrlGMeterZ},
  };
  for (int i=0; i<sizeof(desc)/sizeof(desc[0]); i++)
  {
    if (!strcmpi(source,desc[i].name))
      return _animSources.CreateAnimationSource(desc[i].func);
  }

  // check if some turret uses this animation source
  // TODO: add turrets to visual state
  AutoArray<int, MemAllocLocal<int, 16> > path;
  for (int i=0; i<_turrets.Size(); i++)
  {
    path.Resize(1);
    path[0] = i;
    AnimationSource *result = _turrets[i]->CreateAnimationSource(source, path);
    if (result)
      return result;
  }
  return base::CreateAnimationSource(type,source);
}

AnimationSource *TransportType::CreateAnimationSourcePar(const AnimationType *type, ParamEntryPar entry)
{
  RStringB sourceType = entry>>"source";
  if (!strcmpi(sourceType,"door"))
  {
    //return new AnimationSourceDoor(entry.GetName(),VehicleTypes.New(weapon));
    return _doors.CreateAnimationSource(entry);
  }
  /// argument not known, call a non-argument version
  return base::CreateAnimationSourcePar(type, entry);
}

void TransportType::AddProxy(ManProxy &mProxy, const ProxyObjectTyped &proxy, const ProxyObject &proxyObj)
{
  //LogF("%s: add proxy %s",(const char *)GetName(),(const char *)anim);
  Matrix4 &trans=mProxy.transform;
  trans = proxy.obj->FutureVisualState().Transform();
  LODShape *pshape = proxy.obj->GetShape();
  // reset position BC correction
  Vector3 offset = VZero;
  if (pshape) offset = -pshape->BoundingCenter();
  trans.SetPosition(trans.FastTransform(offset));
  // rotate around y-axis (shape is reversed)
  trans.SetOrientation(trans.Orientation() * Matrix3(MScale,-1,1,-1));
  mProxy._proxy = &proxyObj;
}

class ScanWeaponRange: public ITurretTypeFunc
{
  float &_maxRange;
public:
  explicit ScanWeaponRange(float &maxRange):_maxRange(maxRange){}

  bool operator () (const EntityAIType *entityType, const TurretType &turretType, TurretPath &path) const
  {
    float maxRange = turretType._weapons.GetMaxRange();
    saturateMax(_maxRange,maxRange);
    return false;
  }
};

float TransportType::GetMaxWeaponRange() const
{
  float maxRange = base::GetMaxWeaponRange();
  // consider all turrets as well
  ScanWeaponRange scanWeaponRange(maxRange);
  ForEachTurret(scanWeaponRange);
  return maxRange;
}


#include <Es/Files/filenames.hpp>

static bool NameInList( ParamEntryPar cfg, const char *name )
{
  for (int i=0; i<cfg.GetSize(); i++)
  {
    RStringB val = cfg[i];
    if (!strcmpi(val,name))
      return true;
  }
  return false;
}

size_t TransportType::GetMemoryControlled() const
{
  // who knows how much memory is controlled?
  return sizeof(*this);
}

static void LoadGetInPoints(LODShape *shape, const Shape *memory, RString namedSel, GetInPoints &points)
{
  int index = shape->FindNamedSel(memory, namedSel);
  if (index >= 0)
  {
    const NamedSelection::Access sel(memory->NamedSel(index),shape,memory);
    for (int i=0; i<sel.Size(); i++)
    {
      if (sel[i] >= 0)
      {
        GetInPointIndex& pt = points.Append();
        pt._pos = sel[i];
        pt._dir = -1;
      }
    }
  }
}

static bool LoadGetInPointsDir(LODShape *shape, const Shape *memory, RString namedSel, GetInPoints &points)
{
  bool ok = true;

#define GET_CLOSEST_POINT(array, imin, pt) \
  { \
    float min = FLT_MAX; \
    for(int j = 0; j < (array).Size(); j++) \
    { \
      float dist = ((pt) - memory->Pos((array)[j]._pos)).SquareSize(); \
      if (dist < min) { min = dist; (imin) = j;}; \
    } \
  };

  int index = shape->FindNamedSel(memory, namedSel);
  if (index >= 0)
  {
    const NamedSelection::Access sel(memory->NamedSel(index),shape,memory);
    for (int i=0; i<sel.Size(); i++)
    {
      if (sel[i] >= 0)
      {
        int imin = -1;
        Vector3 dirpt = memory->Pos(sel[i]);
        GET_CLOSEST_POINT(points, imin, dirpt);

        if (imin>=0)
        {
          GetInPointIndex& pt = points[imin];
          pt._dir = sel[i];
        }
        else
          ok = false;
      }
    }
  }
  return ok;
}

void TransportType::InitShape()
{
  base::InitShape();

  _proxies.Init(_shape->NLevels());

  const ParamEntry &par = *_par;

  _squadTitles.Init(_shape, par >> "SquadTitles", GetFontID(Pars >> "fontClanName"));

  const Shape *memory = _shape->MemoryLevel();
  if (memory)
  {
    ParamEntryVal entry = par >> "memoryPointDriverOptics";
    if (entry.IsArray())
    {
      for (int i=0; i<entry.GetSize(); i++)
      {
        _driverOpticsPos = _shape->FindNamedSel(memory,entry[i].operator RString());
        if (_driverOpticsPos >= 0)
          break;
      }
    }
    else
      _driverOpticsPos = _shape->FindNamedSel(memory,(RString)entry.operator RString());
  }
  else
    _driverOpticsPos = -1;

  RStringB oModelName = par >> "driverOpticsModel";
  _driverOpticsModel = oModelName.GetLength() > 0 ? Shapes.New(::GetShapeName(oModelName), true, false) : NULL;
  if (_driverOpticsModel && _driverOpticsModel->NLevels() > 0)
  {
    _driverOpticsModel->Level(0)->MakeCockpit();
    _driverOpticsModel->OrSpecial(BestMipmap);
  }

  ParamEntryVal cfgOpticsPars = Pars >> "CfgOpticsEffect";

  ParamEntryVal optEntry = par >> "driverOpticsEffect";
  if (optEntry.IsArray())
  {
    int size = optEntry.GetSize();

    _opticsType.Realloc(size);
    _opticsType.Resize(size);

    for (int i = 0; i < size; i++)
    {
      // effect type
      RString name = optEntry[i];
      if (cfgOpticsPars.FindEntry(name))
      {
        ParamEntryVal eEntry = cfgOpticsPars >> name;
        if (eEntry.IsClass())
          _opticsType[i].Load(eEntry);
      }
    }
  }

  _driverGetInPos.Resize(0);
  _cargoGetInPos.Resize(0);

#if _HELISIM_PRECISE_INTERACTION
  _driverGetInPosPrecise.Resize(0);
  _cargoGetInPosPrecise.Resize(0);
#endif

  if (memory)
  {
    LoadGetInPoints(_shape, memory, par >> "memoryPointsGetInDriver", _driverGetInPos);
    if (!LoadGetInPointsDir(_shape, memory, par >> "memoryPointsGetInDriverDir", _driverGetInPos))
    {
      RptF("No get in driver point in %s (%s)", cc_cast(_shape->GetName()), cc_cast(GetName()));
    }
    // verify that all get in/out points has direction, if not add default value
    for(int i = 0; i < _driverGetInPos.Size(); i++)
      if (_driverGetInPos[i]._dir < 0)
        RptF("In Vehicle: %s missing driver get in direction point", _shape->Name());

    ParamEntryVal cargoEntry = par >> "memoryPointsGetInCargo";
    if (cargoEntry.IsArray())
    {
      // new style of definition (separate for each cargo position)
      int nPos = cargoEntry.GetSize();
      ParamEntryVal cargoEntryDir = par >> "memoryPointsGetInCargoDir";
      int nDir = cargoEntryDir.GetSize();

      _cargoGetInPos.Realloc(nPos);
      _cargoGetInPos.Resize(nPos);
      for (int i=0; i<nPos; i++)
      {
        GetInPoints &cargoPoints = _cargoGetInPos[i];
        LoadGetInPoints(_shape, memory, cargoEntry[i], cargoPoints);
        if (nDir > 0)
        {
          int iDir = intMin(i, nDir - 1);
          if (!LoadGetInPointsDir(_shape, memory, cargoEntryDir[iDir], cargoPoints))
            RptF("No get in cargo %d point in %s (%s)", i, cc_cast(_shape->GetName()), cc_cast(GetName()));
        }
        for(int i = 0; i < cargoPoints.Size(); i++)
        {
          if (cargoPoints[i]._dir < 0)
            RptF("In Vehicle: %s missing cargo %d get in direction point", _shape->Name(), i);
        }
        cargoPoints.Compact();
      }
    }
    else
    {
      GetInPoints cargoPoints, coDriverPoints;

      // get in points for cargo
      LoadGetInPoints(_shape, memory, cargoEntry, cargoPoints);
      if (!LoadGetInPointsDir(_shape, memory, par >> "memoryPointsGetInCargoDir", cargoPoints))
        RptF("No get in cargo point in %s (%s)", cc_cast(_shape->GetName()), cc_cast(GetName()));
      for(int i = 0; i < cargoPoints.Size(); i++)
      {
        if (cargoPoints[i]._dir < 0)
          RptF("In Vehicle: %s missing cargo get in direction point", _shape->Name());
      }
      cargoPoints.Compact();

      // get in points for coDriver
      LoadGetInPoints(_shape, memory, par >> "memoryPointsGetInCoDriver", coDriverPoints);
      if (!LoadGetInPointsDir(_shape, memory, par >> "memoryPointsGetInCoDriverDir", coDriverPoints))
        RptF("No get in co-driver point in %s (%s)",cc_cast(_shape->GetName()),cc_cast(GetName()));
      for(int i = 0; i < coDriverPoints.Size(); i++)
      {
        if (coDriverPoints[i]._dir < 0)
          RptF("In Vehicle: %s missing codriver get in direction point", _shape->Name());
      }
      coDriverPoints.Compact();

      // distribute points to cargo positions
      ParamEntryVal cargoCoDriver = par >> "cargoIsCoDriver";
      int n = cargoCoDriver.GetSize();
      if (n == 0)
      {
        // all positions are cargo
        _cargoGetInPos.Realloc(1);
        _cargoGetInPos.Resize(1);
        _cargoGetInPos[0] = cargoPoints;
      }
      else
      {
        _cargoGetInPos.Realloc(n);
        _cargoGetInPos.Resize(n);
        for (int i=0; i<n; i++)
        {
          bool coDriver = cargoCoDriver[i];
          if (coDriver)
            _cargoGetInPos[i] = coDriverPoints;
          else
            _cargoGetInPos[i] = cargoPoints;
        }
      }
    }
  }

  if (_driverGetInPos.Size() == 0)
  {
    LogF("Type %s missing driver position", (const char *)GetName());

    // default fall back
    GetInPointIndex& pt = _driverGetInPos.Append();
    pt._pos = -1;
    pt._dir = -1;
  }
  _driverGetInPos.Compact();

  if (_cargoGetInPos.Size() == 0)
  {
    // if there is no cargo points, use driver point instead
    _cargoGetInPos.Realloc(1);
    _cargoGetInPos.Resize(1);
    _cargoGetInPos[0] = _driverGetInPos;
  }
  else
  {
    // check points for all cargo slots
    for (int i=0; i<_cargoGetInPos.Size(); i++)
    {
      if (_cargoGetInPos[i].Size() == 0)
        _cargoGetInPos[i] = _driverGetInPos;
    }
  }

#if _HELISIM_PRECISE_INTERACTION
  if (memory && _usePreciseGetInAction)
  {
    //direction is not required for precise actions - only for player
    LoadGetInPoints(_shape, memory, par >> "memoryPointsGetInDriverPrecise", _driverGetInPosPrecise);

    ParamEntryVal cargoEntryPrecise = par >> "memoryPointsGetInCargoPrecise";
    if (cargoEntryPrecise.IsArray())
    {
      // new style of definition (separate for each cargo position)
      int nPos = cargoEntryPrecise.GetSize();

      _cargoGetInPosPrecise.Realloc(nPos);
      _cargoGetInPosPrecise.Resize(nPos);
      for (int i=0; i<nPos; i++)
      {
        GetInPoints &cargoPoints = _cargoGetInPosPrecise[i];
        LoadGetInPoints(_shape, memory, cargoEntryPrecise[i], cargoPoints);
        cargoPoints.Compact();
      }
    }
    else
    {
      RptF("%s: precise action supports cargo points only as array", _shape->Name());
    }
  }

  if (_driverGetInPosPrecise.Size() == 0)
  {
    if(_usePreciseGetInAction) LogF("Type %s missing driver precise get in position", (const char *)GetName());

    // default fall back
    GetInPointIndex& pt = _driverGetInPosPrecise.Append();
    pt._pos = -1;
    pt._dir = -1;
  }
  _driverGetInPosPrecise.Compact();

  if (_cargoGetInPosPrecise.Size() == 0)
  {
    // if there is no cargo points, use driver point instead
    _cargoGetInPosPrecise.Realloc(1);
    _cargoGetInPosPrecise.Resize(1);
    _cargoGetInPosPrecise[0] = _driverGetInPosPrecise;
  }
  else
  {
    // check points for all cargo slots
    for (int i=0; i<_cargoGetInPosPrecise.Size(); i++)
    {
      if (_cargoGetInPosPrecise[i].Size() == 0)
        _cargoGetInPosPrecise[i] = _driverGetInPosPrecise;
    }
  }
#endif

  for (int i=0; i<_mfd.Size(); i++)
    _mfd[i].InitShape(_shape);

  for (int i=0; i<_turrets.Size(); i++)
    _turrets[i]->Init(_shape, this);

#if _HELISIM_RENDER_TO_TEXTURE
  {
    // load render targets description
    ParamEntryVal targetsEntry = par >> "RenderTargets";

    int entryCount = targetsEntry.GetEntryCount();
    for (int i = 0; i < entryCount; i++)
    {
      const ParamEntryVal &ie = targetsEntry.GetEntry(i);

      R2TTgtDescription &tgtDesc = _r2tTgtDesc.Append();
      bool loadResult = tgtDesc.Load(ie);

      if (!loadResult)
        _r2tTgtDesc.Delete(_r2tTgtDesc.Size() - 1);
    }
    _r2tTgtDesc.Compact();

    // assign memory points
    const LODShape *lShape = GetShape();
    if (lShape)
    {
      for (int i = 0; i < _r2tTgtDesc.Size(); i++)
      {
        AutoArray<R2TCameraDescription> &camDesc = _r2tTgtDesc[i]._camDesc;

        for (int j = 0; j < camDesc.Size(); j++)
        {
          R2TCameraDescription &cam = camDesc[j];

          if (cam._posName.IsEmpty()) continue;
          DoAssert(cam._posIndex == -1);

          const Shape *memory = lShape->MemoryLevel();
          if (memory && lShape->MemoryPointExists(cam._posName) && lShape->MemoryPointExists(cam._dirName))
          {
            cam._posIndex = lShape->PointIndex(memory, cam._posName);
            cam._dirIndex = lShape->PointIndex(memory, cam._dirName);
          }
          else
          {
            RptF("Render target memory points %s & %s not found.", cc_cast(cam._posName), cc_cast(cam._dirName));
          }
        }
      }
    }

  }
#endif
#if _ENABLE_HAND_IK // hand IK 
  if (!_driverLeftHandAnimName.IsEmpty())
    _driverLeftHandIndex = _shape->GetSkeleton()->FindBone(_driverLeftHandAnimName);
  if (!_driverRightHandAnimName.IsEmpty())
    _driverRightHandIndex = _shape->GetSkeleton()->FindBone(_driverRightHandAnimName);
  if (!_driverLeftLegAnimName.IsEmpty())
    _driverLeftLegIndex = _shape->GetSkeleton()->FindBone(_driverLeftLegAnimName);
  if (!_driverRightLegAnimName.IsEmpty())
    _driverRightLegIndex = _shape->GetSkeleton()->FindBone(_driverRightLegAnimName);

#endif

  // connect animations with positions in vehicle, need to be in InitShape because _animSources must be already loaded
  _driverDoors = _doors.GetDoorIndex((par >> "driverDoor").operator RString());
  ParamEntryVal cargoDoors = par >> "cargoDoors";
  if (cargoDoors.GetSize() > 0)
  {
    _cargoDoors.Realloc(cargoDoors.GetSize());
    _cargoDoors.Resize(cargoDoors.GetSize());
    for (int i=0; i< cargoDoors.GetSize();++i)
    {
      _cargoDoors[i] = _doors.GetDoorIndex((cargoDoors[i]).operator RString());
    }
  }
}

void TransportType::DeinitShape()
{
  _driverOpticsModel.Free();
  // clear point arrays which are no longer valid without a shape
  _driverGetInPos.Clear();
  _cargoGetInPos.Clear();

#if _HELISIM_PRECISE_INTERACTION
  _driverGetInPosPrecise.Clear();
  _cargoGetInPosPrecise.Clear();
#endif

  for (int i=0; i<_turrets.Size(); i++)
    _turrets[i]->Deinit(_shape, this);
  base::DeinitShape();

#if _HELISIM_RENDER_TO_TEXTURE
  _r2tTgtDesc.Clear();
#endif
}

void LevelProxies::Clear()
{
  _driverProxy = ManProxy();
  _cargoProxy.Resize(0);
  _normalProxy.Resize(0);
}

void TransportType::InitShapeLevel(int level)
{
  base::InitShapeLevel(level);

  _squadTitles.InitLevel(_shape,level);

  LevelProxies &proxies=_proxies[level];
  //Shape *shape=_shape->Level(level);
  // clear proxy
  proxies.Clear();

  ShapeUsed shape = _shape->Level(level);

  proxies._normalProxy.Resize(0);

  // convert shape proxies to my proxies
  for( int i=0; i<GetProxyCountForLevel(level); i++ )
  {
    const ProxyObjectTyped &proxy=GetProxyForLevel(level,i);
    const ProxyObject &proxyObj = shape->Proxy(i);
    // first check for ProxyCrew
    ProxyCrew *proxyCrew = dyn_cast<ProxyCrew,Object>(proxy.obj);
    if (proxyCrew)
    {
      // check crew type
      CrewPosition pos = proxyCrew->Type()->GetCrewPosition();
      if (pos==CPDriver)
        AddProxy(proxies._driverProxy,proxy,proxyObj);
      else if (pos==CPCargo)
      {
        int id = proxy.id;
        if (id < 1)
        {
          Fail("Bad cargo proxy index");
          continue;
        }

        // _cargoProxyIndexes
        int index = -1;
        int size = _cargoProxyIndexes.Size();

        if (size==0) // no data, use old indexing 
        {
          index = id - 1;
        }
        else
        {
          for (int j = 0; j<size;++j)
            if (_cargoProxyIndexes[j]==id)
            {
              index= j;
              break;
            }

          if (index == -1 && id > _cargoProxyIndexes[size-1])
          {
            index = id - _cargoProxyIndexes[size-1] + size - 1; // calculate out of limit indexes
          }
        }
        
        if (index == -1) continue;  

        if (proxies._cargoProxy.Size() < (index+1))
          proxies._cargoProxy.Resize(index+1);

        ManProxy &mProxy=proxies._cargoProxy[/*id - 1*/ index ];
        AddProxy(mProxy,proxy,proxyObj);
      }
      continue;
    }

    // try to get vehicle class name
    char shortname[1024];
    Entity *proxyVeh = dyn_cast<Entity,Object>(proxy.obj);
    if (proxyVeh)
    {
      //LogF("%s: veh proxy %s",(const char *)GetName(),shortname);
      strcpy(shortname,proxyVeh->GetName());
    }
    else
    {
      //LogF("%s: obj proxy %s",(const char *)GetName(),shortname);
      if (proxy.obj->GetShape())
        GetFilename(shortname,proxy.obj->GetShape()->Name());
      else
        *shortname=0;
    }
    // check proxy class, if it is always visible, count it
    const EntityType *type = proxy.obj->GetEntityType();
    if (!type || !strcmp(type->_simName, "alwaysshow"))
      proxies._normalProxy.Add(i);
  }
  proxies._cargoProxy.Compact();
  proxies._normalProxy.Compact();

  for (int i=0; i<_turrets.Size(); i++)
    _turrets[i]->InitLevel(_shape, level, this);
}

void TransportType::DeinitShapeLevel(int level)
{
  _squadTitles.DeinitLevel(_shape,level);

  _proxies[level].Clear();

  for (int i=0; i<_turrets.Size(); i++)
    _turrets[i]->DeinitLevel(_shape, level, this);

  base::DeinitShapeLevel(level);
}

bool TransportType::ForEachTurret(ITurretTypeFunc &func) const
{
  TurretPath path;

  int n = _turrets.Size();
  for (int i=0; i<n; i++)
  {
    path.Resize(1);
    path[0] = i;
    if (_turrets[i]->ForEachTurret(this, path, func))
      return true;
  }
  return false;
}

/*!
\patch 5501 Date 6/12/2008 by Jirka
- Fixed: Crash at ArmA.exe:0x56536 - caused by wrong turret state in MP.
*/

const TurretType *TransportType::GetTurret(const int *path, int size) const
{
  if (size <= 0)
  {
    LogF("Empty turret path");
    return NULL;
  }
  int i = path[0];
  if (i < 0 || i >= _turrets.Size())
  {
    LogF("Invalid turret path");
    return NULL;
  }
  return _turrets[i]->GetTurret(path + 1, size - 1);
}

RString TransportType::GetCargoGetInAction(int pos) const
{
  int n = _cargoGetInAction.Size();
  if (n == 0)
    return RString();
  saturate(pos, 0, n - 1);
  return _cargoGetInAction[pos];
}

RString TransportType::GetCargoGetOutAction(int pos) const
{
  int n = _cargoGetOutAction.Size();
  if (n == 0)
    return RString();
  saturate(pos, 0, n - 1);
  return _cargoGetOutAction[pos];
}

GetInOutMode TransportType::CargoPreciseGetInOut(int pos) const
{
  int maxPos = _cargoPreciseGetInOut.Size()-1;
  if (maxPos<0) return GETIN_OLD;
  saturateMin(pos,maxPos);
  return _cargoPreciseGetInOut[pos];
}

/*!
\patch 5138 Date 3/9/2007 by Jirka
- Fixed: Separate get in point can be defined for each cargo position
*/

int TransportType::NCargoGetInPos(int cargoIndex) const
{
  if (cargoIndex < 0)
    return 0;
  int n = _cargoGetInPos.Size();
  if (cargoIndex >= n)
    return _cargoGetInPos[n - 1].Size();
  return _cargoGetInPos[cargoIndex].Size();
}

GetInPointIndexVal TransportType::GetCargoGetInPos(int cargoIndex, int i) const
{
  int n = _cargoGetInPos.Size();
  if (cargoIndex >= n)
    return _cargoGetInPos[n - 1][i];
  return _cargoGetInPos[cargoIndex][i];
}

#if _HELISIM_PRECISE_INTERACTION
int TransportType::NCargoGetInPosPrecise(int cargoIndex) const
{
  if (cargoIndex < 0)
    return 0;
  int n = _cargoGetInPosPrecise.Size();
  if (cargoIndex >= n)
    return _cargoGetInPosPrecise[n - 1].Size();
  return _cargoGetInPosPrecise[cargoIndex].Size();
}

GetInPointIndexVal TransportType::GetCargoGetInPosPrecise(int cargoIndex, int i) const
{
  int n = _cargoGetInPosPrecise.Size();
  if (cargoIndex >= n)
    return _cargoGetInPosPrecise[n - 1][i];
  return _cargoGetInPosPrecise[cargoIndex][i];
}
#endif

#pragma warning( disable: 4355 )

static const EnumName LandingModeNames[]=
{
  EnumName(Transport::LMNone, "NONE"),
  EnumName(Transport::LMLand, "LAND"),
  EnumName(Transport::LMGetIn, "GET IN"),
  EnumName(Transport::LMGetOut, "GET OUT"),
  EnumName()
};

template<>
const EnumName *GetEnumNames(Transport::LandingMode dummy)
{
  return LandingModeNames;
}

#if _VBS3 //check override dependency
class LinkOverrideTurret : public ITurretFunc
{
public:
  LinkOverrideTurret(){}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    context._turret->SetOverrideTurretLink();
    return false; // continue
  }
};
#endif


class InitWeaponStateFunc: public ITurretFunc
{
public:
  InitWeaponStateFunc() {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (context._turretType)
      context._weapons->Init(context._turretType->_weapons, *context._turretType->_par, entity, true);
    // open fire by default
    return false; //all turrets
  }
};


TransportVisualState::TransportVisualState(TransportType const& type)
:base(type),
_fuel(type.GetFuelCapacity()),
_driverHidden(type._hideProxyInCombat),
_IKIdent(true),
_leftHandIK(M4Identity),
_rightHandIK(M4Identity),
_leftLegIK(M4Identity),
_rightLegIK(M4Identity)
{
  int n = type._turrets.Size();
  _turretVisualStates.Realloc(n);
  _turretVisualStates.Resize(n);
  for (int i=0; i<n; i++)
    _turretVisualStates[i] = new TurretVisualState(*type._turrets[i], type);

  _doors.Init(type._doors);
}

void TransportVisualState::Interpolate(Object *obj, ObjectVisualState const& t1state, float t, ObjectVisualState& interpolatedResult) const
{
  base::Interpolate(obj, t1state, t, interpolatedResult);

  TransportVisualState const& s = static_cast<TransportVisualState const&>(t1state);
  TransportVisualState& res = static_cast<TransportVisualState&>(interpolatedResult);

  #define INTERPOLATE(x) res.x = x + t*(s.x - x)

  INTERPOLATE(_fuel);  // continuous
  INTERPOLATE(_driverHidden);  // continuous

  #undef INTERPOLATE

  int n = _turretVisualStates.Size();
//  res._turretVisualStates.Realloc(n);
//  res._turretVisualStates.Resize(n);
  for (int i=0; i<n; i++) {
    _turretVisualStates[i]->Interpolate(*s._turretVisualStates[i], t, *res._turretVisualStates[i]);
  }

  _doors.Interpolate(s._doors, t, res._doors);

#define INTERPOLATE_MATRIX(x) res.x = x*(1-t) + s.x*t
  // IK transforms
  INTERPOLATE_MATRIX(_leftLegIK);
  INTERPOLATE_MATRIX(_rightLegIK);
  INTERPOLATE_MATRIX(_leftHandIK);
  INTERPOLATE_MATRIX(_rightHandIK);
  res._IKIdent = _IKIdent && s._IKIdent;
#undef INTERPOLATE
}

Transport::Transport(const EntityAIType *name, Person *driver, bool fullCreate)
:base(name, fullCreate),
#pragma warning(push)
#pragma warning(disable:4355)
_weaponsState(*this),
#pragma warning(pop)
//_planner(TransportGetFieldCost,this),
//_crewDammage(0), // TODO: remove - not used
_engineOff(true),
_zoom(),
_ptrSurfInfo(NULL),
_getOutAfterDamage(TIME_MAX), // never get out
_explosionTime(TIME_MAX), // never explode
_turretFrontUntil(TIME_MIN),
_driverPos(VZero),
_dirWanted(VZero),
_moveMode(VMMFormation),
_turnMode(VTMNone),
_fireEnabled(false),
//_useStrategicPlan(false),
//_strategicPlanValid(false),
//_lastStrategicIndex(-1),
//_completedSent(false),
_lock(LSDefault),
_driverLocked(false),
_landing(LMNone),
_radio(CCVehicle, Pars >> "RadioChannels" >> "VehicleChannel"),
_showDmg(false),
_showDmgValid(Glob.time-60),
_randomizer(GRandGen.RandomValue()), // do not use same sound frequency
// all hatches are by default closed
_driverHiddenWanted(Type()->_hideProxyInCombat),
_manualFire(false),
_azimutWanted(0),
_doCrash(CrashNone),
_oldEnergy(0.0f),
_maxContactSpeed(0),
_validEnergy(false),
_isWreck(false),
_visionMode(VMNormal),
_tiIndex(0),
_enableVisionMode(true),
_turretGunTemperatureCoef(0),
_destroyMaxHeatTime(0),
_lastCamShakeTime(0),
_lastCamDamageShakeTime(0),
_unLockedControls(false),
_enableCopilots(true),
_slingloadForce(VZero)
{
  const TransportType *type = Type();
  _zoom.Init(Type()->GetViewOptics());

  _destrType = type->GetDestructType();
  if (_destrType==DestructDefault)
    _destrType=DestructEngine;

  // note: passing driver is obsolete
  DoAssert(driver==NULL);

  DriverConstruct(driver);
  _manCargo.Resize(type->_maxManCargo);
  
  _engineTemperatureCoef = 0.0f;
  _wheelTemperatureCoef = 0.0f;

  int n = type->_turrets.Size();
  _turrets.Realloc(n);
  _turrets.Resize(n);
  for (int i=0; i<n; i++)
    _turrets[i] = new Turret(*type->_turrets[i], this);

  // for !fullCreate, do not create magazines and weapons, but still initialize the parameters of effects
  _weaponsState.Init(type->_weapons, *type->_par, this, fullCreate);

  // FIX: WeaponState.Init cannot be called inside upper new Turret, as the _turrets contains NULLs
  // and Turret constructor uses ForEachTurret call which assumes the _turrets are initialized already
  // more info: news:hvagam$qjg$1@new-server.localdomain
  InitWeaponStateFunc func;
  ForEachTurret(func);
  // initialize vision modes
  if (type->_viewOptics.HasVisionModes())
    type->_viewOptics.GetInitVisionMode(_visionMode, _tiIndex);

  //units will eject from vehicle without wheels/tracks
  _allowCrewInImmobile = false;

#if _VBS3
  //this has to be done after all turrets are created
  LinkOverrideTurret func;
  for (int i=0; i<n; i++)
    _turrets[i]->ForEachTurret(unconst_cast(this), Type()->_turrets[i], func);
#endif

#if _HELISIM_RENDER_TO_TEXTURE

  const AutoArray<R2TTgtDescription> &tgtsDesc = Type()->_r2tTgtDesc;
  
  for (int i = 0; i < tgtsDesc.Size(); i++)
  {
    R2TTgtPars *tgtPars = new R2TTgtPars(tgtsDesc[i]);
    if (!tgtPars->IsEmpty())
    {
      _r2tTgtPars.Append(tgtPars);
    }
  }
  _r2tTgtPars.Compact();

#endif

  int cCount = SoundControllersCount();
  _soundControllers.Realloc(cCount);
  _soundControllers.Resize(cCount);
  _soundControllers.SetAll(cCount, 0.0f);
}

// IWeaponsHolder implementation
void Transport::Compact()
{
  _weapons.Compact();
  _magazines.Compact();
}

bool Transport::ForEachWeapon(const std::function<bool(Weapon *weapon)> &func)
{
  for (int i=0; i<_weapons.Size(); i++)
  {
    Weapon *weapon = _weapons[i];
    if (!weapon) continue;
    if (func(weapon)) return true;
  }
  return false;
}

bool Transport::ForEachWeapon(const std::function<bool(const Weapon *weapon)> &func) const
{
  for (int i=0; i<_weapons.Size(); i++)
  {
    const Weapon *weapon = _weapons[i];
    if (!weapon) continue;
    if (func(weapon)) return true;
  }
  return false;
}

bool Transport::AddWeapon(EntityAIFull *entity, Weapon *weapon)
{
  weapon->ItemMoveOut(entity);
  _weapons.Add(weapon);
  return true;
}

bool Transport::RemoveWeapon(Weapon *weapon)
{
  // find and remove weapon
  if (!_weapons.Delete(weapon)) return false; // not found
  // unassign the weapon, but leave it in the world
  weapon->CancelHierarchyParent(this);
  return true;
}

bool Transport::DeleteWeapon(RStringB name)
{
  // find and delete the weapon
  for (int i=0; i<_weapons.Size(); i++)
  {
    Weapon *weapon = _weapons[i];
    if (!weapon) continue;
    const WeaponType *type = weapon->Type();
    if (type && stricmp(type->GetName(), name) == 0)
    {
      // remove from the world
      weapon->ItemDestroy();
      // and the container
      _weapons.Delete(i);
      
      return true;
    }
  }
  return false; // not found
}

void Transport::DeleteWeapons(const std::function<bool(const Weapon *weapon)> &func)
{
  for (int i=_weapons.Size()-1; i>=0; i--) // go reverse to avoid problems with deletion
  {
    Weapon *weapon = _weapons[i];
    if (!weapon) continue;

    if (func(weapon))
    {
      // remove from the world
      weapon->ItemDestroy();
      // and the container
      _weapons.DeleteAt(i);
    }
  }
}

void Transport::DeleteAllWeapons()
{
  for (int i=0; i<_weapons.Size(); i++)
  {
    Weapon *weapon = _weapons[i];
    if (!weapon) continue;
    // remove from the world
    weapon->ItemDestroy();
  }
  _weapons.Clear();
}

bool Transport::ForEachMagazine(const std::function<bool(Magazine *)> &func)
{
  for (int i=0; i<_magazines.Size(); i++)
  {
    Magazine *magazine = _magazines[i];
    if (!magazine) continue;
    if (func(magazine)) return true;
  }
  return false;
}

bool Transport::AddMagazine(EntityAIFull *entity, Magazine *magazine)
{
  magazine->ItemMoveOut(entity);
  _magazines.Add(magazine);
  return true;
}

bool Transport::RemoveMagazine(Magazine *magazine)
{
  // find and remove magazine
  if (!_magazines.Delete(magazine)) return false; // not found
  // unassign the weapon, but leave it in the world
  magazine->CancelHierarchyParent(this);
  return true;
}

bool Transport::DeleteMagazine(RStringB name)
{
  // find and delete the magazine
  for (int i=0; i<_magazines.Size(); i++)
  {
    Magazine *magazine = _magazines[i];
    if (!magazine) continue;
    const MagazineType *type = magazine->Type();
    if (type && stricmp(type->GetName(), name) == 0)
    {
      // remove from the world
      magazine->ItemDestroy();
      // and the container
      _magazines.Delete(i);
      
      return true;
    }
  }
  return false; // not found
}

void Transport::DeleteMagazines(const std::function<bool(const Magazine *magazine)> &func)
{
  for (int i=_magazines.Size()-1; i>=0; i--) // go reverse to avoid problems with deletion
  {
    Magazine *magazine = _magazines[i];
    if (!magazine) continue;

    if (func(magazine))
    {
      // remove from the world
      magazine->ItemDestroy();
      // and the container
      _magazines.DeleteAt(i);
    }
  }
}

void Transport::DeleteAllMagazines()
{
  for (int i=0; i<_magazines.Size(); i++)
  {
    Magazine *magazine = _magazines[i];
    if (!magazine) continue;

    // remove from the world
    magazine->ItemDestroy();
  }
  _magazines.Clear();
}

WeaponModePtr Transport::GetCurrentWeapon() const
{
  WeaponModePtr ptr;
  
  if (_currentWeapon)
  {
    ptr._weapon = _currentWeapon;
    ptr._muzzle = _currentWeapon->GetCurrentMuzzle();
    if (ptr._muzzle >= 0 && ptr._muzzle < ptr._weapon->NMuzzles())
    {
      const Muzzle &muzzle = ptr._weapon->GetMuzzle(ptr._muzzle);
      ptr._mode = muzzle._currentMode;
    }
  }

  return ptr;
}

void Transport::SetCurrentWeapon(const WeaponModePtr &ptr)
{
  _currentWeapon = ptr._weapon;
  if (ptr.IsValid())
  {
    Muzzle &muzzle = ptr._weapon->SetCurrentMuzzle(ptr._muzzle);
    muzzle._currentMode = ptr._mode;
  }
}

#if _HELISIM_RENDER_TO_TEXTURE

void Transport::UpdateR2TCamera(int index)
{
  if (index < 0 || index >=_r2tTgtPars.Size()) return;

  const R2TCameraDescription &camDesc = _r2tTgtPars[index]->Desc();
  Matrix4 &tgtMatrix = _r2tTgtPars[index]->Transform();
  Matrix4 toWorld = RenderVisualState().Transform();

  Vector3 pos = camDesc._pos;
  Vector3 dir = camDesc._dir;

  if (camDesc._posIndex != -1 && camDesc._dirIndex != -1)
  {
    int level = _shape->FindMemoryLevel();
    if (level >= 0)
    {
      if (camDesc._posIndex == camDesc._dirIndex)
      {
        pos = AnimatePoint(RenderVisualState(), level, camDesc._posIndex);
        dir = VForward;
      }
      else
      {
        pos = AnimatePoint(RenderVisualState(), level, camDesc._posIndex);
        dir = AnimatePoint(RenderVisualState(), level, camDesc._dirIndex);
      }

      dir = (dir - pos).Normalized();
    }
  }

  // camera pos define vehicle
  tgtMatrix.SetPosition(toWorld.FastTransform(pos));
  tgtMatrix.SetDirectionAndUp(toWorld.Rotate(dir), toWorld.DirectionUp());
}

#endif

Person *Transport::Observer() const
{
  TurretContext context;
  if (!GetPrimaryObserverTurret(context) || !context._turret)
    return NULL;
  return context._gunner;
}

Person *Transport::Gunner() const
{
  TurretContext context;
  if (!GetPrimaryGunnerTurret(context) || !context._turret)
    return NULL;
  return context._gunner;
}


void Transport::SetCopilot(Person *copilot)
{
  OnEvent(EEControlsShifted, copilot, _copilot?_copilot : _driver);
   GWorld->OnMissionEvent(MEControlsShifted, copilot, _copilot?_copilot : _driver);
  _copilot = copilot;
}

void Transport::EnableCopilots(bool enable) 
{
  _enableCopilots = enable;
  if(!enable)
  { 
    _unLockedControls = false;
    SetCopilot(NULL);
  }
}

void Transport::DriverConstruct( Person *driver )
{
  _driver=driver;
  // change brain vehicle
  if( driver )
  {
    AIBrain *brain=_driver->Brain();
    if (brain)
      brain->SetVehicleIn(this);
  }
}

bool Transport::GetEnableCompass(AIBrain *unit) const
{
  int canSee = 0;
  if (unit == ObserverUnit())
    canSee = Type()->_commanderCanSee;
  else if (unit == PilotUnit())
    canSee = Type()->_driverCanSee;
  else if (unit == GunnerUnit())
    canSee = Type()->_gunnerCanSee;
  return (canSee & CanSeeCompass) != 0;
}

float TransportVisualState::GetAltRadar(Ref<LODShape> shape) const
{
  const Shape *geom = shape->LandContactLevel();
  if (!geom)
    geom = shape->GeometryLevel();
  Vector3Val min = geom ? geom->Min() : shape->Min();
  Vector3Val wPos = PositionModelToWorld(Vector3(0.0f, min.Y(), 0.0f));

  // altRadar is used to animate dampers for helicopters
  // therefore we sometimes need it to be quite accurate and to consider roadways
  // to avoid computing it when not necessary, we do not perform this computation for fast moving vehicles
  float surfY = (Speed().SquareSize() < Square(50.0f)) ?
    GLandscape->RoadSurfaceYAboveWater(wPos, NULL, NULL, NULL, Landscape::UONoLock) : GLandscape->SurfaceYAboveWaterNoWaves(wPos.X(), wPos.Z());
  return wPos.Y() - surfY;
}


float TransportVisualState::GetAltBaro() const
{
  return Position().Y();
}

float TransportVisualState::GetSpeedHorizontal() const
{
  return fabs(ModelSpeed()[2]);
}

float TransportVisualState::GetSpeedVertical() const
{
  return Speed().Y();
}

float TransportVisualState::GetDirection() const
{
  Vector3Val dir = Direction();
  return atan2(dir.X(), dir.Z());
}

float Transport::GetCtrlTurretDirection(ObjectVisualState const& vs) const
{
  TurretContext context;
  Vector3 dir = GetPrimaryGunnerTurret(context) ? GetWeaponDirection(vs, context, WeaponModePtr()) : vs.Direction();
  return atan2(dir.X(), dir.Z());
}

float Transport::GetCtrlWaypointDirection(ObjectVisualState const& vs) const
{
#if _ENABLE_IDENTITIES
  Person *person = NULL;
  if (Copilot())
    person = Copilot();
  else if (Driver())
    person = Driver();

  if(person)
  {
    const Task *task = person->GetIdentity().GetCurrentTask();
    if (task)
    {
      Vector3 dir = VZero;
      float dirTransport = GetCtrlDirection(vs);
      task->GetDestination(dir);
      dir = dir - RenderVisualState().Position();
      dir.Normalize();

      float ang = -(atan2(dir.X(), dir.Z()) - dirTransport);
      if(ang < - H_PI) ang +=H_2PI;
      else if(ang > H_PI) ang -=H_2PI;
      return ang;
    }
  }
#endif
  return 0;
}

float TransportVisualState::GetHorizonBank() const
{
  Vector3 asideDir;
  asideDir[0] = +Direction().Z();
  asideDir[1] = 0;
  asideDir[2] = -Direction().X();

  Vector3 relAside = DirectionWorldToModel(asideDir);
  relAside[2] = 0;
  relAside.Normalize();

  float dive = atan2(-relAside.Y(),relAside.X());
  return dive;
}

float TransportVisualState::GetHorizonDive() const
{
  float dive = asin(Direction().Y());
  return dive;
}

float TransportVisualState::GetFuelFraction(TransportType const* type) const
{
  float capacity = type->GetFuelCapacity();
  if (capacity <= 0.0f)
    return 0.0f;
  return GetFuel() / capacity;
}

float TransportVisualState::GetGMeter() const {return _acceleration.Size() * (1.0f / G_CONST);}
float TransportVisualState::GetGMeterX() const {return DirectionWorldToModel(_acceleration).X() * (1.0f / G_CONST);}
float TransportVisualState::GetGMeterY() const {return DirectionWorldToModel(_acceleration).Y() * (1.0f / G_CONST);}
float TransportVisualState::GetGMeterZ() const {return DirectionWorldToModel(_acceleration).Z() * (1.0f / G_CONST);}

TurretVisualState* TransportVisualState::GetTurret(const int *path, int size)
{
  if (size <= 0)
  {
    LogF("Empty turret path");
    return NULL;
  }
  int i = path[0];
  if (i<0 || i>=_turretVisualStates.Size())
  {
    LogF("Invalid turret path");
    return NULL;
  }
  return _turretVisualStates[i]->GetTurret(path + 1, size - 1);
}

/*!
\patch 5163 Date 6/1/2007 by Ondra
- Fixed: Scripting function person moveInTurret [vehicle,[x,y,z]] crashed when [x,y,z] was invalid.
*/
TurretVisualState const* TransportVisualState::GetTurret(const int *path, int size) const
{
  if (size <= 0)
  {
    LogF("Empty turret path");
    return NULL;
  }
  int i = path[0];
  if (i<0 || i>=_turretVisualStates.Size())
  {
    LogF("Invalid turret path");
    return NULL;
  }
  return _turretVisualStates[i]->GetTurret(path + 1, size - 1);
}

/*!
\patch 1.23 Date 9/12/2001 by Ondra
- Fixed: Empty tractor and Rapid cars were flying 1 m above ground.
\patch_internal 1.23 Date 9/12/2001 by Ondra
- Fixed: positioning on objects containing proxies might be incorrect.
*/
void Transport::PlaceOnSurface(Matrix4 &trans)
{
  base::PlaceOnSurface(trans);

  if (!GetShape() || GetShape()->NLevels()<=0)
    return;

  // place in steady position
  Vector3 pos=trans.Position();
  Matrix3 orient=trans.Orientation();

  float dx,dz;
  pos[1] = GLandscape->RoadSurfaceYAboveWater(pos,&dx,&dz);

  //if( type->IsKindOf(GWorld->Preloaded(VTypeStatic)) )
  if (!Type()->HasDriver())
  {
    // static vehicle - place as in bulldozer
    pos += orient * GetShape()->BoundingCenter();
  }
  else
  {
    // move and change orientation
    Vector3 up(-dx,1,-dz);
    Matrix3 orient;
    orient.SetUpAndDirection(up,trans.Direction());
    trans.SetOrientation(orient);

    const Shape *geom = _shape->LandContactLevel();
    if (!geom)
      geom = _shape->GeometryLevel();
    if (geom)
    {
      Vector3 minC(0, geom->Min().Y(), 0);
      pos -= orient*minC;
    }
    else if (_shape->NLevels()>0)
    {
      ShapeUsed level0 = _shape->Level(0);
      Vector3 minC(0, level0->Min().Y(), 0);
      pos -= orient*minC;
    }
    else
    {
      Vector3 minC(0, _shape->Min().Y(), 0);
      pos -= orient*minC;
    }
  }
  trans.SetPosition(pos);
}

void Transport::EngineOn()
{
  bool isStatic = GetType()->GetFuelCapacity()<=0;
  if (isStatic || !_engineOff)
    return;
  if (!isStatic && GetFuel()<=0) return; // fuel expected, no fuel, no engine
  _engineOff = false;
  OnEvent(EEEngine,!_engineOff);
}

void Transport::EngineOff()
{
  if (_engineOff || !EngineCanBeOff())
    return;
  _engineOff = true;
  OnEvent(EEEngine,!_engineOff);
}

Vector3 Transport::GetEnginePos() const
{
  // sound source position in model coord
  return Vector3(0,0,1);
}

Vector3 Transport::GetEnvironPos() const
{
  // sound source position in model coord
  // aerodynamic sound is usually in front of us
  // tanks and other vehicles might be more ground-based
  return Vector3(0,-1,2);
}

AIBrain *Transport::DriverBrain() const
{
  return _driver ? _driver->Brain() : NULL;
}

AIBrain *Transport::CopilotBrain() const
{
  return _copilot ? _copilot->Brain() : NULL;
}

AIBrain *Transport::GunnerBrain() const
{
  Person *person = Gunner();
  return person ? person->Brain() : NULL;
}

AIBrain *Transport::ObserverBrain() const
{
  Person *person = Observer();
  return person ? person->Brain() : NULL;
}

AIBrain *Transport::PilotUnit() const
{
  return DriverBrain();
}

AIBrain *Transport::CopilotUnit() const
{
  return CopilotBrain();
}

AIBrain *Transport::GunnerUnit() const
{
  if (Type()->_hasGunner)
    return GunnerBrain();
  else
    return DriverBrain();
}

AIBrain *Transport::ObserverUnit() const
{
  return ObserverBrain();
}

AIBrain *Transport::EffectivePilotingUnit() const
{
  if(_copilot) return CopilotUnit();
  return DriverBrain();
}


AIBrain *Transport::EffectiveObserverUnit() const
{
  #if _ENABLE_CHEATS
    static bool obsCom = false;
    if (obsCom)
    {
      return CommanderUnit();
    }
  #endif
  // if effective commander is not in cargo, use him as an effective observer as well
  AIBrain *commander = CommanderUnit();
  if (commander && !commander->IsInCargo())
  {
    return commander;
  }
  // roles of commander / observer separated. Observer commands gunners, commanders commands movement
  // this is done to handle situation when player is commanding the vehicle from cargo (or optinally also driver) position, and is unable to command gunners efficiently
  // see http://dev-heaven.net/issues/2353
  AIBrain *unit = ObserverUnit();
  if (!unit)
  {
    unit = CommandingGunner();
  }
  return unit;
}

AIBrain *Transport::EffectiveGunnerUnit(const TurretContext &context) const
{
  if (_manualFire && context._turretType && context._turretType->_primaryGunner)
    return CommanderUnit();
  else
    return base::EffectiveGunnerUnit(context);
}

/// Functor searching the person with the best commanding level
class FindCommander : public ITurretFunc
{
protected:
  AIBrain *&_commander;
  float &_maxCommanding;

public:
  FindCommander(AIBrain *&commander, float &maxCommanding)
    : _commander(commander), _maxCommanding(maxCommanding) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turretType) return false; // continue
    if (!context._gunner) return false; // continue
    if (!context._gunner->Brain()) return false; // continue
    if (context._turretType->_commanding > _maxCommanding)
    {
      _commander = context._gunner->Brain();
      _maxCommanding = context._turretType->_commanding;
    }
    return false; // check all turrets
  }
};

#if _VBS3
/// Functor searches for the player inside vehicle.
/// he becomes the commander
class FindPlayer : public ITurretFunc
{
protected:
  AIBrain *&_player;

public:
  FindPlayer(AIBrain *&player): _player(player){}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turretType) return false; // continue
    if (!context._gunner) return false; // continue
    if (!context._gunner->Brain()) return false; // continue
    if(context._gunner->Brain()->IsPlayer())
      _player = context._gunner->Brain();

    return false; // check all turrets
  }
};
#endif

AIBrain *Transport::CommandingGunner() const
{
#if _VBS3
  // Player takes precedence over config vehicle
  AIBrain *commander = DriverBrain();
  FindPlayer funcFP(commander);
  ForEachTurret(funcFP);
#else
  AIBrain *commander = DriverBrain();
#endif

  // when driver is commander and driver is set, no gunner can be commander
  if (!commander || !Type()->_driverIsCommander)
  {
    // search for commander in turrets
    // find the person with the best commanding level (driver has commanding level 0)
    float maxCommanding = commander ? 0 : -FLT_MAX;
    FindCommander func(commander, maxCommanding);
    ForEachTurret(func);
  }
  return commander;
}

AIBrain *Transport::CommanderUnit() const
{
  if (_effCommander)
  {
    // check if effective commander is in the vehicle
    AIBrain *unit = _effCommander->Brain();
    if (unit && unit->GetVehicleIn() == this)
      return unit;
  }

  AIBrain *commander = CommandingGunner();

  if (!commander)
  {
    // search for commander in cargo
    for (int i=0; i<_manCargo.Size(); i++)
    {
      Person *man = _manCargo[i];
      if (man && man->Brain())
      {
        commander = man->Brain();
        break;
      }
    }
  }
  return commander;
}

Person *Transport::CommanderPerson() const
{
  AIBrain *com = CommanderUnit();
  return com ? com->GetPerson() : NULL;
}

bool Transport::IsCrewed() const
{
  if (Driver() && !Driver()->IsDamageDestroyed())
    return true;
  class IsSomeGunner : public ITurretFunc
  {
  public:
    IsSomeGunner() {}
    bool operator () (EntityAIFull *entity, TurretContext &context)
    {
      if (!context._turret) return false;
      if (!context._gunner) return false;  // continue
      if (context._gunner->IsDamageDestroyed()) return false;
      return true; // break - found
    }
  };
  IsSomeGunner func;
  if (ForEachTurret(func))
    return true;
  for (int i=0; i<GetManCargo().Size(); i++)
  {
    if (GetManCargo()[i] && !GetManCargo()[i]->IsDamageDestroyed())
      return true;
  }

  return false;
}

TargetSide Transport::GetTargetSideAsIfAlive() const
{
  if (!IsCrewed()) return TCivilian;
  return base::GetTargetSideAsIfAlive();
}

TargetSide Transport::GetTargetSide() const
{
  if (!IsDamageDestroyed())
  {
    if (!IsCrewed()) return TCivilian;
  }
  return base::GetTargetSide();
}

float Transport::VisibleLights() const
{
  // TODO: consider light dammage
  return _pilotLight || _collisionLight;
}

bool Transport::QIsManual() const
{
  if (!GWorld->PlayerManual()) return false;
  if (!GWorld->PlayerOn()) return false;
  if (GWorld->PlayerOn() == Observer()) return true;
  if (GWorld->PlayerOn() == Driver()) return true;
  if (GWorld->PlayerOn() == Gunner()) return true;
  if (GWorld->PlayerOn() == Copilot()) return true;
  return false;
}

bool Transport::QIsManualMovement() const
{
  if (!GWorld->PlayerManual()) return false;
  if (!GWorld->PlayerOn()) return false;
  if (GWorld->PlayerOn() == Observer()) return true;
  if (GWorld->PlayerOn() == Driver()) return true;
  if (GWorld->PlayerOn() == Copilot()) return true;
  return false;
}

bool Transport::QIsManual(const AIBrain *unit) const
{
  if (!GWorld->PlayerManual()) return false;
  if (!GWorld->PlayerOn()) return false;
  if (!unit) return false;
  return unit==GWorld->PlayerOn()->Brain();
}

/*!
\patch 5137 Date 3/6/2007 by Bebul
- Fixed: head movement not visible on players in vehicle
*/
static void MovePersonHead(Transport *vehicle, Person *person, const ViewPars &viewPars)
{
  if (!person) return;
  AIBrain *unit = person->Brain();
  if (!unit || unit->IsAnyPlayer()/*vehicle->QIsManual(unit)*/ )
    return;

  // if there is any target assigned, track it
  Vector3 watchDir = unit->GetWatchHeadDirection();
  Vector3 relDir = vehicle->DirectionWorldToModel(watchDir);
  // if target is forward, look forward
  float heading = atan2(relDir.X(), relDir.Z());
  // TODO: calculate dive as well
  float dive = 0.0f;
  float fov = 1.0f;
  // apply head constraints
  viewPars.LimitVirtual(CamInternal,heading,dive,fov);
  Matrix3 rotHeading(MRotationY,-heading);
  Vector3 dir = rotHeading.Direction();
  // TODO: convert to the proxy space
  // currently we assume proxy space is the same as model space
  person->AimHeadCrew(dir);
}

/// Functor aiming head of the AI gunner
class MoveGunnerHead : public ITurretFunc
{
public:
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turretType) return false; // continue
    Person *person = context._gunner;
    if (!person) return false; // continue
    Transport *vehicle = static_cast<Transport *>(entity); // we know where we call it

    int opticsMode = (context._turret)?context._turret->_currentViewGunnerMode : 0;
    MovePersonHead(vehicle, person, context._turretType->_viewGunner[opticsMode]);

    return false; // continue
  }
};

void Transport::MoveCrewHead()
{
  //driver
  MovePersonHead(this, _driver, Type()->_viewPilot);
  //cargo
  for (int i=0; i < _manCargo.Size(); i++)
  {
    MovePersonHead(this, _manCargo[i], Type()->_viewCargo);
  }
  //gunners
  MoveGunnerHead func;
  ForEachTurret(func); //these works for persons in turrets (excluding driver and cargo)
}

static void RemoveMan( Person *man, EntityAI *killer )
{
  //if( man==GLOB_WORLD->PlayerOn() ) GWorld->PlayerKilled(killer);
  //GLOB_WORLD->RemoveOutVehicle(man);
  GLOB_WORLD->RemoveSensor(man);
}

/*
static void RemoveManUnit( AIUnit *unit )
{
  if (unit)
    unit->SendAnswer(AI::UnitDestroyed);
}
*/

static bool CrewDamage(EntityAIFull *entity, Person *man, EntityAI *killer, float overkill, RString ammo)
{
  if (!man)
    return false;
  float dammage = overkill*(GRandGen.RandomValue()*0.5+0.5);
  if (dammage >= 0.0f)
  {
    Vector3 cDammagePoint = man->GetCommonDamagePoint();

    Object::DoDamageResult result;
    result.damage = dammage;
    man->ApplyDoDamage(result,killer, ammo, SkinnedPosition());

    // if man is not local, IsDamageDestroyed is unlikely to be true
    // however it is safer not to rely upon it too much
    // we must not return true for remote units, as KilledBy cannot be called for them
    if (man->IsDamageDestroyed() && man->IsLocal())
    {
      RemoveMan(man,killer);
      return true;
    }
  }
  return false;
}

/// Functor damaging gunner
class DammageGunner : public ITurretFunc
{
protected:
  EntityAI *_killer;
  float _howMuch;
  RString _ammo;

public:
  DammageGunner(EntityAI *killer, float howMuch, RString ammo)
    : _killer(killer), _howMuch(howMuch), _ammo(ammo) {}

  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (context._turret && context._gunner && CrewDamage(entity, context._gunner, _killer, _howMuch, _ammo) )
      context._gunner->KilledBy(_killer);
    return false;
  }
};

void Transport::DamageCrew( EntityAI *killer, float howMuch, RString ammo )
{
  if (!IsLocal())
  {
    ErrF("Cannot dammage remote transport %s",(const char *)GetDebugName());
    //return;
  }
  Ref<Person> man;
  for(int i=_manCargo.Size(); --i>=0;)
  {
    man = _manCargo[i];
    if (!man)
      continue;
    if (CrewDamage(this,man,killer,howMuch,ammo))
      man->KilledBy(killer);
  }
  // crew can also survive
  man = _driver;
  if (man && CrewDamage(this,man,killer,howMuch,ammo))
  {  
    man->KilledBy(killer);
  }
  DammageGunner func(killer, howMuch, ammo);
  ForEachTurret(func);

  if (EjectNeeded())
  {
    if (_getOutAfterDamage>Glob.time+15)
      // not set yet
      _getOutAfterDamage = Glob.time+GRandGen.Gauss(1.5,3,7);
  }
}

void Transport::HitBy( EntityAI *killer, float howMuch, RString ammo, bool wasDestroyed)
{
  if (!IsLocal())
    return;
  base::HitBy(killer,howMuch, ammo,wasDestroyed);
  if (howMuch > 0.0f)
    DamageCrew(killer,howMuch, ammo);
}


// dammage vehicle is heated up
static float MaxDamageHeatUpTime = 5.0f;

void Transport::Destroy(EntityAI *killer, float overkill, float minExp, float maxExp)
{
  base::Destroy(killer,overkill,minExp,maxExp);
  // kill or eject all brains
  DamageCrew(killer,overkill,"");
  // set all hitpoints to 1
  for (int i=0; i<_hit.Size(); i++)
    ChangeHit(killer, i, 1.0f, true);
  // make sure lights go off
  _pilotLight = false;
  _collisionLight = false;
  GLOB_WORLD->RemoveSensor(this); // vehicle destroyed
  UpdateStop();
  // dammage vehicle is heated up
  _destroyMaxHeatTime = Glob.time + MaxDamageHeatUpTime;
}

/*!
\patch 1.22 Date 8/30/2001 by Ondra
- Fixed: AI units now eject from damaged helicopter or plane.
\patch 1.34 Date 12/6/2001 by Ondra
- Fixed: MP: When plane or helicopter was damaged, player auto ejected.
*/

bool Transport::EjectCrew(Person *man)
{
  AIBrain *unit = man->Brain();
  if (!unit)
    return false;
  Eject(unit);
  return true;
}

bool Transport::EjectIfAlive(Person *man)
{
  if (!man || man->IsDamageDestroyed()) return false;
  if (man->IsNetworkPlayer()) return false;
  AIBrain *unit = man->Brain();
  if (!unit || !unit->LSIsAlive()) return false;

  Eject(unit);
  return true;
}

bool Transport::EjectIfAliveCargo()
{
  int ejected = 0;
#define EJECT_GRID_SIZE 0.7f
  for(int i = 0; i < _manCargo.Size(); i++)
  {
    Person * man = _manCargo[i];
    if (!man || man->IsDamageDestroyed())
      continue;

    if (man->IsNetworkPlayer())
      continue;

    AIBrain *unit = man->Brain();
    if (!unit || !unit->LSIsAlive()) continue;

    Vector3 diff(0,ejected / 3 * EJECT_GRID_SIZE,0);
    int xd = ejected % 3;
    switch(xd)
    {
    case 0:diff[0] = 0; break;
    case 1:diff[0] = EJECT_GRID_SIZE; break;
    case 2:diff[0] = -EJECT_GRID_SIZE; break;
    }

    Eject(unit, diff);
    ejected++;
  }
  return ejected > 0;
}

bool Transport::EjectIfDead(Person *man)
{
  if (!man || !man->IsDamageDestroyed()) return false;
  AIBrain *unit = man->Brain();
  if (!unit) return false;
  Assert(IsLocal());
  unit->DoGetOut(this,true,false);
  return true;
}

bool Transport::EjectIfDeadCargo()
{
  int ejected = 0;

  for(int i = 0; i < _manCargo.Size(); i++)
  {
    Person * man = _manCargo[i];
    if (!man || !man->IsDamageDestroyed()) continue;

    AIBrain *unit = man->Brain();
    if (!unit) continue;



    Vector3 diff(0,ejected / 3 * EJECT_GRID_SIZE,0);
    int xd = ejected % 3;
    switch(xd)
    {
    case 0:diff[0] = 0; break;
    case 1:diff[0] = EJECT_GRID_SIZE; break;
    case 2:diff[0] = -EJECT_GRID_SIZE; break;
    }

    Assert(IsLocal());
    unit->DoGetOut(this, true, false, diff);
    ejected++;
  }

  return ejected > 0;
}

/// Functor ejecting gunner from vehicle
class EjectGunner : public ITurretFunc
{
public:
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    Transport *vehicle = static_cast<Transport *>(entity); // we know where we call it
    if (context._gunner && context._turretType && context._turretType->_ejectDeadGunner)
      vehicle->EjectCrew(context._gunner);
    return false;
  }
};

/**
eject all crew-member who is sitting on a seat where he is not fixed.
We recognize such seats based on ejectDeadXxxx config flags.
*/
void Transport::EjectAllNotFixed()
{
  if (Driver() && Type()->_ejectDeadDriver)
  {
    EjectCrew(Driver());
  }
  EjectGunner func;
  ForEachTurret(func);
  if (Type()->_ejectDeadCargo)
  {
    for (int i=0; i<GetManCargo().Size(); i++)
    {
      Person *crew = GetManCargo()[i];
      if (crew) EjectCrew(crew);
    }
  }
}

void Transport::SetTotalDamageHandleDead(float damage, bool setHitpoints)
{
  bool doRepair = damage < GetTotalDamage();
  base::SetTotalDamageHandleDead(damage,setHitpoints);
  if (doRepair)
  {
    _getOutAfterDamage = TIME_MAX;
    _explosionTime = TIME_MAX;
  }
}

bool Transport::CheckEject() const
{
  return ManType::CheckEject(FutureVisualState().Position(),5);
}

/// Functor ejecting alive gunner from vehicle
class EjectAliveGunner : public ITurretFunc
{
public:
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turret) return false; // continue
    Transport *vehicle = static_cast<Transport *>(entity); // we know where we call it
    if(context._turretType && context._turretType->_canEject) vehicle->EjectIfAlive(context._gunner);
    return false;
  }
};

/// Functor ejecting dead gunner from vehicle
class EjectDeadGunner : public ITurretFunc
{
public:
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turretType) return false; // continue
    Transport *vehicle = static_cast<Transport *>(entity); // we know where we call it
    if (context._turretType->_ejectDeadGunner) vehicle->EjectIfDead(context._gunner);
    return false; // continue
  }
};

void Transport::ReactToDamage()
{
  if (!_isDead && IsDamageDestroyed()) 
  {
    _isDead=true;
  }

  // engine off only when upside down and reached a stable state
  if( _isDead || _isUpsideDown && GetStopped())
  {
    // tank  up - it is finished
    EngineOff();
    _pilotLight = false;
    _collisionLight = false; 
  }

  if(Glob.time>_getOutAfterDamage)
  {
    // all must get out - vehicle unusable
    // unassign all assigned soldiers
    // man->AssignAsCargo(NULL);
    // TODO: there may be some assigned soldier outside the vehicle
    // do not eject if the place is not suitable for the soldier
    if (CheckEject())
    {
      //check if type has eject enabled; get out when vehicle is not able to move is done in void AIGroup::GetInVehicles()
      if(GetType()->_cargoCanEject) EjectIfAliveCargo();
      if(GetType()->_driverCanEject) EjectIfAlive(_driver);
      EjectAliveGunner func;
      ForEachTurret(func);
      _getOutAfterDamage = TIME_MAX;
    }
  }

  if (IsLocal())
  {
    if (Type()->_ejectDeadDriver) EjectIfDead(_driver);
    EjectDeadGunner func;
    ForEachTurret(func);
    if (Type()->_ejectDeadCargo) EjectIfDeadCargo();
  }

  if (Glob.time>_explosionTime)
  {
    if (GetRawTotalDamage()<1 )
    {
      EntityAI *killer = this;
      if (_lastDammage && _lastDammageTime > Glob.time - 60)
        killer = _lastDammage;

      // kill crew (with slight chance of escape)
      DamageCrew(killer,10,"");
      // explode
      //LogF("%.3f Start smoke",Glob.time.toFloat());
      Destroy(killer,1.0,0.5,0.75);
      // set both the total damage and all hitpoints to 1
      SetTotalDamage(1, true);
      for (int i=0; i<_hit.Size(); i++) ChangeHit(killer, i, 1.0f, true);
    }
  }

}

float Transport::GetExplosives() const
{
  // if there is an explicit value, use it
  // if it is negative, it is coefficient
  // how much explosives is in
  return base::GetExplosives(); // ammunition
}

/*!
\patch 1.25 Date 09/29/2001 by Ondra
- Fixed: MP: When bad client-side prediction led to crash,
vehicle was damaged and could be even destroyed.
*/

void Transport::CrashDammage( float ammount, const Vector3 &pos, float range )
{
  // only local vehicles may crash
  if (IsLocal())
  {
    // if the vehicle has a driver, he is responsible for the crash
    AIBrain *brain = PilotUnit();
    EntityAI *responsible = this;
    if (brain && brain->GetPerson())
    {
      responsible = brain->GetPerson();
    }

    ammount*=GetType()->GetInvArmor();
    DoDamageResult result;
    LocalDamage(result,ShotInfo(NULL,responsible,this,pos),ammount,(range > 0) ? range : GetRadius());
    ApplyDoDamageLocalHandleHitBy(result,responsible, RString(), SkinnedPosition());
  }
}

void Transport::KeyboardAny(AIBrain *unit, float deltaT)
{
}

RString Transport::DriverAction() const
{
  if (FutureVisualState().IsDriverHidden()) return Type()->_driverInAction;
  return Type()->_driverAction;
}
RString Transport::CargoAction(int pos) const
{
  int maxPos = Type()->_cargoAction.Size()-1;
  if (maxPos<0) return RString();
  saturateMin(pos,maxPos);
  return Type()->_cargoAction[pos];
}

float Transport::DriverAnimSpeed() const
{
  return 1;
}
float Transport::CargoAnimSpeed(int position) const
{
  return 1;
}

bool Transport::IsPersonHidden(ObjectVisualState const& vs, Person *person) const
{
  TransportVisualState const& tvs = vs.cast<Transport>();
  if (person == Driver())
    return tvs.IsDriverHidden();
  else
  {
    TurretContextV context;
    if (FindTurret(unconst_cast(tvs), person, context))
      return context._turret && context._turretVisualState->IsGunnerHidden();
  }
  if (!person->Brain()) return true;
  // not a driver, not a gunner, must be a cargo.
  // When commander is in cargo, decide based on a combat mode only (see also Transport::AICommander)
  return person->Brain()->GetCombatModeLowLevel()>=CMAware;
}

void Transport::HidePerson(Person *person, float hide)
{
  if (person == Driver())
    HideDriver(hide);
  else
  {
    TurretContext context;
    if (FindTurret(person, context) && context._turret && context._turretType->_canHideGunner)
      context._turret->HideGunner(hide);
  }
}

void Transport::SelectWeaponCommander(AIBrain *unit, const TurretContext &context, const WeaponModePtr &ptr)
{
  // used when weapon is selected from UI
  Assert(unit);

  // manual fire is valid only for the primary gunner
  bool manualFire = false;
  if (context._turretType && context._turretType->_primaryGunner)
  {
    manualFire = IsManualFire();
  }

  AIBrain *gunner = context._gunner ? context._gunner->Brain() : NULL;
  if (unit == gunner || manualFire)
  {
    base::SelectWeaponCommander(unit, context, ptr);
  }
  else if (gunner)
  {
    if (IsLocal())
    {
      //SendLoad(context._gunner, ptr);
    }
    else
    {
      //RadioMessageVLoad msg(this, context._gunner, ptr);
      //GetNetworkManager().SendMsg(&msg);
    }
  }
}

void Transport::CommanderLoadMagazine(Magazine *magazine, RString weapon, RString muzzle)
{
}

#if _ENABLE_AI

void Transport::CommandPrimaryGunner(AIBrain *unit, TurretContext &context)
{
}


void Transport::AICommander(AIBrain *unit, float deltaT )
{
  // TODO: consider: turn in / turn out might be better controlled by the observer
  // turn in / turn out
  if (Type()->_hideProxyInCombat)
  {
    // find turret where the commander is
    // we need to check if turret allows turning out
    TurretContext commanderContext;
    bool commanderValid = FindTurret(unit->GetPerson(),commanderContext);
    if (unit->GetCombatModeLowLevel() >= CMAware ||
      (commanderValid && commanderContext._turretType && commanderContext._turretType->_forceHideGunner))
    {
      // some danger - head in
      Person *person = unit->GetPerson();
      HidePerson(person, 1);
    }
    else if (unit->GetCombatModeLowLevel() <= CMSafe)
    {
      // safe - we may turn heads out
      Person *person = unit->GetPerson();
      HidePerson(person, 0);
    }
  }
}

void Transport::AIObserver(AIBrain *unit, float deltaT )
{
}
#endif //_ENABLE_AI

bool Transport::HasSomePilot() const
{
#if _ENABLE_CHEATS
  // pretend we have a pilot, but do not perform any activity
  extern bool disableUnitAI;
  if (disableUnitAI)
    return true;
#endif
  AIBrain *driverUnit = PilotUnit();
  return driverUnit!=NULL;
}

/// Functor cleans up gunner
class CleanUpGunner : public ITurretFunc
{
public:
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (context._turret)
      context._turret->SetGunner(NULL);
    return false; // continue
  }
};

void Transport::CleanUp()
{
  _effCommander = NULL;
  _driver = NULL;
  CleanUpGunner func;
  ForEachTurret(func);
  _manCargo.Clear();
  base::CleanUp();
}

bool Transport::ForEachTurret(ITurretFunc &func) const
{
  // driver controlled weapons
  TurretContext context;
  context._turret = NULL;
  context._turretType = NULL;
  context._gunner = _driver;
  context._weapons = unconst_cast(&_weaponsState);
  if (func(unconst_cast(this), context))
    return true;

  int n = _turrets.Size();
  for (int i=0; i<n; i++)
  {
    if (_turrets[i]->ForEachTurret(unconst_cast(this), Type()->_turrets[i], func))
      return true;
  }
  return false;
}

class CrewInTurretFunc : public ITurretFunc
{
protected:
  ICrewFunc &func;
public:
  CrewInTurretFunc(ICrewFunc &functor) : func(functor) {}

  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turret) return false; // continue
    if (!context._gunner) return false;
    return func(context._gunner);
  }
};

bool Transport::ForEachCrew(ICrewFunc &func) const
{
  if (_driver && func(_driver))
    return true;
  CrewInTurretFunc crewEnumFunc(func);
  if (ForEachTurret(crewEnumFunc))
    return true;
  for (int i=0; i<_manCargo.Size(); i++)
  {
    if (_manCargo[i] && func(_manCargo[i]))
      return true;
  }
  return false;
}


void Transport::ForEachChildren(const std::function<bool(const NetworkObject *)> &func) const
{
  class CallCrew: public ICrewFunc
  {
    const std::function<bool(const NetworkObject *)> &func;

    public:
    CallCrew(const std::function<bool(const NetworkObject *)> &func):func(func){}
    virtual bool operator () (Person *entity) override {return func(entity);}

  } callCrew(func);
  bool ret = false;
  ret = ForEachCrew(callCrew);
  if (!ret) ForEachItemCargo([&func](const InventoryItem *item)->bool {return func(item);});
}

bool Transport::ForEachTurret(EntityVisualState& vs, ITurretFuncV &func) const
{
  // driver controlled weapons
  TurretContextV context;
  context._turret = NULL;
  context._turretType = NULL;
  context._turretVisualState = NULL;
  context._gunner = _driver;
  context._weapons = unconst_cast(&_weaponsState);
  if (func(unconst_cast(this), context)) return true;
  int n = _turrets.Size();
  if (n > 0)
  {
    Assert(static_cast_checked<TransportVisualState&>(vs)._turretVisualStates.Size() == n);
    for (int i=0; i<_turrets.Size(); i++)
    {
      if (_turrets[i]->ForEachTurret(*(static_cast_checked<TransportVisualState&>(vs))._turretVisualStates[i], unconst_cast(this), Type()->_turrets[i], func))
        return true;
    }
  }
  return false;
}

bool Transport::ForEachTurretEx(EntityVisualState& vs, ITurretFuncEx &func) const
{
  // driver controlled weapons
  TurretContextEx context;
  context._turret = NULL;
  context._turretType = NULL;
  context._turretVisualState = NULL;
  context._gunner = _driver;
  context._weapons = unconst_cast(&_weaponsState);
  context._parentTrans = vs.Orientation();
  if (func(unconst_cast(this), context)) return true;
  int n = _turrets.Size();
  if (n > 0)
  {
    Assert(static_cast_checked<TransportVisualState&>(vs)._turretVisualStates.Size() == n);
    Matrix3 trans = vs.Orientation();
    for (int i=0; i<_turrets.Size(); i++)
    {
      if (_turrets[i]->ForEachTurretEx(*(static_cast_checked<TransportVisualState&>(vs))._turretVisualStates[i], unconst_cast(this), Type()->_turrets[i], trans, func))
        return true;
    }
  }
  return false;
}

/// Functor searching for turret
class FindTurretFunc : public ITurretFunc
{
protected:
  const Person *_gunner;
  TurretContext &_context; // out

public:
  FindTurretFunc(const Person *gunner, TurretContext &context) : _gunner(gunner), _context(context) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (context._gunner == _gunner)
    {
      _context = context;
      return true; // found
    }
    return false; // continue
  }
};

#if _VBS3 //Commander override
//Get the turret for which that gunner gets his optics from
//This is used to get the optics for commanders
bool Transport::FindOpticsTurret(const Person *gunner, TurretContext &context) const
{
  FindTurretFunc func(gunner, context);
  if(!ForEachTurret(func)) return false;

  //check if we are currently looking through a different optic
  if(context._turret
    && context._turret->_overrideTurret
    &&(context._turret->_showOverrideOptics || context._turret->_overrideTurret->_overriddenByTurret)
    )
  {
    TurretContext contextNew;
    if(FindTurret(context._turret->_overrideTurret, contextNew))
    {
      context = contextNew;
    }
  }
  return true;
}

//find the turret the gunner currently controls
bool Transport::FindControlledTurret(const Person *gunner, TurretContext &context) const
{
  FindTurretFunc func(gunner, context);
  if(!ForEachTurret(func)) return false;

  //is the gunner currently controlling a different turret?
  if(context._turret
    && context._turret->_overrideTurret
    && context._turret->_overrideTurret->_overriddenByTurret == context._turret
    )
  {
    TurretContext contextNew;
    if(FindTurret(context._turret->_overrideTurret, contextNew))
    {
      //The gunner will be different to the one passed into this function
      context = contextNew;
    }
#if _ENABLE_CHEATS
    else
      Fail("override turret cannot be found");
#endif
  }
  return true;
}
#endif //_VBS3

bool Transport::FindTurret(const Person *gunner, TurretContext &context) const
{
  FindTurretFunc func(gunner, context);
  return ForEachTurret(func);
}

/// Functor searching for turret
class FindTurretFuncVisualState : public ITurretFuncV
{
protected:
  const Person *_gunner;
  TurretContextV &_context; // out

public:
  FindTurretFuncVisualState(const Person *gunner, TurretContextV &context) : _gunner(gunner), _context(context) {}
  bool operator () (EntityAIFull *entity, TurretContextV &context)
  {
    if (context._gunner == _gunner)
    {
      _context = context;
      return true; // found
    }
    return false; // continue
  }
};

bool Transport::FindTurret(EntityVisualState& vs, const Person *gunner, TurretContextV &context) const
{
  FindTurretFuncVisualState func(gunner, context);
  return ForEachTurret(vs, func);
}

/// Functor searching for turret
class FindTurretContextFunc : public ITurretFunc
{
protected:
  const Turret *_turret;
  TurretContext &_context; // out

public:
  FindTurretContextFunc(const Turret *turret, TurretContext &context) : _turret(turret), _context(context) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (context._turret == _turret)
    {
      _context = context;
      return true; // found
    }
    return false; // continue
  }
};

bool Transport::FindTurret(const Turret *turret, TurretContext &context) const
{
  FindTurretContextFunc func(turret, context);
  return ForEachTurret(func);
}

/// Functor searching for turret
class FindTurretContextFuncVisualState : public ITurretFuncV
{
protected:
  const Turret *_turret;
  TurretContextV &_context; // out

public:
  FindTurretContextFuncVisualState(const Turret *turret, TurretContextV &context) : _turret(turret), _context(context) {}
  bool operator () (EntityAIFull *entity, TurretContextV &context)
  {
    if (context._turret == _turret)
    {
      _context = context;
      return true; // found
    }
    return false; // continue
  }
};


bool Transport::FindTurret(EntityVisualState& vs, const Turret *turret, TurretContextV &context) const
{
  FindTurretContextFuncVisualState func(turret, context);
  return ForEachTurret(vs, func);
}

/// Functor searching for assigned position
class FindAssignedGunner : public ITurretFunc
{
protected:
  const AIBrain *_brain;
  TurretContext &_context; // out

public:
  FindAssignedGunner(const AIBrain *brain, TurretContext &context) : _brain(brain), _context(context) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (context._turret && context._turret->GetGunnerAssigned() == _brain)
    {
      _context = context;
      return true; // found
    }
    return false; // continue
  }
};

bool Transport::FindAssignedTurret(const AIBrain *brain, TurretContext &context) const
{
  FindAssignedGunner func(brain, context);
  return ForEachTurret(func);
}


class FindAssignedGunnerVisualState : public ITurretFuncV
{
protected:
  const AIBrain *_brain;
  TurretContextV &_context; // out

public:
  FindAssignedGunnerVisualState(const AIBrain *brain, TurretContextV &context) : _brain(brain), _context(context) {}
  bool operator () (EntityAIFull *entity, TurretContextV &context)
  {
    if (context._turret && context._turret->GetGunnerAssigned() == _brain)
    {
      _context = context;
      return true; // found
    }
    return false; // continue
  }
};


bool Transport::FindAssignedTurret(EntityVisualState& vs, const AIBrain *brain, TurretContextV &context) const
{
  FindAssignedGunnerVisualState func(brain, context);
  return ForEachTurret(vs, func);
}

/// Functor searching for primary gunner turret
class FindPrimaryGunnerTurretFunc : public ITurretFunc
{
protected:
  TurretContext &_context; // out

public:
  FindPrimaryGunnerTurretFunc(TurretContext &context) : _context(context) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (context._turretType && context._turretType->_primaryGunner)
    {
      _context = context;
      return true; // found
    }
    return false; // continue
  }
};

bool Transport::GetPrimaryGunnerTurret(TurretContext &context) const
{
  FindPrimaryGunnerTurretFunc func(context);
  if (ForEachTurret(func)) return true;

  // if no turret is marked as primary gunner, select driver weapon system
  context._turret = NULL;
  context._turretType = NULL;
  context._gunner = _driver;
  context._weapons = unconst_cast(&_weaponsState);
  return true;
}

bool Transport::GetDriverTurret(TurretContext &context) const
{
  // if no turret is marked as primary gunner, select driver weapon system
  context._turret = NULL;
  context._turretType = NULL;
  context._gunner = _driver;
  context._weapons = unconst_cast(&_weaponsState);
  return true;
}

/// Functor searching for primary gunner turret
class FindPrimaryGunnerTurretFuncVisualState : public ITurretFuncV
{
protected:
  TurretContextV &_context; // out

public:
  FindPrimaryGunnerTurretFuncVisualState(TurretContextV &context) : _context(context) {}
  bool operator () (EntityAIFull *entity, TurretContextV &context)
  {
    if (context._turretType && context._turretType->_primaryGunner)
    {
      _context = context;
      return true; // found
    }
    return false; // continue
  }
};

bool Transport::GetPrimaryGunnerTurret(EntityVisualState& vs, TurretContextV &context) const
{
  FindPrimaryGunnerTurretFuncVisualState func(context);
  if (ForEachTurret(vs, func)) return true;

  // if no turret is marked as primary gunner, select driver weapon system
  context._turret = NULL;
  context._turretType = NULL;
  context._turretVisualState = NULL;
  context._gunner = _driver;
  context._weapons = unconst_cast(&_weaponsState);
  return true;
}

/// Functor searching for primary gunner turret
class FindPrimaryGunnerTurretFuncEx : public ITurretFuncEx
{
protected:
  TurretContextEx &_context; // out

public:
  FindPrimaryGunnerTurretFuncEx(TurretContextEx &context) : _context(context) {}
  bool operator () (EntityAIFull *entity, TurretContextEx &context)
  {
    if (context._turretType && context._turretType->_primaryGunner)
    {
      _context = context;
      return true; // found
    }
    return false; // continue
  }
};

bool Transport::GetPrimaryGunnerTurret(EntityVisualState& vs, TurretContextEx &context) const
{
  FindPrimaryGunnerTurretFuncEx func(context);
  if (ForEachTurretEx(vs, func)) return true;

  // if no turret is marked as primary gunner, select driver weapon system
  context._turret = NULL;
  context._turretType = NULL;
  context._turretVisualState = NULL;
  context._gunner = _driver;
  context._weapons = unconst_cast(&_weaponsState);
  context._parentTrans = vs.Orientation();
  return true;
}

/// Functor searching for primary observer turret
class FindPrimaryObserverTurretFunc : public ITurretFunc
{
protected:
  TurretContext &_context; // out

public:
  FindPrimaryObserverTurretFunc(TurretContext &context) : _context(context) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (context._turretType && context._turretType->_primaryObserver)
    {
      _context = context;
      return true; // found
    }
    return false; // continue
  }
};

bool Transport::GetPrimaryObserverTurret(TurretContext &context) const
{
  FindPrimaryObserverTurretFunc func(context);
  return ForEachTurret(func);
}

/// Functor searching for primary observer turret
class FindPrimaryObserverTurretFuncVisualState : public ITurretFuncV
{
protected:
  TurretContextV &_context; // out

public:
  FindPrimaryObserverTurretFuncVisualState(TurretContextV &context) : _context(context) {}
  bool operator () (EntityAIFull *entity, TurretContextV &context)
  {
    if (context._turretType && context._turretType->_primaryObserver)
    {
      _context = context;
      return true; // found
    }
    return false; // continue
  }
};

bool Transport::GetPrimaryObserverTurret(EntityVisualState& vs, TurretContextV &context) const
{
  FindPrimaryObserverTurretFuncVisualState func(context);
  return ForEachTurret(vs, func);
}

/// Functor searching for primary observer turret
class FindPrimaryObserverTurretFuncEx : public ITurretFuncEx
{
protected:
  TurretContextEx &_context; // out

public:
  FindPrimaryObserverTurretFuncEx(TurretContextEx &context) : _context(context) {}
  bool operator () (EntityAIFull *entity, TurretContextEx &context)
  {
    if (context._turretType && context._turretType->_primaryObserver)
    {
      _context = context;
      return true; // found
    }
    return false; // continue
  }
};

bool Transport::GetPrimaryObserverTurret(EntityVisualState& vs, TurretContextEx &context) const
{
  FindPrimaryObserverTurretFuncEx func(context);
  return ForEachTurretEx(vs, func);
}

static const AmmoType *AddMissileToHashMap(MissileCounter &missiles, const Magazine *mag, int &total)
{
  if (!mag) return NULL;
  const MagazineType *type = mag->Type();
  if (!type) return NULL;
  const AmmoType *ammo = type->_ammo;
  if (!ammo) return NULL;
  if (ammo->_simulation != AmmoShotMissile) return NULL;
  if (ammo->maxControlRange < 10) return NULL;
  if (ammo->maverickweaponIndexOffset < 0) return NULL;

  const MissileCounterItem& mItem = missiles.Get(ammo->maverickweaponIndexOffset);
  if (mItem.IsEmpty())
  {
    MissileCounterItem newItem(ammo->maverickweaponIndexOffset);
    total = newItem.Add(type->_maxAmmo);
    missiles.Add(newItem);
  }
  else
  {
    total = mItem.Add(type->_maxAmmo);
  }

  return ammo;
};

LODShape *Transport::GetMissileShape(int proxyIndex) const
{
  // TODO: what turret?
  MissileCounter missiles;
  missiles.CleanUp();
  int total = 0;

  LODShape *shape = NULL;
  bool over = false;
  // track which missile index we are currently processing
  TurretContext context;
  if (GetPrimaryGunnerTurret(context))
  {
    context._weapons->ForEachMagazine([&shape, &over, &total, &missiles, proxyIndex](Magazine *mag) -> bool
    {
      const AmmoType *ammo = AddMissileToHashMap(missiles, mag, total);
      if (!ammo) return false; // continue

      // is this the missile we are interested in?
      if (total + ammo->maverickweaponIndexOffset < proxyIndex) return false; // continue
      if (ammo->maverickweaponIndexOffset > proxyIndex) return false; // continue
      if (total + mag->GetAmmo()- mag->Type()->_maxAmmo + ammo->maverickweaponIndexOffset < proxyIndex)
      {
        over = true;
        return true; // done
      }
      if (ammo->_proxyShape != NULL)
      {
        shape = ammo->_proxyShape;
        return true;
      }
      return false; // continue
    });
    if (over) return NULL;
    if (shape) return shape;
  }

  //pilot weapons
  {
    context._weapons->ForEachMagazine([&shape, &over, &total, &missiles, proxyIndex](Magazine *mag) -> bool
    {
      const AmmoType *ammo = AddMissileToHashMap(missiles, mag, total);
      if(!ammo) return false; // continue

      // is this the missile we are interested in?
      if (total + ammo->maverickweaponIndexOffset<proxyIndex) return false; // continue
      if (total + mag->GetAmmo() - mag->Type()->_maxAmmo + ammo->maverickweaponIndexOffset < proxyIndex)
      {
        over = true;
        return true; // done
      }
      if (ammo->_proxyShape != NULL)
      {
        shape = ammo->_proxyShape;
        return true;
      }
      return false; // continue
    });
    if (over) return NULL;
    if (shape) return shape;
  }

  return NULL;
}

void Transport::CountMissiles(MissileCounter &missiles) const
{
  missiles.CleanUp();
  int total = 0;
  // TODO: what turret?
  TurretContext context;
  if (GetPrimaryGunnerTurret(context))
  {
    context._weapons->ForEachMagazine([&missiles, &total](Magazine *mag) -> bool
    {
      AddMissileToHashMap(missiles, mag, total);
      return false; // continue
    });
  }

  //pilot weapons
  {
    unconst_cast(_weaponsState).ForEachMagazine([&missiles, &total](Magazine *mag) -> bool
    {
      AddMissileToHashMap(missiles, mag, total);
      return false; // continue
    });
  } 
}

bool Transport::CanFireUsing(const TurretContext &context) const
{
  if (!base::CanFireUsing(context)) return false;

#if _VBS2
  if(context._turret && context._turret->_disableGunnerInput) return false;
#endif

  if (context._turretType && Type()->GetHideProxyInCombat() &&
    !context._turretType->_inGunnerMayFire && IsPersonHidden(FutureVisualState(), context._gunner)) return false;

  return true;
}

bool Transport::GetWeaponAim(TurretContext context, Vector3 &position, Vector3 &direction)
{

  Matrix4Val shootTrans = GunTurretTransform(FutureVisualState(), *context._turretType);

  Vector3Val gPosL = context._turretType->GetTurretPos(shootTrans);
  position = FutureVisualState().PositionModelToWorld(gPosL);
  direction = FutureVisualState().DirectionModelToWorld(context._turretType->GetTurretDir(shootTrans));

  return true;
}

void Transport::SimulateGunner(TurretContextEx &context, float deltaT, SimulationImportance prec)
{
  // TODO: remove completely
}

void Transport::SimulateObserver(TurretContextEx &context, float deltaT, SimulationImportance prec)
{
  AIBrain *observerUnit = context._gunner->Brain();
  if (!observerUnit || !observerUnit->LSIsAlive()) return;

  if (context._gunner->IsControlled())
  {
    // observerUnit is partially realized in InGameUI
      KeyboardObserver(context, deltaT);
  }
  else if (observerUnit->IsLocal())
  {
    AIBrain *commanderUnit = CommanderUnit();
    if (observerUnit != commanderUnit)
    {
      bool isCommanderHidden = false;
      if (commanderUnit && commanderUnit->LSIsAlive())
        isCommanderHidden = IsPersonHidden(FutureVisualState(), commanderUnit->GetPerson());
      HidePerson(
        observerUnit->GetPerson(),
        isCommanderHidden || (context._turretType && context._turretType->_forceHideGunner)
        );
    }
  }
}

/// Functor searching for alive gunner
class FindAliveGunner : public ITurretFunc
{
protected:
  const AIBrain *&_unit; // out
public:
  FindAliveGunner(const AIBrain *&unit) : _unit(unit) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turret) return false; // continue
    if (!context._gunner) return false; // continue

    AIBrain *unit = context._gunner->Brain();
    if (unit && unit->LSIsAlive())
    {
      _unit = unit;
      return true; // found
    }
    return false; // continue
  }
};

/// Functor reporting dead gunner
class SimulateGunnerFunc : public ITurretFuncEx
{
protected:
  float _deltaT;
  SimulationImportance _prec;

public:
  SimulateGunnerFunc(float deltaT, SimulationImportance prec) : _deltaT(deltaT), _prec(prec) {}
  bool operator () (EntityAIFull *entity, TurretContextEx &context)
  {
#if _VBS3 //check if it might be overridden
    if (!context._gunner && context._turret && !context._turret->_overriddenByTurret) return false; // continue
#else
    if (!context._gunner) return false; // continue
#endif

    Transport *vehicle = static_cast<Transport *>(entity);

    if (!context._weapons->HasWeapons())
    {
      if (context._turret)
        vehicle->SimulateObserver(context, _deltaT, _prec);
    }
    else
      vehicle->SimulateGunner(context, _deltaT, _prec);
    return false; // do it for all gunners
  }
};

/*!
\patch 1.33 Date 11/29/2001 by Ondra.
- Fixed: MP: Airplanes autofiring from MG or unguided missiles.
*/

void Transport::SimulateAI(float deltaT, SimulationImportance prec)
{
  PROFILE_SCOPE_EX(siATr,sim);

#if _ENABLE_CHEATS
  // pretend we hava a pilot, but fo not perform any activity
  extern bool disableUnitAI;
  if( disableUnitAI ) return;
#endif

  bool isCommanderHidden = true;

  AIBrain *commanderUnit = CommanderUnit();
  AIBrain *driverUnit = PilotUnit();
  AIBrain *observerUnit = EffectiveObserverUnit();
  AIBrain *copilotUnit = CopilotUnit();


  /**/
  const AIBrain *reportedBy = NULL;
  // find any unit that is alive and is able to report the vehicle
  if (driverUnit && driverUnit->LSIsAlive())
  {
    reportedBy = driverUnit;
  }
  FindAliveGunner funcAlive(reportedBy);
  ForEachTurret(funcAlive);
  // search in cargo
  for (int i=0; i<_manCargo.Size(); i++)
  {
    Person *person = _manCargo[i];
    if (!person) continue;
    AIBrain *unit = person->Brain();
    if (!unit || !unit->LSIsAlive()) continue;
    reportedBy = unit;
  }

  // this unit should report any dead units
  if (driverUnit && !driverUnit->LSIsAlive()) driverUnit = NULL;

  if (commanderUnit && commanderUnit->LSIsAlive())
  {
    if (commanderUnit->GetPerson()->IsControlled())
    {
    }
    else if (commanderUnit->GetPerson()->IsRemotePlayer())
    {
      // remote player - avoid fake commander commands
    }
#if _ENABLE_AI
    else if (commanderUnit->IsLocal())
    {
      AICommander(commanderUnit, deltaT);
    }
#endif

    isCommanderHidden = IsPersonHidden(FutureVisualState(), commanderUnit->GetPerson());
  }

  if (observerUnit && observerUnit->LSIsAlive())
  {
    if (observerUnit->GetPerson()->IsControlled())
    {
    }
    else if (observerUnit->GetPerson()->IsRemotePlayer())
    {
      // remote player - avoid fake commander commands
    }
#if _ENABLE_AI
    else if (observerUnit->IsLocal())
    {
      AIObserver(observerUnit, deltaT);
    }
#endif
  }

  VisualState& vs = unconst_cast(FutureVisualState());
  SimulateGunnerFunc funcSimulate(deltaT, prec);
  ForEachTurretEx(vs, funcSimulate);
  
  for (int i=0; i<_manCargo.Size(); i++)
  {
    ManCargoItem &item = _manCargo.Set(i);
    Person *man = item.man;
    if (man && man->IsControlled())
    {
      KeyboardLookAround(man->Brain(), deltaT);
    }
  }
  if (Type()->_hasDriver && Type()->_hideProxyInCombat)
  {
    // if driver hatch is not closed, sometimes we need to close it
    if (IsLocal() && _driverHiddenWanted<0.5f)
    {
      AIBrain *commander = CommanderUnit();
      // check if commander is hidden. If he is, hide as well
      // if there is any fire target ordered, close the has as well
      if (
        commander && PilotUnit()!=commander && IsPersonHidden(FutureVisualState(), commander->GetPerson())
        )
      {
        HideDriver(true);
      }
    }
  }

  if (copilotUnit)
  {
    if (copilotUnit->GetPerson()->IsControlled())
    {
#if _VBS3
      if ( GWorld->GetAllowMovementCtrlsInDlg() || !GWorld->GetPlayerSuspended() )
#else
      if( !GWorld->GetPlayerSuspended())
#endif
      {
        KeyboardPilot(copilotUnit, deltaT);
      }
      else
      {
        SuspendedPilot(copilotUnit, deltaT);
      }
    }
  }
  else if (driverUnit)
  {
    if (driverUnit->GetPerson()->IsControlled())
    {
      if( !GWorld->GetPlayerSuspended())
      {
        KeyboardPilot(driverUnit, deltaT);
      }
      else
      {
        SuspendedPilot(driverUnit, deltaT);
      }
    }
    else if (!driverUnit->GetPerson()->IsLocal())
    {
      FakePilot(deltaT);
    }
#if _ENABLE_AI
    else
    {
      AIPilot(driverUnit, deltaT);
      if (driverUnit && driverUnit != CommanderUnit() && Type()->_hideProxyInCombat)
      {
        // TODO: which gunner
        bool hideDriver = isCommanderHidden;
        HidePerson(
          driverUnit->GetPerson(),
          hideDriver || Type()->_forceHideDriver
          );
      }
    }
#endif //_ENABLE_AI
  }

  base::SimulateAI(deltaT,prec);
}

/// Functor hiding the gunner (if possible)
class HideGunnerFunc : public ITurretFuncV
{
protected:
  bool _hide;

public:
  HideGunnerFunc(bool hide) : _hide(hide) {}
  bool operator () (EntityAIFull *entity, TurretContextV &context)
  {
    if (!context._turret) return false;
    if (!context._gunner) return false;  // continue
    float hidden = context._turretType->_canHideGunner && (_hide || context._turretType->_forceHideGunner) ? 1.0f : 0.0f;
    context._turretVisualState->_crewHidden = hidden;
    context._turret->_crewHiddenWanted = hidden;
    context._gunner->SwitchAction(context._turret->GunnerAction(*context._turretVisualState, *context._turretType));
    context._gunner->OnGetInFinished();
    return false; // continue
  }
};

void Transport::InitUnits()
{
  // called after vehicle is placed in landscape
  // unit are setup and initialized
  AIBrain *unit = CommanderUnit();
  if (unit && Type()->_hideProxyInCombat)
  {
    // initialize hatches - open when in aware or safe mode
    CombatMode cm = unit->GetCombatModeLowLevel();
    bool hide = cm >= CMAware;
    if (Type()->_hasDriver && Driver())
    {
      FutureVisualState()._driverHidden = _driverHiddenWanted = hide || Type()->_forceHideDriver;
      _driver->SwitchAction(DriverAction());
      _driver->OnGetInFinished();
    }
    VisualState& vs = unconst_cast(FutureVisualState());
	HideGunnerFunc func(hide);
    ForEachTurret(vs, func);
  }
  base::InitUnits();
}

bool Transport::ValidateCrew(Person *crew, bool complex) const
{
  if (!crew) return true;
  // check if crew is out
  bool ok = true;
  if (crew->IsInLandscape())
  {
    RptF
      (
      "Error: Crew %s of %s in landscape",
      (const char *)crew->GetDebugName(),
      (const char *)GetDebugName()
      );
    ok = false;
  }
  return ok;
}

/// Functor validating gunner person
class ValidateGunner : public ITurretFunc
{
protected:
  bool &_ok; // out
  bool _complex;

public:
  ValidateGunner(bool &ok, bool complex) : _ok(ok), _complex(complex) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turret) return false; // continue

    Transport *vehicle = static_cast<Transport *>(entity); // we now where we call it
    if (!vehicle->ValidateCrew(context._gunner, _complex))
    {
      RptF("Gunner invalid");
      _ok = false;
    }
    return false; // continue
  }
};

bool Transport::Validate(bool complex) const
{
  // check if all crew members are out
  bool ok = true;
  if (!ValidateCrew(_driver,complex))
  {
    RptF("Driver invalid");
    ok = false;
  }

  ValidateGunner func(ok, complex);
  ForEachTurret(func);

  for (int i=0; i<_manCargo.Size(); i++)
  {
    if (!ValidateCrew(_manCargo[i],complex))
    {
      RptF("ManCargo %d invalid",i);
      ok = false;
    }

  }
  return ok;
}

void Transport::Init( Matrix4Par pos, bool init )
{
  if (!EngineCanBeOff())
  {
    _engineOff = false;
  }
  base::Init(pos, init);
}

class GunnerTrackTargets : public ITurretFunc
{
protected:
  bool _initialize;
  Transport *_vehicle;
  TargetList &_res;
  float *_trackTargetsPeriod;

public:
  GunnerTrackTargets(Transport *vehicle, TargetList &res, bool initialize, float trackTargetsPeriod[NTgtCategory]): _vehicle(vehicle),_initialize(initialize), _res(res)
  {
    _trackTargetsPeriod = trackTargetsPeriod;
  }
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (context._turretType && context._gunner && context._gunner->Brain())
    {
      _vehicle->EntityAIFull::TrackTargets(
        _res, context._gunner->Brain(), _vehicle->GetType()->_gunnerCanSee, _initialize, 1e10,
        _trackTargetsPeriod
        );
    }
    return false;
  }
};

void Transport::TrackTargets(
                             TargetList &res, bool initialize, float trackTargetsPeriod[NTgtCategory]
)
{
  const EntityAIType *type = GetType();
  AIBrain *unit=ObserverBrain();
  if (unit) base::TrackTargets(
    res,unit,type->_commanderCanSee,initialize,1e10,
    trackTargetsPeriod
    );

  GunnerTrackTargets func(this, res, initialize, trackTargetsPeriod);
  ForEachTurret(func);

  //unit=GunnerBrain();
  //if (unit) base::TrackTargets(
  //  res,unit,type->_gunnerCanSee,initialize,1e10,
  //  trackTargetsPeriod
  //  );

  // all units may have driver
  unit=DriverBrain();
  if (unit) base::TrackTargets(
    res,unit,type->_driverCanSee,initialize,1e10,
    trackTargetsPeriod
    );
}

class GunnerTrackOneTarget : public ITurretFunc
{
protected:
  Transport *_vehicle;
  float _trackTargetsPeriod;
  float _maxDist;
  TargetList &_res;
  Target *_target;

public:
  GunnerTrackOneTarget(Transport *vehicle, TargetList &res, float trackTargetsPeriod, float maxDist, Target *target): 
      _vehicle(vehicle),
      _trackTargetsPeriod(trackTargetsPeriod), 
      _maxDist(maxDist), 
      _res(res),
      _target(target)
  {
  }
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (context._turretType && context._gunner && context._gunner->Brain())
    {
        TargetNormal::TrackContext *ctx = _vehicle->InitTrackContext(_res,context._gunner->Brain(), _vehicle->GetType()->_gunnerCanSee,false,_maxDist);
        if (ctx)
        {
          _vehicle->EntityAIFull::TrackOneTarget(_target,context._gunner->Brain(),_trackTargetsPeriod,_maxDist,*ctx);
        }
    }
    return false;
  }
};

void Transport::TrackOneTarget(TargetList &res, Target *target, float trackTargetsPeriod)
{
  const EntityAIType *type = GetType();
  float maxDist = TACTICAL_VISIBILITY;
  AIBrain *unit=ObserverBrain();
  if (unit)
  {
    TargetNormal::TrackContext *ctx = InitTrackContext(res,unit,type->_commanderCanSee,false,maxDist);
    if (ctx)
    {
      base::TrackOneTarget(target,unit,trackTargetsPeriod,maxDist,*ctx);
    }
  }

  GunnerTrackOneTarget func(this, res, trackTargetsPeriod, maxDist, target);
  ForEachTurret(func);


  // all units may have driver
  unit=DriverBrain();
  if (unit)
  {
    TargetNormal::TrackContext *ctx = InitTrackContext(res,unit,type->_driverCanSee,false,maxDist);
    if (ctx)
    {
      base::TrackOneTarget(target,unit,trackTargetsPeriod,maxDist,*ctx);
    }
  }
}

Vector3 Transport::GetCameraDirection( CameraType camType ) const
{
  VisualState& vs = unconst_cast(RenderVisualState());
  // used for external camera
  AIBrain *unit = GWorld->FocusOn();
  Person *man = unit ? unit->GetPerson() : NULL;

  // first check if person is hidden
  if (man)
  {
    // check proxy direction
    Matrix4 transf;
    Vector3 proxyEyeDir = VForward;
    if (GetProxyCamera(transf, camType))
    { //looking around for 3rd person view (using head direction)
      proxyEyeDir = man->GetCrewLookDirection(man->RenderVisualState());
    }
    if (man == _driver)
    {
      if (vs.IsDriverHidden()) return vs.Transform().Rotate(proxyEyeDir);
    }
    else
    {
      TurretContextV context;
      if (FindTurret(vs, man, context))
      {
        if (
          (context._turret && (context._turretVisualState->IsGunnerHidden() || context._turretType->OutGunnerMayFire(context._gunner))) ||
          !Type()->_hideProxyInCombat)
        {
          transf.SetUpAndDirection(VUp, proxyEyeDir); //combine head direction with weapon/turret
          WeaponModePtr ptr = context._weapons->ValidatedCurrentWeapon();
          if (ptr.IsValid())
            return transf.Rotate(GetWeaponDirection(vs, context, ptr));
          else
          {
            // do not look around with player
            if (man == GLOB_WORLD->PlayerOn())
            {
              Vector3 dir = vs.DirectionModelToWorld(man->GetCrewLookDirection(man->GetVisualStateByAge(GetVisualStateAge(vs))));
              return transf.Rotate(dir);
            }
            else
              return transf.Rotate(GetLookAroundDirection(vs, context));
          }
        }
      }
    }
  }

  // check proxy direction
  Matrix4 transf;
  if (GetProxyCamera(transf, camType))
  {
    if (man /*&& !QIsManual(unit)*/)
    {
      Vector3 eyeDir = man->GetCrewLookDirection(man->RenderVisualState());
      return vs.DirectionModelToWorld(transf.Rotate(eyeDir));
    }
    return vs.DirectionModelToWorld(transf.Direction());
  }

  // failed
  return vs.Direction();
}

Matrix4 Transport::TurretTransform(const TurretType &type) const
{
  Matrix4 mat = MIdentity;
  int memory = GetShape()->FindMemoryLevel();
  if (memory >= 0)
  {
    SkeletonIndex bone = type.GetBodyBone(Type(), memory);
    AnimateBoneMatrix(mat, FutureVisualState(), memory, bone);
  }
  return mat;
}

Matrix4 Transport::GunTurretTransform(ObjectVisualState const& vs, const TurretType &type) const
{
  Matrix4 mat = MIdentity;
  int memory = GetShape()->FindMemoryLevel();
  if (memory >= 0)
  {
    SkeletonIndex bone = type.GetGunBone(Type(), memory);
    AnimateBoneMatrix(mat, vs, memory, bone);
  }
  return mat;
}

Vector3 Transport::GetEyeDirection(ObjectVisualState const& vs, const TurretContext &context) const
{
  AIBrain *unit = EffectiveObserverUnit();
  if (!unit || unit!=PilotUnit() || QIsManual(unit))
    return base::GetEyeDirection(vs, context);
  Person *person = unit->GetPerson();
  if (!person)
    return base::GetEyeDirection(vs, context);
  Vector3 dir = person->GetCrewHeadDirection();
  // TODO: convert from the proxy space
  return vs.DirectionModelToWorld(dir);
}

Vector3 Transport::GetEyeDirectionWanted(ObjectVisualState const& vs, const TurretContextEx &context) const
{
  AIBrain *unit = EffectiveObserverUnit();
  if (!unit || unit!=PilotUnit() || QIsManual(unit))
    return base::GetEyeDirectionWanted(vs, context);
  Person *person = unit->GetPerson();
  if (!person)
    return base::GetEyeDirectionWanted(vs, context);
  Vector3 dir = person->GetCrewHeadDirectionWanted();
  // TODO: convert from the proxy space
  return vs.DirectionModelToWorld(dir);
}

Vector3 Transport::GetLookAroundDirection(ObjectVisualState const& vs, const TurretContext &context) const
{
  AIBrain *unit = EffectiveObserverUnit();
  if (!unit || unit!=PilotUnit() || QIsManual(unit))
    return base::GetLookAroundDirection(vs, context);
  Person *person = unit->GetPerson();
  if (!person)
    return base::GetLookAroundDirection(vs, context);
  Vector3 dir = person->GetCrewLookDirection(person->GetVisualStateByAge(GetVisualStateAge(vs)));
  // TODO: convert from the proxy space
  return vs.DirectionModelToWorld(dir);
}

Matrix4 Transport::InsideCamera( CameraType camType ) const
{
  Matrix4 transf;
  if (camType == CamGunner && GetOpticsCamera(transf, camType))
    return transf;
  if (GetProxyCamera(transf, camType))
    return transf;
  // no proxy: return vehicle position
  return MIdentity;
}


Matrix4 Transport::InternalCameraTransform(Matrix4 &base, CameraType camType) const
{
  // perform calculations in relative space to avoid world space rounding errors
  const VisualState &vs = RenderVisualStateScope();
  base = vs.Transform();
  Matrix4 transf = InsideCamera(camType);

  AIBrain *unit = GWorld->FocusOn();
  Person *man = unit ? unit->GetPerson() : NULL;

  // first check if person is hidden
  if (man)
  {
    if (man == _driver || man == _copilot)
    {
      if (vs.IsDriverHidden())
        transf.SetOrientation(M3Identity);
    }
    else
    {
      TurretContextV context;
      if (FindTurret(unconst_cast(vs), man, context))
      {
        if (
          (context._turret && (context._turretType->GunnerMayFire(context))) ||
          !Type()->_hideProxyInCombat)
        {
          WeaponModePtr ptr = context._weapons->ValidatedCurrentWeapon();
          // TODO: calculate GetWeaponDirection/GetEyeDirection in model space
          if (ptr.IsValid())
            transf.SetDirectionAndUp(vs.DirectionWorldToModel(GetWeaponDirection(vs, context, ptr)), VUp);
          else
            // do not look around with player
            if (man != GLOB_WORLD->PlayerOn())
              transf.SetDirectionAndUp(vs.DirectionWorldToModel(GetLookAroundDirection(vs, context)), VUp);
        }
      }
    }
  }

  if (man)
  {
    // we want to change the view direction based on the crew eye direction
    Vector3 eyeDir = transf.Rotate(man->GetCrewLookDirection(man->GetVisualStateByAge(GetVisualStateAge(vs))));
    Matrix3 rot(MDirection,eyeDir,VUp);

#if _VBS3
    if(man == _driver)
    {
      float heading,dive,fov;
      Type()->_viewPilot.InitVirtual(camType,heading,dive,fov);

      Matrix3 mdive(MRotationX,-dive);
      Matrix3 mheading(MRotationY,heading);

      rot = rot*(mheading*mdive);
    }
#endif
    transf.SetOrientation(rot);


    //Shift the head position (for instance using TrackIR)
    Man *theMan = dynamic_cast<Man *>(man);
    if (theMan)
    {
      Vector3 shiftPos = theMan->RenderVisualState().HeadShift();
      float zRotAngle = -0.5*H_PI*theMan->RenderVisualState().HeadRoll();

      // saturate
      const ViewPars *viewPars = &Type()->_viewPilot;
      if (GWorld->GetCameraType()==CamGunner)
      {
        TurretContext context;
        if (FindTurret(man, context) && context._turret)
        {
          int opticsMode = (context._turret)?context._turret->_currentViewOpticsMode : 0;
          viewPars = &context._turretType->_viewOptics[opticsMode];
          //if there are multiple optics, zoom/out will cycle trough them
        }
        else viewPars=&Type()->_viewOptics;
      }
      saturate(shiftPos[0],viewPars->_minMoveX,viewPars->_maxMoveX);
      saturate(shiftPos[1],viewPars->_minMoveY,viewPars->_maxMoveY);
      saturate(shiftPos[2],viewPars->_minMoveZ,viewPars->_maxMoveZ);

      // Combine with Head ROLL
      // update position by the Head Tilt, neck is 20cm below the eyes
      const HeadPars &headLimits = theMan->GetType()->_headLimits;
      headLimits.LimitRoll(zRotAngle);
      float deltaX = -headLimits._rotZRadius * sin(zRotAngle);
      float deltaY = -headLimits._rotZRadius * (1-cos(zRotAngle));
      Vector3 eyeShift(deltaX, deltaY, 0);
      Matrix4 rollMatrix(M4Identity);
      rollMatrix.SetPosition(eyeShift);

      // shiftPos = rot.InverseRotation() * shiftPos;
      Matrix4 shiftMatrix(M4Identity);
      shiftMatrix.SetPosition(shiftPos);
      transf = shiftMatrix * transf * rollMatrix;  // position

      Matrix3P rollRot; rollRot.SetRotationZ(zRotAngle);
      transf.SetOrientation(rollRot * rot); // orientation
    }
  }

  // add camera shake
  GWorld->GetCameraShakeManager().UpdateCameraTransform(transf);
  return transf;
}

int Transport::InternalLOD(UIActionType position) const
{
  int index = -1;
  switch (position)
  {
  case ATGetInTurret:
    index = GetShape()->FindSpecLevel(VIEW_GUNNER);
    if (index < 0)
    {
      // check if we should use gunner or cargo view
      TurretContext context;
      if (GetPrimaryGunnerTurret(context) && (!context._turretType || context._turretType->_gunnerUsesPilotView))
      {
        index = GetShape()->FindSpecLevel(VIEW_PILOT);
        if (index < 0) index = GetShape()->FindSpecLevel(VIEW_CARGO);
      }
      else
      {
        index = GetShape()->FindSpecLevel(VIEW_CARGO);
        if (index < 0) index = GetShape()->FindSpecLevel(VIEW_PILOT);
      }
    }
    break;
  case ATGetInDriver:
  case ATGetInPilot:
    index = GetShape()->FindSpecLevel(VIEW_PILOT);
    if (index < 0) index = GetShape()->FindSpecLevel(VIEW_CARGO);
    break;
  case ATGetInCargo:
    index = GetShape()->FindSpecLevel(VIEW_CARGO);
    if (index < 0) index = GetShape()->FindSpecLevel(VIEW_PILOT);
    break;
  }
  return index;
}

int Transport::InsideLOD(CameraType camType) const
{
  AIBrain *unit = GWorld->FocusOn();
  if (!unit) return 0;
  Person *player = unit->GetPerson();
  if (!player) return 0;

  // preciseGetInOut: show external lod when get in/out animation 
  Man *man = dyn_cast<Man>(player);
  if (man && man->RenderVisualState()._gettingInOut) return 0;


  VisualState& vs = unconst_cast(RenderVisualState());

// moved to ARMA2
//#if _VBS3 //select LOD based on turret entry (if available)
  TurretContextV context;
  if (FindTurret(vs, player, context) && context._turret && context._turretType)
  {
    int index = -1;
    if(context._turretVisualState->IsGunnerHidden())
      index = context._turretType->_lodTurnedIn >=0 ? GetShape()->FindSpecLevel(context._turretType->_lodTurnedIn) : -1;
    else
      index = context._turretType->_lodTurnedOut >= 0 ? GetShape()->FindSpecLevel(context._turretType->_lodTurnedOut) : -1;

    if(index >= 0)
      return index;
  }
// moved to ARMA2
//#endif

  // view for out positions
  if (Type()->_hideProxyInCombat)
  {
    if (player == Driver())
    {
      if (!vs.IsDriverHidden()) return 0;
    }
    else
    {
      TurretContextV context; 
      if (FindTurret(vs, player, context) && context._turret)
      {
        if (!context._turretVisualState->IsGunnerHidden()) return 0;
      }
    }
  }

  // gunner camera
  if (camType == CamGunner)
  {
    int index = GetShape()->FindSpecLevel(VIEW_GUNNER);
    if (index >= 0) return index;
    return 0;
  }

  // internal camera
  if (player == Gunner())
    return InternalLOD(ATGetInTurret);
  else if (player == Observer())
    return InternalLOD(ATGetInTurret);
  else if (player == Driver())
    return InternalLOD(ATGetInDriver);
  else
    return InternalLOD(ATGetInCargo);
}

bool Transport::IsVirtual(CameraType camType) const
{
  if (camType == CamGunner)
  {
    AIBrain *unit = GWorld->FocusOn();
    Person *player = unit ? unit->GetPerson() : NULL;
    if (player && player == Gunner())
      return false;
  }
  return true;
}

bool Transport::IsVirtualX(CameraType camType) const
{
  AIBrain *unit = GWorld->FocusOn();
  Person *player = unit ? unit->GetPerson() : NULL;
  if (player)
  {
    if (player==Driver())
    {
      if (unit->LookAroundEnabled())
        return true;
      return camType!=CamInternal && camType!=CamExternal;
    }
  }
  return true;
}

bool Transport::IsGunner(CameraType camType) const
{
  // check unit position
  AIBrain *unit = GWorld->FocusOn();
  if (unit && unit==PilotUnit())
    return base::IsGunner(camType);
  if (camType == CamInternal)
  {
    if (!Type()->_hideProxyInCombat)
      return true;
    TurretContext context;
    if (unit && FindTurret(unit->GetPerson(), context) && context._turretType)
    {
      if (context._turretType->OutGunnerMayFire(context._gunner))
        return true;
    }
  }
  return camType==CamGunner || camType==CamExternal;
}

void Transport::LimitVirtual(CameraType camType, float &heading, float &dive, float &fov) const
{
  if (camType==CamInternal || camType==CamExternal)
  {
    // internal camera - default processing
    AIBrain *unit = GWorld->FocusOn();
    Person *person = unit ? unit->GetPerson() : NULL;
    if (person)
    {
      VisualState const& vs = RenderVisualState();

      TurretContextV context;
      if (person == _driver)
        Type()->_viewPilot.LimitVirtual(camType,heading,dive,fov);
      else if (FindTurret(unconst_cast(vs), person, context) && context._turret)
      {
        int opticsMode = (context._turret)?context._turret->_currentViewGunnerMode : 0;
        context._turretType->_viewGunner[opticsMode].LimitVirtual(camType, heading, dive, fov);
        if (context._turretVisualState->GetGunnerHidden() < 0.5 && context._turretType->OutGunnerMayFire(context._gunner))
        {
          heading = 0;
          dive = 0;
        }
      }
      else
        Type()->_viewCargo.LimitVirtual(camType,heading,dive,fov);
      return;
    }
  }
  else if (camType == CamGunner)
    Type()->_viewOptics.LimitVirtual(camType,heading,dive,fov);

  base::LimitVirtual(camType,heading,dive,fov);
}

/*!
\patch_internal 1.27 Date 10/10/2001 by Jirka
- Changed: optics fov, heading and dive limits and initials moved from program into config
\patch_internal 1.28 Date 10/26/2001 by Jirka
- Fixed: initial parameters for optics was replaced by base::InitVirtual
*/
void Transport::InitVirtual(CameraType camType, float &heading, float &dive, float &fov) const
{
  AIBrain *unit = GWorld->FocusOn();
  // driver or not internal - default processing
  Person *person = unit ? unit->GetPerson() : NULL;
  if (camType == CamGunner)
    Type()->_viewOptics.InitVirtual(camType,heading,dive,fov);
  else if (person)
  {
    TurretContext context;
    if (person==_driver)
      Type()->_viewPilot.InitVirtual(camType,heading,dive,fov);
    else if (FindTurret(person, context) && context._turret)
    { 
      int opticsMode = (context._turret)?context._turret->_currentViewGunnerMode : 0;
      context._turretType->_viewGunner[opticsMode].InitVirtual(camType, heading, dive, fov);
    }
    else
      Type()->_viewCargo.InitVirtual(camType,heading,dive,fov);
  }
  else
    // driver or not internal - default processing
    base::InitVirtual(camType,heading,dive,fov);
}

void Transport::OverrideCursor(Vector3 &dir) const
{
  AIBrain *unit = GWorld->FocusOn();
  Person *person = unit ? unit->GetPerson() : NULL;
  if (person)
  {
    TurretContextV context;
    VisualState& vs = unconst_cast(RenderVisualState());
    bool isTurret = FindTurret(vs, person, context);
    if (isTurret && !IsGunnerLookingAround(context))
    {
      WeaponModePtr ptr = context._weapons->ValidatedCurrentWeapon();
      if (ptr.IsValid())
      {
        dir = GetWeaponDirection(vs, context, ptr);
        return;
      }
    }
    if (unit->LookAroundEnabled() || 
      (isTurret && context._weapons && context._gunner && !context._weapons->ValidatedCurrentWeapon().IsValid() && _copilot != context._gunner) || (context._gunner && _copilot == context._gunner)) // gunner with no weapons that is not copilot or copilot
    { //freelook
      //return the center of the screen
      const FrameBase *cam = GScene->GetCamera();
      if (cam)
      {
        dir = cam->Direction();
        return;
      }
      else
        dir = vs.Direction();
    }
  }
  // driver or not internal - default processing
  base::OverrideCursor(dir);
}

bool Transport::ConsumeFuel(float ammount)
{
#if _VBS3
  if(Glob.config.IsEnabled(DTUnlimitedFuel)
#if _EXT_CTRL  //no fuel consumption
    || IsExternalControlled()
#endif
    ) //ext. Controller like HLA, AAR
    return true;
#endif
  bool wasFuel = FutureVisualState()._fuel>0;
  FutureVisualState()._fuel -= ammount;
  bool isFuel = FutureVisualState()._fuel>0;
  if (isFuel!=wasFuel) OnEvent(EEFuel,isFuel);
  saturate(FutureVisualState()._fuel,0,GetType()->GetFuelCapacity());
  return isFuel;
}

void Transport::Refuel(float ammount)
{
  ConsumeFuel(-ammount);
}

/*!
\patch 5135 Date 2/27/2007 by Jirka
- Fixed: AI deprecated to get in heavy damaged vehicle even when repaired again
*/

void Transport::Repair(float amount)
{
  base::Repair(amount);
  if (!EjectNeeded()) _getOutAfterDamage = TIME_MAX; // never get out
}

void Transport::StabilizeTurrets(Matrix3Val oldTrans, Matrix3Val newTrans)
{
  for (int i=0; i<_turrets.Size(); i++)
  {
    TurretType *turretType = Type()->_turrets[i];
    Turret *turret = _turrets[i];
    TurretVisualState *turretVisualState = FutureVisualState()._turretVisualStates[i];
    turret->Stabilize(*turretVisualState, this, *turretType, oldTrans, newTrans);
  }
}

/// Functor executing the basic simulation of gunner
class BasicGunnerSimulation : public ITurretFunc
{
protected:
  float _deltaT;
  SimulationImportance _prec;
#if _VBS2
  bool _simulatePost;
  Matrix3 _diffFrame;
#endif


public:
  BasicGunnerSimulation(float deltaT, SimulationImportance prec)
  : _deltaT(deltaT), _prec(prec)
  {
#if _VBS2
    _simulatePost = false;
#endif
  }
#if _VBS3
  BasicGunnerSimulation(float deltaT, SimulationImportance prec,bool simulatepost,Matrix3 diffFrame)
  : _deltaT(deltaT), _prec(prec), _simulatePost(simulatepost), _diffFrame(diffFrame)
  {}
#endif
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (context._turret && context._gunner)
    {
      float animSpeed = context._turret ? context._turret->GunnerAnimSpeed() : 1.0f;
      context._gunner->BasicSimulation(_deltaT, _prec, animSpeed);
    }
    return false; // continue
  }
};

class GunnerShakeEffect : public ITurretFunc
{
protected:
  Vector3 _force;
  float _deltaT;
  Transport *_vehicle;
public:
  GunnerShakeEffect(float deltaT,Transport *vehicle, Vector3 force): _deltaT(deltaT), _force(force),_vehicle(vehicle) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (context._turretType && context._gunner)
    {
      Matrix4 turretTrans = _vehicle->TurretTransform(*context._turretType);
      if (turretTrans.Scale()>0)
      {
        context._gunner->RecalcShakeTransform(turretTrans.InverseRotation() * _force,_deltaT);
      }
    }
    return false;
  }
};

void Transport::Simulate(float deltaT, SimulationImportance prec)
{
  // turn in / turn out
  const float turnInOutSpeed = 1.0;
  if (_driverHiddenWanted > FutureVisualState()._driverHidden)
  {
    FutureVisualState()._driverHidden += turnInOutSpeed * deltaT;
    saturateMin(FutureVisualState()._driverHidden, _driverHiddenWanted);
    if (_driver)
      _driver->SwitchAction(DriverAction());
  }
  else if (_driverHiddenWanted < FutureVisualState()._driverHidden)
  {
    FutureVisualState()._driverHidden -= turnInOutSpeed * deltaT;
    saturateMax(FutureVisualState()._driverHidden, _driverHiddenWanted);
    if (_driver)
      _driver->SwitchAction(DriverAction());
  }

  // advance animation of proxy objects
  if (_driver)
  {
    _driver->BasicSimulation(deltaT, prec, DriverAnimSpeed());
    _driver->RecalcShakeTransform(DirectionWorldToModel(FutureVisualState()._acceleration+_impulseForce),deltaT);

#if _ENABLE_HAND_IK
    if (prec <= SimulateVisibleNear)
    {
      PROFILE_SCOPE(pltIKsim);
      int level = 0;
      const LevelProxies &proxies = Type()->_proxies[level];

      if (GWorld->GetCameraType() != CamExternal && GWorld->GetCameraType() != CamGroup)
      {
        if (GWorld->PlayerOn() == Gunner()) level = InternalLOD(ATGetInTurret);
        else if (GWorld->PlayerOn() == Observer()) level = InternalLOD(ATGetInTurret);
        else if (GWorld->PlayerOn() == Driver()) level = InternalLOD(ATGetInDriver);
        else level = InternalLOD(ATGetInCargo);
      }

      if (level != LOD_INVISIBLE && proxies._driverProxy.Present() && !(level>0 && FutureVisualState().IsDriverHidden()))
      {
        FutureVisualState()._IKIdent=false;
        RecalcIKTransform(level, _driver,  proxies._driverProxy, Type()->_driverLeftHandIndex, Type()->_driverRightHandIndex,Type()->_driverLeftLegIndex, Type()->_driverRightLegIndex, FutureVisualState()._leftHandIK, FutureVisualState()._rightHandIK, FutureVisualState()._leftLegIK, FutureVisualState()._rightLegIK);    
      }
    }
#endif
  }
  BasicGunnerSimulation func(deltaT, prec);
  ForEachTurret(func);

  GunnerShakeEffect funcShake(deltaT,this, DirectionWorldToModel(FutureVisualState()._acceleration+_impulseForce));
  ForEachTurret(funcShake);
  for (int i=0; i<_manCargo.Size(); i++)
  {
    ManCargoItem &item = _manCargo.Set(i);
    Person *man = item.man;
    if (man)
    {
      man->BasicSimulation(deltaT, prec, CargoAnimSpeed(i));
      man->RecalcShakeTransform(DirectionWorldToModel(FutureVisualState()._acceleration+_impulseForce),deltaT);
    }
  }

  // simulate owned weapons
  ForEachWeapon([deltaT, prec](Weapon *weapon) -> bool
  {
    weapon->Simulate(deltaT, prec);
    return false; // continue
  });

  bool moved = false;

  Assert(FutureVisualState()._turretVisualStates.Size() == _turrets.Size());
  for (int i=0; i<_turrets.Size(); i++)
  {
    TurretType *turretType = Type()->_turrets[i];
    Turret *turret = _turrets[i];
    TurretVisualState *turretVisualState = FutureVisualState()._turretVisualStates[i];
    if (turret->Simulate(*turretVisualState, this, *turretType, deltaT, prec, FutureVisualState().Orientation()))
      moved = true;
  }
  if (moved)
  {
    CancelStop();
    EngineOn();
  }
  if (_isDestroyed && _destroyMaxHeatTime > Glob.time)
  {
    float dt = 1.0f - ((_destroyMaxHeatTime - Glob.time) / MaxDamageHeatUpTime);
    _engineTemperatureCoef = dt * 0.6f;
  }
  // Simulate the engine temperature (alive factor)
  if (_engineOff)
  {
    static float hoursToCoolDown = 0.5f; // 1 hour to cool down
    const float invSecToCoolDown = 1.0f / (hoursToCoolDown * 60.0f * 60.0f);
    _engineTemperatureCoef -= deltaT * invSecToCoolDown;
  }
  else
  {
    static float hoursToHeatUp = 10.0f / 60.0f; // 10 minutes to heat up
    const float invSecToHeatUp = 1.0f / (hoursToHeatUp * 60.0f * 60.0f);
    _engineTemperatureCoef += deltaT * invSecToHeatUp;
  }
  saturate(_engineTemperatureCoef, 0.0f, 1.0f);

  // Simulate the wheel temperature (movement factor)
  const float minSpeedToHeatUp = 1.0f; // Speed in m/s
  if (FutureVisualState().GetSpeedHorizontal() < minSpeedToHeatUp)
  {
    static float hoursToCoolDown = 10.0f / 60.0f; // 10 minutes to cool down
    const float invSecToCoolDown = 1.0f / (hoursToCoolDown * 60.0f * 60.0f);
    _wheelTemperatureCoef -= deltaT * invSecToCoolDown;
  }
  else
  {
    static float hoursToHeatUp = 2.0f / 60.0f; // 2 minutes to heat up
    const float invSecToHeatUp = 1.0f / (hoursToHeatUp * 60.0f * 60.0f);
    _wheelTemperatureCoef += deltaT * invSecToHeatUp;
  }
  saturate(_wheelTemperatureCoef, 0.0f, 1.0f);

  // Simulate (main) turret gun heat factor
  TurretContext context;
  if (GetPrimaryGunnerTurret(context))
  {
    if (context._turret)
      _turretGunTemperatureCoef = context._turret->_heatFactor;
  }

  _radio.Simulate(deltaT,this);

  base::Simulate(deltaT,prec);
  ReactToDamage();

  // markers on / off
  if (!CommanderUnit() || CommanderUnit()->IsInCargo() || !CommanderUnit()->LSCanProcessActions())
  {
    // empty vehicle - leave light status as it is
    // _pilotLight = false;
    // _collisionLight = true;
  }
  else if (CommanderUnit()->HasAI()
    //don't override lights if the driver is human
    && (!PilotUnit() || PilotUnit()->HasAI()) && !CopilotUnit()
    )
  {
    // vehicle with AI driver
#if _VBS3 && _EXT_CTRL
    // leave lights alone when replaying
    if(!GAAR.IsPlaying())
#endif
      _pilotLight = false;
    bool pilotLightBak = _pilotLight;
    _pilotLight = LightsNeeded();
    _collisionLight = false;
    float thold = _randomizer * 0.6 + 0.2;
    bool cautiousOrDanger = IsCautiousOrDanger();
    if(!cautiousOrDanger) _collisionLight = true;

    if (GScene->MainLight()->NightEffect() > thold)
    {
      if (!cautiousOrDanger)
      {
        //LogF("%s: %.0f on",(const char *)GetDebugName(),Glob.time-Time(0));
#if _VBS3 && _EXT_CTRL
        // leave lights alone when replaying
        if(!GAAR.IsPlaying())
#endif
          if(!GetType()->IsKindOf(GWorld->Preloaded(VTypeAir)))  _pilotLight = true; //heli AI does not need reflector
          _collisionLight = true;
      }
      else
      {
        //LogF("%s: %.0f off",(const char *)GetDebugName(),Glob.time-Time(0));
#if _VBS3 && _EXT_CTRL
        Soldier* soldier = dyn_cast<Soldier>(Driver());
        if(soldier && soldier->IsWearingNVG())
        {
#if _VBS3
          // leave lights alone when replaying
          if(!GAAR.IsPlaying())
#endif
            _pilotLight = false;
            _collisionLight = false;
        }
#endif
      }
    }
    if ( _pilotLight!=pilotLightBak && !IsLocal() )
    {
      GetNetworkManager().AskForPilotLight(this, _pilotLight);
    }
  }

  if (Glob.time > _showDmgValid)
    _showDmg = false;

  /// camera shake - vehicle movement
  if (!_engineOff)
      {
    if (Glob.time < _lastCamShakeTime || (Glob.time - _lastCamShakeTime) > CameraShakeManager::DefaultPassingVehicleUpdateTime)
      {
      GWorld->GetCameraShakeManager().AddSource(CameraShakeParams(CameraShakeSource::CSVehicle, FutureVisualState().Position(), 0, this));
      _lastCamShakeTime = Glob.time;
      }
    }

  //check if copilot is in vehicle
  if (_copilot && IsLocal())
  {
    if (CopilotBrain() && (!CopilotBrain()->GetVehicleIn() || CopilotBrain()->GetVehicleIn() != this))
    {
      SetCopilot(NULL);
    }
  }

  EvaluateSoundControllers();

  for (int i=0; i < Type()->_doors.Count();i++)
  {
    float doorPos = FutureVisualState().GetDoorState(i);

    float doorPosWanted;
    if (FutureVisualState()._doors[i]._open) doorPosWanted = 1;
    else doorPosWanted = 0;

    float delta = doorPosWanted-doorPos;
    float limit = deltaT* Type()->_doors[i]->GetAnimSpeed();
    Limit(delta,-limit,limit);
    doorPos += delta;
    Limit(doorPos,0,1); 
    FutureVisualState().SetDoorState(i,doorPos);
  }
}


bool Transport::HasTiOptics() const
{
  TurretContext context;
  if (GetPrimaryGunnerTurret(context) && context._turret)
  {
    return context._turret->HasThermalModes(context._turretType);
  }

  return false;
}

bool Transport::NextVisionMode()
{
  const ViewPars &vPars = Type()->_viewOptics;

  if (vPars.HasVisionModes())
    {
    vPars.NextVisionMode(_visionMode, _tiIndex);
    return true;
  }

  return false;
}

bool Transport::HasVisionModes() const
{
  return (Type()->_viewOptics._visionModes.Size() != 0);
}

int Transport::VisionModesCount() const
{
  return (Type()->_viewOptics._visionModes.Size());
}

void Transport::EnableVisionModes(bool val)
{
  _enableVisionMode = val;
}

void Transport::SetAllowCrewInImmobile(bool allow) 
{
  _allowCrewInImmobile = allow;
}


VisionMode Transport::GetVisionModePars(int &flirIndex)
{
  flirIndex = _tiIndex;
  return _visionMode;
}

bool Transport::GetPostProcessEffects(const TurretContextV &context, AutoArray<PPEffectType> **ppEffects) const
{
  // if person is a driver
  if (context._gunner == Driver())
      {
    if (!Type()->_hideProxyInCombat) return false;

    if ((GWorld->GetCameraType() != CamExternal) && RenderVisualState().IsDriverHidden()) 
    {
      *ppEffects = const_cast< AutoArray<PPEffectType> * >(&Type()->_opticsType);
      
      return (Type()->_opticsType.Size() > 0);
    }
  }
  else
  {
    // when _viewGunnerInExternal is set, we assume gunner is actually outside
    bool isInside = (GWorld->GetCameraType() == CamGunner) && context._turret && (!Type()->_hideProxyInCombat || context._turretVisualState->IsGunnerHidden()) && !context._turretType->_viewGunnerInExternal;
   
    if (context._turretType)
    {
      const AutoArray<PPEffectType> &tmp = isInside ? context._turretType->_opticsType : context._turretType->_opticsOutType;
      *ppEffects = const_cast< AutoArray<PPEffectType> * > (&tmp);

      return (tmp.Size() > 0);
    }
  }

  return false;
}

#if _ENABLE_CHEATS
# if !_RELEASE
#   define ARROWS 0
# else
#   define ARROWS 0
# endif
#endif

// Simplified interaction of persons with cars. (Persons are not physicals objects, they must be simulated in a simplyfied way.
void Transport::ObjectCollisionWithPerson(CollisionBuffer objColl, unsigned int fromIndx, unsigned int toIndx, float& appliedImpulseObj)
{
  Assert(fromIndx < toIndx);
  Assert(IsLocal());

  // Find average point and direction.
  Vector3 avgPoint = VZero;
  Vector3 avgDir = VZero;
  Person * obj = dyn_cast<Person, Object>(objColl[fromIndx].object);

  Assert(obj != NULL);

  int lastcomponent = -1;
  for(unsigned int i = fromIndx; i < toIndx; i++)
  {
    CollisionInfo & info =  objColl[i];
    avgPoint += info.pos;

    if (lastcomponent != info.component)  // it finds better direction
    {
      avgDir += info.dirOutNotNorm;  
      lastcomponent = info.component;
    }
  }

  avgPoint /= (toIndx - fromIndx);  
  avgDir.Normalize();

  // Calculate Collision in this point
  float carSpeed = SpeedAtPoint(avgPoint) * avgDir;
  float personSpeed = obj->FutureVisualState().DirectionModelToWorld(obj->FutureVisualState().ModelSpeed()) * avgDir;
  float diffSpeed = carSpeed - personSpeed;

  if (diffSpeed >= 0) // nothing to do
    return;

  // In situation where man crashed into car let man solve it...
  if (personSpeed >= -diffSpeed/2)
    return;

  float reaction = avgDir * DiffAccelerationAtPointOnForceAtPoint(avgPoint, avgDir ,avgPoint) + obj->GetInvMass();

  float restitutionCoef = 1 + (RigidityCoef() + 0.25) / 2;

  float impulseSize = (-diffSpeed * restitutionCoef / reaction);
  Vector3 impulse = avgDir * impulseSize;
  impulseSize = fabsf(impulseSize);

  Vector3 centerOfMass(VFastTransform,FutureVisualState().ModelToWorld(),GetCenterOfMass());
  Vector3 torque = (avgPoint - centerOfMass).CrossProduct(impulse);

  // Apply Impulses
  ApplyImpulse(impulse, torque);
  DamageOnImpulse(this, impulseSize, PositionWorldToModel(avgDir));

  // Because to switch a person into freefall mode, impulseForce must be set, ApplyImpulse cannot be used.
  // I am adding impulse also for local objects. 
  if (!obj->IsLocal())
    obj->AddImpulse(-impulse, VZero);

  obj->AddImpulseNetAware(-impulse, VZero);

  //obj->DammageOnImpulse(obj, fImpulseSize, cAvgDirModel);

  appliedImpulseObj += impulseSize;
}

Vector3 Transport::WreckFriction( Vector3Par speed )
{
  Vector3 friction;
  friction.Init();
  friction[0]=speed[0]*fabs(speed[0])*10+speed[0]*20+fSign(speed[0])*10;
  friction[1]=speed[1]*fabs(speed[1])*7+speed[1]*20+fSign(speed[1])*10;
  //friction[2]=speed[2]*fabs(speed[2])*5+speed[2]*20+fSign(speed[2])*10;
  friction[2]=speed[2]*fabs(speed[2])*9+speed[2]*110+fSign(speed[2])*10;
  return friction*GetMass()*(1.0/1700);
}

#define LimitFriction(f,s,t,im) (f)

void Transport::SimulateWreck(float deltaT, SimulationImportance prec)
{
  //***************************
  // Section 1. Not really connected with physical simulation.
  //***************************

  _isUpsideDown=FutureVisualState().DirectionUp().Y()<0.3f;

  Transport::Simulate(deltaT, prec);  

  const TransportType *type = Type();
  static const float terrainCoef = 6.0f;

  Vector3 speed=FutureVisualState().ModelSpeed();
  float speedSize=fabs(speed.Z());

  if (FutureVisualState()._speed.SquareSize() > 0.1f || _angVelocity.SquareSize() > 0.1f)
    OnMoved();

  // handle impulse
  float impulse2=_impulseForce.SquareSize() + _impulseTorque.SquareSize();
  if (impulse2>/*Square(GetMass()*0.01f)*/ 0.0f)
  {
    OnMoved();
    DamageOnImpulse(this,_impulseForce.Size(), VZero);
  }

  if (GetStopped())
  {
    // reset impulse - avoid cumulation
    _impulseForce = VZero;
    _impulseTorque = VZero;
    FutureVisualState()._modelSpeed = VZero;
    FutureVisualState()._speed = VZero;
    _angVelocity = VZero;
    _angMomentum = VZero;
  }

  //***************************
  // Section 2. Physical simulation
  //***************************

  // calculate all forces, frictions and torques
  Vector3 force(VZero),friction(VZero);
  Vector3 torque(VZero),torqueFriction(VZero);

  Vector3 pForce(VZero); // partial force
  Vector3 pCenter(VZero); // partial force application point

  float newPotencialEnergy = 0.0f;
  bool validPotencialEnergy = (_impulseForce == VZero && _impulseTorque == VZero);

  if (!_isStopped && !CheckPredictionFrozen())
  {
    //***************************
    // Section 2.1 Physical simulation - calculate new position prediction according to velocities
    //***************************
    Ref<EntityVisualState> moveTrans = CloneFutureVisualState();  // we don't know the type of the wreck - can't use PredictPos
    moveTrans->SetTransform(ApplySpeed(deltaT));
    ApplyRemoteState(deltaT,*moveTrans);

    //***************************
    // Section 2.2 Physical simulation - check if in new position are deep contacts
    //***************************

    const float MAX_IN  = 0.2f;
    const float ABOVE_ROAD = 0.05f;

    GroundCollisionBuffer groundCollBuffer;
    CollisionBuffer objCollBuffer;

    bool doObjectColl = prec<=SimulateVisibleFar && IsLocal();


    // Objects are setposed. If car "eat" the pillar it starts shaking because of setposing.
#define SET_POS_OBJECT 1

    float softFactor=floatMin(4000/GetMass()/2 * speedSize/4,1.0f);
    //float softFactor=0;

    PROFILE_SCOPE(carCD);
    GLOB_LAND->GroundCollision(groundCollBuffer,this,*moveTrans,ABOVE_ROAD,softFactor);
#if SET_POS_OBJECT
    if (doObjectColl)
      GLOB_LAND->ObjectCollision(objCollBuffer,this,*moveTrans);
#endif

    Vector3 averageRepairMove = VZero;
    unsigned int numberOfDeepColl = 0;

    // Find all deep contacts and calculate average repair move.
    for(int i = 0; i < groundCollBuffer.Size(); i++)
    {
      if (MAX_IN < groundCollBuffer[i].under && groundCollBuffer[i].type != GroundWater)
      {
        Vector3 dirOut=Vector3(0,groundCollBuffer[i].dZ,1).CrossProduct(Vector3(1,groundCollBuffer[i].dX,0)).Normalized();
        averageRepairMove += (groundCollBuffer[i].under - MAX_IN) * dirOut[1] * dirOut;
        numberOfDeepColl++;                
      }
    }
#if SET_POS_OBJECT

#define MOVE_OBJECT_LESS 1.0f  // collisions with objects are less important or big repairs does not looks good.
    if (doObjectColl)
    {       
      for(int i = 0; i < objCollBuffer.Size();)
      {
        Object *obj=objCollBuffer[i].object;
        obj->SimulationNeeded(GetVisualStateAge(FutureVisualState()));
        if (dyn_cast<Person,Object>(obj)) 
        {
          i++;
          continue;
        }

        unsigned int imax = i;
        float underMax = objCollBuffer[i].under;

        i++;
        while(i < objCollBuffer.Size() && obj == objCollBuffer[i].object)
        {
          if (objCollBuffer[i].under > underMax)
          {
            imax = i;
            underMax = objCollBuffer[i].under;
          }
          i++;
        }

        if (MAX_IN < objCollBuffer[imax].under && !obj->IsPassable())
        {                    
          averageRepairMove += (objCollBuffer[imax].under - MAX_IN) * objCollBuffer[imax].dirOut * MOVE_OBJECT_LESS;
          numberOfDeepColl++;                
        }
      }
    }
#endif        

    if (numberOfDeepColl > 0)
    {
      // Apply repair move to avoid deep contacts.
      averageRepairMove /= numberOfDeepColl;

      Matrix4 transform=moveTrans->Transform();
      Point3 newPos=transform.Position();            
      newPos+=averageRepairMove;
      transform.SetPosition(newPos);
      moveTrans->SetTransform(transform);

      groundCollBuffer.Clear();
      objCollBuffer.Clear();

      GLOB_LAND->GroundCollision(groundCollBuffer,this,*moveTrans,ABOVE_ROAD,softFactor);
#if SET_POS_OBJECT
      if (doObjectColl)
        GLOB_LAND->ObjectCollision(objCollBuffer,this,*moveTrans);

        ///             if (objCollBuffer.Size() > 0)
        //                validPotencialEnergy = false;
#endif
      }

#if !SET_POS_OBJECT
    if (doObjectColl)
      GLOB_LAND->ObjectCollision(objCollBuffer,this,*moveTrans);
#endif

    //***************************
    // Section 2.3 Physical simulation - turning + trust
    //***************************

    Vector3 wCenter(VFastTransform,FutureVisualState().ModelToWorld(),GetCenterOfMass());

    // convert forces to world coordinates
    FutureVisualState().DirectionModelToWorld(torque,torque);
    FutureVisualState().DirectionModelToWorld(force,force);

    //***************************
    // Section 2.4 Physical simulation - gravitation
    //***************************
    // apply gravity
    pForce=Vector3(0,-G_CONST*GetMass(),0);
    force+=pForce;

#if ARROWS
    AddForce(wCenter, pForce*InvMass());
#endif

    //***************************
    // Section 2.5 Physical simulation - friction angular + air friction.
    //***************************

    // angular velocity causes also some angular friction, this should be simulated as torque
    torqueFriction = _angMomentum*0.5f;
    // body air friction
    FutureVisualState().DirectionModelToWorld(friction, WreckFriction(speed));
#if ARROWS
    AddForce(wCenter,-friction*InvMass());
#endif

    //***************************
    // Section 2.6 Physical simulation - collision resolving
    //***************************

    ApplyImpulses(); // Apply all collected impulses

    wCenter.SetFastTransform(moveTrans->ModelToWorld(),GetCenterOfMass());

    float maxImpactSpeed = 0.0f;
    // how much in the collision is the max. under detected vertex
    // this can be used to rewind the time back to make sure high speed collisions are handled better
    float maxImpactUnder = 0.0f;
    // surface normal direction corresponding to maxImpactSpeed
    Vector3 maxImpactDirOut(VZero);
    Vector3 maxImpactSpeedLocal(VZero);

    float soft = 0.0f;
    float dust = 0.0f;
    if (deltaT>0)
    {
      // check collision on new position
      //float crash=0;
      //float sFactor=type->GetMaxSpeedMs()*1.3;
      Vector3 fSpeed=speed/*-Vector3(0,0,_thrust*sFactor)*/;
      // avoid too fast accel/deccel
      float maxAcc = floatMin(5,type->GetMaxSpeedMs()*0.14);
      saturate(fSpeed[0],-maxAcc,+maxAcc);
      saturate(fSpeed[1],-maxAcc,+maxAcc);
#if _VBS3 //increase brake
      saturate(fSpeed[2],-maxAcc,+maxAcc);
#else
      saturate(fSpeed[2],-maxAcc*0.6,+maxAcc);
#endif

      float brakeFriction = 1;

      Vector3 objForce(VZero); // total object force
      Vector3 landForce(VZero); // total land force

      Vector3 objTorque(VZero); // total torque made by object forces.

      _objectContact=false;

      // For crash sound.
      float maxImpactSpeedWater = 0.0f;
      float appliedImpulseObj = 0.0f;
      float maxImpactSpeedLand = 0.0f;
      float maxUnderWater = 0.0f;

      bool onlyStaticObjectContact = true;
      //float maxCFactor = 0;

      // check if we would rather go front or back
      float frontBackPreference = 0.0f;
      float leftRightPreference = 0.0f;
      if (doObjectColl)
      {
        //***************************
        // Section 2.6.1 Physical simulation - collision resolving with objects
        //***************************

#define MAX_IN_FORCE 0.1        

        int i=0;
        bool takeStatic = false;  // first resolve non-static objects and then static ones 
        // it tries to avoid penetration with static objects                                   

        while (i < objCollBuffer.Size())
        {
          //CollisionInfo &info=objCollBuffer[i];
          Object *obj=(objCollBuffer[i].hierLevel == 0) ? objCollBuffer[i].object : objCollBuffer[i].parentObject;

          if( !obj || obj->IsPassable() || (!takeStatic && obj->Static()) ||
            (takeStatic && !obj->Static()))
          {
            i++;
            if (!takeStatic && i == objCollBuffer.Size())
            {
              takeStatic = true;
              i = 0;
            }
            continue;
          }

          _objectContact=true;

          // Find all contacts with the object
          int fromIndx = i;
          int toIndx = i + 1;                    
          while( toIndx < objCollBuffer.Size() && obj == ((objCollBuffer[toIndx].hierLevel == 0) ? objCollBuffer[toIndx].object : objCollBuffer[toIndx].parentObject))
            toIndx++;

          EntityAI * bodyTmp = dyn_cast<EntityAI,Object>(obj); 
          if (bodyTmp && bodyTmp->GetStopped())
            bodyTmp->OnMoved();

          bool calculatedImpulses = false;

          DestructType destType = obj->GetDestructType();
          bool staticObj = obj->Static();

          onlyStaticObjectContact = onlyStaticObjectContact && staticObj;

          if (!staticObj)
            validPotencialEnergy = false;

          bool isPerson = dyn_cast<Person, Object>(obj) != NULL;
          if (isPerson)
          {
            // following line was causing ICE with X360 SDK 5632
            // TODOX360: fix instead of removing
            // Persons are not physical objects, that is why they must be handled in different way..
            ObjectCollisionWithPerson(objCollBuffer, fromIndx, toIndx, appliedImpulseObj);
            calculatedImpulses = true;
          }
          else
          {                                                
            if ((!staticObj /*&& destType != DestructMan*/) || (staticObj && !(destType == DestructTree|| destType == DestructTent || destType == DestructMan)))
            {
              // for such objects run marina simulation.
              // fixed static object
              CarCollisionsContactResolver resolver;
              resolver.Init(deltaT, this, objCollBuffer, fromIndx, toIndx);
              resolver.Resolve();

              calculatedImpulses = true;

              appliedImpulseObj += resolver.GetAppliedImpulse();

              Vector3 contactPos = objCollBuffer.GetContactPos(fromIndx,toIndx);
              Vector3 contactRel = PositionWorldToModel(contactPos);

              frontBackPreference -= contactRel.Z();
              leftRightPreference -= contactRel.X();

            }
            else
            {
              if (staticObj && (destType == DestructTree|| destType == DestructTent || destType == DestructMan))
              {       
                // The object can be destroyed. Apply just impulse till value that destroys it,.
                float maxAllowedImpulse = 1.0f/ (obj->GetInvArmor()* obj->GetInvMass() * 5.0f);

                CarCollisionsResolverMaxImpulse resolver;
                resolver.Init(deltaT,this, objCollBuffer, fromIndx, toIndx, maxAllowedImpulse);                                
                resolver.Resolve();

                if (maxAllowedImpulse <= resolver.GetAppliedImpulse())
                {
                  // the impulse was limited - this means the object is not strong enough to stop it
                  // crush it down
                  DoDamageResult result;
                  result.damage = 1;
                  obj->ApplyDoDamage(result, this, RString(), SkinnedPosition());
                }
                else
                {
                  Vector3 contactPos = objCollBuffer.GetContactPos(fromIndx,toIndx);
                  Vector3 contactRel = PositionWorldToModel(contactPos);

                  frontBackPreference -= contactRel.Z();
                  leftRightPreference -= contactRel.X();
                }

                calculatedImpulses = true;
                appliedImpulseObj += resolver.GetAppliedImpulse();
              }             
            }                 
          }

          for(;i < toIndx; i++)
          {
            CollisionInfo &info=objCollBuffer[i];
            // info.pos is relative to object

            float penFactor ; // factor for penetration forces
            if (staticObj)
              penFactor= GetMass();//floatMin(info.object->GetMass(), GetMass());                                                                        
            else
            {
              if (isPerson)
              { // For man lower penFactor, otherwise people would pushing cars without problem
                penFactor = obj->GetMass() / 10.0f;
              }
              else
              {                            
                penFactor = (GetMass() + obj->GetMass()) / 2;
                saturateMax(penFactor, GetMass() / 2);
                saturateMin(penFactor, GetMass() * 2);
              }
            }

            /// Penetration force in contact
            Vector3 pos=info.pos;
            Vector3 dirOut=info.dirOut;
            // create a force pushing "out" of the collision
            float forceIn=floatMin(info.under,MAX_IN);
            //float forceIn = info.under;
#define MAX_SPRING_POINTS 10
#define MAX_OUT_ACCEL 5.0f 
            penFactor *= MAX_OUT_ACCEL / MAX_IN;

            if (objCollBuffer.Size() > MAX_SPRING_POINTS)
            {
              penFactor *= MAX_SPRING_POINTS * 1.0f/objCollBuffer.Size();
            }                                                

            Vector3 pForce=dirOut*penFactor*forceIn;

            newPotencialEnergy += 0.5 * penFactor * forceIn * forceIn;

            Vector3 pTorque = pForce;

            pCenter=pos-wCenter;                        
            objForce+=pForce;
            objTorque+=pCenter.CrossProduct(pTorque);      

#if ARROWS
            AddForce(pos,pForce*InvMass(),Color(0,1,0));
#endif
            // Friction
            Vector3 speedLocal = SpeedAtPoint(pos);

            if (!obj->Static())
            {
              Entity *pcBody2 = dyn_cast<Entity,Object>(obj);
              if (pcBody2)
              {
                speedLocal -= pcBody2->SpeedAtPoint(pos);
              }
            }

            Vector3 speedFriction = speedLocal - dirOut * (dirOut * speedLocal);

            float speedFrictionSize = speedFriction.Size();
            if (speedFrictionSize > 0.01f)
            {
              //speedFriction.Normalize();
              //if (speedFrictionSize < 1.0f)
              //    speedFrictionSize *= 10.0f;
              //else
              //    saturateMax(speedFrictionSize, 10.0f);                            

#define FRICTION_COEF 0.4f
              pForce = speedFriction / speedFrictionSize * penFactor*forceIn* FRICTION_COEF;

              friction += pForce;
              torqueFriction +=pCenter.CrossProduct(pForce);                             
#if ARROWS
              AddForce(pos, pForce*InvMass(), Color(1,1,0));
#endif
            }


            //damping
            float speedInDirection = (dirOut * speedLocal);
            if (!isPerson)
            {
              // damping is really huge and can stop car... 

              pForce = 15 * dirOut * speedInDirection * GetMass() / objCollBuffer.Size();

              friction += pForce;
              torqueFriction +=pCenter.CrossProduct(pForce);
            }
#if ARROWS
            AddForce(pos, pForce*InvMass(), Color(1,0,1));
#endif
            // DoDamage
            if (!calculatedImpulses && speedInDirection > 0 /*&& obj->GetDestructType() != DestructMan*/&& IsLocal())
            {                                                        
              float damage = speedInDirection * obj->GetMass() * GetInvMass() * GetInvArmor() * 0.02f / (toIndx - fromIndx);
              if( damage>0.1 )
              {
                LocalDamageMyself(VZero,damage,GetRadius());
              }

              appliedImpulseObj += speedInDirection * GetMass() / 10.0f;
            }
          }
          if (!takeStatic && i == objCollBuffer.Size())
          {
            takeStatic = true;
            i = 0;
          }
        }
      } // if( object collisions enabled )


      // if there was some contact, we would like to know what can we do to avoid it
      /*
      if (frontBackPreference!=0)
      {
      LogF(
      "%s: front/back %g, left/right %g",
      cc_cast(GetDebugName()),frontBackPreference,leftRightPreference
      );
      }
      // frontBackPreference
      */


      //***************************
      // Section 2.6.2 Physical simulation - collision resolving with ground
      //***************************                   

      // check for collisions
      _landContact=false;     
      _waterContact=false;

      if( groundCollBuffer.Size()>0 )
      {       
#define MAX_UNDER_FORCE 0.1f
        const Shape *landcontact = GetShape()->LandContactLevel();
        int numberOfContactPoints = landcontact ? landcontact->NPos() : 8;
        // if there are more collision points than possible contact points,
        // number of contact points must be low
        // this may be due to object geometry used instead of landcontact
        //saturateMax(numberOfContactPoints,groundCollBuffer.Size());
        float contactCoef = 8.0f/numberOfContactPoints;

        Vector3 damperDir = FutureVisualState().DirectionUp();
        Vector3 forwardDir = FutureVisualState().Direction();

        SetSurfaceInfo(groundCollBuffer);

        // information for the detected collision
        //LogF("nCollision %d",gCollision.Size());
        for( int i=0; i<groundCollBuffer.Size(); i++ )
        {
          // info.pos is world space
          UndergroundInfo &info=groundCollBuffer[i];
          // we consider two forces
          if( info.under<0 )
          {
            /*
            LogF("Negative under %.3f",info.under);
            LogF("  tex %s",info.texture ? info.texture->Name() :"<null>");
            LogF("  obj %s",info.obj ? (const char *)info.obj->GetDebugName() : "<null>");
            */
            continue;
          }

          float under;

          if (info.type == GroundWater)
          {
            _waterContact=true;

            // simulate swimming force
            //const float coefNPoints=12.0/12.0;
#if _VBS3
            const float coefNPoints=16.0/numberOfContactPoints;
#else
            const float coefNPoints=(16.0/1.5)/numberOfContactPoints;
#endif
            // first is water is "pushing" everything up - causing some momentum
            saturateMax(maxUnderWater,info.under);

            pForce=Vector3(0,GetMass()*0.5f*info.under*coefNPoints,0);
            pForce*=0.1; // cannot float
            pCenter=info.pos-wCenter;
            torque+=pCenter.CrossProduct(pForce);
            landForce+=pForce;

            // add stabilizing torque
            // stabilized means DirectionUp() is (0,1,0)
            Vector3 stabilize=VUp-moveTrans->DirectionUp();
            torque+=Vector3(0,coefNPoints*1.5f*GetMass(),0).CrossProduct(stabilize);

#if ARROWS
            AddForce(wCenter+pCenter,pForce*InvMass());
#endif

            // second is "water friction" - causing no momentum
            pForce[0]=speed[0]*fabs(speed[0])*15;
            pForce[1]=speed[1]*fabs(speed[1])*15+speed[1]*160;
            pForce[2]=speed[2]*fabs(speed[2])*6;

            pForce=FutureVisualState().DirectionModelToWorld(pForce*info.under)*GetMass()*(coefNPoints/700);
#if ARROWS
            AddForce(wCenter+pCenter,-pForce*InvMass());
#endif
            friction+=pForce;
            torqueFriction+=_angMomentum*0.3;

            float colSpeed2 = FutureVisualState()._speed.SquareSize();
            if( colSpeed2>Square(8) )
            {             
              saturateMax(maxImpactSpeedWater,colSpeed2);              
            }
          }
          else
          {
            // Collision with solid ground

            _landContact=true;

            Vector3 contactForce = VZero;                        
            Vector3 contactFriction = VZero;   

            // some friction is caused by moving the land aside
            // this applies only to soft surfaces
            if( info.texture )
            {
              soft=info.texture->Roughness()*2.5f;
              dust=info.texture->Dustness()*2.5f;

              saturateMin(dust,1);
            }


            Vector3 dirOut=Vector3(0,info.dZ,1).CrossProduct(Vector3(1,info.dX,0)).Normalized();
            Vector3 speedLocal = SpeedAtPoint(info.pos);

            // If the damper dir is nearly the same as dirOut
            // It is simple and more stable if it is really in dirOut direction.
            Vector3 localDamperDir = (damperDir * dirOut > 0.9f) ? dirOut : damperDir;


            // braking friction for this point
            float brakeFrictionWheel = brakeFriction;

            bool foundWheel = false;
            under = (info.under - ABOVE_ROAD) * dirOut[1] ; // under is in VUp direction

            float speedToGround = -dirOut * speedLocal;
            if (under > 0)
            {
              contactForce += dirOut * GetMass()*40.0f*contactCoef*under;
              contactFriction -= dirOut * GetMass()*contactCoef * 
                (speedToGround * 0.2f + fSign(speedToGround) * 0.25f);

              newPotencialEnergy += 
                0.5 * GetMass()*40.0f*contactCoef*under*under;
            }

            // points other than wheels brake almost as much as wheel brake would
            saturateMax(brakeFrictionWheel,0.8f);
            // contact with something else but the wheel
            // depending on the speed we may want to damage the vehicle
            // we may also want to simulate some kind of impact / absorb some part of the inertia

            if (speedToGround>0)
            {
              if (speedToGround>maxImpactSpeed)
              {
                maxImpactSpeed = speedToGround;
                maxImpactUnder = info.under;
                maxImpactDirOut = dirOut;
                maxImpactSpeedLocal = speedLocal;
              }
              saturateMax(maxImpactSpeedLand,speedToGround);
            }

            pCenter=info.pos-wCenter;
            torque+=pCenter.CrossProduct(contactForce);
            torqueFriction+=pCenter.CrossProduct(contactFriction);           

#if ARROWS
            AddForce(wCenter+pCenter, contactForce*InvMass(), Color(1,0,0));

            AddForce(wCenter+pCenter + Vector3(0,2,0), contactForce, Color(0,0,1));

            AddForce(wCenter+pCenter, -contactFriction*InvMass(), Color(0,1,1));
#endif
            // Friction                                             

            Vector3 contactFriction2 = VZero;

            Vector3 forwardDirLocal = forwardDir - dirOut * (forwardDir * dirOut);
            if (forwardDirLocal.SquareSize() < 0.1f)
            {
              // Forward dir is nearly parallel with dirOut
              // choose arbitrary perpendicular vector. 
              forwardDirLocal = localDamperDir - dirOut * (localDamperDir * dirOut);
              Assert(forwardDirLocal.SquareSize() >= 0.1f);
            }

            forwardDirLocal.Normalize();
            Vector3 sideDirLocal = forwardDirLocal.CrossProduct(dirOut);

            DirectionWorldToModel(forwardDirLocal, forwardDirLocal);
            DirectionWorldToModel(sideDirLocal, sideDirLocal);


            // Friction in point
            // second is "land friction" - causing little momentum                            

            float forwardSpeed1 = fSpeed * forwardDirLocal;
            float sideSpeed1 = fSpeed * sideDirLocal;                                       

            pForce = forwardDirLocal * (forwardSpeed1*30+fSign(forwardSpeed1)*(3000 + brakeFrictionWheel * 40000));

            if (speed[2] > 1 && foundWheel)
              torque += 0.3 * FutureVisualState().DirectionModelToWorld(Vector3(pForce[2],0,0))*(GetMass()*(1.0/40000)*contactCoef);

            pForce += sideDirLocal * (sideSpeed1*500+fSign(sideSpeed1)*90000);
            pForce=FutureVisualState().DirectionModelToWorld(pForce)*(GetMass()*(1.0/40000)*contactCoef);

            contactFriction2+=LimitFriction(pForce,speed,deltaT,InvMass());
            torqueFriction+=_angMomentum*0.5f*contactCoef;

            // Friction according to land roughness
            float landMoved=0.02;
            //            saturateMin(landMoved,0.1);

            float forwardSpeed2 = speed * forwardDirLocal;
            float sideSpeed2 = speed * sideDirLocal;          

            pForce = forwardDirLocal * (forwardSpeed2 * 4500);

            if (speed[2] > 1 && foundWheel)
              torque += 0.5 * FutureVisualState().DirectionModelToWorld(Vector3(pForce[2],0,0))*(GetMass()*(1.0/1000)*contactCoef*landMoved*soft*terrainCoef);

            pForce += sideDirLocal * (sideSpeed2 * 2000);

            pForce=FutureVisualState().DirectionModelToWorld(pForce)*( GetMass()*(1.0/1000)*contactCoef*landMoved*soft*terrainCoef);

            contactFriction2+=LimitFriction(pForce,speed,deltaT,InvMass());

#if ARROWS
            AddForce(wCenter+pCenter, -contactFriction2*InvMass(), Color(1,1,1));  
#endif              
            contactFriction += contactFriction2;             

            landForce += contactForce;                        
            friction += contactFriction;

            pCenter=info.pos-wCenter;            
          }
        }

#if 0 //_ENABLE_CHEATS
        GlobalShowMessage(100,"Under %.2f, brake %.2f, force %.1f, fric %.1f",totalUnder,brakeFriction,DirectionWorldToModel(force).Z(),DirectionWorldToModel(friction).Z());
#endif

        if (_waterContact)
        {
          const SurfaceInfo &info = GLandscape->GetWaterSurface();
          soft = info._roughness * 2.5f;
          dust = info._dustness * 2.5f;

          saturateMin(dust , 1);
        }
      }                        

      force += objForce;
      torque += objTorque;

      force += landForce;

      //***************************
      // Section 2.6.4 Physical simulation - Crash
      //*************************** 

      if (IsLocal())
      {

        const float maxAllowedImpactSpeed = 10;
        if (maxImpactSpeed > maxAllowedImpactSpeed && _maxContactSpeed<maxAllowedImpactSpeed)
        {
#if ENABLE_REPORT
          if (this==GWorld->CameraOn())
            LogF("Impact maxImpactSpeed=%g, under=%g",maxImpactSpeed,maxImpactUnder);
#endif
          // reduce the object impact speed
          // maxImpactSpeed was computed as -maxImpactDirOut*maxImpactSpeedLocal;
          // target is maxAllowedImpactSpeed
          // speed is in, and quite large - we know the angle is < 90 deg
          // reduce overall speed to avoid excessive jumping back
          // TODO: some part may be caused by the rotation
          // we may need to reduce the rotation as well

          float speedImpact = -maxImpactDirOut*FutureVisualState().Speed();
          if (speedImpact>maxAllowedImpactSpeed)
          {
            Vector3 newSpeed = FutureVisualState().Speed()+maxImpactDirOut*(speedImpact-maxAllowedImpactSpeed);

#if ENABLE_REPORT
            if (this==GWorld->CameraOn())
            {
              LogF(
                "Reduced speed from %g to %g (%g,%g,%g to (%g,%g,%g)",
                speedImpact,maxAllowedImpactSpeed,
                FutureVisualState().Speed()[0],FutureVisualState().Speed()[1],FutureVisualState().Speed()[2],
                newSpeed[0],newSpeed[1],newSpeed[2]
              );
              FutureVisualState()._speed = newSpeed;
            }
#endif
          }
          //

        }
        // remember the information from previous frame
        _maxContactSpeed = maxImpactSpeed;

        // starting time when no crash are evaluate
        if (Glob.time>_disableDamageUntil && _doCrash == CrashNone)
        {
          // Choose the biggest crash for sound
          float crashLand = fabsf(maxImpactSpeedLand);
          float crashObj = appliedImpulseObj * GetInvMass();
          float crashWater = fabsf(maxImpactSpeedWater);
          float crash = 0;

          if (crashLand > crashObj)
          {
            if (crashLand > crashWater)
            {
              _doCrash = CrashLand;
              crash = crashLand;
            }
            else
            {
              _doCrash = CrashWater;
              crash = crashWater;
            }                        
          }
          else
          {
            if (crashObj > crashWater)
            {
              _doCrash = CrashObject;
              crash = crashObj;
            }
            else
            {
              _doCrash = CrashWater;
              crash = crashWater;
            }
          }

          // specify CrashObject
          if (_doCrash == CrashObject)
          {
            // check collision buffer
            if (objCollBuffer.Size() > 0)
            {
              // CrashObject distinguish types
              int destTree = 0;
              int destBuild = 0;
              int destEng = 0;

              for (int i = 0; i < objCollBuffer.Size(); i++)
              {
                CollisionInfo &cInfo = objCollBuffer[i];

                if (cInfo.object)
                {
                  // distinguish crash object on the basis of destruction types
                  DestructType dt = cInfo.object->GetDestructType();

                  if (dt == DestructTree)
                    destTree++;
                  else if (dt == DestructBuilding)
                    destBuild++;
                  else if (dt == DestructEngine || dt == DestructWreck)
                    destEng++;
                }
              }

              // no suitable category found
              if (destEng == 0 && destTree == 0 && destBuild == 0)
              {
                _doCrash = CrashNone;
              }
              else
              {
                // select the most principal object
                if (destTree > destBuild)
                {
                  if (destTree > destEng) 
                    _doCrash = CrashTree;
                  else
                    _doCrash = CrashArmor;
                }
                else
                {
                  if (destBuild > destEng)
                    _doCrash = CrashBuilding;
                  else
                    _doCrash = CrashArmor;
                }
              }
            }
          }

          _crashVolume = floatMinMax(0, 1, crash * 0.1);

          if (crash < 0.01)
          {
            _doCrash = CrashNone;
            _crashVolume = 0;
          }
        }
      }
    }

    //***************************
    // Section 3. Dust, head movement ... 
    //***************************        
#if 0
    if (this==GWorld->CameraOn())
    {
      float ratio = _angMomentum.Size()>0 ? torqueFriction.Size()/_angMomentum.Size() : 0;
      GlobalShowMessage(200, "ratio %7.2f, torqueF %9.2f, angMom %9.2f angVel %7.2f", ratio, torqueFriction.Size(), _angMomentum.Size(), _angVelocity.Size());
    }
#endif    

    StabilizeTurrets(FutureVisualState().Transform().Orientation(), moveTrans->Orientation());

    //***************************
    // Section 2.7 Move to new position and re-normalize energy
    //***************************
    Vector3 oldPosition = FutureVisualState().Position();
    Move(*moveTrans);

    // Energy renormalization
    float newKinetic=GetMass()*FutureVisualState()._speed.SquareSize()*0.5f; // E=0.5*m*v^2
    newKinetic += _angMomentum * _angVelocity * 0.5f;
    float newDiffPotencial = (FutureVisualState().Position()[1] - oldPosition[1]) *GetMass() * G_CONST;                

    if (_validEnergy && validPotencialEnergy && (newDiffPotencial + newKinetic + newPotencialEnergy) > _oldEnergy /*+ GetMass()/10*/)
    {
      float repairedKinetic =  _oldEnergy - newDiffPotencial - newPotencialEnergy;                        

      if (repairedKinetic <= 0)
      {                
        FutureVisualState()._speed = VZero;
        _angVelocity = VZero;
        _angMomentum = VZero;
      }
      else
      {
        float renormalizationCoef = sqrt(repairedKinetic/newKinetic);                
        FutureVisualState()._speed *= renormalizationCoef;
        _angVelocity *= renormalizationCoef;
        _angMomentum *= renormalizationCoef;

      }
    }

    newKinetic = GetMass()*FutureVisualState()._speed.SquareSize()*0.5f; // E=0.5*m*v^2
    newKinetic += _angMomentum * _angVelocity * 0.5f;

    _oldEnergy = newKinetic + newPotencialEnergy;
    _validEnergy = validPotencialEnergy;        

    //***************************
    // Section 2.7 Calculate new velocities
    //***************************       

#if  ARROWS
    // Show force and torque.
    AddForce(wCenter + Vector3(0,4,0), force * InvMass(), Color(1,0.5, 0.25));
    AddForce(wCenter + Vector3(0,4,0), torque * InvMass(), Color(0.25,0.5, 1));
#endif

    ApplyForces(deltaT,force,torque,friction,torqueFriction/*, 5.5f * GetMass()*/);

    // apply static friction
    bool stopCondition = false;
    if (_landContact && !_waterContact /*&& !_objectContact*/)        
    {
      float maxSpeed = Square(0.7f);
      if (!Driver())
        maxSpeed = Square(1.2f);
      if (FutureVisualState()._speed.SquareSize()<maxSpeed && _angVelocity.SquareSize()<maxSpeed*0.3f)
        stopCondition = true;
    }
    if (stopCondition)
      StopDetected();
    else
      OnMoved();
    DirectionWorldToModel(FutureVisualState()._modelSpeed, FutureVisualState()._speed);
  }
  // Simulate items that must be simulate after transformation change
  SimulatePost(deltaT,prec);  
}

void Transport::ShowDamage(int part, EntityAI *killer)
{
  if (EngineIsOn() && !IsDamageDestroyed())
  {
    _showDmg = true;
    _showDmgValid = Glob.time + 5.0f;
  }
  base::ShowDamage(part,killer);
}

AnimationStyle Transport::IsAnimated( int level ) const {return AnimatedGeometry;}

bool Transport::PreloadMuzzleFlash(const WeaponsState &weapons, const WeaponModePtr &ptr) const
{
  if (!ptr.IsValid()) return true;
  const AmmoType *ammo = ptr.GetAmmoType();
  if (!ammo) return true;

  float z2 = GScene->GetCamera()->Position().Distance2(FutureVisualState().Position());

  if (EnableVisualEffects(SimulateVisibleNear))
  {
    switch (ammo->_simulation)
    {
      case AmmoShotBullet:
      case AmmoShotSpread:
        // preload muzzle flash texture - level not known
        // TODO: correct WeaponsType
        return Type()->_weapons._animFire.PreloadTexture(_shape, z2);
    }
  }
  return true;
}


void Transport::Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2)
{
  if (GScene->MainLight()->NightEffect() > 0.01 && EngineIsOn())
    GetType()->_dashboard.Unhide(animContext, _shape, level);
  else
    GetType()->_dashboard.Hide(animContext, _shape, level);

  //float dt = _showDmgValid - Glob.time;
  bool on = (toInt(2.0 * (_showDmgValid - Glob.time)) % 2) != 0;
  if (_showDmg && on)
    GetType()->_showDmg.Unhide(animContext, _shape, level);
  else
    GetType()->_showDmg.Hide(animContext, _shape, level);

  // animate turrets
  for (int i=0; i<_turrets.Size(); i++)
  {
    TurretType *turretType = Type()->_turrets[i];
    Turret *turret = _turrets[i];
    turret->Animate(*turretType, animContext, _shape, level, setEngineStuff, parentObject);
  }

  base::Animate(animContext, level, setEngineStuff, parentObject, dist2);
}

void Transport::AnimateManProxyMatrix(ObjectVisualState const& vs, int level, const ManProxy &proxy, Matrix4 &proxyTransform) const
{
  AnimateBoneMatrix(proxyTransform,vs,level,proxy._proxy->boneIndex);
  // default: do not know how to animate man proxy
}

void Transport::Deanimate(int level, bool setEngineStuff)
{
  // animate turrets
  for (int i=0; i<_turrets.Size(); i++)
  {
    TurretType *turretType = Type()->_turrets[i];
    Turret *turret = _turrets[i];
    turret->Deanimate(*turretType, _shape, level, setEngineStuff);
  }
  base::Deanimate(level,setEngineStuff);
}


LODShape *Transport::GetOpticsModel(const Person *person) const
{
  if (!person) return NULL;
  // if person can be hidden and is not, it means he cannot use optics

  VisualState& vs = unconst_cast(RenderVisualState());

  if (person == _driver)
  {
    if (Type()->_hideProxyInCombat && vs._driverHidden<0.5) return NULL;
    return Type()->_driverOpticsModel;
  }
  else
  {
    TurretContextV context;
#if _VBS3
    if (FindOpticsTurret(vs, person, context) && context._turret)
#else
    if (FindTurret(vs, person, context) && context._turret)
#endif
    {
      if (Type()->_hideProxyInCombat && context._turretVisualState->GetGunnerHidden() < 0.5)
        return context._turretType->_viewGunner[context._turret->_currentViewGunnerMode]._gunnerOpticsModel;
      else
        return context._turretType->_viewOptics[context._turret->_currentViewOpticsMode]._gunnerOpticsModel;
    }
  }
  return NULL;
}


bool Transport::GetForceOptics(const Person *person, CameraType camType) const
{
  if (camType != CamInternal) return false;

  if (person == _driver) return Type()->_driverForceOptics;

  TransportVisualState const& vs = RenderVisualState();
  TurretContextV context;
  if (FindTurret(unconst_cast(vs), person, context) && context._turretType)
  {
    if (Type()->_hideProxyInCombat && context._turretVisualState->GetGunnerHidden() < 0.5)
      return context._turretType->_gunnerOutForceOptics;
    else
      return context._turretType->_gunnerForceOptics;
  }
  return false;
}

bool Transport::PreloadView(Person *person, CameraType camType) const
{
  bool ret = base::PreloadView(person,camType);
  return ret;
}

PackedColor Transport::GetOpticsColor(const Person *person)
{
  if (!person) return PackedBlack;

  if (person == _driver) return Type()->_driverOpticsColor;
  else
  {
    TransportVisualState const& vs = RenderVisualState();
    TurretContextV context;
    if (FindTurret(unconst_cast(vs), person, context) && context._turretType)
    {
      if (Type()->_hideProxyInCombat && context._turretVisualState->GetGunnerHidden() < 0.5)
        return context._turretType->_gunnerOutOpticsColor;
      else
        return context._turretType->_gunnerOpticsColor;
    }
  }
  return PackedBlack;
}

bool Transport::ShowAim(const TurretContextV &context, const WeaponModePtr &ptr, CameraType camType, Person *person) const
{
  if (camType == CamGunner && context._turretType)
  {
    if (Type()->_hideProxyInCombat && context._turretVisualState->GetGunnerHidden() < 0.5)
    {
      if (!context._turretType->_gunnerOutOpticsShowCursor) return false;
    }
    else
    {
      if (!context._turretType->_gunnerOpticsShowCursor) return false;
    }
  }

  // if player is gunner, he needs to see where the weapon is aiming to
  // if player is commander, he also sometimes need to see aiming point
  // this is esp. important for heli. unguided rockers
  AIBrain *gunUnit = EffectiveGunnerUnit(context);
  AIBrain *comUnit = EffectiveObserverUnit();
  if (
    comUnit && person==comUnit->GetPerson() ||
    gunUnit && person==gunUnit->GetPerson()
    )
  {
    return base::ShowAim(context, ptr, camType, person);
  }
  return false;
}

bool Transport::ShowCursor(const TurretContextV &context, const WeaponModePtr &ptr, CameraType camType, Person *person) const
{
  if (camType == CamGunner && context._turretType)
  {
    if (Type()->_hideProxyInCombat && context._turretVisualState->GetGunnerHidden() < 0.5)
    {
      if (!context._turretType->_gunnerOutOpticsShowCursor) return false;
    }
    else
    {
      if (!context._turretType->_gunnerOpticsShowCursor) return false;
    }
  }

  AIBrain *driver = PilotUnit();
  if (driver && person == driver->GetPerson())
  {
    // commander or effective observer need a cursor for commanding
    if (
      EffectiveObserverUnit()->GetPerson() && person != EffectiveObserverUnit()->GetPerson() &&
      CommanderUnit()->GetPerson() && person != CommanderUnit()->GetPerson()
    ) return false;
  }

  // if gunner can hide but is not hidden and may not fire, do not show cursor
  if (context._turret && !context._turretVisualState->IsGunnerHidden() && Type()->GetHideProxyInCombat() && !context._turretType->OutGunnerMayFire(context._gunner)) return false;
  if (driver && driver->LookAroundEnabled() && camType != CamGunner) return false;
  return base::ShowCursor(context, ptr, camType, person);
}

/// Functor returning the texture of flag carried by the gunner
class GunnerFlagTexture : public ITurretFunc
{
protected:
  Texture *&_texture;

public:
  GunnerFlagTexture(Texture *&texture) : _texture(texture) {}

  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._gunner) return false; // continue
    Texture *texture = context._gunner->GetFlagTexture();
    if (!texture) return false; // continue
    _texture = texture;
    return true; // found
  }
};

Texture *Transport::GetFlagTexture()
{
  Texture *texture = NULL;
  GunnerFlagTexture func(texture);
  if (ForEachTurret(func)) return texture;
  for( int i=0; i<_manCargo.Size(); i++ )
  {
    Person *cargo = _manCargo[i];
    if (cargo && cargo->GetFlagTexture()) return cargo->GetFlagTexture();
  }
  return NULL;
}

ManProxy::ManProxy()
{
  //selection = -1;
  _proxy = NULL;
  transform = MIdentity;
}

int Transport::PassNum( int lod )
{

  if (DrawInPass3()) return 3;
  int basePass = base::PassNum(lod);
  //   // if there are any proxies, we need to make sure we are rendered after the crew
  //   // we do this by forcing in pass "Slight alpha - rough z-order preferred"
  //   if (basePass<0) basePass = 0;
  return basePass;
}

/* Shape contain only _shadowVolumeCargo for all
Shape::FindShadowVolumeCargo, Shape::FindShadowVolumeGunner & Shape::FindShadowVolumeGunner */
int Transport::GetPass3ShadowVolumeLevel(int pass, CameraType camType) const 
{ 
  const int level = -1;

  if (pass != 3) return level;

  AIBrain *unit = GWorld->FocusOn();
  if (!unit) return level;
  Person *player = unit->GetPerson();
  if (!player) return level;

  // preciseGetInOut: show external lod when get in/out animation 
  Man *man = dyn_cast<Man>(player);
  if (man && man->RenderVisualState()._gettingInOut) return level;

  VisualState& vs = unconst_cast(RenderVisualState());

  TurretContextV context;
  if (FindTurret(vs, player, context) && context._turret && context._turretType)
  {
    int index = -1;
    if (context._turretVisualState->IsGunnerHidden())
      index = context._turretType->_lodTurnedIn >=0 ? GetShape()->FindShadowVolumeCargo() : level;
    else
      index = context._turretType->_lodTurnedOut >= 0 ? GetShape()->FindShadowVolumeGunner() : level;

    if (index >= 0)
      return index;
  }

  // view for out positions
  if (Type()->_hideProxyInCombat)
  {
    if (player == Driver())
    {
      if (!vs.IsDriverHidden()) return level;
    }
    else
    {
      TurretContextV context; 
      if (FindTurret(vs, player, context) && context._turret)
      {
        if (!context._turretVisualState->IsGunnerHidden()) return level;
      }
    }
  }

  // gunner camera
  if (camType == CamGunner)
  {
    int index = GetShape()->FindShadowVolumeGunner();
    if (index >= 0) return index;
    return level;
  }

  // internal camera
  if (player == Gunner())
    return _shape->FindShadowVolumeGunner();
  else if (player == Driver())
    return _shape->FindShadowVolumePilot();
  else
    return _shape->FindShadowVolumeCargo();
}

bool Transport::CastPass3VolShadow(int level, bool zSpace, float &castShadowDiff, float &castShadowAmb) const
{
  // Check the cargo, gunner and pilot geometries
  bool isCargo = false, isGunner = false, isPilot = false;
  if (level>=_shape->FindSimplestLevel())
  {
    if (_shape->IsSpecLevel(level,VIEW_CARGO)) isCargo = true;
    else if (_shape->IsSpecLevel(level,VIEW_GUNNER)) isGunner = true;
    else if (_shape->IsSpecLevel(level,VIEW_PILOT)) isPilot = true;
  }

  // Determine shadow casting
  bool castVolShadow = false;
  castShadowDiff = 1.0f;
  castShadowAmb = 1.0f;
  if (zSpace)
  {
    if (isCargo)
    {
      castVolShadow = Type()->_viewCargoShadow;
      castShadowDiff = Type()->_viewCargoShadowDiff;
      castShadowAmb = Type()->_viewCargoShadowAmb;
    }
    else if (isPilot)
    {
      castVolShadow = Type()->_viewDriverShadow;
      castShadowDiff = Type()->_viewDriverShadowDiff;
      castShadowAmb = Type()->_viewDriverShadowAmb;
    }
    else if (isGunner)
    {
      TurretContext context;
      if (GetPrimaryGunnerTurret(context) && context._turretType)
      {
        castVolShadow = context._turretType->_viewGunnerShadow;
        castShadowDiff = context._turretType->_viewGunnerShadowDiff;
        castShadowAmb = context._turretType->_viewGunnerShadowAmb;
      }
    }
  }
  return castVolShadow;
}


void TransportType::DrawMFDs(Transport *veh, int cb, const PositionRender &pos) const
{
  for (int i=0; i<_mfd.Size(); i++)
  {
    _mfd[i].Draw(cb,veh,pos);
  }
}

struct DrawMFDs: public Engine::MainThreadRenderTask
{
  Transport*veh;
  PositionRender pos;

  DrawMFDs(Transport*veh, const PositionRender & pos):veh(veh),pos(pos){}
  
  void operator () (Engine *engine) const
  {
    Object::ProtectedVisualState<const ObjectVisualState> vs = veh->RenderVisualStateScope();
    veh->Type()->DrawMFDs(veh,-1,pos);
  }

};

void Transport::Draw(int cb, int level, ClipFlags clipFlags, const DrawParameters &dp,
  const InstanceParameters &ip, float dist2, const PositionRender &pos, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
  , int drawListId
#endif
  )
{
  if (level == LOD_INVISIBLE) return; // invisible LOD
#if _ENABLE_CHEATS
  if (CHECK_DIAG(DETransparent))
    return;
#endif

  float castShadowDiff = Pass3IntensityMod(level,dp);

  // during the rendering we may want to modulate diffuse color
  Color instColor = GEngine->GetInstanceColor(cb);
  float instShadow = GEngine->GetInstanceShadowIntensity(cb);
  GEngine->SetInstanceInfo(cb,instColor,instShadow*castShadowDiff);
#if _HELISIM_RENDER_TO_TEXTURE
  base::Draw(cb,level, clipFlags, dp, ip, dist2, pos, oi, drawListId);
#else
  base::Draw(cb,level, clipFlags, dp, ip, dist2, pos, oi);
#endif
  GEngine->SetInstanceInfo(cb,instColor,instShadow);

  if (!dp._sbIsBeingRendered)
  {
    if (!_shape->IsNormalGraphicalLod(level)) // glass would hide the MFD/HUD anyway, no reason to draw it
    {
      Engine::MainThreadRenderTask *task = new DrawMFDs(this,pos);
      GEngine->PerformTaskOnMainThread(task);
    }
  }
  
  RString text;
#if _VBS2 //draw URN instead of squad sign
  GameVarSpace *vars = GetVars();
  if (vars)
  {
    GameValue var;
    if (vars->VarGet("URN", var))
      text = var.GetData()->GetString();
  }
  if(text.GetLength() == 0) //assign clan squadtitle only if no URN is present
#endif
  if (CommanderUnit())
    text = CommanderUnit()->GetPerson()->GetInfo()._squadTitle;

  if (text.GetLength() > 0)
    Type()->_squadTitles.Draw(cb,level,clipFlags,pos,text);
}

void Transport::DrawDiags()
{
#if _ENABLE_CHEATS
  LODShape *forceArrow=GScene->ForceArrow();
#define DRAW_OBJ(obj) GScene->DrawObject(obj)

  if (CHECK_DIAG(DEPath))
  {
    LODShape *shape = GScene->Preloaded(SphereModel);
    PackedColor colorGetIn = PackedColor(Color(0,1,0,1));
    PackedColor colorGetOut = PackedColor(Color(1,0,0,1));
    Person *player = GWorld->FocusOn() ? GWorld->FocusOn()->GetPerson() : NULL;
    if (ObserverBrain())
    {
      Vector3Val pos = GetCommanderGetOutPos(Observer())._pos;
      Ref<Object> obj = new ObjectColored(shape, VISITOR_NO_ID);
      obj->SetPosition(pos);
      obj->SetConstantColor(colorGetOut);
      DRAW_OBJ(obj);
    }
    else if (player)
    {
      Vector3Val pos = GetCommanderGetInPos(player, player->WorldPosition(player->RenderVisualState()))._pos;
      Ref<Object> obj = new ObjectColored(shape, VISITOR_NO_ID);
      obj->SetPosition(pos);
      obj->SetConstantColor(colorGetIn);
      obj->SetScale(0.2);
      DRAW_OBJ(obj);
    }
    if (DriverBrain())
    {
      Vector3Val pos = GetDriverGetOutPos(Driver())._pos;
      Ref<Object> obj = new ObjectColored(shape, VISITOR_NO_ID);
      obj->SetPosition(pos);
      obj->SetConstantColor(colorGetOut);
      DRAW_OBJ(obj);
    }
    else if (player)
    {
      Vector3Val pos = GetDriverGetInPos(player, player->WorldPosition(player->RenderVisualState()))._pos;
      Ref<Object> obj = new ObjectColored(shape, VISITOR_NO_ID);
      obj->SetPosition(pos);
      obj->SetConstantColor(colorGetIn);
      obj->SetScale(0.2);
      DRAW_OBJ(obj);
    }
    if (GunnerBrain())
    {
      Vector3Val pos = GetGunnerGetOutPos(Gunner())._pos;
      Ref<Object> obj = new ObjectColored(shape, VISITOR_NO_ID);
      obj->SetPosition(pos);
      obj->SetConstantColor(colorGetOut);
      DRAW_OBJ(obj);
    }
    else if (player)
    {
      Vector3Val pos = GetGunnerGetInPos(player, player->WorldPosition(player->RenderVisualState()))._pos;
      Ref<Object> obj = new ObjectColored(shape, VISITOR_NO_ID);
      obj->SetPosition(pos);
      obj->SetConstantColor(colorGetIn);
      obj->SetScale(0.2);
      DRAW_OBJ(obj);
    }
    bool drawn = false;
    if (!drawn && player)
    {
      Vector3Val pos = GetCargoGetInPos(0, player, player->WorldPosition(player->RenderVisualState()))._pos;
      Ref<Object> obj = new ObjectColored(shape, VISITOR_NO_ID);
      obj->SetPosition(pos);
      obj->SetConstantColor(colorGetIn);
      obj->SetScale(0.1);
      DRAW_OBJ(obj);
    }

    {
      {
        Matrix3 mat(MRotationY,-_azimutWanted);
        Ref<Object> arrow=new ObjectColored(forceArrow,VISITOR_NO_ID);

        float size=0.15;
        arrow->SetPosition(RenderVisualState().Position()+VUp*1.0);
        arrow->SetOrient(mat.Direction(),VUp);
        arrow->SetPosition(arrow->RenderVisualState().PositionModelToWorld(forceArrow->BoundingCenter()*size));
        arrow->SetScale(size);
        arrow->SetConstantColor(PackedColor(Color(1,1,0,0.5)));

        DRAW_OBJ(arrow);
      }
      {
        float dirWanted = atan2(_dirWanted.X(), _dirWanted.Z());
        Matrix3 mat(MRotationY,-dirWanted);
        Ref<Object> arrow=new ObjectColored(forceArrow,VISITOR_NO_ID);

        float size=0.1;
        arrow->SetPosition(RenderVisualState().Position()+VUp*1.5);
        arrow->SetOrient(mat.Direction(),VUp);
        arrow->SetPosition(arrow->RenderVisualState().PositionModelToWorld(forceArrow->BoundingCenter()*size));
        arrow->SetScale(size);
        arrow->SetConstantColor(PackedColor(Color(1,1,0,0.5)));

        DRAW_OBJ(arrow);
      }
    }
  }
  if (CHECK_DIAG(DEPath) && QIsManual())
  {
    {
      Ref<Object> arrow=new ObjectColored(forceArrow,VISITOR_NO_ID);

      float size=1.0f;
      arrow->SetPosition(RenderVisualState().Position()+RenderVisualState().Direction()*10+VUp);
      arrow->SetOrient(VForward,VUp);
      arrow->SetPosition(arrow->RenderVisualState().PositionModelToWorld(forceArrow->BoundingCenter()*size));
      arrow->SetScale(size);
      arrow->SetConstantColor(PackedColor(Color(0.7,1,0,0.5)));

      DRAW_OBJ(arrow);
    }
  }
#endif
  base::DrawDiags();
}

#if _VBS3
// Generic behavior of get weapon point
Vector3 Transport::GetWeaponPoint(const TurretContext &context, int weapon) const
{
  if (weapon<0 || weapon >= context._weapons->_magazineSlots.Size()) return VZero;
  const AmmoType *ammo = context._weapons->GetAmmoType(weapon);
  if (ammo)
  {
    switch (ammo->_simulation )
    {
    case AmmoShotLaser:
      if(!context._weapons->_laserTargetOn)
        return VZero;
      break;
    case AmmoShotBullet:
      if (!context._turretType)
        return VZero;
      else
        return context._turretType->GetTurretPos(GunTurretTransform(*context._turretType));
      break;
    }
  }
  return EntityAIFull::GetWeaponPoint(context,weapon);
}
#endif

void Transport::DrawCameraCockpit()
{
  AIBrain *unit = GWorld->FocusOn();
  if (!unit) return;
  Person *person = unit->GetPerson();
  if (!person) return;

  VisualState& vs = unconst_cast(RenderVisualState());

  if (GWorld->GetCameraType() == CamGunner)
  {
    LODShape *oShape = GetOpticsModel(person);
#if _VBS3
    // Draw Lod 1 as the inner optic
    if (oShape && oShape->NLevels()>1)
    {
      float zoom = 1.0f;
      TurretContext context;
      if (FindOpticsTurret(person, context) && context._turret)
      {
        const ViewPars* viewPars = context._turret->GetCurrentViewPars();
        zoom = viewPars->_initFov / GetCameraFOV();
      }
      else
      {
        if (!unit->GetVehicleIn()) //gunner not inside vehicle
        {
          float curWeapon = context._weapons->ValidatedCurrentWeapon();
          if (curWeapon >= 0)
          {
            const MagazineSlot &slot = context._weapons->_magazineSlots[curWeapon];
            const MuzzleType *muzzle = slot._muzzle;
            zoom = muzzle->_opticsZoomInit / GetCameraFOV();
          }
        }
      }
      Draw2D(NULL, oShape, 1, GetOpticsColor(person), zoom);    }
#endif
    if (oShape && oShape->NLevels()>0) Draw2D(NULL, oShape, 0, GetOpticsColor(person));
  }


  float alpha = 0;
  // decision value is not 0.5 but 0.1
  const float posFactor = 1/0.1f;
  // make sure there is time enough for preloading during the transition
  const float negFactor = 1/0.6f;
  if (person == _driver)
    alpha = floatMin(vs._driverHidden*posFactor, (1.0f - vs._driverHidden)*negFactor);
  else
  {
    TurretContextV context;
    if (FindTurret(vs, person, context) && context._turret)
    {
      float hidden = context._turretVisualState->GetGunnerHidden();
      alpha = floatMin(hidden*posFactor, (1.0f - hidden)*negFactor);
    }
  }

  if (alpha <= 0) return;

  saturateMin(alpha, 1);
  PackedColor color = PackedColor(Color(0, 0, 0, alpha));
  const float w = GEngine->WidthBB();
  const float h = GEngine->HeightBB();
  MipInfo mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  GEngine->Draw2D(mip, color, Rect2DAbs(0, 0, w, h));
}

bool Transport::PreloadCrewMember(const ManProxy &proxy, Person *man, float dist2) const
{
  // animate and draw driver
  LODShape *manShape=man->GetShape();
  int level=GScene->PreloadLevelFromDistance2(manShape,dist2,FutureVisualState().Scale());
  if (level>=0)
  {
    // shape ready - preload textures as well
    ShapeUsed lock = manShape->Level(level);
    return lock->PreloadTextures(dist2, NULL) && manShape->PreloadVBuffer(level,dist2);
  }
  return false;
}

bool Transport::NeedsPrepareProxiesForDrawing() const
{
  return base::NeedsPrepareProxiesForDrawing();
}

/// Functor for preparing of gunner proxy for drawing
class PrepareGunnerForDrawing : public ITurretFuncV
{
protected:
  Object *_rootObj;
  int _rootLevel;
  const AnimationContext &_animContext;
  const PositionRender &_transform;
  int _level;
  ClipFlags _clipFlags;
  SortObject *_so;
  bool _external;

public:
  PrepareGunnerForDrawing(
    Object *rootObj, int rootLevel, const AnimationContext &animContext, const PositionRender &transform, int level, ClipFlags clipFlags, SortObject *so, bool external)
    : _rootObj(rootObj), _rootLevel(rootLevel), _animContext(animContext), _transform(transform), _level(level),
    _clipFlags(clipFlags), _so(so), _external(external) {}

  bool operator () (EntityAIFull *entity, TurretContextV &context)
  {
    if (!context._turret) return false; // continue
    const ManProxy &proxy = context._turretType->_gunnerProxy[_level];
    if (context._gunner && proxy.Present() && !(_external && context._turretVisualState->IsGunnerHidden() && !context._turretType->_viewGunnerInExternal))
    {
      Transport *vehicle = static_cast<Transport *>(entity); // we know where we call it

      bool getInOutOnCenter = false;
      if (context._turretType->GunnerPreciseGetInOut()==GETIN_CENTER)
      {
        const ManVisualState &mvs = context._gunner->RenderVisualState().cast<Man>();
        getInOutOnCenter = mvs._gettingInOut;
      }

      vehicle->PrepareCrewMemberForDrawing(
        _rootObj, _rootLevel, _animContext, _transform, _level, _clipFlags, _so, proxy, context._gunner, getInOutOnCenter);
    }
    return false; // continue
  }
};

/*!
\patch_internal 1.05 Date 7/18/2001 by Jirka
- Changed: enable drawing gunner in external camera even if turned in
\patch 1.85 Date 9/10/2002 by Jirka
- Added: CfgVehicles properties hideWeaponsDriver, hideWeaponsGunner, hideWeaponsCommander, hideWeaponsCargo
\patch 5126 Date 2/7/2007 by Flyman
- Fixed: Hellfires (maverickweapon) on a helicopter were visible even when the helicopter was destroyed
*/

void Transport::PrepareProxiesForDrawing(
  Object *rootObj, int rootLevel, const AnimationContext &animContext,
  const PositionRender &transform, int level, ClipFlags clipFlags, SortObject *so)
{
  bool external = !_shape->IsSpecLevel(level,VIEW_CARGO) && !_shape->IsSpecLevel(level,VIEW_PILOT) && !_shape->IsSpecLevel(level,VIEW_GUNNER);
  const TransportType *type = Type();
  const LevelProxies &proxies = type->_proxies[level];

  // driver proxy
  if (_driver && proxies._driverProxy.Present() && !(external && RenderVisualState().IsDriverHidden()
#if _VBS3
    && !Type()->_viewDriverInExternal
#endif
    ))
  {
    bool getInOutOnCenter = false;
    if (Type()->PreciseGetInOut()==GETIN_CENTER)
    {
      const ManVisualState &mvs = _driver->RenderVisualState().cast<Man>();
       getInOutOnCenter = mvs._gettingInOut;
    }

    PrepareCrewMemberForDrawing(
      rootObj, rootLevel, animContext, transform, level, clipFlags, so, proxies._driverProxy, _driver, getInOutOnCenter);
  }

  // gunners proxies
  PrepareGunnerForDrawing func(rootObj, rootLevel, animContext, transform, level, clipFlags, so, external);
  ForEachTurret(unconst_cast(RenderVisualState()), func);

  // cargo proxies
  for (int i=0; i<_manCargo.Size(); i++)
  {
    Person *cargo = _manCargo[i];
    if (!cargo) continue;
    // if cargo position is not defined, do not draw
    if (i >= proxies._cargoProxy.Size()) break;
    if (!proxies._cargoProxy[i].Present()) continue;

    bool getInOutOnCenter = false;
    if (Type()->CargoPreciseGetInOut(i)==GETIN_CENTER)
    {
      const ManVisualState &mvs = cargo->RenderVisualState().cast<Man>();
      getInOutOnCenter = mvs._gettingInOut;
    }

    PrepareCrewMemberForDrawing(
      rootObj, rootLevel, animContext, transform, level, clipFlags, so, proxies._cargoProxy[i], cargo, getInOutOnCenter);
  }

  // missile proxies
  Assert(_shape->IsLevelLocked(level));
  const Shape *shape = _shape->GetLevelLocked(level);

  // check current number of missiles
  for (int i=0; i<Type()->GetProxyCountForLevel(level); i++)
  {
    // Check if the corresponding proxy face is hidden. In such case don't draw the proxy
    {
      int section = Type()->GetProxySectionForLevel(level, i);
      if (section >= 0)
      {
        const PolyProperties &pp = animContext.GetSection(shape, section);
        int spec = pp.Special();
        if (spec & IsHidden) continue;
      }
    }

    const ProxyObjectTyped &proxy = Type()->GetProxyForLevel(level,i);
    Object *obj = proxy.obj;
    const EntityType *type = obj->GetEntityType();
    if (!type) continue;

    if (!strcmp(type->_simName, "maverickweapon"))
    {
      // Decide whether draw or not
      LODShape *pshape = GetMissileShape(proxy.id);
      if (!pshape) continue;

      // Calculate proxy transformation (might be 0 which means the proxy is not visible)
      PositionRender pTransform;
      {
        SkeletonIndex boneIndex = Type()->GetProxyBoneForLevel(level, i);
        Matrix4Val proxyTransform = AnimateProxyMatrix(RenderVisualState(), level, proxy.GetTransform(), boneIndex);
        pTransform = transform * proxyTransform;
      }

      // LOD detection
      // check weapon proxy

      // TODO: propagate Scale instead of Scale() all way down from here
      int level = GScene->LevelFromDistance2(pshape, so->distance2, pTransform.position.Scale());
      if (level == LOD_INVISIBLE) continue;

      // temporary proxy object
      Ref<Object> temp = new Object(pshape, VISITOR_NO_ID);
      // store and register for drawing
      GScene->TempProxyForDrawing(temp, rootObj, level, rootLevel, clipFlags, so->distance2, pTransform);
    }
  }

  base::PrepareProxiesForDrawing(rootObj, rootLevel, animContext, transform, level, clipFlags, so);
}

void Transport::PrepareCrewMemberForDrawing(
  Object *rootObj, int rootLevel, const AnimationContext &animContext, const PositionRender &transform, int level, ClipFlags clipFlags, SortObject *so,
  const ManProxy &proxy, Person *man, bool getInOutOnCenter)
{
  LODShape *manShape = man->GetShape();

  // decide the transformation
  Matrix4 proxyTransform;

  if (getInOutOnCenter)
    proxyTransform = Matrix4(MTranslation,-GetShape()->BoundingCenter());
  else
    proxyTransform = proxy.transform;

  // animate proxy matrix
  AnimateManProxyMatrix(animContext.GetVisualState(), level, proxy, proxyTransform);
  proxyTransform.SetPosition(proxyTransform.FastTransform(manShape->BoundingCenter()));
  PositionRender pTransform = transform * proxyTransform;

  // decide the proxy LOD
  int pLevel;
  bool insidePlayer = false;
  float dist2 = so->distance2;
  CameraType camType = GWorld->GetCameraType();
  if (camType != CamExternal && camType != CamGroup)
  {
    if (!GWorld->GetCameraEffect())
    {
      AIBrain *unit = GWorld->FocusOn();
      Person *player = unit ? unit->GetPerson() : NULL;
      insidePlayer = man == player;
    }
  }
  // LOD detection
  if (insidePlayer)
  {
    if (man->UseInternalLODInVehicles())
      pLevel = man->InsideLOD(CamInternal);
    else
      pLevel = 0; // select best external LOD
  }
  else
  {
    // TODO: propagate Scale2 instead of Scale() all way down from here
    pLevel = GScene->LevelFromDistance2(manShape,dist2,pTransform.position.Scale());
  }
#if _VBS3
  if (pLevel < 0) return;
#endif

  if (pLevel == LOD_INVISIBLE) return;

  Man::ProtectedVisualState<const Man::VisualState> mvs = man->RenderVisualStateScope();
  // registration for drawing
  // avoid clipping because the bounding box is not correctly animated (pass clipFlags 0)
  GScene->ProxyForDrawing(man, rootObj, pLevel, rootLevel, 0, dist2, pTransform);

  // scan if there are some proxies inside of this proxy
  if (man->NeedsPrepareProxiesForDrawing())
  {
    const EntityType *manType = man->GetEntityType();
    if (manType)
    {
      // LOD selection for the proxy of the proxy
      int ppLevel = -1;
      LODShape *manShape = man->GetShape();
      if (insidePlayer)
      {
        // find inside view
        ppLevel = manShape->FindSpecLevel(VIEW_PILOT);
        // if there is no inside view, use the best LOD (if there is any)
        if (ppLevel < 0 && manShape->FindSimplestLevel() >= 0) ppLevel = 0;
      }
      if (ppLevel < 0)
      {
        // TODO: propagate Scale2 instead of Scale() all way down from here
        ppLevel = GScene->LevelFromDistance2(manShape, dist2, pTransform.position.Scale());
      }
      // most likely the Animate is empty, but we call it for consistency
      ShapeUsed pLock = manShape->Level(ppLevel);
      AnimationContextStorageGeom storage;
      AnimationContext animContext(man->RenderVisualState(),storage);
      pLock->InitAnimationContext(animContext, manShape->GetConvexComponents(ppLevel), false);
      man->Animate(animContext, ppLevel, true, man, dist2);
      if (ppLevel != LOD_INVISIBLE)
      {
        man->PrepareProxiesForDrawing(rootObj, rootLevel, animContext, pTransform, ppLevel, ClipAll, so);
      }
      man->Deanimate(ppLevel, true);
    }
  }
}

int Transport::GetCrewMemberComplexity(int level, const Matrix4 &pos, float dist2, const ManProxy &proxy, Person *man, bool getInOutOnCenter) const
{
  //similar to Object::DrawProxies

  // animate and draw driver
  LODShape *manShape=man->GetShape();

  Matrix4 proxyTransform;

  if (getInOutOnCenter)
    proxyTransform = Matrix4(MTranslation,-GetShape()->BoundingCenter());
  else
    proxyTransform = proxy.transform;

  // animate proxy matrix
  AnimateManProxyMatrix(RenderVisualState(), level,proxy,proxyTransform);

  proxyTransform.SetPosition(proxyTransform.FastTransform(manShape->BoundingCenter()));
  Matrix4Val pTransform = pos * proxyTransform;

  int pLevel;
  bool insidePlayer = false;
  CameraType camType = GWorld->GetCameraType();
  if (camType != CamExternal && camType != CamGroup)
  {
    AIBrain *unit = GWorld->FocusOn();
    Person *player = unit ? unit->GetPerson() : NULL;
    insidePlayer = man == player;
  }

  // LOD detection
  if (insidePlayer)
  {
#if 0
    pLevel = man->InsideLOD(CamInternal);
#else
    pLevel = 0; // select best external LOD
#endif
  }
  else
  {
    pLevel = GScene->LevelFromDistance2(
      manShape,dist2,pTransform.Scale()
      );
  }
  if (pLevel != LOD_INVISIBLE)
    return man->GetComplexity(pLevel,pos);
  return 0;
}

/// Functor counting gunner proxies
class CountGunnerProxy : public ITurretFunc
{
protected:
  int &_count; // out
  int _level;

public:
  CountGunnerProxy(int &count, int level)
    : _count(count), _level(level) {}

  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turret) return false; // continue
    const ManProxy &proxy = context._turretType->_gunnerProxy[_level];
    if (proxy.Present()) _count++;
    return false; // continue
  }
};

int Transport::GetProxyCount(int level) const
{
  const TransportType *type=Type();
  const LevelProxies &proxies=type->_proxies[level];
  int count = proxies._cargoProxy.Size();
  if (proxies._driverProxy.Present()) count++;
  CountGunnerProxy func(count, level);
  ForEachTurret(func);

  return count+proxies._normalProxy.Size();
}

/// Functor searching for gunner proxy
class GetGunnerProxy : public ITurretFuncV
{
protected:
  int &_i; // out
  const ManProxy *&_proxy; // out
  Person *&_man; // out
  int _level;

public:
  GetGunnerProxy(int &i, const ManProxy *&proxy, Person *&man, int level)
    : _i(i), _proxy(proxy), _man(man), _level(level) {}

  bool operator () (EntityAIFull *entity, TurretContextV &context)
  {
    if (!context._turret) return false; // continue
    const ManProxy &proxy = context._turretType->_gunnerProxy[_level];
    if (proxy.Present())
    {
      // sometimes even turned in proxies may be hit
      if (_i-- == 0 && (!context._turretVisualState->IsGunnerHidden() || context._turretType->_viewGunnerInExternal))
      {
        _proxy = &proxy;
        _man = context._gunner;
        return true; // found
      }
    }
    return false; // continue
  }
};

Object *Transport::GetProxy(VisualStateAge age, LODShape *&shape, Object *&parentObject, int level,
  Matrix4 &transform, const Matrix4 &parentPos, int i) const
{
  const TransportType *type=Type();
  TransportVisualState const& vs = GetVisualStateByAge(age).cast<Transport>();
  // may return NULL
  const LevelProxies &proxies=type->_proxies[level];
  // first enumerate normal proxies
  if (i<proxies._normalProxy.Size())
  {
    int proxyIndex = proxies._normalProxy[i];
    const ProxyObjectTyped &proxy = type->GetProxyForLevel(level,proxies._normalProxy[i]);

    SkeletonIndex boneIndex = GetType()->GetProxyBoneForLevel(level, proxyIndex);
    Matrix4 proxyTransform = AnimateProxyMatrix(vs, level, proxy.GetTransform(), boneIndex);

    transform = parentPos * proxyTransform;

    shape = proxy.GetShape();
    parentObject = proxy.obj;
    return proxy.obj;

  }
  i -= proxies._normalProxy.Size();


  // find corresponding proxy
  int cargoCount = proxies._cargoProxy.Size();
  const ManProxy *proxy = NULL;
  Person *man = NULL;
  if (i<cargoCount)
  {
    proxy = &proxies._cargoProxy[i];
    if (i<_manCargo.Size())
    {
      man = _manCargo[i];
    }
  }
  else
  {
    i -= cargoCount;
    if (proxies._driverProxy.Present())
    {
      if (i==0 && !vs.IsDriverHidden()
#if _VBS3
        && !Type()->_viewDriverInExternal
#endif
        ) proxy = &proxies._driverProxy, man = _driver;
      i--;
    }
    if (i >= 0)
    {
      GetGunnerProxy func(i, proxy, man, level);
      ForEachTurret(unconst_cast(vs), func);
    }
  }
  if (!proxy || !man || !proxy->_proxy) return NULL;

  LODShape *manShape=man->GetShape();

  Matrix4 proxyTransform=proxy->transform;

  AnimateManProxyMatrix(vs, level,*proxy,proxyTransform);

  proxyTransform.SetPosition(proxyTransform.FastTransform(manShape->BoundingCenter()));
  transform = parentPos*proxyTransform;

  shape = manShape;
  parentObject = man;
  return man;
}

Matrix4 Transport::ProxyWorldTransform(VisualStateAge age, const Object *obj) const
{
  ObjectVisualState const& vs = GetVisualStateByAge(age);
  // find proxy
  int level = 0;
  int n = GetProxyCount(level);
  for (int i=0; i<n; i++)
  {
    // check if it is the wanted proxy
    // problem: which level?
    Matrix4 trans;
    LODShape *shape = NULL;
    Object *parentObject = NULL;
    Object *proxy = GetProxy(age, shape, parentObject,level,trans,vs,i);
    if (proxy==obj)
    {
      return trans;
    }
  }
  return vs.Transform();
}
Matrix4 Transport::ProxyInvWorldTransform(VisualStateAge age, const Object *obj) const
{
  ObjectVisualState const& vs = GetVisualStateByAge(age);
  // find proxy
  // TODO: search not only level 0
  // man should be placed somewhere not to be dependent on level
  int level=0;
  int n = GetProxyCount(level);
  for (int i=0; i<n; i++)
  {
    // check if it is the wanted proxy
    // problem: which level?
    Matrix4 trans;
    LODShape *shape = NULL;
    Object *parentObject = NULL;
    Object *proxy = GetProxy(age, shape, parentObject,level,trans,vs,i);
    if (proxy==obj)
    {
      return trans.InverseGeneral();
    }
  }
  return vs.GetInvTransform();
}

/// Functor for preload of gunner proxy
class PreloadGunner : public ITurretFuncV
{
protected:
  bool &_ret; // out
  int _level;
  float _dist2;
  bool _external;

public:
  PreloadGunner(bool &ret, int level, float dist2, bool external)
    : _ret(ret), _level(level), _dist2(dist2), _external(external) {}

  bool operator () (EntityAIFull *entity, TurretContextV &context)
  {
    if (!context._turret) return false; // continue
    const ManProxy &proxy = context._turretType->_gunnerProxy[_level];
    if (context._gunner && proxy.Present() && !(_external && context._turretVisualState->IsGunnerHidden() && !context._turretType->_viewGunnerInExternal))
    {
      Transport *vehicle = static_cast<Transport *>(entity); // we now where we call it
      if (!vehicle->PreloadCrewMember(proxy, context._gunner, _dist2)) _ret = false;
    }
    return false; // continue
  }
};

bool Transport::PreloadProxies(int level, float dist2, bool preloadTextures) const
{
  bool ret = true;

  bool external = !_shape->IsSpecLevel(level,VIEW_CARGO) && !_shape->IsSpecLevel(level,VIEW_PILOT) && !_shape->IsSpecLevel(level,VIEW_GUNNER);

  const TransportType *type=Type();
  const LevelProxies &proxies=type->_proxies[level];
  if (_driver && proxies._driverProxy.Present() && !(external && RenderVisualState().IsDriverHidden()
#if _VBS3
    && !Type()->_viewDriverInExternal
#endif
    ))
  {
    if (!PreloadCrewMember(proxies._driverProxy,_driver,dist2)) ret = false;
  }
  // CHANGED
  PreloadGunner func(ret, level, dist2, external);
  ForEachTurret(unconst_cast(RenderVisualState()), func);

  for( int i=0; i<_manCargo.Size(); i++ )
  {
    Person *cargo=_manCargo[i];
    if (!cargo) continue;
    // if cargo position is not defined, do not draw
    if (i>=proxies._cargoProxy.Size()) break;
    if (!proxies._cargoProxy[i].Present()) break;
    if (!PreloadCrewMember(proxies._cargoProxy[i],cargo,dist2)) ret = false;
  }

  // draw missiles
  // check number of missiles


  //Shape *sShape = _shape->Level(level);
  for (int i=0; i<Type()->GetProxyCountForLevel(level); i++)
  {
    const ProxyObjectTyped &proxy = Type()->GetProxyForLevel(level,i);
    Object *obj = proxy.obj;
    const EntityType *type = obj->GetEntityType();
    if (!type) continue;
    if (!strcmp(type->_simName, "maverickweapon"))
    {
      // LOD detection
      // check weapon proxy
      LODShape *pshape = GetMissileShape(proxy.id);
      if (!pshape) continue;
      int level = GScene->PreloadLevelFromDistance2(pshape, dist2, FutureVisualState().Scale());
      if (level<0) ret = false;
    }
  }

  if (!base::PreloadProxies(level, dist2, preloadTextures)) ret = false;
  return ret;
}

/// Functor calculating complexity of gunner proxy
class GetGunnerComplexity : public ITurretFuncV
{
protected:
  int &_nFaces; // out
  int _level;
  const Matrix4 &_pos;
  float _dist2;
  bool _external;

public:
  GetGunnerComplexity(int &nFaces, int level, const Matrix4 &pos, float dist2, bool external)
    : _nFaces(nFaces), _level(level), _pos(pos), _dist2(dist2), _external(external) {}

  bool operator () (EntityAIFull *entity, TurretContextV &context)
  {
    if (!context._turret) return false; // continue
    const ManProxy &proxy = context._turretType->_gunnerProxy[_level];
    if (context._gunner && proxy.Present() && !(_external && context._turretVisualState->IsGunnerHidden()))
    {
      bool getInOutOnCenter = false;
      if (context._turretType->GunnerPreciseGetInOut()==GETIN_CENTER)
      {
        const ManVisualState &mvs = context._gunner->RenderVisualState().cast<Man>();
        getInOutOnCenter = mvs._gettingInOut;
      }

      Transport *vehicle = static_cast<Transport *>(entity); // we now where we call it
      _nFaces += vehicle->GetCrewMemberComplexity(
        _level, _pos, _dist2,
        proxy, context._gunner,getInOutOnCenter);
    }
    return false; // continue
  }
};

int Transport::GetProxyComplexity(int level, const Matrix4 &pos, float dist2) const
{
  int nFaces = 0;

  bool external = !_shape->IsSpecLevel(level,VIEW_CARGO) && !_shape->IsSpecLevel(level,VIEW_PILOT);

  const TransportType *type=Type();
  const LevelProxies &proxies=type->_proxies[level];
  if (_driver && proxies._driverProxy.Present() && !(external && RenderVisualState().IsDriverHidden()
#if _VBS3
    && !Type()->_viewDriverInExternal
#endif
    ))
  {
    bool getInOutOnCenter = false;
    if (type->PreciseGetInOut()==GETIN_CENTER)
    {
      const ManVisualState &mvs = _driver->RenderVisualState().cast<Man>();
      getInOutOnCenter =  mvs._gettingInOut;
    }

    nFaces += GetCrewMemberComplexity
      (
      level,pos,dist2,
      proxies._driverProxy,_driver,getInOutOnCenter
      );
  }

  GetGunnerComplexity func(nFaces, level, pos, dist2, external);
  ForEachTurret(unconst_cast(RenderVisualState()), func);

  for( int i=0; i<_manCargo.Size(); i++ )
  {
    Person *cargo=_manCargo[i];
    if (!cargo) continue;
    // if cargo position is not defined, do not draw
    if (i>=proxies._cargoProxy.Size()) break;
    if (!proxies._cargoProxy[i].Present()) break;

    bool getInOutOnCenter = false; 
    if (type->CargoPreciseGetInOut(i)==GETIN_CENTER)
    {
      const ManVisualState &mvs = cargo->RenderVisualState().cast<Man>();
      getInOutOnCenter =  mvs._gettingInOut;
    }

    nFaces += GetCrewMemberComplexity
      (
      level,pos,dist2,
      proxies._cargoProxy[i],cargo,getInOutOnCenter
      );
  }

  MissileCounter missiles;
  CountMissiles(missiles);

  //Shape *sShape = _shape->Level(level);
  for (int i=0; i<Type()->GetProxyCountForLevel(level); i++)
  {
    const ProxyObjectTyped &proxy = Type()->GetProxyForLevel(level,i);
    Object *obj = proxy.obj;
    if (!obj) continue;
    const EntityType *type = obj->GetEntityType();
    if (!type) continue;
    RString simulation = type->_simName;
    if (strcmp(simulation, "maverickweapon") == 0)
    {
      for(int j = 0; j< missiles.NTables(); j++)
      {
        const AutoArray<MissileCounterItem> &missileItems = missiles.GetTable(j);
        for(int k = 0; k< missileItems.Size(); k++)
        {
          if (proxy.id < missiles[j]._maverickIndexOffset ||  (proxy.id >= missiles[j]._total + missiles[j]._maverickIndexOffset)) continue;
          // smart clipping par of obj->Draw
          // LOD detection
          Matrix4Val pTransform=pos*proxy.obj->FutureVisualState().Transform();

          LODShape *pshape = obj->GetShape();
          if (!pshape) continue;
          int level = GScene->LevelFromDistance2(
            pshape, dist2, pTransform.Scale()
            );
          if (level == LOD_INVISIBLE) continue;

          nFaces += obj->GetComplexity(level,pTransform);

          //break both cycles
          j = missiles.NTables();
          break;
        }
      }
    }
  }

  return nFaces;
}

void Transport::RecalcIKTransform(int level,const Person *driver, const ManProxy &proxy,
                       SkeletonIndex leftHandBone, SkeletonIndex rightHandBone, SkeletonIndex leftLegBone, SkeletonIndex rightLegBone,
                       Matrix4 & leftHandIK, Matrix4 & rightHandIK, Matrix4 & leftLegIK, Matrix4 & rightLegIK) const 
{
  if (SkeletonIndexValid(leftHandBone) || SkeletonIndexValid(rightHandBone) || SkeletonIndexValid(leftLegBone) || SkeletonIndexValid(rightLegBone))
  {
    Matrix4 parentBoneTrans = MIdentity;
    SkeletonIndex parentBoneIndex = proxy._proxy->boneIndex;

    if (SkeletonIndexValid(parentBoneIndex))
    {
      AnimateBoneMatrix(parentBoneTrans,FutureVisualState(),level,parentBoneIndex);
      parentBoneTrans = parentBoneTrans.InverseGeneral();
    }

    LODShape *manShape=driver->GetShape();
    Matrix4 proxyTransform=proxy.transform;
    AnimateManProxyMatrix(FutureVisualState(), level,proxy,proxyTransform);
    proxyTransform.SetPosition(proxyTransform.FastTransform(manShape->BoundingCenter()));

    Matrix4 trans = proxyTransform;
    Matrix4 transInv = trans.InverseGeneral();

    if (SkeletonIndexValid(leftHandBone) || SkeletonIndexValid(rightHandBone))
    {
      if (rightHandBone==leftHandBone)
      {
        Matrix4 mat = MIdentity;
        AnimateBoneMatrix(mat,FutureVisualState(),level,leftHandBone);

        if (SkeletonIndexValid(parentBoneIndex))
          mat = mat * parentBoneTrans;

        mat = transInv * mat * trans;
        leftHandIK = mat;
        rightHandIK = mat;
      }
      else
      {
        if (SkeletonIndexValid(leftHandBone))
        {
          Matrix4 mat = MIdentity;
          AnimateBoneMatrix(mat,FutureVisualState(),level,leftHandBone);

          if (SkeletonIndexValid(parentBoneIndex))
            mat = mat * parentBoneTrans;

          mat = transInv * mat * trans;
          leftHandIK = mat;
        }
        if (SkeletonIndexValid(rightHandBone))
        {
          Matrix4 mat = MIdentity;
          AnimateBoneMatrix(mat,FutureVisualState(),level,rightHandBone);

          if (SkeletonIndexValid(parentBoneIndex))
            mat = mat * parentBoneTrans;

          mat = transInv * mat * trans;
          rightHandIK = mat;
        }
      }
    }
    // Legs transformation
    if (SkeletonIndexValid(leftLegBone) || SkeletonIndexValid(rightLegBone))
    {
      if (rightLegBone==leftLegBone)
      {
        Matrix4 mat = MIdentity;
        AnimateBoneMatrix(mat,FutureVisualState(),level,leftLegBone);

        if (SkeletonIndexValid(parentBoneIndex))
          mat = mat * parentBoneTrans;

        mat = transInv * mat * trans;
        leftLegIK = mat;
        rightLegIK = mat;
      }
      else
      {
        if (SkeletonIndexValid(leftLegBone))
        {
          Matrix4 mat = MIdentity;
          AnimateBoneMatrix(mat,FutureVisualState(),level,leftLegBone);

          if (SkeletonIndexValid(parentBoneIndex))
            mat = mat * parentBoneTrans;

          mat = transInv * mat * trans;
          leftLegIK = mat;
        }
        if (SkeletonIndexValid(rightLegBone))
        {
          Matrix4 mat = MIdentity;
          AnimateBoneMatrix(mat,FutureVisualState(),level,rightLegBone);

          if (SkeletonIndexValid(parentBoneIndex))
            mat = mat * parentBoneTrans;

          mat = transInv * mat * trans;
          rightLegIK = mat;
        }
      }
    }
  }
}

Matrix4 Transport::FindMissilePos(int index, bool &found, int maverickweaponIndexOffset) const
{
  found = false;
  //do not use proxy, but memory points if maverickweaponIndexOffset < 0
  if(maverickweaponIndexOffset < 0) return MIdentity;

  //ShapeUsed  lock = _shape->Level(0);
  for (int i=0; i<Type()->GetProxyCountForLevel(0); i++)
  {
    const ProxyObjectTyped &proxy = Type()->GetProxyForLevel(0,i);
    Object *obj = proxy.obj;
    const EntityType *type = obj->GetEntityType();
    if (!type) continue;
    if (strcmp(type->_simName, "maverickweapon")) continue;
    if (proxy.id!= (index + maverickweaponIndexOffset)) continue;
    found = true;
    Matrix4 pos;
    pos.SetPosition(proxy.obj->FutureVisualState().Position());
    // proxy matrix is XZ reversed
    pos.SetDirectionAside(-proxy.obj->FutureVisualState().DirectionAside());
    pos.SetDirection(-proxy.obj->FutureVisualState().Direction());
    pos.SetDirectionUp(proxy.obj->FutureVisualState().DirectionUp());
    return pos;
  }

  return MIdentity;
}

bool Transport::GetOpticsCamera(Matrix4 &transf, CameraType camType) const
{
  AIBrain *unit = GWorld->FocusOn();
  if (!unit)
    return false;

  Person *man = unit->GetPerson();
  if (!man)
    return false;

  VisualState& vs = unconst_cast(RenderVisualState());
  
  const TransportType *type = Type();

  TurretContextV context;
  context._turret = NULL;

  int index = -1;
  if (man == _driver)
    index = type->_driverOpticsPos;
  else
  {
#if _VBS3 //CommanderOverride
    if (FindOpticsTurret(vs, man, context) && context._turret)
#else
    if (FindTurret(vs, man, context) && context._turret)
#endif
    {
      if (Type()->_hideProxyInCombat && context._turretVisualState->GetGunnerHidden() < 0.5f)
        index = context._turretType->_gunnerOutOpticsPos;
      else
        index = context._turretType->_gunnerOpticsPos;
    }
  }
  if (index < 0)
    return false;

  // animate
  const Shape *memory = _shape->MemoryLevel();
  const NamedSelection::Access ptSel(memory->NamedSel(index),_shape,memory);
  int ptIndex = ptSel[0];
  transf.SetPosition(memory->Pos(ptIndex));
  transf.SetOrientation(M3Identity);
  if (_shape->GetSkeleton())
  {
    // get bone index of the point
    const AnimationRTWeight &weight = memory->GetVertexBoneRef()[ptIndex];
    if (weight.Size() > 0)
    {
      Assert(weight.Size() == 1);
      SubSkeletonIndex ssBone = weight[0].GetSel();
      SkeletonIndex bone = memory->GetSkeletonIndexFromSubSkeletonIndex(ssBone);
      AnimateBoneMatrix(transf, vs, _shape->FindMemoryLevel(), bone);
    }
  }

  // eventually apply offsets provided by laser targeting or ballistics computer
  if (camType == CamGunner)
  {
#if _VBS3_LASE_LEAD
    Turret *turret = context._turret;
    if (turret)
    {
      Matrix3 rot1(MRotationX,turret->_laseOffsetX);
      //    Matrix3 rot2(MRotationY,turret->_laseOffsetY);
      //    transf.SetOrientation(transf.Orientation() * rot1 * rot2);
      transf.SetOrientation(transf.Orientation() * rot1);
    }
#endif

  }

  // add camera shake
  GWorld->GetCameraShakeManager().UpdateCameraTransform(transf);

  return true;
}

Vector3 Transport::CameraPointRel(CameraType camType) const
{
  return _shape->AimingCenter();
}

bool Transport::GetProxyCamera(Matrix4 &transf, CameraType camType) const
{
  AIBrain *unit = GWorld->FocusOn();
  if (!unit) return false;

  Person *man = unit->GetPerson();
  if (!man) return false;

  const TransportType *type = Type();
  int level = InsideLOD(camType);
  if (level == LOD_INVISIBLE) return false;

  if (level < 0) return false;

  if (camType==CamExternal)
  { //for 3rd person view, do not return the ProxyCamera, but vehicle Transform (in model coordinates), ie. identity
    // we want the camera to point at the CameraPosition
    // TODO: define via config
    transf.SetTranslation(CameraPointRel(camType));
    return true;
  }

  const VisualState &vs = RenderVisualStateScope();
  
  // the shape needs to be loaded for proxy animation
  ShapeUsed lock = _shape->Level(level);

  const LevelProxies &proxies = type->_proxies[level];

  const ManProxy *proxy = NULL;
  if (man == _driver)
  {
    proxy = &proxies._driverProxy;
    transf = proxy->transform;

    if (Type()->PreciseGetInOut()==GETIN_CENTER)
    { 
      const ManVisualState &mvs = man->RenderVisualState().cast<Man>();
      if (mvs._gettingInOut)
        transf = Matrix4(MTranslation,-GetShape()->BoundingCenter());
    }
  }
  else
  {
    TurretContext context;
    if (FindTurret(man, context) && context._turretType)
    {
      proxy = &context._turretType->_gunnerProxy[level];
      transf = proxy->transform;

      if (context._turretType->GunnerPreciseGetInOut()==GETIN_CENTER)
      { 
        const ManVisualState &mvs = man->RenderVisualState().cast<Man>();
        if (mvs._gettingInOut)
          transf = Matrix4(MTranslation,-GetShape()->BoundingCenter());
      }
    }
    else for (int i=0; i<_manCargo.Size(); i++)
    {
      if (i >= proxies._cargoProxy.Size()) break;
      if (!proxies._cargoProxy[i].Present()) continue;
      if (man == _manCargo[i])
      {
        proxy = &proxies._cargoProxy[i];
        transf = proxy->transform;

        if (Type()->CargoPreciseGetInOut(i)==GETIN_CENTER)
        { 
          const ManVisualState &mvs = man->RenderVisualState().cast<Man>();
          if (mvs._gettingInOut)
            transf = Matrix4(MTranslation,-GetShape()->BoundingCenter());
        }

        break;
      }
    }
  }
  if (!proxy || !proxy->Present()) return false;

  // TODO:
  Vector3 pos = man->GetPilotPosition(camType);
  transf.SetPosition(transf.FastTransform(pos));
  const ProxyObject &proxyObj = *proxy->_proxy;
  AnimateBoneMatrix(transf,vs,level,proxyObj.boneIndex);
  transf.SetOrientation(transf.Orientation() * man->HeadBobOrientation(GetVisualStateAge(vs),camType,false));
  return true;
}

static inline bool CrewCanBeHit(const Person *crew, float hidden)
{
  return crew && hidden<0.5f && crew->Brain() && crew->Brain()->LSIsAlive();
}

struct CanBeHitResult
{
  const ManProxy *_proxy; // out
  const Person *_man; // out
  float _bestPenalty;
  Vector3 _modelDirToAim;

  explicit CanBeHitResult(Vector3Par modelDirToAim)
  :_proxy(NULL),_man(NULL),_bestPenalty(FLT_MAX),_modelDirToAim(modelDirToAim)
  {
  }

  void Evaluate(const ManProxy &proxy, Person *person, float hidden)
  {
    if (proxy.Present() && CrewCanBeHit(person, hidden))
    {
      float cosAlpha = -_modelDirToAim.CosAngle(proxy.transform.Position());
      if (cosAlpha<_bestPenalty)
      {
        _bestPenalty = cosAlpha;
        _proxy = &proxy;
        _man = person;
      }

    }
  }
};

/// Functor checking if gunner in turret can be hit
class GunnerCanBeHit : public ITurretFuncV
{
protected:
  CanBeHitResult &_result;
  int _level;

public:
  GunnerCanBeHit(CanBeHitResult &result, int level)
    : _result(result), _level(level) {}

  bool operator () (EntityAIFull *entity, TurretContextV &context) override
  {
    if (!context._turret) return false; // continue
    _result.Evaluate(context._turretType->_gunnerProxy[_level],context._gunner,context._turretVisualState->GetGunnerHidden());
    // always continue, we may find a better one
    return false;
    }
};

Vector3 Transport::AimingPositionCrew(ObjectVisualState const& ovs, const AmmoType *ammo, Vector3Par aimFromDir) const
{
  int fireGeom = _shape->FindFireGeometryLevel();
  if (fireGeom>=0 && CheckCrewVulnerable())
  {
    VisualState& vs = unconst_cast(ovs.cast<Transport>());

    // check if anyone of the crew can be hit
    // first target gunner, commander, driver
    const LevelProxies &proxies = Type()->_proxies[fireGeom];
    CanBeHitResult result(vs.DirectionWorldToModel(aimFromDir));
    {
      GunnerCanBeHit func(result, fireGeom);
      ForEachTurret(vs, func);
    }
      #if _VBS3
        bool driverInExternal = Type()->_viewDriverInExternal;
      #else
        bool driverInExternal = false;
      #endif

    result.Evaluate(proxies._driverProxy,_driver,vs._driverHidden || driverInExternal);
    if (result._proxy)
      {
      // we know who should we aim at

      LODShape *manShape=result._man->GetShape();
      Matrix4 proxyTransform = result._proxy->transform;
      AnimateManProxyMatrix(vs, fireGeom,*result._proxy,proxyTransform);

      proxyTransform.SetPosition(proxyTransform.FastTransform(manShape->BoundingCenter()));

      // get man aiming position - it should already be animated
      Vector3 aimPos = result._man->GetLocalAimingPosition();
      return FastTransform(proxyTransform.FastTransform(aimPos));
    }
  }
  return base::AimingPosition(ovs, ammo,aimFromDir);
}

Vector3 Transport::AimingPosition(ObjectVisualState const& vs, const AmmoType *ammo, Vector3Par aimFromDir) const
{
  DoAssert(!ammo || aimFromDir.SquareSize()>0); // when ammo is specified, aiming direction needs to be specified as well
  // if the ammo is weak it might be better to target crew instead of tank
  if (ammo && (ammo->_simulation==AmmoShotBullet || ammo->_simulation==AmmoShotSpread) && ammo->hit*Type()->GetInvArmor()<0.5f)
  {
    return AimingPositionCrew(vs, ammo, aimFromDir);
  }
  // if no crew can be hit, target vehicle
  return base::AimingPosition(vs, ammo, aimFromDir);
}


float Transport::NeedsLoadFuel() const
{
  if (IsDamageDestroyed()) return 0;
  float max=GetType()->GetMaxFuelCargo();
  if( max<=0 ) return 0;
  return 1-GetFuelCargo()/max;
}

float Transport::NeedsLoadAmmo() const
{
  if (IsDamageDestroyed()) return 0;
  float max=GetType()->GetMaxAmmoCargo();
  if( max<=0 ) return 0;
  return 1-GetAmmoCargo()/max;
}

float Transport::NeedsLoadRepair() const
{
  if (IsDamageDestroyed()) return 0;
  float max=GetType()->GetMaxRepairCargo();
  if( max<=0 ) return 0;
  return 1-GetRepairCargo()/max;
}


bool Transport::IsPossibleToGetIn() const
{
  if( _isDead ) return false;
  if( _isUpsideDown ) return false;
  if (IsDamageDestroyed()) return false;
  return true;
}

bool Transport::IsWorking() const
{
  if( _isDead ) return false;
  if( _isUpsideDown ) return false;
  if (IsDamageDestroyed()) return false;
  return true;
}

bool Transport::IsAbleToMove() const
{
  // if vehicle has no fuel and should have some, it cannot move
  //if (GetFuel()<=0 && Type()->GetFuelCapacity()>0) return false;
  return IsWorking();
}

bool Transport::EjectNeeded() const
{
  return !IsPossibleToGetIn() || !IsAbleToMove();
}

struct IsAbleToFireTurret : public ITurretFunc
{
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    return entity->IsAbleToFire(context);
  }
};

bool Transport::IsAbleToFireAnyTurret() const
{
  IsAbleToFireTurret func;
  return ForEachTurret(func);
}

bool Transport::IsAbleToFire(const TurretContext &context) const
{
  // if there is a dead gunner, we cannot fire
  AIBrain *gunner = context._gunner ? context._gunner->Brain() : NULL;
  // ? commander or pilot can move to this turret ?
  if (!gunner || !gunner->LSIsAlive()) gunner = EffectiveObserverUnit();
  if (!gunner || !gunner->LSIsAlive()) gunner = PilotUnit();
  if (!gunner || !gunner->LSIsAlive()) gunner = context._turret ? context._turret->GetGunnerAssigned() : NULL;
  if (!gunner || !gunner->LSIsAlive())
  {
    return false;
  }

  bool found = context._weapons->ForEachMagazine([](Magazine *mag) -> bool
  {
    return true; // some magazine exists
  });

  if (!found)
  {
    // check also loaded magazines
    found = context._weapons->ForEachMuzzle([](const WeaponModePtr &ptr) -> bool
    {
      const Magazine *magazine = ptr.GetMagazine();
      return magazine != NULL; // done or continue;
    });
    if (!found) return false;
  }
  return IsWorking();
}

/// Functor returning cost of the gunner
class GetGunnerCost : public ITurretFunc
{
protected:
  float &_cost;

public:
  GetGunnerCost(float &cost) : _cost(cost) {}

  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (context._turret && context._gunner) _cost += context._gunner->GetType()->GetCost();
    return false; // continue
  }
};

float Transport::CalculateTotalCost() const
{
  float cost = GetType()->GetCost();
  if (_driver) cost += _driver->GetType()->GetCost();
  GetGunnerCost func(cost);
  ForEachTurret(func);
  for (int i=0; i<_manCargo.Size(); i++)
  {
    if (_manCargo[i]) cost += _manCargo[i]->GetType()->GetCost();
  }
  return cost;
}

bool Transport::QCanIBeIn(Person *who) const
{
  if (!IsWorking()) return false;
  /*
  if (!who || who->QIsManual())
    // let player decide if its worth getting in
    return true;
  else
    // check IsAbleToFire?
    return IsAbleToMove();
  */
  return true;
}

bool Transport::GetActionParams(UIActionParams &params, const Action *action, AIBrain *unit) const
{
  switch (action->GetType())
  {
    case ATMoveToDriver:
    case ATMoveToPilot:
    case ATMoveToGunner:
    case ATMoveToCommander:
    case ATMoveToCargo:
    case ATManualFire:
    case ATManualFireCancel:
    case ATArtilleryComputer:
      // No arguments
      return true;
  }
  return base::GetActionParams(params, action, unit);
}

static void SwapPositions(Transport *vehicle,
 Ref<Person> &p1, RString a1, bool cargo1,
 Ref<Person> &p2, RString a2, bool cargo2)
{
  // switch animation state
  Swap(p1,p2);
  if (p1) p1->SwitchAction(a1);
  if (p2) p2->SwitchAction(a2);
  AIBrain *unit1 = p1 ? p1->Brain() : NULL;
  AIBrain *unit2 = p2 ? p2->Brain() : NULL;
  if (unit1)
  {
    if (cargo1)
      unit1->SetInCargo(true);
    else if (unit1->IsInCargo())
      unit1->SetInCargo(false);
  }
  if (unit2)
  {
    if (cargo2)
      unit2->SetInCargo(true);
    else if (unit2->IsInCargo())
      unit2->SetInCargo(false);
  }
}

bool Transport::CanMoveToDriver(AIBrain *unit, int currentCompartments) const
{
  if (!Type()->_hasDriver) return false; // no driver position
  if (_driverLocked) return false; // locked driver position
  if (unit == PilotUnit()) return false; // already in driver position
  if (PilotUnit() && PilotUnit()->IsAnyPlayer()) return false; // driver position occupied by a player
  if ((Type()->_driverCompartments & currentCompartments) == 0) return false; // different compartment
  // check whether driver animation is available
  Person *person = unit->GetPerson();
  if (person)
  {
    if (!person->IsAction(DriverAction()))
      return false;
  }

  return true;
}

bool Transport::CanMoveToTurret(TurretContextV &context, AIBrain *unit, int currentCompartments) const
{
  if (!context._turret) return false; // no such turret
  if (context._turret->IsGunnerLocked()) return false; // no locked gunner
  AIBrain *gunnerUnit = context._gunner ? context._gunner->Brain() : NULL;
  if (unit == gunnerUnit) return false; // already in gunner position
  if (gunnerUnit && gunnerUnit->IsAnyPlayer()) return false; // gunner position occupied by a player
  if ((context._turretType->_gunnerCompartments & currentCompartments) == 0) return false; // different compartment
  // check whether gunner animation is available
  Person *person = unit->GetPerson();
  if (person)
  {
    if (!person->IsAction(context._turret->GunnerAction(*context._turretVisualState, *context._turretType)))
      return false;
  }

  return true;
}

bool Transport::CanMoveToCargo(int index, AIBrain *unit, int currentCompartments) const
{
  AIBrain *cargoUnit = _manCargo[index] ? _manCargo[index]->Brain() : NULL;
  if (unit == cargoUnit) return false; // already in this cargo position
  if (_manCargo.IsLocked(index)) return false; // locked cargo position
  if (cargoUnit && cargoUnit->IsAnyPlayer()) return false; // cargo position occupied by a player
  if ((GetCargoCompartments(index) & currentCompartments) == 0) return false; // different compartment
  // check whether cargo animation is available
  Person *person = unit->GetPerson();
  if (person)
  {
    if (!person->IsAction(CargoAction(index)))
      return false;
  }

  return true;
}

bool Transport::CanChangePosition(AIBrain *unit) const
{
  // if there is no effective commander, we may still be able to change position
  AIBrain *commander = CommanderUnit();

  bool canChangePosition = !commander || unit == commander;
  if (!canChangePosition && GWorld->GetMode() == GModeNetware)
  {
    // special conditions for player in MP
    if (unit->IsAnyPlayer())
      canChangePosition = !_effCommander || !_effCommander->Brain() || !_effCommander->Brain()->IsAnyPlayer();
  }

  if (!canChangePosition) return false;

  bool canHideGunner = true;
  TurretContext context;
  if (FindTurret(unit->GetPerson(), context) && context._turret)
    canHideGunner = context._turretType->_canHideGunner;
  return (!Type()->_hideProxyInCombat || (IsPersonHidden(FutureVisualState(), unit->GetPerson()) || !canHideGunner) || unit->IsInCargo());
}

/*!
\patch 5117 Date 1/11/2007 by Ondra
- Fixed: Vehicle side was assumed incorrectly when player switched into a crew position from cargo.
\patch 5140 Date 3/15/2007 by Jirka
- Fixed: Vehicle commanding - after player switched to driver and then to commander, driver did not follow orders sometimes
*/

void Transport::ChangePosition(const Action *action, Person *soldier, bool netAware, bool forced)
{
#if DIAG_CREW
  LogF("### Change position in %s - implementation", cc_cast(GetDebugName()));
#endif

  VisualState& vs = unconst_cast(FutureVisualState());

  AIBrain *unit = soldier->Brain();
  // ??? Can be Transport::CanChangePosition used here ???
  AIBrain *commander = CommanderUnit();
  bool canChangePosition = !commander || unit == commander || forced;
  if (!canChangePosition && GWorld->GetMode() == GModeNetware)
  {
    // special conditions for player in MP
    if (unit && unit->IsAnyPlayer())
      canChangePosition = !_effCommander || !_effCommander->Brain() || !_effCommander->Brain()->IsAnyPlayer();
  }

  if (!canChangePosition)
  {
#if DIAG_CREW
    LogF(" not enabled");
#endif
    return;
  }

  // check current unit position
  bool unitCargo = false;
  // assign functors
  Ref<Person> *unitPos = NULL;
  RString unitAct;

  if (soldier == _driver)
  {
    unitPos = &_driver;
    unitAct = DriverAction();
    if (_driver->Brain() == EffectiveObserverUnit())
      _manualFire = false;
    // driver is changing, cancel current command
    _moveMode = VMMFormation;
  }
  else
  {
    TurretContextV context;
    if (FindTurret(vs, soldier, context) && context._turret)
    {
      unitPos = &context._turret->_gunner;
      unitAct = context._turret->GunnerAction(*context._turretVisualState, *context._turretType);
      if (context._turret->_gunner->Brain() == EffectiveObserverUnit())
        _manualFire = false;
    }
    else
    {
      // find if cargo
      for (int i=0; i<_manCargo.Size(); i++)
      {
        if (soldier==_manCargo[i])
        {
          unitPos = &_manCargo.Set(i).man;
          unitAct = CargoAction(i);
          unitCargo = true;
          break;
        }
      }
    }
  }
  if (!unitPos)
  {
#if DIAG_CREW
    LogF(" original position not found");
#endif
    return;
  }
  switch (action->GetType())
  {
    case ATMoveToDriver:
    case ATMoveToPilot:
      if (!(_driver && _driver->Brain() && _driver->Brain()->IsAnyPlayer()))
      {
        SwapPositions(this,
          _driver,DriverAction(),false,
          *unitPos,unitAct,unitCargo);
        // driver is changing, cancel current command
        _moveMode = VMMFormation;
      }
      else
      {
#if DIAG_CREW
        LogF(" target position occupied");
#endif
      }
      break;
    case ATMoveToGunner:
      {
        Ref<Person> *unitPos2 = NULL;
        RString unitAct2;
        TurretContextV context;
        if (GetPrimaryGunnerTurret(vs, context) && context._turret)
        {
          unitPos2 = &context._turret->_gunner;
          unitAct2 = context._turret->GunnerAction(*context._turretVisualState, *context._turretType);
        }
        if (!unitPos2)
        {
  #if DIAG_CREW
          LogF(" target position not found");
  #endif
          return;
        }
        Person *gunner = *unitPos2;
        if (!(gunner && gunner->Brain() && gunner->Brain()->IsAnyPlayer()))
        {
          SwapPositions(this,
            *unitPos2, unitAct2, false,
            *unitPos, unitAct, unitCargo);
        }
        else
        {
  #if DIAG_CREW
          LogF(" target position occupied");
  #endif
        }
      }
      break;
    case ATMoveToCommander:
      {
        Ref<Person> *unitPos2 = NULL;
        RString unitAct2;
        TurretContextV context;
        if (GetPrimaryObserverTurret(vs, context) && context._turret)
        {
          unitPos2 = &context._turret->_gunner;
          unitAct2 = context._turret->GunnerAction(*context._turretVisualState, *context._turretType);
        }
        if (!unitPos2)
        {
  #if DIAG_CREW
          LogF(" target position not found");
  #endif
          return;
        }
        Person *commander = *unitPos2;
        if (!(commander && commander->Brain() && commander->Brain()->IsAnyPlayer()))
        {
          SwapPositions(this,
            *unitPos2, unitAct2, false,
            *unitPos, unitAct, unitCargo);
        }
        else
        {
  #if DIAG_CREW
          LogF(" target position occupied");
  #endif
        }
      }
      break;
    case ATMoveToCargo:
      if (_manCargo.Size()>0)
      {
        const ActionIndex *actionIndex = static_cast<const ActionIndex *>(action);
        int index = actionIndex->GetIndex();
        if (index >= 0 && index < _manCargo.Size())
        {
          SwapPositions(this,
            _manCargo.Set(index).man,CargoAction(index),true,
            *unitPos,unitAct,unitCargo);
        }
        else
        {
  #if DIAG_CREW
          LogF(" target position occupied");
  #endif
        }
      }
      break;
    case ATMoveToTurret:
      {
        const ActionTurret *actionTurret = static_cast<const ActionTurret *>(action);
        Turret *turret = actionTurret->GetTurret();
        TurretContextV context;
        if (turret && FindTurret(vs, turret, context))
        {
          if (!(turret->_gunner && turret->_gunner->Brain() && turret->_gunner->Brain()->IsAnyPlayer()))
          {
            SwapPositions(this,
              turret->_gunner, turret->GunnerAction(*context._turretVisualState, *context._turretType), false,
              *unitPos, unitAct, unitCargo);
          }
          else
          {
  #if DIAG_CREW
            LogF(" target position occupied");
  #endif
          }
        }
        else
        {
  #if DIAG_CREW
          LogF(" target position not found");
  #endif
          return;
        }
      }
      break;
  }
  if (unit && unit == GWorld->FocusOn())
  {
    // camera is attached to a soldier which is changing a position
    // reset settings as necessary
    GWorld->SwitchCameraTo(this,GWorld->GetCameraType(), false);
    GWorld->UI()->ResetVehicle(this);
    // we may need to select a weapon
    if (QIsManual(unit))
    {
      TurretContext context;
      if (FindTurret(unit->GetPerson(), context))
        AutoselectWeapon(unit->GetPerson());
    }
  }
}


void Transport::PerformAction(const Action *action, AIBrain *unit)
{
  // check if we are able to handle the action
  switch (action->GetType())
  {
#if _VBS2 // interact with vehs
    case ATInteractWithVehicle:
      {
        ConstParamEntryPtr userActions = Pars.FindEntry("CfgActions");
        if(!userActions) break;

        ConstParamEntryPtr entry = userActions->FindEntry("InteractWithVehicle");
        if (!entry) break;

        if (entry->FindEntry("statement"))
        {
          RString statement = *entry >> "statement";
          GameState *gstate = GWorld->GetGameState();
          gstate->VarSet("this", GameValueExt(this), true, false, GWorld->GetMissionNamespace());
          gstate->Execute(statement, GameState::EvalContext::_default, GWorld->GetMissionNamespace());
        }
      }
      return;
    case ATQuickEnterVehicle:
      {
        {
          ConstParamEntryPtr userActions = Pars.FindEntry("CfgActions");
          if(!userActions) break;

          ConstParamEntryPtr entry = userActions->FindEntry("QuickEnterVehicle");
          if (!entry) break;

          if (entry->FindEntry("statement"))
          {
            RString statement = *entry >> "statement";
            GameState *gstate = GWorld->GetGameState();
            gstate->VarSet("this", GameValueExt(this), true, false, GWorld->GetMissionNamespace());
            gstate->Execute(statement, GameState::EvalContext::_default, GWorld->GetMissionNamespace());
          }
        }
      }
      return;
#endif
    case ATMoveToDriver:
    case ATMoveToPilot:
    case ATMoveToGunner:
    case ATMoveToCommander:
    case ATMoveToCargo:
    case ATMoveToTurret:
      {
        // only owner can process actions
        DoAssert(GetNetworkManager().IsServerOwner());
#if DIAG_CREW
        LogF("### Change position in %s - action", cc_cast(GetDebugName()));
#endif
        ChangePosition(action, unit->GetPerson());
      }
      return;
    case ATManualFire:
      _manualFire = true;
      // make sure some weapon is selected
      {
        Person *gunner = GunnerUnit() ? GunnerUnit()->GetPerson() : NULL;
        AutoselectWeapon(gunner);
        //SendSimpleCommand(gunner, SCManualFire);
      }
      return;
    case ATManualFireCancel:
      _manualFire = false;
      {
        //Person *gunner = GunnerUnit() ? GunnerUnit()->GetPerson() : NULL;
        //SendSimpleCommand(gunner, SCCancelManualFire);
      }
      return;
    case ATArtilleryComputer:
      return;
    case ATGetOut:
      // perform an activity which will getout given soldier
      StartGetOutActivity(unit,false);
      return;
    case ATTakeVehicleControl:
      // only owner can process actions
      DoAssert(GetNetworkManager().IsServerOwner());
      if(unit != PilotUnit()) SetCopilot(unit? unit->GetPerson(): NULL);
      else SetCopilot(NULL);
      return;
    case ATSuspendVehicleControl:
      // only owner can process actions
      DoAssert(GetNetworkManager().IsServerOwner());
      SetCopilot(NULL);
      return;
    case ATLockVehicleControl:
      // only owner can process actions
      DoAssert(GetNetworkManager().IsServerOwner());
      _unLockedControls = false;
      SetCopilot(NULL);
      return;
    case ATUnlockVehicleControl:
      {
        // only owner can process actions
        DoAssert(GetNetworkManager().IsServerOwner());
        _unLockedControls = true;
        SetCopilot(NULL);
        return;
      }
    case ATEngineOn:
      EngineOnAction();
      return;
    case ATEngineOff:
      EngineOffAction();
      return;
    case ATEject:
      if (IsStopped())
        // if stopped, get out action is available - use get out, but do not switch off the enginestill keep the engine on
        StartGetOutActivity(unit,true);
      else
        Eject(unit);
      return;
    case ATLand:
      Land();
      return;
    case ATCancelLand:
      CancelLand();
      return;
    case ATTurnIn:
      {
        HidePerson(unit->GetPerson(), 1.0);
      }
      return;
    case ATTurnOut:
      {
        HidePerson(unit->GetPerson(), 0.0);
        if(GWorld->GetCameraType() == CamGunner) GWorld->SetCameraType(CamInternal);
      }
    case ATSearchLightOn:
    case ATSearchLightOff:
      {
        TurretContext context;
        if (unit->GetPerson() && FindTurret(unit->GetPerson(), context))
        {
          if(context._turret)
          {
            if (action->GetType() == ATSearchLightOn) context._turret->SetLightOn(true);
            else context._turret->SetLightOn(false);
          }
        }
      }
      return;
    case ATGear:
      // gear action inside the vehicle
#if _VBS3_INVENTORY
      if(!action->PerformScripted(unit))
#endif
      {
        Display *CreateInventoryDialog(ControlsContainer *parent, AIBrain *me, EntityAI *source);
        GWorld->DestroyUserDialog();
        GWorld->SetUserDialog(CreateInventoryDialog(NULL, unit, this));
      }
      return;
  }
  // fall-back to parent
  base::PerformAction(action,unit);
}

int Transport::GetCargoCompartments(int index) const
{
  Assert(index >= 0);
  const TransportType *type = Type();
  int n = type->_cargoCompartments.Size();
  if (n <= 0) return 0;
  if (index >= n) return type->_cargoCompartments[n - 1];
  return type->_cargoCompartments[index];
}

int Transport::GetPersonCompartments(Person *person) const
{
  const TransportType *type = Type();
  if (_driver == person) return type->_driverCompartments;
  TurretContext context;
  if (FindTurret(person, context))
  {
    if (context._turretType) return context._turretType->_gunnerCompartments;
    return 0;
  }
  for (int i=0; i<_manCargo.Size(); i++)
  {
    if (person == _manCargo[i]) return GetCargoCompartments(i);
  }
  return 0;
}

/// Functor collecting actions MoveToTurret
class GetMoveToTurretActions : public ITurretFunc
{
protected:
  const std::function<void(const UIAction &action)> &_add; // out
  AIBrain *_unit;
  int _compartments;

public:
  GetMoveToTurretActions(const std::function<void(const UIAction &action)> &add, AIBrain *unit, int compartments)
    : _add(add), _unit(unit), _compartments(compartments) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turret) return false; // continue
    AIBrain *unit = context._gunner ? context._gunner->Brain() : NULL;
    if (
      _unit != unit &&
      !(unit && unit->IsAnyPlayer()) &&
      (context._turretType->_gunnerCompartments & _compartments) != 0)
    {
      UIAction action(new ActionTurret(ATMoveToTurret, entity, context._turret));
      _add(action);
    }
    return false; // continue
  }
};

/// Functor checking if there is some enemy person in the vehicle
class IsEnemyFunc : public ITurretFunc
{
protected:
  AIBrain *_brain;

public:
  IsEnemyFunc(AIBrain *brain) : _brain(brain) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turret) return false;
    if (!context._gunner) return false;
    return _brain->IsEnemy(context._gunner->GetTargetSide(1));
  }
};

/// Functor searching the place where commander is usually sitting
class FindCommanderPlace : public ITurretFunc
{
protected:
  Person *&_commander;
  float _maxCommanding;

public:
  FindCommanderPlace(Person *&commander)
    : _commander(commander) {_maxCommanding = 0;}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turretType) return false; // continue
    if (context._turretType->_commanding > _maxCommanding)
    {
      _commander = context._gunner;
      _maxCommanding = context._turretType->_commanding;
    }
    return false; // check all turrets
  }
};

/*!
\patch 1.27 Date 10/17/2001 by Jirka
- Fixed: Manual fire in MP enabled if gunner is not player
*/

void Transport::GetActions(const std::function<void(const UIAction &action)> &add, AIBrain *unit)
{
  base::GetActions(add, unit);
  if (!unit)
    return;

#if _VBS2 // interact with vehs
  if (GWorld->IsIWVActive() && unit->IsPlayer()
    && (unit->GetVehicleIn() == this || unit->IsFreeSoldier())
    && !IsDestroyed())
  {
    bool allowIWV = true;

    Person *person = unit->GetPerson();
    Assert(person);

    // avoid get in enemy vehicle
    // TargetSide(float) for Person returns Civilian if they are captive
    // TargetSide for brain always returns the real side
    if(!unit->GetCaptive())
    {
      if (Driver() && unit->IsEnemy(Driver()->GetTargetSide(1))) allowIWV = false;
      IsEnemyFunc func(unit);
      if (allowIWV && ForEachTurret(func)) allowIWV = false;
      if (allowIWV)
        for (int i=0; i<GetManCargo().Size(); i++)
        {
          Person *man = GetManCargo()[i];
          if (!man) continue;
          if (unit->IsEnemy(man->GetTargetSide(1))) {allowIWV = false; break;}
        }
    }
    if (allowIWV && unit->IsFreeSoldier())
    {
      // check if stopped
      if (Speed().SquareSize() > Square(1.5)) allowIWV = false;

      // check if in front
      Vector3Val relPos = person->RenderVisualStateScope(true)->PositionWorldToModel(Position());
      if (relPos.Z() <= 0) allowIWV = false;

      Vector3 minMax[2];
      ClippingInfo(minMax,ClipGeometry);
      minMax[0] *= 1.3; minMax[1] *= 1.3; //increase Size


      //avoid going under ground with unit position, using ComPosition instead of position
      Vector3 RelPos = RenderVisualStateScope(true)->PositionWorldToModel(unit->COMPosition());

      if( RelPos.X() < minMax[0].X() || RelPos.X() > minMax[1].X()
        || RelPos.Y() < minMax[0].Y() || RelPos.Y() > minMax[1].Y()
        ||RelPos.Z() < minMax[0].Z() || RelPos.Z() > minMax[1].Z()
        )
        allowIWV = false;
    }

    // check lock
    if (allowIWV && (unit->GetVehicleIn() == this || _lock < LSLocked))
    {
      UIAction action(new ActionBasic(ATInteractWithVehicle, this));
      actions.Add(action);
      // Add quick enter action for vehicles
      UIAction quickEnterAction(new ActionBasic(ATQuickEnterVehicle, this));
      actions.Add(quickEnterAction);
    }
  }
#endif

  if (unit->IsFreeSoldier())
  {
    if (unit->IsPlayer() && _lock == LSLocked) return;

    bool leader = true;
    if (unit->IsAnyPlayer() && _lock == LSLockedForPlayer) leader = false;

    if (!leader) return;

    GetGetInActions(add, unit, true);
  }
  else if (unit->GetVehicleIn() == this)
  {
    bool isPlayer = unit->IsPlayer();
    if (!isPlayer || GetLock() != LSLocked /*|| unit->VehicleAssigned() != this*/)
    {
      TurretContext context;
      bool canEject = true;
      if (unit->GetPerson())
      {
        if(FindTurret(unit->GetPerson(), context) && context._turretType)
          canEject = context._turretType->_canEject;
        else if (_driver == unit->GetPerson())
          canEject = GetType()->_driverCanEject;
        else 
          canEject = GetType()->_cargoCanEject;
      }

      if (IsStopped())
      {
        add(UIAction(new ActionBasic(ATGetOut, this)));
        if (EngineIsOn() && canEject)
          add(UIAction(new ActionBasic(ATEject, this)));
      }
      else
        if(canEject) add(UIAction(new ActionBasic(ATEject, this)));

      // check if I can change position
      if (CanChangePosition(unit))
      {
        int currentCompartments = GetPersonCompartments(unit->GetPerson());

        if (CanMoveToDriver(unit, currentCompartments))
        {
          UIActionType type = GetType()->IsKindOf(GWorld->Preloaded(VTypeAir)) ? ATMoveToPilot : ATMoveToDriver;
          UIAction action(new ActionBasic(type, this));
          if (Type()->_driverIsCommander)
          {
            // default shortcut
          }
          else
          {
            // no shortcut
            action.shortcut = (UserAction)-1;
          }
          add(action);
        }

        GetMoveToTurretActions func(add, unit, currentCompartments);
        ForEachTurret(func);

        if (_manCargo.Size()>0 && !unit->IsInCargo())
        {
          int index = -1;
          // search for empty position
          for (int i=0; i<_manCargo.Size(); i++)
          {
            if(_manCargo.IsLocked(i)) continue;
            Person *man = _manCargo[i];

            if (!man && (GetCargoCompartments(i) & currentCompartments) != 0)
            {
              index = i;
              break;
            }
          }
          // if not found, search for position occupied by a AI
          if (index < 0)
          {
            for (int i=0; i<_manCargo.Size(); i++)
            {
              if(_manCargo.IsLocked(i)) continue;
              Person *man = _manCargo[i];
              if (!man) continue; // checked in the previous cycle
              if (
                (!man->Brain() || !man->Brain()->IsAnyPlayer()) &&
                (GetCargoCompartments(i) & currentCompartments) != 0)
              {
                index = i;
                break;
              }
            }
          }

          if (index >= 0)
          {
            UIAction action(new ActionIndex(ATMoveToCargo, this, index));
            add(action);
          }
        }
      }
    }
    if (isPlayer)
    {
      if (PilotUnit() == unit || CopilotUnit() == unit && GetFuel()>0)
      {
        if (EngineIsOn())
        {
#if _HELISIM_ROTORLIB
          HelicopterRTD *helicopter = dyn_cast<HelicopterRTD>(this);
          //RDT helic need more complex engine off
          if(!helicopter || !helicopter->IsRTDSimulationWanted())
#endif
          {
            UIAction action(new ActionBasic(ATEngineOff, this));
            add(action);
          }
        }
        else
        {
          UIAction action(new ActionBasic(ATEngineOn, this));
          add(action);
        }
      }
      if
        (
        // vehicle is supporting manual fire, only commander can manual fire, gunner is not player
        Type()->CanManualFire() && EffectiveObserverUnit() == unit &&
        (!GunnerUnit() || !GunnerUnit()->IsAnyPlayer())
        )
      {
        // commander can manual fire only when sitting on his place
        bool hisPlace = false;
        if (Type()->DriverIsCommander())
          hisPlace = unit->GetPerson() == _driver;
        else
        {
          Person *commander = NULL;
          FindCommanderPlace func(commander);
          ForEachTurret(func);
          hisPlace = unit->GetPerson() == commander;
        }
        if (hisPlace)
        {
          UIAction action(new ActionBasic(IsManualFire() ? ATManualFireCancel : ATManualFire, this));
          action.shortcut = GetManualFireAction();
          add(action);
        }
      }

      if (Type()->_hideProxyInCombat)
      {
        Person *person = unit->GetPerson();
        bool turn = false;
        bool out = false;
        if (person == Driver())
        {
          AIBrain *commUnit = CommanderUnit();
          if (
            // if driver is commander, allow hiding
            unit==commUnit ||
            // if there is no commander or commander is not hidden, allow hiding
            (!Type()->_hasObserver || Observer() || !IsPersonHidden(FutureVisualState(), Observer()))
            )
          {
            // if there is a commander and the commander is not hidden, do not allow hiding
            if (unit==commUnit || !commUnit || !IsPersonHidden(FutureVisualState(), commUnit->GetPerson()))
            {
              turn = !Type()->_forceHideDriver;
              out = FutureVisualState().IsDriverHidden();
            }
          }
        }
        else
        {
          TurretContextV context;
          if (FindTurret(unconst_cast(FutureVisualState()), person, context) && context._turret)
          {
            turn = context._turretType->_canHideGunner && !context._turretType->_forceHideGunner;
            out = context._turretVisualState->IsGunnerHidden();
          }
        }

        if (turn)
        {
          if (out)
          {
            UIAction action(new ActionBasic(ATTurnOut, this));
            add(action);
          }
          else
          {
            UIAction action(new ActionBasic(ATTurnIn, this));
            add(action);
          }
        }
      }
    }
  }
}


/// Functor collecting actions GetInTurret
class GetGetInTurretActionsLeader : public ITurretFunc
{
protected:
  const std::function<void(const UIAction &action)> &_add;
  Person *_person;

public:
  GetGetInTurretActionsLeader(const std::function<void(const UIAction &action)> &add, Person *person)
    : _add(add), _person(person) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turret) return false; // continue
    if (!context._turretType->_hasGunner) return false; // continue
    Transport *vehicle = static_cast<Transport *>(entity); // we know where we call it
    if (vehicle->QCanIGetInTurret(context._turret, _person))
    {
      UIAction action(new ActionTurret(ATGetInTurret, vehicle, context._turret));
      int n = context._turretType->NGunnerGetInPos();
      for (int i=0; i<n; i++)
        action._memoryPoints.Add(context._turretType->GetGunnerGetInPos(i)._pos);
      action.highlight = _person->IsActionHighlighted(ATGetInTurret, vehicle);
      _add(action);
    }
    return false; // for all turrets
  }
};


#if _HELISIM_PRECISE_INTERACTION
/// Functor collecting actions GetInTurret
class GetDrawGetInTurretActionsLeader3D : public ITurretFunc
{
protected:
  UIActions &_actions; // out
  Person *_person;
  bool _now;


public:
  GetDrawGetInTurretActionsLeader3D(
    UIActions &actions, Person *person, bool now)
    : _actions(actions), _person(person), _now(now) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turret) return false; // continue
    if (!context._turretType->_hasGunner) return false; // continue
    Transport *vehicle = static_cast<Transport *>(entity); // we know where we call it
    if (vehicle->QCanIGetInTurret(context._turret, _person))
    {
      if (_now)
      {
        const Camera* camera = GScene->GetCamera();
        if(!camera) return false;
        Object::ProtectedVisualState<const Person::VisualState> pvs = _person->RenderVisualStateScope(true);
        //action description
        UIAction action(new ActionTurret(ATGetInTurret, vehicle, context._turret));
        Vector3 position = vehicle->GetTurretGetInPosPrecise(context._turret, _person, pvs->Position())._pos - camera->Position();
        action.position = position;

        action.highlight = _person->IsActionHighlighted(ATGetInTurret, vehicle);
        _actions.Add(action);
      }
    }
    return false; // for all turrets
  }
};

/// Functor collecting actions GetInTurret
class GetDrawGetInTurretActions3D : public ITurretFunc
{
protected:
  UIActions &_actions; // out
  Person *_person;

public:
  GetDrawGetInTurretActions3D(UIActions &actions, Person *person)
    : _actions(actions), _person(person) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turret) return false; // continue
    if (!context._turretType->_hasGunner) return false; // continue
    if (context._turret->GetGunnerAssigned() == _person->Brain())
    {
      Transport *vehicle = static_cast<Transport *>(entity); // we know where we call it
      if (vehicle->QCanIGetInTurret(context._turret, _person))
      {
        const Camera* camera = GScene->GetCamera();
        if(!camera) return false;

        UIAction action(new ActionTurret(ATGetInTurret, vehicle, context._turret));
        action.highlight = _person->IsActionHighlighted(ATGetInTurret, vehicle);
  
        Object::ProtectedVisualState<const Person::VisualState> pvs = _person->RenderVisualStateScope(true);
        action.position = vehicle->GetTurretGetInPosPrecise(context._turret, _person, pvs->Position())._pos  - camera->Position();;

        _actions.Add(action);
      }
      return true; // position found
    }
    return false; // continue
  }
};

void Transport::GetDrawActions3D(UIActions &actions, AIBrain *unit)
{
  if (!unit)
    return;

  base::GetDrawActions3D(actions, unit);
}


void Transport::GetDrawGetInActions3D(UIActions &actions, AIBrain *unit)
{
  const Camera* camera = GScene->GetCamera();
  if(!camera) return;
  //resolve get in action after DrawActionsAnimatePosition (get in action use diferent action positions)
  if (unit->IsFreeSoldier() && Type()->UsePreciseGetInAction() && _lock != LSLocked)
  {
    bool leader = _lock == LSLockedForPlayer || _lock == LSUnlocked || unit->IsGroupLeader() || GWorld->GetMode() == GModeNetware;
    if(unit->IsAnyPlayer() && _lock == LSLockedForPlayer) leader = false;

    if (leader || unit->VehicleAssigned() == this)
    {
      bool leader = _lock == LSLockedForPlayer || _lock == LSUnlocked || unit->IsGroupLeader() || GWorld->GetMode() == GModeNetware;
      if(unit->IsAnyPlayer() && _lock == LSLockedForPlayer) leader = false;

      Person *person = unit->GetPerson();
      Assert(person);

      // avoid get in enemy vehicle
      if (Driver() && unit->IsEnemy(Driver()->GetTargetSide())) return;
      IsEnemyFunc func(unit);
      if (ForEachTurret(func)) return;
      for (int i=0; i<GetManCargo().Size(); i++)
      {
        Person *man = GetManCargo()[i];
        if (!man) continue;
        if (unit->IsEnemy(man->GetTargetSide())) return;
      }

      // check if stopped
      if (FutureVisualState().Speed().SquareSize() > Square(1.5)) return;

      if (leader)
      {
        if (QCanIGetIn(person))
        {
          UIActionType type = GetType()->IsKindOf(GWorld->Preloaded(VTypeAir)) ? ATGetInPilot : ATGetInDriver;

          ProtectedVisualState<const Person::VisualState> pvs = person->RenderVisualStateScope(true);
          ProtectedVisualState<const VisualState> rvs = RenderVisualStateScope(true);
          // check if near
          //if (CheckDriverGetInPosition(person, maxDist2, 0, true))
          {
            UIAction action(new ActionBasic(type, this));
            Vector3 position = GetDriverGetInPosPrecise(person, pvs->Position())._pos - camera->Position();
            action.position = position;
            action.radiusView = -1;
            actions.Add(action);
          }

        }
        GetDrawGetInTurretActionsLeader3D func(actions, person, true);
        ForEachTurret(func);

        // check the cargo (create a single action only)

        for (int i=0; i<_manCargo.Size(); i++)
        {
          if(i >= Type()->NCargoSizePrecise()) break;
          if (QCanIGetInCargo(person, i) != i) continue;
          // check if near

          ProtectedVisualState<const Person::VisualState> pvs = person->RenderVisualStateScope(true);
          ProtectedVisualState<const VisualState> rvs = RenderVisualStateScope(true);

          //if (CheckCargoGetInPosition(i, person, maxDist2, dist2, strict))
          {
            UIAction action(new ActionIndex(ATGetInCargo, this, i));
            Vector3 position = GetCargoGetInPosPrecise(i, person, pvs->Position())._pos - camera->Position();
            action.position = position;
            actions.Add(action);
          }
        }

      }
      else
      {

        if (GetDriverAssigned() == unit)
        {
          if (QCanIGetIn(person))
          {
            UIActionType type = GetType()->IsKindOf(GWorld->Preloaded(VTypeAir)) ? ATGetInPilot : ATGetInDriver;
            UIAction action(new ActionBasic(type, this));
            action.highlight = person->IsActionHighlighted(type, this);

            //action description
            ProtectedVisualState<const Person::VisualState> pvs = person->RenderVisualStateScope(true);
            action.position = GetDriverGetInPosPrecise(person, pvs->Position())._pos  - camera->Position();;

            actions.Add(action);
          }
        }
        else
        {
          GetDrawGetInTurretActions3D func(actions, person);
          if (!ForEachTurret(func))
          {
            // try the cargo
            int index = QCanIGetInCargo(person);
            if (index >= 0)
            {
              UIAction action(new ActionIndex(ATGetInCargo, this, index));

              ProtectedVisualState<const Person::VisualState> pvs = person->RenderVisualStateScope(true);
              action.position = GetCargoGetInPosPrecise(index, person, pvs->Position())._pos  - camera->Position();;

              action.highlight = person->IsActionHighlighted(ATGetInCargo, this);
              actions.Add(action);
            }
          }
        }
      }
    }
  }

}

#endif

GetInPoint Transport::AnimateGetInPoint(GetInPointIndexVal ptIndex) const
{
  const VisualState &vs = RenderVisualState();
  GetInPoint result;

  // indices of points in the memory LOD
  int posIndex = -1, dirIndex = -1;
  int memory = _shape->FindMemoryLevel();
  if (memory >= 0)
  {
    posIndex = ptIndex._pos;
    dirIndex = ptIndex._dir;
  }

  // animate point if given, use default if not
  if (posIndex >= 0) result._pos = AnimatePoint(vs, memory, posIndex);
  else
  {
    // some reasonable default
    float sizeCoef = _shape->GeometrySphere() * (1.0 / 4);
    result._pos = Vector3(+3 * sizeCoef, +1, -3 * sizeCoef);
  }

  // animate direction if given, use default if not
  result._dir = (dirIndex >= 0) ? AnimatePoint(vs,memory, dirIndex) - result._pos : -result._pos;
  result._dir.Normalize();

  return result;
}

#if _HELISIM_PRECISE_INTERACTION
bool CheckGetInPositionPrecise(GetInPoint &pt, Person *person, float radius,float radiusView)
{
  Camera *camera = GScene->GetCamera();
  if(!camera) return false;

  Vehicle *vehicle = NULL;
  if(person && person->Brain() ) vehicle = person->Brain()->GetVehicle();
  if(!vehicle) return false;

  Vector3 cameraPosition = camera->Position();

  //cursor direction
  Vector3 camDir = GInput.ActionDriverFreeLook() ? camera->Direction() : GWorld->UI()->GetCursorDirection().Normalized();
  Vector3 targetDir = pt._pos - cameraPosition; 

  //check radius
  if (radius > 0)
  {
    if(GWorld->GetCameraType() == CamExternal)
    {
      if(vehicle->AimingPosition(vehicle->RenderVisualState()).Distance2(pt._pos) > Square(radius)) return false;
    }
    else if(cameraPosition.Distance2(pt._pos) > Square(radius)) return false;
  }

  //check if we ar looking at modelPos
  if(radiusView > 0 && GScene->GetCamera())
  {
    //minimal distance
    Vector3 distnceVect = ( cameraPosition +  camDir * (targetDir * camDir)) - pt._pos;
    //only hide action, so shortcut can be still used 
    if( distnceVect.Size() <= radiusView)
    {
      return true;
    }
  }

  return false;
}
#endif

#if _HELISIM_PRECISE_INTERACTION
bool Transport::CheckDriverGetInPositionPrecise(Person *person, float radius, float radiusView) const
{
  GetInPoint pt = GetDriverGetInPosPrecise(person, person->RenderVisualState().Position());
  return CheckGetInPositionPrecise(pt, person, radius, radiusView);
}

bool Transport::CheckCargoGetInPositionPrecise(int cargoIndex, Person *person, float radius, float radiusView) const
{
  GetInPoint pt = GetCargoGetInPosPrecise(cargoIndex, person, person->RenderVisualState().Position());
  return CheckGetInPositionPrecise(pt, person, radius, radiusView);
}

bool Transport::CheckTurretGetInPositionPrecise(Turret *turret, Person *person, float radius, float radiusView) const
{
  GetInPoint pt = GetTurretGetInPosPrecise(turret, person, person->RenderVisualState().Position());
  return CheckGetInPositionPrecise(pt, person, radius, radiusView);
}
#endif

/// Functor collecting actions GetInTurret
class GetGetInTurretActions : public ITurretFunc
{
protected:
  const std::function<void(const UIAction &action)> &_add;
  Person *_person;

public:
  GetGetInTurretActions(const std::function<void(const UIAction &action)> &add, Person *person)
    : _add(add), _person(person) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turret) return false; // continue
    if (!context._turretType->_hasGunner) return false; // continue
    if (context._turret->GetGunnerAssigned() == _person->Brain())
    {
      Transport *vehicle = static_cast<Transport *>(entity); // we know where we call it
      if (vehicle->QCanIGetInTurret(context._turret, _person))
      {
        UIAction action(new ActionTurret(ATGetInTurret, vehicle, context._turret));
        action.highlight = _person->IsActionHighlighted(ATGetInTurret, vehicle);

#if _HELISIM_PRECISE_INTERACTION
        if(vehicle->Type()->UsePreciseGetInAction())
        {
          const UIActionTypeDesc &desc = UIActions::actionTypes[ATGetInTurret];
          if(!(vehicle->Type()->UsePreciseGetInAction() && vehicle->CheckTurretGetInPositionPrecise(context._turret, _person, desc.radius, desc.radiusView))) return false;        
          Object::ProtectedVisualState<const Person::VisualState> pvs = _person->RenderVisualStateScope(true);
          if(vehicle->Type()->UsePreciseGetInAction()) action.position = vehicle->GetTurretGetInPosPrecise(context._turret, _person, pvs->Position())._pos;
        }
        action.radiusView = -1;//! set radiusView < 0 to skip it in CheckActionsRadiusView
#endif

        _add(action);
      }
      return true; // position found
    }
    return false; // continue
  }
};

/*!
\patch 5138 Date 3/8/2007 by Jirka
- Fixed: Getting into vehicles - both player and AI now handle correctly exact cargo position
*/

void Transport::GetGetInActions(const std::function<void(const UIAction &action)> &add, AIBrain *unit, bool now)
{
  bool leader = true;
  if(unit->IsAnyPlayer() && _lock == LSLockedForPlayer) leader = false;

  Person *person = unit->GetPerson();
  Assert(person);

  // avoid get in enemy vehicle
  if (Driver() && unit->IsEnemy(Driver()->GetTargetSide())) return;
  IsEnemyFunc func(unit);
  if (ForEachTurret(func)) return;
  for (int i=0; i<GetManCargo().Size(); i++)
  {
    Person *man = GetManCargo()[i];
    if (!man) continue;
    if (unit->IsEnemy(man->GetTargetSide())) return;
  }

  if (now)
  {
    // check if stopped
    if (FutureVisualState().Speed().SquareSize() > Square(1.5)) return;
  }

  if (leader)
  {
    if (QCanIGetIn(person))
    {
      UIActionType type = GetType()->IsKindOf(GWorld->Preloaded(VTypeAir)) ? ATGetInPilot : ATGetInDriver;
      UIAction action(new ActionBasic(type, this));
      int n = Type()->NDriverGetInPos();
      for (int i=0; i<n; i++)
        action._memoryPoints.Add(Type()->GetDriverGetInPos(i)._pos);
      action.highlight = person->IsActionHighlighted(type, this);
      add(action);
    }
    GetGetInTurretActionsLeader func(add, person);
    ForEachTurret(func);

    // check the cargo (create a single action only)
    if (now)
    {
      for (int i=0; i<_manCargo.Size(); i++)
      {
        ProtectedVisualState<const Person::VisualState> pvs = person->RenderVisualStateScope(true);
#if _HELISIM_PRECISE_INTERACTION
        if (QCanIGetInCargo(person, i) != i) continue;
        //action description
        const UIActionTypeDesc &desc = UIActions::actionTypes[ATGetInCargo];

        // check if near
        if(
          (Type()->UsePreciseGetInAction() && CheckCargoGetInPositionPrecise(i, person,desc.radius, desc.radiusView))
          || (!Type()->UsePreciseGetInAction()  && CheckCargoGetInPosition(i, person, maxDist2, dist2, strict))
          )
        {
          UIAction action(new ActionIndex(ATGetInCargo, this, i));
          if(Type()->UsePreciseGetInAction()) 
          {
            action.position = GetCargoGetInPosPrecise(i, person, pvs->Position())._pos;
          }
          else
          {
            action.position = GetCargoGetInPos(i, person, pvs->Position())._pos;
            action.priority += -0.3 * dist2 * invMaxDist2;
          }
          action.highlight = person->IsActionHighlighted(ATGetInCargo, this);
          //! set radiusView < 0 to skip it in CheckActionsRadiusView
          action.radiusView = -1;

          add(action);
          break;
        }
#else
        if (QCanIGetInCargo(person, i) != i) continue;
        // check if near
        ProtectedVisualState<const VisualState> rvs = RenderVisualStateScope(true);

        UIAction action(new ActionIndex(ATGetInCargo, this, i));
        int n = Type()->NCargoGetInPos(i);
        for (int j=0; j<n; j++)
          action._memoryPoints.Add(Type()->GetCargoGetInPos(i, j)._pos);
        action.highlight = person->IsActionHighlighted(ATGetInCargo, this);
        add(action);
        break;
#endif
      }
    }
    else // !now
    {
      int cargoIndex = QCanIGetInCargo(person);
      if (cargoIndex >= 0)
      {
        UIAction action(new ActionIndex(ATGetInCargo, this, cargoIndex));
        action.highlight = person->IsActionHighlighted(ATGetInCargo, this);
#if _HELISIM_PRECISE_INTERACTION
        if (!Type()->UsePreciseGetInAction()) action.radiusView = -1;
#endif
        add(action);
      }
    }
  }
}

bool Transport::QCanIGetIn( Person *who ) const
{
#if _VBS3 && _EXT_CTRL
  if( IsExternalControlled() && !AllowExternalControl(EC_AAR))
    return false;
#endif

  if (_driverLocked) return false;
  if (!QCanIBeIn(who)) return false;
  if (!Type()->HasDriver()) return false;

  // check whether driver animation is available
  if (who)
  {
#if _PROFILE && (USER_Ondra || USER_ondra)
#else
      if (!who->IsAction(DriverAction())) return false;
#endif
  }

  return _driver==NULL || _driver->IsDamageDestroyed();
}

bool Transport::QCanIGetInGunner( Person *who ) const
{
  TurretContext context;
  if (!GetPrimaryGunnerTurret(context)) return false;
  if (!context._turret) return false;
  return QCanIGetInTurret(context._turret, who);
}

bool Transport::QCanIGetInCommander( Person *who ) const
{
  TurretContext context;
  if (!GetPrimaryObserverTurret(context)) return false;
  if (!context._turret) return false;
  return QCanIGetInTurret(context._turret, who);
}

bool Transport::QCanIGetInTurret(Turret *turret, Person *who) const
{
#if _VBS3 && _EXT_CTRL
  if( turret->IsExternalControlled() && !turret->AllowExternalControl(EC_AAR))
    return false;
#endif

  if (!turret) return false;
  if (turret->IsGunnerLocked()) return false;

  if (!QCanIBeIn(who)) return false;

  // check whether gunner animation is available
  if (who)
  {
	  TurretContextV context;
    if (FindTurret(unconst_cast(FutureVisualState()), turret, context))
    {
      if (!who->IsAction(turret->GunnerAction(*context._turretVisualState, *context._turretType))) return false;
    }
  }

  return turret->_gunner == NULL || turret->_gunner->IsDamageDestroyed();
}

int Transport::QCanIGetInCargo(Person *who, int index) const
{
  if (!QCanIBeIn(who)) return -1;

  int n = _manCargo.Size();
  // check the given position
  if (index >= 0 && index < n && (!_manCargo[index] || _manCargo[index]->IsDamageDestroyed()))
  {
    if (IsCargoLocked(index)) index = -1;
    // check whether cargo animation is available
    else if (who)
    {
      if (!who->IsAction(CargoAction(index))) index = -1;
    }
    if (index >= 0) return index; // correct position
  }

  // find the position
  // first check for empty cargo positions
  for (int i=0; i<n; i++)
  {
    if (_manCargo[i]) continue; // occupied
    if (IsCargoLocked(i)) continue;
    if (who && !who->IsAction(CargoAction(i))) continue; // cargo animation not available
    return i;
  }
  // if not found, search who to replace
  for (int i=0; i<n; i++)
  {
    if (!_manCargo[i]) continue; // already checked
    if (!_manCargo[i]->IsDamageDestroyed()) continue; // occupied by alive
    if (IsCargoLocked(i)) continue;
    if (who && !who->IsAction(CargoAction(i))) continue; // cargo animation not available
    return i;
  }
  return -1; // not found
}

/// Functor checking if person can get in turrets
class CanGetInSomeTurret : public ITurretFunc
{
protected:
  Person *_who;
  bool &_free;

public:
  CanGetInSomeTurret(Person *who, bool &free) : _who(who), _free(free) {}

  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turret) return false; // continue
    Transport *veh = static_cast<Transport *>(entity); // safe here, we know how we called it (+ turret exist)
    return veh->QCanIGetInTurret(context._turret, _who); // end when some position found
  }
};

bool Transport::QCanIGetInAny( Person *who ) const
{
  if (QCanIGetIn(who)) return true;
  if (QCanIGetInCargo(who) >= 0) return true;
  bool free = false;
  CanGetInSomeTurret func(who, free);
  return ForEachTurret(func);
}

void Transport::LandStarted(LandingMode landing)
{
  AIBrain *brain = PilotUnit();
  if (!brain) return;
  _landing = landing;
  if (landing==LMNone)
  {
    // if there is some stopping in progress, cancel it
    if( brain->GetState()== AIBrain::Stopping)
    {
      brain->OnStepTimedOut();
    }
  }
  else
  {
    // initiating stopping
    if (brain->GetState() != AIBrain::Stopping && brain->GetState() != AIBrain::Stopped)
    {
      Verify(brain->SetState(AIBrain::Stopping));
    }
  }
}

bool Transport::IsStopped() const
{
  AIBrain *unitInto = PilotUnit();
  if (!unitInto ) return true;
  if( !unitInto->IsAnyPlayer() ) //FIX: IsPlayer() condition was not sufficient in MP
  {
    // Stopped may timeout - test for Stopping
    if( unitInto->GetState()== AIBrain::Stopping) return false;
    if( unitInto->GetState()== AIBrain::Stopped) return true;
  }
  return FutureVisualState().Speed().SquareSize()<Square(0.5);
}

bool Transport::CanCancelStop() const
{
  if (_landing != LMNone) return false;
  if (!base::CanCancelStop()) return false;
  return true;
}

Vector3 Transport::GetStopPosition() const
{
  // predict stop position based on current speed
  float estT = 2.0f;
  return FutureVisualState().Position() + FutureVisualState().Speed() * estT + 0.5 * Square(estT) * FutureVisualState().Acceleration();
}

/*!
\patch_internal 1.27 Date 10/17/2001 by Ondra
- Changed: ResetMovement moved after SetVehicleIn.
Required because new testing of IsInLandscape in ResetMovement.
\patch 1.27 Date 10/17/2001 by Ondra
- Fixed: speed of vehicle not kept after jumping out (ejecting) from vehicle.
\patch 1.82 Date 8/9/2002 by Ondra
- Fixed: MP: Problems with get-in very quickly followed by get-out.
\patch 5089 Date 11/23/2006 by Ondra
- Fixed: AI reaction improved for enemies disembarking vehicles.
\patch 5101 Date 12/12/2006 by Ondra
- New: Headlights are now not switched when player leaves the vehicle.
*/
void Transport::GetOutAny(const Matrix4 &outPos, Person *soldier, bool sound, const char *name, RString getOutAction)
{
  Assert( !soldier->ToDelete() );

  Assert( soldier );
  //soldier->ForgetMove();

  Assert (GWorld->ValidateOutVehicle(soldier));
  DoAssert (ValidateCrew(soldier));

  soldier->VisualCut();
  
  if (soldier->IsInLandscape())
    RptF("GetOutAny soldier %s already in landscape", (const char *)soldier->GetDebugName());
#if 0
  LogF("%s moved from %s into landscape", (const char *)soldier->GetDebugName(), (const char *)GetDebugName());
#endif
  if (soldier->IsMoveOutInProgress())
  {
    RptF("%s: Getting out while IsMoveOutInProgress", (const char *)soldier->GetDebugName());
    soldier->CancelMoveOutInProgress();
    soldier->Move(outPos);
    soldier->Init(outPos, false); // do not reset movement
  }
  else
  {
    soldier->SetTransform(outPos);
    soldier->Init(outPos, false); // do not reset movement
    soldier->ResetMoveOut(); // mark it is in landscape
    GLOB_WORLD->AddVehicle(soldier);
    GLOB_WORLD->RemoveOutVehicle(soldier);
  }

  // we need other entities nearby to be notified of our presence
  GWorld->OnEntityMovedFar(soldier);

  // there was lock position here, but there is no need to connect locking with getting out
  //LockPosition();

  AIBrain *brain = soldier->Brain();
  if( brain )
  {
    // AI switches the light off when leaving the vehicle
    if (CommanderUnit()==brain && !brain->IsAnyPlayer())
    {
#if _VBS3 && _EXT_CTRL
      if(!GAAR.IsPlaying())
#endif
        if ( _pilotLight )
        {
          _pilotLight = false;
          if ( !IsLocal() )
          {
            GetNetworkManager().AskForPilotLight(this, _pilotLight);
          }
        }
        _collisionLight = false;
    }
    brain->SetVehicleIn(NULL);
  }
  soldier->ResetMovement(2.5, getOutAction);
  soldier->SetSpeed(FutureVisualState()._speed);

  if (QIsManual())
    DecFadeSemaphore(FadeGetOut);
  // we want to give near units chance of noticing us

  if (!_doorSound && sound)
  {
    const SoundPars &pars=GetType()->GetGetOutSound();

    if (pars.name.GetLength() > 0 && GSoundScene->CanBeAudible(WaveEffect,FutureVisualState().Position(),pars.vol))
    {
      _doorSound=GSoundScene->OpenAndPlayOnce(pars.name,this,true,FutureVisualState().Position(),FutureVisualState().Speed(),pars.vol, pars.freq, pars.distance);
      if( _doorSound )
      {
        GSoundScene->SimulateSpeedOfSound(_doorSound);
        GSoundScene->AddSound(_doorSound);
      }
    }

  }
  if (sound) OnEvent(EEGetOut,name,soldier);
  /*
  if (!CommanderUnit() || CommanderUnit()->IsInCargo())
  {
  // empty vehicle - leave light status as it is
  // _pilotLight = false;
  }
  */

  if (_effCommander == soldier) _effCommander = NULL;
}


void Transport::GetInAny(Person *soldier, bool sound, const char *name)
{
  // check if soldier is in some vehicle
  // if yes, get him out before gettin in "this"
  AIBrain *unit=soldier->Brain();
  if (unit)
  {
    Transport *in = unit->GetVehicleIn();
    Assert (in!=this);
    if (in)
    {
      Assert(IsLocal());
      unit->DoGetOut(in,false,false);
#if 0
      LogF
        (
        "Unit %s forced out from %s",
        (const char *)unit->GetDebugName(),
        (const char *)GetDebugName()
        );
#endif
    }
  }

  soldier->UnlockPosition();
  if (soldier->IsInLandscape())
  {
    soldier->SetMoveOut(this); // remove reference from the world
  }
  else
  {
    Assert(!soldier->IsMoveOutInProgress());
    Assert(soldier->GetHierarchyParent()==this);
  }
#if 0
  LogF
    (
    "%s will move into %s",
    (const char *)soldier->GetDebugName(),
    (const char *)GetDebugName()
    );
#endif
  soldier->SetPilotLight(false);
  soldier->SetCollisionLight(false);
  soldier->SwitchLight(false);
  if (unit)
  {
    unit->SetVehicleIn(this);
  }
  soldier->OnGetInFinished();

  if (!_doorSound && sound)
  {
    const SoundPars &pars=GetType()->GetGetInSound();
    if (pars.name.GetLength() > 0 && GSoundScene->CanBeAudible(WaveEffect,FutureVisualState().Position(),pars.vol))
    {
      _doorSound=GSoundScene->OpenAndPlayOnce
        (
        pars.name,this,true,FutureVisualState().Position(),FutureVisualState().Speed(),pars.vol, pars.freq, pars.distance
        );
      if( _doorSound )
      {
        GSoundScene->SimulateSpeedOfSound(_doorSound);
        GSoundScene->AddSound(_doorSound);
      }
    }
  }
  if (sound) OnEvent(EEGetIn,name,soldier);

  GWorld->OnEntityGetIn(soldier,this);
  soldier->OnMovedFromLandscape(); //fix: soldier picked up from water and dropped out on land was still swimming
}

void Transport::PreloadGetIn(UIActionType position)
{
  LODShape *lShape = GetShape();
  if (lShape)
  {
    int level = InternalLOD(position);
    if (level >= 0)
    {
      bool loaded = lShape->CheckLevelLoaded(level,true);
      if (loaded)
      {
        // we may preload textures as well
        ShapeUsed shape = lShape->Level(level);
        shape->PreloadTextures(0, NULL);
      }
    }
  }

  // UI textures
  // - compass
  int canSee = 0;
  switch (position)
  {
  case ATGetInTurret:
    canSee = Type()->_gunnerCanSee; break;
  case ATGetInDriver:
  case ATGetInPilot:
    canSee = Type()->_driverCanSee; break;
  }
  if (canSee & CanSeeCompass)
  {
    PreloadedTexture seg[4] = {Compass180, Compass270, Compass000, Compass090};
    for (int i=0; i<4; i++)
    {
      Texture *texture = GScene->Preloaded(seg[i]);
      if (!texture) continue;
      if (!texture->HeadersReady()) continue;
      GEngine->TextBank()->UseMipmap(texture, 1, 0);
    }
  }
}

bool Transport::PrepareGetIn(UIActionType pos, Turret *turret, int cargoIndex)
{
  if (turret)
  {
    TurretContext context;
    FindTurret(turret,context);
    if (context._turretType)
    {
      int doorIndex = context._turretType->_gunnerDoor;
      if (doorIndex >=0 )
      {
        SetDoorOpen(doorIndex,true);
        return FutureVisualState().GetDoorState(doorIndex) >0.99f;
      }      
    }
  }
  else
  {
    switch (pos)
    {
      case ATGetInCargo:
        int doorIndex;

        if (Type()->_cargoDoors.Size()==0) doorIndex =-1; 
        else if (cargoIndex >= Type()->_cargoDoors.Size()) doorIndex = Type()->_cargoDoors[Type()->_cargoDoors.Size()-1]; // last item
        else doorIndex = Type()->_cargoDoors[cargoIndex];

        if (doorIndex >=0 )
        {
          SetDoorOpen(doorIndex,true);
          return FutureVisualState().GetDoorState(doorIndex) >0.99f;
        }   
      break;
      default:
        if (Type()->_driverDoors >=0 )
        {
          SetDoorOpen(Type()->_driverDoors,true);
          return FutureVisualState().GetDoorState(Type()->_driverDoors)> 0.99f;
        }
      break;
    }
  }
  return true;
}

bool Transport::PrepareGetOut(AIBrain *unit)
{
  Person *person = unit->GetPerson();
  if (person == Driver())
  {
    if (Type()->_driverDoors<0) return true;  
    SetDoorOpen(Type()->_driverDoors,true);
    return FutureVisualState().GetDoorState(Type()->_driverDoors) >0.99f;
  }
  else 
  {
    TurretContext context;
    if (FindTurret(person, context))
    {
      int doorIndex = context._turretType->_gunnerDoor;
      if (doorIndex<0) return true;  

      SetDoorOpen(doorIndex,true);
      return FutureVisualState().GetDoorState(doorIndex) >0.99f;
    }
    else
    {
      // search in cargo
      for (int i=0; i<_manCargo.Size(); i++)
      {
        if (person == _manCargo[i])
        {
          int doorIndex;

          if (Type()->_cargoDoors.Size()==0) doorIndex =-1; 
          else if (i >= Type()->_cargoDoors.Size()) doorIndex = Type()->_cargoDoors[Type()->_cargoDoors.Size()-1]; // last item
          else doorIndex = Type()->_cargoDoors[i];

          if (doorIndex<0) return true;

          SetDoorOpen(doorIndex,true);
          return FutureVisualState().GetDoorState(doorIndex) >0.99f;
        }
      }
    }
  }
  return true;
}

void Transport::CloseDoor(AIBrain *unit)
{
  if (unit->GetPerson() == Driver())
  {
    if (Type()->_driverDoors>=0)
      SetDoorOpen(Type()->_driverDoors,false);
  }
  else 
  {
    TurretContext context;
    if (FindTurret(unit->GetPerson(), context))
    {
      int doorIndex = context._turretType->_gunnerDoor;
      if (doorIndex >=0 )
      {
        SetDoorOpen(doorIndex,false);
      }     
    }
    else
    {
      // search in cargo
      for (int i=0; i<_manCargo.Size(); i++)
      {
        if (unit->GetPerson() == _manCargo[i])
        {
          int doorIndex;

          if (Type()->_cargoDoors.Size()==0) doorIndex =-1; 
          else if (i >= Type()->_cargoDoors.Size()) doorIndex = Type()->_cargoDoors[Type()->_cargoDoors.Size()-1]; // last item
          else doorIndex = Type()->_cargoDoors[i];

          if (doorIndex >=0 )
          {
            SetDoorOpen(doorIndex,false);
          }   
          break;
        }
      }
    }
  }
}

void Transport::OpenDoor(AIBrain *unit)
{
  if (unit->GetPerson() == Driver())
  {
    if (Type()->_driverDoors>=0)
      SetDoorOpen(Type()->_driverDoors,true);
  }
  else 
  {
    TurretContext context;
    if (FindTurret(unit->GetPerson(), context))
    {
      int doorIndex = context._turretType->_gunnerDoor;
      if (doorIndex >=0 )
      {
        SetDoorOpen(doorIndex,true);
      }     
    }
    else
    {
      // search in cargo
      for (int i=0; i<_manCargo.Size(); i++)
      {
        if (unit->GetPerson() == _manCargo[i])
        {
          int doorIndex;

          if (Type()->_cargoDoors.Size()==0) doorIndex =-1; 
          else if (i >= Type()->_cargoDoors.Size()) doorIndex = Type()->_cargoDoors[Type()->_cargoDoors.Size()-1]; // last item
          else doorIndex = Type()->_cargoDoors[i];

          if (doorIndex >=0 )
          {
            SetDoorOpen(doorIndex,true);
          }   
          break;
        }
      }
    }
  }
}

void Transport::SetDoorOpen( int index, bool val )
{
  if (IsLocal()) {
    FutureVisualState().SetDoorOpen(index, val);
  }
  else {
    FutureVisualState().SetDoorOpen(index, val);  // don't wait for the roundup trip, set it right away
    GetNetworkManager().AskForVehicleDoorState(this, index, val);
  }
}

bool Transport::CanBeCommander(AIBrain *unit) const
{
  // when player can become commander if get in to driver / gunner / cargo position
  if (_effCommander && _effCommander->Brain() && _effCommander->Brain()->IsAnyPlayer()) return false; // commander position occupied by other player
  return true;
}

/// Functor searching if person with the better commanding level than given is in vehicle
class IsBetterCommander : public ITurretFunc
{
protected:
  float _limit;

public:
  IsBetterCommander(float limit) : _limit(limit) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (!context._turretType) return false; // continue
    if (!context._gunner) return false; // continue
    if (!context._gunner->Brain()) return false; // continue
    return context._turretType->_commanding > _limit;
  }
};

void Transport::ControlTakenBy(Person *man)
{
  // suppresive fire needs to be explicitely enabled by the FSM
  _useSuppressiveFire = SuppressNo;
}

void Transport::GetInDriver(Person *driver, bool sound, bool isFastMoveIn)
{
  if (_driver)
  {
    DoAssert(_driver->IsDamageDestroyed());

    GetInPoint pt = AnimateGetInPoint(Type()->GetDriverGetInPos(0));
    Matrix4 transform;
    FutureVisualState().PositionModelToWorld(pt._pos, pt._pos);
    FutureVisualState().DirectionModelToWorld(pt._dir,pt._dir);
    transform.SetPosition(pt._pos);
    transform.SetUpAndDirection(VUp, -pt._dir);

    GetOutDriver(transform, true, false);
  }

  _driver=driver;
  if( driver==GLOB_WORLD->CameraOn() )
  {
    GLOB_WORLD->SwitchCameraTo(this,GLOB_WORLD->GetCameraType(), false);
  }
  /*
  EngineOn();
  */
  GetInAny(driver,sound,"driver");
  _driverHiddenWanted = FutureVisualState()._driverHidden = Type()->_hideProxyInCombat;
  if (Type()->PreciseGetInOut()!=GETIN_OLD && !isFastMoveIn)
  {
    driver->PreciseGetInAction(Type()->GetDriverGetInAction(),DriverAction(),Type()->_driverDoors);
  }
  else
  {
    driver->SwitchAction(DriverAction());
  }
  // driver is changing, cancel current command
  _moveMode = VMMFormation;

  // if driver is commander, _effCommander is never set (vehicle does not work with different commander than driver)
  if (!Type()->DriverIsCommander())
  {
    AIBrain *unit = _effCommander ? _effCommander->Brain() : NULL;
    if (unit && unit->IsAnyPlayer() && !unit->IsInCargo())
    {
      // do not change effective commander
    }
    else
    {
      // driver will sometimes become effective commander
      AIBrain *unit = driver->Brain();
      if (unit && unit->IsAnyPlayer() && CanBeCommander(unit))
      {
        _effCommander = driver;
      }
      else
      {
        IsBetterCommander func(0);
        if (!ForEachTurret(func))
        {
          _effCommander = driver;
        }
      }
    }
  }

  if (driver->Brain()==CommanderUnit())
  {
    ControlTakenBy(driver);
  }
}

int Transport::GetManCargoSize() const
{
  int n = 0;
  for (int i=0; i<_manCargo.Size(); i++)
  {
    if (_manCargo[i]) n++;
  }
  return n;
}

int Transport::GetFreeManCargo() const
{
  return Type()->_maxManCargo - GetManCargoSize();
}

/*!
\patch 5137 Date 3/1/2007 by Ondra
- New: Player can use Eject to disembark the vehicle while leaving engine on.
*/
void Transport::GetOutDriver(const Matrix4 &outPos, bool eject, bool sound)
{
  if( !_driver ) return;

  if (Type()->PreciseGetInOut()!=GETIN_OLD)
    GetOutAny(outPos, _driver, sound, "driver",RString());
  else
    GetOutAny(outPos, _driver, sound, "driver", Type()->GetDriverGetOutAction());

  if (!eject)
  {
    // use EngineOffAction to make sure it switches off even for helicopters
    EngineOffAction();
  }
  GWorld->VehicleSwitched(this,_driver);
  if (_driver->Brain() == CommanderUnit())
    _manualFire = false;
  _driver=NULL;

}

bool Transport::CheckCrewVulnerable() const
{
  // by default assume there is no crew or it is nor vulnerable to small arms fire
  // if crew cannot turn out, assume it is not vulnerable
  if (!Type()->_hideProxyInCombat)
  {
    // check if crew is sitting always out
    return Type()->_crewVulnerable;
  }
  // check if some crew is currently turned out?
  return true;
}

void Transport::GetInCargo(Person *soldier, bool sound, int index, bool isFastMoveIn)
{
  // if position is not specified, search for first free one
  if (index<0)
  {
    for (int i=0; i<_manCargo.Size(); i++)
    {
      if (_manCargo[i] == NULL || _manCargo[i]->IsDamageDestroyed())
      {
        index = i;
        break;
      }
    }
    if (index < 0)
    {
      Fail("No free position in cargo");
      return;
    }
  }

  if (_manCargo[index])
  {
    GetInPoint pt = AnimateGetInPoint(Type()->GetCargoGetInPos(index, 0));
    Matrix4 transform;
    FutureVisualState().PositionModelToWorld(pt._pos, pt._pos);
    FutureVisualState().DirectionModelToWorld(pt._dir,pt._dir);
    transform.SetPosition(pt._pos);
    transform.SetUpAndDirection(VUp, -pt._dir);

    GetOutCargo(_manCargo[index], transform, false);
  }

  _manCargo.Set(index).man = soldier;
  GetInAny(soldier,sound,"cargo");

  if (Type()->CargoPreciseGetInOut(index)!=GETIN_OLD && !isFastMoveIn)
  {
    int doorIndex;

    if (Type()->_cargoDoors.Size()==0) doorIndex =-1; 
    else if (index >= Type()->_cargoDoors.Size()) doorIndex = Type()->_cargoDoors[Type()->_cargoDoors.Size()-1]; // last item
    else doorIndex = Type()->_cargoDoors[index];

    soldier->PreciseGetInAction(Type()->GetCargoGetInAction(index),CargoAction(index),doorIndex); 
  }
  else
  {
    soldier->SwitchAction(CargoAction(index));
  }
  GWorld->VehicleSwitched(soldier,this);

  // if driver is commander, _effCommander is never set (vehicle does not work with different commander than driver)
  if (!Type()->DriverIsCommander())
  {
    AIBrain *unit = _effCommander ? _effCommander->Brain() : NULL;
    if (unit && unit->IsAnyPlayer())
    {
      // do not change effective commander
    }
    else
    {
      // we become effective commander
      AIBrain *unit = soldier->Brain();
      if (unit && unit->IsAnyPlayer() && CanBeCommander(unit))
      {
        _effCommander = soldier;
      }
      else if (!_driver)
      {
        // check for any crew
        IsBetterCommander func(-FLT_MAX);
        if (!ForEachTurret(func))
        {
          _effCommander = soldier;
        }
      }
    }
  }

  if (soldier->Brain()==CommanderUnit())
  {
    ControlTakenBy(soldier);
  }

}

void Transport::GetOutCargo(Person *cargo, const Matrix4 &outPos, bool sound)
{
  int pos = -1;
  for (int i=0; i<_manCargo.Size(); i++)
  {
    if (_manCargo[i] == cargo)
    {
      pos = i;
      break;
    }
  }
  if (pos < 0)
  {
    Fail("Unit is not in cargo");
    return;
  }

  if (Type()->CargoPreciseGetInOut(pos)!=GETIN_OLD)
    GetOutAny(outPos, cargo, sound, "cargo", RString());
  else
    GetOutAny(outPos, cargo, sound, "cargo", Type()->GetCargoGetOutAction(pos));

  _manCargo.Set(pos).man = NULL;

}

void Transport::GetInGunner(Person *soldier, bool sound, bool isFastMoveIn)
{
  // check if gunner position exist
  TurretContextV context;
  if (!GetPrimaryGunnerTurret(unconst_cast(FutureVisualState()), context)) return;
  if (!context._turret) return;

  Person *gunner = context._turret->Gunner();

  if (gunner)
  {
    DoAssert(gunner->IsDamageDestroyed());

    GetInPoint pt = AnimateGetInPoint(context._turretType->GetGunnerGetInPos(0));
    Matrix4 transform;
    FutureVisualState().PositionModelToWorld(pt._pos, pt._pos);
    FutureVisualState().DirectionModelToWorld(pt._dir,pt._dir);
    transform.SetPosition(pt._pos);
    transform.SetUpAndDirection(VUp, -pt._dir);

    GetOutGunner(transform, false);
  }

  context._turret->SetGunner(soldier);

  float hidden = Type()->_hideProxyInCombat && context._turretType->_canHideGunner ? 1.0f : 0.0f;
  context._turretVisualState->_crewHidden = hidden;
  context._turret->_crewHiddenWanted = hidden;
  RString action = context._turret->GunnerAction(*context._turretVisualState, *context._turretType);
  GetInAny(soldier, sound, "gunner");


  const TurretType *tType =  context._turretType;
  if (tType->GunnerPreciseGetInOut()!=GETIN_OLD && !isFastMoveIn)
  {
    RString actionAfter = context._turret->GunnerAction(*context._turretVisualState, *context._turretType);
    soldier->PreciseGetInAction(tType->GetGunnerGetInAction(),actionAfter,tType->_gunnerDoor); 
  }
  else
  {
    soldier->SwitchAction(action);
  }
  GWorld->VehicleSwitched(soldier, this);

  // if driver is commander, _effCommander is never set (vehicle does not work with different commander than driver)
  if (!Type()->DriverIsCommander())
  {
    AIBrain *unit = _effCommander ? _effCommander->Brain() : NULL;
    if (unit && unit->IsAnyPlayer() && !unit->IsInCargo())
    {
      // do not change effective commander
    }
    else
    {
      // gunner will sometimes become effective commander
      AIBrain *unit = soldier->Brain();
      if (unit && unit->IsAnyPlayer() && CanBeCommander(unit))
      {
        _effCommander = soldier;
      }
      else
      {
        if (!_driver || context._turretType->_commanding > 0)
        {
          IsBetterCommander func(context._turretType->_commanding);
          if (!ForEachTurret(func))
          {
            _effCommander = soldier;
          }
        }
      }
    }
  }

  if (soldier->Brain()==CommanderUnit())
  {
    ControlTakenBy(soldier);
  }

  // disable manual fire in MP when player gunner gets in
  if (GunnerUnit() && GunnerUnit()->IsAnyPlayer()) _manualFire = false;

}

void Transport::GetOutGunner(const Matrix4 &outPos, bool sound)
{
  TurretContext context;
  if (!GetPrimaryGunnerTurret(context)) return;
  if (!context._turret) return;
  Person *gunner = context._turret->Gunner();

  if (!gunner) return;

  if (context._turretType->GunnerPreciseGetInOut()!=GETIN_OLD)
    GetOutAny(outPos, gunner, sound, "gunner", RString());
  else
    GetOutAny(outPos, gunner, sound, "gunner", context._turretType->GetGunnerGetOutAction());

  context._turret->SetGunner(NULL);

}

/*!
\patch 1.28 Date 10/30/2001 by Ondra
- Fixed: MP character animation in cargo (looking through roof).
*/

void Transport::GetInCommander(Person *soldier, bool sound, bool isFastMoveIn)
{
  // check if observer position exist
  TurretContextV context;
  if (!GetPrimaryObserverTurret(unconst_cast(FutureVisualState()), context)) return;
  if (!context._turret) return;

  Person *commander = context._turret->Gunner();

  if (commander)
  {
    DoAssert(commander->IsDamageDestroyed());

    GetInPoint pt = AnimateGetInPoint(context._turretType->GetGunnerGetInPos(0));
    Matrix4 transform;
    FutureVisualState().PositionModelToWorld(pt._pos, pt._pos);
    FutureVisualState().DirectionModelToWorld(pt._dir,pt._dir);
    transform.SetPosition(pt._pos);
    transform.SetUpAndDirection(VUp, -pt._dir);

    GetOutCommander(transform, false);
  }

  context._turret->SetGunner(soldier);

  float hidden = Type()->_hideProxyInCombat && context._turretType->_canHideGunner ? 1.0f : 0.0f;
  context._turretVisualState->_crewHidden = hidden;
  context._turret->_crewHiddenWanted = hidden;
  RString action = context._turret->GunnerAction(*context._turretVisualState, *context._turretType);
  GetInAny(soldier,sound,"commander");

  const TurretType *tType =  context._turretType;
  if (tType->GunnerPreciseGetInOut()!=GETIN_OLD && !isFastMoveIn)
  {
    RString actionAfter = context._turret->GunnerAction(*context._turretVisualState, *context._turretType);
    soldier->PreciseGetInAction(tType->GetGunnerGetInAction(),actionAfter,tType->_gunnerDoor); 
  }
  else
  {
    soldier->SwitchAction(action);
  }
  GWorld->VehicleSwitched(soldier,this);

  // if driver is commander, _effCommander is never set (vehicle does not work with different commander than driver)
  if (!Type()->DriverIsCommander())
  {
    AIBrain *unit = _effCommander ? _effCommander->Brain() : NULL;
    // any player will become effective commander
    // if AI is getting in and effective commander is a group leader, let him be
    if (unit && unit->IsAnyPlayer() && !unit->IsInCargo())
    {
      // do not change effective commander
    }
    else
    {
      // observer will sometimes become effective commander
      AIBrain *unit = soldier->Brain();
      if (unit && unit->IsAnyPlayer() && CanBeCommander(unit))
      {
        _effCommander = soldier;
      }
      else
      {
        if (!_driver || context._turretType->_commanding > 0)
        {
          IsBetterCommander func(context._turretType->_commanding);
          if (!ForEachTurret(func))
          {
            _effCommander = soldier;
          }
        }
      }
    }
  }

  if (soldier->Brain() == CommanderUnit())
  {
    ControlTakenBy(soldier);
  }

}

void Transport::GetOutCommander(const Matrix4 &outPos, bool sound)
{
  TurretContext context;
  if (!GetPrimaryObserverTurret(context)) return;
  if (!context._turret) return;
  Person *commander = context._turret->Gunner();

  if (!commander) return;

  if (context._turretType->GunnerPreciseGetInOut()!=GETIN_OLD)
    GetOutAny(outPos, commander, sound, "commander", RString());
  else
    GetOutAny(outPos, commander, sound, "commander", context._turretType->GetGunnerGetOutAction());

  if (commander->Brain() == CommanderUnit())
    _manualFire = false;

  context._turret->SetGunner(NULL);

}

void Transport::GetInTurret(Turret *turret, Person *soldier, bool sound, bool isFastMoveIn)
{
  // need the turret type
  TurretContextV context;
  if (!FindTurret(unconst_cast(FutureVisualState()), turret, context)) return;
  if (!context._turret) return;

  Person *gunner = context._turret->Gunner();

  if (gunner)
  {
    DoAssert(gunner->IsDamageDestroyed());

    GetInPoint pt = AnimateGetInPoint(context._turretType->GetGunnerGetInPos(0));
    Matrix4 transform;
    FutureVisualState().PositionModelToWorld(pt._pos, pt._pos);
    FutureVisualState().DirectionModelToWorld(pt._dir,pt._dir);
    transform.SetPosition(pt._pos);
    transform.SetUpAndDirection(VUp, -pt._dir);

    GetOutTurret(context, transform, false);
  }

  context._turret->SetGunner(soldier);

  float hidden = Type()->_hideProxyInCombat && context._turretType->_canHideGunner ? 1.0f : 0.0f;
  context._turretVisualState->_crewHidden = hidden;
  context._turret->_crewHiddenWanted = hidden;
  RString action = context._turret->GunnerAction(*context._turretVisualState, *context._turretType);
  GetInAny(soldier, sound, "gunner");

  const TurretType *tType =  context._turretType;
  if (tType->GunnerPreciseGetInOut()!=GETIN_OLD &&!isFastMoveIn)
  {
    RString actionAfter = context._turret->GunnerAction(*context._turretVisualState, *context._turretType);
    soldier->PreciseGetInAction(tType->GetGunnerGetInAction(),actionAfter,tType->_gunnerDoor); 
  }
  else
  {
    soldier->SwitchAction(action);
  }
  GWorld->VehicleSwitched(soldier, this);

  // if driver is commander, _effCommander is never set (vehicle does not work with different commander than driver)
  if (!Type()->DriverIsCommander())
  {
    AIBrain *unit = _effCommander ? _effCommander->Brain() : NULL;
    // any player will become effective commander
    // if AI is getting in and effective commander is a group leader, let him be
    if (unit && unit->IsAnyPlayer() && !unit->IsInCargo())
    {
      // do not change effective commander
    }
    else
    {
      // gunner will sometimes become effective commander
      AIBrain *unit = soldier->Brain();
      if (unit && unit->IsAnyPlayer() && CanBeCommander(unit))
      {
        _effCommander = soldier;
      }
      else
      {
        if (!_driver || context._turretType->_commanding > 0)
        {
          IsBetterCommander func(context._turretType->_commanding);
          if (!ForEachTurret(func))
          {
            _effCommander = soldier;
          }
        }
      }
    }
  }

  if (soldier->Brain()==CommanderUnit())
  {
    ControlTakenBy(soldier);
  }

  // disable manual fire in MP when player gunner gets in
  if (context._turretType && context._turretType->_primaryGunner && GunnerUnit() && GunnerUnit()->IsAnyPlayer()) _manualFire = false;

}

void Transport::GetOutTurret(TurretContext &context, const Matrix4 &outPos, bool sound)
{
  if (!context._turret) return;

  Person *gunner = context._gunner;
  if (!gunner) return;

  if (context._turretType->GunnerPreciseGetInOut()!=GETIN_OLD)
    GetOutAny(outPos, gunner, sound, "gunner", RString());
  else
    GetOutAny(outPos, gunner, sound, "gunner", context._turretType->GetGunnerGetOutAction());

  if (gunner->Brain() == CommanderUnit())
    _manualFire = false;

  context._turret->SetGunner(NULL);

}

void Transport::Eject(AIBrain *unit, Vector3Val diff)
{
  // TODO: create parachute if necessary
  unit->ProcessGetOut(true, false, diff);
}

void Transport::Land()
{
}

void Transport::CancelLand()
{
}

GetInPoint Transport::GetUnitGetOutPos(AIBrain *unit) const
{
  Assert(unit);
  Person *person = unit->GetPerson();
  if (Driver() == person)
    return GetDriverGetOutPos(person);
  else
  {
    TurretContext context;
    if (FindTurret(person, context) && context._turret)
      return GetTurretGetOutPos(context._turret, person);
    else
      return GetCargoGetOutPos(person);
  }
}

GetInPoint Transport::GetDefaultGetInPos(Person *person, Vector3Par pos) const
{
  GetInPoint best;
  best._dir = pos-RenderVisualState().Position();
  if (best._dir.SquareSize()>Square(0.1f))
  {
    best._dir.Normalize();
  }
  else
  {
    best._dir = VForward;
  }
  best._pos = pos;
  return best;
}

GetInPoint Transport::GetDriverGetInPos(Person *person, Vector3Par pos) const
{
  Vector3Val unitPos = pos;
  GetInPoint best;
  float minDist2 = FLT_MAX;
  int n = Type()->NDriverGetInPos();
  for (int i=0; i<n; i++)
  {
    GetInPoint pt = AnimateGetInPoint(Type()->GetDriverGetInPos(i));
    RenderVisualState().PositionModelToWorld(pt._pos, pt._pos);
    float dist2 = pt._pos.Distance2(unitPos);
    if (dist2 < minDist2)
    {
      minDist2 = dist2;
      best = pt;
    }
  }
  if (minDist2<FLT_MAX)
  {
    RenderVisualState().DirectionModelToWorld(best._dir, best._dir);
    return best;
  }
  else
  {
    return GetDefaultGetInPos(person,pos);
  }
}

GetInPoint Transport::GetCommanderGetInPos(Person *person, Vector3Par pos) const
{
  Vector3Val unitPos = pos;
  GetInPoint best;
  float minDist2 = FLT_MAX;

  TurretContext context;
  if (GetPrimaryObserverTurret(context) && context._turret)
  {
    int n = context._turretType->NGunnerGetInPos();
    for (int i=0; i<n; i++)
    {
      GetInPoint pt = AnimateGetInPoint(context._turretType->GetGunnerGetInPos(i));
      RenderVisualState().PositionModelToWorld(pt._pos, pt._pos);
      float dist2 = pt._pos.Distance2(unitPos);
      if (dist2 < minDist2)
      {
        minDist2 = dist2;
        best = pt;
      }
    }
  }
  if (minDist2<FLT_MAX)
  {
    RenderVisualState().DirectionModelToWorld(best._dir, best._dir);
    return best;
  }
  else
  {
    return GetDefaultGetInPos(person,pos);
  }
}

GetInPoint Transport::GetGunnerGetInPos(Person *person, Vector3Par pos) const
{
  Vector3Val unitPos = pos;
  GetInPoint best;
  float minDist2 = FLT_MAX;
  TurretContext context;
  if (GetPrimaryGunnerTurret(context) && context._turret)
  {
    int n = context._turretType->NGunnerGetInPos();
    for (int i=0; i<n; i++)
    {
      GetInPoint pt = AnimateGetInPoint(context._turretType->GetGunnerGetInPos(i));
      RenderVisualState().PositionModelToWorld(pt._pos, pt._pos);
      float dist2 = pt._pos.Distance2(unitPos);
      if (dist2 < minDist2)
      {
        minDist2 = dist2;
        best = pt;
      }
    }
  }
  if (minDist2<FLT_MAX)
  {
    RenderVisualState().DirectionModelToWorld(best._dir, best._dir);
    return best;
  }
  else
  {
    return GetDefaultGetInPos(person,pos);
  }
}

GetInPoint Transport::GetTurretGetInPos(Turret *turret, Person *person, Vector3Par pos) const
{
  Vector3Val unitPos = pos;
  GetInPoint best;
  float minDist2 = FLT_MAX;
  TurretContext context;
  if (FindTurret(turret, context) && context._turret)
  {
    int n = context._turretType->NGunnerGetInPos();
    for (int i=0; i<n; i++)
    {
      GetInPoint pt = AnimateGetInPoint(context._turretType->GetGunnerGetInPos(i));
      RenderVisualState().PositionModelToWorld(pt._pos, pt._pos);
      float dist2 = pt._pos.Distance2(unitPos);
      if (dist2 < minDist2)
      {
        minDist2 = dist2;
        best = pt;
      }
    }
  }
  if (minDist2<FLT_MAX)
  {
    RenderVisualState().DirectionModelToWorld(best._dir, best._dir);
    return best;
  }
  else
  {
    return GetDefaultGetInPos(person,pos);
  }
}

GetInPoint Transport::GetCargoGetInPos(int cargoIndex, Person *person, Vector3Par pos) const
{
  const VisualState &vs = RenderVisualState();
  Vector3Val unitPos = pos;
  GetInPoint best;
  float minDist2 = FLT_MAX;
  int n = Type()->NCargoGetInPos(cargoIndex);
  for (int i=0; i<n; i++)
  {
    GetInPoint pt = AnimateGetInPoint(Type()->GetCargoGetInPos(cargoIndex, i));
    vs.PositionModelToWorld(pt._pos, pt._pos);
    float dist2 = pt._pos.Distance2(unitPos);
    if (dist2 < minDist2)
    {
      minDist2 = dist2;
      best = pt;
    }
  }
  if (minDist2<FLT_MAX)
  {
    vs.DirectionModelToWorld(best._dir, best._dir);
    return best;
  }
  else
  {
    return GetDefaultGetInPos(person,pos);
  }
}

#if _HELISIM_PRECISE_INTERACTION
GetInPoint Transport::GetDriverGetInPosPrecise(Person *person, Vector3Par pos) const
{
  Vector3Val unitPos = pos;
  GetInPoint best;
  float minDist2 = FLT_MAX;
  int n = Type()->NDriverGetInPosPrecise();
  for (int i=0; i<n; i++)
  {
    GetInPoint pt = AnimateGetInPoint(Type()->GetDriverGetInPosPrecise(i));
    RenderVisualState().PositionModelToWorld(pt._pos, pt._pos);
    float dist2 = pt._pos.Distance2(unitPos);
    if (dist2 < minDist2)
    {
      minDist2 = dist2;
      best = pt;
    }
  }
  if (minDist2<FLT_MAX)
  {
    RenderVisualState().DirectionModelToWorld(best._dir, best._dir);
    return best;
  }
  else
  {
    return GetDefaultGetInPos(person,pos);
  }
}

GetInPoint Transport::GetCargoGetInPosPrecise(int cargoIndex, Person *person, Vector3Par pos) const
{
  const VisualState &vs = RenderVisualState();
  Vector3Val unitPos = pos;
  GetInPoint best;
  float minDist2 = FLT_MAX;
  int n = Type()->NCargoGetInPosPrecise(cargoIndex);
  for (int i=0; i<n; i++)
  {
    GetInPoint pt = AnimateGetInPoint(Type()->GetCargoGetInPosPrecise(cargoIndex, i));
    vs.PositionModelToWorld(pt._pos, pt._pos);
    float dist2 = pt._pos.Distance2(unitPos);
    if (dist2 < minDist2)
    {
      minDist2 = dist2;
      best = pt;
    }
  }
  if (minDist2<FLT_MAX)
  {
    vs.DirectionModelToWorld(best._dir, best._dir);
    return best;
  }
  else
  {
    return GetDefaultGetInPos(person,pos);
  }
}

GetInPoint Transport::GetTurretGetInPosPrecise(Turret *turret, Person *person, Vector3Par pos) const
{
  Vector3Val unitPos = pos;
  GetInPoint best;
  float minDist2 = FLT_MAX;
  TurretContext context;
  if (FindTurret(turret, context) && context._turret)
  {
    int n = context._turretType->NGunnerGetInPosPrecise();
    for (int i=0; i<n; i++)
    {
      GetInPoint pt = AnimateGetInPoint(context._turretType->GetGunnerGetInPosPrecise(i));
      RenderVisualState().PositionModelToWorld(pt._pos, pt._pos);
      float dist2 = pt._pos.Distance2(unitPos);
      if (dist2 < minDist2)
      {
        minDist2 = dist2;
        best = pt;
      }
    }
  }
  if (minDist2<FLT_MAX)
  {
    RenderVisualState().DirectionModelToWorld(best._dir, best._dir);
    return best;
  }
  else
  {
    return GetDefaultGetInPos(person,pos);
  }
}


#endif

GetInPoint Transport::GetDriverGetOutPos(Person *person) const
{
  Vector3Val pos = person->WorldPosition(person->RenderVisualState());
  return GetDriverGetInPos(person, pos);
}

GetInPoint Transport::GetCommanderGetOutPos(Person *person) const
{
  Vector3Val pos = person->WorldPosition(person->RenderVisualState());
  return GetCommanderGetInPos(person, pos);
}

GetInPoint Transport::GetGunnerGetOutPos(Person *person) const
{
  Vector3Val pos = person->WorldPosition(person->RenderVisualState());
  return GetGunnerGetInPos(person, pos);
}

GetInPoint Transport::GetTurretGetOutPos(Turret *turret, Person *person) const
{
  Vector3Val pos = person->WorldPosition(person->RenderVisualState());
  return GetTurretGetInPos(turret, person, pos);
}

GetInPoint Transport::GetCargoGetOutPos(Person *person) const
{
  Vector3Val pos = person->WorldPosition(person->RenderVisualState());

  // check which cargo slot are we in
  int index = -1;
  for (int i=0; i<_manCargo.Size(); i++)
  {
    if (person == _manCargo[i])
    {
      index = i;
      break;
    }
  }
  return GetCargoGetInPos(index, person, pos);
}

void Transport::PerformFF( FFEffects &effects )
{
  base::PerformFF(effects);

  //  if (_engineOff)
  //  {
  //    effects.engineMag = 0;
  //    effects.engineFreq = 50;
  //    return;
  //  }

  float vol,freq;
  vol=GetEngineVol(freq);
  if (freq<=0.001) vol = 0;

  effects.engineMag = vol*0.09f;
  effects.engineFreq = freq*50;
}

/*!
\patch 1.24 Date 09/26/2001 by Ondra
- Fixed: Speed of sound delay was missing on vehicle crash sound.
*/

class CrewSoundFunc : public ICrewFunc
{
protected:
  float _deltaT;
  Transport *_parent;

public:
  CrewSoundFunc(Transport *veh, float timeDelta) : _parent(veh), _deltaT(timeDelta)
  {}

  virtual bool operator () (Person *person)
  {
    if (person->SoundInTransportNeeded())
    {
      Object::ProtectedVisualState<const ObjectVisualState> vs = person->RenderVisualStateScope(); 
      Matrix4 worldPos = _parent->ProxyWorldTransform(person->GetVisualStateAge(vs), person);
      person->SoundInTransport(_parent,worldPos,_deltaT);
    }
    return false;
  }
};

/*
Member function evaluate frequency and volume values on the basis of expression, expression is given in config,
more info about sound controllers can be found at https://wiki.bistudio.com/index.php/Kontrolery_pro_zvuky_%28Audio_Controllers%29
*/
float Transport::GetSoundsVol(float &freq, const SoundParsEx &soundPars, float* evalControllers, int length) const
{
  bool result = soundPars._freqExpr.Evaluate(freq, evalControllers, length);
  if (!result) freq = 1;

  float volume;
  result = soundPars._volExpr.Evaluate(volume, evalControllers, length);
  if (!result) volume = 0;

  return volume;
}

float Transport::EvaluateSoundEvent(const SoundEventPars &soundEPars, float* evalControllers, int length) const
{
  float val;
  bool result = soundEPars._expression.Evaluate(val, evalControllers, length);

  return (result ? val : 0);
}

/*
Camera inside = 0, outside = 1, between interpolation in range [0, 1]
*/
float Transport::GetCamPos() const
{
  if (GWorld->CameraOn() == this)
  {
    bool listenerInside = false;
    OLink(Person) person = GWorld->FocusOn() ? GWorld->FocusOn()->GetPerson() : NULL;

    if (person) { listenerInside = ListenerIsInside(person); }

    // listener inside vehicle
    if (listenerInside)
    {
      bool cameraTypeNew = GWorld->GetCameraTypeNew() == CamInternal || GWorld->GetCameraTypeNew() == CamGunner;
      bool cameraTypeOld = GWorld->GetCameraTypeOld() == CamInternal || GWorld->GetCameraTypeOld() == CamGunner;

      // camera inside vehicle
      if (cameraTypeNew && cameraTypeOld) return 0.0f;

      // transition from internal camera to external
      if (cameraTypeOld && GWorld->GetCameraTypeNew() == CamExternal)
      {
        return (GWorld->GetCameraTransition());
      }

      // transition from external to internal camera
      if (GWorld->GetCameraTypeOld() == CamExternal && cameraTypeNew)
      {
        return (1.0f-GWorld->GetCameraTransition());
      }

      return 1.0f;
    }
    }

  return 1.0f;
}

float Transport::GetSpeed() const
{
  const VisualState &vs = RenderVisualState();
  Vector3 tmp = vs.DirectionWorldToModel(vs.Speed());

  return tmp.Size() * fSign(tmp.Z());
}

/// surface usage count
/* used for audio controllers */
struct SurfInfoCount
{
  const SurfaceInfo *surf;
  int count;
};

TypeIsSimple(SurfInfoCount);

template <>
struct FindArrayKeyTraits<SurfInfoCount>
{
  typedef const SurfaceInfo * KeyType;
  static bool IsEqual(KeyType a, KeyType b) {return a==b;}
  static KeyType GetKey(const SurfInfoCount &a) {return a.surf;}
};

// Select surface info
void Transport::SetSurfaceInfo(GroundCollisionBuffer &groundCollBuffer)
{
  // check which surfaces are we in contact with
  FindArrayKey<SurfInfoCount, FindArrayKeyTraits<SurfInfoCount>, MemAllocLocal<SurfInfoCount,16> > surfsInfo;

  for (int i = 0; i < groundCollBuffer.Size(); i++)
  {
    // info.pos is world space
    UndergroundInfo &info = groundCollBuffer[i];

    if (info.under < 0) { continue; }

    // surfaces occurrences, used for selection the most incident surface
    if (info.texture)
    {
      const SurfaceInfo *surf = &info.texture->GetSurfaceInfo();
      int index = surfsInfo.FindKey(surf);
      if (index<0)
      {
        SurfInfoCount &sInfo = surfsInfo.Append();
        sInfo.count = 1;
        sInfo.surf = surf;
      }
      else
      {
        surfsInfo[index].count++;
      }
    }
  }

  // select the most incident
  int maxCount = 0;
  const SurfaceInfo *surf = NULL;
  for (int j = 0; j < surfsInfo.Size(); j++)
  {
    if (surfsInfo[j].count > maxCount) maxCount = surfsInfo[j].count, surf = surfsInfo[j].surf;
  }

  _ptrSurfInfo = surf;
}

template<class Type>
void SoundsHolder::UpdateSubmixWaves(const AutoArray<Type> &arr, const TransportSoundContex &context)
{
  PROFILE_SCOPE_EX(sndUS, sound);

  if (!_submix) return;

  float distSum = 0;
  float volSum = 0;
  int nWaves = 0;
  SoundCone *cone = NULL;

  // update waves included in submix
  for (int i = 0; i < arr.Size(); i++)
  {
    SoundParsEx &parEx = const_cast<SoundParsEx &>(arr[i]);

    // set cone ptr, cone is same for whole group
    if (!cone && parEx._coneUsed) cone = &parEx._cone;

    float vol, freq;
    // volume & frequency expressions evaluation
    vol = context._transport->GetSoundsVol(freq, parEx, context._ctrl, context._ctrlCount);

    if (freq>0.001f && vol>0.001f && (parEx._pars.name.GetLength() > 0) && (
      context._inside || GSoundScene->CanBeAudible(context._pos, parEx._pars.distance)))
    {
      // create wave
      if (_waves[i].IsNull())
      {
        // create wave
        _waves[i] = GSoundScene->OpenAndPlay(parEx._pars.name, context._pos, context._speed, 
          parEx._pars.vol, parEx._pars.freq, parEx._pars.distance);

        // include wave into submix
        if (_waves[i].NotNull()) { _submix->Include(_waves[i]); }
      }

      // update wave params
      if (_waves[i].NotNull())
      {
        _waves[i]->SetVolume(vol, freq);
        _waves[i]->SetObstruction(context._obstruction, context._occlusion, context._deltaT);
        _waves[i]->SetPosition(context._pos, context._speed);

        distSum += _waves[i]->GetMaxDistance();
        volSum += vol;
        nWaves++;
      }
    }
    else
    {
      // release wave
      if (_waves[i]) _submix->Exclude(_waves[i].GetRef());
      _waves[i].Free();
    }
  }

  if (_waves.Size() == 0 || nWaves == 0) return;

  _submix->EnableDirectionalSound(false);
  // external sounds
  if (!context._inside && cone) { AbstractSoundsHolder::SetDirectSound(*cone, context._transport); }
  if (context._inside) volSum *= context._transport->GetInsideSoundCoeff();

  float avgDist = (nWaves > 0) ? (distSum / (float) nWaves) : 0;
  float avgVol = (nWaves > 0) ? (volSum / (float) nWaves) : 0;

  float doppler = 1.0f;
  if (_submix) doppler = _submix->Calculate3D(true, context._pos, context._speed, avgDist, avgVol);

  for (int i = 0; i < _waves.Size(); i++)
  {
    if (_waves[i].NotNull()) 
      _waves[i]->SetVolume(_waves[i]->GetVolume(), doppler * _waves[i]->GetFrequency());
  }
}

template<class Type>
void SoundsEventHolder::UpdateSubmixWaves(const AutoArray<Type> &arr, const TransportSoundContex &context)
{
  PROFILE_SCOPE_EX(sndUE, sound);

  if (!_submix) return;
 
  float distSum = 0;
  float volSum = 0;
  int nWaves = 0;
  SoundCone *cone = NULL;
 
  // for each wave included into submix
  for (int i = 0; i < arr.Size(); i++)
  {
    SoundEventPars &pars = const_cast<SoundEventPars &>(arr[i]);

    // set cone ptr, cone is same for whole group
    if (!cone && pars._coneUsed) cone = &pars._cone;

    float expression = context._transport->EvaluateSoundEvent(pars, context._ctrl, context._ctrlCount);
    bool limitReached = expression > pars._limit;

    // remember limit overrun
    if (!limitReached) pars._started = false;

    if (_waves[i].IsNull())
    {
      if ((pars._pars.name.GetLength() > 0) && (limitReached) && (!pars._started)
        && GSoundScene->CanBeAudible(context._pos, pars._pars.distance))
      {
        // start non loop sample
        _waves[i] = GSoundScene->OpenAndPlayOnce(
          pars._pars.name, const_cast<Transport *> (context._transport), context._inside, context._pos, 
          context._speed, pars._pars.vol, pars._pars.freq, pars._pars.distance);

        if (_waves[i])
        {
          _submix->Include(_waves[i].GetRef());
          _waves[i]->SetObstruction(context._obstruction, context._occlusion, context._deltaT);
          pars._started = true;
        }
      }
    }
    else
    {
      // update position
      _waves[i]->SetPosition(context._pos, context._speed);
      distSum += _waves[i]->GetMaxDistance();
      volSum += pars._pars.vol;
      nWaves++;

      // wave played, so we can release them
      if (_waves[i]->IsStopped()) { _submix->Exclude(_waves[i].GetRef()); _waves[i].Free(); }
    }
  }

  _submix->EnableDirectionalSound(false);
  // external sounds
  if (!context._inside && cone) { AbstractSoundsHolder::SetDirectSound(*cone, context._transport); }

  // minimal distance is avg. from all distances,
  float avgDist = (nWaves > 0) ? (distSum / (float) nWaves) : 0;
  float avgVol = (nWaves > 0) ? (volSum / (float) nWaves) : 0;

  // calculate 3d sound, if 2D then has no effect
  float doppler = _submix ? _submix->Calculate3D(true, context._pos, context._speed, avgDist, avgVol) : 1.0f;

  // applying doppler effect to source voices
  for (int i = 0; i < _waves.Size(); i++)
  {
    if (_waves[i].NotNull())
      _waves[i]->SetVolume(_waves[i]->GetVolume(), _waves[i]->GetFrequency() * doppler);
  }
}

void AbstractSoundsHolder::Construct(int count)
{
  Assert(!_submix);
  _submix = GSoundsys->CreateSubmix();
  Assert(_submix);

  if (_submix) // for dSound or openAL it is always NULL
  {
    _submix->SetFilter(SubmixLowPFilter);
    _waves.Realloc(count);
    _waves.Resize(count);
  }
}

FilterTypeEx AbstractSoundsHolder::SetFilter(FilterTypeEx fType)
{
  if (_submix)
    return _submix->SetFilter(fType);
  else
    return NoFilter;
}

void AbstractSoundsHolder::SetOutputVoiceSound(SubmixOutput sOutput)
{
  if (_submix)
    _submix->SetOutputVoiceSound(sOutput);
}

void AbstractSoundsHolder::Destruct()
{
  for (int i = 0; i < _waves.Size(); i++)
  {
    if (_waves[i]) _submix->Exclude(_waves[i]);
    _waves[i].Free();
  }

  _waves.Clear();
  
  if (_submix)
  {
    _submix->Unload();
    delete _submix;
    _submix = NULL;
  }
}

void AbstractSoundsHolder::SetDirectSound(SoundCone &cone, const Transport *transport)
{
  Assert(_submix && transport);

  // directional sound
  if (_submix && transport)
  {
    Vector3 frontDir = transport->RenderVisualState().DirectionModelToWorld(transport->GetFrontOrientation());
    frontDir.Normalize();

    Vector3 topDir = transport->RenderVisualState().DirectionModelToWorld(transport->GetTopOrientation());
    topDir.Normalize();

    _submix->EnableDirectionalSound(true);
    _submix->SetOrientation(topDir, frontDir);
    _submix->SetDirSoundConePars(cone._angleIn, cone._angleOut, cone._volIn, cone._volOut);
  }
}

const SoundPars& Transport::GetEngineSoundPars(bool inside)
{
  const TransportType *tType = Type();

  if (_engineOff)
    return inside ? tType->_engineOffSoundInt :  tType->_engineOffSoundExt;
  else
    return inside ? tType->_engineOnSoundInt : tType->_engineOnSoundExt;
}

// used in StartStopHolder for wave callback
struct OnTerminateCallback: public RefCount
{
  StartStopHolder &_holder;
  OnTerminateCallback(StartStopHolder &holder): _holder(holder) {}
};

bool StartStopHolder::StartPlayFromPos(Transport *transport, const SoundPars &pars, bool inside, bool reverse)
{
  const Transport::VisualState &vs = transport->RenderVisualState();
  // necessary params
  if ((pars.name.GetLength() > 0) && (pars.freq > 0.001f) && (pars.vol > 0.001f) &&
    GSoundScene->CanBeAudible(WaveEffect, vs.Position(), pars.vol))
  {
    RefAbstractWave tmp = GSoundScene->OpenAndPlayOnce(pars.name, transport, inside,
      vs.PositionModelToWorld(transport->GetEnginePos()), vs.Speed(), pars.vol, pars.freq, pars.distance);

    if (tmp)
    {
      // set callback
      tmp->SetOnTerminate(WaveCallBack, new OnTerminateCallback(*this));
      float vol = pars.vol;
      if (inside) vol *= transport->GetInsideSoundCoeff();
      tmp->SetVolume(vol, pars.freq);
      float pos = 1.0f;
      // if there is some wave playing
      if (_wave) pos = _wave->GetCurrPosition();
      pos = reverse ? (1.0f - pos) : pos;
      // set starting position
      tmp->SetStartOffset(pos);
      // release actually playing wave and set new wave
      if (_wave) _wave->Unload(), _wave.Free();
      // set as current playing
      _wave = tmp;
      _waveStop = false;
      _wavePlaying = true;

      return true;
    }
  }

  return false;
}

/* on terminate callback */
void StartStopHolder::WaveCallBack(AbstractWave *wave, RefCount *context)
{
  OnTerminateCallback* callback = static_cast<OnTerminateCallback *> (context);

  if (callback)
  {
    // set flags
    callback->_holder._wavePlaying = false;
    callback->_holder._waveStop = true;
  }
}

/* Release wave */
void StartStopHolder::Unload()
{
  if (_wave) _wave->Unload();
  _wave.Free();
}

void StartStopHolder::Sound(Transport *transport)
{
  // recalculate channels
  if (_wave) _wave->SetPosition(transport->RenderVisualState().Position(), VZero);

  // initialization - first pass, set init states
  if (!_stateInitilized)
  {
    _engineState = transport->_engineOff;
    _cameraState = transport->GetCamPos() < 0.5f;

    _stateInitilized = true;
    return;
  }

  int hitIndex = -1;

  //// planes has no hit point "hit engine"
  if (!transport->GetType()->IsKindOf(GWorld->Preloaded(VTypePlane)))
    hitIndex = transport->GetType()->FindHitPoint("HitEngine");

  // when engine damage - no start/stop sound
  if (hitIndex >= 0)
  {
    float engineHit = transport->GetHit(hitIndex);

    // engine is damage -> no start/stop sound
    if (engineHit >= 0.9f) return;
  }

  // engine state change
  if (_engineState != transport->_engineOff)
  {
    // select sound pars
    const SoundPars &pars = transport->GetEngineSoundPars(_cameraState);
    StartPlayFromPos(transport, pars, _cameraState, true);
    _engineState = transport->_engineOff;
  }

  bool cameraState = transport->GetCamPos() < 0.5f;

  // camera position change
  if (cameraState != _cameraState)
  {
    if (_wavePlaying)
    {
      // select sound pars
      const SoundPars &pars = transport->GetEngineSoundPars(cameraState);
      StartPlayFromPos(transport, pars, cameraState, false);
    }

    _cameraState = cameraState;
  }

  // wave is stopped, so my be released
  if (_waveStop) _wave.Free();
}

const RString* Transport::SoundControllers()
{
  return SoundCtrlNames;
}

int Transport::SoundControllersCount()
{
  return lenof(SoundCtrlNames);
}

void Transport::EvaluateSoundControllers()
{
  //_soundControllers.Access(SoundControllersCount());

  _soundControllers[0] = GetRPM();
  _soundControllers[1] = GetRandomizer();
  _soundControllers[2] = GetSpeed();
  _soundControllers[3] = Thrust();
  _soundControllers[4] = GetCamPos();
  _soundControllers[5] = GetEngineOn();
  _soundControllers[6] = GetRotorSpeed();
  _soundControllers[7] = GetMainRotorThrust();
  _soundControllers[8] = GetAngularVelocity().Size();
  _soundControllers[9] = GetGMeterZ();
  _soundControllers[10] = GetRoughness();
  _soundControllers[11] = GetDustness();
  _soundControllers[12] = GetDamperState(0);
  _soundControllers[13] = GetRockness();
  _soundControllers[14] = GetSandness();
  _soundControllers[15] = GetGrassness();
  _soundControllers[16] = GetMudness();
  _soundControllers[17] = GetGravelness();
  _soundControllers[18] = GetAsphaltness();
  _soundControllers[19] = GetGearPos();
  _soundControllers[20] = GetFlapPos();
  _soundControllers[21] = GetRotorPos();
  _soundControllers[22] = GetWaterContact();
#if _HELISIM_SOUNDS
  if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicleIn() == this)
  {
    _soundControllers[23] = GetAPU();
    _soundControllers[24] = IsBatteryOn() ? 1.0f : 0.0f;
    _soundControllers[25] = GetRPMStarter(0);
    _soundControllers[26] = GetRPM(1);
    _soundControllers[27] = GetRPMStarter(1);
    _soundControllers[28] = GetMotorDamage(); // average damage per
    _soundControllers[29] = GetTransmissionDamage();
    _soundControllers[30] = GetRotorBrake();
  }
  else
  {
    _soundControllers[23] = 0.0f;
    _soundControllers[24] = 0.0f;
    _soundControllers[25] = 0.0f;
    _soundControllers[26] = 0.0f;
    _soundControllers[27] = 0.0f;
    _soundControllers[28] = 0.0f;
    _soundControllers[29] = 0.0f;
    _soundControllers[30] = 0.0f;
  }
#endif

#if _ENABLE_CHEATS

  if (GWorld->_soundCtrlsForVeh == this)
  {
    const AutoArray<RString> &ctrlsWanted = GWorld->_soundCtrlsWanted;

    for (int i = 0; i < ctrlsWanted.Size(); i++)
    {
      for (int j = 0; j < Transport::SoundControllersCount(); j++)
      {
        if (ctrlsWanted[i] == Transport::SoundControllers()[j])
          DIAG_MESSAGE_ID(100, 222 + i, Format("%s: %.3f",  cc_cast(Transport::SoundControllers()[j]), _soundControllers[j]));
      }
    }
  }
#endif
}

void Transport::Sound(bool inside, float deltaT)
{
  PROFILE_SCOPE_EX(sndTr, sound);

  const VisualState &vs = RenderVisualState();

  const TransportType *tType = Type();

  // audio controllers initialization
  if (_sounds.Size() == 0)
  {
    int subParsCount = tType->_submixPars.Size();

    _sounds.Realloc(subParsCount);
    _sounds.Resize(subParsCount);

    for (int i = 0; i < _sounds.Size(); i++)
    {
      _sounds[i] = new SoundsHolder(tType->_submixPars[i].Size());
    }
  }

  // audio events initializations
  if (_soundsEvents.Size() == 0)
  {
    int subParsCount = tType->_submixEventPars.Size();

    _soundsEvents.Realloc(subParsCount);
    _soundsEvents.Resize(subParsCount);

    for (int i = 0; i < subParsCount; i++)
    {
      _soundsEvents[i] = new SoundsEventHolder(tType->_submixEventPars[i]._sEventPars.Size());
    }
  }

#if 0

  for (int i = 0; i < tType->_submixPars.Size(); i++)
  {
    const SubmixPars &sPars = tType->_submixPars[i];

    LogF("SubmixPars: %d", i);

    for (int j = 0; j < sPars._sParsEx.Size(); j++)
    {
      SoundParsEx *sEx = sPars._sParsEx[j];

      LogF("\tSoundparsEx %d, %s, %f", j, cc_cast(sEx->_pars.name), sEx->_pars.vol);
    }
  }

#endif

  TransportSoundContex tsc;
  tsc._transport = this;
  tsc._ctrl = _soundControllers.Data();
  tsc._ctrlCount = _soundControllers.Size();
  tsc._inside = inside;
  tsc._deltaT = deltaT;
  tsc._pos = vs.PositionModelToWorld(GetEnginePos());
  tsc._speed = vs.Speed();
  tsc._obstruction = 1.0f;
  tsc._occlusion = 1.0f;

  // we currently assume the engine sound is correct outside however we do not want to occlude it inside,
  // we only change its volume instead
  if (!inside) 
  {
    GWorld->CheckSoundObstruction(this, inside, tsc._obstruction, tsc._occlusion); 
  }

  for (int i = 0; i < _sounds.Size(); i++)
  {
    _sounds[i]->UpdateSubmixWaves(tType->_submixPars[i], tsc);
  }

  for (int i = 0; i < _soundsEvents.Size(); i++)
  {
    const SubmixEventPars &subEventPars = tType->_submixEventPars[i];
    _soundsEvents[i]->UpdateSubmixWaves(subEventPars._sEventPars, tsc);
  }

  // start & stop sounds
  if (!_holder) _holder = new StartStopHolder();
  _holder->Sound(this);

  if (_showDmg)
  {
    const SoundPars &sound=GetType()->GetDmgSound();

    float freq=sound.freq;
    float vol=sound.vol;
    if (sound.name.GetLength() > 0 && (inside || GSoundScene->CanBeAudible(WaveEffect,vs.Position(),vol)))
    {
      if (!_dmgSound)
      {
        _dmgSound = GSoundScene->OpenAndPlay(
          sound.name, vs.PositionModelToWorld(GetType()->_showDmgPoint), vs.Speed(), vol, freq, sound.distance);
      }
      if (_dmgSound)
      {
        float obstruction = 1, occlusion = 1;
        // warnings are obstructed by the vehicle itself
        GWorld->CheckSoundObstruction(this,true,obstruction,occlusion);
        _dmgSound->Set3D(!inside);
        _dmgSound->SetVolume(vol,freq); // volume, frequency
        _dmgSound->SetPosition(vs.PositionModelToWorld(GetType()->_showDmgPoint), vs.Speed());
        _dmgSound->SetObstruction(obstruction,occlusion,deltaT);
      }
    }
  }
  else
  {
    _dmgSound.Free();
  }

  if( _doorSound )
  {
    float obstruction = 1, occlusion = 1;
    GWorld->CheckSoundObstruction(this,true,obstruction,occlusion);
    const SoundPars &pars=GetType()->GetGetOutSound();
    float vol=pars.vol;
    _doorSound->SetVolume(vol); // volume, frequency
    _doorSound->Set3D(!inside);
    _doorSound->SetPosition(vs.Position(), vs.Speed());
    _doorSound->SetObstruction(obstruction,occlusion,deltaT);
  }

  if (_doCrash != CrashNone && Glob.time > _timeCrash + 1.0)
  {
    _timeCrash = Glob.time;
    const SoundPars *pars = NULL;

    switch (_doCrash)
    {
    case CrashObject:
      pars = &GetType()->GetCrashSound();
      break;

    case CrashLand:
      pars = &GetType()->GetLandCrashSound();
      break;

    case CrashWater:
      pars = &GetType()->GetWaterCrashSound();
      break;

    case CrashBuilding:
      pars = &GetType()->GetBuildingCrashSound(GRandGen.RandomValue());
      break;

    case CrashTree:
      pars = &GetType()->GetWoodCrashSound(GRandGen.RandomValue());
      break;

    case CrashArmor:
      pars = &GetType()->GetArmorCrashSound(GRandGen.RandomValue());
      break;
    }

    if (pars)
    {
      float volume=pars->vol*_crashVolume;
      if (inside) volume *= GetInsideSoundCoeff();

      if (pars->name.GetLength() > 0 && GSoundScene->CanBeAudible(vs.Position(), pars->distance))
      {
        float freq=pars->freq;
        AbstractWave *sound=GSoundScene->OpenAndPlayOnce(
          pars->name,this,false,vs.Position(),vs.Speed(),volume,freq, pars->distance);

        if (sound)
        {
          GSoundScene->SimulateSpeedOfSound(sound);
          GSoundScene->AddSound(sound);
        }
      }
    }
  }
  _doCrash=CrashNone;

  for (int i=0; i<_turrets.Size(); i++)
  {
    TurretType *turretType = Type()->_turrets[i];
    Turret *turret = _turrets[i];
    turret->Sound(this,*turretType, inside, deltaT, unconst_cast(vs), vs.Speed());
  }

  //For each crew call Man::UpdateSpeechPosition
  CrewSoundFunc crewSound(this, deltaT);
  ForEachCrew(crewSound);

  base::Sound(inside,deltaT);
}

bool Transport::ListenerIsInside(Person *listener) const
{
  // if person is a driver or a gunner of some turret, check if it is hidden or not
  if (listener == Driver())
  {
    if (!Type()->_hideProxyInCombat) return true;
    return FutureVisualState().IsDriverHidden();
  }
  else
  {
    TurretContextV context;
    if (FindTurret(unconst_cast(RenderVisualState()), listener, context))
    {
      // when _viewGunnerInExternal is set, we assume gunner is actually outside
      return context._turret && (!Type()->_hideProxyInCombat || context._turretVisualState->IsGunnerHidden()) && !context._turretType->_viewGunnerInExternal;
    }
  }
  // not a gunner, not a driver - if vehicle matches, it must be a cargo
  // cargo is currently supposed to be always inside
  return listener->GetHierarchyParent() == this;
}

void Transport::OccludeAndObstructSound(EntityAI *focus, float &occlusion, float &obstruction, ObstructType soundInside) const
{
  //if (focus==this && soundInside ) return;

  // focus should be a crew
  Person *crew = dyn_cast<Person>(focus);
  // turned out crews have no occlusions/obstructions
  bool soundIsInside = soundInside==SameInside;
  bool focusInside = !crew || ListenerIsInside(crew);
  if (focusInside!=soundIsInside)
  {
    float occ = Type()->_occludeSoundsWhenIn;
    float obs = Type()->_obstructSoundsWhenIn;
    if (soundInside==SameOutside)
    {
      // limited occlusion for our own sounds
      // assume they are partially transmitted through the vehicle body
      static float maxSameOcc = 0.5f;
      static float maxSameObs = 0.8f;
      saturateMax(occ,maxSameOcc);
      saturateMax(obs,maxSameObs);
    }
    occlusion *= occ;
    obstruction *= obs;
  }
}

void Transport::UnloadSound()
{
  _engineSound.Free();
  _envSound.Free();
  _dmgSound.Free();

  _holder.Free();

  _sounds.Clear();
  _soundsEvents.Clear();

  for (int i=0; i<_turrets.Size(); i++)
  {
    Turret *turret = _turrets[i];
    turret->UnloadSound();
  }

  base::UnloadSound();
}

bool Transport::IsFireEnabled(const TurretContext &context)
{
  // who is firing
  if (IsManualFire())
    return EffectiveObserverUnit() != NULL;
  else
    return context._gunner && context._gunner->Brain();
  /*
  if (Type()->HasGunner())
  {
  if (!EffectiveGunnerUnit()) return false;
  }
  else
  {
  if (!PilotUnit()) return false;
  }
  return true;
  */
}

#define DIAG_SPEED 0

#include "dikCodes.h"

bool Transport::StopAtStrategicPos() const
{
  switch (_moveMode)
  {
    case VMMBackward:
    case VMMSlowForward:
    case VMMForward:
    case VMMFastForward:
      return false;
    default:
      return true;
  }
}

float Transport::CalculateDriverPos(float &headChange, float minDist, Vector3 &instant)
{
  instant = _driverPos;
  const float reactionTime = DriverReactionTime;
  float speedCoef = 1.0f;
  switch (_moveMode)
  {
    case VMMFormation:
      Fail("No command");
      return speedCoef;
    case VMMMove:
      break;
    case VMMStop:
      if (_turnMode == VTMAbs)
      {
        Vector3 dir = DirectionWorldToModel(_dirWanted);
        headChange = atan2(dir.X(), dir.Z());
        if (fabs(headChange) < 0.01 * H_PI) _turnMode = VTMNone;
      }
      return speedCoef;
    case VMMBackward:
      {
        speedCoef = -1.0;
        Vector3 from = FutureVisualState().Position() + FutureVisualState().Speed()*reactionTime;
        float maxSpeed = GetType()->GetMaxSpeedMs()*0.5f;
        instant = from - (CmdPlanDist+maxSpeed*speedCoef) * _dirWanted;
        if (_turnMode != VTMNone)
          _turnMode = VTMNone;
        else
        {
          float dist2 = (_driverPos - FutureVisualState().Position()).SquareSizeXZ();
          if (dist2 >= Square(minDist))
            break;
        }
        _driverPos = instant;
        break;
      }
    case VMMSlowForward:
      speedCoef = 0.5;
      goto Forward;
    case VMMForward:
      speedCoef = 1.0;
      goto Forward;
    case VMMFastForward:
      speedCoef = 2.0;
      goto Forward;
Forward:
      {
        Vector3 from = FutureVisualState().Position() + FutureVisualState().Speed()*reactionTime;
        float maxSpeed = GetType()->GetMaxSpeedMs()*0.5f;
        instant = from + (CmdPlanDist+maxSpeed*speedCoef) * _dirWanted;
        if (_turnMode != VTMNone)
          _turnMode = VTMNone;
        else
        {
          float dist2 = (_driverPos - FutureVisualState().Position()).SquareSizeXZ();
          if (dist2 >= Square(minDist))
            break;
        }
        _driverPos = instant;
        break;
      }
  }
  return speedCoef;
}

#if _ENABLE_AI

void Transport::CommandPilot(SteerInfo &info)
{
  // driver has any command
  info.speedWanted = 0;
  info.headChange = 0;
  info.turnPredict=0;

  AIBrain *unit = PilotUnit();
  Assert(unit);
  if (unit->GetAIDisabled() & (AIBrain::DAMove)) return;

  if (_turnMode == VTMLeft)
  {
    info.speedWanted = FutureVisualState().ModelSpeed().Z();
    if (VMMBackward == _moveMode && ReverseLeftRightAIPilot())
      info.headChange = +0.25 * H_PI;
    else
    info.headChange = -0.25 * H_PI;
    return;
  }
  else if (_turnMode == VTMRight)
  {
    info.speedWanted = FutureVisualState().ModelSpeed().Z();
    if (VMMBackward == _moveMode && ReverseLeftRightAIPilot())
      info.headChange = -0.25 * H_PI;
    else
    info.headChange = +0.25 * H_PI;
    return;
  }

  Vector3 instantPos;
  float speedCoef = CalculateDriverPos(info.headChange,CmdMinDist,instantPos);

  if (_moveMode==VMMFormation || _moveMode==VMMStop)
  {
    return;
  }

  if (_moveMode==VMMMove)
  {
    if (unit != CommanderUnit())
    {
      // FIX: need to check wanted values, not the current ones
      if (unit->GetPlanningModeWanted() != AIBrain::VehiclePlanned || unit->GetWantedPosition().Distance2(_driverPos) > Square(1))
      {
        // FIX: clean up the path to avoid fulfillment of previous destination
        unit->SetWantedDestination(PlanPosition(_driverPos,false), AIBrain::DoNotPlan, true);
        unit->SetWantedPosition(PlanPosition(_driverPos,false), 0, AIBrain::VehiclePlanned, false);
      }
    }

    // driving to some position
    LeaderPathPilot(unit, info, speedCoef);
    return;
  }

  if (unit->GetPlanningMode() != AIBrain::VehiclePlanned || unit->GetWantedDestination().Distance2(_driverPos) > Square(1))
  {
    FindDriverPath(_driverPos,false);
  }

  // directional driving
  DriverPathPilot(unit, info, speedCoef, instantPos);
}

void Transport::FindDriverPath(Vector3Par pos, bool forceReplan)
{
  //AIUnit *unit = PilotUnit();
  //unit->SetWantedPosition(pos, AIUnit::VehiclePlanned, forceReplan);
}

void Transport::DriverPathPilot(AIBrain *unit, SteerInfo &info, float speedCoef, Vector3Par instant)
{
  // head towards _driverPos
  Vector3 pos = unit->NearestIntersection(FutureVisualState().Position(),instant);
  //NearestIntersection
  // head toward pos - only if pos is impossible, slow down
  float dist = pos.Distance(FutureVisualState().Position());

  Vector3 posRel = PositionWorldToModel(pos);
  if (speedCoef >= 0)
    info.headChange = atan2(posRel.X(), posRel.Z());
  else
    info.headChange = atan2(-posRel.X(), -posRel.Z());
  if (info.headChange>H_PI/2)
    Log("Big headChange %.2f",info.headChange*180/H_PI);

  info.speedWanted = 0;
  float precision = GetPrecision();
  if (dist>=precision*0.25f)
    info.speedWanted = dist*0.3f * speedCoef;

  float speedMin = floatMax(1,GetType()->GetMaxSpeedMs()*0.1f);
  if (speedCoef>0)
    saturateMax(info.speedWanted,speedMin*speedCoef);
  else if (speedCoef<0)
    saturateMin(info.speedWanted,speedMin*speedCoef);

  if (fabs(info.speedWanted)>=0.01)
    EngineOn();

  //LeaderPathPilot(unit, speedWanted, headChange, turnPredict, speedCoef);
}

/*!
\patch 1.82 Date 8/14/2002 by Ondra
- Fixed: MP: When AI soldier was gunner in vehicle where driver was player who was not group leader,
turret movement was not visible on driver's computer.
*/

void Transport::AIGunner(TurretContextEx &context, float deltaT )
{
  if( _isDead || _isUpsideDown ) return;

  AIBrain *unit = context._gunner->Brain();
  Assert(unit);

  WeaponModePtr selected = context._weapons->ValidatedCurrentWeapon();

  // aim weapon based on formation / watch
  /*
  AIBrain *commander = EffectiveObserverUnit();
  if (!commander) commander = unit;
  */
  if (Glob.time < _turretFrontUntil)
  {
    AimWeapon(context, selected, FutureVisualState().Direction());
    return;
  }
  _turretFrontUntil = TIME_MIN;

  Vector3Val watchDir = unit->GetWatchDirection();
  float watchDirSize2 = watchDir.SquareSize();

  if (watchDirSize2 > 0.1f) AimWeapon(context, selected, watchDir);
}

/*!
\patch 5164 Date 6/15/2007 by Bebul
- Fixed: Turret cannot move in 3rd person view without weapons
*/
void Transport::LookAround(float aimX, float aimY, float deltaT, const ViewPars *viewPars, AIBrain *unit, bool forceLookAround, bool forceDiveAround)
{
  bool discrete = false;
  if (GWorld->GetCameraType()==CamGunner)
  {
    AIBrain *unit = GWorld->FocusOn();
    Person *person = unit ? unit->GetPerson() : NULL;
    TurretContext context;
#if _VBS3 //there are viewPars for turned in and out
    if (person && FindOpticsTurret(person, context) && context._turret)
    {
      viewPars = context._turret->GetCurrentViewPars();
    }
#else
    if (person && FindTurret(person, context) && context._turret)
    {
      int opticsMode = (context._turret)?context._turret->_currentViewOpticsMode : 0;
      viewPars = &context._turretType->_viewOptics[opticsMode];
      //if there are multiple optics, zoomin/out will cycle trough them
      if(context._turretType->_viewOptics.Size() > 1) discrete = true;
    }
#endif
    else viewPars=&Type()->_viewOptics;
  }
  // aiming is scaled based on current FOV

  static float maxXSpeed = 6.0f;
  static float maxYSpeed = 4.0f;
  // look around
  float headingSpeed = -aimX;
  saturate(headingSpeed, -maxXSpeed, +maxXSpeed);
  float diveSpeed = -aimY;
  saturate(diveSpeed, -maxYSpeed, +maxYSpeed);
  //GWorld->ChangeCameraDirection(GWorld->GetCameraType(), headingSpeed * deltaT, diveSpeed * deltaT);
  if (unit)
  {
    //Assert(unit->GetVehicleIn()==this);
    Person *player = unit->GetPerson();

    if (player->IsRemotePlayer())
    {
      InputInterface *input = GetNetworkManager().GetInputInterface(player->GetRemotePlayer());

      //Get look around actions and continuous look around actions (TrackIR)
      if (player && GWorld->GetCameraType()!=CamGunner)
      {
        player->UpdateHeadLookAround(input, deltaT, forceLookAround); //Get LookAround Actions
        //Simulate and saturate LookAround angles
        player->ProcessLookAround(headingSpeed * deltaT, diveSpeed * deltaT, viewPars, deltaT, forceLookAround, forceDiveAround);
      }
      Zoom *zoom = GetZoom(); //Get the proper Zoom instance (optics of driver, gunner, observer, ...)
      UpdateZoom(unit); //updates UALockTarget and determine whether to zoom in, or not
      if (zoom) zoom->Update(input, deltaT, this, viewPars, discrete); //Get Zooming Actions and both process and saturate zoom
    }
  }
}

/*!
\patch 5128 Date 2/12/2007 by Bebul
- Fixed: Zoom instead of optics in transport
\patch 5134 Date 2/26/2007 by Bebul
- Fixed: Look around in vehicles (faster and better limits now)
*/
void Transport::UpdateZoom(AIBrain *unit)
{
  Person *player = unit->GetPerson();
  // The zoom flag must reflect the current button state every frame. Setting
  // it only when zooming in left the camera stuck at maximum zoom after the
  // button was released (the "infinite zoom" bug in vehicles).
  bool zoomInsteadOfOptics = false;
  if (player && GInput.GetAction(UALockTarget) && (GWorld->IsCameraActive(CamInternal) || GWorld->IsCameraActive(CamExternal)))
  {
//    EntityAIFull *veh = FocusOn()->GetVehicle();
//    Assert(veh);
    // Turret gunners keep using RMB for lock-on weapons; everybody else
    // (pilot, copilot, cargo) gets hold-to-zoom on the right mouse button.
    bool allowZoom = true;
    TurretContext context;
//    if (veh->GetPrimaryGunnerTurret(context))
    if (FindTurret(player, context))
    {
      WeaponModePtr curPtr = context._weapons->ValidatedCurrentWeapon();
      if (curPtr.IsValid())
      {
        const MuzzleType *muzzle = curPtr.GetMuzzleType();
        bool canLock =
          muzzle->_canBeLocked == 2 ||
          muzzle->_canBeLocked == 1 && Glob.config.IsEnabled(DTAutoGuideAT);
        if (canLock) allowZoom = false;
      }
    }
    if (allowZoom) zoomInsteadOfOptics = true;
  }
  // Use the zoom of the unit being updated (driver or gunner), not the
  // currently focused one, so the flag is always applied to the right camera.
  if (GetZoom(unit)) GetZoom(unit)->_zoomInsteadOfOptics = zoomInsteadOfOptics;
}

Object::Zoom *Transport::GetZoom(const AIBrain *unit)
{
  Person *man = unit ? unit->GetPerson() : NULL;
  if (!man) return NULL;
  // TODO: Get "local" camera type
  //if (GWorld->GetCameraType()!=CamGunner) return man->GetZoom();
  if (man == _driver)
    return &_zoom;
  TurretContext context;
  if (FindTurret(man, context) && context._turret)
    return context._turret->GetZoom();
  //cargo ???
  //man is not driver nor gunner
  return man->GetZoom(); //TODO
}

Object::Zoom *Transport::GetZoom()
{
  AIBrain *unit = GWorld->FocusOn();
  return GetZoom(unit);
}
float Transport::GetCameraFOV() const
{
  Object::Zoom* zoom = const_cast<Transport *>(this)->GetZoom();
  return zoom ? zoom->_camFOV : base::GetCameraFOV();
}

void Transport::KeyboardLookAround(AIBrain *unit, float deltaT)
{
  if( _isDead || _isUpsideDown ) return;
  Assert(unit);

  // aim and turn with aiming actions
  float aimX(0), aimY(0);
  aimX = GInput.GetActionWithCurve(InputInterface::CurveFovMouse, UAAimHeadRight, UAAimHeadLeft);
  aimY = GInput.GetActionWithCurve(InputInterface::CurveFovMouse, UAAimHeadUp, UAAimHeadDown);
  LookAround(aimX, aimY, deltaT, &Type()->_viewCargo, unit, true);
}


CombatMode Transport::GetCombatModeAutoDetected(const AIBrain *brain) const
{
  if (brain->IsAnyPlayer())
  {
    const TransportType *type = Type();
    if (type->_hideProxyInCombat)
    {
      bool hidden = false;
      Person *person = brain->GetPerson();
      if (person == Driver())
        hidden = FutureVisualState().IsDriverHidden();
      else
      {
        TurretContextV context;
        if (FindTurret(unconst_cast(FutureVisualState()), person, context) && context._turret)
        {
          // when the gunner cannot turn out/it, we cannot rely on testing it
          // to make sure we are able to fight, we rather assume the player wants to fight
          hidden = context._turretType->_viewGunnerInExternal || context._turretVisualState->IsGunnerHidden();
        }
      }
      return hidden ? CMAware : CMSafe;
    }
  }


  return CMSafe;
}

void Transport::KeyboardObserver(TurretContextEx &context, float deltaT)
{
  if (_isDead || _isUpsideDown)
    return;

  if (!context._gunner || context._gunner->KeyboardUsedInFrame()) return;

  // aim and turn with aiming actions
  float aimX(0), aimY(0);
  GetAimActions(aimX, aimY);

  CameraType type = GWorld->GetCameraType();
  if (type != CamGunner)
  {
    // do not move with turret
    if (IsGunnerLookingAround(context))
    {
      int opticsMode = (context._turret)?context._turret->_currentViewGunnerMode : 0;
      LookAround(aimX, aimY, deltaT, &context._turretType->_viewGunner[opticsMode], context._gunner->Brain(),true);

      if (context._turretType->_turretFollowFreeLook)
      {
        float lookX, lookY;
        context._gunner->GetLookWanted(lookX,lookY);
        SetObserverDir(context,lookX,lookY);
      }
      else
        SetObserverSpeed(context, 0, 0, deltaT);
    }
    else
    {
      int opticsMode = (context._turret)?context._turret->_currentViewGunnerMode : 0;
      LookAround(aimX, aimY, deltaT, &context._turretType->_viewGunner[opticsMode], context._gunner->Brain(),false);
      // move with turret
      SetObserverSpeed(context, aimY, -aimX, deltaT);
    }
  }
  else
  {
    int opticsMode = (context._turret)?context._turret->_currentViewGunnerMode : 0;
    LookAround(aimX, aimY, deltaT, &context._turretType->_viewGunner[opticsMode], context._gunner->Brain(),false);

    // cursor-level autoaim
    /*
    if (ShowWeaponAim())
      AutoAimCursor(aimX,aimY,deltaT,fov);
    */

    // move with turret
    SetObserverSpeed(context, aimY, -aimX, deltaT);
  }
}


void Transport::SetObserverDir(const TurretContextEx &context, float x, float y)
{
  context._turret->ToAbsolute(context._turret->_xRotWanted, context._turret->_yRotWanted, x, y, context._parentTrans);
}

void Transport::SetObserverSpeed(const TurretContextEx &context, float speedX, float speedY, float deltaT)
{
  if (context._turret)
    context._turret->SetSpeed(*context._turretVisualState, *context._turretType, speedX, speedY, deltaT, context._parentTrans);
}

bool Transport::IsGunnerLookingAround(const TurretContextV &context) const
{
#if _VBS3 //Commander override
  if(context._turret
    && context._turret->_overriddenByTurret
    && context._turret->_overriddenByTurret->_gunner == context._gunner
    )
    return false;
#endif

  CameraType cameraType = GWorld->GetCameraType();
  bool gunnerLookAround = false;
  if  (context._turret)
    gunnerLookAround = cameraType == CamInternal && !context._turretType->GunnerMayFire(context);
  return cameraType != CamGunner && (context._gunner->GetZoom()->_lookAroundEnabled || IsTurretLocked(context) || gunnerLookAround || 
    (!context._weapons->ValidatedCurrentWeapon().IsValid() && _copilot != context._gunner) // gunner with no weapons that is not copilot
  );
}

void Transport::KeyboardGunner(TurretContextEx &context, float deltaT)
{
  if (_isDead || _isUpsideDown)
    return;

  InputInterface *input = GetNetworkManager().GetInputInterface(context._gunner->GetRemotePlayer());

  if (!context._gunner || context._gunner->KeyboardUsedInFrame()) return;

  // aim and turn with aiming actions
  float aimX(0), aimY(0);
  GetAimActions(aimX, aimY);

  bool forceLookAround = false;
#if _VBS3
  if(context._turret && !context._turret->_overriddenByTurret)
#endif
    int opticsMode = (context._turret)?context._turret->_currentViewGunnerMode : 0;
  if (context._turret)
  {
    //Gunner is driving
    if(context._gunner == Copilot())
    {
      CameraType cameraType = GWorld->GetCameraType();
      bool gunnerLookAround = false;
      if  (context._turret)
        gunnerLookAround = cameraType == CamInternal;
      forceLookAround != CamGunner && (context._gunner->GetZoom()->_lookAroundEnabled || gunnerLookAround);
    }
    else forceLookAround = !context._turret->_gunStabilized || IsGunnerLookingAround(context);
  } 
  LookAround(aimX, aimY, deltaT, &context._turretType->_viewGunner[opticsMode], context._gunner->Brain(),forceLookAround);
  //if gunner is piloting, he cannot control guns
  if(context._gunner == Copilot()) return;

  if (!IsGunnerLookingAround(context))
  {
#if _VBS3_LASE_LEAD
    //only for gunner cam?
    if(context._turretType->_canLase && GInput.GetActionToDo(UALase,true,false))
    {
      // sight direction is where the 'laser' points
      Camera *camera = GScene->GetCamera();
      Vector3 camDir = camera->Direction();

      Vector3 targetPos,dir;
      int weapon = context._weapons->ValidatedCurrentWeapon();
      bool found = CalculateLaser(targetPos,dir,context,weapon,camDir);
      if(found)
      {
        float leadTime;
#if 0 //Debug
        Vector3 aimPos;
        if(CalculateAimWeaponPos(context,weapon,aimPos,leadTime,targetPos))
        {
          GScene->DrawDiagSphere(aimPos, 1.0f, PackedColor(Color(1,0,0,1)));
        }
#endif
        Vector3 aimDir;
        if(CalculateAimWeapon(context,weapon,aimDir,leadTime,targetPos))
        {
          context._turret->Lase(targetPos, aimDir, camDir);
        }
      }
    }
#endif //_VBS3_LASE_LEAD

    if (context._turret)
    {
      context._turret->SetSpeed(*context._turretVisualState, *context._turretType, aimY, -aimX, deltaT, context._parentTrans);
    }
  }

  if (context._turret)
  {
    Assert(context._gunner && context._gunner->QIsManual());
    {
      // advanced missile control
      // check if last shot is missile
      Missile *missile = dyn_cast<Missile, Vehicle>(context._weapons->_lastShot);
      if (missile)
      {
        // check cursor position
        WeaponModePtr selected = context._weapons->ValidatedCurrentWeapon();
        Vector3 tmpEnd;
        GLandscape->IntersectWithGroundOrSea(&tmpEnd, FutureVisualState().Position(), GetWeaponDirection(FutureVisualState(), context, selected).Normalized(), 0, 3500);
        missile->SetControlDirection(tmpEnd);
      }
    }

    bool fire = false;
    if (CanFire() && !GWorld->HasMap())
    {
      // Gunner is firing using mouse
      WeaponModePtr ptr;
      ptr = context._weapons->ValidatedCurrentWeapon();
      if (ptr.IsValid())
      {
        const WeaponModeType *mode = ptr.GetWeaponModeType();
        const Magazine *mag = ptr.GetMagazine();

        // check if this is throw
        if (mode && mode->_autoFire && mag && mag->GetAmmo() > 0)
        {
          fire = input->GetActionDo(UADefaultAction);
        }
        else
        {
          fire = input->GetActionToDo(UADefaultAction);
        }

        if (fire)
        {
          FireWeapon(context, ptr, NULL, false);
        }
      }
    }
  }
  
}

bool Transport::GIsManual(const Person *gunner) const
{
  if (!gunner)
    return false;
  if (!GLOB_WORLD->PlayerManual())
    return false;
  if (!GLOB_WORLD->PlayerOn())
    return false;
  return GLOB_WORLD->PlayerOn() == gunner;
}

// adjust weapon for distance fov
Vector3 Transport::AdjustWeapon(const TurretContext &context,const WeaponModePtr &ptr, float fov, Vector3 direction) const
{
  if (!ptr.IsValid())
    return direction;
  
  const Magazine *magazine = ptr.GetMagazine();
  if (!magazine)
    return direction;
  const MagazineType *aInfo = magazine->Type();
  if (!aInfo)
    return direction;

  const MuzzleType *muzzle = ptr.GetMuzzleType();
  Vector3 newDirection;

  if (!muzzle || muzzle->_ballisticsComputer == 0) return direction;

  if(!context._turretType || !context._turret) return direction;

  // calculate adjustment distance
  // interpolate between adjustment distances depending on fov
  float distance = 0;
  if(context._turretType->_discreteDistance.Size() > 0)
  {
    distance =  context._turretType->_discreteDistance[std::min(context._turret->_distanceIndex,context._turretType->_discreteDistance.Size()-1)];
  }
  else return direction;

  const AmmoType *ammo = aInfo->_ammo;
  float fall = 0.0f;
  Vector3 dir = direction.Normalized();
  PreviewBallistics(Vector3(0,0,0), direction*distance, aInfo, ammo, fall);

  float fallPerM = fall / distance;
  // this correction should be applied to weapon aiming
  newDirection = direction;
  newDirection[1] += fallPerM;

  return newDirection;
}

void Transport::LeaderPilot(SteerInfo &info)
{
  if (_userStopped || !PilotUnit())
  {
    info.speedWanted=0;
    info.headChange=0;
    info.turnPredict=0;
    return;
  }

  if (_moveMode == VMMFormation)
    base::LeaderPilot(info);
  else
    CommandPilot(info);
}

#endif // _ENABLE_AI

static bool CheckUnitAlive(AIBrain *unit)
{
  // check if unit is alive
  // it should be responding to some message
  // if it is not, it must be dead
  return unit && unit->LSIsAlive();
}


void Transport::ResetStatus()
{
  base::ResetStatus();
}

float Transport::GetFovFactor() const
{
  // default FOV is assumed to be 0.7
  float fov = GetCameraFOV();
  //float fovFactor = Square(fov*(1.0/0.7));
  static float lookSpeed = 1.0f/0.7f;
  return fov*lookSpeed;
}

static const EnumName MoveModeNames[]=
{
  EnumName(VMMFormation,"FORMATION"),
  EnumName(VMMMove,"MOVE"),
  EnumName(VMMBackward,"BACK"),
  EnumName(VMMStop,"STOP"),
  EnumName(VMMSlowForward,"SLOW"),
  EnumName(VMMForward,"FORWARD"),
  EnumName(VMMFastForward,"FAST"),
  EnumName(),
};

template<>
const EnumName *GetEnumNames( VehicleMoveMode dummy ) {return MoveModeNames;}

static const EnumName TurnModeNames[]=
{
  EnumName(VTMNone,"NONE"),
  EnumName(VTMLeft,"LEFT"),
  EnumName(VTMRight,"RIGHT"),
  EnumName(VTMAbs,"ABS"),
  EnumName()
};

template<>
const EnumName *GetEnumNames( VehicleTurnMode dummy ) {return TurnModeNames;}

LSError ManCargoItem::Serialize(ParamArchive &ar)
{
  CHECK(ar.SerializeRef("Man", man, 1))
  CHECK(ar.Serialize("locked", _locked, 1))
  return LSOK;
}

/*!
\patch 2.01 Date 1/3/2003 by Jirka
- Fixed: after load, engine was off for cars, tanks and planes
*/

LSError Transport::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))

  if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
  {
    // turret need to know its owner even in the first pass
    void *oldParams = ar.GetParams();
    ar.SetParams(this);
    CHECK(ar.Serialize("Turrets", _turrets, 1))
    ar.SetParams(oldParams);

    CHECK(ar.SerializeRef("Driver", _driver, 1))
    CHECK(ar.SerializeRef("SecondaryDriver", _copilot, 1))
    CHECK(ar.Serialize("unLockedControls",_unLockedControls, 1, false))
    CHECK(ar.Serialize("enableCopilots",_enableCopilots, 1, false))

    CHECK(ar.SerializeRef("EffCommander", _effCommander, 1))

    CHECK(ar.Serialize("driverLocked", _driverLocked, 1, false))
    CHECK(ar.Serialize("ManCargo", _manCargo, 1))
    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassSecond)
    {
      int n = Type()->_maxManCargo;
      if (_manCargo.Size() != n) _manCargo.Resize(n); // handle type change

      // switch light off for soldiers in vehicle
      //if (_driver)
      //{
      //  _driver->SetPilotLight(false);
      //  _driver->SwitchLight(false);
      //  _driver->SetCollisionLight(false);
      //}
      //for (int i=0; i<n; i++)
      //{
      //  Person *man = _manCargo[i];
      //  if (man)
      //  {
      //    man->SetPilotLight(false);
      //    man->SwitchLight(false);
      //    man->SetCollisionLight(false);
      //  }
      //}
    }

    CHECK(::Serialize(ar, "driverPos", _driverPos, 1, VZero))
    CHECK(::Serialize(ar, "dirWanted", _dirWanted, 1, VZero))

    CHECK(::Serialize(ar, "slingloadForce", _slingloadForce, 1, VZero))

    CHECK(ar.SerializeEnum("moveMode", _moveMode, 1, VMMFormation))
    CHECK(ar.SerializeEnum("turnMode", _turnMode, 1, VTMNone))

    //  CHECK(ar.Serialize("lastStrategicIndex", _lastStrategicIndex, 1, -1))
    //  CHECK(ar.Serialize("useStrategicPlan", _useStrategicPlan, 1, 0))
    //  CHECK(ar.Serialize("strategicPlanValid", _strategicPlanValid, 1, 0))
    // TODO: check and remove  _fireEnabled

    //  CHECK(ar.Serialize("Planner", _planner, 1))
    CHECK(ar.Serialize("Radio", _radio, 1))

    if (ar.IsSaving() || ar.GetArVersion() >= 10)
      CHECK(ar.SerializeEnum("lock", _lock, 1, ENUM_CAST(LockState,LSDefault)))
    else if (ar.GetPass() == ParamArchive::PassFirst)
    {
      bool locked;
      CHECK(ar.Serialize("locked", locked, 1, false))
      _lock = locked ? LSDefault : LSLocked;
    }

    CHECK(ar.Serialize("manualFire", _manualFire, 1, false))
    CHECK(::Serialize(ar, "turretFrontUntil", _turretFrontUntil, 1, TIME_MIN))

    CHECK(ar.Serialize("zoom", _zoom, 1));
  }

  CHECK(ar.Serialize("fuel", FutureVisualState()._fuel, 1, Type()->GetFuelCapacity()))
  CHECK(ar.Serialize("engineOff", _engineOff, 1, false))
  CHECK(ar.Serialize("engineTemperatureCoef", _engineTemperatureCoef, 1, 0.0f))
  CHECK(ar.Serialize("wheelTemperatureCoef", _wheelTemperatureCoef, 1, 0.0f))
  CHECK(ar.Serialize("turretGunTemperatureCoef", _turretGunTemperatureCoef, 1, 0.0f))
  CHECK(::Serialize(ar, "destroyMaxHeatTime", _destroyMaxHeatTime, 1, TIME_MIN))

  CHECK(ar.Serialize("driverHidden", FutureVisualState()._driverHidden, 1, 1.0f))
  CHECK(ar.Serialize("driverHiddenWanted", _driverHiddenWanted, 1, 1.0f))

  CHECK(ar.SerializeEnum("visionMode", _visionMode, 1, ENUM_CAST(VisionMode, VMNormal)))
  CHECK(ar.Serialize("tiIndex", _tiIndex, 1, 0))
  CHECK(ar.Serialize("enableVisionMode", _enableVisionMode, 1, true))

  CHECK(ar.Serialize("allowCrewInImmobile",_allowCrewInImmobile, 1, false))

  CHECK(ar.SerializeRefs("Weapons", _weapons, 1));
  CHECK(ar.SerializeRefs("Magazines", _magazines, 1));
  CHECK(ar.SerializeRef("currentWeapon", _currentWeapon, 1))
  CHECK(_weaponsState.Serialize(this, ar))

  ProgressRefresh();
  return LSOK;
}

#define TRANSPORT_MSG_LIST(XX) \
  XX(UpdateGeneric, UpdateTransport)

DEFINE_NETWORK_OBJECT(Transport, base, TRANSPORT_MSG_LIST)

DEFINE_NET_INDICES_EX_ERR(UpdateTransport, UpdateVehicleAIFull, UPDATE_TRANSPORT_MSG, NoErrorInitialFunc)

NetworkMessageFormat &Transport::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat &format)
{
  switch (cls)
  {
    case NMCUpdateGeneric:
      base::CreateFormat(cls, format);
      UPDATE_TRANSPORT_MSG(UpdateTransport, MSG_FORMAT_ERR)
        break;
    default:
      base::CreateFormat(cls, format);
      break;
  }
  return format;
}

bool IsInVehicle(Person *soldier, Transport *veh)
{
  if (veh->Driver() == soldier) return true;
  TurretContext context;
  if (veh->FindTurret(soldier, context)) return true;
  for (int i=0; i<veh->GetManCargo().Size(); i++)
  {
    if (veh->GetManCargo()[i] == soldier)
      return true;
  }
  return false;
}

/*!
\patch 1.28 Date 10/30/2001 by Ondra
- Fixed: MP: bug in cargo position transfer. Caused problems when
unit from first cargo seat disembarked while someone was still
sitting in second. It could also lead to player animation corruption
or character duplication when player made double attempt to enter vehicle,
thinking first ateempt failed when result was lagged.
\patch_internal 1.28 Date 10/30/2001 by Ondra
- Fixed: GetInCargo can now be position specific
\patch 1.31 Date 11/07/2001 by Jirka
- Fixed: Problem with dead bodies inside vehicle in multiplayer
\patch 1.47 Date 3/7/2002 by Ondra
- Fixed: Different number of cargo slot could crash server or client in MP.
*/

CameraType ValidateCamera(CameraType cam);

TMError Transport::TransferMsg(NetworkMessageContext &ctx)
{
  switch (ctx.GetClass())
  {
    case NMCUpdateGeneric:
      TMCHECK(base::TransferMsg(ctx))
      {
        PREPARE_TRANSFER(UpdateTransport)

        TRANSF(driverLocked)
        if (ctx.IsSending())
        {
          TRANSF_REF(driver)
          TRANSF_REF(copilot)
          TRANSF_REF(effCommander)
          RefArray<Person> manCargo;
          AutoArray<bool> cargoLocked;
          int n = _manCargo.Size();
          manCargo.Resize(n);
          cargoLocked.Resize(n);
          for (int i=0; i<n; i++)
          {
            manCargo[i] = _manCargo[i];
            cargoLocked[i] = _manCargo.IsLocked(i);
          }
          TRANSF_REFS_EX(manCargo, manCargo)
          TRANSF_EX(cargoLocked, cargoLocked)

          AUTO_STATIC_ARRAY(bool,doors,2);
          doors.Resize(FutureVisualState()._doors.Count());
          for (int i=0; i<FutureVisualState()._doors.Count(); i++) doors[i] = FutureVisualState().GetDoorOpen(i);
          TRANSF_EX(doors, doors) 
        }
        else
        {
          OLinkPerm<Person> driver;
          TRANSF_REF_EX(driver, driver)
          TRANSF_REF(copilot)
          // first check position changes
          if (driver != _driver && driver && IsInVehicle(driver, this))
          {
#if DIAG_CREW
            LogF("### Turret update arrived - '%s' found in the vehicle", cc_cast(driver->GetDebugName()));
#endif
            UIActionType type = GetType()->IsKindOf(GWorld->Preloaded(VTypeAir)) ? ATMoveToPilot : ATMoveToDriver;
            Ref<Action> action = new ActionBasic(type, this);
            ChangePosition(action, driver);
          }

          if (driver != _driver)
          {
#if DIAG_CREW
            LogF("### Turret update arrived - '%s' not found in the vehicle", cc_cast(driver ? driver->GetDebugName() : "NULL"));
#endif
            if (_driver)
            {
              // get out
              //GetOutDriver(*_driver);
              AIBrain *unit = _driver->Brain();
              if (unit)
              {
                bool isFocused = (GWorld->FocusOn() == unit);
                unit->DoGetOut(this, true, false);
                if (isFocused)
                  GWorld->SwitchCameraTo(unit->GetVehicle(), ValidateCamera(GWorld->GetCameraType()), false);
              }
              else
              {
                // FIX - enable get out dead body (without brain)
                // calculate get out position
                GetInPoint pt = GetDriverGetOutPos(_driver);
                Matrix4 transform;
                transform.SetPosition(pt._pos);
                transform.SetUpAndDirection(VUp, - pt._dir);
                // get out, use eject (do not turn off the engine - owner will decide)
                GetOutDriver(transform,true);
              }
            }
            if (driver)
            {
              // get in
              GetInDriver(driver, true, true);
              if (GWorld->GetRealPlayer() == driver)
                GWorld->SwitchCameraTo(this, ValidateCamera(GWorld->GetCameraType()), false);
            }
          }

          RefArray<Person> manCargo;
          AutoArray<bool> cargoLocked;
          TRANSF_REFS_EX(manCargo, manCargo)
          TRANSF_EX(cargoLocked, cargoLocked)
          //DoAssert(manCargo.Size() == _manCargo.Size());
          for (int i=0; i<_manCargo.Size(); i++)
          {
            if (i < cargoLocked.Size())
              _manCargo.Lock(i, cargoLocked[i]);
            else
              _manCargo.Lock(i, false);
            OLinkPerm<Person> soldier = i<manCargo.Size() ? manCargo[i].GetRef() : NULL;
            if (soldier != _manCargo[i])
            {
              if (_manCargo[i])
              {
                // get out
                //GetOutCargo(_manCargo[i], *_manCargo[i]);
                AIBrain *unit = _manCargo[i]->Brain();
                if (unit)
                {
                  bool isFocused = (GWorld->FocusOn() == unit);
                  unit->DoGetOut(this, true, false);
                  if (isFocused)
                    GWorld->SwitchCameraTo(unit->GetVehicle(), ValidateCamera(GWorld->GetCameraType()), false);
                }
                else
                {
                  // FIX - enable get out dead body (without brain)
                  // calculate get out position
                  GetInPoint pt = GetCargoGetOutPos(_manCargo[i]);
                  Matrix4 transform;
                  transform.SetPosition(pt._pos);
                  transform.SetUpAndDirection(VUp, - pt._dir);
                  // get out
                  GetOutCargo(_manCargo[i], transform);
                }
              }
              if (soldier)
              {
                // get in
                GetInCargo(soldier,true,i,true);
                if (GWorld->GetRealPlayer() == soldier)
                  GWorld->SwitchCameraTo(this, ValidateCamera(GWorld->GetCameraType()), false);
                AIBrain *unit = soldier->Brain();
                if (unit)
                {
                  unit->SetState(AIBrain::InCargo);
                }
              }
            }
          }

          AUTO_STATIC_ARRAY(bool,doors,2);
          TRANSF_EX(doors, doors)
          for (int i=0; i<FutureVisualState()._doors.Count() && i<doors.Size(); i++) FutureVisualState().SetDoorOpen(i, doors[i]);
        }
        TRANSF_ENUM(lock)
        TRANSF(driverHiddenWanted)
        TRANSF(manualFire)
        if (ctx.IsSending())
        {
          TRANSF(engineOff)
          TRANSF_EX(fuel, FutureVisualState()._fuel)

#if _VBS3 && _EXT_CTRL
          int controller = GetExternalController();
          TRANSF_BASE(HLAController,controller);
#endif
        }
        else
        {
          bool engineOff;
          float fuel;
          TRANSF_EX(engineOff, engineOff)
          TRANSF_EX(fuel, fuel)
          if (engineOff!=_engineOff)
          {
            _engineOff = engineOff;
            OnEvent(EEEngine,_engineOff);
          }
          if ((FutureVisualState()._fuel>0)!=(fuel>0))
            OnEvent(EEFuel,fuel>0);
          FutureVisualState()._fuel = fuel;

#if _VBS3 && _EXT_CTRL
          int controller = 0;
          TRANSF_BASE(HLAController,controller);
          SetExternalController(controller);
#endif
        }
        TRANSF(enableVisionMode)
        TRANSF(allowCrewInImmobile)
        TRANSF(unLockedControls)
        TRANSF(enableCopilots)

        if (ctx.IsSending())
        {
          TRANSF_REFS(weapons)
          TRANSF_REFS(magazines)
        }
        else
        {
          // weapons
          // unassign old weapons (if assigned)
          for (int i=0; i<_weapons.Size(); i++)
          {
            Weapon *weapon = _weapons[i];
            if (weapon)
            {
              // switch owner of the weapon
              weapon->CancelHierarchyParent(this);
            }
          }
          // transfer weapons
          TRANSF_REFS(weapons)
          // assign current weapons
          for (int i=0; i<_weapons.Size(); i++)
          {
            Weapon *weapon = _weapons[i];
            if (weapon)
            {
              // switch owner of the weapon
              weapon->ItemMoveOut(this);
            }
          }
  
          // magazines
          // unassign old magazines
          for (int i=0; i<_magazines.Size(); i++)
          {
            Magazine *magazine = _magazines[i];
            if (magazine)
            {
              // switch owner of the magazine
              magazine->CancelHierarchyParent(this);
            }
          }
          // transfer magazines
          TRANSF_REFS(magazines)
          // assign current magazines
          for (int i=0; i<_magazines.Size(); i++)
          {
            Magazine *magazine = _magazines[i];
            if (magazine)
            {
              // switch owner of the magazine
              magazine->ItemMoveOut(this);
            }
          }
        }
        TRANSF_REF_EX(currentWeapon, _currentWeapon)

      }
      break;
    default:
      return base::TransferMsg(ctx);
  }
  return TMOK;
}

float Transport::CalculateError(NetworkMessageContextWithError &ctx)
{
  float error = 0;
  switch (ctx.GetClass())
  {
    case NMCUpdateGeneric:
      {
        error += base::CalculateError(ctx);

        PREPARE_TRANSFER(UpdateTransport)

        ICALCERR_NEQREF_PERM(Person, driver, ERR_COEF_STRUCTURE)
        ICALCERR_NEQREF_PERM(Person, copilot, ERR_COEF_STRUCTURE)
        /*
        RefArray<Person> manCargo;
        if (TRANSF_REFS_BASE(manCargo, manCargo) == TMOK)
        {
        const float coef = 0.5 * ERR_COEF_STRUCTURE;
        for (int i=0; i<_manCargo.Size(); i++)
        {
        Person *soldier = i<manCargo.Size() ? manCargo[i] : NULL;
        if (soldier != _manCargo[i]) error += coef;
        }
        }
        */
        const float coef = 0.5 * ERR_COEF_STRUCTURE;
        for (int i=0; i<_manCargo.Size(); i++)
        {
          NetworkId id = i < message->_manCargo.Size() ? message->_manCargo[i] : NetworkId::Null();
          if (_manCargo[i])
          {
            if (_manCargo[i]->GetNetworkId() != id)
              error += coef;
          }
          else
          {
            if (!id.IsNull())
              error += coef;
          }
        }

        ICALCERR_NEQ(int, lock, ERR_COEF_STRUCTURE)
        ICALCERR_ABSDIF(float, driverHiddenWanted, ERR_COEF_VALUE_MAJOR)
        {
          error += ERR_COEF_VALUE_MINOR * fabs(message->_fuel - FutureVisualState()._fuel);
        }
        ICALCERR_NEQ(bool, engineOff, ERR_COEF_MODE)
        ICALCERR_NEQ(bool, manualFire, ERR_COEF_MODE)
        ICALCERR_NEQ(bool, enableVisionMode, ERR_COEF_MODE)
        ICALCERR_NEQ(bool, allowCrewInImmobile, ERR_COEF_MODE)
        ICALCERR_NEQ(bool,unLockedControls, ERR_COEF_MODE)
        ICALCERR_NEQ(bool,enableCopilots, ERR_COEF_MODE)

        // weapons
        int count = intMin(message->_weapons.Size(), _weapons.Size());
        int dif = (message->_weapons.Size() - count) + (_weapons.Size() - count);
        for (int i=0; i<count; i++)
        {
          if (_weapons[i])
          {
            if (message->_weapons[i] != _weapons[i]->GetNetworkId()) dif++;
          }
          else
          {
            if (!message->_weapons[i].IsNull()) dif++;
          }
        }
        error += ERR_COEF_MODE * dif;

        // magazines
        count = intMin(message->_magazines.Size(), _magazines.Size());
        dif = (message->_magazines.Size() - count) + (_magazines.Size() - count);
        for (int i=0; i<count; i++)
        {
          if (_magazines[i])
          {
            if (message->_magazines[i] != _magazines[i]->GetNetworkId()) dif++;
          }
          else
          {
            if (!message->_magazines[i].IsNull()) dif++;
          }
        }
        error += ERR_COEF_MODE * dif;
  
        ICALCERRE_NEQREF_SOFT(Weapon, currentWeapon, _currentWeapon, ERR_COEF_MODE)
      }
      break;
    default:
      error += base::CalculateError(ctx);
      break;
  }
  return error;
}

bool Transport::ChildUpdateByParent(const Entity *obj) const
{
  // crew should be updated together with the vehicle
  DoAssert(dyn_cast<const Man>(obj)); // if there are other transport children than a crew, handle them as appropriate for them
  return true;
}

void Transport::CreateSubobjects()
{
  // turrets
  TurretPath path;
  for (int i=0; i<_turrets.Size(); i++)
  {
    path.Resize(1);
    path[0] = i;
    _turrets[i]->CreateSubobjects(this, path);
  }

  // weapons
  for (int i=0; i<_weapons.Size(); i++)
  {
    Weapon *weapon = _weapons[i];
    if (weapon) GetNetworkManager().CreateVehicle(weapon, VLTOut, "", -1);
  }
  // magazines
  for (int i=0; i<_magazines.Size(); i++)
  {
    Magazine *magazine = _magazines[i];
    if (magazine) GetNetworkManager().CreateVehicle(magazine, VLTOut, "", -1);
  }
}

Time Transport::GetDestroyedTime() const
{
  if (!_isDead)
    return TIME_MAX;

  if (_explosionTime<Glob.time+60)
  {
    if (_whenDestroyed<_explosionTime)
      return _whenDestroyed;
    return _explosionTime;
  }
  return base::GetDestroyedTime();
}

/*!
\patch 1.37 Date 12/18/2001 by Jirka
- Fixed: MP parachute - get out units on clients when parachute destroyed
*/
void Transport::DestroyObject()
{
  if (Observer())
  {
    AIBrain *unit = Observer()->Brain();
    if (unit)
    {
      // get out
      bool isFocused = (GWorld->FocusOn() == unit);
      unit->DoGetOut(this, true, false);
      if (isFocused)
        GWorld->SwitchCameraTo(unit->GetVehicle(), ValidateCamera(GWorld->GetCameraType()), false);
    }
    else
    {
      Ref<Person> soldier = Observer();
      // calculate get out position
      GetInPoint pt = GetCommanderGetOutPos(Observer());
      Matrix4 transform;
      transform.SetPosition(pt._pos);
      transform.SetUpAndDirection(VUp, - pt._dir);
      // get out
      GetOutCommander(transform);
      // switch camera if dead player gets out
      if (GWorld->CameraOn() == this && GWorld->PlayerOn() == soldier)
      {
        GWorld->SwitchCameraTo(soldier, ValidateCamera(GWorld->GetCameraType()), false);
      }
    }
  }
  if (_driver)
  {
    AIBrain *unit = _driver->Brain();
    if (unit)
    {
      // get out
      bool isFocused = (GWorld->FocusOn() == unit);
      unit->DoGetOut(this, true, false);
      if (isFocused)
        GWorld->SwitchCameraTo(unit->GetVehicle(), ValidateCamera(GWorld->GetCameraType()), false);
    }
    else
    {
      Ref<Person> soldier = _driver;
      // calculate get out position
      GetInPoint pt = GetDriverGetOutPos(_driver);
      Matrix4 transform;
      transform.SetPosition(pt._pos);
      transform.SetUpAndDirection(VUp, - pt._dir);
      // get out - do not turn off the engine
      GetOutDriver(transform,true);
      // switch camera if dead player gets out
      if (GWorld->CameraOn() == this && GWorld->PlayerOn() == soldier)
        GWorld->SwitchCameraTo(soldier, ValidateCamera(GWorld->GetCameraType()), false);
    }
  }
  if (Gunner())
  {
    AIBrain *unit = Gunner()->Brain();
    if (unit)
    {
      // get out
      bool isFocused = (GWorld->FocusOn() == unit);
      unit->DoGetOut(this, true, false);
      if (isFocused)
        GWorld->SwitchCameraTo(unit->GetVehicle(), ValidateCamera(GWorld->GetCameraType()), false);
    }
    else
    {
      Ref<Person> soldier = Gunner();
      // calculate get out position
      GetInPoint pt = GetGunnerGetOutPos(Gunner());
      Matrix4 transform;
      transform.SetPosition(pt._pos);
      transform.SetUpAndDirection(VUp, - pt._dir);
      // get out
      GetOutGunner(transform);
      // switch camera if dead player gets out
      if (GWorld->CameraOn() == this && GWorld->PlayerOn() == soldier)
        GWorld->SwitchCameraTo(soldier, ValidateCamera(GWorld->GetCameraType()), false);
    }
  }
  for (int i=0; i<_manCargo.Size(); i++)
  {
    if (_manCargo[i])
    {
      AIBrain *unit = _manCargo[i]->Brain();
      if (unit)
      {
        // get out
        bool isFocused = (GWorld->FocusOn() == unit);
        unit->DoGetOut(this, true, false);
        if (isFocused)
          GWorld->SwitchCameraTo(unit->GetVehicle(), ValidateCamera(GWorld->GetCameraType()), false);
      }
      else
      {
        Ref<Person> soldier = _manCargo[i];
        // calculate get out position
        GetInPoint pt = GetCargoGetOutPos(_manCargo[i]);
        Matrix4 transform;
        transform.SetPosition(pt._pos);
        transform.SetUpAndDirection(VUp, - pt._dir);
        // get out
        GetOutCargo(_manCargo[i], transform);
        // switch camera if dead player gets out
        if (GWorld->CameraOn() == this && GWorld->PlayerOn() == soldier)
          GWorld->SwitchCameraTo(soldier, ValidateCamera(GWorld->GetCameraType()), false);
      }
    }
  }

  //there can still remain some units in the vehicle's turrets
  //get them also out
  class EjectRemainingFunc: public ICrewFunc
  {
    Transport* _veh;
  public:
    EjectRemainingFunc(Transport* veh){_veh = veh;}
    virtual bool operator () (Person *entity)
    {
      AIBrain *unit = entity->Brain();
      if (unit)
      {
        bool isFocused = (GWorld->FocusOn() == unit);
        unit->DoGetOut(_veh, true, false);
        if (isFocused)
          GWorld->SwitchCameraTo(unit->GetVehicle(), ValidateCamera(GWorld->GetCameraType()), false);
      }
      return false;
    }
  };
  EjectRemainingFunc func(this);
  ForEachCrew(func);

  base::DestroyObject();
}

void Transport::SimulateImpulseDamage(DoDamageResult &result, EntityAI *owner, float impulse, Vector3Par modelPos)
{
  float fInvMass = GetInvMass();
  float damage = impulse * fInvMass * 0.06f + impulse * fInvMass * impulse * fInvMass * 0.024;
  float damageObj = damage * (0.01f + GetInvArmor());

  if (damageObj > 0.05f)
    LocalDamage(result, ShotInfo(NULL,owner,this, modelPos), damageObj, GetRadius() * 0.5);
  damage *= (0.03f + GetInvArmor())/2;
  if (damage > 0.03f && IsLocal())
    DamageCrew(owner, damage , "");
}

Turret *Transport::GetTurret(const int *path, int size)
{
  if (size <= 0)
  {
    LogF("Empty turret path");
    return NULL;
  }
  int i = path[0];
  if (i<0 || i>=_turrets.Size())
  {
    LogF("Invalid turret path");
    return NULL;
  }
  return _turrets[i]->GetTurret(path + 1, size - 1);
}

/*!
\patch 5163 Date 6/1/2007 by Ondra
- Fixed: Scripting function person moveInTurret [vehicle,[x,y,z]] crashed when [x,y,z] was invalid.
*/
const Turret *Transport::GetTurret(const int *path, int size) const
{
  if (size <= 0)
  {
    LogF("Empty turret path");
    return NULL;
  }
  int i = path[0];
  if (i<0 || i>=_turrets.Size())
  {
    LogF("Invalid turret path");
    return NULL;
  }
  return _turrets[i]->GetTurret(path + 1, size - 1);
}

float AnimationSourceDoor::GetPhase(const ObjectVisualState &vs, const Animated *obj) const
{
  if (_index<0) return 0;
  float door = static_cast_checked<TransportVisualState const&>(vs).GetDoorState(_index);
  return door;
}

AnimationSource *DoorsHolder::CreateAnimationSource(ParamEntryPar cfg)
{
  AnimationSourceDoor *source = new AnimationSourceDoor(cfg.GetName(),_sources.Size(),cfg >> "animPeriod");
  _sources.Add(source);
  return source;
}


int DoorsHolder::GetDoorIndex(const char *name) const
{
  for( int i=0; i<_sources.Size(); i++ ) 
    if (stricmp(name,_sources[i]->GetName())==0)
      return _sources[i]->GetIndex();   
  return -1;
};

void DoorsState::Init(const DoorsHolder &type)
{
  _door.Init(type.Count());
  for( int i=0; (size_t)i<_door.Size(); i++ ) 
  {
    _door[i]._pos=0.0f;
    _door[i]._open=false;
  }
}

void DoorsState::Interpolate(DoorsState const& t1state, float t, DoorsState& res) const
{
  Assert(_door.Size()==res._door.Size());
  Assert(_door.Size()==t1state._door.Size());
  for (int i=0; (size_t)i < _door.Size(); i++)
  {
    res._door[i]._pos = _door[i]._pos + t*(t1state._door[i]._pos - _door[i]._pos);
    res._door[i]._open = t1state._door[i]._open || _door[i]._open;
  }
}
